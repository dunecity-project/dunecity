// Real growth-loop regression. Included only by the native diagnostic harness.
#include <players/QuantBotCityPolicy.h>

void runCustomCityCapProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok){SDL_Log("CUSTOM_CITY_CAP_PROBE_FAIL: %s",why);throw std::runtime_error(why);}};
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    QuantBot* bot=nullptr;
    for(const auto& player:house->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get())) bot=candidate;
    require(bot,"City cap fixture needs a QuantBot");
    game.citySimEnabled_=true;
    if(!game.citySimulation_) {
        game.citySimulation_=std::make_unique<DuneCity::CitySimulation>();
        game.citySimulation_->init(currentGameMap->getSizeX(),currentGameMap->getSizeY());
    }
    // This diagnostic starts in a Vanilla campaign. Enable the city-zone
    // products alongside the city simulation we explicitly enabled above.
    for(Uint32 item:{Structure_ZoneResidential,Structure_ZoneCommercial,Structure_ZoneIndustrial}) {
        auto& data=game.objectData.data[item][house->getHouseID()];
        data.enabled=true;data.builder=Structure_ConstructionYard;
        data.techLevel=1;data.upgradeLevel=0;data.prerequisiteStructuresSet.reset();
    }
    game.gameType=GameType::CustomGame;
    bot->gameMode=QuantBot::GameMode::Custom;bot->role=AIRole::Both;
    bot->difficulty=QuantBot::Difficulty::Easy;
    const int area=currentGameMap->getSizeX()*currentGameMap->getSizeY();
    require(bot->getCityPopulationLimit(area)==QuantBotCityPolicy::populationLimit(QuantBotCityPolicy::Easy,area),
        "Shared Easy helper is missing its city ceiling");
    bot->difficulty=QuantBot::Difficulty::Medium;
    const int mediumCap=QuantBotCityPolicy::populationLimit(QuantBotCityPolicy::Medium,area);
    require(bot->getCityPopulationLimit(area)==mediumCap,"Shared Medium helper is missing its city ceiling");
    bot->role=AIRole::Builder;
    require(bot->getCityPopulationLimit(area)==mediumCap,"Support helper is missing its city ceiling");
    bot->role=AIRole::Both;
    bot->difficulty=QuantBot::Difficulty::Hard;
    require(bot->getCityPopulationLimit(256*256)==120000,"Hard256 map ceiling must be120000");
    const int hardCap=QuantBotCityPolicy::populationLimit(QuantBotCityPolicy::Hard,area);
    require(bot->getCityPopulationLimit(area)==hardCap,"Shared Hard helper is missing its city ceiling");
    bot->difficulty=QuantBot::Difficulty::Easy;
    std::list<std::unique_ptr<Player>> humanPlayers;
    struct RestoreHumans {
        std::list<std::unique_ptr<Player>>& players;
        std::list<std::unique_ptr<Player>>& removed;
        ~RestoreHumans() { players.splice(players.end(),removed); }
    } restoreHumans{house->players,humanPlayers};
    for(auto it=house->players.begin();it!=house->players.end();) {
        auto current=it++;
        if(dynamic_cast<HumanPlayer*>(current->get())) humanPlayers.splice(humanPlayers.end(),house->players,current);
    }
    require(!humanPlayers.empty(),"City cap fixture needs a human sharing the helper's house");
    const int cap=bot->getCityPopulationLimit(area);
    require(cap>0,"Easy has no city ceiling");
    bot->difficulty=QuantBot::Difficulty::Brutal;
    require(bot->getCityPopulationLimit(area)==0,"Brutal has a city ceiling");
    bot->difficulty=QuantBot::Difficulty::Easy;
    game.gameType=GameType::Campaign;
    require(bot->getCityPopulationLimit(area)==0,"Campaign helper inherited custom city ceiling");
    game.gameType=GameType::CustomGame;
    auto place=[&]() -> ZoneStructure* {
        for(int y=1;y+3<currentGameMap->getSizeY();y+=3)
            for(int x=1;x+3<currentGameMap->getSizeX();x+=3) {
                bool clear=true;
                for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
                    if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject()) clear=false;
                if(clear) return dynamic_cast<ZoneStructure*>(house->placeStructure(
                    NONE_ID,Structure_ZoneResidential,x,y,true,true));
            }
        return nullptr;
    };
    // Existing oversize cities are preserved, but cannot continue growing.
    // The city is matured past the Medium ceiling as well, so both capped
    // difficulties meet the same oversized save.
    for(int i=0;i<hardCap/800+4;++i) {
        auto* zone=place();require(zone,"No mature city fixture site");zone->setResidentialPopulation(40);
    }
    auto* vacant=place();require(vacant,"No vacant fixture site");vacant->setResidentialPopulation(0);
    auto* sim=game.getCitySimulation();
    auto ready=[&]() {
        house->producedPower=100000;
        auto& state=sim->houseState_[house->getHouseID()];
        state.resValve=2000;state.comValve=1500;state.indValve=1500;
        state.hasStadium=true;state.hasAirport=true;state.hasStarport=true;
    };
    // The ordinary planner must refuse population-bearing orders in the same
    // oversized city, using its own real queues, cash and placement survey.
    auto placeItem=[&](Uint32 item)->StructureBase* {
        const Coord size=getStructureSize(item);
        for(int y=1;y+size.y<currentGameMap->getSizeY();++y)
            for(int x=1;x+size.x<currentGameMap->getSizeX();++x) {
                bool clear=true;
                for(int dy=0;dy<size.y;++dy)for(int dx=0;dx<size.x;++dx)
                    if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject()) clear=false;
                if(clear) return house->placeStructure(NONE_ID,item,x,y,true,true);
            }
        return nullptr;
    };
    auto* yard=dynamic_cast<BuilderBase*>(placeItem(Structure_ConstructionYard));
    require(yard,"No construction yard fixture site");
    for(int y=0;y<currentGameMap->getSizeY();++y)
        for(int x=0;x<currentGameMap->getSizeX();++x)
            if(!currentGameMap->getTile(x,y)->hasAStructure())currentGameMap->getTile(x,y)->setType(Terrain_Rock);
    for(Uint32 item:{Structure_WindTrap,Structure_Refinery,Structure_LightFactory,
        Structure_HeavyFactory,Structure_HighTechFactory,Structure_Radar})
        if(!house->getNumItems(item))require(placeItem(item),"Missing city planning prerequisite");
    if(!house->getNumItems(Unit_Harvester))house->createUnit(Unit_Harvester,true);
    if(!house->getNumItems(Unit_Carryall))house->createUnit(Unit_Carryall,true);
    std::vector<BuilderBase*> yards;
    for(auto* structure:structureList)if(structure->getOwner()==house
        && structure->getItemID()==Structure_ConstructionYard)
        yards.push_back(static_cast<BuilderBase*>(structure));
    auto hasZone=[&]() {
        for(auto* builder:yards)for(const auto& order:builder->currentProductionQueue)
            if(DuneCity::isCityZoneStructure(order.itemID))return true;
        return false;
    };
    auto hasPopulationOrder=[&]() {
        for(auto* builder:yards)for(const auto& order:builder->currentProductionQueue)
            if(DuneCity::isCityZoneStructure(order.itemID)
                || DuneCity::getZonePopulation(order.itemID,1)>0
                || (order.itemID==Structure_Palace && DuneCity::getPalaceCommercialPopulation(1)>0))return true;
        return false;
    };
    auto clearQueues=[&]() {
        for(auto* builder:yards) {
            while(builder->getProductionQueueSize())builder->doCancelItem(builder->getCurrentProducedItem());
            builder->upgrading=false;builder->curUpgradeLev=builder->getMaxUpgradeLevel();builder->updateBuildList();
        }
    };
    auto plan=[&](QuantBot::Difficulty difficulty) {
        clearQueues();
        bot->difficulty=difficulty;
        bot->builderPlaceLocations.clear();bot->reservedStructures.clear();bot->clearPlacementCache();
        bot->lastZoneTraceCycle.clear();
        bot->lastCalculatedSpice=0;bot->militaryValueLimit=10000;
        house->takeCredits(house->getCredits());house->addCredits(100000);
        sim->houseState_[house->getHouseID()].avgLandValue=160;
        ready();bot->build(10000);
    };
    for(auto difficulty:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,QuantBot::Difficulty::Hard}) {
        plan(difficulty);
        require(!hasPopulationOrder(),"Capped bot ordered city population above its ceiling");
    }
    plan(QuantBot::Difficulty::Brutal);
    require(hasZone(),"Uncapped helper cannot zone here, so the capped refusal proves nothing");
    clearQueues();
    bot->difficulty=QuantBot::Difficulty::Easy;
    ready();sim->runZoneGrowth();
    require(vacant->getResidentialPopulation()==0,"Easy city grew above population limit");
    bot->difficulty=QuantBot::Difficulty::Medium;
    ready();sim->runZoneGrowth();
    require(vacant->getResidentialPopulation()==0,"Medium city grew above population limit");
    bot->difficulty=QuantBot::Difficulty::Hard;
    ready();sim->runZoneGrowth();
    require(vacant->getResidentialPopulation()==0,"Hard city grew above population limit");
    bot->difficulty=QuantBot::Difficulty::Brutal;
    ready();sim->runZoneGrowth();
    require(vacant->getResidentialPopulation()>0,"Brutal growth was capped or fixture cannot grow");
    house->players.splice(house->players.end(),humanPlayers);
    // A human in the house stays unrestricted: the same oversized city still
    // accepts a newly placed lot and still grows it naturally, at every capped
    // helper difficulty, while the helper keeps its own ceiling.
    for(auto difficulty:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,QuantBot::Difficulty::Hard}) {
        plan(difficulty);
        require(bot->getCityPopulationLimit(area)>0,"Shared helper lost its ceiling beside a human");
        require(!hasPopulationOrder(),"Shared helper ordered city population above its ceiling");
        auto* shared=place();require(shared,"No human placement site above the ceiling");
        shared->setResidentialPopulation(0);
        ready();sim->runZoneGrowth();
        require(shared->getResidentialPopulation()>0,"Human-shared city stopped growing naturally");
        clearQueues();
        const HumanPlayer* human=nullptr;
        for(const auto& player:house->players)
            if(auto* candidate=dynamic_cast<const HumanPlayer*>(player.get()))human=candidate;
        require(human,"Shared city lost its human controller");
        human->doProduceItem(yard,Structure_ZoneResidential);
        require(hasZone(),"Human construction order inherited the helper's city limit");
        clearQueues();
    }
    bot->difficulty=QuantBot::Difficulty::Easy;
    SDL_Log("CUSTOM_CITY_CAP_PROBE_PASS: capped helper refuses orders and growth, human placement and natural growth continue, Brutal grows, campaign excluded");
}
