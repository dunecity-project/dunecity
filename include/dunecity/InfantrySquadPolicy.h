#ifndef DUNECITY_INFANTRY_SQUAD_POLICY_H
#define DUNECITY_INFANTRY_SQUAD_POLICY_H
// Infantry squads (Stefan, 2026-11-28: "in the original dune you could build three troopers for a discount -
// i think maybe 3 were 200 credits?" ... "yes and the ai should use it if it wants to build these men").
// An Infantry order puts three Soldiers on the map, a Troopers order three Troopers (BuilderBase::update);
// the men are ordinary single units, so garrisons, capture, Hospital beds and the levy count men.
// docs/infantry-squads.md.
#include <data.h>
#include <SDL_stdinc.h>
#include <cstdint>

namespace DuneCity::InfantrySquad {
/// Men one order of \a item puts on the map: three for Infantry/Troopers, five for the Tornie orders, one
/// for a single foot soldier, 0 for anything else.
inline int orderSize(Uint32 item) {
    switch (item) {
        case Unit_Soldier: case Unit_Trooper: case Unit_GasInfantry: case Unit_FlameTrooper: return 1;
        case Unit_Infantry: case Unit_Troopers: return 3;
        case Unit_Infantry5: case Unit_Troopers5: return 5;
        default: return 0;
    }
}
/// The three-man squads. QuantBot orders one only with SquadOrders on.
inline bool threeManSquad(Uint32 item) { return item == Unit_Infantry || item == Unit_Troopers; }
/// The squad a builder of \a single offers alongside it, or ItemID_Invalid.
inline Uint32 squadOf(Uint32 single) {
    return single == Unit_Soldier ? Uint32(Unit_Infantry) : single == Unit_Trooper ? Uint32(Unit_Troopers) : Uint32(ItemID_Invalid);
}
/// An order of \a size men at \a price beats one of \a bestSize at \a bestPrice when it costs less per man
/// (ties: the larger order). A squad at three singles' price or more never beats the single.
inline bool cheaperPerMan(int price, int size, int bestPrice, int bestSize) {
    if (size <= 0) return false;
    if (bestSize <= 0) return true;
    const int64_t lhs = int64_t(price) * bestSize, rhs = int64_t(bestPrice) * size;
    return lhs < rhs || (lhs == rhs && size > bestSize);
}
}
#endif
