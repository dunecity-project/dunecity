// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <ObjectData.h>
#include <array>

class Random;

/// Tornie technology exchanges leave house identity, ownership and unit catalogues intact.
class ChaosMode {
public:
    using TechnologyTable = ObjectData::ObjectDataStruct[Num_ItemID][NUM_HOUSES];
    ChaosMode() { reset(); }
    void reset();
    void generate(TechnologyTable& objectData, bool requested, Random& random);
    void save(OutputStream& stream) const;
    void load(InputStream& stream);
    bool isEnabled() const { return enabled; }
    int getTechnologyHouse(int house, int structure) const;
    static bool includesStructure(int item);

    static constexpr std::array<int, 16> Structures{{
        Structure_Barracks, Structure_WOR, Structure_LightFactory,
        Structure_HeavyFactory, Structure_HighTechFactory, Structure_StarPort,
        Structure_Worfinery, Structure_LoveFactory, Structure_ChaosFactory,
        Structure_Palace, Structure_GunTurret, Structure_RocketTurret,
        Structure_Scoutpost, Structure_Flamepost, Structure_Chemipost,
        Structure_WindTrap
    }};

private:
    bool enabled = false;
    std::array<std::array<int, Structures.size()>, NUM_HOUSES> donors{};
};
