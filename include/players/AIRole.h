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

#ifndef AIROLE_H
#define AIROLE_H

#include <cstdint>

/**
    What an AI player does for its house (docs/ai-roles.md). The values are saved, so only append.
    Both:    builds the base and commands the army (the normal AI).
    Builder: builds and runs the economy, never attacks or defends (the former "AI Support").
    Fighter: commands units, attacks and defends, never builds or produces.
*/
enum class AIRole : std::uint8_t {
    Both = 0,
    Builder = 1,
    Fighter = 2
};

inline const char* aiRoleName(AIRole role) {
    switch(role) {
        case AIRole::Builder: return "Builder";
        case AIRole::Fighter: return "Fighter";
        case AIRole::Both:
        default:              return "Both";
    }
}

#endif // AIROLE_H
