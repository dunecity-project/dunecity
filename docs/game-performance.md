# Game frame rate (interactive game at 2 ms game speed)

Stefan, 2026-10-18 (1.0.847, MacBook Air M5): "the ai changes appear to have killed the fps ... I
have 13 fps" and "make sure you include performance testing as part of regression testing".
Investigation on branch `perf/game-fps`. Raw results: `~/Documents/projects/dunecity-results/game-fps`
on the Mac mini (JSONL per run, his save, perf logs, samples).

## Tools

`tests/performance/game-perf.py` runs a match through `run-campaign-balance.py` with
`BALANCE_PERF=1` (`tests/performance/game-perf.inc`) and appends one JSON line per run:

- **stepped** (default): the probe loop, cycle by cycle. Per cycle: `updateGameState()` wall and
  thread-CPU time, every house's own update (the engine's `house.update` timing, read from the AI
  decision log's counters), city simulation, units, structures, path search; p50/p95/p99/max; a
  frame model of `--frame-cycles` (10 = the engine's cap per displayed frame at 2 ms game speed);
  the top AI telemetry scopes; `PROBE_DIGEST` lines (determinism). A calibration loop (integer
  hashing over 4 MiB, thread CPU, best of five) is timed in the same process.
- **`--mainloop` / `--rendered`**: the interactive `Game::runMainLoop()` with the engine's own frame
  pacing at game speed 2 ms (`BALANCE_PERF_GAME_SPEED`), headless or in a real Cocoa window
  (`--viewport`, default his 1440x900). Frame, AI, city, units, structures, path and render cost
  per frame and per-house update maxima come from the decision log's five-second windows.
- Scenarios: `--scenario stefan-map` = `tests/performance/data/stefan-4p-dunecity-brutal.ini`, the
  map of his slow game (4P 160x160 Random 1v3 FFA DuneCity T9, seed 779636253, taken from the
  Workshop revision his save pins), with the save's roster (four QuantBot Brutal, spectated), match
  seed 1443930314 and game options (`stefan-game-options.ini`, the dunecity mod's). Loads on every
  release since 1.0.833. `--save FILE --save-profile DIR` continues a save (his `slow.dls` pins a
  29 MB mod revision from his profile, so it is not in the repository).
- `--source TREE --build-dir TREE/build` measures an older release: the script copies
  `game-perf.inc` into that (throwaway) worktree and patches its probe and runner.
- `--summary R.jsonl` prints tables; `--check` is `perf_regression` (below).
- **Unit motion trace**: `--motion-csv FILE` writes one row per unit whose position changed:
  cycle, cycles since the previous row, frame, id, item, house, `realX/realY` and the change,
  location/next tile, `moving`/`justStoppedMoving`, Dynasty route tick and step start/end, carryall
  wait, active, flying. Stepped: the simulated positions, before every cycle; `--mainloop`/
  `--rendered`: the drawn positions (inside `UnitBase::DrawnStepScope`, where the build has it)
  before each frame's first cycle, i.e. what that frame drew. Used for Stefan's `skipping.dls`
  (`docs/unit-route-comparison.md`, "Drawing chained steps").

## Findings

| measurement (2 ms game speed) | fps | cycles/frame | AI / render ms per frame | worst house update |
|---|---|---|---|---|
| his MBA, his 1.0.847 session (DuneCity-Performance.log) | 13 | 10 (capped) | 50-90 / 12-19 | 50-545 ms |
| his MBA, same 1.0.847 code, his `slow.dls`, scratch profile, window in front | ~100 (120 Hz) | 4.95 (full 500 cycles/s) | 2.2 / 5.4 | 28.8 ms |
| same, with a copy of his `Dune City.ini` (FrameLimit off, ScaleHD, decision log on) | ~105 | 4.74 (full speed) | - | - |
| same, decision log off | ~105 | 4.75 (full speed) | - | - |
| his MBA, `stefan-map` from start, 6 game min | 107 | 4.66 (full speed) | 1.95 / 5.9 | 24.6 ms |
| quiet Mac mini (base M4), `slow.dls`, rendered | 59.9 (vsync) | 8.23 (full speed) | 3.7 / 9.8 | 25.3 ms |

Rendered version bisect, quiet Mac mini, `stefan-map`, 8 game minutes, 2 ms: 1.0.833, 842, 843,
844, 845, 846 and 847 all run 59.4-60.0 fps (vsync-capped) at the full 500 cycles/s; AI per frame
3.1 / 3.3 / 3.4 / 3.7 / 3.9 / 3.9 / 3.5 ms; worst house update 25-31 ms in every release; no house
update over 33 ms. Stepped (uncapped, one path thread, 10 game minutes) simulation ms per cycle:
0.80 / 0.84 / 0.69 / 0.61 / 0.90 / 0.91 / 0.91 (the games differ by release, so these move with
each release's own play, not only its code cost).

Conclusion: no 1.0.833-1.0.847 code change makes this game slow. The identical 1.0.847 code
(with his settings file, with and without the decision log) runs his saved game at full 2 ms speed
on his own Air. His 13 fps session had every simulation component (AI, city simulation, units)
3-7x slower per cycle than the same build anywhere else, which points at the machine state during
that session (his own load average was 11-33; an occluded/App-Napped window may run on efficiency
cores; the game has no App Nap opt-out), not at the simulation code. Not yet compared: the
`/Applications` app launched through the menus versus the probe (both run `updateGameState()` and
`renderFrame()` the same way; the menu path adds `Workshop::updatePublications()` per frame).

Threading (unchanged since 1.0.833 apart from 8173ad73, 16 Oct: the calling thread helps run a
path batch and one-task batches run inline): main thread = input, the whole simulation including
all AI, rendering and the decision log; up to min(4, cores) path workers; audio. AI and rendering
were never threaded. No probe default leaks into the game (`FrameTiming::unitPhases` defaults on,
`DUNECITY_PATH_WORKERS` unset, the decision log now per `[General] AI Decision Log`, below).

His Air held 33 GB of old AI decision logs (`~/Library/Application Support/Dune City/ai-decisions`,
429 sessions); deleted with his OK on 2026-10-18.

## The real app on his Air (2026-10-18, agent GameFPS2)

A copy of the installed 1.0.847 bundle (`~/dunecity-runner/gamefps2/dc847.app`, same SHA256),
launched through LaunchServices (`open -n`, as the Dock does) with a scratch profile holding a copy
of his `Dune City.ini` (Fixed Seed on), driven through the real menus by an injected test-only
input driver (`DYLD_INSERT_LIBRARIES`, SDL_PushEvent: main menu -> Random Skirmish, or Continue for
his `slow.dls`). Figures from the game's own decision-log performance windows, on AC:

| state | fps | cycles/s (target 500) | AI / city / units / path / render ms per frame |
|---|---|---|---|
| window in front, 450-900 units | 100-117 | 475-499 | 1.4-2.9 / 0.35-0.6 / 0.4-1.3 / 0.1-2 / 3.3-5.9 |
| fully covered by another app's full-screen window, 1,000-1,300 units | 30-40 (one 5 s window 94) | 290-400 (10-cycle cap) | 7-11 / 1.8-2.5 / 4.3-6.4 / 3-11 / 2.7-4.5 |

So the real menu-launched app runs his game at full speed in front, like the probe. Covered, the
per-cycle cost of every component (the city simulation's too, which barely depends on unit count)
roughly doubles while the main thread keeps its UI priority (`ps -M`: 46/47): macOS gives a
non-focal, occluded app's thread less CPU performance. Not separated: the unit count also grew by a
quarter during the covered minute, and Stefan closed the test game before the planned back-and-forth
and the opt-out comparison (NSProcessInfo activity) could run. His slow session also started on
battery with Low Power Mode on (`pmset -g custom`: Battery lowpowermode 1; AC from 12:42:58), and
after plugging in, system daemons used 3-4 cores (load 15-33). Likely explanation of 13 fps: window
not focal/covered (and/or Low Power Mode, background system load), not a code change; not proven.

Per-frame work only the app does, from his 1.0.847 sample: `Workshop::updatePublications()` 46 of
1,003 ms, nearly all in a TLS handshake that reloads the CA bundle (`X509_STORE_load_locations`) on
the main thread; his outbox holds 43 queued "collect" entries since 8 Oct that never drain (one try,
then a 60 s retry). A periodic 20-40 ms hitch, not a throughput cost; not changed.

Changes from this round (no simulation change):

- The game holds `NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical`
  while a match runs (`GameActivity::Scope` in `Game::runMainLoop`, `src/misc/GameActivityMac.mm`);
  idle system sleep stays allowed. Its effect on a covered window is not yet measured on the Air.
- F12 (fn+F12 on a Mac laptop) overlay: fps, cycles per frame, achieved vs target cycles/s, and
  AI / city / units / structures / path / render / other ms per frame.
- `[General] AI Decision Log` (Settings, advanced page: "AI decision log" next to "Diagnostic logs"), default off,
  independent of "Diagnostic logs"; `DUNECITY_AI_TELEMETRY=0/1` forces it off/on. Every game start
  prunes `ai-decisions/` to the newest 20 sessions and 2 GB (`AITelemetry::pruneSessions`, room
  left for the new session's 256 MB cap). Harnesses that read the log enable it explicitly
  (`run-campaign-balance.py` sets `DUNECITY_AI_TELEMETRY=1` unless `--no-ai-telemetry`; INI-based
  probes write `AI Decision Log = true`).

Evidence to capture next time it is slow: F12 overlay (photo); Activity Monitor > CPU and Energy
for dunecity (App Nap column, % CPU); whether the window was in front and the Mac on battery/Low
Power Mode; `~/Library/Application Support/Dune City/DuneCity-Performance.log` (needs "Diagnostic
logs"); `sample $(pgrep -x dunecity) 10 -file ~/Desktop/dc-sample.txt`; `pmset -g batt`, `uptime`.

## Changes (bit-identical)

- `QuantBot::reactorClearance` walked every structure on the map (all houses, every city zone) for
  every placement candidate: the top of his profile. The blast-spacing footprints are now collected
  once per search (`reactorBlastSites`, `clearsBlastSites`) in `findPlaceLocation` and
  `findRedevelopmentSite`; same pairs, same answers.
- `findPlaceLocation` built a JSON telemetry record (`AITelemetry::Record`, string quoting) for every
  scored candidate; it now keeps plain numbers and writes the record once for the chosen site, with
  the same fields in the same order.

Measured back to back on the mini (`stefan-map`, 10 game minutes, one path thread, machine under
other agents' load): QuantBot build time per house -25 to -32% (h2 6,834 -> 4,775 ms), simulation
mean 1.99 -> 1.78 ms per cycle, p95 7.85 -> 7.08 ms, worst house update 76 -> 66 ms. All 51
`PROBE_DIGEST` lines identical.

## perf_regression (ctest, mandatory with `quantbot_all_against_atreides`)

`game-perf.py --scenario stefan-map --minutes 6 --path-workers 1 --check` (~45 s). Budgets are
multiples of the calibration loop measured in the same process (thread CPU), so they hold across
machines and moderate load:

| metric | budget | 1.0.847 + changes (mini, load 25, calibration 9.9 ms) |
|---|---|---|
| mean simulation CPU per cycle / calibration | 0.25 | 0.135 |
| worst house p99 update / calibration | 1.0 | 0.35 |
| worst single house update / calibration | 10 | 7.5 |

House updates are the engine's wall-clock timings, so the maximum is the noisiest figure on a
loaded machine; run it on a quiet one before concluding a change regressed it. The budgets catch
a doubling of AI or simulation cost; they do not separate 1.0.833 from 1.0.847 (which cost the same).

`--menu-launch 30` (also in the ctest) then plays 30 s started the way a player does
(`tests/performance/menu-launch.inc`: the real `MainMenu`, `RandomSkirmishMenu` with his choices
from `[Random Skirmish]`, its Start handler, `startSinglePlayerGame`, `Game::runMainLoop` with the
Workshop pump, Discord, analytics; metaserver at a closed local port) and budgets the per-frame
time outside the measured components: mini headless 143 fps, 489 cycles/s, other 0.23 ms/frame =
0.032 x calibration (budget 0.3).

## Not done

- Rendering (5-10 ms per frame at 560 units) and the city simulation's 16-phase spikes were not
  changed; neither differs between releases.
- Spreading houses' 100-cycle build passes so two never land in one 10-cycle frame (houses 1 and 3
  are 8 cycles apart) would change AI timing, i.e. game results; not done.
- No duel-report perf column.

## Eight houses on a 384x384 map (2026-10-20/21, branch `perf/fps-8house`, agent FpsFix)

Stefan (1.0.849, MBA, `fpsstillfucked.dls`: "4P - 384x384 - new beginning", eight QuantBot Hard,
~810 units, Game Speed 2 ms, battery + Low Power Mode): "fix the fucking fps lag". His perf log:
every frame 10 cycles, 37-90 ms, AI 17-60 ms per frame, `[CITYSIM SPIKE]` 20-34 ms every 78 cycles.
Raw data and `sample` reports: `~/Documents/projects/dunecity-results/fps-8house` on the mini.

Cause: work proportional to the map area (147,456 tiles, 9x a 128x128 map) done per call, per
house. `sample` of the stepped probe on his save (mini): QuantBot placement's four chamfer
distance fields (`CityDistanceField::build`, 6,063 map-sized builds in 7,500 cycles, 0.83 ms each,
for 27,509 queries), `Map::removeObjectFromMap` walking every tile per unit pickup/death, the
city effects scan's per-sand-tile 11x11 stamp (~12 million updates per scan) and seven whole-map
origin walks in row order over column-major tiles, the rock survey's candidates x enemies
clearance loop and per-tile loss-history walks, the anchor field rebuilt per search, the
harvester sweep's unsafe-field walk per spice tile, the air-coverage raster `escape()` and
(2r+1)^2 `blockDistance` stamps.

Changes (all exact; `PROBE_DIGEST` every 750 cycles identical over 7,500 cycles of his save):

- `CityDistanceField` answers each query from the bucketed source rectangles (closed-form
  Chebyshev gap) instead of filling every cell; test against the old chamfer field.
- `Map::removeObjectFromMap` visits only tiles in an object-tile bitset maintained by `Tile`'s
  assign/unassign (full scan and rebuild if an assignment ever hits a non-current map).
- City effects: origin walks collect from `structureList` sorted by origin tile (same set and
  order); tile scan column-major; the sand-as-water stamp summed with a summed-area table
  (saturating adds of non-negative values; falls back to per-tile stamps otherwise).
- QuantBot: anchor field cached per house on `Tile::ownerRevision` (bumped by every real
  `setOwner` and tile construction/load), replacing the per-`build()` scope and manual
  invalidations; `RockExpansionPolicy::choose` uses one chessboard distance transform (off-map
  enemy indices measured directly, as before) and a per-formation reserved flag; rock survey
  loss tests stamped once; harvester sweep's remembered-field test stamped once, spice list
  read column-major; `factoryEnemyClearance` computed when read (placement/save), not per
  danger refresh; `AirStrikePolicy::Coverage` row spans and ring-ordered `escape()`.
- `game-perf.inc` reports `ai_scope_totals_ms` (all houses and items, city scopes: ms, calls,
  worst call). The decision log keeps 512 metric keys, so counts can be truncated.

Stepped probe on his save, mini (base M4, other agents' load 5-7), 7,500 cycles from cycle 73,528:

| | sim ms/cycle mean / p99 / max | AI mean / max | city mean / p99 / max | 10-cycle frame mean / p99 | frames > 33 / > 50 ms (of 750) | model fps |
|---|---|---|---|---|---|---|
| 1.0.849 | 2.78 / 32.7 / 98.6 | 1.70 / 98.2 | 0.33 / 18.1 / 24.7 | 27.8 / 87.9 | 206 / 62 | 35.9 |
| perf/fps-8house | 1.42 / 12.4 / 57.4 | 0.69 / 56.9 | 0.13 / 2.8 / 16.2 | 14.2 / 35.7 | 9 / 6 | 70.2 |

First pass's open items, closed by the second pass below except the rendered Air runs and the
1.0.833/1.0.847 comparison (the 9868 save does not load there).

### Second pass (2026-10-21, agent FpsFix2): the 57 ms spike, frame pacing, perf_regression

**The 57 ms house update** was `manageHarvesterSafety`, not bundling. Every harvester of a house
is checked on the same pass (`nextCheck` = now + 2 s for all of them), and each threatened one
swept every spice tile (~72,000) in map order, route-testing (`escapeCorridor`, a line walk of
up to 384 steps) every candidate that beat the running best. From a far map corner that is
10,000-12,000 failing corridors per harvester before the nearby winner: one pass of house 3
had 18 sweeps, 201,038 corridor tests, 29.6 ms of 33 ms. Exact fix:
`TacticalSafetyPolicy::lowestSafeCandidate` tests corridors in ascending (score, position)
order (score bands that double) until the first safe one, which is exactly the candidate the
sequential sweep kept (first safe one of the lowest score; unit test against the old loop on
3,000 random cases); the sweep's spice list also records plain tiles (no mountain, no ground
object), where `canPass()` is true for every unit. One corridor test per sweep instead of
~11,000; `checkAllUnits` worst pass 56 -> 11 ms. The decision log's `rejected_routes` now counts
only failed tests of candidates at least as good as the pick (telemetry only).

**Frame pacing** (`include/FramePacing.h`, `Game::runMainLoop`): a single-player frame stops
running due cycles once they have used 16 ms (`FramePacing::kCatchUpBudgetMs`; the first cycle
always runs); the owed time carries to the next frame under the existing 24 ms cap, so a game
that cannot keep up runs slower than its set speed instead of drawing at 20 fps. Network games:
no-op (the ten-cycle cap alone, so a paced peer never holds lockstep back). Only cycles per
frame change; the guardrail still counts only ten-cycle frames. Telemetry `frame.catchup_limited`;
`FrameTiming::frameMsHistogram` (whole-match 1 ms buckets) for frame percentiles.
`Game::beforeCycleHook` (test probes only) lets game-perf digest the interactive loop's cycles.
Note that a single-player game's path budget also follows the measured frame rate
(`applySinglePlayerBudgetAdjustment`, unchanged), so interactive single-player cycles were
never independent of wall time; the stepped probe never adapts.

**perf_regression** now also continues the save itself for 2 game minutes (`--eight-house 2`,
`--scenario eight-house`): `tests/performance/data/eight-house-384` holds the save (gzip,
419 KB), both Workshop manifests and the three files this tree lacks; the other 849 mod files
are this tree's dunecity skins and `config/` ini files, found by SHA-256 (a changed skin fails
by name). Budgets, multiples of the calibration loop: p99 simulation CPU per cycle 3.0, p99
modelled 10-cycle frame (CPU) 9.0; then the same minutes through the headless main loop at 2 ms
with pacing: its 750-cycle `PROBE_DIGEST`s must equal the stepped run's (up to any frame-rate
path-budget change, which is reported), p99 frame wall time <= 8.0. Mini:

| | cpu p99 | frame-CPU p99 | main-loop frame p99 | result |
|---|---|---|---|---|
| 1.0.849 (4018fe36) | 5.4-6.3 | 13.3-16.8 | 18.7 | fails all three |
| this branch | 1.3-2.2 | 3.7-6.2 | 5.8 (10 digests equal) | passes |

Measurements, `fpsstillfucked.dls` from cycle 73,528, 7,500 cycles, game speed 2 ms. "Main
loop" is `Game::runMainLoop` headless (dummy video driver, software renderer, ~5-7 ms per
frame), not a 1440x900 ScaleHD window. 1.0.849 main-loop runs use the 1.0.849 source plus the
frame histogram and digest hook only (`/private/tmp/dunecity-fps-849-base`). Digests: 21 over
15,000 cycles of the stepped probe equal 1.0.849's; 10 of the main loop with pacing equal the
stepped run's on the mini and on the Air.

| | host / power | sim ms/cycle mean / p99 / max | 10-cycle frame p50 / p95 / p99 | frames > 33 ms | model fps |
|---|---|---|---|---|---|
| 1.0.849 | mini (load 5-7) | 2.78 / 32.7 / 98.6 | 23.8 / 54.1 / 87.9 | 27.5 % | 35.9 |
| this branch | mini | 1.34 / 11.6 / 24.3 | 12.2 / 24.9 / 33.3 | 0.9 % | 74.9 |
| 1.0.849 | Air M5, battery, Low Power Mode | 4.36 / 54.2 / 134.9 | 37.8 / 89.7 / 144.1 | 58.4 % | 22.9 |
| this branch | Air, battery, Low Power Mode | 1.89 / 17.3 / 41.6 | 16.9 / 36.8 / 49.8 | 7.3 % | 52.9 |

| main loop, headless | host / power | fps | frame p50 / p95 / p99 ms | frames > 33 ms | cycles/s (500 = set speed) | pacing-limited frames |
|---|---|---|---|---|---|---|
| 1.0.849 | mini | 32.1 | 27 / 61 / 97 | 34.5 % | 310 | - |
| this branch | mini | 57.6 | 17 / 30 / 35 | 1.5 % | 470 | 125 of 928 |
| 1.0.849 | Air, battery, LPM | 18.6 | 47 / 116 / 172 | 77.0 % | 183 | - |
| this branch | Air, battery, LPM | 42.3 | 23 / 37 / 48 | 7.6 % | 354 | 433 of 926 |

Not measured: a rendered 1440x900 ScaleHD window (mini or Air) and the Air on AC (it stayed on
battery in Low Power Mode; its power settings were not touched). Remaining spikes: the 100-cycle
build pass (`ai.build.evaluate`, worst 20-29 ms: heavy-factory `findPlaceLocation` falling back to
whole-map passes, `factoryClearance`'s O(map) clearance per danger refresh) and the city growth
phase (worst 16-18 ms).

### Third pass (2026-10-24, agent FpsFix3, branch `perf/fps-round3`): build pass and city growth

Profiled per build pass in thread CPU (temporary scope breakdown, not committed) on the stepped
probe, 15,000 cycles from cycle 73,528. 1.0.852's worst passes (18-22 ms) were 6-8
`findPlaceLocation` calls of ~2 ms each, nearly all of it the whole-map pass 1/2 origin loop
(147,456 origins through the build-range gate) for items with no site near the base centre
(zones, Palace, factories), plus `factoryClearance` (1.3 ms), the rock survey (5 ms), the anchor
field (1.2 ms) and the city service search. City growth's worst 16-18 ms was the AI decision
log's `city_map_snapshot` every 96 city days (14 ms, telemetry only); without it growth was
1.5 ms row-major tile walk + 3 ms traffic BFS. Changes (all exact; 21 `PROBE_DIGEST`s over
15,000 cycles identical to 1.0.852; protocol 94, save 9871 unchanged):

- Placement search: origins are tested in blocks of 32 along a column; a block whose union of
  footprints has no anchor within BUILDRANGE is skipped at once (same skip count).
- `DuneCity::chessboardPasses` (SpatialFields.h): the two raster passes with each row taking its
  neighbour row first (vectorised); used by `enemyClearanceInto` (buffer reused) and the rock
  survey's enemy field. `RockExpansionPolicy::choose` labels formations and runs the drive BFS
  without per-visit div/mod lambdas (existing random equivalence test against the legacy survey).
- City service search: candidate windows as a 2D difference array instead of 47x47 stamps into
  a `vector<bool>`; row-major scan without per-cell div/mod.
- `ObjectManager::getObject`: a dense id-indexed table beside the ordered map (ids below 2^22;
  the map still serves iteration, save order and larger ids).
- City growth: origins from `structureList` sorted by tile (`forEachStructureOrigin`, as the
  effects scan); `TrafficSimulation` memoises road and neighbour-role tests per instance (one
  growth pass or one AI road plan, no map change in between).
- Telemetry only: `Record::quote` copies plain runs, `Record::add` builds no temporaries, the
  map snapshot writes digits in place and reads tiles column by column.

Stepped probe, mini (other agents' load), thread CPU per cycle:

| | cpu mean / p99 / max ms | 10-cycle frame p50 / p95 / p99 ms | frames > 33 ms (of 1,500) | build pass total / worst | growth worst |
|---|---|---|---|---|---|
| 1.0.852 (load 6.9, cal 6.6 ms) | 1.30 / 14.3 / 24.9 | 11.9 / 27.0 / 35.6 | 29 | 7.3 s / 27 ms | 16 ms |
| perf/fps-round3 (load 10.5, cal 8.0 ms) | 1.17 / 8.4 / 21.6 | 11.9 / 22.5 / 29.3 | 8 | 3.5 s / 22 ms | 9 ms |

Worst single build pass in thread CPU: 18-22 ms before, 10-13 ms after (rock survey, service
search, anchor field and the heavy factory's pass 0 remain). Frames with two or three houses'
passes stay the p99: the passes are 4-8 cycles apart by the fixed AI schedule, which is not moved
(that would change results).

Rendered 1440x900 window with Stefan's `[Video]` (ScaleHD, Interface Height 600, FrameLimit off,
zoom 0), 2 game minutes at 2 ms, decision log on, alternating pairs on the mini (load 7-14, so
pairs are noisy; `rendered-mini.jsonl`):

| pair | 1.0.852 fps, p50/p95/p99 ms, > 33 ms | perf/fps-round3 |
|---|---|---|
| 1 | 46.5, 22/35/47, 7.2 % | 42.4, 23/32/41, 3.3 % |
| 2 | 107.8, 9/20/25, 0.2 % | 151.7, 6/15/22, 0.1 % |
| 3 | 34.2, 27/46/59, 20.5 % | 38.7, 26/37/45, 8.8 % |
| 4 | 37.4, 25/40/50, 11.1 % | 67.9, 14/25/28, 0.4 % |
| 5 | 70.6, 14/25/31, 0.8 % | 61.6, 18/27/34, 1.1 % |

Stefan's Air (M5), same runs, on battery with Low Power Mode on (its state at 12:05, not
changed; cluster paused by `~/dunecity-runner/PAUSE` during the runs, scratch profile only;
`rendered-air.jsonl`):

| pair | 1.0.852 fps, p50/p95/p99 ms, > 33 ms, cycles/s | perf/fps-round3 |
|---|---|---|
| 1 | 40.7, 22/41/66, 11.9 %, 316 | 48.9, 22/36/50, 6.6 %, 350 |
| 2 | 42.7, 22/38/52, 7.8 %, 346 | 46.4, 22/31/45, 3.4 %, 368 |
| 3 | 42.4, 22/37/49, 7.3 %, 349 | 47.1, 21/30/37, 2.5 %, 395 |

The Air on AC was not measured: it went onto battery before the approved window.

perf_regression: 8-house `cpu_p99` budget 3.0 -> 1.6 and `frame_cpu_p99` 9.0 -> 5.5 (x calibration).
1.0.852 measured 2.2-2.9 / 5.3-7.9 on the loaded mini and 1.807 / 4.575 on the Pro (cluster job
20261024T111917-ctest-718d52; main loop 4.04, p50/p95/p99 20/31/36 ms), so the cpu_p99 budget
fails 1.0.852 on both; this branch measured 1.1-1.5 / 3.3-4.7 on the mini. The main-loop budget
(8.0) is unchanged.
