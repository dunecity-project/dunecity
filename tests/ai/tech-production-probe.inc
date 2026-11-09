// Private balance executable only. Every asserted technology order is selected
// by full QuantBot::build(), paid over ordinary engine cycles, and (for a
// structure) placed by the ordinary AI. Initial scenario assets and explicitly
// labelled queued-commitment controls are not claimed as bot production.
#include <Command.h>
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <Map.h>
#include <TechResearch.h>
#include <dunecity/CityConstants.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/CityStructurePopulation.h>
#include <mod/ModManager.h>
#include <players/HumanPlayer.h>
#include <Network/NetworkPacketTypes.h>
#include <players/QuantBot.h>
#include <players/QuantBotBuildPass.h>
#include <players/QuantBotBuildPolicy.h>
#include <players/QuantBotConfig.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <structures/ConstructionYard.h>
#include <structures/ZoneStructure.h>
#include <units/Harvester.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace TechProductionProbe {

inline void require(bool ok, const char* reason) {
    if (!ok) {
        SDL_Log("TECH_SAVING_FAIL: %s", reason);
        throw std::runtime_error(reason);
    }
}

constexpr std::array<Uint32, 10> vehicles = {
    Unit_Tank, Unit_SiegeTank, Unit_Launcher, Unit_Devastator, Unit_SonicTank,
    Unit_Deviator, Unit_Ornithopter, Unit_Trike, Unit_RaiderTrike, Unit_Quad
};
constexpr std::array<Uint32, 12> technology = {
    Structure_StarPort, Structure_HeavyFactory, Structure_RepairYard,
    Structure_HighTechFactory, Structure_IX, Structure_Palace, Structure_LightFactory,
    Structure_ResearchLab, Structure_WindTrap, Structure_Refinery, Structure_Silo, Structure_NuclearPlant
};

inline bool light(Uint32 item) {
    return item == Unit_Trike || item == Unit_RaiderTrike || item == Unit_Quad;
}

inline std::string mapText(HOUSETYPE side, int techLevel = 9) {
    std::ostringstream map;
    map << "[BASIC]\nVersion=2\nWinFlags=3\nLoseFlags=1\nTechLevel=" << techLevel
        << "\n[MAP]\nSizeX=64\nSizeY=64\n";
    for (int y = 0; y < 64; ++y) {
        char row[16];
        std::snprintf(row, sizeof(row), "%03d=", y);
        map << row << std::string(64, '%') << '\n';
    }
    const HOUSETYPE opposite = side == HOUSE_HARKONNEN ? HOUSE_ATREIDES : HOUSE_HARKONNEN;
    const auto defender = getHouseNameByNumber(side);
    const auto attacker = getHouseNameByNumber(opposite);
    // Generic Player1/2 sections are randomly assigned by INIMapLoader. Named
    // houses keep the bot's yard with its scenario economy for every seed.
    map << '[' << defender << "]\nCredits=100000\nBrain=Team1\n"
        << '[' << attacker << "]\nCredits=100000\nBrain=Team2\n"
        << "[STRUCTURES]\nID000=" << defender << ",Const Yard,256," << 8 + 8 * 64 << '\n'
        << "ID001=" << attacker << ",Const Yard,256," << 56 + 54 * 64 << '\n';
    return map.str();
}

enum class Stage { Progression, FirstHeavy, Advanced, FirstStarport, FirstPalace, Complete,
                   NaturalOpening, NaturalResearch };
enum class Army { Empty, BelowValueFloor, ReadyLight, TinyMixed, TinyTank };

struct World {
    std::unique_ptr<Game> game;
    House* owner = nullptr;
    House* enemy = nullptr;
    QuantBot* bot = nullptr;
    HumanPlayer* pilot = nullptr;
    ConstructionYard* yard = nullptr;
    std::string configHash;
    std::vector<BuilderBase*> lights;
    std::vector<Uint32> initialArmy;
    int fullTurns = 0;

    World(const char* mod, const char* playerClass, bool enabled, Stage stage,
          Army army = Army::ReadyLight, int workers = 8, bool causalControlsOff = false,
          HOUSETYPE side = HOUSE_HARKONNEN, int carryalls = 3, int unitLimit = -1) {
        require(currentGame == nullptr, "Fixture worlds share live engine globals");
        require(ModManager::instance().setActiveMod(mod), "Cannot activate savings fixture mod");
        getQuantBotConfig().strategy.random = false;
        getQuantBotConfig().strategy.fixedMix = QuantBotStrategy::Mix::Learned;
        getQuantBotConfig().strategy.fixedTempo = QuantBotStrategy::Tempo::Base;
        auto options = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
        options.fogOfWar = false;
        options.startWithExploredMap = true;
        options.immortalHumanPlayer = true;
        options.instantBuild = false;
        options.concreteRequired = false;
        options.researchLab = stage == Stage::NaturalResearch;
        if (options.researchLab) options.researchPace = TechResearch::Pace::Normal;
        options.starportPopulation = true;
        options.onlyOnePalace = true; // This fixture measures the first legal one-off, not repeat Palace capacity.
        options.maximumNumberOfHarvestersOverride = 8;
        options.maximumNumberOfUnitsOverride = unitLimit;
        GameInitSettings init("tech-saving.ini", mapText(side, options.researchLab ? 1 : 9), true, options);
        init.randomSeed = static_cast<Uint32>(std::stoul(std::getenv("BALANCE_SEED")));
        GameInitSettings::HouseInfo defender(side, 1);
        defender.addPlayerInfo({"Technology savings QuantBot", playerClass});
        init.addHouseInfo(defender);
        const HOUSETYPE opposite = side == HOUSE_HARKONNEN ? HOUSE_ATREIDES : HOUSE_HARKONNEN;
        GameInitSettings::HouseInfo attacker(opposite, 2);
        attacker.addPlayerInfo({settings.general.playerName, HUMANPLAYERCLASS});
        init.addHouseInfo(attacker);
        game = std::make_unique<Game>();
        currentGame = game.get();
        game->bReplay = true;
        game->initGame(init);
        game->bReplay = false;
        game->initializeGameLoop();
        game->bPause = false;
        game->frameTiming.unitPhases = false;
        owner = game->getHouse(side);
        enemy = game->getHouse(opposite);
        require(owner && enemy && game->gameType == GameType::CustomGame,
                "Savings fixture is not a genuine Custom match");
        for (const auto& player : owner->getPlayerList())
            if (auto* candidate = dynamic_cast<QuantBot*>(player.get())) bot = candidate;
        for (const auto& player : enemy->getPlayerList())
            if (auto* candidate = dynamic_cast<HumanPlayer*>(player.get())) pilot = candidate;
        for (auto* structure : structureList)
            if (structure->getOwner() == owner && structure->getItemID() == Structure_ConstructionYard)
                yard = dynamic_cast<ConstructionYard*>(structure);
        require(bot && pilot && yard && pLocalHouse == enemy && game->localHumanImmortalityApplies(),
                "Missing registered bot, yard or genuinely immortal passive Human opponent");
        require(yard->getLocation() == Coord(8, 8),
                "Named scenario binding put the production bot in the opposite start");
        // Per-house overrides are installed after mod initialization, as in the
        // causal match arms, rather than assuming a mod left the global INI alone.
        auto config = std::make_shared<QuantBotConfig>(bot->botConfig());
        config->force.simpleTechSavings = false;
        if (causalControlsOff) {
            config->force.trooperSupport = false;
            config->force.fundedClassicProduction = false;
        }
        bot->configOverride = config;
        configHash = config->getConfigHash();
        if (stage == Stage::NaturalOpening || stage == Stage::NaturalResearch) {
            require(game->isCitySimEnabled(), "Fresh-city fixture requires the real City economy");
            require(owner->getNumStructures() == 1 && owner->getNumUnits() == 0,
                    "Fresh-city fixture inherited prebuilt economic assets or an army");
            require(owner->getTechLevel() == (options.researchLab ? 1 : 9),
                    "Fresh-city fixture did not start at its actual requested technology level");
            // Only a scenario's initial grant. No prebuilt power, worker,
            // factory, zoning, roads, cargo, artificial capacity or later cash.
            cash(options.researchLab ? 30000 : 4000);
            for (int y = 4; y < 30; ++y) for (int x = 38; x < 62; ++x) {
                auto* tile = currentGameMap->getTile(x, y);
                tile->setType(Terrain_Spice);
                tile->setSpice(1000);
            }
            auto& market = owner->getChoam();
            market.availableItems.clear();
            for (const Uint32 item : {Unit_Harvester, Unit_Carryall, Unit_MCV, Unit_Tank,
                                     Unit_SiegeTank, Unit_Launcher, Unit_Quad}) {
                market.addItem(item, 4);
                market.availableItems.back().price = price(item);
            }
            bot->lastCalculatedSpice = 24 * 26 * 1000;
            bot->militaryValueLimit = 10000;
            bot->harvesterLimit = 8;
            bot->squadRallyLocation = Coord(18, 37);
            bot->squadRetreatLocation = Coord(18, 35);
            bot->pickStrategy();
            require(!bot->infantryStrategy(), "Fresh-city fixture inherited an Infantry-strategy exception");
            for (int item = ItemID_FirstID; item <= ItemID_LastID; ++item)
                bot->initialItemCount[item] = owner->getNumItems(item);
            bot->initialMilitaryValue = 0;
            bot->buildTimer = INT_MAX;
            bot->attackTimer = INT_MAX;
            config->force.simpleTechSavings = enabled;
            configHash = config->getConfigHash();
            return;
        }

        for (int i = 0; i < 14; ++i) place(Structure_WindTrap);
        if (game->isCitySimEnabled()) {
            // A physical, road-served district: lower initial apartments and
            // mature jobs, with services in both the industrial and residential
            // neighbourhoods. No population exceeds the engine's 40-person cap.
            for (const Coord at : {Coord(10,14), Coord(22,14), Coord(10,29), Coord(28,29)})
                placeAt(Structure_PoliceStation, at);
            for (const Coord at : {Coord(10,23), Coord(25,23)}) placeAt(Structure_Stadium, at);
            for (int i = 0; i < 8; ++i) {
                auto* zone = static_cast<ZoneStructure*>(placeAt(Structure_ZoneResidential, Coord(10 + i * 3, 26)));
                zone->setStartingLevel(stage == Stage::FirstStarport ? 2 : 1);
            }
            for (const int x : {10, 16, 22})
                static_cast<ZoneStructure*>(placeAt(Structure_ZoneCommercial, Coord(x,20)))->setStartingLevel(3);
            static_cast<ZoneStructure*>(placeAt(Structure_ZoneIndustrial, Coord(28,20)))->setStartingLevel(3);
        }
        place(Structure_Radar);
        for (int i = 0; i < 3; ++i) lights.push_back(static_cast<BuilderBase*>(place(Structure_LightFactory)));
        for (int i = 0; i < 2; ++i) place(side == HOUSE_ATREIDES ? Structure_Barracks : Structure_WOR);
        for (int i = 0; i < 3; ++i) place(Structure_Refinery);
        for (int i = 0; i < 5; ++i) place(Structure_Silo);
        if (stage != Stage::FirstStarport) place(Structure_StarPort);
        if (stage != Stage::Progression && stage != Stage::FirstHeavy) place(Structure_HeavyFactory);
        if (stage != Stage::Progression) {
            place(Structure_RepairYard);
            place(Structure_HighTechFactory);
        }
        if (stage != Stage::Progression && stage != Stage::Advanced) {
            place(Structure_IX);
            if (stage != Stage::FirstPalace) place(Structure_Palace);
        }
        // Cover the starting core so a peaceful technology fixture is not
        // measuring a different proactive-turret or crime-capital project.
        for (int i = 0; i < 8; ++i) place(Structure_RocketTurret);
        if (game->isCitySimEnabled())
            for (int y = 2; y < 37; ++y) for (int x = 2; x < 37; ++x)
                if (!currentGameMap->getTile(x, y)->hasAStructure()) {
                    owner->placeStructure(NONE_ID, Structure_Road, x, y, true, true);
                    require(currentGameMap->getTile(x, y)->isRoad(),
                            "Initial city road network failed real scenario placement");
                }
        for (int y = 4; y < 30; ++y) for (int x = 38; x < 62; ++x) {
            auto* tile = currentGameMap->getTile(x, y);
            tile->setType(Terrain_Spice);
            tile->setSpice(1000);
        }
        while (owner->getNumItems(Unit_Harvester) < workers) {
            const int i = owner->getNumItems(Unit_Harvester);
            auto* worker = static_cast<Harvester*>(spawn(owner, Unit_Harvester, Coord(37 + i % 3, 29 + i / 3)));
            worker->doSetAttackMode(HARVEST);
            if (stage == Stage::Progression) {
                // The mature working fleet starts with its harvested cargo,
                // not extra cash. Only ordinary unloading can fund the plan.
                worker->setAmountOfSpice(worker->getSpiceCapacity());
                worker->doReturn();
            }
        }
        require(owner->getNumItems(Unit_Harvester) == workers, "Opening-worker scenario has unintended free workers");
        for (int i = 0; i < carryalls; ++i) spawn(owner, Unit_Carryall, Coord(18 + i, 34));
        auto seedArmy = [&](Uint32 item, int count) {
            for (int i = 0; i < count; ++i) {
                const int n = static_cast<int>(initialArmy.size());
                initialArmy.push_back(spawn(owner, item, Coord(4 + n % 16, 39 + n / 16))->getObjectID());
            }
        };
        if (army == Army::ReadyLight) seedArmy(Unit_Quad, 18);
        if (army == Army::BelowValueFloor) seedArmy(Unit_Trooper, 6);
        if (army == Army::TinyMixed) { seedArmy(Unit_Tank, 1); seedArmy(Unit_Quad, 2); }
        if (army == Army::TinyTank) seedArmy(Unit_Tank, 1);

        // A real, ordinary-price scenario catalogue. Fresh generated fixtures
        // otherwise have no imports at all, which cannot test a legal first
        // Starport or its exact-budget opening worker.
        auto& market = owner->getChoam();
        market.availableItems.clear();
        for (const Uint32 item : {Unit_Harvester, Unit_Carryall, Unit_MCV, Unit_Tank,
                                 Unit_SiegeTank, Unit_Launcher, Unit_Quad}) {
            market.addItem(item, 4);
            market.availableItems.back().price = price(item);
        }

        for (auto* structure : structureList) {
            auto* builder = dynamic_cast<BuilderBase*>(structure);
            if (!builder || structure->getOwner() != owner) continue;
            // Initial suppliers start mature. A new, asserted HF starts at its
            // engine-default level zero and must pay for all of its own upgrades.
            builder->curUpgradeLev = builder->getMaxUpgradeLevel();
            builder->updateBuildList();
        }
        // As in shared-spending-probe, the mature opening has ample bank
        // headroom. Storage expansion is an independent capital decision, not
        // the shortage this technology fixture is intended to reproduce.
        owner->capacity = 100000;
        game->gameCycleCount = MILLI2CYCLES(300000);
        if (game->isCitySimEnabled()) initializePhysicalCity();
        bot->lastCalculatedSpice = 24 * 26 * 1000;
        bot->militaryValueLimit = 10000000;
        bot->harvesterLimit = 8;
        bot->squadRallyLocation = Coord(18, 37);
        bot->squadRetreatLocation = Coord(18, 35);
        bot->pickStrategy();
        require(!bot->infantryStrategy(), "Savings fixture inherited an Infantry-strategy exception");
        bot->initialMilitaryValue = bot->militaryUnitValue();
        for (int item = ItemID_FirstID; item <= ItemID_LastID; ++item)
            bot->initialItemCount[item] = owner->getNumItems(item);
        bot->buildTimer = INT_MAX;
        bot->attackTimer = INT_MAX;
        config->force.simpleTechSavings = enabled;
        configHash = config->getConfigHash();
    }

    ~World() {
        game.reset();
        currentGame = nullptr;
        pLocalPlayer = nullptr;
        pLocalHouse = nullptr;
    }

    StructureBase* placeAt(Uint32 item, Coord at) {
        auto* structure = owner->placeStructure(NONE_ID, item, at.x, at.y, true, true);
        require(structure, "Initial positioned savings structure placement failed");
        return structure;
    }

    void initializePhysicalCity() {
        auto* sim = game->getCitySimulation();
        auto& state = sim->houseState_[owner->getHouseID()];
        int residential = 0, commercial = 0, industrial = 0, taxBase = 0;
        for (auto* structure : structureList) {
            if (structure->getOwner() != owner) continue;
            const Uint32 item = structure->getItemID();
            auto* zone = dynamic_cast<ZoneStructure*>(structure);
            if (!zone) structure->setCityOccupancy(DuneCity::getStructureMaxLevel(item));
            const int level = zone
                ? currentGameMap->getTile(structure->getLocation())->getCityZoneDensity()
                : structure->getCityOccupancy();
            const int population = DuneCity::getStructurePopulation(structure, level);
            switch (DuneCity::getStructureCityRole(item)) {
            case DuneCity::CityRole::Residential: residential += population; break;
            case DuneCity::CityRole::Commercial: commercial += population; break;
            case DuneCity::CityRole::Industrial: industrial += population; break;
            default: break;
            }
            if (item == Structure_Palace) commercial += DuneCity::getPalaceCommercialPopulation(level);
            taxBase += DuneCity::getStructureTaxBaseEighths(structure, level);
        }
        state.resPop = state.prevResPop = residential;
        state.comPop = state.prevComPop = commercial;
        state.indPop = state.prevIndPop = industrial;
        state.taxBaseEighths = taxBase;
        state.resValve = state.comValve = state.indValve = 0;
        state.policeFundingPercent = 100;
        // Recount actual structures, roads, land value, crime and tax base at
        // the mature scenario clock. No synthetic res800/tax8000 can disappear
        // on the first real scan, and no stale day-zero history drives migration.
        sim->reconcileLoadedMapState(game->getGameCycleCount());
        state.prevResPop = state.resPop;
        state.prevComPop = state.comPop;
        state.prevIndPop = state.indPop;
        // The builders' mature catalogues were initialized before this
        // physical census. Re-evaluate their population-gated offers now,
        // rather than relying on a future House threshold-transition tick.
        owner->updateBuildLists();
    }

    StructureBase* place(Uint32 item) {
        const Coord size = getStructureSize(item);
        for (int y = 3; y + size.y < 36; ++y) for (int x = 3; x + size.x < 36; ++x) {
            bool clear = true;
            for (int dy = -1; dy <= size.y; ++dy) for (int dx = -1; dx <= size.x; ++dx)
                clear &= !currentGameMap->getTile(x + dx, y + dy)->hasAGroundObject();
            if (!clear) continue;
            auto* structure = owner->placeStructure(NONE_ID, item, x, y, true, true);
            require(structure, "Initial savings structure placement failed");
            return structure;
        }
        throw std::runtime_error("No space for initial savings scenario assets");
    }

    UnitBase* spawn(House* house, Uint32 item, Coord at) {
        auto* unit = house->createUnit(item, true);
        require(unit, "Initial savings unit creation failed");
        unit->deploy(at);
        unit->setVisible(VIS_ALL, true);
        return unit;
    }

    BuilderBase* producer(Uint32 item) const {
        for (auto* structure : structureList)
            if (structure->getOwner() == owner && structure->getItemID() == item && structure->getHealth() > 0)
                return dynamic_cast<BuilderBase*>(structure);
        return nullptr;
    }

    int price(Uint32 item) const { return game->objectData.data[item][owner->getHouseID()].price; }

    int queued(Uint32 item) const {
        int count = 0;
        for (const auto* structure : structureList) {
            const auto* builder = dynamic_cast<const BuilderBase*>(structure);
            if (!builder || structure->getOwner() != owner) continue;
            for (const auto& order : builder->getBuildList()) if (order.itemID == item) count += order.num;
        }
        return count;
    }

    int queuedLight() const { return queued(Unit_Trike) + queued(Unit_RaiderTrike) + queued(Unit_Quad); }

    int vehicleValue(bool includeQueues) const {
        int total = 0;
        for (const Uint32 item : vehicles)
            total += price(item) * (owner->getNumItems(item) + (includeQueues ? queued(item) : 0));
        return total;
    }

    int lightValue(bool includeQueues) const {
        int total = 0;
        for (const Uint32 item : {Unit_Trike, Unit_RaiderTrike, Unit_Quad})
            total += price(item) * (owner->getNumItems(item) + (includeQueues ? queued(item) : 0));
        return total;
    }

    int infantryValue(bool includeQueues) const {
        int total = 0;
        for (const Uint32 item : {Unit_Soldier, Unit_Infantry, Unit_Trooper, Unit_Troopers})
            total += price(item) * (owner->getNumItems(item) + (includeQueues ? queued(item) : 0));
        return total;
    }

    void cash(int credits) {
        owner->storedCredits = 0;
        owner->cityCredits = 0;
        owner->startingCredits = credits;
    }

    void closeMilitaryMarket() {
        // A genuine empty import catalogue makes the local LF the only legal
        // vehicle supplier; sold-out-but-listed heavy offers are not that case.
        owner->getChoam().availableItems.clear();
        if (auto* port = producer(Structure_StarPort)) port->updateBuildList();
    }

    QuantBot::BuildPass observePlan() {
        QuantBot::BuildPass pass(bot->militaryUnitValue());
        bot->resetBuildPass();
        bot->planBuildInventory(pass);
        bot->planBuildBudget(pass);
        bot->planCityState(pass);
        bot->planStrategicReserve(pass);
        bot->planSpiceEconomy(pass);
        bot->planPowerAndYards(pass);
        bot->planCityPopulation(pass);
        bot->planCivicNeedAndTrips(pass);
        bot->planUnitMix(pass);
        bot->planCapitalForecast(pass);
        bot->planCityBootstrap(pass);
        bot->planOpeningProduction(pass);
        bot->planDefenceCoverage(pass);
        bot->planTechSavings(pass);
        return pass;
    }

    void build() {
        bot->build(bot->militaryUnitValue());
        ++fullTurns;

    }

    void tick() {
        bot->buildTimer = INT_MAX;
        bot->attackTimer = INT_MAX;
        game->negotiatedBudget = 15000;
        game->carryOverTokens = 0;
        game->updateGameState();
        require(!game->finished, "Passive savings match ended before evidence collection");
    }
};

struct PaidProduction {
    World& world;
    std::set<Uint32> queuedTechnology;
    std::set<Uint32> paidTechnology;
    std::set<Uint32> paidHeavyUnits;
    std::set<Uint32> paidUpgrades;
    std::set<Uint32> paidFactoryUnits;
    std::map<Uint32, int> builtBefore;
    std::map<Uint32, int> unitsBuiltBefore;
    int heavyBuiltBefore = 0;

    explicit PaidProduction(World& world) : world(world) {
        for (const Uint32 item : technology) builtBefore[item] = world.owner->getNumBuiltItems(item);
        for (const Uint32 item : vehicles) unitsBuiltBefore[item] = world.owner->getNumBuiltItems(item);
        for (const Uint32 item : vehicles)
            if (!light(item)) heavyBuiltBefore += world.owner->getNumBuiltItems(item);
    }

    void observe() {
        for (const auto* structure : structureList) {
            const auto* builder = dynamic_cast<const BuilderBase*>(structure);
            if (!builder || structure->getOwner() != world.owner) continue;
            for (const auto& offer : builder->getBuildList())
                if (offer.num > 0 && builtBefore.count(offer.itemID)) {
                    if (offer.itemID == Structure_StarPort && !queuedTechnology.count(offer.itemID))
                        require(world.owner->meetsStarportPopulation(),
                                "Full bot queued its first Starport below the actual legal city population");
                    queuedTechnology.insert(offer.itemID);
                }
            if (builder->getProductionProgress() > 0) {
                const Uint32 item = builder->getCurrentProducedItem();
                if (builtBefore.count(item)) paidTechnology.insert(item);
                if (std::find(vehicles.begin(), vehicles.end(), item) != vehicles.end() && !light(item))
                    paidHeavyUnits.insert(item);
                if ((builder->getItemID() == Structure_LightFactory || builder->getItemID() == Structure_HeavyFactory)
                    && unitsBuiltBefore.count(item)) paidFactoryUnits.insert(item);
            }
            if (builder->getItemID() == Structure_HeavyFactory && builder->getUpgradeProgress() > 0)
                paidUpgrades.insert(builder->getObjectID());
        }
        for (const auto& [item, before] : builtBefore)
            if (world.owner->getNumBuiltItems(item) > before)
                require(queuedTechnology.count(item) && paidTechnology.count(item),
                        "Completed technology lacks a real observed paid bot queue");
    }

    void step() {
        if (world.game->getGameCycleCount() % 25 == 0) world.build();
        observe();
        world.tick();
        observe();
    }

    bool completed(Uint32 item) const { return world.owner->getNumBuiltItems(item) > builtBefore.at(item); }
    bool factoryUnitCompleted() const {
        for (const Uint32 item : paidFactoryUnits)
            if (world.owner->getNumBuiltItems(item) > unitsBuiltBefore.at(item)) return true;
        return false;
    }

    int heavyCompleted() const {
        int total = 0;
        for (const Uint32 item : vehicles) if (!light(item)) total += world.owner->getNumBuiltItems(item);
        return total - heavyBuiltBefore;
    }
};

inline std::pair<int, int> actualGroundReadiness(const World& world) {
    int units = 0, value = 0;
    for (const auto* unit : unitList) {
        if (unit->getOwner() != world.owner || !unit->isActive() || unit->isBadlyDamaged()
            || unit->getHealth() <= 0 || !unit->isAGroundUnit() || !unit->canAttack()
            || unit->getItemID() == Unit_Saboteur || isSandwormUnit(unit->getItemID())) continue;
        ++units;
        value += world.price(unit->getItemID());
    }
    return {units, value};
}

inline int builtCityLots(const World& world) {
    return world.owner->getNumBuiltItems(Structure_ZoneResidential)
        + world.owner->getNumBuiltItems(Structure_ZoneCommercial)
        + world.owner->getNumBuiltItems(Structure_ZoneIndustrial);
}

inline std::vector<UnitBase*> spawnGroundBand(World& world, House* owner, Uint32 item, int count,
                                              Coord center, int minimum, int maximum) {
    std::vector<Coord> sites;
    for (int y = 1; y < 63; ++y) for (int x = 1; x < 63; ++x) {
        const Coord at(x, y);
        const auto distance = blockDistance(at, center);
        if (!currentGameMap->getTile(at)->hasAGroundObject() && distance >= minimum && distance <= maximum)
            sites.push_back(at);
    }
    std::sort(sites.begin(), sites.end(), [&](Coord a, Coord b) {
        const auto da = blockDistance(a, center), db = blockDistance(b, center);
        if (da != db) return da < db;
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });
    require(int(sites.size()) >= count, "Local-defence control cannot physically deploy its finite scenario army");
    std::vector<UnitBase*> result;
    for (int i = 0; i < count; ++i) result.push_back(world.spawn(owner, item, sites[i]));
    return result;
}


inline void statelessSaveRoundTrips(World& world) {
    require(SAVEGAMEVERSION == 9884 && NETWORK_PROTOCOL_VERSION == 120,
            "Stateless AI fixture disagrees with the feature-only save/wire boundary");
    auto originalConfig = world.bot->configOverride;
    auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
    world.bot->configOverride = config;
    OMemoryStream disabled;
    disabled.open();
    config->force.simpleTechSavings = false;
    world.bot->save(disabled);
    OMemoryStream enabled;
    enabled.open();
    config->force.simpleTechSavings = true;
    world.bot->save(enabled);
    world.bot->configOverride = originalConfig;
    require(disabled.getDataLength() > 0 && disabled.getDataLength() == enabled.getDataLength()
                && std::memcmp(disabled.getData(), enabled.getData(), disabled.getDataLength()) == 0,
            "Simple policy alone changed the serialized QuantBot block");
    enabled.writeUint32(0xabcdef01u);
    struct RestoreVersion {
        Game& game;
        Uint32 version;
        ~RestoreVersion() { game.loadedSavegameVersion = version; }
    } restore{*world.game, world.game->loadedSavegameVersion};
    for (const Uint32 version : {Uint32(9882), Uint32(9883)}) {
        world.game->loadedSavegameVersion = version;
        IMemoryStream input(enabled.getData(), enabled.getDataLength());
        QuantBot loaded(input, world.owner);
        require(loaded.militaryValueLimit == world.bot->militaryValueLimit
                    && loaded.initialMilitaryValue == world.bot->initialMilitaryValue
                    && input.readUint32() == 0xabcdef01u && input.getRemainingLength() == 0,
                "An installed-format QuantBot lost saved state or following-record alignment");
        OMemoryStream output;
        output.open();
        loaded.save(output);
        output.writeUint32(0xabcdef01u);
        world.game->loadedSavegameVersion = SAVEGAMEVERSION;
        IMemoryStream roundtrip(output.getData(), output.getDataLength());
        QuantBot reloaded(roundtrip, world.owner);
        require(reloaded.militaryValueLimit == loaded.militaryValueLimit
                    && reloaded.initialMilitaryValue == loaded.initialMilitaryValue
                    && roundtrip.readUint32() == 0xabcdef01u && roundtrip.getRemainingLength() == 0,
                "An installed-format QuantBot did not round-trip through current9883");
        SDL_Log("SIMPLE_TECH_STATELESS_SAVE: input_version=%u output_version=%d protocol=%d bytes=%zu roundtrip=1 policy_bytes_unchanged=1",
                version, SAVEGAMEVERSION, NETWORK_PROTOCOL_VERSION, size_t(disabled.getDataLength()));
    }
}
} // namespace TechProductionProbe
