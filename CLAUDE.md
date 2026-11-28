# CLAUDE.md — DuneCity

You are working in `~/development/dunecity`, a Dune Legacy C++17/SDL2 fork with Micropolis-style city simulation integrated into the RTS game loop.

## First reads

Before making non-trivial changes, read:

1. `ARCHITECTURE.md` — repo-root entry point and settled constraints
2. `docs/dunecity-current-architecture.md` — code-verified current implementation state
3. `README.md` — project overview/build commands
4. Relevant source files for the task

For deeper background only when needed:

- `analysis/delivery-summary.md` — historical integration summary; useful but may be stale versus current code
- `analysis/architecture-comparison.md`
- `analysis/dunelegacy-architecture.md`
- `analysis/simcity-architecture.md`
- `scripts/SPRITE-IMPORT.md` for sprite/art pipeline work

## Hard architecture constraints

- R/C/I zones stay **2x2 in gameplay footprint**.
- Do **not** convert simulation, placement, or zoning rules to 3x3.
- Micropolis/SimCity 3x3 art may be referenced/imported, but adapt/render it cleanly inside the 2x2 gameplay footprint.
- Tiles remain the current substrate for city state and overlays.
- Lot objects are a possible future architecture, not an opportunistic refactor.
- **Roads are player-built structures**, alongside but separate from concrete slabs. Like Slab1, a Road is special-cased in `House::placeStructure` to mutate a tile flag (`Tile::isRoad_`) rather than spawning a `StructureBase` — so roads aren't selectable. Roads only appear in the build menu when city-sim mode is active (gated in `BuilderBase::updateBuildList`). Cost defaults to 10 credits; build is instant in city mode.
- Concrete slabs (Slab1/Slab4) and Roads are **independent tile states**. Concrete renders as concrete; roads render as an auto-tiled overlay sprite drawn on top of the underlying rock terrain. They do not share rendering or placement logic.
- **Power flows globally** based on `producedPower >= powerRequirement` on the structure's owner. There is no per-tile electrical grid and no power lines. `Tile::cityPowered_` exists in save format but is unused; the legacy `cityConductive_` was renamed to `isRoad_`.
- Dune-style power remains; WindTraps and the city-mode Nuclear Plant feed the same global house-power pool.
- Preserve deterministic simulation and save/load compatibility.

## Workspace boundaries

- Edit files under `~/development/dunecity` only unless explicitly instructed.
- `../simcity` is read-only Micropolis reference.
- Do not hand-edit generated/imported assets unless the task is specifically about the asset pipeline.
- Disk hygiene (see AGENTS.md): delete your worktree's `build*/` once its branch is merged
  (`scripts/prune-merged-worktrees.sh` lists them); game batches use leased scratch under
  `/private/tmp/dunecity-work`, never long-lived ad-hoc `/tmp` directories.

## Likely touch points

City simulation:

- `include/dunecity/`
- `src/dunecity/`
- `include/Tile.h`, `src/Tile.cpp`
- `include/Game.h`, `src/Game.cpp`
- `include/Command.h`, `src/Command.cpp`

Zone/build/render work:

- `src/structures/ZoneStructure.cpp`
- `include/structures/ZoneStructure.h`
- `src/structures/ConstructionYard.cpp`
- `src/structures/BuilderBase.cpp`
- `src/FileClasses/GFXManager.cpp`
- `include/FileClasses/GFXManager.h`
- `scripts/import-micropolis.py`
- `scripts/import-sprites.py`
- `imported_sprites/`

Tests:

- `tests/`
- `tests/CMakeLists.txt`

## Versioning

The canonical app version lives in three source-controlled files, kept in sync by `scripts/bump-version.sh`:

```bash
scripts/bump-version.sh 1.0.8          # set version
scripts/bump-version.sh --check        # verify all files agree
scripts/bump-version.sh 1.0.8 --dry-run  # preview changes
```

Tag releases use `vX.Y.Z` (e.g. `v1.0.8`). CI verifies that source metadata already matches the tag before building; it will not auto-bump for you.

Do not hand-edit `CMakeLists.txt` project VERSION, `include/config.h` VERSION, or `vcpkg.json` version separately. Use the script.

Generated build outputs (`build/include/config.h`, app bundle `Info.plist`) are not source of truth -- they are derived at configure/build time from `CMakeLists.txt`.

Release/version rule: any user-visible release build, tag, or CI-triggering push must include the intended version bump in the same commit as the release work. Before saying a release is tagged, pushed, building in CI, or ready for Stefan to test, run:

```bash
scripts/bump-version.sh --check
git diff -- CMakeLists.txt include/config.h vcpkg.json
git tag --points-at HEAD
```

If the tag is meant to be `vX.Y.Z`, the checked version must be exactly `X.Y.Z`. If it is not, stop and fix the version before tagging or reporting completion.

## Build/test commands

Use targeted verification. Start with:

```bash
cmake --build build --target dunelegacy
cmake --build build --target dunelegacy_tests
./build/tests/dunelegacy_tests
```

For every Claude implementation run that changes gameplay, UI, assets, config,
or release/version behaviour, leave a locally built version in Stefan's dev
checkout before reporting completion:

```bash
cd /Users/stefanclaw/development/dunecity
cmake --build build --target dunelegacy
open build/bin/dunecity.app
```

The `open` command is optional and only for manual smoke testing; the required
deliverable is that `build/bin/dunecity.app` is rebuilt in the dev folder so
Stefan can launch it. If the local build tree is stale or the target names have
drifted, fix the build-tree issue or report the exact blocker instead of
claiming the feature is done.

For builds intended for Stefan or a release tag, verify the source-controlled
version first with `scripts/bump-version.sh --check`, then rebuild. Do not rely
on an already-open app bundle, a stale build tree, or a tag name as proof of the
visible version.

If those fail because the local build tree is stale or target names differ, inspect `README.md`, `BUILD.md`, `CMakeLists.txt`, and `tests/CMakeLists.txt` before changing build configuration.

## Style of work

- Prefer small, compileable changes.
- Fix the direct cause before broad refactors.
- Add/update tests when touching placement, rendering lookup, command routing, save/load, or city sim behaviour.
- Keep DuneCity-specific logic clearly named and isolated where possible.
- If a task tempts you to redesign zoning, stop and write the tradeoff down first.

The main trap: solving a 3x3 art/rendering problem by accidentally changing the 2x2 gameplay architecture. Don’t step on the rake. It has teeth.

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

## AI changes: mandatory regression matches

Every AI or unit-behaviour change (QuantBot, AI Player, units, combat, pathfinding, AI-facing
game options) must pass
`ctest --test-dir build -R 'quantbot_all_against_atreides|quantbot_scenario_matrix'` before merging:
Atreides QuantBot Brutal must eliminate the four AI Player (hard) houses on the bundled vanilla
"All against Atreides" map within 50 game minutes on all six seeds (Stefan's limit since 2026-11-07,
replacing 40), with Stefan's game options, and
the scenario-matrix smoke tier (Random Skirmish FFA, Construction Yard duel replayed to the same
digest, reverse All against Atreides, Hard v Easy both sides, a 2v2 multiplayer map, campaign levels
1/4/9) must pass every check. Never raise the limit or remove seeds to get a change through
(`docs/quantbot-all-against-atreides.md`, `docs/ai-scenario-matrix.md`).

That regex also runs `quantbot_all_against_atreides_dune2r`: the saved powered,
non-city Dune2R setup must win on seven fixed seeds within 40 game minutes,
including 906213928 and 1702260251. Its three-at-a-time run has a 600-second wall
limit. It tests the shipped default, without a forced play-style override;
passing the original vanilla test is not a substitute.

They must also pass `ctest --test-dir build -R '^quantbot_ffa_army_deploys$'`: four QuantBot Brutal
houses in generated 4P 160x160 DuneCity free-for-alls (seeds 9101, 9103, 9105) must launch a wave
of 30+ units by minute 8, never keep an idle reserve 80%+ inside their own base for more than 6
minutes in a row, and eliminate a house by minute 35 on at least one seed (minute 30 until
2026-11-05; `docs/formations.md`, "Free-for-all: the army deploys", has the measured reason). Never
loosen these thresholds to get a change through.

The same changes, and game-loop or rendering changes, must also pass
`ctest --test-dir build -R '^perf_regression$'`: Stefan's 4P DuneCity Brutal Random Skirmish map,
6 game minutes headless, budgets on mean simulation CPU per cycle and per-house AI update time as
multiples of a calibration loop. Run it on a quiet machine (`docs/game-performance.md`).

The four AI gates are `headless` and registered for Linux too: run them on the AWS workers with
`dunecity-queue ctest --rev FULL_SHA --machines aws aws-mel2 -R '<gate regex>'` inside
`agent-workspace run --tests --wait`, not on the Mini (AGENTS.md has the exact command).
`perf_regression` stays on the Mini: its budgets are calibrated on that machine. Every dummy-driver
probe is `headless` and runs on AWS too; the three other Mini-only tests and their reasons are in
`docs/ai-scenario-matrix.md`, "Where the gates run".

## AI test games: scoring and the default rule

AI test reports (`tests/ai/formation-duel.py`, `vanilla-match.py`, `play-style-matrix.py`) score games
with `tests/ai/match_score.py`. The score is placement: 1st +1 ... last -1. A time-out is a negative
draw: survivors share the top places minus 0.25, so a 1v1 draw is -0.25. A candidate becomes a default
only when the 95% interval of its paired score difference against the control is above 0, and it is not
worse on Fortress, Brutal and `/weak`. Report each arm's time-out rate. `--scoring legacy` is only for
comparing old reports (`docs/ai-play-styles.md` section 7).

## Release and hosting handover

Before release, website or SourceForge work, read [docs/release-operations.md](docs/release-operations.md)
and [docs/sourceforge-releases.md](docs/sourceforge-releases.md). They map the three repositories,
automated versus manual publishing, verification and recovery commands. SourceForge Files
contains binaries, notes and checksums only; link to tagged Git source, never upload a source archive.
Keep these runbooks current when changing deployment behaviour. Credentials live in GitHub
Actions secrets/local SSH storage, never in these documents. Verify current service state;
historical HANDOVER entries are dated evidence, not claims that a deployment is still pending.
