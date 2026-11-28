# AGENTS.md — DuneCity

## Shared staging and multiple agents

Read `/Users/stefan/.config/agent-workspace/POLICY.md` on managed Macs and
`docs/shared-agent-staging.md` before editing, building or preparing a release.
This applies to every Codex, Claude Code and OMP session and subagent, including
native OMP task/eval agents. Coordinate disjoint path ownership; hand off shared
files and Git operations to the integrator. The main agent only reports to Stefan
and dispatches instructions; integrator and worker subagents do implementation,
integration, builds, deployment and tests. Contributors hand green targeted-test
commits to the integrator. Push bounded-green batches; exact-SHA AWS qualification
runs extensive tests once after push and gates qualified packages/releases.

On claw.local the shared native Release lane is this checkout's **build-int**.
The always-on Air autodeployer owns
`agent-workspace stage --project dunecity --owner FRAMEWORK:SESSION:AGENT --wait build`
after the integrator lands targeted-test-green commits and releases all edit claims.
Until it is confirmed live, the integrator owns manual build/delivery. The mutex
protects mutable staging build/test/Git inputs; Air copying and swapping take no
project mutex. The copied `build` tree
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

1. **The coordinator's status record** (`/Users/stefan/.omp/workspaces/dunecity/STATUS.md` on claw.local): what is
   in flight, who owns it, its test commit and job, and what is on Stefan's Air. Per-feature write-ups are in
   `docs/handover/`. `HANDOVER.md` is frozen history through 2026-11-28; read it for older decisions, do not
   append to it.
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
Run tests through `ctest`, not `./build/bin/dunelegacy_tests` directly: ctest sets
`DUNE_CITY_SOURCE_DIR`, which the tests that read shipped files (maps, `config/`, mods) need.
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
  committing as you go is what keeps the write-up and the tree describing the same thing.
- **Do not leave analysis markdown in the repo root.** Per-version review notes (`AI-*.md`,
  `*-REVIEW.md`, `*-ANALYSIS.md`) are session scratch and are gitignored. Fold whatever outlives
  the session into the feature's `docs/handover/YYYY-MM-DD-<slug>.md`; if a note is genuinely reference material,
  put it under `docs/` with a lowercase name so it is tracked deliberately.
- **Playtest-first landing replaces test-before-landing.** Stefan, 2026-11-28: "the things i ask for a built
  on mba BEFORE all the extensive testing etc. so i can play test"; "key thing is to increse the human
  feedback loop from me". Contributors compile and pass their own targeted tests at an immutable commit,
  then hand its SHA, job/results and green timestamp to the integrator. The integrator cherry-picks through
  `agent-workspace run --git`; the autodeployer incrementally builds and immediately delivers each
  successful build to the Air, never holding it for another feature or extensive testing.
  Before pushing, require owners' targeted greens, bounded `-L integration -LE gui` on the combined
  source and applicable Mini `perf_regression`. Broad nightly `-L nightly -LE gui` and four AI gates
  run once after push on that exact SHA on AWS; reds block qualification/packages/releases.
  Preserve failures, fix forward (or locally revert game-breaking changes), and immediately
  rebuild/redeploy each accepted fix. Dormant experimental failures remain explicitly unqualified.
- **Dev builds on Stefan's Air come before full testing.** The always-on service watches committed HEAD,
  builds only the newest source, and delivers every successful `build-int` build immediately.
  Provenance is `build-int/air-build.json`, never a later HEAD. While Stefan plays, stage the newest
  app once and swap after he exits; never stop his game. Until the service is confirmed live,
  the integrator runs `scripts/air-dev-install.sh --no-build --wait` asynchronously with exactly one
  waiter, replacing it for a newer built app rather than adding a second. After handoff, only the
  service builds/deploys. New features are enabled in the app's own Dune City rules, never a separate
  mod (Stefan: "i just want it as a feature, not a mod"); only independent switches belong in `DEV_SWITCHES`.
  The dev copy enables `GarrisonHold` and `AirliftColonists`; their shipped defaults remain OFF.
  Actual MCV flight requires **Manual Carryall Drops** ON in **Game Rules**; OFF keeps the ground mission.
  Report source SHA, new things to try, untested scope and installed versus staged/waiting state.
  Record green-to-install and landing-to-install in `STATUS.md` (target under five minutes, with
  user-game wait identified). Service receipts are in `~/.local/state/dunecity-air-autodeploy/`.
  This dev build qualifies nothing.

## Player features: real assertion tests through the player's own path

Stefan, 2026-11-27, after trying Zone Garrisons, Hospital Care and capture in a game: "nothing to do with
infantry actually works. you need to make sure you include REAL assert tests to make sure the things you add
actually work. please ensure this is written into your guidelines".

A gameplay feature is not done, and must not be reported as working, until all of these hold:

1. **An assertion test that drives the player's own path.** Select the units, press the actual command button
   or hotkey (the sidebar/unit-panel widget, not a hand-built `Command` or a direct method call), click the
   map target through the same click handler a mouse click uses (`Game::handleSelectedObjects*Click`, cursor
   mode, `CursorManager`), run real game cycles, and `assert` the observable result: owner changed, unit inside
   the zone, health went up, the unit did *not* go to a Repair Yard, the button is visible and enabled. A probe
   that calls the internal function the button would call proves the function, not the feature.
2. **Every user-visible part has its own assert:** the button or hotkey exists in the state the player is in
   (city game, the unit type selected, the rule on), its tooltip, the cursor accepting the target, the command
   reaching the unit, the result, and the undo/exit path (Evacuate, cancel). Include the negative cases the
   player will try (own building, enemy building, full, no Hospital).
3. **Asserts fail the test.** A probe that prints counts or writes a CSV is evidence, not a test. The CTest
   registration must exit non-zero when any assertion fails, and must run headless (dummy driver) on AWS so it is
   in the integration or nightly tier, never as a GUI run on the Air.
4. **Proven in the build the player will run.** Before telling Stefan to try something, check that the app he
   is running contains the commit (`CFBundleShortVersionString`, the staging tag or `build.json` source SHA). If it
   does not, say so first; never let him test a feature in a build that does not have it.
5. **Report what was not exercised.** If a path could only be checked below the UI (for example a real mouse in a
   window), say exactly which part was not proven; do not describe the feature as working.

Existing features without such tests (Zone Garrisons, Hospital Care, Trooper anti-air, Saboteur Hunt, Carryall
manual drop) get them before any further change to them is reported done.

## AI behaviours: prove the switch actually does its job before its score counts

Stefan, 2026-11-27: "i have a feeling the reason you think none of these things work is because you aren't
actually doing that. you are thinking you are testing these ideas but not actually implementing them. of course
the test for its usefulness will fail if you aren't actually doing it. you need much much more tests to prove you
have actually implemented a feature and this needs to be baled into your guidelines and hindsight".

He was right: on 2026-11-27 several switches were reported "level, stays off" although they barely ran
(TurretCreep "never fired" on rs192, AdvancingTurrets "never fired" on 3waysplit, PollutionRelocation moved one
zone in 30 minutes, ZoneGarrison needed two orders per entry because other controllers pulled soldiers away).
A score from a switch that did not run is not a measurement of the idea.

Before any OFF/ON score is reported, and before a switch is left off "because it was level":

1. **Activation asserts.** A test (CTest, headless, exits non-zero on failure) sets up the situation the behaviour
   is for and asserts that with the switch ON it acts (the orders, builds, demolitions, captures, trips it is
   meant to make, by count) and with it OFF it does not. One case per trigger condition, plus the cases where it
   must not act.
2. **Proximate-effect asserts.** The same test asserts the immediate thing the behaviour is supposed to achieve,
   not just that it ran: land value of the affected lots rises after a redevelopment, the captured zone changes
   owner, the turret actually stands at the front and fires, the soldier ends inside the zone, the patient's
   health rises. An action that is cancelled, re-ordered by another controller or placed somewhere else fails.
3. **Activation in the measured games.** Every OFF/ON report states, per arm and per map, how often the behaviour
   fired per house-game and how often it reached its proximate effect, read from match log lines. An arm that
   fired in fewer than half of its house-games, or whose actions mostly failed, is reported as **not tested**,
   never as "level", and the trigger or the conflicting controller is fixed and the arm run again.
4. **No silent override.** Check that no other controller (waves, defence pulls, formations, retreat, city build
   passes skipped in combat) cancels or redirects the behaviour's orders; count redirections in the log.
5. Only then does the default rule (95% interval of the paired score above 0) decide the default.

## Asked-for behaviour: an acceptance list first, and the harness refuses untested verdicts

Stefan, 2026-11-27, after building by hand (Palaces for land value, a Rocket Turret line, turrets massed into
an enemy base) what the AI switches had been reported as trying: "i ask you to try things. you say you do and it
doesn't work so then i do it and it does. that tells me you aren't actually trying the thing. you think you are
but you are not. you need more assertions".

What went wrong: the request ("turrets used offensively, a line that moves forward") was turned into cautious
rules (12 turrets at most, 10 tiles off enemy buildings, only where one enemy turret reaches), the rules almost
never triggered, and the level score was reported as the idea's result. Nothing checked the built behaviour
against the words of the request.

1. **Acceptance list before code.** Quote the request and write, for each thing asked, the observable fact that
   would show it happening in a game, with a number: "at least N turrets standing inside the enemy base's
   footprint within M minutes", "average land value of lots within 8 tiles of a new Palace rises by X". Put the
   list in the feature's `docs/handover/` write-up. Caps, stand-off distances and trigger conditions that the request did not ask for
   are listed as **deviations** with their reason; a deviation that stops the asked-for thing from happening is
   not allowed.
2. **One assert per acceptance line**, in a headless CTest that exits non-zero, run in a real game (bundled map or
   Stefan's save), not a hand-built toy position only. The final report is a table: request line, test name,
   measured value, pass or fail. A line without a passing test is reported as **not done**.
3. **A human demonstration is the acceptance test.** When Stefan shows it working in a save, the AI with the
   switch on is started from his earlier save (or the same map and position) and must reach the measured footprint
   of what he did (how many, where, how fast, what the enemy lost) within a stated tolerance. Until it does, the
   idea has not been tried, whatever the score says.
4. **The harness enforces it.** A play-style label that turns on a switch declares its activation event in the
   match log. `vanilla-match.py --report` (and `scenario-matrix.py`, `play-style-matrix.py`) print the fire and
   success rate per arm and give the verdict **NOT TESTED** in place of level/better/worse when the behaviour
   reached its effect in fewer than half of the arm's house-games. No hand-written summary may override it.

## AI and unit behaviour: mandatory regression matches

Every change that can alter AI decisions or unit behaviour (anything under `src/players/`,
`include/players/`, unit/structure/combat/pathfinding code, `QuantBot Config` defaults, game
option semantics) must pass `quantbot_all_against_atreides` and `quantbot_scenario_matrix`
during exact-SHA AWS post-push qualification, before qualified packages or releases:

```bash
ctest --test-dir build -R 'quantbot_all_against_atreides|quantbot_scenario_matrix' --output-on-failure
```

`quantbot_all_against_atreides` plays Stefan's setup to the end on six seeds - vanilla,
`5P - 128x128 - All against Atreides`, Atreides QuantBot Brutal (spectated) against four AI Player
(hard) houses, unit and harvester overrides 0 - and fails unless Atreides eliminates everyone within
50 game minutes on every seed (Stefan's limit since 2026-11-07, replacing 40; about 3.5 minutes wall,
three games at a time). Do not raise the limit or drop a seed to make a change pass; see
`docs/quantbot-all-against-atreides.md`.

The sibling `quantbot_all_against_atreides_dune2r` uses the saved **Dune2R,
non-city** setup, not the vanilla fixture. It requires wins within 40 game minutes
on all seven fixed seeds, including 906213928 and the saved seed 1702260251.
The same regex above runs both Atreides gates. It uses three games at a time and
a 600-second wall limit; do not replace it with vanilla results or force a test
style that hides a broken default.

`quantbot_scenario_matrix` is the smoke tier of the AI scenario matrix (`docs/ai-scenario-matrix.md`):
one fast game per kind - a 4-house Random Skirmish free-for-all, a Construction Yard only duel
(replayed to the same final digest), the reverse All against Atreides (Medium against four Hard must
lose), the Hard v Easy ladder on both sides, a 2v2 on Sihaya-Toto and campaign missions of levels 1, 4
and 9 the Hard partner must win - and fails on a crash, a missed expected winner, a time-out, nobody
eliminated or idle armies (a few minutes wall). Changes with a wide reach (unit mix, attack timing,
economy, campaign AI) also run the full tier on the runners and compare its table with the last one.

They must also pass `quantbot_ffa_army_deploys`, the free-for-all counterpart (Stefan's 4P DuneCity
game, 2026-10-18: "all the men sit in the base"):

```bash
ctest --test-dir build -R '^quantbot_ffa_army_deploys$' --output-on-failure
```

It plays generated 4P 160x160 DuneCity free-for-alls (seeds 9101, 9103 and 9105, three at a time;
Fortress starts, 20,000-credit armies, 40,000 cities, four QuantBot Brutal, his game options) and
fails when, on 9101, no wave of 30+ units has left by minute 8 or a house keeps its idle reserve 80%+
inside its own base for more than 6 minutes in a row, or when no seed has a house eliminated by
minute 35 (a stalemate of armies at home is a draw). The deadline was minute 30 until 2026-11-05:
stronger trooper defence then moved the first eliminations to 30.3 and 30.6 minutes, and the stuck
army this check exists for eliminates nobody in 45. Do not loosen the thresholds to make a change
pass; see `docs/formations.md`, "Free-for-all: the army deploys".

The same changes, and anything touching the game loop or rendering, must also pass
`perf_regression` (Stefan, 2026-10-18: "make sure you include performance testing as part of
regression testing"):

```bash
ctest --test-dir build -R '^perf_regression$' --output-on-failure
```

It plays Stefan's 4P DuneCity Brutal Random Skirmish map headless for 6 game minutes and fails when
the mean simulation CPU per cycle or a house's AI update exceeds its budget (multiples of a
calibration loop, so it works on any Mac). Run it on a quiet machine; see `docs/game-performance.md`
(also `tests/performance/game-perf.py --rendered` for real-window frame rates at 2 ms game speed).

Where they run: the four AI gates above are `headless` tests registered for macOS and Linux, so run
them on the AWS workers, not the Mini, from a committed SHA:

```bash
agent-workspace run --project dunecity --owner OWNER --tests --wait -- dunecity-queue ctest \
  --cwd /Users/stefan/Documents/projects/dunecity-feature-batch --rev FULL_SHA --machines aws aws-mel2 \
  -R '^(quantbot_all_against_atreides|quantbot_all_against_atreides_dune2r|quantbot_scenario_matrix|quantbot_ffa_army_deploys)$'
dunecity-queue report JOB --follow   # outside the wrapper; accepted is not passed
```

Wait for the result of every queue job you submit (games and gates alike); never end a turn on
"next" or "I will" while results are owed. Submitted through `agent-workspace run`, a job carries
your owner id, and that is how the hooks find it. In Claude Code a background hook
(`~/.local/share/agent-workspace/queue_watch.py`) starts a new turn in your session with a summary
when your jobs finish, so you need no waiter of your own. Codex keeps the turn open with
`dunecity-queue wait JOB...`; OMP starts that command as an async bash job with `timeout: 0`.
A Stop hook refuses once to end a turn while jobs run and nothing waits on them. The notice
says a job finished, not that it passed: read `dunecity-queue report JOB` first
(`~/.config/agent-workspace/POLICY.md`, "Queue jobs: wait for the result").

`perf_regression` stays on the Mini: its budgets are calibrated on that machine (and it launches a
game through the real main menu), so run it alone on a quiet Mac. Every other test that runs on the
dummy video driver is `headless` and runs on AWS (`-L nightly -LE gui`); only five more stay on the
Mini, each with its reason in `docs/ai-scenario-matrix.md`, "Where the gates run".

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
