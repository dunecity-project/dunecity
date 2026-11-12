#ifndef DUNECITY_ZONEPOWER_H
#define DUNECITY_ZONEPOWER_H

#include <data.h>

namespace DuneCity {
/// A vacant lot still needs a live generator before it can grow. The legacy
/// rule accepted 0/0; keep that only for games saved before the vacancy rule.
inline bool hasZonePower(int produced, int required, bool unpoweredZonesVacate) {
    return produced >= required && (!unpoweredZonesVacate || produced > 0);
}


/**
 * Per-density power consumption for city zones (R/C/I).
 *
 * Anchors:
 *  - Industrial density-3 = 24, ≈ 2/3 of Heavy Factory (35)
 *  - Residential density-1 = 3, slightly above a Spice Silo (5)
 *  - Commercial sits between R and I
 *
 * Density 0 returns 0 (zone not yet populated). Densities >3 are clamped
 * to the density-3 value — current tile model only uses 0-3.
 *
 * Returns 0 for non-zone itemIDs so callers can treat this as a uniform
 * lookup without a separate isZoneStructure() guard.
 */
inline int getZonePower(int itemID, int density) {
    if (density <= 0) return 0;
    if (density > 3) density = 3;

    static constexpr int kResidential[3] = { 3,  7, 12 };
    static constexpr int kCommercial [3] = { 5, 11, 18 };
    static constexpr int kIndustrial [3] = { 7, 14, 24 };

    switch (itemID) {
        case Structure_ZoneResidential: return kResidential[density - 1];
        case Structure_ZoneCommercial:  return kCommercial [density - 1];
        case Structure_ZoneIndustrial:  return kIndustrial [density - 1];
        default:                        return 0;
    }
}

} // namespace DuneCity

#endif // DUNECITY_ZONEPOWER_H
