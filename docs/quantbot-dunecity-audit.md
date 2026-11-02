# QuantBot DuneCity Random Skirmish audit (unreleased)

## Reproduction and scope

`tests/ai/dunecity-audit.py` uses the existing `ScenarioMatch` Random Skirmish generator and the real
all-QuantBot simulation. No human controller, fixture cash, artificial reinforcements or changed
combat orders are introduced. `dunecity-audit.inc` is a probe-only, read-only sampler, enabled only
by `BALANCE_DUNECITY_AUDIT`. Ordinary game/probe runs do not pay for these extra measurements.

The initial matrix is 3 map sizes (96/128/160), 3 spice levels (None/Low/Medium), 3 formats
(1v1/2v2/4-house FFA), 3 mirror difficulties (Medium/Hard/Brutal), and 2 fixed seeds
(9101, 148680876): **162 games / 540 AI-house histories**, capped at 30 game minutes.
Each start has a Medium base, the Small 2,500-credit army, no prebuilt city, 3,000 credits,
High rock and tech 9. These are specified starting conditions, not a claim to cover every
Random Skirmish base/city/army choice. Generated terrain retries and skipped start items are
recorded in `games.csv`. Changing starts/length changes game ids, so resume cannot mix those setups.

```sh
dunecity-queue submit --tag dunecity-audit-baseline --cwd "$PWD" --   python3 tests/ai/dunecity-audit.py --results /path/to/audit/baseline --style dunecity-audit-control
python3 tests/ai/dunecity-audit.py --report /path/to/audit/baseline
python3 tests/ai/dunecity-audit.py --compare /path/to/audit/control /path/to/audit/candidate
```

For direct adjacent-difficulty matches use
`--mode ladder --formats 1v1 --sizes 128 160 --minutes 90`. Each seed is played with
both house assignments; the stronger controller remains team 1. A 30-minute mirror-game
draw is not evidence that the difficulty ladder holds or fails.
Use a separate results directory for every config arm:

- `--style dunecity-audit-control` disables CityPowerSizing/ThreatTurrets/CityStarportSupply
  while retaining CityGrowthPower (the combined diagnostic baseline).
- `--style dunecity-audit-power-off` disables **only CityPowerSizing**, retaining the other
  completed fixes. Compare this against no style to isolate the power fix in one native build.
- `--game-options FILE` changes game rules, not QuantBot policy switches.

Styles use the existing per-bot test overrides of normal hashed switches. Source identity
and machine receipts are in the queue job.

Every complete run writes `games.csv`, `houses.csv` and per-minute `samples.csv`; the game row is
written last. Resume skips only complete game ids. Failure logs and failed leased scratch survive
for inspection; successful game profiles/bulk are removed through the existing scratch helpers.
No fresh private build tree is created.

## Automatic detectors

A detector flags a suspicious history for diagnosis; it does **not** establish that an AI decision
was wrong. In particular, resource scarcity, deliberate difficulty caps and losing a fair fight
can produce flags. Rates are hit houses / exposed houses, not percentages of minute samples.
All interval lengths use engine cycles (3,750 per minute), so duplicate final samples cannot
satisfy a persistence threshold. Every flag has a first-minute field and inspectable raw samples.

| Detector | Condition and exposure |
|---|---|
| `economy_stall` | Alive with a yard; cash <1,000 and neither cumulative structures built nor population increases for 4 minutes. All yard-owning houses observed at minute 4+ are exposed. |
| `power_deficit` | Alive with a yard; produced power below demand for 4 minutes. Exposed as above. |
| `no_ground_defence` | At minute 5+, at least 3 enemy mobile ground fighters within 10 tiles of a live own building, and none of those enemies lies in a live gun/rocket turret’s tile range, for 2 minutes. Rocket power rules are respected. Only threatened yard-owning houses are exposed. This uses actual state, not AI omniscience; fog-awareness must be checked separately. |
| `missing_starport`, `missing_radar`, `missing_heavy_factory`, `missing_ix` | No initial/built instance observed by minute 20, while still alive with a yard. The denominator excludes houses that lost their yard first. Tech limits/prerequisites and affordability require manual diagnosis. |
| `idle_under_attack` | 3+ own ground fighters but none moving or targeting a live enemy for 2 minutes, while 3+ nearby enemies coincide with lost structures or falling base HP. New construction/repairs can mask HP damage; this is intentionally a conservative detector. |
| `unused_credits` | At minute 5+, cash >=5,000, empty production queues, no upgrades, no new structures/population for 4 minutes; alive with a yard. |
| `bankrupt_queue_empty` | Cash <100, empty production queues and no upgrades for 2 minutes; alive with a yard. |
| `early_no_fight` | Eliminated before minute 10 with <500 credits of confirmed kill value. All observed houses are exposed. |
| `zero_harvesters` | At minute 5+, alive with a yard and spice still on the map, but no harvester for 4 minutes. A profitable city may deliberately not harvest; not automatically a bug. |

Combat values, turret and prerequisite counts, yard upgrades, queue fronts/holds, population/demand,
power, cumulative building/loss counts, city tax and nearby enemy values accompany every minute.
Use AI decision telemetry in a targeted `run-campaign-balance.py` rerun for a flagged seed to establish
why a spending/ordering rule fired. The matrix disables full decision telemetry to keep capture bounded.

## Comparison rules

Same game/house ids are paired. Reports include each detector's rate, placement score/wins by
difficulty, game length and loop timing. Placement intervals resample **game means**, never pretend
that opposing houses in one game are independent. Mixed-host loop timings are diagnostic only:
AI building-placement CPU and frame/FPS gates come from the mandatory performance tests, not these
noisy numbers. No-formation remains the control; saboteur orders and human-order leases are untouched.

## Root cause: a reserve top-up spent the seed town's working capital

Diagnostic source `9db1c94e15e6`, game
`matrix/medium-a2-c0/96/none/1v1/medium/30m/9101`, Harkonnen:

- At cycle 25 the actual `city_generator_choice` event has `need=40`, `cash=2993`,
  `wind_needed=1`, `wind_site=1`, `nuclear_site=1`, `power_shortage=0`.
- Final yard vetting nevertheless selects item 25 (Nuclear), and `production_order`
  accepts its quoted 2,000-credit price. One 300-credit/100-power Windtrap could cover
  the reserve increment. By minute 1 the base has 2,300 power for 242 demand.
- At minute 5 it has 4,600 population, 38 credits and an empty queue. Population and
  cumulative structures built then stay unchanged through minute 15, despite positive
  demand and no nearby enemy/contact losses; cash rises only to 481. The other house
  is similarly flat at 4,500 population from minutes 4–18, earning about 26 credits/min.
- The first observed 32 minute-one zero-spice house samples (Medium/Hard/Brutal) all
  show the same 300→2,300 power purchase. This is a recurrence, not a one-frame empty queue.
- At minute 8 the capital telemetry reports annual-half-rate tax forecast 105/min,
  upkeep 60/min, and 172 cash. The demanded Industrial lot costs 108 including its power
  allowance, but crime-prevention saving owns the 500-credit civic budget. Overspending
  on the reactor left too little working cash to get this seed economy moving promptly.

The cause is the common `vetYardChoice` conversion: after the first windtrap, **any**
reactor with a legal site and affordable sticker price replaces a proposed Windtrap,
regardless of the useful increment or operating reserve. The existing tested
`preferNuclearPower` policy was no longer called by production code. This is distinct
from the Harkonnen save's missing-growth-power budget (CityGrowthPower): the audited
cities had ample power, not a persistent deficit.

`[Force Concentration] CityPowerSizing=1` restores that existing cost/load/space/cash
comparison in the common final choice, with at least 1,000 credits (or the larger live
city/seed reserve) protected when judging nuclear affordability. It uses the already
cached 2×2 footprint survey; reaching its roomy cap is not misread as an exact limit of
four generator sites. No new placement search, allocation or saved AI state is added.
New proactive reactor plans must also justify useful capacity before asking for installment
funding; positive income alone does not justify a 2,000-power project for a one-windtrap gap.
The demanded-growth candidate applies the same choice **before** quoting/reserving capital,
so a selected Windtrap holds its own price plus foundation, not the reactor's budget.
Existing queued/in-flight reactors are untouched. Useful large loads, genuinely cramped
sites, prosperous cities, affordable blackout recovery and justified funded installment
projects retain nuclear. Vanilla selection is untouched. Switch OFF reproduces the prior choice for same-build A/B;
it is loaded, saved and included in the normal config hash. Shared unreleased protocol
111 already covers deterministic AI changes in this release batch.

The no-spice engine fixture exercises the real final yard vetter with legal live sites:
off reproduces the oversized first reactor; on selects wind for the 40-power top-up,
and preserves rich-city, seven-windtrap-cost, blackout, funded-installment and vanilla
choices. Additional checks reject an oversized new installment plan and assert that
`capitalCandidates[capitalChoice]` and `capitalReserve` contain the selected Windtrap's
full capital cost, not just that the final queue happens to contain wind. The config test
covers default/override/save/reload/hash. Fixtures are separate from the ordinary-game evidence.

Evidence: `outputs/dunecity-audit-20261103/power-sizing-evidence/` contains the full
compressed decision stream, original setup/summary/run log and selected event extract.
The diagnostic matrix receipt is
`20261103T020642-dunecity-audit-diagnostic-control-320e2b`; raw minute/house/game tables
are under `outputs/dunecity-audit-20261103/diagnostic-control/`.

## Completed diagnostic matrix

Source `9db1c94e15e6`, both seeds, all 81 cells: **162/162 games, 540 house histories**.
Mac mini ran 85 games; Windows PC ran 77. Pro was unavailable and Air on battery.
There were 144 time limits and 18 decided games; mean duration 29.63 game minutes.
This is the diagnostic pre-sizing/pre-turret/pre-supplier baseline, not a completed
same-build effect estimate for the final combined release.

Rates below are affected houses / exposed houses. Columns use the detector definitions above.

| Difficulty | Spice | Economy stall | Power deficit | No ground cover | No Starport | No Radar | No Heavy | No IX | Idle defence | Hoarding | Bankrupt/empty | Early/no fight | No harvester |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Medium | None | 25/60 | 0/60 | 2/33 | 56/60 | 0/60 | 0/60 | 60/60 | 0/15 | 0/60 | 46/60 | 0/60 | 0/0 |
| Medium | Low | 0/60 | 1/60 | 6/47 | 7/60 | 0/60 | 0/60 | 39/60 | 0/35 | 0/60 | 2/60 | 0/60 | 0/60 |
| Medium | Medium | 0/60 | 0/60 | 4/56 | 5/60 | 0/60 | 0/60 | 30/60 | 0/46 | 0/60 | 0/60 | 0/60 | 0/60 |
| Hard | None | 14/52 | 0/52 | 4/38 | 40/47 | 0/47 | 0/47 | 47/47 | 0/29 | 0/48 | 45/60 | 0/60 | 0/0 |
| Hard | Low | 1/60 | 0/60 | 8/53 | 12/60 | 0/60 | 0/60 | 41/60 | 0/38 | 0/60 | 4/60 | 0/60 | 0/60 |
| Hard | Medium | 0/60 | 0/60 | 8/57 | 3/60 | 0/60 | 0/60 | 32/60 | 0/49 | 0/60 | 0/60 | 0/60 | 0/60 |
| Brutal | None | 14/52 | 0/52 | 4/38 | 40/47 | 0/47 | 0/47 | 47/47 | 0/29 | 0/48 | 45/60 | 0/60 | 0/0 |
| Brutal | Low | 2/59 | 0/59 | 3/45 | 9/50 | 0/50 | 0/50 | 28/50 | 0/28 | 0/57 | 6/60 | 0/60 | 0/59 |
| Brutal | Medium | 3/59 | 0/59 | 4/53 | 8/57 | 0/57 | 0/57 | 24/57 | 0/42 | 0/58 | 4/60 | 0/60 | 0/58 |
| All | All | 59/522 | 1/522 | 43/420 | 180/501 | 0/501 | 0/501 | 348/501 | 0/311 | 0/511 | 152/540 | 0/540 | 0/357 |

**Interpretation:** 53 of 59 economy flags are zero-spice houses. Most match the oversized
early-capital pattern above. The few spice-map flags include legitimate harvester-fleet
ramp-up (e.g. Brutal 128/Low/1v1, seed9101, 4→11 harvesters while population stays340)
and combat collapse; these are not all 59 confirmed bugs. The single persistent power
flag is Medium 128/Low/2v2, seed9101, Atreides: 108 structures already lost at minute24,
all generation lost at25, 135 structures lost at26. It is not another peaceful growth-power
deadlock. No blanket repair/harvester/idle-army policy change was made from these signals.

Examples: the 96/None/1v1/Medium seed9101 economy stall starts at minutes10/9
(Harkonnen/Atreides); Medium 96/None/2v2 seed148680876 Harkonnen has no local turret
cover at25; Hard 96/None/FFA seed9101 Atreides has the same cover flag at14.
Starport/IX delays overlap the supplier/ground-cover work owned by RocketTurrets, rather
than warranting a second competing spending patch. `diagnostic-summary.json` preserves
every detector's grouped numerator/denominator and example game ids.

## Verification boundary

The completed diagnostic matrix and separate ordinary-game generator telemetry are measured.
Detector invariant checks cover persistence/reset, duplicate final samples, spice exclusion,
built-tech history and local rather than house-wide turret coverage. The combined native
build at `5d4d7c72` and the original seven-case sizing fixture passed
(`quantbot_custom_attack` 6.64 s; `quantbot_no_spice_city` 27.56 s).

The final planner/candidate-reservation fixture result is now established on combined
`ff111654` (power code `ebd8ba42`, turret/supplier code `d76d5e62`): RocketTurrets reports
native build PASS, `quantbot_no_spice_city` **28.72 s PASS** and `quantbot_custom_attack`
**6.68 s PASS**. This includes the selected-generator/full-reservation assertions.
The `820bae19` local no-spice fixture timed out at 180 s after `CITY_GROWTH_POWER_PASS`.
Its preceding market step left a ten-Quad Starport batch: using `currentProducedItem`
to cancel that batch can make no progress, because Starport queue items are not its
ordinary production slot. Fixture cleanup now cancels the real queue-front item and
asserts progress at all four callsites. The pure nuclear-capacity rejection also runs
before the optional reactor site search, avoiding an expensive sweep for a rejected plan.
At `4dc09268` the corrected fixture terminated in 23 s and then exposed an input error
in the new quote test: its builder list omitted the real Starport bargain, so a legitimate
routine core-cover candidate (score2600) beat the growth-power candidate (score16).
The quote case now retains that real market, with the same reset shipment/70-credit Quad
setup as the preceding growth-power fixture, so it exercises the actual military-to-growth
allocation. Selected-generator and full-capital-reservation assertions remain unchanged.
These corrections neither waived assertions nor extended the timeout.

Final same-build on/off detector and win-rate tables, 90-minute adjacent-difficulty ladder,
all mandatory AI regressions and performance/FPS gates are **not yet established** by this
diagnostic matrix. The exact isolated control is `dunecity-audit-power-off`; do not
present the 30-minute mirror time limits as ladder or performance evidence.

The final eight-gate job is `20261103T034510-rocket-turrets-final-ad0693` (RocketTurrets).
The three earlier `820bae19` comparison matrix jobs were explicitly cancelled when
that candidate was withdrawn; they are not final effect estimates. Main owns relaunching
the same-source combined control/default/isolated-power-off arms from the qualified final
source and recording the long ladder and remaining gate outcomes.

