// Real-engine QuantBot force concentration defaults (docs/formations.md, "Concentrating force").
// Included only by run-campaign-balance.py --force-probe; never linked into the game.
//
// Drives the production QuantBot::concentrateForces() and launchGroundHunt() on a Custom Hard
// house against real units and structures on the real map:
//   1. Defaults: the compiled and the shipped configuration focus fire, converge and launch
//      waves at 60% readiness; only Custom Hard/Brutal use them; the duel override replaces them.
//   2. Focus fire: every hunting tank with a damaged enemy quad in reach shoots it (fighters
//      before buildings, fewest hit points first), the same for a reversed unit list.
//   3. Converge: a wave member that is not fighting walks (hunt-toward) to a fight of its army
//      13 tiles away; one 35 tiles away and a home reserve keep their orders.
//   4. Wave size: a ready army worth 80% of the difficulty's wave readiness attacks with the
//      defaults (60%) and waits with the override 0 (the previous 100%).
//   5. Override 0 turns focus fire and converging off.
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
    require(defaults.force.focusFire && defaults.force.converge && defaults.force.waveReadinessPercent==60,
        "Compiled force defaults are not focus fire + converge + 60% waves");
    const auto& loaded=getQuantBotConfig();
    require(loaded.force.focusFire && loaded.force.converge && loaded.force.waveReadinessPercent==60,
        "The shipped QuantBot configuration did not load the force defaults");
    require(bot->forceBits()==(QuantBot::kForceFocusFire|QuantBot::kForceConverge) && bot->waveReadinessPercent()==60,
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
    Coord origin=Coord::Invalid();
    for(int y=2;y+kHeight<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for(int x=2;x+kWidth<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear=true;
            for(const Coord& at:enemyThings) {
                const int dx=std::max({x-at.x,0,at.x-(x+kWidth-1)}), dy=std::max({y-at.y,0,at.y-(y+kHeight-1)});
                if(std::max(dx,dy)<14) {clear=false;break;}
            }
            for(int dy=0;dy<kHeight && clear;++dy)for(int dx=0;dx<kWidth && clear;++dx) {
                const auto* tile=currentGameMap->getTile(x+dx,y+dy);
                if(tile->hasAGroundObject() || tile->isMountain())clear=false;
            }
            if(clear)origin=Coord(x,y);
        }
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
