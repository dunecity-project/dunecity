// The Micropolis helicopter/airplane arithmetic and the shipped original art.
//
// The direction and turn tables are checked against the original's own data,
// transcribed from MicropolisCore/MicropolisEngine/src/sprite.cpp rather than
// from our port, so a regression in the port cannot agree with itself.
#include <catch2/catch_test_macros.hpp>

#include <dunecity/CityAircraftPolicy.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/lodepng.h>

#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace DuneCity::CityAircraft;

namespace {

// sprite.cpp:698 doCopterSprite and :803 doAirplaneSprite step tables.
// Index 0 is the inactive frame; 1..8 are the headings.
const int kCopterDx[9] = { 0,  0,  3,  5,  3,  0, -3, -5, -3 };
const int kCopterDy[9] = { 0, -5, -3,  0,  3,  5,  3,  0, -3 };
const int kPlaneDx[12] = { 0,  0,  6, 8, 6, 0, -6, -8, -6, 8, 8, 8 };
const int kPlaneDy[12] = { 0, -8, -6, 0, 6, 8,  6,  0, -6, 0, 0, 0 };

/// sprite.cpp:396 turnTo, transcribed independently of our port.
int originalTurnTo(int p, int d) {
    if (p == d) return p;
    if (p < d) { if (d - p < 4) p++; else p--; }
    else       { if (p - d < 4) p--; else p++; }
    if (p > 8) p = 1;
    if (p < 1) p = 8;
    return p;
}

/// sprite.cpp:477 getDir, transcribed independently of our port.
int originalGetDir(int orgX, int orgY, int desX, int desY, int* absDist) {
    static const int Gdtab[13] = { 0, 3, 2, 1, 3, 4, 5, 7, 6, 5, 7, 8, 1 };
    int dispX = desX - orgX, dispY = desY - orgY, z;
    if (dispX < 0) z = (dispY < 0) ? 11 : 8;
    else           z = (dispY < 0) ? 2 : 5;
    if (dispX < 0) dispX = -dispX;
    if (dispY < 0) dispY = -dispY;
    *absDist = dispX + dispY;
    if (dispX * 2 < dispY) z++;
    else if (dispY * 2 < dispY) z--;
    if (z < 0 || z > 12) z = 0;
    return Gdtab[z];
}

struct Sheet {
    std::vector<unsigned char> rgba;
    unsigned w = 0, h = 0;
    explicit Sheet(const std::string& name) {
        const char* root = std::getenv("DUNE_CITY_SOURCE_DIR");
        REQUIRE(root != nullptr);
        const auto path = std::string(root) + "/imported_sprites/micropolis/aircraft/" + name + ".png";
        REQUIRE(lodepng::decode(rgba, w, h, path) == 0);
    }
    std::vector<unsigned char> cell(int col, int row, int size) const {
        std::vector<unsigned char> out;
        REQUIRE((col + 1) * size <= static_cast<int>(w));
        REQUIRE((row + 1) * size <= static_cast<int>(h));
        for (int y = row * size; y < (row + 1) * size; ++y) {
            auto start = rgba.begin() + (y * w + col * size) * 4;
            out.insert(out.end(), start, start + size * 4);
        }
        return out;
    }
    bool opaquePixels(int col, int row, int size) const {
        const auto pixels = cell(col, row, size);
        for (size_t i = 3; i < pixels.size(); i += 4) if (pixels[i] != 0) return true;
        return false;
    }
};

std::string manifestText() {
    const char* root = std::getenv("DUNE_CITY_SOURCE_DIR");
    REQUIRE(root != nullptr);
    const auto path = std::string(root) + "/imported_sprites/micropolis/aircraft/manifest.json";
    std::vector<unsigned char> bytes;
    REQUIRE(lodepng::load_file(bytes, path) == 0);
    return std::string(bytes.begin(), bytes.end());
}

} // namespace

TEST_CASE("Micropolis turnTo is reproduced for every direction pair", "[city][aircraft]") {
    for (int present = 1; present <= 8; ++present) {
        for (int destination = 1; destination <= 8; ++destination) {
            INFO("present " << present << " destination " << destination);
            const int ported = turnTo(present, destination);
            CHECK(ported == originalTurnTo(present, destination));
            CHECK(ported >= 1);
            CHECK(ported <= 8);
            // One step at a time, and never away from the destination forever:
            // repeating the turn has to arrive within four steps.
            int heading = present;
            int steps = 0;
            while (heading != destination && steps < 8) { heading = turnTo(heading, destination); ++steps; }
            CHECK(heading == destination);
            CHECK(steps <= 4);
        }
    }
}

TEST_CASE("Micropolis getDir is reproduced over a dense displacement grid", "[city][aircraft]") {
    for (int dx = -40; dx <= 40; ++dx) {
        for (int dy = -40; dy <= 40; ++dy) {
            int expectedDistance = 0;
            const int expected = originalGetDir(0, 0, dx, dy, &expectedDistance);
            const auto bearing = getDir(0, 0, dx, dy);
            INFO("dx " << dx << " dy " << dy);
            CHECK(bearing.direction == expected);
            CHECK(bearing.distance == expectedDistance);
        }
    }
    // The vertical axis resolves cleanly, because getDir's only surviving
    // correction is the `dispX * 2 < dispY` one.
    CHECK(getDir(0, 0, 0, -100).direction == 1);   // north
    CHECK(getDir(0, 0, 0, 100).direction == 5);    // south
    // The horizontal axis does not: `dispY < 0` is a strict test, so dy == 0
    // lands in the southern bucket, and the correction that would pull it back
    // is upstream's `dispY * 2 < dispY`, which never holds (sprite.cpp:505,
    // marked "XXX This never holds!!"). Due east therefore reports
    // east-south-east and due west west-south-west in the original too. The
    // port reproduces the bug rather than fixing it, so headings and the
    // helicopter's approach wobble stay identical to Micropolis.
    CHECK(getDir(0, 0, 100, 0).direction == 4);    // east -> south-east
    CHECK(getDir(0, 0, -100, 0).direction == 6);   // west -> south-west
    CHECK(getDir(0, 0, 100, -1).direction == 2);   // a single unit up flips it
    CHECK(getDir(0, 0, -100, -1).direction == 8);
}

TEST_CASE("Direction/angle mapping matches the original step tables", "[city][aircraft]") {
    // A heading's DuneCity angle must point the same way as Micropolis's own
    // per-direction step vector for that heading.
    for (int direction = 1; direction <= 8; ++direction) {
        const ANGLETYPE angle = directionToAngle(direction);
        CHECK(angleToDirection(static_cast<int>(angle)) == direction);

        // DuneCity angles run counter-clockwise from east; y grows downwards,
        // which is also how the Micropolis tables are written.
        const int expectedX = (kCopterDx[direction] > 0) - (kCopterDx[direction] < 0);
        const int expectedY = (kCopterDy[direction] > 0) - (kCopterDy[direction] < 0);
        static const int angleX[NUM_ANGLES] = {  1,  1,  0, -1, -1, -1,  0,  1 };
        static const int angleY[NUM_ANGLES] = {  0, -1, -1, -1,  0,  1,  1,  1 };
        INFO("direction " << direction);
        CHECK(angleX[angle] == expectedX);
        CHECK(angleY[angle] == expectedY);

        // The plane uses the same eight headings as the helicopter.
        CHECK(((kPlaneDx[direction] > 0) - (kPlaneDx[direction] < 0)) == expectedX);
        CHECK(((kPlaneDy[direction] > 0) - (kPlaneDy[direction] < 0)) == expectedY);
    }
    // Every angle is reachable; the mapping is a bijection.
    std::set<int> angles;
    for (int direction = 1; direction <= 8; ++direction) angles.insert(directionToAngle(direction));
    CHECK(angles.size() == 8);
}

TEST_CASE("Take-off runs the original frame sequence exactly once", "[city][aircraft]") {
    // doAirplaneSprite: z-- while z > 8, and `if (z < 9) z = 3`.
    int frame = kAirplaneFirstTakeoffFrame;
    std::vector<int> sequence;
    for (int step = 0; step < 10 && frame > 0; ++step) {
        sequence.push_back(frame);
        frame = nextTakeoffFrame(frame);
    }
    CHECK(sequence == std::vector<int>{11, 10, 9});
    CHECK(frame == 0);
    // Frames 9..11 are the only take-off frames; the cruise heading after the
    // run is east, as in `z = 3`.
    CHECK(nextTakeoffFrame(kAirplaneLastTakeoffFrame) == 0);
    CHECK(directionToAngle(kAirplaneAfterTakeoffDirection) == RIGHT);
    // Plane take-off frames step due east in the original table.
    for (int takeoff = 9; takeoff <= 11; ++takeoff) {
        CHECK(kPlaneDx[takeoff] == kAirplaneStep);
        CHECK(kPlaneDy[takeoff] == 0);
    }
}

TEST_CASE("Heavy-traffic reporting follows the original gate and cooldown", "[city][aircraft]") {
    // doCopterSprite: density > 170, one random draw in eight, and nothing at
    // all while the 200-tick cooldown is running.
    CHECK_FALSE(shouldReportTraffic(kHeavyTrafficThreshold, 0, 0));
    CHECK(shouldReportTraffic(kHeavyTrafficThreshold + 1, 0, 0));
    CHECK(shouldReportTraffic(240, 0, 8));
    int accepted = 0;
    for (uint32_t bits = 0; bits < 64; ++bits) if (shouldReportTraffic(255, 0, bits)) ++accepted;
    CHECK(accepted == 8);                       // exactly one draw in eight
    for (uint32_t bits = 0; bits < 64; ++bits) CHECK_FALSE(shouldReportTraffic(255, 1, bits));
    CHECK(kReportCooldown == 200);
    CHECK(kHeavyTrafficSteerThreshold == 240);  // traffic.cpp's saturated cell
}

TEST_CASE("Off-map detection matches spriteNotInBounds", "[city][aircraft]") {
    const int w = 64, h = 64;
    CHECK_FALSE(notInBounds(0, 0, w, h));
    CHECK_FALSE(notInBounds(w * 16 - 1, h * 16 - 1, w, h));
    CHECK(notInBounds(-1, 0, w, h));
    CHECK(notInBounds(0, -1, w, h));
    CHECK(notInBounds(w * 16, 0, w, h));
    CHECK(notInBounds(0, h * 16, w, h));
}

TEST_CASE("Outside waypoints and the departure route", "[city][aircraft]") {
    const int w = 64, h = 48;

    // doAirplaneSprite draws destinations from [-50, size*16 + 50], so an
    // ordinary cruise waypoint is regularly off the map; that is the original's
    // only way of ending a flight.
    CHECK_FALSE(isOutsideMap(0, 0, w, h));
    CHECK_FALSE(isOutsideMap(w - 1, h - 1, w, h));
    CHECK(isOutsideMap(-1, 0, w, h));
    CHECK(isOutsideMap(0, -1, w, h));
    CHECK(isOutsideMap(w, 0, w, h));
    CHECK(isOutsideMap(0, h, w, h));

    // The departure waypoint must sit outside the map, past the arrival radius
    // so the plane keeps heading out, and past the notInBounds edge so the plane
    // is always retired at the boundary before it could reach the waypoint.
    CHECK(kAirplaneDepartureMarginTiles * kMicropolisUnitsPerTile > kAirplaneArriveDistance);
    CHECK(kAirplaneDepartureMarginTiles * kMicropolisUnitsPerTile > kAirplaneDestinationMargin);

    // Nearest edge, with a deterministic tie order (west, east, north, south)
    // so every peer and every reload derives the same route.
    CHECK(departureWaypoint(3, h / 2, w, h) == Coord(-kAirplaneDepartureMarginTiles, h / 2));
    CHECK(departureWaypoint(w - 4, h / 2, w, h)
          == Coord(w - 1 + kAirplaneDepartureMarginTiles, h / 2));
    CHECK(departureWaypoint(w / 2, 3, w, h) == Coord(w / 2, -kAirplaneDepartureMarginTiles));
    CHECK(departureWaypoint(w / 2, h - 4, w, h)
          == Coord(w / 2, h - 1 + kAirplaneDepartureMarginTiles));
    // A corner is a tie between two edges; west wins, and the same input always
    // gives the same answer.
    CHECK(departureWaypoint(0, 0, w, h) == Coord(-kAirplaneDepartureMarginTiles, 0));
    CHECK(departureWaypoint(0, 0, w, h) == departureWaypoint(0, 0, w, h));

    // Every route leads out, from every tile of a map of each supported scale.
    for (const int size : {51, 64, 128, 192, 384}) {
        for (int tile = 0; tile < size; tile += 7) {
            for (const Coord probe : {Coord(tile, 0), Coord(tile, size - 1), Coord(0, tile),
                                      Coord(size - 1, tile), Coord(tile, size / 2),
                                      Coord(size / 2, tile)}) {
                const Coord out = departureWaypoint(probe.x, probe.y, size, size);
                CHECK(isOutsideMap(out.x, out.y, size, size));
                // Never the (-1, -1) "no destination" sentinel on either axis.
                CHECK(out.x != INVALID_POS);
                CHECK(out.y != INVALID_POS);
            }
        }
    }
}

TEST_CASE("Sprite clock keeps the original 50ms cadence without drift", "[city][aircraft]") {
    CHECK(kMicropolisSpriteIntervalMs == 50);          // sim.c:70 sim_delay
    CHECK(isSpriteTick(0));
    int ticks = 0;
    // One in-game minute: 60 000 ms must be 1200 sprite ticks, not 1199/1201.
    const uint32_t cycles = 60000 / GAMESPEED_DEFAULT;
    for (uint32_t cycle = 1; cycle <= cycles; ++cycle) if (isSpriteTick(cycle)) ++ticks;
    CHECK(ticks == 60000 / kMicropolisSpriteIntervalMs);
    // spriteCycle is monotonic and never skips a value, so `% 4` and `% 5`
    // gates fire on every fourth and fifth tick exactly as upstream.
    uint32_t previous = spriteCycle(0);
    for (uint32_t cycle = 1; cycle <= cycles; ++cycle) {
        const uint32_t current = spriteCycle(cycle);
        CHECK(current - previous <= 1u);
        previous = current;
    }
}

TEST_CASE("Shipped ObjectData speeds equal the original sprite steps", "[city][aircraft][assets]") {
    const char* root = std::getenv("DUNE_CITY_SOURCE_DIR");
    REQUIRE(root != nullptr);
    INIFile data(std::string(root) + "/config/ObjectData.ini.default");
    // ObjectData stores tiles-per-cycle style world speed; the original covers
    // one axis step of its table per 50 ms sprite tick.
    const int helicopterMilli = static_cast<int>(
        data.getDoubleValue("Ambient Helicopter", "MaxSpeed", 0.0) * 1000.0 + 0.5);
    const int airplaneMilli = static_cast<int>(
        data.getDoubleValue("Ambient Airplane", "MaxSpeed", 0.0) * 1000.0 + 0.5);
    CHECK(helicopterMilli == worldSpeedMilli(kHelicopterStep, TILESIZE));
    CHECK(airplaneMilli == worldSpeedMilli(kAirplaneStep, TILESIZE));
    CHECK(helicopterMilli == 6400);
    CHECK(airplaneMilli == 10240);
    // Civilian aircraft never leave infantry behind when they are shot down.
    CHECK(data.getIntValue("Ambient Helicopter", "InfSpawnProp", -1) == 0);
    CHECK(data.getIntValue("Ambient Airplane", "InfSpawnProp", -1) == 0);
}

TEST_CASE("Shipped Micropolis aircraft art has every original frame", "[city][aircraft][assets]") {
    Sheet helicopter("city_helicopter");
    REQUIRE(helicopter.w == 8 * 32);
    REQUIRE(helicopter.h == 1 * 32);

    std::set<std::vector<unsigned char>> copterFrames;
    for (int angle = 0; angle < 8; ++angle) {
        INFO("helicopter angle " << angle);
        CHECK(helicopter.opaquePixels(angle, 0, 32));
        copterFrames.insert(helicopter.cell(angle, 0, 32));
    }
    CHECK(copterFrames.size() == 8);   // eight distinct original headings

    Sheet airplane("city_airplane");
    REQUIRE(airplane.w == 8 * 48);
    REQUIRE(airplane.h == 4 * 48);

    std::set<std::vector<unsigned char>> cruiseFrames;
    for (int angle = 0; angle < 8; ++angle) {
        INFO("airplane angle " << angle);
        CHECK(airplane.opaquePixels(angle, 0, 48));
        cruiseFrames.insert(airplane.cell(angle, 0, 48));
    }
    CHECK(cruiseFrames.size() == 8);

    // Rows 1..3 are take-off frames 9, 10 and 11: real, distinct artwork, and
    // repeated across the columns so angle indexing is harmless.
    std::set<std::vector<unsigned char>> takeoffFrames;
    for (int row = 1; row <= 3; ++row) {
        const auto reference = airplane.cell(0, row, 48);
        CHECK(airplane.opaquePixels(0, row, 48));
        takeoffFrames.insert(reference);
        for (int angle = 1; angle < 8; ++angle) CHECK(airplane.cell(angle, row, 48) == reference);
        CHECK(reference != airplane.cell(0, 0, 48));
    }
    CHECK(takeoffFrames.size() == 3);

    // Both sheets stay inside the renderer's zoom-3 texture budget.
    CHECK(helicopter.w * 3 <= 2048);
    CHECK(airplane.w * 3 <= 2048);
    CHECK(airplane.h * 3 <= 2048);
}

TEST_CASE("Aircraft art provenance records the original XPM sources", "[city][aircraft][assets]") {
    const std::string manifest = manifestText();
    // Every original frame file has to be named, so a silently redrawn sheet
    // cannot pass as imported art.
    for (int index = 0; index <= 7; ++index)
        CHECK(manifest.find("obj2-" + std::to_string(index) + ".xpm") != std::string::npos);
    for (int index = 0; index <= 10; ++index)
        CHECK(manifest.find("obj3-" + std::to_string(index) + ".xpm") != std::string::npos);
    CHECK(manifest.find("scripts/import-micropolis-aircraft.py") != std::string::npos);
    CHECK(manifest.find("GPL-3.0-or-later") != std::string::npos);
}
