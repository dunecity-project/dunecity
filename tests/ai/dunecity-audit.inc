// Probe-only, read-only DuneCity Random Skirmish audit. Enabled by the matrix harness through
// BALANCE_DUNECITY_AUDIT; no changes to the shipped game or to AI decisions.
#include <dunecity/CitySimulation.h>
#include <dunecity/PowerRules.h>
#include <dunecity/SpatialFields.h>
#include <structures/BuilderBase.h>
#include <structures/ConstructionYard.h>
#include <array>
#include <cmath>
#include <vector>

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
        std::array<double,2> result{-1,-1};
        if (!allBases || houses.size()<2) return result;
        double total=0,minimum=0;
        int pairs=0;
        for (size_t a=0;a<houses.size();++a) for (size_t b=a+1;b<houses.size();++b) {
            const int first=houses[a]->getHouseID(),second=houses[b]->getHouseID();
            if (owner>=0 && first!=owner && second!=owner) continue;
            const double distance=std::hypot(double(bases[first].x-bases[second].x),
                double(bases[first].y-bases[second].y));
            if (!pairs || distance<minimum) minimum=distance;
            total+=distance; ++pairs;
        }
        if (pairs) result={minimum,total/pairs};
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
                " base_x=%d base_y=%d base_distance_min=%.9f base_distance_mean=%.9f",
            owner,w,h,houses.size(),double(spiceTiles)/n,spiceAmount,double(mountains)/n,
            measured.area,formations,largest,measured.origins33,largestOrigins,measured.area,measured.free,
            measured.free,rangeTiles,reserved,measured.origins22,measured.origins32,range22,range32,range33,
            base.x,base.y,distance[0],distance[1]);
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
    int capacity{};
    int64_t lostRaw{}, incomeLostRaw{};
    bool full{};
};
inline std::array<StorageObservation,NUM_HOUSES> storage;

// Every simulation tick, not a minute-sample estimate. The private ledger is
// read only in this existing -fno-access-control probe. A capacity-loss tick is
// excluded from income overflow: destroying a vault is not failure to buy one.
inline void observe(Game& game) {
    static const bool enabled = std::getenv("BALANCE_DUNECITY_AUDIT")!=nullptr;
    if (!enabled) return;
    const Uint32 cycle = game.getGameCycleCount();
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
            if (lost>state.lostRaw && house->getCapacity()>=state.capacity) {
                state.overflowCycles+=elapsed;
                state.incomeLostRaw+=lost-state.lostRaw;
            }
        }
        state.house=house;state.cycle=cycle;state.capacity=house->getCapacity();state.lostRaw=lost;
        state.full=house->isAlive() && state.capacity>0 && house->getEarnedCredits()>=FixPoint(state.capacity);
    }
}

inline void sample(Game& game) {
    if (!std::getenv("BALANCE_DUNECITY_AUDIT")) return;
    observe(game);
    sampleMapFeatures(game);
    const int minute = static_cast<int>(game.getGameCycleCount() / MILLI2CYCLES(60000));
    for (const House* house : EliminationMatch::playingHouses(game)) {
        const QuantBot* bot = FfaArmy::quantBotOf(house);
        if (!bot) continue;
        sampleComposition(game,house,bot,minute);
        const auto grid = FfaArmy::baseGrid(house);
        int fighters = 0, moving = 0, targeting = 0, enemies = 0, enemyValue = 0, coveredEnemies = 0;
        int trooperLive = 0, ornithopterLive = 0, eliteOrnithopterLive = 0;
        for (const auto* unit : unitList) {
            if (unit->getOwner() == house && unit->getHealth() > 0) {
                trooperLive += int(unit->getItemID() == Unit_Trooper);
                ornithopterLive += int(unit->getItemID() == Unit_Ornithopter);
                eliteOrnithopterLive += int(unit->getItemID() == Unit_EliteOrnithopter);
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
        std::string fronts;
        for (const auto* structure : structureList) {
            if (structure->getOwner() != house || structure->getHealth() <= 0) continue;
            wor += int(structure->getItemID() == Structure_WOR);
            structureHP += structure->getHealth().lround();
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
        if (const auto* sim = game.getCitySimulation(); sim && sim->isInitialized()) {
            const auto& city = sim->getHouseState(house->getHouseID());
            population = city.getTotalPop() * DuneCity::CitySimulation::kPopDisplayMultiplier;
            tax = city.budget.getLastTaxRevenue();
            demand = std::max({int(city.resValve), int(city.comValve), int(city.indValve)});
            policeFunding = city.policeFundingPercent;
        }
        SDL_Log("DUNECITY_AUDIT: minute=%d cycle=%u house=%d credits=%d power=%d need=%d population=%d tax=%d "
                "demand=%d built=%d structures_lost=%d base_hp=%d yards=%d yard_level=%d queue=%d upgrading=%d "
                "waiting=%d fronts=%s gun=%d rocket=%d radar=%d radar_built=%d starport=%d starport_built=%d "
                "heavy=%d heavy_built=%d ix=%d ix_built=%d fighters=%d moving=%d targeting=%d enemies=%d "
                "enemy_value=%d covered_enemies=%d spice=%d refineries=%d power_required=%d",
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
                int(house->isPowerRequired()));
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
                " storage_income_lost=%lld economy=%s",
                minute,game.getGameCycleCount(),house->getHouseID(),int(AITelemetry::log().enabled()),
                house->getHarvestedSpice().lround(),house->getCityTaxReceipts().lround(),
                house->getStartingCredits().lround(),house->getStoredCredits().lround(),house->getCityCredits().lround(),
                house->getCapacity(),house->getNumItems(Structure_ConstructionYard)
                    *game.objectData.data[Structure_ConstructionYard][house->getHouseID()].capacity,
                policeFunding,bot->militaryValueLimit,int(house->isGroundUnitLimitReached()),
                house->getSpiceIncomeMultiplier(),house->getSaleCredits(),
                storage[house->getHouseID()].fullCycles,storage[house->getHouseID()].overflowCycles,
                static_cast<long long>(storage[house->getHouseID()].incomeLostRaw/(int64_t{1}<<32)),
                AITelemetry::log().economyTotals(house->getHouseID()).json().c_str());
    }
}

} // namespace DuneCityAudit
