#!/bin/bash
# Stop the services started by ./start.sh.
cd "$(dirname "$0")"
for name in core inference; do
  if [ -f "logs/$name.pid" ] && kill "$(cat "logs/$name.pid")" 2>/dev/null; then echo "stopped $name"; fi
  rm -f "logs/$name.pid"
done
