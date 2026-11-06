// Diagnostic-only exact-save continuation; never linked into the shipped game.
#include <players/QuantBotBuildPass.h>
#include <dunecity/CityEffects.h>

namespace ResearchLabContinuation {
void snapshot(Game& game, const char* phase) {
    const auto& rules = game.getGameInitSettings().getGameOptions();
    SDL_Log("RESEARCH_LAB_SAVE_SETUP: phase=%s cycle=%u version=%u mod=%s research_rule=%d pace=%u technology_installments=%d city=%d hash=%s",
        phase, game.getGameCycleCount(), game.getLoadedSavegameVersion(),
        game.getGameInitSettings().getModName().c_str(), rules.researchLab,
        static_cast<unsigned>(rules.researchPace), rules.technologyInstallments,
        game.isCitySimEnabled(), rules.getHash().c_str());
    for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game.getHouse(h)) {
        AITelemetry::Record row, players, structures, yards, zoneSites;
        for (const auto& player : house->getPlayerList())
            players.set(std::to_string(player->getPlayerID()), AITelemetry::Record()
                .set("name", player->getPlayername()).set("class", player->getPlayerclass()));
        bool researchLegal = false;
        for (const auto* structure : structureList)
            if (structure->getOwner() == house && structure->getItemID() == Structure_ResearchLab)
                researchLegal |= house->canStartTechResearch(structure->getObjectID());
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
        // Count human-legal lots only at endpoints; minute samples stay cheap.
        if (std::string_view(phase) != "minute")
            for (Uint32 item : {Structure_ZoneResidential, Structure_ZoneCommercial, Structure_ZoneIndustrial}) {
                int legal = 0;
                const Coord extent = getStructureSize(item);
                for (int y = 0; y + extent.y <= currentGameMap->getSizeY(); ++y)
                    for (int x = 0; x + extent.x <= currentGameMap->getSizeX(); ++x)
                        legal += currentGameMap->okayToPlaceStructure(x, y, extent.x, extent.y, false, house, false, item);
                zoneSites.set(getItemNameByID(item), legal);
            }
        row.set("phase", phase).set("cycle", game.getGameCycleCount()).set("house", h).set("players", players)
            .set("tech", house->getTechLevel()).set("credits", house->getCredits()).set("power", house->getProducedPower())
            .set("power_required", house->getPowerRequirement()).set("capacity", house->getCapacity())
            .set("lab_enabled", spec.enabled).set("lab_tech", spec.techLevel).set("lab_price", spec.price)
            .set("lab_sites", sites).set("structures", structures).set("yards", yards)
            .set("research_target", house->getTechResearchTarget()).set("research_remaining", house->getTechResearchRemaining())
            .set("research_cost", house->getTechResearchCost()).set("research_start_legal", researchLegal)
            .set("research_installment", house->isTechResearchInstallment()).set("research_paused", house->isTechResearchPaused())
            .set("research_total_cost", house->getTechResearchTotalCost()).set("research_total_cycles", house->getTechResearchTotalCycles())
            .set("research_paid_raw", house->getTechResearchPaid().getRawValue());
        row.set("res_zones", house->getNumItems(Structure_ZoneResidential))
            .set("com_zones", house->getNumItems(Structure_ZoneCommercial))
            .set("ind_zones", house->getNumItems(Structure_ZoneIndustrial)).set("zone_legal_sites", zoneSites);
        if (const auto* sim = game.getCitySimulation()) {
            const auto& state = sim->getHouseState(h);
            int zones = 0, crime = 0, dangerous = 0, vacant = 0, unpowered = 0;
            for (const auto* structure : structureList) {
                if (structure->getOwner() != house || !DuneCity::isCityZoneStructure(structure->getItemID())) continue;
                const Coord p = structure->getLocation();
                const auto* tile = currentGameMap->getTile(p.x, p.y);
                const int localCrime = sim->getCrimeRateMap().worldGet(p.x, p.y);
                ++zones; crime += localCrime; dangerous += localCrime >= DuneCity::kCrimeDangerousThreshold;
                vacant += DuneCity::getStructurePopulation(structure, tile->getCityZoneDensity()) == 0;
                unpowered += !static_cast<const ZoneStructure*>(structure)->hasCityPower();
            }
            DuneCity::ValveInputs civics;
            civics.resPop = state.resPop; civics.comPop = state.comPop; civics.indPop = state.indPop;
            civics.hasStadium = house->getNumItems(Structure_Stadium) > 0;
            civics.hasAirport = house->getNumItems(Structure_Airport) > 0;
            civics.hasStarport = house->getNumItems(Structure_StarPort) > 0;
            row.set("population", state.getTotalPop() * 20).set("annual_tax", state.budget.getLastTaxRevenue())
                .set("annual_police", state.lastPoliceExpense)
                .set("res_demand", state.resValve).set("com_demand", state.comValve).set("ind_demand", state.indValve)
                .set("crime_mean", zones ? crime / zones : 0).set("dangerous_zones", dangerous)
                .set("vacant_zones", vacant).set("unpowered_zones", unpowered)
                .set("civic_demand_blocked", state.civicDemandBlocked)
                .set("missing_demand_civics", DuneCity::missingDemandCivics(civics));
        }
        SDL_Log("RESEARCH_LAB_SAVE_HOUSE: %s", row.json().c_str());
    }
}

void audit(Game& game) {
    // Audit is a separate scheduled unit: diagnostic planning cannot alter a
    // measured off/on continuation, which each reload the unchanged save.
    for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game.getHouse(h))
        for (const auto& player : house->getPlayerList()) if (auto* bot = dynamic_cast<QuantBot*>(player.get())) {
            QuantBot::BuildPass pass(bot->militaryUnitValue());
            bot->resetBuildPass(); bot->planBuildInventory(pass); bot->planBuildBudget(pass);
            bot->planCityState(pass); bot->planStrategicReserve(pass); bot->planSpiceEconomy(pass);
            bot->planPowerAndYards(pass); bot->planCityPopulation(pass); bot->planCivicNeedAndTrips(pass);
            bot->planUnitMix(pass); bot->planCapitalForecast(pass); bot->planCityBootstrap(pass);
            bot->planOpeningProduction(pass); bot->planDefenceCoverage(pass); bot->planTechSavings(pass);
            bot->planCapitalCandidates(pass); bot->chooseCapitalPlan(pass);
            AITelemetry::Record row, zones, candidates;
            for (const auto* yard : pass.capitalBuilders) {
                if (yard->getItemID() != Structure_ConstructionYard) continue;
                bot->planningBuilder = yard->getObjectID(); bot->clearPlacementCache(false, true);
                AITelemetry::Record options;
                for (Uint32 item : {Structure_ZoneResidential, Structure_ZoneCommercial, Structure_ZoneIndustrial}) {
                    const Coord site = bot->findPlaceLocation(item);
                    options.set(getItemNameByID(item), AITelemetry::Record()
                        .set("admitted", bot->cityAdmitsStructure(pass, item))
                        .set("available", bot->campaignAvailableToBuild(yard, item))
                        .set("site_valid", site.isValid()).set("x", site.x).set("y", site.y)
                        .set("placement", bot->placementScoreDetails[item]));
                }
                const Uint32 choice = bot->chooseCityZone(pass, yard, false);
                const Uint32 affordable = bot->affordableCityZone(pass, yard, pass.money);
                const Uint32 economy = bot->chooseCityEconomy(pass, yard, false);
                zones.set(std::to_string(yard->getObjectID()), AITelemetry::Record()
                    .set("queue_size", yard->getProductionQueueSize()).set("upgrading", yard->isUpgrading())
                    .set("hold", yard->isOnHold()).set("zone", choice).set("affordable_zone", affordable)
                    .set("economy", economy).set("options", options));
            }
            bot->planningBuilder = NONE_ID; bot->clearPlacementCache(false, true);
            for (size_t i = 0; i < pass.capitalCandidates.size(); ++i) {
                const auto& candidate = pass.capitalCandidates[i];
                candidates.set(std::to_string(i), AITelemetry::Record()
                    .set("builder", candidate.builder).set("item", candidate.item).set("kind", candidate.kind)
                    .set("reason", candidate.reason).set("price", candidate.price)
                    .set("foundation_cost", candidate.foundationCost).set("score", candidate.score));
            }
            row.set("house", h).set("player", bot->getPlayerID()).set("difficulty", int(bot->difficulty))
                .set("role", int(bot->role)).set("config_hash", bot->botConfig().getConfigHash())
                .set("research_policy", bot->researchLabPolicyActive()).set("research_required", bot->researchRequired(pass))
                .set("continuous_research", bot->botConfig().force.continuousResearch)
                .set("research_cost", house->getTechResearchCost())
                .set("research_spending_admitted", bot->spendingAdmits(pass, Structure_ResearchLab, house->getTechResearchCost(), true))
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
                .set("city_production_prerequisites", bot->botConfig().force.cityProductionPrerequisites)
                .set("state", bot->decisionState(pass)).set("zoning", zones).set("capital_candidates", candidates)
                .set("capital_choice", pass.capitalChoice).set("protection_capital", pass.protectionCapital)
                .set("tech_funding_priority", pass.techFundingPriority).set("growth_protected", pass.cityGrowthProtected);
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
        std::array<bool, NUM_HOUSES> paused{};
        for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game.getHouse(h)) {
            labs[h] = house->getNumItems(Structure_ResearchLab); tech[h] = house->getTechLevel(); target[h] = house->getTechResearchTarget();
            paused[h] = house->isTechResearchPaused();
        }
        Uint32 sample = first + MILLI2CYCLES(60000);
        while (!game.finished && game.getGameCycleCount() < end) {
            game.updateGameState();
            for (int h = 0; h < NUM_HOUSES; ++h) if (const auto* house = game.getHouse(h)) {
                const int nextLabs = house->getNumItems(Structure_ResearchLab), nextTech = house->getTechLevel(), nextTarget = house->getTechResearchTarget();
                const bool nextPaused = house->isTechResearchPaused();
                if (labs[h] != nextLabs || tech[h] != nextTech || target[h] != nextTarget || paused[h] != nextPaused) {
                    SDL_Log("RESEARCH_LAB_SAVE_EVENT: cycle=%u elapsed=%u house=%d labs=%d tech=%d target=%d remaining=%d credits=%d installment=%d paused=%d total_cost=%d total_cycles=%d paid_raw=%lld",
                        game.getGameCycleCount(), game.getGameCycleCount() - first, h, nextLabs, nextTech, nextTarget,
                        house->getTechResearchRemaining(), house->getCredits(), house->isTechResearchInstallment(), nextPaused,
                        house->getTechResearchTotalCost(), house->getTechResearchTotalCycles(),
                        static_cast<long long>(house->getTechResearchPaid().getRawValue()));
                    labs[h] = nextLabs; tech[h] = nextTech; target[h] = nextTarget; paused[h] = nextPaused;
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
