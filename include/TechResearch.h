#ifndef DUNECITY_TECH_RESEARCH_H
#define DUNECITY_TECH_RESEARCH_H

#include <Definitions.h>
#include <algorithm>
#include <cstdint>
#include <limits>

namespace TechResearch {

enum class Pace : std::uint32_t { Fast = 0, Normal = 1, Slow = 2, VerySlow = 3 };
constexpr std::uint32_t kReferenceArea = 129u * 96u;

constexpr bool isValid(std::uint32_t value) { return value <= static_cast<std::uint32_t>(Pace::VerySlow); }
constexpr Pace sanitize(int value) {
    return isValid(static_cast<std::uint32_t>(value)) ? static_cast<Pace>(value) : Pace::Normal;
}

constexpr std::uint32_t mapArea(int width, int height) {
    if(width <= 0 || height <= 0) return 0;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        std::uint64_t(width) * std::uint64_t(height), std::numeric_limits<std::uint32_t>::max()));
}

// Normal preserves the old prices/times through the smallest Civ Wars map. Each
// extra reference area adds 2x: max(A0, 2*A-A0)/A0. Area is frozen at initial map
// load, not inferred from income, surviving houses, buildings or explored tiles.
constexpr std::uint64_t areaNumerator(std::uint32_t area) {
    return area > kReferenceArea ? 2 * std::uint64_t(area) - kReferenceArea : kReferenceArea;
}

constexpr unsigned paceHalfUnits(Pace pace) {
    switch(pace) {
        case Pace::Fast: return 1;
        case Pace::Normal: return 2;
        case Pace::Slow: return 4;
        case Pace::VerySlow: return 8;
    }
    return 2; // Local configuration is sanitized; serialized unknown values are refused.
}

constexpr int scaled(int value, std::uint32_t area, Pace pace) {
    const std::uint64_t denominator = 2 * kReferenceArea;
    const std::uint64_t numerator = std::uint64_t(value) * areaNumerator(area) * paceHalfUnits(pace);
    return static_cast<int>(std::min<std::uint64_t>(
        (numerator + denominator - 1) / denominator, std::numeric_limits<int>::max()));
}

constexpr int cost(int target, std::uint32_t area, Pace pace) {
    return target >= 2 && target <= 9 ? scaled(300 * target, area, pace) : 0;
}

constexpr int cycles(int target, std::uint32_t area, Pace pace) {
    return target >= 2 && target <= 9 ? scaled(MILLI2CYCLES((30 + 10 * target) * 1000), area, pace) : 0;
}

} // namespace TechResearch
#endif
