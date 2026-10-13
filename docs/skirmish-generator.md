# Spec: Random skirmish generator

Goal: a "Random Skirmish" setup screen where the player picks map and opponent parameters and gets a
freshly generated, playable map with pre-built enemy (and allied) bases, armies and — in DuneCity mode —
cities.

## Entry point
- Single Player menu → new button "Random Skirmish" (keep existing Skirmish/Custom Game). Also reachable
  from Custom Game as "Generate…" only if trivial.

## Parameters (all persisted as last-used in the ini)
Map
- Size: 64, 96, 128, 160, 192, 256 (square). Default 128.
- Rock: Low / Medium / High / Very high (target share of rock+mountain terrain, e.g. 15/25/35/50%).
- Spice: None / Low / Medium / High / Very high (field count and thick-spice share).
- Symmetric layout on/off (fair mirrored start areas; reuse MapMirror).
- Seed: random by default, shown and editable; same seed + same parameters = identical map and setup.

Players
- Your house (or Random), starting credits (existing presets).
- Allies: 0–3. Enemies: 1–5. Total houses ≤ what the engine/map supports (verify the real cap; clamp UI).
- Enemies allied with each other: on (one enemy team) / off (free-for-all among enemies; allies of the
  player always team with the player).
- AI difficulty per side (Allies / Enemies), using the existing AI player types.
- Game mode: Vanilla or DuneCity (DuneCity enables city-sim the same way Custom Game does with the mod).
- Tech level 1–9 (map `[BASIC] TechLevel`), applies to every house in both modes.

Opponent starting position (set separately for Allies and Enemies; the human always starts with the
normal start: Construction Yard or MCV per existing start rules + chosen credits)
- Vanilla — Base size: None (MCV only) / Outpost / Small / Medium / Large / Fortress. Each is a template
  of a building mix (yard, wind traps sized to power need, refinery+silos, barracks, light/heavy
  factory, outpost, repair, starport, IX, palace, walls/turrets for larger tiers) limited by tech level
  and faction availability, laid out on concrete on the house's rock plateau so every building is a
  valid placement and power is positive.
- Vanilla — Army size in credits: 0, 1000, 2500, 5000, 10000, 20000, or "Random" (uniform between a
  min and max). A seeded mix is bought at item prices for that faction/tech level (cap ≈ 40% of budget
  on any one unit type), placed on passable tiles around the base in Guard/AreaGuard.
- DuneCity — additionally City size by population: None / 1,000 / 5,000 / 15,000 / 40,000 / Random.
  The generator lays a road grid and R/C/I zones (+ police, power to cover requirements) on the
  plateau, sized from the simulation's population-per-zone level so the AI's displayed city population
  after load is within ±25% of the target. If zones can't be pre-developed via the map format today,
  extend the INI loader in a backwards-compatible way (existing maps unaffected) so a pre-placed zone can
  carry its starting development level; the city sim must accept it on load.

## Map generation
- Build on `MapEditor/MapGenerator` (`generateRandomMap`) but add controls: start positions spread
  evenly (corners/ring, mirrored when symmetric), each on a rock plateau large enough for the requested
  base/city footprint (carve rock as needed; plateau size derived from template area), spice fields
  between starts, reachable by ground from every start (verify connectivity; regenerate with seed+1 up
  to N times otherwise).
- Output an in-memory INI map (same format as saved custom maps) and start through the existing
  `GameInitSettings(mapfile, filedata, …)` CustomGame path so saving/loading the match works.
- "Save map" button writes the generated map into the user maps directory under a descriptive name so
  it can be replayed or hosted in multiplayer.
- Single player only for this slice (multiplayer hosting via the saved map is enough).

## Non-goals
A visual map preview editor, new AI behaviour, new unit types.

## Acceptance
- Unit tests: determinism (seed+params → identical INI text), house count/team assignment,
  base templates respect tech level and positive power, army mix spends ≤ budget and ≥ 90% of it,
  every placed structure/unit lies on a legal tile, connectivity check, city population estimate within
  ±25% of target for each preset, older maps without the new zone-level key load identically.
- Real-engine probe: generate and load several configurations (small vanilla 1v1, 2v3 allied enemies
  with Large bases and 10k armies, DuneCity 192 map with 15k city at tech 8); assert houses/teams,
  structure counts, army value, measured city population after the first full city cycle.
- Menu probe/screenshot of the Random Skirmish screen; a smoke playthrough of a generated match for a
  minute of game time without errors in the log.
