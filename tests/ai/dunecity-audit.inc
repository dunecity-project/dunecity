// Probe-only, read-only DuneCity Random Skirmish audit. Enabled by the matrix harness through
// BALANCE_DUNECITY_AUDIT; no changes to the shipped game or to AI decisions.
#include <dunecity/CitySimulation.h>
#include <dunecity/PowerRules.h>
#include <structures/BuilderBase.h>
#include <array>

namespace DuneCityAudit {

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
    const int minute = static_cast<int>(game.getGameCycleCount() / MILLI2CYCLES(60000));
    for (const House* house : EliminationMatch::playingHouses(game)) {
        const QuantBot* bot = FfaArmy::quantBotOf(house);
        if (!bot) continue;
        const auto grid = FfaArmy::baseGrid(house);
        int fighters = 0, moving = 0, targeting = 0, enemies = 0, enemyValue = 0, coveredEnemies = 0;
        for (const auto* unit : unitList) {
            if (!FfaArmy::groundFighter(unit)) continue;
            if (unit->getOwner() == house) {
                ++fighters;
                moving += int(unit->isMoving());
                const auto* target = unit->getTarget();
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
        std::string fronts;
        for (const auto* structure : structureList) {
            if (structure->getOwner() != house || structure->getHealth() <= 0) continue;
            structureHP += structure->getHealth().lround();
            if (const auto* builder = dynamic_cast<const BuilderBase*>(structure)) {
                queue += builder->getProductionQueueSize();
                upgrading += int(builder->isUpgrading());
                waiting += int(builder->isWaitingToPlace());
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
                "enemy_value=%d covered_enemies=%d spice=%d",
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
                coveredEnemies, bot->lastCalculatedSpice);
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
