// Private balance executable only. This is a focused production/weapon fixture,
// not a claim about whole-game construction priorities (the paired audit owns that).
// The real planner selects the infrastructure; its normal foundation, acceptance,
// payment, placement and WOR producer paths run against real engine objects. No
// troop is scenario-created, no queue/progress is filled by the fixture, and no
// produced troop receives a fixture attack/move/mode order before the air fight.
#include <Bullet.h>
#include <Command.h>
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <Map.h>
#include <dunecity/CitySimulation.h>
#include <mod/ModManager.h>
#include <players/HumanPlayer.h>
#include <players/PlayerFactory.h>
#include <players/QuantBot.h>
#include <players/QuantBotBuildPass.h>
#include <players/QuantBotConfig.h>
#include <structures/ConstructionYard.h>
#include <structures/ZoneStructure.h>
#include <units/Harvester.h>
#include <units/Ornithopter.h>
#include <units/Trooper.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace TrooperSupportProbe {

inline void require(bool ok, const char* reason) {
    if (!ok) {
        SDL_Log("TROOPER_SUPPORT_FAIL: %s", reason);
        throw std::runtime_error(reason);
    }
}

inline std::string mapText() {
    std::ostringstream map;
    map << "[BASIC]\nVersion=2\nWinFlags=3\nLoseFlags=1\nTechLevel=9\n"
        << "[MAP]\nSizeX=64\nSizeY=64\n";
    for (int y = 0; y < 64; ++y) {
        char row[16];
        std::snprintf(row, sizeof(row), "%03d=", y);
        map << row << std::string(64, '%') << '\n';
    }
    map << "[Player1]\nCredits=100000\nBrain=Team1\n"
        << "[Player2]\nCredits=100000\nBrain=Team2\n"
        << "[STRUCTURES]\nID000=Player1,Const Yard,256," << 8 + 8 * 64 << '\n'
        << "ID001=Player2,Const Yard,256," << 56 + 52 * 64 << '\n';
    return map.str();
}

struct World {
    std::unique_ptr<Game> game;
    House* owner = nullptr;
    House* enemy = nullptr;
    QuantBot* bot = nullptr;
    HumanPlayer* pilot = nullptr;
    ConstructionYard* yard = nullptr;
    std::string configHash;

    World(const char* mod, bool enabled, HOUSETYPE house = HOUSE_HARKONNEN) {
        require(currentGame == nullptr, "Two fixture worlds must not share the engine globals");
        getQuantBotConfig().force.trooperSupport = enabled;
        configHash = getQuantBotConfig().getConfigHash();
        require(ModManager::instance().setActiveMod(mod), "Cannot activate fixture mod");
        auto options = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
        options.fogOfWar = false;
        options.startWithExploredMap = true;
        options.immortalHumanPlayer = false;
        options.concreteRequired = false;
        options.researchLab = false;
        options.trooperAntiAir = true;
        options.maximumNumberOfHarvestersOverride = 8;
        // The server-name overload creates a network game, not this offline observer.
        GameInitSettings init("trooper-support.ini", mapText(), true, options);
        init.randomSeed = static_cast<Uint32>(std::stoul(std::getenv("BALANCE_SEED")));
        init.setLocalSpectator(true);
        const HOUSETYPE opposing = house == HOUSE_ATREIDES ? HOUSE_HARKONNEN : HOUSE_ATREIDES;
        GameInitSettings::HouseInfo defender(house, 1);
        defender.addPlayerInfo({"Trooper support QuantBot", "qBotHard"});
        init.addHouseInfo(defender);
        GameInitSettings::HouseInfo attacker(opposing, 2);
        attacker.addPlayerInfo({"Trooper support strike pilot", HUMANPLAYERCLASS});
        init.addHouseInfo(attacker);
        game = std::make_unique<Game>();
        currentGame = game.get();
        game->bReplay = true;
        game->initGame(init);
        require(game->gameType == GameType::CustomGame && game->isObserving()
                && pLocalHouse && pLocalPlayer, "Offline fixture did not initialize its genuine observer view");
        game->bReplay = false;
        game->initializeGameLoop();
        game->bPause = false;
        game->frameTiming.unitPhases = false;
        owner = game->getHouse(house);
        enemy = game->getHouse(opposing);
        require(owner && enemy && game->gameType == GameType::CustomGame, "Fixture is not a real Custom game");
        for (const auto& player : owner->getPlayerList())
            if (auto* candidate = dynamic_cast<QuantBot*>(player.get())) bot = candidate;
        for (const auto& player : enemy->getPlayerList())
            if (auto* human = dynamic_cast<HumanPlayer*>(player.get())) pilot = human;
        for (auto* structure : structureList)
            if (structure->getOwner() == owner && structure->getItemID() == Structure_ConstructionYard)
                yard = dynamic_cast<ConstructionYard*>(structure);
        require(bot && pilot && yard && game->getPlayerByID(pilot->getPlayerID()) == pilot,
                "Missing registered production bot, yard or genuine enemy pilot");

        // Controlled initial mature economy and a non-AA escort army. These are
        // scenario assets, NOT the WOR/troopers whose production is asserted.
        for (int x = 4; x <= 32; x += 4) place(Structure_WindTrap, Coord(x, 4));
        place(Structure_Radar, Coord(12, 8));
        place(Structure_LightFactory, Coord(16, 8));
        place(Structure_HeavyFactory, Coord(20, 8));
        place(Structure_HighTechFactory, Coord(24, 8));
        place(Structure_StarPort, Coord(28, 8));
        place(Structure_Refinery, Coord(8, 12));
        place(Structure_Refinery, Coord(12, 12));
        place(Structure_IX, Coord(16, 12));
        place(Structure_RepairYard, Coord(20, 12));
        place(Structure_RepairYard, Coord(24, 12));
        place(Structure_Barracks, Coord(28, 12));
        place(Structure_Silo, Coord(32, 12));
        if (game->isCitySimEnabled()) {
            auto* residential = dynamic_cast<ZoneStructure*>(place(Structure_ZoneResidential, Coord(4, 18)));
            require(residential, "Missing initial residential district");
            residential->setResidentialPopulation(100);
            place(Structure_ZoneCommercial, Coord(8, 18));
            place(Structure_ZoneIndustrial, Coord(12, 18));
            auto& state = game->getCitySimulation()->houseState_[house];
            state.resPop = 100;
            state.comPop = 20;
            state.indPop = 20;
            state.taxBaseEighths = 8000;
            state.avgLandValue = 200;
            state.resValve = state.comValve = state.indValve = 0;
        }
        for (int y = 2; y < 28; ++y) for (int x = 2; x < 36; ++x)
            if (!currentGameMap->getTile(x, y)->hasAStructure()) currentGameMap->getTile(x, y)->setRoad(true);
        for (int y = 4; y < 23; ++y) for (int x = 38; x < 62; ++x) {
            auto* tile = currentGameMap->getTile(x, y);
            tile->setType(Terrain_Spice);
            tile->setSpice(1000);
        }
        for (int i = 0; i < 8; ++i) {
            auto* worker = spawn(owner, Unit_Harvester, Coord(34 + i % 2, 24 + i / 2));
            worker->doSetAttackMode(HARVEST);
        }
        for (int i = 0; i < 40; ++i) spawn(owner, Unit_Tank, Coord(3 + i % 10, 38 + i / 10));
        yard->curUpgradeLev = yard->getMaxUpgradeLevel(); // The initial mature yard's legal capacity.
        yard->updateBuildList();
        game->gameCycleCount = MILLI2CYCLES(300000);
        bot->lastCalculatedSpice = 24 * 19 * 1000;
        bot->militaryValueLimit = 30000;
        bot->squadRallyLocation = Coord(18, 22);
        bot->squadRetreatLocation = Coord(18, 24);
        bot->pickStrategy();
        require(!bot->infantryStrategy(), "Fixture must not use the vanilla Infantry-strategy exception");
        require(owner->getNumItems(Structure_WOR) == 0 && owner->getNumItems(Unit_Trooper) == 0
                && owner->getNumItems(Unit_Troopers) == 0, "Fixture started with an asserted producer or troop");
        bot->buildTimer = INT_MAX;
        bot->attackTimer = INT_MAX;
    }

    ~World() {
        game.reset();
        currentGame = nullptr;
        pLocalPlayer = nullptr;
        pLocalHouse = nullptr;
    }

    StructureBase* place(Uint32 item, Coord at) {
        auto* structure = owner->placeStructure(NONE_ID, item, at.x, at.y, true, true);
        require(structure != nullptr, "Initial mature structure placement failed");
        return structure;
    }

    UnitBase* spawn(House* house, Uint32 item, Coord at) {
        require(item != Unit_Trooper && item != Unit_Troopers, "Asserted troops must only come from paid production");
        auto* unit = house->createUnit(item, true);
        require(unit != nullptr, "Initial fixture unit creation failed");
        unit->deploy(at);
        unit->setVisible(VIS_ALL, true);
        return unit;
    }

    // Exactly build()'s planning phases, with only the yard/WOR receiving turns.
    // Unrelated factories/turrets cannot win the aircraft killing blow for us.
    QuantBot::BuildPass plan() {
        QuantBot::BuildPass pass(bot->militaryUnitValue());
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
        bot->planCapitalCandidates(pass);
        bot->chooseCapitalPlan(pass);
        bot->planCivWarsResearch(pass);
        return pass;
    }

    Uint32 infrastructure(QuantBot::BuildPass& pass) {
        bot->planningBuilder = yard->getObjectID();
        bot->clearPlacementCache(false, true);
        return bot->trooperInfrastructureItem(pass, yard);
    }

    BuilderBase* wor() const {
        for (auto* structure : structureList)
            if (structure->getOwner() == owner && structure->getItemID() == Structure_WOR && structure->getHealth() > 0)
                return dynamic_cast<BuilderBase*>(structure);
        return nullptr;
    }

    void tick() {
        bot->buildTimer = INT_MAX; // Focused producer passes below, not competing AA purchases.
        bot->attackTimer = INT_MAX; // This is base defence, not an outbound attack-wave fixture.
        game->negotiatedBudget = 15000;
        game->carryOverTokens = 0;
        game->updateGameState();
        require(!game->finished, "Focused encounter ended before its evidence was collected");
    }
};

inline void blocked(World& world, QuantBot::BuildPass pass, const char* cause) {
    require(world.infrastructure(pass) == NONE_ID, cause);
    SDL_Log("TROOPER_SUPPORT_CONSTRAINT: cause=%s wor_selected=0 spendable=%d target=%d committed=%d quota_percent=%d",
            cause, pass.money, pass.trooperTarget, pass.trooperCount, pass.infantryPercent);
}

inline void availabilityAndBudgetGates(World& world) {
    auto pass = world.plan();
    require(pass.trooperSupport && pass.trooperTarget > 0 && world.infrastructure(pass) == Structure_WOR,
            "Funded mature city has no legal WOR opportunity");
    require(world.owner->hasPower() && !world.bot->openingWorkersNeeded(pass), "Positive fixture is not mature and powered");
    auto& data = world.game->objectData.data;
    const int house = world.owner->getHouseID();
    auto capped = pass;
    capped.infantryValue = capped.militaryValue;
    blocked(world, capped, "infantry_quota");
    capped = pass;
    capped.lightBudget = capped.militaryValue;
    blocked(world, capped, "military_value_cap");
    capped = pass;
    capped.trooperCount = capped.trooperTarget;
    blocked(world, capped, "useful_troop_target");
    capped = pass;
    capped.money = static_cast<int>(world.bot->buildingCapitalCost(capped, Structure_WOR) + capped.cityWorCashReserve - 1);
    require(capped.cityWorCashReserve >= capped.cityWorkingReserve && capped.cityWorkingReserve > 0,
            "City WOR test has no working reserve to protect");
    blocked(world, capped, "city_wor_cash_reserve");
    capped = pass;
    capped.itemCount[Structure_HeavyFactory] = 0;
    capped.hostileAircraft = 0;
    blocked(world, capped, "opening_without_heavy_factory_or_air");
    capped = pass;
    capped.itemCount[Unit_Harvester] = 0;
    capped.fundedHarvesterTarget = std::max(1, capped.fundedHarvesterTarget);
    require(!capped.harvesterFactories.empty(), "Worker gate has no real supplier");
    blocked(world, capped, "opening_workers");
    const int power = world.owner->producedPower;
    world.owner->producedPower = 0;
    blocked(world, pass, "power_deficit");
    world.owner->producedPower = power;

    const int tech = world.game->techLevel;
    world.game->techLevel = 1;
    world.yard->updateBuildList();
    blocked(world, world.plan(), "tech_unavailable");
    world.game->techLevel = tech;
    world.yard->updateBuildList();
    const bool single = data[Unit_Trooper][house].enabled;
    const bool squad = data[Unit_Troopers][house].enabled;
    data[Unit_Trooper][house].enabled = data[Unit_Troopers][house].enabled = false;
    blocked(world, world.plan(), "troop_content_unavailable");
    data[Unit_Trooper][house].enabled = single;
    data[Unit_Troopers][house].enabled = squad;
    const bool producer = data[Structure_WOR][house].enabled;
    data[Structure_WOR][house].enabled = false;
    world.yard->updateBuildList();
    blocked(world, world.plan(), "wor_content_unavailable");
    data[Structure_WOR][house].enabled = producer;
    world.yard->updateBuildList();
    auto restored = world.plan();
    require(world.infrastructure(restored) == Structure_WOR, "Negative gate setup leaked into the positive case");
}

inline void baselineGates() {
    std::string offHash;
    {
        World world("vanilla", false);
        auto pass = world.plan();
        offHash = world.configHash;
        require(pass.vanillaEconomy && !pass.trooperSupport && !world.bot->infantryStrategy()
                && world.yard->isAvailableToBuild(Structure_WOR) && world.bot->troopContentExists(pass, world.yard),
                "Vanilla baseline exclusion is masked by an unavailable or unfunded producer");
        QuantBot::BuilderTurn turn(pass.money);
        turn.pStructure = world.yard;
        turn.pBuilder = world.yard;
        QuantBot::YardTurn yardTurn;
        yardTurn.itemID = NONE_ID;
        world.bot->chooseYardMilitaryInfrastructure(pass, turn, yardTurn);
        require(yardTurn.itemID != Structure_WOR && world.yard->getProductionQueueSize() == 0,
                "OFF removed the Custom vanilla non-Infantry WOR exclusion");
        SDL_Log("TROOPER_SUPPORT_BASELINE: mod=vanilla enabled=0 wor_offer=1 wor_selected=0 wor_queued=0 "
                "baseline_cause=custom_vanilla_non_infantry_gate config_hash=%s", offHash.c_str());
    }
    {
        World world("vanilla", true);
        auto pass = world.plan();
        require(world.configHash != offHash && world.infrastructure(pass) == Structure_WOR,
                "ON failed to bypass the vanilla exclusion through its hashed config");
        SDL_Log("TROOPER_SUPPORT_GATE: mod=vanilla enabled=1 wor_selected=1 config_hash=%s", world.configHash.c_str());
        world.spawn(world.enemy, Unit_Ornithopter, Coord(48, 24));
        auto classicAir = world.plan();
        require(classicAir.hostileAircraft == 0 && classicAir.trooperTarget == pass.trooperTarget,
                "Classic non-tracking rockets were counted as useful moving-aircraft defence");
        SDL_Log("TROOPER_SUPPORT_CAPABILITY: mod=vanilla visible_aircraft=1 effective_air_demand=0");
    }
    {
        World world("dunecity", false);
        auto pass = world.plan();
        require(pass.citySimEnabled && !pass.cityAntiAirInfantryDemand && !pass.trooperSupport,
                "City OFF baseline unexpectedly observes a hostile wing");
        QuantBot::BuilderTurn turn(pass.money);
        turn.pStructure = world.yard;
        turn.pBuilder = world.yard;
        QuantBot::YardTurn yardTurn;
        yardTurn.itemID = NONE_ID;
        yardTurn.isCitySim = true;
        world.bot->chooseYardMilitaryInfrastructure(pass, turn, yardTurn);
        require(yardTurn.itemID != Structure_WOR, "City OFF founded WOR for peaceful army escort");
        SDL_Log("TROOPER_SUPPORT_BASELINE: mod=dunecity enabled=0 wor_selected=0 observed_aircraft=0 "
                "baseline_cause=legacy_city_requires_observed_air legacy_visible_air_path=retained");
    }
    {
        World world("dunecity", true, HOUSE_ATREIDES);
        auto pass = world.plan();
        require(!world.yard->isAvailableToBuild(Structure_WOR), "Atreides negative requires its real unavailable WOR");
        blocked(world, pass, "house_wor_unavailable");
    }
}

inline void observedDemand() {
    World world("dunecity", true);
    auto escort = world.plan();
    require(escort.trooperTarget > 0 && escort.trooperTarget <= 6 && escort.hostileAircraft == 0,
            "Peaceful escort demand is not bounded by six");
    for (int i = 0; i < 18; ++i) world.spawn(world.enemy, Unit_Tank, Coord(44 + i % 6, 36 + i / 6));
    world.bot->dangerUpdated = UINT32_MAX;
    world.bot->refreshTacticalDanger();
    auto armour = world.plan();
    require(armour.trooperTarget > escort.trooperTarget && armour.trooperTarget <= 9,
            "Real observed armour did not create bounded useful troop demand");
    for (int i = 0; i < 4; ++i) world.spawn(world.enemy, Unit_Ornithopter, Coord(48 + i, 24));
    auto air = world.plan();
    require(air.hostileAircraft == 4 && air.trooperTarget == 18,
            "Observed wing did not reach, and stop at, the eighteen-trooper cap");
    world.game->gameInitSettings.gameOptions.trooperAntiAir = false;
    auto disabledAir = world.plan();
    require(disabledAir.hostileAircraft == 0 && disabledAir.trooperTarget == armour.trooperTarget,
            "Disabled tracking rockets still raised anti-air troop demand");
    world.game->gameInitSettings.gameOptions.trooperAntiAir = true;
    SDL_Log("TROOPER_SUPPORT_CAPABILITY: mod=dunecity anti_air_option=0 visible_aircraft=4 effective_air_demand=0 target=%d",
            disabledAir.trooperTarget);
    SDL_Log("TROOPER_SUPPORT_DEMAND: escort=%d observed_armour=%d observed_aircraft=%d air_target=%d hard_cap=18",
            escort.trooperTarget, armour.trooperTarget, air.hostileAircraft, air.trooperTarget);
}

struct Orders {
    int singles = 0, squads = 0;
    int people() const { return singles + 3 * squads; }
};

inline void recordOrder(World& world, BuilderBase* producer, int before, int committedBefore,
                        const QuantBot::BuildPass& pass, Orders& orders) {
    if (producer->getProductionQueueSize() == before) return;
    require(before == 0 && producer->getProductionQueueSize() == 1, "WOR inserted more than one ordinary order");
    const Uint32 item = producer->getCurrentProducedItem();
    require(item == Unit_Trooper || item == Unit_Troopers, "Support selected a nonordinary infantry product");
    const int people = item == Unit_Troopers ? 3 : 1;
    if (people == 3) ++orders.squads; else ++orders.singles;
    require(pass.trooperCount == committedBefore + people && pass.trooperCount <= pass.trooperTarget,
            "Single/squad acceptance did not book one/three people inside the shared target");
    require(int64_t(pass.infantryValue) * 100 <= int64_t(pass.militaryValue) * pass.infantryPercent,
            "Accepted troop order exceeded the existing infantry quota");
    require(pass.money >= pass.cityWorkingReserve, "Accepted troop order spent the city working reserve");
    SDL_Log("TROOPER_SUPPORT_ORDER: cycle=%u item=%s people=%d committed=%d target=%d "
            "infantry_value=%d army_value=%d quota_percent=%d spendable_after=%d city_working_reserve=%d",
            world.game->getGameCycleCount(), item == Unit_Troopers ? "Troopers" : "Trooper", people,
            pass.trooperCount, pass.trooperTarget, pass.infantryValue, pass.militaryValue,
            pass.infantryPercent, pass.money, pass.cityWorkingReserve);
}

inline void producerCaps(World& world, BuilderBase* producer, Orders& orders) {
    auto baseline = world.plan();
    require(producer->isAvailableToBuild(Unit_Trooper), "Completed legal WOR has no real single Trooper offer");
    const int price = QuantBot::purchasePrice(producer, Unit_Trooper);
    auto reject = [&](QuantBot::BuildPass pass, const char* cause) {
        QuantBot::BuilderTurn turn(pass.money);
        turn.pBuilder = producer;
        turn.pStructure = producer;
        world.bot->buildWOR(pass, turn);
        require(producer->getProductionQueueSize() == 0, cause);
        SDL_Log("TROOPER_SUPPORT_CONSTRAINT: cause=%s troop_queued=0 spendable=%d city_working_reserve=%d",
                cause, pass.money, pass.cityWorkingReserve);
    };
    auto pass = baseline;
    pass.money = pass.cityWorkingReserve + price - 1;
    reject(pass, "troop_cash_and_city_working_reserve");
    pass = baseline;
    pass.infantryValue = pass.militaryValue;
    reject(pass, "troop_infantry_quota");
    pass = baseline;
    pass.lightBudget = pass.militaryValue;
    reject(pass, "troop_military_value_cap");
    pass = baseline;
    pass.trooperCount = pass.trooperTarget;
    reject(pass, "troop_useful_target_satisfied");
    pass = baseline;
    pass.trooperTarget = pass.trooperCount + 2;
    require(!world.bot->trooperOrderAdmitted(pass, Unit_Troopers,
                world.game->objectData.data[Unit_Troopers][world.owner->getHouseID()].price),
            "A three-person squad fitted into two remaining support slots");
    pass.trooperTarget = pass.trooperCount + 1;
    const int committed = pass.trooperCount;
    QuantBot::BuilderTurn turn(pass.money);
    turn.pBuilder = producer;
    turn.pStructure = producer;
    world.bot->buildWOR(pass, turn);
    require(producer->getCurrentProducedItem() == Unit_Trooper && producer->getProductionQueueSize() == 1,
            "One useful remaining slot did not select and queue an affordable single Trooper");
    recordOrder(world, producer, 0, committed, pass, orders);
}

inline void authority(World& world, const std::set<Uint32>& produced) {
    const auto* factory = PlayerFactory::getByPlayerClass(HUMANPLAYERCLASS);
    require(factory != nullptr, "Human authority fixture has no normal player factory");
    auto human = factory->create(world.owner, "Trooper support shared-house commander");
    auto* commander = dynamic_cast<HumanPlayer*>(human.get());
    require(commander != nullptr, "Authority fixture did not create a genuine HumanPlayer");
    world.owner->addPlayer(std::move(human));
    require(world.game->getPlayerByID(commander->getPlayerID()) == commander, "Shared human was not registered");
    UnitBase* commanded = nullptr;
    for (Uint32 id : produced)
        if (auto* live = dynamic_cast<UnitBase*>(world.game->getObjectManager().getObject(id))) { commanded = live; break; }
    require(commanded != nullptr, "No produced troop survived for the human-authority control");
    Command(commander->getPlayerID(), CMD_UNIT_SETMODE, commanded->getObjectID(), HOLD).executeCommand();
    require(world.bot->humanControls(commanded)
            && world.bot->unitRole(commanded, {QuantBot::UnitRole::Human}) == QuantBot::UnitRole::Human,
            "Real human command did not acquire the existing unit-role authority");
    auto* saboteur = world.spawn(world.owner, Unit_Saboteur, Coord(38, 46));
    saboteur->doSetAttackMode(AREAGUARD);
    saboteur->doMove2Pos(Coord(48, 46), true);
    const Coord destination = saboteur->getDestination();
    require(saboteur->wasForced() && !world.bot->humanControls(saboteur), "Saboteur control is masked by a human lease");
    for (int i = 0; i < 10; ++i) {
        auto pass = world.plan();
        if (auto* producer = world.wor()) world.bot->buildProducer(pass, producer);
        world.bot->checkAllUnits();
        world.tick();
        require(commanded->getAttackMode() == HOLD && world.bot->humanControls(commanded),
                "Production/tactical updates replaced a human-leased troop's order");
        require(saboteur->getAttackMode() == AREAGUARD && saboteur->wasForced()
                && saboteur->getDestination() == destination, "Production/tactical updates replaced a forced saboteur order");
    }
    world.bot->retreatAllUnits();
    require(commanded->getAttackMode() == HOLD && saboteur->getDestination() == destination && saboteur->wasForced(),
            "Retreat captured a human lease or independent saboteur");
    saboteur->doSetAttackMode(HUNT);
    world.bot->checkAllUnits();
    require(saboteur->getAttackMode() == HUNT, "Ordinary checkAllUnits intercepted independent saboteur Hunt");
    SDL_Log("TROOPER_SUPPORT_AUTHORITY: registered_human_command=1 human_role=Human lease_preserved=1 "
            "forced_saboteur_preserved=1 independent_saboteur_hunt=1 cycles=10");
}

inline void productionAndAirDefence() {
    World world("dunecity", true);
    availabilityAndBudgetGates(world);
    auto pass = world.plan();
    const int target = pass.trooperTarget;
    const int builtWorBefore = world.owner->getNumBuiltItems(Structure_WOR);
    const int builtTroopsBefore = world.owner->getNumBuiltItems(Unit_Trooper);
    const Uint32 start = world.game->getGameCycleCount();
    QuantBot::BuilderTurn turn(pass.money);
    turn.pBuilder = world.yard;
    turn.pStructure = world.yard;
    world.bot->beginBuilderTurn(pass, turn);
    const Uint32 selected = world.infrastructure(pass);
    require(selected == Structure_WOR && world.bot->queueFoundedStructure(pass, turn, selected, __LINE__, "trooper_support"),
            "Real support selector/foundation/acceptance path did not queue WOR");
    require(world.yard->getBuildItem(Structure_WOR) && world.yard->getBuildItem(Structure_WOR)->num == 1,
            "Selected WOR never entered the real construction queue");
    SDL_Log("TROOPER_SUPPORT_WOR_QUEUED: cycle=%u producer_scope=focused_yard_wor enabled=1 target=%d "
            "cash=%d city_working_reserve=%d config_hash=%s health_damage_range=stock",
            start, target, world.owner->getCredits(), pass.cityWorkingReserve, world.configHash.c_str());
    const Uint32 deadline = start + MILLI2CYCLES(300000);
    BuilderBase* producer = nullptr;
    while (!producer && world.game->getGameCycleCount() < deadline) {
        if (world.yard->isWaitingToPlace()) {
            auto placement = world.plan();
            world.bot->buildProducer(placement, world.yard);
        }
        world.tick();
        producer = world.wor();
    }
    require(producer && world.owner->getNumBuiltItems(Structure_WOR) == builtWorBefore + 1,
            "Selected, paid WOR did not complete and pass ordinary AI placement");
    SDL_Log("TROOPER_SUPPORT_WOR_COMPLETED: cycle=%u wor_completed=1 object=%u elapsed_cycles=%u",
            world.game->getGameCycleCount(), producer->getObjectID(), world.game->getGameCycleCount() - start);
    Orders orders;
    producerCaps(world, producer, orders);
    const bool squadOffer = producer->isAvailableToBuild(Unit_Troopers);
    while (world.game->getGameCycleCount() < deadline) {
        if (world.game->getGameCycleCount() % 25 == 0) {
            auto training = world.plan();
            const int before = producer->getProductionQueueSize();
            const int committed = training.trooperCount;
            world.bot->buildProducer(training, producer);
            recordOrder(world, producer, before, committed, training, orders);
        }
        world.tick();
        if (world.owner->getNumBuiltItems(Unit_Trooper) - builtTroopsBefore == target
            && producer->getProductionQueueSize() == 0) break;
    }
    const int completed = world.owner->getNumBuiltItems(Unit_Trooper) - builtTroopsBefore;
    require(completed == target && orders.people() == completed && orders.singles > 0
            && (!squadOffer || orders.squads > 0), "Real troop completion differs from the bounded single/squad orders");
    auto atTarget = world.plan();
    world.bot->buildProducer(atTarget, producer);
    require(atTarget.trooperCount == target && producer->getProductionQueueSize() == 0,
            "Completed troops did not stop production at the useful target");
    std::set<Uint32> produced;
    for (const auto* unit : unitList)
        if (unit->getOwner() == world.owner && unit->getItemID() == Unit_Trooper) produced.insert(unit->getObjectID());
    require(static_cast<int>(produced.size()) == completed && world.owner->getNumItems(Structure_RocketTurret) == 0
            && world.owner->getNumItems(Unit_Launcher) == 0, "Air fixture has unproduced troopers or a competing AA weapon");
    SDL_Log("TROOPER_SUPPORT_TROOPERS_COMPLETED: cycle=%u troopers_completed=%d singles_queued=%d squads_queued=%d "
            "weighted_queued=%d target=%d real_queue_completion=1 producer_scope=focused_yard_wor",
            world.game->getGameCycleCount(), completed, orders.singles, orders.squads, orders.people(), target);

    const auto rewardBefore = world.owner->getCombatReward(Unit_Trooper);
    const Uint32 producerID = producer->getObjectID();
    const int killBefore = world.owner->getNumKilledItems(Unit_Ornithopter);
    const int lostBefore = world.enemy->getNumLostItems(Unit_Ornithopter);
    auto* bird = dynamic_cast<Ornithopter*>(world.spawn(world.enemy, Unit_Ornithopter, producer->getLocation() + Coord(3, 1)));
    require(bird && bird->getHealth() == bird->getMaxHealth(), "Strike bird is not an ordinary full-health Ornithopter");
    const Uint32 birdID = bird->getObjectID();
    const long long initialHP = bird->getHealth().getRawValue();
    long long minimumHP = initialHP;
    const long long baseInitial = producer->getHealth().getRawValue();
    long long baseMinimum = baseInitial;
    bird->angle = destinationDrawnAngle(bird->getLocation(), producer->getLocation());
    bird->drawnAngle = lround(bird->angle) % NUM_ANGLES;
    Command(world.pilot->getPlayerID(), CMD_UNIT_ATTACKOBJECT, birdID, producerID).executeCommand();
    require(bird->getTarget() == producer && bird->getOwner()->getTeamID() != world.owner->getTeamID(),
            "Genuine hostile pilot did not order the real base attack");
    const Uint32 combatStart = world.game->getGameCycleCount();
    const Uint32 combatEnd = combatStart + MILLI2CYCLES(90000);
    int rocketCycles = 0, strikeCycles = 0, targetedCycles = 0;
    while (world.game->getGameCycleCount() < combatEnd) {
        world.tick();
        const auto* live = world.game->getObjectManager().getObject(birdID);
        if (live) minimumHP = std::min(minimumHP, static_cast<long long>(live->getHealth().getRawValue()));
        else minimumHP = 0;
        const auto* base = world.game->getObjectManager().getObject(producerID);
        require(base && base->getHealth() > 0, "Troopers failed to preserve their attacked production base");
        baseMinimum = std::min(baseMinimum, static_cast<long long>(base->getHealth().getRawValue()));
        bool rocket = false, strike = false;
        for (const auto* bullet : bulletList) {
            if (bullet->shooterID == birdID) strike = true;
            if (bullet->getBulletID() == Bullet_SmallRocket && bullet->target.getObjectID() == birdID
                && produced.count(bullet->shooterID) && bullet->owner == world.owner) rocket = true;
        }
        rocketCycles += rocket;
        strikeCycles += strike;
        for (Uint32 id : produced)
            if (const auto* unit = dynamic_cast<const UnitBase*>(world.game->getObjectManager().getObject(id)))
                if (unit->getTarget() && unit->getTarget()->getObjectID() == birdID) { ++targetedCycles; break; }
        if (!live || live->getHealth() <= 0) break;
    }
    const auto* live = world.game->getObjectManager().getObject(birdID);
    const bool destroyed = !live || live->getHealth() <= 0;
    const auto& reward = world.owner->getCombatReward(Unit_Trooper);
    const auto troopKills = reward.kills - rewardBefore.kills;
    const auto troopHits = reward.hits - rewardBefore.hits;
    const auto troopHP = reward.hpRemovedMilli - rewardBefore.hpRemovedMilli;
    const int birdKills = world.owner->getNumKilledItems(Unit_Ornithopter) - killBefore;
    const int birdLosses = world.enemy->getNumLostItems(Unit_Ornithopter) - lostBefore;
    SDL_Log("TROOPER_SUPPORT_AIR_RESULT: bird_destroyed=%d bird_initial_hp_raw=%lld bird_min_hp_raw=%lld "
            "bird_hp_loss_raw=%lld troop_hits=%llu troop_hp_removed_milli=%lld troop_killing_blows=%llu "
            "enemy_bird_kills=%d enemy_bird_losses=%d smallrocket_target_cycles=%d ordinary_target_cycles=%d "
            "bird_attack_projectile_cycles=%d base_hp_loss_raw=%lld combat_cycles=%u producer_scope=focused_yard_wor "
            "defender_attack_orders=none lethal_damager=%s",
            int(destroyed), initialHP, minimumHP, initialHP - minimumHP,
            static_cast<unsigned long long>(troopHits), static_cast<long long>(troopHP),
            static_cast<unsigned long long>(troopKills), birdKills, birdLosses, rocketCycles, targetedCycles,
            strikeCycles, baseInitial - baseMinimum, world.game->getGameCycleCount() - combatStart,
            troopKills == 1 ? "Trooper" : "unproven");
    require(destroyed && birdKills == 1 && birdLosses == 1 && troopKills == 1 && troopHits > 0 && troopHP > 0,
            "Bird disappearance was not a real hostile killing blow credited to produced Troopers");
    require(rocketCycles > 0 && targetedCycles > 0 && (strikeCycles > 0 || baseMinimum < baseInitial),
            "No real produced-trooper SmallRocket interception of an attacking bird was observed");
    authority(world, produced);
}

} // namespace TrooperSupportProbe

void runTrooperSupportProbe() {
    struct RestoreConfig {
        QuantBotConfig saved = getQuantBotConfig();
        ~RestoreConfig() { getQuantBotConfig() = saved; }
    } restore;
    getQuantBotConfig().strategy.random = false;
    getQuantBotConfig().strategy.fixedMix = QuantBotStrategy::Mix::Learned;
    getQuantBotConfig().strategy.fixedTempo = QuantBotStrategy::Tempo::Base;
    TrooperSupportProbe::baselineGates();
    TrooperSupportProbe::observedDemand();
    TrooperSupportProbe::productionAndAirDefence();
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=trooper_support outcome=passed producer_scope=focused_yard_wor");
}
