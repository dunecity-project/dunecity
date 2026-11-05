// Diagnostic-only exact-save continuation; never linked into the shipped game.
#include <players/QuantBotBuildPass.h>

namespace ResearchLabContinuation {
void snapshot(Game& game, const char* phase) {
    const auto& rules = game.getGameInitSettings().getGameOptions();
    SDL_Log("RESEARCH_LAB_SAVE_SETUP: phase=%s cycle=%u version=%u mod=%s research_rule=%d city=%d hash=%s",
        phase, game.getGameCycleCount(), game.getLoadedSavegameVersion(),
        game.getGameInitSettings().getModName().c_str(), rules.researchLab, game.isCitySimEnabled(), rules.getHash().c_str());
    for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game.getHouse(h)) {
        AITelemetry::Record row, players, structures, yards;
        for (const auto& info : game.getGameInitSettings().getHouseInfoList()) if (info.houseID == h)
            for (const auto& player : info.playerInfoList) players.set(player.playerName, player.playerClass);
        for (Uint32 item = ItemID_FirstID; item <= ItemID_LastID; ++item)
            if (isStructure(item) && house->getNumItems(item)) structures.set(getItemNameByID(item), house->getNumItems(item));
        for (const auto* structure : structureList) if (structure->getOwner() == house && structure->getItemID() == Structure_ConstructionYard) {
            const auto* yard = static_cast<const BuilderBase*>(structure);
            AITelemetry::Record queue;
            for (const auto& offer : yard->getBuildList()) if (offer.num > 0) queue.set(getItemNameByID(offer.itemID), offer.num);
            yards.set(std::to_string(yard->getObjectID()), AITelemetry::Record().set("available", yard->isAvailableToBuild(Structure_ResearchLab))
                .set("queue", queue).set("upgrade", yard->getCurrentUpgradeLevel()).set("upgrading", yard->isUpgrading()).set("hold", yard->isOnHold()));
        }
        int sites = 0;
        const Coord size = getStructureSize(Structure_ResearchLab);
        for (int y = 0; y + size.y <= currentGameMap->getSizeY(); ++y)
            for (int x = 0; x + size.x <= currentGameMap->getSizeX(); ++x)
                sites += currentGameMap->okayToPlaceStructure(x, y, size.x, size.y, false, house, false, Structure_ResearchLab);
        const auto& spec = game.objectData.data[Structure_ResearchLab][h];
        row.set("phase", phase).set("cycle", game.getGameCycleCount()).set("house", h).set("players", players)
            .set("tech", house->getTechLevel()).set("credits", house->getCredits()).set("power", house->getProducedPower())
            .set("power_required", house->getPowerRequirement()).set("capacity", house->getCapacity())
            .set("lab_enabled", spec.enabled).set("lab_tech", spec.techLevel).set("lab_price", spec.price)
            .set("lab_sites", sites).set("structures", structures).set("yards", yards)
            .set("research_target", house->getTechResearchTarget()).set("research_remaining", house->getTechResearchRemaining());
        if (const auto* sim = game.getCitySimulation()) {
            const auto& state = sim->getHouseState(h);
            row.set("population", state.getTotalPop() * 20).set("annual_tax", state.budget.getLastTaxRevenue())
                .set("annual_police", state.lastPoliceExpense);
        }
        SDL_Log("RESEARCH_LAB_SAVE_HOUSE: %s", row.json().c_str());
    }
}

void audit(Game& game) {
    // Audit is a separate scheduled unit: diagnostic planning cannot alter a
    // measured off/on continuation, which each reload the unchanged save.
    for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game.getHouse(h))
        for (const auto& player : house->getPlayerList()) if (auto* bot = dynamic_cast<QuantBot*>(player.get())) {
            QuantBot::BuildPass pass(0);
            bot->resetBuildPass(); bot->planBuildInventory(pass); bot->planBuildBudget(pass);
            bot->planCityState(pass); bot->planStrategicReserve(pass); bot->planSpiceEconomy(pass);
            bot->planPowerAndYards(pass); bot->planCityPopulation(pass); bot->planCivicNeedAndTrips(pass);
            bot->planUnitMix(pass); bot->planCapitalForecast(pass); bot->planCityBootstrap(pass);
            bot->planOpeningProduction(pass); bot->planDefenceCoverage(pass); bot->planTechSavings(pass);
            bot->planCapitalCandidates(pass); bot->chooseCapitalPlan(pass);
            AITelemetry::Record row;
            row.set("house", h).set("player", bot->getPlayerID()).set("difficulty", int(bot->difficulty))
                .set("role", int(bot->role)).set("config_hash", bot->botConfig().getConfigHash())
                .set("research_policy", bot->researchLabPolicyActive()).set("research_required", bot->researchRequired())
                .set("tech_savings", pass.techSavings).set("spendable", pass.money).set("queued_cost", pass.queuedProductionCost)
                .set("forecast_tax", pass.forecastTax).set("forecast_upkeep", pass.forecastUpkeep)
                .set("forecast_spice", pass.forecastSpice).set("forecast_net", pass.forecastNetIncome)
                .set("economy_ready", bot->economyReadyForCapital(pass)).set("investment_ready", bot->techInvestmentReady(pass))
                .set("opening_fleet", bot->openingFleetIncomplete(pass)).set("opening_workers", bot->openingWorkersNeeded(pass))
                .set("bootstrap", bot->cityBootstrapNeeded(pass)).set("dangerous_crime", pass.dangerousCrimeProperties)
                .set("defending_economy", pass.defendingEconomy).set("air_engaged", pass.airEngaged)
                .set("lab_capital", bot->buildingCapitalCost(pass, Structure_ResearchLab))
                .set("lab_funded", bot->canFundTechInvestment(pass, bot->buildingCapitalCost(pass, Structure_ResearchLab)))
                .set("research_funded", bot->canFundTechInvestment(pass, house->getTechResearchCost()))
                .set("cash_buffer", pass.cashBuffer).set("economy_reserve", pass.economyReserve)
                .set("working_reserve", pass.cityWorkingReserve).set("strategic_reserve", pass.strategicReserveCost)
                .set("civic_reserve", pass.civicReserveCost).set("trooper_support", bot->botConfig().force.trooperSupport)
                .set("city_production_prerequisites", bot->botConfig().force.cityProductionPrerequisites);
            SDL_Log("RESEARCH_LAB_SAVE_AUDIT: %s", row.json().c_str());
        }
}
} // namespace ResearchLabContinuation

void runResearchLabContinuation(Game& game, int minutes) {
    ResearchLabContinuation::snapshot(game, "loaded");
    if (std::getenv("BALANCE_RESEARCH_LAB_AUDIT")) {
        ResearchLabContinuation::audit(game);
        SDL_Log("RESEARCH_LAB_SAVE_RESULT: audit=1 cycle=%u", game.getGameCycleCount());
    } else {
        const Uint32 first = game.getGameCycleCount(), end = first + MILLI2CYCLES(minutes * 60000);
        std::array<int, NUM_HOUSES> labs{}, tech{}, target{};
        for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game.getHouse(h)) {
            labs[h] = house->getNumItems(Structure_ResearchLab); tech[h] = house->getTechLevel(); target[h] = house->getTechResearchTarget();
        }
        Uint32 sample = first + MILLI2CYCLES(60000);
        while (!game.finished && game.getGameCycleCount() < end) {
            game.updateGameState();
            for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game.getHouse(h)) {
                const int nextLabs = house->getNumItems(Structure_ResearchLab), nextTech = house->getTechLevel(), nextTarget = house->getTechResearchTarget();
                if (labs[h] != nextLabs || tech[h] != nextTech || target[h] != nextTarget) {
                    SDL_Log("RESEARCH_LAB_SAVE_EVENT: cycle=%u elapsed=%u house=%d labs=%d tech=%d target=%d remaining=%d credits=%d",
                        game.getGameCycleCount(), game.getGameCycleCount() - first, h, nextLabs, nextTech, nextTarget,
                        house->getTechResearchRemaining(), house->getCredits());
                    labs[h] = nextLabs; tech[h] = nextTech; target[h] = nextTarget;
                }
            }
            if (game.getGameCycleCount() >= sample) { ResearchLabContinuation::snapshot(game, "minute"); sample += MILLI2CYCLES(60000); }
        }
        ResearchLabContinuation::snapshot(game, "finished");
        SDL_Log("RESEARCH_LAB_SAVE_RESULT: audit=0 start=%u cycle=%u elapsed=%u finished=%d", first, game.getGameCycleCount(), game.getGameCycleCount() - first, game.finished);
    }
    SDL_Log("CAMPAIGN_BALANCE_RESULT: level=1 seed=%u attack_percent=100 partner=hard enemy=hard cycle=%u outcome=research_lab_save",
        game.getGameInitSettings().getRandomSeed(), game.getGameCycleCount());
}
