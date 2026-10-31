# Tornie Lite changelog

## DuneCity integration of upstream 1.0.546

- Imported Tornie Lite authored payload from ce89e53, including current solo/co-op campaigns, unit data and presentation assets.
- Added DuneCity family-scoped metadata, declared playable roster, integrity/capability hashes, managed refresh and direct transfer support.
- Retained DuneCity city isolation, fixed faction identities and existing non-family behavior.

## 1.0.544

Ten exact supplied PNGs, native sprite layouts and clean portraits. Total upgraded-building refunds, Area Guard harvesting fix, short Scoutpost labels, one-tile Slowpost editor preview and impact-only napalm in expanded random Palaces. Kleshmersh ordinary Harvestank removed in full mods. Protocol 19 / save gate 9837. See root bilingual release notes.

## 1.0.543 — expanded replacement (preparation)

Non-Vanilla selling by default; 75% Easy Mode refund. New editable vanilla-derived units, movement slowing, saved Chaos worm offer, Elite Ornithopter. Harvestank and the two Harvester variants: level 7 + IX. Gas range 8 / sight 5. Protocol 18 / saves 9836. See the root bilingual release notes for faction availability.

## Game 1.0.534

- Optional Chaos Mode: same-level donor faction per production/defence building and Palace; deterministic campaigns, custom and multiplayer, with saved draws. Vanilla is excluded.

## 1.0.533 engine update
- Sabotage now hunts capturable enemy buildings for Soldiers/Troopers. Infantry-only single/group commands appear below Retreat; editor groups, retargeting and save/load retain the mode.
- The editor unit palette now scrolls vertically with a scrollbar and mouse wheel when needed.
- Editor custom-unit section starts with five Soldiers then five Troopers, using squad tiles and a blue star. Map and reinforcement groups expand to five units.
- New first Barracks order for five Soldiers at technology level 4, second upgrade, without IX.
- New first Worfinery order for five Troopers at technology level 7, after upgrade and with IX.
- Price is three single Troopers of the building's original house; the three-unit order remains.
- Missile Barrage, Blue Harvest and Purple Harvest achievement support.

# Tornie changelog

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
