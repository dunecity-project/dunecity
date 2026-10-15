# DuneCity Feature Roadmap

## Keybinds
| Key | Action |
|-----|--------|
| `C` | Capture unit mode (existing) |
| `Shift+C` | Toggle city stats HUD |
| `Shift+B` | Open city budget screen |
| `Z` | Enter/exit zone placement mode |
| `1` (in zone mode) | Select Residential zone |
| `2` (in zone mode) | Select Commercial zone |
| `3` (in zone mode) | Select Industrial zone |
| `Shift+1` | Toggle overlay: None (default) |
| `Shift+2` | Toggle overlay: Power Grid |
| `Shift+3` | Toggle overlay: Traffic Density |
| `Shift+4` | Toggle overlay: Pollution |
| `Shift+5` | Toggle overlay: Land Value |
| `Shift+6` | Toggle overlay: Crime Rate |
| `Shift+7` | Toggle overlay: Population Density |
| `Shift+8` | Toggle overlay: Wind Trap Radius |
| `Shift+R` | Enter/exit road placement mode |
| `Escape` (in zone/road mode) | Exit placement mode |

---

## Phase 0: Foundation (DONE)
- [x] Fork Dune Legacy codebase
- [x] Integrate Micropolis city simulation engine
- [x] 16-phase city simulation loop (CitySimulation)
- [x] Zone simulation (R/C/I zones)
- [x] Power grid simulation
- [x] Traffic simulation
- [x] City budget and tax system
- [x] City evaluation and scoring
- [x] Save/Load city state
- [x] Economic victory condition

## Phase 1: Identity & First UI (DONE)
- [x] Rebrand: binary, window title, project name → DuneCity
- [x] City Stats HUD overlay (population, budget, power, score)
- [x] Feature tracking (this file)

## Phase 2: Zoning UI (DONE)
- [x] Zone placement tool (R/C/I zones on map tiles)
- [x] Visual zone indicators on map (colored overlays or tile borders)
- [x] Zone info tooltip on hover
- [x] Zones buildable as structures via Construction Yard (Phase 3 upgrade)

## Phase 3: Power Grid Visualization (DONE)
- [x] Power grid overlay mode (show powered/unpowered areas)
- [x] Power coverage percentage in city stats HUD
- [x] Wind Trap radius visualization
- [x] Power shortage warnings

## Phase 4: City Building Menu (DONE)
- [x] Zone structures (R/C/I) buildable via Construction Yard (100 credits each)
- [x] Sand placement support (zones can be placed on any non-mountain terrain)
- [x] Removed legacy freeform zone placement (Z hotkey)
- [x] Dedicated "City" build menu tab alongside military buildings
- [x] City-specific structures (roads, power lines)
- [x] Build cost integration with city budget

## Phase 5: Economy Integration (DONE)
- [x] Dual economy display (Spice credits + City tax revenue)
- [x] City budget management screen
- [x] Tax rate adjustment UI
- [x] Funding allocation sliders (police, fire, transport)

## Phase 6: Traffic & Roads (PARTIAL)
- [x] Road placement tool
- [x] Traffic density visualization overlay
- [x] Road network affecting zone growth rates

## Phase 7: Map Overlays (DONE)
- [x] Data layer toggles: crime, pollution, land value, population density, traffic, power
- [x] Color-coded overlay rendering on game map
- [x] Legend/key for overlay values

## Phase 8: Disasters & Events
- [x] Sandstorm damage to city zones
- [x] Sandworm attacks on populated areas
- [x] Fire spread from combat into city zones
- [x] Disaster notification system

## Phase 9: Polish & Balance
- [x] Balance city growth rates with RTS pacing
- [x] Tutorial/tooltip for city mechanics
- [x] City milestone notifications
- [x] Sound effects for city events

## Phase 10: Modern RTS controls & skirmish (local, see HANDOVER 2026-10-13)
- [x] Attack-move, Hold, Patrol, Shift-queued orders with waypoint lines (docs/rts-controls.md)
- [x] Repeat production, Alt+number group add, double-click / Ctrl+A selection, hotkeys in tooltips
- [x] Construction Yard pre-placement queue, spread over all yards (docs/preplace-queue.md)
- [x] Two-column build list grid (docs/sidebar-grid.md)
- [x] Formation movement for players and AI (docs/formations.md)
- [x] Random Skirmish generator: map, teams, bases, armies, cities, tech level (docs/skirmish-generator.md)

## Future Ideas
- [x] Multiplayer city comparison (competitive city scores) - requires network infrastructure
- [x] Faction-specific city bonuses (Atreides = higher approval, Harkonnen = cheaper industry)
- [x] Inter-city trade routes between allied players - requires network infrastructure
- [x] City-produced units (militia from population)
