# Micropolis city aircraft

1.0.822. The city Airport launches the two original Micropolis aircraft: the
traffic helicopter and the airplane. They use the original pixel art, the
original movement and reporting arithmetic, and the engine's ordinary air-unit
lifecycle, so hostile anti-air can shoot them down.

They reuse the existing `Unit_AmbientAirplane` (50) and `Unit_AmbientHelicopter`
(51) item IDs and the existing `AmbientAirplane`/`AmbientHelicopter` classes. No
new unit type was added; the placeholder orbit-and-expire helicopter and
edge-to-edge flyover plane were replaced by the real behaviour.

## Reference sources

Read-only originals, inspected for this work. The engine reference is
MicropolisCore revision `c591573ad8c8c8bbecf26cea018e2c164c22c252`:

| What | Where |
|---|---|
| Sprite behaviour | `MicropolisCore/MicropolisEngine/src/sprite.cpp` — `doCopterSprite` (695), `doAirplaneSprite` (800), `turnTo` (396), `getDir` (477), `spriteNotInBounds` (459), `newSprite` SPRITE_HELICOPTER/SPRITE_AIRPLANE cases (225/240), `generateCopter` (1969), `generatePlane` (1985) |
| Airport launch | `MicropolisCore/MicropolisEngine/src/simulate.cpp::doAirport` (1651) |
| Helicopter steering | `MicropolisCore/MicropolisEngine/src/traffic.cpp::addToTrafficDensityMap` (185) |
| Message text | `micropolis/micropolis-activity/res/stri.301` line 41, "Heavy Traffic reported." |
| Sprite clock | `micropolis/micropolis-activity/src/sim/sim.c:70`, `int sim_delay = 50;` |
| Pixel art | `micropolis/micropolis-activity/images/obj2-0.xpm` … `obj2-7.xpm` (helicopter, 32x32), `obj3-0.xpm` … `obj3-10.xpm` (airplane, 48x48) |

All of it is the GPL Micropolis release, Copyright (C) 1989-2007 Electronic Arts
Inc., GPL-3.0-or-later with the full additional terms recorded in
`imported_sprites/micropolis/NOTICE.txt`.

## Artwork

`scripts/import-micropolis-aircraft.py` decodes the original XPMs and re-packs
them, unchanged pixel for pixel, into two tracked sheets:

```
imported_sprites/micropolis/aircraft/city_helicopter.png   8 x 1 cells of 32px
imported_sprites/micropolis/aircraft/city_airplane.png     8 x 4 cells of 48px
imported_sprites/micropolis/aircraft/manifest.json
```

Only the frame order changes. Micropolis numbers sprite frames from 1 (frame 0
means "inactive"), so `objN-k.xpm` is sprite frame `k+1`, and frames 1..8 are
north, north-east, east … north-west. The columns are re-ordered into Dune
Legacy's `ANGLETYPE` (RIGHT, RIGHTUP, UP, LEFTUP, LEFT, LEFTDOWN, DOWN,
RIGHTDOWN). The airplane's rows are the eight cruise headings, then take-off
frames 9, 10 and 11; take-off art is heading-independent upstream, so each
take-off row repeats its single frame across all eight columns and the
renderer's angle indexing stays harmless.

Micropolis tiles and DuneCity zoom-0 tiles are both 16 px, so the helicopter
covers 2x2 tiles and the airplane 3x3, exactly as in the original. Nothing is
rescaled at import; zoom levels 2 and 3 are nearest-neighbour integer scales,
as for the other imported city art.

```sh
python3 scripts/import-micropolis-aircraft.py --source <micropolis-activity/images>
python3 scripts/import-micropolis-aircraft.py --check   # byte-compare the tracked PNGs
```

The art reference is pinned to upstream revision
`ccc346165564a2ceec86a588861d7e7c27911714`. The manifest uses an upstream relative
path so checking the import works from any reference checkout. Full upstream
notices and GPL version 3 ship alongside the imported assets.

Pillow is an authoring dependency only. `manifest.json` records the source file
and SHA-256 of every frame; `tests/CityAircraftPolicyTestCase.cpp` decodes the
shipped PNGs and checks the layout and provenance without Pillow.

The sheets load in `GFXManager` next to the city atlases as
`ObjPic_CityHelicopter` and `ObjPic_CityAirplane`, are house-independent (listed
in `usesSharedCityAtlas`), and are copied into the app bundle, the Windows and
Linux installers and the browser package by the existing `imported_sprites`
rules in `src/CMakeLists.txt`. A missing or wrong-sized sheet throws at load
rather than flying an invisible aircraft.

## Translating the original clock and speeds

Micropolis moves sprites one `MoveObjects()` pass every `sim_delay` = 50 ms, in
1/16-tile units. DuneCity runs 16 ms game cycles. `CityAircraft::spriteCycle()`
converts exactly (`gameCycle * 16 / 50`) instead of rounding to three or four
cycles, so the original's `(spriteCycle & 3)` helicopter turn and
`(spriteCycle % 5)` airplane turn keep their cadence with no drift.

The original per-tick axis steps are 5/16 tile (helicopter) and 8/16 tile
(airplane). In DuneCity world units per game cycle that is exactly 6.4 and
10.24, which is what `config/ObjectData.ini.default` now carries; `CityAircraft::worldSpeedMilli()` derives those two numbers from the
original tables and the test asserts the ini against it.

The diagonal table components are preserved too: (3,3) for the helicopter
and (6,6) for the airplane. Their `getMaxSpeed()` adjusts the diagonal magnitude
used by `AirUnit::move`, retaining the ordinary FixPoint movement, tile
assignment and spatial-grid updates. Native tests measure all sixteen heading
vectors against independently transcribed original tables, allowing less than
0.01 world units (1/6400 tile) for the engine's fixed-point trigonometry.

## Helicopter

`doCopterSprite` only ever runs its `control < 0` branch: `makeSprite` sets
`control = -1` and nothing for the helicopter ever sets it back, which makes the
`absDist < 16` branch at sprite.cpp:749 dead upstream. The port implements the
live branch:

- `patrolCount` starts at the original 1500 sprite ticks and counts down.
- While it is running, the helicopter is steered at saturated traffic. Upstream
  `addToTrafficDensityMap` *pushes* the cell it just saturated (density 240) at
  the sprite; DuneCity keeps traffic in a map layer rather than a per-journey
  stack, so the helicopter *pulls*: the same saturation threshold, one of the
  congested cells drawn from the shared simulation RNG. Drawing one rather than
  taking the nearest matters — the nearest is constant, and the helicopter would
  park on one corner instead of touring the city.
- The scan is bounded to `kSeekRadiusTiles` (24) around its own airport and to
  roads whose tile owner is its own house. The original has one city filling the
  map; DuneCity maps are shared, so "its city" has to be said explicitly.
- When there is nothing congested to visit, it picks a random road of its own
  city as the next waypoint (upstream picks a random map point, which in a
  one-city map is the same thing).
- Reporting is the original gate: density above 170 under the helicopter, one
  draw in eight, then a 200 sprite-tick cooldown. The report also requires the
  tile to be a road owned by the helicopter's house, so one city's jam never
  shows up in another player's news. Only `pLocalHouse` sees the message.
- At `patrolCount == 0` it heads home and, within 30 units of the pad, lands:
  removed with no explosion and no wreck, exactly like `sprite->frame = 0`.

The news text is the original string plus the tile, so the notice is localised
in both senses: `"Heavy Traffic reported." (x, y)`.

## Airplane

- `newSprite`'s two cases are kept. An airport more than 20 tiles from the
  eastern map edge runs the take-off (frame 11 → 10 → 9 → cruise east, one step
  every five sprite ticks); one closer to that edge starts already westbound on
  frame 7 with no take-off run.
- Cruising steers with `turnTo(getDir(...))` every five sprite ticks.
- At `absDist < 50` it draws a new destination from
  `[-50, size*16 + 50]` in both axes — the original's margin outside the map,
  which is how the plane eventually leaves. Airplane waypoints bypass the
  ordinary map-tile destination validator, so those outside coordinates are
  retained. Negative coordinates that collide with the engine's `-1` sentinel
  are moved one tile farther out. The plane is removed, with no explosion,
  when its centre crosses the map boundary.
- DuneCity retains a 6,000-sprite-tick cruise budget. Expiry now starts an
  outward departure instead of deleting the plane over the city: it keeps an
  existing outside waypoint or picks the nearest edge deterministically, then
  uses the original turning clock to fly through that edge. Departure stops
  the random waypoint draws and persists in the existing budget and destination
  fields.
- It has no landing or arrival phase: it has no home, never seeks traffic and
  never returns. Micropolis implements takeoff and free flight, then off-map
  retirement; this port keeps that behavior.

The airplane-to-aircraft collision explosions are omitted; they are part of
Micropolis's disaster system (`enableDisasters` in `doAirplaneSprite`). The
original's `getDir` bug is preserved: its
`dispY * 2 < dispY` correction never holds (upstream marks it
`XXX This never holds!!`), so due east reports east-south-east. Fixing it would
change every heading the originals fly.

## Launch

`Airport::updateCityAircraft()` is the counterpart of `doAirport()`: one chance
in six of a plane, otherwise one in thirteen of a helicopter, drawn from the
shared deterministic simulation RNG. `generatePlane`/`generateCopter` return
early when that sprite already exists, so the original city has at most one of
each; here the singleton is per owning house, so two cities each keep their own
pair and a second airport cannot double the fleet. Only an operational airport
launches: alive, powered and in city-simulation mode.

The cadence is DuneCity's: the roll runs on the same five-second structure tick
as the Airport's existing ornithopter patrol, because Micropolis's simulation
pass has no counterpart here. The map-wide spawn loop that used to live in
`Game::updateGameState` was removed — it gave every airport a second,
independent chance per pass and allowed three aircraft per house.

## Combat and lifecycle

Both are civilian: no weapon (`canAttackStuff = false`), `attackMode = STOP` so
they never run a target scan, not selectable and not commandable. They are
ordinary air units otherwise, so the existing rules apply unchanged and no
targeting rule was widened:

- `ObjectBase::canAttack` still refuses every flying target, so ordinary ground
  weapons cannot hit them.
- The engine's existing air-capable attackers can: `RocketTurret`, `Launcher`,
  `EliteLauncher`, `Trooper` and `Ornithopter`. Their `canAttack` overrides
  already omit the flying-target filter; none of them was changed.
- Every one of those requires a different team, so allied and friendly
  units never attack them.
- They die through the normal projectile and `UnitBase::destroy` path:
  removed from the map, the object manager, the unit list and the house item
  count, which frees the airport's singleton slot for the next launch. Their
  `InfSpawnProp` is 0, so a shot-down civilian aircraft leaves no infantry.

## Save and protocol compatibility

`SAVEGAMEVERSION` remains **9853** (the aircraft block added over 9852).
`NETWORK_PROTOCOL_VERSION` is **62** for 1.0.822.

The feature was first built as local 1.0.817 from production 1.0.816, where the
next free protocol number was 58. Carrying it onto production 1.0.820
renumbered it to 61: 58, 59 and 60 are main's QuantBot civic/economy decisions,
which this build keeps. The flight-exit repair advances it to 62 because
changed routes and retirement times also change subsequent Airport RNG draws.
The repair adds no saved fields; 9853 aircraft saves still load.

The helicopter now persists its home pad, patrol count, report cooldown, scan
cooldown, report tally and return-home flag; the airplane its take-off frame and
flight budget. None of that is recomputable after a load: a report cooldown and
a patrol count decide whether the next congested tile produces a message or
silence, and a take-off frame decides which artwork is on screen.

A 9852 save still loads. The old placeholder fields are read and discarded, the
aircraft keeps its position and starts a fresh patrol or an airborne cruise,
because the old orbit/flyover state has no meaning in the new behaviour.

The protocol bump is required for a different reason: the Airport draws from the
shared simulation RNG with the original odds, so a peer on 60 consumes a
different number of RNG values on the same cycle, flies the placeholder motion
and keeps up to three aircraft per house. The two diverge immediately.

## Tests

`tests/CityAircraftPolicyTestCase.cpp` (in `dunelegacy_tests`, run by ctest)
checks `turnTo` over all 64 direction pairs and `getDir` over an 81x81
displacement grid against tables transcribed separately from `sprite.cpp`, the
direction/angle mapping against the original step vectors, the take-off
sequence, the report gate and its one-in-eight rate, `spriteNotInBounds`, the
sprite clock over a simulated minute, the shipped `ObjectData` speeds against
the original steps, and the shipped PNGs' dimensions, distinct frames,
take-off row layout and manifest provenance.

`ctest -R city_aircraft_probe` builds `tests/units/city-aircraft-probe.inc`
against the existing Release objects and drives the real
`Game::updateGameState` loop with real Airport, aircraft, turret and bullet
objects, in an isolated profile with dummy SDL drivers. Phases: art actually
loaded at every zoom and house; airport launch odds, per-house singleton,
two-airport case and the unpowered airport; the traffic patrol's reports,
bounded rate, road coverage and the foreign-city silence; the airplane's
take-off frames, climb-out, heading changes, off-map ending and the
eastern-edge variant; hostile anti-air destroying both with no tile, unit-list
or house-count leak while same-team and ground weapons cannot target them; and
a save taken mid-take-off restoring its visible artwork immediately and replaying
300 frames bit-identically after reload. The exit regression also checks
retained random outside waypoints, negative/sentinel coordinates, actual
movement across every edge, departure after budget expiry, lifecycle cleanup,
and 300 identical frames after loading an outside departure waypoint.
Sparse-road steering checks a road on
the opposite coordinate parity from its traffic cell origin.

```sh
python3 tests/units/run-city-aircraft-probe.py --output-dir <dir>
```
