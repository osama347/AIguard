#!/bin/bash
# Developer run: start guard-inference and guard-core from this checkout.
#   ./start.sh      (builds first if needed)      ./stop.sh
# Web UI: http://localhost:8090   Logs: logs/   Data: data/
# Production devices use the .deb package instead (see packaging/).
# Run from server/.
set -e
cd "$(dirname "$0")"

if [ ! -x build/core/guard-core ] || [ ! -x build/inference/guard-inference ]; then
  cmake -B build -S . -DAI_USE_TENSORRT=ON >/dev/null
  cmake --build build -j"$(nproc)" --target guard-core guard-inference
fi
if [ ! -f ../frontend/dist/index.html ]; then
  (cd ../frontend && npm ci && npm run build)
fi

mkdir -p data logs
[ -s data/dev.env ] || printf 'GUARD_INFERENCE_SECRET=%s\n' "$(od -An -tx1 -N16 /dev/urandom | tr -d ' \n')" > data/dev.env
set -a; . data/dev.env; set +a

for port in 8081 8090; do
  if ss -ltn | grep -q ":$port\b"; then echo "Port $port is busy (./stop.sh?)"; exit 1; fi
done

nohup build/inference/guard-inference inference/config/inference.json > logs/inference.log 2>&1 &
echo $! > logs/inference.pid
nohup build/core/guard-core core/config/core.json > logs/core.log 2>&1 &
echo $! > logs/core.pid

sleep 1
curl -s http://127.0.0.1:8090/api/v1/health; echo
echo "Web UI: http://localhost:8090  (AI models may take a while to load on first start: tail -f logs/inference.log)"
