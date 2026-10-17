#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>

// Real-engine QuantBot force concentration defaults (docs/formations.md, "Concentrating force").
// Included only by run-campaign-balance.py --force-probe; never linked into the game.
//
// Drives the production QuantBot::concentrateForces(), checkAllUnits() and launchGroundHunt() on
// a Custom Hard house against real units and structures on the real map:
//   1. Defaults: the compiled and the shipped configuration focus fire, converge, bundle the
//      reserve, launch from the bundle and launch waves at 60% readiness; only Custom
//      Hard/Brutal use them; the duel override replaces them.
//   2. Focus fire: every hunting tank with a damaged enemy quad in reach shoots it (fighters
//      before buildings, fewest hit points first), the same for a reversed unit list.
//   3. Converge: a wave member that is not fighting walks (hunt-toward) to a fight of its army
//      13 tiles away; one 35 tiles away and a home reserve keep their orders.
//   4. Wave size: a ready army worth 80% of the difficulty's wave readiness attacks with the
//      defaults (60%) and waits with the override 0 (the previous 100%).
//   5. Override 0 turns focus fire and converging off.
//   6. Bundling: idle reserve tanks get at most 12 bundle orders a pass, farthest first, the
//      same units for a reversed unit list, and gather at the bundle over real cycles; the
//      bundle point keeps for 30 s; a ready wave waits for the bundle, launches after 60 s at
//      most, and launches from the bundle once 70% stand there; an attack on a building pulls
//      defenders out of the bundle; save/load mid-gather and while the gate waits restores the
//      bundle exactly and re-saves the same bytes; a 9862 save has no bundle.
void runForceConcentrationProbe(Game& game) {
    auto require=[](bool ok,const char* why){
        if(!ok){SDL_Log("FORCE_PROBE_FAIL: %s",why);throw std::runtime_error(why);}
    };
    auto* owner=game.getHouse(HOUSE_HARKONNEN);
    auto* enemy=game.getHouse(HOUSE_ATREIDES);
    require(owner && enemy,"Force fixture missing houses");
    QuantBot* bot=nullptr;
    for(const auto& player:owner->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get()))bot=candidate;
    require(bot,"Force fixture missing bot");

    game.gameType=GameType::CustomGame;
    bot->gameMode=QuantBot::GameMode::Custom;
    bot->supportMode=false;
    bot->difficulty=QuantBot::Difficulty::Hard;
    bot->manualUnitOrders.clear();
    bot->defenceAssignments.clear();
    bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
    bot->forceVariantOverride=QuantBot::kNoForceOverride;
    const bool savedRecovery=getQuantBotConfig().recovery.enabled;
    const int savedLimit=bot->militaryValueLimit;

    // ---- 1. Defaults -----------------------------------------------------------------------
    const QuantBotConfig defaults;
    require(defaults.force.focusFire && defaults.force.converge && defaults.force.waveReadinessPercent==60
        && defaults.force.bundle && defaults.force.launchFromBundle && defaults.force.perimeterHold
        && defaults.force.perimeterTurrets && defaults.force.sizedDefence,
        "Compiled force defaults are not focus fire + converge + 60% waves + bundle + launch from it"
        " + perimeter hold + perimeter turrets + sized defence");
    const auto& loaded=getQuantBotConfig();
    require(loaded.force.focusFire && loaded.force.converge && loaded.force.waveReadinessPercent==60
        && loaded.force.bundle && loaded.force.launchFromBundle && loaded.force.perimeterHold
        && loaded.force.perimeterTurrets && loaded.force.sizedDefence,
        "The shipped QuantBot configuration did not load the force defaults");
    require(bot->forceBits()==(QuantBot::kForceFocusFire|QuantBot::kForceConverge|QuantBot::kForceBundle
            |QuantBot::kForceBundleGate|QuantBot::kForcePerimeterHold|QuantBot::kForceThreatTurrets
            |QuantBot::kForceSizedDefence) && bot->waveReadinessPercent()==60,
        "Custom Hard does not use the configured force rules");
    bot->difficulty=QuantBot::Difficulty::Medium;
    require(!bot->forceVariant(QuantBot::kForceFocusFire) && bot->waveReadinessPercent()==100,
        "Custom Medium uses the Hard/Brutal force rules");
    bot->difficulty=QuantBot::Difficulty::Brutal;
    require(bot->forceVariant(QuantBot::kForceConverge) && bot->waveReadinessPercent()==60,
        "Custom Brutal does not use the configured force rules");
    bot->difficulty=QuantBot::Difficulty::Hard;
    bot->forceVariantOverride=0;
    require(bot->forceBits()==0 && bot->waveReadinessPercent()==100,"The duel override 0 did not turn the rules off");
    bot->forceVariantOverride=QuantBot::kForceSmallWaves|QuantBot::kForceFocusFire;
    require(bot->forceBits()==(QuantBot::kForceSmallWaves|QuantBot::kForceFocusFire) && bot->waveReadinessPercent()==60,
        "The duel override is not exactly its bits");
    bot->forceVariantOverride=QuantBot::kNoForceOverride;

    // ---- Free ground: 60 x 24 tiles without objects or mountains, no enemy within 14 tiles ----
    constexpr int kWidth=60, kHeight=24;
    std::vector<Coord> enemyThings;
    for(const auto* structure:structureList)
        if(structure->getOwner()->getTeamID()!=owner->getTeamID()) enemyThings.push_back(structure->getLocation());
    for(const auto* unit:unitList)
        if(unit->getOwner()->getTeamID()!=owner->getTeamID() && unit->getLocation().isValid())
            enemyThings.push_back(unit->getLocation());
    // The first such area \a width tiles wide, at least 14 tiles from the 60 x 24 area at \a avoid
    // (the earlier fixture, whose dead enemy units stay attackable until the engine collects
    // them) when that is valid.
    auto clearArea=[&](Coord avoid,int width) {
        Coord found=Coord::Invalid();
        for(int y=2;y+kHeight<currentGameMap->getSizeY() && found.isInvalid();++y)
            for(int x=2;x+width<currentGameMap->getSizeX() && found.isInvalid();++x) {
                if(avoid.isValid()) {
                    const int dx=std::max({x-(avoid.x+kWidth-1),0,avoid.x-(x+width-1)});
                    const int dy=std::max({y-(avoid.y+kHeight-1),0,avoid.y-(y+kHeight-1)});
                    if(std::max(dx,dy)<14) continue;
                }
                bool clear=true;
                for(const Coord& at:enemyThings) {
                    const int dx=std::max({x-at.x,0,at.x-(x+width-1)}), dy=std::max({y-at.y,0,at.y-(y+kHeight-1)});
                    if(std::max(dx,dy)<14) {clear=false;break;}
                }
                for(int dy=0;dy<kHeight && clear;++dy)for(int dx=0;dx<width && clear;++dx) {
                    const auto* tile=currentGameMap->getTile(x+dx,y+dy);
                    if(tile->hasAGroundObject() || tile->isMountain())clear=false;
                }
                if(clear)found=Coord(x,y);
            }
        return found;
    };
    Coord origin=clearArea(Coord::Invalid(),kWidth);
    require(origin.isValid(),"Force fixture needs a clear 60x24 area away from the enemy");

    std::vector<UnitBase*> roster;
    std::map<const UnitBase*,Uint32> rosterIds;
    std::vector<Uint32> fixtureStructureIds;
    const auto savedUnits=unitList;
    auto alive=[&](const UnitBase* unit){
        return currentGame->getObjectManager().getObject(rosterIds.at(unit))==unit && unit->getHealth()>0;
    };
    auto publish=[&](bool reversed=false) {
        roster.erase(std::remove_if(roster.begin(),roster.end(),[&](UnitBase* unit) {
            return currentGame->getObjectManager().getObject(rosterIds.at(unit))!=unit;
        }),roster.end());
        unitList.clear();
        if(reversed) for(auto it=roster.rbegin();it!=roster.rend();++it) unitList.push_back(*it);
        else for(auto* unit:roster) unitList.push_back(unit);
    };
    auto spawn=[&](House* house,Uint32 item,Coord offset) {
        auto* unit=house->createUnit(item);
        require(unit,"Force fixture unit creation failed");
        unit->deploy(origin+offset);
        unit->setVisible(owner->getTeamID(),true);
        unit->setVisible(enemy->getTeamID(),true);
        roster.push_back(unit);
        rosterIds[unit]=unit->getObjectID();
        return unit;
    };
    auto retireAll=[&] {
        for(auto* unit:roster) if(alive(unit)) unit->setHealth(0);
        game.processObjects();++game.gameCycleCount;
        roster.clear();
        publish();
        bot->customWave.members.clear();
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
    auto hunt=[&](UnitBase* unit) {
        // A plain move to where it stands ends any hunt-toward walk; then engine Hunt, unforced.
        bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);
        bot->doSetAttackMode(unit,HUNT);
        unit->setForced(false);
    };

    try {
        getQuantBotConfig().recovery.enabled=false;
        require(place(enemy,Structure_ConstructionYard,Coord(40,1)),"Fixture enemy yard placement failed");

        // ---- 2. Focus fire -------------------------------------------------------------------
        {
            std::vector<UnitBase*> tanks;
            for(const Coord at:{Coord(10,10),Coord(10,11),Coord(11,10),Coord(11,11),Coord(10,12),Coord(12,10)})
                tanks.push_back(spawn(owner,Unit_Tank,at));
            auto* healthy=spawn(enemy,Unit_Quad,Coord(13,12));
            auto* damaged=spawn(enemy,Unit_Quad,Coord(13,11));
            damaged->setHealth(damaged->getMaxHealth()*3/10);
            auto targets=[&](bool reversed) {
                publish(reversed);
                for(auto* tank:tanks) { hunt(tank); tank->setTarget(nullptr); }
                bot->forceStats=QuantBot::ForceStats();
                bot->concentrateForces();
                std::vector<Uint32> chosen;
                for(auto* tank:tanks) chosen.push_back(tank->getTarget() ? tank->getTarget()->getObjectID() : NONE_ID);
                return chosen;
            };
            const auto first=targets(false);
            int inReach=0;
            for(size_t i=0;i<tanks.size();++i) {
                if(!tanks[i]->isInWeaponRange(damaged)) continue;
                ++inReach;
                require(first[i]==damaged->getObjectID(),"A hunting tank in reach did not focus the damaged fighter");
            }
            require(inReach>=4,"Focus fixture: too few tanks in reach of the damaged quad");
            require(bot->forceStats.focusSwitches>=inReach,"Focus fire switches were not counted");
            require(targets(true)==first,"Focus fire depends on the order of the unit list");
            // Override 0 (the previous defaults): nobody is given a target.
            bot->forceVariantOverride=0;
            const auto off=targets(false);
            bot->forceVariantOverride=QuantBot::kNoForceOverride;
            for(const Uint32 id:off) require(id==NONE_ID,"Override 0 still focused fire");
            SDL_Log("FORCE_PROBE: focus in_reach=%d target=%u healthy=%u",inReach,damaged->getObjectID(),healthy->getObjectID());
            retireAll();
        }

        // ---- 3. Converge ---------------------------------------------------------------------
        {
            auto* fighter=spawn(owner,Unit_Tank,Coord(20,4));
            auto* quad=spawn(enemy,Unit_Quad,Coord(22,4));
            auto* near=spawn(owner,Unit_Tank,Coord(20,17));
            auto* far=spawn(owner,Unit_Tank,Coord(57,22));
            auto* reserve=spawn(owner,Unit_Tank,Coord(24,17));
            auto run=[&](bool reversed) {
                publish(reversed);
                for(auto* unit:{fighter,near,far}) { hunt(unit); unit->setTarget(nullptr); }
                bot->doSetAttackMode(reserve,AREAGUARD);
                bot->doMove2Pos(reserve,reserve->getX(),reserve->getY(),false);
                bot->doAttackObject(fighter,quad,false);
                bot->customWave.members={near->getObjectID(),far->getObjectID(),fighter->getObjectID()};
                bot->forceStats=QuantBot::ForceStats();
                bot->concentrateForces();
                return std::make_tuple(near->isHuntingToward(),near->getDestination(),far->isHuntingToward(),
                    reserve->getAttackMode(),bot->forceStats.converges);
            };
            require(fighter->isInWeaponRange(quad),"Converge fixture: the fighter cannot reach its quad");
            const auto first=run(false);
            require(std::get<0>(first),"A wave member 13 tiles from a fight did not converge on it");
            require(blockDistance(std::get<1>(first),quad->getLocation()).lround()<=ForceConcentrationPolicy::kConvergeSameGoalTiles,
                "The converging member does not walk to the fight");
            require(!std::get<2>(first),"A wave member 35 tiles from the fight converged");
            require(std::get<3>(first)==AREAGUARD,"Converging re-ordered a home reserve");
            require(std::get<4>(first)==1,"Converge orders were not counted once");
            require(run(true)==first,"Converging depends on the order of the unit list");
            SDL_Log("FORCE_PROBE: converge near_dest=%d,%d fight=%d,%d",std::get<1>(first).x,std::get<1>(first).y,
                quad->getLocation().x,quad->getLocation().y);
            retireAll();
        }

        // ---- 4. Wave size --------------------------------------------------------------------
        {
            require(place(owner,Structure_RepairYard,Coord(1,1)),"Fixture repair yard placement failed");
            std::vector<UnitBase*> army;
            for(int i=0;i<12;++i) army.push_back(spawn(owner,Unit_Tank,Coord(6+2*(i%6),14+2*(i/6))));
            int value=0;
            for(auto* unit:army) value+=std::max(100,currentGame->objectData.data[unit->getItemID()][unit->getOriginalHouseID()].price);
            const float threshold=getQuantBotConfig().getSettings(static_cast<int>(QuantBot::Difficulty::Hard)).attackThresholdPercent;
            // The difficulty's readiness at 125% of the army: 60% of it is 75% of the army.
            bot->militaryValueLimit=int(value*125/(threshold*100))+1;
            auto launch=[&](int override) {
                publish();
                for(auto* unit:army) { bot->doSetAttackMode(unit,GUARD); unit->setForced(false); }
                bot->customWave.members.clear();
                bot->forceVariantOverride=override;
                // No bundle yet: the launch gate stands aside (section 6 covers it).
                bot->bundlePoint=Coord::Invalid();
                bot->bundleCentre=Coord::Invalid();
                bot->bundleGateSince=0;
                bot->launchGroundHunt();
                int hunters=0;
                for(auto* unit:army) if(unit->getAttackMode()==HUNT) ++hunters;
                return hunters;
            };
            require(launch(0)==0,"The previous 100% readiness launched an army below it");
            require(launch(QuantBot::kNoForceOverride)==int(army.size()),"The 60% default did not launch the ready army");
            bot->forceVariantOverride=QuantBot::kNoForceOverride;
            SDL_Log("FORCE_PROBE: waves value=%d limit=%d threshold=%.2f",value,bot->militaryValueLimit,threshold);
            retireAll();
        }

        // ---- 6. Bundling the reserve and launching from the bundle -----------------------------
        {
            bot->forceVariantOverride=QuantBot::kNoForceOverride;
            bot->customWave.members.clear();
            bot->defenceAssignments.clear();
            bot->defenceResponseCycles.clear();
            // Fresh ground: Area Guard tanks would otherwise go for the earlier sections' dead
            // enemy units and their yard, which stay attackable until the engine collects them.
            const Coord firstArea=origin;
            origin=clearArea(firstArea,40);
            require(origin.isValid(),"Bundle fixture needs a second clear 40x24 area");
            std::vector<UnitBase*> reserve;
            for(const int x:{2,4,6,8}) for(const int y:{8,11,14,17}) reserve.push_back(spawn(owner,Unit_Tank,Coord(x,y)));
            const Coord bundle=origin+Coord(24,14);
            auto tiles=[](Coord a,Coord b){ return blockDistance(a,b).lround(); };
            // The bundle is pinned for the fixture (its 30 s clock renewed each pass): the house's
            // real forward rally lies near its real base, far from this ground.
            auto pin=[&]{ bot->bundlePoint=bundle; bot->bundleCentre=Coord::Invalid(); bot->bundleCycle=game.getGameCycleCount(); };
            auto idle=[&]{
                for(auto* unit:reserve) {
                    if(!alive(unit)) continue;
                    bot->doSetAttackMode(unit,GUARD);
                    bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);
                    unit->setForced(false);
                    unit->setTarget(nullptr);
                }
                bot->customWave.members.clear();
            };
            auto tick=[&]{
                game.processObjects();++game.gameCycleCount;
                if(game.getGameCycleCount()%50==0) { publish(); pin(); bot->checkAllUnits(); }
            };
            auto inBundle=[&](const UnitBase* unit,int slack){
                return tiles(unit->getLocation(),bundle)<=ForceConcentrationPolicy::bundleRadius(int(reserve.size()))+slack;
            };
            // A save and load of the whole bot restores the bundle exactly and re-saves the same
            // bytes; a 9862 save carries no bundle.
            auto saveRoundTrip=[&](const char* when) {
                OMemoryStream out; out.open();
                bot->save(out);
                const std::string bytes(out.getData(),out.getDataLength());
                const Uint32 savedVersion=currentGame->loadedSavegameVersion;
                currentGame->loadedSavegameVersion=SAVEGAMEVERSION;
                IMemoryStream in(bytes.data(),bytes.size());
                QuantBot restored(in,owner);
                require(restored.bundlePoint==bot->bundlePoint && restored.bundleCentre==bot->bundleCentre
                    && restored.bundleCycle==bot->bundleCycle && restored.bundleGateSince==bot->bundleGateSince,
                    "A save and load changed the bundle or the launch gate's wait");
                OMemoryStream again; again.open();
                restored.save(again);
                require(std::string(again.getData(),again.getDataLength())==bytes,
                    "Re-saving a loaded bundle produced different bytes");
                currentGame->loadedSavegameVersion=9862;
                IMemoryStream legacyIn(bytes.data(),bytes.size());
                QuantBot legacy(legacyIn,owner);
                require(legacy.bundlePoint.isInvalid() && legacy.bundleCentre.isInvalid()
                    && legacy.bundleCycle==std::numeric_limits<Uint32>::max() && legacy.bundleGateSince==0,
                    "A 9862 save invented a bundle");
                currentGame->loadedSavegameVersion=savedVersion;
                SDL_Log("FORCE_PROBE: bundle save_load=%s point=%d,%d cycle=%u gate_since=%u bytes=%zu",when,
                    bot->bundlePoint.x,bot->bundlePoint.y,bot->bundleCycle,bot->bundleGateSince,bytes.size());
            };

            // The fixture runs at the start of the game; the clock checks below look back 30 s
            // and 60 s, so move the game clock past them (nothing is simulated in between).
            game.gameCycleCount+=MILLI2CYCLES(120000);
            // The 30 s throttle: the same point inside 30 s, a fresh choice after it.
            pin();
            require(bot->forwardBundlePoint()==bundle,"The bundle point changed inside its 30 s");
            bot->bundleCycle=game.getGameCycleCount()-MILLI2CYCLES(30000);
            bot->forwardBundlePoint();
            require(bot->bundleCycle==game.getGameCycleCount(),"The bundle point was not re-chosen after 30 s");

            // One pass: 12 orders, the farthest reserve units first, the same for either list order.
            auto onePass=[&](bool reversed) {
                idle();
                publish(reversed);
                pin();
                bot->forceStats=QuantBot::ForceStats();
                bot->checkAllUnits();
                std::set<Uint32> ordered;
                const int radius=ForceConcentrationPolicy::bundleRadius(int(reserve.size()));
                for(auto* unit:reserve) {
                    // Posted at the bundle: the guard point is the bundle, or the slot in it a
                    // walk was ordered to.
                    require(unit->getAttackMode()==AREAGUARD && tiles(unit->getGuardPoint(),bundle)<=radius,
                        "A reserve tank is not posted at the bundle");
                    if(unit->getDestination().isValid() && unit->getDestination()!=unit->getLocation()
                        && tiles(unit->getDestination(),bundle)<=radius)
                        ordered.insert(unit->getObjectID());
                }
                int pending=0, moving=0;
                for(auto* unit:reserve) { pending+=unit->hasPendingPathRequest(); moving+=unit->isMoving(); }
                SDL_Log("FORCE_PROBE: bundle pass reversed=%d orders=%d ordered=%zu point=%d,%d bundle=%d,%d queue=%d"
                    " pending=%d moving=%d",
                    int(reversed),bot->forceStats.bundleOrders,ordered.size(),bot->bundlePoint.x,bot->bundlePoint.y,
                    bundle.x,bundle.y,int(currentGame->getPathRequestQueueSize()),pending,moving);
                require(bot->forceStats.bundleOrders==ForceConcentrationPolicy::kBundleOrdersPerPass
                    && int(ordered.size())==ForceConcentrationPolicy::kBundleOrdersPerPass,
                    "One pass did not give exactly the capped number of bundle orders");
                return ordered;
            };
            const auto forward=onePass(false);
            require(onePass(true)==forward,"The bundle orders depend on the order of the unit list");
            std::vector<UnitBase*> byDistance(reserve);
            std::sort(byDistance.begin(),byDistance.end(),[&](const UnitBase* a,const UnitBase* b){
                return ForceConcentrationPolicy::bundleOrderFirst(tiles(a->getLocation(),bundle),a->getObjectID(),
                    tiles(b->getLocation(),bundle),b->getObjectID());
            });
            for(int i=0;i<ForceConcentrationPolicy::kBundleOrdersPerPass;++i)
                require(forward.count(byDistance[i]->getObjectID()),"A nearer reserve tank got a bundle order before a farther one");

            // A ready wave scattered at home waits for the bundle (re-checked in 5 s) ...
            int value=0;
            for(auto* unit:reserve) value+=std::max(100,currentGame->objectData.data[unit->getItemID()][unit->getOriginalHouseID()].price);
            const float threshold=getQuantBotConfig().getSettings(static_cast<int>(QuantBot::Difficulty::Hard)).attackThresholdPercent;
            // Readiness (60%) at 30% of the army, so the wave is ready with or without defenders.
            bot->militaryValueLimit=int(value*50/(threshold*100))+1;
            auto hunters=[&]{ int n=0; for(auto* unit:reserve) if(alive(unit) && unit->getAttackMode()==HUNT) ++n; return n; };
            idle();
            publish();
            pin();
            bot->bundleGateSince=0;
            bot->attackTimer=MILLI2CYCLES(90000);
            bot->forceStats=QuantBot::ForceStats();
            bot->launchGroundHunt();
            require(hunters()==0 && bot->forceStats.gateHolds==1 && bot->bundleGateSince!=0,
                "A ready wave outside the bundle did not wait for it");
            require(bot->attackTimer<=Sint32(MILLI2CYCLES(ForceConcentrationPolicy::kBundleRecheckMs)),
                "The launch gate did not re-check within 5 s");
            saveRoundTrip("gate_waiting");
            // ... at most 60 s.
            bot->bundleGateSince=std::max<Uint32>(1,game.getGameCycleCount()-MILLI2CYCLES(ForceConcentrationPolicy::kBundleGateMaxMs));
            bot->launchGroundHunt();
            require(hunters()==int(reserve.size()) && bot->forceStats.gateTimeouts==1 && bot->bundleGateSince==0,
                "A wave held 60 s for the bundle did not launch");

            // Real cycles: the reserve gathers at the bundle.
            idle();
            publish();
            bool savedMid=false;
            int gathered=0, cycles=0;
            const int limit=MILLI2CYCLES(120000);
            for(;cycles<limit;++cycles) {
                tick();
                gathered=0;
                for(auto* unit:reserve) if(inBundle(unit,0)) ++gathered;
                if(!savedMid && gathered>=4) { saveRoundTrip("mid_gather"); savedMid=true; }
                if(gathered==int(reserve.size())) break;
            }
            SDL_Log("FORCE_PROBE: bundle gathered=%d/%zu seconds=%d orders=%d",gathered,reserve.size(),
                cycles/MILLI2CYCLES(1000),bot->forceStats.bundleOrders);
            require(savedMid,"The bundle save/load mid-gather never ran");
            require(gathered*10>=int(reserve.size())*9,"The idle reserve did not gather at the bundle");
            for(auto* unit:reserve)
                require(unit->getAttackMode()==AREAGUARD
                    && tiles(unit->getGuardPoint(),bundle)<=ForceConcentrationPolicy::bundleRadius(int(reserve.size())),
                    "A bundled tank is not on Area Guard at the bundle");

            // An attack on a building pulls defenders out of the bundle; the rest stay bundled.
            auto* post=place(owner,Structure_WindTrap,Coord(29,19));
            require(post,"Fixture wind trap placement failed");
            auto* raider=spawn(enemy,Unit_Tank,Coord(33,20));
            publish();
            raider->doAttackObject(post,true);
            bot->defenceResponseCycles.clear();
            bot->onDamage(post,40,raider->getObjectID());
            std::vector<UnitBase*> defenders, bundled;
            for(auto* unit:reserve) (bot->defenceAssignments.count(unit->getObjectID()) ? defenders : bundled).push_back(unit);
            SDL_Log("FORCE_PROBE: bundle defence defenders=%zu bundled=%zu",defenders.size(),bundled.size());
            require(!defenders.empty(),"An attack on a building did not pull defenders out of the bundle");
            for(auto* unit:defenders) require(unit->getAttackMode()==HUNT,"A defender from the bundle is not hunting the attack");
            publish();
            pin();
            bot->checkAllUnits();
            for(auto* unit:defenders)
                require(unit->getAttackMode()==HUNT && bot->defenceAssignments.count(unit->getObjectID()),
                    "Bundling took a defender back to the bundle");
            raider->setHealth(0);
            publish();

            // A ready wave in the bundle launches from it: every bundled tank hunts.
            bot->customWave.members.clear();
            bot->bundleGateSince=0;
            bot->forceStats=QuantBot::ForceStats();
            std::vector<UnitBase*> launchedFrom;
            for(auto* unit:bundled) if(inBundle(unit,2)) launchedFrom.push_back(unit);
            require(launchedFrom.size()*10>=bundled.size()*7,"Too few tanks in the bundle to test the launch from it");
            bot->launchGroundHunt();
            int launched=0;
            for(auto* unit:bundled) if(unit->getAttackMode()==HUNT) ++launched;
            SDL_Log("FORCE_PROBE: bundle launch launched=%d bundled=%zu holds=%d",launched,bundled.size(),
                bot->forceStats.gateHolds);
            require(bot->forceStats.gateHolds==0 && launched==int(bundled.size()) && bot->bundleGateSince==0,
                "A ready wave standing in the bundle did not launch from it");
            bot->defenceAssignments.clear();
            retireAll();
        }

        // ---- 7. Round 5: the remembered enemy army, the perimeter hold and turrets, sized defence --
        {
            namespace Force=ForceConcentrationPolicy;
            bot->forceVariantOverride=QuantBot::kNoForceOverride;
            bot->customWave.members.clear();
            bot->defenceAssignments.clear();
            bot->defenceResponseCycles.clear();
            bot->enemySightings.clear();
            bot->musters.clear();
            const Coord bundleArea=origin;
            origin=clearArea(bundleArea,40);
            require(origin.isValid(),"Round 5 fixture needs a third clear 40x24 area");
            auto step=[&]{ game.processObjects(); ++game.gameCycleCount; publish(); };
            std::vector<UnitBase*> ours, theirs;
            for(const int y:{6,9}) ours.push_back(spawn(owner,Unit_Tank,Coord(3,y)));
            for(int i=0;i<10;++i) theirs.push_back(spawn(enemy,Unit_Tank,Coord(28+2*(i%5),4+3*(i/5))));
            publish();

            // The memory: everything seen is remembered, also once out of sight; the dead are
            // forgotten at once and the unseen after kSightingMemoryMs.
            bot->updateEnemySightings();
            require(bot->enemySightings.size()==theirs.size(),"The visible enemy army was not remembered");
            theirs[0]->setVisible(owner->getTeamID(),false);
            bot->updateEnemySightings();
            require(bot->enemySightings.count(theirs[0]->getObjectID()),"An enemy out of sight was forgotten at once");
            theirs[1]->setHealth(0);
            step();
            bot->updateEnemySightings();
            require(bot->enemySightings.size()==theirs.size()-1 && !bot->enemySightings.count(rosterIds.at(theirs[1])),
                "A dead enemy was still remembered");
            game.gameCycleCount+=MILLI2CYCLES(Force::kSightingMemoryMs)+1;
            bot->updateEnemySightings();
            require(!bot->enemySightings.count(theirs[0]->getObjectID()) && bot->enemySightings.size()==theirs.size()-2,
                "An enemy unseen for the whole memory was still remembered");
            theirs[0]->setVisible(owner->getTeamID(),true);
            bot->updateEnemySightings();
            SDL_Log("FORCE_PROBE: memory remembered=%zu estimate=%lld own=%lld",bot->enemySightings.size(),
                (long long)bot->enemyArmyEstimate(),(long long)bot->ownGroundArmyValue());

            // Outnumbered (9 tanks against 2): garrisoned, perimeter turrets wanted.
            ++game.gameCycleCount;
            require(bot->outnumberedByEstimate() && bot->garrisoned(),"Nine remembered tanks against two did not outnumber us");
            const int goal=bot->perimeterTurretGoal();
            require(goal>0 && goal==Force::perimeterTurretGoal(bot->ownGroundArmyValue(),bot->enemyArmyEstimate()),
                "The perimeter turret goal does not follow the enemy's lead");

            // The perimeter: towards the observed enemy base, beyond the outermost own building.
            const auto savedBases=bot->visibleEnemyBases;
            bot->visibleEnemyBases={bot->findBaseCentre(enemy->getHouseID())};
            ++game.gameCycleCount;
            const QuantBot::Perimeter perimeter=bot->perimeter();
            require(perimeter.length>0 && perimeter.point.isValid(),"No perimeter towards the observed enemy base");
            auto along=[&](Coord at){ return int((int64_t(at.x-perimeter.anchor.x)*perimeter.forward.x
                + int64_t(at.y-perimeter.anchor.y)*perimeter.forward.y)/perimeter.length); };
            auto across=[&](Coord at){ return int((int64_t(at.y-perimeter.anchor.y)*perimeter.forward.x
                - int64_t(at.x-perimeter.anchor.x)*perimeter.forward.y)/perimeter.length); };
            for(const auto* structure:structureList)
                if(structure->getOwner()==owner && structure->getHealth()>0
                    && blockDistance(structure->getLocation(),perimeter.anchor).lround()<=Force::kPerimeterBaseTiles)
                    require(along(structure->getLocation())<=perimeter.depth,"A building stands beyond the perimeter depth");
            require(along(perimeter.point)>=perimeter.depth-Force::kPerimeterInnerTiles
                && !currentGameMap->getTile(perimeter.point)->hasAStructure(),
                "The perimeter point is not on the enemy-facing edge of the base");
            // The garrison holds the perimeter (the house anchor, with the posture machine on);
            // in the city simulation it holds the protected rally (the edge is where lots go).
            getQuantBotConfig().recovery.enabled=true;
            bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
            bot->localSquadWithdraw=false;
            const bool savedCitySim=game.citySimEnabled_;
            game.citySimEnabled_=false;
            const Coord vanillaAnchor=bot->armyAssemblyAnchor();
            game.citySimEnabled_=true;
            const Coord cityAnchor=bot->armyAssemblyAnchor();
            game.citySimEnabled_=savedCitySim;
            require(vanillaAnchor==perimeter.point && bot->reserveHeldHome(),
                "The outnumbered reserve does not hold the perimeter");
            require(cityAnchor!=perimeter.point && (bot->protectedRally.isInvalid() || cityAnchor==bot->protectedRally),
                "The outnumbered reserve holds the perimeter in the city simulation");
            getQuantBotConfig().recovery.enabled=false;
            // A perimeter turret site: on the band and legal, and none when the band has no
            // legal ground at all (rock that is free and in build range).
            int legalBand=0;
            for(int y=perimeter.anchor.y-40;y<=perimeter.anchor.y+40;++y)
                for(int x=perimeter.anchor.x-40;x<=perimeter.anchor.x+40;++x)
                    if(currentGameMap->tileExists(x,y)
                        && Force::onPerimeterBand(along(Coord(x,y)),across(Coord(x,y)),perimeter.depth)
                        && currentGameMap->okayToPlaceStructure(x,y,1,1,false,owner,false,Structure_RocketTurret)) ++legalBand;
            const Coord site=bot->findPerimeterTurretLocation();
            SDL_Log("FORCE_PROBE: perimeter anchor=%d,%d forward=%d,%d depth=%d point=%d,%d site=%d,%d legal_band=%d goal=%d",
                perimeter.anchor.x,perimeter.anchor.y,perimeter.forward.x,perimeter.forward.y,perimeter.depth,
                perimeter.point.x,perimeter.point.y,site.x,site.y,legalBand,goal);
            if(site.isValid())
                require(Force::onPerimeterBand(along(site),across(site),perimeter.depth)
                    && currentGameMap->okayToPlaceStructure(site.x,site.y,1,1,false,owner,false,Structure_RocketTurret),
                    "The perimeter turret site is off the band or not legal");
            else require(legalBand==0,"No perimeter turret site although the band has legal ground");
            // No wave leaves while outnumbered.
            if(owner->getNumItems(Structure_RepairYard)==0)
                require(place(owner,Structure_RepairYard,Coord(10,16)),"Fixture repair yard placement failed");
            bot->militaryValueLimit=1000;
            bot->forceStats=QuantBot::ForceStats();
            bot->attack(1000000);
            for(auto* unit:ours) require(unit->getAttackMode()!=HUNT,"A wave left while outnumbered");
            require(bot->forceStats.garrisonHolds==1,"The outnumbered attack check was not held by the garrison");

            // Save/load carries the memory and the musters byte for byte; 9862 has neither.
            QuantBot::Muster muster;
            muster.building=theirs[2]->getObjectID();
            muster.point=origin+Coord(12,8);
            muster.since=game.getGameCycleCount();
            muster.members={ours[0]->getObjectID()};
            bot->musters.push_back(muster);
            {
                OMemoryStream out; out.open();
                bot->save(out);
                const std::string bytes(out.getData(),out.getDataLength());
                const Uint32 savedVersion=currentGame->loadedSavegameVersion;
                currentGame->loadedSavegameVersion=SAVEGAMEVERSION;
                IMemoryStream in(bytes.data(),bytes.size());
                QuantBot restored(in,owner);
                bool same=restored.enemySightings.size()==bot->enemySightings.size() && restored.musters.size()==1
                    && restored.musters[0].building==muster.building && restored.musters[0].point==muster.point
                    && restored.musters[0].since==muster.since && restored.musters[0].members==muster.members;
                for(const auto& [id,sighting]:bot->enemySightings) {
                    const auto found=restored.enemySightings.find(id);
                    same=same && found!=restored.enemySightings.end() && found->second.value==sighting.value
                        && found->second.cycle==sighting.cycle;
                }
                require(same,"A save and load changed the remembered enemy army or the musters");
                OMemoryStream again; again.open();
                restored.save(again);
                require(std::string(again.getData(),again.getDataLength())==bytes,
                    "Re-saving the loaded memory and musters produced different bytes");
                currentGame->loadedSavegameVersion=9862;
                IMemoryStream legacyIn(bytes.data(),bytes.size());
                QuantBot legacy(legacyIn,owner);
                require(legacy.enemySightings.empty() && legacy.musters.empty(),"A 9862 save invented an enemy memory");
                currentGame->loadedSavegameVersion=savedVersion;
                SDL_Log("FORCE_PROBE: memory save_load sightings=%zu musters=1 bytes=%zu",bot->enemySightings.size(),bytes.size());
            }
            bot->musters.clear();

            // A worker raid the troops in reach cannot match gets nobody; one they can gets 1.5x.
            auto* harvester=spawn(owner,Unit_Harvester,Coord(8,18));
            for(int i=2;i<10;++i) { theirs[i]->setHealth(0); }
            step();
            std::vector<UnitBase*> raiders;
            for(int i=0;i<4;++i) raiders.push_back(spawn(enemy,Unit_Tank,Coord(12+i,20)));
            publish();
            for(auto* raider:raiders) raider->doAttackObject(harvester,true);
            bot->forceStats=QuantBot::ForceStats();
            bot->defenceResponseCycles.clear();
            bot->scrambleUnitsAndDefend(raiders[0],false,harvester);
            for(auto* unit:ours) require(!bot->defenceAssignments.count(unit->getObjectID()),"A lost cause was answered piecemeal");
            require(bot->forceStats.declinedResponses==1,"A worker raid out of reach of a match was not declined");
            for(int i=1;i<4;++i) raiders[i]->setHealth(0);
            step();
            bot->defenceResponseCycles.clear();
            bot->scrambleUnitsAndDefend(raiders[0],false,harvester);
            int answered=0;
            for(auto* unit:ours) answered+=bot->defenceAssignments.count(unit->getObjectID());
            SDL_Log("FORCE_PROBE: sized worker declined=%d answered=%d",bot->forceStats.declinedResponses,answered);
            require(answered==2 && bot->forceStats.sizedResponses==2,
                "A worker raid the troops can match did not get 1.5x the threat");
            bot->visibleEnemyBases=savedBases;
            bot->defenceAssignments.clear();
            bot->enemySightings.clear();
            retireAll();
        }
    } catch(...) {
        getQuantBotConfig().recovery.enabled=savedRecovery;
        bot->forceVariantOverride=QuantBot::kNoForceOverride;
        bot->militaryValueLimit=savedLimit;
        cleanupStructures(); unitList=savedUnits; throw;
    }
    getQuantBotConfig().recovery.enabled=savedRecovery;
    bot->militaryValueLimit=savedLimit;
    cleanupStructures();
    unitList=savedUnits;
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=force_concentration outcome=passed");
}
