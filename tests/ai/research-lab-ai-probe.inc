// Private balance executable only. Initial assets are ordinary tech-1 scenario
// assets. The measured lab is selected by full QuantBot::build(), paid by the
// real Construction Yard, placed by the production AI and researched by House.
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
#include <players/QuantBotConfig.h>
#include <structures/ConstructionYard.h>
#include <structures/ResearchLab.h>
#include <structures/ZoneStructure.h>
#include <units/Harvester.h>

#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ResearchLabAIProbe {

inline void require(bool value, const char* why) {
    if (!value) {
        SDL_Log("RESEARCH_LAB_AI_FAIL: %s", why);
        throw std::runtime_error(why);
    }
}

enum class Control { Solvent, RuleOff, MissingYard, NoCash, ExistingLab };

inline std::string mapText(Control control) {
    std::ostringstream map;
    map << "[BASIC]\nVersion=2\nWinFlags=3\nLoseFlags=1\nTechLevel=1\n"
        << "[MAP]\nSizeX=64\nSizeY=64\n";
    for (int y = 0; y < 64; ++y) {
        char row[16];
        std::snprintf(row, sizeof(row), "%03d=", y);
        map << row << std::string(64, '%') << '\n';
    }
    // Named houses keep the same assets and locations in both switch arms.
    map << "[Harkonnen]\nCredits=" << (control == Control::NoCash ? 0 : 30000)
        << "\nBrain=Team1\n[Atreides]\nCredits=1000\nBrain=Team2\n[STRUCTURES]\n"
        << "ID000=Harkonnen," << (control == Control::MissingYard ? "Windtrap" : "Const Yard")
        << ",256," << 8 + 8 * 64 << '\n'
        << "ID001=Atreides,Const Yard,256," << 56 + 54 * 64 << '\n';
    return map.str();
}

struct World {
    std::unique_ptr<Game> game;
    House* owner = nullptr;
    QuantBot* bot = nullptr;
    ConstructionYard* yard = nullptr;
    std::string hash;

    World(const char* mod, const char* playerClass, bool enabled, Control control = Control::Solvent) {
        require(currentGame == nullptr, "Lab fixtures share live engine globals");
        require(ModManager::instance().setActiveMod(mod), "Cannot activate lab fixture mod");
        getQuantBotConfig().strategy.random = false;
        getQuantBotConfig().strategy.fixedMix = QuantBotStrategy::Mix::Learned;
        getQuantBotConfig().strategy.fixedTempo = QuantBotStrategy::Tempo::Base;
        auto options = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
        options.fogOfWar = false;
        options.startWithExploredMap = true;
        options.immortalHumanPlayer = true;
        options.instantBuild = false;
        options.concreteRequired = false;
        options.researchLab = control != Control::RuleOff;
        options.researchPace = TechResearch::Pace::Normal;
        options.maximumNumberOfHarvestersOverride = 8;
        GameInitSettings init("research-lab-ai.ini", mapText(control), true, options);
        init.randomSeed = static_cast<Uint32>(std::stoul(std::getenv("BALANCE_SEED")));
        GameInitSettings::HouseInfo defender(HOUSE_HARKONNEN, 1);
        defender.addPlayerInfo({"Research progression QuantBot", playerClass});
        init.addHouseInfo(defender);
        GameInitSettings::HouseInfo opponent(HOUSE_ATREIDES, 2);
        opponent.addPlayerInfo({settings.general.playerName, HUMANPLAYERCLASS});
        init.addHouseInfo(opponent);
        game = std::make_unique<Game>();
        currentGame = game.get();
        game->bReplay = true;
        game->initGame(init);
        game->bReplay = false;
        game->initializeGameLoop();
        game->bPause = false;
        game->frameTiming.unitPhases = false;
        owner = game->getHouse(HOUSE_HARKONNEN);
        require(owner && game->gameType == GameType::CustomGame && owner->getTechLevel() == 1,
                "Lab fixture is not a real tech-1 Custom match");
        for (const auto& player : owner->getPlayerList())
            if (auto* candidate = dynamic_cast<QuantBot*>(player.get())) bot = candidate;
        for (auto* structure : structureList)
            if (structure->getOwner() == owner && structure->getItemID() == Structure_ConstructionYard)
                yard = dynamic_cast<ConstructionYard*>(structure);
        require(bot && pLocalHouse == game->getHouse(HOUSE_ATREIDES) && game->localHumanImmortalityApplies(),
                "Missing registered QuantBot or passive real Human opponent");
        require((yard != nullptr) == (control != Control::MissingYard), "Wrong initial lab supplier");
        auto config = std::make_shared<QuantBotConfig>(bot->botConfig());
        config->force.researchLab = true;
        config->force.researchLabProgression = enabled;
        config->force.techSavings = false;
        config->force.simpleTechSavings = false;
        bot->configOverride = config;
        hash = config->getConfigHash();

        // These assets are all available at match tech 1. In particular there
        // is no free Starport, factory, Silo, Radar or higher-tech worker supplier.
        for (int x : {4, 8, 12, 16, 20, 24}) place(Structure_WindTrap, Coord(x, 3));
        place(Structure_Refinery, Coord(5, 13));
        place(Structure_Refinery, Coord(12, 13));
        if (game->isCitySimEnabled()) {
            place(Structure_PoliceStation, Coord(6, 21));
            place(Structure_PoliceStation, Coord(24, 21));
            for (int x : {10, 13, 16, 19}) zone(Structure_ZoneResidential, Coord(x, 25));
            for (int x : {10, 13}) zone(Structure_ZoneCommercial, Coord(x, 21));
            for (int x : {16, 19}) zone(Structure_ZoneIndustrial, Coord(x, 21));
            for (int y = 2; y < 35; ++y) for (int x = 2; x < 35; ++x)
                if (!currentGameMap->getTile(x, y)->hasAStructure()) {
                    owner->placeStructure(NONE_ID, Structure_Road, x, y, true, true);
                    require(currentGameMap->getTile(x, y)->isRoad(), "Initial city road placement failed");
                }
        }
        for (int y = 6; y < 35; ++y) for (int x = 39; x < 62; ++x) {
            auto* tile = currentGameMap->getTile(x, y);
            tile->setType(Terrain_Spice);
            tile->setSpice(1000);
        }
        while (owner->getNumItems(Unit_Harvester) < 2) {
            auto* worker = dynamic_cast<Harvester*>(owner->createUnit(Unit_Harvester, true));
            require(worker, "Could not create the scenario's working harvester");
            worker->deploy(Coord(39 + owner->getNumItems(Unit_Harvester), 16));
            worker->doSetAttackMode(HARVEST);
        }
        require(owner->getNumItems(Unit_Harvester) == 2, "Unexpected free opening harvesters");
        if (control == Control::ExistingLab) place(Structure_ResearchLab, legalLabSite());
        game->gameCycleCount = MILLI2CYCLES(300000);
        if (game->isCitySimEnabled()) initializePhysicalCity();
        owner->updateBuildLists();
        bot->buildTimer = INT_MAX;
        bot->attackTimer = INT_MAX;
        // Let production initialization derive the difficulty's actual worker
        // target from this map and its real initial assets; do not fake a fleet gap.
        bot->update();
        require(!bot->infantryStrategy(), "Lab fixture inherited an unrelated Infantry strategy");
        require(owner->getNumItems(Structure_HeavyFactory) == 0
                && owner->getNumItems(Structure_StarPort) == 0,
                "Tech-1 fixture contains an impossible higher-tech supplier");
    }

    ~World() {
        game.reset();
        currentGame = nullptr;
        pLocalPlayer = nullptr;
        pLocalHouse = nullptr;
    }

    StructureBase* place(Uint32 item, Coord at) {
        const auto& data = game->objectData.data[item][owner->getFactionID()];
        require(data.enabled && data.techLevel <= 1, "Initial asset is not a shipped tech-1 building");
        auto* structure = owner->placeStructure(NONE_ID, item, at.x, at.y, true, true);
        require(structure, "Initial lab scenario structure placement failed");
        return structure;
    }

    void zone(Uint32 item, Coord at) {
        auto* structure = dynamic_cast<ZoneStructure*>(place(item, at));
        require(structure, "City fixture lot is not a real zone");
        structure->setStartingLevel(1);
    }

    void initializePhysicalCity() {
        auto* sim = game->getCitySimulation();
        require(sim && sim->isInitialized(), "Real city simulation did not initialize");
        auto& state = sim->getHouseStateMut(owner->getHouseID());
        int residential = 0, commercial = 0, industrial = 0, taxBase = 0;
        for (auto* structure : structureList) {
            if (structure->getOwner() != owner) continue;
            const auto* lot = dynamic_cast<const ZoneStructure*>(structure);
            if (!lot) structure->setCityOccupancy(1);
            const int level = lot ? currentGameMap->getTile(structure->getLocation())->getCityZoneDensity()
                                  : structure->getCityOccupancy();
            const int population = DuneCity::getStructurePopulation(structure, level);
            switch (DuneCity::getStructureCityRole(structure->getItemID())) {
            case DuneCity::CityRole::Residential: residential += population; break;
            case DuneCity::CityRole::Commercial: commercial += population; break;
            case DuneCity::CityRole::Industrial: industrial += population; break;
            default: break;
            }
            taxBase += DuneCity::getStructureTaxBaseEighths(structure, level);
        }
        // Seed a census of physical map assets, not invented population, taxes,
        // power or crime. The real engine recomputes every value immediately.
        state.resPop = state.prevResPop = residential;
        state.comPop = state.prevComPop = commercial;
        state.indPop = state.prevIndPop = industrial;
        state.taxBaseEighths = taxBase;
        state.resValve = state.comValve = state.indValve = 0;
        sim->reconcileLoadedMapState(game->getGameCycleCount());
        state.prevResPop = state.resPop;
        state.prevComPop = state.comPop;
        state.prevIndPop = state.indPop;
    }

    Coord legalLabSite() const {
        const Coord size = getStructureSize(Structure_ResearchLab);
        for (int y = 2; y + size.y < 35; ++y) for (int x = 2; x + size.x < 35; ++x)
            if (currentGameMap->okayToPlaceStructure(x, y, size.x, size.y, false, owner, false,
                                                    Structure_ResearchLab)) return Coord(x, y);
        throw std::runtime_error("No human-legal lab site beside the initial base");
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

    int queuedLabs() const {
        int count = 0;
        for (const auto* structure : structureList) {
            const auto* builder = dynamic_cast<const BuilderBase*>(structure);
            if (!builder || structure->getOwner() != owner) continue;
            for (const auto& offer : builder->getBuildList())
                if (offer.itemID == Structure_ResearchLab) count += offer.num;
        }
        return count;
    }

    const ResearchLab* lab() const {
        for (const auto* structure : structureList)
            if (structure->getOwner() == owner)
                if (const auto* found = dynamic_cast<const ResearchLab*>(structure)) return found;
        return nullptr;
    }

    void build() { bot->build(bot->militaryUnitValue()); }

    void tick() {
        // As in the technology probes, full builds are sampled every 25 cycles;
        // unit orders, payment, placement and House research use normal updates.
        bot->buildTimer = INT_MAX;
        bot->attackTimer = INT_MAX;
        game->negotiatedBudget = 15000;
        game->carryOverTokens = 0;
        game->updateGameState();
        require(!game->finished, "Passive lab fixture ended before collecting evidence");
    }
};

struct Evidence {
    bool queued = false;
    bool paid = false;
    bool placed = false;
    int queuedCycle = -1;
    int placedCycle = -1;
    int researchCycle = -1;
    int completedCycle = -1;
    int researchDuration = 0;

    void observe(const World& world) {
        const int count = world.queuedLabs();
        require(count + world.owner->getNumItems(Structure_ResearchLab) <= 1,
                "QuantBot ordered a duplicate existing or already queued lab");
        if (count > 0 && !queued) {
            queued = true;
            queuedCycle = world.game->getGameCycleCount();
        }
        for (const auto* structure : structureList) {
            const auto* builder = dynamic_cast<const BuilderBase*>(structure);
            if (builder && structure->getOwner() == world.owner
                && builder->getCurrentProducedItem() == Structure_ResearchLab
                && builder->getProductionProgress() > 0) paid = true;
        }
        if (world.owner->getNumItems(Structure_ResearchLab) > 0 && !placed) {
            require(queued && paid && world.owner->getNumBuiltItems(Structure_ResearchLab) == 1,
                    "Measured lab was not selected, paid and placed by the real AI production path");
            placed = true;
            placedCycle = world.game->getGameCycleCount();
        }
        if (world.owner->getTechLevel() >= 2 && completedCycle < 0) {
            completedCycle = world.game->getGameCycleCount();
            require(researchCycle >= 0 && completedCycle - researchCycle == researchDuration,
                    "Normal research completed without its exact real engine duration");
        }
    }

    void build(World& world) {
        const bool active = world.owner->isTechResearchActive();
        const int cost = world.owner->getTechResearchCost();
        const int cash = world.owner->getCredits();
        const FixPoint exactCash = world.owner->storedCredits + world.owner->startingCredits + world.owner->cityCredits;
        const auto* labBefore = world.lab();
        const bool legalBefore = labBefore && world.owner->canStartTechResearch(labBefore->getObjectID());
        const bool placedBefore = placed;
        const bool houseLegalBefore = world.game->getGameInitSettings().getGameOptions().researchLab
            && !world.owner->isNatives() && !active && world.owner->getTechLevel() == 1
            && exactCash >= FixPoint(cost);
        world.build();
        // Legacy build() may place the completed lab in its producer pass and
        // start research later in that same call. Observe placement before
        // judging the research transition, not one full build afterwards.
        observe(world);
        if (!active && world.owner->isTechResearchActive()) {
            const auto* lab = world.lab();
            // canStartTechResearch() now correctly rejects already-active
            // research. For a same-call new lab, check its original House
            // predicates read-only, plus the observed real paid placement and
            // exact accepted lab ID; never rewind or alter research state.
            const bool sameCallLabLegal = !labBefore && !placedBefore && placed
                && placedCycle == int(world.game->getGameCycleCount())
                && houseLegalBefore && world.owner->getTechLevel() == 1
                && lab && lab->getOwner() == world.owner && lab->isActive() && lab->getHealth() > 0;
            require(placed && lab && world.owner->getTechResearchLabID() == lab->getObjectID()
                    && (labBefore ? legalBefore : sameCallLabLegal)
                    && world.owner->getTechResearchTarget() == 2
                    && world.owner->getCredits() == cash - cost
                    && world.owner->storedCredits + world.owner->startingCredits + world.owner->cityCredits
                        == exactCash - FixPoint(cost),
                    "AI research did not use the same lab legality and full payment as a human");
            researchCycle = world.game->getGameCycleCount();
            researchDuration = world.owner->techResearchCycles(2);
            require(cost == 1000 && researchDuration == 7500
                    && world.owner->getTechResearchRemaining() == researchDuration,
                    "Fixture changed the Normal research price or timer");
        }
    }
};

inline std::string progression(const char* mod, const char* playerClass, bool enabled) {
    World world(mod, playerClass, enabled);
    require(world.yard->isAvailableToBuild(Structure_ResearchLab),
            "Real tech-1 yard does not offer the human-legal lab");
    world.legalLabSite();
    auto plan = world.observePlan();
    require(plan.forecastNetIncome > 0 && world.owner->hasPower()
            && world.owner->getCredits() >= 30000 && !world.bot->cityBootstrapNeeded(plan),
            "Solvent lab fixture lacks a genuine funded, powered, bootstrapped economy");
    const bool fleetGap = world.bot->openingFleetIncomplete(plan);
    const bool investmentReady = world.bot->techInvestmentReady(plan);
    if (fleetGap) require(investmentReady == enabled,
                         "Lab progression switch did not isolate the unreachable opening-fleet prerequisite");
    Evidence evidence;
    evidence.build(world);
    if (!enabled && fleetGap) {
        require(!evidence.queued && !world.owner->isTechResearchActive(),
                "Legacy full build did not reproduce the legal solvent opening-fleet lab refusal");
        SDL_Log("RESEARCH_LAB_AI_CASE: mod=%s class=%s switch=off hash=%s initial_tech=1 lab_legal=1 "
                "solvent=1 opening_fleet_incomplete=1 observation=first_full_build legacy_refusal=1",
                mod, playerClass, world.hash.c_str());
        return world.hash;
    }
    const Uint32 end = world.game->getGameCycleCount() + MILLI2CYCLES(600000);
    while (world.game->getGameCycleCount() < end && world.owner->getTechLevel() < 2) {
        world.tick();
        evidence.observe(world);
        if (world.owner->getTechLevel() < 2 && world.game->getGameCycleCount() % 25 == 0)
            evidence.build(world);
    }
    require(evidence.queued && evidence.paid && evidence.placed && evidence.researchCycle >= 0
            && evidence.completedCycle >= 0 && world.owner->getTechLevel() == 2,
            "Full QuantBot failed to queue, pay, place, start and finish its first legal lab research");
    require(world.game->getHouse(HOUSE_ATREIDES)->getTechLevel() == 1,
            "AI research changed another house's technology");
    SDL_Log("RESEARCH_LAB_AI_CASE: mod=%s class=%s switch=%s hash=%s initial_tech=1 lab_legal=1 "
            "solvent=1 opening_fleet_incomplete=%d queue_cycle=%d paid=1 place_cycle=%d "
            "research_cycle=%d complete_cycle=%d research_cycles=%d final_tech=2 duplicates=0",
            mod, playerClass, enabled ? "on" : "off", world.hash.c_str(), int(fleetGap),
            evidence.queuedCycle, evidence.placedCycle, evidence.researchCycle, evidence.completedCycle,
            evidence.researchDuration);
    return world.hash;
}

inline void negative(const char* mod, const char* playerClass, Control control, const char* name) {
    World world(mod, playerClass, true, control);
    if (control == Control::RuleOff)
        require(world.yard && !world.yard->isAvailableToBuild(Structure_ResearchLab),
                "Research-rule-off fixture still has a lab catalogue offer");
    if (control == Control::MissingYard)
        require(!world.yard, "Unavailable-prerequisite fixture still has a Construction Yard supplier");
    if (control == Control::NoCash)
        require(world.owner->getCredits() < world.game->objectData.data[Structure_ResearchLab][HOUSE_HARKONNEN].price,
                "Insufficient-cash fixture can pay for a lab");
    if (control == Control::ExistingLab) require(world.lab(), "Duplicate control is missing its initial lab");
    world.build();
    require(world.queuedLabs() == 0, "Negative legality/funding/existing-lab case ordered a lab");
    if (control != Control::ExistingLab)
        require(!world.owner->isTechResearchActive() && world.owner->getNumItems(Structure_ResearchLab) == 0,
                "Negative lab case started impossible research or created a free lab");
    else require(world.owner->getNumItems(Structure_ResearchLab) == 1,
                 "Existing-lab control changed its one real scenario lab");
    SDL_Log("RESEARCH_LAB_AI_NEGATIVE: mod=%s class=%s case=%s hash=%s queued_labs=0 existing_labs=%d pass=1",
            mod, playerClass, name, world.hash.c_str(), world.owner->getNumItems(Structure_ResearchLab));
}

} // namespace ResearchLabAIProbe

void runResearchLabAIProbe(const char* mod, const char* playerClass) {
    struct RestoreConfig {
        QuantBotConfig saved = getQuantBotConfig();
        ~RestoreConfig() { getQuantBotConfig() = saved; }
    } restore;
    using namespace ResearchLabAIProbe;
    auto pair = [&](const std::string& controller) {
        const auto off = progression(mod, controller.c_str(), false);
        const auto on = progression(mod, controller.c_str(), true);
        require(off != on, "ResearchLabProgression switch is absent from the effective config hash");
    };
    pair(playerClass);
    const std::string base = playerClass;
    require(base.rfind("qBot", 0) == 0, "Lab fixture needs a registered QuantBot difficulty");
    pair("qBotSupport" + base.substr(4));
    // Defend is a real fifth difficulty, not a role or a renamed Hard bot.
    // Once per mod is sufficient; Builder variants above cover every CLI tier.
    if (base == "qBotHard") {
        pair("qBotDefend");
        pair("qBotSupportDefend");
        negative(mod, playerClass, Control::RuleOff, "research_rule_disabled");
        negative(mod, playerClass, Control::MissingYard, "construction_yard_unavailable");
        negative(mod, playerClass, Control::NoCash, "insufficient_cash");
        negative(mod, playerClass, Control::ExistingLab, "existing_lab_no_duplicate");
    }
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=research_lab_ai outcome=passed mod=%s difficulty=%s "
            "full_build=1 human_legality=1 paid_lab=1 normal_research_completion=1 hashed_off_on=1 builder=1",
            mod, playerClass);
}
