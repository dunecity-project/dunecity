#ifndef SPICEVARIANTS_H
#define SPICEVARIANTS_H

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace SpiceVariants {

// Fixed integer arithmetic keeps the map identical on every multiplayer platform.
inline uint32_t mix(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    return value ^ (value >> 16);
}

// Only normal spice is replaced. Thickness, terrain geometry and blooms stay intact.
// Each pocket grows over at most seven connected spice tiles; conversion is capped
// at 15% even if a custom map requests more. This never uses the game's RNG.
inline std::string apply(std::string terrain, int width, int percentage, uint32_t seed, int version = 1) {
    if(width <= 0 || terrain.size() % static_cast<size_t>(width) != 0) return terrain;
    std::vector<size_t> candidates;
    for(size_t index = 0; index < terrain.size(); ++index) {
        if(terrain[index] == '~' || terrain[index] == '+') candidates.push_back(index);
    }
    const size_t target = candidates.size() * static_cast<size_t>(std::clamp(percentage, 0, 15)) / 100;
    if(target == 0) return terrain;
    std::sort(candidates.begin(), candidates.end(), [seed](size_t left, size_t right) {
        const auto leftRank = mix(seed ^ static_cast<uint32_t>(left));
        const auto rightRank = mix(seed ^ static_cast<uint32_t>(right));
        return leftRank == rightRank ? left < right : leftRank < rightRank;
    });

    size_t converted = 0;
    size_t pocketNumber = 0;
    const int height = static_cast<int>(terrain.size() / width);
    for(const auto center : candidates) {
        if(converted == target) break;
        if(terrain[center] != '~' && terrain[center] != '+') continue;
        const uint32_t rank = mix(seed ^ static_cast<uint32_t>(center));
        const bool green = ((rank >> 8) & 1U) != 0;
        const size_t color = (seed + pocketNumber++) % 4;
        const size_t pocketSize = version >= 2
            ? std::min<size_t>(3 + rank % 5, std::max<size_t>(1, target / 4))
            : 3 + rank % 5;
        std::vector<size_t> pending{center};
        size_t pocketCount = 0;
        for(size_t cursor = 0; cursor < pending.size() && pocketCount < pocketSize && converted < target; ++cursor) {
            const auto index = pending[cursor];
            const char original = terrain[index];
            if(original != '~' && original != '+') continue;
            terrain[index] = version >= 2
                ? (original == '~' ? "rglw"[color] : "RGLW"[color])
                : (original == '~' ? (green ? 'g' : 'r') : (green ? 'G' : 'R'));
            ++converted;
            ++pocketCount;
            const int x = static_cast<int>(index % width);
            const int y = static_cast<int>(index / width);
            const int centerX = static_cast<int>(center % width);
            const int centerY = static_cast<int>(center / width);
            const int dx[] = {1, 0, -1, 0};
            const int dy[] = {0, 1, 0, -1};
            for(int step = 0; step < 4; ++step) {
                const int direction = (step + static_cast<int>((rank >> 4) % 4)) % 4;
                const int nx = x + dx[direction];
                const int ny = y + dy[direction];
                if(nx >= 0 && nx < width && ny >= 0 && ny < height
                   && std::abs(nx - centerX) + std::abs(ny - centerY) <= 2) {
                    pending.push_back(static_cast<size_t>(ny) * width + nx);
                }
            }
        }
    }
    return terrain;
}

} // namespace SpiceVariants

#endif
