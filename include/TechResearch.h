#ifndef DUNECITY_TECH_RESEARCH_H
#define DUNECITY_TECH_RESEARCH_H

#include <Definitions.h>
#include <cstdint>

namespace TechResearch {

enum class Pace : std::uint32_t { Fast = 0, Normal = 1, Slow = 2, VerySlow = 3 };

constexpr bool isValid(std::uint32_t value) { return value <= static_cast<std::uint32_t>(Pace::VerySlow); }
constexpr Pace sanitize(int value) {
    return isValid(static_cast<std::uint32_t>(value)) ? static_cast<Pace>(value) : Pace::Normal;
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

constexpr int scaled(int value, Pace pace) {
    return value * static_cast<int>(paceHalfUnits(pace)) / 2;
}

constexpr int cost(int target, Pace pace) {
    return target >= 2 && target <= 9 ? scaled(1000 * (target - 1), pace) : 0;
}

constexpr int cycles(int target, Pace pace) {
    return target >= 2 && target <= 9 ? scaled(MILLI2CYCLES(target * 60 * 1000), pace) : 0;
}

} // namespace TechResearch
#endif
