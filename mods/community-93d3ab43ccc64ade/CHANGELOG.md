# Tornie changelog

## 1.0.524-23 - 2026-08-13

- Integrated the final tagged Tornie fork payload into DuneCity 1.0.530.
- Added Chemical Siege Tank, Chemical Carryall, Flamepost, Chemipost, Love Factory, and Chaos Factory gameplay and presentation assets.
- Added Wildspade, Kleshmersh, and Tharpique guest identities for Tornie custom games without exposing those identities to other mods.
- Preserved Tornie's revised campaigns, colored spice, Mentats, Palace abilities, special-vehicle pools, Worfinery extraction, editor tools, and faction-specific behavior.
- Scoped all Tornie object creation, AI choices, graphics, voices, campaign lookup, editor choices, and guest palettes to exact `Tornie` activation.
- Added a Tornie engine-capability checksum and complete staged lobby payload verification without changing the multiplayer packet layout.
- Rebuilt `Tornie.PAK` from the authoritative loose files and regenerated every SHA-256 entry.

## 1.0.524-2 - 2026-07-26

- Added the 2x3 Love Factory at tech level 9 for every house.
- Added Small, Medium, Heavy, and Support deliveries with configurable base prices and Starport-style price variation.
- Added Frigate delivery animation, house colors, editor placement, save support, AI construction, and AI ordering.
- Added the Love Factory asset to both Tornie editions.

## 1.0.523 - 2026-07-18

- Added generic ninth-house content registration for Tharpique: slot 8, letter T, region prefix THA, cyan palette ramp, and Mercenary fallback.
- Added the Tharpique campaign, region map, herald, voice assets, Chani Mentat configuration, and tested technology overrides.
- Reworked all nine Tornie campaigns and opening scenarios with faction-specific opponents, varied starting units, WOR placement, preserved seeds, corrected start screens, and 1000-credit intro objectives.
- Corrected Harkonnen campaign region progression using the final tested REGIONH data.
- Updated Scoutpost damage, Tharpique IX units, Sonic Trike and Trike availability, Trooper technology, and special-vehicle rules.
- Updated Sonic Trike, Rocket Trike mask, Tornie building coloration, and custom cyan palette graphics.
- Updated Worfinery graphics and occupied-Harvester overlay content.
- Updated Neutral, Rebels, and Tharpique voice assets.
- Added mod-scoped Mentat declarations for Atreides, Neutral, Rebels, and Tharpique.
- Added machine-readable manifest, SHA-256 checksums, provenance notes, and exact-case filenames.
- No DuneCity application version, save format, or multiplayer protocol change is included in this content milestone.
## 1.0.523 presentation integration follow-up

- Register the Tharpique herald and processed house-name voice through the generic mod-scoped custom-house hooks.
- Require the generic custom-house palette routing and editor Team9 correction.

## 1.0.523 intro and sprite-colour follow-up

- Removed all CPU-owned structures from the nine Tornie opening scenarios while preserving enemy units, player Construction Yard and WOR, credits, objectives, seeds, start screens, and unit placement.
- Require the generic HOUSE_CUSTOM sprite-palette correction so only the Harkonnen colour ramp is remapped to the active mod palette on units and buildings.

## 1.0.523 Advanced Windtrap and special-unit follow-up

- Added vanilla-style animated atlases and bounded 10x7 placement previews for all three Advanced Windtraps.
- Preserved per-house team colours while cycling only the intended Windtrap energy index.
- Aligned Tornie Unit_Special and Tech Center pools with the approved nine-house IX plan.
- Added a safe ObjectData fallback only when a house has no explicit special-vehicle pool.

## 1.0.523 review correction

- Prioritize HOUSE_CUSTOM IX vehicles discovered from ObjectData before the generic Sonic Tank and Devastator fallback.
- Confirm Tornie Tharpique resolves to Deviator and Elite Launcher through its mod-owned ObjectData entries.
- Regenerate the Tornie manifest checksums and normalize branch-reported trailing whitespace.
- Add focused regression coverage for Tornie selection and the generic custom-house fallback.
