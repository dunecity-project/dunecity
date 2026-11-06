// Exact-save diagnostics only; never linked into the shipped executable.
#include <players/QuantBotBuildPass.h>
#include <string_view>

namespace FactoryContinuation {
int queued(const House* house, Uint32 item) {
    int count = 0;
    for (const auto* structure : structureList)
        if (structure->getOwner() == house && structure->isABuilder())
            for (const auto& order : static_cast<const BuilderBase*>(structure)->getBuildList())
                if (order.itemID == item) count += order.num;
    return count;
}

AITelemetry::Record stock(const House* house) {
    int army = 0, value = 0;
    for (const auto* unit : unitList)
        if (unit->getOwner() == house && QuantBotBuildPolicy::militaryItem(unit->getItemID())) {
            ++army;
            value += currentGame->objectData.data[unit->getItemID()][unit->getProductionHouseID()].price;
        }
    return AITelemetry::Record().set("lf", house->getNumItems(Structure_LightFactory))
        .set("lf_queued", queued(house, Structure_LightFactory)).set("lf_built", house->getNumBuiltItems(Structure_LightFactory))
        .set("hf", house->getNumItems(Structure_HeavyFactory)).set("hf_queued", queued(house, Structure_HeavyFactory))
        .set("hf_built", house->getNumBuiltItems(Structure_HeavyFactory)).set("army", army).set("army_value", value);
}

int sites(const House* house, Uint32 item) {
    int count = 0;
    const Coord size = getStructureSize(item);
    for (int y = 0; y + size.y <= currentGameMap->getSizeY(); ++y)
        for (int x = 0; x + size.x <= currentGameMap->getSizeX(); ++x)
            count += currentGameMap->okayToPlaceStructure(x, y, size.x, size.y, false, house, false, item);
    return count;
}

void snapshot(Game& game, const char* phase, Uint32 first) {
    for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game.getHouse(h)) {
        auto row = stock(house);
        row.set("phase", phase).set("house", h).set("cycle", game.getGameCycleCount())
            .set("elapsed", game.getGameCycleCount() - first).set("credits", house->getCredits()).set("tech", house->getTechLevel());
        if (std::string_view(phase) == "loaded") row.set("lf_sites", sites(house, Structure_LightFactory)).set("hf_sites", sites(house, Structure_HeavyFactory));
        SDL_Log("FACTORY_SAVE_HOUSE: %s", row.json().c_str());
    }
    // Reuse the full roster, structures, queues, power and city-tax snapshot.
    ResearchLabContinuation::snapshot(game, phase);
}

void audit(Game& game) {
    // Separate scheduled unit: diagnostics must not perturb either continuation.
    for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game.getHouse(h))
        for (const auto& player : house->getPlayerList()) if (auto* bot = dynamic_cast<QuantBot*>(player.get())) {
            QuantBot::BuildPass pass(bot->militaryUnitValue());
            bot->resetBuildPass(); bot->planBuildInventory(pass); bot->planBuildBudget(pass);
            bot->planCityState(pass); bot->planStrategicReserve(pass); bot->planSpiceEconomy(pass);
            bot->planPowerAndYards(pass); bot->planCityPopulation(pass); bot->planCivicNeedAndTrips(pass);
            bot->planUnitMix(pass); bot->planCapitalForecast(pass); bot->planCityBootstrap(pass);
            bot->planOpeningProduction(pass); bot->planDefenceCoverage(pass); bot->planTechSavings(pass);
            bot->planCapitalCandidates(pass); bot->chooseCapitalPlan(pass); bot->planTechResearch(pass);
            AITelemetry::Record row, yards;
            for (const auto* structure : structureList) if (structure->getOwner() == house && structure->getItemID() == Structure_ConstructionYard) {
                const auto* yard = static_cast<const BuilderBase*>(structure);
                bot->planningBuilder = yard->getObjectID(); bot->clearPlacementCache(false, true);
                const Coord lf = bot->findPlaceLocation(Structure_LightFactory), hf = bot->findPlaceLocation(Structure_HeavyFactory);
                const auto& spec = game.objectData.data[Structure_LightFactory][h];
                yards.set(std::to_string(yard->getObjectID()), AITelemetry::Record()
                    .set("lf_available", bot->campaignAvailableToBuild(yard, Structure_LightFactory))
                    .set("lf_offered", yard->isAvailableToBuild(Structure_LightFactory))
                    .set("hf_available", bot->campaignAvailableToBuild(yard, Structure_HeavyFactory))
                    .set("lf_enabled", spec.enabled).set("lf_tech", spec.techLevel).set("lf_price", spec.price)
                    .set("lf_x", lf.x).set("lf_y", lf.y).set("hf_x", hf.x).set("hf_y", hf.y)
                    .set("lf_cost", bot->buildingCapitalCost(pass, Structure_LightFactory))
                    .set("lf_spending", bot->spendingAdmits(pass, Structure_LightFactory, bot->buildingCapitalCost(pass, Structure_LightFactory))));
            }
            row.set("house", h).set("player", bot->getPlayerID()).set("difficulty", int(bot->difficulty))
                .set("role", int(bot->role)).set("mode", int(bot->gameMode)).set("config_hash", bot->botConfig().getConfigHash())
                .set("spendable", pass.money).set("queued_cost", pass.queuedProductionCost)
                .set("forecast_tax", pass.forecastTax).set("forecast_upkeep", pass.forecastUpkeep)
                .set("forecast_spice", pass.forecastSpice).set("forecast_net", pass.forecastNetIncome)
                .set("opening_fleet", bot->openingFleetIncomplete(pass)).set("opening_workers", bot->openingWorkersNeeded(pass))
                .set("bootstrap", bot->cityBootstrapNeeded(pass)).set("dangerous_crime", pass.dangerousCrimeProperties)
                .set("funded_factory_opening", pass.fundedFactoryOpening).set("first_factory_capital", pass.firstFactoryCapital)
                .set("cash_buffer", pass.cashBuffer).set("economy_reserve", pass.economyReserve)
                .set("working_reserve", pass.cityWorkingReserve).set("strategic_reserve", pass.strategicReserveCost)
                .set("civic_reserve", pass.civicReserveCost).set("capital_choice", pass.capitalDecision).set("yards", yards);
            SDL_Log("FACTORY_SAVE_AUDIT: %s", row.json().c_str());
            bot->build(bot->militaryUnitValue());
        }
}
} // namespace FactoryContinuation

void runFactoryContinuation(Game& game, int minutes) {
    const Uint32 first = game.getGameCycleCount();
    FactoryContinuation::snapshot(game, "loaded", first);
    if (std::getenv("BALANCE_FACTORY_SAVE_AUDIT")) {
        FactoryContinuation::audit(game);
    } else {
        const Uint32 end = first + MILLI2CYCLES(minutes * 60000);
        std::array<std::array<int, 4>, NUM_HOUSES> previous{};
        auto observe = [&](bool initial) {
            for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game.getHouse(h)) {
                const std::array<int, 4> next{{house->getNumItems(Structure_LightFactory), FactoryContinuation::queued(house, Structure_LightFactory),
                    house->getNumItems(Structure_HeavyFactory), FactoryContinuation::queued(house, Structure_HeavyFactory)}};
                if (!initial && previous[h] != next) {
                    auto row = FactoryContinuation::stock(house);
                    row.set("cycle", game.getGameCycleCount()).set("elapsed", game.getGameCycleCount() - first)
                        .set("house", h).set("credits", house->getCredits()).set("tech", house->getTechLevel());
                    SDL_Log("FACTORY_SAVE_EVENT: %s", row.json().c_str());
                }
                previous[h] = next;
            }
        };
        observe(true);
        Uint32 sample = first + MILLI2CYCLES(60000);
        while (!game.finished && game.getGameCycleCount() < end) {
            game.updateGameState(); observe(false);
            if (game.getGameCycleCount() >= sample) { FactoryContinuation::snapshot(game, "minute", first); sample += MILLI2CYCLES(60000); }
        }
    }
    FactoryContinuation::snapshot(game, "finished", first);
    SDL_Log("FACTORY_SAVE_RESULT: start=%u cycle=%u elapsed=%u finished=%d", first, game.getGameCycleCount(), game.getGameCycleCount() - first, game.finished);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: level=1 seed=%u attack_percent=100 partner=brutal enemy=brutal cycle=%u outcome=factory_save",
        game.getGameInitSettings().getRandomSeed(), game.getGameCycleCount());
}
