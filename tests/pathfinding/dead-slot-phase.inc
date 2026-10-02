// Included by budget-probe.inc only.
//
// The checkpoint boundary Codex identified: an active job's unit can be destroyed
// later in the same cycle, after the path phase, so a legitimate checkpoint can
// contain a suspended slot whose unit is absent from the serialized world. That
// slot must survive the restore and be cancelled by the next path phase in the
// same order, with the same stats and budget effect, as in the uninterrupted run.
//
// The restore uses the ACTUAL spectator path, because that is the only context in
// which the comparison is meaningful: Game::spectatorSnapshot() produces the
// GameInitSettings a viewer is admitted with, and both UnitBase's negative
// queued-timer sentinel and loadSaveGame's city-cache reconciliation are
// deliberately skipped while isSpectating(). Loading the same bytes as an ordinary
// save would normalise state the host never normalised, which is what made an
// earlier version of this test appear to drift. Production semantics are not
// touched to make this pass.

struct DeadSlotRun {
    std::vector<std::string> digests;
    std::vector<std::string> continuations;
    std::string finalSave;
    size_t applied = 0;
    size_t cancelled = 0;
    size_t startedDelta = 0;
};

// Ordinary saves include the local player id, selection and viewport. A passive
// viewer has a detached local player and its own view. Canonicalise only those
// UI fields and each structure's local fog-display frame while saving, then
// restore them. Every gameplay byte remains
// covered, alongside exact per-cycle digests and observer continuation bytes.
std::string comparableDeadSlotSave(Game& game) {
    HumanPlayer* localPlayer = pLocalPlayer;
    House* localHouse = pLocalHouse;
    const auto selection = game.selectedList;
    struct SelectedView { ObjectBase* object; bool selected; bool other; };
    std::vector<SelectedView> selectedViews;
    game.objectManager.forEachObject([&](Uint32, const ObjectBase* value) {
        auto* object = const_cast<ObjectBase*>(value);
        selectedViews.push_back({object, object->selected, object->selectedByOtherPlayer});
        object->selected = false; object->selectedByOtherPlayer = false;
    });
    struct FogView { StructureBase* object; bool fogged; int frame; };
    std::vector<FogView> fogViews;
    for(auto* structure : structureList) {
        fogViews.push_back({structure, structure->fogged, structure->lastVisibleFrame});
        structure->fogged = false; structure->lastVisibleFrame = 0;
    }
    OMemoryStream view; view.open(); screenborder->save(view);
    HumanPlayer* canonicalPlayer = nullptr;
    for(int id = 0; id < 256 && canonicalPlayer == nullptr; ++id) {
        canonicalPlayer = dynamic_cast<HumanPlayer*>(game.getPlayerByID(id));
    }
    require(canonicalPlayer != nullptr, "The fixture has a registered human view identity");
    pLocalPlayer = canonicalPlayer;
    pLocalHouse = game.getHouse(canonicalPlayer->getHouse()->getHouseID());
    game.selectedList.clear();
    screenborder->setNewScreenCenter(Coord(0, 0));
    OMemoryStream bytes; bytes.open(); game.saveGame(bytes);
    pLocalPlayer = localPlayer; pLocalHouse = localHouse; game.selectedList = selection;
    for(const auto& selected : selectedViews) {
        selected.object->selected = selected.selected;
        selected.object->selectedByOtherPlayer = selected.other;
    }
    for(const auto& fog : fogViews) {
        fog.object->fogged = fog.fogged; fog.object->lastVisibleFrame = fog.frame;
    }
    IMemoryStream previousView(view.getData(), view.getDataLength());
    screenborder->load(previousView);
    return std::string(bytes.getData(), bytes.getDataLength());
}

// A spectator only advances when the host has prepared its cycle. The probe
// supplies that gate directly; the real command transport is covered separately by
// tests/network/run-late-join-probe.py.
DeadSlotRun runDeadSlotTail(Game& game, int tail, const std::string& savePath, bool spectator) {
    DeadSlotRun result;
    const size_t appliedBefore = game.pathResultsApplied;
    const size_t cancelledBefore = game.pathRequestsCancelled;
    const size_t startedBefore = game.pathRequestsStarted;
    for(int i = 0; i < tail; ++i) {
        game.negotiatedBudget = 5000;
        if(spectator) game.observerCyclePrepared = true;
        game.updateGameState();
        requireLifecycle(game, "Request lifecycle holds through the dead-slot tail");
        result.digests.push_back(digestOf(game));
        result.continuations.push_back(game.saveObserverRuntime());
    }
    result.applied = game.pathResultsApplied - appliedBefore;
    result.cancelled = game.pathRequestsCancelled - cancelledBefore;
    result.startedDelta = game.pathRequestsStarted - startedBefore;
    result.finalSave = comparableDeadSlotSave(game);
    require(!result.finalSave.empty(), "The dead-slot tail save is readable");
    std::ofstream copy(savePath, std::ios::binary);
    copy.write(result.finalSave.data(), static_cast<std::streamsize>(result.finalSave.size()));
    return result;
}

void checkDeadActiveSlotCheckpoint() {
    const char* configured = std::getenv("DUNECITY_PATH_WORKERS");
    const bool hadOverride = configured != nullptr;
    const std::string previousWorkers = configured ? configured : "";
    auto selectWorkers = [](const char* count) {
#if defined(_WIN32)
        _putenv_s("DUNECITY_PATH_WORKERS", count ? count : "");
#else
        if(count) setenv("DUNECITY_PATH_WORKERS", count, 1);
        else unsetenv("DUNECITY_PATH_WORKERS");
#endif
    };
    const int tail = 60;
    const std::string plainSave = gOutput + "/dead-slot-plain.dls";
    const std::string restoredSave = gOutput + "/dead-slot-restored.dls";
    std::string runtime;
    std::unique_ptr<GameInitSettings> snapshot;
    Uint32 doomedID = NONE_ID;
    std::vector<Uint32> snapshotUnits;
    DeadSlotRun plain;

    {
        selectWorkers("1");
        // Hosts and observers share the network budget policy (no single-player
        // carry-over). Keep the fixture's host in that context too.
        pNetworkManager = std::make_unique<NetworkManager>(0, std::string("http://127.0.0.1:1/"));
        pNetworkManager->bIsServer = true;
        auto game = loadFixture();
        require(PathWorkerPool::instance().workerCount() == 1, "The checkpoint host uses serial slices");
        advanceToSuspension(*game, 60);
        require(!game->activePathJobs.empty(), "A suspended search exists to orphan");
        snapshotUnits = suspendedUnitIds(*game);

        // Destroy the unit of a suspended slot. This is exactly what a unit dying
        // later in the cycle does to the scheduler.
        doomedID = game->activePathJobs.front().objectId;
        auto* doomed = dynamic_cast<UnitBase*>(game->getObjectManager().getObject(doomedID));
        require(doomed != nullptr, "The suspended slot's unit is addressable");
        doomed->setHealth(0);
        doomed->destroy();
        require(game->getObjectManager().getObject(doomedID) == nullptr, "The unit is gone from the world");
        require(findJob(*game, doomedID) != nullptr, "Its suspended slot survives the destruction");
        require(game->pendingPathRequestIds.count(doomedID) == 1, "Its pending id survives the destruction");
        requireLifecycle(*game, "A slot whose unit died is a legitimate state");

        // The host's admission payload: the world as a network snapshot plus the
        // runtime that carries the suspended continuations.
        snapshot.reset(new GameInitSettings(game->spectatorSnapshot()));
        runtime = game->saveObserverRuntime();
        require(!runtime.empty(), "Captured the runtime at the dead-slot boundary");

        plain = runDeadSlotTail(*game, tail, plainSave, false);
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
        pNetworkManager.reset();
    }
    require(plain.cancelled > 0, "The uninterrupted run cancelled the orphaned slot");

    {
        selectWorkers("4");
        // Real observer identity and context, established BEFORE the world load so
        // the spectator branches in UnitBase and loadSaveGame apply.
        // Bound to an ephemeral loopback port; nothing is contacted. The real
        // transfer is covered by tests/network/run-late-join-probe.py.
        pNetworkManager = std::make_unique<NetworkManager>(0, std::string("http://127.0.0.1:1/"));
        settings.general.playerName = "DeadSlotObserver";
        pNetworkManager->playerName = settings.general.playerName;
        pNetworkManager->spectators.insert(settings.general.playerName);
        require(pNetworkManager->isSpectating(), "The probe really holds observer identity");

        auto game = std::make_unique<Game>();
        currentGame = game.get();
        game->bReplay = true; // Suppress public match analytics for this private probe.
        game->initGame(*snapshot);
        game->bReplay = false;
        // The real admission flow initialises the loop before the first advance.
        // initializeNetwork() consumes the transferred supplement itself.
        pNetworkManager->observerRuntime = runtime;
        game->initializeGameLoop();
        require(PathWorkerPool::instance().workerCount() == 4, "The restored spectator uses four workers");
        game->bPause = false;
        require(game->isSpectating(), "The restored game is in spectator context");
        require(game->getObjectManager().getObject(doomedID) == nullptr,
            "The snapshot world really does not contain the destroyed unit");

        require(findJob(*game, doomedID) != nullptr,
            "The restore preserved the orphaned slot instead of rejecting the stream");
        require(game->pendingPathRequestIds.count(doomedID) == 1,
            "The restore preserved the orphaned slot's pending id");
        requireLifecycle(*game, "The restore rebuilt a consistent lifecycle with an orphaned slot");
        size_t restoredSuspended = game->activePathJobs.size();

        const DeadSlotRun restored = runDeadSlotTail(*game, tail, restoredSave, true);
        require(findJob(*game, doomedID) == nullptr, "The next path phase cancelled the orphaned slot");
        require(game->pendingPathRequestIds.count(doomedID) == 0, "The cancellation released the pending id");
        // The snapshot's own searches must actually finish inside the tail, so this
        // is continuation equality through completion, not through waiting.
        size_t stillRunning = 0;
        for(const Uint32 id : snapshotUnits) if(findJob(*game, id) != nullptr) ++stillRunning;

        size_t digestMismatch = plain.digests.size();
        for(size_t i = 0; i < plain.digests.size() && i < restored.digests.size(); ++i) {
            if(plain.digests[i] != restored.digests[i]) { digestMismatch = i; break; }
        }
        SDL_Log("BUDGET_PROBE_DEAD_SLOT: doomed=%u restored_suspended=%zu snapshot_jobs=%zu still_running=%zu "
                "cancelled plain=%zu restored=%zu applied plain=%zu restored=%zu started plain=%zu restored=%zu "
                "first_digest_mismatch=%zu of %d save_equal=%d save_bytes=%zu/%zu",
            doomedID, restoredSuspended, snapshotUnits.size(), stillRunning,
            plain.cancelled, restored.cancelled, plain.applied, restored.applied,
            plain.startedDelta, restored.startedDelta, digestMismatch, tail,
            plain.finalSave == restored.finalSave ? 1 : 0,
            plain.finalSave.size(), restored.finalSave.size());

        require(stillRunning == 0, "Every search the snapshot carried completed inside the tail");
        require(restored.cancelled == plain.cancelled, "The orphaned slot cancels identically after a restore");
        require(restored.startedDelta == plain.startedDelta, "The same requests start after a restore");
        require(restored.applied == plain.applied, "The same routes are applied after a restore");
        require(restored.digests == plain.digests,
            "The spectator-restored continuation produces identical state digests");
        require(restored.continuations == plain.continuations,
            "The spectator-restored continuation preserves every suspended search and queue");
        require(restored.finalSave == plain.finalSave,
            "The spectator-restored continuation produces identical saved gameplay bytes");

        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
        pNetworkManager.reset();
    }
    selectWorkers(hadOverride ? previousWorkers.c_str() : nullptr);
}
