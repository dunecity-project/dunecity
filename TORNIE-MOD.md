# Mods Guide — Tornie & DuneCity

This game ships **two separate built-in mods**. They are *different things* and are
enabled independently from the **MODS** menu:

| Mod | What it is | City mode |
|-----|-----------|-----------|
| **Tornie** | A content/tech-tree mod: new **units**, **red/green spice**, and the **8th faction (Rebels)** | Off |
| **DuneCity** | A **city builder** grafted onto the RTS: zones, roads, civic buildings, budget | On |

> ⚠️ **The city-building content (zones, roads, nuclear plant, police/stadium/
> airport, budget) belongs to the DuneCity mod — NOT the Tornie mod.** Tornie is
> about units, spice, and the Rebels faction. This guide keeps them in separate
> parts below. (One item, the Advanced Windtrap, is shared — it needs Tornie *and*
> city mode.)

All stat numbers come from each mod's `ObjectData.ini`; if a number here disagrees
with the game, the in-game values win.

---
---

# PART 1 — The Tornie Mod

New factions, units, and spice. City mode is **off**.

## 1.1 Factions — 8 playable careers

Vanilla Dune II has 3 campaigns (Atreides, Ordos, Harkonnen). Tornie brings the
total to **8 full careers**, 22 scenarios each:

| House | Notes |
|-------|-------|
| Atreides, Ordos, Harkonnen | The classic three |
| **Fremen** | Desert natives |
| **Sardaukar** | Imperial shock troops |
| **Mercenary** | Guns for hire |
| **Neutral** | Independent forces — home of several exclusive units (below) |
| **Rebels** | The **"8th career"** — grey livery, own campaign, and the "Cyril" mentat |

All 8 appear in the house-selection carousel and are playable in skirmish/MP.

## 1.2 New units

### Flame Tank
Close-range incineration tank, **exclusive to Tornie**.
- **Heavy Factory · Tech 9, Upgrade 4** (Rebels: tech 8)
- **HP 100 · Price 700 · Speed 3.84 · View 5**
- **Weapon:** a Sonic-Tank-style flame projectile (range 5, damage 75); throws off
  flame bursts, and scatters flame explosions + a screen shake on death.

> ⚙️ *Current behavior:* damage lands on the projectile's impact. The flame **trail**
> along the path is presently visual only (path-damage is being reworked) — treat it
> as a hard-hitting short-range shell for now.

### Elite Siege Tank
Tougher, longer-ranged siege tank.
- **Heavy Factory · Prerequisite: House IX** (Rebels tech 9)
- **HP 350 · Price 750 · Speed 2.46 · Range 7 · large shells**
- Also appears as a **Palace super-weapon** roll for Atreides/Harkonnen/Ordos under
  Tornie (~50% chance vs. their normal special).

### Elite Launcher *(Neutral only)*
- **Heavy Factory · Prerequisite: House IX · Neutral house only**
- **HP 120 · Price 725 · Range 9 · damage 94** · can target sandworms.

### Rocket Trike *(Neutral only)*
- **Light Factory · Neutral only · tech 3**
- **HP 80 · Price 200 · very fast (9.25)** · fires trooper-style rockets.
- Map-placed Neutral Trikes are **auto-upgraded** to Rocket Trikes under Tornie.

### Deviator *(rebalanced)*
Buildable only by **Ordos and Neutral** (Heavy Factory); tuned stats
(HP 120, Price 750, range 7).

## 1.3 Red & Green Spice

Two new spice varieties that change your economy:

| Spice | Effect |
|-------|--------|
| **Green Spice** | **+30% faster harvesting** (same credit value) |
| **Red Spice** | **+25% more credits** at the refinery (same gather rate) |

- **Red/Green Spice Blooms** burst into a field of that color (radius 5) when
  triggered.
- Ordinary spice blooms have a small (~5%) chance to seed a new colored bloom on
  adjacent sand, so red/green spice can spread over a match.
- Placeable in the Map Editor's terrain palette (bottom row).

## 1.4 Tornie cosmetics & tuning
- Per-house **corner flags** on buildings; the Rebels' custom **grey palette**.
- Tuned defaults via Tornie's `GameOptions.ini` (e.g. instant build, higher
  unit/harvester caps).

---
---

# PART 2 — The DuneCity Mod (City Builder)

With **DuneCity** active ("City Mode"), a Micropolis / SimCity-Classic style city
builder runs alongside the RTS. You zone land, wire it with roads and power,
provide services, and manage a budget — and the city's economy becomes a route to
victory. **None of this is part of the Tornie mod.**

## 2.1 Zones — Residential / Commercial / Industrial
- **2×2 · Price 100 each · instant build**, placed on sand/rock/slab.
- Zones start empty and **grow over time** based on demand, land value, power, and
  pollution — the sprite fills in as they develop.
- Classic city-sim rules: unpowered zones won't grow; high pollution slows (>80)
  then blocks (>160) residential/commercial growth; crime drags down land value.

## 2.2 Roads & power
- **Road** — cheap 1×1 tiles that auto-connect; buildings get road frontage
  automatically in city mode.
- Zones and civic buildings must be **powered** to function and grow.

## 2.3 Power plants
- **Nuclear Plant** — 3×3 · Price 1500 · **-1000 power** (~10× a Windtrap; one can
  power a whole base + city). Tech 6, needs a Windtrap.
- **Advanced Windtrap** *(shared: Tornie + city)* — 3×3 · Price 500 · **-300 power**
  (~3× a Windtrap). Tech 6, needs Windtrap + Radar + High-Tech Factory.

## 2.4 Civic & service buildings
- **Police Station** — Price 500. **The only source of crime/police coverage**
  (Barracks/WOR no longer count). Radius ~16 tiles.
- **Stadium** — 3×3 · Price 3000. Large land-value / civic boost.
- **Airport** — 3×3 · Price 5000. Economic & commercial boost; spawns cosmetic
  **ambient aircraft** that fly across the map (don't count against unit caps).
- **Hospitals & Churches** aren't built directly — they **appear automatically** as
  residential population grows (~one per 256 pop), replacing a residential tile.

## 2.5 Budget, taxes & winning
- A **City Budget** window sets the **tax rate** (default 7%, 0–20%) and funds
  police/roads; the city pays its budget out daily.
- **Economic Victory:** grow the city economy past the threshold (default 500) to
  win — a peaceful alternative to destroying the enemy.
- **Milestones** track population (100 / 500 / 1000 / 5000), first zone, economy,
  and full build-out.
- In city mode a **Starport** needs a sizable population (20,000 displayed) first.

---
---

# PART 3 — Engine features (both mods)

Not tied to either mod — available generally:

- **Up to 8 teams & 8 players.** Player Settings offers **Team1–Team8** (was 6),
  matching the 8 player slots. (Also fixed a latent out-of-bounds affecting the
  Rebels house and teams 7–8.)
- **Map Editor** exposes all new content with hover tooltips: Tornie units in the
  Units palette, DuneCity structures in the Structures palette, red/green spice in
  the Terrain palette.
- **Cursor Scale** *(Options → Video)* — Auto / 1× / 2× / 3× / 4× for crisp cursors
  on HiDPI / Retina displays.

---

## In-progress / partial features
- **Flame Tank flame trail** damages on impact only; path damage is being reworked.
- **Power Line** exists in the data but is disabled (not currently buildable).
- Some **city milestones** are declared but their rewards aren't fully wired yet.
