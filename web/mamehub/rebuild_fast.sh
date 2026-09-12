#!/usr/bin/env bash
# Incremental SNES wasm rebuild for browser iteration.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

export EMSCRIPTEN="${EMSCRIPTEN:-/opt/homebrew/opt/emscripten/libexec}"
export PATH="/opt/homebrew/opt/emscripten/bin:${PATH}"

JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}"
# Link must be -O2 with Asyncify or browsers reject the wasm (local count too large).
# Avoid flipping OPTIMIZE= between builds (forces mass recompile).
MAKE_ARGS=(SUBTARGET=snes WEBASSEMBLY=1 -j"$JOBS")
if [[ "${REGENIE:-0}" == "1" ]]; then
  MAKE_ARGS+=(REGENIE=1)
  echo "==> build (REGENIE=1 -j$JOBS; Asyncify link -O2)"
else
  echo "==> build (no-REGENIE -j$JOBS; Asyncify link -O2)"
fi

time emmake make "${MAKE_ARGS[@]}"

mkdir -p web/mamehub/dist
cp -f mamesneshub.js mamesneshub.wasm web/mamehub/dist/
echo "==> copied to web/mamehub/dist/ ($(du -h web/mamehub/dist/mamesneshub.wasm | awk '{print $1}'))"
echo "Hard-refresh http://127.0.0.1:8765/ (or bump app.js ?v=)"
