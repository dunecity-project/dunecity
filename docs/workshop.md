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
remain readable. Saves and replays resolve their original revision from the local
store, then from installed mods with the same bytes, and finally by downloading
the pinned mod revision. Legacy saves with a known mismatched checksum report a
failure rather than loading unrelated current content.

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
