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

#ifndef QuantBot_H
#define QuantBot_H

#include <players/Player.h>
#include <players/CityPlanningPolicy.h>
#include <players/CityDistanceField.h>
#include <players/CityServiceInvestmentPolicy.h>
#include <players/CombatReward.h>
#include <players/GroundAccessPolicy.h>
#include <players/UnitMixPolicy.h>
#include <players/QuantBotColonisationPolicy.h>
#include <units/MCV.h>
class Harvester;
#include <players/QuantBotConfig.h>
#include <players/QuantBotCityCampaignPolicy.h>
#include <players/CampaignDifficultyPolicy.h>
#include <players/ArmyPosturePolicy.h>
#include <players/BlobWavePolicy.h>
#include <players/ForceConcentrationPolicy.h>
#include <players/CombatPowerPolicy.h>
#include <players/FrontBatteryPolicy.h>
#include <players/UnitMixPolicy.h>
#include <players/AIDecisionLog.h>
#include <dunecity/SpatialFields.h>

#include <DataTypes.h>
#include <limits>
#include <set>
#include <map>
#include <unordered_map>
#include <memory>
#include <array>
#include <optional>
#include <vector>
#include <list>

class QuantBot : public Player
{
public:
    enum class Difficulty {
        Easy = 0,
        Medium = 1,
        Hard = 2,
        Brutal = 3,
        Defend = 4
    };

    enum class GameMode {
        Custom = 4,
        Campaign = 5
    };

    QuantBot(House* associatedHouse, const std::string& playername, Difficulty difficulty, bool supportModeEnabled = false);
    QuantBot(InputStream& stream, House* associatedHouse);
    void init();
    ~QuantBot();
    void save(OutputStream& stream) const override;
    void saveObserverRuntime(OutputStream& stream) const;
    void loadObserverRuntime(InputStream& stream);

    void update() override;
    void onHumanUnitOrder(Uint32 id);
    /// Uses the existing human-order lease to distinguish AI orders in shared houses.
    bool managesAutonomousOrnithopter(const UnitBase* unit) const;
    /**
        Offensive air hunts are a Hard and Brutal behaviour. Every other
        autonomous difficulty may only answer an attack on
        something this house owns, plus a strike on an enemy base building with
        no observed anti-air over it. Keyed to the difficulty rather than the
        INI flag, so a settings copy cannot re-enable the restriction.
    */
    bool autonomousAircraftRaids() const;
    /**
        Is this candidate a legal target for one of our autonomous aircraft?
        Bounded lookups: the raid permission, what the air planner
        authorized for this hull on its last pass, and the candidate's own
        current target. Engine target searches, held targets and the shot
        itself all consult this, so the restriction cannot be bypassed.
    */
    bool authorizesAircraftTarget(const UnitBase* aircraft, const ObjectBase* candidate) const;
    /// Controller authority for autonomous launcher target searches.
    bool managesAutonomousLauncherHunt(const UnitBase* unit) const;
    /// Controller authority for autonomous rocket target searches: launchers and the
    /// Troopers that answer aircraft with their own shorter-range SmallRocket. Used
    /// only by the in-range aircraft candidate filter, which both need for the same
    /// out-of-range release, not by launcher spacing or wave tracking.
    bool managesAutonomousRocketHunt(const UnitBase* unit) const;
    void onScriptedReinforcement(const UnitBase* unit);
    void finishTelemetry() override;
    void onCombatReward(Uint32 attacker, Uint32 target, const CombatReward::Totals& reward) override;
    /**
        Drops the cached unit-mix performance window when the House-side learning ledger is
        reset for measured Deviator scoring, so the two cannot disagree after a legacy load.
    */
    void resetLearningForMeasuredScoring();

    /// Observational data for the compact end-of-match metaserver summary.
    /// It is not saved or consulted by simulation decisions.
    const UnitMixPolicy::Mix& getLastUnitMixBps() const { return lastUnitMixBps; }
    /// The unit of each mix slot (UnitMixPolicy::kSlots order).
    static constexpr std::array<Uint32, UnitMixPolicy::kSlots> kMixItems = {Unit_Tank, Unit_SiegeTank,
        Unit_Launcher, Unit_Devastator, Unit_SonicTank, Unit_Deviator, Unit_Ornithopter, Unit_Trike,
        Unit_RaiderTrike, Unit_Quad};

    /**
        Read-only record of the allocation the last production pass actually used: the
        same performance scores and the same intended army-value shares that produced
        lastUnitMixBps, taken at that pass. Purely observational - nothing in the
        simulation reads it, and a reader must never recompute learning from it.
        Slot order is the production mix order: Tank, Siege Tank, Launcher, Devastator,
        Sonic Tank, Deviator, Ornithopter, Trike, Raider Trike, Quad (UnitMixPolicy::kSlots).
    */
    struct AllocationSnapshot {
        struct Slot {
            Uint32 itemID = 0;       ///< Representative item of this mix slot.
            int64_t score = 0;       ///< UnitMixPolicy::performanceScore() as used.
            int targetBps = 0;       ///< Intended share of army value, basis points.
            bool allocated = false;  ///< Producible in that pass; otherwise no target exists.
        };
        std::array<Slot, UnitMixPolicy::kSlots> slots{};
        bool ready = false;          ///< A real production pass has filled this.
        Uint32 cycle = 0;            ///< Game cycle of that pass.
        bool learning = false;       ///< Scores came from the measured learning window.
        int militaryValue = 0;       ///< Army value the shares were measured against.
        /// Mobile anti-air share protected in that pass while a hostile wing was
        /// observed, in basis points; zero when no floor was in force.
        int antiAirFloorBps = 0;
        /// Aggregate infantry admission quota, separate from the unit-mix slots.
        int infantryQuotaBps = 0;
    };
    const AllocationSnapshot& getAllocationSnapshot() const { return allocationSnapshot; }
    std::string getDifficultyName() const;
    bool permitsPoliceReinforcement(int unitValue) const;
    /// True when the lobby selected an explicit "Maximum Number of Units Override"
    /// (0 = unlimited, positive = that many). -1 leaves the map/ObjectData default.
    bool hasExplicitUnitCountOverride() const;
    /// Hard keeps its authored behaviour. Brutal only ignores the count ceiling while
    /// no explicit override exists; with one it honours the engine limit, which an
    /// override of 0 reports as unlimited anyway.
    bool ignoresUnitCountLimit() const;
    /// Brutal plus an explicit override plans against rolling headroom instead of the
    /// configured military value cap. Nothing else about the difficulty changes.
    bool overridesMilitaryValueCap() const;
    /// Effective planning budget for this build pass. Equals militaryValueLimit unless
    /// overridesMilitaryValueCap(), which never mutates the serialized field.
    int planningMilitaryBudget(int committedValue, int productionCash, int largestUnitValue) const;
    bool isAlliedWithHuman() const;
    int harvesterCountCeiling() const;
    /// The difficulty's configured harvester limit for this map's size class (HarvesterLimit*Map)
    /// in a custom game the house plays itself; 0 elsewhere (no ceiling from the config).
    int configuredCustomHarvesterLimit() const;
    int getCityPopulationLimit(int mapArea) const override;
    int campaignAllyHarvesterLimit() const;
    bool canAddRepairYard(int includingQueued) const;

    // Only opposing QuantBots in Dune City campaigns use this policy.
    QuantBotCityCampaignPolicy::Limits campaignCityLimits() const;
    bool campaignCityEconomy() const;
    bool campaignPermitsStructure(Uint32 itemID) const;
    bool campaignAllowsZone(int zonesIncludingQueued) const;
    bool campaignCanAddHarvester() const;
    int campaignHarvesterCeiling() const; // -1 outside policy; zero forbids new workers
    void doProduceItem(const BuilderBase* builder, Uint32 itemID) const;

    void onObjectWasBuilt(const ObjectBase* pObject) override;
    void onDecrementStructures(int itemID, const Coord& location) override;
    void onDecrementUnits(int itemID) override;
    void onIncrementUnitKills(int itemID) override;
    void onHostileUnitKilled(Uint32 itemID, Uint32 originalHouseID) override;
    void onHostileStructureDestroyed(Uint32 itemID, Uint32 originalHouseID) override;
    void onDamage(const ObjectBase* pObject, int damage, Uint32 damagerID) override;

private:

    struct OrnithopterStrikeTeam {
        int minMembers = 0;
        Uint32 targetId = 0;
        std::set<Uint32> memberIds;

        bool isActive() const {
            return targetId != 0 && !memberIds.empty();
        }

        void reset() {
            minMembers = 0;
            targetId = 0;
            memberIds.clear();
        }

        void setTarget(Uint32 newTargetId, int requiredMembers) {
            targetId = newTargetId;
            minMembers = requiredMembers;
        }
    };

    Difficulty difficulty;  ///< difficulty level
    GameMode  gameMode;     ///< game mode (custom or campaign)
    Sint32  buildTimer;     ///< When to build the next structure/unit
    Sint32  attackTimer;    ///< When to build the next structure/unit
    Sint32  retreatTimer;   ///< When you last retreated>

    int initialItemCount[Num_ItemID]{};
    // Negative until the first update, after scenario/save objects are loaded.
    // This sentinel also survives saving before a newly added partner updates.
    int initialMilitaryValue = -1;
    int militaryValueLimit = 0;
    int harvesterLimit = 4;
    int lastCalculatedSpice = 0;
    bool campaignAIAttackFlag = false;
    // Legacy squad fields retained for save compatibility; released on first update.
    Uint32 groundSquadPhase = 0, groundSquadStarted = 0, groundSquadNextControl = 0;
    Uint32 groundSquadInitialCount = 0, groundSquadObjective = NONE_ID, groundSquadObjectiveCycle = 0;
    Uint32 groundSquadProgressCycle = 0;
    Coord groundSquadProgressLocation = Coord::Invalid();
    UnitMixPolicy::PerformanceHistory performanceHistory;
    std::set<Uint32> groundSquad;
    std::map<Uint32, Uint32> manualUnitOrders, defenceAssignments;
    // Air difficulty policy, both transient: what the air planner authorized
    // for each of our aircraft on its last pass, and the hostile ids last seen
    // actually hitting a building or worker we own. Ordinary disk saves rebuild
    // plans; the supplemental observer checkpoint preserves these permissions
    // so a peer joining mid-sortie makes the same immediate targeting decisions.
    std::map<Uint32, Uint32> aircraftTargetAuthority;
    struct AssetContact { Uint32 cycle = 0; bool base = false; };
    std::map<Uint32, AssetContact> assetAttackerContacts;
    /// A contact that is itself attacking a live asset of ours, or hit one
    /// within the grace below. The grace is what stops a reload being read as
    /// the end of an attack.
    bool aircraftDefenceTarget(const ObjectBase* candidate) const;
    static constexpr int kAircraftDefenceGraceMs = 3000;
    void launchGroundHunt();
    CampaignDifficultyPolicy::Wave campaignWave;
    std::set<Uint32> scriptedAssaults;
    bool isCampaignEnemy() const;
    QuantBotCityCampaignPolicy::Baseline campaignBaseline;
    std::set<Uint32> campaignOriginalStructures;
    bool campaignBaselineCaptured = false;
    bool campaignMapHasSpice = true;
    Uint32 campaignSpiceZeroSince = std::numeric_limits<Uint32>::max();
    void noteCampaignOriginalState(bool legacySave = false);
    int campaignIncomeForecastPerMinute() const;
    int campaignCommittedCount(Uint32 itemID) const;
    int campaignHarvesterTarget() const;
    bool campaignPostSpice() const;
    bool campaignAvailableToBuild(const BuilderBase* builder, Uint32 itemID) const;
    CampaignDifficultyPolicy::Profile campaignProfile() const;
    CampaignDifficultyPolicy::Pressure campaignPressure() const;
    bool campaignCanLaunch() const;
    int campaignRequiredArmy(int configuredThreshold) const;
    bool campaignCombatUnit(const UnitBase* unit) const;
    bool reserveDamagedUnitForRepair(const UnitBase* unit) const;
    bool campaignLocalContact(const ObjectBase* target) const;
    bool campaignDefensiveContact(const UnitBase* unit, const ObjectBase* target) const;
    bool campaignControlsUnit(const UnitBase* unit);
    bool scoutCampaignFront(const UnitBase* unit);
    void updateCampaignWave();
    void holdCampaignUnit(const UnitBase* unit);
    const ObjectBase* campaignObjective(const UnitBase* unit, int group) const;
    void releaseLegacyGroundSquad();
    std::map<Uint32,Uint32> defenceResponseCycles;
    bool humanControls(const UnitBase* unit) const;
    /// Who holds a unit's orders. Each claim keeps its own registry - human
    /// orders (manualUnitOrders), the repair reservation, defenceAssignments,
    /// customWave.members and the per-cycle colonistEscorts() and
    /// colonyGuardPosts() - and unitRole() is the one place controllers ask
    /// whether a unit holds one. Only the controller that grants a claim
    /// iterates, updates or releases its registry. Listed by authority, highest
    /// first; when a unit holds several of the claims asked for, unitRole()
    /// reports the first. The three defence roles are different readings of one
    /// defenceAssignments entry.
    enum class UnitRole : Uint8 {
        Human,       ///< A recent human order (humanControls()).
        Repair,      ///< Held back for the repair yard (reserveDamagedUnitForRepair()).
        BaseDefence, ///< Hunting an attacker of a building we own (baseDefenceAssignment()).
        LiveDefence, ///< Answering a live emergency or anti-air contact (activeDefenceAssignment()).
        Defence,     ///< Booked by base or air defence, live or not (a defenceAssignments entry).
        Wave,        ///< A member of the Custom Hard/Brutal offensive (customWave.members).
        Escort,      ///< Escorting a colonist (colonistEscorts()).
        ColonyGuard, ///< Holding a colony while recovery is active (colonyGuardPosts()).
        Free,
    };
    /// The claims a controller defers to.
    struct UnitRoles {
        constexpr UnitRoles(std::initializer_list<UnitRole> roles) {
            for (const UnitRole role : roles) bits |= 1u << static_cast<unsigned>(role);
        }
        constexpr bool contains(UnitRole role) const { return (bits >> static_cast<unsigned>(role)) & 1u; }
        unsigned bits = 0;
    };
    /// The highest-authority claim in \a among that \a unit holds, or Free.
    /// Only the claims in \a among are evaluated. Escorts and colony guards are
    /// derived on first use in each game cycle, so asking for them fixes them
    /// for the rest of that cycle: ask only for the claims a controller defers to.
    UnitRole unitRole(const UnitBase* unit, UnitRoles among) const;
    Coord squadRallyLocation = Coord::Invalid();
    Uint32 rallySelectedCycle = std::numeric_limits<Uint32>::max();
    Uint32 nonServiceConstructionOrders = 3; // Respond immediately to a new crime emergency.
    Uint32 powerDemandSampleCycle = 0;
    Sint32 powerDemandSample = 0;
    Sint32 projectedPowerDemandGrowth = 0;
    Coord squadRetreatLocation = Coord::Invalid();
    bool supportMode = false;
    Uint32 lastStatsLogCycle = 0;
    Uint32 lastPoliceBudgetReviewCycle = 0;
    Uint32 lastTelemetrySnapshotCycle = 0;
    Uint32 lastCityBuildingSnapshotCycle = 0;
    uint64_t telemetryState = 0; // Runtime only; never part of save/simulation state.
    UnitMixPolicy::Mix lastUnitMixBps{};
    /// Written once per production pass beside lastUnitMixBps, read only by the
    /// observational Stats UI and the supplemental observer runtime.
    AllocationSnapshot allocationSnapshot{};
    // Diagnostic de-duplication only. These must never affect a game decision,
    // save, or lockstep state.
    std::map<Uint32, uint64_t> lastKiteTrace;
    std::map<Uint32, uint64_t> lastMcvTrace;
    std::map<Uint32, uint64_t> lastHarvesterSafetyTrace;
    std::map<Uint32, std::pair<uint64_t, Uint32>> lastHeavyAllocationTrace;
    std::map<Uint32, AITelemetry::Record> placementScoreDetails;
    std::map<Uint32, Uint32> lastEconomyTraceCycle;
    std::map<Uint32, uint64_t> zoneDecisionIds;
    std::map<Uint32, Uint32> lastZoneTraceCycle;
    uint64_t traceDecision(const std::string& event, AITelemetry::Record details) const;

    Uint32 ixEligibleSinceCycle = std::numeric_limits<Uint32>::max();
    Uint32 palaceEligibleSinceCycle = std::numeric_limits<Uint32>::max();
    
    std::map<Uint32, int> idleHarvesterCounters; ///< Track idle time for each harvester (objectID -> cycle count)
    std::map<Uint32, int> harvesterMovingCounters; ///< Track continuous movement time (objectID -> cycle count)

    void scrambleUnitsAndDefend(const ObjectBase* pIntruder, bool clearingSpice = false,
                                const ObjectBase* protectedAsset = nullptr);

    /// Aircraft attacking any building we own — the main base or an outlying
    /// colony — are answered by the units that can actually shoot them down.
    /// Runs on the ordinary checkAllUnits cadence, records its responders in
    /// the saved defenceAssignments map and adds no state of its own.
    void defendStructuresFromAircraft();
    /// May this unit be committed to shooting an aircraft down? Human orders,
    /// helper mode, workers, saboteurs and units needed at the repair yard are
    /// never taken; an ordinary ground skirmish or rally order is.
    bool availableAirDefender(const UnitBase* unit, const UnitBase* aircraft) const;
    /// Reachable ground near \a victim that \a unit can defend from.
    /// Search from this unit, respecting terrain and structures. Invalid when there
    /// is none: the tile the aircraft is flying over may be a mountain or a
    /// building, and is never an order for a ground unit.
    Coord findAntiAirFiringPosition(const UnitBase* unit, const StructureBase* victim) const;
    /// Is this rescue still live — the aircraft still attacking something of
    /// ours, or still inside this defender's weapon range?
    bool airAttackContinues(const UnitBase* defender, const ObjectBase* aircraft) const;
    /// How far a defender will travel to reach an attacked building, and how
    /// many answer one aircraft. Both bound the work this pass can create.
    static constexpr int kAirRescueRadius = 40;
    static constexpr int kAirRescueDefenders = 3;
    /// How far an attack on one of our own buildings or workers pulls troops
    /// from. Scaled by the observed local threat between these bounds, so a
    /// raid stays a district response and a real assault reaches the army.
    static constexpr int kEmergencyBandMin = 12;
    static constexpr int kEmergencyBandMax = 40;
    static int emergencyResponseRadius(int threatValue);

    /// Does this house answer a real attack on a building it owns with its whole
    /// ground army? Scoped exactly like the whole-army dispatch and the hunting
    /// launcher spacing - Custom Medium/Hard/Brutal, no campaign, no helper - and
    /// deliberately independent of the optional recovery policy.
    bool wholeArmyBaseDefence() const;
    /// Is this an autonomous Custom Medium/Hard/Brutal controller? No campaign, no
    /// support/helper house. The shared scope of the whole-army behaviours (one
    /// concentrated wave instead of Easy's standing raid share) and of the mobile
    /// anti-air floor (docs/ai-scenario-matrix.md, "Difficulty ladder").
    bool autonomousCustomArmy() const;
    /// Does this house play a production strategy (QuantBotStrategy: the default Fixed:Tuned,Swarm
    /// mix and tempo)? Custom Hard and Brutal only: the strategies were tuned there, and Custom
    /// Medium keeps its learned mix and the configured wave readiness, so the strategy is one of
    /// Hard's real advantages over Medium (docs/ai-scenario-matrix.md, "Difficulty separation").
    bool playsStrategy() const;
    /// Count of hostile Ornithopters this house can actually see right now:
    /// alive, active and visible to our team. No fog cheating, and no sticky
    /// "was once attacked from the air" flag - the demand disappears with the
    /// aircraft. One bounded pass, used only where the unit mix is recomputed.
    int visibleHostileAircraft() const;
    /// Is \a contact right now shooting at a building we own? A live hostile
    /// attacker, our structure as its target, and inside weapon range of it. A dead,
    /// stopped, friendly or merely nearby enemy is not.
    bool attackingOwnedBuilding(const ObjectBase* contact) const;
    /// Was this unit scrambled to an attacked building and is it still on that
    /// job (walking there or fighting)? Such a unit is not scrambled again.
    bool baseDefenceAssignment(const UnitBase* unit) const;
    /// Custom Hard/Brutal answer to an attack on a building we own - the main
    /// base or an outlying colony, including an R/C/I zone. The free ground
    /// combat troops nearest to \a building, within the threat-scaled
    /// emergencyResponseRadius() band, are sent to it with a Hunt order, nearest
    /// first, until together with the troops already in that fight they outweigh
    /// the local threat (SimpleArmyPolicy::reinforcements). They walk there with
    /// UnitBase::doHuntToward, so Hunt overrides the move: they attack any enemy
    /// they meet on the way. A unit already under a forced order (a spacing
    /// step, a transit) is not moved, and a scrambled unit is not scrambled
    /// again until it has finished: not walking, not fighting, and no enemy left
    /// at its building. Human orders, repair runs, booked carryall pickups,
    /// workers, builders, transports and aircraft keep their units.
    void scrambleWholeArmyToDefend(const ObjectBase* intruder, const ObjectBase* building);
    /// How close to the attacked building a troop's own fight, or the enemy
    /// strength it faces, counts as this battle.
    static constexpr int kBaseScrambleContactTiles = 8;


    Coord findMcvPlaceLocation(const MCV* pMCV);
    /// \a needsLocalSpace states that this MCV has already failed to find a
    /// site on the rock the base stands on. The core prerequisite below then
    /// cannot be satisfied where it stands, so it is waived for this query.
    Coord findRockExpansionSite(const MCV* mcv = nullptr, bool needsLocalSpace = false);
    /// Orders one undeployed MCV: keep its remembered site, drive there, deploy.
    void manageMcv(const MCV* pMCV);
    /// A reachable 2x2 yard footprint on the rock formation the base already
    /// occupies, or invalid when this formation has no room left. \a current
    /// is the site this MCV is already driving at; it is kept while it stays
    /// reachable rock of ours.
    Coord findLocalDeploySite(const MCV* pMCV, Coord current = Coord::Invalid());
    /// Is this remembered site still a legal yard footprint for this MCV?
    /// Only permanent obstacles count, so passing traffic never rerolls it.
    bool mcvSiteUsable(const MCV* pMCV, Coord site) const;
    /// May this MCV turn into a yard where it stands right now?
    bool mcvMayDeployHere(const MCV* pMCV, bool expansion);
    /// Does this footprint share a rock formation with one of our structures?
    bool onOwnRockFormation(Coord site) const;
    /// Sites remembered by our other, still undeployed MCVs.
    std::vector<Coord> otherMcvSites(const MCV* mcv) const;
    /// Has the base built out its own rock, so that further growth needs a new
    /// formation? Read from the last rock survey, never from a memory of an
    /// earlier one: clearing an enemy out restores the ground it denied us.
    bool baseBuiltOut() const;
    /// Should another MCV be bought to settle free rock? This is colonisation
    /// only: local production yards and replacing a lost yard are separate.
    /// \a mcvsIncludingQueued counts MCVs alive, paid for and queued, and
    /// \a yardLimit is the game-option ceiling (0 = none).
    bool colonisationMcvDue(int mcvsIncludingQueued, int yardLimit) const;
    /// Inputs shared by the colonist purchase and the per-MCV mission decision.
    QuantBotColonisationPolicy::Demand colonisationDemand(int mcvsIncludingQueued, int yardLimit) const;
    /// May a colonist set out for the surveyed new rock now? rockExpansionSite
    /// is where new rock lies; this adds the expansion defence gate and the
    /// colonists already lost on the way.
    bool colonySiteOpen() const;
    /// Is this MCV's job settling another formation rather than growing the
    /// base? A built-out base, or a formation already running its share of
    /// yards, sends it to a surveyed destination when one exists.
    bool colonyMissionDue(const MCV* pMCV) const;
    /// Our construction yards on the rock formation containing \a at.
    int yardsOnFormation(Coord at) const;
    /// Distinct rock formations holding at least one of our yards.
    int yardFormations() const;
    Uint32 rockSurveyCycle = std::numeric_limits<Uint32>::max();
    Coord rockExpansionSite = Coord::Invalid();
    int availableBaseRock = 0;
    /// Building slots left on the base's own rock, and whether the placement
    /// search can still find room for another production building. Both are
    /// refreshed by the rock survey and are part of the observer checkpoint.
    int availableBaseFootprints = 0;
    bool baseProductionRoomBlocked = false;
    Uint32 refineryQueueSince = std::numeric_limits<Uint32>::max();
    std::unordered_map<Uint32,Coord> mcvExpansionSites;
    std::unordered_map<Uint32,Uint32> mcvSurveyCycles;
    /// Local deploy search window, and the free rock wanted around a new yard.
    static constexpr int kMcvLocalRadius = 20;
    static constexpr int kMcvDeployRoom = 12;
    Coord findPlaceLocation(Uint32 itemID);
    /// `clearedZones` are object ids this placement is about to demolish; their
    /// tiles count as passable, because by the time the building stands they
    /// are. Pass nothing for an ordinary placement.
    bool preservesGroundAccess(Uint32 item, Coord pos,
                               const std::vector<Uint32>* clearedZones = nullptr);
    void clearPlacementCache(bool geometryChanged = true, bool reuseForBuilder = false);
    Coord findRedevelopmentSite(Uint32 itemID);
    bool redevelopmentZones(Uint32 itemID, Coord pos, std::vector<Uint32>& zones) const;
    Coord findPlaceLocationSimple(Uint32 itemID);
    Coord findSlabPlaceLocation(Uint32 itemID);
    Coord findTurretPlaceLocation(Uint32 itemID);
    bool selectCityServiceInvestment(const BuilderBase* builder, int money, bool emergency,
                                    Uint32& item, Coord& site, bool landValueOnly = false, Uint32 requiredItem = NONE_ID);
    Coord findCityTurretPlaceLocation(Uint32 itemID, int* defenseScore = nullptr, int* amenityScore = nullptr,
                                      int* crimeBenefit = nullptr, int* crimeHotspot = nullptr);

    Coord findEffectiveTurretPlaceLocation(Uint32 itemID);
    /// Site for an enemy-facing battery emplacement, in city mode as well as
    /// outside it. The ordinary city coverage planner keeps every other
    /// rocket-turret decision; see the definition for why the battery needs its
    /// own search rather than reusing a coverage score that has no front.
    Coord findFrontBatteryPlaceLocation();
    /// Last resort for an established city with no free legal ground left on
    /// the side the enemy comes from: a tile of an eligible own R/C/I lot,
    /// preferring lower displacement cost. Invalid - and no lot is
    /// ever touched - whenever findFrontBatteryPlaceLocation() has an answer,
    /// whenever the battery rule's own gates are shut, and outside Custom
    /// Hard/Brutal city mode. Choosing this site commits nothing: the lot is
    /// still standing when the order is accepted and is displaced only by
    /// commitBatteryClearance(), at the moment finished material is placed on
    /// it.
    ///
    /// With a valid `requiredSite` the same search becomes a re-validation of
    /// that one tile and returns it only if every rule still holds; this is
    /// how the commit re-applies the whole decision to the live map instead of
    /// keeping a second copy of its conditions.
    Coord findBatteryClearanceSite(Coord requiredSite = Coord::Invalid());
    /// The single own R/C/I lot an emplacement at `pos` would displace, or false
    /// if that tile is not a legal clearance candidate. Revalidates ownership,
    /// type, services, growth, occupancy, reservations and the local
    /// economic floor around `anchor`, so it is also the check made again
    /// immediately before the lot is demolished.
    /// Local R/C/I lot counts followed by their population/job totals.
    std::array<int,6> batteryZoneCounts(Coord anchor) const;
    bool batteryClearanceZones(Coord pos, std::vector<Uint32>& zones, Coord anchor,
                               const std::array<int,6>* localCounts = nullptr) const;
    /// Displace the lot under a reserved battery emplacement, at the moment the
    /// finished material that will stand on it is about to be placed. Returns
    /// true when a lot was demolished in this call; the caller then places the
    /// finished item on the ground it freed, in the same synchronous build
    /// call. Returns false - and touches nothing - in every other case,
    /// including when free ground has opened in the meantime, in which case the
    /// reservation is retargeted onto that free ground instead.
    bool commitBatteryClearance(const BuilderBase* builder, Uint32 itemToBePlaced,
                                std::list<Coord>& placeLocations, int spendable,
                                int economyReserve, int commitmentShortfall = 0,
                                bool* defer = nullptr);
    /// Lots displaced so far in this construction pass, across every yard.
    /// Reset by build(); bounds the work a single pass may do regardless of
    /// how many yards hold a clearance reservation. Derived, never serialised.
    int batteryClearancesThisPass = 0;
    /// Anchor a battery belongs to: the colony of the yard planning it, then the
    /// shelter cluster, then the house centre. Shared by the free-ground search
    /// and the clearance fallback so both face the same way.
    Coord batteryAnchor();
    /// Geometry a battery search needs once per pass rather than per candidate.
    struct BatteryGeometry {
        Coord anchor = Coord::Invalid();
        Coord forward = Coord(0, 0);
        std::vector<Coord> turrets;   ///< Standing and already reserved emplacements.
        int frontTurrets = 0;         ///< Of those, how many are on the enemy side.
        int localTurrets = 0;         ///< Of those, how many belong to this colony.
        int coverRadius = 1, clusterRadius = 2, frontAllowance = 0;
    };
    BatteryGeometry batteryGeometry(Coord anchor, Coord forward);
    /// Clearance site already chosen for this anchor in this build pass.
    /// Derived exactly like turretSiteCache: retired by clearPlacementCache()
    /// on any geometry change or planning-builder change, never serialised.
    std::optional<Coord> batteryClearanceCache;
    /// Planning builder the two caches below were filled for. Both are anchored
    /// on that yard's colony, so an unreserved yard must not inherit another
    /// unreserved yard's answer. Derived, never serialised.
    Uint32 turretSearchBuilder = NONE_ID;
    /// Non-city emplacement sites already chosen in this build pass, keyed by
    /// item. findTurretPlaceLocation is called as a validity probe by several
    /// rules and then once more for the real site, and the world does not change
    /// between those calls; a geometry change retires the entry in
    /// clearPlacementCache(). Derived state: never serialised, and it returns
    /// exactly what a repeated search would have returned.
    std::map<Uint32, Coord> turretSiteCache;
    // preferHunting=false returns the body at home instead of the attack
    // centroid, for troops that must not be dragged towards the front.
    Coord findSquadCenter(int houseID, bool preferHunting = true);
    Coord findBaseCentre(int houseID) const;
    Coord findBestDeathHandTarget(int enemyHouseID);
    const UnitBase* findLightRaiderTarget(const UnitBase* raider) const;
    const UnitBase* findThreateningTank(const UnitBase* raider) const;
    double getProductionBuildingMultiplier(int itemID) const;
    // Runtime-only observations. Never consulted by tactical/production decisions.
    struct HarvesterStrikeMemberTrace { Uint32 id, item; int price; CombatReward::Totals reward; };
    struct HarvesterStrikeTrace {
        uint64_t id; Uint32 target, start, sampled, logged, lastVisible;
        Uint32 transitCycles = 0, engagementCycles = 0;
        bool targetKilledByStrike = false;
        std::vector<HarvesterStrikeMemberTrace> members;
    };
    std::vector<HarvesterStrikeTrace> harvesterStrikeTraces;
    void updateHarvesterStrikeTelemetry(bool final = false);
    Coord findSquadRallyLocation();
    /// Opening space only, kept apart from the harvesting rally above: the
    /// units a custom game starts with stand on the home rock the base needs
    /// for its buildings, so each takes one short step towards the enemy onto
    /// free sand. Units already off the rock are left alone, and a unit with no
    /// safe site within reach receives no opening order at all.
    void applyOpeningSpaceDispersal();
    /// Where the opening build-out happens: the base centre, or the starting
    /// MCV while no yard exists yet.
    Coord openingAnchor();
    /// Offset from \a anchor towards the nearest visible enemy, or towards the
    /// middle of the map while nothing hostile has been seen. Zero when there
    /// is no forward direction at all.
    Coord openingForwardOffset(Coord anchor) const;
    /// Is this unit still walking to, or holding, its opening site? Regrouping
    /// leaves those units alone until the bounded opening window ends.
    bool holdsOpeningPosition(const UnitBase* unit) const;
    /// Opening sites handed out at game start, and the cycle at which ordinary
    /// regrouping takes over again. Both decide orders — whether a unit is left
    /// standing where it was stepped to, or marched back onto the home rock —
    /// so both are part of the ordinary save from SAVEGAMEVERSION 9848 and of
    /// every network checkpoint taken during the opening window.
    std::unordered_map<Uint32,Coord> openingDispersal;
    Uint32 openingDispersalUntil = 0;
    static constexpr int kOpeningStepMin = 2;      ///< Off the rock, not a twitch.
    static constexpr int kOpeningStepMax = 6;      ///< Still inside the base pocket.
    static constexpr int kOpeningWormClearance = 8;
    static constexpr Uint32 kOpeningHoldMs = 90000;
    /// A custom game never starts with this many units; a larger count in a
    /// save is corruption, not an opening.
    static constexpr Uint32 kOpeningDispersalLimit = 4096;
    Coord findSquadRetreatLocation();
    void moveToOptimalSquadPosition(const UnitBase* pUnit, FixPoint squadRadius, int* orderBudget = nullptr);
    void kiteAwayFromThreat(const UnitBase* pUnit, const ObjectBase* pThreat, int desiredRange);

    // `emergencyOnBase` reports whether the reported attacker was hitting one of
    // our buildings rather than a worker in the field; a base emergency outranks
    // a remote rescue when aircraft choose between two live interceptions.
    bool tryLaunchOrnithopterStrike(const QuantBotConfig::DifficultySettings& diffSettings,
                                    const QuantBotConfig& config, const ObjectBase* emergencyAttacker = nullptr,
                                    bool emergencyOnBase = false);

    std::list<Coord> placeLocations;    ///< Where to place structures
    // Runtime-only plans; the legacy list above remains in the save layout.
    // After loading, each yard safely finds positions for its own queued items.
    std::map<Uint32, std::list<Coord>> builderPlaceLocations;
    struct PlannedStructure { Uint32 item; Coord location; };
    std::map<Uint32, PlannedStructure> reservedStructures;
    // Recomputed every planning pass; zoning leaves two usable production plots.
    std::vector<PlannedStructure> cityProductionPlots;
    bool planningCityProductionPlots = false;
    struct RecentStructureLoss { Coord location; Coord size; Uint32 cycle; Uint32 item; };
    std::vector<int> tacticalDanger, harvesterDanger, lossDanger, factoryEnemyClearance;
    std::vector<Coord> visibleEnemyBases;
    std::vector<Uint32> visibleHarvestLaunchers;
    Uint32 dangerUpdated = std::numeric_limits<Uint32>::max();
    Uint32 lastSafetyTrace = std::numeric_limits<Uint32>::max();
    struct HarvesterSafety { Uint32 nextCheck = 0, retreatUntil = 0; Coord lastLocation = Coord::Invalid(), plannedDestination = Coord::Invalid(); bool controlled = false; };
    std::map<Uint32, HarvesterSafety> harvesterSafety;
    struct UnsafeField { Coord location; Uint32 cycle; };
    std::vector<UnsafeField> unsafeFields;
    void refreshTacticalDanger();
    int dangerAt(Coord pos, Coord size = Coord(1, 1), bool losses = false) const;
    bool reactorClearance(Uint32 item, Coord pos) const;
    /// The footprints (x, y, w, h) a placement of @a item must keep reactor blast clearance from:
    /// for a reactor every own protected building, for a protected building every own reactor
    /// (standing or reserved by another builder); empty for anything else. reactorClearance()
    /// is clearsBlastSites() over this list; a placement search builds it once per search.
    std::vector<std::array<int, 4>> reactorBlastSites(Uint32 item) const;
    static bool clearsBlastSites(const std::vector<std::array<int, 4>>& sites, Coord pos, Coord size);
    int rearScore(Coord pos, Coord base) const;
    int recentFactoryLossCount() const;
    struct SpiceFieldCache;
    bool manageHarvesterSafety(const Harvester* harvester, SpiceFieldCache* spice = nullptr);

    std::vector<RecentStructureLoss> recentStructureLosses;
    bool nearRecentStructureLoss(int x, int y, int width, int height) const;
    /// Total price of this house's military units: every unit type except
    /// carryalls, harvesters, MCVs and sandworms. Computed live at each use.
    int militaryUnitValue() const;
    /// Builds this house's owned-tile indicator so Map::isWithinBuildRange()
    /// can be answered in O(1) instead of a 5x5 probe per tile. Rebuilt per
    /// use; holds no state across decisions.
    void buildAnchorField(DuneCity::BoxAnyField& field) const;

    /// Owned-tile field shared by the placement searches of ONE build() call.
    ///
    /// The field depends only on tile ownership. It therefore survives changes
    /// to reservations or planningBuilder — those do not move a single owned
    /// tile — and is invalidated only where this house actually mutates the
    /// world inside build(): a zone demolition and a structure placement. No
    /// simulation tick runs inside build(), so no other actor can change
    /// ownership while the cache is live.
    ///
    /// Lifetime is bounded by BuildAnchorScope below: `active` is only ever
    /// true inside one build() invocation and the guard clears the field on
    /// every exit path, including exceptions. Nothing here is serialised or
    /// read across cycles.
    struct BuildAnchorCache {
        bool active = false;
        bool valid = false;
        DuneCity::BoxAnyField field;
        uint64_t builds = 0, hits = 0, invalidations = 0;
    };
    BuildAnchorCache buildAnchors;

    /// RAII activation of buildAnchors for exactly one build() invocation.
    class BuildAnchorScope {
    public:
        explicit BuildAnchorScope(QuantBot& bot) : bot_(bot) {
            bot_.buildAnchors.active = true;
            bot_.buildAnchors.valid = false;
        }
        ~BuildAnchorScope() {
            bot_.buildAnchors.active = false;
            bot_.buildAnchors.valid = false;
            bot_.buildAnchors.field = DuneCity::BoxAnyField();
        }
        BuildAnchorScope(const BuildAnchorScope&) = delete;
        BuildAnchorScope& operator=(const BuildAnchorScope&) = delete;
    private:
        QuantBot& bot_;
    };
    /// Called where this house actually changes tile ownership inside build().
    void invalidateAnchorField();

    /// Ordered spice-tile membership shared by the harvester candidate sweeps of
    /// ONE checkAllUnits() invocation. Holds tile hasSpice() membership only, in
    /// the original y-then-x order. Everything that can change as earlier
    /// harvesters are ordered — peers, plannedDestination reservations,
    /// unsafeFields memory, danger, passability and routes — is still evaluated
    /// per candidate, per harvester. Lives in a checkAllUnits() local and is
    /// passed down by pointer; other callers pass nothing and get a fresh scan.
    struct SpiceFieldCache {
        bool valid = false;
        std::vector<Coord> tiles;
        uint64_t builds = 0, hits = 0, invalidations = 0;
    };
    /// The house's oldest surviving construction yard: the main base anchor.
    Uint32 mainConstructionYardID() const;
    /// May a colonist set out at all: the core (heavy factory, high tech
    /// factory, repair yard) stands. Rocket cover is not required; the colony
    /// yard orders its own. \a needsLocalSpace waives the core exactly as a
    /// measured built-out base does, for a caller that has already established
    /// there is no usable site at home.
    bool expansionDefenceReady(bool needsLocalSpace = false) const;
    int expansionTurretsMissing(const StructureBase* yard, bool planned = true) const;
    /// The owner-invariant inputs of an expansion cover test: the main base
    /// anchor and the house's living rocket turrets, in structure-list order.
    /// Collected once so a reduction over every yard does not rewalk the
    /// structure list per candidate. Purely a hoist: the facts are exactly
    /// what expansionTurretsMissing() would have recomputed per call.
    struct ExpansionCoverFacts {
        Uint32 mainYardID = NONE_ID;
        int    turretRadius = 1;
        std::vector<Coord> turrets;
    };
    ExpansionCoverFacts collectExpansionCoverFacts() const;
    int expansionTurretsMissing(const StructureBase* yard, bool planned,
                               const ExpansionCoverFacts& facts) const;
    /// Is this construction yard (or planned yard, NONE_ID) an expansion
    /// outside the main base rather than the base's own anchor?
    static bool isExpansionYard(Uint32 item, Uint32 objectID, Uint32 mainYardID);
    /// Construction yards lost within \a radius tiles in the retained history.
    int lostYardsNear(int x, int y, int radius) const;
    /// A site that already swallowed this many yards is not expanded onto
    /// again while the loss is still remembered.
    static constexpr int kRepeatedYardLossLimit = 2;
    static constexpr int kRepeatedYardLossRadius = 6;
    /// Colonists that died on the way to another formation, remembered in the
    /// retained loss history beside lost yards. kRepeatedYardLossLimit of them
    /// suspend colonisation until the history ages out, so MCVs are not fed
    /// one by one to an enemy holding the route.
    int colonistsLost() const;
    /// Remembers undeployed MCVs that vanished on a trip to another formation.
    void recordLostColonists();
    Uint32 planningBuilder = NONE_ID;
    bool overlapsReservedStructure(int x, int y, int width, int height) const;
    // ---- The footprint rule (docs/formations.md "Planned sites and the army") --------------
    // This house's own ground units are traffic on a site it plans, not an obstacle: the site
    // is chosen as if they were not there, they are asked off it (the pre-placement queue's
    // idle-unit rule) and the finished building waits for them while they leave. Another
    // house's unit, a structure, or an own unit that was ordered away and cannot leave still
    // blocks the site.
    /// An own unit standing here is in the way for good: unresponsive, or ordered away and stuck.
    bool ownUnitCannotLeave(const UnitBase* unit) const;
    /// A mountain, a structure, another house's unit or an own unit that cannot leave.
    bool tileBlocksOwnPlacement(const Tile* tile) const;
    /// Map::okayToPlaceStructure with own leaving-capable units ignored (other units, structures
    /// and mountains still block). tilesRequired/item as the engine check.
    bool placeableAmongOwnUnits(int x, int y, int width, int height, Uint32 item) const;
    /// Does any unit of this house stand on the footprint?
    bool ownUnitsOnFootprint(int x, int y, int width, int height) const;
    /// Is p inside any planned site of this house (every yard's reservation)? Holding, bundle,
    /// muster, recall and guard slots skip such tiles.
    bool onPlannedSite(Coord p) const;
    /// Once per unit pass: idle own units standing on a planned site are asked off it.
    void keepPlannedSitesClear();
    OrnithopterStrikeTeam ornithopterStrikeTeam;
    std::unordered_map<Uint32, Coord> placementCache; ///< Per-build-cycle cache for findPlaceLocation results
    Uint32 placementCacheExcludedBuilder = NONE_ID;

    /// The four map-sized placement distance fields, reused across searches.
    /// The key is the contribution list itself -- every live owned structure and
    /// every reservation that is not the planning builder's, in the order
    /// findPlaceLocation visits them -- so reuse is exact rather than inferred
    /// from builder identity, which cannot see a structure dying or a
    /// reservation changing mid-pass. The pollution field additionally depends
    /// on the planned item's own sensitive/polluter roles; the three nearest-
    /// origin fields do not depend on the planned item at all. Derived state:
    /// never serialised, unlike placementCache.
    struct PlacementDistanceCache {
        struct Contribution {
            Uint32 item = 0;
            Sint32 x = 0, y = 0, w = 0, h = 0;
            bool operator==(const Contribution& o) const {
                return item == o.item && x == o.x && y == o.y && w == o.w && h == o.h;
            }
        };
        std::vector<Contribution> key;
        Sint32 width = 0, height = 0;
        bool originFieldsValid = false;
        bool pollutionValid = false;
        bool sensitive = false, polluter = false;
        /// How many times each group of fields was actually rebuilt. Diagnostic
        /// only -- never serialised and never read by a decision -- but it is the
        /// one observable that distinguishes a reuse from a silent rebuild, so
        /// the cache tests assert on it rather than re-deriving the key.
        Uint32 originBuilds = 0, pollutionBuilds = 0;
        CityDistanceField nearestResidential{0,0}, nearestCommercial{0,0},
            nearestIndustrial{0,0}, pollutionSeparation{0,0};
        void invalidate() {
            originFieldsValid = false; pollutionValid = false;
            key.clear(); key.shrink_to_fit();
            width = 0; height = 0;
            // Release the map-sized cells too, so a finished build pass does not
            // keep four of them per bot alive until the next one.
            nearestResidential = CityDistanceField(0,0); nearestCommercial = CityDistanceField(0,0);
            nearestIndustrial = CityDistanceField(0,0); pollutionSeparation = CityDistanceField(0,0);
        }
    };
    PlacementDistanceCache placementDistances;

    struct CityServiceSite {
        Coord site = Coord::Invalid();
        CityServiceInvestmentPolicy::Value value;
    };
    // [normal/emergency/land-value-only][police/rocket], scored together.
    using CityServiceResults = std::array<std::array<CityServiceSite, 2>, 3>;
    CityPlanningPolicy::PassSearch<Uint32, CityServiceResults> cityServiceSearch;
    struct CityTurretResult {
        Coord site = Coord::Invalid();
        int defense = 0, amenity = 0, crime = 0, hotspot = 0;
    };
    CityPlanningPolicy::PassSearch<Uint32, CityTurretResult> cityTurretSearch;
    unsigned cityReadyYardCount = 1; // Derived each build pass, for fair replan sweeps.

    // ---- Army posture, cohesion and the outnumbered dispatch gate ----------
    //
    // Custom Hard/Brutal only. Campaign pacing and roles, helper bots, support
    // mode and the established Easy/Medium home-reserve behaviour are untouched:
    // recoveryActive() is the single gate and every call site checks it.
    //
    // Saved state. The posture and its age decide orders, so a save or a network
    // checkpoint taken mid-withdrawal has to carry them; the attrition ledger is
    // cumulative, so recomputing it after a load is impossible. Appended under
    // the SAVEGAMEVERSION 9852 gate.
    ArmyPosturePolicy::Posture armyPosture = ArmyPosturePolicy::Posture::Offensive;
    Uint32 postureSince = 0;
    ArmyPosturePolicy::AttritionLedger attrition;
    /// Test-only (tests/ai play styles, docs/ai-play-styles.md): this player's own copy of the
    /// QuantBot config with a style's keys applied (QuantBotConfig::applyOverrides), so two
    /// QuantBots of one match can play different styles. Never set by the game, not saved and
    /// not part of the config hash; null = the one global config, as in every real game.
    std::shared_ptr<const QuantBotConfig> configOverride;
    /// The config this player plays by: configOverride if set, else getQuantBotConfig().
    const QuantBotConfig& botConfig() const { return configOverride ? *configOverride : getQuantBotConfig(); }
    /// Test-only play-style behaviours a config key cannot express (tests/ai play styles,
    /// `[Play Style]` in a tests/ai/play-styles/*.ini file; docs/ai-play-styles.md). The
    /// defaults are inert: every real game plays with them. Custom Hard/Brutal only. Never set by
    /// the game, not saved, not hashed.
    struct PlayStyle {
        /// Share (basis points) of the final unit mix taken from `mix` (0 = the learned mix only).
        int mixBps = 0;
        /// The style's composition in the group form (tank, siege tank, launcher, special,
        /// ornithopter, trike, raider trike, quad; the special share is split between the specials
        /// the house can build); unavailable slots are dropped.
        UnitMixPolicy::GroupWeights mix{};
        /// Game minutes the style mix applies for (0 = the whole game).
        int mixMinutes = 0;
        /// Counter-composition: blend the mix towards the counters of the remembered enemy army
        /// (light -> tanks, armour -> launchers and siege, launchers -> quads and tanks, air -> launchers).
        int counterBps = 0;
        /// Rush opening: the light factory right after the first refinery, before every other
        /// structure, and ground waves do not wait for a repair yard.
        bool lightFirst = false;
        /// A viable main wave (default 6 units and 3,000 credits).
        int minWaveUnits = 6, minWaveValue = 3000;
        /// Baseline rocket turrets after the repair yard (default 2).
        int rocketTurrets = 2;
        /// Harvest as if no other house shared the spice (more harvesters and refineries).
        bool greedyHarvest = false;
        /// Refineries beyond the harvester-driven target.
        int extraRefineries = 0;
        /// Construction yards (incl. MCVs) the house keeps at least once a heavy factory stands.
        int minYards = 0;
        /// Light vehicles kept raiding: each hunts the nearest enemy harvester (else refinery).
        int raidUnits = 0;
        /// The in-game unit-mix learning (false: production keeps the bounded starting mix all
        /// game; the "learning off" arm of docs/ai-play-styles.md, section 8).
        bool learning = true;
        /// The starting-mix bounds (false: the 1.0.847 unbounded starting mix, for comparison).
        bool startBounds = true;
        bool active() const {
            return mixBps > 0 || counterBps > 0 || lightFirst || minWaveUnits != 6 || minWaveValue != 3000
                || rocketTurrets != 2 || greedyHarvest || extraRefineries > 0 || minYards > 0 || raidUnits > 0;
        }
    };
    PlayStyle playStyle;
    /// playStyle if this is a Custom Hard/Brutal QuantBot, else the inert defaults.
    const PlayStyle& style() const;
    /// The raiders of PlayStyle::raidUnits (id order; runtime only, not saved).
    std::set<Uint32> styleRaiders;
    /// Re-selects the raiders (unit pass): drops the lost and busy, tops up with free light vehicles.
    void updateStyleRaiders();
    /// Orders a raider at the nearest enemy harvester (else refinery); false if it has no prey.
    bool raidWithStyle(const UnitBase* raider);
    /// The unit mix with the style mix and counter-composition applied (unit production).
    UnitMixPolicy::Mix styledUnitMix(const UnitMixPolicy::Mix& mix, const UnitMixPolicy::Slots& available) const;
    /// The strategy this house plays (QuantBotStrategy; docs/ai-play-styles.md, "Strategies"):
    /// picked once by pickStrategy() from the game seed, the house and the config, saved
    /// (SAVEGAMEVERSION 9865); a 9864 save picks it afresh by the same function.
    QuantBotStrategy::Choice strategy;
    bool strategyPicked = false;
    /// The starting ratios a Tuned or Bounded strategy resolved to when it was picked (the house's
    /// [Unit Ratios Tuned] for the game's tech band, or QuantBotStrategy::randomMix of seed and
    /// house), bounded over the slots the house can produce; all zero for every other mix. Saved
    /// (SAVEGAMEVERSION 9866; ten slots, one per special, since 9868 - an older save's special
    /// share is split between the house's specials): a reload plays the mix it drew whatever
    /// config is loaded then. A 9865 save has no such mix.
    UnitMixPolicy::Mix strategyPrior{};
    /// Picks the strategy if none is set (first update of a new game or a pre-9865 load).
    void pickStrategy();
    /// The strategy's mix / tempo while it applies (Custom Hard/Brutal; the tempo within its
    /// minutes), else null: the learned default and the configured readiness.
    const QuantBotStrategy::MixProfile* strategyMix() const;
    const QuantBotStrategy::TempoProfile* strategyTempo() const;
    /// The mix slots this house can produce at \a tech (default: the game's tech level): the unit
    /// is enabled, priced and has a builder, its tech level is at most \a tech, and its builder
    /// and every prerequisite structure can be obtained at \a tech (ObjectData, recursively).
    UnitMixPolicy::Slots fieldableMixSlots(int tech = -1) const;
    /// The first tech level of the game's tech band for this house: the lowest level at which
    /// the house fields the same slots as at the game's tech level (tests/ai/tech-bands.py).
    int techBand() const;
    /// fieldableMixSlots() of the game's tech level, computed once (runtime only, not saved).
    mutable UnitMixPolicy::Slots fieldableSlots{};
    mutable bool fieldableSlotsCached = false;
    /// The starting mix over the \a available slots, before any combat evidence: the configured
    /// opening, the strategy's prior in its place, then the starting-mix bounds (unless a test
    /// style turns them off). The prior UnitMixPolicy::allocate blends the learning into.
    UnitMixPolicy::Weights startingPrior(const UnitMixPolicy::Slots& available) const;
    /// The opening and wave rules of the style (if a test style is active) or else the tempo.
    bool lightFirstOpening() const;
    int minWaveUnits() const;
    int minWaveValue() const;
    /// An Infantry mix strategy: Custom vanilla then founds a barracks and a WOR too.
    bool infantryStrategy() const { const auto* mix = strategyMix(); return mix && mix->infantryPercent > 0; }
    /// QuantBotStrategy::counterPrior of the remembered enemy army and the hostile wing in sight.
    UnitMixPolicy::Weights enemyCounterPrior() const;
    /// Test-only (tests/ai/formation-duel.py): this player's attack-wave shape instead of
    /// QuantBotConfig::waveFormation, so two QuantBots of one match can march in different
    /// shapes. Never set by the game, not saved and not part of the config hash; production
    /// always uses the one config value. kNoWaveFormationOverride = no override.
    static constexpr int kNoWaveFormationOverride = -2;
    int waveFormationOverride = kNoWaveFormationOverride;
    /// The attack-wave shape this player uses: the override if set, else the config value.
    int waveFormationShape() const;

    /// profile(strategy.mix) with a Tuned/Bounded strategyPrior in place (strategyMix()).
    QuantBotStrategy::MixProfile strategyProfile;
    /// The strategy's starting ratios over this house's mix slots: strategyPrior (Tuned,
    /// Bounded) or the preset's group form expanded over the house's specials.
    UnitMixPolicy::Weights strategyStart{};
    void resolveStrategyProfile();
    // ---- Blob waves (docs/formations.md, "Blob waves") ------------------------------------
    //
    // A Custom Hard/Brutal ground wave with WaveFormation=Blob is a hunt party: it gathers at a
    // rally point towards its objective, marches as one blob leg by leg (the fast members wait
    // at each leg's end for the rest), releases into engine Hunt on contact, reforms when the
    // fight is quiet and, when locally outmatched, falls back a few tiles to reform instead of
    // going home (the tactical retreat). Saved under the SAVEGAMEVERSION 9862 gate.
    struct BlobParty {
        BlobWavePolicy::Phase phase = BlobWavePolicy::Phase::Gather;
        Uint32 since = 0;                   ///< cycle the phase (or the march's leg) began
        Uint32 objective = NONE_ID;         ///< what the march heads for
        Coord goal = Coord::Invalid();      ///< the march's destination (objective's nearest point)
        Coord rally = Coord::Invalid();     ///< gather, reform or fall-back point; the leg's end
        Coord threat = Coord::Invalid();    ///< where the last contact was
        Uint32 lastContact = 0;             ///< last cycle a member was in contact or hit
        Uint32 lastArrival = 0;             ///< gather/reform: last cycle the gathered count grew
        Uint32 gathered = 0;                ///< gather/reform: that count
        Uint32 legCycles = 0;               ///< time limit of the current gather, reform or leg
        Uint32 outmatchedSince = std::numeric_limits<Uint32>::max();
        Uint32 progressCycle = 0;           ///< march: last cycle the anchor got closer
        Sint32 progressRemaining = std::numeric_limits<Sint32>::max();
        Uint8 fallbacks = 0;                ///< tactical fall-backs so far
        /// Test-only (+e): cycle the staged party began holding for superiority (max: not
        /// holding). Not saved: never set without the test-only override.
        Uint32 holdSince = std::numeric_limits<Uint32>::max();
        std::set<Uint32> members;           ///< ordered: identical iteration on every peer
    };
    std::vector<BlobParty> blobParties;
    /// Corruption bound for a loaded party list, not a design limit (one party per launch).
    static constexpr Uint32 kBlobPartyLimit = 256;
    /// Test-only (tests/ai/formation-duel.py ablations): -1 = the tactical fall-back runs with
    /// Blob waves, 0 = never, 1 = also for plain Hunt waves. Never set by the game, not saved,
    /// not hashed.
    int tacticalRetreatOverride = -1;
    /// Test-only Blob-wave variants (tests/ai/formation-duel.py, label suffix +v/+r/+k): bits of
    /// kBlobVehiclePace (foot infantry neither sets the pace nor holds a gather or leg; 75%
    /// quorum), kBlobRealFight (only a real fight releases the blob; a lone scout or harvester
    /// is shot by the nearest members while the blob marches on) and kBlobKeepDamaged (members
    /// below 60% stay with a party that has not met a fight yet instead of driving to repair).
    /// Never set by the game, not saved, not hashed.
    static constexpr int kBlobVehiclePace = 1, kBlobRealFight = 2, kBlobKeepDamaged = 4;
    /// kBlobStage (+s): no gather at home; the blob marches in one leg to a staging point just
    /// outside enemy reach near its objective, waits there for the rest and then attacks all
    /// at once. kBlobSuperiority (+e, with +s): it attacks from the staging point only with
    /// local superiority, else holds for reinforcements (later waves join it) for a while.
    static constexpr int kBlobStage = 8, kBlobSuperiority = 16;
    int blobVariantOverride = 0;

    // ---- Concentrating force (docs/formations.md, "Concentrating force") -----------------
    /// The rules as bits (tests/ai/formation-duel.py label flags, uppercase): bits of
    /// kForceFocusFire (F: hunting ground units shoot the best target in reach - fighters
    /// first, then the one with the fewest hit points), kForceConverge (C: wave members not
    /// fighting join a fight of their army nearby), kForcePool (P: an attacked building gets
    /// 2x the threat within 12 tiles from up to 40 tiles), kForceRecall (H: when the troops
    /// nearby cannot answer a base attack, the raiding army comes home for it), kForceMuster
    /// (M: distant defenders muster just outside the fight and engage together),
    /// kForceBigWaves / kForceSmallWaves (L / Q: wave readiness 150% / 60%), kForceNoOverkill
    /// (N: focus fire by attack value - the most enemy damage per hit point - without overkill,
    /// in object-id order), kForceBundle (B: the idle reserve keeps bundled at a forward rally a
    /// quarter of the way to the observed enemy base), kForceBundleOld (O, with B: the pre-1.0.602
    /// rule - the closer of that rally and the army's centre of mass), kForceBundleGate (W, with
    /// B: a ready wave launches only once 70% of it stands in the bundle, at most 60 s late),
    /// kForceSplit (S: the wave splits in two halves that approach the objective from 90 degrees
    /// apart and attack together) and kForceFlank (T: 70% main force head-on, 30% flanking from
    /// 90 degrees, attacking together). Custom Hard/Brutal only. The game takes F, C, B, W, U, X and I
    /// from QuantBotConfig::force (and the wave size from its WaveReadinessPercent); the duel
    /// harness sets forceVariantOverride to exactly the bits of a label (0 = all off: the
    /// rules before protocol 78). The override is never set by the game, not saved, not hashed.
    /// kForceStrengthReadiness (D, test-only: while the observed enemy ground army outweighs
    /// ours by kOutnumberedReadinessBps, waves wait for full readiness - 100% - instead of a
    /// smaller configured one). Round 5 defence rules (docs/formations.md "Round 5"; U and X
    /// are taken from QuantBotConfig::force, the others are test-only; the perimeter hold V was
    /// removed in protocol 84 - docs/formations.md "Free-for-all: finishing the game"):
    /// kForceEstimate (E: D measures the remembered enemy army,
    /// enemyThreatEstimate(), instead of what is visible this cycle), kForceGarrison (G: while
    /// that estimate outnumbers us, no wave launches and the reserve holds the protected
    /// rally under turret cover; the enemy attacks into it), kForceStrengthWaves (A: a ready
    /// wave launches only when its value is forceStrengthBps of the estimate), kForceReserve
    /// (R: while the estimate exceeds our army, the share it exceeds it by - at most half -
    /// stays home, the units nearest the protected rally), kForceSizedDefence (X: a defence
    /// counts the enemy within 12 tiles, sends 1.5x it from up to 40 tiles with the bundle
    /// first, distant defenders muster for a building, and a worker under attack gets nobody
    /// when the troops in reach cannot match the threat) and kForceFallBack (Y: a wave
    /// locally outmatched for the configured time falls back to the protected rally - the
    /// recovery's local withdrawal, which is off with withdrawals - and relaunches from there).
    /// kForceOpeningReadiness (I - recorded as E before the 1.0.846 merge - [Force Concentration]
    /// OpeningReadiness, default on since protocol 81: a house that started without an army able
    /// to launch a wave - a Construction Yard start - holds its first ground wave for full
    /// readiness; ForceConcentrationPolicy::openingReadiness, docs/formations.md "Scenario tests").
    /// Late game (docs/formations.md, "Free-for-all: finishing the game"; [Force Concentration]
    /// TurretPriority, PressOn, FinishWeakest, FortifiedWaves): kForceTurretPriority (T: a wave
    /// hunter in an enemy turret's reach that is not shooting an armed unit attacks the turret),
    /// kForcePressOn (P: a wave member out of visible targets hunts on to the nearest enemy
    /// structure seen), kForceFinishWeakest (H: a ground wave hunts towards the weakest known
    /// enemy house's production and economy), kForceFortifiedWaves (K: against an objective
    /// guarded by many seen turrets a ready wave waits, at most three minutes, for a bigger army)
    /// and kForceSonicClearLane (S, SonicClearLane: a hunting sonic tank whose wave would pass over
    /// one of our own units shoots another enemy in reach whose lane is clear). Round 2 candidates
    /// (all test-only, default off): kForceSpendAtCap (M, SpendAtCap: banked credits raise the
    /// production budget above the military limit, for siege tanks, launchers and specials only),
    /// kForceSiegeStandoff (O, SiegeStandoff: wave launchers shell known turrets from outside their
    /// reach while the rest of the wave holds short of the turrets) and kForceMassAssault (Z,
    /// MassAssault: against a fortified objective the army waits for kAssaultLimitPercent of the
    /// limit, then all of it hunts towards one objective of the weakest house). On by default since
    /// 1.0.848: kForceFinishLeading (FinishWhenLeading: H's target choice for the waves of a house
    /// whose team army leads the strongest enemy team's remembered army, finishLeading()).
    static constexpr int kForceFocusFire = 1, kForceConverge = 2, kForcePool = 4, kForceRecall = 8,
        kForceMuster = 16, kForceBigWaves = 32, kForceSmallWaves = 64, kForceNoOverkill = 128,
        kForceBundle = 256, kForceBundleOld = 512, kForceBundleGate = 1024, kForceSplit = 2048,
        kForceFlank = 4096, kForceStrengthReadiness = 8192, kForceEstimate = 16384, kForceGarrison = 32768,
        kForceStrengthWaves = 65536, kForceReserve = 131072, kForceSizedDefence = 262144, kForceFallBack = 524288,
        kForceThreatTurrets = 1048576, kForceFinishLeading = 2097152, kForceOpeningReadiness = 4194304,
        kForceTurretPriority = 8388608, kForcePressOn = 16777216, kForceFinishWeakest = 33554432,
        kForceFortifiedWaves = 67108864, kForceSonicClearLane = 134217728, kForceSpendAtCap = 268435456,
        kForceSiegeStandoff = 536870912, kForceMassAssault = 1073741824;
    static constexpr int kNoForceOverride = -1;
    int forceVariantOverride = kNoForceOverride;
    /// kForceStrengthWaves' k, basis points of the enemy estimate (duel variant /k<percent>).
    int forceStrengthBps = 10000;
    /// The force bits in effect: the override if set, else the configured F, C, B, W, U, X, I,
    /// T, P, H, K and S.
    int forceBits() const;
    bool forceVariant(int bit) const { return (forceBits() & bit) != 0 && autonomousCustomArmy(); }
    /// Wave readiness and house attack threshold, percent of the difficulty's: the override's
    /// L (150) or Q (60) if set, else the configured WaveReadinessPercent; 100 outside Custom
    /// Medium/Hard/Brutal.
    int waveReadinessPercent() const;
    /// The readiness before any outnumbered raise: waveReadinessPercent(), or 100 under I while
    /// the house has not launched a ground wave yet (the opening).
    int openingReadinessPercent() const;
    /// The readiness a launch uses now: openingReadinessPercent(), raised to 100 by D while the
    /// house is outnumbered (outnumberedByObservedArmy(), with E outnumberedByEstimate()).
    int launchReadinessPercent();
    /// Health-weighted value of the visible enemy mobile ground fighters of the strongest enemy
    /// team against ours (wherever they stand): ForceConcentrationPolicy::outnumbered. One
    /// unit-list pass.
    bool outnumberedByObservedArmy() const;
    /// The remembered enemy army: every enemy mobile ground fighter seen within the last
    /// kSightingMemoryMs that still exists, at its health-weighted value when last seen.
    struct Sighting { int64_t value = 0; Uint32 cycle = 0; };
    std::map<Uint32, Sighting> enemySightings;
    /// Records the visible enemy fighters and forgets the dead and the long unseen (unit pass).
    void updateEnemySightings();
    /// The threat the remembered enemy army poses: the strongest single enemy team's share of it
    /// (every remembered fighter in a 1v1 or against one alliance; in a free-for-all not the sum
    /// of houses that also fight each other).
    int64_t enemyThreatEstimate() const;
    /// Health-weighted value of our own mobile ground fighters.
    int64_t ownGroundArmyValue() const;
    /// Health-weighted value of our team's mobile ground fighters (ours and our allies'; ours
    /// alone in a free-for-all or a 1v1).
    int64_t teamGroundArmyValue() const;
    /// FinishWhenLeading: our team's ground army is worth more than the strongest enemy team's
    /// remembered army (enemyThreatEstimate) - the leader of a free-for-all.
    bool finishLeading() const { return teamGroundArmyValue() > enemyThreatEstimate(); }
    /// ForceConcentrationPolicy::outnumbered on teamGroundArmyValue() and enemyThreatEstimate(),
    /// once per cycle.
    bool outnumberedByEstimate() const;
    mutable Uint32 estimateCycle = std::numeric_limits<Uint32>::max();
    mutable bool estimateOutnumbered = false;
    /// kForceGarrison (G, test-only) in effect now: outnumbered by the estimate, so no wave
    /// leaves and the reserve holds at the protected rally.
    bool garrisoned() const { return forceVariant(kForceGarrison) && outnumberedByEstimate(); }
    /// The base perimeter on the observed enemy side (docs/formations.md "Round 5"): from the
    /// base centre along the observed front direction, the farthest own building within
    /// kPerimeterBaseTiles (its projection, depth) and a standable tile kPerimeterStandoffTiles
    /// beyond it (point) - further out if that is still among our buildings or roads (a city
    /// base; builtUpDistance). Derived from the world once per cycle; invalid without a base or a front.
    struct Perimeter {
        Coord anchor = Coord::Invalid(), forward = Coord(0, 0), point = Coord::Invalid();
        int length = 0;         ///< isqrt of |forward|^2
        int depth = 0;          ///< tiles from the anchor to the outermost building, along forward
    };
    const Perimeter& perimeter() const;
    mutable Perimeter perimeterCache;
    mutable Uint32 perimeterCycle = std::numeric_limits<Uint32>::max();
    /// kForceThreatTurrets (U): rocket turrets wanted on the perimeter now - one per
    /// kPerimeterCreditsPerTurret of the estimate's lead over our army, at most
    /// kPerimeterTurretsMax, none when we are not outnumbered.
    int perimeterTurretGoal() const;
    /// Our rocket turrets standing on the perimeter band.
    int perimeterTurrets() const;
    /// A legal rocket-turret site on the perimeter band (bounded box, deterministic), or invalid.
    Coord findPerimeterTurretLocation();
    /// Health-weighted value of the living units among \a ids.
    int64_t groundValueOf(const std::vector<Uint32>& ids) const;
    /// kForceReserve: of a wave about to launch (\a ids), the units that stay home -
    /// ForceConcentrationPolicy::reserveShareBps of its value, nearest the protected rally first
    /// (ties by id). Empty when we are not outnumbered.
    std::vector<Uint32> homeReserve(const std::vector<Uint32>& ids) const;
    /// Does \a unit stand in the bundle (within 8 tiles of the bundle point or centre)?
    bool standsInBundle(const UnitBase* unit) const;
    /// One pass of the force rules over the army (unit phase, after the hunt parties).
    void concentrateForces();
    /// Focus fire for one hunting ground unit; true if it was given a new target.
    bool focusFire(const UnitBase* unit, const std::vector<const ObjectBase*>& enemies);
    /// Attack-value focus fire without overkill over \a hunters (sorted by object id).
    void focusFireNoOverkill(const std::vector<const UnitBase*>& hunters, const std::vector<const ObjectBase*>& enemies);
    /// kForceTurretPriority (T): \a unit, a hunter not shooting an armed unit in its reach, attacks
    /// the nearest enemy turret among \a turrets that has it in reach (or that it reaches); true if
    /// it was given that target.
    bool attackTurretInReach(const UnitBase* unit, const std::vector<const ObjectBase*>& turrets);
    /// kForceSonicClearLane (S): \a unit, a hunting sonic tank, keeps a target whose lane is clear
    /// of \a friends (our team's ground units within reach, sonic tanks excluded) or switches to the
    /// nearest enemy in its reach among \a enemies whose lane is; true if it is now on a clear-lane
    /// target (no other targeting rule then applies to it this pass).
    bool sonicClearTarget(const UnitBase* unit, const std::vector<const ObjectBase*>& enemies,
                          const std::vector<const UnitBase*>& friends);
    /// kForceSiegeStandoff (O, test-only): wave launchers not shooting an armed unit in their reach
    /// attack the nearest known enemy turret within kSiegeReachTiles beyond their range (they
    /// outrange rocket turrets by a tile); wave members walking into a known turret's reach while
    /// at least kSiegeMinLaunchers of those launchers are near them guard a point just outside it
    /// (siegeHeld), released when the turret is gone, the launchers are, or after kSiegeMaxHoldMs.
    /// Adds every unit it ordered or holds to \a handled (no other targeting rule this pass).
    void siegeStandoff(std::set<Uint32>& handled);
    /// O's held escorts: unit id -> the cycle its hold began. Not saved: the rule is test-only.
    std::map<Uint32, Uint32> siegeHeld;
    /// An enemy structure our team has seen (its tile is explored), alive, not a wall, slab, road
    /// or power line: what a player knows stands there.
    bool knownEnemyStructure(const StructureBase* structure) const;
    /// kForceFinishWeakest (H): the enemy team to finish - of the teams with a known structure,
    /// the weakest by remembered army plus kKnownTurretValue per known turret (ties: fewer known
    /// structures, then the lower team id); -1 when none is known.
    int finishTargetTeam() const;
    /// H's objective for a wave led by \a lead: the nearest known structure of \a team the lead can
    /// reach by ground, production and economy (yard, factories, refinery) counted 20 tiles nearer.
    const ObjectBase* finishObjective(const UnitBase* lead, int team) const;
    /// kForcePressOn (P): the nearest known enemy structure to \a unit (of \a team, or any when -1)
    /// it can reach by ground, from \a known (the known enemy structures of this pass), or nullptr.
    const StructureBase* pressOnTarget(const UnitBase* unit, const std::vector<const StructureBase*>& known,
                                       int team) const;
    /// kForceFortifiedWaves (K): known enemy turrets within kFortifiedRadiusTiles of \a objective.
    int knownTurretsNear(const ObjectBase* objective) const;
    /// Distant defenders gathering before they engage at an attacked building (kForceMuster).
    struct Muster {
        Uint32 building = NONE_ID;
        Coord point = Coord::Invalid();
        Uint32 since = 0;
        std::set<Uint32> members;
    };
    std::vector<Muster> musters;
    const Muster* musterOf(Uint32 id) const;
    /// Sends \a ids to muster for \a building (joining its muster if there is one).
    void startMuster(const ObjectBase* building, const std::vector<Uint32>& ids);
    void updateMusters();
    /// Bundling (kForceBundle; QuantBotConfig::force.bundle, on by default): where the idle
    /// reserve gathers, chosen at most every 30 s (bundleCycle) or when it becomes unsafe.
    /// Saved with bundleCentre and bundleGateSince (SAVEGAMEVERSION 9863): the throttle and
    /// the gate's wait decide orders, so a reload must not re-run them early.
    Coord bundlePoint = Coord::Invalid();
    /// With kForceBundleOld: the army's centre of mass at the last unit pass (the second
    /// gathering point; the launch gate reads it between passes).
    Coord bundleCentre = Coord::Invalid();
    Uint32 bundleCycle = std::numeric_limits<Uint32>::max();
    /// First cycle a ready wave waited for the bundle (kForceBundleGate); 0 = not waiting.
    Uint32 bundleGateSince = 0;
    /// The bundle point, out of our own base by \a clearance tiles where the line towards the
    /// enemy allows it (ForceConcentrationPolicy::bundleClearance; docs/formations.md
    /// "Free-for-all: the army deploys").
    Coord forwardBundlePoint(int clearance);
    /// Tiles (Chebyshev, at most \a limit) from \a p to the nearest tile of one of our buildings
    /// or a road: how far a point is from the built-up base. Rings outward, bounded box.
    int builtUpDistance(Coord p, int limit) const;
    /// The house attack threshold attack() starts from (credits of military value, before the
    /// campaign adjustment and the wave readiness scaling).
    int houseAttackThreshold() const;
    /// The custom-game threshold actually required (campaign: \a required unchanged): 0 when the
    /// house's credits and the spice left on the map cannot buy the difference to it
    /// (SimpleArmyPolicy::reachableReadiness), and during a whole-army house's held opening (no
    /// starting army able to launch, no wave yet) at most its own threshold share of Medium's
    /// limit on this map (docs/ai-scenario-matrix.md, "Difficulty ladder").
    int reachableAttackThreshold(int required) const;
    /// Bundling waits until the house could attack: its military value reaches the house
    /// attack threshold at the configured wave readiness. Below it - an opening, a rebuild after
    /// losses - the reserve keeps to the home rally and defends the base.
    bool bundleArmyReady() const;
    /// A withdrawal or recovery holds reinforcements at the protected shelter (so does the
    /// garrison, kForceGarrison): bundling and its launch gate stand aside for the home rally then.
    bool reserveHeldHome() const {
        return recoveryActive() && (ArmyPosturePolicy::holdsReinforcementsAtHome(armyPosture) || localSquadWithdraw
                                    || garrisoned());
    }
    /// A reserve unit that has to walk to the bundle, ordered after the unit pass.
    struct BundleWalker { Uint32 id; int distance; Coord at; };
    /// Keeps an idle reserve unit posted at the bundle. False when bundling does not own the
    /// unit (a role, a tracked wave member, a posture that holds reinforcements at home, no
    /// bundle): the home rally decides then. A unit outside the bundle that can take an order
    /// is appended to \a walkers.
    bool bundleUnit(const UnitBase* unit, int radius, std::vector<BundleWalker>& walkers);
    /// Orders at most kBundleOrdersPerPass of \a walkers to the bundle, farthest first (object
    /// id on ties), whatever the order of the unit list.
    void orderBundleWalkers(std::vector<BundleWalker>& walkers, int radius);
    /// Share of \a ids (value, basis points) standing within \a radius of the bundle.
    int bundledShareBps(const std::vector<Uint32>& ids, int radius);
    void saveForceState(OutputStream& stream) const;
    void loadForceState(InputStream& stream);
    /// The remembered enemy army and the musters (SAVEGAMEVERSION 9864); a 9863 save has neither.
    void saveDefenceMemory(OutputStream& stream) const;
    void loadDefenceMemory(InputStream& stream);
    /// A split wave (kForceSplit / kForceFlank): two groups walk to release points on two
    /// sides of the objective, wait for each other and then attack it together.
    struct Split {
        Uint32 since = 0;
        Uint32 objective = NONE_ID;
        Coord target = Coord::Invalid();
        Coord point[2] = {Coord::Invalid(), Coord::Invalid()};
        std::set<Uint32> members[2];
    };
    std::vector<Split> splits;
    const Split* splitOf(Uint32 id) const;
    /// Splits the launched ground units; false (nothing ordered) when the wave cannot split.
    bool startSplit(const std::vector<const UnitBase*>& units, const ObjectBase* objective);
    void updateSplits();
    /// Held by a force controller (a split group, a muster or O's siege hold): no rally or regroup orders.
    bool forceHeld(Uint32 id) const {
        return (!musters.empty() && musterOf(id)) || (!splits.empty() && splitOf(id)) || (!siegeHeld.empty() && siegeHeld.count(id));
    }
    /// Telemetry for the duel reports only: never read by a decision, not saved.
    struct ForceStats {
        int focusSwitches = 0, converges = 0, pooled = 0, recalled = 0, musters = 0, musterReleases = 0,
            stageHolds = 0, staged = 0, bundleOrders = 0, gateHolds = 0, gateTimeouts = 0, splits = 0,
            splitReleases = 0, splitTimeouts = 0, fullReadiness = 0, garrisonHolds = 0, strengthHolds = 0,
            reserveKept = 0, sizedResponses = 0, declinedResponses = 0, fallBacks = 0, perimeterTurretOrders = 0,
            turretsBuilt = 0, turretsLost = 0, raidOrders = 0, turretTargets = 0, pressOnOrders = 0,
            finishWaves = 0, fortifiedHolds = 0, sonicRetargets = 0, surplusPeak = 0, siegeOrders = 0,
            siegeHolds = 0, assaultHolds = 0, assaultWaves = 0;
    };
    ForceStats forceStats;
    /// Staged (+e): is the party strong enough against the visible enemy fighters and turrets
    /// around its objective (ForceConcentrationPolicy::superior)?
    bool huntPartySuperior(const BlobParty& party) const;
    /// Is \a contact (met at \a at) a real fight for this party rather than a lone scout?
    bool huntPartyRealFight(const BlobParty& party, const ObjectBase* contact) const;
    /// Sends the members nearest \a contact (at most three) to shoot it; the party marches on.
    void huntPartySkirmish(const BlobParty& party, const ObjectBase* contact);
    /// Telemetry for the duel reports only: never read by a decision, not saved.
    struct BlobStats {
        int parties = 0, gathered = 0, gatherTimeouts = 0, releases = 0, reforms = 0, fallbacks = 0,
            fallbackReturns = 0, legs = 0, dissolved = 0, stalls = 0, repairs = 0;
    };
    BlobStats blobStats;
    /// This house's waves gather and march as a blob (WaveFormation=Blob, Custom Hard/Brutal).
    bool blobMarch() const;
    /// This house's hunt parties fall back a few tiles to reform when locally outmatched.
    bool tacticalRetreat() const;
    /// Hunt parties run at all (either of the two above).
    bool huntParties() const { return blobMarch() || tacticalRetreat(); }
    /// The party \a id belongs to, or nullptr.
    BlobParty* huntPartyOf(Uint32 id);
    const BlobParty* huntPartyOf(Uint32 id) const;
    /// Makes the launched ground units one hunt party heading for \a objective.
    void startHuntParty(const std::vector<const UnitBase*>& units, const ObjectBase* objective);
    /// One pass over every party (the unit phase of update()).
    void updateHuntParties();
    /// A member was hit: a gathering, marching or reforming party releases into the fight.
    void huntPartyMemberHit(const UnitBase* unit, const ObjectBase* damager);
    /// Ends a party; \a hunt gives its idle members engine Hunt (as None would have).
    void dissolveHuntParty(BlobParty& party, bool hunt, const char* reason);
    void releaseHuntParty(BlobParty& party, Coord contact, const char* reason);
    void startPartyMarch(BlobParty& party);
    void startPartyLeg(BlobParty& party, const UnitBase* anchor);
    void startPartyGather(BlobParty& party, BlobWavePolicy::Phase phase, Coord point);
    void startPartyFallback(BlobParty& party);
    /// Sends every active member to its own tile around \a point in Area Guard; \a onlyIdle
    /// re-sends only members that stand idle away from it.
    void sendHuntParty(const BlobParty& party, Coord point, bool forced, bool onlyIdle);
    /// The member closest to the members' centroid (vehicles first), or nullptr.
    const UnitBase* huntPartyAnchor(const BlobParty& party) const;
    /// A usable rally/leg/fall-back tile near \a desired that a party standing at \a anchor can
    /// reach; \a safe also keeps it out of observed enemy weapon reach.
    Coord huntPartyPoint(Coord desired, const UnitBase* anchor, bool safe) const;
    /// A tile within \a radius of \a centre for \a unit not yet in \a reserved, or Coord::Invalid().
    Coord huntPartySlot(const UnitBase* unit, Coord centre, int radius, std::set<int64_t>& reserved) const;
    /// The visible enemy nearest the anchor within a member's huntTowardRange(), or nullptr.
    const ObjectBase* huntPartyContact(const BlobParty& party, const UnitBase* anchor) const;
    const ObjectBase* huntPartyObjective(const BlobParty& party, const UnitBase* anchor) const;
    void saveHuntParties(OutputStream& stream) const;
    void loadHuntParties(InputStream& stream);
    /// Tracked Custom wave. Same type as the campaign wave, so membership,
    /// objective and save/load are the proven ones.
    CampaignDifficultyPolicy::Wave customWave;
    /// Protected assembly point, chosen for turret cover and distance from the
    /// observed enemy rather than for screening workers.
    Coord protectedRally = Coord::Invalid();
    Uint32 protectedRallyCycle = std::numeric_limits<Uint32>::max();
    /// Corruption bound only. It is NOT a wave size cap: an unlimited or a
    /// ten-thousand unit override can legitimately field far more than a few
    /// thousand attackers, so this is sized to the engine's own object space
    /// rather than to an assumed army size. A count beyond it cannot describe
    /// any reachable world state and means the stream is damaged.
    static constexpr Uint32 kCustomWaveLimit = 1u << 20;
    /// Deterministic fair recall cursor: an index into the wave's sorted member
    /// ids, so every pass continues where the last one stopped and no member can
    /// be starved by a permanently higher-priority neighbour.
    Uint32 recallCursor = 0;
    /// When the main squad's local disadvantage started, for the persistence
    /// requirement. Reset while the squad is not outmatched.
    Uint32 localPressureSince = std::numeric_limits<Uint32>::max();
    /// A sustained local withdrawal holds until the squad is actually home or
    /// the house is ready again. Without this the recall stopped the moment the
    /// enemy drifted out of the measuring radius and the same units resumed the
    /// attack they were being pulled off.
    Uint32 localWithdrawSince = std::numeric_limits<Uint32>::max();
    /// Cycle of the most recent material mobile-combat loss, for the quiet test
    /// that gates every resume path.
    Uint32 lastMaterialLossCycle = std::numeric_limits<Uint32>::max();

    /// True when this controller runs the recovery/cohesion behaviour at all.
    bool recoveryActive() const;
    /// Thresholds from config, converted to cycles once per use.
    ArmyPosturePolicy::Thresholds postureThresholds() const;
    /// One bounded pass over units and structures: deployable power, assembly,
    /// the observed front and the core-attack emergency. Fog and team respecting;
    /// neutral worms and ambient units are never observed as hostile.
    struct ArmySurvey {
        CombatPowerPolicy::Force deployable;   ///< Ours, active, healthy, orderable.
        /// Ours plus allied troops that are actually NEAR the chosen front or the
        /// main cohort. A distant ally's home army is not strength this house can
        /// bring to a fight and must never bypass the outnumbered veto.
        CombatPowerPolicy::Force allied;
        CombatPowerPolicy::Force hostileFront; ///< Observed, near the front anchor.
        CombatPowerPolicy::Force squad;        ///< The tracked wave, where it stands.
        CombatPowerPolicy::Force squadHostile; ///< Observed hostiles around the wave.
        /// Friendly strength near the squad, including allies and the static
        /// cover that actually reaches it, on the same radius as squadHostile.
        CombatPowerPolicy::Force squadSupport;
        /// Wave members actually present in the local engagement. A dispersed
        /// wave's empty centroid cannot establish that its main force is losing.
        int64_t localWavePower = 0;
        /// Healthy deployable military value in credits, over the SAME combat
        /// population the configured militaryValueLimit covers: every armed
        /// unit including aircraft, excluding support, inactive cargo/bay
        /// occupants and units withheld for repair. This is the 80% numerator.
        int64_t deployableValue = 0;
        /// The ground-only part of the same figure, kept separate so the
        /// telemetry can distinguish an air-heavy army from a ground one and so
        /// the ground power comparisons are never contaminated by it.
        int64_t deployableGroundValue = 0;
        int64_t deployableAirValue = 0;
        int64_t assembledPower = 0;            ///< Healthy power already at the rally.
        int64_t designatedPower = 0;           ///< Healthy power that belongs there.
        /// Tracked wave power already inside the assembly radius, for the ready
        /// gate: a wave is ready when it is gathered, not merely when it exists.
        int64_t assembledWavePower = 0;
        bool coreUnderAttack = false;
        bool frontObserved = false;
        Coord frontAnchor = Coord::Invalid();
        int waveMembers = 0;
        /// Local holding strength around the attacked core asset, and the
        /// hostile power actually attacking it. A minor raid that the local
        /// defenders and turret cover outweigh is not a house emergency.
        /// Measured per attacked asset, and reported for the one that actually
        /// decided: a house-wide total lets a strong colony's garrison answer
        /// for a raid on an undefended one, and describes no real place.
        int64_t coreThreatPower = 0;
        int64_t coreHoldingPower = 0;
        bool coreSeriouslyDamaged = false;
        Coord coreAt = Coord::Invalid();
    };
    ArmySurvey surveyArmy(const ObjectBase* plannedFront = nullptr);
    /// Posture evaluation, once per AI pass this house actually takes.
    void updateArmyPosture();
    /// Apply the current posture to units: hold reinforcements, recall the wave.
    /// Bounded by the configured order budget and the path queue; never touches a
    /// unit under a human order, a forced player order, cargo, a repair run or an
    /// emergency base-defence assignment.
    void applyArmyPosture(int& orderBudget);
    /// Withdraw one unit to \a destination. AREAGUARD plus a forced move: the
    /// forced flag suppresses target acquisition on the way (UnitBase.cpp:1756)
    /// and the engine clears it on arrival (UnitBase.cpp:903,962), which is what
    /// re-enables area defence at the rally. RETREAT is deliberately not used:
    /// it returns false from isInGuardRange/isInAttackRange (UnitBase.cpp:1446,
    /// 1492), so a retreating army cannot shoot back at all.
    /// \a radius is the assembly spread around \a anchor, derived from the size
    /// of the force coming home exactly as the established rally radius is.
    /// \a slots is the shared list of standable tiles near the anchor, scanned
    /// once per pass by assemblySlots(); per-unit passability is still checked,
    /// because infantry and vehicles do not share terrain rules.
    bool withdrawUnit(const UnitBase* unit, Coord anchor, int radius = 3,
                      const std::vector<Coord>* slots = nullptr,
                      std::set<int64_t>* reserved = nullptr);
    /// Assembly spread for a force of \a members units: large enough that the
    /// resume share of that force can actually stand inside it. A radius that
    /// cannot hold eighty percent of the army would make "assembled" unreachable
    /// and the house would never resume.
    static int assemblyRadius(int members);
    int assemblyForceSize() const;
    int armyAssemblyRadius() const;
    /// One bounded scan per pass: tiles within \a radius of \a anchor that are
    /// on the map, not mountain, not built on and not in observed danger.
    std::vector<Coord> assemblySlots(Coord anchor, int radius) const;
    /// Is this unit ours to order right now?
    bool orderableCombatUnit(const UnitBase* unit) const;
    /// Is this unit still answering a live emergency contact? Stale entries are
    /// dropped by checkAllUnits(), so a true answer means an actual defence or
    /// anti-air rescue in progress, not a reserve being held back.
    bool activeDefenceAssignment(const UnitBase* unit) const;
    /// Custom Hard/Brutal attacks use engine Hunt without tactical overrides.
    bool engineHuntAttack(const UnitBase* unit) const;
    /// A true launcher — rocket artillery, not a Deviator — attacking under
    /// engine Hunt. Only these keep their close-range spacing while hunting.
    bool huntingLauncher(const UnitBase* unit) const;
    /// How far a hunting launcher should stand off \a threat, or 0 when that
    /// enemy is not close enough to be worth breaking contact for. Proximity is
    /// measured against the enemy's own reach and the inner part of the
    /// launcher's band, never its whole range: an outranged enemy at the edge of
    /// that range is a target, not a threat.
    int launcherEscapeRange(const UnitBase* launcher, const ObjectBase* threat) const;
    /// The enemy a hunting launcher has to step away from this pass (the escape's own
    /// choice: \a knownThreat if it qualifies, else the nearest qualifying enemy in a bounded
    /// tile box, lowest id on ties), or nullptr when the escape does not apply. \a desiredRange
    /// receives the stand-off. Focus fire and converging leave such a launcher to the escape.
    const ObjectBase* closeThreatWhileHunting(const UnitBase* launcher, const ObjectBase* knownThreat = nullptr,
                                              int* desiredRange = nullptr) const;
    /// Short close-range escape for a hunting launcher. \a knownThreat is the
    /// authoritative damager of a damage callback; without one the nearest
    /// qualifying closing enemy is used. Hunt and tracked army membership are
    /// kept, so the launcher resumes firing across the gap it opens. Returns
    /// true when this pass belongs to the escape.
    bool escapeCloseThreatWhileHunting(const UnitBase* launcher,
                                       const ObjectBase* knownThreat = nullptr);
    /// One of this house's colonies: a core building, the core buildings
    /// clustered with it and the living emplacements that actually reach it.
    /// Derived once per search from the structure list, so "colony" means a real
    /// group of this house's own buildings rather than a map region.
    struct ColonyCluster {
        Coord at = Coord::Invalid();
        Uint32 id = 0;
        int cores = 0;     ///< Own core buildings in the cluster.
        int cover = 0;     ///< Living emplacements whose range reaches it.
        int64_t score = 0; ///< The established shelter score.
    };
    std::vector<ColonyCluster> colonyClusters() const;
    /// The production/repair cluster this house would actually shelter at: the
    /// strongest accessible group of its own core buildings, not the arithmetic
    /// mean of every structure it owns. On a two-colony map the mean sits in open
    /// sand between them, which is the worst possible place to gather.
    Coord shelterAnchor();
    /// The colony an offensive should form up at: the most enemy-facing colony
    /// that has real local defence, otherwise the shelter cluster. A forward
    /// colony with no cover is never chosen, because massing the army on exposed
    /// ground is worse than walking a little further.
    Coord stagingAnchor() const;
    /// How far this colony's own buildings actually extend from \a anchor, so a
    /// staging point can be placed outside the built-up area instead of in the
    /// gaps between its buildings.
    int colonyFootprintRadius(Coord anchor) const;
    /// Protected assembly point with hysteresis and a bounded search.
    Coord findProtectedRallyLocation();
    /// Offensive form-up point: a safe, standable, bounded, deterministic slot
    /// just OUTSIDE the staging colony's own footprint, preferring the enemy
    /// facing side and leaving roads and exits clear. An army gathering inside
    /// its own base cannot deploy out of it; the shelter is a separate place and
    /// stays inside. Falls back to the shelter when there is no usable exterior
    /// ground at all, so this is never worse than the previous behaviour.
    Coord offensiveStagingLocation() const;
    /// Actual reserve slots must be exterior too, not just their centre.
    bool offensiveAssemblySlot(Coord at) const;
    /// How far outside the colony footprint the form-up search may look. Fixed
    /// and narrow: it bounds the scan, and "a little outside" is the intent.
    static constexpr int kStagingBand = 8;
    /// Where this house currently gathers: the exterior staging point while
    /// offensive, the protected shelter while withdrawing or recovering, and the
    /// established squad rally outside Custom Hard/Brutal.
    Coord assemblyPoint();
    /// The anchor the assembly measurements and the arrival tests use. Same
    /// posture split as assemblyPoint(), as a const helper for the survey.
    Coord armyAssemblyAnchor() const;
    /// Bounded local protection for a threatened or observed forward colony.
    /// Finite: at most a few units, and never
    /// the troops already committed to an offensive, so a quiet colony cannot
    /// drain the army and a raided one is not simply ignored.
    void assignColonyGuards(int& orderBudget);
    /// Which units this house currently wants holding which colony, derived
    /// fresh from observed world state and cached for the current cycle only.
    /// Derived rather than remembered on purpose: a remembered assignment would
    /// differ between a live host and a reloaded peer on the first pass after a
    /// load, and the posture state that must survive a save is already saved.
    const std::map<Uint32,Coord>& colonyGuardPosts() const;
    mutable std::map<Uint32,Coord> colonyGuardSet;
    mutable Uint32 colonyGuardCycle = std::numeric_limits<Uint32>::max();
    /// Finite reserve. A raided colony gets real help; it can never become a
    /// second army that starves the offensive.
    static constexpr int kColonyGuardMax = 4;
    /// Ground units escorting each colonist (an MCV whose remembered site lies
    /// on another formation), keyed by unit and valued by MCV. Derived from
    /// live state and cached for the current cycle only, for the same reason
    /// as colonyGuardPosts(): the colonist's site is saved, and the escort is
    /// always the nearest free vehicles to it. Escorts take precedence over
    /// colony guards, attacks and regrouping.
    const std::map<Uint32,Uint32>& colonistEscorts() const;
    mutable std::map<Uint32,Uint32> colonistEscortSet;
    mutable Uint32 colonistEscortCycle = std::numeric_limits<Uint32>::max();
    static constexpr int kColonistEscortSize = 3;
    /// An escort within this many tiles has gathered beside its colonist.
    static constexpr int kEscortAssemblyRadius = 5;
    /// Keeps an escort guarding beside its colonist. True when \a unit is one,
    /// so no other controller orders it this pass.
    bool escortColonist(const UnitBase* unit);
    /// Escort upkeep between passes, every cycle: while a colonist drives, each escort whose
    /// order no longer leads to its ring place is sent on again (escortColonist), so a place
    /// that moved with the MCV's tile is followed at once. Derived from live state only.
    void keepColonistEscortsInPlace();
    /// An escort walking to within this many tiles of its ring place keeps its route.
    static constexpr int kEscortPlaceSlack = 2;
    /// The escort's place in the ring round its colonist (docs/formations.md): relative to the
    /// MCV's tile and heading, sonic escorts in the front row; \a lead MCV tiles further along
    /// the heading; on an unpassable place, the nearest passable tile on the same side of the
    /// MCV. Invalid when it has none.
    Coord colonistEscortPlace(const UnitBase* escort, const ObjectBase* colonist, int lead = 0) const;
    /// Escorts assigned to this colonist, and whether all of them have gathered.
    int colonistEscortCount(const UnitBase* mcv, bool* gathered = nullptr) const;
    /// Runtime-only exterior staging cache. Recomputed whenever the cycle
    /// changes, exactly like lastSurvey, so it is never carried across a save
    /// and a reload reproduces it from live state rather than restoring it.
    mutable Coord offensiveStaging = Coord::Invalid();
    mutable Uint32 offensiveStagingCycle = std::numeric_limits<Uint32>::max();
    mutable Coord offensiveColonyAnchor = Coord::Invalid();
    mutable int offensiveColonyFootprint = 0;
    mutable bool exteriorStagingAvailable = false;
    /// Tracked-wave bookkeeping: drop the dead, expire a stalled sortie, keep the
    /// shared objective. Mirrors the campaign wave rules for Custom Hard/Brutal.
    void updateCustomWave();
    /// Shared reachable objective for a Custom wave, or null when none is
    /// observed. Visible enemy structures only, and only ones the wave can
    /// actually reach over ground: an objective behind impassable terrain
    /// otherwise sends the whole wave into a forced path search it can never
    /// satisfy.
    const ObjectBase* customWaveObjective(const UnitBase* unit) const;
    /// Can this ground unit actually reach attacking range of this object over
    /// terrain it can cross? A bounded reachability answer, not a full path.
    bool groundAttackReachable(const UnitBase* unit, const ObjectBase* target) const;
    /// Adopt this house's existing autonomous attackers into the tracked wave.
    /// A save written before the posture existed carries Hard/Brutal units that
    /// are already hunting; without this they would never be recalled, because
    /// the recall only ever touches tracked members.
    void adoptLegacyHuntersIntoWave();
    bool legacyHuntersAdopted = false;
    /// Outnumbered dispatch gate, including the 80%-of-configured-limit bypass.
    ArmyPosturePolicy::DispatchGate offensiveDispatchGate(const ArmySurvey& survey) const;
    /// Front battery goal for this base, zero when the feature is off or the
    /// economy is not ready. Never lowers the established counter-air goal.
    int frontBatteryGoal(int coverageCap, int refineries, int heavyFactories, int repairYards) const;
    /// Observed enemy approach direction for battery siting, from visible enemy
    /// bases and remembered structure losses only.
    Coord observedFrontDirection(Coord anchor) const;
    /// Cached survey for this AI pass. Runtime only: recomputed by
    /// updateArmyPosture() on every pass this house takes, so a reload or an
    /// observer checkpoint reproduces it from live state rather than carrying it.
    ArmySurvey lastSurvey;
    Uint32 lastSurveyCycle = std::numeric_limits<Uint32>::max();
    /// The tracked squad is locally outmatched and should disengage even though
    /// the house as a whole is still offensive. Recomputed every pass alongside
    /// the survey, so it is derived state and not part of the save.
    bool localSquadWithdraw = false;

    void checkAllUnits();
    void retreatAllUnits();
    void build(int militaryValue);
    void attack(int militaryValue);
    void manageCityBuilding();
    std::map<Uint32,Uint32> roadRedirectRetryCycle;
    Coord findFinishedRoadSite(const BuilderBase* yard);
    std::vector<std::pair<int,int>> cityRoadRepairSites();
    int queueCityRoadRepairs(const BuilderBase* yard, int limit);

    Sint32 cityBuildTimer = 0;
};

#endif //QuantBot_H
