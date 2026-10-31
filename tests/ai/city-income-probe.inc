// Included only by the diagnostic main TU, never by the shipped game.
//
// Measures what a controlled set of newly zoned R/I/C lots actually pays into
// the city treasury. The city fixture uses the real engine: real placement
// (House::placeStructure), the real budget/growth path
// (DuneCity::CitySimulation::advancePhase) and the real power upkeep charge
// (House::update). No formula output is mocked or substituted.
//
// Controlled-ness: the measured house keeps only the fixture structures (its
// scenario base and all of its units, harvesters included, are destroyed before
// the measurement), and the loop advances only the city simulation and the
// house budget, so no unit/AI/combat/production update can contribute credits.
#include <dunecity/CityConstants.h>
#include <dunecity/CityEffects.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/CityStructurePopulation.h>
#include <dunecity/ZonePower.h>
#include <structures/StructureBase.h>
#include <structures/ZoneStructure.h>
#include <Map.h>
#include <Tile.h>

#include <string>
#include <vector>

namespace {

int cityIncomeZoneItem(char code) {
    switch(code) {
        case 'R': return Structure_ZoneResidential;
        case 'C': return Structure_ZoneCommercial;
        case 'I': return Structure_ZoneIndustrial;
        default: return NONE_ID;
    }
}

const char* cityIncomeZoneCode(int itemID) {
    switch(itemID) {
        case Structure_ZoneResidential: return "R";
        case Structure_ZoneCommercial: return "C";
        case Structure_ZoneIndustrial: return "I";
        default: return "?";
    }
}

int cityIncomeEnvInt(const char* name, int fallback) {
    const char* value = std::getenv(name);
    return value ? std::atoi(value) : fallback;
}

} // namespace

void runCityIncomeProbe(Game& game) {
    auto require = [](bool ok, const char* why) {
        if(!ok) { SDL_Log("CITY_INCOME_FAIL: %s", why); throw std::runtime_error(why); }
    };

    const std::string mix = std::getenv("CITY_INCOME_MIX") ? std::getenv("CITY_INCOME_MIX") : "RICRIC";
    const int minutes = cityIncomeEnvInt("CITY_INCOME_MINUTES", 30);
    const int policeStations = cityIncomeEnvInt("CITY_INCOME_POLICE", 0);
    const int taxOverride = cityIncomeEnvInt("CITY_INCOME_TAX", -1);
    const int fixtureCash = cityIncomeEnvInt("CITY_INCOME_CASH", 10000);
    const std::string label = std::getenv("CITY_INCOME_LABEL") ? std::getenv("CITY_INCOME_LABEL") : "fixture";
    require(!mix.empty() && mix.size() <= 20, "Zone mix must hold 1-20 lots");
    require(minutes >= 1 && minutes <= 120, "Use 1-120 simulated minutes");

    // The city simulation is constructed on the first real game update, so take
    // one ordinary update before the world is reduced to the fixture.
    if(!game.getCitySimulation()) game.updateGameState();
    auto* sim = game.getCitySimulation();
    require(sim && sim->isInitialized(), "City simulation is not active (needs the dunecity mod)");

    House* house = pLocalHouse ? pLocalHouse : game.getHouse(HOUSE_HARKONNEN);
    require(house != nullptr, "Missing measured house");
    const int houseID = house->getHouseID();

    // --- Strip the measured house back to nothing: no base, no harvesters. ---
    // Destroying a building or unit can spawn surviving infantry, so sweep
    // repeatedly until the house owns nothing at all.
    int removedStructures = 0, removedUnits = 0, sweeps = 0;
    for(; sweeps < 64; ++sweeps) {
        std::vector<ObjectBase*> doomed;
        for(auto* structure : structureList)
            if(structure->getOwner() == house) doomed.push_back(structure);
        for(auto* unit : unitList)
            if(unit->getOwner() == house) doomed.push_back(unit);
        if(doomed.empty()) break;
        for(auto* object : doomed) {
            if(object->isAStructure()) ++removedStructures; else ++removedUnits;
            object->destroy();
        }
    }
    SDL_Log("CITY_INCOME_CLEARED: sweeps=%d structures=%d units=%d remaining_units=%d remaining_structures=%d",
            sweeps, removedStructures, removedUnits, house->getNumUnits(), house->getNumStructures());
    require(house->getNumUnits() == 0, "Measured house still owns units");
    require(house->getNumStructures() == 0, "Measured house still owns structures");

    // --- Find a clear fixture plot and normalise it to owned rock. ---
    const int kPlotWidth = std::max(14, int((mix.size() + 1) / 2) * 3 + 2);
    constexpr int kPlotHeight = 14;
    Coord plot = Coord::Invalid();
    for(int y = 2; y + kPlotHeight + 2 < currentGameMap->getSizeY() && plot.isInvalid(); ++y)
        for(int x = 2; x + kPlotWidth + 2 < currentGameMap->getSizeX() && plot.isInvalid(); ++x) {
            bool clear = true;
            for(int dy = -1; dy <= kPlotHeight && clear; ++dy)
                for(int dx = -1; dx <= kPlotWidth && clear; ++dx) {
                    const auto* tile = currentGameMap->getTile(x + dx, y + dy);
                    if(!tile || tile->hasAnObject() || tile->hasCityZone() || tile->isRoad()) clear = false;
                }
            if(clear) plot = Coord(x, y);
        }
    require(plot.isValid(), "No clear city fixture plot on this map");
    for(int dy = 0; dy < kPlotHeight; ++dy)
        for(int dx = 0; dx < kPlotWidth; ++dx) {
            auto* tile = currentGameMap->getTile(plot.x + dx, plot.y + dy);
            tile->setType(Terrain_Rock);
            tile->setRoad(false);
            tile->setOwner(houseID);
        }

    // Fixture placement uses the real execution path with bForcePlacing, the
    // same flag scenario/editor placement uses: the measured house has no base
    // left, so the ordinary build-range check has no anchor. Terrain and
    // occupancy protections still apply.
    // --- Roads: one connected spine every lot touches (growth past level 1
    // needs real road connectivity, not just adjacency). ---
    const int roadRow = plot.y + 3;
    int roadTiles = 0;
    for(int dx = 0; dx < kPlotWidth; ++dx) {
        house->placeStructure(NONE_ID, Structure_Road, plot.x + dx, roadRow, false, true);
        if(currentGameMap->getTile(plot.x + dx, roadRow)->isRoad()) ++roadTiles;
    }
    require(roadTiles == kPlotWidth, "Road spine could not be placed");

    // --- Zones: 2x2 lots on both sides of the spine, in the requested order. ---
    struct FixtureLot { StructureBase* structure; int itemID; Coord pos; };
    std::vector<FixtureLot> lots;
    for(size_t index = 0; index < mix.size(); ++index) {
        const int itemID = cityIncomeZoneItem(mix[index]);
        require(itemID != NONE_ID, "Zone mix accepts only R, C and I");
        const bool above = index < (mix.size() + 1) / 2;
        const int slot = static_cast<int>(above ? index : index - (mix.size() + 1) / 2);
        const Coord pos(plot.x + 1 + slot * 3, above ? roadRow - 2 : roadRow + 1);
        auto* zone = house->placeStructure(NONE_ID, itemID, pos.x, pos.y, false, true);
        require(zone != nullptr, "Newly zoned lot failed real placement");
        lots.push_back({zone, itemID, pos});
    }

    // --- Power: windtraps well clear of the lots, plus optional services. ---
    // Enough windtraps that the fixture stays powered once every lot reaches
    // density 3, so a large fixture is not silently measured power-starved.
    // Windtraps only add production, never powerRequirement, and House::update
    // charges powerRequirement/32, so this cannot inflate the metered cost.
    int fullyGrownPower = 0;
    for(const auto& lot : lots) fullyGrownPower += DuneCity::getZonePower(lot.itemID, 3);
    const int windtrapSlots = std::max(4, (kPlotWidth - 3) / 3 + 1);
    int windtraps = 0;
    for(int slot = 0; slot < windtrapSlots && house->getProducedPower() < fullyGrownPower; ++slot)
        if(house->placeStructure(NONE_ID, Structure_WindTrap, plot.x + 1 + slot * 3, plot.y + 10, false, true))
            ++windtraps;
    while(windtraps < 4 && windtraps < windtrapSlots) {
        if(!house->placeStructure(NONE_ID, Structure_WindTrap, plot.x + 1 + windtraps * 3, plot.y + 10, false, true)) break;
        ++windtraps;
    }
    SDL_Log("CITY_INCOME_POWER: windtraps=%d slots=%d produced=%d fully_grown_requirement=%d",
            windtraps, windtrapSlots, house->getProducedPower(), fullyGrownPower);
    int police = 0;
    for(int slot = 0; slot < policeStations; ++slot)
        if(house->placeStructure(NONE_ID, Structure_PoliceStation, plot.x + 1 + slot * 3, plot.y + 7, false, true))
            ++police;
    require(police == policeStations, "Requested police stations could not be placed");
    require(house->getProducedPower() >= house->getPowerRequirement(), "Fixture starts power-starved");

    if(taxOverride >= 0) sim->setCityTax(static_cast<int16_t>(taxOverride));
    sim->setPoliceFundingPercent(houseID, 100);

    // Fixture cash is an explicit injection: recorded, then excluded from every
    // recurring-income figure below.
    house->storedCredits = 0;
    house->startingCredits = FixPoint(fixtureCash);
    house->cityCredits = 0;
    game.finished = false;
    // Keep registered player objects alive, but suppress House::update callbacks.
    auto savedPlayers = std::move(house->players);
    struct RestorePlayers { House* house; decltype(savedPlayers)& players;
        ~RestorePlayers() { house->players = std::move(players); }
    } restore{house, savedPlayers};
    FixPoint measuredPowerPaid = 0, measuredPolicePaid = 0;

    // Distance to the nearest structure we do not own tells us whether foreign
    // bases could leak into the shared land-value/supply neighbourhood.
    int nearestForeign = -1;
    for(const auto* structure : structureList) {
        if(structure->getOwner() == house) continue;
        const auto at = structure->getLocation();
        const int distance = std::max(std::abs(at.x - plot.x), std::abs(at.y - plot.y));
        if(nearestForeign < 0 || distance < nearestForeign) nearestForeign = distance;
    }

    const Uint32 startCycle = game.getGameCycleCount();
    const auto startReceipts = house->getCityTaxReceipts();
    const auto startCityCredits = house->getCityCredits();

    const int startCredits = house->getCredits();
    const int structuresAtStart = house->getNumStructures();

    SDL_Log("CITY_INCOME_SETUP: label=%s mix=%s zones=%zu roads=%d windtraps=%d police=%d "
            "plot=%d,%d tax=%d police_funding=%d start_cycle=%u fixture_cash=%d "
            "start_credits=%d start_receipts=%.3f removed_structures=%d removed_units=%d "
            "nearest_foreign_structure=%d produced_power=%d required_power=%d "
            "cycles_per_city_year=%u cycles_per_city_day=%u budget_ticks_per_year=%d",
            label.c_str(), mix.c_str(), lots.size(), roadTiles, windtraps, police,
            plot.x, plot.y, sim->getCityTax(), sim->getPoliceFundingPercent(houseID),
            startCycle, fixtureCash, startCredits, startReceipts.toDouble(),
            removedStructures, removedUnits, nearestForeign,
            house->getProducedPower(), house->getPowerRequirement(),
            DuneCity::cyclesPerCityYear(), DuneCity::cyclesPerCityDay(), DuneCity::cyclesPerCityYear());

    auto logSnapshot = [&](const char* kind, Uint32 cycle) {
        const auto& state = sim->getHouseState(houseID);
        int densities[4] = {0, 0, 0, 0};
        int zonePopulation = 0;
        std::string lotDetail;
        for(const auto& lot : lots) {
            const auto* tile = currentGameMap->getTile(lot.pos.x, lot.pos.y);
            const int density = tile ? tile->getCityZoneDensity() : 0;
            const int population = DuneCity::getStructurePopulation(lot.structure, density);
            densities[std::min(3, density)]++;
            zonePopulation += population;
            lotDetail += fmt::sprintf("%s%s@%d,%d:d%d:p%d", lotDetail.empty() ? "" : " ",
                cityIncomeZoneCode(lot.itemID), lot.pos.x, lot.pos.y, density, population);
        }
        const auto receipts = house->getCityTaxReceipts();
        const auto cityCredits = house->getCityCredits();
        const auto otherCredits = house->getStoredCredits() + house->getStartingCredits();
        SDL_Log("CITY_INCOME_ROW: label=%s kind=%s cycle=%u minute=%.3f city_year=%d city_day=%d "
                "tax=%d tax_base_eighths=%d avg_land_value=%d res_pop=%d com_pop=%d ind_pop=%d "
                "zone_pop=%d density0=%d density1=%d density2=%d density3=%d "
                "annual_revenue_now=%d police_nominal=%d police_last_paid=%d "
                "gross_receipts=%.4f city_credits=%.4f other_credits=%.4f credits=%d power_paid=%.4f police_paid=%.4f "
                "produced_power=%d required_power=%d structures=%d units=%d lots=[%s]",
                label.c_str(), kind, cycle,
                double(cycle - startCycle) / double(DuneCity::kCyclesPerMinute),
                sim->getCityYear(), sim->getCityDay(), sim->getCityTax(),
                state.taxBaseEighths, state.avgLandValue, state.resPop, state.comPop, state.indPop,
                zonePopulation, densities[0], densities[1], densities[2], densities[3],
                state.budget.getLastTaxRevenue(), state.nominalPoliceCost, state.lastPoliceExpense,
                receipts.toDouble(), cityCredits.toDouble(), otherCredits.toDouble(), house->getCredits(),
                measuredPowerPaid.toDouble(), measuredPolicePaid.toDouble(), house->getProducedPower(), house->getPowerRequirement(),
                house->getNumStructures(), house->getNumUnits(), lotDetail.c_str());
    };

    logSnapshot("start", startCycle);

    // --- Controlled cycle loop -------------------------------------------
    // One iteration == one 16 ms game cycle: the same increment the real game
    // loop uses, so kCyclesPerMinute (3750) cycles == one simulated minute.
    // Only the city simulation and the owning house's budget are advanced; no
    // world/unit/AI/combat update runs, so nothing else can move credits.
    const Uint32 totalCycles = static_cast<Uint32>(minutes) * DuneCity::kCyclesPerMinute;
    for(Uint32 step = 1; step <= totalCycles; ++step) {
        const Uint32 cycle = startCycle + step;
        game.gameCycleCount = cycle; // Test-only access, provided by -fno-access-control.
        const auto beforeHouse = house->storedCredits + house->startingCredits + house->cityCredits;
        house->update(); // Player callbacks are suppressed; only real power upkeep can spend.
        const auto afterHouse = house->storedCredits + house->startingCredits + house->cityCredits;
        measuredPowerPaid += beforeHouse - afterHouse;
        const auto beforeTax = house->getCityTaxReceipts();
        const auto beforeCity = house->getCityCredits();
        sim->advancePhase(cycle);
        measuredPolicePaid += (house->getCityTaxReceipts() - beforeTax) - (house->getCityCredits() - beforeCity);
        if(step % DuneCity::kCyclesPerMinute == 0) logSnapshot("minute", cycle);
    }

    const Uint32 endCycle = game.getGameCycleCount();
    const auto grossDelta = house->getCityTaxReceipts() - startReceipts;
    const auto cityDelta = house->getCityCredits() - startCityCredits;

    const auto policePaid = measuredPolicePaid;
    const auto powerPaid = measuredPowerPaid;
    const int netCredits = house->getCredits() - startCredits;

    logSnapshot("end", endCycle);
    SDL_Log("CITY_INCOME_SUMMARY: label=%s mix=%s zones=%zu minutes=%d cycles=%u "
            "start_cycle=%u end_cycle=%u tax=%d gross=%.4f police_paid=%.4f power_paid=%.4f "
            "net_city_credits=%.4f net_credits=%d gross_per_minute=%.4f net_per_minute=%.4f "
            "start_credits=%d end_credits=%d fixture_cash=%d structures=%d units=%d "
            "reconciled=%d",
            label.c_str(), mix.c_str(), lots.size(), minutes, endCycle - startCycle,
            startCycle, endCycle, sim->getCityTax(), grossDelta.toDouble(), policePaid.toDouble(),
            powerPaid.toDouble(), cityDelta.toDouble(), netCredits,
            grossDelta.toDouble() * DuneCity::kCyclesPerMinute / double(endCycle - startCycle),
            double(netCredits) * DuneCity::kCyclesPerMinute / double(endCycle - startCycle),
            startCredits, house->getCredits(), fixtureCash,
            house->getNumStructures(), house->getNumUnits(),
            int(std::abs((grossDelta - policePaid - powerPaid).toDouble() - double(netCredits)) <= 1.0));

    require(std::abs((grossDelta - policePaid - powerPaid).toDouble() - double(netCredits)) <= 1.0, "Income accounting does not reconcile");
    require(house->getNumUnits() == 0, "Something produced units during the measurement");
    require(house->getNumStructures() == structuresAtStart, "Fixture structure count changed");
    require(!game.finished, "Game ended before the measurement window closed");
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=city_income label=%s outcome=measured", label.c_str());
}
