#ifndef QUANTBOT_COLONISATION_POLICY_H
#define QUANTBOT_COLONISATION_POLICY_H
#include <vector>

// When a city has built out the rock it stands on, growth can only continue
// somewhere else. This policy answers two questions: should another MCV be
// ordered so that safe, reachable free rock can be settled, whatever the
// production-yard target already says; and should an MCV already in hand
// settle a new formation rather than add one more yard where it stands?
//
// It deliberately owns nothing else. Where a colony goes stays with
// RockExpansionPolicy, growing on the formation the base already holds stays
// with McvDeployPolicy, and replacing a lost yard stays with the opening path.
// Nothing here is map, seed or difficulty specific: the same rule applies at
// every custom difficulty, and the campaign is excluded outright so scripted
// missions cannot start expanding.
namespace QuantBotColonisationPolicy {

// Yards one rock formation hosts before further MCVs move on. The founding
// yard plus one production yard keeps a base's build capacity; every MCV after
// that settles new rock while a safe destination exists, so the city spreads
// across the map instead of stacking all of its yards on one formation.
inline constexpr int kYardsPerFormation = 2;

// Building slots the base must still hold before it counts as built out. Four
// is "there is room for another corner of the city"; below it the yards own
// nothing worth developing and the next building has nowhere to go.
inline constexpr int kCrampedFootprints = 4;

// Counting every legal origin would read one small patch of rock as room for a
// whole district, so pack disjoint footprints instead: the answer is how many
// more buildings actually fit. Row-major and greedy, which is stable for a
// given map state and cheap enough to run inside the existing rock survey.
//
// \a buildable is one entry per tile: rock we may still build on, already
// excluding structures, city zones, reserved plots, unsafe ground and
// everything outside the base's build range. \a limit stops the count early —
// the decision only cares whether the base is cramped, not how large a healthy
// base is.
inline int freeFootprints(int w, int h, const std::vector<char>& buildable, int fw, int fh, int limit) {
    if(w <= 0 || h <= 0 || fw <= 0 || fh <= 0 || limit <= 0) return 0;
    if(static_cast<int>(buildable.size()) != w * h) return 0;
    std::vector<char> used(buildable.size(), 0);
    int found = 0;
    for(int y = 0; y + fh <= h; ++y) for(int x = 0; x + fw <= w; ++x) {
        bool fits = true;
        for(int dy = 0; dy < fh && fits; ++dy) for(int dx = 0; dx < fw && fits; ++dx) {
            const int i = (y + dy) * w + x + dx;
            if(!buildable[i] || used[i]) fits = false;
        }
        if(!fits) continue;
        for(int dy = 0; dy < fh; ++dy) for(int dx = 0; dx < fw; ++dx) used[(y + dy) * w + x + dx] = 1;
        if(++found >= limit) return found;
    }
    return found;
}

struct Demand {
    bool citySim = false;               ///< City growth rules are in force.
    bool customGame = false;            ///< Campaign bots keep their scripted base.
    bool campaignGame = false;          ///< Campaign game type, even in custom mode.
    bool supportMode = false;           ///< A helper on someone else's house does not colonise.
    bool siteAvailable = false;         ///< A colonist may set out for safe, reachable, roomy rock right now.
    bool productionRoomBlocked = false; ///< The placement search finds no site for another factory.
    int freeFootprints = 0;             ///< Building slots left on the base's own rock.
    int yards = 0;                      ///< Construction yards standing.
    int mcvsIncludingQueued = 0;        ///< MCVs alive, paid for or queued.
    int yardLimit = 0;                  ///< Game option ceiling; 0 means no ceiling.
    int yardsOnFormation = 0;           ///< Our yards on the formation an MCV stands on.
};

// Out of usable building rock: either the base cannot host another production
// building at all, or what is left of its own rock no longer holds a district.
inline bool builtOut(const Demand& demand) {
    return demand.productionRoomBlocked || demand.freeFootprints < kCrampedFootprints;
}

// The custom-game boundary every colonist shares: campaigns, helpers and
// vanilla rules never colonise, and nothing expands from a lost base.
inline bool colonisationAllowed(const Demand& demand) {
    if(!demand.citySim || !demand.customGame || demand.campaignGame || demand.supportMode) return false;
    // No yard means recovery, not colonisation: the opening path owns that.
    return demand.yards > 0;
}

// One colonist at a time, only from a standing base, only where the map and
// the game options allow another yard, and only towards a destination the
// survey has just confirmed. Affordability stays with the surrounding capital
// allocation, which already holds the forecast and the working reserve.
inline bool due(const Demand& demand) {
    if(!colonisationAllowed(demand)) return false;
    // An MCV already bought is the colonist; ordering a second one duplicates it.
    if(demand.mcvsIncludingQueued > 0) return false;
    if(demand.yardLimit > 0 && demand.yards + demand.mcvsIncludingQueued >= demand.yardLimit) return false;
    // Availability is re-surveyed, never remembered: rock that was unsafe
    // while an enemy held it becomes eligible again once that enemy is gone.
    if(!demand.siteAvailable) return false;
    return builtOut(demand);
}

// An MCV in hand settles new rock when its base is built out, or when the
// formation it stands on already runs its share of yards. Without a confirmed
// destination it stays a local yard: placing on the current rock is the
// fallback, never a reason to leave an MCV idle.
inline bool missionDue(const Demand& demand) {
    if(!colonisationAllowed(demand) || !demand.siteAvailable) return false;
    if(demand.yardLimit > 0 && demand.yards >= demand.yardLimit) return false;
    return builtOut(demand) || demand.yardsOnFormation >= kYardsPerFormation;
}

// Production yards the capacity target may ask for: the home formation's
// kYardsPerFormation and the founding yard of each colony. Capacity MCVs come
// out of the home factories, so a share counted for a colony would only be
// built at home, and a built-out base has no footprint left for one at all.
// Once the standing yards fill that, one more MCV is worth buying, as the
// colonist, and only while one may set out. Granting it any earlier buys an
// MCV that arrives while its formation's share is still filling and then
// stacks on the home rock or stands idle.
inline int productionYardCeiling(int heldFormations, int standingYards, bool siteAvailable, bool builtOut) {
    int share = kYardsPerFormation + (heldFormations > 1 ? heldFormations - 1 : 0);
    if(builtOut && standingYards < share) share = standingYards;
    return share + (siteAvailable && standingYards >= share ? 1 : 0);
}

}
#endif
