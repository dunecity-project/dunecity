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

    // 1. An enemy building on the border costs the site nothing. Heavy fire everywhere but one
    //    footprint leaves the search a single candidate, so the score compared is that site's.
    //    Enemy Wind Traps go on the site's diagonals, each touching only a corner of its border
    //    ring: sides, roads and access see no change, only the -10 a tile before 141 would.
    //    The base is crowded, so candidates are tried outward from the yard until one has an
    //    open ring that a corner trap leaves legal.
    const Coord trap=getStructureSize(Structure_WindTrap);
    auto mask=[&](Coord open){
        threaten(2*TacticalSafetyPolicy::kLightFire);
        for(int dy=0;dy<span.y;++dy) for(int dx=0;dx<span.x;++dx) bot->tacticalDanger[(open.y+dy)*width+open.x+dx]=0;
    };
    auto freeCorner=[&](Coord corner){
        if(!currentGameMap->okayToPlaceStructure(corner.x,corner.y,trap.x,trap.y,false,nullptr,false,Structure_WindTrap))
            return false;
        for(int dy=0;dy<trap.y;++dy) for(int dx=0;dx<trap.x;++dx)   // placing clears a road
            if(currentGameMap->getTile(corner.x+dx,corner.y+dy)->isRoad()) return false;
        return true;
    };
    const Coord yardAt=yard->getLocation();
    Coord site=Coord::Invalid();
    int before=0,after=0,afterOld=0,planted=0,tried=0;
    std::string beforeDetails;
    for(int radius=0;radius<=24 && site.isInvalid();++radius)
        for(int y=yardAt.y-radius;y<=yardAt.y+radius && site.isInvalid();++y)
            for(int x=yardAt.x-radius;x<=yardAt.x+radius && site.isInvalid();++x) {
                if(std::max(std::abs(x-yardAt.x),std::abs(y-yardAt.y))!=radius) continue;
                if(x<0 || y<0 || x+span.x>width || y+span.y>height) continue;
                if(!bot->placeableAmongOwnUnits(x,y,span.x,span.y,item)) continue;
                const Coord candidate(x,y);
                const Coord corners[4]={Coord(x-trap.x,y-trap.y),Coord(x+span.x,y-trap.y),
                                        Coord(x-trap.x,y+span.y),Coord(x+span.x,y+span.y)};
                bool anyCorner=false;
                for(const Coord corner:corners) anyCorner|=freeCorner(corner);
                if(!anyCorner) continue;
                mask(candidate);
                int score=0,oldScore=0;
                if(search(false,&score)!=candidate) continue;   // not a legal site
                const std::string details=bot->placementScoreDetails[item].json();
                ++tried;
                require(search(true,&oldScore)==candidate && oldScore==score,"Spacings differ before any enemy building");
                int traps=0;
                for(const Coord corner:corners) {
                    if(!freeCorner(corner)) continue;
                    StructureBase* placedTrap=enemy->placeStructure(NONE_ID,Structure_WindTrap,corner.x,corner.y,false,true);
                    if(!placedTrap) continue;
                    if(search(false,nullptr)!=candidate) {   // took the last exit: remove it as sold (no survivors)
                        placedTrap->selling_=true; placedTrap->destroy(); continue;
                    }
                    ++traps;
                }
                if(!traps) continue;
                site=candidate; before=score; planted=traps; beforeDetails=details;
            }
    require(site.isValid(),"No open Stadium site near the yard takes an enemy Wind Trap on a corner");
    const Coord again=search(false,&after);
    require(again==site,"An enemy building next door made the Stadium site illegal");
    if(after!=before) SDL_Log("ENEMY_BORDER_DETAILS: site=(%d,%d) traps=%d before=%s after=%s",site.x,site.y,planted,
        beforeDetails.c_str(),bot->placementScoreDetails[item].json().c_str());
    // Same map, same single candidate: the spacings differ by exactly the enemy border charge, which
    // the old spacing makes (-10 a tile) and the new one does not.
    const Coord oldSite=search(true,&afterOld);
    require(oldSite==site,"The old spacing refused the only site");
    require(after-afterOld==-planted*TacticalSafetyPolicy::kOldEnemyBorderTileScore,
        "The spacings do not differ by the enemy border charge alone: the new one still charges enemy buildings");
    SDL_Log("ENEMY_BORDER_SCORE: site=(%d,%d) tried=%d enemy_traps=%d score_before=%d score=%d old_score=%d",
        site.x,site.y,tried,planted,before,after,afterOld);

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
