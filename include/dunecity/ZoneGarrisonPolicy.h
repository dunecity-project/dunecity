/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef DUNECITY_ZONEGARRISONPOLICY_H
#define DUNECITY_ZONEGARRISONPOLICY_H

#include <fixmath/FixPoint.h>

#include <algorithm>
#include <cstdint>

/// Zone Garrisons (match rule, default on; Stefan, 2026-11-27: "i want infantry and troopers to be
/// able to occupy friendly and enemy R/I/C - using the capture command ... it now is owned by the
/// house that occupies it"). Pure rules shared by ZoneStructure, InfantryBase, UnitBase and QuantBot;
/// docs/zone-garrisons.md describes the whole feature.
namespace DuneCity::ZoneGarrison {

/// Soldiers a 2x2 Residential, Commercial or Industrial zone holds: one a tile.
inline constexpr int kCapacity = 4;

/// A capture-ordered soldier enters from any tile touching the zone (diagonals included:
/// blockDistance of a diagonal neighbour is 1.41), so it never has to stand on the building.
inline const FixPoint kEntryDistance = 1.5_fix;

/// Game cycles between target searches of a garrison whose last search found nothing.
inline constexpr int kIdleSearchCycles = 8;

/// What a garrisoned zone does with a hit of DAMAGE: the building takes all of it, and the soldier
/// at the front of the garrison (the first to enter) half of it, rounded up. Sandbags and walls are
/// cover, not immunity, so shooting a garrisoned zone is how attackers empty it.
inline constexpr int occupantDamage(int damage) {
    return damage <= 0 ? 0 : (damage + 1) / 2;
}

/// Stefan, 2026-11-27: "make troopers and infantry evacuate buildings with an evacuate when it gets
/// below X% health". A garrisoned zone below this share of its hit points puts its soldiers out,
/// and a zone below it cannot be garrisoned or taken. Chosen from measured games
/// (docs/zone-garrisons.md, "Evacuation threshold"); 0 would mean never.
inline constexpr int kEvacuateHealthPercent = 40;

/// Is a zone at HEALTH of MAXHEALTH too damaged to hold, at threshold PERCENT?
inline constexpr bool belowEvacuateHealth(int health, int maxHealth, int percent) {
    return percent > 0 && int64_t(health) * 100 < int64_t(maxHealth) * percent;
}

/// "if the building explodes they are ejected but are damaged": a soldier thrown out of a ruin
/// loses half its maximum hit points (rounded up) but always comes out alive with at least 1.
inline constexpr int healthAfterEjection(int health, int maxHealth) {
    return std::max(1, health - (maxHealth + 1) / 2);
}

/// What a capture order on a zone means for a soldier of house OWN (team OWNTEAM) against a zone
/// held by house ZONEOWNER (team ZONETEAM) with OCCUPANTS soldiers of house GARRISONHOUSE inside.
enum class Entry {
    Enter,          ///< walk in: an own zone with room, or an enemy zone nobody holds
    EnterAndTake,   ///< walk in and the zone becomes the soldier's house's (enemy, empty)
    Assault,        ///< enemy soldiers hold it: fire on it until they are gone, then walk in
    Refused,        ///< full, or an ally's zone (allies' zones are never taken or shared)
};

inline constexpr Entry entryFor(int ownHouse, int ownTeam, int zoneOwner, int zoneTeam,
                                int occupants, int garrisonHouse) {
    if(zoneTeam != ownTeam) {
        if(occupants > 0) return Entry::Assault;
        return Entry::EnterAndTake;
    }
    if(zoneOwner != ownHouse) return Entry::Refused;
    if(occupants >= kCapacity) return Entry::Refused;
    if(occupants > 0 && garrisonHouse != ownHouse) return Entry::Refused;
    return Entry::Enter;
}

} // namespace DuneCity::ZoneGarrison

#endif // DUNECITY_ZONEGARRISONPOLICY_H
