/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <players/QuantBotConfig.h>
#include <players/QuantBotCoreDefencePolicy.h>
#include <players/QuantBotVanillaPolicy.h>
#include <FormationPlanner.h>
#include <FileClasses/FileManager.h>
#include <FileClasses/INIFile.h>
#include <mod/ModManager.h>
#include <misc/fnkdat.h>
#include <misc/FileSystem.h>
#include <misc/exceptions.h>
#include <data.h>
#include <globals.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_set>

// Constructor with default values
QuantBotConfig::QuantBotConfig() {
    
    // === DEFEND DIFFICULTY (Very Easy) ===
    defend.attackEnabled = false;                           // Never attacks
    defend.attackThresholdPercent = 0.90f;                  // 90% - very defensive (rarely used since attackEnabled=false)
    defend.ornithopterAttackEnabled = false;                // No ornithopter attacks
    defend.ornithopterAttackThreshold = 999;                // Effectively disabled
    defend.attackForceMilitaryValueRatio = 0.0f;            // N/A (doesn't attack)
    defend.harvesterLimitPerRefineryMultiplier = 2;         // Campaign: 2 harvesters per refinery
    defend.militaryValueMultiplier = 1.8f;                  // Campaign: 1.8x initial military value
    defend.refineryMinimum = 0;                             // Campaign: No guaranteed refineries
    defend.harvesterLimitCustomSmallMap = 3;                // Custom: Small map (32x32)
    defend.harvesterLimitCustomMediumMap = 4;               // Custom: Medium map (64x64)
    defend.harvesterLimitCustomLargeMap = 15;               // Custom: Large map (128x128)
    defend.harvesterLimitCustomHugeMap = 20;                // Custom: Huge map (>128x128)
    defend.militaryValueLimitCustomSmallMap = 4000;
    defend.militaryValueLimitCustomMediumMap = 10000;
    defend.militaryValueLimitCustomLargeMap = 25000;
    defend.militaryValueLimitCustomHugeMap = 30000;         // Custom: Huge map (>128x128)
    defend.structureDefenders = 5;                          // Max units to scramble for structure defense
    defend.harvesterDefenders = 3;                          // Max units to scramble for harvester defense
    
    // === EASY DIFFICULTY ===
    easy.attackEnabled = true;                              // Can attack
    easy.attackThresholdPercent = 0.50f;                    // 50% - attack when half ready
    easy.ornithopterAttackEnabled = false;                  // NO ornithopter attacks
    easy.ornithopterAttackThreshold = 999;                  // Disabled
    easy.attackForceMilitaryValueRatio = 0.25f;             // 25% of military value per attack
    easy.harvesterLimitPerRefineryMultiplier = 1;           // Campaign: 1 harvester per refinery
    easy.militaryValueMultiplier = 2.0f;                    // Campaign: 2.0x initial (min 2000 at mission 21+)
    easy.refineryMinimum = 0;                               // Campaign: No guaranteed refineries
    easy.harvesterLimitCustomSmallMap = 2;
    easy.harvesterLimitCustomMediumMap = 2;
    easy.harvesterLimitCustomLargeMap = 8;
    easy.harvesterLimitCustomHugeMap = 15;
    easy.militaryValueLimitCustomSmallMap = 3000;
    easy.militaryValueLimitCustomMediumMap = 8000;
    easy.militaryValueLimitCustomLargeMap = 15000;
    easy.militaryValueLimitCustomHugeMap = 25000;
    easy.structureDefenders = 10;
    easy.harvesterDefenders = 5;
    
    // === MEDIUM DIFFICULTY ===
    medium.attackEnabled = true;
    medium.attackThresholdPercent = 0.40f;                  // 40%
    medium.ornithopterAttackEnabled = false;                // NO ornithopter attacks
    medium.ornithopterAttackThreshold = 999;                // Disabled
    medium.attackForceMilitaryValueRatio = 0.40f;           // 40% of military value per attack
    medium.harvesterLimitPerRefineryMultiplier = 2;         // Campaign: 2 harvesters per refinery
    medium.militaryValueMultiplier = 2.0f;                  // Campaign: 2.0x initial (min 4000 at mission 21+)
    medium.refineryMinimum = 0;                             // Campaign: No guaranteed refineries
    medium.harvesterLimitCustomSmallMap = 3;
    medium.harvesterLimitCustomMediumMap = 4;
    medium.harvesterLimitCustomLargeMap = 15;
    medium.harvesterLimitCustomHugeMap = 25;
    medium.militaryValueLimitCustomSmallMap = 5000;
    medium.militaryValueLimitCustomMediumMap = 12000;
    medium.militaryValueLimitCustomLargeMap = 25000;
    medium.militaryValueLimitCustomHugeMap = 40000;
    medium.structureDefenders = 15;
    medium.harvesterDefenders = 10;
    
    // === HARD DIFFICULTY ===
    hard.attackEnabled = true;
    hard.attackThresholdPercent = 0.30f;                    // 30% - aggressive
    hard.ornithopterAttackEnabled = true;
    hard.ornithopterAttackThreshold = 1;                    // Attack as soon as 1 ornithopter is ready
    hard.attackForceMilitaryValueRatio = 0.50f;             // 50% of military value per attack (large attacks)
    hard.harvesterLimitPerRefineryMultiplier = 2;           // Campaign: 2.5 harvesters per refinery (rounded to 2)
    hard.militaryValueMultiplier = 2.5f;                    // Campaign: 2.5x initial (fixed 10000 at mission 21+)
    hard.refineryMinimum = 2;                               // Campaign: Guaranteed 2 refineries (tops up if needed)
    hard.harvesterLimitCustomSmallMap = 4;
    hard.harvesterLimitCustomMediumMap = 7;
    hard.harvesterLimitCustomLargeMap = 40;
    hard.harvesterLimitCustomHugeMap = 40;
    hard.militaryValueLimitCustomSmallMap = 8000;
    hard.militaryValueLimitCustomMediumMap = 20000;
    hard.militaryValueLimitCustomLargeMap = 50000;
    hard.militaryValueLimitCustomHugeMap = 70000;
    hard.structureDefenders = 25;
    hard.harvesterDefenders = 15;
    
    // === BRUTAL DIFFICULTY ===
    brutal.attackEnabled = true;
    brutal.attackThresholdPercent = 0.25f;                  // 25% of its military ceiling
    brutal.ornithopterAttackEnabled = true;
    brutal.ornithopterAttackThreshold = 3;                  // Attack as soon as 3 ornithopters are ready
    brutal.attackForceMilitaryValueRatio = 0.60f;           // 60% of military value per attack (massive attacks)
    brutal.harvesterLimitPerRefineryMultiplier = 3;         // Campaign: 3 harvesters per refinery
    brutal.militaryValueMultiplier = 4.0f;                  // Campaign: 4.0x initial military
    brutal.refineryMinimum = 2;                             // Campaign: Guaranteed 2 refineries (tops up if needed)
    brutal.harvesterLimitCustomSmallMap = 6;
    brutal.harvesterLimitCustomMediumMap = 10;
    brutal.harvesterLimitCustomLargeMap = 100;
    brutal.harvesterLimitCustomHugeMap = 50;
    brutal.militaryValueLimitCustomSmallMap = 20000;
    brutal.militaryValueLimitCustomMediumMap = 40000;
    brutal.militaryValueLimitCustomLargeMap = 80000;
    brutal.militaryValueLimitCustomHugeMap = 100000;
    brutal.structureDefenders = 25;
    brutal.harvesterDefenders = 25;
    
    // === UNIT COMPOSITION RATIOS ===
    // Single set of ratios used across all difficulties
    // Ornithopter spam controlled by OrnithopterAttackEnabled flag (disabled for Defend/Easy/Medium)
    
    // Atreides - Balanced mix with strong launcher/sonic presence
    unitRatios.atreides.tank = 0.05f;
    unitRatios.atreides.siegeTank = 0.10f;
    unitRatios.atreides.launcher = 0.35f;
    unitRatios.atreides.special = 0.35f;          // Sonic tank
    unitRatios.atreides.ornithopter = 0.15f;
    
    // Harkonnen - Cannot build ornithopters, focuses on heavy firepower (capped launcher at 50%)
    unitRatios.harkonnen.tank = 0.17f;
    unitRatios.harkonnen.siegeTank = 0.17f;
    unitRatios.harkonnen.launcher = 0.50f;
    unitRatios.harkonnen.special = 0.16f;         // Devastator
    unitRatios.harkonnen.ornithopter = 0.00f;     // Can't build
    
    // Ordos - Balanced with launchers, minimal deviators, strong air support
    unitRatios.ordos.tank = 0.20f;
    unitRatios.ordos.siegeTank = 0.20f;
    unitRatios.ordos.launcher = 0.30f;
    unitRatios.ordos.special = 0.05f;             // Deviator
    unitRatios.ordos.ornithopter = 0.25f;
    
    // Fremen - Balanced with light ornithopter support (capped tank at 50%)
    unitRatios.fremen.tank = 0.50f;
    unitRatios.fremen.siegeTank = 0.10f;
    unitRatios.fremen.launcher = 0.27f;
    unitRatios.fremen.special = 0.00f;
    unitRatios.fremen.ornithopter = 0.13f;
    
    // Sardaukar - Heavy firepower with light ornithopter support
    unitRatios.sardaukar.tank = 0.05f;
    unitRatios.sardaukar.siegeTank = 0.40f;
    unitRatios.sardaukar.launcher = 0.45f;
    unitRatios.sardaukar.special = 0.00f;
    unitRatios.sardaukar.ornithopter = 0.10f;
    
    // Mercenary - Balanced across all unit types
    unitRatios.mercenary.tank = 0.30f;
    unitRatios.mercenary.siegeTank = 0.30f;
   unitRatios.mercenary.launcher = 0.30f;
    unitRatios.mercenary.special = 0.05f;
    unitRatios.mercenary.ornithopter = 0.10f;

    // Tuned starting mixes ([Unit Ratios Tuned], Mix=Tuned; percent of tank, siege tank, launcher,
    // Devastator, Sonic Tank, Deviator, ornithopter, trike, raider trike, quad), measured on
    // Construction Yard starts (docs/ai-play-styles.md, section 8): per house the round-2 mix that
    // did best for it - the siege/Sonic refinement for Atreides (it also keeps
    // quantbot_all_against_atreides 6/6; the winners' end composition took 40.7 minutes on one
    // seed) and Ordos, the gradient mix for Harkonnen; each house's one special keeps the former
    // special share. Bounded like every starting mix (2%-50% per producible slot). Index 0 is the
    // house's top mix (every tech band without its own); [house][N] the band from tech level N.
    tunedRatios = {};
    tunedOpening = {};
    tunedRatios[0][0] = {14, 16, 15, 22, 0, 0, 0, 0, 0, 33};   // Harkonnen (Devastator)
    tunedRatios[1][0] = {9, 20, 13, 0, 22, 0, 13, 10, 0, 13};  // Atreides (Sonic Tank)
    tunedRatios[2][0] = {9, 17, 7, 0, 0, 13, 27, 0, 7, 20};    // Ordos (Deviator)
    // Fremen: the x8 winners' end composition (section 8.10; v the configured opening +0.24
    // (+0.05..+0.42) over two fresh-seed runs, n=630). Sardaukar and Mercenary: none (no candidate
    // beat the configured opening), so their top band plays it.
    tunedRatios[3][0] = {11, 10, 13, 5, 8, 0, 18, 11, 13, 11};
    // Lower tech bands: set by design from what each band can field (tests/ai/tech-bands.py; not
    // measured, docs/ai-play-styles.md 8.9): two light slots split evenly; light units only at 3
    // (quads the strongest); tank-heavy at 4 (50% tanks); tanks and launchers lead at 5 (40/25); siege
    // added at 6 (30/25/20); the light remainder follows the house's top mix (Harkonnen quads, the
    // Atreides trike/quad, Ordos raider/quad). A band with one fieldable slot plays it alone.
    tunedRatios[0][4] = {50, 0, 0, 0, 0, 0, 0, 0, 0, 50};      // Harkonnen: tank, quad
    tunedRatios[0][5] = {40, 0, 25, 0, 0, 0, 0, 0, 0, 35};
    tunedRatios[0][6] = {30, 25, 20, 0, 0, 0, 0, 0, 0, 25};
    tunedRatios[1][3] = {0, 0, 0, 0, 0, 0, 0, 50, 0, 50};      // Atreides: trike, quad
    tunedRatios[1][4] = {50, 0, 0, 0, 0, 0, 0, 20, 0, 30};
    tunedRatios[1][5] = {40, 0, 25, 0, 0, 0, 0, 15, 0, 20};
    tunedRatios[1][6] = {30, 25, 20, 0, 0, 0, 0, 10, 0, 15};
    tunedRatios[2][3] = {0, 0, 0, 0, 0, 0, 0, 0, 50, 50};      // Ordos: raider trike, quad
    tunedRatios[2][4] = {50, 0, 0, 0, 0, 0, 0, 0, 20, 30};
    tunedRatios[2][5] = {40, 0, 25, 0, 0, 0, 0, 0, 15, 20};    // tech 5-6 (siege needs the IX band)
    for (const size_t house : {size_t(3), size_t(4), size_t(5)}) {   // Fremen, Sardaukar, Mercenary
        if (house != 4) tunedRatios[house][2] = {0, 0, 0, 0, 0, 0, 0, 50, 50, 0};   // Sardaukar: 1-2 none
        tunedRatios[house][3] = {0, 0, 0, 0, 0, 0, 0, 30, 30, 40};
        tunedRatios[house][4] = {50, 0, 0, 0, 0, 0, 0, 15, 15, 20};
        tunedRatios[house][5] = {40, 0, 25, 0, 0, 0, 0, 10, 10, 15};
        tunedRatios[house][6] = {30, 25, 20, 0, 0, 0, 0, 8, 7, 10};
    }

    // === TARGET PRIORITY TABLES (defaults mirror original game weights) ===
    registerStructurePriority(Structure_Slab1, "Slab1", 0, 5);
    registerStructurePriority(Structure_Slab4, "Slab4", 0, 10);
    registerStructurePriority(Structure_Palace, "Palace", 0, 400);
    registerStructurePriority(Structure_LightFactory, "LightFactory", 0, 200);
    registerStructurePriority(Structure_HeavyFactory, "HeavyFactory", 0, 600);
    registerStructurePriority(Structure_HighTechFactory, "HighTechFactory", 0, 200);
    registerStructurePriority(Structure_IX, "IX", 0, 100);
    registerStructurePriority(Structure_WOR, "WOR", 0, 175);
    registerStructurePriority(Structure_ConstructionYard, "ConstructionYard", 0, 300);
    registerStructurePriority(Structure_WindTrap, "WindTrap", 0, 300);
    registerStructurePriority(Structure_Barracks, "Barracks", 0, 100);
    registerStructurePriority(Structure_StarPort, "StarPort", 0, 250);
    registerStructurePriority(Structure_Refinery, "Refinery", 0, 300);
    registerStructurePriority(Structure_RepairYard, "RepairYard", 0, 600);
    registerStructurePriority(Structure_Wall, "Wall", 0, 30);
    registerStructurePriority(Structure_GunTurret, "GunTurret", 75, 150);
    registerStructurePriority(Structure_RocketTurret, "RocketTurret", 100, 75);
    registerStructurePriority(Structure_Silo, "Silo", 0, 150);
    registerStructurePriority(Structure_Radar, "Radar", 0, 275);
    if (ModManager::instance().isTornieFamilyContentActive()) {
        registerStructurePriority(Structure_Doublefinery, "Doublefinery", 0, 300);
        registerStructurePriority(Structure_Slowpost, "Slowpost", 75, 150);
    }

    registerUnitPriority(Unit_Carryall, "Carryall", 20, 16);
    registerUnitPriority(Unit_Devastator, "Devastator", 175, 180);
    registerUnitPriority(Unit_Deviator, "Deviator", 50, 175);
    registerUnitPriority(Unit_Frigate, "Frigate", 0, 0);
    registerUnitPriority(Unit_Harvester, "Harvester", 10, 150);
    registerUnitPriority(Unit_Soldier, "Soldier", 10, 10);
    registerUnitPriority(Unit_Launcher, "Launcher", 100, 150);
    registerUnitPriority(Unit_MCV, "MCV", 10, 150);
    registerUnitPriority(Unit_Ornithopter, "Ornithopter", 75, 30);
    registerUnitPriority(Unit_Quad, "Quad", 60, 60);
    registerUnitPriority(Unit_Saboteur, "Saboteur", 0, 700);
    registerUnitPriority(Unit_Sandworm, "Sandworm", 0, 0);
    registerUnitPriority(Unit_SiegeTank, "SiegeTank", 130, 150);
    registerUnitPriority(Unit_SonicTank, "SonicTank", 80, 110);
    registerUnitPriority(Unit_Tank, "Tank", 80, 100);
    registerUnitPriority(Unit_Trike, "Trike", 50, 50);
    registerUnitPriority(Unit_RaiderTrike, "RaiderTrike", 55, 60);
    registerUnitPriority(Unit_Trooper, "Trooper", 20, 30);
    registerUnitPriority(Unit_Special, "Special", 0, 0);
    registerUnitPriority(Unit_Infantry, "Infantry", 20, 20);
    registerUnitPriority(Unit_Troopers, "Troopers", 50, 50);
    if (ModManager::instance().isTornieFamilyContentActive()) {
        registerUnitPriority(Unit_Troopers5, "Troopers5", 50, 50);
        registerUnitPriority(Unit_Infantry5, "Infantry5", 20, 20);
        registerUnitPriority(Unit_FlameQuad, "FlameQuad", 60, 60);
        registerUnitPriority(Unit_GasInfantry, "GasInfantry", 20, 20);
        registerUnitPriority(Unit_FlameTrooper, "FlameTrooper", 20, 30);
        registerUnitPriority(Unit_EliteOrnithopter, "EliteOrnithopter", 75, 30);
        registerUnitPriority(Unit_RaiderQuad, "RaiderQuad", 60, 60);
        registerUnitPriority(Unit_RangedTank, "RangedTank", 80, 100);
        registerUnitPriority(Unit_SlowTank, "SlowTank", 80, 100);
        registerUnitPriority(Unit_CapacityHarvester, "CapacityHarvester", 10, 150);
        registerUnitPriority(Unit_FastHarvester, "FastHarvester", 10, 150);
        registerUnitPriority(Unit_TameSandworm, "TameSandworm", 80, 100);
    }
    
    // === ARMY RECOVERY AND FRONT BATTERIES (Custom Hard/Brutal) ===
    // All PROVISIONAL and untuned. Milliseconds of game time, or basis points.
    recovery.enabled = true;
    recovery.attritionWindowMs = 90000;         // 90 s material attrition window
    recovery.lossShareBps = 2000;               // 20% of the deployed force cost
    recovery.tradeShareBps = 6500;              // Confirmed kills < 65% of our losses
    recovery.lossFloorCredits = 1500;           // Below this, an ordinary raid
    recovery.localWithdrawBps = 13000;          // 1.3x local enemy power
    recovery.localSevereBps = 20000;            // 2.0x is severe
    recovery.localPersistMs = 9000;             // Must hold for 9 s
    recovery.resumeAssembledBps = 8000;         // 80% of the designated wave
    recovery.resumeAdvantageBps = 12000;        // 1.2x front advantage
    recovery.stabiliseMs = 25000;               // 25 s stabilisation
    recovery.minWithdrawMs = 15000;
    recovery.maxWithdrawMs = 75000;             // Bounded fallback
    recovery.maxRecoverMs = 180000;             // Bounded fallback
    recovery.outnumberedBps = 15000;            // 1.5x hostile front power
    recovery.dispatchBypassBps = 8000;          // 80% of the CONFIGURED limit
    recovery.recallOrdersPerPass = 12;
    recovery.frontBatteriesEnabled = true;
    recovery.withdrawalsEnabled = false;           // measured: docs/quantbot-all-against-atreides.md

    // === FORCE CONCENTRATION (Custom Hard/Brutal) ===
    // Force concentration (docs/formations.md); the r3831-w151 seven-gene treatment and
    // original-map scope repairs are documented in docs/ai-scenario-matrix.md.
    force.focusFire = true;
    force.focusFireLevel = 0;
    force.targetPriorityLevel = 0;
    force.converge = false;
    force.waveReadinessPercent = 60;
    force.bundle = true;
    force.launchFromBundle = true;
    // Round 5 (defence while outnumbered, /weak duels): perimeter turrets and sized defences
    // (UX). The perimeter hold V was removed in protocol 84 (placement-score ablation, docs/
    // formations.md "Free-for-all: finishing the game"); an old PerimeterHold key is ignored.
    force.perimeterTurrets = true;
    force.sizedDefence = true;
    // r3831-w151 releases the full-readiness opening except in OpeningOpenMapScope.
    force.openingReadiness = false;
    force.turretPriority = true;
    force.pressOn = true;
    force.finishWeakest = false;
    force.fortifiedWaves = false;
    force.sonicClearLane = false;
    force.spendAtCap = false;
    force.siegeStandoff = false;
    force.massAssault = false;
    force.finishWhenLeading = false;
    force.vanillaFfaWeakestTarget = false;
    force.vanillaFfaNearbyTarget = false;
    force.vanillaFfaReserve = false;
    force.vanillaFfaFullWaves = false;
    force.vanillaFfaStarportAfterWave = false;
    force.starportBuying = 0;
    force.starportCashStockpile = 0;
    force.starportEconomyPriority = 0;
    force.vanillaFfaThreatProduction = false;
    force.vanillaFfaSingleFront = false;
    force.vanillaFfaLightWorkingCash = false;
    force.vanillaFfaFundedTech = false;
    force.vanillaHarvesterBoom = false;
    force.vanillaRefineryBoom = false;
    force.vanillaGreedyBoom = false;
    force.vanillaReactiveBoom = false;
    force.vanillaBoomFfaOnly = false;
    force.vanillaBoomSpiceGate = false;
    force.vanillaBoomSpicePerRefinery = 4000;
    force.duelOpeningPlan = 2;
    force.vanillaHarvesterBoomWorkers = 8;
    force.vanillaRefineryBoomRefineries = 4;
    force.vanillaGreedyBoomMinutes = 6;
    force.vanillaReactiveArmyRatioBps = 6000;
    force.vanillaReactiveEconomyRatioBps = 12000;
    force.vanillaCoreDefence = false;
    force.vanillaCoreDefenceThreatBps = 5000;
    force.vanillaArmyCounters = true;  // counters50-S5, default since protocol 119
    force.vanillaArmyCounterBlendBps = 5000;
    force.economicFleetRatios = false;
    force.fleetHarvestersPerBay = 3;
    force.fleetCarryallQuarters = 1;
    force.fleetRepairYards = 1;
    force.repairCapacityDemand = false;
    force.ffaVultureLevel = 0;
    force.techIncomeTiming = 0;
    // Candidate (fix/ladder-campaign, docs/ai-scenario-matrix.md "Team games"): team waves join an
    // ally's fresh wave.
    force.joinAllyWaves = false;
    force.teamSynchroniseAttacks = false;
    force.teamDefenceSharePercent = 0;
    force.openingCombatBudget = false;
    force.campaignCombatBudget = false;
    force.singleMissionCampaignPolicy = true;
    force.campaignPlayerHouse = false;
    force.campaignMilitaryOpening = true;
    force.campaignAdaptiveReadiness = true;
    force.campaignAllyStrengthFloor = true;
    force.campaignEasyHarvesters = 1;
    force.campaignMediumHarvesters = 1;
    force.campaignHardHarvesters = 2;
    force.campaignEasyWaveValue = 1700;
    force.campaignMediumWaveValue = 2500;
    force.campaignHardWaveValue = 3500;
    force.campaignEasyOpeningDelaySec = 0;
    force.campaignMediumOpeningDelaySec = 0;
    force.campaignHardOpeningDelaySec = 60;
    force.campaignThreatProduction = true;
    force.campaignFundedLightOpening = true;
    force.campaignLightWorkingCash = true;
    force.campaignLightIncomeSeed = true;
    force.campaignFactoryBeforeStarport = false;
    force.campaignIdleCashFactory = true;
    force.campaignIdleCashFactoryCredits = 2000;
    force.campaignRefineryDefence = false;
    force.campaignFactoryRunway = false;
    force.campaignHomeFields = 2;
    force.campaignSizedWorkerRescue = 0;
    force.capacityMcvRunway = false;
    force.respectServiceOwnership = false;
    force.clearMcvFootprint = true;
    force.smallMapYardCap = false;
    force.deathHandTurrets = false;
    force.palaceValueTarget = 0;
    force.palaceWaveTiming = 0;
    force.spreadVsDeathHand = false;
    // On since 1.0.861: no-spice 1v1, 16 seeds, blue wiped out 8/16 -> 0/16; matrix neutral.
    force.defencePrerequisites = true;
    // Stefan, 2026-11-01: Hard's city 20k/40k/60k/90k, growing to the largest other city.
    force.hardCityMatch = true;
    force.researchLab = true;
    force.researchLabProgression = true;
    force.cityGrowthPower = true;
    force.cityPowerSizing = true;
    force.cityOpeningCapital = true;
    force.cityConstructionCapital = true;
    force.cityProductionPrerequisites = true;
    force.cityFirstFactorySaving = false;
    force.cityThreatBudget = false;
    force.cityZoneDefence = false;
    force.cityAdvancedTech = false;
    force.cityContinuousResearch = false;
    force.cityResearchOrder = 2;
    force.citySpiceTransition = false;
    force.spiceFieldExpansion = 0;
    force.citySpiceCapacityRunway = false;
    force.citySingleYardGrowth = false;
    force.cityPowerGrowthHorizon = false;
    force.cityBoomWorkers = false;
    force.cityBoomRefineries = false;
    force.cityBoomGreedy = false;
    force.cityBoomShortTax = false;
    force.cityBoomReactive = false;
    force.cityBoomWorkerGoal = 16;
    force.cityBoomRefineryGoal = 6;
    force.cityBoomGreedyMinutes = 6;
    force.cityBoomTaxMinutes = 6;
    force.cityBoomArmyRatioBps = 6000;
    force.cityBoomEconomyRatioBps = 12000;
    force.cityBoomSpiceGate = false;
    force.cityBoomPaybackMinutes = 10;
    force.cityHarvestFieldChoice = false;
    force.cityHarvestFieldSafetyTiles = 6;
    force.cityRefineryLogistics = false;
    force.cityRefinerySiteTiles = 8;
    force.cityHarvesterReplacement = false;
    force.cityHarvesterReplacementMinutes = 10;
    force.cityLandValueServices = false;
    force.cityLandValueFundingPercent = 60;
    force.cityGrowthStageTax = false;
    force.cityGrowthTaxPopulation = 3000;
    force.cityCapitalPacing = 2;  // default since protocol 120 (1 in 118-119)
    force.cityFleetRatios = 0;
    force.cityTechRefineries = 0;
    force.cityYardRefineries = 0;
    force.cityWorkerReplacement = 0;
    force.closingMountainScope = 0;
    force.convergeChokepointScope = 1;
    force.openingOpenMapScope = 1;
    force.vanillaArmyCounterMapScope = 5;
    force.focusFireMapScope = 0;
    force.spiceBloomHarvest = 1;
    force.retreatPreserve = false;
    force.retreatPreserveLevel = 1;
    force.harvesterRaid = false;
    force.harvesterRaidLevel = 1;
    force.threatTurrets = true;
    force.moreRocketTurrets = false; // Opt-in: the final paired count-gain gate was not met.
    force.cityStarportSupply = true;
    force.storageCapacity = true;
    force.powerLeadBuildings = 0;
    force.siloFillPercent = 0;
    force.fundedClassicProduction = true;
    force.trooperSupport = true; // Provisional shared-candidate ON; outcome gate decides the final default.
    force.simpleTechSavings = false;
    force.launcherKiteExtraDistance = 2; // Accepted ON after the paired two-cohort outcome audit.
    force.exhaustedArmyDispatch = true;
    force.mediumClassic = 0; // Regression807 diagnostic: 1.0.807 Custom Medium economy (1) / army (2).

    // === GENERAL AI BEHAVIOR ===
    attackTimerMs = 15000;                      // 15 seconds between attacks
    attackThresholdPercent = 0.30f;             // Attack when military >= 30% of limit
    minMoneyForProduction = 500;                // Minimum money to produce units
    strategicSiteGuards = true;
    policeFundingRecovery = true;
    deviatedHarvesterEarlyReturn = true;
    civWarsStrategy = false;                   // Experimental: mirrors did not meet the finishing gate.
    // Attack waves hunt without a formation: in the formation research (docs/formations.md,
    // "Formation research") plain engine-Hunt waves beat every formation shape.
    waveFormation = kWaveFormationNone;
}

// Recovery/battery settings are integers, so the INI round-trip cannot change a
// value the way a float can; load clamps nothing silently beyond refusing a
// negative window, which the policy itself would otherwise treat as "no window".
static void saveRecoverySettings(INIFile& iniFile, const std::string& section,
                                 const QuantBotConfig::RecoverySettings& settings) {
    iniFile.setBoolValue(section, "Enabled", settings.enabled);
    iniFile.setIntValue(section, "AttritionWindowMs", settings.attritionWindowMs);
    iniFile.setIntValue(section, "LossShareBps", settings.lossShareBps);
    iniFile.setIntValue(section, "TradeShareBps", settings.tradeShareBps);
    iniFile.setIntValue(section, "LossFloorCredits", settings.lossFloorCredits);
    iniFile.setIntValue(section, "LocalWithdrawBps", settings.localWithdrawBps);
    iniFile.setIntValue(section, "LocalSevereBps", settings.localSevereBps);
    iniFile.setIntValue(section, "LocalPersistMs", settings.localPersistMs);
    iniFile.setIntValue(section, "ResumeAssembledBps", settings.resumeAssembledBps);
    iniFile.setIntValue(section, "ResumeAdvantageBps", settings.resumeAdvantageBps);
    iniFile.setIntValue(section, "StabiliseMs", settings.stabiliseMs);
    iniFile.setIntValue(section, "MinWithdrawMs", settings.minWithdrawMs);
    iniFile.setIntValue(section, "MaxWithdrawMs", settings.maxWithdrawMs);
    iniFile.setIntValue(section, "MaxRecoverMs", settings.maxRecoverMs);
    iniFile.setIntValue(section, "OutnumberedBps", settings.outnumberedBps);
    iniFile.setIntValue(section, "DispatchBypassBps", settings.dispatchBypassBps);
    iniFile.setIntValue(section, "RecallOrdersPerPass", settings.recallOrdersPerPass);
    iniFile.setBoolValue(section, "FrontBatteriesEnabled", settings.frontBatteriesEnabled);
    iniFile.setBoolValue(section, "WithdrawalsEnabled", settings.withdrawalsEnabled);
}

static void loadRecoverySettings(const INIFile& iniFile, const std::string& section,
                                 QuantBotConfig::RecoverySettings& settings) {
    settings.enabled = iniFile.getBoolValue(section, "Enabled", settings.enabled);
    settings.attritionWindowMs = iniFile.getIntValue(section, "AttritionWindowMs", settings.attritionWindowMs);
    settings.lossShareBps = iniFile.getIntValue(section, "LossShareBps", settings.lossShareBps);
    settings.tradeShareBps = iniFile.getIntValue(section, "TradeShareBps", settings.tradeShareBps);
    settings.lossFloorCredits = iniFile.getIntValue(section, "LossFloorCredits", settings.lossFloorCredits);
    settings.localWithdrawBps = iniFile.getIntValue(section, "LocalWithdrawBps", settings.localWithdrawBps);
    settings.localSevereBps = iniFile.getIntValue(section, "LocalSevereBps", settings.localSevereBps);
    settings.localPersistMs = iniFile.getIntValue(section, "LocalPersistMs", settings.localPersistMs);
    settings.resumeAssembledBps = iniFile.getIntValue(section, "ResumeAssembledBps", settings.resumeAssembledBps);
    settings.resumeAdvantageBps = iniFile.getIntValue(section, "ResumeAdvantageBps", settings.resumeAdvantageBps);
    settings.stabiliseMs = iniFile.getIntValue(section, "StabiliseMs", settings.stabiliseMs);
    settings.minWithdrawMs = iniFile.getIntValue(section, "MinWithdrawMs", settings.minWithdrawMs);
    settings.maxWithdrawMs = iniFile.getIntValue(section, "MaxWithdrawMs", settings.maxWithdrawMs);
    settings.maxRecoverMs = iniFile.getIntValue(section, "MaxRecoverMs", settings.maxRecoverMs);
    settings.outnumberedBps = iniFile.getIntValue(section, "OutnumberedBps", settings.outnumberedBps);
    settings.dispatchBypassBps = iniFile.getIntValue(section, "DispatchBypassBps", settings.dispatchBypassBps);
    settings.recallOrdersPerPass = iniFile.getIntValue(section, "RecallOrdersPerPass", settings.recallOrdersPerPass);
    settings.frontBatteriesEnabled = iniFile.getBoolValue(section, "FrontBatteriesEnabled", settings.frontBatteriesEnabled);
    settings.withdrawalsEnabled = iniFile.getBoolValue(section, "WithdrawalsEnabled", settings.withdrawalsEnabled);
}

// Helper function to save difficulty settings to INI
static void saveDifficultySettings(INIFile& iniFile, const std::string& section, const std::string& prefix, 
                                   const QuantBotConfig::DifficultySettings& settings) {
    iniFile.setBoolValue(section, prefix + "_AttackEnabled", settings.attackEnabled);
    iniFile.setDoubleValue(section, prefix + "_AttackThresholdPercent", settings.attackThresholdPercent);
    iniFile.setBoolValue(section, prefix + "_OrnithopterAttackEnabled", settings.ornithopterAttackEnabled);
    iniFile.setIntValue(section, prefix + "_OrnithopterAttackThreshold", settings.ornithopterAttackThreshold);
    iniFile.setDoubleValue(section, prefix + "_AttackForceMilitaryValueRatio", settings.attackForceMilitaryValueRatio);
    iniFile.setDoubleValue(section, prefix + "_MilitaryValueMultiplier", settings.militaryValueMultiplier);
    iniFile.setIntValue(section, prefix + "_MilitaryValueLimitSmallMap", settings.militaryValueLimitCustomSmallMap);
    iniFile.setIntValue(section, prefix + "_MilitaryValueLimitMediumMap", settings.militaryValueLimitCustomMediumMap);
    iniFile.setIntValue(section, prefix + "_MilitaryValueLimitLargeMap", settings.militaryValueLimitCustomLargeMap);
    iniFile.setIntValue(section, prefix + "_MilitaryValueLimitHugeMap", settings.militaryValueLimitCustomHugeMap);
    iniFile.setIntValue(section, prefix + "_HarvesterLimitMultiplier", settings.harvesterLimitPerRefineryMultiplier);
    iniFile.setIntValue(section, prefix + "_RefineryMinimum", settings.refineryMinimum);
    iniFile.setIntValue(section, prefix + "_HarvesterLimitSmallMap", settings.harvesterLimitCustomSmallMap);
    iniFile.setIntValue(section, prefix + "_HarvesterLimitMediumMap", settings.harvesterLimitCustomMediumMap);
    iniFile.setIntValue(section, prefix + "_HarvesterLimitLargeMap", settings.harvesterLimitCustomLargeMap);
    iniFile.setIntValue(section, prefix + "_HarvesterLimitHugeMap", settings.harvesterLimitCustomHugeMap);
    iniFile.setIntValue(section, prefix + "_StructureDefenders", settings.structureDefenders);
    iniFile.setIntValue(section, prefix + "_HarvesterDefenders", settings.harvesterDefenders);
}

// Helper function to load difficulty settings from INI
static void loadDifficultySettings(const INIFile& iniFile, const std::string& section, 
                                   const std::string& prefix, QuantBotConfig::DifficultySettings& settings) {
    settings.attackEnabled = iniFile.getBoolValue(section, prefix + "_AttackEnabled", settings.attackEnabled);
    settings.attackThresholdPercent = static_cast<float>(iniFile.getDoubleValue(section, prefix + "_AttackThresholdPercent", settings.attackThresholdPercent));
    settings.ornithopterAttackEnabled = iniFile.getBoolValue(section, prefix + "_OrnithopterAttackEnabled", settings.ornithopterAttackEnabled);
    settings.ornithopterAttackThreshold = iniFile.getIntValue(section, prefix + "_OrnithopterAttackThreshold", settings.ornithopterAttackThreshold);
    settings.attackForceMilitaryValueRatio = static_cast<float>(iniFile.getDoubleValue(section, prefix + "_AttackForceMilitaryValueRatio", settings.attackForceMilitaryValueRatio));
    settings.militaryValueMultiplier = static_cast<float>(iniFile.getDoubleValue(section, prefix + "_MilitaryValueMultiplier", settings.militaryValueMultiplier));
    settings.militaryValueLimitCustomSmallMap = iniFile.getIntValue(section, prefix + "_MilitaryValueLimitSmallMap", settings.militaryValueLimitCustomSmallMap);
    settings.militaryValueLimitCustomMediumMap = iniFile.getIntValue(section, prefix + "_MilitaryValueLimitMediumMap", settings.militaryValueLimitCustomMediumMap);
    settings.militaryValueLimitCustomLargeMap = iniFile.getIntValue(section, prefix + "_MilitaryValueLimitLargeMap", settings.militaryValueLimitCustomLargeMap);
    settings.militaryValueLimitCustomHugeMap = iniFile.getIntValue(section, prefix + "_MilitaryValueLimitHugeMap", settings.militaryValueLimitCustomHugeMap);
    settings.harvesterLimitPerRefineryMultiplier = iniFile.getIntValue(section, prefix + "_HarvesterLimitMultiplier", settings.harvesterLimitPerRefineryMultiplier);
    settings.refineryMinimum = iniFile.getIntValue(section, prefix + "_RefineryMinimum", settings.refineryMinimum);
    settings.harvesterLimitCustomSmallMap = iniFile.getIntValue(section, prefix + "_HarvesterLimitSmallMap", settings.harvesterLimitCustomSmallMap);
    settings.harvesterLimitCustomMediumMap = iniFile.getIntValue(section, prefix + "_HarvesterLimitMediumMap", settings.harvesterLimitCustomMediumMap);
    settings.harvesterLimitCustomLargeMap = iniFile.getIntValue(section, prefix + "_HarvesterLimitLargeMap", settings.harvesterLimitCustomLargeMap);
    settings.harvesterLimitCustomHugeMap = iniFile.getIntValue(section, prefix + "_HarvesterLimitHugeMap", settings.harvesterLimitCustomHugeMap);
    settings.structureDefenders = iniFile.getIntValue(section, prefix + "_StructureDefenders", settings.structureDefenders);
    settings.harvesterDefenders = iniFile.getIntValue(section, prefix + "_HarvesterDefenders", settings.harvesterDefenders);
}

// Helper function to save unit ratios to INI
static void saveUnitRatios(INIFile& iniFile, const std::string& section, const std::string& prefix, 
                          const QuantBotConfig::UnitRatios& ratios) {
    iniFile.setDoubleValue(section, prefix + "_Tank", ratios.tank);
    iniFile.setDoubleValue(section, prefix + "_SiegeTank", ratios.siegeTank);
    iniFile.setDoubleValue(section, prefix + "_Launcher", ratios.launcher);
    iniFile.setDoubleValue(section, prefix + "_Special", ratios.special);
    iniFile.setDoubleValue(section, prefix + "_Ornithopter", ratios.ornithopter);
}

// Helper function to load unit ratios from INI
static void loadUnitRatios(const INIFile& iniFile, const std::string& section, 
                          const std::string& prefix, QuantBotConfig::UnitRatios& ratios) {
    ratios.tank = static_cast<float>(iniFile.getDoubleValue(section, prefix + "_Tank", ratios.tank));
    ratios.siegeTank = static_cast<float>(iniFile.getDoubleValue(section, prefix + "_SiegeTank", ratios.siegeTank));
    ratios.launcher = static_cast<float>(iniFile.getDoubleValue(section, prefix + "_Launcher", ratios.launcher));
    ratios.special = static_cast<float>(iniFile.getDoubleValue(section, prefix + "_Special", ratios.special));
    ratios.ornithopter = static_cast<float>(iniFile.getDoubleValue(section, prefix + "_Ornithopter", ratios.ornithopter));
}

std::string QuantBotConfig::normalizeKey(const std::string& value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return result;
}

int QuantBotConfig::parseWaveFormation(const std::string& name, int fallback) {
    auto compact = [](const std::string& value) {
        std::string key = normalizeKey(value);
        key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c) { return std::isspace(c); }), key.end());
        return key;
    };
    const std::string key = compact(name);
    if (key == "NONE" || key == "OFF") return kWaveFormationNone;
    if (key == "BLOB") return kWaveFormationBlob;
    for (const FormationPlanner::Shape shape : FormationPlanner::kShapes)
        if (key == compact(FormationPlanner::shapeName(shape))) return static_cast<int>(shape);
    return fallback;
}

const char* QuantBotConfig::waveFormationName(int value) {
    if (value == kWaveFormationBlob) return "Blob";
    for (const FormationPlanner::Shape shape : FormationPlanner::kShapes)
        if (static_cast<int>(shape) == value) return FormationPlanner::shapeName(shape);
    return "None";
}

void QuantBotConfig::registerStructurePriority(int structureID, const std::string& name, int build, int target) {
    structurePriorities[name] = TargetPriority{build, target};
    normalizedStructureNames[normalizeKey(name)] = name;
    if (structureID >= Structure_FirstID && structureID <= Structure_LastID) {
        structureIdToName[structureID] = name;
    }
}

void QuantBotConfig::registerUnitPriority(int unitID, const std::string& name, int build, int target) {
    unitPriorities[name] = TargetPriority{build, target};
    normalizedUnitNames[normalizeKey(name)] = name;
    if (unitID >= Unit_FirstID && unitID <= Unit_LastID) {
        unitIdToName[unitID] = name;
    }
}

static void savePriorities(INIFile& iniFile,
                           const std::string& section,
                           const std::map<std::string, QuantBotConfig::TargetPriority>& priorities) {
    for (const auto& entry : priorities) {
        const std::string& key = entry.first;
        const auto& values = entry.second;
        iniFile.setIntValue(section, key + "_Build", values.build);
        iniFile.setIntValue(section, key + "_Target", values.target);
    }
}

static void loadPriorities(const INIFile& iniFile,
                           const std::string& section,
                           std::map<std::string, QuantBotConfig::TargetPriority>& priorities,
                           std::unordered_map<std::string, std::string>& lookup,
                           bool& changed) {
    std::unordered_set<std::string> seenNames;

    if (!iniFile.hasSection(section)) {
        changed = true;
        return;
    }

    for (auto it = iniFile.begin(section); it != iniFile.end(section); ++it) {
        std::string keyName = it->getKeyName();
        int value = it->getIntValue(0);

        const size_t sep = keyName.find_last_of('_');
        if (sep == std::string::npos) {
            continue;
        }

        std::string entryName = keyName.substr(0, sep);
        std::string entryField = keyName.substr(sep + 1);

        std::string normalizedName = QuantBotConfig::normalizeKey(entryName);
        auto lookupIt = lookup.find(normalizedName);
        std::string displayName;

        if (lookupIt != lookup.end()) {
            displayName = lookupIt->second;
        } else {
            displayName = entryName;
            lookup[normalizedName] = displayName;
            priorities[displayName] = QuantBotConfig::TargetPriority{};
            changed = true;
        }

        seenNames.insert(displayName);
        auto& priority = priorities[displayName];

        std::string normalizedField = QuantBotConfig::normalizeKey(entryField);
        if (normalizedField == "BUILD") {
            priority.build = value;
        } else if (normalizedField == "TARGET") {
            priority.target = value;
        }
    }

    for (const auto& entry : priorities) {
        if (seenNames.find(entry.first) == seenNames.end()) {
            changed = true;
            break;
        }
    }
}

bool QuantBotConfig::save(const std::string& filepath) const {
    try {
        INIFile iniFile(true, "QuantBot AI Configuration");
        
        // === DIFFICULTY SETTINGS ===
        saveDifficultySettings(iniFile, "Difficulty Settings", "Defend", defend);
        saveDifficultySettings(iniFile, "Difficulty Settings", "Easy", easy);
        saveDifficultySettings(iniFile, "Difficulty Settings", "Medium", medium);
        saveDifficultySettings(iniFile, "Difficulty Settings", "Hard", hard);
        saveDifficultySettings(iniFile, "Difficulty Settings", "Brutal", brutal);
        
        // === UNIT RATIOS (single set for all difficulties) ===
        saveUnitRatios(iniFile, "Unit Ratios", "Atreides", unitRatios.atreides);
        saveUnitRatios(iniFile, "Unit Ratios", "Harkonnen", unitRatios.harkonnen);
        saveUnitRatios(iniFile, "Unit Ratios", "Ordos", unitRatios.ordos);
        saveUnitRatios(iniFile, "Unit Ratios", "Fremen", unitRatios.fremen);
        saveUnitRatios(iniFile, "Unit Ratios", "Sardaukar", unitRatios.sardaukar);
        saveUnitRatios(iniFile, "Unit Ratios", "Mercenary", unitRatios.mercenary);
        
        // === ARMY RECOVERY AND FRONT BATTERIES ===
        saveRecoverySettings(iniFile, "Army Recovery", recovery);

        // === GENERAL BEHAVIOR ===
        iniFile.setIntValue("General Behavior", "AttackTimerMs", attackTimerMs);
        iniFile.setDoubleValue("General Behavior", "AttackThresholdPercent", attackThresholdPercent);
        iniFile.setIntValue("General Behavior", "MinMoneyForProduction", minMoneyForProduction);
        iniFile.setStringValue("General Behavior", "WaveFormation", waveFormationName(waveFormation));
        iniFile.setBoolValue("General Behavior", "StrategicSiteGuards", strategicSiteGuards);
        iniFile.setBoolValue("General Behavior", "PoliceFundingRecovery", policeFundingRecovery);
        iniFile.setBoolValue("General Behavior", "DeviatedHarvesterEarlyReturn", deviatedHarvesterEarlyReturn);
        iniFile.setBoolValue("Civ Wars", "Strategy", civWarsStrategy);

        // === FORCE CONCENTRATION ===
        iniFile.setBoolValue("Force Concentration", "FocusFire", force.focusFire);
        iniFile.setIntValue("Force Concentration", "FocusFireLevel", force.focusFireLevel);
        iniFile.setIntValue("Force Concentration", "TargetPriorityLevel", force.targetPriorityLevel);
        iniFile.setBoolValue("Force Concentration", "Converge", force.converge);
        iniFile.setIntValue("Force Concentration", "WaveReadinessPercent", force.waveReadinessPercent);
        iniFile.setBoolValue("Force Concentration", "Bundle", force.bundle);
        iniFile.setBoolValue("Force Concentration", "LaunchFromBundle", force.launchFromBundle);
        iniFile.setBoolValue("Force Concentration", "PerimeterTurrets", force.perimeterTurrets);
        iniFile.setBoolValue("Force Concentration", "SizedDefence", force.sizedDefence);
        iniFile.setBoolValue("Force Concentration", "OpeningReadiness", force.openingReadiness);
        iniFile.setBoolValue("Force Concentration", "TurretPriority", force.turretPriority);
        iniFile.setBoolValue("Force Concentration", "PressOn", force.pressOn);
        iniFile.setBoolValue("Force Concentration", "FinishWeakest", force.finishWeakest);
        iniFile.setBoolValue("Force Concentration", "FortifiedWaves", force.fortifiedWaves);
        iniFile.setBoolValue("Force Concentration", "SonicClearLane", force.sonicClearLane);
        iniFile.setBoolValue("Force Concentration", "SpendAtCap", force.spendAtCap);
        iniFile.setBoolValue("Force Concentration", "SiegeStandoff", force.siegeStandoff);
        iniFile.setBoolValue("Force Concentration", "MassAssault", force.massAssault);
        iniFile.setBoolValue("Force Concentration", "FinishWhenLeading", force.finishWhenLeading);
        iniFile.setBoolValue("Force Concentration", "VanillaFfaWeakestTarget", force.vanillaFfaWeakestTarget);
        iniFile.setBoolValue("Force Concentration", "VanillaFfaNearbyTarget", force.vanillaFfaNearbyTarget);
        iniFile.setBoolValue("Force Concentration", "VanillaFfaReserve", force.vanillaFfaReserve);
        iniFile.setBoolValue("Force Concentration", "VanillaFfaFullWaves", force.vanillaFfaFullWaves);
        iniFile.setBoolValue("Force Concentration", "VanillaFfaStarportAfterWave", force.vanillaFfaStarportAfterWave);
        iniFile.setIntValue("Force Concentration", "StarportBuying", force.starportBuying);
        iniFile.setIntValue("Force Concentration", "StarportCashStockpile", force.starportCashStockpile);
        iniFile.setIntValue("Force Concentration", "StarportEconomyPriority", force.starportEconomyPriority);
        iniFile.setBoolValue("Force Concentration", "VanillaFfaThreatProduction", force.vanillaFfaThreatProduction);
        iniFile.setBoolValue("Force Concentration", "VanillaFfaSingleFront", force.vanillaFfaSingleFront);
        iniFile.setBoolValue("Force Concentration", "VanillaFfaLightWorkingCash", force.vanillaFfaLightWorkingCash);
        iniFile.setBoolValue("Force Concentration", "VanillaFfaFundedTech", force.vanillaFfaFundedTech);
        iniFile.setBoolValue("Force Concentration", "VanillaHarvesterBoom", force.vanillaHarvesterBoom);
        iniFile.setBoolValue("Force Concentration", "VanillaRefineryBoom", force.vanillaRefineryBoom);
        iniFile.setBoolValue("Force Concentration", "VanillaGreedyBoom", force.vanillaGreedyBoom);
        iniFile.setBoolValue("Force Concentration", "VanillaReactiveBoom", force.vanillaReactiveBoom);
        iniFile.setBoolValue("Force Concentration", "VanillaBoomFfaOnly", force.vanillaBoomFfaOnly);
        iniFile.setBoolValue("Force Concentration", "VanillaBoomSpiceGate", force.vanillaBoomSpiceGate);
        iniFile.setIntValue("Force Concentration", "VanillaBoomSpicePerRefinery", force.vanillaBoomSpicePerRefinery);
        iniFile.setIntValue("Force Concentration", "DuelOpeningPlan", force.duelOpeningPlan);
        iniFile.setIntValue("Force Concentration", "VanillaHarvesterBoomWorkers", force.vanillaHarvesterBoomWorkers);
        iniFile.setIntValue("Force Concentration", "VanillaRefineryBoomRefineries", force.vanillaRefineryBoomRefineries);
        iniFile.setIntValue("Force Concentration", "VanillaGreedyBoomMinutes", force.vanillaGreedyBoomMinutes);
        iniFile.setIntValue("Force Concentration", "VanillaReactiveArmyRatioBps", force.vanillaReactiveArmyRatioBps);
        iniFile.setIntValue("Force Concentration", "VanillaReactiveEconomyRatioBps", force.vanillaReactiveEconomyRatioBps);
        iniFile.setBoolValue("Force Concentration", "VanillaCoreDefence", force.vanillaCoreDefence);
        iniFile.setIntValue("Force Concentration", "VanillaCoreDefenceThreatBps", force.vanillaCoreDefenceThreatBps);
        iniFile.setBoolValue("Force Concentration", "VanillaArmyCounters", force.vanillaArmyCounters);
        iniFile.setIntValue("Force Concentration", "VanillaArmyCounterBlendBps", force.vanillaArmyCounterBlendBps);
        iniFile.setBoolValue("Force Concentration", "EconomicFleetRatios", force.economicFleetRatios);
        iniFile.setIntValue("Force Concentration", "FleetHarvestersPerBay", force.fleetHarvestersPerBay);
        iniFile.setIntValue("Force Concentration", "FleetCarryallQuarters", force.fleetCarryallQuarters);
        iniFile.setIntValue("Force Concentration", "FleetRepairYards", force.fleetRepairYards);
        iniFile.setBoolValue("Force Concentration", "RepairCapacityDemand", force.repairCapacityDemand);
        iniFile.setIntValue("Force Concentration", "FfaVultureLevel", force.ffaVultureLevel);
        iniFile.setIntValue("Force Concentration", "TechIncomeTiming", force.techIncomeTiming);
        iniFile.setBoolValue("Force Concentration", "JoinAllyWaves", force.joinAllyWaves);
        iniFile.setBoolValue("Force Concentration", "TeamSynchroniseAttacks", force.teamSynchroniseAttacks);
        iniFile.setIntValue("Force Concentration", "TeamDefenceSharePercent", force.teamDefenceSharePercent);
        iniFile.setBoolValue("Force Concentration", "OpeningCombatBudget", force.openingCombatBudget);
        iniFile.setBoolValue("Force Concentration", "CampaignCombatBudget", force.campaignCombatBudget);
        iniFile.setBoolValue("Force Concentration", "SingleMissionCampaignPolicy", force.singleMissionCampaignPolicy);
        iniFile.setBoolValue("Force Concentration", "CampaignPlayerHouse", force.campaignPlayerHouse);
        iniFile.setBoolValue("Force Concentration", "CampaignMilitaryOpening", force.campaignMilitaryOpening);
        iniFile.setBoolValue("Force Concentration", "CampaignAdaptiveReadiness", force.campaignAdaptiveReadiness);
        iniFile.setBoolValue("Force Concentration", "CampaignAllyStrengthFloor", force.campaignAllyStrengthFloor);
        iniFile.setIntValue("Force Concentration", "CampaignEasyHarvesters", force.campaignEasyHarvesters);
        iniFile.setIntValue("Force Concentration", "CampaignMediumHarvesters", force.campaignMediumHarvesters);
        iniFile.setIntValue("Force Concentration", "CampaignHardHarvesters", force.campaignHardHarvesters);
        iniFile.setIntValue("Force Concentration", "CampaignEasyWaveValue", force.campaignEasyWaveValue);
        iniFile.setIntValue("Force Concentration", "CampaignMediumWaveValue", force.campaignMediumWaveValue);
        iniFile.setIntValue("Force Concentration", "CampaignHardWaveValue", force.campaignHardWaveValue);
        iniFile.setIntValue("Force Concentration", "CampaignEasyOpeningDelaySec", force.campaignEasyOpeningDelaySec);
        iniFile.setIntValue("Force Concentration", "CampaignMediumOpeningDelaySec", force.campaignMediumOpeningDelaySec);
        iniFile.setIntValue("Force Concentration", "CampaignHardOpeningDelaySec", force.campaignHardOpeningDelaySec);
        iniFile.setBoolValue("Force Concentration", "CampaignThreatProduction", force.campaignThreatProduction);
        iniFile.setBoolValue("Force Concentration", "CampaignFundedLightOpening", force.campaignFundedLightOpening);
        iniFile.setBoolValue("Force Concentration", "CampaignLightWorkingCash", force.campaignLightWorkingCash);
        iniFile.setBoolValue("Force Concentration", "CampaignLightIncomeSeed", force.campaignLightIncomeSeed);
        iniFile.setBoolValue("Force Concentration", "CampaignFactoryBeforeStarport", force.campaignFactoryBeforeStarport);
        iniFile.setBoolValue("Force Concentration", "CampaignIdleCashFactory", force.campaignIdleCashFactory);
        iniFile.setIntValue("Force Concentration", "CampaignIdleCashFactoryCredits", force.campaignIdleCashFactoryCredits);
        iniFile.setBoolValue("Force Concentration", "CampaignRefineryDefence", force.campaignRefineryDefence);
        iniFile.setBoolValue("Force Concentration", "CampaignFactoryRunway", force.campaignFactoryRunway);
        iniFile.setIntValue("Force Concentration", "CampaignHomeFields", force.campaignHomeFields);
        iniFile.setIntValue("Force Concentration", "CampaignSizedWorkerRescue", force.campaignSizedWorkerRescue);
        iniFile.setBoolValue("Force Concentration", "CapacityMcvRunway", force.capacityMcvRunway);
        iniFile.setBoolValue("Force Concentration", "RespectServiceOwnership", force.respectServiceOwnership);
        iniFile.setBoolValue("Force Concentration", "ClearMcvFootprint", force.clearMcvFootprint);
        iniFile.setBoolValue("Force Concentration", "SmallMapYardCap", force.smallMapYardCap);
        iniFile.setBoolValue("Force Concentration", "DeathHandTurrets", force.deathHandTurrets);
        iniFile.setIntValue("Force Concentration", "PalaceValueTarget", force.palaceValueTarget);
        iniFile.setIntValue("Force Concentration", "PalaceWaveTiming", force.palaceWaveTiming);
        iniFile.setBoolValue("Force Concentration", "SpreadVsDeathHand", force.spreadVsDeathHand);
        iniFile.setBoolValue("Force Concentration", "DefencePrerequisites", force.defencePrerequisites);
        iniFile.setBoolValue("Force Concentration", "HardCityMatch", force.hardCityMatch);
        iniFile.setBoolValue("Force Concentration", "ResearchLab", force.researchLab);
        iniFile.setBoolValue("Force Concentration", "ResearchLabProgression", force.researchLabProgression);
        iniFile.setBoolValue("Force Concentration", "CityGrowthPower", force.cityGrowthPower);
        iniFile.setBoolValue("Force Concentration", "CityPowerSizing", force.cityPowerSizing);
        iniFile.setBoolValue("Force Concentration", "CityOpeningCapital", force.cityOpeningCapital);
        iniFile.setBoolValue("Force Concentration", "CityConstructionCapital", force.cityConstructionCapital);
        iniFile.setBoolValue("Force Concentration", "CityProductionPrerequisites", force.cityProductionPrerequisites);
        iniFile.setBoolValue("Force Concentration", "CityFirstFactorySaving", force.cityFirstFactorySaving);
        iniFile.setBoolValue("Force Concentration", "CityThreatBudget", force.cityThreatBudget);
        iniFile.setBoolValue("Force Concentration", "CityZoneDefence", force.cityZoneDefence);
        iniFile.setBoolValue("Force Concentration", "CityAdvancedTech", force.cityAdvancedTech);
        iniFile.setBoolValue("Force Concentration", "CityContinuousResearch", force.cityContinuousResearch);
        iniFile.setIntValue("Force Concentration", "CityResearchOrder", force.cityResearchOrder);
        iniFile.setBoolValue("Force Concentration", "CitySpiceTransition", force.citySpiceTransition);
        iniFile.setIntValue("Force Concentration", "SpiceFieldExpansion", force.spiceFieldExpansion);
        iniFile.setBoolValue("Force Concentration", "CitySpiceCapacityRunway", force.citySpiceCapacityRunway);
        iniFile.setBoolValue("Force Concentration", "CitySingleYardGrowth", force.citySingleYardGrowth);
        iniFile.setBoolValue("Force Concentration", "CityPowerGrowthHorizon", force.cityPowerGrowthHorizon);
        iniFile.setBoolValue("Force Concentration", "CityBoomWorkers", force.cityBoomWorkers);
        iniFile.setBoolValue("Force Concentration", "CityBoomRefineries", force.cityBoomRefineries);
        iniFile.setBoolValue("Force Concentration", "CityBoomGreedy", force.cityBoomGreedy);
        iniFile.setBoolValue("Force Concentration", "CityBoomShortTax", force.cityBoomShortTax);
        iniFile.setBoolValue("Force Concentration", "CityBoomReactive", force.cityBoomReactive);
        iniFile.setIntValue("Force Concentration", "CityBoomWorkerGoal", force.cityBoomWorkerGoal);
        iniFile.setIntValue("Force Concentration", "CityBoomRefineryGoal", force.cityBoomRefineryGoal);
        iniFile.setIntValue("Force Concentration", "CityBoomGreedyMinutes", force.cityBoomGreedyMinutes);
        iniFile.setIntValue("Force Concentration", "CityBoomTaxMinutes", force.cityBoomTaxMinutes);
        iniFile.setIntValue("Force Concentration", "CityBoomArmyRatioBps", force.cityBoomArmyRatioBps);
        iniFile.setIntValue("Force Concentration", "CityBoomEconomyRatioBps", force.cityBoomEconomyRatioBps);
        iniFile.setBoolValue("Force Concentration", "CityBoomSpiceGate", force.cityBoomSpiceGate);
        iniFile.setIntValue("Force Concentration", "CityBoomPaybackMinutes", force.cityBoomPaybackMinutes);
        iniFile.setBoolValue("Force Concentration", "CityHarvestFieldChoice", force.cityHarvestFieldChoice);
        iniFile.setIntValue("Force Concentration", "CityHarvestFieldSafetyTiles", force.cityHarvestFieldSafetyTiles);
        iniFile.setBoolValue("Force Concentration", "CityRefineryLogistics", force.cityRefineryLogistics);
        iniFile.setIntValue("Force Concentration", "CityRefinerySiteTiles", force.cityRefinerySiteTiles);
        iniFile.setBoolValue("Force Concentration", "CityHarvesterReplacement", force.cityHarvesterReplacement);
        iniFile.setIntValue("Force Concentration", "CityHarvesterReplacementMinutes", force.cityHarvesterReplacementMinutes);
        iniFile.setBoolValue("Force Concentration", "CityLandValueServices", force.cityLandValueServices);
        iniFile.setIntValue("Force Concentration", "CityLandValueFundingPercent", force.cityLandValueFundingPercent);
        iniFile.setBoolValue("Force Concentration", "CityGrowthStageTax", force.cityGrowthStageTax);
        iniFile.setIntValue("Force Concentration", "CityGrowthTaxPopulation", force.cityGrowthTaxPopulation);
        iniFile.setIntValue("Force Concentration", "CityCapitalPacing", force.cityCapitalPacing);
        iniFile.setIntValue("Force Concentration", "CityFleetRatios", force.cityFleetRatios);
        iniFile.setIntValue("Force Concentration", "CityTechRefineries", force.cityTechRefineries);
        iniFile.setIntValue("Force Concentration", "CityYardRefineries", force.cityYardRefineries);
        iniFile.setIntValue("Force Concentration", "CityWorkerReplacement", force.cityWorkerReplacement);
        iniFile.setIntValue("Force Concentration", "ClosingMountainScope", force.closingMountainScope);
        iniFile.setIntValue("Force Concentration", "ConvergeChokepointScope", force.convergeChokepointScope);
        iniFile.setIntValue("Force Concentration", "OpeningOpenMapScope", force.openingOpenMapScope);
        iniFile.setIntValue("Force Concentration", "VanillaArmyCounterMapScope", force.vanillaArmyCounterMapScope);
        iniFile.setIntValue("Force Concentration", "FocusFireMapScope", force.focusFireMapScope);
        iniFile.setIntValue("Force Concentration", "SpiceBloomHarvest", force.spiceBloomHarvest);
        iniFile.setBoolValue("Force Concentration", "RetreatPreserve", force.retreatPreserve);
        iniFile.setIntValue("Force Concentration", "RetreatPreserveLevel", force.retreatPreserveLevel);
        iniFile.setBoolValue("Force Concentration", "HarvesterRaid", force.harvesterRaid);
        iniFile.setIntValue("Force Concentration", "HarvesterRaidLevel", force.harvesterRaidLevel);
        iniFile.setBoolValue("Force Concentration", "ThreatTurrets", force.threatTurrets);
        iniFile.setBoolValue("Force Concentration", "MoreRocketTurrets", force.moreRocketTurrets);
        iniFile.setBoolValue("Force Concentration", "CityStarportSupply", force.cityStarportSupply);
        iniFile.setBoolValue("Force Concentration", "StorageCapacity", force.storageCapacity);
        iniFile.setIntValue("Force Concentration", "PowerLeadBuildings", force.powerLeadBuildings);
        iniFile.setIntValue("Force Concentration", "SiloFillPercent", force.siloFillPercent);
        iniFile.setBoolValue("Force Concentration", "FundedClassicProduction", force.fundedClassicProduction);
        iniFile.setBoolValue("Force Concentration", "TrooperSupport", force.trooperSupport);
        iniFile.setBoolValue("Force Concentration", "SimpleTechSavings", force.simpleTechSavings);
        iniFile.setIntValue("Force Concentration", "LauncherKiteExtraDistance", force.launcherKiteExtraDistance);
        iniFile.setBoolValue("Force Concentration", "ExhaustedArmyDispatch", force.exhaustedArmyDispatch);
        iniFile.setIntValue("Force Concentration", "MediumClassic", force.mediumClassic);

        // === TARGET PRIORITIES ===
        savePriorities(iniFile, "Structure Priorities", structurePriorities);
        savePriorities(iniFile, "Unit Priorities", unitPriorities);
        
        // Save to file
        if (!iniFile.saveChangesTo(filepath)) {
            SDL_Log("Warning: Failed to save QuantBot config to %s", filepath.c_str());
            return false;
        }
        
        SDL_Log("QuantBot config saved to: %s", filepath.c_str());
        return true;
        
    } catch (std::exception& e) {
        SDL_Log("Error saving QuantBot config: %s", e.what());
        return false;
    }
}

bool QuantBotConfig::load(const std::string& filepath) {
    try {
        // Missing priorities still use their in-memory defaults, but loading
        // must not change a pinned revision or an integrity-checked family
        // bundle (including its Vanilla fallback). Draft/user configs retain
        // the historical automatic upgrade.
        const auto folder = std::filesystem::path(filepath).parent_path();
        const auto metadata = folder / "workshop-revision.ini";
        const auto& mods = ModManager::instance();
        const bool immutable = folder.filename().string().rfind("ws-", 0) == 0
            || (existsFile(metadata.string()) && INIFile(metadata.string()).getBoolValue("Workshop", "Immutable", false));
        const bool familyBundle = mods.isTornieFamilyContentActive()
            && (folder == std::filesystem::path(mods.getModPath(mods.getContentBase(mods.getActiveModName())))
                || folder == std::filesystem::path(mods.getModPath("vanilla")));
        const bool readOnly = immutable || familyBundle;
        // Try to load from user directory, create defaults if not found
        if (!existsFile(filepath)) {
            SDL_Log("QuantBot config not found: %s", filepath.c_str());
            if (readOnly) return false;
            SDL_Log("Creating default configuration");
            return save(filepath);
        }
        
        INIFile iniFile(filepath);
        if (readFrom(iniFile) && !readOnly) {
            save(filepath);
        }
        
        SDL_Log("QuantBot config loaded from: %s", filepath.c_str());
        return true;
        
    } catch (std::exception& e) {
        SDL_Log("Error loading QuantBot config: %s", e.what());
        SDL_Log("Using default QuantBot configuration");
        return false;
    }
}

bool QuantBotConfig::applyOverrides(const std::string& filepath) {
    if (!existsFile(filepath)) return false;
    INIFile iniFile(filepath);
    readFrom(iniFile);
    return true;
}

bool QuantBotConfig::readFrom(const INIFile& iniFile) {
    // === LOAD DIFFICULTY SETTINGS ===
    loadDifficultySettings(iniFile, "Difficulty Settings", "Defend", defend);
    loadDifficultySettings(iniFile, "Difficulty Settings", "Easy", easy);
    loadDifficultySettings(iniFile, "Difficulty Settings", "Medium", medium);
    loadDifficultySettings(iniFile, "Difficulty Settings", "Hard", hard);
    loadDifficultySettings(iniFile, "Difficulty Settings", "Brutal", brutal);
    
    // === UNIT RATIOS (single set for all difficulties) ===
    loadUnitRatios(iniFile, "Unit Ratios", "Atreides", unitRatios.atreides);
    loadUnitRatios(iniFile, "Unit Ratios", "Harkonnen", unitRatios.harkonnen);
    loadUnitRatios(iniFile, "Unit Ratios", "Ordos", unitRatios.ordos);
    loadUnitRatios(iniFile, "Unit Ratios", "Fremen", unitRatios.fremen);
    loadUnitRatios(iniFile, "Unit Ratios", "Sardaukar", unitRatios.sardaukar);
    loadUnitRatios(iniFile, "Unit Ratios", "Mercenary", unitRatios.mercenary);
    
    // === LOAD ARMY RECOVERY AND FRONT BATTERIES ===
    loadRecoverySettings(iniFile, "Army Recovery", recovery);

    // === LOAD GENERAL BEHAVIOR ===
    attackTimerMs = iniFile.getIntValue("General Behavior", "AttackTimerMs", attackTimerMs);
    attackThresholdPercent = static_cast<float>(iniFile.getDoubleValue("General Behavior", "AttackThresholdPercent", attackThresholdPercent));
    minMoneyForProduction = iniFile.getIntValue("General Behavior", "MinMoneyForProduction", minMoneyForProduction);
    strategicSiteGuards = iniFile.getBoolValue("General Behavior", "StrategicSiteGuards", strategicSiteGuards);
    policeFundingRecovery = iniFile.getBoolValue("General Behavior", "PoliceFundingRecovery", policeFundingRecovery);
    deviatedHarvesterEarlyReturn = iniFile.getBoolValue("General Behavior", "DeviatedHarvesterEarlyReturn", deviatedHarvesterEarlyReturn);
    {
        const std::string shape = iniFile.getStringValue("General Behavior", "WaveFormation", waveFormationName(waveFormation));
        const int parsed = parseWaveFormation(shape, INT_MIN);
        if (parsed == INT_MIN) SDL_Log("QuantBot config: unknown WaveFormation '%s', keeping %s", shape.c_str(), waveFormationName(waveFormation));
        else waveFormation = parsed;
    }
    force.focusFire = iniFile.getBoolValue("Force Concentration", "FocusFire", force.focusFire);
    force.focusFireLevel = std::clamp(iniFile.getIntValue("Force Concentration", "FocusFireLevel", force.focusFireLevel), 0, 2);
    force.targetPriorityLevel = std::clamp(iniFile.getIntValue("Force Concentration", "TargetPriorityLevel", force.targetPriorityLevel), 0, 2);
    force.converge = iniFile.getBoolValue("Force Concentration", "Converge", force.converge);
    force.waveReadinessPercent = std::clamp(
        iniFile.getIntValue("Force Concentration", "WaveReadinessPercent", force.waveReadinessPercent), 10, 300);
    force.bundle = iniFile.getBoolValue("Force Concentration", "Bundle", force.bundle);
    force.launchFromBundle = iniFile.getBoolValue("Force Concentration", "LaunchFromBundle", force.launchFromBundle);
    force.perimeterTurrets = iniFile.getBoolValue("Force Concentration", "PerimeterTurrets", force.perimeterTurrets);
    force.sizedDefence = iniFile.getBoolValue("Force Concentration", "SizedDefence", force.sizedDefence);
    force.openingReadiness = iniFile.getBoolValue("Force Concentration", "OpeningReadiness", force.openingReadiness);
    force.turretPriority = iniFile.getBoolValue("Force Concentration", "TurretPriority", force.turretPriority);
    force.pressOn = iniFile.getBoolValue("Force Concentration", "PressOn", force.pressOn);
    force.finishWeakest = iniFile.getBoolValue("Force Concentration", "FinishWeakest", force.finishWeakest);
    force.fortifiedWaves = iniFile.getBoolValue("Force Concentration", "FortifiedWaves", force.fortifiedWaves);
    force.sonicClearLane = iniFile.getBoolValue("Force Concentration", "SonicClearLane", force.sonicClearLane);
    force.spendAtCap = iniFile.getBoolValue("Force Concentration", "SpendAtCap", force.spendAtCap);
    force.siegeStandoff = iniFile.getBoolValue("Force Concentration", "SiegeStandoff", force.siegeStandoff);
    force.massAssault = iniFile.getBoolValue("Force Concentration", "MassAssault", force.massAssault);
    force.finishWhenLeading = iniFile.getBoolValue("Force Concentration", "FinishWhenLeading", force.finishWhenLeading);
    force.vanillaFfaWeakestTarget = iniFile.getBoolValue("Force Concentration", "VanillaFfaWeakestTarget", force.vanillaFfaWeakestTarget);
    force.vanillaFfaNearbyTarget = iniFile.getBoolValue("Force Concentration", "VanillaFfaNearbyTarget", force.vanillaFfaNearbyTarget);
    force.vanillaFfaReserve = iniFile.getBoolValue("Force Concentration", "VanillaFfaReserve", force.vanillaFfaReserve);
    force.vanillaFfaFullWaves = iniFile.getBoolValue("Force Concentration", "VanillaFfaFullWaves", force.vanillaFfaFullWaves);
    force.vanillaFfaStarportAfterWave = iniFile.getBoolValue("Force Concentration", "VanillaFfaStarportAfterWave", force.vanillaFfaStarportAfterWave);
    force.starportBuying = std::clamp(iniFile.getIntValue("Force Concentration", "StarportBuying", force.starportBuying), 0, 2);
    force.starportCashStockpile = std::clamp(iniFile.getIntValue("Force Concentration", "StarportCashStockpile", force.starportCashStockpile), 0, 2);
    force.starportEconomyPriority = std::clamp(iniFile.getIntValue("Force Concentration", "StarportEconomyPriority", force.starportEconomyPriority), 0, 2);
    force.vanillaFfaThreatProduction = iniFile.getBoolValue("Force Concentration", "VanillaFfaThreatProduction", force.vanillaFfaThreatProduction);
    force.vanillaFfaSingleFront = iniFile.getBoolValue("Force Concentration", "VanillaFfaSingleFront", force.vanillaFfaSingleFront);
    force.vanillaFfaLightWorkingCash = iniFile.getBoolValue("Force Concentration", "VanillaFfaLightWorkingCash", force.vanillaFfaLightWorkingCash);
    force.vanillaFfaFundedTech = iniFile.getBoolValue("Force Concentration", "VanillaFfaFundedTech", force.vanillaFfaFundedTech);
    force.vanillaHarvesterBoom = iniFile.getBoolValue("Force Concentration", "VanillaHarvesterBoom", force.vanillaHarvesterBoom);
    force.vanillaRefineryBoom = iniFile.getBoolValue("Force Concentration", "VanillaRefineryBoom", force.vanillaRefineryBoom);
    force.vanillaGreedyBoom = iniFile.getBoolValue("Force Concentration", "VanillaGreedyBoom", force.vanillaGreedyBoom);
    force.vanillaReactiveBoom = iniFile.getBoolValue("Force Concentration", "VanillaReactiveBoom", force.vanillaReactiveBoom);
    force.vanillaBoomFfaOnly = iniFile.getBoolValue("Force Concentration", "VanillaBoomFfaOnly", force.vanillaBoomFfaOnly);
    force.vanillaBoomSpiceGate = iniFile.getBoolValue("Force Concentration", "VanillaBoomSpiceGate", force.vanillaBoomSpiceGate);
    force.vanillaBoomSpicePerRefinery = QuantBotVanillaPolicy::spicePerRefineryLevel(iniFile.getIntValue(
        "Force Concentration", "VanillaBoomSpicePerRefinery", force.vanillaBoomSpicePerRefinery));
    force.duelOpeningPlan = std::clamp(iniFile.getIntValue(
        "Force Concentration", "DuelOpeningPlan", force.duelOpeningPlan), 0, 3);
    force.vanillaHarvesterBoomWorkers = std::clamp(iniFile.getIntValue(
        "Force Concentration", "VanillaHarvesterBoomWorkers", force.vanillaHarvesterBoomWorkers), 8, 12);
    force.vanillaRefineryBoomRefineries = std::clamp(iniFile.getIntValue(
        "Force Concentration", "VanillaRefineryBoomRefineries", force.vanillaRefineryBoomRefineries), 4, 6);
    force.vanillaGreedyBoomMinutes = std::clamp(iniFile.getIntValue(
        "Force Concentration", "VanillaGreedyBoomMinutes", force.vanillaGreedyBoomMinutes), 6, 12);
    force.vanillaReactiveArmyRatioBps = std::clamp(iniFile.getIntValue(
        "Force Concentration", "VanillaReactiveArmyRatioBps", force.vanillaReactiveArmyRatioBps), 6000, 9000);
    force.vanillaReactiveEconomyRatioBps = std::clamp(iniFile.getIntValue(
        "Force Concentration", "VanillaReactiveEconomyRatioBps", force.vanillaReactiveEconomyRatioBps), 12000, 15000);
    force.vanillaCoreDefence = iniFile.getBoolValue("Force Concentration", "VanillaCoreDefence", force.vanillaCoreDefence);
    // Exactly two preregistered intensities: any value other than 10000 is the 5000 level.
    force.vanillaCoreDefenceThreatBps = QuantBotCoreDefencePolicy::level(iniFile.getIntValue(
        "Force Concentration", "VanillaCoreDefenceThreatBps", force.vanillaCoreDefenceThreatBps));
    force.vanillaArmyCounters = iniFile.getBoolValue("Force Concentration", "VanillaArmyCounters", force.vanillaArmyCounters);
    force.vanillaArmyCounterBlendBps = QuantBotCoreDefencePolicy::level(iniFile.getIntValue(
        "Force Concentration", "VanillaArmyCounterBlendBps", force.vanillaArmyCounterBlendBps));
    force.economicFleetRatios = iniFile.getBoolValue("Force Concentration", "EconomicFleetRatios", force.economicFleetRatios);
    force.fleetHarvestersPerBay = std::clamp(iniFile.getIntValue(
        "Force Concentration", "FleetHarvestersPerBay", force.fleetHarvestersPerBay), 2, 4);
    force.fleetCarryallQuarters = std::clamp(iniFile.getIntValue(
        "Force Concentration", "FleetCarryallQuarters", force.fleetCarryallQuarters), 0, 2);
    force.fleetRepairYards = std::clamp(iniFile.getIntValue(
        "Force Concentration", "FleetRepairYards", force.fleetRepairYards), 0, 1);
    force.repairCapacityDemand = iniFile.getBoolValue(
        "Force Concentration", "RepairCapacityDemand", force.repairCapacityDemand);
    force.ffaVultureLevel = std::clamp(iniFile.getIntValue("Force Concentration", "FfaVultureLevel", force.ffaVultureLevel), 0, 2);
    force.techIncomeTiming = std::clamp(iniFile.getIntValue(
        "Force Concentration", "TechIncomeTiming", force.techIncomeTiming), 0, 2);
    force.joinAllyWaves = iniFile.getBoolValue("Force Concentration", "JoinAllyWaves", force.joinAllyWaves);
    force.teamSynchroniseAttacks = iniFile.getBoolValue("Force Concentration", "TeamSynchroniseAttacks", force.teamSynchroniseAttacks);
    force.teamDefenceSharePercent = iniFile.getIntValue("Force Concentration", "TeamDefenceSharePercent", force.teamDefenceSharePercent);
    if (force.teamDefenceSharePercent != 25 && force.teamDefenceSharePercent != 50)
        force.teamDefenceSharePercent = 0;
    force.openingCombatBudget = iniFile.getBoolValue("Force Concentration", "OpeningCombatBudget", force.openingCombatBudget);
    force.campaignCombatBudget = iniFile.getBoolValue("Force Concentration", "CampaignCombatBudget", force.campaignCombatBudget);
    force.singleMissionCampaignPolicy = iniFile.getBoolValue(
        "Force Concentration", "SingleMissionCampaignPolicy", force.singleMissionCampaignPolicy);
    force.campaignPlayerHouse = iniFile.getBoolValue("Force Concentration", "CampaignPlayerHouse", force.campaignPlayerHouse);
    force.campaignMilitaryOpening = iniFile.getBoolValue("Force Concentration", "CampaignMilitaryOpening", force.campaignMilitaryOpening);
    force.campaignAdaptiveReadiness = iniFile.getBoolValue("Force Concentration", "CampaignAdaptiveReadiness", force.campaignAdaptiveReadiness);
    force.campaignAllyStrengthFloor = iniFile.getBoolValue("Force Concentration", "CampaignAllyStrengthFloor", force.campaignAllyStrengthFloor);
    // Literal keys, one per line: the genome inventory reads these statements.
    force.campaignEasyHarvesters = std::clamp(iniFile.getIntValue("Force Concentration", "CampaignEasyHarvesters", force.campaignEasyHarvesters), 0, 40);
    force.campaignMediumHarvesters = std::clamp(iniFile.getIntValue("Force Concentration", "CampaignMediumHarvesters", force.campaignMediumHarvesters), 0, 40);
    force.campaignHardHarvesters = std::clamp(iniFile.getIntValue("Force Concentration", "CampaignHardHarvesters", force.campaignHardHarvesters), 0, 40);
    force.campaignEasyWaveValue = std::clamp(iniFile.getIntValue("Force Concentration", "CampaignEasyWaveValue", force.campaignEasyWaveValue), 300, 100000);
    force.campaignMediumWaveValue = std::clamp(iniFile.getIntValue("Force Concentration", "CampaignMediumWaveValue", force.campaignMediumWaveValue), 300, 100000);
    force.campaignHardWaveValue = std::clamp(iniFile.getIntValue("Force Concentration", "CampaignHardWaveValue", force.campaignHardWaveValue), 300, 100000);
    force.campaignEasyOpeningDelaySec = std::clamp(iniFile.getIntValue("Force Concentration", "CampaignEasyOpeningDelaySec", force.campaignEasyOpeningDelaySec), 0, 1800);
    force.campaignMediumOpeningDelaySec = std::clamp(iniFile.getIntValue("Force Concentration", "CampaignMediumOpeningDelaySec", force.campaignMediumOpeningDelaySec), 0, 1800);
    force.campaignHardOpeningDelaySec = std::clamp(iniFile.getIntValue("Force Concentration", "CampaignHardOpeningDelaySec", force.campaignHardOpeningDelaySec), 0, 1800);
    force.campaignThreatProduction = iniFile.getBoolValue("Force Concentration", "CampaignThreatProduction", force.campaignThreatProduction);
    force.campaignFundedLightOpening = iniFile.getBoolValue("Force Concentration", "CampaignFundedLightOpening", force.campaignFundedLightOpening);
    force.campaignLightWorkingCash = iniFile.getBoolValue("Force Concentration", "CampaignLightWorkingCash", force.campaignLightWorkingCash);
    force.campaignLightIncomeSeed = iniFile.getBoolValue("Force Concentration", "CampaignLightIncomeSeed", force.campaignLightIncomeSeed);
    force.campaignFactoryBeforeStarport = iniFile.getBoolValue("Force Concentration", "CampaignFactoryBeforeStarport", force.campaignFactoryBeforeStarport);
    force.campaignIdleCashFactory = iniFile.getBoolValue("Force Concentration", "CampaignIdleCashFactory", force.campaignIdleCashFactory);
    force.campaignIdleCashFactoryCredits = std::clamp(
        iniFile.getIntValue("Force Concentration", "CampaignIdleCashFactoryCredits", force.campaignIdleCashFactoryCredits), 1500, 2000);
    force.campaignRefineryDefence = iniFile.getBoolValue("Force Concentration", "CampaignRefineryDefence", force.campaignRefineryDefence);
    force.campaignFactoryRunway = iniFile.getBoolValue("Force Concentration", "CampaignFactoryRunway", force.campaignFactoryRunway);
    force.campaignHomeFields = std::clamp(
        iniFile.getIntValue("Force Concentration", "CampaignHomeFields", force.campaignHomeFields), 0, 2);
    force.campaignSizedWorkerRescue = std::clamp(
        iniFile.getIntValue("Force Concentration", "CampaignSizedWorkerRescue", force.campaignSizedWorkerRescue), 0, 2);
    force.capacityMcvRunway = iniFile.getBoolValue("Force Concentration", "CapacityMcvRunway", force.capacityMcvRunway);
    force.respectServiceOwnership = iniFile.getBoolValue("Force Concentration", "RespectServiceOwnership", force.respectServiceOwnership);
    force.clearMcvFootprint = iniFile.getBoolValue("Force Concentration", "ClearMcvFootprint", force.clearMcvFootprint);
    force.smallMapYardCap = iniFile.getBoolValue("Force Concentration", "SmallMapYardCap", force.smallMapYardCap);
    force.deathHandTurrets = iniFile.getBoolValue("Force Concentration", "DeathHandTurrets", force.deathHandTurrets);
    force.palaceValueTarget = std::clamp(iniFile.getIntValue("Force Concentration", "PalaceValueTarget", force.palaceValueTarget), 0, 2);
    force.palaceWaveTiming = std::clamp(iniFile.getIntValue("Force Concentration", "PalaceWaveTiming", force.palaceWaveTiming), 0, 2);
    force.spreadVsDeathHand = iniFile.getBoolValue("Force Concentration", "SpreadVsDeathHand", force.spreadVsDeathHand);
    force.defencePrerequisites = iniFile.getBoolValue("Force Concentration", "DefencePrerequisites", force.defencePrerequisites);
    force.hardCityMatch = iniFile.getBoolValue("Force Concentration", "HardCityMatch", force.hardCityMatch);
    civWarsStrategy = iniFile.getBoolValue("Civ Wars", "Strategy", civWarsStrategy);
    force.researchLab = iniFile.getBoolValue("Force Concentration", "ResearchLab", force.researchLab);
    force.researchLabProgression = iniFile.getBoolValue("Force Concentration", "ResearchLabProgression", force.researchLabProgression);
    force.cityGrowthPower = iniFile.getBoolValue("Force Concentration", "CityGrowthPower", force.cityGrowthPower);
    force.cityPowerSizing = iniFile.getBoolValue("Force Concentration", "CityPowerSizing", force.cityPowerSizing);
    force.cityOpeningCapital = iniFile.getBoolValue("Force Concentration", "CityOpeningCapital", force.cityOpeningCapital);
    force.cityConstructionCapital = iniFile.getBoolValue("Force Concentration", "CityConstructionCapital", force.cityConstructionCapital);
    force.cityProductionPrerequisites = iniFile.getBoolValue("Force Concentration", "CityProductionPrerequisites", force.cityProductionPrerequisites);
    force.cityFirstFactorySaving = iniFile.getBoolValue("Force Concentration", "CityFirstFactorySaving", force.cityFirstFactorySaving);
    force.cityThreatBudget = iniFile.getBoolValue("Force Concentration", "CityThreatBudget", force.cityThreatBudget);
    force.cityZoneDefence = iniFile.getBoolValue("Force Concentration", "CityZoneDefence", force.cityZoneDefence);
    force.cityAdvancedTech = iniFile.getBoolValue("Force Concentration", "CityAdvancedTech", force.cityAdvancedTech);
    force.cityContinuousResearch = iniFile.getBoolValue("Force Concentration", "CityContinuousResearch", force.cityContinuousResearch);
    force.cityResearchOrder = std::clamp(iniFile.getIntValue("Force Concentration", "CityResearchOrder", force.cityResearchOrder), 0, 2);
    force.citySpiceTransition = iniFile.getBoolValue("Force Concentration", "CitySpiceTransition", force.citySpiceTransition);
    force.spiceFieldExpansion = iniFile.getIntValue("Force Concentration", "SpiceFieldExpansion", force.spiceFieldExpansion);
    force.citySpiceCapacityRunway = iniFile.getBoolValue("Force Concentration", "CitySpiceCapacityRunway", force.citySpiceCapacityRunway);
    force.citySingleYardGrowth = iniFile.getBoolValue("Force Concentration", "CitySingleYardGrowth", force.citySingleYardGrowth);
    force.cityPowerGrowthHorizon = iniFile.getBoolValue("Force Concentration", "CityPowerGrowthHorizon", force.cityPowerGrowthHorizon);
    force.cityBoomWorkers = iniFile.getBoolValue("Force Concentration", "CityBoomWorkers", force.cityBoomWorkers);
    force.cityBoomRefineries = iniFile.getBoolValue("Force Concentration", "CityBoomRefineries", force.cityBoomRefineries);
    force.cityBoomGreedy = iniFile.getBoolValue("Force Concentration", "CityBoomGreedy", force.cityBoomGreedy);
    force.cityBoomShortTax = iniFile.getBoolValue("Force Concentration", "CityBoomShortTax", force.cityBoomShortTax);
    force.cityBoomReactive = iniFile.getBoolValue("Force Concentration", "CityBoomReactive", force.cityBoomReactive);
    force.cityBoomWorkerGoal = iniFile.getIntValue("Force Concentration", "CityBoomWorkerGoal", force.cityBoomWorkerGoal) == 24 ? 24 : 16;
    force.cityBoomRefineryGoal = iniFile.getIntValue("Force Concentration", "CityBoomRefineryGoal", force.cityBoomRefineryGoal) == 10 ? 10 : 6;
    force.cityBoomGreedyMinutes = iniFile.getIntValue("Force Concentration", "CityBoomGreedyMinutes", force.cityBoomGreedyMinutes) == 10 ? 10 : 6;
    force.cityBoomTaxMinutes = iniFile.getIntValue("Force Concentration", "CityBoomTaxMinutes", force.cityBoomTaxMinutes) == 10 ? 10 : 6;
    force.cityBoomArmyRatioBps = iniFile.getIntValue("Force Concentration", "CityBoomArmyRatioBps", force.cityBoomArmyRatioBps) == 9000 ? 9000 : 6000;
    force.cityBoomEconomyRatioBps = iniFile.getIntValue("Force Concentration", "CityBoomEconomyRatioBps", force.cityBoomEconomyRatioBps) == 15000 ? 15000 : 12000;
    force.cityBoomSpiceGate = iniFile.getBoolValue("Force Concentration", "CityBoomSpiceGate", force.cityBoomSpiceGate);
    force.cityBoomPaybackMinutes = iniFile.getIntValue("Force Concentration", "CityBoomPaybackMinutes", force.cityBoomPaybackMinutes) == 15 ? 15 : 10;
    force.cityHarvestFieldChoice = iniFile.getBoolValue("Force Concentration", "CityHarvestFieldChoice", force.cityHarvestFieldChoice);
    force.cityHarvestFieldSafetyTiles = iniFile.getIntValue("Force Concentration", "CityHarvestFieldSafetyTiles", force.cityHarvestFieldSafetyTiles) == 10 ? 10 : 6;
    force.cityRefineryLogistics = iniFile.getBoolValue("Force Concentration", "CityRefineryLogistics", force.cityRefineryLogistics);
    force.cityRefinerySiteTiles = iniFile.getIntValue("Force Concentration", "CityRefinerySiteTiles", force.cityRefinerySiteTiles) == 12 ? 12 : 8;
    force.cityHarvesterReplacement = iniFile.getBoolValue("Force Concentration", "CityHarvesterReplacement", force.cityHarvesterReplacement);
    force.cityHarvesterReplacementMinutes = iniFile.getIntValue("Force Concentration", "CityHarvesterReplacementMinutes", force.cityHarvesterReplacementMinutes) == 15 ? 15 : 10;
    force.cityLandValueServices = iniFile.getBoolValue("Force Concentration", "CityLandValueServices", force.cityLandValueServices);
    force.cityLandValueFundingPercent = iniFile.getIntValue("Force Concentration", "CityLandValueFundingPercent", force.cityLandValueFundingPercent) == 100 ? 100 : 60;
    force.cityGrowthStageTax = iniFile.getBoolValue("Force Concentration", "CityGrowthStageTax", force.cityGrowthStageTax);
    force.cityGrowthTaxPopulation = iniFile.getIntValue("Force Concentration", "CityGrowthTaxPopulation", force.cityGrowthTaxPopulation) == 6000 ? 6000 : 3000;
    force.cityCapitalPacing = std::clamp(iniFile.getIntValue("Force Concentration", "CityCapitalPacing", force.cityCapitalPacing), 0, 2);
    force.cityFleetRatios = std::clamp(iniFile.getIntValue("Force Concentration", "CityFleetRatios", force.cityFleetRatios), 0, 2);
    force.cityTechRefineries = std::clamp(iniFile.getIntValue("Force Concentration", "CityTechRefineries", force.cityTechRefineries), 0, 2);
    force.cityYardRefineries = std::clamp(iniFile.getIntValue("Force Concentration", "CityYardRefineries", force.cityYardRefineries), 0, 2);
    force.cityWorkerReplacement = std::clamp(iniFile.getIntValue("Force Concentration", "CityWorkerReplacement", force.cityWorkerReplacement), 0, 2);
    force.closingMountainScope = std::clamp(iniFile.getIntValue("Force Concentration", "ClosingMountainScope", force.closingMountainScope), 0, 2);
    force.convergeChokepointScope = std::clamp(iniFile.getIntValue("Force Concentration", "ConvergeChokepointScope", force.convergeChokepointScope), 0, 1);
    force.openingOpenMapScope = std::clamp(iniFile.getIntValue("Force Concentration", "OpeningOpenMapScope", force.openingOpenMapScope), 0, 1);
    force.vanillaArmyCounterMapScope = std::clamp(iniFile.getIntValue("Force Concentration", "VanillaArmyCounterMapScope", force.vanillaArmyCounterMapScope), 0, 5);
    force.focusFireMapScope = std::clamp(iniFile.getIntValue("Force Concentration", "FocusFireMapScope", force.focusFireMapScope), 0, 5);
    force.spiceBloomHarvest = std::clamp(iniFile.getIntValue("Force Concentration", "SpiceBloomHarvest", force.spiceBloomHarvest), 0, 2);
    force.retreatPreserve = iniFile.getBoolValue("Force Concentration", "RetreatPreserve", force.retreatPreserve);
    force.retreatPreserveLevel = std::clamp(iniFile.getIntValue("Force Concentration", "RetreatPreserveLevel", force.retreatPreserveLevel), 1, 2);
    force.harvesterRaid = iniFile.getBoolValue("Force Concentration", "HarvesterRaid", force.harvesterRaid);
    force.harvesterRaidLevel = std::clamp(iniFile.getIntValue("Force Concentration", "HarvesterRaidLevel", force.harvesterRaidLevel), 1, 2);
    force.threatTurrets = iniFile.getBoolValue("Force Concentration", "ThreatTurrets", force.threatTurrets);
    force.moreRocketTurrets = iniFile.getBoolValue("Force Concentration", "MoreRocketTurrets", force.moreRocketTurrets);
    force.cityStarportSupply = iniFile.getBoolValue("Force Concentration", "CityStarportSupply", force.cityStarportSupply);
    force.storageCapacity = iniFile.getBoolValue("Force Concentration", "StorageCapacity", force.storageCapacity);
    force.powerLeadBuildings = std::clamp(iniFile.getIntValue(
        "Force Concentration", "PowerLeadBuildings", force.powerLeadBuildings), 0, 2);
    const int siloFill = iniFile.getIntValue("Force Concentration", "SiloFillPercent", force.siloFillPercent);
    if (siloFill == 0 || siloFill == 80 || siloFill == 90) force.siloFillPercent = siloFill;
    force.fundedClassicProduction = iniFile.getBoolValue("Force Concentration", "FundedClassicProduction", force.fundedClassicProduction);
    force.trooperSupport = iniFile.getBoolValue("Force Concentration", "TrooperSupport", force.trooperSupport);
    force.simpleTechSavings = iniFile.getBoolValue("Force Concentration", "SimpleTechSavings", force.simpleTechSavings);
    force.launcherKiteExtraDistance = std::clamp(iniFile.getIntValue(
        "Force Concentration", "LauncherKiteExtraDistance", force.launcherKiteExtraDistance), 0, 2);
    force.exhaustedArmyDispatch = iniFile.getBoolValue(
        "Force Concentration", "ExhaustedArmyDispatch", force.exhaustedArmyDispatch);
    force.mediumClassic = std::clamp(iniFile.getIntValue(
        "Force Concentration", "MediumClassic", force.mediumClassic), 0, 2);
    // [Strategy]: optional keys, never written by save(); a malformed value keeps the current one.
    if (iniFile.hasKey("Strategy", "Mode")) {
        const std::string mode = iniFile.getStringValue("Strategy", "Mode");
        if (!QuantBotStrategy::parseMode(mode, strategy))
            SDL_Log("QuantBot config: invalid [Strategy] Mode '%s', keeping %s", mode.c_str(), QuantBotStrategy::modeText(strategy).c_str());
    }
    if (iniFile.hasKey("Strategy", "MixWeights")) {
        const std::string weights = iniFile.getStringValue("Strategy", "MixWeights");
        if (!QuantBotStrategy::parseWeights(weights, strategy.mixWeights))
            SDL_Log("QuantBot config: invalid [Strategy] MixWeights '%s', keeping %s", weights.c_str(),
                    QuantBotStrategy::weightsText(strategy.mixWeights).c_str());
    }
    if (iniFile.hasKey("Strategy", "TempoWeights")) {
        const std::string weights = iniFile.getStringValue("Strategy", "TempoWeights");
        if (!QuantBotStrategy::parseWeights(weights, strategy.tempoWeights))
            SDL_Log("QuantBot config: invalid [Strategy] TempoWeights '%s', keeping %s", weights.c_str(),
                    QuantBotStrategy::weightsText(strategy.tempoWeights).c_str());
    }
    // [Unit Ratios Tuned]: optional keys, never written by save(); a malformed value keeps the current one.
    // <House> is the house's top mix, <House>_Tech<N> the mix of its tech band from level N (or
    // "Opening": that band plays the configured opening; a mix or an empty value clears it).
    {
        static const char* const kTunedHouses[6] = {"Harkonnen", "Atreides", "Ordos", "Fremen", "Sardaukar", "Mercenary"};
        for (size_t house = 0; house < tunedRatios.size(); ++house) {
            for (size_t band = 0; band < kTunedBands; ++band) {
                const std::string key = band == 0 ? std::string(kTunedHouses[house])
                                                  : std::string(kTunedHouses[house]) + "_Tech" + std::to_string(band);
                if (!iniFile.hasKey("Unit Ratios Tuned", key)) continue;
                const std::string text = iniFile.getStringValue("Unit Ratios Tuned", key);
                std::string word = text;
                word.erase(std::remove_if(word.begin(), word.end(), [](unsigned char c) { return std::isspace(c); }), word.end());
                std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                if (band > 0 && word == "opening") {
                    tunedRatios[house][band] = {};
                    tunedOpening[house][band] = true;
                } else if (QuantBotStrategy::parseRatios(text, tunedRatios[house][band])) {
                    tunedOpening[house][band] = false;
                } else {
                    SDL_Log("QuantBot config: invalid [Unit Ratios Tuned] %s '%s', keeping %s", key.c_str(), text.c_str(),
                            QuantBotStrategy::ratiosText(tunedRatios[house][band]).c_str());
                }
            }
        }
    }

    bool needsSave = false;

    // === LOAD TARGET PRIORITIES ===
    loadPriorities(iniFile, "Structure Priorities", structurePriorities, normalizedStructureNames, needsSave);
    loadPriorities(iniFile, "Unit Priorities", unitPriorities, normalizedUnitNames, needsSave);
    return needsSave;
}

const QuantBotConfig::DifficultySettings& QuantBotConfig::getSettings(int difficulty) const {
    switch (difficulty) {
        case 0: return easy;
        case 1: return medium;
        case 2: return hard;
        case 3: return brutal;
        case 4: return defend;
        default: return medium;
    }
}

QuantBotConfig::DifficultySettings& QuantBotConfig::getSettings(int difficulty) {
    switch (difficulty) {
        case 0: return easy;
        case 1: return medium;
        case 2: return hard;
        case 3: return brutal;
        case 4: return defend;
        default: return medium;
    }
}

const QuantBotConfig::UnitRatios& QuantBotConfig::getRatios(int houseID) const {
    // Same ratios for all difficulties - ornithopter spam controlled by attack flags
    switch (houseID) {
        case HOUSE_ATREIDES: return unitRatios.atreides;
        case HOUSE_HARKONNEN: return unitRatios.harkonnen;
        case HOUSE_ORDOS: return unitRatios.ordos;
        case HOUSE_FREMEN: return unitRatios.fremen;
        case HOUSE_SARDAUKAR: return unitRatios.sardaukar;
        case HOUSE_MERCENARY: return unitRatios.mercenary;
        default: return unitRatios.mercenary;
    }
}

const UnitMixPolicy::Weights& QuantBotConfig::getTunedRatios(int houseID, int band) const {
    static const UnitMixPolicy::Weights none{};
    if (houseID < 0 || size_t(houseID) >= tunedRatios.size()) return none;
    const size_t h = size_t(houseID);
    const auto& house = tunedRatios[h];
    if (band > 0 && size_t(band) < kTunedBands) {
        if (tunedOpening[h][size_t(band)]) return none;
        if (*std::max_element(house[size_t(band)].begin(), house[size_t(band)].end()) > 0) return house[size_t(band)];
    }
    return house[0];
}

const QuantBotConfig::TargetPriority& QuantBotConfig::getStructurePriority(int structureID) const {
    auto it = structureIdToName.find(structureID);
    if (it != structureIdToName.end()) {
        auto priIt = structurePriorities.find(it->second);
        if (priIt != structurePriorities.end()) {
            return priIt->second;
        }
    }
    return defaultPriority;
}

const QuantBotConfig::TargetPriority& QuantBotConfig::getUnitPriority(int unitID) const {
    auto it = unitIdToName.find(unitID);
    if (it != unitIdToName.end()) {
        auto priIt = unitPriorities.find(it->second);
        if (priIt != unitPriorities.end()) {
            return priIt->second;
        }
    }
    return defaultPriority;
}

const QuantBotConfig::TargetPriority* QuantBotConfig::findStructurePriority(const std::string& name) const {
    std::string normalized = normalizeKey(name);
    auto lookupIt = normalizedStructureNames.find(normalized);
    if (lookupIt != normalizedStructureNames.end()) {
        auto priIt = structurePriorities.find(lookupIt->second);
        if (priIt != structurePriorities.end()) {
            return &priIt->second;
        }
    }
    auto directIt = structurePriorities.find(name);
    if (directIt != structurePriorities.end()) {
        return &directIt->second;
    }
    return nullptr;
}

const QuantBotConfig::TargetPriority* QuantBotConfig::findUnitPriority(const std::string& name) const {
    std::string normalized = normalizeKey(name);
    auto lookupIt = normalizedUnitNames.find(normalized);
    if (lookupIt != normalizedUnitNames.end()) {
        auto priIt = unitPriorities.find(lookupIt->second);
        if (priIt != unitPriorities.end()) {
            return &priIt->second;
        }
    }
    auto directIt = unitPriorities.find(name);
    if (directIt != unitPriorities.end()) {
        return &directIt->second;
    }
    return nullptr;
}

// Global instance
static QuantBotConfig* g_quantBotConfig = nullptr;

QuantBotConfig& getQuantBotConfig() {
    if (!g_quantBotConfig) {
        g_quantBotConfig = new QuantBotConfig();
        
        // Load from file
        std::string configPath = getQuantBotConfigFilepath();
        g_quantBotConfig->load(configPath);
        
        // Log configuration for debugging
        g_quantBotConfig->logSettings();
    }
    return *g_quantBotConfig;
}

std::string getQuantBotConfigFilepath() {
    // If ModManager is initialized and a non-vanilla mod is active, use mod path
    if (ModManager::instance().isInitialized() && 
        ModManager::instance().getActiveModName() != "vanilla") {
        return ModManager::instance().getActiveQuantBotConfigPath();
    }
    
    // Default: user config directory (preserves existing user customizations)
    char tmp[FILENAME_MAX];
    if(fnkdat("config/QuantBot Config.ini", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) < 0) {
        THROW(std::runtime_error, "fnkdat() failed for QuantBot Config.ini!");
    }
    return std::string(tmp);
}

std::string getObjectDataFilepath() {
    // If ModManager is initialized and a non-vanilla mod is active, use mod path
    if (ModManager::instance().isInitialized() && 
        ModManager::instance().getActiveModName() != "vanilla") {
        return ModManager::instance().getActiveObjectDataPath();
    }
    
    // Default: user config directory (preserves existing user customizations)
    char tmp[FILENAME_MAX];
    if(fnkdat("config/ObjectData.ini", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) < 0) {
        THROW(std::runtime_error, "fnkdat() failed for ObjectData.ini!");
    }
    return std::string(tmp);
}

std::string getObjectDataHash() {
    // Use the cached hash from ModManager which covers ObjectData.ini
    // This ensures consistency with mod system and avoids re-reading files
    if (ModManager::instance().isInitialized()) {
        return ModManager::instance().getEffectiveChecksums().objectData;
    }
    
    // Fallback: canonical hash of file if ModManager not ready
    std::string filePath = getObjectDataFilepath();
    
    std::ifstream file(filePath);
    if (!file.is_open()) {
        SDL_Log("Warning: Could not open ObjectData.ini for hashing: %s", filePath.c_str());
        return "ERROR_FILE_NOT_FOUND";
    }
    
    // Read and canonicalize: skip comments, skip empty lines, trim whitespace
    std::string contents;
    std::string line;
    while (std::getline(file, line)) {
        // Remove trailing \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // Trim leading/trailing whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) {
            continue;  // Empty or whitespace-only line
        }
        size_t end = line.find_last_not_of(" \t");
        line = line.substr(start, end - start + 1);
        
        // Skip comment lines
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        
        // Strip inline comments
        size_t commentPos = line.find(" ;");
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
            end = line.find_last_not_of(" \t");
            if (end != std::string::npos) {
                line = line.substr(0, end + 1);
            }
        }
        
        if (!line.empty()) {
            contents += line + '\n';
        }
    }
    file.close();
    
    // FNV-1a hash
    uint64_t hash = 14695981039346656037ULL;
    const uint64_t prime = 1099511628211ULL;
    for (char c : contents) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= prime;
    }
    
    char hashStr[17];
    snprintf(hashStr, sizeof(hashStr), "%016llx", (unsigned long long)hash);
    return std::string(hashStr);
}

void QuantBotConfig::logSettings() const {
    SDL_Log("==================== QUANTBOT CONFIGURATION ====================");
    SDL_Log("Config file: %s", getQuantBotConfigFilepath().c_str());
    SDL_Log("%s", "");
    
    SDL_Log("=== DIFFICULTY SETTINGS ===");
    SDL_Log("DEFEND:  Attack=%d OrnAttack=%d OrnThresh=%d AttackForce=%.0f%% MilMult=%.2f HarvMult=%d RefMin=%d",
        defend.attackEnabled, defend.ornithopterAttackEnabled, defend.ornithopterAttackThreshold,
        defend.attackForceMilitaryValueRatio * 100, defend.militaryValueMultiplier, defend.harvesterLimitPerRefineryMultiplier, defend.refineryMinimum);
    SDL_Log("EASY:    Attack=%d OrnAttack=%d OrnThresh=%d AttackForce=%.0f%% MilMult=%.2f HarvMult=%d RefMin=%d",
        easy.attackEnabled, easy.ornithopterAttackEnabled, easy.ornithopterAttackThreshold,
        easy.attackForceMilitaryValueRatio * 100, easy.militaryValueMultiplier, easy.harvesterLimitPerRefineryMultiplier, easy.refineryMinimum);
    SDL_Log("MEDIUM:  Attack=%d OrnAttack=%d OrnThresh=%d AttackForce=%.0f%% MilMult=%.2f HarvMult=%d RefMin=%d",
        medium.attackEnabled, medium.ornithopterAttackEnabled, medium.ornithopterAttackThreshold,
        medium.attackForceMilitaryValueRatio * 100, medium.militaryValueMultiplier, medium.harvesterLimitPerRefineryMultiplier, medium.refineryMinimum);
    SDL_Log("HARD:    Attack=%d OrnAttack=%d OrnThresh=%d AttackForce=%.0f%% MilMult=%.2f HarvMult=%d RefMin=%d",
        hard.attackEnabled, hard.ornithopterAttackEnabled, hard.ornithopterAttackThreshold,
        hard.attackForceMilitaryValueRatio * 100, hard.militaryValueMultiplier, hard.harvesterLimitPerRefineryMultiplier, hard.refineryMinimum);
    SDL_Log("BRUTAL:  Attack=%d OrnAttack=%d OrnThresh=%d AttackForce=%.0f%% MilMult=%.2f HarvMult=%d RefMin=%d",
        brutal.attackEnabled, brutal.ornithopterAttackEnabled, brutal.ornithopterAttackThreshold,
        brutal.attackForceMilitaryValueRatio * 100, brutal.militaryValueMultiplier, brutal.harvesterLimitPerRefineryMultiplier, brutal.refineryMinimum);
    SDL_Log("%s", "");
    
    SDL_Log("=== GENERAL BEHAVIOR ===");
    SDL_Log("AttackTimerMs: %d", attackTimerMs);
    SDL_Log("AttackThresholdPercent: %.2f", attackThresholdPercent);
    SDL_Log("MinMoneyForProduction: %d", minMoneyForProduction);
    SDL_Log("WaveFormation: %s", waveFormationName(waveFormation));
    SDL_Log("StrategicSiteGuards: %d", int(strategicSiteGuards));
    SDL_Log("PoliceFundingRecovery: %d", int(policeFundingRecovery));
    SDL_Log("DeviatedHarvesterEarlyReturn: %d", int(deviatedHarvesterEarlyReturn));
    SDL_Log("ExhaustedArmyDispatch: %d", int(force.exhaustedArmyDispatch));
    SDL_Log("%s", "");

    SDL_Log("=== ARMY RECOVERY (Custom Hard/Brutal, provisional) ===");
    SDL_Log("Enabled=%d Batteries=%d WindowMs=%d LossShareBps=%d TradeShareBps=%d LossFloor=%d",
        recovery.enabled, recovery.frontBatteriesEnabled, recovery.attritionWindowMs,
        recovery.lossShareBps, recovery.tradeShareBps, recovery.lossFloorCredits);
    SDL_Log("LocalWithdrawBps=%d LocalSevereBps=%d LocalPersistMs=%d RecallOrdersPerPass=%d",
        recovery.localWithdrawBps, recovery.localSevereBps, recovery.localPersistMs,
        recovery.recallOrdersPerPass);
    SDL_Log("ResumeAssembledBps=%d ResumeAdvantageBps=%d StabiliseMs=%d MinWithdrawMs=%d MaxWithdrawMs=%d MaxRecoverMs=%d",
        recovery.resumeAssembledBps, recovery.resumeAdvantageBps, recovery.stabiliseMs,
        recovery.minWithdrawMs, recovery.maxWithdrawMs, recovery.maxRecoverMs);
    SDL_Log("OutnumberedBps=%d DispatchBypassBps=%d",
        recovery.outnumberedBps, recovery.dispatchBypassBps);
    SDL_Log("%s", "");

    SDL_Log("=== STRUCTURE PRIORITIES ===");
    for (const auto& entry : structurePriorities) {
        SDL_Log("%s: build=%d target=%d", entry.first.c_str(), entry.second.build, entry.second.target);
    }
    SDL_Log("%s", "");

    SDL_Log("=== UNIT PRIORITIES ===");
    for (const auto& entry : unitPriorities) {
        SDL_Log("%s: build=%d target=%d", entry.first.c_str(), entry.second.build, entry.second.target);
    }
    SDL_Log("%s", "");
    
    SDL_Log("=== UNIT RATIOS (same for all difficulties) ===");
    SDL_Log("Atreides:  Tank=%.2f Siege=%.2f Launcher=%.2f Special=%.2f Orni=%.2f",
        unitRatios.atreides.tank, unitRatios.atreides.siegeTank, 
        unitRatios.atreides.launcher, unitRatios.atreides.special, unitRatios.atreides.ornithopter);
    SDL_Log("Harkonnen: Tank=%.2f Siege=%.2f Launcher=%.2f Special=%.2f Orni=%.2f",
        unitRatios.harkonnen.tank, unitRatios.harkonnen.siegeTank,
        unitRatios.harkonnen.launcher, unitRatios.harkonnen.special, unitRatios.harkonnen.ornithopter);
    SDL_Log("Ordos:     Tank=%.2f Siege=%.2f Launcher=%.2f Special=%.2f Orni=%.2f",
        unitRatios.ordos.tank, unitRatios.ordos.siegeTank,
        unitRatios.ordos.launcher, unitRatios.ordos.special, unitRatios.ordos.ornithopter);
    SDL_Log("Fremen:    Tank=%.2f Siege=%.2f Launcher=%.2f Special=%.2f Orni=%.2f",
        unitRatios.fremen.tank, unitRatios.fremen.siegeTank,
        unitRatios.fremen.launcher, unitRatios.fremen.special, unitRatios.fremen.ornithopter);
    SDL_Log("Sardaukar: Tank=%.2f Siege=%.2f Launcher=%.2f Special=%.2f Orni=%.2f",
        unitRatios.sardaukar.tank, unitRatios.sardaukar.siegeTank,
        unitRatios.sardaukar.launcher, unitRatios.sardaukar.special, unitRatios.sardaukar.ornithopter);
    SDL_Log("Mercenary: Tank=%.2f Siege=%.2f Launcher=%.2f Special=%.2f Orni=%.2f",
        unitRatios.mercenary.tank, unitRatios.mercenary.siegeTank,
        unitRatios.mercenary.launcher, unitRatios.mercenary.special, unitRatios.mercenary.ornithopter);
    SDL_Log("===============================================================");
}

std::string QuantBotConfig::getConfigHash() const {
    // Create a string representation of all config values for multiplayer verification
    std::string configStr;
    
    // Difficulty settings
    auto addDiffSettings = [&](const char* name, const DifficultySettings& s) {
        configStr += name;
        configStr += std::to_string(s.attackEnabled);
        // The per-difficulty attack threshold and force ratio decide when a house
        // attacks and how much of its army it commits. They were absent from this
        // hash, so two peers could run different offensive tuning and diverge
        // without the lobby noticing. They are part of it now.
        configStr += std::to_string(s.attackThresholdPercent);
        configStr += std::to_string(s.attackForceMilitaryValueRatio);
        configStr += std::to_string(s.refineryMinimum);
        configStr += std::to_string(s.ornithopterAttackEnabled);
        configStr += std::to_string(s.ornithopterAttackThreshold);
        configStr += std::to_string(s.militaryValueMultiplier);
        configStr += std::to_string(s.harvesterLimitPerRefineryMultiplier);
        configStr += std::to_string(s.militaryValueLimitCustomSmallMap);
        configStr += std::to_string(s.militaryValueLimitCustomMediumMap);
        configStr += std::to_string(s.militaryValueLimitCustomLargeMap);
        configStr += std::to_string(s.militaryValueLimitCustomHugeMap);
        configStr += std::to_string(s.harvesterLimitCustomSmallMap);
        configStr += std::to_string(s.harvesterLimitCustomMediumMap);
        configStr += std::to_string(s.harvesterLimitCustomLargeMap);
        configStr += std::to_string(s.harvesterLimitCustomHugeMap);
        configStr += std::to_string(s.structureDefenders);
        configStr += std::to_string(s.harvesterDefenders);
    };
    
    addDiffSettings("defend", defend);
    addDiffSettings("easy", easy);
    addDiffSettings("medium", medium);
    addDiffSettings("hard", hard);
    addDiffSettings("brutal", brutal);
    
    // Unit ratios - add all houses (same for all difficulties)
    auto addRatios = [&](const char* name, const UnitRatios& r) {
        configStr += name;
        configStr += std::to_string(r.tank);
        configStr += std::to_string(r.siegeTank);
        configStr += std::to_string(r.launcher);
        configStr += std::to_string(r.special);
        configStr += std::to_string(r.ornithopter);
    };
    
    addRatios("Atr", unitRatios.atreides);
    addRatios("Har", unitRatios.harkonnen);
    addRatios("Ord", unitRatios.ordos);
    addRatios("Fre", unitRatios.fremen);
    addRatios("Sar", unitRatios.sardaukar);
    addRatios("Mer", unitRatios.mercenary);

    for (const auto& entry : structurePriorities) {
        configStr += "SP";
        configStr += entry.first;
        configStr += std::to_string(entry.second.build);
        configStr += std::to_string(entry.second.target);
    }

    for (const auto& entry : unitPriorities) {
        configStr += "UP";
        configStr += entry.first;
        configStr += std::to_string(entry.second.build);
        configStr += std::to_string(entry.second.target);
    }

    // General behavior
    configStr += std::to_string(attackTimerMs);
    configStr += std::to_string(attackThresholdPercent);
    configStr += std::to_string(minMoneyForProduction);
    configStr += "|waveFormation=";
    configStr += std::to_string(waveFormation);

    // Army recovery, cohesion, outnumbered dispatch gate and front batteries.
    // Every one of these changes an AI order, so every one of them is hashed.
    //
    // Named and delimited, unlike the older blocks above: bare concatenation of
    // adjacent integers aliases distinct configurations - "12" followed by "34"
    // and "123" followed by "4" produce the same bytes - so two peers with
    // genuinely different recovery tuning could agree on the hash.
    auto addNamed = [&](const char* name, long long value) {
        configStr += '|'; configStr += name; configStr += '='; configStr += std::to_string(value);
    };
    addNamed("strategicSiteGuards", strategicSiteGuards);
    addNamed("policeFundingRecovery", policeFundingRecovery);
    addNamed("deviatedHarvesterEarlyReturn", deviatedHarvesterEarlyReturn);
    configStr += "|REC";
    addNamed("enabled", recovery.enabled);
    addNamed("windowMs", recovery.attritionWindowMs);
    addNamed("lossShareBps", recovery.lossShareBps);
    addNamed("tradeShareBps", recovery.tradeShareBps);
    addNamed("lossFloor", recovery.lossFloorCredits);
    addNamed("localWithdrawBps", recovery.localWithdrawBps);
    addNamed("localSevereBps", recovery.localSevereBps);
    addNamed("localPersistMs", recovery.localPersistMs);
    addNamed("resumeAssembledBps", recovery.resumeAssembledBps);
    addNamed("resumeAdvantageBps", recovery.resumeAdvantageBps);
    addNamed("stabiliseMs", recovery.stabiliseMs);
    addNamed("minWithdrawMs", recovery.minWithdrawMs);
    addNamed("maxWithdrawMs", recovery.maxWithdrawMs);
    addNamed("maxRecoverMs", recovery.maxRecoverMs);
    addNamed("outnumberedBps", recovery.outnumberedBps);
    addNamed("dispatchBypassBps", recovery.dispatchBypassBps);
    addNamed("recallOrdersPerPass", recovery.recallOrdersPerPass);
    addNamed("frontBatteries", recovery.frontBatteriesEnabled);
    addNamed("withdrawals", recovery.withdrawalsEnabled);
    configStr += "|/REC|";
    configStr += "|FORCE";
    addNamed("focusFire", force.focusFire);
    // OFF preserves the native config hash, including strategy's seeded choices.
    if (force.focusFireLevel) addNamed("focusFireLevel", force.focusFireLevel);
    if (force.targetPriorityLevel) addNamed("targetPriorityLevel", force.targetPriorityLevel);
    addNamed("converge", force.converge);
    addNamed("waveReadinessPercent", force.waveReadinessPercent);
    addNamed("bundle", force.bundle);
    addNamed("launchFromBundle", force.launchFromBundle);
    addNamed("perimeterTurrets", force.perimeterTurrets);
    addNamed("sizedDefence", force.sizedDefence);
    addNamed("openingReadiness", force.openingReadiness);
    addNamed("turretPriority", force.turretPriority);
    addNamed("pressOn", force.pressOn);
    addNamed("finishWeakest", force.finishWeakest);
    addNamed("fortifiedWaves", force.fortifiedWaves);
    addNamed("sonicClearLane", force.sonicClearLane);
    addNamed("spendAtCap", force.spendAtCap);
    addNamed("siegeStandoff", force.siegeStandoff);
    addNamed("massAssault", force.massAssault);
    addNamed("finishWhenLeading", force.finishWhenLeading);
    addNamed("vanillaFfaWeakestTarget", force.vanillaFfaWeakestTarget);
    addNamed("vanillaFfaNearbyTarget", force.vanillaFfaNearbyTarget);
    addNamed("vanillaFfaReserve", force.vanillaFfaReserve);
    addNamed("vanillaFfaFullWaves", force.vanillaFfaFullWaves);
    addNamed("vanillaFfaStarportAfterWave", force.vanillaFfaStarportAfterWave);
    if (force.starportBuying) addNamed("starportBuying", force.starportBuying);
    if (force.starportCashStockpile) addNamed("starportCashStockpile", force.starportCashStockpile);
    if (force.starportEconomyPriority) addNamed("starportEconomyPriority", force.starportEconomyPriority);
    addNamed("vanillaFfaThreatProduction", force.vanillaFfaThreatProduction);
    addNamed("vanillaFfaSingleFront", force.vanillaFfaSingleFront);
    addNamed("vanillaFfaLightWorkingCash", force.vanillaFfaLightWorkingCash);
    addNamed("vanillaFfaFundedTech", force.vanillaFfaFundedTech);
    addNamed("vanillaHarvesterBoom", force.vanillaHarvesterBoom);
    addNamed("vanillaRefineryBoom", force.vanillaRefineryBoom);
    addNamed("vanillaGreedyBoom", force.vanillaGreedyBoom);
    addNamed("vanillaReactiveBoom", force.vanillaReactiveBoom);
    addNamed("vanillaBoomFfaOnly", force.vanillaBoomFfaOnly);
    addNamed("vanillaBoomSpiceGate", force.vanillaBoomSpiceGate);
    addNamed("vanillaBoomSpicePerRefinery", force.vanillaBoomSpicePerRefinery);
    if (force.duelOpeningPlan != 0) addNamed("duelOpeningPlan", force.duelOpeningPlan);
    addNamed("vanillaHarvesterBoomWorkers", force.vanillaHarvesterBoomWorkers);
    addNamed("vanillaRefineryBoomRefineries", force.vanillaRefineryBoomRefineries);
    addNamed("vanillaGreedyBoomMinutes", force.vanillaGreedyBoomMinutes);
    addNamed("vanillaReactiveArmyRatioBps", force.vanillaReactiveArmyRatioBps);
    addNamed("vanillaReactiveEconomyRatioBps", force.vanillaReactiveEconomyRatioBps);
    addNamed("vanillaCoreDefence", force.vanillaCoreDefence);
    addNamed("vanillaCoreDefenceThreatBps", force.vanillaCoreDefenceThreatBps);
    addNamed("vanillaArmyCounters", force.vanillaArmyCounters);
    addNamed("vanillaArmyCounterBlendBps", force.vanillaArmyCounterBlendBps);
    addNamed("economicFleetRatios", force.economicFleetRatios);
    addNamed("fleetHarvestersPerBay", force.fleetHarvestersPerBay);
    addNamed("fleetCarryallQuarters", force.fleetCarryallQuarters);
    addNamed("fleetRepairYards", force.fleetRepairYards);
    if (force.repairCapacityDemand) addNamed("repairCapacityDemand", force.repairCapacityDemand);
    if (force.ffaVultureLevel != 0) addNamed("ffaVultureLevel", force.ffaVultureLevel);
    addNamed("techIncomeTiming", force.techIncomeTiming);
    addNamed("joinAllyWaves", force.joinAllyWaves);
    addNamed("teamSynchroniseAttacks", force.teamSynchroniseAttacks);
    addNamed("teamDefenceSharePercent", force.teamDefenceSharePercent);
    addNamed("openingCombatBudget", force.openingCombatBudget);
    addNamed("campaignCombatBudget", force.campaignCombatBudget);
    addNamed("singleMissionCampaignPolicy", force.singleMissionCampaignPolicy);
    addNamed("campaignPlayerHouse", force.campaignPlayerHouse);
    addNamed("campaignMilitaryOpening", force.campaignMilitaryOpening);
    addNamed("campaignAdaptiveReadiness", force.campaignAdaptiveReadiness);
    addNamed("campaignAllyStrengthFloor", force.campaignAllyStrengthFloor);
    addNamed("campaignEasyHarvesters", force.campaignEasyHarvesters);
    addNamed("campaignMediumHarvesters", force.campaignMediumHarvesters);
    addNamed("campaignHardHarvesters", force.campaignHardHarvesters);
    addNamed("campaignEasyWaveValue", force.campaignEasyWaveValue);
    addNamed("campaignMediumWaveValue", force.campaignMediumWaveValue);
    addNamed("campaignHardWaveValue", force.campaignHardWaveValue);
    addNamed("campaignEasyOpeningDelaySec", force.campaignEasyOpeningDelaySec);
    addNamed("campaignMediumOpeningDelaySec", force.campaignMediumOpeningDelaySec);
    addNamed("campaignHardOpeningDelaySec", force.campaignHardOpeningDelaySec);
    addNamed("campaignThreatProduction", force.campaignThreatProduction);
    addNamed("campaignFundedLightOpening", force.campaignFundedLightOpening);
    addNamed("campaignLightWorkingCash", force.campaignLightWorkingCash);
    addNamed("campaignLightIncomeSeed", force.campaignLightIncomeSeed);
    addNamed("campaignFactoryBeforeStarport", force.campaignFactoryBeforeStarport);
    addNamed("campaignIdleCashFactory", force.campaignIdleCashFactory);
    addNamed("campaignIdleCashFactoryCredits", force.campaignIdleCashFactoryCredits);
    addNamed("campaignRefineryDefence", force.campaignRefineryDefence);
    addNamed("campaignFactoryRunway", force.campaignFactoryRunway);
    addNamed("campaignHomeFields", force.campaignHomeFields);
    addNamed("campaignSizedWorkerRescue", force.campaignSizedWorkerRescue);
    addNamed("capacityMcvRunway", force.capacityMcvRunway);
    addNamed("respectServiceOwnership", force.respectServiceOwnership);
    addNamed("clearMcvFootprint", force.clearMcvFootprint);
    addNamed("smallMapYardCap", force.smallMapYardCap);
    addNamed("deathHandTurrets", force.deathHandTurrets);
    addNamed("palaceValueTarget", force.palaceValueTarget);
    addNamed("palaceWaveTiming", force.palaceWaveTiming);
    addNamed("spreadVsDeathHand", force.spreadVsDeathHand);
    addNamed("defencePrerequisites", force.defencePrerequisites);
    addNamed("hardCityMatch", force.hardCityMatch);
    addNamed("researchLab", force.researchLab);
    addNamed("researchLabProgression", force.researchLabProgression);
    addNamed("cityGrowthPower", force.cityGrowthPower);
    addNamed("cityPowerSizing", force.cityPowerSizing);
    addNamed("cityOpeningCapital", force.cityOpeningCapital);
    addNamed("cityConstructionCapital", force.cityConstructionCapital);
    addNamed("cityProductionPrerequisites", force.cityProductionPrerequisites);
    addNamed("cityFirstFactorySaving", force.cityFirstFactorySaving);
    addNamed("cityThreatBudget", force.cityThreatBudget);
    addNamed("cityZoneDefence", force.cityZoneDefence);
    addNamed("cityAdvancedTech", force.cityAdvancedTech);
    addNamed("cityContinuousResearch", force.cityContinuousResearch);
    addNamed("cityResearchOrder", force.cityResearchOrder);
    addNamed("citySpiceTransition", force.citySpiceTransition);
    if (force.spiceFieldExpansion != 0) addNamed("spiceFieldExpansion", force.spiceFieldExpansion);
    addNamed("citySpiceCapacityRunway", force.citySpiceCapacityRunway);
    addNamed("citySingleYardGrowth", force.citySingleYardGrowth);
    addNamed("cityPowerGrowthHorizon", force.cityPowerGrowthHorizon);
    addNamed("cityBoomWorkers", force.cityBoomWorkers);
    addNamed("cityBoomRefineries", force.cityBoomRefineries);
    addNamed("cityBoomGreedy", force.cityBoomGreedy);
    addNamed("cityBoomShortTax", force.cityBoomShortTax);
    addNamed("cityBoomReactive", force.cityBoomReactive);
    addNamed("cityBoomWorkerGoal", force.cityBoomWorkerGoal);
    addNamed("cityBoomRefineryGoal", force.cityBoomRefineryGoal);
    addNamed("cityBoomGreedyMinutes", force.cityBoomGreedyMinutes);
    addNamed("cityBoomTaxMinutes", force.cityBoomTaxMinutes);
    addNamed("cityBoomArmyRatioBps", force.cityBoomArmyRatioBps);
    addNamed("cityBoomEconomyRatioBps", force.cityBoomEconomyRatioBps);
    addNamed("cityBoomSpiceGate", force.cityBoomSpiceGate);
    addNamed("cityBoomPaybackMinutes", force.cityBoomPaybackMinutes);
    addNamed("cityHarvestFieldChoice", force.cityHarvestFieldChoice);
    addNamed("cityHarvestFieldSafetyTiles", force.cityHarvestFieldSafetyTiles);
    addNamed("cityRefineryLogistics", force.cityRefineryLogistics);
    addNamed("cityRefinerySiteTiles", force.cityRefinerySiteTiles);
    addNamed("cityHarvesterReplacement", force.cityHarvesterReplacement);
    addNamed("cityHarvesterReplacementMinutes", force.cityHarvesterReplacementMinutes);
    addNamed("cityLandValueServices", force.cityLandValueServices);
    addNamed("cityLandValueFundingPercent", force.cityLandValueFundingPercent);
    addNamed("cityGrowthStageTax", force.cityGrowthStageTax);
    addNamed("cityGrowthTaxPopulation", force.cityGrowthTaxPopulation);
    // Batch3 City levels: absent (OFF) keeps the native multiplayer hash.
    if (force.cityCapitalPacing != 0) addNamed("cityCapitalPacing", force.cityCapitalPacing);
    if (force.cityFleetRatios != 0) addNamed("cityFleetRatios", force.cityFleetRatios);
    if (force.cityTechRefineries != 0) addNamed("cityTechRefineries", force.cityTechRefineries);
    if (force.cityYardRefineries != 0) addNamed("cityYardRefineries", force.cityYardRefineries);
    if (force.cityWorkerReplacement != 0) addNamed("cityWorkerReplacement", force.cityWorkerReplacement);
    // Map-scoped repair levels: absent (OFF) keeps the native multiplayer hash.
    if (force.closingMountainScope != 0) addNamed("closingMountainScope", force.closingMountainScope);
    if (force.convergeChokepointScope != 0) addNamed("convergeChokepointScope", force.convergeChokepointScope);
    if (force.openingOpenMapScope != 0) addNamed("openingOpenMapScope", force.openingOpenMapScope);
    if (force.vanillaArmyCounterMapScope != 0) addNamed("vanillaArmyCounterMapScope", force.vanillaArmyCounterMapScope);
    if (force.focusFireMapScope != 0) addNamed("focusFireMapScope", force.focusFireMapScope);
    // SpiceBloomHarvest levels: absent (OFF) keeps the native multiplayer hash.
    if (force.spiceBloomHarvest != 0) addNamed("spiceBloomHarvest", force.spiceBloomHarvest);
    addNamed("retreatPreserve", force.retreatPreserve);
    addNamed("retreatPreserveLevel", force.retreatPreserveLevel);
    addNamed("harvesterRaid", force.harvesterRaid);
    addNamed("harvesterRaidLevel", force.harvesterRaidLevel);
    addNamed("threatTurrets", force.threatTurrets);
    addNamed("moreRocketTurrets", force.moreRocketTurrets);
    addNamed("cityStarportSupply", force.cityStarportSupply);
    addNamed("storageCapacity", force.storageCapacity);
    // Preserve the native config/digest when these research switches are OFF.
    if (force.powerLeadBuildings) addNamed("powerLeadBuildings", force.powerLeadBuildings);
    if (force.siloFillPercent) addNamed("siloFillPercent", force.siloFillPercent);
    addNamed("fundedClassicProduction", force.fundedClassicProduction);
    addNamed("trooperSupport", force.trooperSupport);
    addNamed("simpleTechSavings", force.simpleTechSavings);
    addNamed("launcherKiteExtraDistance", force.launcherKiteExtraDistance);
    addNamed("exhaustedArmyDispatch", force.exhaustedArmyDispatch);
    addNamed("mediumClassic", force.mediumClassic);
    configStr += "|/FORCE|";
    addNamed("civWarsStrategy", civWarsStrategy);
    configStr += "|STRATEGY";
    addNamed("random", strategy.random);
    addNamed("fixedMix", static_cast<int>(strategy.fixedMix));
    addNamed("fixedTempo", static_cast<int>(strategy.fixedTempo));
    for (size_t i = 0; i < strategy.mixWeights.size(); ++i)
        addNamed(QuantBotStrategy::name(static_cast<QuantBotStrategy::Mix>(i)), strategy.mixWeights[i]);
    for (size_t i = 0; i < strategy.tempoWeights.size(); ++i)
        addNamed(QuantBotStrategy::name(static_cast<QuantBotStrategy::Tempo>(i)), strategy.tempoWeights[i]);
    configStr += "|TUNED";
    for (size_t h = 0; h < tunedRatios.size(); ++h)
        for (size_t band = 0; band < kTunedBands; ++band) {
            configStr += '|';
            configStr += tunedOpening[h][band] ? std::string("opening") : QuantBotStrategy::ratiosText(tunedRatios[h][band]);
        }
    configStr += "|/TUNED|";
    configStr += "|/STRATEGY|";


    // Use FNV-1a hash (consistent across platforms and compilers)
    uint64_t hash = 14695981039346656037ULL; // FNV offset basis
    const uint64_t prime = 1099511628211ULL;  // FNV prime
    
    for (char c : configStr) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= prime;
    }
    
    char hashStr[17];
    snprintf(hashStr, sizeof(hashStr), "%016llx", (unsigned long long)hash);
    return std::string(hashStr);
}
