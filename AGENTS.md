# AGENTS.md — DuneCity

## Shared staging and multiple agents

Read `/Users/stefan/.config/agent-workspace/POLICY.md` on managed Macs and
`docs/shared-agent-staging.md` before editing, building or preparing a release.
This applies to every Codex, Claude Code and OMP session and subagent, including
native OMP task/eval agents. Coordinate disjoint path ownership; hand off shared
files and Git operations to the integrator. Batch accepted changes into one
tested release rather than creating a full build environment per request.

On claw.local the shared native Release lane is this checkout's **build-int**.
Use `agent-workspace stage --project dunecity --owner FRAMEWORK:SESSION:AGENT verify`
in Tern after committing the batch and releasing all edit claims. Its mutex
covers build/test/Git operations across frameworks. The copied `build` tree
points to `dunecity-quantbot-split` and must not be reused here. The older generic
`build` instructions below describe configuration mechanics, not the current
registered staging lane. Keep separate trees for genuinely different toolchains
or sanitizer configurations. Do not copy or relocate configured CMake trees.

### Disk hygiene (worktrees, builds, game scratch)

- One configured build tree per active worktree. When your branch is merged into
  `integrate/feature-batch` or `origin/main`, delete that worktree's `build*/` and,
  if it is clean, the worktree. `scripts/prune-merged-worktrees.sh` (dry run by
  default) lists merged, clean, idle worktrees with their build sizes; `--apply`
  deletes their build trees, `--remove-worktree` also removes those worktrees
  (branches are kept). Never apply it to a worktree another agent is using.
- Game batches (formation-duel, formation-wave-matrix, headless-bench,
  run-all-against-atreides) run `agent-scratch guard` first (refuse below 25 GB free)
  and, unless you pass `--scratch`, run in leased scratch under
  `/private/tmp/dunecity-work`, all through `tests/performance/probe_profile.py`.
  Each game's directory is deleted as soon as its rows are written; a failed game
  keeps its logs, saves and replay (not mods, workshop revisions or the decision log)
  in `<scratch>/failed`, at most 5 per batch, and the batch is kept 72 h as failed.
  Probe profiles drop their regenerated mods/revisions when a run ends, and copy only
  the workshop revisions a save or `ws-*` mod pins, never the whole store. Keep results
  in `--results`, never in scratch. Ad-hoc scratch: `agent-scratch new --project
  dunecity --purpose X` and `agent-scratch finish PATH` (or `--failed`). The hourly
  sweep removes dead owners' scratch and idle `/private/tmp` agent scratch
  (`agent-scratch sweep --dry-run`).

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

## AI and unit behaviour: mandatory regression matches

Every change that can alter AI decisions or unit behaviour (anything under `src/players/`,
`include/players/`, unit/structure/combat/pathfinding code, `QuantBot Config` defaults, game
option semantics) must pass `quantbot_all_against_atreides` and `quantbot_scenario_matrix`
before it is merged:

```bash
ctest --test-dir build -R 'quantbot_all_against_atreides|quantbot_scenario_matrix' --output-on-failure
```

`quantbot_all_against_atreides` plays Stefan's setup to the end on six seeds - vanilla,
`5P - 128x128 - All against Atreides`, Atreides QuantBot Brutal (spectated) against four AI Player
(hard) houses, unit and harvester overrides 0 - and fails unless Atreides eliminates everyone within
40 game minutes on every seed (about 3.5 minutes wall, three games at a time). Do not raise the limit
or drop a seed to make a change pass; see `docs/quantbot-all-against-atreides.md`.

`quantbot_scenario_matrix` is the smoke tier of the AI scenario matrix (`docs/ai-scenario-matrix.md`):
one fast game per kind - a 4-house Random Skirmish free-for-all, a Construction Yard only duel
(replayed to the same final digest), the reverse All against Atreides (Medium against four Hard must
lose), the Hard v Easy ladder on both sides, a 2v2 on Sihaya-Toto and campaign missions of levels 1, 4
and 9 the Hard partner must win - and fails on a crash, a missed expected winner, a time-out, nobody
eliminated or idle armies (a few minutes wall). Changes with a wide reach (unit mix, attack timing,
economy, campaign AI) also run the full tier on the runners and compare its table with the last one.

## AI test games: scoring and the default rule

AI test games (`tests/ai/formation-duel.py`, `vanilla-match.py`, `play-style-matrix.py` reports) are
scored by `tests/ai/match_score.py`. The score is placement: 1st +1, last -1, linear in between. A
time-out is a draw and draws are negative: survivors share the top places minus 0.25, so a 1v1 draw is
-0.25. There is no kill-value adjudication. A candidate behaviour becomes a default only when the 95%
bootstrap interval of its paired score difference against the control is above 0, and it is not worse
on Fortress, Brutal and `/weak`. Report the time-out rate of each arm. Do not reintroduce the old
1/0/0.5 rule except through `--scoring legacy` to compare old reports. See
`docs/ai-play-styles.md` section 7.

## Release and hosting handover

Before release, website or SourceForge work, read [docs/release-operations.md](docs/release-operations.md)
and [docs/sourceforge-releases.md](docs/sourceforge-releases.md). They map the three repositories,
automated versus manual publishing, verification and recovery commands. SourceForge Files
contains binaries, notes and checksums only; link to tagged Git source, never upload a source archive.
Keep these runbooks current when changing deployment behaviour. Credentials live in GitHub
Actions secrets/local SSH storage, never in these documents. Verify current service state;
historical HANDOVER entries are dated evidence, not claims that a deployment is still pending.
