#ifndef DUNECITY_CITYCONSTANTS_H
#define DUNECITY_CITYCONSTANTS_H

#include <Command.h>
#include <data.h>

#include <algorithm>
#include <cstdint>

namespace DuneCity {

enum class ZoneType : uint8_t {
    None        = 0,
    Residential = 1,
    Commercial  = 2,
    Industrial  = 3
};

constexpr int NUM_CITY_PHASES    = 16;
constexpr int CITY_PHASE_INTERVAL = 1;
constexpr int16_t MAX_TAX_RATE   = 20;

enum CityToolType : uint32_t {
    CityTool_Bulldoze  = 0,
    CityTool_Road      = 1,
    CityTool_PowerLine = 2
};

inline bool shouldEnableLoadedCityEffects(bool hasCitySimulation) {
    return hasCitySimulation;
}

inline bool isTrafficConnector(bool road, Uint32 item) {
    return road || item == Structure_RocketTurret;
}

inline bool isCityZoneStructure(int itemID) {
    return itemID == Structure_ZoneResidential
        || itemID == Structure_ZoneCommercial
        || itemID == Structure_ZoneIndustrial;
}

inline bool isCityOnlyStructure(int itemID) {
    return isCityZoneStructure(itemID)
        || itemID == Structure_Road
        || itemID == Structure_PowerLine
        || itemID == Structure_NuclearPlant
        || itemID == Structure_PoliceStation
        || itemID == Structure_Stadium
        || itemID == Structure_Airport;
}

// Construction reach follows owned tiles, regardless of their foundation.
// Enemy roads, like enemy concrete, may be covered inside our normal build
// range but never grant a foothold or extend that range themselves.
inline bool isConstructionAnchor(int tileOwner, int builderHouse) {
    return tileOwner == builderHouse;
}

inline bool isCityBuildableTerrain(uint32_t terrain) {
    return terrain == Terrain_Rock || terrain == Terrain_Slab;
}

// Zones may spill onto sand: every tile must be rock, slab, sand or dunes,
// and the footprint must keep at least one rock or slab tile so the lot stays
// anchored to the buildable substrate. Spice, blooms and mountains never
// qualify.
inline bool isCityZoneTerrain(uint32_t terrain) {
    return terrain == Terrain_Rock || terrain == Terrain_Slab
        || terrain == Terrain_Sand || terrain == Terrain_Dunes;
}

inline int getCityBuildTime(int itemID, int configuredBuildTime) {
    // Only roads bypass normal construction timing. Zones use their configured
    // duration like other buildings, respecting active house/mod data.
    if (itemID == Structure_Road) return 1;
    return std::max(1, configuredBuildTime);
}

struct CityTilePlacementState {
    bool supportedTerrain = false;
    bool isMountain = false;
    bool hasGroundObject = false;
    bool hasCityZone = false;
    bool hasRoad = false;
};

struct CityBuildCommandDescriptor {
    int commandId = CMD_NONE;
    uint32_t parameter = 0;
};

inline CityTilePlacementState makeCityTilePlacementState(
        bool isRock, bool isMountain, bool hasGroundObject,
        bool hasCityZone, bool hasRoad) {
    return { isRock && !isMountain, isMountain, hasGroundObject, hasCityZone, hasRoad };
}

inline bool canPlaceRoad(const CityTilePlacementState& s) {
    return s.supportedTerrain && !s.isMountain && !s.hasGroundObject
           && !s.hasCityZone && !s.hasRoad;
}

inline bool applyRoadPlacement(CityTilePlacementState& state) {
    if (!canPlaceRoad(state)) {
        return false;
    }

    state.hasRoad = true;
    return true;
}

inline CityBuildCommandDescriptor getRoadPlacementCommandDescriptor() {
    return CityBuildCommandDescriptor{CMD_CITY_TOOL, static_cast<uint32_t>(CityTool_Road)};
}

inline const char* getRoadPlacementModeLabel() {
    return "Road tool";
}

} // namespace DuneCity

#endif // DUNECITY_CITYCONSTANTS_H
