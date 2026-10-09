/* sprite.cpp
 *
 * Micropolis, Unix Version.  This game was released for the Unix platform
 * in or about 1990 and has been modified for inclusion in the One Laptop
 * Per Child program.  Copyright (C) 1989 - 2007 Electronic Arts Inc.  If
 * you need assistance with this program, you may contact:
 *   http://wiki.laptop.org/go/Micropolis  or email  micropolis@laptop.org.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.  You should have received a
 * copy of the GNU General Public License along with this program.  If
 * not, see <http://www.gnu.org/licenses/>.
 *
 *             ADDITIONAL TERMS per GNU GPL Section 7
 *
 * No trademark or publicity rights are granted.  This license does NOT
 * give you any right, title or interest in the trademark SimCity or any
 * other Electronic Arts trademark.  You may not distribute any
 * modification of this program using the trademark SimCity or claim any
 * affliation or association with Electronic Arts Inc. or its employees.
 *
 * Any propagation or conveyance of this program must include this
 * copyright notice and these terms.
 *
 * If you convey this program (or any modifications of it) and assume
 * contractual liability for the program to recipients of it, you agree
 * to indemnify Electronic Arts for any liability that those contractual
 * assumptions impose on Electronic Arts.
 *
 * You may not misrepresent the origins of this program; modified
 * versions of the program must be marked as such and not identified as
 * the original program.
 *
 * This disclaimer supplements the one included in the General Public
 * License.  TO THE FULLEST EXTENT PERMISSIBLE UNDER APPLICABLE LAW, THIS
 * PROGRAM IS PROVIDED TO YOU "AS IS," WITH ALL FAULTS, WITHOUT WARRANTY
 * OF ANY KIND, AND YOUR USE IS AT YOUR SOLE RISK.  THE ENTIRE RISK OF
 * SATISFACTORY QUALITY AND PERFORMANCE RESIDES WITH YOU.  ELECTRONIC ARTS
 * DISCLAIMS ANY AND ALL EXPRESS, IMPLIED OR STATUTORY WARRANTIES,
 * INCLUDING IMPLIED WARRANTIES OF MERCHANTABILITY, SATISFACTORY QUALITY,
 * FITNESS FOR A PARTICULAR PURPOSE, NONINFRINGEMENT OF THIRD PARTY
 * RIGHTS, AND WARRANTIES (IF ANY) ARISING FROM A COURSE OF DEALING,
 * USAGE, OR TRADE PRACTICE.  ELECTRONIC ARTS DOES NOT WARRANT AGAINST
 * INTERFERENCE WITH YOUR ENJOYMENT OF THE PROGRAM; THAT THE PROGRAM WILL
 * MEET YOUR REQUIREMENTS; THAT OPERATION OF THE PROGRAM WILL BE
 * UNINTERRUPTED OR ERROR-FREE, OR THAT THE PROGRAM WILL BE COMPATIBLE
 * WITH THIRD PARTY SOFTWARE OR THAT ANY ERRORS IN THE PROGRAM WILL BE
 * CORRECTED.  NO ORAL OR WRITTEN ADVICE PROVIDED BY ELECTRONIC ARTS OR
 * ANY AUTHORIZED REPRESENTATIVE SHALL CREATE A WARRANTY.  SOME
 * JURISDICTIONS DO NOT ALLOW THE EXCLUSION OF OR LIMITATIONS ON IMPLIED
 * WARRANTIES OR THE LIMITATIONS ON THE APPLICABLE STATUTORY RIGHTS OF A
 * CONSUMER, SO SOME OR ALL OF THE ABOVE EXCLUSIONS AND LIMITATIONS MAY
 * NOT APPLY TO YOU.
 */

// Modified for DuneCity: deterministic clock/coordinates and policy helpers.

#ifndef DUNECITY_CITYAIRCRAFTPOLICY_H
#define DUNECITY_CITYAIRCRAFTPOLICY_H

/**
 * Micropolis helicopter and airplane behaviour, ported verbatim where the
 * original is pure arithmetic.
 *
 * Reference: Micropolis (GPL release) MicropolisCore/MicropolisEngine/src
 *   sprite.cpp:396   Micropolis::turnTo
 *   sprite.cpp:459   Micropolis::spriteNotInBounds
 *   sprite.cpp:477   Micropolis::getDir
 *   sprite.cpp:695   Micropolis::doCopterSprite
 *   sprite.cpp:800   Micropolis::doAirplaneSprite
 *   sprite.cpp:1969  Micropolis::generateCopter / :1985 generatePlane
 *   simulate.cpp:1651 Micropolis::doAirport
 *   traffic.cpp:185  heavy-traffic steering of the helicopter
 *
 * Micropolis works in 1/16-tile units and numbers directions 1..8 clockwise
 * from north; frame 0 means "sprite inactive".  DuneCity works in
 * TILESIZE-unit world coordinates and ANGLETYPE (RIGHT..RIGHTDOWN,
 * counter-clockwise from east).  Everything in this header is integer-only so
 * both the simulation and the tests stay bit-identical across platforms.
 */

#include <DataTypes.h>
#include <Definitions.h>
#include <fixmath/FixPoint.h>

#include <cstdint>

namespace DuneCity::CityAircraft {

/// Micropolis direction, 1..8 (1 = north, 3 = east, 5 = south, 7 = west).
/// 0 is the original's "no direction" result from getDir's clamp.
using Direction = int;

/// sprite.cpp:396 Micropolis::turnTo — one step of the original turn.
inline Direction turnTo(int present, int destination) {
    int p = present;
    const int d = destination;
    if (p == d) return p;
    if (p < d) {
        if (d - p < 4) p++; else p--;
    } else {
        if (p - d < 4) p--; else p++;
    }
    if (p > 8) p = 1;
    if (p < 1) p = 8;
    return p;
}

/// sprite.cpp:477 Micropolis::getDir — direction and Manhattan distance from
/// origin to destination.  The original stores the distance in the member
/// `absDist`; we return it so nothing depends on call ordering (the upstream
/// `@todo` notes that reading a stale absDist is a real bug there).
///
/// The `dispY * 2 < dispY` branch in the original never holds (upstream marks
/// it `XXX This never holds!!`); it is reproduced as written so the direction
/// table is identical, including its bias towards the vertical axis.
struct Bearing {
    Direction direction;
    int distance;
};

inline Bearing getDir(int orgX, int orgY, int desX, int desY) {
    static const int Gdtab[13] = { 0, 3, 2, 1, 3, 4, 5, 7, 6, 5, 7, 8, 1 };

    int dispX = desX - orgX;
    int dispY = desY - orgY;
    int z;

    if (dispX < 0) {
        z = (dispY < 0) ? 11 : 8;
    } else {
        z = (dispY < 0) ? 2 : 5;
    }

    dispX = dispX < 0 ? -dispX : dispX;
    dispY = dispY < 0 ? -dispY : dispY;
    const int absDist = dispX + dispY;

    if (dispX * 2 < dispY) {
        z++;
    } else if (dispY * 2 < dispY) {  // never true upstream; kept for fidelity
        z--;
    }

    if (z < 0 || z > 12) z = 0;
    return { Gdtab[z], absDist };
}

/// Micropolis direction 1..8 -> Dune Legacy ANGLETYPE.
/// Derived from sprite.cpp's step tables, e.g. the helicopter's
/// CDx/CDy[1] = (0,-5) is north and CDx/CDy[3] = (5,0) is east.
inline ANGLETYPE directionToAngle(Direction direction) {
    switch (direction) {
        case 1: return UP;
        case 2: return RIGHTUP;
        case 3: return RIGHT;
        case 4: return RIGHTDOWN;
        case 5: return DOWN;
        case 6: return LEFTDOWN;
        case 7: return LEFT;
        case 8: return LEFTUP;
        default: return UP;
    }
}

/// Inverse of directionToAngle; the sprite sheets are packed in this order.
inline Direction angleToDirection(int angle) {
    switch (angle) {
        case RIGHT:     return 3;
        case RIGHTUP:   return 2;
        case UP:        return 1;
        case LEFTUP:    return 8;
        case LEFT:      return 7;
        case LEFTDOWN:  return 6;
        case DOWN:      return 5;
        case RIGHTDOWN: return 4;
        default:        return 1;
    }
}

// ---------------------------------------------------------------------------
// Original constants.  Distances are Micropolis 1/16-tile units; convert with
// toWorld()/toMicropolis() rather than hard-coding a tile size at the call site.
// ---------------------------------------------------------------------------

/// doCopterSprite: `sprite->count` starts at 1500 (makeSprite, sprite.cpp:233)
/// and counts down one per sprite cycle before the copter goes home.
inline constexpr int kHelicopterPatrolCount = 1500;
/// doCopterSprite: `absDist < 16` — one tile — means "arrived at destination".
inline constexpr int kHelicopterArriveDistance = 16;
/// doCopterSprite: `absDist < 30` from home means "landed" (sprite ends).
inline constexpr int kHelicopterLandDistance = 30;
/// doCopterSprite: traffic density above this is worth reporting.
/// (Upstream comment: "Don changed from 160 to 170 to shut the #$%#$% thing up!")
inline constexpr int kHeavyTrafficThreshold = 170;
/// traffic.cpp:185 — a saturated traffic cell is what the helicopter is
/// steered towards.  addToTrafficDensityMap caps density at 240, so this is
/// the "as bad as it gets" value, not a second report threshold.
inline constexpr int kHeavyTrafficSteerThreshold = 240;
/// DuneCity bound, not an original constant: the original has one city filling
/// the whole map, so a map-wide scan is a city scan.  Here the helicopter looks
/// for traffic within this many tiles of its own airport, which keeps the scan
/// over its owner's city and bounded on large shared maps.
inline constexpr int kSeekRadiusTiles = 24;
/// DuneCity bound: sprite ticks between traffic scans (one second of game time
/// at the original 50 ms sprite interval).
inline constexpr int kSeekInterval = 20;
/// DuneCity bound, not an original constant: the original airplane only ends
/// by leaving the map, which its random destinations make likely but do not
/// guarantee.  Four times the helicopter's patrol (five minutes of game time)
/// retires a flight that keeps drawing destinations back over the map, so no
/// aircraft can live forever.
inline constexpr int kAirplaneMaxTicks = 4 * kHelicopterPatrolCount;
/// doCopterSprite: `sprite->soundCount = 200` after a report.
inline constexpr int kReportCooldown = 200;
/// doAirplaneSprite: `absDist < 50` picks a new destination.
inline constexpr int kAirplaneArriveDistance = 50;
/// doAirplaneSprite: destinations are drawn from [-50, size*16 + 50], inclusive.
inline constexpr int kAirplaneDestinationMargin = 50;
/// doCopterSprite turns on `(spriteCycle & 3) == 0`; doAirplaneSprite on
/// `(spriteCycle % 5) == 0`.
inline constexpr int kHelicopterTurnInterval = 4;
inline constexpr int kAirplaneTurnInterval = 5;
/// doAirplaneSprite: frames 11 -> 10 -> 9 then straight to 3 (east).
inline constexpr int kAirplaneFirstTakeoffFrame = 11;
inline constexpr int kAirplaneLastTakeoffFrame = 9;
inline constexpr int kAirplaneAfterTakeoffDirection = 3;
/// simulate.cpp doAirport: getRandom(5) == 0 launches a plane, otherwise
/// getRandom(12) == 0 launches the helicopter. getRandom's bound is inclusive:
/// these give one chance in six, then one in thirteen, respectively.
inline constexpr int kAirplaneRandomMax = 5;
inline constexpr int kHelicopterRandomMax = 12;

/// Micropolis 1/16-tile units per tile.
inline constexpr int kMicropolisUnitsPerTile = 16;

/// Micropolis axis step per sprite tick, from the CDx/CDy tables in
/// doCopterSprite (5) and doAirplaneSprite (8).
inline constexpr int kHelicopterStep = 5;
inline constexpr int kAirplaneStep = 8;
inline constexpr int kHelicopterDiagonalStep = 3;
inline constexpr int kAirplaneDiagonalStep = 6;

/// AirUnit moves with cos/sin. Adjust its diagonal magnitude to retain the
/// original CDx/CDy components (3,3 for the helicopter; 6,6 for the airplane)
/// while keeping the normal tile, spatial-grid and combat lifecycle.
inline FixPoint headingSpeed(FixPoint axisSpeed, int angle, int axisStep, int diagonalStep) {
    if((angle & 1) == 0) return axisSpeed;
    return (axisSpeed * diagonalStep / axisStep) / FixPoint::cos(FixPt_PI / 4);
}

/// sim.c:70 `int sim_delay = 50;` — the original runs one MoveObjects() pass
/// every 50 ms, so a "sprite tick" is 50 ms of simulated time.  DuneCity
/// counts 16 ms game cycles (GAMESPEED_DEFAULT), so one sprite tick is 3.125
/// game cycles; the division below keeps that exact instead of rounding it to
/// 3 or 4 and drifting away from the original speeds.
inline constexpr int kMicropolisSpriteIntervalMs = 50;

/// The original sprite-loop counter, expressed on DuneCity's clock.
inline uint32_t spriteCycle(uint32_t gameCycle) {
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(gameCycle) * GAMESPEED_DEFAULT) / kMicropolisSpriteIntervalMs);
}

/// True on the game cycles where the original would have moved its sprites.
inline bool isSpriteTick(uint32_t gameCycle) {
    return gameCycle == 0 || spriteCycle(gameCycle) != spriteCycle(gameCycle - 1);
}

/// The original cruise speed in DuneCity world units per game cycle, scaled by
/// 1000 so it stays integral: one axis step per sprite tick.  This is what
/// ObjectData.ini's MaxSpeed has to say for the aircraft to cover the ground
/// the original covers (6.400 and 10.240 at TILESIZE 64).
inline constexpr int worldSpeedMilli(int micropolisStep, int tileSize) {
    return micropolisStep * tileSize * 1000 * GAMESPEED_DEFAULT
         / (kMicropolisUnitsPerTile * kMicropolisSpriteIntervalMs);
}

/// Convert Micropolis sprite units to DuneCity world units.
inline int toWorld(int micropolisUnits, int tileSize) {
    return micropolisUnits * tileSize / kMicropolisUnitsPerTile;
}

/// Convert DuneCity world units to Micropolis sprite units.
inline int toMicropolis(int worldUnits, int tileSize) {
    return worldUnits * kMicropolisUnitsPerTile / tileSize;
}

/// doAirplaneSprite take-off step: 11 -> 10 -> 9 -> (cruise east).
/// Returns the next frame, or 0 once the run is over and normal heading
/// steering takes back over.
inline int nextTakeoffFrame(int frame) {
    if (frame <= kAirplaneLastTakeoffFrame) return 0;
    const int next = frame - 1;
    return (next < kAirplaneLastTakeoffFrame) ? 0 : next;
}

/// doCopterSprite report gate: `trafficDensityMap.worldGet(x,y) > 170 &&
/// (getRandom16() & 7) == 0`, and only while the report cooldown has expired.
inline bool shouldReportTraffic(int density, int soundCount, uint32_t randomBits) {
    return soundCount == 0 && density > kHeavyTrafficThreshold && (randomBits & 7u) == 0u;
}

/// sprite.cpp:459 spriteNotInBounds, in Micropolis units.
inline bool notInBounds(int x, int y, int mapWidth, int mapHeight) {
    return x < 0 || y < 0
        || x >= mapWidth * kMicropolisUnitsPerTile
        || y >= mapHeight * kMicropolisUnitsPerTile;
}

} // namespace DuneCity::CityAircraft

#endif // DUNECITY_CITYAIRCRAFTPOLICY_H
