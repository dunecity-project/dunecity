// Exact-save power diagnostics only; never linked into the shipped executable.
#include <players/QuantBotBuildPass.h>

namespace PowerContinuation {
constexpr std::array<Uint32, 5> generators{{Structure_WindTrap, Structure_AdvancedWindTrap,
    Structure_AdvancedWindTrapMK2, Structure_AdvancedWindTrapMK3, Structure_NuclearPlant}};

AITelemetry::Record stock(const House* house) {
    AITelemetry::Record result;
    for (Uint32 item : generators)
        result.set(getItemNameByID(item), AITelemetry::Record().set("count", house->getNumItems(item))
            .set("item", item)
            .set("built", house->getNumBuiltItems(item)).set("queued", FactoryContinuation::queued(house, item)));
    return result;
}

AITelemetry::Record roster(const House* house) {
    AITelemetry::Record result;
    for (const auto& player : house->getPlayerList())
        result.set(std::to_string(player->getPlayerID()), AITelemetry::Record()
            .set("name", player->getPlayername()).set("class", player->getPlayerclass()));
    return result;
}

void snapshot(Game& game, const char* phase, Uint32 first) {
    ResearchLabContinuation::snapshot(game, phase);
    SDL_Log("POWER_SAVE_SETUP: phase=%s cycle=%u elapsed=%u version=%u mod=%s revision=%s city=%d technology_installments=%d hash=%s",
        phase, game.getGameCycleCount(), game.getGameCycleCount() - first, game.getLoadedSavegameVersion(),
        game.getGameInitSettings().getModName().c_str(), game.getGameInitSettings().getModRevisionHash().c_str(),
        game.isCitySimEnabled(), game.getGameInitSettings().getGameOptions().technologyInstallments,
        game.getGameInitSettings().getGameOptions().getHash().c_str());
    for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game.getHouse(h))
        SDL_Log("POWER_SAVE_HOUSE: %s", AITelemetry::Record().set("phase", phase).set("house", h)
            .set("cycle", game.getGameCycleCount()).set("elapsed", game.getGameCycleCount() - first)
            .set("credits", house->getCredits()).set("tech", house->getTechLevel())
            .set("players", roster(house)).set("economy", AITelemetry::log().economyTotals(h))
            .set("produced", house->getProducedPower()).set("required", house->getPowerRequirement())
            .set("generators", stock(house)).json().c_str());
}

void audit(Game& game) {
    // Separate scheduled unit: diagnostic planning must not perturb continuation.
    for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game.getHouse(h))
        for (const auto& player : house->getPlayerList()) if (auto* bot = dynamic_cast<QuantBot*>(player.get())) {
            QuantBot::BuildPass pass(bot->militaryUnitValue());
            bot->resetBuildPass(); bot->planBuildInventory(pass); bot->planBuildBudget(pass);
            bot->planCityState(pass); bot->planStrategicReserve(pass); bot->planSpiceEconomy(pass);
            bot->planPowerAndYards(pass); bot->planCityPopulation(pass); bot->planCivicNeedAndTrips(pass);
            bot->planUnitMix(pass); bot->planCapitalForecast(pass); bot->planCityBootstrap(pass);
            bot->planOpeningProduction(pass); bot->planDefenceCoverage(pass); bot->planTechSavings(pass);
            bot->planCapitalCandidates(pass); bot->chooseCapitalPlan(pass); bot->planTechResearch(pass);
            AITelemetry::Record yards;
            for (const auto* structure : structureList) if (structure->getOwner() == house
                    && structure->getItemID() == Structure_ConstructionYard) {
                const auto* yard = static_cast<const BuilderBase*>(structure);
                bot->planningBuilder = yard->getObjectID(); bot->clearPlacementCache(false, true);
                AITelemetry::Record offers;
                for (Uint32 item : generators) {
                    const auto& spec = game.objectData.data[item][h];
                    const Coord extent = getStructureSize(item), at = bot->findPlaceLocation(item);
                    AITelemetry::Record prerequisites;
                    for (Uint32 prerequisite = Structure_FirstID; prerequisite < spec.prerequisiteStructuresSet.size(); ++prerequisite)
                        if (spec.prerequisiteStructuresSet[prerequisite])
                            prerequisites.set(getItemNameByID(prerequisite), house->getNumItems(prerequisite));
                    offers.set(getItemNameByID(item), AITelemetry::Record().set("enabled", spec.enabled)
                        .set("tech", spec.techLevel).set("price", spec.price).set("output", -spec.power)
                        .set("width", extent.x).set("height", extent.y).set("buildtime", spec.buildtime)
                        .set("prerequisites", prerequisites).set("available", bot->campaignAvailableToBuild(yard, item))
                        .set("human_legal_sites", FactoryContinuation::sites(house, item))
                        .set("site_valid", at.isValid()).set("x", at.x).set("y", at.y)
                        .set("placement", bot->placementScoreDetails[item]).set("capital", bot->buildingCapitalCost(pass, item)));
                }
                yards.set(std::to_string(yard->getObjectID()), AITelemetry::Record()
                    .set("upgrade", yard->getCurrentUpgradeLevel()).set("upgrading", yard->isUpgrading())
                    .set("queue_size", yard->getProductionQueueSize()).set("offers", offers));
            }
            bot->planningBuilder = NONE_ID; bot->clearPlacementCache(false, true);
            SDL_Log("POWER_SAVE_AUDIT: %s", AITelemetry::Record().set("house", h).set("player", bot->getPlayerID())
                .set("difficulty", int(bot->difficulty)).set("role", int(bot->role)).set("mode", int(bot->gameMode))
                .set("config_hash", bot->botConfig().getConfigHash()).set("spendable", pass.money)
                .set("queued_cost", pass.queuedProductionCost).set("forecast_net", pass.forecastNetIncome)
                .set("power_reserve", pass.cityPowerReserve).set("zone_growth_headroom", pass.zoneGrowthHeadroom)
                .set("committed_demand", pass.committedPowerDemand).set("nuclear_plan", pass.nuclearPlan)
                .set("nuclear_build_available", pass.nuclearBuildAvailable).set("pending", bot->powerGenerationPending(pass))
                .set("footprints", bot->availableBaseFootprints).set("working_reserve", pass.cityWorkingReserve)
                .set("tech_savings", pass.techSavings).set("yards", yards).json().c_str());
            bot->build(bot->militaryUnitValue());
        }
}
} // namespace PowerContinuation

void runPowerContinuation(Game& game, int minutes) {
    if (const char* overlay = std::getenv("BALANCE_POWER_CONFIG"))
        for (int h = 0; h < NUM_HOUSES; ++h) if (auto* house = game.getHouse(h))
            for (const auto& player : house->getPlayerList()) if (auto* bot = dynamic_cast<QuantBot*>(player.get())) {
                auto config = std::make_shared<QuantBotConfig>(bot->botConfig());
                if (!config->applyOverrides(overlay)) throw std::runtime_error("Invalid power candidate overlay");
                bot->configOverride = config;
            }
    const Uint32 first = game.getGameCycleCount();
    PowerContinuation::snapshot(game, "loaded", first);
    if (std::getenv("BALANCE_POWER_SAVE_AUDIT")) {
        PowerContinuation::audit(game);
    } else {
        const Uint32 end = first + MILLI2CYCLES(minutes * 60000);
        std::array<std::array<int, 10>, NUM_HOUSES> previous{};
        auto observe = [&](bool initial) {
            for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game.getHouse(h)) {
                std::array<int, 10> next{};
                for (size_t i = 0; i < PowerContinuation::generators.size(); ++i) {
                    next[i * 2] = house->getNumItems(PowerContinuation::generators[i]);
                    next[i * 2 + 1] = house->getNumBuiltItems(PowerContinuation::generators[i]);
                }
                if (!initial && previous[h] != next)
                    SDL_Log("POWER_SAVE_EVENT: %s", AITelemetry::Record().set("cycle", game.getGameCycleCount())
                        .set("elapsed", game.getGameCycleCount() - first).set("house", h)
                        .set("credits", house->getCredits()).set("produced", house->getProducedPower())
                        .set("required", house->getPowerRequirement()).set("generators", PowerContinuation::stock(house)).json().c_str());
                previous[h] = next;
            }
        };
        observe(true);
        Uint32 sample = first + MILLI2CYCLES(60000);
        while (!game.finished && game.getGameCycleCount() < end) {
            game.updateGameState(); observe(false);
            if (game.getGameCycleCount() >= sample) { PowerContinuation::snapshot(game, "minute", first); sample += MILLI2CYCLES(60000); }
        }
    }
    PowerContinuation::snapshot(game, "finished", first);
    SDL_Log("POWER_SAVE_RESULT: start=%u cycle=%u elapsed=%u finished=%d", first, game.getGameCycleCount(), game.getGameCycleCount() - first, game.finished);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: level=1 seed=%u attack_percent=100 partner=hard enemy=hard cycle=%u outcome=power_save",
        game.getGameInitSettings().getRandomSeed(), game.getGameCycleCount());
}
