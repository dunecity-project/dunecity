#ifndef DUNECITY_TRAFFICSIMULATION_H
#define DUNECITY_TRAFFICSIMULATION_H

#include <dunecity/CityConstants.h>
#include <dunecity/CityTrafficPolicy.h>

namespace DuneCity {
class CitySimulation;

// Deterministic BFS connectivity for existing 2x2 zone perimeters. Density
// sampling/decay follow Micropolis; connectivity remains our bounded BFS.
// An instance answers for one unchanged map: init() starts it, and the road and
// destination tests it memoises per tile hold until the next init(). Callers
// run their trips with no road, structure or unit change in between.
class TrafficSimulation {
public:
    TrafficSimulation();
    void init(CitySimulation* sim);
    // 1 = connected, 0 = no destination, -1 = no perimeter road.
    int makeTraffic(int x, int y, ZoneType destZone, uint32_t day = 0);
    using Pos = CityTraffic::Point;
    // Ordered successful route including start and destination, empty on failure.
    const std::vector<Pos>& getLastPath() const { return routeFinder_.route(); }
private:
    bool findPerimeterRoad(int zoneX, int zoneY, int& roadX, int& roadY) const;
    bool tryDrive(int startX, int startY, ZoneType destZone, unsigned directionOffset);
    bool isRoad(int x, int y) const;
    bool driveDone(int x, int y, ZoneType destZone) const;
    uint8_t neighbourRoles(int x, int y) const;
    CityTraffic::RouteFinder routeFinder_;
    // Per tile, row-major: isRoadConnection() (-1 = not read yet) and the roles of the
    // structures on its four sides (bit 0x80 = read).
    mutable std::vector<int8_t> roadMemo_;
    mutable std::vector<uint8_t> roleMemo_;
    static constexpr int DX[4] = { 0, 1, 0, -1 };
    static constexpr int DY[4] = { -1, 0, 1, 0 };
};
} // namespace DuneCity
#endif
