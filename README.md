# DuneCity

A Dune II RTS engine fork with the **Tornie Mod** integrated — a 16-phase Micropolis-style city simulation, 8th house (HOUSE_REBELS) faction, custom campaign bundle, custom units, and a per-house color tint system.

**Status:** Active development. Latest release **v1.0.426** on `main` / `tornie-beta`. Tornie Mod loads via `mods/Tornie/data/` and ships its own PAK (`Tornie.PAK`, 260+ entries).

---

## Quick Start

```bash
# Clone
git clone https://github.com/moonbear-dev/dunecity.git
cd dunecity

# Configure + build (Linux)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run
./build/bin/dunecity
```

The Tornie Mod is **enabled by default** at `mods/Tornie/`. To switch mods (vanilla / dunecity / Tornie), use the in-game **Mods** menu.

### Tornie-family campaigns and co-op

Tornie, Jericho and ArcaKev offer twelve playable factions; their Lite editions use the six-house roster declared by the mod. Derived Workshop copies retain their base edition's roster. Full-family custom games support twelve house slots (humans plus AI); Internet rooms retain the existing eight-human-peer limit. Natives remain a non-player faction.

In the campaign house screen, **Online co-op** keeps DuneCity's existing shared-house campaign. **Online faction co-op** is an additional Tornie-family mode with two or four allied bases. Archon mode adds a second controller to each of four bases, for up to eight players over direct peer-to-peer connections. Allies can choose the same faction or a campaign's authored support-enemy faction without replacing that enemy.

Faction co-op uses the mod's `campaign/coop` mission variants, shared harvest objectives and allied reinforcements. Its setup offers +5 and +10 units for each active enemy, optional allied control/building, Chaos Mode, and Easy Mode (starts at mission two and benefits every active ally). Progress and resumable checkpoints are stored under the profile's `coop` directory, separately from solo campaign progression.

**Extras → Achievements** shows the 73 offline awards. The local profile and run statistics are observational only: achievements do not change simulation state, randomness, multiplayer commands or game rules. The new faction, campaign and achievement behavior is restricted to Tornie-family content; other mods keep their existing selection and lobby limits.

For native release QA, `tests/ai/tornie-gameplay-matrix.py --results <directory>` schedules every edition's data-defined houses at missions 001, 011, 020, 021 and 022, plus 25-minute Hard/Brutal custom games. Submit this batch through `dunecity-queue` on managed hosts. Its CSV evidence includes mission/region identities, actual production and combat damage, coloured-spice coverage and deterministic checkpoints. Tornie/Tornie Lite use the revised 546 opponent counts; Jericho/ArcaKev retain their authored 545 campaigns. Quota introductions may finish before ten minutes, and enemies without an authored factory are not expected to manufacture units. `--categories isolation` runs fixed seeds in Vanilla, Dune City and Dune2R; `--baseline-root <main-checkout> --build-dir <main-build>` compares an existing read-only main build without modifying that checkout.

The `tornie_object_state_roundtrip` CTest also destroys a fully occupied Doublefinery after save/load, checking that both bay occupants are removed and all ten tiles of its five-by-two footprint receive the correct rubble without blocking the map.

In Tornie-family content, an explicit human attack order for either Ornithopter hull keeps its selected target, including an enemy harvester, until that target is released or destroyed; the aircraft then resumes Hunt. `tornie_gameplay_state` covers this command behavior together with manufacturing, combat, sales, research and deterministic continuation of live family state.

The matrix retains actual production counts and natural victory/defeat times rather than treating a ten-minute cap as ten minutes played. Factory-use assertions require a powered, funded, legal unit-production option; original campaigns that require a manual opening are explicitly annotated, not converted into new campaign recipes. These functional checks are not a campaign-balance certification.

Two-process direct-play QA uses `tests/network/run-late-join-probe.py --build-dir <native-build> --output-dir <fresh-results> --tornie-case custom` (or `--tornie-case campaign`). Both native players must agree on settings, executed human commands and every 300-cycle digest through cycle 3600; the campaign uses distinct Atreides and Mercenary bases with the authored enemy roles retained. These fixtures use direct WebRTC with no TURN or gameplay relay. A settled match's signaling-service loss timeout includes its intentional idle polling interval plus the normal failure grace, so a healthy poll is not reported as an outage.

---

## What's in the box

| Component | Status | Notes |
|-----------|--------|-------|
| Dune Legacy engine (combat, units, structures) | ✅ Stable | Upstream v0.96 base + 576+ Tornie commits |
| 16-phase Micropolis city simulation | ✅ Integrated | Runs every tick alongside combat |
| 8th house **HOUSE_REBELS** | ✅ Stable | Dark grey tint from `Custom_IBM.PAL` |
| Tornie-family content | Integrated | Twelve-house full editions, six-house Lite editions, solo and separate-faction co-op campaigns |
| Per-house color tint (units/structures) | ✅ v1.0.426 | Vanilla v1.0.305 path + Custom_IBM.PAL for REBELS |
| Custom units (FlameTank, EliteSiegeTank, RocketTrike, etc.) | ✅ Loaded | 8-direction palette-indexed strips |
| Discord Rich Presence | ✅ Optional | Auto-shows current mod + version |
| Multiplayer (network + savegame) | ✅ | v1.0.367 multiplayer CustomGamePlayers |
| Save/load (v9820+ format) | ✅ | Backward-compat with v9818 saves |
| Map editor + advanced windtrap | ✅ | Per-house brush icons + pen size preview |
| Mentor portraits (Paul Atreides, Baron, etc.) | ✅ | Custom_IBM.PAL tint for REBELS + Neutral |
| Performance log | ✅ | FPS budget auto-tuning (15000→16000 tokens/cycle) |

---

## Development Workflow

**Trunk-based development** on `main`:
- `main` = trunk, always deployable
- `tornie-beta` = pre-release branch for Tornie Mod builds (parallel CI pipeline)
- Single source of truth (rebase, no long-lived branches)

```bash
# Make a change + test
git checkout main
# ... edit, cmake --build build, etc ...

# Commit with a descriptive message
git add -A
git commit -m "fix(color): Custom_IBM.PAL dark grey for HOUSE_REBELS"

# Bump version + tag
sed -i 's/DuneCity VERSION 1.0.426/DuneCity VERSION 1.0.427/' CMakeLists.txt
sed -i 's/"version": "1.0.426"/"version": "1.0.427"/' vcpkg.json
sed -i 's/#define VERSION "1.0.426"/#define VERSION "1.0.427"/' include/config.h
bash scripts/bump-version.sh --check   # verify all 3 files agree

# Push
git push origin main --force
git tag -a v1.0.427 -m "v1.0.427 - ..."
git push origin v1.0.427 --force
git push origin tornie-beta --force    # mirror to Tornie beta branch
```

The CI workflow (`.github/workflows/build.yml`) builds Windows ZIPs on every push to `main` or `tornie-beta` and uploads to https://dunelegacy.com/.

---

## Architecture

DuneCity is a fork of [Dune Legacy](https://github.com/svan058/dunelegacy) (C++17/SDL2) with the [Micropolis](https://github.com/SimHacker/MicropolisCore) city-building simulation ported into the same game loop. The Tornie Mod is layered on top with custom campaigns, units, palettes, and a 8th faction (HOUSE_REBELS).

### Game Loop

```
Game::updateGameState()  [runs every tick at ~62.5 Hz]
  ├── cmdManager.executeCommands()
  ├── house[i]->update()              // AI city-building
  ├── processObjects()                // WindTrap registers power sources
  └── citySimulation_->advancePhase() // one of 16 city phases per tick
```

### City Simulation Phases (16 per cycle)

| Phase | What Happens |
|-------|-------------|
| 0 | Increment city time, clear census, reset power flags |
| 1-8 | Map scan — process 1/8 of zones per phase |
| 9 | Census, tax collection, growth valve calculation |
| 10 | Decay rate-of-growth, decay traffic density |
| 11 | Power flood-fill from WindTrap seeds |
| 12 | Pollution, terrain, land value analysis |
| 13 | Crime scan |
| 14 | Population density scan |
| 15 | Fire analysis, Arrakis disasters |

### Tornie Mod Architecture

```
dunecity/
├── src/                           # Dune Legacy + Tornie mod engine code
│   ├── FileClasses/
│   │   └── GFXManager.cpp         # Per-house remap, Custom_IBM.PAL, palette write
│   ├── Menu/
│   │   ├── CustomGamePlayers.cpp  # Color swap dropdown (Original/Teal/Custom 7)
│   │   └── SinglePlayerSkirmishMenu.cpp  # 8-house carousel
│   ├── players/
│   │   └── SpectatorPlayer.cpp    # 9th house type
│   ├── structures/
│   │   ├── ConstructionYard.cpp   # 8th house build logic
│   │   └── AdvancedWindTrap.cpp   # Custom super power plant
│   └── Game.cpp                   # Per-house color swap init, 8th house loading
├── include/
│   ├── globals.h                  # getHouseSDLColor(house) - REBELS reads Custom_IBM
│   ├── DataTypes.h                # NUM_HOUSES=8, PALCOLOR_REBELS=192
│   └── Tile.h                     # lastAccess[NUM_HOUSES=8] (was NUM_TEAMS=7)
├── data/
│   ├── Custom_IBM.PAL             # REBELS dark grey ramp at indices 192-199
│   ├── RREBELS.voc                # House-name voice for 8th faction
│   ├── REGION*.INI + scen*.ini    # 8 Tornie campaigns + 2 vanilla (mirrored in PAK)
│   ├── HeraldRebels.png           # 8th house herald (dark grey)
│   ├── EliteSiegeTank.png         # Custom unit (8-dir paletted)
│   ├── FlameTank.png              # Custom unit (8-dir paletted)
│   ├── Tornie_AdvancedWindtrap_gfx.png      # 2-frame animated in-world sprite
│   └── Tornie_AdvancedWindtrap_gfx_editor.png  # 48x48 static editor icon
├── mods/Tornie/                   # Active mod (mirrors data/ + custom config)
│   ├── data/                      # 38 files mirrored from data/
│   └── campaign/                  # REGION*.INI + scen*.ini per house
└── scripts/
    └── make-tornie-pak.py         # Builds data/Tornie.PAK (260 entries)
```

### Per-House Color Tint System

The engine reads from **runtime `palette[]`** (set at GFX init from `Custom_IBM.PAL` for indices 192-199). For each non-HARKONNEN house, sprites are remapped at render time via `mapSurfaceColorRange(src, PALCOLOR_HARKONNEN, houseToPaletteIndex[house])`:

- `PALCOLOR_HARKONNEN = 144` (Harkonnen)
- `PALCOLOR_ATREIDES  = 160` (Atreides)
- `PALCOLOR_ORDOS     = 176` (Ordos)
- `PALCOLOR_FREMEN    = 192` (Fremen, **shared with REBELS**)
- `PALCOLOR_SARDAUKAR = 208` (Sardaukar)
- `PALCOLOR_MERCENARY = 224` (Mercenary)
- `PALCOLOR_NEUTRAL   = 128` (Neutral)

For `HOUSE_REBELS`, the surface palette at 192-199 is overwritten with `customColorRamp[192..199]` (the dark grey from `Custom_IBM.PAL`) so the engine reads the dark grey instead of vanilla Fremen orange.

### Versioning

The canonical app version lives in three source-controlled files, kept in sync by `scripts/bump-version.sh`:

```bash
scripts/bump-version.sh 1.0.427   # set version
scripts/bump-version.sh --check   # verify all files agree
```

Tag releases use `vX.Y.Z` (e.g. `v1.0.426`). CI verifies that source metadata already matches the tag before building.

---

## Building

### Prerequisites

- CMake 4.0+
- C++17 compiler (GCC 9+ on Linux, Clang on macOS, MSVC 2019+ on Windows)
- SDL2 + SDL2_mixer + SDL2_ttf + SDL2_gfx (vcpkg handles this)
- Original Dune II PAK files (ATRE.PAK, DUNE.PAK, HARK.PAK, MENTAT.PAK, MERC.PAK, ORDOS.PAK, SCENARIO.PAK, VOC.PAK, INTRO.PAK, FINALE.PAK, ENGLISH.PAK, SOUND.PAK, LEGACY.PAK, GFXHD.PAK, OPENSD2.PAK, INTROVOC.PAK) in `data/`

### Linux (tested)

```bash
# Install deps (Ubuntu)
sudo apt install cmake g++ libsdl2-dev libsdl2-mixer-dev libsdl2-ttf-dev \
                libsdl2-gfx-dev libcurl4-openssl-dev

# Configure + build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### macOS

```bash
brew install cmake sdl2 sdl2_mixer sdl2_ttf sdl2_gfx
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)
open build/bin/dunecity.app
```

Release DMGs are built on the self-hosted Apple Silicon GitHub Actions runner.
See [docs/macos-release-runner.md](docs/macos-release-runner.md) for runner setup,
artifact verification, and the release sequence.

### Windows

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release -j
build\bin\Release\dunecity.exe
```

### Running

```bash
# Linux
./build/bin/dunecity

# macOS
open build/bin/dunecity.app

# Windows
build\bin\Release\dunecity.exe
```

The first time you run, the game seeds mods at `~/.local/share/DuneCity/mods/` (Linux) or `%APPDATA%\DuneCity\mods\` (Windows). The Tornie Mod will be detected from `mods/Tornie/` in the install directory.

---

## Multi-Agent Workflow

This project is developed by **moonbear** (autonomous agent) and reviewed by **syntox** (principal). The Tornie Mod is steered by **Tornie** (Tornie Panther) for visual / asset / gameplay balance, and **Quix** is blocked at the gateway.

**Rules:**
1. **Trunk-based** — work on `main` or short-lived branches
2. **One authority per change** — accept instructions from any of: Tornie (mods), syntox (release/git), or ggtothemax (ops/website)
3. **Small changes** — prefer small compileable changes over broad refactors
4. **Versioned releases** — every user-visible change bumps the version and tags a release
5. **Coordinate on hot files** — `GFXManager.cpp`, `Game.cpp`, `CustomGamePlayers.cpp` are the most-edited

---

## Files of Interest

| Path | What It Is |
|------|-----------|
| `src/FileClasses/GFXManager.cpp` | Per-house remap, Custom_IBM.PAL, palette write, sprite load |
| `include/globals.h` | `getHouseSDLColor(house)` — REBELS reads Custom_IBM.PAL |
| `include/DataTypes.h` | `NUM_HOUSES=8`, `PALCOLOR_REBELS=192`, `houseToPaletteIndex[]` |
| `include/Tile.h` | `lastAccess[NUM_HOUSES]` (was `NUM_TEAMS=7` before v1.0.360) |
| `src/Menu/CustomGamePlayers.cpp` | 12-entry color swap dropdown (Original + Teal + 4 custom + 7 vanilla) |
| `src/Menu/SinglePlayerSkirmishMenu.cpp` | 8-house carousel (9 entries, scroll max 6) |
| `src/players/SpectatorPlayer.cpp` | 9th house type for non-playing slots |
| `src/Game.cpp` | Per-house color swap init at game start, save/load |
| `data/Custom_IBM.PAL` | REBELS dark grey ramp at 192-199 |
| `data/Tornie.PAK` | 260+ entries: campaigns, units, sprites, voices |
| `scripts/make-tornie-pak.py` | Builds `data/Tornie.PAK` from `data/` + `mods/Tornie/data/` |

---

## Version History (recent)

- **v1.0.426** — HOUSE_REBELS dark grey written to surface palette (fixes orange tint)
- **v1.0.425** — Removed v1.0.413 structure remap (diagnostic, restored v1.0.305 lazy remap)
- **v1.0.424** — Suppress "UI Graphic not loaded" log spam (log once per ID)
- **v1.0.423** — Rollback to v1.0.305 vanilla remap (regression fix)
- **v1.0.418** — Multi-color ghost fix on units (write source + dest slots to surface palette)
- **v1.0.415** — Advanced windtrap editor icon = in-map preview, Elite Siege Tank path fix
- **v1.0.414** — Reactivated 7 vanilla house color swap slots in CustomGamePlayers
- **v1.0.413** — Per-house structure sprite remap restored
- **v1.0.411** — Per-house remap-on-demand restored (units + buildings)
- **v1.0.408** — Tearnie campaign rewrite (8 houses + 2 vanilla × 22 scenarios)
- **v1.0.405** — Crash fix for GFX init recursion
- **v1.0.400** — Custom_IBM.PAL no longer applied as default
- **v1.0.394** — 4 new custom colors (Fushia/Apple Green/Dark Purple/Light Pink)
- **v1.0.358** — scenr001-022 Mercenary→Rebels re-routing (root cause of Rebels construction)

---

## Credits

- **Dune Legacy** by the [Dune Legacy team](https://github.com/dunelegacy/dunelegacy) — base engine
- **Micropolis** (SimCity open-source port) by the [SimHacker team](https://github.com/SimHacker/MicropolisCore) — city simulation reference
- **Tornie** (Tornie Panther) — mod author, art direction, asset uploads
- **moonbear** — autonomous AI agent, port and integration
- **syntox** — principal, release coordination
- Original **Westwood Studios** — Dune II (1992)
