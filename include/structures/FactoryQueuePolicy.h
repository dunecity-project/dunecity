#ifndef FACTORY_QUEUE_POLICY_H
#define FACTORY_QUEUE_POLICY_H

#include <data.h>

#include <cstddef>
#include <cstdint>
#include <list>
#include <utility>

/// Shared Factory Queue: allocation-free rules shared by BuilderBase and its tests.
namespace FactoryQueue {

/// Only ordinary pay-as-built unit producers pool orders; yards and paid imports do not.
constexpr bool builderSharesQueue(std::uint32_t itemID) {
    switch(itemID) {
        case Structure_LightFactory:
        case Structure_HeavyFactory:
        case Structure_HighTechFactory:
        case Structure_Barracks:
        case Structure_WOR:
        case Structure_Worfinery:
            return true;
        default:
            return false;
    }
}

struct SharedSlot {
    std::size_t position = 0;        ///< zero-based position in the donor's queue
    std::size_t structureOrder = 0;  ///< place in structureList, never object id or selection order
};

/// The earliest waiting position wins; ties keep structure-list order.
constexpr bool shouldPickOrder(const SharedSlot& candidate, const SharedSlot& selected) {
    return candidate.position != selected.position
        ? candidate.position < selected.position
        : candidate.structureOrder < selected.structureOrder;
}

/**
    Find a donor's first eligible waiting order. Progress belongs to the first entry matching
    currentProducedItem, not necessarily entry 0 (duplicate item ids are common). With no current
    item, protect entry 0 conservatively. No paid-at-order-time entry may change builders.
    CanTake supplies the receiver's build-list/availability and house unit-limit checks.
*/
template<class Entry, class CanTake>
std::pair<typename std::list<Entry>::iterator, std::size_t> firstWaitingOrder(
    std::list<Entry>& queue, std::uint32_t currentProducedItem, CanTake canTake) {
    bool foundCurrent = false;
    std::size_t position = 0;
    for(auto entry = queue.begin(); entry != queue.end(); ++entry, ++position) {
        const bool inProgress = currentProducedItem == ItemID_Invalid
            ? position == 0
            : !foundCurrent && entry->itemID == currentProducedItem;
        if(inProgress) {
            foundCurrent = true;
            continue;
        }
        if(entry->startingCreditsPaid == 0 && canTake(entry->itemID)) {
            return {entry, position};
        }
    }
    return {queue.end(), position};
}

} // namespace FactoryQueue

#endif // FACTORY_QUEUE_POLICY_H
