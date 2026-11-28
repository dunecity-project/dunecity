// Private balance executable: forced, funded infantry demand; real QuantBot orders and engine output.
// This proves capacity causality, not natural activation frequency or a score benefit.
#include <players/QuantBotStrategy.h>

namespace InfantryLanesProbe {
using TechProductionProbe::require;

constexpr int kSamples = 480;
constexpr int kMinimumOutputSamples = 120;
constexpr Uint32 kSampleCycles = MILLI2CYCLES(1000);

struct Result {
    std::vector<int> delivered;
    int firstExtra = -1;
    int firstQueued = -1;
    int builtProducers = 0;
    int maxPending = 0;
};

Result run(HOUSETYPE side, bool enabled) {
    const Uint32 producer = side == HOUSE_ATREIDES ? Structure_Barracks : Structure_WOR;
    const Uint32 infantry = side == HOUSE_ATREIDES ? Unit_Soldier : Unit_Trooper;
    TechProductionProbe::World world("dunecity", "qBotBrutal", false,
        TechProductionProbe::Stage::Complete, TechProductionProbe::Army::ReadyLight,
        8, false, side, 3, 0);
    auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
    config->force.infantryLanes = enabled;
    config->force.infantryInMix = true;
    config->force.squadOrders = false;
    config->force.trooperSupport = false;
    config->strategy.random = false;
    config->strategy.fixedMix = QuantBotStrategy::Mix::Infantry;
    config->strategy.fixedTempo = QuantBotStrategy::Tempo::Base;
    world.bot->configOverride = config;
    world.bot->strategyPicked = false;
    world.bot->pickStrategy();
    require(world.bot->strategy.mix == QuantBotStrategy::Mix::Infantry,
        "Infantry capacity fixture did not select its real 35-percent Infantry strategy");

    // The mature technology fixture has two infantry producers. Retain one as the scenario start;
    // any infantry released by setup destruction precedes the production counter baseline below.
    std::vector<StructureBase*> extra;
    bool retained = false;
    for (auto* structure : structureList) {
        if (structure->getOwner() != world.owner || structure->getItemID() != int(producer)) continue;
        if (retained) extra.push_back(structure);
        retained = true;
    }
    for (auto* structure : extra) structure->destroy();
    require(world.owner->getNumItems(producer) == 1, "Capacity fixture must start with exactly one producer");
    // Parallel genuine yards let optional vehicle capacity and infantry demand be considered in the same pass.
    auto* parallelYard = static_cast<ConstructionYard*>(world.place(Structure_ConstructionYard));
    parallelYard->curUpgradeLev = parallelYard->getMaxUpgradeLevel();
    parallelYard->updateBuildList();
    for (int i = 0; i < 48; ++i)
        world.spawn(world.owner, Unit_Tank, Coord(4 + i % 24, 42 + i / 24));
    world.bot->militaryValueLimit = 200000;
    world.cash(100000); // Explicit initial scenario grant, never replenished during the measurement.
    world.bot->initialMilitaryValue = world.bot->militaryUnitValue();
    for (int item = ItemID_FirstID; item <= ItemID_LastID; ++item)
        world.bot->initialItemCount[item] = world.owner->getNumItems(item);
    world.owner->updateBuildLists();
    const auto initialPlan = world.observePlan();
    const int64_t army = std::max<int64_t>(initialPlan.militaryValue,
        int64_t(initialPlan.vehiclePlanValue) + initialPlan.infantryValue);
    const int64_t deficit = army * initialPlan.infantryPercent / 100 - initialPlan.infantryValue;
    require(initialPlan.infantryPercent == 35 && deficit >= world.price(producer),
        "Forced infantry demand is not above the lane-price backlog threshold");
    require(initialPlan.money >= initialPlan.economyReserve + world.price(producer) + 1000,
        "Forced infantry backlog is not genuinely funded");
    SDL_Log("INFANTRY_LANES_FIXTURE: house=%d enabled=%d producer=%u share=%d deficit=%lld price=%d credits=%d reserve=%d",
        side, enabled ? 1 : 0, producer, initialPlan.infantryPercent, static_cast<long long>(deficit),
        world.price(producer), initialPlan.money, initialPlan.economyReserve);

    const int initialMenBuilt = world.owner->getNumBuiltItems(infantry);
    const int initialProducersBuilt = world.owner->getNumBuiltItems(producer);
    Result result;
    result.delivered.reserve(kSamples + 1);
    for (int sample = 0; sample <= kSamples; ++sample) {
        result.delivered.push_back(world.owner->getNumBuiltItems(infantry) - initialMenBuilt);
        const int pending = world.queued(producer);
        result.maxPending = std::max(result.maxPending, pending);
        if (pending > 0 && result.firstQueued < 0) result.firstQueued = sample;
        if (world.owner->getNumItems(producer) > 1 && result.firstExtra < 0) result.firstExtra = sample;
        if (sample == kSamples) break;
        world.build(); // Full planning, ordinary producer orders, normal placement consumer.
        for (Uint32 cycle = 0; cycle < kSampleCycles; ++cycle) world.tick();
    }
    result.builtProducers = world.owner->getNumBuiltItems(producer) - initialProducersBuilt;
    require(result.delivered.back() > 0, "Capacity fixture never delivered any purchased infantry");
    require(result.maxPending <= 1, "Parallel yards duplicated pending infantry capacity");
    if (enabled) {
        require(result.firstQueued >= 0 && result.firstExtra >= 0 && result.builtProducers > 0,
            "Sustained funded infantry backlog did not produce a real additional Barracks/WOR");
        require(result.firstExtra <= kSamples - kMinimumOutputSamples,
            "Additional infantry capacity arrived too late for its bounded output measurement");
    } else {
        require(result.firstQueued < 0 && result.firstExtra < 0 && result.builtProducers == 0,
            "OFF control added infantry capacity through a different controller");
    }
    SDL_Log("INFANTRY_LANES_ARM: house=%d enabled=%d first_order_sample=%d first_standing_sample=%d extra_built=%d men=%d max_pending=%d",
        side, enabled ? 1 : 0, result.firstQueued, result.firstExtra, result.builtProducers,
        result.delivered.back(), result.maxPending);
    return result;
}

void compare(HOUSETYPE side) {
    const Result off = run(side, false);
    const Result on = run(side, true);
    const int from = on.firstExtra;
    require(from >= 0 && from < kSamples, "Missing real capacity placement before comparing output");
    const int offOutput = off.delivered.back() - off.delivered[from];
    const int onOutput = on.delivered.back() - on.delivered[from];
    require(onOutput > offOutput,
        "Additional infantry producer did not raise actual delivered infantry against the same-time OFF control");
    SDL_Log("INFANTRY_LANES_CASE: house=%d output_from_sample=%d output_to_sample=%d off_men=%d on_men=%d passed=1",
        side, from, kSamples, offOutput, onOutput);
}
} // namespace InfantryLanesProbe

void runInfantryLanesProbe() {
    InfantryLanesProbe::compare(HOUSE_ATREIDES);
    InfantryLanesProbe::compare(HOUSE_HARKONNEN);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=infantry-lanes outcome=passed");
}
