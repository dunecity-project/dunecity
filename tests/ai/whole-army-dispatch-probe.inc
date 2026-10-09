#include <structures/TurretBase.h>

// Real-engine Custom Hard/Brutal whole-army dispatch and asset emergency
// defence. Included only by run-campaign-balance.py; never linked into the game.
//
// Everything here drives the production QuantBot entry points against real
// units, real structures and the real map. Nothing is simulated.
//
// The two behaviours under test:
//   1. One attack dispatch sends the WHOLE available combat army to engine
//      Hunt, wherever it stands and whatever it was previously doing.
//   2. A building attack sends the whole ground army to Hunt; worker raids
//      retain a bounded local response.
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

            int responders=0, hunting=0;
            for(auto* unit:eligible)
                if(bot->defenceAssignments.count(unit->getObjectID())) {
                    ++responders;
                    if(unit->getAttackMode()==HUNT) ++hunting;
                }
            const int threat=14*currentGame->objectData.data[Unit_SiegeTank]
                [enemy->getHouseID()].price;
            SDL_Log("WHOLE_ARMY_DIAG: emergency responders=%d of %zu hunting=%d "
                "threat=%d band=%d",
                responders,eligible.size(),hunting,threat,
                QuantBot::emergencyResponseRadius(threat));
            // Adequate, not token: the old proportional answer to this raid was
            // a handful of the nearest troops.
            require(responders>=8,"An overwhelming attack on our own building got a token response");
            // And every one of them answers it the way the owner requires: by
            // hunting the invasion, not by parking in Area Guard. The recall of
            // troops that were hunting elsewhere is asserted below.
            require(hunting==responders,
                "A troop answering an attack on our own building was not sent to Hunt");
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

        // ---- A real attack on a building we own scrambles the WHOLE army ----
        //
        // The requirement the bounded local response above cannot meet: when a
        // building this house owns is actually under attack, EVERY eligible
        // ground combat troop on the map hunts that invasion. Local or well
        // outside the bounded response band, idle, guarding, retreating or
        // already hunting somewhere else, in every posture, and with no
        // readiness, quota, strength or assembly condition in the way. Movement
        // and damage are measured over real game cycles, so "it answered" means
        // it travelled and fought rather than that an order was accepted.
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

            // Where every responder starts from. The last three are the distant
            // reserve, parked in whichever corner is furthest from the building
            // under attack so "outside the bounded band" is a measured fact.
            std::vector<Coord> origins;
            for(auto* unit:eligible) origins.push_back(unit->getLocation());
            std::vector<Coord> farSlots{origin+Coord(45,25),origin+Coord(44,25),origin+Coord(45,23)};
            auto reset=[&](bool mixed) {
                bot->defenceAssignments.clear();
                bot->defenceResponseCycles.clear();
                bot->customWave.members.clear();
                for(size_t i=0;i<army.size();++i) {
                    const Coord at=i<origins.size() ? origins[i] : farSlots[i-origins.size()];
                    army[i]->setHealth(army[i]->getMaxHealth());
                    army[i]->setLocation(at);
                    bot->doMove2Pos(army[i],army[i]->getX(),army[i]->getY(),false);
                    bot->doSetAttackMode(army[i],GUARD);
                    army[i]->setForced(false);
                }
                if(mixed) {
                    // Retreating, posted on Area Guard with prey of its own, and
                    // already hunting: none of these may hold a troop at home.
                    bot->doSetAttackMode(retreating,RETREAT);
                    bot->doSetAttackMode(targeting,AREAGUARD);
                    bot->doAttackObject(targeting,prey,false);
                    for(auto* unit:remote) bot->doSetAttackMode(unit,HUNT);
                    bot->colonyGuardSet.clear();
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
            auto everyoneHunts=[&](const char* why) {
                for(auto* unit:army) {
                    if(unit->getAttackMode()==HUNT
                        && bot->defenceAssignments.count(unit->getObjectID())
                        && bot->defenceAssignments[unit->getObjectID()]==raider->getObjectID()) continue;
                    SDL_Log("WHOLE_ARMY_DIAG: base scramble left id=%u item=%u at=%d,%d dist=%d "
                        "mode=%d assigned=%d",
                        unit->getObjectID(),unit->getItemID(),unit->getLocation().x,
                        unit->getLocation().y,range(unit,raider),int(unit->getAttackMode()),
                        int(bot->defenceAssignments.count(unit->getObjectID())));
                    require(false,why);
                }
            };
            auto protectedUnitsKept=[&](const char* why) {
                for(auto* unit:excluded)
                    require(!bot->defenceAssignments.count(unit->getObjectID()),why);
                for(auto* unit:{commanded,wreck,worker,builder})
                    require(unit->getAttackMode()!=HUNT,why);
            };
            auto farthest=[&]{
                int worst=0;
                for(auto* unit:army) worst=std::max(worst,range(unit,raider));
                return worst;
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
                    const int far=farthest();
                    require(far>QuantBot::kEmergencyBandMax,
                        "Base-scramble fixture has no responder outside the bounded response band");
                    bot->onDamage(yard,40,raider->getObjectID());
                    SDL_Log("WHOLE_ARMY_DIAG: base scramble difficulty=%d posture=%s army=%zu "
                        "farthest=%d band_max=%d",
                        int(difficulty),ArmyPosturePolicy::postureName(posture),army.size(),
                        far,QuantBot::kEmergencyBandMax);
                    everyoneHunts("A real attack on our own building did not send the whole army to Hunt");
                    protectedUnitsKept("The base scramble commandeered a protected unit");
                }

            // Separate live attacks must not send a travelling army back and
            // forth between colonies. Keep the invasion it is already hunting.
            {
                bot->difficulty=QuantBot::Difficulty::Brutal;
                bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
                reset(false);
                invade(yard,origin+Coord(7,3));
                bot->onDamage(yard,40,raider->getObjectID());
                everyoneHunts("The first invasion did not mobilise the army");
                const Coord transit=farTank->getDestination();
                auto* otherColony=place(owner,Structure_ZoneResidential,Coord(42,20));
                require(otherColony,"Second invasion fixture could not place its colony");
                auto* other=spawn(enemy,Unit_SiegeTank,Coord(40,22));
                other->doAttackObject(otherColony,true);
                publish();
                require(other->isInWeaponRange(otherColony),
                    "Second invasion fixture is outside its weapon range");
                bot->defenceResponseCycles.clear();
                bot->scrambleUnitsAndDefend(other,false,otherColony);
                require(farTank->getTarget()==raider && farTank->getDestination()==transit,
                    "A second base attack restarted a travelling defender's order");
                everyoneHunts("A second invasion stole an ongoing whole-army defence");
                retire(other);
                publish();

                // Ending the attack releases its ownership even if the living
                // enemy remains beside the base. Hunt can still fight it normally.
                raider->setTarget(nullptr);
                raider->doSetAttackMode(STOP);
                bot->checkAllUnits();
                for(auto* unit:army)
                    require(!bot->defenceAssignments.count(unit->getObjectID()),
                        "A stopped attacker beside the base retained emergency ownership");

                // The same ordinary scan must notice real turret fire, and
                // release it when that target is removed without a death.
                auto* gun=dynamic_cast<TurretBase*>(place(enemy,Structure_RocketTurret,Coord(36,20)));
                require(gun,"Turret invasion fixture could not place its attacker");
                gun->doAttackObject(otherColony);
                require(gun->getTarget()==otherColony && gun->targetInWeaponRange(),
                    "Turret invasion fixture is not attacking the colony in range");
                bot->defenceResponseCycles.clear();
                bot->checkAllUnits();
                for(auto* unit:army)
                    require(unit->getAttackMode()==HUNT
                        && bot->defenceAssignments.count(unit->getObjectID())
                        && bot->defenceAssignments[unit->getObjectID()]==gun->getObjectID(),
                        "Turret fire at a colony did not mobilise the whole army");
                gun->setTarget(nullptr);
                bot->checkAllUnits();
                for(auto* unit:army)
                    require(!bot->defenceAssignments.count(unit->getObjectID()),
                        "Ended turret fire retained whole-army ownership");
                gun->setHealth(0);
                otherColony->setHealth(0);
                publish();
            }

            // 2. The ordinary scan, with no damage callback at all, against an
            //    outlying colony lot of each R/C/I kind rather than the base.
            //    The reserve moves to the opposite corner for these, so it is
            //    again genuinely far from the building that is being attacked.
            {
                farSlots={origin+Coord(0,25),origin+Coord(1,25),origin+Coord(0,23)};
                int lotRow=8;
                for(const Uint32 lot:{Structure_ZoneResidential,Structure_ZoneCommercial,
                                      Structure_ZoneIndustrial}) {
                    bot->difficulty=QuantBot::Difficulty::Hard;
                    bot->armyPosture=ArmyPosturePolicy::Posture::Recovering;
                    auto* colony=place(owner,lot,Coord(42,lotRow));
                    require(colony,"Base-scramble fixture could not place a remote colony lot");
                    reset(true);
                    invade(colony,origin+Coord(41,lotRow+2));
                    require(farthest()>QuantBot::kEmergencyBandMax,
                        "Base-scramble colony fixture has no responder outside the response band");
                    bot->checkAllUnits();
                    SDL_Log("WHOLE_ARMY_DIAG: colony scramble lot=%u row=%d farthest=%d",
                        lot,lotRow,farthest());
                    everyoneHunts("An ordinary scan did not send the whole army to a colony under attack");
                    protectedUnitsKept("The colony scramble commandeered a protected unit");
                    colony->setHealth(0);
                    lotRow+=4;
                }
                farSlots={origin+Coord(45,25),origin+Coord(44,25),origin+Coord(45,23)};
            }

            // 3. Real cycles: the recall cannot claim a committed defender, the
            //    distant reserve really closes on the invasion, repeated scans
            //    do not restart what is already under way, and the troops that
            //    reach the raider really damage it.
            {
                bot->difficulty=QuantBot::Difficulty::Brutal;
                bot->armyPosture=ArmyPosturePolicy::Posture::Withdrawing;
                bot->postureSince=currentGame->getGameCycleCount();
                reset(false);
                invade(yard,origin+Coord(7,3));
                // The whole army is a tracked wave that the withdrawal would
                // otherwise be pulling home, with a valid shelter to pull it to.
                for(auto* unit:army) bot->customWave.members.insert(unit->getObjectID());
                bot->protectedRally=origin+Coord(5,11);
                bot->protectedRallyCycle=std::numeric_limits<Uint32>::max();
                bot->checkAllUnits();
                everyoneHunts("A withdrawal reclaimed the defenders of a building under attack");
                require(farTank->getTarget()==raider,
                    "The distant reserve was not pointed at the invasion itself");

                const int start=range(farTank,raider);
                int closed=start, ticks=0;
                // The invader stays alive through this phase so the contact
                // cannot end before the measurement does. Several tiles of real
                // ground, not one, so a single step cannot pass for travelling.
                while(ticks<1500 && closed>start-3) {
                    bot->checkAllUnits();
                    tick(60);
                    ticks+=60;
                    raider->setHealth(raider->getMaxHealth());
                    yard->setHealth(yard->getMaxHealth());
                    closed=std::min(closed,range(farTank,raider));
                }
                SDL_Log("WHOLE_ARMY_DIAG: base scramble travel start=%d closed=%d ticks=%d mode=%d",
                    start,closed,ticks,int(farTank->getAttackMode()));
                require(start>QuantBot::kEmergencyBandMax,
                    "The travelling reserve did not start outside the response band");
                require(closed<=start-3,
                    "A scrambled distant reserve never moved towards the invasion");
                require(farTank->getAttackMode()==HUNT,
                    "The travelling reserve lost engine Hunt on the way");
                require(bot->defenceAssignments.count(farTank->getObjectID()),
                    "The travelling reserve lost its contact on the way");

                // Now let it die: real combat damage, not an accepted order.
                const FixPoint invaderHealth=raider->getHealth();
                int combat=0;
                while(combat<900 && raider->getHealth()>=invaderHealth) {
                    bot->checkAllUnits();
                    tick(30);
                    combat+=30;
                }
                SDL_Log("WHOLE_ARMY_DIAG: base scramble combat ticks=%d health=%d->%d",
                    combat,invaderHealth.lround(),raider->getHealth().lround());
                require(raider->getHealth()<invaderHealth,
                    "The scrambled army never actually damaged the invader");

                // The threat dies: every contact is released. What the house
                // does next - here, resume the withdrawal it was interrupted
                // from - is its own decision again.
                retire(raider);
                bot->checkAllUnits();
                for(auto* unit:army)
                    require(!bot->defenceAssignments.count(unit->getObjectID()),
                        "A defender kept its contact after the invasion died");
                raider=spawn(enemy,Unit_Trike,Coord(7,3));
                publish();
            }

            // 3b. The same release with nothing else competing for the troops,
            //     so the forced transit the emergency created is observed being
            //     dropped rather than replaced by a recall or a rally order.
            {
                bot->difficulty=QuantBot::Difficulty::Brutal;
                bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
                reset(false);
                invade(yard,origin+Coord(7,3));
                bot->checkAllUnits();
                everyoneHunts("A quiet-posture base attack did not scramble the whole army");
                require(farTank->wasForced(),
                    "The distant reserve was not actually committed to travel");
                retire(raider);
                bot->checkAllUnits();
                for(auto* unit:army) {
                    if(!bot->defenceAssignments.count(unit->getObjectID()) && !unit->wasForced())
                        continue;
                    SDL_Log("WHOLE_ARMY_DIAG: not released id=%u item=%u mode=%d assigned=%d "
                        "forced=%d dest=%d,%d",
                        unit->getObjectID(),unit->getItemID(),int(unit->getAttackMode()),
                        int(bot->defenceAssignments.count(unit->getObjectID())),
                        int(unit->wasForced()),unit->getDestination().x,unit->getDestination().y);
                    require(false,"A defender was not released when the invasion died");
                }
                raider=spawn(enemy,Unit_Trike,Coord(7,3));
                publish();
            }

            // 4. What must NOT scramble a house: an enemy that is merely
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
                    require(unit->getAttackMode()!=HUNT,
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
                    require(unit->getAttackMode()!=HUNT,
                        "An attacker that had stopped shooting scrambled the whole house");

                // A dead contact, handed in through the damage callback.
                reset(false);
                invade(yard,origin+Coord(7,3));
                raider->setHealth(0);
                bot->defenceAssignments.clear();
                bot->defenceResponseCycles.clear();
                bot->scrambleUnitsAndDefend(raider,false,yard);
                for(auto* unit:army)
                    require(unit->getAttackMode()!=HUNT,"A dead contact scrambled the whole house");
                retire(loiterer);
            }

            // 5. A harvester under fire keeps the established bounded district
            //    response: it answers, and it does not mobilise the map.
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
                    if(bot->defenceAssignments.count(unit->getObjectID())) ++answered;
                SDL_Log("WHOLE_ARMY_DIAG: harvester response answered=%d of %zu far=%d band=%d",
                    answered,army.size(),range(farTank,poacher),
                    QuantBot::emergencyResponseRadius(
                        currentGame->objectData.data[Unit_Tank][enemy->getHouseID()].price));
                require(answered>0,"A harvester under fire got no answer at all");
                require(range(farTank,poacher)>QuantBot::kEmergencyBandMax
                    && !bot->defenceAssignments.count(farTank->getObjectID())
                    && farTank->getAttackMode()!=HUNT,
                    "A harvester raid mobilised a reserve on the far side of the map");
                require(!bot->defenceAssignments.count(worker->getObjectID()),
                    "The harvester was sent to defend itself");
                retire(poacher);
            }

            for(auto* unit:{farTank,farQuad,farLauncher}) retire(unit);
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
