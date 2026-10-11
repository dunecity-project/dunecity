# Building condition and concrete

Local implementation: 1.0.780. These are DuneCity engine rules, shared by the
Vanilla/DuneCity/Dune2R/Tornie modes; they are not a claim that every economic
mechanic matches Dune Dynasty.

## Player setting

Game Rules contains **Concrete is required**. Checked enables slab construction,
missing-foundation placement damage and periodic foundation decay. Unchecked
hides slabs and rejects player slab placement; new buildings incur neither
foundation penalty. Roads and authored scenario foundations remain supported.
DuneCity defaults unchecked; Vanilla defaults checked. Explicit saved player
choices remain overrides. Power-shortage damage is independent of this setting.

## Health effects

Standard and advanced windtraps now produce `floor(nominalPower * HP / maxHP)`.
This follows Dynasty's enhanced rule, without original Dune II's 50% output
floor. Repairs restore output immediately; destroying or removing the generator
removes its remaining contribution. Saved totals are rebuilt on load.

In city mode, damage proportionally reduces the final land value of every
building's footprint, after amenities and hostile-threat effects. Half health
means half the otherwise-calculated value (integer rounding, minimum 1 on
developed land). Shared 2x2 land-value cells use the worst building condition,
not stacked multipliers. Repairs restore value at the next city effects scan.
Damaged walls also devalue their occupied land, although foundations never
damage walls. This affects existing land-value growth gates and the
owner's average land value used for tax revenue. It adds no new saved state.

## Which buildings do not directly slow down from damage?

No ordinary building is completely unaffected by missing foundations when the
setting is enabled: it loses durability, and in city mode it also loses land
value. The following functions do **not** directly scale with building health:

| Building | Function unaffected directly | Concrete decision |
| --- | --- | --- |
| Spice Silo | Storage capacity | Optional outside city mode; value/protection inside city mode |
| Radar/Outpost | Radar and prerequisite unlock | Same; power availability is a separate requirement |
| House IX | Technology unlocks | Same |
| Starport | Purchase/delivery throughput | Same; vulnerable deliveries justify protection |
| Repair Yard | Unit-repair speed | Same |
| Palace | Special-ability timer | Protection and city value; no repair-credit saving under the current high-HP repair formula |
| Police Station | Police coverage (funding, power and connectivity still matter) | City land value and resilience |
| Stadium | Amenity bonus | City land value and resilience |
| Airport | Civic/transport benefits | City land value and resilience |
| Residential, commercial, industrial zones | No separate direct HP multiplier on population | Growth and taxes are affected indirectly through land value; foundation priority in city mode |
| Gun/Rocket Turrets | Firing cadence | Always protect foundations when required: durability is critical to defence |

Mod-only equivalents without direct health scaling include Tech Center's spawn
timer, Love Factory's import function and Scoutpost variants' output/functions.
Worfinery unloading and ordinary factory production are health-sensitive. Chaos
Factory uses the ordinary health-sensitive production path. Advanced windtraps
use the same new health scaling as standard windtraps.

Walls do not suffer placement-health or foundation-decay penalties. Roads and
slabs are tile states rather than damageable buildings. They do not need slabs
beneath them.

Construction yards are a special case: placement itself does not remove half
their health, but missing-foundation decay can subsequently damage them and
slow their production. An MCV deployment is not an ordinary yard-built building
with a preceding slab order.

The Palace retains its land-value amenity bonus, but does not satisfy the
stadium requirement for residential growth above 500 population. An actual
stadium is required to remove that civic growth cap.

## Economic interpretation

For tightly fitted foundations at the default full-health yard speed, covering
a 2x2 footprint costs 20 credits
and takes 3.84 seconds with one large slab, versus 15.36 seconds with singles.
A 3x2 footprint costs 30 credits and takes 11.52 versus 23.04 seconds. A 3x3
footprint costs 45 credits and takes 23.04 versus 34.56 seconds. Placement/AI
handling adds latency; a damaged yard slows production. Tech/mod availability
and Instant Build can change this comparison. QuantBot now prefers large-slab
overhangs: a clear 3x2 footprint uses two large slabs (40 credits, 7.68 seconds),
trading 10 extra credits for one fewer construction/placement operation.

Building bare then repairing immediately can deliver urgent production sooner,
because repair and manufacturing run concurrently in this engine. It costs more
than slabs for most ordinary buildings. This is **not** Dynasty's repair timing:
Dynasty pauses factory production during building repairs.

Leaving a building at exactly half health avoids continuing foundation damage,
but loses durability, factory speed/refinery unloading, windtrap output and city
land value as applicable. Concrete outside city mode only saves repair credits if the building would
otherwise be repaired. The current repair
formula has integer `512 / maxHP`; buildings above 512 HP can repair for zero
credits (while still requiring at least 5 credits to keep repairing). Do not
justify Palace/Nuclear foundations with nonexistent repair-credit savings.

The first construction-yard upgrade costs 200 credits with the default 400-credit
yard and occupies it for about 9.6 seconds when continuously funded. One 2x2 slab
saves 11.52 seconds over four singles, so its manufacturing-time investment is
recovered on the first fully covered 2x2 footprint. The upgrade unlock still
requires the configured tech level (default level 4), and delaying the first
refinery to buy it can be a worse trade than accepting single slabs briefly.
The requested QuantBot policy now prioritises this upgrade before new yard
construction as soon as the configured technology permits it, including before
income infrastructure. Missions where bulk slabs are still locked use singles.

For the three 100-credit, 200-HP zones, repairing the initial missing-foundation
health costs about 15.6 credits, less than their 20-credit concrete footprint.
Their reason for foundations is immediate full land value and avoiding repair
latency/maintenance, not a universal claim that concrete is always cheaper.
With DuneCity's default disabled-concrete setting, none of this foundation
expense or latency is incurred.

Relevant implementation: `WindTrap.cpp`, `AdvancedWindTrap.cpp`,
`StructureBase.cpp`, `BuilderBase.cpp`, `House.cpp`, `CityEffectsRuntime.cpp`,
`CityEffects.h`, `QuantBotBuild.cpp`, `QuantBotPlacement.cpp`, and `QuantBotBuildPolicy.h`.

## QuantBot decisions

The 1.0.777 policy supersedes the earlier selective foundation and late repair
rules. With concrete required, QuantBot upgrades for bulk slabs first when
technology permits and fully founds ordinary buildings. Oversized slabs can
cover the final narrow strip beside a footprint. It repairs health-sensitive
and free-to-repair buildings without the rich-cash threshold; city mode extends
that to all structures because of land value. See
[QuantBot foundations, repairs and power](quantbot-foundation-repair-power.md)
for the policy and Vanilla power investment criteria.
