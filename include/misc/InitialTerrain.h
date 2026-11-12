#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// Static terrain counts of an original explicit map grid (the [MAP] rows of an INI scenario),
/// measured once from the saved scenario (GameInitSettings::getInitialTerrain). Spice tiles are
/// exactly those the initial spice density counts and thick spice is their thick variants; rock is
/// '%' and mountain '@' only. Rock contiguity is rockWindows / rock.
struct InitialTerrain {
    bool known = false;       ///< false: seeded, missing or malformed scenario
    int64_t tiles = 0;        ///< width x height
    int64_t spice = 0;
    int64_t thickSpice = 0;
    int64_t mountain = 0;
    int64_t rock = 0;
    int64_t rockWindows = 0;  ///< 3x3 windows whose nine tiles are all rock
};

/// Counts \a rows, each exactly \a width map characters (the caller validates the grid).
inline InitialTerrain countInitialTerrain(int width, const std::vector<std::string>& rows) {
    InitialTerrain terrain;
    terrain.known = true;
    terrain.tiles = int64_t(width) * int64_t(rows.size());
    for (const auto& row : rows) {
        for (const char tile : row) {
            switch (tile) {
                case '+': case 'G': case 'R': case 'L': case 'W': ++terrain.thickSpice; [[fallthrough]];
                case '~': case 'g': case 'r': case 'l': case 'w': ++terrain.spice; break;
                case '@': ++terrain.mountain; break;
                case '%': ++terrain.rock; break;
                default: break;
            }
        }
    }
    for (size_t y = 2; y < rows.size(); ++y) {
        for (int x = 2; x < width; ++x) {
            bool allRock = true;
            for (size_t dy = 0; dy < 3 && allRock; ++dy)
                for (int dx = 0; dx < 3 && allRock; ++dx)
                    allRock = rows[y - dy][size_t(x - dx)] == '%';
            terrain.rockWindows += allRock ? 1 : 0;
        }
    }
    return terrain;
}
