#include <stdexcept>
#include <limits>
#include <type_traits>
#include <players/AIDecisionLog.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/CityConstants.h>
#include <dunecity/CityEffects.h>

#include <globals.h>
#include <Game.h>
#include <House.h>
#include <players/Player.h>
#include <Map.h>
#include <Tile.h>
#include <Command.h>
#include <CommandAuthorization.h>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>

#include <SDL2/SDL_log.h>

namespace DuneCity {

uint32_t cyclesPerCityYear() {
    return currentGame != nullptr && currentGame->getGameInitSettings().getGameOptions().slowCityYear
        ? 2 * kCyclesPerMinute : kCyclesPerMinute;
}

CitySimulation::CitySimulation() = default;
CitySimulation::~CitySimulation() = default;

const HouseCityState& CitySimulation::getHouseState(int houseID) const {
    if (houseID < 0 || houseID >= kMaxCityHouses) houseID = 0;
    return houseState_[houseID];
}
HouseCityState& CitySimulation::getHouseStateMut(int houseID) {
    if (houseID < 0 || houseID >= kMaxCityHouses) houseID = 0;
    return houseState_[houseID];
}

void CitySimulation::setCityTax(int16_t v) {
    if (v < kMinTaxRate) v = kMinTaxRate;
    if (v > kMaxTaxRate) v = kMaxTaxRate;
    cityTax_ = v;
}

void CitySimulation::init(int width, int height) {
    mapWidth_  = width;
    mapHeight_ = height;

    powerGridMap_.init(width, height, 2);
    trafficDensityMap_.init(width, height, 2);
    pollutionDensityMap_.init(width, height, 2);
    landValueMap_.init(width, height, 2);
    parkTerrain_.init(width, height);
    crimeRateMap_.init(width, height, 2);
    crimeUnrestProgress_.assign(kMaxCityHouses * ((width+15)/16) * ((height+15)/16), {});
    policeCoverageMap_.init(width, height, 2);
    crimeBeforePoliceMap_.init(width, height, 2);
    populationDensityMap_.init(width, height, 2);
    growthRateMap_.init(width, height, 2);

    initialized_ = true;
    SDL_Log("[CitySim] initialized %dx%d", width, height);
}

void CitySimulation::load(InputStream& stream) {
    // SAVEGAMEVERSION 9809+ persists the full sim state. Older saves
    // wrote nothing in this slot, so just leave the freshly-initialised
    // defaults in place and let the first effects scan rebuild stats.
    if (!currentGame || currentGame->getLoadedSavegameVersion() < 9809) {
        return;
    }

    // SAVEGAMEVERSION 9818+ persists every kMaxCityHouses HouseCityState slot
    // in a freshly-reordered layout (see CitySimulation::save). Older saves
    // (9809..9817 inclusive) wrote only houseState_[0] AND in a DIFFERENT
    // field order (resPop/valve sandwiched around the global sim fields).
    // Both branches are kept so existing saves still load - the bug this
    // fixes: in any campaign where the local player was NOT house 0 (Ordos =
    // 2, Atreides = 1, etc.), R/C/I / Pop / valves all read 0 after a reload
    // because the player's slot was never on disk. Pre-9818, only Harkonnen's
    // data round-tripped; everyone else's resPop/comPop/indPop was zero.
    const int loadedVersion = currentGame->getLoadedSavegameVersion();

    if (loadedVersion >= 9818) {
        // --- New format (9818+) ---
        // Global sim fields (same across all houses - read once)
        totalFunds_              = stream.readSint32();
        cityTax_                 = stream.readSint16();
        cityYear_                = stream.readSint32();
        cityDay_                 = stream.readSint32();
        lastProcessedDay_        = stream.readUint32();
        lastTaxYear_             = stream.readUint32();
        lastBudgetTick_          = (lastProcessedDay_ * cyclesPerCityDay()) / kCyclesPerBudgetTick;
        stream.readSint32(); // retired economic-victory threshold (no population victory); discarded

        const int savedCityHouseCount = loadedVersion >= 9823
            ? kMaxCityHouses
            : (loadedVersion >= 9821 ? NUM_CAMPAIGN_HOUSES : NUM_LEGACY_HOUSES);
        for (int h = 0; h < savedCityHouseCount; ++h) {
            houseState_[h].load(stream);
        }
    } else {
        // --- Legacy format (9809..9817) ---
        // Stream order is: [houseState_[0].resPop/valve] [global fields]
        // [houseState_[0].police/budget] [milestones]. Note that fields like
        // prevResPop / avgLandValue / hasStadium didn't exist on disk pre-9818,
        // so we leave them at their freshly-initialised defaults. Remaining
        // slots (1..7) also stay at defaults - same as before the fix, so this
        // isn't a regression.
        houseState_[0].resPop     = stream.readSint32();
        houseState_[0].comPop     = stream.readSint32();
        houseState_[0].indPop     = stream.readSint32();
        houseState_[0].prevResPop = houseState_[0].resPop;
        houseState_[0].prevComPop = houseState_[0].comPop;
        houseState_[0].prevIndPop = houseState_[0].indPop;
        houseState_[0].resValve   = stream.readSint16();
        houseState_[0].comValve   = stream.readSint16();
        houseState_[0].indValve   = stream.readSint16();

        // Global sim fields (mid-stream in the old layout)
        totalFunds_              = stream.readSint32();
        cityTax_                 = stream.readSint16();
        cityYear_                = stream.readSint32();
        cityDay_                 = stream.readSint32();
        lastProcessedDay_        = stream.readUint32();
        lastTaxYear_             = stream.readUint32();
        lastBudgetTick_          = (lastProcessedDay_ * cyclesPerCityDay()) / kCyclesPerBudgetTick;
        stream.readSint32(); // retired economic-victory threshold (no population victory); discarded

        // policeFunding / nominalCost / lastExpense / budget come AFTER global fields
        // in the old format (and are part of each house slot in the new format).
        houseState_[0].policeFundingPercent = stream.readSint32();
        houseState_[0].nominalPoliceCost    = stream.readSint32();
        houseState_[0].lastPoliceExpense    = stream.readSint32();

        houseState_[0].budget.setLastTaxRevenue(stream.readSint32());
        houseState_[0].budget.setRoadPercent(stream.readSint32());
        houseState_[0].budget.setPolicePercent(stream.readSint32());
        houseState_[0].budget.setFirePercent(stream.readSint32());
    }

    milestoneFirstZoneAchieved_ = stream.readBool();
    for (int i = 0; i < NUM_MILESTONES; ++i) {
        milestones_[i] = stream.readBool();
    }
    if (loadedVersion >= 9825) loadCrimeUnrest(stream,crimeUnrestProgress_,loadedVersion);
}

void CitySimulation::save(OutputStream& stream) const {
    // Global sim fields (same across all houses - written once)
    stream.writeSint32(totalFunds_);
    stream.writeSint16(cityTax_);
    stream.writeSint32(cityYear_);
    stream.writeSint32(cityDay_);
    stream.writeUint32(lastProcessedDay_);
    stream.writeUint32(lastTaxYear_);
    stream.writeSint32(0); // retired economic-victory threshold slot, kept so the 9865 layout is unchanged

    // SAVEGAMEVERSION 9818+ persists all kMaxCityHouses slots.
    for (int h = 0; h < kMaxCityHouses; ++h) {
        houseState_[h].save(stream);
    }

    stream.writeBool(milestoneFirstZoneAchieved_);
    for (int i = 0; i < NUM_MILESTONES; ++i) {
        stream.writeBool(milestones_[i]);
    }
    saveCrimeUnrest(stream,crimeUnrestProgress_);
}

void CitySimulation::saveObserverRuntime(OutputStream& stream) const {
    stream.writeSint32(mapWidth_); stream.writeSint32(mapHeight_);
    stream.writeUint32(lastBudgetTick_); stream.writeBool(pendingGrowthPhase_);
    stream.writeBool(effectsEnabled_);
    for(int h=0;h<kMaxCityHouses;++h) {
        stream.writeSint32(houseState_[h].taxBaseEighths);
        stream.writeUint8(houseState_[h].civicDemandBlocked);
        const auto& status=environmentStatus_[h];
        stream.writeSint32(status.averageLandValue); stream.writeSint32(status.averagePollution);
        stream.writeSint32(status.averageCrime); stream.writeSint32(status.averageTraffic);
        stream.writeSint32(status.sampledStructures);
    }
    const auto layer=[&](const auto& map) {
        const int bs=map.getBlockSize();
        for(int y=0;y<(mapHeight_+bs-1)/bs;++y)
            for(int x=0;x<(mapWidth_+bs-1)/bs;++x) stream.writeSint32(map.get(x,y));
    };
    layer(powerGridMap_); layer(trafficDensityMap_); layer(pollutionDensityMap_);
    layer(landValueMap_); layer(hostileLandValuePenaltyMap_); layer(crimeRateMap_);
    layer(policeCoverageMap_); layer(crimeBeforePoliceMap_); layer(populationDensityMap_);
    layer(growthRateMap_); layer(parkTerrain_.sources_);
}

void CitySimulation::loadObserverRuntime(InputStream& stream) {
    if(stream.readSint32()!=mapWidth_ || stream.readSint32()!=mapHeight_)
        throw std::runtime_error("Invalid spectator city dimensions");
    lastBudgetTick_=stream.readUint32(); pendingGrowthPhase_=stream.readBool();
    effectsEnabled_=stream.readBool();
    for(int h=0;h<kMaxCityHouses;++h) {
        houseState_[h].taxBaseEighths=stream.readSint32();
        houseState_[h].civicDemandBlocked=stream.readUint8();
        auto& status=environmentStatus_[h];
        status.averageLandValue=stream.readSint32(); status.averagePollution=stream.readSint32();
        status.averageCrime=stream.readSint32(); status.averageTraffic=stream.readSint32();
        status.sampledStructures=stream.readSint32();
    }
    const auto layer=[&](auto& map) {
        const int bs=map.getBlockSize();
        using Value=std::decay_t<decltype(map.get(0,0))>;
        map.init(mapWidth_,mapHeight_,bs);
        for(int y=0;y<(mapHeight_+bs-1)/bs;++y) for(int x=0;x<(mapWidth_+bs-1)/bs;++x) {
            const auto value=stream.readSint32();
            if(value<std::numeric_limits<Value>::min() || value>std::numeric_limits<Value>::max())
                throw std::runtime_error("Invalid spectator city layer value");
            map.set(x,y,static_cast<Value>(value));
        }
    };
    layer(powerGridMap_); layer(trafficDensityMap_); layer(pollutionDensityMap_);
    layer(landValueMap_); layer(hostileLandValuePenaltyMap_); layer(crimeRateMap_);
    layer(policeCoverageMap_); layer(crimeBeforePoliceMap_); layer(populationDensityMap_);
    layer(growthRateMap_); layer(parkTerrain_.sources_);
}

// --- HouseCityState self-serializing impls ---
// Factored out of CitySimulation::save/load so future field additions live in
// one place. Read/write order MUST stay in lockstep.

void HouseCityState::save(OutputStream& stream) const {
    stream.writeSint32(resPop);
    stream.writeSint32(comPop);
    stream.writeSint32(indPop);
    stream.writeSint32(prevResPop);
    stream.writeSint32(prevComPop);
    stream.writeSint32(prevIndPop);
    stream.writeSint16(resValve);
    stream.writeSint16(comValve);
    stream.writeSint16(indValve);

    stream.writeSint32(avgLandValue);
    stream.writeSint32(unemploymentRate);
    stream.writeSint32(hospitalCount);
    stream.writeSint32(churchCount);
    stream.writeBool(hasStadium);
    stream.writeBool(hasPalace);
    stream.writeBool(hasAirport);
    stream.writeBool(hasStarport);

    stream.writeSint32(policeFundingPercent);
    stream.writeSint32(nominalPoliceCost);
    stream.writeSint32(lastPoliceExpense);

    stream.writeSint32(budget.getLastTaxRevenue());
    stream.writeSint32(budget.getRoadPercent());
    stream.writeSint32(budget.getPolicePercent());
    stream.writeSint32(budget.getFirePercent());
}

void HouseCityState::load(InputStream& stream) {
    taxBaseEighths = 0;
    resPop     = stream.readSint32();
    comPop     = stream.readSint32();
    indPop     = stream.readSint32();
    prevResPop = stream.readSint32();
    prevComPop = stream.readSint32();
    prevIndPop = stream.readSint32();
    resValve   = stream.readSint16();
    comValve   = stream.readSint16();
    indValve   = stream.readSint16();

    avgLandValue    = stream.readSint32();
    unemploymentRate = stream.readSint32();
    hospitalCount   = stream.readSint32();
    churchCount     = stream.readSint32();
    hasStadium      = stream.readBool();
    hasPalace       = stream.readBool();
    hasAirport      = stream.readBool();
    hasStarport     = stream.readBool();

    policeFundingPercent = stream.readSint32();
    nominalPoliceCost    = stream.readSint32();
    lastPoliceExpense    = stream.readSint32();

    budget.setLastTaxRevenue(stream.readSint32());
    budget.setRoadPercent(stream.readSint32());
    budget.setPolicePercent(stream.readSint32());
    budget.setFirePercent(stream.readSint32());
}

void CitySimulation::advancePhase(uint32_t gameCycleCount) {
    if (!effectsEnabled_ || !initialized_) return;

    // City clock — SimCity Classic-style. A year is 48 day-ticks; each
    // day ticks at gameCycleCount / cyclesPerCityDay(). Day ticks drive
    // effects scans and zone growth; year ticks drive taxes/budget.
    const uint32_t totalDays = gameCycleCount / cyclesPerCityDay();
    cityYear_ = static_cast<int>(totalDays / kCityDaysPerYear);
    cityDay_  = static_cast<int>(totalDays % kCityDaysPerYear);

    // Budget tick: runs every ~1 second (62 cycles), pays 1/50th of the
    // annual budget. Decoupled from the day-tick so income is smooth.
    const uint32_t budgetTick = gameCycleCount / kCyclesPerBudgetTick;
    if (budgetTick > lastBudgetTick_) {
        lastBudgetTick_ = budgetTick;
        {
            AITelemetry::PerformanceScope perfScope("city.budget", gameCycleCount);
            runDailyBudget();
        }
    }

    // Day tick: run effects scans and zone growth on SEPARATE cycles to
    // halve the per-frame spike. Effects scan runs on the day boundary;
    // zone growth runs on the next game cycle via pendingGrowthPhase_.
    if (pendingGrowthPhase_) {
        pendingGrowthPhase_ = false;
        {
            AITelemetry::PerformanceScope perfScope("city.growth", gameCycleCount);
            runZoneGrowth();
        }
    }

    if (totalDays > lastProcessedDay_) {
        lastProcessedDay_ = totalDays;
        {
            AITelemetry::PerformanceScope perfScope("city.effects", gameCycleCount);
            runEffectsScans();
        }
        pendingGrowthPhase_ = true;

        // Diagnostic snapshot every 8 city days (~1/6 city year). Logs
        // average/max land value, crime, pollution across DEVELOPED
        // blocks only — undeveloped desert always sits at 0 and would
        // dominate the mean. Lets us tell at a glance whether one side
        // of the map is starving on land value or drowning in crime.
        if (cityDay_ % 8 == 0) {
            const int bs = std::max(1, landValueMap_.getBlockSize());
            const int bw = (mapWidth_  + bs - 1) / bs;
            const int bh = (mapHeight_ + bs - 1) / bs;
            int devBlocks = 0;
            long long lvSum = 0, crSum = 0, poSum = 0;
            int lvMax = 0, crMax = 0, poMax = 0;
            for (int by = 0; by < bh; ++by) {
                for (int bx = 0; bx < bw; ++bx) {
                    const int lv = landValueMap_.get(bx, by);
                    if (lv <= 0) continue;
                    ++devBlocks;
                    const int cr = crimeRateMap_.get(bx, by);
                    const int po = pollutionDensityMap_.get(bx, by);
                    lvSum += lv; crSum += cr; poSum += po;
                    if (lv > lvMax) lvMax = lv;
                    if (cr > crMax) crMax = cr;
                    if (po > poMax) poMax = po;
                }
            }
            if (devBlocks > 0) {
                SDL_Log("[CitySim] day=%d/%d devBlocks=%d "
                        "lv(avg/max)=%lld/%d crime=%lld/%d pol=%lld/%d "
                        "popR/C/I=%d/%d/%d valves=R%+d C%+d I%+d",
                        cityYear_, cityDay_, devBlocks,
                        lvSum / devBlocks, lvMax,
                        crSum / devBlocks, crMax,
                        poSum / devBlocks, poMax,
                        getResPop(), getComPop(), getIndPop(),
                        getResValve(), getComValve(), getIndValve());
            }
        }
    }
}

void CitySimulation::registerPowerSource(int /*x*/, int /*y*/, int /*power*/) {
    // Stub — power grid registration not yet implemented
}

void CitySimulation::executeCityCommand(int playerID, int commandID,
                                        uint32_t p0, uint32_t p1, uint32_t p2) {
    if(!currentGameMap) return;

    // City commands carry no acting object, so the issuer is the whole authorization: a
    // command from a player that does not exist (or has no house) is a deterministic no-op on
    // every peer. The parameter ranges are checked here too, because these values are cast
    // straight into enums and simulation state.
    const Player* issuer = (currentGame && playerID >= 0 && playerID <= 255)
        ? currentGame->getPlayerByID(static_cast<Uint8>(playerID))
        : nullptr;
    if(issuer == nullptr || issuer->getHouse() == nullptr) {
        return;
    }

    switch(commandID) {
        case CMD_CITY_TOOL: {
            int x = static_cast<int>(p0);
            int y = static_cast<int>(p1);
            uint32_t toolType = p2;

            if(!currentGameMap->tileExists(x, y)) return;

            Tile* tile = currentGameMap->getTile(x, y);

            if(!CommandAuthorization::isValidCityToolType(toolType)) return;

            switch(toolType) {
                case CityTool_Road: {
                    auto placementState = makeCityTilePlacementState(
                        tile->isRock(),
                        tile->isMountain(),
                        tile->hasAGroundObject(),
                        tile->hasCityZone(),
                        tile->isRoad());

                    if (!applyRoadPlacement(placementState)) {
                        return;
                    }

                    // The road overlay does not claim the underlying tile.
                    tile->setRoad(placementState.hasRoad);
                    tile->setDestroyedStructureTile(DestroyedStructure_None);
                    SDL_Log("CityTool: Road placed at (%d, %d)", x, y);
                } break;

                case CityTool_Bulldoze:
                case CityTool_PowerLine:
                default:
                    break;
            }
        } break;

        case CMD_CITY_PLACE_ZONE: {
            int x = static_cast<int>(p0);
            int y = static_cast<int>(p1);

            if(!CommandAuthorization::isValidCityZoneType(p2)) return;
            auto zoneType = static_cast<ZoneType>(p2);

            if(!currentGameMap->tileExists(x, y)) return;

            Tile* tile = currentGameMap->getTile(x, y);

            // The same preconditions Game::handleCityZonePlacementClick applies locally. They
            // have to be re-checked here because this is the path a remote peer reaches, and
            // every peer must reach the same conclusion.
            if(!tile->isRock() && tile->getType() != Terrain_Slab) return;
            if(tile->hasANonInfantryGroundObject()) return;

            tile->setCityZoneType(zoneType);
            // Start at density 0 (empty lot). The first growth scan will
            // bump this to 1 once basic conditions are met.
            tile->setCityZoneDensity(0);
        } break;

        case CMD_CITY_SET_TAX_RATE: {
            // p0 reserved (was houseID in the legacy form); p1 = new rate
            // (0-20). Routed through the command system so multiplayer
            // stays deterministic.
            auto* sim = currentGame ? currentGame->getCitySimulation() : nullptr;
            if (sim && CommandAuthorization::isValidCityTaxRate(p1)) {
                sim->setCityTax(static_cast<int16_t>(p1));
            }
        } break;

        case CMD_CITY_SET_BUDGET: {
            // p0 = police funding %; p1/p2 reserved (roads have no upkeep). Routed through the command
            // system so multiplayer stays deterministic.
            auto* sim = currentGame ? currentGame->getCitySimulation() : nullptr;
            // The command belongs to its issuer, never the observing/local house.
            if (sim && CommandAuthorization::isValidFundingPercent(p0)) {
                sim->setPoliceFundingPercent(issuer->getHouse()->getHouseID(), static_cast<int>(p0));
            }
        } break;

        default:
            break;
    }
}

// spendCityFunds is implemented in CityEffectsRuntime.cpp so it can
// access pLocalHouse directly. The test build provides its own stub
// in CityEffectsRuntime_test_stub.cpp that operates on totalFunds_.

void DuneCity::CitySimulation::checkAndTriggerMilestones(int32_t totalPop, int32_t totalFunds,
                                                         int32_t resPop, int32_t comPop, int32_t indPop,
                                                         bool hasRoads) {
    if (totalPop >= 100) milestones_[MILESTONE_POPULATION_100] = true;
    if (totalPop >= 500) milestones_[MILESTONE_POPULATION_500] = true;
    if (totalPop >= 1000) milestones_[MILESTONE_POPULATION_1000] = true;
    if (totalPop >= 5000) milestones_[MILESTONE_POPULATION_5000] = true;
    if (totalFunds >= 1000) milestones_[MILESTONE_ECONOMY_1000] = true;
    if (totalFunds >= 5000) milestones_[MILESTONE_ECONOMY_5000] = true;
    if (milestoneFirstZoneAchieved_) milestones_[MILESTONE_FIRST_ZONE] = true;
    if (hasRoads && resPop > 0 && comPop > 0 && indPop > 0) milestones_[MILESTONE_ALL_ZONES] = true;
}

} // namespace DuneCity
