// Headless integration fixture; linked with the production game objects.
//
// Transport containment, against the real engine: a unit that is not on the map belongs to one
// carrier or one structure bay, and to nothing else. Two things are checked here. First the
// handoff itself: a badly damaged passenger stays the passenger even though the repair order
// issued during its own pickup cancels the booking and can promise it to a second aircraft.
// Second the recovery of units that are already hidden inside nothing, the shape old saves
// carry, without touching units that are legitimately inside something.
//
// Every phase drives production updates on production units and structures placed on a
// production map, and reads the state back off the objects themselves.
#include <Game.h>
#include <Command.h>
#include <players/HumanPlayer.h>
#include <House.h>
#include <Map.h>
#include <SpatialGrid.h>
#include <sand.h>
#include <mod/ModManager.h>
#include <Network/GameStateDigest.h>
#include <misc/IMemoryStream.h>
#include <functional>
#include <structures/Refinery.h>
#include <structures/RepairYard.h>
#include <structures/StructureBase.h>
#include <units/Carryall.h>
#include <units/GroundUnit.h>
#include <units/Harvester.h>
#include <units/UnitBase.h>
#include <algorithm>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

int gContainmentChecks = 0;
std::string gBoundarySave;
Uint32 gBoundaryCycle = 0;
Uint32 gBoundaryOrphan = NONE_ID;
Uint32 gBoundaryPassenger = NONE_ID;
Uint32 gBoundaryCarrier = NONE_ID;

// A stable checksum of the saved bytes for the log: compared across runs and across worker
// counts, so it must not depend on anything but the bytes themselves.
Uint64 saveChecksum(const std::string& bytes) {
    Uint64 hash = 1469598103934665603ull;
    for(const char byte : bytes) {
        hash = (hash ^ static_cast<unsigned char>(byte)) * 1099511628211ull;
    }
    return hash;
}

void containmentRequire(bool ok, const char* why) {
    if(!ok) {
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_FAIL: %s", why);
        throw std::runtime_error(why);
    }
    ++gContainmentChecks;
}

bool isInSpatialGrid(Game* game, const UnitBase* unit) {
    SpatialGrid* grid = game->getSpatialGrid();
    if(grid == nullptr) {
        return true;  // No grid configured in this build; nothing to be inconsistent with.
    }
    std::vector<SpatialGridEntry> entries;
    grid->collectEntries(grid->clampToCell(unit->getLocation()), entries);
    return std::any_of(entries.begin(), entries.end(),
                       [unit](const SpatialGridEntry& entry) { return entry.object == unit; });
}

}  // namespace

int runTransportContainmentProbe() {
    const std::string mod = std::getenv("CONTAINMENT_PROBE_MOD");
    containmentRequire(ModManager::instance().setActiveMod(mod), "Missing containment fixture mod");
    gBoundarySave = std::string(std::getenv("CONTAINMENT_PROBE_OUT")) + "/boundary-" + mod + ".dls";
    settings.general.playerName = "Containment tester";
    // Carryall transport has to be available to the human house, and cargo must be mortal: a
    // destroyed carrier is one of the cases under test.
    settings.gameOptions.manualCarryallDrops = true;
    settings.gameOptions.immortalHumanPlayer = false;

    std::ifstream mapFile(PROBE_MAP_PATH);
    const std::string mapData((std::istreambuf_iterator<char>(mapFile)), {});
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    GameInitSettings init("containment.ini", mapData, "Transport containment", true, settings.gameOptions);
    init.randomSeed = 913;
    GameInitSettings::HouseInfo humans(HOUSE_ATREIDES, 1);
    humans.addPlayerInfo({"Containment tester", "HumanPlayer"});
    init.addHouseInfo(humans);
    GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN, 2);
    enemies.addPlayerInfo({"Enemy", "HumanPlayer"});
    init.addHouseInfo(enemies);
    game->bReplay = true;  // Private synthetic fixture: no public match analytics.
    game->initGame(init);
    game->bReplay = false;

    auto* owner = game->getHouse(HOUSE_ATREIDES);
    auto* foe = game->getHouse(HOUSE_HARKONNEN);
    containmentRequire(owner && owner->isAlive(), "Containment fixture house missing");
    containmentRequire(foe && foe->isAlive(), "Containment fixture enemy house missing");
    owner->addCredits(20000, false);

    auto openPocket = [&](int width, int height) {
        for(int y = 3; y + height < currentGameMap->getSizeY() - 3; ++y) {
            for(int x = 3; x + width < currentGameMap->getSizeX() - 3; ++x) {
                bool clear = true;
                for(int dy = 0; dy < height && clear; ++dy) {
                    for(int dx = 0; dx < width; ++dx) {
                        const Tile* tile = currentGameMap->getTile(x + dx, y + dy);
                        if(tile->hasAGroundObject() || tile->isMountain()) { clear = false; break; }
                    }
                }
                if(clear) return Coord(x, y);
            }
        }
        throw std::runtime_error("Fixture needs an open pocket");
    };
    const Coord pocket = openPocket(18, 12);

    auto makeTank = [&](House* house, const Coord& at) {
        auto* tank = static_cast<GroundUnit*>(house->createUnit(Unit_Tank, true));
        containmentRequire(tank != nullptr, "Cannot create fixture tank");
        tank->deploy(at);
        tank->doSetAttackMode(GUARD);
        return tank;
    };
    auto makeCarryall = [&](House* house, const Coord& at) {
        auto* carry = static_cast<Carryall*>(house->createUnit(Unit_Carryall, true));
        containmentRequire(carry != nullptr, "Cannot create fixture carryall");
        carry->deploy(at);
        return carry;
    };

    // The orphan shape the regression produced, reproduced exactly: a cargo entry that cannot
    // describe a passenger, and a unit marked as carried anyway.
    auto orphan = [&](GroundUnit* unit, Carryall* staleCarrier) {
        if(staleCarrier != nullptr) {
            staleCarrier->pickedUpUnitList.push_back(NONE_ID);
        }
        unit->setPickedUp(staleCarrier);
        containmentRequire(!unit->isActive() && unit->isPickedUp(), "The orphan fixture is not hidden");
    };

    auto recovered = [&](const GroundUnit* unit, const char* context) {
        containmentRequire(unit->isActive() && !unit->isPickedUp(), context);
        containmentRequire(unit->isVisible(), "A recovered unit is still invisible");
        containmentRequire(unit->isRespondable(), "A recovered unit cannot be selected or hit");
        containmentRequire(currentGameMap->tileExists(unit->getLocation()),
                           "A recovered unit is not on the map");
        const Tile* tile = currentGameMap->getTile(unit->getLocation());
        containmentRequire(tile->getNonInfantryGroundObject() == unit,
                           "A recovered unit does not occupy its tile");
        containmentRequire(isInSpatialGrid(game.get(), unit),
                           "A recovered unit is missing from the spatial grid");
        containmentRequire(!unit->isAwaitingPickup() && !unit->hasBookedCarrier(),
                           "A recovered unit kept a stale carrier booking");
        containmentRequire(unit->getAttackMode() != CARRYALLREQUESTED,
                           "A recovered unit is still waiting for a lift");
    };

    // -----------------------------------------------------------------------------------------
    // 1. The regression: a badly damaged booked passenger, a free repair yard, and a second
    //    free carrier for the repair order to grab.
    // -----------------------------------------------------------------------------------------
    {
        auto* yard = static_cast<StructureBase*>(owner->placeStructure(
            NONE_ID, Structure_RepairYard, pocket.x + 12, pocket.y + 9, true, true));
        containmentRequire(yard != nullptr, "Could not place the repair yard");
        containmentRequire(static_cast<RepairYard*>(yard)->getNumBookings() == 0,
                           "The repair yard fixture starts booked");

        // Created first, so it is the first free carrier in unitList order: this is the aircraft
        // the repair order's own carryall request reaches, and the one the regression left
        // holding a passenger that another aircraft was flying away.
        auto* second = makeCarryall(owner, pocket + Coord(1, 1));
        auto* carrier = makeCarryall(owner, pocket + Coord(1, 4));

        auto* tank = makeTank(owner, pocket + Coord(6, 4));
        const Uint32 passengerID = tank->getObjectID();
        tank->setHealth(tank->getMaxHealth() / 3);
        containmentRequire(tank->isBadlyDamaged(), "The pickup phase needs a badly damaged unit");
        // Forced, so checkPos' own "drive yourself to the yard" rule stays out of the way and the
        // repair order really comes from the pickup.
        tank->doMove2Pos(pocket + Coord(16, 2), true);

        // The ordinary booking: the carrier takes the unit as its target, the unit books it back.
        carrier->setTarget(tank);
        carrier->clearPath();
        tank->bookCarrier(carrier);
        containmentRequire(tank->hasBookedCarrier() && tank->isAwaitingPickup(),
                           "The pickup phase never booked a carrier");
        containmentRequire(!second->isBooked(), "The second carrier starts booked");

        int ticks = 0;
        while(!carrier->hasCargo() && ticks < 6000) {
            containmentRequire(carrier->update(), "The carrier vanished before pickup");
            containmentRequire(second->update(), "The second carrier vanished before pickup");
            tank->update();
            ++game->gameCycleCount;
            ++ticks;
        }
        containmentRequire(carrier->hasCargo(), "The damaged unit was never collected");

        // This is the regression. The repair order cancels the booking that brought this aircraft
        // here and clears its target, so enrolling "the current target" enrolled NONE_ID and left
        // a live unit hidden inside nothing.
        const std::list<Uint32>& cargo = carrier->getCargoIds();
        containmentRequire(cargo.size() == 1, "The carrier enrolled more than one passenger");
        containmentRequire(cargo.front() == passengerID,
                           "The carrier enrolled something other than the unit it collected");
        containmentRequire(std::find(cargo.begin(), cargo.end(), NONE_ID) == cargo.end(),
                           "The carrier enrolled an unusable cargo id");
        containmentRequire(tank->isPickedUp() && !tank->isActive(),
                           "The collected unit is still on the map");

        // Exactly one aircraft owns it.
        containmentRequire(!tank->hasBookedCarrier() && !tank->isAwaitingPickup(),
                           "The passenger kept a pickup booking while being carried");
        containmentRequire(second->getTarget() != tank,
                           "A second aircraft still holds the passenger it did not collect");
        containmentRequire(!second->isBooked(),
                           "A replacement booking was retained against the second aircraft");

        // Repair priority is preserved: the aircraft now flies the unit to the yard.
        containmentRequire(carrier->getTarget() == yard,
                           "A badly damaged passenger was not redirected to the repair yard");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_PICKUP: ticks=%d cargo=%u second_booked=0",
                ticks, cargo.front());

        // Delivery into the bay, and the bay occupant is not an orphan.
        while(carrier->hasCargo() && ticks < 12000) {
            containmentRequire(carrier->update(), "The carrier vanished during delivery");
            ++game->gameCycleCount;
            ++ticks;
        }
        containmentRequire(!carrier->hasCargo(), "The damaged unit was never delivered");
        auto* repairYard = static_cast<RepairYard*>(yard);
        containmentRequire(repairYard->getContainedRepairUnit() == tank,
                           "The damaged unit did not end up inside the repair yard");
        containmentRequire(tank->isPickedUp() && !tank->isActive(),
                           "The bay occupant fixture is not hidden");

        game->reconcileTransportContainment();
        containmentRequire(repairYard->getContainedRepairUnit() == tank,
                           "Reconciliation pulled a unit out of a repair bay");
        containmentRequire(!tank->isActive() && tank->isPickedUp(),
                           "A repair bay occupant was recovered as an orphan");

        // And the yard releases it normally once it is repaired. Carryall return is switched off
        // for this release so the yard deploys it on the ground, which is the shortest production
        // path to "the unit is back under the player's control".
        game->gameInitSettings.gameOptions.manualCarryallDrops = false;
        int repairTicks = 0;
        while(repairYard->getContainedRepairUnit() != nullptr && repairTicks < 20000) {
            yard->update();
            ++game->gameCycleCount;
            ++repairTicks;
        }
        game->gameInitSettings.gameOptions.manualCarryallDrops = true;
        containmentRequire(repairYard->getContainedRepairUnit() == nullptr,
                           "The repair yard never released the unit");
        containmentRequire(tank->isActive() && !tank->isPickedUp(),
                           "The repaired unit did not come back out of the yard");
        containmentRequire(tank->getHealth() >= tank->getMaxHealth(),
                           "The released unit was not repaired");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_REPAIR: delivered=1 released=1 ticks=%d", repairTicks);

        tank->cancelDeployment(); carrier->cancelDeployment(); second->cancelDeployment();
        yard->destroy();
    }

    // -----------------------------------------------------------------------------------------
    // 2. Valid cargo and a refinery occupant are left alone.
    // -----------------------------------------------------------------------------------------
    {
        auto* carry = makeCarryall(owner, pocket + Coord(1, 1));
        auto* tank = makeTank(owner, pocket + Coord(6, 4));
        carry->giveCargo(tank);
        containmentRequire(carry->hasCargo() && !tank->isActive() && tank->isPickedUp(),
                           "The valid cargo fixture is not carried");

        auto* refinery = static_cast<StructureBase*>(owner->placeStructure(
            NONE_ID, Structure_Refinery, pocket.x + 12, pocket.y + 9, true, true));
        containmentRequire(refinery != nullptr, "Could not place the refinery");
        auto* harvester = static_cast<GroundUnit*>(owner->createUnit(Unit_Harvester, true));
        harvester->deploy(pocket + Coord(6, 6));
        auto* ferry = makeCarryall(owner, pocket + Coord(1, 3));
        ferry->giveCargo(harvester);
        // The production drop-off: the aircraft is over the refinery and hands the harvester in.
        currentGameMap->removeObjectFromMap(ferry->getObjectID());
        ferry->setLocation(refinery->getLocation());
        ferry->deployUnit(harvester->getObjectID());
        containmentRequire(refinery->getContainedHarvesterUnit() == harvester,
                           "The harvester was not stored by the refinery");
        containmentRequire(!harvester->isActive(), "The refinery occupant fixture is not hidden");

        game->reconcileTransportContainment();
        containmentRequire(carry->hasCargo() && carry->getCargoIds().front() == tank->getObjectID(),
                           "Reconciliation emptied a carrier that was holding a passenger");
        containmentRequire(!tank->isActive() && tank->isPickedUp(),
                           "Valid cargo was recovered as an orphan");
        containmentRequire(refinery->getContainedHarvesterUnit() == harvester,
                           "Reconciliation pulled a harvester out of a refinery");
        containmentRequire(!harvester->isActive(),
                           "A refinery occupant was recovered as an orphan");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_VALID: cargo=1 refinery=1 recovered=%u",
                game->getRecoveredPassengerCount());

        tank->cancelDeployment(); harvester->cancelDeployment();
        carry->cancelDeployment(); ferry->cancelDeployment();
        refinery->destroy();
    }

    // An earlier stale pickup request must never outrank a later actual cargo entry. This is
    // a valid old-save shape after a replacement booking survived a pickup.
    {
        auto* pending = makeCarryall(owner, pocket + Coord(1, 1));
        auto* actual = makeCarryall(owner, pocket + Coord(1, 3));
        auto* passenger = makeTank(owner, pocket + Coord(6, 2));
        pending->setTarget(passenger);
        passenger->bookCarrier(pending);
        actual->giveCargo(passenger);
        containmentRequire(pending->getTarget() == passenger,
                           "The pending-versus-cargo fixture lost its stale request");
        game->reconcileTransportContainment();
        containmentRequire(actual->getCargoIds().size() == 1 &&
                               actual->getCargoIds().front() == passenger->getObjectID(),
                           "A stale pickup request displaced actual cargo");
        containmentRequire(!passenger->isActive() && passenger->isPickedUp(),
                           "A stale pickup request caused actual cargo to be recovered");
        containmentRequire(pending->getTarget() != passenger,
                           "A stale pickup request still pursues an already carried passenger");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_STALE_REQUEST: actual_cargo_kept=1 stale_request_released=1");
        actual->cancelDeployment(); passenger->cancelDeployment(); pending->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 3. The three shapes an already orphaned unit comes in: a carrier that still exists with an
    //    unusable cargo entry, a carrier that is gone, and no carrier pointer at all.
    // -----------------------------------------------------------------------------------------
    {
        const int tanksBefore = owner->getNumItems(Unit_Tank);

        // (a) stale: the carrier is alive and its cargo entry cannot describe a passenger.
        auto* stale = makeCarryall(owner, pocket + Coord(1, 1));
        auto* tankA = makeTank(owner, pocket + Coord(6, 2));
        const Coord lostAt = tankA->getLocation();
        orphan(tankA, stale);
        containmentRequire(stale->isBooked(), "The stale fixture carrier is not booked by its cargo");

        game->reconcileTransportContainment();
        recovered(tankA, "An orphan with a stale carrier was not recovered");
        containmentRequire(tankA->getLocation() == lostAt,
                           "The orphan was not put back where it was lost");
        containmentRequire(!stale->hasCargo() && !stale->isBooked(),
                           "An unusable cargo entry kept the carrier booked");

        // (b) gone: the carrier is destroyed while the entry still cannot describe a passenger,
        // which is what leaves the unit's own carrier pointer dangling.
        auto* tankB = makeTank(owner, pocket + Coord(8, 2));
        auto* doomed = makeCarryall(owner, pocket + Coord(1, 3));
        orphan(tankB, doomed);
        doomed->destroy();
        containmentRequire(tankB->getTarget() == nullptr,
                           "The destroyed-carrier fixture still resolves a carrier");
        game->reconcileTransportContainment();
        recovered(tankB, "An orphan whose carrier was destroyed was not recovered");

        // (c) no carrier pointer at all.
        auto* tankC = makeTank(owner, pocket + Coord(10, 2));
        orphan(tankC, nullptr);
        game->reconcileTransportContainment();
        recovered(tankC, "An orphan with no carrier pointer was not recovered");

        containmentRequire(owner->getNumItems(Unit_Tank) == tanksBefore + 3,
                           "Recovery changed the house unit count");
        containmentRequire(owner->isAlive(), "Recovery lost a house that still had units");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_ORPHANS: stale=1 destroyed=1 unset=1 total_recovered=%u",
                game->getRecoveredPassengerCount());

        tankA->cancelDeployment(); tankB->cancelDeployment(); tankC->cancelDeployment();
        stale->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 4. The tile it was lost on is occupied: it comes back next to the occupant, which is not
    //    disturbed.
    // -----------------------------------------------------------------------------------------
    {
        auto* lost = makeTank(owner, pocket + Coord(6, 2));
        const Coord contested = lost->getLocation();
        orphan(lost, nullptr);
        auto* occupant = makeTank(owner, contested);
        containmentRequire(occupant->getLocation() == contested,
                           "The contested-tile fixture could not take the tile");

        game->reconcileTransportContainment();
        recovered(lost, "An orphan was not recovered when its own tile was taken");
        containmentRequire(lost->getLocation() != contested,
                           "Recovery put a unit on an occupied tile");
        containmentRequire(blockDistance(lost->getLocation(), contested) <= 2,
                           "Recovery put the unit far away from where it was lost");
        containmentRequire(occupant->isActive()
                               && currentGameMap->getTile(contested)->getNonInfantryGroundObject() == occupant,
                           "Recovery disturbed the unit already standing there");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_OCCUPIED: moved_to=%d,%d",
                lost->getLocation().x, lost->getLocation().y);

        lost->cancelDeployment(); occupant->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 5. Nothing legal anywhere near, but the map has room elsewhere: the unit comes back on a
    //    far tile instead of staying hidden for ever inside a walled-off corner.
    // -----------------------------------------------------------------------------------------
    {
        const Coord island = pocket + Coord(6, 4);
        auto* marooned = makeTank(owner, island);
        const Uint32 maroonedID = marooned->getObjectID();
        const int tanksBefore = owner->getNumItems(Unit_Tank);
        orphan(marooned, nullptr);

        // Real terrain, restored at the end of the phase: a tracked unit cannot stand on rock
        // mountains, so every tile the bounded search can reach is genuinely illegal. Tiles that
        // already hold something are left as they are - they are illegal for this unit anyway,
        // and turning a tile to mountain under a structure would destroy it.
        std::vector<std::pair<Coord, int>> savedTerrain;
        for(int dy = -13; dy <= 13; ++dy) {
            for(int dx = -13; dx <= 13; ++dx) {
                const Coord at(island.x + dx, island.y + dy);
                if(!currentGameMap->tileExists(at)) continue;
                Tile* tile = currentGameMap->getTile(at);
                if(tile->hasAGroundObject()) continue;
                savedTerrain.emplace_back(at, tile->getType());
                tile->setType(Terrain_Mountain);
            }
        }
        const Coord stillLegal = game->findPassengerRecoverySpot(marooned, island);
        if(stillLegal.isValid()) {
            const Tile* tile = currentGameMap->getTile(stillLegal);
            SDL_Log("TRANSPORT_CONTAINMENT_PROBE_BLOCKFIXTURE: legal=%d,%d type=%d ground=%d"
                    " infantry=%d underground=%d", stillLegal.x, stillLegal.y, tile->getType(),
                    tile->hasAGroundObject() ? 1 : 0, tile->hasInfantry() ? 1 : 0,
                    tile->hasAnUndergroundUnit() ? 1 : 0);
        }
        containmentRequire(stillLegal.isInvalid(),
                           "The blocked fixture left a legal tile inside the search radius");

        game->reconcileTransportContainment();
        containmentRequire(game->getObjectManager().getObject(maroonedID) != nullptr,
                           "A unit with no nearby tile was deleted");
        recovered(marooned, "A unit was not recovered from a walled-off area with room elsewhere");
        containmentRequire(blockDistance(marooned->getLocation(), island) > 12,
                           "The far-tile fallback did not leave the blocked area");
        containmentRequire(owner->getNumItems(Unit_Tank) == tanksBefore,
                           "Recovery changed the house unit count");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_BLOCKED: nearby_blocked=1 recovered_at=%d,%d",
                marooned->getLocation().x, marooned->getLocation().y);

        marooned->cancelDeployment();
        for(const auto& entry : savedTerrain) {
            currentGameMap->getTile(entry.first)->setType(entry.second);
        }
    }

    // -----------------------------------------------------------------------------------------
    // 5a. The whole map blocked except one far tile: consecutive passes walk the map in bounded
    //     slices, so the unit waits - recorded, never deleted - and lands on that one tile.
    // -----------------------------------------------------------------------------------------
    {
        const Coord island = pocket + Coord(6, 4);
        auto* marooned = makeTank(owner, island);
        const Uint32 maroonedID = marooned->getObjectID();
        const int tanksBefore = owner->getNumItems(Unit_Tank);
        orphan(marooned, nullptr);

        // The one tile left open is deliberately far from the anchor and from the map centre, so
        // only the map-wide slice search can find it.
        const Coord onlyTile(currentGameMap->getSizeX() - 4, currentGameMap->getSizeY() - 4);
        std::vector<std::pair<Coord, int>> savedTerrain;
        for(int y = 0; y < currentGameMap->getSizeY(); ++y) {
            for(int x = 0; x < currentGameMap->getSizeX(); ++x) {
                const Coord at(x, y);
                if(at == onlyTile) continue;
                Tile* tile = currentGameMap->getTile(at);
                if(tile->hasAGroundObject() || tile->isMountain()) continue;
                savedTerrain.emplace_back(at, tile->getType());
                tile->setType(Terrain_Mountain);
            }
        }
        savedTerrain.emplace_back(onlyTile, currentGameMap->getTile(onlyTile)->getType());
        currentGameMap->getTile(onlyTile)->setType(Terrain_Sand);
        containmentRequire(game->isLegalRecoveryTile(marooned, onlyTile),
                           "The single-tile fixture did not leave its one tile legal");

        // Bounded slices: the map is 51x31, the slice is 1024 tiles, so two or three passes cover
        // it. Each pass advances the simulation cycle by one interval, which is the only thing the
        // slice position is derived from.
        int passes = 0;
        const int slicesForWholeMap =
            (currentGameMap->getSizeX() * currentGameMap->getSizeY()) / 1024 + 2;
        while(!marooned->isActive() && passes < slicesForWholeMap) {
            containmentRequire(game->getObjectManager().getObject(maroonedID) != nullptr,
                               "A waiting unit was deleted");
            containmentRequire(marooned->isPickedUp(), "A waiting unit was put on an illegal tile");
            game->gameCycleCount += Game::transportContainmentIntervalCycles;
            game->reconcileTransportContainment();
            ++passes;
        }
        recovered(marooned, "The map-wide slice search never reached the only legal tile");
        containmentRequire(marooned->getLocation() == onlyTile,
                           "Recovery did not take the only legal tile on the map");
        containmentRequire(owner->getNumItems(Unit_Tank) == tanksBefore,
                           "A waiting unit changed the house unit count");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_SLICES: passes=%d bound=%d recovered_at=%d,%d",
                passes, slicesForWholeMap, onlyTile.x, onlyTile.y);

        marooned->cancelDeployment();
        for(const auto& entry : savedTerrain) {
            currentGameMap->getTile(entry.first)->setType(entry.second);
        }
    }

    // -----------------------------------------------------------------------------------------
    // 5b. A unit whose last position is gone: a bay occupant has no location at all. Recovery
    //     falls back to something its own house still holds.
    // -----------------------------------------------------------------------------------------
    {
        auto* nowhere = makeTank(owner, pocket + Coord(6, 2));
        currentGameMap->removeObjectFromMap(nowhere->getObjectID());
        nowhere->setLocation(INVALID_POS, INVALID_POS);
        nowhere->guardPoint.invalidate();
        orphan(nowhere, nullptr);
        containmentRequire(!currentGameMap->tileExists(nowhere->getLocation()),
                           "The no-location fixture still has a position");

        game->reconcileTransportContainment();
        recovered(nowhere, "An orphan with no last position was not recovered");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_ANCHOR: recovered_at=%d,%d",
                nowhere->getLocation().x, nowhere->getLocation().y);
        nowhere->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 5c. A refinery that has already released its harvester still points at it. That pointer is
    //     history, not containment: if the harvester is later lost it must still come back.
    // -----------------------------------------------------------------------------------------
    {
        auto* refinery = static_cast<StructureBase*>(owner->placeStructure(
            NONE_ID, Structure_Refinery, pocket.x + 12, pocket.y + 9, true, true));
        containmentRequire(refinery != nullptr, "Could not place the historical-pointer refinery");
        auto* harvester = static_cast<GroundUnit*>(owner->createUnit(Unit_Harvester, true));
        harvester->deploy(pocket + Coord(6, 6));
        auto* ferry = makeCarryall(owner, pocket + Coord(1, 3));
        ferry->giveCargo(harvester);
        currentGameMap->removeObjectFromMap(ferry->getObjectID());
        ferry->setLocation(refinery->getLocation());
        ferry->deployUnit(harvester->getObjectID());
        containmentRequire(refinery->getContainedHarvesterUnit() == harvester,
                           "The historical-pointer fixture never stored the harvester");

        // The refinery finishes with it the ordinary way. extractingSpice goes false, the stored
        // pointer stays.
        refinery->deployContainedHarvester(nullptr);
        containmentRequire(refinery->isHarvesterDropoffFree(),
                           "The refinery is still busy after releasing its harvester");
        containmentRequire(refinery->getContainedHarvesterUnit() == harvester,
                           "The fixture needs the refinery's historical pointer to survive");

        // Much later the same harvester is lost in a pickup.
        orphan(harvester, nullptr);
        game->reconcileTransportContainment();
        recovered(harvester, "A free refinery's historical pointer suppressed a recovery");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_HISTORICAL: free_refinery_pointer=1 recovered_at=%d,%d",
                harvester->getLocation().x, harvester->getLocation().y);

        harvester->cancelDeployment(); ferry->cancelDeployment();
        refinery->destroy();
    }

    // -----------------------------------------------------------------------------------------
    // 5d. A live aircraft still flying to a hidden unit: recovery releases both sides, so the
    //     carrier is not left chasing a unit that is back on the map.
    // -----------------------------------------------------------------------------------------
    {
        auto* chaser = makeCarryall(owner, pocket + Coord(1, 1));
        auto* lost = makeTank(owner, pocket + Coord(6, 2));
        orphan(lost, nullptr);
        // Both sides of a real booking, pointing at a unit no carrier actually holds.
        chaser->setTarget(lost);
        lost->bookCarrier(chaser);
        containmentRequire(chaser->isBooked() && lost->isAwaitingPickup() && lost->hasBookedCarrier(),
                           "The stale-booking fixture is not booked on both sides");

        game->reconcileTransportContainment();
        recovered(lost, "An orphan with a live carrier booking was not recovered");
        containmentRequire(chaser->getTarget() != lost,
                           "The carrier is still chasing the recovered unit");
        containmentRequire(!chaser->isBooked(), "The carrier kept its assignment after recovery");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_RECIPROCAL: released_both_sides=1");

        lost->cancelDeployment(); chaser->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 5e. Cargo entries are validated against live passengers and deduplicated, without ever
    //     dropping the one entry that is a carrier's real claim.
    // -----------------------------------------------------------------------------------------
    {
        // (a) one carrier listing the same passenger twice.
        auto* carry = makeCarryall(owner, pocket + Coord(1, 1));
        auto* twice = makeTank(owner, pocket + Coord(6, 2));
        carry->giveCargo(twice);
        carry->pickedUpUnitList.push_back(twice->getObjectID());
        containmentRequire(carry->getCargoIds().size() == 2, "The duplicate fixture is not doubled");

        // (b) two carriers claiming one passenger.
        auto* firstClaim = makeCarryall(owner, pocket + Coord(1, 3));
        auto* secondClaim = makeCarryall(owner, pocket + Coord(1, 5));
        auto* shared = makeTank(owner, pocket + Coord(8, 2));
        firstClaim->giveCargo(shared);
        secondClaim->pickedUpUnitList.push_back(shared->getObjectID());

        // (c) entries that do not name a hidden passenger at all.
        auto* bystander = makeTank(owner, pocket + Coord(10, 2));
        auto* liar = makeCarryall(owner, pocket + Coord(1, 7));
        liar->pickedUpUnitList.push_back(bystander->getObjectID());        // An active map unit.
        liar->pickedUpUnitList.push_back(carry->getObjectID());            // Another aircraft.

        game->reconcileTransportContainment();

        containmentRequire(carry->getCargoIds().size() == 1
                               && carry->getCargoIds().front() == twice->getObjectID(),
                           "Deduplication did not keep exactly one real claim");
        containmentRequire(!twice->isActive() && twice->isPickedUp(),
                           "Deduplication dropped the passenger out of its carrier");

        containmentRequire(firstClaim->getCargoIds().size() == 1
                               && firstClaim->getCargoIds().front() == shared->getObjectID(),
                           "The first carrier lost its claim on a shared passenger");
        containmentRequire(!secondClaim->hasCargo(),
                           "The second carrier kept a claim on another carrier's passenger");
        containmentRequire(!shared->isActive() && shared->isPickedUp(),
                           "A doubly claimed passenger was dropped by both carriers");

        containmentRequire(!liar->hasCargo(), "Entries that name no passenger were kept");
        containmentRequire(bystander->isActive() && !bystander->isPickedUp(),
                           "A unit named by a bogus cargo entry was disturbed");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_CARGO: deduped=1 foreign_claim_dropped=1 bogus_dropped=2");

        twice->cancelDeployment(); shared->cancelDeployment(); bystander->cancelDeployment();
        carry->cancelDeployment(); firstClaim->cancelDeployment();
        secondClaim->cancelDeployment(); liar->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 5f. Cargo belonging to another house - a deviated carryall keeps flying its old owner's
    //     passengers - is still cargo, and is not recovered out of the aircraft.
    // -----------------------------------------------------------------------------------------
    {
        auto* carry = makeCarryall(owner, pocket + Coord(1, 1));
        auto* foreign = makeTank(foe, pocket + Coord(6, 2));
        const int foeTanksBefore = foe->getNumItems(Unit_Tank);
        carry->giveCargo(foreign);
        containmentRequire(carry->hasCargo() && foreign->getOwner() == foe,
                           "The different-owner cargo fixture is wrong");

        game->reconcileTransportContainment();
        containmentRequire(carry->getCargoIds().size() == 1
                               && carry->getCargoIds().front() == foreign->getObjectID(),
                           "Another house's legitimate cargo was dropped from the carrier");
        containmentRequire(!foreign->isActive() && foreign->isPickedUp(),
                           "Another house's legitimate cargo was recovered as an orphan");
        containmentRequire(foe->getNumItems(Unit_Tank) == foeTanksBefore,
                           "Protecting foreign cargo changed its house's count");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_FOREIGN: protected=1");

        foreign->cancelDeployment(); carry->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 5g. More hidden units than one pass will touch: the batch is bounded, the rest are
    //     recorded as waiting, and the next pass works through them. No unit is recovered twice.
    // -----------------------------------------------------------------------------------------
    {
        const size_t crowd = 40;   // Deliberately above the per-pass batch of 32.
        std::vector<GroundUnit*> hidden;
        for(size_t i = 0; i < crowd; ++i) {
            auto* unit = makeTank(owner, pocket + Coord(2 + static_cast<int>(i % 14),
                                                        1 + static_cast<int>(i / 14)));
            orphan(unit, nullptr);
            hidden.push_back(unit);
        }
        const int tanksBefore = owner->getNumItems(Unit_Tank);

        game->gameCycleCount += Game::transportContainmentIntervalCycles;
        game->reconcileTransportContainment();
        size_t back = 0;
        for(GroundUnit* unit : hidden) if(unit->isActive()) ++back;
        containmentRequire(back == 32, "A pass did not recover exactly its bounded batch");
        containmentRequire(game->getPendingPassengerCount() == crowd - 32,
                           "The units left over were not recorded as waiting");

        game->gameCycleCount += Game::transportContainmentIntervalCycles;
        game->reconcileTransportContainment();
        back = 0;
        for(GroundUnit* unit : hidden) {
            if(unit->isActive()) ++back;
        }
        containmentRequire(back == crowd, "The next pass did not work through the rest");
        containmentRequire(game->getPendingPassengerCount() == 0, "Waiting units were not cleared");
        containmentRequire(owner->getNumItems(Unit_Tank) == tanksBefore,
                           "Batched recovery changed the house unit count");
        for(GroundUnit* unit : hidden) recovered(unit, "A batched recovery left a unit half-deployed");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_BATCH: crowd=%zu first_pass=32 second_pass=%zu",
                crowd, crowd - 32);

        for(GroundUnit* unit : hidden) unit->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 6. Destruction: a carrier takes its cargo with it, a repair yard takes its occupant, and
    //    neither leaves anything for recovery to find.
    // -----------------------------------------------------------------------------------------
    {
        auto* carry = makeCarryall(owner, pocket + Coord(1, 1));
        auto* cargo = makeTank(owner, pocket + Coord(6, 2));
        const Uint32 cargoID = cargo->getObjectID();
        const int tanksBefore = owner->getNumItems(Unit_Tank);
        carry->giveCargo(cargo);
        carry->destroy();
        containmentRequire(game->getObjectManager().getObject(cargoID) == nullptr,
                           "Cargo survived the loss of its carrier");
        containmentRequire(owner->getNumItems(Unit_Tank) == tanksBefore - 1,
                           "A destroyed passenger was still counted");

        auto* yard = static_cast<StructureBase*>(owner->placeStructure(
            NONE_ID, Structure_RepairYard, pocket.x + 12, pocket.y + 9, true, true));
        containmentRequire(yard != nullptr, "Could not place the destruction-phase repair yard");
        auto* patient = makeTank(owner, pocket + Coord(8, 2));
        const Uint32 patientID = patient->getObjectID();
        patient->setHealth(patient->getMaxHealth() / 3);
        patient->setTarget(yard);
        patient->goingToRepairYard = true;
        patient->setGettingRepaired();
        containmentRequire(static_cast<RepairYard*>(yard)->getContainedRepairUnit() == patient,
                           "The destruction-phase patient is not inside the yard");
        yard->destroy();
        containmentRequire(game->getObjectManager().getObject(patientID) == nullptr,
                           "A bay occupant survived the loss of its structure");

        const Uint32 recoveredBefore = game->getRecoveredPassengerCount();
        game->reconcileTransportContainment();
        containmentRequire(game->getRecoveredPassengerCount() == recoveredBefore,
                           "Destruction left something for recovery to find");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_DESTRUCTION: cargo_lost=1 occupant_lost=1");
    }

    // -----------------------------------------------------------------------------------------
    // 7. Determinism: recovery reads simulation state only. Same state, different random
    //    generator, same tiles - and the shared generator is not advanced at all.
    // -----------------------------------------------------------------------------------------
    {
        auto placeTrio = [&] {
            std::vector<GroundUnit*> trio;
            for(int i = 0; i < 3; ++i) {
                auto* unit = makeTank(owner, pocket + Coord(6 + i, 2));
                orphan(unit, nullptr);
                trio.push_back(unit);
            }
            return trio;
        };

        auto firstRun = placeTrio();
        const Uint32 seedBefore = game->randomGen.getSeed();
        game->reconcileTransportContainment();
        containmentRequire(game->randomGen.getSeed() == seedBefore,
                           "Recovery advanced the shared random generator");
        std::vector<Coord> firstTiles;
        for(GroundUnit* unit : firstRun) {
            recovered(unit, "A determinism-phase orphan was not recovered");
            firstTiles.push_back(unit->getLocation());
        }
        for(GroundUnit* unit : firstRun) unit->cancelDeployment();

        game->randomGen.rand(0, 1000);  // A different generator state for the second run.
        containmentRequire(game->randomGen.getSeed() != seedBefore, "The generator did not move");
        auto secondRun = placeTrio();
        game->reconcileTransportContainment();
        for(size_t i = 0; i < secondRun.size(); ++i) {
            recovered(secondRun[i], "A determinism-phase orphan was not recovered on the rerun");
            containmentRequire(secondRun[i]->getLocation() == firstTiles[i],
                               "Recovery chose a different tile with a different generator state");
        }
        for(GroundUnit* unit : secondRun) unit->cancelDeployment();
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_DETERMINISM: rng_untouched=1 tiles=%zu",
                firstTiles.size());
    }

    // -----------------------------------------------------------------------------------------
    // 8. The scheduled pass: the production cadence, not a direct call, is what recovers a unit,
    //    and it does so at a cycle derived from the cycle counter alone.
    // -----------------------------------------------------------------------------------------
    {
        game->initializeGameLoop();  // The production prerequisites of a simulation cycle.
        auto* unit = makeTank(owner, pocket + Coord(6, 2));
        orphan(unit, nullptr);
        const Uint32 interval = Game::transportContainmentIntervalCycles;
        game->gameCycleCount = interval * 20 - 3;
        while(game->gameCycleCount < interval * 20) {
            game->updateGameState();
            containmentRequire(!unit->isActive(),
                               "The scheduled pass ran before its cadence cycle");
        }
        // The cycle counter is now on an interval boundary, so this is the cycle that reconciles.
        game->updateGameState();
        recovered(unit, "The scheduled pass did not recover a hidden unit at its cadence cycle");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_SCHEDULE: interval=%u recovered_at=%u",
                interval, interval * 20);
        unit->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 8b. A save taken three cycles before a scheduled boundary, with one hidden orphan and one
    //     legitimately carried passenger. Phase 10 replays it as an ordinary load and as an
    //     observer restore. Written here, while this fixture game is still healthy.
    // -----------------------------------------------------------------------------------------
    {
        auto* carrier = makeCarryall(owner, pocket + Coord(1, 1));
        auto* passenger = makeTank(owner, pocket + Coord(6, 6));
        carrier->giveCargo(passenger);
        auto* lost = makeTank(owner, pocket + Coord(6, 2));
        orphan(lost, nullptr);
        gBoundaryOrphan = lost->getObjectID();
        gBoundaryPassenger = passenger->getObjectID();
        gBoundaryCarrier = carrier->getObjectID();

        const Uint32 interval = Game::transportContainmentIntervalCycles;
        game->gameCycleCount = (game->gameCycleCount / interval + 2) * interval - 3;
        gBoundaryCycle = (game->gameCycleCount / interval + 1) * interval;
        containmentRequire(game->saveGame(gBoundarySave.c_str()),
                           "Could not save the boundary fixture");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_BOUNDARY_SAVE: cycle=%u boundary=%u orphan=%u carried=%u",
                game->gameCycleCount, gBoundaryCycle, gBoundaryOrphan, gBoundaryPassenger);

        // The live fixture keeps going without them; the file is what phase 10 uses.
        lost->cancelDeployment(); passenger->cancelDeployment(); carrier->cancelDeployment();
    }

    // -----------------------------------------------------------------------------------------
    // 9. Elimination still works: a house whose last unit was hidden stays alive, as it did in
    //    the stalled match, and loses once the recovered unit is destroyed in the normal way.
    //    Its anchors are all invalid and its house owns nothing else, which is the case that has
    //    to fall back to the middle of the map rather than leave a permanent ghost.
    // -----------------------------------------------------------------------------------------
    {
        auto* survivor = makeTank(foe, pocket + Coord(14, 2));
        currentGameMap->removeObjectFromMap(survivor->getObjectID());
        survivor->setLocation(INVALID_POS, INVALID_POS);
        survivor->guardPoint.invalidate();
        orphan(survivor, nullptr);

        // Everything else the enemy owns goes first, so the hidden unit is the only thing
        // keeping it alive - the exact stalemate the match ended in.
        // Repeated, because a destroyed object can leave a surviving crew behind: the sweep is
        // finished only when the hidden unit really is the last thing the house owns.
        int sweeps = 0;
        for(; sweeps < 20; ++sweeps) {
            std::vector<Uint32> doomed;
            for(UnitBase* other : unitList) {
                if(other->getOwner() == foe && other != survivor) doomed.push_back(other->getObjectID());
            }
            for(StructureBase* structure : structureList) {
                if(structure->getOwner() == foe) doomed.push_back(structure->getObjectID());
            }
            if(doomed.empty()) break;
            for(const Uint32 id : doomed) {
                if(auto* object = game->getObjectManager().getObject(id)) object->destroy();
            }
        }
        containmentRequire(sweeps < 20, "Could not reduce the enemy house to its hidden unit");
        containmentRequire(foe->isAlive(),
                           "The elimination phase needs the hidden unit to keep the house alive");

        game->reconcileTransportContainment();
        recovered(survivor, "The enemy's hidden last unit was not recovered");
        containmentRequire(foe->isAlive(), "A recovered unit does not keep its house alive");
        const Coord centre(currentGameMap->getSizeX() / 2, currentGameMap->getSizeY() / 2);
        containmentRequire(blockDistance(survivor->getLocation(), centre) <= 12,
                           "A unit with no anchor at all did not fall back to the map centre");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_NOANCHOR: recovered_at=%d,%d centre=%d,%d",
                survivor->getLocation().x, survivor->getLocation().y, centre.x, centre.y);

        survivor->destroy();

        // A destroyed vehicle can leave its crew behind. That crew is an ordinary map unit, so
        // the house is eliminated when the last of them is killed in the normal way - and
        // nothing invisible is left holding it alive.
        for(int sweep = 0; sweep < 20 && foe->isAlive(); ++sweep) {
            std::vector<Uint32> crew;
            for(UnitBase* other : unitList) {
                if(other->getOwner() == foe) crew.push_back(other->getObjectID());
            }
            if(crew.empty()) break;
            for(const Uint32 id : crew) {
                if(auto* object = game->getObjectManager().getObject(id)) object->destroy();
            }
        }
        for(UnitBase* other : unitList) {
            containmentRequire(other->getOwner() != foe,
                               "A hidden unit survived the elimination sweep");
        }
        containmentRequire(!foe->isAlive(),
                           "The house survived the normal destruction of its last recovered unit");
        containmentRequire(game->finished && game->won,
                           "Destroying the last recovered opponent did not finish with victory");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_ELIMINATION: alive_while_hidden=1 eliminated_after_loss=1 victory=1");
    }

    const Uint32 liveRecovered = game->getRecoveredPassengerCount();
    game.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;

    // -----------------------------------------------------------------------------------------
    // 10. The same boundary from three starting points: an ordinary load, a second ordinary load,
    //     and an observer checkpoint restore. The recovery has to happen at the same cycle in all
    //     three, and the continuations have to stay identical - digest by digest and byte for
    //     byte - because the schedule is derived from the cycle counter and nothing is saved.
    // -----------------------------------------------------------------------------------------
    {
        const int tail = 8;   // Three cycles to the boundary, then five past it.
        std::string observerRuntime;

        struct Tail {
            std::vector<std::string> digests;
            std::string save;
            Uint32 recoveredAt = 0;
            Coord recoveredTile = Coord::Invalid();
        };

        auto loadBoundary = [&] {
            auto loaded = std::make_unique<Game>();
            currentGame = loaded.get();
            loaded->bReplay = true;   // No briefing, no public match analytics.
            loaded->initGame(GameInitSettings(gBoundarySave));
            loaded->bReplay = false;
            loaded->initializeGameLoop();
            loaded->bPause = false;
            return loaded;
        };

        auto runTail = [&](const char* label, bool restoreObserver, const std::string& savePath) {
            Tail result;
            auto loaded = loadBoundary();
            containmentRequire(loaded->getGameCycleCount() == gBoundaryCycle - 3,
                               "The boundary save did not reload at its own cycle");

            auto* orphanUnit = dynamic_cast<UnitBase*>(
                loaded->getObjectManager().getObject(gBoundaryOrphan));
            auto* carried = dynamic_cast<UnitBase*>(
                loaded->getObjectManager().getObject(gBoundaryPassenger));
            containmentRequire(orphanUnit != nullptr && carried != nullptr,
                               "The boundary save lost its fixture units");
            containmentRequire(orphanUnit->isPickedUp() && !orphanUnit->isActive(),
                               "The reloaded orphan is not hidden any more");
            containmentRequire(carried->isPickedUp() && !carried->isActive(),
                               "The reloaded passenger is not carried any more");

            if(restoreObserver) {
                containmentRequire(!observerRuntime.empty(), "No observer checkpoint was captured");
                loaded->loadObserverRuntime(observerRuntime);
                containmentRequire(orphanUnit->isPickedUp() && !orphanUnit->isActive(),
                                   "The observer restore deployed the orphan early");
            } else if(observerRuntime.empty()) {
                observerRuntime = loaded->saveObserverRuntime();
                IMemoryStream header(observerRuntime.data(), observerRuntime.size());
                containmentRequire(header.readUint32() == 7,
                                   "The observer runtime version changed");
            }

            for(int i = 0; i < tail; ++i) {
                // Pinned, so the comparison is of the simulation and not of two machines' frame
                // times: in single player the budget is adapted from measured wall-clock FPS.
                loaded->negotiatedBudget = 5000;
                loaded->carryOverTokens = 0;
                loaded->updateGameState();
                result.digests.push_back(GameStateDigest::describe(loaded->computeStateDigest()));
                if(result.recoveredAt == 0 && orphanUnit->isActive()) {
                    result.recoveredAt = loaded->getGameCycleCount();
                    result.recoveredTile = orphanUnit->getLocation();
                }
            }

            // The carrier is an ordinary aircraft and may well deliver its passenger during the
            // tail. What must never happen is the passenger ending up hidden with no carrier, or
            // the scheduled pass recovering anything other than the one real orphan.
            bool stillHeld = false;
            if(auto* boundaryCarrier = dynamic_cast<Carryall*>(
                   loaded->getObjectManager().getObject(gBoundaryCarrier))) {
                const auto& cargo = boundaryCarrier->getCargoIds();
                stillHeld = std::find(cargo.begin(), cargo.end(), gBoundaryPassenger) != cargo.end();
            }
            containmentRequire(carried->isActive() || stillHeld,
                               "A legitimately carried passenger ended up hidden with no carrier");
            containmentRequire(loaded->getRecoveredPassengerCount() == 1,
                               "The scheduled pass recovered something other than the one orphan");
            containmentRequire(loaded->saveGame(savePath.c_str()), "Could not save the tail");
            std::ifstream saved(savePath, std::ios::binary);
            result.save.assign(std::istreambuf_iterator<char>(saved), {});
            containmentRequire(!result.save.empty(), "The tail save is unreadable");

            SDL_Log("TRANSPORT_CONTAINMENT_PROBE_TAILDIGEST: %s recovered_at=%u tile=%d,%d "
                    "digest=%s save_bytes=%zu save_checksum=%llu", label, result.recoveredAt,
                    result.recoveredTile.x, result.recoveredTile.y, result.digests.back().c_str(),
                    result.save.size(),
                    static_cast<unsigned long long>(saveChecksum(result.save)));

            loaded.reset();
            currentGame = nullptr;
            pLocalPlayer = nullptr;
            return result;
        };

        const std::string base = std::string(std::getenv("CONTAINMENT_PROBE_OUT")) + "/tail-" + mod;
        const Tail first = runTail("ordinary", false, base + "-a.dls");
        const Tail second = runTail("ordinary_repeat", false, base + "-b.dls");
        const Tail observed = runTail("observer_restore", true, base + "-c.dls");

        containmentRequire(first.recoveredAt == gBoundaryCycle + 1,
                           "The reloaded orphan was not recovered on the boundary cycle");
        containmentRequire(second.recoveredAt == first.recoveredAt
                               && observed.recoveredAt == first.recoveredAt,
                           "Recovery landed on a different cycle after a reload or a restore");
        containmentRequire(second.recoveredTile == first.recoveredTile
                               && observed.recoveredTile == first.recoveredTile,
                           "Recovery chose a different tile after a reload or a restore");
        containmentRequire(second.digests == first.digests,
                           "Two ordinary loads of the same save diverged");
        containmentRequire(observed.digests == first.digests,
                           "An observer restore diverged from the ordinary continuation");
        containmentRequire(second.save == first.save && observed.save == first.save,
                           "The continuations produced different saved bytes");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_CONTINUATION: tail=%d recovered_at=%u digests=%zu "
                "identical=1 observer_runtime_bytes=%zu",
                tail, first.recoveredAt, first.digests.size(), observerRuntime.size());
    }

    // Power-of-two map and a stable backlog above the batch size: a unit tried only on
    // alternate passes must still visit consecutive map windows. Re-hide other subjects after
    // each pass to keep the queue stable and isolate the first subject's progress.
    {
        std::string largeMap = "[BASIC]\nVersion=2\nWinFlags=3\nLoseFlags=1\nTechLevel=8\n"
                               "[MAP]\nSizeX=128\nSizeY=128\n";
        for(int row = 0; row < 128; ++row) {
            char key[16];
            std::snprintf(key, sizeof(key), "%03d=", row);
            largeMap += key + std::string(128, '+') + "\n";
        }
        largeMap += "[Player1]\nCredits=2000\nBrain=Team1\n"
                    "[Player2]\nCredits=2000\nBrain=Team2\n"
                    "[UNITS]\nID001=Player1,MCV,256,0,64,Area Guard\n"
                    "ID002=Player2,MCV,256,16383,64,Area Guard\n";
        auto large = std::make_unique<Game>();
        currentGame = large.get();
        GameInitSettings largeInit("containment-large.ini", largeMap, "Containment queue", true,
                                   settings.gameOptions);
        largeInit.randomSeed = 913;
        largeInit.addHouseInfo(humans);
        largeInit.addHouseInfo(enemies);
        large->bReplay = true;
        large->initGame(largeInit);
        large->bReplay = false;
        auto* house = large->getHouse(HOUSE_ATREIDES);
        const Coord anchor(100, 100);
        std::vector<GroundUnit*> backlog;
        for(int i = 0; i < 64; ++i) {
            auto* unit = dynamic_cast<GroundUnit*>(house->createUnit(Unit_Tank, true));
            containmentRequire(unit != nullptr, "Cannot create a large-queue subject");
            unit->deploy(anchor);
            unit->setPickedUp(nullptr);
            backlog.push_back(unit);
        }
        for(int y = 0; y < 128; ++y) for(int x = 0; x < 128; ++x) {
            auto* tile = currentGameMap->getTile(x, y);
            if(!tile->hasAGroundObject()) tile->setType(Terrain_Mountain);
        }
        auto* subject = backlog.front();
        const Uint32 index = (subject->getObjectID() + 1536) % 16384;
        const Coord onlyTile(index % 128, index / 128);
        currentGameMap->getTile(onlyTile)->setType(Terrain_Sand);
        containmentRequire(large->findPassengerRecoverySpot(subject, anchor).isInvalid(),
                           "The large-queue fixture left a legal local tile");
        containmentRequire(large->isLegalRecoveryTile(subject, onlyTile),
                           "The large-queue fixture's far tile is not legal");
        Uint32 attemptedPasses = 0;
        for(Uint32 pass = 0; pass < 32; ++pass) {
            large->gameCycleCount = pass * Game::transportContainmentIntervalCycles;
            large->reconcileTransportContainment();
            ++attemptedPasses;
            if(subject->isActive()) break;
            for(auto* unit : backlog) if(unit->isActive()) {
                unit->setPickedUp(nullptr);
                unit->setLocation(anchor);
            }
        }
        containmentRequire(subject->isActive() && subject->getLocation() == onlyTile,
                           "Batch rotation starved a subject's global search windows");
        containmentRequire(attemptedPasses == 3,
                           "A queue round did not advance the subject's search window");
        SDL_Log("TRANSPORT_CONTAINMENT_PROBE_LARGE_QUEUE: backlog=64 map=128x128 passes=%u tile=%d,%d",
                attemptedPasses, onlyTile.x, onlyTile.y);
        large.reset();
        currentGame = nullptr;
        pLocalPlayer = nullptr;
    }

    SDL_Log("TRANSPORT_CONTAINMENT_PROBE_PASS: checks=%d mod=%s interval_cycles=%u recovered=%u",
            gContainmentChecks, mod.c_str(), Game::transportContainmentIntervalCycles,
            liveRecovered);
    return -1;  // Leaves the host's menu loop, the same convention the other probes use.
}
