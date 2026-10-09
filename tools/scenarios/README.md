# Saved world to fresh scenario

This macOS/Ninja diagnostic converts the current saved world using the production
save loader and the existing map editor writer. It never runs the installed app.
Build the project first, then run:

```sh
python3 tools/scenarios/export-save-scenario.py \
  --save /absolute/path/3waysplit.dls \
  --output /absolute/path/3waysplit.ini \
  --report /absolute/path/report.json \
  --work-dir /absolute/path/export-scratch \
  --profile-from "/absolute/path/Dune City" \
  --author "Stefan van der Wel"
```

The source profile must contain the save's exact mod revision. Only mod folders,
settings and revision content are copied into an isolated profile; publisher
credentials and publication queues are excluded. A missing revision fails offline
before any download dialog. SDL uses dummy drivers, the intro is disabled and
the diagnostic metaserver is loopback. Each load uses a separate engine process.
The input save is read only. Output defaults to refusing an existing file;
`--force` replaces the requested scenario, while input/output/report path
collisions are refused. Use a dedicated scratch directory.

The report records source/save hashes, tool hashes, world counts by faction,
health and ownership checks, unit relocations, reset orders and startup road
frontage. The fresh engine reload must retain every structure and unit, every
saved terrain/concrete tile and road, factions, alliances and whole spendable
credit balances. The default simulation check advances sixty game seconds.
It is a startup check, not a whole-match balance claim.

## Ordinary scenario limits

City population/density, demand and internal ledgers, production queues,
construction progress, live orders, upgrades, cargo amounts, fog, elapsed time,
lobby rules and CHOAM stock restart. Whole available house credits survive.
Terrain spice classes remain; fractional amounts are regenerated. Rubble does
not persist. Unsupported live Retreat orders become Area Guard.

All carried and repairing units survive as deployed forces. If their recorded
position is unavailable, the tool finds a deterministic nearby free tile, using
their own construction yard when no valid position was saved. Air units over
buildings keep their position. Every relocation is listed; units are never
silently dropped. Search fails explicitly if the requested radius is exhausted.

The editor has one GEN key per tile, so the tool supplements concrete-plus-road
with a distinct zero-padded road key after concrete. Damaged walls use the existing
ID syntax instead of GEN, preserving their health. Ordinary city placement may
restore road frontage around buildings. The verifier accepts only added roads
on valid frontage and records their coordinates; missing saved roads always fail.

GEN terrain objects need a house owner. Unowned substrate is assigned to an
unused passive faction, keeping it out of the selectable slots and preventing
any active player gaining construction anchors in other cities. Existing valid
owners stay unchanged. A map with no spare owner fails if this is required.

## Regression

```sh
ctest --test-dir build --output-on-failure -R '^save_scenario_export$'
```

The synthetic saved world exercises three factions, damaged buildings and walls,
concrete under buildings and units, concrete-plus-road, stacked infantry, air
over a building, carried infantry and a live Retreat order. It uses bundled
content and does not depend on any private user save.
