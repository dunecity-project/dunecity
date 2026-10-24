#include <players/CityPlacementPolicy.h>

// Real queues, market, placement, power and household accounting in both mods.
void runSharedSpendingProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok){SDL_Log("SHARED_SPENDING_FAIL: %s",why);throw std::runtime_error(why);}};
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    auto* bot=dynamic_cast<QuantBot*>(game.getPlayerByName("Harkonnen"));
    require(house && bot,"Missing fixture bot");
    game.gameType=GameType::CustomGame;
    bot->gameMode=QuantBot::GameMode::Custom;bot->difficulty=QuantBot::Difficulty::Brutal;
    bot->militaryValueLimit=20000;bot->harvesterLimit=12;
    auto place=[&](Uint32 item)->StructureBase* {
        const Coord size=getStructureSize(item);
        for(int y=2;y+size.y<currentGameMap->getSizeY();++y)for(int x=2;x+size.x<currentGameMap->getSizeX();++x) {
            bool clear=true;
            for(int dy=0;dy<size.y;++dy)for(int dx=0;dx<size.x;++dx)
                clear &= !currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject();
            if(clear)return house->placeStructure(NONE_ID,item,x,y,true,true);
        }
        throw std::runtime_error("No shared spending fixture space");
    };
    for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x)
        if(!currentGameMap->getTile(x,y)->hasAStructure())currentGameMap->getTile(x,y)->setType(Terrain_Rock);
    for(Uint32 item:{Structure_WindTrap,Structure_Radar,Structure_LightFactory,Structure_HeavyFactory,
        Structure_HighTechFactory,Structure_StarPort,Structure_RepairYard,Structure_IX,Structure_Palace})
        if(!house->getNumItems(item))require(place(item),"Prerequisite placement failed");
    while(house->getNumItems(Structure_Refinery)<4)place(Structure_Refinery);
    while(house->getNumItems(Unit_Harvester)<2)house->createUnit(Unit_Harvester,true);
    if(!house->getNumItems(Unit_Carryall))house->createUnit(Unit_Carryall,true);
    if(game.isCitySimEnabled()) {
        auto& hs=game.getCitySimulation()->houseState_[HOUSE_HARKONNEN];
        hs.resValve=2000;hs.comValve=1500;hs.indValve=1500;
        hs.avgLandValue=160;hs.taxBaseEighths=8000;
        hs.civicDemandBlocked=0;
    }
    BuilderBase *yard=nullptr,*heavy=nullptr;
    StructureBase* refinery=nullptr;
    for(auto* structure:structureList)if(structure->getOwner()==house) {
        if(structure->getItemID()==Structure_Refinery)refinery=structure;
        if(!structure->isABuilder())continue;
        auto* builder=static_cast<BuilderBase*>(structure);
        builder->upgrading=false;builder->updateBuildList();
        if(builder->getItemID()==Structure_ConstructionYard)yard=builder;
        if(builder->getItemID()==Structure_HeavyFactory)heavy=builder;
    }
    require(yard && heavy && refinery,"Missing real producers");
    // A nearby field makes workers worthwhile; a large remaining reserve is
    // supplied explicitly, just as the normal terrain survey does before build.
    for(int dy=-4;dy<=4;++dy)for(int dx=-4;dx<=4;++dx) {
        const int x=refinery->getX()+dx,y=refinery->getY()+dy;
        if(currentGameMap->tileExists(x,y) && !currentGameMap->getTile(x,y)->hasAGroundObject()) {
            currentGameMap->getTile(x,y)->setType(Terrain_Spice);
            currentGameMap->getTile(x,y)->setSpice(10000_fix);
        }
    }
    auto* secondYard=static_cast<BuilderBase*>(place(Structure_ConstructionYard));
    secondYard->upgrading=false;
    // Isolate parallel spending after the new early concrete upgrade. The
    // opening probe checks that upgrade; the explicit defensive-upgrade case
    // below still resets its yard to zero and verifies the transition.
    for(auto* constructionYard:{yard,secondYard}) {
        constructionYard->curUpgradeLev=constructionYard->getMaxUpgradeLevel();
        constructionYard->updateBuildList();
    }
    auto saved=structureList;
    auto reset=[&](int credits,int spice) {
        for(auto* builder:{yard,heavy,secondYard}) {
            while(builder->getProductionQueueSize())builder->doCancelItem(builder->getCurrentProducedItem());
            builder->upgrading=false;
            builder->setHealth(builder->getMaxHealth());
        }
        bot->builderPlaceLocations.clear();bot->reservedStructures.clear();bot->clearPlacementCache();
        bot->lastCalculatedSpice=spice;
        house->producedPower=10000;house->storedCredits=0;house->cityCredits=0;house->startingCredits=credits; // Explicit scenario funding, not an over-capacity spice deposit.
        structureList.clear();structureList.push_back(yard);structureList.push_back(heavy);structureList.push_back(refinery);
    };
    auto count=[&](BuilderBase* builder,Uint32 item) {
        return std::count_if(builder->currentProductionQueue.begin(),builder->currentProductionQueue.end(),
            [&](const auto& order){return order.itemID==item;});
    };
    const int price=game.objectData.data[Unit_Harvester][HOUSE_HARKONNEN].price;
    const bool originalRocketEnabled=game.objectData.data[Structure_RocketTurret][HOUSE_HARKONNEN].enabled;
    // Isolate generic parallel spending from the new proactive core defence.
    // Dedicated coverage/crime cases below restore the normal turret rules.
    if(game.isCitySimEnabled()) {
        game.objectData.data[Structure_RocketTurret][HOUSE_HARKONNEN].enabled=false;
        yard->updateBuildList();secondYard->updateBuildList();
    }
    try {
        reset(price,1000000);bot->build(10000);
        require(count(heavy,Unit_Harvester)==1,"Next worthwhile worker lost its exact purchase budget");
        require(yard->getProductionQueueSize()==0,"Yard spent the worker's protected cash");
        reset(price+1000,1000000);bot->build(10000);
        require(count(heavy,Unit_Harvester)==1,"Funded parallel worker missing");
        require(yard->getProductionQueueSize()>0,"Independent construction failed to use remaining cash");
        if(game.isCitySimEnabled()) {
            bool zone=false;
            for(const auto& order:yard->currentProductionQueue)zone|=DuneCity::isCityZoneStructure(order.itemID);
            require(zone,"City did not grow demanded R/I/C alongside worker");
        }
        if (game.isCitySimEnabled()) {
            const int yardUpgrade=yard->curUpgradeLev;
            const bool growthRocketEnabled=game.objectData.data[Structure_RocketTurret][HOUSE_HARKONNEN].enabled;
            game.objectData.data[Structure_RocketTurret][HOUSE_HARKONNEN].enabled=false;
            yard->curUpgradeLev=yard->getMaxUpgradeLevel();yard->updateBuildList();
            // Low-value land makes the next army purchase outrank the lot in
            // the raw four-minute forecast, reproducing the stagnant Fremen.
            game.getCitySimulation()->houseState_[HOUSE_HARKONNEN].avgLandValue=64;
            reset(218,0);bot->build(10000);
            bool fundedZone=false;
            for(const auto& order:yard->currentProductionQueue)fundedZone|=DuneCity::isCityZoneStructure(order.itemID);
            require(fundedZone,"Idle yard saved affordable city growth cash for a more expensive military unit");
            reset(1000,0);bot->build(10000);
            fundedZone=false;
            for(const auto& order:yard->currentProductionQueue)fundedZone|=DuneCity::isCityZoneStructure(order.itemID);
            require(fundedZone,"City growth lost its funding alongside the army");
            require(heavy->getProductionQueueSize()>0 || heavy->isUpgrading(),"Protected city growth starved independently affordable military production");
            game.objectData.data[Structure_RocketTurret][HOUSE_HARKONNEN].enabled=growthRocketEnabled;
            yard->curUpgradeLev=yardUpgrade;yard->updateBuildList();
            game.getCitySimulation()->houseState_[HOUSE_HARKONNEN].avgLandValue=160;
            reset(price+1000,1000000);
            structureList.push_back(secondYard);
            bot->build(10000);
            bool parallelZone=false;
            for (auto* builder:{yard,secondYard}) {
                bool economy=false;
                for (const auto& order:builder->currentProductionQueue) {
                    parallelZone|=DuneCity::isCityZoneStructure(order.itemID);
                    economy|=DuneCity::isCityZoneStructure(order.itemID) || order.itemID==Structure_Refinery;
                }
                require(economy,"Funded parallel yard failed to grow the tax or spice economy");
            }
            require(parallelZone,"Profitable refinery displaced every dedicated city-growth slot");
            require(count(heavy,Unit_Harvester)==1,"Parallel yards consumed the worker reserve");

            const int firstGrowthUpgrade=yard->curUpgradeLev, secondGrowthUpgrade=secondYard->curUpgradeLev;
            for(auto* builder:{yard,secondYard}) {
                builder->curUpgradeLev=builder->getMaxUpgradeLevel();builder->updateBuildList();
            }
            // Reproduce services taking every yard despite affordable demanded
            // lots. One real queue must keep zoning; the other still serves crime.
            auto& crimeMap=game.getCitySimulation()->crimeRateMap_;
            auto setCrime=[&](int value) {
                for(int y=0;y<currentGameMap->getSizeY();++y)
                    for(int x=0;x<currentGameMap->getSizeX();++x) {
                        crimeMap.set(x/crimeMap.getBlockSize(),y/crimeMap.getBlockSize(),value);
                        auto& baseCrime=game.getCitySimulation()->crimeBeforePoliceMap_;
                        baseCrime.set(x/baseCrime.getBlockSize(),y/baseCrime.getBlockSize(),value);
                    }
            };
            auto hasZone=[&](BuilderBase* builder) {
                return std::any_of(builder->currentProductionQueue.begin(),builder->currentProductionQueue.end(),
                    [](const auto& order){return DuneCity::isCityZoneStructure(order.itemID);});
            };
            std::vector<std::pair<StructureBase*,uint8_t>> occupancy;
            for(auto* structure:{static_cast<StructureBase*>(yard),static_cast<StructureBase*>(secondYard),refinery}) {
                occupancy.push_back({structure,structure->getCityOccupancy()});
                structure->setCityOccupancy(1);
            }
            setCrime(0);
            // The reported 100k opening must grow production before reaching
            // its six-yard target, while its dedicated yard still zones.
            reset(100000,0);structureList.push_back(secondYard);
            bot->militaryValueLimit=80000;
            heavy->doProduceItem(Unit_Tank);
            bot->build(1000);
            require(hasZone(yard),"Rich production displaced the dedicated R/I/C yard");
            require(count(secondYard,Structure_HeavyFactory)>0,
                "Cash-funded factory expansion waited for the construction-yard target");
            require(bot->cityProductionPlots.size()==2,"Did not preserve factory and repair plots on open rock");
            for(const auto& entry:bot->reservedStructures) {
                if (!DuneCity::isCityZoneStructure(entry.second.item)) continue;
                const auto p=entry.second.location, size=getStructureSize(entry.second.item);
                for(const auto& plot:bot->cityProductionPlots) {
                    const auto extent=getStructureSize(plot.item);
                    require(!CityPlacementPolicy::overlaps(p.x,p.y,size.x,size.y,
                        plot.location.x-1,plot.location.y-1,extent.x+2,extent.y+2),
                        "Zoning consumed a future production footprint or access");
                }
            }
            // With only two workers, ample runway must still unlock the MCV
            // that supplies parallel city construction; neither target waits
            // for the other to finish.
            reset(100000,1000000);structureList.push_back(secondYard);
            const int heavyUpgrade=heavy->curUpgradeLev;
            heavy->curUpgradeLev=heavy->getMaxUpgradeLevel();heavy->updateBuildList();
            bot->build(1000);
            require(count(heavy,Unit_MCV)>0,
                "Funded city expansion waited for the opening harvester target");
            heavy->curUpgradeLev=heavyUpgrade;heavy->updateBuildList();
            bot->militaryValueLimit=20000;
            game.objectData.data[Structure_RocketTurret][HOUSE_HARKONNEN].enabled=originalRocketEnabled;
            for(auto* builder:{yard,secondYard}) {
                builder->curUpgradeLev=builder->getMaxUpgradeLevel();builder->updateBuildList();
            }
            setCrime(250);
            reset(2000,0);structureList.push_back(secondYard);
            bot->build(10000);
            auto hasProtection=[&](BuilderBase* builder) {
                return count(builder,Structure_PoliceStation)>0 || count(builder,Structure_RocketTurret)>0;
            };
            require(hasProtection(yard) || hasProtection(secondYard),
                "Dangerous crime lost its funded protection slot to growth");

            reset(2000,0);structureList.push_back(secondYard);
            secondYard->doProduceItem(Structure_ZoneCommercial);
            bot->build(10000);
            require(hasProtection(yard),"Busy growth yard prevented the idle yard from ordering protection");

            // One lot is already underway. The remaining yard cannot afford a
            // service. Preserve savings instead of spending them on another plot.
            reset(200,0);structureList.push_back(secondYard);
            yard->doProduceItem(Structure_ZoneResidential);
            bot->build(10000);
            require(!hasZone(secondYard),"Idle zoning consumed savings for urgent protection");
            // More income must eventually fund a real service queue, including
            // its foundations, rather than start the starvation loop again.
            house->addCredits(1000);
            bot->build(10000);
            require(hasProtection(secondYard),"Saved protection never became a real construction order");

            // The single-yard case must save for police even before rocket
            // technology exists; once unlocked, turrets must also get a slot.
            auto& rocketData=game.objectData.data[Structure_RocketTurret][HOUSE_HARKONNEN];
            auto& policeData=game.objectData.data[Structure_PoliceStation][HOUSE_HARKONNEN];
            const bool rocketEnabled=rocketData.enabled, policeEnabled=policeData.enabled;
            rocketData.enabled=false;yard->updateBuildList();
            reset(200,0);bot->build(10000);
            require(!hasZone(yard),"Single-yard growth consumed police savings");
            house->addCredits(1000);bot->build(10000);
            require(count(yard,Structure_PoliceStation)>0,"Single yard failed to build funded police");
            rocketData.enabled=rocketEnabled;policeData.enabled=false;yard->updateBuildList();
            reset(2000,0);bot->build(10000);
            require(count(yard,Structure_RocketTurret)>0,"Single yard failed to build funded rocket protection");
            policeData.enabled=policeEnabled;yard->updateBuildList();

            // Actual shortages and invalid demand still prevent zoning.
            reset(99,0);structureList.push_back(secondYard);bot->build(10000);
            require(!hasZone(yard) && !hasZone(secondYard),"City growth overspent its cash");
            auto& hs=game.getCitySimulation()->houseState_[HOUSE_HARKONNEN];
            hs.resValve=hs.comValve=hs.indValve=-1000;
            reset(2000,0);structureList.push_back(secondYard);bot->build(10000);
            require(!hasZone(yard) && !hasZone(secondYard),"City growth ignored negative demand");
            hs.resValve=2000;hs.comValve=hs.indValve=1500;
            reset(2000,0);structureList.push_back(secondYard);house->producedPower=0;bot->build(10000);
            require(!hasZone(yard) && !hasZone(secondYard),"City growth bypassed blackout recovery");
            std::vector<std::pair<Tile*,int>> terrain;
            for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x) {
                auto* tile=currentGameMap->getTile(x,y);
                if (!tile->hasAGroundObject()) {terrain.push_back({tile,tile->getType()});tile->setType(Terrain_Mountain);}
            }
            reset(2000,0);structureList.push_back(secondYard);bot->build(10000);
            const bool invalidZone=hasZone(yard) || hasZone(secondYard);
            for(const auto& tile:terrain)tile.first->setType(tile.second);
            require(!invalidZone,"City growth queued a zone without a legal building site");
            setCrime(0);
            for(const auto& entry:occupancy)entry.first->setCityOccupancy(entry.second);
            yard->curUpgradeLev=firstGrowthUpgrade;yard->updateBuildList();
            secondYard->curUpgradeLev=secondGrowthUpgrade;secondYard->updateBuildList();
        }
        reset(3000,0);bot->build(10000);
        require(count(heavy,Unit_Harvester)==0 && count(yard,Structure_Refinery)==0,"Depleted spice still attracts economy spending");
        require(heavy->getProductionQueueSize()>0 || heavy->isUpgrading(),"Military production and upgrades stopped when spice investment ceased");
        reset(200,1000000);bot->build(10000);
        require(heavy->getProductionQueueSize()==0 && yard->getProductionQueueSize()==0,"Spent savings below next worker cost");
        if(game.isCitySimEnabled()) {
            auto& hs=game.getCitySimulation()->houseState_[HOUSE_HARKONNEN];
            hs.resValve=hs.comValve=hs.indValve=-1000;hs.taxBaseEighths=0;
        }
        reset(1000,0);heavy->doProduceItem(Unit_Tank);bot->build(10000);
        require(!count(yard,Structure_HeavyFactory),"Added a factory while existing line was cash-starved");
        if (game.isCitySimEnabled()) {
            reset(40000,0);yard->curUpgradeLev=0;yard->updateBuildList();
            heavy->doProduceItem(Unit_Tank);bot->build(1000);
            require(yard->isUpgrading(),"Repeat construction bypassed the first defensive yard upgrade");
            yard->curUpgradeLev=yard->getMaxUpgradeLevel();yard->updateBuildList();
        }
        const bool rocketEnabled=game.objectData.data[Structure_RocketTurret][HOUSE_HARKONNEN].enabled;
        game.objectData.data[Structure_RocketTurret][HOUSE_HARKONNEN].enabled=false;
        yard->updateBuildList();
        reset(40000,0);heavy->doProduceItem(Unit_Tank);bot->build(1000);
        require(count(yard,Structure_HeavyFactory)==1,"Funded military backlog did not add production capacity");
        game.objectData.data[Structure_RocketTurret][HOUSE_HARKONNEN].enabled=rocketEnabled;
        yard->updateBuildList();
        if (game.isCitySimEnabled()) {
            auto& hs=game.getCitySimulation()->houseState_[HOUSE_HARKONNEN];
            hs.resValve=2000;hs.comValve=1500;hs.indValve=1500;
            while(house->getNumItems(Unit_Harvester)<8) house->createUnit(Unit_Harvester,true);
            // Before 703 the immediate military shortcut queued another tank
            // forever, skipping the upgrade that unlocks MCVs and artillery.
            reset(700,1000000);
            yard->curUpgradeLev=yard->getMaxUpgradeLevel();yard->updateBuildList();
            structureList.remove(yard); // Isolate factory progression from civic investment.
            heavy->curUpgradeLev=0;heavy->updateBuildList();
            bot->harvesterLimit=8;
            bot->build(10000);
            require(heavy->isUpgrading(),"Military shortcut still bypasses the factory upgrade");
            reset(0,1000000);
            heavy->curUpgradeLev=1;heavy->updateBuildList();
            require(heavy->isAvailableToBuild(Unit_MCV),"MCV unlock fixture unavailable");
            const int mcvPrice=game.objectData.data[Unit_MCV][HOUSE_HARKONNEN].price;
            reset(mcvPrice-1,1000000);bot->build(10000);
            require(heavy->getProductionQueueSize()==0,"Cheaper unit consumed MCV savings");
            reset(mcvPrice+500,1000000);bot->build(10000);
            require(count(heavy,Unit_MCV)==1,"City demand did not fund another construction yard");
            // Additional transport has the same real supplier/stock/cash checks
            // as the first carryall, and competes before more cheap military.
            reset(800,1000000);
            auto* port=static_cast<StarPort*>(*std::find_if(saved.begin(),saved.end(),[&](const auto* structure) {
                return structure->getOwner()==house && structure->getItemID()==Structure_StarPort;
            }));
            structureList.clear();structureList.push_back(port);structureList.push_back(refinery);
            port->updateBuildList();
            for(auto& offer:port->buildList) if(offer.itemID==Unit_Carryall) offer.price=800;
            for(auto& offer:house->getChoam().availableItems) offer.num=offer.itemID==Unit_Carryall ? 1 : 0;
            bot->build(10000);
            require(count(port,Unit_Carryall)==1,"Growing fleet did not buy a second carryall");
            // The fleet target must also unlock a supplier when imports sell out.
            // Hide the fixture's existing high-tech count as well as its objects;
            // restore it even on failure so object teardown remains consistent.
            const int highTechCount=house->numItem[Structure_HighTechFactory];
            house->numItem[Structure_HighTechFactory]=0;
            try {
                reset(3000,1000000);
                structureList.remove(heavy); // Keep the yard decision independent of another MCV.
                for(auto& offer:house->getChoam().availableItems)offer.num=0;
                yard->updateBuildList();bot->build(10000);
                require(count(yard,Structure_HighTechFactory)==1,
                    "Sold-out transports did not fund the first High Tech supplier");
                reset(mcvPrice+100,1000000);
                bot->build(10000);
                require(count(heavy,Unit_MCV)==1,
                    "Additional transport supplier displaced demanded city construction capacity");
            } catch (...) {house->numItem[Structure_HighTechFactory]=highTechCount;throw;}
            house->numItem[Structure_HighTechFactory]=highTechCount;
            SDL_Log("CITY_CAPACITY_PASS: upgrade, save MCV, parallel zoning, additional transport");
        }
        if (game.isCitySimEnabled()) {
            const auto savedConfig=bot->configOverride;
            auto config=std::make_shared<QuantBotConfig>(bot->botConfig());
            config->force.openingCombatBudget=false;
            config->force.campaignCombatBudget=false;
            bot->configOverride=config;
            try {
                auto& hs=game.getCitySimulation()->houseState_[HOUSE_HARKONNEN];
                hs.resValve=2000;hs.comValve=hs.indValve=1500;hs.taxBaseEighths=0;
                const int mcvPrice=game.objectData.data[Unit_MCV][HOUSE_HARKONNEN].price;
                // The shared combat-tech upgrade must still run with the
                // optional-capacity guard enabled.
                config->force.capacityMcvRunway=true;
                reset(700,1000000);
                structureList.remove(yard);
                heavy->curUpgradeLev=0;heavy->updateBuildList();
                bot->harvesterLimit=8;
                bot->build(10000);
                require(heavy->isUpgrading(),"Optional MCV runway blocked shared combat technology");

                // A real busy yard and idle heavy line spend more than this
                // no-income treasury can sustain. The off arm must actually
                // queue the optional MCV, not merely report that it could.
                for (bool enabled:{false,true}) {
                    config->force.capacityMcvRunway=enabled;
                    reset(5000,0);
                    heavy->curUpgradeLev=heavy->getMaxUpgradeLevel();heavy->updateBuildList();
                    yard->doProduceItem(Structure_ZoneResidential);
                    require(count(yard,Structure_ZoneResidential)==1,"Runway fixture needs a real busy yard");
                    bot->build(10000);
                    if (!enabled) {
                        require(count(heavy,Unit_MCV)==1,"Control did not reproduce the unfunded optional MCV");
                    } else {
                        require(count(heavy,Unit_MCV)==0,"Optional MCV consumed existing production runway");
                        const bool combat=std::any_of(heavy->currentProductionQueue.begin(),heavy->currentProductionQueue.end(),
                            [](const auto& order){return QuantBotBuildPolicy::militaryItem(order.itemID);});
                        require(combat,"Rejected optional MCV swallowed affordable combat production");
                    }
                }
                // Genuine spare funding keeps the same demanded capacity
                // purchase available; the guard is not another opening cap.
                reset(100000,0);
                yard->doProduceItem(Structure_ZoneResidential);
                bot->build(10000);
                require(count(heavy,Unit_MCV)==1,"Positive runway failed to fund optional construction capacity");

                // Remove actual objects, not just their planner-list entries:
                // recovery is defined by the house's real yard count. This is
                // last because the destroyed fixture objects cannot be restored.
                reset(mcvPrice+1500,0);
                std::vector<StructureBase*> recoveryLosses;
                for (auto* structure:saved) if (structure->getOwner()==house
                    && (structure->getItemID()==Structure_ConstructionYard
                        || structure->getItemID()==Structure_StarPort)) recoveryLosses.push_back(structure);
                for (auto* structure:recoveryLosses) {
                    saved.remove(structure);
                    structure->setVisible(VIS_ALL,false);
                    structure->destroy();
                }
                require(house->getNumItems(Structure_ConstructionYard)==0
                    && house->getNumItems(Structure_StarPort)==0,"Recovery fixture retained a real yard or port");
                require(house->getNumLostItems(Structure_ConstructionYard)>0,"Recovery fixture did not lose a real yard");
                require(house->getNumItems(Unit_MCV)==0,"Recovery fixture already owns a replacement MCV");
                bot->builderPlaceLocations.clear();bot->reservedStructures.clear();bot->clearPlacementCache();
                structureList.clear();structureList.push_back(heavy);structureList.push_back(refinery);
                house->storedCredits=0;house->cityCredits=0;house->startingCredits=mcvPrice+1500;
                bot->build(10000);
                require(count(heavy,Unit_MCV)==1,"Negative runway blocked actual lost-yard recovery");
                SDL_Log("CAPACITY_MCV_RUNWAY_PASS: real off/on queues, combat fallthrough, funded capacity, lost-yard recovery, shared technology");
            } catch (...) {bot->configOverride=savedConfig;throw;}
            bot->configOverride=savedConfig;
        }
        SDL_Log("SHARED_SPENDING_PASS: exact worker reserve, simultaneous construction, depletion, scarce credits (%s)",game.isCitySimEnabled()?"city":"vanilla");
    }catch(...){structureList=saved;throw;}
    structureList=saved;
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=shared-spending outcome=passed");
}
