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
`attack_launched` decision events; worm deaths join complete lethal-damage
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

