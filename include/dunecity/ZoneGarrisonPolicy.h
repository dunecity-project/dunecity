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

/// Infantry a 2x2 R/C/I zone holds (Stefan, 2026-11-28: "allow up to 5 units to garrison").
inline constexpr int kCapacity = 5;

/// A capture-ordered soldier enters from any tile touching the zone (diagonals included:
/// blockDistance of a diagonal neighbour is 1.41), so it never has to stand on the building.
inline const FixPoint kEntryDistance = 1.5_fix;

/// Game cycles between target searches of a garrison whose last search found nothing.
inline constexpr int kIdleSearchCycles = 8;

/// Every occupant takes 10% of actual building HP lost, not raw/overkill damage.
/// Carry sub-point damage exactly; never round each small hit up to one HP.
inline constexpr int64_t kDamagePoint = int64_t{10} << 32;
inline int occupantDamage(FixPoint buildingLoss, int64_t& remainder) {
    const int64_t raw = std::max<int64_t>(0, buildingLoss.getRawValue());
    const int64_t fraction = remainder + raw % kDamagePoint;
    const int damage = static_cast<int>(raw / kDamagePoint + fraction / kDamagePoint);
    remainder = fraction % kDamagePoint;
    return damage;
}

/// Unit health already carries fractional HP. Preserve the division remainder too,
/// so a full heal takes exactly CYCLES game cycles, independent of frame rate.
inline FixPoint healingStep(int maxHealth, uint64_t cycles, uint64_t& remainder) {
    const uint64_t fullHealth = static_cast<uint64_t>(FixPoint(maxHealth).getRawValue());
    uint64_t step = fullHealth / cycles;
    remainder += fullHealth - step * cycles;
    if(remainder >= cycles) {
        ++step;
        remainder -= cycles;
    }
    return FixPoint::FromRawValue(static_cast<int64_t>(step));
}

/// Stefan, 2026-11-27: "make troopers and infantry evacuate buildings with an evacuate when it gets
/// below X% health". A garrisoned zone below this share of its hit points puts its soldiers out,
/// and a zone below it cannot be garrisoned or taken. 60, chosen from 1,152 measured games at
/// 0/25/40/60 (docs/zone-garrisons.md, "Evacuation threshold"): placement and exchange value were
/// level at every threshold; 60 brought most soldiers out alive (65% of entries, 53% at 0) and lost
/// as few garrisons in exploding zones as 40. 0 would mean never.
inline constexpr int kEvacuateHealthPercent = 60;

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
