// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <data.h>
#include <cstdint>

namespace radar {
enum Kind : int { BaseExpansion = 0, ExtendedVision = 1, Specialization = 2, Count = 3 };
inline bool valid(int kind) { return kind >= 0 && kind < Count; }
inline int cost(int kind) { return kind == Specialization ? 750 : 500; }
inline int seconds(int kind) { return kind == Specialization ? 60 : 45; }
inline int level(int kind) { return kind == Specialization ? 7 : 4; }
inline bool lightVehicle(int item) {
    switch(item) {
        case Unit_Trike: case Unit_RaiderTrike: case Unit_Quad: case Unit_RaiderQuad:
        case Unit_RocketTrike: case Unit_SonicTrike: case Unit_FlameQuad: return true;
        default: return false;
    }
}
inline bool aircraft(int item) {
    return item == Unit_Carryall || item == Unit_ChemicalCarryall
        || item == Unit_Ornithopter || item == Unit_EliteOrnithopter;
}
inline bool conventionalTank(int item) {
    switch(item) {
        case Unit_Tank: case Unit_SiegeTank: case Unit_EliteSiegeTank: case Unit_ChemicalSiegeTank:
        case Unit_FlameTank: case Unit_RangedTank: case Unit_SlowTank: return true;
        default: return false;
    }
}
inline bool tank(int item) {
    return conventionalTank(item) || item == Unit_Devastator
        || item == Unit_SonicTank || item == Unit_RebelHarvester;
}
inline bool nonHeavyUnit(int item) { return isInfantryUnit(item) || lightVehicle(item) || aircraft(item); }
inline bool scoutOrArtillery(int item) {
    return lightVehicle(item) || item == Unit_Launcher || item == Unit_EliteLauncher
        || item == Unit_SiegeTank || item == Unit_EliteSiegeTank
        || item == Unit_ChemicalSiegeTank || item == Unit_RangedTank;
}
struct State {
    std::uint8_t completed = 0;
    int active = -1;
    int remaining = 0;
};
}
