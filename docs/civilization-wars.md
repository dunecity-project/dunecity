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
pay the first step: at Normal on 129×96 it is 600 credits; larger maps cost more (below).
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

## Research size and pace

**Research Pace** is available directly on the Civ Wars setup and in **Game Rules** for
Civ Wars, Custom Game and Random Skirmish, including online generated maps. It changes
both the price and duration for humans and AI:

| Pace | Price and time multiplier |
|---|---:|
| Fast | ½ |
| Normal (default) | 1 |
| Slow | 2 |
| Very slow | 4 |

For initial map area `A = width × height`, reference `A0 = 129 × 96 = 12,384`, target tech
`n` (2–9), and pace multiplier `P`, the single shared policy in `include/TechResearch.h` is:

```text
S = max(A0, 2*A - A0) / A0
price(n)  = ceil(300*n * S * P) credits
cycles(n) = ceil(((30 + 10*n)*1000 / 16) * S * P)
```

All arithmetic is integer, using 64-bit intermediates; there is one final ceiling for each
price and duration, not a rounded Normal value multiplied again by pace. Maps at or below
the reference area retain today's prices/times at Normal; Fast halves them. Each additional
reference area's worth of map adds 2× research, so larger maps have a deliberately longer
technology phase. No income, live player count, surviving buildings or explored area affects
the scale. The actual initial dimensions are bound during map allocation, before any house
advances; both this frozen area and pace are saved and included in the game-options hash.
QuantBot's research payment, protected saving and forecast all use the same House price.

### Normal: credits / game seconds

Seconds below are rounded **up** for display; the simulation's exact boundary is a 16-ms
cycle. Tech 1 is the free starting level.

| Target tech | 129×96 | 161×112 | 193×128 | 255×160 |
|---:|---:|---:|---:|---:|
| 2 | 600 / 50 | 1,148 / 96 | 1,794 / 150 | 3,354 / 280 |
| 3 | 900 / 60 | 1,721 / 115 | 2,691 / 180 | 5,031 / 336 |
| 4 | 1,200 / 70 | 2,295 / 134 | 3,588 / 210 | 6,707 / 392 |
| 5 | 1,500 / 80 | 2,869 / 153 | 4,485 / 240 | 8,384 / 448 |
| 6 | 1,800 / 90 | 3,442 / 173 | 5,382 / 270 | 10,061 / 504 |
| 7 | 2,100 / 100 | 4,016 / 192 | 6,279 / 299 | 11,738 / 559 |
| 8 | 2,400 / 110 | 4,590 / 211 | 7,176 / 329 | 13,414 / 615 |
| 9 | 2,700 / 120 | 5,163 / 230 | 8,073 / 359 | 15,091 / 671 |
| **Total price** | **13,200** | **25,244** | **39,468** | **73,780** |
| **Exact total time** | **11:20.000** | **21:40.304** | **33:53.024** | **63:20.688** |

Times are uninterrupted research only: funding, cancelled attempts and rebuilding can only
delay them. They are game time, not wall time at a faster/slower game-speed setting.
Save format 9882 / MOD17 stores the new rules. Older saves load as Normal and bind their
original map dimensions; completed tech and an already-paid active research timer are
retained, with the new prices/durations used for subsequent starts.

### Calibration: Stefan's `research.dls`, 2026-11-03

The save is a 193×128, medium-spice, 3v3 match, seed 138083612, cycle 112,730:
**30:03.680 game time**. All six controllers are QuantBots (Ordos Medium, the rest Hard).
The map starts at tech 1 with zero credits. Byte-preserving decoding of the saved House
ledgers gives:

| House | Tech / active target | Credits now | Gross tax received | Harvested credits | Gross credits / min |
|---|---:|---:|---:|---:|---:|
| Harkonnen | 9 / — | 638 | 10,762.21 | 43,278.97 | 1,797.70 |
| Ordos | 9 / — | 1,015 | 12,762.27 | 54,978.97 | 2,253.43 |
| Fremen | 8 / 9 | 587 | 9,410.97 | 44,304.08 | 1,786.85 |
| Mercenary | 8 / 9 | 473 | 13,108.01 | 44,427.90 | 1,913.95 |
| Neutral | 8 / — | 1,358 | 9,247.11 | 44,351.28 | 1,782.97 |
| Rebels | 9 / — | 729 | 7,274.46 | 38,162.45 | 1,511.47 |

Income is cumulative gross tax plus refined spice credit value divided by game minutes,
**before** police/upkeep, storage losses and spending; it is not net spendable cash.
The median is 1,792.27 gross credits/min (tax alone 335.53/min). The old complete research
bill, 13,200, was only 7.365 such income-minutes / 24.50% of the median accumulated gross.
A simple area ratio would still allow an uninterrupted tech-9 chain in 22:36, before this
checkpoint. The chosen excess-area scale is 2.989664× here: 39,468 credits, 22.021 observed
gross-income-minutes / 73.25% of the median accumulated gross, and **no tech 9 before
33:53.024**, even with unlimited funding. Actual AI spending and funding waits add time.

Completed-level timestamps are **not recorded** in these saves or the supplied log.
There is no invented per-level history: three houses had finished level 9 by the checkpoint;
the others had finished level 8. The two active level-9 timers imply starts at cycles
106,788 (Fremen) and 109,696 (Mercenary), within one update-cycle boundary; if their labs
survive, their remaining timers finish at cycles 114,288 and 117,196 respectively.
The earlier `notresearching.dls` is a **different seed/game** (95244949), at cycle 36,876 /
9:50.016, with all six houses at tech 1 and no active research; it is not an earlier snapshot
from which this match's income or completion-time deltas can be calculated.

### Measured same-build comparison

Both 45-game-minute arms ran on clean `d635fd0acc36`, Hard vs Hard, 193×128, seed
138083612, medium spice, two worms/lane, strategy off. They used the same binary,
source fingerprint, AI configuration hash `c5cf0c6e3cda82a5` and terrain hash
`f7a83c439bd0eb21`. Only the frozen research area differed: reference 12,384
(old prices/durations) versus actual 24,704 (new Normal). Options hashes were
`b640f8ca8a87867a` and `57f618c4c52f6f87`, respectively.

| Completed tech | Atreides, reference | Atreides, Normal | Ordos, reference | Ordos, Normal |
| --- | ---: | ---: | ---: | ---: |
| 2 | 5:23.280 | 8:54.768 | 5:33.744 | 10:09.232 |
| 3 | 8:45.280 | 13:51.872 | 8:41.344 | 14:08.736 |
| 4 | 11:40.080 | 18:52.160 | 11:21.744 | 19:15.424 |
| 5 | 13:11.680 | 24:16.464 | 12:42.144 | 25:42.128 |
| 6 | 15:07.280 | 33:34.352 | 15:06.544 | 31:54.416 |
| 7 | 18:45.280 | 39:57.856 | 17:53.344 | 38:53.120 |
| 8 | 24:21.680 | >45:00 | 23:34.544 | >45:00 |
| 9 | 32:58.880 | >45:00 | 26:56.544 | >45:00 |

All four houses remained alive at the time limit. Both Normal houses ended at tech 7;
Ordos was researching 8. Both arms' telemetry was complete, with no parse errors or
sequence gaps. Every observed start's charged price and duration matched the integer
policy. This is a controlled pacing example, not a win-rate or multi-seed balance claim.
The separate frozen pre-change `3d5db650` run reached tech 9 at 28:39.680 (Atreides) and
26:54.944 (Ordos); the same-build control above avoids attributing other combined-feature
AI changes to research.

Reproduce with `tests/ai/run-campaign-balance.py --civ-wars 1 --civ-size 2 --civ-worms 2
--civ-spice 2 --seed 138083612 --partner-difficulty hard --enemy-difficulty hard
--local-spectator --mod CivWars --minutes 45`, with and without
`--civ-research-baseline`. The test-only control binds reference research area at cycle 0,
without changing terrain or other AI settings. Raw CSVs and the paired receipt are in
`outputs/research-pace-20261103/{reference,normal,paired-progression.json}`.

### Saved-match compatibility

The actual `research.dls` loaded through the full 1,775,110-byte save in the new build,
at its original cycle 112,730 and all six original tech/timer states. Its migrated rule
was **Normal**, initial research area 24,704; the next level-9 quote was **8,073 credits /
22,423 cycles**. Fremen's and Mercenary's already-paid remaining timers stayed 1,558 and
4,466 cycles. One real engine tick and a 9882 resave succeeded; the resaved pace/area
were independently decoded as Normal / 24,704.

An empty test profile initially lacked the save's immutable CivWars revision. Its four
original rule files were recovered from the retained producer source/configuration;
the canonical manifest SHA256 exactly matched the saved
`37d5ce34e477fc5ddbb21f2328cf9898bd6baa347c1311f0f4dec307cf066f38` before use.
No checksum, save or resolver bypass was used. Provenance and the minimal reusable exact
revision cache are in `outputs/research-save-20261103/{revision-reconstruction.json,exact-profile}`;
the load log, research CSV, migrated save and receipt are in
`outputs/research-pace-20261103/save-load-exact-d635fd0a/`.

Raw checkpoint fields, offsets and hashes are in
`outputs/research-save-20261103/research-analysis.json`; exact Normal prices/cycles are in
`outputs/research-pace-20261103/normal-cost-time.csv` on the staging host.


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

- `dunelegacy_tests` covers research size/pace boundaries, exact prices/cycles at all four
  Civ Wars sizes, one-step rounding, safe integer limits, option hashes and MOD17/older
  stream migration, truncation and invalid-value refusal.
- `research_lab_probe` covers exact scaled House timing as well as command/payment,
  active-save continuation, cancellation/rebuild and QuantBot. Its optional `--load-save`
  exercises a real old player save, Normal migration and a one-tick 9882 resave.
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
  least ten minutes. Its **research deadline** still uses the original eight-minute reference.
  For dearer/slower research, the funding portion `(8 min − reference tech-2 duration)`
  scales by `quoted tech-2 cost / reference cost`, rounded up to cycles, then adds the
  actual quoted duration. This preserves the same minimum research allocation,
  83.72 credits/game-minute, rather than adding arbitrary grace. At Normal the research
  windows after the initial city checkpoint are 8:00.000 (129×96), 15:18.352 (161×112), and
  23:55.200 (193×128); the smallest match then continues to ten minutes. The observed
  161×112 regression was not a funding stall: all four
  houses had paid 1,148 credits and started by 7:29.616; their 5,976-cycle study's last
  scheduled completion was 9:05.232, beyond the obsolete eight-minute assertion.
- `civilization_wars_research_probe`: actual Civ Wars Start button, real human SDL Lab
  selection and Research click, zero-credit feedback, normal-income enablement, queued
  command and completed tech level; 640×480 and 1280×720 screenshots. Its diagnostic
  launcher replaces only the blocking interactive loop with deterministic game steps.
- `menu_navigation_probe` includes the main-menu destination, selectable mod activation and
  online generator-to-player setup flow. `ui_gallery_probe` renders and lints the play,
  Spectate and online map screens, including 640×480 desert corner-crest clearance.

Runtime evidence and screenshots for the implementation are recorded in the unreleased
HANDOVER entry and `/Users/stefan/Documents/projects/outputs/civ-wars/`.
