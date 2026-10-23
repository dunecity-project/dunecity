# Dune Legacy Android 0.2.30

Android ARM64 update using the DuneCity 1.0.851 engine and bundled assets.

- Catches Android up with the published desktop/browser engine, including
  upstream campaign, multiplayer, AI and performance updates.
- Classic Desert is the default interface for new profiles and profiles without
  a saved UI Skin choice: sandy buttons and crisp, engraved retro lettering.
- Modern and Lapis Enamel remain selectable in Settings; explicit saved skin
  choices are preserved. Interface skin selection does not change world zoom.
- Includes the existing DuneCity artwork and asset-refresh handling. Optional
  Dune2R remastered artwork remains downloadable in-game, not embedded here.

## Package and upgrade

- Application ID: `net.dunecity.dune2r`
- Version name / code: `0.2.30` / `1000550`
- Engine: `1.0.851`
- Download: `DuneLegacy.apk` (ARM64, sideload)

Install over the previous version rather than uninstalling: saves and settings
should be retained. This sideload package uses the existing Android debug
signing identity, not a new production/Play Store signing identity.

No connected phone was available for an on-device test of this release.
Desktop and browser engine verification does not replace Android play-testing.
