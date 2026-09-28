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

#include <cstdint>

/**
    What an ordinary gun, shell or rocket impact removes from a ground victim.

    "Ordinary" means the plain kinetic families: the unit and turret shells and the
    unguided/guided rockets. The special paths keep their own rules and their own radii and
    are deliberately not described here - gas conversion, Sonic waves, flame and persistent
    flame, the Sandworm swallow, sabotage, the Death Hand and the Devastator death blast all
    stay outside ordinaryBlast().

    Three things are settled here, all read from the shot rather than from the shooter's
    present owner:

      - Geometry, shared by both modes. Dune Dynasty's blast metric (max axis plus half the
        min axis, integer) over a full tile, cut into quarter-tile bands. A victim exactly one
        tile away takes nothing, and nothing is ever rounded up, so the zero tail of a small
        weapon stays zero. Dynasty's optional directional-consistency enhancement is not used.
      - The ground payload: what the shot carries once it is aimed at the ground.
      - The centre share of that payload, from which the bands fall away.

    The last two depend on Mode; the geometry does not. The category comes from the projectile
    and its source type. Damage comes from the configured payload, with the Ornithopter ratio
    below restoring its classic value while retaining proportional mod tuning.

    Only ordinary ground damage is described here. Direct damage to buildings, everything about
    the air layer, projectile flight, arming and reload, and all the special weapons are the
    same in both modes.
*/
namespace OrdinaryDamagePolicy {

/**
    Which ordinary-damage rules a match runs under.

    Chosen once per match from GameOptionsClass::originalUnitDamage, which is part of the
    settings every peer agrees on and which the save carries, so an in-flight shot lands under
    the same rules it was fired under.
*/
enum class Mode {
    /// The shipped balance: light guns deliver their whole payload, heavy weapons half.
    CityBalance,
    /**
        The "original unit damage" option: Dune II / Dune Dynasty classic ground damage, where
        the centre of the blast takes the whole payload whatever fired it.

        This reproduces the classic ground damage and splash numbers. It is not a complete
        re-implementation of the original engine - targeting, flight, reload and the special
        weapons are unchanged, and only ordinary ground damage moves.
    */
    Original,
};

inline Mode modeOf(bool originalUnitDamage) {
    return originalUnitDamage ? Mode::Original : Mode::CityBalance;
}

/// Ordinary splash reaches one full tile, strictly: at exactly TILESIZE the victim takes nothing.
inline constexpr int kBlastRadius = TILESIZE;
/// Quarter-tile falloff bands; each band halves what is left.
inline constexpr int kBandSize = TILESIZE / 4;

/**
    Ratio that restores the classic Ornithopter ground payload from the payload this content
    configures.

    Dune Dynasty gives the Ornithopter damage 50 and fires it as a mini rocket
    (src/table/unitinfo.c:159,161), and every mini rocket loses a quarter of its payload before
    it is launched (src/script/unit.c:679-681), so 50 - 50/4 = 38 hit points arrive. DuneCity's
    shipped ObjectData configures 45 instead, and the reduction lives in the Trooper branch of
    UnitBase::attack() rather than in the projectile, so neither number can be recovered from
    the other by the classic rule alone.

    Scaling by 38/45 turns the shipped 45 into the classic 38 and keeps a mod's own
    Ornithopter tuning proportional, which editing ObjectData would not. It applies to ground
    impacts only: the Ornithopter's anti-air damage is untouched in both modes.
*/
inline constexpr int kClassicOrnithopterGroundNumerator = 38;
inline constexpr int kClassicOrnithopterGroundDenominator = 45;

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
    Is this a light gun, which keeps its whole payload at the centre under CityBalance?

    \param  sourceItemID  the natural type of the object that fired, snapshotted when the shot
                          left the weapon. A Trooper's ranged rocket is a light weapon and a
                          Rocket Trike's or Ornithopter's rocket of the same projectile type is
                          not, so the projectile alone cannot decide this.

    Under Mode::Original every ordinary weapon keeps its whole payload, so this distinction
    does not apply there.
*/
inline bool lightPayload(Uint32 bulletID, Uint32 sourceItemID) {
    if(bulletID == Bullet_ShellSmall) return true;
    return bulletID == Bullet_SmallRocket && sourceItemID == Unit_Trooper;
}

/**
    The payload this shot delivers to the ground, before the blast bands are applied.

    This is the configured weapon damage in both modes, except for the classic Ornithopter
    correction described at kClassicOrnithopterGroundNumerator.
*/
inline int groundPayload(Mode mode, Uint32 bulletID, Uint32 sourceItemID, int nominalDamage) {
    if(mode != Mode::Original || nominalDamage <= 0) return nominalDamage;
    if(bulletID != Bullet_SmallRocket || sourceItemID != Unit_Ornithopter) return nominalDamage;
    return static_cast<int>(static_cast<int64_t>(nominalDamage) * kClassicOrnithopterGroundNumerator
                            / kClassicOrnithopterGroundDenominator);
}

/// How far the ground payload is halved at the centre of the blast: the band shift's offset.
inline int centreShift(Mode mode, Uint32 bulletID, Uint32 sourceItemID) {
    if(mode == Mode::Original) return 0;  // classic: the centre takes everything
    return lightPayload(bulletID, sourceItemID) ? 0 : 1;
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
inline int damageAt(Mode mode, Uint32 bulletID, Uint32 sourceItemID, int nominalDamage, int distance) {
    if(!withinBlast(distance)) return 0;
    const int payload = groundPayload(mode, bulletID, sourceItemID, nominalDamage);
    if(payload <= 0) return 0;
    return payload >> (centreShift(mode, bulletID, sourceItemID) + band(distance));
}

/// Effective centre damage, i.e. what the accepted per-weapon table lists.
inline int centreDamage(Mode mode, Uint32 bulletID, Uint32 sourceItemID, int nominalDamage) {
    return damageAt(mode, bulletID, sourceItemID, nominalDamage, 0);
}

} // namespace OrdinaryDamagePolicy

#endif // ORDINARY_DAMAGE_POLICY_H
