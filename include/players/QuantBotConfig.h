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
#include <players/QuantBotStrategy.h>

#include <map>
#include <string>
#include <unordered_map>

class INIFile;

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
        /// House-level withdrawals: severe local defeat, sustained attrition, core attack
        /// and the local squad pull-back. Off by default: with them the all-against-Atreides
        /// match loses a seed and wins 2-5 minutes later (quantbot_all_against_atreides).
        bool withdrawalsEnabled;
    };
    RecoverySettings recovery;

    /// How a Custom Hard/Brutal army concentrates force (docs/formations.md, "Concentrating
    /// force"). INI: [Force Concentration] FocusFire, Converge, WaveReadinessPercent, Bundle,
    /// LaunchFromBundle, PerimeterTurrets, SizedDefence, OpeningReadiness, TurretPriority, PressOn,
    /// FinishWeakest, FortifiedWaves, SonicClearLane, SpendAtCap, SiegeStandoff, MassAssault,
    /// FinishWhenLeading, JoinAllyWaves, OpeningCombatBudget, CampaignCombatBudget, CapacityMcvRunway,
    /// RespectServiceOwnership, ClearMcvFootprint, SmallMapYardCap, DeathHandTurrets,
    /// SpreadVsDeathHand, DefencePrerequisites; optional keys, the code defaults
    /// apply when absent. Hashed for multiplayer.
    struct ForceSettings {
        /// Hunting ground fighters shoot the best target in reach: fighters first, then the
        /// one with the fewest hit points (ForceConcentrationPolicy::betterTarget).
        bool focusFire;
        /// Wave members not yet fighting join a fight of their army within 24 tiles.
        bool converge;
        /// The wave readiness and house attack threshold as a percentage of the difficulty's
        /// (100 = unchanged; smaller = smaller, more frequent waves). Clamped to 10-300.
        int waveReadinessPercent;
        /// The idle reserve stands bundled at a forward rally a quarter of the way from the
        /// assembly point to the observed enemy base (saved: SAVEGAMEVERSION 9863).
        bool bundle;
        /// With bundle: a ready wave launches once 70% of its value stands in the bundle,
        /// re-checked every 5 s and at most 60 s late.
        bool launchFromBundle;
        /// While the remembered enemy army (round 5; saved, 9864) is worth 125% of ours, rocket
        /// turrets go up on the enemy-facing base perimeter: one per 1,000 credits of the enemy's
        /// lead, at most eight there, never below the economy and unit reserve.
        bool perimeterTurrets;
        /// Defences count the enemy within 12 tiles and send 1.5x it, the bundle first; distant
        /// defenders of a building muster first (saved, 9864); a worker raid the troops in reach
        /// cannot match gets nobody.
        bool sizedDefence;
        /// A house whose starting army cannot launch a wave at WaveReadinessPercent (a
        /// Construction Yard start, not a Fortress with an army) holds its first ground wave
        /// for full readiness (ForceConcentrationPolicy::openingReadiness; docs/formations.md,
        /// "Scenario tests").
        bool openingReadiness;
        /// Late game (docs/formations.md, "Free-for-all: finishing the game"): a wave hunter
        /// standing in an enemy turret's reach while it is not shooting an armed unit attacks
        /// that turret (ranged units from beyond it where they outrange it).
        bool turretPriority;
        /// A wave member whose engine Hunt ran out of visible targets hunts on towards the
        /// nearest enemy structure its house has seen, instead of standing in the field or
        /// walking back to the bundle; the next wave meets it there.
        bool pressOn;
        /// A ground wave goes for the weakest enemy house it knows of (remembered army and
        /// seen turrets), hunting towards that house's production and economy.
        bool finishWeakest;
        /// Against an objective guarded by many seen turrets a ready wave waits (at most three
        /// minutes) for a bigger army before it leaves.
        bool fortifiedWaves;
        /// A hunting sonic tank whose wave would pass over one of its own team's units (the
        /// wave runs about nine tiles and hurts all but sonic tanks) shoots the nearest other
        /// enemy in its reach with a clear lane instead.
        bool sonicClearLane;
        /// Round 2 late-game candidates (docs/formations.md, "Free-for-all: finishing the game").
        /// Banked credits above a reserve raise the production budget above the military limit
        /// (at most double), spent only on siege tanks, launchers and specials.
        bool spendAtCap;
        /// Wave launchers shell known enemy turrets from beyond the turrets' reach while the
        /// wave's other ground units hold short of that reach, guarding them.
        bool siegeStandoff;
        /// Against an objective guarded by many seen turrets the army waits (at most four
        /// minutes) for 70% of the military limit, then everything hunts towards one objective
        /// of the weakest known house.
        bool massAssault;
        /// On by default (1.0.848): a house whose team army leads the strongest enemy team's
        /// remembered army sends its ground waves at the weakest known house (FinishWeakest's
        /// choice) instead of the nearest.
        bool finishWhenLeading;
        /// Team play (Hard/Brutal): an allied QuantBot's whole-army wave launched within the last
        /// 30 s is joined by whatever viable wave we have, whatever our own readiness, at that
        /// wave's objective (docs/ai-scenario-matrix.md, "Team games"). Inert without allies.
        bool joinAllyWaves;
        /// Custom Hard/Brutal starts without a heavy factory establish a four-worker economy,
        /// then fund their first full combat wave before optional fleet/yard expansion. Initial
        /// heavy-factory starts, Easy/Medium and campaign games are unchanged.
        bool openingCombatBudget;
        /// The equivalent four-worker first-wave budget for a Hard/Brutal campaign partner of a
        /// human that started without a heavy factory. Enemy controllers and Easy/Medium stay as-is.
        bool campaignCombatBudget;
        /// Optional capacity MCVs retain the forecast costs of existing production plus its
        /// cash reserve. Real lost-yard and terrain-driven expansion recovery are exempt.
        /// Independent candidate, off until same-build outcome acceptance.
        bool capacityMcvRunway;
        /// Light-raider tactical orders leave repair and carryall-pickup ownership intact.
        /// Independent candidate; off until same-build mechanics/outcome acceptance.
        bool respectServiceOwnership;
        /// An undeployed MCV steps its own units off its Construction Yard footprint (and the
        /// ring hemming them in) instead of waiting for ever. On by default: a bug fix.
        bool clearMcvFootprint;
        /// 2026-10-31 tuning candidates (off until same-build acceptance).
        /// Short-map ladder: Hard/Brutal on small maps keep Medium's city-yard target and
        /// opening tempo instead of buying MCVs for a third yard.
        bool smallMapYardCap;
        /// Free-for-all finishing: the Palace's Death Hand aims at the densest cluster of seen
        /// enemy turrets guarding the house the waves are attacking.
        bool deathHandTurrets;
        /// Campaign survival: while an enemy can fire a Death Hand, the yard, refineries and
        /// factories are placed apart so one missile cannot take out several of them.
        bool spreadVsDeathHand;
        /// City defence: while uncovered core assets or enemy aircraft over the base demand
        /// rocket turrets that this yard cannot build yet, the next unlock step (the yard
        /// upgrade, then a missing prerequisite such as the Radar) becomes a protected capital
        /// project, so its price is saved instead of spent on bargains or plots.
        bool defencePrerequisites;
    };
    ForceSettings force;

    /// The strategy each Custom Hard/Brutal QuantBot plays (docs/ai-play-styles.md,
    /// "Strategies"): a unit-mix strategy and a tempo, each picked once per game from weights.
    /// INI: [Strategy] Mode = Random | Fixed:<mix>,<tempo>, MixWeights, TempoWeights; optional
    /// keys (not written by save()), the code defaults apply when absent. Hashed for multiplayer.
    QuantBotStrategy::Settings strategy;
    /// The Tuned mix strategy's starting ratios per house (docs/ai-play-styles.md, section 8),
    /// indexed by house id (Harkonnen, Atreides, Ordos, Fremen, Sardaukar, Mercenary) and tech band
    /// (0 = the house's top mix, used in every band without its own; 1-9 = the band whose first tech
    /// level that is, QuantBot::techBand) over the mix slots tank, siege tank, launcher, Devastator,
    /// Sonic Tank, Deviator, ornithopter, trike, raider trike, quad (percent).
    /// INI: [Unit Ratios Tuned] <House> = t,s,l,dv,so,de,o,tr,r,q and <House>_Tech<N> = ... or
    /// <House>_Tech<N> = Opening (that band plays the configured opening, bounded: a band whose
    /// tuned mix did not beat it); optional keys (not written by save()), the code defaults apply
    /// when absent; all zero = none (the house's top mix, or with none at all the configured opening).
    /// Hashed.
    static constexpr size_t kTunedBands = 10;
    std::array<std::array<UnitMixPolicy::Weights, kTunedBands>, 6> tunedRatios{};
    /// Bands (1-9) that play the configured opening although the house has a top mix.
    std::array<std::array<bool, kTunedBands>, 6> tunedOpening{};
    /// The tuned ratios of a house in the tech band starting at \a band: none (all zero) for an
    /// Opening band, else the band's own mix, else the house's top mix (renormalised over the band's
    /// slots when bounded); all zero for none.
    const UnitMixPolicy::Weights& getTunedRatios(int houseID, int band = 0) const;

    // === General AI behavior ===
    int attackTimerMs;              // Time between attack checks (milliseconds)
    float attackThresholdPercent;   // Don't attack until military reaches this % of limit (0.0-1.0)
    int minMoneyForProduction;      // Minimum money needed to queue production
    /// [General Behavior] StrategicSiteGuards: IX/Palace selections require a real site.
    /// Off reproduces the old overdue-order starvation for same-build comparisons. Hashed.
    bool strategicSiteGuards;
    /// How a launched ground attack wave travels (docs/formations.md, QuantBot waves):
    /// kWaveFormationNone keeps engine Hunt / today's per-unit orders, kWaveFormationBlob makes
    /// a Custom Hard/Brutal wave a hunt party that gathers, marches as one blob and releases into
    /// engine Hunt on contact ("Blob waves"); otherwise the FormationPlanner::Shape index the wave
    /// marches in as a hunting formation.
    /// INI: [General Behavior] WaveFormation = None (default) | Blob | Box | Pincer | Column | Staggered | Echelon.
    static constexpr int kWaveFormationNone = -1;
    /// Not a FormationPlanner::Shape: never passed to the planner (formationShapeOf()).
    static constexpr int kWaveFormationBlob = -3;
    int waveFormation;
    /// "None", "Blob" or a kept shape name (case-insensitive) to a waveFormation
    /// value; anything else returns 'fallback'.
    static int parseWaveFormation(const std::string& name, int fallback);
    static const char* waveFormationName(int value);
    /// The planner shape a waveFormation value stands for, for every formation move that is
    /// not a QuantBot hunt party (AIPlayer, CampaignAIPlayer, QuantBot's recall and campaign
    /// waves): Blob is not a shape, so it moves those groups like None.
    static int formationShapeOf(int waveFormation) {
        return waveFormation == kWaveFormationBlob ? kWaveFormationNone : waveFormation;
    }

    // Priority tables (configurable)
    const TargetPriority& getStructurePriority(int structureID) const;
    const TargetPriority& getUnitPriority(int unitID) const;
    const TargetPriority* findStructurePriority(const std::string& name) const;
    const TargetPriority* findUnitPriority(const std::string& name) const;
    
    // Constructor with default values
    QuantBotConfig();
    
    // Load from INI, upgrading writable user/draft files when defaults are missing.
    // Immutable revisions and checksummed family bundles use defaults in memory only.
    bool load(const std::string& filepath);

    /// Test-only (tests/ai play styles): applies the keys present in an INI file on top of this
    /// config - any QuantBot Config.ini key, the others keep their values - without creating or
    /// writing the file. False if the file does not exist.
    bool applyOverrides(const std::string& filepath);
    
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
    /// Reads every key present in iniFile over the current values; true if the priority tables
    /// differ from the file's (load() then rewrites it).
    bool readFrom(const INIFile& iniFile);

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
