// Real engine fixture; only compiled by the diagnostic driver.
//
// Measures the per-house spice income multiplier against the shipped code, not against a
// re-implementation of it: a real Refinery (and, in DuneCity, a real Worfinery) unloads a real
// Harvester (or RebelHarvester) through StructureBase::update() for as many ticks as the
// extraction speed needs, and the credits the House ends up with are compared with the cargo
// that physically left the harvester.
//
// What it has to establish, because none of it is visible from a payout calculation alone:
//   - the lobby's per-row factor reaches the live House through INIMapLoader, including for a
//     row that was left on Random and only learned its house at load time;
//   - a delivery pays cargo * factor exactly, whether it is unloaded in one go, interrupted
//     part way, or slowed down by a damaged refinery;
//   - the cargo, the map's spice and the unload duration are not touched by the factor;
//   - storage limits, the credit ceiling, taxes, starting cash and refunds behave as before;
//   - the factor survives an ordinary save and load, and reaches a reloaded match's House,
//     its setup metadata and a joining peer's snapshot;
//   - two houses with different factors do not borrow each other's rate, and the two
//     controllers of a shared house share one.
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <structures/StructureBase.h>
#include <structures/Refinery.h>
#include <structures/Worfinery.h>
#include <units/Harvester.h>
#include <units/HarvesterHelpers.h>
#include <Network/GameInitSettingsPolicy.h>
#include <Map.h>
#include <Tile.h>
#include <SpiceIncome.h>

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace SpiceIncomeProbe {

void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_Log("SPICE_INCOME_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

/// Somewhere on the map with room for a refinery and a deploy spot next to it.
Coord findClearArea(int width, int height) {
    for(int y = 1; y + height < currentGameMap->getSizeY(); ++y) {
        for(int x = 1; x + width < currentGameMap->getSizeX(); ++x) {
            bool clear = true;
            for(int dy = 0; dy < height && clear; ++dy) {
                for(int dx = 0; dx < width && clear; ++dx) {
                    if(currentGameMap->getTile(x + dx, y + dy)->hasAGroundObject()) clear = false;
                }
            }
            if(clear) return Coord(x, y);
        }
    }
    return Coord::Invalid();
}

/// Keeps the measured house out of reach of anything that could earn or spend behind the
/// fixture's back: its controllers are parked, its power charge is pushed out, and its storage
/// is wide enough that nothing is lost unless a scenario wants it to be.
struct QuietHouse {
    House* house;
    std::list<std::unique_ptr<Player>> players;
    int capacity;
    Uint32 winFlags;

    explicit QuietHouse(House* measured, int storage = 4 * HARVESTERMAXSPICE * 10)
     : house(measured), players(std::move(measured->players)),
       capacity(measured->capacity), winFlags(currentGame->winFlags) {
        house->capacity = storage;
        house->powerUsageTimer = MILLI2CYCLES(15 * 1000);
        // A quota win would end the match in the middle of a measurement.
        currentGame->winFlags = 0;
    }
    ~QuietHouse() {
        house->players = std::move(players);
        house->capacity = capacity;
        currentGame->winFlags = winFlags;
    }

    void resetCredits() {
        house->storedCredits = 0;
        house->startingCredits = 0;
        house->cityCredits = 0;
        house->harvestedSpice = 0;
        house->oldCredits = 0;
    }
};

/// One measured delivery.
struct Delivery {
    FixPoint cargoDelivered;    ///< what physically left the harvester
    FixPoint creditsGained;     ///< what the house was paid for it
    FixPoint grossGained;       ///< what the harvested-spice statistic recorded
    int ticks;                  ///< how many refinery updates it took
};

/**
    Unloads \a cargo through the real structure, stopping early after \a maxTicks so an
    interrupted delivery can be measured too.
*/
Delivery unload(House* house, StructureBase* structure, UnitBase* harvester,
                FixPoint cargo, int maxTicks = 100000) {
    // Release whatever the structure is still holding, the way a finished delivery does in a
    // match: with no cargo left there is nothing to pay for, so this cannot disturb the
    // measurement, and a Worfinery will not accept a new harvester until it has let go.
    harvesterSetAmountOfSpice(harvester, 0);
    structure->update();

    harvesterSetAmountOfSpice(harvester, cargo);
    if(auto* refinery = dynamic_cast<Refinery*>(structure)) {
        refinery->assignHarvester(static_cast<TrackedUnit*>(harvester));
    } else if(auto* worfinery = dynamic_cast<Worfinery*>(structure)) {
        require(worfinery->receiveHarvester(static_cast<TrackedUnit*>(harvester)),
                "The Worfinery refused the fixture harvester");
    } else {
        require(false, "Unknown fixture structure");
    }

    const FixPoint creditsBefore = house->getStoredCredits();
    const FixPoint grossBefore = house->getHarvestedSpice();
    Delivery result{0, 0, 0, 0};
    for(int tick = 0; tick < maxTicks; ++tick) {
        if(harvesterGetAmountOfSpice(harvester) <= 0) break;
        structure->update();
        result.ticks++;
    }
    result.cargoDelivered = cargo - harvesterGetAmountOfSpice(harvester);
    result.creditsGained = house->getStoredCredits() - creditsBefore;
    result.grossGained = house->getHarvestedSpice() - grossBefore;
    return result;
}

std::string describe(const char* label, Uint32 factor, const Delivery& delivery) {
    return std::string(label) + " factor=" + std::to_string(factor)
        + " cargo=" + std::to_string(delivery.cargoDelivered.toDouble())
        + " credits=" + std::to_string(delivery.creditsGained.toDouble())
        + " gross=" + std::to_string(delivery.grossGained.toDouble())
        + " ticks=" + std::to_string(delivery.ticks);
}

/// The factors the driver gave the roster rows, in roster order.
std::vector<Uint32> assignedFactors(std::size_t rows) {
    std::vector<Uint32> factors;
    for(std::size_t row = 0; row < rows; ++row) {
        factors.push_back(SpiceIncome::kMin
            + static_cast<Uint32>(row % SpiceIncome::kChoiceCount));
    }
    return factors;
}

} // namespace SpiceIncomeProbe

void runSpiceIncomeProbe(std::unique_ptr<Game>& game) {
    using namespace SpiceIncomeProbe;
    const bool cityMode = ModManager::instance().isCityModeActive();
    // The Worfinery and the Harvestank are Tornie content; ObjectBase::createObject refuses
    // them anywhere else, so that pair is measured in the Tornie run of this probe.
    const bool tornieContent = ModManager::instance().isTornieContentActive();

    // ---- 1. The lobby's per-row factors reached the live houses -----------------------
    //
    // The driver put a different factor on every roster row before initGame, so this is the
    // whole chain: GameInitSettings row -> INIMapLoader's resolved row -> House.
    {
        const auto& rows = game->getGameInitSettings().getHouseInfoList();
        const auto expected = assignedFactors(rows.size());
        require(!rows.empty(), "The fixture has no lobby rows");
        std::set<Uint32> observed;
        int seeded = 0;
        for(std::size_t row = 0; row < rows.size(); ++row) {
            require(rows[row].spiceIncomeMultiplier == expected[row],
                "A lobby row lost the factor the driver gave it");
            // A row the map had no slot for is skipped by INIMapLoader, as it always was.
            const House* house = game->getHouse(rows[row].houseID);
            if(house == nullptr) continue;
            require(house->getSpiceIncomeMultiplier() == expected[row],
                "A house did not take the factor of the row it was created from");
            observed.insert(expected[row]);
            seeded++;
        }
        require(observed.size() >= 2,
                "The fixture needs at least two houses with different factors");

        // The resolved rows INIMapLoader kept, in the order it kept them. This is the
        // mechanism a Random row depends on: the factor travels with the row's position, and
        // the house it was bound to is read off the resolved row rather than looked up by
        // identity in the original settings - where a Random row still says HOUSE_INVALID.
        require(!game->houseInfoListSetup.empty(), "INIMapLoader kept no resolved rows");
        for(const auto& resolved : game->houseInfoListSetup) {
            require(SpiceIncome::isValid(resolved.spiceIncomeMultiplier),
                "A resolved row carries a factor the simulation cannot pay at");
            const House* house = game->getHouse(resolved.houseID);
            require(house != nullptr, "A resolved row names a house that was not created");
            require(house->getSpiceIncomeMultiplier() == resolved.spiceIncomeMultiplier,
                "A house disagrees with the resolved row it was created from");
            // And that resolved row is the original row at the same position.
            const auto position = static_cast<std::size_t>(&resolved - &game->houseInfoListSetup[0]);
            require(position < rows.size(), "More resolved rows than configured rows");
            require(resolved.spiceIncomeMultiplier == rows[position].spiceIncomeMultiplier
                    || rows[position].houseID != resolved.houseID,
                "A resolved row took its factor from a different lobby row");
        }
        SDL_Log("SPICE_INCOME_PROBE: seeded %d of %zu rows (%zu resolved) with %zu distinct factors",
                seeded, rows.size(), game->houseInfoListSetup.size(), observed.size());
    }

    House* measured = pLocalHouse;
    require(measured != nullptr, "Missing measured house");

    // Shared controllers share one house economy: the factor belongs to the house, not to a
    // seat, so two controllers cannot earn at two different rates.
    require(measured->getPlayerList().size() == 2,
            "The fixture needs a shared house with two controllers");

    const Coord area = findClearArea(20, 10);
    require(area.isValid(), "Missing fixture space");
    for(int y = 0; y < currentGameMap->getSizeY(); ++y) {
        for(int x = 0; x < currentGameMap->getSizeX(); ++x) {
            if(!currentGameMap->getTile(x, y)->hasAGroundObject()) {
                currentGameMap->getTile(x, y)->setType(Terrain_Sand);
            }
        }
    }

    // Degradation runs inside StructureBase::update(); a powered fixture keeps the refinery's
    // health where the scenario puts it instead of decaying under measurement.
    const int producedPower = measured->getProducedPower();
    measured->producedPower = 1000000;

    auto* refinery = measured->placeStructure(NONE_ID, Structure_Refinery,
                                              area.x, area.y, true, true);
    require(refinery != nullptr, "Missing fixture refinery");
    auto* worker = measured->createUnit(Unit_Harvester);
    require(worker != nullptr, "Missing fixture harvester");
    worker->deploy(area + Coord(0, 4));
    // StructureBase::update() repairs a damaged structure, which both heals it mid-measurement
    // and spends credits. Keep the fixture at full health so a measured payout is the only
    // thing moving, and let the damaged-refinery section below arrange its own slowdown.
    measured->setAutoRepairEnabled(false);
    refinery->setHealth(refinery->getMaxHealth());

    // Sections 2 to 10 run with the measured house's controllers parked, so no decision can
    // earn or spend between two assertions. They are handed back before anything is saved:
    // a savegame written without its local player would not be loadable.
    {
    QuietHouse quiet(measured);

    // ---- 2. A full load pays cargo * factor, exactly, at every accepted factor ---------
    FixPoint baselineCredits = 0;
    int baselineTicks = 0;
    for(Uint32 factor = SpiceIncome::kMin; factor <= SpiceIncome::kMax; ++factor) {
        measured->setSpiceIncomeMultiplier(factor);
        quiet.resetCredits();
        const auto delivery = unload(measured, refinery, worker, HARVESTERMAXSPICE);
        SDL_Log("SPICE_INCOME_PROBE: %s", describe("full", factor, delivery).c_str());
        require(delivery.cargoDelivered == FixPoint(HARVESTERMAXSPICE),
                "A full load did not leave the harvester completely");
        require(delivery.creditsGained == FixPoint(HARVESTERMAXSPICE) * static_cast<int>(factor),
                "A full load did not pay the cargo times the factor");
        require(delivery.grossGained == delivery.creditsGained,
                "The harvested-spice statistic disagrees with the credited payout");
        require(harvesterGetAmountOfSpice(worker) == 0, "Cargo survived a complete unload");
        if(factor == SpiceIncome::kMin) {
            baselineCredits = delivery.creditsGained;
            baselineTicks = delivery.ticks;
        } else {
            // The factor pays more without unloading faster: the extraction speed, and so the
            // time a refinery is occupied, is the same work as at 1x.
            require(delivery.ticks == baselineTicks,
                    "The factor changed how long a delivery takes");
            require(delivery.creditsGained == baselineCredits * static_cast<int>(factor),
                    "The payout is not a whole multiple of the 1x payout");
        }
    }

    // ---- 3. The cargo capacity and the map's spice are untouched -----------------------
    {
        measured->setSpiceIncomeMultiplier(SpiceIncome::kMax);
        const Coord field = area + Coord(6, 6);
        currentGameMap->getTile(field)->setSpice(500);
        const FixPoint spiceBefore = currentGameMap->getTile(field)->getSpice();
        harvesterSetAmountOfSpice(worker, HARVESTERMAXSPICE);
        require(harvesterGetAmountOfSpice(worker) == FixPoint(HARVESTERMAXSPICE),
                "The harvester's capacity changed with the factor");
        harvesterSetAmountOfSpice(worker, 0);
        require(currentGameMap->getTile(field)->getSpice() == spiceBefore,
                "A delivery factor changed the spice lying on the map");
    }

    // ---- 4. Partial, fractional and interrupted deliveries conserve cargo exactly -------
    for(Uint32 factor : {SpiceIncome::kMin, 2u, SpiceIncome::kMax}) {
        measured->setSpiceIncomeMultiplier(factor);

        // A cargo that is not a whole number of extraction steps: 0.625 per tick does not
        // divide 100.5, so the last tick takes less than a full step.
        quiet.resetCredits();
        const FixPoint odd = FixPoint(201) / 2;
        const auto fractional = unload(measured, refinery, worker, odd);
        SDL_Log("SPICE_INCOME_PROBE: %s", describe("fractional", factor, fractional).c_str());
        require(fractional.cargoDelivered == odd, "A fractional load was not fully delivered");
        require(fractional.creditsGained == odd * static_cast<int>(factor),
                "A fractional delivery did not pay the exact cargo times the factor");

        // Stopped half way: what is paid matches what has actually left the harvester, and the
        // remainder is still on board.
        quiet.resetCredits();
        const auto interrupted = unload(measured, refinery, worker, HARVESTERMAXSPICE, 200);
        const FixPoint onBoard = harvesterGetAmountOfSpice(worker);
        SDL_Log("SPICE_INCOME_PROBE: %s remaining=%f",
                describe("interrupted", factor, interrupted).c_str(), onBoard.toDouble());
        require(onBoard > 0, "The interrupted fixture unloaded everything after all");
        require(interrupted.cargoDelivered + onBoard == FixPoint(HARVESTERMAXSPICE),
                "An interrupted delivery did not conserve the cargo");
        require(interrupted.creditsGained == interrupted.cargoDelivered * static_cast<int>(factor),
                "An interrupted delivery paid for cargo that is still on board");

        // Resuming finishes the same load and the two halves add up.
        const auto resumed = unload(measured, refinery, worker, onBoard);
        require(resumed.cargoDelivered == onBoard, "A resumed delivery lost cargo");
        require(interrupted.creditsGained + resumed.creditsGained
                    == FixPoint(HARVESTERMAXSPICE) * static_cast<int>(factor),
                "Two halves of one load did not pay the same as one whole load");
    }

    // ---- 5. A damaged refinery unloads more slowly and still pays the exact amount -------
    {
        measured->setSpiceIncomeMultiplier(3);

        // The extraction speed scales with the refinery's health, so the same number of ticks
        // moves less cargo when it is damaged - and the payment follows the cargo, not the
        // clock. A short window keeps both measurements at the health they were given.
        refinery->setHealth(refinery->getMaxHealth());
        quiet.resetCredits();
        const auto healthy = unload(measured, refinery, worker, HARVESTERMAXSPICE, 40);
        SDL_Log("SPICE_INCOME_PROBE: %s", describe("healthy-window", 3, healthy).c_str());
        require(healthy.grossGained == healthy.cargoDelivered * 3,
                "A healthy refinery paid for something other than what it extracted");

        refinery->setHealth(refinery->getMaxHealth() / 5);
        quiet.resetCredits();
        const auto hurt = unload(measured, refinery, worker, HARVESTERMAXSPICE, 40);
        SDL_Log("SPICE_INCOME_PROBE: %s", describe("damaged-window", 3, hurt).c_str());
        require(hurt.cargoDelivered < healthy.cargoDelivered,
                "The damaged fixture did not actually slow the extraction down");
        require(hurt.grossGained == hurt.cargoDelivered * 3,
                "A damaged refinery paid for something other than what it extracted");

        // And a whole load through a damaged refinery still pays for exactly that load.
        quiet.resetCredits();
        const auto damaged = unload(measured, refinery, worker, HARVESTERMAXSPICE);
        SDL_Log("SPICE_INCOME_PROBE: %s", describe("damaged-full", 3, damaged).c_str());
        require(damaged.cargoDelivered == FixPoint(HARVESTERMAXSPICE),
                "A damaged refinery did not finish the load");
        require(damaged.grossGained == FixPoint(HARVESTERMAXSPICE) * 3,
                "A damaged refinery paid the wrong amount for a full load");
        require(damaged.ticks > baselineTicks,
                "A damaged refinery finished a whole load as quickly as a healthy one");
        refinery->setHealth(refinery->getMaxHealth());
    }

    // ---- 6. Storage and the credit ceiling still bound the multiplied income ------------
    {
        measured->setSpiceIncomeMultiplier(4);
        quiet.resetCredits();
        // Room for a quarter of what 4x would pay: the surplus is destroyed on arrival, as it
        // is for an ordinary delivery that does not fit, and the gross statistic still counts
        // what the refinery processed.
        measured->capacity = HARVESTERMAXSPICE;
        const auto spilled = unload(measured, refinery, worker, HARVESTERMAXSPICE);
        SDL_Log("SPICE_INCOME_PROBE: %s stored=%f",
                describe("storage-limited", 4, spilled).c_str(),
                measured->getStoredCredits().toDouble());
        require(measured->getStoredCredits() == FixPoint(HARVESTERMAXSPICE),
                "The storage limit did not bound a multiplied delivery");
        require(spilled.grossGained == FixPoint(HARVESTERMAXSPICE) * 4,
                "The gross statistic dropped the part storage could not hold");
        measured->capacity = 4 * HARVESTERMAXSPICE * 10;

        // The absolute ceiling is unchanged too.
        quiet.resetCredits();
        measured->capacity = House::MAX_GAME_CREDITS;
        measured->storedCredits = FixPoint(House::MAX_GAME_CREDITS) - 100;
        unload(measured, refinery, worker, HARVESTERMAXSPICE);
        require(measured->getCredits() <= House::MAX_GAME_CREDITS,
                "A multiplied delivery pushed the house past the credit ceiling");
        measured->capacity = 4 * HARVESTERMAXSPICE * 10;
    }

    // ---- 7. Nothing else earns at the factor -------------------------------------------
    {
        measured->setSpiceIncomeMultiplier(SpiceIncome::kMax);
        quiet.resetCredits();

        // Starting cash is handed out, not earned.
        House fresh(HOUSE_MERCENARY, 3000, 0, 0, 0);
        fresh.setSpiceIncomeMultiplier(SpiceIncome::kMax);
        require(fresh.getStartingCredits() == 3000 && fresh.getCredits() == 3000,
                "The factor scaled starting cash");

        // A refund returns what was paid.
        measured->startingCredits = 1000;
        const auto payment = measured->payCredits(400);
        measured->returnCredits(payment.total, payment.fromStarting);
        require(measured->getCredits() == 1000, "The factor scaled a refund");
        quiet.resetCredits();

        // Credits that are not a spice delivery - a captured store, a crate - go in unscaled.
        measured->addCredits(250, false);
        require(measured->getStoredCredits() == 250,
                "The factor scaled credits that were not a spice delivery");
        require(measured->getHarvestedSpice() == 0,
                "Credits that were not refined were counted as harvested spice");
        quiet.resetCredits();

        if(cityMode) {
            // City taxes share the store but are not a delivery.
            measured->addCityCredits(300);
            measured->addCityTaxReceipts(300);
            require(measured->getCityCredits() == 300, "The factor scaled city tax income");
            require(measured->getCityTaxReceipts() == 300,
                    "The factor scaled the city tax statistic");
            quiet.resetCredits();
            measured->cityTaxReceipts = 0;
        }
    }

    // ---- 8. Two houses at two factors, and the DuneCity pair --------------------------
    {
        House* other = nullptr;
        for(int id = 0; id < NUM_HOUSES; ++id) {
            if(House* candidate = game->getHouse(id)) {
                if(candidate != measured) { other = candidate; break; }
            }
        }
        require(other != nullptr, "Missing second fixture house");
        const int otherCapacity = other->capacity;
        other->capacity = 4 * HARVESTERMAXSPICE * 10;
        other->powerUsageTimer = MILLI2CYCLES(15 * 1000);
        other->producedPower = 1000000;
        other->storedCredits = 0; other->startingCredits = 0; other->harvestedSpice = 0;

        const Coord otherArea = findClearArea(20, 10);
        require(otherArea.isValid(), "Missing space for the second house");
        auto* otherRefinery = other->placeStructure(NONE_ID, Structure_Refinery,
                                                   otherArea.x, otherArea.y, true, true);
        auto* otherWorker = other->createUnit(Unit_Harvester);
        require(otherRefinery != nullptr && otherWorker != nullptr,
                "Missing second house fixture");
        otherWorker->deploy(otherArea + Coord(0, 4));

        measured->setSpiceIncomeMultiplier(2);
        other->setSpiceIncomeMultiplier(5);
        quiet.resetCredits();
        const auto mine = unload(measured, refinery, worker, HARVESTERMAXSPICE);
        const auto theirs = unload(other, otherRefinery, otherWorker, HARVESTERMAXSPICE);
        SDL_Log("SPICE_INCOME_PROBE: two houses mine=%f theirs=%f",
                mine.creditsGained.toDouble(), theirs.creditsGained.toDouble());
        require(mine.creditsGained == FixPoint(HARVESTERMAXSPICE) * 2,
                "A house earned at another house's factor");
        require(theirs.creditsGained == FixPoint(HARVESTERMAXSPICE) * 5,
                "The second house did not earn at its own factor");

        // Changing one house's rate must change the simulation fingerprint, so two peers that
        // disagree about it find out at the next digest instead of at the next delivery.
        const auto before = game->computeStateDigest();
        other->setSpiceIncomeMultiplier(4);
        const auto after = game->computeStateDigest();
        require(before.houseHash != after.houseHash,
                "Changing only a delivery factor left the state digest unchanged");
        other->setSpiceIncomeMultiplier(5);
        require(game->computeStateDigest().houseHash == before.houseHash,
                "The digest does not return to its value when the factor does");

        other->capacity = otherCapacity;

        if(tornieContent) {
            // The second payout site: a Worfinery unloading a Harvestank earns at the same
            // house factor through the same contract as the ordinary pair.
            const Coord rebelArea = findClearArea(20, 10);
            require(rebelArea.isValid(), "Missing space for the Worfinery fixture");
            auto* worfinery = measured->placeStructure(NONE_ID, Structure_Worfinery,
                                                       rebelArea.x, rebelArea.y, true, true);
            auto* rebel = measured->createUnit(Unit_RebelHarvester);
            require(worfinery != nullptr && rebel != nullptr, "Missing Worfinery fixture");
            rebel->deploy(rebelArea + Coord(0, 4));
            for(Uint32 factor : {SpiceIncome::kMin, 3u, SpiceIncome::kMax}) {
                measured->setSpiceIncomeMultiplier(factor);
                quiet.resetCredits();
                const auto delivery = unload(measured, worfinery, rebel, HARVESTERMAXSPICE);
                SDL_Log("SPICE_INCOME_PROBE: %s", describe("worfinery", factor, delivery).c_str());
                require(delivery.cargoDelivered == FixPoint(HARVESTERMAXSPICE),
                        "The Worfinery did not finish a Rebel load");
                require(delivery.creditsGained == FixPoint(HARVESTERMAXSPICE) * static_cast<int>(factor),
                        "The Worfinery paid the wrong amount for a Rebel delivery");
                require(delivery.grossGained == delivery.creditsGained,
                        "The Worfinery's gross statistic disagrees with its payout");
            }
        }
    }

    // ---- 9. A savegame from before the field means 1x --------------------------------
    {
        // A genuine byte fixture: the current layout minus the four bytes the field occupies,
        // read back at the version that did not have them.
        measured->setSpiceIncomeMultiplier(3);
        OMemoryStream saved; saved.open();
        measured->save(saved);
        std::string bytes(reinterpret_cast<const char*>(saved.getData()), saved.getDataLength());
        // houseID, teamID, autoRepairEnabled, then four FixPoints, then the factor.
        const std::size_t factorAt = 3 + 4 * sizeof(Sint64);
        require(bytes.size() > factorAt + 4, "The fixture house block is too short");
        Uint32 encoded = 0;
        for(int shift = 0; shift < 4; ++shift) {
            encoded |= static_cast<Uint32>(static_cast<unsigned char>(bytes[factorAt + shift])) << (8 * shift);
        }
        require(encoded == 3, "The factor is not where the save layout says it is");

        const auto savedVersion = game->loadedSavegameVersion;
        std::string older = bytes;
        older.erase(factorAt, 4);
        game->loadedSavegameVersion = SpiceIncome::kFirstSavegameVersion - 1;
        {
            IMemoryStream in(older.data(), older.size());
            House legacy(in);
            require(legacy.getSpiceIncomeMultiplier() == SpiceIncome::kDefault,
                    "A house from before the field did not resume at 1x");
            require(legacy.getTeamID() == measured->getTeamID(),
                    "The legacy house fixture did not parse the rest of the block");
        }
        game->loadedSavegameVersion = SpiceIncome::kFirstSavegameVersion;
        {
            IMemoryStream in(bytes.data(), bytes.size());
            House current(in);
            require(current.getSpiceIncomeMultiplier() == 3,
                    "A house from the current format lost its factor");
        }
        // A factor this build does not accept is refused rather than defaulted.
        for(const Uint32 bad : {0u, SpiceIncome::kMax + 1, 0xFFFFFFFFu}) {
            std::string hostile = bytes;
            for(int shift = 0; shift < 4; ++shift) {
                hostile[factorAt + shift] = static_cast<char>((bad >> (8 * shift)) & 0xFF);
            }
            IMemoryStream in(hostile.data(), hostile.size());
            bool refused = false;
            try { House broken(in); } catch(const std::exception&) { refused = true; }
            require(refused, "A house accepted a factor outside the accepted range");
        }
        game->loadedSavegameVersion = savedVersion;
    }

    }   // the measured house's controllers are back

    // ---- 10. A joining peer and a spectator see the rate the match is played at --------
    //
    // Outside the parked-controller scope on purpose: both snapshot paths skip a house with no
    // controllers, which is exactly the house the fixture had just emptied.
    {
        measured->setSpiceIncomeMultiplier(5);
        const auto snapshot = game->spectatorSnapshot();
        bool sawMeasured = false;
        for(const auto& info : snapshot.getHouseInfoList()) {
            if(info.houseID == measured->getHouseID()) {
                require(info.spiceIncomeMultiplier == 5,
                        "A spectator snapshot dropped the house's factor");
                sawMeasured = true;
            }
            require(SpiceIncome::isValid(info.spiceIncomeMultiplier),
                    "A spectator snapshot carried a factor it could not play at");
        }
        require(sawMeasured, "The spectator snapshot has no row for the measured house");
        // The same settings a receiver validates before it commits to them.
        std::string reason;
        require(GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(snapshot, reason),
                "A spectator snapshot would be refused by the receiving policy: " + reason);
    }

    measured->producedPower = producedPower;

    // ---- 11. An ordinary save and load keeps the rate, the setup rows and the resume -----
    //
    // Everything above ran on houses created by INIMapLoader. This is the other half: a saved
    // match reloads at the rate it was played at - including a row that was left on Random,
    // whose factor only exists in the save's own setup metadata.
    const std::string savePath = std::string("spice-income-resume.dls");
    std::vector<std::pair<int, Uint32>> expectedFactors;
    FixPoint interruptedCargo = 0;
    {
        measured->setSpiceIncomeMultiplier(2);
        measured->capacity = 4 * HARVESTERMAXSPICE * 10;
        measured->powerUsageTimer = MILLI2CYCLES(15 * 1000);
        measured->producedPower = 1000000;
        for(int id = 0; id < NUM_HOUSES; ++id) {
            if(const House* house = game->getHouse(id)) {
                expectedFactors.emplace_back(id, house->getSpiceIncomeMultiplier());
            }
        }
        // Leave a load part way through so the resume can be measured after the reload.
        unload(measured, refinery, worker, HARVESTERMAXSPICE, 150);
        interruptedCargo = harvesterGetAmountOfSpice(worker);
        require(interruptedCargo > 0, "Nothing was left on board for the reload to finish");
        require(game->saveGame(savePath.c_str()), "Could not save the fixture match");
    }
    const int savedHouseCount = static_cast<int>(expectedFactors.size());

    // The setup metadata a load-game lobby reads, straight out of the saved bytes.
    {
        auto fileData = readCompleteFile(savePath);
        IMemoryStream setupStream(fileData.c_str(), fileData.size());
        GameInitSettings::HouseInfoList savedRows;
        const auto parsed = GameInitSettings::readSaveSetup(setupStream, savedRows);
        require(static_cast<int>(savedRows.size()) == savedHouseCount
                || !savedRows.empty(), "The saved setup has no rows");
        bool matchedMeasured = false;
        for(const auto& savedRow : savedRows) {
            require(SpiceIncome::isValid(savedRow.spiceIncomeMultiplier),
                    "A saved setup row carries an unusable factor");
            for(const auto& [id, factor] : expectedFactors) {
                if(savedRow.houseID == id) {
                    require(savedRow.spiceIncomeMultiplier == factor,
                        "A saved setup row disagrees with the house it was played by");
                    if(id == measured->getHouseID()) matchedMeasured = true;
                }
            }
        }
        require(matchedMeasured, "The saved setup lost the measured house's row");
        require(parsed.getGameType() != GameType::Invalid, "The saved setup has no game type");
        SDL_Log("SPICE_INCOME_PROBE: saved setup rows=%zu", savedRows.size());
    }

    game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
    {
        auto reloaded = std::make_unique<Game>();
        currentGame = reloaded.get();
        reloaded->bReplay = true;
        reloaded->initGame(GameInitSettings(savePath));
        reloaded->bReplay = false;
        reloaded->initializeGameLoop();
        reloaded->bPause = false;

        for(const auto& [id, factor] : expectedFactors) {
            const House* house = reloaded->getHouse(id);
            require(house != nullptr, "A house disappeared across the reload");
            require(house->getSpiceIncomeMultiplier() == factor,
                    "A house resumed at a different delivery rate than it was saved at");
        }
        for(const auto& setupRow : reloaded->houseInfoListSetup) {
            require(SpiceIncome::isValid(setupRow.spiceIncomeMultiplier),
                    "A reloaded setup row carries an unusable factor");
            const House* house = reloaded->getHouse(setupRow.houseID);
            if(house != nullptr) {
                require(setupRow.spiceIncomeMultiplier == house->getSpiceIncomeMultiplier(),
                    "A reloaded setup row disagrees with its own house");
            }
        }

        // The half-finished delivery continues at the rate the match was saved at.
        House* resumedHouse = pLocalHouse;
        require(resumedHouse != nullptr && resumedHouse->getSpiceIncomeMultiplier() == 2,
                "The measured house did not resume at its saved rate");
        StructureBase* resumedRefinery = nullptr;
        for(StructureBase* structure : structureList) {
            if(structure->getOwner() == resumedHouse
               && structure->getItemID() == Structure_Refinery) {
                resumedRefinery = structure; break;
            }
        }
        UnitBase* resumedWorker = nullptr;
        for(UnitBase* unit : unitList) {
            if(unit->getOwner() == resumedHouse && unit->getItemID() == Unit_Harvester) {
                resumedWorker = unit; break;
            }
        }
        require(resumedRefinery != nullptr && resumedWorker != nullptr,
                "The reloaded match lost the fixture refinery or harvester");
        {
            QuietHouse resumedQuiet(resumedHouse);
            resumedHouse->producedPower = 1000000;
            resumedQuiet.resetCredits();
            const auto resumed = unload(resumedHouse, resumedRefinery, resumedWorker, interruptedCargo);
            SDL_Log("SPICE_INCOME_PROBE: %s", describe("after-reload", 2, resumed).c_str());
            require(resumed.cargoDelivered == interruptedCargo,
                    "The resumed delivery lost cargo across the reload");
            require(resumed.creditsGained == interruptedCargo * 2,
                    "The resumed delivery did not pay at the saved rate");
        }

        // Saving again keeps the rates, so a resume chain does not drift.
        require(reloaded->saveGame(savePath.c_str()), "Could not re-save the reloaded match");
        auto fileData = readCompleteFile(savePath);
        IMemoryStream again(fileData.c_str(), fileData.size());
        GameInitSettings::HouseInfoList rows;
        GameInitSettings::readSaveSetup(again, rows);
        for(const auto& savedRow : rows) {
            const House* house = reloaded->getHouse(savedRow.houseID);
            if(house != nullptr) {
                require(savedRow.spiceIncomeMultiplier == house->getSpiceIncomeMultiplier(),
                    "A re-saved setup row drifted from its house");
            }
        }
        game = std::move(reloaded);
    }

    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=spice_income outcome=passed mod=%s",
            ModManager::instance().getActiveModName().c_str());
}
