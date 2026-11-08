// Reproduce the level-four 100-harvester override and oversized readiness goal.
void runCampaignPacingProbe(Game& game) {
    auto require=[](bool ok,const char* reason){if(!ok){SDL_Log("CAMPAIGN_PACING_FAIL: %s",reason);throw std::runtime_error(reason);}};
    std::vector<QuantBot*> enemies;QuantBot* partner=nullptr;
    game.gameCycleCount=123;
    for(int h=0;h<NUM_HOUSES;++h) if(auto* house=game.getHouse(h))
        for(const auto& player:house->getPlayerList()) if(auto* bot=dynamic_cast<QuantBot*>(player.get())) {
            bot->update();
            if(bot->isCampaignEnemy()) enemies.push_back(bot);else if(h==HOUSE_HARKONNEN) partner=bot;
        }
    require(partner && !enemies.empty(),"Pacing fixture needs partner and enemy");
    auto* bot=enemies.front();
    for(auto tier:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
        bot->difficulty=tier;bot->initialItemCount[Structure_Refinery]=2;
        bot->initialMilitaryValue=1000;bot->lastCalculatedSpice=200000;bot->harvesterLimit=100;
        game.gameInitSettings.gameOptions.maximumNumberOfHarvestersOverride=100;
        bot->update();
        const int expected=tier==QuantBot::Difficulty::Brutal ? 7 : 2*getQuantBotConfig().getSettings(int(tier)).harvesterLimitPerRefineryMultiplier;
        require(bot->harvesterLimit==expected,"High game ceiling replaced campaign difficulty economy cap");
        bot->update();require(bot->harvesterLimit==expected,"Campaign worker cap drifted on next update");
        game.gameInitSettings.gameOptions.maximumNumberOfHarvestersOverride=1;
        bot->update();require(bot->harvesterLimit==1,"Lower player ceiling was ignored");
        SDL_Log("CAMPAIGN_WORKER_LIMIT_PASS: tier=%d initial_refineries=2 limit=%d",int(tier),expected);
    }
    game.gameInitSettings.gameOptions.maximumNumberOfHarvestersOverride=100;
    partner->lastCalculatedSpice=200000;partner->update();
    require(partner->harvesterLimit>2,"Human partner incorrectly inherited small enemy worker cap");
    partner->difficulty=QuantBot::Difficulty::Brutal;
    game.gameInitSettings.gameOptions.maximumNumberOfHarvestersOverride=15;
    partner->update();require(partner->harvesterLimit==15,"Brutal helper inherited the enemy seven-worker cap");
    const auto savedMission=game.gameInitSettings.mission;
    auto* partnerHouse=const_cast<House*>(partner->getHouse());
    const int savedWorkerLimit=partnerHouse->maxHarvesters;
    partnerHouse->maxHarvesters=15;
    game.gameInitSettings.gameOptions.maximumNumberOfHarvestersOverride=-1;
    for (int mission : {17,20,21,22}) {
        game.gameInitSettings.mission=mission;
        const int expected=mission>=20 ? 20 : 100;
        partner->lastCalculatedSpice=200000;partner->update();
        require(partner->harvesterLimit==expected && partnerHouse->getMaxHarvesters()==0,
            "Friendly Brutal default did not distinguish campaign levels seven and eight/nine");
    }
    partner->difficulty=QuantBot::Difficulty::Hard;partner->update();
    require(partnerHouse->getMaxHarvesters()==0 && partner->harvesterLimit==100,"Hard retained the old default engine ceiling");
    partner->difficulty=QuantBot::Difficulty::Brutal;
    partner->lastCalculatedSpice=18000;partner->update();
    require(partner->harvesterLimit==9 && partnerHouse->getMaxHarvesters()==0,"Maximum20 became a mandatory worker target");
    game.gameInitSettings.gameOptions.maximumNumberOfHarvestersOverride=3;partner->update();
    require(partner->harvesterLimit==3 && partner->campaignAllyHarvesterLimit()==0,"Explicit worker override ignored");
    game.gameInitSettings.mission=savedMission;partnerHouse->maxHarvesters=savedWorkerLimit;
    game.gameInitSettings.gameOptions.maximumNumberOfHarvestersOverride=15;
    partner->lastCalculatedSpice=200000;partner->update();
    partner->gameMode=QuantBot::GameMode::Campaign;
    partner->attackTimer=MILLI2CYCLES(720000);partner->update();
    require(partner->attackTimer==0 && partner->campaignCanLaunch(),"New ally retained campaign opening delay");
    partner->attackTimer=MILLI2CYCLES(720000);partner->update();
    require(partner->attackTimer==0,"Loaded ally retained legacy opening delay");
    partner->attack(100000);
    require(partner->attackTimer==MILLI2CYCLES(60000),"Campaign ally attack interval is not60 seconds");
    partner->update();
    require(partner->attackTimer==MILLI2CYCLES(60000),"Ally lost its break between attacks");
    SDL_Log("ALLIED_CAMPAIGN_LIMITS_PASS: no default engine cap, level8/9 Brutal house target20, override3, spice target9, attack interval60s");
    auto* allyHouse=const_cast<House*>(bot->getHouse());
    const int enemyTeam=allyHouse->teamID;
    allyHouse->teamID=partner->getHouse()->getTeamID();
    bot->difficulty=QuantBot::Difficulty::Brutal;bot->lastCalculatedSpice=200000;
    bot->update();
    require(bot->isAlliedWithHuman() && !bot->isCampaignEnemy(),"Separate ally misclassified as enemy");
    require(bot->harvesterCountCeiling()==0 && bot->harvesterLimit==15,"Separate ally retained enemy worker cap");
    require(bot->gameMode==QuantBot::GameMode::Custom,"Separate ally retained campaign rebuild logic");
    allyHouse->teamID=enemyTeam;bot->gameMode=QuantBot::GameMode::Campaign;
    bot->update();require(bot->harvesterCountCeiling()==7 && bot->harvesterLimit==7,"Opponent lost seven-worker cap");
    for(auto* enemy:enemies) {
        enemy->difficulty=QuantBot::Difficulty::Easy;enemy->campaignWave={};
        enemy->campaignWave.initialized=true;enemy->attackTimer=INT32_MAX;
    }
    // Ensure a usable army exists; these real troops still pass through the
    // normal percentage selection and shared-wave budget code.
    auto savedUnits=unitList;std::vector<UnitBase*> army;
    auto* owner=const_cast<House*>(bot->getHouse());int spawned=0;
    for(int y=1;y<currentGameMap->getSizeY()-1 && spawned<8;++y)
        for(int x=1;x<currentGameMap->getSizeX()-1 && spawned<8;++x)
            if(!currentGameMap->getTile(x,y)->hasAGroundObject() && !currentGameMap->getTile(x,y)->isMountain()) {
                auto* unit=owner->createUnit(Unit_Tank);require(unit,"Pacing tank creation failed");
                unit->deploy(Coord(x,y));bot->doSetAttackMode(unit,AREAGUARD);army.push_back(unit);++spawned;
            }
    savedUnits=unitList;unitList.clear();for(auto* unit:army)unitList.push_back(unit);
    try {
    bot->militaryValueLimit=4800;bot->attackTimer=0;
    game.techLevel=4;bot->campaignWave.opening=game.getGameCycleCount()+100;
    bot->attack(2400);require(bot->campaignWave.members.empty(),"Small-wave readiness bypassed opening grace");
    game.gameCycleCount+=100;
    bot->attack(2399);require(bot->campaignWave.members.empty(),"Easy launched without its defensive reserve");
    bot->attackTimer=0; // Readiness retry after its ordinary countdown.
    bot->attack(2400);
    require(bot->campaignWave.members.size()==4,"Eight tanks did not send four while keeping four defenders");
    const auto first=bot->campaignWave.members;bot->attack(2400);
    require(bot->campaignWave.members==first,"Readiness change topped up an active wave");
    const auto used=bot->campaignPressure();const auto limit=bot->campaignProfile();
    require(used.houses<=limit.houses && used.units<=limit.units && used.value<=limit.value,"Small wave exceeded assault limits");
    // Late-game threshold regressions: the same readiness calculation serves
    // enemy fairness and dispatch; a full partner also escapes a depleted map.
    game.gameCycleCount=MILLI2CYCLES(16*60000);game.techLevel=9;
    bot->lastCalculatedSpice=0;partner->lastCalculatedSpice=0;
    // Keep the house's remaining credits: idle money was another deadlock.
    require(bot->campaignRequiredArmy(16000)==0,"Exhausted enemy still demands an unaffordable army");
    require(partner->campaignRequiredArmy(16000)==0,"Exhausted partner still waits for 16000 army value");
    partner->lastCalculatedSpice=1000;
    require(partner->campaignRequiredArmy(8000)==8000,"Live economy lost its normal readiness goal");
    bot->campaignWave={};bot->campaignWave.initialized=true;
    require(!owner->hasRepairYard(),"Fixture must lack a repair yard");
    bot->attackTimer=0;bot->attack(2400);
    require(!bot->campaignWave.members.empty(),"Late-game survivors did not attack without a repair yard");
    } catch (...) {unitList=savedUnits;throw;}
    unitList=savedUnits;
    SDL_Log("CAMPAIGN_PACING_PASS: eight 300-value tanks send four and retain four at 2400 readiness; opening and shared limits preserved");
    UnitBase* scout=nullptr;
    for(auto* unit:unitList) if(unit->getOwner()==partner->getHouse() && unit->isActive()
        && unit->isAGroundUnit() && unit->canAttack()) {scout=unit;break;}
    require(scout,"Missing helper scouting fixture unit");
    scout->setHealth(scout->getMaxHealth());
    for(auto* building:structureList)
        if(building->getOwner()->getTeamID()!=partner->getHouse()->getTeamID())
            building->setVisible(partner->getHouse()->getTeamID(),false);
    for(int y=0;y<currentGameMap->getSizeY();++y)
        for(int x=0;x<currentGameMap->getSizeX();++x)for(auto& explored:currentGameMap->getTile(x,y)->explored)explored=false;
    partner->doMove2Pos(scout,scout->getX(),scout->getY(),false);
    partner->doSetAttackMode(scout,AREAGUARD);
    require(!partner->campaignControlsUnit(scout),"Home defender was reassigned to scouting");
    partner->doSetAttackMode(scout,HUNT);
    require(partner->campaignControlsUnit(scout),"Idle helper attacker failed to explore");
    require(scout->getDestination().isValid() && scout->getDestination()!=scout->getLocation()
        && !currentGameMap->getTile(scout->getDestination())->isExploredByTeam(partner->getHouse()->getTeamID()),
        "Scout did not receive an unexplored terrain destination");
    partner->doMove2Pos(scout,scout->getX(),scout->getY(),false);
    partner->doSetAttackMode(scout,HUNT);partner->onHumanUnitOrder(scout->getObjectID());
    require(!partner->campaignControlsUnit(scout),"Scouting replaced a manual unit order");
    SDL_Log("CAMPAIGN_SCOUT_PASS: dispatched helper explores; home defender and human orders preserved");
    {
        // This house has no human on its team. Selecting it as the campaign
        // player reproduces the sole-controller role without inventing a human.
        const auto selectedHouse=game.gameInitSettings.getHouseID();
        const auto savedConfig=bot->configOverride;
        auto config=std::make_shared<QuantBotConfig>(bot->botConfig());
        bot->configOverride=config;
        config->force.campaignPlayerHouse=true;
        require(bot->isCampaignEnemy(),"Player-house switch changed an authored opponent's role");
        require(partner->isAlliedWithHuman() && !partner->isCampaignEnemy(),"Player-house switch changed a human partner's role");
        game.gameInitSettings.setHouseID(static_cast<HOUSETYPE>(bot->getHouse()->getHouseID()));
        config->force.campaignPlayerHouse=false;
        require(!bot->isAlliedWithHuman() && bot->isCampaignEnemy(),"OFF did not retain the legacy sole-controller role");
        bot->gameMode=QuantBot::GameMode::Campaign;
        bot->campaignWave={};
        bot->campaignWave.initialized=true;
        bot->campaignWave.opening=std::numeric_limits<Uint32>::max();
        bot->attackTimer=std::numeric_limits<Sint32>::max();
        config->force.campaignPlayerHouse=true;
        bot->update();
        require(bot->isAlliedWithHuman() && !bot->isCampaignEnemy(),"Selected campaign house still treated as an enemy without a human");
        require(bot->gameMode==QuantBot::GameMode::Custom,"Selected campaign house retained campaign enemy economic planning");
        require(bot->harvesterCountCeiling()==0,"Selected campaign house retained the enemy worker cap");
        require(bot->attackTimer==0 && bot->campaignCanLaunch(),"Selected campaign house retained the missing-trigger opening hold");
        bot->updateCampaignWave();
        require(bot->campaignCanLaunch(),"Enemy wave update re-gated the player house");
        game.gameInitSettings.setHouseID(selectedHouse);
        require(bot->isCampaignEnemy(),"Unselected enemy house lost its campaign role");
        bot->configOverride=savedConfig;
        SDL_Log("CAMPAIGN_PLAYER_HOUSE_PASS: OFF legacy role; ON selected sole controller uses Custom planning and attacks; enemy/partner unchanged");
    }
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=pacing outcome=passed");
}
