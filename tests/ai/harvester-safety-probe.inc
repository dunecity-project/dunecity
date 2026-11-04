#include <structures/Refinery.h>

// Exercise real harvester orders, remembered jobs and refinery redeployment.
void runHarvesterSafetyProbe(Game& game) {
    auto require=[](bool ok,const char* why){if(!ok){SDL_Log("HARVESTER_SAFETY_FAIL: %s",why);throw std::runtime_error(why);}};
    auto* owner=pLocalHouse;
    QuantBot* bot=nullptr;
    for(const auto& player:owner->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get()))bot=candidate;
    require(bot,"Missing partner bot");
    House* enemy=nullptr;
    for(int id=0;id<NUM_HOUSES;++id)if(auto* house=game.getHouse(id))
        if(house->getTeamID()!=owner->getTeamID()) {enemy=house;break;}
    require(enemy,"Missing enemy house");
    Coord origin=Coord::Invalid();
    for(int y=1;y+8<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for(int x=1;x+18<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear=true;
            for(int dy=0;dy<8;++dy)for(int dx=0;dx<18;++dx)
                if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject())clear=false;
            if(clear)origin=Coord(x,y);
        }
    require(origin.isValid(),"Missing harvester fixture space");
    for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x)
        if(!currentGameMap->getTile(x,y)->hasAGroundObject())currentGameMap->getTile(x,y)->setType(Terrain_Sand);
    const Coord oldField=origin+Coord(10,3),safeField=origin+Coord(1,3);
    currentGameMap->getTile(oldField)->setSpice(1000);
    currentGameMap->getTile(safeField)->setSpice(1000);
    auto* worker=static_cast<Harvester*>(owner->createUnit(Unit_Harvester));
    auto* threat=enemy->createUnit(Unit_Tank);
    require(worker && threat,"Failed fixture unit creation");
    worker->deploy(oldField);threat->deploy(origin+Coord(14,3));
    threat->setVisible(owner->getTeamID(),true);
    auto* refinery=owner->placeStructure(NONE_ID,Structure_Refinery,origin.x,origin.y+5,true,true);
    require(refinery,"Missing fixture refinery");
    auto savedUnits=unitList;auto savedStructures=structureList;
    unitList.clear();unitList.push_back(worker);unitList.push_back(threat);
    structureList.clear();structureList.push_back(refinery);
    auto check=[&]() {
        bot->dangerUpdated=std::numeric_limits<Uint32>::max();bot->refreshTacticalDanger();
        bot->harvesterSafety[worker->getObjectID()].nextCheck=0;
        bot->manageHarvesterSafety(worker);
    };
    auto oldJob=[&](FixPoint cargo) {
        worker->setTarget(nullptr);worker->setAmountOfSpice(cargo);
        worker->doSetAttackMode(HARVEST);worker->doMove2Pos(oldField,false);
    };
    try {
        for(FixPoint cargo:{0_fix,100_fix}) {
            oldJob(cargo);check();
            require(!worker->isReturning(),"Evacuation unnecessarily returned to refinery");
            require(worker->getDestination()==safeField && worker->getGuardPoint()==safeField,
                "Evacuation did not replace the old field with safe spice");
            require(worker->getAttackMode()==HARVEST,"Safe alternate job is not harvesting");
        }
        oldJob(HARVESTERMAXSPICE);check();
        require(worker->getTarget()==refinery && worker->isReturning(),"Full load did not unload safely");
        require(worker->getGuardPoint()==safeField,"Unloading retained dangerous old job");
        check();require(worker->getTarget()==refinery,"Safety cancelled safe unloading");
        // The real deploy hook uses guardPoint after unloading or a carryall trip.
        worker->setTarget(nullptr);worker->setAmountOfSpice(0);
        currentGameMap->removeObjectFromMap(worker->getObjectID());worker->deploy(origin+Coord(3,4));
        require(worker->getDestination()==safeField,"Refinery redeployment returned to unsafe field");
        // Losing visibility is not evidence that the old field has become safe.
        threat->setVisible(owner->getTeamID(),false);oldJob(0);check();
        require(worker->getDestination()==safeField,"Forgot evacuated field when enemy became hidden");
        currentGameMap->getTile(safeField)->setType(Terrain_Sand);oldJob(0);check();
        require(worker->getAttackMode()==STOP && !worker->isReturning(),"No alternate field caused base/field loop");
        oldJob(100);check();
        require(worker->getTarget()==refinery && worker->getAttackMode()==STOP,
            "No safe field failed to unload cargo and disable the old job");
        check();require(worker->getTarget()==refinery,"Stopped post-unload job interrupted cargo return");
        worker->setTarget(nullptr);worker->setAmountOfSpice(0);
        currentGameMap->removeObjectFromMap(worker->getObjectID());worker->deploy(origin+Coord(3,4));
        check();require(worker->getAttackMode()==STOP && !worker->isReturning(),
            "Unloading without a safe alternative restarted dangerous harvesting");
        // Once danger has actually cooled down, harvesting can resume.
        game.gameCycleCount+=MILLI2CYCLES(120001);check();
        require(worker->getAttackMode()==HARVEST && worker->getDestination()==oldField,
            "Expired danger memory permanently stranded harvester");
        // Real enemy-origin worker captured by this bot's Deviator. Use the
        // actual timer (which damage can shorten), never capture-episode age.
        auto* borrowed=static_cast<Harvester*>(enemy->createUnit(Unit_Harvester));
        auto* deviator=owner->createUnit(Unit_Deviator);
        require(borrowed && deviator,"Failed deviation fixture creation");
        const Coord field=origin+Coord(3,3);
        currentGameMap->getTile(field)->setSpice(10000);
        borrowed->deploy(field);deviator->deploy(origin+Coord(5,3));
        borrowed->doSetAttackMode(STOP);borrowed->setVisible(VIS_ALL,true);
        SDL_Log("DEVIATED_HARVEST_STOP_SETUP: worker=%u item=%u mode=%d STOP=%d ptr=%p source=%u ptr=%p",
            borrowed->getObjectID(),borrowed->getItemID(),int(borrowed->getAttackMode()),int(STOP),
            static_cast<void*>(borrowed),deviator->getObjectID(),static_cast<void*>(deviator));
        require(deviator->canAttack(borrowed),"Deviator targeting excludes harvesters");
        unitList.clear();unitList.push_back(borrowed);unitList.push_back(deviator);
        SDL_Log("DEVIATED_HARVEST_STOP_LIST: mode=%d",int(borrowed->getAttackMode()));
        bot->doAttackObject(deviator,borrowed,true);
        SDL_Log("DEVIATED_HARVEST_STOP_ORDER: mode=%d",int(borrowed->getAttackMode()));
        require(deviator->getTarget()==borrowed && deviator->wasForced(),
            "QuantBot Deviator order did not retain the fixture target");
        require(borrowed->getAttackMode()==STOP,"Capture target was not stopped");
        const Uint32 captureStart=game.gameCycleCount;
        while(borrowed->getOwner()!=owner && game.gameCycleCount-captureStart<MILLI2CYCLES(120000)) {
            game.processObjects();++game.gameCycleCount;
        }
        SDL_Log("DEVIATED_HARVEST_CAPTURE_STATE: worker=%u owner=%d mode=%d at=%d,%d "
            "deviator=%u active=%d target=%u forced=%d at=%d,%d primary=%d bullets=%zu",
            borrowed->getObjectID(),borrowed->getOwner()->getHouseID(),int(borrowed->getAttackMode()),
            borrowed->getLocation().x,borrowed->getLocation().y,deviator->getObjectID(),int(deviator->isActive()),
            deviator->getTarget() ? deviator->getTarget()->getObjectID() : NONE_ID,int(deviator->wasForced()),
            deviator->getLocation().x,deviator->getLocation().y,deviator->primaryWeaponTimer,bulletList.size());
        require(borrowed->wasDeviated() && borrowed->getOwner()==owner,
            "QuantBot's real Deviator did not capture enemy worker");
        bot->doSetAttackMode(deviator,STOP);unitList.remove(deviator);
        SDL_Log("DEVIATED_HARVEST_CAPTURE: elapsed_cycles=%u",game.gameCycleCount-captureStart);
        const auto savedConfig=bot->configOverride;
        const auto savedManual=bot->manualUnitOrders;
        const auto savedDefence=bot->defenceAssignments;
        const int savedCapacity=owner->capacity;
        auto config=std::make_shared<QuantBotConfig>(bot->botConfig());
        bot->configOverride=config;owner->capacity=100000;
        auto reset=[&](FixPoint cargo,Sint32 remaining) {
            if(!borrowed->isActive()) {
                borrowed->setAmountOfSpice(0);refinery->update();
            }
            currentGameMap->removeObjectFromMap(borrowed->getObjectID());
            borrowed->deploy(field);
            borrowed->deviate(owner,deviator->getObjectID());
            borrowed->setTarget(nullptr);borrowed->setAmountOfSpice(cargo);
            borrowed->doSetAttackMode(HARVEST);borrowed->doMove2Pos(field,false);
            borrowed->deviationTimer=remaining;
            // The new deadline must bypass the old two-second safety cache.
            auto& state=bot->harvesterSafety[borrowed->getObjectID()];
            state.nextCheck=std::numeric_limits<Uint32>::max();state.controlled=false;
        };
        auto inspect=[&] {bot->manageHarvesterSafety(borrowed);};
        auto tick=[&] {game.processObjects();++game.gameCycleCount;};
        try {
            config->deviatedHarvesterEarlyReturn=true;
            reset(0,DEVIATIONTIME);borrowed->doSetAttackMode(GUARD);inspect();
            require(borrowed->getAttackMode()==HARVEST,
                "Captured idle worker was not put to work for the deviator");
            reset(borrowed->getSpiceCapacity()/2-1,DEVIATIONTIME);inspect();
            require(!borrowed->isReturning(),"Captured worker returned below half too early");
            reset(borrowed->getSpiceCapacity()/2,DEVIATIONTIME);inspect();
            require(borrowed->isReturning() && borrowed->getTarget()==refinery,
                "Captured half-full worker did not return to deviator refinery");
            const FixPoint cargo=borrowed->getAmountOfSpice();
            const FixPoint before=owner->getHarvestedSpice(),enemyBefore=enemy->getHarvestedSpice();
            const Uint32 start=game.gameCycleCount;
            while(borrowed->getAmountOfSpice()>0 && game.gameCycleCount-start<MILLI2CYCLES(60000))tick();
            require(borrowed->getAmountOfSpice()==0 && borrowed->getOwner()==owner,
                "Captured worker did not deliver before reverting");
            require(owner->getHarvestedSpice()-before==cargo*int(owner->getSpiceIncomeMultiplier())
                && enemy->getHarvestedSpice()==enemyBefore,"Delivery credited wrong house");
            SDL_Log("DEVIATED_HARVEST_DELIVERY: cargo=%d elapsed_cycles=%u remaining_cycles=%d",
                cargo.lround(),game.gameCycleCount-start,borrowed->getDeviationTimeRemaining());
            while(!borrowed->isActive() && game.gameCycleCount-start<MILLI2CYCLES(60000))tick();
            require(borrowed->isActive(),"Delivered worker did not redeploy");

            const Sint32 deadline=MILLI2CYCLES(10000);
            reset(100,deadline+1);inspect();
            require(!borrowed->isReturning(),"Deadline return fired before T-10s");
            borrowed->deviationTimer=deadline;inspect();
            require(borrowed->isReturning() && borrowed->getTarget()==refinery,
                "Low cargo did not return at T-10s");
            const FixPoint deadlineBefore=owner->getHarvestedSpice();
            const Uint32 deadlineStart=game.gameCycleCount;
            while(borrowed->getAmountOfSpice()>0
                && game.gameCycleCount-deadlineStart<Uint32(deadline))tick();
            require(borrowed->getAmountOfSpice()==0 && borrowed->getOwner()==owner,
                "T-10s low-cargo return did not deliver before reverting");
            require(owner->getHarvestedSpice()-deadlineBefore==100*int(owner->getSpiceIncomeMultiplier()),
                "T-10s delivery lost cargo income");
            SDL_Log("DEVIATED_HARVEST_DEADLINE_DELIVERY: cargo=100 elapsed_cycles=%u remaining_cycles=%d",
                game.gameCycleCount-deadlineStart,borrowed->getDeviationTimeRemaining());
            reset(0,deadline);inspect();
            require(!borrowed->isReturning(),"Empty captured worker made deadline trip");
            config->deviatedHarvesterEarlyReturn=false;
            reset(borrowed->getSpiceCapacity()/2,deadline);inspect();
            require(!borrowed->isReturning(),"Disabled rule changed old worker behaviour");
            config->deviatedHarvesterEarlyReturn=true;
            reset(borrowed->getSpiceCapacity()/2,deadline);borrowed->deviate(enemy);inspect();
            require(!borrowed->isReturning(),"Non-deviated worker used captured return rule");
            worker->setAmountOfSpice(worker->getSpiceCapacity()/2);
            auto& ordinaryState=bot->harvesterSafety[worker->getObjectID()];
            ordinaryState.nextCheck=std::numeric_limits<Uint32>::max();ordinaryState.controlled=false;
            bot->manageHarvesterSafety(worker);
            require(worker->getOwner()==owner && !worker->wasDeviated() && !worker->isReturning(),
                "Ordinary own worker used captured return rule");
            reset(borrowed->getSpiceCapacity()/2,deadline);
            bot->onHumanUnitOrder(borrowed->getObjectID());inspect();
            require(!borrowed->isReturning(),"Captured return replaced human order lease");
            bot->manualUnitOrders.erase(borrowed->getObjectID());
            bot->defenceAssignments[borrowed->getObjectID()]=threat->getObjectID();inspect();
            require(!borrowed->isReturning(),"Captured return stole defence role");
            bot->defenceAssignments.erase(borrowed->getObjectID());
            structureList.clear();inspect();
            require(!borrowed->isReturning(),"No-refinery capture forced a return");
            structureList.push_back(refinery);

            // Ownership expiring on the road clears the booking and target.
            // Inside, income belongs to the refinery physically unloading it.
            reset(100,1);inspect();
            require(borrowed->isReturning(),"Expiry fixture was not on return trip");
            const int booked=refinery->getHarvesterDropoffBookings();
            borrowed->update();
            require(borrowed->getOwner()==enemy && !borrowed->isReturning()
                && !borrowed->getTarget() && refinery->getHarvesterDropoffBookings()==booked-1,
                "Mid-return expiry left stale return target or booking");
            reset(100,1);inspect();borrowed->setReturned();
            require(!borrowed->isActive(),"Unload expiry fixture did not enter refinery");
            borrowed->update();
            require(borrowed->getOwner()==enemy,"Inactive harvester timer did not expire");
            const FixPoint unloadingBefore=owner->getHarvestedSpice(),originalBefore=enemy->getHarvestedSpice();
            const Uint32 unloadingStart=game.gameCycleCount;
            while(!borrowed->isActive() && game.gameCycleCount-unloadingStart<MILLI2CYCLES(30000))tick();
            require(borrowed->isActive() && borrowed->getAmountOfSpice()==0,
                "Mid-unload expiry stranded harvester inside refinery");
            require(owner->getHarvestedSpice()-unloadingBefore==100*int(owner->getSpiceIncomeMultiplier())
                && enemy->getHarvestedSpice()==originalBefore,
                "Mid-unload expiry credited non-unloading house");
            require(refinery->getHarvesterDropoffBookings()==0,"Mid-unload expiry leaked booking");
            SDL_Log("DEVIATED_HARVEST_PASS: half-load, T-10s=%d cycles, empty/OFF/ordinary/human/role, "
                "no-refinery, return-expiry, unload-expiry=%u cycles",deadline,game.gameCycleCount-unloadingStart);
        } catch(...) {
            bot->configOverride=savedConfig;bot->manualUnitOrders=savedManual;
            bot->defenceAssignments=savedDefence;owner->capacity=savedCapacity;throw;
        }
        bot->configOverride=savedConfig;bot->manualUnitOrders=savedManual;
        bot->defenceAssignments=savedDefence;owner->capacity=savedCapacity;
        SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=harvester_safety outcome=passed");
    } catch(...) {unitList=savedUnits;structureList=savedStructures;throw;}
    unitList=savedUnits;structureList=savedStructures;
}
