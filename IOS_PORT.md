# iOS Port Notes

Dune City runs on iPhone and iPad as a native SDL2 app built from the same
source as the desktop, browser and Android versions. It follows the Android
port (see `ANDROID_PORT.md`): the same touch translation, cursor policy,
on-screen keyboard handling and fixed logical interface. Code that applies to
both phones is gated by `DUNECITY_MOBILE`, iOS-only code by `DUNECITY_IOS`,
both defined in `include/misc/MobilePlatform.h`. Android-only services (the
Back button, app storage paths, the certificate path) keep `__ANDROID__`.

## Controls

Identical to Android; `src/misc/TouchInput.cpp` turns touches into mouse
events before normal UI and game handling:

```text
single tap                            -> left click (select, press buttons, place)
single drag                           -> left drag (box-select units)
hold one finger + tap a second finger -> right click at the first finger
```

iOS has no Back button: a tap skips the intro and other cutscenes, and the
in-game Options button or the Mentat reach the menus. A connected keyboard,
mouse or trackpad (iPad) works as on the desktop; the pointer appears when a
physical mouse is used (Video options `Cursor`: Auto, Hidden, Visible).

Without a second finger, orders also work through the sidebar: select a unit,
tap Move (or Attack, Capture...) and then tap the destination.

## Display

The interface keeps a fixed logical size (640x480, 854x480, 800x600,
1067x600, 1024x768 or 1366x768) that SDL scales and letterboxes onto the
screen. The first start chooses the 16:9 interface at 480 lines: every iPhone
since the X is wider than 16:9, so the side bars keep the camera housing clear
of the game. The home indicator is hidden and system edge gestures need a
second swipe, so drags along the bottom edge reach the game. The app is
landscape only.

## App lifecycle

iOS 27 does not launch apps built with its SDK unless they adopt the UIScene
lifecycle (it stops in `_UIApplicationEvaluateRuntimeIssueForNoSceneLifecycleAdoption`).
SDL 2.32 has no scene support: it keeps its UIApplication delegate and creates
its windows with `-initWithFrame:`, which belong to no scene. Info.plist
declares one application scene served by `DuneCitySceneDelegate`
(`src/misc/IOSScene.m`), which attaches every UIWindow to the connected
`UIWindowScene`, so SDL itself stays unpatched.

## Files

| What | Where |
|---|---|
| Game data, config templates, mods, imported sprites | The app bundle root (read-only), found through `SDL_GetBasePath()` |
| Settings, saves, logs, profile mods and caches | `Documents/Dune City/` in the app sandbox |

`Documents` is shared through the Files app and Finder (`UIFileSharingEnabled`),
so players can back up saves and add maps or PAK files there. The original
Dune II PAK files are bundled exactly as in the Android APK; the licensing
boundary noted in `ANDROID_PORT.md` applies before any public distribution.

Not included on iOS: the desktop updater (Sparkle) and Discord Rich Presence
(`discord-rpc` is excluded in `vcpkg.json` because it needs AppKit). Online
play, direct peer-to-peer (libdatachannel) and UPnP are built in;
`NSLocalNetworkUsageDescription` covers local network games.

## Requirements

```text
Xcode (full install; the Command Line Tools have no iOS SDK)
iOS Simulator runtime: xcodebuild -downloadPlatform iOS
CMake 3.24+, Ninja, ccache (Homebrew)
vcpkg checkout in VCPKG_ROOT (default ~/vcpkg)
platform/web: npm ci --ignore-scripts   (p2pkit SDK headers, as on every platform)
```

If `xcode-select` still points at the Command Line Tools, the build script
uses `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`, which needs no
`sudo`.

The minimum is iOS 15. `cmake/vcpkg-triplets/` overrides vcpkg's community
`arm64-ios` and `arm64-ios-simulator` triplets only to give every static
dependency the same minimum.

## Simulator

```sh
scripts/build-ios.sh          # configure and build build-ios-sim/bin/dunecity.app
scripts/build-ios.sh --run    # also create/boot "Dune City iPhone", install and launch
```

`--run` creates the simulator on the runtime that supports the most iPhone
models (a point release such as iOS 27.1 may exist for a single new device)
and opens Simulator.app, or DeviceHub.app on Xcode 27, which replaces it.

The first build compiles the vcpkg dependencies for the simulator; later builds
reuse vcpkg's binary cache and ccache. Useful commands afterwards:

```sh
xcrun simctl launch --console "Dune City iPhone" net.dunecity.DuneCity   # with log output
xcrun simctl io "Dune City iPhone" screenshot shot.png
xcrun simctl get_app_container "Dune City iPhone" net.dunecity.DuneCity data   # sandbox
```

Taps can be scripted with AXe (`brew install cameroncooke/axe/axe`), in points
of the landscape screen. Hold each touch for a frame or more; a zero-length
tap arrives as press and release in the same frame:

```sh
axe touch -x 437 -y 205 --down --up --delay 0.15 --udid <UDID>
axe swipe --start-x 300 --start-y 360 --end-x 470 --end-y 395 --duration 0.8 --udid <UDID>
```

## iPhone hardware

Device builds need an Apple Development certificate in the keychain and a
provisioning profile that covers the bundle identifier and the device (a free
Apple ID signed into Xcode can create both; the Developer ID certificate used
for the macOS release cannot sign iOS apps).

```sh
scripts/build-ios.sh --device     # unsigned build-ios-device/bin/dunecity.app (compile check)
scripts/build-ios.sh --device \
    --identity "Apple Development: Name (TEAMID)" \
    --profile ~/Downloads/DuneCity.mobileprovision \
    --bundle-id net.dunecity.DuneCity \
    --install <device UDID or name from 'xcrun devicectl list devices'>
```

The script embeds the profile, signs with the profile's entitlements and
installs with `devicectl`. Pass `--bundle-id` when the profile was issued for
a different identifier.

## Remaining work

1. Touch camera panning (shared with Android): a two-finger drag would conflict
   with the current two-finger right click, so it needs a gesture decision.
2. App Store or TestFlight distribution: asset catalog icons, export
   compliance for the bundled OpenSSL, and the Dune II data licensing question.
