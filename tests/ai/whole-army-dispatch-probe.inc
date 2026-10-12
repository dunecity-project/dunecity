#include <structures/TurretBase.h>
#include <players/SimpleArmyPolicy.h>

// Real-engine Custom Hard/Brutal whole-army dispatch and asset emergency
// defence. Included only by run-campaign-balance.py; never linked into the game.
//
// Everything here drives the production QuantBot entry points against real
// units, real structures and the real map. Nothing is simulated.
//
// The two behaviours under test:
//   1. One attack dispatch sends the WHOLE available combat army to engine
//      Hunt, wherever it stands and whatever it was previously doing.
//   2. A hit on a building or harvester sends the next batch of the nearest
//      free ground troops, each with a forced attack on the attacker there
//      nearest to itself; a scrambled troop is locked while that attack runs.
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
    std::map<const UnitBase*,Uint32> rosterIds;
    std::vector<Uint32> fixtureStructureIds;
    const auto savedUnits=unitList;
    // Real combat can kill a fixture unit, and the engine then deletes it. Only
    // units the object manager still holds are published back into the list.
    auto publish=[&] {
        roster.erase(std::remove_if(roster.begin(),roster.end(),[&](UnitBase* unit) {
            return currentGame->getObjectManager().getObject(rosterIds.at(unit))!=unit;
        }),roster.end());
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
        rosterIds[unit]=unit->getObjectID();
        return unit;
    };
    auto retire=[&](UnitBase* unit) {
        const auto id=rosterIds.find(unit);
        if(id!=rosterIds.end() && currentGame->getObjectManager().getObject(id->second)==unit)
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
    auto tick=[&](int cycles){ for(int i=0;i<cycles;++i){game.processObjects();++game.gameCycleCount;} };
    auto range=[&](const ObjectBase* a,const ObjectBase* b){
        return blockDistance(a->getLocation(),b->getLocation()).lround();
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
        // than any one troop. The nearest free troops answer it with forced
        // attacks, up to the per-hit ceiling.
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
            // contact.
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

            auto value=[&](const ObjectBase* object) {
                const int price=currentGame->objectData.data[object->getItemID()][object->getOriginalHouseID()].price;
                return std::max(1,(FixPoint(price)*object->getHealth()/object->getMaxHealth()).lround());
            };
            int responders=0, attacking=0, sentValue=0, threat=0;
            for(auto* raider:assault) threat+=value(raider);
            for(auto* unit:eligible)
                if(bot->defenceAssignments.count(unit->getObjectID())) {
                    require(bot->defenceAssignments[unit->getObjectID()]==asset->getObjectID(),
                        "A base defender was not recorded against the building it defends");
                    ++responders; sentValue+=value(unit);
                    const auto* fight=unit->getTarget();
                    if(unit->wasForced() && fight
                        && (std::find(assault.begin(),assault.end(),fight)!=assault.end()
                            || unit->isInWeaponRange(fight)))
                        ++attacking;
                }
            int reachable=0, nearbyLeft=0;
            for(auto* unit:eligible) {
                if(unit==engaged) continue;
                const int distance=blockDistance(unit->getLocation(),asset->getClosestPoint(unit->getLocation())).lround();
                if(distance<=QuantBot::kDefenceScrambleRadius) ++reachable;
                if(distance<=QuantBot::kDefenceNearbyTiles && !bot->defenceAssignments.count(unit->getObjectID()))
                    ++nearbyLeft;
            }
            SDL_Log("WHOLE_ARMY_DIAG: emergency responders=%d of %zu attacking=%d value=%d "
                "threat=%d required=%d reachable=%d",
                responders,eligible.size(),attacking,sentValue,threat,
                SimpleArmyPolicy::responseValue(threat),reachable);
            // Adequate, not token: the per-hit ceiling against an overwhelming
            // raid, or every troop within reach when there are fewer.
            require(responders>=std::min(reachable,QuantBot::kDefenceScrambleBatchMax),
                "An overwhelming attack on our own building got a token response");
            require(nearbyLeft==0,"A free troop beside the attacked building was left standing");
            // Every one of them answers with a forced attack on an attacker, or
            // on an enemy already inside its own weapon range.
            require(responders>0 && attacking==responders,
                "A troop answering an attack on our own building was not given a forced attack on an attacker");
            require(!bot->defenceAssignments.count(engaged->getObjectID()),
                "The emergency pulled a troop out of its own fight elsewhere");
            // Human orders, workers, transports and the air planner keep theirs.
            require(!bot->defenceAssignments.count(commanded->getObjectID()),
                "The emergency overrode a human order");
            require(!bot->defenceAssignments.count(worker->getObjectID())
                && !bot->defenceAssignments.count(carryall->getObjectID())
                && !bot->defenceAssignments.count(aircraft->getObjectID()),
                "The emergency commandeered a worker, a transport or the air planner's aircraft");
            require(!bot->defenceAssignments.count(wreck->getObjectID()),
                "The emergency took a unit reserved for repair");
            // The district response band stays bounded at both ends.
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
                    if(entry!=bot->defenceAssignments.end() && entry->second==worker->getObjectID()
                        && unit->wasForced() && unit->getTarget()==poacher) ++workerDefenders;
                }
                SDL_Log("WHOLE_ARMY_DIAG: worker defenders=%d",workerDefenders);
                require(workerDefenders>0,
                    "An ordinary scan left a harvester under fire undefended");
                require(!bot->defenceAssignments.count(worker->getObjectID()),
                    "The worker was sent to defend itself");
                retire(poacher);
                bot->defenceAssignments.clear();
            }
        }

        // ---- A real attack on a building we own: the nearest troops are scrambled
        //
        // When a building this house owns is actually under attack, every hit
        // sends the next batch of the nearest free ground troops, never from
        // beyond the scramble radius, each with a forced attack on the attacker
        // at that building nearest to itself. A troop under another forced order
        // is not moved, and a scrambled troop is locked while its forced attack
        // runs: it is not scrambled again. On the way it fights an enemy that
        // comes inside its weapon range; an attacker that leaves the building is
        // not chased. Movement and damage are measured over real game cycles.
        {
            bot->colonyGuardSet.clear();
            bot->colonyGuardCycle=std::numeric_limits<Uint32>::max();
            bot->localSquadWithdraw=false;
            // No offensive dispatch can explain anything below: no wave is ever
            // ready at this limit, and the attack timer never expires.
            bot->militaryValueLimit=100000000;
            bot->attackTimer=std::numeric_limits<Sint32>::max();
            getQuantBotConfig().recovery.enabled=true;

            auto* farTank=spawn(owner,Unit_Tank,Coord(45,25));
            auto* farQuad=spawn(owner,Unit_Quad,Coord(44,25));
            auto* farLauncher=spawn(owner,Unit_Launcher,Coord(45,23));
            std::vector<UnitBase*> army=eligible;
            for(auto* unit:{farTank,farQuad,farLauncher}) army.push_back(unit);
            publish();

            auto* yard=dynamic_cast<StructureBase*>(currentGame->getObjectManager()
                .getObject(fixtureStructureIds.front()));
            require(yard && yard->getItemID()==Structure_ConstructionYard && yard->getHealth()>0,
                "Base-scramble fixture lost its construction yard");
            UnitBase* raider=spawn(enemy,Unit_Trike,Coord(7,3));
            publish();

            // Where every troop starts each scenario. Troops now really fight, so
            // one can die between scenarios; dead troops are dropped, never touched.
            std::map<UnitBase*,Coord> homeOf;
            for(auto* unit:eligible) homeOf[unit]=unit->getLocation();
            homeOf[farTank]=origin+Coord(45,25);
            homeOf[farQuad]=origin+Coord(44,25);
            homeOf[farLauncher]=origin+Coord(45,23);
            auto alive=[&](UnitBase* unit) {
                const auto id=rosterIds.find(unit);
                return id!=rosterIds.end() && currentGame->getObjectManager().getObject(id->second)==unit;
            };
            auto reset=[&](bool mixed) {
                // A dead troop's address can be reused by a later fixture unit,
                // possibly an enemy; ownership catches that.
                army.erase(std::remove_if(army.begin(),army.end(),[&](UnitBase* unit) {
                    return !alive(unit) || unit->getOwner()!=owner; }),army.end());
                bot->defenceAssignments.clear();
                bot->defenceResponseCycles.clear();
                bot->customWave.members.clear();
                for(auto* unit:army) {
                    unit->setHealth(unit->getMaxHealth());
                    unit->setLocation(homeOf.at(unit));
                    bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);
                    bot->doSetAttackMode(unit,GUARD);
                    unit->setForced(false);
                }
                if(mixed) {
                    // Retreating, posted on Area Guard with prey of its own, and
                    // already hunting: none of these is a forced order.
                    if(alive(retreating)) bot->doSetAttackMode(retreating,RETREAT);
                    if(alive(targeting)) {
                        bot->doSetAttackMode(targeting,AREAGUARD);
                        if(alive(prey)) bot->doAttackObject(targeting,prey,false);
                    }
                    for(auto* unit:remote) if(alive(unit)) bot->doSetAttackMode(unit,HUNT);
                    bot->colonyGuardSet.clear();
                    if(alive(colonyGuard))
                        bot->colonyGuardSet[colonyGuard->getObjectID()]=origin+Coord(40,2);
                    bot->colonyGuardCycle=currentGame->getGameCycleCount();
                }
                bot->manualUnitOrders.clear();
                bot->onHumanUnitOrder(commanded->getObjectID());
                wreck->setHealth(wreck->getMaxHealth()/10);
                require(bot->reserveDamagedUnitForRepair(wreck),
                    "Base-scramble fixture repair-bound unit is not actually reserved");
                bot->dangerUpdated=std::numeric_limits<Uint32>::max();
                bot->refreshTacticalDanger();
                publish();
            };
            auto invade=[&](StructureBase* building,Coord at) {
                raider->setLocation(at);
                raider->setHealth(raider->getMaxHealth());
                raider->doSetAttackMode(AREAGUARD);
                raider->doAttackObject(building,true);
                publish();
                require(raider->getTarget()==building && raider->isInWeaponRange(building),
                    "Base-scramble fixture raider is not actually attacking the building");
            };
            auto protectedUnitsKept=[&](const char* why) {
                for(auto* unit:excluded)
                    require(!bot->defenceAssignments.count(unit->getObjectID()),why);
                for(auto* unit:{commanded,wreck,worker,builder})
                    require(unit->getAttackMode()!=HUNT,why);
            };
            auto distanceTo=[&](const UnitBase* unit,const ObjectBase* building) {
                return blockDistance(unit->getLocation(),building->getClosestPoint(unit->getLocation())).lround();
            };
            auto value=[&](const ObjectBase* object) {
                const int price=currentGame->objectData.data[object->getItemID()][object->getOriginalHouseID()].price;
                return std::max(1,(FixPoint(price)*object->getHealth()/object->getMaxHealth()).lround());
            };
            // The building's threat as the bot measures it: visible enemy ground
            // fighters within the contact distance of the building.
            auto threatAt=[&](const ObjectBase* building) {
                int threat=0;
                for(const auto* unit:unitList)
                    if(unit->getOwner()->getTeamID()!=owner->getTeamID() && unit->getHealth()>0
                        && unit->isActive() && unit->canAttack() && !unit->isAFlyingUnit()
                        && distanceTo(unit,building)<=QuantBot::kBaseScrambleContactTiles)
                        threat+=value(unit);
                return threat;
            };
            auto sentTo=[&](const ObjectBase* building) {
                std::vector<UnitBase*> sent;
                for(auto* unit:army)
                    if(bot->defenceAssignments.count(unit->getObjectID())
                        && bot->defenceAssignments[unit->getObjectID()]==building->getObjectID())
                        sent.push_back(unit);
                return sent;
            };
            // The attackers at a building as the bot sees them: visible enemy
            // ground fighters within the contact distance of it.
            auto attackersAt=[&](const ObjectBase* building) {
                std::vector<const UnitBase*> attackers;
                for(const auto* unit:unitList)
                    if(unit->getOwner()->getTeamID()!=owner->getTeamID() && unit->getHealth()>0
                        && unit->isActive() && unit->canAttack() && !unit->isAFlyingUnit()
                        && distanceTo(unit,building)<=QuantBot::kBaseScrambleContactTiles)
                        attackers.push_back(unit);
                return attackers;
            };
            // Every scramble: a forced attack on an enemy inside the troop's
            // weapon range, else on the attacker at the building nearest to it, the nearest available troops first, at
            // least a batch (or every available troop) and never more than the
            // per-hit ceiling from beyond the nearby ring, every free troop
            // inside it, all within the scramble radius.
            auto nearestScramble=[&](const ObjectBase* building,const std::set<Uint32>& unavailable,
                                     const char* why) {
                const auto sent=sentTo(building);
                const auto attackers=attackersAt(building);
                const int threat=threatAt(building);
                int sentValue=0, farthestSent=0;
                for(auto* unit:sent) {
                    sentValue+=value(unit);
                    farthestSent=std::max(farthestSent,distanceTo(unit,building));
                    const auto* fight=unit->getTarget();
                    const UnitBase* nearest=nullptr;
                    for(const auto* attacker:attackers)
                        if(unit->canAttack(attacker) && (!nearest
                            || blockDistance(unit->getLocation(),attacker->getLocation())
                                < blockDistance(unit->getLocation(),nearest->getLocation())))
                            nearest=attacker;
                    // A launcher's spacing step is left to finish.
                    if((unit->getItemID()==Unit_Launcher || unit->getItemID()==Unit_EliteLauncher)
                        && unit->wasForced() && !fight && unit->getDestination().isValid()) continue;
                    // An enemy already inside the troop's weapon range comes
                    // first; otherwise the attacker at the building nearest to it.
                    if(unit->wasForced() && fight && unit->isInWeaponRange(fight)
                        && fight->getOwner()->getTeamID()!=owner->getTeamID()) continue;
                    if(unit->wasForced() && fight && nearest
                        && blockDistance(unit->getLocation(),fight->getLocation())
                            ==blockDistance(unit->getLocation(),nearest->getLocation())) continue;
                    SDL_Log("WHOLE_ARMY_DIAG: base scramble order id=%u item=%d mode=%d forced=%d target=%u nearest=%u dest=%d,%d",
                        unit->getObjectID(),unit->getItemID(),int(unit->getAttackMode()),int(unit->wasForced()),
                        fight?fight->getObjectID():0,nearest?nearest->getObjectID():0,
                        unit->getDestination().x,unit->getDestination().y);
                    require(false,why);
                }
                // A troop already shooting at an enemy keeps that fight.
                auto fighting=[&](const UnitBase* unit) {
                    const auto* fight=unit->getTarget();
                    return fight && fight->getHealth()>0 && fight->getOwner()->getTeamID()!=owner->getTeamID()
                        && blockDistance(unit->getLocation(),fight->getClosestPoint(unit->getLocation())).lround()
                            <=unit->getWeaponRange()+1;
                };
                int nearerLeft=0, leftInRadius=0, nearbyLeft=0, sentFromAfar=0;
                for(auto* unit:sent)
                    if(distanceTo(unit,building)>QuantBot::kDefenceNearbyTiles) ++sentFromAfar;
                for(auto* unit:army)
                    // A troop under its own forced order (a spacing step) is not moved.
                    if(alive(unit) && unit->getOwner()==owner
                        && !unavailable.count(unit->getObjectID()) && !fighting(unit) && !unit->wasForced()
                        && std::find(sent.begin(),sent.end(),unit)==sent.end()) {
                        if(distanceTo(unit,building)<farthestSent) {
                            ++nearerLeft;
                            SDL_Log("WHOLE_ARMY_DIAG: nearer left id=%u item=%d mode=%d forced=%d target=%u dist=%d assigned=%d",
                                unit->getObjectID(),unit->getItemID(),int(unit->getAttackMode()),int(unit->wasForced()),
                                unit->getTarget()?unit->getTarget()->getObjectID():0,distanceTo(unit,building),
                                int(bot->defenceAssignments.count(unit->getObjectID())));
                        }
                        if(distanceTo(unit,building)<=QuantBot::kDefenceScrambleRadius) ++leftInRadius;
                        if(distanceTo(unit,building)<=QuantBot::kDefenceNearbyTiles) ++nearbyLeft;
                    }
                SDL_Log("WHOLE_ARMY_DIAG: base scramble building=%u sent=%zu value=%d threat=%d "
                    "required=%d farthest=%d nearer_left=%d left_in_radius=%d",
                    building->getObjectID(),sent.size(),sentValue,threat,
                    SimpleArmyPolicy::responseValue(threat),farthestSent,nearerLeft,leftInRadius);
                require(!sent.empty(),why);
                require(int(sent.size())>=QuantBot::kDefenceScrambleBatch || leftInRadius==0,
                    "A base scramble sent less than a batch while troops were available");
                require(sentFromAfar<=QuantBot::kDefenceScrambleBatchMax,
                    "A base scramble sent more than the per-hit ceiling from afar");
                require(nearbyLeft==0,"A free troop beside the attacked building was left standing");
                require(farthestSent<=QuantBot::kDefenceScrambleRadius,
                    "A base scramble pulled a troop from beyond the scramble radius");
                require(nearerLeft==0,"A base scramble passed over a nearer free troop");
                return sent;
            };

            // 1. The damage callback, in every difficulty and every posture the
            //    army machine can be in - including the ones that suppress all
            //    offensive dispatch - and from a mixed set of unit states.
            for(const auto difficulty:{QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal})
                for(const auto posture:{ArmyPosturePolicy::Posture::Offensive,
                                        ArmyPosturePolicy::Posture::Withdrawing,
                                        ArmyPosturePolicy::Posture::Recovering}) {
                    bot->difficulty=difficulty;
                    bot->armyPosture=posture;
                    bot->postureSince=currentGame->getGameCycleCount();
                    reset(true);
                    invade(yard,origin+Coord(7,3));
                    bot->onDamage(yard,40,raider->getObjectID());
                    nearestScramble(yard,{},"A real attack on our own building did not scramble its nearest troops");
                    require(!bot->defenceAssignments.count(farTank->getObjectID()),
                        "A small raid on the base pulled the far reserve across the map");
                    protectedUnitsKept("The base scramble commandeered a protected unit");
                }

            // 2. A troop under a forced order is not moved; the next nearest goes.
            {
                bot->difficulty=QuantBot::Difficulty::Brutal;
                bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
                reset(false);
                UnitBase* nearest=nullptr;
                for(auto* unit:army)
                    if(!nearest || distanceTo(unit,yard)<distanceTo(nearest,yard)) nearest=unit;
                bot->doMove2Pos(nearest,origin.x+20,origin.y+20,true);
                const Coord forcedTo=nearest->getDestination();
                invade(yard,origin+Coord(7,3));
                bot->onDamage(yard,40,raider->getObjectID());
                require(!bot->defenceAssignments.count(nearest->getObjectID())
                    && nearest->wasForced() && nearest->getDestination()==forcedTo,
                    "A base scramble moved a troop that was under a forced order");
                nearestScramble(yard,{nearest->getObjectID()},"A forced troop left the base without an answer");
            }

            // 2b. A wave member on a forced march home walks blind past the
            //     attack; the scramble takes it. A short forced step stays.
            {
                bot->difficulty=QuantBot::Difficulty::Brutal;
                bot->armyPosture=ArmyPosturePolicy::Posture::Withdrawing;
                reset(false);
                UnitBase* marcher=nullptr;
                UnitBase* stepper=nullptr;
                for(auto* unit:army) {
                    if(!marcher || distanceTo(unit,yard)<distanceTo(marcher,yard)) marcher=unit;
                }
                for(auto* unit:army)
                    if(unit!=marcher && (!stepper || distanceTo(unit,yard)<distanceTo(stepper,yard))) stepper=unit;
                bot->customWave.members.insert(marcher->getObjectID());
                bot->customWave.members.insert(stepper->getObjectID());
                bot->doMove2Pos(marcher,origin.x+44,origin.y+24,true);
                bot->doMove2Pos(stepper,stepper->getX()+2,stepper->getY(),true);
                require(marcher->wasForced() && !marcher->getTarget() && stepper->wasForced(),
                    "Wave-march fixture could not force its walkers");
                invade(yard,origin+Coord(7,3));
                bot->onDamage(yard,40,raider->getObjectID());
                require(bot->defenceAssignments.count(marcher->getObjectID())
                    && bot->defenceAssignments[marcher->getObjectID()]==yard->getObjectID()
                    && marcher->wasForced() && marcher->getTarget()
                    && marcher->getTarget()->getOwner()->getTeamID()!=owner->getTeamID(),
                    "A wave member marching past an attack on its base was not scrambled");
                require(!bot->defenceAssignments.count(stepper->getObjectID()),
                    "A wave member's short forced step was taken by the scramble");
                bot->customWave.members.clear();
            }

            // 3. A scrambled troop is not scrambled again: a second attack on
            //    another building is answered by other troops.
            {
                bot->difficulty=QuantBot::Difficulty::Brutal;
                bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
                reset(false);
                invade(yard,origin+Coord(7,3));
                bot->onDamage(yard,40,raider->getObjectID());
                const auto first=nearestScramble(yard,{},"The first attack was not answered");
                std::map<Uint32,const ObjectBase*> firstTarget;
                std::set<Uint32> firstIds;
                for(auto* unit:first) {
                    firstTarget[unit->getObjectID()]=unit->getTarget();
                    firstIds.insert(unit->getObjectID());
                }
                auto* otherColony=place(owner,Structure_ZoneResidential,Coord(42,20));
                require(otherColony,"Second invasion fixture could not place its colony");
                auto* other=spawn(enemy,Unit_SiegeTank,Coord(40,22));
                other->doAttackObject(otherColony,true);
                publish();
                require(other->isInWeaponRange(otherColony),
                    "Second invasion fixture is outside its weapon range");
                bot->defenceResponseCycles.clear();
                bot->scrambleUnitsAndDefend(other,false,otherColony);
                for(auto* unit:first)
                    require(bot->defenceAssignments[unit->getObjectID()]==yard->getObjectID()
                        && unit->getTarget()==firstTarget[unit->getObjectID()] && unit->wasForced(),
                        "A second base attack re-scrambled a troop already scrambled");
                nearestScramble(otherColony,firstIds,"The second building got no answer of its own");
                // A second hit on the first building takes the next batch, not
                // the troops already locked on their forced attacks.
                bot->defenceResponseCycles.clear();
                bot->onDamage(yard,40,raider->getObjectID());
                for(auto* unit:first)
                    require(bot->defenceAssignments[unit->getObjectID()]==yard->getObjectID()
                        && unit->getTarget()==firstTarget[unit->getObjectID()] && unit->wasForced(),
                        "A second hit on the same building re-scrambled a troop already scrambled");
                retire(other);
                otherColony->setHealth(0);
                publish();
            }

            // 4. Real cycles: the nearest troops close on the raider under their
            //    forced attack and damage it; they stay scrambled while the
            //    raider is there and are released once the raider is dead.
            {
                bot->difficulty=QuantBot::Difficulty::Brutal;
                bot->armyPosture=ArmyPosturePolicy::Posture::Withdrawing;
                bot->postureSince=currentGame->getGameCycleCount();
                reset(false);
                invade(yard,origin+Coord(7,3));
                bot->onDamage(yard,40,raider->getObjectID());
                const auto sent=nearestScramble(yard,{},"The raid on the yard was not answered");
                const FixPoint invaderHealth=raider->getHealth();
                int ticks=0;
                while(ticks<3000 && raider->getHealth()>=invaderHealth) {
                    bot->checkAllUnits();
                    tick(30);
                    ticks+=30;
                    yard->setHealth(yard->getMaxHealth());
                }
                SDL_Log("WHOLE_ARMY_DIAG: base scramble combat ticks=%d health=%d->%d",
                    ticks,invaderHealth.lround(),raider->getHealth().lround());
                require(raider->getHealth()<invaderHealth,
                    "The scrambled troops never actually damaged the raider");
                bot->checkAllUnits();
                if(raider->getHealth()>0)
                    for(auto* unit:sent)
                        require(bot->defenceAssignments.count(unit->getObjectID()),
                            "A defender was released while the raider was still at the building");
                retire(raider);
                publish();
                for(int i=0;i<10;++i) { tick(60); bot->checkAllUnits(); }
                for(auto* unit:sent) {
                    if(!bot->defenceAssignments.count(unit->getObjectID())) continue;
                    SDL_Log("WHOLE_ARMY_DIAG: not released id=%u mode=%d forced=%d target=%u",
                        unit->getObjectID(),int(unit->getAttackMode()),int(unit->wasForced()),
                        unit->getTarget()?unit->getTarget()->getObjectID():0);
                    require(false,"A defender kept its scramble after the raider died");
                }
                raider=spawn(enemy,Unit_Trike,Coord(7,3));
                publish();
            }

            // 5. Hunt overrides the walk, for enemies it meets: the nearest enemy
            //    on the map, far off the route, does not divert a hunting troop,
            //    but an enemy that comes within its range on the way does.
            {
                bot->difficulty=QuantBot::Difficulty::Brutal;
                bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
                reset(false);
                retire(raider);
                // Mid-map, about twenty tiles from the yard: the probe advances
                // objects only, so it keeps the walk to what that path service
                // completes. The prey trike to the south is the nearest enemy.
                farTank->setLocation(origin+Coord(25,8));
                publish();
                const Coord home=yard->getClosestPoint(farTank->getLocation());
                bot->doHuntToward(farTank,home.x,home.y);
                require(farTank->getAttackMode()==HUNT && farTank->isHuntingToward() && !farTank->wasForced(),
                    "doHuntToward did not issue an unforced Hunt walk");
                // Whatever Hunt would pick on the whole map is out of this troop's
                // reach and off its route.
                const ObjectBase* nearestEnemy=farTank->findTarget();
                require(nearestEnemy!=nullptr && blockDistance(farTank->getLocation(),
                        nearestEnemy->getClosestPoint(farTank->getLocation())).lround()>farTank->huntTowardRange(),
                    "Hunt-toward fixture has an enemy within reach at the start");
                SDL_Log("WHOLE_ARMY_DIAG: hunt toward nearest enemy=%u item=%u",
                    nearestEnemy->getObjectID(),nearestEnemy->getItemID());
                const int start=distanceTo(farTank,yard);
                for(int i=0;i<10;++i) tick(30);
                SDL_Log("WHOLE_ARMY_DIAG: hunt toward walk start=%d now=%d target=%u toward=%d dest=%d,%d home=%d,%d mode=%d",
                    start,distanceTo(farTank,yard),farTank->getTarget()?farTank->getTarget()->getObjectID():0,
                    int(farTank->isHuntingToward()),farTank->getDestination().x,farTank->getDestination().y,
                    home.x,home.y,int(farTank->getAttackMode()));
                require(farTank->getTarget()!=nearestEnemy && farTank->isHuntingToward()
                    && farTank->getDestination()==home && distanceTo(farTank,yard)<start,
                    "A hunting troop left its walk for the nearest enemy across the map");
                // An enemy that turns up on the way: a free tile 2-3 tiles away.
                Coord met=Coord::Invalid();
                for(int dy=-3;dy<=3 && met.isInvalid();++dy)
                    for(int dx=-3;dx<=3 && met.isInvalid();++dx) {
                        const Coord at=farTank->getLocation()+Coord(dx,dy);
                        if(std::max(std::abs(dx),std::abs(dy))<2 || !currentGameMap->tileExists(at)) continue;
                        const auto* tile=currentGameMap->getTile(at);
                        if(!tile->hasAGroundObject() && !tile->isMountain() && !tile->isRock()) met=at;
                    }
                if(met.isInvalid())
                    for(int dy=-3;dy<=3 && met.isInvalid();++dy)
                        for(int dx=-3;dx<=3 && met.isInvalid();++dx) {
                            const Coord at=farTank->getLocation()+Coord(dx,dy);
                            if(std::max(std::abs(dx),std::abs(dy))<2 || !currentGameMap->tileExists(at)) continue;
                            const auto* tile=currentGameMap->getTile(at);
                            if(!tile->hasAGroundObject() && !tile->isMountain()) met=at;
                        }
                require(met.isValid(),"Hunt-toward fixture found no free tile for the enemy met on the way");
                auto* blocker=spawn(enemy,Unit_Trike,met-origin);
                publish();
                int ticks=0;
                while(ticks<600 && farTank->getTarget()!=blocker) { tick(10); ticks+=10; }
                SDL_Log("WHOLE_ARMY_DIAG: hunt toward met enemy after %d ticks target=%u toward=%d",
                    ticks,farTank->getTarget()?farTank->getTarget()->getObjectID():0,int(farTank->isHuntingToward()));
                require(farTank->getTarget()==blocker && !farTank->isHuntingToward(),
                    "A hunting troop walked past an enemy it met on the way");
                retire(blocker);
                raider=spawn(enemy,Unit_Trike,Coord(7,3));
                publish();
            }

            // 5b. Leashed: a defender lets an attacker go once it has left the
            //     building, and fights an enemy that comes inside its weapon
            //     range on the way instead of walking past it.
            {
                bot->difficulty=QuantBot::Difficulty::Brutal;
                bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
                reset(false);
                invade(yard,origin+Coord(7,3));
                bot->onDamage(yard,40,raider->getObjectID());
                auto sent=nearestScramble(yard,{},"The leash fixture raid was not answered");
                UnitBase* walker=nullptr;
                for(auto* unit:sent)
                    if(!unit->isInWeaponRange(raider)
                        && (!walker || distanceTo(unit,yard)>distanceTo(walker,yard))) walker=unit;
                require(walker!=nullptr,"Leash fixture has no defender still closing in");
                // Still on its way, well away from the yard.
                auto freeTile=[&](Coord around) {
                    for(int r=0;r<=4;++r)
                        for(int dy=-r;dy<=r;++dy)
                            for(int dx=-r;dx<=r;++dx) {
                                const Coord at=around+Coord(dx,dy);
                                if(!currentGameMap->tileExists(at)) continue;
                                const auto* tile=currentGameMap->getTile(at);
                                if(!tile->hasAGroundObject() && !tile->isMountain()) return at;
                            }
                    return Coord::Invalid();
                };
                const Coord away=freeTile(origin+Coord(20,20));
                require(away.isValid(),"Leash fixture found no free tile away from the yard");
                walker->setLocation(away);
                publish();
                // An enemy beside the walker.
                Coord beside=Coord::Invalid();
                for(int dy=-1;dy<=1 && beside.isInvalid();++dy)
                    for(int dx=-1;dx<=1 && beside.isInvalid();++dx) {
                        const Coord at=walker->getLocation()+Coord(dx,dy);
                        if((dx==0 && dy==0) || !currentGameMap->tileExists(at)) continue;
                        const auto* tile=currentGameMap->getTile(at);
                        if(!tile->hasAGroundObject() && !tile->isMountain()) beside=at;
                    }
                require(beside.isValid(),"Leash fixture found no free tile beside the walker");
                auto* ambusher=spawn(enemy,Unit_Trike,beside-origin);
                publish();
                require(walker->isInWeaponRange(ambusher)
                    && distanceTo(ambusher,yard)>QuantBot::kBaseScrambleContactTiles,
                    "Leash fixture ambusher is not inside the walker's range away from the yard");
                bot->checkAllUnits();
                SDL_Log("WHOLE_ARMY_DIAG: leash walker target=%u raider=%u ambusher=%u forced=%d assigned=%u yard=%u in_range=%d mode=%d",
                    walker->getTarget()?walker->getTarget()->getObjectID():0,raider->getObjectID(),
                    ambusher->getObjectID(),int(walker->wasForced()),
                    bot->defenceAssignments.count(walker->getObjectID())?bot->defenceAssignments[walker->getObjectID()]:0,
                    yard->getObjectID(),int(walker->isInWeaponRange(ambusher)),int(walker->getAttackMode()));
                require(walker->getTarget()==ambusher && walker->wasForced()
                    && bot->defenceAssignments[walker->getObjectID()]==yard->getObjectID(),
                    "A defender walked past an enemy inside its weapon range");
                retire(ambusher);
                publish();
                // The raider leaves the yard for the far side of the fixture.
                bot->checkAllUnits();
                raider->setLocation(origin+Coord(44,20));
                raider->setTarget(nullptr);
                raider->doSetAttackMode(STOP);
                publish();
                bot->checkAllUnits();
                for(auto* unit:sent) {
                    if(!alive(unit) || unit->isInWeaponRange(raider)) continue;
                    require(unit->getTarget()!=raider && !bot->defenceAssignments.count(unit->getObjectID()),
                        "A defender chased an attacker that had left the building");
                }
                retire(raider);
                raider=spawn(enemy,Unit_Trike,Coord(7,3));
                publish();
            }

            // 6. The ordinary scan, with no damage callback at all, against an
            //    outlying colony lot of each R/C/I kind rather than the base.
            {
                int lotRow=8;
                for(const Uint32 lot:{Structure_ZoneResidential,Structure_ZoneCommercial,
                                      Structure_ZoneIndustrial}) {
                    bot->difficulty=QuantBot::Difficulty::Hard;
                    bot->armyPosture=ArmyPosturePolicy::Posture::Recovering;
                    auto* colony=place(owner,lot,Coord(42,lotRow));
                    require(colony,"Base-scramble fixture could not place a remote colony lot");
                    reset(true);
                    invade(colony,origin+Coord(41,lotRow+2));
                    bot->checkAllUnits();
                    nearestScramble(colony,{},"An ordinary scan did not answer a colony under attack");
                    protectedUnitsKept("The colony scramble commandeered a protected unit");
                    colony->setHealth(0);
                    lotRow+=4;
                }
            }
            // 7. What must NOT scramble a house: an enemy that is merely
            //    standing somewhere, one that has stopped attacking, and a dead
            //    contact. The attack has to be real and current.
            {
                bot->difficulty=QuantBot::Difficulty::Brutal;
                bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
                reset(false);
                auto* loiterer=spawn(enemy,Unit_Tank,Coord(36,20));
                publish();
                loiterer->doSetAttackMode(STOP);
                loiterer->setTarget(nullptr);
                bot->scrambleUnitsAndDefend(loiterer);
                bot->checkAllUnits();
                for(auto* unit:army)
                    require(!bot->baseDefenceAssignment(unit),
                        "An idle distant enemy scrambled the whole house");

                // Standing on the base but no longer shooting it.
                reset(false);
                invade(yard,origin+Coord(7,3));
                raider->setTarget(nullptr);
                raider->doSetAttackMode(STOP);
                bot->defenceAssignments.clear();
                bot->defenceResponseCycles.clear();
                bot->checkAllUnits();
                for(auto* unit:army)
                    require(!bot->baseDefenceAssignment(unit),
                        "An attacker that had stopped shooting scrambled the whole house");

                // A dead contact, handed in through the damage callback.
                reset(false);
                invade(yard,origin+Coord(7,3));
                raider->setHealth(0);
                bot->defenceAssignments.clear();
                bot->defenceResponseCycles.clear();
                bot->scrambleUnitsAndDefend(raider,false,yard);
                for(auto* unit:army)
                    require(!bot->baseDefenceAssignment(unit),
                        "A dead contact scrambled the whole house");
                retire(loiterer);
            }

            // 8. A harvester under fire is scrambled for like a building: the
            //    nearest batch, forced on its attacker, and not the far reserve.
            {
                bot->difficulty=QuantBot::Difficulty::Brutal;
                bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
                reset(false);
                retire(raider);     // Nothing is attacking a building any more.
                auto* poacher=spawn(enemy,Unit_Tank,Coord(7,13));
                publish();
                poacher->doSetAttackMode(AREAGUARD);
                poacher->doAttackObject(worker,true);
                require(poacher->getTarget()==worker && poacher->isInWeaponRange(worker),
                    "Harvester fixture attacker is not actually shooting the harvester");
                bot->defenceAssignments.clear();
                bot->defenceResponseCycles.clear();
                bot->checkAllUnits();
                int answered=0;
                for(auto* unit:army)
                    if(bot->defenceAssignments.count(unit->getObjectID())) {
                        require(bot->defenceAssignments[unit->getObjectID()]==worker->getObjectID()
                            && unit->wasForced() && unit->getTarget()==poacher,
                            "A harvester defender was not forced onto the harvester's attacker");
                        ++answered;
                    }
                SDL_Log("WHOLE_ARMY_DIAG: harvester response answered=%d of %zu far=%d",
                    answered,army.size(),range(farTank,poacher));
                require(answered>0,"A harvester under fire got no answer at all");
                require(!bot->defenceAssignments.count(farTank->getObjectID()) && !farTank->wasForced(),
                    "A harvester raid mobilised a reserve on the far side of the map");
                require(!bot->defenceAssignments.count(worker->getObjectID()),
                    "The harvester was sent to defend itself");
                retire(poacher);
            }

            for(auto* unit:{farTank,farQuad,farLauncher}) retire(unit);
            // Troops killed in these real fights are gone for the later scenarios.
            eligible.erase(std::remove_if(eligible.begin(),eligible.end(),
                [&](UnitBase* unit) { return !alive(unit); }),eligible.end());
            bot->defenceAssignments.clear();
            bot->customWave.members.clear();
            bot->colonyGuardSet.clear();
            bot->colonyGuardCycle=std::numeric_limits<Uint32>::max();
            bot->localSquadWithdraw=false;
        }

        // ---- Launchers in the waves, and the rescue handing them back -------
        //
        // "Launchers are never in the attack waves" has no type exclusion
        // behind it: launchGroundHunt() filters on ownership, health, activity,
        // human orders, workers, saboteurs and sandworms, and launchers are
        // none of those. The one mechanism that claims them specifically is the
        // anti-air rescue, which sorts launchers first. So the contract is:
        // every free launcher is dispatched; a launcher on a LIVE air contact
        // is withheld; and when that contact dies or breaks off the launcher is
        // released and joins the next wave. The ground_hunt record reports each
        // of those populations so a live match no longer needs inference.
        {
            bot->defenceAssignments.clear();
            bot->defenceResponseCycles.clear();
            bot->customWave.members.clear();
            bot->colonyGuardSet.clear();
            bot->colonyGuardCycle=std::numeric_limits<Uint32>::max();
            bot->manualUnitOrders.clear();
            bot->localSquadWithdraw=false;
            bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
            bot->attackTimer=0;
            getQuantBotConfig().recovery.enabled=false;
            bot->difficulty=QuantBot::Difficulty::Brutal;
            bot->militaryValueLimit=1;

            // Launchers far enough from the colony that answering the rescue is
            // a journey: a defender that can already shoot is not what empties
            // a wave, so the bound is about the ones that have to march.
            std::vector<UnitBase*> guns;
            for(int i=0;i<6;++i) guns.push_back(spawn(owner,Unit_Launcher,Coord(38+i%3,4+i/3)));
            auto* colony=place(owner,Structure_ConstructionYard,Coord(16,12));
            require(colony,"Launcher handoff fixture could not place its colony");
            auto* raider=spawn(enemy,Unit_Ornithopter,Coord(14,12));
            // A whole wing on one building is what 1.0.823 actually sends, and
            // it is what multiplied a per-building bound by the wing size.
            auto* wingman=spawn(enemy,Unit_Ornithopter,Coord(14,16));
            publish();
            auto rearm=[&] {
                bot->customWave.members.clear();
                for(auto* unit:roster) {
                    if(unit->getOwner()!=owner || unit->isAFlyingUnit()) continue;
                    unit->setHealth(unit->getMaxHealth());
                    bot->doSetAttackMode(unit,GUARD);
                    bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);
                    unit->setForced(false);
                }
                bot->manualUnitOrders.clear();
            };
            auto hunting=[&] {
                int n=0;for(auto* gun:guns) if(gun->getAttackMode()==HUNT) ++n;return n;
            };
            auto assigned=[&] {
                int n=0;
                for(auto* gun:guns) if(bot->defenceAssignments.count(gun->getObjectID())) ++n;
                return n;
            };

            // 1. No live air contact: every launcher goes with the wave.
            raider->setHealth(0);wingman->setHealth(0);
            rearm();
            bot->launchGroundHunt();
            require(hunting()==int(guns.size()),
                "A whole-army dispatch left launchers out of the wave");

            // 2. A whole wing on one building we own claims launchers, and the
            //    claim stays inside the documented per-building bound instead of
            //    being multiplied by the number of aircraft in the wing. The
            //    dispatch then withholds exactly the claimed launchers.
            rearm();
            auto hover=[&](UnitBase* bird,Coord at) {
                bird->setLocation(origin+at);
                bird->setHealth(bird->getMaxHealth());
                bird->doSetAttackMode(AREAGUARD);
                bird->doAttackObject(colony,true);
                require(bird->getTarget()==colony,"Launcher handoff fixture raider is not attacking");
            };
            hover(raider,Coord(14,12));
            hover(wingman,Coord(14,16));
            for(auto* gun:guns)
                require(!gun->isInWeaponRange(raider) && !gun->isInWeaponRange(wingman),
                    "The rescue fixture launchers can already shoot, so no journey is bounded");
            bot->defendStructuresFromAircraft();
            const int claimed=assigned();
            SDL_Log("WHOLE_ARMY_DIAG: wing of 2 on one building claimed %d of %zu launchers "
                "(bound %d, journey %d tiles)",claimed,guns.size(),
                QuantBot::kAirRescueDefenders,range(guns.front(),colony));
            require(claimed>0,"The rescue never claimed a launcher for a live air attack");
            require(claimed<=QuantBot::kAirRescueDefenders,
                "One attacked building claimed more travelling anti-air responders than its bound");
            require(claimed<int(guns.size()),
                "The anti-air rescue claimed every launcher the house owned");
            // An older aircraft can join a contact already owned by a newer
            // one. Count those existing journeys before processing ID order.
            bot->defenceAssignments.clear();rearm();
            raider->setHealth(0);
            bot->defendStructuresFromAircraft();
            require(assigned()==QuantBot::kAirRescueDefenders,
                "The later-ID aircraft did not establish its rescue");
            const auto continuingAssignments=bot->defenceAssignments;
            hover(raider,Coord(14,12));
            bot->defendStructuresFromAircraft();
            require(assigned()==QuantBot::kAirRescueDefenders,
                "An earlier-ID new contact multiplied an existing building rescue");
            require(bot->defenceAssignments==continuingAssignments,
                "A new contact restarted travelling defenders");
            for(auto* gun:guns)
                if(bot->defenceAssignments.count(gun->getObjectID()))
                    require(bot->activeDefenceAssignment(gun),
                        "A launcher was claimed by a contact that is not live");
            bot->launchGroundHunt();
            for(auto* gun:guns)
                require((gun->getAttackMode()==HUNT)
                        !=bool(bot->activeDefenceAssignment(gun)),
                    "A launcher was both withheld for air defence and dispatched, or neither");

            // 3. The aircraft dies: the claim is released and the launcher is
            //    back in the next wave. This is the handoff the complaint is
            //    about - a rescue that never ends keeps the launchers forever.
            raider->setHealth(0);wingman->setHealth(0);
            bot->checkAllUnits();
            require(assigned()==0,"A dead aircraft kept its anti-air claim on the launchers");
            rearm();
            bot->launchGroundHunt();
            require(hunting()==int(guns.size()),
                "Launchers released by a finished rescue did not rejoin the wave");

            // 4. The same handoff when the aircraft merely breaks off: it is
            //    alive and visible but no longer attacking anything of ours and
            //    out of every launcher's reach.
            rearm();
            hover(raider,Coord(14,12));
            bot->defendStructuresFromAircraft();
            require(assigned()>0,"The break-off fixture never claimed a launcher");
            raider->setLocation(origin+Coord(44,24));
            bot->doSetAttackMode(raider,STOP);
            raider->setTarget(nullptr);
            for(auto* gun:guns)
                require(!gun->isInWeaponRange(raider),
                    "The break-off fixture aircraft is still within a launcher's reach");
            bot->checkAllUnits();
            require(assigned()==0,"A disengaged aircraft kept its anti-air claim");
            rearm();
            bot->launchGroundHunt();
            require(hunting()==int(guns.size()),
                "Launchers released by a disengaged aircraft did not rejoin the wave");

            raider->setHealth(0);wingman->setHealth(0);
            for(auto* gun:guns) retire(gun);
            if(colony->getHealth()>0) colony->setHealth(0);
            bot->defenceAssignments.clear();
            bot->customWave.members.clear();
            getQuantBotConfig().recovery.enabled=true;
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
