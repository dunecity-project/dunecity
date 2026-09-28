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

#ifndef ORDINARY_DAMAGE_POLICY_H
#define ORDINARY_DAMAGE_POLICY_H

#include <DataTypes.h>
#include <Definitions.h>
#include <DynastyProjectile.h>
#include <data.h>

/**
    What an ordinary gun, shell or rocket impact removes from a ground victim.

    "Ordinary" means the plain kinetic families: the unit and turret shells and the
    unguided/guided rockets. The special paths keep their own rules and their own radii and
    are deliberately not described here - gas conversion, Sonic waves, flame and persistent
    flame, the Sandworm swallow, sabotage, the Death Hand and the Devastator death blast all
    stay outside ordinaryBlast().

    Two independent things are settled here, both read from the shot rather than from the
    shooter's present owner:

      - Geometry. Dune Dynasty's blast metric (max axis plus half the min axis, integer) over
        a full tile, cut into quarter-tile bands. A victim exactly one tile away takes nothing,
        and nothing is ever rounded up, so the zero tail of a small weapon stays zero.
      - Payload category. A light gun (the small shell, and the Trooper's ranged rocket, which
        is a light infantry weapon carried on a rocket) delivers its full nominal payload at
        the centre. Heavy shells, turret shells and the rocket families deliver half.

    Nothing is hard-coded per unit: the category comes from the projectile and its source type,
    and the amount comes from the payload the content actually configured, so a mod that
    retunes WeaponDamage moves the whole profile with it.

    This is the accepted default only. The planned "original unit damage" game option is a
    second rule set behind the same three entry points - ordinaryBlast(), withinBlast() and
    damageAt() - so wiring it up means giving those a mode and adding the other branch, not
    threading a setting through Map::damage.
*/
namespace OrdinaryDamagePolicy {

/// Ordinary splash reaches one full tile, strictly: at exactly TILESIZE the victim takes nothing.
inline constexpr int kBlastRadius = TILESIZE;
/// Quarter-tile falloff bands; each band halves what is left.
inline constexpr int kBandSize = TILESIZE / 4;

/**
    Is this an ordinary kinetic impact against ground victims?

    Deliberately a closed list. Anything else - including the Devastator death blast, which
    passes its item id where a bullet id is expected - keeps the behaviour it had before.
*/
inline bool ordinaryBlast(Uint32 bulletID) {
    switch(bulletID) {
        case Bullet_ShellSmall:
        case Bullet_ShellMedium:
        case Bullet_ShellLarge:
        case Bullet_ShellTurret:
        case Bullet_SmallRocket:
        case Bullet_Rocket:
        case Bullet_TurretRocket:
            return true;
        default:
            return false;
    }
}

/**
    Does this shot deliver its whole nominal payload at the centre?

    \param  sourceItemID  the natural type of the object that fired, snapshotted when the shot
                          left the weapon. A Trooper's ranged rocket is a light weapon and a
                          Rocket Trike's or Ornithopter's rocket of the same projectile type is
                          not, so the projectile alone cannot decide this.
*/
inline bool lightPayload(Uint32 bulletID, Uint32 sourceItemID) {
    if(bulletID == Bullet_ShellSmall) return true;
    return bulletID == Bullet_SmallRocket && sourceItemID == Unit_Trooper;
}

/// Dune Dynasty's blast metric in world coordinates (64 per tile).
inline int distance(const Coord& a, const Coord& b) {
    return DynastyProjectile::distance(a*4, b*4)/4;
}

/// Quarter-tile band of a blast distance; 0 at the centre, 3 at the outer band.
inline int band(int distance) { return distance/kBandSize; }

inline bool withinBlast(int distance) { return distance >= 0 && distance < kBlastRadius; }

/**
    Hit points an ordinary impact removes from a ground victim at this blast distance.

    Integer, floored, and zero outside the full tile. The caller still decides whether the
    victim is eligible at all; this only answers "how much".
*/
inline int damageAt(Uint32 bulletID, Uint32 sourceItemID, int nominalDamage, int distance) {
    if(!withinBlast(distance) || nominalDamage <= 0) return 0;
    return nominalDamage >> ((lightPayload(bulletID, sourceItemID) ? 0 : 1) + band(distance));
}

/// Effective centre damage, i.e. what the accepted per-weapon table lists.
inline int centreDamage(Uint32 bulletID, Uint32 sourceItemID, int nominalDamage) {
    return damageAt(bulletID, sourceItemID, nominalDamage, 0);
}

} // namespace OrdinaryDamagePolicy

#endif // ORDINARY_DAMAGE_POLICY_H
