// Real-engine Custom Hard/Brutal launcher spacing while hunting.
// Included only by run-campaign-balance.py; never linked into the game.
//
// Engine Hunt walks artillery into point-blank range, where a launcher dies
// without firing. The one exception restored here is a short escape from an
// enemy that is ALREADY too close. Everything measured below is real: real
// units on the real map, the production QuantBot entry points, and real game
// cycles, so "it moved away" means the gap actually opened rather than that a
// move command was accepted.
//
// What is checked:
//   1. A hunting launcher with a too-close dangerous enemy moves AWAY, keeps
//      engine Hunt, keeps its place in the tracked army, and can shoot again
//      across the gap it opened. Both an ordinary AI pass and the damage
//      callback can trigger it.
//   2. A distant enemy - including one that just hit the launcher - produces no
//      order at all.
//   3. Human orders, repair runs, carryall transit and a live defence contact
//      keep their units. Other hunters, Deviators, Easy/Medium and campaign
//      games are untouched.
void runLauncherKitingProbe(Game& game) {
    auto require=[](bool ok,const char* why){
        if(!ok){SDL_Log("LAUNCHER_KITE_FAIL: %s",why);throw std::runtime_error(why);}
    };
    auto* owner=game.getHouse(HOUSE_HARKONNEN);
    auto* enemy=game.getHouse(HOUSE_ATREIDES);
    require(owner && enemy,"Launcher fixture missing houses");
    QuantBot* bot=nullptr;
    for(const auto& player:owner->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get()))bot=candidate;
    require(bot,"Launcher fixture missing bot");

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

    // ---- Free ground, large enough for a stand-off and a real retreat -------
    Coord origin=Coord::Invalid();
    for(int y=2;y+30<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for(int x=2;x+40<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear=true;
            for(int dy=0;dy<30 && clear;++dy)for(int dx=0;dx<40 && clear;++dx) {
                const auto* tile=currentGameMap->getTile(x+dx,y+dy);
                if(tile->hasAGroundObject() || tile->isMountain())clear=false;
            }
            if(clear)origin=Coord(x,y);
        }
    require(origin.isValid(),"Launcher fixture needs a clear 40x30 area");

    std::vector<UnitBase*> roster;
    std::vector<Uint32> fixtureStructureIds;
    const auto savedUnits=unitList;
    auto publish=[&] {
        unitList.clear();
        for(auto* unit:roster) unitList.push_back(unit);
    };
    auto spawn=[&](House* house,Uint32 item,Coord offset) {
        auto* unit=house->createUnit(item);
        require(unit,"Launcher fixture unit creation failed");
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
    auto gap=[&](const UnitBase* a,const UnitBase* b){
        return blockDistance(a->getLocation(),b->getLocation()).lround();
    };

    try {
        // A real base with the repair yard the repair reservation needs.
        require(place(owner,Structure_ConstructionYard,Coord(2,2)),"Fixture yard placement failed");
        place(owner,Structure_HeavyFactory,Coord(6,2));
        place(owner,Structure_RepairYard,Coord(10,2));

        const int launcherRange=currentGame->objectData.data[Unit_Launcher]
            [owner->getHouseID()].weaponrange;
        require(launcherRange>2,"Fixture launcher has no stand-off range to keep");

        // The launcher under test and one closing enemy tank, out in the open
        // well away from the base so nothing else can explain a move.
        auto* launcher=spawn(owner,Unit_Launcher,Coord(20,20));
        auto* closer=spawn(enemy,Unit_Tank,Coord(22,20));
        publish();
        bot->dangerUpdated=std::numeric_limits<Uint32>::max();
        bot->refreshTacticalDanger();
        bot->squadRallyLocation=origin+Coord(4,5);
        bot->protectedRallyCycle=std::numeric_limits<Uint32>::max();
        bot->protectedRally=origin+Coord(4,5);
        bot->militaryValueLimit=1;

        const int reach=std::max(1,closer->getWeaponRange());
        const int band=std::min(reach+1,launcherRange/2+1);
        SDL_Log("LAUNCHER_KITE_DIAG: launcher_range=%d tank_reach=%d close_band=%d",
            launcherRange,reach,band);
        require(band>=1 && band<launcherRange,
            "The close band is not a short inner part of the launcher's own range");

        auto hunt=[&](UnitBase* unit,Coord at) {
            unit->setLocation(at);
            unit->setTarget(nullptr);
            // The move comes first: an unforced move downgrades Hunt to Guard,
            // which is exactly why the spacing helper restores the mission after
            // its own step.
            bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);
            bot->doSetAttackMode(unit,HUNT);
            unit->setForced(false);
            bot->customWave.members.insert(unit->getObjectID());
            publish();
        };

        for(const auto difficulty:{QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            bot->difficulty=difficulty;
            require(bot->recoveryActive(),"Launcher fixture difficulty is not a whole-army difficulty");

            // ---- Boundary: proximity decides, not damage ------------------
            closer->setLocation(origin+Coord(20+band,20));
            hunt(launcher,origin+Coord(20,20));
            require(bot->huntingLauncher(launcher),"A hunting launcher was not recognised");
            const int atBand=bot->launcherEscapeRange(launcher,closer);
            closer->setLocation(origin+Coord(20+band+1,20));
            const int pastBand=bot->launcherEscapeRange(launcher,closer);
            SDL_Log("LAUNCHER_KITE_DIAG: escape_at_band=%d escape_past_band=%d",atBand,pastBand);
            require(atBand>band && atBand<=launcherRange,
                "The escape stand-off is not outside the close band and inside the firing range");
            require(pastBand==0,"An enemy outside the close band still ordered an escape");

            // ---- 1. An ordinary AI pass: the gap actually opens ------------
            closer->setLocation(origin+Coord(21,20));
            hunt(launcher,origin+Coord(20,20));
            closer->doAttackObject(launcher,true);
            require(closer->getTarget()==launcher && closer->isInWeaponRange(launcher),
                "Fixture enemy is not shooting the launcher");
            const int before=gap(launcher,closer);
            require(before<=band,"Fixture enemy did not start inside the close band");
            bot->checkAllUnits();
            require(launcher->getAttackMode()==HUNT,
                "The escape replaced engine Hunt instead of keeping the mission");
            require(bot->customWave.members.count(launcher->getObjectID()),
                "The escape dropped the launcher out of the tracked army");
            const Coord away=launcher->getDestination();
            require(away.isValid() && blockDistance(away,closer->getLocation()).lround()>before,
                "The ordered destination is not further from the enemy than the launcher stands");
            // Real cycles, real pathfinding: separation has to actually grow.
            // The enemy is held where it stands so the measured gap can only
            // come from the launcher's own movement. It stays a unit that can
            // shoot the launcher, so it is still a threat by the same test.
            closer->doSetAttackMode(STOP);
            closer->setTarget(nullptr);
            closer->setForced(false);
            const Coord enemyAt=closer->getLocation();
            // The helper steps a bounded distance per pass, so the stand-off is
            // reached over consecutive ordinary passes rather than one teleport.
            // A handful of passes has to be enough; an unbounded loop would hide
            // a launcher that never actually gets away.
            int after=gap(launcher,closer), passes=0;
            for(;passes<8 && after<=band;++passes) {
                bot->checkAllUnits();
                tick(60);
                after=gap(launcher,closer);
            }
            require(closer->getLocation()==enemyAt,
                "Fixture enemy moved during the separation measurement");
            SDL_Log("LAUNCHER_KITE_DIAG: difficulty=%d scan before=%d after=%d passes=%d mode=%d tracked=%d",
                int(difficulty),before,after,passes,int(launcher->getAttackMode()),
                int(bot->customWave.members.count(launcher->getObjectID())));
            require(after>band,"The launcher never got outside the close band");
            require(after>before,"A too-close enemy did not actually increase the separation");
            require(after>reach,"The launcher stayed inside the enemy's own reach");
            require(after<=launcherRange && launcher->canAttack(closer),
                "The launcher escaped out of its own firing range instead of standing off");
            require(launcher->getAttackMode()==HUNT,
                "The launcher lost engine Hunt while opening the gap");
            require(bot->customWave.members.count(launcher->getObjectID()),
                "The launcher left the tracked army while opening the gap");
            // Renewed combat, not a withdrawal: the next pass issues no further
            // escape, and the launcher actually shoots from the gap it opened.
            // Being inside its own range would prove nothing on its own, so what
            // is measured is real damage landing on the enemy - a fired, flown
            // and arrived projectile - while the launcher holds its stand-off.
            require(!bot->escapeCloseThreatWhileHunting(launcher),
                "The launcher kept escaping after it was already standing off");
            const FixPoint enemyHealthBefore=closer->getHealth();
            int firingTicks=0;
            while(firingTicks<600 && closer->getHealth()>=enemyHealthBefore) {
                bot->checkAllUnits();
                tick(30);
                firingTicks+=30;
            }
            const int firingGap=gap(launcher,closer);
            SDL_Log("LAUNCHER_KITE_DIAG: difficulty=%d firing ticks=%d health=%d->%d gap=%d mode=%d",
                int(difficulty),firingTicks,enemyHealthBefore.lround(),
                closer->getHealth().lround(),firingGap,int(launcher->getAttackMode()));
            require(closer->getHealth()<enemyHealthBefore,
                "The launcher never actually hit the enemy again after breaking contact");
            require(firingGap>reach,
                "The launcher only resumed firing by closing back inside the enemy's reach");
            require(launcher->getAttackMode()==HUNT,
                "The launcher lost engine Hunt while resuming fire");
            closer->setHealth(closer->getMaxHealth());

            // ---- 2. The damage callback, close and distant -----------------
            closer->setLocation(origin+Coord(21,20));
            hunt(launcher,origin+Coord(20,20));
            bot->onDamage(launcher,10,closer->getObjectID());
            const Coord hitDestination=launcher->getDestination();
            require(hitDestination.isValid()
                && blockDistance(hitDestination,closer->getLocation()).lround()>gap(launcher,closer),
                "A hit from a too-close enemy did not move the launcher away");
            require(launcher->getAttackMode()==HUNT,
                "The damage callback replaced engine Hunt with Area Guard");
            require(bot->customWave.members.count(launcher->getObjectID()),
                "The damage callback dropped the launcher out of the tracked army");

            closer->setLocation(origin+Coord(20+band+2,20));
            hunt(launcher,origin+Coord(20,20));
            const Coord idle=launcher->getDestination();
            bot->onDamage(launcher,10,closer->getObjectID());
            require(launcher->getDestination()==idle && !launcher->wasForced()
                && launcher->getAttackMode()==HUNT,
                "A hit from a distant enemy retreated a hunting launcher");
            bot->checkAllUnits();
            require(launcher->getDestination()==idle && !launcher->wasForced()
                && launcher->getAttackMode()==HUNT,
                "An ordinary pass moved a launcher that no enemy was close to");

            // ---- 3. Authority that outranks fresh offence ------------------
            closer->setLocation(origin+Coord(21,20));
            hunt(launcher,origin+Coord(20,20));
            bot->onHumanUnitOrder(launcher->getObjectID());
            require(!bot->escapeCloseThreatWhileHunting(launcher),
                "The escape overrode a human order");
            bot->manualUnitOrders.clear();
            bot->customWave.members.insert(launcher->getObjectID());

            launcher->setHealth(launcher->getMaxHealth()/10);
            require(bot->reserveDamagedUnitForRepair(launcher),
                "Fixture launcher is not actually reserved for repair");
            require(!bot->escapeCloseThreatWhileHunting(launcher),
                "The escape took a launcher that belongs to the repair yard");
            launcher->setHealth(launcher->getMaxHealth());

            static_cast<GroundUnit*>(launcher)->setAwaitingPickup(true);
            require(!bot->escapeCloseThreatWhileHunting(launcher),
                "The escape ordered a launcher waiting for a carryall");
            static_cast<GroundUnit*>(launcher)->setAwaitingPickup(false);

            launcher->setGuardPoint(launcher->getLocation());
            bot->defenceAssignments[launcher->getObjectID()]=closer->getObjectID();
            require(bot->activeDefenceAssignment(launcher),
                "Fixture defence contact is not live");
            require(!bot->escapeCloseThreatWhileHunting(launcher),
                "The escape took a launcher off a live defence contact");
            bot->defenceAssignments.clear();

            // Still works right afterwards: none of the above left state behind.
            hunt(launcher,origin+Coord(20,20));
            require(bot->escapeCloseThreatWhileHunting(launcher),
                "The escape stopped working after the authority checks");
        }

        // ---- Emergency base defence keeps the spacing AND the firing --------
        //
        // A real attack on a building this house owns sends the whole army to
        // Hunt the invasion, and a launcher is part of that army. Being a
        // defender is not a reason to die at point-blank range inside its own
        // base: it still backs off an enemy that is already on top of it, stays
        // in Hunt, keeps its contact, and gets its shots away across the gap.
        // Everything below is measured over real cycles, and the launcher is the
        // only unit of ours on the map, so the damage can only be its own.
        {
            bot->difficulty=QuantBot::Difficulty::Brutal;
            bot->defenceAssignments.clear();
            bot->defenceResponseCycles.clear();
            bot->customWave.members.clear();
            bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
            auto* yard=dynamic_cast<StructureBase*>(currentGame->getObjectManager()
                .getObject(fixtureStructureIds.front()));
            require(yard && yard->getItemID()==Structure_ConstructionYard && yard->getHealth()>0,
                "Emergency fixture lost its construction yard");
            closer->setLocation(origin+Coord(5,5));
            closer->setHealth(closer->getMaxHealth());
            closer->doSetAttackMode(AREAGUARD);
            closer->doAttackObject(yard,true);
            require(closer->getTarget()==yard && closer->isInWeaponRange(yard),
                "Emergency fixture invader is not actually attacking the yard");
            hunt(launcher,origin+Coord(6,5));
            require(gap(launcher,closer)<=band,
                "Emergency fixture invader does not start inside the launcher's close band");
            bot->onDamage(yard,40,closer->getObjectID());
            require(launcher->getAttackMode()==HUNT,
                "The base emergency took its launcher out of engine Hunt");
            require(bot->defenceAssignments.count(launcher->getObjectID()),
                "The base emergency did not commit the launcher to the invasion");
            require(bot->escapeCloseThreatWhileHunting(launcher),
                "An emergency Hunt defender was refused its close-range spacing");

            const Coord escape=launcher->getDestination();
            require(launcher->wasForced() && !launcher->hasATarget(),
                "Emergency launcher fixture did not enter a real escape move");
            auto* worker=spawn(owner,Unit_Harvester,Coord(10,8));
            auto* workerRaider=spawn(enemy,Unit_Tank,Coord(9,8));
            workerRaider->doAttackObject(worker,true);
            publish();
            require(workerRaider->isInWeaponRange(worker),
                "Worker raid fixture is not actually in range");
            bot->defenceResponseCycles.clear();
            bot->scrambleUnitsAndDefend(workerRaider,false,worker);
            require(launcher->getAttackMode()==HUNT && launcher->wasForced()
                && launcher->getDestination()==escape && !launcher->hasATarget(),
                "A worker raid interrupted a base defender's launcher escape");
            // Keep the extra actors alive off-stage; removing them from the
            // published fixture list would strand their spatial-grid entries.
            workerRaider->setLocation(origin+Coord(35,28));
            workerRaider->doSetAttackMode(STOP);
            workerRaider->setTarget(nullptr);
            worker->setLocation(origin+Coord(38,28));
            worker->doSetAttackMode(STOP);

            const FixPoint invaderHealth=closer->getHealth();
            int opened=gap(launcher,closer), ticks=0;
            while(ticks<900 && (opened<=reach || closer->getHealth()>=invaderHealth)) {
                bot->checkAllUnits();      // repeated ordinary scans, every pass
                tick(30);
                ticks+=30;
                opened=std::max(opened,gap(launcher,closer));
            }
            SDL_Log("LAUNCHER_KITE_DIAG: emergency opened=%d reach=%d ticks=%d health=%d->%d "
                "mode=%d assigned=%d",
                opened,reach,ticks,invaderHealth.lround(),closer->getHealth().lround(),
                int(launcher->getAttackMode()),
                int(bot->defenceAssignments.count(launcher->getObjectID())));
            require(opened>reach,
                "An emergency Hunt defender never got outside the invader's own reach");
            require(closer->getHealth()<invaderHealth,
                "An emergency Hunt defender never actually fired on the invasion");
            require(launcher->getAttackMode()==HUNT,
                "The emergency defender lost engine Hunt while defending");
            require(!bot->reserveDamagedUnitForRepair(launcher),
                "Emergency fixture launcher ended up reserved for repair, which proves nothing");

            // The invasion stops: the contact is released and nothing is left
            // forced at the place the invader used to stand.
            closer->setTarget(nullptr);
            closer->doSetAttackMode(STOP);
            closer->setLocation(origin+Coord(30,25));
            bot->checkAllUnits();
            require(!bot->defenceAssignments.count(launcher->getObjectID()),
                "The defence contact outlived the attack on the building");
            closer->setHealth(closer->getMaxHealth());
            launcher->setHealth(launcher->getMaxHealth());
            bot->defenceAssignments.clear();
        }

        // ---- Everything else keeps the 805 behaviour -----------------------
        {
            bot->difficulty=QuantBot::Difficulty::Brutal;
            closer->setLocation(origin+Coord(21,20));

            // Other hunting attackers are not kited, as 804/805 established.
            auto* tank=spawn(owner,Unit_Tank,Coord(20,22));
            auto* deviator=spawn(owner,Unit_Deviator,Coord(20,24));
            publish();
            for(auto* unit:{tank,deviator}) {
                hunt(unit,unit->getLocation());
                closer->setLocation(unit->getLocation()+Coord(1,0));
                require(!bot->escapeCloseThreatWhileHunting(unit),
                    "A hunting attacker other than a launcher was kited");
                const Coord destination=unit->getDestination();
                bot->checkAllUnits();
                require(unit->getAttackMode()==HUNT && unit->getDestination()==destination,
                    "An ordinary pass took a hunting attacker out of engine Hunt");
            }

            // A friendly or harmless neighbour is not a threat at any distance.
            closer->setLocation(origin+Coord(30,20));
            hunt(launcher,origin+Coord(20,20));
            auto* friendly=spawn(owner,Unit_Tank,Coord(21,20));
            auto* worker=spawn(enemy,Unit_Harvester,Coord(19,20));
            publish();
            require(bot->launcherEscapeRange(launcher,friendly)==0,
                "A friendly unit standing next to the launcher counted as a threat");
            require(bot->launcherEscapeRange(launcher,worker)==0,
                "An enemy that cannot shoot the launcher counted as a threat");
            require(!bot->escapeCloseThreatWhileHunting(launcher),
                "A launcher escaped from units that cannot hurt it");
            retire(friendly); retire(worker);
            retire(tank); retire(deviator);

            // Easy, Medium and campaign games keep their own spacing paths.
            closer->setLocation(origin+Coord(21,20));
            hunt(launcher,origin+Coord(20,20));
            for(const auto difficulty:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium}) {
                bot->difficulty=difficulty;
                require(!bot->huntingLauncher(launcher) && !bot->escapeCloseThreatWhileHunting(launcher),
                    "The hunting-launcher escape leaked into Easy or Medium");
            }
            bot->difficulty=QuantBot::Difficulty::Brutal;
            game.gameType=GameType::Campaign;
            require(!bot->huntingLauncher(launcher) && !bot->escapeCloseThreatWhileHunting(launcher),
                "The hunting-launcher escape leaked into a campaign game");
            game.gameType=GameType::CustomGame;
            bot->supportMode=true;
            require(!bot->escapeCloseThreatWhileHunting(launcher),
                "The hunting-launcher escape leaked into support mode");
            bot->supportMode=false;
        }
    } catch(...) {
        cleanupStructures(); unitList=savedUnits; throw;
    }
    cleanupStructures();
    unitList=savedUnits;
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=launcher_kiting outcome=passed");
}
