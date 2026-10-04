// Real-engine check for the enemy-facing battery clearance fallback.
// Included by army-recovery-probe.inc and selected by its own CTest entry.
// Never linked into the shipped game.
//
// What it establishes, against the production build pass, the production
// placement route and real engine frames:
//
//   * an established city with no free legal ground left really does displace
//     one of its own eligible R/C/I lots and really does end up with a rocket
//     turret standing on the freed tile - with concrete required and without;
//   * the lot is still standing while the order is merely accepted, and a
//     cancellation, a destroyed yard or a site that stops being legal before
//     finished material is placed costs the city nothing;
//   * free ground always wins, including when it opens after the order;
//   * the economic floor is local to the colony that is building, so a second
//     colony cannot mask the loss of the last lots next to this one;
//   * two yards with different colonies do not read each other's answers;
//   * no demand, no economy, an unready yard, no money, no observed enemy, a
//     foreign or human-shared house and a protected lot displace nothing;
//   * one lot per construction pass across every yard;
//   * all three zone types are reachable and the choice is reproducible.
//
// Construction is never forced anywhere in the end-to-end cases: the fixture
// keeps the treasury inside the band an established city sits in and then runs
// ordinary engine frames. It never writes productionProgress, never calls
// setWaitingToPlace and never advances a builder by hand.
#include <PathWorkerPool.h>

void runBatteryClearanceProbe(Game& game) {
    auto require=[](bool ok,const char* why){
        if(!ok){SDL_Log("BATTERY_CLEARANCE_FAIL: %s",why);throw std::runtime_error(why);}
    };
    const size_t workers=PathWorkerPool::instance().workerCount();
    const char* requestedWorkers=std::getenv("DUNECITY_PATH_WORKERS");
    SDL_Log("BATTERY_CLEARANCE_WORKERS: actual=%zu requested=%s",workers,
        requestedWorkers?requestedWorkers:"default");
    if(requestedWorkers && (std::string(requestedWorkers)=="1" || std::string(requestedWorkers)=="4"))
        require(workers==size_t(std::atoi(requestedWorkers)),"Path worker startup fell back during parity verification");
    auto* house=game.getHouse(HOUSE_HARKONNEN);
    auto* enemy=game.getHouse(HOUSE_ATREIDES);
    require(house && enemy,"Clearance fixture missing houses");
    require(game.isCitySimEnabled(),"Clearance fixture needs city mode");
    auto* sim=game.getCitySimulation();
    require(sim && sim->isInitialized(),"Clearance fixture needs a live city simulation");
    QuantBot* bot=nullptr;
    for(const auto& player:house->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get()))bot=candidate;
    require(bot,"Clearance fixture missing bot");

    game.gameType=GameType::CustomGame;
    bot->gameMode=QuantBot::GameMode::Custom;
    bot->supportMode=false;
    bot->difficulty=QuantBot::Difficulty::Brutal;
    getQuantBotConfig().recovery.enabled=true;
    getQuantBotConfig().recovery.frontBatteriesEnabled=true;
    require(bot->recoveryActive(),"Clearance fixture refused Custom Brutal");

    // The colony neighbourhood the belt and the economic floor both use.
    const int turretRange=std::max(1,
        game.objectData.data[Structure_RocketTurret][HOUSE_HARKONNEN].weaponrange);
    const int colonyRadius=2*turretRange;
    SDL_Log("BATTERY_CLEARANCE_DIAG: turret_range=%d colony_radius=%d",turretRange,colonyRadius);
    require(turretRange>=7,
        "Clearance fixture lays its lots out for a colony radius of at least fourteen tiles");

    // Enough to buy the emplacement and its concrete comfortably, and short of
    // the treasury at which this base would want another production lane: an
    // unplaceable heavy factory would otherwise take every pass before the
    // battery rule is reached, which says nothing about the fallback.
    const int kFunded=4400;

    const std::array<Uint32,3> zoneItems{Structure_ZoneResidential,Structure_ZoneCommercial,
        Structure_ZoneIndustrial};

    auto setConcreteRequired=[&](bool required) {
        auto options=game.getGameInitSettings().getGameOptions();
        options.concreteRequired=required;
        game.gameInitSettings.setGameOptions(options);
    };

    // ---- The fixture city ------------------------------------------------
    //
    // The fallback only ever runs when the ordinary free-ground search has
    // nothing at all, so the fixture reproduces that honestly rather than by
    // disabling the search. Every building on the map goes first - including
    // the other houses', so the observed front is the fixture's own - and then
    // every tile that is not under a building becomes sand, which is legal
    // ground for a city lot but not for an emplacement. The only rock this
    // house owns afterwards is the ground its own buildings stand on, which is
    // exactly the position an established city reaches on its own.
    //
    // Two colonies: a main one that builds the battery, and a remote one whose
    // lots must neither be counted as this colony's economy nor inherit this
    // colony's search answers.
    Coord origin=Coord::Invalid();
    struct Colony { BuilderBase* yard=nullptr; Coord at=Coord::Invalid(); };
    Colony main, remote;
    StructureBase* enemyBase=nullptr;
    StructureBase* remoteEnemyBase=nullptr;
    std::vector<Uint32> mainLotIds, remoteLotIds;

    auto demolishEverything=[&] {
        std::vector<Uint32> doomed;
        for(auto* structure:structureList) doomed.push_back(structure->getObjectID());
        for(Uint32 id:doomed)
            if(auto* structure=dynamic_cast<StructureBase*>(game.getObjectManager().getObject(id)))
                structure->destroy();
    };
    // Keep unrelated units out of the fixture's construction sites. Destroying
    // a unit can leave a survivor behind, so repeat until the map is empty.
    auto removeAllUnits=[&] {
        for(int round=0;round<4;++round) {
            std::vector<Uint32> doomed;
            for(auto* unit:unitList) doomed.push_back(unit->getObjectID());
            if(doomed.empty())return;
            for(Uint32 id:doomed)
                if(auto* unit=dynamic_cast<UnitBase*>(game.getObjectManager().getObject(id)))
                    unit->destroy();
        }
    };

    auto put=[&](House* owner,Uint32 item,Coord at)->StructureBase* {
        auto* structure=owner->placeStructure(NONE_ID,item,at.x,at.y,true,true);
        require(structure!=nullptr,"Clearance fixture placement failed");
        structure->setVisible(house->getTeamID(),true);
        structure->setVisible(enemy->getTeamID(),true);
        return structure;
    };

    // Lot offsets from a colony's yard, all inside the colony neighbourhood and
    // each with a one-tile gap so no lot starts out landlocked.
    const std::array<int,3> lotColumns{5,8,11};
    const std::array<int,5> lotRows{-6,-3,0,3,6};

    auto buildColony=[&](Coord yardAt,std::vector<Uint32>& lotIds,bool productionCore)->BuilderBase* {
        auto* yard=static_cast<BuilderBase*>(put(house,Structure_ConstructionYard,yardAt));
        if(productionCore) {
            put(house,Structure_Refinery,yardAt+Coord(-0,-12));
            put(house,Structure_Refinery,yardAt+Coord(-0,-9));
            put(house,Structure_WindTrap,yardAt+Coord(-0,-6));
            put(house,Structure_Radar,yardAt+Coord(-0,-3));
            put(house,Structure_HeavyFactory,yardAt+Coord(0,4));
            put(house,Structure_HighTechFactory,yardAt+Coord(0,7));
            put(house,Structure_RepairYard,yardAt+Coord(0,10));
            put(house,Structure_LightFactory,yardAt+Coord(0,13));
            const auto prerequisites=
                game.objectData.data[Structure_RocketTurret][HOUSE_HARKONNEN].prerequisiteStructuresSet;
            for(unsigned item=0;item<prerequisites.size();++item)
                if(prerequisites.test(item) && !house->getNumItems(item))
                    put(house,item,yardAt+Coord(-6,-12));
        }
        lotIds.clear();
        for(size_t type=0;type<zoneItems.size();++type)
            for(int row:lotRows)
                lotIds.push_back(put(house,zoneItems[type],
                    yardAt+Coord(lotColumns[type],row))->getObjectID());
        return yard;
    };

    auto sandEverythingUnbuilt=[&] {
        for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x) {
            Tile* tile=currentGameMap->getTile(x,y);
            if(tile->hasAStructure())continue;
            tile->setType(Terrain_Sand);
            tile->setRoad(false);
            tile->setDestroyedStructureTile(DestroyedStructure_None);
            tile->setOwner(INVALID);
        }
    };

    auto resetCity=[&](bool concreteRequired) {
        setConcreteRequired(concreteRequired);
        demolishEverything();
        removeAllUnits();
        if(origin.isInvalid()) {
            const int span=34;
            for(int y=2;y+span<currentGameMap->getSizeY() && origin.isInvalid();++y)
                for(int x=2;x+span<currentGameMap->getSizeX() && origin.isInvalid();++x) {
                    bool clear=true;
                    for(int dy=0;dy<span && clear;++dy)for(int dx=0;dx<span && clear;++dx) {
                        const auto* tile=currentGameMap->getTile(x+dx,y+dy);
                        if(tile->hasAGroundObject() || tile->isMountain())clear=false;
                    }
                    if(clear)origin=Coord(x,y);
                }
            require(origin.isValid(),"Clearance fixture needs a clear area");
        }
        // Bare sand before anything is placed: spice, blooms, leftover concrete
        // and road flags would each give the fixture ground it is not supposed
        // to have. Every rock tile from here on is one a building made.
        sandEverythingUnbuilt();
        main.at=origin+Coord(4,16);
        remote.at=main.at+Coord(0,40);
        require(currentGameMap->tileExists(remote.at.x+14,remote.at.y+14),
            "Clearance fixture needs room for a remote colony");
        main.yard=buildColony(main.at,mainLotIds,true);
        remote.yard=buildColony(remote.at,remoteLotIds,false);
        // A visible enemy base due east of each colony, so both face east and
        // neither colony's nearest enemy is the other colony's.
        enemyBase=put(enemy,Structure_ConstructionYard,main.at+Coord(26,0));
        remoteEnemyBase=put(enemy,Structure_ConstructionYard,remote.at+Coord(26,0));
        sandEverythingUnbuilt();
        bot->dangerUpdated=std::numeric_limits<Uint32>::max();
        bot->refreshTacticalDanger();
        for(Uint32 item:zoneItems)
            require(house->getNumItems(item)==int(2*lotRows.size()),
                "Clearance fixture did not place both colonies' lots");
        auto& state=sim->houseState_[HOUSE_HARKONNEN];
        state.resValve=0;state.comValve=0;state.indValve=0;
        for(int y=0;y<currentGameMap->getSizeY()/sim->landValueMap_.getBlockSize()+1;++y)
            for(int x=0;x<currentGameMap->getSizeX()/sim->landValueMap_.getBlockSize()+1;++x)
                sim->landValueMap_.set(x,y,0);
        house->producedPower=20000;
        house->storedCredits=0;house->cityCredits=0;house->startingCredits=kFunded;
        bot->builderPlaceLocations.clear();bot->reservedStructures.clear();
        bot->lastSurveyCycle=std::numeric_limits<Uint32>::max();
        bot->clearPlacementCache();
    };

    auto readyYard=[&](BuilderBase* yard) {
        yard->upgrading=false;yard->setHealth(yard->getMaxHealth());
        yard->curUpgradeLev=yard->getMaxUpgradeLevel();yard->updateBuildList();
    };
    auto freshPass=[&](int credits) {
        for(auto* structure:structureList)if(structure->getOwner()==house && structure->isABuilder()) {
            auto* builder=static_cast<BuilderBase*>(structure);
            while(builder->getProductionQueueSize())builder->doCancelItem(builder->getCurrentProducedItem());
        }
        if(main.yard)readyYard(main.yard);
        // Only the main colony's yard is allowed to order the battery in the
        // whole-pass cases: the per-pass bound is one emplacement across every
        // yard, so an equally eligible remote yard would simply win the race
        // and the case would be about the wrong colony. An unupgraded yard is
        // an ordinary state the battery rule already refuses.
        if(remote.yard) {
            remote.yard->upgrading=false;remote.yard->setHealth(remote.yard->getMaxHealth());
            remote.yard->curUpgradeLev=0;remote.yard->updateBuildList();
        }
        bot->builderPlaceLocations.clear();bot->reservedStructures.clear();
        bot->clearPlacementCache();
        house->storedCredits=0;house->cityCredits=0;house->startingCredits=credits;
        house->producedPower=20000;bot->lastCalculatedSpice=1300000;
        bot->lastSurveyCycle=std::numeric_limits<Uint32>::max();
        bot->planningBuilder=main.yard?main.yard->getObjectID():NONE_ID;
    };
    auto queuedItem=[&](BuilderBase* yard) {
        for(const auto& order:yard->currentProductionQueue)
            if(order.itemID!=Structure_Slab1 && order.itemID!=Structure_Slab4
                && order.itemID!=Structure_Road)return order.itemID;
        return Uint32(NONE_ID);
    };
    auto ownZones=[&]{
        int total=0;
        for(Uint32 item:zoneItems)total+=house->getNumItems(item);
        return total;
    };
    auto liveZone=[&](Uint32 id)->ZoneStructure* {
        return dynamic_cast<ZoneStructure*>(currentGame->getObjectManager().getObject(id));
    };
    // Boundary checks need finished material without the AI placing it before
    // the check runs. Hold the house's players, not its builders or the world
    // clock: the ordinary engine still constructs and pays for every item.
    auto waitForMaterial=[&](std::initializer_list<BuilderBase*> yards) {
        std::list<std::unique_ptr<Player>> held;
        held.splice(held.end(),house->players);
        struct RestorePlayers {
            House* owner; std::list<std::unique_ptr<Player>>& held;
            ~RestorePlayers(){owner->players.splice(owner->players.end(),held);}
        } restore{house,held};
        int frames=0;
        for(;frames<9000;++frames) {
            const bool ready=std::all_of(yards.begin(),yards.end(),
                [](const BuilderBase* yard){return yard->isWaitingToPlace();});
            if(ready)break;
            if(house->getCredits()<kFunded)
                house->startingCredits+=kFunded-house->getCredits();
            house->producedPower=20000;
            game.updateGameState();
        }
        require(frames<9000,"Real production did not finish the boundary-test material");
    };
    auto anchorOf=[&](BuilderBase* yard) {
        bot->planningBuilder=yard->getObjectID();
        bot->clearPlacementCache(false,true);
        return bot->batteryAnchor();
    };
    auto clearanceSiteFor=[&](BuilderBase* yard) {
        bot->planningBuilder=yard->getObjectID();
        bot->clearPlacementCache(false,true);
        return bot->findBatteryClearanceSite();
    };

    resetCity(true);

    // ---- 1. The free-ground search really has nothing --------------------
    freshPass(kFunded);
    bot->lastSurvey=bot->surveyArmy();
    const Coord anchor=bot->batteryAnchor();
    const Coord forward=bot->observedFrontDirection(anchor);
    SDL_Log("BATTERY_CLEARANCE_DIAG: anchor=%d,%d forward=%d,%d lots=%d",
        anchor.x,anchor.y,forward.x,forward.y,ownZones());
    require(anchor==main.at,"The battery anchored somewhere other than the planning colony");
    require(forward.x>0,"Clearance fixture does not face the enemy it placed due east");
    require(!bot->findFrontBatteryPlaceLocation().isValid(),
        "Clearance fixture still has free legal ground for an emplacement");

    // ---- 2. Who may never displace a lot ---------------------------------
    //
    // House::isAI() is false exactly when a human player shares the house, and
    // that is the authority boundary the fallback must respect: the lots are
    // then somebody's city, not the bot's. The engine derives the flag from the
    // player list, so setting it is the same statement as attaching a human.
    {
        const bool wasAI=house->isAI();
        house->ai=false;
        bot->clearPlacementCache();
        require(!bot->findBatteryClearanceSite().isValid(),
            "A house shared with a human player had its lots displaced");
        house->ai=wasAI;
        bot->clearPlacementCache();
        require(house->isAI(),"Clearance fixture needs a house this bot owns outright");

        const auto restoreType=game.gameType;
        game.gameType=GameType::Campaign;
        bot->clearPlacementCache();
        require(!bot->findBatteryClearanceSite().isValid(),"Clearance leaked into a campaign game");
        game.gameType=restoreType;
        bot->difficulty=QuantBot::Difficulty::Medium;
        bot->clearPlacementCache();
        require(!bot->findBatteryClearanceSite().isValid(),"Clearance leaked into Medium");
        bot->difficulty=QuantBot::Difficulty::Brutal;
        bot->supportMode=true;
        bot->clearPlacementCache();
        require(!bot->findBatteryClearanceSite().isValid(),"Clearance leaked into support mode");
        bot->supportMode=false;
        getQuantBotConfig().recovery.frontBatteriesEnabled=false;
        bot->clearPlacementCache();
        require(!bot->findBatteryClearanceSite().isValid(),
            "Clearance ignored the battery configuration switch");
        getQuantBotConfig().recovery.frontBatteriesEnabled=true;
        bot->clearPlacementCache();
    }

    // ---- 3. A legal clearance site, on the enemy side, on our own lot -----
    const Coord site=bot->findBatteryClearanceSite();
    SDL_Log("BATTERY_CLEARANCE_DIAG: site=%d,%d valid=%d",site.x,site.y,site.isValid());
    require(site.isValid(),"A crowded city found no lot it could displace");
    require(FrontBatteryPolicy::onFrontSide(site.x-anchor.x,site.y-anchor.y,forward.x,forward.y),
        "The displaced lot is not on the observed enemy side");
    require(blockDistance(site,anchor).lround()<=colonyRadius,
        "The displaced lot is not in the colony that is building the battery");
    {
        const auto* zone=dynamic_cast<const ZoneStructure*>(
            currentGameMap->getTile(site)->getNonInfantryGroundObject());
        require(zone && zone->getOwner()==house,"The clearance site is not one of this house's own lots");
        require(DuneCity::isCityZoneStructure(zone->getItemID()),
            "The clearance site is not a residential, commercial or industrial lot");
    }
    bot->clearPlacementCache();
    require(bot->findBatteryClearanceSite()==site,"The clearance search is not reproducible");

    // ---- 4. Nothing essential, foreign or protected is ever a candidate ---
    {
        std::vector<Uint32> ignored;
        require(!bot->batteryClearanceZones(main.yard->getLocation(),ignored,anchor),
            "A construction yard was offered as clearance ground");
        require(!bot->batteryClearanceZones(enemyBase->getLocation(),ignored,anchor),
            "A foreign building was offered as clearance ground");
        auto* chosen=dynamic_cast<ZoneStructure*>(
            currentGameMap->getTile(site)->getNonInfantryGroundObject());
        require(chosen,"Clearance fixture lost its chosen lot");
        const Coord lot=chosen->getLocation();
        currentGameMap->getTile(lot.x,lot.y)->setCityZoneDensity(3);
        require(!bot->batteryClearanceZones(site,ignored,anchor),"A grown lot was displaced");
        currentGameMap->getTile(lot.x,lot.y)->setCityZoneDensity(0);
        // Residents are a residential lot's measure, so that case is made on
        // one. setResidentialPopulation also raises the tile's growth level,
        // which is the other half of "this lot is carrying the economy".
        auto* inhabited=liveZone(mainLotIds.front());
        require(inhabited && inhabited->getItemID()==Structure_ZoneResidential,
            "Clearance fixture expected a residential lot first");
        const Coord inhabitedAt=inhabited->getLocation();
        require(bot->batteryClearanceZones(inhabitedAt,ignored,anchor),
            "An empty residential lot was not displaceable to begin with");
        inhabited->setResidentialPopulation(40);
        require(inhabited->getResidentialPopulation()==40,"Clearance fixture could not fill a lot");
        require(!bot->batteryClearanceZones(inhabitedAt,ignored,anchor),"An inhabited lot was displaced");
        inhabited->setResidentialPopulation(0);
        require(bot->batteryClearanceZones(inhabitedAt,ignored,anchor),
            "An emptied lot never became displaceable again");
        chosen->setCivicOverlay(ZoneStructure::CivicOverlay::Hospital);
        require(!bot->batteryClearanceZones(site,ignored,anchor),"A civic service lot was displaced");
        chosen->setCivicOverlay(ZoneStructure::CivicOverlay::None);
        sim->landValueMap_.set(lot.x/sim->landValueMap_.getBlockSize(),
            lot.y/sim->landValueMap_.getBlockSize(),200);
        require(bot->batteryClearanceZones(site,ignored,anchor),
            "Land value alone vetoed an otherwise affordable displacement");
        bot->clearPlacementCache();
        const Coord cheaper=bot->findBatteryClearanceSite();
        require(cheaper.isValid() && !(cheaper.x>=lot.x && cheaper.x<lot.x+2
                && cheaper.y>=lot.y && cheaper.y<lot.y+2),
            "The planner failed to prefer cheaper lots over valuable land");
        sim->landValueMap_.set(lot.x/sim->landValueMap_.getBlockSize(),
            lot.y/sim->landValueMap_.getBlockSize(),0);
        bot->reservedStructures[main.yard->getObjectID()+1u]={Structure_RocketTurret,lot};
        require(!bot->batteryClearanceZones(site,ignored,anchor),"A reserved lot was displaced");
        bot->reservedStructures.clear();
        require(bot->batteryClearanceZones(site,ignored,anchor) && ignored.size()==1,
            "The clearance rule stopped accepting its own site, or took more than one lot");
    }

    // ---- 5. The floor is the building colony's, not the whole house's -----
    //
    // R2: with a second colony standing, a whole-house count of a zone type
    // says nothing about whether this colony can still afford to give one up.
    {
        auto* chosen=liveZone(currentGameMap->getTile(site)->getNonInfantryGroundObject()
            ? currentGameMap->getTile(site)->getNonInfantryGroundObject()->getObjectID() : NONE_ID);
        require(chosen,"Clearance fixture lost its chosen lot");
        const Uint32 item=chosen->getItemID();
        const Coord chosenAt=chosen->getLocation();
        std::vector<Coord> removedAt;
        for(const Uint32 id:mainLotIds) {
            auto* zone=liveZone(id);
            if(!zone || zone->getItemID()!=item || zone->getLocation()==chosenAt) continue;
            if(removedAt.size()>=2) break;
            removedAt.push_back(zone->getLocation());
            zone->demolish();
        }
        require(removedAt.size()==2,"Clearance fixture could not thin this colony's lots");
        const int localRemaining=int(lotRows.size())-2;
        SDL_Log("BATTERY_CLEARANCE_DIAG: floor local=%d house=%d",
            localRemaining,house->getNumItems(item));
        require(house->getNumItems(item)>=localRemaining+int(lotRows.size()),
            "Clearance fixture expected the remote colony to still hold its lots");
        std::vector<Uint32> ignored;
        require(!bot->batteryClearanceZones(site,ignored,anchor),
            "A remote colony's lots masked the loss of the last local ones");
        // Give the colony its lots back exactly where they stood. The ids in
        // mainLotIds are stale from here on, so the fixture re-reads them.
        for(const Coord at:removedAt) put(house,item,at);
        mainLotIds.clear();
        for(auto* structure:structureList)
            if(structure->getOwner()==house && DuneCity::isCityZoneStructure(structure->getItemID())
                && blockDistance(structure->getLocation(),main.at).lround()<=colonyRadius)
                mainLotIds.push_back(structure->getObjectID());
        bot->clearPlacementCache();
        require(bot->batteryClearanceZones(site,ignored,anchor),
            "A restored local floor did not make the lot displaceable again");
    }

    // ---- 6. Two colonies, two anchors, no shared answers ------------------
    //
    // R3: both yards are unreserved, so they share the reservation-set cache
    // key. Anything anchored on the planning builder must still be re-derived.
    {
        const Coord mainAnchor=anchorOf(main.yard);
        const Coord mainSite=clearanceSiteFor(main.yard);
        const Coord remoteAnchor=anchorOf(remote.yard);
        const Coord remoteSite=clearanceSiteFor(remote.yard);
        SDL_Log("BATTERY_CLEARANCE_DIAG: anchors main=%d,%d remote=%d,%d sites main=%d,%d remote=%d,%d",
            mainAnchor.x,mainAnchor.y,remoteAnchor.x,remoteAnchor.y,
            mainSite.x,mainSite.y,remoteSite.x,remoteSite.y);
        require(mainAnchor==main.at && remoteAnchor==remote.at,
            "A yard did not anchor its battery on its own colony");
        require(mainSite.isValid() && remoteSite.isValid(),
            "A colony found no clearance site of its own");
        require(mainSite!=remoteSite,
            "The second yard reused the first colony's cached clearance site");
        require(blockDistance(mainSite,main.at).lround()<=colonyRadius
            && blockDistance(remoteSite,remote.at).lround()<=colonyRadius,
            "A colony's clearance site belongs to the other colony");
        // And back again: the first yard still gets its own answer.
        require(clearanceSiteFor(main.yard)==mainSite,
            "Returning to the first yard did not restore its own clearance site");
        // The same derived decision is reproduced from the same state rather
        // than carried in the save: a round trip through the real save stream
        // keeps no clearance state at all and the reloaded bot re-derives it.
        OMemoryStream out; out.open();
        bot->save(out);
        const std::string bytes(out.getData(),out.getDataLength());
        IMemoryStream in(bytes.data(),bytes.size());
        const Uint32 savedVersion=currentGame->loadedSavegameVersion;
        currentGame->loadedSavegameVersion=SAVEGAMEVERSION;
        QuantBot restored(in,house);
        currentGame->loadedSavegameVersion=savedVersion;
        require(!restored.batteryClearanceCache.has_value(),
            "A reloaded bot carried a derived clearance answer instead of re-deriving it");
        restored.planningBuilder=main.yard->getObjectID();
        restored.lastSurveyCycle=std::numeric_limits<Uint32>::max();
        restored.lastSurvey=restored.surveyArmy();
        restored.visibleEnemyBases=bot->visibleEnemyBases;
        restored.clearPlacementCache();
        const Coord reloadedSite=restored.findBatteryClearanceSite();
        SDL_Log("BATTERY_CLEARANCE_DIAG: reloaded_site=%d,%d",reloadedSite.x,reloadedSite.y);
        require(reloadedSite==mainSite,
            "A bot reloaded from the same state made a different clearance decision");
        bot->planningBuilder=main.yard->getObjectID();
        bot->clearPlacementCache();
    }

    // ---- 7. Ordering commits nothing -------------------------------------
    //
    // R1: an accepted order is neither payment nor a placement. Everything that
    // can go wrong between the order and finished material must cost the city
    // nothing at all.
    auto orderBattery=[&]()->Coord {
        freshPass(kFunded);
        bot->build(0);
        const Uint32 ordered=queuedItem(main.yard);
        require(ordered==Structure_RocketTurret,
            "A crowded city did not order the emplacement its battery rule wanted");
        require(bot->reservedStructures.count(main.yard->getObjectID()),
            "The ordered emplacement reserved no site");
        return bot->reservedStructures.at(main.yard->getObjectID()).location;
    };
    {
        const int before=ownZones();
        const Coord reserved=orderBattery();
        SDL_Log("BATTERY_CLEARANCE_DIAG: ordered reserved=%d,%d zones=%d",
            reserved.x,reserved.y,ownZones());
        require(ownZones()==before,"Accepting the order already cost the city a lot");
        const auto* standing=currentGameMap->getTile(reserved)->getNonInfantryGroundObject();
        require(dynamic_cast<const ZoneStructure*>(standing)!=nullptr,
            "The reserved emplacement site is no longer the lot it named");

        // Cancelled before any material exists.
        main.yard->doCancelItem(Structure_RocketTurret);
        bot->reservedStructures.clear();
        require(ownZones()==before,"A cancelled emplacement still cost the city a lot");

        // The yard itself destroyed before any material exists.
        orderBattery();
        const Uint32 lostYard=main.yard->getObjectID();
        main.yard->destroy();main.yard=nullptr;
        bot->reservedStructures.erase(lostYard);
        require(ownZones()==before,"Losing the yard mid-order still cost the city a lot");
        main.yard=static_cast<BuilderBase*>(put(house,Structure_ConstructionYard,main.at));
        sandEverythingUnbuilt();
        bot->clearPlacementCache();

        // A site that stops being legal before any material exists.
        const Coord stale=orderBattery();
        auto* intruder=house->createUnit(Unit_Soldier);
        require(intruder,"Clearance fixture could not create a blocker");
        intruder->deploy(stale+Coord(1,0));
        bot->clearPlacementCache();
        std::vector<Uint32> ignored;
        require(!bot->batteryClearanceZones(stale,ignored,bot->batteryAnchor()),
            "An occupied lot stayed displaceable");
        require(ownZones()==before,"A site that stopped being legal still cost the city a lot");
        intruder->setHealth(0);
        currentGameMap->removeObjectFromMap(intruder->getObjectID());
        currentGame->getObjectManager().removeObject(intruder->getObjectID());
        unitList.remove(intruder);
        bot->reservedStructures.clear();
        bot->clearPlacementCache();
        require(ownZones()==before,"The no-commit cases cost the city a lot between them");
    }

    // ---- 8. Free ground that opens after the order wins ------------------
    {
        const int before=ownZones();
        const Coord reserved=orderBattery();
        const Coord opened=main.at+Coord(-3,0);
        currentGameMap->getTile(opened)->setType(Terrain_Rock);
        currentGameMap->getTile(opened)->setOwner(house->getHouseID());
        // The production route runs this with the planning builder set to the
        // yard it is servicing, which is what anchors the battery.
        bot->planningBuilder=main.yard->getObjectID();
        bot->clearPlacementCache(false,true);
        require(bot->findFrontBatteryPlaceLocation().isValid(),
            "Clearance fixture failed to open a free emplacement site");
        auto& planned=bot->builderPlaceLocations[main.yard->getObjectID()];
        require(!planned.empty() && planned.front()==reserved,
            "Clearance fixture expected the reserved site at the head of the placement plan");
        const bool committed=bot->commitBatteryClearance(main.yard,Structure_Slab1,planned,
            kFunded,0);
        SDL_Log("BATTERY_CLEARANCE_DIAG: retarget committed=%d reserved_now=%d,%d",
            int(committed),bot->reservedStructures.at(main.yard->getObjectID()).location.x,
            bot->reservedStructures.at(main.yard->getObjectID()).location.y);
        require(!committed,"A lot was displaced although free ground had opened");
        require(ownZones()==before,"A lot was displaced although free ground had opened");
        require(bot->reservedStructures.at(main.yard->getObjectID()).location!=reserved,
            "The emplacement stayed on the lot although free ground had opened");
        currentGameMap->getTile(opened)->setType(Terrain_Sand);
        currentGameMap->getTile(opened)->setOwner(INVALID);
        bot->reservedStructures.clear();
        bot->clearPlacementCache();
    }

    // ---- 9. End to end on real frames, concrete required ------------------
    //
    // R1/R4: no forced progress anywhere. The fixture orders once through the
    // production entry point, then hands the world to the engine and keeps the
    // treasury where an established city's is. The lot must still be standing
    // when the order is accepted, and a real rocket turret must end up on the
    // ground it eventually gives up.
    auto runToTurret=[&](const char* label,bool concreteRequired)->Coord {
        resetCity(concreteRequired);
        freshPass(kFunded);
        bot->lastSurvey=bot->surveyArmy();
        require(!bot->findFrontBatteryPlaceLocation().isValid(),
            "Clearance fixture has free ground before the end-to-end case");
        const int before=ownZones();
        const Coord reserved=orderBattery();
        require(ownZones()==before,"Accepting the order already cost the city a lot");
        auto* lot=dynamic_cast<ZoneStructure*>(
            currentGameMap->getTile(reserved)->getNonInfantryGroundObject());
        require(lot,"The reserved emplacement site is not a standing lot");
        const Uint32 lotID=lot->getObjectID();

        const StructureBase* placed=nullptr;
        int frames=0, lotGoneAt=-1;
        for(;frames<9000 && !placed;++frames) {
            if(house->getCredits()<kFunded)
                house->startingCredits+=kFunded-house->getCredits();
            house->producedPower=20000;
            game.updateGameState();
            if(lotGoneAt<0 && !liveZone(lotID))lotGoneAt=frames;
            const auto* standing=dynamic_cast<const StructureBase*>(
                currentGameMap->getTile(reserved)->getNonInfantryGroundObject());
            if(standing && standing->getItemID()==Structure_RocketTurret
                && standing->getOwner()==house)placed=standing;
        }
        SDL_Log("BATTERY_CLEARANCE_DIAG: %s frames=%d lot_gone_at=%d placed=%d zones %d->%d turrets=%d",
            label,frames,lotGoneAt,int(placed!=nullptr),before,ownZones(),
            house->getNumItems(Structure_RocketTurret));
        require(placed,"No rocket turret ever stood on the ground the city gave up");
        require(lotGoneAt>=0,"The emplacement appeared without the lot ever being displaced");
        require(ownZones()<=before-1,"Exactly one lot was not displaced for the emplacement");
        return reserved;
    };
    const Coord concreteSite=runToTurret("concrete_on",true);

    // ---- 10. No churn: the planner does not rebuild what was just taken ---
    {
        bot->planningBuilder=main.yard->getObjectID();
        bot->clearPlacementCache();
        const Coord replacement=bot->findPlaceLocation(Structure_ZoneResidential);
        SDL_Log("BATTERY_CLEARANCE_DIAG: replacement=%d,%d valid=%d",
            replacement.x,replacement.y,replacement.isValid());
        require(!replacement.isValid()
                || !CityPlacementPolicy::overlaps(replacement.x,replacement.y,2,2,
                       concreteSite.x,concreteSite.y,1,1),
            "The city planner immediately rebuilt a lot over the emplacement it just paid for");
    }

    // ---- 11. End to end on real frames, concrete not required -------------
    runToTurret("concrete_off",false);

    // ---- 12. One lot per construction pass, across every yard -------------
    //
    // Both yards hold a clearance reservation from an earlier pass, so the
    // bound cannot come from counting orders.
    {
        resetCity(false);
        freshPass(kFunded);
        bot->lastSurvey=bot->surveyArmy();
        readyYard(remote.yard);
        const Coord mainSite=clearanceSiteFor(main.yard);
        const Coord remoteSite=clearanceSiteFor(remote.yard);
        require(mainSite.isValid() && remoteSite.isValid() && mainSite!=remoteSite,
            "Clearance fixture needs a distinct pending clearance at each yard");
        bot->reservedStructures[main.yard->getObjectID()]={Structure_RocketTurret,mainSite};
        bot->reservedStructures[remote.yard->getObjectID()]={Structure_RocketTurret,remoteSite};
        for(BuilderBase* yard:{main.yard,remote.yard})yard->doProduceItem(Structure_RocketTurret);
        waitForMaterial({main.yard,remote.yard});
        const int before=ownZones();
        bot->batteryClearancesThisPass=0;
        int committed=0;
        for(BuilderBase* yard:{main.yard,remote.yard}) {
            bot->planningBuilder=yard->getObjectID();
            bot->clearPlacementCache(false,true);
            auto& planned=bot->builderPlaceLocations[yard->getObjectID()];
            planned.clear();
            planned.push_back(bot->reservedStructures.at(yard->getObjectID()).location);
            bool deferred=false;
            if(bot->commitBatteryClearance(yard,Structure_RocketTurret,planned,kFunded,0,0,&deferred)) {
                ++committed;
                const Coord placed=planned.front();
                require(static_cast<ConstructionYard*>(yard)->doPlaceStructure(placed.x,placed.y),
                    "A reclaimed lot did not receive its finished turret immediately");
            } else {
                require(deferred,"The second ready yard did not retain its material for the next pass");
                require(yard->isWaitingToPlace(),"The per-pass limit lost the second yard's material");
            }
        }
        SDL_Log("BATTERY_CLEARANCE_DIAG: per_pass committed=%d zones %d->%d",
            committed,before,ownZones());
        require(committed==FrontBatteryPolicy::kClearanceLotsPerPass,
            "A single construction pass displaced more than one lot across the yards");
        require(ownZones()==before-FrontBatteryPolicy::kClearanceLotsPerPass,
            "A single construction pass displaced more than one lot across the yards");
        bot->reservedStructures.clear();
    }

    // A queued turret's price is already deducted from the new-order budget.
    // Exact funding must work, while even a one-credit raw shortfall must keep
    // the lot standing and retain the completed slab for a later pass.
    {
        resetCity(true);
        freshPass(kFunded);
        bot->lastSurvey=bot->surveyArmy();
        const Coord fundedSite=orderBattery();
        const int before=ownZones();
        auto& planned=bot->builderPlaceLocations[main.yard->getObjectID()];
        bool deferred=false;
        require(!bot->commitBatteryClearance(main.yard,Structure_Slab1,planned,0,0,0,&deferred)
                && ownZones()==before,"An unfinished foundation displaced its lot");
        waitForMaterial({main.yard});
        bot->planningBuilder=main.yard->getObjectID();
        bot->clearPlacementCache();
        require(main.yard->getCurrentProducedItem()==Structure_Slab1,
            "The funding check expected a completed foundation first");
        auto* blocker=house->createUnit(Unit_Soldier);
        require(blocker,"The ready-material check could not create a blocker");
        blocker->deploy(fundedSite+Coord(0,1));
        bot->clearPlacementCache();
        require(!bot->commitBatteryClearance(main.yard,Structure_Slab1,planned,0,0,0,&deferred)
                && ownZones()==before,"Finished material bypassed changed lot occupancy");
        blocker->destroy();
        house->ai=false;
        bot->clearPlacementCache();
        require(!bot->commitBatteryClearance(main.yard,Structure_Slab1,planned,0,0,0,&deferred)
                && ownZones()==before,"Finished material bypassed shared-human authority");
        house->ai=true;
        const auto oldOptions=game.getGameInitSettings().getGameOptions();
        auto powerOptions=oldOptions; powerOptions.rocketTurretsNeedPower=true;
        game.gameInitSettings.setGameOptions(powerOptions);
        house->producedPower=0;
        bot->clearPlacementCache();
        require(!bot->commitBatteryClearance(main.yard,Structure_Slab1,planned,0,0,0,&deferred)
                && ownZones()==before,"Finished material bypassed lost power headroom");
        game.gameInitSettings.setGameOptions(oldOptions);
        house->producedPower=20000;
        bot->clearPlacementCache();
        require(!bot->commitBatteryClearance(main.yard,Structure_Slab1,planned,0,0,1,&deferred)
                && deferred && ownZones()==before && main.yard->isWaitingToPlace(),
            "A one-credit commitment shortfall displaced a lot or lost its material");
        require(bot->commitBatteryClearance(main.yard,Structure_Slab1,planned,0,0,0,&deferred),
            "Exactly funded construction required the turret price twice");
        require(static_cast<ConstructionYard*>(main.yard)->doPlaceStructure(fundedSite.x,fundedSite.y)
                && currentGameMap->getTile(fundedSite)->hasPreparedFoundation(),
            "Funded clearance did not lay its finished concrete immediately");
        require(ownZones()==before-1,"Exact funding displaced an unexpected number of lots");
        SDL_Log("BATTERY_CLEARANCE_DIAG: funding shortfall=protected exact=placed");
    }

    // ---- 13. Free ground leaves every lot alone ---------------------------
    {
        resetCity(false);
        const Coord free=main.at+Coord(-3,0);
        currentGameMap->getTile(free)->setType(Terrain_Rock);
        currentGameMap->getTile(free)->setOwner(house->getHouseID());
        const int before=ownZones();
        freshPass(kFunded);
        bot->lastSurvey=bot->surveyArmy();
        require(bot->findFrontBatteryPlaceLocation().isValid(),
            "Clearance fixture failed to open a free emplacement site");
        require(!bot->findBatteryClearanceSite().isValid(),
            "A lot was offered for displacement although free ground existed");
        bot->build(0);
        require(ownZones()==before,"A lot was displaced although free ground existed");
    }

    // ---- 14. No economy, no money, an unready yard, no observed enemy -----
    {
        resetCity(false);
        const int before=ownZones();
        auto* factory=[&]()->StructureBase*{
            for(auto* structure:structureList)
                if(structure->getOwner()==house && structure->getItemID()==Structure_HeavyFactory
                    && structure->getHealth()>0)return structure;
            return nullptr;
        }();
        require(factory,"Clearance fixture lost its heavy factory");
        const Coord factoryAt=factory->getLocation();
        factory->destroy();
        freshPass(kFunded);
        bot->lastSurvey=bot->surveyArmy();
        require(!bot->findBatteryClearanceSite().isValid(),
            "A base without the economy that pays for a battery still displaced a lot");
        bot->build(0);
        require(ownZones()==before,"A lot was displaced without the economy gate");
        put(house,Structure_HeavyFactory,factoryAt);
        sandEverythingUnbuilt();

        freshPass(10);
        bot->lastSurvey=bot->surveyArmy();
        bot->build(0);
        require(ownZones()==before,"A lot was displaced although the emplacement was unaffordable");

        freshPass(kFunded);
        bot->lastSurvey=bot->surveyArmy();
        main.yard->curUpgradeLev=0;main.yard->updateBuildList();
        bot->build(0);
        require(ownZones()==before,"A lot was displaced although the yard was not upgraded");
        readyYard(main.yard);

        enemyBase->destroy();remoteEnemyBase->destroy();
        bot->visibleEnemyBases.clear();
        bot->recentStructureLosses.clear();
        bot->dangerUpdated=std::numeric_limits<Uint32>::max();
        bot->refreshTacticalDanger();
        freshPass(kFunded);
        bot->lastSurvey=bot->surveyArmy();
        bot->visibleEnemyBases.clear();
        bot->recentStructureLosses.clear();
        bot->clearPlacementCache();
        require(!bot->findBatteryClearanceSite().isValid(),
            "A lot was displaced with no observed enemy approach at all");
        bot->build(0);
        require(ownZones()==before,"A lot was displaced with no observed enemy approach at all");
    }

    // ---- 15. All three zone types are reachable ---------------------------
    //
    // The lot that is actually chosen depends on where the belt wants to stand,
    // so what is checked is that none of the three is excluded by type.
    {
        resetCity(false);
        freshPass(kFunded);
        bot->lastSurvey=bot->surveyArmy();
        const Coord liveAnchor=bot->batteryAnchor();
        std::set<Uint32> displaceable;
        for(auto* structure:structureList) {
            const auto* zone=dynamic_cast<const ZoneStructure*>(structure);
            if(!zone || zone->getOwner()!=house)continue;
            std::vector<Uint32> ignored;
            if(bot->batteryClearanceZones(zone->getLocation(),ignored,liveAnchor))
                displaceable.insert(zone->getItemID());
        }
        SDL_Log("BATTERY_CLEARANCE_DIAG: displaceable_types=%zu",displaceable.size());
        for(Uint32 item:zoneItems)
            require(displaceable.count(item),"A zone type was excluded from clearance by type");
    }

    // Deliberately not a CAMPAIGN_BALANCE_RESULT line: the runner requires
    // exactly one of those per process and the recovery probe owns it.
    SDL_Log("BATTERY_CLEARANCE_RESULT: outcome=passed");
}
