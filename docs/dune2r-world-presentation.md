# Dune2R world presentation and basic infantry

## Isolation contract

The additional world presentation factor is **3**, enabled only for the built-in
`Dune2R` mod or its verified immutable Workshop snapshots, when the existing
Enhanced Visuals switch is enabled. Online sessions use `ws-<64 lowercase hex>`
names: their content base is resolved through `ModManager`'s verified revision
store, not an untrusted metadata file. A snapshot of another mod and an arbitrary
Dune2R-derived mod (including a snapshot of one) do not acquire this new
presentation factor. Canonical snapshots have no `Base Mod` in their verified
`mod.ini`; descendants do. This capability is cached at mod activation, restored
on activation failure and cleared when switching mods. Existing derived
mods' graphics-toggle policy is preserved separately.

Snapshot EditoR render preferences are saved in the user's main configuration,
under a separate section keyed by immutable snapshot identity. They never write
`workshop-render.ini` into an immutable payload. Built-in Dune2R keeps its existing
per-mod preference file. No download or preference change mutates a snapshot.
Asset pack Download and Refresh are disabled for immutable online snapshots;
their underlying calls also refuse before network activity or writes. Installed
packs remain inspectable. Download any additional artwork in the working Dune2R
mod before creating/hosting the next immutable revision.
Classic Dune2R, DuneLegacy, DuneCity, Tornie's Mod and other mods keep factor 1.
This is separate from the existing three zoom levels: switching graphics does not
change `currentZoomlevel`.

The simulation still uses `TILESIZE = 64`, the same map grid, building footprints,
pathfinding, weapon ranges, timing, multiplayer commands, save format and state
digest. Presentation is local, so peers may select different graphics and zoom
without changing their simulation coordinates.

`ScreenBorder` projects world coordinates and inversely projects pointer clicks
using the presentation factor. The camera center is preserved across a graphics
switch as far as map boundaries allow. Small maps remain centered in the playfield.

`Game::drawScreen` activates a scoped destination multiplier for the world only.
It restores the original drawing scale before the sidebar, menus, cursor and text.
Terrain, concrete, fog, rubble, tracks, classic corpses, fallback units, turrets,
projectiles, explosions and shadows retain their original atlas **source** cells;
only world destinations and positions grow. Manual structure footprints and
unit/harvester selection health bars follow the scaled destination.

## Size calibration

Classic single Devastator and Tank frames are 16px canvases, with visible widths
approximately 13px and 12px. The accepted enhanced Devastator uses `BaseWidth=48`,
`Scale=1`; the Ordos Tank uses `BaseWidth=48`, `Scale=1.21`, approximately 58px.
Their visible horizontal widths are approximately 47px and 38px, respectively.
These ratios support a 3x world footprint rather than shrinking new artwork into
the classic 16px grid.

At ordinary zoom 0, a world tile therefore occupies 48px, a 2x2 building 96px and
a 3x2 refinery 144x96px. Building artwork can extend vertically according to its
registered image anchor; changing its source resolution does not stretch the
footprint. Complete-unit HQ dimensions are already authored presentation pixels
and are **not multiplied again**: the Devastator stays 48px, the Ordos Tank stays
approximately 58px, and the new basic infantry uses a 40px canvas. The ordinary
zoom multiplier still applies to these values.

Missing remaster assets use the existing classic/scaler textures at 3x world
destination size. This is a temporary coherent fallback, not newly authored HQ
artwork. The Dune2R EditoR preview remains independently sized.

## Infantry package and animation

The approved package binds `ItemID=32` (`Unit_Soldier`), `HouseID=0` (Harkonnen),
`BaseWidth=40`, `BaseHeight=40`, `Scale=1`. Only basic Harkonnen soldiers in the
Dune2R mod or its verified Workshop snapshots enter this new infantry rendering
hook. Other houses, Troopers,
Saboteurs and legacy squad assets retain their existing render path and fallback.

All eight directions have these sections:

| Section | Runtime purpose |
| --- | --- |
| `Idle` | Exact low-ready standing pose |
| `Movement` | Approved looping walk |
| `Combat` | One-shot firing event, triggered by the existing real weapon fire |
| `CombatReturn` | One-shot lowering/recovery after firing |
| `DamageExploded` | Approved collapse/prone transition on an ordinary death |
| `DamageAftermath` | Final fallen still in the existing corpse layer |

`CombatReturn` is appended after the existing enhanced unit state IDs, preserving
old render preference keys. It is exposed as “Return to Idle” in the EditoR.
Movement takes priority over a stationary infantry firing recovery.

Optional `DurationsMs=30,60,120,...` supplies a positive integer duration for every
declared frame, in order. Each value must be 1..60000ms and the list must exactly
match `Frames`. Omit the list to retain the existing uniform `FrameMs` behavior.
Looping and one-shot clamping both honor the authored timeline. Whole-frame
registration and common image anchors are preserved; the renderer does not fit
or recenter each individual pose.

## Cosmetic death lifecycle

The gameplay unit still dies and is removed immediately through the existing
`GroundUnit::destroy()` path. No falling animation keeps an object alive, occupies
a tile, changes a health value, consumes RNG or sends a command.

An ordinary dead basic Harkonnen soldier may receive a local, nonzero renderer
token alongside the original corpse assignment. The default token is zero for
every other corpse. This prevents a nearby Trooper or older corpse with the same
owner/position from being mistaken for the enhanced soldier's death.

The token is **not serialized**. `Tile::save` and `Tile::load` retain their original
six corpse fields and byte order. Existing/load-restored corpses have token zero
and render through the classic fallback. `Game::computeStateDigest` does not read
the token or renderer history.

The renderer cache is bounded to 2048 events and the existing corpse lifetime
(32016ms). It clears when sprite caches reset for a game/map/mod and when a unit
mount revision changes. Token identities are not reused when the cache clears.
Missing/invalid animations, expired history or disabled Enhanced Visuals fall
back to the original corpse, with its existing sand decay and lifetime.

## Verification

`Dune2RPresentationTestCase.cpp` covers the strict built-in/verified snapshot gate,
rejection of other bases and malformed snapshot identities, Classic isolation,
camera/pointer inversion at all zoom levels, small-map margins, unchanged atlas
source cells, world/UI scope restoration, HQ sizing without double multiplication,
variable frame durations, bounded corpse identity/lifetime and source/save-byte
invariants preserving immediate simulation death, RNG calls and digest exclusion.

Before publishing, run the complete native test target and play-test graphics
toggle, selection/placement, fog/concrete, projectile effects, infantry walking,
firing/recovery and ordinary death in Dune2R. Confirm Classic, DuneCity and Tornie
remain unchanged. A live two-peer mixed-graphics multiplayer test is still needed
for release confidence; source isolation is not a substitute for that test.
