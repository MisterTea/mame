#!/usr/bin/env bash
# Build a macOS zip: MAMEHubOnline + shell assets (wasm, hash/json, js).
# PROFILE=all (default, every profiles.json site) | arcade | snes | nes | ...
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$ROOT/../.." && pwd)"
GEN="$REPO/scripts/mamehub/gen_browser_profiles.py"
PROFILE="${PROFILE:-all}"
BIN_NAME="MAMEHubOnline"

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
  OUT="${1:-$ROOT/release/MAMEHubOnline-macos}"
  LABEL="all profiles"
else
  OUT="${1:-$ROOT/release/MAMEHubOnline-${PROFILE}-macos}"
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

# Root config.js matches default profile (snes) for legacy root-only packages.
if [[ "$PROFILE" == "all" || "$PROFILE" == "snes" ]]; then
  cp -f "$ROOT/config.snes.js" "$OUT/config.js"
elif [[ -f "$ROOT/config.${PROFILE}.js" ]]; then
  cp -f "$ROOT/config.${PROFILE}.js" "$OUT/config.js"
fi

cat > "$OUT/README.txt" <<EOF
MAMEHub Online — macOS package ($LABEL)
=======================================

1. Unzip somewhere permanent (not Downloads-only if Gatekeeper blocks)
2. Double-click MAMEHubOnline (or: ./MAMEHubOnline)
3. Browser opens http://127.0.0.1:8765/ — pick a system
4. Leave the process running while playing
5. Quit (Ctrl+C / close window) to stop

If macOS says the app is damaged / can’t be opened (unsigned binary):
  xattr -dr com.apple.quarantine /path/to/$(basename "$OUT")

Optional: ./MAMEHubOnline -profile snes   (or arcade, nes, gameboy, …)

Host: Host lobby → pick game → Play → Copy join link → share URL
Join: open the host’s join link → wait for Start
EOF

ZIP="$OUT.zip"
rm -f "$ZIP"
(cd "$(dirname "$OUT")" && zip -qr "$(basename "$ZIP")" "$(basename "$OUT")")
echo "==> package ready ($LABEL):"
echo "    $OUT/"
echo "    $ZIP"
du -h "$OUT/$BIN_NAME" "$ZIP" | sed 's/^/    /'
file "$OUT/$BIN_NAME" | sed 's/^/    /'
ls "$OUT/dist" | sed 's/^/    dist\//'
