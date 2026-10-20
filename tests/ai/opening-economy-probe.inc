#include <structures/Refinery.h>
#include <units/Harvester.h>
#include <players/QuantBotBuildPolicy.h>
#include <players/QuantBotPowerInvestmentPolicy.h>
#include <set>

// Drive real build decisions on All against Atreides, with its empty market.
void runOpeningEconomyProbe(Game& game) {
    auto require=[](bool ok,const char* why) {
        if(!ok) {SDL_Log("OPENING_ECONOMY_FAIL: %s",why);throw std::runtime_error(why);}
    };
    auto* house=game.getHouse(HOUSE_ATREIDES);
    QuantBot* bot=nullptr;BuilderBase* yard=nullptr;
    for(const auto& player:house->getPlayerList())
        if(auto* candidate=dynamic_cast<QuantBot*>(player.get()))bot=candidate;
    for(auto* structure:structureList)
        if(structure->getOwner()==house && structure->getItemID()==Structure_ConstructionYard)
            yard=static_cast<BuilderBase*>(structure);
    require(bot && yard,"Missing opening controller/yard");
    // This fixture compares economic tech routes in isolation; proactive
    // rocket technology/coverage is exercised by the defence fixtures.
    if(game.isCitySimEnabled()) {
        game.objectData.data[Structure_RocketTurret][HOUSE_ATREIDES].enabled=false;
        yard->updateBuildList();
    }
    game.gameCycleCount=123;bot->update();
    require(house->getNumItems(Structure_HeavyFactory)==0,"Opening must start without heavy production");
    for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x)
        if(!currentGameMap->getTile(x,y)->hasAStructure())currentGameMap->getTile(x,y)->setType(Terrain_Slab);
    auto clearOrders=[&] {
        for(auto* structure:structureList)if(structure->getOwner()==house && structure->isABuilder()) {
            auto* builder=static_cast<BuilderBase*>(structure);
            while(builder->getProductionQueueSize())builder->doCancelItem(builder->getCurrentProducedItem());
        }
        yard->upgrading=false;yard->setHealth(yard->getMaxHealth());
        bot->builderPlaceLocations.clear();bot->reservedStructures.clear();bot->clearPlacementCache();
    };
    auto place=[&](Uint32 item) {
        clearOrders();const auto site=bot->findPlaceLocation(item);
        require(site.isValid(),"No fixture placement");
        require(house->placeStructure(NONE_ID,item,site.x,site.y,true,true),"Fixture placement failed");
        yard->updateBuildList();
    };
    auto build=[&](int credits) {
        clearOrders();house->storedCredits=0;house->cityCredits=0;house->startingCredits=credits; // Explicit scenario funding, not an over-capacity spice deposit.
        house->producedPower=10000;bot->lastCalculatedSpice=1300000;
        bot->build(5100);
        for(const auto& order:yard->currentProductionQueue)
            if(order.itemID!=Structure_Slab1 && order.itemID!=Structure_Slab4 && order.itemID!=Structure_Road)
                return order.itemID;
        return Uint32(NONE_ID);
    };
    auto storageBuild=[&](int capacity,int stored,int city) {
        clearOrders();
        house->capacity=capacity;house->startingCredits=0;
        house->storedCredits=stored;house->cityCredits=city;
        house->producedPower=10000;bot->lastCalculatedSpice=1300000;
        bot->build(5100);
        for(const auto& order:yard->currentProductionQueue)
            if(order.itemID!=Structure_Slab1 && order.itemID!=Structure_Slab4 && order.itemID!=Structure_Road)
                return order.itemID;
        return Uint32(NONE_ID);
    };
    // Foundation auditing. The yard queues its slabs in plan order and then the
    // building, and records one planned location for each, so the two lists
    // line up and describe exactly what the bot intends to prepare.
    struct FoundationAudit {
        Uint32 item=NONE_ID;
        Coord site=Coord::Invalid();
        std::vector<std::pair<Uint32,Coord>> slabs;
    };
    auto auditFoundation=[&]() {
        FoundationAudit audit;
        const auto& planned=bot->builderPlaceLocations[yard->getObjectID()];
        auto site=planned.begin();
        for(const auto& order:yard->currentProductionQueue) {
            if(site==planned.end())break;
            if(order.itemID==Structure_Slab1 || order.itemID==Structure_Slab4)
                audit.slabs.emplace_back(order.itemID,*site);
            else {audit.item=order.itemID;audit.site=*site;}
            ++site;
        }
        return audit;
    };
    auto slabCovers=[&](const FoundationAudit& audit,int x,int y) {
        for(const auto& slab:audit.slabs) {
            const int span=slab.first==Structure_Slab4?2:1;
            if(x>=slab.second.x && x<slab.second.x+span
                && y>=slab.second.y && y<slab.second.y+span)return true;
        }
        return false;
    };
    // Every tile a slab would pave has to be legal ground, and a road is never
    // erased: House::placeStructure clears the road flag of whatever it paves.
    auto requireLegalSlabs=[&](const FoundationAudit& audit) {
        for(const auto& slab:audit.slabs) {
            const int span=slab.first==Structure_Slab4?2:1;
            for(int dx=0;dx<span;++dx)for(int dy=0;dy<span;++dy) {
                const int x=slab.second.x+dx,y=slab.second.y+dy;
                require(currentGameMap->tileExists(x,y),"Concrete was planned off the map");
                auto* tile=currentGameMap->getTile(x,y);
                require(!tile->isRoad(),"Concrete would erase an existing road");
                require(tile->isRock() && !tile->isMountain(),
                    "Concrete was planned onto ground the engine cannot pave");
                require(!tile->hasAStructure(),"Concrete was planned under a standing building");
            }
        }
    };
    auto requireFullyFounded=[&](const FoundationAudit& audit) {
        require(audit.item!=NONE_ID && audit.site.isValid(),"Founded order lost its building");
        const auto size=getStructureSize(audit.item);
        for(int dx=0;dx<size.x;++dx)for(int dy=0;dy<size.y;++dy) {
            const int x=audit.site.x+dx,y=audit.site.y+dy;
            require(currentGameMap->tileExists(x,y),"Building site left the map");
            require(currentGameMap->getTile(x,y)->hasPreparedFoundation() || slabCovers(audit,x,y),
                "An ordinary building was ordered onto ground no slab prepares");
        }
        requireLegalSlabs(audit);
    };
    auto countSlabs=[&](Uint32 slab) {
        int count=0;
        for(const auto& order:yard->currentProductionQueue)count+=order.itemID==slab;
        return count;
    };
    auto stripConcrete=[&] {
        for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x) {
            auto* tile=currentGameMap->getTile(x,y);
            if(!tile->hasAStructure() && tile->isConcrete())tile->setType(Terrain_Rock);
        }
    };
    auto pavePlain=[&] {
        for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x)
            if(!currentGameMap->getTile(x,y)->hasAStructure())currentGameMap->getTile(x,y)->setType(Terrain_Slab);
    };

    place(Structure_WindTrap);place(Structure_Refinery);
    // Real queue checks while the opening still has an unambiguous productive
    // next building. Isolate policies from the later mature-base fixtures.
    {
        const bool savedConcrete=game.gameInitSettings.gameOptions.concreteRequired;
        const auto savedDifficulty=bot->difficulty;
        const auto savedUpgrade=yard->curUpgradeLev;
        const auto slab4=game.objectData.data[Structure_Slab4][HOUSE_ATREIDES];
        if (house->getNumItems(Unit_Harvester)==0) {
            auto* income=house->createUnit(Unit_Harvester,true);
            require(income!=nullptr,"Could not create opening income worker");
        }
        game.gameInitSettings.gameOptions.concreteRequired=true;
        // Bulk concrete comes before any construction, at every difficulty.
        for (auto difficulty : {QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                               QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            bot->difficulty=difficulty;
            yard->curUpgradeLev=0;yard->updateBuildList();
            const auto item=build(100000);
            require(yard->isUpgrading(),"Funded opening did not upgrade the yard before optional infrastructure");
            require(item==NONE_ID,"Optional construction ran ahead of the affordable bulk-concrete upgrade");
        }
        // Saving for the upgrade owns the pass too: no cheap building first.
        {
            bot->difficulty=QuantBot::Difficulty::Hard;
            yard->curUpgradeLev=0;yard->updateBuildList();
            const int cost=yard->getUpgradeCost();
            require(cost>1,"Fixture needs a priced yard upgrade");
            const auto item=build(cost-1);
            require(!yard->isUpgrading(),"Yard upgraded without the credits for it");
            require(item==NONE_ID,"A yard saving for bulk concrete bought something else first");
        }
        // A tree that cannot reach the 2x2 slab never holds the yard; it keeps
        // founding with singles instead.
        {
            game.objectData.data[Structure_Slab4][HOUSE_ATREIDES].enabled=false;
            yard->curUpgradeLev=0;yard->updateBuildList();
            stripConcrete();
            const auto item=build(100000);
            require(!yard->isUpgrading(),"An unreachable 2x2 slab still forced a yard upgrade");
            require(item!=NONE_ID,"Low-tech foundation policy stopped construction altogether");
            require(countSlabs(Structure_Slab4)==0,"Disabled 2x2 concrete was ordered");
            require(countSlabs(Structure_Slab1)>0,"Low-tech opening did not fall back to single slabs");
            requireFullyFounded(auditFoundation());
            game.objectData.data[Structure_Slab4][HOUSE_ATREIDES]=slab4;
            yard->updateBuildList();
        }
        // Concrete switched off: no upgrade, no slabs, construction unchanged.
        {
            game.gameInitSettings.gameOptions.concreteRequired=false;
            yard->curUpgradeLev=0;yard->updateBuildList();
            stripConcrete();
            const auto item=build(100000);
            require(!yard->isUpgrading(),"Disabled concrete still bought the bulk-slab upgrade");
            require(item!=NONE_ID,"Disabled concrete stopped construction");
            require(countSlabs(Structure_Slab1)==0 && countSlabs(Structure_Slab4)==0,
                "Disabled concrete entered the AI queue");
            game.gameInitSettings.gameOptions.concreteRequired=true;
        }
        // The rest of the opening tests exercise build selection after the
        // new foundation upgrade, so give them the unlocked level explicitly.
        clearOrders();yard->curUpgradeLev=std::max<int>(savedUpgrade,slab4.upgradeLevel);
        yard->updateBuildList();
        // Every ordinary building is prepared in full, with and without the
        // 2x2 slab, at every difficulty. There is no urgent or passive escape:
        // the base has no spare cash and is short of power in the third case.
        auto queueCheck=[&](bool bulk,int credits,int power) {
            game.objectData.data[Structure_Slab4][HOUSE_ATREIDES].enabled=bulk;
            yard->updateBuildList();
            stripConcrete();
            clearOrders();
            house->storedCredits=0;house->cityCredits=0;house->startingCredits=credits;
            house->producedPower=power;bot->lastCalculatedSpice=1300000;
            bot->build(5100);
            const auto audit=auditFoundation();
            if(credits>=100000 && power>=house->getPowerRequirement())
                require(audit.item!=NONE_ID,"Funded opening did not order a founded building");
            if(audit.item==NONE_ID) {
                require(countSlabs(Structure_Slab1)==0 && countSlabs(Structure_Slab4)==0,
                    "Concrete was ordered for a building the bot did not order");
                return;
            }
            requireFullyFounded(audit);
            if(!bulk)require(countSlabs(Structure_Slab4)==0,"Disabled 2x2 concrete was ordered");
            const auto size=getStructureSize(audit.item);
            if(bulk && size.x>=2 && size.y>=2)
                require(countSlabs(Structure_Slab4)>0,"Upgraded yard failed to use 2x2 concrete");
        };
        for (auto difficulty : {QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                               QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            bot->difficulty=difficulty;
            queueCheck(false,100000,10000);queueCheck(true,100000,10000);
            queueCheck(true,900,10000);        // no spare cash: still founded or deferred
            queueCheck(true,100000,0);         // power short: no urgent bare placement
        }
        // A road across the footprint is prepared ground that survives.
        {
            bot->difficulty=QuantBot::Difficulty::Hard;
            game.objectData.data[Structure_Slab4][HOUSE_ATREIDES].enabled=true;
            yard->updateBuildList();
            stripConcrete();
            const auto site=bot->findPlaceLocation(Structure_LightFactory);
            require(site.isValid(),"No fixture site for the road foundation check");
            currentGameMap->getTile(site.x,site.y)->setRoad(true);
            clearOrders();
            house->storedCredits=0;house->cityCredits=0;house->startingCredits=100000;
            house->producedPower=10000;bot->lastCalculatedSpice=1300000;
            bot->build(5100);
            const auto audit=auditFoundation();
            if(audit.item!=NONE_ID)requireFullyFounded(audit);
            require(currentGameMap->getTile(site.x,site.y)->isRoad(),
                "A planned slab was allowed to erase an existing road");
            currentGameMap->getTile(site.x,site.y)->setRoad(false);
        }
        // Legacy non-city campaigns use a separate reconstruction branch.
        // It must share both the early upgrade and the foundation queue rules.
        if (!game.isCitySimEnabled()) {
            const auto mode=bot->gameMode;
            const auto type=game.gameType;
            const int originalLight=bot->initialItemCount[Structure_LightFactory];
            bot->gameMode=QuantBot::GameMode::Campaign;game.gameType=GameType::Campaign;
            bot->initialItemCount[Structure_LightFactory]=1;
            for (auto difficulty : {QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                                   QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
                bot->difficulty=difficulty;
                game.gameInitSettings.gameOptions.concreteRequired=true;
                yard->curUpgradeLev=0;yard->updateBuildList();
                const auto first=build(100000);
                require(yard->isUpgrading(),"Campaign rebuild bypassed the early concrete upgrade");
                require(first==NONE_ID,"Campaign rebuild ran ahead of the affordable concrete upgrade");
                clearOrders();yard->curUpgradeLev=std::max<int>(savedUpgrade,slab4.upgradeLevel);
                yard->updateBuildList();
                queueCheck(false,100000,10000);queueCheck(true,100000,10000);
                queueCheck(true,900,10000);queueCheck(true,100000,0);
            }
            clearOrders();bot->gameMode=mode;game.gameType=type;
            bot->initialItemCount[Structure_LightFactory]=originalLight;
        }
        if (game.isCitySimEnabled()) {
            bot->clearPlacementCache();
            const auto oldSite=bot->findPlaceLocation(Structure_ZoneResidential);
            require(oldSite.isValid(),"Missing city-zone foundation site");
            currentGameMap->getTile(oldSite)->setType(Terrain_Sand);
            bot->clearPlacementCache();
            const auto site=bot->findPlaceLocation(Structure_ZoneResidential);
            require(site.isValid(),"No alternative prepared-ground city site");
            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
                require(currentGameMap->getTile(site.x+dx,site.y+dy)->isRock(),
                    "QuantBot selected sand for a required concrete foundation");
        }
        // Raw power matters even in Vanilla. At 50 credits and exactly half
        // health neither the rich nor emergency-health repair rule applies.
        StructureBase* wind=nullptr;
        for(auto* structure:structureList)
            if(structure->getOwner()==house && structure->getItemID()==Structure_WindTrap)wind=structure;
        require(wind!=nullptr,"Missing windtrap repair fixture");
        for (auto difficulty : {QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                               QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            clearOrders();bot->difficulty=difficulty;
            wind->setHealth(FixPoint(wind->getMaxHealth())/2);wind->repairing=false;
            house->storedCredits=0;house->cityCredits=0;house->startingCredits=50;
            house->producedPower=0;
            bot->build(5100);
            require(wind->isRepairing(),"Damaged windtrap was not repaired during a power shortage");
            wind->setHealth(wind->getMaxHealth());wind->repairing=false;
        }
        clearOrders();bot->difficulty=savedDifficulty;
        game.gameInitSettings.gameOptions.concreteRequired=savedConcrete;
        game.objectData.data[Structure_Slab4][HOUSE_ATREIDES]=slab4;
        yard->updateBuildList();
        for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x)
            if(!currentGameMap->getTile(x,y)->hasAStructure())currentGameMap->getTile(x,y)->setType(Terrain_Slab);
        SDL_Log("FOUNDATION_POLICY_PASS: all difficulties, early yard upgrade, singles, bulk and concrete disabled");
    }
    require(build(100000)==Structure_LightFactory,"Rich empty-market opening failed to unlock factory before extra refineries");
    house->getChoam().addItem(Unit_Harvester,0); // Supported, temporarily sold out.
    require(build(1800)==Structure_Refinery,"Poor opening lost the supported Starport economy path");
    require(build(100000)==Structure_LightFactory,"Rich opening waited for refineries despite ample operating funds");
    place(Structure_LightFactory);
    require(build(100000)==Structure_Radar,"Rich opening skipped its heavy-factory prerequisite");
    place(Structure_Radar);
    require(build(100000)==Structure_HeavyFactory,"Rich opening built repair/Starport before first heavy factory");
    // The heavy factory is 3x2: real terrain, real placement. Two 2x2 slabs,
    // one of them laying new concrete beside the footprint, rather than one
    // 2x2 and two singles or one that re-paves the slab before it.
    {
        const bool savedConcrete=game.gameInitSettings.gameOptions.concreteRequired;
        const auto savedUpgrade=yard->curUpgradeLev;
        game.gameInitSettings.gameOptions.concreteRequired=true;
        yard->curUpgradeLev=std::max<Uint8>(savedUpgrade,
            static_cast<Uint8>(game.objectData.data[Structure_Slab4][HOUSE_ATREIDES].upgradeLevel));
        yard->updateBuildList();
        stripConcrete();
        const auto item=build(100000);
        require(item==Structure_HeavyFactory,"Founded 3x2 fixture did not order the heavy factory");
        const auto audit=auditFoundation();
        requireFullyFounded(audit);
        const auto size=getStructureSize(item);
        require(size.x==3 && size.y==2,"Fixture expected a 3x2 factory footprint");
        int bulk=0,singles=0,outside=0;
        for(const auto& slab:audit.slabs) {
            const int span=slab.first==Structure_Slab4?2:1;
            (slab.first==Structure_Slab4?bulk:singles)++;
            for(int dx=0;dx<span;++dx)for(int dy=0;dy<span;++dy) {
                const int x=slab.second.x+dx,y=slab.second.y+dy;
                if(x<audit.site.x || x>=audit.site.x+size.x
                    || y<audit.site.y || y>=audit.site.y+size.y)++outside;
            }
        }
        SDL_Log("FOUNDATION_3x2: site=%d,%d bulk=%d singles=%d outside=%d",
            audit.site.x,audit.site.y,bulk,singles,outside);
        require(bulk==2 && singles==0,"A 3x2 footprint did not use two 2x2 slabs");
        require(outside==2,"The 3x2 residual strip did not lay its extra concrete beside the footprint");
        game.gameInitSettings.gameOptions.concreteRequired=savedConcrete;
        yard->curUpgradeLev=savedUpgrade;
        yard->updateBuildList();
        pavePlain();
        clearOrders();
    }
    // The final coverage gate. Foundations are still planned and ordered
    // first, but a building that is already finished is never thrown away for
    // the sake of its concrete: whatever happened to the slabs, it goes up.
    // The bot prefers a prepared footprint nearby and otherwise accepts the
    // engine's own foundation damage on the site it planned.
    {
        const bool savedConcrete=game.gameInitSettings.gameOptions.concreteRequired;
        game.gameInitSettings.gameOptions.concreteRequired=true;
        yard->updateBuildList();
        stripConcrete();
        clearOrders();
        const Uint32 bare=Structure_Silo;
        bot->clearPlacementCache();
        const auto site=bot->findPlaceLocation(bare);
        require(site.isValid(),"No fixture site for the bare-placement guard");
        yard->doProduceItem(bare);
        require(yard->getProductionQueueSize()>0,"Fixture could not queue the guarded building");
        yard->currentProducedItem=bare;
        yard->productionProgress=game.objectData.data[bare][HOUSE_ATREIDES].price;
        require(yard->isWaitingToPlace(),"Fixture could not finish the guarded building");
        bot->builderPlaceLocations[yard->getObjectID()].clear();
        bot->builderPlaceLocations[yard->getObjectID()].push_back(site);
        bot->reservedStructures[yard->getObjectID()]={bare,site};
        const int before=house->getNumItems(bare);
        house->storedCredits=0;house->cityCredits=0;house->startingCredits=100000;
        house->producedPower=10000;bot->lastCalculatedSpice=1300000;
        bot->build(5100);
        require(house->getNumItems(bare)==before+1,
            "A finished building was cancelled instead of placed because its concrete was missing");
        require(yard->getProductionQueueSize()==0,
            "The unprepared building was neither placed nor refunded");
        // No synthetic damage and no free concrete: the engine's own placement
        // rule decides what standing on bare rock costs.
        const StructureBase* placed=nullptr;
        for(auto* structure:structureList)
            if(structure->getOwner()==house && structure->getItemID()==bare
                && structure->getLocation()==site)placed=structure;
        require(placed!=nullptr,"The kept building was not placed at its planned site");
        require(placed->foundationDegrades && placed->getHealth()<placed->getMaxHealth(),
            "The bare building did not receive normal engine foundation damage");
        clearOrders();
        game.gameInitSettings.gameOptions.concreteRequired=savedConcrete;
        yard->updateBuildList();
        pavePlain();
        SDL_Log("FOUNDATION_GUARD_PASS: a finished building is placed, never cancelled for missing concrete");
    }
    // The same building with a legal prepared footprint beside its planned one
    // uses the prepared ground instead of taking the foundation damage.
    {
        const bool savedConcrete=game.gameInitSettings.gameOptions.concreteRequired;
        game.gameInitSettings.gameOptions.concreteRequired=true;
        yard->updateBuildList();
        stripConcrete();
        clearOrders();
        const Uint32 kept=Structure_Silo;
        bot->clearPlacementCache();
        const auto site=bot->findPlaceLocation(kept);
        require(site.isValid(),"No fixture site for the prepared-preference check");
        const auto span=getStructureSize(kept);
        // Prepared ground everywhere except the footprint the bot planned.
        pavePlain();
        for(int dx=0;dx<span.x;++dx)for(int dy=0;dy<span.y;++dy)
            currentGameMap->getTile(site.x+dx,site.y+dy)->setType(Terrain_Rock);
        yard->doProduceItem(kept);
        yard->currentProducedItem=kept;
        yard->productionProgress=game.objectData.data[kept][HOUSE_ATREIDES].price;
        require(yard->isWaitingToPlace(),"Fixture could not finish the prepared-preference building");
        bot->builderPlaceLocations[yard->getObjectID()].clear();
        bot->builderPlaceLocations[yard->getObjectID()].push_back(site);
        bot->reservedStructures[yard->getObjectID()]={kept,site};
        const int before=house->getNumItems(kept);
        std::set<Uint32> existing;
        for(auto* structure:structureList)
            if(structure->getItemID()==kept)existing.insert(structure->getObjectID());
        house->storedCredits=0;house->cityCredits=0;house->startingCredits=100000;
        house->producedPower=10000;bot->lastCalculatedSpice=1300000;
        bot->build(5100);
        require(house->getNumItems(kept)==before+1,"The prepared-preference building was lost");
        const StructureBase* placed=nullptr;
        for(auto* structure:structureList)
            if(structure->getOwner()==house && structure->getItemID()==kept
                && !existing.count(structure->getObjectID()))placed=structure;
        require(placed!=nullptr && placed->getLocation()!=site,
            "A prepared footprint beside the planned one was not preferred");
        // Placement consumes the tile's slab state; the structure remembers it.
        require(!placed->foundationDegrades && placed->getHealth()==placed->getMaxHealth(),
            "The preferred alternative site did not provide a full foundation");
        SDL_Log("FOUNDATION_PREFERENCE_PASS: prepared footprint at %d,%d used instead of %d,%d",
            placed->getLocation().x,placed->getLocation().y,site.x,site.y);
        clearOrders();
        game.gameInitSettings.gameOptions.concreteRequired=savedConcrete;
        yard->updateBuildList();
        pavePlain();
    }
    place(Structure_HeavyFactory);
    house->getChoam().availableItems.clear();
    // With prerequisites built and spending otherwise exhausted, the fallback
    // must not construct the absent market merely because a heavy line exists.
    for(Uint32 item:{Structure_Radar,Structure_RepairYard,Structure_HighTechFactory,Structure_IX,Structure_Palace})
        if(!house->getNumItems(item))place(item);
    while(house->getNumItems(Structure_Refinery)<4)place(Structure_Refinery);
    bot->militaryValueLimit=5100;
    require(build(1800)!=Structure_StarPort,"Heavy factory bypassed absent-market check");
    house->getChoam().addItem(Unit_Harvester,0);
    require(build(1800)==Structure_StarPort,"Sold-out supported imports incorrectly treated as disabled");
    auto& worker=game.objectData.data[Unit_Harvester][HOUSE_ATREIDES];
    const bool enabled=worker.enabled;worker.enabled=false;
    require(build(1800)!=Structure_StarPort,"All disabled imports still caused Starport construction");
    worker.enabled=enabled;clearOrders();

    const auto probeDifficulty=bot->difficulty;
    // Condition policy across the whole base. Three quarters of health is above
    // the survival rule and below full, so only the condition policy decides.
    // Whatever the engine repairs for free, and whatever turns condition into
    // output, production or land value, is repaired on fifty credits; the rest
    // waits for a comfortable treasury. City mode maintains the whole colony.
    auto repairSweep=[&](int credits) {
        clearOrders();
        std::vector<StructureBase*> damaged;
        for(auto* structure:structureList) {
            if(structure->getOwner()!=house || structure->getHealth()<=0)continue;
            structure->setHealth(FixPoint(structure->getMaxHealth())*3/4);
            structure->repairing=false;
            damaged.push_back(structure);
        }
        require(!damaged.empty(),"No structures to exercise the repair policy");
        house->storedCredits=0;house->cityCredits=0;house->startingCredits=credits;
        house->producedPower=10000;bot->lastCalculatedSpice=1300000;
        bot->build(5100);
        int maintainedCount=0,passiveCount=0;
        for(auto* structure:damaged) {
            const bool maintained=QuantBotBuildPolicy::repairMaintainsValue(structure->getItemID(),
                game.isCitySimEnabled(),structure->getMaxHealth(),
                game.objectData.data[structure->getItemID()][structure->getOriginalHouseID()].price);
            maintainedCount+=maintained;passiveCount+=!maintained;
            if(maintained || QuantBotBuildPolicy::repairWhenWealthy(credits))
                require(structure->isRepairing(),
                    "A building whose condition is worth credits was left damaged");
            else require(!structure->isRepairing(),
                "A passive building was repaired without the spare cash for it");
            structure->setHealth(structure->getMaxHealth());structure->repairing=false;
        }
        require(maintainedCount>0,"Fixture has no condition-driven buildings");
        if(!game.isCitySimEnabled())require(passiveCount>0,"Fixture has no passive buildings");
        else require(passiveCount==0,"City mode left part of the colony unmaintained");
        SDL_Log("REPAIR_POLICY: credits=%d maintained=%d passive=%d",credits,maintainedCount,passiveCount);
    };
    for(auto difficulty:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
            QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
        bot->difficulty=difficulty;
        repairSweep(50);      // productive, free and city repairs on small change
        repairSweep(100000);  // a wealthy base maintains everything it owns
    }
    bot->difficulty=probeDifficulty;
    clearOrders();
    SDL_Log("REPAIR_POLICY_PASS: condition-driven, free and city repairs at low cash; passive only when wealthy");

    // Generation outside the city simulation. Vanilla's hasPower() answers true
    // whatever the meters say, so only the economic gate can close a raw
    // deficit -- and merely being powered is not the target. A base at 1800
    // against 1840 is one building away from the next shortage, so an
    // established base invests in a reserve. Other mods keep their functional
    // power rules and are skipped.
    if(!game.isCitySimEnabled() && !house->isPowerRequired()) {
        const int windOutput=-game.objectData.data[Structure_WindTrap][HOUSE_ATREIDES].power;
        require(windOutput>0,"Fixture needs a windtrap that produces power");
        StructureBase* generator=nullptr;
        for(auto* structure:structureList)
            if(structure->getOwner()==house && structure->getItemID()==Structure_WindTrap)generator=structure;
        require(generator!=nullptr,"Missing generation fixture");
        auto generatorsQueued=[&] {
            int count=0;
            for(auto* structure:structureList) {
                if(structure->getOwner()!=house || !structure->isABuilder())continue;
                for(const auto& order:static_cast<BuilderBase*>(structure)->currentProductionQueue)
                    count+=order.itemID==Structure_WindTrap || order.itemID==Structure_NuclearPlant;
            }
            return count;
        };
        // Exactly the target the policy computes, read from the live house.
        auto bufferedTargetNow=[&] {
            QuantBotPowerInvestmentPolicy::PowerTarget shape;
            shape.demand=house->getPowerRequirement();
            shape.standardOutput=windOutput;
            return QuantBotPowerInvestmentPolicy::bufferedTarget(shape);
        };
        auto powerBuild=[&](int credits,int produced,int spice) {
            clearOrders();
            generator->setHealth(generator->getMaxHealth());generator->repairing=false;
            house->storedCredits=0;house->cityCredits=0;house->startingCredits=credits;
            bot->lastCalculatedSpice=spice;
            house->producedPower=std::max(0,produced);
            bot->build(5100);
            return generatorsQueued();
        };
        const auto savedMode=bot->gameMode;
        const auto savedType=game.gameType;
        // Model an established fleet in the same house inventory used by the
        // live income forecast; the opening fleet is deliberately too small.
        const int savedPowerWorkers=house->numItem[Unit_Harvester];
        house->numItem[Unit_Harvester]=16;
        for(auto difficulty:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
                QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal}) {
            bot->difficulty=difficulty;
            const int demand=house->getPowerRequirement();
            const int buffered=bufferedTargetNow();
            require(buffered>demand,"Fixture needs a positive power buffer");
            // Funded, earning and short of power: close the deficit.
            require(powerBuild(100000,demand-windOutput,1300000)>0,
                "A funded raw power deficit did not buy generation");
            // Exactly powered, no headroom: the mature base the user reported.
            // It invests now rather than after the next building breaks it.
            require(powerBuild(100000,demand,1300000)>0,
                "A powered base with no reserve did not invest in a buffer");
            // The reserve is standing: there is nothing left to buy.
            require(powerBuild(100000,buffered,1300000)==0,
                "Generation was bought on top of an adequate reserve");
            require(powerBuild(100000,buffered+windOutput*4,1300000)==0,
                "Generation was bought by a base already well above its buffer");
            // No measured income, however large the bank.
            require(powerBuild(100000,demand-windOutput,0)==0,
                "Generation was bought out of a starting balance rather than income");
            // No spare cash after the reserve, and nothing to repay it with.
            // Neither the buffered package nor the bare shortage is affordable.
            require(powerBuild(600,demand-windOutput,1300000)==0,
                "Generation was bought without the cash to fund the package");
            // Repairs already under way restore the missing output. The
            // restored capacity has to reach the buffered target, not merely
            // put the meters level.
            auto damagedBuild=[&](int produced) {
                clearOrders();
                generator->setHealth(FixPoint(generator->getMaxHealth())/2);generator->repairing=false;
                house->storedCredits=0;house->cityCredits=0;house->startingCredits=100000;
                bot->lastCalculatedSpice=1300000;
                house->producedPower=std::max(0,produced);
                bot->build(5100);
                const int queued=generatorsQueued();
                generator->setHealth(generator->getMaxHealth());generator->repairing=false;
                return queued;
            };
            const int restored=windOutput-windOutput/2; // the half it repairs back
            require(damagedBuild(buffered-restored)==0,
                "Generation was bought for output a repair in progress already restores");
            require(damagedBuild(demand-windOutput)>0,
                "A repair that cannot reach the buffer left the base unfunded");
            // The yard holding a generator order does not stack another on the
            // same pass; parallel yards counting that pending output are
            // exercised in the vanilla progression fixture below.
            clearOrders();
            house->storedCredits=0;house->cityCredits=0;house->startingCredits=100000;
            bot->lastCalculatedSpice=1300000;
            house->producedPower=std::max(0,demand-windOutput);
            yard->doProduceItem(Structure_WindTrap);
            require(yard->getProductionQueueSize()>0,"Fixture could not queue the pending generator");
            bot->build(5100);
            require(generatorsQueued()==1,"A pending generator was duplicated");
            // The core comes first: no worker fleet, no generation investment.
            const int fieldWorkers=house->numItem[Unit_Harvester];
            house->numItem[Unit_Harvester]=0;
            require(powerBuild(100000,demand-windOutput,1300000)==0,
                "Generation was bought before the income core existed");
            house->numItem[Unit_Harvester]=fieldWorkers;
            // Legacy campaign reconstruction shares the same gate.
            // A campaign house's update has no custom map-size ceiling (configuredCustomHarvesterLimit):
            // its limit is the spice budget, as the update would set it.
            bot->gameMode=QuantBot::GameMode::Campaign;game.gameType=GameType::Campaign;
            bot->harvesterLimit=1300000/2000;
            require(powerBuild(100000,demand-windOutput,1300000)>0,
                "Legacy campaign reconstruction never closes a raw power deficit");
            require(powerBuild(100000,demand,1300000)>0,
                "Legacy campaign reconstruction never builds a power buffer");
            bot->gameMode=savedMode;game.gameType=savedType;
        }
        house->numItem[Unit_Harvester]=savedPowerWorkers;
        bot->difficulty=probeDifficulty;
        clearOrders();
        SDL_Log("POWER_INVESTMENT_PASS: funded deficit and buffer bought, adequate reserve left alone; "
            "no income, no cash, no core, restoring repair and pending generator all decline; "
            "custom and campaign, all difficulties");
    }

    // Default must ignore the old engine cap in saved houses; explicit options win. In a custom game
    // the difficulty's configured map-size limit is the AI's own ceiling (docs/ai-scenario-matrix.md,
    // "Difficulty separation"); without one the remaining spice decides.
    house->maxHarvesters=60;
    game.gameInitSettings.gameOptions.maximumNumberOfHarvestersOverride=-1;
    bot->lastCalculatedSpice=1300000;bot->update();
    const int configuredWorkers=bot->configuredCustomHarvesterLimit();
    const int defaultWorkers=configuredWorkers>0 ? std::min(configuredWorkers,650) : 650;
    require(house->getMaxHarvesters()==0 && bot->harvesterLimit==defaultWorkers,"Default retained map-size worker ceiling");
    const int saved=house->numItem[Unit_Harvester];house->numItem[Unit_Harvester]=100;
    require(!house->isUnitLimitReached(Unit_Harvester),"Default still blocks the 101st worker");
    game.gameInitSettings.gameOptions.maximumNumberOfHarvestersOverride=100;bot->update();
    require(house->getMaxHarvesters()==100 && house->isUnitLimitReached(Unit_Harvester)
        && bot->harvesterLimit==std::min(100,defaultWorkers),"Explicit100 cap ignored");
    game.gameInitSettings.gameOptions.maximumNumberOfHarvestersOverride=0;bot->update();
    require(!house->isUnitLimitReached(Unit_Harvester) && bot->harvesterLimit==defaultWorkers,"Explicit unlimited froze AI production");
    bot->lastCalculatedSpice=1000;bot->update();
    require(bot->harvesterLimit<=1,"Unlimited engine cap removed finite-spice planning");
    house->numItem[Unit_Harvester]=saved;

    // Distant loaded returners cannot request a lift to occupied refineries.
    // They must count as a queue before walking within six tiles of the base.
    house->getChoam().availableItems.clear(); // Restore this map's absent market.
    Refinery* occupied=nullptr;
    for(auto* structure:structureList)if(structure->getOwner()==house && structure->getItemID()==Structure_Refinery) {
        auto* refinery=static_cast<Refinery*>(structure);
        auto* worker=static_cast<Harvester*>(house->createUnit(Unit_Harvester,true));
        require(worker,"Could not create unloading worker");
        worker->setAmountOfSpice(HARVESTERMAXSPICE);worker->setActive(false);
        refinery->book();refinery->assignHarvester(worker);occupied=refinery;
    }
    require(occupied,"Missing unloading bay");
    for(int i=0;i<2;++i) {
        auto* worker=static_cast<Harvester*>(house->createUnit(Unit_Harvester,true));
        require(worker,"Could not create field returner");
        bool deployed=false;
        for(int y=1;y<currentGameMap->getSizeY()-1 && !deployed;++y)
            for(int x=1;x<currentGameMap->getSizeX()-1 && !deployed;++x)
                if(!currentGameMap->getTile(x,y)->hasAGroundObject()
                    && blockDistance(Coord(x,y),occupied->getLocation())>12) {
                    worker->deploy(Coord(x,y));deployed=true;
                }
        require(deployed,"No field returner location");
        worker->setAmountOfSpice(HARVESTERMAXSPICE);worker->setTarget(occupied);
    }
    build(2000);game.gameCycleCount+=MILLI2CYCLES(11000);
    require(build(2000)==Structure_Refinery,"Distant blocked returners failed to fund additional unloading capacity");
    SDL_Log("REFINERY_QUEUE_PASS: full field returners trigger capacity before reaching the base");

    // Mature bases must relieve a full earned-income bank before continuing
    // optional expansion. Exercise the real order path at every difficulty.
    const auto savedDifficulty=bot->difficulty;
    for(auto difficulty:{QuantBot::Difficulty::Easy,QuantBot::Difficulty::Medium,
            QuantBot::Difficulty::Hard,QuantBot::Difficulty::Brutal,QuantBot::Difficulty::Defend}) {
        bot->difficulty=difficulty;
        require(storageBuild(35025,35023,0)==Structure_Silo,"Near-full storage was starved by optional investment");
        require(storageBuild(35025,10000,25023)==Structure_Silo,"City income was ignored by storage expansion");
        require(storageBuild(35025,1000,0)!=Structure_Silo,"Low storage usage triggered a silo");
        require(storageBuild(999999,999999,0)!=Structure_Silo,"Silos grew beyond the absolute credit ceiling");
    }
    bot->difficulty=savedDifficulty;
    // A paid capacity order already in a second yard prevents duplicate silos.
    clearOrders();
    const auto secondSite=bot->findPlaceLocation(Structure_ConstructionYard);
    require(secondSite.isValid(),"No second yard site");
    auto* secondYard=dynamic_cast<BuilderBase*>(house->placeStructure(NONE_ID,
        Structure_ConstructionYard,secondSite.x,secondSite.y,true,true));
    require(secondYard,"Second yard placement failed");
    clearOrders();house->capacity=35025;house->startingCredits=0;
    house->storedCredits=35023;house->cityCredits=0;house->producedPower=10000;
    bot->build(5100);
    int silos=0;
    for(auto* builder:{yard,secondYard})for(const auto& order:builder->currentProductionQueue)
        if(order.itemID==Structure_Silo)++silos;
    require(silos==1,"Parallel yards duplicated or omitted the storage order");
    clearOrders();
    // Losing core infrastructure re-establishes its priority over storage.
    auto destroyCore=[&](Uint32 item) {
        StructureBase* victim=nullptr;
        for(auto* structure:structureList)
            if(structure->getOwner()==house && structure->getItemID()==item) {victim=structure;break;}
        require(victim,"Missing core fixture building");
        victim->destroy();
    };
    if(game.isCitySimEnabled()) {
        destroyCore(Structure_HighTechFactory);
        require(storageBuild(35025,35023,0)!=Structure_Silo,"Storage displaced missing high-tech core");
    }
    destroyCore(Structure_HeavyFactory);
    require(storageBuild(35025,35023,0)!=Structure_Silo,"Storage displaced missing heavy factory/core infrastructure");
    SDL_Log("STORAGE_PRIORITY_PASS: core first, all difficulties, spice/tax capacity, pending orders and absolute ceiling");

    // Vanilla and Dune2R progression. An enemy wing elsewhere on the map buys
    // the baseline emplacements and then waits behind the production core and
    // the advanced factories, instead of owning every slot for the whole game.
    if(!game.isCitySimEnabled()) {
        House* airEnemy=nullptr;
        for(int h=0;h<NUM_HOUSES && !airEnemy;++h) {
            House* other=game.getHouse(h);
            if(other && other->getTeamID()!=house->getTeamID())airEnemy=other;
        }
        require(airEnemy,"No enemy house for air pressure");
        yard->curUpgradeLev=std::max<Uint8>(yard->curUpgradeLev,2); // Emplacements already unlocked.
        secondYard->curUpgradeLev=yard->curUpgradeLev;
        secondYard->updateBuildList();
        // IX requires a Starport even when this fixture has no import stock.
        for(Uint32 item:{Structure_HeavyFactory,Structure_RepairYard,Structure_StarPort})
            if(!house->getNumItems(item))place(item);
        // Other non-city mods retain infantry prerequisites in their opening.
        if(house->isPowerRequired())
            for(Uint32 item:{Structure_Barracks,Structure_WOR})
                if(!house->getNumItems(item))place(item);
        if(house->getNumItems(Structure_HighTechFactory))destroyCore(Structure_HighTechFactory);
        if(house->getNumItems(Structure_IX))destroyCore(Structure_IX);
        while(house->getNumItems(Structure_RocketTurret)<2)place(Structure_RocketTurret);
        // The unloading-queue fixture above has been made; empty its cargo so
        // these decisions are about progression rather than trapped spice.
        for(auto* unit:unitList) if(unit->getOwner()==house && unit->getItemID()==Unit_Harvester) {
            auto* worker=static_cast<Harvester*>(unit);
            worker->setAmountOfSpice(0);worker->setTarget(nullptr);
        }
        // Restore the finite-spice planning the harvester-cap fixture above
        // deliberately starved, so refining decisions see a real field again.
        clearOrders();bot->lastCalculatedSpice=1300000;bot->update();
        require(bot->harvesterLimit>10 || bot->harvesterLimit==bot->configuredCustomHarvesterLimit(),
            "Fixture could not restore a funded worker plan");
        // The progression fixtures below model fleets of up to 15+ workers: lift an Easy/Medium
        // configured custom ceiling (8/15 on this map) to the field's spice budget.
        bot->harvesterLimit=std::max(bot->harvesterLimit,1300000/2000);
        // Expire unrelated structure-loss recovery bookkeeping from fixture setup.
        game.gameCycleCount+=MILLI2CYCLES(200000);
        const int fieldWorkers=house->numItem[Unit_Harvester];
        house->numItem[Unit_Harvester]=3; // The four bays already outrun this fleet.
        airEnemy->numItem[Unit_Ornithopter]=6;
        require(build(100000)==Structure_HighTechFactory,
            "Distant enemy aircraft blocked the affordable missing production core");
        place(Structure_HighTechFactory);
        require(build(100000)==Structure_IX,"Distant enemy aircraft blocked affordable advanced tech");
        // Earned credits at capacity are lost income now; that does not wait
        // for tech the base has not finished.
        require(storageBuild(35025,35023,0)==Structure_Silo,"Overflowing storage waited for optional tech");
        place(Structure_IX);
        require(build(100000)==Structure_RocketTurret,
            "A finished core and IX did not release the counter-aircraft response");

        // Tech this tree cannot reach never reserves a slot and never withholds
        // the defence the base can actually build.
        auto& advanced=game.objectData.data[Structure_HighTechFactory][HOUSE_ATREIDES];
        auto& advancedTech=game.objectData.data[Structure_IX][HOUSE_ATREIDES];
        const int savedTechLevel=game.techLevel;
        const int loweredTech=std::max(1,std::min<int>(advanced.techLevel,advancedTech.techLevel)-1);
        if(loweredTech>=game.objectData.data[Structure_RocketTurret][HOUSE_ATREIDES].techLevel) {
            game.techLevel=loweredTech;
            if(house->getNumItems(Structure_IX))destroyCore(Structure_IX);
            if(house->getNumItems(Structure_HighTechFactory))destroyCore(Structure_HighTechFactory);
            game.gameCycleCount+=MILLI2CYCLES(200000);
            yard->updateBuildList();secondYard->updateBuildList();
            const Uint32 lowTech=build(100000);
            require(lowTech!=Structure_HighTechFactory && lowTech!=Structure_IX,
                "Construction ordered advanced tech this tech level does not offer");
            require(lowTech==Structure_RocketTurret,
                "An unreachable advanced tree still withheld the defence the base could build");
            game.techLevel=savedTechLevel;
            yard->updateBuildList();secondYard->updateBuildList();
            for(Uint32 item:{Structure_HighTechFactory,Structure_IX})
                if(!house->getNumItems(item))place(item);
            game.gameCycleCount+=MILLI2CYCLES(200000);
        }
        airEnemy->numItem[Unit_Ornithopter]=6;

        // Processing capacity follows the worker fleet and the field, not a
        // cash surplus: four bays are not a ceiling, and empty ones are not a
        // reason to keep buying more.
        house->numItem[Unit_Harvester]=15;
        require(build(4000)==Structure_Refinery,
            "A funded worker fleet did not grow refining capacity past four bays");
        airEnemy->numItem[Unit_Ornithopter]=0;
        house->numItem[Unit_Harvester]=3;
        require(build(4000)!=Structure_Refinery,"Empty bays kept expanding without workers to fill them");

        // A queued bay brings its own storage. Count that pending capacity
        // rather than cancelling every silo a growing refinery base still needs.
        const int bayCapacity=game.objectData.data[Structure_Refinery][HOUSE_ATREIDES].capacity;
        require(bayCapacity>0,"Fixture needs refinery storage capacity");
        auto storageWithPendingBay=[&](int capacity,int stored) {
            clearOrders();
            house->capacity=capacity;house->startingCredits=0;
            house->storedCredits=stored;house->cityCredits=0;
            house->producedPower=10000;bot->lastCalculatedSpice=1300000;
            secondYard->doProduceItem(Structure_Refinery);
            require(secondYard->getProductionQueueSize()>0,"Fixture could not queue the pending bay");
            bot->build(5100);
            int silos=0;
            for(auto* builder:{yard,secondYard})for(const auto& order:builder->currentProductionQueue)
                if(order.itemID==Structure_Silo)++silos;
            return silos;
        };
        require(storageWithPendingBay(20000,19000)==1,
            "A queued refinery cancelled the storage the bank still needed");
        require(storageWithPendingBay(20000,(20000+bayCapacity)*79/100)==0,
            "Pending refinery capacity was not counted before ordering another silo");

        // Parallel yards share one power package. Every generator already on
        // order counts as capacity, so the idle yard fills what is still
        // missing and never buys the same buffer twice. A single pending
        // generator that cannot reach the target does not stop the rest.
        if(!house->isPowerRequired()) {
            const int windOutput=-game.objectData.data[Structure_WindTrap][HOUSE_ATREIDES].power;
            require(windOutput>0,"Fixture needs a windtrap that produces power");
            const int savedRequirement=house->powerRequirement;
            const int savedWorkers=house->numItem[Unit_Harvester];
            house->numItem[Unit_Harvester]=16;       // an established income fleet
            house->powerRequirement=windOutput*10;   // a mature base, 20% is two generators
            QuantBotPowerInvestmentPolicy::PowerTarget shape;
            shape.demand=house->getPowerRequirement();shape.standardOutput=windOutput;
            const int missing=(QuantBotPowerInvestmentPolicy::bufferedTarget(shape)
                -house->getPowerRequirement()+windOutput-1)/windOutput;
            require(missing>1,"Fixture needs a buffer of more than one generator");
            auto parallelPower=[&](int pending) {
                clearOrders();
                secondYard->upgrading=false;secondYard->setHealth(secondYard->getMaxHealth());
                house->storedCredits=0;house->cityCredits=0;house->startingCredits=100000;
                house->producedPower=house->getPowerRequirement();bot->lastCalculatedSpice=1300000;
                for(int queued=0;queued<pending;++queued)yard->doProduceItem(Structure_WindTrap);
                require(yard->getProductionQueueSize()>=pending,"Fixture could not queue pending generation");
                bot->build(5100);
                int generators=0;
                for(auto* builder:{yard,secondYard})for(const auto& order:builder->currentProductionQueue)
                    generators+=order.itemID==Structure_WindTrap || order.itemID==Structure_NuclearPlant;
                return generators;
            };
            require(parallelPower(missing)==missing,
                "A second yard bought generation the pending package already covers");
            require(parallelPower(1)>1,
                "One pending generator blocked the rest of an insufficient package");
            clearOrders();
            secondYard->upgrading=false;secondYard->setHealth(secondYard->getMaxHealth());
            house->storedCredits=0;house->cityCredits=0;house->startingCredits=100000;
            house->producedPower=QuantBotPowerInvestmentPolicy::bufferedTarget(shape);
            bot->lastCalculatedSpice=1300000;
            yard->doProduceItem(Structure_HeavyFactory);
            require(yard->getProductionQueueSize()>0,"Fixture could not queue future power demand");
            bot->build(5100);
            bool anticipatingDemand=false;
            for(const auto& order:secondYard->currentProductionQueue)
                anticipatingDemand|=order.itemID==Structure_WindTrap || order.itemID==Structure_NuclearPlant;
            require(anticipatingDemand,"Standing reserve ignored a queued factory's future power demand");
            house->powerRequirement=savedRequirement;
            house->numItem[Unit_Harvester]=savedWorkers;
            clearOrders();
            SDL_Log("POWER_BUFFER_PARALLEL_PASS: pending generation counted once across yards, "
                "insufficient pending still completed");
        }

        // Defence never invents power headroom of its own. Outside the city the
        // economic reserve is now the larger of the two, so these cases fund it
        // first and then ask whether defence added anything on top: with the
        // option off nothing is bought, with it on the emplacement's own draw
        // is the requirement, and a genuine shortage still builds its generator.
        const int turretPower=game.objectData.data[Structure_RocketTurret][HOUSE_ATREIDES].power;
        const int economicCover=[&] {
            if(house->isPowerRequired())return house->getPowerRequirement();
            QuantBotPowerInvestmentPolicy::PowerTarget shape;
            shape.demand=house->getPowerRequirement();
            shape.standardOutput=-game.objectData.data[Structure_WindTrap][HOUSE_ATREIDES].power;
            return QuantBotPowerInvestmentPolicy::bufferedTarget(shape);
        }();
        auto pressuredBuild=[&](int credits,int power) {
            clearOrders();house->storedCredits=0;house->cityCredits=0;house->startingCredits=credits;
            house->producedPower=power;bot->lastCalculatedSpice=1300000;
            bot->build(5100);
            for(const auto& order:yard->currentProductionQueue)
                if(order.itemID!=Structure_Slab1 && order.itemID!=Structure_Slab4 && order.itemID!=Structure_Road)
                    return order.itemID;
            return Uint32(NONE_ID);
        };
        airEnemy->numItem[Unit_Ornithopter]=6;
        const bool savedTurretPower=game.gameInitSettings.gameOptions.rocketTurretsNeedPower;
        game.gameInitSettings.gameOptions.rocketTurretsNeedPower=false;
        require(pressuredBuild(100000,economicCover)!=Structure_WindTrap,
            "Defence bought a power buffer with the turret power option off");
        game.gameInitSettings.gameOptions.rocketTurretsNeedPower=true;
        require(pressuredBuild(100000,std::max(economicCover,
                house->getPowerRequirement()+turretPower))!=Structure_WindTrap,
            "A covered turret draw still reserved spare generation");
        if(turretPower>0) {
            // A base level with its requirement is short of both reserves.
            const Uint32 deficitBuild=pressuredBuild(100000,house->getPowerRequirement());
            require(deficitBuild==Structure_WindTrap || deficitBuild==Structure_NuclearPlant,
                "A real turret power deficit no longer builds its generator");
        }
        game.gameInitSettings.gameOptions.rocketTurretsNeedPower=savedTurretPower;
        airEnemy->numItem[Unit_Ornithopter]=0;
        house->numItem[Unit_Harvester]=fieldWorkers;
        clearOrders();
        SDL_Log("VANILLA_PROGRESSION_PASS: core and IX before repeated cover, storage before tech, "
            "worker-led refining, pending bay capacity, genuine turret power");
    }
    SDL_Log("OPENING_ECONOMY_PASS: rich factory opening, poor economy opening, absent/sold-out/disabled markets, default/100/unlimited caps");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: level=0 seed=0 attack_percent=0 partner=brutal enemy=hard cycle=%u outcome=opening_economy_pass",game.getGameCycleCount());
}
