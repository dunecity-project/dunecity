# Civilization Wars

**Civilization Wars** is a small, ordinary DuneCity RTS match with a three-lane battlefield.
Open it directly below **Random Skirmish** on the main menu. Units are built and commanded
normally; the city simulation is the economy. The Warcraft III map inspired the layout, not
its banks, eras, capitals, automatically spawned armies or income nodes: none of those are
added here.

## Generated battlefield

- **Teams:** 1v1, 2v2 or 3v3. The left team is Team 1, the right Team 2. Offline, the player
  takes the uppermost left seat and AI fills every other seat. **Spectate** gives that seat to
  its selected AI too. Allies and enemies have independent AI/difficulty selectors.
- **Size:** 129×96, 161×112, 193×128 or 255×160. Odd widths are deliberate: an odd number of
  worms needs an actual centre tile, rather than a centre line between two tiles.
- Each end is an all-rock plateau, roughly a quarter of the map's width. Three horizontal
  sand lanes connect them. Two mountain ridges run continuously across the middle, from
  one plateau to the other. The ridges do not cut the plateaus: teammates can change lanes
  behind their front. These are normal Dune mountains: vehicles cannot cross, infantry can.
- A buildable rock island sits halfway along each lane, with sand routes around it. Spice
  fields and some blooms grow on lane sand, never over the rock ends or the islands. The
  Spice slider ranges from None to Very high.
- The seed changes ridge wiggle, island outlines and spice patterns. Every terrain tile,
  starting position, building footprint, road, foundation, aircraft and worm origin is
  mirrored left-to-right. **New** rolls another seed; **Keep this seed** retains it for the
  next visit. Choices live in the configuration's `[Civilization Wars]` section.
- In **3v3**, seats face the top, middle and bottom lanes respectively. In **2v2**, the upper
  seat covers top and middle mouths and the lower seat covers the bottom. In **1v1**, each
  player covers all three mouths. Both teams use exactly the same allocation.

The pure generator is `MapEditor/CivWarsGenerator.{h,cpp}`. It uses integer simulation data
and a supplied Random Skirmish catalogue, not engine globals, wall clock or floating point.
The result is a normal rectangular INI map, with the zone-level extension already used by
Random Skirmish cities.

## Starting setup

Every player starts with **0 credits** and **tech level 1**, with:

- one Construction Yard;
- one built **Research Lab**, available to rebuild from level 1;
- one Refinery and one permanent Carryall;
- two rocket turrets at each lane mouth that seat covers;
- **two additional core rocket turrets**, covering both the Construction Yard and Research
  Lab against early ornithopters;
- windtraps sufficient for the entire kit, including all turrets, the lab and developed zones,
  with spare capacity;
- three mature Residential lots, one Commercial lot, one Industrial lot and one Police
  Station, with road access and an industrial/residential buffer. This is a small tax base,
  intended to cover the starting police/turret upkeep and leave a small income.

**No extra harvester is written to `[UNITS]`.** The engine's existing first-Refinery rule
(`House::placeStructure`) gives a preplaced Refinery a free Harvester, delivered by a temporary,
non-owned Carryall, even at tech level 1. This delivery aircraft is distinct from the permanent
starting Carryall. The normal buildable Harvester requires a Heavy Factory, which is not a
tech-1 building; new Refineries also retain their normal Harvester grant.

The generated match uses the **CivWars** mod and enables Research Lab, respawning sandworms,
trooper anti-air and city effects. Other settings are available in **Game Rules**. Tech progression
is the Research Lab's per-house tech-level system; no new resource, era or unit-control system is
introduced.

## Sandworms

**Worms / lane** is 0–4, default 1: total starting worms are three times that value. Each lane
gets the same count. Two-worm pairs are mirrored about the vertical centre column; an odd
unpaired worm starts on that column. All origins are sand tiles, outside the central islands.

Ordinary sandworm respawn picks random positions across the whole map. Generated Civilization
Wars maps instead carry `[CIVILIZATIONWARS]` with `Version=1`. An untamed worm's first valid
deployment on such a map records a permanent origin. After defeat or its usual three meals,
it sleeps as usual, then wakes at **that same origin**. If ground or another underground unit
occupies the tile, it takes another nap and retries that slot; it never falls back to map-wide
random placement. The slot survives movement and saving/loading. The shared 9881 save format
stores it; older worm records read no extra fields.

This preserves per-lane, mirrored **spawn slots**, not synchronized combat waves: worms move
normally and individual defeat/eating events retain their normal randomized sleep duration.
Live positions and the number currently awake need not mirror after play begins. Mountain and
rock barriers keep each lane's worms on its sand.

## Multiplayer

**Host Online** opens the normal Custom Game host setup using the same choices. Its generator
writes generic `[Player1]`…`[PlayerN]` starts, with `Brain=Team1` / `Brain=Team2` defaults. The
player setup and existing lobby assign controllers and houses; those starts retain the fixed
left/right team defaults. No gameplay relay or alternate network path is introduced.

You can also select **Civilization Wars** as the Custom Game mod and choose **Random map...**,
or use Play Online's **Create Random Skirmish** while CivWars is active. Both open the
Civilization Wars generator. **Save Map** writes an open-start scenario to MP User Maps for
later use. Generated maps classify as DuneCity because they contain city buildings.

The pregame player setup and prepared host roster are heap-owned, like the guest roster.
This avoids overflowing Windows' default 1 MiB thread stack when **Host Online** nests the
large player menus; their destruction order, match rules and network protocol are unchanged.

## Use the rules on your own scenarios

The installed, selectable mod folder is `mods/CivWars`; its displayed name is **Civilization
Wars**. Its content base is `dunecity`, and it explicitly enables the city simulation. The
bundled game options select **Research Lab = true**, **Sandworms Respawn = true** and
**Trooper Anti-Air = true**, plus the normal DuneCity economy defaults. DuneCity content
capabilities (including the trooper anti-air gate) apply to it through that content base.

Choose this mod in Custom Game, campaigns or your own scenarios to use those rules without
using the generator. **No starting buildings, credits or units are injected.** The map/lobby's
tech level is preserved: the mod does not force level 1. Arbitrary scenarios without the
versioned generator marker retain the ordinary random sandworm respawn policy. Their authors
choose their own terrain and starting economy, and may place or build the Research Lab normally.

The unmodified installer-managed CivWars payload is an approved online mod like DuneCity.
Approval hashes the actual bundled rules and metadata; edited or derived copies do not inherit
approval merely by claiming its name or `Base Mod`. Existing approved-mod tokens are unchanged;
CivWars uses the next hexadecimal token slot.

## Checks

- `civilization_wars_tests`: seeded generator matrices for all team counts, all sizes and all
  worm counts, including exact mirroring, building footprints/power, lane connectivity and
  separation, islands, roster and map serialization; mode-marker/cache/wire-format unit tests.
- `civilization_wars_probe`: real menus, persisted seed, generic-start lobby and guest settings;
  an all-QuantBot game for each team size through a three-game-minute limit; starting kit,
  powered turrets, normal Harvester grant and positive starting city budget; worm defeat,
  eating, occupied-slot retry, legacy RNG and sleeping save/load behavior.
- `menu_navigation_probe` includes the main-menu destination, selectable mod activation and
  online generator-to-player setup flow. `ui_gallery_probe` renders and lints the play,
  Spectate and online map screens, including 640×480 desert corner-crest clearance.

Runtime evidence and screenshots for the implementation are recorded in the unreleased
HANDOVER entry and `/Users/stefan/Documents/projects/outputs/civ-wars/`.
