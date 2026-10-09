# User scenarios in release 1.0.820

The bundled single-player catalogue includes the authored maps captured from
Stefan's MBA on 2026-10-09. The original save, user maps and Workshop metadata
remain unchanged. Six previously missing maps are copied byte for byte:

- 2P - 128x128 Four Quadrants
- 4P - 192x192 - BigCityLife
- 4P - 192x192 - New Beginning
- 4P - 384x384 - new beginning
- 8P - 256x256 - Colonists
- 8P - 384x384 - Colonist

4 corners, DuneCity, test and city seige were already bundled. They now carry
catalogue metadata too, so all ten authored scenarios retain their Dune City
mod dependency when browsed on a fresh installation. Metropolis was already
bundled in the multiplayer catalogue with Stefan's author attribution. The
local laptop's thirteen scenario files are also already present byte for byte.
Downloaded community maps retain their existing authorship; the PR89 infantry
verification fixture remains a development fixture.

Historical filenames stay unchanged. Their real lobby slot counts drive the
player filter: the 384x384 new beginning map has eight slots despite its 4P
filename, and test has six despite its 5P filename. A declared Neutral faction
counts as a selectable slot. Ambient units owned by Neutral without a Neutral
section do not create another selectable slot.

## Catalogue metadata

Each promoted map has an `.ini.workshop.ini` sidecar with its name, Dune City
dependency and revision number. These bundled hints deliberately have no cached
revision hash. Starting a new game captures the bundled map bytes under the
selected mod, using the existing exact-content pinning path. A nonempty invalid
revision hash still fails; save and multiplayer join revision checks stay strict.
The immutable flag makes the editor give an edited copy a fresh Workshop identity,
using its existing fork behaviour.

The native app copies the whole `data` directory. The browser payload checker
also verifies the promoted INIs and their catalogue sidecars byte for byte.

## 3waysplit conversion

The native exporter loads the developed saved world and writes an ordinary
scenario using the map editor. It preserves the saved layout and forces as a
fresh starting point. As accepted by Stefan, city population, city simulation,
production queues and live orders restart. The source save remains available
for resuming the original match.

Conversion counts, any placement normalizations and engine reload checks are
recorded with the conversion receipt. The reusable command and precise format
limits are documented in `tools/scenarios/README.md`.

The converted map is **3P - 192x192 - 3waysplit**: Harkonnen (team 3),
Sardaukar (team 4) and Neutral (team 1). It retains **4,525 structures and 831
units**, all health within scenario quantization, 1,428 concrete tiles and all
9,650 saved road tiles. The source save has SHA256
`af8eb37806dcda5ef6296c064ac128a11e0d8f051a55eb5524e69cf52edcd630`.

Five carried/repairing units start near a legal tile in their own city. Twenty-three
Retreat orders reset to Area Guard. The ordinary loader adds 1,536 road frontage
tiles around buildings. 4,955 previously unowned substrate tiles use passive
Atreides ownership: it has no selectable slot or forces, and none of the three
active factions gains construction range from these tiles. These are declared
fresh-scenario normalizations, not a checkpoint replay.

## Try it

On the MBA running 1.0.819, the scenario is available under **SP User Maps**.
Set the player filter to **Any**: the old app undercounts declared Neutral
factions in the preview. Release 1.0.820 corrects that count to three.
For release 1.0.820, use **SP Maps**, choose **Dune City 1.002**, and select
**3P - 192x192 - 3waysplit**. Leave SimCity enabled. Select your player name for
one faction to play, or enable **Spectate** and choose QuantBot for all three.
Shared house is optional for normal play and unnecessary for Spectate.

The next release bundles all eleven authored single-player scenarios, including
this conversion. The existing multiplayer Metropolis map and thirteen laptop
scenarios already present in the bundle remain available.

## Verification

The native Release build, version consistency and Ninja dependency audits pass.
The full unit gate, map collection/chooser, spectator setup, synthetic export,
input preservation and web payload tests pass. Each of the ten original authored
maps starts in a fresh profile and advances 3,750 cycles, including both generic
eight-slot 384x384 maps. The converted map also passes a fresh-default-mod,
all-AI spectator startup and sixty seconds of simulation. Menu verification
covers all eleven bundled scenarios at 640, 854 and 1280 pixels.

The packaged maps and sidecars are byte-identical to the source copies. A
map-only incremental build dependency now reruns bundle packaging. Browser
payload checker regressions pass. Release 1.0.820 was published on 2026-10-09
from tag v1.0.820 (8ed2e69c). Stable CI 37877452294 passed all platforms,
Apple notarization and updater signing. Published Mac ZIP maps and metadata
are byte-identical to these source files. The exact stable browser artifact
passed its content audit and was deployed by website run 37878733753; the live
manifest and all eight artifact hashes match the release. SourceForge run
37878677986 verified readback hashes, source refs and all three OS defaults.

Detailed conversion and installation receipts are under
`../outputs/scenario-promotion-20261009/`. The actual save reload and scenario
reload use separate processes. The report lists every unit relocation, reset
order and automatic frontage tile. These checks establish preservation and
startup, not full-match balance or a resumed city simulation.
