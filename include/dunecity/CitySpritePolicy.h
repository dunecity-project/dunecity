#ifndef DUNECITY_CITYSPRITEPOLICY_H
#define DUNECITY_CITYSPRITEPOLICY_H

#include <algorithm>
#include <cstdint>
#include <dunecity/CityConstants.h>
#include <Definitions.h>
#include <dunecity/ResidentialPopulation.h>

namespace DuneCity::CitySprites {
// Must match scripts/build-city-atlases.py. Visual-only: no random-generator
// calls, saved fields, scans, or changes to population/density/footprints.
constexpr int residentialColumns = 15;
constexpr int residentialRows = 8; // 29 models per value tier, packed into two rows
constexpr int commercialColumns = 6;
constexpr int industrialColumns = 5;
constexpr int industrialRows = 18; // 2 value tiers * (static + 8 phases)
constexpr int specialFrames = 9;
constexpr int roadRows = 9;
/// Park atlas: WOODS2..WOODS5 (Micropolis tiles 40-43), then the four FOUNTAIN frames (840-843).
constexpr int parkFrames = 8;
constexpr int parkFountainFrame = 4;
constexpr uint32_t frameCycles = MILLI2CYCLES(128);

inline uint32_t siteSeed(int x, int y) {
    // Mix both coordinates; modulo patterns must not synchronize on the road grid.
    uint32_t seed = static_cast<uint32_t>(x) * 0x9e3779b9u
                  ^ static_cast<uint32_t>(y) * 0x85ebca6bu;
    seed ^= seed >> 16;
    seed *= 0x7feb352du;
    return seed ^ (seed >> 15);
}

inline int phase(uint32_t cycle, uint32_t seed = 0) {
    return static_cast<int>((cycle / frameCycles + (seed & 7u)) & 7u);
}

inline int zoneColumns(ZoneType type) {
    if (type == ZoneType::Residential) return residentialColumns;
    if (type == ZoneType::Commercial) return commercialColumns;
    return industrialColumns;
}

inline int zoneRows(ZoneType type) {
    return type == ZoneType::Residential ? residentialRows : type == ZoneType::Industrial ? industrialRows : 4;
}

inline int zoneFrame(ZoneType type, int density, int valueTier,
                     int x, int y, uint32_t cycle, bool powered, int residentialPopulation = -1) {
    density = std::clamp(density, 0, 3);
    const uint32_t seed = siteSeed(x, y);
    int model = 0;
    if (type == ZoneType::Residential) {
        const int pop = residentialPopulation < 0 ? ResidentialPopulation::fromDensity(density)
            : ResidentialPopulation::normalize(residentialPopulation);
        if (pop > 0 && pop <= 8) model = 1 + (seed % 3)*8 + pop-1;
        else if (pop >= 16) model = 25 + (pop-16)/8;
        return model + std::clamp(valueTier, 0, 3) * residentialColumns * 2;
    }
    if (type == ZoneType::Commercial) {
        if (density == 1) model = 1 + seed % 2;
        if (density == 2) model = 3 + seed % 2;
        if (density == 3) model = 5;
        return model + std::clamp(valueTier, 0, 3) * commercialColumns;
    }
    if (density == 1) model = 1 + seed % 2;
    if (density >= 2) model = density + 1;
    const int animation = powered && density > 0 ? 1 + phase(cycle, seed) : 0;
    return model + (std::clamp(valueTier, 0, 1) * 9 + animation) * industrialColumns;
}

inline int roadRow(int traffic, uint32_t cycle, bool visible = true) {
    // Micropolis doRoad: density>>6, subtract one when >1.
    // 0..63 no cars, 64..191 light, 192..255 heavy. Hidden roads reveal
    // no live traffic and draw their static road surface under the fog.
    if (!visible || traffic < 64) return 0;
    return (traffic < 192 ? 1 : 5) + phase(cycle) % 4;
}

inline int poweredFrame(uint32_t cycle, bool powered) {
    return powered ? 1 + phase(cycle) : 0;
}

inline int stadiumFrame(uint32_t cycle, int x, int y, bool powered) {
    // Original full/empty cycle: an eight-tick match in each 32-tick window.
    // This presentation schedule uses sim seconds, offset by the site, so it
    // freezes on pause and remains stable across saves/replays.
    const bool playing = ((cycle / MILLI2CYCLES(1000) + siteSeed(x, y)) & 31u) < 8;
    return powered && playing ? 1 + phase(cycle) : 0;
}

/// Micropolis putDownPark picks getRandom(4): WOODS2..WOODS5, or the animated fountain for 4.
/// The site picks the variant here, so it draws no random number and survives save/load.
inline int parkFrame(uint32_t cycle, int x, int y) {
    const uint32_t seed = siteSeed(x, y);
    const int variant = static_cast<int>(seed % 5u);
    return variant < parkFountainFrame ? variant : parkFountainFrame + (phase(cycle, seed) & 3);
}
}
#endif
