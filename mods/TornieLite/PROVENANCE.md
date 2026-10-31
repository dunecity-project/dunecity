# Tornie Lite 1.0.546 provenance and DuneCity integration

## Source and authorship

- Upstream repository: `Torneko/dunelegacy-tornie` (https://github.com/Torneko/dunelegacy-tornie).
- Upstream version: `1.0.546`; commit `ce89e53ccc51847fcd41540a6a4cd132acf9bf99`.
- Integration target: `VR48/dunecity`, based on `dbd410fc` (DuneCity 1.0.862).
- The upstream mod metadata and provenance credit **Tornie_Panther** for Tornie configuration, campaign rewrites, balancing, faction design, graphics direction and audio direction. Tornie Lite retains those credits; the edition name is not a separate authorship claim.
- Upstream records Wildspade/Kleshmersh names, heralds and house-name voices as supplied user contributions. Doublefinery art was supplied by Tornie; its final 80x224 seven-frame indexed sprite is preserved without resizing.
- Inherited Dune Legacy/DuneCity files retain their existing authorship and licensing status. Redistribution of inherited or derivative assets remains subject to the rights of their respective owners.

## Imported and adapted

- Authored graphics, voices, data, solo campaigns and `campaign/coop/**` come from the stated commit. Campaign plans are mod-local in `data/CampaignPlan.ini`; the non-Tornie editions retain the campaign baseline published by upstream at this commit.
- Campaign/faction/co-op feature history includes `47efae59`, `e1ce006d`, `4c26d6cc`, `882a9ce3` and campaign rebuild `b951fb4e`; offline achievements originate in `3b3dc108`. Their DuneCity engine adaptation retains the current direct-P2P transport and same-house co-op.
- Twelve faction score/region frames are shared once under `data/frames/` and loaded only for family content. Their lossless recompression preserves decoded pixels and all original colour/profile chunks.
- DuneCity metadata replaces stale upstream version/integration claims. The roster is declared in `mod.ini`; six legacy factions are playable; the DuneCity Natives house is never a player slot.
- Family menus use the declared six/twelve-faction roster and up to twelve custom rows, while legacy nine-row/eight-random-house behavior remains unchanged. The campaign engine understands revision-2 role/support plans and inherited family `openCampaignFile` lookup (`b7ec090e` presentation/campaign work included).
- DuneCity keeps fixed faction IDs, its custom-house palette index 192, separate guest palettes and Natives allocations. Upstream Jericho runtime-slot swaps are represented by canonical faction identities, not enum changes.
- `City Effects = false`; DuneCity city simulation and same-house co-op are preserved outside this content. Tornie retains DuneCity-only object definitions, calibrated Rocket-Turret/Launcher cadences and newer QuantBot Brutal tuning rather than reverting those unrelated changes.
- The obsolete upstream concrete-degradation option is superseded by DuneCity's current Dynasty-aligned foundation/power policy. Harvester-count visibility uses the local `[Video]` setting, not a synchronized mod rule; neither ignored key is shipped in `GameOptions.ini`.
- Gameplay capabilities are gated by the active content base in the six-edition Tornie family, including derived and immutable Workshop copies. Switching to Vanilla, DuneCity, Dune2R or another family removes this payload from the active search path.
- Compatibility uses DuneCity save version 9878/protocol 108, a family engine-capability signature, canonical manifest and complete payload checksums. Direct lobby transfer is bounded at 12 MiB (largest framed family payload plus margin) and verifies staged payloads before activation.

## Exclusions

The upstream authenticated host gameplay relay/fallback forwarding (`12d67c60`) is excluded. Multiplayer game traffic remains direct peer-to-peer. Upstream AppImage, Windows, Android release, SourceForge, website/publication tooling, dependency changes and deletions of DuneCity features are not imported. No original Dune II executable, installer, APK or base-game PAK archive is added by this port; `Tornie.PAK` is the upstream mod-specific content archive.
