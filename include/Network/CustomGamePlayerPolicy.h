// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef CUSTOMGAMEPLAYERPOLICY_H
#define CUSTOMGAMEPLAYERPOLICY_H

#include <DataTypes.h>

#include <algorithm>
#include <array>
#include <vector>

namespace CustomGamePlayerPolicy {

/// Storage supports all twelve factions; other content keeps its nine-row lobby.
constexpr int maxPlayers(bool tornieFamily) noexcept {
    return tornieFamily ? NUM_PLAYER_HOUSES : HOUSE_CUSTOM + 1;
}

constexpr bool isPlayableHouseID(int house) noexcept {
    return isPlayerHouseID(house);
}

struct HouseRoster {
    std::array<int, NUM_PLAYER_HOUSES> houses{};
    int count = 0;

    bool contains(int house) const noexcept {
        return std::find(houses.begin(), houses.begin() + count, house) != houses.begin() + count;
    }

    int operator[](int index) const noexcept { return houses[index]; }
};

/// Family editions declare their roster in mod data. Legacy selection and order stay unchanged.
inline HouseRoster selectableHouses(bool tornieFamily, bool customHouseEnabled,
                                   const std::vector<int>& declaredRoster) {
    constexpr std::array<int, NUM_PLAYER_HOUSES> order = {
        HOUSE_ATREIDES, HOUSE_ORDOS, HOUSE_HARKONNEN, HOUSE_MERCENARY,
        HOUSE_FREMEN, HOUSE_SARDAUKAR, HOUSE_NEUTRAL, HOUSE_REBELS,
        HOUSE_CUSTOM, HOUSE_WILDSPADE, HOUSE_KLESHMERSH, HOUSE_THARPIQUE
    };
    HouseRoster result;
    for(const int house : order) {
        const bool available = tornieFamily
            ? std::find(declaredRoster.begin(), declaredRoster.end(), house) != declaredRoster.end()
            : house < NUM_LEGACY_HOUSES || (house == HOUSE_CUSTOM && customHouseEnabled);
        if(available) result.houses[result.count++] = house;
    }
    return result;
}

} // namespace CustomGamePlayerPolicy

#endif // CUSTOMGAMEPLAYERPOLICY_H
