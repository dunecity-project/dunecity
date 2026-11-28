# Infantry squads, more Barracks and WOR, infantry in the mix, taxable infantry housing

Stefan, 2026-11-28: "in the original dune you could build three troopers for a discount - i think maybe 3 were
200 credits?", then, asked whether to bring them back: "yes and the ai should use it if it wants to build these
men. also additional infantry and wor should be built if the ai needs more capacity similar to factories. also
include infantry in the calculation for what to build (same performance algorithm). also both wor and infantry
barracks count as a high density residential and is taxable".

## Rules (every game using the default object table)

| Item | Builder | Price | Build time | Men |
|---|---|---|---|---|
| Soldier | Barracks | 30 | 32 | 1 |
| **Infantry** | Barracks | **50** | 32 | 3 Soldiers |
| Trooper | WOR | 100 | 56 | 1 |
| **Troopers** | WOR | **200** | 56 | 3 Troopers |

- 200 for three Troopers is Stefan's number.
- 50 for three Soldiers is the integrator's number. The original squad cost 100 against a 60-credit Soldier.
- A squad builds in its single's time, as the original squads do: `mods/Tornie/ObjectData.ini` and
  `mods/Jericho/ObjectData.ini` give Infantry 32 and Troopers 56.
- A squad order puts three ordinary Soldiers or Troopers on the map, deployed together (`BuilderBase::update`,
  the Dune Legacy squad code). Garrisons (four men a zone), capture, Hospital beds and the levy count men, as
  before. The singles stay buildable.
- Tech level, prerequisites and houses are the singles'. Harkonnen have no Barracks, and Atreides no WOR.
- **Barracks and WOR are taxable top-density housing in a city game.** Each counts as a level-3 Residential
  zone from the day it stands: 40 residents and 80 eighths of tax base, the same as a level-3 Residential zone
  (40 x 2). Until now they were Residential but grew from level 1 and paid no tax (`CityEffects.h`:
  `isInfantryHousing`, `effectiveCityLevel`, `taxablePopulationEighths`).

- **Moderate land value** (Stefan: "and they moderately increase land value"). Each Barracks and WOR stamps
  land value as the Palace and Stadium do. The numbers are the integrator's: +13, a third of the Palace's 40,
  over 6 tiles (the Palace's is 8), with linear falloff. It stacks with the Palace, Stadium, Park and turret
  stamps, all capped at 250.
- **Half a Police Station** (Stefan: "and infantry and wor have 50% police effect"). Police coverage is 500,
  against a station's 1000. It uses the same diffusion as a station and is scaled the same way by Police funding
  and power.
  - No upkeep of its own.
  - It is not a Police Station anywhere else: not in the AI's station counts or `TURRET_LEDGER police_stations`,
    and Police Reinforcements still need a Police Station.
  - QuantBot's police planning sees its coverage as it sees a turret's.
  - Lower crime also raises land value, so a Barracks raises it twice: by its own stamp and through its police
    effect.

## QuantBot (each switch default off until measured)

- **SquadOrders.** Every QuantBot infantry order takes the offer cheapest per man, which is the squad at these
  prices. That covers the Barracks and WOR passes, the Trooper anti-air floor (`supportTrooperOrder`, which
  already compared per man), the capital-candidate loop, and the Zone Garrisons levy. With the switch off,
  QuantBot never orders Infantry or Troopers, so enabling the items left the default AI unchanged. The AI
  Player and the campaign AI did buy them, from their existing priority tables.
- **InfantryLanes.** Another Barracks or WOR is built by the rule that adds Light and Heavy Factories
  (`QuantBotBuildPolicy::needsProductionLane`). The rule fires when every lane is built and at least 75% are
  busy, the infantry share's deficit is at least the lane's price, and the house holds the lane's price plus
  1000 over its reserve. It builds a WOR once Troopers are fielded, else a Barracks. No other cap. The
  Construction Yard rule is `infantry_unit_backlog`, right after `light_unit_backlog`.
- **InfantryInMix.** Since 2026-11-26 (`938eae7c7`) infantry has been one more slot of the unit mix
  (`UnitMixPolicy::measuredInfantryBps`, the same performance score as the vehicle slots). In a city game,
  though, the Barracks never built, and the WOR and the capital loop bought infantry only while hostile
  aircraft were seen (`cityAntiAirInfantryDemand`). With the switch on, a city's Barracks and WOR buy to the
  measured share, inside the city working reserve. The debug line "Adaptive unit mix" now prints Infantry's
  reward, losses and share.

## Match log

- `INFANTRY_ORDER` (engine): every foot-soldier order, with its price, its men, and whether the builder
  offered the squad.
- `INFANTRY_SQUAD event=delivered` (engine): the men a squad put on the map.
- `INFANTRY_MIX` (QuantBot, once a game minute): the measured share, its opening, the score, the infantry and
  army value, and `on_share`.
- `INFANTRY_LANE` (backlog, ordered, standing).
- `INFANTRY_BUY` (reason `city_infantry_share`).
- `CITY_INFANTRY_HOUSING` (once a city year): the Barracks and WOR of a house, their residents and their tax
  base.

`vanilla-match.py` collects these into `infantry.csv`. `tests/ai/activation-events.json` has the fire and
success events of the play styles `squad-orders`, `infantry-lanes`, `infantry-in-mix` and `infantry-squads`
(all three switches).

## Tests

- `infantry_squad_probe` (dummy driver, player path): the build lists and their prices; one click on
  Troopers costs 200 and puts three Troopers on the map, and one Move click sends all three; one click on
  Infantry gives three Soldiers for 50; the singles; a Barracks and a WOR ordered from the yard list and
  placed with a map click add 80 tax eighths and 40 residents, and take them back when destroyed; a Barracks or
  WOR raises nearby lots' land value by 20-50% of a Palace's on the same spot; its police coverage is 0.50
  (+-0.02) and its crime drop 0.50 (+-0.10) of a Police Station's, also at 50% funding.
- `infantry_squads_real_game` (three rs160 games, style `infantry-squads`): ACCEPT S1-S5, one per request.
- `CityEffectsTestCase` "Barracks and WOR are taxable top-density residential".
