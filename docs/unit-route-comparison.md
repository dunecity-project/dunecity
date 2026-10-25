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

## Chained steps

Stefan's `skipping.dls` (1.0.850, 2 ms game speed): some units "jump from one
tile to the next instead of showing the animations". Per-cycle and per-frame
position traces of the save (`tests/performance/game-perf.py --motion-csv`,
`docs/game-performance.md`, "Unit motion trace") showed two faults in the
Dynasty step scheduler (1.0.760-1.0.852):

- **Chained steps kept `justStoppedMoving`.** A step that ends on the cycle a
  route tick falls due is followed by the next step on the same cycle (the
  arrival-and-next-step case above). Only `move()`'s idle branch cleared the
  flag, so the new step kept it and `GroundUnit::checkPos` held `realX/realY`
  on the tile centre on every cycle of that step: the unit stood still for half
  a step, jumped a whole tile when its location changed, and stood still again.
  A tracked unit also squashed its tile every cycle instead of on arrival, a
  tank's turret skipped facing its route, infantry did not animate. It hits
  units whose step is a whole number of route periods and whose route phase is
  a multiple of 3: Tanks driving diagonally over rock (25 movement ticks), and
  Launcher, Deviator and Sonic Tank on sand (20).
- **Missed route ticks started steps in the past**: `GroundUnit::navigate`
  skips a unit awaiting a carryall, so its route tick fell behind; when the
  wait ended without a lift its steps began at past ticks and finished at once,
  a tile per cycle until the ticks caught up (24 tiles in 60 cycles after a
  400-cycle wait).

**Fix (simulation, protocol 103 / save 9874):** the hashed and persisted
`[Game Options] Continuous Ground Steps` rule controls both scheduler corrections.
It remains **off by default and unaccepted** pending same-build paired acceptance.
With the rule on, `UnitBase::moveDynastyStep` clears the flag
on every cycle of a step. The stop of the previous step was handled on the cycle
it ended, exactly as for a step that does not chain (where `move()`'s idle
branch clears it the next cycle), so nothing that reacts to an arrival (tile
squash, spice bloom, refinery and repair-yard entry, infantry capture) loses
its cycle. Every step starts in `UnitBase::navigate`, which every ground unit
(formation members, carryall drops, harvesters, infantry) goes through, and
every Dynasty ground step moves through `moveDynastyStep`
(`UnitBase::move`, `InfantryBase::move`). `navigate` also resyncs a route tick
that is more than one tick old to its cadence's next tick. Step timing is
untouched: the 12-tile routes of 14 unit types, straight and diagonal, sand and
rock, 16 legs each, take exactly the same cycles (`tests/units/run-movement-effects.py`,
phase `speed`). With the rule on, the simulated positions are what is drawn.
With it off, the 1.0.852 simulation and render-only `UnitBase::DrawnStepScope`
remain intact. MOD8 and earlier settings streams keep it off; MOD9 stores it.

`unit_motion_probe` (`tests/units/run-unit-motion-probe.py`) shuttles ground
units over open sand through the real main loop at 2 ms game speed with frame
pacing (1-10 cycles per frame) and checks the simulated (drawn) positions every
cycle and every rendered frame, that no moving unit keeps `justStoppedMoving`
mid-step, and that a tank whose carryall never came drives on at its speed.

### Same-build comparison

Use only shared `dunecity-feature-batch/build-int`, with separate results directories
for the two arms. Do not rebuild a private tree or relax the existing AAA assertions.

- Matrix off: omit `--game-options`; each cell keeps its authored options.
- Matrix on: append `--game-options tests/ai/continuous-ground-steps-on.ini`.
  The overlay is merged after the cell's options, including the mandatory AAA
  concrete, carryall and worker/unit-limit settings; campaign cells use it too.
- Random-skirmish FFA off: retain
  `--game-options tests/performance/data/stefan-game-options.ini`.
- The same FFA on: use
  `--game-options tests/ai/continuous-ground-steps-ffa-on.ini`, the frozen
  historical FFA setup with only this rule added. `vanilla-match.py` replaces
  its options file rather than layering it, unlike the matrix overlay.

Relative paths resolve inside each immutable cluster source checkout. These are
comparison inputs, not evidence that the movement fix meets AI acceptance.

### Movement-fix AI evidence (2026-10-24)

Evidence directory: `~/Documents/projects/dunecity-results/unit-movement/`. Control
is 13c29c5d: staging 1.0.852 (6fd861ae) plus measurement-only harness additions.
Candidate is 9f4f492b / 666c53bf: the movement correction and test/docs changes,
without an AI change. Games were cluster runs, not shortened fixtures. Existing
jobs/results were collected during recovery; none were resubmitted.

#### All against Atreides: 60 paired seeds

The original 24-game jobs (`20261024T100453-um-aaa-before-7ba517`,
`20261024T101313-um-aaa-after-5baf6d`) were extended to 60 by jobs
`20261024T111817-um-aaa-before60-aabee5` and
`20261024T111809-um-aaa-after60-093f8c`. The latter jobs each played only the
36 missing seeds. Fixture, options, controller levels, and 40-minute win
threshold are unchanged; games finish on victory/defeat or at the 41-minute cap.

| Measure | 1.0.852 | Movement fix | Paired difference, fix - control [95% interval] |
|---|---:|---:|---:|
| Wins within 40 min | 57/60 (95.0%) | 52/60 (86.7%) | -8.3 percentage points [-16.7, 0.0] |
| All wins before game ends | 59/60 | 53/60 | — |
| Median time of wins | 35.4 min | 34.8 min | -0.6 min [-2.0, +0.9] |
| Atreides placement score | +0.967 | +0.854 | -0.113 [-0.237, +0.008] |

`aaa_paired.py aaa-before aaa-after` reads the CSVs and reproduces the full
per-seed table. Rate/median intervals resample the same 60 paired seeds 10,000
times; median times condition on wins in each resample, so faster winning games
do not compensate for extra time-outs. Placement uses `tests/ai/match_score.py`
(win +1, loss -1, time-out -0.25; 2,000 paired bootstrap resamples). Its verdict
is **level**, not evidence of noninferiority or improvement: the interval still
allows a material regression. Six seeds win in 40 minutes only on the control
(75102, 85192, 88219, 99318, 109408, 906213928), one only on the fix (77120).

All six mandatory seeds are retained:

| Seed | 1.0.852 | Movement fix |
|---|---:|---:|
| 1756374161 | win 38.0 | win 38.3 |
| 906213928 | win 34.9 | time-out 41.0 |
| 41562270 | win 32.3 | win 34.4 |
| 1841712204 | win 35.4 | win 38.1 |
| 1430976924 | win 31.4 | win 33.9 |
| 1804669015 | win 36.0 | win 33.6 |

The control's three exceptions among 60 show that its six-of-six passing sample
is not a universal guarantee. They do not establish that the candidate's larger
failure count is harmless. A five-of-six plus median-time rule was considered,
but **not adopted**. The mandatory test still requires six wins within 40;
the known 1.0.842 regression (two of six within 40, one loss) remains its
historical counterexample (`quantbot-all-against-atreides.md`). No limit, seed,
or threshold was changed to obtain a passing result.

#### What the saved failing game actually shows

The retained `aaa-local-{before,after}/seed-906213928/profile/ai-decisions/*/events.jsonl`
contains the detailed evidence; no confirmation game was run.

- First differing recorded decision data: minute 9.994, small learned production
  target-share changes; kiting decisions differ by minute 10.014. Per-minute
  army totals first show the divergence at minute 11.
- At minute 20 the enemy army is 69,850 versus the control's 46,410. The first
  `base_scramble` is only at minute 23.35, after that advantage has formed.
- There are 137 scramble **records**, not 137 whole-army recalls. Of them, 69
  send no new responders; the others total 466 responder assignments over the
  game (not necessarily distinct units). `QuantBot::scrambleWholeArmyToDefend`
  also logs when defenders are already committed. The last record is at 33.44.
- Finish waves still dispatch 94 units at 36.20 and 69 at 38.92; neither wave
  has launchers held by defence. Enemy structures (including walls/concrete)
  fall from 931 at minute 34 to 671 at 41. This is ongoing combat, not evidence
  of a persistent defence assignment preventing all further attacks.

Thus the saved evidence does not justify suppressing base defence or changing
the AI to fit seed 906213928. The initiating movement/combat interaction is not
isolated, and unchanged nominal route speed/crushing does not prove unchanged
battle outcomes. Acceptance remains unresolved.

#### Strategy probe: twelve cluster seeds per build

Jobs `20261024T111949-ctest-bdc04e` (control) and
`20261024T111937-ctest-40f2d0` (fix) used temporary CTest registrations, since
the campaign probe is not a schedulable game harness. Each source registers:
the actual `quantbot_strategy`, twelve individual full probes with a single
margin seed, and twelve section-3 margin-only probes. Temporary registrations
were removed from the worktree after snapshotting, not committed.

Seeds: 486409243, 11, 22, 33, 44, 55, 66, 77, 88, 99, 111, 122.

| Mix | Control twelve-seed mean lead margin (bps) | Fix | Control / fix failing three-seed means (of 220) |
|---|---:|---:|---:|
| Artillery | +792 | +934 | 5 / 1 |
| Counter | +976 | +933 | 0 / 0 |
| QuadMass | +568 | +534 | 0 / 0 |
| Specials | +748 | +586 | 0 / 7 |
| TankMass | +1482 | +1350 | 0 / 0 |
| TrikeSwarm | +582 | +533 | 0 / 0 |
| Tuned | +1260 | +1270 | 0 / 0 |

The actual three-seed `quantbot_strategy` passes on both builds (Specials
control +615, fix +268 bps). Across all seven mixes the mean fails in 5/220
control and 8/220 candidate seed triples. These overlapping triples are a
descriptive sensitivity calculation, not 220 independent observations.

The calibration suites are **not green**: control 11/25, fix 14/25 passed.
The twelve single-seed full probes pass 3/12 and 4/12 respectively. Saved
failures distinguish random-coverage assumptions from production evidence:
both fail seeds 11/99 because no house drew Bounded; both fail 22/66 because
Air ornithopters become producible after learning starts; control seed 77 and
fix seed 44 build no more ornithopters than Learned; fix seed 122 builds no
more infantry than Learned. Four remaining control failures (44,55,111,122)
and two fix failures (486409243,77) are the deliberately single-seed margins.
Margin-only probes fail on five control and three fix seeds. No additional
check was loosened. `strategy_cluster.py before=... after=...` reproduces
per-seed margins and causes; `strategy-cluster-{before,after}-logs` preserves
the available mini `run.log` files before cluster build trees are reused.

#### Twelve-seed four-house FFA

Jobs `20261024T100455-um-ffa-before-f9ba32` /
`20261024T101313-um-ffa-after-b029aa`: seeds 9101–9112, rs160-dunecity,
four Brutal QuantBots, 45-minute games.

| Measure | 1.0.852 | Movement fix |
|---|---:|---:|
| Decided games | 2/12 | 4/12 |
| Mean end time | 43.6 min | 42.1 min |
| Eliminated houses | 14 | 17 |
| Mean waves per game | 27.3 | 26.0 |
| Mean of each game's house placement scores | -0.167 | -0.141 |

`ffa_paired.py ffa-before ffa-after`: paired game-mean placement difference
+0.026 [-0.052, +0.099], **level**. This is self-play: the aggregate score
measures draw penalties/decisiveness, not candidate-vs-control playing strength.
It uses `match_score` with the same final-elimination fallback as
`vanilla-match.placings` (a decided game's final defeated house may have no
elimination line). Ignoring that fallback would incorrectly score a decisive
game as a draw.

#### Pending acceptance

The saved full suite `20261024T104919-ctest-c7a6bb` is **108/109 passed**;
only `quantbot_all_against_atreides` fails, on both machines. It is not a
passing acceptance receipt.

The preserved long-game jobs have now completed, **720/720 games per arm**:
`20261024T112050-um-ladder-before-0983dd` /
`20261024T112050-um-ladder-after-fb033c`. Each pair has 144 games per arm,
72 paired seed/map setups: eighteen seeds, both house assignments, 128/160
vanilla/DuneCity and unchanged 75/90-minute caps. `ladder_paired.py
ladder-before ladder-after` reproduces the paired seed/map bootstrap:

| Stronger v weaker | Control stronger-house score [95% interval] | Fix score [95% interval] | Paired difference [95% interval] |
|---|---|---|---|
| Brutal v Hard | +0.604 [+0.457, +0.736] | +0.571 [+0.424, +0.705] | -0.033 [-0.122, +0.057] |
| Hard v Medium | +0.512 [+0.387, +0.627] | +0.469 [+0.333, +0.599] | -0.043 [-0.123, +0.042] |
| Medium v Easy | +0.929 [+0.875, +0.974] | +0.948 [+0.896, +0.991] | +0.019 [-0.017, +0.061] |
| Brutal v Medium | +0.839 [+0.733, +0.931] | +0.866 [+0.778, +0.944] | +0.028 [-0.056, +0.122] |
| Hard v Easy | +1.000 [+1.000, +1.000] | +0.983 [+0.957, +1.000] | -0.017 [-0.043, +0.000] |

The aggregate difficulty order is positive in both arms. All paired differences
are level by the existing classifier; that is **not noninferiority proof** and
does not clear the mandatory AAA failure above.

The parent repeated the unchanged six mandatory AAA seeds in the integrated
game source, including the Palace/IX correctness fix. Off job
`20261024T214127-integrated-aaa-off-4b4f86` at `e286003d100a` wins **6/6**.
The first on job ran no games: writing its overlay inside the run directory
made the launcher fail with `FileExistsError`. Harness-only fix `45417259`
writes that file beside the directory. A real shared-build smoke played the
first seed; `20261024T221200-integrated-aaa-on-fixed-7d5372` skipped it and
played only the other five. The corrected on cohort wins **5/6**: seed
`906213928` again times out at 41.00 minutes. The five wins are 38.34, 34.36,
38.11, 33.90 and 33.64 minutes in the mandatory seed order above.

Only documentation and the overlay launcher differ between these integrated
snapshots; no production game code changed. Do not pool these six seeds with
the earlier sixty-seed cohort. The current default-off combined suite at
`e286003d100a` passed **118/118** (`20261024T213618-combined-vuk-final-3ee6bc`),
including the explicit on-rule motion probe. That does **not** clear this
separate AI gate. Continuous Ground Steps remains off and unaccepted; no seed,
deadline or check was weakened.

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
