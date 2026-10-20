// Real production selector and reserve movement; never linked into the game.
void runCustomAttackProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    QuantBot* bot=nullptr;
    for(const auto& player:house->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get()))bot=candidate;
    require(bot,"Custom attack fixture missing bot");
    game.gameType=GameType::CustomGame;
    bot->gameMode=QuantBot::GameMode::Custom;bot->supportMode=false;
    bot->manualUnitOrders.clear();bot->defenceAssignments.clear();
    std::vector<UnitBase*> army;
    for(int y=1;y<currentGameMap->getSizeY()-1 && army.size()<76;++y)
        for(int x=1;x<currentGameMap->getSizeX()-1 && army.size()<76;++x) {
            const auto* tile=currentGameMap->getTile(x,y);
            if(tile->hasAGroundObject() || tile->isMountain())continue;
            auto* unit=house->createUnit(Unit_Trooper);
            require(unit,"Custom attack fixture creation failed");
            unit->deploy(Coord(x,y));army.push_back(unit);
        }
    require(army.size()==76,"Custom attack fixture needs 76 empty tiles");
    auto saved=unitList;unitList.clear();
    for(auto* unit:army)unitList.push_back(unit);
    try {
        auto reset=[&](QuantBot::Difficulty difficulty) {
            bot->difficulty=difficulty;
            for(auto* unit:army) {bot->doSetAttackMode(unit,GUARD);unit->setForced(false);}
        };
        auto hunters=[&] {
            int n=0;for(auto* unit:army)if(unit->getAttackMode()==HUNT)++n;return n;
        };
        // Verify compiled defaults and the independently loaded shipped config.
        // Wave sizes below depend on owned troops, not the production target.
        const QuantBotConfig defaults;
        require(defaults.easy.attackForceMilitaryValueRatio==0.25f
            && defaults.medium.attackForceMilitaryValueRatio==0.40f
            && defaults.hard.attackForceMilitaryValueRatio==0.50f
            && defaults.brutal.attackForceMilitaryValueRatio==0.60f,
            "Default custom attack shares are not 25/40/50/60 percent");
        require(defaults.easy.militaryValueLimitCustomLargeMap==15000
            && defaults.easy.militaryValueLimitCustomHugeMap==25000
            && defaults.medium.militaryValueLimitCustomLargeMap==25000
            && defaults.medium.militaryValueLimitCustomHugeMap==40000,
            "Default Easy/Medium large and huge military limits drifted");
        const auto& loaded=getQuantBotConfig();
        require(loaded.easy.militaryValueLimitCustomLargeMap==15000
            && loaded.easy.militaryValueLimitCustomHugeMap==25000
            && loaded.medium.militaryValueLimitCustomLargeMap==25000
            && loaded.medium.militaryValueLimitCustomHugeMap==40000,
            "Shipped config did not load the approved military limits");
        // An enormous production target must not enlarge this 7600-value army's attack.
        bot->militaryValueLimit=999999;
        getQuantBotConfig().easy.attackForceMilitaryValueRatio=0.25f;
        getQuantBotConfig().medium.attackForceMilitaryValueRatio=0.40f;
        // 76 troopers at 100 credits: 25 percent buys 19, 40 percent buys 30.
        reset(QuantBot::Difficulty::Easy);bot->launchGroundHunt();
        require(hunters()==19,"Custom Easy did not send its 25-percent share");
        // Forced movement must not remove survivors from the counted roster.
        for(auto* unit:army)if(unit->getAttackMode()==HUNT)unit->setForced(true);
        bot->launchGroundHunt();
        require(hunters()==19,"A repeated dispatch or forced hunter exceeded the share");
        // Custom Medium left the share path: it launches whole-army waves like Hard and Brutal
        // (docs/ai-scenario-matrix.md, "Difficulty ladder"; quantbot_whole_army_dispatch).
        reset(QuantBot::Difficulty::Medium);
        require(bot->autonomousCustomArmy(),"Custom Medium is not an autonomous custom army");
        // The difficulty steps (docs/ai-scenario-matrix.md, "Difficulty separation"): the production
        // strategy is Hard's and Brutal's alone.
        require(!bot->playsStrategy(),"Custom Medium plays the Hard/Brutal strategy");
        reset(QuantBot::Difficulty::Hard);
        require(bot->playsStrategy(),"Custom Hard lost its strategy");
        reset(QuantBot::Difficulty::Brutal);
        require(bot->playsStrategy(),"Custom Brutal lost its strategy");
        reset(QuantBot::Difficulty::Easy);
        require(!bot->playsStrategy(),"Custom Easy plays a strategy");
        for(auto difficulty:{QuantBot::Difficulty::Easy}) {
            reset(difficulty);bot->launchGroundHunt();
            auto* reserve=army.back();
            bot->squadRallyLocation=army[army.size()-2]->getLocation();
            bot->moveToOptimalSquadPosition(reserve,1);
            require(reserve->getGuardPoint()==bot->squadRallyLocation,
                "Reserve followed offensive centroid instead of home rally");
            auto* hunter=army.front();const auto guard=hunter->getGuardPoint();
            hunter->setForced(false);bot->moveToOptimalSquadPosition(hunter,1);
            require(hunter->getAttackMode()==HUNT && hunter->getGuardPoint()==guard,
                "Home rally stole an active attacker");
        }
        reset(QuantBot::Difficulty::Easy);bot->launchGroundHunt();
        game.gameType=GameType::Campaign;
        auto* helperReserve=army.back();
        bot->doMove2Pos(helperReserve,helperReserve->getX(),helperReserve->getY(),false);
        helperReserve->setForced(false);
        const auto assaultCenter=bot->findSquadCenter(house->getHouseID());
        bot->moveToOptimalSquadPosition(helperReserve,1);
        require(blockDistance(helperReserve->getGuardPoint(),assaultCenter)<=3,
            "Custom reserve rule changed campaign helper movement");
        game.gameType=GameType::CustomGame;
        reset(QuantBot::Difficulty::Easy);
        getQuantBotConfig().easy.attackForceMilitaryValueRatio=0;
        bot->launchGroundHunt();require(hunters()==0,"Zero percent still attacked");
        // Restore normal percentage and expose only four 100-credit troopers.
        getQuantBotConfig().easy.attackForceMilitaryValueRatio=0.25f;
        unitList.clear();for(int i=0;i<4;++i)unitList.push_back(army[i]);
        bot->launchGroundHunt();require(hunters()==1,"Small army ignored 25-percent budget");
        bot->launchGroundHunt();require(hunters()==1,"Small army leaked its reserve on repeat");
    } catch(...) {unitList=saved;throw;}
    unitList=saved;
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=custom_attack outcome=passed");
}
