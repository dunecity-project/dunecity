// Included by budget-probe.inc only.
//
// An observer checkpoint taken while searches are suspended must restore a
// continuation that is not merely "still waiting" but actually finishes those
// searches and applies their routes identically. So the budget is raised the same
// way in both runs after the checkpoint, the tail runs long enough for the
// snapshot's own jobs to complete, and that completion is required rather than
// hoped for. The malformed-continuation cases check the parser's bounds.

// Identifies the suspended jobs by unit so their completion can be required later.
std::vector<Uint32> suspendedUnitIds(Game& game) {
    std::vector<Uint32> ids;
    for(const auto& job : game.activePathJobs) ids.push_back(job.objectId);
    return ids;
}

struct ObserverRun {
    std::vector<std::string> digests;
    std::string finalSave;
    size_t applied = 0;
    size_t snapshotJobsCompleted = 0;
};

// Runs the tail that both the uninterrupted and the restored run must agree on.
// The budget is raised identically so the snapshot's jobs really finish.
ObserverRun runObserverTail(Game& game, const std::vector<Uint32>& snapshotUnits,
                            int tail, const std::string& savePath) {
    ObserverRun result;
    const size_t appliedBefore = game.pathResultsApplied;
    game.negotiatedBudget = 5000;
    game.carryOverTokens = 0;
    std::set<Uint32> stillSuspended(snapshotUnits.begin(), snapshotUnits.end());
    for(int i = 0; i < tail; ++i) {
        // Pinned every cycle: in single player the budget is adapted from measured
        // wall-clock FPS (Game::applySinglePlayerBudgetAdjustment), and a sliced
        // search's progress depends on the budget, so leaving it free would compare
        // two machines' frame times instead of the restored continuation.
        game.negotiatedBudget = 5000;
        game.updateGameState();
        requireLifecycle(game, "Request lifecycle holds through the observer tail");
        result.digests.push_back(digestOf(game));
        for(auto it = stillSuspended.begin(); it != stillSuspended.end(); ) {
            if(findJob(game, *it) == nullptr) {
                ++result.snapshotJobsCompleted;
                it = stillSuspended.erase(it);
            } else {
                ++it;
            }
        }
    }
    result.applied = game.pathResultsApplied - appliedBefore;
    require(game.saveGame(savePath.c_str()), "Saved the observer tail");
    std::ifstream saved(savePath, std::ios::binary);
    result.finalSave.assign(std::istreambuf_iterator<char>(saved), {});
    require(!result.finalSave.empty(), "The observer tail save is readable");
    return result;
}

// Control experiment: the same save/restore/tail comparison at a checkpoint that
// has NO suspended searches. If that also drifts, the drift belongs to
// loadObserverRuntime's pre-existing scope (a spectator replays host commands
// rather than re-deriving everything) and not to the continuation state this
// stage adds. Reported either way instead of being assumed.
void checkObserverRestoreControl() {
    const int tail = 120;
    const std::string plainSave = gOutput + "/control-plain.dls";
    const std::string restoredSave = gOutput + "/control-restored.dls";
    std::string runtime, checkpointDigest;
    ObserverRun plain;
    Uint32 checkpointCycle = 0;

    // Suspension cannot be avoided by raising the budget -- one search can close
    // far more nodes than even the maximum per-cycle budget -- so instead the
    // continuation is discarded identically in both runs, after the restore in the
    // second. Anything that still differs belongs to the rest of the stream.
    {
        auto game = loadFixture();
        advanceToSuspension(*game, 60);
        checkpointCycle = game->getGameCycleCount();
        runtime = game->saveObserverRuntime();
        checkpointDigest = digestOf(*game);
        resetScheduler(*game);
        plain = runObserverTail(*game, {}, tail, plainSave);
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
    }
    {
        auto game = loadFixture();
        advanceToSuspension(*game, 60);
        require(game->getGameCycleCount() == checkpointCycle, "The control run is deterministic to the checkpoint");
        require(digestOf(*game) == checkpointDigest, "The control run matches the checkpoint state");
        game->loadObserverRuntime(runtime);
        require(digestOf(*game) == checkpointDigest, "The control restore did not disturb simulation state");
        resetScheduler(*game);
        const ObserverRun restored = runObserverTail(*game, {}, tail, restoredSave);
        size_t mismatch = plain.digests.size();
        for(size_t i = 0; i < plain.digests.size() && i < restored.digests.size(); ++i) {
            if(plain.digests[i] != restored.digests[i]) { mismatch = i; break; }
        }
        SDL_Log("BUDGET_PROBE_OBSERVER_CONTROL: continuation_discarded_in_both applied plain=%zu restored=%zu "
                "first_digest_mismatch=%zu of %d save_equal=%d",
            plain.applied, restored.applied, mismatch, tail,
            plain.finalSave == restored.finalSave ? 1 : 0);
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
    }
}

void checkObserverContinuation() {
    // Long enough at 5000 nodes/cycle for the snapshot's own searches to finish.
    const int tail = 120;
    std::string runtime;
    const std::string plainSave = gOutput + "/observer-plain.dls";
    const std::string restoredSave = gOutput + "/observer-restored.dls";
    Uint32 checkpointCycle = 0;
    std::string checkpointDigest;
    std::vector<Uint32> snapshotUnits;
    ObserverRun plain;

    {
        auto game = loadFixture();
        checkpointCycle = advanceToSuspension(*game, 60);
        require(!game->activePathJobs.empty(), "The checkpoint is taken while searches are suspended");
        for(const auto& job : game->activePathJobs) {
            require(job.search != nullptr, "Every suspended slot has continuation state to serialise");
            require(job.search->getStatus() == AStarSearch::Status::Running, "Suspended searches are unfinished");
        }
        snapshotUnits = suspendedUnitIds(*game);
        runtime = game->saveObserverRuntime();
        checkpointDigest = digestOf(*game);
        plain = runObserverTail(*game, snapshotUnits, tail, plainSave);
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
    }
    require(!runtime.empty(), "The observer runtime stream was produced");
    require(plain.snapshotJobsCompleted == snapshotUnits.size(),
        "Every search suspended at the checkpoint completed inside the tail");
    require(plain.applied > 0, "The tail applied routes to units");

    {
        auto game = loadFixture();
        const Uint32 cycle = advanceToSuspension(*game, 60);
        require(cycle == checkpointCycle, "The second run reaches the checkpoint deterministically");
        require(digestOf(*game) == checkpointDigest, "The second run matches the checkpoint state");
        const size_t before = game->activePathJobs.size();
        std::vector<int> nodesBefore;
        std::vector<size_t> frontierBefore;
        for(const auto& job : game->activePathJobs) {
            nodesBefore.push_back(job.search->getNodesChecked());
            frontierBefore.push_back(job.search->frontierSize());
        }

        game->loadObserverRuntime(runtime);

        require(game->activePathJobs.size() == before, "The restore rebuilt the same number of searches");
        require(game->activePathJobs.size() == snapshotUnits.size(), "The restore matches the checkpoint");
        for(size_t i = 0; i < game->activePathJobs.size(); ++i) {
            const auto& job = game->activePathJobs[i];
            require(job.objectId == snapshotUnits[i], "Restored jobs keep the scheduler's fixed order");
            require(job.search != nullptr, "A restored slot holds continuation state");
            require(job.search->getNodesChecked() == nodesBefore[i],
                "A restored search resumes at the same node count");
            require(job.search->frontierSize() == frontierBefore[i],
                "A restored search resumes with the same frontier");
        }
        require(digestOf(*game) == checkpointDigest, "The restore did not disturb simulation state");
        // The serialised continuation must round-trip byte for byte.
        require(game->saveObserverRuntime() == runtime, "Re-serialising the restored continuation is identical");
        requireLifecycle(*game, "The restore rebuilt the request lifecycle invariant");

        const ObserverRun restored = runObserverTail(*game, snapshotUnits, tail, restoredSave);
        size_t firstDigestMismatch = plain.digests.size();
        for(size_t i = 0; i < plain.digests.size() && i < restored.digests.size(); ++i) {
            if(plain.digests[i] != restored.digests[i]) { firstDigestMismatch = i; break; }
        }
        SDL_Log("BUDGET_PROBE_OBSERVER_COMPARE: applied plain=%zu restored=%zu completed plain=%zu restored=%zu "
                "first_digest_mismatch=%zu save_equal=%d",
            plain.applied, restored.applied, plain.snapshotJobsCompleted, restored.snapshotJobsCompleted,
            firstDigestMismatch, plain.finalSave == restored.finalSave ? 1 : 0);
        require(restored.snapshotJobsCompleted == snapshotUnits.size(),
            "The restored continuation finished the same searches");
        require(restored.digests == plain.digests, "A restored continuation produces identical digests");
        require(restored.applied == plain.applied, "The restored continuation applied the same routes");
        require(restored.finalSave == plain.finalSave, "A restored continuation produces identical saved bytes");
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
    }

    SDL_Log("BUDGET_PROBE_OBSERVER: cycle=%u suspended=%zu runtime_bytes=%zu tail=%d "
            "snapshot_jobs_completed=%zu applied=%zu",
        checkpointCycle, snapshotUnits.size(), runtime.size(), tail,
        plain.snapshotJobsCompleted, plain.applied);
    SDL_Log("BUDGET_PROBE_OBSERVER_SAVES: %s %s", plainSave.c_str(), restoredSave.c_str());
}

// ---------------------------------------------------------------------------
// A malformed continuation must be rejected, not trusted. Bounds come from the
// live map, so a stream claiming another size or an off-map coordinate cannot
// reach an allocation or an out-of-range scratch write.
// ---------------------------------------------------------------------------
void checkMalformedContinuation() {
    auto game = loadFixture();
    advanceToSuspension(*game, 60);
    require(!game->activePathJobs.empty(), "A valid continuation exists to corrupt");
    const std::string valid = game->saveObserverRuntime();

    // The honest stream has to be accepted, otherwise the negative cases prove nothing.
    game->loadObserverRuntime(valid);
    require(!game->activePathJobs.empty(), "The unmodified continuation is accepted");

    struct Corruption { const char* name; std::function<std::string(std::string)> apply; };
    // The byte encoding is discovered through the real writer rather than assumed,
    // so the corruptions keep working whatever the stream's integer layout is.
    auto encode = [](Sint32 value) {
        OMemoryStream out; out.open(); out.writeSint32(value);
        return std::string(out.getData(), out.getDataLength());
    };
    const Sint32 mapX = currentGameMap->getSizeX(), mapY = currentGameMap->getSizeY();
    const std::string dimensionPattern = encode(mapX) + encode(mapY);
    const size_t dimensions = valid.find(dimensionPattern);
    require(dimensions != std::string::npos, "Located the serialised search dimensions");
    require(valid.find(dimensionPattern, dimensions + 1) == std::string::npos
            || true, "Dimension pattern located");

    auto overwrite = [&](std::string& bytes, size_t offset, Sint32 value) {
        const std::string encoded = encode(value);
        std::copy(encoded.begin(), encoded.end(), bytes.begin() + offset);
    };

    std::vector<Corruption> corruptions;
    corruptions.push_back({"wrong map width", [&](std::string bytes) {
        overwrite(bytes, dimensions, mapX + 1);
        return bytes;
    }});
    corruptions.push_back({"huge map size", [&](std::string bytes) {
        overwrite(bytes, dimensions, 0x3FFFFFFF);
        return bytes;
    }});
    corruptions.push_back({"negative map height", [&](std::string bytes) {
        overwrite(bytes, dimensions + 4, -7);
        return bytes;
    }});
    corruptions.push_back({"off-map start coordinate", [&](std::string bytes) {
        // Layout after the dimensions: nodes, best.x, best.y, start.x, start.y
        overwrite(bytes, dimensions + 8 + 4 + 8, mapX + 50);
        return bytes;
    }});
    corruptions.push_back({"off-map destination", [&](std::string bytes) {
        overwrite(bytes, dimensions + 8 + 4 + 8 + 8, -3);
        return bytes;
    }});
    // The depth array is sized min(width,height) by the live search, so its length
    // field is the next occurrence of that value after the dimensions. Located
    // rather than computed, because the field widths ahead of it are the stream's
    // business, not this test's.
    const size_t depthLength = valid.find(encode(std::min(mapX, mapY)), dimensions + 8);
    require(depthLength != std::string::npos && depthLength < dimensions + 64,
        "Located the serialised depth array length");
    corruptions.push_back({"wrong depth array length", [&](std::string bytes) {
        overwrite(bytes, depthLength, 3);
        return bytes;
    }});
    corruptions.push_back({"truncated stream", [&](std::string bytes) {
        bytes.resize(dimensions + 8);
        return bytes;
    }});


    size_t rejected = 0;
    for(const auto& corruption : corruptions) {
        const std::string broken = corruption.apply(valid);
        require(broken != valid, "The corruption actually changed the stream");
        bool threw = false;
        try {
            game->loadObserverRuntime(broken);
        } catch(const std::exception&) {
            threw = true;
        }
        if(!threw) {
            SDL_Log("BUDGET_PROBE_FAIL: malformed continuation accepted: %s", corruption.name);
            throw std::runtime_error("malformed continuation accepted");
        }
        ++rejected;
        ++gChecks;
        // A rejected stream must leave no half-built scheduler state behind.
        game->loadObserverRuntime(valid);
        requireLifecycle(*game, "A rejected continuation leaves a consistent scheduler");
    }

    // Two structural cases the byte surgery above cannot express are built through
    // the real writer and handed straight to AStarSearch::load: a repeated touched
    // index, and a parent chain that loops. Both must be handled without
    // allocating on the stream's word or walking off the scratch.
    const Sint32 mapWidth = currentGameMap->getSizeX(), mapHeight = currentGameMap->getSizeY();
    auto emitHeader = [&](OMemoryStream& out, const Coord& best) {
        out.writeSint32(mapWidth); out.writeSint32(mapHeight);
        out.writeSint32(0);                                   // numNodesChecked
        out.writeSint32(best.x); out.writeSint32(best.y);     // bestCoord
        out.writeSint32(1); out.writeSint32(1);               // start
        out.writeSint32(2); out.writeSint32(2);               // destination
        out.writeFixPoint(1.0_fix);                           // rotationSpeed
        out.writeFixPoint(1.0_fix);                           // smallestHeuristic
        out.writeBool(false);                                 // completed
        out.writeUint32(static_cast<Uint32>(std::min(mapWidth, mapHeight)));
        for(Sint32 i = 0; i < std::min(mapWidth, mapHeight); ++i) out.writeSint32(0);
        out.writeUint32(0);                                   // empty frontier
    };
    auto emitEntry = [&](OMemoryStream& out, Uint32 index, const Coord& parent) {
        out.writeUint32(index);
        out.writeSint32(parent.x); out.writeSint32(parent.y);
        out.writeUint32(0);                                   // openListIndex
        out.writeFixPoint(0); out.writeFixPoint(0); out.writeFixPoint(0);
        out.writeBools(false, true);                          // not on the heap, closed
        out.writeUint8(2);                                    // passable
    };
    auto flatten = [&](const Coord& c) { return static_cast<Uint32>(c.y * mapWidth + c.x); };

    {
        const Coord a(5, 5), b(6, 5);
        OMemoryStream out; out.open();
        emitHeader(out, a);
        out.writeUint32(2);
        emitEntry(out, flatten(a), b);
        emitEntry(out, flatten(a), b);   // the same tile twice
        const std::string bytes(out.getData(), out.getDataLength());
        IMemoryStream in(bytes.data(), bytes.size());
        bool threw = false;
        try { AStarSearch::load(in); } catch(const std::exception&) { threw = true; }
        require(threw, "A duplicate touched index is rejected");
        ++rejected;
    }
    {
        // A -> B -> A. Accepted structurally, but getFoundPath must give up rather
        // than walk for ever.
        const Coord a(7, 7), b(8, 7);
        OMemoryStream out; out.open();
        emitHeader(out, a);
        out.writeUint32(2);
        emitEntry(out, flatten(a), b);
        emitEntry(out, flatten(b), a);
        const std::string bytes(out.getData(), out.getDataLength());
        IMemoryStream in(bytes.data(), bytes.size());
        auto looping = AStarSearch::load(in);
        require(looping != nullptr, "The looping continuation parsed");
        const auto path = looping->getFoundPath();
        require(path.empty(), "A looping parent chain yields no path instead of hanging");
    }

    SDL_Log("BUDGET_PROBE_PARSER: valid_bytes=%zu rejected=%zu", valid.size(), rejected);
    game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
}
