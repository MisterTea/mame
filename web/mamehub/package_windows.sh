#!/usr/bin/env bash
# Build a Windows zip: MAMEHubOnline.exe + shell assets (wasm, hash/json, js).
# PROFILE=all (default, every profiles.json site) | arcade | snes | nes | ...
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$ROOT/../.." && pwd)"
GEN="$REPO/scripts/mamehub/gen_browser_profiles.py"
PROFILE="${PROFILE:-all}"
EXE_NAME="MAMEHubOnline.exe"

valid=0
while IFS= read -r id; do
  [[ "$id" == "$PROFILE" ]] && valid=1
done < <(python3 "$GEN" ids)
if [[ "$PROFILE" != "all" && "$valid" -ne 1 ]]; then
  echo "PROFILE must be all or a profiles.json id (got: $PROFILE)" >&2
  python3 "$GEN" ids | sed 's/^/  /' >&2
  exit 1
fi

if [[ "$PROFILE" == "all" ]]; then
  OUT="${1:-$ROOT/release/MAMEHubOnline-windows}"
  LABEL="all profiles"
else
  OUT="${1:-$ROOT/release/MAMEHubOnline-${PROFILE}-windows}"
  LABEL="$PROFILE"
fi

need=(
  "$ROOT/index.html"
  "$ROOT/play.html"
  "$ROOT/app.js"
  "$ROOT/profiles.json"
  "$ROOT/virtual_gamepad.js"
  "$ROOT/webrtc_peer.js"
  "$ROOT/nostr_lobby.js"
  "$ROOT/vendor/nostr.bundle.js"
)
while IFS= read -r rel; do
  [[ -n "$rel" ]] && need+=("$ROOT/$rel")
done < <(python3 "$GEN" package-files "$PROFILE")

for f in "${need[@]}"; do
  if [[ ! -f "$f" ]]; then
    echo "missing $f" >&2
    if [[ "$f" == *hub.* ]]; then
      echo "Build wasm first, e.g. ./web/mamehub/rebuild_fast.sh all" >&2
    fi
    exit 1
  fi
done

mkdir -p "$OUT/dist" "$OUT/hash" "$OUT/vendor"
echo "==> building $EXE_NAME (windows/amd64) PROFILE=$PROFILE"
(cd "$ROOT/launcher" && GOOS=windows GOARCH=amd64 go build -ldflags="-s -w" -o "$OUT/$EXE_NAME" .)

copy_shared=(
  app.js play.html virtual_gamepad.js webrtc_peer.js nostr_lobby.js profiles.json
)
for f in "${copy_shared[@]}"; do
  cp -f "$ROOT/$f" "$OUT/$f"
done
cp -f "$ROOT/vendor/nostr.bundle.js" "$OUT/vendor/"
cp -f "$ROOT/index.html" "$OUT/index.html"

while IFS= read -r rel; do
  [[ -z "$rel" ]] && continue
  mkdir -p "$OUT/$(dirname "$rel")"
  cp -f "$ROOT/$rel" "$OUT/$rel"
done < <(python3 "$GEN" package-files "$PROFILE")

if [[ "$PROFILE" == "all" || "$PROFILE" == "snes" ]]; then
  cp -f "$ROOT/config.snes.js" "$OUT/config.js"
elif [[ -f "$ROOT/config.${PROFILE}.js" ]]; then
  cp -f "$ROOT/config.${PROFILE}.js" "$OUT/config.js"
fi

cat > "$OUT/README.txt" <<EOF
MAMEHub Online — Windows package ($LABEL)
========================================

1. Double-click MAMEHubOnline.exe
2. Browser opens http://127.0.0.1:8765/ — pick a system
3. Leave the console window open while playing (local server + ROM proxy)
4. Close the console window to quit

Optional: MAMEHubOnline.exe -profile snes   (or arcade, nes, gameboy, …)

Host: Host lobby → pick game → Play → Copy join link → share URL
     Wait for a full green mesh, then Start game
Join: open the host’s join link → wait for Start

Everyone uses P1 controls; MAMEHub maps them to your seat.
EOF

ZIP="$OUT.zip"
rm -f "$ZIP"
(cd "$(dirname "$OUT")" && zip -qr "$(basename "$ZIP")" "$(basename "$OUT")")
echo "==> package ready ($LABEL):"
echo "    $OUT/"
echo "    $ZIP"
du -h "$OUT/$EXE_NAME" "$ZIP" | sed 's/^/    /'
ls "$OUT/dist" | sed 's/^/    dist\//'
