// Probe-only, read-only DuneCity Random Skirmish audit. Enabled by the matrix harness through
// BALANCE_DUNECITY_AUDIT; no changes to the shipped game or to AI decisions.
#include <dunecity/CitySimulation.h>
#include <dunecity/PowerRules.h>
#include <structures/BuilderBase.h>
#include <array>

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
