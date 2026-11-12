# Release and hosting operations

Verified 2026-09-10. This is the entry point for Codex, Claude, Hermes and human
maintainers. Check current branches, workflows and release state before acting;
version numbers and run IDs below are verification examples, not next-version defaults.

## Repositories and destinations

| Purpose | Repository / local checkout on Stefan's MacBook Air | Destination |
| --- | --- | --- |
| Game and release automation | `dunecity-project/dunecity`; `~/Documents/projects/dunecity` | GitHub Releases; SourceForge Files and dedicated source refs |
| Main website | `VR48/dunelegacy.com`; `~/Documents/projects/dunelegacy.com` | https://dunelegacy.com via **Deploy to Droplet** |
| Historical SourceForge repository | `ssh://svan058@git.code.sf.net/p/dunelegacy/code`; `~/Documents/projects/dunelegacy-code` | `master` holds Legacy history/old website; `dunecity` holds latest released game source |

The legacy website files are `sourceforge_website/` in both game checkouts.
Main website files are `website/` in the separate website repository. Do not
confuse pushing website source with publishing it to SourceForge web hosting.

## Organization transfer (2026-09-30)

The game repository now belongs to `dunecity-project`. The website repository
remains `VR48/dunelegacy.com`. GitHub Pages now uses
`https://dunecity-project.github.io/dunecity/`.

Keep the old repository locations vacant: recreating `ggtothemax/dunecity` or
`VR48/dunecity` can destroy redirects used by installed clients and release links.
The old update-feed and Dune2R catalog URLs were checked after transfer and still
returned the same content. Preserve the update-signing key for existing clients.

The in-game feedback service needs a token scoped to the new organization-owned
repository. Its successful responses must retain the old
`https://github.com/ggtothemax/dunecity/issues/N` prefix for installed clients.
See the website repository's `docs/game-feedback.md` for that compatibility rule.

## Signed desktop updates from 1.0.731

See [desktop updates](desktop-updates.md) for platform behavior, the local signing
key/profile prerequisites and validation. Stable Mac packages now require
Developer ID signing and Apple acceptance. The Windows EXE enables the updater;
the portable ZIP remains available. Stable feeds and binaries are uploaded to a
draft before publication. Do not replace assets in an already published release;
use a new version. No update feed is emitted for `latest-dev`.

## Development identity and official release opt-in

Normal staging, local agent builds and non-release CI builds use
`DUNECITY_RELEASE_BUILD=OFF` (the CMake default). Their runtime label is
`<last release version>dev`, currently `1.0.864dev`; do not bump the numeric version
just to identify development work. `CMAKE_BUILD_TYPE=Release` selects optimized
code, not official release identity.

Only an explicitly requested official build configures with
`-DDUNECITY_RELEASE_BUILD=ON`, giving the plain runtime label (currently `1.0.864`).
Apply that opt-in at each release channel's configure step:

| Channel | Official release identity |
|---|---|
| macOS, Windows, Linux | Stable-tag release jobs and deliberate local official builds pass `-DDUNECITY_RELEASE_BUILD=ON` to CMake. `scripts/build.sh --release-build` is the explicit helper opt-in. Ordinary staging/PR/main builds remain OFF. |
| Browser | The published stable-tag build and an authorized browser hotfix pass `-DDUNECITY_RELEASE_BUILD=ON` through `emcmake cmake` (or `scripts/package-web.ps1 -ReleaseBuild`). Ordinary web development/CI remains OFF. |
| iOS device, IPA, TestFlight | Use `scripts/build-ios.sh --device --release-build` for a deliberate official device build. `--ipa` and `--testflight` are explicit distribution requests and select ON automatically. Normal simulator/device builds default to dev. |
| Android | Official release APK builds pass `-DDUNECITY_RELEASE_BUILD=ON` to CMake (`scripts/package-android-apk.ps1 -BuildNative -BuildApk -ReleaseBuild`). Workflow package probes/candidates remain dev unless explicitly requested as official with both `android_package_probe=true` and `android_release_build=true`; a stable tag alone does not opt Android in. |

CMake caches this option: when returning an official build tree to staging or
agent work, explicitly reconfigure with `-DDUNECITY_RELEASE_BUILD=OFF`; do not make
dev builds official merely because they use Release optimizations or reuse a tree.
Keep `PROJECT_VERSION` and the `VERSION` macro numeric. Packaging/updater versions,
protocol/save/mod compatibility metadata, `CFBundleShortVersionString` and iOS
`CFBundleVersion` stay unchanged; the `dev` suffix belongs only to runtime display
identity (and macOS Finder's long version/Get Info description).

## Release checklist: every channel (verified for 1.0.864, 2026-11-01)

One release touches all of these; each is a separate gate. Report each one as verified,
held or untested. Never claim one channel from another channel's evidence.

| # | Channel | How it is produced | Verify |
|---|---|---|---|
| 1 | Version and notes | `scripts/bump-version.sh X.Y.Z` (+ `--check`). Bump the network protocol for deterministic AI/sim changes and the save version for format changes. Write `releases/desktop/X.Y.Z.md`. | `--check` passes; notes say which protocol/save players need |
| 2 | Tests on every computer | `dunecity-queue ctest` (below), one agent per native platform, PR CI | Report names the machine for each check |
| 3 | Windows, macOS, Linux | Merge the PR, push tag `vX.Y.Z`. **Build Dune Legacy** builds Windows EXE/ZIP, macOS DMG/ZIP (signed and notarized on the mini's self-hosted runner, `docs/macos-release-runner.md`), Linux AppImage/DEB/RPM/tar.gz, and signs the update feeds on the mini (`docs/desktop-updates.md`) | 13 assets on the GitHub release, which is marked Latest: 8 packages, 3 `updates-*.txt`, 2 appcasts |
| 4 | SourceForge Files | Automatic: **Sync SourceForge release** after the stable build | 7 packages + README + SHA256SUMS read back; `dunecity-vX.Y.Z` tag; three OS defaults |
| 5 | Browser (`/play`) | Automatic: **Publish browser build** commits to the website repo, then **Deploy to Droplet** | Live `play/build.json` has the version and the tag commit; every listed artifact hash matches; a real browser reaches the main menu |
| 6 | Website pages | The release job updates links, labels and `changelog.html`. The "New in X.Y.Z" prose on `website/index.html` and `website/dune-city.html` is manual | Live pages show the new prose; Android and TestFlight labels match what is actually published |
| 7 | iPhone/iPad (TestFlight) | `scripts/build-ios.sh --testflight` on the mini, then App Store Connect steps (below) | Build `VALID`, in the Public group, Beta App Review submitted |
| 8 | Android | Independent version line; signed with the existing upgrade key; needs Stefan's phone (below) | Published only after a phone test and Stefan's explicit "publish" |
| 9 | Records | `HANDOVER.md` entry (docs-only PR, `[skip ci]`); Hindsight memory | Machines per check and untested platforms are listed |

Order: 1 → 2 → merge + tag (3) → 4 and 5 start automatically. Do 6 after the release job's
website commit. 7 can run while 3 builds; 8 has its own gates. Cancel main's duplicate
**Build Dune Legacy** run started by the merge (and runs started by docs-only merges); never
cancel the tag build, the SourceForge sync or the browser publish.

Not part of a normal release: the legacy SourceForge web page (its download link follows
the SourceForge default), `/Applications` on Stefan's Mac (only when asked, see step 2
below), the Discord bot and the relay/signaling services (deploy them separately before a
client that needs them).

## Test on every available computer (Stefan, 2026-11-01)

Every build, release candidate and release is tested on all reachable machines in parallel,
not on the Mac mini alone (`~/.config/agent-workspace/POLICY.md`, "Use every available
computer"). The full suite runs through `dunecity-queue ctest --cwd <staging> --wait`, which
spreads it over the MacBook Pro, Windows PC, MacBook Air and mini. Native checks run on the
machine that is that platform: Windows packages on the Windows PC, iOS on the mini's
simulators, the browser build on the mini with the pinned Emscripten 4.0.14. Run one agent
per platform while PR CI runs. Android needs Stefan's phone. The release report names the
machine for each check and lists every untested platform.

## An authorized desktop release

1. Inspect dirty files and current tags; preserve other work. Follow `AGENTS.md`
   and `CLAUDE.md` version/build rules. Use `scripts/bump-version.sh` and its
   `--check` mode; commit all intended source before tagging.
2. If a local build was requested, build and verify it separately. Remote CI
   success does not update the app on Stefan's Mac. Use dependency audits and
   CTest as described in `AGENTS.md`; do not launch a game merely to check version.
   If local deployment is requested, also inspect `/Applications/dunecity.app`:
   it may be an independent stale installation rather than a link to the build.
   Preserve the old app, stage the new bundle, verify its signature, replace it,
   and compare version/binary SHA256 against `build/bin/dunecity.app`. Do not
   interrupt a running game or launch it merely for verification.
3. Merge through a pull request. Since the 2026-09-30 transfer to
   `dunecity-project/dunecity`, main ruleset 23003149 permits repository admins
   to bypass with Always allow. `ggtothemax` owns the organization and `VR48`
   has repository Admin access. After checks pass, an authorized release can use
   `gh pr merge --merge --admin --match-head-commit SHA`.
   Push the authorized release and its `vX.Y.Z` tag. **Build Dune Legacy** in
   `.github/workflows/build.yml` gates publication on tests and Windows, Linux
   and macOS success. Verify the EXE, portable ZIP, DMG, Mac update ZIP, AppImage, DEB, RPM,
   tar.gz and all signed update feeds.
4. The release job updates version/download links in the separate website repo's
   `website/index.html` and `website/dune-city.html`, using `WEBSITE_DEPLOY_KEY`.
   It also rebuilds the release history page `website/changelog.html` ("What's new")
   with that repo's `scripts/build-changelog.py`: each published release shows its
   `releases/desktop/X.Y.Z.md` notes (plus notes of untagged versions it includes),
   else filtered commit summaries. Write a notes file for every release you want
   described properly. Watch **Deploy to Droplet** there and check the live pages.
   Release prose ("New in X.Y.Z" on both pages) is not rewritten by version
   replacement; review it explicitly.
5. A successful stable-tag build triggers **Sync SourceForge release** in
   `.github/workflows/sourceforge.yml`. Verify its success separately. It copies
   the user-facing packages without rebuilding (seven from 1.0.731), adds README and SHA256SUMS,
   reads uploads back to verify hashes, publishes `dunecity-vX.Y.Z`, advances
   SourceForge's `dunecity` branch and changes the three OS defaults.
6. Report only destinations actually verified. Checksum/upload success, source
   refs, download defaults, main website deployment and local app are distinct.

**Source policy:** no source archive in SourceForge Files. README links to the
matching GitHub tag; source remains available in Git. Never force-push Legacy
master or mirror-delete historic releases. Website-only updates to Legacy master
are separate deliberate changes, such as `dc69c5a`.

## Retry without rebuilding

```sh
gh workflow run sourceforge.yml --repo dunecity-project/dunecity --ref main -f tag=vX.Y.Z
gh run list --repo dunecity-project/dunecity --workflow sourceforge.yml --limit 5
gh run watch RUN_ID --repo dunecity-project/dunecity --exit-status
```

Dispatch only an existing published stable tag. Historical backfills do not
change current download defaults or the source branch. A failed SourceForge run
does not roll back a GitHub release. Fix its specific failure and rerun the mirror;
do not create a new game version merely to retry an upload. Strict host checking,
missing secrets, conflicting tags or non-fast-forward refs must be investigated.
Infrastructure/docs-only pushes can use `[skip ci]` to avoid starting a game build;
do not skip required CI for game changes. SourceForge workflow_run dispatch occurs
only after successful stable-tag builds, not such documentation pushes.

## SourceForge presentation and web hosting

See [sourceforge-releases.md](sourceforge-releases.md) for credential **names**,
SSH transport, metadata, paths and evidence. The project description in
Admin → Metadata feeds both the project overview and download landing page.
Project display name is **Dune Legacy & Dune City**; shortname remains `dunelegacy`.

The old URL `https://dunelegacy.sourceforge.net/website/downloads.html` is a static
HTML page served separately from the main website. Its evergreen download link
uses SourceForge's platform default, so a normal release needs no version edit.
For content changes: edit/commit both tracked copies, push the intended repository
branches, back up the current remote file, SFTP only the changed file to a temporary
name under `/home/project-web/dunelegacy/htdocs/website/`, rename it into place,
read back and compare bytes, then inspect the public page. CDN caching can briefly
show old HTML; a cache-busting query helps distinguish that from a failed upload.
Keep old manuals/maps and other files. No PHP upgrade is needed for static HTML.

## Knowledge maintenance

Keep this runbook and SourceForge guide authoritative in Git; entry links exist
in both `AGENTS.md` and `CLAUDE.md`. Put dated outcomes in `HANDOVER.md`, correcting
stale current-state claims. When available, retain verified outcomes in Codex
context-memory and deliberately promote shared engineering knowledge; memory is
an index to evidence, not a replacement for repository documentation. Never put
private keys, API tokens, raw secrets or browser session data into docs or memory.

## Play Online browser releases

Play Online has its own Emscripten package. Updating desktop download links does
not update `website/play/`. Starting with 1.0.654, **Publish browser build**
(`.github/workflows/web.yml`) follows successful stable desktop release builds,
builds the latest published tag with Emscripten 4.0.14, packages it with
`scripts/package-web.py`, validates the website policy/hashes, and pushes the
website deploy. It refuses to overwrite a newer browser version. Check this run
and the website deployment separately from desktop and SourceForge publishing.

GitHub rejects files over 100 MB, and from 1.0.863 (Tornie-family mods) `dunecity.data`
exceeds that. `package-web.py` then commits it as ordered 64 MiB parts
(`dunecity.data.part-000`, ...) listed under `split` in `build.json`, whose `sha256` stays the
hash of the whole file. The website's `deploy/play_parts.py` joins the parts before CI
compression and in the server's staged web root, so browsers still download one file;
`check-web-security.py` hashes the parts in order. The live site serves only the joined
`dunecity.data`: the part URLs return 404 by design. Check the joined file against
`build.json`'s hash.

For an authorized browser hotfix, build current committed source locally:

```sh
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release -DDUNECITY_RELEASE_BUILD=ON -DDUNECITY_BUILD_TESTS=OFF -DDUNECITY_ENABLE_PCH=OFF
cmake --build build-web --parallel 8
python3 scripts/check-web-mods.py --build-root build-web
python3 scripts/package-web.py --build-root build-web --play-root ../dunelegacy.com/website/play
python3 ../dunelegacy.com/deploy/check-web-security.py
node --test scripts/tests/test-web-shell.cjs
```

A completed successful stable-tag build also provides a reusable
`DuneCity-Emscripten` artifact. For an authorized release, download that exact
run's artifact into a staging `bin/` directory, verify its source commit against
the tag, run `scripts/check-web-mods.py` with the staging parent as `--build-root`,
and package it using the matching tagged source. Include the matching
`package-p2p-service.py` output, validate website security and hashes, then
fetch/rebase the website changes and push. Recheck that GitHub latest still
matches the intended tag and refuse to downgrade a newer browser version.
Cancel only the redundant automatic browser rebuild for that release before
it publishes; do not cancel the stable release or SourceForge jobs. Record
both CI and website deployment IDs and perform the same live checks below.
This path was verified for 1.0.679 using build 34760679530 and website d38c374.

Windows `package-web.ps1` uses the same packager (Python 3 required). The manifest
records version, source commit, artifact hashes and packaging time. Every script,
WASM and data URL carries the same version/content token; unversioned artifacts
must revalidate. Verify the live `play/build.json` and download/hash every listed
artifact after deployment. Browser-test fresh defaults, Display aspect changes,
Options resolution changes, reload persistence, viewport resize and fullscreen.

Current clients use direct peer-to-peer gameplay. The PHP introduction service
provides rooms, invitations and signaling; it does not carry gameplay packets.
See `docs/direct-play.md` and `tools/p2p-signaling/README.md`. The packaged
manifest must identify `gameTransport=direct-webrtc`, contain no relay origins,
and match the candidate's source and artifact hashes.

Hold stable publication until actual public browser/browser and browser/native
gameplay, non-relay ICE selection, artifact checks and bounded client recovery
have passed. Exercise endpoint failures only against an owned isolated preview;
never stop a live production service or other players' rooms to manufacture a
recovery result. Where the production trusted-local analytics hook is enabled,
verify SQLite delivery for the acceptance test's own room through an authorized
server-side reader. Do not substitute unrelated legacy HMAC receiver tests.
Successful localhost tests or desktop CI alone do not establish public gameplay
readiness.

The relay, HTTPS polling queue, signed HTTP analytics receiver and Node watchdog
described in `tools/room-relay/deploy/README.md` belong to historical clients,
including 1.0.661. They are not the current client's transport or acceptance
path. Do not deploy a relay or TURN service for a current release.

### Introduction service and preview persistence during website deployments

Website deployments use rsync --delete, including scheduled statistics updates.
Introduction-service code and deployment/preservation rules must be committed
to the website repository before relying on them. Keep private configuration,
room/session state and SQLite data outside the replaced web root.
Untracked play-test-* previews can be removed by the next deployment; use an
explicitly preserved candidate preview or finish its tests before deploying.
Historical gateway persistence failures do not establish current P2P readiness.

## iPhone and iPad: TestFlight

Details: `IOS_PORT.md` ("Distribution"). Upload from the Mac mini, where the team's Apple ID
is signed into Xcode. The upload command uses no API key. Test first on the mini's
simulators (iPhone and iPad), then build the exact committed release tree:

```sh
agent-workspace run --project dunecity --owner OWNER --tests --wait -- nice -n 10 env JOBS=4 /bin/bash -c \
  'scripts/build-ios.sh --device --release-build && /usr/libexec/PlistBuddy -c "Print :NSBluetoothAlwaysUsageDescription" build-ios-device/bin/dunecity.app/Info.plist && scripts/build-ios.sh --testflight'
```

`--testflight` (like `--ipa`) selects official release identity automatically;
ordinary simulator/device builds do not. The `CFBundleVersion` build number remains
the UTC time (`YYYYMMDD.HHMM`); App Store Connect shows it without the leading zero
(`20261101.356`). After processing, use the App Store Connect API key
(1Password `Agent-Operator`, "App Store Connect API key", read with `agent-op`) to set the
en-US "What to Test" text, add the build to the **Public** group (public link
https://testflight.apple.com/join/nsEVY7Eb) and submit it for Beta App Review. A missing
usage string is rejected at upload (ITMS-90683 needed `NSBluetoothAlwaysUsageDescription`,
1.0.864); check `IDE/iOS/Info.plist` before uploading. The website's TestFlight engine label
changes only after Apple approves the build for external testing.

## Android

Android has its own version line (`android-version.json`, `net.dunecity.dune2r`, e.g.
0.2.33 / 1000553 on engine 1.0.858) and is never the repository's Latest release.

1. Official release candidate:
   `gh workflow run build.yml --ref BRANCH -f android_package_probe=true -f android_release_build=true`,
   then download `DuneCity-Android-Runner-Candidate`. The explicit release input selects
   `-DDUNECITY_RELEASE_BUILD=ON`; omit it for ordinary dev package probes. The runner
   signs it with a throwaway debug key, so it cannot be installed over a public APK.
2. Re-sign it locally with the existing upgrade key (certificate SHA-256
   `a29825bfecef94665244e1963e3c7dc64eadb387ae52820a793d26e193c21b60`, DN "Android Debug").
   Use `zipalign -P 16`, then `apksigner sign` and `verify`. Compare every non-signature entry
   before and after. Never upload the key to a hosted runner, and never generate a new key:
   installed copies could then no longer upgrade.
   **As of 1.0.864 nobody can find this keystore** on the mini, Air, Pro, Windows PC or in
   1Password. Once Stefan finds it, store it in 1Password `Agent-Operator`.
   **2026-11-07 update:** PRED's existing
   `%USERPROFILE%\.android\debug.keystore` was inspected read-only and its
   public certificate exactly matches the required upgrade SHA-256 above.
   The key was not exported, replaced or uploaded. Use that existing key
   locally; the phone-test and explicit publication gate below still apply.
3. Install with `adb install -r` on Stefan's phone, keeping app data. He tests it, and the APK
   is published only after he says "publish".
4. `gh release create android-vX.Y.Z DuneLegacy.apk SHA256SUMS.txt --target SOURCE_SHA --latest=false`,
   download it again and compare the hash. Then update the Android links and labels in both
   website pages.

When any of these gates is missing, Android stays on its previous version. Say so in the
notes, on the website labels and in the report: an older Android build cannot play online
with a newer protocol.

## In-game feedback service

From 1.0.664, the client POSTs to `https://dunelegacy.com/metaserver/feedback.php`.
Before publishing clients, deploy the corresponding website service and provision
`FEEDBACK_GITHUB_TOKEN` as documented in the website repository's
`docs/game-feedback.md`. Use a fine-grained token scoped to `ggtothemax/dunecity` with
Issues read/write only (plus mandatory Metadata read); never embed it in binaries,
web assets or Git. A successful native build alone does not activate this service.

### Direct-P2P release transition (1.0.663 candidate)

The candidate uses actual P2PKit RTC/framing in browsers and compatible libdatachannel
on desktop. Apache/PHP at `/p2p` handles admission, lobby/chat and introductions only.
No gameplay relay or TURN fallback is present. Native ENet remains available.

The web workflow copies matching PHP source with `scripts/package-p2p-service.py`
into the website repository's private `p2p-service` directory. Its normal deploy
installs a verified snapshot outside the webroot, preserves private configuration
and worker-owned state, and keeps the tracked `/p2p` entrypoint across hourly updates.
See that repository's `deploy/p2p-signaling.md`. Back up the existing SQLite database
before its additive schema-3 migration. Old relay records and clients stay compatible.
Package production browser clients with `--signaling-origin https://dunelegacy.com`.

Hold stable publication until full public browser/browser and browser/native matches
pass, including gameplay after signaling outage and SQLite runtime attribution.
Transport fixtures alone are insufficient. Networks unable to establish direct ICE
connectivity fail visibly; there is no hidden relay fallback.

## Native pointer alignment release gate

Before the next production release, require the native mouse/menu regression
with the candidate's bundled SDL libraries on the Mac mini. It must exercise
native pointer motion and button down/up through SDL and the real menu hit-test
path, not inject already-logical SDL events or call widget handlers directly.
Check off-center controls and control edges with Retina drawable/window scaling,
letterboxing, and fullscreen/windowed resolution transitions. Record the actual
window, drawable, logical and viewport sizes and loaded SDL identities.

A hidden-window initialization pass, center-only coordinate check, or skipped
opt-in test does not satisfy this gate. Keep production held if this check fails
or cannot run. Never run graphical or pointer-automation checks on the MacBook Air.

For the registered CTest gate, `DUNECITY_POINTER_RUNTIME_APP` is a CMake `PATH`
cache entry passed directly to the existing `--runtime-app` CLI. Its default is
`$<TARGET_BUNDLE_DIR:dunecity>`, preserving CI that already bundles the target app.
A local Homebrew build's `build-int/bin/dunecity.app` is not a packaged candidate:
select the dependency-fixed **installed** app explicitly. Do not credit a prior
packaged-app pass to a CTest command that selected a different, unbundled app.

On the mini, with `FINALPIN` set to the full committed candidate SHA and `OWNER`
to the workspace owner identity, release edit claims first and use the shared
staging lane. Configure the candidate path **before** the coherent build,
installation and fixture preparation so their CMake cache identity stays stable.
For example, the local 1.0.865 candidate sequence is:

```sh
CANDIDATE_PREFIX="/Users/stefan/Documents/projects/outputs/release-1.0.865/macos-local-${FINALPIN}/install"
CANDIDATE_APP="$CANDIDATE_PREFIX/dunecity.app"
agent-workspace run --project dunecity --owner "$OWNER" --wait -- \
  cmake -S . -B build-int -DDUNECITY_POINTER_RUNTIME_APP:PATH="$CANDIDATE_APP"
agent-workspace stage --project dunecity --owner "$OWNER" --wait --jobs 4 build
agent-workspace run --project dunecity --owner "$OWNER" --wait -- \
  cmake --install build-int --config Release --prefix "$CANDIDATE_PREFIX"
codesign --verify --deep --strict "$CANDIDATE_APP"
agent-workspace run --project dunecity --owner "$OWNER" --tests --wait -- \
  env DUNECITY_NATIVE_POINTER_TEST=1 ctest --test-dir build-int \
  --output-on-failure -R '^native_pointer_mapping_probe$'
```

The install rules bundle and fix up runtime dependencies; do not copy SDK
libraries into the build app as a substitute. The gate still requires every
dynamically loaded SDL library to reside inside the selected app, records its
runtime identities, and returns skip code 77 without the native opt-in. A skip
is not a passing release gate. CTest diagnostics remain under
`build-int/native-pointer-mapping-probe`.

For a direct full regression, retain the same packaged `CANDIDATE_APP` and set
`EVIDENCE_DIR` to a dedicated results directory:

```sh
DUNECITY_NATIVE_POINTER_TEST=1 python3 tests/menu/run-menu-probe.py \
  --pointer-mapping --build-dir build-int \
  --runtime-app "$CANDIDATE_APP" --output-dir "$EVIDENCE_DIR"
```

`--pointer-sdl-only` is a fast, no-game-startup diagnostic, not a substitute for
the full menu gate. Stable macOS CI runs the full packaged-library check before
artifact upload and retains its diagnostics on failure.

## Windows compile caching

The macOS release job also enables tests and runs `classic_desert_ui_tests`
and `perf_regression`, plus the mandatory Atreides, scenario-matrix and
free-for-all AI regressions, through CTest before packaging. Dependency audits bracket
that verification. A successful package compile alone is not evidence that
the native rendering/performance gate passed.

Windows CI uses Ninja with four concurrent MSVC compiler processes and sccache
for game object files. Precompiled headers are disabled in this CI configuration
so compilation is cacheable. Local Visual Studio builds retain their defaults.
Installer DLL collection uses the target output directory for either generator.
The vcpkg cache key hashes dependency fields rather than the app version;
existing cache entries remain available through restore prefixes. Object caches
use runner-image prefixes and a fresh entry per successful run. Main-branch
caches can be restored by later PR/tag builds; PR caches do not seed main.

For explicit validation, dispatch Build Dune Legacy with
`windows_cache_probe=true`. It runs Windows alone, deletes the build tree after
the first build, and requires at least 100 compiler-cache hits and a 90% hit rate
on the clean rebuild. It still validates the relay and packages the installers,
but does not publish a release. Check the first build's sccache statistics on a
second run to verify persisted cache reuse across fresh runners. Normal builds
perform only one compile and print cache statistics.
