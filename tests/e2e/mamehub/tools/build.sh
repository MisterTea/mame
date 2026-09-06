#!/bin/bash
# Build macOS UI automation helpers used by the MAMEHub e2e harness.
set -euo pipefail

TOOLS_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="${1:-$TOOLS_DIR/bin}"
mkdir -p "$OUT_DIR"

need_swiftc() {
  if ! command -v swiftc >/dev/null 2>&1; then
    echo "FAIL: swiftc not found (Xcode / Command Line Tools required on macOS)" >&2
    exit 1
  fi
}

build_one() {
  local src="$1"
  local name="$2"
  local dest="$OUT_DIR/$name"
  if [[ -x "$dest" && "$dest" -nt "$src" ]]; then
    echo "up-to-date $name"
    return 0
  fi
  echo "building $name"
  swiftc -O -o "$dest" "$src"
}

need_swiftc
build_one "$TOOLS_DIR/click_highlight.swift" click_highlight
build_one "$TOOLS_DIR/focus_key.swift" focus_key
build_one "$TOOLS_DIR/pad.swift" pad
build_one "$TOOLS_DIR/key.swift" key
echo "tools ready in $OUT_DIR"
