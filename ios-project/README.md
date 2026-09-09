# ios-project

Minimal iOS packaging for MAMEHub with Discord Social SDK.

## Prerequisites

1. Xcode + iPhoneSimulator / iPhoneOS SDKs
2. Deps (SDL2, OpenSSL, libsodium):

```bash
./tests/e2e/mamehub/build_ios_deps.sh
# optional device:
IOS_PLATFORM=iphoneos ./tests/e2e/mamehub/build_ios_deps.sh
```

3. Discord iOS xcframework under
   `3rdparty/discord_social_sdk/lib/release/discord_partner_sdk.xcframework`
   (copied from `~/Downloads/discord_social_sdk`).

## Build (simulator)

```bash
export IOS_OPENSSL_ROOT=/tmp/mamehub-ios-deps/openssl-sim-arm64
export IOS_SODIUM_ROOT=/tmp/mamehub-ios-deps/libsodium-sim-arm64
export SDL_INSTALL_ROOT=/tmp/mamehub-ios-deps/sdl-sim-arm64

make ios-simulator CONFIG=release NOWERROR=1 REGENIE=1 -j"$(sysctl -n hw.ncpu)"
./ios-project/package_app.sh
```

## Discord

- Application ID: `1545444437482676284`
- Redirect scheme: `discord-1545444437482676284:/authorize/callback`
- Sign-in is required at startup by default; opt out with `-nodiscord_auth`
- Mock auth for e2e: `-discord_mock iOSGuest`

## Device build

```bash
export IOS_OPENSSL_ROOT=/tmp/mamehub-ios-deps/openssl-ios-arm64
export IOS_SODIUM_ROOT=/tmp/mamehub-ios-deps/libsodium-ios-arm64
export SDL_INSTALL_ROOT=/tmp/mamehub-ios-deps/sdl-ios-arm64
make ios-arm64 CONFIG=release NOWERROR=1 REGENIE=1 -j"$(sysctl -n hw.ncpu)"
IOS_GCC=ios-arm64 ./ios-project/package_app.sh
```

Device installs need a real signing identity (package script ad-hoc signs for simulator only).
