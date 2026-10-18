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
