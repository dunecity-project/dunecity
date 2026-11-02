# QuantBot Civilization Wars training

## Scope and deterministic gate

All strategy changes are controlled by a multiplayer-hashed `[Civ Wars] Strategy`
switch. `civWarsRules()` requires that switch, the Research Lab match rule, and
either a generated Civilization Wars map or the `CivWars` mod identity.
`isCivWarsMatch()` additionally requires the generator's canonical
`[CIVILIZATIONWARS] Version=1` marker, exposed by the cached
`GameInitSettings::isCivilizationWars()` accessor. Arbitrary maps using the CivWars
mod get research-aware economy but never generated-lane assumptions. Ordinary
matches retain the existing decision path. ResearchLab owns protocol 111 for the
combined release; there is no independent protocol or app-version bump here.

The generated-map contract is three horizontal lanes, centred at
`(2 * lane + 1) * height / 6`, rock ends of width `width / 4`, mirrored teams
(left team 1, right team 2), and one midpoint rock island per lane. Vehicles stay
in lanes; infantry may cross Mountain ridges. The marker, not a mod display name
or a terrain guess, authorises those assumptions.

## Opening from zero credits

* Keep the provided Refinery, Carryall, powered starter city and Police Station
  productive. Existing QuantBot city demand, roads, power and police-budget logic
  supplies tax income; it must not be replaced by a fixed build-order city.
* Check the actual starting worker count. In base ObjectData a Harvester costs
  300 and is built by a Heavy Factory (tech 4, Radar and Light Factory required),
  while a Refinery costs 400 and unlocks at tech 1. A newly placed Refinery gives
  a free worker, already accounted for in `planBuildBudget()`. Before tech 4,
  missing workers therefore require a Refinery, not a factory order that cannot
  execute. Measure the generator's free-worker delivery rather than assuming it.
* Preserve essential power and the ability to bank receipts. Do not spend every
  small tax receipt on optional upgrades, repeatable zones or reinforcement while
  the first useful research step cannot accumulate its full up-front cost.
* Reuse ResearchLab's basic lab construction/research helper and its hashed
  `[Force Concentration] ResearchLab` switch. Its house-level command validates
  the lab and deducts `300 * targetLevel`; a step takes `30 + 10 * targetLevel`
  seconds. Tune the reserve/pacing through that helper rather than issuing a
  competing command. Tech 2 costs 600; levels 2 through 9 total 13,200 credits
  and 680 seconds of research if continuously funded.

## Research, army and defence

Measure time to every completed level and whether upgrades unlock usable
production. Protect the next research payment without blocking blackout recovery,
replacement income, a live air attack or an affordable combat producer. Avoid
saving beyond storage capacity. Compare research-first and income-first pacing
against the unchanged basic lab policy; retain only measured improvements.

The starting rocket turrets already cover the lane mouths and core. Existing
observed-air detection, launcher floor and counter-air construction stay the
primary response. With Trooper Anti-Air enabled, mobile Troopers can bridge the
period before launchers; only buy a bounded, useful counter-air contingent and
respect existing infantry quotas, human-order leases and normal unit limits.
Saboteur orders remain untouched. Keep difficulty-specific budgets and readiness
as the control; no formation is the default.

## Spice, islands, worms and city growth

Existing `planSpiceEconomy()` evaluates remaining spice, unloading queues,
Carryall demand and expected trip cost. Retain those controls. Evaluate whether
workers are blocked at tech 1, whether new Refineries shorten the lane trip, and
whether queues are caused by unloading or booked transports before adding bays.

The existing rock survey/MCV/escort planner is the starting point for island
colonisation. Measure its core-completion, spare-rock and defence gates before
changing them: a large home-rock region may postpone every midfield island even
when the island is strategically useful. A lane-only extension should choose a
safe, reachable island and retain existing recent-loss, destination reservation,
escort and MCV footprint protection. Do not create yards without an economic or
lane-control purpose.

Code inspection found two concrete control limitations: `RockExpansionPolicy`
requires 48 free tiles in both the formation and local window, whereas a generated
island guarantees only a 5×5 core; the smallest `(rx=5, ry=3)` bounds contain at most
37 rock tiles. New-colonist demand also waits for the home base to be built out.
Changing island ranking alone cannot overcome either gate. Teammates' existing
buildings and assigned MCV destinations must count when avoiding duplicate colonies.

Harvester safety already reacts to visible/remembered threats; inspect its worm
handling and measure worm-attributed worker deaths. Respawning worms mean a
cleared lane is not permanently safe. Prefer nearby protected spice/short sand
exposure and Carryall transport; never infer that all sand is forbidden or give
an invisible-worm omniscience advantage. Keep normal combat pathfinding: attack
objectives and staging points must be reachable through a lane, not over a ridge.
Infantry can use its ordinary cross-ridge paths without a separate formation
system. City growth stays on buildable rock with existing R/C/I demand, road
access, services and difficulty ceilings.

The control's visible-worm Harvester danger radius is one tile (zero weapon range
is clamped to one); its two-second danger refresh can therefore warn too late.
The existing two-minute unsafe-field memory can be reused rather than creating
omniscient permanent worm exclusion. Its route estimate checks danger along a
straight line, not terrain reachability; vehicles can otherwise choose spice
across a Mountain ridge and detour. The three lanes connect around the rock ends,
so whole-map vehicle-region IDs are not lane IDs.

Ordinary ground waves with no formation use engine Hunt. The existing
`customWaveObjective()` is mainly a front-strength observation anchor, not a
movement order. If simulations show that waves never cross a contested lane,
the minimal extension is an unforced, contact-responsive `doHuntToward()` on
already-eligible dispatched members, not periodic forced attack orders.

## Experiment contract

Use cluster `dunecity-queue submit`; each game is one schedulable unit using the
existing `schedule` / `build_probe` / `play` harness contract and managed scratch.
Use the same binary for switch-off baseline and switch-on candidates. Fixed seeds:
**7101, 7202, 7303, 7404, 7505**. Initial matrix: 1v1, 2v2 and 3v3;
Medium/Medium, Hard/Hard and Brutal/Brutal; 30 game minutes, with side-swapped
Hard/Medium and Brutal/Hard ladder matches. Record generator size, spice, worms,
research options, switches, source revision and exact commands with each run.
A timeout is a stalemate/draw, never a victory adjudicated from material.

Collect per-house research start/completion times; credits curves; workers,
Refineries and city population; island yards; units produced; waves launched and
reaching the far rock; elimination/outcome and length; worm losses; simulation
FPS and AI/placement CPU. Keep baseline data before tuning. Experiment records
below report only completed observations; absent evidence is explicitly unverified.

Final gates: paired switch comparisons over all three team sizes and at least
five seeds, difficulty ladder, mandatory QuantBot attack/recovery/Atreides/
scenario-matrix/FFA regressions, performance checks, ordinary-game off/on digest
identity, and the full distributed CTest report. Do not weaken existing checks
or use a partial fixture as evidence for full-game strength.

## Experiment results

Evidence root: `~/Documents/projects/dunecity-results/civwars-ai/`. The matrix
harness writes raw games, houses, samples, research, items, waves, performance
and worm-ID CSVs before deleting each managed game profile. Actual matches use
a 1024 MiB diagnostic capture ceiling; capped captures never claim exact worm
losses or attack totals. `CIV_WARS_AI_PERF` is measured thread CPU around the
real `QuantBot::update()` using a private probe subclass, not inferred from
wall-time scopes. Concurrent cluster timing is labelled by host; quiet-host
performance regressions remain a separate gate.

### Integration observations before the gameplay baseline

| Source / job | Parameters | Observed result |
|---|---|---|
| `348e4a28`, cluster `20261102T215902-civwars-ai-baseline-016899` | 45 games; all three team sizes; Medium/Hard/Brutal mirror pairs; five fixed seeds; Strategy off; size 0, worms 1/lane, spice 2; 30 min | Cancelled before gameplay: PC could not compile two ambiguous `INIFile(bool, "")` fixture calls; other machines were offline/guarded. This is not a strength result. |
| `348e4a28`, local queue `20261102T222028-civwars-baseline-pilot-572d46` | Hard/Hard, 1v1, seed 7101, Strategy off; same map/rule parameters; 30 min | Private probe compiled, but initialization rejected disabled Research Lab. `CivWars::makeInit()` passed default Random Skirmish parameters that overwrote the required research rule. No cycles were played. |
| `23aa35a9`, cluster `20261102T222453-civwars-ai-baseline-fixed-b63cef` | Same 45-game baseline, with generator fix `b849a943` and Research Lab fixture correction `dc3a27e0` | Submitted with the canonical tech-1/research-enabled initialization and unambiguous Windows fixture constructors. Gameplay results are reported below when available. |

The one-game local queue fallback runs at `nice -n 10`, one game at a time, under
the integrator's explicit authorization while the cluster is blocked. It never
changes cluster guest guards. Native `build-civwars-ai` uses the staging Release
options, shared ccache, and `nice -n 10 -j4`; its initial 709-step build completed
in 48.69 seconds and the dependency-record check passed. The playable-mode
incremental correction built in 5.27 seconds.

### First playable control: seed 7101, Hard 1v1

Local queue `20261102T222512-civwars-baseline-pilot-fixed-f56fcc`, source
`23aa35a9` with documentation-only working changes, size 0 (129×96), one worm
per lane, medium spice, Strategy off, 30-minute limit. Both houses survived:
**time limit, no attacks, no far-rock arrivals, no island yards**. Telemetry was
complete (15,269 records); three worm IDs accounted for **12 unit deaths**
(7 Atreides, 5 Sardaukar).

| House | First research start | Tech 2 complete | Final tech | Final credits | Harvesters / Refineries | Ground attacks |
|---|---:|---:|---:|---:|---:|---:|
| Atreides | 24.15 min | 24.99 min | 2 | 34,133 | 16 / 29 | 0 |
| Sardaukar | 23.32 min | 24.15 min | 4 | 28,161 | 19 / 29 | 0 |

Research was delayed despite banked income: the basic helper treats it as
optional capital behind the map-share opening fleet, projected repeatable
production, and any dangerous-crime property. The control expanded bays for
most of the game instead of unlocking the army. This motivates the first
candidate; it does not establish a strength improvement by itself.

### Candidate 1 parameters

* Worker target: at most **2 before tech 4**, **4 until the first ground wave**,
  then **4 Medium / 6 Hard / 8 Brutal**, still bounded by sustainable spice and
  the existing engine/config limits. Refineries retain the real included-worker
  and unloading-throughput rules.
* Research runs before optional producer orders. Its up-front payment keeps
  **max(300, city working reserve)** after existing queue obligations. While
  saving, other producers preserve the next payment plus that runway only if
  the existing four-minute cash forecast can cover it; tiny tax-only cities can
  still compound their income instead of freezing every receipt indefinitely.
  Live economy attacks, engaged aircraft, blackout recovery, missing income and
  insufficient opening workers still prevent the purchase. A crime statistic
  alone is not a permanent technology veto.
* Tech 4 must become an actual Heavy Factory before tech 5 is researched.
  Storage expansion can spend research savings to avoid a storage-cap deadlock.
  Ordinary Research Lab matches retain their original optional-capital path.
* Visible, active hostile worms warn harvesters and tactical planners at **8
  tiles**; rock is safe from this worm-only field. Hidden and sleeping worms
  do not reveal their locations. Generated-map spice assignments reject
  straight corridors crossing mountain ridges; refinery returns keep the
  ordinary detour-capable path rather than that conservative spice filter.
* Generated midpoint islands require **24** free/local tiles instead of the
  ordinary **48**. Team end formations and allied-claimed/reserved islands are
  excluded. After the first ground wave, a solvent, defended production core
  with at least one wave's active ground value may request one escorted
  colonist even with room at home. Existing yard ceilings, route safety,
  repeated-loss hold and escort gates remain in force.

### Complete initial control matrix

Source `23aa35a9da6d75fb88cdae0c048b67b13b1ec04d`; cluster job
`20261102T222453-civwars-ai-baseline-fixed-b63cef` completed all **45/45** games
on `stefPC`. Seeds were **7101, 7202, 7303, 7404, 7505**, size 0 (129×96),
one worm per lane, spice level 2, a 30-game-minute cap, strategy off. Every
telemetry stream was complete. Local and Windows Hard/7101 controls had the
same end digest `1461d4bb945ce2bd`.

| Format | Mirror difficulty | Decisive / 5 | Time limits / 5 | Median length, min | Tech 2 reached | Median tech 2 completion, min | Worm unit deaths | Attacks launched |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 1v1 | Medium | 2 | 3 | 30.00 | 5/10 | 15.46 | 47 | 9 |
| 1v1 | Hard | 0 | 5 | 30.00 | 8/10 | 23.14 | 59 | 6 |
| 1v1 | Brutal | 0 | 5 | 30.00 | 10/10 | 22.54 | 49 | 8 |
| 2v2 | Medium | 4 | 1 | 28.27 | 15/20 | 12.21 | 70 | 22 |
| 2v2 | Hard | 2 | 3 | 30.00 | 15/20 | 12.71 | 78 | 33 |
| 2v2 | Brutal | 1 | 4 | 30.00 | 17/20 | 12.92 | 101 | 29 |
| 3v3 | Medium | 0 | 5 | 30.00 | 22/30 | 13.14 | 107 | 31 |
| 3v3 | Hard | 2 | 3 | 30.00 | 21/30 | 12.87 | 92 | 32 |
| 3v3 | Brutal | 1 | 4 | 30.00 | 20/30 | 11.73 | 89 | 36 |

Tech timing above is the median over houses that reached that level
(**133/180** reached tech 2); the `-1` unreached sentinel is excluded. This is
not a claim that eliminated houses retain their technology. Decisive games are
actual surviving-team wins;
time limits are not awarded by army value or credits. Attacks are complete
nonempty `ground_hunt` decision events; worm deaths join complete lethal-damage
attacker IDs to the engine-observed worm set. These historical controls are
diagnostic; the final off/on comparison must use the same final binary.


### Candidate 1 pilot: faster technology is not sufficient

Commit `37a15d23`, job `20261102T224344-civwars-candidate1-pilot-93a0d7`,
Hard 1v1, seed 7101, 30 minutes, the same map parameters as the control.
All four arms timed out; none launched a normal ground wave.

| Strategy enabled for | Tech 2 completion, left / right min | Final tech, left / right | Harvester worm deaths | All-unit worm deaths | Enemy-end ground units reached |
|---|---:|---:|---:|---:|---:|
| Neither (off) | 24.99 / 24.15 | 2 / 4 | 10 | 12 | 0 |
| Both (on) | 3.76 / 3.80 | 7 / 8 | 0 | 17 | 14 |
| Left only | 3.76 / 25.35 | 8 / 3 | 3 | 9 | 0 |
| Right only | 27.84 / 3.80 | 2 / 7 | 3 | 16 | 1 |

The off digest still exactly matched `1461d4bb945ce2bd`. The both-on arm
had no economic vehicles eaten, but more combat-unit exposure, and spent on
59/79 city zones plus technology before accumulating the 7,500-value ground
dispatch threshold. Late `attack_deferred` events were army-threshold and
missing-repair-yard holds. This candidate is **not** accepted as a win-rate
improvement.

### Candidate 2 parameters

Retain candidate 1's measured early-economy/harvester changes, but stop
researching at tech **4** until an actual ground wave has launched. Later
research requires at least `minWaveValue()` of active ground force; tech **5**
must become an actual Repair Yard before researching tech 6. City land
investment is capped at **24** R+C+I lots before the first wave, then
**36 Medium / 48 Hard / 60 Brutal**, never raising an existing lower city cap.
These are AI construction ceilings, not caps on human population, city
maturation, services or power. No attack orders or wave thresholds are changed.


### Candidate 2 measured results and acceptance status

Commit `7a51e0ce`:

* `20261102T225438-civwars-candidate2-pilot-ed4673`: Hard 1v1, seed
  7101, off/on/left/right, 30 minutes. All four timed out. On, left-only and
  right-only each launched one ground attack; both-on had zero Harvester worm
  deaths (15 combat-unit worm deaths), one island yard and four ground units
  reaching the enemy end.
* `20261102T225438-civwars-candidate2-formats-7985ba`: seed 7101,
  both-on, Hard:Hard, Hard:Medium and Brutal:Hard in each of 1v1/2v2/3v3.
  All **9/9 timed out at 30 minutes**, despite 1/3/4 attacks in the Hard mirrors
  and 1/1/2 island yards respectively. Tech 2 completion was about
  **3.74–4.10 minutes** across those houses. This does not establish a
  difficulty ladder: there were no decisive wins in those pairings.
* `20261102T225743-civwars-candidate2-forty-b1d7ad`: the same Hard 1v1
  off/on/left/right pilot extended to 40 minutes:

| Strategy enabled for | Outcome | End, min | Final tech left / right | Ground attacks | Island yards | Ground units reaching enemy end |
|---|---|---:|---:|---:|---:|---:|
| Neither | Time limit | 40.00 | 2 / 5 | 6 | 0 | 27 |
| Both | Time limit | 40.00 | 6 / 6 | 2 | 3 | 9 |
| Left only | Time limit | 40.00 | 8 / 2 | 1 | 1 | 25 |
| Right only | Right won | 32.62 | eliminated / 7 | 1 | 3 | 39 |

The candidate converts technology and island access much earlier and protects
economic vehicles, but **has not met the finishing/strength acceptance gate**.
The 30-minute control banked 75,638/77,673 refined spice credits versus
23,804/23,165 for both-on: the four-worker pre-wave ceiling restricts ongoing
army throughput. Future tuning must resolve that measured income/army
bottleneck, rather than merely lowering victory or attack thresholds.

Review after the pilots also found that an unlimited population ceiling
(Brutal) bypassed the new land cap. Admission now independently enforces a
finite zone cap even when population is unlimited; ordinary unlimited cities
keep their old fast admission. **The admission correction has not had new
simulation measurements.** Generator revision `df92c57f` adds only fixtures/docs;
its author confirmed the generated map/start state is unchanged from the control.
The measured
candidate-2 Brutal rows above must not be relabelled as final-candidate results.

No final ≥5-seed mirrored-difficulty/side-swapped ladder matrix, full cluster
CTest, mandatory AI regression/performance pass, rendered FPS measurement or
completed non-Civ-Wars off/on digest comparison is recorded. One ordinary
off-arm run was exercised, but it is not a paired gate proof. Do not treat the
default-on experimental switch as accepted for release on this evidence.

Data directories under
`/Users/stefan/Documents/projects/dunecity-results/civwars-ai/`:
`baseline/`, `candidate1-pilot/`, `candidate2-pilot/`, `candidate2/`,
`candidate2-pilot40/`. Each retains game/house/research/minute credit-curve,
unit, island, attack, worm-loss and actual AI CPU tables; later runs also retain
selected decision JSON in `decisions.csv`. Headless cycles/second is not
rendered FPS.


### Candidate 3 parameters and evidence identity

Keep the two-worker opening through tech 3, then permit **6 Medium / 8 Hard /
10 Brutal** workers before the first wave and **8 / 12 / 16** afterwards,
still clamped to the original sustainable/configured target. This directly
addresses candidate 2's observed spice-income deficit while retaining its
research staging, city-investment pacing and worm/route safety.

Historical `workingTreeModified` flags include non-ignored build products, so
those rows are descriptive, not automatically source-matched A/B proof. New
runs additionally record an actual SHA256 `sourceFingerprint` over sorted
tracked/non-ignored source-tree files, excluding the invocation's build/output
trees and Python/npm caches. Missing inputs withhold identity. The queue's
immutable snapshot keeps inputs fixed during a match. This identifies source
inputs, not system toolchains or rendered-asset provenance; map/options/config
identities and source commits remain separately recorded.


### Candidate 3 cross-format pilot

Source `737074c4`, job `20261102T233351-civwars-candidate3-formats-c8fd43`,
seed 7101, strategy on, 40-minute cap. All nine streams were complete, with
the same actual source-fingerprint prefix `14c5ce77f49b`:

| Format | Pair | Outcome | End, min | Ground attacks | Island yards |
|---|---|---|---:|---:|---:|
| 1v1 | Hard:Hard | Time limit | 40.00 | 3 | 2 |
| 1v1 | Hard:Medium | Time limit | 40.00 | 3 | 2 |
| 1v1 | Brutal:Hard | Brutal won | 35.30 | 3 | 3 |
| 2v2 | Hard:Hard | Time limit | 40.00 | 8 | 3 |
| 2v2 | Hard:Medium | Time limit | 40.00 | 6 | 2 |
| 2v2 | Brutal:Hard | Brutal won | 39.45 | 9 | 2 |
| 3v3 | Hard:Hard | Time limit | 40.00 | 6 | 2 |
| 3v3 | Hard:Medium | Time limit | 40.00 | 7 | 3 |
| 3v3 | Brutal:Hard | Time limit | 40.00 | 7 | 1 |

This is not yet the required five-seed, side-swapped ladder evidence. Higher
worker throughput increased attacks and produced two decisive Brutal wins,
but Hard mirrors and the largest team games still stalled.

### Candidate 4: lane-directed attack move, not forced combat

Hypothesis: independent map-wide Hunt sends a nominal wave toward different
contacts instead of concentrating it at one lane's defences. The engine
already has `doHuntToward`: normal attack-move combat stops for enemies met
on the route rather than replacing the walk with a map-wide nearest target.
Generated-map plain-Hunt waves now share the central-lane sand approach just
before the opposite rock end. Alliance members use the same known terrain
waypoint; no hidden enemy object is consulted. After arrival ordinary Hunt
continues. No formation shape, forced target, combat retreat, human lease,
saboteur order or non-generated-map order is changed. Explicit formation/
party/split/finish controllers retain their own destinations.


### Candidate 4 measured result and candidate 5 adjustment

Source `9945bc55`, job `20261102T235633-civwars-candidate4-formats-8225c0`,
the same nine seed-7101 format/pair cases as candidate 3, all strategy on:
**0/9 decisive games; all reached 40 minutes**. The Hard mirrors launched
3/7/6 attacks and reached the opposite end with 27/28/25 distinct ground
combat units in 1v1/2v2/3v3. Brutal:Hard reached it with only 3/2/14.
This is worse than candidate 3's two decisive Brutal wins and is not retained
as a strength improvement.

Candidate 5 moves that attack-move objective **inside** the opposite rock end
(`x = width−1−width/8` or `width/8`, `y = height/2`) instead of stopping on
sand outside it. Hypothesis: the outside waypoint ended the directed walk
before the wave actually entered the base. The rule still consults known
generated terrain, not unseen enemy objects, and retains normal combat.

The first defence/isolation gate run, source `737074c4`, completed all
**16 ordinary-map cases with identical off/on digest sequences and distinct
config hashes**, plus all 16 left-defender air cases. Right-defender cases
failed: their attacking QuantBot retreated/reassigned the single scripted
order, giving zero observed enemy projectile cycles, no aircraft damage and
no kill despite actual defensive fire. The whole gate job was cancelled
instead of accepting those invalid strike fixtures. The corrected fixture
must use real human command authority on the attacking house; defender
orders, money, health, timers and policy remain untouched.

Native candidate-4 build plus focused checks passed: **24 Python tests**
(0.851s), **35 tagged native cases / 6,577,325 assertions**; the complete
build/check chain took **18.73s**. The integrator explicitly owns the final
full cluster CTest/regression/performance run after merging this branch; its
earlier shared-mutex full-suite attempt was cancelled at 22/133, not passed.


### Converged selection and required staging integration

The integrator requested convergence and required staging's
`93d96ea3` **CityGrowthPower** correction before the authoritative matrix.
It was merged as `71560cee`; only the additive HANDOVER headings conflicted
and both were preserved. No native-code conflict occurred.

Select **candidate 3's measured economic/research/worm/island policy** as the
best completed pilot: two decisive wins versus zero for candidate 4 on the
same nine seed-7101 cases. Remove the experimental lane-directed commands,
waypoint helper and its test rather than ship an unproved tactical change.
Candidate 5's speculative inward waypoint was never accepted; its 240-game
batch and gate snapshot were cancelled when the staging prerequisite landed.
Any partial rows in `candidate5/` are excluded from final acceptance.

The authoritative comparison now holds CityGrowthPower **on in both arms**,
and compares only the Civ Wars strategy switch. It uses five fixed seeds,
all three team sizes, same-tier mirrors, both difficulty-ladder orientations
and direct strategy-versus-control games. A separate corrected real-human-
authority strike/isolation gate uses the same merged source. This is the
existing best policy with a required integration fix, not another speculative
strategy round.

For completeness, candidate 4's rejected nine-case table (all at 40 minutes):

| Format | Pair | Decisive | Ground attacks | Enemy-end ground units | Island yards |
|---|---|---:|---:|---:|---:|
| 1v1 | Hard:Hard | 0 | 3 | 27 | 2 |
| 1v1 | Hard:Medium | 0 | 3 | 68 | 2 |
| 1v1 | Brutal:Hard | 0 | 4 | 3 | 2 |
| 2v2 | Hard:Hard | 0 | 7 | 28 | 2 |
| 2v2 | Hard:Medium | 0 | 8 | 113 | 2 |
| 2v2 | Brutal:Hard | 0 | 10 | 2 | 2 |
| 3v3 | Hard:Hard | 0 | 6 | 25 | 2 |
| 3v3 | Hard:Medium | 0 | 7 | 65 | 3 |
| 3v3 | Brutal:Hard | 0 | 7 | 14 | 1 |

Candidate-5 code had passed a native build and 24 Python / 35 tagged native
cases (6,577,325 assertions), taking 44.72s; that does not establish its
simulation strength and does not replace checks on the converged source.

