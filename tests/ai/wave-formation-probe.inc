#include <FormationOrders.h>
#include <FormationPlanner.h>

// Real-engine QuantBot attack waves in formation (docs/formations.md, QuantBot waves).
// Included only by run-campaign-balance.py --wave-formation-probe; never linked into the game.
//
// Drives the production QuantBot::launchGroundHunt() on a Custom Hard house against real
// units, structures and the real map:
//   1. For every kept WaveFormation shape (Box, Pincer, Column, Staggered, Echelon), a mixed wave (tanks, sonic tanks, siege tanks,
//      launchers, quads, troopers) launches as one hunting formation in exactly the configured
//      shape towards the wave's own objective; the harvester and the MCV stay out; no slot is
//      in a sonic lane; the wave marches and, on meeting the enemy, every member breaks into
//      a fight and stays in Hunt.
//   2. With WaveFormation None the same wave gets today's orders: Hunt, no formation, no
//      hunt-toward walk, its destination and force flag untouched.
//   3. A wave smaller than FormationOrders::kMinAIGroup keeps today's orders as well.
void runWaveFormationProbe(Game& game) {
    auto require=[](bool ok,const char* why){
        if(!ok){SDL_Log("WAVE_FORMATION_FAIL: %s",why);throw std::runtime_error(why);}
    };
    auto* owner=game.getHouse(HOUSE_HARKONNEN);
    auto* enemy=game.getHouse(HOUSE_ATREIDES);
    require(owner && enemy,"Wave-formation fixture missing houses");
    QuantBot* bot=nullptr;
    for(const auto& player:owner->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get()))bot=candidate;
    require(bot,"Wave-formation fixture missing bot");

    game.gameType=GameType::CustomGame;
    bot->gameMode=QuantBot::GameMode::Custom;
    bot->role=AIRole::Both;
    bot->difficulty=QuantBot::Difficulty::Hard;
    bot->manualUnitOrders.clear();
    bot->defenceAssignments.clear();
    bot->armyPosture=ArmyPosturePolicy::Posture::Offensive;
    const int savedShape=getQuantBotConfig().waveFormation;

    // ---- Free ground: 60 x 24 tiles of sand and rock without objects or mountains, and no
    //      enemy building or unit within 14 tiles of it (the march must meet only the fixture) ----
    std::vector<Coord> enemyBuildings;
    for(const auto* structure:structureList)
        if(structure->getOwner()->getTeamID()!=owner->getTeamID()) enemyBuildings.push_back(structure->getLocation());
    for(const auto* unit:unitList)
        if(unit->getOwner()->getTeamID()!=owner->getTeamID() && unit->getLocation().isValid())
            enemyBuildings.push_back(unit->getLocation());
    Coord origin=Coord::Invalid();
    for(int y=2;y+24<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for(int x=2;x+60<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear=true;
            for(const Coord& at:enemyBuildings) {
                const int dx=std::max({x-at.x,0,at.x-(x+59)}), dy=std::max({y-at.y,0,at.y-(y+23)});
                if(std::max(dx,dy)<14) {clear=false;break;}
            }
            for(int dy=0;dy<24 && clear;++dy)for(int dx=0;dx<60 && clear;++dx) {
                const auto* tile=currentGameMap->getTile(x+dx,y+dy);
                if(tile->hasAGroundObject() || tile->isMountain())clear=false;
            }
            if(clear)origin=Coord(x,y);
        }
    require(origin.isValid(),"Wave-formation fixture needs a clear 60x24 area away from enemy buildings");

    std::vector<UnitBase*> roster;
    std::map<const UnitBase*,Uint32> rosterIds;
    std::vector<Uint32> fixtureStructureIds;
    const auto savedUnits=unitList;
    auto publish=[&] {
        roster.erase(std::remove_if(roster.begin(),roster.end(),[&](UnitBase* unit) {
            return currentGame->getObjectManager().getObject(rosterIds.at(unit))!=unit;
        }),roster.end());
        unitList.clear();
        for(auto* unit:roster) unitList.push_back(unit);
    };
    auto spawn=[&](House* house,Uint32 item,Coord offset) {
        auto* unit=house->createUnit(item);
        require(unit,"Wave-formation fixture unit creation failed");
        unit->deploy(origin+offset);
        unit->setVisible(owner->getTeamID(),true);
        unit->setVisible(enemy->getTeamID(),true);
        roster.push_back(unit);
        rosterIds[unit]=unit->getObjectID();
        return unit;
    };
    auto retireAll=[&] {
        for(auto* unit:roster)
            if(currentGame->getObjectManager().getObject(rosterIds.at(unit))==unit) unit->setHealth(0);
        game.processObjects();++game.gameCycleCount;
        roster.clear();
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
    auto tick=[&]{ game.processObjects();++game.gameCycleCount; };

    try {
        require(place(owner,Structure_ConstructionYard,Coord(2,2)),"Fixture yard placement failed");
        place(owner,Structure_RepairYard,Coord(2,6));
        // An enemy base 54 tiles east of the wave; the map's own Atreides bases are further
        // candidates for the wave's objective.
        require(place(enemy,Structure_ConstructionYard,Coord(56,10)),"Fixture enemy yard placement failed");

        // The mixed wave, scattered over the home ground, plus the two units that never join.
        struct Pick { Uint32 item; Coord at; };
        const std::vector<Pick> picks{
            {Unit_Tank,{8,4}},{Unit_Tank,{9,9}},{Unit_Tank,{7,14}},{Unit_Tank,{10,18}},
            {Unit_SonicTank,{6,8}},{Unit_SonicTank,{11,12}},
            {Unit_SiegeTank,{5,11}},{Unit_SiegeTank,{12,6}},
            {Unit_Launcher,{4,15}},{Unit_Launcher,{9,16}},
            {Unit_Quad,{13,10}},{Unit_Quad,{12,15}},
            {Unit_Trooper,{6,17}},{Unit_Trooper,{7,19}},{Unit_Trooper,{8,20}},
        };
        struct Wave { std::vector<UnitBase*> fighters; UnitBase* harvester=nullptr; UnitBase* mcv=nullptr; };
        auto muster=[&](size_t count) {
            Wave wave;
            for(size_t i=0;i<count && i<picks.size();++i) wave.fighters.push_back(spawn(owner,picks[i].item,picks[i].at));
            wave.harvester=spawn(owner,Unit_Harvester,Coord(10,13));
            wave.mcv=spawn(owner,Unit_MCV,Coord(5,6));
            publish();
            for(auto* unit:wave.fighters) {
                bot->doSetAttackMode(unit,GUARD);
                bot->doMove2Pos(unit,unit->getX(),unit->getY(),false);
                unit->setForced(false);
            }
            bot->customWave.members.clear();
            bot->customWave.front=NONE_ID;
            bot->militaryValueLimit=1;
            getQuantBotConfig().recovery.enabled=false;
            return wave;
        };
        auto centreOf=[](const std::vector<UnitBase*>& units) {
            int x=0,y=0;
            for(const auto* unit:units){x+=unit->getLocation().x;y+=unit->getLocation().y;}
            return Coord(x/int(units.size()),y/int(units.size()));
        };

        // ---- 1. Formation waves in every measured shape -------------------------------
        const std::vector<std::pair<FormationPlanner::Shape,const char*>> shapes{
            {FormationPlanner::Shape::Box,"Box"},{FormationPlanner::Shape::Pincer,"Pincer"},
            {FormationPlanner::Shape::Column,"Column"},{FormationPlanner::Shape::Staggered,"Staggered"},
            {FormationPlanner::Shape::Echelon,"Echelon"}};
        for(const auto& [shape,name] : shapes) {
            getQuantBotConfig().waveFormation=static_cast<int>(shape);
            Wave wave=muster(picks.size());
            // What the configured shape plans for these units towards the wave's objective: the
            // observed enemy base the wave anchors on (QuantBot::customWaveObjective of its lead).
            const std::vector<const UnitBase*> members(wave.fighters.begin(),wave.fighters.end());
            const ObjectBase* target=bot->customWaveObjective(wave.fighters.front());
            require(target,"The fixture wave has no objective");
            const Coord goal=target->getClosestPoint(centreOf(wave.fighters));
            const auto expected=FormationOrders::planGroup(members,goal,shape);
            require(expected.formation,"The fixture wave does not plan as a formation");

            bot->launchGroundHunt();
            require(bot->customWave.front==target->getObjectID(),"The wave lost its objective");
            const Uint32 group=wave.fighters.front()->getFormationGroup();
            require(group!=0,"The wave did not launch as a formation");
            for(auto* unit:wave.fighters) {
                require(unit->getFormationGroup()==group && unit->isFormationHunt() && unit->isFormationHuntToward(),
                    "A wave member is not in the wave's hunting formation");
                require(unit->getFormationSlot()==expected.slotFor(unit->getObjectID()),
                    "A wave member's slot is not the configured shape's slot");
                require(unit->getAttackMode()==HUNT && unit->isHuntingToward() && !unit->wasForced(),
                    "A wave member is not hunting its way to its slot");
                require(bot->customWave.members.count(unit->getObjectID()),"A wave member is not tracked");
            }
            require(!wave.harvester->isInFormation() && wave.harvester->getAttackMode()!=HUNT,
                "The harvester joined the attack wave");
            require(!wave.mcv->isInFormation() && wave.mcv->getAttackMode()!=HUNT,"The MCV joined the attack wave");
            // The sonic-lane rule on the planned slots.
            for(const auto* sonic:wave.fighters) {
                if(!FormationPlanner::isSonic(sonic->getItemID())) continue;
                for(const auto* other:wave.fighters)
                    if(other!=sonic && !FormationPlanner::mayStandInLane(sonic->getItemID(),other->getItemID()))
                        require(!FormationPlanner::inSonicLane(sonic->getFormationSlot(),other->getFormationSlot(),
                                                               expected.forward),
                            "A wave member's slot is in a sonic tank's lane");
            }

            // The march: form up and travel together until the first contact.
            const Coord home=centreOf(wave.fighters);
            int formingCycles=0, marchingCycles=0, laneSteps=0;
            // Pairs (sonic member in its place, other member) standing in that sonic lane, as of
            // the previous cycle: a pair that forms on the march is a step into a lane. A pair
            // already sharing a lane does not stop either unit, and a sonic tank still
            // recovering its front-row place has no lane yet (FormationOrders::stepEntersSonicLane).
            std::set<std::pair<Uint32,Uint32>> lanePairs;
            std::set<Uint32> sonicsInPlace;
            for(int i=0;i<MILLI2CYCLES(90000);++i) {
                tick();
                int forming=0, marching=0;
                for(auto* unit:wave.fighters) {
                    if(unit->isFormationForming()) ++forming;
                    else if(unit->isInFormation()) ++marching;
                }
                if(forming) ++formingCycles;
                if(marching) ++marchingCycles;
                const Coord forward=FormationPlanner::facingFromCode(wave.fighters.front()->getFormationFacing());
                std::set<std::pair<Uint32,Uint32>> pairs;
                std::set<Uint32> inPlace;
                if(!forming && marching && forward!=Coord(0,0))
                    for(const auto* sonic:wave.fighters) {
                        if(!sonic->isInFormation() || !FormationPlanner::isSonic(sonic->getItemID())
                           || sonic->getFormationMarch()==FormationPlanner::March::Recover) continue;
                        inPlace.insert(sonic->getObjectID());
                        for(const auto* other:wave.fighters)
                            if(other!=sonic && other->isInFormation()
                               && !FormationPlanner::mayStandInLane(sonic->getItemID(),other->getItemID())
                               && FormationPlanner::inSonicLane(sonic->getLocation(),other->getLocation(),forward))
                                pairs.insert({sonic->getObjectID(),other->getObjectID()});
                    }
                for(const auto& pair:pairs)
                    if(!lanePairs.count(pair) && sonicsInPlace.count(pair.first)) ++laneSteps;
                lanePairs=pairs;
                sonicsInPlace=inPlace;
                for(auto* unit:wave.fighters)
                    require(unit->getAttackMode()==HUNT,"A marching wave member left Hunt");
                if(marching && blockDistance(home,centreOf(wave.fighters)).lround()>=12) break;
            }
            SDL_Log("WAVE_FORMATION_PROBE: shape=%s forming_s=%.1f marching_s=%.1f travelled=%d",
                name,formingCycles*GAMESPEED_DEFAULT/1000.0,marchingCycles*GAMESPEED_DEFAULT/1000.0,
                int(blockDistance(home,centreOf(wave.fighters)).lround()));
            require(marchingCycles>0,"The wave never marched in formation");
            require(blockDistance(home,centreOf(wave.fighters)).lround()>=12,"The formation wave did not travel");
            for(auto* unit:wave.fighters) require(unit->isInFormation(),"A member left the formation before any contact");

            // Contact: three enemy quads 14 tiles ahead of the formation, on its way.
            const Coord facing=FormationPlanner::facingFromCode(wave.fighters.front()->getFormationFacing());
            const Coord ahead=centreOf(wave.fighters)+Coord(facing.x*14,facing.y*14);
            std::vector<UnitBase*> raiders;
            for(int i=0;i<3;++i) raiders.push_back(spawn(enemy,Unit_Quad,ahead-origin+Coord(facing.y*(i-1)*2,-facing.x*(i-1)*2)));
            publish();
            for(auto* raider:raiders) {raider->doSetAttackMode(GUARD);raider->doMove2Pos(raider->getX(),raider->getY(),false);}
            bool broke=false;
            for(int i=0;i<MILLI2CYCLES(30000) && !broke;++i) {
                tick();
                broke=std::none_of(wave.fighters.begin(),wave.fighters.end(),
                    [](const UnitBase* unit){return unit->isInFormation();});
            }
            require(broke,"The formation did not break on contact");
            // The break happened at an enemy: the raiders, or whatever enemy the members now fight
            // (a wide Line can meet the map's own Atreides base first).
            std::vector<const ObjectBase*> enemies(raiders.begin(),raiders.end());
            for(const auto* unit:wave.fighters)
                if(unit->getTarget() && unit->getTarget()->getOwner()->getTeamID()!=owner->getTeamID())
                    enemies.push_back(unit->getTarget());
            int contactRange=INT32_MAX;
            for(const auto* unit:wave.fighters)
                for(const auto* foe:enemies)
                    contactRange=std::min(contactRange,
                        int(blockDistance(unit->getLocation(),foe->getClosestPoint(unit->getLocation())).lround()));
            require(contactRange<=10,"The formation broke away from any contact");
            int fighting=0, alive=0;
            for(auto* unit:wave.fighters) {
                if(currentGame->getObjectManager().getObject(rosterIds.at(unit))!=unit) continue;
                ++alive;
                require(unit->getAttackMode()==HUNT,"A member left Hunt when the formation broke");
                if(unit->getTarget() || unit->isHuntingToward() || unit->isMoving()) ++fighting;
            }
            require(alive>0 && fighting==alive,"A member did not go into the fight after the break");
            SDL_Log("WAVE_FORMATION_PROBE: shape=%s members=%zu forming_s=%.1f marching_s=%.1f lane_steps=%d fighting=%d",
                name,wave.fighters.size(),formingCycles*GAMESPEED_DEFAULT/1000.0,
                marchingCycles*GAMESPEED_DEFAULT/1000.0,laneSteps,fighting);
            require(laneSteps==0,"A marching member stood in a sonic tank's lane");
            retireAll();
        }

        // ---- 2. None: today's orders exactly ------------------------------------------
        {
            getQuantBotConfig().waveFormation=QuantBotConfig::kWaveFormationNone;
            Wave wave=muster(picks.size());
            std::vector<std::pair<Coord,bool>> before;
            for(auto* unit:wave.fighters) before.push_back({unit->getDestination(),unit->wasForced()});
            const ObjectBase* target=bot->customWaveObjective(wave.fighters.front());
            bot->launchGroundHunt();
            require(target && bot->customWave.front==target->getObjectID(),"None changed the wave's objective");
            for(size_t i=0;i<wave.fighters.size();++i) {
                auto* unit=wave.fighters[i];
                require(unit->getAttackMode()==HUNT && !unit->isInFormation() && !unit->isHuntingToward()
                        && unit->getDestination()==before[i].first && unit->wasForced()==before[i].second,
                    "WaveFormation None did not give today's engine-Hunt order");
            }
            retireAll();
        }

        // ---- 3. A wave below kMinAIGroup keeps today's orders ---------------------------
        {
            getQuantBotConfig().waveFormation=static_cast<int>(FormationPlanner::Shape::Pincer);
            Wave wave=muster(size_t(FormationOrders::kMinAIGroup-1));
            // Two tanks are not a viable whole-army wave: Easy (the share path; Medium joined
            // the whole-army difficulties, docs/ai-scenario-matrix.md) sends its share instead.
            bot->difficulty=QuantBot::Difficulty::Easy;
            getQuantBotConfig().easy.attackForceMilitaryValueRatio=1.0f;
            bot->launchGroundHunt();
            int hunting=0;
            for(auto* unit:wave.fighters) {
                require(!unit->isInFormation() && !unit->isHuntingToward(),"A small wave was sent in formation");
                if(unit->getAttackMode()==HUNT) ++hunting;
            }
            require(hunting>0,"The small wave did not launch");
            bot->difficulty=QuantBot::Difficulty::Hard;
            retireAll();
        }
    } catch(...) {
        getQuantBotConfig().waveFormation=savedShape;
        cleanupStructures(); unitList=savedUnits; throw;
    }
    getQuantBotConfig().waveFormation=savedShape;
    cleanupStructures();
    unitList=savedUnits;
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=wave_formation outcome=passed");
}
