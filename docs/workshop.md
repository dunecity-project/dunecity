# Workshop revisions and online content

Editors (Map Editor, Mod Editor, Asset Editors) create maps and mods. Extras
contains Replays, How to Play, and About & Credits. Choose a mod when setting up a
game. There is no separate activation screen.

## Creating

The mod editor starts with a project chooser. Copy a bundled mod or an installed
exact version to make an editable draft. Copies include the complete asset and
campaign tree, metadata, custom houses, and rules. The editor changes unit/building
properties, game rules, AI configuration, and descriptive metadata. It validates
field types and stages changed files before replacing the draft.

The map editor asks which mod supplies its units and rules. Save captures the map
and its required mod as a local revision. The map format's `[BASIC] Version`
remains the existing terrain format; it is unrelated to the Workshop revision
number.

Asset Editors currently provides the existing Dune2R sprite previews, asset-pack
downloads, and animation-render settings. Choose an editable Dune2R copy. Its
render settings live in the mod's `workshop-render.ini`, so saved revisions
include them. This is not a general pixel-painting editor.

## Removed: community catalogue and map sharing (2026-10-18)

The game no longer browses, uploads or automatically shares maps and mods:

- no Community Maps & Mods / "Load from metaserver" browser, no Custom Game
  "Metaserver Maps" tab, no map-editor "Save/Load online" buttons, and no
  "Save to metaserver" in the Mod Editor or Dune2R asset editor;
- no automatic collection of offline custom maps ("queued for the community
  catalogue") and no LAN-host upload queue;
- no per-frame upload pump in menus or games, so no background TLS requests.

Earlier releases kept that queue in `workshop/outbox/` and retried it every minute.
Opening the revision store now deletes that folder once and logs
`Workshop: removed the retired automatic sharing queue`. Revisions, drafts and
saves are untouched; old `collected` receipts left in revision folders are
ignored.

## Bundled community maps (2026-10-24)

With the catalogue browser gone, the player-made multiplayer maps from the
catalogue are shipped instead. All 1,267 map revisions (1,102 items) were
downloaded and reduced to genuinely new maps: dropped were copies of bundled maps
(same bytes, same content ignoring header/author fields, or same terrain), maps
named like a bundled map, auto-generated `Random ... T<n> <seed>` maps, RWRK
campaign scenarios, placeholders/tests, and maps whose slots or starts do not
match their name. Each accepted map is the latest revision of its item. The initial
import played two identical two-minute all-AI games per map (equal final digests);
Scattered War initially used Vanilla and is now associated with its exact bundled
`new - modded` dependency. See [the mod reviews](community-mods.md) for that change,
the corrected Wezowe Pola provenance, rejected mods and the current validation status.

The files are the exact catalogue bytes. The catalogue has no author field, so
`data/maps/community-maps.json` records each map's catalogue item ID, version,
revision hash and original name, the `[BASIC] Author` written into the map where
there is one, and the mod revision it was published with. Sidecars carry the
clean display name, item ID, mod and version. Dune City maps live in
`data/maps/singleplayer/`, the others in `data/maps/multiplayer/`; all are
listed in Custom Game and the multiplayer map chooser (All Maps and the mod
filter). `ctest -R community_maps_startup` checks every entry.

| Map | Mod | Author | Catalogue item, version |
| --- | --- | --- | --- |
| 2P - 128x128 - 1v1 - Habbanya-Penny Revised | Tornie | Rippsblack | `762816b6…` v1 |
| 2P - 128x128 - Deserted Revised | Tornie | kc | `ca99d13b…` v2 |
| 2P - 192x192 - Kleiner Lvl | Vanilla | – | `cba45259…` v1 |
| 3P - 128x128 - Oli5 | Vanilla | oli | `7358c937…` v1 |
| 3P - 128x128 - Wezowe Pola | Vanilla | – | `1eae1e5b…` v1 |
| 4P - 128x128 - Habbanya-Froiud AA | Dune2R | Rippsblack | `08d7c9c8…` v1 |
| 4P - 448x448 - Big Border | Vanilla | – | `309721df…` v1 |
| 4P - 448x448 - No Mercy | Dune City | – | `522b446c…` v2 |
| 5P - 128x128 - Fremen Homebase | Vanilla | Dune lover | `148fbc36…` v2 |
| 6P - 128x128 - Alla Map | Dune2R | Mmed | `59de4d55…` v1 |
| 6P - 512x448 - No Win | Dune2R | cYN | `93133d16…` v1 |
| 8P - 256x256 - Scattered War | new - modded (exact original dependency; folder `new`) | TijnAG07 | `92791220…` v4 |

Big Border also places Fremen and Neutral fortresses without lobby slots; Fremen
Homebase is a four-against-one scenario whose Fremen slot starts with a large army.
Earlier catalogue maps already bundled: Reacciona rapido forro, Alto mapa HD and La
guerra dei ricchi (1.0.827), and the Dune City maps in `data/maps/singleplayer/`.

## Versions and storage

A draft has a permanent item ID. Changed content creates the next numbered
revision; saving identical bytes and dependencies reuses the revision. Display
names are not identities. Renaming a draft keeps its item ID; independent drafts
with the same name remain separate. Older unversioned maps obtain a lineage
namespaced to the local publishing identity and their original name/author.
Bundled mod snapshots derive their IDs from the complete content.

The user data directory contains:

- `workshop/revisions/<sha256>/`: immutable manifest, version, and verified files.
- `workshop/owner`: private local identity used when an online host uploads its mod.
- `mods/<draft>/workshop-revision.ini`: draft ID and latest saved revision.
- `mods/ws-<sha256>/`: installed exact mod version, with its verified identity.
- `<map>.workshop.ini`: map revision and dependency lookup metadata.

A revision's SHA-256 covers a canonical manifest, including item identity,
display name, engine family, exact map-to-mod dependency, and every file's path,
size and SHA-256. Revision numbers are labels; hashes remain the permanent
references in saves, replays, and network settings.

## Multiplayer and saves

Approved shipped mods (Vanilla, Dune City, Tornie and Dune2R) use their installed
files. Campaign starts online by default and automatically starts after admission;
it never captures, uploads or downloads a Workshop package. These games permit
hot joining. Join Online -> Create Campaign retains the editable pregame lobby.
Admission and readiness compare a self-identifying approved-mod token based on
the installed rules, alongside the matching app/protocol version and runtime
configuration. A joiner selects the corresponding installed mod locally.

New/authored mods require the pregame lobby, with hot joining disabled even for
copies based on an approved mod. An online host of such a mod uploads that exact
mod revision (`/v1/content/begin`, `chunk`, `commit`) before advertising; this is
the only upload the game still performs. Joiners missing it download the pinned
revision (`/v1/content/manifest`, `blob`) before admission. Maps are never uploaded:
the host sends the map bytes and manifest in the game setup, and joiners save a
received map only after its manifest and payload hashes match. Same-name revisions
receive separate installed filenames.

LAN games use exact local content or the verified peer transfer for packages up
to 10 MiB. For new mods, the lobby, readiness gate, and game start check the full
mod revision as well as runtime configuration. A missing or mismatched revision
cannot silently start.

MOD4 game settings record exact map/mod hashes and numbers. Older MOD3 settings
remain readable. A save of a shipped mod loads with the installed mod when its files
are the pinned revision's. Vanilla and Dune City stamp the game version into their
`mod.ini`, so a save from an earlier release is compared under that release's stamp
(named by the save header). The two captured older builtin metadata layouts,
including their duplicate Version field and original quoting/order, are also
recognized for Vanilla and Dune City only. In every case the full hash must match
after substituting only that known metadata: current rule, option and asset bytes
must be identical, and base-mod/city-mode semantics must agree. Unknown metadata
fields or a changed payload do not qualify. The game then re-pins the unchanged
shipped content as currently stamped.

Otherwise saves and replays resolve their original revision from the local store,
then from installed mods with the same bytes, and finally by downloading the
pinned mod revision. Legacy saves with a known mismatched checksum report a
failure rather than loading unrelated current content.

Verified in shared source `18e64e8f`: the actual MBA `fpsstillfucked` (1.0.849)
and `skipping` (1.0.850) saves both load in fresh profiles without the MBA's mod
store, advance 600 cycles, re-save as 9874 and reload. `workshop_store_tests`
passes, including wrong stamps, changed payload/mode, unknown metadata and
current-version re-saves of the historical metadata layout. Evidence:
`dunecity-results/regression-852/savecompat/current-fresh-18e64e8f/`.

## Service and validation

The content API is part of `tools/p2p-signaling`, deployed as the dunelegacy.com
`p2p-service`. The game no longer calls `/v1/content/list` or `/v1/content/lookup`
and no longer sends `collect=1`; the service still answers them for older clients.
Only HTTPS is allowed, except an explicitly selected loopback development URL.
Transfers are cancellable and chunked, and rate-limited transfers retry without
publishing partial revisions. The browser uses the existing IDBFS persistence
sync hooks.

Validation includes native CTest suites (`workshop_store_tests` covers the store
and the retired-queue cleanup), menu probes, PHP service tests, and a real C++
client-to-PHP transfer test:

```sh
python3 tools/p2p-signaling/test/test_content.py
python3 tools/p2p-signaling/test/test_client_smoke.py
ctest --test-dir build --output-on-failure
```
