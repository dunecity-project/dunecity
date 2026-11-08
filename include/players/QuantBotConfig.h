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
    /// FinishWhenLeading, JoinAllyWaves, OpeningCombatBudget, CampaignCombatBudget, CampaignPlayerHouse,
    /// CapacityMcvRunway,
    /// RespectServiceOwnership, ClearMcvFootprint, SmallMapYardCap, DeathHandTurrets,
    /// SpreadVsDeathHand, DefencePrerequisites, HardCityMatch, ResearchLab, ResearchLabProgression,
    /// CityGrowthPower, CityPowerSizing, CityOpeningCapital, CityConstructionCapital, CityProductionPrerequisites,
    /// CityFirstFactorySaving, ThreatTurrets, MoreRocketTurrets, CityStarportSupply,
    /// StorageCapacity, FundedClassicProduction, TrooperSupport, SimpleTechSavings,
    /// LauncherKiteExtraDistance, ExhaustedArmyDispatch;
    /// optional keys, hashed for multiplayer.
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
        /// Independent default-off vanilla multi-enemy candidates. Their original roster
        /// must contain at least two hostile teams: duels, two-team games and campaigns are inert.
        /// Hunt towards the weakest known rival even before our army leads the field.
        bool vanillaFfaWeakestTarget;
        /// Add travel cost to remembered rival strength, preferring a nearby opportunity.
        bool vanillaFfaNearbyTarget;
        /// Keep at least 25% of an available wave nearest the protected rally; real defence
        /// ownership and exhausted-economy finishing remain authoritative.
        bool vanillaFfaReserve;
        /// Wait for full readiness rather than the strategy's smaller repeated waves.
        bool vanillaFfaFullWaves;
        /// With a funded worker seed and local ground production, defer the first optional
        /// Starport until a ground wave launches; required first-wave/recovery capital stays legal.
        bool vanillaFfaStarportAfterWave;
        /// In vanilla FFA with at least five original players, below ordinary readiness,
        /// live ground fire funds replacements before optional growth, retaining the worker seed.
        bool vanillaFfaThreatProduction;
        /// Fresh waves reinforce the existing live army's reachable known structure objective.
        /// Uses checkpointed wave state; immediate combat and defence keep their authority.
        bool vanillaFfaSingleFront;
        /// Below ordinary readiness, let a funded light-unit order use its real price rather
        /// than the blanket 700-credit entry floor. Existing reserves and mix stay authoritative.
        bool vanillaFfaLightWorkingCash;
        /// In vanilla FFA with at least five original players, fund up to eight sustainable
        /// workers and ordinary readiness before optional IX/Palace savings or orders.
        bool vanillaFfaFundedTech;
        /// Fixed, default-OFF vanilla opening families; each kind has only its preregistered levels.
        bool vanillaHarvesterBoom;
        bool vanillaRefineryBoom;
        bool vanillaGreedyBoom;
        bool vanillaReactiveBoom;
        /// At least three original occupied hostile controllers and multiple hostile teams.
        bool vanillaBoomFfaOnly;
        int vanillaHarvesterBoomWorkers;       ///< 8 or 12, with two refineries.
        int vanillaRefineryBoomRefineries;     ///< 4 or 6, with two workers per refinery.
        int vanillaGreedyBoomMinutes;          ///< Stop at 6, 10 or 12 minutes; fund army until minute 20.
        int vanillaReactiveArmyRatioBps;       ///< 6000 or 9000 of the strongest reachable enemy army.
        int vanillaReactiveEconomyRatioBps;    ///< 12000 or 15000 of the leading hostile economy.
        /// Independent default-OFF Custom Brutal Vanilla FFA4/6 policies (docs/ai-play-styles.md,
        /// "Vanilla FFA core defence and army counters"); neither enables boom.
        bool vanillaCoreDefence;               ///< Guards and turrets for threatened refineries/combat factories.
        int vanillaCoreDefenceThreatBps;       ///< Exactly 5000 or 10000 of the strongest hostile team's live threat.
        bool vanillaArmyCounters;              ///< Unit mix blended toward counters of the strongest reachable team.
        int vanillaArmyCounterBlendBps;        ///< Exactly 5000 or 10000 counter-composition blend.
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
        /// The campaign's selected player house uses normal player-side economy and attacks,
        /// even when its QuantBot is the sole controller. Authored enemy houses are unchanged.
        bool campaignPlayerHouse;
        /// Campaign player-side openings establish a combat producer before repeat refineries.
        /// Hashed; default on. OFF retains the existing economy-first opening.
        bool campaignMilitaryOpening;
        /// Classic campaign player-side readiness scales to enemy strength from the first wave.
        /// Hashed; default on. OFF retains the fifteen-minute fixed opening floor; cities are unchanged.
        bool campaignAdaptiveReadiness;
        /// A campaign player's ground producers can spend economic savings while
        /// visible attackers are firing on its income/base. Hashed; default on.
        bool campaignThreatProduction;
        /// Delay the campaign opening Light Factory until its first unit and
        /// ordinary producer cash protection are funded. Hashed; default on.
        bool campaignFundedLightOpening;
        /// Before heavy-factory technology, campaign light production funds growth
        /// from real uncommitted cash instead of a 2,000-credit economic buffer.
        /// Hashed; default on. OFF retains the 2,000-credit economic buffer.
        bool campaignLightWorkingCash;
        /// Working-cash light production protects the second refinery's quote
        /// until that refinery is queued, without waiting for worker delivery.
        /// Hashed; default on. OFF retains unrestricted working-cash light production.
        bool campaignLightIncomeSeed;
        /// Classic campaign support buys a usable first vehicle factory before the Starport ladder.
        /// Independent hashed candidate, default off.
        bool campaignFactoryBeforeStarport;
        /// Classic campaign support uses idle uncommitted cash for its first vehicle factory.
        /// Hashed; default on at 2,000 credits after paired campaign and official AAA2 acceptance.
        bool campaignIdleCashFactory;
        int campaignIdleCashFactoryCredits;
        /// Reuse whole-army assignments for an attacked campaign refinery and protect replacement funding.
        /// Independent hashed candidate, default off; city and skirmish games are unchanged.
        bool campaignRefineryDefence;
        /// Save the first campaign light factory's founded quote and first unit before extra refineries.
        /// Independent hashed candidate, default off.
        bool campaignFactoryRunway;
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
        /// Custom DuneCity games, Hard only (QuantBotCityPolicy): the city population ceiling is
        /// 20,000 / 40,000 / 60,000 / 90,000 by map size, raised to the largest other house's
        /// city while that one is bigger. Off: the fixed 20,000 / 40,000 / 80,000 / 120,000.
        /// On by default (Stefan, 2026-11-01).
        bool hardCityMatch;
        /// With the match's Research Lab option, build/rebuild one lab after essentials
        /// and fund research from cash not already committed to accepted production.
        /// On by default; inert when the match option is off.
        bool researchLab;
        /// Treat research-enabled matches as technology progression, not optional
        /// capital behind the higher-tech opening fleet. Hashed; no saved state.
        bool researchLabProgression;
        /// Reserve a legal generator before demanded city growth runs out of its 24-power
        /// headroom, instead of allowing military savings and bargains to starve it.
        /// On by default; no effect outside city games or while generation is pending.
        bool cityGrowthPower;
        /// Size reserve-power purchases against useful load and protected working cash
        /// instead of turning every affordable windtrap into a 2,000-credit reactor.
        /// Funded installment projects and emergency recovery retain their existing priority.
        bool cityPowerSizing;
        /// Protect tax-dependent optional yards and producer upgrades with the
        /// existing four-minute working-capital forecast. Viable spice economies
        /// retain their normal budgets; military and required recovery are unchanged.
        bool cityOpeningCapital;
        /// Protect Hard/Brutal's affordable first heavy factory in a seeded tax
        /// city with a standing Starport. CityFirstFactorySaving optionally
        /// permits forecast-funded saving; seed, power and recovery gates remain.
        bool cityConstructionCapital;
        /// Reserve a missing production prerequisite before the factory/repair
        /// plot it unlocks, so future footprints cannot exclude their own supplier.
        /// Standing or queued suppliers do not reserve another speculative plot.
        bool cityProductionPrerequisites;
        /// Experimental/default off: a seeded Hard/Brutal multi-yard tax city
        /// may save below its first heavy factory's quote when forecast net
        /// income covers the founded quote plus all queued production debt.
        bool cityFirstFactorySaving;
        /// Experimental/default off: a spice-backed Custom city under direct
        /// attack keeps military capital ahead of the ordinary growth interleave.
        bool cityThreatBudget;
        /// Experimental/default off: reactive ground cover also protects city
        /// zones in spice-backed Custom city games, within the existing turret cap.
        bool cityZoneDefence;
        /// Experimental/default off: at tech 9, fund the first IX/prerequisite
        /// before repeatable city/military capacity in a solvent spice-backed city.
        bool cityAdvancedTech;
        /// Experimental/default off: continue funding city research after the
        /// opening fleet, retaining power, income, bootstrap and live-threat gates.
        bool cityContinuousResearch;
        /// Experimental/default off: wind down extra workers and refinery bays
        /// when the working fleet can exhaust its remaining spice share shortly.
        bool citySpiceTransition;
        /// Experimental/default off: optional MCVs and producer upgrades retain
        /// working capital in spice-backed Custom cities as well as tax cities.
        bool citySpiceCapacityRunway;
        /// Experimental/default off: a single yard reserves profitable growth
        /// against two completed ordinary infrastructure orders per extra zone.
        bool citySingleYardGrowth;
        /// Experimental/default off: cash-constrained spice cities reserve one
        /// zone's latent growth per yard, not full maturity of every standing lot.
        bool cityPowerGrowthHorizon;
        /// Buy bounded, locally sited rocket cover against visible ground raids,
        /// without waiting for the peaceful production-core/refinery gates.
        bool threatTurrets;
        /// Experimental, default off: fund a healthy tax city's standing battery
        /// and full unlock bridge; denser local ground cover retains raid caps.
        bool moreRocketTurrets;
        /// Protect a solvent city's first useful Starport from repeat zoning and
        /// factory orders. The no-spice seed economy still comes first.
        bool cityStarportSupply;
        /// Bank forecast income before it outgrows physical storage, after useful
        /// production spending. One safe storage project at a time; use a buildable
        /// refinery for its vault when the cheaper Silo is not unlocked yet.
        bool storageCapacity;
        /// Default on: powered non-city Custom Brutal games use the established
        /// funded spice policy for workers, production and capacity, not campaigns.
        /// Engine power requirements and the existing mix learning remain unchanged.
        bool fundedClassicProduction;
        /// DuneCity-content Custom games: bounded rocket infantry for aircraft,
        /// armour and escorts, using existing infantry shares/economy reserves.
        /// Default ON at Stefan's direction despite unproven acceptance; see HANDOVER.
        bool trooperSupport;
        /// Stateless existing-composition/technology experiment, default OFF.
        /// Current paired outcome audits reject adoption; see HANDOVER.
        bool simpleTechSavings;
        /// Extra tiles in a launcher spacing step and its Hunt stand-off (0 = legacy).
        /// Clamped to 0..2; other units keep their existing retreat distance.
        int launcherKiteExtraDistance;
        /// Default on: Custom noncampaign armies can dispatch without rebuild-only
        /// holds once reachable spice, affordable units and all income/cargo are exhausted.
        /// Manual orders, defence and saboteur ownership remain authoritative.
        bool exhaustedArmyDispatch;
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
    /// [General Behavior] PoliceFundingRecovery: restore small affordable funding
    /// increases after a longer review instead of leaving them in the deadband.
    /// Default on; off preserves the previous smoothing policy. Hashed.
    bool policeFundingRecovery;
    /// [General Behavior] DeviatedHarvesterEarlyReturn: earn captured cargo at
    /// half capacity or T-10s, without overriding human/role claims. Default on; hashed.
    bool deviatedHarvesterEarlyReturn;
    /// [Civ Wars] Strategy: experimental research-aware economy and generated-map logistics.
    /// Defaults off; opt-in requires two occupied opposing initial houses and Research Lab.
    /// Lane assumptions also require the map marker. Hashed and inert outside those rules.
    bool civWarsStrategy;
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
