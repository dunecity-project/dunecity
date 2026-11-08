#ifndef QUANTBOT_FIELD_EXPANSION_POLICY_H
#define QUANTBOT_FIELD_EXPANSION_POLICY_H

#include <algorithm>
#include <cstdint>
#include <vector>

namespace QuantBotFieldExpansionPolicy {
inline constexpr int kLocalRadius = 16;
inline constexpr int kFieldRadius = 12;
inline bool due(int threshold, int localSpice, int refineries, int64_t sharedReachableSpice) {
    return threshold > 0 && refineries > 0 && localSpice < int64_t(threshold) * refineries
        && sharedReachableSpice >= int64_t(threshold) * (refineries + 1);
}

// Permanent ground connectivity, not a straight-line claim across mountains.
// Moving traffic is intentionally not a permanent obstacle.
inline std::vector<uint8_t> reachable(int width, int height, const std::vector<uint8_t>& passable,
                                       const std::vector<int>& starts) {
    std::vector<uint8_t> seen(passable.size(), 0);
    if (width <= 0 || height <= 0 || passable.size() != size_t(width) * height) return seen;
    std::vector<int> flood;
    flood.reserve(passable.size());
    for (int start : starts) if (start >= 0 && start < int(passable.size()) && passable[start] && !seen[start]) {
        seen[start] = 1; flood.push_back(start);
    }
    for (size_t i = 0; i < flood.size(); ++i) {
        const int p = flood[i], x = p % width, y = p / width;
        auto visit = [&](int n) { if (passable[n] && !seen[n]) { seen[n] = 1; flood.push_back(n); } };
        if (x > 0) visit(p - 1);
        if (x + 1 < width) visit(p + 1);
        if (y > 0) visit(p - width);
        if (y + 1 < height) visit(p + width);
    }
    return seen;
}
}
#endif
