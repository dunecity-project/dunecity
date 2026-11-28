#ifndef UNITMOVEMENTPOLICY_H
#define UNITMOVEMENTPOLICY_H

#include <DataTypes.h>

namespace UnitMovementPolicy {
// Legacy bots rely on automatic restart; qBot owns explicit safety holds.
inline bool resumeStoppedHarvester(bool aiHouse, bool managedSafety) {
    return aiHouse && !managedSafety;
}


inline bool shouldCancelPickupOnMove(bool awaitingPickup, ATTACKMODE attackMode, bool bForced) {
    if(!bForced) {
        return false;
    }
    return awaitingPickup || attackMode == CARRYALLREQUESTED;
}

inline ATTACKMODE attackModeAfterCancellingPickup(ATTACKMODE current, bool isHarvester) {
    if(current != CARRYALLREQUESTED) {
        return current;
    }
    return isHarvester ? HARVEST : GUARD;
}

/// The mode a unit gets when a carryall puts it down (Carryall::deployUnit), after deploy() has
/// turned a lift request back into Guard. Saboteurs hunt and harvesters harvest, as in Dune
/// Legacy. A bot's unit gets Area Guard, which its army code expects. A player's unit keeps its
/// own mode, so a unit flown to its factory's destination guards that spot like one that drove
/// there: Area Guard chases anything within ten tiles, and units dropped at a rally point ran
/// off after sandworms (player report on 1.0.873). A player's attack-move or patrol keeps Area
/// Guard, the mode its walk fights in.
inline ATTACKMODE attackModeAfterCarryallDrop(ATTACKMODE current, bool isSaboteur, bool isHarvester,
                                              bool aiHouse, bool attackMoving) {
    if(isSaboteur) {
        return HUNT;
    }
    if(isHarvester) {
        return HARVEST;
    }
    if(aiHouse || attackMoving) {
        return AREAGUARD;
    }
    return attackModeAfterCancellingPickup(current, false);
}

}

#endif
