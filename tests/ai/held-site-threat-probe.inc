// A finished building that no safe site can take is placed on the least threatened legal site
// once its yard has held it for QuantBot::kHeldThreatGrace, instead of being held for good.
// Stefan's acantplace.dls (1.0.878): enemy Rocket Turrets covered every Atreides site, the
// finished Stadium waited 26 game minutes and the yard built nothing else meanwhile.
void runHeldSiteThreatProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok){SDL_Log("HELD_SITE_FAIL: %s",why);throw std::runtime_error(why);}};
    require(game.isCitySimEnabled(),"Run the held-site fixture with the Dune City mod");
    QuantBot* bot=nullptr;BuilderBase* yard=nullptr;
    for(auto* building:structureList) if(building->getItemID()==Structure_ConstructionYard && !yard)
        for(const auto& player:building->getOwner()->getPlayerList())
            if(auto* candidate=dynamic_cast<QuantBot*>(player.get())) {
                bot=candidate;yard=dynamic_cast<BuilderBase*>(building);break;
            }
    require(bot && yard,"Missing QuantBot construction yard");
    auto* house=const_cast<House*>(bot->getHouse());
    const int width=currentGameMap->getSizeX(),height=currentGameMap->getSizeY();
    const Uint32 item=Structure_Stadium;
    const Coord span=getStructureSize(item);
    while(yard->getProductionQueueSize())yard->doCancelItem(yard->getCurrentProducedItem());
    yard->upgrading=false;
    yard->buildList.push_back(BuildItem(item,3000));
    house->addCredits(10000);
    yard->doProduceItem(item,false);
    require(yard->getCurrentProducedItem()==item,"Fixture Stadium did not enter production");
    yard->productionProgress=yard->getBuildItem(item)->price;yard->setWaitingToPlace();
    require(yard->isWaitingToPlace(),"Fixture Stadium is not finished");
    const int stadiumsBefore=house->getNumItems(item);

    // Enemy fire over every tile of the map: no safe site exists. Keep the grid for this cycle.
    bot->refreshTacticalDanger();
    bot->dangerUpdated=game.getGameCycleCount();
    auto threaten=[&](int level){
        bot->tacticalDanger.assign(width*height,level);
        bot->factoryEnemyClearance.assign(bot->tacticalDanger.size(),0);
        bot->lossDanger.assign(bot->tacticalDanger.size(),0);
        bot->clearPlacementCache();
    };
    threaten(300);
    bot->planningBuilder=yard->getObjectID();
    require(bot->findPlaceLocation(item).isInvalid(),"Fixture has a safe Stadium site");
    bot->placementToleratesThreat=true;
    const Coord worse=bot->findPlaceLocation(item);
    bot->placementToleratesThreat=false;
    require(worse.isValid(),"Fixture has no legal Stadium site at all");
    require(bot->placementCache.find(item)==bot->placementCache.end()
        || bot->placementCache[item].isInvalid(),"A tolerant search wrote the placement cache");
    // Make that site the most threatened: the tolerant search must pick a less threatened one.
    for(int dy=0;dy<span.y;++dy)for(int dx=0;dx<span.x;++dx)
        bot->tacticalDanger[(worse.y+dy)*width+worse.x+dx]=500;
    bot->clearPlacementCache();
    bot->planningBuilder=NONE_ID;

    // Within the grace period the yard keeps holding it and starts the clock.
    bot->build(0);
    require(yard->isWaitingToPlace() && house->getNumItems(item)==stadiumsBefore,
        "Stadium placed under enemy fire before the grace period");
    const auto held=bot->heldWithoutSiteSince.find(yard->getObjectID());
    require(held!=bot->heldWithoutSiteSince.end(),"Held Stadium did not start the wait");
    SDL_Log("HELD_SITE_SETUP: yard=%u since=%u worse=(%d,%d)",yard->getObjectID(),held->second,worse.x,worse.y);

    // After the grace period it goes down on the least threatened legal site.
    held->second=game.getGameCycleCount()-QuantBot::kHeldThreatGrace;
    bot->build(0);
    require(!yard->isWaitingToPlace(),"Stadium still held after the grace period");
    require(house->getNumItems(item)==stadiumsBefore+1,"Stadium was not placed");
    require(!bot->heldWithoutSiteSince.count(yard->getObjectID()),"Placed Stadium left its wait");
    Coord placed=Coord::Invalid();
    for(auto* building:structureList)
        if(building->getOwner()==house && building->getItemID()==item) placed=building->getLocation();
    require(placed.isValid() && placed!=worse,"Stadium went to the most threatened site");
    SDL_Log("HELD_SITE_PASS: placed=(%d,%d) threat=%d",placed.x,placed.y,bot->dangerAt(placed,span));
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=held_site_threat outcome=passed");
}
