// Real-game fixture, included only by the diagnostic main.
#include <dunecity/CityConstants.h>
#include <dunecity/CitySimulation.h>
#include <players/QuantBot.h>
#include <structures/StarPort.h>
#include <Choam.h>
#include <Map.h>
#include <units/UnitBase.h>
#include <array>
#include <limits>
#include <stdexcept>

void runStarportEconomyProbe(Game& game) {
    auto* house = game.getHouse(HOUSE_HARKONNEN);
    auto* bot = dynamic_cast<QuantBot*>(game.getPlayerByName("Harkonnen"));
    if (!house || !bot) throw std::runtime_error("Starport fixture requires the Harkonnen AI partner");
    auto place = [&](Uint32 item) -> StructureBase* {
        for (int y=1; y<50; y+=4) for (int x=1; x<50; x+=4) {
            bool clear=true;
            for (int dy=0; dy<3; ++dy) for (int dx=0; dx<3; ++dx)
                if (currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject()) clear=false;
            if (clear) return house->placeStructure(NONE_ID,item,x,y,true,true);
        }
        throw std::runtime_error("No free fixture site");
    };
    for (int i=0; i<3; ++i) if (!place(Structure_Refinery)) throw std::runtime_error("Fixture refinery failed");
    if (!house->getNumItems(Structure_WindTrap)) place(Structure_WindTrap);
    for (auto* building : structureList) if (building->getOwner() == house
        && building->getItemID() == Structure_ConstructionYard) {
        auto* yard = static_cast<BuilderBase*>(building);
        yard->updateBuildList();
        if (!yard->isAvailableToBuild(Structure_StarPort))
            throw std::runtime_error("Starport unavailable before city population develops");
    }
    if (game.isCitySimEnabled()) {
        bot->initialItemCount[Structure_RepairYard] = 0;
        for (auto tier : {QuantBot::Difficulty::Easy, QuantBot::Difficulty::Medium,
                         QuantBot::Difficulty::Hard, QuantBot::Difficulty::Brutal}) {
            bot->difficulty = tier;
            if (!bot->canAddRepairYard(0)) throw std::runtime_error("City difficulty cannot add a repair yard");
        }
        bot->difficulty = QuantBot::Difficulty::Easy;
    }
    auto* port = dynamic_cast<StarPort*>(place(Structure_StarPort));
    if (!port) throw std::runtime_error("Fixture Starport failed");
    if (!house->getNumItems(Unit_Harvester)) house->createUnit(Unit_Harvester,true);
    if (house->getNumItems(Unit_Carryall)) throw std::runtime_error("Fixture already has a carryall");
    auto& market = house->getChoam();
    for (auto& item : market.availableItems) {
        item.num = item.itemID == Unit_Harvester ? 5 : item.itemID == Unit_Carryall ? 1 : 0;
        if (item.itemID == Unit_Harvester) item.price = 1200;
        if (item.itemID == Unit_Carryall) item.price = 1500;
    }
    port->updateBuildList();
    if (market.isCheap(Unit_Harvester) || market.isCheap(Unit_Carryall))
        throw std::runtime_error("Fixture must exercise non-discounted prices");
    house->takeCredits(house->getCredits()); house->addCredits(2700);
    bot->gameMode = QuantBot::GameMode::Custom; // Full-control human partner mode.
    bot->harvesterLimit = 15; bot->militaryValueLimit = 20000;
    // The normal first AI update surveys spice before build(); this fixture
    // calls build directly, so populate that survey from the actual map.
    bot->lastCalculatedSpice = 0;
    for (int y=0; y<currentGameMap->getSizeY(); ++y)
        for (int x=0; x<currentGameMap->getSizeX(); ++x)
            bot->lastCalculatedSpice += currentGameMap->getTile(x,y)->getSpice().lround();
    // Isolate this producer for one planning pass. Household structures/counts
    // and the real Starport ordering/payment code remain intact.
    auto savedStructures = structureList;
    structureList.clear(); structureList.push_back(port);
    try { bot->build(0); } catch (...) { structureList = savedStructures; throw; }
    structureList = savedStructures;
    int workers=0, transports=0;
    for (const auto& queued : port->currentProductionQueue) {
        workers += queued.itemID == Unit_Harvester;
        transports += queued.itemID == Unit_Carryall;
    }
    if (workers != 1 || transports != 1 || house->getCredits() != 0 || port->okToOrder())
        throw std::runtime_error("Economic imports did not spend reserved cash at above-normal prices");
    SDL_Log("STARPORT_ECONOMY_PROBE_PASS: 2700 credits buy first carryall at 1500 and harvester at 1200; order placed, no overspend");
    // Sold-out workers must not turn their reserved budget into military or
    // extra carryalls. Once stock returns, use the saved money in bulk.
    auto* restockPort = dynamic_cast<StarPort*>(place(Structure_StarPort));
    for (auto& offer : market.availableItems) {
        offer.num = (offer.itemID == Unit_Quad || offer.itemID == Unit_Carryall) ? 5 : 0;
        offer.price = offer.itemID == Unit_Harvester ? 300 : 100;
    }
    restockPort->updateBuildList(); house->addCredits(800);
    bot->difficulty = QuantBot::Difficulty::Hard;
    auto planPorts = [&](StarPort* extra) {
        savedStructures = structureList;
        structureList.clear(); structureList.push_back(port); structureList.push_back(restockPort);
        if (extra) structureList.push_back(extra);
        try { bot->build(0); } catch (...) { structureList = savedStructures; throw; }
        structureList = savedStructures;
    };
    planPorts(nullptr);
    if (!restockPort->currentProductionQueue.empty() || house->getCredits() != 800)
        throw std::runtime_error("Sold-out workers' reserve was spent on military or extra carryalls");
    for (auto& offer : market.availableItems) if (offer.itemID == Unit_Harvester) offer.num = 2;
    restockPort->updateBuildList(); planPorts(nullptr);
    int restockedWorkers = 0;
    for (const auto& queued : restockPort->currentProductionQueue) restockedWorkers += queued.itemID == Unit_Harvester;
    if (restockedWorkers != 2 || restockPort->currentProductionQueue.size() != 2
        || house->getCredits() != 200 || restockPort->okToOrder())
        throw std::runtime_error("Restocked workers did not get the saved cash before military");
    SDL_Log("STARPORT_WORKER_RESERVE_PASS: saved 800 while workers sold out; bought two at 300 on restock, retained 200");
    // Once the worker target is covered by existing and paid cargo, release
    // the reserve and buy all cheap combat types below the old 2000 floor.
    auto* bargainPort = dynamic_cast<StarPort*>(place(Structure_StarPort));
    bot->harvesterLimit = house->getNumItems(Unit_Harvester) + workers + restockedWorkers;
    for (auto& offer : market.availableItems) {
        offer.num = (offer.itemID == Unit_Quad || offer.itemID == Unit_Tank
            || offer.itemID == Unit_Launcher || offer.itemID == Unit_SiegeTank) ? 2 : 0;
        offer.price = offer.itemID == Unit_Harvester ? 300 : 100;
    }
    bargainPort->updateBuildList();
    house->takeCredits(house->getCredits()); house->addCredits(800);
    planPorts(bargainPort);
    for (Uint32 type : {Unit_Quad, Unit_Tank, Unit_Launcher, Unit_SiegeTank}) {
        int count = 0;
        for (const auto& queued : bargainPort->currentProductionQueue) count += queued.itemID == type;
        if (count != 2) throw std::runtime_error("Did not buy every cheap combat type in bulk");
    }
    if (house->getCredits() != 0 || bargainPort->okToOrder())
        throw std::runtime_error("Bargain order overspent or was not dispatched");
    SDL_Log("STARPORT_BARGAIN_PASS: 800 credits buy eight units across four types, including quads");
    // The reported regression: six existing workers, 180-credit harvesters,
    // and a 15-worker human-allied ceiling. Fill all nine vacancies at once,
    // even if a legacy/remaining-spice planner target still says six.
    auto* workerPort = dynamic_cast<StarPort*>(place(Structure_StarPort));
    house->maxHarvesters = 15;
    game.gameInitSettings.gameOptions.maximumNumberOfHarvestersOverride = 15;
    bot->difficulty = QuantBot::Difficulty::Brutal;
    while (house->getNumItems(Unit_Harvester) < 6) house->createUnit(Unit_Harvester,true);
    bot->harvesterLimit = 6; bot->lastCalculatedSpice = 20000;
    for (auto& offer : market.availableItems) {
        offer.num = offer.itemID == Unit_Harvester ? 9 : offer.itemID == Unit_Carryall ? 1 : 0;
        if (offer.itemID == Unit_Carryall) offer.price = 800;
        if (offer.itemID == Unit_Harvester) offer.price = 180;
    }
    workerPort->updateBuildList();house->addCredits(2420);
    savedStructures = structureList;
    structureList.clear();structureList.push_back(workerPort);
    try { bot->build(0); } catch (...) { structureList=savedStructures;throw; }
    structureList=savedStructures;
    workers=0;transports=0;
    for (const auto& queued : workerPort->currentProductionQueue) {
        workers += queued.itemID==Unit_Harvester;
        transports += queued.itemID==Unit_Carryall;
    }
    if (workers!=9 || transports!=1 || house->getCredits()!=0 || workerPort->okToOrder()
        || workerPort->currentProductionQueue.front().itemID!=Unit_Carryall)
        throw std::runtime_error("Human-allied Brutal failed to fill 15-worker fleet with nine bargain imports");
    SDL_Log("STARPORT_ALLIED_WORKERS_PASS: first carryall at 800, then six existing + nine workers at 180 = 15; paid 2420");
    // A factory must be able to spend that reserve on a worker, rather than
    // freezing behind its own protection or choosing a military unit/upgrade.
    auto* factory = dynamic_cast<BuilderBase*>(place(Structure_HeavyFactory));
    if (!factory) throw std::runtime_error("Worker factory fixture placement failed");
    factory->updateBuildList(); bot->harvesterLimit = 15; bot->lastCalculatedSpice = 200000;
    bot->rockExpansionSite = Coord::Invalid(); bot->rockSurveyCycle = game.getGameCycleCount();
    const int workerPrice = game.objectData.data[Unit_Harvester][HOUSE_HARKONNEN].price;
    house->takeCredits(house->getCredits()); house->addCredits(workerPrice);
    // This case isolates the worker reserve from the helper army floor lever, under which a
    // Brutal helper without an army funds ground units before workers.
    const auto savedWorkerConfig = bot->configOverride;
    auto workerConfig = std::make_shared<QuantBotConfig>(bot->botConfig());
    workerConfig->force.campaignHelperBrutalArmyFloor = 0;
    bot->configOverride = workerConfig;
    savedStructures = structureList;
    structureList.clear(); structureList.push_back(factory);
    try { bot->build(4000); } catch (...) { structureList = savedStructures; bot->configOverride = savedWorkerConfig; throw; }
    structureList = savedStructures; bot->configOverride = savedWorkerConfig;
    if (factory->getCurrentProducedItem() != Unit_Harvester || factory->isUpgrading())
        throw std::runtime_error("Factory failed to spend protected worker cash on a harvester");
    SDL_Log("FACTORY_WORKER_RESERVE_PASS: one worker's cash funds a harvester before military or upgrades");
    // Custom games buy workers and combat together, without the campaign
    // helper's full-fleet cash reservation or enemy seven-worker ceiling.
    game.gameType=GameType::CustomGame;
    bot->harvesterLimit=15;bot->lastCalculatedSpice=200000;
    if (bot->harvesterCountCeiling()!=0) throw std::runtime_error("Custom ally inherited campaign worker cap");
    for (int h=0;h<NUM_HOUSES;++h) if(auto* other=game.getHouse(h))
        for(const auto& player:other->getPlayerList()) if(auto* qb=dynamic_cast<QuantBot*>(player.get())) {
            qb->difficulty=QuantBot::Difficulty::Brutal;
            if(qb->harvesterCountCeiling()!=0) throw std::runtime_error("Custom opponent inherited campaign worker cap");
        }
    auto* mixedPort=dynamic_cast<StarPort*>(place(Structure_StarPort));
    for(auto& offer:market.availableItems) {
        offer.num=offer.itemID==Unit_Harvester ? 10 : offer.itemID==Unit_Tank ? 20 : 0;
        offer.price=offer.itemID==Unit_Harvester ? 180 : 100;
    }
    mixedPort->updateBuildList();house->takeCredits(house->getCredits());house->addCredits(1800);
    savedStructures=structureList;structureList.clear();structureList.push_back(mixedPort);
    try {bot->build(4000);}catch(...){structureList=savedStructures;throw;}
    structureList=savedStructures;
    int customWorkers=0,customTanks=0;
    for(const auto& queued:mixedPort->currentProductionQueue) {
        customWorkers+=queued.itemID==Unit_Harvester;customTanks+=queued.itemID==Unit_Tank;
    }
    if(customWorkers<1 || customTanks<1 || house->getCredits()<0
        || house->getCredits()!=1800-customWorkers*180-customTanks*100)
        throw std::runtime_error("Custom Starport failed to mix worker and army investment");
    SDL_Log("CUSTOM_MIXED_IMPORTS_PASS: workers=%d tanks=%d, campaign fleet reserve absent",customWorkers,customTanks);
    // Market prices can refresh before this port's build list does. The real
    // queue charges the displayed offer: never budget the newer market quote.
    auto* quotedPort=dynamic_cast<StarPort*>(place(Structure_StarPort));
    for (auto& offer:market.availableItems) {
        offer.num=offer.itemID==Unit_Tank ? 20 : 0;
        offer.price=120;
    }
    quotedPort->updateBuildList();
    for (auto& offer:market.availableItems) offer.price=80;
    bot->lastCalculatedSpice=0;
    house->takeCredits(house->getCredits());house->addCredits(350);
    savedStructures=structureList;structureList.clear();structureList.push_back(quotedPort);
    try {bot->build(4000);}catch(...){structureList=savedStructures;throw;}
    structureList=savedStructures;
    if (quotedPort->currentProductionQueue.size()!=2 || house->getCredits()!=110)
        throw std::runtime_error("Shared budget did not use Starport's actual quoted price");
    for (const auto& order:quotedPort->currentProductionQueue)
        if (order.itemID!=Unit_Tank || order.price!=120)
            throw std::runtime_error("Starport quote fixture ordered unexpected cargo");
    SDL_Log("STARPORT_QUOTED_PRICE_PASS: market 80, displayed/charged 120; 350 funds two tanks with 110 remaining");
    // Reported opening: workers sold out, first transport available for 640,
    // but optional construction gets the money first. Exercise both the custom
    // and allied campaign planning paths with real yard/Starport queues.
    BuilderBase* openingYard=nullptr;
    for(auto* building:structureList)
        if(building->getOwner()==house && building->getItemID()==Structure_ConstructionYard)
            openingYard=static_cast<BuilderBase*>(building);
    if(!openingYard) throw std::runtime_error("No yard for carryall priority fixture");
    openingYard->upgrading=false;openingYard->updateBuildList();
    // The first transport's peaceful savings priority is distinct from an
    // emergency defence upgrade against the native campaign's visible army.
    const int team=house->getTeamID();
    std::vector<UnitBase*> visibleHostiles;
    for(auto* unit:unitList)
        if(unit->getOwner() && unit->getOwner()->getTeamID()!=team
            && unit->canAttack() && unit->isVisible(team)) visibleHostiles.push_back(unit);
    auto restoreHostiles=[&] {
        for(auto* unit:visibleHostiles)unit->setVisible(team,true);
        bot->dangerUpdated=std::numeric_limits<Uint32>::max();
    };
    auto buildOpeningTransport=[&] {
        for(auto* unit:visibleHostiles)unit->setVisible(team,false);
        bot->dangerUpdated=std::numeric_limits<Uint32>::max();
        try {
            for(const auto* unit:unitList)
                if(unit->getOwner() && unit->getOwner()->getTeamID()!=team
                    && unit->canAttack() && unit->isVisible(team))
                    throw std::runtime_error("Peaceful carryall reserve fixture still observes a hostile army");
            bot->build(4000);
        }catch(...){restoreHostiles();throw;}
        restoreHostiles();
    };
    for(const auto gameType:{GameType::CustomGame,GameType::Campaign}) {
        game.gameType=gameType;bot->gameMode=QuantBot::GameMode::Custom;
        auto* transportPort=dynamic_cast<StarPort*>(place(Structure_StarPort));
        auto* secondPort=dynamic_cast<StarPort*>(place(Structure_StarPort));
        for(auto& offer:market.availableItems) {
            offer.num=offer.itemID==Unit_Carryall ? 5 : offer.itemID==Unit_Tank ? 5 : 0;
            offer.price=offer.itemID==Unit_Carryall ? 640 : 100;
        }
        transportPort->updateBuildList();secondPort->updateBuildList();
        house->producedPower=10000;bot->lastCalculatedSpice=200000;
        house->takeCredits(house->getCredits());house->addCredits(639);
        savedStructures=structureList;structureList.clear();
        structureList.push_back(openingYard);structureList.push_back(transportPort);structureList.push_back(secondPort);
        try {
            buildOpeningTransport();
            if(transportPort->getProductionQueueSize() || secondPort->getProductionQueueSize()
                || openingYard->getProductionQueueSize() || house->getCredits()!=639)
                throw std::runtime_error("Cheap troops or construction consumed first carryall savings");
            house->addCredits(361);
            buildOpeningTransport();
        }catch(...){structureList=savedStructures;throw;}
        structureList=savedStructures;
        int firstTransports=0;
        for(auto* candidate:{transportPort,secondPort})for(const auto& order:candidate->currentProductionQueue)
            firstTransports+=order.itemID==Unit_Carryall;
        if(firstTransports!=1 || transportPort->currentProductionQueue.empty()
            || transportPort->currentProductionQueue.front().itemID!=Unit_Carryall || transportPort->okToOrder())
            throw std::runtime_error("First carryall lost priority or was duplicated across Starports");
        if(openingYard->getCurrentProducedItem()==Structure_RepairYard)
            throw std::runtime_error("Repair yard spent the first carryall's cash");
        while(openingYard->getProductionQueueSize())openingYard->doCancelItem(openingYard->getCurrentProducedItem());
        bot->builderPlaceLocations.clear();bot->reservedStructures.clear();
        SDL_Log("STARPORT_FIRST_TRANSPORT_PASS: sold-out workers, first carryall before repair yard, two ports, mode=%d",int(gameType));
    }
    // Reported emergency: the base is reduced to a Starport with cash and
    // stock — no construction yard, no heavy factory and no MCV anywhere.
    // Recovery is not expansion: every difficulty must convert that cash into
    // exactly one MCV, using the cash the other queues normally hold back, and
    // must keep saving while it is short or the market is sold out.
    game.gameType=GameType::CustomGame;bot->gameMode=QuantBot::GameMode::Custom;
    std::vector<ObjectBase*> stranded;
    for(auto* building:structureList)
        if(building->getOwner()==house && (building->getItemID()==Structure_ConstructionYard
            || building->getItemID()==Structure_HeavyFactory)) stranded.push_back(building);
    for(auto* unit:unitList)
        if(unit->getOwner()==house && unit->getItemID()==Unit_MCV) stranded.push_back(unit);
    for(auto* object:stranded) object->destroy();
    bot->builderPlaceLocations.clear();bot->reservedStructures.clear();bot->clearPlacementCache();
    if(house->getNumItems(Structure_ConstructionYard) || house->getNumItems(Structure_HeavyFactory)
        || house->getNumItems(Unit_MCV))
        throw std::runtime_error("Recovery fixture still owns a yard, heavy factory or MCV");
    const int mcvPrice=900;
    auto setOffer=[&](Uint32 item,int stock,int price) {
        for(auto& offer:market.availableItems) if(offer.itemID==item) {offer.num=stock;offer.price=price;return;}
        market.addItem(item,stock);market.availableItems.back().price=price;
    };
    // Cheap workers, transports and troops stay on sale throughout: saving for
    // the MCV must beat all of them, not merely an empty market.
    auto stockRecoveryMarket=[&](int mcvStock) {
        for(auto& offer:market.availableItems) {offer.num=0;offer.price=100;}
        setOffer(Unit_MCV,mcvStock,mcvPrice);setOffer(Unit_Harvester,5,180);
        setOffer(Unit_Carryall,5,200);setOffer(Unit_Tank,5,100);
    };
    auto planRecovery=[&](std::initializer_list<StructureBase*> ports) {
        savedStructures=structureList;structureList.clear();
        for(auto* member:ports) structureList.push_back(member);
        try {bot->build(4000);}catch(...){structureList=savedStructures;throw;}
        structureList=savedStructures;
    };
    auto ordered=[](const StarPort* seller,Uint32 item) {
        int count=0;
        for(const auto& order:seller->currentProductionQueue) count+=order.itemID==item;
        return count;
    };
    bot->harvesterLimit=15;bot->lastCalculatedSpice=200000;
    for(auto tier:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                   QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
        bot->difficulty=tier;
        auto* recoveryPort=dynamic_cast<StarPort*>(place(Structure_StarPort));
        if(!recoveryPort) throw std::runtime_error("Recovery Starport fixture failed");
        stockRecoveryMarket(1);recoveryPort->updateBuildList();
        if(!recoveryPort->isAvailableToBuild(Unit_MCV))
            throw std::runtime_error("Recovery fixture Starport cannot offer an MCV");
        house->takeCredits(house->getCredits());house->addCredits(mcvPrice-1);
        planRecovery({recoveryPort});
        if(recoveryPort->getProductionQueueSize() || house->getCredits()!=mcvPrice-1 || !recoveryPort->okToOrder())
            throw std::runtime_error("Recovery savings were spent one credit short of the MCV");
        house->addCredits(1);
        planRecovery({recoveryPort});
        if(ordered(recoveryPort,Unit_MCV)!=1 || recoveryPort->getProductionQueueSize()!=1
            || house->getCredits()!=0 || recoveryPort->okToOrder())
            throw std::runtime_error("Starport did not buy the recovery MCV at its exact market price");
        SDL_Log("STARPORT_RECOVERY_MCV_PASS: difficulty=%d bought one MCV at %d with no yard, factory or MCV",
            int(tier),mcvPrice);
    }
    // Sold out: hold that cash rather than convert it, and buy on restock.
    auto* restockRecovery=dynamic_cast<StarPort*>(place(Structure_StarPort));
    if(!restockRecovery) throw std::runtime_error("Restock recovery Starport fixture failed");
    stockRecoveryMarket(0);restockRecovery->updateBuildList();
    house->takeCredits(house->getCredits());house->addCredits(mcvPrice);
    planRecovery({restockRecovery});
    if(restockRecovery->getProductionQueueSize() || house->getCredits()!=mcvPrice || !restockRecovery->okToOrder())
        throw std::runtime_error("Sold-out MCV reserve was spent on other imports");
    stockRecoveryMarket(1);restockRecovery->updateBuildList();
    planRecovery({restockRecovery});
    if(ordered(restockRecovery,Unit_MCV)!=1 || house->getCredits()!=0 || restockRecovery->okToOrder())
        throw std::runtime_error("Restocked MCV was not bought with the saved cash");
    SDL_Log("STARPORT_RECOVERY_RESTOCK_PASS: %d saved while the MCV was sold out, spent on restock",mcvPrice);
    // One MCV recovers the base: two funded ports must not both order one, and
    // an idle port must not duplicate cargo another port is already awaiting.
    auto* firstRecovery=dynamic_cast<StarPort*>(place(Structure_StarPort));
    auto* secondRecovery=dynamic_cast<StarPort*>(place(Structure_StarPort));
    if(!firstRecovery || !secondRecovery) throw std::runtime_error("Duplicate recovery Starport fixture failed");
    stockRecoveryMarket(2);
    for(auto& offer:market.availableItems) if(offer.itemID!=Unit_MCV) offer.num=0;
    firstRecovery->updateBuildList();secondRecovery->updateBuildList();
    house->takeCredits(house->getCredits());house->addCredits(2*mcvPrice);
    planRecovery({firstRecovery,secondRecovery});
    if(ordered(firstRecovery,Unit_MCV)+ordered(secondRecovery,Unit_MCV)!=1 || house->getCredits()!=mcvPrice)
        throw std::runtime_error("Recovery MCV was duplicated across idle Starports");
    auto* deliveringPort=firstRecovery->getProductionQueueSize() ? firstRecovery : secondRecovery;
    auto* idleRecovery=deliveringPort==firstRecovery ? secondRecovery : firstRecovery;
    if(deliveringPort->okToOrder() || !idleRecovery->okToOrder() || idleRecovery->getProductionQueueSize())
        throw std::runtime_error("Recovery order was not dispatched from a single Starport");
    planRecovery({deliveringPort,idleRecovery});
    if(idleRecovery->getProductionQueueSize() || house->getCredits()!=mcvPrice)
        throw std::runtime_error("Idle Starport duplicated the MCV awaiting delivery");
    SDL_Log("STARPORT_RECOVERY_SINGLE_ORDER_PASS: one MCV from two funded ports, none added while delivery is pending");
    // An unpaid optional factory queue must not hide money that can already
    // buy the emergency MCV. Process that factory alongside the port too.
    auto* optionalFactory=dynamic_cast<BuilderBase*>(place(Structure_LightFactory));
    auto* queuedCashPort=dynamic_cast<StarPort*>(place(Structure_StarPort));
    if(!optionalFactory || !queuedCashPort) throw std::runtime_error("Queued-cash recovery fixture failed");
    optionalFactory->updateBuildList();optionalFactory->doProduceItem(Unit_Quad);
    if(optionalFactory->getProductionQueueSize()!=1)
        throw std::runtime_error("Optional factory queue was not established");
    stockRecoveryMarket(1);queuedCashPort->updateBuildList();
    house->takeCredits(house->getCredits());house->addCredits(mcvPrice);
    planRecovery({optionalFactory,queuedCashPort});
    if(ordered(queuedCashPort,Unit_MCV)!=1 || house->getCredits()!=0
        || optionalFactory->getProductionQueueSize()!=1 || queuedCashPort->okToOrder())
        throw std::runtime_error("Optional factory spending blocked emergency MCV recovery");
    SDL_Log("STARPORT_RECOVERY_QUEUED_CASH_PASS: unspent factory queue budget buys the emergency MCV");
    // Real construction decisions with an empty CHOAM market. Isolate the
    // tech priority from unrelated economic purchases, retaining real queues,
    // configured prerequisites, placement, availability and all difficulties.
    std::vector<StructureBase*> oldPorts;
    for (auto* structure : structureList)
        if (structure->getOwner()==house && structure->getItemID()==Structure_StarPort) oldPorts.push_back(structure);
    for (auto* structure : oldPorts) structure->destroy();
    market.availableItems.clear();
    // The late technology cases run as a custom game, unlike the initial
    // campaign import cases. Supply their independent population prerequisite
    // without disabling the rule or weakening any empty-market tech assertion.
    auto* techCity=game.isCitySimEnabled() ? game.getCitySimulation() : nullptr;
    if(game.isCitySimEnabled() && !techCity) throw std::runtime_error("Tech fixture needs its active city census");
    std::array<int,3> savedTechPopulation{};
    if(techCity) {
        auto& state=techCity->getHouseStateMut(house->getHouseID());
        savedTechPopulation={state.resPop,state.comPop,state.indPop};
        state.resPop=(DuneCity::kStarportPopulation+DuneCity::CitySimulation::kPopDisplayMultiplier-1)
            /DuneCity::CitySimulation::kPopDisplayMultiplier;
        state.comPop=0;state.indPop=0;
        if(!house->meetsStarportPopulation()) throw std::runtime_error("Tech fixture did not meet the Starport population prerequisite");
    }
    for(int y=0;y<currentGameMap->getSizeY();++y) for(int x=0;x<currentGameMap->getSizeX();++x)
        if(!currentGameMap->getTile(x,y)->hasAStructure()) currentGameMap->getTile(x,y)->setType(Terrain_Rock);
    auto* techYard=static_cast<BuilderBase*>(place(Structure_ConstructionYard));
    auto* parallelYard=static_cast<BuilderBase*>(place(Structure_ConstructionYard));
    for (Uint32 item : {Structure_WindTrap, Structure_Refinery, Structure_Radar,
                       Structure_LightFactory, Structure_HeavyFactory, Structure_HighTechFactory, Structure_RepairYard})
        if (!house->getNumItems(item)) place(item);
    const auto originalData=game.objectData;
    auto clearOrders=[&]() {
        for (auto* yard : {techYard,parallelYard}) {
            while (yard->getProductionQueueSize()) yard->doCancelItem(yard->currentProductionQueue.front().itemID);
            yard->upgrading=false;yard->curUpgradeLev=3;
        }
        bot->builderPlaceLocations.clear();bot->reservedStructures.clear();bot->clearPlacementCache();
    };
    auto countOrders=[&](Uint32 item) {
        int count=0;
        for (auto* yard : {techYard,parallelYard})
            for (const auto& order : yard->currentProductionQueue) count+=order.itemID==item;
        return count;
    };
    auto planTech=[&]() {
        techYard->updateBuildList();parallelYard->updateBuildList();
        if (!techYard->isAvailableToBuild(Structure_StarPort) && house->getNumItems(Structure_StarPort)==0)
            throw std::runtime_error("Fixture Starport unavailable: tech="+std::to_string(game.techLevel)+" upgrade="+std::to_string(techYard->getCurrentUpgradeLevel()));
        savedStructures=structureList;structureList.clear();
        structureList.push_back(techYard);structureList.push_back(parallelYard);
        try {bot->build(4000);}catch(...){structureList=savedStructures;throw;}
        structureList=savedStructures;
    };
    for (Uint32 goal : {Structure_IX,Structure_Palace}) {
        for (auto tier : {QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                          QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            game.objectData=originalData;
            for (int item=ItemID_FirstID;item<Num_ItemID;++item)
                game.objectData.data[item][HOUSE_HARKONNEN].enabled=false;
            auto& goalData=game.objectData.data[goal][HOUSE_HARKONNEN];
            auto& portData=game.objectData.data[Structure_StarPort][HOUSE_HARKONNEN];
            goalData.enabled=true;portData.enabled=true;
            // Keep the foundations needed by these legal tech orders enabled.
            // This fixture disables unrelated purchases, not required concrete.
            game.objectData.data[Structure_Slab1][HOUSE_HARKONNEN].enabled=true;
            game.objectData.data[Structure_Slab4][HOUSE_HARKONNEN].enabled=true;
            clearOrders();bot->difficulty=tier;bot->lastCalculatedSpice=0;
            house->producedPower=100000;house->takeCredits(house->getCredits());house->startingCredits=10000;
            // Unreachable tech must not justify an empty port.
            const int goalTech=goalData.techLevel;goalData.techLevel=game.techLevel+1;
            planTech();
            if(countOrders(Structure_StarPort)) throw std::runtime_error("Locked tech bought empty Starport");
            clearOrders();goalData.techLevel=goalTech;
            goalData.enabled=false;planTech();
            if(countOrders(Structure_StarPort)) throw std::runtime_error("Disabled tech bought empty Starport");
            clearOrders();goalData.enabled=true;
            const int goalUpgrade=goalData.upgradeLevel;
            goalData.upgradeLevel=techYard->getCurrentUpgradeLevel()+1;planTech();
            if(countOrders(Structure_StarPort)) throw std::runtime_error("Upgrade-locked tech bought empty Starport");
            clearOrders();goalData.upgradeLevel=goalUpgrade;
            goalData.prerequisiteStructuresSet[Structure_StarPort]=false;planTech();
            if(countOrders(Structure_StarPort)) throw std::runtime_error("Tech without Starport dependency bought empty port");
            clearOrders();goalData.prerequisiteStructuresSet[Structure_StarPort]=true;
            planTech();
            if(countOrders(Structure_StarPort)!=1 || countOrders(goal))
                throw std::runtime_error("Empty-market tech prerequisite did not order exactly one Starport");
            planTech();
            if(countOrders(Structure_StarPort)!=1) throw std::runtime_error("Pending Starport duplicated");
            clearOrders();
            auto* completedPort=place(Structure_StarPort);
            planTech();
            if(countOrders(goal)!=1 || countOrders(Structure_StarPort))
                throw std::runtime_error("Tech goal did not resume after Starport completion");
            clearOrders();completedPort->destroy();
            SDL_Log("STARPORT_TECH_PREREQUISITE_PASS: goal=%u difficulty=%d empty market, one port, resumed goal",goal,int(tier));
        }
    }
    game.objectData=originalData;
    if(techCity) {
        auto& state=techCity->getHouseStateMut(house->getHouseID());
        state.resPop=savedTechPopulation[0];state.comPop=savedTechPopulation[1];state.indPop=savedTechPopulation[2];
    }
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=starport outcome=passed");
}
