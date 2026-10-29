# Android Port Notes

Dune2R/DuneCity is SDL2-based. The Android path uses the SDL Android activity
wrapper and builds the native game as the app's `libmain.so` shared library.

Known external references:

- Official Dune Legacy releases currently list Windows and macOS packages, but
  no Android APK release.
- `caiiiycuk/dunedroid` is an older SDL 1.2/1.3 Android porting tree for
  legacy games. It is useful background, but should not be used directly for
  this SDL2 codebase.
- SDL2 supports Android through its Android project skeleton / SDLActivity
  flow.

## Touch Mapping

The game remains mouse-driven internally. `TouchInput` turns finger events
into mouse events and map pan/zoom before normal UI/game handling; the
gestures (tap, drag, two-finger scroll and pinch zoom, two-finger right click)
are described once for both phones in `IOS_PORT.md` "Touch controls". Android
adds:

```text
Android Back button                   -> Escape
```

External mice use Android's native system cursor rather than SDL color
cursors. The Video options expose three cursor policies:

```text
Auto      show for physical mouse input and hide for touch input
Hidden    always hide
Visible   always show, including Mentat, briefing, gameplay, and editor screens
```

Android may reset pointer visibility while changing SDL views or window
focus. The runtime therefore reapplies the selected policy across menu,
cutscene, gameplay, and map-editor transitions.

## Android TV

The generated APK supports both the standard Android launcher and Android
TV's Leanback launcher. It declares touchscreen, gamepad, Bluetooth, USB host,
and Leanback support as optional, includes a 320x180 TV banner, and remains
locked to landscape. This makes the app discoverable from launchers such as
Nvidia Shield Home while preserving phone and tablet installation.

## Foldables And Resizable Windows

The generated activity is explicitly resizable and uses sensor landscape, so
both landscape rotations remain available while portrait is rejected. The SDL
orientation hint repeats that policy when its resizable native window starts;
without the hint, SDL permits every orientation and follows the user's Android
rotation lock. SDLActivity owns fold, unfold, DeX, and multi-window surface
changes, and its `surfaceChanged` path forwards the current resolution to SDL
without restarting the game. Density and large-screen configuration changes
are declared in the manifest so Android does not place the game in size
compatibility mode.

Android keeps a stable `640x480` logical game canvas while the physical SDL
surface changes size. SDL scales and letterboxes that canvas for the current
outer screen, inner screen, TV, or window. This keeps the fixed-size menus
readable and prevents a fold transition from retaining a portrait-shaped game
target. Runtime logs include physical pixels, dp dimensions, density, and
orientation under the `Dune2RActivity` and `SDL` tags.

Foldable verification should cover launching on both displays, folding and
unfolding while a menu and match are active, rotating the open device, and
Samsung DeX or Android split-screen resizing.

## Required Tooling

The PC needs these discoverable by Android Studio or environment variables:

```text
Android SDK
Android NDK 26 or newer (28.2 is the currently tested local toolchain)
CMake 3.24 or newer
Ninja
JDK 17
vcpkg (VCPKG_ROOT or the Visual Studio vcpkg component)
```

Expected variables:

```text
ANDROID_HOME or ANDROID_SDK_ROOT
ANDROID_NDK_HOME, or an NDK installed under the SDK's ndk/ directory
JAVA_HOME
```

Load these paths in PowerShell with:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\android-env.ps1
```

Or in `cmd.exe` without changing PowerShell execution policy:

```bat
call scripts\android-env.cmd
```

Dune2R builds a native Android shared library and debug APK with this toolchain.
Runtime was verified on an Android 13 Ulefone Armor 21 over ADB.

The packager records a toolchain fingerprint in the native build directory.
It includes the checkout path, SDK/NDK and vcpkg locations and versions, CMake,
Ninja, both CMake project files, and `vcpkg.json`. If any requirement changes, only the
Android native build directory is recreated. This prevents CMake or vcpkg from
reusing absolute paths and compiler identities from an older checkout while
preserving a warm build when the toolchain is unchanged.

## Verified Build Status

Native library:

```text
build-android-arm64-ndk/lib/libmain.so
```

APK:

```text
GitHub Release asset: DuneLegacy.apk
```

Data/config/mod payload:

```text
build-android-payload/
```

The debug APK also embeds this payload under `assets/dune2r_payload/` and the
generated `Dune2RActivity` copies it to Android app storage before launching
the native SDL game. The payload marker uses the Android release version, so
installing a newer APK refreshes bundled game and mod files while preserving
the user's `config/Dune City.ini` and any additional custom files. Optional
Dune2R remastered atlases are not embedded in the APK. They can be installed
per unit or as one resumable, verified queue from `Dune2R EditoR` -> `ASSETS`;
managed payload refreshes preserve those downloads.

Android package versions are maintained independently in
`android-version.json`. Increment `versionCode` for every distributed APK and
use `versionName` for the public Android release number; do not change the
DuneCity project version merely to publish an Android update.

## Upstream Payload Policy

Stefan's `svan058/dunecity` `tornie` branch is the source of truth for the
DuneCity base, Tornie mod, configuration, and data files. Before an Android
release, merge that branch into `feature/dune2r-mod`; the packager then copies
`data/`, `config/`, and `mods/` from the merged checkout. Dune2R and the Android
runtime adaptations are the only layers maintained by this fork. Do not copy
payload files from a separate desktop installation or keep a second Android-
specific copy of Stefan's files.

## Android TLS Trust

The Android package includes `data/cacert.pem`, curl's Mozilla-derived CA
bundle, because the statically linked OpenSSL backend cannot reliably consume
Android's legacy hash-named certificate directories. Refresh it from
`https://curl.se/ca/cacert.pem` and verify it against
`https://curl.se/ca/cacert.pem.sha256`. The certificate bundle is distributed
under Mozilla Public License 2.0, as documented by curl.

Verified runtime behavior:

```text
landscape launch and scaling
single-finger tap / click alignment
Android Back as Escape
two-finger right-click gesture
```

## Build Commands

Configure the native Android library, build it with one worker, stage the
payload, and build the APK:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package-android-apk.ps1 -BuildNative -BuildApk -NativeBuildJobs 1
```

The script discovers Visual Studio's vcpkg component when `VCPKG_ROOT` is not
set. NDK discovery ignores incomplete or cloud-placeholder installations that
do not contain `android.toolchain.cmake`, then checks the selected SDK and the
standard per-user SDK. Pass `-AndroidSdk`, `-AndroidNdk`, or `-VcpkgRoot` only
to override the discovered requirements.

Current MSYS packages contain hard links, so vcpkg's `VCPKG_DOWNLOADS` must be
on an NTFS volume during Android dependency builds. An exFAT downloads path
fails while extracting aliases such as `gawk.exe` or `ld.exe`. The binary cache
and the large build tree may still remain on an external exFAT drive.

Install and push the staged payload after a device is attached:

```bat
adb install -r "DuneLegacy.apk"
```

The generated APK stages the bundled payload automatically on first launch.
For debugging, the payload can still be pushed manually:

```bat
adb push "build-android-payload\." /sdcard/Android/data/net.dunecity.dune2r/files/
```

The native code searches Android app storage and its `data/` subdirectory. The
payload contains:

```text
config/
data/
mods/
imported_sprites/
```

## Remaining Work

1. Add touch camera panning and possibly a small command overlay for mobile.
2. Keep the original Dune II data licensing boundary explicit. The APK should
   not include copyrighted data unless the distributor has rights to bundle it.
