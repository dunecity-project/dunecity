# Route timing aligned with Dune Dynasty (1.0.758)

DuneCity, Vanilla and Dune2R now reproduce Dynasty's ground tile and turn timing.
Across 260 route groups the largest mean travel-time difference is **0.593%**,
within the 2% acceptance target. All three modes produce identical CSVs.
Comparison uses normal speed: 16 ms DuneCity cycles and 60 Hz Dynasty ticks.

## Measured eight-tile sand routes

Healthy units, initially aligned; harvester empty. Times include tile pauses.

| Unit | Dynasty seconds | 1.0.758 seconds | Difference |
|---|---:|---:|---:|
| Tank | 11.917 | 11.931 | +0.12% |
| Trike | 7.917 | 7.931 | +0.18% |
| Raider Trike | 4.017 | 4.031 | +0.35% |
| Quad | 7.917 | 7.931 | +0.18% |
| Harvester | 13.883 | 13.900 | +0.12% |
| Soldier | 35.917 | 35.931 | +0.04% |
| Trooper | 18.017 | 18.031 | +0.08% |
| Devastator | 26.067 | 26.081 | +0.06% |
| Launcher | 8.117 | 8.131 | +0.18% |
| Siege Tank | 13.967 | 13.981 | +0.10% |
| MCV | 13.883 | 13.900 | +0.12% |
| Deviator | 8.117 | 8.131 | +0.18% |
| Sonic Tank | 8.117 | 8.131 | +0.18% |
| Saboteur | 7.917 | 7.931 | +0.18% |

The previous 1.0.757 comparison found raiders and launcher-family units 11–12%
slower, and soldiers/saboteurs 7–11% faster. These were scheduling and arrival
errors; this change does not compensate by altering each unit's INI speed.
The initial 90-degree tank-turn penalty now averages approximately 1.25 seconds,
matching Dynasty (previously 0.80 seconds).

## Implementation

- The route loop retries every 15 Dynasty ticks, including its two delayed
  script updates, rather than the Legacy two-stage navigation poll.
- Movement and rotation run before routing, allowing arrival and the next step
  to occur on the same script tick, as in Dynasty.
- Each step's duration derives from the original speed-accumulator reset,
  quantized movement impulses, direction-table rounding and strict arrival
  threshold. Damage, entered terrain and harvester cargo are applied first.
- Body turning uses Dynasty's four-tick cadence, clamps to the exact requested
  heading, and completes before travel starts. Tank and generic-unit paths
  both use this rule.
- Rendering positions interpolate between tile endpoints over that duration.
  Infantry slots and rock wobble remain supported. Intermediate positions and
  collision timing are not a bit-for-bit port of Dynasty's movement impulses.
- Configured MaxSpeed/TurnSpeed remain the existing Dynasty conversions. Custom
  caps scale travel duration, city roads retain their movement multiplier, and
  mod-only units retain their explicit Legacy behavior.
- Save version 9844 stores route phase, step deadlines, starting position and
  endpoint. Earlier saves finish their existing step before adopting the new
  scheduler. Network protocol 16 prevents incompatible simulations connecting.
  Existing saves still retain their serialized object-data caps.

Sandworms keep their existing continuous movement behavior and previous nominal
rate correction. Carryall, ornithopter and frigate flight handling is unchanged
by this ground-timing change; their earlier nominal-rate fixes remain in place.
Pathfinding, combat AI and Dynasty's optional unquantized-speed setting are not
ported by this change.

## Drawing chained steps

Stefan's `skipping.dls` (1.0.850, 2 ms game speed): some units "jump from one
tile to the next instead of showing the animations". Per-cycle and per-frame
position traces of the save (`tests/performance/game-perf.py --motion-csv`,
`docs/game-performance.md`, "Unit motion trace") showed the cause in the Dynasty
step scheduler:

- **Chained steps keep `justStoppedMoving`.** A step that ends on the cycle a
  route tick falls due is followed by the next step on the same cycle (the
  arrival-and-next-step case above). Only `move()`'s idle branch clears the
  flag, so the new step keeps it and `GroundUnit::checkPos` holds `realX/realY`
  on the tile centre on every cycle of that step: drawn as simulated, the unit
  stands still for half a step, jumps a whole tile when its location changes,
  and stands still again. It hits units whose step is a whole number of route
  periods and whose route phase is a multiple of 3; in the save, Tanks driving
  diagonally over rock (25 movement ticks) did it on every tile.
- **Missed route ticks start steps in the past** (seen once in the save, a
  harvester): `GroundUnit::navigate` skips a unit awaiting a carryall, so its
  route tick falls behind; when the wait ends its steps begin at past ticks and
  finish at once, a tile per cycle until the ticks catch up. Not changed here.

**Fix (drawing only, protocol 88):** `Game::drawScreen` holds a
`UnitBase::DrawnStepScope`. For every Dynasty ground unit on a step with the
stale flag it sets `realX/realY` to the step's own interpolation for the last
cycle run (`moveDynastyStep`'s value before `checkPos` snapped it, rock bumps
included) and restores the simulated values when drawing ends. No cycle runs
inside the scope, so the simulation, digests and every game outcome are those
of 1.0.850.

Fixing the flag in the simulation instead (branch `fix/dynasty-step-stale-stop`,
protocol 92, also resyncing missed route ticks) changes positions, targeting
distances and crushing during chained steps, and with them game trajectories:
`quantbot_all_against_atreides` 4/6 (1.0.850: 6/6), `quantbot_ffa_army_deploys`
and `quantbot_strategy` failed on it; the route-tick part alone also flips the
same two All against Atreides seeds. Left for Stefan to decide.

`unit_motion_probe` (`tests/units/run-unit-motion-probe.py`) shuttles ground
units over open sand through the real main loop at 2 ms game speed with frame
pacing (1-10 cycles per frame) and checks the drawn positions every cycle and
every rendered frame.

## Verification

The reference uses unmodified Dynasty 4469449c core movement, map, pathfinding
and script VM with the original UNIT.EMC from the installed DUNE.PAK. Only
presentation/audio/network callbacks and normal-speed timer plumbing are stubbed.
Script SHA256: da9d00b178cd4a96034698c0fcc3435a9428822dbee1530089e76fdb527c2583.
No original game data is committed.

- 15,600 reference routes under ASan/UBSan, repeated for produced and scenario
  units with identical results (31,200 runs).
- 32,500 real Game::updateGameState routes per mode (97,500 total), with 125
  starting phases covering the two-second common clock period; reference uses
  60 starting phases. Results are identical across the three modes.
- Fourteen unit types; 8/16-tile sand routes; diagonal sand routes; rock, dunes,
  spice, concrete and infantry mountain routes; damaged units; loaded harvesters;
  aligned/90-degree initial headings; and two four-tile legs with a 90-degree
  replacement order at the corner. These are controlled empty-map cases, not
  every combination of terrain, damage, cargo and congestion.
- Exact save plus observer continuation: tank, soldier and harvester saved while
  turning, moving and at a tile stop, then compared frame-for-frame for 160
  updates after each reload in every mode (4,320 checked frames).
- Custom speed caps, city roads, zero-speed tile reservation, replacement orders
  during a tile step, real carryall pickup/drop-off and repair runs, and repeated
  damaging ornithopter attack passes all pass.
- CTest unit suite, command regressions and menu probe pass. The command probe's
  reused test profile once retained a stale Workshop selection; an isolated
  fresh test profile passed. User profiles were not involved.

## Reproduce

```sh
python3 scripts/check-build-deps.py build
python3 tests/units/compare-dynasty-routes.py \
  --dynasty-dir ../dunedynasty \
  --output-dir ../outputs/route-speed-alignment/repro
ctest --test-dir build --output-on-failure \
  -R 'dunelegacy_tests|carryall_flight_probe|unit_speed_probe|unit_route_continuation_probe'
```

The comparison fails if a route group's mean differs by more than 2%, if the
scenario/produced reference results differ, or if the three local modes differ.
Requires the macOS Ninja game build and original locally installed game data.

Measured artifacts: `../outputs/route-speed-alignment/extended3/` and
`../outputs/route-speed-alignment/continuation/`; final continuation tests also
live in `build/unit-route-continuation-probe/`.

The MBA's saved speed preferences at audit time were Vanilla 4 ms, DuneCity
18 ms, Dune2R defaults 16 ms. These preferences remain unchanged; select the same
game speed when comparing modes. All timings above use normal 16 ms speed.
