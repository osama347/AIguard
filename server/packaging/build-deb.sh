#!/bin/bash
# ---------------------------------------------------------------------------
# Build guard-server_<version>_arm64.deb — the Jetson-side installer package
# (AI engine + database/API as background services). The desktop app is built
# separately: see desktop/README.md.
#
# Run natively on a JetPack 6.2 Jetson (CUDA/TensorRT must match the targets):
#   server/packaging/build-deb.sh
#   JOBS=2 server/packaging/build-deb.sh   limit parallel compile jobs (lower peak power draw)
#
# Output: dist/guard-server_<version>_arm64.deb
#
# Prebuilt TensorRT engines are taken from data/engines/ when present (run the
# services once on this machine to create them). Targets whose TensorRT/GPU
# match reuse them; others rebuild from the bundled ONNX files on first start.
# ---------------------------------------------------------------------------
set -euo pipefail

SERVER="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="$(dirname "$SERVER")"
VERSION="$(sed -n 's/^project(GuardCore VERSION \([0-9.]*\).*/\1/p' "$SERVER/core/CMakeLists.txt")"
PKG="guard-server_${VERSION}_arm64"
STAGE="$SERVER/build/pkg/$PKG"
OUT="$ROOT/dist"

step() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(uname -m)" = "aarch64" ] || die "build on the Jetson itself (aarch64); cross-compiling is not supported"
command -v dpkg-deb >/dev/null || die "dpkg-deb not found"

if [ -s "$HOME/.nvm/nvm.sh" ]; then export NVM_DIR="$HOME/.nvm"; . "$NVM_DIR/nvm.sh" >/dev/null; fi
[ -d "$HOME/.cargo/bin" ] && export PATH="$HOME/.cargo/bin:$PATH"
command -v npm >/dev/null || die "npm not found (install Node.js 20+)"

# ------------------------------------------------------------------ build
step "Building guard-core and guard-inference (Release, TensorRT)"
cmake -S "$SERVER" -B "$SERVER/build" -DCMAKE_BUILD_TYPE=Release -DAI_USE_TENSORRT=ON >/dev/null
cmake --build "$SERVER/build" -j"${JOBS:-$(nproc)}" --target guard-core guard-inference guard-core-tests

step "Running guard-core tests"
"$SERVER/build/core/guard-core-tests" 2>/dev/null | tail -n1

step "Building frontend"
(cd "$ROOT/frontend" && npm ci --no-audit --no-fund >/dev/null && npm run build >/dev/null)

# ------------------------------------------------------------------ stage
step "Staging package tree"
rm -rf "$STAGE"
install -d "$STAGE/DEBIAN" "$STAGE/opt/guard/bin" "$STAGE/opt/guard/share" "$STAGE/opt/guard/models/prebuilt" \
           "$STAGE/etc/guard" "$STAGE/lib/systemd/system" "$STAGE/etc/avahi/services"

install -m755 "$SERVER/build/core/guard-core" "$SERVER/build/inference/guard-inference" "$STAGE/opt/guard/bin/"
strip "$STAGE/opt/guard/bin/guard-core" "$STAGE/opt/guard/bin/guard-inference"
cp -r "$ROOT/frontend/dist" "$STAGE/opt/guard/share/frontend"
cp -r "$SERVER/core/migrations" "$STAGE/opt/guard/share/migrations"
install -Dm644 "$SERVER/README.md" "$STAGE/opt/guard/share/doc/README.md"
install -m644 "$ROOT/api/openapi.yaml" "$STAGE/opt/guard/share/doc/openapi.yaml"

# Models: ONNX sources (always) + engines built on this machine (optional fast path).
for onnx in $(python3 -c "import json;[print(m['onnx'].split('/')[-1]) for m in json.load(open('$SERVER/packaging/config/inference.json'))['models'].values()]"); do
    install -m644 "$SERVER/inference/models/$onnx" "$STAGE/opt/guard/models/"
done
if compgen -G "$SERVER/data/engines/*.engine" >/dev/null; then
    for e in "$SERVER"/data/engines/*.engine; do
        [ -f "$e.json" ] && install -m644 "$e" "$e.json" "$STAGE/opt/guard/models/prebuilt/"
    done
    echo "  prebuilt engines: $(ls "$STAGE/opt/guard/models/prebuilt" | grep -c '\.engine$')"
else
    echo "  no prebuilt engines (targets will build them on first start)"
fi

install -m644 "$SERVER/packaging/avahi/guard.service" "$STAGE/etc/avahi/services/"
install -m644 "$SERVER/packaging/config/core.json" "$SERVER/packaging/config/inference.json" "$STAGE/etc/guard/"
install -m644 "$SERVER/packaging/systemd/"*.service "$STAGE/lib/systemd/system/"
install -m755 "$SERVER/packaging/debian/"{preinst,postinst,prerm,postrm} "$STAGE/DEBIAN/"
install -m644 "$SERVER/packaging/debian/conffiles" "$STAGE/DEBIAN/"

SIZE_KB="$(du -sk --exclude=DEBIAN "$STAGE" | cut -f1)"
sed -e "s/@VERSION@/$VERSION/" -e "s/@INSTALLED_SIZE@/$SIZE_KB/" "$SERVER/packaging/debian/control.in" > "$STAGE/DEBIAN/control"
(cd "$STAGE" && find . -type f ! -path './DEBIAN/*' -exec md5sum {} + | sed 's| \./| |' > DEBIAN/md5sums)

# ------------------------------------------------------------------ package
step "Building $PKG.deb"
mkdir -p "$OUT"
fakeroot dpkg-deb --build -Zxz "$STAGE" "$OUT/$PKG.deb" >/dev/null
dpkg-deb --info "$OUT/$PKG.deb" | sed -n '/Package:/,/Depends:/p'
ls -lh "$OUT/$PKG.deb"
echo "Install on a Jetson with:  sudo apt install ./$PKG.deb"
