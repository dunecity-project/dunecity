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
    // Measured in duels (docs/formations.md, "Concentrating force", rounds 3 and 4): focus
    // fire + converge + waves at 60% readiness (FCQ) beat the previous defaults; bundling the
    // idle reserve forward and launching waves from the bundle (BWFCQ) beats FCQ.
    force.focusFire = true;
    force.converge = true;
    force.waveReadinessPercent = 60;
    force.bundle = true;
    force.launchFromBundle = true;
    // Round 5 (defence while outnumbered, /weak duels): perimeter turrets and sized defences
    // (UX). The perimeter hold V was removed in protocol 84 (placement-score ablation, docs/
    // formations.md "Free-for-all: finishing the game"); an old PerimeterHold key is ignored.
    force.perimeterTurrets = true;
    force.sizedDefence = true;
    // Construction Yard starts (docs/formations.md, "Scenario tests"): holding the first wave
    // for full readiness beats BWFCQ (CY 1v1, 360 games, 0.567); inert with a Fortress army.
    force.openingReadiness = true;
    // Late game (docs/formations.md, "Free-for-all: finishing the game"): candidates, off.
    force.turretPriority = false;
    force.pressOn = false;
    force.finishWeakest = false;
    force.fortifiedWaves = false;
    force.sonicClearLane = false;
    force.spendAtCap = false;
    force.siegeStandoff = false;
    force.massAssault = false;
    // The leader finishes the weakest (docs/formations.md, "Free-for-all: finishing the game,
    // 1.0.848"): 4P DuneCity self-play time-outs 16 -> 13 of 24, first elimination median 37.6 ->
    // 31.3 min; level head-to-head, on Fortress, /weak, CY and the difficulty ladder.
    force.finishWhenLeading = true;
    // Candidate (fix/ladder-campaign, docs/ai-scenario-matrix.md "Team games"): team waves join an
    // ally's fresh wave.
    force.joinAllyWaves = false;
    force.openingCombatBudget = false;
    force.campaignCombatBudget = false;
    force.capacityMcvRunway = false;

    // === GENERAL AI BEHAVIOR ===
    attackTimerMs = 15000;                      // 15 seconds between attacks
    attackThresholdPercent = 0.30f;             // Attack when military >= 30% of limit
    minMoneyForProduction = 500;                // Minimum money to produce units
    strategicSiteGuards = true;
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

        // === FORCE CONCENTRATION ===
        iniFile.setBoolValue("Force Concentration", "FocusFire", force.focusFire);
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
        iniFile.setBoolValue("Force Concentration", "JoinAllyWaves", force.joinAllyWaves);
        iniFile.setBoolValue("Force Concentration", "OpeningCombatBudget", force.openingCombatBudget);
        iniFile.setBoolValue("Force Concentration", "CampaignCombatBudget", force.campaignCombatBudget);
        iniFile.setBoolValue("Force Concentration", "CapacityMcvRunway", force.capacityMcvRunway);

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
        // Try to load from user directory, create defaults if not found
        if (!existsFile(filepath)) {
            SDL_Log("QuantBot config not found: %s", filepath.c_str());
            SDL_Log("Creating default configuration");
            return save(filepath);
        }
        
        INIFile iniFile(filepath);
        if (readFrom(iniFile)) {
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
    {
        const std::string shape = iniFile.getStringValue("General Behavior", "WaveFormation", waveFormationName(waveFormation));
        const int parsed = parseWaveFormation(shape, INT_MIN);
        if (parsed == INT_MIN) SDL_Log("QuantBot config: unknown WaveFormation '%s', keeping %s", shape.c_str(), waveFormationName(waveFormation));
        else waveFormation = parsed;
    }
    force.focusFire = iniFile.getBoolValue("Force Concentration", "FocusFire", force.focusFire);
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
    force.joinAllyWaves = iniFile.getBoolValue("Force Concentration", "JoinAllyWaves", force.joinAllyWaves);
    force.openingCombatBudget = iniFile.getBoolValue("Force Concentration", "OpeningCombatBudget", force.openingCombatBudget);
    force.campaignCombatBudget = iniFile.getBoolValue("Force Concentration", "CampaignCombatBudget", force.campaignCombatBudget);
    force.capacityMcvRunway = iniFile.getBoolValue("Force Concentration", "CapacityMcvRunway", force.capacityMcvRunway);
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
    addNamed("joinAllyWaves", force.joinAllyWaves);
    addNamed("openingCombatBudget", force.openingCombatBudget);
    addNamed("campaignCombatBudget", force.campaignCombatBudget);
    addNamed("capacityMcvRunway", force.capacityMcvRunway);
    configStr += "|/FORCE|";
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
