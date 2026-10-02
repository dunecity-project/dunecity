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
   Watch **Deploy to Droplet** there and check the live pages. Release prose is
   not guaranteed to be rewritten by version replacement; review it explicitly.
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

For an authorized browser hotfix, build current committed source locally:

```sh
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release -DDUNECITY_BUILD_TESTS=OFF -DDUNECITY_ENABLE_PCH=OFF
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
Released 1.0.661 crossplay uses `https://dunelegacy.com/relay`. Desktop ENet multiplayer remains
available; browser/native crossplay supports WebSocket and HTTPS polling transports,
the public directory, optional private invitations and confirmed-name lobby chat.
Package production clients with `--relay-origin https://dunelegacy.com` so their CSP
permits the exact HTTPS/WSS origin. Local development packages explicitly opt into
loopback and must never be copied to production.

Before publishing a crossplay client, verify the relay and signed PHP analytics
receiver. See `tools/room-relay/deploy/README.md` for both deployment paths and the
rollback contract. The administrator path installs a service and WebSocket proxy.
The restricted-account path uses existing Apache/PHP with a fixed-route HTTPS
polling gateway and a supervised, sandboxed loopback relay. It needs neither a new
server nor administrator access; it does not install Apache proxy modules.

A `release-*` branch builds candidate packages without publishing a stable release
or replacing the browser site. Hold stable publication until actual public
browser/browser and browser/native gameplay, bounded gateway behavior, signed
SQLite delivery, artifact checks and watchdog recovery have passed. Successful
localhost tests or desktop CI alone do not establish public gameplay readiness.

### Gateway persistence during website deployments

The website deploy uses rsync --delete and runs after scheduled download-stat updates
as well as release pushes. Relay PHP/.htaccess and analytics receiver files must be
committed to website main before relying on them in a public test or release. A manual
webroot upload alone is temporary: the 13 September 2026 test lost its gateway to the
next scheduled deploy. Website main 9a85d48 contains the gateway and additive receiver.
Untracked play-test-* previews are also removed; restore previews after deployment,
or finish their tests before publishing. The private loopback relay and SQLite data
live outside the deployed webroot and are preserved.

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
