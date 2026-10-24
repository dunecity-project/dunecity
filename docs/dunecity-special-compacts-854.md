# DuneCity special-building detail pass — 1.0.854 candidate

Recompacts 31 existing special-building stills from their original HQ images,
using balanced reduction at four times their native sprite canvas. This recovers
source detail instead of enlarging the previously reduced PNGs. Transparent
exterior cleanup and fringe removal preserve enclosed faction colors.

| Asset | Packages | Previous canvas | New canvas |
| --- | ---: | --- | --- |
| Church | 8 | 32×32 | 128×128 |
| Hospital: Harkonnen, Neutral, Rebels | 3 | 32×32 | 128×128 |
| Stadium: all except Harkonnen | 7 | 64×64 | 256×256 |
| Nuclear Plant: Harkonnen, Atreides, Ordos, Mercenary, Rebels | 5 | 64×64 | 256×256 |
| Airport | 8 | 96×96 | 384×384 |

The logical map destination, building footprint, placement and simulation remain
unchanged. This is source-detail scaling, not world scaling or zoom. All R/C/I
cells, 384 Industrial smoke frames, 33 portraits, Police stations, Atreides
Hospital and Harkonnen Stadium retain their existing bytes.

Hospitals and Churches are Residential civic overlays and previously missed the
direct high-detail building renderer because they have no standalone structure
item IDs. Their manifests now register by picture ID as well. Structure drawing
passes its active picture so the civic overlay selects its own house-specific
HQ source into the existing destination. SimCity, missing-house fallback and
fog retain their existing paths; no save/network/simulation fields change.

The Mac validation lane now exercises the existing isolated skin/cache probe,
including explicit Hospital/Church high-detail selection, SimCity exclusion and
missing-faction fallback. Probe screenshots and logs are retained as an artifact.

Authoring records point to versioned new Compacts; original sources and previous
Compacts remain available for recovery. Only the resulting PNG replacements,
renderer fix, tests and this documentation belong in the game repository.

Local validation: exactly 31 skin PNGs changed, with every other skin file
byte-identical to 1.0.853. All new dimensions match the intended 4× canvas.
Eight packaging tests and nine browser payload verification tests pass.
Hosted platform/render validation is pending; no 1.0.854 release is published.

This batch does not author the seven missing special buildings or the 32
missing dedicated special-building portraits, and does not add new activity
sequences. Those remain the following artwork passes. Dune2R is separate.
