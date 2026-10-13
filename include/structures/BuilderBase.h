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

#ifndef BUILDERBASE_H
#define BUILDERBASE_H

#include <structures/StructureBase.h>
#include <structures/ProductionRepeat.h>

#include <data.h>

#include <list>
#include <string>

class BuildItem {
public:
    BuildItem() {
        itemID = ItemID_Invalid;
        price = 0;
        num = 0;
    }

    BuildItem(int itemID, int price) {
        this->itemID = itemID;
        this->price = price;
        num = 0;
    }

    void save(OutputStream& stream) const {
        stream.writeUint32(itemID);
        stream.writeUint32(price);
        stream.writeUint32(num);
    }

    void load(InputStream& stream) {
        itemID = stream.readUint32();
        price = stream.readUint32();
        num = stream.readUint32();
    }

    Uint32 itemID;
    Uint32 price;
    Uint32 num;
};

class ProductionQueueItem {
public:
    ProductionQueueItem()
     : itemID(0), price(0) {

    }

    ProductionQueueItem(Uint32 _ItemID, Uint32 _price, FixPoint _startingCreditsPaid = 0)
     : itemID(_ItemID), price(_price), startingCreditsPaid(_startingCreditsPaid) {

    }

    void save(OutputStream& stream) const;

    void load(InputStream& stream);

    Uint32 itemID;
    Uint32 price;
    /// For builders that charge the whole price when the order is queued
    /// (Starport, Love Factory): how much of it came from the owner's exempt
    /// starting cash, so cancelling returns it there.
    FixPoint startingCreditsPaid = 0;
    /// Construction Yard pre-placement: 0 = no planned plot, otherwise the id of
    /// the reservation, unique within its builder (PlacementReservationPolicy.h).
    Uint32 reservationID = 0;
    Sint32 plotX = 0;               ///< planned top-left tile of a reserved order
    Sint32 plotY = 0;
    /// The planned plot was invalid when the order finished: it waits to be
    /// placed by hand like an unreserved order.
    bool   reservationFailed = false;
};


class BuilderBase : public StructureBase
{
public:
    explicit BuilderBase(House* newOwner);
    explicit BuilderBase(InputStream& stream);
    void init();
    virtual ~BuilderBase();

    BuilderBase(const BuilderBase &) = delete;
    BuilderBase(BuilderBase &&) = delete;
    BuilderBase& operator=(const BuilderBase &) = delete;
    BuilderBase& operator=(BuilderBase &&) = delete;

    void save(OutputStream& stream) const override;

    ObjectInterface* getInterfaceContainer() override;

    void setOwner(House *no);

    void setOriginalHouseID(int i) override
    {
        StructureBase::setOriginalHouseID(i);
        updateBuildList();
    }

    /**
        This method returns the maximum number of upgrades available in this building.
        \return the maximum number of upgrades available (0 if none)
    */
    int getMaxUpgradeLevel() const;

    /**
        This method checks what is available for purchase in this builder. It shall
        modify buildList appropriately.
    */
    virtual void updateBuildList();

    void setWaitingToPlace();
    void unSetWaitingToPlace();

    int getBuildListSize() const { return buildList.size(); }

    int getProductionQueueSize() const { return currentProductionQueue.size(); }

    /// The production queue in order; the front entry is the one under construction.
    const std::list<ProductionQueueItem>& getProductionQueue() const { return currentProductionQueue; }

    /// True while the finished front order holds a plot the simulation is still trying
    /// to place it on (ConstructionYard pre-placement); it is not ready to place by hand.
    bool isAwaitingReservedPlacement() const;

    /**
        Updates this builder.
        \return true if this object still exists, false if it was destroyed
    */
    bool update() override;

    virtual void handleUpgradeClick();
    virtual void handleProduceItemClick(Uint32 itemID, bool multipleMode = false);
    virtual void handleCancelItemClick(Uint32 itemID, bool multipleMode = false);
    virtual void handleSetOnHoldClick(bool OnHold);
    void handleSetRepeatClick(Uint32 itemID, bool enabled);



    /**
        Start upgrading this builder if possible.
        \return true if upgrading was started, false if not possible or already upgrading
    */
    virtual bool doUpgrade();

    /**
        Start production of the specified item.
        \param  itemID          the item to produce
        \param  multipleMode    false = 1 item, true = 5 items
    */
    virtual void doProduceItem(Uint32 itemID, bool multipleMode = false);

    /**
        Cancel production of the specified item.
        \param  itemID          the item to cancel
        \param  multipleMode    false = 1 item, true = 5 items
    */
    virtual void doCancelItem(Uint32 itemID, bool multipleMode = false);

    /**
        Sets the currently produced item on hold or continues production.
        \param  bOnHold         true = hold production; false = resume production
    */
    inline void doSetOnHold(bool bOnHold) { bCurrentItemOnHold = bOnHold; }

    /**
        Turns repeat production of a unit on or off (docs/rts-controls.md). Turning it on queues
        one copy when none is queued, so the loop has something to repeat.
        \param  itemID  the unit to repeat
        \param  enabled true = keep producing it; false = stop repeating
    */
    void doSetRepeat(Uint32 itemID, bool enabled);

    /// Can \a itemID be set to repeat in this builder right now?
    bool supportsRepeat(Uint32 itemID) const;

    /// Is \a itemID set to repeat?
    bool isRepeating(Uint32 itemID) const { return repeatItems.contains(itemID); }

    /**
        Start building a random item in this builder.
    */
    virtual void doBuildRandom();

    /**
        Limit the build speed of this builder to the given argument. The build speed can be limited by the AI
        to make it weaker, e.g. the campaign AI adjusts the build speed to the mission number
        \param newBuildSpeedLimit   the new limit; must be between 0 and 1
    */
    void doSetBuildSpeedLimit(FixPoint newBuildSpeedLimit) { buildSpeedLimit = std::max(0.0_fix, std::min(1.0_fix, newBuildSpeedLimit)); }



    inline bool isUpgrading() const { return upgrading; }
    inline bool isAllowedToUpgrade() const { return (curUpgradeLev < getMaxUpgradeLevel()); }
    inline int getCurrentUpgradeLevel() const { return curUpgradeLev; }
    int getUpgradeCost() const;
    inline FixPoint getUpgradeProgress() const { return upgradeProgress; }

    inline Uint32 getCurrentProducedItem() const { return currentProducedItem; }
    inline bool isOnHold() const { return bCurrentItemOnHold; }
    bool isWaitingToPlace() const;
    bool isUnitLimitReached(Uint32 itemID) const;
    inline FixPoint getProductionProgress() const { return productionProgress; }
    /// The part of the current production progress that was paid out of the
    /// owner's storage-exempt starting cash; a refund returns exactly this
    /// much to that pool.
    inline FixPoint getProductionProgressFromStartingCredits() const { return productionProgressFromStartingCredits; }
    inline const std::list<BuildItem>& getBuildList() const { return buildList; }

    virtual inline bool isAvailableToBuild(Uint32 itemID) const {
        return (getBuildItem(itemID) != nullptr);
    }

    /**
        Get the current build speed limit.
        \return the current build speed limit
    */
    FixPoint getBuildSpeedLimit() const { return buildSpeedLimit; }

protected:
    virtual void updateProductionProgress();

    void removeBuiltItemFromProductionQueue();

    virtual void insertItem(std::list<BuildItem>& buildItemList, std::list<BuildItem>::iterator& iter, Uint32 itemID, int price=-1);

    void removeItem(std::list<BuildItem>& buildItemList, std::list<BuildItem>::iterator& iter, Uint32 itemID);

    BuildItem* getBuildItem(Uint32 itemID) {
        for(BuildItem& buildItem : buildList) {
            if(buildItem.itemID == itemID) {
                return &buildItem;
            }
        }
        return nullptr;
    }

    const BuildItem* getBuildItem(Uint32 itemID) const {
        for(const BuildItem& buildItem : buildList) {
            if(buildItem.itemID == itemID) {
                return &buildItem;
            }
        }
        return nullptr;
    }

    void produceNextAvailableItem();

protected:
    static const int itemOrder[];  ///< the order in which items are in the build list

    // structure state
    bool     upgrading;              ///< Currently upgrading?
    FixPoint upgradeProgress;        ///< The current state of the upgrade progress (measured in money spent)
    Uint8    curUpgradeLev;          ///< Current upgrade level

    bool     bCurrentItemOnHold;     ///< Is the currently produced item on hold?
    Uint32   currentProducedItem;    ///< The ItemID of the currently produced item
    FixPoint productionProgress;     ///< The current state of the production progress (measured in money spent)
    FixPoint productionProgressFromStartingCredits; ///< Part of productionProgress paid from exempt starting cash
    Uint32   deployTimer;            ///< Timer for deploying a unit

    Uint32   nextReservationID = 1;  ///< Next placement reservation id handed out by this builder
    Uint32   reservedPlacementWaitCycles = 0; ///< Cycles the finished front reservation has waited for units to clear its plot

    /// Clear the progress and the payment provenance together: money already
    /// spent is gone, so a later refund must not claim a starting-cash share
    /// of it.
    void clearProductionProgress() { productionProgress = 0; productionProgressFromStartingCredits = 0; }
    /// Refund the whole progress of the item being produced to the pools that
    /// paid for it, then clear both.
    void refundProductionProgress();

    FixPoint buildSpeedLimit;        ///< Limit the build speed to that percentage [0;1]. This may be used by the AI to make it weaker.

    std::list<ProductionQueueItem>  currentProductionQueue;     ///< This list is the production queue (It contains the item IDs of the units/structures to produce)
    std::list<BuildItem>            buildList;                  ///< This list contains all the things that can be produced by this builder

    /// Units that are produced again whenever one leaves (SAVEGAMEVERSION 9855, docs/rts-controls.md).
    ProductionRepeat::RepeatSet     repeatItems;
};

#endif //BUILDERBASE_H
