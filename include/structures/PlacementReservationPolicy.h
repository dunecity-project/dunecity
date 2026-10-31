#ifndef PLACEMENT_RESERVATION_POLICY_H
#define PLACEMENT_RESERVATION_POLICY_H

#include <algorithm>
#include <cstdint>
#include <list>
#include <vector>

/**
    Construction Yard pre-placement queue: the rules that do not need the map.

    A reservation is a production queue entry that also carries the plot its building goes on.
    The engine (ConstructionYard) answers the terrain questions; this header decides what the
    answers mean, so the unit tests exercise the same rules the simulation runs:
    - a plot may lean on earlier reservations of the same house for the build-range rule
      (chain planning), but may never overlap one;
    - a finished reservation retries obstructions and cancels on hostile units under the
      repaired match rule; the legacy control waits for units or falls back;
    - cancelling a reservation removes exactly its own queue entry, and only the entry under
      construction has money in it to refund.
*/
namespace PlacementReservation {

/// A plot in map tiles.
struct Footprint {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

inline bool overlaps(const Footprint& a, const Footprint& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

/**
    Map::isWithinBuildRange semantics with a reserved plot standing in for owned tiles: some
    tile of plot lies within `range` tiles (on both axes) of some tile of anchor.
*/
inline bool withinRange(const Footprint& plot, const Footprint& anchor, int range) {
    return overlaps(plot, Footprint{anchor.x - range, anchor.y - range, anchor.w + 2*range, anchor.h + 2*range});
}

enum class Verdict {
    Valid,          ///< the plot may be reserved
    Blocked,        ///< terrain, a structure, a city zone or the map edge refuses it
    Overlap,        ///< it overlaps another reservation of the same house
    OutOfRange      ///< nothing owned or reserved is within build range
};

/**
    \param  plot        the requested plot
    \param  tilesOk     the engine's per-tile answer with units ignored
    \param  ownRange    ownRange(x, y): Map::isWithinBuildRange for the house at that tile
    \param  reserved    every plot the house has reserved and still queued
    \param  range       BUILDRANGE
*/
template<class OwnRange>
Verdict validate(const Footprint& plot, bool tilesOk, OwnRange ownRange,
                 const std::vector<Footprint>& reserved, int range) {
    if(!tilesOk || plot.w <= 0 || plot.h <= 0) {
        return Verdict::Blocked;
    }
    for(const Footprint& other : reserved) {
        if(overlaps(plot, other)) {
            return Verdict::Overlap;
        }
    }
    for(int y = plot.y; y < plot.y + plot.h; ++y) {
        for(int x = plot.x; x < plot.x + plot.w; ++x) {
            if(ownRange(x, y)) {
                return Verdict::Valid;
            }
        }
    }
    for(const Footprint& other : reserved) {
        if(withinRange(plot, other, range)) {
            return Verdict::Valid;
        }
    }
    return Verdict::OutOfRange;
}

/// What the simulation does with a finished, reserved building this cycle.
enum class Completion {
    Place,          ///< the plot is valid on the real map: place it now
    WaitForUnits,   ///< only units are in the way: own units are moved off, others are waited for
    Retry,          ///< retain the plan until terrain, range or a friendly obstruction clears
    Cancel,         ///< hostile unit on the plot, or unreachable build range: refund this order and continue the queue
    Fallback        ///< legacy rule: the order becomes ordinary ready-to-place
};

/**
    Legacy control: units never cancel a plan; a plot blocked only by units waits until it clears
    (own units are moved off, enemy units are waited out) or the player places the building
    elsewhere by hand. Only a plot that is unusable without any units - a structure on it, or
    lost build range - falls back.
    With retryBlocked on, all obstructions retain the plan and hostile units cancel it. So does a
    plot whose build range nothing can still supply (rangeUnreachable: out of the house's own
    range and no other planned plot that can still be built is within range of it, e.g. after the
    player cancelled the order it was chain-planned on): it would otherwise wait forever and hold
    up every order behind it.
    \param  placeableNow            the plot passes the normal placement rules right now
    \param  placeableIgnoringUnits  it would pass if no unit stood on it (range included)
*/
inline Completion decideCompletion(bool placeableNow, bool placeableIgnoringUnits,
                                   bool retryBlocked = false, bool hostileUnit = false,
                                   bool rangeUnreachable = false) {
    if(retryBlocked) {
        if(hostileUnit) return Completion::Cancel;
        if(placeableNow) return Completion::Place;
        return rangeUnreachable ? Completion::Cancel : Completion::Retry;
    }
    if(placeableNow) {
        return Completion::Place;
    }
    return placeableIgnoringUnits ? Completion::WaitForUnits : Completion::Fallback;
}

/// Result of removing one reservation from a production queue.
struct CancelResult {
    bool found = false;
    bool wasInProduction = false;   ///< it was the front entry, the one money was spent on
    std::uint32_t itemID = 0;
};

/**
    Removes the queue entry holding reservationID. Entry needs itemID and reservationID.
    Only the front entry is under construction, so only it has progress to refund.
*/
template<class Entry>
CancelResult eraseReservation(std::list<Entry>& queue, std::uint32_t reservationID) {
    CancelResult result;
    if(reservationID == 0) {
        return result;
    }
    const auto entry = std::find_if(queue.begin(), queue.end(),
        [reservationID](const Entry& e) { return e.reservationID == reservationID; });
    if(entry == queue.end()) {
        return result;
    }
    result.found = true;
    result.wasInProduction = (entry == queue.begin());
    result.itemID = entry->itemID;
    queue.erase(entry);
    return result;
}

/// Credits a cancelled reservation returns: the progress if it was under construction.
template<class Money>
Money refundOnCancel(const CancelResult& result, Money progress) {
    return (result.found && result.wasInProduction) ? progress : Money(0);
}

/// One ghost to draw, in queue order.
struct Ghost {
    std::uint32_t reservationID = 0;
    std::uint32_t itemID = 0;
    int x = 0;
    int y = 0;
    int number = 0;                 ///< 1-based position in the yard's production queue
    bool inProduction = false;      ///< the front entry
    bool failed = false;            ///< fell back to ready-to-place
};

/**
    The reserved entries of a queue as ghosts, numbered by queue position, so unreserved orders
    between them keep their place in the count. Entry needs itemID, reservationID, plotX, plotY
    and reservationFailed.
*/
template<class Entry>
std::vector<Ghost> ghostsInQueueOrder(const std::list<Entry>& queue) {
    std::vector<Ghost> ghosts;
    int number = 0;
    for(const Entry& entry : queue) {
        ++number;
        if(entry.reservationID == 0) {
            continue;
        }
        Ghost ghost;
        ghost.reservationID = entry.reservationID;
        ghost.itemID = entry.itemID;
        ghost.x = entry.plotX;
        ghost.y = entry.plotY;
        ghost.number = number;
        ghost.inProduction = (number == 1);
        ghost.failed = entry.reservationFailed;
        ghosts.push_back(ghost);
    }
    return ghosts;
}

/// True if the front entry is reserved and still expects the simulation to place it.
template<class Entry>
bool frontAwaitsReservedPlacement(const std::list<Entry>& queue, bool retryFailed = false) {
    return !queue.empty() && queue.front().reservationID != 0
        && (retryFailed || !queue.front().reservationFailed);
}

/// The next reservation id after current, never 0 (0 means "not reserved").
inline std::uint32_t nextReservationID(std::uint32_t current) {
    return (current == 0xFFFFFFFFu) ? 1u : current + 1u;
}

/// A Construction Yard that can take a new reserved order, and how much work it still has.
struct YardLoad {
    std::uint32_t yardID = 0;
    long long remainingWork = 0;   ///< credits still to spend on its queue, incl. orders in flight
};

/**
    The yard a new reserved order goes to, so a house with several Construction Yards builds in
    parallel: the one with the least remaining work; on a tie the yard the player picked the
    building from, then the lowest object id. Returns 0 when there is no candidate.
*/
inline std::uint32_t chooseYard(const std::vector<YardLoad>& candidates, std::uint32_t preferredID) {
    const YardLoad* best = nullptr;
    for(const YardLoad& yard : candidates) {
        if(best == nullptr || yard.remainingWork < best->remainingWork
           || (yard.remainingWork == best->remainingWork
               && (yard.yardID == preferredID
                   || (best->yardID != preferredID && yard.yardID < best->yardID)))) {
            best = &yard;
        }
    }
    return best != nullptr ? best->yardID : 0;
}

/**
    Shared yard queue: the house's planned orders are one queue, in the order they were planned
    (ProductionQueueItem::planOrder), then by queue position and the yard's place in the
    structure list. Orders loaded from saves without a plan order (0) keep the order the
    least-work routing spread them over the yards in.
*/
struct SharedSlot {
    std::uint32_t planOrder = 0;   ///< ProductionQueueItem::planOrder
    std::size_t position = 0;      ///< place in its own yard's queue (0 = that yard's front)
    std::size_t yardOrder = 0;     ///< the yard's place among the house's yards
};

inline bool sharedBefore(const SharedSlot& a, const SharedSlot& b) {
    if(a.planOrder != b.planOrder) return a.planOrder < b.planOrder;
    return a.position != b.position ? a.position < b.position : a.yardOrder < b.yardOrder;
}

/**
    The waiting order an idle yard takes next: the earliest in the shared queue among the
    offers. Each other yard offers at most its first planned order behind its front; the front
    stays where its money and progress are. Returns the index into offers, or -1.
*/
inline int pickSharedOrder(const std::vector<SharedSlot>& offers) {
    int best = -1;
    for(std::size_t i = 0; i < offers.size(); ++i) {
        if(offers[i].position == 0) continue;
        if(best < 0 || sharedBefore(offers[i], offers[static_cast<std::size_t>(best)])) best = static_cast<int>(i);
    }
    return best;
}

} // namespace PlacementReservation

#endif // PLACEMENT_RESERVATION_POLICY_H
