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

/// VanillaArmyCounterMapScope: thick spice is below 20% of the spice tiles (level 1), or also rock
/// contiguity (3x3 all-rock windows per rock tile) is below 0.45 (level 2).
inline bool thinOrFragmented(int level, const InitialTerrain& terrain) {
    if (level <= 0 || !measured(terrain)) return false;
    if (terrain.thickSpice * 5 < terrain.spice) return true;
    return level >= 2 && terrain.rock > 0 && terrain.rockWindows * 100 < 45 * terrain.rock;
}

/// FocusFireMapScope: at least 192x192 tiles (level 1), or also below 1,000 tiles per original
/// occupied controller (level 2).
inline bool hugeOrCrowded(int level, const InitialTerrain& terrain, int controllers) {
    if (level <= 0 || !measured(terrain)) return false;
    if (terrain.tiles >= int64_t(192) * 192) return true;
    return level >= 2 && controllers > 0 && terrain.tiles < int64_t(1000) * controllers;
}

} // namespace QuantBotMapScopePolicy
