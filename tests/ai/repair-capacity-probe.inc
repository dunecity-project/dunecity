// Existing balance executable only: real queues, repair jobs, placement and saves.
#include <structures/RepairYard.h>
#include <players/AIDecisionLog.h>
#include <units/GroundUnit.h>
#include <units/Carryall.h>
#include <units/InfantryBase.h>
#include <filesystem>

namespace RepairCapacityProbe {
using TechProductionProbe::require;

struct World {
    std::unique_ptr<Game> game;
    House* owner = nullptr;
    QuantBot* bot = nullptr;
    std::shared_ptr<QuantBotConfig> config;
    ConstructionYard* first = nullptr;
    ConstructionYard* second = nullptr;
    RepairYard* bay = nullptr;
    std::vector<GroundUnit*> jobs;
    int completed = 0;

    explicit World(const char* mod, bool withBay = true) {
        require(!currentGame, "Repair fixtures share live engine globals");
        require(ModManager::instance().setActiveMod(mod), "Cannot activate repair fixture mod");
        auto options = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
        options.fogOfWar = false;
        options.startWithExploredMap = true;
        options.immortalHumanPlayer = true;
        options.instantBuild = false;
        options.concreteRequired = false;
        options.researchLab = false;
        options.maximumNumberOfUnitsOverride = 0;
        options.maximumNumberOfHarvestersOverride = 1;
        GameInitSettings init("repair-capacity.ini", TechProductionProbe::mapText(HOUSE_HARKONNEN), true, options);
        init.randomSeed = 7851;
        GameInitSettings::HouseInfo defender(HOUSE_HARKONNEN, 1);
        defender.addPlayerInfo({"Repair capacity QuantBot", "qBotHard"});
        init.addHouseInfo(defender);
        GameInitSettings::HouseInfo opponent(HOUSE_ATREIDES, 2);
        opponent.addPlayerInfo({settings.general.playerName, HUMANPLAYERCLASS});
        init.addHouseInfo(opponent);
        game = std::make_unique<Game>();
        currentGame = game.get();
        game->bReplay = true;
        game->initGame(init);
        game->bReplay = false;
        game->initializeGameLoop();
        game->bPause = false;
        owner = game->getHouse(HOUSE_HARKONNEN);
        for (const auto& player : owner->getPlayerList())
            if (auto* candidate = dynamic_cast<QuantBot*>(player.get())) bot = candidate;
        for (auto* structure : structureList)
            if (structure->getOwner() == owner && structure->getItemID() == Structure_ConstructionYard)
                first = static_cast<ConstructionYard*>(structure);
        require(bot && first && game->gameType == GameType::CustomGame, "Repair fixture is not a genuine Custom bot");
        config = std::make_shared<QuantBotConfig>(bot->botConfig());
        config->force.repairCapacityDemand = true;
        config->force.economicFleetRatios = false;
        config->force.cityFleetRatios = 0;
        config->force.duelOpeningPlan = 0;
        config->force.simpleTechSavings = false;
        bot->configOverride = config;
        second = static_cast<ConstructionYard*>(place(Structure_ConstructionYard, Coord(8, 12)));
        // One local construction patch; the initial outlying suppliers have
        // their own founded plots, not speculative free building sites.
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x)
            if (!currentGameMap->getTile(x, y)->hasAStructure())
                currentGameMap->getTile(x, y)->setType(x >= 5 && x <= 20 && y >= 5 && y <= 20
                    ? Terrain_Rock : Terrain_Sand);
        for (int i = 0; i < 12; ++i) place(Structure_WindTrap, Coord(26 + (i % 6) * 4, 3 + (i / 6) * 4));
        place(Structure_Radar, Coord(26, 12));
        place(Structure_LightFactory, Coord(30, 12));
        place(Structure_HeavyFactory, Coord(34, 12));
        place(Structure_HighTechFactory, Coord(38, 12));
        place(Structure_StarPort, Coord(42, 12));
        place(Structure_IX, Coord(26, 17));
        place(Structure_Palace, Coord(30, 17));
        place(Structure_Refinery, Coord(34, 17));
        for (int i = 0; i < 8; ++i) place(Structure_RocketTurret, Coord(26 + i * 3, 22));
        if (withBay) bay = static_cast<RepairYard*>(place(Structure_RepairYard, Coord(12, 12)));
        owner->capacity = 200000;
        owner->getChoam().availableItems.clear();
        cash();
        for (auto* structure : structureList)
            if (auto* builder = dynamic_cast<BuilderBase*>(structure); builder && builder->getOwner() == owner) {
                builder->curUpgradeLev = builder->getMaxUpgradeLevel();
                builder->updateBuildList();
            }
        game->gameCycleCount = MILLI2CYCLES(300000);
        if (game->isCitySimEnabled()) {
            for (int i = 0; i < 8; ++i)
                static_cast<ZoneStructure*>(place(Structure_ZoneResidential, Coord(26 + i * 3, 28)))->setStartingLevel(3);
            for (int i = 0; i < 4; ++i)
                static_cast<ZoneStructure*>(place(Structure_ZoneCommercial, Coord(26 + i * 3, 32)))->setStartingLevel(3);
            for (int i = 0; i < 2; ++i)
                static_cast<ZoneStructure*>(place(Structure_ZoneIndustrial, Coord(40 + i * 3, 32)))->setStartingLevel(3);
            for (int y = 26; y < 36; ++y) for (int x = 25; x < 52; ++x)
                if (!currentGameMap->getTile(x, y)->hasAStructure())
                    owner->placeStructure(NONE_ID, Structure_Road, x, y, true, true);
            for (auto* structure : structureList)
                if (structure->getOwner() == owner && !dynamic_cast<ZoneStructure*>(structure))
                    structure->setCityOccupancy(DuneCity::getStructureMaxLevel(structure->getItemID()));
            auto* sim = game->getCitySimulation();
            sim->reconcileLoadedMapState(game->getGameCycleCount());
            auto& state = sim->houseState_[owner->getHouseID()];
            state.prevResPop = state.resPop; state.prevComPop = state.comPop; state.prevIndPop = state.indPop;
            owner->updateBuildLists();
        }
        bot->squadRallyLocation = Coord(18, 37);
        bot->squadRetreatLocation = Coord(18, 35);
        bot->militaryValueLimit = 1;
        bot->harvesterLimit = 1;
        bot->lastCalculatedSpice = 0;
        bot->buildTimer = bot->attackTimer = INT_MAX;
        for (int item = ItemID_FirstID; item <= ItemID_LastID; ++item)
            bot->initialItemCount[item] = owner->getNumItems(item);
    }
    ~World() {
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr; pLocalHouse = nullptr;
    }
    StructureBase* place(Uint32 item, Coord at) {
        const Coord size = getStructureSize(item);
        for (int y = at.y; y < at.y + size.y; ++y) for (int x = at.x; x < at.x + size.x; ++x)
            currentGameMap->getTile(x, y)->setType(Terrain_Rock);
        auto* result = owner->placeStructure(NONE_ID, item, at.x, at.y, true, true);
        require(result, "Initial repair scenario asset could not be placed");
        return result;
    }
    GroundUnit* tank(Coord at, bool damaged) {
        auto* unit = dynamic_cast<GroundUnit*>(owner->createUnit(Unit_Tank, true));
        require(unit, "Could not create repair fixture vehicle");
        unit->deploy(at);
        unit->setVisible(VIS_ALL, true);
        unit->setForced(false);
        unit->doSetAttackMode(GUARD);
        if (damaged) unit->setHealth(unit->getMaxHealth() / 4);
        return unit;
    }
    void cash() { owner->startingCredits = 100000; owner->storedCredits = owner->cityCredits = 0; }
    int queued(Uint32 item = Structure_RepairYard) const {
        int count = 0;
        for (const auto* structure : structureList)
            if (const auto* builder = dynamic_cast<const BuilderBase*>(structure); builder && builder->getOwner() == owner)
                for (const auto& offer : builder->getBuildList())
                    if (offer.itemID == item) count += offer.num;
        return count;
    }
    QuantBot::BuildPass plan() {
        QuantBot::BuildPass pass(bot->militaryUnitValue());
        // Orders consume the completed build plan, not just inventory/budget.
        // In particular chooseCapitalPlan establishes the selected candidate
        // (or -1); production acceptance consults it even for a repair order.
        bot->resetBuildPass();
        bot->planBuildInventory(pass);
        bot->planBuildBudget(pass);
        bot->planCityState(pass);
        bot->planStrategicReserve(pass);
        bot->planSpiceEconomy(pass);
        bot->planPowerAndYards(pass);
        bot->planCityPopulation(pass);
        bot->planCivicNeedAndTrips(pass);
        bot->traceBuildState(pass);
        bot->planUnitMix(pass);
        bot->planCapitalForecast(pass);
        bot->planCityBootstrap(pass);
        bot->planOpeningProduction(pass);
        bot->planDefenceCoverage(pass);
        bot->planVanillaCoreDefence(pass);
        bot->planCityResearchOrder(pass);
        bot->planTechSavings(pass);
        bot->planCapitalCandidates(pass);
        bot->planVanillaBoldCapital(pass);
        bot->chooseCapitalPlan(pass);
        if (!pass.vanillaBoomArmy) bot->planTechResearch(pass);
        return pass;
    }
    void contain(GroundUnit* unit) {
        require(bay && bay->isFree(), "Repair workload overbooks its scenario bay");
        unit->setTarget(bay);
        unit->setGettingRepaired();
        require(bay->getContainedRepairUnit() == unit && !unit->isActive(), "Scenario job was not contained by the engine");
    }
    void workload() {
        for (int y = 7; y <= 19 && jobs.size() < 80; ++y)
            for (int x = 7; x <= 19 && jobs.size() < 80; ++x)
                if (!currentGameMap->getTile(x, y)->hasAGroundObject())
                    jobs.push_back(tank(Coord(x, y), true));
        contain(jobs.front());
    }
    void tick() {
        // Hold unrelated army controllers passive; repair, payment, completion
        // and release still run through the ordinary game update.
        const auto role = bot->role;
        bot->role = AIRole::Off;
        game->negotiatedBudget = 15000;
        game->carryOverTokens = 0;
        game->updateGameState();
        bot->role = role;
        require(!game->finished, "Passive repair fixture ended before its bound");
        if (bay && bay->isFree()) {
            for (auto* unit : jobs) if (unit->isActive() && !unit->getTarget() && unit->getHealth() < unit->getMaxHealth()) {
                ++completed;
                contain(unit);
                break;
            }
        }
    }
    void roundtrip(bool observer) {
        const auto anchor = bot->repairPressureYard, since = bot->repairPressureSince;
        const auto firstID = first->getObjectID(), secondID = second->getObjectID(), bayID = bay->getObjectID();
        const auto cycle = game->getGameCycleCount();
        const std::string path = observer ? "repair-observer.sav" : "repair-pressure.sav";
        require(game->saveGame(path), "Cannot save the running repair pressure window");
        const std::string runtime = observer ? game->saveObserverRuntime() : std::string{};
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr; pLocalHouse = nullptr;
        game = std::make_unique<Game>(); currentGame = game.get();
        game->bReplay = true; game->initGame(GameInitSettings(path)); game->bReplay = false;
        game->initializeGameLoop(); game->bPause = false;
        if (observer) game->loadObserverRuntime(runtime);
        owner = game->getHouse(HOUSE_HARKONNEN); bot = nullptr;
        for (const auto& player : owner->getPlayerList())
            if (auto* candidate = dynamic_cast<QuantBot*>(player.get())) bot = candidate;
        require(bot && bot->repairPressureYard == anchor && bot->repairPressureSince == since
            && game->getGameCycleCount() == cycle, "Save/checkpoint lost the exact repair pressure window");
        bot->configOverride = config;
        first = dynamic_cast<ConstructionYard*>(game->getObjectManager().getObject(firstID));
        second = dynamic_cast<ConstructionYard*>(game->getObjectManager().getObject(secondID));
        bay = dynamic_cast<RepairYard*>(game->getObjectManager().getObject(bayID));
        require(first && second && bay, "Save/checkpoint lost repair or construction ownership");
        jobs.clear();
        for (auto* unit : unitList)
            if (unit->getOwner() == owner && unit->getItemID() == Unit_Tank) jobs.push_back(static_cast<GroundUnit*>(unit));
    }
};

// These checks compare the diagnostic ledger with real credit/HP/container
// mutations. A stopped logger is unknown, never a synthetic all-zero ledger.
struct Quality {
    std::string fields;
    explicit Quality(World& world, int house)
        : fields(AITelemetry::log().repairQuality(house, world.game->getGameCycleCount()).json()) {}
    explicit Quality(World& world) : Quality(world, world.owner->getHouseID()) {}
    int64_t value(const char* key) const {
        const std::string token = std::string("\"") + key + "\":";
        const auto begin = fields.find(token);
        require(begin != std::string::npos, "Repair quality omitted a required measured field");
        size_t consumed = 0;
        const auto result = std::stoll(fields.substr(begin + token.size()), &consumed);
        const auto end = begin + token.size() + consumed;
        require(consumed && end < fields.size() && (fields[end] == ',' || fields[end] == '}'),
            "Repair quality did not expose an exact integer");
        return result;
    }
};

void expectQuality(World& world, bool logging,
        std::initializer_list<std::pair<const char*, int64_t>> expected) {
    const Quality quality(world);
    if (!logging) {
        require(quality.fields == "{}", "Disabled repair measurement fabricated observed zero counters");
        return;
    }
    require(quality.value("version") == 1 && quality.value("capture_complete") == 0
        && quality.value("end_cycle") == world.game->getGameCycleCount(),
        "Live repair quality has the wrong version, coverage or clock");
    const auto live = quality.value("live_cycles_total");
    const auto free = quality.value("free_cycles_total");
    const auto damaged = quality.value("damaged_cycles_total");
    const auto returning = quality.value("full_return_cycles_total");
    const auto unfunded = quality.value("unfunded_cycles_total");
    require(live >= 0 && free >= 0 && damaged >= 0 && returning >= 0 && unfunded >= 0
        && live == free + damaged + returning && unfunded <= damaged,
        "Repair bay cycles lost their integer partition or unfunded subset");
    for (const auto& field : expected) {
        const auto actual = quality.value(field.first);
        if (actual != field.second)
            SDL_Log("REPAIR_QUALITY_FIELD: key=%s expected=%lld actual=%lld quality=%s",
                field.first, static_cast<long long>(field.second), static_cast<long long>(actual), quality.fields.c_str());
        require(actual == field.second, "Repair quality differs from its real consumer effect");
    }
}

FixPoint credits(const World& world) {
    return world.owner->getStartingCredits() + world.owner->getStoredCredits() + world.owner->getCityCredits();
}

void bayStep(World& world) {
    world.bay->updateStructureSpecificStuff();
    ++world.game->gameCycleCount;
}

std::vector<uint64_t> measuredConsumers(const char* mod, bool logging) {
    World world(mod);
    require(AITelemetry::log().enabled(), "Repair measurement fixture requires its existing telemetry-enabled runner");
    if (logging) {
        AITelemetry::log().beginRepairCapture(world.game->getGameCycleCount(), false);
        world.bay->beginRepairTelemetry(true);
    } else {
        AITelemetry::log().stop();
    }
    const auto start = world.game->getGameCycleCount();
    expectQuality(world, logging, {{"start_cycle", start}, {"left_censored", 0},
        {"baseline_bays", 1}, {"baseline_pending_paid_raw", 0}, {"live_bays", 1},
        {"capital_paid_raw_total", 0}, {"health_restored_raw_total", 0}, {"service_paid_raw_total", 0}});
    std::vector<uint64_t> states;
    auto snapshot = [&] { states.push_back(ProbeDigest::deep(*world.game)); };
    snapshot();

    // Pay a genuinely partial, fractional construction order, then lose most
    // refund storage. Requested sunk cost must not masquerade as accepted cash.
    world.owner->startingCredits = world.owner->cityCredits = 0;
    world.owner->storedCredits = 100;
    world.owner->capacity = 100;
    world.first->setHealth(FixPoint(world.first->getMaxHealth()) * 7 / 8);
    world.first->doProduceItem(Structure_RepairYard);
    require(world.queued() == 1, "Measured capital order never reached the real queue");
    const auto beforeProduction = credits(world);
    for (int i = 0; i < 256 && world.first->getProductionProgress() < FixPoint(5) / 4; ++i)
        world.first->updateProductionProgress();
    const auto paid = world.first->getProductionProgress();
    require(paid >= FixPoint(5) / 4 && paid < world.first->getCurrentProductionPrice()
        && paid.getRawValue() % FixPoint(1).getRawValue() != 0
        && beforeProduction - credits(world) == paid
        && world.first->getProductionProgressFromStartingCredits() == 0,
        "Repair capital fixture did not make a real exact Q32 partial payment");
    expectQuality(world, logging, {{"capital_paid_raw_total", paid.getRawValue()},
        {"capital_refund_requested_raw_total", 0}, {"capital_refund_actual_raw_total", 0}});
    snapshot();
    states.push_back(static_cast<uint64_t>(paid.getRawValue()));
    world.owner->capacity = 1;
    world.owner->storedCredits = FixPoint(7) / 8;
    const auto accepted = world.owner->addCredits(FixPoint(1) / 4, false);
    require(accepted == FixPoint(1) / 8 && world.owner->getStoredCredits() == 1,
        "House credit receipt returned the request instead of actual fractional storage acceptance");
    world.owner->storedCredits = FixPoint(7) / 8;
    const auto beforeRefund = credits(world);
    world.first->doCancelItem(Structure_RepairYard);
    require(world.first->getProductionProgress() == 0 && world.queued() == 0
        && credits(world) - beforeRefund == FixPoint(1) / 8 && world.owner->getStartingCredits() == 0,
        "Partial repair cancellation changed its actual capacity-limited refund");
    expectQuality(world, logging, {{"capital_paid_raw_total", paid.getRawValue()},
        {"capital_refund_requested_raw_total", paid.getRawValue()},
        {"capital_refund_actual_raw_total", (FixPoint(1) / 8).getRawValue()}});
    snapshot();
    world.owner->capacity = 200000;
    world.cash();

    auto* ground = world.tank(Coord(10, 15), false);
    ground->setHealth(FixPoint(ground->getMaxHealth()) - FixPoint(1) / 8);
    world.contain(ground);
    auto* carrier = dynamic_cast<Carryall*>(world.owner->createUnit(Unit_Carryall, true));
    require(carrier, "Measured repair return could not create its real carrier");
    carrier->deploy(Coord(45, 45));
    carrier->setTarget(world.bay);
    ground->bookCarrier(carrier);
    const auto beforeService = credits(world);
    bayStep(world);
    require(ground->getHealth() == ground->getMaxHealth()
        && beforeService - credits(world) == UNIT_REPAIRCOST,
        "Fractional HP completion failed to saturate or changed the ordinary one-step repair price");
    int64_t restored = (FixPoint(1) / 8).getRawValue();
    int64_t servicePaid = (beforeService - credits(world)).getRawValue();
    for (int i = 0; i < 8; ++i) bayStep(world);
    require(world.bay->getContainedRepairUnit() == ground && !ground->isActive()
        && credits(world) == beforeService - UNIT_REPAIRCOST,
        "Completed carrier wait released early or charged additional repair ticks");
    expectQuality(world, logging, {{"health_restored_raw_total", restored}, {"service_paid_raw_total", servicePaid},
        {"completed_total", 1}, {"released_ground_total", 0}, {"handoff_carrier_total", 0},
        {"delivered_total", 0}, {"useful_delivered_total", 0},
        {"right_censored_jobs", 1}, {"right_censored_returns", 0},
        {"live_cycles_total", world.game->getGameCycleCount() - start}});
    if (logging) require(Quality(world).value("full_return_cycles_total") >= 8,
        "Completed-return wait did not contribute real bay cycle exposure");
    snapshot();
    carrier->setTarget(nullptr);
    ground->bookCarrier(nullptr);
    ground->setGuardPoint(Coord(10, 15));
    bayStep(world);
    require(world.bay->isFree() && ground->isActive() && !ground->isPickedUp()
        && ground->getTarget() == nullptr, "Measured ground repair release did not physically deploy its customer");
    expectQuality(world, logging, {{"completed_total", 1}, {"released_ground_total", 1},
        {"handoff_carrier_total", 0}, {"delivered_total", 1}, {"useful_delivered_total", 1}});
    snapshot();

    // An unfunded job exposes blocked damaged-bay time, not repair payments or
    // completions. Two funded healing steps then return it through real flight.
    auto* flown = world.tank(Coord(11, 16), false);
    flown->setHealth(FixPoint(flown->getMaxHealth()) - FixPoint(9) / 8);
    flown->setGuardPoint(Coord(20, 35));
    world.contain(flown);
    const auto damagedHealth = flown->getHealth();
    world.owner->startingCredits = world.owner->storedCredits = world.owner->cityCredits = 0;
    const auto unfundedBefore = logging ? Quality(world).value("unfunded_cycles_total") : int64_t(-1);
    for (int i = 0; i < 4; ++i) bayStep(world);
    require(flown->getHealth() == damagedHealth && credits(world) == 0,
        "Unfunded real repair made unpaid health progress");
    expectQuality(world, logging, {{"health_restored_raw_total", restored}, {"service_paid_raw_total", servicePaid},
        {"completed_total", 1}});
    if (logging) require(Quality(world).value("unfunded_cycles_total") - unfundedBefore == 4,
        "Unfunded bay measurement counted credit-scaled values instead of four cycles");
    snapshot();
    world.cash();
    carrier->setTarget(world.bay);
    flown->bookCarrier(carrier);
    const auto beforeFlightRepair = credits(world);
    bayStep(world);
    require(flown->getHealth() == FixPoint(flown->getMaxHealth()) - FixPoint(1) / 8,
        "Measured service skipped the real intermediate fractional HP state");
    bayStep(world);
    restored += (FixPoint(9) / 8).getRawValue();
    servicePaid += (beforeFlightRepair - credits(world)).getRawValue();
    require(flown->getHealth() == flown->getMaxHealth()
        && beforeFlightRepair - credits(world) == UNIT_REPAIRCOST * 2,
        "Fractional finishing step did not consume exactly two ordinary repair payments");
    const auto returnBound = world.game->getGameCycleCount() + MILLI2CYCLES(60000);
    while (!carrier->hasCargo() && world.game->getGameCycleCount() < returnBound) world.tick();
    require(carrier->hasCargo() && world.bay->isFree() && flown->isPickedUp() && !flown->isActive(),
        "Repaired customer never made the real bay-to-carrier handoff");
    expectQuality(world, logging, {{"health_restored_raw_total", restored}, {"service_paid_raw_total", servicePaid},
        {"completed_total", 2}, {"released_ground_total", 1}, {"handoff_carrier_total", 1},
        {"delivered_total", 1}, {"useful_delivered_total", 1},
        {"right_censored_jobs", 0}, {"right_censored_returns", 1}});
    snapshot();
    while (!flown->isActive() && world.game->getGameCycleCount() < returnBound) world.tick();
    require(flown->isActive() && !flown->isPickedUp() && !carrier->hasCargo()
        && flown->getHealth() == flown->getMaxHealth()
        && blockDistance(flown->getLocation(), Coord(20, 35)) <= 2,
        "Carrier handoff was mistaken for final physical ground delivery");
    expectQuality(world, logging, {{"completed_total", 2}, {"released_ground_total", 1},
        {"handoff_carrier_total", 1}, {"delivered_total", 2}, {"useful_delivered_total", 2},
        {"right_censored_jobs", 0}, {"right_censored_returns", 0}});
    if (logging) require(Quality(world).value("free_cycles_total") > 0,
        "A physically released bay accumulated no free-cycle exposure during its customer's return flight");
    snapshot();

    auto* stale = world.tank(Coord(11, 16), true);
    world.contain(stale);
    const auto staleID = stale->getObjectID();
    stale->destroy();
    bayStep(world);
    require(world.bay->isFree() && !world.game->getObjectManager().getObject(staleID),
        "Destroyed repair customer left stale containment");
    expectQuality(world, logging, {{"aborted_total", 1}, {"completed_total", 2}, {"delivered_total", 2}});
    snapshot();

    auto* lostReturn = world.tank(Coord(11, 16), false);
    lostReturn->setHealth(FixPoint(lostReturn->getMaxHealth()) - FixPoint(1) / 8);
    lostReturn->setGuardPoint(Coord(20, 35));
    world.contain(lostReturn);
    carrier->setTarget(world.bay);
    lostReturn->bookCarrier(carrier);
    const auto beforeLostRepair = credits(world);
    bayStep(world);
    restored += (FixPoint(1) / 8).getRawValue();
    servicePaid += (beforeLostRepair - credits(world)).getRawValue();
    const auto lostBound = world.game->getGameCycleCount() + MILLI2CYCLES(60000);
    while (!carrier->hasCargo() && world.game->getGameCycleCount() < lostBound) world.tick();
    require(carrier->hasCargo() && lostReturn->isPickedUp() && world.bay->isFree(),
        "Return-abort fixture never reached a real carrier handoff");
    const auto lostID = lostReturn->getObjectID();
    carrier->destroy();
    require(!world.game->getObjectManager().getObject(lostID), "Destroyed carrier retained its repair-return cargo");
    expectQuality(world, logging, {{"health_restored_raw_total", restored}, {"service_paid_raw_total", servicePaid},
        {"completed_total", 3}, {"handoff_carrier_total", 2}, {"delivered_total", 2},
        {"useful_delivered_total", 2}, {"aborted_total", 1}, {"return_aborted_total", 1},
        {"right_censored_jobs", 0}, {"right_censored_returns", 0}});
    snapshot();

    auto* crushed = world.tank(Coord(11, 16), true);
    world.contain(crushed);
    const auto crushedID = crushed->getObjectID();
    world.bay->destroy();
    world.bay = nullptr;
    require(!world.game->getObjectManager().getObject(crushedID), "Destroyed bay retained its contained customer");
    expectQuality(world, logging, {{"live_bays", 0}, {"aborted_total", 2}, {"return_aborted_total", 1}});
    snapshot();

    // Infantry capture replaces the bay and its contained vehicle in the real
    // engine. The old owner's service aborts; no service is invented for captor.
    world.bay = static_cast<RepairYard*>(world.place(Structure_RepairYard, Coord(12, 12)));
    auto* capturedJob = world.tank(Coord(11, 16), true);
    world.contain(capturedJob);
    const auto capturedID = capturedJob->getObjectID();
    const auto capturedHealth = capturedJob->getHealth();
    const auto site = world.bay->getLocation();
    world.bay->setHealth(1);
    auto* enemy = world.game->getHouse(HOUSE_ATREIDES);
    auto* infantry = dynamic_cast<InfantryBase*>(enemy->createUnit(Unit_Soldier, true));
    require(infantry, "Cannot create real repair-bay capture consumer");
    infantry->deploy(site);
    infantry->doCaptureStructure(world.bay);
    infantry->justStoppedMoving = true;
    infantry->checkPos();
    world.bay = nullptr;
    RepairYard* capturedBay = nullptr;
    for (auto* structure : structureList)
        if (structure->getOwner() == enemy && structure->getItemID() == Structure_RepairYard
            && structure->getLocation() == site) capturedBay = static_cast<RepairYard*>(structure);
    require(capturedBay && capturedBay->getContainedRepairUnit()
        && capturedBay->getContainedRepairUnit()->getOwner() == enemy
        && capturedBay->getContainedRepairUnit()->getHealth() == capturedHealth
        && !world.game->getObjectManager().getObject(capturedID),
        "Real infantry capture failed to replace repair ownership and contained customer");
    expectQuality(world, logging, {{"live_bays", 0}, {"aborted_total", 3}, {"return_aborted_total", 1},
        {"completed_total", 3}, {"delivered_total", 2}, {"useful_delivered_total", 2},
        {"health_restored_raw_total", restored}, {"service_paid_raw_total", servicePaid}});
    if (logging) {
        const Quality captor(world, enemy->getHouseID());
        require(captor.value("live_bays") == 1 && captor.value("health_restored_raw_total") == 0
            && captor.value("service_paid_raw_total") == 0 && captor.value("completed_total") == 0
            && captor.value("delivered_total") == 0,
            "Capture reassigned the old owner's observed repair effects to the captor");
    }
    snapshot();
    SDL_Log("REPAIR_CAPACITY_CASE: measurement_consumers logging=%d passed", logging);
    return states;
}

void baselineContinuation(const char* mod) {
    World world(mod);
    require(AITelemetry::log().enabled(), "Repair baseline fixture requires structured capture");
    auto* unit = world.tank(Coord(10, 15), false);
    unit->setHealth(FixPoint(unit->getMaxHealth()) - FixPoint(1) / 8);
    world.contain(unit);
    const auto unitID = unit->getObjectID();
    const auto health = unit->getHealth();
    world.first->doProduceItem(Structure_RepairYard);
    for (int i = 0; i < 3; ++i) world.first->updateProductionProgress();
    const auto pending = world.first->getProductionProgress();
    require(pending > 0 && pending < world.first->getCurrentProductionPrice(),
        "Baseline fixture did not retain real partially paid repair construction");

    // Explicit initial capture, followed by the production Game::initGame load
    // path. Outstanding paid work is a disclosed baseline, not fresh spending.
    auto& log = AITelemetry::log();
    log.beginRepairCapture(world.game->getGameCycleCount(), false);
    world.bay->beginRepairTelemetry(true);
    log.repairCapitalBaseline(world.owner->getHouseID(), world.first->getObjectID(), pending.getRawValue());
    expectQuality(world, true, {{"left_censored", 0}, {"baseline_bays", 1}, {"live_bays", 1},
        {"baseline_pending_paid_raw", pending.getRawValue()}, {"capital_paid_raw_total", 0},
        {"health_restored_raw_total", 0}, {"service_paid_raw_total", 0}, {"completed_total", 0},
        {"released_ground_total", 0}, {"handoff_carrier_total", 0}, {"delivered_total", 0}});
    for (bool observer : {false, true}) {
        world.roundtrip(observer);
        unit = dynamic_cast<GroundUnit*>(world.game->getObjectManager().getObject(unitID));
        require(unit && unit->getHealth() == health && world.bay->getContainedRepairUnit() == unit
            && world.first->getProductionProgress() == pending,
            "Saved repair baseline changed exact HP, paid progress or containment");
        expectQuality(world, true, {{"start_cycle", world.game->getGameCycleCount()}, {"left_censored", 1},
            {"baseline_bays", 1}, {"live_bays", 1}, {"baseline_pending_paid_raw", pending.getRawValue()},
            {"capital_paid_raw_total", 0}, {"capital_refund_requested_raw_total", 0},
            {"capital_refund_actual_raw_total", 0}, {"health_restored_raw_total", 0},
            {"service_paid_raw_total", 0}, {"completed_total", 0}, {"delivered_total", 0},
            {"live_cycles_total", 0}});
    }
    const auto beforeProduction = credits(world);
    world.first->updateProductionProgress();
    const auto newProgress = world.first->getProductionProgress() - pending;
    require(newProgress > 0 && beforeProduction - credits(world) == newProgress,
        "Loaded repair production did not retain its actual credit-funded consumer");
    const auto beforeRepair = credits(world);
    bayStep(world);
    require(unit->getHealth() == unit->getMaxHealth() && beforeRepair - credits(world) == UNIT_REPAIRCOST,
        "Loaded fractional repair failed real completion and payment");
    bayStep(world);
    require(unit->isActive() && world.bay->isFree(), "Loaded repaired unit never reached actual ground deployment");
    const auto refund = world.first->getProductionProgress();
    const auto beforeRefund = credits(world);
    world.first->doCancelItem(Structure_RepairYard);
    require(credits(world) - beforeRefund == refund && world.first->getProductionProgress() == 0,
        "Loaded partial construction refund changed actual accepted cash");
    expectQuality(world, true, {{"left_censored", 1}, {"baseline_pending_paid_raw", pending.getRawValue()},
        {"capital_paid_raw_total", newProgress.getRawValue()}, {"capital_refund_requested_raw_total", refund.getRawValue()},
        {"capital_refund_actual_raw_total", refund.getRawValue()},
        {"health_restored_raw_total", (FixPoint(1) / 8).getRawValue()},
        {"service_paid_raw_total", UNIT_REPAIRCOST.getRawValue()}, {"completed_total", 1},
        {"released_ground_total", 1}, {"delivered_total", 1}});
    SDL_Log("REPAIR_CAPACITY_CASE: initial loaded observer censored_baselines passed");
}

void lethalGroundReturn(const char* mod) {
    World world(mod);
    require(AITelemetry::log().enabled(), "Lethal repair-return fixture requires structured capture");
    AITelemetry::log().beginRepairCapture(world.game->getGameCycleCount(), false);
    world.bay->beginRepairTelemetry(true);
    auto* unit = world.tank(Coord(10, 15), false);
    unit->setHealth(FixPoint(unit->getMaxHealth()) - FixPoint(1) / 8);
    unit->setGuardPoint(Coord(10, 15));
    const auto unitID = unit->getObjectID();
    world.contain(unit);
    const auto beforeRepair = credits(world);
    bayStep(world);
    require(unit->getHealth() == unit->getMaxHealth()
        && beforeRepair - credits(world) == UNIT_REPAIRCOST,
        "Lethal return did not first complete a real paid fractional repair");
    expectQuality(world, true, {{"completed_total", 1}, {"return_attempted_total", 0},
        {"health_restored_raw_total", (FixPoint(1) / 8).getRawValue()},
        {"service_paid_raw_total", UNIT_REPAIRCOST.getRawValue()}});

    // Every immediately accessible bay exit is a real bloom. The normal
    // findDeploySpot consumer chooses the tile and deploy() applies its death.
    const auto origin = world.bay->getLocation();
    const auto size = world.bay->getStructureSize();
    for (int y = origin.y - 1; y <= origin.y + size.y; ++y)
        for (int x = origin.x - 1; x <= origin.x + size.x; ++x)
            if (x < origin.x || x >= origin.x + size.x || y < origin.y || y >= origin.y + size.y) {
                auto* tile = currentGameMap->getTile(x, y);
                require(!tile->hasAGroundObject(), "Lethal repair-return exit ring is not genuinely clear");
                tile->setType(Terrain_SpiceBloom);
            }
    bayStep(world);
    require(world.bay->isFree() && unit->isActive() && !unit->isPickedUp() && unit->getHealth() == 0
        && !currentGameMap->getTile(unit->getLocation())->isSpiceBloom()
        && currentGameMap->getTile(unit->getLocation())->hasSpice(),
        "Real bay ground return did not trigger its lethal spice-bloom deployment");
    expectQuality(world, true, {{"completed_total", 1}, {"return_attempted_total", 1},
        {"return_aborted_total", 1}, {"aborted_total", 0}, {"released_ground_total", 0},
        {"handoff_carrier_total", 0}, {"delivered_total", 0}, {"useful_delivered_total", 0},
        {"right_censored_jobs", 0}, {"right_censored_returns", 0},
        {"health_restored_raw_total", (FixPoint(1) / 8).getRawValue()},
        {"service_paid_raw_total", UNIT_REPAIRCOST.getRawValue()}});
    world.tick();
    require(!world.game->getObjectManager().getObject(unitID),
        "Bloom-killed repair return did not follow the real unit-destruction consumer");
    expectQuality(world, true, {{"return_attempted_total", 1}, {"return_aborted_total", 1},
        {"released_ground_total", 0}, {"handoff_carrier_total", 0}, {"delivered_total", 0},
        {"health_restored_raw_total", (FixPoint(1) / 8).getRawValue()},
        {"service_paid_raw_total", UNIT_REPAIRCOST.getRawValue()}});
    SDL_Log("REPAIR_CAPACITY_CASE: lethal_ground_return attempted=1 aborted=1 delivered=0 passed");
}

void producerSale() {
    require(!currentGame, "Repair producer sale must not replace another live fixture");
    auto& mods = ModManager::instance();
    const auto originalMod = mods.getActiveModName();
    if (!mods.modExists("Tornie") || mods.tornieFamilyNeedsReseed("Tornie")) {
        // Shared vanilla/City rules deliberately omit other content. Install
        // only the checksummed canonical Tornie payload in this runner's own
        // writable profile, through the ordinary managed-mod installer.
        const auto* profileRoot = std::getenv("DUNECITY_USERDIR");
        require(profileRoot && *profileRoot, "Repair sale fixture has no isolated runner profile");
        const auto profile = std::filesystem::path(profileRoot);
        const auto profileMods = profile / "mods";
        const auto destination = std::filesystem::path(mods.getModPath("Tornie"));
        const auto writable = [](const std::filesystem::path& path) {
            return (std::filesystem::status(path).permissions() & std::filesystem::perms::owner_write)
                != std::filesystem::perms::none;
        };
        require(!std::filesystem::is_symlink(profile) && !std::filesystem::is_symlink(profileMods)
            && !std::filesystem::is_symlink(destination)
            && std::filesystem::weakly_canonical(destination).parent_path() == std::filesystem::canonical(profileMods)
            && writable(profile) && writable(profileMods)
            && (!std::filesystem::exists(destination) || writable(destination)),
            "Repair sale fixture refuses to seed linked, read-only or non-profile content");
        mods.seedTornieFamilyFromDefaults("Tornie");
    }
    {
        World world("Tornie");
        require(mods.getActiveModName() == "Tornie" && mods.isTornieFamilyContentActive()
            && world.game->getGameInitSettings().getGameOptions().sellingBuildings && world.first->canSell(),
            "Repair producer sale did not activate canonical sales-enabled Tornie rules");
        require(AITelemetry::log().enabled(), "Repair producer sale requires structured capture");
        AITelemetry::log().beginRepairCapture(world.game->getGameCycleCount(), false);
        world.bay->beginRepairTelemetry(true);
        world.owner->startingCredits = world.owner->cityCredits = 0;
        world.owner->storedCredits = 100;
        world.owner->capacity = 100;
        auto partialOrder = [&](BuilderBase* builder) {
            builder->setHealth(FixPoint(builder->getMaxHealth()) * 7 / 8);
            builder->doProduceItem(Structure_RepairYard);
            require(builder->getCurrentProducedItem() == Structure_RepairYard,
                "Sale/cancel control did not accept real repair construction");
            const auto before = credits(world);
            for (int i = 0; i < 256 && builder->getProductionProgress() < FixPoint(5) / 4; ++i)
                builder->updateProductionProgress();
            const auto paid = builder->getProductionProgress();
            require(paid >= FixPoint(5) / 4 && paid < builder->getCurrentProductionPrice()
                && paid.getRawValue() % FixPoint(1).getRawValue() != 0
                && before - credits(world) == paid && builder->getProductionProgressFromStartingCredits() == 0,
                "Producer sale fixture did not make its exact earned-funded partial Q32 payment");
            return paid;
        };
        const auto cancelled = partialOrder(world.second);
        const auto beforeCancel = credits(world);
        world.second->doCancelItem(Structure_RepairYard);
        require(credits(world) - beforeCancel == cancelled && world.second->getProductionProgress() == 0,
            "Sale fixture's ordinary cancellation changed its actual refund");
        expectQuality(world, true, {{"capital_paid_raw_total", cancelled.getRawValue()},
            {"capital_refund_requested_raw_total", cancelled.getRawValue()},
            {"capital_refund_actual_raw_total", cancelled.getRawValue()}});

        const auto soldProgress = partialOrder(world.first);
        const auto producerID = world.first->getObjectID();
        const auto yards = world.owner->getNumItems(Structure_ConstructionYard);
        const int saleValue = world.first->getSaleRefund();
        require(saleValue > 0 && world.queued() == 1, "Producer sale lacks its pending order or actual building value");
        world.owner->capacity = 1;
        world.owner->storedCredits = FixPoint(7) / 8;
        const auto beforeSale = credits(world);
        world.first->doSell();
        world.first = nullptr;
        require(!world.game->getObjectManager().getObject(producerID)
            && world.owner->getNumItems(Structure_ConstructionYard) == yards - 1 && world.queued() == 0
            && world.owner->getStoredCredits() == 1 && world.owner->getStartingCredits() == FixPoint(saleValue)
            && credits(world) - beforeSale == FixPoint(saleValue) + FixPoint(1) / 8,
            "Real producer SELL lost or duplicated pending refund, building value or storage-clipped acceptance");
        expectQuality(world, true, {{"capital_paid_raw_total", (cancelled + soldProgress).getRawValue()},
            {"capital_refund_requested_raw_total", (cancelled + soldProgress).getRawValue()},
            {"capital_refund_actual_raw_total", (cancelled + FixPoint(1) / 8).getRawValue()},
            {"return_attempted_total", 0}, {"health_restored_raw_total", 0}, {"service_paid_raw_total", 0}});
        SDL_Log("REPAIR_CAPACITY_CASE: producer_sell mod=Tornie requested_raw=%lld accepted_raw=%lld sale_value=%d passed",
            static_cast<long long>(soldProgress.getRawValue()), static_cast<long long>((FixPoint(1) / 8).getRawValue()),
            saleValue);
    }
    require(mods.setActiveMod(originalMod), "Repair sale fixture failed to restore its original active mod");
}

void idleAndSelectors(const char* mod) {
    World world(mod);
    for (int i = 0; i < 175; ++i) world.tank(Coord(5 + i % 25, 30 + i / 25), false);
    for (int i = 0; i < 3; ++i) {
        world.bot->build(world.bot->militaryUnitValue());
        require(world.queued() == 0, "A healthy 175-vehicle fleet bought repeat repair yards");
    }
    auto pass = world.plan();
    require(pass.repairTarget == 1 && pass.observedBusyRepairBays == 0, "Idle repair capacity was treated as service pressure");
    for (bool funded : {false, true}) {
        pass.fundedCityProduction = funded;
        QuantBot::BuilderTurn turn(pass.money); turn.pBuilder = world.first; turn.pStructure = world.first;
        QuantBot::YardTurn choice; choice.itemID = Structure_RepairYard; choice.structureRule = "probe_alternate_selector";
        world.bot->vetYardChoice(pass, turn, choice);
        require(choice.itemID == NONE_ID, "An alternate/funded selector bypassed repeat repair admission");
        require(!world.bot->produceItemWithLogging(pass, turn, Structure_RepairYard, __LINE__, "probe_direct"),
            "Direct production bypassed the common repair policy");
    }
    // Explicit OFF remains the original ratio policy in this very same binary.
    world.config->force.repairCapacityDemand = false;
    auto control = world.plan();
    require(!control.repairCapacityDemand && control.repairBaseline > 1, "OFF did not preserve the shipped large-fleet policy");
    SDL_Log("REPAIR_CAPACITY_CASE: idle175 selectors off_contract passed");
}

void backlogAndContinuation(const char* mod) {
    World world(mod); world.workload();
    // Poll the fixture about once per simulated second, independently of the bot's private scheduler.
    constexpr int planPollCycles = MILLI2CYCLES(1000);
    auto start = world.plan();
    require(start.repairTarget == 1, "Initial repair burst expanded before the sustained window");
    for (int i = 0; i < MILLI2CYCLES(10000); ++i) { world.tick(); if (i % planPollCycles == 0) world.plan(); }
    world.roundtrip(false);
    world.roundtrip(true);
    const auto oldSince = world.bot->repairPressureSince;
    while (world.game->getGameCycleCount() < oldSince + MILLI2CYCLES(30000)) {
        world.tick();
        if (world.game->getGameCycleCount() % planPollCycles == 0) world.plan();
    }
    auto pass = world.plan();
    require(world.completed > 0 && pass.repairTarget == 2, "Saved persistent repair demand failed bounded capacity admission");
    QuantBot::BuilderTurn first(pass.money); first.pBuilder = world.first; first.pStructure = world.first;
    require(world.bot->queueFoundedStructure(pass, first, Structure_RepairYard, __LINE__, "probe_backlog"),
        "Persistent reachable backlog could not queue useful local capacity");
    require(world.queued() == 1, "Useful expansion did not reach the real construction queue");
    const auto reserved = world.bot->reservedStructures.find(world.first->getObjectID());
    require(reserved != world.bot->reservedStructures.end(), "Accepted repair capacity has no reserved plot");
    const Coord expansionSite = reserved->second.location;
    const Coord repairSize = getStructureSize(Structure_RepairYard);
    require(world.bot->repairPlacementAccess.waitingFor(expansionSite.x, expansionSite.y, repairSize.x, repairSize.y) >= 2,
        "Accepted repair reservation does not connect the waiting local workload");
    // Deliberately do not advance the pass count: the execution layer must
    // revalidate accepted queues independently of a stale selector snapshot.
    QuantBot::BuilderTurn second(pass.money); second.pBuilder = world.second; second.pStructure = world.second;
    require(!world.bot->produceItemWithLogging(pass, second, Structure_RepairYard, __LINE__, "probe_parallel"),
        "A parallel CY duplicated the same demand through stale accounting");
    require(world.queued() == 1, "Parallel repair admission accepted multiple bays");
    auto pending = world.plan();
    require(pending.itemCount[Structure_RepairYard] == 2 && pending.repairTarget == 1
        && !world.bot->repairYardAdmitted(pending), "Pending repair capacity did not consume the demand");
    RepairYard* expansion = nullptr;
    const Uint32 placementBound = world.game->getGameCycleCount() + MILLI2CYCLES(180000);
    while (!expansion && world.game->getGameCycleCount() < placementBound) {
        world.tick();
        if (world.game->getGameCycleCount() % planPollCycles == 0)
            world.bot->build(world.bot->militaryUnitValue());
        for (auto* structure : structureList)
            if (structure->getOwner() == world.owner && structure->getItemID() == Structure_RepairYard
                && structure != world.bay) expansion = static_cast<RepairYard*>(structure);
    }
    // Own traffic can invalidate a reservation while the paid structure is
    // building. The normal yard consumer replans it; the runner verifies the
    // accepted builder's actual placement receipt at this resulting site.
    require(expansion && world.owner->getNumItems(Structure_RepairYard) == 2 && world.queued() == 0
        && !world.bot->reservedStructures.count(world.first->getObjectID()),
        "Accepted repair order was not consumed into exactly one owned expansion within the bound");
    SDL_Log("REPAIR_CAPACITY_PLACEMENT: builder=%u item=%u cycle=%u initial_x=%d initial_y=%d x=%d y=%d",
        world.first->getObjectID(), static_cast<unsigned>(Structure_RepairYard), world.game->getGameCycleCount(),
        expansionSite.x, expansionSite.y, expansion->getX(), expansion->getY());
    const auto* capital = AITelemetry::log().repairBay(world.owner->getHouseID(), expansion->getObjectID(),
        world.game->getGameCycleCount(), false);
    require(capital && capital->builder == world.first->getObjectID()
        && capital->capitalProgressRaw > 0 && capital->capitalBaselineRaw == 0
        && capital->capitalProgressRaw == Quality(world).value("capital_paid_raw_total")
        && capital->counters.values[static_cast<size_t>(AITelemetry::RepairMetric::CapitalPaidRaw)] == capital->capitalProgressRaw,
        "Actual repair placement lost its accepted builder, exact paid capital or counted the house debit twice");
    GroundUnit* next = nullptr;
    FixPoint distance = 0;
    for (auto* unit : world.jobs) {
        if (!unit->isActive() || unit->getHealth() >= unit->getMaxHealth() || unit->getTarget()) continue;
        const auto candidate = blockDistance(unit->getLocation(), expansion->getClosestPoint(unit->getLocation()));
        if (!next || candidate < distance) { distance = candidate; next = unit; }
    }
    require(next, "Backlog fixture exhausted its repair jobs before useful capacity arrived");
    // The real engine must be able to walk a waiting customer into the newly
    // constructed bay, heal it and release it. No pathfinder result is mocked.
    next->setGuardPoint(next->getLocation());
    next->doMove2Object(expansion);
    const Uint32 serviceBound = world.game->getGameCycleCount() + MILLI2CYCLES(60000);
    bool entered = false;
    while (world.game->getGameCycleCount() < serviceBound) {
        world.tick();
        entered |= expansion->getContainedRepairUnit() == next;
        if (entered && next->isActive() && next->getHealth() == next->getMaxHealth()) break;
    }
    require(entered && next->isActive() && next->getHealth() == next->getMaxHealth(),
        "The expanded bay could not complete and release a real reachable backlog job");
    SDL_Log("REPAIR_CAPACITY_CASE: paid_backlog saved observer parallel pending built_served passed completed=%d", world.completed);
}

void blockedAndFirst(const char* mod) {
    {
        World world(mod, false);
        for (auto* structure : structureList)
            if (structure->getOwner() == world.owner && structure->getItemID() == Structure_Radar) {
                structure->destroy();
                break;
            }
        world.owner->updateBuildLists();
        require(!world.bot->campaignAvailableToBuild(world.first, Structure_RepairYard),
            "First repair prerequisite fixture still permits a yard without Radar");
        auto pass = world.plan();
        QuantBot::BuilderTurn turn(pass.money); turn.pBuilder = world.first; turn.pStructure = world.first;
        require(world.bot->queueFoundedStructure(pass, turn, Structure_Radar, __LINE__, "probe_repair_prerequisite")
            && world.queued(Structure_Radar) == 1,
            "Demand gating blocked the first repair yard's real technology prerequisite");
    }
    {
        World world(mod); world.workload();
        for (size_t i = 1; i < world.jobs.size(); ++i) {
            auto* unit = world.jobs[i];
            if (i > 2) { unit->setHealth(unit->getMaxHealth()); continue; }
            auto* carrier = dynamic_cast<Carryall*>(world.owner->createUnit(Unit_Carryall, true));
            require(carrier, "Cannot seed a transport for repair backlog control");
            carrier->deploy(Coord(45, 45 + int(i)));
            unit->setTarget(world.bay);
            require(unit->requestCarryall(), "Transport control did not acquire a genuine pickup booking");
        }
        auto* stranded = world.tank(Coord(45, 40), true);
        stranded->setTarget(world.bay);
        require(!stranded->requestCarryall(), "Blocked transport control unexpectedly acquired a free aircraft");
        auto blocked = world.plan();
        require(blocked.waitingLocalRepairVehicles == 0 && blocked.inFlightRepairVehicles >= 2
            && blocked.transportBlockedRepairVehicles >= 1 && blocked.repairTarget == 1,
            "Transport-blocked repair trips manufactured repeat demand");
        const auto* occupant = world.bay->getContainedRepairUnit();
        require(occupant, "Missing completed-return scenario occupant");
        const_cast<UnitBase*>(occupant)->setHealth(occupant->getMaxHealth());
        auto* returnFlight = dynamic_cast<Carryall*>(world.owner->createUnit(Unit_Carryall, true));
        require(returnFlight, "Cannot seed the completed repair return flight");
        returnFlight->deploy(Coord(45, 49));
        returnFlight->setTarget(world.bay);
        static_cast<GroundUnit*>(const_cast<UnitBase*>(occupant))->bookCarrier(returnFlight);
        auto completed = world.plan();
        require(completed.observedBusyRepairBays == 0 && completed.completedRepairReturns == 1
            && completed.repairTarget == 1, "A completed repair awaiting release counted as repair occupancy");
    }
    {
        World world(mod); world.workload();
        for (auto* unit : world.jobs) if (unit->isActive()) { unit->setTarget(nullptr); unit->setForced(false); }
        // A solid mountain ring separates the waiting jobs from the bay.
        for (int x = 11; x <= 14; ++x) for (int y = 11; y <= 14; ++y)
            if (x == 11 || x == 14 || y == 11 || y == 14) currentGameMap->getTile(x, y)->setType(Terrain_Mountain);
        auto blocked = world.plan();
        require(blocked.waitingLocalRepairVehicles == 0 && blocked.repairTarget == 1,
            "An access failure manufactured repeat repair pressure");
    }
    {
        World world(mod, false);
        auto pass = world.plan();
        QuantBot::BuilderTurn turn(pass.money); turn.pBuilder = world.first; turn.pStructure = world.first;
        require(world.bot->queueFoundedStructure(pass, turn, Structure_RepairYard, __LINE__, "probe_first_technology"),
            "Demand gating removed the useful first repair/technology prerequisite");
        require(world.queued() == 1, "First prerequisite was not accepted exactly once");
        auto pending = world.plan();
        require(!world.bot->repairYardAdmitted(pending), "Pending first repair prerequisite was duplicated");
        world.bot->difficulty = QuantBot::Difficulty::Medium;
        world.bot->initialItemCount[Structure_RepairYard] = 0;
        // Repair demand and the campaign difficulty restriction are independent.
        // Retain the legacy OFF control without requiring its Medium ban under
        // the corrected default: only an Easy campaign enemy is prohibited.
        const bool savedRoleFix = world.config->force.campaignRepairRoleFix;
        world.config->force.campaignRepairRoleFix = false;
        require(!world.bot->canAddRepairYard(0) || world.game->isCitySimEnabled(),
            "Repair demand changed the legacy OFF Medium restriction");
        world.config->force.campaignRepairRoleFix = true;
        require(world.bot->canAddRepairYard(0), "Custom Medium inherited a campaign repair prohibition");
        const auto originalType = world.game->gameType;
        world.game->gameType = GameType::Campaign;
        require(!world.bot->repairCapacityDemandActive(), "Treatment changed authored campaign reconstruction");
        world.bot->difficulty = QuantBot::Difficulty::Easy;
        require(world.bot->isCampaignEnemy() && !world.bot->canAddRepairYard(0),
            "Repair demand bypassed the Easy campaign enemy prohibition");
        world.bot->difficulty = QuantBot::Difficulty::Medium;
        require(world.bot->canAddRepairYard(0), "Medium campaign enemy retained the Easy repair prohibition");
        world.game->gameType = originalType;
        world.config->force.campaignRepairRoleFix = savedRoleFix;
    }
    SDL_Log("REPAIR_CAPACITY_CASE: transport access completed_return first_prerequisite restrictions passed");
}
} // namespace RepairCapacityProbe

void runRepairCapacityProbe(const char* mod) {
    QuantBotConfig defaults;
    RepairCapacityProbe::require(!defaults.force.repairCapacityDemand, "Repair treatment shipped ON without shared evidence");
    const auto offHash = defaults.getConfigHash();
    defaults.force.repairCapacityDemand = true;
    RepairCapacityProbe::require(defaults.getConfigHash() != offHash, "Repair treatment is not multiplayer-hashed");
    RepairCapacityProbe::require(defaults.save("repair-capacity-config.ini"), "Cannot save repair treatment config");
    QuantBotConfig loaded;
    RepairCapacityProbe::require(loaded.load("repair-capacity-config.ini") && loaded.force.repairCapacityDemand
        && loaded.getConfigHash() == defaults.getConfigHash(), "Repair treatment config did not roundtrip");
    RepairCapacityProbe::idleAndSelectors(mod);
    RepairCapacityProbe::backlogAndContinuation(mod);
    RepairCapacityProbe::blockedAndFirst(mod);
    const auto logged = RepairCapacityProbe::measuredConsumers(mod, true);
    const auto unlogged = RepairCapacityProbe::measuredConsumers(mod, false);
    RepairCapacityProbe::require(logged == unlogged,
        "Repair logging changed exact simulation credits, RNG, HP, queues, ownership, position or delivery timing");
    RepairCapacityProbe::baselineContinuation(mod);
    RepairCapacityProbe::lethalGroundReturn(mod);
    RepairCapacityProbe::producerSale();
    SDL_Log("REPAIR_CAPACITY_CASE: measurement_logging_parity passed");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=repair_capacity outcome=passed");
}
