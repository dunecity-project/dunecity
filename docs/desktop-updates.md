# Desktop updates

First publicly released in 1.0.732 after local 1.0.731 testing. Users install this
edition once; later stable
releases can be installed from the main menu. Nothing is published by a local
build. Browser and Android builds do not include the desktop updater.

## User flow

The main menu checks once per process in the background. From 1.0.769, a newer
signed release automatically opens the **Install update / Later** confirmation
once per launch, waiting until any existing menu dialog closes. **Later** dismisses
the automatic prompt for the rest of that launch, including after returning from
a match. The footer remains **Update available** and can reopen the confirmation.
Installation still requires choosing **Install update**. Manual checks report
errors and whether the game is current. Installation disables game-entry actions,
including keyboard activation. No update is installed during a match.

- macOS: Sparkle 2.10.0 installs the signed/notarized app and handles relaunch.
- Windows x64: WinSparkle 0.9.4 runs the EXE installer. Its registry key stays
  `DuneCity` across versions so upgrades retain the installation directory.
  Portable ZIP users install the EXE edition once to enable in-game installation.
- Linux: user-owned AppImages download into a private temporary sibling file,
  verify the expected size/hash and AppImage header, retain a uniquely named
  `.previous-*` backup, then atomically replace the original. Normal game shutdown
  completes before relaunch. A failed process launch restores the backup. A game
  that launches and subsequently crashes is not automatically rolled back.
- DEB/RPM and other Linux layouts display guidance for manual package updates.
  No package repositories or package-manager auto-updates are configured here.

Updates replace application files, not the separate user profile containing
saves, settings and user mods. AppImage backups are retained for manual recovery.
The Linux update feed currently publishes x86_64 packages; arm64 has a distinct
channel identifier and will not receive an x86_64 update. Mac Intel likewise has
a separate identifier; the current release workflow builds Apple silicon only.

## Trust and channels

`cmake/update-public-key.txt` is the public Ed25519 trust anchor embedded in the
game. The encrypted private key and its password are **not in Git**. They live
under the signing account's protected
`~/Library/Application Support/DuneCity Signing/updates/` directory on the mini.
Back up both protected files securely; changing the public key requires an
explicit migration for existing installations.

The default feed base is
`https://github.com/VR48/dunecity/releases/latest/download`. Each platform reads
`updates-PLATFORM.txt`. The seven-line UTF-8 format is:

```text
DuneCityUpdate1
X.Y.Z
platform-id
https://versioned-host/path/to/archive
archive-byte-count
lowercase-sha256
base64-ed25519-signature
```

Each line, including the last, ends in LF. The signature covers the first six
lines including their terminating LF. Parsing is bounded to 4096 bytes, versions
have three numeric components, and update downloads are bounded to 512 MiB.
Only HTTPS redirects are permitted. Bad certificates, signatures, platforms,
sizes and hashes fail closed. Releases at or below the running version do not
install. This does not promise availability against a compromised update host
that withholds newer releases.

Sparkle and WinSparkle additionally verify Ed25519 signatures on their update
archives through their appcasts. Mac app signatures/notarization are separate
from these update signatures. This work does not provision Windows Authenticode.
`DUNECITY_UPDATE_FEED_BASE` is a build-time HTTPS channel setting; there is no
runtime environment override in the shipped game. Test/development releases do
not publish stable feeds. `DUNECITY_ENABLE_UPDATER=OFF` excludes the dependencies.

## Packaging and release

Normal local builds remain ad-hoc signed on macOS. `cmake --install` first bundles
portable dependencies. It preserves the complete Sparkle framework, including
the updater app/XPC services and framework symlinks, before final signing.
From 1.0.732, packaging detects SDL2 compatibility builds and explicitly includes
their dynamically loaded SDL3 library and license. Static linker dependency
inspection alone does not find this dependency. Mac package verification launches
the app's isolated `--check-desktop-runtime` mode, checks hidden-window rendering,
and confirms that every loaded SDL library comes from the app bundle. The same
check runs on the signed app after Apple's ticket is stapled and validated, and
before the final ZIP and DMG are made; if it fails, neither is made. It used to
run before submission. Launching code that Apple has already notarized (a rerun
of the same source) can put the bundle under macOS App Management: in the third
1.0.868 attempt the runner's `stapler` was then refused the write of
`Contents/CodeResources` (exit 73). Nothing writes into the bundle after the
launch now. The new order is confirmed by test for sequence only; its first
runner validation is the 1.0.869 release.

`scripts/package-signed-macos.py` copies an installed bundle, signs all nested
Mach-O files and bundles with hardened runtime/timestamps, notarizes and staples
the app, then emits its update ZIP and a separately signed/notarized/stapled DMG.
It requires the local `DuneCityNotarization` Keychain profile and never receives
an Apple password as an argument. Use a fresh output directory for each attempt.

The mini's signing keychain is
`~/Library/Keychains/DuneCity-Signing.keychain-db`. The password remains in the
protected signing directory. `scripts/unlock-macos-signing.py` unlocks it without
printing the password. Provision Apple credentials interactively with
`notarytool store-credentials`; a login-keychain profile on the Air is not enough
for unattended builds on the mini.

`scripts/prepare-desktop-updates.py` requires Python `cryptography` and produces
the signed platform manifest and (Mac/Windows) appcast from a final immutable
archive. It checks that the signing key matches the compiled public key. Supply
a **versioned** HTTPS archive base URL. It does not upload anything.

Stable tag builds create Windows EXE + ZIP, Linux packages, and notarized Mac
DMG + ZIP. A dependent job on the mini signs all platform feeds using the local
update key. The release job uploads binaries and feeds to a draft and publishes
only after all uploads succeed. Already published releases are not overwritten.
SourceForge keeps the portable Windows ZIP and, from 1.0.731, adds the EXE as its
Windows default. Older release backfills retain their original asset sets.

No key upload to GitHub secrets is required by this design. The runner account
must have the locally provisioned signing files and notarization profile.
Signing jobs run only for stable tags, never PRs. Publication still requires the
owner-authorized release process in [release operations](release-operations.md).

## Verification

- `ctest --test-dir build --output-on-failure` includes signature/parser/atomic
  replacement tests and the rendered main-menu navigation/confirmation probe.
- `python3 -m unittest discover -s scripts/tests -p test_desktop_updates.py`
  verifies manifest and native archive signatures and rejects invalid inputs.
- `python3 tests/desktop-updater/run-appimage-flow.py --output /tmp/update-flow`
  downloads from a temporary HTTPS server using a disposable key and profile.
  It checks success, corruption, truncation, signature tampering, downgrade,
  platform mismatch and untrusted certificates. On macOS this exercises the
  production POSIX AppImage path, not Linux AppImage runtime launching.
- Before the first public release, perform an actual old-to-new upgrade on each
  supported OS. Verify custom install directories, network interruption, **Later**,
  restart, user data, and the same build joining a multiplayer match.

The first release carrying the updater cannot update earlier installations that
only know how to open a download page.
