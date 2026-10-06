# Candidate 1.0.813 verification

Verified 2026-10-06. This is candidate evidence, not a stable-release approval.

## Frozen game source

- Code: `6ec781f3d9cde2887487b2057f2308818a1c01f9`.
- Upstream PR #88: `e5797cd3ef3e14ad95d7ebff96a2dd0978936364` (1.0.810).
- Asset-only commit: `e4285d2d1e8f4e633b724a9f1040705a25313c5a`.
- Game version: 1.0.813. Android version: 0.2.29 / 1000549, arm64-v8a.
- Draft integration PR: [#89](https://github.com/dunecity-project/dunecity/pull/89).
- [Push CI](https://github.com/dunecity-project/dunecity/actions/runs/37518651928)
  and [PR CI](https://github.com/dunecity-project/dunecity/actions/runs/37518659527)
  succeeded at that exact code commit. Windows, Linux, macOS and Emscripten
  candidates were built; native, relay and signaling checks passed. PR CI
  deliberately skips duplicate macOS work. Stable signing/publication jobs skipped.

## Local checks

All nine Windows CTest targets passed. The primary native suite passed 960 of
962 cases, with two explicit skips (symlink privilege and opt-in live download),
and 10,219,271 assertions. The focused Dune2R presentation/catalog suite then
passed all 21 cases / 370 assertions with the live GitHub download test enabled.
That network test covers resumable downloading and verification of an existing
public manifest; it is not a full download/play-test of the new Infantry pack.

Real Direct3D Refinery playback drew 234/240 frames including first-loop decode
warm-up, with zero second-loop misses, a worst request of 12.1099 ms and
155.039 MiB resident cache. This is renderer evidence, not a complete game match.
The independent source/pixel audit verified all 386 approved Infantry poses.

The arm64 Android native build passed pre/post dependency audits. APK packaging
completed successfully; APK v1/v2 debug signatures verify. Its packaged native
libraries match the pre-stripped staged libraries, and the native library embeds
1.0.813. Compiled bootstrap markers identify the current payload and skin
fingerprint `D7CAF796B1349228`. No ADB device was present for installation.

## Packaged content and hashes

Android contains all 849 DuneCity skin files byte-identically. Windows contains
all 784 PNGs byte-identically and all 65 INI manifests with only LF/CRLF differences
from the local checkout. The browser preload verifies all 849 skin files, five
required City maps against committed source, 769 Tornie files and the current
six-pack Dune2R catalog. Windows, Android and browser base payloads contain no
optional Dune2R unit media; those assets remain an on-demand catalog download.

| Candidate file | SHA-256 |
| --- | --- |
| Windows x64 installer | `4aa0f08a33ead323ee588ef2a9ff83c0abb5cc8046cbf983472c3ef4d5c2aa71` |
| Windows x64 portable ZIP | `41aa2f3a235d21172a50b502cfe9f8727ed16ca3e7d16698414489e5f7decbb2` |
| Android debug APK | `f066a167e6aad6a03158cd07eb932a8129d5d4a6e518bf807946b9ecf02068c2` |
| Linux amd64 DEB | `3ddbc0b9f45cbb614e768344a4d0ebf461123a3681c7c6341e7aa16fe36023e8` |
| Linux x64 RPM | `f5726f1705b0f8c55d69430a0a9856c30685ae0df6909f3b935c916f7b7b97e5` |
| Linux x64 tar.gz | `6adee9b1dc51f5029b07af81b06d39dad003d5f4b25bc5e6f787e1b33c6d5ce5` |
| Linux x86_64 AppImage | `c152c3edaa018d8c9d5ab1f6b8d462b7b3254676059ea12af81e03967f0c9dd3` |
| macOS candidate DMG | `f03cb4966258a2a3179003f98bcd45be0ffce8fe817e5bef7b39509f3cd00b23` |
| Browser WASM | `49e3a92d7d13887d0891882040f51d28df8916f805237730cde79678ce5b1269` |

The macOS candidate is unsigned/not notarized. Android is a debug test package,
not a claim of production signing or an already published release.

## Remaining release gate

Main remains `5835966ad1333ac2a1676a8b19ea575e9958713f`, latest stable v1.0.796.
PR #88 still documents the Four Corners browser observer-to-player promotion
heap/free/Map teardown trap, seed 315473198. This candidate does not repair that
path. Passing CI's WebRTC peer-lifecycle ASAN fixture does not cover Game/Map/Tile
teardown. The original release-809 crash/checkpoint archive is not available on
this host; the tracked promotion runner is Mac-specific and requires browser
interaction. Obtain the original evidence and replay the matching promotion
before concluding the defect is resolved. Live mixed-graphics and public
browser/native play-tests remain required.

Keep PR #89 draft. Do not merge #88/#89, tag stable, advance signed feeds, publish
SourceForge defaults, or deploy desktop/browser website updates yet. The website
was not changed. For candidate Dune2R testing, use an isolated profile: bundled
catalog has six packs, but remote Refresh still reads current main's five-pack
catalog until integration. Download in the mutable Dune2R working mod before
creating an immutable online snapshot; snapshot asset menus are read-only.
