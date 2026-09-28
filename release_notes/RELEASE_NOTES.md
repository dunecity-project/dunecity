# Dune City 1.0.792

Ground combat now uses consistent splash damage: ordinary gun, shell and rocket hits weaken every quarter tile and stop at one tile. The default combines classic light-unit damage with DuneCity's lower heavy-weapon damage.

- Enable **Original Dune II damage** in game options to use classic ordinary damage against ground units. For example, direct Tank hits rise from 12 to 25 HP and Launcher rockets from 37 to 75 HP.
- Hover over the option for an explanation of damage and splash. The setting is preserved in configuration, saved games and multiplayer match rules.
- Building damage, aircraft hits and special weapons retain their existing rules.

Existing saves remain loadable, with the new option off for older settings. Multiplayer players must all update because the synchronized combat rules and network protocol have changed. Saves, settings and user mods remain in the user folder.

# Dune City 1.0.790

Deviators can now convert Ornithopters and Carryalls, with an 80% conversion chance for eligible units across houses and game modes.

- QuantBot values Deviators by the damage their controlled units deal and absorb, including the normal 20% unit-kill bonus. Repeated conversions alone no longer inflate their score.
- QuantBot promptly arms captured enemy Devastators. Completed detonations earn their remaining-health value and one kill bonus, with blast damage counted separately.
- Deviated units no longer request repairs, book repair yards or enter them through Carryall delivery.
- Map revision labels appear beside names in the custom-game chooser.

Existing saves remain loadable. Loading an older save starts fresh AI unit-learning counters; historical match totals remain. Multiplayer players must all update to this version because synchronized rules and the network protocol have changed. Saves, settings and user mods remain in the user folder.

# Dune City 1.0.783

This release improves QuantBot expansion and base defence, and aligns building degradation with Dune Dynasty across game modes. Power shortages now cause building damage across modes, making power management more important in existing games too.

- Adds **Concrete is required**: enabled by default in Vanilla and disabled by default in DuneCity. When disabled, missing foundations cause neither placement damage nor ongoing degradation. Roads also provide foundations.
- Damaged windtraps produce less power, and damaged city buildings reduce land value. Palaces retain their land-value bonus but no longer replace the stadium requirement.
- QuantBot prepares concrete with upgraded construction yards, repairs productive buildings, and maintains a power reserve once its Vanilla economy and production are established. A partial foundation no longer causes a completed building to be cancelled.
- City AIs deploy MCVs on their current rock and colonise safe free rock when space runs out. They can import colonists through Starports and relax road-layout preferences to fit essential factories.
- Opening combat units clear construction space. Launchers move towards buildings under air attack, including remote colonies; ornithopter targeting and damaged-aircraft combat are corrected.
- Production growth is better balanced against defensive rocket-turret spending.

Multiplayer protocol is now 37: all players must update together. New saves use format 9848; older saves remain loadable, but saves made by this version cannot be opened by older builds. Saves, settings and user mods remain in the user folder.

# Dune City 1.0.774

QuantBot now builds a missing Starport when it reaches House IX or Palace in its construction priorities, even on maps with no Starport imports. Once the port is completed, the AI continues to the intended building.

- Keeps the earlier import and opening priorities unchanged.
- Respects map/mod prerequisites, tech and upgrade restrictions, affordability and placement; queued Starports prevent duplicate orders from multiple construction yards.
- Applies across Easy, Medium, Hard and Brutal. Real-game regression probes cover Vanilla and DuneCity.
- Includes all twelve new Dune City scenarios and repaired SimCity from 1.0.773.

Multiplayer protocol is now 28. All players in a match must use this version. Save format is unchanged.

# Dune City 1.0.773

Twelve new Dune City single-player scenarios are bundled with the desktop installers and available through Metaserver Maps. The repaired SimCity map is included too. Select DuneCity in Custom Game to play the city scenarios.

- New missions: Sihaya Basin, Ash Quarter, Hagal Flats, Arrakeen Blackout, Harg Pass Convoy, Cielago Watch, Tuono Crossing, Shield Wall Rift, Carthag Vise, Coriolis Gap, Habbanya Crescent and Habbanya Narrows.
- Rocket turrets now match Dune Dynasty's measured firing cadence after accounting for engine timing: 2.72 seconds between missiles and 2.048 seconds for the close-range cannon. Healthy launchers fire pairs 0.288 seconds apart, repeating every 12.288 seconds; launchers at half health or below fire singles every 12 seconds. Interrupted pairs cannot leave a late extra rocket queued. Applies across Vanilla, DuneCity, Dune2R and Tornie.
- AI builds its production core and needed storage more reliably, balances expansion with air defence, sends aircraft to rescue attacked structures and harvesters, and avoids unreachable Hunt targets. Dune2R stops buying workers for exhausted spice fields.
- Game setup remembers offline/online choices and the selected mod through map selection. Duplicate local/server maps are consolidated, and the repair sidebar layout is improved.

Multiplayer protocol is now 27; all players in a match need compatible updated clients. Save format remains 9846. Desktop updates preserve saves, settings and user mods.

---

# DuneCity Tornie Edition Release Notes

## Version 1.0.520

- `Unit_Special` scenario entries now choose randomly from every enabled vehicle in the selected House pool when more than one result is available.
- Single-entry and single-enabled-entry pools remain deterministic and do not consume the synchronized game RNG.
- The scenario loader now uses the same synchronized game random generator as runtime special-unit creation and Tech Center spawning.

## Version 1.0.519

- Added the Rebels Sonic Trike and finalized Tornie-only unit progression.
- Updated Flame Tank fire behavior, damage over time, and House IX prerequisite.
- Finalized Tornie and non-Tornie campaign routing for all eight Houses.
- Added and corrected custom unit sheets, icons, Mentats, structures, and palettes.
- Added six optional player colors with save-game persistence and unique-color validation.
- Improved eight-player lobby setup, Neutral/Rebels AI support, and campaign stability.
- Fixed infantry crushing crashes and several mod-switching resource conflicts.
- Packaged custom assets in Tornie.PAK and shared non-Tornie additions in Extra.PAK.

---

## Dune Legacy 0.99.5

**Release Date:** December 21, 2025  
**Download:** [Windows Installer](https://dunelegacy.sourceforge.net/downloads/)

---

## 🚀 Version 0.99.5 Updates

### 🎮 Internet Multiplayer - No Port Forwarding Required!

**Play multiplayer games over the internet without any router configuration!**

The game now automatically handles NAT traversal using hole punching technology. Just host or join an internet game - no need to configure port forwarding, DMZ, or UPnP on your router.

**Tips for best experience:**
- If you experience lag or stuttering, try **reducing the game speed** in Options
- The default game speed (16) works well for most connections
- For higher latency connections, game speed 20-24 provides smoother gameplay
- Players on the same local network are automatically detected and use direct LAN connection

**Technical Details:**
- Automatic NAT hole punching for internet games
- Uses STUN protocol to discover external IP:port
- Coordinated UDP hole punching via metaserver
- Falls back to direct connect if hole punch fails
- Works with most symmetric NAT routers
- Custom STUN client implementation (no external library)
- Non-blocking punch state machine (no UI freezing)

### Multiplayer Stability

**Connection Improvements:**
- Added NAT keep-alive packets to prevent router timeout
- Fixed host disconnect detection (game now ends properly)
- Smart NAT: Detects when players are on same network and uses local IP (avoids hairpin NAT lag)
- Smoother gameplay during network stalls (capped catch-up to 3 cycles/frame)

---

## 🚀 Version 0.99.4 Updates

### Discord Integration

**Discord Game Notifications:**
- Game Starting notifications posted to Discord channel when multiplayer game begins
- Shows map name, mod, and all player/house assignments
- Notifications sent via metaserver (no client-side webhook configuration needed)

**Enhanced Rich Presence:**
- Shows lobby status when hosting/joining multiplayer games
- Displays in-game activity with house, map, and player count
- Updates dynamically as players join/leave lobby
- Proper party size display (current players / max slots)
- DiscordManager::update() now called regularly for reliable presence updates

### Multiplayer Mod Sync Fixes

**Vanilla Mod Sync:**
- Fixed issue where clients couldn't sync when host uses vanilla mod
- Clients now switch to local vanilla first before attempting download
- Vanilla mod checksum now based on file contents, not runtime settings
- Clear error message if vanilla versions don't match

**General Improvements:**
- Mod sync tries local mod switch before downloading
- Better error handling for mod mismatch scenarios

---

## 🚀 Version 0.99.3 Updates

### Steam Integration
- **Discord-style Rich Presence:** Shows your game status to friends
- Displays current game mode, map, and player count
- Integrates with Steam's social features

### Multiplayer Improvements

**Custom Player Names:**
- Set your own multiplayer name in the Multiplayer menu
- Name is saved between sessions to your config file
- Validates that name is not empty before joining/hosting games
- Maximum 20 characters

**UPnP Port Forwarding:**
- Automatic router configuration when hosting Internet games
- Uses UPnP to open ports without manual router setup
- Makes hosting games much easier for players behind NAT
- Discovery cached per session to avoid delays on subsequent hosts

### Bug Fixes
- Fixed crash on Mac when clicking Multiplayer without network permissions
- Fixed metaserver delay caused by HTTP→HTTPS redirect
- Added null checks throughout multiplayer menu for graceful network failure handling

---

## 🚀 Version 0.99.2 Updates

### Harvester & Carryall Improvements

**Smarter Path Blocking Recovery:**
- Harvesters now request carryall pickup when path to spice is blocked (instead of giving up)
- Returning harvesters wait for blockers to move instead of switching refineries
- Harvesters with a booked carrier wait patiently instead of canceling
- Freshly deployed units can now request carryall help immediately
- Reduced carryall request cooldown from 5 seconds to 2 seconds for faster recovery

**Refinery Patience Logic:**
- Returning harvesters no longer give up when path is blocked 3 times
- Added patience logic - harvesters keep trying instead of resetting destination
- Prevents constant refinery-switching when temporarily blocked by other units

### Configurable Unit Limits

**Map-Size Based Unit Limits:**
- New `ObjectData.ini` settings for per-house unit limits based on map size:
  - `UnitLimitSmallMap = 25` (maps 32x32 or smaller)
  - `UnitLimitMediumMap = 100` (maps up to 64x64)
  - `UnitLimitLargeMap = 250` (maps up to 128x128)
  - `UnitLimitHugeMap = 300` (maps 128x128 or larger)
- Replaces hardcoded formula with configurable INI values
- Only used when "Override max. number of units" is unchecked in game options

### QuantBot Refactoring
- Removed obsolete `heavyFactoryRushActive` flag
- Code cleanup and logic improvements

### Website Updates
- Added Linux download links for version 0.99.1

---

## 🎮 NEW: Complete Mod System

### Mod Editor & Management

**Mod Editor:**
- Create new mods with a custom name
- Edit mod display name and version number
- Shows file paths for manual editing of:
  - `ObjectData.ini` - Unit/structure stats
  - `QuantBot Config.ini` - AI behavior settings  
  - `GameOptions.ini` - Game rules
- Automatic file seeding from vanilla templates

**Mod Management Menu:**
- Access via Main Menu → **MODS**
- List all available mods with details
- Activate any mod with one click
- Create new mods (copies from vanilla)
- Edit existing mods
- Delete unwanted mods
- Shows mod version, author, and checksums

### Mod Files & Structure

**Moddable Configuration Files:**
- `ObjectData.ini` - Unit/structure stats (hitpoints, damage, speed, cost, etc.)
- `GameOptions.ini` - Game rules (fog of war, concrete required, etc.)
- `QuantBot Config.ini` - AI behavior settings

**Mod Folder Structure:**
```
mods/
├── vanilla/           (base game - auto-seeded from defaults)
├── my-custom-mod/
│   ├── mod.ini        (metadata: name, version)
│   ├── ObjectData.ini
│   ├── GameOptions.ini
│   └── QuantBot Config.ini
└── active_mod.txt     (tracks current mod)
```

### Multiplayer Mod Synchronization

**Automatic Mod Sync:**
- Host's mod is automatically sent to all clients
- Clients download and activate host's mod before game starts
- Checksum verification ensures all players have identical settings
- No manual mod sharing required!

**How It Works:**
1. Host activates a mod and starts a game
2. Mod info (name, version, checksum) sent to joining clients
3. If checksums mismatch, client automatically downloads host's mod
4. Client activates received mod and sends acknowledgment
5. Game starts only when all clients are synchronized

**Strict Mode:**
- All players must use identical mod configuration
- Prevents desyncs from mismatched settings
- Host is authoritative - clients sync to host's mod

### Metaserver Integration

**Mod Info in Server Browser:**
- Active servers now display mod name and version
- Players can see what mod each game is running before joining
- Recent games show mod information in statistics

**Popular Mods Tracking:**
- New "Popular Mods" leaderboard on metaserver status page
- Tracks most-played mods over time
- Backward compatible - old games show as "vanilla"

### Save/Replay Compatibility

**Mod-Aware Saves:**
- Savegames now store active mod name and checksum
- Warning shown when loading save with different mod
- Replays tagged with mod information

---

## ⚔️ Major Combat & Unit Behavior Fixes

### Unit Engagement & Counter-Attack (Restores 0.96.4 Behavior)

**Instant Retaliation When Damaged:**
- Units now immediately acquire targets when attacked (findTargetTimer reset to 0)
- Eliminates up to 2-second delay in counter-attacking
- Target scanning interval reduced from 2 seconds to 1 second for better responsiveness

**Pursuit Behavior Fixed:**
- Removed flawed "close enough" navigation logic that prevented units from pursuing enemies within 3 tiles
- Units now move to exact destination (weapon range for combat units)
- Fixes saboteurs, infantry, and all combat units being unable to close distance properly

**AMBUSH Mode Improvements:**
- Infantry in AMBUSH mode now switch to HUNT when damaged (matches Dynasty behavior)
- Allows units to pursue attackers instead of standing idle after being shot
- Restores original Dune 2/Dynasty game mechanics

**Optimized Performance:**
- Removed excessive debug logging that was causing performance issues
- Maintains instant response on damage with good frame rates

### Saboteur Fixes (AI & Manual Control)

**Root Cause Identified:**
- Saboteurs have WeaponRange=0 in ObjectData.ini (by design)
- Modern A* pathfinding changes since 0.96.4 caused saboteurs to stop 2-3 tiles short of targets
- Manual right-click worked because it uses forced=true targeting

**The Complete Fix:**
- Saboteurs now use `forced=true` when auto-acquiring targets in HUNT mode
- Makes pathfinding persist to occupied tiles (structures/vehicles)
- Matches behavior of manual right-click targeting
- Detonates when stopped within 1.5 tiles of target
- Palace-spawned AI saboteurs work correctly
- Screen shake and explosion effects on detonation

### Sandworm Improvements

**Attack Behavior:**
- Sandworms now counter-attack when damaged from range
- Switch to HUNT mode before processing damage
- Directly attack damager's position if on sand
- No longer sit idle when shot at from long range

---

## 🤖 AI Improvements

### Campaign Difficulty Balance

**Rebalanced for Better Progression:**
- **Easy:** 30% attack force (up from 25%), 2.0x military, 1 harvester/refinery
- **Medium:** 40% attack force, 2.5x military (up from 2.0x), 2 harvesters/refinery  
- **Hard:** 60% attack force (up from 50%), 3.0x military (up from 2.5x), 2.5 harvesters/refinery, 2 refineries minimum
- **Brutal:** 80% attack force (up from 50%), 3.5x military, map-size-based harvester limits, 4 refineries minimum

**Spice Management:**
- All difficulties now respect 2000 spice per harvester rule
- Brutal difficulty uses Custom game map-size limits
- Prevents over-harvesting on small maps

### Campaign AI Production Fixes

**Factory Priority System:**
- Light Factory and WOR only produce when no Heavy Factory exists
- Prevents AI from wasting money on light units when heavy vehicle production is available
- Fixes Sardaukar producing trikes instead of tanks
- Comprehensive production logging for all builder structures

### Squad Rally Point System (Hard/Brutal)

**Tactical Coordination:**
- AI now establishes rally points for squad formations
- Units group intelligently at game start for better positioning
- Improved retreat mechanics based on weapon range
- Prevents command spam during tactical maneuvers

### Enhanced Launcher & Deviator Kiting

**Kiting Now Available on All Difficulties:**
- Launchers and Deviators now kite away from threats on **all difficulty levels** (Easy, Medium, Hard, Brutal)
- Previously only worked on Hard and Brutal difficulties
- Provides better tactical gameplay and AI survivability across all skill levels

**Improved Kiting Distance Calculation:**
- Kiting distance is now **proportional to threat proximity**
- Closer threats trigger longer retreats (3-8 tiles)
  - 1 tile away → retreat 8 tiles
  - 5 tiles away → retreat 4 tiles
  - Minimum 3-tile retreat for meaningful movement
  - Maximum 8-tile retreat to prevent over-kiting
- Previously used fixed 3-tile retreat regardless of distance
- Results in more intelligent positioning that maintains weapon range advantage

**Support Mode Awareness:**
- Launchers and Deviators no longer kite when in support mode
- Ensures long-range units stay with the main force when explicitly supporting
- Prevents unwanted retreats during coordinated attacks

---

## 🎯 Bug Fixes

### Harvester Pathfinding & Stuck Recovery

**Pathfinding Failure Detection:**
- Added retry counter system to detect unreachable spice destinations
- Harvesters now give up after 3 failed pathfinding attempts
- Prevents infinite stuck loops trying to reach impossible destinations
- Automatically searches for new spice fields when stuck

**Stuck Harvester Recovery:**
- Comprehensive logging for harvesters stuck 30+ seconds
- Automatic detection of unreachable destinations
- Improved spice field navigation logic
- Better handling of blocked or inaccessible spice tiles

**Result:** Harvesters no longer get permanently stuck and will self-recover when encountering pathfinding issues.

### Deviated Unit Positioning

**Fixed Deviated Units Not Moving to Squad:**
- Deviated units now use tighter 2-tile radius when repositioning
- Previously used large radius (squadRadius - 1), allowing units to stay at current position
- Ensures deviated units actually move to squad center for protection and coordination
- More aggressive positioning for units fighting for your side

### Deathhand Missile Accuracy

**Dynasty Scatter Algorithm:**
- Implemented Dynasty scatter algorithm for more accurate targeting
- Uses halving algorithm biased towards smaller scatter values
- 160 Dynasty units = 10 tiles = 320 pixels conversion
- More predictable and balanced missile strikes

### Palace & Support AI

**Special Weapon Restrictions:**
- Support AI now prevented from launching palace specials
- Prevents wasteful use of Fremen, Sardaukar, or Deathhand by support players
- Only main AI/human players control palace weapons

---

## ⚙️ Performance & Technical Improvements

### VSync & Frame Rate

**Proper VSync Implementation:**
- Implemented VSync using SDL_RenderSetVSync()
- Default VSync enabled in config
- Multi-tier FPS thresholds for pathfinding budget (58/55/50 fps)
- Smoother gameplay with better frame pacing

### Carryall Fixes

**Damage System Improvements:**
- Removed rocket tracking behavior for Carryalls
- Re-implemented distance-based damage falloff
- Better pickup/dropoff logic integration
- Enhanced movement and combat behavior

---

## 🎮 Menu System Enhancements

**Improved Game Setup:**
- Added support bot difficulty selection in House Choice Menu
- Added enemy AI difficulty selection options
- Persistent game options across menu sessions
- Expanded menu layout for better option visibility

**New Mod Menu:**
- Access all mod functions from Main Menu → MODS
- Visual mod browser with details panel
- One-click mod activation
- Integrated mod editor

---

## 🎮 Cheat Codes & God Mode

### Immortal Human Player (God Mode)

**New Cheat Feature:**
- Human-controlled units and structures become invulnerable to all damage
- Available in single-player modes only (Campaign, Custom Game, Skirmish)
- Does not work in multiplayer to prevent unfair gameplay

**How to Enable:**
1. **In-Game Toggle:** Type `muaddib` during gameplay to toggle on/off
   - Shows message: "God mode: Immortality enabled" or "God mode: Immortality disabled"
   - Can be toggled anytime during single-player games
   - No cheat mode required for this feature

2. **Game Options Menu:** Enable checkbox "Immortal Human Player"
   - Found in Game Options → Immortal Human Player
   - Checkbox available when starting new games

**What It Does:**
- Your units take no damage from any source (enemy fire, sandworms, etc.)
- Your structures are invulnerable to all attacks
- Special interactions:
  - Infantry survive capture/sabotage attempts
  - Sandworms die when trying to eat immortal units (choke on them)
  - Units in destroyed Carryalls drop safely instead of dying
  - Ground units survive being crushed

**Why "muaddib"?** 
Named after Paul Atreides' Fremen name from the Dune universe - a fitting god-mode reference!

---

## 🗺️ New Multiplayer Maps

**Four New Maps Added:**
- **4P Moshpit with Garbages** (128x128) - Intense 4-player combat arena
- **6P Alkozeltser 2** (128x128) - Two variants for 6-player matches
- **6P Alkozeltser 4** (256x256) - Large 6-player map
- **6P Full Wormage** (128x128) - Large-scale worm-infested battlefield

---

## 🌐 Multiplayer Improvements

### Automatic Map Sharing & Local Storage

**Seamless Map Distribution:**
- Custom maps are now automatically saved to clients when joining multiplayer games
- Received maps are stored in the user's local maps directory for future use
- Maps saved to: `~/Library/Application Support/Dune Legacy/maps/multiplayer/` (macOS), `%APPDATA%\dunelegacy\maps\multiplayer\` (Windows), `~/.config/dunelegacy/maps/multiplayer/` (Linux)
- Preserves existing local maps (won't overwrite user-modified versions)
- Eliminates need to manually share map files between players

**How It Works:**
1. Host selects a custom map for multiplayer game
2. Map data is automatically transmitted to all connecting clients
3. Clients can play immediately (no manual file copying needed)
4. Map is saved locally for future games without the original host

### Automatic Mod Synchronization

**New in 0.99:**
- Host's mod automatically synced to all clients
- Mod files transferred via network (ObjectData, GameOptions, QuantBot Config, mod.ini)
- Size-limited chunked transfer with validation
- Checksum verification before game start
- Server browser shows mod name and version
- Mod selection available in Custom Game and Internet Game screens

---

## 🛠️ Configuration System

**File Naming Standardization:**
- Renamed `.bak` files to `.ini.default` for clarity
- `ObjectData.ini.default` - Unit/structure stats template
- `QuantBot Config.ini.default` - AI behavior template
- Updated all file references and installation scripts

**Mod-Aware Configuration:**
- Game options now loaded from active mod
- User preferences (video, audio, network) remain separate
- Mod checksums use canonical hashing (whitespace-insensitive)
- Automatic vanilla mod seeding on first run or when files are missing

**macOS App Bundle Fix:**
- Fixed data directory path resolution for macOS app bundles
- Template files now correctly loaded from `Resources/config/`
- Ensures proper mod seeding and file discovery

---

## 📋 Installation

### Update from Previous Version

If you have 0.98.x or earlier installed:
1. Download the new installer: `Dune Legacy 0.99 Setup.exe`
2. Run installer (will update existing installation)
3. Launch and enjoy the new mod system!

### Fresh Installation

1. Download installer: `Dune Legacy 0.99 Setup.exe`
2. Run installer and follow prompts
3. Copy original Dune 2 PAK files to `data/` folder
4. Launch game

**Configuration files** are located in:
- Windows: `<game_install>\config\`
- Linux: `<game_install>/config/`
- macOS: `Dune Legacy.app/Contents/Resources/config/`

**Mod files** are located in:
- Windows: `%APPDATA%\dunelegacy\mods\`
- Linux: `~/.config/dunelegacy/mods/`
- macOS: `~/Library/Application Support/Dune Legacy/mods/`

---

## 🎮 Gameplay Impact

**For Players:**
- Create and share custom mods easily
- Units respond instantly to threats (no more 2-second delay)
- Saboteurs actually work when spawned by AI palaces
- Infantry and combat units pursue enemies properly
- AI opponents use long-range units more effectively at all difficulties
- Deviated units are more valuable (actually join your squad)
- Harvesters no longer get stuck indefinitely
- **NEW:** God mode available via "muaddib" cheat code for casual/practice play

**For Multiplayer:**
- Mods automatically synchronized between host and clients
- Custom maps automatically shared when joining games
- No more manual file copying between players
- Server browser shows what mod each game is running

**For Modders:**
- Create mods with custom names directly in-game
- Edit INI files manually for full control over:
  - Unit/structure stats (ObjectData.ini)
  - AI behavior (QuantBot Config.ini)
  - Game rules (GameOptions.ini)
- Version tracking and metadata in mod.ini
- Automatic checksum generation for multiplayer sync

**For Campaign Players:**
- Difficulty progression feels more natural and balanced
- AI production is more intelligent (no more trike spam with tank factories)
- Hard and Brutal modes provide appropriate challenge

---

## 🐛 Major Bug Fixes Summary

1. ✅ Unit counter-attack delay eliminated (instant response)
2. ✅ Unit pursuit behavior fixed (no more stopping 3 tiles short)
3. ✅ Saboteur pathfinding and detonation working correctly
4. ✅ AMBUSH mode units now pursue attackers
5. ✅ Sandworms retaliate when shot at
6. ✅ Harvester stuck recovery system implemented
7. ✅ Deviated units actually move to squad
8. ✅ Campaign AI production priorities fixed
9. ✅ Carryall damage system corrected
10. ✅ VSync properly implemented

---

## 🔗 Links

- **Website:** https://dunelegacy.com/
- **Metaserver:** https://dunelegacy.com/metaserver/
- **Bug Reports:** https://github.com/dunelegacy/dunelegacy/issues
- **Source Code:** https://github.com/dunelegacy/dunelegacy

---

## ⚠️ Known Issues

None reported. Please report bugs at the link above.

---

**Enjoy the new mod system and all the gameplay improvements!**
