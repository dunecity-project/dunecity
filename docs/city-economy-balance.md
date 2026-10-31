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
