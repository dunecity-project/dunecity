// Real-engine continuation of the city whose overdue Palace consumed every yard decision.
// Disk saves intentionally rebuild strategic timers: restore the overdue condition explicitly,
// rather than letting a reset timer make the unfixed planner appear to pass.
void runStrategicSiteProbe(Game& game) {
    auto require=[](bool ok,const char* why) {
        if (!ok) { SDL_Log("STRATEGIC_SITE_FAIL: %s",why); throw std::runtime_error(why); }
    };
    auto* house=game.getHouse(HOUSE_ATREIDES);
    require(house && game.isCitySimEnabled(),"Siting fixture needs the saved Atreides city");
    QuantBot* bot=nullptr;
    for (const auto& player:house->getPlayerList())
        if (auto* candidate=dynamic_cast<QuantBot*>(player.get())) bot=candidate;
    require(bot && bot->gameMode==QuantBot::GameMode::Custom,"Siting fixture lost its Custom QuantBot");
    require(game.getGameCycleCount()==MILLI2CYCLES(35*60000),"Siting fixture is not at minute 35");
    require(house->getNumItems(Structure_HeavyFactory)>0 && house->getNumItems(Structure_LightFactory)>0,
        "Siting fixture lost the Palace prerequisites");

    std::vector<BuilderBase*> yards;
    for (auto* structure:structureList)
        if (structure->getOwner()==house && structure->getItemID()==Structure_ConstructionYard)
            yards.push_back(static_cast<BuilderBase*>(structure));
    require(!yards.empty(),"Siting fixture has no yard");
    bool available=false;
    for (auto* yard:yards) {
        while (yard->getProductionQueueSize()) yard->doCancelItem(yard->getCurrentProducedItem());
        yard->upgrading=false;
        yard->updateBuildList();
        available|=yard->isAvailableToBuild(Structure_Palace);
    }
    require(available,"Siting fixture cannot build a Palace for reasons other than siting");
    bot->builderPlaceLocations.clear();
    bot->reservedStructures.clear();
    // A disk load rebuilds placement plans, so the historical map can regain a Palace site
    // before its overdue condition is restored. Make the *physical* no-site condition explicit:
    // open ground has at most two consecutive rock rows. This preserves existing buildings and
    // city lots, leaves 3x2 production sites and smaller construction possible, but no empty
    // 3x3 Palace footprint can pass Map::okayToPlaceStructure. Never fake the placement cache.
    for (int y=0;y<currentGameMap->getSizeY();y+=3)
        for (int x=0;x<currentGameMap->getSizeX();++x) {
            auto* tile=currentGameMap->getTile(x,y);
            if (!tile->hasAStructure() && !tile->hasCityZone()) tile->setType(Terrain_Sand);
        }
    for (const auto* yard:yards) {
        bot->planningBuilder=yard->getObjectID();
        bot->clearPlacementCache();
        require(bot->findPlaceLocation(Structure_Palace).isInvalid(),
            "Siting fixture unexpectedly has a Palace site; it would not reproduce starvation");
    }
    bot->planningBuilder=NONE_ID;
    bot->clearPlacementCache();
    std::vector<Uint32> yardIds;
    for (const auto* yard:yards) yardIds.push_back(yard->getObjectID());
    const Uint32 start=game.getGameCycleCount();
    bot->palaceEligibleSinceCycle=start-MILLI2CYCLES(120000);
    // Start at the real storage ceiling, not an unbounded starting-credit grant. A working
    // city must invest rather than keep its yards empty and throw away arriving tax income.
    house->startingCredits=0;
    house->storedCredits=house->getCapacity();
    house->cityCredits=0;
    auto built=[&]() {
        int total=0;
        for (int item=Structure_FirstID;item<=ItemID_LastID;++item)
            if (isStructure(item)) total+=house->getNumBuiltItems(item);
        return total;
    };
    const int before=built();
    const Uint32 minute=MILLI2CYCLES(60000);
    int cappedRun=0, longestCapped=0, idleRun=0, longestIdle=0;
    int previous=built();
    bool sawOverdue=false;
    for (Uint32 cycle=0;cycle<6*minute;++cycle) {
        game.updateGameState();
        sawOverdue|=bot->palaceEligibleSinceCycle!=std::numeric_limits<Uint32>::max()
            && game.getGameCycleCount()-bot->palaceEligibleSinceCycle>=Uint32(MILLI2CYCLES(45000));
        if ((cycle+1)%minute) continue;
        const int completed=built();
        bool busy=false;
        for (const Uint32 id:yardIds)
            if (const auto* yard=dynamic_cast<const BuilderBase*>(game.getObjectManager().getObject(id)))
                busy|=yard->getProductionQueueSize()>0 || yard->isUpgrading();
        idleRun=!busy && completed==previous ? idleRun+1 : 0;
        longestIdle=std::max(longestIdle,idleRun);
        const bool capped=house->getCapacity()>0
            && int64_t(house->getCredits())*100>=int64_t(house->getCapacity())*99;
        cappedRun=capped ? cappedRun+1 : 0;
        longestCapped=std::max(longestCapped,cappedRun);
        SDL_Log("STRATEGIC_SITE_SAMPLE: minute=%u credits=%d capacity=%d completed=%d busy=%d capped_run=%d idle_run=%d",
            (game.getGameCycleCount()-start)/minute,house->getCredits(),house->getCapacity(),
            completed-before,int(busy),cappedRun,idleRun);
        previous=completed;
    }
    require(sawOverdue,"Siting probe never exercised the overdue Palace rule");
    require(built()>before,"Unsiteable Palace starved every other completed structure");
    require(longestIdle<=2,"Unsiteable Palace kept every Construction Yard idle for over two minutes");
    require(longestCapped<=2,"Unsiteable Palace left credits at the storage cap for over two minutes");
    SDL_Log("STRATEGIC_SITE_PASS: completed=%d longest_idle=%d longest_capped=%d",built()-before,longestIdle,longestCapped);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=strategic_site outcome=passed");
}
