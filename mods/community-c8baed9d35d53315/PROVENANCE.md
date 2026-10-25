# Tornie 1.0.524-23 provenance and integration notes

## Source

- Upstream repository: `Torneko/dunecity-tornie`
- Upstream tag: `tornie-v1.0.524-23`
- Upstream commit: `f7eb26269c6832f06aa9412bc752e93e997550fb`
- Integration target: `VR48/dunecity`
- Integration base: `e7a5a1b`

The upstream fork was audited and ported selectively. It was not merged wholesale. Its Tornie gameplay, campaign, graphics, and audio requirements were integrated; stale build, release, dependency, root-data, Jericho, and TornieLite changes were excluded.

## Authorship and adaptation

- Tornie configuration, campaign rewrites, scenario balancing, custom-house design, graphics direction, and audio direction were contributed by Tornie_Panther.
- Mod-specific graphics, audio, and campaign INI files are user contributions or adaptations supplied for Tornie.
- Files inherited unchanged from Dune Legacy or DuneCity retain their existing provenance and licensing status.

## Isolation and compatibility

- Runtime content is rooted under `mods/Tornie/**` and is mounted only when the exact active mod name is `Tornie`.
- Tornie-only houses, objects, factories, units, palettes, interface controls, AI choices, editor entries, and campaign paths are exact-mod gated.
- Switching to Vanilla, DuneCity, Dune2R, or another mod removes Tornie from the active search path.
- Save format `9823` records the expanded house state while retaining version-gated loading for older saves.
- Multiplayer compatibility includes a Tornie engine-capability signature and canonical manifest hash only when Tornie is active.
- Multiplayer mod transfer sends the complete isolated payload, validates paths and size, stages extraction, and verifies the host checksum before joining.

## Third-party material

This integration does not add original Dune II executables, installers, APKs, or base-game PAK archives. It does not claim ownership of trademarks or underlying game material. Redistribution of inherited or derivative assets remains subject to maintainer review and the rights of their respective owners.
