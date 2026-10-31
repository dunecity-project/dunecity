#include <dunecity/PoliceCoveragePolicy.h>
#include <players/RocketTurretPolicy.h>
// Compare the selected station against all legal sites using the actual police
// field. The settlement sits outside the first large-map scan stripe.
void runPolicePlacementProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    QuantBot* bot=nullptr;
    for(const auto& p:house->getPlayerList())if(auto* q=dynamic_cast<QuantBot*>(p.get()))bot=q;
    require(bot && game.isCitySimEnabled(),"Police fixture needs city QuantBot");
    game.gameType=GameType::CustomGame;bot->gameMode=QuantBot::GameMode::Custom;bot->role=AIRole::Both;
    Coord origin=Coord::Invalid();
    for(int y=currentGameMap->getSizeY()/3;y+24<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for(int x=4;x+24<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear=true;
            for(int dy=0;dy<24;++dy)for(int dx=0;dx<24;++dx)
                if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject())clear=false;
            if(clear)origin=Coord(x,y);
        }
    require(origin.isValid(),"Police fixture needs empty settlement space");
    for(int y=origin.y;y<origin.y+24;++y)for(int x=origin.x;x<origin.x+24;++x)
        currentGameMap->getTile(x,y)->setType(Terrain_Rock);
    auto* yard=static_cast<BuilderBase*>(house->placeStructure(NONE_ID,Structure_ConstructionYard,
        origin.x+10,origin.y+2,true,true));
    require(yard,"Police fixture yard failed");
    std::vector<StructureBase*> properties{yard};
    for(Coord offset:{Coord(5,9),Coord(9,9),Coord(13,9),Coord(5,13),Coord(9,13),Coord(13,13)}) {
        auto* zone=house->placeStructure(NONE_ID,Structure_ZoneResidential,
            origin.x+offset.x,origin.y+offset.y,true,true);
        require(zone,"Police fixture residence failed");
        static_cast<ZoneStructure*>(zone)->setResidentialPopulation(40);
        properties.push_back(zone);
    }
    const auto prerequisites=game.objectData.data[Structure_PoliceStation][HOUSE_HARKONNEN].prerequisiteStructuresSet
        | game.objectData.data[Structure_RocketTurret][HOUSE_HARKONNEN].prerequisiteStructuresSet;
    for(unsigned item=0;item<prerequisites.size();++item)if(prerequisites.test(item) && !house->getNumItems(item)) {
        bool placed=false;
        const auto footprint=getStructureSize(item);
        for(int y=2;y+footprint.y<origin.y && !placed;++y)for(int x=2;x+footprint.x<currentGameMap->getSizeX() && !placed;++x) {
            bool clear=true;
            for(int dy=0;dy<footprint.y;++dy)for(int dx=0;dx<footprint.x;++dx)
                clear &= !currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject();
            if(clear)placed=house->placeStructure(NONE_ID,item,x,y,true,true)!=nullptr;
        }
        require(placed,"Police prerequisite could not be placed");
    }
    yard->setCityOccupancy(1);yard->curUpgradeLev=yard->getMaxUpgradeLevel();yard->updateBuildList();
    for(int y=origin.y;y<origin.y+24;++y)for(int x=origin.x;x<origin.x+24;++x)
        if(!currentGameMap->getTile(x,y)->hasAStructure())currentGameMap->getTile(x,y)->setRoad(true);
    auto saved=structureList;structureList.clear();for(auto* p:properties)structureList.push_back(p);
    auto* sim=game.getCitySimulation();house->producedPower=10000;
    auto& state=sim->houseState_[HOUSE_HARKONNEN];state.policeFundingPercent=100;
    state.avgLandValue=100;state.resValve=2000;
    for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x) {
        sim->crimeRateMap_.set(x/sim->crimeRateMap_.getBlockSize(),y/sim->crimeRateMap_.getBlockSize(),250);
        sim->crimeBeforePoliceMap_.set(x/sim->crimeBeforePoliceMap_.getBlockSize(),y/sim->crimeBeforePoliceMap_.getBlockSize(),250);
    }
    bot->planningBuilder=yard->getObjectID();bot->reservedStructures.clear();
    const Coord size=getStructureSize(Structure_PoliceStation);
    auto relief=[&](Coord p) {
        const auto source=DuneCity::policeSource(*currentGameMap,p.x,p.y,size.x,size.y,
            DuneCity::getPoliceCoverage(Structure_PoliceStation),100,true);
        int value=0;
        for(const auto* property:properties) {
            const Coord at=property->getLocation();
            value+=std::min(250,DuneCity::policeCoverageAt(source.x,source.y,at.x,at.y,2,
                source.strength,currentGameMap->getSizeX(),currentGameMap->getSizeY()));
        }
        return value;
    };
    try {
        int maximum=0;
        for(int y=origin.y;y<origin.y+24-size.y;++y)for(int x=origin.x;x<origin.x+24-size.x;++x) {
            if(!currentGameMap->okayToPlaceStructure(x,y,size.x,size.y,false,house,false,Structure_PoliceStation)
                || !bot->preservesGroundAccess(Structure_PoliceStation,Coord(x,y)))continue;
            // Keep a full tile of frontage around each residence. With the
            // fixture's solid road grid these reference sites cannot sever it.
            bool frontage=true;
            for(const auto* property:properties) {
                const auto at=property->getLocation(), footprint=property->getStructureSize();
                if(CityPlacementPolicy::overlaps(x,y,size.x,size.y,at.x-1,at.y-1,footprint.x+2,footprint.y+2))frontage=false;
            }
            if(!frontage)continue;
            maximum=std::max(maximum,relief(Coord(x,y)));
        }
        require(maximum>0,"Police reference field has no useful legal location");
        for(Uint32 cycle:{0u,100u,700u,1500u}) {
            game.gameCycleCount=cycle;bot->cityServiceSearch.reset();bot->clearPlacementCache();
            Uint32 item=NONE_ID;Coord selected=Coord::Invalid();
            require(bot->selectCityServiceInvestment(yard,10000,true,item,selected,false,Structure_PoliceStation),
                "Occupied district outside current scan stripe lost its police candidate");
            require(item==Structure_PoliceStation && relief(selected)>=maximum,
                "Police selected a fringe site that removes less crime than a legal central site");
        }
        // Routine crime should prefer the turret's combined civic and defence
        // benefits; the strong police station is reserved for dangerous crime.
        for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x) {
            sim->crimeRateMap_.set(x/sim->crimeRateMap_.getBlockSize(),y/sim->crimeRateMap_.getBlockSize(),170);
            sim->crimeBeforePoliceMap_.set(x/sim->crimeBeforePoliceMap_.getBlockSize(),y/sim->crimeBeforePoliceMap_.getBlockSize(),170);
        }
        bot->cityServiceSearch.reset();bot->clearPlacementCache();
        Uint32 routine=NONE_ID;Coord routineSite=Coord::Invalid();
        require(!bot->selectCityServiceInvestment(yard,10000,true,routine,routineSite,false,Structure_PoliceStation),
            "Routine crime spent on a police station instead of reserving it for dangerous hotspots");
        require(bot->selectCityServiceInvestment(yard,10000,true,routine,routineSite)
            && routine==Structure_RocketTurret,"Routine crime did not prefer useful rocket coverage");
        // With one isolated high-value target, Medium/Hard need two covers,
        // Brutal three. Reserved emplacements count before construction ends.
        structureList.clear();structureList.push_back(yard);
        for(auto difficulty:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                              QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            bot->difficulty=difficulty;bot->reservedStructures.clear();
            const int covers=difficulty==QuantBot::Difficulty::Easy ? 1
                : difficulty==QuantBot::Difficulty::Brutal ? 3 : 2;
            for(int planned=0;planned<=covers;++planned) {
                bot->cityTurretSearch.reset();bot->clearPlacementCache();
                int defense=0;
                const auto site=bot->findCityTurretPlaceLocation(Structure_RocketTurret,&defense);
                require(planned==covers ? defense==0 : site.isValid() && defense>0,
                    "Rocket coverage stopped early or ignored reserved overlapping defence");
                if(planned<covers)bot->reservedStructures[yard->getObjectID()+100+planned]={Structure_RocketTurret,site};
            }
        }
        // An outlying yard gets three covers at every difficulty. Cover the
        // original anchor in advance so the test measures the expansion only.
        auto* anchor=yard;
        auto* expansion=house->placeStructure(NONE_ID,Structure_ConstructionYard,
            origin.x+20,origin.y+20,true,true);
        require(expansion,"Expansion fixture yard could not be placed");
        saved.push_back(expansion);
        structureList.clear();structureList.push_back(anchor);structureList.push_back(expansion);
        const int radius=std::max(1,game.objectData.data[Structure_RocketTurret][house->getHouseID()].weaponrange-1);
        for(auto difficulty:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                              QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            bot->difficulty=difficulty;bot->reservedStructures.clear();
            for(int n=0;n<3;++n)
                bot->reservedStructures[yard->getObjectID()+300+n]={Structure_RocketTurret,anchor->getLocation()+Coord(n,0)};
            for(int planned=0;planned<=3;++planned) {
                bot->cityTurretSearch.reset();bot->clearPlacementCache();
                int defense=0;
                const auto site=bot->findCityTurretPlaceLocation(Structure_RocketTurret,&defense);
                require(planned==3 ? defense==0 : site.isValid() && defense>0
                    && RocketTurretPolicy::coversBuilding(site,expansion->getLocation(),expansion->getStructureSize(),radius),
                    "Expansion did not receive exactly three planned rocket covers");
                if(planned<3)bot->reservedStructures[yard->getObjectID()+400+planned]={Structure_RocketTurret,site};
            }
        }
        // Planned and finished covers both still go up beside the expansion,
        // but neither holds back the next colonist: rock without turrets is
        // exactly where a colony starts, and its own yard builds the cover.
        std::vector<std::pair<Uint32,int>> coreCounts;
        for(Uint32 item:{Structure_HeavyFactory,Structure_HighTechFactory,Structure_RepairYard}) {
            coreCounts.push_back({item,house->numItem[item]});house->numItem[item]=std::max(1,house->numItem[item]);
        }
        require(bot->expansionDefenceReady(),"An uncovered expansion held back the next colonist");
        for(int n=0;n<3;++n) {
            const auto site=bot->reservedStructures.at(yard->getObjectID()+400+n).location;
            auto* turret=house->placeStructure(NONE_ID,Structure_RocketTurret,site.x,site.y,true,true);
            require(turret,"Could not finish expansion turret fixture");saved.push_back(turret);
            require(bot->expansionDefenceReady(),"A partly covered expansion held back the next colonist");
        }
        for(const auto& count:coreCounts)house->numItem[count.first]=count.second;
        bot->planningBuilder=yard->getObjectID();
        // Growing city districts also need overlapping general-purpose cover
        // on Medium and above, without escalating the first core upgrade path.
        structureList.clear();structureList.push_back(properties.at(1));
        for(auto difficulty:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                              QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            bot->difficulty=difficulty;bot->reservedStructures.clear();
            const int covers=difficulty==QuantBot::Difficulty::Easy ? 1 : 2;
            for(int planned=0;planned<=covers;++planned) {
                bot->cityTurretSearch.reset();bot->clearPlacementCache();
                int defense=0;
                const auto site=bot->findCityTurretPlaceLocation(Structure_RocketTurret,&defense);
                require(planned==covers ? defense==0 : site.isValid() && defense>0,
                    "Residential rocket overlap stopped early or ignored planned coverage");
                if(planned<covers)bot->reservedStructures[yard->getObjectID()+200+planned]={Structure_RocketTurret,site};
            }
        }
        bot->reservedStructures.clear();
    } catch(...) {structureList=saved;throw;}
    structureList=saved;
    SDL_Log("POLICE_PLACEMENT_PASS: station maximizes crime relief; rocket overlap and reservations work on all difficulties");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=police_placement outcome=passed");
}
