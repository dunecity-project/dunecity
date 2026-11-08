#pragma once

#include <cstdint>

namespace StarportBuyingPolicy {
// Level zero is deliberately the original strict discount predicate.
constexpr bool admitted(int market, int factory, int level) {
    return market > 0 && factory > 0 && (level == 0 ? market < factory
        : int64_t(market) * 100 <= int64_t(factory) * (level == 1 ? 80 : 100));
}
constexpr bool preserveCash(int cash, int orderPrice, int factoryPrice, int level) {
    return level > 0 && int64_t(cash) - orderPrice < int64_t(factoryPrice) * level;
}
}
