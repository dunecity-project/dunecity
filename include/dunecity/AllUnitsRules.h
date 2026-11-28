#ifndef DUNECITY_ALL_UNITS_RULES_H
#define DUNECITY_ALL_UNITS_RULES_H

#include <data.h>
#include <array>

namespace DuneCity::AllUnitsRules {
// Ordinary purchased units only. Palace summons, superweapons, ambient aircraft
// and content-family-exclusive units are not factory or barracks products.
inline constexpr std::array<int, 17> kUnits{
    Unit_Soldier, Unit_Infantry, Unit_Trooper, Unit_Troopers,
    Unit_Trike, Unit_RaiderTrike, Unit_Quad,
    Unit_Tank, Unit_SiegeTank, Unit_Launcher,
    Unit_Devastator, Unit_SonicTank, Unit_Deviator,
    Unit_Harvester, Unit_MCV, Unit_Carryall, Unit_Ornithopter
};

constexpr int builderFor(int unit) {
    switch(unit) {
        case Unit_Soldier: case Unit_Infantry:
            return Structure_Barracks;
        case Unit_Trooper: case Unit_Troopers:
            return Structure_WOR;
        case Unit_Trike: case Unit_RaiderTrike: case Unit_Quad:
            return Structure_LightFactory;
        case Unit_Tank: case Unit_SiegeTank: case Unit_Launcher:
        case Unit_Devastator: case Unit_SonicTank: case Unit_Deviator:
        case Unit_Harvester: case Unit_MCV:
            return Structure_HeavyFactory;
        case Unit_Carryall: case Unit_Ornithopter:
            return Structure_HighTechFactory;
        default:
            return ItemID_Invalid;
    }
}
} // namespace DuneCity::AllUnitsRules

#endif
