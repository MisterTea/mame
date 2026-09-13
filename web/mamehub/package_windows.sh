#!/usr/bin/env bash
# Build a Windows zip: MAMEHubOnline.exe + shell assets (wasm, hash/json, js).
# PROFILE=snes (default) or PROFILE=arcade
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
PROFILE="${PROFILE:-snes}"
EXE_NAME="MAMEHubOnline.exe"

if [[ "$PROFILE" == "arcade" ]]; then
  OUT="${1:-$ROOT/release/MAMEHubOnline-Arcade-windows}"
  WASM_JS="$ROOT/dist/mamearcadehub.js"
  WASM_BIN="$ROOT/dist/mamearcadehub.wasm"
  CONFIG_SRC="$ROOT/config.arcade.js"
  EXTRA_JSON="$ROOT/arcade_top_mp.json"
  LABEL="Arcade"
else
  PROFILE=snes
  OUT="${1:-$ROOT/release/MAMEHubOnline-SNES-windows}"
  WASM_JS="$ROOT/dist/mamesneshub.js"
  WASM_BIN="$ROOT/dist/mamesneshub.wasm"
  CONFIG_SRC="$ROOT/config.js"
  EXTRA_JSON=""
  LABEL="SNES"
fi

need=(
  "$ROOT/index.html"
  "$ROOT/app.js"
  "$CONFIG_SRC"
  "$ROOT/virtual_gamepad.js"
  "$ROOT/webrtc_peer.js"
  "$ROOT/nostr_lobby.js"
  "$WASM_JS"
  "$WASM_BIN"
  "$ROOT/vendor/nostr.bundle.js"
)
if [[ "$PROFILE" == "snes" ]]; then
  need+=("$ROOT/hash/snes.xml")
else
  need+=("$EXTRA_JSON")
fi
for f in "${need[@]}"; do
  if [[ ! -f "$f" ]]; then
    echo "missing $f" >&2
    if [[ "$f" == *mame*hub.* ]]; then
      echo "Run SUBTARGET=$PROFILE ./web/mamehub/rebuild_fast.sh first." >&2
    fi
    exit 1
  fi
done

mkdir -p "$OUT/dist" "$OUT/hash" "$OUT/vendor"
echo "==> building $EXE_NAME (windows/amd64) PROFILE=$PROFILE"
(cd "$ROOT/launcher" && GOOS=windows GOARCH=amd64 go build -ldflags="-s -w" -o "$OUT/$EXE_NAME" .)

copy_files=(
  index.html app.js virtual_gamepad.js webrtc_peer.js nostr_lobby.js
)
for f in "${copy_files[@]}"; do
  cp -f "$ROOT/$f" "$OUT/$f"
done
cp -f "$CONFIG_SRC" "$OUT/config.js"
cp -f "$WASM_JS" "$WASM_BIN" "$OUT/dist/"
cp -f "$ROOT/vendor/nostr.bundle.js" "$OUT/vendor/"
if [[ "$PROFILE" == "snes" ]]; then
  cp -f "$ROOT/hash/snes.xml" "$OUT/hash/"
else
  cp -f "$EXTRA_JSON" "$OUT/arcade_top_mp.json"
fi

if [[ "$PROFILE" == "arcade" ]]; then
  cat > "$OUT/README.txt" <<'EOF'
MAMEHub Online (Arcade) — Windows package
=========================================

1. Double-click MAMEHubOnline.exe
2. Your browser opens to http://127.0.0.1:8765/
3. Leave the console window open while playing (it is the local server + ROM proxy)
4. Close the console window to quit

Host: Host lobby → pick a 2P+ arcade machine → Play → Copy join link → share URL
     Wait for a full green mesh, then Start game (Kick before start if needed)
Join: open the host’s join link (machine is already chosen) → wait for Start

Default pick is X-Men (6 Players) / xmen6p. Everyone uses P1 controls; MAMEHub maps them to your seat.
EOF
else
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
fi

ZIP="$OUT.zip"
rm -f "$ZIP"
(cd "$(dirname "$OUT")" && zip -qr "$(basename "$ZIP")" "$(basename "$OUT")")
echo "==> package ready ($LABEL):"
echo "    $OUT/"
echo "    $ZIP"
du -h "$OUT/$EXE_NAME" "$OUT/dist/$(basename "$WASM_BIN")" "$ZIP" | sed 's/^/    /'
