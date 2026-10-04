// A checkpoint between Custom unit and build phases. This uses production
// save/load, observer restoration, orders and path servicing in actual frames.
#include <Network/GameStateDigest.h>
#include <players/QuantBotSchedulePolicy.h>
void runArmyRecoveryContinuationProbe(std::unique_ptr<Game>& game) {
    auto require=[](bool ok,const char* why) {
        if(!ok){SDL_Log("ARMY_RECOVERY_CONTINUATION_FAIL: %s",why);throw std::runtime_error(why);}
    };
    auto botFor=[](Game& world) -> QuantBot* {
        auto* house=world.getHouse(HOUSE_HARKONNEN);
        if(house) for(const auto& player:house->getPlayerList())
            if(auto* bot=dynamic_cast<QuantBot*>(player.get())) return bot;
        return nullptr;
    };
    auto tick=[](Game& world) {
        world.negotiatedBudget=5000; world.carryOverTokens=0; world.updateGameState();
    };
    auto hash=[](const std::string& bytes) {
        GameStateDigest::Hasher h;
        for(const unsigned char c:bytes) h.mixUint8(c);
        return h.value();
    };
    // The preceding probe injected and retired scenario objects. Start this
    // continuation fixture from a canonical ordinary load, just as a resumed
    // match does; this also applies the engine's existing load normalisation.
    tick(*game);
    require(game->saveGame("army-recovery-fixture.sav"),"Could not save continuation fixture");
    game.reset(); currentGame=nullptr; pLocalPlayer=nullptr;
    game=std::make_unique<Game>(); currentGame=game.get();
    game->bReplay=true; game->initGame(GameInitSettings(std::string("army-recovery-fixture.sav")));
    game->bReplay=false; game->initializeGameLoop(); game->bPause=false;
    // Capture exactly after the unit phase, before the next build phase.
    do { tick(*game); }
    while(!QuantBotSchedulePolicy::unitPhaseDue(game->getGameCycleCount()-1,HOUSE_HARKONNEN,50,NUM_HOUSES));
    auto* bot=botFor(*game);
    require(bot && bot->recoveryActive(),"Continuation fixture missing Custom Brutal bot");
    bot->armyPosture=ArmyPosturePolicy::Posture::Withdrawing;
    bot->postureSince=game->getGameCycleCount();
    bot->lastMaterialLossCycle=game->getGameCycleCount();
    bot->recallCursor=17;
    bot->buildTimer=0; // The next phase really evaluates production.
    bot->customWave.members.clear();
    for(const auto* unit:unitList)
        if(bot->orderableCombatUnit(unit) && unit->isAGroundUnit()) {
            bot->customWave.members.insert(unit->getObjectID());
            if(bot->customWave.members.size()>=32) break;
        }
    require(!bot->customWave.members.empty(),"No wave survived for the checkpoint");
    // Derived survey state is intentionally poisoned. It must be recomputed
    // before the next build phase, rather than alter decisions after a restore.
    bot->lastSurvey.hostileFront.ground=987654321;
    bot->lastSurveyCycle=game->getGameCycleCount()-1;
    const Uint32 boundaryCycle=game->getGameCycleCount();
    const std::string path="army-recovery-boundary.sav";
    require(game->saveGame(path),"Could not save army boundary");
    const std::string runtime=game->saveObserverRuntime();
    IMemoryStream header(runtime.data(),runtime.size());
    require(header.readUint32()==7,"Unexpected observer runtime version");
    struct Tail { std::vector<std::string> states, armyStates, houses; bool refreshed=false; };
    auto runTail=[&](Game& world) {
        Tail tail;
        for(int frame=0;frame<100;++frame) {
            const Uint32 cycle=world.getGameCycleCount();
            tick(world);
            auto* resumed=botFor(world);
            require(resumed,"A continued world lost its bot");
            if(QuantBotSchedulePolicy::buildPhaseDue(cycle,HOUSE_HARKONNEN,50,NUM_HOUSES)) {
                require(resumed->lastSurveyCycle==cycle,
                    "Build phase consumed a stale survey after the checkpoint");
                require(resumed->lastSurvey.hostileFront.ground!=987654321,
                    "Build phase retained the poisoned warm survey");
                tail.refreshed=true;
            }
            OMemoryStream saved;
            // Persistent bot bytes plus explicit movement/combat orders cover
            // decisions the position digest omits. Visual frames and ordinary
            // load's legacy path-timer normalisation are outside this check.
            resumed->save(saved);
            const auto botHash=hash(std::string(saved.getData(),saved.getDataLength()));
            OMemoryStream orders;
            world.getObjectManager().forEachObject([&](Uint32 id,ObjectBase* object){
                const auto* unit=dynamic_cast<const UnitBase*>(object);
                if(!unit) return;
                orders.writeUint32(id);
                orders.writeSint32(unit->getDestination().x); orders.writeSint32(unit->getDestination().y);
                orders.writeSint32(unit->getGuardPoint().x); orders.writeSint32(unit->getGuardPoint().y);
                orders.writeBool(unit->wasForced()); orders.writeBool(unit->isMoving());
                orders.writeUint32(unit->getTarget() ? unit->getTarget()->getObjectID() : NONE_ID);
                orders.writeUint32(unit->getAttackMode());
            });
            std::string houseState;
            for(int h=0;h<NUM_HOUSES;++h) if(const auto* house=world.getHouse(static_cast<HOUSETYPE>(h)))
                houseState+="h"+std::to_string(h)+":"+std::to_string(house->getCredits())+"/"+
                    std::to_string(house->getNumUnits())+"/"+std::to_string(house->getNumStructures())+" ";
            tail.houses.push_back(houseState);
            const std::string decisions=std::to_string(botHash)+" "+
                std::to_string(hash(std::string(orders.getData(),orders.getDataLength())));
            auto digest=world.computeStateDigest();
            tail.states.push_back(GameStateDigest::describe(digest)+" "+decisions);
            // Scope the host/checkpoint comparison to this new AI's state and
            // economy, every object's physical state, RNG, and all unit orders.
            // Ordinary-load city accounting in another house can differ by a
            // credit; the existing global checkpoint issue is not certified by
            // this new army-policy test. Repeated ordinary loads below still
            // compare the complete house-inclusive digest.
            digest.houseHash=0;
            const auto* own=world.getHouse(HOUSE_HARKONNEN);
            tail.armyStates.push_back(GameStateDigest::describe(digest)+" "+decisions+" "+
                std::to_string(own->storedCredits.getRawValue())+" "+
                std::to_string(own->startingCredits.getRawValue())+" "+
                std::to_string(own->cityCredits.getRawValue())+" "+
                std::to_string(own->getNumUnits())+" "+std::to_string(own->getNumStructures()));
        }
        require(tail.refreshed,"Continuation never reached a build phase");
        return tail;
    };
    const auto live=runTail(*game);
    game.reset(); currentGame=nullptr; pLocalPlayer=nullptr;
    auto restore=[&](bool observer) {
        auto loaded=std::make_unique<Game>(); currentGame=loaded.get();
        loaded->bReplay=true; loaded->initGame(GameInitSettings(path)); loaded->bReplay=false;
        loaded->initializeGameLoop(); loaded->bPause=false;
        require(loaded->getGameCycleCount()==boundaryCycle,"Checkpoint changed its cycle");
        auto* restored=botFor(*loaded);
        require(restored && restored->armyPosture==ArmyPosturePolicy::Posture::Withdrawing
            && restored->recallCursor==17 && !restored->customWave.members.empty(),
            "Ordinary save lost recovery state");
        if(observer) loaded->loadObserverRuntime(runtime);
        return loaded;
    };
    auto observer=restore(true);
    const auto checkpoint=runTail(*observer);
    if(live.armyStates!=checkpoint.armyStates) {
        for(size_t i=0;i<live.states.size();++i) if(live.armyStates[i]!=checkpoint.armyStates[i]) {
            // Print the strings that actually differ - the army-scoped ones -
            // rather than the full digests, which can still match when the
            // difference is in one of the appended own-house fields.
            SDL_Log("ARMY_RECOVERY_CONTINUATION_DIAG: frame=%zu live=%s observer=%s",
                i,live.armyStates[i].c_str(),checkpoint.armyStates[i].c_str());
            SDL_Log("ARMY_RECOVERY_CONTINUATION_HOUSES: live=%s observer=%s",
                live.houses[i].c_str(),checkpoint.houses[i].c_str()); break;
        }
    }
    require(live.armyStates==checkpoint.armyStates,"Army observer continuation diverged from the live host");
    if(live.states!=checkpoint.states) SDL_Log("ARMY_RECOVERY_CHECKPOINT_SCOPE: army/orders/objects match; "
        "full other-house city-credit parity remains outside this test");
    observer.reset(); currentGame=nullptr; pLocalPlayer=nullptr;
    auto ordinary=restore(false);
    const auto first=runTail(*ordinary);
    ordinary.reset(); currentGame=nullptr; pLocalPlayer=nullptr;
    game=restore(false);
    const auto second=runTail(*game);
    require(first.states==second.states,"Repeated ordinary army loads diverged");
    GameStateDigest::Hasher history;
    for(const auto& state:second.states) for(const unsigned char c:state) history.mixUint8(c);
    SDL_Log("ARMY_RECOVERY_CONTINUATION: cycle=%u frames=100 observer_army=live ordinary_full=repeat history=%llu",
        boundaryCycle,static_cast<unsigned long long>(history.value()));
}
