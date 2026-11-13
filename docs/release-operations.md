# Release and hosting operations

Operating contract updated 2026-11-13. This is the entry point for Codex, Claude,
Hermes and human maintainers. Check current branches, workflows and release state
before acting; historical version numbers/run IDs below are examples, not defaults.

Read [Release execution contract](#release-execution-contract) before starting
qualification. It governs readiness, parallel execution, evidence reuse and stalls.

## Repositories and destinations

| Purpose | Repository / local staging checkout | Destination |
| --- | --- | --- |
| Game and release automation | `dunecity-project/dunecity`; `~/Documents/projects/dunecity-feature-batch` (`integrate/feature-batch`, `build-int`) | GitHub Releases; SourceForge Files and dedicated source refs |
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
`<last release version>dev`; do not bump the numeric version just to identify
development work. `CMAKE_BUILD_TYPE=Release` selects optimized
code, not official release identity.

Only an explicitly requested official build configures with
`-DDUNECITY_RELEASE_BUILD=ON`, giving the plain numeric runtime label.
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

## Release execution contract

Use the existing workspace coordinator, queue and CI; do not build another release
orchestrator. These rules change execution order, not the required channel gates.

### Establish readiness before long runs

1. Name one release coordinator and pin the full 40-hex committed source SHA.
   Record required gates and their owners in the release evidence directory under
   `~/Documents/projects/outputs/release-X.Y.Z/`. Each gate records its command,
   source SHA, configuration, machine, state, timestamps and evidence path.
2. Read `~/.local/state/compute-watch/latest.md`,
   `~/.local/state/agent-workspace/reports/compute-watch-alerts.md`,
   `agent-workspace status` and `dunecity-queue status -v`. Check actual host
   reachability, admission holds and platform capability. AWS game capacity is
   not CTest capacity. Air work must be positively classified headless.
3. Budget physical free bytes for the next phase: the unchanged 25 GB hard reserve
   plus its measured or conservatively bounded peak scratch/package footprint.
   Research admission on Mini requires 60 GB free; release phases use their own
   explicit budget rather than overriding the hard floor. Recheck before each
   phase. Clean registered disposable assets or offload verified copies, never
   active artifacts, pinned snapshots or sole-copy failure evidence.
4. Inspect the harness at the pinned SHA, its required assets and packaged-runtime
   inputs. Prove admission and one representative required test/game before
   submitting a large dependent matrix. Count that canary toward its required
   scope where valid; do not invent an extra suite. AI observer-off canaries must
   prove the strict verifier receives required per-house research evidence.
   An earlier source-cache prewarm is not proof the archive still exists: verify
   its identity immediately before consumption using existing canonical checks.

### Execute independent work concurrently

Start PR CI and independent native-platform checks together with readiness work.
Before a production qualification window, acquire the queue's durable production
reservation and pause new research admission. Let owned active work drain, retain
artifact pins, and use the reservation's explicit release token for release work.
Never override another owner's reservation or cancel unrelated jobs. Keep
Mini-only GUI, signing/packaging and simulator phases on one ordered lane.
Fan suitable pinned AI games out to reachable Linux/AWS hosts after the canary.
No Pro/PC availability assumption; unavailable platform coverage is held, not passed.
The research controller stays paused until release evidence is recorded and the
reservation is explicitly released. The one-heavy-build rule still applies.

Capture `dunecity-queue reservation pause --cwd "$CHECKOUT" --rev "$FULL_SHA"
--owner "$OWNER" --reason production --budget-gb "$PHASE_BUDGET_GB"` as private
JSON outside uploaded evidence. Its `id` is a bearer token: never print it.
Use `reservation phase TOKEN Mini-reserved --owner OWNER`, followed by
`building`, `publishing`, `verifying` and `completed`, attaching measured budgets,
artifact paths and run IDs. Only the owner explicitly calls `reservation resume`;
a completed upload does not implicitly resume research.

Use one pinned queue submission per positively classified headless scope:

```sh
agent-workspace run --project dunecity --owner OWNER --tests --wait -- \
  dunecity-queue ctest --cwd /Users/stefan/Documents/projects/dunecity-feature-batch \
  --rev FULL_SHA --priority 5 --tag release-X.Y.Z --owner OWNER -R CLASSIFIED_REGEX
# Capture the returned JOB_ID; follow outside the workspace wrapper:
dunecity-queue report JOB_ID --follow
```

Replace placeholders with the actual identity, full SHA and headless test
allowlist. Partition GUI/headless scopes without omissions or overlap.
`dunecity-queue ctest` has no host-selection flag: a graphical regex alone does
not keep work off the Air. Run graphical scopes through the existing coordinated
native lane on a verified non-Air host, for example on the Mini:

```sh
agent-workspace run --project dunecity --owner OWNER --tests --wait -- \
  ctest --test-dir /Users/stefan/Documents/projects/dunecity-feature-batch/build-int \
  -R GUI_REGEX --output-on-failure --no-tests=error -j1
```

Confirm that tree's source/configuration matches the recorded gate inputs;
native execution requires committed staging and the normal source freeze.
Queue wrapper exit 0 and a job ID mean **accepted**, not completed or passed.

### Bound stalls and preserve completed work

- Agents report `preparing`, `submitted`, `running`, `passed`, `failed` or
  `blocked`. Running requires a live operation/job identity and start time.
  After five minutes preparing without launch, report the exact missing input
  or guard; do not silently expand a wrapper or wait for redundant approval.
- After an admission timeout, first check for an accepted job to avoid duplicates.
  Preserve the failure and measure archive creation, extraction, unit discovery,
  cleanup and publication separately with existing diagnostics before retrying.
  Do not increase deadlines, weaken assertions or repeat an unchanged failure.
- After ten minutes with no new phase evidence, the coordinator investigates and
  names the owner and next action. This is an escalation interval, not permission
  to terminate a legitimate long test or cluster job. Distinguish a product
  failure from a harness failure, unavailable host and work never launched.
- Reuse completed passes when the required gate's inputs are unchanged: source
  dependency closure, harness, assets, configuration, toolchain and platform.
  Keep the actual tested SHA and record explicit equivalence evidence for reuse
  across commits. Docs-only changes do not restart game qualification. Harness
  fixes rerun affected gates; runtime changes invalidate affected consumers.
  Never relabel old results as tested at the new pin.
- Progress updates state published production version, completed gates, currently
  running operations and the single critical-path blocker. Preparation is not
  execution; CI success is not publication. No ETA without measured throughput.

### Test tiers and frequent staging

- `integration` is the bounded ordinary change gate; `nightly` retains broad
  regression coverage and collects every result without stop-on-first-failure;
  `release-smoke` checks the actual packaged candidate. Capability labels
  (`headless`, `gui`, platform and `packaged`) are independent of tier.
- Only tests with a real official-runtime input qualify as packaged smoke.
  A source-linked menu/gallery probe is not packaged evidence. The pointer
  regression remains required; a passing build or hidden-window launch is not
  a substitute. Keep GUI automation off Air.
- Reuse one sealed fixture per source/toolchain/runtime identity, not per test
  case. Cancellation/timeout terminates and reaps the complete owned process
  group. Preserve the failed case's concise evidence, not endless raw traces.
- Green integration batches produce immutable staging packages after the two-hour
  debounce, with a daily safety-net and full-SHA manual dispatch. Unchanged input
  does not create another package. Development stays `DUNECITY_RELEASE_BUILD=OFF`,
  prerelease and never Latest. Routine Mac packages use hosted ARM64, not Mini.
- Browser staging commits only tiny `deploy/staging-browser.json` metadata to
  the website repository; verified immutable archives download into temporary
  storage. Never commit WASM/data snapshots into its Git history. `/play-staging`
  isolates dev/SHA identity, caches, saves and signaling from stable `/play`.
- Stable headless-nightly qualification runs on hosted Mac at the exact source
  SHA before the Mini lane. Long QuantBot/Atreides/scenario/FFA checks stay there;
  Mini retains bounded native integration, calibrated GUI/performance/skin and
  packaged-pointer verification. Qualification metadata, plan and JUnit digests
  must agree before Latest.
- Freeze one candidate artifact set and required gates before production.
  Record scoped accepted known failures separately from genuinely blocking
  defects. Do not silently waive a gate or rebuild a successful artifact because
  another publication channel failed.
- Report GitHub, SourceForge, browser, website, TestFlight and Android separately.
  A channel is complete only after verifying its live destination/source/digest;
  resume from those facts rather than restarting the entire release. Missing
  Android signer/phone approval holds Android alone, not every other channel.

Staging and recovery use the existing automation, with `FULL_SHA` set to the
committed, qualified source and the workflows first deployed on default `main`:

```sh
gh workflow run build.yml --repo dunecity-project/dunecity --ref main -f staging_sha="$FULL_SHA"
python3 scripts/release-automation.py prune-staging --dry-run
gh workflow run build.yml --repo dunecity-project/dunecity --ref "$STAGING_TAG" \
  -f ios_staging=true -f staging_sha="$FULL_SHA"
```

The weekly iOS lane exports a signed **internal dev IPA** with unique build
number and source provenance. It cannot upload, install or claim device coverage.
Its Mini admission is denied during an active production reservation.

Publication/request/pruning use GitHub's shared staging concurrency group with
`queue: max` and no in-progress cancellation; coalescing belongs before expensive
builds, not after frozen artifacts exist. Retention keeps the newest seven
successful snapshots, known-good and investigation holds, and admitted consumers.
After actual qualification, the existing `latest-dev` body records
`<!-- dunecity-staging-tag: STAGING_TAG -->`; an individual staging release's
`<!-- dunecity-retain: reason -->` pins an investigation. Missing/ambiguous state
holds cleanup. Retired inputs fail explicitly, never silently rebuild.


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

## Platform coverage without personal-machine dependencies

Use reachable existing Linux workers and hosted native CI under the current
workspace policy. Pro/PC availability is not a prerequisite; Air's running game
is not a test resource. Classify the actual CTest registry into complete, disjoint
headless and graphical sets. Only positively headless cases use the pinned queue;
graphical work uses the coordinated non-Air native lane. A regex does not select
a host, and queue acceptance is not completion.

Hosted Windows/Mac jobs provide their actual platform coverage; Linux does not
stand in for Windows or Cocoa. Mini owns required signing and packaged native
GUI/iOS phases, serialized. Browser compilation uses pinned Emscripten in hosted
CI rather than consuming Mini by default. Android still requires the correct
signer, phone validation and explicit publication approval. Name the execution
environment and every untested surface in the report.

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
python3 scripts/release-automation.py resume build --run-id "$PACKAGE_RUN_ID"
python3 scripts/release-automation.py resume browser --tag "$TAG"
python3 scripts/release-automation.py resume sourceforge --tag "$TAG"
python3 scripts/release-automation.py resume deploy --run-id "$WEBSITE_RUN_ID"
python3 scripts/release-automation.py verify "$TAG" --live
```

Resume uses frozen successful inputs and reconciles actual channel state; it
does not replace published bytes or rerun completed platform builds merely
because a later upload failed. A draft/incomplete staging set requires explicit
resume. Investigate missing secrets, host verification, conflicting tags and
non-fast-forward refs instead of retrying the entire pipeline. Historical
SourceForge backfills never advance current download defaults or source branches.
Documentation-only pushes may use `[skip ci]`; game changes may not skip their
required CI.

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

Require the native mouse/menu regression on the Mac mini against the candidate's
exact SDL implementation. Native motion/down/up must cross SDL's live Cocoa
application dispatcher and the real menu hit-test. Keep the existing off-centre,
edge/outside, drawable/window scaling, letterbox, render-target restoration,
fullscreen/windowed and settings-reinitialization assertions unchanged. Log actual
window/drawable/logical/viewport dimensions, loaded images and `SDL_GetVersion`'s
resolved Mach-O image. An unavailable sample, skipped opt-in, hidden-window pass
or SDL-boundary-only run is **not** a passing full release gate.

The fixture is **not the actual packaged executable**. Its own-window NSEvents
execute inside its process; they are not external actual-app input evidence.
Observe the actual candidate separately. No new Accessibility/global event-posting
permission, physical-pointer automation or external event framework is required
or authorized. Never run graphical checks on the Air or change the user's app.

### Static SDK: normal producer builds, Mini only consumes

The normal `build-macos` producer uses its existing cached `build`/compiler/vcpkg
context, not a second SDK or build tree. Before the candidate build it freezes
source, compiler/toolchain/platform SDK settings, Ninja link recipe and installed
static SDL hashes. After packaging it compiles the existing full pointer fixture
through the same frontend. Export refuses drift from that pre-build snapshot.
No game objects/behaviour are instrumented in the candidate.

`DuneCity-macOS-pointer-fixture-FULL_SOURCE_SHA-RUN_ATTEMPT` is a **separate standard
Actions diagnostic artifact**, retained for 90 days. Its single
`pointer-fixture.tar.gz` contains only the compiled fixture and `receipt.json`;
no SDK, resources, test payload or receipt enters the app/public runtime assets.
The receipt binds source commit/content fingerprint, producer repository/run/
attempt/workflow commit, checked-out workflow/frontend hashes, configuration and
link inputs, final package ZIP, every candidate bundle file (including Mach-O),
fixture bytes and the actual compile/link commands.

Static SDL identity is not inferred from its filename/version: each archive's
SHA256 must match its installed vcpkg SPDX binary-file hash; the SPDX binary ABI
must hash the installed ABI-info bytes, whose port recipe/patch hashes must agree.
Require pinned port Git objects, hashed upstream resources, the source-controlled
baseline/overrides and the SDL header hash/version. Missing/uncertain archive
provenance blocks export/consumption; there is no local/Homebrew SDK fallback.
At runtime only a receipt-verified fixture may resolve `SDL_GetVersion` to itself,
and **no dynamic SDL image** may be loaded. All inherited `DYLD_*` and pointer-proof
environment variables are stripped before installing the verified environment.

The trust root is an independently pinned, frozen `publication-inputs.json`.
Its existing `artifacts` metadata records the producing run's diagnostic ID,
name, raw Actions ZIP SHA256 and size without adding diagnostics to release
assets. Root must review the changed workflow hash and update the protected Mini
admission gate **before trusting a producer using this contract**.

After the active frozen producer settles, combine source changes into the normal
shared next batch. Do not cancel/rebuild it, force an old tag, mutate old manifests
or replace stable assets to manufacture a diagnostic. The observed 31d5b6f30
package (asset 741573212, static SDL 2.32.10) remains blocked for this coverage:
the build-int/Homebrew SDL 2.32.72 fixture correctly failed the packaged-library
guard (`outputs/staging-native-31d5b6f30/pointer/run-1440.log`, line 448).
Preserve that failure; a named old snapshot without its diagnostic cannot PASS.

For a future produced snapshot, obtain `CANDIDATE_SOURCE`, `REPOSITORY`,
`DIAGNOSTIC_ID` and the manifest hash from the trusted frozen publication record.
Keep the actual extracted package in `CANDIDATE_APP`, the frozen manifest in
`PUBLICATION_MANIFEST`, its **independently trusted** hash in
`PUBLICATION_MANIFEST_SHA256`, and use dedicated evidence/download paths:

```sh
gh api "repos/$REPOSITORY/actions/artifacts/$DIAGNOSTIC_ID/zip" > "$POINTER_ARTIFACT"
agent-workspace run --project dunecity --owner "$OWNER" --tests --wait -- \
  env DUNECITY_NATIVE_POINTER_TEST=1 python3 tests/menu/run-menu-probe.py \
  --pointer-mapping --pointer-artifact "$POINTER_ARTIFACT" \
  --publication-manifest "$PUBLICATION_MANIFEST" \
  --publication-manifest-sha256 "$PUBLICATION_MANIFEST_SHA256" \
  --candidate-source "$CANDIDATE_SOURCE" \
  --runtime-app "$CANDIDATE_APP" --output-dir "$EVIDENCE_DIR"
```

Download the **raw Actions ZIP** (not `gh run download`'s unpacked contents) so its
digest remains verifiable. The consumer has no configure/compile/build-dependency
path: authenticate first, extract only bounded regular files into existing leased,
private owned scratch using the shared safe-extraction helper, then verify all
receipt/candidate/source bytes before execution. It reuses candidate resources
read-only and restores foreground ownership. Receipt/reference, profiles and logs
remain in evidence; successful leased scratch is removed by the existing helper.
`gate-status.json` distinguishes `blocked-prerequisite`, `blocked-environment`,
`failed-assertion`, other harness/runtime failures and an actual full-menu PASS.
Compilation-only `prepared-not-run` and `--pointer-sdl-only` never satisfy it.

The staging-retention helper protects newest-seven/known-good/active-consumer
snapshots and explicit `dunecity-retain` holds, **not** Actions artifact expiry.
While a consumer/release owns this evidence, retain the downloaded, digest-verified
raw diagnostic ZIP with its pinned manifest under the existing artifact hold.
An expired/missing/unpinned diagnostic is blocked coverage, never a reason to
rebuild an unchanged published package or silently select a newer fixture.

Narrow source-contract check (no SDK or GUI):

```sh
PYTHONPATH=tests/menu python3 -m unittest test_probe_process.PointerArtifactTests
```

### Dynamic packaged SDL

The existing strict packaged-path guard remains: every loaded SDL image and
`SDL_GetVersion`'s symbol must resolve inside the selected app. The registered
CTest `DUNECITY_POINTER_RUNTIME_APP:PATH` still selects the dependency-fixed,
installed app; its default target bundle does not make a Homebrew build an
official package. This local-build CTest route cannot qualify a static package.
Validate SDL's live `sendEvent:` implementation for every native event rather
than pinning an earlier Objective-C class; AppKit may add a KVO subclass during
focus/renderer transitions. Never weaken dispatcher, geometry or coverage checks.

## Windows compile caching


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
