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

#ifndef OBSERVEDHOUSESELECTOR_H
#define OBSERVEDHOUSESELECTOR_H

#include <globals.h>
#include <Game.h>
#include <House.h>
#include <sand.h>
#include <data.h>

#include <string>
#include <vector>

/**
    Which house a read-only window is currently showing.

    An observer may browse every house that exists in this match, in ascending
    house order, wrapping at the end. A player who is taking part sees their own
    house and nothing else. Nothing here changes pLocalHouse, pLocalPlayer or any
    control identity: the selection is window state, so looking at another house
    cannot become playing it.
*/
namespace ObservedHouse {

/// Houses that actually exist in this match, in ascending id order.
inline std::vector<int> browsable() {
    std::vector<int> houses;
    if(currentGame == nullptr) return houses;
    for(int houseID = 0; houseID < NUM_HOUSES; ++houseID)
        if(currentGame->getHouse(houseID) != nullptr) houses.push_back(houseID);
    return houses;
}

/// May this viewer look at a house other than their own?
inline bool mayBrowse() {
    return currentGame != nullptr && currentGame->isObserving();
}

/// The house a window should open on: an observer starts at the first house in
/// the match, a participant is pinned to their own.
inline int initialSelection() {
    if(!mayBrowse() && pLocalHouse != nullptr) return pLocalHouse->getHouseID();
    const auto houses = browsable();
    if(!houses.empty()) return houses.front();
    return pLocalHouse != nullptr ? pLocalHouse->getHouseID() : 0;
}

/// The next house in ascending order, wrapping back to the first. A participant
/// keeps their own house, so an injected click cannot move the selection.
inline int next(int current) {
    if(!mayBrowse()) return pLocalHouse != nullptr ? pLocalHouse->getHouseID() : current;
    const auto houses = browsable();
    if(houses.empty()) return current;
    for(size_t i = 0; i < houses.size(); ++i)
        if(houses[i] == current) return houses[(i + 1) % houses.size()];
    return houses.front();
}

/// The resolved house, or nullptr when the match has none with that id.
inline House* resolve(int houseID) {
    if(currentGame == nullptr) return nullptr;
    if(!mayBrowse()) return pLocalHouse;
    if(houseID < 0 || houseID >= NUM_HOUSES) return nullptr;
    return currentGame->getHouse(houseID);
}

/// Display name of a house, from the same helper the rest of the game uses.
inline std::string name(const House* house) {
    if(house == nullptr) return "-";
    return getHouseNameByNumber(static_cast<HOUSETYPE>(house->getHouseID()));
}

} // namespace ObservedHouse

#endif // OBSERVEDHOUSESELECTOR_H
