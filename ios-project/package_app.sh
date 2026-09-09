#!/bin/bash
# Package the Genie-built MAMEHub iOS binary into a signed .app and optionally
# install it on the booted simulator.
#
# Env:
#   IOS_GCC          ios-simulator (default) | ios-arm64
#   MAMEHUB_IOS_ARGS optional args file contents for Documents/launch_args.txt
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IOS_GCC="${IOS_GCC:-ios-simulator}"
BIN_DIR="$ROOT/ios-project/build/$IOS_GCC"
APP_DIR="$BIN_DIR/MAMEHub.app"
DISCORD_SLICE="ios-arm64-simulator"
if [[ "$IOS_GCC" == "ios-arm64" ]]; then
  DISCORD_SLICE="ios-arm64"
fi
DISCORD_FW="$ROOT/3rdparty/discord_social_sdk/lib/release/discord_partner_sdk.xcframework/$DISCORD_SLICE/discord_partner_sdk.framework"

[[ -x "$BIN_DIR/MAMEHub" ]] || { echo "missing $BIN_DIR/MAMEHub — build with: make ios-simulator"; exit 1; }
[[ -d "$DISCORD_FW" ]] || { echo "missing Discord framework at $DISCORD_FW"; exit 1; }

rm -rf "$APP_DIR"
mkdir -p "$APP_DIR/Frameworks"
cp "$BIN_DIR/MAMEHub" "$APP_DIR/MAMEHub"
cp "$ROOT/ios-project/Info.plist" "$APP_DIR/Info.plist"
rsync -a "$DISCORD_FW" "$APP_DIR/Frameworks/"

# Documents directory is created at runtime; seed optional default args.
if [[ -n "${MAMEHUB_IOS_ARGS:-}" ]]; then
  mkdir -p "$APP_DIR/Documents"
  printf '%s\n' "$MAMEHUB_IOS_ARGS" >"$APP_DIR/Documents/launch_args.txt"
fi

# Ad-hoc sign for simulator.
codesign --force --sign - --timestamp=none \
  --entitlements /dev/null \
  "$APP_DIR/Frameworks/discord_partner_sdk.framework" 2>/dev/null || \
  codesign --force --sign - --timestamp=none "$APP_DIR/Frameworks/discord_partner_sdk.framework"
codesign --force --sign - --timestamp=none "$APP_DIR"

echo "OK: $APP_DIR"
if [[ "$IOS_GCC" == "ios-simulator" ]]; then
  BOOTED="$(xcrun simctl list devices | awk -F'[()]' '/Booted/{print $2; exit}')"
  if [[ -n "$BOOTED" ]]; then
    xcrun simctl install booted "$APP_DIR"
    echo "Installed on simulator $BOOTED"
    echo "Launch example:"
    echo "  xcrun simctl launch --console-pty booted org.mamedev.mamehub -discord_auth -discord_mock iOSGuest"
  else
    echo "No booted simulator; install later with:"
    echo "  xcrun simctl install booted $APP_DIR"
  fi
fi
