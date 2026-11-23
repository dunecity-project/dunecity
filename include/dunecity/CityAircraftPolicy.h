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

#include <cstdlib>
#include <DataTypes.h>
#include <Definitions.h>
#include <fixmath/FixPoint.h>

#include <algorithm>
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

/// True when a waypoint lies off the map, i.e. it is a leaving waypoint.
/// doAirplaneSprite draws destinations from `[-50, size*16 + 50]`, so an
/// ordinary cruise waypoint is regularly outside; that is how the original
/// plane eventually crosses an edge and ends.
inline bool isOutsideMap(int tileX, int tileY, int mapWidth, int mapHeight) {
    return tileX < 0 || tileY < 0 || tileX >= mapWidth || tileY >= mapHeight;
}

/// DuneCity bound, not an original constant: how far outside the map the
/// departure waypoint is placed, in tiles.  It has to be further out than
/// kAirplaneArriveDistance (50 units = 3.125 tiles) so the plane keeps heading
/// outward instead of treating the waypoint as reached and turning back, and
/// further out than the notInBounds() edge, so the plane is always retired at
/// the boundary before it could ever arrive.
inline constexpr int kAirplaneDepartureMarginTiles = 4;

/// The original airplane has no landing and no timeout: `doAirplaneSprite` ends
/// a flight only at sprite.cpp:856, when spriteNotInBounds() becomes true.
/// When DuneCity's flight budget (kAirplaneMaxTicks) runs out, the plane is
/// therefore routed straight out through its nearest edge instead of being
/// removed over the city.  Deterministic, tie order included, so every peer and
/// every reload derives the same route from the same position.
inline Coord departureWaypoint(int tileX, int tileY, int mapWidth, int mapHeight) {
    const int west  = tileX + 1;
    const int east  = mapWidth - tileX;
    const int north = tileY + 1;
    const int south = mapHeight - tileY;
    const int nearest = std::min(std::min(west, east), std::min(north, south));

    if (nearest == west)  return Coord(-kAirplaneDepartureMarginTiles, tileY);
    if (nearest == east)  return Coord(mapWidth - 1 + kAirplaneDepartureMarginTiles, tileY);
    if (nearest == north) return Coord(tileX, -kAirplaneDepartureMarginTiles);
    return Coord(tileX, mapHeight - 1 + kAirplaneDepartureMarginTiles);
}

// ---------------------------------------------------------------------------
// DuneCity airplane steering (Stefan, 2026-10-22: "stop the plane from turning
// eratically it should move in straight lines mostly").
//
// The airplane no longer steers with turnTo(getDir()).  getDir's table never
// returns east or west - its `dispY * 2 < dispY` correction never holds - so
// for any waypoint less than ~63 degrees off the horizontal it answers north-east
// or south-east (north-west / south-west going west) depending on which side of
// the plane's line the waypoint is. Every turn step crossed that line, so the
// plane flew a zig-zag of 45-degree legs 2.5 tiles long: 0.18 heading changes per
// tile on fixed legs and 0.37 over a whole flight (city_aircraft_probe, before).
// The helicopter keeps the original steering; its legs are short city hops.
//
// The plane now holds a continuous heading (ANGLETYPE eighths of a turn, FixPoint),
// turns on the original clock by at most the original 45-degree step, and stops
// on the exact bearing to its waypoint, then flies that straight line until it
// arrives: it holds its course while the course already passes close enough to
// the waypoint to arrive. It is drawn from sixteen headings.
// ---------------------------------------------------------------------------

/// Largest turn per turn step, in eighths: the original turnTo step of 45 degrees,
/// so the turning circle, and with it reaching every waypoint, is the original's.
inline constexpr int kAirplaneMaxTurn = 1;

/// Re-aim only when the waypoint is more than 1/16 of an eighth (2.8 degrees) off
/// the nose, so fixed-point rounding along a straight leg never turns the plane.
inline FixPoint airplaneAimTolerance() { return FixPoint(1) / 16; }

/// Hold course while the waypoint is ahead and the straight course passes within
/// this many Micropolis units of it - half the arrival distance, so holding on
/// always arrives. Without it, the fixed-point drift of a long leg (a fraction of
/// a tile) grows past the aim tolerance in the last few tiles and the plane makes
/// one last small turn, as often the other way as its first.
inline constexpr int kAirplaneHoldCrossTrack = kAirplaneArriveDistance / 2;

/// One turn step from `heading` towards `bearing` (both ANGLETYPE eighths, [0, 8)),
/// `distance` from the waypoint (world units), holding course while the course
/// misses it by at most `holdCrossTrack` (world units). Within one step it turns
/// straight onto the bearing; a waypoint exactly behind turns left
/// (counter-clockwise), deterministically.
inline FixPoint airplaneSteer(FixPoint heading, FixPoint bearing, FixPoint distance, FixPoint holdCrossTrack) {
    FixPoint error = bearing - heading;
    while (error > 4) error -= 8;
    while (error <= -4) error += 8;
    const FixPoint offCourse = FixPoint::abs(error);
    if (offCourse <= airplaneAimTolerance()) return heading;
    if (offCourse < 2 && distance * FixPoint::sin(offCourse * FixPt_PI / 4) <= holdCrossTrack) return heading;
    FixPoint next = offCourse <= kAirplaneMaxTurn
        ? bearing
        : heading + (error > 0 ? FixPoint(kAirplaneMaxTurn) : FixPoint(-kAirplaneMaxTurn));
    while (next < 0) next += 8;
    while (next >= 8) next -= 8;
    return next;
}

/// The airplane sheet: eight columns in ANGLETYPE order. Row 0 is the eight
/// original cruise headings, row 1 the eight in-between headings (22.5 degrees
/// counter-clockwise of the column's), rows 2..4 take-off frames 9, 10 and 11.
inline constexpr int kAirplaneHeadingFrames = 16;
inline constexpr int kAirplaneSheetRows = 5;

/// Nearest of the sixteen drawn headings, 0 = east, counter-clockwise.
inline int airplaneHeadingFrame(FixPoint heading) {
    return (heading * 2).lround() % kAirplaneHeadingFrames;
}
inline int airplaneCruiseColumn(int headingFrame) { return headingFrame / 2; }
inline int airplaneCruiseRow(int headingFrame) { return headingFrame & 1; }
inline int airplaneTakeoffRow(int takeoffFrame) { return takeoffFrame - kAirplaneLastTakeoffFrame + 2; }

/// Speed along a drawn heading: the original table vectors on the eight original
/// headings (headingSpeed), the mean of the axis and diagonal magnitudes between them.
inline FixPoint airplaneHeadingSpeed(FixPoint axisSpeed, int headingFrame) {
    const FixPoint diagonal = headingSpeed(axisSpeed, RIGHTUP, kAirplaneStep, kAirplaneDiagonalStep);
    switch (headingFrame % 4) {
        case 0:  return axisSpeed;
        case 2:  return diagonal;
        default: return (axisSpeed + diagonal) / 2;
    }
}

// ---------------------------------------------------------------------------
// DuneCity airplane bombing (Stefan, 2026-11-22: "make planes drop bombs as they
// fly over enemy buildings strong enough bombs to destroy some buildings").
// Match rule GameOptionsClass::cityPlaneBombing. Not Micropolis behaviour.
//
// The plane keeps its course: it bombs only what lies under it. Its bomb sight is
// the point kBombSightDistance ahead; each time that point enters a tile on which
// an enemy building stands, and a bomb is on board, it drops one (none for a
// building the bombs already falling will destroy). So a building gets one bomb
// per tile of it that the course crosses.
// The numbers come from the Dune City mod's ObjectData.ini:
// a bomb does kBombDamage = 100, so two bombs take a Wind Trap, a zone, a turret
// or a Heavy Factory (200) and a Spice Silo (150), while a Construction Yard (400)
// needs four, a Refinery (450) or Radar (500) five and a Palace (1000) ten. A full
// load of kBombLoad = 6 is 600 damage: a run can take up to three 200-point
// buildings and never a Construction Yard or anything stronger on its own. The
// load comes back one bomb per kBombRearmTicks (8 s), a full load in 48 s, so a
// five-minute flight carries at most 6 + 37 bombs.
// ---------------------------------------------------------------------------

/// Bombs a plane carries; it launches with a full load.
inline constexpr int kBombLoad = 6;
/// Damage of one bomb to the building it lands on (units next to it take splash).
inline constexpr int kBombDamage = 100;
/// Sprite ticks to regain one bomb (8 s of game time at 50 ms a tick).
inline constexpr int kBombRearmTicks = 160;
// The fall (Stefan, 2026-11-23: "the planes own bombs shouldn't hurt it, and they should
// take a while to drop ... so that the plane has already passed the bombs"). A bomb is
// released towards the bomb sight, kBombSightDistance = 3 tiles ahead of the plane, and
// takes kBombFallCycles = 75 game cycles (1.2 s) to get there: 2.56 world units a cycle,
// a quarter of the plane's 10.24, so it is seen dropping back from the plane. In those
// 75 cycles the plane flies 12 tiles: when the bomb goes off the plane is 9 tiles past
// it, against a blast that reaches units one tile from the impact. The bomb lands on
// the tile that was under the sight when it was released, 0.3 s before the plane itself
// passed over it. Whatever the geometry, a bomb never damages an aircraft (Map::damage).

/// How far ahead of the plane the bomb lands, in Micropolis units (3 tiles).
inline constexpr int kBombSightDistance = 48;
/// Game cycles a bomb takes from release to impact (1.2 s).
inline constexpr int kBombFallCycles = 75;
/// Bomb speed in world units per game cycle at TILESIZE 64: the sight distance over the
/// fall time.
inline FixPoint bombSpeed() {
    return FixPoint(kBombSightDistance * 64 / kMicropolisUnitsPerTile) / kBombFallCycles;
}

/// One sprite tick of the bomb rack: returns the load after rearming. `timer`
/// counts ticks towards the next bomb and rests at 0 while the rack is full.
inline int rearmBombs(int bombs, int& timer, int load, int rearmTicks) {
    if(bombs >= load) { timer = 0; return load; }
    if(++timer < rearmTicks) return bombs;
    timer = 0;
    return bombs + 1;
}
inline int rearmBombs(int bombs, int& timer) { return rearmBombs(bombs, timer, kBombLoad, kBombRearmTicks); }

// ---------------------------------------------------------------------------
// The stick (Stefan, 2026-11-23: "they should drop bombs like a b-52 does as they fly
// over"). Match rule GameOptionsClass::cityPlaneCarpetBombing, with cityPlaneBombing.
//
// The bomb (Stefan, 2026-11-23, after playing the first stick of 60-point bombs two a
// tile with the Ornithopter rocket's explosion: "make a new bomb. the current
// ornithopter one is too small - i want a big explosion. drop less frequently them but
// they should do more damage. a direct hit in the middle of a residential for example
// should destroy it and 2 hits should be taking out other buildings"):
// - kStickDamage = 250 to the building the bomb lands on. One hit destroys every
//   200-point building (zones, Wind Trap, turrets, Heavy Factory, Repair Yard, Silo,
//   Wall); two destroy everything up to 500 (Barracks, Light Factory, Construction
//   Yard, High-Tech Factory, House IX, Police Station, WOR, Refinery, Airport, Radar,
//   Research Lab, Stadium, Starport); a Nuclear Plant (750) takes three and a Palace
//   (1000) four.
// - One bomb every 12.5 game cycles (kStickPhasePeriod / kStickPhaseStep): two tiles of
//   flight on an axis heading, a quarter of the first stick's rate.
// - A stick starts with a bomb as soon as the next bomb would splash an enemy building or
//   unit (kStickStartDamage, below). It carries on for kStickTailBombs more bombs after the last
//   building (so it runs on across roads and gaps of up to two tiles in step); it never
//   falls on a tile that holds a building of the plane's own team.
// - Its impact is Explosion_Bomb: the large building explosion drawn four tiles wide,
//   with the large explosion sound; the falling bomb is the Death Hand's rocket picture.
// The rack does not empty (Stefan, 2026-11-23: "planes racks don't empty"): there is
// no load and no rearm time under the stick rule. With a load, six bombs or twenty-four,
// a plane in his games had nothing left for 85 to 89% of the time it was over an enemy
// building (HANDOVER.md, 2026-11-23).
// ---------------------------------------------------------------------------

/// Damage of one stick bomb to the building it lands on.
inline constexpr int kStickDamage = 250;
/// Bombs the stick still releases after the last enemy building under the sight.
inline constexpr int kStickTailBombs = 1;
/// The stick's clock: kStickPhaseStep is added every game cycle and a bomb is due each
/// time kStickPhasePeriod is reached: one bomb per 12.5 cycles, 128 world units (two
/// tiles) of an axis heading's 10.24 a cycle.
inline constexpr int kStickPhaseStep = 2;
inline constexpr int kStickPhasePeriod = 25;
/// Enemy ground units start and carry a stick as buildings do (Stefan, 2026-11-23: "also get
/// planes to drop bombs on enemy units as well"). Against units the heavy bomb's blast reaches
/// kStickUnitBlastRadius, a tile and a half, the size its explosion is drawn: the full
/// kStickDamage within half a tile of the impact, half of it to a tile, a quarter to a tile and
/// a half (stickUnitDamage). A unit keeps moving for the 1.2 seconds the bomb falls, so a
/// blast of half a tile, the aimed bomb's, would nearly always miss it. Buildings still take
/// damage only when the bomb lands on them.
inline constexpr int kStickUnitBlastTiles2 = 3;   // in half tiles
inline constexpr int stickUnitDamage(int damage, int distance, int tileSize) {
    return damage >> (distance / (tileSize / 2));
}
/// Splash and the start of a stick (Stefan, 2026-11-23: "the plane should drop bombs when it will
/// splash enemy units or buildings with them, also it starts dropping bombs slighly too last when
/// aproaching a building"). A bomb that misses a building of another team by up to a tile and a
/// half now splashes it with kStickBuildingSplashPercent of what a unit there would take: 125
/// within half a tile of its footprint, 62 to a tile, 31 beyond (a direct hit is still 250). The
/// bomber's own side's buildings take no splash, as they take no bomb. A stick starts with a bomb
/// the cycle the next bomb would do at least kStickStartDamage to an enemy building or ground
/// unit, which is half a tile before the bomb sight reaches a building, and carries on while a
/// bomb would do at least kStickCarryDamage. (It used to wait three cycles after the sight had
/// entered the first building, so the first bomb fell half a tile inside it.)
inline constexpr int kStickBuildingSplashPercent = 50;
inline constexpr int kStickStartDamage = 125;
inline constexpr int kStickCarryDamage = 62;
/// What a heavy bomb of `damage` does to a building whose footprint is `distance` world units
/// from the impact (0: the bomb landed on it).
inline constexpr int stickBuildingDamage(int damage, int distance, int tileSize, int splashPercent) {
    return distance <= 0 ? damage : stickUnitDamage(damage, distance, tileSize) * splashPercent / 100;
}
/// kStickBuildingSplashPercent, or the experiment-only DUNECITY_TEST_BOMB_BUILDING_SPLASH_PCT
/// (0 to 100, read once per process, never a match rule).
inline int stickBuildingSplashPercent() {
    static const int percent = [] {
        const char* text = std::getenv("DUNECITY_TEST_BOMB_BUILDING_SPLASH_PCT");
        const int value = text != nullptr ? std::atoi(text) : -1;
        return value >= 0 && value <= 100 ? value : kStickBuildingSplashPercent;
    }();
    return percent;
}

// ---------------------------------------------------------------------------
// Bombing runs (match rule GameOptionsClass::cityPlaneBombingRuns). Each leg of the
// flight is aimed at a living building of another team, drawn evenly from all of
// them, and ends kRunOvershootTiles beyond it on the same line, so the plane crosses
// the building and whatever stands around it before it turns for the next one.
// ---------------------------------------------------------------------------

inline constexpr int kRunOvershootTiles = 12;

// ---------------------------------------------------------------------------
// Buying a plane (Stefan, 2026-11-23: "allow the ai to spawn planes for 1500
// credits"). Match rule GameOptionsClass::cityPlanePurchase. A powered Airport
// launches a plane at once for kPlanePrice while its house has fewer than
// kMaxPlanesPerHouse in the air (Stefan, 2026-11-23: "i should be able to buy more
// planes, up to 10"). There is no wait between purchases for a player: the first
// version sold one plane a minute per Airport, and with flights of half a minute that
// held him to one plane in the air. Nothing waits on kPurchaseCooldownMs any more: an
// Airport still starts that clock at each purchase and keeps it in the save, and the
// probes read it to see a purchase.
// The Airport's free roll is unchanged: it still only launches for a house with no free plane.
//
// A computer player (Stefan, 2026-11-23: "AI buys freely too", then "ai should figure out
// when it makes sense to buy") buys on its own planes' record, purchasePays(): the credit
// value its planes' bombs have destroyed so far (the house's combat record of the type:
// price x share of hit points removed, buildings and units) against the price of every
// flight that has ended (shot down or flown off the map). The free plane flies first and
// gives the evidence: no purchase before one flight has ended (kPurchaseEvidenceFlights; the
// free plane now comes only every five minutes, so three would mean a quarter of an hour), and
// then only while the average flight has destroyed at least what a plane costs. Against a
// city whose turrets bring planes down early the record stays under the price and it stops
// buying; against an open city it buys up to the ten-plane limit. QuantBot's reserves and
// cash floor still apply (QuantBot::buyCityAirplanes).
// ---------------------------------------------------------------------------

inline constexpr int kPlanePrice = 1500;
inline constexpr int kMaxPlanesPerHouse = 10;
inline constexpr int kPurchaseCooldownMs = 60000;
/// The free plane's clock: two minutes (Stefan, 2026-11-23: "reduce the free plane from 5 mins
/// to 2 mins"). It was the Ordos Palace's five (the time to make a Saboteur), and before that a
/// one-in-six roll every five seconds, about half a minute.
inline constexpr int kFreePlaneMs = 120000;
/// Flights of its own that must have ended before a computer player judges whether planes pay.
inline constexpr int kPurchaseEvidenceFlights = 1;
/// True when a house's planes have so far destroyed at least their price per ended flight.
inline constexpr bool purchasePays(int64_t destroyedCredits, int flightsEnded) {
    return flightsEnded >= kPurchaseEvidenceFlights && destroyedCredits >= int64_t(kPlanePrice) * flightsEnded;
}

// The plane's performance score (Stefan, 2026-11-23: "also include planes in the performance
// scores so we know how cost effective it is compared to other units. think about how we could
// include it in the ratio for when to buy them as well"). It is scored as the army's unit types
// are (UnitMixPolicy::performanceScore): the credit value its bombs and its crash have destroyed
// (the house's combat record of the type, in milli credits) over the price of every flight that
// has ended plus one price of prior, times a million. x1.0 is a plane that destroys what it
// costs. A computer player buys while the plane's score is at least its army's own (all its
// unit types' value destroyed per credit of units lost), held between kPlaneBuyScoreFloor (x0.5)
// and kPlaneBuyScoreCeiling (x1.0): a credit goes to planes when it does at least what it does
// in the army, never asking more of a plane than paying for itself and never accepting less
// than half. The Stats window shows both numbers.
inline constexpr int64_t kPlaneBuyScoreFloor = 500000;
inline constexpr int64_t kPlaneBuyScoreCeiling = 1000000;
inline constexpr int64_t planePerformanceScore(int64_t rewardMilli, int flightsEnded) {
    return (rewardMilli < 0 ? 0 : rewardMilli) * 1000000
        / ((int64_t(flightsEnded < 0 ? 0 : flightsEnded) + 1) * kPlanePrice * 1000);
}
inline constexpr int64_t planeBuyScore(int64_t armyScore) {
    return armyScore <= 0 || armyScore > kPlaneBuyScoreCeiling ? kPlaneBuyScoreCeiling
         : armyScore < kPlaneBuyScoreFloor ? kPlaneBuyScoreFloor : armyScore;
}
inline constexpr bool planeWorthBuying(int64_t rewardMilli, int flightsEnded, int64_t armyScore) {
    return flightsEnded >= kPurchaseEvidenceFlights
        && planePerformanceScore(rewardMilli, flightsEnded) >= planeBuyScore(armyScore);
}

// Heaps of money (Stefan, 2026-11-23: "if the ai has heaps and heaps of money then spawning 10
// could be useful to break up the enemy base"; "no don't cap the planes. I want planes to act as
// a counter to a massively built city where the game becomes a stalemate"). Against turrets that
// fire, single planes stop paying and purchasePays() ends the buying. A computer player with
// kRichSurplus credits beyond its reserves and cash floor buys all the same, one plane per
// Airport and build pass, for as long as it stays that rich; the ten planes in the air are its
// only limit. On Metropolis, a million credits a house and 928 Rocket Turrets, that was 819
// planes and 455 buildings in six minutes: slow, costly, and the thing that moves a stalemate.
inline constexpr int kRichSurplus = 2 * kMaxPlanesPerHouse * kPlanePrice;   // 30,000
inline constexpr bool richBuys(int64_t surplus) { return surplus >= kRichSurplus; }

// ---------------------------------------------------------------------------
// Anti-air against the airplane (match rule GameOptionsClass::cityPlaneDefence).
//
// A turret rocket flies 11.52 world units a cycle, the plane 10.24. Aimed at where
// the plane is, as up to 1.0.876, the rocket arrives where the plane was: over 480
// measured passes no plane was shot down (HANDOVER.md, 2026-11-23). With the rule
// the rocket flies to the point where the two meet, solved here in integers so
// every peer gets the same point. Positions are quarter world units, speeds are
// quarter world units per game cycle times 16.
// ---------------------------------------------------------------------------

inline int64_t integerSqrt(int64_t value) {
    if(value <= 0) return 0;
    int64_t root = 0;
    for(int64_t bit = int64_t(1) << 62; bit != 0; bit >>= 2) {
        if(value >= root + bit) { value -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
    }
    return root;
}

/// A rocket chasing a city airplane goes off once it is this many times the Rocket Turret's
/// WeaponRange from where it was fired: the turret's own reach against aircraft, 3 x 8 = 24
/// tiles (Stefan, 2026-11-23: "missiles chasing a plane just keep going forever. there needs to
/// be a distance cutoff"). A turret rocket is 1.13 times as fast as the plane, so one fired
/// after a plane flying away followed it for nine times the gap between them. A Rocket Turret
/// now only fires at a plane its rocket can meet inside this distance (RocketTurret::canAttack).
inline constexpr int kRocketChaseRangeFactor = 3;

// Balance of 2026-11-23, set from measurements (HANDOVER.md, "City planes: balance"). Stefan:
// "why are turrets reaching 24 tiles?? thatv sounds insane", "we need a balance with these
// planes, don't nerf them too much", "it should be able to deal some real damage to enemy bases
// to break up a stalemate".
// - A Rocket Turret reaches a city airplane, and its rocket chases one, kTurretPlaneReachFactor
//   = 2 times its WeaponRange: 16 tiles. It was the Ornithopter's 3 (24 tiles), under which 92%
//   of flights in ordinary AI games were shot down, half within ten seconds of take-off.
//   Ornithopters keep 3.
// - The plane has kPlaneHitPoints = 150, five turret rockets instead of four. The unit data of
//   every mod was seeded with 100, so a new game that reads exactly 100 gets 150
//   (AmbientAirplane::applyBalance); another value is a mod's own choice and stays.
// - Its speed stays. A turret rocket is 12% faster than the plane; 15% more speed outran the
//   rockets and took 87% of Metropolis in six minutes, 10% with 125 hit points 77%.
inline constexpr int kTurretPlaneReachFactor = 2;
inline constexpr int kPlaneHitPoints = 150;
inline constexpr int kPlaneHitPointsSeeded = 100;

struct Intercept {
    bool possible = false;  ///< false when the target is as fast as the missile
    int cycles16 = 0;       ///< flight time in sixteenths of a game cycle
    Coord point;            ///< meeting point, quarter world units
};

/// Solve |target + v t - missile| = s t for the smallest t > 0.
inline Intercept intercept(const Coord& missile, const Coord& target, int targetVX16, int targetVY16, int missileSpeed16) {
    Intercept result;
    const int64_t rx = int64_t(target.x) - missile.x, ry = int64_t(target.y) - missile.y;
    const int64_t a = int64_t(targetVX16) * targetVX16 + int64_t(targetVY16) * targetVY16
                    - int64_t(missileSpeed16) * missileSpeed16;
    if(a >= 0) return result;
    const int64_t b = 16 * (rx * targetVX16 + ry * targetVY16);
    const int64_t c = 256 * (rx * rx + ry * ry);
    const int64_t time16 = (16 * (-b - integerSqrt(b * b - a * c))) / a;
    result.possible = true;
    result.cycles16 = static_cast<int>(std::min<int64_t>(time16, 1 << 24));
    result.point = Coord(static_cast<int>(target.x + targetVX16 * int64_t(result.cycles16) / 256),
                         static_cast<int>(target.y + targetVY16 * int64_t(result.cycles16) / 256));
    return result;
}

} // namespace DuneCity::CityAircraft

#endif // DUNECITY_CITYAIRCRAFTPOLICY_H
