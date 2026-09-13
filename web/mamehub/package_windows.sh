#!/usr/bin/env bash
# Build a Windows zip: MAMEHubOnline.exe + shell assets (wasm, hash, js).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$ROOT/../.." && pwd)"
OUT="${1:-$ROOT/release/MAMEHubOnline-SNES-windows}"
EXE_NAME="MAMEHubOnline.exe"

need=(
  "$ROOT/index.html"
  "$ROOT/app.js"
  "$ROOT/config.js"
  "$ROOT/virtual_gamepad.js"
  "$ROOT/webrtc_peer.js"
  "$ROOT/nostr_lobby.js"
  "$ROOT/dist/mamesneshub.js"
  "$ROOT/dist/mamesneshub.wasm"
  "$ROOT/hash/snes.xml"
  "$ROOT/vendor/nostr.bundle.js"
)
for f in "${need[@]}"; do
  if [[ ! -f "$f" ]]; then
    echo "missing $f" >&2
    if [[ "$f" == *mamesneshub.* ]]; then
      echo "Run ./web/mamehub/rebuild_fast.sh first." >&2
    fi
    exit 1
  fi
done

mkdir -p "$OUT/dist" "$OUT/hash" "$OUT/vendor"
echo "==> building $EXE_NAME (windows/amd64)"
(cd "$ROOT/launcher" && GOOS=windows GOARCH=amd64 go build -ldflags="-s -w" -o "$OUT/$EXE_NAME" .)

copy_files=(
  index.html app.js config.js virtual_gamepad.js webrtc_peer.js nostr_lobby.js
)
for f in "${copy_files[@]}"; do
  cp -f "$ROOT/$f" "$OUT/$f"
done
cp -f "$ROOT/dist/mamesneshub.js" "$ROOT/dist/mamesneshub.wasm" "$OUT/dist/"
cp -f "$ROOT/hash/snes.xml" "$OUT/hash/"
cp -f "$ROOT/vendor/nostr.bundle.js" "$OUT/vendor/"

cat > "$OUT/README.txt" <<'EOF'
MAMEHub Online (SNES) — Windows package
=======================================

1. Double-click MAMEHubOnline.exe
2. Your browser opens to http://127.0.0.1:8765/
3. Leave the console window open while playing (it is the local server + ROM proxy)
4. Close the console window to quit

Host: Host lobby → pick cart → Play → Copy join link → share URL
     Wait for a full green mesh, then Start game (Kick before start if needed)
Join: open the host’s join link (cart is already chosen) → wait for Start

Controls: Z/X/A/S = B/A/Y/X, arrows = D-pad, Q/W = L/R
Everyone uses P1 controls; MAMEHub maps them to your seat (P2/P3/…).
EOF

ZIP="$OUT.zip"
rm -f "$ZIP"
(cd "$(dirname "$OUT")" && zip -qr "$(basename "$ZIP")" "$(basename "$OUT")")
echo "==> package ready:"
echo "    $OUT/"
echo "    $ZIP"
du -h "$OUT/$EXE_NAME" "$OUT/dist/mamesneshub.wasm" "$ZIP" | sed 's/^/    /'
