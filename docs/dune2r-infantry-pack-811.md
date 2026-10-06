# Approved Harkonnen Infantry pack — 1.0.811 candidate

This is optional **Dune2R** artwork, not a DuneCity skin. The pack is
`mods/Dune2R/graphics_hd/units/harkonneninfantry/`: 48 directional atlases and
one engine manifest, containing 386 approved poses (34,640,457 bytes).

## Registration and timing

Every approved 1024x1024 transparent source canvas was reduced uniformly to a
512x512 atlas cell with nearest-neighbor sampling. No pose was cropped,
individually fitted, recentered, or given a different scale. All cells use the
same image anchor, `256,451`, and a 40x40 presentation canvas at ordinary zoom.
The anchor was calibrated against the low-ready standing poses' boot baseline.
Atlas dimensions are bounded to 4096 pixels per axis and 16 megapixels.

The engine binding is `ItemID=32` (`Unit_Soldier`), `HouseID=0` (Harkonnen).
The classic `Unit_Infantry=46` is a squad that produces these soldiers; squad
creation and gameplay behavior are not replaced. Troopers and other factions
remain on their existing rendering paths.

| Direction | Walk poses | Exact walk duration |
| --- | ---: | ---: |
| East | 30 | 1200ms |
| Northeast | 16 | 1333ms |
| North | 14 | 1273ms |
| Northwest | 14 | 1273ms |
| West | 39 | 1300ms |
| Southwest | 14 | 1273ms |
| South | 8 | 880ms |
| Southeast | 13 | 1300ms |

Each direction also includes a low-ready idle still, an eight-pose firing
event (520ms), a lowering/recovery sequence, an approved prone/fallen transition,
and its exact final fallen still. Recovery and falling use 60ms per pose;
variable walk timing is preserved in `DurationsMs`. The approved prone/fallen
art is not represented as an independently verified realistic death performance.

`scripts/package-dune2r-infantry.py` takes an explicit, hash-bound local selection
and validates all source images, directional/state coverage, exact timing and
atlas bounds before producing PNG/INI runtime files. Its eight offline tests
include source mismatch rejection, ordering/timing and geometry preservation.
Neither source selection manifests nor generation metadata belong in this pack.

## Immutable publication

The asset-only commit is `e4285d2d1e8f4e633b724a9f1040705a25313c5a`.
The catalog pins every pack file to that commit, with a size and SHA-256, using
the transferred `dunecity-project/dunecity` repository. Existing immutable
`VR48/dunecity` catalog URLs remain accepted for older clients; arbitrary
repositories, traversal and executable payloads remain rejected.

An independent ordered-pixel comparison checked every atlas cell against its
approved source. The ordered source digest is
`102f36f9cd6e56bfd7d1c46b2a49acb8b1572ab883ff2c0b9d6250c5a71d2a2c`;
the ordered runtime-file digest is
`051f39801f2b821beed9613c68872553dee074c738f849ebc2d05535003c69a3`.

Base desktop, Android and browser packages continue excluding downloadable
Dune2R artwork. Players download it through the existing asset manager and
preview it in the Dune2R EditoR. The new Return to Idle section is available
there, and missing sections or uninstalled packs retain classic fallback.

See [world presentation](dune2r-world-presentation.md) for the separate Dune2R
scale change and multiplayer/save isolation. This candidate has not cleared
the upstream browser observer-promotion release gate; the asset commit is
not a stable release announcement.
