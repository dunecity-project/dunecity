# Random Skirmish generator (as built)

The Random Skirmish screen builds a playable custom map from a few choices: terrain, start plateaus,
pre-built opposing bases, armies, and DuneCity cities. It writes the map as an in-memory INI in the
map-editor format and starts it through the normal CustomGame path,
`GameInitSettings(mapfile, filedata, ...)`, so saving and loading the match work as usual.

## Where it is
- **Main menu → "Random Skirmish"**, directly below "Custom Game". Deviation from the spec: the
  `SinglePlayerMenu` class is no longer reachable. The main menu is the live single-player menu, so
  the button goes there. Skirmish and Custom Game are unchanged.
- **Hosting → "Random map..."** in the host's map chooser (`CustomGameMenu`): Multiplayer → LAN or
  Internet game, and Custom Game's Offline/Online chooser (Play Online hosting). It opens the same
  screen in multiplayer mode; see [Multiplayer](#multiplayer-random-map) below.
- Code:
  - `include/MapEditor/SkirmishGenerator.h` and `src/MapEditor/SkirmishGenerator.cpp`: the pure
    generator. No engine globals, no floats, no wall clock.
  - `src/MapEditor/SkirmishCatalogue.cpp`: the only engine-facing piece. It reads prices, power,
    tech levels, builders and prerequisites from the live `ObjectData`.
  - `include/Menu/RandomSkirmishMenu.h` and `src/Menu/RandomSkirmishMenu.cpp`: the screen, plus the
    `RandomSkirmish::{activateMod, modeForMod, loadCatalogue, makeInit, saveMap}` steps. The probe uses the same
    steps.
  - `include/INIMap/ZoneStartLevel.h`, `INIMapLoader::loadStructures` and
    `ZoneStructure::setStartingLevel`: the zone-level map extension.

## Parameters
All of these are saved as last-used values in the configuration file, section `[Random Skirmish]`.

Settings with an order (Size, Rock, Spice, Credits, Tech level, Allies, Enemies, and each side's
Base, Army and City) are horizontal sliders (`include/GUI/Slider.h`): lowest on the left, highest
on the right, the chosen step's text beside the track. Click or drag on the track, use the mouse
wheel, or Left/Right when the slider is active. "Random" is the rightmost step of Army and City.
Mod, house, AI and AI partner stay drop-downs.

- **Map**
  - Size: 64, 96, 128, 160, 192 or 256. Default 128.
  - Rock: 15 / 25 / 35 / 50 / 65 / 80 / 90 / 100% target share of rock plus mountain (the first four
    keep their saved indices). Past what the editor's rock fields reach, rock grows outward from its
    edges ring by ring with a seeded coin flip per tile (`growRock`), so coasts stay ragged and
    mirrored maps stay symmetric; at 100% the whole map is rock and mountain. Spice only grows on
    sand, so high rock leaves little (at 100% no) spice. Starts are always clean rock, so a crowded
    map ends above its target: Stefan's 128x128 5-house 40,000-people match measures 67%.
  - Spice: None, Low 6%, Medium 12%, High 18%, Very high 24% of the map (spice, thick spice and
    blooms; Very high matches the map editor's default random map). Thick spice share inside the
    fields 0/10/25/40/55%.
  - "Mirrored starts".
  - Seed. It is shown and editable, and "New" rolls a fresh one. "Keep this seed" stops a new seed
    from being rolled on each visit. This checkbox is an addition, because "random by default" and
    "persist all parameters" conflict.
- **Game**
  - Mod: every installed mod, the same list as Custom Game (Vanilla, DuneCity, Tornie, Dune2R,
    Workshop mods). The match activates that mod and its effective rules the way a new Custom Game
    does; the remaster's artwork gate runs first, as in Custom Game. The generator's mode follows the
    mod: a mod with `Enables City Mode` (DuneCity) generates cities, any other mod is vanilla-style.
    The City rows are shown only for a city mod. An older config without `Mod` falls back to the
    saved mode (`dunecity` / `vanilla`).
  - Your house: Random or any house a custom game can field.
  - Credits: 1,000 / 2,000 / 3,000 / 5,000 / 7,500 / 10,000 / 20,000, applied to every house.
    Deviation: the engine has no existing credit presets, so this list is new. The default of 3,000
    is `DEFAULT_STARTINGCREDITS`.
  - Tech level 1–9, default 9 (everything unlocked), written to `[BASIC] TechLevel`.
  - The saved map's `[BASIC] Mod` is `dunecity` for a city mod, otherwise the chosen mod's content
    category (`tornie` or `vanilla`).
  - "Game rules..." (bottom row) opens the same Game Rules window as Custom Game (game speed,
    concrete, fog of war, explored map, instant build, one palace, unit damage, rocket turret
    power, sandworms, carryall drops, immortal player, unit/harvester/yard caps, plan building
    sites when ordering — the Construction Yard pre-placement queue, default on). Until it is used
    the match takes the chosen mod's effective rules; once confirmed, its rules are used for every
    match started from the screen. "Remember as default" in the window saves them globally.
- **Players**
  - Allies 0–6 and enemies 1–7, at most 7 together (the player plus seven houses). Moving one
    slider past the total pushes the other down. The engine's house cap (`isCustomGameHouseAvailable`:
    8, or 9 with a registered Custom house) also applies; a ninth house does not lift the 7.
  - "Enemies are allied" on puts all enemies on team 2. Off puts each enemy on its own team (2, 3, …).
    The player and allies are always team 1.
- **Per side** (Allies / Enemies)
  - AI: every non-support controller in `PlayerFactory`.
  - Base: None (MCV only) / Outpost / Small / Medium / Large / Fortress.
  - Army (credits): 0 / 1,000 / 2,500 / 5,000 / 10,000 / 20,000 credits, or Random. Random is uniform
    from 1,000 to 20,000 in steps of 100, drawn **once per map** from the seed: every house whose
    Army is Random (the player's, allies' and enemies') gets that same budget. Armies never buy
    light infantry; foot soldiers are Troopers, placed up to five to a tile (the engine's
    `NUM_INFANTRY_PER_TILE`). Vehicles take one tile each.
  - City (DuneCity only): None / 1,000 / 5,000 / 15,000 / 40,000 people, or Random. Random is uniform
    from 1,000 to 40,000 in steps of 1,000, drawn once per map like the army: every house whose City
    is Random gets the same target. (Before, each house drew its own.) The unit mix each house buys
    is still its own, from its own units.
- The player gets their own Base, Army and City from "You start with", plus an optional AI partner
  sharing their house, on a plateau as large as the largest opponent's.

## Generation
- **Terrain** (`shapeRock`, `addSpice`), modelled on the map editor's random map
  (`generateRandomMap`, which itself is unchanged):
  - The base layer is the editor's random map itself (organic rock fields, dunes, rock bits,
    blooms) with no spice. A binary search picks the smallest editor rock-field count that reaches
    the rock target with everything below in place.
  - Scattered rock islands (one per 2,048 tiles, editor-style random walks).
  - Every start's clean core (the rectangle its base and city need) is rock, grown by a rim of
    irregular width: smooth value noise between 2 and 7 tiles, plus a tile of per-tile jitter,
    smoothed with the editor's fill-in rule, plus three editor-style rock lobes walked out from
    beyond the rim. The core is never the outline.
  - Mountains, never within about 3 tiles of a core: massifs in rock at least 5 tiles from the
    sand and a ragged 10–16 tiles from every core, wandering ridges (one per 60 rock tiles
    outside the cores) through rock fields away from the rims, and clumps of outcrops on the rims
    within 3 tiles of the sand. The map border counts as an edge of the rock.
  - Spice fields grow like the editor's: random walks over sand and dunes that turn thick where
    they cross themselves, then the editor's fill-in passes for spice and thick spice, until the
    spice target is reached. The first field goes beside every start (toward the map centre),
    then between neighbouring starts, then anywhere.
  - Mirrored maps paint every change at every mirror image and key all noise and coin flips to
    the mirror-canonical tile, so they stay exactly symmetric.
  - Every start is checked for ground connection. Up to 8 seeds are tried, then mountains are
    levelled. In the unit test's 150-configuration random sweep no map needs levelling.
  - Measured (`render` probe case, 128x128): editor random maps have their longest straight rock
    edge at 11–12 tiles and 45–47 outline corners per 100 outline edges; the old generator had 57
    and 3 on Stefan's map (rectangular plateaus) and 23 and 37 on a vanilla 2v2; the new generator
    has 12 and 53 on Stefan's configuration and 10 and 56 on the vanilla 2v2.
- **Start positions**
  - Mirrored mode: point mirroring for 2 houses; otherwise both axes, from the corner and edge slots.
  - Normal mode: an evenly spaced square ring with a seeded offset.
  - The player takes slot 0, allies the nearest slots, and enemies the rest.
  - Each start owns its core and two tiles around it (cores first): no other house builds,
    zones or parks there.
- **Base templates** (each item is included only if the house can field it at the chosen tech
  level, counting the item's own tech level, its builder and its prerequisites):
  - Outpost: Construction Yard, Refinery, Radar.
  - Small: adds Silo, Barracks or WOR, Light Factory and 1 Rocket Turret.
  - Medium: adds a 2nd Refinery, Heavy Factory, Repair Yard, 2 Gun Turrets and 2 Rocket Turrets.
  - Large: adds a 2nd Silo, Barracks and WOR, Hi-Tech, Starport, IX, 2 Gun and 4 Rocket Turrets, with
    walls beside the turrets.
  - Fortress: 3 Refineries, 3 Silos, 2 Heavy Factories, Palace, 4 Gun and 8 Rocket Turrets, and
    longer wall runs.
  - Rocket Turrets fall back to Gun Turrets where the house or tech level cannot build them.
- **Power** (`addPower`): base and city together, all on the base side, production at least a
  tenth above requirement (the buffer the AI's city-power rule keeps). Where the house can build
  `Structure_NuclearPlant` (DuneCity, tech 6+, the Catalogue's real power value, 2,000), reactors
  carry the load and at most 4 wind traps top up; a larger remainder takes another reactor, and
  one wind trap always stays (the reactor's prerequisite). Vanilla and tech 1–5 use wind traps
  only. Stefan's 40,000-people cities with Medium bases went from 19 wind traps each to 1 reactor
  and 1 wind trap (power 2,100 / 1,895 in the engine).
- **Base layout** (`layoutBase`): the Construction Yard at the heart of the base area, then every
  building takes the free spot with the best score: close to the yard, refineries toward the
  nearest spice, silos beside refineries, factories toward the nearest enemy start, wind traps
  and reactors behind; seeded jitter of up to two tiles keeps spacing irregular. Every building
  stands on concrete and keeps a one-tile lane; the base may spread onto its rim but never around
  the city. Reactors keep the AI's blast spacing (footprint distance 5) from the Construction
  Yard, factories, refineries and the other buildings `TacticalSafetyPolicy` protects, where the
  base has room. Turrets take spots 2–3 tiles outside the buildings, at least 3 apart, on the side
  facing the enemy (relaxed to 2–5 and 2 apart in a crowded base), with wall runs across the
  approach.
- **Armies**
  - Bought at the house's ObjectData prices from the ground combat units it can field.
  - Half the budget is spent on seeded random picks. The rest is filled exactly by a bounded
    knapsack.
  - No unit type may take more than 40% of the budget. When fewer than 3 types are available the cap
    rises to 100/n%, otherwise the budget could not be spent.
  - Units wait in Area Guard on the nearest open tiles in front of the base, toward the enemy,
    never touching a building or road and never on another house's start.
- **Cities**
  - The generator works from the target population divided by the display multiplier (20).
    Population from the base's own city-role buildings is subtracted first.
  - The remainder is filled with R, C, R, I lots at the highest level that fits.
  - A Stadium, Airport or Starport is added when residential, commercial or industrial population
    would pass the engine's civic thresholds (500 / 100 / 70).
  - One Police Station is added per 16 zones.
  - Districts (`arrangeCity`) in whole columns of road-grid blocks (four 2x2 lots, roads on every
    fifth line) from the base side outwards: Industrial, Commercial, Residential. Industrial is
    packed into one block-connected district centred along its column; Commercial and Residential
    are spread evenly along theirs. A Commercial column always separates Industrial from
    Residential (6 tiles apart on Stefan's configuration; the unit test requires at least 5;
    his old map had I lots touching R lots). Columns are as tall as the Industrial or Commercial
    district needs (at most 8 blocks), so every simulation trip (R→C, C→I, I→R from the first ring
    road, at most 20 road steps) finds its destination; the unit test replays that rule for every
    lot. Police take the empty lots that cover the most homes and shops. On a crowded map a city
    only uses tiles nobody else owns.
- **Map extension:** an `ID` entry may carry a fifth field, `...,Position,Level`, giving a zone's
  starting level (0–3). A residential lot gets the matching population (16 / 24 / 40).
  - Entries without the field load exactly as before, as empty lots.
  - Older builds read only four fields, so they ignore it.
  - The map editor's loader ignores it too, so a generated map re-saved in the editor loses the
    pre-developed levels.
  - No new saved state, so no save gate and no `SAVEGAMEVERSION` change. Zone density and residential
    population are already saved.
- **Save map** writes `<user>/maps/multiplayer/<N>P - <S>x<S> - Random <a>v<e>[ FFA] <Mode> T<tech> <seed>.ini`.
  The map shows up under Custom Game > MP User Maps and can be hosted (its houses are fixed; the
  multiplayer Random map's starts are open). A `[RANDOMSKIRMISH]` section records seed, map seed
  and per-house plan; the loader ignores it.
- If a crowded map cannot fit everything (for example many houses on 64x64), the items that do not
  fit are skipped. They are counted in `HouseSetup::skipped`, and the screen reports them.

## Multiplayer Random map
Stefan's ask: "make random skirmish available for multiplayer … players can join and replace ai so
no enemies and allies, you set that in the next screen where the players join after map is
generated", and "random settings on army and city must apply equally to each player".

- **Flow:** the host opens Custom Game as usual (LAN / Internet game, or Custom Game → Online) and
  presses **Random map...** beside Load/Next. The **Random Map** screen
  (`RandomSkirmishMenu(hostMod)`) offers:
  - Map: Size, Rock, Spice, Mirrored starts (on by default here, saved separately as
    `Multiplayer Symmetric`), Seed, Keep this seed — the same controls as single player.
  - Game: the host's mod (shown, not chosen: the map chooser's Mod box stays the one mod picker),
    Credits, Tech level. Game Rules stay on the map chooser's Game Options / Game Rules button.
  - Players: **Start positions** 2–8 (and within the engine's custom-game house cap).
  - **Every house starts with** Base / Army / City, applied to every start (Random draws once).
  - No allies/enemies counts, no house, no AI type, no AI partner: the lobby decides those.
  - **Next** generates, refuses a map whose starts would not be equal (anything skipped on a
    crowded map; it asks for a larger map, a smaller start or fewer starts), saves the map to the
    user `maps/multiplayer/` folder exactly like Save map, and closes. The map chooser switches to
    MP User Maps with its filters cleared, selects the file (the online setup also adds it to its
    map list) and continues as its own Next does, into `CustomGamePlayers`.
- **Map format:** the starts are generic `[Player1]..[PlayerN]` sections (with `Credits=`), and
  every structure, tile and unit belongs to `PlayerK`, as bundled multiplayer maps declare them.
  The lobby counts them (`MapPlayerSectionUtils`) and shows N rows; humans join and take rows,
  empty rows can be AI or closed, houses (or Random) and teams are chosen there. `INIMapLoader`
  binds each playing house to one of the Player sections with the match's synchronized random
  generator, as for any bundled multiplayer map. Map transfer, checksums, saves and replays are
  the normal ones: nothing in the lobby, network or save format changed (protocol 74, save 9860
  untouched).
- **Equal starts:** each start is planned with `startCatalogue()`: an item is used only if every
  custom-game house can field it at the chosen tech level, at the slowest house's unlock level,
  the dearest price and the least favourable power. So whichever house sits on a start can build
  and field what it holds. All starts share one plan and one army stream: the same buildings,
  the same units, the same army value and the same city (same lots and levels). Only positions
  differ, laid out on each start's own plateau, and the wall wings beside turrets, which stop
  where the rock does. Every start is its own team for the layout (bases face the nearest start).
- **Mirrored:** 2 starts are point-mirrored, more use both axes, exactly as single player; every
  start's plateau is a mirror image of the others' for 2 and 4 starts (8 adds the edge slots).
- **Name:** `<N>P - <S>x<S> - Random <N> starts <Mode> T<tech> <seed>.ini`; `[RANDOMSKIRMISH]`
  records `Starts=N` and each `PlayerK=Open,<Base>,Army<spent>/<budget>,City<estimate>/<target>`.
- **Persistence** (`[Random Skirmish]`): `Multiplayer Starts`, `Start Base/Army/City`,
  `Multiplayer Symmetric`; the hosted screen never writes `Mod`/`Mode`, so the single-player screen
  keeps its own mod. Both screens keep each other's choices.

## Limits and deviations
- Multiplayer starts use only what every custom-game house can field, so a house-specific item
  (where a mod restricts one) never appears on a hosted random map even for the house that has it.
- Multiplayer starts are equal in contents, not in placement: each start's base is laid out on
  its own plateau, and wall wings beside turrets stop where the rock does.
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
    keys, across six configurations from 64 to 256, Stefan's configuration, and a 150-configuration
    seeded sweep over every choice (which also requires connected starts and no levelled mountains).
  - Ground connectivity, including a negative case.
  - Rock share is monotonic and within +12 points of its target; spice share within 3 points of
    its target for every preset over three seeds, monotonic, with thick spice.
  - Terrain looks like the editor's: longest straight rock edge at most 16 tiles, mountains and
    dunes present, every start's rim reaches at least 2 tiles past its core and changes width at
    least once per 8 edge tiles, and the core stays clean rock.
  - Mirror symmetry.
  - Spice "None" leaves no spice.
  - Every city preset × 5 base sizes × tech 4 and 8 lands within ±25% (one exception, above).
  - Zoning: Industrial lots at least 5 tiles from every Residential lot, one connected Industrial
    district per city, every R/C/I trip reaches its destination by road within 20 steps (the
    simulation's rule replayed), every R/C lot within 23 tiles of a police station.
  - Power: reactors where available (tech 6, 8, 9, all houses), at most 4 wind traps, wind traps
    only before tech 6 and in vanilla, positive power for every generated base including the
    player's; reactors blast-spaced from protected buildings.
  - Bases: the Construction Yard is among the most central buildings, and positions spread over
    many rows and columns.
  - Zone-level parsing: bundled maps parse as "no level", and malformed fields are rejected.
  - Last-used parameters round-trip through the config file, and corrupt values are clamped.
  - Random Army/City (`[random]`): over 12 seeds on a 6-house DuneCity map, every house set to
    Random shares one army budget and one city target, in range, deterministic per seed; a side
    with a preset keeps it while the Random sides still share; different seeds draw at least
    3 different sizes.
  - Multiplayer (`[multiplayer]`): 2, 3, 4, 6 and 8 starts (64–192, vanilla and DuneCity,
    mirrored or not, Random army/city included, two seeds each): N open starts in order, no
    house section, `Player1..N` sections the lobby counts with the map's credits, every
    `[STRUCTURES]`/`[UNITS]` owner a `PlayerK`; every start holds the same building and unit
    multiset, tile entries (walls aside), army value and city target/estimate, nothing skipped;
    every placed building and unit is available to every house; legal tiles and connected starts;
    mirrored terrain and mirrored plateaus. `startCatalogue()` equals the slowest house's unlock
    level and dearest price for every item. The hosted screen's choices round-trip without
    overwriting the single-player mod or layout; starts clamp to 2–8 and the house cap.
- `ctest --test-dir build -R skirmish_generator_probe` (`tests/skirmish/run-skirmish-probe.py`, real
  engine, isolated profiles) runs these cases:
  - **menu:** at 640x480 and 1280x720, checks house choices and the enemy cap, persistence across
    reopening, and that Save map writes the file. Screenshots are saved as
    `build/skirmish-probe/random-skirmish*.bmp`.
  - **legacy-map:** Metropolis loads with all 2,478 zones at level 0.
  - **mp-lobby:** the multiplayer path on the real menus and engine, DuneCity, 128, 4 starts,
    Medium, Random army, 5,000 cities, seed 20261015. LAN-host Custom Game shows Random map...
    (single-player Custom Game does not); the Random Map screen has none of the single-player
    controls, mirrors by default and offers 2–8 starts (screenshot `mp-random-map-1024x768.bmp`);
    Next saves exactly the generated map; the LAN host's map chooser selects it
    (`mp-map-chosen-1024x768.bmp`) and the Online setup's Next hands it to its players step.
    The lobby opened as `onNext` opens it shows 4 rows; the host sits in row 1, a guest is seated
    as a join seats it, two QuantBot Easy fill the rest, houses Atreides/Harkonnen/Random/Ordos,
    teams 1/2/1/2 (`mp-lobby-1024x768.bmp`). The guest's lobby is built from the host's game info
    read back from the wire format (40,781 map bytes, unchanged) plus the host's roster events,
    and starts byte-identical settings. Both peers start the match: 4 houses, each on its own
    start with exactly that start's 27 structures and 34 units, 5,000 credits, the lobby's teams
    and controllers, each peer controlling its own house; after one minute (3,750 cycles, clean
    log) both report the same state digest. This is two peers in one process: the repository's
    only socket-level two-peer harness (`tests/network/run-late-join-probe.py`) needs a local PHP
    signalling service and WebRTC and is not part of ctest, so it was not run.
  - **vanilla-1v1** (64): 8 enemy structures, army 2500/2500, power 200/95 (a tenth spare means
    2 wind traps). Every case also checks players per house and the player's own base.
  - **allied-2v3** (128, Large, 10k): 5 houses with the right teams, 36–37 structures each,
    army 10000/10000 each, positive power. A one-minute played smoke had no problem lines, and a
    save/load round trip kept every structure and team.
  - **city-192** (DuneCity, 15k, tech 8): 52 structures, 1 reactor and 1 wind trap, power
    2,100/878, army 5000/5000 per AI house. Measured population after two city days was 14,720
    and 14,360 (within 25%). A one-minute smoke had a clean log, and save/load passed.
  - **stefan-128** (Stefan's played configuration, 40k cities): 111 structures, 1 reactor and 1
    wind trap, power 2,100/1,895 per AI house. Population after two city days 37,720 / 38,600 /
    38,280 / 38,880 (target 40,000). Clean one-minute smoke, save/load passed.
  - **partner-support** (player Medium base, 5k army, 5k city, qBotSupportMedium partner): the
    player's house holds exactly HumanPlayer + qBotSupportMedium, 25 structures with the
    Construction Yard, power 2,100/396, army 5000/5000; population 5,000 and 4,680 after two
    days; clean one-minute smoke and save/load. In-game screenshots
    `build/skirmish-probe/ingame-player-base.png` and `ingame-player-city.png`.
  - **partner-quantbot** (vanilla 96, qBotMedium partner): loads with HumanPlayer + qBotMedium in
    the player's house, power 300/200.
  - **render:** whole-map PNGs (4 px per tile) and measurements of two editor random maps and
    the generated Stefan and vanilla 2v2 maps; every match case also writes `loaded-<case>.png`
    and, after its smoke run, `played-<case>.png`. `--render-save <file.dls>` draws the map a
    saved match started from (the save itself needs its exact mod revision, which an isolated
    profile cannot fetch).
