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
#include <units/MCV.h>
class Harvester;
#include <players/QuantBotConfig.h>
#include <players/QuantBotCityCampaignPolicy.h>
#include <players/CampaignDifficultyPolicy.h>
#include <players/AIDecisionLog.h>
#include <dunecity/SpatialFields.h>

#include <DataTypes.h>
#include <limits>
#include <set>
#include <map>
#include <unordered_map>
#include <array>

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
    const std::array<int, 8>& getLastUnitMixBps() const { return lastUnitMixBps; }
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
    std::array<int, 8> lastUnitMixBps{};
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
    /// Is this MCV's job settling another formation rather than growing the
    /// base? Only a built-out base with a surveyed destination sends one away.
    bool colonyMissionDue() const;
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
    bool preservesGroundAccess(Uint32 item, Coord pos);
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
    // preferHunting=false returns the body at home instead of the attack
    // centroid, for troops that must not be dragged towards the front.
    Coord findSquadCenter(int houseID, bool preferHunting = true);
    Coord findBaseCentre(int houseID);
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
    /// \a needsLocalSpace waives the core prerequisite (heavy factory, high
    /// tech factory, repair yard) exactly as a measured built-out base does,
    /// for a caller that has already established there is no usable site at
    /// home. Defence cover, threat and recent-loss checks are unaffected.
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
    Uint32 planningBuilder = NONE_ID;
    bool overlapsReservedStructure(int x, int y, int width, int height) const;
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
