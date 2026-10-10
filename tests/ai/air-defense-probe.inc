// Real target selection with a tempting raid and simultaneous home/worker threat.
#include <players/AirStrikePolicy.h>
#include <units/Ornithopter.h>

void runAirDefenseProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto* owner=game.getHouse(HOUSE_HARKONNEN);
    auto* enemy=game.getHouse(HOUSE_ATREIDES);
    QuantBot* bot=nullptr;
    for(const auto& player:owner->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get()))bot=candidate;
    require(bot && enemy,"Air defence fixture missing houses or bot");
    game.gameType=GameType::CustomGame;
    bot->gameMode=QuantBot::GameMode::Custom;bot->supportMode=false;
    bot->manualUnitOrders.clear();
    Coord origin=Coord::Invalid();
    for(int y=2;y+24<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for(int x=2;x+24<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear=true;
            for(int dy=0;dy<24;++dy)for(int dx=0;dx<24;++dx) {
                const auto* tile=currentGameMap->getTile(x+dx,y+dy);
                if(tile->hasAGroundObject() || tile->isMountain())clear=false;
            }
            if(clear)origin=Coord(x,y);
        }
    require(origin.isValid(),"Air defence fixture needs clear space");
    auto spawn=[&](House* house,Uint32 item,Coord offset) {
        auto* unit=house->createUnit(item);require(unit,"Air fixture unit failed");
        unit->deploy(origin+offset);unit->setVisible(owner->getTeamID(),true);return unit;
    };
    auto* air=spawn(owner,Unit_Ornithopter,Coord(8,8));
    auto* worker=spawn(owner,Unit_Harvester,Coord(19,19));
    auto* attacker=spawn(enemy,Unit_Tank,Coord(3,7));
    auto* launcher=spawn(enemy,Unit_Launcher,Coord(22,2));
    auto* base=owner->placeStructure(NONE_ID,Structure_ConstructionYard,origin.x+2,origin.y+2,true,true);
    auto* raid=enemy->placeStructure(NONE_ID,Structure_ConstructionYard,origin.x+10,origin.y+6,true,true);
    require(base && raid,"Air fixture buildings failed");
    raid->setVisible(owner->getTeamID(),true);
    auto savedUnits=unitList;auto savedStructures=structureList;
    unitList.clear();structureList.clear();
    unitList.push_back(air);unitList.push_back(worker);unitList.push_back(attacker);
    structureList.push_back(base);structureList.push_back(raid);
    try {
        for(auto difficulty:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                              QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            bot->difficulty=difficulty;
            const auto& config=getQuantBotConfig();
            auto settings=config.getSettings(static_cast<int>(difficulty));
            settings.ornithopterAttackEnabled=true;
            attacker->setLocation(origin+Coord(3,7));attacker->doAttackObject(base,true);
            bot->doSetAttackMode(air,STOP);bot->doAttackObject(air,raid,true);
            bot->tryLaunchOrnithopterStrike(settings,config);
            require(air->getTarget()==attacker,"Aircraft kept raiding while its construction yard was attacked");
            attacker->setLocation(origin+Coord(18,19));attacker->doAttackObject(worker,true);
            bot->doAttackObject(air,raid,true);
            bot->tryLaunchOrnithopterStrike(settings,config);
            require(air->getTarget()==attacker,"Aircraft ignored an attacked harvester outside its base");
            settings.ornithopterAttackEnabled=false;
            bot->doSetAttackMode(air,STOP);bot->tryLaunchOrnithopterStrike(settings,config);
            require(air->getTarget()==attacker,"Disabling offensive raids disabled defensive interception");
            settings.ornithopterAttackEnabled=true;
            attacker->setHealth(0);bot->tryLaunchOrnithopterStrike(settings,config);
            require(air->getTarget()==raid,"Aircraft did not resume raiding after the threat died");
            attacker->setHealth(attacker->getMaxHealth());
        }
        const auto& config=getQuantBotConfig();auto settings=config.hard;
        settings.ornithopterAttackEnabled=true;
        // A real attack overrides raid safety, even under an anti-air escort.
        // The escort itself is now what gets hit: flying into this engagement is
        // already sanctioned by the emergency, so the aircraft kills the thing
        // that shoots it rather than trading with the tank beside it. The rescue
        // is not abandoned - the launcher stands on the harvester's attacker.
        launcher->setLocation(attacker->getLocation());unitList.push_back(launcher);
        bot->doSetAttackMode(air,STOP);bot->tryLaunchOrnithopterStrike(settings,config);
        require(air->getTarget()==launcher,
                "An escorted rescue did not engage the launcher escorting the attacker");
        require(blockDistance(launcher->getLocation(),attacker->getLocation()).lround()
                <=AirStrikePolicy::kLocalEngagementRadius,
                "The escort fixture no longer has its launcher inside the engagement area");
        attacker->doSetAttackMode(STOP);
        launcher->doAttackObject(worker,true);
        bot->doAttackObject(air,raid,true);
        bot->tryLaunchOrnithopterStrike(settings,config);
        require(air->getTarget()==launcher,"Aircraft refused to intercept a launcher attacking its harvester");
        // Exercise the damage callback, not just the standalone target chooser.
        bot->doAttackObject(air,raid,true);bot->defenceResponseCycles.clear();
        bot->onDamage(worker,1,launcher->getObjectID());
        require(air->getTarget()==launcher,"Harvester damage did not immediately interrupt the air raid");
        // Nearby threats without an actual attack still obey normal raid safety.
        launcher->doSetAttackMode(STOP);
        bot->doSetAttackMode(air,STOP);bot->tryLaunchOrnithopterStrike(settings,config);
        require(air->getTarget()!=attacker && air->getTarget()!=launcher,
                "Proximity alone bypassed anti-air safety");
        launcher->setLocation(origin+Coord(3,7));launcher->doAttackObject(base,true);
        bot->doAttackObject(air,raid,true);bot->defenceResponseCycles.clear();
        bot->onDamage(base,1,launcher->getObjectID());
        require(air->getTarget()==launcher,"Base damage did not recall aircraft from a raid");
        launcher->setHealth(0);attacker->setHealth(0);
        bot->tryLaunchOrnithopterStrike(settings,config);
        require(air->getTarget()==raid,"Aircraft failed to resume raids after the emergency ended");

        // Damaged aircraft keep fighting. Nothing repairs an ornithopter —
        // UnitBase::doRepair is empty and only ground units drive to a repair
        // yard — so reserving one for repair parks it for the rest of the match.
        attacker->setHealth(attacker->getMaxHealth());
        launcher->setHealth(launcher->getMaxHealth());
        launcher->setLocation(origin+Coord(3,7));launcher->doAttackObject(base,true);
        air->setHealth(air->getMaxHealth()/8);
        require(air->isBadlyDamaged(),"Damaged-air fixture is not badly damaged");
        require(!bot->reserveDamagedUnitForRepair(air),
                "An unrepairable damaged aircraft was still reserved for repair");
        bot->doSetAttackMode(air,STOP);bot->doAttackObject(air,raid,true);
        bot->tryLaunchOrnithopterStrike(settings,config);
        require(air->getTarget()==launcher,"A damaged ornithopter refused to defend its base");

        // A live interception is stable. Repeated damage callbacks and a moving
        // attacker must not restart the attack run: the reported match had 149
        // target switches in five minutes, 144 of them while the previous
        // target was still alive.
        const ObjectBase* held=air->getTarget();
        attacker->setLocation(origin+Coord(2,3));attacker->doAttackObject(base,true);
        for(int volley=0;volley<6;++volley) {
            bot->defenceResponseCycles.clear();
            launcher->setLocation(origin+Coord(3+volley%3,7+volley%2));
            launcher->doAttackObject(base,true);
            bot->onDamage(base,1,volley%2 ? attacker->getObjectID() : launcher->getObjectID());
            require(air->getTarget()==held,"Aircraft re-chose its target on a later volley");
        }
        // A worker attacked out in the field does not pull aircraft off the base.
        attacker->setLocation(origin+Coord(18,19));attacker->doAttackObject(worker,true);
        bot->defenceResponseCycles.clear();
        bot->onDamage(worker,1,attacker->getObjectID());
        require(air->getTarget()==launcher,"A remote harvester rescue outranked an attack on the base");
        // With the base safe the same wing does rescue the worker, and a fresh
        // attack on the base then takes it back.
        launcher->doSetAttackMode(STOP);launcher->setHealth(0);
        bot->doSetAttackMode(air,STOP);bot->tryLaunchOrnithopterStrike(settings,config);
        require(air->getTarget()==attacker,"Aircraft ignored the remaining harvester attacker");
        launcher->setHealth(launcher->getMaxHealth());
        launcher->setLocation(origin+Coord(3,7));launcher->doAttackObject(base,true);
        bot->defenceResponseCycles.clear();
        bot->onDamage(base,1,launcher->getObjectID());
        require(air->getTarget()==launcher,"An attack on the base did not outrank a rescue under way");
        air->setHealth(air->getMaxHealth());
        launcher->setHealth(0);attacker->setHealth(0);
        bot->tryLaunchOrnithopterStrike(settings,config);
        require(air->getTarget()==raid,"Aircraft did not resume raiding after the stable interception ended");

        unitList.remove(launcher);
        bot->onHumanUnitOrder(air->getObjectID());bot->doAttackObject(air,raid,true);
        bot->tryLaunchOrnithopterStrike(settings,config);
        require(air->getTarget()==raid,"Defensive automation replaced a human aircraft order");

        // === Ordinary ground prey, the launcher ratio and launcher first ===
        // The reported match flew 3636 sorties and found a building to hit in
        // 551 of them; a roaming enemy unit was never once a target, because an
        // unescorted tank in the open ranked zero. It is prey now, and mobile
        // rocket cover is priced in aircraft instead of forbidden outright.
        {
            auto outerUnits=unitList;auto outerStructures=structureList;
            bot->manualUnitOrders.clear();
            bot->difficulty=QuantBot::Difficulty::Hard;
            bot->defenceAssignments.clear();bot->defenceResponseCycles.clear();
            auto raids=config.hard;raids.ornithopterAttackEnabled=true;
            auto strike=[&]{bot->tryLaunchOrnithopterStrike(raids,config);};
            const int launcherReach=AirStrikePolicy::safetyRange(
                currentGame->objectData.data[Unit_Launcher][enemy->getHouseID()].weaponrange);

            // Only our own base stands: no exposed enemy building competes for
            // the sortie, so every assertion below is about ground prey.
            unitList.clear();structureList.clear();
            structureList.push_back(base);
            std::vector<UnitBase*> fleet;
            for(int i=0;i<8;++i) {
                auto* bird=spawn(owner,Unit_Ornithopter,Coord(2+i%4,20+i/4));
                bot->doSetAttackMode(bird,STOP);
                fleet.push_back(bird);
            }
            auto* prey=spawn(enemy,Unit_Tank,Coord(14,20));
            auto* foot=spawn(enemy,Unit_Trooper,Coord(14,22));
            auto* escortA=spawn(enemy,Unit_Launcher,Coord(12,20));
            auto* escortB=spawn(enemy,Unit_Launcher,Coord(16,20));
            escortA->setVisible(VIS_ALL,false);escortB->setVisible(VIS_ALL,false);
            unitList.push_back(prey);unitList.push_back(foot);
            require(blockDistance(prey->getLocation(),base->getClosestPoint(prey->getLocation()))
                    .lround() > prey->getWeaponRange()+3,
                "The prey is close enough to our base to be ordinary base defence,"
                " so this would not test a roaming raid");
            // One tank plus the wing, so no second candidate can decide a case.
            auto wingOf=[&](int count){
                unitList.clear();unitList.push_back(prey);
                for(int i=0;i<count;++i) unitList.push_back(fleet[i]);
            };
            auto flyer=[&]{return fleet[0];};

            // --- An exposed unit in the open is attacked, and really shot at.
            wingOf(1);
            bot->doSetAttackMode(flyer(),STOP);strike();
            require(flyer()->getTarget()==prey,
                "A single aircraft ignored an exposed enemy tank with no anti-air near it");
            require(flyer()->getAttackMode()==STOP && flyer()->wasForced(),
                "The raid was not issued as an explicit forced order in STOP mode");
            // Infantry is prey on the same terms.
            unitList.clear();unitList.push_back(foot);unitList.push_back(flyer());
            bot->doSetAttackMode(flyer(),STOP);strike();
            require(flyer()->getTarget()==foot,"Exposed enemy infantry was not raided");

            // Real frames: it flies there and damages the tank. Selection alone
            // is what the old telemetry already showed for buildings.
            wingOf(1);
            bot->doSetAttackMode(flyer(),STOP);strike();
            require(flyer()->getTarget()==prey,"The firing fixture lost its target before flying");
            const FixPoint preyFull=prey->getMaxHealth();
            const int startedAway=blockDistance(flyer()->getLocation(),prey->getLocation()).lround();
            const Uint32 fireDeadline=game.getGameCycleCount()+MILLI2CYCLES(120000);
            bool damagedPrey=false,closed=false;
            while(!damagedPrey && game.getGameCycleCount()<fireDeadline) {
                for(int step=0;step<25;++step) {game.processObjects();++game.gameCycleCount;}
                if(prey->getHealth()<preyFull) damagedPrey=true;
                // Hold the fixture steady so what is measured is the attack run.
                prey->setHealth(preyFull);
                if(blockDistance(flyer()->getLocation(),prey->getLocation()).lround()<startedAway)
                    closed=true;
                if(!damagedPrey) strike();
            }
            require(closed,"The aircraft never flew towards the exposed tank it was sent at");
            require(damagedPrey,"No aircraft ever damaged the exposed tank in the open");

            // Launcher priority applies to an already-held base interception,
            // even when the settings disable offensive raids.
            const Coord preyAt=prey->getLocation();
            const Coord escortAt=escortA->getLocation();
            prey->setLocation(origin+Coord(5,4));
            prey->doAttackObject(base,true);
            escortA->setLocation(origin+Coord(19,4));
            unitList.push_back(escortA);
            escortA->setVisible(VIS_ALL,false);
            bot->doSetAttackMode(flyer(),STOP);strike();
            require(flyer()->getTarget()==prey,
                "The held emergency fixture did not acquire the base attacker");
            raids.ornithopterAttackEnabled=false;
            escortA->setVisible(owner->getTeamID(),true);
            strike();
            require(flyer()->getTarget()==escortA,
                "A held emergency target or disabled raids suppressed launcher escort priority");
            strike();
            require(flyer()->getTarget()==escortA,"The emergency launcher interception was unstable");
            raids.ornithopterAttackEnabled=true;
            prey->doSetAttackMode(STOP);
            prey->setLocation(preyAt);escortA->setLocation(escortAt);
            escortA->setVisible(VIS_ALL,false);
            unitList.remove(escortA);

            // --- Mobile launcher cover is a price, not a wall.
            escortA->setVisible(owner->getTeamID(),true);
            unitList.push_back(escortA);
            require(blockDistance(escortA->getLocation(),prey->getLocation()).lround()<=launcherReach,
                "The escort fixture no longer covers the prey it is meant to protect");
            auto wingWithEscort=[&](int count){wingOf(count);unitList.push_back(escortA);};
            auto holdsAt=[&](int count,const char* why) {
                wingWithEscort(count);
                bot->doSetAttackMode(flyer(),STOP);strike();
                require(flyer()->getTarget()==nullptr,why);
            };
            holdsAt(3,"Three aircraft raided into a launcher's cover, below the four-per-launcher ratio");
            wingWithEscort(4);
            bot->doSetAttackMode(flyer(),STOP);strike();
            require(flyer()->getTarget()==escortA,
                "Four aircraft did not engage the launcher covering their prey first");
            // Overlapping launchers cost twice as much: seven hold, eight fly.
            escortB->setVisible(owner->getTeamID(),true);
            unitList.push_back(escortB);
            require(blockDistance(escortB->getLocation(),prey->getLocation()).lround()<=launcherReach
                    && blockDistance(escortA->getLocation(),prey->getLocation()).lround()<=launcherReach,
                "The overlap fixture needs both launchers covering the prey");
            auto withBoth=[&](int count){
                wingOf(count);unitList.push_back(escortA);unitList.push_back(escortB);
            };
            withBoth(7);
            bot->doSetAttackMode(flyer(),STOP);strike();
            require(flyer()->getTarget()==nullptr,
                "Seven aircraft entered the overlap of two launchers, which costs eight");
            withBoth(8);
            bot->doSetAttackMode(flyer(),STOP);strike();
            require(flyer()->getTarget()==escortA || flyer()->getTarget()==escortB,
                "Eight aircraft did not engage either launcher of the two-launcher overlap");
            unitList.remove(escortB);escortB->setVisible(VIS_ALL,false);

            // --- Who counts as the local wing. One launcher, four aircraft, and
            // each exclusion in turn has to put the sortie back below the ratio.
            auto fourWithOne=[&]{
                wingOf(4);unitList.push_back(escortA);
                bot->doSetAttackMode(flyer(),STOP);strike();
            };
            fourWithOne();
            require(flyer()->getTarget()==escortA,"The wing fixture does not start at the ratio");
            auto* spare=fleet[3];
            const Coord spareHome=spare->getLocation();
            auto excluded=[&](const char* why) {
                unitList.clear();unitList.push_back(prey);unitList.push_back(escortA);
                for(int i=0;i<4;++i) unitList.push_back(fleet[i]);
                bot->doSetAttackMode(flyer(),STOP);strike();
                require(flyer()->getTarget()==nullptr,why);
            };
            spare->setLocation(origin.x+2,origin.y+2);
            require(blockDistance(flyer()->getLocation(),spare->getLocation()).lround()
                    > AirStrikePolicy::kLocalEngagementRadius,
                "The distant aircraft is still inside the engagement radius");
            excluded("An aircraft outside the engagement radius was counted as local support");
            spare->setLocation(spareHome.x,spareHome.y);
            bot->doSetAttackMode(spare,RETREAT);
            excluded("A retreating aircraft was counted towards an offensive sortie");
            bot->doSetAttackMode(spare,STOP);
            bot->onHumanUnitOrder(spare->getObjectID());
            excluded("A human-ordered aircraft was conscripted into the AI's force ratio");
            bot->manualUnitOrders.clear();
            spare->setHealth(spare->getMaxHealth()/8);
            require(spare->isBadlyDamaged(),"The damaged-wing fixture is not badly damaged");
            excluded("A badly damaged aircraft was counted towards an offensive sortie");
            spare->setHealth(spare->getMaxHealth());
            spare->setActive(false);
            excluded("An inactive aircraft was counted towards an offensive sortie");
            spare->setActive(true);
            spare->setRespondable(false);
            excluded("An unrespondable aircraft was counted towards an offensive sortie");
            spare->setRespondable(true);
            fourWithOne();
            require(flyer()->getTarget()==escortA,"Restoring the wing did not restore the sortie");

            // --- A launcher we cannot see, do not own or have already killed
            // charges nothing. Three aircraft take the tank in each case.
            auto threeTakePrey=[&](const char* why) {
                wingOf(3);unitList.push_back(escortA);
                bot->doSetAttackMode(flyer(),STOP);strike();
                require(flyer()->getTarget()==prey,why);
                require(flyer()->getTarget()!=escortA,"A launcher that charges nothing became prey itself");
            };
            escortA->setVisible(VIS_ALL,false);
            threeTakePrey("A launcher hidden by fog was still charged for, inferring cover we cannot see");
            escortA->setVisible(owner->getTeamID(),true);
            holdsAt(3,"Revealing the launcher again did not restore its cover");
            escortA->setHealth(0);
            threeTakePrey("A dead launcher still protected the ground around it");
            escortA->setHealth(escortA->getMaxHealth());
            escortA->deviate(owner);
            require(escortA->wasDeviated() && escortA->getOwner()==owner,
                "The deviation fixture did not hand the launcher to our own house");
            threeTakePrey("A launcher of our own was treated as enemy anti-air");
            escortA->quitDeviation();
            holdsAt(3,"Returning the launcher to the enemy did not restore its cover");

            // --- A launcher arriving over our prey takes the attack run from an
            // ordinary ground target; its death hands the run back.
            escortA->setVisible(VIS_ALL,false);
            wingOf(4);
            bot->doSetAttackMode(flyer(),STOP);strike();
            require(flyer()->getTarget()==prey && flyer()->wasForced(),
                "The arrival fixture does not start on a forced ordinary ground target");
            escortA->setVisible(owner->getTeamID(),true);
            wingOf(4);unitList.push_back(escortA);
            strike();
            require(flyer()->getTarget()==escortA,
                "A launcher arriving over the prey did not take over the attack run");
            strike();
            require(flyer()->getTarget()==escortA,
                "A live launcher attack run was re-chosen on the following pass");
            escortA->setHealth(0);
            strike();
            require(flyer()->getTarget()==prey,
                "Killing the escort did not hand the attack run back to the ground target");
            escortA->setHealth(escortA->getMaxHealth());
            escortA->setVisible(VIS_ALL,false);unitList.remove(escortA);

            // --- Turret and Deviator cover has no price. Eight aircraft still
            // do not enter it, and neither weapon becomes prey itself.
            auto* turret=enemy->placeStructure(NONE_ID,Structure_RocketTurret,
                origin.x+20,origin.y+18,true,true);
            require(turret!=nullptr,"The hard-cover fixture could not place its rocket turret");
            turret->setVisible(owner->getTeamID(),true);
            structureList.push_back(turret);
            require(blockDistance(turret->getLocation(),prey->getLocation()).lround()
                    <= AirStrikePolicy::safetyRange(turret->getWeaponRange()),
                "The rocket turret does not cover the prey, so this tests nothing");
            wingOf(8);
            bot->doSetAttackMode(flyer(),STOP);strike();
            require(flyer()->getTarget()==nullptr,
                "A large wing entered rocket turret cover, which no number of aircraft buys");
            structureList.remove(turret);
            auto* deviator=spawn(enemy,Unit_Deviator,Coord(20,22));
            require(blockDistance(deviator->getLocation(),prey->getLocation()).lround()
                    <= AirStrikePolicy::safetyRange(deviator->getWeaponRange()),
                "The Deviator does not cover the prey, so this tests nothing");
            wingOf(8);unitList.push_back(deviator);
            bot->doSetAttackMode(flyer(),STOP);strike();
            require(flyer()->getTarget()==nullptr,
                "A large wing entered Deviator cover, which converts whatever it hits");

            // === Enemy workers are never an autonomous air target ===
            // Raiding roaming ground units made harvesters prey as well, which
            // is not a raid anyone asked for. The veto is absolute for an
            // aircraft nobody is flying by hand: no selection at any rank, no
            // stale order kept, no reacquisition below the planner, and no shot.
            unitList.remove(deviator);deviator->setVisible(VIS_ALL,false);
            bot->manualUnitOrders.clear();
            auto* worker1=spawn(enemy,Unit_Harvester,Coord(13,20));
            UnitBase* worker2=nullptr;
            if(!isTornieExclusiveItem(Unit_RebelHarvester)
                || ModManager::instance().isTornieContentActive())
                worker2=spawn(enemy,Unit_RebelHarvester,Coord(15,22));
            auto withWorkers=[&](int count) {
                unitList.clear();unitList.push_back(worker1);
                if(worker2) unitList.push_back(worker2);
                for(int i=0;i<count;++i) unitList.push_back(fleet[i]);
            };
            auto notAWorker=[&](const char* why) {
                require(flyer()->getTarget()!=worker1 && (!worker2 || flyer()->getTarget()!=worker2),why);
            };
            // An exposed worker with nothing else around is simply not a target.
            withWorkers(1);
            bot->doSetAttackMode(flyer(),STOP);strike();
            require(flyer()->getTarget()==nullptr,"An ordinary raid selected an exposed enemy harvester");
            // The same pass still raids an ordinary combat unit beside it, so
            // this is a worker veto and not a disabled raid.
            withWorkers(1);unitList.push_back(prey);
            bot->doSetAttackMode(flyer(),STOP);strike();
            require(flyer()->getTarget()==prey,"The control tank beside the workers was not raided");
            notAWorker("The raid chose a worker over the exposed tank next to it");
            // Escorts first still holds, and the worker under the escort is
            // still not what gets hit.
            escortA->setVisible(owner->getTeamID(),true);
            withWorkers(4);unitList.push_back(escortA);
            bot->doSetAttackMode(flyer(),STOP);strike();
            require(flyer()->getTarget()==escortA,
                "A wing at the ratio did not take the launcher escorting the workers");
            notAWorker("A wing at the ratio raided a worker under launcher escort");
            escortA->setVisible(VIS_ALL,false);
            // A retained order - an older build's, or one restored from a save -
            // is dropped by the planner rather than flown out.
            withWorkers(1);
            bot->doSetAttackMode(flyer(),STOP);bot->doAttackObject(flyer(),worker1,true);
            require(flyer()->getTarget()==worker1,"The stale-order fixture could not set its order");
            require(static_cast<Ornithopter*>(flyer())->isVetoedAutonomousPrey(worker1),
                "A shared-house AI worker order bypassed the execution veto");
            const FixPoint staleWorkerHealth=worker1->getHealth();
            for(int step=0;step<900;++step) {game.processObjects();++game.gameCycleCount;}
            notAWorker("A shared-house stale AI worker order survived without a planner pass");
            require(worker1->getHealth()>=staleWorkerHealth,
                "A shared-house stale AI worker order fired before the planner pass");
            strike();
            notAWorker("A stale worker order survived the air planner");
            // Real frames from the dropped state: the engine's own
            // reacquisition must not put the worker back, and nothing fires.
            const FixPoint worker1Full=worker1->getHealth();
            const FixPoint worker2Full=worker2 ? worker2->getHealth() : FixPoint(0);
            auto workersUnhurt=[&](const char* why) {
                require(worker1->getHealth()>=worker1Full
                        && (!worker2 || worker2->getHealth()>=worker2Full),why);
            };
            for(auto mode:{STOP,GUARD,AREAGUARD,AMBUSH,HUNT}) {
                bot->doSetAttackMode(flyer(),mode);
                require(flyer()->findTarget()!=worker1
                        && (!worker2 || flyer()->findTarget()!=worker2),
                    "An autonomous ornithopter search returned an enemy harvester");
                for(int step=0;step<300;++step) {game.processObjects();++game.gameCycleCount;}
                workersUnhurt("An autonomously flown ornithopter shot an enemy harvester");
                notAWorker("Autonomous reacquisition below the planner picked an enemy harvester");
            }
            bot->doSetAttackMode(flyer(),STOP);

            // An actual human order in the shared house keeps its authority.
            bot->onHumanUnitOrder(flyer()->getObjectID());
            bot->doAttackObject(flyer(),worker1,true);
            require(!static_cast<Ornithopter*>(flyer())->isVetoedAutonomousPrey(worker1),
                "A human order in a shared house was classified as autonomous");
            bool manualHit=false;
            const FixPoint manualWorkerHealth=worker1->getHealth();
            for(int step=0;step<4000 && !manualHit;++step) {
                game.processObjects();++game.gameCycleCount;
                manualHit=worker1->getHealth()<manualWorkerHealth;
            }
            require(manualHit,"A human-ordered shared-house aircraft never damaged its target");
            bot->doSetAttackMode(flyer(),STOP);bot->manualUnitOrders.clear();

            // A house with no human operator also drops stale orders without a
            // planner pass. Our own harvester is the enemy bot's worker target.
            auto* aiBird=spawn(enemy,Unit_Ornithopter,Coord(13,16));
            unitList.clear();unitList.push_back(aiBird);unitList.push_back(worker);
            worker->setVisible(enemy->getTeamID(),true);
            aiBird->doSetAttackMode(STOP);aiBird->doAttackObject(worker,true);
            const FixPoint ourWorkerFull=worker->getHealth();
            for(int step=0;step<900;++step) {game.processObjects();++game.gameCycleCount;}
            require(aiBird->getTarget()!=worker,
                "An aircraft of a house with no human operator kept a worker target");
            require(worker->getHealth()>=ourWorkerFull,
                "An aircraft of a house with no human operator shot a harvester");
            aiBird->doSetAttackMode(STOP);aiBird->setVisible(VIS_ALL,false);
            aiBird->setHealth(0);
            worker1->setVisible(VIS_ALL,false);
            if(worker2) worker2->setVisible(VIS_ALL,false);

            unitList=outerUnits;structureList=outerStructures;
            bot->manualUnitOrders.clear();
            bot->defenceAssignments.clear();bot->defenceResponseCycles.clear();
            for(auto* bird:fleet) {bird->setVisible(VIS_ALL,false);bot->doSetAttackMode(bird,STOP);}
            for(auto* ground:{prey,foot,escortA,escortB,deviator}) ground->setVisible(VIS_ALL,false);
            turret->setVisible(VIS_ALL,false);
            // Hand the fixture back exactly as the human-order check left it.
            bot->onHumanUnitOrder(air->getObjectID());bot->doAttackObject(air,raid,true);
            require(air->getTarget()==raid,"The ground-prey block did not restore the outer fixture");
        }

        // === Launchers rescue an outlying colony from aircraft ===
        // The reported match lost two remote construction yards, and the
        // colonies standing around them, to ornithopters while every launcher
        // carried on with a ground skirmish or a rally point. Aircraft are not
        // a reinforcement problem: most of an army cannot shoot at them at all,
        // and the engine drops any flying target that leaves weapon range, so
        // an attack order across the map only parks the launcher that got it.
        {
            bot->difficulty=QuantBot::Difficulty::Hard;
            bot->defenceAssignments.clear();bot->defenceResponseCycles.clear();
            auto* colony=owner->placeStructure(NONE_ID,Structure_ConstructionYard,
                origin.x+20,origin.y+12,true,true);
            require(colony!=nullptr,"Air rescue fixture could not found its colony");
            structureList.push_back(colony);
            const Coord colonyAt=colony->getLocation();
            require(blockDistance(colonyAt,base->getLocation()).lround()>=16,
                "The rescued base is not outlying, so this would test the home base again");
            // The aircraft hovers over a mountain: the tile it occupies is
            // never somewhere a ground unit could be sent.
            const Coord hover(colonyAt.x+3,colonyAt.y);
            auto* peak=currentGameMap->getTile(hover);
            const auto peakTerrain=peak->getType();
            peak->setType(Terrain_Mountain);
            auto* raider=spawn(enemy,Unit_Ornithopter,Coord(23,4));
            raider->setLocation(hover.x,hover.y);
            raider->setVisible(owner->getTeamID(),true);
            raider->doAttackObject(colony,true);
            unitList.push_back(raider);
            require(currentGameMap->getTile(raider->getLocation())->isMountain(),
                "The air rescue fixture needs its aircraft over ground no vehicle can hold");

            // The ground picture: a launcher already committed to a forced AI
            // attack on an enemy tank, a launcher idle at its rally point, a
            // launcher under a human order, and a tank that cannot reach the
            // sky at all. Plus one visible enemy launcher parked beside the
            // colony, whose only job is to make the approach dangerous.
            attacker->setHealth(attacker->getMaxHealth());
            attacker->doSetAttackMode(STOP);
            attacker->setLocation(origin.x+4,origin.y+8);
            launcher->setHealth(launcher->getMaxHealth());
            launcher->doSetAttackMode(STOP);
            launcher->setLocation(origin.x+17,origin.y+12);
            launcher->setVisible(owner->getTeamID(),true);
            unitList.push_back(launcher);
            auto* skirmisher=spawn(owner,Unit_Launcher,Coord(4,6));
            auto* rallying=spawn(owner,Unit_Launcher,Coord(6,9));
            auto* commanded=spawn(owner,Unit_Launcher,Coord(3,9));
            auto* groundOnly=spawn(owner,Unit_Tank,Coord(5,9));
            for(auto* unit:{skirmisher,rallying,commanded,groundOnly}) unitList.push_back(unit);
            require(skirmisher->getLocation()==origin+Coord(4,6)
                && rallying->getLocation()==origin+Coord(6,9)
                && commanded->getLocation()==origin+Coord(3,9),
                "Air rescue fixture could not place its launchers");
            bot->doSetAttackMode(skirmisher,AREAGUARD);
            bot->doAttackObject(skirmisher,attacker,true);
            require(skirmisher->getTarget()==attacker && skirmisher->wasForced(),
                "The fixture needs a launcher already committed by a forced AI order");
            bot->doSetAttackMode(rallying,AREAGUARD);
            bot->doMove2Pos(rallying,rallying->getX(),rallying->getY(),false);
            bot->onHumanUnitOrder(commanded->getObjectID());
            commanded->doAttackObject(attacker,true);
            const ObjectBase* commandedTarget=commanded->getTarget();
            bot->doSetAttackMode(groundOnly,AREAGUARD);
            require(!groundOnly->canAttack(raider),
                "A tank that can shoot aircraft would make the exclusion below meaningless");
            const int rocketRange=currentGame->objectData.data[Unit_Launcher][owner->getHouseID()].weaponrange;
            require(!skirmisher->isInWeaponRange(raider) && !rallying->isInWeaponRange(raider),
                "The rescuing launchers start in range, so nothing would have to travel");

            bot->dangerUpdated=std::numeric_limits<Uint32>::max();
            bot->checkAllUnits();
            require(bot->defenceAssignments[skirmisher->getObjectID()]==raider->getObjectID(),
                "A launcher stayed on its ground skirmish while aircraft killed an outlying colony");
            require(bot->defenceAssignments[rallying->getObjectID()]==raider->getObjectID(),
                "A launcher kept its rally point while aircraft killed an outlying colony");
            require(skirmisher->getTarget()!=attacker,
                "A forced AI ground order outranked an active attack on one of our buildings");
            const Coord rescuePost=skirmisher->getDestination();
            bot->defenceResponseCycles.clear();
            bot->scrambleUnitsAndDefend(attacker,false,base);
            require(bot->defenceAssignments[skirmisher->getObjectID()]==raider->getObjectID()
                && skirmisher->getDestination()==rescuePost,
                "An incidental ground contact stole a launcher travelling to air defence");
            require(bot->defenceAssignments.count(commanded->getObjectID())==0
                && commanded->getTarget()==commandedTarget,
                "The rescue replaced a human order");
            require(!bot->defenceAssignments.count(groundOnly->getObjectID())
                || bot->defenceAssignments.at(groundOnly->getObjectID())!=raider->getObjectID(),
                "A unit that cannot shoot at aircraft was committed to the rescue");

            auto* trapped=spawn(owner,Unit_Launcher,Coord(10,21));
            unitList.push_back(trapped);
            std::vector<std::pair<Coord,int>> enclosure;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                if(!dx && !dy) continue;
                const Coord tileAt=trapped->getLocation()+Coord(dx,dy);
                auto* tile=currentGameMap->getTile(tileAt);
                require(!tile->hasAGroundObject(),"Air fixture ridge would overwrite an occupant");
                enclosure.push_back({tileAt,tile->getType()});
                tile->setType(Terrain_Mountain);
            }
            require(bot->findAntiAirFiringPosition(trapped,colony).isInvalid(),
                "A launcher enclosed by mountains was assigned unreachable ground at the colony");
            bot->defendStructuresFromAircraft();
            require(!bot->defenceAssignments.count(trapped->getObjectID()),
                "A stranded launcher consumed a colony defence slot");
            unitList.remove(trapped);
            for(const auto& saved:enclosure) currentGameMap->getTile(saved.first)->setType(saved.second);

            // Where they were sent: ground they can actually hold, covering the
            // colony, and never the tile the aircraft occupies.
            for(auto* rescuer:{skirmisher,rallying}) {
                const Coord post=rescuer->getDestination();
                require(post.isValid() && post!=raider->getLocation(),
                    "A ground rescuer was ordered at the tile the aircraft flies over");
                require(!currentGameMap->getTile(post)->isMountain()
                        && !currentGameMap->getTile(post)->hasAStructure(),
                    "A ground rescuer was ordered onto ground it cannot hold");
                require(blockDistance(post,colonyAt).lround()<=rocketRange,
                    "A rescuer was sent somewhere that does not cover the colony");
                require(rescuer->getGuardPoint()==post,"The rescue left no guard post at the colony");
            }
            // The aircraft is what makes this ground unsafe. Refusing to enter
            // it would refuse the rescue, so threat safety must not veto it.
            require(bot->dangerAt(skirmisher->getDestination())>0,
                "The approach is not under threat, so this does not test that an emergency"
                " rescue still crosses ground the safety map has marked");
            launcher->setVisible(VIS_ALL,false);
            unitList.remove(launcher);
            // Keep unrelated fixture actors alive for the saved-list restore;
            // this segment measures only the two rescuers and the air attacker.
            for(auto* inactive:{air,worker,attacker,commanded,groundOnly}) unitList.remove(inactive);

            // Real frames: they walk there and open fire. Assignment alone is
            // what the previous behaviour already achieved.
            const int startedAway=blockDistance(skirmisher->getLocation(),colonyAt).lround();
            const Uint32 deadline=game.getGameCycleCount()+MILLI2CYCLES(180000);
            const int shotsBefore=game.combatStats.launcherFiresOnOrni;
            bool engaged=false;
            while(!engaged && game.getGameCycleCount()<deadline) {
                for(int step=0;step<25;++step) {game.processObjects();++game.gameCycleCount;}
                // Hold the fixture steady: the colony survives and the aircraft
                // keeps attacking it, so what is measured is the rescue itself.
                colony->setHealth(colony->getMaxHealth());
                raider->setHealth(raider->getMaxHealth());
                if(raider->getLocation()!=hover) raider->setLocation(hover.x,hover.y);
                if(raider->getTarget()!=colony) raider->doAttackObject(colony,true);
                bot->checkAllUnits();
                engaged=game.combatStats.launcherFiresOnOrni>shotsBefore;
            }
            require(engaged,"No launcher ever reached the colony and fired at the aircraft");
            require(blockDistance(skirmisher->getLocation(),colonyAt).lround()<startedAway,
                "The rescuing launcher never moved towards the colony it was sent to");

            // A launcher that is already in range shoots. It is not marched at
            // the aircraft again on every pass, and a live interception holds.
            auto* onStation=spawn(owner,Unit_Launcher,Coord(23,14));
            unitList.push_back(onStation);
            require(onStation->getLocation()==origin+Coord(23,14),
                "Air rescue fixture could not place its on-station launcher");
            bot->doSetAttackMode(onStation,AREAGUARD);
            require(onStation->isInWeaponRange(raider),"The on-station fixture launcher is not in range");
            const Coord held=onStation->getLocation();
            for(int pass=0;pass<4;++pass) {
                if(raider->getLocation()!=hover) raider->setLocation(hover.x,hover.y);
                if(raider->getTarget()!=colony) raider->doAttackObject(colony,true);
                bot->checkAllUnits();
                require(onStation->getTarget()==raider,
                    "A launcher in range of the aircraft did not fire at it, or re-chose its target");
                require(onStation->getLocation()==held && !onStation->isMoving(),
                    "A launcher in range was force-moved instead of firing");
                require(bot->defenceAssignments[onStation->getObjectID()]==raider->getObjectID(),
                    "An ordinary unit pass dropped a live interception");
            }

            // The attack ends without anybody dying: the rescue clears and
            // nothing chases an aircraft that has broken off.
            raider->doSetAttackMode(STOP);
            raider->setLocation(currentGameMap->getSizeX()-2,currentGameMap->getSizeY()-2);
            bot->checkAllUnits();
            for(auto* rescuer:{skirmisher,rallying,onStation}) {
                require(bot->defenceAssignments.count(rescuer->getObjectID())==0,
                    "A launcher stayed committed after the aircraft broke off");
                require(!rescuer->wasForced(),"A finished rescue left its launcher under a forced order");
                require(rescuer->getTarget()==nullptr || !rescuer->getTarget()->isAFlyingUnit(),
                    "A launcher kept chasing an aircraft that was no longer attacking anything");
            }
            peak->setType(peakTerrain);

            // A borrowed unit is not withheld for repair. Our repair yards refuse a deviated
            // unit, so reserving one parks it for the whole deviation without ever healing it -
            // the same dead weight the damaged-aircraft rule above avoids. Our own damaged
            // vehicles are still reserved.
            groundOnly->setHealth(groundOnly->getMaxHealth()/8);
            require(groundOnly->isBadlyDamaged() && bot->reserveDamagedUnitForRepair(groundOnly),
                "Our own badly damaged vehicle is no longer reserved for repair");
            attacker->setHealth(attacker->getMaxHealth()/8);
            attacker->deviate(owner);
            require(attacker->wasDeviated() && attacker->getOwner()==owner && attacker->isBadlyDamaged(),
                "The deviation fixture did not hand a badly damaged enemy vehicle to the bot's house");
            require(!bot->reserveDamagedUnitForRepair(attacker),
                "A deviated vehicle the bot cannot repair was still reserved for repair");
            attacker->quitDeviation();
            require(!attacker->wasDeviated() && bot->reserveDamagedUnitForRepair(groundOnly),
                "Reverting the deviation disturbed ordinary repair reservation");
            attacker->setHealth(attacker->getMaxHealth());
            groundOnly->setHealth(groundOnly->getMaxHealth());
        }
        // ---- Mobile anti-air production floor ---------------------------
        //
        // Observed hostile aircraft, and only observed hostile aircraft, give an
        // autonomous Custom Hard/Brutal house a launcher share of at least the
        // floor. The share itself comes out of the real production pass, so a
        // floor that is applied after the per-type percentages are derived - and
        // therefore buys nothing - fails here.
        {
            bot->difficulty=QuantBot::Difficulty::Brutal;
            bot->supportMode=false;
            game.gameType=GameType::CustomGame;
            bot->gameMode=QuantBot::GameMode::Custom;
            require(bot->autonomousCustomHardBrutal(),
                "The anti-air floor fixture is not an autonomous Custom Hard/Brutal house");

            auto* factory=dynamic_cast<BuilderBase*>(owner->placeStructure(NONE_ID,Structure_HeavyFactory,
                origin.x+6,origin.y+2,true,true));
            require(factory,"Anti-air floor fixture heavy factory placement failed");
            auto cancelFactoryOrders=[&] {
                const int limit=factory->getProductionQueueSize();
                for(int n=0;n<limit && !factory->currentProductionQueue.empty();++n) {
                    const int before=factory->getProductionQueueSize();
                    factory->doCancelItem(factory->currentProductionQueue.front().itemID);
                    require(factory->getProductionQueueSize()<before,
                        "Fixture factory order could not be cancelled");
                }
            };
            if(std::find(structureList.begin(),structureList.end(),factory)==structureList.end())
                structureList.push_back(factory);
            // Launchers carry structure prerequisites of their own in some mods;
            // give the fixture the usual radar/light-factory chain so that
            // availability is decided by the mod's rules rather than by a
            // missing fixture building.
            auto* radar=owner->placeStructure(NONE_ID,Structure_Radar,
                origin.x+10,origin.y+2,true,true);
            if(radar && std::find(structureList.begin(),structureList.end(),radar)==structureList.end())
                structureList.push_back(radar);
            auto* light=dynamic_cast<BuilderBase*>(owner->placeStructure(NONE_ID,Structure_LightFactory,
                origin.x+14,origin.y+2,true,true));
            require(radar && light,"Launcher fixture missing producer prerequisites");
            if(std::find(structureList.begin(),structureList.end(),light)==structureList.end())
                structureList.push_back(light);

            auto* raider=enemy->createUnit(Unit_Ornithopter);
            require(raider,"Anti-air floor fixture aircraft creation failed");
            raider->deploy(origin+Coord(12,12));
            raider->setVisible(owner->getTeamID(),true);
            require(bot->visibleHostileAircraft()>0,
                "A visible hostile Ornithopter was not observed by the defending house");

            owner->addCredits(20000);

            // Availability boundary, bottom side: at a tech level that cannot
            // build launchers at all, an observed air threat must not conjure a
            // launcher share out of nothing.
            const int savedTech=currentGame->techLevel;
            currentGame->techLevel=1;
            bot->build(4000);
            SDL_Log("AIR_DEFENSE_DIAG: unavailable tech=1 launcher_bps=%d aircraft=%d",
                bot->lastUnitMixBps[2],bot->visibleHostileAircraft());
            require(bot->lastUnitMixBps[2]==0,
                "The anti-air floor funded a launcher share the house cannot build");

            auto* refinery=owner->placeStructure(NONE_ID,Structure_Refinery,
                origin.x+16,origin.y+14,true,true);
            require(refinery,"Production fixture refinery prerequisite failed");
            if(std::find(structureList.begin(),structureList.end(),refinery)==structureList.end())
                structureList.push_back(refinery);

            // Availability boundary, top side: once launchers are genuinely
            // producible the observed threat secures the floor. The share comes
            // out of the real production pass, so a floor applied after the
            // per-type percentages are derived would buy nothing and fail here.
            cancelFactoryOrders();
            currentGame->techLevel=8;
            factory->curUpgradeLev=factory->getMaxUpgradeLevel();
            factory->updateBuildList();
            light->curUpgradeLev=light->getMaxUpgradeLevel();
            light->updateBuildList();
            require(factory->isAvailableToBuild(Unit_Launcher),
                "The production fixture did not unlock real launcher prerequisites");
            require(factory->isAvailableToBuild(Unit_Tank),
                "The production fixture did not unlock its competing tank choice");
            // Isolate military allocation from separate harvester/MCV service
            // orders; all competing combat choices retain their real gates.
            factory->buildList.remove_if([](const BuildItem& item){
                return item.itemID==Unit_Harvester || item.itemID==Unit_MCV;
            });

            // Fabricate a measured history which undervalues launchers. The
            // factory and orders are real; only the historical combat ledger is
            // controlled so the floor must bind rather than passing on priors.
            std::array<CombatReward::Totals,Num_ItemID> savedRewards;
            std::array<int,Num_ItemID> savedLosses;
            std::copy(std::begin(owner->combatRewards),std::end(owner->combatRewards),savedRewards.begin());
            std::copy(std::begin(owner->numItemLosses),std::end(owner->numItemLosses),savedLosses.begin());
            std::fill(std::begin(owner->combatRewards),std::end(owner->combatRewards),CombatReward::Totals{});
            std::fill(std::begin(owner->numItemLosses),std::end(owner->numItemLosses),0);
            owner->combatRewards[Unit_Tank].damageMilli=200000000;
            owner->combatRewards[Unit_Quad].damageMilli=30000000;
            for(int item=Unit_FirstID;item<=Unit_LastID;++item)
                if(item!=Unit_Tank && item!=Unit_Quad) owner->numItemLosses[item]=1000;
            const auto savedHistory=bot->performanceHistory;
            bot->performanceHistory={};
            const auto floorUnits=unitList;
            unitList.clear();
            unitList.push_back(raider);
            // An army near the learned tank quota makes the real heavy-factory
            // choice switch from a tank to mobile AA when the floor binds.
            std::vector<UnitBase*> floorTanks;
            const int tankPrice=game.objectData.data[Unit_Tank][owner->getHouseID()].price;
            const int tankCount=(6000+tankPrice-1)/tankPrice;
            for(int n=0;n<tankCount;++n) {
                auto* tank=owner->createUnit(Unit_Tank);
                require(tank,"Production fixture could not create its funded tank army");
                tank->deploy(origin+Coord(1+n%20,18+n/20));
                floorTanks.push_back(tank); // createUnit already publishes it.
            }
            const int fundedArmy=tankCount*tankPrice;
            // Production counts are House state, not just unitList. Isolate
            // them to this roster as well, then restore the prior fixture state.
            std::array<int,Num_ItemID> savedCounts;
            std::copy(std::begin(owner->numItem),std::end(owner->numItem),savedCounts.begin());
            const int savedNumUnits=owner->numUnits;
            for(int item=Unit_FirstID;item<=Unit_LastID;++item) owner->numItem[item]=0;
            owner->numUnits=0;
            for(const auto* unit:unitList) if(unit->getOwner()==owner) {
                ++owner->numItem[unit->getItemID()];
                ++owner->numUnits;
            }
            cancelFactoryOrders();
            const auto savedLimit=bot->militaryValueLimit;
            bot->militaryValueLimit=10000;
            bot->difficulty=QuantBot::Difficulty::Hard;
            const auto savedStartingCash=owner->startingCredits;
            owner->startingCredits+=100000;
            raider->setVisible(owner->getTeamID(),false);
            bot->lastStatsLogCycle=game.getGameCycleCount()-MILLI2CYCLES(30000);
            bot->build(fundedArmy);
            const int beforeFloor=bot->lastUnitMixBps[2];
            SDL_Log("AIR_DEFENSE_DIAG: learned_launcher_bps=%d tank=%d quad=%d",
                beforeFloor,bot->lastUnitMixBps[0],bot->lastUnitMixBps[7]);
            require(beforeFloor<UnitMixPolicy::kMobileAntiAirFloorBps,
                "The engine fixture did not create a learned-down launcher mix");
            SDL_Log("AIR_DEFENSE_DIAG: quiet_factory queue=%d first=%d upgrading=%d hold=%d credits=%d budget=%d tanks=%d launchers=%d infantry=%d",
                factory->getProductionQueueSize(),factory->currentProductionQueue.empty()?-1:int(factory->currentProductionQueue.front().itemID),
                int(factory->isUpgrading()),int(factory->isOnHold()),owner->getCredits(),bot->militaryValueLimit,
                owner->getNumItems(Unit_Tank),owner->getNumItems(Unit_Launcher),owner->getNumItems(Unit_Soldier));
            require(!factory->currentProductionQueue.empty()
                    && factory->currentProductionQueue.front().itemID==Unit_Tank,
                "Without visible air, the real factory did not follow the learned tank allocation");
            cancelFactoryOrders();
            raider->setVisible(owner->getTeamID(),true);
            bot->lastStatsLogCycle=game.getGameCycleCount()-MILLI2CYCLES(30000);
            bot->build(fundedArmy);
            require(bot->lastUnitMixBps[2]==UnitMixPolicy::kMobileAntiAirFloorBps,
                "The visible air threat did not bind the launcher floor in the real build pass");
            const bool queuedLauncher=!factory->currentProductionQueue.empty()
                && factory->currentProductionQueue.front().itemID==Unit_Launcher;
            SDL_Log("AIR_DEFENSE_DIAG: binding_floor before=%d after=%d queued_launcher=%d",
                beforeFloor,bot->lastUnitMixBps[2],int(queuedLauncher));
            require(queuedLauncher,"The protected launcher mix did not cause a real factory order");
            std::copy(savedRewards.begin(),savedRewards.end(),std::begin(owner->combatRewards));
            std::copy(savedLosses.begin(),savedLosses.end(),std::begin(owner->numItemLosses));
            bot->performanceHistory=savedHistory;
            bot->militaryValueLimit=savedLimit;
            owner->startingCredits=savedStartingCash;
            std::copy(savedCounts.begin(),savedCounts.end(),std::begin(owner->numItem));
            owner->numUnits=savedNumUnits;
            for(auto* tank:floorTanks) tank->setHealth(0);
            unitList=floorUnits;
            SDL_Log("AIR_DEFENSE_DIAG: available tech=8 launcher_bps=%d aircraft=%d",
                bot->lastUnitMixBps[2],bot->visibleHostileAircraft());
            currentGame->techLevel=savedTech;

            // The hostile/alive/visible conjunction in visibleHostileAircraft()
            // is deliberately not re-asserted against absolute counts here: this
            // fixture already carries several visible hostile aircraft from the
            // earlier phases, so a differential on one more would only be
            // measuring fixture bookkeeping. Our own aircraft, dead aircraft and
            // unseen aircraft are excluded by that single conjunction.
            require(bot->visibleHostileAircraft()>0,
                "The anti-air floor fixture lost its observed air threat");

            // Authority boundaries: campaign, support mode and Easy/Medium are
            // authored behaviour and never acquire the floor.
            game.gameType=GameType::Campaign;
            require(!bot->autonomousCustomHardBrutal(),
                "The anti-air floor leaked into a campaign game");
            game.gameType=GameType::CustomGame;
            bot->supportMode=true;
            require(!bot->autonomousCustomHardBrutal(),
                "The anti-air floor leaked into a human-helper support house");
            bot->supportMode=false;
            for(const auto soft:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium}) {
                bot->difficulty=soft;
                require(!bot->autonomousCustomHardBrutal(),
                    "The anti-air floor leaked into Easy or Medium");
            }
            bot->difficulty=QuantBot::Difficulty::Brutal;

            raider->setHealth(0);
            // A launcher borrowed from an existing wave must resume that wave,
            // rather than remaining at a dead aircraft's old guard post.
            auto* waveLauncher=owner->createUnit(Unit_Launcher);
            require(waveLauncher,"Wave resumption fixture failed to create a launcher");
            waveLauncher->deploy(origin+Coord(3,12));
            bot->manualUnitOrders.erase(waveLauncher->getObjectID());
            bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
            bot->localSquadWithdraw=false;
            bot->lastSurveyCycle=game.getGameCycleCount();
            bot->customWave.launched=game.getGameCycleCount();
            bot->customWave.members.insert(waveLauncher->getObjectID());
            waveLauncher->doSetAttackMode(AREAGUARD);
            waveLauncher->setForced(false);
            waveLauncher->setTarget(nullptr);
            bot->defenceAssignments[waveLauncher->getObjectID()]=raider->getObjectID();
            bot->updateCustomWave();
            bot->checkAllUnits();
            require(waveLauncher->getAttackMode()==HUNT
                    && bot->customWave.members.count(waveLauncher->getObjectID())>0,
                "The ended air-defense assignment stranded its tracked wave launcher");
            require(bot->defenceAssignments.count(waveLauncher->getObjectID())==0,
                "The ended air contact retained its launcher assignment");
            SDL_Log("AIR_DEFENSE_DIAG: ended_contact_wave_resumed=1");
            raid->setHealth(raid->getMaxHealth());
            raid->setVisible(owner->getTeamID(),true);
            const Coord frontPoint=raid->getClosestPoint(waveLauncher->getLocation());
            currentGameMap->getTile(frontPoint)->setExplored(owner->getHouseID(),game.getGameCycleCount());
            bot->customWave.front=raid->getObjectID();
            waveLauncher->setTarget(nullptr);
            waveLauncher->doSetAttackMode(GUARD);
            waveLauncher->setForced(false);
            bot->updateCustomWave();
            require(waveLauncher->getAttackMode()==HUNT && waveLauncher->getTarget()==raid
                    && bot->customWave.members.count(waveLauncher->getObjectID())>0,
                "An idle committed launcher did not rejoin its observed reachable wave front");
            waveLauncher->setTarget(nullptr);
            waveLauncher->doSetAttackMode(GUARD);
            bot->onHumanUnitOrder(waveLauncher->getObjectID());
            bot->updateCustomWave();
            require(waveLauncher->getAttackMode()==GUARD && !waveLauncher->hasATarget(),
                "The wave rejoin fallback overwrote a human guard order");
            bot->manualUnitOrders.erase(waveLauncher->getObjectID());
            bot->customWave.members.insert(waveLauncher->getObjectID());
            bot->armyPosture=ArmyPosturePolicy::Posture::Recovering;
            bot->updateCustomWave();
            require(waveLauncher->getAttackMode()==GUARD && !waveLauncher->hasATarget(),
                "The wave rejoin fallback sent a recovering launcher forward");
            bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
            bot->customWave.members.erase(waveLauncher->getObjectID());
            bot->updateCustomWave();
            require(waveLauncher->getAttackMode()==GUARD && !waveLauncher->hasATarget(),
                "The rejoin fallback recruited an undispatched home reserve");
            SDL_Log("AIR_DEFENSE_DIAG: idle_wave_rejoined=1 manual_recovery_home_reserve_preserved=1");
            waveLauncher->setHealth(0);
            if(factory->getHealth()>0) factory->setHealth(0);
            if(radar && radar->getHealth()>0) radar->setHealth(0);
            if(light && light->getHealth()>0) light->setHealth(0);
            if(refinery->getHealth()>0) refinery->setHealth(0);
        }
        // ---- City Troopers: the close-range part of the answer ----------
        //
        // A city colony's own producers are construction yards and a WOR; the
        // launcher share only exists where a vehicle factory can make launchers.
        // Troopers supplement that share and the rocket turrets. An observed
        // hostile wing - and nothing else - lets a city order rocket infantry,
        // inside the ordinary infantry quota (which counts queued orders and the
        // order being placed) and out of cash its working reserve does not need.
        {
            const bool savedCitySim=game.citySimEnabled_;
            const int savedTech=currentGame->techLevel;
            const auto savedLimit=bot->militaryValueLimit;
            const auto savedStored=owner->storedCredits;
            const auto savedStarting=owner->startingCredits;
            const auto savedCityCredits=owner->cityCredits;
            const int savedPower=owner->producedPower;
            auto setCityCredits=[&](int amount) {
                owner->storedCredits=0;owner->cityCredits=0;owner->startingCredits=amount;
            };
            std::array<int,Num_ItemID> savedCounts;
            std::copy(std::begin(owner->numItem),std::end(owner->numItem),savedCounts.begin());
            const int savedNumUnits=owner->numUnits;
            const auto cityUnits=unitList;
            const auto cityStructures=structureList;
            game.citySimEnabled_=true;
            if(!game.citySimulation_) {
                game.citySimulation_=std::make_unique<DuneCity::CitySimulation>();
                game.citySimulation_->init(currentGameMap->getSizeX(),currentGameMap->getSizeY());
            }
            // This diagnostic starts from a campaign roster; enable the city-zone
            // products alongside the city simulation, as the city cap probe does.
            for(const Uint32 zoneItem:{Structure_ZoneResidential,Structure_ZoneCommercial,
                                       Structure_ZoneIndustrial}) {
                auto& zone=game.objectData.data[zoneItem][owner->getHouseID()];
                zone.enabled=true;zone.builder=Structure_ConstructionYard;
                zone.techLevel=1;zone.upgradeLevel=0;zone.prerequisiteStructuresSet.reset();
            }
            currentGame->techLevel=8;
            bot->difficulty=QuantBot::Difficulty::Brutal;
            bot->supportMode=false;
            game.gameType=GameType::CustomGame;
            bot->gameMode=QuantBot::GameMode::Custom;
            bot->militaryValueLimit=10000;
            owner->producedPower=100000;
            require(bot->autonomousCustomHardBrutal(),
                "The city trooper fixture is not an autonomous Custom Hard/Brutal house");

            auto* cityYard=dynamic_cast<BuilderBase*>(owner->placeStructure(NONE_ID,
                Structure_ConstructionYard,origin.x+2,origin.y+22,true,true));
            auto* worA=dynamic_cast<BuilderBase*>(owner->placeStructure(NONE_ID,
                Structure_WOR,origin.x+6,origin.y+22,true,true));
            auto* worB=dynamic_cast<BuilderBase*>(owner->placeStructure(NONE_ID,
                Structure_WOR,origin.x+10,origin.y+22,true,true));
            require(cityYard && worA && worB,"City trooper fixture producer placement failed");
            for(auto* producer:{cityYard,worA,worB}) {
                producer->curUpgradeLev=producer->getMaxUpgradeLevel();
                producer->upgrading=false;
                producer->updateBuildList();
            }
            // Mods differ over which of the two offers a WOR carries, so the fixture
            // takes whichever the real build list actually has. A finished
            // Unit_Troopers order deploys three Unit_Trooper, which is why the squad
            // price is what the quota accounts for while it is queued.
            const int trooperValue=game.objectData.data[Unit_Trooper][owner->getHouseID()].price;
            const int squadValue=game.objectData.data[Unit_Troopers][owner->getHouseID()].price;
            const bool trooperOffered=worA->isAvailableToBuild(Unit_Trooper) && trooperValue>0;
            const bool squadOffered=worA->isAvailableToBuild(Unit_Troopers) && squadValue>0;
            SDL_Log("AIR_DEFENSE_DIAG: city_wor_offers trooper=%d/%d squad=%d/%d",
                int(trooperOffered),trooperValue,int(squadOffered),squadValue);
            require(trooperOffered || squadOffered,
                "The city fixture WOR offers no rocket infantry at all, so this tests nothing");
            // The production pass prefers the first offer while both counts are equal.
            const int firstValue=trooperOffered ? trooperValue : squadValue;
            const int cheapestValue=std::min(trooperOffered ? trooperValue : std::numeric_limits<int>::max(),
                                             squadOffered ? squadValue : std::numeric_limits<int>::max());

            auto* cityRaider=enemy->createUnit(Unit_Ornithopter);
            require(cityRaider,"City trooper fixture aircraft creation failed");
            cityRaider->deploy(origin+Coord(14,22));
            unitList.clear();unitList.push_back(cityRaider);
            structureList.clear();
            structureList.push_back(cityYard);structureList.push_back(worA);structureList.push_back(worB);
            for(int item=Unit_FirstID;item<=Unit_LastID;++item) owner->numItem[item]=0;
            owner->numUnits=0;
            auto clearCityQueues=[&] {
                for(auto* producer:{cityYard,worA,worB})
                    while(producer->getProductionQueueSize()>0)
                        producer->doCancelItem(producer->getCurrentProducedItem());
            };
            auto troopOrders=[&] {
                int orders=0;
                for(const auto* producer:{worA,worB})
                    for(const auto& queued:producer->currentProductionQueue)
                        if(queued.itemID==Unit_Trooper || queued.itemID==Unit_Troopers) ++orders;
                return orders;
            };
            auto cityPass=[&](int armyValue) {
                bot->lastStatsLogCycle=game.getGameCycleCount()-MILLI2CYCLES(30000);
                bot->clearPlacementCache();
                bot->build(armyValue);
            };
            // Fixture sizing only: the smallest army value whose infantry share has
            // room for one Trooper and no room for a second of either offer. The
            // assertions below count actual orders, not this arithmetic.
            const int cityInfantryPercent=10;     // Brutal
            auto admits=[&](int64_t owned,int64_t army,int64_t value) {
                return (owned+value)*100 <= (army+value)*cityInfantryPercent;
            };
            int oneTrooperArmy=0;
            while(oneTrooperArmy<2000000 && !admits(0,oneTrooperArmy,firstValue)) ++oneTrooperArmy;
            require(oneTrooperArmy<2000000
                    && !admits(firstValue,oneTrooperArmy+firstValue,cheapestValue),
                "No army value leaves room for exactly one city Trooper");
            // Take the top of that band, not its floor. A per-producer quota that only
            // looked at the share already owned would admit a second order here, so the
            // single-order assertion below actually distinguishes the aggregate rule.
            while(oneTrooperArmy+1<2000000
                && !admits(firstValue,oneTrooperArmy+1+firstValue,cheapestValue))
                ++oneTrooperArmy;
            const int roomyArmy=oneTrooperArmy*4;
            setCityCredits(200000);

            // No air at all, then an aircraft the house cannot see: neither is a
            // reason for a city to raise infantry.
            cityRaider->setVisible(owner->getTeamID(),false);
            require(bot->visibleHostileAircraft()==0,
                "The invisible city fixture aircraft was still observed");
            clearCityQueues();cityPass(roomyArmy);
            SDL_Log("AIR_DEFENSE_DIAG: city_invisible_air_troop_orders=%d",troopOrders());
            require(troopOrders()==0,
                "A city raised rocket infantry for an aircraft it cannot see");
            cityRaider->setHealth(0);
            clearCityQueues();cityPass(roomyArmy);
            require(troopOrders()==0,"A city raised rocket infantry with no hostile aircraft at all");
            cityRaider->setHealth(cityRaider->getMaxHealth());
            cityRaider->setVisible(owner->getTeamID(),true);
            require(bot->visibleHostileAircraft()>0,
                "The city fixture lost its observed air threat");

            // Visible air: the WOR really orders rocket infantry.
            clearCityQueues();cityPass(roomyArmy);
            const int roomyOrders=troopOrders();
            SDL_Log("AIR_DEFENSE_DIAG: city_visible_air_troop_orders=%d army=%d trooper=%d squad=%d",
                roomyOrders,roomyArmy,trooperValue,squadValue);
            require(roomyOrders>0,
                "An observed hostile wing did not make a city WOR produce rocket infantry");

            // The quota is aggregate: a second WOR in the same pass finds it full,
            // and a later pass still counts the order already queued.
            clearCityQueues();cityPass(oneTrooperArmy);
            require(troopOrders()==1,
                "Two city WORs each claimed a full infantry share in one pass");
            cityPass(oneTrooperArmy);
            SDL_Log("AIR_DEFENSE_DIAG: city_quota_orders_after_second_pass=%d army=%d",
                troopOrders(),oneTrooperArmy);
            require(troopOrders()==1,
                "A later city pass ignored the trooper order already queued");

            // Cash: the price of a trooper alone does not buy one, because the
            // city's working reserve and the strategic reserves come first.
            clearCityQueues();
            setCityCredits(firstValue+100);
            cityPass(roomyArmy);
            SDL_Log("AIR_DEFENSE_DIAG: city_poor_troop_orders=%d credits=%d",
                troopOrders(),owner->getCredits());
            require(troopOrders()==0,
                "A city spent its working and strategic reserves on rocket infantry");
            setCityCredits(200000);
            clearCityQueues();cityPass(roomyArmy);
            require(troopOrders()>0,"Restoring the city's cash did not restore the order");

            // Authority boundaries: Easy/Medium, a support helper and a campaign
            // game are authored behaviour and never raise city infantry.
            for(const auto soft:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium}) {
                bot->difficulty=soft;
                clearCityQueues();cityPass(roomyArmy);
                require(troopOrders()==0,"Easy or Medium raised city rocket infantry");
            }
            bot->difficulty=QuantBot::Difficulty::Brutal;
            bot->supportMode=true;
            clearCityQueues();cityPass(roomyArmy);
            require(troopOrders()==0,"A support helper raised city rocket infantry");
            bot->supportMode=false;
            game.gameType=GameType::Campaign;
            bot->gameMode=QuantBot::GameMode::Campaign;
            clearCityQueues();cityPass(roomyArmy);
            require(troopOrders()==0,"A campaign game raised city rocket infantry");
            game.gameType=GameType::CustomGame;
            bot->gameMode=QuantBot::GameMode::Custom;
            clearCityQueues();cityPass(roomyArmy);
            require(troopOrders()>0,
                "Restoring the autonomous Custom Brutal house lost the city trooper order");
            SDL_Log("AIR_DEFENSE_DIAG: city_trooper_bounds_checked=1");

            // Troopers are complementary: where a city can also build launchers, the
            // 20% mobile anti-air floor still binds in the same pass that orders them.
            {
                auto* cityFactory=dynamic_cast<BuilderBase*>(owner->placeStructure(NONE_ID,
                    Structure_HeavyFactory,origin.x+14,origin.y+26,true,true));
                require(cityFactory,"City launcher-floor fixture factory placement failed");
                for(int prereq=Structure_FirstID;prereq<=Structure_LastID;++prereq)
                    if(game.objectData.data[Unit_Launcher][owner->getHouseID()]
                            .prerequisiteStructuresSet[prereq] && owner->numItem[prereq]==0)
                        owner->numItem[prereq]=1;
                cityFactory->curUpgradeLev=cityFactory->getMaxUpgradeLevel();
                cityFactory->upgrading=false;
                cityFactory->updateBuildList();
                require(cityFactory->isAvailableToBuild(Unit_Launcher),
                    "The city fixture factory cannot build launchers, so the floor proves nothing");
                structureList.push_back(cityFactory);
                clearCityQueues();
                while(cityFactory->getProductionQueueSize()>0)
                    cityFactory->doCancelItem(cityFactory->getCurrentProducedItem());
                cityPass(roomyArmy);
                SDL_Log("AIR_DEFENSE_DIAG: city_launcher_floor_bps=%d troop_orders=%d",
                    bot->lastUnitMixBps[2],troopOrders());
                require(bot->lastUnitMixBps[2]>=UnitMixPolicy::kMobileAntiAirFloorBps,
                    "City rocket infantry weakened the mobile anti-air launcher floor");
                require(troopOrders()>0,
                    "A producible launcher share replaced the city's close-range troopers");
                while(cityFactory->getProductionQueueSize()>0)
                    cityFactory->doCancelItem(cityFactory->getCurrentProducedItem());
                structureList.remove(cityFactory);
                cityFactory->setHealth(0);
            }

            // The WOR itself: a city founds the producer only under the same demand
            // and only when its troop content is actually enabled and unlocked.
            {
                clearCityQueues();
                structureList.clear();structureList.push_back(cityYard);
                owner->numItem[Structure_WOR]=0;
                // Isolate the rule under test from the rest of the city ladder the
                // same way the launcher-floor fixture isolates military allocation:
                // leave the yard only the WOR (and the slabs a foundation plan may
                // need), and satisfy the opening worker fleet that outranks it. Every
                // gate on the WOR rule itself is still the shipped one.
                owner->numItem[Unit_Harvester]=8;
                owner->numItem[Unit_Carryall]=2;
                // The WOR's own content prerequisites differ by mod and house; grant
                // exactly the ones its real data table names, then let the engine
                // rebuild the offer list before it is narrowed.
                for(int prereq=Structure_FirstID;prereq<=Structure_LastID;++prereq)
                    if(game.objectData.data[Structure_WOR][owner->getHouseID()]
                            .prerequisiteStructuresSet[prereq] && owner->numItem[prereq]==0)
                        owner->numItem[prereq]=1;
                cityYard->updateBuildList();
                cityYard->buildList.remove_if([](const BuildItem& offer) {
                    return offer.itemID!=Structure_WOR && offer.itemID!=Structure_Slab1
                        && offer.itemID!=Structure_Slab4;
                });
                require(cityYard->isAvailableToBuild(Structure_WOR),
                    "The city fixture yard cannot offer a WOR at all, so this tests nothing");
                auto yardOrdersWor=[&] {
                    for(const auto& queued:cityYard->currentProductionQueue)
                        if(queued.itemID==Structure_WOR) return true;
                    return false;
                };
                auto& trooperSpec=game.objectData.data[Unit_Trooper][owner->getHouseID()];
                auto& squadSpec=game.objectData.data[Unit_Troopers][owner->getHouseID()];
                const bool savedTrooperEnabled=trooperSpec.enabled;
                const bool savedSquadEnabled=squadSpec.enabled;
                cityRaider->setVisible(owner->getTeamID(),false);
                clearCityQueues();cityPass(roomyArmy);
                require(!yardOrdersWor(),"A city founded a WOR for an aircraft it cannot see");
                cityRaider->setVisible(owner->getTeamID(),true);
                trooperSpec.enabled=false;squadSpec.enabled=false;
                clearCityQueues();cityPass(roomyArmy);
                require(!yardOrdersWor(),
                    "A city founded a WOR with no enabled troop content to make in it");
                trooperSpec.enabled=savedTrooperEnabled;squadSpec.enabled=savedSquadEnabled;
                const auto savedTroopPrerequisites=trooperSpec.prerequisiteStructuresSet;
                const auto savedSquadPrerequisites=squadSpec.prerequisiteStructuresSet;
                int missingTroopPrerequisite=-1;
                for(int item=Structure_FirstID;item<=Structure_LastID;++item)
                    if(item!=Structure_WOR && owner->getNumItems(item)==0) {
                        missingTroopPrerequisite=item;break;
                    }
                require(missingTroopPrerequisite>=0,
                    "The WOR fixture needs an absent troop prerequisite");
                trooperSpec.prerequisiteStructuresSet.set(missingTroopPrerequisite);
                squadSpec.prerequisiteStructuresSet.set(missingTroopPrerequisite);
                clearCityQueues();cityPass(roomyArmy);
                require(!yardOrdersWor(),
                    "A city founded a WOR whose troops lack another required building");
                trooperSpec.prerequisiteStructuresSet=savedTroopPrerequisites;
                squadSpec.prerequisiteStructuresSet=savedSquadPrerequisites;
                SDL_Log("AIR_DEFENSE_DIAG: city_wor_missing_troop_prerequisite_blocked=1");
                clearCityQueues();
                bool founded=false;
                for(int pass=0;pass<6 && !founded;++pass) {
                    cityPass(roomyArmy);
                    founded=yardOrdersWor();
                    if(!founded) clearCityQueues();
                }
                SDL_Log("AIR_DEFENSE_DIAG: city_wor_founded=%d",int(founded));
                require(founded,
                    "An observed hostile wing never let a city found the WOR its troopers need");
                clearCityQueues();
            }

            cityRaider->setHealth(0);
            for(auto* producer:{cityYard,worA,worB}) if(producer->getHealth()>0) producer->setHealth(0);
            game.citySimEnabled_=savedCitySim;
            currentGame->techLevel=savedTech;
            bot->militaryValueLimit=savedLimit;
            owner->storedCredits=savedStored;owner->startingCredits=savedStarting;
            owner->cityCredits=savedCityCredits;
            owner->producedPower=savedPower;
            std::copy(savedCounts.begin(),savedCounts.end(),std::begin(owner->numItem));
            owner->numUnits=savedNumUnits;
            unitList=cityUnits;structureList=cityStructures;
            bot->difficulty=QuantBot::Difficulty::Brutal;
        }

        // === Difficulty policy for autonomous aircraft ===
        // Offensive air hunts are Hard and Brutal. Easy and Medium may answer an
        // attack on a building or worker of their own, and may strike an enemy
        // base building that no observed anti-air covers. Everything below uses
        // the shipped settings for the difficulty under test: the policy must be
        // keyed to the difficulty itself, not to a flag a settings copy can flip.
        {
            auto policyUnits=unitList;auto policyStructures=structureList;
            const auto& config=getQuantBotConfig();
            bot->manualUnitOrders.clear();
            bot->defenceAssignments.clear();bot->defenceResponseCycles.clear();
            bot->aircraftTargetAuthority.clear();bot->assetAttackerContacts.clear();
            auto* bird=air;
            bird->setHealth(bird->getMaxHealth());
            base->setHealth(base->getMaxHealth());
            raid->setHealth(raid->getMaxHealth());
            raid->setVisible(owner->getTeamID(),true);
            // A fresh worker of ours: the fixture's original one has driven off
            // during the earlier real-frame sections.
            auto* asset=spawn(owner,Unit_Harvester,Coord(19,19));
            asset->setVisible(VIS_ALL,true);
            bot->doSetAttackMode(asset,STOP);
            auto* wanderer=spawn(enemy,Unit_Tank,Coord(14,20));
            wanderer->doSetAttackMode(STOP);
            unitList.clear();structureList.clear();
            structureList.push_back(base);structureList.push_back(raid);
            unitList.push_back(bird);unitList.push_back(asset);unitList.push_back(wanderer);
            // The wanderer stands close enough to our harvester that the Hard
            // proximity rank applies to it, which is exactly what the lower
            // difficulties must not act on.
            require(blockDistance(wanderer->getLocation(),
                        asset->getClosestPoint(wanderer->getLocation())).lround()
                    <= wanderer->getWeaponRange()+3,
                "The policy fixture's wanderer is not inside the proximity band it has to test");
            auto flyFor=[&](QuantBot::Difficulty difficulty) {
                bot->difficulty=difficulty;
                bot->doSetAttackMode(bird,STOP);
                bot->tryLaunchOrnithopterStrike(config.getSettings(static_cast<int>(difficulty)),config);
            };
            auto strikeAt=[&](QuantBot::Difficulty difficulty) {
                bot->difficulty=difficulty;
                return bot->tryLaunchOrnithopterStrike(
                    config.getSettings(static_cast<int>(difficulty)),config);
            };

            // --- A non-threatening enemy unit near our assets: denied on Easy
            // and Medium, still answered on Hard.
            for(auto difficulty:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium}) {
                flyFor(difficulty);
                require(bird->getTarget()!=wanderer,
                    "Easy/Medium attacked an enemy unit that was not attacking anything of ours");
                require(bird->getTarget()==raid,
                    "Easy/Medium did not take the undefended enemy base building instead");
                require(bird->getAttackMode()==STOP && bird->wasForced(),
                    "The lower-difficulty raid was not issued as an explicit forced order");
                // A repeated pass with nothing changed issues no further orders.
                require(!strikeAt(difficulty),
                    "A repeated identical air pass re-issued orders instead of holding");
            }
            flyFor(QuantBot::Difficulty::Hard);
            require(bird->getTarget()==wanderer,
                "Hard lost its proximity defence against an enemy unit beside our harvester");
            flyFor(QuantBot::Difficulty::Brutal);
            require(bird->getTarget()==wanderer,
                "Brutal lost its proximity defence against an enemy unit beside our harvester");

            // --- Actual defence is legal at every difficulty: the attacker of
            // a live harvester, and of the base, is a target on Easy.
            wanderer->doAttackObject(asset,true);
            flyFor(QuantBot::Difficulty::Easy);
            require(bird->getTarget()==wanderer,
                "Easy refused to defend its own harvester from the unit attacking it");
            wanderer->doSetAttackMode(STOP);wanderer->setTarget(nullptr);
            // The same contact between volleys: the onDamage ledger keeps it
            // legal for the grace, so a reload does not end the defence.
            bot->assetAttackerContacts.clear();
            flyFor(QuantBot::Difficulty::Easy);
            require(bird->getTarget()!=wanderer,
                "A contact with no target of its own stayed an Easy air target");
            bot->onDamage(base,1,wanderer->getObjectID());
            flyFor(QuantBot::Difficulty::Easy);
            require(bird->getTarget()==wanderer,
                "A confirmed recent attacker of our base was not a legal Easy defence target");
            game.gameCycleCount+=MILLI2CYCLES(QuantBot::kAircraftDefenceGraceMs)+1;
            flyFor(QuantBot::Difficulty::Easy);
            require(bird->getTarget()!=wanderer,
                "The bounded onDamage defence grace never expired");
            bot->assetAttackerContacts.clear();

            // --- Defence still works under anti-air cover.
            auto* guardTurret=enemy->placeStructure(NONE_ID,Structure_RocketTurret,
                origin.x+16,origin.y+17,true,true);
            require(guardTurret!=nullptr,"The policy fixture could not place its covering turret");
            guardTurret->setVisible(owner->getTeamID(),true);
            structureList.push_back(guardTurret);
            wanderer->setLocation(origin.x+18,origin.y+19);
            wanderer->doAttackObject(asset,true);
            require(blockDistance(guardTurret->getLocation(),wanderer->getLocation()).lround()
                    <= AirStrikePolicy::safetyRange(guardTurret->getWeaponRange()),
                "The covering turret does not cover the attacker, so this tests nothing");
            flyFor(QuantBot::Difficulty::Easy);
            require(bird->getTarget()==wanderer,
                "Anti-air cover suppressed a legal Easy defence of our own harvester");
            // The same turret denies the opportunistic raid, three times further
            // out than its ground range, because that is how far it shoots
            // aircraft. A raid is an opportunity, not a loss being answered.
            wanderer->doSetAttackMode(STOP);wanderer->setTarget(nullptr);
            structureList.remove(guardTurret);guardTurret->setVisible(VIS_ALL,false);
            guardTurret->setHealth(0);
            // A second turret outside its ground cover of the target but inside
            // the triple range it engages aircraft at. Placed where the geometry
            // actually holds, in a fixed search order, never moved afterwards.
            const int turretRange=currentGame->objectData.data[Structure_RocketTurret]
                [enemy->getHouseID()].weaponrange;
            Coord farSpot=Coord::Invalid();
            int turretToRaid=0;
            for(int dy=23;dy>=12 && farSpot.isInvalid();--dy) {
                const Coord spot(origin.x+10,origin.y+dy);
                if(!currentGameMap->tileExists(spot)) continue;
                const int reach=blockDistance(spot,raid->getClosestPoint(spot)).lround();
                if(reach>AirStrikePolicy::safetyRange(turretRange)
                    && reach<=AirStrikePolicy::antiAirSafetyRange(Structure_RocketTurret,turretRange)) {
                    farSpot=spot;turretToRaid=reach;
                }
            }
            require(farSpot.isValid(),
                "The triple-range fixture found no tile outside ground cover but inside air reach");
            auto* farTurret=enemy->placeStructure(NONE_ID,Structure_RocketTurret,
                farSpot.x,farSpot.y,true,true);
            require(farTurret!=nullptr,"The triple-range fixture could not place its turret");
            farTurret->setVisible(owner->getTeamID(),true);
            structureList.push_back(farTurret);
            SDL_Log("AIR_DEFENSE_DIAG: air_policy_triple_range turret_range=%d turret_to_target=%d"
                " ground_cover=%d air_cover=%d",
                turretRange,turretToRaid,AirStrikePolicy::safetyRange(turretRange),
                AirStrikePolicy::antiAirSafetyRange(Structure_RocketTurret,turretRange));
            flyFor(QuantBot::Difficulty::Easy);
            require(bird->getTarget()==nullptr,
                "An Easy raid flew at a base building inside a rocket turret's anti-air reach");
            structureList.remove(farTurret);farTurret->setVisible(VIS_ALL,false);
            farTurret->setHealth(0);

            // --- Rocket infantry is real anti-air and denies the same raid; an
            // ordinary tank beside the building is not and must not deny it.
            auto* trooper=spawn(enemy,Unit_Trooper,Coord(12,7));
            unitList.push_back(trooper);
            require(blockDistance(trooper->getLocation(),
                        raid->getClosestPoint(trooper->getLocation())).lround()
                    <= AirStrikePolicy::antiAirSafetyRange(Unit_Trooper,trooper->getWeaponRange()),
                "The rocket infantry fixture does not cover the building it guards");
            require(trooper->canAttack(bird),
                "The rocket infantry fixture cannot actually shoot at aircraft");
            flyFor(QuantBot::Difficulty::Easy);
            require(bird->getTarget()==nullptr,
                "An Easy raid flew at a base building guarded by rocket infantry");
            trooper->setHealth(0);
            flyFor(QuantBot::Difficulty::Easy);
            require(bird->getTarget()==raid,
                "Clearing the guarding rocket infantry did not re-open the raid");
            trooper->setVisible(VIS_ALL,false);unitList.remove(trooper);
            auto* bystander=spawn(enemy,Unit_Tank,Coord(12,7));
            unitList.push_back(bystander);
            bystander->doSetAttackMode(STOP);
            flyFor(QuantBot::Difficulty::Easy);
            require(bird->getTarget()==raid,
                "A non-anti-air enemy unit beside the building blocked an undefended raid");
            bystander->setVisible(VIS_ALL,false);unitList.remove(bystander);

            // --- No wing size buys passage on Easy: a launcher over the target
            // denies the raid however many aircraft are local.
            auto* guardLauncher=spawn(enemy,Unit_Launcher,Coord(12,7));
            unitList.push_back(guardLauncher);
            std::vector<UnitBase*> policyFleet;
            for(int i=0;i<8;++i) {
                auto* extra=spawn(owner,Unit_Ornithopter,Coord(9+i%4,9+i/4));
                bot->doSetAttackMode(extra,STOP);
                policyFleet.push_back(extra);unitList.push_back(extra);
            }
            auto flyWing=[&](QuantBot::Difficulty difficulty) {
                bot->difficulty=difficulty;
                for(auto* extra:policyFleet) bot->doSetAttackMode(extra,STOP);
                bot->doSetAttackMode(bird,STOP);
                bot->tryLaunchOrnithopterStrike(
                    config.getSettings(static_cast<int>(difficulty)),config);
            };
            flyWing(QuantBot::Difficulty::Easy);
            require(bird->getTarget()==nullptr,
                "A large Easy wing priced its way into launcher cover over a base building");
            for(auto* extra:policyFleet)
                require(extra->getTarget()==nullptr,
                    "A wing member raided into launcher cover on Easy");
            // The same wing and the same launcher on Hard: the established
            // four-aircraft-per-launcher ratio and launcher-first still apply,
            // so this is a difficulty policy and not a disabled planner.
            flyWing(QuantBot::Difficulty::Hard);
            require(policyFleet[0]->getTarget()==guardLauncher,
                "Hard's wing ratio and launcher-first no longer engage the covering launcher");
            for(auto* extra:policyFleet) {
                bot->doSetAttackMode(extra,STOP);
                extra->setVisible(VIS_ALL,false);unitList.remove(extra);extra->setHealth(0);
            }
            guardLauncher->setVisible(VIS_ALL,false);unitList.remove(guardLauncher);
            guardLauncher->setHealth(0);

            // --- The restriction survives below the planner: a stale forced
            // order is dropped by the hull, the engine searches never return the
            // illegal contact, and no shot is fired.
            wanderer->setLocation(origin.x+14,origin.y+20);
            wanderer->doSetAttackMode(STOP);wanderer->setTarget(nullptr);
            wanderer->setHealth(wanderer->getMaxHealth());
            bot->difficulty=QuantBot::Difficulty::Easy;
            bot->aircraftTargetAuthority.clear();bot->assetAttackerContacts.clear();
            // A spectator joining between planner passes must carry both an
            // admitted structure sortie and a recent confirmed asset hit.
            bot->aircraftTargetAuthority[bird->getObjectID()]=raid->getObjectID();
            bot->assetAttackerContacts[wanderer->getObjectID()]={game.gameCycleCount,true};
            const auto checkpoint=game.saveObserverRuntime();
            bot->aircraftTargetAuthority.clear();bot->assetAttackerContacts.clear();
            game.loadObserverRuntime(checkpoint);
            require(bot->aircraftTargetAuthority.at(bird->getObjectID())==raid->getObjectID(),
                "A spectator checkpoint lost an admitted air sortie");
            const auto restoredContact=bot->assetAttackerContacts.find(wanderer->getObjectID());
            require(restoredContact!=bot->assetAttackerContacts.end()
                    && restoredContact->second.cycle==game.gameCycleCount && restoredContact->second.base,
                "A spectator checkpoint lost a recent base-attack contact");
            bot->assetAttackerContacts.clear();
            // Even a stale cache entry from an earlier unit order may not keep
            // granting permission once that unit stops attacking our assets.
            bot->aircraftTargetAuthority[bird->getObjectID()]=wanderer->getObjectID();
            bot->doSetAttackMode(bird,STOP);bot->doAttackObject(bird,wanderer,true);
            require(bird->getTarget()==wanderer,"The stale-order fixture could not set its order");
            require(static_cast<Ornithopter*>(bird)->isVetoedAutonomousPrey(wanderer),
                "An unauthorized Easy air target bypassed the execution veto");
            const FixPoint wandererFull=wanderer->getHealth();
            for(int step=0;step<900;++step) {game.processObjects();++game.gameCycleCount;}
            require(bird->getTarget()!=wanderer,
                "A stale Easy air order survived without a planner pass");
            require(wanderer->getHealth()>=wandererFull,
                "An Easy aircraft fired at a contact it was not allowed to attack");
            for(auto mode:{STOP,GUARD,AREAGUARD,AMBUSH,HUNT}) {
                bot->doSetAttackMode(bird,mode);
                require(bird->findTarget()!=wanderer,
                    "An Easy autonomous air search returned a non-attacking enemy unit");
                for(int step=0;step<300;++step) {game.processObjects();++game.gameCycleCount;}
                require(bird->getTarget()!=wanderer,
                    "Autonomous reacquisition below the planner picked an illegal Easy target");
                require(wanderer->getHealth()>=wandererFull,
                    "An Easy aircraft fired after reacquiring an illegal target");
            }
            bot->doSetAttackMode(bird,STOP);
            // Hard acquires the very same contact through the same search, so
            // this is a difficulty policy and not a broken search.
            bot->difficulty=QuantBot::Difficulty::Easy;
            bot->doSetAttackMode(bird,HUNT);
            require(bird->findTarget()==nullptr,
                "An Easy whole-map air hunt still returned an unauthorized target");
            bot->difficulty=QuantBot::Difficulty::Hard;
            const ObjectBase* hardFound=bird->findTarget();
            SDL_Log("AIR_DEFENSE_DIAG: air_policy_hard_hunt_target=%d item=%d",
                hardFound?int(hardFound->getObjectID()):-1,
                hardFound?int(hardFound->getItemID()):-1);
            require(hardFound!=nullptr,
                "Hard's own autonomous air search returned nothing where Easy was restricted");
            bot->doSetAttackMode(bird,STOP);
            bot->difficulty=QuantBot::Difficulty::Easy;

            // --- An explicit human order keeps its authority on Easy.
            bot->onHumanUnitOrder(bird->getObjectID());
            bot->doAttackObject(bird,wanderer,true);
            require(!static_cast<Ornithopter*>(bird)->isVetoedAutonomousPrey(wanderer),
                "A human air order was classified as autonomous on Easy");
            bool manualAirHit=false;
            for(int step=0;step<4000 && !manualAirHit;++step) {
                game.processObjects();++game.gameCycleCount;
                manualAirHit=wanderer->getHealth()<wandererFull;
            }
            require(manualAirHit,"A human-ordered Easy aircraft never damaged its ordered target");
            bot->manualUnitOrders.clear();
            bot->doSetAttackMode(bird,STOP);

            wanderer->setVisible(VIS_ALL,false);wanderer->setHealth(0);
            asset->setVisible(VIS_ALL,false);asset->setHealth(0);
            bot->aircraftTargetAuthority.clear();bot->assetAttackerContacts.clear();
            bot->defenceAssignments.clear();bot->defenceResponseCycles.clear();
            unitList=policyUnits;structureList=policyStructures;
            bot->difficulty=QuantBot::Difficulty::Brutal;
            SDL_Log("AIR_DEFENSE_DIAG: difficulty_air_policy_checked=1");
        }
    } catch(...) {unitList=savedUnits;structureList=savedStructures;throw;}
    unitList=savedUnits;structureList=savedStructures;
    SDL_Log("AIR_DEFENSE_PASS: exposed ground units and infantry are raided and really damaged;"
        " mobile launcher cover is priced four aircraft per covering launcher, overlaps multiply it,"
        " and the launcher in the engagement area is hit first; wing counts exclude distant,"
        " retreating, human-ordered, damaged, inactive and unrespondable aircraft; hidden, dead and"
        " own launchers charge nothing; an arriving launcher takes the attack run and its death"
        " returns it; rocket turret and Deviator cover holds against eight;"
        " base/harvester attacks preempt raids and AA avoidance; damaged air still attacks,"
        " interceptions stay stable across volleys, base outranks remote rescue;"
        " proximity safety, resumption and human control preserved;"
        " launchers leave a ground skirmish and a rally point for aircraft over an outlying colony,"
        " reach holdable ground covering it through threatened approaches, fire in range without"
        " being force-moved, hold across ordinary unit passes and clear when the attack ends;"
        " deviated vehicles are not reserved for repair while our own damaged ones still are;"
        " a city raises rocket infantry only for an observed hostile wing, inside one aggregate"
        " infantry quota that counts queued orders, out of cash its working and strategic"
        " reserves do not need, never on Easy/Medium, in support mode or in a campaign, founds"
        " the WOR only under that demand and only with real troop content, and leaves the"
        " mobile anti-air launcher floor binding where launchers are producible;"
        " autonomous air offence is Hard/Brutal only: Easy/Medium deny a non-attacking enemy"
        " unit beside their own assets at the planner, in every engine search and at the shot,"
        " still defend a harvester and a base under anti-air cover including across a bounded"
        " reload grace, and may only strike an enemy base building that no observed rocket"
        " turret at triple anti-air range, launcher or rocket infantry covers - which no wing"
        " size overrides, which a non-anti-air bystander does not block, and which re-opens"
        " when the guard is cleared - while Hard keeps the same contacts and human orders stand");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=air_defense outcome=passed");
}
