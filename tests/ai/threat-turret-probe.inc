#include <players/QuantBotBuildPass.h>

// One refinery, two rear rockets, no heavy/repair core, and an observed quad
// raid on the other side. Exercise capital, legal placement and real queues.
void runThreatTurretProbe(Game& game) {
    auto require=[](bool ok,const char* why) {
        if (!ok) { SDL_Log("THREAT_TURRET_FAIL: %s",why);throw std::runtime_error(why); }
    };
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    QuantBot* bot=nullptr;
    for (const auto& player:house->getPlayerList())
        if (auto* q=dynamic_cast<QuantBot*>(player.get())) bot=q;
    House* hostile=nullptr;
    for (int h=0;h<NUM_HOUSES && !hostile;++h)
        if (auto* other=game.getHouse(h);other && !other->isNatives()
            && other->getTeamID()!=house->getTeamID()) hostile=other;
    require(bot && hostile,"Missing fixture controllers");
    const auto oldConfig=bot->configOverride;
    const auto oldDifficulty=bot->difficulty;
    const auto oldMode=bot->gameMode;
    const auto oldRole=bot->role;
    const auto oldType=game.gameType;
    const auto oldTech=game.techLevel;
    const auto oldCity=game.citySimEnabled_;
    const bool hadSimulation=bool(game.citySimulation_);
    const auto oldStructures=structureList;
    const auto oldUnits=unitList;
    const auto oldLosses=bot->recentStructureLosses;
    const auto oldStored=house->storedCredits, oldStarting=house->startingCredits, oldCityCredits=house->cityCredits;
    const auto oldPower=house->producedPower;
    const auto oldNumStructures=house->numStructures, oldNumUnits=house->numUnits;
    std::array<int,Num_ItemID> oldCounts{};
    std::copy(std::begin(house->numItem),std::end(house->numItem),oldCounts.begin());
    std::array<int,Num_ItemID> oldBuilt{};
    std::copy(std::begin(house->numItemBuilt),std::end(house->numItemBuilt),oldBuilt.begin());
    const auto oldMarket=house->getChoam().availableItems;
    const auto oldCycle=game.gameCycleCount;
    const auto oldSpice=bot->lastCalculatedSpice;
    const bool oldConcrete=game.gameInitSettings.gameOptions.concreteRequired;
    const bool oldInstant=game.gameInitSettings.gameOptions.instantBuild;
    const bool oldInstallments=game.gameInitSettings.gameOptions.technologyInstallments;
    std::vector<StructureBase*> buildings;
    std::vector<UnitBase*> raiders;
    auto cleanup=[&] {
        bot->builderPlaceLocations.clear();bot->reservedStructures.clear();
        for (auto* unit:raiders) unit->destroy();
        for (auto* structure:buildings) structure->destroy();
        structureList=oldStructures;unitList=oldUnits;
        bot->configOverride=oldConfig;bot->difficulty=oldDifficulty;bot->gameMode=oldMode;bot->role=oldRole;
        bot->recentStructureLosses=oldLosses;bot->dangerUpdated=std::numeric_limits<Uint32>::max();
        game.gameType=oldType;game.citySimEnabled_=oldCity;
        game.techLevel=oldTech;
        house->storedCredits=oldStored;house->startingCredits=oldStarting;house->cityCredits=oldCityCredits;
        house->producedPower=oldPower;
        std::copy(oldCounts.begin(),oldCounts.end(),std::begin(house->numItem));
        std::copy(oldBuilt.begin(),oldBuilt.end(),std::begin(house->numItemBuilt));
        house->numStructures=oldNumStructures;house->numUnits=oldNumUnits;
        house->getChoam().availableItems=oldMarket;
        game.gameCycleCount=oldCycle;bot->lastCalculatedSpice=oldSpice;
        game.gameInitSettings.gameOptions.concreteRequired=oldConcrete;
        game.gameInitSettings.gameOptions.instantBuild=oldInstant;
        game.gameInitSettings.gameOptions.technologyInstallments=oldInstallments;
        if (!hadSimulation) game.citySimulation_.reset();
    };
    try {
        Coord origin=Coord::Invalid();
        constexpr int span=30;
        for (int y=4;y+span<currentGameMap->getSizeY() && origin.isInvalid();++y)
            for (int x=4;x+span<currentGameMap->getSizeX() && origin.isInvalid();++x) {
                bool clear=true;
                for (int dy=0;dy<span && clear;++dy) for (int dx=0;dx<span;++dx)
                    if (currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject()) { clear=false;break; }
                if (clear) origin=Coord(x,y);
            }
        require(origin.isValid(),"No empty settlement patch");
        game.techLevel=9;
        for (int y=origin.y;y<origin.y+span;++y) for (int x=origin.x;x<origin.x+span;++x)
            currentGameMap->getTile(x,y)->setType(Terrain_Rock);
        auto put=[&](Uint32 item,int x,int y) {
            auto* structure=house->placeStructure(NONE_ID,item,origin.x+x,origin.y+y,true,true);
            require(structure,"Fixture building placement failed");buildings.push_back(structure);return structure;
        };
        auto* yard=static_cast<BuilderBase*>(put(Structure_ConstructionYard,12,12));
        put(Structure_Refinery,12,6);put(Structure_Radar,16,9);
        put(Structure_RocketTurret,6,10);put(Structure_RocketTurret,6,13);
        put(Structure_WindTrap,3,3);put(Structure_WindTrap,3,6);put(Structure_WindTrap,3,9);
        yard->curUpgradeLev=yard->getMaxUpgradeLevel();yard->updateBuildList();
        for (int n=0;n<8;++n) {
            auto* unit=hostile->createUnit(Unit_Quad);require(unit,"Quad creation failed");
            unit->deploy(origin+Coord(23,n+9));unit->setVisible(house->getTeamID(),true);raiders.push_back(unit);
        }
        structureList.clear();for (auto* structure:buildings) structureList.push_back(structure);
        unitList.clear();for (auto* unit:raiders) unitList.push_back(unit);
        bot->difficulty=QuantBot::Difficulty::Hard;bot->gameMode=QuantBot::GameMode::Custom;bot->role=AIRole::Both;
        game.gameType=GameType::CustomGame;
        auto config=std::make_shared<QuantBotConfig>(bot->botConfig());
        config->force.cityStarportSupply=false;config->force.perimeterTurrets=false;
        bot->configOverride=config;
        auto clear=[&] {
            while (yard->getProductionQueueSize()) yard->doCancelItem(yard->getCurrentProducedItem());
            bot->builderPlaceLocations.clear();bot->reservedStructures.clear();
            bot->dangerUpdated=std::numeric_limits<Uint32>::max();bot->resetBuildPass();
        };
        auto plan=[&](bool enabled,bool city=false) {
            config->force.threatTurrets=enabled;clear();
            QuantBot::BuildPass pass(0);
            pass.itemCount[Structure_ConstructionYard]=1;pass.itemCount[Structure_Refinery]=1;
            pass.itemCount[Structure_WindTrap]=1;pass.itemCount[Structure_Radar]=1;
            pass.itemCount[Structure_RocketTurret]=2;
            pass.money=game.objectData.data[Structure_RocketTurret][house->getHouseID()].price+100;
            pass.sharedSpending=true;pass.citySimEnabled=city;pass.capitalBuilders.push_back(yard);
            bot->planDefenceCoverage(pass);bot->planCapitalCandidates(pass);bot->chooseCapitalPlan(pass);
            return pass;
        };
        config->force.moreRocketTurrets=false;
        auto earlyOld=plan(false);
        require(earlyOld.earlyRocketLimit==2 && !earlyOld.routineRocketsAllowed,
            "More-off did not retain the two-rocket unfinished-core gate");
        config->force.moreRocketTurrets=true;
        auto earlyNew=plan(false);
        require(earlyNew.earlyRocketLimit>=3 && earlyNew.earlyRocketLimit<=4 && earlyNew.routineRocketsAllowed,
            "An established Hard base did not permit modest additional core cover");
        for (size_t n=3;n<raiders.size();++n) raiders[n]->setVisible(house->getTeamID(),false);
        config->force.moreRocketTurrets=false;
        auto threeOld=plan(true);
        config->force.moreRocketTurrets=true;
        auto threeNew=plan(true);
        require(threeNew.groundTurretGoal>threeOld.groundTurretGoal
            && threeNew.groundTurretGoal<=threeOld.groundTurretGoal+2,
            "A small visible quad rush did not receive bounded denser rocket cover");
        SDL_Log("MORE_ROCKET_STATE: three_quads off=%d on=%d early=%d",
            threeOld.groundTurretGoal,threeNew.groundTurretGoal,earlyNew.earlyRocketLimit);
        for (auto* unit:raiders) unit->setVisible(house->getTeamID(),true);
        auto off=plan(false);
        require(off.groundThreatAsset.isInvalid(),"Disabled switch still demands ground turrets");
        auto on=plan(true);
        require(on.groundTurretGoal>=4 && on.groundTurretCover<on.groundTurretGoal,
            "Vehicle raid did not increase local rocket demand beyond the rear pair");
        SDL_Log("THREAT_TURRET_STATE: goal=%d cover=%d available=%d upgrade=%d/%d builder=%u choices=%zu selected=%d",
            on.groundTurretGoal,on.groundTurretCover,bot->campaignAvailableToBuild(yard,Structure_RocketTurret),
            yard->getCurrentUpgradeLevel(),yard->getMaxUpgradeLevel(),yard->getObjectID(),
            on.capitalCandidates.size(),on.capitalChoice);
        require(on.capitalChoice>=0 && on.capitalCandidates[on.capitalChoice].reason==std::string("ground_threat"),
            "Missing production core suppressed the active ground defence");
        auto bargain=on.capitalCandidates[on.capitalChoice];
        bargain.item=Unit_Quad;bargain.kind="military";bargain.reason="active_defence";
        bargain.price=140;bargain.score=100000;
        on.capitalCandidates.push_back(bargain);
        bot->chooseCapitalPlan(on);
        require(on.protectionCapital && on.capitalCandidates[on.capitalChoice].reason==std::string("ground_threat"),
            "A high-return import diverted the active ground defence budget");
        auto blackout=bargain;blackout.item=Structure_WindTrap;blackout.kind="power";
        blackout.reason="blackout";blackout.score=7500;
        on.capitalCandidates.push_back(blackout);bot->chooseCapitalPlan(on);
        require(on.capitalCandidates[on.capitalChoice].reason==std::string("blackout"),
            "Bounded protection displaced a higher-priority non-military emergency");
        on.capitalCandidates.pop_back();bot->chooseCapitalPlan(on);
        const Coord radarSite=bot->findCoveredDefencePlaceLocation(Structure_Radar,yard->getLocation());
        require(radarSite.isValid() && blockDistance(radarSite,origin+Coord(6,13))<=6,
            "Rebuilt defensive Radar was not placed inside the standing battery");
        const auto site=on.capitalCandidates[on.capitalChoice].site;
        require(site.isValid() && site.x>origin.x+12,"Rocket was not sited on the attacked side");
        bot->buildProducer(on,yard);
        bool rocket=false;
        for (const auto& order:yard->getProductionQueue()) if (order.itemID==Structure_RocketTurret) rocket=true;
        require(rocket,"Selected ground defence did not reach real production");
        auto pending=plan(true);
        pending.itemCount[Structure_RocketTurret]=pending.earlyRocketLimit-1;
        bot->planDefenceCoverage(pending);
        pending.capitalCandidates.clear();pending.capitalChoice=-1;pending.capitalConsumed=false;
        bot->planCapitalCandidates(pending);bot->chooseCapitalPlan(pending);
        bot->buildProducer(pending,yard);
        require(pending.itemCount[Structure_RocketTurret]==pending.earlyRocketLimit
            && !pending.routineRocketsAllowed,
            "A queued rocket did not close the shared early allowance for the other yards");
        for (auto* unit:raiders) unit->setVisible(house->getTeamID(),false);
        auto unseen=plan(true);
        require(unseen.groundThreatAsset.isInvalid(),"An unseen ground army demanded cover");
        for (auto* unit:raiders) unit->setVisible(house->getTeamID(),true);
        auto capped=plan(true);
        capped.itemCount[Structure_RocketTurret]=RocketTurretPolicy::threatTurretTotalCap;
        capped.groundThreatAsset=Coord::Invalid();capped.groundTurretGoal=0;
        bot->planDefenceCoverage(capped);
        require(capped.groundThreatAsset.isInvalid(),"Ground defence exceeded its house-wide cap");
        game.citySimEnabled_=true;
        if (!game.citySimulation_) {
            game.citySimulation_=std::make_unique<DuneCity::CitySimulation>();
            game.citySimulation_->init(currentGameMap->getSizeX(),currentGameMap->getSizeY());
        }
        // Model already-pending seed plots and first factories. The selected,
        // funded cover must execute instead of saving for the next core item.
        for (auto* unit:raiders) unit->setVisible(house->getTeamID(),false);
        auto extraCover=[&](bool more,int cash=-1,int netPerMinute=250) {
            config->force.moreRocketTurrets=more;
            auto pass=plan(false,true);
            pass.itemCount[Structure_LightFactory]=1;pass.itemCount[Structure_HeavyFactory]=1;
            pass.itemCount[Structure_ZoneResidential]=2;pass.itemCount[Structure_ZoneCommercial]=1;
            pass.itemCount[Structure_ZoneIndustrial]=1;pass.cityZonesIncludingQueued=4;pass.cityYards=1;
            pass.money=cash>=0 ? cash : pass.money+100;
            pass.cityPlotPrice=game.objectData.data[Structure_ZoneResidential][house->getHouseID()].price;
            pass.forecastTax=50+netPerMinute;pass.forecastUpkeep=50;
            pass.forecastNetIncome=netPerMinute*QuantBotSpendingPolicy::horizonMinutes;
            pass.forecastFunding=std::max(0,pass.money+pass.forecastNetIncome);
            pass.emitStatsLog=true;
            pass.cashFlow.projectedCash=pass.forecastFunding;
            // Complete the native storage/bootstrap setup for this city pass:
            // a value-initialized storage item is slab 0, not a quoted vault.
            bot->planCityBootstrap(pass);
            require(pass.cityStorageCapacityIncludingQueued>0,
                "The established battery fixture lacks its native refinery vault");
            bot->planDefenceCoverage(pass);
            pass.capitalCandidates.clear();pass.capitalChoice=-1;pass.capitalConsumed=false;
            bot->planCapitalCandidates(pass);bot->chooseCapitalPlan(pass);
            return pass;
        };
        auto oldCover=extraCover(false);
        bot->buildProducer(oldCover,yard);
        require(std::none_of(yard->getProductionQueue().begin(),yard->getProductionQueue().end(),
            [](const auto& order){return order.itemID==Structure_RocketTurret;}),
            "More-off bought extra ordinary cover before completing its core");
        auto newCover=extraCover(true);
        auto idleTransport=bargain;
        idleTransport.item=Structure_HighTechFactory;idleTransport.kind="transport_production";
        idleTransport.reason="transport_imports_unavailable";idleTransport.price=500;idleTransport.score=5500;
        idleTransport.site=bot->findPlaceLocation(Structure_HighTechFactory);
        newCover.capitalCandidates.push_back(bargain);newCover.capitalCandidates.push_back(idleTransport);
        bot->chooseCapitalPlan(newCover);
        require(newCover.capitalChoice>=0 && newCover.protectionCapital
            && std::string_view(newCover.capitalCandidates[newCover.capitalChoice].reason)=="early_rocket",
            "A bargain or idle fleet-ratio transport displaced the funded standing battery");
        auto activeTransport=newCover;
        activeTransport.waitingRepairVehicles=1;
        bot->chooseCapitalPlan(activeTransport);
        require(activeTransport.capitalChoice>=0
            && std::string_view(activeTransport.capitalCandidates[activeTransport.capitalChoice].kind)=="transport_production",
            "The standing battery displaced actual repair transport work");
        bot->buildProducer(newCover,yard);
        require(std::any_of(yard->getProductionQueue().begin(),yard->getProductionQueue().end(),
            [](const auto& order){return order.itemID==Structure_RocketTurret;}),
            "Optional core saving displaced the funded selected extra rocket");
        auto savingCover=extraCover(true,game.objectData.data[Structure_RocketTurret][house->getHouseID()].price
            +game.objectData.data[Structure_ZoneResidential][house->getHouseID()].price-1);
        require(savingCover.capitalChoice>=0 && savingCover.protectionCapital
            && std::string_view(savingCover.capitalCandidates[savingCover.capitalChoice].reason)=="early_rocket",
            "A fully income-budgeted standing battery could not save its next step");
        bot->buildProducer(savingCover,yard);
        require(std::none_of(yard->getProductionQueue().begin(),yard->getProductionQueue().end(),
            [](const auto& order){return order.itemID==Structure_RocketTurret;}),
            "The standing battery spent its working cash");
        auto poorCover=extraCover(true,game.objectData.data[Structure_RocketTurret][house->getHouseID()].price-1,0);
        require(std::none_of(poorCover.capitalCandidates.begin(),poorCover.capitalCandidates.end(),
            [](const auto& c){return c.score>0 && std::string_view(c.reason)=="early_rocket";}),
            "An unfunded optional battery created a savings hold");
        auto losingIncome=extraCover(true,10000,-1);
        require(std::none_of(losingIncome.capitalCandidates.begin(),losingIncome.capitalCandidates.end(),
            [](const auto& c){return c.score>0 && std::string_view(c.reason)=="early_rocket";}),
            "A negative-income city committed to an optional standing battery");
        const auto readyLevel=yard->curUpgradeLev;
        yard->curUpgradeLev=0;yard->updateBuildList();
        auto weakBridge=extraCover(true,yard->getUpgradeCost()
            +game.objectData.data[Structure_ZoneResidential][house->getHouseID()].price,10);
        require(std::none_of(weakBridge.capitalCandidates.begin(),weakBridge.capitalCandidates.end(),
            [](const auto& c){return c.score>0 && std::string_view(c.reason)=="early_rocket";}),
            "One affordable upgrade was mistaken for a funded full rocket bridge");
        // The mandatory bulk-slab unlock owns an earlier, independent pass in
        // both switch arms. Isolate the rocket's remaining upgrade after it.
        const int slabLevel=game.objectData.data[Structure_Slab4][house->getHouseID()].upgradeLevel;
        require(slabLevel>=0 && slabLevel<game.objectData.data[Structure_RocketTurret][house->getHouseID()].upgradeLevel,
            "The native tree cannot isolate rocket tech after bulk concrete");
        yard->curUpgradeLev=slabLevel;yard->updateBuildList();
        auto lockedCover=extraCover(true,yard->getUpgradeCost()
            +game.objectData.data[Structure_ZoneResidential][house->getHouseID()].price-1);
        require(lockedCover.capitalChoice>=0
            && std::string_view(lockedCover.capitalCandidates[lockedCover.capitalChoice].reason)=="early_rocket"
            && std::string_view(lockedCover.capitalCandidates[lockedCover.capitalChoice].kind)=="defence_upgrade",
            "Starting rockets hid the affordable yard unlock needed to add another");
        bot->buildProducer(lockedCover,yard);
        require(!yard->isUpgrading(),"A funded unlock consumed its working cash");
        lockedCover.money=lockedCover.capitalCandidates[lockedCover.capitalChoice].price+lockedCover.cityPlotPrice;
        bot->buildProducer(lockedCover,yard);
        require(yard->isUpgrading(),"The fully budgeted unlock did not reach native production");
        yard->upgrading=false;
        yard->curUpgradeLev=readyLevel;yard->updateBuildList();
        for (auto* unit:raiders) unit->setVisible(house->getTeamID(),true);
        // These cases test supplier funding, not the separate Starport Population
        // rule (custom-city-cap-probe covers it); this fresh city is below 5,000.
        auto& starportRule=game.gameInitSettings.gameOptions.starportPopulation;
        const bool keepStarportRule=starportRule;
        SDL_Log("THREAT_TURRET_SUPPLY_STATE: population_rule=%d population_met=%d available=%d",
            int(starportRule),int(house->meetsStarportPopulation()),
            int(bot->campaignAvailableToBuild(yard,Structure_StarPort)));
        starportRule=false;house->update();yard->updateBuildList();
        require(bot->campaignAvailableToBuild(yard,Structure_StarPort),
            "The supplier funding fixture lacks a native buildable Starport");
        auto supply=[&](bool enabled,int cash) {
            config->force.cityStarportSupply=enabled;
            auto pass=plan(false,true);
            pass.capitalCandidates.clear();pass.capitalChoice=-1;pass.capitalConsumed=false;
            pass.cityYards=1;pass.cityZonesIncludingQueued=12;pass.cityStorageCapacityIncludingQueued=1000;
            pass.itemCount[Structure_ZoneResidential]=6;pass.itemCount[Structure_ZoneCommercial]=3;
            pass.itemCount[Structure_ZoneIndustrial]=3;pass.starportMarketAvailable=true;
            pass.forecastTax=1000;pass.forecastUpkeep=100;pass.forecastFunding=3600;
            pass.cashFlow.projectedCash=1000;pass.money=cash;
            pass.emitStatsLog=true;
            bot->planCapitalCandidates(pass);bot->chooseCapitalPlan(pass);
            return pass;
        };
        auto noSupply=supply(false,300);
        require(std::none_of(noSupply.capitalCandidates.begin(),noSupply.capitalCandidates.end(),
            [](const auto& c){return c.kind==std::string("city_supply");}),"Disabled supplier still claimed cash");
        auto saving=supply(true,300);
        saving.capitalCandidates.push_back(bargain);bot->chooseCapitalPlan(saving);
        require(saving.protectionCapital && saving.capitalChoice>=0
            && saving.capitalCandidates[saving.capitalChoice].item==Structure_StarPort,
            "A solvent city's first useful Starport was absent from protected capital");
        const int price=saving.capitalCandidates[saving.capitalChoice].price;
        require(bot->capitalReserve(saving,NONE_ID)>=price,"Units could spend the first Starport's savings");
        auto funded=supply(true,price+200);
        bot->buildProducer(funded,yard);
        bool port=false;
        for (const auto& order:yard->getProductionQueue()) if (order.itemID==Structure_StarPort) port=true;
        require(port,"Funded city Starport did not reach real production");
        auto repair=supply(false,600);
        auto prerequisite=saving.capitalCandidates[saving.capitalChoice];
        prerequisite.item=Structure_Radar;prerequisite.kind="defence_prerequisite";
        prerequisite.reason="ground_threat";prerequisite.urgent=true;prerequisite.score=6500;
        prerequisite.price=game.objectData.data[Structure_Radar][house->getHouseID()].price;
        prerequisite.cost=prerequisite.price;prerequisite.foundationCost=0;
        prerequisite.site=bot->findCoveredDefencePlaceLocation(Structure_Radar,yard->getLocation());
        repair.capitalCandidates={prerequisite};bot->chooseCapitalPlan(repair);
        bot->buildProducer(repair,yard);
        bool radar=false;
        for (const auto& order:yard->getProductionQueue()) if (order.itemID==Structure_Radar) radar=true;
        require(radar
            && !bot->builderPlaceLocations[yard->getObjectID()].empty()
            && bot->builderPlaceLocations[yard->getObjectID()].back()==prerequisite.site,
            "Final yard vetting discarded the protected prerequisite's selected site");
        clear();
        put(Structure_RocketTurret,10,22);put(Structure_RocketTurret,13,22);
        put(Structure_RocketTurret,16,22);put(Structure_RocketTurret,19,22);
        for (const Coord at:{Coord(3,20),Coord(3,23),Coord(3,26),Coord(23,23),Coord(26,23),Coord(26,26)}) {
            if (house->getProducedPower()-house->getPowerRequirement()
                >=std::max(0,game.objectData.data[Structure_StarPort][house->getHouseID()].power)+24) break;
            put(Structure_WindTrap,at.x,at.y);
        }
        house->update();
        yard->updateBuildList();
        auto established=supply(true,1000);
        config->force.threatTurrets=true;
        established.itemCount[Structure_RocketTurret]=house->getNumItems(Structure_RocketTurret);
        bot->planDefenceCoverage(established);
        established.capitalCandidates.clear();bot->planCapitalCandidates(established);bot->chooseCapitalPlan(established);
        require(established.groundThreatAsset.isValid()
            && established.capitalChoice>=0
            && established.capitalCandidates[established.capitalChoice].kind==std::string("city_supply"),
            "An established battery's remaining covers starved its first mobile supplier");
        // A separate physical first-AA city. Existing fixture assets and their
        // actual house counts agree; the detached scenario's inventory is not
        // allowed to supply a phantom Radar or first rocket.
        clear();
        for (auto* unit:raiders) unit->setVisible(house->getTeamID(),false);
        for (auto it=buildings.begin();it!=buildings.end();) {
            if ((*it)->getItemID()==Structure_RocketTurret || (*it)->getItemID()==Structure_Radar) {
                delete *it;it=buildings.erase(it);
            } else ++it;
        }
        put(Structure_ZoneResidential,20,3);put(Structure_ZoneResidential,23,3);
        put(Structure_ZoneCommercial,20,6);put(Structure_ZoneIndustrial,23,6);
        auto* airPort=static_cast<BuilderBase*>(put(Structure_StarPort,20,18));
        std::fill(std::begin(house->numItem),std::end(house->numItem),0);
        for (const auto* building:buildings) ++house->numItem[building->getItemID()];
        house->numStructures=int(buildings.size());house->numUnits=0;
        house->update();
        game.gameInitSettings.gameOptions.concreteRequired=false;
        game.gameInitSettings.gameOptions.instantBuild=false;
        game.gameInitSettings.gameOptions.technologyInstallments=false;
        config->force.defencePrerequisites=true;config->force.moreRocketTurrets=false;
        config->force.startWhatMakesSense=false;config->force.simpleTechSavings=false;
        config->force.existingCompositionUnitChoice=false;config->force.bufferedHeavyProduction=false;
        config->force.incomeHeavyFactories=false;config->force.firstFactoryOwnPrerequisites=false;
        config->force.cityStarportSupply=false;config->force.threatTurrets=false;config->force.trooperSupport=false;
        bot->lastCalculatedSpice=0;
        const int technology=ModManager::instance().isTornieFamilyContentActive()
            ? yard->getTechnologyHouseID() : yard->getOriginalHouseID();
        const int rocketLevel=game.objectData.data[Structure_RocketTurret][technology].upgradeLevel;
        require(rocketLevel>slabLevel,"First-AA fixture cannot isolate a native rocket upgrade");
        yard->curUpgradeLev=rocketLevel-1;yard->upgrading=false;yard->setHealth(yard->getMaxHealth());
        yard->updateBuildList();
        // A real CHOAM offer at its native catalog price; acceptance still
        // obtains the actual market/research quote from the Starport build list.
        house->getChoam().availableItems.clear();
        BuildItem offer;offer.itemID=Unit_Tank;offer.num=2;
        offer.price=game.objectData.data[Unit_Tank][house->getHouseID()].price;
        house->getChoam().availableItems.push_back(offer);airPort->updateBuildList();
        const int importPrice=bot->purchasePrice(airPort,Unit_Tank);
        const int radarPrice=bot->purchasePrice(yard,Structure_Radar);
        const int rocketPrice=game.objectData.data[Structure_RocketTurret][technology].price;
        const int upgradePrice=yard->getUpgradeCost();
        require(importPrice>1 && radarPrice>0 && rocketPrice>0 && upgradePrice>0,
            "First-AA fixture lacks actual native quotes");
        // The completion arm uses one declared grant and native payments.
        // Isolated quote-boundary arms below save and restore that balance.
        house->storedCredits=0;house->cityCredits=0;
        house->startingCredits=upgradePrice+radarPrice+rocketPrice+importPrice-1;
        auto* aircraft=hostile->createUnit(Unit_Ornithopter);
        require(aircraft,"First-AA aircraft creation failed");
        aircraft->deploy(origin+Coord(15,14));aircraft->setVisible(house->getTeamID(),true);
        raiders.push_back(aircraft);
        auto airPlan=[&] {
            bot->resetBuildPass();
            QuantBot::BuildPass pass(0);
            bot->planBuildInventory(pass);bot->planBuildBudget(pass);bot->planCityState(pass);
            bot->planStrategicReserve(pass);bot->planSpiceEconomy(pass);bot->planPowerAndYards(pass);
            bot->planCityPopulation(pass);bot->planCivicNeedAndTrips(pass);bot->planUnitMix(pass);
            bot->planCapitalForecast(pass);bot->planCityBootstrap(pass);bot->planOpeningProduction(pass);
            bot->planDefenceCoverage(pass);bot->planTechSavings(pass);
            bot->planSensibleFirstFactory(pass);
            bot->planCapitalCandidates(pass);bot->chooseCapitalPlan(pass);
            return pass;
        };
        auto exactCoreAA=[&](Uint32 item,bool upgrade) {
            const auto keepStored=house->storedCredits, keepStarting=house->startingCredits, keepCity=house->cityCredits;
            const int quote=upgrade ? yard->getUpgradeCost() : bot->purchasePrice(yard,item);
            require(quote>0,"Nonurgent first-AA component lacks a native quote");
            aircraft->setVisible(house->getTeamID(),false);
            house->storedCredits=0;house->cityCredits=0;house->startingCredits=quote;
            auto exact=airPlan();
            require(!exact.airEngaged && exact.money==quote
                && exact.firstAirDefenceReserve>=quote
                && (upgrade ? exact.firstAirDefenceBuilder==yard->getObjectID()
                    && exact.firstAirDefenceUpgradeReserve>=quote : exact.firstAirDefenceStepCosts[item]>0)
                && (exact.capitalChoice<0 || !exact.capitalCandidates[exact.capitalChoice].urgent),
                "Exact-cash AA control is not a nonurgent, actually priced chain component");
            const int remaining=exact.remainingFirstAirDefenceCash();
            QuantBot::BuilderTurn turn(exact.money);turn.pStructure=yard;turn.pBuilder=yard;
            bot->beginBuilderTurn(exact,turn);
            const bool accepted=upgrade ? bot->upgradeWithLogging(exact,turn,__LINE__)
                : bot->queueFoundedStructure(exact,turn,item,__LINE__,"first_aa_nonurgent_exact_quote");
            // Match native founded callers: normal accepted item counts are caller-owned.
            if (accepted && !upgrade) ++exact.itemCount[item];
            require(accepted && exact.money==0
                && (upgrade ? yard->isUpgrading() : yard->getCurrentProducedItem()==item)
                && exact.remainingFirstAirDefenceCash()==remaining-quote,
                "A nonurgent first-AA component could not spend its own exact native quote");
            if (upgrade) yard->upgrading=false; // Independent initial-quote control; no payment tick advanced.
            clear();
            house->storedCredits=keepStored;house->startingCredits=keepStarting;house->cityCredits=keepCity;
            aircraft->setVisible(house->getTeamID(),true);
        };
        exactCoreAA(Structure_RocketTurret,true);
        {
            // A busy upgraded yard prices Radar+rocket. An idle yard's newly
            // accepted upgrade was NOT part of that minimum and cannot release
            // any of those reserved component quotes to a Starport.
            const auto keepStored=house->storedCredits, keepStarting=house->startingCredits, keepCity=house->cityCredits;
            const int keepYardLosses=house->numItemLosses[Structure_ConstructionYard];
            const auto keepRecentLosses=bot->recentStructureLosses;
            auto* pricedYard=static_cast<BuilderBase*>(put(Structure_ConstructionYard,8,18));
            pricedYard->curUpgradeLev=pricedYard->getMaxUpgradeLevel();pricedYard->setHealth(pricedYard->getMaxHealth());
            pricedYard->updateBuildList();pricedYard->doProduceItem(Structure_Road,false);
            const int busyQuote=bot->purchasePrice(pricedYard,Structure_Road);
            require(busyQuote>0 && pricedYard->getProductionQueueSize()==1,"The minimum-chain yard is not natively busy");
            house->storedCredits=0;house->cityCredits=0;
            house->startingCredits=busyQuote+upgradePrice+radarPrice+rocketPrice+importPrice-1;
            auto unreservedUpgrade=airPlan();
            unreservedUpgrade.nuclearPlan=false; // No unrelated reactor savings may mask the exact import boundary.
            require(unreservedUpgrade.firstAirDefenceBuilder==pricedYard->getObjectID()
                && unreservedUpgrade.firstAirDefenceUpgradeReserve==0
                && unreservedUpgrade.firstAirDefenceReserve==radarPrice+rocketPrice
                && unreservedUpgrade.capitalChoice>=0
                && unreservedUpgrade.capitalCandidates[unreservedUpgrade.capitalChoice].builder==yard->getObjectID()
                && std::string_view(unreservedUpgrade.capitalCandidates[unreservedUpgrade.capitalChoice].kind)=="defence_upgrade",
                "The cheaper busy chain did not isolate the idle yard's unreserved upgrade");
            bot->buildProducer(unreservedUpgrade,yard);
            require(yard->isUpgrading()
                && bot->capitalReserve(unreservedUpgrade,airPort->getObjectID())==radarPrice+rocketPrice,
                "An upgrade outside the priced chain released protected Radar/rocket cash");
            const auto credits=house->getStartingCredits()+house->getEarnedCredits();
            {
                QuantBot::BuilderTurn turn(unreservedUpgrade.money);turn.pStructure=airPort;turn.pBuilder=airPort;
                bot->beginBuilderTurn(unreservedUpgrade,turn);bot->reserveBuilderCash(unreservedUpgrade,turn);
                require(unreservedUpgrade.money==importPrice-1
                    && !bot->produceItemWithLogging(unreservedUpgrade,turn,Unit_Tank,__LINE__,"unreserved_aa_upgrade_import"),
                    "A Starport spent the unreserved-upgrade boundary's protected AA balance");
            }
            require(credits==house->getStartingCredits()+house->getEarnedCredits()
                && airPort->getProductionQueueSize()==0,"The refused boundary import changed real credits or cargo");
            yard->upgrading=false;
            while (pricedYard->getProductionQueueSize()) pricedYard->doCancelItem(pricedYard->getCurrentProducedItem());
            buildings.erase(std::find(buildings.begin(),buildings.end(),pricedYard));delete pricedYard;
            house->numItemLosses[Structure_ConstructionYard]=keepYardLosses;
            bot->recentStructureLosses=keepRecentLosses;
            house->storedCredits=keepStored;house->startingCredits=keepStarting;house->cityCredits=keepCity;
            clear();
        }
        {
            // An actual working-fleet supplier retains its established 5500
            // first-Carryall claim above the 3000 urgent AA unlock. Exercise
            // native queue acceptance and the immediate CHOAM debit at exact cash.
            const auto keepStored=house->storedCredits, keepStarting=house->startingCredits, keepCity=house->cityCredits;
            const auto keepMarket=house->getChoam().availableItems;
            const auto keepSpice=bot->lastCalculatedSpice;
            const int keepHeavyLosses=house->numItemLosses[Structure_HeavyFactory];
            const int keepWorkerLosses=house->numItemLosses[Unit_Harvester];
            const auto keepRecentLosses=bot->recentStructureLosses;
            auto* heavy=put(Structure_HeavyFactory,20,12);
            auto* worker=house->createUnit(Unit_Harvester);require(worker,"First-transport worker creation failed");
            worker->deploy(origin+Coord(15,6));raiders.push_back(worker);
            // Positive cached spice is the existing first-transport demand
            // input; this admission arm does not add income or change prices.
            bot->lastCalculatedSpice=1;
            BuildItem carry;carry.itemID=Unit_Carryall;carry.num=1;
            carry.price=game.objectData.data[Unit_Carryall][house->getHouseID()].price;
            house->getChoam().availableItems.push_back(carry);airPort->updateBuildList();
            const int carryQuote=bot->purchasePrice(airPort,Unit_Carryall);
            require(carryQuote>0,"The first transport has no native CHOAM quote");
            house->storedCredits=0;house->cityCredits=0;house->startingCredits=carryQuote;
            auto transport=airPlan();
            require(bot->needsFirstTransport(transport) && transport.firstAirDefenceReserve>0
                && transport.capitalChoice>=0 && transport.capitalCandidates[transport.capitalChoice].item==Unit_Carryall
                && transport.capitalCandidates[transport.capitalChoice].builder==airPort->getObjectID(),
                "Canonical essential first-Carryall selection did not outrank the AA unlock");
            const auto before=house->getStartingCredits()+house->getEarnedCredits();
            {
                QuantBot::BuilderTurn turn(transport.money);turn.pStructure=airPort;turn.pBuilder=airPort;
                bot->beginBuilderTurn(transport,turn);bot->reserveBuilderCash(transport,turn);
                require(transport.money==carryQuote,"AA withholding took the selected first-Carryall supplier's exact quote");
                bot->buildCapitalUnits(transport,turn);
            }
            require(airPort->getProductionQueueSize()==1 && airPort->getProductionQueue().front().itemID==Unit_Carryall
                && before-house->getStartingCredits()-house->getEarnedCredits()==FixPoint(carryQuote),
                "The essential Carryall did not enter a real paid native cargo queue");
            airPort->doCancelItem(Unit_Carryall,false);
            house->getChoam().availableItems=keepMarket;airPort->updateBuildList();
            raiders.erase(std::find(raiders.begin(),raiders.end(),worker));worker->destroy();
            buildings.erase(std::find(buildings.begin(),buildings.end(),heavy));delete heavy;
            house->numItemLosses[Structure_HeavyFactory]=keepHeavyLosses;
            house->numItemLosses[Unit_Harvester]=keepWorkerLosses;
            bot->recentStructureLosses=keepRecentLosses;
            bot->lastCalculatedSpice=keepSpice;
            house->storedCredits=keepStored;house->startingCredits=keepStarting;house->cityCredits=keepCity;
            clear();
        }
        auto urgent=airPlan();
        require(urgent.airEngaged && urgent.capitalChoice>=0
            && std::string_view(urgent.capitalCandidates[urgent.capitalChoice].kind)=="defence_upgrade"
            && urgent.capitalCandidates[urgent.capitalChoice].urgent
            && urgent.firstAirDefenceReserve==upgradePrice+radarPrice+rocketPrice,
            "The physical first-AA city did not price its native urgent unlock chain");
        auto competing=urgent.capitalCandidates[urgent.capitalChoice];
        competing.builder=airPort->getObjectID();competing.item=Unit_Tank;
        competing.price=importPrice;competing.kind="military";competing.reason="military_shortfall";
        competing.urgent=false;competing.score=urgent.capitalCandidates[urgent.capitalChoice].score+1;
        urgent.capitalCandidates.push_back(competing);
        auto growth=competing;growth.builder=yard->getObjectID();growth.item=Structure_NuclearPlant;
        growth.kind="city_growth_power";growth.reason="power_for_demanded_growth";growth.score=1;
        growth.price=game.objectData.data[growth.item][technology].price;
        growth.site=bot->findPlaceLocation(growth.item);urgent.capitalCandidates.push_back(growth);
        bot->chooseCapitalPlan(urgent);
        require(urgent.protectionCapital && !urgent.cityGrowthProtected
            && std::string_view(urgent.capitalCandidates[urgent.capitalChoice].kind)=="defence_upgrade",
            "Military/growth interleave displaced the affordable native urgent AA upgrade");
        auto capacity=urgent;
        capacity.capitalCandidates[capacity.capitalCandidates.size()-2].kind="production";
        capacity.capitalCandidates[capacity.capitalCandidates.size()-2].item=Structure_HeavyFactory;
        bot->chooseCapitalPlan(capacity);
        require(std::string_view(capacity.capitalCandidates[capacity.capitalChoice].kind)=="defence_upgrade",
            "Hypothetical production capacity displaced urgent AA service");
        const auto powered=house->producedPower;
        house->producedPower=0;
        bot->buildProducer(urgent,yard);
        require(yard->getProductionQueueSize()>0
            && (yard->getCurrentProducedItem()==Structure_WindTrap || yard->getCurrentProducedItem()==Structure_NuclearPlant)
            && !yard->isUpgrading(),"Urgent AA displaced genuine blackout recovery");
        clear();house->producedPower=powered;
        urgent=airPlan();bot->buildProducer(urgent,yard);
        require(yard->isUpgrading(),"Selected urgent AA did not reach the native upgrade consumer");
        require(bot->capitalReserve(urgent,airPort->getObjectID())==radarPrice+rocketPrice,
            "The newly accepted AA upgrade quote was reserved again in the same pass");
        ++game.gameCycleCount;require(yard->update(),"First-AA yard died before its first payment");
        auto upgrading=airPlan();
        require(yard->getUpgradeProgress()>0 && upgrading.queuedProductionCost>0
            && upgrading.firstAirDefenceReserve==radarPrice+rocketPrice,
            "An active yard upgrade lost the next AA steps or reserved its queued debt twice");
        for (Uint32 tick=0;yard->isUpgrading() && tick<MILLI2CYCLES(60000);++tick) {
            ++game.gameCycleCount;require(yard->update(),"First-AA yard died during native upgrade");
        }
        require(!yard->isUpgrading() && yard->getCurrentUpgradeLevel()>=rocketLevel,
            "The healthy funded AA yard did not finish its native paid upgrade");
        exactCoreAA(Structure_Radar,false);
        auto radarPlan=airPlan();bot->buildProducer(radarPlan,yard);
        require(yard->getCurrentProducedItem()==Structure_Radar,
            "The AA upgrade did not hand its actual budget to native Radar production");
        require(bot->capitalReserve(radarPlan,airPort->getObjectID())==rocketPrice,
            "The newly accepted Radar quote was reserved again in the same pass");
        ++game.gameCycleCount;require(yard->update(),"First-AA Radar yard died before payment");
        auto radarBusy=airPlan();
        require(yard->getProductionProgress()>0 && radarBusy.queuedProductionCost>0
            && radarBusy.firstAirDefenceReserve==rocketPrice
            && radarBusy.money==std::max(0,house->getCredits()-radarBusy.queuedProductionCost),
            "A partially paid Radar lost the next rocket or double-booked existing queue debt");
        auto* spareYard=static_cast<BuilderBase*>(put(Structure_ConstructionYard,8,18));
        spareYard->curUpgradeLev=spareYard->getMaxUpgradeLevel();
        spareYard->setHealth(spareYard->getMaxHealth());spareYard->updateBuildList();
        auto clearSpare=[&] {
            while (spareYard->getProductionQueueSize()) spareYard->doCancelItem(spareYard->getCurrentProducedItem());
            bot->builderPlaceLocations.erase(spareYard->getObjectID());
            bot->reservedStructures.erase(spareYard->getObjectID());
            bot->clearPlacementCache();
        };
        auto queuedCapital=[&](bool rocketPending) {
            const bool keepCompact=config->force.cityCompactPower;
            const bool keepFinance=config->force.cityCompactPowerInstallments;
            config->force.cityCompactPower=true;config->force.cityCompactPowerInstallments=true;
            auto pendingAA=airPlan();
            const int freeCash=std::max(0,pendingAA.money-pendingAA.remainingFirstAirDefenceCash());
            require(house->getNumItems(Structure_RocketTurret)==0 && pendingAA.queuedProductionCost>0
                && pendingAA.itemCount[Structure_RocketTurret]==int(rocketPending)
                && (rocketPending ? pendingAA.firstAirDefenceReserve==0 : pendingAA.firstAirDefenceReserve==rocketPrice)
                && house->getProducedPower()-house->getPowerRequirement()
                    >=pendingAA.committedPowerDemand+pendingAA.rocketPowerBuffer,
                "Two-yard fixture does not separate protected AA debt from adequate current power");
            auto nuclearStart=[&] {
                auto pass=airPlan();
                // Isolate this native quote from unrelated optional capital
                // owners; the observed AA inventory, cash and debt stay intact.
                pass.capitalChoice=-1;
                bot->planningBuilder=spareYard->getObjectID();bot->clearPlacementCache(false,true);
                const int quote=bot->purchasePrice(spareYard,Structure_NuclearPlant);
                const int first=CityEconomyInvestmentPolicy::firstInstallment(
                    quote,bot->cityBuildTicks(pass,Structure_NuclearPlant));
                require(quote>freeCash && first>0 && pass.money>=first
                    && bot->campaignAvailableToBuild(spareYard,Structure_NuclearPlant)
                    && bot->cityAdmitsStructure(pass,Structure_NuclearPlant)
                    && bot->findPlaceLocation(Structure_NuclearPlant).isValid()
                    && bot->campaignAvailableToBuild(spareYard,Structure_WindTrap)
                    && bot->findPlaceLocation(Structure_WindTrap).isValid(),
                    "The competing Nuclear project is not a legal first-tick-funded, full-quote-unfunded start");
                const auto credits=house->getStartingCredits()+house->getEarnedCredits();
                QuantBot::BuilderTurn turn(pass.money);turn.pStructure=spareYard;turn.pBuilder=spareYard;
                bot->beginBuilderTurn(pass,turn);bot->reserveBuilderCash(pass,turn);
                require(pass.money>=first,"An unrelated reserve, not pending AA, blocked the first installment");
                turn.installmentProject=Structure_NuclearPlant;turn.installmentProjectStartBudget=first;
                turn.compactPowerInstallment=true;
                require(!bot->queueFoundedStructure(pass,turn,Structure_NuclearPlant,__LINE__,"first_aa_competing_nuclear")
                    && spareYard->getProductionQueueSize()==0
                    && credits==house->getStartingCredits()+house->getEarnedCredits(),
                    "Another yard borrowed pending first-AA cash for a financed Nuclear package");
            };
            nuclearStart();
            if (rocketPending) {
                // Native full-yard regression for the reviewed loop: a first
                // rocket is queued, Windtrap cash exists, and the legacy
                // positive-income nuclearPlan would otherwise reselect an
                // inadmissible financed reactor on every visit.
                const auto keepStored=house->storedCredits, keepStarting=house->startingCredits, keepCity=house->cityCredits;
                const auto keepPower=house->producedPower;
                const int windQuote=bot->purchasePrice(spareYard,Structure_WindTrap);
                const int nuclearQuote=bot->purchasePrice(spareYard,Structure_NuclearPlant);
                require(windQuote>0 && nuclearQuote>windQuote,"The generator boundary has no cheaper native wind quote");
                house->storedCredits=0;house->cityCredits=0;
                house->startingCredits=pendingAA.queuedProductionCost+windQuote;
                house->producedPower=house->getPowerRequirement();
                for (int visit=0;visit<2;++visit) {
                    auto power=airPlan();
                    power.capitalChoice=-1;power.protectionCapital=false;power.cityGrowthProtected=false;
                    power.nuclearPlan=true;power.forecastNetIncome=std::max(power.forecastNetIncome,nuclearQuote);
                    power.cityPowerReserve=std::max(power.cityPowerReserve,
                        power.rocketPowerBuffer+power.committedPowerDemand);
                    require(power.money==windQuote && power.firstAirDefenceReserve==0
                        && power.itemCount[Structure_RocketTurret]>0 && bot->firstAirDefencePending(power),
                        "Queued-rocket generator control did not retain real AA debt and exact Windtrap cash");
                    bot->buildProducer(power,spareYard);
                    require(spareYard->getProductionQueueSize()>0
                        && spareYard->getCurrentProducedItem()==Structure_WindTrap,
                        "The financed Nuclear veto ended the yard turn instead of retaining legal Windtrap fallback");
                    clearSpare();
                }
                house->storedCredits=keepStored;house->startingCredits=keepStarting;house->cityCredits=keepCity;
                house->producedPower=keepPower;
            }
            if (!rocketPending) {
                // F1 is an actual full-build consumer: its Light Factory may
                // fit M, but not free cash after the still-unordered rocket.
                config->force.firstFactoryOwnPrerequisites=true;
                auto firstLane=airPlan();
                require(firstLane.capitalChoice>=0
                    && std::string_view(firstLane.capitalCandidates[firstLane.capitalChoice].kind)=="first_factory"
                    && firstLane.capitalCandidates[firstLane.capitalChoice].builder==spareYard->getObjectID()
                    && firstLane.capitalCandidates[firstLane.capitalChoice].price<=firstLane.money
                    && firstLane.capitalCandidates[firstLane.capitalChoice].price>freeCash,
                    "The competing F1 factory does not straddle the native AA free-cash boundary");
                bot->buildProducer(firstLane,spareYard);
                require(spareYard->getProductionQueueSize()==0 && !spareYard->isUpgrading(),
                    "F1 spent the first-rocket balance while the other yard's Radar was queued");
                config->force.firstFactoryOwnPrerequisites=false;
                // Some real power need is still not a reason to finance the
                // larger reactor when a native Windtrap is timely and legal.
                const auto keepPower=house->producedPower;
                house->producedPower=house->getPowerRequirement();
                nuclearStart();
                auto power=airPlan();
                power.capitalChoice=-1;
                const int windQuote=bot->purchasePrice(spareYard,Structure_WindTrap);
                require(power.money>=windQuote && windQuote>0,"The canonical power prerequisite is not fundable");
                {
                    QuantBot::BuilderTurn turn(power.money);turn.pStructure=spareYard;turn.pBuilder=spareYard;
                    bot->beginBuilderTurn(power,turn);bot->reserveBuilderCash(power,turn);
                    require(bot->queueFoundedStructure(power,turn,Structure_WindTrap,__LINE__,"first_aa_real_power")
                        && spareYard->getCurrentProducedItem()==Structure_WindTrap,
                        "First-AA protection blocked the canonical real power prerequisite");
                }
                clearSpare();house->producedPower=keepPower;
            }
            // The other yard is not globally disabled: an actual full founded
            // quote fitting the remaining free cash still enters a real queue.
            auto funded=airPlan();
            funded.capitalChoice=-1;
            Uint32 fundedItem=NONE_ID;
            bot->planningBuilder=spareYard->getObjectID();bot->clearPlacementCache(false,true);
            for (Uint32 item:{Structure_ZoneResidential,Structure_ZoneCommercial,Structure_ZoneIndustrial})
                if (bot->campaignAvailableToBuild(spareYard,item) && bot->purchasePrice(spareYard,item)>0
                    && bot->purchasePrice(spareYard,item)<=freeCash && bot->findPlaceLocation(item).isValid()) {
                    fundedItem=item;break;
                }
            require(fundedItem!=NONE_ID,"The two-yard fixture has no legal fully funded free-cash control");
            {
                QuantBot::BuilderTurn turn(funded.money);turn.pStructure=spareYard;turn.pBuilder=spareYard;
                bot->beginBuilderTurn(funded,turn);bot->reserveBuilderCash(funded,turn);
                require(bot->queueFoundedStructure(funded,turn,fundedItem,__LINE__,"first_aa_fully_funded_parallel")
                    && spareYard->getCurrentProducedItem()==fundedItem,
                    "First-AA protection rejected a fully funded free-cash parallel project");
            }
            clearSpare();
            config->force.cityCompactPower=keepCompact;config->force.cityCompactPowerInstallments=keepFinance;
        };
        queuedCapital(false);
        house->producedPower=0;
        auto blackedOut=airPlan();
        const auto beforeImport=house->getStartingCredits()+house->getEarnedCredits();
        const auto portQueue=airPort->getProductionQueueSize();
        {
            QuantBot::BuilderTurn turn(blackedOut.money);turn.pStructure=airPort;turn.pBuilder=airPort;
            bot->beginBuilderTurn(blackedOut,turn);bot->reserveBuilderCash(blackedOut,turn);
            require(blackedOut.firstAirDefenceReserve==rocketPrice && blackedOut.money<importPrice
                && !bot->produceItemWithLogging(blackedOut,turn,Unit_Tank,__LINE__,"first_aa_native_import_boundary"),
                "An unpowered discretionary import spent first-rocket savings while Radar was busy");
        }
        require(airPort->getProductionQueueSize()==portQueue
            && beforeImport==house->getStartingCredits()+house->getEarnedCredits(),
            "Refused first-AA import changed the real queue or credits");
        config->force.defencePrerequisites=false;
        require(airPlan().firstAirDefenceReserve==0,"DefencePrerequisites OFF retained the new first-AA hold");
        config->force.defencePrerequisites=true;house->producedPower=powered;
        auto finishBuilding=[&](Uint32 item) {
            const int before=house->getNumBuiltItems(item);
            const auto beforePayment=house->getStartingCredits()+house->getEarnedCredits();
            auto pass=airPlan();
            const int ticks=3*bot->cityBuildTicks(pass,item)+int(MILLI2CYCLES(60000));
            for (int tick=0;house->getNumBuiltItems(item)==before && tick<ticks;++tick) {
                ++game.gameCycleCount;require(yard->update(),"First-AA yard died during native construction");
                if (yard->isWaitingToPlace()) { auto placement=airPlan();bot->buildProducer(placement,yard); }
            }
            for (auto* building:structureList)
                if (building->getOwner()==house && building->getItemID()==item
                    && std::find(buildings.begin(),buildings.end(),building)==buildings.end()) buildings.push_back(building);
            require(house->getNumBuiltItems(item)>before && house->getNumItems(item)>0
                && house->getStartingCredits()+house->getEarnedCredits()<beforePayment,
                "First-AA building did not physically complete with native payments and placement");
        };
        finishBuilding(Structure_Radar);
        exactCoreAA(Structure_RocketTurret,false);
        // Powered is not the same as enough headroom for an operational rocket.
        // Its real enabling generator must still precede the protected turret.
        house->producedPower=house->getPowerRequirement();
        auto powerPrerequisite=airPlan();bot->buildProducer(powerPrerequisite,yard);
        require(yard->getProductionQueueSize()>0
            && (yard->getCurrentProducedItem()==Structure_WindTrap || yard->getCurrentProducedItem()==Structure_NuclearPlant),
            "Protected AA skipped the actual turret-power prerequisite");
        clear();house->producedPower=powered;
        // OFF retains the original one-yard military-to-growth arbitration
        // and its early yard allocation, even with buildable urgent AA.
        growth.item=Structure_WindTrap;growth.price=bot->purchasePrice(yard,Structure_WindTrap);
        bot->planningBuilder=yard->getObjectID();growth.site=bot->findPlaceLocation(Structure_WindTrap);
        {
            config->force.defencePrerequisites=false;
            auto legacy=airPlan();
            const auto offer=std::find_if(legacy.capitalCandidates.begin(),legacy.capitalCandidates.end(),[](const auto& candidate) {
                return candidate.item==Structure_RocketTurret && candidate.urgent
                    && std::string_view(candidate.reason)=="air_coverage";
            });
            require(offer!=legacy.capitalCandidates.end() && growth.site.isValid() && legacy.money>=growth.price,
                "The OFF control lacks native urgent rocket and affordable growth-power alternatives");
            const auto air=*offer;
            legacy.capitalCandidates={air,competing,growth};legacy.cityYards=1;
            legacy.nuclearPlan=false;
            bot->chooseCapitalPlan(legacy);
            require(legacy.capitalChoice>=0
                && std::string_view(legacy.capitalCandidates[legacy.capitalChoice].kind)=="city_growth_power",
                "DefencePrerequisites OFF changed the original military-to-growth choice");
            bot->buildProducer(legacy,yard);
            require(yard->getProductionQueueSize()>0
                && (yard->getCurrentProducedItem()==Structure_WindTrap || yard->getCurrentProducedItem()==Structure_NuclearPlant),
                "DefencePrerequisites OFF lost the original early growth allocation to AA");
            clear();config->force.defencePrerequisites=true;
        }
        auto firstRocket=airPlan();firstRocket.moderateCrimeProperties=1;
        firstRocket.capitalCandidates.clear();firstRocket.capitalChoice=-1;
        bot->planCapitalCandidates(firstRocket);
        firstRocket.capitalCandidates.push_back(competing);firstRocket.capitalCandidates.push_back(growth);
        firstRocket.cityYards=1;bot->chooseCapitalPlan(firstRocket);
        require(firstRocket.capitalChoice>=0
            && firstRocket.capitalCandidates[firstRocket.capitalChoice].item==Structure_RocketTurret
            && firstRocket.capitalCandidates[firstRocket.capitalChoice].urgent,
            "Moderate crime relabelled the first urgent AA response as optional service");
        bot->buildProducer(firstRocket,yard);
        require(yard->getCurrentProducedItem()==Structure_RocketTurret,
            "The protected Radar-to-first-rocket budget did not reach native production");
        require(bot->capitalReserve(firstRocket,airPort->getObjectID())==0,
            "The accepted first rocket kept a second same-pass cash reservation");
        queuedCapital(true);
        finishBuilding(Structure_RocketTurret);
        require(airPlan().firstAirDefenceReserve==0,"A physical first rocket retained the bounded AA hold");
        SDL_Log("FIRST_AA_DEFENCE_PASS: upgrade=paid radar=paid_and_placed rocket=paid_and_placed import=held_during_blackout");
        starportRule=keepStarportRule;
        SDL_Log("THREAT_TURRET_PASS: observed=8 goal=%d local=%d site=%d,%d hidden=ignored cap=12 starport=saved_and_queued",
            on.groundTurretGoal,on.groundTurretCover,site.x,site.y);
        clear();
    } catch (...) { cleanup();throw; }
    cleanup();
}
