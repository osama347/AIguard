#!/bin/bash
# ---------------------------------------------------------------------------
# Build the offline installer: dist/guard-server-<version>-jp6.2-offline.run
#
# A self-extracting script containing guard-server_<version>_arm64.deb plus every
# package it depends on, laid out as a local apt repository. On the target it
# points apt at that repository only, so apt installs just what is missing —
# no internet needed.
#
#   server/packaging/make-offline.sh                    (after build-deb.sh)
#   server/packaging/make-offline.sh --with-jetpack     also bundle CUDA/TensorRT/L4T
#                                                packages (large; only for
#                                                devices flashed without them)
# ---------------------------------------------------------------------------
set -euo pipefail

SERVER="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="$(dirname "$SERVER")"
VERSION="$(sed -n 's/^project(GuardCore VERSION \([0-9.]*\).*/\1/p' "$SERVER/core/CMakeLists.txt")"
DEB="$ROOT/dist/guard-server_${VERSION}_arm64.deb"
WORK="$SERVER/build/offline"
RUN="$ROOT/dist/guard-server-${VERSION}-jp6.2-offline.run"
WITH_JETPACK=0
[ "${1:-}" = "--with-jetpack" ] && WITH_JETPACK=1

[ -f "$DEB" ] || { echo "missing $DEB — run server/packaging/build-deb.sh first" >&2; exit 1; }
command -v dpkg-scanpackages >/dev/null || { echo "dpkg-scanpackages missing: sudo apt install dpkg-dev" >&2; exit 1; }

rm -rf "$WORK"
mkdir -p "$WORK/repo"
cp "$DEB" "$WORK/repo/"

# Direct dependencies of the package (names only, alternatives flattened).
roots="$(dpkg-deb -f "$DEB" Depends | tr ',|' '\n\n' | sed 's/(.*)//; s/[[:space:]]//g' | grep -v '^$' | sort -u)"

# Full dependency closure, without recommends/suggests.
closure="$(apt-cache depends --recurse --no-recommends --no-suggests --no-conflicts --no-breaks \
             --no-replaces --no-enhances $roots 2>/dev/null | grep -E '^[a-z0-9]' | sort -u)"

if [ "$WITH_JETPACK" = 0 ]; then
    # Tied to the device's JetPack/L4T image: must already be installed and matching.
    closure="$(echo "$closure" | grep -Ev '^(nvidia-l4t-|cuda-|libcu|libnv|libnvinfer|libnvonnx|tensorrt|libcudnn)')"
fi

# Keep only real packages, and drop the base system (Priority required/important
# is present on every Ubuntu/L4T image).
closure="$(apt-cache show --no-all-versions $closure 2>/dev/null | awk '
    /^Package:/  { pkg = $2 }
    /^Priority:/ { prio = $2 }
    /^$/         { if (pkg != "" && prio != "required" && prio != "important") print pkg; pkg = ""; prio = "" }
    END          { if (pkg != "" && prio != "required" && prio != "important") print pkg }' | sort -u)"

echo "Downloading $(echo "$closure" | wc -l) packages..."
(
    cd "$WORK/repo"
    # Batches for speed; a failed batch is retried one by one so a single
    # unavailable package cannot drop the others.
    echo "$closure" | xargs -n 25 sh -c 'apt-get download -qq "$@" 2>/dev/null || for p in "$@"; do apt-get download -qq "$p" 2>/dev/null || echo "  skipped $p"; done' _
)
(cd "$WORK/repo" && dpkg-scanpackages --multiversion . /dev/null 2>/dev/null > Packages && gzip -9k Packages)
echo "Repository: $(ls "$WORK/repo"/*.deb | wc -l) packages, $(du -sh "$WORK/repo" | cut -f1)"

cat > "$WORK/header.sh" <<'EOF'
#!/bin/bash
# Guard++ offline installer. Usage: sudo bash guard-server-<version>-jp6.2-offline.run
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "Please run with sudo: sudo bash $0" >&2; exit 1; }
TMP="$(mktemp -d /tmp/guard-installer.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
echo "Extracting Guard++ installer..."
tail -n +"$(awk '/^__PAYLOAD_BELOW__$/{print NR + 1; exit}' "$0")" "$0" | tar -xz -C "$TMP"
chmod -R a+rX "$TMP"                     # apt's sandbox user must read the repository
mkdir -p "$TMP/lists/partial"
echo "deb [trusted=yes] file:$TMP/repo ./" > "$TMP/guard.list"
APT=(apt-get -o "Dir::Etc::sourcelist=$TMP/guard.list" -o "Dir::Etc::sourceparts=-"
     -o "Dir::State::Lists=$TMP/lists" -o "APT::Get::List-Cleanup=0" -o "APT::Sandbox::User=root")
"${APT[@]}" update -qq
"${APT[@]}" install -y guard-server
echo "Done."
exit 0
__PAYLOAD_BELOW__
EOF

(cat "$WORK/header.sh"; tar -C "$WORK" -czf - repo) > "$RUN"
chmod 755 "$RUN"
ls -lh "$RUN"
echo "Install on an offline Jetson with:  sudo bash $(basename "$RUN")"
