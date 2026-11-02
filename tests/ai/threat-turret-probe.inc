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
        auto supply=[&](bool enabled,int cash) {
            config->force.cityStarportSupply=enabled;
            auto pass=plan(false,true);
            pass.capitalCandidates.clear();pass.capitalChoice=-1;pass.capitalConsumed=false;
            pass.cityYards=1;pass.cityZonesIncludingQueued=12;pass.cityStorageCapacityIncludingQueued=1000;
            pass.itemCount[Structure_ZoneResidential]=6;pass.itemCount[Structure_ZoneCommercial]=3;
            pass.itemCount[Structure_ZoneIndustrial]=3;pass.starportMarketAvailable=true;
            pass.forecastTax=1000;pass.forecastUpkeep=100;pass.forecastFunding=3600;
            pass.cashFlow.projectedCash=1000;pass.money=cash;
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
        SDL_Log("THREAT_TURRET_PASS: observed=8 goal=%d local=%d site=%d,%d hidden=ignored cap=12 starport=saved_and_queued",
            on.groundTurretGoal,on.groundTurretCover,site.x,site.y);
        clear();
    } catch (...) { cleanup();throw; }
    cleanup();
}
