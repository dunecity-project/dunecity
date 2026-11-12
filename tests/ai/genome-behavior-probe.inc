// Passive reported-save observations, included only in the private balance probe.
#include "genome-phases.inc"
#include <structures/BuilderBase.h>
#include <structures/ResearchLab.h>
#include <structures/ZoneStructure.h>
#include <dunecity/CityStructurePopulation.h>

namespace GenomeBehavior {
inline constexpr std::array<Uint32, 12> items = {{
    Structure_LightFactory, Structure_HeavyFactory, Structure_IX, Structure_StarPort,
    Structure_ResearchLab, Structure_NuclearPlant, Structure_WindTrap, Structure_RocketTurret,
    Structure_ConstructionYard, Structure_ZoneResidential, Structure_ZoneIndustrial, Structure_ZoneCommercial
}};
inline constexpr std::array<const char*, 12> names = {{
    "light_factory", "heavy_factory", "ix", "starport", "research_lab", "nuclear", "windtrap", "rocket_turret",
    "yard", "residential", "industrial", "commercial"
}};

struct State {
    std::array<int, items.size()> owned{}, built{}, queued{}, available{};
    int tech = 0, target = 0, readyYards = 0;
    bool researchLegal = false, paused = false, alive = false, unpoweredZonesVacate = false;
    bool operator==(const State& other) const {
        return owned == other.owned && built == other.built && queued == other.queued && available == other.available
            && tech == other.tech && target == other.target && researchLegal == other.researchLegal
            && paused == other.paused && alive == other.alive
            && readyYards == other.readyYards && unpoweredZonesVacate == other.unpoweredZonesVacate;
    }
};

// Observe physical queues/completions once per house, not once per AI controller.
// No planner, RNG, command, artificial site or extra resources are invoked here.
std::array<State, NUM_HOUSES> observe(Game& game) {
    std::array<State, NUM_HOUSES> result{};
    for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game.getHouse(h)) {
        auto& state = result[h];
        state.tech = house->getTechLevel(); state.target = house->getTechResearchTarget();
        state.paused = house->isTechResearchPaused(); state.alive = house->isAlive();
        state.unpoweredZonesVacate = game.getGameInitSettings().getGameOptions().unpoweredZonesVacate;
        for (size_t i = 0; i < items.size(); ++i) {
            state.owned[i] = house->getNumItems(items[i]); state.built[i] = house->getNumBuiltItems(items[i]);
        }
    }
    for (const auto* structure : structureList) {
        const auto* house = structure->getOwner();
        const int h = house->getHouseID();
        auto& state = result[h];
        if (structure->getItemID() == Structure_ResearchLab)
            state.researchLegal |= house->canStartTechResearch(structure->getObjectID());
        if (!structure->isABuilder()) continue;
        const auto* builder = static_cast<const BuilderBase*>(structure);
        if (builder->getItemID() == Structure_ConstructionYard && builder->isActive()
            && !builder->isUpgrading() && !builder->isOnHold() && builder->getProductionQueueSize() == 0)
            ++state.readyYards;
        for (size_t i = 0; i < items.size(); ++i) {
            for (const auto& order : builder->getProductionQueue()) if (order.itemID == items[i]) ++state.queued[i];
            if (builder->getItemID() == Structure_ConstructionYard && builder->isAvailableToBuild(items[i])) ++state.available[i];
        }
    }
    return result;
}

int legalSites(const House& house, Uint32 item) {
    int count = 0;
    const Coord extent = getStructureSize(item);
    for (int y = 0; y + extent.y <= currentGameMap->getSizeY(); ++y)
        for (int x = 0; x + extent.x <= currentGameMap->getSizeX(); ++x)
            count += currentGameMap->okayToPlaceStructure(x, y, extent.x, extent.y, false, &house, false, item);
    return count;
}

void emit(Game& game, int h, const State& state, const char* phase, Uint32 first, bool change = false) {
    const auto* house = game.getHouse(h);
    if (!house) return;
    AITelemetry::Record row, controllers;
    int bots = 0, others = 0;
    for (const auto& player : house->getPlayerList()) {
        AITelemetry::Record controller;
        controller.set("name", player->getPlayername()).set("class", player->getPlayerclass());
        if (const auto* bot = dynamic_cast<const QuantBot*>(player.get())) {
            controller.set("difficulty", static_cast<int>(bot->difficulty)).set("config_hash", bot->botConfig().getConfigHash());
            ++bots;
        } else ++others;
        controllers.set(std::to_string(player->getPlayerID()), controller);
    }
    row.set("phase", phase).set("house", h).set("cycle", game.getGameCycleCount())
        .set("elapsed", game.getGameCycleCount() - first).set("alive", state.alive)
        .set("bots", bots).set("other_controllers", others).set("controllers", controllers)
        .set("colour_slot", getHouseVisualHouse(h)).set("colour_rgb", getHouseColorRGB(getHouseVisualHouse(h)))
        .set("credits", house->getCredits()).set("tech", state.tech).set("research_target", state.target)
        .set("research_remaining", house->getTechResearchRemaining()).set("research_start_legal", state.researchLegal)
        .set("research_installment", house->isTechResearchInstallment()).set("research_paused", state.paused)
        .set("research_total_cycles", house->getTechResearchTotalCycles()).set("research_cost", house->getTechResearchCost())
        .set("power", house->getProducedPower()).set("power_required", house->getPowerRequirement());
    row.set("ready_yards", state.readyYards).set("unpowered_zones_vacate", state.unpoweredZonesVacate);
    for (size_t i = 0; i < items.size(); ++i) {
        row.set(names[i], state.owned[i]).set(std::string(names[i]) + "_built", state.built[i])
            .set(std::string(names[i]) + "_queued", state.queued[i]).set(std::string(names[i]) + "_available", state.available[i]);
    }
    row.set("economy", AITelemetry::log().economyTotals(h));
    // The reported LF opportunity is pinned at load; do not repeatedly survey a 384x384 map.
    if (std::string_view(phase) == "loaded" && bots && !others)
        row.set("light_factory_sites", legalSites(*house, Structure_LightFactory));
    const char* selected = std::getenv("BALANCE_GENOME_BEHAVIOR");
    if (selected && std::string_view(selected) == "norocketturrets" && h == 6) {
        // Retain material state at the existing loaded/change/minute/final
        // samples only. In particular, a ready yard is not evidence that its
        // health permits an upgrade; no extra planner or per-tick scan runs.
        AITelemetry::Record yards;
        int radarQueued = 0;
        for (const auto* building : structureList) {
            if (building->getOwner() != house || !building->isABuilder()) continue;
            const auto* builder = static_cast<const BuilderBase*>(building);
            for (const auto& order : builder->getProductionQueue())
                radarQueued += order.itemID == Structure_Radar;
            if (builder->getItemID() != Structure_ConstructionYard) continue;
            AITelemetry::Record queue;
            size_t index = 0;
            for (const auto& order : builder->getProductionQueue())
                queue.set(std::to_string(index++), AITelemetry::Record()
                    .set("item", order.itemID).set("price", order.price));
            const auto at = builder->getLocation();
            yards.set(std::to_string(builder->getObjectID()), AITelemetry::Record()
                .set("x", at.x).set("y", at.y).set("health", builder->getHealth().lround())
                .set("health_raw", builder->getHealth().getRawValue()).set("max_health", builder->getMaxHealth())
                .set("upgrade_level", builder->getCurrentUpgradeLevel()).set("max_upgrade_level", builder->getMaxUpgradeLevel())
                .set("upgrading", builder->isUpgrading()).set("upgrade_cost", builder->getUpgradeCost())
                .set("upgrade_progress_raw", builder->getUpgradeProgress().getRawValue())
                .set("hold", builder->isOnHold()).set("repairing", builder->isRepairing())
                .set("current_item", builder->getCurrentProducedItem())
                .set("production_progress_raw", builder->getProductionProgress().getRawValue()).set("queue", queue));
        }
        row.set("construction_yards", yards).set("radar", house->getNumItems(Structure_Radar))
            .set("radar_built", house->getNumBuiltItems(Structure_Radar)).set("radar_queued", radarQueued);
    }
    if (std::string_view(phase) == "loaded" && selected
        && std::string_view(selected) == "atreides_no_expand" && h == 1) {
        const auto& spec = game.objectData.data[Structure_HeavyFactory][h];
        row.set("heavy_factory_sites", legalSites(*house, Structure_HeavyFactory))
            .set("heavy_factory_price", spec.price).set("heavy_factory_power", spec.power);
    }
    if (std::string_view(phase) == "loaded" && selected
        && std::string_view(selected) == "purple_zero_power" && h == 7) {
        AITelemetry::Record zones;
        for (const auto* building : structureList) {
            const auto* zone = dynamic_cast<const ZoneStructure*>(building);
            if (!zone || zone->getOwner() != house) continue;
            const auto at = zone->getLocation();
            const int level = currentGameMap->getTile(at.x, at.y)->getCityZoneDensity();
            zones.set(std::to_string(zone->getObjectID()), AITelemetry::Record()
                .set("item", zone->getItemID()).set("level", level)
                .set("population", DuneCity::getStructurePopulation(zone, level))
                .set("powered", zone->hasCityPower()));
        }
        row.set("zones", zones);
    }
    if (selected && std::string_view(selected) == "sardaukar_lf" && h == 4) {
        AITelemetry::Record yards;
        for (const auto* building : structureList) {
            if (building->getOwner() != house || building->getItemID() != Structure_ConstructionYard) continue;
            const auto at = building->getLocation();
            yards.set(std::to_string(building->getObjectID()), AITelemetry::Record().set("x", at.x).set("y", at.y));
        }
        row.set("yard_locations", yards);
    }
    if (const auto* sim = game.getCitySimulation()) {
        const auto& city = sim->getHouseState(h);
        row.set("population", city.getTotalPop() * 20).set("residential_demand", city.resValve)
            .set("industrial_demand", city.indValve).set("commercial_demand", city.comValve);
    }
    SDL_Log("%s: %s", change ? "GENOME_BEHAVIOR_EVENT" : "GENOME_BEHAVIOR_HOUSE", row.json().c_str());
}

void snapshot(Game& game, const std::array<State, NUM_HOUSES>& states, const char* phase, Uint32 first) {
    for (int h = 0; h < NUM_HOUSES; ++h) emit(game, h, states[h], phase, first);
}
} // namespace GenomeBehavior

void runGenomeBehavior(Game& game, int minutes) {
    const Uint32 first = game.getGameCycleCount(), end = first + MILLI2CYCLES(minutes * 60000);
    FormationDuel::applyGenomePhase(game);
    const auto& init = game.getGameInitSettings();
    const auto& rules = init.getGameOptions();
    AITelemetry::Record setup;
    setup.set("cycle", first).set("save_version", game.getLoadedSavegameVersion()).set("mod", init.getModName())
        .set("mode", game.isCitySimEnabled() ? "dunecity" : "vanilla").set("format", "saved")
        .set("width", currentGameMap->getSizeX()).set("height", currentGameMap->getSizeY())
        .set("research", rules.researchLab).set("technology_installments", rules.technologyInstallments)
        .set("concrete_required", rules.concreteRequired).set("options_hash", rules.getHash());
    setup.set("unpowered_zones_vacate", rules.unpoweredZonesVacate)
        .set("mod_revision", init.getModRevisionHash()).set("map_revision", init.getMapRevisionHash());
    SDL_Log("GENOME_BEHAVIOR_SETUP: %s", setup.json().c_str());
    if (const char* directory = std::getenv("BALANCE_GENOME_PHASE_DIR")) {
        const std::string path(directory);
        INIFile options(true, "Complete effective reported-save options");
        writeGameOptionsToConfig(options, "Game Options", rules);
        if (!options.saveChangesTo(path + "/effective-options.ini")) throw std::runtime_error("Cannot retain behaviour options");
        std::ofstream(path + "/map.ini", std::ios::binary) << init.getFiledata();
    }
    auto states = GenomeBehavior::observe(game);
    GenomeBehavior::snapshot(game, states, "loaded", first);
    Uint32 next = first + MILLI2CYCLES(60000);
    while (!game.finished && game.getGameCycleCount() < end) {
        game.updateGameState();
        FormationDuel::applyGenomePhase(game);
        const auto changed = GenomeBehavior::observe(game);
        for (int h = 0; h < NUM_HOUSES; ++h) if (!(states[h] == changed[h]))
            GenomeBehavior::emit(game, h, changed[h], "change", first, true);
        states = changed;
        if (game.getGameCycleCount() >= next) {
            GenomeBehavior::snapshot(game, states, "minute", first);
            next += MILLI2CYCLES(60000);
        }
    }
    GenomeBehavior::snapshot(game, states, "finished", first);
    SDL_Log("GENOME_BEHAVIOR_RESULT: start=%u cycle=%u elapsed=%u completed=%d finished=%d telemetry_enabled=%d capture_limited=%d captured_bytes=%llu",
        first, game.getGameCycleCount(), game.getGameCycleCount() - first, int(game.getGameCycleCount() >= end), int(game.finished),
        AITelemetry::log().enabled(), AITelemetry::log().captureLimited, static_cast<unsigned long long>(AITelemetry::log().bytes));
    SDL_Log("CAMPAIGN_BALANCE_RESULT: level=1 seed=%u attack_percent=100 partner=saved enemy=saved cycle=%u outcome=genome_behavior",
        init.getRandomSeed(), game.getGameCycleCount());
}
