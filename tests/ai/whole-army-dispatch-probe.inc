// Real-engine Custom Hard/Brutal whole-army dispatch and asset emergency
// defence. Included only by run-campaign-balance.py; never linked into the game.
//
// Everything here drives the production QuantBot entry points against real
// units, real structures and the real map. Nothing is simulated.
//
// The two behaviours under test:
//   1. One attack dispatch sends the WHOLE available combat army to engine
//      Hunt, wherever it stands and whatever it was previously doing.
//   2. An attack on a building or worker we own pulls an adequate local
//      response in a single event, including troops that were hunting.
void runWholeArmyDispatchProbe(Game& game) {
    auto require=[](bool ok,const char* why){
        if(!ok){SDL_Log("WHOLE_ARMY_FAIL: %s",why);throw std::runtime_error(why);}
    };
    auto* owner=game.getHouse(HOUSE_HARKONNEN);
    auto* enemy=game.getHouse(HOUSE_ATREIDES);
    require(owner && enemy,"Whole-army fixture missing houses");
    QuantBot* bot=nullptr;
    for(const auto& player:owner->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get()))bot=candidate;
    require(bot,"Whole-army fixture missing bot");

    game.gameType=GameType::CustomGame;
    bot->gameMode=QuantBot::GameMode::Custom;
    bot->supportMode=false;
    bot->manualUnitOrders.clear();
    bot->defenceAssignments.clear();
    bot->defenceResponseCycles.clear();
    bot->customWave.members.clear();
    bot->customWave.front=NONE_ID;
    bot->attrition.reset();
    bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
    bot->postureSince=0;
    getQuantBotConfig().recovery.enabled=true;

    // ---- Free ground -----------------------------------------------------
    Coord origin=Coord::Invalid();
    for(int y=2;y+26<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for(int x=2;x+46<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear=true;
            for(int dy=0;dy<26 && clear;++dy)for(int dx=0;dx<46 && clear;++dx) {
                const auto* tile=currentGameMap->getTile(x+dx,y+dy);
                if(tile->hasAGroundObject() || tile->isMountain())clear=false;
            }
            if(clear)origin=Coord(x,y);
        }
    require(origin.isValid(),"Whole-army fixture needs a clear 46x26 area");

    std::vector<UnitBase*> roster;
    std::vector<Uint32> fixtureStructureIds;
    const auto savedUnits=unitList;
    auto publish=[&] {
        unitList.clear();
        for(auto* unit:roster) unitList.push_back(unit);
    };
    auto spawn=[&](House* house,Uint32 item,Coord offset) {
        auto* unit=house->createUnit(item);
        require(unit,"Whole-army fixture unit creation failed");
        unit->deploy(origin+offset);
        unit->setVisible(owner->getTeamID(),true);
        unit->setVisible(enemy->getTeamID(),true);
        roster.push_back(unit);
        return unit;
    };
    auto retire=[&](UnitBase* unit) {
        unit->setHealth(0);
        roster.erase(std::remove(roster.begin(),roster.end(),unit),roster.end());
        publish();
    };
    auto place=[&](House* house,Uint32 item,Coord offset) {
        auto* structure=house->placeStructure(NONE_ID,item,origin.x+offset.x,origin.y+offset.y,true,true);
        if(structure) {
            structure->setVisible(owner->getTeamID(),true);
            structure->setVisible(enemy->getTeamID(),true);
            fixtureStructureIds.push_back(structure->getObjectID());
        }
        return structure;
    };
    auto cleanupStructures=[&] {
        for(const Uint32 id:fixtureStructureIds)
            if(auto* s=dynamic_cast<StructureBase*>(currentGame->getObjectManager().getObject(id)))
                if(s->getHealth()>0) s->setHealth(0);
    };

    try {
        // A real base, including the repair yard attack() requires, so a
        // "did not attack" result can never pass for the wrong reason.
        require(place(owner,Structure_ConstructionYard,Coord(4,2)),"Fixture yard placement failed");
        place(owner,Structure_Refinery,Coord(8,2));
        place(owner,Structure_HeavyFactory,Coord(4,6));
        place(owner,Structure_RepairYard,Coord(8,6));

        // ---- A geographically scattered army, in every state the old gates
        //      used to withhold ---------------------------------------------
        // Home block: ordinary idle reserve.
        std::vector<UnitBase*> home;
        for(int i=0;i<8;++i) home.push_back(spawn(owner,Unit_Tank,Coord(3+i%4,10+i/4)));
        // Remote colony production, nowhere near any staging slot.
        std::vector<UnitBase*> remote;
        for(int i=0;i<6;++i) remote.push_back(spawn(owner,Unit_Quad,Coord(40+i%3,22+i/3)));
        // Mid-map troops that are busy: targeting, moving under a forced
        // order, tracked from an older wave in Area Guard, and posted as a
        // colony guard.
        auto* targeting=spawn(owner,Unit_Tank,Coord(22,14));
        auto* moving=spawn(owner,Unit_Tank,Coord(24,14));
        auto* trackedGuard=spawn(owner,Unit_Tank,Coord(26,14));
        auto* colonyGuard=spawn(owner,Unit_Tank,Coord(28,14));
        auto* retreating=spawn(owner,Unit_Tank,Coord(30,14));
        // Excluded populations: a human order, a repair run, a transport, a
        // worker, a builder and an aircraft.
        auto* commanded=spawn(owner,Unit_Tank,Coord(3,14));
        auto* wreck=spawn(owner,Unit_Tank,Coord(4,14));
        auto* carryall=spawn(owner,Unit_Carryall,Coord(5,14));
        auto* worker=spawn(owner,Unit_Harvester,Coord(6,14));
        auto* builder=spawn(owner,Unit_MCV,Coord(7,14));
        auto* aircraft=spawn(owner,Unit_Ornithopter,Coord(8,14));
        // One weak observed enemy: enough for a front anchor, far too weak to
        // trigger the outnumbered veto, which has its own fixture.
        auto* prey=spawn(enemy,Unit_Trike,Coord(22,16));
        publish();

        bot->dangerUpdated=std::numeric_limits<Uint32>::max();
        bot->refreshTacticalDanger();
        bot->squadRallyLocation=origin+Coord(5,4);
        bot->protectedRallyCycle=std::numeric_limits<Uint32>::max();
        bot->protectedRally=origin+Coord(5,11);

        std::vector<UnitBase*> eligible;
        for(auto* unit:home) eligible.push_back(unit);
        for(auto* unit:remote) eligible.push_back(unit);
        for(auto* unit:{targeting,moving,trackedGuard,colonyGuard,retreating})
            eligible.push_back(unit);
        const std::vector<UnitBase*> excluded{commanded,wreck,carryall,worker,builder,aircraft};

        auto scatter=[&] {
            for(auto* unit:eligible) {
                bot->doSetAttackMode(unit,GUARD);
                bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);
                unit->setForced(false);
            }
            // Busy in four different ways. None of these may hold a troop back.
            bot->doSetAttackMode(targeting,AREAGUARD);
            bot->doAttackObject(targeting,prey,false);
            bot->doSetAttackMode(moving,AREAGUARD);
            bot->doMove2Pos(moving,origin.x+34,origin.y+20,true);
            bot->doSetAttackMode(trackedGuard,AREAGUARD);
            bot->customWave.members.clear();
            bot->customWave.members.insert(trackedGuard->getObjectID());
            bot->doSetAttackMode(retreating,RETREAT);
            bot->colonyGuardSet.clear();
            bot->colonyGuardSet[colonyGuard->getObjectID()]=origin+Coord(40,2);
            bot->colonyGuardCycle=currentGame->getGameCycleCount();
            // Excluded populations, set up every pass so one dispatch cannot
            // leave them in a state the next pass would accept.
            bot->manualUnitOrders.clear();
            bot->onHumanUnitOrder(commanded->getObjectID());
            wreck->setHealth(wreck->getMaxHealth()/10);
            for(auto* unit:excluded) {
                if(unit==commanded) continue;
                bot->doSetAttackMode(unit,unit->isAFlyingUnit() ? AREAGUARD : GUARD);
            }
        };

        for(const auto difficulty:{QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            bot->difficulty=difficulty;
            require(bot->recoveryActive(),"Whole-army dispatch is not active for this difficulty");
            scatter();
            require(bot->colonyGuardPosts().count(colonyGuard->getObjectID()),
                "Fixture could not post a colony guard");
            require(bot->reserveDamagedUnitForRepair(wreck),
                "Fixture repair-bound unit is not actually reserved for repair");
            require(!bot->orderableCombatUnit(commanded),
                "Fixture human order did not take the unit away from the bot");
            // A live enemy that has left the old defence district must not
            // keep a stale defender out of the whole-army launch.
            trackedGuard->setGuardPoint(origin+Coord(45,25));
            bot->defenceAssignments[trackedGuard->getObjectID()]=prey->getObjectID();
            require(!bot->activeDefenceAssignment(trackedGuard),
                "An ended ground contact still owns its defender");
            getQuantBotConfig().recovery.enabled=false;
            bot->militaryValueLimit=1;      // Readiness is the army itself here.
            bot->customWave.front=NONE_ID;
            const size_t trackedBefore=bot->customWave.members.size();
            bot->launchGroundHunt();
            int hunting=0, missing=0;
            for(auto* unit:eligible) {
                if(unit->getAttackMode()==HUNT) ++hunting;
                else { ++missing;
                    SDL_Log("WHOLE_ARMY_DIAG: left behind id=%u item=%u at=%d,%d mode=%d",
                        unit->getObjectID(),unit->getItemID(),
                        unit->getLocation().x,unit->getLocation().y,int(unit->getAttackMode()));
                }
            }
            SDL_Log("WHOLE_ARMY_DIAG: difficulty=%d eligible=%zu hunting=%d missing=%d "
                "tracked_before=%zu tracked_after=%zu",
                int(difficulty),eligible.size(),hunting,missing,
                trackedBefore,bot->customWave.members.size());
            require(missing==0,"One dispatch did not send the whole available army to Hunt");
            for(auto* unit:eligible)
                require(bot->customWave.members.count(unit->getObjectID()),
                    "A dispatched attacker was not tracked with the army");
            for(auto* unit:remote)
                require(unit->getAttackMode()==HUNT,
                    "A remote, unassembled reserve stayed at home");
            // And nothing the controller does not own went with them.
            require(commanded->getAttackMode()!=HUNT,"A human-ordered unit was dispatched");
            require(wreck->getAttackMode()!=HUNT,"A repair-bound unit was dispatched");
            require(carryall->getAttackMode()!=HUNT,"A transport was dispatched");
            require(worker->getAttackMode()!=HUNT,"A worker was dispatched");
            require(builder->getAttackMode()!=HUNT,"An MCV was dispatched");
            require(aircraft->getAttackMode()!=HUNT,
                "An aircraft was put on the ground hunt instead of its own planner");
            for(auto* unit:excluded)
                require(!bot->customWave.members.count(unit->getObjectID()),
                    "An excluded unit was tracked as part of the army");

            getQuantBotConfig().recovery.enabled=true;
            bot->defenceAssignments.clear();

            // No fresh drip while that army is out: the next pass has nothing
            // available and must not re-tag or re-order anybody.
            const auto membersAfter=bot->customWave.members;
            std::vector<std::pair<Uint32,bool>> forcedBefore;
            for(auto* unit:eligible) forcedBefore.push_back({unit->getObjectID(),unit->wasForced()});
            bot->launchGroundHunt();
            require(bot->customWave.members==membersAfter,
                "A repeated dispatch changed the tracked army");
            for(size_t i=0;i<eligible.size();++i)
                require(eligible[i]->getAttackMode()==HUNT
                    && eligible[i]->wasForced()==forcedBefore[i].second,
                    "A repeated dispatch re-ordered an attacker that was already hunting");

            // A reinforcement produced while the army is out waits for the
            // next whole-army launch rather than trickling after it.
            {
                auto* recruit=spawn(owner,Unit_Tank,Coord(5,12));
                publish();
                bot->doSetAttackMode(recruit,GUARD);
                recruit->setForced(false);
                bot->militaryValueLimit=1000000;   // Far above one tank.
                bot->launchGroundHunt();
                require(recruit->getAttackMode()!=HUNT,
                    "A single new recruit was dripped out after the army left");
                retire(recruit);
                bot->militaryValueLimit=1;
            }

            // Existing hunters keep engine Hunt through ordinary scans and
            // damage callbacks: the controller still does not micro them.
            {
                auto* attacker=eligible.front();
                const Coord destination=attacker->getDestination();
                bot->onDamage(attacker,1,prey->getObjectID());
                require(attacker->getAttackMode()==HUNT && !attacker->wasForced()
                    && attacker->getDestination()==destination,
                    "A damage callback replaced engine Hunt with tactical micro");
                bot->checkAllUnits();
                require(attacker->getAttackMode()==HUNT,
                    "An ordinary AI scan replaced an attacker's engine Hunt");
            }
        }

        // ---- The emergency: our own building under an overwhelming attack ---
        //
        // Scattered responders, most of them hunting, and a raid far larger
        // than the proportional reinforcement budget used to answer.
        {
            bot->difficulty=QuantBot::Difficulty::Brutal;
            scatter();
            bot->militaryValueLimit=1;
            bot->launchGroundHunt();
            int hunters=0;
            for(auto* unit:eligible) if(unit->getAttackMode()==HUNT) ++hunters;
            require(hunters==int(eligible.size()),"Emergency fixture army is not hunting");

            auto* asset=place(owner,Structure_WindTrap,Coord(14,10));
            require(asset,"Emergency fixture asset placement failed");
            std::vector<UnitBase*> assault;
            for(int i=0;i<14;++i) assault.push_back(spawn(enemy,Unit_SiegeTank,Coord(14+i%7,13+i/7)));
            publish();
            for(auto* raider:assault) raider->doAttackObject(asset,true);
            // A hunter engaged with something of its own, well away from the
            // contact: being busy elsewhere is not a reason to let the base die.
            auto* distraction=spawn(enemy,Unit_Trike,Coord(30,15));
            auto* engaged=eligible.back();
            engaged->setLocation(origin+Coord(30,14));
            publish();
            bot->doAttackObject(engaged,distraction,false);
            require(engaged->getTarget()==distraction,"Fixture could not engage a hunter elsewhere");

            bot->defenceAssignments.clear();
            bot->defenceResponseCycles.clear();
            bot->dangerUpdated=std::numeric_limits<Uint32>::max();
            bot->refreshTacticalDanger();
            bot->scrambleUnitsAndDefend(assault.front(),false,asset);

            int responders=0, huntersPulled=0;
            for(auto* unit:eligible)
                if(bot->defenceAssignments.count(unit->getObjectID())) {
                    ++responders;
                    if(unit->getAttackMode()==AREAGUARD) ++huntersPulled;
                }
            const int threat=14*currentGame->objectData.data[Unit_SiegeTank]
                [enemy->getHouseID()].price;
            SDL_Log("WHOLE_ARMY_DIAG: emergency responders=%d of %zu hunters_pulled=%d "
                "threat=%d band=%d",
                responders,eligible.size(),huntersPulled,threat,
                QuantBot::emergencyResponseRadius(threat));
            // Adequate, not token: the old proportional answer to this raid was
            // a handful of the nearest troops.
            require(responders>=8,"An overwhelming attack on our own building got a token response");
            require(huntersPulled>0,"The emergency could not recall any hunting troop");
            require(bot->defenceAssignments.count(engaged->getObjectID()),
                "A hunter busy elsewhere was left out of a base emergency");
            // Human orders, workers, transports and the air planner keep theirs.
            require(!bot->defenceAssignments.count(commanded->getObjectID()),
                "The emergency overrode a human order");
            require(!bot->defenceAssignments.count(worker->getObjectID())
                && !bot->defenceAssignments.count(carryall->getObjectID())
                && !bot->defenceAssignments.count(aircraft->getObjectID()),
                "The emergency commandeered a worker, a transport or the air planner's aircraft");
            require(!bot->defenceAssignments.count(wreck->getObjectID()),
                "The emergency took a unit reserved for repair");
            // The band is bounded: a raid on a worker stays a district answer.
            require(QuantBot::emergencyResponseRadius(0)==QuantBot::kEmergencyBandMin
                && QuantBot::emergencyResponseRadius(1000000)==QuantBot::kEmergencyBandMax,
                "The emergency response band is not bounded at both ends");

            for(auto* raider:assault) retire(raider);
            retire(distraction);
            asset->setHealth(0);
            bot->defenceAssignments.clear();

            // A worker under fire is the other asset the player watches die.
            // The ordinary scan rechecks it, not only the damage callback.
            {
                engaged->setLocation(origin+Coord(32,14));
                for(auto* unit:eligible) {
                    bot->doSetAttackMode(unit,AREAGUARD);
                    bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);
                    unit->setForced(false);
                }
                auto* poacher=spawn(enemy,Unit_Tank,Coord(7,13));
                publish();
                poacher->doAttackObject(worker,true);
                require(poacher->getTarget()==worker && poacher->isInWeaponRange(worker),
                    "Worker-defence fixture attacker is not actually shooting the harvester");
                bot->defenceAssignments.clear();
                bot->defenceResponseCycles.clear();
                bot->dangerUpdated=std::numeric_limits<Uint32>::max();
                bot->refreshTacticalDanger();
                bot->checkAllUnits();
                int workerDefenders=0;
                for(auto* unit:eligible) {
                    const auto entry=bot->defenceAssignments.find(unit->getObjectID());
                    if(entry!=bot->defenceAssignments.end()
                        && entry->second==poacher->getObjectID()) ++workerDefenders;
                }
                SDL_Log("WHOLE_ARMY_DIAG: worker defenders=%d band=%d",
                    workerDefenders,QuantBot::emergencyResponseRadius(
                        currentGame->objectData.data[Unit_Tank][enemy->getHouseID()].price));
                require(workerDefenders>0,
                    "An ordinary scan left a harvester under fire undefended");
                require(!bot->defenceAssignments.count(worker->getObjectID()),
                    "The worker was sent to defend itself");
                retire(poacher);
                bot->defenceAssignments.clear();
            }
        }

        // ---- Easy, Medium and campaign behaviour are untouched --------------
        {
            bot->defenceAssignments.clear();
            bot->customWave.members.clear();
            bot->colonyGuardSet.clear();
            bot->colonyGuardCycle=std::numeric_limits<Uint32>::max();
            bot->manualUnitOrders.clear();
            for(auto* unit:eligible) {
                unit->setHealth(unit->getMaxHealth());
                bot->doSetAttackMode(unit,GUARD);
                bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);
                unit->setForced(false);
            }
            bot->militaryValueLimit=999999;
            getQuantBotConfig().easy.attackForceMilitaryValueRatio=0.25f;
            getQuantBotConfig().medium.attackForceMilitaryValueRatio=0.40f;
            auto share=[&]{
                int n=0; for(auto* unit:eligible) if(unit->getAttackMode()==HUNT) ++n; return n;
            };
            for(const auto difficulty:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium}) {
                bot->difficulty=difficulty;
                require(!bot->recoveryActive(),"Whole-army dispatch leaked into Easy or Medium");
                for(auto* unit:eligible) {
                    bot->doSetAttackMode(unit,GUARD);
                    bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);
                    unit->setForced(false);
                }
                bot->customWave.members.clear();
                bot->launchGroundHunt();
                const int sent=share();
                SDL_Log("WHOLE_ARMY_DIAG: shared difficulty=%d sent=%d of %zu",
                    int(difficulty),sent,eligible.size());
                require(sent>0 && sent<int(eligible.size()),
                    "Easy or Medium stopped keeping its established home reserve");
                require(bot->customWave.members.empty(),
                    "Easy or Medium started tracking a Hard/Brutal army");
            }
            game.gameType=GameType::Campaign;
            bot->difficulty=QuantBot::Difficulty::Brutal;
            require(!bot->recoveryActive(),"Whole-army dispatch leaked into a campaign game");
            game.gameType=GameType::CustomGame;
        }
    } catch(...) {
        cleanupStructures(); unitList=savedUnits; throw;
    }
    cleanupStructures();
    unitList=savedUnits;
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=whole_army_dispatch outcome=passed");
}
