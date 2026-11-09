// Real-engine fixture for SpiceBloomHarvest (docs/ai-scenario-matrix.md, "Spice bloom harvest"),
// selected by BALANCE_SPICE_BLOOM_PROBE (run-campaign-balance.py --spice-bloom-probe). The
// Atreides QuantBot (alone on its team in "All against Atreides"; Harkonnen is the human's ally)
// plays Custom Brutal on a cleared sand area with a refinery, an off-roster harvester and a
// Soldier, a Trike and a Tank. Production code decides and the real units walk, fire and burst:
//   A. an open-sand bloom: the cheapest unit is ordered at both levels, never stands on the bloom,
//      survives, and the bloom becomes 81 thin-spice tiles;
//   B. a bloom inside that new field is skipped as wasteful (counted once), at both levels;
//   C. a bloom beside 500 credits of spice is skipped at level 1 and burst at level 2;
// plus the gates: OFF, campaign and Easy are inert, the cadence acts on every fourth pass only,
// and a nearer enemy building is respected only once our team has explored it.
#include <players/QuantBotSpiceBloomPolicy.h>

void runSpiceBloomProbe(Game& game) {
    namespace Policy = QuantBotSpiceBloomPolicy;
    auto require = [](bool ok, const char* why) {
        if (!ok) { SDL_Log("SPICE_BLOOM_FAIL: %s", why); throw std::runtime_error(why); }
    };
    auto* owner = game.getHouse(HOUSE_ATREIDES);
    require(owner != nullptr, "Missing the Atreides fixture house");
    House* enemy = nullptr;
    for (int h = 0; h < NUM_HOUSES && !enemy; ++h)
        if (auto* house = game.getHouse(h); house && house->getTeamID() != owner->getTeamID()) enemy = house;
    require(enemy != nullptr, "Missing an opposing fixture house");
    QuantBot* bot = nullptr;
    for (const auto& player : owner->getPlayerList())
        if (auto* candidate = dynamic_cast<QuantBot*>(player.get())) bot = candidate;
    require(bot && !bot->isAlliedWithHuman(), "The spice bloom fixture needs the unallied Atreides QuantBot");
    const int team = owner->getTeamID();

    const auto savedUnits = unitList;
    const auto savedStructures = structureList;
    const auto savedConfig = bot->configOverride;
    const auto savedType = game.gameType;
    const auto savedMode = bot->gameMode;
    const auto savedRole = bot->role;
    const auto savedDifficulty = bot->difficulty;
    std::vector<Uint32> objects;

    // A 34x26 area free of objects and mountains; it is turned into plain sand.
    constexpr int kWidth = 34, kHeight = 26;
    Coord origin = Coord::Invalid();
    for (int y = 2; y + kHeight + 2 < currentGameMap->getSizeY() && origin.isInvalid(); ++y)
        for (int x = 2; x + kWidth + 2 < currentGameMap->getSizeX() && origin.isInvalid(); ++x) {
            bool clear = true;
            for (int dy = -1; dy <= kHeight && clear; ++dy) for (int dx = -1; dx <= kWidth && clear; ++dx) {
                const auto* tile = currentGameMap->getTile(x + dx, y + dy);
                if (tile->hasAnObject() || tile->hasAStructure() || tile->isMountain()) clear = false;
            }
            if (clear) origin = Coord(x, y);
        }
    require(origin.isValid(), "The spice bloom fixture needs a clear 34x26 area");
    const Coord bloom = origin + Coord(15, 12);
    const Coord refineryAt = origin + Coord(1, 1);

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
        game.gameType = savedType;
    };
    auto advance = [&] { game.processObjects(); ++game.gameCycleCount; };
    auto config = std::make_shared<QuantBotConfig>(bot->botConfig());
    bot->configOverride = config;
    game.gameType = GameType::CustomGame;
    bot->gameMode = QuantBot::GameMode::Custom;
    bot->role = AIRole::Both;
    bot->difficulty = QuantBot::Difficulty::Brutal;
    bot->manualUnitOrders.clear(); bot->defenceAssignments.clear();
    bot->tacticalDanger.clear();
    unitList.clear(); structureList.clear();
    try {
        // Only the fixture's blooms exist: every other one on the map becomes plain sand.
        for (int x = 0; x < currentGameMap->getSizeX(); ++x)
            for (int y = 0; y < currentGameMap->getSizeY(); ++y) {
                auto* tile = currentGameMap->getTile(x, y);
                if (tile->isSpiceBloom() || tile->isSpecialBloom()) tile->setSpice(0);
            }
        auto resetArea = [&] {
            for (int dy = 0; dy < kHeight; ++dy) for (int dx = 0; dx < kWidth; ++dx) {
                auto* tile = currentGameMap->getTile(origin.x + dx, origin.y + dy);
                tile->setType(Terrain_Sand);
                tile->setSpice(0);
                tile->setExplored(owner->getHouseID(), game.getGameCycleCount());
            }
        };
        resetArea();
        auto* refinery = owner->placeStructure(NONE_ID, Structure_Refinery, refineryAt.x, refineryAt.y, true, true);
        require(refinery != nullptr && refinery->acceptsHarvesterDropoff(), "Could not place the fixture refinery");
        objects.push_back(refinery->getObjectID());
        // Units a refinery delivers on placement are not part of the fixture.
        std::vector<UnitBase*> delivered;
        for (auto* unit : unitList) delivered.push_back(unit);
        for (auto* unit : delivered) {
            objects.push_back(unit->getObjectID());
            currentGameMap->removeObjectFromMap(unit->getObjectID());
            unitList.remove(unit);
        }
        auto spawn = [&](House* house, Uint32 item, Coord at) {
            auto* unit = house->createUnit(item);
            require(unit != nullptr, "Could not create a spice bloom fixture unit");
            objects.push_back(unit->getObjectID());
            unit->deploy(at);
            unit->setVisible(VIS_ALL, true);
            return unit;
        };
        // An off-roster harvester: the house has one, and it never walks into the fixture.
        auto* harvester = spawn(owner, Unit_Harvester, origin + Coord(1, 5));
        unitList.remove(harvester);
        require(bot->getAIHarvesterCount() > 0, "The fixture house has no harvester");

        std::vector<UnitBase*> shooters;
        auto respawnShooters = [&] {
            for (auto* unit : shooters) {
                currentGameMap->removeObjectFromMap(unit->getObjectID());
                unitList.remove(unit);
            }
            shooters.clear();
            int dy = -2;
            for (Uint32 item : {Unit_Tank, Unit_Soldier, Unit_Trike}) {
                auto* unit = spawn(owner, item, bloom + Coord(-8, dy));
                bot->doSetAttackMode(unit, GUARD);
                shooters.push_back(unit);
                dy += 2;
            }
        };
        auto price = [&](const UnitBase* unit) {
            return game.objectData.data[unit->getItemID()][unit->getProductionHouseID()].price;
        };
        auto placeBloom = [&](Coord at) {
            currentGameMap->getTile(at)->setType(Terrain_SpiceBloom);
            bot->spiceBloomSites.clear();
            bot->spiceBloomSitesScanned = false;
        };
        auto ordered = [&]() -> const UnitBase* {
            const UnitBase* found = nullptr;
            int count = 0;
            for (auto* unit : shooters) if (unit->getAttackPos().isValid()) { found = unit; ++count; }
            require(count <= 1, "More than one shooter was ordered at once");
            return found;
        };
        auto spiceTiles = [&](Coord at, int& inside, int& outside, int64_t& credits) {
            inside = outside = 0; credits = 0;
            for (int dy = 0; dy < kHeight; ++dy) for (int dx = 0; dx < kWidth; ++dx) {
                const Coord c = origin + Coord(dx, dy);
                const auto* tile = currentGameMap->getTile(c);
                if (!tile->isSpice()) continue;
                credits += tile->getSpice().lround();
                if (distanceFrom(at, c) <= Policy::kSpillRadius) ++inside; else ++outside;
            }
        };
        const QuantBot::HarvesterRaidPlan noRaid{};

        // Gates, with an open bloom and idle shooters: OFF, campaign and Easy never order.
        placeBloom(bloom);
        respawnShooters();
        while (Policy::evaluationDue(game.getGameCycleCount())) advance();
        while (!Policy::evaluationDue(game.getGameCycleCount())) advance();
        const auto statsBefore = bot->spiceBloomStats;
        config->force.spiceBloomHarvest = 0;
        require(!bot->spiceBloomHarvestScope(), "OFF was in scope");
        bot->updateSpiceBloomHarvest(noRaid);
        require(!ordered(), "OFF ordered a shot");
        config->force.spiceBloomHarvest = 2;
        game.gameType = GameType::Campaign;
        require(!bot->spiceBloomHarvestScope(), "A campaign game was in scope");
        bot->updateSpiceBloomHarvest(noRaid);
        require(!ordered(), "A campaign game ordered a shot");
        game.gameType = GameType::CustomGame;
        bot->difficulty = QuantBot::Difficulty::Easy;
        require(!bot->spiceBloomHarvestScope(), "Custom Easy was in scope");
        bot->difficulty = QuantBot::Difficulty::Medium;
        require(bot->spiceBloomHarvestScope(), "Custom Medium was out of scope");
        bot->difficulty = QuantBot::Difficulty::Brutal;
        require(bot->spiceBloomStats.orders == statsBefore.orders && bot->spiceBloomStats.wasteful == statsBefore.wasteful,
            "An inert gate counted an order or a wasted bloom");

        // A nearer enemy building: respected once explored by our team, ignored while unexplored.
        auto* silo = enemy->placeStructure(NONE_ID, Structure_Silo, bloom.x + 7, bloom.y, true, true);
        require(silo != nullptr, "Could not place the enemy fixture building");
        objects.push_back(silo->getObjectID());
        require(!bot->orderSpiceBloomShot(noRaid) && !ordered(), "A bloom nearer an explored enemy building was ordered");
        for (int h = 0; h < NUM_HOUSES; ++h)
            if (const auto* house = game.getHouse(h); house && house->getTeamID() == team)
                currentGameMap->getTile(silo->getLocation())->explored[h] = false;
        require(bot->orderSpiceBloomShot(noRaid) && ordered(), "An unexplored enemy building blocked the bloom");
        currentGameMap->removeObjectFromMap(silo->getObjectID());
        structureList.remove(silo);

        // A: an open-sand bloom at each level, through the unit-pass hook on its cadence.
        for (int level : {1, 2}) {
            resetArea();
            placeBloom(bloom);
            respawnShooters();
            config->force.spiceBloomHarvest = level;
            while (Policy::evaluationDue(game.getGameCycleCount())) advance();
            bot->updateSpiceBloomHarvest(noRaid);
            require(!ordered(), "The hook ordered off its cadence");
            while (!Policy::evaluationDue(game.getGameCycleCount())) advance();
            const Uint32 ordersBefore = bot->spiceBloomStats.orders;
            bot->updateSpiceBloomHarvest(noRaid);
            const UnitBase* shooter = ordered();
            require(shooter && shooter->getAttackPos() == bloom && shooter->wasForced()
                && bot->spiceBloomStats.orders == ordersBefore + 1, "The open-sand bloom was not ordered");
            for (const auto* unit : shooters)
                require(price(shooter) <= price(unit), "A dearer unit was sent");
            const Uint32 shooterId = shooter->getObjectID();
            const Uint32 start = game.getGameCycleCount();
            int cycles = -1;
            for (int step = 0; step < 6000; ++step) {
                if (!currentGameMap->getTile(bloom)->isSpiceBloom()) { cycles = step; break; }
                require(shooter->getLocation() != bloom, "The shooter stood on the bloom");
                advance();
            }
            require(cycles >= 0, "The ordered shooter never burst the bloom");
            for (int step = 0; step < 100; ++step) advance();
            const auto* survivor = game.getObjectManager().getObject(shooterId);
            require(survivor && survivor->getHealth() > 0 && survivor->getLocation() != bloom,
                "The shooter did not survive the burst");
            int inside = 0, outside = 0; int64_t credits = 0;
            spiceTiles(bloom, inside, outside, credits);
            require(inside == 81 && outside == 0, "The open-sand bloom did not become 81 spice tiles");
            SDL_Log("SPICE_BLOOM_CASE: case=A level=%d shooter=%s price=%d cycles=%d start=%u spice_tiles=%d credits=%lld"
                " shooter_health=%d distance=%d",
                level, getItemNameByID(shooter->getItemID()).c_str(), price(shooter), cycles, start, inside,
                static_cast<long long>(credits), survivor->getHealth().lround(),
                blockDistance(survivor->getLocation(), bloom).lround());
        }

        // B: a bloom inside the field the last burst made is wasteful at both levels, counted once.
        const Coord inner = bloom + Coord(2, 0);
        placeBloom(inner);
        respawnShooters();
        for (int level : {1, 2}) {
            config->force.spiceBloomHarvest = level;
            bot->spiceBloomSites.clear(); bot->spiceBloomSitesScanned = false;
            const auto before = bot->spiceBloomStats;
            require(!bot->orderSpiceBloomShot(noRaid) && !ordered() && bot->spiceBloomStats.wasteful == before.wasteful + 1
                && bot->spiceBloomStats.orders == before.orders, "A bloom inside a field was not skipped as wasteful");
            require(!bot->orderSpiceBloomShot(noRaid) && bot->spiceBloomStats.wasteful == before.wasteful + 1,
                "A wasteful bloom was counted twice");
            int inside = 0, outside = 0; int64_t credits = 0;
            spiceTiles(inner, inside, outside, credits);
            SDL_Log("SPICE_BLOOM_CASE: case=B level=%d skipped=1 known_spice=%lld wasteful=%u orders=%u",
                level, static_cast<long long>(credits), bot->spiceBloomStats.wasteful, bot->spiceBloomStats.orders);
        }
        currentGameMap->getTile(inner)->setSpice(0);

        // C: 500 credits of spice in the margin ring: level 1 skips, level 2 bursts.
        resetArea();
        for (int dy = 0; dy < 5; ++dy) currentGameMap->getTile(bloom + Coord(6, dy - 2))->setSpice(100);
        placeBloom(bloom);
        respawnShooters();
        config->force.spiceBloomHarvest = 1;
        const auto lightBefore = bot->spiceBloomStats;
        require(!bot->orderSpiceBloomShot(noRaid) && !ordered() && bot->spiceBloomStats.wasteful == lightBefore.wasteful + 1,
            "Level 1 burst a bloom beside known spice");
        SDL_Log("SPICE_BLOOM_CASE: case=C level=1 skipped=1 known_spice=500");
        config->force.spiceBloomHarvest = 2;
        require(bot->orderSpiceBloomShot(noRaid) && bot->spiceBloomStats.orders == lightBefore.orders + 1,
            "Level 2 skipped a bloom beside 500 credits of spice");
        const UnitBase* light = ordered();
        require(light && light->getAttackPos() == bloom, "Level 2 ordered the wrong position");
        const Uint32 lightId = light->getObjectID();
        int lightCycles = -1;
        for (int step = 0; step < 6000; ++step) {
            if (!currentGameMap->getTile(bloom)->isSpiceBloom()) { lightCycles = step; break; }
            require(light->getLocation() != bloom, "The level 2 shooter stood on the bloom");
            advance();
        }
        require(lightCycles >= 0, "The level 2 shooter never burst the bloom");
        for (int step = 0; step < 100; ++step) advance();
        const auto* lightSurvivor = game.getObjectManager().getObject(lightId);
        require(lightSurvivor && lightSurvivor->getHealth() > 0, "The level 2 shooter did not survive the burst");
        int inside = 0, outside = 0; int64_t credits = 0;
        spiceTiles(bloom, inside, outside, credits);
        require(inside == 81 && outside == 5, "The lightly spiced bloom did not become 81 spice tiles");
        SDL_Log("SPICE_BLOOM_CASE: case=C level=2 shooter=%s cycles=%d spice_tiles=%d credits=%lld shooter_health=%d",
            getItemNameByID(light->getItemID()).c_str(), lightCycles, inside, static_cast<long long>(credits),
            lightSurvivor->getHealth().lround());
        SDL_Log("SPICE_BLOOM_PASS: mod=%s orders=%u wasteful=%u (open bloom L1/L2 burst to 81 tiles by the cheapest unit,"
            " field bloom wasteful, light spice L1 skip/L2 burst, OFF/campaign/Easy inert, cadence, enemy side)",
            game.getGameInitSettings().getModName().c_str(), bot->spiceBloomStats.orders, bot->spiceBloomStats.wasteful);
    } catch (...) { cleanup(); throw; }
    cleanup();
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=spice_bloom outcome=passed");
}
