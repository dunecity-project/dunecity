// Real-engine army recovery, cohesion, outnumbered dispatch and front battery.
// Included only by run-campaign-balance.py; never linked into the game.
//
// Everything here drives the production QuantBot entry points against real
// units, real structures and the real map. No behaviour is simulated.
#include "battery-clearance-probe.inc"

void runArmyRecoveryProbe(Game& game) {
    // QuantBot's AI pass interval (AIUPDATEINTERVAL in QuantBot.cpp, which is a
    // translation-unit macro). One interval of real frames is serviced between
    // recall passes in the stress section below.
    constexpr int kAiInterval = 50;
    auto require=[](bool ok,const char* why){
        if(!ok){SDL_Log("ARMY_RECOVERY_FAIL: %s",why);throw std::runtime_error(why);}
    };
    auto* owner=game.getHouse(HOUSE_HARKONNEN);
    auto* enemy=game.getHouse(HOUSE_ATREIDES);
    require(owner && enemy,"Recovery fixture missing houses");
    QuantBot* bot=nullptr;
    for(const auto& player:owner->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get()))bot=candidate;
    require(bot,"Recovery fixture missing bot");

    // Custom Hard/Brutal is the only mode this behaviour runs in.
    game.gameType=GameType::CustomGame;
    bot->difficulty=QuantBot::Difficulty::Brutal;
    bot->gameMode=QuantBot::GameMode::Custom;
    bot->supportMode=false;
    bot->manualUnitOrders.clear();
    bot->defenceAssignments.clear();
    bot->customWave.members.clear();
    bot->attrition.reset();
    bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
    bot->postureSince=0;
    getQuantBotConfig().recovery.enabled=true;
    getQuantBotConfig().recovery.frontBatteriesEnabled=true;
    require(bot->recoveryActive(),"Recovery gate refused Custom Brutal");

    if (std::getenv("BALANCE_BATTERY_CLEARANCE_ONLY")) {
        runBatteryClearanceProbe(game);
        SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=battery_clearance outcome=passed");
        return;
    }

    // The gate itself: nothing below may leak into other modes.
    {
        bot->difficulty=QuantBot::Difficulty::Medium;
        require(!bot->recoveryActive(),"Recovery leaked into Medium");
        bot->difficulty=QuantBot::Difficulty::Hard;
        require(bot->recoveryActive(),"Recovery refused Hard");
        game.gameType=GameType::Campaign;
        require(!bot->recoveryActive(),"Recovery leaked into a campaign game");
        game.gameType=GameType::CustomGame;
        bot->supportMode=true;
        require(!bot->recoveryActive(),"Recovery leaked into support mode");
        bot->supportMode=false;
        getQuantBotConfig().recovery.enabled=false;
        require(!bot->recoveryActive(),"Recovery ignored its configuration switch");
        getQuantBotConfig().recovery.enabled=true;
        bot->difficulty=QuantBot::Difficulty::Brutal;
    }

    // ---- Free ground for the fixture ------------------------------------
    Coord origin=Coord::Invalid();
    for(int y=2;y+28<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for(int x=2;x+40<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear=true;
            for(int dy=0;dy<28 && clear;++dy)for(int dx=0;dx<40 && clear;++dx) {
                const auto* tile=currentGameMap->getTile(x+dx,y+dy);
                if(tile->hasAGroundObject() || tile->isMountain())clear=false;
            }
            if(clear)origin=Coord(x,y);
        }
    require(origin.isValid(),"Recovery fixture needs a clear 40x28 area");

    auto spawn=[&](House* house,Uint32 item,Coord offset) {
        auto* unit=house->createUnit(item);
        require(unit,"Recovery fixture unit creation failed");
        unit->deploy(origin+offset);
        unit->setVisible(owner->getTeamID(),true);
        unit->setVisible(enemy->getTeamID(),true);
        return unit;
    };

    const auto savedUnits=unitList;
    // Set once the real-frame stress section hands the world unit list back to
    // the engine; after that the fixture must not substitute it again.
    bool worldListRestored=false;
    std::vector<StructureBase*> fixtureStructures;
    // Real frames can destroy a fixture building, so cleanup resolves ids
    // through the object manager rather than dereferencing stale pointers.
    std::vector<Uint32> fixtureStructureIds;
    auto cleanupStructures=[&] {
        for(const Uint32 id:fixtureStructureIds)
            if(auto* s=dynamic_cast<StructureBase*>(currentGame->getObjectManager().getObject(id)))
                if(s->getHealth()>0) s->setHealth(0);
    };
    // Coord has an implicit operator bool(), so std::set<Coord> would compare
    // every valid coordinate as equal and silently collapse to one entry.
    // Distinct-tile counts therefore go through an encoded key.
    auto tileKey=[](Coord c){ return (int64_t(c.x)<<32) ^ uint32_t(c.y); };
    // unitList is a RobustList, so the fixture keeps its own roster and
    // republishes it whenever a unit joins or leaves the scenario.
    std::vector<UnitBase*> roster;
    auto publish=[&] {
        unitList.clear();
        for(auto* unit:roster) unitList.push_back(unit);
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
            fixtureStructures.push_back(structure);fixtureStructureIds.push_back(structure->getObjectID());
        }
        return structure;
    };

    try {
        // A real base to assemble at, including the emplacements the protected
        // rally is supposed to prefer.
        auto* yard=place(owner,Structure_ConstructionYard,Coord(4,4));
        require(yard,"Recovery fixture yard placement failed");
        place(owner,Structure_Refinery,Coord(8,4));
        place(owner,Structure_HeavyFactory,Coord(4,8));
        // attack() refuses to dispatch at all without a repair yard, so the
        // fixture owns one: otherwise a "did not attack" assertion could pass
        // for the wrong reason.
        place(owner,Structure_RepairYard,Coord(8,8));
        place(owner,Structure_RocketTurret,Coord(7,7));
        place(owner,Structure_RocketTurret,Coord(9,7));

        // Our army: a tracked wave far forward, reinforcements at home.
        std::vector<UnitBase*> waveUnits, homeUnits;
        for(int i=0;i<8;++i) waveUnits.push_back(spawn(owner,Unit_Tank,Coord(30+i%4,14+i/4)));
        for(int i=0;i<4;++i) homeUnits.push_back(spawn(owner,Unit_Tank,Coord(5+i,12)));
        auto* hostileTank=spawn(enemy,Unit_SiegeTank,Coord(33,16));

        for(auto* unit:waveUnits)roster.push_back(unit);
        for(auto* unit:homeUnits)roster.push_back(unit);
        roster.push_back(hostileTank);
        publish();

        bot->dangerUpdated=std::numeric_limits<Uint32>::max();
        bot->refreshTacticalDanger();
        bot->squadRallyLocation=origin+Coord(6,6);
        bot->protectedRallyCycle=std::numeric_limits<Uint32>::max();
        bot->protectedRally=Coord::Invalid();
        bot->protectedRally=bot->findProtectedRallyLocation();
        require(bot->protectedRally.isValid(),"Protected rally was not found near a real base");
        // The search is a bounded window around this house's own base centre of
        // mass, which on a real map includes its scenario base as well as the
        // fixture buildings.
        const Coord baseCentre=bot->findBaseCentre(owner->getHouseID());
        require(baseCentre.isValid(),"Fixture house has no base centre");
        require(std::max(std::abs(bot->protectedRally.x-baseCentre.x),
                         std::abs(bot->protectedRally.y-baseCentre.y))<=8,
            "Protected rally left its bounded search window around the base");
        require(bot->dangerAt(bot->protectedRally)==0,"Protected rally sits in observed danger");
        require(!currentGameMap->getTile(bot->protectedRally)->isMountain()
            && !currentGameMap->getTile(bot->protectedRally)->hasAStructure(),
            "Protected rally is not a tile a unit can stand on");

        // Turret cover is the first term of the score, so adding emplacements
        // beside the base centre must move the assembly point under them.
        {
            std::vector<StructureBase*> cover;
            for(const Coord offset:{Coord(3,0),Coord(-3,0)}) {
                auto* turret=owner->placeStructure(NONE_ID,Structure_RocketTurret,
                    baseCentre.x+offset.x,baseCentre.y+offset.y,true,true);
                if(turret){turret->setVisible(owner->getTeamID(),true);
                    fixtureStructureIds.push_back(turret->getObjectID());cover.push_back(turret);}
            }
            if(!cover.empty()) {
                const int radius=std::max(1,currentGame->objectData.data[Structure_RocketTurret]
                    [owner->getHouseID()].weaponrange-1);
                bot->protectedRallyCycle=std::numeric_limits<Uint32>::max();
                bot->protectedRally=Coord::Invalid();
                const Coord covered=bot->findProtectedRallyLocation();
                require(covered.isValid(),"Protected rally vanished once turrets existed");
                bool underCover=false;
                for(auto* turret:cover)
                    if(blockDistance(turret->getLocation(),covered).lround()<=radius) underCover=true;
                require(underCover,"The protected rally ignored available turret cover");
                bot->protectedRally=covered;
            }
        }

        // ---- 1. Reinforcements assemble at home, not at the hunt centroid --
        for(auto* unit:waveUnits){bot->doSetAttackMode(unit,HUNT);bot->customWave.members.insert(unit->getObjectID());}
        for(auto* unit:homeUnits){bot->doSetAttackMode(unit,AREAGUARD);unit->setForced(false);}
        const Coord huntCentre=bot->findSquadCenter(owner->getHouseID());
        require(huntCentre.isValid() && blockDistance(huntCentre,origin+Coord(31,14)).lround()<=6,
            "Fixture hunt centroid is not forward as expected");
        for(auto* unit:homeUnits) {
            bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);
            unit->setForced(false);
            bot->moveToOptimalSquadPosition(unit,2);
            SDL_Log("ARMY_RECOVERY_DIAG: reinforcement id=%u mode=%d guard=%d,%d rally=%d,%d "
                "hunt=%d,%d assembly=%d,%d",
                unit->getObjectID(),int(unit->getAttackMode()),
                unit->getGuardPoint().x,unit->getGuardPoint().y,
                bot->protectedRally.x,bot->protectedRally.y,huntCentre.x,huntCentre.y,
                bot->assemblyPoint().x,bot->assemblyPoint().y);
            // "Home" is the house's own assembly point, which while offensive
            // is the exterior staging slot rather than the retreat shelter. The
            // thing a reinforcement must never do is follow the attacking
            // army's centroid out to the front on its own.
            require(blockDistance(unit->getGuardPoint(),bot->assemblyPoint()).lround()
                    < blockDistance(unit->getGuardPoint(),huntCentre).lround(),
                "A reinforcement followed the hunt centroid instead of assembling at home");
        }
        // A wave member is not dragged backwards by the same rule, whether it
        // is hunting or has momentarily lost its target: a committed attacker
        // belongs to the attack until the posture recalls it.
        {
            auto* member=waveUnits.front();
            const Coord before=member->getGuardPoint();
            bot->moveToOptimalSquadPosition(member,2);
            require(member->getAttackMode()==HUNT && member->getGuardPoint()==before,
                "Home assembly stole an active wave member");
            bot->doSetAttackMode(member,AREAGUARD);
            member->setForced(false);
            bot->doMove2Pos(member,member->getX(),member->getY(),false);
            member->setForced(false);
            const Coord idleGuard=member->getGuardPoint();
            bot->moveToOptimalSquadPosition(member,2);
            require(member->getGuardPoint()==idleGuard && !member->wasForced(),
                "Home assembly reclaimed a committed attacker that was between targets");
            bot->doSetAttackMode(member,HUNT);
        }
        // Offensive form-up is outside the colony's own buildings; the retreat
        // shelter stays inside it. Two different places for two different jobs.
        {
            const Coord staging=bot->offensiveStagingLocation();
            const Coord anchor=bot->stagingAnchor();
            require(staging.isValid() && anchor.isValid(),
                "The house has no staging ground at all");
            const int footprint=bot->colonyFootprintRadius(anchor);
            SDL_Log("ARMY_RECOVERY_DIAG: staging=%d,%d anchor=%d,%d footprint=%d shelter=%d,%d",
                staging.x,staging.y,anchor.x,anchor.y,footprint,
                bot->protectedRally.x,bot->protectedRally.y);
            require(blockDistance(staging,anchor).lround()>footprint,
                "Offensive staging is inside the colony footprint");
            require(currentGameMap->tileExists(staging)
                && !currentGameMap->getTile(staging)->isMountain()
                && !currentGameMap->getTile(staging)->hasAStructure()
                && !currentGameMap->getTile(staging)->isRoad()
                && bot->dangerAt(staging)==0,
                "Offensive staging is not safe, standable, road-free ground");
            // And it is a form-up point, not a march: still beside its colony.
            require(blockDistance(staging,anchor).lround()<=footprint+1+QuantBot::kStagingBand,
                "Offensive staging left the colony behind");
            // The posture decides which of the two the house is using.
            const auto posture=bot->armyPosture;
            bot->armyPosture=ArmyPosturePolicy::Posture::Recovering;
            require(bot->assemblyPoint()==bot->protectedRally,
                "Recovery did not assemble at the protected shelter");
            bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
            require(bot->assemblyPoint()==staging,
                "An offensive house did not form up at its exterior staging point");
            int exteriorOrders=0;
            for(auto* unit:homeUnits) {
                bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);
                unit->setForced(false);
                bot->moveToOptimalSquadPosition(unit,bot->armyAssemblyRadius());
                if(unit->getDestination()==unit->getLocation()) continue;
                ++exteriorOrders;
                require(!currentGameMap->getTile(unit->getDestination())->isRoad()
                    && blockDistance(unit->getDestination(),anchor).lround()>footprint,
                    "The staging centre is exterior but a reserve destination is inside the base or on a road");
            }
            require(exteriorOrders>0,"No interior reserve was ordered onto exterior ground");
            bot->armyPosture=posture;
        }

        // Hunt owns tactical decisions both on damage and on ordinary scans.
        {
            std::vector<UnitBase*> attackers;
            for(const auto item:{Unit_Launcher,Unit_Trike}) {
                auto* unit=spawn(owner,item,Coord(35,20+int(attackers.size())));
                attackers.push_back(unit);roster.push_back(unit);
                bot->doSetAttackMode(unit,HUNT);
            }
            publish();
            for(auto* unit:attackers) {
                const Coord destination=unit->getDestination();
                bot->onDamage(unit,1,hostileTank->getObjectID());
                require(unit->getAttackMode()==HUNT && !unit->wasForced()
                    && unit->getDestination()==destination,
                    "A damage callback replaced engine Hunt with tactical micro");
            }
            bot->checkAllUnits();
            for(auto* unit:attackers)
                require(unit->getAttackMode()==HUNT && !unit->wasForced(),
                    "An ordinary AI scan replaced an attacker's engine Hunt");
            for(auto* unit:attackers) retire(unit);
        }

        // Local members are support once, not once locally plus once globally.
        {
            const auto oldLedger=bot->attrition;
            const auto oldPosture=bot->armyPosture;
            const auto oldSince=bot->postureSince;
            const auto oldPressure=bot->localPressureSince;
            const auto oldWithdraw=bot->localWithdrawSince;
            std::vector<UnitBase*> overwhelming;
            for(int i=0;i<9;++i) {
                auto* unit=spawn(enemy,Unit_SiegeTank,Coord(28+i%3,19+i/3));
                overwhelming.push_back(unit); roster.push_back(unit);
            }
            publish();
            bot->dangerUpdated=std::numeric_limits<Uint32>::max();
            bot->localPressureSince=std::numeric_limits<Uint32>::max();
            bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
            bot->updateArmyPosture();
            require(bot->lastSurvey.squadSupport.ground==bot->lastSurvey.squad.ground,
                "Local wave members were duplicated in the support survey");
            require(bot->armyPosture==ArmyPosturePolicy::Posture::Withdrawing,
                "A materially overwhelmed wave did not withdraw without persistence");
            for(auto* unit:overwhelming) retire(unit);
            bot->attrition=oldLedger; bot->armyPosture=oldPosture; bot->postureSince=oldSince;
            bot->localPressureSince=oldPressure; bot->localWithdrawSince=oldWithdraw;
            bot->localSquadWithdraw=false;
            bot->lastSurveyCycle=std::numeric_limits<Uint32>::max();
            bot->dangerUpdated=std::numeric_limits<Uint32>::max();
            bot->refreshTacticalDanger();
        }

        // Enemies at an empty centroid of a dispersed wave are not a defeat of
        // the main army. Use real units on both sides of that empty space.
        {
            const auto oldLedger=bot->attrition;
            const auto oldPosture=bot->armyPosture;
            const auto oldSince=bot->postureSince;
            const auto oldPressure=bot->localPressureSince;
            const auto oldWithdraw=bot->localWithdrawSince;
            std::vector<Coord> positions;
            for(size_t i=0;i<waveUnits.size();++i) {
                positions.push_back(waveUnits[i]->getLocation());
                waveUnits[i]->setLocation(origin+Coord(i%2 ? 39 : 0,26));
            }
            std::vector<UnitBase*> raid;
            for(int i=0;i<6;++i) {
                auto* unit=spawn(enemy,Unit_SiegeTank,Coord(18+i,24));
                raid.push_back(unit);roster.push_back(unit);
            }
            publish();
            bot->attrition.reset();
            bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
            bot->localPressureSince=bot->localWithdrawSince=std::numeric_limits<Uint32>::max();
            bot->localSquadWithdraw=false;
            bot->updateArmyPosture();
            require(bot->lastSurvey.squadHostile.offensive()>0 && bot->lastSurvey.localWavePower==0,
                "The empty-centroid fixture did not observe a raid between the dispersed attackers");
            require(bot->armyPosture==ArmyPosturePolicy::Posture::Offensive && !bot->localSquadWithdraw,
                "A raid at an empty wave centroid recalled the main army");
            for(size_t i=0;i<waveUnits.size();++i) waveUnits[i]->setLocation(positions[i]);
            for(auto* unit:raid) retire(unit);
            bot->attrition=oldLedger;bot->armyPosture=oldPosture;bot->postureSince=oldSince;
            bot->localPressureSince=oldPressure;bot->localWithdrawSince=oldWithdraw;
            bot->localSquadWithdraw=false;
            bot->lastSurveyCycle=std::numeric_limits<Uint32>::max();
            bot->dangerUpdated=std::numeric_limits<Uint32>::max();bot->refreshTacticalDanger();
        }

        // Confirmed hostile fatalities, rather than legacy friendly-fire kills.
        {
            const int64_t before=bot->attrition.killCost;
            auto* friendly=spawn(owner,Unit_Soldier,Coord(36,25));
            roster.push_back(friendly); publish();
            friendly->handleDamage(friendly->getMaxHealth(),waveUnits.front()->getObjectID(),owner);
            require(friendly->getHealth()<=0,"Friendly-fire fixture did not die");
            require(bot->attrition.killCost==before,"Friendly fire inflated hostile kill value");
            retire(friendly);
            auto* victim=spawn(enemy,Unit_Tank,Coord(36,25));
            roster.push_back(victim); publish();
            const int price=currentGame->objectData.data[victim->getItemID()][victim->getOriginalHouseID()].price;
            victim->handleDamage(victim->getMaxHealth(),waveUnits.front()->getObjectID(),owner);
            require(victim->getHealth()<=0,"Hostile-kill fixture did not die");
            require(bot->attrition.killCost==before+price,"Confirmed hostile death was not priced correctly");
            victim->handleDamage(victim->getMaxHealth(),waveUnits.front()->getObjectID(),owner);
            require(bot->attrition.killCost==before+price,"Repeated damage credited the same death twice");
            retire(victim);
            auto* support=spawn(enemy,Unit_Harvester,Coord(36,25));
            roster.push_back(support); publish();
            support->handleDamage(support->getMaxHealth(),waveUnits.front()->getObjectID(),owner);
            require(bot->attrition.killCost==before+price,"A support casualty inflated military kill value");
            retire(support);
            SDL_Log("ARMY_RECOVERY_KILL_LEDGER: hostile_price=%d friendly=excluded support=excluded repeated=excluded",price);
        }

        // ---- 2. A minor raid does not panic the whole house ----------------
        bot->attrition.reset();
        bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
        bot->postureSince=0;
        const auto thresholds=bot->postureThresholds();
        // Fill the baseline ring so the window is genuinely covered, then lose
        // one cheap unit: a real loss, well under the material floor.
        for(uint32_t step=0;step<=8;++step)
            bot->attrition.sample(step*thresholds.sampleStrideCycles,40000,thresholds.sampleStrideCycles);
        bot->onDecrementUnits(Unit_Trike);
        bot->updateArmyPosture();
        require(bot->armyPosture==ArmyPosturePolicy::Posture::Offensive,
            "A single cheap loss moved the whole house out of the offensive");
        // Support losses must not count at all.
        const int64_t beforeSupport=bot->attrition.lostCost;
        bot->onDecrementUnits(Unit_Harvester);
        bot->onDecrementUnits(Unit_Carryall);
        bot->onDecrementUnits(Unit_MCV);
        bot->onDecrementUnits(Unit_Sandworm);
        require(bot->attrition.lostCost==beforeSupport,
            "A worker, transport, MCV or worm loss entered the combat attrition ledger");
        // Razing a building is not evidence of winning a losing battle.
        const int64_t beforeKills=bot->attrition.killCost;
        bot->onIncrementUnitKills(Structure_WindTrap);
        bot->onIncrementUnitKills(Unit_Harvester);
        require(bot->attrition.killCost==beforeKills,
            "A structure or worker kill entered the confirmed combat kill ledger");
        bot->onIncrementUnitKills(Unit_Tank);
        require(bot->attrition.killCost==beforeKills,"Legacy kill statistics entered the hostile ledger");
        bot->onHostileUnitKilled(Unit_Tank,enemy->getHouseID());
        require(bot->attrition.killCost>beforeKills,"A confirmed combat kill was not credited");

        // ---- 3. Sustained main-force attrition withdraws -------------------
        bot->attrition.reset();
        bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
        bot->postureSince=0;
        {
            // A covered window whose start saw a large deployed force, then a
            // heavy badly-traded loss inside it.
            const uint32_t stride=thresholds.sampleStrideCycles;
            ArmyPosturePolicy::AttritionLedger& ledger=bot->attrition;
            ledger.lostCost=0; ledger.killCost=0;
            for(uint32_t step=0;step<9;++step) ledger.sample(step*stride,40000,stride);
            ledger.lostCost=20000;   // half the force that was in the field
            ledger.killCost=1000;    // confirmed hostile loss: a bad trade
            ledger.sample(9*stride,12000,stride);
            bot->postureSince=0;
            // Drive the real evaluator with a real situation.
            ArmyPosturePolicy::Situation s;
            s.cycle=9*stride;
            const auto* start=ledger.windowStart(s.cycle,thresholds.windowCycles);
            require(start!=nullptr,"Fixture attrition window is not covered");
            s.windowCovered=true;
            s.windowStartDeployable=start->deployable;
            s.windowLoss=ledger.lostCost-start->lostCost;
            s.windowKill=ledger.killCost-start->killCost;
            s.readinessWorsening=true;
            s.designatedPower=10000;
            const auto decision=ArmyPosturePolicy::evaluate(ArmyPosturePolicy::Posture::Offensive,
                0,s,thresholds);
            require(decision.posture==ArmyPosturePolicy::Posture::Withdrawing,
                "Sustained badly-traded main-force attrition did not withdraw");
        }

        // ---- 4. The withdrawal really moves units home, and defends there --
        bot->armyPosture=ArmyPosturePolicy::Posture::Withdrawing;
        bot->postureSince=game.getGameCycleCount();
        bot->localSquadWithdraw=false;
        for(auto* unit:waveUnits) {
            bot->doSetAttackMode(unit,HUNT);
            bot->doAttackObject(unit,hostileTank,false);
        }
        int recallBudget=getQuantBotConfig().recovery.recallOrdersPerPass;
        bot->recallCursor=0;
        bot->applyArmyPosture(recallBudget);
        const int withdrawRadius=QuantBot::assemblyRadius(int(bot->customWave.members.size()));
        int recalled=0;
        for(auto* unit:waveUnits) {
            if(unit->getDestination().isValid() && unit->wasForced()
                && blockDistance(unit->getDestination(),bot->protectedRally).lround()<=withdrawRadius) {
                ++recalled;
                require(unit->getAttackMode()==AREAGUARD,
                    "A withdrawing unit was put into a mode that cannot shoot back");
                require(unit->getAttackMode()!=RETREAT,"Withdrawal used RETREAT");
                require(unit->wasForced(),"Withdrawal was not a committed move home");
                require(!unit->hasATarget(),"Withdrawal kept a target, so forced can never clear");
                // The guard point is the unit's own assembly slot, which is what
                // it will defend from on arrival, and that slot is inside the
                // assembly area rather than stacked on the anchor tile.
                require(unit->getGuardPoint()==unit->getDestination(),
                    "Withdrawing unit does not guard the slot it is walking to");
                require(blockDistance(unit->getGuardPoint(),bot->protectedRally).lround()
                        <=withdrawRadius,
                    "The withdrawal slot is outside the assembly area");
            }
        }
        require(recalled>0,"The withdrawal issued no orders at all");
        // Scattered over the room that exists, not queued onto one tile. The
        // bound is the number of standable tiles the assembly area actually
        // has: asserting more than that would be asserting against the map.
        {
            std::set<int64_t> used;
            for(auto* unit:waveUnits)
                if(unit->wasForced() && unit->getDestination().isValid())
                    used.insert(tileKey(unit->getDestination()));
            const auto roomSlots=bot->assemblySlots(bot->protectedRally,withdrawRadius);
            const size_t room=roomSlots.size();
            SDL_Log("ARMY_RECOVERY_DIAG: withdrawal recalled=%d distinct_slots=%zu room=%zu radius=%d rally=%d,%d",
                recalled,used.size(),room,withdrawRadius,bot->protectedRally.x,bot->protectedRally.y);
            require(room<2 || used.size()>1,
                "The whole withdrawal was aimed at a single tile although the area had room");
            // Most of the recalled units get their own slot. Some sharing is
            // normal - the hashed offsets can collide - so this asserts a clear
            // spread rather than a perfect permutation.
            require(used.size()*4>=std::min<size_t>(size_t(recalled),std::max<size_t>(1,room))*3,
                "The withdrawal did not spread over the standable room it had");
        }
        // Arrival restores area defence: the engine clears forced at the
        // destination, and AREAGUARD then acquires again.
        {
            auto* arrived=waveUnits.front();
            arrived->setLocation(bot->protectedRally);
            arrived->setForced(false);
            require(arrived->getAttackMode()==AREAGUARD,
                "An arrived unit is not defending the rally");
            require(arrived->isInGuardRange(hostileTank)
                || blockDistance(arrived->getLocation(),hostileTank->getLocation()).lround()>12,
                "Area guard at the rally cannot engage anything in range");
        }

        // ---- 5. Recovery suppresses dispatch; defence still works ----------
        bot->armyPosture=ArmyPosturePolicy::Posture::Recovering;
        bot->postureSince=game.getGameCycleCount();
        for(auto* unit:homeUnits){bot->doSetAttackMode(unit,AREAGUARD);unit->setForced(false);}
        bot->militaryValueLimit=100;              // Trivially above the army threshold.
        bot->attackTimer=0;
        bot->attack(bot->militaryUnitValue());
        for(auto* unit:homeUnits)
            require(unit->getAttackMode()!=HUNT,"Recovery still dispatched a fresh offensive");
        // An attack on the base still scrambles defenders while recovering.
        {
            auto* victim=place(owner,Structure_WindTrap,Coord(4,12));
            require(victim,"Recovery fixture defence target placement failed");
            auto* raider=spawn(enemy,Unit_Tank,Coord(7,12));
            roster.push_back(raider); publish();
            raider->doAttackObject(victim,true);
            bot->defenceResponseCycles.clear();
            bot->dangerUpdated=std::numeric_limits<Uint32>::max();
            bot->scrambleUnitsAndDefend(raider,false,victim);
            bool defended=false;
            for(auto* unit:homeUnits)
                if(unit->getTarget()==raider && bot->defenceAssignments.count(unit->getObjectID()))
                    defended=true;
            require(defended,"Emergency base defence was blocked by the recovery posture");
            // And the recall must not steal an assigned defender back.
            int budget=getQuantBotConfig().recovery.recallOrdersPerPass;
            for(auto* unit:homeUnits)
                if(bot->defenceAssignments.count(unit->getObjectID()))
                    bot->customWave.members.insert(unit->getObjectID());
            bot->applyArmyPosture(budget);
            for(auto* unit:homeUnits)
                if(bot->defenceAssignments.count(unit->getObjectID()))
                    require(unit->getTarget()==raider,
                        "The recall pulled an assigned base defender off an active attack");
            for(auto* unit:homeUnits) bot->customWave.members.erase(unit->getObjectID());
            bot->defenceAssignments.clear();
            retire(raider);
        }

        // ---- 6. Inactive cargo and repair-bound units are not strength -----
        {
            bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
            bot->customWave.members.clear();
            const auto before=bot->surveyArmy();
            auto* cargo=homeUnits.front();
            const int64_t cargoShare=before.deployableValue;
            cargo->setActive(false);
            cargo->setRespondable(false);
            const auto hidden=bot->surveyArmy();
            require(hidden.deployableValue<cargoShare,
                "A unit inside a transport or a bay still counted as deployable value");
            cargo->setActive(true);
            cargo->setRespondable(true);
            auto* wreck=homeUnits.back();
            wreck->setHealth(wreck->getMaxHealth()/10);
            const auto damaged=bot->surveyArmy();
            require(damaged.deployableValue<before.deployableValue,
                "A badly damaged unit held for repair still counted at full value");
            wreck->setHealth(wreck->getMaxHealth());
        }

        // ---- 7. The outnumbered dispatch gate, through the real attack path --
        //
        // This drives attack() and launchGroundHunt(), not just the chooser, and
        // measures the boundary with ACTUAL cheap units so the credit value is
        // the real sum of real prices rather than a floored approximation.
        {
            bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
            bot->postureSince=0;
            bot->lastSurveyCycle=std::numeric_limits<Uint32>::max();
            bot->customWave.members.clear();
            bot->customWave.front=NONE_ID;
            bot->attrition.reset();
            bot->lastMaterialLossCycle=std::numeric_limits<Uint32>::max();
            bot->localWithdrawSince=std::numeric_limits<Uint32>::max();
            bot->localPressureSince=std::numeric_limits<Uint32>::max();
            for(auto* unit:waveUnits) retire(unit);
            for(auto* unit:homeUnits) retire(unit);
            waveUnits.clear(); homeUnits.clear();

            // A cheap army: Soldiers really do cost sixty credits, so a floored
            // hundred-per-unit value would overstate the army by two thirds and
            // the boundary below would land in the wrong place.
            const int soldierPrice=currentGame->objectData.data[Unit_Soldier][owner->getHouseID()].price;
            require(soldierPrice>0 && soldierPrice<100,
                "Fixture needs a genuinely cheap unit to distinguish price from the floor");
            // Compact, so the readiness/assembly gate is satisfied and the
            // outnumbered veto is the only thing deciding below.
            std::vector<UnitBase*> cheap;
            for(int i=0;i<40;++i) {
                const Coord at=origin+Coord(2+(i%6),14+(i/6));
                if(!currentGameMap->tileExists(at)
                    || currentGameMap->getTile(at)->hasAGroundObject()
                    || currentGameMap->getTile(at)->isMountain()) continue;
                auto* unit=spawn(owner,Unit_Soldier,at-origin);
                cheap.push_back(unit); roster.push_back(unit);
            }
            require(cheap.size()>=24,"Fixture could not field a cheap army");
            // Observed hostile ground that genuinely outnumbers it.
            std::vector<UnitBase*> horde;
            for(int i=0;i<24;++i) horde.push_back(spawn(enemy,Unit_SiegeTank,Coord(28+i%8,20+i/8)));
            for(auto* unit:horde)roster.push_back(unit);
            // An inactive hostile unit and one invisible to us must not count.
            auto* hiddenEnemy=spawn(enemy,Unit_SiegeTank,Coord(26,24));
            hiddenEnemy->setVisible(owner->getTeamID(),false);
            auto* inactiveEnemy=spawn(enemy,Unit_SiegeTank,Coord(27,24));
            inactiveEnemy->setActive(false);
            roster.push_back(hiddenEnemy); roster.push_back(inactiveEnemy);
            publish();
            auto* enemyBase=place(enemy,Structure_ConstructionYard,Coord(32,22));
            require(enemyBase,"Recovery fixture enemy base placement failed");
            bot->dangerUpdated=std::numeric_limits<Uint32>::max();
            bot->refreshTacticalDanger();
            bot->lastSurveyCycle=std::numeric_limits<Uint32>::max();
            const auto survey=bot->surveyArmy();
            require(survey.frontObserved,"The observed enemy base did not become a front");
            require(survey.hostileFront.ground>0,"Observed hostile front power is zero");
            const auto thresholdsNow=bot->postureThresholds();

            // The credit value is the real sum of real prices.
            int64_t expected=0;
            for(auto* unit:cheap)
                expected+=CombatPowerPolicy::unitValue(soldierPrice,
                    unit->getHealth().lround(),unit->getMaxHealth());
            require(survey.deployableValue==expected,
                "Healthy deployable value is not the actual sum of actual prices");
            require(survey.deployableValue<int64_t(cheap.size())*100,
                "Healthy deployable value still carries a hundred-credit floor");
            SDL_Log("ARMY_RECOVERY_DIAG: cheap army units=%zu price=%d value=%lld",
                cheap.size(),soldierPrice,(long long)survey.deployableValue);

            // Fog and inactivity exclusions, measured on the hostile side.
            {
                auto* probeEnemy=spawn(enemy,Unit_SiegeTank,Coord(30,24));
                roster.push_back(probeEnemy); publish();
                probeEnemy->setVisible(owner->getTeamID(),false);
                bot->dangerUpdated=std::numeric_limits<Uint32>::max();
                bot->refreshTacticalDanger();
                const auto blind=bot->surveyArmy();
                probeEnemy->setVisible(owner->getTeamID(),true);
                bot->dangerUpdated=std::numeric_limits<Uint32>::max();
                bot->refreshTacticalDanger();
                const auto seen=bot->surveyArmy();
                require(seen.hostileFront.ground>blind.hostileFront.ground,
                    "A hostile unit under fog was counted as observed front power");
                probeEnemy->setActive(false);
                const auto parked=bot->surveyArmy();
                require(parked.hostileFront.ground<seen.hostileFront.ground,
                    "An inactive hostile unit was counted as front power");
                retire(probeEnemy);
            }

            // A distant allied army must not bypass the veto.
            {
                auto* ally=game.getHouse(HOUSE_ORDOS);
                if(ally && ally->getTeamID()==owner->getTeamID()) {
                    std::vector<UnitBase*> distant;
                    for(int i=0;i<30;++i) {
                        const Coord at(currentGameMap->getSizeX()-6+(i%4),
                                       currentGameMap->getSizeY()-10+(i/4));
                        if(!currentGameMap->tileExists(at)
                            || currentGameMap->getTile(at)->hasAGroundObject()
                            || currentGameMap->getTile(at)->isMountain()) continue;
                        auto* unit=ally->createUnit(Unit_SiegeTank);
                        if(!unit)break;
                        unit->deploy(at); unit->setVisible(owner->getTeamID(),true);
                        distant.push_back(unit); roster.push_back(unit);
                    }
                    publish();
                    const auto withDistant=bot->surveyArmy();
                    require(withDistant.allied.offensive()==survey.allied.offensive(),
                        "A distant allied army was counted as strength at this front");
                    for(auto* unit:distant) retire(unit);
                }
            }

            auto huntersAmong=[&](const std::vector<UnitBase*>& group) {
                int n=0; for(auto* unit:group) if(unit->getAttackMode()==HUNT)++n; return n;
            };
            auto resetArmy=[&] {
                bot->customWave.members.clear(); bot->customWave.front=NONE_ID;
                bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
                bot->localWithdrawSince=std::numeric_limits<Uint32>::max();
                bot->localPressureSince=std::numeric_limits<Uint32>::max();
                for(auto* unit:cheap) {
                    bot->doSetAttackMode(unit,GUARD); unit->setForced(false);
                    bot->doMove2Pos(unit,unit->getX(),unit->getY(),false); unit->setForced(false);
                }
                bot->lastSurveyCycle=std::numeric_limits<Uint32>::max();
            };
            // Put the cheap army at the form-up point so the readiness/assembly
            // gate is satisfied and the outnumbered veto is the only thing
            // deciding. The offensive form-up point is the exterior staging
            // slot, so the fixture holds both anchors on the block centre: this
            // section is about the value gate, not about where troops gather.
            bot->protectedRallyCycle=std::numeric_limits<Uint32>::max();
            bot->protectedRally=origin+Coord(4,17);   // Centre of the cheap block.
            bot->squadRallyLocation=bot->protectedRally;
            bot->offensiveStaging=bot->protectedRally;
            bot->offensiveStagingCycle=currentGame->getGameCycleCount();
            bot->exteriorStagingAvailable=false; // Isolate the value gate from geometry.
            require(currentGameMap->tileExists(bot->protectedRally),"Fixture rally is off-map");
            require(bot->assemblyPoint()==bot->protectedRally,
                "Fixture could not hold the gate army at one form-up point");

            const int64_t value=survey.deployableValue;
            // Configured limits that put the army at 79.9%, 80% and 80.1% of it.
            const int limitAt=int((value*10000+thresholdsNow.dispatchBypassBps-1)
                /thresholdsNow.dispatchBypassBps);                         // exactly 80%
            const int limitBelow=int(value*10000/7990);                    // 79.9%
            const int limitAbove=int(value*10000/8010);                    // 80.1%
            struct Case { const char* name; int limit; bool dispatch; };
            const Case cases[]={
                {"79.9 percent defers",limitBelow,false},
                {"80.0 percent dispatches",limitAt,true},
                {"80.1 percent dispatches",limitAbove,true},
            };
            for(const auto& scenario:cases) {
                resetArmy();
                bot->militaryValueLimit=scenario.limit;
                bot->attackTimer=0;
                bot->attack(bot->militaryUnitValue());
                const int hunters=huntersAmong(cheap);
                const bool dispatched=hunters>0 || !bot->customWave.members.empty();
                SDL_Log("ARMY_RECOVERY_DIAG: gate case=%s limit=%d value=%lld share_bps=%lld "
                    "hunters=%d members=%zu",
                    scenario.name,scenario.limit,(long long)value,
                    (long long)(value*10000/std::max(1,scenario.limit)),
                    hunters,bot->customWave.members.size());
                require(dispatched==scenario.dispatch,scenario.name);
            }
            // Enemy units remain actionable intelligence when their buildings
            // are hidden: the gate must still refuse an outnumbered dispatch.
            {
                std::vector<std::pair<StructureBase*,bool>> visibility;
                for(auto* building:structureList)
                    if(building->getOwner() && building->getOwner()->getTeamID()!=owner->getTeamID()) {
                        visibility.push_back({building,building->isVisible(owner->getTeamID())});
                        building->setVisible(owner->getTeamID(),false);
                    }
                resetArmy();
                bot->militaryValueLimit=limitBelow;
                bot->attackTimer=0;
                bot->attack(bot->militaryUnitValue());
                require(huntersAmong(cheap)==0 && bot->customWave.members.empty(),
                    "A visible superior army was ignored because its base was hidden");
                const auto observed=bot->surveyArmy();
                require(observed.frontObserved && observed.hostileFront.ground>0,
                    "Visible enemy units supplied no front intelligence");
                for(auto entry:visibility) entry.first->setVisible(owner->getTeamID(),entry.second);
            }
            // Very weak enemy: dispatch regardless of the configured share.
            {
                for(auto* unit:horde) retire(unit);
                horde.clear();
                auto* lone=spawn(enemy,Unit_Trike,Coord(30,21));
                roster.push_back(lone); publish();
                bot->dangerUpdated=std::numeric_limits<Uint32>::max();
                bot->refreshTacticalDanger();
                resetArmy();
                bot->militaryValueLimit=limitBelow;   // Still only 79.9 percent.
                bot->attackTimer=0;
                bot->attack(bot->militaryUnitValue());
                require(huntersAmong(cheap)>0 || !bot->customWave.members.empty(),
                    "Good local odds below the value share still refused to attack");
                retire(lone);
            }
            // A nonpositive configured limit never silently means "always attack"
            // while outnumbered, and the unit overrides are untouched.
            {
                for(int i=0;i<24;++i) {
                    auto* unit=spawn(enemy,Unit_SiegeTank,Coord(28+i%8,20+i/8));
                    horde.push_back(unit); roster.push_back(unit);
                }
                publish();
                bot->dangerUpdated=std::numeric_limits<Uint32>::max();
                bot->refreshTacticalDanger();
                resetArmy();
                bot->militaryValueLimit=0;
                bot->attackTimer=0;
                bot->attack(bot->militaryUnitValue());
                require(huntersAmong(cheap)==0 && bot->customWave.members.empty(),
                    "A zero configured limit granted the bypass by accident");
                // Brutal's rolling override budget is never the denominator: it
                // is owned value plus headroom, so eighty percent of it would be
                // reachable at any size and the veto would be a no-op.
                bot->militaryValueLimit=1000;
                const int rolling=QuantBotBuildPolicy::rollingMilitaryBudget(
                    int(value),bot->militaryValueLimit,0,0);
                require(rolling>bot->militaryValueLimit,"Fixture rolling budget is not larger");
                require(ArmyPosturePolicy::dispatchBypassValue(bot->militaryValueLimit,thresholdsNow)
                        <ArmyPosturePolicy::dispatchBypassValue(rolling,thresholdsNow),
                    "The configured limit and the rolling budget are indistinguishable here");
                require(bot->ignoresUnitCountLimit()
                        || bot->hasExplicitUnitCountOverride(),
                    "Brutal unit-count behaviour changed");
            }
            // Armed aircraft belong to the same population as the configured
            // limit, so they count toward the bypass value.
            {
                auto* orni=spawn(owner,Unit_Ornithopter,Coord(3,20));
                roster.push_back(orni); publish();
                bot->lastSurveyCycle=std::numeric_limits<Uint32>::max();
                const auto withAir=bot->surveyArmy();
                const int orniPrice=currentGame->objectData.data[Unit_Ornithopter][owner->getHouseID()].price;
                require(withAir.deployableValue==value+orniPrice,
                    "An armed aircraft is missing from the healthy deployable military value");
                require(withAir.deployableAirValue==orniPrice,
                    "The air share of the deployable value is not reported");
                require(withAir.deployableGroundValue==value,
                    "The ground share of the deployable value changed when air was added");
                // And the ground power comparisons stay ground-only.
                require(withAir.deployable.offensive()==survey.deployable.offensive(),
                    "An aircraft leaked into the ground offensive power");
                retire(orni);
            }
            for(auto* unit:horde) retire(unit);
            for(auto* unit:cheap) retire(unit);
            enemyBase->setHealth(0);
            // Rebuild the ordinary fixture army for the sections that follow.
            // Gathered in one compact block, because the readiness gate now
            // requires an assembled wave and the sections below are about
            // cohesion and the battery rather than about assembly.
            for(int i=0;i<8;++i) {
                auto* unit=spawn(owner,Unit_Tank,Coord(3+i%4,20+i/4));
                waveUnits.push_back(unit); roster.push_back(unit);
            }
            for(int i=0;i<4;++i) {
                auto* unit=spawn(owner,Unit_Tank,Coord(3+i,22));
                homeUnits.push_back(unit); roster.push_back(unit);
            }
            publish();
            bot->protectedRallyCycle=std::numeric_limits<Uint32>::max();
            bot->protectedRally=origin+Coord(4,21);
            bot->squadRallyLocation=bot->protectedRally;
            // Sections below are about cohesion, dispatch bookkeeping and the
            // battery, not about where an army forms up, so both anchors are
            // held on the rebuilt fixture army.
            bot->offensiveStaging=bot->protectedRally;
            bot->offensiveStagingCycle=currentGame->getGameCycleCount();
            bot->exteriorStagingAvailable=false; // Isolate commitment accounting.
        }

        // ---- 8. Dispatched attackers hunt, and stay hunting ----------------
        //
        // The approved behaviour: a dispatched Custom Hard/Brutal attacker uses
        // the engine's own Hunt. This controller does not micro an attack - no
        // per-unit objective, no per-unit movement while offensive - and the
        // ordinary assembly pass must not reclaim a committed attacker.
        {
            bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
            bot->customWave.members.clear();
            bot->customWave.front=NONE_ID;
            auto* objective=place(enemy,Structure_Refinery,Coord(34,6));
            require(objective,"Recovery fixture objective placement failed");
            for(auto* unit:waveUnits){bot->doSetAttackMode(unit,GUARD);unit->setForced(false);
                bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);unit->setForced(false);}
            for(auto* unit:homeUnits){bot->doSetAttackMode(unit,GUARD);unit->setForced(false);
                bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);unit->setForced(false);}
            bot->militaryValueLimit=1;      // Readiness is satisfied by the fixture army.
            bot->launchGroundHunt();
            require(!bot->customWave.members.empty(),"A ready Brutal wave was not tracked");
            int hunting=0;
            for(const auto id:bot->customWave.members) {
                const auto* unit=dynamic_cast<const UnitBase*>(bot->getObject(id));
                require(unit,"Tracked wave member vanished");
                if(!unit->isAGroundUnit()) continue;
                require(unit->getAttackMode()==HUNT,
                    "A dispatched attacker was not left on the engine hunt");
                require(!unit->wasForced(),
                    "A dispatched attacker was given a forced order by the controller");
                ++hunting;
            }
            require(hunting>0,"No ground attacker was dispatched at all");
            // The observed objective is recorded as the front anchor the
            // strength comparison uses. It is intelligence, not an order: no
            // member is pointed at it.
            if(bot->customWave.front!=NONE_ID) {
                const auto* anchor=bot->getObject(bot->customWave.front);
                require(anchor && anchor->isAStructure() && anchor->getOwner()
                    && anchor->getOwner()->getTeamID()!=owner->getTeamID()
                    && anchor->isVisible(owner->getTeamID()),
                    "The recorded front anchor is not an observed enemy structure");
                for(const auto id:bot->customWave.members) {
                    const auto* unit=dynamic_cast<const UnitBase*>(bot->getObject(id));
                    if(!unit || !unit->isAGroundUnit()) continue;
                    require(unit->getTarget()!=anchor || !unit->wasForced(),
                        "A dispatched attacker was forced onto a controller-chosen target");
                }
            }
            // Ordinary assembly runs every pass. It must leave the attackers be.
            {
                std::vector<std::pair<Uint32,ATTACKMODE>> before;
                for(const auto id:bot->customWave.members) {
                    const auto* unit=dynamic_cast<const UnitBase*>(bot->getObject(id));
                    if(unit) before.push_back({id,unit->getAttackMode()});
                }
                for(int pass=0;pass<3;++pass) {
                    int budget=12;
                    bot->applyArmyPosture(budget);
                    bot->assignColonyGuards(budget);
                    for(const auto& entry:before) {
                        const auto* unit=dynamic_cast<const UnitBase*>(bot->getObject(entry.first));
                        if(!unit) continue;
                        bot->moveToOptimalSquadPosition(unit,3);
                    }
                }
                for(const auto& entry:before) {
                    const auto* unit=dynamic_cast<const UnitBase*>(bot->getObject(entry.first));
                    require(unit,"An attacker disappeared during ordinary updates");
                    require(unit->getAttackMode()==entry.second,
                        "Ordinary updates reset a dispatched attacker's attack mode");
                }
                require(!bot->customWave.members.empty(),
                    "Ordinary updates released the committed attackers");
            }
            // A repeated pass must not stack a second wave on top of the first.
            const size_t first=bot->customWave.members.size();
            bot->launchGroundHunt();
            require(bot->customWave.members.size()==first,
                "A repeated dispatch stacked another wave onto the tracked one");
            objective->setHealth(0);
        }

        // ---- 9. Readiness gate replaces indefinite fresh hunts -------------
        {
            bot->customWave.members.clear();
            bot->customWave.front=NONE_ID;
            for(auto* unit:waveUnits){bot->doSetAttackMode(unit,GUARD);unit->setForced(false);}
            for(auto* unit:homeUnits){bot->doSetAttackMode(unit,GUARD);unit->setForced(false);}
            bot->militaryValueLimit=1000000;   // Far above anything assembled.
            bot->launchGroundHunt();
            require(bot->customWave.members.empty(),
                "An unready Brutal house still issued a fresh wave");
            int hunters=0;
            for(auto* unit:waveUnits) if(unit->getAttackMode()==HUNT) ++hunters;
            for(auto* unit:homeUnits) if(unit->getAttackMode()==HUNT) ++hunters;
            require(hunters==0,"An unready house dripped hunters out anyway");
            bot->militaryValueLimit=1;
            // One whole-army launch: remote, unassembled reserves leave with
            // the rest rather than waiting for a staging cohort to form.
            std::vector<UnitBase*> remote;
            for(int i=0;i<16;++i) {
                auto* unit=spawn(owner,Unit_Tank,Coord(24+i%8,2+i/8));
                remote.push_back(unit); roster.push_back(unit);
            }
            publish();
            for(auto* unit:waveUnits){bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);unit->setForced(false);}
            for(auto* unit:homeUnits){bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);unit->setForced(false);}
            bot->launchGroundHunt();
            require(!bot->customWave.members.empty(),"A ready Brutal house did not launch at all");
            for(auto* unit:remote) require(bot->customWave.members.count(unit->getObjectID())
                && unit->getAttackMode()==HUNT,
                "A whole-army dispatch left a remote reserve at home");
            for(auto* unit:waveUnits) require(unit->getAttackMode()==HUNT,
                "A whole-army dispatch left a home unit behind");
            for(auto* unit:homeUnits) require(unit->getAttackMode()==HUNT,
                "A whole-army dispatch left a home unit behind");
            for(auto* unit:remote) retire(unit);
            bot->customWave.members.clear();
        }

        // ---- 10. A blocked or dangerous rally falls back safely ------------
        {
            // Stand an observed enemy on the shelter anchor so the whole search
            // window is inside its danger stamp, and require the result to be a
            // genuinely safe, standable, passable tile - or no shelter at all.
            // "Not a mountain OR zero danger" would have admitted a dangerous
            // ordinary tile, which is not a fallback, it is the thing being
            // avoided.
            const Coord anchor=bot->shelterAnchor();
            require(anchor.isValid(),"Fixture house has no shelter anchor");
            auto* intruder=spawn(enemy,Unit_SiegeTank,anchor-origin);
            roster.push_back(intruder); publish();
            bot->dangerUpdated=std::numeric_limits<Uint32>::max();
            bot->refreshTacticalDanger();
            bot->protectedRallyCycle=std::numeric_limits<Uint32>::max();
            bot->protectedRally=Coord::Invalid();
            const Coord fallback=bot->findProtectedRallyLocation();
            SDL_Log("ARMY_RECOVERY_DIAG: rally fallback anchor=%d,%d result=%d,%d valid=%d",
                anchor.x,anchor.y,fallback.x,fallback.y,fallback.isValid());
            if(fallback.isValid()) {
                require(currentGameMap->tileExists(fallback),"The rally fallback is off the map");
                const auto* tile=currentGameMap->getTile(fallback);
                require(!tile->isMountain(),"The rally fallback is a mountain");
                require(!tile->hasAStructure(),"The rally fallback is inside a building");
                require(bot->dangerAt(fallback)==0,
                    "The rally fallback stands in observed enemy fire");
                require(waveUnits.front()->canPass(fallback.x,fallback.y),
                    "The rally fallback is not passable for the units sent to it");
            }
            // An unsafe anchor is re-chosen at once rather than being held for
            // the thirty-second search cadence.
            {
                bot->protectedRally=anchor;      // Now inside the danger stamp.
                bot->protectedRallyCycle=game.getGameCycleCount();
                const Coord rechosen=bot->findProtectedRallyLocation();
                require(rechosen!=anchor || !fallback.isValid(),
                    "An unsafe rally was held through the search throttle");
            }
            // A unit is never ordered to an invalid or impassable destination.
            require(!bot->withdrawUnit(waveUnits.front(),Coord::Invalid()),
                "A withdrawal was issued to an invalid destination");
            retire(intruder);
            bot->dangerUpdated=std::numeric_limits<Uint32>::max();
            bot->refreshTacticalDanger();
            bot->protectedRallyCycle=std::numeric_limits<Uint32>::max();
            bot->protectedRally=origin+Coord(4,21);
        }

        // ---- 10b. Two production colonies far apart --------------------------
        //
        // The arithmetic mean of two colonies is the open sand between them.
        // The shelter anchor has to be one of the actual colonies, and the army
        // has to gather and scatter there.
        {
            const int far=std::min(currentGameMap->getSizeY()-6,origin.y+60);
            std::vector<StructureBase*> southColony;
            auto placeFar=[&](Uint32 item,int dx,int dy) {
                auto* s=owner->placeStructure(NONE_ID,item,origin.x+dx,far+dy,true,true);
                if(s){s->setVisible(owner->getTeamID(),true);
                    fixtureStructureIds.push_back(s->getObjectID());southColony.push_back(s);}
                return s;
            };
            // A second, stronger colony: more core buildings and its own cover.
            const bool built=placeFar(Structure_ConstructionYard,4,0)
                && placeFar(Structure_Refinery,8,0) && placeFar(Structure_HeavyFactory,4,4)
                && placeFar(Structure_RepairYard,8,4) && placeFar(Structure_StarPort,12,0);
            placeFar(Structure_RocketTurret,7,2);
            placeFar(Structure_RocketTurret,9,2);
            placeFar(Structure_RocketTurret,5,2);
            if(built) {
                const Coord mean=bot->findBaseCentre(owner->getHouseID());
                const Coord anchor=bot->shelterAnchor();
                SDL_Log("ARMY_RECOVERY_DIAG: two colonies mean=%d,%d anchor=%d,%d north=%d,%d south=%d,%d",
                    mean.x,mean.y,anchor.x,anchor.y,origin.x+4,origin.y+4,origin.x+4,far);
                // The anchor is an actual owned core building, not the mean.
                bool onCore=false;
                for(const auto* s:structureList)
                    if(s->getOwner()==owner && s->getHealth()>0 && s->getLocation()==anchor
                        && RocketTurretPolicy::coreAsset(s->getItemID())) onCore=true;
                require(onCore,"The shelter anchor is not one of this house's core buildings");
                // And it is the better-covered colony, not the empty middle.
                require(blockDistance(anchor,mean).lround()>4
                        || blockDistance(Coord(origin.x+4,origin.y+4),mean).lround()<=4,
                    "The shelter anchor collapsed onto the structural mean");
                // The army actually gathers there, scattered rather than stacked.
                bot->dangerUpdated=std::numeric_limits<Uint32>::max();
                bot->refreshTacticalDanger();
                bot->protectedRallyCycle=std::numeric_limits<Uint32>::max();
                bot->protectedRally=Coord::Invalid();
                const Coord rally=bot->findProtectedRallyLocation();
                bot->protectedRally=rally;
                require(rally.isValid(),"No rally near a real two-colony shelter");
                require(blockDistance(rally,anchor).lround()
                        <=std::max(8,QuantBot::assemblyRadius(int(bot->customWave.members.size()))),
                    "The rally is not at the chosen shelter colony");
                bot->armyPosture=ArmyPosturePolicy::Posture::Withdrawing;
                bot->postureSince=game.getGameCycleCount();
                bot->customWave.members.clear();
                for(auto* unit:waveUnits) bot->customWave.members.insert(unit->getObjectID());
                for(auto* unit:waveUnits){bot->doSetAttackMode(unit,HUNT);unit->setForced(false);}
                int budget=getQuantBotConfig().recovery.recallOrdersPerPass;
                bot->applyArmyPosture(budget);
                std::set<int64_t> destinations;
                int ordered=0;
                const int spread=bot->armyAssemblyRadius();
                for(auto* unit:waveUnits)
                    if(unit->wasForced() && unit->getDestination().isValid()
                        && blockDistance(unit->getDestination(),rally).lround()<=spread) {
                        ++ordered; destinations.insert(tileKey(unit->getDestination()));
                    }
                const size_t room=bot->assemblySlots(rally,spread).size();
                SDL_Log("ARMY_RECOVERY_DIAG: two colonies recalled=%d distinct_slots=%zu room=%zu",
                    ordered,destinations.size(),room);
                require(ordered>=4,"The army was not recalled to the chosen colony");
                require(room<2 || destinations.size()>1,
                    "Every recalled unit was sent to the same tile although the area had room");
                bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
                bot->customWave.members.clear();

                // Colony-aware offensive staging. Whichever colony is chosen,
                // it has to be a real core building of this house, and it has
                // to be one that can hold ground: either the strongest cluster
                // or one with its own living cover. An uncovered outpost is
                // never where the army is told to mass.
                bot->offensiveStagingCycle=std::numeric_limits<Uint32>::max();
                const Coord stageAt=bot->stagingAnchor();
                const Coord stage=bot->offensiveStagingLocation();
                const int turretReach=std::max(1,currentGame->objectData.data
                    [Structure_RocketTurret][owner->getHouseID()].weaponrange-1);
                bool stageOnCore=false, stageCovered=false;
                for(const auto* s:structureList) {
                    if(s->getOwner()!=owner || s->getHealth()<=0) continue;
                    if(s->getLocation()==stageAt && RocketTurretPolicy::coreAsset(s->getItemID()))
                        stageOnCore=true;
                    if((s->getItemID()==Structure_RocketTurret || s->getItemID()==Structure_GunTurret)
                        && blockDistance(s->getLocation(),stageAt).lround()<=turretReach)
                        stageCovered=true;
                }
                SDL_Log("ARMY_RECOVERY_DIAG: staging colony anchor=%d,%d shelter_anchor=%d,%d "
                    "slot=%d,%d on_core=%d covered=%d",
                    stageAt.x,stageAt.y,anchor.x,anchor.y,stage.x,stage.y,
                    stageOnCore?1:0,stageCovered?1:0);
                require(stageOnCore,"The staging colony is not one of this house's core buildings");
                require(stageCovered || stageAt==anchor,
                    "An uncovered colony was chosen to mass the army at");
                require(stage.isValid()
                        && blockDistance(stage,stageAt).lround()>bot->colonyFootprintRadius(stageAt),
                    "Two-colony staging is inside the chosen colony's footprint");

                // Bounded local protection for the colony that is NOT the one
                // the army is forming up at.
                const Coord northColony=Coord(origin.x+4,origin.y+4);
                const Coord southColony=Coord(origin.x+4,far);
                const Coord assembly=bot->armyAssemblyAnchor();
                const Coord threatened=
                    blockDistance(assembly,northColony).lround()>blockDistance(assembly,southColony).lround()
                    ? northColony : southColony;
                std::vector<UnitBase*> colonyRaid;
                for(int i=0;i<8;++i) {
                    auto* unit=enemy->createUnit(Unit_SiegeTank);
                    require(unit,"Colony-guard fixture unit creation failed");
                    unit->deploy(threatened+Coord(2+i%4,3+i/4));
                    unit->setVisible(owner->getTeamID(),true);
                    colonyRaid.push_back(unit); roster.push_back(unit);
                }
                publish();
                bot->dangerUpdated=std::numeric_limits<Uint32>::max();
                bot->refreshTacticalDanger();
                bot->colonyGuardCycle=std::numeric_limits<Uint32>::max();
                // A small army is never taxed for a garrison: the reserve only
                // comes out of a house that can clearly spare it.
                require(bot->colonyGuardPosts().empty(),
                    "A twelve-unit house was taxed for a colony garrison");
                // Spare troops, deliberately out of defence range of the raided
                // colony: they are the reserve the garrison may draw on, not
                // local cover that would make the colony look already held.
                std::vector<UnitBase*> spare;
                for(int i=0;i<8;++i) {
                    auto* unit=spawn(owner,Unit_Tank,Coord(16+i,26));
                    spare.push_back(unit); roster.push_back(unit);
                }
                publish();
                bot->customWave.members.clear();
                for(auto* unit:waveUnits) bot->customWave.members.insert(unit->getObjectID());
                bot->colonyGuardCycle=std::numeric_limits<Uint32>::max();
                const auto posts=bot->colonyGuardPosts();
                SDL_Log("ARMY_RECOVERY_DIAG: colony guard threatened=%d,%d assembly=%d,%d posts=%zu",
                    threatened.x,threatened.y,assembly.x,assembly.y,posts.size());
                require(!posts.empty(),"A raided colony received no local protection at all");
                require(int(posts.size())<=QuantBot::kColonyGuardMax,
                    "The colony garrison exceeded its finite reserve");
                for(const auto& entry:posts) {
                    require(blockDistance(entry.second,threatened).lround()
                            <=2*turretReach+8,
                        "A colony guard was posted away from the raided colony");
                    require(!bot->customWave.members.count(entry.first),
                        "A colony garrison took a unit already committed to an offensive");
                }
                // Selection and execution share a cycle, but a later command
                // can still claim a cached pick. Execution must respect that.
                auto picked=posts.begin();
                const auto* dispatched=dynamic_cast<const UnitBase*>(bot->getObject(picked->first));
                require(dispatched,"Cached colony pick disappeared");
                bot->doSetAttackMode(dispatched,HUNT);
                ++picked;
                const auto* defender=dynamic_cast<const UnitBase*>(bot->getObject(picked->first));
                require(defender,"Second cached colony pick disappeared");
                const Coord defenceDestination=defender->getDestination();
                bot->defenceAssignments[defender->getObjectID()]=colonyRaid.front()->getObjectID();
                {
                    int budget=4;
                    bot->assignColonyGuards(budget);
                    require(budget>=4-int(posts.size()),
                        "Colony guard orders exceeded their share of the order budget");
                }
                require(dispatched->getAttackMode()==HUNT,
                    "Cached colony execution stole a newly dispatched hunter");
                require(defender->getDestination()==defenceDestination,
                    "Cached colony execution stole an emergency defender");
                bot->defenceAssignments.erase(defender->getObjectID());
                bot->doSetAttackMode(dispatched,GUARD);
                for(auto* unit:colonyRaid) retire(unit);
                bot->dangerUpdated=std::numeric_limits<Uint32>::max();
                bot->refreshTacticalDanger();
                bot->colonyGuardCycle=std::numeric_limits<Uint32>::max();
                const auto quietPosts=bot->colonyGuardPosts();
                require(!quietPosts.empty() && quietPosts.size()<=size_t(QuantBot::kColonyGuardMax),
                    "A quiet forward colony lost its bounded defensive post");
                for(const auto& entry:quietPosts) {
                    require(!bot->customWave.members.count(entry.first),
                        "A quiet colony took a committed attacker");
                    require(blockDistance(entry.second,assembly).lround()>turretReach*2,
                        "A quiet colony duplicated the army's existing assembly garrison");
                }
                for(auto* unit:spare) retire(unit);
                bot->colonyGuardCycle=std::numeric_limits<Uint32>::max();
                bot->offensiveStagingCycle=std::numeric_limits<Uint32>::max();
                bot->customWave.members.clear();
            }
            for(auto* s:southColony) if(s) s->setHealth(0);
            bot->dangerUpdated=std::numeric_limits<Uint32>::max();
            bot->refreshTacticalDanger();
            bot->protectedRallyCycle=std::numeric_limits<Uint32>::max();
            bot->protectedRally=origin+Coord(4,21);
        }

        // ---- 11. Human and forced player orders are respected -------------
        {
            auto* manual=homeUnits.front();
            bot->onHumanUnitOrder(manual->getObjectID());
            require(!bot->orderableCombatUnit(manual),
                "A human-ordered unit is still treated as the bot's to command");
            bot->customWave.members.insert(manual->getObjectID());
            bot->armyPosture=ArmyPosturePolicy::Posture::Withdrawing;
            bot->doMove2Pos(manual,manual->getX(),manual->getY(),true);
            const Coord manualDestination=manual->getDestination();
            int budget=8;
            bot->applyArmyPosture(budget);
            require(manual->getDestination()==manualDestination,
                "The recall overrode a human order");
            bot->updateCustomWave();
            require(!bot->customWave.members.count(manual->getObjectID()),
                "A human-ordered unit stayed in the tracked wave");
            bot->manualUnitOrders.clear();
            bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
        }

        // ---- 12. Front battery: goal, legality, corridors, economy ---------
        {
            bot->lastSurveyCycle=std::numeric_limits<Uint32>::max();
            bot->lastSurvey=bot->surveyArmy();
            // Economy gate: no battery without refineries, a factory and a yard.
            require(bot->frontBatteryGoal(27,1,1,1,bot->lastSurvey)==0,
                "A battery was planned before the economy that pays for it");
            const int goal=bot->frontBatteryGoal(27,2,1,1,bot->lastSurvey);
            require(goal>=24,"A mature base did not scale past twenty-four emplacements");
            const int small=bot->frontBatteryGoal(9,2,1,1,bot->lastSurvey);
            require(small>=FrontBatteryPolicy::kOpeningBandMin
                && small<=FrontBatteryPolicy::kOpeningBandMax+1,
                "A small base did not land in the opening band");
            // A ground-only opponent must still produce useful defence.
            require(goal>2,"A ground-only front still capped the battery at the baseline");

            // Siting: legal, spaced, corridor-preserving and enemy-facing.
            auto* enemyBase=place(enemy,Structure_ConstructionYard,Coord(34,20));
            require(enemyBase,"Battery fixture enemy base placement failed");
            bot->dangerUpdated=std::numeric_limits<Uint32>::max();
            bot->refreshTacticalDanger();
            const Coord base=bot->findBaseCentre(owner->getHouseID());
            const Coord forward=bot->observedFrontDirection(base);
            require(forward.x!=0 || forward.y!=0,
                "No observed front direction from a visible enemy base");
            bot->turretSiteCache.clear();
            // The battery's own entry point, which is what the build rule uses.
            // In city mode findEffectiveTurretPlaceLocation would route to the
            // coverage planner, which has no front at all, so this must not be
            // the same call.
            const Coord site=bot->findFrontBatteryPlaceLocation();
            require(site.isValid(),"No legal battery site on a clear map");
            SDL_Log("ARMY_RECOVERY_DIAG: battery city_sim=%d site=%d,%d",
                currentGame->isCitySimEnabled()?1:0,site.x,site.y);
            if(currentGame->isCitySimEnabled()) {
                // In city mode the battery still has to be a legal city
                // placement. The zone-landlock rule lives in an anonymous
                // namespace in QuantBot.cpp, so it is enforced inside the
                // search rather than re-evaluated here; what is checked here is
                // that the site the search returns really is placeable and does
                // not sit on a city zone.
                require(!currentGameMap->getTile(site)->hasCityZone(),
                    "The city battery site is on a city zone tile");
                // And the ordinary coverage planner must be untouched: it keeps
                // answering its own question about critical and colony cover.
                bot->cityTurretSearch.reset();
                const Coord coverage=bot->findCityTurretPlaceLocation(Structure_RocketTurret);
                SDL_Log("ARMY_RECOVERY_DIAG: battery coverage_site=%d,%d valid=%d",
                    coverage.x,coverage.y,coverage.isValid());
                require(!coverage.isValid()
                        || currentGameMap->okayToPlaceStructure(coverage.x,coverage.y,1,1,false,
                               owner,false,Structure_RocketTurret),
                    "The ordinary city coverage planner stopped returning legal sites");
            }
            require(currentGameMap->okayToPlaceStructure(site.x,site.y,1,1,false,owner,false,
                        Structure_RocketTurret),
                "The battery site is not a legal placement");
            require(bot->preservesGroundAccess(Structure_RocketTurret,site),
                "The battery site closes local ground access or a factory exit");
            int passable=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                if(!dx && !dy)continue;
                if(!currentGameMap->tileExists(site.x+dx,site.y+dy))continue;
                const auto* tile=currentGameMap->getTile(site.x+dx,site.y+dy);
                if(!tile->isMountain() && !tile->hasAStructure())++passable;
            }
            require(FrontBatteryPolicy::keepsCorridor(passable),
                "The battery site does not leave a movement corridor");
            int nearest=-1;
            for(const auto* structure:structureList) {
                if(structure->getOwner()!=owner || structure->getHealth()<=0)continue;
                if(structure->getItemID()!=Structure_RocketTurret
                    && structure->getItemID()!=Structure_GunTurret)continue;
                const int d=std::max(std::abs(structure->getLocation().x-site.x),
                                     std::abs(structure->getLocation().y-site.y));
                if(nearest<0 || d<nearest)nearest=d;
            }
            require(FrontBatteryPolicy::spacedEnough(nearest),
                "The battery site is stacked on an existing emplacement");
            // Side selection follows the live allowance: while the enemy-facing
            // share of the goal is unspent the site must be on that side, and
            // once it is spent the site must be elsewhere, which is how the
            // flank and rear keep their cover.
            int demand=0;
            const Uint32 anchorYard=bot->mainConstructionYardID();
            for(const auto* structure:structureList)
                if(structure->getOwner()==owner && structure->getHealth()>0)
                    demand+=RocketTurretPolicy::desiredCoverage(structure->getItemID(),3,
                        QuantBot::isExpansionYard(structure->getItemID(),structure->getObjectID(),anchorYard));
            const int goalNow=bot->frontBatteryGoal(
                RocketTurretPolicy::coverageTurretCap(demand),
                owner->getNumItems(Structure_Refinery),owner->getNumItems(Structure_HeavyFactory),
                owner->getNumItems(Structure_RepairYard),bot->lastSurvey);
            const int allowance=FrontBatteryPolicy::frontAllowance(goalNow);
            int frontTurrets=0;
            for(const auto* structure:structureList) {
                if(structure->getOwner()!=owner || structure->getHealth()<=0)continue;
                if(structure->getItemID()!=Structure_RocketTurret
                    && structure->getItemID()!=Structure_GunTurret)continue;
                if(FrontBatteryPolicy::onFrontSide(structure->getLocation().x-base.x,
                        structure->getLocation().y-base.y,forward.x,forward.y))++frontTurrets;
            }
            const bool onFront=FrontBatteryPolicy::onFrontSide(site.x-base.x,site.y-base.y,
                forward.x,forward.y);
            SDL_Log("ARMY_RECOVERY_DIAG: battery site=%d,%d base=%d,%d forward=%d,%d goal=%d "
                "allowance=%d front_turrets=%d on_front=%d",
                site.x,site.y,base.x,base.y,forward.x,forward.y,goalNow,allowance,frontTurrets,onFront);
            if(allowance>0 && frontTurrets<allowance)
                require(onFront,"The battery emplacement is not on the observed enemy side");
            else
                require(!onFront,"The enemy-facing allowance did not reserve flank and rear cover");
            // The per-pass cache returns the same site without rescanning.
            require(bot->findTurretPlaceLocation(Structure_RocketTurret)==site,
                "The emplacement pass cache changed its answer");
            enemyBase->setHealth(0);
        }

        // ---- 13. Special allocator learns instead of taking array order ----
        {
            SpecialUnitPolicy::Candidates specials{};
            const std::array<Uint32,3> items={Unit_Devastator,Unit_SonicTank,Unit_Deviator};
            for(size_t s=0;s<items.size();++s) {
                specials[s].item=items[s];
                specials[s].price=currentGame->objectData.data[items[s]][owner->getHouseID()].price;
                specials[s].available=true;
                specials[s].affordable=true;
            }
            // Equal evidence, equal share, both affordable: this is the logged
            // condition that always chose the Devastator from array order.
            const int blind=SpecialUnitPolicy::select(specials);
            require(blind>=0,"No special was selectable with everything available");
            specials[size_t(blind)].committedValue+=specials[size_t(blind)].price;
            const int second=SpecialUnitPolicy::select(specials);
            require(second!=blind,"The special choice did not rotate across the group");
            // Measured evidence then decides, in either direction.
            specials[0].rewardMilli=1'000'000'000; specials[0].lossMilli=50'000'000'000;
            specials[1].rewardMilli=80'000'000'000; specials[1].lossMilli=2'000'000'000;
            specials[2].available=false;
            require(SpecialUnitPolicy::select(specials)==1,
                "The measurably better special was not chosen");
            std::swap(specials[0].rewardMilli,specials[1].rewardMilli);
            std::swap(specials[0].lossMilli,specials[1].lossMilli);
            require(SpecialUnitPolicy::select(specials)==0,
                "The special choice is fixed rather than measured");
        }

        // ---- 14. Save, load and older-save defaults ------------------------
        {
            bot->armyPosture=ArmyPosturePolicy::Posture::Recovering;
            bot->postureSince=4242;
            bot->recallCursor=3;
            bot->localPressureSince=777;
            bot->protectedRally=origin+Coord(6,6);
            bot->protectedRallyCycle=99;
            bot->attrition.lostCost=54321;
            bot->attrition.killCost=12345;
            bot->customWave.members.clear();
            for(auto* unit:waveUnits) bot->customWave.members.insert(unit->getObjectID());
            bot->customWave.front=4711;
            bot->customWave.launched=909;
            OMemoryStream out; out.open();
            bot->save(out);
            const std::string bytes(out.getData(),out.getDataLength());
            IMemoryStream in(bytes.data(),bytes.size());
            const Uint32 savedVersion=currentGame->loadedSavegameVersion;
            currentGame->loadedSavegameVersion=SAVEGAMEVERSION;
            QuantBot restored(in,owner);
            require(restored.armyPosture==ArmyPosturePolicy::Posture::Recovering,
                "Posture did not survive a save and load");
            require(restored.postureSince==4242,"Posture age did not survive a save and load");
            require(restored.recallCursor==3,"Recall cursor did not survive a save and load");
            require(restored.localPressureSince==777,"Local pressure clock was lost");
            require(restored.protectedRally==bot->protectedRally,"Protected rally was lost");
            require(restored.protectedRallyCycle==99,"Rally search throttle was lost");
            require(restored.attrition.lostCost==54321 && restored.attrition.killCost==12345,
                "Attrition ledger totals were lost");
            require(restored.customWave.members==bot->customWave.members,
                "Tracked wave membership was lost");
            require(restored.customWave.front==4711,"Shared objective was lost");
            OMemoryStream again; again.open();
            restored.save(again);
            require(std::string(again.getData(),again.getDataLength())==bytes,
                "Re-saving a loaded bot produced different bytes");
            // A legitimate very large wave must survive the round trip. An
            // unlimited or ten-thousand unit override can field far more than a
            // few thousand attackers, so the stream bound is a corruption check
            // and not a cap on the wave.
            {
                const auto keep=bot->customWave.members;
                for(Uint32 id=1;id<=6000;++id) bot->customWave.members.insert(900000+id);
                require(bot->customWave.members.size()>4096,"Large-wave fixture is too small");
                OMemoryStream big; big.open();
                bot->save(big);
                const std::string bigBytes(big.getData(),big.getDataLength());
                IMemoryStream bigIn(bigBytes.data(),bigBytes.size());
                QuantBot huge(bigIn,owner);
                require(huge.customWave.members==bot->customWave.members,
                    "A legitimate wave larger than four thousand members was rejected or truncated");
                bot->customWave.members=keep;
            }
            // An older save has no posture block; the defaults must stand and
            // no strategic trigger may fire until the window is covered again.
            currentGame->loadedSavegameVersion=9851;
            OMemoryStream legacyOut; legacyOut.open();
            bot->save(legacyOut);
            const std::string legacyBytes(legacyOut.getData(),legacyOut.getDataLength());
            IMemoryStream legacyIn(legacyBytes.data(),legacyBytes.size());
            QuantBot legacy(legacyIn,owner);
            require(legacy.armyPosture==ArmyPosturePolicy::Posture::Offensive,
                "An older save did not default to the offensive posture");
            require(legacy.attrition.count==0 && legacy.attrition.lostCost==0,
                "An older save did not start with an empty attrition ledger");
            require(legacy.customWave.members.empty(),"An older save invented a tracked wave");
            require(legacy.attrition.windowStart(100000,bot->postureThresholds().windowCycles)==nullptr,
                "An older save could trigger on an uncovered attrition window");
            currentGame->loadedSavegameVersion=savedVersion;
        }

        // ---- 14b. An older save's existing attackers are adopted ------------
        //
        // A 9851 save carries Hard/Brutal units that are already hunting. The
        // recall only ever orders tracked members, so without adoption those
        // old attackers would keep trickling forward for the rest of the match
        // while the new reinforcements correctly held at home.
        {
            bot->customWave.members.clear();
            bot->legacyHuntersAdopted=false;      // As an older save loads.
            bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
            for(auto* unit:waveUnits){bot->doSetAttackMode(unit,HUNT);unit->setForced(false);}
            for(auto* unit:homeUnits){bot->doSetAttackMode(unit,AREAGUARD);unit->setForced(false);}
            // A human-ordered hunter is never adopted.
            auto* manual=waveUnits.back();
            bot->onHumanUnitOrder(manual->getObjectID());
            bot->adoptLegacyHuntersIntoWave();
            int adoptedHunters=0;
            for(auto* unit:waveUnits)
                if(bot->customWave.members.count(unit->getObjectID()))++adoptedHunters;
            SDL_Log("ARMY_RECOVERY_DIAG: legacy adoption hunters=%zu adopted=%d members=%zu",
                waveUnits.size(),adoptedHunters,bot->customWave.members.size());
            require(adoptedHunters>=int(waveUnits.size())-1,
                "An older save's existing attackers were not adopted into the tracked wave");
            require(!bot->customWave.members.count(manual->getObjectID()),
                "A human-ordered attacker was adopted into the tracked wave");
            for(auto* unit:homeUnits)
                require(!bot->customWave.members.count(unit->getObjectID()),
                    "A home guard was adopted as if it were an attacker");
            // Adoption is idempotent, and those adopted units are now
            // recallable - which is the whole point.
            const size_t afterFirst=bot->customWave.members.size();
            bot->adoptLegacyHuntersIntoWave();
            require(bot->customWave.members.size()==afterFirst,"Adoption ran twice");
            bot->manualUnitOrders.clear();
            bot->armyPosture=ArmyPosturePolicy::Posture::Withdrawing;
            bot->postureSince=game.getGameCycleCount();
            // Put the rally where the real base is, so the adopted attackers
            // are genuinely away from home and the recall has work to do.
            bot->protectedRallyCycle=std::numeric_limits<Uint32>::max();
            bot->protectedRally=bot->findProtectedRallyLocation();
            require(bot->protectedRally.isValid(),"Adoption fixture lost its rally");
            require(blockDistance(waveUnits.front()->getLocation(),bot->protectedRally).lround()
                    >QuantBot::assemblyRadius(int(bot->customWave.members.size())),
                "Adoption fixture attackers are already at the rally");
            int adoptBudget=getQuantBotConfig().recovery.recallOrdersPerPass;
            bot->applyArmyPosture(adoptBudget);
            int recalledLegacy=0;
            const int legacySpread=QuantBot::assemblyRadius(int(bot->customWave.members.size()));
            for(auto* unit:waveUnits)
                if(unit->wasForced() && unit->getDestination().isValid()
                    && blockDistance(unit->getDestination(),bot->protectedRally).lround()<=legacySpread)
                    ++recalledLegacy;
            require(recalledLegacy>0,
                "Adopted legacy attackers were still never recalled");
            bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
            bot->customWave.members.clear();
            bot->legacyHuntersAdopted=true;
        }

        // ---- 14c. A minor raid on a core asset is not a house emergency -----
        {
            bot->lastSurveyCycle=std::numeric_limits<Uint32>::max();
            auto* core=place(owner,Structure_Refinery,Coord(12,20));
            require(core,"Minor-raid fixture refinery placement failed");
            // A freshly placed building is not necessarily at full health, and
            // "seriously damaged" is one of the two emergency conditions, so the
            // minor-raid case has to start from an undamaged asset.
            core->setHealth(core->getMaxHealth());
            // One cheap raider, with our own tanks and turret cover beside it.
            auto* raider=spawn(enemy,Unit_Trike,Coord(14,21));
            roster.push_back(raider); publish();
            raider->doAttackObject(core,true);
            bot->dangerUpdated=std::numeric_limits<Uint32>::max();
            bot->refreshTacticalDanger();
            auto minor=bot->surveyArmy();
            SDL_Log("ARMY_RECOVERY_DIAG: minor raid threat=%lld holding=%lld damaged=%d emergency=%d",
                (long long)minor.coreThreatPower,(long long)minor.coreHoldingPower,
                minor.coreSeriouslyDamaged?1:0,minor.coreUnderAttack?1:0);
            require(minor.coreThreatPower>0,"The minor raid was not observed at all");
            require(!minor.coreUnderAttack,
                "One raider on a refinery was treated as a house emergency");
            // A tiny raid remains a local defence problem even without cover.
            {
                const auto savedRoster=unitList;
                const auto savedStructures=structureList;
                unitList.clear(); unitList.push_back(raider);
                structureList.clear(); structureList.push_back(core);
                const auto bare=bot->surveyArmy();
                unitList=savedRoster; structureList=savedStructures;
                require(bare.coreThreatPower>0 && bare.coreHoldingPower==0,
                    "The undefended-raid fixture still had cover");
                require(!bare.coreUnderAttack,"One undefended small raid panicked the whole house");
            }
            // A raid that genuinely overwhelms the local defence is one.
            std::vector<UnitBase*> assault;
            for(int i=0;i<16;++i) {
                const Coord at=origin+Coord(12+(i%4),22+(i/4));
                if(!currentGameMap->tileExists(at)
                    || currentGameMap->getTile(at)->hasAGroundObject()
                    || currentGameMap->getTile(at)->isMountain()) continue;
                auto* unit=spawn(enemy,Unit_SiegeTank,at-origin);
                unit->doAttackObject(core,true);
                assault.push_back(unit); roster.push_back(unit);
            }
            publish();
            bot->dangerUpdated=std::numeric_limits<Uint32>::max();
            bot->refreshTacticalDanger();
            const auto heavy=bot->surveyArmy();
            SDL_Log("ARMY_RECOVERY_DIAG: heavy raid threat=%lld holding=%lld emergency=%d",
                (long long)heavy.coreThreatPower,(long long)heavy.coreHoldingPower,
                heavy.coreUnderAttack?1:0);
            require(heavy.coreThreatPower>heavy.coreHoldingPower,
                "Heavy-raid fixture did not out-weigh the local defence");
            require(heavy.coreUnderAttack,
                "A raid that overwhelms the local defence was not an emergency");
            // A half-destroyed core asset lowers the bar; it does not remove it.
            // The live 1.0.803 regression was exactly this: damage never heals
            // on its own, so "wounded and shot at" re-declared a house
            // emergency on every evaluation and both Brutal houses stopped
            // attacking with 63-80M of deployable power in hand.
            for(auto* unit:assault) retire(unit);
            core->setHealth(core->getMaxHealth()/4);
            bot->dangerUpdated=std::numeric_limits<Uint32>::max();
            bot->refreshTacticalDanger();
            const auto wounded=bot->surveyArmy();
            SDL_Log("ARMY_RECOVERY_DIAG: wounded core threat=%lld holding=%lld damaged=%d emergency=%d",
                (long long)wounded.coreThreatPower,(long long)wounded.coreHoldingPower,
                wounded.coreSeriouslyDamaged?1:0,wounded.coreUnderAttack?1:0);
            require(wounded.coreSeriouslyDamaged,
                "The wounded-core fixture asset is not actually half destroyed");
            require(wounded.coreThreatPower<wounded.coreHoldingPower,
                "The wounded-core fixture is not actually well defended");
            require(!wounded.coreUnderAttack,
                "One raider on a well defended, already damaged asset recalled the whole house");
            // The halved bar itself. A raid worth more than half the local
            // defence is an emergency once the asset is wounded and is not one
            // while it is whole; an immaterial raid is never one either way.
            {
                const int64_t floor=std::max<int64_t>(1,bot->postureThresholds().lossFloor)
                    *CombatPowerPolicy::kPowerScale;
                require(!ArmyPosturePolicy::coreEmergency(floor*3,floor*4,false,floor),
                    "A raid smaller than the local defence panicked a whole asset");
                require(ArmyPosturePolicy::coreEmergency(floor*3,floor*4,true,floor),
                    "A half destroyed asset did not lower the bar for the same raid");
                require(!ArmyPosturePolicy::coreEmergency(floor-1,0,true,floor),
                    "An immaterial raid was an emergency because the asset was wounded");
            }
            core->setHealth(core->getMaxHealth());
            bot->dangerUpdated=std::numeric_limits<Uint32>::max();
            bot->refreshTacticalDanger();

            // ---- 14d. The emergency is measured per colony ------------------
            //
            // Two of this house's own core buildings, far enough apart to be
            // separate colonies: the construction yard at the covered base, and
            // the refinery out at (12,20). Both are raided. The question is
            // whether the defence of one is allowed to answer for the other.
            //
            // Substituted unit lists only - no new building is placed, because a
            // fixture structure that is later destroyed leaves rubble behind and
            // changes the world the continuation checkpoint runs in.
            {
                auto block=[&](std::vector<UnitBase*>& into,House* house,Uint32 item,
                               Coord at,int count,int stride) {
                    for(int i=0;i<count;++i) {
                        const Coord spot=origin+at+Coord(i%stride,i/stride);
                        if(!currentGameMap->tileExists(spot)
                            || currentGameMap->getTile(spot)->hasAGroundObject()
                            || currentGameMap->getTile(spot)->isMountain()) continue;
                        auto* unit=spawn(house,item,spot-origin);
                        into.push_back(unit); roster.push_back(unit);
                    }
                };
                // Our defence, massed at the covered base and nowhere near the
                // refinery. Kept deliberately small: every fixture unit that
                // later dies leaves damage on the map, and the continuation
                // checkpoint runs in whatever world this probe leaves behind.
                std::vector<UnitBase*> baseGarrison;
                block(baseGarrison,owner,Unit_Tank,Coord(0,0),7,7);
                // A nuisance on the base, and a real assault on the refinery.
                std::vector<UnitBase*> baseRaid, refineryRaid;
                block(baseRaid,enemy,Unit_Trike,Coord(2,3),1,1);
                block(refineryRaid,enemy,Unit_SiegeTank,Coord(16,22),3,2);
                SDL_Log("ARMY_RECOVERY_DIAG: two-colony fixture base=%zu base_raid=%zu "
                    "refinery_raid=%zu",
                    baseGarrison.size(),baseRaid.size(),refineryRaid.size());
                require(baseGarrison.size()>=6 && !baseRaid.empty() && refineryRaid.size()>=3,
                    "Two-colony fixture could not field both sides");
                publish();
                for(auto* unit:baseRaid) unit->doAttackObject(yard,true);
                for(auto* unit:refineryRaid) unit->doAttackObject(core,true);
                bot->dangerUpdated=std::numeric_limits<Uint32>::max();
                bot->refreshTacticalDanger();
                const auto savedRoster=unitList;
                auto survey=[&](const std::vector<UnitBase*>& defence) {
                    unitList.clear();
                    for(auto* unit:defence) unitList.push_back(unit);
                    for(auto* unit:baseRaid) unitList.push_back(unit);
                    for(auto* unit:refineryRaid) unitList.push_back(unit);
                    unitList.push_back(raider);
                    const auto result=bot->surveyArmy();
                    unitList=savedRoster;
                    bot->lastSurveyCycle=std::numeric_limits<Uint32>::max();
                    return result;
                };
                // The house's own troops already standing at the refinery are
                // the on-site comparison, so no second garrison is spawned.
                std::vector<UnitBase*> refineryGarrison;
                for(auto* unit:waveUnits) refineryGarrison.push_back(unit);
                for(auto* unit:homeUnits) refineryGarrison.push_back(unit);
                const auto elsewhere=survey(baseGarrison);
                const auto onSite=survey(refineryGarrison);
                SDL_Log("ARMY_RECOVERY_DIAG: colony scope elsewhere threat=%lld holding=%lld "
                    "emergency=%d | on_site threat=%lld holding=%lld emergency=%d",
                    (long long)elsewhere.coreThreatPower,(long long)elsewhere.coreHoldingPower,
                    elsewhere.coreUnderAttack?1:0,
                    (long long)onSite.coreThreatPower,(long long)onSite.coreHoldingPower,
                    onSite.coreUnderAttack?1:0);
                // The assault is the same in both surveys, and so is the amount
                // of defence the house owns. Only its location differs.
                require(elsewhere.coreUnderAttack,
                    "A raid on an undefended colony was answered for by another colony's garrison");
                require(elsewhere.coreHoldingPower==0 && elsewhere.coreThreatPower>0,
                    "The reported deciding asset is not the undefended one");
                require(!onSite.coreUnderAttack,
                    "The same raid stayed a house emergency with the defence actually on site");
                require(onSite.coreHoldingPower>onSite.coreThreatPower,
                    "The on-site fixture did not actually out-hold the raid");
                for(auto* unit:refineryRaid) retire(unit);
                for(auto* unit:baseRaid) retire(unit);
                for(auto* unit:baseGarrison) retire(unit);
                bot->lastSurveyCycle=std::numeric_limits<Uint32>::max();
            }
            retire(raider);
            core->setHealth(0);
            bot->dangerUpdated=std::numeric_limits<Uint32>::max();
            bot->refreshTacticalDanger();
        }

        // ---- 15. Fair, bounded recall under a large army, with real frames --
        //
        // Real engine frames run here, so the fixture stops substituting its own
        // unit list: updateGameState() creates and destroys units, and a
        // substituted list would leave the engine iterating objects it no longer
        // owns. The world list is restored first, the crowd is appended to it,
        // and every member is tracked by object id so a unit that dies during
        // the simulation is simply dropped rather than dereferenced.
        {
            unitList.clear();
            currentGame->getObjectManager().forEachObject([&](Uint32,ObjectBase* object) {
                if(auto* unit=dynamic_cast<UnitBase*>(object)) unitList.push_back(unit);
            });
            // Include dead fixture objects so the engine destroys them normally;
            // leaving them in ObjectManager but outside unitList breaks reload.
            worldListRestored=true;
            // Manual calls below occur at the production 50-cycle cadence.
            // Temporarily hold this house's players so House::update does not
            // issue a second recall pass between them. The game player registry
            // and ownership remain intact, and RAII restores them before saves.
            std::list<std::unique_ptr<Player>> heldPlayers;
            heldPlayers.splice(heldPlayers.end(),owner->players);
            struct RestorePlayers {
                House* house;
                std::list<std::unique_ptr<Player>>& held;
                ~RestorePlayers(){house->players.splice(house->players.end(),held);}
            } restorePlayers{owner,heldPlayers};
            bot->armyPosture=ArmyPosturePolicy::Posture::Withdrawing;
            bot->postureSince=game.getGameCycleCount();
            bot->customWave.members.clear();
            std::vector<Uint32> crowdIds;
            // A deliberately large tracked wave: the acceptance target is a
            // nine-hundred-unit army, so the fixture fills the whole clear area.
            for(int i=0;i<40*28;++i) {
                const Coord at=origin+Coord(i%40,i/40);
                if(!currentGameMap->tileExists(at) || currentGameMap->getTile(at)->hasAGroundObject()
                    || currentGameMap->getTile(at)->isMountain()) continue;
                auto* unit=owner->createUnit(Unit_Trooper);
                if(!unit) break;
                unit->deploy(at);
                crowdIds.push_back(unit->getObjectID());
                bot->customWave.members.insert(unit->getObjectID());
            }
            auto live=[&](Uint32 id)->UnitBase* {
                auto* object=currentGame->getObjectManager().getObject(id);
                auto* unit=dynamic_cast<UnitBase*>(object);
                return (unit && unit->getHealth()>0) ? unit : nullptr;
            };
            SDL_Log("ARMY_RECOVERY_DIAG: stress fielded=%zu units total_units=%zu",
                crowdIds.size(),unitList.size());
            require(crowdIds.size()>=900,"Stress fixture could not field a nine-hundred-unit wave");
            bot->protectedRallyCycle=std::numeric_limits<Uint32>::max();
            bot->protectedRally=bot->findProtectedRallyLocation();
            require(bot->protectedRally.isValid(),"Stress fixture lost its rally");
            const int budgetPerPass=getQuantBotConfig().recovery.recallOrdersPerPass;
            std::set<Uint32> everOrdered;
            bot->recallCursor=0;
            int passes=0;
            if(auto* sample=live(crowdIds.front())) {
                SDL_Log("ARMY_RECOVERY_DIAG: stress orderable=%d members=%zu pathqueue=%zu "
                    "rally=%d,%d dist=%d",
                    bot->orderableCombatUnit(sample),bot->customWave.members.size(),
                    currentGame->getPathRequestQueueSize(),
                    bot->protectedRally.x,bot->protectedRally.y,
                    blockDistance(sample->getLocation(),bot->protectedRally).lround());
            }
            // Every pass must make progress and must respect its budget, and
            // repeated passes must eventually reach every member: that is the
            // fairness the cursor exists for.
            int lastProgress=0;
            // The guarantee under test is that every member the bot may order
            // eventually receives its order. A fixture unit the engine left
            // inactive is not one of those, and is counted separately below.
            auto pendingOrderable=[&] {
                const int homeRadius=bot->armyAssemblyRadius();
                int waiting=0;
                for(const Uint32 id:crowdIds) {
                    auto* unit=live(id);
                    if(unit && bot->orderableCombatUnit(unit) && !everOrdered.count(id)
                        && !bot->defenceAssignments.count(id)
                        && !bot->reserveDamagedUnitForRepair(unit)
                        && blockDistance(unit->getLocation(),bot->protectedRally).lround()
                            >homeRadius) ++waiting;
                }
                return waiting;
            };
            // Real simulation frames, not a bare loop of AI calls: the engine
            // services movement and the path queue between passes, which is the
            // only way the recall latency, the queue peak and the actual
            // movement below mean anything.
            std::map<Uint32,Coord> startedAt;
            for(const Uint32 id:crowdIds) if(auto* unit=live(id)) startedAt[id]=unit->getLocation();
            const Uint32 stressStartCycle=game.getGameCycleCount();
            size_t queuePeak=currentGame->getPathRequestQueueSize();
            int maxIssuedInOnePass=0;
            while(passes<600 && pendingOrderable()>0) {
                int budget=budgetPerPass;
                const auto before=budget;
                bot->applyArmyPosture(budget);
                require(before-budget<=budgetPerPass,"A recall pass exceeded its order budget");
                maxIssuedInOnePass=std::max(maxIssuedInOnePass,before-budget);
                const int issuedRadius=bot->armyAssemblyRadius();
                for(const Uint32 id:crowdIds) if(auto* unit=live(id))
                    if(unit->wasForced() && unit->getDestination().isValid()
                        && blockDistance(unit->getDestination(),bot->protectedRally).lround()<=issuedRadius)
                        everOrdered.insert(id);
                // One AI interval of real engine time between passes.
                for(int frame=0;frame<kAiInterval;++frame) {
                    game.negotiatedBudget=5000;
                    game.carryOverTokens=0;
                    game.updateGameState();
                    queuePeak=std::max(queuePeak,currentGame->getPathRequestQueueSize());
                }
                const int spread=bot->armyAssemblyRadius();
                for(const Uint32 id:crowdIds) {
                    auto* unit=live(id);
                    if(unit && (blockDistance(unit->getLocation(),bot->protectedRally).lround()<=spread
                        || (unit->wasForced() && unit->getDestination().isValid()
                            && blockDistance(unit->getDestination(),bot->protectedRally).lround()<=spread)))
                        everOrdered.insert(id);
                }
                ++passes;
                if(passes%25==0) {
                    SDL_Log("ARMY_RECOVERY_DIAG: recall pass=%d ordered=%zu of %zu cursor=%u "
                        "budget_left=%d queue=%zu cycle=%u",
                        passes,everOrdered.size(),crowdIds.size(),bot->recallCursor,budget,
                        currentGame->getPathRequestQueueSize(),game.getGameCycleCount());
                    if(int(everOrdered.size())<=lastProgress) {
                        for(const Uint32 id:crowdIds) if(!everOrdered.count(id))
                            if(auto* unit=live(id)) SDL_Log("ARMY_RECOVERY_REMAINING: id=%u orderable=%d defender=%d "
                                "mode=%d moving=%d forced=%d loc=%d,%d target=%d,%d pending=%d",
                                id,bot->orderableCombatUnit(unit),int(bot->defenceAssignments.count(id)),
                                int(unit->getAttackMode()),unit->isMoving(),unit->wasForced(),
                                unit->getX(),unit->getY(),unit->getDestination().x,unit->getDestination().y,
                                unit->hasPendingPathRequest());
                    }
                    require(int(everOrdered.size())>lastProgress,
                        "A block of recall passes made no progress at all");
                    lastProgress=int(everOrdered.size());
                }
            }
            int inactiveMembers=0, deadMembers=0, defenderMembers=0, repairMembers=0;
            for(const Uint32 id:crowdIds) {
                auto* unit=live(id);
                if(!unit) { ++deadMembers; continue; }
                if(!bot->orderableCombatUnit(unit))++inactiveMembers;
                else if(bot->defenceAssignments.count(id))++defenderMembers;
                else if(bot->reserveDamagedUnitForRepair(unit))++repairMembers;
            }
            SDL_Log("ARMY_RECOVERY_DIAG: recall finished passes=%d ordered=%zu of %zu inactive=%d dead=%d",
                passes,everOrdered.size(),crowdIds.size(),inactiveMembers,deadMembers);
            require(pendingOrderable()==0,
                "The fair recall cursor starved part of a large wave");
            const int accountable=int(crowdIds.size())-deadMembers;
            require(inactiveMembers*20<=accountable,
                "Stress fixture failed to activate most of its units");
            require(int(everOrdered.size())+inactiveMembers+deadMembers+defenderMembers+repairMembers>=int(crowdIds.size()),
                "Some tracked members were neither ordered, inactive nor destroyed");
            require(maxIssuedInOnePass<=budgetPerPass,
                "A pass issued more orders than its configured bound");
            // Continue the actual movement until a coherent majority reaches
            // shelter, or the bounded travel window expires. No repeated AI
            // orders are needed for troops already walking home.
            bool assemblyReached=false;
            int arrivalFrames=0;
            for(;arrivalFrames<24000 && !assemblyReached;arrivalFrames+=kAiInterval) {
                for(int frame=0;frame<kAiInterval;++frame) {
                    game.negotiatedBudget=5000; game.carryOverTokens=0;
                    game.updateGameState();
                    queuePeak=std::max(queuePeak,currentGame->getPathRequestQueueSize());
                }
                const int spread=bot->armyAssemblyRadius();
                int survivors=0,home=0;
                for(const Uint32 id:crowdIds) if(auto* unit=live(id)) {
                    ++survivors;
                    if(blockDistance(unit->getLocation(),bot->protectedRally).lround()<=spread) ++home;
                }
                assemblyReached=survivors>=600 && home*100>=survivors*80;
                if(arrivalFrames%1000==0 || assemblyReached) {
                    SDL_Log("ARMY_RECOVERY_ARRIVAL: frames=%d survivors=%d home=%d radius=%d queue=%zu",
                        arrivalFrames+kAiInterval,survivors,home,spread,currentGame->getPathRequestQueueSize());
                    if(auto* unit=live(crowdIds.front())) SDL_Log("ARMY_RECOVERY_ARRIVAL_SAMPLE: loc=%d,%d "
                        "target=%d,%d moving=%d forced=%d pending=%d path=%zu timer=%d",
                        unit->getX(),unit->getY(),unit->getDestination().x,unit->getDestination().y,
                        unit->isMoving(),unit->wasForced(),unit->hasPendingPathRequest(),
                        unit->pathList.size(),unit->recalculatePathTimer);
                }
            }
            require(assemblyReached,"A large recalled army never assembled at shelter");
            deadMembers=0; inactiveMembers=0;
            for(const Uint32 id:crowdIds) {
                auto* unit=live(id);
                if(!unit) ++deadMembers;
                else if(!bot->orderableCombatUnit(unit)) ++inactiveMembers;
            }
            // Actual arrival and movement, rather than destination flags alone.
            {
                int moved=0, closer=0, measured=0, arrived=0;
                const int homeRadius=bot->armyAssemblyRadius();
                for(const Uint32 id:crowdIds) {
                    auto* unit=live(id);
                    if(!unit || !startedAt.count(id))continue;
                    ++measured;
                    if(blockDistance(unit->getLocation(),bot->protectedRally).lround()
                        <=homeRadius)++arrived;
                    const Coord from=startedAt[id];
                    if(unit->getLocation()!=from)++moved;
                    if(blockDistance(unit->getLocation(),bot->protectedRally).lround()
                        <blockDistance(from,bot->protectedRally).lround())++closer;
                }
                const Uint32 latency=game.getGameCycleCount()-stressStartCycle;
                SDL_Log("ARMY_RECOVERY_STRESS: wave=%zu measured=%d budget=%d max_issued=%d "
                    "passes=%d cycles=%u queue_peak=%zu queue_now=%zu moved=%d closer=%d "
                    "inactive=%d dead=%d arrived=%d",
                    crowdIds.size(),measured,budgetPerPass,maxIssuedInOnePass,passes,latency,
                    queuePeak,currentGame->getPathRequestQueueSize(),moved,closer,
                    inactiveMembers,deadMembers,arrived);
                require(measured>=600,"Too few surviving members to measure movement");
                require(moved*2>=measured,
                    "Most of the recalled army never actually moved under real frames");
                require(closer*2>=measured,
                    "The recalled army did not get measurably closer to its rally");
            }
            // Backpressure bounds fresh additions but permits replacement of pending work.
            for(const Uint32 id:crowdIds) if(auto* unit=live(id)) {
                unit->setForced(false); bot->doSetAttackMode(unit,GUARD);
                bot->doMove2Pos(unit,unit->getX(),unit->getY(),false); unit->setForced(false);
            }
            const size_t queuedBefore=currentGame->pathRequestQueue.size();
            for(int i=0;i<200;++i) {
                Game::PathRequest request;
                request.objectId=crowdIds.front();
                currentGame->pathRequestQueue.push_back(request);
            }
            require(currentGame->getPathRequestQueueSize()>150,
                "Backpressure fixture did not congest the path queue");
            std::map<Uint32,bool> pendingBefore;
            for(const Uint32 id:crowdIds) if(auto* unit=live(id))
                pendingBefore[id]=unit->hasPendingPathRequest();
            int budget=budgetPerPass;
            bot->applyArmyPosture(budget);
            int freshIssued=0;
            for(const auto& entry:pendingBefore) if(!entry.second)
                if(auto* unit=live(entry.first))
                    if(unit->wasForced() && unit->getDestination()!=unit->getLocation()) ++freshIssued;
            require(freshIssued<=2,"A congested recall added more than two fresh requests");
            require(budgetPerPass-budget<=budgetPerPass,
                "A congested recall exceeded its bounded budget");
            SDL_Log("ARMY_RECOVERY_BACKPRESSURE: issued=%d fresh=%d queue=%zu",
                budgetPerPass-budget,freshIssued,currentGame->getPathRequestQueueSize());
            while(currentGame->pathRequestQueue.size()>queuedBefore)
                currentGame->pathRequestQueue.pop_back();
            bot->customWave.members.clear();
            // The world list stays as the engine left it from here on: the
            // fixture no longer owns it.
            worldListRestored=true;
        }

    } catch(...) {
        if(!worldListRestored) unitList=savedUnits;
        cleanupStructures();
        throw;
    }
    if(!worldListRestored) unitList=savedUnits;
    cleanupStructures();
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=army_recovery outcome=passed");
}
