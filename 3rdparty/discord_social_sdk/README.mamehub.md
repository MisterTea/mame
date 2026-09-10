# Discord Social SDK

This directory contains the release redistributables from Discord Social SDK
1.10.19337, imported from `DiscordSocialSdk-1.10.19337.zip`.

MAMEHub uses application ID `1545444437482676284`.

Discord sign-in is required by default at startup on all platforms (desktop,
Android, iOS). Disable with `-nodiscord_auth` for Offline-only / local testing.
Mock auth for e2e: `-discord_mock <name>`.

Desktop authentication uses the SDK's public-client PKCE flow and requires this
redirect URL in the Discord developer portal:

    http://127.0.0.1/callback

Mobile (Android / iOS) authentication uses the deep-link redirect:

    discord-1545444437482676284:/authorize/callback

Vendored binaries:

- Desktop: `lib/release/libdiscord_partner_sdk.{dylib,so,lib}` (+ `arm64/`)
- Android: `lib/release/android/arm64-v8a/libdiscord_partner_sdk.so`
  and `android-project/app/libs/discord_partner_sdk.aar`
- iOS: `lib/release/discord_partner_sdk.xcframework` (`ios-arm64`, `ios-arm64-simulator`)

These Android/iOS SDK bits must be committed for GitHub Actions
(`build_android` / `build_ios` jobs). Copy them from the Discord Social SDK
release zip the same way as the desktop libs.

Debug libraries and the optional Krisp voice assets are intentionally omitted.
