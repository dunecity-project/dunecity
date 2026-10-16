# Headless simulation performance

How to run large batches of real-engine headless games (formation duels, campaign probes,
`quantbot_*` ctest probes) quickly on the Mac mini, what costs what, and what was changed
(branch `perf/headless-sim`, 2026-10-16). The simulation itself is untouched: every change
below was checked to give bit-identical games (see "Determinism").

## Quick start

```sh
# Duels: defaults are already the fast settings (one path thread per game, no AI log,
# biggest maps first). Pick --jobs from the table below.
python3 tests/ai/formation-duel.py --scratch /tmp/duels --results /tmp/duels/results --jobs 8 ...

# Benchmark a build: three fixed Hard duels (128/7101, 160/7202, 192/7303, 20 game minutes)
python3 tests/performance/headless-bench.py --jobs 1,4,8 --label mine --results /tmp/b.jsonl
python3 tests/performance/headless-bench.py --summary /tmp/b.jsonl
# Prove an engine change did not change the simulation (same games, two binaries):
python3 tests/performance/headless-bench.py --probe OLD/balance-probe --label old --results /tmp/old.jsonl
python3 tests/performance/headless-bench.py --probe NEW/balance-probe --label new --results /tmp/new.jsonl
python3 tests/performance/headless-bench.py --compare /tmp/old.jsonl /tmp/new.jsonl   # exit 0 = identical
```

`headless-bench.py` records per game: wall and CPU seconds (runner + probe), peak RSS, the
probe's setup/loop split, game cycles per second of the loop, the outcome and every
`PROBE_DIGEST` and `FORMATION_*` line. `--compare` requires all of them to match.

## Settings that matter

| Setting | Use | Why |
|---|---|---|
| `--path-workers 1` (`DUNECITY_PATH_WORKERS=1`) | batches (formation-duel default) | Path search runs inline. With 4 worker threads per game a full batch spends CPU on hand-offs and waits; inline used ~37% less CPU per game under load. Results are identical for any count. Single interactive-style runs may still prefer the default 4. |
| `--no-ai-telemetry` (`DUNECITY_AI_TELEMETRY=0`) | batches (formation-duel default) | No `events.jsonl` (18 MB per 20-minute 128 game, more on bigger maps) and no JSON parsing afterwards; ~3% CPU. Same simulation. ctest probes keep it: some assert on it. |
| `BALANCE_UNIT_PHASE_TIMING=1` | only when profiling | Probes switch the per-unit phase timing (`DuneCity-Performance.log`) off; it cost ~5% of a busy match in clock reads. |
| `BALANCE_DIGEST_CYCLES=N` | determinism checks | `PROBE_DIGEST` interval in cycles (default 750 = 12 game s, 0 = only the final one). |
| `--jobs` | see scaling table | One game uses one core with `--path-workers 1`. |

Probe binaries are cached per build tree in `build/balance-probe-cache/` (key: generated
source and compile/link commands; validated against the depfile and every link input's
size and mtime). Every ctest `quantbot_*` probe and every `run-campaign-balance.py` run
after the first reuses it instead of compiling and linking `balance-main.cpp` again
(~10 s each). `--prebuilt-probe` still works and skips even the check.

## Where the time goes (profiles with `sample`)

192 map, 40-minute Hard duel, game minutes 0:20-1:20 under load, origin/main 1.0.842,
default harness (4 path workers, AI log on). Percentages are of the main thread:

- 37%: waiting for the 4 path workers (`PathWorkerPool::runBatch`); the workers did ~0.47
  core of A* between them.
- 25%: the duel harness itself (`FormationWaveMeasure`), 17% of it `inTargetArea` (every
  wave member against every building, every cycle).
- 5.5%: `mach_absolute_time` from the per-unit phase timing in `UnitBase::update`.
- 19% units (targeting/findTarget ~5%, move), 8% QuantBot, 4% structures (turret scans).
- ~2% AI telemetry (JSON records, writes); audio thread ~1% of a core (OPL music on the
  dummy driver). Rendering: none in the loop (probes call `updateGameState` directly).

With the harness fixed and path search inline, A* is 53% of a game (`AStarSearch::step`
26% self, heap `extractMin`/`putOnOpenListIfBetter` 12-15%, `canPathThrough` 6%), units 16-20%,
QuantBot 10%, harness 2.5%, `Map::removeObjectFromMap` 2-3% (scans every tile per removal).

Process start-up is ~1.5-1.9 s per game: `GFXManager` 33%, re-activating the mod (graphics
reload) 28%, voice loading 16%; a fresh profile also copies 36 MB of bundled mods (~0.3-0.5 s).
Peak RSS is ~590-740 MB per game (start-up surfaces), ~420 MB steady, so memory does not
limit 10 parallel games on 16 GB.

## Changes (all bit-identical)

Engine:
- A* frontier heap slots carry their tile's `f`, so sifting compares slots instead of looking
  up the scratch buffer per comparison; one entry lookup per insert; neighbour offsets and
  per-node invariants hoisted out of the eight-neighbour loop.
- `fix32_mul` is defined inline in `fix32.h` (same arithmetic, no call per multiply).
- `PathWorkerPool`: the calling thread takes tasks alongside the workers instead of sleeping;
  one-task batches run inline.
- `UnitBase::update`: one clock read per phase boundary (was two), none with
  `FrameTiming::unitPhases` off.

Harness:
- `formation-wave-measure.inc`: live building footprints once per cycle and an axis-distance
  reject before the exact `blockDistance` test; hashed maps for per-cycle bookkeeping.
- `run-campaign-balance.py`: probe-binary cache, `--no-ai-telemetry`.
- `formation-duel.py`: `--path-workers 1` and no AI log by default, biggest maps first within
  each seed.
- `probe-digest.inc`: `PROBE_DIGEST` / `PROBE_TIMING` lines; `headless-bench.py`.

## Determinism

`PROBE_DIGEST` = the lockstep digest (`Network/GameStateDigest.h`: cycle, shared RNG seed,
house credits/counts, every object's id, item, owner, health, tile) plus every object's exact
fixed-point position and angle, destination and target, and each house's exact credits,
every 750 cycles and at the end. See the results section for what was compared.

## Results

Fixed benchmark, three Hard duels (128/7101, 160/7202, 192/7303, 20 game minutes),
`max(3, jobs)` games per level, 2026-10-16 on the Mac mini while other agents' duels kept
the load average at 30-36 (so wall times are pessimistic; CPU s/game is the steadier figure).
"before" = origin/main 1.0.842 engine and harness defaults (4 path workers, AI log on);
"after" = this branch with the new defaults.

| build | jobs | games/h | wall s/game | CPU s/game | loop cycles/s | peak RSS MiB |
|---|---|---|---|---|---|---|
| before | 1 | 56 | 63.7 | 55.4 | 1363 | 727 |
| before | 4 | 156 | 72.5 | 47.1 | 1198 | 536 |
| before | 6 | 194 | 93.2 | 52.7 | 925 | 475 |
| after | 1 | 67 | 53.5 | 28.9 | 1774 | 729 |
| after | 2 | 123 | 55.1 | 32.3 | 2255 | 705 |
| after | 4 | 279 | 37.3 | 27.8 | 2604 | 531 |
| after | 6 | 487 | 34.3 | 30.0 | 3052 | 465 |
| after | 8 | 519 | 42.9 | 32.4 | 2396 | 405 |
| after | 10 | 589 | 46.5 | 33.4 | 2204 | 460 |

At six parallel games the batch went from 194 to 487 games/hour (2.5x); CPU per game fell
about 40%. Same-settings comparisons on a quieter machine (load 5-10, `--jobs 3`,
inline paths, no AI log): origin/main 25.2 s/game, harness fixes only 22.4, plus engine 18.8
(-25% wall, -27% CPU); engine default (4 workers) with harness fixes 26.2 vs 43.1 s/game before.

Recommended parallelism: `--jobs 8` on an otherwise idle 10-core Mac mini (one core spare
for the OS and Python), `--jobs 6` while other agents run batches (above 6 the shared machine
gave little: 487 -> 589 games/h for 4 more cores). Keep `--path-workers 1`.

Determinism evidence (`headless-bench.py --compare`, all `PROBE_DIGEST` lines, outcome,
house results and every `FORMATION_WAVE`/`FORMATION_HOUSE` measurement line):
- the final table: 47 runs of the three games (before and after, 1-10 parallel, 4 vs 1 path
  workers, AI log on vs off): identical, 101 digests each;
- the 192/7303 duel over 40 game minutes (150,000 cycles, 201 digests), origin/main vs both
  stages of this branch: identical;
- earlier runs: origin/main with 4 vs 1 path workers, AI log on vs off: identical.

ctest (`-j3`, Release tree `build`): dunelegacy_tests, game_command_regressions,
network_wire_harness, formation_planner_tests, formation_movement_probe,
skirmish_generator_tests, active_tile_probe and all 29 `quantbot_*`: 36/36 passed in 214 s
(each `quantbot_*` probe 3-22 s, reusing the cached probe binary; a first compile+link is
~10 s). No pathfinding/performance probes are registered in ctest; their runners in
`tests/pathfinding` and `tests/performance` were not run.

## Not done (ideas, measured costs above)

- Skip graphics in headless batches: start-up `GFXManager` + mod re-activation is ~1 s and
  most of the 420-740 MB per process. Needs a headless flag through `GFXManager`, the
  sidebar and every caller that reads surfaces; not worth the risk for ~1-5% of a duel.
- Shared read-only mod payload instead of copying 36 MB into every fresh profile (~0.3 s).
- `Map::removeObjectFromMap` scans all tiles per removal (2-3%); an exact faster version
  needs an object-to-tiles index kept in step with every assign/unassign.
- `ObjectManager` is a `std::map` (lookups in `Tile::getGroundObject`, `ObjectPointer`); a hash
  index beside it would speed lookups, iteration order must stay the map's.
- A*: smaller `TileData` (56 bytes), `reserve` for the frontier, unchecked tile access.
- Build flags: LTO/`-mcpu=native` were not tried; release flags unchanged.
- Audio: probes still open the dummy device and play OPL music (~1% of a core).
