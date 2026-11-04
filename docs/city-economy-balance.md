# City tax and spice economics (tax basis 1.0.638)

Current AI spending rules are documented in [QuantBot shared spending](quantbot-spending.md)
for 1.0.701. Historical AI sections below describe the earlier implementation.
The fixed tax/spice hedge gate and alternating investment windows are removed.

## Active tax formula

Stefan authorized the Micropolis easy restructuring, with Palace as an explicit
R+C exception to the government tax exemption. Only actual R/C/I zones and the
Palace generate direct city tax. Other government buildings retain their jobs,
demand and pollution roles, but generate no tax.

Annual gross tax = `[2*(zoneR/8 + zoneC + zoneI) + palaceR/8 + palaceC] * averageLandValue/120 * taxPercent * 1.4`.
Use taxable populations only; Palace contributes both its R and C portions.
Stefan chose a 2x private-zone boost because tax is easier and less exposed
than spice harvesting. Palace is excluded from this boost.
The census stores weighted eighths (`2*(zoneR + 8*zoneC + 8*zoneI) + palaceR + 8*palaceC`) to retain partial residential houses.
Annual totals are rounded once per house and paid fractionally each simulation
cycle. This follows Micropolis easy's weights/rate while avoiding its intermediate
integer truncation of tiny cities. Explicit zero land value earns zero; AI
forecasts can assume 128 for unknown future land. All values are deterministic
integer arithmetic. No per-lot tax calculation or new save-format field.

One classic city year = 3,750 cycles = 60 simulated seconds, so annual credits
equal credits per simulated minute. Since 1.0.861 the hashed match rule
`Slow City Year` (ini key, default on; GameInitSettings MOD11, save 9877,
protocol 106) doubles the year to 7,500 cycles = 120 seconds: taxes and police
are paid at half the rate per minute, and zone growth (48 day ticks a year) runs
at half speed. Games saved before the rule keep the 60-second year. The AI keeps
the minute as its forecast unit and converts city amounts with
`DuneCity::annualPerMinute`, so its behaviour is unchanged when the rule is off.
The per-minute figures below are for the classic year; halve them with the rule.
Simulation speed changes both taxes and harvesting; do not use wall-clock FPS to
compare them.

### Tax rate per house (save 9880, protocol 110)

Each house has its own tax rate, 0-20%, default 7% (`HouseCityState::taxRate`). The budget
window sends `CMD_CITY_SET_TAX_RATE`, which sets the rate of the issuer's house only, like police
funding. Before 9880 there was one rate for the whole map, so one player's budget changed every
house's tax and demand; such a save gives that rate to every house. QuantBot never sets a rate:
it plans with its own house's rate. The rate also feeds that house's demand valves (Micropolis
tax table), so it affects only its own growth.

### Land value for tax (match rule `Micropolis Land Value`, default on; MOD14, protocol 110)

Tax multiplies by a house's average land value, so how that average is taken decides who earns
more from the same city. The rule follows Micropolis (`s_scan.c`, `s_sim.c` CollectTax):

| | Rule on (Micropolis) | Rule off (before 110) |
|---|---|---|
| Developed 2x2 block | holds a road or a building | holds a road or any ground object, units included |
| City centre (per connected city) | centre of mass of its buildings, each counted once at its centre; walls excluded | centroid of its developed blocks |
| House average land value | mean over every developed block where the house owns a road tile or a building, by area, roads included | mean over one sample per building, at its top-left tile, roads excluded |

Micropolis taxes one city with `TotalPop * LVAverage / 120 * CityTax * FLevels`, where
`TotalPop = ResPop/8 + ComPop + IndPop` and `LVAverage` is the mean over every developed
half-resolution block. DuneCity keeps that per house. Industrial is taxed like commercial in
both. Under the old rule many small buildings (walls, turrets, windtraps) each counted as much
as a zone, and parked units and roads pulled the centre away from the zones, so a large mixed
base earned less per citizen than a compact AI city.

The block land value itself is unchanged by the Micropolis rule: `4*(34 - distance/2)` +
terrain (open sand counts as water; mountains count as forest with Mountain Scenery on),
including park terrain, - pollution, -20 above crime 190, clamped 1-250, then
Palace/Stadium bonuses and hostile-unit penalties. Building damage does **not** reduce
land value by default. The separate **Damage Lowers Land Value** match rule (default off, MOD17)
restores the old proportional health penalty on occupied blocks after these effects;
repairs restore that value on the next scan. Shared blocks use the worst building
condition, not compounded penalties. This rule does not change health-scaled windtrap power.
The census check is `custom-city-cap-probe.inc` (ctest `quantbot_custom_attack`): a parked
unit changes neither a block nor the house average with Micropolis on, and still develops
its block with it off. `structure_degradation_dunecity` checks damaged and repaired
buildings through real effects scans with Damage Lowers Land Value both off and on.

### Mountain scenery (match rule `Mountain Scenery`, default on; MOD18, protocol 112)

Mountain tiles, including Civilization Wars divider ridges, act like Micropolis forests:
each contributes **15 raw terrain points**. Sources share the existing park-terrain grid
and integer smoothing: `(centre + (north + south + east + west)/4)/2`, one pass.
Micropolis uses 4-tile terrain blocks (3-tile zone + road); DuneCity uses 3-tile blocks
(2-tile zone + road). The score reaches the source block and its four cardinal neighbours,
not a radial long-distance stamp. The resulting tile scores are averaged over each
2x2 land-value block before pollution/crime and clamping. Empty, undeveloped desert stays
at zero. Mountains remain scenic even with infantry on them.

This reuses the natural-terrain treatment in Micropolis
[`pollutionTerrainLandValueScan` and `smoothTerrain`](https://github.com/SimHacker/micropolis/blob/master/MicropolisCore/src/MicropolisEngine/src/scan.cpp):
nonzero terrain below rubble (including trees) adds 15, then one terrain smooth.
No mountain terrain bonus existed before this rule. Ordinary rock has no scenery bonus.
Open sand/dunes retain their separate, stronger water-like effect: 40 raw points per
unoccupied tile smoothed twice on the 2x2 grid, plus a direct 8-point falloff over 5 tiles.
Switching Mountain Scenery off restores the former terrain calculation; earlier
settings streams load it off. The option is saved/hashed in MOD18 (save 9883),
shared with fixed-price research, and can be set in Game Rules, defaults or mod rules.
Frozen 9882/MOD17 carries no scenery byte; its retired research-area field is consumed
and ignored rather than changing its layout in place.


### Starport population (match rule `Starport Population`, default on; MOD16, protocol 111)

In non-campaign city games a house can start a Starport only while its displayed city population
(internal × 20) is at least 5,000 (`DuneCity::kStarportPopulation`). The Construction Yard list
refreshes when the city crosses the line. A Starport already being built or queued is kept if the
city later shrinks, and existing Starports keep working. Off, older saves and campaigns: no
requirement.

## Income readout

At 7% tax and house-average land value 128, gross credits per simulated minute:

| Tax-producing building | Low | Medium | High |
| --- | ---: | ---: | ---: |
| Residential zone | 41.81 | 62.72 | 104.53 |
| Commercial zone | 20.91 | 62.72 | 104.53 |
| Industrial zone | 20.91 | 62.72 | 83.63 |
| Palace (R+C) | 31.36 | 62.72 | 104.53 |

Approximate contributions before city-total rounding and upkeep. Empty zones
pay zero. A single developed house within an R lot adds population 2, about 5.23
credits/minute; eight houses total 41.81 before the next density stage.
At land value 64, all amounts halve; at 192, multiply by 1.5. Rates scale with tax.

Before 1.0.637, high-density R/C/I yielded 186.67/23.33/18.67 at the same settings.
Relative to that legacy rate, 1.0.638 is R -44%, C/I +348%. Palace was exempt in 1.0.634–636; now both halves
pay tax (rather than the old pre-634 mismatch between R-only payout and R+C UI).

## Government roles

| Infrastructure | Role / maximum density | Direct tax |
| --- | --- | ---: |
| WindTrap | Power only; no industry | 0 |
| Light Factory, Spice Silo | Low I | 0 |
| Refinery, Heavy Factory, High Tech Factory, Repair Yard | Medium I | 0 |
| House IX | High C | 0 |
| Starport | Seaport / existing high-I employment and demand gate | 0 |
| Construction Yard | Existing high I | 0 |
| Radar, Airport | Existing medium/high C respectively | 0 |
| Barracks, WOR | Existing high R garrison | 0 |
| Palace | R+C, up to high density | See income table |

All remaining non-zone structures also have zero direct tax. Silo, WindTrap,
IX, Starport and Construction Yard stay clean. Light Factory emissions cap 10;
Refinery/Heavy/HighTech/Repair cap 25. Jobs/population/emissions and loaded
occupancy are clamped to mapped density; UI labels agree.

## Roads and other costs

Road upkeep is removed in 1.0.637 at every population, including heavy traffic.
There is no per-house road census or charge in the runtime, UI or AI forecasts.
The billing-specific ownership additions from 647b98c are reverted: automatic
frontage roads and the city road-overlay command preserve underlying tile
ownership. Load no longer infers owners from neighbouring structures. Existing
saved tile ownership is preserved; no speculative clearing of concrete owners.
Normal House::placeStructure ownership for manually built foundations/roads
remains. Roads still provide foundations; enemy roads/concrete do not expand a
house's construction range. Roads are not converted to concrete or removed.

Police/turret upkeep remains. Separate power charges remain and are not part of
the city budget panel: `powerRequirement/32` every 15 seconds when enabled, or
nominally `powerRequirement/8` per city year. Construction, units and repairs
are also separate from gross tax income.

### QuantBot police funding recovery

In custom city games the full-control QuantBot reviews its own police budget.
The normal target is the affordable share of recurring tax after power: 33%
early, or 50% with at least 5,000 **uncommitted** credits or a healthy operating
margin. A spice windfall is not recurring tax, so cash recovery does not bypass
that limit. Campaign and human-house helper budgets are not rewritten.

`[General Behavior] PoliceFundingRecovery=true` in `QuantBot Config.ini` is
default-on and included in the multiplayer config hash. Cuts remain immediate;
increases of at least five points retain the 30-game-second review. A smaller
affordable increase now waits two game minutes rather than being rejected
forever by the smoothing deadband (including the last 96-99% back to 100%).
Off reproduces the former deadband for same-build comparisons. The existing
serialized last-change cycle supplies both review intervals; no new save field.

The `quantbot_police_budget` real-engine probe follows one house from fully
funded to cash-constrained and back to its normal affordable target, then
checks a small cut followed by full recovery, with an off-switch negative
control and exact review-boundary checks. AI orders apply to that house
directly after the command phase; human `CMD_CITY_SET_BUDGET` still uses its
issuer's house through `CitySimulation::executeCityCommand`.


## Complete harvester cycle

Capacity 700; harvesting 0.1344/cycle = 8.4/second, so filling takes 83.33 seconds.
A healthy refinery unloads 0.625/cycle = 39.0625/second, taking 17.92 seconds.
Travel, fields, queues, damage and carryalls alter actual delivered income.

| Extra travel/queue seconds per trip | Gross credits/minute | High R or C equivalent | High I equivalent | High Palace equivalent |
| --- | ---: | ---: | ---: | ---: |
| 0 (upper bound) | 414.80 | 3.97 | 4.96 | 3.97 |
| 30 | 319.99 | 3.06 | 3.83 | 3.06 |
| 60 | 260.46 | 2.49 | 3.11 | 2.49 |

Formula: `700*60/(83.333+17.92+extraSeconds)`. Source-derived estimates, not
measured match income. A refinery receives 700 per full delivery, but has no
independent passive income: its income is its fleet's delivered spice. Do not
add refinery income to harvester income again. The Tornie-only Worfinery also
processes deliveries; it is not a standard DuneCity tax-producing building.

## Zone construction and AI

R/C/I use normal BuilderBase configured timing, respecting house/mod data.
Default 40*15 ticks*16ms = 9.6 simulated seconds at full speed with sufficient
funds. Roads/instant-build options unchanged; zone prices and population growth
unchanged. QuantBot forecasts include construction plus 60s growth allowance.

First refinery remains an income/technology prerequisite, then a demanded R
hedge. No C/I is forced against nonpositive demand. Zone choice normalizes demand
maxima; among needs within 20% of strongest, balances built+queued plots with the
existing 3:1:1 R/C/I weights. This avoids C-before-I 500 and forced-R-infill starvation.
Housing infill remains a placement preference, not a zone-type override.

Tax versus refinery comparison evaluates four simulated minutes of marginal
proceeds per credit, with actual setup/power, demand, suitability, growth and
unfinished lots. No road upkeep estimate. C/I gets limited indirect credit for
supporting housing short of jobs, using the boosted zone R/8 tax weighting. Refinery
investment counts only extra throughput and its included worker, using committed
workers/bays rather than a hypothetical future fleet. First delivered loads are
credited explicitly. Includes fill/unload, construction, bounded local travel
sampling and danger; no new pathfinding.

Payout, budget, QuantBot services/production and both active Mentat build paths
use the same weighted taxable census. Telemetry `tax_base_eighths` explicitly
labels its units; policy `transport-tax-hedge-v59`. Existing demand/population
census remains unweighted and separate from taxation.

## Reference

Micropolis `simulate.cpp` setValves uses R/8+C+I; collectTax uses landValue/120,
tax percentage and FLevels 1.4/1.2/0.8. DuneCity adopts the easy 1.4 factor for all
AI difficulties; it is an economic balance constant, not an AI handicap.
Verified local read-only reference:
`../simcity/micropolis/MicropolisCore/src/MicropolisEngine/src/simulate.cpp`.
Upstream: https://github.com/SimHacker/micropolis/blob/master/MicropolisCore/src/MicropolisEngine/src/simulate.cpp

## Fleet comparison behind the 2x choice

At 7% tax, LV128, 30 seconds travel/queue per load, and allocated wind capacity
including generator foundation (320/100 power), a refinery with three harvesters
costs 1,126 and nets ~956/min; four cost 1,426 and net ~1,276/min. The refinery
includes the first harvester; 30 power upkeep costs 3.75/min. At staggered arrivals
these fleets use ~41%/55% of ideal unloading capacity. Factory setup, losses,
repair and escorts are excluded; existing production capacity is assumed.

A mature 3R+1C+1I blend costs ~850 including all zone foundations and allocated
78 power, netting ~492/min after 9.75 power upkeep at the chosen 2x tax. Per 300
invested this is ~174/min versus ~255/~268 for the three/four-harvester fleets.
The blend is illustrative, not a forced jobs ratio; infrastructure supplies
additional employment. Income starts only after construction and growth.
Stefan explicitly chose 2x instead of the proposed 3x fleet-parity multiplier
because zone taxation has the easier risk profile. No harvester or power rates
were changed, and Palace retains its 1.0.637 income.

## Independent yard and factory allocation (1.0.639)

Stefan explicitly rejected any 3/4-workers-per-refinery production cap. City
heavy factories target remaining-map-spice capacity, bounded only by the map's
harvester limit, not the number of refineries. The old direct 3-per-refinery
factory gate is also removed for vanilla QuantBot; its spice target and refinery
construction policy remain unchanged. Once a worker-capable Heavy Factory exists,
yards do not build spare refineries just for the included worker, even when that
factory is busy with military orders. Exceptions: recover fewer than two workers,
or no factory can supply them. Refineries catch up with existing and queued fleet
throughput; a capacity upgrade that repays its full cost within the forecast takes
precedence even when the tax hedge is short. Mandatory MCV orders retain priority. Military unit selection stays unchanged;
economy-versus-military factory priority is now explicit.

Refinery capacity compares committed workers' predicted income against 75% of
theoretical unloading throughput (~1,757 credits/min/bay), reserving 25% for
manoeuvring/uneven arrivals. The income estimate includes only marginal bay
relief and one included worker, never the full existing fleet. The four-minute
forecast counts complete first/new-worker loads; an existing-fleet throughput
improvement starts after construction and unloading. Risk/spice confidence
applies to both streams. Pending refineries count their promised workers.

Removed the city reserve for hypothetical ratio-driven extra refineries. When
zoning is chosen, leave the affordable parallel worker and combat-unit reserve
funded. Operating generators determine amortized power construction cost, with
wind as fallback and generator concrete included; upkeep stays power/8 per minute.
This is a bounded forecast, not a measurement of local queues or congestion;
travel improvement for existing workers is not separately valued. No fresh
world/path searches or persistent AI/save state were added. Existing structure
census gathers factory pointers; availability is rechecked as queues change.

Telemetry adds refinery_useful, parallel_factory_supply, wanted_included_worker,
worker_income, bay_capacity and generation_cost_per_thousand to the existing
city_economy_comparison event. Policy transport-tax-hedge-v59. Added factory_can_supply, tax_income,
developing_tax_income and forecast_fleet_income.

Factories also balance economic growth against military demand. Below the spice
target, fewer than two committed harvesters gets recovery priority. Otherwise,
while military demand remains and an affordable combat vehicle is available,
prefer army production until committed military value covers twice current
harvester purchase value; then another worker can take a slot. Re-evaluate after
each accepted order, including queued units. At the army target (or no available
combat order), factories can keep growing the workforce to the spice/map target.
This is a soft priority, never a harvester limit, and does not depend on refinery
count. MCV emergencies and existing strategic priority remain ahead of it.
The yard's parallel-production forecast calls this same policy, so a factory
choosing a tank is not incorrectly counted as supplying an extra harvester.
The factory_economy_priority event records both targets and the choice.

## Early transport and continuing tax hedge (1.0.639)

Custom QuantBot (city and vanilla) now orders the first High Tech Factory and
carryall before extra Heavy Factories when enabled tech, active harvesting,
air capacity and a legal site permit. It saves the actual factory price, rather
than requiring 1,000/2,000+ credits. Committed factories/aircraft prevent duplicate
orders. A queued first heavy is enough to start the transport prerequisite.

First carryall production runs ahead of other builders, receives its purchase
funds before optional spending, and waits instead of buying an upgrade or combat
aircraft with those credits. Existing power shortages release the cash reserve
for recovery. An active workforce has a minimum target of one carryall. After
the first transport is committed, normal aircraft/ground priorities resume.
Low-tech, disabled transport, air limits and unplaceable sites do not permanently
block ground expansion. No extra world scan or saved AI state was introduced.

City hedge: target forecast tax >= one third of forecast fleet spice income
(25% of combined gross income), with actual tax plus half low-density estimated
income for demanded developing/queued lots. Suitable demanded zones get an early
priority window alternating every ten simulated seconds, leaving other windows
for normal civic/production ordering. Needed profitable bays still take priority.
This is an AI diversification target, not a tax payout change or harvester cap.
The target uses existing ground-trip forecasts; carryall travel improvement and
observed queue delays are not yet separately measured in the investment model.

## Evidence from the completed 638 four-player match

Local session 1789115118630650-0, map 4P - 192x192 - DuneCity, seed 1640328219,
120-worker lobby cap; finished at cycle 264735 (~70:36 simulated). Neutral won.
At ~20min, each house still had one R, no C/I, but 22–32 refineries. All 106 sampled
refinery choices reported capacity_needed=false. Busy military factories made
the included worker attractive repeatedly; the one-plot hedge never grew. At
~37min the map spice was exhausted, after city investment had begun too late.
Ordos's second/third Heavy orders at 668/1121s preceded first High Tech at 1145s.
Mercenary's fifth Heavy preceded its first High Tech. These observations drove 639.

Further review findings, not changed by 639:
- Only Neutral ordered nuclear, first at 4024.7s (~67:05). Atreides/Ordos/Mercenary
  built 52/49/66 windtraps over the match. Review saving for economic large power
  additions rather than repeatedly using immediately affordable small generators.
- At~50min Ordos had 695 gross tax/min and 575 police expense/min after city losses.
  Consider emergency service-budget adjustment and productive-zone recovery.
- 125 road-step cancellations: 115 already had roads; 12 had unit occupants (counts
  overlap). Current code cancels/refunds that road item and pops one location;
  it does not cancel the remaining building plan. Deduplicate/retry as cleanup.
- Logged AI frame aggregate max 18.9ms; ai.build max 12.1ms. Frame mean 6.64ms/max 170ms,
  380 frames over 33ms and 3 over 100ms. A unit-update scope peaked 159.2ms; this is
  isolated rather than evidence of recurring AI stalls. Timings are nested;
  do not sum their averages. No crash recorded; session_end and game_summary exist.


## Nuclear investment and post-loss service funding (1.0.640)

The 638 follow-ups above are now implemented. Established city AI starts saving
for nuclear at three windtraps' load when production is less than requirement +
growth reserve + one windtrap. A Heavy Factory, unlocked reactor and valid site
are required. First transport remains ahead. Optional production protects reactor
funds; actual power loss releases this protection for affordable recovery. Pending
generation prevents duplicate reactor plans. Existing growth reserve includes
latent zone maturity, including zones that shrank during a blackout.

After at least max(3, remaining buildings/10) losses within three minutes, a city
with <2,000 cash and a police bill exceeding 75% of post-power tax cuts funding
by up to 25 points per 30-second review. The target is half post-power income,
with a 25% floor. At >=5,000 cash or income covering twice nominal police cost,
funding recovers by 25 points. The command applies to its issuing house. This is
an emergency response, not automatic low funding for an intact city.

Redundant finished roads are redirected to connected gaps around owned buildings,
excluding queued locations. If none is useful/free, keep the road ready and retry
with a five-second delay rather than cancelling it. This preserves later building
orders, although a held road occupies the yard until a site becomes available.

## Rich-AI investigation (2026-11-03, protocol 111 staging)

Ten real 30-minute all-QuantBot city games compared `police-recovery-off/on`
in the same native build: 160×160 Fortress starts, 20,000-credit armies,
40,000 starting cities, Medium spice, Hard and Brutal mirrors, seeds 9101 and
148680876; plus 128×128 Medium-base, 2,500-credit-army, no-prebuilt-city Hard
mirrors at seed 9101. Every house started with **3,000 credits**, spice factor
1, the default slow city year and the mod's other defaults. These are ten
30-minute observations, all time limits, not a win-rate experiment.

Raw per-house minute curves, sources, expenditure, queue fronts and attacks:
`~/Documents/projects/outputs/ai-rich-police-20261103/{fortress,medium}-{off,on}/`.
The paired snapshot is source `2456373e6002`; a further full-capture replay of
the richest Hard game is in `capital-trace/` (commit `f23489918a5c`, no working
tree edits, no capture limit). Its final balance and original game digest
reproduce the measured wealthy household.

### Where the credits came from

Hard Atreides, Fortress seed 9101, ended with **37,257** credits:

| Cumulative source or use over 30 game minutes | Credits |
|---|---:|
| Starting grant, identical for every house | 3,000 |
| Refined spice, factor 1 | 159,401 |
| Gross city tax | 147,294 |
| Net city credits actually applied, after policing | 123,834 |
| Withdrawals for construction, units, upgrades, repairs and power | 248,978 |
| Power, already included in those withdrawals | 16,819 |
| Posted police charges, separate from withdrawals | 23,466 |
| Refunds / sales / storage losses | 0 / 0 / 0 |
| Construction Yard income | **0** |

The exact balance is `3,000 + 159,401 + 123,834 - 248,978 = 37,257`.
Posted police charges exceed the actual reduction of gross tax by six
credits: the existing empty-city-treasury floor waives an early deficit
(`House::addCityCredits`), rather than taking it from starting/spice cash.
This small floor effect does not explain the large balance.

Its four Yards supply **4,000 of 42,040 storage capacity**, not income.
`Game.cpp` sets each city Yard's capacity to 1,000; `House::incrementStructures`
adds capacity, while only refinery delivery and city-budget payout add the
measured recurring income. No AI-only starting grant or passive Yard payout
was found.

### Why money accumulates late

Between minutes 20 and 30 the same house banked **31,930** additional credits:
64,812 spice + 72,384 gross tax - 10,363 police - 94,903 other withdrawals.
Thus it was still spending **9,490 credits/minute** (779 of that power), not
forgetting to pay for production; income after police was 12,683/minute.
Tax supplied 52.8% of this window's gross recurring income.

The construction sink reaches a deliberate difficulty limit. At cycle 96,979
the full trace records city population **89,860 + 480 pending**, versus Hard's
90,000 ceiling, **30,927** cash, **59,850** queue-inclusive military versus a
70,000 target, four idle Yards and available building space. Each yard wants
another Repair Yard, but `QuantBotBuildProducers.cpp::produceItemWithLogging`
first calls `QuantBotBuild.cpp::cityAdmitsStructure`. That ceiling includes
government jobs as well as private lots, so the repair investment cannot be
admitted; additional population-bearing factories face the same ceiling.
Zero-population services/power and military production remain available.
The capital comparison also refuses another production lane when existing
four-minute nominal military capacity (50,001 credits here) already exceeds
the remaining army shortfall (`QuantBotSpendingPolicy::additionalProduction`).

This is an **income-versus-deliberate-city/army-cap balance constraint**, not
uncreated build commands or an extra source of AI cash. All five paired games
had active production whenever a house retained 5,000+ credits and a Yard;
none met the audit's four-minute whole-production-idle `unused_credits` flag.
That detector does not mean every individual yard was busy: the capped city's
yard-idle period above is explicitly retained in the evidence.

No spending-cap bypass or income rebalance is implemented by this fix.
**Recommendation:** if smaller mature-city treasuries are desired, A/B the
private-zone multiplier at 1.5× rather than 2× before choosing a balance
change; do not remove Yard storage or the 3,000-credit opening grant. At fixed
population/behaviour, a 25% reduction of this window's tax would remove at most
18,096 of its 31,930 surplus (Palace is exempt from the private-zone multiplier).
That is a bounded estimate, not a played result; ordinary starts in this sample
were cash-constrained, so a universal tax nerf needs wider validation.

### Credit curves and police acceptance

Every Fortress household's OFF/ON credit curve is identical, including final
game digests: these well-funded cities normally remained at 100% police funding.
All start at 3,000; each entry below is the balance at the stated game minute.

| Difficulty | Seed | House | 10 min | 20 min | 30 min |
|---|---:|---|---:|---:|---:|
| Brutal | 148680876 | Harkonnen | 1,217 | 1,479 | 13,633 |
| Brutal | 148680876 | Atreides | 463 | 1,037 | 0 |
| Brutal | 148680876 | Ordos | 1,168 | 518 | 1,267 |
| Brutal | 148680876 | Sardaukar | 735 | 656 | 655 |
| Brutal | 9101 | Harkonnen | 381 | 905 | 0 |
| Brutal | 9101 | Atreides | 7,048 | 2,245 | 5,649 |
| Brutal | 9101 | Ordos | 899 | 2,465 | 6,472 |
| Brutal | 9101 | Sardaukar | 1,647 | 577 | 0 |
| Hard | 148680876 | Harkonnen | 1,998 | 2,175 | 2,164 |
| Hard | 148680876 | Atreides | 1,140 | 836 | 1,169 |
| Hard | 148680876 | Ordos | 1,629 | 6,303 | 32,623 |
| Hard | 148680876 | Sardaukar | 294 | 433 | 0 |
| Hard | 9101 | Harkonnen | 0 | 0 | 0 |
| Hard | 9101 | Atreides | 1,103 | 5,327 | 37,257 |
| Hard | 9101 | Ordos | 2,091 | 5,844 | 860 |
| Hard | 9101 | Sardaukar | 1,044 | 0 | 0 |

Ordinary Medium-base starts, Hard seed 9101, **OFF / ON**:

| House | 10 min | 20 min | 30 min |
|---|---:|---:|---:|
| Harkonnen | 496 / 369 | 1,752 / 945 | 1,184 / 2,208 |
| Atreides | 715 / 463 | 825 / 1,219 | 350 / 563 |
| Ordos | 558 / 574 | 2,076 / 1,490 | 713 / 884 |
| Sardaukar | 947 / 344 | 909 / 248 | 1,205 / 1,026 |

Funding recovery changes this cash-constrained game's subsequent simulation,
so its differing end balances are not an isolated economic-strength claim.
The controlled real-engine budget probe proves the exact cause and fix:
cash 1,000→6,000 takes funding **100→53→81** after the 30-second review;
a small tax-headroom cut takes **100→97→100** after two minutes with recovery
on, while off remains at **97**. Immediate cuts, exact review boundaries,
campaign/helper ownership and actual budget charging all pass unchanged.

Verification on the frozen paired source: `quantbot_police_budget` and the ten
other selected QuantBot/performance tests pass (cluster job
`20261103T202057-ctest-daf8f5`); `dunelegacy_tests` passes on the Windows PC
in `20261103T201949-ctest-746ac5`. The former includes police placement,
shared spending, no-spice city, growth installments, custom attack, strategic
site stalls, all-against-Atreides, scenario smoke, FFA deployment and performance.
