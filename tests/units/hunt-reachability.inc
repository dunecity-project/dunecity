// Target-search workload: the ring walk visits one perimeter per ring instead
// of a whole square, and a unit holding a distant target stops re-running the
// whole search every cycle. Both baselines are closed-form, so "before" needs
// no second binary: the square walk over rings 0..R costs
// (2R+1)(2R+2)(2R+3)/6 cell tests and the perimeter walk costs 1+4R(R+1).
namespace {
uint64_t squareWalkCells(int radius) {
    const uint64_t r = static_cast<uint64_t>(radius);
    return (2*r+1)*(2*r+2)*(2*r+3)/6;
}
uint64_t perimeterWalkCells(int radius) {
    const uint64_t r = static_cast<uint64_t>(radius);
    return 1 + 4*r*(r+1);
}
} // namespace

void runTargetSearchWorkloadProbe(const std::string& mod) {
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    // A dense, open 128x128 field: a whole-map Hunt with nothing to shoot is the
    // worst case the reported match was spending its frame budget on.
    std::ostringstream map;
    map<<"[BASIC]\nVersion=2\nTechLevel=8\n[MAP]\nSizeX=128\nSizeY=128\n";
    for(int y=0;y<128;++y) map<<std::setw(3)<<std::setfill('0')<<y<<'='<<std::string(128,'%')<<'\n';
    map<<"[Player1]\nCredits=0\nBrain=Team1\n[Player2]\nCredits=0\nBrain=Team2\n";
    auto game=std::make_unique<Game>();currentGame=game.get();
    GameInitSettings init("workload.ini",map.str(),false,settings.gameOptions);
    init.randomSeed=71;
    for(auto house:{HOUSE_ATREIDES,HOUSE_HARKONNEN}) {
        GameInitSettings::HouseInfo slot(house,house==HOUSE_ATREIDES?1:2);
        slot.addPlayerInfo({house==HOUSE_ATREIDES?"Speed tester":"Enemy","HumanPlayer"});
        init.addHouseInfo(slot);
    }
    game->initGame(init);game->initializeGameLoop();
    auto* owner=game->getHouse(HOUSE_ATREIDES);auto* enemy=game->getHouse(HOUSE_HARKONNEN);
    for(int y=0;y<128;++y)for(int x=0;x<128;++x)
        currentGameMap->getTile(x,y)->setExplored(owner->getHouseID(),game->getGameCycleCount());
    auto spawn=[&](House* house,int item,Coord point) {
        auto* unit=house->createUnit(item,true);require(unit,"Workload fixture cannot create unit");
        unit->deploy(point);unit->setVisible(owner->getTeamID(),true);unit->doSetAttackMode(STOP);return unit;
    };
    const auto* grid=game->getSpatialGrid();
    require(grid!=nullptr,"Workload fixture has no spatial grid");

    // --- One whole-map Hunt with no target at all. The grid walk and the legacy
    // tile walk each cover every ring, so the exact cell count is predictable.
    auto* hunter=spawn(owner,Unit_Tank,Coord(0,0));
    hunter->doSetAttackMode(HUNT);
    const Coord cell=grid->clampToCell(hunter->getLocation());
    const int gridRadius=std::max(std::max(cell.x,grid->getGridWidth()-1-cell.x),
                                  std::max(cell.y,grid->getGridHeight()-1-cell.y));
    const int tileRadius=std::max(std::max(hunter->getLocation().x,127-hunter->getLocation().x),
                                  std::max(hunter->getLocation().y,127-hunter->getLocation().y));
    ObjectBase::targetSearchStats={};
    require(hunter->findTarget()==nullptr,"Workload fixture found a target with no enemies on the map");
    const uint64_t walked=ObjectBase::targetSearchStats.cellsVisited;
    // Grid rings 0..gridRadius, then the legacy fallback over tile rings
    // 1..tileRadius (which starts at radius 1 and so omits the centre tile).
    const uint64_t expected=perimeterWalkCells(gridRadius)
        +perimeterWalkCells(tileRadius)-1;
    const uint64_t before=squareWalkCells(gridRadius)+squareWalkCells(tileRadius)-1;
    require(ObjectBase::targetSearchStats.searches==1,"One findTarget() was not counted once");
    require(walked==expected,"The ring walk no longer visits exactly one perimeter per ring");
    require(walked*8<before,"The perimeter walk did not cut the old square walk by at least 8x");
    SDL_Log("TARGET_SEARCH_WORKLOAD: %s whole-map Hunt cells %llu -> %llu (%.1fx fewer, grid r=%d tile r=%d)",
            mod.c_str(),(unsigned long long)before,(unsigned long long)walked,
            double(before)/double(walked),gridRadius,tileRadius);

    // --- Dense fleet stress: the per-search cost must stay proportional to the
    // rings actually needed, not to the fleet size.
    std::vector<UnitBase*> fleet;
    for(int i=0;i<64;++i) fleet.push_back(spawn(owner,Unit_Tank,Coord(20+i%8,20+i/8)));
    for(int i=0;i<64;++i) {
        auto* prey=spawn(enemy,Unit_Tank,Coord(40+i%8,40+i/8));
        prey->setVisible(owner->getTeamID(),true);
    }
    for(auto* tank:fleet) tank->doSetAttackMode(HUNT);
    ObjectBase::targetSearchStats={};
    for(auto* tank:fleet) require(tank->findTarget()!=nullptr,"A dense-fleet hunter found no target");
    const uint64_t dense=ObjectBase::targetSearchStats.cellsVisited;
    require(ObjectBase::targetSearchStats.searches==fleet.size(),"Dense fleet searches were miscounted");
    // Nothing here may approach the whole-map walk: a hit a few rings out ends
    // the search, and that was already true - what changed is the per-ring cost.
    require(dense<walked,"A dense fleet with visible prey cost more than one empty whole-map Hunt");
    SDL_Log("TARGET_SEARCH_WORKLOAD: %s 64 hunters/64 prey cells=%llu searches=%llu candidates=%llu",
            mod.c_str(),(unsigned long long)dense,
            (unsigned long long)ObjectBase::targetSearchStats.searches,
            (unsigned long long)ObjectBase::targetSearchStats.candidatesTested);

    // --- Cadence: a unit that keeps a target the search cannot see used to
    // re-run the entire search on every single cycle, because the refresh only
    // set a timer when it actually found something. The retention test and the
    // search disagree by construction here - the target is inside weapon range
    // of the guard point and outside it from where the unit now stands - which
    // is the same disagreement that makes a whole-map Hunt do this 10 times a
    // frame per unit in a 600-unit match.
    for(auto* tank:fleet) tank->doSetAttackMode(STOP);
    for(const auto* other:unitList)
        if(other->getOwner()==enemy) const_cast<UnitBase*>(other)->setVisible(VIS_ALL,false);
    auto* chaser=fleet.front();
    const int reach=chaser->getWeaponRange();
    require(reach>=2,"The cadence fixture needs a unit with a real weapon range");
    const Coord stand=chaser->getLocation();
    auto* quarry=spawn(enemy,Unit_Tank,Coord(stand.x,stand.y+2*reach));
    quarry->setVisible(owner->getTeamID(),true);
    chaser->doSetAttackMode(GUARD);
    chaser->setGuardPoint(Coord(stand.x,stand.y+reach));
    chaser->doAttackObject(quarry,false);
    require(chaser->getTarget()==quarry,"The cadence fixture lost its target");
    require(!chaser->isInWeaponRange(quarry),"The cadence fixture's target is already in weapon range");
    require(chaser->isInAttackRange(quarry),"The cadence fixture does not retain its target");
    require(chaser->findTarget()==nullptr,"The cadence fixture's search can see the retained target");
    ObjectBase::targetSearchStats={};
    const int cadenceCycles=60;
    for(int i=0;i<cadenceCycles;++i) {
        chaser->targeting();
        if(chaser->findTargetTimer>0) --chaser->findTargetTimer;
    }
    const uint64_t refreshes=ObjectBase::targetSearchStats.searches;
    require(chaser->getTarget()==quarry,"The retained target was dropped by the refresh");
    require(refreshes>=1,"The refresh never searched at all");
    require(static_cast<int>(refreshes)*4<=cadenceCycles,
            "A retained target still re-runs the whole target search every cycle");
    SDL_Log("TARGET_SEARCH_WORKLOAD: %s retained target %llu searches over %d cycles (was %d)",
            mod.c_str(),(unsigned long long)refreshes,cadenceCycles,cadenceCycles);

    // --- Autonomous ornithopter searches never return an enemy worker, while an
    // explicit order from this human-player house keeps its authority.
    auto* bird=spawn(owner,Unit_Ornithopter,Coord(60,60));
    auto* worker=spawn(enemy,Unit_Harvester,Coord(62,60));
    worker->setVisible(owner->getTeamID(),true);
    // The rebel worker is Tornie-exclusive content, so it only exists as a real
    // unit in a mod that has it. Both ids go through the same isHarvesterLikeUnit
    // predicate, and the one that can be built here is exercised as a live unit.
    UnitBase* rebel=nullptr;
    if(!isTornieExclusiveItem(Unit_RebelHarvester) || ModManager::instance().isTornieContentActive()) {
        rebel=spawn(enemy,Unit_RebelHarvester,Coord(58,60));
        rebel->setVisible(owner->getTeamID(),true);
    }
    require(isHarvesterLikeUnit(Unit_Harvester) && isHarvesterLikeUnit(Unit_RebelHarvester),
            "The worker veto predicate no longer covers both harvester types");
    require(bird->canAttack(worker) && (!rebel || bird->canAttack(rebel)),
            "The worker fixture is not attackable at all, so the veto would test nothing");
    for(auto mode:{GUARD,AREAGUARD,AMBUSH,HUNT}) {
        bird->doSetAttackMode(mode);
        const auto* found=bird->findTarget();
        require(found!=worker && (!rebel || found!=rebel),
                "An autonomous ornithopter search returned an enemy harvester");
    }
    // A human player's own explicit order still flies and fires.
    bird->doSetAttackMode(STOP);
    bird->doAttackObject(worker,true);
    require(bird->getTarget()==worker,"An explicit human order on a harvester was refused");
    const FixPoint workerFull=worker->getMaxHealth();
    bool hitWorker=false;
    for(int i=0;i<4000 && !hitWorker;++i) {
        game->processObjects();++game->gameCycleCount;
        if(worker->getHealth()<workerFull) hitWorker=true;
    }
    require(bird->getTarget()==worker || hitWorker,
            "A human-ordered strike on a harvester lost its target without ever firing");
    require(hitWorker,"A human-ordered ornithopter never damaged the harvester it was sent at");
    SDL_Log("TARGET_SEARCH_WORKLOAD_PASS: %s perimeter walk, refresh cadence, worker veto and manual authority",
            mod.c_str());
    game.reset();currentGame=nullptr;
}

// Real Hunt selection and path-budget checks across a map-wide mountain ridge.
void runHuntReachabilityProbe(const std::string& mod) {
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    std::ostringstream map;
    map<<"[BASIC]\nVersion=2\nTechLevel=8\n[MAP]\nSizeX=64\nSizeY=32\n";
    for(int y=0;y<32;++y) {
        std::string row(64,'%');
        for(int x=24;x<=37;++x)row[x]='@';
        map<<std::setw(3)<<std::setfill('0')<<y<<'='<<row<<'\n';
    }
    map<<"[Player1]\nCredits=0\nBrain=Team1\n[Player2]\nCredits=0\nBrain=Team2\n";
    auto game=std::make_unique<Game>();currentGame=game.get();
    GameInitSettings init("hunt.ini",map.str(),false,settings.gameOptions);
    init.randomSeed=67;
    for(auto house:{HOUSE_ATREIDES,HOUSE_HARKONNEN}) {
        GameInitSettings::HouseInfo slot(house,house==HOUSE_ATREIDES?1:2);
        slot.addPlayerInfo({house==HOUSE_ATREIDES?"Speed tester":"Enemy","HumanPlayer"});
        init.addHouseInfo(slot);
    }
    game->initGame(init);game->initializeGameLoop();
    auto* owner=game->getHouse(HOUSE_ATREIDES);auto* enemy=game->getHouse(HOUSE_HARKONNEN);
    for(int y=0;y<32;++y)for(int x=0;x<64;++x)
        currentGameMap->getTile(x,y)->setExplored(owner->getHouseID(),game->getGameCycleCount());
    auto spawn=[&](House* house,int item,Coord point) {
        auto* unit=house->createUnit(item,true);require(unit,"Hunt fixture cannot create unit");
        unit->deploy(point);unit->setVisible(owner->getTeamID(),true);unit->doSetAttackMode(STOP);return unit;
    };
    auto* hunter=spawn(owner,Unit_Tank,Coord(21,16));
    auto* unreachable=spawn(enemy,Unit_Tank,Coord(39,16));
    auto* reachable=spawn(enemy,Unit_Tank,Coord(2,16));
    hunter->doSetAttackMode(HUNT);
    require(!currentGameMap->terrainAttackReachable(*hunter,*unreachable),"Mountain ridge accepted a disconnected target");
    require(hunter->findTarget()==reachable,"Hunt preferred an unreachable target over a reachable one");
    const unsigned builds=currentGameMap->terrainConnectivityBuilds;
    for(int i=0;i<1000;++i) {
        currentGameMap->incrementPathingRevision(); // Traffic must not invalidate terrain labels.
        require(!currentGameMap->terrainAttackReachable(*hunter,*unreachable),"Cached reachability changed with traffic");
    }
    require(currentGameMap->terrainConnectivityBuilds==builds,"Movement rebuilt the terrain connectivity cache");
    auto grid=std::move(game->spatialGrid);
    require(hunter->findTarget()==reachable,"Legacy Hunt ignored terrain reachability");
    game->spatialGrid=std::move(grid);
    // An already acquired target crossing the ridge is released without an A* search.
    hunter->doAttackObject(unreachable,false);
    hunter->setDestination(unreachable->getLocation());
    const auto path= hunter->resolvePendingPathRequest();
    require(!hunter->getTarget() && path.nodesExpanded==0,"Unreachable hunt retained its target or consumed A* nodes");
    // No reachable enemies: stop acquiring the same impossible destination.
    reachable->setVisible(owner->getTeamID(),false);
    require(!hunter->findTarget(),"Hunt reacquired an unreachable enemy");
    hunter->pendingTargetRequest=UnitBase::TargetRequestKind::Acquire;
    hunter->resolvePendingTargetRequest();
    require(hunter->getAttackMode()==GUARD,"Unreachable Hunt kept repeating a whole-map target search");
    // Infantry climb the same ridge; nearby enemies across a narrow ridge can be shot.
    auto* infantry=spawn(owner,Unit_Soldier,Coord(20,17));
    require(currentGameMap->terrainAttackReachable(*infantry,*unreachable),"Vehicle reachability blocked infantry");
    auto* near=spawn(enemy,Unit_Soldier,Coord(25,16));
    require(currentGameMap->terrainAttackReachable(*hunter,*near),"Ridge filter rejected a target within weapon range");
    near->cancelDeployment();
    // Open a remote gap. The same target becomes reachable, then unreachable when closed.
    for(int x=24;x<=37;++x)currentGameMap->getTile(x,1)->setType(Terrain_Rock);
    require(currentGameMap->terrainAttackReachable(*hunter,*unreachable),"New mountain gap did not invalidate connectivity");
    require(currentGameMap->terrainConnectivityBuilds==builds+1,"A batch of terrain edits caused more than one rebuild");
    hunter->doSetAttackMode(HUNT);
    require(hunter->findTarget()==unreachable,"Hunt failed to use a reachable detour around the ridge");
    for(int x=24;x<=37;++x)currentGameMap->getTile(x,1)->setType(Terrain_Mountain);
    require(!currentGameMap->terrainAttackReachable(*hunter,*unreachable),"Closing a gap left stale connectivity");
    // A target can die between queuing and processing a deferred path request.
    hunter->doSetAttackMode(HUNT);
    hunter->target.pointTo(Uint32(NONE_ID-1));
    hunter->setDestination(Coord(39,16));
    const auto stalePath=hunter->resolvePendingPathRequest();
    require(!hunter->getTarget() && stalePath.nodesExpanded==0,"Expired Hunt target consumed a path search");
    // Explicit attack orders retain their transport/manual intent.
    hunter->doAttackObject(unreachable,true);hunter->engageTarget();
    require(hunter->getTarget()==unreachable,"Terrain filtering replaced an explicit attack order");
    SDL_Log("HUNT_REACHABILITY_PASS: %s reachable alternatives, no-route rejection, infantry/range, cached traffic and terrain invalidation",mod.c_str());
    game.reset();currentGame=nullptr;
}
