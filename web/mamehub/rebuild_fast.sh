#!/usr/bin/env bash
# Incremental wasm rebuild for browser iteration.
#   ./web/mamehub/rebuild_fast.sh              # default SUBTARGET=snes
#   SUBTARGET=arcade ./web/mamehub/rebuild_fast.sh
#   ./web/mamehub/rebuild_fast.sh nes genesis
#   ./web/mamehub/rebuild_fast.sh all          # every unique subtarget in profiles.json
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

export EMSCRIPTEN="${EMSCRIPTEN:-/opt/homebrew/opt/emscripten/libexec}"
export PATH="/opt/homebrew/opt/emscripten/bin:${PATH}"

JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}"
GEN="$ROOT/scripts/mamehub/gen_browser_profiles.py"

targets=()
if [[ "${1:-}" == "all" ]]; then
  while IFS= read -r st; do
    [[ -n "$st" ]] && targets+=("$st")
  done < <(python3 "$GEN" subtargets all)
elif [[ $# -gt 0 ]]; then
  targets=("$@")
else
  targets=("${SUBTARGET:-snes}")
fi

if [[ ${#targets[@]} -eq 0 ]]; then
  echo "no subtargets" >&2
  exit 1
fi

build_one() {
  local st="$1"
  local regen="${REGENIE:-0}"
  # Switching subtarget, or a first-time binary, needs a project regen.
  if [[ ${#targets[@]} -gt 1 || ! -f "mame${st}hub.wasm" ]]; then
    regen=1
  fi
  local make_args=(SUBTARGET="$st" WEBASSEMBLY=1 -j"$JOBS")
  if [[ "$regen" == "1" ]]; then
    make_args+=(REGENIE=1)
    echo "==> build SUBTARGET=$st (REGENIE=1 -j$JOBS; Asyncify link -O2)"
  else
    echo "==> build SUBTARGET=$st (no-REGENIE -j$JOBS; Asyncify link -O2)"
  fi
  time emmake make "${make_args[@]}"
  local bin="mame${st}hub"
  mkdir -p web/mamehub/dist
  if [[ ! -f "${bin}.js" || ! -f "${bin}.wasm" ]]; then
    echo "missing ${bin}.js / ${bin}.wasm after build" >&2
    exit 1
  fi
  cp -f "${bin}.js" "${bin}.wasm" web/mamehub/dist/
  echo "==> copied to web/mamehub/dist/ ($(du -h "web/mamehub/dist/${bin}.wasm" | awk '{print $1}'))"
}

for st in "${targets[@]}"; do
  build_one "$st"
done

echo "Hard-refresh http://127.0.0.1:8765/ (or bump app.js ?v=)"
echo "Landing lists every profile in web/mamehub/profiles.json"
