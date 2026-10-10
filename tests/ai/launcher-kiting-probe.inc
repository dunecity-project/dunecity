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
//   4. Rocket infantry share the same out-of-range aircraft filter - a different,
//      shorter-range projectile with the same selection problem - and a Trooper's
//      SmallRocket really removes Ornithopter hit points.
#include <Bullet.h>
#include <units/AirUnit.h>

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

        // ---- Advance past an unreachable aircraft -----------------------
        //
        // Reproduces the stall measured on whatswrong.dls (cycle 154917,
        // Twin Cities 256x256): a hunting launcher whose nearest attackable
        // object is an Ornithopter it cannot reach acquires that aircraft,
        // engageTarget() releases it in the same cycle because it is flying and
        // beyond weapon range, and releaseTarget() re-pins the destination onto
        // the launcher's own tile and clears the search cooldown. The launcher
        // then repeats that every cycle and never advances with its wave.
        //
        // Before the fix the launcher never closes on the ground target at all;
        // the net displacement stays zero. An aircraft INSIDE weapon range must
        // still be selected, because that shot is what a launcher is for.
        bot->difficulty=QuantBot::Difficulty::Brutal;
        game.gameType=GameType::CustomGame;
        bot->supportMode=false;
        {
            // The earlier phases leave retired fixture units on their tiles until
            // the engine collects them, and a dead unit is still an attackable
            // candidate. Stage this phase in the far corner of the cleared area so
            // that debris cannot be the nearest object.
            const Coord base(4,27);
            closer->setLocation(origin+Coord(38,4));    // Parked well clear.
            closer->doSetAttackMode(STOP);
            closer->setTarget(nullptr);
            closer->setForced(false);

            auto* prey=spawn(enemy,Unit_Tank,base+Coord(24,0));
            prey->doSetAttackMode(STOP);
            prey->setTarget(nullptr);
            prey->setForced(false);
            hunt(launcher,origin+base);
            const Coord start=launcher->getLocation();
            const int preyGap=gap(launcher,prey);

            // The fixture shares the live map, and a Hunt search is whole-map, so
            // establish what the launcher would otherwise pick before introducing
            // the aircraft. The stall only arises when the aircraft is the NEAREST
            // attackable object, so the aircraft has to beat this distance.
            const ObjectBase* without=launcher->findTarget();
            const int groundGap=without
                ? blockDistance(start,without->getClosestPoint(start)).lround() : -1;
            SDL_Log("LAUNCHER_ADVANCE_DIAG: ground_choice=%d item=%d gap=%d prey_gap=%d range=%d",
                without?int(without->getObjectID()):-1,without?int(without->getItemID()):-1,
                groundGap,preyGap,launcherRange);
            require(without!=nullptr,
                "The launcher fixture has no reachable ground candidate at all");

            // Place the aircraft inside that distance but outside the launcher's
            // own reach, which is exactly the geometry measured on the save.
            const int airGapWanted=groundGap-1;
            const bool airCanDominate=airGapWanted>launcherRange;
            auto* raider=spawn(enemy,Unit_Ornithopter,
                base+Coord(airCanDominate?airGapWanted:launcherRange+3,0));
            publish();
            const int airGap=gap(launcher,raider);
            require(launcher->canAttack(raider),
                "Fixture launcher cannot target aircraft at all, so the stall cannot arise");
            require(airGap>launcherRange,
                "Fixture aircraft is not outside the launcher's weapon range");

            // The search must now offer the reachable ground target, not the
            // aircraft the engine would throw away.
            const ObjectBase* chosen=launcher->findTarget();
            SDL_Log("LAUNCHER_ADVANCE_DIAG: air_gap=%d prey_gap=%d range=%d chosen=%d chosen_item=%d",
                airGap,preyGap,launcherRange,
                chosen?int(chosen->getObjectID()):-1,chosen?int(chosen->getItemID()):-1);
            require(chosen!=nullptr,
                "A hunting launcher with a reachable ground target found nothing");
            // The invariant, stated without assuming which object wins: the Hunt
            // search never yields an aircraft this launcher cannot shoot. The
            // fixture shares the real map, so a nearer real unit may legitimately
            // be chosen instead of the fixture's own tank.
            require(!chosen->isAFlyingUnit() || launcher->isInWeaponRange(chosen),
                "A hunting launcher still selected an aircraft beyond its weapon range");

            // Real cycles and real pathfinding. The stall's signature is that the
            // launcher ends every cycle with no target, standing on its own tile,
            // so "not stalled" means it either holds a shot it can take or it has
            // actually travelled. Before the fix neither is ever true.
            int moved=0, passes=0;
            bool engaged=false;
            for(;passes<10 && moved<=2 && !engaged;++passes) {
                bot->checkAllUnits();
                tick(60);
                moved=blockDistance(start,launcher->getLocation()).lround();
                const ObjectBase* held=launcher->getTarget();
                engaged=held!=nullptr && launcher->isInWeaponRange(held);
            }
            SDL_Log("LAUNCHER_ADVANCE_DIAG: moved=%d engaged=%d passes=%d mode=%d tracked=%d"
                " air_gap=%d dominating=%d",
                moved,int(engaged),passes,int(launcher->getAttackMode()),
                int(bot->customWave.members.count(launcher->getObjectID())),
                airGap,int(airCanDominate));
            if(airCanDominate) {
                require(moved>2 || engaged,
                    "A hunting launcher held up by a distant aircraft neither advanced nor engaged");
            } else {
                // The live map offered a candidate closer than the launcher's own
                // reach, so this map cannot stage the stall. The invariant above
                // still held, which is the part this fixture owns.
                SDL_Log("LAUNCHER_ADVANCE_DIAG: map too crowded to stage the stall;"
                    " selection invariant checked only");
            }
            require(bot->customWave.members.count(launcher->getObjectID()),
                "The advance dropped the launcher out of the tracked army");

            // Anti-air is untouched: inside weapon range the aircraft is the
            // selection, and Guard/AreaGuard posts keep their own search range.
            // Anti-air is untouched: an aircraft right beside the launcher is
            // nearer than anything else on this map, so it must be the selection.
            raider->setLocation(launcher->getLocation()+Coord(1,0));
            require(launcher->isInWeaponRange(raider),
                "Fixture aircraft did not come inside the launcher's weapon range");
            const ObjectBase* nowChosen=launcher->findTarget();
            SDL_Log("LAUNCHER_ADVANCE_DIAG: in_range_choice=%d item=%d",
                nowChosen?int(nowChosen->getObjectID()):-1,
                nowChosen?int(nowChosen->getItemID()):-1);
            require(nowChosen==raider,
                "A launcher refused an aircraft inside its own weapon range");

            // An Area Guard post keeps its established 12-tile air search: the
            // veto is Hunt-only, so a launcher covering a building still picks
            // up an aircraft it is not yet in range of.
            raider->setLocation(launcher->getLocation()+Coord(launcherRange+2,0));
            require(!launcher->isInWeaponRange(raider),
                "Fixture aircraft did not move back outside the launcher's reach");
            bot->doSetAttackMode(launcher,AREAGUARD);
            launcher->setGuardPoint(launcher->getLocation());
            const ObjectBase* posted=launcher->findTarget();
            SDL_Log("LAUNCHER_ADVANCE_DIAG: areaguard_chosen=%d flying=%d",
                posted?int(posted->getObjectID()):-1,
                (posted&&posted->isAFlyingUnit())?1:0);
            require(posted!=nullptr,
                "An Area Guard launcher lost its own area search");
            bot->doSetAttackMode(launcher,HUNT);

            // ---- The same in-range filter for rocket infantry ---------------
            // A Trooper fires its own SmallRocket over five tiles, not the
            // launcher's Rocket over nine, but the engine releases any flying
            // target beyond weapon range for both, so a distant Ornithopter
            // stalls its Hunt in exactly the same way and the candidate filter
            // has to cover it. The launcher's close-range spacing is not
            // extended with it.
            {
                const int trooperRange=currentGame->objectData.data[Unit_Trooper]
                    [owner->getHouseID()].weaponrange;
                require(trooperRange>0,"Fixture Trooper has no weapon range at all");
                auto* foot=spawn(owner,Unit_Trooper,base+Coord(0,3));
                const Coord footStart=foot->getLocation();
                foot->setTarget(nullptr);
                bot->doMove2Pos(foot,foot->getX(),foot->getY(),false);
                bot->doSetAttackMode(foot,HUNT);
                foot->setForced(false);
                publish();
                require(bot->managesAutonomousRocketHunt(foot),
                    "An autonomously hunting Trooper is not recognised as rocket anti-air");
                require(!bot->huntingLauncher(foot)
                        && !bot->escapeCloseThreatWhileHunting(foot,closer),
                    "The launcher's close-range spacing was extended to rocket infantry");

                const ObjectBase* footGround=foot->findTarget();
                require(footGround!=nullptr,
                    "The Trooper fixture has no reachable ground candidate at all");
                const int footGroundGap=blockDistance(footStart,
                    footGround->getClosestPoint(footStart)).lround();
                if(footGroundGap-1>trooperRange) {
                    raider->setLocation(footStart+Coord(footGroundGap-1,0));
                    publish();
                    require(foot->canAttack(raider) && !foot->isInWeaponRange(raider),
                        "The Trooper fixture aircraft is not an unreachable candidate");
                    const ObjectBase* footChoice=foot->findTarget();
                    SDL_Log("LAUNCHER_ADVANCE_DIAG: trooper_range=%d ground_gap=%d chosen_item=%d",
                        trooperRange,footGroundGap,footChoice?int(footChoice->getItemID()):-1);
                    require(footChoice!=nullptr && (!footChoice->isAFlyingUnit()
                            || foot->isInWeaponRange(footChoice)),
                        "A hunting Trooper selected an aircraft beyond its weapon range");
                } else {
                    SDL_Log("LAUNCHER_ADVANCE_DIAG: map too crowded to stage the Trooper stall;"
                        " selection invariant checked only");
                }

                // Inside range the aircraft is the selection, and the rocket really
                // removes hit points. A stepped rocket detonates where it was aimed,
                // so the Ornithopter is held on station by its own speed data - as the
                // anti-air audit does - rather than by teleporting it back between
                // cycles. Only the Trooper and the aircraft are live, and the launcher
                // rocket counter must not move, so the hit points removed cannot have
                // come from another friendly shooter.
                auto& airSpeed=currentGame->objectData.data[Unit_Ornithopter]
                    [enemy->getHouseID()].maxspeed;
                const FixPoint savedAirSpeed=airSpeed;
                airSpeed=0;
                static_cast<AirUnit*>(raider)->currentMaxSpeed=0;
                raider->doSetAttackMode(STOP);
                raider->setTarget(nullptr);
                // An aircraft-targeted SmallRocket doubles its arming delay from 3 to 6
                // movement steps. Measure at the far edge of the Trooper's normal range
                // so that delay has elapsed when the rocket reaches the aircraft.
                const Coord hover=footStart+Coord(trooperRange,0);
                raider->setLocation(hover);
                // Drain anything already in flight with the Trooper not updating, so a
                // shot from an earlier phase cannot be mistaken for this one.
                unitList.clear();unitList.push_back(raider);
                tick(120);
                raider->setLocation(hover);
                raider->setHealth(raider->getMaxHealth());
                unitList.clear();unitList.push_back(foot);unitList.push_back(raider);
                require(foot->getHealth()>0 && foot->isInWeaponRange(raider),
                    "The Trooper fixture aircraft did not come inside rocket range");
                require(foot->findTarget()==raider,
                    "A Trooper refused an aircraft inside its own weapon range");
                // The shot itself is measured from the defensive posture the air rescue
                // actually assigns: an Area Guard post, which holds its ground and fires
                // rather than walking the engagement distance away.
                bot->doSetAttackMode(foot,AREAGUARD);
                foot->setGuardPoint(footStart);
                foot->setForced(false);
                const FixPoint airFull=raider->getHealth();
                const int otherRocketsBefore=game.combatStats.launcherFiresOnOrni;
                bool removedAirHp=false, aimed=false, firedSmallRocket=false;
                for(int cycle=0;cycle<1200 && !removedAirHp;++cycle) {
                    foot->setHealth(foot->getMaxHealth());
                    tick(1);
                    for(const auto* shot:bulletList)
                        if(shot->shooterID==foot->getObjectID()
                            && shot->bulletID==Bullet_SmallRocket) firedSmallRocket=true;
                    aimed=aimed || foot->getTarget()==raider;
                    removedAirHp=raider->getHealth()<airFull;
                }
                airSpeed=savedAirSpeed;
                SDL_Log("LAUNCHER_ADVANCE_DIAG: trooper_aimed=%d trooper_small_rocket=%d"
                    " trooper_removed_air_hp=%d other_rockets=%d gap=%d range=%d",int(aimed),
                    int(firedSmallRocket),int(removedAirHp),
                    game.combatStats.launcherFiresOnOrni-otherRocketsBefore,
                    gap(foot,raider),trooperRange);
                bot->doSetAttackMode(foot,HUNT);
                require(aimed,"The defending Trooper never acquired the aircraft in range");
                require(firedSmallRocket,
                    "The defending Trooper never fired a SmallRocket at the aircraft in range");
                require(game.combatStats.launcherFiresOnOrni==otherRocketsBefore,
                    "Another friendly rocket unit fired during the Trooper measurement");
                require(removedAirHp,
                    "A Trooper in range never removed any Ornithopter hit points");

                // Authority boundaries for the shared filter: a human-ordered Trooper,
                // a support helper, a campaign game and Easy/Medium keep the engine's
                // own unfiltered search, exactly as launchers do.
                bot->onHumanUnitOrder(foot->getObjectID());
                require(!bot->managesAutonomousRocketHunt(foot),
                    "The rocket filter overrode a human-ordered Trooper");
                bot->manualUnitOrders.erase(foot->getObjectID());
                bot->supportMode=true;
                require(!bot->managesAutonomousRocketHunt(foot),
                    "The rocket filter claimed a support helper's Trooper");
                bot->supportMode=false;
                game.gameType=GameType::Campaign;
                require(!bot->managesAutonomousRocketHunt(foot),
                    "The rocket filter leaked into a campaign game");
                game.gameType=GameType::CustomGame;
                for(const auto soft:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium}) {
                    bot->difficulty=soft;
                    require(!bot->managesAutonomousRocketHunt(foot),
                        "The rocket filter leaked into Easy or Medium");
                }
                bot->difficulty=QuantBot::Difficulty::Brutal;
                require(bot->managesAutonomousRocketHunt(foot),
                    "Restoring the autonomous Custom Hard/Brutal house lost the filter");
                retire(foot);
            }

            retire(raider); retire(prey);
        }
    } catch(...) {
        cleanupStructures(); unitList=savedUnits; throw;
    }
    cleanupStructures();
    unitList=savedUnits;
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=launcher_kiting outcome=passed");
}
