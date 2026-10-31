/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <structures/ConstructionYard.h>
#include <structures/PlacementResult.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <SoundPlayer.h>
#include <sand.h>
#include <dunecity/CityConstants.h>
#include <misc/format.h>
#include <mod/ModManager.h>
#include <players/HumanPlayer.h>
#include <units/InfantryBase.h>

namespace {

bool footprintInsideMap(int x, int y, const Coord& size) {
    // Written without x + size so a hostile coordinate near INT_MAX cannot overflow.
    return size.x > 0 && size.y > 0 && x >= 0 && y >= 0
        && x <= currentGameMap->getSizeX() - size.x
        && y <= currentGameMap->getSizeY() - size.y;
}

/// One tile a slab may go on, units ignored.
bool slabTileOk(const Tile* tile) {
    return tile->isRock() && !tile->isMountain() && !tile->hasAStructure() && !tile->hasCityZone();
}

/**
    The normal placement terrain rules for itemID at x,y with units ignored and without the
    build-range rule: Map::okayToPlaceStructure (city rules included), no structure, no
    mountain, and no spice under a city-only structure (House::placeStructure refuses it).
    Slabs keep the sidebar placement rules: Slab4 needs one usable and one unpaved tile.
*/
bool plotTilesOk(Uint32 itemID, int x, int y) {
    const Coord size = getStructureSize(itemID);
    if(!footprintInsideMap(x, y, size)) {
        return false;
    }

    if(itemID == Structure_Slab1) {
        const Tile* tile = currentGameMap->getTile(x, y);
        return slabTileOk(tile) && !tile->isConcrete();
    }
    if(itemID == Structure_Slab4) {
        bool usable = false;
        bool unpaved = false;
        for(int dy = 0; dy < 2; ++dy) {
            for(int dx = 0; dx < 2; ++dx) {
                const Tile* tile = currentGameMap->getTile(x + dx, y + dy);
                usable |= slabTileOk(tile);
                unpaved |= !tile->isConcrete();
            }
        }
        return usable && unpaved;
    }

    if(!currentGameMap->okayToPlaceStructure(x, y, size.x, size.y, false, nullptr, true, itemID)) {
        return false;
    }
    const bool cityOnly = DuneCity::isCityOnlyStructure(itemID);
    for(int dy = 0; dy < size.y; ++dy) {
        for(int dx = 0; dx < size.x; ++dx) {
            const Tile* tile = currentGameMap->getTile(x + dx, y + dy);
            if(tile->isMountain() || tile->hasAStructure()) {
                return false;
            }
            if(cityOnly && (tile->isSpice() || tile->isSpiceBloom() || tile->isSpecialBloom())) {
                return false;
            }
        }
    }
    return true;
}

/// Some tile of the plot is within build range of the house's real map tiles.
bool plotInOwnRange(const House* house, Uint32 itemID, int x, int y) {
    const Coord size = getStructureSize(itemID);
    for(int dy = 0; dy < size.y; ++dy) {
        for(int dx = 0; dx < size.x; ++dx) {
            if(currentGameMap->isWithinBuildRange(x + dx, y + dy, house)) {
                return true;
            }
        }
    }
    return false;
}

/**
    The real-map check a finished reserved building is placed with: exactly the sidebar
    placement rules for slabs, Map::okayToPlaceStructure for everything else (plus the
    city-only spice rule House::placeStructure applies).
*/
bool plotPlaceableNow(const House* house, Uint32 itemID, int x, int y) {
    const Coord size = getStructureSize(itemID);
    if(!footprintInsideMap(x, y, size)) {
        return false;
    }
    if(itemID == Structure_Slab1) {
        return currentGameMap->isWithinBuildRange(x, y, house)
            && currentGameMap->okayToPlaceStructure(x, y, 1, 1, false, house)
            && !currentGameMap->getTile(x, y)->isConcrete();
    }
    if(itemID == Structure_Slab4) {
        bool usable = false;
        bool unpaved = false;
        for(int dy = 0; dy < 2; ++dy) {
            for(int dx = 0; dx < 2; ++dx) {
                usable |= currentGameMap->okayToPlaceStructure(x + dx, y + dy, 1, 1, false, house);
                unpaved |= !currentGameMap->getTile(x + dx, y + dy)->isConcrete();
            }
        }
        return usable && unpaved && plotInOwnRange(house, itemID, x, y);
    }
    return currentGameMap->okayToPlaceStructure(x, y, size.x, size.y, false, house, false, itemID)
        && plotTilesOk(itemID, x, y);
}

} // namespace

ConstructionYard::ConstructionYard(House* newOwner) : BuilderBase(newOwner) {
    ConstructionYard::init();

    setHealth(getMaxHealth());
}

ConstructionYard::ConstructionYard(InputStream& stream) : BuilderBase(stream) {
    ConstructionYard::init();
}

void ConstructionYard::init() {
    itemID = Structure_ConstructionYard;
    owner->incrementStructures(itemID);

    structureSize.x = 2;
    structureSize.y = 2;

    graphicID = ObjPic_ConstructionYard;
    graphic = pGFXManager->getObjPic(graphicID,getOwner()->getHouseID());
    numImagesX = 4;
    numImagesY = 1;

    firstAnimFrame = 2;
    lastAnimFrame = 3;
}

ConstructionYard::~ConstructionYard() = default;

bool ConstructionYard::doPlaceStructure(int x, int y, bool byPlayer) {
    if(isWaitingToPlace() || (byPlayer && isReservedPlacementBlocked())) {
        return placeFinishedStructure(x, y);
    } else {
        return false;
    }
}

bool ConstructionYard::placeFinishedStructure(int x, int y) {
    return PlacementResult::apply([&] { return getProductionQueueSize(); }, [&] {
        return getOwner()->placeStructure(getObjectID(), getCurrentProducedItem(), x, y) != nullptr;
    });
}

bool ConstructionYard::update() {
    if(BuilderBase::update() == false) {
        return false;
    }
    takeSharedQueueWork();
    keepPlannedPlotsClear();
    updateReservedPlacement();
    return true;
}

bool ConstructionYard::isReservedPlacementBlocked() const {
    return isAwaitingReservedPlacement() && reservedPlacementWaitCycles > 0;
}

bool ConstructionYard::isReservableItem(Uint32 itemID) {
    // Roads and power lines build instantly, so they keep the order-then-place flow.
    return isStructure(itemID) && !DuneCity::isCityTileTool(itemID);
}

bool ConstructionYard::mayReserveItem(Uint32 itemID) const {
    if(!isReservableItem(itemID)) {
        return false;
    }
    const BuildItem* buildItem = getBuildItem(itemID);
    if(buildItem == nullptr) {
        return false;
    }
    // The same one-palace rule doProduceItem applies to an ordinary order.
    return !(currentGame->getGameInitSettings().getGameOptions().onlyOnePalace
             && itemID == Structure_Palace
             && (buildItem->num > 0 || owner->getNumItems(Structure_Palace) > 0));
}

std::vector<PlacementReservation::Footprint> ConstructionYard::reservedFootprints(const House* house) {
    std::vector<PlacementReservation::Footprint> footprints;
    for(const StructureBase* structure : structureList) {
        if(structure->getOwner() != house || structure->getItemID() != Structure_ConstructionYard) {
            continue;
        }
        for(const ProductionQueueItem& entry : static_cast<const ConstructionYard*>(structure)->getProductionQueue()) {
            if(entry.reservationID != 0) {
                const Coord size = ::getStructureSize(entry.itemID);
                footprints.push_back({entry.plotX, entry.plotY, size.x, size.y});
            }
        }
    }
    return footprints;
}

std::vector<ConstructionYard::HouseGhost> ConstructionYard::houseGhosts(const House* house) {
    std::vector<HouseGhost> ghosts;
    std::vector<PlacementReservation::SharedSlot> slots;
    std::size_t yardOrder = 0;
    for(const StructureBase* structure : structureList) {
        if(structure->getOwner() != house || structure->getItemID() != Structure_ConstructionYard) {
            continue;
        }
        const auto* yard = static_cast<const ConstructionYard*>(structure);
        std::vector<Uint32> planOrders;
        for(const ProductionQueueItem& entry : yard->getProductionQueue()) {
            if(entry.reservationID != 0) planOrders.push_back(entry.planOrder);
        }
        std::size_t reserved = 0;
        for(const PlacementReservation::Ghost& ghost : PlacementReservation::ghostsInQueueOrder(yard->getProductionQueue())) {
            ghosts.push_back({yard, ghost});
            slots.push_back({planOrders[reserved++], static_cast<std::size_t>(ghost.number - 1), yardOrder});
        }
        ++yardOrder;
    }
    if(currentGame == nullptr || !currentGame->getGameInitSettings().getGameOptions().sharedYardQueue) {
        return ghosts;
    }
    // One queue: number the plots by their shared order.
    std::vector<std::size_t> order(ghosts.size());
    for(std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return PlacementReservation::sharedBefore(slots[a], slots[b]);
    });
    std::vector<HouseGhost> shared;
    shared.reserve(ghosts.size());
    for(std::size_t rank = 0; rank < order.size(); ++rank) {
        shared.push_back(ghosts[order[rank]]);
        shared.back().ghost.number = static_cast<int>(rank) + 1;
    }
    return shared;
}

PlacementReservation::Verdict ConstructionYard::checkReservationPlot(Uint32 itemID, int x, int y,
        const std::vector<PlacementReservation::Footprint>& pending) const {
    const Coord size = ::getStructureSize(itemID);
    std::vector<PlacementReservation::Footprint> reserved = reservedFootprints(owner);
    reserved.insert(reserved.end(), pending.begin(), pending.end());
    return PlacementReservation::validate({x, y, size.x, size.y}, plotTilesOk(itemID, x, y),
        [this](int tileX, int tileY) { return currentGameMap->isWithinBuildRange(tileX, tileY, owner); },
        reserved, BUILDRANGE);
}

bool ConstructionYard::doReservePlacement(Uint32 itemID, int x, int y) {
    if(!mayReserveItem(itemID)
       || checkReservationPlot(itemID, x, y) != PlacementReservation::Verdict::Valid) {
        return false;
    }

    const std::size_t queuedBefore = currentProductionQueue.size();
    doProduceItem(itemID, false);
    if(currentProductionQueue.size() != queuedBefore + 1) {
        return false;
    }

    // Next in the house's shared queue: after every plot the house still has planned.
    Uint32 lastPlanned = 0;
    for(const StructureBase* structure : structureList) {
        if(structure->getOwner() != owner || structure->getItemID() != Structure_ConstructionYard) {
            continue;
        }
        for(const ProductionQueueItem& entry : static_cast<const ConstructionYard*>(structure)->getProductionQueue()) {
            if(entry.reservationID != 0) lastPlanned = std::max(lastPlanned, entry.planOrder);
        }
    }

    ProductionQueueItem& order = currentProductionQueue.back();
    order.reservationID = nextReservationID;
    order.plotX = x;
    order.plotY = y;
    order.reservationFailed = false;
    order.planOrder = lastPlanned + 1;
    nextReservationID = PlacementReservation::nextReservationID(nextReservationID);
    return true;
}

bool ConstructionYard::doCancelReservation(Uint32 reservationID) {
    const PlacementReservation::CancelResult result =
        PlacementReservation::eraseReservation(currentProductionQueue, reservationID);
    if(!result.found) {
        return false;
    }

    for(BuildItem& buildItem : buildList) {
        if(buildItem.itemID == result.itemID && buildItem.num > 0) {
            buildItem.num--;
            break;
        }
    }

    if(result.wasInProduction) {
        // Exactly what cancelling the item under construction refunds today.
        refundProductionProgress();
        deployTimer = 0;
        produceNextAvailableItem();
    }
    return true;
}

void ConstructionYard::handleReservePlacementClick(Uint32 itemID, int x, int y) {
    currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_BUILDER_RESERVE_PLACEMENT,
        objectID, itemID, static_cast<Uint32>(x), static_cast<Uint32>(y)));
}

void ConstructionYard::handleCancelReservationClick(Uint32 reservationID) {
    currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_BUILDER_CANCEL_RESERVATION,
        objectID, reservationID));
}

void ConstructionYard::moveOwnUnitsOffPlot(Uint32 itemID, int x, int y, bool idleOnly) {
    moveUnitsOffPlot(itemID, x, y, idleOnly, false);
}

void ConstructionYard::moveUnitsOffPlot(Uint32 itemID, int x, int y, bool idleOnly, bool includeAllies) {
    // Seeded from the cycle, like the sidebar's version of this request, so every peer
    // picks the same spots without drawing from the game's random generator. The orders
    // are the forced moves UnitBase::handleMoveClick would send, applied in the tick.
    Random tempRandomGen(currentGame->getGameCycleCount());
    const Coord size = ::getStructureSize(itemID);
    const auto moveOff = [&](UnitBase* pUnit) {
        if(pUnit == nullptr || !pUnit->isRespondable()
           || (includeAllies ? pUnit->getOwner()->getTeamID() != owner->getTeamID() : pUnit->getOwner() != owner)) {
            return;
        }
        if(idleOnly && (pUnit->isMoving() || pUnit->getAttackMode() == HOLD
                        || (pUnit->getDestination().isValid() && pUnit->getDestination() != pUnit->getLocation()))) {
            return;
        }
        const Coord destination = currentGameMap->findDeploySpot(pUnit, Coord(x, y), tempRandomGen, pUnit->getLocation(), size);
        if(currentGameMap->tileExists(destination.x, destination.y)) {
            if(includeAllies) {
                pUnit->leaveHoldForOrder();
                pUnit->leaveFormation();
            }
            pUnit->doMove2Pos(destination.x, destination.y, true);
        }
    };
    for(int tileY = y; tileY < y + size.y; tileY++) {
        for(int tileX = x; tileX < x + size.x; tileX++) {
            Tile* pTile = currentGameMap->getTile(tileX, tileY);
            if(pTile == nullptr) continue;
            if(pTile->hasANonInfantryGroundObject()) {
                ObjectBase* pObject = pTile->getNonInfantryGroundObject();
                if(pObject->isAUnit()) {
                    moveOff(static_cast<UnitBase*>(pObject));
                }
            } else if(pTile->hasInfantry()) {
                const std::list<Uint32> infantryList = pTile->getInfantryList();
                for(Uint32 objectID : infantryList) {
                    moveOff(dynamic_cast<InfantryBase*>(currentGame->getObjectManager().getObject(objectID)));
                }
            }
        }
    }
}

void ConstructionYard::keepPlannedPlotsClear() {
    // Planned plots stay free: once a second (staggered per yard) this house's units that have
    // stopped on one are sent off it. Units walking through are left alone.
    const Uint32 period = MILLI2CYCLES(1000);
    if(period == 0 || (currentGame->getGameCycleCount() + objectID) % period != 0) {
        return;
    }
    const bool retry = currentGame->getGameInitSettings().getGameOptions().retryPlannedPlacement;
    for(const ProductionQueueItem& entry : currentProductionQueue) {
        if(entry.reservationID != 0 && (retry || !entry.reservationFailed)) {
            moveUnitsOffPlot(entry.itemID, entry.plotX, entry.plotY, true, retry);
        }
    }
}

void ConstructionYard::takeSharedQueueWork() {
    // An idle yard pulls the next planned order from the house's other yards. Only the queue
    // behind each front moves: those entries hold no money and no progress, and the plot is the
    // house's, so any yard that can build the item can take it. Checked twice a second, at a
    // per-yard phase, in structure-list order, so every peer moves the same order.
    if(!currentGame->getGameInitSettings().getGameOptions().sharedYardQueue
       || !currentProductionQueue.empty() || isUpgrading()
       || owner->getNumItems(Structure_ConstructionYard) < 2) {
        return;
    }
    const Uint32 period = MILLI2CYCLES(500);
    if(period != 0 && (currentGame->getGameCycleCount() + objectID) % period != 0) {
        return;
    }
    struct Offer { ConstructionYard* yard; std::list<ProductionQueueItem>::iterator entry; };
    std::vector<Offer> offers;
    std::vector<PlacementReservation::SharedSlot> slots;
    std::size_t yardOrder = 0;
    for(StructureBase* structure : structureList) {
        if(structure->getOwner() != owner || structure->getItemID() != Structure_ConstructionYard) {
            continue;
        }
        auto* yard = static_cast<ConstructionYard*>(structure);
        if(yard != this) {
            std::size_t position = 0;
            for(auto entry = yard->currentProductionQueue.begin(); entry != yard->currentProductionQueue.end(); ++entry, ++position) {
                if(position == 0 || entry->reservationID == 0 || !mayReserveItem(entry->itemID)) continue;
                offers.push_back({yard, entry});
                slots.push_back({entry->planOrder, position, yardOrder});
                break;
            }
        }
        ++yardOrder;
    }
    const int pick = PlacementReservation::pickSharedOrder(slots);
    if(pick < 0) {
        return;
    }
    ConstructionYard* donor = offers[static_cast<std::size_t>(pick)].yard;
    ProductionQueueItem moved = *offers[static_cast<std::size_t>(pick)].entry;
    donor->currentProductionQueue.erase(offers[static_cast<std::size_t>(pick)].entry);
    for(BuildItem& buildItem : donor->buildList) {
        if(buildItem.itemID == moved.itemID && buildItem.num > 0) {
            buildItem.num--;
            break;
        }
    }
    for(BuildItem& buildItem : buildList) {
        if(buildItem.itemID == moved.itemID) {
            buildItem.num++;
            moved.price = buildItem.price;
            break;
        }
    }
    // Ids are unique per yard: the order takes this yard's next one.
    moved.reservationID = nextReservationID;
    nextReservationID = PlacementReservation::nextReservationID(nextReservationID);
    currentProductionQueue.push_back(moved);
    clearProductionProgress();
    currentProducedItem = moved.itemID;
    reservedPlacementWaitCycles = 0;
}

bool ConstructionYard::foreignUnitsOnPlot(Uint32 itemID, int x, int y) const {
    const Coord size = ::getStructureSize(itemID);
    for(int tileY = y; tileY < y + size.y; tileY++) {
        for(int tileX = x; tileX < x + size.x; tileX++) {
            const Tile* pTile = currentGameMap->getTile(tileX, tileY);
            if(pTile == nullptr) continue;
            if(pTile->hasANonInfantryGroundObject()) {
                const ObjectBase* pObject = pTile->getNonInfantryGroundObject();
                if(pObject->isAUnit() && pObject->getOwner() != owner) return true;
            } else if(pTile->hasInfantry()) {
                for(Uint32 id : pTile->getInfantryList()) {
                    const ObjectBase* pObject = currentGame->getObjectManager().getObject(id);
                    if(pObject != nullptr && pObject->getOwner() != owner) return true;
                }
            }
        }
    }
    return false;
}

bool ConstructionYard::hostileUnitsOnPlot(Uint32 itemID, int x, int y) const {
    const Coord size = ::getStructureSize(itemID);
    const auto hostile = [&](const ObjectBase* object) {
        return object != nullptr && object->isAUnit() && object->getOwner() != nullptr
            && object->getOwner()->getTeamID() != owner->getTeamID();
    };
    for(int dy = 0; dy < size.y; ++dy) for(int dx = 0; dx < size.x; ++dx) {
        const Tile* tile = currentGameMap->getTile(x + dx, y + dy);
        if(tile == nullptr) continue;
        if(tile->hasANonInfantryGroundObject() && hostile(tile->getNonInfantryGroundObject())) return true;
        for(Uint32 id : tile->getInfantryList())
            if(hostile(currentGame->getObjectManager().getObject(id))) return true;
    }
    return false;
}

void ConstructionYard::updateReservedPlacement() {
    if(!isAwaitingReservedPlacement()) {
        return;
    }

    ProductionQueueItem& order = currentProductionQueue.front();
    const Uint32 itemID = order.itemID;
    const int x = order.plotX;
    const int y = order.plotY;

    const bool retry = currentGame->getGameInitSettings().getGameOptions().retryPlannedPlacement;
    // Re-arm reservations saved after the old permanent fallback. Never reinterpret
    // an ordinary unreserved order, which still needs the player to choose a plot.
    if(retry) order.reservationFailed = false;
    const bool placeableNow = plotPlaceableNow(owner, itemID, x, y);
    const bool placeableIgnoringUnits = !placeableNow
        && plotTilesOk(itemID, x, y) && plotInOwnRange(owner, itemID, x, y);

    switch(PlacementReservation::decideCompletion(placeableNow, placeableIgnoringUnits, retry,
                                                   retry && hostileUnitsOnPlot(itemID, x, y))) {
        case PlacementReservation::Completion::Cancel: {
            const Uint32 reservationID = order.reservationID;
            if(doCancelReservation(reservationID) && owner == pLocalHouse) {
                currentGame->addToNewsTicker(fmt::sprintf(_("%s cancelled: enemy units are on the planned site"), resolveItemName(itemID)));
            }
            return;
        }

        case PlacementReservation::Completion::Retry: {
            if(reservedPlacementWaitCycles % MILLI2CYCLES(1000) == 0)
                moveUnitsOffPlot(itemID, x, y, false, true);
            reservedPlacementWaitCycles++;
            return;
        }
        case PlacementReservation::Completion::Place: {
            // The order is now an ordinary finished building at its planned site; place it
            // through the same path a CMD_PLACE_STRUCTURE takes.
            reservedPlacementWaitCycles = 0;
            if(placeFinishedStructure(x, y)) {
                if(owner == pLocalHouse) {
                    soundPlayer->playSound(Sound_PlaceStructure);
                }
                return;
            }
        } break;

        case PlacementReservation::Completion::WaitForUnits: {
            // Units never cancel the plan. Own units are asked off once a second, as a player
            // clicking on a blocked plot would; another house's units are waited out. Meanwhile
            // the player may place the building elsewhere by hand (doPlaceStructure byPlayer).
            if(reservedPlacementWaitCycles == 0 && owner == pLocalHouse && foreignUnitsOnPlot(itemID, x, y)) {
                currentGame->addToNewsTicker(fmt::sprintf(_("%s is waiting: enemy units are on the planned site"), resolveItemName(itemID)));
            }
            if(reservedPlacementWaitCycles % MILLI2CYCLES(1000) == 0) {
                moveOwnUnitsOffPlot(itemID, x, y, false);
            }
            reservedPlacementWaitCycles++;
            return;
        }

        case PlacementReservation::Completion::Fallback:
        default:
            break;
    }

    if(retry) {
        ++reservedPlacementWaitCycles;
        return;
    }

    // Fall back to today's ready-to-place state: the ghost turns red and the player
    // places the building by hand.
    order.reservationFailed = true;
    reservedPlacementWaitCycles = 0;
    if(owner == pLocalHouse) {
        currentGame->addToNewsTicker(fmt::sprintf(_("%s could not be placed at the planned site"), resolveItemName(itemID)));
        soundPlayer->playSound(Sound_InvalidAction);
    }
}

void ConstructionYard::updateStructureSpecificStuff() {
    if(!ModManager::instance().isTornieContentActive()
       || currentGame == nullptr || getOwner() == nullptr || !getOwner()->isAI()
       || currentGame->techLevel < 9
       || getOwner()->getNumItems(Structure_LoveFactory) > 0
       || getProductionQueueSize() > 0
       || getCurrentProducedItem() != ItemID_Invalid
       || isUpgrading()
       || !isAvailableToBuild(Structure_LoveFactory)) {
        return;
    }

    const BuildItem* loveFactory = getBuildItem(Structure_LoveFactory);
    if(loveFactory != nullptr
       && getOwner()->getCredits() >= static_cast<int>(loveFactory->price) + 1500) {
        doProduceItem(Structure_LoveFactory);
    }
}
