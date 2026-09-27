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

#ifndef GAS_DEVIATION_POLICY_H
#define GAS_DEVIATION_POLICY_H

#include <data.h>
#include <fixmath/FixPoint.h>

// Shared by target selection and gas impact; independent of house and game mode.
namespace GasDeviationPolicy {

inline FixPoint conversionChance() { return 0.80_fix; }

inline bool eligibleTarget(int itemID) {
    return isUnit(itemID)
        && itemID != Unit_Frigate
        && itemID != Unit_Sandworm
        && !isAmbientUnit(itemID);
}

} // namespace GasDeviationPolicy

#endif // GAS_DEVIATION_POLICY_H
