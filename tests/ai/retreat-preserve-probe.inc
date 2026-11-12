// Real-engine fixture selected by BALANCE_RETREAT_PRESERVE_ONLY in the recovery probe.
// No observer calls policy predicates; these are direct integration assertions.
#include <structures/RepairYard.h>
#include <units/Harvester.h>

void runRetreatPreserveProbe(Game& game) {
    auto require = [](bool ok, const char* why) {
        if (!ok) { SDL_Log("RETREAT_PRESERVE_FAIL: %s", why); throw std::runtime_error(why); }
    };
    auto* owner = game.getHouse(HOUSE_HARKONNEN);
    auto* enemy = game.getHouse(HOUSE_ATREIDES);
    require(owner && enemy && owner->getTeamID() != enemy->getTeamID(), "Missing opposing fixture houses");
    QuantBot* bot = nullptr;
    for (const auto& player : owner->getPlayerList())
        if (auto* candidate = dynamic_cast<QuantBot*>(player.get())) bot = candidate;
    require(bot && !bot->isAlliedWithHuman(), "Retreat fixture requires a houseless all-AI roster");
    const auto savedUnits = unitList;
    const auto savedStructures = structureList;
    const auto savedConfig = bot->configOverride;
    const auto savedType = game.gameType;
    const auto savedMode = bot->gameMode;
    const auto savedRole = bot->role;
    const auto savedDifficulty = bot->difficulty;
    const auto savedVersion = game.loadedSavegameVersion;
    const bool savedCitySim = game.citySimEnabled_;
    auto& init = const_cast<GameInitSettings&>(game.getGameInitSettings());
    const std::string savedMod = init.getModName(), savedChecksum = init.getModChecksum();
    const auto savedStarting = owner->startingCredits;
    const auto savedStored = owner->storedCredits;
    const auto savedCityCredits = owner->cityCredits;
    auto& data = game.objectData.data;
    const int ownPrice = data[Unit_Tank][owner->getHouseID()].price;
    const int enemyPrice = data[Unit_Tank][enemy->getHouseID()].price;
    std::vector<Uint32> objects;
    Coord origin = Coord::Invalid();
    for (int y = 2; y + 26 < currentGameMap->getSizeY() && origin.isInvalid(); ++y)
        for (int x = 2; x + 42 < currentGameMap->getSizeX() && origin.isInvalid(); ++x) {
            bool clear = true;
            for (int dy = 0; dy < 26 && clear; ++dy) for (int dx = 0; dx < 42 && clear; ++dx) {
                const auto* tile = currentGameMap->getTile(x + dx, y + dy);
                if (tile->hasAGroundObject() || tile->isMountain()) clear = false;
            }
            if (clear) origin = Coord(x, y);
        }
    require(origin.isValid(), "Retreat fixture needs a clear 42x26 area");
    auto cleanup = [&] {
        for (Uint32 id : objects) if (auto* object = game.getObjectManager().getObject(id)) {
            object->setTarget(nullptr);
            currentGameMap->removeObjectFromMap(id);
            object->setHealth(0);
        }
        unitList = savedUnits;
        structureList = savedStructures;
        bot->configOverride = savedConfig;
        bot->gameMode = savedMode; bot->role = savedRole; bot->difficulty = savedDifficulty;
        game.gameType = savedType; game.loadedSavegameVersion = savedVersion;
        game.citySimEnabled_ = savedCitySim; init.setModIdentity(savedMod, savedChecksum);
        owner->startingCredits = savedStarting;
        owner->storedCredits = savedStored; owner->cityCredits = savedCityCredits;
        data[Unit_Tank][owner->getHouseID()].price = ownPrice;
        data[Unit_Tank][enemy->getHouseID()].price = enemyPrice;
    };
    auto spawn = [&](House* house, Uint32 item, Coord offset) {
        auto* unit = house->createUnit(item);
        require(unit != nullptr, "Could not create retreat fixture unit");
        objects.push_back(unit->getObjectID());
        unit->deploy(origin + offset);
        unit->setVisible(VIS_ALL, true);
        return unit;
    };
    auto place = [&](Uint32 item, Coord offset) {
        auto* structure = owner->placeStructure(NONE_ID, item, origin.x + offset.x, origin.y + offset.y, true, true);
        require(structure != nullptr, "Could not place retreat fixture structure");
        objects.push_back(structure->getObjectID());
        return structure;
    };
    auto config = std::make_shared<QuantBotConfig>(bot->botConfig());
    bot->configOverride = config;
    game.gameType = GameType::CustomGame;
    bot->gameMode = QuantBot::GameMode::Custom;
    bot->role = AIRole::Both;
    bot->difficulty = QuantBot::Difficulty::Brutal;
    bot->manualUnitOrders.clear(); bot->defenceAssignments.clear();
    bot->forceVariantOverride = 0;
    config->recovery.enabled = true;
    config->recovery.withdrawalsEnabled = false;
    config->waveFormation = QuantBotConfig::kWaveFormationNone;
    data[Unit_Tank][owner->getHouseID()].price = 300;
    data[Unit_Tank][enemy->getHouseID()].price = 300;
    unitList.clear(); structureList.clear();
    try {
        place(Structure_ConstructionYard, Coord(2, 2));
        auto* repair = static_cast<RepairYard*>(place(Structure_RepairYard, Coord(6, 2)));
        auto* first = spawn(owner, Unit_Tank, Coord(28, 10));
        auto* second = spawn(owner, Unit_Tank, Coord(29, 11));
        std::vector<UnitBase*> hostile;
        for (int i = 0; i < 4; ++i) {
            auto* unit = spawn(enemy, Unit_Tank, Coord(32 + i, 12));
            unit->setVisible(owner->getTeamID(), false);
            hostile.push_back(unit);
        }
        hostile.back()->active = false;
        auto resetWave = [&] {
            bot->customWave.members = {first->getObjectID(), second->getObjectID()};
            bot->customWave.front = NONE_ID;
            bot->customWave.launched = game.getGameCycleCount();
            bot->legacyHuntersAdopted = true;
            bot->armyPosture = ArmyPosturePolicy::Posture::Offensive;
            bot->postureSince = game.getGameCycleCount();
            bot->localPressureSince = bot->localWithdrawSince = std::numeric_limits<Uint32>::max();
            bot->localSquadWithdraw = false;
            bot->attrition.reset();
            bot->dangerUpdated = bot->protectedRallyCycle = std::numeric_limits<Uint32>::max();
            bot->protectedRally = Coord::Invalid();
            bot->squadRallyLocation = origin + Coord(4, 8);
            for (auto* unit : {first, second}) {
                unit->setForced(false);
                bot->doSetAttackMode(unit, HUNT);
                unit->setTarget(hostile.front());
            }
        };
        config->force.retreatPreserve = false;
        resetWave();
        bot->updateArmyPosture();
        require(bot->armyPosture == ArmyPosturePolicy::Posture::Offensive, "OFF enabled withdrawal");
        const auto targetBefore = first->target.getObjectID();
        const auto destinationBefore = first->getDestination();
        int budget = 12;
        require(!bot->preserveCombatUnit(first, budget) && budget == 12
            && first->target.getObjectID() == targetBefore && first->getDestination() == destinationBefore,
            "OFF preservation issued or claimed an order");
        config->force.retreatPreserve = true;
        for (auto difficulty : {QuantBot::Difficulty::Easy, QuantBot::Difficulty::Medium,
                QuantBot::Difficulty::Hard, QuantBot::Difficulty::Brutal, QuantBot::Difficulty::Defend}) {
            bot->difficulty = difficulty;
            require(bot->retreatPreserveActive(), "Skirmish difficulty lost preservation authority");
        }
        // Gate-only control: the real House power predicate must not become an
        // AI authority check. Preserve loaded content; vary only its power-rule
        // receipt, then restore it before any unit/repair policy is exercised.
        game.citySimEnabled_ = false;
        init.setModIdentity("Dune2R", savedChecksum);
        require(owner->isPowerRequired() && bot->retreatPreserveActive(),
            "Powered non-city skirmish lost preservation authority");
        config->force.retreatPreserve = false;
        require(!bot->retreatPreserveActive(), "Powered skirmish bypassed the OFF switch");
        config->force.retreatPreserve = true;
        game.citySimEnabled_ = savedCitySim; init.setModIdentity(savedMod, savedChecksum);
        bot->difficulty = QuantBot::Difficulty::Brutal;
        config->force.retreatPreserveLevel = 1;
        resetWave();
        bot->updateArmyPosture();
        require(bot->lastSurvey.squadHostile.ground == 900 * CombatPowerPolicy::kPowerScale,
            "True local survey missed hidden enemies or counted inactive cargo");
        require(bot->lastSurvey.squadSupport.ground == 600 * CombatPowerPolicy::kPowerScale,
            "True local survey counted friendly force more than once");
        require(bot->armyPosture == ArmyPosturePolicy::Posture::Withdrawing, "Level 1 missed exact 1.5x boundary");
        budget = 12; bot->applyArmyPosture(budget);
        for (auto* unit : {first, second}) require(unit->getAttackMode() == AREAGUARD && unit->wasForced()
            && blockDistance(unit->getDestination(), bot->protectedRally).lround() <= bot->armyAssemblyRadius(),
            "Ratio withdrawal did not issue ordinary protected-rally movement");
        const Coord withdrawal = first->getDestination();
        bot->checkAllUnits();
        bot->onDamage(first, 1, hostile.front()->getObjectID());
        require(first->getDestination() == withdrawal && first->wasForced(),
            "Unit control or damage callback rewrote the withdrawal");
        OMemoryStream out; out.open(); bot->save(out);
        const std::string bytes(out.getData(), out.getDataLength());
        IMemoryStream input(bytes.data(), bytes.size());
        game.loadedSavegameVersion = SAVEGAMEVERSION;
        QuantBot restored(input, owner); restored.configOverride = config;
        require(restored.armyPosture == bot->armyPosture && restored.localWithdrawSince == bot->localWithdrawSince
            && restored.customWave.members == bot->customWave.members, "Existing saved withdrawal state did not round-trip");
        budget = 12;
        require(restored.preserveCombatUnit(first, budget) && first->getDestination() == withdrawal,
            "Reloaded preservation did not retain the saved movement order");
        config->force.retreatPreserveLevel = 2;
        resetWave(); bot->updateArmyPosture();
        require(bot->armyPosture == ArmyPosturePolicy::Posture::Offensive, "Level 2 retreated at 1.5x");
        hostile.back()->active = true;
        hostile.back()->setHealth(hostile.back()->getMaxHealth() - 1);
        resetWave(); bot->updateArmyPosture();
        require(bot->armyPosture == ArmyPosturePolicy::Posture::Offensive, "Level 2 retreated below 2x");
        hostile.back()->setHealth(hostile.back()->getMaxHealth());
        resetWave(); bot->updateArmyPosture();
        require(bot->armyPosture == ArmyPosturePolicy::Posture::Withdrawing, "Level 2 missed exact 2x boundary");
        resetWave(); bot->customWave.members.clear(); bot->updateArmyPosture();
        require(bot->armyPosture == ArmyPosturePolicy::Posture::Withdrawing && bot->customWave.members.empty(),
            "A non-wave raider fight escaped preservation or had its membership stolen");
        resetWave();
        // Distant allies are not force at this fight; a nearby ally really is.
        House* ally = nullptr;
        for (int id = 0; id < NUM_HOUSES; ++id) if (auto* house = game.getHouse(id))
            if (house != owner && house->getTeamID() == owner->getTeamID()) { ally = house; break; }
        require(ally != nullptr, "Fixture map lost its allied house");
        auto* supporter = spawn(ally, Unit_Tank, Coord(27, 11));
        auto nearby = bot->surveyArmy().squadSupport.ground;
        currentGameMap->removeObjectFromMap(supporter->getObjectID()); supporter->deploy(origin + Coord(2, 22));
        require(bot->surveyArmy().squadSupport.ground < nearby, "Distant ally inflated local force");
        // Pull a 79%-health vehicle back through the actual booking and paid bay.
        bot->armyPosture = ArmyPosturePolicy::Posture::Offensive; bot->localSquadWithdraw = false;
        auto* damaged = static_cast<GroundUnit*>(spawn(owner, Unit_Tank, Coord(10, 6)));
        damaged->setHealth(FixPoint(damaged->getMaxHealth()) * 79 / 100);
        config->force.retreatPreserve = false;
        require(!bot->reserveDamagedUnitForRepair(damaged), "OFF changed the native repair reservation");
        config->force.retreatPreserve = true;
        require(bot->reserveDamagedUnitForRepair(damaged), "Early repair reservation missed damaged vehicle");
        bot->onHumanUnitOrder(damaged->getObjectID()); budget = 12;
        require(!bot->preserveCombatUnit(damaged, budget), "Preservation replaced a human order");
        bot->manualUnitOrders.erase(damaged->getObjectID());
        budget = 12;
        require(bot->preserveCombatUnit(damaged, budget) && damaged->target.getObjectID() == repair->getObjectID()
            && damaged->goingToRepairYard, "Early repair did not book an actual repair-yard trip");
        damaged->setHealth(FixPoint(damaged->getMaxHealth()) * 9 / 10);
        require(bot->reserveDamagedUnitForRepair(damaged), "Repair ownership ended before full health");
        bot->customWave.members.insert(damaged->getObjectID()); bot->updateCustomWave(); bot->checkAllUnits();
        require(!bot->customWave.members.count(damaged->getObjectID())
            && damaged->target.getObjectID() == repair->getObjectID(), "Wave or unit scan stole repair ownership");
        damaged->setHealth(damaged->getMaxHealth() - 2); damaged->setGettingRepaired();
        damaged->setGuardPoint(repair->getLocation());
        require(!damaged->isActive() && repair->getContainedRepairUnit(), "Repair trip could not enter its actual bay");
        owner->startingCredits = 1000; owner->storedCredits = 0; owner->cityCredits = 0;
        const auto cashBefore = owner->getStartingCredits() + owner->getEarnedCredits();
        for (int tick = 0; tick < 4; ++tick) repair->updateStructureSpecificStuff();
        require(damaged->getHealth() == damaged->getMaxHealth() && damaged->isActive()
            && owner->getStartingCredits() + owner->getEarnedCredits() < cashBefore,
            "Actual repair did not heal, debit and release the unit");
        require(!bot->reserveDamagedUnitForRepair(damaged), "Fully repaired vehicle stayed reserved");
        // Reuse existing field relocation/clearing; no return/refinery policy changes.
        auto* worker = static_cast<Harvester*>(spawn(owner, Unit_Harvester, Coord(30, 20)));
        auto* fieldEnemy = spawn(enemy, Unit_Tank, Coord(35, 20));
        auto* escort = spawn(owner, Unit_SiegeTank, Coord(21, 20));
        fieldEnemy->setVisible(owner->getTeamID(), false);
        unitList.clear(); unitList.push_back(worker); unitList.push_back(fieldEnemy); unitList.push_back(escort);
        bot->customWave.members.clear(); bot->defenceAssignments.clear(); bot->defenceResponseCycles.clear();
        const Coord field = worker->getLocation(), alternative = origin + Coord(8, 20);
        currentGameMap->getTile(field)->setSpice(1000); currentGameMap->getTile(alternative)->setSpice(1000);
        auto fieldCheck = [&] {
            worker->setTarget(nullptr); worker->setAmountOfSpice(0);
            bot->doSetAttackMode(worker, HARVEST); bot->doMove2Pos(worker, field.x, field.y, false);
            bot->unsafeFields.clear(); bot->harvesterSafety.clear();
            bot->harvesterSafety[worker->getObjectID()].lastLocation = field;
            bot->dangerUpdated = std::numeric_limits<Uint32>::max(); bot->refreshTacticalDanger();
            return bot->manageHarvesterSafety(worker);
        };
        config->force.retreatPreserve = false;
        require(!fieldCheck() && worker->getDestination() == field, "OFF detected an unseen approaching field attacker");
        config->force.retreatPreserve = true;
        require(fieldCheck() && worker->getDestination() != field, "ON failed to evacuate an unseen threatened field");
        const Coord safe = worker->getDestination();
        require(safe.isValid() && bot->harvesterDanger[safe.y * currentGameMap->getSizeX() + safe.x] == 0,
            "Evacuated worker was sent to a true threat");
        fieldEnemy->setVisible(owner->getTeamID(), true); bot->defenceResponseCycles.clear();
        fieldCheck();
        require(bot->defenceAssignments.count(escort->getObjectID()), "Approaching field attacker did not receive bounded clearing support");
        // Both representations of campaign authority must be inert even when ON.
        for (bool campaignType : {false, true}) {
            game.gameType = campaignType ? GameType::Campaign : GameType::CustomGame;
            bot->gameMode = campaignType ? QuantBot::GameMode::Custom : QuantBot::GameMode::Campaign;
            require(!bot->retreatPreserveActive(), "Preservation leaked into campaign authority");
            budget = 12;
            require(!bot->preserveCombatUnit(escort, budget) && budget == 12, "Campaign preservation issued orders");
        }
        game.gameType = GameType::CustomGame; bot->gameMode = QuantBot::GameMode::Custom;
        for (AIRole role : {AIRole::Builder, AIRole::Off}) {
            bot->role = role;
            require(!bot->retreatPreserveActive(), "Preservation escaped Builder/Off authority");
        }
        SDL_Log("RETREAT_PRESERVE_PASS: exact 1.5x/2x, hidden/inactive enemies, local ally, saved orders, paid repair, field withdrawal/clearing, human/roles/campaign");
    } catch (...) { cleanup(); throw; }
    cleanup();
}
