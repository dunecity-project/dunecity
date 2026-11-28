#ifndef PARK_DISTRICT_POLICY_H
#define PARK_DISTRICT_POLICY_H

#include <dunecity/CityEffects.h>

#include <algorithm>

// ParkDistricts (QuantBot [Force Concentration] ParkDistricts, default off; docs/formations.md,
// "Parks in the districts"). A Park is 10 credits and one +15 park-terrain source: Micropolis's
// one-pass smoothing turns it into about +7 land value on every tile of its 3x3 terrain block and
// +1 on the four neighbouring blocks (DuneCity::ParkTerrainPolicy). City tax scales with the
// house's average land value, so a park pays where its terrain block holds our R/C lots.
namespace ParkDistrictPolicy {
/// Own R+C lots before a house plants its first park.
constexpr int kMinLots = 4;
/// At most one park (built or queued) per kLotsPerPark R+C lots.
constexpr int kLotsPerPark = 2;
/// A site must add at least this much land value, summed over the R/C land-value blocks it raises.
constexpr int kMinGain = 4;
/// Tiles around a lot searched for park sites: its terrain block and the next one reach this far.
constexpr int kLotReach = 2 * DuneCity::kParkTerrainBlockSize - 1;

inline int parkCap(int lots) { return lots < kMinLots ? 0 : lots / kLotsPerPark; }

/// Land value a park adds to one block, never past the land-value ceiling. A block at the floor (1)
/// gains nothing: its pollution already outweighs its value by an unknown amount, so a park's few
/// points are lost under the clamp (on Stefan's low.dls, LotParks cleared lots for such neighbours and
/// their land value moved +0.1 a lot).
inline int blockGain(int landValue, int marginalGain) {
    if (landValue <= 1) return 0;
    return std::min(std::max(0, DuneCity::kMaxLandValue - landValue), std::max(0, marginalGain));
}

/// Whether another park is worth ordering now.
inline bool wantsPark(int lots, int parks, bool pending, int bestGain) {
    return !pending && parks < parkCap(lots) && bestGain >= kMinGain;
}
} // namespace ParkDistrictPolicy

#endif
