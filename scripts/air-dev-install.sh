#!/bin/bash
# Put the staging build on Stefan's MacBook Air as a dev build, before full testing,
# so he can play the newest features (Stefan, 2026-11-28: "we need a method whereby latest
# features can be deployed on mba as a dev build prior to extensive testing").
#
# The app is the shared build-int tree's dunecity.app: Homebrew-linked, ad-hoc signed, built
# from committed staging HEAD by `agent-workspace stage build`. The Air has the same Homebrew
# libraries; the script refuses if any is missing. It is not the official static staging
# package and qualifies nothing.
#
# New features are part of the game, not a separate mod (Stefan: "i just want it as a
# feature, not a mod"). Garrisoning and Capturing uses the normal default-on Dune City menu option.
# Only DEV_SWITCHES are forced on in this copy's shipped defaults, then the app is re-signed.
# Each temporary copy gets one local copy-time/source version in its resource and plist;
# build-int and official package identities are never changed.
# The game reseeds those defaults at launch; the script keeps built-in Dune City active.
#
#   AGENT_OWNER=FRAMEWORK:SESSION:AGENT scripts/air-dev-install.sh [--no-build|--swap-staged] [--wait] [--switch NAME]...
#
# --no-build  copy build-int as it is (default: incremental build of HEAD first)
# --swap-staged  swap the already copied exact-source app if the game has exited; no recopy
# --wait      if the game is running on the Air, wait for it to close, then swap
#             (default: leave the new app staged and swap at the next run)
# --switch    QuantBot switch to turn on; repeatable. Default: every switch in DEV_SWITCHES.
# DUNECITY_AIR_BUILD_SOURCE pins an existing artifact when staging HEAD has advanced.
# The previous app is kept in /tmp/dunecity-dev-<sha> on the Air.
set -euo pipefail

DEV_SWITCHES=(EconomyFirst RepairReturnCheck SquadOrders InfantryLanes InfantryInMix)

prepare_dev_app() {
    local source=$1 app=$2 sha=$3; shift 3
    ditto "$source" "$app"
    local defaults="$app/Contents/Resources/config/QuantBot Config.ini.default"
    local key
    for key in "$@"; do
        grep -q "^$key=" "$defaults" || { echo "unknown switch $key" >&2; exit 1; }
        sed -i '' -E "s/^$key=.*/$key=1/" "$defaults"
    done
    printf '%s\n' "$sha" > "$app/Contents/Resources/air-dev-source.txt"
    python3 - "$app" "$sha" <<'PY'
from datetime import datetime
from pathlib import Path
import plistlib
import sys

app, sha = Path(sys.argv[1]), sys.argv[2]
plist = app / 'Contents/Info.plist'
with plist.open('rb') as stream:
    info = plistlib.load(stream)
base = info['CFBundleShortVersionString']
# Microseconds distinguish rapid copies of the same built source without rebuilding.
stamp = datetime.now().strftime('%Y%m%d.%H%M%S.%f')
version = f'{base}-t{stamp}-{sha[:7]}'
info['CFBundleShortVersionString'] = info['CFBundleVersion'] = version
if 'CFBundleGetInfoString' in info:
    info['CFBundleGetInfoString'] = f'Dune City {version}'
with plist.open('wb') as stream:
    plistlib.dump(info, stream)
(app / 'Contents/Resources/air-dev-version.txt').write_text(version + '\n')
print(f'LOCAL_COPY_READY {sha} ({version})')
PY
    codesign --force --deep --sign - "$app" 2>/dev/null
    codesign --verify --deep --strict "$app"
}

copy_dev_app() {
    export -f prepare_dev_app
    # Only local copy/sign/transfer owns this shared resource lease. In
    # particular, a remote game wait/swap never holds it or the project mutex.
    agent-workspace performance-lock --project dunecity --shared -- bash -c '
        set -euo pipefail
        source=$1 work=$2 sha=$3 host=$4 dest=$5; shift 5
        built=$(stat -f "%Sm" -t "%Y-%m-%d %H:%M" "$source/Contents/MacOS/dunecity")
        codesign --verify --deep --strict "$source"
        echo "artifact source $sha, built $built -> $host:$dest"
        prepare_dev_app "$source" "$work/dunecity.app" "$sha" "$@"
        ssh -o BatchMode=yes -o ConnectTimeout=10 "$host" "mkdir -p \"$dest\"; rm -rf \"$dest/dunecity.app\""
        tar -C "$work" -cf - dunecity.app | ssh -o BatchMode=yes -o ConnectTimeout=10 "$host" "tar -xf - -C \"$dest\""
    ' -- "$@"
}

HOST=${DUNECITY_AIR_HOST:-Stefans-MacBook-Air.local}
REPO=$(cd "$(dirname "$0")/.." && pwd)
BIN=$REPO/build-int/bin
build=1 wait=0 swap=0 switches=()
while (($#)); do
    case $1 in
        --no-build) build=0 ;;
        --swap-staged) build=0; swap=1 ;;
        --wait) wait=1 ;;
        --switch) switches+=("$2"); shift ;;
        *) sed -n '2,23p' "$0"; exit 2 ;;
    esac
    shift
done
if ((${#switches[@]} == 0 && ${#DEV_SWITCHES[@]} > 0)); then switches=("${DEV_SWITCHES[@]}"); fi

if ((build)); then
    : "${AGENT_OWNER:?set AGENT_OWNER=FRAMEWORK:SESSION:AGENT for the build mutex}"
    agent-workspace stage --project dunecity --owner "$AGENT_OWNER" --wait build
fi
sha=${DUNECITY_AIR_BUILD_SOURCE:-}
if [ -z "$sha" ]; then
    sha=$(python3 - "$REPO/build-int/air-build.json" <<'PY'
import json, sys
receipt = json.load(open(sys.argv[1]))
if receipt['status'] != 'built':
    raise SystemExit('No successful build source receipt')
print(receipt['sha'])
PY
)
fi
[[ "$sha" =~ ^[0-9a-f]{40}$ ]] || { echo "a full built-source SHA is required" >&2; exit 2; }
dest=/tmp/dunecity-dev-${sha:0:9}
if ((!swap)); then
    # Do not hash/copy/sign a large app on every tick while the Air is asleep.
    ssh -o BatchMode=yes -o ConnectTimeout=5 "$HOST" true

    # Turn the switches on in a copy's shipped defaults; build-int stays the tested default.
    work=$(mktemp -d /private/tmp/dunecity-air-dev.XXXXXX)
    trap 'rm -rf "$work"' EXIT
    copy_dev_app "$BIN/dunecity.app" "$work" "$sha" "$HOST" "$dest" "${switches[@]+"${switches[@]}"}"
fi

# Install, make built-in Dune City the active mod, report what the Air has.
ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" bash -s -- "$dest" "$wait" "$sha" "$swap" "${switches[@]+"${switches[@]}"}" <<'REMOTE'
set -euo pipefail
dest=$1 wait=$2 sha=$3 swap=$4; shift 4
staged=/Applications/.dunecity-staged.app
app=$dest/dunecity.app
if [ "$swap" = 1 ]; then app=$staged; fi
marker=$app/Contents/Resources/air-dev-source.txt
[ -f "$marker" ] && [ "$(cat "$marker")" = "$sha" ] || {
    echo "staged artifact missing or belongs to another source" >&2; exit 4;
}
codesign --verify --deep --strict "$app"
version=$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$app/Contents/Info.plist")
missing=$(otool -L "$app/Contents/MacOS/dunecity" | awk 'NR>1 && $1 ~ /^\/opt\/homebrew/ {print $1}' |
          while read -r lib; do [ -e "$lib" ] || echo "$lib"; done)
[ -z "$missing" ] || { echo "Air lacks Homebrew libraries:"; echo "$missing"; exit 1; }
if [ "$swap" != 1 ]; then
    rm -rf "$staged"
    mv "$app" "$staged"
fi
running() { pgrep -f 'dunecity.app/Contents/MacOS' >/dev/null; }
if running && [ "$wait" = 1 ]; then
    echo "game running on the Air; waiting for it to close"
    while running; do sleep 1; done
fi
if running; then
    echo "STAGED $sha ($version); game is running, next tick swaps after it exits"
    exit 0
fi
backup=$dest/replaced-$(date +%s).app
mv /Applications/dunecity.app "$backup"
if ! mv "$staged" /Applications/dunecity.app; then
    mv "$backup" /Applications/dunecity.app
    exit 1
fi
echo "$sha" > "$dest/source.txt"

mods="$HOME/Library/Application Support/Dune City/mods"
if [ "$(cat "$mods/active_mod.txt" 2>/dev/null)" = HouseToHouse ]; then
    printf dunecity > "$mods/active_mod.txt"
fi
# The House to House test mod (2026-11-28) stays installed but inactive: saves made under it name it.
echo "INSTALLED $sha ($version), app sha256 $(shasum -a 256 /Applications/dunecity.app/Contents/MacOS/dunecity | cut -c1-16)"
echo "Garrisoning and Capturing follows the Dune City menu default; extra dev switches: $*; active mod: $(cat "$mods/active_mod.txt" 2>/dev/null)"
REMOTE
