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

#ifndef QUANTBOTCONFIG_H
#define QUANTBOTCONFIG_H

#include <DataTypes.h>
#include <data.h>

#include <map>
#include <string>
#include <unordered_map>

/**
 * Configuration structure for QuantBot AI behavior.
 * All settings can be customized via "QuantBot Config.ini" in the user config directory.
 */
struct QuantBotConfig {

    struct TargetPriority {
        int build = 0;
        int target = 0;
    };
    
    // === Difficulty-specific settings ===
    struct DifficultySettings {
        // Attack behavior
        bool attackEnabled;                     // Can this difficulty attack at all?
        float attackThresholdPercent;           // Attack when military >= X% of limit (0.0-1.0)
        bool ornithopterAttackEnabled;          // Can ornithopters attack?
        int ornithopterAttackThreshold;         // Minimum ornithopters needed to attack
        float attackForceMilitaryValueRatio;    // Campaign enemy ground force budget, including active attacks (0.0-1.0)
        
        // Military limits
        float militaryValueMultiplier;          // Multiplier for initial military value (Campaign)
        int militaryValueLimitCustomSmallMap;   // Military value limit for small maps (Custom)
        int militaryValueLimitCustomMediumMap;  // Military value limit for medium maps (Custom)
        int militaryValueLimitCustomLargeMap;   // Military value limit for large maps (Custom)
        int militaryValueLimitCustomHugeMap;    // Military value limit for huge maps >128x128 (Custom)
        
        // Harvester limits
        int harvesterLimitPerRefineryMultiplier; // Harvesters per refinery
        int harvesterLimitCustomSmallMap;       // Harvester limit for small maps (Custom)
        int harvesterLimitCustomMediumMap;      // Harvester limit for medium maps (Custom)
        int harvesterLimitCustomLargeMap;       // Harvester limit for large maps (Custom)
        int harvesterLimitCustomHugeMap;        // Harvester limit for huge maps >128x128 (Custom)
        
        // Refinery minimum (Campaign)
        int refineryMinimum;                    // Guaranteed refineries at start (tops up if below, 0 = no guarantee)
        
        // Defense scramble settings
        int structureDefenders;                 // Max units to scramble when structure is attacked
        int harvesterDefenders;                 // Max units to scramble when harvester is attacked
    };
    
    DifficultySettings defend;   // Very Easy (Defend only)
    DifficultySettings easy;
    DifficultySettings medium;
    DifficultySettings hard;
    DifficultySettings brutal;
    
    // === Unit composition ratios (house-specific) ===
    // These are used when totalDamage < 3000 (early game)
    // Same ratios across all difficulties - ornithopter spam controlled by attack flags
    struct UnitRatios {
        float tank;
        float siegeTank;
        float launcher;
        float special;      // Devastator/Deviator/Sonic Tank
        float ornithopter;
    };
    
    struct HouseRatios {
        UnitRatios atreides;
        UnitRatios harkonnen;
        UnitRatios ordos;
        UnitRatios fremen;
        UnitRatios sardaukar;
        UnitRatios mercenary;  // Default for other houses
    };
    
    // Single set of ratios used across all difficulties
    HouseRatios unitRatios;

    // === Army recovery, cohesion and front batteries ===
    //
    // One shared block rather than a per-difficulty table: the behaviour itself
    // is restricted to Custom Hard/Brutal in code, and campaign pacing, campaign
    // roles and the established Easy/Medium small-wave reserve are untouched.
    //
    // Every field is an integer in milliseconds of game time or in basis points,
    // so nothing here can introduce a floating-point difference between peers.
    // All values are PROVISIONAL and untuned; they are hashed for multiplayer.
    struct RecoverySettings {
        bool enabled;                 // Posture machine for Custom Hard/Brutal
        int attritionWindowMs;        // Material mobile-combat attrition window
        int lossShareBps;             // Window loss vs window-start deployable cost
        int tradeShareBps;            // Confirmed hostile loss vs our own
        int lossFloorCredits;         // Material floor; minor raids never panic
        int localWithdrawBps;         // Local squad disengage ratio
        int localSevereBps;           // Severe: withdraw with no dwell
        int localPersistMs;           // Local disadvantage must persist this long
        int resumeAssembledBps;       // Share of the designated wave gathered
        int resumeAdvantageBps;       // Front advantage needed to resume
        int stabiliseMs;              // Quiet time before resuming
        int minWithdrawMs;            // Ordinary dwell while withdrawing
        int maxWithdrawMs;            // Bounded fallback out of withdrawing
        int maxRecoverMs;             // Bounded fallback out of recovering
        int outnumberedBps;           // Hostile front power that counts as outnumbered
        int dispatchBypassBps;        // Share of CONFIGURED limit that bypasses it
        int recallOrdersPerPass;      // Fair recall/hold orders per AI pass
        bool frontBatteriesEnabled;   // Enemy-facing rocket batteries
    };
    RecoverySettings recovery;

    // === General AI behavior ===
    int attackTimerMs;              // Time between attack checks (milliseconds)
    float attackThresholdPercent;   // Don't attack until military reaches this % of limit (0.0-1.0)
    int minMoneyForProduction;      // Minimum money needed to queue production

    // Priority tables (configurable)
    const TargetPriority& getStructurePriority(int structureID) const;
    const TargetPriority& getUnitPriority(int unitID) const;
    const TargetPriority* findStructurePriority(const std::string& name) const;
    const TargetPriority* findUnitPriority(const std::string& name) const;
    
    // Constructor with default values
    QuantBotConfig();
    
    // Load from INI file (creates default file if not exists)
    bool load(const std::string& filepath);
    
    // Save to INI file
    bool save(const std::string& filepath) const;
    
    // Get difficulty settings by difficulty enum
    const DifficultySettings& getSettings(int difficulty) const;
    DifficultySettings& getSettings(int difficulty);
    
    // Get unit ratios by house ID (same ratios for all difficulties)
    const UnitRatios& getRatios(int houseID) const;
    
    // Logging and verification
    void logSettings() const;
    std::string getConfigHash() const;  // For multiplayer consistency check

    static std::string normalizeKey(const std::string& value);

private:
    void registerStructurePriority(int structureID, const std::string& name, int build, int target);
    void registerUnitPriority(int unitID, const std::string& name, int build, int target);

    std::map<std::string, TargetPriority> structurePriorities;
    std::map<std::string, TargetPriority> unitPriorities;

    std::unordered_map<std::string, std::string> normalizedStructureNames;
    std::unordered_map<std::string, std::string> normalizedUnitNames;

    std::unordered_map<int, std::string> structureIdToName;
    std::unordered_map<int, std::string> unitIdToName;

    mutable TargetPriority defaultPriority;
};

// Global instance (initialized on first use)
QuantBotConfig& getQuantBotConfig();

// Get config file paths
std::string getQuantBotConfigFilepath();
std::string getObjectDataFilepath();

// Get config file hashes for multiplayer verification
std::string getObjectDataHash();

#endif // QUANTBOTCONFIG_H
