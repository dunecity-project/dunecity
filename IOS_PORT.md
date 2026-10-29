# iOS Port Notes

Dune City runs on iPhone and iPad as a native SDL2 app built from the same
source as the desktop, browser and Android versions. It follows the Android
port (see `ANDROID_PORT.md`): the same touch translation, cursor policy,
on-screen keyboard handling and fixed logical interface. Code that applies to
both phones is gated by `DUNECITY_MOBILE`, iOS-only code by `DUNECITY_IOS`,
both defined in `include/misc/MobilePlatform.h`. Android-only services (the
Back button, app storage paths, the certificate path) keep `__ANDROID__`.

## Touch controls

Shared with Android. `src/misc/TouchInput.cpp` feeds finger events to
`TouchGestures` (`src/misc/TouchGestures.cpp`), which decides what they mean
and hands the game ordinary mouse events, plus pan and zoom for the map:

```text
tap                                   -> left click (select, press buttons, place)
drag                                  -> left drag (box-select, minimap)
two fingers moved together            -> scroll the map
two fingers spread / pinched          -> zoom in / out one level (1x, 2x, 3x)
two-finger tap                        -> right click between the fingers
hold one finger + tap a second finger -> right click at the first finger
```

A press waits until the finger moves or lifts. A second finger within 150 ms
makes it a two-finger gesture, and by then no click may have happened: it would
have started a selection box, or issued an Attack or Move cursor's order. That
is why `main.cpp` turns SDL's own touch-to-mouse events off on mobile. Leaving
the foreground mid-touch resets the gesture.

On phones "Left-click orders" (Options) is on by default: with units
selected, a tap on an enemy attacks it and a tap on the ground moves there. A
tap on one of your own units selects it, and a two-finger tap clears the
selection or cancels an Attack/Move cursor. The sidebar order icons are 36 px,
three per row; the unit panels drop the unit picture to make room. Tooltips
stay hidden while the pointer is a finger, since a lifted finger would leave
one open over the map.

iOS has no Back button: a tap skips the intro and other cutscenes, and the
in-game Options button or the Mentat reach the menus. A connected keyboard,
mouse or trackpad (iPad) works as on the desktop; the pointer appears when a
physical mouse is used (Video options `Cursor`: Auto, Hidden, Visible).

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
installs with `devicectl`, then launches the app (unlock the phone first).
Pass `--bundle-id` when the profile was issued for a different identifier.
Xcode's wildcard "iOS Team Provisioning Profile: *" works: the script signs
with the app's own identifier.

First-time setup for a phone, as done for 1.0.857 on team 34X7AYJZ93: turn on
Settings → Privacy & Security → Developer Mode on the phone, add the Apple ID
in Xcode → Settings → Accounts, accept any pending Program License Agreement
at developer.apple.com, then let Xcode create the certificate and register the
device by building any app for it with `xcodebuild -allowProvisioningUpdates
-allowProvisioningDeviceRegistration`. Development builds run until the
profile expires (one year).

## Distribution (not yet possible)

Phones other than the developer's own need one of:

| Route | Who can install | Needs |
|---|---|---|
| TestFlight, internal | up to 100 App Store Connect users of the team | app record in App Store Connect, App Store upload (below); no review |
| TestFlight, external | anyone with the public link, up to 10,000 | the same plus Beta App Review |
| Ad Hoc | up to 100 registered devices a year | each tester's UDID, an Ad Hoc profile, an install page serving an `itms-services` manifest over HTTPS |
| App Store | everyone | full App Review |

An App Store or TestFlight upload additionally needs: an `Assets.car` asset
catalog with `CFBundleIconName` and a 1024 px icon, `ITSAppUsesNonExemptEncryption`
(the bundled OpenSSL is used for TLS and DTLS), a `PrivacyInfo.xcprivacy`
declaring the required-reason APIs used (file timestamps, system boot time,
user defaults), the Xcode build metadata keys (`DTPlatformName`, `DTSDKName`,
`DTXcode`...) that CMake/Ninja bundles lack, an Apple Distribution certificate
and an App Store provisioning profile. The bundle ships the original Dune II
PAK and VOC files; App Review (and external TestFlight review) will judge them
under the intellectual-property guideline, so public distribution first needs
a decision on the game data, as for Android (`ANDROID_PORT.md`).
