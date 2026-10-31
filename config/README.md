# Dune Legacy Configuration Files

## Overview

This directory contains **READ-ONLY TEMPLATE FILES** for Dune Legacy configuration and the bundled achievement catalog. These files are part of the game installation and should not be modified directly.

## User Configuration Location

The game automatically copies the configuration templates to your user configuration directory on first run. To customize game behavior, you should edit the files in your **user directory**, not here. The achievement catalog stays installed; its separate local progress profile is described below.

### User Configuration Directories by Platform

- **Windows**: `C:\Users\<username>\AppData\Roaming\dunelegacy\config\`
- **Linux**: `~/.config/dunelegacy/config/`
- **macOS**: `~/Library/Application Support/Dune Legacy/config/`

## Configuration Files

### ObjectData.ini

Customizes unit and structure properties including:
- Hit points, price, power consumption
- Weapon damage, range, and reload time
- Movement speed, build time, tech requirements
- House-specific overrides

**Important**: For multiplayer games, all players must have identical `ObjectData.ini` values to prevent desynchronization.

### QuantBot Config.ini

Configures AI behavior including:
- Difficulty settings (Defend, Easy, Medium, Hard, Brutal)
- Attack behavior and timing
- Resource gathering limits
- Unit composition ratios per house
- Campaign vs. Custom game settings

### Dune Legacy.ini

General game configuration including:
- Video settings (resolution, fullscreen, scaling)
- Audio settings (music type, volume)
- Game options (concrete required, fog of war, etc.)
- Player name and language

### Achievements.ini

The installed catalog contains Tornie_Panther's 73 offline achievements, including
English and French names, descriptions and rule thresholds. A built-in copy keeps
the catalog available if the installed file is missing.

With Tornie, TornieLite, Jericho, JerichoLite, ArcaKev or ArcaKevLite active
(including derived content copies), open **Extras → Achievements** for awards and
lifetime statistics. Progress is stored locally in lowercase `achievements.ini`
beside your user `Dune City.ini`; it is not uploaded or shared with other players.
Browser builds retain it in the browser's local persistent filesystem.

Only eligible, non-observer games collect progress. Replays, cheat mode and
single-player immortality do not earn achievements. Save checkpoints are tracked
in the local profile without adding achievement state to saves or network packets;
an older save with no matching profile checkpoint cannot earn unknown-history
pacifism or no-casualty awards. Achievement observations never change gameplay or
consume the game RNG. Other content families do not collect or show achievements.

## How It Works

1. **First Run**: When you launch the game for the first time (or when a config file is missing), Dune Legacy automatically copies the template from this directory to your user directory.

2. **Subsequent Runs**: The game loads user configuration from your user directory. The achievement catalog is read from the installation, with a built-in fallback.

3. **Resetting Configuration**: To reset a configuration file to defaults, simply delete it from your user directory. The game will recreate it from the template on next launch.

## Modifying Configuration

To customize your game:

1. Navigate to your user configuration directory (see paths above)
2. Edit the desired `.ini` file with any text editor
3. Save your changes
4. Restart Dune Legacy to apply

## Multiplayer Compatibility

**Critical**: When playing multiplayer games, ensure all players have:
- **Identical `ObjectData.ini` settings** - Differences will cause desynchronization
- **Identical `QuantBot Config.ini` settings** if playing against AI - Ensures fair and consistent AI behavior

You can verify configuration consistency by comparing config hashes in the game logs.

## Troubleshooting

### "My changes aren't taking effect"

You may be editing the template files in the installation directory instead of the active files in your user directory. Always edit files in your user configuration directory (see paths above).

### "I want to reset to defaults"

Delete the config file(s) from your user directory. The game will automatically recreate them from the templates on next launch.

### "Where are my user config files?"

Run Dune Legacy and check the log file (also in your user directory). The game logs the exact paths it's using for configuration files.

## Version Information

These templates are updated with each Dune Legacy release. Your user configuration files are preserved across updates, but you may want to review the templates for new options or changes.

## More Information

For detailed documentation about configuration options and modding, visit:
- https://dunelegacy.sourceforge.net/website/development/modding.html
- https://dunelegacy.sourceforge.net/

## File Details

- `Dune Legacy.ini` - **Template file** - General game settings
- `ObjectData.ini` - **Template file** - Unit/structure properties
- `QuantBot Config.ini` - **Template file** - AI behavior configuration
- `Achievements.ini` - **Bundled catalog** - Offline awards; lowercase `achievements.ini` in the user directory holds progress

**Remember**: Edit user copies of configuration templates, not the installed files. The achievement progress profile is local data, not a template.

