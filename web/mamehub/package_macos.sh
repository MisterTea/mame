#!/usr/bin/env bash
# Build a macOS zip: MAMEHubOnline + shell assets (wasm, hash/json, js).
# PROFILE=snes (default) or PROFILE=arcade
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
PROFILE="${PROFILE:-snes}"
BIN_NAME="MAMEHubOnline"

if [[ "$PROFILE" == "arcade" ]]; then
  OUT="${1:-$ROOT/release/MAMEHubOnline-Arcade-macos}"
  WASM_JS="$ROOT/dist/mamearcadehub.js"
  WASM_BIN="$ROOT/dist/mamearcadehub.wasm"
  CONFIG_SRC="$ROOT/config.arcade.js"
  EXTRA_JSON="$ROOT/arcade_top_mp.json"
  LABEL="Arcade"
else
  PROFILE=snes
  OUT="${1:-$ROOT/release/MAMEHubOnline-SNES-macos}"
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
TMP="$(mktemp -d "${TMPDIR:-/tmp}/mamehub-macos.XXXXXX")"
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

echo "==> building $BIN_NAME (darwin arm64 + amd64 → universal if lipo available) PROFILE=$PROFILE"
(cd "$ROOT/launcher" && GOOS=darwin GOARCH=arm64 go build -ldflags="-s -w" -o "$TMP/arm64" .)
(cd "$ROOT/launcher" && GOOS=darwin GOARCH=amd64 go build -ldflags="-s -w" -o "$TMP/amd64" .)
if command -v lipo >/dev/null 2>&1; then
  lipo -create -output "$OUT/$BIN_NAME" "$TMP/arm64" "$TMP/amd64"
  chmod +x "$OUT/$BIN_NAME"
  echo "    universal: $(lipo -archs "$OUT/$BIN_NAME")"
else
  HOST_ARCH="$(uname -m)"
  if [[ "$HOST_ARCH" == "arm64" || "$HOST_ARCH" == "aarch64" ]]; then
    cp -f "$TMP/arm64" "$OUT/$BIN_NAME"
  else
    cp -f "$TMP/amd64" "$OUT/$BIN_NAME"
  fi
  chmod +x "$OUT/$BIN_NAME"
  echo "    single-arch (no lipo): $HOST_ARCH"
fi

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
MAMEHub Online (Arcade) — macOS package
=======================================

1. Unzip this folder somewhere permanent (not Downloads-only if Gatekeeper blocks)
2. Double-click MAMEHubOnline (or run it from Terminal: ./MAMEHubOnline)
3. Your browser opens to http://127.0.0.1:8765/
4. Leave the Terminal/console open while playing (local server + ROM proxy)
5. Quit the process (Ctrl+C or close the window) to stop

If macOS says the app is damaged / can’t be opened (unsigned binary):
  xattr -dr com.apple.quarantine /path/to/MAMEHubOnline-Arcade-macos

Host: Host lobby → pick a 2P+ arcade machine → Play → Copy join link → share URL
     Wait for a full green mesh, then Start game (Kick before start if needed)
Join: open the host’s join link (machine is already chosen) → wait for Start

Default pick is X-Men (6 Players) / xmen6p. Everyone uses P1 controls; MAMEHub maps them to your seat.
EOF
else
  cat > "$OUT/README.txt" <<'EOF'
MAMEHub Online (SNES) — macOS package
=====================================

1. Unzip this folder somewhere permanent (not Downloads-only if Gatekeeper blocks)
2. Double-click MAMEHubOnline (or run it from Terminal: ./MAMEHubOnline)
3. Your browser opens to http://127.0.0.1:8765/
4. Leave the Terminal/console open while playing (local server + ROM proxy)
5. Quit the process (Ctrl+C or close the window) to stop

If macOS says the app is damaged / can’t be opened (unsigned binary):
  xattr -dr com.apple.quarantine /path/to/MAMEHubOnline-SNES-macos

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
du -h "$OUT/$BIN_NAME" "$OUT/dist/$(basename "$WASM_BIN")" "$ZIP" | sed 's/^/    /'
file "$OUT/$BIN_NAME" | sed 's/^/    /'
