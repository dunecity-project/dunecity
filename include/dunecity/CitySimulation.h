#include <algorithm>
#include <vector>
#ifndef DUNECITY_CITYSIMULATION_H
#define DUNECITY_CITYSIMULATION_H

#include <cstdint>
#include <DataTypes.h>
#include <dunecity/CityConstants.h>
#include <dunecity/CityMapLayer.h>
#include <dunecity/CityBudget.h>
#include <dunecity/CrimeUnrestPolicy.h>
#include <dunecity/CityDemandNoticePolicy.h>
#include <dunecity/ParkTerrainPolicy.h>

class InputStream;
class OutputStream;

namespace DuneCity {

/// Per-house city state — population, demand, economy, civic buildings.
/// Density maps (pollution, crime, land value) remain global (shared environment).
static constexpr int kMaxCityHouses = NUM_HOUSES;

struct HouseCityState {
    int resPop = 0, comPop = 0, indPop = 0;
    uint8_t civicDemandBlocked = 0; // Derived from the latest demand calculation; not serialized.
    int taxBaseEighths = 0; // Derived (R/8+C+I) in eighths, zones + Palace; not serialized.
    int prevResPop = 0, prevComPop = 0, prevIndPop = 0;
    int16_t resValve = 0, comValve = 0, indValve = 0;
    int avgLandValue = 0;
    int unemploymentRate = 0;
    int hospitalCount = 0, churchCount = 0;
    bool hasStadium = false, hasPalace = false, hasAirport = false, hasStarport = false;
    int policeFundingPercent = 100;
    int32_t nominalPoliceCost = 0;
    int32_t lastPoliceExpense = 0;
    CityBudget budget;

    int getTotalPop() const { return resPop + comPop + indPop; }

    // SAVEGAMEVERSION 9818 persists every house slot, fixing R/C/I state
    // loss when the local campaign player is not house zero.
    void save(class OutputStream& stream) const;
    void load(class InputStream& stream);
};

/// Live environmental summary for one house's developed property. This is
/// derived from the current effect maps after every scan rather than saved:
/// it is presentation/diagnostic state, not simulation input.
struct CityEnvironmentStatus {
    int averageLandValue = 0;
    int averagePollution = 0;
    int averageCrime = 0;
    int averageTraffic = 0;
    int sampledStructures = 0;
};

class CitySimulation {
public:
    CitySimulation();
    ~CitySimulation();

    void init(int width, int height);
    void load(InputStream& stream);
    void save(OutputStream& stream) const;
    // Exact live checkpoint supplement; ordinary disk saves still reconcile on load.
    void saveObserverRuntime(OutputStream& stream) const;
    void loadObserverRuntime(InputStream& stream);
    void reconcileLoadedMapState(uint32_t gameCycleCount);
    void advancePhase(uint32_t gameCycleCount);

    bool isInitialized() const { return initialized_; }

    static void executeCityCommand(int playerID, int commandID,
                                   uint32_t p0, uint32_t p1, uint32_t p2);

    void registerPowerSource(int x, int y, int power);

    // Per-house city state access
    const HouseCityState& getHouseState(int houseID) const;
    HouseCityState& getHouseStateMut(int houseID);

    // Raw internal population (SC units — local player, for UI)
    int getResPop() const;
    int getComPop() const;
    int getIndPop() const;
    int getTotalPop() const;
    int getTaxBaseEighths() const;

    // Display population (SC multiplied for UI — what players see)
    static constexpr int kPopDisplayMultiplier = 20;
    int getDisplayResPop() const { return getResPop() * kPopDisplayMultiplier; }
    int getDisplayComPop() const { return getComPop() * kPopDisplayMultiplier; }
    int getDisplayIndPop() const { return getIndPop() * kPopDisplayMultiplier; }
    int getDisplayTotalPop() const { return getTotalPop() * kPopDisplayMultiplier; }

    int16_t getResValve() const;
    int16_t getComValve() const;
    int16_t getIndValve() const;

    int32_t getTotalFunds() const;
    void setTotalFunds(int32_t v) { totalFunds_ = v; }
    int16_t getCityTax() const { return cityTax_; }
    /// Set the tax rate (percentage of population paid annually). Clamped
    /// to [kMinTaxRate, kMaxTaxRate] — outside that band the budget either
    /// stops collecting (0%) or crushes growth (20%+).
    void setCityTax(int16_t v);
    static constexpr int16_t kMinTaxRate = 0;
    static constexpr int16_t kMaxTaxRate = 20;
    int getCityYear() const { return cityYear_; }
    int getCityDay()  const { return cityDay_; }

    /// Police-funding %: 0..100, default 100. Scales coverage from each
    /// police-role structure proportionally and is also the share of
    /// annual police cost actually paid into the budget.
    /// These delegate to local player's house state.
    int getPoliceFundingPercent() const;
    void setPoliceFundingPercent(int v);
    int getPoliceFundingPercent(int houseID) const {
        return houseID >= 0 && houseID < kMaxCityHouses ? houseState_[houseID].policeFundingPercent : 100;
    }
    void setPoliceFundingPercent(int houseID, int value) {
        if (houseID >= 0 && houseID < kMaxCityHouses)
            houseState_[houseID].policeFundingPercent = std::clamp(value,0,100);
    }
    /// Last computed police annual expense (full nominal, before funding%).
    int32_t getNominalPoliceCost() const;
    /// Last actual amount paid out (nominal * funding%/100).
    int32_t getLastPoliceExpense() const;

    int getMapWidth() const { return mapWidth_; }
    int getMapHeight() const { return mapHeight_; }

    bool isPowerShortageJustStarted() const { return false; }
    int32_t getUnpoweredZoneCount() const { return 0; }

    /// Average land value across developed blocks (0-250), refreshed each scan.
    /// Local player version for UI; per-house via getHouseState().
    int getAvgLandValue() const;

    /// Unemployment rate (0-100%), refreshed each zone growth pass.
    int getUnemploymentRate() const;

    /// Hospital/church count: auto-created by the game on residential zones.
    int getHospitalCount() const;
    int getChurchCount() const;

    /// Current city-wide environmental summary for a house. The no-argument
    /// UI getters above use the local house; this supports spectators and
    /// QuantBot analytics without treating local UI state as game state.
    const CityEnvironmentStatus& getEnvironmentStatus(int houseID) const;
    int getAveragePollution() const;
    int getAverageCrime() const;
    int getAverageTraffic() const;

    /// Civic building presence flags (refreshed each scan).
    bool getHasStadium() const;
    bool getHasAirport() const;

    /// City Effects feature flag (Phase 6). When false, all effect scans
    /// are skipped and the maps stay at zero. Set from Game options at init.
    void setCityEffectsEnabled(bool enabled) { effectsEnabled_ = enabled; }
    bool areCityEffectsEnabled() const { return effectsEnabled_; }

    const CityMapLayer<uint8_t>& getPowerGridMap() const { return powerGridMap_; }
    const CityMapLayer<uint8_t>& getTrafficDensityMap() const { return trafficDensityMap_; }
    const CityMapLayer<uint8_t>& getPollutionDensityMap() const { return pollutionDensityMap_; }
    const CityMapLayer<uint8_t>& getLandValueMap() const { return landValueMap_; }
    const ParkTerrainPolicy& getParkTerrain() const { return parkTerrain_; }
    const CityMapLayer<int32_t>& getPoliceCoverageMap() const { return policeCoverageMap_; }
    const CityMapLayer<uint16_t>& getCrimeBeforePoliceMap() const { return crimeBeforePoliceMap_; }
    const CityMapLayer<uint16_t>& getCrimeRateMap() const { return crimeRateMap_; }
    const CityMapLayer<uint8_t>& getPopulationDensityMap() const { return populationDensityMap_; }
    const CityMapLayer<int8_t>&  getGrowthRateMap() const { return growthRateMap_; }

    CityBudget& getCityBudget();
    const CityBudget& getCityBudget() const;

    // Milestone stub methods (Phase 9 - declared but not yet implemented)
    bool wasFirstZoneMilestoneTriggered() const { return milestoneFirstZoneAchieved_; }
    void onFirstZoneBuilt() { milestoneFirstZoneAchieved_ = true; }

    // Milestone tracking
    enum MilestoneType {
        MILESTONE_POPULATION_100 = 0,
        MILESTONE_POPULATION_500,
        MILESTONE_POPULATION_1000,
        MILESTONE_POPULATION_5000,
        MILESTONE_FIRST_ZONE,
        MILESTONE_ECONOMY_1000,
        MILESTONE_ECONOMY_5000,
        MILESTONE_ALL_ZONES,
        NUM_MILESTONES
    };
    bool hasMilestoneBeenAchieved(int milestone) const {
        return milestone >= 0 && milestone < NUM_MILESTONES && milestones_[milestone];
    }

    // Phase/milestone methods used by Game.cpp
    int getPhaseCycle() const { return 9; } // Always return 9 so milestone check runs
    void checkAndTriggerMilestones(int32_t totalPop, int32_t totalFunds, int32_t resPop, int32_t comPop, int32_t indPop, bool hasRoads);
    /// Deduct from the player's credit pool. Returns false (no deduction)
    /// if insufficient funds. Implemented in CityEffectsRuntime.cpp so it
    /// can access pLocalHouse directly.
    bool spendCityFunds(int32_t amount);

private:
    bool initialized_ = false;
    bool milestones_[NUM_MILESTONES] = {};
    bool milestoneFirstZoneAchieved_ = false;
    int mapWidth_  = 0;
    int mapHeight_ = 0;

    /// Default tax rate used at game start. 7% sits between SC Classic's
    /// suggested 6% and the punitive 10%+ band — keeps growth healthy
    /// while making the budget meaningful. Player can adjust at runtime
    /// via setCityTax() / the budget window.
    static constexpr int16_t kDefaultTaxRate = 7;

    /// Per-house city state (population, demand, economy, civic buildings).
    HouseCityState houseState_[kMaxCityHouses];
    CityEnvironmentStatus environmentStatus_[kMaxCityHouses];
    bool crimeWarningActive_[kMaxCityHouses] = {};
    CityDemandNoticePolicy civicDemandNotices_[kMaxCityHouses];

    int32_t totalFunds_ = 0;
    int16_t cityTax_ = kDefaultTaxRate;
    int cityYear_ = 0;

    CityMapLayer<uint8_t> powerGridMap_;
    CityMapLayer<uint8_t> trafficDensityMap_;
    CityMapLayer<uint8_t> pollutionDensityMap_;
    CityMapLayer<uint8_t> landValueMap_;
    ParkTerrainPolicy parkTerrain_; // derived from structures; rebuilt, not serialized
    CityMapLayer<uint8_t> hostileLandValuePenaltyMap_;
    CityMapLayer<uint16_t> crimeRateMap_;
    std::vector<CrimeUnrestDistrict> crimeUnrestProgress_; // Per-house 16x16 districts; persisted.
    CityMapLayer<int32_t> policeCoverageMap_; // derived diagnostics, rebuilt on scan/load
    CityMapLayer<uint16_t> crimeBeforePoliceMap_;
    CityMapLayer<uint8_t> populationDensityMap_;
    CityMapLayer<int8_t>  growthRateMap_;

    bool      effectsEnabled_ = true;   ///< City effects feature flag
    uint32_t  lastProcessedDay_ = 0;    ///< Last city-day number that ran a scan + growth roll
    uint32_t  lastTaxYear_ = 0;         ///< Legacy (unused) — kept for save-file compatibility
    uint32_t  lastBudgetTick_ = 0;      ///< Last budget tick number (1 per second, pays 1/50 annual)
    int       cityDay_ = 0;             ///< Current city day (resets within the year, monotonic across years)
    bool      pendingGrowthPhase_ = false; ///< true when runEffectsScans() ran this day but runZoneGrowth() is deferred to next cycle

    // Civic flags are now per-house in houseState_[].

    /// Full-map scan: rebuilds pollution, land value, crime maps and runs
    /// zone growth from current map contents. Called from advancePhase()
    /// at a slow cadence (every kEffectsScanCycles). Pure read of game
    /// state, deterministic — safe in multiplayer.
    void runEffectsScans();
    void runZoneGrowth();

    /// Year-tick budget pass: walks all structures on the map, aggregates
    /// population and police cost per house, then applies (tax revenue -
    /// Distribute 1/48th of the annual budget each city day-tick: tax
    /// revenue minus funded police bill. Smooth income instead of a
    /// yearly lump sum. Every owner with city-role structures pays
    /// their own bill. Lives in CityEffectsRuntime.cpp.
    void runDailyBudget();

    /// Decay growth rate map toward zero each scan tick.
    void decayGrowthRateMap();
};

} // namespace DuneCity

#endif // DUNECITY_CITYSIMULATION_H
