# Dune Legacy AI Bots Reference Guide

## Overview

Dune Legacy includes four AI implementations with varying levels of sophistication. This document explains how each bot operates.

---

## 1. AIPlayer (Basic AI)

### Difficulty Levels
- **Easy**: Can only build military when weaker than enemy. 1 harvester per refinery.
- **Medium**: Can build military until 2x enemy strength. ~2/3 harvester per refinery.
- **Hard**: No restrictions. 2 harvesters per refinery.

### How It Works

**Update Cycle:** Every 50 game cycles, runs three functions:
1. `checkAllUnits()` - MCV deployment, harvester management
2. `build()` - Construction decisions
3. `attack()` - Military assault logic

**Build Order:**
1. WindTraps (when power deficit)
2. Refineries (up to 3-5 depending on difficulty)
3. Radar
4. StarPort
5. Rocket Turrets
6. Light Factory → Heavy Factory
7. IX, Palace, WOR, High Tech Factory

**Structure Placement:**
- Refineries: Near spice
- Factories/StarPorts: Near sand (vehicle exits)
- Turrets: Toward enemy
- Defensive buildings: Away from enemy

**Combat:**
- Timer-based attacks (faster on Hard difficulty)
- When timer expires, ALL military units set to HUNT mode
- When structures/harvesters damaged, idle units scramble to defend
- Harvesters attack infantry threats directly

**Special Weapons:**
- Harkonnen/Sardaukar: Death Hand at enemy with most structures
- Others: Fire immediately when ready

### Limitations
- No unit composition optimization
- No tactical retreating
- No coordinated squad movements
- "All-or-nothing" attacks

---

## 2. SmartBot (Improved AI)

### Difficulty Levels
- **Normal**: Standard behavior
- **Defense**: Prioritizes turrets; won't attack until 21+ Ornithopters
- **Hard**: Aggressive behavior

### How It Works

**Focus System:** Dynamically switches priorities based on credits:
- `focusEconomy()` → credits < 2000-4500
- `focusMilitary()` → credits > 3000-5000
- `focusFactory()` → credits > 4500-6375
- `focusBase()` → credits > 7500-9375

**Key Improvements over AIPlayer:**
- Builds MCV from Heavy Factory if no Construction Yard
- Scales harvesters based on army size
- Uses gap checking for building placement (pathways between buildings)
- Dynamic harvester limits based on map size
- Refinery limit of 10 maximum

**Build Order:**
1. WindTrap (1 required)
2. Refinery (economy-focused expansion)
3. StarPort
4. Light Factory, Radar, Heavy Factory
5. Repair Yard, High Tech Factory, IX
6. Additional factories when wealthy

**Combat Differences:**
- Launchers sent to repair when damaged
- Units set to AREAGUARD (responsive to nearby threats)
- Uses Carryall drops for long-distance defense
- Defense mode delays attacks until 21 Ornithopters

**StarPort Logic:**
1. MCV if no Construction Yard
2. Fill Harvesters to limit
3. Add 2 Carryalls
4. Buy discounted military units

---

## 3. QuantBot (Advanced AI)

### Difficulty Levels
- **Easy**: 25% attack force, no orni attacks, 1× harvester multiplier
- **Medium**: 40% attack force, no orni attacks, 2× harvester multiplier
- **Hard**: 50% attack force, orni attacks enabled, 2.5× multiplier, 2 guaranteed refineries
- **Brutal**: 60% attack force, aggressive orni strikes, 3× multiplier, highest limits
- **Defend**: Never attacks, purely defensive

### External Configuration

All behavior configurable via `QuantBot Config.ini`:
- Per-difficulty attack settings
- Military value limits by map size
- Harvester limits by map size
- Unit composition ratios per house
- Target priority weights for structures/units
- Attack timers and thresholds

### How It Works

**Military Value System:**
Tracks total army worth (unit count × unit price) and enforces limits:
- Campaign Mode: limit = initialMilitaryValue × difficultyMultiplier
- Custom Mode: limit based on map size (small/medium/large/huge)

**Adaptive Unit Composition:**
Calculates "damage loss ratio" (DLR) for each unit type:
```
DLR = damageInflicted / (lostUnits × unitPrice)
```

After 3000+ total damage, shifts production toward better-performing units. Early game uses house-specific defaults:

| House | Tank | Siege | Launcher | Special | Orni |
|-------|------|-------|----------|---------|------|
| Atreides | 5% | 10% | 35% | 35% (Sonic) | 15% |
| Harkonnen | 17% | 17% | 50% | 16% (Devastator) | 0% |
| Ordos | 25% | 25% | 0% | 25% (Deviator) | 25% |
| Fremen | 50% | 10% | 27% | 0% | 13% |
| Sardaukar | 5% | 40% | 45% | 0% | 10% |
| Mercenary | 30% | 30% | 30% | 5% | 10% |

Unit ratios capped at 80% max per type to ensure diversity.

**Building Placement:**
Uses scoring across entire map:
- Adjacent friendly structure bonus (+10 per tile)
- Adjacent enemy penalty (-10)
- Edge placement bonus (+12)
- Gap-filling penalty
- Building-specific preferences:
  - Refineries: Near spice + near sand for harvester access
  - Factories/Turrets: Near squad rally point and base center
  - Turrets: On enemy-facing side of base

**MCV Deployment:**
Evaluates locations by:
1. Distance from current position (closer = better)
2. Available rock tiles in 12-tile radius (more space = better)
3. Slight preference for map center

**Squad Rally System:**
Rally point = 75% base center + 25% enemy center

Military units gather here before attacks for coordinated assaults.

**Ornithopter Strike Team (Hard/Brutal):**
1. Waits for threshold (1 for Hard, 3 for Brutal)
2. Uses priority-weighted target selection
3. All ornithopters focus-fire same target
4. Auto-retargets when objective destroyed
5. "Last stand" strike if all structures destroyed

**Attack Logic:**
```
if (militaryValue >= militaryValueLimit × attackThreshold):
    attackForceLimit = militaryValueLimit × difficultyRatio
    for each unit:
        if (unitValue + usedValue <= attackForceLimit):
            set unit to HUNT mode
```

Creates wave-based attacks rather than committing everything.

**Kiting Mechanics:**
Launchers and Deviators retreat when threats approach:
- Trigger: target within weaponRange - 2 tiles
- Movement: 70% away from threat + 30% toward squad center

**Harvester Management:**
- Dynamic limits based on remaining map spice
- 10-second stuck detection per harvester
- Auto-return when low refineries and credits
- Spice-based cap: limit = min(configLimit, spice/2000)

**Death Hand Targeting:**
Weighted scoring:
```
score = (buildPriority + targetPriority) × productionMultiplier
score += Σ(nearbyWeights / distance)  // cluster bonus
```

Prioritizes clusters of high-value production buildings.

**Counter-Ornithopter Defense:**
When enemy ornithopters detected:
1. Fast-tracks CY upgrade to level 2
2. Builds radar if missing
3. Ensures power buffer for turrets (200 + 25 per turret)
4. Builds 2× enemy ornithopter count in rocket turrets

**Campaign vs Custom Mode:**

Campaign:
- Longer attack timers (8-12 minutes initial)
- Military limits based on starting forces
- Waits for player attack before becoming aggressive
- Rebuilds structures that existed at game start

Custom:
- Quick attack timers (15-75 seconds)
- Military limits based on map size
- Immediately gathers units at rally
- Dynamically expands based on wealth

**Deviated Unit Handling:**
- Devastators: Self-destruct immediately
- Harvesters: Return to base if carrying spice
- Others: Retreat to squad center

---

## 4. QuantBot Roles: Builder, Fighter, Both

### Overview
Menus choose an AI, a difficulty and a role (docs/ai-roles.md). Both is the standard
QuantBot. The other two are teammate roles for an AI that shares the human's house.

**Builder** (formerly "AI Support", classes `qBotSupport<Difficulty>`): handles base building and
economy while the human commands the army.
- Attack timer set to infinity: never initiates attacks.
- No coordinated ornithopter strikes, kiting, offensive repositioning or attack force calculations.
- Same building placement, economic scaling, power buffer and unit production as the standard QuantBot.

**Fighter** (classes `qBotFighter<Difficulty>`): commands units, launches attacks and defends while the
human builds.
- Never builds, produces, repairs, deploys MCVs or runs the harvester routine.
- Still fires palace weapons and pulls harvesters out of danger.
- Attacks without waiting for a Repair Yard; wave readiness leaves out units the human is ordering.

**Activation:** `QuantBot(house, name, difficulty, AIRole role)`; a `qBotSupport*` class from an old
save still loads as Builder.

**Use Cases:**
- Co-op campaigns and skirmishes: pick the half of the game the AI should play.
- Team games: a Builder and a Fighter AI can share one house.

---

## 5. Mentat (Advanced AI — City Simulation Aware)

### Difficulty Levels
- **Easy**: 25% attack force, no orni attacks, 1× harvester multiplier
- **Medium**: 40% attack force, no orni attacks, 2× harvester multiplier
- **Hard**: 50% attack force, orni attacks enabled, 2.5× multiplier
- **Brutal**: 60% attack force, aggressive orni strikes, 3× multiplier
- **Defend**: Never attacks, purely defensive

### Mentat Support Mode
Same as QuantBot Support — acts as a cooperative teammate. Handles base-building and economy; never initiates attacks.

### How It Differs from QuantBot

Mentat is QuantBot's refactored successor with **city simulation integration** baked in (~3,900 lines vs QuantBot's ~3,700).

**City-Sim Awareness:**
- Reads the **pollution density map** — avoids placing buildings in high-pollution zones
- Reads the **crime rate map** — factors crime impact into structure placement and strategy
- These checks run as part of the placement scoring system, improving long-term base efficiency

**Nuclear Plant Support:**
- Can build **Nuclear Plants** for power generation
- QuantBot only builds Windtraps and WORs for power; Mentat expands options

**Shared with QuantBot:**
- Squad rally system, kiting, ornithopter strikes, adaptive unit composition (DLR)
- External configuration via `QuantBot Config.ini` (shared config file)
- Same 5 difficulty levels + 5 Support variants (10 total entries in the player picker)
- Counter-ornithopter turret construction
- Harvester management, Death Hand targeting, MCV deployment logic

---

## Comparison Table

| Feature | AIPlayer | SmartBot | QuantBot | QB Support | Mentat | Mentat Support |
|---------|----------|----------|----------|------------|--------|----------------|
| Complexity | Basic | Moderate | Advanced | Advanced | Advanced+ | Advanced+ |
| Config File | No | No | Yes | Yes | Yes (shared) | Yes (shared) |
| Squad System | No | No | Yes | Yes (defense) | Yes | Yes (defense) |
| Adaptive Units | No | Partial | Full DLR | Full | Full DLR | Full |
| Kiting | No | No | Yes | No | Yes | No |
| Orni Strikes | No | Partial | Coordinated | No | Coordinated | No |
| Attack Style | All-in | Delayed | Wave-based | Never | Wave-based | Never |
| Counter-Orni | No | No | Auto-turrets | Auto-turrets | Auto-turrets | Auto-turrets |
| Eco Management | Fixed | Dynamic | Map-scaled | Map-scaled | Map-scaled | Map-scaled |
| City-Sim Aware | No | No | No | No | Yes | Yes |
| Nuclear Plants | No | No | No | No | Yes | Yes |
| Code Lines | ~800 | ~900 | ~3,700 | (shared) | ~3,900 | (shared) |

---

## TL;DR

- **AIPlayer** = Classic Dune 2 AI behavior - simple timer-based rushes
- **SmartBot** = Smarter economy management, some tactical awareness
- **QuantBot** = Full strategic depth with external configuration
- **QuantBot Support** = QuantBot as your cooperative teammate
- **Mentat** = QuantBot's successor — same strategic depth plus city simulation awareness (pollution/crime-aware placement, Nuclear Plant support)
- **Mentat Support** = Mentat as your cooperative teammate

