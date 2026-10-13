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

#ifndef CONSTRUCTIONYARD_H
#define CONSTRUCTIONYARD_H

#include <structures/BuilderBase.h>
#include <structures/PlacementReservationPolicy.h>

#include <vector>

class ConstructionYard final : public BuilderBase
{
public:
    explicit ConstructionYard(House* newOwner);
    explicit ConstructionYard(InputStream& stream);
    void init();
    virtual ~ConstructionYard();

    /**
        Places the just produced structure at x,y.
        \param  x           the x coordinate (in tile coordinates)
        \param  y           the y coordinate (in tile coordinates)
        \param  byPlayer    a player's CMD_PLACE_STRUCTURE: may also place a finished reserved
                            order whose plot is blocked by units somewhere else
        \return true if placement was successful, false otherwise
    */
    bool doPlaceStructure(int x, int y, bool byPlayer = false);
    void updateStructureSpecificStuff() override;

    bool update() override;

    /// The finished reserved front order is waiting for units to leave its plot.
    bool isReservedPlacementBlocked() const;

    /// A player may place the finished front order by hand: ready to place, or blocked by units.
    bool mayPlaceByHand() const override { return isWaitingToPlace() || isReservedPlacementBlocked(); }

    /// Structures that go through the pre-placement flow (everything but instant Roads).
    static bool isReservableItem(Uint32 itemID);

    /// The item is in this yard's list and may be ordered now (one-palace rule included).
    bool mayReserveItem(Uint32 itemID) const;

    /**
        Whether itemID may be reserved at x,y: the normal placement rules with units ignored,
        where earlier reservations of the same house count toward build range but may not be
        overlapped. The command applies exactly this check.
        \param  pending extra plots the local UI has ordered but not yet seen executed
    */
    PlacementReservation::Verdict checkReservationPlot(Uint32 itemID, int x, int y,
        const std::vector<PlacementReservation::Footprint>& pending = {}) const;

    /**
        Queues itemID and reserves its plot in one step (CMD_BUILDER_RESERVE_PLACEMENT).
        \return true if the order was queued
    */
    bool doReservePlacement(Uint32 itemID, int x, int y);

    /**
        Cancels the order holding reservationID with the normal refund
        (CMD_BUILDER_CANCEL_RESERVATION).
        \return true if such an order existed
    */
    bool doCancelReservation(Uint32 reservationID);

    void handleReservePlacementClick(Uint32 itemID, int x, int y);
    void handleCancelReservationClick(Uint32 reservationID);

    /// Every plot the house has reserved in any of its yards, in structure-list order.
    static std::vector<PlacementReservation::Footprint> reservedFootprints(const House* house);

private:
    /// The same placement the CMD_PLACE_STRUCTURE path performs.
    bool placeFinishedStructure(int x, int y);

    /// Places, waits for units, or falls back, once the reserved front order is finished.
    void updateReservedPlacement();

    /**
        Ask this house's units standing on the plot to move off it (deterministic).
        \param  idleOnly    only units that are standing still and not holding position; units
                            walking through or holding are left alone
    */
    void moveOwnUnitsOffPlot(Uint32 itemID, int x, int y, bool idleOnly);

    /// Once a second, own idle units are moved off every planned plot of this yard.
    void keepPlannedPlotsClear();

    /// Does a unit of another house stand on the plot?
    bool foreignUnitsOnPlot(Uint32 itemID, int x, int y) const;
};


#endif // CONSTRUCTIONYARD_H
