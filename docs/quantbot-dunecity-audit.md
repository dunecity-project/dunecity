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
- `--style dunecity-audit-opening-capital-off` disables **only CityOpeningCapital**.
- `--style dunecity-audit-storage-off` disables **only StorageCapacity**.
- `--prebuilt-probe FILE` uses an already-built, frozen probe without compiling.
  For a bounded local fallback, copy the committed app/probe bundles outside `build-int`,
  pass the frozen executable here and run at `nice -n 10 --jobs 2`; do not hold an
  `agent-workspace --tests` lease for a long matrix. Keep the source hash/binary SHA256
  receipt with the CSVs. Ordinary games never execute the controlled fixture branches.
- `--attack-events` enables the runner's existing AI telemetry and retains nonempty
  `ground_hunt` dispatches in `waves.csv`. Reports include first dispatch time among
  attackers, attacking/measured houses, dispatches per ten alive-house minutes,
  army value at minutes10/20 and final kill value. A readiness check is not an attack.
  Old telemetry-disabled rows report dispatch timing/frequency as unmeasured, never zero.
  Army checkpoints include eliminated houses as zero; living winners whose game ended
  before a checkpoint are censored. `sortie_minutes` is duration, not an attack timestamp.

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
| `storage_full` | Earned income was refused during at least one observed simulation tick without a simultaneous storage-capacity decrease. All living houses with cycle accounting and the economy ledger enabled are exposed. A destroyed vault is not attributed to failure to buy storage. |

Combat values, turret and prerequisite counts, yard upgrades, queue fronts/holds, population/demand,
power, cumulative building/loss counts, city tax and nearby enemy values accompany every minute.
Use AI decision telemetry in a targeted `run-campaign-balance.py` rerun for a flagged seed to establish
why a spending/ordering rule fired. The matrix disables full decision telemetry to keep capture bounded.

The separate mandatory no-spice-income gate retains both tax>100 and four completed
R/I-lot assertions for surviving construction/recovery-capable houses (CY, MCV,
HeavyFactory or Starport), and requires at least one such survivor. The original
passing pre-sizing source2ccb2894 (CityGrowthPower93d96ea3 plus fixture corrections)
and integrated271 have **identical** Harkonnen early completions1825/3825/4825/5825/
6825 and losses10478/11230/13682/15020: the yard falls at13682, after onlyR2/I1.
Both bank117 tax and spend1619. The old game later eliminated the residual lots at
54363/54967 and exempted that house; the new capture keeps its zones/harvester alive
without a yard. Treating that residual survival as a capable economic opening was
a test-precondition error, not evidence to change the refinery-first policy.
Original passing/failing streams are retained in the audit's regression evidence.

## Comparison rules

Same game/house ids are paired. Reports include each detector's rate, placement score/wins by
difficulty, game length and loop timing. Placement intervals resample **game means**, never pretend
that opposing houses in one game are independent. Mixed-host loop timings are diagnostic only:
AI building-placement CPU and frame/FPS gates come from the mandatory performance tests, not these
noisy numbers. No-formation remains the control; saboteur orders and human-order leases are untouched.

## Storage capacity: income outgrows the first vault (2026-11-03)

`80rock.dls` from Stefan's Air reproduces the reported loss without artificial income or
orders. The saved 160x160 CivWars match has no spice and begins at technology 1. Continuing
from cycle26188 with the frozen pre-change probe, Neutral (house6) stays at **2,005 storage**
(Construction Yard1,000 + Refinery1,005) through minute15. Its cumulative storage loss rises
at minutes10–15: **1, 552, 1,705, 2,474, 2,783, 2,870 credits**. No structure is lost in
that interval. Research2 completes at cycle52825; the first Silo is ordered at56700 and
capacity reaches12,005 by minute16. Raw evidence:
`outputs/silo-capacity-20261103/{80rock-baseline,reproduction-evidence.json}`.

The old rule only considered Silos (unavailable until technology2), and at higher technology
also required a Heavy Factory and the city's complete Heavy/HighTech/Repair core. It checked
80% occupancy without forecasting income or construction lead time. A refinery's vault was
only an opening bootstrap or a side-effect of spice expansion, not an overflow alternative.

The first paired audit exposed a second independent gate at the population limit. In
`matrix/medium-a2-c0/128/low/1v1/medium/30m/9101`, Harkonnen repeatedly selected a Silo at
cycle103025: safe site(110,46),197 legal candidates,666 free rock, forecast net26,848/4min
against active burn1,562/min. The queue accepted nothing because `cityAdmitsStructure`
counts a Silo's industrial workers against the already-full40,000 population ceiling.
The StorageCapacity-on rule therefore admits physical-storage buildings at that ceiling;
ordinary R/C/I zoning keeps its existing cap. No redevelopment or placement exception was
needed or added. The retained causal trace is `outputs/silo-capacity-20261103/residual-cause/`.

The default-on, multiplayer-hashed `[Force Concentration] StorageCapacity` policy selects the
best buildable storage capacity per capital cost, normally the Silo, with a refinery-vault
fallback at technology1. It forecasts earned income over the building/foundation lead time
(one-minute minimum, four-minute maximum), subtracts confirmed active production burn and
orders already made in the pass, and leaves starting grants outside physical storage.
Existing queues spend before surplus is banked; theoretical factory throughput is not assumed
to be actual spending. A storage order consumes only its own funded cost, not the rest of the
army/defence/city budget. Live power recovery and air defence retain priority.

All pending storage types are counted, one non-yard vault project is allowed in flight, and
capacity is bounded by the absolute credit ceiling after exempt starting cash. Sites use the
ordinary placement/foundation rules and must have no current weapon danger. The off style
retains the old Silo-only/core/80% decision for same-build comparisons. No save state, combat
orders or human-order leases are added or changed.

The read-only probe observes every simulation cycle. `storage_full_cycles` measures time at
the physical ceiling at cycle boundaries; `storage_overflow_cycles` counts ticks with refused
income; `storage_income_lost` retains whole credits lost on those ticks, excluding ticks where
storage capacity fell. These are distinct: taxes can overflow before construction spends in
the same tick, leaving the minute checkpoint slightly below capacity. The older
`storage_lost` ledger still includes capacity destruction. Reports never label old, unmeasured
CSV histories as zero loss. Divide cycle totals by3,750 for house-minutes.

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

The integrated `growth_installments` regression exposed a legality error in this
comparison: its real controlled case disables Windtraps, but admission still priced
the hypothetical cheap alternative and rejected the only legal reactor at1,200 cash/
368 forecast. Inventory now caches actual Windtrap buildability alongside Nuclear;
sizing compares alternatives only while wind can be built. The fixture itself also
had a stale third, native-map yard: its ObjectData mutation refreshed only its two
new yards, so the native one still advertised disabled wind. The helper now refreshes
every owned producer and asserts the disabled item is absent. The existing installment/
foundation/blackout assertions are retained, and the sizing boundary adds this case.
Normal Wind-enabled games retain the same comparison; no placement search is added.

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

## Root cause exposed by integrated A/B: optional capital spends the power savings

On frozen combined source `27176982874d`, ordinary 96/None/1v1/Medium seed9101 now
correctly buys a300-credit Windtrap. But Harkonnen commits another1,950 credits during
the first minute to an MCV900, three factory upgrades300 each, and a Trike150.
The first upgrade is cycle25, the MCV625, further upgrades1925/2525; all three upgrades
are builder37's ordinary HeavyFactory technology path (`QuantBotBuildProducers.cpp:1107`
in that frozen source). This is not a reactor-sizing failure or a population cap.
Population5400 and built8 stay flat from minutes5–20; around minute8 the next selected
purchase is an unaffordable500-credit crime-prevention station. The old reactor made
the optional purchases unaffordable; fixing one priority merely exposed the next.

At cycle25 the existing four-minute forecast already reports tax0/min, upkeep52/min,
net income-208, projected cash-6070 and a1,000-credit working reserve. The optional
MCV path used cash plus net income, not the existing production runway; producer
upgrades checked only cash. Evidence is in `271-opening-budget/`: original setup,
summary, run log, compressed decision stream and `trace-summary.json`.

`[Force Concentration] CityOpeningCapital=1` is a separate default-on, loaded/saved/
hashed switch. While the city has **no viable spice economy**, optional extra MCVs
and producer upgrades share the existing financial working-capital check: real
spendable cash, the four-minute forecast, already ordered costs, operating reserve
and the next protected purchase. Productive spice-backed expansion keeps its normal
capacity/technology budgets. Research readiness remains unchanged for research;
fleet/crime readiness is not an additional condition on funded tax-city capital.
There is no second budget model, new saved state, RNG use, placement search or allocation.
Founding/lost-yard/actual-rock recovery and required yard upgrades keep their old
budgets. Blackout recovery, CityGrowthPower, ThreatTurrets and Starport supply keep
their existing precedence and cash protection.

The `60bbe8eb` MCV/technology-only candidate still applied this guard to productive
spice economies. A same-frozen-binary three-seed H/M ladder exposed the regression:
OFF Hard7–Medium3+8 time limits, ON Hard6–Medium8+4 (18 games,45-minute cap).
In128/Low/seed9101, Hard's first heavy-factory upgrade moved4.38→12.54min,
three productive MCV orders disappeared, and Medium won23.31min rather than Hard
winning40.29min despite11.3k→22.8k four-minute spice receipts. The tax-dependent
scope restores the existing spice budgets instead of inventing a new economy model.
The MCV deployment fixture also had30k bank/18.3k+ projected cash but negative net
income; research readiness wrongly refused its funded colonist. Financial funding
alone restores that case without changing its assertions.


The initial `8ff2d79a` candidate also deferred discretionary military at negative
projected cash. That version is **withdrawn**, not the final policy. In an ordinary
15-minute 96/None/1v1/Hard game, seed9101, its picked tempo's 2,000-credit standing
threshold was already met by the initial 2,500-credit army: ON produced no combat
units versus OFF's three Quads/two Trikes (900 credits). Saved funds did buy a Starport
and real repair-demand Carryalls, but this did not justify suppressing reinforcements.
The final switch therefore gates **only tax-dependent optional MCVs and producer technology**.
Military eligibility, queue acceptance, saboteur orders, human leases and attack policy
are unchanged. Obsolete broad-ON matrix/ladder jobs were cancelled; their partial
detector/combat results must not be presented as final-policy A/B.

The no-spice fixture retains real queue/upgrade negative controls, funded resumption
with research disabled, live defence, lost-yard/rock recovery and required-yard exceptions.
The manually assembled budget pass explicitly uses the normal no-capital-selection
sentinel (`-1`): inventory alone does not run `chooseCapitalPlan()`. This fixes the
original fixture-only `capitalPending` null-vector SIGSEGV without weakening queue checks.
It additionally asserts that reinforcements above the standing-wave threshold still
queue with negative projected cash. The completed same-binary detector, combat and
adjacent-difficulty comparisons are recorded below.

The independent `CapacityMcvRunway` fixture disables `CityOpeningCapital` in both
arms: otherwise the tax-opening guard correctly rejects the intentionally unfunded
MCV even in the runway-OFF negative control. All real queue, combat-fallthrough,
funded-capacity and recovery assertions remain; the new tax guard is tested separately.

## Verification boundary

### Final coverage and controls

Production policy is `d56d1710`; `495c1b23` is a fixture-only isolation correction.

- Broad diagnostic: **162 games / 540 houses**, sizes96/128/160, spiceNone/Low/Medium,
  1v1/2v2/FFA4, Medium/Hard/Brutal, seeds9101/148680876, 30-game-minute cap.
  No playing human. The frozen271 power/control arms also each completed162/540.
- Final exact-same-binary capital A/B: **72 games / 240 houses per arm**,
  sizes96/128, None/Low, all three formats/difficulties, the same two seeds,
  30-minute cap. Both arms use probe SHA256
  `c2f587e03d1c38c61332600d8e0b12fcceea7614d25ae8d03d675ec0de990885`.
- Final difficulty A/B: **54 games per arm**, size128, all three spice levels,
  B/H, H/M and M/E, both house assignments, seeds9101/148680876/424242,
  45-minute cap. Total final acceptance: **252 games**.
- These use the existing generator/runner, no formation, nice10/jobs2 and frozen
  bundles without a workspace lease. The approved final mirror recheck is the
  96/128,None/Low subset: the earlier full162 on60bbe is historical, not final
  validation of160/Medium after the tax-scope correction. The mixed ladder covers128/Medium.

### Final detector table: matched72 games / 240 houses

Counts are hits/exposed, not automatic proof of a bug. Earlier wins/eliminations
censor long deadlines; joint exposure excludes houses unobservable in either arm.

| Detector | OFF | ON | Jointly exposed OFF→ON |
|---|---:|---:|---:|
| Economy stall | 57/233 | 39/223 | 57→39 /223 |
| Power deficit | 3/233 | 0/223 | 3→0 /223 |
| No local ground cover | 31/193 | 20/171 | 23→20 /156 |
| Missing Starport | 57/202 | 17/156 | 28→15 /151 |
| Missing Radar | 0/202 | 0/156 | 0→0 /151 |
| Missing Heavy Factory | 0/202 | 0/156 | 0→0 /151 |
| Missing IX | 193/202 | 147/156 | 142→142 /151 |
| Idle under attack | 0/129 | 0/109 | 0→0 /84 |
| Unused credits | 0/220 | 0/191 | 0→0 /187 |
| Bankrupt/empty queue | 99/240 | 77/240 | 99→77 /240 |
| Early no-fight elimination | 0/240 | 0/240 | 0→0 /240 |
| Zero harvesters on spice | 1/119 | 1/119 | 1→1 /119 |

The Starport gain is not merely censoring:28→15 on151 jointly20-minute-observed
houses. Local-cover raw31→20 becomes23→20 on joint exposure, so do not attribute
the entire raw drop to improved defence. **IX does not improve on joint exposure:
142→142**. Its existing rule needs Heavy/HiTech/Repair, buildability and >1000
spendable cash; the deadline remains a signal, not a resolved bug.

### All12 detectors by difficulty and spice

Each row has40 matched houses. Each cell is OFF hits/exposed → ON hits/exposed;
not every stratum improves. In particular Medium/None economy flags15→16.

| Difficulty/spice | Economy stall | Power deficit | No ground cover | Missing Starport | Missing Radar | Missing HF |
|---|---:|---:|---:|---:|---:|---:|
| Brutal/Low | 7/39 → 7/39 | 0/39 → 0/39 | 3/34 → 4/34 | 0/28 → 0/28 | 0/28 → 0/28 | 0/28 → 0/28 |
| Brutal/None | 17/37 → 7/34 | 1/37 → 0/34 | 5/28 → 1/20 | 20/30 → 7/12 | 0/30 → 0/12 | 0/30 → 0/12 |
| Hard/Low | 1/40 → 1/40 | 0/40 → 0/40 | 4/37 → 7/36 | 0/38 → 0/38 | 0/38 → 0/38 | 0/38 → 0/38 |
| Hard/None | 17/37 → 7/34 | 1/37 → 0/34 | 5/28 → 1/20 | 20/30 → 7/12 | 0/30 → 0/12 | 0/30 → 0/12 |
| Medium/Low | 0/40 → 1/40 | 0/40 → 0/40 | 8/37 → 6/37 | 0/40 → 0/40 | 0/40 → 0/40 | 0/40 → 0/40 |
| Medium/None | 15/40 → 16/36 | 1/40 → 0/36 | 6/29 → 1/24 | 17/36 → 3/26 | 0/36 → 0/26 | 0/36 → 0/26 |

| Difficulty/spice | Missing IX | Idle under attack | Unused credits | Bankrupt/empty | Early no-fight | Zero harvesters |
|---|---:|---:|---:|---:|---:|---:|
| Brutal/Low | 26/28 → 26/28 | 0/17 → 0/18 | 0/34 → 0/34 | 4/40 → 4/40 | 0/40 → 0/40 | 1/39 → 1/39 |
| Brutal/None | 30/30 → 12/12 | 0/19 → 0/14 | 0/33 → 0/24 | 28/40 → 18/40 | 0/40 → 0/40 | 0/0 → 0/0 |
| Hard/Low | 35/38 → 35/38 | 0/26 → 0/24 | 0/40 → 0/40 | 8/40 → 9/40 | 0/40 → 0/40 | 0/40 → 0/40 |
| Hard/None | 30/30 → 12/12 | 0/19 → 0/14 | 0/33 → 0/24 | 28/40 → 18/40 | 0/40 → 0/40 | 0/0 → 0/0 |
| Medium/Low | 36/40 → 36/40 | 0/29 → 0/27 | 0/40 → 0/40 | 0/40 → 1/40 | 0/40 → 0/40 | 0/40 → 0/40 |
| Medium/None | 36/36 → 26/26 | 0/19 → 0/12 | 0/40 → 0/29 | 31/40 → 27/40 | 0/40 → 0/40 | 0/0 → 0/0 |

### Combat, wins and length

| Metric | OFF | ON |
|---|---:|---:|
| First nonempty ground dispatch, mean min among attackers | 8.421 | 7.074 |
| Dispatches /10 alive-house min | 0.415 | 0.482 |
| Attacking houses /240 | 147 | 154 |
| Total dispatches | 264 | 268 |
| Army value at10min | 1841.525 | 1719.957 |
| Army value at20min | 2817.149 | 2860.180 |
| Mean combat-unit kill value | 11140.833 | 11615.375 |
| Mean observed minutes,30min cap | 27.881 | 25.331 |
| Decisive games /72 | 22 | 26 |
| Winning houses /240 | 30 | 33 |

Mirror-house win fractions describe resolution, not relative AI strength. Army10
is lower(1842→1720,236→232 observations), not hidden: earlier attrition/censoring
and cash allocation change the stock. Army20 is higher(2817→2860,235→222 observations).
End-army+combat-loss stock proxy rises15612→16131; kill value/alive-house-minute
420.8→501.0. Earlier/more frequent dispatches and higher kills do not support a
passivity regression. Dispatch is not projectile-first-shot timing.

### Difficulty ordering: three seeds,18 games/pair/arm

| Pair | OFF strong–weak +limits | ON strong–weak +limits | ON strong wins among decided |
|---|---:|---:|---:|
| Brutal–Hard | 7–4 +7 | 7–3 +8 | 70.0% |
| Hard–Medium | 7–3 +8 | 7–1 +10 | 87.5% |
| Medium–Easy | 10–0 +8 | 9–0 +9 | 100.0% |

Observed ordering is Brutal>Hard>Medium>Easy. Three seeds and many caps do not
statistically establish a universal ranking. H/M was initially borderline, so it
received a targeted third seed; final validation uses all three seeds for every pair
and both arms. The128/Low/9101 case that reversed on60bbe returns to Hard winning
40.29min in both final arms.

### Reproducible examples with both arms exposed

- Economy:128/None/1v1/Medium,seeds148680876 and9101,house0,first OFF flag minute9.
- Starport:128/None/1v1/Brutal or Hard,seed148680876,house1,first OFF flag minute20.
- Local cover:128/None/FFA4/Brutal or Hard,seed148680876,house2,first OFF flag minute9.
- Bankrupt/empty:128/None/1v1/Brutal or Hard,seed9101,house1,first OFF flag minute6.

Exact case IDs and additional examples are in `d56-final-cleared-examples.json`.

### Commits, exercised checks and limitations

- Runner/sampler/reporting:ead23281,46551960,51c9cb85,9db1c94e.
- Power sizing:110d8d3b,820bae19; corrections4dc09268,ebd8ba42,a43467cc.
- Opening experiments8ff2d79a; no-military/cache correction60bbe8eb;
  low-cash capability5900ca7e; controlled-pass/frozen driver2aa9efa6;
  final tax-dependent financial scope**d56d1710**; independent guard fixture495c1b23.
- Historical fixture corrections did not waive assertions or extend timeouts:
  Starport cancellation uses the real queue front with a progress assertion;
  the quote case retains the real cheap-Quad competitor; legal wind availability
  and all owned-yard caches agree; the controlled pass uses the normal -1 sentinel.
- Final short gates exercised by CtestTriage: no-spice city **PASS23.23s**,
  MCV deployment **PASS9.63s**, growth installments **PASS8.31s**.
- Completed integrationfd36161f full133: **130 PASS/3 FAIL**, source unchanged.
  Mandatory AI gates individually passed: custom_attack6.57s, army_recovery16.85s,
  all_against_atreides119.17s, scenario_matrix61.08s. perf_regression passed109.71s.
- The full suite is **not clean**: spectator_online_end_probe and projectile_combat_probe
  timed out, and shared-spending's independent negative control was masked by the
  new guard. After495c1b23, shared-spending DuneCity **PASS23.61s**; the targeted
  projectile test still timed out300.05s. These rechecks are not a new133/133 pass.
  CtestTriage owns remaining profiling/integration. Its projectile sample is dominated
  by game initialization/content pin/mod activation/GFX/PNG loading; no AI production
  change is made here. Our final A/B cohorts ended11:35:08AEDT, before the full
  projectile window12:04–12:09, so do not blame that failure on their overlap.
- Earlier339 headless six-minute capture:22,500cycles, max processCPU14.863ms
  atcycle6633; worst AI-house wall15.19ms atcycle9525 had5.618ms processCPU.
  Render=-1, so this is not rendered FPS or final isolated CPU A/B. Earlier60bbe
  headless diagnostic AI-build max2355us/379calls and placement max360us/1669calls
  are historical wall-scope evidence. Final rendered FPS and isolated perf A/B are
  **not claimed**.
- Final mirrors do not re-cover160/Medium; browser/other-platform verification for
  this policy is not claimed. Persistent IX and rare zero-harvester signals are not
  all proven bugs. Saboteur orders, human leases, victory, refinery-first and unrelated
  repair/harvester rules are unchanged. Both behaviour switches are hashed/defaultON;
  shared unreleased protocol111 already covers them.

Evidence: `../outputs/dunecity-audit-20261103/`, especially `d56-final-matrix-comparison.json`,
`d56-final-stratified-detectors.json`, `d56-final-joint-exposure.json`,
`d56-final-combat.json`, `d56-final-turnover.json`, `d56-ladder-*-summary.json`,
`d56-frozen-manifest.json`, `d56-matrix-receipt.json` and raw diagnostic/crash gzip.
Verification-owner receipts are in `../outputs/ctest-triage-20261103/`.
Frozen bundles remain outside the checkout at `../dunecity-results/audit-d56d1710/`.

