// Real target selection with a tempting raid and simultaneous home/worker threat.
#include <players/AirStrikePolicy.h>

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
        " deviated vehicles are not reserved for repair while our own damaged ones still are");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=air_defense outcome=passed");
}
