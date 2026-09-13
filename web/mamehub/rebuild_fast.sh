#!/usr/bin/env bash
# Incremental wasm rebuild for browser iteration.
# Default SUBTARGET=snes (mamesneshub.*). Use SUBTARGET=arcade for mamearcadehub.*.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

export EMSCRIPTEN="${EMSCRIPTEN:-/opt/homebrew/opt/emscripten/libexec}"
export PATH="/opt/homebrew/opt/emscripten/bin:${PATH}"

SUBTARGET="${SUBTARGET:-snes}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}"
# Link must be -O2 with Asyncify or browsers reject the wasm (local count too large).
# Avoid flipping OPTIMIZE= between builds (forces mass recompile).
MAKE_ARGS=(SUBTARGET="$SUBTARGET" WEBASSEMBLY=1 -j"$JOBS")
if [[ "${REGENIE:-0}" == "1" ]]; then
  MAKE_ARGS+=(REGENIE=1)
  echo "==> build SUBTARGET=$SUBTARGET (REGENIE=1 -j$JOBS; Asyncify link -O2)"
else
  echo "==> build SUBTARGET=$SUBTARGET (no-REGENIE -j$JOBS; Asyncify link -O2)"
fi

time emmake make "${MAKE_ARGS[@]}"

BIN="mame${SUBTARGET}hub"
mkdir -p web/mamehub/dist
if [[ ! -f "${BIN}.js" || ! -f "${BIN}.wasm" ]]; then
  echo "missing ${BIN}.js / ${BIN}.wasm after build" >&2
  exit 1
fi
cp -f "${BIN}.js" "${BIN}.wasm" web/mamehub/dist/
echo "==> copied to web/mamehub/dist/ ($(du -h "web/mamehub/dist/${BIN}.wasm" | awk '{print $1}'))"
echo "Hard-refresh http://127.0.0.1:8765/ (or bump app.js ?v=)"
if [[ "$SUBTARGET" == "snes" ]]; then
  echo "SNES dist left as mamesneshub.*; arcade builds use SUBTARGET=arcade (mamearcadehub.*)."
fi
