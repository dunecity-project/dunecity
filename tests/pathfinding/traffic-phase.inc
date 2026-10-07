// Included by budget-probe.inc only.
//
// Unit traffic versus long-lived geometry, against the real engine. Three
// properties that a crowded map depends on:
//
//   * A friendly unit parking on a future waypoint is traffic, not a geometry
//     change: the route is kept, and the step onto that tile is still refused.
//   * A real terrain change is a geometry change: it still invalidates the
//     cached route, so occupancy is not simply ignored once the world moves.
//   * A goal this unit cannot enter is approached from an adjacent tile for
//     bounded work, instead of being proved unreachable tile by tile, and the
//     delivered route never ends inside the blocked goal.

// A Chebyshev step: the eight directions A* can actually move in.
bool adjacentTiles(const Coord& a, const Coord& b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)) == 1;
}

// Deployment-style relocation: setLocation assigns the new tile but does not
// remove the old occupancy. Explicitly release it so this fixture never leaves
// ghost blockers behind.
void relocateTrafficUnit(UnitBase* unit,const Coord& at) {
    currentGameMap->removeObjectFromMap(unit->getObjectID());
    unit->setLocation(at);
}

Coord trafficPocket() {
    for(int y=1; y+12<currentGameMap->getSizeY(); ++y) {
        for(int x=1; x+20<currentGameMap->getSizeX(); ++x) {
            bool free=true;
            for(int dy=0; dy<12 && free; ++dy) for(int dx=0; dx<20; ++dx) {
                const Tile* tile=currentGameMap->getTile(x+dx,y+dy);
                if(tile->hasAGroundObject() || tile->isMountain()) { free=false; break; }
            }
            if(free) return Coord(x,y);
        }
    }
    throw std::runtime_error("Fixture needs an open traffic-test pocket");
}

void checkTrafficVersusGeometry() {
    auto game=loadFixture();
    const Coord pocket=trafficPocket();
    auto* subject=pLocalHouse->placeUnit(Unit_Tank,pocket.x+1,pocket.y+3,true);
    auto* occupant=pLocalHouse->placeUnit(Unit_Trike,pocket.x+1,pocket.y+8,true);
    require(subject && occupant,"Production vehicles created on separate free tiles");
    const Coord origin=subject->getLocation(), occupantOrigin=occupant->getLocation();
    const Coord goal(pocket.x+18,pocket.y+3);
    auto deliver=[&] {
        resetScheduler(*game);
        subject->clearPath();
        pinOrder(subject,goal);
        subject->setGuardPoint(goal);
        subject->setForced(true);
        subject->enqueuePathRequest();
        game->negotiatedBudget=Game::kMaxBudget;
        game->carryOverTokens=0;
        // Exercise the production scheduler with a stable world. Full simulation
        // would let unrelated combat/transport rewrite the captured unit's order.
        for(int i=0; i<8 && subject->hasPendingPathRequest(); ++i) game->processPathRequests();
        requireLifecycle(*game,"Delivery preserves request ownership");
        require(subject->getPlannedPath().size()>=6,"Scheduler delivers a multi-tile route");
        require(subject->isCachedPathStillValid(),"Delivered route validates");
    };
    deliver();
    const auto route=subject->getPlannedPath();
    auto future=route.begin(); std::advance(future,2);
    const Coord waypoint=*future;
    Tile* unrelated=currentGameMap->getTile(pocket.x+1,pocket.y+10);
    unrelated->setType(Terrain_Mountain);
    require(subject->isCachedPathStillValid(),"Unrelated geometry validates the long route once");
    const auto revision=currentGameMap->getPathingRevision();
    relocateTrafficUnit(occupant,waypoint);
    require(currentGameMap->getPathingRevision()==revision,"Unit traffic leaves geometry revision stable");
    require(!subject->canPass(waypoint.x,waypoint.y),"Parked vehicle really blocks future waypoint");
    require(subject->isCachedPathStillValid() && subject->getPlannedPath()==route,
        "Future traffic retains the complete route");
    relocateTrafficUnit(occupant,occupantOrigin);
    require(currentGameMap->getPathingRevision()==revision,"Leaving a tile also preserves revision");

    auto* wall=pLocalHouse->placeStructure(NONE_ID,Structure_Wall,waypoint.x,waypoint.y,true);
    require(wall!=nullptr,"Production wall placed on the route");
    require(currentGameMap->getPathingRevision()!=revision,"Structure placement invalidates geometry");
    require(!subject->isCachedPathStillValid(),"Wall invalidates the blocked cached prefix");
    const auto placedRevision=currentGameMap->getPathingRevision();
    wall->destroy();
    require(currentGameMap->getPathingRevision()!=placedRevision,"Structure removal invalidates geometry");
    deliver();
    const Coord terrainStep=subject->getPlannedPath().front();
    Tile* changed=currentGameMap->getTile(terrainStep);
    const int previous=changed->getType();
    changed->setType(Terrain_Mountain);
    require(!subject->isCachedPathStillValid(),"Mountain on route invalidates the cached prefix");
    changed->setType(previous);
    deliver();
    const Coord immediate=subject->getPlannedPath().front();
    relocateTrafficUnit(occupant,immediate);
    require(!subject->canPass(immediate.x,immediate.y),"Immediate step is actually blocked");
    // Drive navigation until it attempts that step. Keep the blocker stationary
    // and avoid unrelated combat; this is the real step-commit transition.
    for(int i=0; i<60 && !subject->getPlannedPath().empty(); ++i) {
        subject->navigate();
        ++game->gameCycleCount;
    }
    require(subject->getPlannedPath().empty(),"Navigation actually attempted and rejected the blocked step");
    require(!subject->isMoving() && subject->getLocation()==origin,"No blocked movement was committed");
    require(currentGameMap->getTile(immediate)->getGroundObject()==occupant,
        "Blocked tile retains its sole vehicle");
    SDL_Log("BUDGET_PROBE_TRAFFIC: route=%zu waypoint=%d,%d collision_rejected=1 structures=1 terrain=1",
        route.size(),waypoint.x,waypoint.y);
    game.reset(); currentGame=nullptr; pLocalPlayer=nullptr;
}

// ---------------------------------------------------------------------------
// A goal that cannot be entered costs an adjacent approach, not a proof.
// ---------------------------------------------------------------------------
void checkOccupiedGoalApproach() {
    auto game = loadFixture();

    const Coord pocket=trafficPocket();
    UnitBase* subject=pLocalHouse->placeUnit(Unit_Tank,pocket.x+1,pocket.y+3,true);
    require(subject!=nullptr,"Production vehicle available for goal checks");
    const Coord goal(pocket.x+18,pocket.y+3);
    const Coord from=subject->getLocation();

    // ---- a free goal is still reached exactly ----
    std::list<Coord> freeRoute;
    int freeNodes = 0;
    {
        AStarSearch search(currentGameMap, subject, from, goal);
        freeRoute = search.getFoundPath();
        freeNodes = search.getNodesChecked();
    }
    require(!freeRoute.empty(), "A free goal still produces a route");
    require(freeRoute.back() == goal, "A free goal is still reached exactly");

    // ---- now hold the goal with a real unit ----
    UnitBase* occupant=pLocalHouse->placeUnit(Unit_Trike,pocket.x+1,pocket.y+8,true);
    require(occupant!=nullptr,"Production vehicle available to hold goal");
    const Coord occupantOrigin = occupant->getLocation();
    relocateTrafficUnit(occupant,goal);
    require(!subject->canPass(goal.x, goal.y), "The held goal is not passable for the subject");

    std::list<Coord> heldRoute;
    int heldNodes = 0;
    {
        AStarSearch search(currentGameMap, subject, from, goal);
        heldRoute = search.getFoundPath();
        heldNodes = search.getNodesChecked();
    }
    require(!heldRoute.empty(), "An occupied goal still produces an approach route");
    require(heldRoute.back() != goal, "An approach route never ends inside the occupied goal");
    require(adjacentTiles(heldRoute.back(), goal), "The approach route ends next to the goal");
    for(const Coord& step : heldRoute) {
        require(step != goal, "No route tile is the blocked goal");
        require(subject->canPass(step.x, step.y), "Every delivered route tile is passable for this unit");
    }
    // The goal is the only passability change in this static fixture. Reaching
    // its neighbour must cost no more than reaching the previously free goal.
    require(heldNodes < 16384, "Approaching an occupied goal stays well inside the expansion cap");
    require(heldNodes <= freeNodes, "Approaching an occupied goal costs no more than reaching the free goal");

    // ---- sliced and whole agree on the approach ----
    for(const size_t quota : {size_t(1), size_t(7), size_t(251)}) {
        AStarSearch sliced(currentGameMap, subject, from, goal, AStarSearch::Resumable{});
        size_t guard = 0;
        while(sliced.step(currentGameMap, subject, quota, quota + 4096) == AStarSearch::Status::Running) {
            require(++guard < 400000, "The sliced approach search terminates");
        }
        require(sliced.getFoundPath() == heldRoute, "A sliced search returns the same approach route");
        require(sliced.getNodesChecked() == heldNodes, "A sliced search closes the same nodes");
    }

    relocateTrafficUnit(occupant,occupantOrigin);
    SDL_Log("BUDGET_PROBE_APPROACH: goal=%d,%d free_nodes=%d free_len=%zu held_nodes=%d held_len=%zu end=%d,%d",
        goal.x, goal.y, freeNodes, freeRoute.size(), heldNodes, heldRoute.size(),
        heldRoute.back().x, heldRoute.back().y);
    game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
}

// ---------------------------------------------------------------------------
// A harvester waiting for the scheduler is not a harvester with a blocked road.
// ---------------------------------------------------------------------------
template<class HarvesterType>
void checkPendingCounter(Game& game,HarvesterType* unit,StructureBase* dropoff,Coord beside) {
    relocateTrafficUnit(unit,beside);
    unit->setTarget(dropoff);
    unit->returningToRefinery=true;
    unit->setDestination(dropoff->getClosestPoint(beside));
    unit->setAwaitingPickup(false);
    unit->moving=false;
    unit->justStoppedMoving=false;
    unit->clearPath();
    unit->returnPathFailCounter=0;
    require(blockDistance(beside,unit->getDestination())<MIN_CARRYALL_LIFT_DISTANCE,
        "Counter fixture excludes ordinary distance-based lift requests");
    unit->enqueuePathRequest();
    for(int i=0; i<6; ++i) {
        require(unit->hasPendingPathRequest(),"A real path request remains pending");
        unit->checkPos();
        require(unit->returnPathFailCounter==0,"Pending path is never counted as a blocked route");
    }
    requireLifecycle(game,"Harvester checks retain path request ownership");
    resetScheduler(game);
    unit->clearPath();
    unit->recalculatePathTimer=400;
    unit->checkPos();
    require(unit->returnPathFailCounter>0,"Actual no-route state still counts a blocked-path failure");
    SDL_Log("BUDGET_PROBE_HARVESTER_GUARD: item=%d pending_checks=6 fallback_counted=1",unit->getItemID());
}

void checkHarvesterPendingGuard() {
    auto game=loadFixture();
    const Coord pocket=trafficPocket();
    auto* refinery=pLocalHouse->placeStructure(NONE_ID,Structure_Refinery,pocket.x+8,pocket.y+2,true);
    auto* yard=pLocalHouse->placeStructure(NONE_ID,Structure_RepairYard,pocket.x+14,pocket.y+2,true);
    require(refinery && yard,"Production refinery and repair yard placed in open pocket");
    auto* harvester=dynamic_cast<Harvester*>(pLocalHouse->placeUnit(Unit_Harvester,pocket.x+1,pocket.y+8,true));
    auto* tank=pLocalHouse->placeUnit(Unit_Tank,pocket.x+1,pocket.y+10,true);
    require(harvester && tank,"Production vehicles available for building-entry checks");
    harvester->setTarget(refinery);
    harvester->returningToRefinery=true;
    tank->setTarget(yard); // Production booking sets goingToRepairYard.
    for(const auto pair : {std::make_pair(static_cast<UnitBase*>(harvester),refinery),std::make_pair(tank,yard)}) {
        const Coord goal=pair.second->getClosestPoint(pair.first->getLocation());
        require(pair.first->canPass(goal.x,goal.y),"Free booked building remains enterable");
        AStarSearch search(currentGameMap,pair.first,pair.first->getLocation(),goal);
        const auto path=search.getFoundPath();
        require(!path.empty() && path.back()==goal,"Free building route reaches its exact entry tile");
    }
    checkPendingCounter(*game,harvester,refinery,Coord(pocket.x+7,pocket.y+2));
    relocateTrafficUnit(harvester,Coord(pocket.x+1,pocket.y+8));
    // Tornie-only units must be exercised only by a Tornie save fixture.
    if(ModManager::instance().isTornieContentActive()) {
        auto* rebel=dynamic_cast<RebelHarvester*>(pLocalHouse->placeUnit(Unit_RebelHarvester,pocket.x+2,pocket.y+8,true));
        require(rebel!=nullptr,"Production RebelHarvester available for the parallel guard");
        checkPendingCounter(*game,rebel,refinery,Coord(pocket.x+7,pocket.y+2));
    }
    SDL_Log("BUDGET_PROBE_BUILDING_ENTRY: refinery=1 repair_yard=1");
    game.reset(); currentGame=nullptr; pLocalPlayer=nullptr;
}
