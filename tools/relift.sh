#!/bin/bash
# Full lift (seeds changed) + incremental build. Logs in build/regen.log, build/build.log.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
export PATH="$ROOT/.venv/bin:/opt/homebrew/bin:$PATH"
bash engine/tools/regen.sh > build/regen.log 2>&1 || { tail -20 build/regen.log; exit 1; }
grep -E "Translating|unresolved" build/regen.log
python3 tools/stubcheck.py > build/stubcheck.log 2>&1 || { tail -12 build/stubcheck.log; exit 1; }   # a real-code stub is a silent bug
tail -1 build/stubcheck.log
cmake --build build/release > build/build.log 2>&1 || { grep -E "error" build/build.log | head; exit 1; }
echo "build OK"
