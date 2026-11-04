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
  lanes connect them. A continuous **three-tile-wide buildable rock strip** runs down each
  lane from each team's plateau to the widened central island. It is ordinary, passable
  rock: there are no starting structures on the strips or in the lane-mouth centre.
- Two **seven-tile-thick** mountain ridges run continuously across the middle, from one
  plateau to the other, with a **one-tile rock margin on both sides**. They do not cut the
  plateaus: teammates can change lanes behind their front. These are normal Dune mountains:
  vehicles cannot cross, infantry can.
- A larger buildable rock island widens each strip halfway along the lane, with a guaranteed
  **7×7** core. Seeded outlines span 19–23 tiles horizontally and 11–13 vertically (11 on
  129×96, preserving worm clearance). **Two continuous sand channels per lane** pass around
  its shoulders, between the strip/island and the ridges. Spice fields and blooms remain on
  lane sand, never over rock ends, strips, margins or islands. The Spice slider ranges from
  None to Very high.
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
- **fifteen rocket turrets per player's base**, including **ten at the sand-facing front**
  and **five covering the core/flanks**; at least two core turrets cover both Yard and Lab;
  the ten front turrets are divided over that player's owned mouths (4/3/3 in 1v1, 5/5 for
  the upper 2v2 seat and ten for the lower seat, ten on each 3v3 player's own lane);
- windtraps sufficient for the entire kit, including all turrets, the lab and developed zones,
  with spare capacity;
- three mature Residential lots, one Commercial lot, one Industrial lot and one Police
  Station, with road access and a twelve-tile industrial/residential buffer. All R/C lots
  occupy the sand-facing front edge; Yard, Lab, Refinery, Police, Windtraps and Industry are
  further back. Roads stay behind the R/C frontage, and the Refinery's side/bottom exits
  remain clear. The engine probe checks that actual annual tax covers the full starting
  police/turret upkeep **and** annual power costs, not the AI's reduced funding settings.

**No extra harvester is written to `[UNITS]`.** The engine's existing first-Refinery rule
(`House::placeStructure`) gives a preplaced Refinery a free Harvester, delivered by a temporary,
non-owned Carryall, even at tech level 1. This delivery aircraft is distinct from the permanent
starting Carryall. The normal buildable Harvester requires a Heavy Factory, which is not a
tech-1 building; new Refineries also retain their normal Harvester grant.

**Sand really does improve nearby land value.** In `CitySimulation::runEffectsScans`, open
sand contributes `kSandTerrainRawBonus = 40` per tile to the terrain map, which is smoothed
twice, plus an independent `kSandLandValueBonus = 8` falloff stamp reaching five tiles.
These are positive contributions relative to otherwise identical rock surroundings, not a
guarantee that any sand-adjacent tile beats every rear tile: city-centre distance, pollution,
crime, damage and park/turret bonuses also affect the result. Putting R/C at the front gives
those private lots the sand amenity while keeping industrial pollution further away.

The generated match uses the **CivWars** mod and enables Research Lab, respawning sandworms,
trooper anti-air, city effects and **Original Unit Damage**. This is a match-local choice:
opening Civ Wars does not change the saved global damage preference for other modes.
Other settings are available in **Game Rules**. Tech progression is the Research Lab's
per-house tech-level system; no new resource, era or unit-control system is introduced.

Select the Lab and use **Research tech level N**. The zero-credit start cannot immediately
pay the first step: at Normal it is 1,000 credits and two game minutes on every map.
The sidebar shows the actual price, **Needs N credits** or **Ready to research**, and the
research time; active work shows its real remaining time. A click sends the normal
synchronized command; a completed level updates the house's build lists. There is no
population requirement for Lab research.

QuantBot treats research as required progression in generated Civ Wars, before repeatable
city/military orders. It preserves queued commitments, operating cash and urgent power/
defence recovery, and rebuilds a lost Lab. This applies to teams and every difficulty,
including Builder/Defend controllers; it does not wait for an attack wave or an
unbuildable high-tech worker fleet. It uses the hashed `[Force Concentration] ResearchLab`
switch (on by default), **not** the experimental `[Civ Wars] Strategy` switch.

## Research cost and pace

**Research Pace** is available directly on the Civ Wars setup and in **Game Rules** for
Civ Wars, Custom Game and Random Skirmish, including online generated maps. It changes
both the price and duration for humans and AI:

| Pace | Price and time multiplier |
|---|---:|
| Fast | ½ |
| Normal (default) | 1 |
| Slow | 2 |
| Very slow | 4 |

For the target tech level `n` (the level being reached, 2–9), the single shared policy in
`include/TechResearch.h` is:

```text
price(n)  = 1000 * (n - 1) * P credits
cycles(n) = MILLI2CYCLES(n * 60 * 1000) * P
```

Research does **not** depend on map dimensions, income, player count, surviving buildings
or explored area. The same quotes apply on every Civ Wars size and on a 256×256 Random
Skirmish. Pace is a saved/hashed match rule; there is no separate research-area field in
new saves or the options hash. Integer simulation is exact even at Fast: each quote is
integral in credits and 16-ms game cycles. House payment/timing, Lab price/countdown and
QuantBot's protected savings/forecast all use the same policy.

### Normal: every map

Tech 1 is the starting level. These are **game-time minutes**, not wall time at a
different game-speed setting.

| Target tech | Credits | Game time |
|---:|---:|---:|
| 2 | 1,000 | 2:00 |
| 3 | 2,000 | 3:00 |
| 4 | 3,000 | 4:00 |
| 5 | 4,000 | 5:00 |
| 6 | 5,000 | 6:00 |
| 7 | 6,000 | 7:00 |
| 8 | 7,000 | 8:00 |
| 9 | 8,000 | 9:00 |
| **Full chain** | **36,000** | **44:00** |

Funding waits, cancellations and rebuilding can only delay the uninterrupted chain.
Fast halves both columns; Slow doubles them; Very slow multiplies them by four.

### Why map scaling was removed

Stefan's `researchtooexpensive.dls` is a real 9882/MOD17 save from a 256×256 Random
Skirmish, with Research Lab enabled and Normal pace. Its retired area field is 65,536.
The former formula calculated a level-4 quote of **11,501 credits / 11:10.880** on that
map, prompting his explicit request for **3,000 credits / four minutes**, then another
1,000 credits and one minute per target level. The November 3 map-scaled experiment is
therefore superseded, not an additional selectable balance mode.

Historical calibration, old same-build A/B CSVs and original immutable content evidence
remain under `outputs/research-save-20261103/` and `outputs/research-pace-20261103/`.
They describe the withdrawn policy, not current quotes. The obsolete test-only
reference-area override was removed with the production scale.

### Saved-match compatibility

**9883 / MOD18 / protocol 112** is shared with Mountain Scenery. Real installed
9882/MOD17 saves are frozen: their exact suffix is pace(uint32), area(uint32),
Damage Lowers Land Value(bool), with **no** Mountain Scenery byte. The loader consumes
and ignores the retired area. MOD18 writes pace, damage and scenery, without area.
Older saves still load; saves predating Research Pace use Normal.

Completed levels, credits already paid and an active study's **exact remaining timer**
are retained. A legacy timer may exceed today's quote and remains valid through a
9883 resave/reload; it is neither shortened nor refunded. The sidebar's countdown stays
exact and its percentage stays within 0–100. Subsequent starts use the new quote.
Mountain Scenery loads off for the old formats, independently of the research change.

## Sandworms

**Worms / lane** is **0–10**, default 1: up to **30** starting worms in total. Each lane
gets the same count. Two-worm pairs are mirrored about the vertical centre column; an odd
unpaired worm starts on that column. All origins are distinct sand tiles outside the
central islands and strips, with a clear surrounding sand neighbourhood. Changing only the
worm count never changes the terrain.

Ordinary sandworm respawn picks random positions across the whole map. Generated Civilization
Wars maps instead carry `[CIVILIZATIONWARS]` with `Version=1`. An untamed worm's first valid
deployment on such a map records a permanent origin. After defeat or its usual three meals,
it sleeps as usual, then wakes at **that same origin**. If ground or another underground unit
occupies the tile, it takes another nap and retries that slot; it never falls back to map-wide
random placement. The slot survives movement and saving/loading. The shared 9881 save format
stores it; older worm records read no extra fields. The layout update retains marker
`Version=1`: it identifies this permanent-slot **policy**, not a recipe for reconstructing
terrain or slots from a seed. Saved INI maps carry their exact terrain/buildings/worm
placements, and saves carry each worm's actual origin coordinates. Neither is regenerated
with the current layout; existing maps and saved origins therefore remain unchanged.

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

- `dunelegacy_tests` covers every research level/pace, exact full-chain totals, invalid
  targets, option hashes, MOD18 and frozen nine-byte MOD17 compatibility, truncation
  and invalid-pace refusal.
- `research_lab_probe` covers map-independent real House quotes, exact Slow timing,
  command/payment, a paid timer longer than its new quote surviving save/reload,
  cancellation/rebuild and QuantBot. Its optional `--load-save` checks the original
  serialized timers and a one-tick 9883 resave/reload of a real player save.
- `civilization_wars_tests`: seeded generator matrices for all team counts, all sizes and all
  worm counts, including exact mirroring, building footprints/power, front/back zoning,
  fifteen turrets per base (ten front), coverage, lane connectivity/separation, continuous
  strips and ridge margins, both sand channels, islands, roster and map serialization;
  mode-marker/cache/wire-format unit tests.
- `civilization_wars_probe`: real menus, persisted seed, ten-worm choice at 640×480,
  generic-start lobby and guest settings; all-AI games for each team size at 640×480 and
  1280×720, including Easy/Medium/Hard/Brutal/Defend and Builder controllers; positive
  full-cost starting budget and per-house research progression; worm defeat, eating,
  occupied-slot retry, legacy RNG and sleeping save/load behavior. Every game reaches at
  least ten minutes. Both AI and human research checks retain the original minimum
  research allocation: 600 credits over 430 game seconds (83.72 credits/game-minute),
  followed by the actual quoted study duration. The allowance is
  `ceil(430 seconds in cycles * quoted cost / 600) + quoted duration`, never less than
  eight minutes. With the new Normal first step this is **13:56.672 on every map**;
  each research milestone is still asserted separately from the ten-minute minimum.
  The deadline follows cost/time policy, not arbitrary extra grace or map area.
- `civilization_wars_research_probe`: actual Civ Wars Start button, real human SDL Lab
  selection and Research click, zero-credit feedback, normal-income enablement, queued
  command and completed tech level; 640×480 and 1280×720 screenshots. Its diagnostic
  launcher replaces only the blocking interactive loop with deterministic game steps.
- `menu_navigation_probe` includes the main-menu destination, selectable mod activation and
  online generator-to-player setup flow. `ui_gallery_probe` renders and lints the play,
  Spectate and online map screens, including 640×480 desert corner-crest clearance.

Runtime evidence and screenshots for the original implementation are recorded in the
unreleased HANDOVER entry and `/Users/stefan/Documents/projects/outputs/civ-wars/`.

The 2026-11-04 layout acceptance is retained in
`/Users/stefan/Documents/projects/outputs/civ-wars-layout-20261104/acceptance.json`
and its neighbouring PNGs/logs/INI maps. On combined source `ade2e8b79d28`, the three
Civ Wars tests passed on the mini: generator 30 cases / 37,803,797 assertions,
gameplay probe 86.17s, human research probe 19.08s. All-QuantBot 1v1/2v2/3v3 at both
640×480 and 1280×720 reached 14:01.696; every base loaded 15 turrets (10 front),
six windtraps and 600/533 power. Full annual upkeep/power was covered by actual tax.
Viewed 1v1/3v3 overviews and base close-ups show the strips, ridge margins and
front/back layout. The diagnostic overview uses white turret marks, green R,
blue C and yellow I; actual in-game base screenshots sit beside those previews.
