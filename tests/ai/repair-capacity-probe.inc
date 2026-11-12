// Existing balance executable only: real queues, repair jobs, placement and saves.
#include <structures/RepairYard.h>
#include <units/GroundUnit.h>
#include <units/Carryall.h>

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
    require(expansion && expansion->getLocation() == expansionSite,
        "Accepted useful repair capacity was not built at its real reserved site within the bound");
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
        require(!world.bot->canAddRepairYard(0) || world.game->isCitySimEnabled(),
            "Treatment loosened non-city Medium's repair restriction");
        const auto originalType = world.game->gameType;
        world.game->gameType = GameType::Campaign;
        require(!world.bot->repairCapacityDemandActive(), "Treatment changed authored campaign reconstruction");
        world.game->gameType = originalType;
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
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=repair_capacity outcome=passed");
}
