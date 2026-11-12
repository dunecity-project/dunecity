#pragma once

#include <misc/InitialTerrain.h>

#include <cstdint>

// Map-scoped repairs of control-map losses (docs/ai-scenario-matrix.md, "Map-scoped repairs"):
// fixed preregistered thresholds on the immutable original explicit grid, never a map name, seed
// or mission. Level 0 is OFF. Unknown and initially spiceless maps are never scoped, so a scope
// cannot make a candidate pass the no-spice controls by switching itself off there.
namespace QuantBotMapScopePolicy {

inline bool measured(const InitialTerrain& terrain) {
    return terrain.known && terrain.spice > 0 && terrain.tiles > 0;
}

/// ClosingMountainScope: mountain is at least 8% (level 1) or 6% (level 2) of the tiles.
inline bool mountainRich(int level, const InitialTerrain& terrain) {
    if (level <= 0 || !measured(terrain)) return false;
    return terrain.mountain * 100 >= (level == 1 ? 8 : 6) * terrain.tiles;
}

/// ConvergeChokepointScope: exactly 2 original occupied controller rows, area at most 4,096 tiles,
/// and mountain at least 8% of the measured, initially spice-bearing map.
inline bool smallMountainDuel(const InitialTerrain& terrain, int controllers) {
    return controllers == 2 && terrain.tiles <= int64_t(64) * 64 && mountainRich(1, terrain);
}

/// VanillaArmyCounterMapScope: thick spice is below 20% of the spice tiles (level 1), or also rock
/// contiguity (3x3 all-rock windows per rock tile) is below 0.45 (level 2).
inline bool thinOrFragmented(int level, const InitialTerrain& terrain) {
    if (level <= 0 || !measured(terrain)) return false;
    if (terrain.thickSpice * 5 < terrain.spice) return true;
    return level >= 2 && terrain.rock > 0 && terrain.rockWindows * 100 < 45 * terrain.rock;
}

/// OpeningOpenMapScope: at least 4 original occupied controller rows, mountain below 1.1%,
/// and thin spice or fragmented rock (thinOrFragmented level 2); measured, spice-bearing only.
inline bool openFlatCrowd(const InitialTerrain& terrain, int controllers) {
    return controllers >= 4 && terrain.mountain * 1000 < 11 * terrain.tiles
        && thinOrFragmented(2, terrain);
}

/// FocusFireMapScope: at least 192x192 tiles (level 1), or also below 1,000 tiles per original
/// occupied controller (level 2).
inline bool hugeOrCrowded(int level, const InitialTerrain& terrain, int controllers) {
    if (level <= 0 || !measured(terrain)) return false;
    if (terrain.tiles >= int64_t(192) * 192) return true;
    return level >= 2 && controllers > 0 && terrain.tiles < int64_t(1000) * controllers;
}

/// Levels 3/4 of VanillaArmyCounterMapScope and FocusFireMapScope scope by the original roster
/// instead of the map: at least 3 (level 3: FFA4 and larger) or 5 (level 4: FFA6) opposing sides.
inline bool manySides(int level, int opposingSides) {
    return level >= 3 && opposingSides >= (level == 3 ? 3 : 5);
}

/// Level 5 adds map level 1 to a roster level (control-map repairs): FocusFireMapScope=5 is level 3
/// or at least 192x192 tiles; VanillaArmyCounterMapScope=5 is level 4 or thick spice below 20% of
/// the spice tiles. Unknown and spiceless maps keep the roster level alone.
inline bool focusFireLevel5(const InitialTerrain& terrain, int opposingSides) {
    return manySides(3, opposingSides) || hugeOrCrowded(1, terrain, 0);
}

inline bool armyCountersLevel5(const InitialTerrain& terrain, int opposingSides) {
    return manySides(4, opposingSides) || thinOrFragmented(1, terrain);
}

/// Distinct teams other than \a ownTeam among the original occupied controller rows
/// (GameInitSettings at game start, never survivors): 1v1 and 2v2 have 1, FFA4 3, FFA6 5.
template <class Houses>
int opposingSides(const Houses& houses, int ownTeam) {
    int sides = 0;
    for (auto row = houses.begin(); row != houses.end(); ++row) {
        if (row->playerInfoList.empty() || row->team == ownTeam) continue;
        bool counted = false;
        for (auto prior = houses.begin(); prior != row && !counted; ++prior)
            counted = !prior->playerInfoList.empty() && prior->team == row->team;
        if (!counted) ++sides;
    }
    return sides;
}

} // namespace QuantBotMapScopePolicy
