# How to Play Dune City

Dune City layers a Micropolis-style city simulation on top of Dune II.
Build a Construction Yard, lay out roads, drop R/C/I zone tiles, and keep
the lights on. The simulation does the rest.

This is the long-form guide. For a quick web version, see
[dunelegacy.com/dune-city.html](https://dunelegacy.com/dune-city.html).

---

## 1. Turn on city-sim mode

City-sim play is the bundled **Dune City** mod (`dunecity`). You choose a
mod when you start a game: **Campaign**, **Custom Game** and **Random
Skirmish** on the main menu each have a **Mod** list. Pick **Dune City**
there. Other mods, such as **Tornie** and **Dune2R**, are chosen the same
way. The **DuneCity Campaign** always plays Dune City.

On first launch a one-time message explains this. It has a single **OK**
button and does not change your mod.

The banner above the main menu buttons shows the active mod. When it reads
**MOD: DUNE CITY**, the city-sim build menu entries become available
in-game.

## 2. Start a skirmish on a city-sized map

City-sim play wants room. The bundled map **`2P - 192x192 - City`**
is designed for it.

1. Main Menu → **Custom Game**, and choose **Dune City** in the **Mod** list
2. Pick **`2P - 192x192 - City`** from the map list
3. Set up players and start the game

City tools only work on **rock** tiles. Sand is for spice harvesting and
combat as usual.

QuantBot puts enemy harvesters captured by its Deviators to work for itself.
With a refinery available, a borrowed worker returns to the nearest own
refinery at half capacity, or with ten seconds of deviation remaining if it
has any spice, whichever comes first. Human orders take priority; harvesters
controlled by a human keep their usual behaviour. The refinery doing the
unloading receives the spice income even if deviation expires inside it.

## 3. Lay down the backbone

Before zoning, build the core infrastructure:

- **Construction Yard** — your anchor. Place on rock first.
- **Wind Traps / Nuclear Plants** — power everything else. Without
  surplus power, zones decay. In city-sim mode the AI also builds
  nuclear aggressively, so keep up.
- **Refinery + Silos** — spice income funds your build queue.
- **Roads** — the build menu shows a **Road** tile when city-sim mode is
  on. Roads are independent from concrete slabs — they auto-tile and
  carry the city's supply network. Without roads, Commercial and
  Residential zones won't develop past Level 1 because Industrial supply
  can't reach them.

Roads cost 10 credits and build instantly. They're not selectable units;
they just flip a tile flag.

## 4. Zone the city

Three new buildable tiles appear in the build menu when city-sim mode is
active:

| Zone | Icon | Role |
|------|------|------|
| 🏠 **Residential (R)** | houses | Population. Needs jobs and low pollution. |
| 🛍️ **Commercial (C)** | shops | Jobs + services. Grows near people and road-connected industry. |
| 🏭 **Industrial (I)** | factories | Production. Cluster away from R — pollution drops land value. |

Each zone is a **2×2 gameplay tile**. The art is Micropolis-derived 3×3
imagery rendered inside the 2×2 footprint.

Zones start empty and **develop over time** as the simulation runs:

- Land value rises near the city centroid and near roads
- Supply flows along the road network from Industrial → Commercial
- People flow from Residential to Commercial along roads
- Crime rises in low-coverage areas
- Pollution radiates from Industrial

A zone at Level 1 is a single small building. Level 2 / 3 are denser,
nicer sprites. A zone declines if conditions worsen.

## 5. Manage the city budget

Population pays taxes annually. Civic services cost upkeep.

Open the **City Budget** window from the HUD to see:

- **Tax rate** — higher rate = more revenue, but slows growth
- **Police funding %** — 0–100% of police budget you actually pay.
  Underfunding crashes coverage, which crashes land value, which stalls
  growth.

Every player runs their own budget. The AI also pays its own bills.
There's no global "city treasury" — each house's spice account is its
own budget.

## 6. Keep growing

Practical tips:

- **Spread police stations.** They have a coverage radius. Clustering
  them wastes coverage.
- **Cluster industry at the city edge.** Pollution from Industrial
  reduces Residential land value. Keep them apart.
- **Watch the power meter.** If surplus power drops, growth halts
  everywhere. Build another Nuclear Plant or two before you run out.
- **Inner industrial spur.** Commercial zones need Industrial supply
  within ~16 tiles via road. If your C zones are stuck at Level 1, drop
  a small I cluster nearby.
- **Race the AI's land value.** On large maps you can compete on city
  quality, not just military.

## 7. Combat still applies

Dune City is layered on top of Dune Legacy. Everything from the original
still works:

- Units, vehicles, ornithopters, sandworms, combat
- Original Dune II campaigns
- Multiplayer (LAN + Internet via metaserver)
- Map editor (city zones can be pre-placed in the editor)

You can play a **pure city-builder** skirmish on a big rock map, or a
**hybrid** where you defend the colony while it grows. The simulation
runs the same way in both modes.

## Carryalls and Repair Yards

Dune City games start with **Manual Carryall Drops** on (Game Rules): select units, press the Carryall drop
button or key and click a spot, and a free Carryall flies each unit there. A damaged unit with a Repair Yard and
a Carryall is flown in for repairs.

Select a **Repair Yard** to find the **Return units** button in the side panel. On (the starting state): a
Carryall flies each repaired unit back to where it was picked up. Off: repaired units stay beside the yard. The
button sets all of your Repair Yards at once. It is hidden when Manual Carryall Drops is off, because then no
unit is flown back.

## City airplanes

A powered **Airport** (city games only) launches the city airplane and the traffic helicopter. You do
not order them, but you can buy more planes.

- **The Airport's size.** An Airport you build covers 4x4 tiles and shows the full Micropolis
  airport. An Airport already on a map grows to 4x4 towards whichever side has free ground; one
  that is boxed in by other buildings, and every Airport in a game saved before this change, stays
  3x3 with the same picture drawn smaller. Both work the same.
- **The Airport in the city.** As in Micropolis it has no population of its own: it is counted,
  and a city needs one for its commercial zones to keep growing. It pollutes twice as much as a
  fully built industrial zone, so keep it away from housing.

- **The free plane.** Each powered Airport makes a free plane every five minutes; the Airport's panel
  shows the countdown. It takes off when the clock has run out and your house has no free plane
  in the air, and that restarts the clock of all your Airports, so a second Airport does not
  shorten the wait. Nobody bombs in the first five minutes of a game: Airports that stand when
  the game begins charge first, and one you build in those minutes is ready when they are up. An
  Airport you build later has its free plane ready at once. Planes you have bought do not hold
  the free one back. It takes off from the runway two tiles east of the Airport. The radar marks
  every plane you can see with a small white cross for its whole flight, which is easier to
  follow than the plane itself at a fast game speed. A flight ends
  when the plane is shot down or crosses the edge of the map, which a run towards a building
  near the edge soon makes it do: a plane that is not shot down is usually gone within half a
  minute to a minute. Human and computer players get the same plane.
- **Buying a plane.** Select a powered Airport and press **Buy airplane 1500**: a plane takes off at
  once. You can buy as fast as you can pay, until your house has ten planes in the air (the panel
  shows the count).
- **When computer players buy.** A computer player keeps its reserves and 3000 credits more, and
  then buys for one of two reasons. Its planes pay: the plane has a performance score like every
  unit type, the credits its bombs and its crash have destroyed for each credit of plane (x1.0 is
  a plane that destroys what it costs), and the computer buys while that score is at least what
  its army gets for a credit, never asking for more than x1.0 and never accepting less than x0.5.
  The free plane gives the first evidence. Or it is rich: with 30,000 credits to spare it keeps
  buying, up to ten in the air, even where single planes do not pay. That is what grinds down a
  massively built city when a game has stalled.
- **Finding your Airport.** With nothing selected, the side menu lists an **Airport** button under
  the Construction Yard's. It selects your Airport (the one that can sell a plane now, if you have
  several) without moving the view.
- **Where it flies.** Each leg of the flight is a run across one enemy building, picked at random from
  all enemy buildings on the map, and on for twelve tiles beyond it. Then it turns for the next.
- **Bombs.** The plane lays a stick of heavy bombs like a heavy bomber. The first bomb is released
  as soon as it would land within half a tile of an enemy building or within a tile of an enemy
  ground unit; then one falls every two tiles for as long as a bomb would still reach an enemy building or unit
  with its blast, and one more after the last. The rack never empties. A bomb does 250 damage to
  the building it lands on and goes off in a large explosion four tiles wide. One hit destroys a
  zone, Wind Trap, Gun or Rocket Turret, Heavy Factory, Repair Yard, Silo or Wall. Two hits
  destroy every other building except a Nuclear Plant (three) and a Palace (four). A bomb that
  just misses an enemy building still splashes it: 125 within half a tile of its edge, 62 to a
  tile, 31 to a tile and a half. Bombs are never released over your own or an allied building,
  and their splash never damages your own or an allied building. Against ground units a bomb's
  blast reaches a tile and a half: the full 250 within half a tile, 125 to a tile, less beyond,
  whoever owns the unit.
- **The fall.** A bomb takes 1.2 seconds to fall and lands three tiles ahead of where it left the
  plane. By then the plane is nine tiles further on, so you see the stick drop back from it and go
  off behind it. A bomb never damages an aircraft, not the plane that dropped it nor any other.
- **Shooting it down.** Rocket Turrets fire at an enemy plane from twice their normal range (16
  tiles; Ornithopters they see at 24), and Launchers from their own range. Their rockets fly to
  where the plane will be and go off on touching it, or on any other enemy plane they touch on the
  way. A rocket that is 16 tiles from where it was fired goes off, and a turret does not fire at
  a plane its rocket cannot catch inside that distance: a plane flying away is left to the turrets
  ahead of it. A plane has 150 hit points; a turret's rocket does 30 and a Launcher's 75. Damage
  stays on the plane. Rocket Turrets need power in the Dune City options, and a turret that a
  bomb lands on is destroyed. A city without Rocket Turrets under a plane's course loses dozens
  of buildings to one flight.
- **The crash.** A plane that is shot down crashes with the blast of a Death Hand where it falls.
  Nothing holds its fire for that: a plane shot down over your city crashes into your city, which
  costs far less than letting it bomb on. What the crash destroys counts as the plane's damage
  and goes to the score of the house that sent it, not to whoever shot it down.
- **Computer players under bombs.** While its buildings are being bombed a computer player adds
  Rocket Turrets spread through its city, near the hits first and at least six tiles apart, until
  every building has one within eight tiles: at most two a minute, eight a raid, and only from
  spare money and with power for them. (A rule that has it build these before the first bomb,
  `AirThreatTurrets` in `QuantBot Config.ini`, exists and is off: measured, it did not save a
  city any buildings.)
- **Stats and picture.** The Stats window lists the City Airplane with your other units: how many
  are flying, the units it destroyed, the planes you lost (every flight that has ended) and the
  hit points its bombs and crashes removed. For a computer player's house the Army allocation
  table on the left has two rows under its ten unit types. **Trooper** shows the Trooper's
  performance score, and as its per cent the infantry cap: the most of the army's value that may
  be infantry. **City Airplane** (once the house has an Airport or has flown a plane) shows the
  plane's performance score, and as its per cent that score as a share of the score the house
  buys at: at 100 or more it buys planes. Neither number is a share of the army as the ten rows
  above are; the note under the table says so. A selected plane or helicopter shows its own
  picture.
- **No Ornithopters.** An Airport no longer sends out Ornithopters. They come from the High-Tech
  Factory as before.

Six lines in `[Game Options]` of `Dune City.ini` switch these rules (hosts decide in multiplayer):
`City Plane Defence = true`, `City Plane Bombing = true`, `Airport Ornithopter Patrol = false`,
`City Plane Carpet Bombing = true` (false: one aimed 100-point bomb per building tile from a rack of
six), `City Plane Bombing Runs = true` (false: random waypoints anywhere on the map) and
`City Plane Purchase = true`.

## Research your house's technology

Enable **Research Lab** in **Game Rules**, build a lab, then select it and
press **Research level** to raise only your house's technology. Each study
unlocks the next level, up to level 9. **Research Pace** changes the total
price and study time; the sidebar shows both before you start.

In a **Custom Game**, **Tech level** below **Game Rules** sets the level
every house starts at: **Map** keeps the level the map was made for, 1 to 9
replaces it. With Research Lab on, research continues from that level.

**Technology Installments** is an optional rule in **Game Rules** (off by
default) and applies to humans and AI alike, for both research and
Construction Yard/factory upgrades. When it is on, you do not need the full
price in the bank and can start even with zero credits. Research drains
credits on each funded
simulation tick. The displayed **Drain** is the nominal credits per game
second; the exact total paid over a completed study equals its quoted price.
The sidebar also shows progress, the amount paid, and remaining study time.

If money runs out, the sidebar says **Paused: no credits**. Any affordable
part of the next payment is kept, but the timer advances only once that tick
is fully funded. Income resumes research automatically: time spent paused
does not create catch-up charges. Losing the researching lab cancels the
study without a refund.

With **Technology Installments** off, the full price is needed when starting
each new study. A study keeps the price, duration and payment mode it started
with. Older saved games retain their upfront-payment rules, and their
already-paid research timers continue without another charge; the sidebar
labels these **Paid upfront** and **No further charge**.

## Upgrade Construction Yards and factories

Select an eligible, fully repaired Construction Yard or factory and press
**Upgrade** (or **U**). With **Technology Installments** enabled, you can
start without saving the whole price first, just like the AI. Credits drain
as the upgrade progresses. At zero credits the upgrade pauses; income
resumes it automatically, with no catch-up bill.

While upgrading, the sidebar replaces the building portrait with its
**Upgrading** or **Paused: no credits** status, total price and amount paid.
The upgrade bar shows the funded percentage. Production resumes after the
upgrade completes; an existing production hold remains your choice.

With the rule off, including older saved games, new upgrades retain their
old full-balance starting requirement and then drain credits as before.
Already-paid upgrade progress is retained when loading a save or changing
the rule, never charged again.


## Save a favourite map

In a Random Skirmish, Civilization Wars or Custom Game, open the game menu
and choose **Save as Map**. This also works while spectating; campaign
missions do not offer it.

- **From the start** exports the original map embedded in the match, unchanged:
  its terrain, spice, starting buildings and units, players and generator settings.
  Your chosen name is used for the file and map browser; the original map's
  internal name is retained.
- **From right now** makes a new starting map from the current terrain, spice
  types, concrete/roads, surviving buildings and units, houses, teams and credits.
  Health is rounded to the map format's 1/256 steps. Unit facing and supported
  attack modes, wind-trap upgrades and map-supported city-zone density are kept.
  Units in transports, contained in buildings or off-map are placed on nearby
  legal tiles that the map loader can represent.

Enter a map name and press **Save**. Replacing an existing file requires
confirmation. Maps appear under **Custom Game → MP User Maps** (more than
one player) or **SP User Maps** (one player), and in the map editor's user maps.
The map keeps its gameplay mod dependency, including the Civilization Wars marker.

This is a **new map**, not a resumable save: exact spice amounts, transported
cargo, zone residents and civic overlays, completed/active research, construction
and production queues, targets and paths, fog of war, statistics and scheduled
reinforcements are not copied. Current credits become starting credits.
Native sandworms are retained; native crime units are omitted because maps cannot
express their special ownership. Normal new-map initialization still applies,
including building road rings and a first refinery's harvester when needed.
Use **Save Game** when you want to continue the complete match.

## Mouse camera controls

- Over the map, **wheel up** zooms in and **wheel down** zooms out, one step
  per notch, anchored at the pointer. Zoom stops at the available limits.
- On a **Mac trackpad**, move two fingers together to pan the map without
  holding a button. Spread two fingers to zoom in; pinch them together to zoom
  out at the pointer. Two-finger scrolling pans — it does **not** zoom. Both pan
  axes honor the macOS scroll-direction preference consistently.
  Physical mouse wheels, including unphased high-resolution wheels, still zoom.
- On **iPhone/iPad**, move two fingers together to pan; spread/pinch them to
  zoom around their midpoint. Start both fingers over the map, not a menu,
  sidebar or map-control button. These controls work in the map editor too.
- Hold **middle mouse button (mouse 3)** over the map and drag to grab it.
  The map follows the cursor: dragging left moves the view right. Movement
  follows the current zoom and stops at map edges. Release to stop panning;
  the cursor is hidden during the grab. This never orders selected units.
- Wheels over sidebars, lists and menus still scroll those controls. Arrow-key
  and edge scrolling, minimap clicks and touch controls are unchanged.
- The map editor supports the same wheel zoom and middle-drag controls, using
  its three existing zoom levels. **Shift+wheel** over a unit rotates it.


---

## Hard constraints worth knowing

A few simulation rules that are not obvious from the UI:

- **R/C/I zones are 2×2.** This is a hard constraint. Don't expect 3×3
  footprints.
- **Power is global, not gridded.** If the owner's `producedPower >=
  powerRequirement`, every zone they own has power. There are no
  per-tile electrical grids and no power lines.
- **Roads are mandatory for growth.** A C zone with no road within
  range will sit at Level 1 forever.
- **Concrete slabs and roads are separate.** A concrete slab is not a
  road; a road is not a concrete slab.
- **Saves and replays remain deterministic** — same inputs produce the
  same city.

---

## Troubleshooting

### macOS: "dunecity.app is damaged and can't be opened"

This appears the first time you run the macOS build downloaded from a
browser. It is **not** actually damaged — macOS Gatekeeper is rejecting
the app because the GitHub-built bundle is ad-hoc signed (not signed
with a paid Apple Developer ID). Any file downloaded from the internet
is also stamped with a `com.apple.quarantine` attribute, and macOS will
not run ad-hoc-signed apps that carry that flag.

**Fix it once, in Terminal:**

```bash
xattr -cr /Applications/dunecity.app
```

After that the app opens normally and will keep working across reboots.
You only need to do this once per install (or after each new version
you drag in).

If you'd rather not use Terminal, you can also do:

1. Right-click `dunecity.app` → **Open**
2. macOS will refuse the first time; close the dialog
3. Right-click again → **Open** → click **Open Anyway** in the dialog

The first method (`xattr -cr`) is the most reliable on recent macOS
versions.

### Linux/Windows

No equivalent issue — Linux packages and the Windows ZIP run without any
quarantine fuss.

---

## Getting help

- Bug reports / feature requests: <https://github.com/svan058/dunecity/issues>
- Discord community: <https://discord.com/invite/6sAcZr6y3B>
- Source: <https://github.com/svan058/dunecity>
