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

#ifndef SPICEINCOME_H
#define SPICEINCOME_H

#include <SDL.h>

/**
    The per-house spice income multiplier.

    What it is: a small whole number, chosen once per house row in the Custom Game lobby, that
    multiplies the credits a refinery pays for spice a harvester actually delivers. Nothing else
    scales: not the cargo a harvester carries, not the spice on the map, not how fast a refinery
    unloads, and not city taxes, starting cash, refunds, crates or Choam trades.

    Why it is an integer and not a ratio: the factor is applied inside the deterministic
    simulation, so every peer has to compute the identical fixed-point result from it. An integer
    multiply of a fixed-point amount is exact, so a load delivered over many ticks pays out
    exactly cargo * factor with no accumulated residue, whichever tick boundaries the unload
    happens to straddle.

    Why there is no silent clamp: this value arrives from a savegame, from a host's game-init
    settings and from a lobby change event. A malformed factor is a refusal, not something to be
    quietly rounded into range - clamping would let a broken or hostile peer pick "1x" for a
    house the host set to "4x" and desynchronise the match instead of failing visibly. Only the
    *absence* of the field, in a save or payload written before the feature existed, defaults to
    kDefault.
*/
namespace SpiceIncome {

/// Lowest selectable factor; also what every pre-feature save and payload means.
constexpr Uint32 kMin = 1;
/// Highest selectable factor. The lobby offers every step from kMin to kMax.
constexpr Uint32 kMax = 5;
/// What a house gets when nothing chose for it (campaign, skirmish, old saves).
constexpr Uint32 kDefault = kMin;

/// How many steps the lobby selector offers.
constexpr Uint32 kChoiceCount = kMax - kMin + 1;

/**
    \param  value   a factor from a stream, a packet or a widget
    \return true if it is one of the factors this build accepts
*/
constexpr bool isValid(Uint32 value) {
    return value >= kMin && value <= kMax;
}

/// Marks the resolved per-row factors in a savegame's setup metadata ("SMUL"). It sits after
/// the setup colour block and carries one factor per saved setup row, in row order, taken from
/// the live house that row resolved to - which is the only place a Random row's factor exists.
constexpr Uint32 kSetupMarker = 0x534D554C;

/// First SAVEGAMEVERSION that writes kSetupMarker and the House field. Older saves have
/// neither and mean kDefault everywhere.
constexpr Uint32 kFirstSavegameVersion = 9851;

} // namespace SpiceIncome

#endif // SPICEINCOME_H
