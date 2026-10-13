# Random Skirmish generator (as built)

The Random Skirmish screen builds a playable custom map from a few choices: terrain, start plateaus,
pre-built opposing bases, armies, and DuneCity cities. It writes the map as an in-memory INI in the
map-editor format and starts it through the normal CustomGame path,
`GameInitSettings(mapfile, filedata, ...)`, so saving and loading the match work as usual.

## Where it is
- **Main menu → "Random Skirmish"**, directly below "Custom Game". Deviation from the spec: the
  `SinglePlayerMenu` class is no longer reachable. The main menu is the live single-player menu, so
  the button goes there. Skirmish and Custom Game are unchanged. The optional "Generate…" entry in
  Custom Game was not added.
- Code:
  - `include/MapEditor/SkirmishGenerator.h` and `src/MapEditor/SkirmishGenerator.cpp`: the pure
    generator. No engine globals, no floats, no wall clock.
  - `src/MapEditor/SkirmishCatalogue.cpp`: the only engine-facing piece. It reads prices, power,
    tech levels, builders and prerequisites from the live `ObjectData`.
  - `include/Menu/RandomSkirmishMenu.h` and `src/Menu/RandomSkirmishMenu.cpp`: the screen, plus the
    `RandomSkirmish::{activateMode, loadCatalogue, makeInit, saveMap}` steps. The probe uses the same
    steps.
  - `include/INIMap/ZoneStartLevel.h`, `INIMapLoader::loadStructures` and
    `ZoneStructure::setStartingLevel`: the zone-level map extension.

## Parameters
All of these are saved as last-used values in the configuration file, section `[Random Skirmish]`.

- **Map**
  - Size: 64, 96, 128, 160, 192 or 256. Default 128.
  - Rock: Low 15%, Medium 25%, High 35%, Very high 50%. This is the target share of rock plus
    mountain.
  - Spice: None, Low, Medium, High, Very high. This sets the number of fields, a field between
    neighbouring starts, and the share of thick spice (0/10/25/40/55%).
  - "Mirrored starts".
  - Seed. It is shown and editable, and "New" rolls a fresh one. "Keep this seed" stops a new seed
    from being rolled on each visit. This checkbox is an addition, because "random by default" and
    "persist all parameters" conflict.
- **Game**
  - Your house: Random or any house a custom game can field.
  - Credits: 1,000 / 2,000 / 3,000 / 5,000 / 7,500 / 10,000 / 20,000, applied to every house.
    Deviation: the engine has no existing credit presets, so this list is new. The default of 3,000
    is `DEFAULT_STARTINGCREDITS`.
  - Mode: Vanilla or DuneCity. Each mode activates its mod (`vanilla` / `dunecity`) and that mod's
    effective rules, the same way a new Custom Game does.
  - Tech level 1–9, written to `[BASIC] TechLevel`.
- **Players**
  - Allies 0–3 and enemies 1–5.
  - Total houses are capped at what the engine can field: `isCustomGameHouseAvailable`, which is 8
    in vanilla and DuneCity, or 9 with a registered Custom house. The UI limits the enemy count to
    match.
  - "Enemies are allied" on puts all enemies on team 2. Off puts each enemy on its own team (2, 3, …).
    The player and allies are always team 1.
- **Per side** (Allies / Enemies)
  - AI: every non-support controller in `PlayerFactory`.
  - Base: None (MCV only) / Outpost / Small / Medium / Large / Fortress.
  - Army: 0 / 1,000 / 2,500 / 5,000 / 10,000 / 20,000 credits, or Random. Random is uniform from
    1,000 to 20,000 in steps of 100.
  - City (DuneCity only): None / 1,000 / 5,000 / 15,000 / 40,000 people, or Random. Random is uniform
    from 1,000 to 40,000 in steps of 1,000.
- The human always starts with a Construction Yard and the chosen credits, on a plateau as large as
  the largest opponent's.

## Generation
- **Terrain**
  - Built on `generateRandomMap`. A binary search finds the smallest rock-field count that reaches
    the rock target, with the start plateaus already carved.
  - Rock lying at least 4 tiles from any other terrain, and outside every plateau's 5-tile margin,
    becomes mountain.
  - Every start is checked for ground connection (only mountains block). If a start is cut off, the
    generator retries with seed+1, up to 8 attempts. If all fail, it levels the mountains, which
    always connects the starts, and logs that it did.
- **Start positions**
  - Mirrored mode: point mirroring for 2 houses; otherwise both axes, from the corner and edge slots.
    The terrain is post-processed through `MapMirror`, so it is exactly symmetric, and every mirror
    image of a start is carved too.
  - Normal mode: an evenly spaced square ring with a seeded offset.
  - The player takes slot 0, allies the nearest slots, and enemies the rest.
- **Base templates** (each item is included only if the house can field it at the chosen tech
  level, counting the item's own tech level, its builder and its prerequisites):
  - Outpost: Construction Yard, Refinery, Radar.
  - Small: adds Silo, Barracks or WOR, and Light Factory.
  - Medium: adds a 2nd Refinery, Heavy Factory, Repair Yard and 2 Gun Turrets.
  - Large: adds a 2nd Silo, Barracks and WOR, Hi-Tech, Starport, IX, and 4 Rocket Turrets, with walls
    beside the turrets.
  - Fortress: 3 Refineries, 3 Silos, 2 Heavy Factories, Palace, 4 Gun and 8 Rocket Turrets, and
    longer wall runs.
  - Windtraps are added until power produced is greater than power required, including the city's
    draw.
  - Buildings are packed on concrete, on rock, with a one-tile lane between them. Turrets go on a
    ring two tiles outside the packed base.
- **Armies**
  - Bought at the house's ObjectData prices from the ground combat units it can field.
  - Half the budget is spent on seeded random picks. The rest is filled exactly by a bounded
    knapsack.
  - No unit type may take more than 40% of the budget. When fewer than 3 types are available the cap
    rises to 100/n%, otherwise the budget could not be spent.
  - Units are placed on the nearest open tiles around the base in Area Guard, never touching a
    building or road and never on another house's plateau.
- **Cities**
  - The generator works from the target population divided by the display multiplier (20).
    Population from the base's own city-role buildings is subtracted first.
  - The remainder is filled with R, C, R, I lots at the highest level that fits.
  - A Stadium, Airport or Starport is added when residential, commercial or industrial population
    would pass the engine's civic thresholds (500 / 100 / 70).
  - One Police Station is added per 16 zones.
  - Lots sit in a road grid of 4-zone blocks on the map-edge side of the plateau. City Windtraps sit
    in the grid.
- **Map extension:** an `ID` entry may carry a fifth field, `...,Position,Level`, giving a zone's
  starting level (0–3). A residential lot gets the matching population (16 / 24 / 40).
  - Entries without the field load exactly as before, as empty lots.
  - Older builds read only four fields, so they ignore it.
  - The map editor's loader ignores it too, so a generated map re-saved in the editor loses the
    pre-developed levels.
  - No new saved state, so no save gate and no `SAVEGAMEVERSION` change. Zone density and residential
    population are already saved.
- **Save map** writes `<user>/maps/multiplayer/<N>P - <S>x<S> - Random <a>v<e>[ FFA] <Mode> T<tech> <seed>.ini`.
  The map shows up under Custom Game > MP User Maps and can be hosted. A `[RANDOMSKIRMISH]` section
  records seed, map seed and per-house plan; the loader ignores it.
- If a crowded map cannot fit everything (for example many houses on 64x64), the items that do not
  fit are skipped. They are counted in `HouseSetup::skipped`, and the screen reports them.

## Limits and deviations
- A Fortress's own garrison (Palace, Barracks, WOR) is already about 1,000 people. For the 1,000-person
  preset with a Fortress base, no zones are added and the city starts at the base's own population,
  up to about 1,300. This is the only case outside ±25%, and the unit test pins it.
- At tech level 1 no unit-producing building exists, so the army is empty.
- The city simulation drifts after load. Measured on the probe's 192 map with 15,000-person cities:
  13,080 and 13,440 after two city days, and 12,260 and 12,420 after one minute. Early demand valves
  start negative before job history exists.

## Verification
- `ctest --test-dir build -R skirmish_generator_tests` (Catch2 `[skirmish]`, file
  `tests/SkirmishGeneratorTestCase.cpp`) checks the following against the shipped
  `config/ObjectData.ini.default`:
  - Identical INI text for the same seed and parameters, in both modes, mirrored or not.
  - House count, uniqueness and teams, allied and free-for-all, plus the house cap.
  - Every template item is legal for its house and tech level (tech 1–9, all 8 houses, all tiers),
    and power is positive, city draw included.
  - Armies: 90–100% of budget and the per-type cap, for 5 budgets × 9 tech levels × 8 houses.
  - Every structure, tile entry and unit sits on a legal tile, with no overlaps and unique `GEN`
    keys, across six configurations from 64 to 256.
  - Ground connectivity, including a negative case.
  - Rock share is monotonic and within +12 points of its target.
  - Mirror symmetry.
  - Spice "None" leaves no spice.
  - Every city preset × 5 base sizes × tech 4 and 8 lands within ±25% (one exception, above).
  - Zone-level parsing: bundled maps parse as "no level", and malformed fields are rejected.
  - Last-used parameters round-trip through the config file, and corrupt values are clamped.
- `ctest --test-dir build -R skirmish_generator_probe` (`tests/skirmish/run-skirmish-probe.py`, real
  engine, isolated profiles) runs these cases:
  - **menu:** at 640x480 and 1280x720, checks house choices and the enemy cap, persistence across
    reopening, and that Save map writes the file. Screenshots are saved as
    `build/skirmish-probe/random-skirmish*.bmp`.
  - **legacy-map:** Metropolis loads with all 2,478 zones at level 0.
  - **vanilla-1v1** (64): 7 enemy structures, army 2500/2500, power 100/95.
  - **allied-2v3** (128, Large, 10k): 5 houses with the right teams, 36–37 structures each,
    army 10000/10000 each, positive power. A one-minute played smoke had no problem lines, and a
    save/load round trip kept every structure and team.
  - **city-192** (DuneCity, 15k, tech 8): 59 structures and army 5000/5000 per AI house. Measured
    population after two city days was 13,080 and 13,440 (within 25%). A one-minute smoke had a
    clean log, and save/load passed.
