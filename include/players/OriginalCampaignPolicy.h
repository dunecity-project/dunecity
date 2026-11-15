#ifndef ORIGINAL_CAMPAIGN_POLICY_H
#define ORIGINAL_CAMPAIGN_POLICY_H

#include <data.h>
#include <array>

// Dune Dynasty 755876a710c0b188075ebe89cb40c068490e08e2, original (non-Brutal)
// branches of src/ai.c and src/table/unitinfo.c. See docs/original-campaign-ai.md.
namespace OriginalCampaignPolicy {

// Dune II enumerates unit types, not the factory menu's display/build-time order.
inline constexpr std::array<int, 18> unitOrder = {
    Unit_Carryall, Unit_Ornithopter, Unit_Infantry, Unit_Troopers,
    Unit_Soldier, Unit_Trooper, Unit_Saboteur, Unit_Launcher, Unit_Deviator,
    Unit_Tank, Unit_SiegeTank, Unit_Devastator, Unit_SonicTank, Unit_Trike,
    Unit_RaiderTrike, Unit_Quad, Unit_Harvester, Unit_MCV
};

inline int buildPriority(int item) {
    constexpr std::array<int, 18> priorities = {
        20, 75, 20, 50, 10, 20, 0, 100, 50, 80, 130, 175, 80, 50, 55, 60, 10, 10
    };
    for (std::size_t i = 0; i < unitOrder.size(); ++i)
        if (unitOrder[i] == item) return priorities[i];
    return 0;
}

// One random byte per available candidate; the 1/4 replacement is allowed to
// replace a MORE valuable prior candidate. It is not a fixed per-house choice.
template<class Available, class RandomByte>
int pickUnit(Available available, RandomByte randomByte) {
    int selected = ItemID_Invalid;
    int priority = 0;
    for (const int item : unitOrder) {
        if (!available(item)) continue;
        const int candidatePriority = buildPriority(item);
        if (randomByte() % 4 == 0 || selected == ItemID_Invalid || candidatePriority > priority) {
            selected = item;
            priority = candidatePriority;
        }
    }
    return selected;
}

inline bool combatUnit(int item) {
    return item != Unit_Carryall && item != Unit_Frigate && item != Unit_Harvester
        && item != Unit_MCV && item != Unit_Sandworm && item != Unit_Saboteur;
}

// Dune II wakes a computer house (production, repairs, teams, palace) only when the
// player's own sight uncovers one of its ground units: unit.c Unit_UpdateMap calls
// Unit_HouseUnitCount_Add(unit, g_playerHouseID) for a unit on a tile unveiled to the
// player, and Unit_Server_HouseUnitCount_Add then sets isAIActive on both houses
// unless they are allied or the unit is winged. A computer house seeing the player's
// units wakes nobody. `observerIsOriginalAI` is that computer side.
inline bool sightActivates(bool observerIsOriginalAI, bool allied, bool flying, bool tileSeenByObserver) {
    return !observerIsOriginalAI && !allied && !flying && tileSeenByObserver;
}

inline int targetPriority(int combinedPriority, int distance, bool structure) {
    if (distance == 0) return combinedPriority;
    return combinedPriority / distance + (structure ? 0 : 1);
}

} // namespace OriginalCampaignPolicy
#endif
