// Private real-engine SquadOrders acceptance. No probe directly creates purchased infantry.
#include <players/QuantBotStrategy.h>
#include <players/QuantBotSpendingPolicy.h>

namespace StrictSquadsProbe {
using TechProductionProbe::require;
constexpr Uint32 kStepCycles = MILLI2CYCLES(1000);

struct Fixture {
    TechProductionProbe::World world;
    std::shared_ptr<QuantBotConfig> config;
    Uint32 single, squad;
    int baseline;
    const char* name;

    Fixture(const char* label, HOUSETYPE side, bool enabled)
        : world("dunecity", "qBotBrutal", false, TechProductionProbe::Stage::Complete,
                TechProductionProbe::Army::ReadyLight, 8, false, side, 3, 0),
          single(side == HOUSE_ATREIDES ? Unit_Soldier : Unit_Trooper),
          squad(side == HOUSE_ATREIDES ? Unit_Infantry : Unit_Troopers), name(label) {
        config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
        config->force.squadOrders = enabled;
        config->force.infantryInMix = true;
        config->force.infantryLanes = false;
        config->force.trooperSupport = false;
        config->strategy.random = false;
        config->strategy.fixedMix = QuantBotStrategy::Mix::Infantry;
        config->strategy.fixedTempo = QuantBotStrategy::Tempo::Base;
        world.bot->configOverride = config;
        world.bot->strategyPicked = false;
        world.bot->pickStrategy();
        for (int i = 0; i < 48; ++i)
            world.spawn(world.owner, Unit_Tank, Coord(4 + i % 24, 42 + i / 24));
        world.closeMilitaryMarket();
        world.bot->militaryValueLimit = 200000;
        world.cash(100000);
        world.bot->initialMilitaryValue = world.bot->militaryUnitValue();
        for (int item = ItemID_FirstID; item <= ItemID_LastID; ++item)
            world.bot->initialItemCount[item] = world.owner->getNumItems(item);
        baseline = world.owner->getNumBuiltItems(single);
        require(builder() && builder()->isAvailableToBuild(single) && builder()->isAvailableToBuild(squad),
            "Strict squad fixture does not genuinely offer both single and squad");
        SDL_Log("STRICT_SQUADS_BEGIN: case=%s strict=%d house=%d single=%u squad=%u cycle=%u",
            name, int(enabled), side, single, squad, world.game->getGameCycleCount());
    }
    BuilderBase* builder() const {
        return world.producer(single == Unit_Soldier ? Structure_Barracks : Structure_WOR);
    }
    int men() const { return world.owner->getNumBuiltItems(single) - baseline; }
    void step() { for (Uint32 i = 0; i < kStepCycles; ++i) world.tick(); }
    void end() {
        SDL_Log("STRICT_SQUADS_END: case=%s men=%d cycle=%u", name, men(), world.game->getGameCycleCount());
    }
    QuantBot::BuildPass plan() {
        // Accepted orders consume the complete capital plan. observePlan() intentionally omits that
        // final stage and its default capitalChoice=0 is not an executable production context.
        QuantBot::BuildPass pass(world.bot->militaryUnitValue());
        world.bot->resetBuildPass();
        world.bot->planBuildInventory(pass);
        world.bot->planBuildBudget(pass);
        world.bot->planCityState(pass);
        world.bot->planStrategicReserve(pass);
        world.bot->planSpiceEconomy(pass);
        world.bot->planPowerAndYards(pass);
        world.bot->planCityPopulation(pass);
        world.bot->planCivicNeedAndTrips(pass);
        world.bot->traceBuildState(pass);
        world.bot->planUnitMix(pass);
        world.bot->planCapitalForecast(pass);
        world.bot->planInfantrySquadSavings(pass);
        world.bot->planCityBootstrap(pass);
        world.bot->planOpeningProduction(pass);
        world.bot->planDefenceCoverage(pass);
        world.bot->planVanillaCoreDefence(pass);
        world.bot->planTurretClusters(pass);
        world.bot->planCityEconomyProjects(pass);
        world.bot->planPalaceTurrets(pass);
        world.bot->planAirRaidDefence(pass);
        world.bot->planAirportTurrets(pass);
        world.bot->planTurretFront(pass);
        world.bot->planEconomyFirst(pass);
        world.bot->planExtraAirport(pass);
        world.bot->planCityResearchOrder(pass);
        world.bot->planTechSavings(pass);
        world.bot->planMissionObjectives(pass);
        world.bot->planCapitalCandidates(pass);
        world.bot->planVanillaBoldCapital(pass);
        world.bot->chooseCapitalPlan(pass);
        if (!pass.vanillaBoomArmy) world.bot->planTechResearch(pass);
        return pass;
    }
    QuantBot::BuildPass constrained(int cash, int room) {
        auto pass = plan();
        pass.cityWorkingReserve = std::max(500, pass.cityWorkingReserve);
        pass.money = pass.cityWorkingReserve + cash;
        pass.lightBudget = pass.militaryValue + room;
        pass.militaryBudget = pass.lightBudget;
        world.cash(pass.money);
        world.bot->planInfantrySquadSavings(pass);
        return pass;
    }
    void select(QuantBot::BuildPass& pass) {
        QuantBot::BuilderTurn turn(pass.money);
        turn.pBuilder = builder(); turn.pStructure = builder();
        if (single == Unit_Soldier) world.bot->buildBarracks(pass, turn);
        else world.bot->buildWOR(pass, turn);
    }
    void finishOrder(int expected) {
        const int before = men();
        for (int i = 0; i < 60 && builder()->getProductionQueueSize() > 0; ++i) step();
        require(builder()->getProductionQueueSize() == 0 && men() - before == expected,
            "Accepted strict-squad order did not deploy its exact actual men within the engine bound");
    }
};

int rate(const char* name, HOUSETYPE side, bool enabled) {
    Fixture f(name, side, enabled);
    const Uint32 start = f.world.game->getGameCycleCount();
    for (int sample = 0; sample < 120; ++sample) { f.world.build(); f.step(); }
    const int men = f.men();
    require(men > 0, "Squad throughput fixture produced no purchased infantry");
    SDL_Log("STRICT_SQUADS_RATE: case=%s enabled=%d men=%d cycles=%u", name, int(enabled), men,
        f.world.game->getGameCycleCount() - start);
    f.end();
    return men;
}

void savingAndPremium(HOUSETYPE side, const char* label) {
    Fixture f(label, side, true);
    const int singlePrice = f.world.price(f.single);
    const int squadPrice = f.world.price(f.squad);
    // Even an old/direct selector cannot leak a single through the authoritative QuantBot order boundary.
    f.world.bot->doProduceItem(f.builder(), f.single);
    require(f.builder()->getProductionQueueSize() == 0, "A direct ordinary-single order bypassed strict policy");
    auto pass = f.constrained(squadPrice - 1, singlePrice);
    require(pass.infantrySquadCashReserve == squadPrice && pass.infantrySquadSavingsItem == f.squad,
        "Wanted squad did not reserve its full actual quote");
    const auto* light = f.world.producer(Structure_LightFactory);
    require(light, "Savings fixture lacks its optional competing producer");
    const int optionalPrice = f.world.bot->purchasePrice(light, Unit_Quad);
    require(!f.world.bot->spendingAdmits(pass, Unit_Quad, optionalPrice),
        "Optional competing order consumed the credits being saved for a squad");
    const int saved = pass.infantrySquadCashReserve;
    pass.infantrySquadCashReserve = 0;
    require(f.world.bot->spendingAdmits(pass, Unit_Quad, optionalPrice),
        "Savings control was blocked by an unrelated policy rather than the squad reserve");
    pass.infantrySquadCashReserve = saved;
    f.select(pass);
    require(f.builder()->getProductionQueueSize() == 0,
        "Cash short by one credit bought a single or queued an unaffordable squad");
    f.world.owner->addCredits(1); ++pass.money;
    f.select(pass);
    require(f.builder()->getCurrentProducedItem() == f.squad,
        "The fully saved quote failed to buy a squad with only a single desired in the value budget");
    f.finishOrder(3);
    // This deliberately makes the squad MORE expensive per man. Choice still depends only on availability.
    for (auto* structure : structureList) {
        if (structure->getOwner() != f.world.owner || structure->getItemID() != int(f.builder()->getItemID())) continue;
        auto* producer = static_cast<BuilderBase*>(structure);
        f.world.game->objectData.data[f.squad][producer->getOriginalHouseID()].price = singlePrice * 4;
        // Ordinary data prices are cached when an offer is inserted, not repriced by updateBuildList().
        // Rebuild that real catalogue after this fixture's deliberate data change; no pending order is altered.
        require(producer->getProductionQueueSize() == 0, "Premium fixture would invalidate a pending order quote");
        producer->buildList.clear();
        producer->updateBuildList();
    }
    const int premium = f.world.bot->purchasePrice(f.builder(), f.squad);
    require(premium > singlePrice * 3, "Premium fixture did not actually remove the per-man discount");
    auto narrow = f.constrained(premium, singlePrice);
    f.select(narrow);
    require(f.builder()->getCurrentProducedItem() == f.squad,
        "Per-man price or separate squad admission let a premium squad fall back to a single");
    f.finishOrder(3);
    SDL_Log("STRICT_SQUADS_CASE: case=%s saved_quote=%d premium_quote=%d original_single_room=%d delivered=%d passed=1",
        label, squadPrice, premium, singlePrice, f.men());
    f.end();
}

void producerTechBoundary() {
    Fixture f("producer_tech_boundary", HOUSE_ATREIDES, true);
    BuilderBase* upgraded = nullptr;
    for (auto* structure : structureList) {
        if (structure->getOwner() == f.world.owner && structure != f.builder()
                && structure->getItemID() == Structure_Barracks) {
            upgraded = static_cast<BuilderBase*>(structure);
            break;
        }
    }
    require(upgraded, "Producer-tech fixture lacks its second real Barracks");
    // Genuine upgrade prerequisites create one single-only producer and one squad-capable producer.
    f.world.game->objectData.data[f.squad][HOUSE_ATREIDES].upgradeLevel = 1;
    f.world.game->objectData.data[f.single][HOUSE_ATREIDES].upgradeLevel = 0;
    f.builder()->curUpgradeLev = 0;
    upgraded->curUpgradeLev = 1;
    f.builder()->updateBuildList();
    upgraded->updateBuildList();
    require(f.world.bot->campaignAvailableToBuild(f.builder(), f.single)
            && !f.world.bot->campaignAvailableToBuild(f.builder(), f.squad)
            && f.world.bot->campaignAvailableToBuild(upgraded, f.squad),
        "Producer-tech fixture did not establish its actual single/squad availability boundary");
    const int singlePrice = f.world.price(f.single);
    const int squadPrice = f.world.bot->purchasePrice(upgraded, f.squad);
    auto pass = f.constrained(squadPrice + singlePrice, 2 * singlePrice);
    require(pass.infantrySquadCashReserve == squadPrice, "Upgraded Barracks did not reserve its wanted squad");
    f.select(pass);
    require(f.builder()->getCurrentProducedItem() == f.single,
        "SquadOrders rejected a legal single when that producer cannot offer its squad");
    require(pass.infantrySquadCashReserve == squadPrice && pass.infantrySquadSavingsItem == f.squad,
        "Single-only producer released the other Barracks' saved squad credits");
    QuantBot::BuilderTurn turn(pass.money);
    turn.pBuilder = upgraded; turn.pStructure = upgraded;
    f.world.bot->buildBarracks(pass, turn);
    require(upgraded->getCurrentProducedItem() == f.squad,
        "Available upgraded producer failed to spend the full quote saved for its squad");
    for (int i = 0; i < 60 && (f.builder()->getProductionQueueSize() || upgraded->getProductionQueueSize()); ++i)
        f.step();
    require(f.builder()->getProductionQueueSize() == 0 && upgraded->getProductionQueueSize() == 0 && f.men() == 4,
        "Actual producer-tech boundary orders did not deliver exactly one plus three men");
    SDL_Log("STRICT_SQUADS_CASE: case=producer_tech_boundary single_without_squad=1 squad=1 delivered=%d passed=1", f.men());
    f.end();
}

void airEmergency() {
    Fixture f("real_air_emergency", HOUSE_HARKONNEN, true);
    f.config->force.trooperSupport = true;
    std::vector<UnitBase*> workers;
    for (auto* unit : unitList)
        if (unit->getOwner() == f.world.owner && unit->getItemID() == Unit_Harvester) workers.push_back(unit);
    for (auto* unit : workers) unit->destroy();
    f.world.game->getCitySimulation()->setCityTax(f.world.owner->getHouseID(), 0);
    f.world.spawn(f.world.enemy, Unit_Ornithopter, Coord(20, 38));
    for (int i = 0; i < 3; ++i) f.step();
    auto pass = f.constrained(f.world.price(f.single), f.world.price(f.single));
    require(pass.hostileAircraft > 0 && pass.airEngaged,
        "Emergency fixture has no real observed hostile aircraft response");
    require(f.world.bot->infantrySingleEmergency(pass, f.builder(), f.single),
        "Actual cash-poor air emergency did not grant its forecast-backed single exception");
    const int income = pass.forecastNetIncome;
    pass.forecastNetIncome = f.world.price(f.squad) * 60 * QuantBotSpendingPolicy::horizonMinutes;
    require(!f.world.bot->infantrySingleEmergency(pass, f.builder(), f.single),
        "The policy used a single although the squad could be funded within three seconds");
    pass.forecastNetIncome = income;
    const int aircraft = pass.hostileAircraft;
    pass.hostileAircraft = 0;
    require(!f.world.bot->infantrySingleEmergency(pass, f.builder(), f.single),
        "An ordinary cash shortage was classified as an air emergency");
    pass.hostileAircraft = aircraft;
    pass.airEngaged = false;
    require(!f.world.bot->infantrySingleEmergency(pass, f.builder(), f.single),
        "A distant observed aircraft was incorrectly classified as an immediate emergency");
    pass.airEngaged = true;
    f.select(pass);
    require(f.builder()->getCurrentProducedItem() == f.single,
        "A validated time-critical air exception did not reach the actual builder queue");
    f.finishOrder(1);
    SDL_Log("STRICT_SQUADS_CASE: case=real_air_emergency expected_exceptions=1 delivered=%d passed=1", f.men());
    f.end();
}

void garrisonLevy() {
    Fixture f("permitted_capture_squads", HOUSE_ATREIDES, true);
    f.config->force.zoneCaptureLevy = true;
    INIFile captureOptions(true, "Strict squad levy permission");
    captureOptions.setBoolValue("Game Options", "Garrisoning and Capturing", true);
    auto& options = const_cast<SettingsClass::GameOptionsClass&>(f.world.game->getGameInitSettings().getGameOptions());
    applyGameOptionsFromConfig(captureOptions, "Game Options", options);
    require(ZoneStructure::garrisonsEnabled(), "Levy fixture does not enable the real garrison rule");
    for (int y = 40; y < 42; ++y) for (int x = 50; x < 52; ++x)
        currentGameMap->getTile(x, y)->setType(Terrain_Rock);
    auto* target = f.world.enemy->placeStructure(NONE_ID, Structure_ZoneResidential, 50, 40, true, true);
    require(target, "Levy fixture could not place its real hostile zone");
    target->setVisible(VIS_ALL, true);
    f.world.bot->updateZoneGarrisons();
    require(f.world.queued(f.squad) > 0 && f.world.queued(f.single) == 0,
        "Independent raw SquadOrders left permitted capture recruitment buying singles");
    for (int i = 0; i < 60 && f.men() < 3; ++i) f.step();
    require(f.men() >= 3, "The permitted raw-Config levy never delivered its squad");
    SDL_Log("STRICT_SQUADS_CASE: case=permitted_capture_squads delivered=%d passed=1", f.men());
    f.end();
}
} // namespace StrictSquadsProbe

void runStrictSquadsProbe() {
    using namespace StrictSquadsProbe;
    const int soldierOff = rate("soldier_rate_off", HOUSE_ATREIDES, false);
    const int soldierOn = rate("soldier_rate_on", HOUSE_ATREIDES, true);
    require(soldierOn >= soldierOff, "SquadOrders reduced actual Soldier men/minute");
    const int trooperOff = rate("trooper_rate_off", HOUSE_HARKONNEN, false);
    const int trooperOn = rate("trooper_rate_on", HOUSE_HARKONNEN, true);
    require(trooperOn >= trooperOff, "SquadOrders reduced actual Trooper men/minute");
    savingAndPremium(HOUSE_ATREIDES, "soldier_save_and_premium");
    savingAndPremium(HOUSE_HARKONNEN, "trooper_save_and_premium");
    producerTechBoundary();
    airEmergency();
    garrisonLevy();
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=strict-squads outcome=passed");
}
