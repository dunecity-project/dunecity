#ifndef PRODUCTIONREPEAT_H
#define PRODUCTIONREPEAT_H

#include <data.h>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>

#include <set>
#include <stdexcept>

/**
    Repeat production (docs/rts-controls.md): Alt+click on a unit in a factory's build list
    keeps one more copy of it queued every time one leaves the factory. Pure rules and state,
    shared by BuilderBase, the save format and the unit tests.
*/
namespace ProductionRepeat {

/// Factories that offer repeat production. Never the Construction Yard or the Starport.
inline bool builderSupportsRepeat(int builderItemID) {
    switch(builderItemID) {
        case Structure_Barracks:
        case Structure_WOR:
        case Structure_LightFactory:
        case Structure_HeavyFactory:
        case Structure_HighTechFactory:
            return true;
        default:
            return false;
    }
}

/// Only units repeat; a structure has to be placed by hand.
inline bool itemSupportsRepeat(int builderItemID, int itemID) {
    return builderSupportsRepeat(builderItemID) && isUnit(itemID);
}

/**
    Does production of the current item advance this cycle? A repeated item obeys the same gate
    as any queued item: on hold, a reached unit limit or no credits pause it; none of them
    cancels it.
*/
inline bool productionAdvances(bool onHold, bool unitLimitReached, bool hasCredits) {
    return !onHold && !unitLimitReached && hasCredits;
}

class RepeatSet {
public:
    bool contains(Uint32 itemID) const { return items.count(itemID) != 0; }
    bool empty() const { return items.empty(); }
    std::size_t size() const { return items.size(); }

    void set(Uint32 itemID, bool enabled) {
        if(enabled) items.insert(itemID);
        else items.erase(itemID);
    }

    /// A repeating item that just left the factory is ordered once more while it can be built.
    bool requeueAfterCompletion(Uint32 finishedItemID, bool stillBuildable) const {
        return stillBuildable && contains(finishedItemID);
    }

    void save(OutputStream& stream) const {
        stream.writeUint32(static_cast<Uint32>(items.size()));
        for(const Uint32 itemID : items) stream.writeUint32(itemID);
    }

    void load(InputStream& stream) {
        items.clear();
        const Uint32 count = stream.readUint32();
        if(count > static_cast<Uint32>(Num_ItemID)) throw std::runtime_error("Too many repeating production items");
        for(Uint32 i = 0; i < count; ++i) {
            const Uint32 itemID = stream.readUint32();
            if(!isUnit(static_cast<int>(itemID))) throw std::runtime_error("Repeating production item is not a unit");
            items.insert(itemID);
        }
    }

    bool operator==(const RepeatSet& other) const { return items == other.items; }

private:
    std::set<Uint32> items;     ///< ordered, so every peer saves and iterates it identically
};

} // namespace ProductionRepeat

#endif // PRODUCTIONREPEAT_H
