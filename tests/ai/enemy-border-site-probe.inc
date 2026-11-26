// QuantBot may build right up to enemy buildings (protocol 141; Stefan, 2026-11-27: "also allow ai
// to build buildings right up to enemy buildings"). TacticalSafetyPolicy::enemyBorderTileScore and
// ordinarySiteUnderFireAllowed have the rules. Each check is also run with the spacing before 141
// (QuantBot::oldEnemySpacing, as BuildUpToEnemy=false), which must fail it: the -10 a tile for an enemy building on the
// border, and any enemy fire refusing a site outright.
void runEnemyBorderSiteProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok){SDL_Log("ENEMY_BORDER_FAIL: %s",why);throw std::runtime_error(why);}};
    require(game.isCitySimEnabled(),"Run the enemy-border fixture with the Dune City mod");
    QuantBot* bot=nullptr;BuilderBase* yard=nullptr;
    for(auto* building:structureList) if(building->getItemID()==Structure_ConstructionYard && !yard)
        for(const auto& player:building->getOwner()->getPlayerList())
            if(auto* candidate=dynamic_cast<QuantBot*>(player.get())) {
                bot=candidate;yard=dynamic_cast<BuilderBase*>(building);break;
            }
    require(bot && yard,"Missing QuantBot construction yard");
    require(!bot->usesOldEnemySpacing(),"BuildUpToEnemy=false restores the old spacing: this probe fails under it");
    House* enemy=nullptr;
    for(int h=0;h<NUM_HOUSES && !enemy;++h) {
        House* house=game.getHouse(h);
        if(house && house!=bot->getHouse() && house->getTeamID()!=bot->getHouse()->getTeamID()) enemy=house;
    }
    require(enemy,"Fixture has no enemy house");
    const int width=currentGameMap->getSizeX(),height=currentGameMap->getSizeY();
    const Uint32 item=Structure_Stadium;
    const Coord span=getStructureSize(item);

    // A fixed tactical picture for this cycle: refreshTacticalDanger() does not run again.
    bot->refreshTacticalDanger();
    bot->dangerUpdated=game.getGameCycleCount();
    auto threaten=[&](int level){
        bot->tacticalDanger.assign(width*height,level);
        bot->factoryEnemyClearance.assign(bot->tacticalDanger.size(),level>0 ? 0 : 12);
        bot->factoryEnemyClearanceStale=false;
        bot->lossDanger.assign(bot->tacticalDanger.size(),0);
        bot->recentStructureLosses.clear();
        bot->clearPlacementCache();
    };
    auto search=[&](bool old,int* score){
        bot->oldEnemySpacing=old;
        bot->clearPlacementCache();
        const Coord site=bot->findPlaceLocation(item);
        bot->oldEnemySpacing=false;
        if(score) {
            const std::string details=bot->placementScoreDetails[item].json();
            const auto at=details.find("\"score\":");
            *score=at==std::string::npos ? std::numeric_limits<int>::min() : std::atoi(details.c_str()+at+8);
        }
        return site;
    };
    bot->planningBuilder=yard->getObjectID();

    // 1. An enemy building on the border costs the site nothing. Put enemy Wind Traps on the
    //    diagonals of the best site, each touching only a corner of its border ring: the side,
    //    road and access rules see no change, only the -10 a tile before 141 would.
    threaten(0);
    int before=0,beforeOld=0;
    const Coord site=search(false,&before);
    require(site.isValid(),"Fixture has no safe Stadium site");
    require(search(true,&beforeOld)==site && beforeOld==before,"Spacings differ before any enemy building");
    const Coord trap=getStructureSize(Structure_WindTrap);
    const Coord corners[4]={Coord(site.x-trap.x,site.y-trap.y),Coord(site.x+span.x,site.y-trap.y),
                            Coord(site.x-trap.x,site.y+span.y),Coord(site.x+span.x,site.y+span.y)};
    int planted=0;
    for(const Coord corner:corners) {
        if(!currentGameMap->okayToPlaceStructure(corner.x,corner.y,trap.x,trap.y,false,nullptr,false,Structure_WindTrap))
            continue;
        bool road=false;   // placing on a road clears it, which would change the site's road terms
        for(int dy=0;dy<trap.y;++dy) for(int dx=0;dx<trap.x;++dx) road|=currentGameMap->getTile(corner.x+dx,corner.y+dy)->isRoad();
        if(road) continue;
        StructureBase* placedTrap=enemy->placeStructure(NONE_ID,Structure_WindTrap,corner.x,corner.y,false,true);
        if(!placedTrap) continue;
        // A trap that makes the site itself illegal (its corner was the detour for a road the
        // Stadium covers, or the site's ground exit) tests nothing about the score: take it away.
        bot->clearPlacementCache();
        if(bot->findPlaceLocation(item)!=site) {
            SDL_Log("ENEMY_BORDER_SKIP: corner=(%d,%d) moved=(%d,%d) access=%d details=%s",corner.x,corner.y,
                bot->placementCache[item].x,bot->placementCache[item].y,
                int(bot->preservesGroundAccess(item,site)),bot->placementScoreDetails[item].json().c_str());
            placedTrap->destroy();
            continue;
        }
        ++planted;
    }
    require(planted>0,"No enemy Wind Trap on a corner of the Stadium site leaves the site legal");
    int after=0,afterOld=0;
    const Coord again=search(false,&after);
    require(again==site,"An enemy building next door moved the Stadium off its best site");
    require(after==before,"An enemy building next door lowered the site's score");
    const Coord oldSite=search(true,&afterOld);
    require(oldSite!=site || afterOld==before+planted*TacticalSafetyPolicy::kOldEnemyBorderTileScore,
        "The old spacing did not charge the enemy border tiles");
    require(oldSite!=site || afterOld<after,"The old spacing kept the score: the check cannot tell them apart");
    SDL_Log("ENEMY_BORDER_SCORE: site=(%d,%d) enemy_traps=%d score=%d old_site=(%d,%d) old_score=%d",
        site.x,site.y,planted,after,oldSite.x,oldSite.y,afterOld);

    // 2. Light fire (one enemy weapon over every site) no longer refuses a site; heavy fire does.
    threaten(TacticalSafetyPolicy::kLightFire);
    const Coord light=search(false,nullptr);
    require(light.isValid(),"A site under light fire was refused");
    require(search(true,nullptr).isInvalid(),"The old spacing took a site under fire");
    // ... but a safe site comes first, whatever its score. Heavy fire on the light-fire pick gives
    // the next site; clearing only that one must make it the answer over the better-scored pick.
    auto setDanger=[&](Coord at,int level){
        for(int dy=0;dy<span.y;++dy) for(int dx=0;dx<span.x;++dx) bot->tacticalDanger[(at.y+dy)*width+at.x+dx]=level;
    };
    setDanger(light,3*TacticalSafetyPolicy::kLightFire);
    const Coord next=search(false,nullptr);
    require(next.isValid() && next!=light,"Fixture has a single Stadium site");
    setDanger(light,TacticalSafetyPolicy::kLightFire);
    require(search(false,nullptr)==light,"The light-fire pick changed");
    setDanger(next,0);
    const Coord safe=search(false,nullptr);
    require(safe==next,"A site under light fire outranked a safe site");
    threaten(2*TacticalSafetyPolicy::kLightFire);
    require(search(false,nullptr).isInvalid(),"A site under heavy fire was taken without the held-building wait");
    bot->planningBuilder=NONE_ID;
    threaten(0);
    SDL_Log("ENEMY_BORDER_PASS: site=(%d,%d) light_fire_site=(%d,%d) safe_first=(%d,%d)",
        site.x,site.y,light.x,light.y,safe.x,safe.y);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=enemy_border_site outcome=passed");
}
