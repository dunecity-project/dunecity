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

No simulations have been run for this initiative yet. The playable generator and
house-research implementation are being integrated; the first measured baseline
will use those exact committed implementations.
