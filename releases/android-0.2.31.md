# Dune Legacy Android 0.2.31

- Updates the Android engine to **1.0.852**, matching the desktop and browser
  release for multiplayer. All players need matching engine versions.
- Includes corrected silent-audio recovery if a sound backend cannot initialize.
- Retains Classic Desert, Lapis Enamel and Modern interfaces, current DuneCity
  artwork, asset-refresh handling and existing game rules.
- Optional Dune2R artwork remains downloadable in-game rather than embedded.

Application ID: `net.dunecity.dune2r`. Version/code: `0.2.31` / `1000551`.
Package: `DuneLegacy.apk` (ARM64 sideload).

Install over the previous version rather than uninstalling, to retain saves
and preferences. Public upgrades use the existing sideload signing identity,
not the temporary hosted-runner signing key. This is not a Play Store package.

No physical phone was available for an on-device test of this release.
