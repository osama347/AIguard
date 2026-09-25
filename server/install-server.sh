#!/usr/bin/env bash
# Installs the Guard++ server on this Jetson and prints how to connect.
#
#   sudo ./install-server.sh                       newest guard-server_*.deb from ../dist (or next to this script)
#   sudo ./install-server.sh /path/guard-server_2.5.0_arm64.deb
#   sudo ./install-server.sh /path/guard-server-2.5.0-jp6.2-offline.run     (no internet)
set -euo pipefail

[ "$(id -u)" = 0 ] || { echo "Run with sudo:  sudo $0 $*" >&2; exit 1; }
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"

PKG="${1:-}"
if [ -z "$PKG" ]; then
    PKG="$(ls -1 "$HERE"/../dist/guard-server_*_arm64.deb "$HERE"/guard-server_*_arm64.deb 2>/dev/null | sort -V | tail -n1 || true)"
    [ -n "$PKG" ] || { echo "No guard-server_*_arm64.deb found. Pass the file as an argument." >&2; exit 1; }
fi
[ -f "$PKG" ] || { echo "File not found: $PKG" >&2; exit 1; }
PKG="$(readlink -f "$PKG")"

echo "==> Installing $(basename "$PKG")"
case "$PKG" in
    *.run) bash "$PKG" ;;
    *.deb) apt-get install -y --reinstall "$PKG" ;;
    *) echo "Expected a .deb or .run file" >&2; exit 1 ;;
esac

echo "==> Waiting for the server to answer"
for _ in $(seq 1 30); do
    if curl -fsS http://127.0.0.1:8090/api/v1/health >/dev/null 2>&1; then UP=1; break; fi
    sleep 2
done
[ "${UP:-0}" = 1 ] || echo "The server did not answer yet. Check: journalctl -fu guard-core"

CODE="$(grep '^GUARD_SETUP_CODE=' /etc/guard/guard.env 2>/dev/null | cut -d= -f2 || true)"
echo
echo "Guard++ server installed."
echo "  Connect the desktop app to:"
for ip in $(hostname -I 2>/dev/null); do echo "      http://$ip:8090"; done
echo "  First-time setup (your community details and the administrator account):"
echo "      open the desktop app and follow the setup screen, or http://localhost:8090 on this Jetson."
[ -n "$CODE" ] && echo "  Setup code (needed only when setting up from another PC): $CODE"
echo "  The first start prepares the AI models and can take up to ~20 minutes: journalctl -fu guard-inference"
