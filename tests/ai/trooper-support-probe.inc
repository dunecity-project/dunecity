// Private balance executable only. Full build() turns must found exactly one WOR
// and bootstrap paid standing troops from an empty army; the paired audit still
// owns whole-game outcome claims. A separate focused yard/WOR world isolates the
// produced troops' real aircraft kill from competing launchers/turrets. No troop
// is scenario-created, no queue/progress is filled by the fixture, and no produced
// troop receives a fixture attack/move/mode order before that air fight.
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
#include <players/QuantBotBuildPolicy.h>
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

    World(const char* mod, bool enabled, HOUSETYPE house = HOUSE_HARKONNEN, int escortTanks = 40,
          bool passiveOpponentImmortal = false) {
        require(currentGame == nullptr, "Two fixture worlds must not share the engine globals");
        getQuantBotConfig().force.trooperSupport = enabled;
        configHash = getQuantBotConfig().getConfigHash();
        require(ModManager::instance().setActiveMod(mod), "Cannot activate fixture mod");
        auto options = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
        options.fogOfWar = false;
        options.startWithExploredMap = true;
        options.immortalHumanPlayer = passiveOpponentImmortal;
        options.instantBuild = false;
        options.concreteRequired = false;
        options.researchLab = false;
        options.trooperAntiAir = true;
        options.maximumNumberOfHarvestersOverride = 8;
        // The server-name overload creates a network game, not this offline observer.
        GameInitSettings init("trooper-support.ini", mapText(), true, options);
        init.randomSeed = static_cast<Uint32>(std::stoul(std::getenv("BALANCE_SEED")));
        init.setLocalSpectator(!passiveOpponentImmortal);
        const HOUSETYPE opposing = house == HOUSE_ATREIDES ? HOUSE_HARKONNEN : HOUSE_ATREIDES;
        GameInitSettings::HouseInfo defender(house, 1);
        defender.addPlayerInfo({"Trooper support QuantBot", "qBotHard"});
        init.addHouseInfo(defender);
        GameInitSettings::HouseInfo attacker(opposing, 2);
        attacker.addPlayerInfo({passiveOpponentImmortal ? settings.general.playerName : "Trooper support strike pilot",
                                HUMANPLAYERCLASS});
        init.addHouseInfo(attacker);
        game = std::make_unique<Game>();
        currentGame = game.get();
        game->bReplay = true;
        game->initGame(init);
        require(game->gameType == GameType::CustomGame && game->isObserving() == !passiveOpponentImmortal
                && pLocalHouse && pLocalPlayer, "Offline fixture did not initialize its genuine local view");
        game->bReplay = false;
        game->initializeGameLoop();
        game->bPause = false;
        game->frameTiming.unitPhases = false;
        owner = game->getHouse(house);
        enemy = game->getHouse(opposing);
        require(owner && enemy && game->gameType == GameType::CustomGame, "Fixture is not a real Custom game");
        if (passiveOpponentImmortal)
            require(pLocalHouse == enemy && game->localHumanImmortalityApplies(),
                    "Full-builder passive opponent is not the genuinely immortal local Human");
        for (const auto& player : owner->getPlayerList())
            if (auto* candidate = dynamic_cast<QuantBot*>(player.get())) bot = candidate;
        for (const auto& player : enemy->getPlayerList())
            if (auto* human = dynamic_cast<HumanPlayer*>(player.get())) pilot = human;
        for (auto* structure : structureList)
            if (structure->getOwner() == owner && structure->getItemID() == Structure_ConstructionYard)
                yard = dynamic_cast<ConstructionYard*>(structure);
        require(bot && pilot && yard && game->getPlayerByID(pilot->getPlayerID()) == pilot,
                "Missing registered production bot, yard or genuine enemy pilot");

        // Controlled initial mature economy and optional non-AA escort army.
        // These are scenario assets, NOT the WOR/troopers whose production is asserted.
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
        for (int i = 0; i < escortTanks; ++i) spawn(owner, Unit_Tank, Coord(3 + i % 10, 38 + i / 10));
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
        return bot->trooperInfrastructureItem(pass, yard, true);
    }

    BuilderBase* wor() const {
        for (auto* structure : structureList)
            if (structure->getOwner() == owner && structure->getItemID() == Structure_WOR && structure->getHealth() > 0)
                return dynamic_cast<BuilderBase*>(structure);
        return nullptr;
    }

    void tick() {
        bot->buildTimer = INT_MAX; // Measured full/focused turns run explicitly, never a hidden extra builder pass.
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
    require(pass.trooperSupport && pass.trooperTarget == 6 && world.infrastructure(pass) == Structure_WOR,
            "Funded mature city has no legal standing-WOR opportunity");
    require(world.owner->hasPower() && !world.bot->openingWorkersNeeded(pass) && pass.forecastNetIncome > 0,
            "Positive fixture is not mature, powered and earning income");
    auto& data = world.game->objectData.data;
    const int house = world.owner->getHouseID();
    auto independent = [&](QuantBot::BuildPass capped, const char* cause) {
        require(world.infrastructure(capped) == Structure_WOR, cause);
        SDL_Log("TROOPER_SUPPORT_INFRASTRUCTURE_INDEPENDENT: cause=%s wor_selected=1 target=%d committed=%d",
                cause, capped.trooperTarget, capped.trooperCount);
    };
    auto capped = pass;
    capped.infantryValue = capped.militaryValue;
    independent(capped, "infantry_quota_does_not_remove_first_wor");
    capped = pass;
    capped.lightBudget = capped.militaryValue;
    independent(capped, "military_value_cap_does_not_remove_first_wor");
    capped = pass;
    capped.trooperCount = capped.trooperTarget;
    capped.itemCount[Unit_Trooper] = capped.trooperCount;
    require(!world.bot->trooperOrderAdmitted(capped, Unit_Trooper, data[Unit_Trooper][house].price),
            "Infrastructure test's satisfied contingent still admits an extra troop");
    independent(capped, "useful_target_satisfied_but_first_wor_missing");
    capped = pass;
    capped.itemCount[Structure_WOR] = 1;
    blocked(world, capped, "existing_or_pending_wor");
    capped = pass;
    const int64_t fundedCost = world.bot->buildingCapitalCost(capped, Structure_WOR) + capped.cityWorCashReserve;
    capped.money = static_cast<int>(fundedCost - 1);
    require(capped.cityWorCashReserve >= capped.cityWorkingReserve && capped.cityWorkingReserve > 0,
            "City WOR test has no working reserve to protect");
    blocked(world, capped, "city_wor_cash_reserve");
    capped = pass;
    capped.cashFlow.projectedCash = fundedCost - 1;
    independent(capped, "parallel_queue_forecast_does_not_remove_first_wor");
    capped = pass;
    capped.money = 0;
    capped.forecastNetIncome = 1;
    require(world.bot->trooperInfrastructureItem(capped, world.yard, false) == Structure_WOR
            && world.infrastructure(capped) == NONE_ID,
            "Positive low income cannot reserve a first WOR before it is affordable");
    SDL_Log("TROOPER_SUPPORT_FIRST_WOR_SAVINGS: spendable=0 forecast_net_income=1 "
            "candidate_selected=1 unfunded_queue_rejected=1");
    capped = pass;
    capped.forecastNetIncome = 0;
    blocked(world, capped, "wor_no_stable_income");
    capped = pass;
    // No engine tick occurs while the completed-count control is changed. A
    // pending HF and an observed wing must not substitute for a standing HF.
    const int heavyFactories = world.owner->numItem[Structure_HeavyFactory];
    world.owner->numItem[Structure_HeavyFactory] = 0;
    capped.itemCount[Structure_HeavyFactory] = 1;
    capped.hostileAircraft = 4;
    blocked(world, capped, "no_standing_heavy_factory_despite_pending_factory_and_air");
    world.owner->numItem[Structure_HeavyFactory] = heavyFactories;
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
        require(world.configHash != offHash && !pass.trooperSupport
                && world.infrastructure(pass) == NONE_ID,
                "DuneCity recruitment changed the classic content policy");
        SDL_Log("TROOPER_SUPPORT_GATE: mod=vanilla enabled=1 active=0 wor_selected=0 config_hash=%s", world.configHash.c_str());
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
    require(escort.trooperTarget == 6 && escort.hostileAircraft == 0,
            "Peaceful standing contingent is not exactly six");
    for (int i = 0; i < 18; ++i) world.spawn(world.enemy, Unit_Tank, Coord(44 + i % 6, 36 + i / 6));
    world.bot->updateEnemySightings();
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
    SDL_Log("TROOPER_SUPPORT_DEMAND: escort=%d standing_floor=6 observed_armour=%d observed_aircraft=%d air_target=%d hard_cap=18",
            escort.trooperTarget, armour.trooperTarget, air.hostileAircraft, air.trooperTarget);
}

struct Orders {
    int singles = 0, squads = 0, quotaExempt = 0;
    int people() const { return singles + 3 * squads; }
};

inline void recordOrder(World& world, BuilderBase* producer, int before, int committedBefore,
                        const QuantBot::BuildPass& pass, Orders& orders, const char* scope = "focused_yard_wor") {
    if (producer->getProductionQueueSize() == before) return;
    require(before == 0 && producer->getProductionQueueSize() == 1, "WOR inserted more than one ordinary order");
    const Uint32 item = producer->getCurrentProducedItem();
    require(item == Unit_Trooper || item == Unit_Troopers, "Support selected a nonordinary infantry product");
    const int people = item == Unit_Troopers ? 3 : 1;
    if (people == 3) ++orders.squads; else ++orders.singles;
    require(pass.trooperCount == committedBefore + people && pass.trooperCount <= pass.trooperTarget,
            "Single/squad acceptance did not book one/three people inside the shared target");
    const int floor = std::max(6, std::min(18, std::clamp(pass.hostileAircraft, 0, 4) * 5));
    const bool withinQuota = int64_t(pass.infantryValue) * 100 <= int64_t(pass.militaryValue) * pass.infantryPercent;
    require(withinQuota || pass.trooperCount <= floor, "Extra anti-armour order exceeded the aggregate infantry quota");
    require(pass.militaryValue <= pass.lightBudget, "Standing/reactive exemption exceeded the normal military cap");
    if (!withinQuota) ++orders.quotaExempt;
    require(pass.money >= pass.cityWorkingReserve, "Accepted troop order spent the city working reserve");
    SDL_Log("TROOPER_SUPPORT_ORDER: cycle=%u item=%s people=%d committed=%d target=%d floor=%d quota_exempt=%d "
            "infantry_value=%d army_value=%d quota_percent=%d spendable_after=%d city_working_reserve=%d producer_scope=%s",
            world.game->getGameCycleCount(), item == Unit_Troopers ? "Troopers" : "Trooper", people,
            pass.trooperCount, pass.trooperTarget, floor, int(!withinQuota), pass.infantryValue, pass.militaryValue,
            pass.infantryPercent, pass.money, pass.cityWorkingReserve, scope);
}

inline void producerCaps(World& world, BuilderBase* producer, Orders& orders) {
    auto baseline = world.plan();
    require(producer->isAvailableToBuild(Unit_Trooper), "Completed legal WOR has no real single Trooper offer");
    const int house = world.owner->getHouseID();
    const int value = world.game->objectData.data[Unit_Trooper][house].price;
    const int squadValue = world.game->objectData.data[Unit_Troopers][house].price;
    const int price = QuantBot::purchasePrice(producer, Unit_Trooper);
    auto reject = [&](QuantBot::BuildPass pass, const char* cause) {
        QuantBot::BuilderTurn turn(pass.money);
        turn.pBuilder = producer;
        turn.pStructure = producer;
        world.bot->buildWOR(pass, turn);
        require(producer->getProductionQueueSize() == 0, cause);
        SDL_Log("TROOPER_SUPPORT_CONSTRAINT: cause=%s troop_queued=0 committed=%d target=%d effective_aircraft=%d "
                "spendable=%d city_working_reserve=%d",
                cause, pass.trooperCount, pass.trooperTarget, pass.hostileAircraft, pass.money, pass.cityWorkingReserve);
    };
    auto pass = baseline;
    pass.money = pass.cityWorkingReserve + price - 1;
    reject(pass, "troop_cash_and_city_working_reserve");
    pass = baseline;
    pass.infantryValue = pass.militaryValue;
    require(!world.bot->infantryQuotaAdmits(pass, value) && world.bot->trooperOrderAdmitted(pass, Unit_Trooper, value),
            "Current-army quota still forbids the first standing contingent");
    pass.trooperCount = 6;
    pass.trooperTarget = 9;
    pass.hostileAircraft = 0;
    reject(pass, "extra_armour_above_six_infantry_quota");
    pass = baseline;
    pass.infantryValue = pass.militaryValue;
    pass.trooperCount = 6;
    pass.trooperTarget = 18;
    pass.hostileAircraft = 4;
    require(!world.bot->infantryQuotaAdmits(pass, squadValue)
            && world.bot->trooperOrderAdmitted(pass, Unit_Troopers, squadValue),
            "Current-army quota still forbids the effective wing's reactive floor");
    pass.trooperCount = 17;
    require(world.bot->trooperOrderAdmitted(pass, Unit_Trooper, value)
            && !world.bot->trooperOrderAdmitted(pass, Unit_Troopers, squadValue),
            "Reactive floor did not admit exactly one of its last eighteen slots");
    pass.trooperCount = 18;
    reject(pass, "troop_reactive_eighteen_cap_satisfied");
    pass = baseline;
    pass.lightBudget = pass.militaryValue;
    reject(pass, "troop_military_value_cap");
    pass = baseline;
    pass.trooperCount = pass.trooperTarget;
    reject(pass, "troop_useful_target_satisfied");
    pass = baseline;
    pass.trooperTarget = pass.trooperCount + 2;
    require(!world.bot->trooperOrderAdmitted(pass, Unit_Troopers, squadValue),
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

inline void fullBuildStandingRecruitment() {
    // This full-builder world has unrestricted normal military producers.
    // Keep its passive Human opponent alive with the existing game option;
    // the separate stock-health aircraft-kill world remains mortal.
    World world("dunecity", true, HOUSE_HARKONNEN, 0, true);
    auto initial = world.plan();
    const int troopValue = world.game->objectData.data[Unit_Trooper][world.owner->getHouseID()].price;
    require(initial.militaryValue == 0 && initial.trooperCount == 0 && initial.trooperTarget == 6
            && initial.hostileAircraft == 0 && world.infrastructure(initial) == Structure_WOR,
            "Full-builder world is not an empty army with a funded standing-WOR opportunity");
    require(!world.bot->infantryQuotaAdmits(initial, troopValue)
            && world.bot->trooperOrderAdmitted(initial, Unit_Trooper, troopValue),
            "Empty current army cannot bootstrap its standing troop contingent");
    SDL_Log("TROOPER_SUPPORT_BOOTSTRAP: starting_army_value=%d standing_floor=6 ordinary_quota_admits=0 "
            "support_admits=1 initial_tanks=0 passive_opponent_immortal=1 producer_scope=full_build",
            initial.militaryValue);

    const int builtWorBefore = world.owner->getNumBuiltItems(Structure_WOR);
    const int builtTroopsBefore = world.owner->getNumBuiltItems(Unit_Trooper);
    const Uint32 start = world.game->getGameCycleCount();
    int fullTurns = 0;
    int expectedTarget = 6;
    bool worQueued = false, worCompleted = false, worPaid = false, troopPaid = false;
    Orders orders;
    auto observe = [&]() {
        int committedWors = world.owner->getNumItems(Structure_WOR);
        for (const auto* structure : structureList) {
            const auto* builder = dynamic_cast<const BuilderBase*>(structure);
            if (!builder || structure->getOwner() != world.owner) continue;
            for (const auto& entry : builder->getBuildList())
                if (entry.itemID == Structure_WOR) committedWors += entry.num;
            if (builder->getProductionProgress() <= 0) continue;
            if (builder->getCurrentProducedItem() == Structure_WOR) worPaid = true;
            if (builder->getItemID() == Structure_WOR
                && (builder->getCurrentProducedItem() == Unit_Trooper || builder->getCurrentProducedItem() == Unit_Troopers))
                troopPaid = true;
        }
        require(committedWors <= 1, "Full builder duplicated the main house's existing/pending WOR");
        require(world.owner->getNumBuiltItems(Unit_Trooper) - builtTroopsBefore <= expectedTarget,
                "Full builder completed troops beyond its standing/reactive target");
        if (!worCompleted && world.wor()) {
            worCompleted = true;
            require(worQueued && worPaid, "Full builder's WOR completion lacks its ordinary paid queue");
            SDL_Log("TROOPER_SUPPORT_FULL_WOR_COMPLETED: cycle=%u wor_completed=1 elapsed_cycles=%u full_turns=%d "
                    "producer_scope=full_build",
                    world.game->getGameCycleCount(), world.game->getGameCycleCount() - start, fullTurns);
        }
    };
    auto fullTurn = [&]() {
        auto before = world.plan();
        auto* producer = world.wor();
        const int queueBefore = producer ? producer->getProductionQueueSize() : 0;
        // All producers, normal priorities, queue acceptance and placement run.
        // Neither WOR nor troop orders are inserted by this fixture.
        world.bot->build(world.bot->militaryUnitValue());
        ++fullTurns;
        auto after = world.plan();
        require(after.trooperTarget == expectedTarget && after.trooperCount <= expectedTarget,
                "Full builder's committed count escaped the live standing/reactive demand");
        if (!worQueued && world.owner->getNumItems(Structure_WOR) == 0 && after.itemCount[Structure_WOR] == 1) {
            worQueued = true;
            SDL_Log("TROOPER_SUPPORT_FULL_WOR_QUEUED: cycle=%u target=%d current_army_value=%d full_turns=%d "
                    "cash=%d producer_scope=full_build queue_insertion=normal_build",
                    world.game->getGameCycleCount(), after.trooperTarget, after.militaryValue, fullTurns, world.owner->getCredits());
        }
        if (auto* liveProducer = world.wor())
            recordOrder(world, liveProducer, queueBefore, before.trooperCount, after, orders, "full_build");
        observe();
    };
    auto step = [&]() {
        if (world.game->getGameCycleCount() % 25 == 0) fullTurn();
        observe();
        world.tick();
        observe();
    };
    auto completeContingent = [&](const char* stage) {
        troopPaid = false;
        const Uint32 deadline = world.game->getGameCycleCount() + MILLI2CYCLES(300000);
        while (world.game->getGameCycleCount() < deadline) {
            auto* producer = world.wor();
            if (producer && world.owner->getNumBuiltItems(Unit_Trooper) - builtTroopsBefore == expectedTarget
                && producer->getProductionQueueSize() == 0) break;
            step();
        }
        auto done = world.plan();
        auto* producer = world.wor();
        require(producer && worCompleted && worPaid && troopPaid
                && world.owner->getNumBuiltItems(Structure_WOR) == builtWorBefore + 1
                && world.owner->getNumItems(Structure_WOR) == 1
                && world.owner->getNumBuiltItems(Unit_Trooper) - builtTroopsBefore == expectedTarget
                && done.trooperCount == expectedTarget && producer->getProductionQueueSize() == 0
                && orders.people() == expectedTarget,
                "Full build() did not complete exactly one paid WOR and its useful standing/reactive troops");
        // Normal full turns can grow vehicles while urgent cover and the WOR
        // complete. The isolated zero-army phase below separately requires
        // actual paid quota-exempt orders, without racing those producers.
        SDL_Log("TROOPER_SUPPORT_FULL_TROOPS_COMPLETED: stage=%s cycle=%u wor_completed=1 troopers_completed=%d "
                "target=%d effective_aircraft=%d weighted_queued=%d quota_exempt_orders=%d full_turns=%d "
                "paid_progress=1 producer_scope=full_build",
                stage, world.game->getGameCycleCount(), expectedTarget, done.trooperTarget, done.hostileAircraft,
                orders.people(), orders.quotaExempt, fullTurns);
        const Uint32 holdEnd = world.game->getGameCycleCount() + MILLI2CYCLES(30000);
        while (world.game->getGameCycleCount() < holdEnd) {
            step();
            require(world.wor() && world.wor()->getProductionQueueSize() == 0 && orders.people() == expectedTarget,
                    "Continued full builder turns ordered unnecessary troops after satisfying the contingent");
        }
        require(world.owner->getNumBuiltItems(Structure_WOR) == builtWorBefore + 1
                && world.owner->getNumItems(Structure_WOR) == 1,
                "Continued full builder turns replaced or duplicated the completed WOR");
        SDL_Log("TROOPER_SUPPORT_FULL_CAP_HELD: stage=%s target=%d wor_completed=1 troop_queued=0 "
                "hold_ms=30000 full_turns=%d producer_scope=full_build",
                stage, expectedTarget, fullTurns);
    };
    fullTurn();
    completeContingent("standing");

    // A real, visible wing loiters by its distant human-owned base. The full
    // builder may make other AA here; only the separate killing-blow world below
    // excludes it. No fixture order is ever sent to the produced troops.
    for (int i = 0; i < 4; ++i) {
        auto* bird = world.spawn(world.enemy, Unit_Ornithopter, Coord(52 + 4 * (i % 2), 54 + 4 * (i / 2)));
        Command(world.pilot->getPlayerID(), CMD_UNIT_SETMODE, bird->getObjectID(), HOLD).executeCommand();
    }
    expectedTarget = 18;
    auto wing = world.plan();
    require(wing.hostileAircraft == 4 && wing.trooperTarget == 18 && wing.trooperCount == 6,
            "Live effective wing did not raise the existing standing contingent to the bounded reactive floor");
    completeContingent("effective_wing");
}

inline void productionAndAirDefence() {
    World world("dunecity", true, HOUSE_HARKONNEN, 0);
    availabilityAndBudgetGates(world);
    auto pass = world.plan();
    const int target = pass.trooperTarget;
    require(pass.militaryValue == 0 && pass.trooperCount == 0 && target == 6,
            "Focused standing bootstrap did not start with a genuinely empty army");
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
    require(orders.quotaExempt > 0,
            "The actual empty-army paid orders did not exercise the standing-quota exemption");
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
            "weighted_queued=%d target=%d quota_exempt_orders=%d starting_army_value=0 "
            "real_queue_completion=1 producer_scope=focused_yard_wor",
            world.game->getGameCycleCount(), completed, orders.singles, orders.squads, orders.people(), target,
            orders.quotaExempt);

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
    TrooperSupportProbe::fullBuildStandingRecruitment();
    TrooperSupportProbe::productionAndAirDefence();
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=trooper_support outcome=passed producer_scope=full_build_and_focused_yard_wor "
            "full_build_wor=1 full_build_standing=6 full_build_air_cap=18");
}
