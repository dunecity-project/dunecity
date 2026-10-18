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

## AI changes: mandatory regression matches

Every AI or unit-behaviour change (QuantBot, AI Player, units, combat, pathfinding, AI-facing
game options) must pass
`ctest --test-dir build -R 'quantbot_all_against_atreides|quantbot_scenario_matrix'` before merging:
Atreides QuantBot Brutal must eliminate the four AI Player (hard) houses on the bundled vanilla
"All against Atreides" map within 40 game minutes on all six seeds, with Stefan's game options, and
the scenario-matrix smoke tier (Random Skirmish FFA, Construction Yard duel replayed to the same
digest, reverse All against Atreides, Hard v Easy both sides, a 2v2 multiplayer map, campaign levels
1/4/9) must pass every check. Never raise the limit or remove seeds to get a change through
(`docs/quantbot-all-against-atreides.md`, `docs/ai-scenario-matrix.md`).

They must also pass `ctest --test-dir build -R '^quantbot_ffa_army_deploys$'`: four QuantBot Brutal
houses in a generated 4P 160x160 DuneCity free-for-all must launch a wave of 30+ units by minute 8,
never keep an idle reserve 80%+ inside their own base for more than 6 minutes in a row, and
eliminate a house by minute 30 (`docs/formations.md`, "Free-for-all: the army deploys"). Never
loosen these thresholds to get a change through.

The same changes, and game-loop or rendering changes, must also pass
`ctest --test-dir build -R '^perf_regression$'`: Stefan's 4P DuneCity Brutal Random Skirmish map,
6 game minutes headless, budgets on mean simulation CPU per cycle and per-house AI update time as
multiples of a calibration loop. Run it on a quiet machine (`docs/game-performance.md`).

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
