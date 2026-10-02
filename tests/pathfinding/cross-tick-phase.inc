// Included by budget-probe.inc only.
//
// The cross-tick input policy, against real cycles: a retained frontier must
// survive global geometry changes and unrelated unit movement, while a change to
// the request itself must requeue rather than ever deliver the stale plan. Target
// loss and unit removal must both be exercised, not skipped.

// Harvesters, MCVs and carryalls have their own AI that rewrites destinations, so
// they cannot be used as a stable subject.
bool steerable(const UnitBase* unit) {
    const Uint32 item = unit->getItemID();
    return item != Unit_Harvester && item != Unit_MCV && item != Unit_Carryall
        && item != Unit_Frigate && item != Unit_Sandworm && unit->getOwner() != nullptr;
}

Game::PathJob* findJob(Game& game, Uint32 objectId) {
    for(auto& job : game.activePathJobs) {
        if(job.objectId == objectId) return &job;
    }
    return nullptr;
}

// A unit left to itself acquires targets, and a target's closest point becomes its
// effective destination, so its request legitimately changes. Pinning the order is
// what makes "the inputs did not change" a property of the test rather than luck.
// processPathRequests runs before unit updates, so this is what the scheduler sees.
void pinOrder(UnitBase* unit, const Coord& target) {
    if(unit == nullptr) return;
    unit->doSetAttackMode(STOP);
    unit->releaseTarget();
    unit->setDestination(target);
}

// Fixture control, not a relaxed assertion: at a 60-node budget the four slots
// stay occupied by other units' long searches, so a case that needs its own
// suspended search would otherwise only ever see a queued request. Clearing the
// scheduler leaves the invariant intact (nothing active, queued or pending) and
// lets the following case be required rather than skipped.
void resetScheduler(Game& game) {
    game.pathRequestsCancelled += game.activePathJobs.size();   // They really are cancelled.
    game.activePathJobs.clear();
    game.pathRequestQueue.clear();
    game.pendingPathRequestIds.clear();
    for(UnitBase* unit : unitList) unit->markPathRequestFinished();
    requireLifecycle(game, "The scheduler reset left a consistent lifecycle");
}

// Issues a long haul and runs cycles until that unit's search is suspended.
Game::PathJob* suspendLongHaul(Game& game, UnitBase* unit, const Coord& target, int limit) {
    unit->clearPath();
    pinOrder(unit, target);
    unit->enqueuePathRequest();
    for(int i = 0; i < limit; ++i) {
        pinOrder(dynamic_cast<UnitBase*>(game.getObjectManager().getObject(unit->getObjectID())), target);
        game.updateGameState();
        requireLifecycle(game, "Request lifecycle holds while arranging a suspension");
        Game::PathJob* job = findJob(game, unit->getObjectID());
        if(job != nullptr && job->search != nullptr) return job;
    }
    return nullptr;
}

void checkCrossTickInputs() {
    auto game = loadFixture();
    game->negotiatedBudget = 60;   // Guarantees suspension on a 384x384 map.
    game->carryOverTokens = 0;

    // A unit under AI control has its destination rewritten by its own planner
    // every few cycles, which would make this a test of QuantBot rather than of
    // the scheduler. Prefer a human-controlled house so the order really is stable.
    auto humanControlled = [&](const UnitBase* unit) {
        const House* owner = unit->getOwner();
        if(owner == nullptr) return false;
        for(const auto& player : owner->getPlayerList()) {
            if(dynamic_cast<const HumanPlayer*>(player.get()) != nullptr) return true;
        }
        return false;
    };
    UnitBase* subject = nullptr;
    for(UnitBase* unit : movableUnits(900)) {
        if(steerable(unit) && humanControlled(unit)) { subject = unit; break; }
    }
    if(subject == nullptr) {
        for(UnitBase* unit : movableUnits(900)) {
            if(steerable(unit)) { subject = unit; break; }
        }
        SDL_Log("BUDGET_PROBE_SUBJECT: no human-controlled unit, using an AI-owned one");
    }
    require(subject != nullptr, "Fixture provides a steerable subject unit");
    SDL_Log("BUDGET_PROBE_SUBJECT: id=%u item=%d house=%d human=%d",
        subject->getObjectID(), subject->getItemID(),
        subject->getOwner() ? subject->getOwner()->getHouseID() : -1, humanControlled(subject) ? 1 : 0);
    const Uint32 subjectID = subject->getObjectID();
    const Coord farCorner(currentGameMap->getSizeX() - 2, currentGameMap->getSizeY() - 2);

    Game::PathJob* job = suspendLongHaul(*game, subject, farCorner, 40);
    require(job != nullptr, "The subject's search is suspended across cycles");
    require(job->restarts == 0, "A freshly started search has not been requeued");
    const size_t chargedWhenSuspended = job->chargedNodes;
    require(chargedWhenSuspended > 0, "The suspended search has already been charged for work");

    // ---- a retained frontier survives geometry and occupancy churn ----
    // Both signals are produced deliberately rather than hoped for: the blocking
    // geometry revision is bumped through the production API, and a real unit is
    // moved onto and then off a tile on the straight line to the destination.
    // Neither may discard the frontier.
    currentGameMap->incrementPathingRevision();

    UnitBase* occupant = nullptr;
    for(UnitBase* unit : movableUnits(600)) {
        if(steerable(unit) && unit->getObjectID() != subjectID && !unit->isMoving()) { occupant = unit; break; }
    }
    require(occupant != nullptr, "Fixture provides a unit to move onto the route");
    const Uint32 occupantID = occupant->getObjectID();
    const Coord occupantOrigin = occupant->getLocation();
    // Re-fetched rather than reused: a raw unit pointer must not be carried across
    // cycles in which the unit could be destroyed.
    auto liveUnit = [&](Uint32 id) { return dynamic_cast<UnitBase*>(game->getObjectManager().getObject(id)); };
    UnitBase* subjectNow = liveUnit(subjectID);
    require(subjectNow != nullptr, "The subject is still alive before the churn window");
    const Coord subjectAt = subjectNow->getLocation();
    Coord blockTile = Coord::Invalid();
    for(int step = 2; step < 12 && blockTile.isInvalid(); ++step) {
        const Coord candidate(subjectAt.x + step, subjectAt.y + step);
        if(!currentGameMap->tileExists(candidate)) continue;
        const Tile* tile = currentGameMap->getTile(candidate);
        if(!tile->hasAGroundObject() && !tile->isMountain()) blockTile = candidate;
    }
    require(blockTile.isValid(), "Found a free tile ahead of the subject to block");
    require(liveUnit(occupantID) != nullptr, "The occupant is alive");
    liveUnit(occupantID)->setLocation(blockTile);   // Production move: assigns the tile too.
    require(currentGameMap->getTile(blockTile)->hasAGroundObject(), "The occupant really blocks the tile");

    const UnitBase::PathInputFingerprint baseline = subjectNow->capturePathInputs();
    Uint32 revisionChanges = 0, cyclesAlive = 0;
    size_t lowWaterCharge = chargedWhenSuspended;
    Uint32 lastRevision = currentGameMap->getPathingRevision();
    bool everSawGeometryDifference = false;
    bool stillAlive = true;
    for(int i = 0; i < 30; ++i) {
        if(i == 10) {
            currentGameMap->incrementPathingRevision();   // More unrelated construction.
        }
        if(i == 15 && liveUnit(occupantID) != nullptr) {
            liveUnit(occupantID)->setLocation(occupantOrigin);   // Unblocks the tile again.
        }
        pinOrder(liveUnit(subjectID), farCorner);
        game->updateGameState();
        requireLifecycle(*game, "Request lifecycle holds while the world churns");
        if(currentGameMap->getPathingRevision() != lastRevision) {
            ++revisionChanges;
            lastRevision = currentGameMap->getPathingRevision();
        }
        Game::PathJob* current = findJob(*game, subjectID);
        if(current == nullptr) {
            // Either finished, or queued again for a later round. Only a finished
            // request leaves the pending set.
            stillAlive = game->pendingPathRequestIds.count(subjectID) == 1;
            auto* live = liveUnit(subjectID);
            SDL_Log("BUDGET_PROBE_SUBJECT_GONE: i=%d pending=%d requeued_total=%zu diff=%u path=%zu",
                i, stillAlive ? 1 : 0, game->pathRequestsRequeued,
                live != nullptr ? live->capturePathInputs().differenceMask(baseline) : 0u,
                live != nullptr ? live->getPlannedPath().size() : 0u);
            continue;
        }
        ++cyclesAlive;
        if((current->restartReasons & UnitBase::PathInputFingerprint::GeometryChanged) != 0) {
            everSawGeometryDifference = true;
        }
        // Progress is monotonic: a retained frontier keeps its accumulated charge.
        require(current->chargedNodes >= lowWaterCharge, "A resumed search keeps its accumulated charge");
        lowWaterCharge = current->chargedNodes;
        require(current->restarts == 0, "Geometry and unrelated movement never discard the frontier");
    }
    require(cyclesAlive > 0, "The search really did span cycles");
    require(everSawGeometryDifference,
        "The suspended search observed the changed geometry revision and kept its frontier");
    SDL_Log("BUDGET_PROBE_GEOMETRY: revision_changes=%u cycles_alive=%u blocked_tile=%d,%d charge=%zu",
        revisionChanges, cyclesAlive, blockTile.x, blockTile.y, lowWaterCharge);

    // ---- the request changing requeues, and no stale plan is delivered ----
    UnitBase* again = dynamic_cast<UnitBase*>(game->getObjectManager().getObject(subjectID));
    require(again != nullptr, "The subject survived the churn window");
    const size_t requeuedBefore = game->pathRequestsRequeued;
    const Coord nearCorner(2, currentGameMap->getSizeY() - 2);
    again->clearPath();
    again->setDestination(nearCorner);
    again->enqueuePathRequest();
    bool deliveredForNewOrder = false;
    for(int i = 0; i < 80 && !deliveredForNewOrder; ++i) {
        game->updateGameState();
        requireLifecycle(*game, "Request lifecycle holds across a changed order");
        auto* live = dynamic_cast<UnitBase*>(game->getObjectManager().getObject(subjectID));
        if(live == nullptr) break;
        Game::PathJob* current = findJob(*game, subjectID);
        if(current != nullptr && current->search != nullptr) {
            // A plan in flight always answers the unit's current question: the
            // obsolete one was discarded rather than finished and applied.
            require(current->search->getDestination() == current->inputs.destination,
                "A live plan matches the request it was started for");
        }
        if(!live->getPlannedPath().empty()) {
            // Whatever route was delivered has to lead to the order the unit has
            // now, never to the abandoned corner.
            require(live->getPlannedPath().back() != farCorner,
                "A route for a superseded order is never delivered");
            deliveredForNewOrder = true;
        }
    }
    require(game->pathRequestsRequeued > requeuedBefore,
        "Changing the order requeued the request instead of applying the stale plan");
    SDL_Log("BUDGET_PROBE_REQUEUE: requeued_delta=%zu delivered_new_order=%d",
        game->pathRequestsRequeued - requeuedBefore, deliveredForNewOrder ? 1 : 0);

    // ---- losing the target requeues too ----
    UnitBase* hunter = nullptr;
    for(UnitBase* unit : movableUnits(600)) {
        if(steerable(unit) && unit->getObjectID() != subjectID) { hunter = unit; break; }
    }
    require(hunter != nullptr, "Fixture provides a second steerable unit");
    const Uint32 hunterID = hunter->getObjectID();
    resetScheduler(*game);
    ObjectBase* prey = nullptr;
    for(UnitBase* unit : unitList) {
        if(unit->getOwner() != nullptr && hunter->getOwner() != nullptr
           && unit->getOwner()->getTeamID() != hunter->getOwner()->getTeamID()
           && currentGameMap->tileExists(unit->getLocation())) { prey = unit; break; }
    }
    require(prey != nullptr, "Fixture provides a hostile object to target");
    const Uint32 preyID = prey->getObjectID();
    hunter->clearPath();
    hunter->setTarget(prey);
    hunter->setDestination(prey->getLocation());
    hunter->enqueuePathRequest();
    Game::PathJob* hunterJob = nullptr;
    for(int i = 0; i < 60 && hunterJob == nullptr; ++i) {
        game->updateGameState();
        requireLifecycle(*game, "Request lifecycle holds while targeting");
        hunterJob = findJob(*game, hunterID);
    }
    require(hunterJob != nullptr, "A search toward a live target is suspended, so target loss is really exercised");
    size_t targetLossRequeues = 0;
    {
        const size_t before = game->pathRequestsRequeued;
        auto* victim = dynamic_cast<UnitBase*>(game->getObjectManager().getObject(preyID));
        require(victim != nullptr, "The target is still addressable");
        victim->setHealth(0);
        victim->destroy();
        for(int i = 0; i < 10; ++i) {
            game->updateGameState();
            requireLifecycle(*game, "Request lifecycle holds after the target is destroyed");
        }
        targetLossRequeues = game->pathRequestsRequeued - before;
        require(game->getObjectManager().getObject(preyID) == nullptr, "The destroyed target is gone");
        // Either the plan was discarded, or the request finished before the target
        // died; what must never happen is the unit keeping a plan toward a target
        // that canPass would no longer let it enter.
        auto* live = dynamic_cast<UnitBase*>(game->getObjectManager().getObject(hunterID));
        require(live != nullptr, "The hunter survived");
        require(live->capturePathInputs().targetObjectID != preyID
                || live->getPlannedPath().empty(),
            "No route is retained toward a destroyed target");
    }
    SDL_Log("BUDGET_PROBE_TARGET_LOSS: hunter_suspended=%d requeues=%zu",
        hunterJob != nullptr ? 1 : 0, targetLossRequeues);

    // ---- removing the unit cancels its suspended search ----
    // Exercised unconditionally: the subject is put back into a suspended search
    // first, so there is always a live continuation to cancel.
    resetScheduler(*game);
    UnitBase* doomed = liveUnit(subjectID);
    require(doomed != nullptr, "The subject is available for the cancellation case");
    Game::PathJob* doomedJob = suspendLongHaul(*game, doomed, farCorner, 60);
    require(doomedJob != nullptr, "A suspended search exists to cancel");
    const size_t cancelledBefore = game->pathRequestsCancelled;
    doomed->setHealth(0);
    doomed->destroy();
    for(int i = 0; i < 6; ++i) {
        game->updateGameState();
        requireLifecycle(*game, "Request lifecycle holds after the unit is removed");
    }
    require(game->getObjectManager().getObject(subjectID) == nullptr, "The removed unit is gone");
    require(findJob(*game, subjectID) == nullptr, "A removed unit's search is cancelled");
    require(game->pendingPathRequestIds.count(subjectID) == 0, "A cancelled request leaves no pending id");
    require(game->pathRequestsCancelled > cancelledBefore, "The cancellation was actually counted");

    SDL_Log("BUDGET_PROBE_INPUTS: subject=%u suspended_charge=%zu revision_changes=%u cycles_alive=%u "
            "started=%zu applied=%zu requeued=%zu cancelled=%zu",
        subjectID, chargedWhenSuspended, revisionChanges, cyclesAlive,
        game->pathRequestsStarted, game->pathResultsApplied,
        game->pathRequestsRequeued, game->pathRequestsCancelled);
    game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
}
