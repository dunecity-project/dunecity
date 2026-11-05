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
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace TechSavingProbe {

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
        config->force.techSavings = enabled;
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

    void seedSpentEconomy() {
        cash(10000);
        // A valid paused ledger after 20k of receipts, 3k of completed
        // production and 7k of completed economic commitments. This is initial
        // scenario history, not a fabricated payment for the asserted next unit.
        auto& ledger = bot->spendingLedger;
        ledger = {};
        ledger.reconcile(20000, QuantBotSpendingPolicy::incomeShares(false));
        ledger.charge(QuantBotSpendingPolicy::Bucket::Production, 3000);
        ledger.charge(QuantBotSpendingPolicy::Bucket::Economy, 7000);
        require(ledger.lastWealth == netWealth() && ledger.available(QuantBotSpendingPolicy::Bucket::Economy) == 0
                && ledger.available(QuantBotSpendingPolicy::Bucket::Military) == 7000,
                "Spent-economy control is not a wealth-conserving historical ledger");
    }

    void cash(int credits) {
        owner->storedCredits = 0;
        owner->cityCredits = 0;
        owner->startingCredits = credits;
    }

    int64_t netWealth() const {
        int64_t unpaid = 0;
        for (const auto* structure : structureList) {
            const auto* builder = dynamic_cast<const BuilderBase*>(structure);
            if (!builder || structure->getOwner() != owner) continue;
            if (builder->isUpgrading())
                unpaid += std::max(0, builder->getUpgradeCost() - builder->getUpgradeProgress().lround());
            if (builder->getItemID() == Structure_StarPort) continue; // Cargo was already paid at acceptance.
            int queuedCost = 0;
            for (const auto& offer : builder->getBuildList()) queuedCost += offer.num * int(offer.price);
            unpaid += std::max(0, queuedCost - builder->getProductionProgress().lround());
        }
        return int64_t(owner->getCredits()) - unpaid;
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
        const auto& ledger = bot->getSpendingLedger();
        if (bot->botConfig().force.techSavings) {
            require(ledger.initialized && ledger.lastWealth == netWealth(),
                    "Accepted orders were not charged exactly once against real cash minus unpaid commitments");
            require(ledger.balance[0] + ledger.balance[1] + ledger.balance[2]
                        == ledger.lastWealth * QuantBotSpendingPolicy::creditScale,
                    "Income buckets lost or minted credit value");
        } else {
            require(!ledger.initialized && ledger.balance == std::array<int64_t, 3>{},
                    "Explicit OFF changed the legacy bot's bucket state");
        }
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

inline void currentAndQueuedComposition(const char* mod, const char* playerClass, bool queuedControl,
                                        bool causalControlsOff) {
    std::string offHash;
    int offCheap = 0;
    for (const bool enabled : {false, true}) {
        World world(mod, playerClass, enabled, Stage::Complete,
                    queuedControl ? Army::TinyTank : Army::TinyMixed, 8, causalControlsOff);
        // At this treasury the normal classic target is two yards. Supply both
        // actual mature yards, rather than selecting an MCV that a one-yard
        // override rejects and mistaking that legacy branch for composition.
        auto* secondYard = static_cast<BuilderBase*>(world.place(Structure_ConstructionYard));
        secondYard->curUpgradeLev = secondYard->getMaxUpgradeLevel();
        secondYard->updateBuildList();
        world.game->gameInitSettings.gameOptions.maximumNumberOfConstructionYardsOverride = 2;
        world.cash(19999);
        if (queuedControl) {
            for (auto* factory : world.lights) factory->doProduceItem(Unit_Quad);
            world.lights.push_back(static_cast<BuilderBase*>(world.place(Structure_LightFactory)));
            world.lights.push_back(static_cast<BuilderBase*>(world.place(Structure_LightFactory)));
            for (auto* factory : world.lights) {
                factory->curUpgradeLev = factory->getMaxUpgradeLevel();
                factory->updateBuildList();
            }
        }
        const int queuedBefore = world.queuedLight();
        const int existingValue = world.vehicleValue(false);
        const int committedValue = world.vehicleValue(true);
        const int existingLight = world.lightValue(false);
        const int committedLight = world.lightValue(true);
        require(world.bot->militaryValueLimit >= 1000 * committedValue,
                "Composition fixture does not separate a huge cap from its tiny current army");
        PaidProduction evidence(world);
        world.build();
        evidence.observe();
        const auto& mix = world.bot->getAllocationSnapshot();
        require(mix.ready, "Full build did not expose its actual allocation snapshot");
        int lightBps = 0;
        for (const auto& slot : mix.slots) if (light(slot.itemID)) lightBps += slot.targetBps;
        require(int64_t(committedLight) * 10000 > int64_t(committedValue) * lightBps,
                "Tiny army does not already exceed its real current light-vehicle target");
        const int addedCheap = world.queuedLight() - queuedBefore;
        if (!enabled) {
            offHash = world.configHash;
            offCheap = addedCheap;
            require(addedCheap > 0, "OFF did not reproduce the legacy future-funded cheap-unit quota");
        } else {
            require(world.configHash != offHash && addedCheap == 0,
                    "ON refilled an overweight current/queued light share using the military cap");
            // Repeated full passes, before any completion changes the denominator,
            // cannot mint an independent oversized quota for each idle producer.
            for (int i = 0; i < 5; ++i) world.build();
            require(world.queuedLight() == queuedBefore,
                    "Parallel/repeated producers ignored the already-queued composition commitments");
            const int infantry = world.infantryValue(true);
            const int army = world.bot->militaryUnitValue() + world.vehicleValue(true) - world.vehicleValue(false)
                + world.infantryValue(true) - world.infantryValue(false);
            require(int64_t(infantry) * 10000 <= int64_t(army) * mix.infantryQuotaBps,
                    "Unobserved standing infantry bypassed its current-army quota under TechSavings");
        }
        const Uint32 deadline = world.game->getGameCycleCount() + MILLI2CYCLES(90000);
        while (world.game->getGameCycleCount() < deadline && evidence.heavyCompleted() == 0) evidence.step();
        require(!evidence.paidHeavyUnits.empty() && evidence.heavyCompleted() > 0,
                "Composition arbitration left legal tank/launcher production idle instead of paying a heavy order");
        SDL_Log("TECH_SAVING_COMPOSITION: mod=%s difficulty=%s enabled=%d queued_control=%d causal_controls_off=%d "
                "military_cap=%d existing_vehicle_value=%d existing_light_value=%d committed_vehicle_value=%d "
                "committed_light_value=%d light_target_bps=%d cheap_added=%d legacy_cheap_added=%d "
                "paid_heavy_completed=%d full_turns=%d config_hash=%s",
                mod, playerClass, int(enabled), int(queuedControl), int(causalControlsOff), world.bot->militaryValueLimit,
                existingValue, existingLight, committedValue, committedLight, lightBps, addedCheap, offCheap,
                evidence.heavyCompleted(), world.fullTurns, world.configHash.c_str());
    }
}

inline void rejectedFactoryGrowthDoesNotStarveArmy(const char* mod, const char* playerClass) {
    for (const bool emptyEconomy : {false, true}) {
        World world(mod, playerClass, true, Stage::Complete, Army::TinyMixed);
        world.closeMilitaryMarket();
        if (emptyEconomy) world.seedSpentEconomy();
        else {
            world.game->gameInitSettings.gameOptions.maximumNumberOfConstructionYardsOverride = 1;
            world.cash(100000);
        }
        auto* heavy = world.producer(Structure_HeavyFactory);
        const auto pass = world.observePlan();
        const size_t next = UnitMixPolicy::nextExistingSlot(pass.unitMix, pass.compositionValue,
            pass.compositionPrice, pass.compositionAvailable);
        require(heavy && next < UnitMixPolicy::kOrnithopterSlot
                && heavy->isAvailableToBuild(vehicles[next]),
                "Rejected-growth control has no globally selected legal heavy unit");
        const Uint32 selected = vehicles[next];
        const auto economyBefore = world.bot->getSpendingLedger().balance[1];
        const int completedBefore = world.owner->getNumBuiltItems(selected);
        PaidProduction evidence(world);
        world.build();
        evidence.observe();
        require(heavy->getProductionQueueSize() == 1 && heavy->getCurrentProducedItem() == selected
                && world.queued(Unit_MCV) == 0 && world.queuedLight() == 0,
                "Rejected MCV growth occupied the HF turn instead of the selected heavy composition order");
        if (emptyEconomy)
            require(world.bot->getSpendingLedger().balance[1] == economyBefore,
                    "A rejected economic priority spent the exhausted economy wallet");
        bool paidByHeavy = false;
        const Uint32 deadline = world.game->getGameCycleCount() + MILLI2CYCLES(90000);
        while (world.game->getGameCycleCount() < deadline
               && world.owner->getNumBuiltItems(selected) == completedBefore) {
            evidence.step();
            paidByHeavy |= heavy->getCurrentProducedItem() == selected && heavy->getProductionProgress() > 0;
        }
        require(paidByHeavy && world.owner->getNumBuiltItems(selected) > completedBefore,
                "Admitted heavy order after rejected growth never received payment and completed");
        SDL_Log("TECH_SAVING_HF_GROWTH_HOLD: mod=%s difficulty=%s cause=%s selected=%u paid=1 completed=1 "
                "mcv_queued=0 cheap_fallback=0 exact_ledger=1",
                mod, playerClass, emptyEconomy ? "economy_wallet" : "yard_limit", selected);
    }
}

inline void rejectedTransportGrowthDoesNotStarveAir(const char* mod, const char* playerClass) {
    World world(mod, playerClass, true, Stage::Complete, Army::Empty, 8, false, HOUSE_ATREIDES, 1);
    world.closeMilitaryMarket();
    auto mix = world.observePlan();
    require(mix.compositionAvailable[UnitMixPolicy::kOrnithopterSlot]
            && mix.unitMix[UnitMixPolicy::kOrnithopterSlot] > 0,
            "Transport-growth control has no genuine Atreides Ornithopter offer");
    // Seed a finite ground army near its actual current target, leaving only
    // its air allocation missing. These are labelled initial scenario units.
    int seeded = 0;
    for (size_t i = 0; i < vehicles.size(); ++i) {
        if (i == UnitMixPolicy::kOrnithopterSlot || !mix.compositionAvailable[i] || mix.unitMix[i] <= 0) continue;
        const int price = world.price(vehicles[i]);
        const int count = (20000 * mix.unitMix[i] / 10000 + price - 1) / price;
        for (int n = 0; n < count; ++n) {
            world.spawn(world.owner, vehicles[i], Coord(4 + seeded % 16, 39 + seeded / 16));
            ++seeded;
        }
    }
    world.seedSpentEconomy();
    const auto pass = world.observePlan();
    require(pass.itemCount[Unit_Carryall] == 1 && pass.transportTarget > 1
            && UnitMixPolicy::nextExistingSlot(pass.unitMix, pass.compositionValue,
                pass.compositionPrice, pass.compositionAvailable) == UnitMixPolicy::kOrnithopterSlot,
            "Transport-growth control does not need both an economic carryall and its globally due air slot");
    auto* highTech = world.producer(Structure_HighTechFactory);
    require(highTech && highTech->isAvailableToBuild(Unit_Ornithopter), "Measured aircraft is not legal at its actual factory");
    const auto economyBefore = world.bot->getSpendingLedger().balance[1];
    const int builtBefore = world.owner->getNumBuiltItems(Unit_Ornithopter);
    PaidProduction evidence(world);
    world.build();
    evidence.observe();
    require(highTech->getProductionQueueSize() == 1 && highTech->getCurrentProducedItem() == Unit_Ornithopter
            && world.queued(Unit_Carryall) == 0 && world.queuedLight() == 0
            && world.bot->getSpendingLedger().balance[1] == economyBefore,
            "Unfunded additional transport swallowed or paid from the selected military aircraft's wallet");
    bool paidAtHighTech = false;
    const Uint32 deadline = world.game->getGameCycleCount() + MILLI2CYCLES(90000);
    while (world.game->getGameCycleCount() < deadline && world.owner->getNumBuiltItems(Unit_Ornithopter) == builtBefore) {
        evidence.step();
        paidAtHighTech |= highTech->getCurrentProducedItem() == Unit_Ornithopter && highTech->getProductionProgress() > 0;
    }
    require(paidAtHighTech && world.owner->getNumBuiltItems(Unit_Ornithopter) > builtBefore,
            "Aircraft after rejected transport growth never received actual factory payment and completion");
    SDL_Log("TECH_SAVING_HT_GROWTH_HOLD: mod=%s difficulty=%s house=atreides carryalls=1 target=%d "
            "economy_available=0 ornithopter_paid=1 completed=1 cheap_fallback=0 exact_ledger=1",
            mod, playerClass, pass.transportTarget);
}

inline void paidTechnologyProgression(const char* mod, const char* playerClass, bool causalControlsOff) {
    World world(mod, playerClass, true, Stage::Progression, Army::ReadyLight, 8, causalControlsOff);
    world.cash(world.price(Structure_HeavyFactory) - 1);
    world.bot->militaryValueLimit = 6000;
    require(world.yard->isAvailableToBuild(Structure_HeavyFactory)
            && world.owner->hasPower() && world.owner->getNumItems(Structure_StarPort) == 1
            && world.bot->ownGroundArmyValue() >= world.bot->minWaveValue(),
            "Progression premise lacks legal first HF, a real port, power or a dispatch-capable own army");
    for (const Uint32 item : {Structure_HeavyFactory, Structure_RepairYard, Structure_HighTechFactory,
                             Structure_IX, Structure_Palace})
        require(world.owner->getNumItems(item) == 0, "Asserted progression technology was supplied by the scenario");
    const auto incomeBefore = world.owner->getHarvestedSpice();
    const Uint32 start = world.game->getGameCycleCount();
    const Uint32 deadline = start + MILLI2CYCLES(480000);
    PaidProduction evidence(world);
    int firstHfCycle = -1, launcherCycle = -1, ixCycle = -1, palaceCycle = -1;
    bool upgradeQueued = false;
    while (world.game->getGameCycleCount() < deadline) {
        evidence.step();
        auto* heavy = world.producer(Structure_HeavyFactory);
        const int elapsed = int(world.game->getGameCycleCount() - start);
        if (heavy && firstHfCycle < 0) firstHfCycle = elapsed;
        if (heavy && heavy->isUpgrading()) upgradeQueued = true;
        if (heavy && heavy->isAvailableToBuild(Unit_Launcher) && launcherCycle < 0) launcherCycle = elapsed;
        if (evidence.completed(Structure_IX) && ixCycle < 0) ixCycle = elapsed;
        if (evidence.completed(Structure_Palace) && palaceCycle < 0) palaceCycle = elapsed;
        for (const Uint32 item : technology) {
            // Once its first supplier stands, ordinary paid capacity expansion
            // remains legal. The pending first copy and one-off IX/Palace do not
            // get an independent reservation from each yard.
            if (world.owner->getNumItems(item) == 0 || item == Structure_IX || item == Structure_Palace)
                require(world.owner->getNumItems(item) + world.queued(item) <= 1,
                        "Parallel technology planning duplicated an existing or pending first supplier");
        }
        if (firstHfCycle >= 0 && launcherCycle >= 0 && ixCycle >= 0 && palaceCycle >= 0) break;
    }
    if (firstHfCycle < 0 || launcherCycle < 0 || ixCycle < 0 || palaceCycle < 0
        || !evidence.completed(Structure_RepairYard) || !evidence.completed(Structure_HighTechFactory)
        || !upgradeQueued || evidence.paidUpgrades.empty()) {
        const auto pass = world.observePlan();
        const auto& ledger = world.bot->getSpendingLedger();
        SDL_Log("TECH_SAVING_PROGRESSION_BLOCKED: first_hf=%d launcher=%d ix=%d palace=%d repair=%d hightech=%d "
                "upgrade_started=%d upgrade_paid=%d cash=%d net_income_4m=%d ready_units=%d ready_value=%d minimum=%d "
                "city_population=%d dangerous_crime=%d power=%d/%d candidate=%u candidate_cost=%d active=%d "
                "production_raw=%lld economy_raw=%lld military_raw=%lld",
                firstHfCycle, launcherCycle, ixCycle, palaceCycle, int(evidence.completed(Structure_RepairYard)),
                int(evidence.completed(Structure_HighTechFactory)), int(upgradeQueued), int(!evidence.paidUpgrades.empty()),
                world.owner->getCredits(), pass.forecastNetIncome, pass.readyGroundUnits, pass.readyGroundValue,
                pass.defenceMinimumValue, pass.ownTotalPop, pass.dangerousCrimeProperties,
                world.owner->getProducedPower(), world.owner->getPowerRequirement(),
                pass.techCandidate.item, pass.techCandidate.cost, int(pass.techSavingActive),
                static_cast<long long>(ledger.balance[0]), static_cast<long long>(ledger.balance[1]),
                static_cast<long long>(ledger.balance[2]));
    }
    require(firstHfCycle >= 0 && launcherCycle >= 0 && ixCycle >= 0 && palaceCycle >= 0
            && evidence.completed(Structure_RepairYard) && evidence.completed(Structure_HighTechFactory)
            && upgradeQueued && !evidence.paidUpgrades.empty(),
            "Stable real income did not complete the first HF, paid upgrades and legal advanced technology in bounded time");
    require(world.owner->getHarvestedSpice() > incomeBefore,
            "Low-cash progression completed without observing any genuine spice delivery income");
    // Technology is not an attack lease. The same army passes the ordinary
    // readiness/repair/posture gates while build() continues planning capital.
    const Uint32 dispatchDeadline = world.game->getGameCycleCount() + MILLI2CYCLES(90000);
    while (world.bot->customWave.launched == 0 && world.game->getGameCycleCount() < dispatchDeadline) {
        world.bot->attack(world.bot->militaryUnitValue());
        if (world.bot->customWave.launched == 0) evidence.step();
    }
    require(world.bot->customWave.launched != 0 && !world.bot->customWave.members.empty(),
            "Completed bounded technology plan left the dispatch-capable army indefinitely idle");
    SDL_Log("TECH_SAVING_PROGRESSION: mod=%s difficulty=%s causal_controls_off=%d first_hf_cycles=%d "
            "launcher_unlock_cycles=%d first_ix_cycles=%d first_palace_cycles=%d upgrade_paid=1 "
            "real_income=1 first_starport_preserved=1 dispatch_members=%zu elapsed_cycles=%u "
            "full_turns=%d config_hash=%s",
            mod, playerClass, int(causalControlsOff), firstHfCycle, launcherCycle, ixCycle, palaceCycle,
            world.bot->customWave.members.size(), world.game->getGameCycleCount() - start,
            world.fullTurns, world.configHash.c_str());
}

inline void foundedTechnologyQuote(const char* mod, const char* playerClass) {
    World world(mod, playerClass, true, Stage::FirstHeavy);
    world.closeMilitaryMarket();
    world.game->gameInitSettings.gameOptions.concreteRequired = true;
    world.yard->updateBuildList();
    world.cash(5000);
    const auto pass = world.observePlan();
    require(pass.techSavingActive && pass.techCandidate.item == Structure_HeavyFactory
            && pass.techCandidate.cost > world.price(Structure_HeavyFactory),
            "Founded technology control has no legal full-quote first HF savings plan");
    const auto productionBefore = world.bot->getSpendingLedger().balance[0];
    PaidProduction evidence(world);
    world.build();
    evidence.observe();
    const int slabs = world.queued(Structure_Slab1) + world.queued(Structure_Slab4);
    require(world.queued(Structure_HeavyFactory) == 1 && slabs > 0
            && world.bot->getSpendingLedger().balance[0]
                == productionBefore - int64_t(pass.techCandidate.cost) * QuantBotSpendingPolicy::creditScale,
            "A founded HF did not charge its complete real slab/building quote once to the production bucket");
    const Uint32 deadline = world.game->getGameCycleCount() + MILLI2CYCLES(180000);
    while (!evidence.completed(Structure_HeavyFactory) && world.game->getGameCycleCount() < deadline)
        evidence.step();
    require(evidence.completed(Structure_HeavyFactory),
            "The paid concrete sequence and first HF did not complete through ordinary placement");
    SDL_Log("TECH_SAVING_FOUNDATION: mod=%s difficulty=%s building_price=%d founded_quote=%d slab_orders=%d "
            "full_quote_charged_once=1 paid_completed=1",
            mod, playerClass, world.price(Structure_HeavyFactory), pass.techCandidate.cost, slabs);
}

inline void realUnlockStopsCheapReplenishment(const char* mod, const char* playerClass) {
    World world(mod, playerClass, true, Stage::FirstHeavy);
    world.closeMilitaryMarket();
    world.cash(3000);
    PaidProduction evidence(world);
    const Uint32 deadline = world.game->getGameCycleCount() + MILLI2CYCLES(240000);
    int cheapBeforeUnlock = 0, cheapAfterUnlock = 0;
    bool tankUnlocked = false, launcherUnlocked = false;
    while (world.game->getGameCycleCount() < deadline) {
        if (world.game->getGameCycleCount() % 25 == 0) {
            const auto* heavy = world.producer(Structure_HeavyFactory);
            const bool legalHeavy = heavy && heavy->isAvailableToBuild(Unit_Tank);
            const int before = world.queuedLight();
            world.build();
            const int added = world.queuedLight() - before;
            if (legalHeavy) {
                tankUnlocked = true;
                cheapAfterUnlock += added;
                int lightBps = 0;
                for (const auto& slot : world.bot->getAllocationSnapshot().slots)
                    if (light(slot.itemID)) lightBps += slot.targetBps;
                require(lightBps < 10000 && int64_t(world.lightValue(true)) * 10000
                            > int64_t(world.vehicleValue(true)) * lightBps && added == 0,
                        "A real heavy unlock kept filling the old all-cheap current-army target");
            } else {
                cheapBeforeUnlock += added;
            }
        }
        evidence.observe();
        world.tick();
        evidence.observe();
        const auto* heavy = world.producer(Structure_HeavyFactory);
        launcherUnlocked |= heavy && heavy->isAvailableToBuild(Unit_Launcher);
        if (launcherUnlocked && evidence.heavyCompleted() > 0) break;
    }
    require(evidence.completed(Structure_HeavyFactory) && tankUnlocked && launcherUnlocked
            && cheapBeforeUnlock > 0 && cheapAfterUnlock == 0
            && !evidence.paidUpgrades.empty() && !evidence.paidHeavyUnits.empty() && evidence.heavyCompleted() > 0,
            "Actual paid HF/upgrades failed to switch cheap production to legal tank/launcher composition");
    SDL_Log("TECH_SAVING_REAL_UNLOCK: mod=%s difficulty=%s military_cap=%d first_hf_paid=1 "
            "upgrade_paid=1 tank_legal=1 launcher_legal=1 cheap_before_unlock=%d cheap_after_unlock=%d heavy_completed=%d",
            mod, playerClass, world.bot->militaryValueLimit, cheapBeforeUnlock, cheapAfterUnlock, evidence.heavyCompleted());
}

inline void protectedFirstStarport(const char* mod, const char* playerClass) {
    World world(mod, playerClass, true, Stage::FirstStarport);
    world.cash(world.price(Structure_StarPort) - 1);
    require(world.owner->meetsStarportPopulation() && world.yard->isAvailableToBuild(Structure_StarPort)
            && world.owner->getNumItems(Structure_StarPort) == 0,
            "First-port fixture is not a genuinely legal missing Starport");
    PaidProduction evidence(world);
    const Uint32 deadline = world.game->getGameCycleCount() + MILLI2CYCLES(300000);
    while (!evidence.completed(Structure_StarPort) && world.game->getGameCycleCount() < deadline) evidence.step();
    require(evidence.completed(Structure_StarPort) && world.owner->getNumItems(Structure_StarPort) == 1,
            "Technology savings consumed the legal first Starport's income and construction slot");
    SDL_Log("TECH_SAVING_FIRST_STARPORT: mod=%s difficulty=%s completed=1 paid=1 population_legal=1 full_turns=%d",
            mod, playerClass, world.fullTurns);
}

inline void threatenedDefensiveMinimum(const char* mod, const char* playerClass) {
    World world(mod, playerClass, true, Stage::FirstHeavy);
    world.closeMilitaryMarket();
    world.cash(3000);
    StructureBase* refinery = nullptr;
    for (auto* structure : structureList)
        if (structure->getOwner() == world.owner && structure->getItemID() == Structure_Refinery) {
            refinery = structure;
            break;
        }
    require(refinery, "Threat-scaled defence has no real economic asset");
    int enemies = 0;
    for (int y = 1; y < 63 && enemies < 18; ++y) for (int x = 1; x < 63 && enemies < 18; ++x) {
        const Coord at(x, y);
        if (currentGameMap->getTile(at)->hasAGroundObject()
            || blockDistance(at, refinery->getClosestPoint(at)) > 4) continue;
        auto* tank = world.spawn(world.enemy, Unit_Tank, at);
        Command(world.pilot->getPlayerID(), CMD_UNIT_ATTACKOBJECT, tank->getObjectID(),
                refinery->getObjectID()).executeCommand();
        ++enemies;
    }
    require(enemies == 18, "Threat control could not deploy its visible real hostile army");
    world.bot->updateEnemySightings();
    const auto pass = world.observePlan();
    require(pass.defendingEconomy && !pass.techSavingActive && pass.readyGroundUnits == 18
            && pass.readyGroundValue == 18 * world.price(Unit_Quad)
            && pass.incomeShares == QuantBotSpendingPolicy::Shares{1500, 2000, 6500},
            "Actual base attack did not release optional technology savings using only the house's own army");
    if (world.bot->difficulty >= QuantBot::Difficulty::Hard)
        require(pass.groundThreatValue > 0 && pass.defenceMinimumValue >= pass.groundThreatValue * 3 / 2
                && pass.defenceMinimumValue > pass.readyGroundValue,
                "Visible ground pressure failed to raise the defensive minimum above the six-unit peacetime army");
    world.build();
    require(world.queuedLight() > 0,
            "Threat-scaled defensive shortfall could not replenish affordable cheap units");
    SDL_Log("TECH_SAVING_THREAT_FLOOR: mod=%s difficulty=%s own_units=%d own_value=%d enemy_units=%d "
            "ground_threat_value=%d defensive_minimum=%d tech_saving_active=0 cheap_queued=%d",
            mod, playerClass, pass.readyGroundUnits, pass.readyGroundValue, enemies,
            pass.groundThreatValue, pass.defenceMinimumValue, world.queuedLight());
}

inline void defensiveReplenishment(const char* mod, const char* playerClass) {
    for (const Army army : {Army::Empty, Army::BelowValueFloor}) {
        World world(mod, playerClass, true, Stage::FirstHeavy, army);
        world.closeMilitaryMarket();
        // A real pre-existing construction commitment leaves the LF as the only
        // ready combat lane. It does not manufacture a technology order for us.
        world.yard->doProduceItem(Structure_WindTrap);
        world.cash(3000);
        require(world.bot->ownGroundArmyValue() < world.bot->minWaveValue(),
                "Defensive replenishment control accidentally meets the main-wave value floor");
        world.build();
        require(world.queuedLight() > 0,
                "Optional technology savings suppressed affordable cheap defence below its own-army minimum");
        bool paid = false;
        for (int i = 0; i < 100 && !paid; ++i) {
            world.tick();
            for (const auto* factory : world.lights) paid |= factory->getProductionProgress() > 0;
        }
        require(paid, "Defensive cheap-unit permission did not become real paid production");
        SDL_Log("TECH_SAVING_DEFENSIVE_FLOOR: mod=%s difficulty=%s initial_units=%zu initial_value=%lld "
                "minimum_value=%d cheap_queued=%d cheap_paid=1",
                mod, playerClass, world.initialArmy.size(), static_cast<long long>(world.bot->ownGroundArmyValue()),
                world.bot->minWaveValue(), world.queuedLight());
    }
}

inline void openingWorkersAndPower(const char* mod, const char* playerClass) {
    {
        World world(mod, playerClass, true, Stage::FirstHeavy, Army::ReadyLight, 0);
        auto* port = world.producer(Structure_StarPort);
        require(port && port->isAvailableToBuild(Unit_Harvester), "Opening worker has no genuine import supplier");
        world.owner->getChoam().setNumAvailable(Unit_Harvester, 2);
        const auto* offer = port->getBuildItem(Unit_Harvester);
        require(offer && offer->price > 0, "Opening worker has no actual market price");
        world.cash(int(offer->price));
        const int before = world.owner->getCredits();
        world.build();
        require(world.queued(Unit_Harvester) == 1 && world.owner->getCredits() < before,
                "Tech savings or bucket reserve starved the opening worker's exact paid import budget");
        require(world.bot->getSpendingLedger().balance[1] < 0,
                "Exact-budget opening worker did not bypass and debit its insufficient economy bucket");
        SDL_Log("TECH_SAVING_OPENING_WORKER: mod=%s difficulty=%s worker_paid=1 workers_queued=1 exact_budget=%d",
                mod, playerClass, before);
    }
    {
        World world(mod, playerClass, true, Stage::Advanced);
        if (!world.owner->isPowerRequired()) return;
        std::vector<StructureBase*> generators;
        for (auto* structure : structureList)
            if (structure->getOwner() == world.owner && structure->getItemID() == Structure_WindTrap)
                generators.push_back(structure);
        for (auto* structure : generators) {
            structure->setVisible(VIS_ALL, false);
            structure->destroy();
        }
        require(!world.owner->hasPower(), "Power control did not actually remove the house's generation");
        world.cash(world.price(Structure_WindTrap));
        world.build();
        require(world.queued(Structure_WindTrap) + world.queued(Structure_NuclearPlant) > 0
                && world.queued(Structure_IX) == 0 && world.queued(Structure_Palace) == 0,
                "Optional technology or bucket reserve displaced actual blackout recovery");
        require(world.bot->getSpendingLedger().balance[1] < 0,
                "Blackout recovery did not bypass and debit its insufficient economy bucket");
        bool paid = false;
        for (int i = 0; i < 100 && !paid; ++i) {
            world.tick();
            paid = world.yard->getProductionProgress() > 0;
        }
        require(paid, "Protected emergency generation never received real payment");
        SDL_Log("TECH_SAVING_POWER: mod=%s difficulty=%s generation_queued=1 generation_paid=1 tech_queued=0",
                mod, playerClass);
    }
}

inline void botSaveRoundTrips(World& world) {
    using Ledger = QuantBotSpendingPolicy::Ledger;
    struct RestoreVersion {
        Game& game;
        Uint32 saved;
        ~RestoreVersion() { game.loadedSavegameVersion = saved; }
    } restore{*world.game, world.game->loadedSavegameVersion};
    const auto expected = world.bot->getSpendingLedger();
    require(expected.initialized && expected.balance != std::array<int64_t, 3>{},
            "Bot save control has no initialized nonzero savings ledger");
    OMemoryStream out;
    out.open();
    world.bot->save(out);
    const std::string bytes(out.getData(), out.getDataLength());
    OMemoryStream trailer;
    trailer.open();
    expected.save(trailer);
    require(trailer.getDataLength() == Ledger::savedBytes && bytes.size() > Ledger::savedBytes
            && bytes.compare(bytes.size() - Ledger::savedBytes, Ledger::savedBytes,
                             trailer.getData(), trailer.getDataLength()) == 0,
            "Current QuantBot's savings suffix differs from the real ledger encoding");
    out.writeUint32(0xabcdef01u);
    world.game->loadedSavegameVersion = SAVEGAMEVERSION;
    {
        IMemoryStream input(out.getData(), out.getDataLength());
        QuantBot restored(input, world.owner);
        const auto& ledger = restored.getSpendingLedger();
        require(ledger.initialized == expected.initialized && ledger.lastWealth == expected.lastWealth
                && ledger.balance == expected.balance && input.readUint32() == 0xabcdef01u
                && input.getRemainingLength() == 0,
                "Current QuantBot reload changed bucket balances or consumed the following record");
        OMemoryStream again;
        again.open();
        restored.save(again);
        require(std::string(again.getData(), again.getDataLength()) == bytes,
                "Current QuantBot ledger did not re-save byte-identically");
    }
    const std::string prefix = bytes.substr(0, bytes.size() - Ledger::savedBytes);
    OMemoryStream sentinel;
    sentinel.open();
    sentinel.writeUint32(0xabcdef01u);
    for (const Uint32 version : {9882u, 9883u}) {
        const std::string legacy = prefix + std::string(sentinel.getData(), sentinel.getDataLength());
        world.game->loadedSavegameVersion = version;
        IMemoryStream input(legacy.data(), legacy.size());
        QuantBot old(input, world.owner);
        const auto& empty = old.getSpendingLedger();
        require(!empty.initialized && empty.lastWealth == 0 && empty.balance == std::array<int64_t, 3>{}
                && input.readUint32() == 0xabcdef01u && input.getRemainingLength() == 0,
                "An old QuantBot block invented savings or lost following-record alignment");
        OMemoryStream upgraded;
        upgraded.open();
        old.save(upgraded);
        upgraded.writeUint32(0xabcdef01u);
        world.game->loadedSavegameVersion = SAVEGAMEVERSION;
        IMemoryStream upgradedInput(upgraded.getData(), upgraded.getDataLength());
        QuantBot reloaded(upgradedInput, world.owner);
        const auto& zero = reloaded.getSpendingLedger();
        require(!zero.initialized && zero.lastWealth == 0 && zero.balance == std::array<int64_t, 3>{}
                && upgradedInput.readUint32() == 0xabcdef01u && upgradedInput.getRemainingLength() == 0,
                "An upgraded old QuantBot did not retain its explicit zero-ledger state");
        require(upgraded.getDataLength() == bytes.size() + sentinel.getDataLength(),
                "Upgrading an old QuantBot added more than the single frozen ledger suffix");
        SDL_Log("TECH_SAVING_OLD_BOT_SAVE: old_version=%u current_version=%u zero_ledger=1 sentinel=1 consumed=1",
                version, SAVEGAMEVERSION);
    }
    SDL_Log("TECH_SAVING_BOT_SAVE: current_version=%u exact_ledger=1 byte_identical=1 sentinel=1 consumed=1",
            SAVEGAMEVERSION);
}

inline void unavailableProjectsDoNotFreezeArmy(const char* mod, const char* playerClass) {
    for (int gate = 0; gate < 3; ++gate) {
        World world(mod, playerClass, true, gate == 2 ? Stage::FirstPalace : Stage::FirstHeavy);
        world.closeMilitaryMarket();
        world.cash(2999);
        if (gate == 0) {
            world.game->objectData.data[Structure_HeavyFactory][HOUSE_HARKONNEN].enabled = false;
            world.yard->updateBuildList();
        } else if (gate == 1) {
            for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
                auto* tile = currentGameMap->getTile(x, y);
                if (!tile->hasAGroundObject()) tile->setType(Terrain_Mountain);
            }
            world.bot->clearPlacementCache();
        } else {
            world.bot->lastCalculatedSpice = 0;
            if (world.game->isCitySimEnabled()) {
                auto& state = world.game->getCitySimulation()->houseState_[HOUSE_HARKONNEN];
                state.taxBaseEighths = 0;
            }
        }
        world.build();
        int militaryQueued = 0;
        for (const Uint32 item : vehicles) militaryQueued += world.queued(item);
        require(militaryQueued > 0 && world.queued(Structure_HeavyFactory) == 0
                && world.queued(Structure_Palace) == 0,
                "Unavailable, unplaceable or income-ineligible technology froze legal funded army production");
        SDL_Log("TECH_SAVING_INELIGIBLE: mod=%s difficulty=%s cause=%s military_queued=%d tech_queued=0",
                mod, playerClass, gate == 0 ? "disabled_content" : gate == 1 ? "no_site" : "no_income",
                militaryQueued);
    }
}

inline void liveIncomeBuckets(const char* mod, const char* playerClass) {
    World world(mod, playerClass, true, Stage::Complete);
    world.cash(0);
    world.build();
    const auto empty = world.bot->getSpendingLedger();
    require(empty.initialized && empty.lastWealth == 0 && empty.balance == std::array<int64_t, 3>{},
            "Forecast income was deposited before real credits arrived");

    // Explicit tiny windfalls are isolated receipt controls, not a substitute
    // for the genuine harvesting that pays the progression fixture. They stay
    // below every real offer so no accepted order can obscure the split.
    world.owner->addCredits(10);
    world.build();
    const auto safe = world.bot->getSpendingLedger();
    require(safe.balance == std::array<int64_t, 3>{30000, 35000, 35000} && safe.lastWealth == 10,
            "Safe real cash was not split exactly 30/35/35 between the three buckets");
    world.build();
    require(world.bot->getSpendingLedger().balance == safe.balance,
            "Repeated planning counted the same receipt twice");

    auto* bird = world.spawn(world.enemy, Unit_Ornithopter, world.yard->getLocation() + Coord(3, 1));
    Command(world.pilot->getPlayerID(), CMD_UNIT_SETMODE, bird->getObjectID(), HOLD).executeCommand();
    world.owner->addCredits(5);
    world.build();
    const auto threatened = world.bot->getSpendingLedger();
    require(threatened.balance == std::array<int64_t, 3>{37500, 45000, 67500} && threatened.lastWealth == 15,
            "Observed hostile air did not move only new receipts to the exact 15/20/65 threat shares");
    botSaveRoundTrips(world);
    SDL_Log("TECH_SAVING_BUCKETS: mod=%s difficulty=%s safe_receipt=10 safe_bps=3000,3500,3500 "
            "threat_receipt=5 threat_bps=1500,2000,6500 no_forecast_credit=1 repeated_receipt=0 "
            "production_raw=%lld economy_raw=%lld military_raw=%lld",
            mod, playerClass, static_cast<long long>(threatened.balance[0]),
            static_cast<long long>(threatened.balance[1]), static_cast<long long>(threatened.balance[2]));
}

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

inline void freshCityReachesMilitaryAndTechnology(const char* mod, const char* playerClass) {
    if (!ModManager::instance().isDuneCityContentActive()) return;
    World world(mod, playerClass, true, Stage::NaturalOpening, Army::Empty, 0);
    PaidProduction evidence(world);
    const int minimum = std::max(3000, world.bot->minWaveValue());
    bool firstSupplierBelowFloor = false, firstSupplierHasWorkerAndPower = false;
    bool reachedFloor = false, dispatched = false;
    int lotsAtSupplier = -1;
    Uint32 floorCycle = 0;
    const Uint32 end = world.game->getGameCycleCount() + MILLI2CYCLES(1800000);
    bool complete = false;
    while (world.game->getGameCycleCount() < end) {
        evidence.step();
        const auto [units, value] = actualGroundReadiness(world);
        if (lotsAtSupplier < 0 && (evidence.paidTechnology.count(Structure_LightFactory)
                                  || evidence.paidTechnology.count(Structure_HeavyFactory))) {
            firstSupplierBelowFloor = units < 6 || value < minimum;
            firstSupplierHasWorkerAndPower = world.owner->getNumHarvesters() > 0 && world.owner->hasPower();
            lotsAtSupplier = builtCityLots(world);
        }
        if (!reachedFloor && units >= 6 && value >= minimum) {
            reachedFloor = true;
            floorCycle = world.game->getGameCycleCount();
        }
        if (reachedFloor && !dispatched && world.game->getGameCycleCount() % 50 == 0)
            dispatched = world.bot->tryLaunchWave(false);
        if (reachedFloor && world.game->getGameCycleCount() - floorCycle > MILLI2CYCLES(90000))
            require(dispatched, "Fresh-city funded technology indefinitely starved an actual dispatch-capable army");
        const auto* heavy = world.producer(Structure_HeavyFactory);
        complete = firstSupplierBelowFloor && firstSupplierHasWorkerAndPower && reachedFloor && dispatched
            && evidence.completed(Structure_StarPort) && evidence.completed(Structure_HeavyFactory)
            && evidence.factoryUnitCompleted() && evidence.heavyCompleted() > 0
            && !evidence.paidUpgrades.empty() && heavy && heavy->getCurrentUpgradeLevel() > 0
            && builtCityLots(world) > lotsAtSupplier
            && world.owner->getHarvestedSpice() + world.owner->getCityTaxReceipts() > 0;
        if (complete) break;
    }
    if (!complete) {
        const auto pass = world.observePlan();
        const auto [units, value] = actualGroundReadiness(world);
        SDL_Log("TECH_SAVING_FRESH_CITY_BLOCKED: mod=%s difficulty=%s supplier_below_floor=%d "
                "supplier_worker_power=%d ready_units=%d ready_value=%d minimum=%d dispatched=%d "
                "LF=%d HF=%d SP=%d heavy_completed=%d paid_upgrades=%zu lots_before=%d lots_now=%d "
                "income_4m=%d cash=%d production_raw=%lld economy_raw=%lld military_raw=%lld",
                mod, playerClass, int(firstSupplierBelowFloor), int(firstSupplierHasWorkerAndPower),
                units, value, minimum, int(dispatched), world.owner->getNumItems(Structure_LightFactory),
                world.owner->getNumItems(Structure_HeavyFactory), world.owner->getNumItems(Structure_StarPort),
                evidence.heavyCompleted(), evidence.paidUpgrades.size(), lotsAtSupplier, builtCityLots(world),
                pass.forecastNetIncome, world.owner->getCredits(),
                static_cast<long long>(world.bot->getSpendingLedger().balance[0]),
                static_cast<long long>(world.bot->getSpendingLedger().balance[1]),
                static_cast<long long>(world.bot->getSpendingLedger().balance[2]));
    }
    require(complete, "CY-only City opening did not pay its first supplier, reach the real army floor, "
                      "and continue to Starport/HF/upgrades, dispatch and economic growth");
    SDL_Log("TECH_SAVING_FRESH_CITY: mod=%s difficulty=%s initial_structures=1 initial_units=0 "
            "initial_grant=4000 prebuilt_economy=0 supplier_paid_below_floor=1 worker_power_preserved=1 "
            "paid_factory_unit=1 ready_floor=1 starport_paid_placed=1 heavy_paid_placed=1 "
            "heavy_upgrade_paid=1 heavy_unit_completed=1 continued_city_growth=1 dispatch=1 cycle=%u",
            mod, playerClass, world.game->getGameCycleCount());
}

inline void debtCannotBlockReceiptCapacity(const char* mod, const char* playerClass) {
    if (!ModManager::instance().isDuneCityContentActive()) return;
    World world(mod, playerClass, true, Stage::Complete);
    world.closeMilitaryMarket();
    auto config = std::make_shared<QuantBotConfig>(world.bot->botConfig());
    config->force.storageCapacity = false; // TechSavings must close its own receipt-capacity cycle.
    world.bot->configOverride = config;
    int realCapacity = 0;
    for (const auto* structure : structureList)
        if (structure->getOwner() == world.owner)
            realCapacity += world.game->objectData.data[structure->getItemID()][world.owner->getHouseID()].capacity;
    require(realCapacity > 800 && realCapacity <= 35800, "Debt control's physical storage is not a valid capped snapshot");
    world.owner->capacity = realCapacity;
    world.cash(0);
    world.owner->storedCredits = realCapacity - 1;
    using Bucket = QuantBotSpendingPolicy::Bucket;
    auto& ledger = world.bot->spendingLedger;
    ledger = {};
    // Valid paused historical receipts/accepted costs, not an asserted new
    // purchase. Debt remains exact and no other wallet is transferred into it.
    ledger.reconcile(100000, QuantBotSpendingPolicy::incomeShares(false));
    ledger.charge(Bucket::Production, 28000);
    ledger.charge(Bucket::Economy, 36200);
    ledger.charge(Bucket::Military, 35800 - (realCapacity - 1));
    require(ledger.lastWealth == world.netWealth() && ledger.balance[1] == -1200 * int64_t(10000)
            && ledger.available(Bucket::Production) == 2000 && world.owner->getEarnedCreditRoom() <= 1,
            "Debt-cap control did not preserve real wealth, negative Economy and funded Production");
    auto pass = world.observePlan();
    const Uint32 storage = world.bot->storageExpansionItem(pass, world.yard);
    require(storage != NONE_ID
            && world.game->objectData.data[storage][world.owner->getHouseID()].capacity > 0,
            "Full real vault and positive income did not select a bounded legal receipt-capacity project");
    const int quote = world.bot->buildingCapitalCost(pass, storage);
    const int64_t debtBefore = ledger.balance[1];
    const FixPoint receiptsBefore = world.owner->getHarvestedSpice() + world.owner->getCityTaxReceipts();
    PaidProduction evidence(world);
    world.build();
    evidence.observe();
    require(world.queued(storage) > 0 && ledger.balance[1] <= debtBefore - int64_t(quote) * 10000,
            "Negative Economy blocked critical paid storage or its emergency charge erased the debt");
    bool negativeDebtRefilled = false;
    const Uint32 end = world.game->getGameCycleCount() + MILLI2CYCLES(300000);
    while (world.game->getGameCycleCount() < end
           && !(evidence.completed(storage) && world.owner->getCapacity() > realCapacity && negativeDebtRefilled)) {
        const int64_t before = ledger.balance[1];
        evidence.step();
        negativeDebtRefilled |= before < 0 && ledger.balance[1] < 0 && ledger.balance[1] > before;
    }
    require(evidence.completed(storage) && world.owner->getCapacity() > realCapacity
            && negativeDebtRefilled
            && world.owner->getHarvestedSpice() + world.owner->getCityTaxReceipts() > receiptsBefore,
            "Critical storage did not actually pay/place, expand real capacity and refill still-negative debt from real receipts");
    SDL_Log("TECH_SAVING_DEBT_CAPACITY: mod=%s difficulty=%s storage=%u quote=%d old_capacity=%d "
            "new_capacity=%d negative_economy_borrowed=1 exact_charge=1 real_receipts=1 "
            "negative_debt_refilled=1 storage_switch_off=1",
            mod, playerClass, storage, quote, realCapacity, world.owner->getCapacity());
}

inline void actualLowTechResearchReachesFirstSupplier(const char* mod, const char* playerClass) {
    if (!ModManager::instance().isDuneCityContentActive()) return;
    World world(mod, playerClass, true, Stage::NaturalResearch, Army::Empty, 0);
    PaidProduction evidence(world);
    std::map<int, Uint32> started;
    std::set<int> completed;
    const Uint32 end = world.game->getGameCycleCount() + MILLI2CYCLES(1200000);
    while (world.game->getGameCycleCount() < end && !evidence.factoryUnitCompleted()) {
        if (world.game->getGameCycleCount() % 25 == 0) {
            // Reconcile the real receipts before taking the payment snapshot,
            // so the quote cannot be hidden by a simultaneous wallet refill.
            world.observePlan();
            const bool active = world.owner->isTechResearchActive();
            const int cost = world.owner->getTechResearchCost();
            const int cash = world.owner->getCredits();
            const int64_t production = world.bot->getSpendingLedger().balance[0];
            world.build();
            if (!active && world.owner->isTechResearchActive()) {
                const int target = world.owner->getTechResearchTarget();
                require(target == 2 || target == 3,
                        "First-supplier research waiver leaked into optional research before the actual army floor");
                require(world.owner->getCredits() <= cash - cost
                        && world.bot->getSpendingLedger().balance[0] <= production - int64_t(cost) * 10000
                        && world.owner->getTechResearchRemaining() == world.owner->techResearchCycles(target),
                        "Low-tech research did not debit the full real cash/Production quote and start its real duration");
                started.emplace(target, world.game->getGameCycleCount());
            }
        }
        evidence.observe();
        world.tick();
        evidence.observe();
        for (const auto& [target, cycle] : started)
            if (world.owner->getTechLevel() >= target) {
                require(world.game->getGameCycleCount() - cycle + 1 >= Uint32(world.owner->techResearchCycles(target)),
                        "Low-tech supplier research completed without its actual engine duration");
                completed.insert(target);
            }
    }
    const auto [units, value] = actualGroundReadiness(world);
    require(evidence.completed(Structure_ResearchLab) && completed == std::set<int>{2, 3}
            && world.owner->getTechLevel() == 3 && evidence.completed(Structure_LightFactory)
            && evidence.factoryUnitCompleted()
            && (units < 6 || value < std::max(3000, world.bot->minWaveValue())),
            "Actual T1 City opening did not pay its lab, both studies and first legal factory unit below the army floor");
    const auto pass = world.observePlan();
    const StructureBase* lab = nullptr;
    for (const auto* structure : structureList)
        if (structure->getOwner() == world.owner && structure->getItemID() == Structure_ResearchLab) {
            lab = structure;
            break;
        }
    const int optionalCost = world.owner->getTechResearchCost();
    require(lab && optionalCost > 0 && world.owner->canStartTechResearch(lab->getObjectID())
            && world.bot->getSpendingLedger().available(QuantBotSpendingPolicy::Bucket::Production) >= optionalCost
            && world.owner->getCredits() >= optionalCost + pass.techWorkingReserve && !pass.techSavingActive,
            "Optional-research floor control is not genuinely legal and fully funded after the first supplier");
    for (int i = 0; i < 75; ++i) {
        evidence.step();
        const auto ready = actualGroundReadiness(world);
        require(ready.first < 6 || ready.second < std::max(3000, world.bot->minWaveValue()),
                "Optional-research observation outlived its actual below-floor premise");
        require(!world.owner->isTechResearchActive() && world.owner->getTechLevel() == 3,
                "Funded optional T4 research bypassed the army floor after the first legal supplier was reachable");
    }
    SDL_Log("TECH_SAVING_ACTUAL_T1: mod=%s difficulty=%s initial_tech=1 initial_structures=1 initial_units=0 "
            "research_pace=normal lab_paid_placed=1 research2_paid_elapsed=1 research3_paid_elapsed=1 "
            "light_factory_paid_placed=1 factory_unit_paid_completed=1 funded_optional_research_blocked=1 cycle=%u",
            mod, playerClass, world.game->getGameCycleCount());
}

inline std::vector<UnitBase*> spawnGroundBand(World& world, House* owner, Uint32 item, int count,
                                              Coord center, int minimum, int maximum) {
    std::vector<Coord> sites;
    for (int y = 1; y < 63; ++y) for (int x = 1; x < 63; ++x) {
        const Coord at(x, y);
        const int distance = blockDistance(at, center);
        if (!currentGameMap->getTile(at)->hasAGroundObject() && distance >= minimum && distance <= maximum)
            sites.push_back(at);
    }
    std::sort(sites.begin(), sites.end(), [&](Coord a, Coord b) {
        const int da = blockDistance(a, center), db = blockDistance(b, center);
        if (da != db) return da < db;
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });
    require(int(sites.size()) >= count, "Local-defence control cannot physically deploy its finite scenario army");
    std::vector<UnitBase*> result;
    for (int i = 0; i < count; ++i) result.push_back(world.spawn(owner, item, sites[i]));
    return result;
}

inline void seedExhaustedMilitary(World& world) {
    using Bucket = QuantBotSpendingPolicy::Bucket;
    world.cash(10000);
    auto& ledger = world.bot->spendingLedger;
    ledger = {};
    // Exact historical receipt/spend snapshot with neither a Military balance
    // nor a deficit in either other wallet. No asserted order is prepaid here.
    ledger.reconcile(50000, QuantBotSpendingPolicy::incomeShares(false));
    ledger.charge(Bucket::Production, 10000);
    ledger.charge(Bucket::Economy, 12500);
    ledger.charge(Bucket::Military, 17500);
    require(ledger.lastWealth == world.netWealth() && ledger.balance[2] == 0
            && ledger.available(Bucket::Production) == 5000 && ledger.available(Bucket::Economy) == 5000,
            "Local-emergency control is not an exact wealth-conserving exhausted-Military snapshot");
}

inline void onlyUnmetLocalGroundDefenceBorrows(const char* mod, const char* playerClass) {
    for (const bool local : {true, false}) {
        // A finite large scenario army isolates locality, not the authored
        // Easy/Medium unit cap. The ordinary composition cases keep that cap.
        World world(mod, playerClass, true, Stage::FirstHeavy, Army::ReadyLight, 8,
                    false, HOUSE_HARKONNEN, 3, 0);
        world.closeMilitaryMarket(); // Quad is genuinely the only legal vehicle, not a cheap fallback.
        StructureBase* asset = nullptr;
        for (auto* structure : structureList)
            if (structure->getOwner() == world.owner && structure->getItemID() == Structure_Refinery) {
                asset = structure;
                break;
            }
        require(asset, "Local-emergency control has no real protected income asset");
        const Coord center = asset->getLocation();
        const auto attackers = spawnGroundBand(world, world.enemy, Unit_Tank, 18, center, 1, 4);
        for (auto* tank : attackers)
            Command(world.pilot->getPlayerID(), CMD_UNIT_ATTACKOBJECT, tank->getObjectID(),
                    asset->getObjectID()).executeCommand();
        const auto defenders = spawnGroundBand(world, world.owner, Unit_SiegeTank, 60, center,
                                               local ? 1 : 25, local ? 8 : 64);
        for (const auto* defender : defenders)
            require(local || blockDistance(defender->getLocation(), center) > 24,
                    "Distant-army control accidentally supplied local mobile defence");
        seedExhaustedMilitary(world);
        world.bot->updateEnemySightings();
        const auto pass = world.observePlan();
        require(pass.defendingEconomy && pass.readyGroundUnits >= 6
                && pass.readyGroundValue >= pass.defenceMinimumValue
                && pass.incomeShares == QuantBotSpendingPolicy::Shares{1500, 2000, 6500}
                && pass.militaryEmergency == !local,
                "Military emergency used global army/air pressure instead of the actual unmet local ground defence");
        for (int i = 0; i < 5; ++i) world.build();
        if (local) {
            require(world.queuedLight() == 0 && world.bot->getSpendingLedger().balance[2] == 0,
                    "Strong local defence authorized unlimited non-AA cheap borrowing from an empty Military wallet");
        } else {
            require(world.queuedLight() > 0 && world.bot->getSpendingLedger().balance[2] < 0,
                    "A huge distant army blocked affordable local emergency reinforcement and its exact borrowed charge");
            bool paid = false;
            for (int i = 0; i < 100 && !paid; ++i) {
                world.tick();
                for (const auto* factory : world.lights) paid |= factory->getProductionProgress() > 0;
            }
            require(paid, "Permitted local emergency reinforcement never became actual paid factory production");
        }
        SDL_Log("TECH_SAVING_LOCAL_EMERGENCY: mod=%s difficulty=%s local_army=%d "
                "attackers=18 defenders=60 globally_above_floor=1 military_wallet_initial=0 "
                "military_emergency=%d cheap_queued=%d military_raw=%lld",
                mod, playerClass, int(local), int(pass.militaryEmergency), world.queuedLight(),
                static_cast<long long>(world.bot->getSpendingLedger().balance[2]));
    }
}

inline void airOnlyPressureBorrowsOnlyAntiAir(const char* mod, const char* playerClass) {
    if (!ModManager::instance().isDuneCityContentActive()) return;
    World world(mod, playerClass, true, Stage::FirstHeavy);
    world.closeMilitaryMarket();
    auto* bird = world.spawn(world.enemy, Unit_Ornithopter, Coord(11, 9));
    Command(world.pilot->getPlayerID(), CMD_UNIT_SETMODE, bird->getObjectID(), HOLD).executeCommand();
    seedExhaustedMilitary(world);
    world.bot->updateEnemySightings();
    const auto pass = world.observePlan();
    require(pass.airEngaged && pass.hostileAircraft > 0 && !pass.militaryEmergency
            && pass.readyGroundUnits >= 6 && pass.readyGroundValue >= pass.defenceMinimumValue
            && pass.incomeShares == QuantBotSpendingPolicy::Shares{1500, 2000, 6500},
            "Air-only pressure incorrectly became a ground military emergency or lost its threatened receipts/AA floor");
    for (int i = 0; i < 5; ++i) world.build();
    require(world.queuedLight() == 0
            && world.queued(Unit_Trooper) + world.queued(Unit_Troopers) > 0
            && world.bot->getSpendingLedger().balance[2] < 0,
            "Air-only pressure borrowed for ordinary cheap units or starved explicit reactive anti-air infantry");
    bool paid = false;
    for (int i = 0; i < 100 && !paid; ++i) {
        world.tick();
        for (const auto* structure : structureList) {
            const auto* factory = dynamic_cast<const BuilderBase*>(structure);
            if (!factory || factory->getOwner() != world.owner || factory->getProductionProgress() <= 0) continue;
            paid |= factory->getCurrentProducedItem() == Unit_Trooper
                || factory->getCurrentProducedItem() == Unit_Troopers;
        }
    }
    require(paid, "Preserved air-only reactive infantry floor did not receive actual engine payment");
    SDL_Log("TECH_SAVING_AIR_ONLY: mod=%s difficulty=%s ground_emergency=0 threatened_bps=1500,2000,6500 "
            "military_wallet_initial=0 ordinary_cheap_queued=0 anti_air_borrowed=1 anti_air_paid=1",
            mod, playerClass);
}

} // namespace TechSavingProbe

void runTechSavingProbe(const char* mod, const char* playerClass) {
    struct RestoreConfig {
        QuantBotConfig saved = getQuantBotConfig();
        ~RestoreConfig() { getQuantBotConfig() = saved; }
    } restore;
    for (const bool causalControlsOff : {false, true}) {
        TechSavingProbe::currentAndQueuedComposition(mod, playerClass, false, causalControlsOff);
        TechSavingProbe::currentAndQueuedComposition(mod, playerClass, true, causalControlsOff);
        TechSavingProbe::paidTechnologyProgression(mod, playerClass, causalControlsOff);
    }
    TechSavingProbe::rejectedFactoryGrowthDoesNotStarveArmy(mod, playerClass);
    TechSavingProbe::rejectedTransportGrowthDoesNotStarveAir(mod, playerClass);
    TechSavingProbe::protectedFirstStarport(mod, playerClass);
    TechSavingProbe::foundedTechnologyQuote(mod, playerClass);
    TechSavingProbe::realUnlockStopsCheapReplenishment(mod, playerClass);
    TechSavingProbe::defensiveReplenishment(mod, playerClass);
    TechSavingProbe::threatenedDefensiveMinimum(mod, playerClass);
    TechSavingProbe::openingWorkersAndPower(mod, playerClass);
    TechSavingProbe::unavailableProjectsDoNotFreezeArmy(mod, playerClass);
    TechSavingProbe::liveIncomeBuckets(mod, playerClass);
    TechSavingProbe::onlyUnmetLocalGroundDefenceBorrows(mod, playerClass);
    TechSavingProbe::airOnlyPressureBorrowsOnlyAntiAir(mod, playerClass);
    TechSavingProbe::debtCannotBlockReceiptCapacity(mod, playerClass);
    TechSavingProbe::freshCityReachesMilitaryAndTechnology(mod, playerClass);
    TechSavingProbe::actualLowTechResearchReachesFirstSupplier(mod, playerClass);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=tech_saving outcome=passed mod=%s difficulty=%s "
            "producer_scope=full_build paid_progress=1 legacy_off=1 queued_composition=1 causal_controls_off=1",
            mod, playerClass);
}
