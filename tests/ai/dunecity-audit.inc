// Probe-only, read-only DuneCity Random Skirmish audit. Enabled by the matrix harness through
// BALANCE_DUNECITY_AUDIT; no changes to the shipped game or to AI decisions.
#include <dunecity/CitySimulation.h>
#include <dunecity/PowerRules.h>
#include <dunecity/SpatialFields.h>
#include <structures/BuilderBase.h>
#include <structures/ConstructionYard.h>
#include <structures/Refinery.h>
#include <units/HarvesterHelpers.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>
#include "field-expansion-audit.inc"

namespace DuneCityAudit {

// Replacement prices belong to the current owner, not the original owner or
// CHOAM's discounted quote. Infantry products expand only while still queued.
constexpr size_t kInfantrySlot = UnitMixPolicy::kSlots;
constexpr size_t kOtherSlot = kInfantrySlot + 1;
constexpr size_t kCompositionSlots = kOtherSlot + 1;
constexpr std::array<Uint32,6> kTechItems = {Structure_HeavyFactory, Structure_IX,
    Structure_StarPort, Structure_Palace, Structure_RepairYard, Structure_HighTechFactory};
constexpr std::array<const char*,6> kTechNames = {"heavy", "ix", "starport", "palace", "repair", "hitech"};

struct TechObservation {
    const House* house{};
    Uint32 cycle{};
    std::array<int64_t,6> firstOwned{-1,-1,-1,-1,-1,-1};
    std::array<int64_t,6> firstCompleted{-1,-1,-1,-1,-1,-1};
};
inline std::array<TechObservation,NUM_HOUSES> technology;

// Start-only port of RockBudget's sampleRock geometry:
// 4e1a962d7c581c7e03ba16b0a761f427e3d09cfc, tests/ai/dunecity-audit.inc.
// Four-neighbour rock includes occupied land (including the starting yard),
// excludes mountains, and does not let buildings split a formation. Free land
// excludes structures, zones and accepted reservations, never transient units.
// Footprint counts enumerate overlapping top-left origins, NOT disjoint capacity.
struct MapFeatureCounts {
    int area{}, free{}, origins22{}, origins32{}, origins33{};
};

struct MapFeatureDistances {
    std::array<double,2> values{-1,-1};
    int pairCount{};
    int64_t minimumSquared=-1;
};

struct MapFeatureObservation {
    const Game* game{};
    const Map* map{};
    Uint32 cycle{};
    bool emitted{};
};
inline MapFeatureObservation mapFeatureObservation;

inline void sampleMapFeatures(Game& game) {
    const Uint32 cycle=game.getGameCycleCount();
    auto& observation=mapFeatureObservation;
    if (observation.game!=&game || observation.map!=currentGameMap || cycle<observation.cycle)
        observation={&game,currentGameMap,cycle,false};
    observation.cycle=cycle;
    // The sole later-sample cost is this observer guard: no geometry allocation,
    // placement query, RNG use, AI cache update, or persistent world write.
    if (cycle!=0 || observation.emitted || !currentGameMap) return;
    observation.emitted=true;
    const auto& map=*currentGameMap;
    const int w=map.getSizeX(), h=map.getSizeY(), n=w*h;
    const auto houses=EliminationMatch::playingHouses(game);
    std::vector<int> component(n,-1), queue;
    std::vector<char> free(n,0), committed(n,0);
    std::vector<MapFeatureCounts> counts;
    queue.reserve(n);
    int spiceTiles=0, mountains=0;
    double spiceAmount=0;
    for (int at=0;at<n;++at) {
        const auto* tile=map.getTile(at%w,at/w);
        spiceTiles+=int(tile->hasSpice());
        spiceAmount+=tile->getSpice().toDouble();
        mountains+=int(tile->isMountain());
        free[at]=tile->isRock() && !tile->isMountain()
            && !tile->hasAStructure() && !tile->hasCityZone();
    }
    auto reserve=[&](Coord at,Coord size) {
        for (int y=at.y;y<at.y+size.y;++y) for (int x=at.x;x<at.x+size.x;++x)
            if (map.tileExists(x,y) && free[y*w+x]) {
                free[y*w+x]=0; committed[y*w+x]=1;
            }
    };
    for (int owner=0;owner<NUM_HOUSES;++owner) {
        const House* house=game.getHouse(owner);
        if (!house) continue;
        if (const auto* bot=FfaArmy::quantBotOf(house))
            for (const auto& entry:bot->reservedStructures)
                reserve(entry.second.location,getStructureSize(entry.second.item));
    }
    for (const auto* structure:structureList) {
        if (structure->getHealth()<=0 || structure->getItemID()!=Structure_ConstructionYard) continue;
        for (const auto& entry:static_cast<const ConstructionYard*>(structure)->getProductionQueue())
            if (entry.reservationID!=0)
                reserve(Coord(entry.plotX,entry.plotY),getStructureSize(entry.itemID));
    }
    for (int seed=0;seed<n;++seed) {
        const auto* tile=map.getTile(seed%w,seed/w);
        if (component[seed]>=0 || !tile->isRock() || tile->isMountain()) continue;
        const int id=int(counts.size());
        queue.clear();
        auto visit=[&](int at) {
            const auto* candidate=map.getTile(at%w,at/w);
            if (component[at]<0 && candidate->isRock() && !candidate->isMountain()) {
                component[at]=id; queue.push_back(at);
            }
        };
        visit(seed);
        for (size_t i=0;i<queue.size();++i) {
            const int at=queue[i], x=at%w;
            if (x>0) visit(at-1);
            if (x+1<w) visit(at+1);
            if (at>=w) visit(at-w);
            if (at+w<n) visit(at+w);
        }
        MapFeatureCounts measured;
        measured.area=int(queue.size());
        for (int at:queue) measured.free+=int(free[at]);
        counts.push_back(measured);
    }
    auto fits=[&](int at,int width,int height) {
        const int x=at%w,y=at/w;
        if (x+width>w || y+height>h || !free[at]) return false;
        for (int dy=0;dy<height;++dy) for (int dx=0;dx<width;++dx)
            if (!free[(y+dy)*w+x+dx]) return false;
        return true;
    };
    for (int at=0;at<n;++at) if (component[at]>=0) {
        auto& measured=counts[component[at]];
        measured.origins22+=int(fits(at,2,2));
        measured.origins32+=int(fits(at,3,2));
        measured.origins33+=int(fits(at,3,3));
    }
    std::array<const StructureBase*,NUM_HOUSES> yards{};
    std::array<const UnitBase*,NUM_HOUSES> mcvs{};
    std::array<Coord,NUM_HOUSES> bases;
    bases.fill(Coord::Invalid());
    for (const auto* structure:structureList)
        if (structure->getHealth()>0 && structure->getItemID()==Structure_ConstructionYard) {
            const int owner=structure->getOwner()->getHouseID();
            if (!yards[owner] || structure->getObjectID()<yards[owner]->getObjectID()) yards[owner]=structure;
        }
    for (const auto* unit:unitList)
        if (unit->getHealth()>0 && unit->getItemID()==Unit_MCV) {
            const int owner=unit->getOwner()->getHouseID();
            if (!mcvs[owner] || unit->getObjectID()<mcvs[owner]->getObjectID()) mcvs[owner]=unit;
        }
    bool allBases=true;
    for (const House* house:houses) {
        const int owner=house->getHouseID();
        if (yards[owner]) bases[owner]=yards[owner]->getLocation();
        else if (mcvs[owner]) bases[owner]=mcvs[owner]->getLocation();
        allBases=allBases && bases[owner].isValid();
    }
    auto distances=[&](int owner) {
        MapFeatureDistances result;
        if (!allBases || houses.size()<2) return result;
        double total=0,minimum=0;
        for (size_t a=0;a<houses.size();++a) for (size_t b=a+1;b<houses.size();++b) {
            const int first=houses[a]->getHouseID(),second=houses[b]->getHouseID();
            if (owner>=0 && first!=owner && second!=owner) continue;
            const int64_t dx=int64_t(bases[first].x)-bases[second].x;
            const int64_t dy=int64_t(bases[first].y)-bases[second].y;
            const int64_t squared=dx*dx+dy*dy;
            const double distance=std::hypot(double(bases[first].x-bases[second].x),
                double(bases[first].y-bases[second].y));
            if (!result.pairCount || distance<minimum) minimum=distance;
            if (result.minimumSquared<0 || squared<result.minimumSquared) result.minimumSquared=squared;
            total+=distance; ++result.pairCount;
            // Exact integer evidence alongside the unchanged floating display.
            // Pair identities are physical runtime houses, never candidate seats.
            if (owner<0)
                SDL_Log("DUNECITY_MAP_DISTANCE: cycle=0 house=%d other_house=%d"
                        " base_x=%d base_y=%d other_base_x=%d other_base_y=%d"
                        " dx=%lld dy=%lld distance_squared=%lld",
                    first,second,bases[first].x,bases[first].y,bases[second].x,bases[second].y,
                    static_cast<long long>(dx),static_cast<long long>(dy),static_cast<long long>(squared));
        }
        if (result.pairCount) result.values={minimum,total/result.pairCount};
        return result;
    };
    auto emit=[&](int owner,const MapFeatureCounts& measured,int formations,int largest,int largestOrigins,
                  int rangeTiles,int range22,int range32,int range33,int reserved) {
        const auto distance=distances(owner);
        const Coord base=owner<0 ? Coord::Invalid() : bases[owner];
        SDL_Log("DUNECITY_MAP_FEATURE: cycle=0 house=%d width=%d height=%d player_count=%zu"
                " initial_spice_density=%.9f initial_spice_amount=%.9f mountain_density=%.9f"
                " rock_area=%d rock_components=%d largest_rock_area=%d free3x3_count=%d"
                " largest_component_free3x3=%d rock_initial_area=%d rock_initial_free=%d rock_free=%d"
                " rock_in_range=%d rock_committed=%d free2x2_count=%d free3x2_count=%d"
                " in_range2x2_count=%d in_range3x2_count=%d in_range3x3_count=%d"
                " base_x=%d base_y=%d base_distance_min=%.9f base_distance_mean=%.9f"
                " map_tile_count=%d initial_spice_tiles=%d mountain_tiles=%d"
                " base_distance_pair_count=%d base_distance_min_squared=%lld",
            owner,w,h,houses.size(),double(spiceTiles)/n,spiceAmount,double(mountains)/n,
            measured.area,formations,largest,measured.origins33,largestOrigins,measured.area,measured.free,
            measured.free,rangeTiles,reserved,measured.origins22,measured.origins32,range22,range32,range33,
            base.x,base.y,distance.values[0],distance.values[1],n,spiceTiles,mountains,
            distance.pairCount,static_cast<long long>(distance.minimumSquared));
    };
    MapFeatureCounts total;
    int largest=-1,reserved=0;
    for (size_t id=0;id<counts.size();++id) {
        const auto& measured=counts[id];
        total.area+=measured.area; total.free+=measured.free;
        total.origins22+=measured.origins22; total.origins32+=measured.origins32; total.origins33+=measured.origins33;
        if (largest<0 || measured.area>counts[largest].area) largest=int(id);
    }
    for (char value:committed) reserved+=int(value);
    emit(-1,total,int(counts.size()),largest<0 ? 0 : counts[largest].area,
        largest<0 ? 0 : counts[largest].origins33,-1,-1,-1,-1,reserved);
    std::vector<char> selected(counts.size(),0);
    for (const House* house:houses) {
        const int owner=house->getHouseID();
        const auto* yard=yards[owner];
        if (!yard) {
            emit(owner,{-1,-1,-1,-1,-1},-1,-1,-1,-1,-1,-1,-1,-1);
            continue;
        }
        std::fill(selected.begin(),selected.end(),0);
        const Coord at=yard->getLocation(),size=yard->getStructureSize();
        for (int y=at.y;y<at.y+size.y;++y) for (int x=at.x;x<at.x+size.x;++x)
            if (map.tileExists(x,y) && component[y*w+x]>=0) selected[component[y*w+x]]=1;
        MapFeatureCounts local;
        int formations=0;
        largest=-1;
        for (size_t id=0;id<counts.size();++id) if (selected[id]) {
            const auto& measured=counts[id];
            local.area+=measured.area; local.free+=measured.free;
            local.origins22+=measured.origins22; local.origins32+=measured.origins32; local.origins33+=measured.origins33;
            ++formations;
            if (largest<0 || measured.area>counts[largest].area) largest=int(id);
        }
        DuneCity::BoxAnyField anchors;
        anchors.build(w,h,[&](int x,int y) { return map.getTile(x,y)->getOwner()==owner; });
        int rangeTiles=0,range22=0,range32=0,range33=0;
        reserved=0;
        for (int tile=0;tile<n;++tile) if (component[tile]>=0 && selected[component[tile]]) {
            const int x=tile%w,y=tile/w;
            reserved+=int(committed[tile]);
            rangeTiles+=int(free[tile] && anchors.anyWithin(x,y,BUILDRANGE));
            range22+=int(fits(tile,2,2) && anchors.anyNearFootprint(x,y,2,2,BUILDRANGE));
            range32+=int(fits(tile,3,2) && anchors.anyNearFootprint(x,y,3,2,BUILDRANGE));
            range33+=int(fits(tile,3,3) && anchors.anyNearFootprint(x,y,3,3,BUILDRANGE));
        }
        emit(owner,local,formations,largest<0 ? 0 : counts[largest].area,
            largest<0 ? 0 : counts[largest].origins33,rangeTiles,range22,range32,range33,reserved);
    }
}

inline size_t compositionSlot(Uint32 item) {
    for (size_t i=0; i<QuantBot::kMixItems.size(); ++i)
        if (QuantBot::kMixItems[i]==item) return i;
    switch (item) {
    case Unit_Soldier: case Unit_Infantry: case Unit_Infantry5:
    case Unit_Trooper: case Unit_Troopers: case Unit_Troopers5:
    case Unit_GasInfantry: case Unit_FlameTrooper: return kInfantrySlot;
    default: return kOtherSlot;
    }
}

inline Uint32 singleItem(Uint32 item) {
    if (item==Unit_Infantry || item==Unit_Infantry5) return Unit_Soldier;
    if (item==Unit_Troopers || item==Unit_Troopers5) return Unit_Trooper;
    return item;
}

inline int productCount(Uint32 item) {
    if (item==Unit_Infantry || item==Unit_Troopers) return 3;
    if (item==Unit_Infantry5 || item==Unit_Troopers5) return 5;
    return 1;
}

inline void sampleComposition(Game& game, const House* house, const QuantBot* bot, int minute) {
    std::array<int64_t,kCompositionSlots> liveCount{}, liveValue{}, queuedCount{}, queuedValue{};
    const int houseID=house->getHouseID();
    for (const auto* unit:unitList) {
        if (unit->getOwner()!=house || unit->getHealth()<=0
            || !QuantBotInternal::mobileCombatItem(unit->getItemID())) continue;
        const size_t slot=compositionSlot(unit->getItemID());
        ++liveCount[slot];
        liveValue[slot]+=game.objectData.data[unit->getItemID()][houseID].price;
    }
    for (const auto* structure:structureList) {
        if (structure->getOwner()!=house || structure->getHealth()<=0) continue;
        if (const auto* builder=dynamic_cast<const BuilderBase*>(structure)) {
            for (const auto& order:builder->getProductionQueue()) {
                if (!QuantBotInternal::mobileCombatItem(order.itemID)) continue;
                const size_t slot=compositionSlot(order.itemID);
                const int count=productCount(order.itemID);
                queuedCount[slot]+=count;
                queuedValue[slot]+=int64_t(count)*game.objectData.data[singleItem(order.itemID)][houseID].price;
            }
            SDL_Log("DUNECITY_BUILDER: minute=%d cycle=%u house=%d builder=%u item=%u level=%d upgrading=%d",
                minute,game.getGameCycleCount(),houseID,builder->getObjectID(),builder->getItemID(),
                builder->getCurrentUpgradeLevel(),int(builder->isUpgrading()));
        }
    }
    const auto& allocation=bot->getAllocationSnapshot();
    int64_t totalLiveCount=0,totalLiveValue=0,totalQueuedCount=0,totalQueuedValue=0;
    for (size_t slot=0; slot<kCompositionSlots; ++slot) {
        totalLiveCount+=liveCount[slot]; totalLiveValue+=liveValue[slot];
        totalQueuedCount+=queuedCount[slot]; totalQueuedValue+=queuedValue[slot];
        const int target=allocation.ready && slot<UnitMixPolicy::kSlots && allocation.slots[slot].allocated
            ? allocation.slots[slot].targetBps : -1;
        SDL_Log("DUNECITY_COMPOSITION: minute=%d cycle=%u house=%d slot=%zu item=%u"
                " live_count=%lld live_value=%lld queued_count=%lld queued_value=%lld"
                " target_bps=%d allocation_ready=%d allocation_cycle=%u",
            minute,game.getGameCycleCount(),houseID,slot,
            slot<UnitMixPolicy::kSlots ? QuantBot::kMixItems[slot] : Uint32(NONE_ID),
            static_cast<long long>(liveCount[slot]),static_cast<long long>(liveValue[slot]),
            static_cast<long long>(queuedCount[slot]),static_cast<long long>(queuedValue[slot]),
            target,int(allocation.ready),allocation.cycle);
    }
    SDL_Log("DUNECITY_TECH_SAMPLE: minute=%d cycle=%u house=%d credits=%d"
            " military_live_count=%lld military_live_value=%lld military_queued_count=%lld military_queued_value=%lld"
            " allocation_ready=%d allocation_cycle=%u infantry_quota_bps=%d"
            " tech_level=%d research_target=%d research_remaining=%d research_enabled=%d city_sim=%d",
        minute,game.getGameCycleCount(),houseID,house->getCredits(),
        static_cast<long long>(totalLiveCount),static_cast<long long>(totalLiveValue),
        static_cast<long long>(totalQueuedCount),static_cast<long long>(totalQueuedValue),
        int(allocation.ready),allocation.cycle,allocation.infantryQuotaBps,house->getTechLevel(),
        house->getTechResearchTarget(),house->getTechResearchRemaining(),
        int(game.getGameInitSettings().getGameOptions().researchLab),int(game.isCitySimEnabled()));
    const auto& state=technology[houseID];
    for (size_t i=0; i<kTechItems.size(); ++i)
        SDL_Log("DUNECITY_TECH_STRUCTURE: minute=%d cycle=%u house=%d name=%s item=%u live=%d built=%d"
                " first_owned_cycle=%lld first_completed_cycle=%lld",
            minute,game.getGameCycleCount(),houseID,kTechNames[i],kTechItems[i],
            house->getNumItems(kTechItems[i]),house->getNumBuiltItems(kTechItems[i]),
            static_cast<long long>(state.firstOwned[i]),static_cast<long long>(state.firstCompleted[i]));
}

struct StorageObservation {
    const House* house{};
    Uint32 cycle{};
    Uint32 fullCycles{}, overflowCycles{};
    Uint32 powerDeficitCycles{}, requiredPowerDeficitCycles{};
    int capacity{};
    int64_t lostRaw{}, incomeLostRaw{};
    bool full{};
    bool powerDeficit{}, requiredPowerDeficit{};
};
inline std::array<StorageObservation,NUM_HOUSES> storage;


// Fleet study state belongs only to this diagnostic observer. In particular,
// never resolve an ObjectPointer (even a const getter can populate its cache),
// call a path planner, construct a unit, or consult the AI's fogged spice cache.
struct FleetHouseObservation {
    int harvesters{}, carryalls{}, refineries{}, repairYards{}, bays{}, occupied{}, unloading{};
    int64_t capital{}, capitalCycles{}, harvesterCycles{}, bayCycles{}, occupiedCycles{}, unloadingCycles{};
    int64_t unloadStarts{}, unloadCensored{}, trips{}, tripCycles{}, tripMin{-1}, tripMax{-1};
    int64_t tripLeftCensored{}, leftCensoredCycles{}, tripCensored{}, censoredTripCycles{};
    int64_t unstartedCensored{}, unstartedCensoredCycles{};
};
struct FleetHarvesterObservation {
    int owner{-1};
    Uint32 cycle{}, firstCycle{};
    int64_t unloadCycle{-1};
    int64_t cargoRaw{}, previousCargoRaw{};
    bool inBay{}, nowInBay{}, previous{};
};
struct FleetFieldObservation {
    int64_t cycle{-1};
    int distance{-1}, field{-1}, fieldTiles{}, component{-1}, componentTiles{}, accessibleTiles{};
    double fieldAmount{}, componentAmount{}, accessibleAmount{};
};
struct FleetRefineryObservation {
    int owner{-1}, bays{}, occupied{}, unloading{};
    Uint32 cycle{};
    int64_t bayCycles{}, occupiedCycles{}, unloadingCycles{};
    FleetFieldObservation field;
};
struct FleetSpiceComponent {
    int root{}, tiles{};
    double amount{};
};
struct FleetGeometry {
    int64_t cycle{-1}, samples{};
    // Reuse work buffers; geometry is O(map area), at most once per game minute.
    std::vector<char> passable;
    std::vector<double> spice;
    std::vector<int> field, component, distance, nearestField, queue, accessible;
    std::vector<FleetSpiceComponent> fields, components;
};
struct FleetObservation {
    const Game* game{};
    const Map* map{};
    Uint32 cycle{};
    bool initialized{};
    int64_t steps{}, observedCycles{}, gaps{}, gapCycles{}, maxStep{};
    std::array<FleetHouseObservation,NUM_HOUSES> houses;
    std::map<Uint32,FleetHarvesterObservation> harvesters;
    std::map<Uint32,FleetRefineryObservation> refineries;
    FleetGeometry geometry;
};
inline FleetObservation fleet;

inline void censorFleetTrip(FleetHarvesterObservation& unit, Uint32 cycle) {
    if (unit.owner<0) return;
    auto& house=fleet.houses[unit.owner];
    if (unit.unloadCycle>=0) {
        ++house.tripCensored;
        house.censoredTripCycles+=cycle-unit.unloadCycle;
    } else {
        ++house.unstartedCensored;
        house.unstartedCensoredCycles+=cycle-unit.firstCycle;
    }
    unit.unloadCycle=-1; unit.owner=-1;
}

inline void observeFleet(Game& game) {
    const Uint32 cycle=game.getGameCycleCount();
    if (!fleet.initialized || fleet.game!=&game || fleet.map!=currentGameMap || cycle<fleet.cycle) {
        fleet=FleetObservation{};
        fleet.game=&game; fleet.map=currentGameMap;
    } else if (cycle==fleet.cycle) return; // sample() can revisit the same tick.
    const bool contiguous=fleet.initialized && cycle==fleet.cycle+1;
    if (fleet.initialized) {
        const Uint32 elapsed=cycle-fleet.cycle;
        fleet.maxStep=std::max(fleet.maxStep,int64_t(elapsed));
        if (contiguous) {
            ++fleet.observedCycles;
            // Left-endpoint integration of every observed tick, including bays
            // waiting for a carryall after their cargo has become empty.
            for (auto& house:fleet.houses) {
                house.capitalCycles+=house.capital;
                house.harvesterCycles+=house.harvesters;
                house.bayCycles+=house.bays;
                house.occupiedCycles+=house.occupied;
                house.unloadingCycles+=house.unloading;
            }
        } else {
            ++fleet.gaps; fleet.gapCycles+=elapsed;
            for (auto& entry:fleet.harvesters) {
                censorFleetTrip(entry.second,fleet.cycle);
                entry.second.inBay=false;
            }
        }
    }
    for (auto& house:fleet.houses) {
        house.harvesters=house.carryalls=house.refineries=house.repairYards=0;
        house.bays=house.occupied=house.unloading=0;
        house.capital=0;
    }
    for (const auto* unit:unitList) {
        if (unit->getHealth()<=0) continue;
        const int owner=unit->getOwner()->getHouseID();
        const auto item=unit->getItemID();
        auto& house=fleet.houses[owner];
        if (isCarryallUnit(item)) {
            ++house.carryalls;
            house.capital+=game.objectData.data[item][owner].price;
        }
        if (!isHarvesterLikeUnit(item)) continue;
        ++house.harvesters;
        house.capital+=game.objectData.data[item][owner].price;
        const auto inserted=fleet.harvesters.try_emplace(unit->getObjectID());
        auto& seen=inserted.first->second;
        seen.previous=!inserted.second && contiguous && seen.cycle==fleet.cycle && seen.owner==owner;
        if (seen.owner!=owner) {
            censorFleetTrip(seen,contiguous ? cycle : fleet.cycle);
            seen.firstCycle=cycle;
            seen.inBay=false;
        }
        seen.owner=owner; seen.cycle=cycle; seen.nowInBay=false;
        seen.previousCargoRaw=seen.cargoRaw;
        seen.cargoRaw=harvesterGetAmountOfSpice(unit).getRawValue();
    }
    for (const auto* structure:structureList) {
        if (structure->getHealth()<=0) continue;
        const int owner=structure->getOwner()->getHouseID();
        auto& house=fleet.houses[owner];
        if (structure->getItemID()==Structure_RepairYard) {
            ++house.repairYards;
            house.capital+=game.objectData.data[Structure_RepairYard][owner].price;
        }
        const auto* refinery=dynamic_cast<const Refinery*>(structure);
        if (!refinery) continue;
        ++house.refineries;
        house.capital+=game.objectData.data[structure->getItemID()][owner].price;
        auto& seen=fleet.refineries[structure->getObjectID()];
        if (seen.owner!=owner) seen=FleetRefineryObservation{};
        if (contiguous && seen.owner==owner && seen.cycle==fleet.cycle) {
            seen.bayCycles+=seen.bays;
            seen.occupiedCycles+=seen.occupied;
            seen.unloadingCycles+=seen.unloading;
        }
        seen.owner=owner; seen.cycle=cycle; seen.bays=refinery->getNumBays();
        seen.occupied=seen.unloading=0;
        house.bays+=seen.bays;
        for (int bay=0;bay<seen.bays;++bay) {
            if (!refinery->extractingSpice[bay]) continue;
            ++seen.occupied; ++house.occupied;
            // getContainedHarvester() resolves a lazy ObjectPointer; use its
            // stored ID and the object manager's read-only lookup instead.
            const auto* object=game.getObjectManager().getObject(refinery->harvesters[bay].getObjectID());
            if (!object || object->getHealth()<=0 || !isHarvesterLikeObject(object)) continue;
            auto found=fleet.harvesters.find(object->getObjectID());
            if (found==fleet.harvesters.end()) continue;
            auto& unit=found->second;
            unit.nowInBay=true;
            if (unit.cargoRaw>0) { ++seen.unloading; ++house.unloading; }
            // A small delivery can be fully extracted on its entry tick.
            if (unit.cargoRaw<=0 && !(unit.previous && unit.previousCargoRaw>0)) continue;
            if (unit.inBay) continue;
            auto& ownerStats=fleet.houses[unit.owner];
            if (!unit.previous) {
                ++ownerStats.unloadCensored; // Already unloading at first sight or after a gap.
                continue;
            }
            ++ownerStats.unloadStarts;
            if (unit.unloadCycle>=0) {
                const int64_t duration=cycle-unit.unloadCycle;
                ++ownerStats.trips; ownerStats.tripCycles+=duration;
                if (ownerStats.tripMin<0 || duration<ownerStats.tripMin) ownerStats.tripMin=duration;
                ownerStats.tripMax=std::max(ownerStats.tripMax,duration);
            } else {
                ++ownerStats.tripLeftCensored; // No preceding observed unload start.
                ownerStats.leftCensoredCycles+=cycle-unit.firstCycle;
            }
            unit.unloadCycle=cycle;
        }
    }
    for (auto it=fleet.harvesters.begin();it!=fleet.harvesters.end();) {
        if (it->second.cycle!=cycle) {
            censorFleetTrip(it->second,contiguous ? cycle : fleet.cycle);
            it=fleet.harvesters.erase(it);
        } else {
            it->second.inBay=it->second.nowInBay;
            ++it;
        }
    }
    for (auto it=fleet.refineries.begin();it!=fleet.refineries.end();)
        if (it->second.cycle!=cycle) it=fleet.refineries.erase(it);
        else ++it;
    fleet.initialized=true; fleet.cycle=cycle; ++fleet.steps;
}

inline void sampleFleetGeometry(Game& game) {
    if (!currentGameMap) return;
    auto& g=fleet.geometry;
    const Uint32 cycle=game.getGameCycleCount(), minute=MILLI2CYCLES(60000);
    if (g.cycle>=0 && cycle-Uint32(g.cycle)<minute) return;
    const auto& map=*currentGameMap;
    const int w=map.getSizeX(),h=map.getSizeY(),n=w*h;
    g.cycle=cycle; ++g.samples;
    g.passable.resize(n); g.spice.resize(n);
    g.field.assign(n,-1); g.component.assign(n,-1);
    g.distance.assign(n,-1); g.nearestField.assign(n,-1);
    g.fields.clear(); g.components.clear(); g.queue.reserve(n);
    for (int at=0;at<n;++at) {
        const auto* tile=map.getTile(at%w,at/w);
        g.passable[at]=!tile->isMountain() && !tile->hasAStructure();
        g.spice[at]=tile->getSpice().toDouble();
    }
    auto neighbours=[&](int at,auto visit) {
        const int x=at%w,y=at/w;
        for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx)
            if ((dx || dy) && x+dx>=0 && x+dx<w && y+dy>=0 && y+dy<h)
                visit((y+dy)*w+x+dx);
    };
    // Eight-neighbour terrain reachability matches the engine's diagonal
    // connectivity. Ignore fog and temporary units; block mountains and all
    // structures. This is NOT A* travel time: no terrain/turn/speed/queue or airlift model.
    auto components=[&](bool spiceOnly,std::vector<int>& labels,std::vector<FleetSpiceComponent>& totals) {
        for (int seed=0;seed<n;++seed) {
            if (labels[seed]>=0 || !g.passable[seed] || (spiceOnly && g.spice[seed]<=0)) continue;
            const int id=int(totals.size());
            FleetSpiceComponent total{seed,0,0};
            g.queue.clear(); g.queue.push_back(seed); labels[seed]=id;
            for (size_t i=0;i<g.queue.size();++i) {
                const int at=g.queue[i];
                if (g.spice[at]>0) { ++total.tiles; total.amount+=g.spice[at]; }
                neighbours(at,[&](int next) {
                    if (labels[next]<0 && g.passable[next] && (!spiceOnly || g.spice[next]>0)) {
                        labels[next]=id; g.queue.push_back(next);
                    }
                });
            }
            totals.push_back(total);
        }
    };
    components(true,g.field,g.fields);
    components(false,g.component,g.components);
    // One multi-source BFS, not a search per refinery. Row-major spice seeds
    // and fixed neighbour order make equal-distance field selection deterministic.
    g.queue.clear();
    for (int at=0;at<n;++at) if (g.field[at]>=0) {
        g.distance[at]=0; g.nearestField[at]=g.field[at]; g.queue.push_back(at);
    }
    for (size_t i=0;i<g.queue.size();++i) {
        const int at=g.queue[i];
        neighbours(at,[&](int next) {
            if (g.passable[next] && g.distance[next]<0) {
                g.distance[next]=g.distance[at]+1;
                g.nearestField[next]=g.nearestField[at];
                g.queue.push_back(next);
            }
        });
    }
    for (const auto* structure:structureList) {
        const auto found=fleet.refineries.find(structure->getObjectID());
        if (found==fleet.refineries.end()) continue;
        auto& field=found->second.field;
        field=FleetFieldObservation{}; field.cycle=cycle;
        g.accessible.clear();
        const Coord origin=structure->getLocation(),size=structure->getStructureSize();
        int nearest=-1;
        for (int y=origin.y-1;y<=origin.y+size.y;++y) for (int x=origin.x-1;x<=origin.x+size.x;++x) {
            if (!map.tileExists(x,y) || (x>=origin.x && x<origin.x+size.x && y>=origin.y && y<origin.y+size.y)) continue;
            const int at=y*w+x,component=g.component[at];
            if (component>=0 && std::find(g.accessible.begin(),g.accessible.end(),component)==g.accessible.end())
                g.accessible.push_back(component);
            if (g.distance[at]>=0 && (nearest<0 || g.distance[at]<g.distance[nearest])) nearest=at;
        }
        for (int component:g.accessible) {
            field.accessibleTiles+=g.components[component].tiles;
            field.accessibleAmount+=g.components[component].amount;
        }
        if (nearest<0) continue;
        const auto& patch=g.fields[g.nearestField[nearest]];
        const auto& component=g.components[g.component[nearest]];
        // The perimeter is one move from the refinery footprint.
        field.distance=g.distance[nearest]+1;
        field.field=patch.root; field.fieldTiles=patch.tiles; field.fieldAmount=patch.amount;
        field.component=component.root; field.componentTiles=component.tiles; field.componentAmount=component.amount;
    }
}

inline void sampleFleet(Game& game,const House* house,int minute) {
    const int owner=house->getHouseID();
    const auto& state=fleet.houses[owner];
    int harvestersBuilt=0,harvestersLost=0,carryallsBuilt=0,refineriesBuilt=0,repairYardsBuilt=0;
    int64_t builtValue=0,open=0,openCycles=0,unstarted=0,unstartedCycles=0;
    for (int item=ItemID_Invalid+1;item<Num_ItemID;++item) {
        const bool harvester=isHarvesterLikeUnit(item),carryall=isCarryallUnit(item);
        const bool refinery=item==Structure_Refinery || item==Structure_Doublefinery;
        if (!harvester && !carryall && !refinery && item!=Structure_RepairYard) continue;
        const int built=house->getNumBuiltItems(item);
        builtValue+=int64_t(built)*game.objectData.data[item][owner].price;
        if (harvester) { harvestersBuilt+=built; harvestersLost+=house->getNumLostItems(item); }
        if (carryall) carryallsBuilt+=built;
        if (refinery) refineriesBuilt+=built;
        if (item==Structure_RepairYard) repairYardsBuilt+=built;
    }
    for (const auto& entry:fleet.harvesters) {
        const auto& unit=entry.second;
        if (unit.owner!=owner) continue;
        if (unit.unloadCycle>=0) {
            ++open; openCycles+=game.getGameCycleCount()-unit.unloadCycle;
        } else {
            ++unstarted; unstartedCycles+=game.getGameCycleCount()-unit.firstCycle;
        }
    }
    // Replacement prices, not actual expenditure: built ledger includes free
    // deliveries where the engine counts them and is separate from live assets.
    // Trips are successive positive-cargo UNLOAD STARTS for the same object and
    // owner, including unloading, harvest, waiting and transport between starts.
    // Open/censored durations are exposure, never completed trips or zero trips.
    AITelemetry::Record row;
    row.set("harvesters",state.harvesters).set("refineries",state.refineries)
        .set("refined_spice",house->getHarvestedSpice().lround())
        .set("refined_spice_raw",house->getHarvestedSpice().getRawValue())
        .set("carryalls",state.carryalls).set("repair_yards",state.repairYards)
        .set("harvesters_built",harvestersBuilt).set("harvesters_lost",harvestersLost)
        .set("carryalls_built",carryallsBuilt).set("refineries_built",refineriesBuilt).set("repair_yards_built",repairYardsBuilt)
        .set("fleet_capital_current_value",state.capital).set("fleet_capital_built_value",builtValue)
        .set("fleet_capital_value_cycles",state.capitalCycles)
        .set("fleet_steps",fleet.steps).set("fleet_observed_cycles",fleet.observedCycles)
        .set("fleet_gap_count",fleet.gaps).set("fleet_gap_cycles",fleet.gapCycles).set("fleet_max_step_cycles",fleet.maxStep)
        .set("fleet_harvester_cycles",state.harvesterCycles).set("fleet_bay_available_cycles",state.bayCycles)
        .set("fleet_bay_occupied_cycles",state.occupiedCycles).set("fleet_bay_unloading_cycles",state.unloadingCycles)
        .set("refinery_bays",state.bays).set("occupied_refinery_bays",state.occupied).set("unloading_refinery_bays",state.unloading)
        .set("harvester_unload_starts",state.unloadStarts).set("harvester_unload_censored",state.unloadCensored)
        .set("harvester_trip_count",state.trips).set("harvester_trip_cycles_sum",state.tripCycles)
        .set("harvester_trip_cycles_min",state.tripMin).set("harvester_trip_cycles_max",state.tripMax)
        .set("harvester_trip_left_censored",state.tripLeftCensored).set("harvester_trip_censored",state.tripCensored)
        .set("harvester_trip_left_censored_cycles",state.leftCensoredCycles)
        .set("harvester_trip_censored_cycles",state.censoredTripCycles)
        .set("harvester_trip_open",open).set("harvester_trip_open_cycles",openCycles)
        .set("harvester_trip_unstarted",unstarted).set("harvester_trip_unstarted_cycles",unstartedCycles)
        .set("harvester_trip_unstarted_censored",state.unstartedCensored)
        .set("harvester_trip_unstarted_censored_cycles",state.unstartedCensoredCycles)
        .set("fleet_geometry_samples",fleet.geometry.samples).set("fleet_geometry_cycle",fleet.geometry.cycle);
    SDL_Log("DUNECITY_FLEET: minute=%d cycle=%u house=%d fleet=%s",
        minute,game.getGameCycleCount(),owner,row.json().c_str());
    for (const auto* structure:structureList) {
        if (structure->getOwner()!=house) continue;
        const auto found=fleet.refineries.find(structure->getObjectID());
        if (found==fleet.refineries.end()) continue;
        const auto& refinery=found->second;
        const auto& field=refinery.field;
        SDL_Log("DUNECITY_REFINERY: minute=%d cycle=%u house=%d refinery=%u x=%d y=%d geometry_cycle=%lld"
                " nearest_spice_distance_steps=%d nearest_spice_field_id=%d nearest_spice_field_tiles=%d"
                " nearest_spice_field_amount=%.9f spice_component_id=%d spice_component_tiles=%d spice_component_amount=%.9f"
                " accessible_spice_tiles=%d accessible_spice_amount=%.9f refinery_bays=%d occupied_refinery_bays=%d"
                " unloading_refinery_bays=%d fleet_bay_available_cycles=%lld fleet_bay_occupied_cycles=%lld fleet_bay_unloading_cycles=%lld",
            minute,game.getGameCycleCount(),owner,structure->getObjectID(),structure->getLocation().x,structure->getLocation().y,
            static_cast<long long>(field.cycle),field.distance,field.field,field.fieldTiles,field.fieldAmount,
            field.component,field.componentTiles,field.componentAmount,field.accessibleTiles,field.accessibleAmount,
            refinery.bays,refinery.occupied,refinery.unloading,static_cast<long long>(refinery.bayCycles),
            static_cast<long long>(refinery.occupiedCycles),static_cast<long long>(refinery.unloadingCycles));
    }
}
// Every simulation tick, not a minute-sample estimate. The private ledger is
// read only in this existing -fno-access-control probe. A capacity-loss tick is
// excluded from income overflow: destroying a vault is not failure to buy one.
inline void observe(Game& game) {
    static const bool enabled = std::getenv("BALANCE_DUNECITY_AUDIT")!=nullptr;
    if (!enabled) return;
    const Uint32 cycle = game.getGameCycleCount();
    observeFleet(game);
    FieldExpansionAudit::observe(game);
    for (int h=0; h<NUM_HOUSES; ++h) {
        const House* house = game.getHouse(h);
        if (!house || !FfaArmy::quantBotOf(house)) continue;
        auto& tech=technology[h];
        if (tech.house!=house || cycle<tech.cycle) tech={};
        tech.house=house; tech.cycle=cycle;
        for (size_t i=0; i<kTechItems.size(); ++i) {
            if (tech.firstOwned[i]<0 && house->getNumItems(kTechItems[i])>0)
                tech.firstOwned[i]=cycle;
            if (tech.firstCompleted[i]<0 && house->getNumBuiltItems(kTechItems[i])>0)
                tech.firstCompleted[i]=cycle;
        }
        auto& state = storage[h];
        int64_t lost = 0;
        const auto totals = AITelemetry::log().economy.find(h);
        if (totals!=AITelemetry::log().economy.end()) {
            const auto entry = totals->second.find("storage_lost");
            if (entry!=totals->second.end()) lost=entry->second;
        }
        if (state.house!=house || cycle<state.cycle) state={};
        if (state.house==house && cycle>state.cycle) {
            const Uint32 elapsed = cycle-state.cycle;
            if (state.full) state.fullCycles+=elapsed;
            if (state.powerDeficit) state.powerDeficitCycles+=elapsed;
            if (state.requiredPowerDeficit) state.requiredPowerDeficitCycles+=elapsed;
            if (lost>state.lostRaw && house->getCapacity()>=state.capacity) {
                state.overflowCycles+=elapsed;
                state.incomeLostRaw+=lost-state.lostRaw;
            }
        }
        state.house=house;state.cycle=cycle;state.capacity=house->getCapacity();state.lostRaw=lost;
        state.full=house->isAlive() && state.capacity>0 && house->getEarnedCredits()>=FixPoint(state.capacity);
        state.powerDeficit=house->isAlive() && house->getProducedPower()<house->getPowerRequirement();
        state.requiredPowerDeficit=state.powerDeficit && house->isPowerRequired();
    }
}

inline void sample(Game& game) {
    if (!std::getenv("BALANCE_DUNECITY_AUDIT")) return;
    observe(game);
    sampleMapFeatures(game);
    sampleFleetGeometry(game);
    sampleMapFeatures(game);
    const int minute = static_cast<int>(game.getGameCycleCount() / MILLI2CYCLES(60000));
    FieldExpansionAudit::sample(game,minute);
    for (const House* house : EliminationMatch::playingHouses(game)) {
        const QuantBot* bot = FfaArmy::quantBotOf(house);
        if (!bot) continue;
        sampleComposition(game,house,bot,minute);
        sampleFleet(game,house,minute);
        const auto grid = FfaArmy::baseGrid(house);
        int fighters = 0, moving = 0, targeting = 0, enemies = 0, enemyValue = 0, coveredEnemies = 0;
        int trooperLive = 0, ornithopterLive = 0, eliteOrnithopterLive = 0;
        // Acceptance measurements are read at this exact minute checkpoint,
        // not reconstructed from staggered AI build/decision traces.
        int harvestersTotal = 0, harvestersActive = 0, harvestersHarvesting = 0, harvestersReturning = 0;
        for (const auto* unit : unitList) {
            if (unit->getOwner() == house && unit->getHealth() > 0) {
                trooperLive += int(unit->getItemID() == Unit_Trooper);
                ornithopterLive += int(unit->getItemID() == Unit_Ornithopter);
                eliteOrnithopterLive += int(unit->getItemID() == Unit_EliteOrnithopter);
                if (isHarvesterLikeUnit(unit->getItemID())) {
                    ++harvestersTotal;
                    harvestersActive += int(unit->isActive());
                    harvestersHarvesting += int(unit->isActive() && harvesterIsHarvesting(unit));
                    harvestersReturning += int(unit->isActive() && harvesterIsReturning(unit));
                }
            }
            if (!FfaArmy::groundFighter(unit)) continue;
            if (unit->getOwner() == house) {
                ++fighters;
                moving += int(unit->isMoving());
                const auto* target = game.getObjectManager().getObject(unit->target.getObjectID());
                if (target && target->getHealth() > 0 && target->getOwner()
                    && target->getOwner()->getTeamID() != house->getTeamID()) ++targeting;
            } else if (unit->getOwner()->getTeamID() != house->getTeamID()
                       && grid.distance(unit->getLocation()) <= 10) {
                ++enemies;
                enemyValue += FfaArmy::price(unit);
                for (const auto* structure : structureList) {
                    if (structure->getOwner() != house || structure->getHealth() <= 0) continue;
                    const auto item = structure->getItemID();
                    if (item != Structure_GunTurret && item != Structure_RocketTurret) continue;
                    if (item == Structure_RocketTurret && !DuneCity::rocketTurretPowered(
                            game.getGameInitSettings().getGameOptions().rocketTurretsNeedPower,
                            house->getProducedPower(), house->getPowerRequirement())) continue;
                    if (blockDistance(structure->getLocation(), unit->getLocation()) <= structure->getWeaponRange()) {
                        ++coveredEnemies;
                        break;
                    }
                }
            }
        }
        int queue = 0, upgrading = 0, waiting = 0, structureHP = 0, structuresLost = 0;
        int yardLevel = -1;
        int wor = 0, trooperSingleQueued = 0, trooperSquadsQueued = 0, trooperSquads5Queued = 0;
        int refineryBays = 0, refineryBaysBusy = 0;
        std::string fronts;
        for (const auto* structure : structureList) {
            if (structure->getOwner() != house || structure->getHealth() <= 0) continue;
            wor += int(structure->getItemID() == Structure_WOR);
            structureHP += structure->getHealth().lround();
            if (structure->acceptsHarvesterDropoff()) {
                if (const auto* refinery = dynamic_cast<const Refinery*>(structure)) {
                    refineryBays += refinery->getNumBays();
                    refineryBaysBusy += refinery->getNumOccupiedBays();
                } else {
                    ++refineryBays;
                    refineryBaysBusy += int(!structure->isHarvesterDropoffFree());
                }
            }
            if (const auto* builder = dynamic_cast<const BuilderBase*>(structure)) {
                queue += builder->getProductionQueueSize();
                upgrading += int(builder->isUpgrading());
                waiting += int(builder->isWaitingToPlace());
                for (const auto& order : builder->getProductionQueue()) {
                    trooperSingleQueued += int(order.itemID == Unit_Trooper);
                    trooperSquadsQueued += int(order.itemID == Unit_Troopers);
                    trooperSquads5Queued += int(order.itemID == Unit_Troopers5);
                }
                if (structure->getItemID() == Structure_ConstructionYard)
                    yardLevel = std::max(yardLevel, builder->getCurrentUpgradeLevel());
                if (builder->getProductionQueueSize() || builder->isUpgrading())
                    fronts += (fronts.empty() ? "" : "/") + std::to_string(structure->getItemID()) + ":"
                        + std::to_string(builder->getCurrentProducedItem()) + ":"
                        + std::to_string(builder->getProductionQueueSize()) + ":"
                        + std::to_string(int(builder->isOnHold()));
            }
        }
        for (int item = Structure_FirstID; item <= Structure_LastID; ++item)
            if (isStructure(item)) structuresLost += house->getNumLostItems(item);
        int population = 0, tax = 0, demand = 0, policeFunding = 100;
        int taxPerMinute = 0, taxRate = 0, harvestersLost = 0;
        for (int item : {Unit_Harvester, Unit_RebelHarvester, Unit_FastHarvester, Unit_CapacityHarvester})
            if (isHarvesterLikeUnit(item)) harvestersLost += house->getNumLostItems(item);
        if (const auto* sim = game.getCitySimulation(); sim && sim->isInitialized()) {
            const auto& city = sim->getHouseState(house->getHouseID());
            population = city.getTotalPop() * DuneCity::CitySimulation::kPopDisplayMultiplier;
            tax = city.budget.getLastTaxRevenue();
            taxRate = sim->getCityTax(house->getHouseID());
            taxPerMinute = DuneCity::annualPerMinute(DuneCity::computeAnnualTaxRevenue(
                city.taxBaseEighths, taxRate, city.avgLandValue));
            demand = std::max({int(city.resValve), int(city.comValve), int(city.indValve)});
            policeFunding = city.policeFundingPercent;
        }
        SDL_Log("DUNECITY_LOGISTICS: minute=%d cycle=%u house=%d"
                " harvesters_total=%d harvesters_active=%d harvesters_harvesting=%d harvesters_returning=%d"
                " refinery_bays=%d refinery_bays_busy=%d harvesters_lost=%d tax_per_min=%d tax_rate=%d",
                minute, game.getGameCycleCount(), house->getHouseID(), harvestersTotal, harvestersActive,
                harvestersHarvesting, harvestersReturning, refineryBays, refineryBaysBusy, harvestersLost,
                taxPerMinute, taxRate);
        const auto& observation = bot->vanillaBoomObservation();
        SDL_Log("DUNECITY_AUDIT: minute=%d cycle=%u house=%d credits=%d power=%d need=%d population=%d tax=%d "
                "demand=%d built=%d structures_lost=%d base_hp=%d yards=%d yard_level=%d queue=%d upgrading=%d "
                "waiting=%d fronts=%s gun=%d rocket=%d radar=%d radar_built=%d starport=%d starport_built=%d "
                "heavy=%d heavy_built=%d ix=%d ix_built=%d fighters=%d moving=%d targeting=%d enemies=%d "
                "enemy_value=%d covered_enemies=%d spice=%d refineries=%d power_required=%d "
                "boom_phase=%s boom_stop_minute=%d boom_own_army=%lld boom_enemy_army=%lld boom_near_army=%lld "
                "boom_own_economy=%lld boom_leader_economy=%lld boom_army_ratio_bps=%d boom_economy_ratio_bps=%d "
                "boom_reachable_spice_share=%lld boom_refinery_cap=%lld boom_harvester_cap=%lld boom_current_workers=%d",
                minute, game.getGameCycleCount(), house->getHouseID(), house->getCredits(),
                house->getProducedPower(), house->getPowerRequirement(), population, tax, demand,
                house->getNumBuiltStructures(), structuresLost, structureHP,
                house->getNumItems(Structure_ConstructionYard), yardLevel, queue, upgrading, waiting,
                fronts.empty() ? "none" : fronts.c_str(), house->getNumItems(Structure_GunTurret),
                house->getNumItems(Structure_RocketTurret), house->getNumItems(Structure_Radar),
                house->getNumBuiltItems(Structure_Radar), house->getNumItems(Structure_StarPort),
                house->getNumBuiltItems(Structure_StarPort), house->getNumItems(Structure_HeavyFactory),
                house->getNumBuiltItems(Structure_HeavyFactory), house->getNumItems(Structure_IX),
                house->getNumBuiltItems(Structure_IX), fighters, moving, targeting, enemies, enemyValue,
                coveredEnemies, bot->lastCalculatedSpice, house->getNumItems(Structure_Refinery),
                int(house->isPowerRequired()), QuantBotVanillaPolicy::phaseName(observation.plan), observation.stopMinute,
                static_cast<long long>(observation.ownArmy), static_cast<long long>(observation.enemyArmy),
                static_cast<long long>(observation.nearArmy), static_cast<long long>(observation.ownEconomy),
                static_cast<long long>(observation.leaderEconomy), observation.armyRatioBps, observation.economyRatioBps,
                static_cast<long long>(observation.reachableSpiceShare), static_cast<long long>(observation.refineryCap),
                static_cast<long long>(observation.harvesterCap), observation.currentWorkers);
        // BuilderBase expands a squad into 3 (or 5) Unit_Trooper objects before
        // informWasBuilt is called for each single. Never multiply that ledger
        // again. Queue entries still describe products, so expand only those.
        // Losses follow the engine's original-house ledger; live units follow
        // their present owner. Killed-items is the lethal damagerOwner's ledger
        // by victim type, including friendly splash, NOT enemy-only AA kills.
        // Exact Trooper AA is derived separately from lethal_damage's natural
        // attacker_item/firing-house provenance, never from a Deviator reward bucket.
        SDL_Log("DUNECITY_TROOPERS: minute=%d cycle=%u house=%d wor=%d wor_built=%d"
                " trooper_live=%d trooper_queued=%d trooper_single_queued=%d trooper_squads_queued=%d"
                " trooper_squads5_queued=%d trooper_built=%d ornithopter_live=%d elite_ornithopter_live=%d"
                " ornithopter_built=%d elite_ornithopter_built=%d ornithopter_lost=%d elite_ornithopter_lost=%d"
                " ornithopter_killed=%d elite_ornithopter_killed=%d",
                minute, game.getGameCycleCount(), house->getHouseID(), wor, house->getNumBuiltItems(Structure_WOR),
                trooperLive, trooperSingleQueued + 3 * trooperSquadsQueued + 5 * trooperSquads5Queued,
                trooperSingleQueued, trooperSquadsQueued, trooperSquads5Queued, house->getNumBuiltItems(Unit_Trooper),
                ornithopterLive, eliteOrnithopterLive, house->getNumBuiltItems(Unit_Ornithopter),
                house->getNumBuiltItems(Unit_EliteOrnithopter), house->getNumLostItems(Unit_Ornithopter),
                house->getNumLostItems(Unit_EliteOrnithopter), house->getNumKilledItems(Unit_Ornithopter),
                house->getNumKilledItems(Unit_EliteOrnithopter));
        // Retain source-separated balances and the existing accounting ledger
        // before disposable game profiles are removed. Read-only, probe-only;
        // the cumulative spending/loss fields need --attack-events telemetry.
        SDL_Log("DUNECITY_ECONOMY: minute=%d cycle=%u house=%d ledger_enabled=%d"
                " spice_gross=%d tax_gross=%d starting_cash=%d spice_cash=%d city_cash=%d"
                " capacity=%d yard_storage=%d police_funding=%d army_target=%d unit_limit=%d"
                " spice_multiplier=%u sale_credits=%u storage_full_cycles=%u storage_overflow_cycles=%u"
                " storage_income_lost=%lld power_deficit_cycles=%u required_power_deficit_cycles=%u economy=%s",
                minute,game.getGameCycleCount(),house->getHouseID(),int(AITelemetry::log().enabled()),
                house->getHarvestedSpice().lround(),house->getCityTaxReceipts().lround(),
                house->getStartingCredits().lround(),house->getStoredCredits().lround(),house->getCityCredits().lround(),
                house->getCapacity(),house->getNumItems(Structure_ConstructionYard)
                    *game.objectData.data[Structure_ConstructionYard][house->getHouseID()].capacity,
                policeFunding,bot->militaryValueLimit,int(house->isGroundUnitLimitReached()),
                house->getSpiceIncomeMultiplier(),house->getSaleCredits(),
                storage[house->getHouseID()].fullCycles,storage[house->getHouseID()].overflowCycles,
                static_cast<long long>(storage[house->getHouseID()].incomeLostRaw/(int64_t{1}<<32)),
                storage[house->getHouseID()].powerDeficitCycles,
                storage[house->getHouseID()].requiredPowerDeficitCycles,
                AITelemetry::log().economyTotals(house->getHouseID()).json().c_str());
    }
}

} // namespace DuneCityAudit
