#include <players/QuantBotBuildPass.h>
// Diagnostic-only real construction queue checks for both campaign roles.
void runCampaignRepairProbe(Game& game) {
    auto require=[](bool ok,const char* reason){if(!ok){SDL_Log("CAMPAIGN_REPAIR_FAIL: %s",reason);throw std::runtime_error(reason);}};
    game.gameInitSettings.gameOptions.concreteRequired = false; // Queue policy isolated from slab sequencing.
    if(game.isCitySimEnabled())
        for(int y=0;y<currentGameMap->getSizeY();++y)for(int x=0;x<currentGameMap->getSizeX();++x)
            if(!currentGameMap->getTile(x,y)->hasAStructure())currentGameMap->getTile(x,y)->setType(Terrain_Rock);
    QuantBot* partner=nullptr;QuantBot* enemy=nullptr;
    for(int h=0;h<NUM_HOUSES;++h) if(auto* house=game.getHouse(h))
        for(const auto& player:house->getPlayerList()) if(auto* bot=dynamic_cast<QuantBot*>(player.get())) {
            if(h==HOUSE_HARKONNEN) partner=bot;
            else if(!enemy && house->getNumItems(Structure_ConstructionYard)) enemy=bot;
        }
    require(partner && enemy,"Repair fixture requires both campaign roles");
    for(auto* bot:{partner,enemy}) {
        auto& force = const_cast<QuantBotConfig&>(bot->botConfig()).force;
        const bool savedRoleFix = force.campaignRepairRoleFix;
        const bool savedDeferred = force.campaignDeferredYardUpgrade;
        force.campaignDeferredYardUpgrade = false;
        force.campaignRepairRoleFix = false; // Historical OFF control below.
        bot->difficulty=QuantBot::Difficulty::Hard;
        // City campaign enemies retain their authored technology permissions.
        // Give this queue fixture the repair chain; role restrictions are tested below.
        if (bot == enemy && game.isCitySimEnabled()) {
            bot->campaignOriginalStructures.insert(Structure_Radar);
            bot->campaignOriginalStructures.insert(Structure_RepairYard);
        }
        auto* house=const_cast<House*>(bot->getHouse());
        auto place=[&](Uint32 item) {
            for(int y=1;y<currentGameMap->getSizeY()-4;y+=4)
                for(int x=1;x<currentGameMap->getSizeX()-4;x+=4) {
                    bool clear=true;
                    for(int dy=0;dy<3;++dy)for(int dx=0;dx<3;++dx)
                        if(currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject())clear=false;
                    if(clear && house->placeStructure(NONE_ID,item,x,y,true,true))return;
                }
            throw std::runtime_error("No fixture building site");
        };
        for(auto item:{Structure_WindTrap,Structure_Refinery,Structure_HeavyFactory,Structure_LightFactory})
            if(!house->getNumItems(item))place(item);
        // Brutal also establishes its Starport before optional repair capacity.
        if(!house->getNumItems(Structure_StarPort))place(Structure_StarPort);
        if(!house->getNumItems(Unit_Harvester))house->createUnit(Unit_Harvester,true);
        auto originalStructures=structureList;
        for(auto* building:originalStructures)
            if(building->getOwner()==house && (building->getItemID()==Structure_RepairYard
                || building->getItemID()==Structure_Radar))building->destroy();
        BuilderBase* yard=nullptr;
        for(auto* building:structureList)
            if(building->getOwner()==house && building->getItemID()==Structure_ConstructionYard)
                {yard=dynamic_cast<BuilderBase*>(building);break;}
        require(yard,"Missing fixture construction yard");
        yard->curUpgradeLev = yard->getMaxUpgradeLevel();
        yard->setHealth(yard->getMaxHealth());
        yard->updateBuildList();
        require(yard->isAvailableToBuild(Structure_Radar),"Radar prerequisites missing in fixture");
        // Keep this fixture about repair prerequisites; Medium correctly
        // prioritizes required power before optional construction.
        while(house->getProducedPower()<house->getPowerRequirement()+200)place(Structure_WindTrap);
        // Starting credits are not clipped to the fixture's refinery storage.
        house->startingCredits = 60000; bot->lastCalculatedSpice=100000;
        while (house->getNumItems(Unit_Harvester) < 12) house->createUnit(Unit_Harvester, true);
        // Satisfy the city transport baseline before asserting repair priority.
        while (house->getNumItems(Unit_Carryall) < 4) house->createUnit(Unit_Carryall, true);
        // A deferred bulk-concrete upgrade must return the producer's turn.
        // Otherwise even ordinary construction waits forever for spare cash.
        {
            force.campaignDeferredYardUpgrade = true;
            QuantBot::BuildPass pass(4000);
            pass.money = 1000;
            pass.actualHarvesters = house->getNumItems(Unit_Harvester);
            pass.citySimEnabled = game.isCitySimEnabled();
            pass.forecastNetIncome = 10000;
            QuantBot::BuilderTurn turn(pass.money);
            turn.pBuilder = yard; turn.pStructure = yard;
            yard->curUpgradeLev = 0; yard->upgrading = false;
            game.gameInitSettings.gameOptions.concreteRequired = true;
            require(!bot->upgradeYardForConcrete(pass, turn) && !yard->isUpgrading(),
                "Deferred concrete upgrade consumed the yard's construction turn");
            game.gameInitSettings.gameOptions.concreteRequired = false;
            force.campaignDeferredYardUpgrade = false;
            yard->curUpgradeLev = yard->getMaxUpgradeLevel();
        }
        if(bot==partner) bot->gameMode=QuantBot::GameMode::Custom;
        bot->build(4000);
        require(yard->getCurrentProducedItem()==Structure_Radar,"Missing repair prerequisite was not prioritized");
        yard->doCancelItem(Structure_Radar);
        bot->builderPlaceLocations.erase(yard->getObjectID());
        bot->reservedStructures.erase(yard->getObjectID());
        place(Structure_Radar);yard->updateBuildList();
        require(yard->isAvailableToBuild(Structure_RepairYard),"Repair prerequisites still missing");
        bot->difficulty=QuantBot::Difficulty::Medium;
        bot->initialItemCount[Structure_RepairYard]=0; // Only vanilla Medium is replacement-only.
        for(int attempt=0;attempt<3;++attempt) {
            bot->build(4000);
            if(game.isCitySimEnabled())
                require(bot->canAddRepairYard(0),"City Medium lost repair-yard admission");
            for(auto* building:structureList) if(building->getOwner()==house)
                if(auto* builder=dynamic_cast<BuilderBase*>(building)) {
                    for(const auto& item:builder->currentProductionQueue)
                        require(game.isCitySimEnabled() || item.itemID!=Structure_RepairYard,"Vanilla Medium queued a repair yard");
                    while(builder->getProductionQueueSize()) builder->doCancelItem(builder->getCurrentProducedItem());
                    bot->builderPlaceLocations.erase(builder->getObjectID());
                    bot->reservedStructures.erase(builder->getObjectID());
                }
        }
        // An authored yard is still replaced, using the same rebuild count.
        bot->initialItemCount[Structure_RepairYard]=1;
        bot->build(4000);
        require(yard->getCurrentProducedItem()==Structure_RepairYard,"Medium failed to replace its authored repair yard");
        bot->build(4000);
        int mediumQueued=0;
        for(auto* building:structureList) if(building->getOwner()==house)
            if(auto* builder=dynamic_cast<BuilderBase*>(building))
                for(const auto& item:builder->currentProductionQueue)
                    if(item.itemID==Structure_RepairYard)++mediumQueued;
        require(mediumQueued==1,"Medium expanded beyond its authored repair-yard count");
        yard->doCancelItem(Structure_RepairYard);
        bot->builderPlaceLocations.erase(yard->getObjectID());
        bot->reservedStructures.erase(yard->getObjectID());
        bot->difficulty=QuantBot::Difficulty::Hard;
        bot->build(4000);
        require(yard->getCurrentProducedItem()==Structure_RepairYard,"Missing repair yard was not prioritized");
        bot->build(4000);
        int queued=0;
        for(auto* building:structureList) if(building->getOwner()==house)
            if(auto* builder=dynamic_cast<BuilderBase*>(building))
                for(const auto& item:builder->currentProductionQueue)
                    if(item.itemID==Structure_RepairYard) ++queued;
        require(queued==1,"Repair planning duplicated an already queued yard");
        // Role-scoped policy: every human helper may establish repairs; only
        // an Easy campaign enemy is barred. Verify real queues, not just a
        // predicate, and retain the OFF checks above as the negative control.
        force.campaignRepairRoleFix = true;
        for (auto tier : {QuantBot::Difficulty::Easy, QuantBot::Difficulty::Medium,
                          QuantBot::Difficulty::Hard, QuantBot::Difficulty::Brutal}) {
            for (auto* building : structureList) if (building->getOwner() == house)
                if (auto* builder = dynamic_cast<BuilderBase*>(building)) {
                    while (builder->getProductionQueueSize()) builder->doCancelItem(builder->getCurrentProducedItem());
                    bot->builderPlaceLocations.erase(builder->getObjectID());
                    bot->reservedStructures.erase(builder->getObjectID());
                }
            bot->difficulty = tier;
            bot->initialItemCount[Structure_RepairYard] = 0;
            house->startingCredits = 60000;
            const bool denied = bot->isCampaignEnemy() && tier == QuantBot::Difficulty::Easy;
            require(bot->canAddRepairYard(0) != denied, "Repair restriction crossed campaign role boundary");
            bot->build(4000);
            SDL_Log("CAMPAIGN_REPAIR_CASE: tier=%d enemy=%d queued=%u denied=%d", int(tier), bot->isCampaignEnemy(), yard->getCurrentProducedItem(), denied);
            require((yard->getCurrentProducedItem() == Structure_RepairYard) != denied,
                "Role-scoped repair policy produced the wrong construction order");
        }
        // The legacy enemy else-if chain used to consume the whole turn on a
        // refused upgrade. Exercise its actual blackout recovery with ON/OFF.
        if (bot == enemy && game.isCitySimEnabled()) {
            std::array<int, Num_ItemID> authored;
            std::copy(std::begin(bot->initialItemCount), std::end(bot->initialItemCount), authored.begin());
            for (int i = 0; i < Num_ItemID; ++i) bot->initialItemCount[i] = house->getNumItems(i);
            const int required = house->powerRequirement;
            house->powerRequirement = house->getProducedPower() + 1;
            bot->difficulty = QuantBot::Difficulty::Easy;
            bot->gameMode = QuantBot::GameMode::Campaign;
            force.campaignRepairRoleFix = true;
            yard->curUpgradeLev = 0; yard->updateBuildList();
            for (bool deferred : {true, false}) {
                force.campaignDeferredYardUpgrade = deferred;
                house->startingCredits = 60000;
                QuantBot::BuildPass pass(4000);
                bot->resetBuildPass(); bot->planBuildInventory(pass); bot->planBuildBudget(pass);
                bot->planCityState(pass); bot->planStrategicReserve(pass); bot->planSpiceEconomy(pass);
                bot->planPowerAndYards(pass); bot->planCityPopulation(pass); bot->planCivicNeedAndTrips(pass);
                bot->planUnitMix(pass); bot->planCapitalForecast(pass); bot->planCityBootstrap(pass);
                bot->planOpeningProduction(pass); bot->planDefenceCoverage(pass); bot->planVanillaCoreDefence(pass);
                bot->planCityResearchOrder(pass); bot->planTechSavings(pass); bot->planCapitalCandidates(pass);
                bot->planVanillaBoldCapital(pass); bot->chooseCapitalPlan(pass);
                // Isolate this producer branch from competing capital projects.
                pass.capitalChoice = -1; pass.sharedSpending = false; pass.cityGrowthProtected = false;
                pass.money = 60000; pass.citySimEnabled = true;
                pass.actualHarvesters = house->getNumItems(Unit_Harvester);
                const int oldLimit = bot->harvesterLimit;
                bot->harvesterLimit = 0; // Explicitly enter the legacy upgrade-vs-power branch.
                QuantBot::BuilderTurn turn(pass.money);
                turn.pBuilder = yard; turn.pStructure = yard;
                QuantBot::YardTurn yardTurn;
                bot->planningBuilder = yard->getObjectID(); bot->clearPlacementCache();
                bot->buildCampaignYard(pass, turn, yardTurn);
                bot->harvesterLimit = oldLimit;
                if (deferred)
                    require(!yard->isUpgrading() && (yard->getCurrentProducedItem() == Structure_WindTrap
                        || yard->getCurrentProducedItem() == Structure_NuclearPlant),
                        "Deferred enemy upgrade blocked blackout recovery");
                else require(yard->isUpgrading(), "OFF control failed to attempt its legacy yard upgrade");
                for (auto* building : structureList) if (building->getOwner() == house)
                    if (auto* builder = dynamic_cast<BuilderBase*>(building)) {
                        while (builder->getProductionQueueSize()) builder->doCancelItem(builder->getCurrentProducedItem());
                        bot->builderPlaceLocations.erase(builder->getObjectID());
                        bot->reservedStructures.erase(builder->getObjectID());
                    }
                yard->upgrading = false; yard->upgradeProgress = 0;
            }
            std::copy(authored.begin(), authored.end(), std::begin(bot->initialItemCount));
            house->powerRequirement = required;
            bot->difficulty = QuantBot::Difficulty::Hard;
            force.campaignRepairRoleFix = false;
            yard->curUpgradeLev = yard->getMaxUpgradeLevel(); yard->updateBuildList();
            SDL_Log("CAMPAIGN_UPGRADE_PASS: enemy blackout recovery continues with upgrade deferred");
        }
        force.campaignRepairRoleFix = savedRoleFix;
        force.campaignDeferredYardUpgrade = savedDeferred;
        SDL_Log("CAMPAIGN_REPAIR_PASS: tier=%d enemy=%d missing yard queued once",int(bot->difficulty),bot->isCampaignEnemy());
    }
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=repair outcome=passed");
}
