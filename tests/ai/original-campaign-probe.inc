// Linked against the real simulation by run-campaign-balance.py. Access-control
// relaxation belongs to the diagnostic main only, never the shipped executable.
#include <players/CampaignAIPlayer.h>
#include <players/OriginalCampaignPolicy.h>
#include <misc/OMemoryStream.h>
#include <misc/IMemoryStream.h>

void runOriginalCampaignProbe(Game& game) {
    const auto require = [](bool ok, const char* message) {
        if(!ok) throw std::runtime_error(std::string("ORIGINAL_CAMPAIGN_FAIL: ") + message);
    };
    CampaignAIPlayer* bot = nullptr;
    for(int h = 0; h < NUM_HOUSES; ++h) if(auto* house = game.getHouse(h))
        for(const auto& player : house->getPlayerList())
            if(auto* candidate = dynamic_cast<CampaignAIPlayer*>(player.get()))
                if(house->getNumItems(Structure_ConstructionYard)) bot = candidate;
    require(bot, "fixture requires a Campaign AI enemy with a yard");
    auto* house = const_cast<House*>(bot->getHouse());
    auto* other = game.getHouse(HOUSE_HARKONNEN);
    require(other && other != house, "fixture needs an opposing human house");
    auto spawn = [&](int item, bool scenario) {
        for(int y = 1; y < currentGameMap->getSizeY() - 1; ++y)
            for(int x = 1; x < currentGameMap->getSizeX() - 1; ++x) {
                auto* tile = currentGameMap->getTile(x, y);
                if(tile->hasAGroundObject() || tile->isMountain()) continue;
                auto* unit = house->createUnit(item, scenario);
                require(unit, "unit allocation failed");
                unit->deploy(Coord(x, y)); unit->doSetAttackMode(GUARD);
                return unit;
            }
        throw std::runtime_error("No free campaign fixture tile");
    };

    auto* produced = spawn(Unit_Tank, false);
    auto* authored = spawn(Unit_Tank, true);
    bot->originalRules = false;
    bot->onObjectWasBuilt(produced);
    require(produced->getAttackMode() == GUARD, "OFF changed the existing unit-built callback");
    bot->originalRules = true;
    bot->onObjectWasBuilt(produced);
    bot->onObjectWasBuilt(authored);
    require(produced->getAttackMode() == HUNT, "one produced tank did not hunt immediately");
    require(authored->getAttackMode() == GUARD, "scenario guard action was overwritten");
    require(!bot->attackTriggered, "individual production unexpectedly enabled legacy waves");

    // Original full-scale attack bug: flag only, no synthetic army-wide order.
    house->doneFullScaleAttack = false;
    bot->triggerFullScaleAttack();
    require(house->hasTriggeredFullScaleAttack(), "base attack flag not recorded");
    require(authored->getAttackMode() == GUARD, "original mode scrambled scenario guards");
    for(const auto* attacker : unitList) if(attacker->getOwner() == other && authored->canAttack(attacker)) {
        bot->onDamage(authored, 10, attacker->getObjectID());
        require(authored->getAttackMode() == GUARD && !authored->wasForced(), "original guard chased damage with a forced order");
        break;
    }

    BuilderBase* yard = nullptr;
    BuilderBase* factory = nullptr;
    for(auto* structure : structureList) if(structure->getOwner() == house) {
        if(structure->getItemID() == Structure_ConstructionYard) yard = dynamic_cast<BuilderBase*>(structure);
        if(structure->getItemID() == Structure_HeavyFactory) factory = dynamic_cast<BuilderBase*>(structure);
    }
    require(yard && factory, "level-nine fixture needs yard and heavy factory");
    factory->curUpgradeLev = 0; factory->updateBuildList();
    bot->originalRules = false;
    factory->grantOriginalCampaignUpgrades();
    require(factory->getCurrentUpgradeLevel() == 0, "OFF got a free upgrade");
    bot->originalRules = true;
    const auto credits = house->getCredits();
    factory->grantOriginalCampaignUpgrades();
    require(factory->getCurrentUpgradeLevel() == factory->getMaxUpgradeLevel(), "original factory not upgraded");
    require(house->getCredits() == credits, "original upgrade charged credits");
    for(auto* structure : structureList) if(structure->getOwner() == other)
        if(auto* builder = dynamic_cast<BuilderBase*>(structure)) {
            const int level = builder->getCurrentUpgradeLevel();
            builder->grantOriginalCampaignUpgrades();
            require(builder->getCurrentUpgradeLevel() == level, "non-Campaign controller got free upgrade");
        }

    // The five original slots keep temporarily unavailable entries. Selection
    // skips those slots instead of deleting them or blocking a later rebuild.
    bot->rebuildQueue.clear();
    bot->onDecrementStructures(Structure_Wall, Coord(1, 1));
    require(bot->rebuildQueue.empty(), "walls consumed original building rebuild slots");
    bot->onDecrementStructures(Structure_ResearchLab, Coord(1, 1));
    bot->onDecrementStructures(Structure_WindTrap, yard->getLocation());
    for(int n = 0; n < 5; ++n) bot->onDecrementStructures(Structure_WindTrap, Coord(n + 3, 3));
    require(bot->rebuildQueue.size() == 5, "rebuild queue exceeded original five slots");
    while(yard->getProductionQueueSize()) yard->doCancelItem(yard->getCurrentProducedItem());
    house->addCredits(10000);
    yard->grantOriginalCampaignUpgrades();
    bot->updateStructures();
    require(bot->rebuildQueue.front().itemID == Structure_ResearchLab, "unavailable slot was lost");
    require(yard->getCurrentProducedItem() == Structure_WindTrap, "later buildable slot was blocked");
    yard->productionProgress = yard->getBuildItem(Structure_WindTrap)->price;
    yard->setWaitingToPlace();
    // Force all matching slots to occupied yard footprint for the blocked check.
    for(auto& entry : bot->rebuildQueue) if(entry.itemID == Structure_WindTrap) entry.location = yard->getLocation();
    bot->updateStructures();
    require(bot->rebuildQueue.size() == 5 && bot->rebuildQueue[1].itemID == Structure_WindTrap,
        "blocked rebuild placement forgot the original slot");
    require(yard->isWaitingToPlace(), "blocked rebuilt structure was cancelled");

    // Authored definitions, movement filters, minimum/maximum and stale IDs.
    house->aiteams.clear(); house->addAITeam(AITeamBehavior_Normal, AITeamType_Tracked, 1, 2);
    bot->scenarioTeams.clear(); bot->scenarioTeamsInitialized = false; bot->nextTeamTick = 0;
    bot->updateScenarioTeams();
    require(bot->scenarioTeams.size() == 1, "authored team was ignored");
    auto& team = bot->scenarioTeams.front();
    require(team.minimum == 1 && team.maximum == 2, "authored min/max replaced by fixed eight");
    for(int cycle = 0; cycle < 5000 && team.members.empty(); ++cycle) {
        ++game.gameCycleCount; bot->updateScenarioTeams();
    }
    require(team.members.size() == 1, "original team script did not recruit a scenario unit within its bounded cadence");
    require(team.members.front() != produced->getObjectID(), "scenario team recruited a produced unit");
    team.members.push_back(NONE_ID);
    bot->nextTeamTick = 0;
    bot->updateScenarioTeams();
    require(std::find(team.members.begin(), team.members.end(), NONE_ID) == team.members.end(), "dead ID survived team cleanup");
    require(team.members.size() <= 2, "team exceeded mission maximum");

    // Exact controller-state persistence, including next action time and IDs.
    const auto previousVersion = game.loadedSavegameVersion;
    game.loadedSavegameVersion = 9886;
    OMemoryStream saved; saved.open(); bot->save(saved);
    IMemoryStream input(saved.getData(), saved.getDataLength());
    CampaignAIPlayer loaded(input, house);
    OMemoryStream resaved; resaved.open(); loaded.save(resaved);
    require(std::string(saved.getData(), saved.getDataLength()) == std::string(resaved.getData(), resaved.getDataLength()),
        "controller save/load changed mode, team membership, target or schedule");

    // Serialize the actual pre-9886 layout, followed by a sentinel. A legacy
    // load must not consume new mode/team bytes or inherit today's default.
    OMemoryStream legacy; legacy.open(); bot->Player::save(legacy);
    legacy.writeUint32(bot->rebuildQueue.size());
    for(const auto& entry : bot->rebuildQueue) entry.save(legacy);
    legacy.writeBool(true); legacy.writeUint32(0x12345678);
    game.loadedSavegameVersion = 9885;
    IMemoryStream oldInput(legacy.getData(), legacy.getDataLength());
    CampaignAIPlayer old(oldInput, house);
    require(!old.usesOriginalRules() && old.attackTriggered && old.scenarioTeams.empty(), "9885 controller changed modes");
    require(oldInput.readUint32() == 0x12345678, "9885 load consumed the following save section");
    game.loadedSavegameVersion = previousVersion;
    SDL_Log("ORIGINAL_CAMPAIGN_PASS: produced Hunt, authored guards/teams, five-slot rebuild, scoped free upgrades, new and legacy serialization");
}
