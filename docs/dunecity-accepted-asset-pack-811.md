# DuneCity accepted presentation pack — 1.0.811 candidate

This candidate adds 30 accepted special-building packages to the DuneCity
`Dune2` skin. They are presentation assets only: city simulation, placement,
collision, multiplayer rules, and the 2×2 R/C/I footprint remain unchanged.
No Dune2R unit, terrain, or enlarged-world asset belongs to this pack.

## Verified coverage

- 65 packages: 24 faction-specific R/C/I zones and 41 special buildings.
- 784 PNG files and 65 engine INI manifests.
- All eight factions retain their Residential and Industrial architecture,
  Commercial sprites, and their matching sidebar portraits.
- 190 authored Residential/Industrial static cells, with native fallback for
  the two un-authored vacant Rebels Industrial cells.
- 48 developed Industrial cells have complete ordered eight-phase smoke
  animations at 128 milliseconds per phase: 384 smoke frames in total.
- 33 authored portraits retain the approved 182×110 landscape dimensions.
  A building without an authored portrait keeps the existing fallback.

The 789 previously published files are byte-identical to the prior Git blobs.
The asset delta is exactly 60 new files: one manifest and one accepted static
PNG for each of the 30 newly included special buildings. It does not replace
older successful artwork or copy source images, authoring manifests, account
details, or generation-workflow metadata into a release.

## New special-building coverage

| Faction | Newly included accepted buildings |
| --- | --- |
| Harkonnen | Airport, Church, Hospital, Nuclear Power Plant |
| Atreides | Airport, Church, Nuclear Power Plant |
| Ordos | Airport, Church, Nuclear Power Plant, Stadium |
| Fremen | Airport, Church, Stadium |
| Sardaukar | Airport, Church, Stadium |
| Mercenary | Airport, Church, Nuclear Power Plant, Stadium |
| Neutral | Airport, Church, Hospital, Stadium |
| Rebels | Airport, Church, Hospital, Nuclear Power Plant, Stadium |

Seven unaccepted special slots are not bundled: Fremen, Sardaukar, and Neutral
Nuclear Power Plants; Ordos, Fremen, Sardaukar, and Mercenary Hospitals. Their
native fallback remains available. Adding a static special building does not
claim that all of its future activity animation or portrait artwork is done.

## Packaging and cache compatibility

All platforms consume the same `mods/dunecity/graphics_skins/Dune2` packages.
Desktop CMake copy/install rules include this tree. The Android packager copies
the DuneCity tree and derives its separate graphics-cache marker from relative
paths and file-content hashes. Desktop startup likewise refreshes changed
bundled graphics without rewriting gameplay settings or saved games. Neither
path requires players to delete their profiles after updating.

Emscripten preloads the complete DuneCity mod, and `scripts/check-web-mods.py`
checks each skin file against the actual browser data archive. Optional Dune2R
art remains excluded from desktop, Android, and browser base packages and
continues to use its independent downloadable catalog.

## Validation and release limits

The accepted-export rehearsal and the installed package audit verified every
manifest reference, all PNGs, unchanged prior files, package counts, zone
footprints, activity phase order/timing, and portrait dimensions. Eight skin
packaging tests and eight browser-mod verification tests passed.

These are source/asset checks, not proof of a completed platform build or live
multiplayer play test. The upstream observer-promotion browser heap-trap hold
recorded for 1.0.810 remains a separate release gate; adding artwork does not
resolve that defect or authorize advertising a candidate as stable.
