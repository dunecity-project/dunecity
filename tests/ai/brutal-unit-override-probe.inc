// Real-engine fixture for the Brutal "Maximum Number of Units Override" behaviour.
// Only compiled by run-brutal-unit-override-probe.py, never by the shipped game.
//
// It drives the production decision through QuantBot::build() with real builders, so the
// configured military value cap, the rolling override budget and the engine's unit-count
// limits are all exercised where they actually apply. The configured cap is set to zero
// for every case: a house that keeps ordering military units in that state can only be
// using the override budget.
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <Map.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <Choam.h>
#include <players/QuantBot.h>
#include <players/QuantBotBuildPolicy.h>
#include <structures/BuilderBase.h>
#include <structures/StarPort.h>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

int runBrutalUnitOverrideProbe() {
    const std::string mod = std::getenv("UNIT_OVERRIDE_PROBE_MOD");
    auto require = [](bool ok, const char* why) {
        if(!ok) { SDL_Log("UNIT_OVERRIDE_FAIL: %s", why); throw std::runtime_error(why); }
    };
    require(ModManager::instance().setActiveMod(mod), "Missing override fixture mod");
    settings.general.playerName = "Override tester";
    std::ifstream mapFile(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(mapFile)), {});
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("override.ini", mapData, "Brutal unit override", true, settings.gameOptions);
    init.randomSeed = 91;
    require(init.getModName() == mod, "Override fixture selected the wrong mod");
    GameInitSettings::HouseInfo humans(HOUSE_ATREIDES, 1);
    humans.addPlayerInfo({"Override tester", "HumanPlayer"});
    init.addHouseInfo(humans);
    // Ordos: its configured unit mix includes aircraft, so the air branch is reachable
    // in this fixture instead of being skipped for a house that never buys them.
    GameInitSettings::HouseInfo bots(HOUSE_ORDOS, 2);
    bots.addPlayerInfo({"Brutal bot", "qBotBrutal"});
    init.addHouseInfo(bots);
    game->initGame(init);
    auto* house = game->getHouse(HOUSE_ORDOS);
    require(house && house->isAlive(), "Override fixture AI house missing");
    QuantBot* bot = nullptr;
    for(const auto& player : house->getPlayerList())
        if(auto* candidate = dynamic_cast<QuantBot*>(player.get())) bot = candidate;
    require(bot != nullptr, "Override fixture needs a QuantBot controller");
    require(bot->difficulty == QuantBot::Difficulty::Brutal, "Fixture bot is not Brutal");

    game->techLevel = 9;
    // The engine builds the city simulation when the main loop starts, which this
    // fixture never runs. City-mode planning dereferences it, so create it exactly as
    // Game does: each mod then plans in its own mode rather than crashing or silently
    // falling back to the vanilla branches.
    if(game->isCitySimEnabled() && !game->getCitySimulation()) {
        game->citySimulation_ = std::make_unique<DuneCity::CitySimulation>();
        game->citySimulation_->init(currentGameMap->getSizeX(), currentGameMap->getSizeY());
        game->citySimulation_->setCityEffectsEnabled(true);
    }
    require(!game->isCitySimEnabled() || game->getCitySimulation() != nullptr,
            "City mode fixture has no city simulation");
    const int houseID = house->getHouseID();
    auto& data = game->objectData.data;
    // Keep the decision on military production: economy and colonist orders have their
    // own priorities inside the same factory branch, and the fixture is not about them.
    const bool savedMcv = data[Unit_MCV][houseID].enabled;
    const bool savedWorker = data[Unit_Harvester][houseID].enabled;
    const bool savedTransport = data[Unit_Carryall][houseID].enabled;
    data[Unit_MCV][houseID].enabled = false;
    data[Unit_Harvester][houseID].enabled = false;
    data[Unit_Carryall][houseID].enabled = false;

    // Returns the structure, not a builder: a builder-only result would keep searching
    // after a successful placement and litter the map with prerequisite structures.
    auto place = [&](Uint32 item) -> StructureBase* {
        const Coord size = getStructureSize(item);
        for(int y = 2; y + size.y < currentGameMap->getSizeY() - 2; ++y)
            for(int x = 2; x + size.x < currentGameMap->getSizeX() - 2; ++x) {
                bool clear = true;
                for(int dy = 0; dy < size.y; ++dy) for(int dx = 0; dx < size.x; ++dx)
                    clear &= !currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject()
                        && !currentGameMap->getTile(x+dx,y+dy)->isMountain();
                if(!clear) continue;
                if(auto* built = house->placeStructure(NONE_ID, item, x, y, true, true))
                    return dynamic_cast<StructureBase*>(built);
            }
        return nullptr;
    };
    auto placeBuilder = [&](Uint32 item) { return dynamic_cast<BuilderBase*>(place(item)); };
    // Satisfy the catalogue's own prerequisites for an item, so availability is decided by
    // the real build list instead of being assumed.
    auto satisfyPrerequisites = [&](Uint32 item) {
        const auto& objData = data[item][houseID];
        const int limit = std::min<int>(Num_ItemID, int(objData.prerequisiteStructuresSet.size()));
        for(int prerequisite = 0; prerequisite < limit; ++prerequisite)
            if(objData.prerequisiteStructuresSet[prerequisite]
               && house->getNumItems(prerequisite) <= 0
               && data[prerequisite][houseID].enabled)
                require(place(prerequisite) != nullptr, "Cannot place a catalogue prerequisite");
    };
    // A construction yard exists so the factory never falls back to the colonist
    // bootstrap; it is deliberately kept out of the driven builder list.
    require(place(Structure_ConstructionYard) != nullptr, "Override fixture needs a yard");
    satisfyPrerequisites(Unit_Tank);
    auto* factory = placeBuilder(Structure_HeavyFactory);
    require(factory != nullptr, "Override fixture needs a heavy factory");
    // Aircraft and imports are only exercised where the active catalogue really offers
    // them; everything else about their reachability is set up for real.
    const bool aircraftInCatalogue = data[Unit_Ornithopter][houseID].enabled
        && game->techLevel >= data[Unit_Ornithopter][houseID].techLevel;
    BuilderBase* airFactory = nullptr;
    if(aircraftInCatalogue) {
        satisfyPrerequisites(Unit_Ornithopter);
        satisfyPrerequisites(Structure_HighTechFactory);
        airFactory = placeBuilder(Structure_HighTechFactory);
        require(airFactory != nullptr, "Catalogue offers aircraft but the fixture has no factory");
    }
    satisfyPrerequisites(Structure_StarPort);
    auto* port = dynamic_cast<StarPort*>(place(Structure_StarPort));
    // Fully upgraded builders: an available upgrade legitimately outranks military
    // production, which would hide the budget decision this fixture measures. The build
    // list has to be refreshed afterwards or the new upgrade level unlocks nothing.
    for(auto* builder : {factory, airFactory, static_cast<BuilderBase*>(port)})
        if(builder) {
            builder->curUpgradeLev = static_cast<Uint8>(builder->getMaxUpgradeLevel());
            builder->updateBuildList();
        }
    if(airFactory)
        require(airFactory->isAvailableToBuild(Unit_Ornithopter),
                "Aircraft are in the catalogue but still not buildable after prerequisites");
    bot->gameMode = QuantBot::GameMode::Custom;
    bot->lastCalculatedSpice = 0;
    // Earned credits are limited by silo/refinery storage this fixture has none of, so
    // the planning cash is set as starting credits, which the engine keeps outside it.
    // Enough that the funded plan can reach the air share as well as the heavy one.
    // The fully upgraded builders keep the cash from being spent on upgrades instead.
    house->startingCredits = 60000;
    require(house->getCredits() >= 60000, "Override fixture house has no planning cash");

    const auto savedOverride = game->gameInitSettings.gameOptions.maximumNumberOfUnitsOverride;
    const int savedMaxUnits = house->maxUnits;
    const auto savedDifficulty = bot->difficulty;
    std::ofstream results(std::string(std::getenv("UNIT_OVERRIDE_PROBE_OUT"))+"/"+mod+".csv");
    results << "case,difficulty,override,max_units,max_units_effective,ground_limit_reached,ordered_item\n";

    // Returns the military item this builder was given, or ItemID_Invalid for none.
    auto orderedMilitary = [&](BuilderBase* builder, QuantBot::Difficulty tier,
                               int unitOverride, int maxUnits, const char* label) {
        game->gameInitSettings.gameOptions.maximumNumberOfUnitsOverride = unitOverride;
        house->maxUnits = maxUnits;
        bot->difficulty = tier;
        bot->militaryValueLimit = 0;   // configured cap exhausted in every case
        while(builder->getCurrentProducedItem() != ItemID_Invalid)
            builder->doCancelItem(builder->getCurrentProducedItem(), false);
        auto structures = structureList;
        structureList.clear(); structureList.push_back(builder);
        try { bot->build(0); } catch(...) { structureList = structures; throw; }
        structureList = structures;
        const Uint32 item = builder->getCurrentProducedItem();
        const Uint32 ordered = (item != ItemID_Invalid && QuantBotBuildPolicy::militaryItem(item))
            ? item : Uint32(ItemID_Invalid);
        results << label << ',' << int(tier) << ',' << unitOverride << ',' << maxUnits << ','
                << house->getMaxUnits() << ',' << house->isGroundUnitLimitReached() << ','
                << (ordered == ItemID_Invalid ? std::string("none") : getItemNameByID(ordered)) << '\n';
        return ordered;
    };
    const int groundUnits = house->getNumUnits();
    const int roomyCap = groundUnits + 20;

    // 1. Override disabled keeps the configured value cap, and Brutal keeps ignoring the
    //    authored count ceiling exactly as before.
    require(orderedMilitary(factory, QuantBot::Difficulty::Brutal, -1, 1, "disabled") == ItemID_Invalid,
            "Disabled override let Brutal build past the configured military value cap");
    require(house->getMaxUnits() == 0 && !house->isGroundUnitLimitReached(),
            "Disabled override changed Brutal's engine count behaviour");
    // 2. Override 0 means unlimited: production continues past the configured value cap.
    const Uint32 unlimited = orderedMilitary(factory, QuantBot::Difficulty::Brutal, 0, 0, "unlimited");
    require(unlimited != ItemID_Invalid, "Unlimited override did not build past the value cap");
    require(house->getMaxUnits() == 0, "Unlimited override stopped reporting an unlimited ceiling");
    // 3. A positive override below its count limit still produces, and the engine ceiling
    //    is now the one the player selected.
    const Uint32 below = orderedMilitary(factory, QuantBot::Difficulty::Brutal, roomyCap, roomyCap, "below_cap");
    require(below != ItemID_Invalid, "Positive override below the count limit built nothing");
    require(house->getMaxUnits() == roomyCap && !house->isGroundUnitLimitReached(),
            "Positive override did not become the engine ceiling");
    // 4. At the count limit the engine remains the authority: no further military orders.
    require(orderedMilitary(factory, QuantBot::Difficulty::Brutal, 1, 1, "at_cap") == ItemID_Invalid,
            "Brutal ordered military units at the override unit-count limit");
    require(house->getMaxUnits() == 1 && house->isGroundUnitLimitReached(),
            "Fixture failed to reach the override unit-count limit");
    // 5. Every other difficulty is unchanged, including Hard's count behaviour.
    for(auto tier : {QuantBot::Difficulty::Easy, QuantBot::Difficulty::Medium,
                     QuantBot::Difficulty::Hard, QuantBot::Difficulty::Defend}) {
        require(orderedMilitary(factory, tier, 0, 0, "other_difficulty") == ItemID_Invalid,
                "A non-Brutal difficulty gained the override military budget");
        require(house->getMaxUnits() == (tier == QuantBot::Difficulty::Hard ? 0 : 0),
                "Override 0 stopped meaning unlimited");
    }
    for(auto tier : {QuantBot::Difficulty::Easy, QuantBot::Difficulty::Medium, QuantBot::Difficulty::Hard}) {
        bot->difficulty = tier;
        game->gameInitSettings.gameOptions.maximumNumberOfUnitsOverride = 1;
        house->maxUnits = 1;
        require(house->getMaxUnits() == (tier == QuantBot::Difficulty::Hard ? 0 : 1),
                "A positive override changed Hard's authored count behaviour");
    }
    // 6. Air production follows the same budget, and its own category limit still stops it.
    bool airExercised = false;
    if(airFactory) {
        require(orderedMilitary(airFactory, QuantBot::Difficulty::Brutal, 0, 0, "air_unlimited")
                    == Unit_Ornithopter,
                "Unlimited override did not order the reachable aircraft past the value cap");
        require(orderedMilitary(airFactory, QuantBot::Difficulty::Brutal, -1, 0, "air_disabled") == ItemID_Invalid,
                "Disabled override let air production pass the configured military value cap");
        // Aircraft have their own category limit, 11/25 of a ceiling of at least 25, so the
        // fleet itself has to reach it rather than the ground count.
        const int savedAircraft = house->numItem[Unit_Ornithopter];
        house->maxUnits = 1;
        game->gameInitSettings.gameOptions.maximumNumberOfUnitsOverride = 1;
        house->numItem[Unit_Ornithopter] = 11 * std::max(house->getMaxUnits(), 25) / 25;
        require(house->isAirUnitLimitReached(), "Air fixture did not reach the air category limit");
        const int aircraft = house->numItem[Unit_Ornithopter];
        require(orderedMilitary(airFactory, QuantBot::Difficulty::Brutal, 1, 1, "air_at_cap") == ItemID_Invalid,
                "Aircraft were ordered with the air category limit reached");
        require(house->numItem[Unit_Ornithopter] == aircraft, "Blocked air case still created aircraft");
        house->numItem[Unit_Ornithopter] = savedAircraft;
        airExercised = true;
    }

    // 7. Imports: real discounted stock with real cash. The same budget decides the order,
    //    and the engine's count limit decides both the order and the delivery.
    bool importsExercised = false;
    if(port && data[Unit_Tank][houseID].enabled) {
        auto& choam = house->getChoam();
        // Ordering really consumes market stock, so each case starts from the same
        // controlled offer instead of inheriting the previous case's sold-out market.
        auto seedMarket = [&]() {
            choam.availableItems.clear();
            BuildItem stock;
            stock.itemID = Unit_Tank;
            stock.num = 3;
            stock.price = std::max(1, data[Unit_Tank][houseID].price / 2);   // genuine discount
            choam.availableItems.push_back(stock);
            for(auto& offer : port->buildList) offer.num = 0;
            port->updateBuildList();
        };
        seedMarket();
        require(house->getChoam().getNumAvailable(Unit_Tank) == 3
                && house->getChoam().getPrice(Unit_Tank) < data[Unit_Tank][houseID].price,
                "Import fixture has no discounted stock");
        auto importOrders = [&](int unitOverride, int maxUnits, const char* label) {
            seedMarket();
            port->currentProductionQueue.clear();
            port->arrivalTimer = -1;      // STARPORT_NO_ARRIVAL_AWAITED
            port->deploying = false;
            port->deployTimer = 0;
            game->gameInitSettings.gameOptions.maximumNumberOfUnitsOverride = unitOverride;
            house->maxUnits = maxUnits;
            bot->difficulty = QuantBot::Difficulty::Brutal;
            bot->militaryValueLimit = 0;
            auto structures = structureList;
            structureList.clear(); structureList.push_back(port);
            try { bot->build(0); } catch(...) { structureList = structures; throw; }
            structureList = structures;
            const size_t queued = port->currentProductionQueue.size();
            results << label << ",3," << unitOverride << ',' << maxUnits << ','
                    << house->getMaxUnits() << ',' << house->isGroundUnitLimitReached() << ','
                    << (queued ? "import_x" + std::to_string(queued) : std::string("none")) << '\n';
            return queued;
        };
        const int roomyPortCap = house->getNumUnits() + 20;
        require(importOrders(0, 0, "import_unlimited") > 0,
                "Unlimited override imported nothing from discounted stock");
        require(importOrders(roomyPortCap, roomyPortCap, "import_below_cap") > 0,
                "Positive override below the count limit imported nothing");
        require(importOrders(-1, 1, "import_disabled") == 0,
                "Disabled override imported past the configured military value cap");
        require(importOrders(1, 1, "import_at_cap") == 0,
                "Imports were ordered with the unit-count limit already reached");

        // Admission at delivery: an order placed below the ceiling must wait, not arrive.
        const size_t pending = importOrders(roomyPortCap, roomyPortCap, "import_pending");
        require(pending > 0, "Import admission case needs a pending shipment");
        // A paid import still contributes its NOMINAL military value to planning,
        // without reserving its discounted purchase price a second time. Inspect the
        // production telemetry at a unique fixture cycle in the driver after exit.
        const int nominalQueued = int(pending) * data[Unit_Tank][houseID].price;
        const auto savedCycle = game->gameCycleCount;
        game->gameCycleCount = 17;
        auto savedStructures = structureList;
        structureList.clear(); structureList.push_back(port);
        bot->build(0);
        structureList = savedStructures;
        game->gameCycleCount = savedCycle;
        require(port->currentProductionQueue.size() == pending,
                "Planning changed a paid pending shipment");
        SDL_Log("OVERRIDE_QUEUE_EXPECTATION: nominal=%d paid=%d", nominalQueued,
                int(pending) * house->getChoam().getPrice(Unit_Tank));
        const Uint32 pendingItem = port->currentProductionQueue.front().itemID;
        const FixPoint creditsBefore = house->storedCredits + house->startingCredits + house->cityCredits;
        const int unitsBefore = house->getNumItems(pendingItem);
        house->maxUnits = 1;                             // category filled after ordering
        require(house->isUnitLimitReached(pendingItem), "Import fixture did not fill the category");
        port->arrivalTimer = -1; port->deploying = true; port->deployTimer = 1;
        for(int attempt = 0; attempt < 5; ++attempt) {
            port->updateStructureSpecificStuff();
            port->deployTimer = 1;                        // skip the real retry interval
        }
        require(port->currentProductionQueue.size() == pending
                && port->currentProductionQueue.front().itemID == pendingItem,
                "A blocked import lost its place in the queue");
        require(house->storedCredits + house->startingCredits + house->cityCredits == creditsBefore && house->getNumItems(pendingItem) == unitsBefore,
                "A blocked import was refunded, cancelled or delivered anyway");
        const int occupiedGround = house->numUnits - house->numItem[Unit_Soldier]
            - house->numItem[Unit_Trooper] - house->numItem[Unit_Carryall]
            - house->numItem[Unit_ChemicalCarryall] - house->numItem[Unit_Ornithopter]
            + (house->numItem[Unit_Soldier]+2)/3 + (house->numItem[Unit_Trooper]+2)/3;
        house->maxUnits = occupiedGround + 1;           // exactly one category slot opens
        port->deployTimer = 1;
        // One update delivers exactly one order: the engine re-arms its own interval.
        port->updateStructureSpecificStuff();
        require(house->getNumItems(pendingItem) == unitsBefore + 1,
                "The waiting import did not resume once there was room");
        require(port->currentProductionQueue.size() == pending - 1,
                "Resuming delivered more or less than the one waiting order");
        require(house->storedCredits + house->startingCredits + house->cityCredits == creditsBefore, "Resuming the import charged or refunded again");
        require(house->isGroundUnitLimitReached(), "The resumed import did not fill its one slot");
        port->deployTimer = 1;
        port->updateStructureSpecificStuff();
        require(port->currentProductionQueue.size() == pending - 1
                && house->getNumItems(pendingItem) == unitsBefore + 1,
                "A second import bypassed the newly filled category");
        results << "import_resumed,3," << house->maxUnits << ',' << house->maxUnits << ','
                << house->getMaxUnits() << ',' << house->isGroundUnitLimitReached() << ','
                << getItemNameByID(pendingItem) << '\n';
        importsExercised = true;
    }

    // 8. The factory execution layer is authority too, with no AI involved: a queued unit
    //    makes no progress and costs nothing while the category is full.
    {
        game->gameInitSettings.gameOptions.maximumNumberOfUnitsOverride = 1;
        bot->difficulty = QuantBot::Difficulty::Brutal;
        house->maxUnits = 1;
        while(factory->getCurrentProducedItem() != ItemID_Invalid)
            factory->doCancelItem(factory->getCurrentProducedItem(), false);
        factory->doProduceItem(Unit_Tank, false);
        require(factory->getCurrentProducedItem() == Unit_Tank, "Factory refused the direct order");
        require(house->isGroundUnitLimitReached(), "Builder authority case is not at the cap");
        const FixPoint blockedProgress = factory->productionProgress;
        const FixPoint blockedCredits = house->storedCredits + house->startingCredits + house->cityCredits;
        for(int tick = 0; tick < 50; ++tick) factory->updateProductionProgress();
        require(factory->productionProgress == blockedProgress,
                "A full category still advanced factory production");
        require(house->storedCredits + house->startingCredits + house->cityCredits == blockedCredits, "A blocked factory order still spent credits");
        house->maxUnits = house->getNumUnits() + 5;
        for(int tick = 0; tick < 50; ++tick) factory->updateProductionProgress();
        require(factory->productionProgress > blockedProgress,
                "Production did not resume once the category had room");
        while(factory->getCurrentProducedItem() != ItemID_Invalid)
            factory->doCancelItem(factory->getCurrentProducedItem(), false);
    }

    game->gameInitSettings.gameOptions.maximumNumberOfUnitsOverride = savedOverride;
    house->maxUnits = savedMaxUnits;
    bot->difficulty = savedDifficulty;
    data[Unit_MCV][houseID].enabled = savedMcv;
    data[Unit_Harvester][houseID].enabled = savedWorker;
    data[Unit_Carryall][houseID].enabled = savedTransport;
    results.close();
    SDL_Log("BRUTAL_UNIT_OVERRIDE_PROBE_PASS: %s (heavy=yes air=%s imports=%s builder_authority=yes"
            " unlimited_item=%s)", mod.c_str(),
            airExercised ? "asserted" : "not_in_catalogue",
            importsExercised ? "asserted" : "not_in_catalogue", getItemNameByID(unlimited).c_str());
    game.reset(); currentGame = nullptr;
    return -1;
}
