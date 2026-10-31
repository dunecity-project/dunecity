#pragma once

#include <Definitions.h>
#include <cstdint>

// Object-ID phase staggering heals one HP per simulated second without saved state.
inline bool spiceHealingTick(std::uint64_t cycle, std::uint32_t object) {
    constexpr auto interval = (1000 + GAMESPEED_DEFAULT - 1) / GAMESPEED_DEFAULT;
    return cycle % interval == object % interval;
}
