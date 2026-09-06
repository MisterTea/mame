#!/bin/bash
# Shared helpers for MAMEHub macOS e2e harnesses.
# Sourced by run_*.sh scripts; do not execute directly.

e2e_init_paths() {
  if [[ -n "${E2E_SCRIPT_DIR:-}" ]]; then
    SCRIPT_DIR="$E2E_SCRIPT_DIR"
  else
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[1]}")" && pwd)"
  fi
  ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
  BIN="${MAMEHUB_BIN:-$ROOT/mamehub}"
  ROMPATH="${MAMEHUB_ROMPATH:-roms}"
  OUT="${MAMEHUB_E2E_OUT:-/tmp/mamehub-e2e}"
  MOCK="${MAMEHUB_MOCK:-/tmp/mamehub_mock}"
  MASH_SECS="${MASH_SECS:-300}"
  TOOLS_BIN="$SCRIPT_DIR/tools/bin"
  FK="$TOOLS_BIN/focus_key"
  CH="$TOOLS_BIN/click_highlight"
  PAD="$TOOLS_BIN/pad"
  export MAMEHUB_E2E_OUT="$OUT"
}

e2e_require_macos() {
  if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "FAIL: this e2e harness requires macOS (CGEvent / screencapture automation)"
    exit 1
  fi
  if [[ ! -x "$BIN" ]]; then
    echo "FAIL: mamehub binary not found at $BIN (build mamehub or set MAMEHUB_BIN)"
    exit 1
  fi
  if ! command -v rg >/dev/null 2>&1; then
    echo "FAIL: ripgrep (rg) is required"
    exit 1
  fi
  "$SCRIPT_DIR/tools/build.sh" "$TOOLS_BIN"
}

e2e_require_rom() {
  # Never block on missing zips. Candy mode (-candy, default on) fetches on load.
  local game=$1
  local cand
  for cand in "$ROOT/$ROMPATH/${game}.zip" "$ROOT/$ROMPATH/${game}.7z" \
              "$ROMPATH/${game}.zip" "$ROMPATH/${game}.7z"; do
    if [[ -f "$cand" ]]; then
      echo "rom found: $cand"
      return 0
    fi
  done
  echo "rom '$game' not local; candy mode will download on-the-fly"
}

e2e_shot() { screencapture -x "$OUT/$1.png" || true; }
e2e_alive() { kill -0 "$1" 2>/dev/null; }
e2e_die() {
  echo "FAIL: $*"
  local f
  for f in "$OUT"/p*.log "$OUT"/host.log "$OUT"/join.log; do
    [[ -f "$f" ]] && { echo "--- $(basename "$f") ---"; tail -30 "$f"; }
  done
  exit 1
}

e2e_select_highlight() {
  local pid=$1
  local row=${2:-0}
  local tries=${3:-5}
  local i
  for i in $(seq 1 "$tries"); do
    if "$CH" "$pid" "$row" 1; then
      return 0
    fi
    echo "select_highlight retry $i for pid=$pid row=$row"
    sleep 1
  done
  return 1
}

e2e_place() {
  local pid=$1 x=$2 y=$3
  osascript -e "tell application \"System Events\" to set position of window 1 of (first process whose unix id is $pid) to {$x, $y}" 2>/dev/null || true
}

e2e_check_desync() {
  if rg -qi 'INPUT DESYNC|Fatal log|Aborting application' "$OUT"/*.log "$ROOT/MAMEHub.log" 2>/dev/null; then
    e2e_die "DESYNC or FATAL"
  fi
}

e2e_cleanup() {
  pkill -f "$BIN" 2>/dev/null || true
  sleep 0.8
  rm -rf "$MOCK"
  mkdir -p "$OUT" "$MOCK"
  rm -f "$ROOT/MAMEHub.log" "$OUT"/*.png "$OUT"/*.log "$OUT"/*.err
}

# Port plan: peer=BASE_PEER+i, dir=BASE_DIR+i (no collisions across N instances)
e2e_peer_port() { echo $(( ${E2E_BASE_PEER:-5900} + $1 )); }
e2e_dir_port() { echo $(( ${E2E_BASE_DIR:-6000} + $1 )); }
