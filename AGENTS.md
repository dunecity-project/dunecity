# AGENTS.md — DuneCity

## Read first

1. **`HANDOVER.md`** — state of the current, uncommitted work: what was fixed, what is still
   open, and the diagnostics already built into `build/bin/dunecity.app`. Start here.
2. `CLAUDE.md` — the standing architecture constraints (2x2 zone footprint, roads as tile flags,
   global power, versioning rules). Its `~/development/dunecity` and `/Users/stefanclaw/...`
   paths refer to a different machine; on this Mac the checkout is `~/Documents/projects/dunecity`.
3. `ARCHITECTURE.md` and `docs/dunecity-current-architecture.md` for deeper background.

## Build on this Mac (macOS, Apple silicon, Homebrew — no vcpkg, no Xcode)

```bash
brew install cmake ninja sdl2_mixer sdl2_ttf miniupnpc catch2
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/opt/homebrew -DDUNECITY_BUILD_TESTS=ON
cmake --build build --parallel 10
python3 scripts/check-build-deps.py build
ctest --test-dir build --output-on-failure
```

`-DCMAKE_PREFIX_PATH=/opt/homebrew` is required or configure fails on miniupnpc.
Run tests through `ctest`, never `./build/bin/dunelegacy_tests` directly — ctest supplies
`DUNE_CITY_SOURCE_DIR` and `DUNECITY_DATADIR`, without which ~50 tests silently misbehave.
Legacy `build2/`, `build_phase4/`, `build.bad/`, and `buildtests/` artifacts were
removed from tracking; keep generated build trees out of git. On this Mac,
`build` currently links to `build-714`, whose cache now holds native 1.0.718.
Do not relocate a configured CMake tree or a running app; their paths matter.

Run `python3 scripts/check-build-deps.py build` before and after incremental builds.
If it fails, use `cmake --build build --clean-first --parallel 10` and check again.
Version 1.0.588 crashed because six existing objects had empty Ninja dependency
records: an old inline sidebar reader accessed the new CitySimulation layout at
obsolete offsets. A successful incremental link alone did not detect this.

Baseline test result: 362 passed, 2 failed, 3 skipped. The two failures are pre-existing
(`parseDouble` accepts `"nan"`); see `HANDOVER.md` §1.

Sanitizers: a native Debug build with GCC or Clang compiles with AddressSanitizer and
UndefinedBehaviorSanitizer by default (`-DDUNECITY_SANITIZE=OFF` turns them off; any other
value is the `-fsanitize=` list). Release builds, which is everything that ships, never get
them. Every report aborts the process, so a ctest run fails at the first one. CI's
`test-linux` job runs the unit tests and network harnesses this way. The cache keeps the
value from the first configure, so reuse a Debug tree only for Debug. On macOS the tests run
with `detect_container_overflow=0`, because Homebrew's Catch2 is not instrumented and libc++
then reports false overflows inside it; set the same `ASAN_OPTIONS` when running a sanitized
binary outside ctest.

## Runtime paths

- Config: `~/Library/Application Support/Dune City/Dune City.ini`
- Log: `~/Library/Application Support/Dune City/Dune City.log` (truncated on every launch)
- Mod overrides: `~/Library/Application Support/Dune City/mods/<mod>/`

## Working agreement

- Do not push or open a PR without being asked.
- Any release build, tag, or CI-triggering push must include the version bump in the same commit
  (`scripts/bump-version.sh`); CI verifies the tag against the source metadata.
- **Every code change you make must end up in git.** When you finish, `git status` should show no
  untracked `.h`/`.cpp`/`.py` files and no unstaged source edits. Leaving new headers and test
  cases untracked means a release built from a tree nobody else can reproduce — 1.0.599 was
  carried as 88 modified plus 23 untracked source files before it was captured. Staging and
  committing as you go is what keeps `HANDOVER.md` and the tree describing the same thing.
- **Do not leave analysis markdown in the repo root.** Per-version review notes (`AI-*.md`,
  `*-REVIEW.md`, `*-ANALYSIS.md`) are session scratch and are gitignored. Fold whatever outlives
  the session into `HANDOVER.md`; if a note is genuinely reference material, put it under `docs/`
  with a lowercase name so it is tracked deliberately.

## Release and hosting handover

Before release, website or SourceForge work, read [docs/release-operations.md](docs/release-operations.md)
and [docs/sourceforge-releases.md](docs/sourceforge-releases.md). They map the three repositories,
automated versus manual publishing, verification and recovery commands. SourceForge Files
contains binaries, notes and checksums only; link to tagged Git source, never upload a source archive.
Keep these runbooks current when changing deployment behaviour. Credentials live in GitHub
Actions secrets/local SSH storage, never in these documents. Verify current service state;
historical HANDOVER entries are dated evidence, not claims that a deployment is still pending.
