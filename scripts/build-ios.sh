#!/usr/bin/env bash
# Build Dune City for the iOS Simulator or for iPhone/iPad hardware.
#
#   scripts/build-ios.sh                       Simulator build (build-ios-sim/)
#   scripts/build-ios.sh --run                 ...then boot a simulator, install and launch it
#   scripts/build-ios.sh --device              Device build (build-ios-device/), unsigned
#   scripts/build-ios.sh --device \
#       --identity "Apple Development: Name (TEAMID)" \
#       --profile ~/Downloads/DuneCity.mobileprovision \
#       [--bundle-id com.example.dunecity] [--install DEVICE]
#                                              ...signed, optionally installed with devicectl
#
# Needs full Xcode (not only the Command Line Tools), CMake, Ninja, ccache and a
# vcpkg checkout (VCPKG_ROOT, default ~/vcpkg). See IOS_PORT.md.
set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
MODE=simulator
RUN=0
IDENTITY=""
PROFILE=""
BUNDLE_ID="net.dunecity.DuneCity"
INSTALL_DEVICE=""
JOBS=${JOBS:-4}
DEPLOYMENT_TARGET=15.0
SIMULATOR_NAME="Dune City iPhone"

usage() { sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --device) MODE=device ;;
        --simulator) MODE=simulator ;;
        --run) RUN=1 ;;
        --identity) IDENTITY=${2:?}; shift ;;
        --profile) PROFILE=${2:?}; shift ;;
        --bundle-id) BUNDLE_ID=${2:?}; shift ;;
        --install) INSTALL_DEVICE=${2:?}; shift ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1" >&2; usage ;;
    esac
    shift
done

# xcode-select may still point at the Command Line Tools, which have no iOS
# SDK; switching it needs sudo, DEVELOPER_DIR does not.
if [[ -z "${DEVELOPER_DIR:-}" ]] && ! xcrun --sdk iphoneos --show-sdk-path >/dev/null 2>&1; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi
xcrun --sdk iphoneos --show-sdk-path >/dev/null 2>&1 || {
    echo "No iOS SDK found. Install Xcode, or set DEVELOPER_DIR to its Contents/Developer." >&2
    exit 1
}

VCPKG_ROOT=${VCPKG_ROOT:-$HOME/vcpkg}
[[ -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]] || {
    echo "vcpkg not found at $VCPKG_ROOT (set VCPKG_ROOT)." >&2
    exit 1
}
[[ -f "$REPO_ROOT/platform/web/node_modules/p2pkit/emscripten/include/p2pkit-wasm/packet.h" ]] || {
    echo "p2pkit SDK headers missing: run 'npm ci --ignore-scripts' in platform/web." >&2
    exit 1
}

if [[ "$MODE" == simulator ]]; then
    BUILD_DIR="$REPO_ROOT/build-ios-sim"
    TRIPLET=arm64-ios-simulator
    SYSROOT=iphonesimulator
else
    BUILD_DIR="$REPO_ROOT/build-ios-device"
    TRIPLET=arm64-ios
    SYSROOT=iphoneos
    if [[ -n "$IDENTITY" || -n "$PROFILE" || -n "$INSTALL_DEVICE" ]]; then
        if [[ -z "$IDENTITY" || -z "$PROFILE" ]]; then
            echo "Signing needs both --identity and --profile (an Apple Development certificate" >&2
            echo "and a provisioning profile for $BUNDLE_ID that includes the target device)." >&2
            exit 2
        fi
        [[ -f "$PROFILE" ]] || { echo "Provisioning profile not found: $PROFILE" >&2; exit 1; }
    fi
fi

LAUNCHERS=()
if command -v ccache >/dev/null 2>&1; then
    LAUNCHERS=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
               -DCMAKE_OBJC_COMPILER_LAUNCHER=ccache -DCMAKE_OBJCXX_COMPILER_LAUNCHER=ccache)
fi

cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="$SYSROOT" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT_TARGET" \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET="$TRIPLET" \
    -DVCPKG_OVERLAY_TRIPLETS="$REPO_ROOT/cmake/vcpkg-triplets" \
    -DDUNECITY_IOS_BUNDLE_ID="$BUNDLE_ID" \
    -DDUNECITY_BUILD_TESTS=OFF \
    "${LAUNCHERS[@]}"

nice -n 10 cmake --build "$BUILD_DIR" --parallel "$JOBS"

APP="$BUILD_DIR/bin/dunecity.app"
[[ -x "$APP/dunecity" ]] || { echo "Build produced no $APP" >&2; exit 1; }

if [[ "$MODE" == device && -z "$IDENTITY" ]]; then
    echo "Unsigned device build: $APP (pass --identity and --profile to sign and install)"
    exit 0
fi
if [[ "$MODE" == device ]]; then
    ENTITLEMENTS="$BUILD_DIR/entitlements.plist"
    PROFILE_PLIST="$BUILD_DIR/profile.plist"
    security cms -D -i "$PROFILE" > "$PROFILE_PLIST"
    /usr/libexec/PlistBuddy -x -c 'Print :Entitlements' "$PROFILE_PLIST" > "$ENTITLEMENTS"
    APP_ID=$(/usr/libexec/PlistBuddy -c 'Print :Entitlements:application-identifier' "$PROFILE_PLIST")
    TEAM_ID=$(/usr/libexec/PlistBuddy -c 'Print :TeamIdentifier:0' "$PROFILE_PLIST")
    if [[ "$APP_ID" == "$TEAM_ID.*" ]]; then
        # A wildcard profile (Xcode's "iOS Team Provisioning Profile: *") covers
        # any bundle ID, but the signature must name the app's own identifier.
        /usr/libexec/PlistBuddy -c "Set :application-identifier $TEAM_ID.$BUNDLE_ID" "$ENTITLEMENTS"
        /usr/libexec/PlistBuddy -c "Set :keychain-access-groups:0 $TEAM_ID.$BUNDLE_ID" "$ENTITLEMENTS" 2>/dev/null || true
    elif [[ "$APP_ID" != "$TEAM_ID.$BUNDLE_ID" ]]; then
        echo "Profile is for $APP_ID, not $TEAM_ID.$BUNDLE_ID (use --bundle-id)." >&2
        exit 1
    fi
    cp "$PROFILE" "$APP/embedded.mobileprovision"
    codesign --force --sign "$IDENTITY" --entitlements "$ENTITLEMENTS" --timestamp=none "$APP"
    codesign --verify --strict "$APP"
    echo "Signed device build: $APP"
    if [[ -n "$INSTALL_DEVICE" ]]; then
        xcrun devicectl device install app --device "$INSTALL_DEVICE" "$APP"
        xcrun devicectl device process launch --device "$INSTALL_DEVICE" "$BUNDLE_ID"
    fi
    exit 0
fi

echo "Simulator build: $APP"
[[ "$RUN" -eq 1 ]] || exit 0

UDID=$(xcrun simctl list devices available -j | python3 -c '
import json, sys
name = sys.argv[1]
for devices in json.load(sys.stdin)["devices"].values():
    for device in devices:
        if device["name"] == name:
            print(device["udid"]); sys.exit()
' "$SIMULATOR_NAME")
if [[ -z "$UDID" ]]; then
    # Use the general-purpose runtime (the one supporting the most iPhone
    # models; a point release can exist for a single new device) and its
    # newest iPhone, which simctl lists first.
    read -r RUNTIME DEVICE_TYPE < <(xcrun simctl list runtimes -j | python3 -c '
import json, sys
best = None
for r in json.load(sys.stdin)["runtimes"]:
    if r["platform"] != "iOS" or not r["isAvailable"]:
        continue
    phones = [d["identifier"] for d in r.get("supportedDeviceTypes", []) if d.get("productFamily") == "iPhone"]
    key = (len(phones), [int(p) for p in r["version"].split(".")])
    if phones and (best is None or key > best[0]):
        best = (key, r["identifier"], phones[0])
if best: print(best[1], best[2])
')
    [[ -n "${RUNTIME:-}" ]] || { echo "No iOS simulator runtime: run 'xcodebuild -downloadPlatform iOS'." >&2; exit 1; }
    UDID=$(xcrun simctl create "$SIMULATOR_NAME" "$DEVICE_TYPE" "$RUNTIME")
fi
xcrun simctl bootstatus "$UDID" -b >/dev/null
# The simulator runs headless; show it in Simulator.app (Xcode 26 and older)
# or DeviceHub.app (Xcode 27), whichever this Xcode has.
DEV_DIR=${DEVELOPER_DIR:-$(xcode-select -p)}
for viewer in "$DEV_DIR/Applications/Simulator.app" "$DEV_DIR/../Applications/DeviceHub.app"; do
    if [[ -d "$viewer" ]]; then
        open "$viewer"
        break
    fi
done
xcrun simctl install "$UDID" "$APP"
xcrun simctl launch "$UDID" "$BUNDLE_ID"
echo "Launched $BUNDLE_ID on $SIMULATOR_NAME ($UDID)"
