#ifndef ENHANCED_ANIMATION_TIMELINE_H
#define ENHANCED_ANIMATION_TIMELINE_H

#include <SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

inline std::vector<Uint32> parseEnhancedFrameDurations(const std::string& text, int frames) {
    std::vector<Uint32> durations;
    if(text.empty()) return durations;
    uint64_t total = 0;
    size_t begin = 0;
    do {
        const size_t end = text.find(',', begin);
        const std::string token = text.substr(begin, end == std::string::npos ? end : end - begin);
        if(token.empty() || token.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("Invalid per-frame duration");
        const auto value = std::stoull(token);
        if(value == 0 || value > 60000) throw std::invalid_argument("Per-frame duration out of range");
        total += value;
        if(total > std::numeric_limits<Uint32>::max()) throw std::invalid_argument("Animation is too long");
        durations.push_back(static_cast<Uint32>(value));
        if(static_cast<int>(durations.size()) > frames) throw std::invalid_argument("Too many per-frame durations");
        if(end == std::string::npos) break;
        begin = end + 1;
    } while(true);
    if(static_cast<int>(durations.size()) != frames) throw std::invalid_argument("Per-frame duration count mismatch");
    return durations;
}

inline Uint32 enhancedAnimationDuration(int frames, int frameMs, const std::vector<Uint32>& durations) {
    if(durations.empty()) return static_cast<Uint32>(frames) * static_cast<Uint32>(frameMs);
    Uint32 result = 0;
    for(const auto duration : durations) result += duration;
    return result;
}

inline Uint32 enhancedAnimationFrame(Uint32 elapsed, int frames, int frameMs,
                                     bool loop, const std::vector<Uint32>& durations) {
    const auto total = enhancedAnimationDuration(frames, frameMs, durations);
    if(total == 0 || frames <= 0) return 0;
    if(loop) elapsed %= total;
    else if(elapsed >= total) return static_cast<Uint32>(frames - 1);
    if(durations.empty()) return elapsed / static_cast<Uint32>(frameMs);
    for(size_t i = 0; i < durations.size(); ++i) {
        if(elapsed < durations[i]) return static_cast<Uint32>(i);
        elapsed -= durations[i];
    }
    return static_cast<Uint32>(frames - 1);
}

#endif
