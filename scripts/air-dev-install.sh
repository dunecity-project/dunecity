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
# New features are on in the game itself, not in a separate mod (Stefan: "i just want it as a
# feature, not a mod"): the copy's shipped `config/QuantBot Config.ini.default` gets the switches
# set to 1 and the app is re-signed ad hoc. The game reseeds its built-in Dune City rules from that
# file at launch (ModManager::dunecityNeedsReseed), and the script makes Dune City the active mod.
#
#   AGENT_OWNER=FRAMEWORK:SESSION:AGENT scripts/air-dev-install.sh [--no-build] [--wait] [--switch NAME]...
#
# --no-build  copy build-int as it is (default: incremental build of HEAD first)
# --wait      if the game is running on the Air, wait for it to close, then swap
#             (default: leave the new app staged and swap at the next run)
# --switch    QuantBot switch to turn on; repeatable. Default: every switch in DEV_SWITCHES.
# The previous app is kept in /tmp/dunecity-dev-<sha> on the Air.
set -euo pipefail

DEV_SWITCHES=(ZoneGarrison ZoneCaptureRaid ZoneCaptureLevy ZoneGarrisonReinforce ZoneFrontGarrison
              ZoneCaptureChain SquadOrders InfantryLanes InfantryInMix)
HOST=${DUNECITY_AIR_HOST:-Stefans-MacBook-Air.local}
REPO=$(cd "$(dirname "$0")/.." && pwd)
BIN=$REPO/build-int/bin
build=1 wait=0 switches=()
while (($#)); do
    case $1 in
        --no-build) build=0 ;;
        --wait) wait=1 ;;
        --switch) switches+=("$2"); shift ;;
        *) sed -n '2,23p' "$0"; exit 2 ;;
    esac
    shift
done
((${#switches[@]})) || switches=("${DEV_SWITCHES[@]}")

if ((build)); then
    : "${AGENT_OWNER:?set AGENT_OWNER=FRAMEWORK:SESSION:AGENT for the build mutex}"
    agent-workspace stage --project dunecity --owner "$AGENT_OWNER" --wait build
fi
sha=$(git -C "$REPO" rev-parse --short=9 HEAD)
built=$(stat -f '%Sm' -t '%Y-%m-%d %H:%M' "$BIN/dunecity.app/Contents/MacOS/dunecity")
codesign --verify --deep --strict "$BIN/dunecity.app"
dest=/tmp/dunecity-dev-$sha
echo "staging HEAD $sha, build-int app built $built -> $HOST:$dest"

# Turn the switches on in a copy's shipped defaults; build-int itself stays the tested default.
work=$(mktemp -d /private/tmp/dunecity-air-dev.XXXXXX)
trap 'rm -rf "$work"' EXIT
ditto "$BIN/dunecity.app" "$work/dunecity.app"
defaults="$work/dunecity.app/Contents/Resources/config/QuantBot Config.ini.default"
for key in "${switches[@]}"; do
    grep -q "^$key=" "$defaults" || { echo "unknown switch $key" >&2; exit 1; }
    sed -i '' -E "s/^$key=.*/$key=1/" "$defaults"
done
codesign --force --deep --sign - "$work/dunecity.app" 2>/dev/null
codesign --verify --deep --strict "$work/dunecity.app"

ssh -o ConnectTimeout=10 "$HOST" "rm -rf '$dest' && mkdir -p '$dest'"
tar -C "$work" -cf - dunecity.app | ssh "$HOST" "tar -xf - -C '$dest'"

# Install, make built-in Dune City the active mod, report what the Air has.
ssh "$HOST" bash -s -- "$dest" "$wait" "$sha" "${switches[@]}" <<'REMOTE'
set -euo pipefail
dest=$1 wait=$2 sha=$3; shift 3
app=$dest/dunecity.app
codesign --verify --deep --strict "$app"
missing=$(otool -L "$app/Contents/MacOS/dunecity" | awk 'NR>1 && $1 ~ /^\/opt\/homebrew/ {print $1}' |
          while read -r lib; do [ -e "$lib" ] || echo "$lib"; done)
[ -z "$missing" ] || { echo "Air lacks Homebrew libraries:"; echo "$missing"; exit 1; }
running() { pgrep -f 'dunecity.app/Contents/MacOS' >/dev/null; }
if running && [ "$wait" = 1 ]; then
    echo "game running on the Air; waiting for it to close"
    while running; do sleep 30; done
fi
if running; then
    rm -rf /Applications/.dunecity-staged.app
    ditto "$app" /Applications/.dunecity-staged.app
    echo "STAGED: game is running; rerun with --no-build --wait, or swap after it closes:"
    echo "  mv /Applications/dunecity.app $dest/replaced.app && mv /Applications/.dunecity-staged.app /Applications/dunecity.app"
    exit 0
fi
mv /Applications/dunecity.app "$dest/replaced.app"
mv "$app" /Applications/dunecity.app
echo "$sha" > "$dest/source.txt"

mods="$HOME/Library/Application Support/Dune City/mods"
if [ "$(cat "$mods/active_mod.txt" 2>/dev/null)" = HouseToHouse ]; then
    printf dunecity > "$mods/active_mod.txt"
fi
# The House to House test mod (2026-11-28) stays installed but inactive: saves made under it name it.
version=$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' /Applications/dunecity.app/Contents/Info.plist)
echo "INSTALLED $sha ($version), app sha256 $(shasum -a 256 /Applications/dunecity.app/Contents/MacOS/dunecity | cut -c1-16)"
echo "on in the game's own Dune City rules: $*; active mod: $(cat "$mods/active_mod.txt" 2>/dev/null)"
REMOTE
