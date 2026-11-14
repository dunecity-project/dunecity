#!/usr/bin/env bash
# Stable callers hold agent-workspace's shared heavy-work gate for this entire command.
set -euo pipefail
cd "$GITHUB_WORKSPACE"
# Only reached after the approved workflow's atomic workspace/queue admission.
# A later compile/test failure must never masquerade as an unavailable runner.
if [ "${1:-}" = release-headless-qualification ] && [ "${RUNNER_ENVIRONMENT:-}" = self-hosted ]; then
    touch "$RUNNER_TEMP/qualification-admitted-$GITHUB_RUN_ID-$GITHUB_RUN_ATTEMPT"
fi
if [ "${1:-build}" = ios-staging ]; then
    : "${IOS_BUILD_NUMBER:?The workflow must provide its unique internal build number}"
    npm ci --ignore-scripts --prefix platform/web
    JOBS=4 bash scripts/build-ios.sh --internal-staging --build-number "$IOS_BUILD_NUMBER"
    exit 0
fi
if [ "${1:-build}" = feeds ]; then
    [ "${DUNECITY_RELEASE_BUILD:-OFF}" = ON ] || { echo 'Official feeds require explicit release identity' >&2; exit 1; }
    VERSION="${UPDATE_TAG#v}"
    KEY_DIR="$HOME/Library/Application Support/DuneCity Signing/updates"
    BASE="https://github.com/${UPDATE_REPO}/releases/download/${UPDATE_TAG}"
    for SPEC in "macos-arm64:macos:DuneCity-${VERSION}-macOS.zip" "windows-x64:windows:DuneCity-${VERSION}-Windows-x64.exe" "linux-x86_64:linux:DuneCity-${VERSION}-Linux-x86_64.AppImage"; do
        IFS=: read -r PLATFORM FOLDER FILE <<< "$SPEC"
        "$RUNNER_TEMP/dunecity-update-signing-python/bin/python" scripts/prepare-desktop-updates.py --version "$VERSION" --platform "$PLATFORM" \
            --archive "update-input/$FOLDER/$FILE" --base-url "$BASE" \
            --key-file "$KEY_DIR/update-ed25519.pem" --password-file "$KEY_DIR/update-key-password" \
            --output update-feeds
    done
    exit 0
fi
# Lease one outside-checkout cache for every preparation/game in this lane.
# The private re-entry argument prevents inherited environment from silently
# replacing the lane's lifecycle with an unowned caller-selected directory.
if [ "${1:-}" = --leased-headless-cache ]; then
    shift
    : "${DUNECITY_HEADLESS_MOD_CACHE:?Missing lane cache lease}"
else
    exec python3 tests/performance/probe_profile.py -- \
        bash "$0" --leased-headless-cache "$@"
fi
export HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_CLEANUP=1 HOMEBREW_NO_ENV_HINTS=1
STABLE="${DUNECITY_RELEASE_BUILD:-OFF}"
MODE="${1:-build}"
if [ "$MODE" = release-headless-qualification ] && {
    [ "$STABLE" != OFF ] || [ "${PACKAGE_BUILD:-false}" != false ];
}; then
    echo '::error::Headless qualification requires dev identity and PACKAGE_BUILD=false'
    exit 2
fi
export CMAKE_BUILD_PARALLEL_LEVEL=2
if [ "$STABLE" = ON ] || [ "${RUNNER_ENVIRONMENT:-}" = self-hosted ]; then
    export CMAKE_BUILD_PARALLEL_LEVEL=4
fi
POINTER_OUT="$RUNNER_TEMP/dunecity-pointer-mapping-${GITHUB_RUN_ID}-${GITHUB_RUN_ATTEMPT}"
SIGNED_OUT="$RUNNER_TEMP/dunecity-mac-signed-${GITHUB_RUN_ID}-${GITHUB_RUN_ATTEMPT}"
mkdir -p "$POINTER_OUT"
printf '{"status":"not-run","reason":"packaged pointer gate has not started"}\n' > "$POINTER_OUT/gate-status.json"
echo "Host: $(hostname); macOS $(sw_vers -productVersion); Xcode $(xcode-select -p)"
echo "Free disk: $(df -h / | awk 'NR==2 {print $4}')"
command -v brew >/dev/null || { echo '::error::Mac runner requires Homebrew'; exit 1; }
WANT=(autoconf autoconf-archive automake cmake libtool pkg-config nasm ninja)
INSTALLED=$(brew list --formula -1)
MISSING=()
for formula in "${WANT[@]}"; do
    grep -qx "$formula" <<<"$INSTALLED" || MISSING+=("$formula")
done
if [ ${#MISSING[@]} -gt 0 ]; then brew install "${MISSING[@]}"; fi
npm ci --ignore-scripts --prefix platform/web
# The checkout, toolchain and dependency cache are the same as the workflow's
# run-vcpkg step. Do not reuse a CMake tree from another checkout/platform.
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DDUNECITY_RELEASE_BUILD="$STABLE" \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=arm64-osx -DDUNECITY_BUILD_TESTS=ON
# Snapshot installed static SDL bytes and their SPDX/ABI recipes before the game
# link. The later diagnostic export refuses any source/toolchain/SDK drift.
if [ "${PACKAGE_BUILD:-false}" = true ] && [ "$MODE" != release-headless-qualification ]; then
    python3 tests/menu/pointer_artifact.py snapshot --build-dir build --output build/pointer-producer-inputs.json \
        --status-output "$POINTER_OUT/gate-status.json"
fi
cmake --build build --config Release --parallel "$CMAKE_BUILD_PARALLEL_LEVEL"
python3 scripts/check-build-deps.py build
if [ "$MODE" = release-headless-qualification ]; then
    # Single-game probes reserve two CTest slots. The Mini can overlap two;
    # the smaller hosted Mac keeps one, alongside eligible lightweight tests.
    TEST_SLOTS=2
    if [ "${RUNNER_ENVIRONMENT:-github-hosted}" = self-hosted ]; then TEST_SLOTS=4; fi
    echo "Headless qualification: ${RUNNER_ENVIRONMENT:-github-hosted}, $TEST_SLOTS CTest slots"
    # PHP-backed network coverage remains mandatory in the stable Mini lane.
    # Both backends use the same qualification inventory and receipt recipe.
    ctest --test-dir build --show-only=json-v1 -L headless -L nightly -LE network > build/macos-headless-plan.json
    ctest_status=0
    ctest --test-dir build --output-on-failure --no-tests=error --parallel "$TEST_SLOTS" \
        -L headless -L nightly -LE network --output-junit "$GITHUB_WORKSPACE/build/macos-headless-results.xml" || ctest_status=$?
    # CTest 8 means test failures; only exact committed decisions can accept them.
    # Launch/configuration failures and incomplete/skipped XML still fail closed.
    if [ "$ctest_status" -ne 0 ] && [ "$ctest_status" -ne 8 ]; then exit "$ctest_status"; fi
    python3 scripts/release-automation.py check-headless --directory build --ctest-status "$ctest_status"
    build/bin/dunecity.app/Contents/MacOS/dunecity --check-relay-support
    exit 0
fi
ctest --test-dir build --output-on-failure --no-tests=error -L integration -LE gui
if [ "$STABLE" = ON ]; then
    command -v php >/dev/null || [ -x /opt/homebrew/bin/php ] || {
        echo '::error::Stable Mini lane requires PHP for the headless network tests'; exit 1; }
    ctest --test-dir build --output-on-failure --no-tests=error -L headless -L network \
        --output-junit "$POINTER_OUT/macos-network-results.xml"
    if grep -q '<skipped' "$POINTER_OUT/macos-network-results.xml"; then
        echo '::error::A headless network test was skipped on the Mini'; exit 1
    fi
fi
if [ "${TEST_TIER:-integration}" = nightly ]; then
    # Headless nightly is owned by qualify-release.yml; this lane owns GUI nightly.
    ctest --test-dir build --output-on-failure --no-tests=error --parallel 2 -L nightly -L gui
fi
build/bin/dunecity.app/Contents/MacOS/dunecity --check-relay-support
if [ "$STABLE" = ON ] || [ "${MACOS_SKIN_PROBE:-false}" = true ]; then
    # Calibrated GUI performance stays on Mini; bulk headless nightly runs hosted.
    ctest --test-dir build --output-on-failure --no-tests=error -R '^perf_regression$'
    python3 tests/skins/run-skin-probe.py --build-dir build \
        --output-dir "$RUNNER_TEMP/dunecity-skin-probe-$GITHUB_RUN_ID-$GITHUB_RUN_ATTEMPT"
    python3 scripts/check-build-deps.py build
fi
if [ "${PACKAGE_BUILD:-false}" != true ]; then exit 0; fi
VERSION=$(sed -n 's/^project(DuneCity VERSION \([0-9]*\.[0-9]*\.[0-9]*\).*/\1/p' CMakeLists.txt)
rm -f build/*-macOS.dmg build/*-macOS.zip build/macos-signing.json
cmake --install build --prefix "$RUNNER_TEMP/dunecity-mac-install" --config Release
if [ "$STABLE" = ON ]; then
    MAC_SIGNING_IDENTITY='Developer ID Application: Stefan van der Wel (34X7AYJZ93)'
    python3 scripts/unlock-macos-signing.py
    python3 scripts/package-signed-macos.py \
        --app "$RUNNER_TEMP/dunecity-mac-install/dunecity.app" --output "$SIGNED_OUT" \
        --identity "$MAC_SIGNING_IDENTITY" \
        --signing-keychain "$HOME/Library/Keychains/DuneCity-Signing.keychain-db" \
        --notary-profile DuneCityNotarization \
        --notary-keychain "$HOME/Library/Keychains/DuneCity-Signing.keychain-db"
    cp "$SIGNED_OUT/"*.dmg "$SIGNED_OUT/"*.zip build/
    # Read back the identity from the exact final ZIP, not the unsigned install tree.
    ditto -x -k "build/DuneCity-${VERSION}-macOS.zip" "$RUNNER_TEMP/dunecity-mac-signed-check"
    codesign --verify --deep --strict "$RUNNER_TEMP/dunecity-mac-signed-check/dunecity.app"
    codesign -dv --verbose=4 "$RUNNER_TEMP/dunecity-mac-signed-check/dunecity.app" 2> "$POINTER_OUT/signing-identity.txt"
    grep -Fx 'Authority=Developer ID Application: Stefan van der Wel (34X7AYJZ93)' "$POINTER_OUT/signing-identity.txt"
    grep -Fx 'TeamIdentifier=34X7AYJZ93' "$POINTER_OUT/signing-identity.txt"
    xcrun stapler validate "$RUNNER_TEMP/dunecity-mac-signed-check/dunecity.app"
    python3 - <<'PY'
import hashlib, json, os, pathlib, subprocess
paths = list(pathlib.Path('build').glob('*-macOS.zip')) + list(pathlib.Path('build').glob('*-macOS.dmg'))
pathlib.Path('build/macos-signing.json').write_text(json.dumps({
    'sourceCommit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
    'identity': 'Developer ID Application: Stefan van der Wel (34X7AYJZ93)',
    'runId': os.environ['GITHUB_RUN_ID'],
    'sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
}, indent=2) + '\n')
PY
else
    # Hosted staging is unsigned/ad-hoc; it never opens the Mini's signing keychain.
    (cd build && cpack -G DragNDrop -C Release)
    ditto -c -k --sequesterRsrc --keepParent "$RUNNER_TEMP/dunecity-mac-install/dunecity.app" \
        "build/DuneCity-${VERSION}-macOS.zip"
fi
# Full pointer logic is compiled only in this normal producer's existing cached
# frontend/build/SDK context. Nothing diagnostic enters the app or public assets.
POINTER_APP="$RUNNER_TEMP/dunecity-mac-install/dunecity.app"
if [ "$STABLE" = ON ]; then POINTER_APP="$RUNNER_TEMP/dunecity-mac-signed-check/dunecity.app"; fi
POINTER_ARGS=(--pointer-mapping --build-dir build --runtime-app "$POINTER_APP"
    --producer-inputs build/pointer-producer-inputs.json --package-zip "build/DuneCity-${VERSION}-macOS.zip"
    --export-pointer-artifact "build/pointer-diagnostic-${GITHUB_RUN_ID}-${GITHUB_RUN_ATTEMPT}" --output-dir "$POINTER_OUT")
if [ "$STABLE" != ON ]; then
    python3 tests/menu/run-menu-probe.py "${POINTER_ARGS[@]}" --prepare-only
fi
if [ "$STABLE" = ON ]; then
    # Keep environment coverage distinct from a game assertion. Own-window NSEvents
    # do not require global event-posting permission, but record that permission.
    export POINTER_OUT
    python3 - <<'PY'
import ctypes, json, os, pathlib, subprocess
out = pathlib.Path(os.environ['POINTER_OUT'])
try:
    cg = ctypes.CDLL('/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics')
    display = cg.CGMainDisplayID()
    cg.CGDisplayPixelsWide.restype = cg.CGDisplayPixelsHigh.restype = ctypes.c_size_t
    geometry = [cg.CGDisplayPixelsWide(display), cg.CGDisplayPixelsHigh(display)]
    console_uid = pathlib.Path('/dev/console').stat().st_uid
    ready = console_uid == os.getuid() and min(geometry) > 0
    permission = bool(cg.CGPreflightPostEventAccess()) if hasattr(cg, 'CGPreflightPostEventAccess') else None
    evidence = dict(status='ready' if ready else 'blocked-environment', consoleUid=console_uid,
                    runnerUid=os.getuid(), displayPixels=geometry, globalEventPostingAllowed=permission)
except Exception as error:
    ready = False
    evidence = dict(status='blocked-environment', reason=str(error))
(out / 'environment.json').write_text(json.dumps(evidence, indent=2) + '\n')
if not ready:
    (out / 'gate-status.json').write_text(json.dumps(evidence, indent=2) + '\n')
    raise SystemExit('Packaged GUI coverage BLOCKED: no owned console desktop/display; see environment.json')
PY
    set +e
    DUNECITY_NATIVE_POINTER_TEST=1 python3 tests/menu/run-menu-probe.py "${POINTER_ARGS[@]}" > "$POINTER_OUT/wrapper.log" 2>&1
    STATUS=$?
    set -e
    cat "$POINTER_OUT/wrapper.log"
    export STATUS
    python3 - <<'PY'
import json, os, pathlib
out = pathlib.Path(os.environ['POINTER_OUT'])
status = int(os.environ['STATUS'])
receipt = json.loads((out / 'gate-status.json').read_text())
state = receipt['status']
passed = status == 0 and state == 'passed' and receipt.get('releaseGateSatisfied') is True
print('Packaged pointer gate: ' + state)
if status == 0 and not passed:
    raise SystemExit('Pointer wrapper returned zero without the full native gate receipt')
PY
    if [ "$STATUS" -ne 0 ]; then
        for log in "$POINTER_OUT"/run-*.log; do [ ! -f "$log" ] || cat "$log"; done
        exit "$STATUS"
    fi
fi
bash scripts/verify-macos-dmg.sh build/DuneCity-*-macOS.dmg
