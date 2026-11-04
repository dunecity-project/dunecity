# How to Play Dune City

Dune City layers a Micropolis-style city simulation on top of Dune II.
Build a Construction Yard, lay out roads, drop R/C/I zone tiles, and keep
the lights on. The simulation does the rest.

This is the long-form guide. For a quick web version, see
[dunelegacy.com/dune-city.html](https://dunelegacy.com/dune-city.html).

---

## 1. Turn on city-sim mode

On first launch Dune City offers a one-click **Enable now** prompt for
the bundled `dunecity` mod. If you skip it, you can enable it later:

1. Main Menu → **Mods**
2. Select **Dune City**
3. Click **Activate**

The bottom-left watermark on the main menu shows the active mod. When
"Dune City" appears there, the city-sim build menu entries become
available in-game.

## 2. Start a skirmish on a city-sized map

City-sim play wants room. The bundled map **`2P - 192x192 - SimCity`**
is designed for it.

1. Main Menu → **Single Player** → **Custom Game**
2. Pick **`2P - 192x192 - SimCity`** from the map list
3. Set up players and start the game

City tools only work on **rock** tiles. Sand is for spice harvesting and
combat as usual.

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

## Mouse camera controls

- Over the map, **wheel up** zooms in and **wheel down** zooms out, one step
  per notch, anchored at the pointer. Zoom stops at the available limits.
  High-resolution trackpad gestures accumulate small deltas and limit rapid zooms.
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
