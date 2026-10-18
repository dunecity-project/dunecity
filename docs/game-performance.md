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
`DUNECITY_PATH_WORKERS` unset, decision log per `[General] Diagnostic Logs`).

His Air holds 33 GB of old AI decision logs (`~/Library/Application Support/Dune City/ai-decisions`,
429 sessions); not deleted.

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

## Not done

- Rendering (5-10 ms per frame at 560 units) and the city simulation's 16-phase spikes were not
  changed; neither differs between releases.
- Spreading houses' 100-cycle build passes so two never land in one 10-cycle frame (houses 1 and 3
  are 8 cycles apart) would change AI timing, i.e. game results; not done.
- No duel-report perf column.
