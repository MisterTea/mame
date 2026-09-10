#!/bin/bash
# Package ios-arm64 MAMEHub.app into a signed App Store IPA for TestFlight.
# Required env:
#   DEVELOPMENT_TEAM   10-char Team ID
# Optional:
#   CODE_SIGN_IDENTITY  default "Apple Distribution"
#   BUNDLE_ID           default from Info.plist
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IOS_GCC=ios-arm64
BIN_DIR="$ROOT/ios-project/build/$IOS_GCC"
APP_DIR="$BIN_DIR/MAMEHub.app"
OUT_DIR="${IPA_OUT:-$BIN_DIR}"
TEAM="${DEVELOPMENT_TEAM:?set DEVELOPMENT_TEAM}"
IDENTITY="${CODE_SIGN_IDENTITY:-Apple Distribution}"
ENTITLEMENTS="${ENTITLEMENTS_PATH:-$ROOT/ios-project/Entitlements.plist}"

[[ -x "$BIN_DIR/MAMEHub" ]] || { echo "missing $BIN_DIR/MAMEHub — run: make ios-arm64"; exit 1; }

# Ensure .app exists (ad-hoc packaging first)
IOS_GCC=ios-arm64 "$ROOT/ios-project/package_app.sh"

BUNDLE_ID="$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$APP_DIR/Info.plist")"
VERSION="$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$APP_DIR/Info.plist")"
BUILD="$(/usr/libexec/PlistBuddy -c 'Print CFBundleVersion' "$APP_DIR/Info.plist")"

if [[ ! -f "$ENTITLEMENTS" ]]; then
  cat > "$ENTITLEMENTS" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>application-identifier</key>
  <string>${TEAM}.${BUNDLE_ID}</string>
  <key>com.apple.developer.team-identifier</key>
  <string>${TEAM}</string>
  <key>get-task-allow</key>
  <false/>
</dict>
</plist>
PLIST
fi

# Prefer an App Store provisioning profile if present
PROFILE="${PROVISIONING_PROFILE:-}"
if [[ -z "$PROFILE" ]]; then
  for d in "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles" \
           "$HOME/Library/MobileDevice/Provisioning Profiles"; do
    [[ -d "$d" ]] || continue
    while IFS= read -r -d '' p; do
      if security cms -D -i "$p" 2>/dev/null | rg -q "$BUNDLE_ID"; then
        PROFILE="$p"
        break 2
      fi
    done < <(find "$d" -name '*.mobileprovision' -print0 2>/dev/null)
  done
fi

echo "Signing with identity: $IDENTITY team=$TEAM bundle=$BUNDLE_ID"
codesign --force --sign "$IDENTITY" --timestamp \
  --entitlements "$ENTITLEMENTS" \
  "$APP_DIR/Frameworks/discord_partner_sdk.framework"
codesign --force --sign "$IDENTITY" --timestamp \
  --entitlements "$ENTITLEMENTS" \
  "$APP_DIR"

if [[ -n "$PROFILE" ]]; then
  cp "$PROFILE" "$APP_DIR/embedded.mobileprovision"
  echo "Embedded profile: $PROFILE"
fi

codesign --verify --deep --strict "$APP_DIR"

STAGE="$OUT_DIR/ipa-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/Payload"
cp -a "$APP_DIR" "$STAGE/Payload/"
IPA="$OUT_DIR/MamehubDiscord-${VERSION}-${BUILD}.ipa"
( cd "$STAGE" && zip -qry "$IPA" Payload )
echo "OK: $IPA"
echo "Upload with:"
echo "  xcrun altool --upload-app -f '$IPA' -t ios -u YOUR_APPLE_ID -p @keychain:AC_PASSWORD"
echo "  # or: xcrun notarytool / Transporter / Xcode Organizer"
