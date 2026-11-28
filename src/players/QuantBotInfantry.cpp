// Infantry squads and infantry production for QuantBot (docs/infantry-squads.md).
//
// Stefan, 2026-11-28: "in the original dune you could build three troopers for a discount" ... "yes and the
// ai should use it if it wants to build these men. also additional infantry and wor should be built if the ai
// needs more capacity similar to factories. also include infantry in the calculation for what to build (same
// performance algorithm)". Three switches, each default off until measured:
//  - SquadOrders: an infantry order takes the offer cheapest per man; off, QuantBot never orders a squad.
//  - InfantryLanes: another Barracks or WOR by the Light/Heavy Factory lane rule
//    (QuantBotBuildPolicy::needsProductionLane).
//  - InfantryInMix: in a city game the Barracks and WOR build to the measured infantry share, not only
//    against observed aircraft.
// Every decision is a match-log line (INFANTRY_MIX, INFANTRY_LANE; the engine logs INFANTRY_ORDER and
// INFANTRY_SQUAD) that tests/ai/run-infantry-squads-real-game.py and tests/ai/activation-events.json read.

#include "QuantBotInternal.h"
#include <players/QuantBotBuildPass.h>
#include <structures/BuilderBase.h>
#include <dunecity/InfantrySquadPolicy.h>

#include <algorithm>

namespace {
/// One INFANTRY_MIX line a house per game minute.
constexpr Uint32 kInfantryMixLogCycles = MILLI2CYCLES(60 * 1000);
/// One INFANTRY_LANE backlog line a house and producer per game minute.
constexpr Uint32 kInfantryLaneLogCycles = MILLI2CYCLES(60 * 1000);
/// "if the ai needs more capacity": the factory lane rule must hold this long, and the deficit must be at least
/// kLaneDeficitOrders orders per existing lane. A lane added on one busy moment sat idle in the first real games
/// (men per 10 minutes 10 -> 10 and 13 -> 7 after the lane): demand, not capacity, was the limit.
constexpr Uint32 kLaneSustainCycles = MILLI2CYCLES(120 * 1000);
constexpr int kLaneDeficitOrders = 3;

size_t laneIndex(Uint32 producer) { return producer == Structure_WOR ? 1 : 0; }
}

const char* QuantBot::squadOrderRefusal(const BuilderBase* builder, Uint32 item) const {
    // Tornie content keeps its own order rules (its five-man orders, its own tables).
    if (!DuneCity::InfantrySquad::threeManSquad(item) || ModManager::instance().isTornieFamilyContentActive())
        return nullptr;
    if (!botConfig().force.squadOrders) return "squad_orders_off";
    const Uint32 single = item == Unit_Infantry ? Unit_Soldier : Unit_Trooper;
    const int singlePrice = purchasePrice(builder, single);
    if (singlePrice > 0 && !DuneCity::InfantrySquad::cheaperPerMan(purchasePrice(builder, item), 3, singlePrice, 1))
        return "single_cheaper_per_man";
    return nullptr;
}

Uint32 QuantBot::infantryOrderFor(const BuilderBase* builder, Uint32 single, int64_t money) const {
    if (!botConfig().force.squadOrders || ModManager::instance().isTornieFamilyContentActive()) return single;
    const Uint32 squad = DuneCity::InfantrySquad::squadOf(single);
    if (squad == ItemID_Invalid || !campaignAvailableToBuild(builder, squad) || squadOrderRefusal(builder, squad))
        return single;
    if (purchasePrice(builder, squad) > money) return single;
    return squad;
}

void QuantBot::logInfantryMix(const BuildPass& pass, int bps, bool learning, int64_t reward, int64_t loss,
                              int64_t score, bool troopers) {
    if (infantryMixOpeningBps_ < 0) infantryMixOpeningBps_ = bps;
    const Uint32 now = currentGame->getGameCycleCount();
    if (infantryMixLogged_ && now < infantryMixLogCycle_ + kInfantryMixLogCycles) return;
    infantryMixLogged_ = true;
    infantryMixLogCycle_ = now;
    // on_share: the infantry the house has reaches at least half of the share the mix gives it.
    const bool onShare = bps >= 200 && int64_t(pass.infantryValue) * 20000 >= int64_t(pass.militaryValue) * bps;
    SDL_Log("INFANTRY_MIX: house=%d share_bps=%d opening_bps=%d learning=%d reward_milli=%lld loss_milli=%lld score=%lld type=%s infantry_value=%d military_value=%d on_share=%d cycle=%u",
        getHouse()->getHouseID(), bps, infantryMixOpeningBps_, learning ? 1 : 0, static_cast<long long>(reward),
        static_cast<long long>(loss), static_cast<long long>(score), troopers ? "trooper" : "soldier",
        pass.infantryValue, pass.militaryValue, onShare ? 1 : 0, now);
}

Uint32 QuantBot::infantryLaneProducer() const {
    const int houseID = getHouse()->getHouseID();
    const auto& trooper = currentGame->objectData.data[Unit_Trooper][houseID];
    // The Troopers are the infantry the house buys once it can field them (planUnitMix scores them).
    const bool troopers = trooper.enabled && trooper.price > 0 && trooper.techLevel <= getHouse()->getTechLevel()
        && getHouse()->getNumItems(Structure_WOR) > 0;
    return troopers ? Structure_WOR : Structure_Barracks;
}

bool QuantBot::infantryLaneBacklog(BuildPass& pass, Uint32& producer) {
    producer = infantryLaneProducer();
    if (!botConfig().force.infantryLanes || ModManager::instance().isTornieFamilyContentActive()
        || getHouse()->isGroundUnitLimitReached()) return false;
    // The infantry share's value of the army the house is funding, as the vehicle lanes compare the
    // funded mix with what is fielded and queued (planYardLanes).
    const int64_t army = std::max<int64_t>(pass.militaryValue, int64_t(pass.vehiclePlanValue) + pass.infantryValue);
    const int deficit = int(std::max<int64_t>(0, army * pass.infantryPercent / 100 - pass.infantryValue));
    int busy = 0;
    for (const StructureBase* structure : getStructureList()) {
        if (structure->getOwner() != getHouse() || structure->getItemID() != int(producer)) continue;
        if (static_cast<const BuilderBase*>(structure)->getProductionQueueSize() > 0) ++busy;
    }
    const int houseID = getHouse()->getHouseID();
    const int actual = getHouse()->getNumItems(producer);
    const auto& data = currentGame->objectData.data;
    const Uint32 single = producer == Structure_WOR ? Unit_Trooper : Unit_Soldier;
    const Uint32 squad = DuneCity::InfantrySquad::squadOf(single);
    const int orderValue = botConfig().force.squadOrders && data[squad][houseID].enabled
        ? data[squad][houseID].price : data[single][houseID].price;
    const bool rule = QuantBotBuildPolicy::needsProductionLane(actual, pass.itemCount[producer], busy, deficit,
        pass.money, pass.economyReserve, data[producer][houseID].price)
        && int64_t(deficit) >= int64_t(kLaneDeficitOrders) * actual * std::max(1, orderValue);
    const Uint32 now = currentGame->getGameCycleCount();
    auto& since = infantryLaneRuleSince_[laneIndex(producer)];
    if (!rule) since = 0;
    else if (since == 0) since = std::max<Uint32>(1, now);
    const bool backlog = rule && now >= since + kLaneSustainCycles;
    auto& logged = infantryLaneLogCycle_[laneIndex(producer)];
    if (backlog && (logged == 0 || now >= logged + kInfantryLaneLogCycles)) {
        logged = std::max<Uint32>(1, now);
        SDL_Log("INFANTRY_LANE: event=backlog house=%d producer=%u have=%d busy=%d deficit=%d credits=%d cycle=%u",
            houseID, producer, actual, busy, deficit, pass.money, now);
    }
    return backlog;
}

void QuantBot::noteInfantryLaneOrdered(Uint32 producer) {
    const size_t lane = laneIndex(producer);
    ++infantryLaneOrdered_[lane];
    SDL_Log("INFANTRY_LANE: event=ordered house=%d producer=%u have=%d cycle=%u", getHouse()->getHouseID(),
        producer, getHouse()->getNumItems(producer), currentGame->getGameCycleCount());
}

void QuantBot::updateInfantryLanes() {
    // A lane ordered by the rule stands once the house has more of that producer than before the order.
    for (const Uint32 producer : {Structure_Barracks, Structure_WOR}) {
        const size_t lane = laneIndex(producer);
        const int have = getHouse()->getNumItems(producer);
        if (infantryLaneOrdered_[lane] > 0 && have > infantryLaneSeen_[lane]) {
            --infantryLaneOrdered_[lane];
            SDL_Log("INFANTRY_LANE: event=standing house=%d producer=%u count=%d cycle=%u", getHouse()->getHouseID(),
                producer, have, currentGame->getGameCycleCount());
        }
        infantryLaneSeen_[lane] = have;
    }
}

bool QuantBot::cityInfantryShare() const {
    return botConfig().force.infantryInMix && !ModManager::instance().isTornieFamilyContentActive();
}

int QuantBot::infantryQuotaValue(Uint32 item, int value) const {
    if (!botConfig().force.squadOrders || ModManager::instance().isTornieFamilyContentActive()
        || !DuneCity::InfantrySquad::threeManSquad(item)) return value;
    return std::max(1, value / 3);
}

Uint32 QuantBot::infantryFirstProducer(BuildPass& pass, const BuilderBase* yard) const {
    if (!cityInfantryShare() || pass.infantryPercent < 2
        || pass.itemCount[Structure_Barracks] + pass.itemCount[Structure_WOR] > 0) return NONE_ID;
    const int houseID = getHouse()->getHouseID();
    for (const Uint32 producer : {Structure_WOR, Structure_Barracks}) {
        if (!campaignAvailableToBuild(yard, producer)) continue;
        const int price = currentGame->objectData.data[producer][houseID].price;
        if (pass.money < pass.economyReserve + pass.cityWorkingReserve + price + 1000) return NONE_ID;
        return producer;
    }
    return NONE_ID;
}
