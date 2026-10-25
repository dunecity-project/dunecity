#include <units/AmbientAirplane.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <Bullet.h>
#include <DynastyProjectile.h>
#include <House.h>
#include <Map.h>
#include <Game.h>
#include <Tile.h>
#include <structures/StructureBase.h>
#include <dunecity/CityAircraftPolicy.h>

#include <algorithm>
#include <cstdlib>
#include <set>

namespace CA = DuneCity::CityAircraft;

AmbientAirplane::AmbientAirplane(House* newOwner) : AirUnit(newOwner)
{
    AmbientAirplane::init();

    setHealth(getMaxHealth());

    // Civilian: STOP keeps the per-cycle target scan from running at all.
    attackMode = STOP;
    respondable = false;

    takeoffFrame = 0;
    remainingTicks = CA::kAirplaneMaxTicks;
}

AmbientAirplane::AmbientAirplane(InputStream& stream) : AirUnit(stream)
{
    AmbientAirplane::init();

    if(currentGame->getLoadedSavegameVersion() >= 9853) {
        takeoffFrame = stream.readSint32();
        remainingTicks = stream.readSint32();
    } else {
        // 9852 and older stored a single `passedDestination` flag for the
        // placeholder flyover. A plane loaded from such a save keeps flying,
        // already airborne, with a fresh flight budget.
        stream.readBool();
        takeoffFrame = 0;
        remainingTicks = CA::kAirplaneMaxTicks;
    }
    // init() sets the cruise row; a save may be between take-off frames.
    if(takeoffFrame >= CA::kAirplaneLastTakeoffFrame && takeoffFrame <= CA::kAirplaneFirstTakeoffFrame) {
        drawnFrame = CA::airplaneTakeoffRow(takeoffFrame);
    } else {
        showHeading();
    }
}

void AmbientAirplane::init()
{
    itemID = Unit_AmbientAirplane;
    registerUnit();

    canAttackStuff = false;

    // Original Micropolis sprite art, plus the in-between headings rotated from
    // it by scripts/import-micropolis-aircraft.py: row 0 is the eight original
    // cruise headings, row 1 the eight headings 22.5 degrees counter-clockwise
    // of them, rows 2..4 take-off frames 9, 10 and 11.
    graphicID = ObjPic_CityAirplane;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    // No shadow: Micropolis never drew one, and AirUnit::blitToScreen skips a
    // null shadowGraphic.

    numImagesX = NUM_ANGLES;
    numImagesY = CA::kAirplaneSheetRows;
}

AmbientAirplane::~AmbientAirplane() = default;

void AmbientAirplane::save(OutputStream& stream) const
{
    AirUnit::save(stream);

    stream.writeSint32(takeoffFrame);
    stream.writeSint32(remainingTicks);
}

bool AmbientAirplane::canPass(int xPos, int yPos) const
{
    return currentGameMap->tileExists(xPos, yPos);
}

void AmbientAirplane::deploy(const Coord& newLocation)
{
    AirUnit::deploy(newLocation);

    respondable = false;
}

/**
    Point the plane at a waypoint that may be outside the map.

    `ObjectBase::setDestination` (src/ObjectBase.cpp:378) only accepts a
    waypoint on an existing tile, or the (INVALID_POS, INVALID_POS) sentinel,
    and silently discards anything else.  That validation is right for every
    unit that has to walk or drive to its destination, and it stays: the
    airplane needs destinations that are deliberately outside the map.
    `doAirplaneSprite` draws them from `[-50, size*16 + 50]` (sprite.cpp:825),
    and flying to one is how the original plane leaves and ends its flight.
    With the generic setter those waypoints never landed, so the plane kept
    turning back over the city until its budget deleted it in mid-air.

    A coordinate of exactly -1 is pushed one tile further out: INVALID_POS is
    -1, and `Coord::isInvalid()` is true if *either* axis is -1, so a waypoint
    one Micropolis unit beyond the west or north edge would otherwise read as
    "no destination at all".
*/
void AmbientAirplane::setFlightDestination(const Coord& waypoint)
{
    const auto outward = [](int tile) { return tile == INVALID_POS ? INVALID_POS - 1 : tile; };
    destination.x = outward(waypoint.x);
    destination.y = outward(waypoint.y);
}

void AmbientAirplane::beginTakeoff()
{
    // newSprite(SPRITE_AIRPLANE): `destX = sprite->x + 200; frame = 11;`
    takeoffFrame = CA::kAirplaneFirstTakeoffFrame;
    drawnFrame = CA::airplaneTakeoffRow(takeoffFrame);
    angle = FixPoint(static_cast<int>(RIGHT));
    drawnAngle = static_cast<Sint8>(RIGHT);

    const int distance = 200 * TILESIZE / CA::kMicropolisUnitsPerTile;
    setFlightDestination(Coord((lround(realX) + distance) / TILESIZE, location.y));
}

void AmbientAirplane::beginWestboundCruise()
{
    // newSprite(SPRITE_AIRPLANE), eastern-edge case:
    // `sprite->x -= 100 + 48; destX = sprite->x - 200; frame = 7;`
    takeoffFrame = 0;
    angle = FixPoint(static_cast<int>(LEFT));
    showHeading();

    const int distance = 200 * TILESIZE / CA::kMicropolisUnitsPerTile;
    setFlightDestination(Coord((lround(realX) - distance) / TILESIZE, location.y));
}

Coord AmbientAirplane::micropolisPosition() const
{
    return Coord(CA::toMicropolis(lround(realX), TILESIZE),
                 CA::toMicropolis(lround(realY), TILESIZE));
}

Coord AmbientAirplane::micropolisDestination() const
{
    return Coord(CA::toMicropolis(destination.x * TILESIZE + TILESIZE / 2, TILESIZE),
                 CA::toMicropolis(destination.y * TILESIZE + TILESIZE / 2, TILESIZE));
}

void AmbientAirplane::pickDestination()
{
    // doAirplaneSprite:
    //   destX = getRandom((WORLD_W * 16) + 100) - 50;
    //   destY = getRandom((WORLD_H * 16) + 100) - 50;
    // i.e. anywhere on the map plus a ~3-tile margin outside it, which is how
    // the original plane eventually flies off the edge and ends.
    const int margin = CA::kAirplaneDestinationMargin;
    const int spanX = currentGameMap->getSizeX() * CA::kMicropolisUnitsPerTile + 2 * margin;
    const int spanY = currentGameMap->getSizeY() * CA::kMicropolisUnitsPerTile + 2 * margin;

    const int microX = currentGame->randomGen.rand(0, spanX) - margin;
    const int microY = currentGame->randomGen.rand(0, spanY) - margin;

    // Micropolis units floor-divide to tiles; keep negatives on the outside.
    const auto toTile = [](int micro) {
        return (micro >= 0) ? micro / CA::kMicropolisUnitsPerTile
                            : -((-micro + CA::kMicropolisUnitsPerTile - 1) / CA::kMicropolisUnitsPerTile);
    };
    setFlightDestination(Coord(toTile(microX), toTile(microY)));
}

/**
    End the flight the way the original does: by leaving the map.

    `doAirplaneSprite` has no landing state and no timeout; the only ending is
    sprite.cpp:856, `if (spriteNotInBounds(sprite)) sprite->frame = 0`.  The
    flight budget is DuneCity's own bound on a plane whose random waypoints
    keep falling back over a map far larger than the original's 120x100, so
    when it runs out the plane is sent out through its nearest edge instead of
    being removed over the city.

    The phase needs no new saved state.  `remainingTicks == 0` is the budget
    flag and is already serialised, the outward waypoint lives in the inherited
    destination fields (ObjectBase writes them raw, src/ObjectBase.cpp:226), and
    "already leaving" is read back off that waypoint, so a reload resumes the
    same route instead of choosing a new one.
*/
void AmbientAirplane::beginDeparture()
{
    if(destination.isValid()
       && CA::isOutsideMap(destination.x, destination.y,
                           currentGameMap->getSizeX(), currentGameMap->getSizeY())) {
        return;     // already on its way out, including after a reload
    }

    setFlightDestination(CA::departureWaypoint(location.x, location.y,
                                               currentGameMap->getSizeX(),
                                               currentGameMap->getSizeY()));
}

void AmbientAirplane::leaveMap()
{
    // Original: `sprite->frame = 0` — off the map, gone, no explosion.
    setVisible(VIS_ALL, false);
    destroy();
}

void AmbientAirplane::showHeading()
{
    const int frame = CA::airplaneHeadingFrame(angle);
    drawnAngle = static_cast<Sint8>(CA::airplaneCruiseColumn(frame));
    drawnFrame = CA::airplaneCruiseRow(frame);
}

void AmbientAirplane::turn()
{
    // doAirplaneSprite: `if ((spriteCycle % 5) == 0) { takeoff step, else turnTo }`
    const Uint32 gameCycle = currentGame->getGameCycleCount();
    if(!CA::isSpriteTick(gameCycle)) return;
    if((CA::spriteCycle(gameCycle) % CA::kAirplaneTurnInterval) != 0) return;

    if(takeoffFrame > 0) {
        // `z--; if (z < 9) z = 3;` — climb out, then cruise east.
        const int next = CA::nextTakeoffFrame(takeoffFrame);
        takeoffFrame = next;
        if(next > 0) {
            drawnFrame = CA::airplaneTakeoffRow(next);
            angle = FixPoint(static_cast<int>(RIGHT));
            drawnAngle = static_cast<Sint8>(RIGHT);
        } else {
            angle = FixPoint(static_cast<int>(CA::directionToAngle(CA::kAirplaneAfterTakeoffDirection)));
            showHeading();
        }
        return;
    }

    if(destination.isInvalid()) return;

    // Straight legs instead of turnTo(getDir()): see CityAircraft::airplaneSteer.
    const FixPoint distance = distanceFrom(realX, realY, destination.x * TILESIZE + TILESIZE / 2,
                                           destination.y * TILESIZE + TILESIZE / 2);
    angle = CA::airplaneSteer(angle, getDestinationAngle(), distance,
                              CA::toWorld(CA::kAirplaneHoldCrossTrack, TILESIZE));
    showHeading();
}

void AmbientAirplane::handleDamage(int damage, Uint32 damagerID, House* damagerOwner,
                                   const DeviationReward::Provenance& provenance)
{
    const bool flying = getHealth() > 0;
    AirUnit::handleDamage(damage, damagerID, damagerOwner, provenance);
    if(flying && getHealth() <= 0) {
        crash(damagerID, damagerOwner, provenance);
    }
}

/**
    Stefan, 2026-10-22: "when the plane gets shot down it should make the
    equivalent of a nuclear missile attack".

    The plane crashes where it was hit with exactly a Death Hand impact: the
    Palace missile's own Bullet::detonateDeathHand(), its damage
    (DynastyProjectile::deathHandDamage), blast centres, radius, explosions and
    sound. As with the Palace, whoever fired is the damager: the house that
    brought the plane down is credited with everything the blast destroys, and
    the blast spares nobody - its own side, the plane's owner and other aircraft
    alike. Damage with no owning house is credited to the plane's owner.

    It runs inside the damage pass that killed the plane. That is safe: the plane
    is already at zero health, so the blast's own pass over it returns at once
    (ObjectBase::handleDamage), and everything else the blast kills is only
    marked dead and removed by its own next update, as for any other weapon. A
    plane that leaves the map (leaveMap) or is removed any other way never
    reaches here.
*/
void AmbientAirplane::crash(Uint32 shooterID, House* shooterOwner, const DeviationReward::Provenance& provenance)
{
    House* const credited = shooterOwner != nullptr ? shooterOwner : getOwner();
    const Coord at = getCenterPoint();
    SDL_Log("City airplane of house %d shot down at %d,%d by house %d (cycle %u): Death Hand impact",
            getOwner()->getHouseID(), at.x / TILESIZE, at.y / TILESIZE, credited->getHouseID(),
            currentGame->getGameCycleCount());
    Bullet::detonateDeathHand(shooterID, credited, at, DynastyProjectile::deathHandDamage, false, provenance);
}

bool AmbientAirplane::crashWouldHit(int teamID, FixPoint lead) const
{
    // The crash points: the plane now, then every half tile along its course
    // up to `lead` world units ahead.
    const Coord from = getCenterPoint();
    const FixPoint angleRad = (angle * (FixPt_PI << 1)) / 8;
    const Coord to(from.x + lround(FixPoint::cos(angleRad) * lead), from.y - lround(FixPoint::sin(angleRad) * lead));
    const int steps = std::max(std::abs(to.x - from.x), std::abs(to.y - from.y)) / (TILESIZE / 2) + 1;

    // Every own-side object a blast centre could reach: the farthest centre is
    // two tiles out on either axis, and a unit is hit within the blast radius of one.
    // Natives objects are never own-side (crashEndangers).
    constexpr int spread = 2 * TILESIZE + Bullet::deathHandBlastRadius;
    const int mapW = currentGameMap->getSizeX(), mapH = currentGameMap->getSizeY();
    const int minX = std::max(0, (std::min(from.x, to.x) - spread) / TILESIZE);
    const int maxX = std::min(mapW - 1, (std::max(from.x, to.x) + spread) / TILESIZE);
    const int minY = std::max(0, (std::min(from.y, to.y) - spread) / TILESIZE);
    const int maxY = std::min(mapH - 1, (std::max(from.y, to.y) + spread) / TILESIZE);
    std::set<Uint32> ownIDs;
    const auto collect = [&](const std::list<Uint32>& objects) {
        for(Uint32 id : objects) {
            const ObjectBase* object = currentGame->getObjectManager().getObject(id);
            if(object != nullptr && object != this && object->getOwner()->getTeamID() == teamID
               && !object->getOwner()->isNatives()) {
                ownIDs.insert(id);
            }
        }
    };
    for(int y = minY; y <= maxY; ++y) {
        for(int x = minX; x <= maxX; ++x) {
            const Tile* tile = currentGameMap->getTile(x, y);
            collect(tile->getNonInfantryGroundObjectList());
            collect(tile->getInfantryList());
            collect(tile->getAirUnitList());
            collect(tile->getUndergroundUnitList());
        }
    }
    if(ownIDs.empty()) return false;

    // Map::damage for a Death Hand centre: a structure is hit when the centre
    // lies on its footprint, a unit when it is within the blast radius by
    // Dynasty's distance. Centres off the map are skipped, as detonateDeathHand does.
    for(int step = 0; step <= steps; ++step) {
        const Coord crashAt(from.x + (to.x - from.x) * step / steps, from.y + (to.y - from.y) * step / steps);
        for(int i = 0; i < DynastyProjectile::deathHandBlastCentres; ++i) {
            const Coord centre(crashAt.x + DynastyProjectile::deathHandBlastX[i],
                               crashAt.y + DynastyProjectile::deathHandBlastY[i]);
            if(centre.x < 0 || centre.y < 0 || centre.x >= mapW * TILESIZE || centre.y >= mapH * TILESIZE) continue;
            for(Uint32 id : ownIDs) {
                const ObjectBase* object = currentGame->getObjectManager().getObject(id);
                if(object->isAStructure()) {
                    const auto* structure = static_cast<const StructureBase*>(object);
                    const Coord topLeft = structure->getLocation() * TILESIZE;
                    const Coord bottomRight = topLeft + structure->getStructureSize() * TILESIZE;
                    if(centre.x >= topLeft.x && centre.y >= topLeft.y && centre.x < bottomRight.x && centre.y < bottomRight.y)
                        return true;
                } else if(DynastyProjectile::distance(object->getCenterPoint() * 4, centre * 4) / 4
                          < Bullet::deathHandBlastRadius) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool AmbientAirplane::crashEndangers(const ObjectBase& attacker, const ObjectBase& target)
{
    if(target.getItemID() != Unit_AmbientAirplane) return false;
    const auto& plane = static_cast<const AmbientAirplane&>(target);
    // A missile fired now meets the plane this far along its course: the plane's
    // speed over the turret rocket's, the slowest anti-air missile (Bullet::init),
    // times the distance between them.
    static const FixPoint slowestMissile = FixPoint(DynastyProjectile::parameters(Bullet_TurretRocket).step) * 0.08_fix;
    const FixPoint lead = distanceFrom(attacker.getCenterPoint(), plane.getCenterPoint()) * plane.getMaxSpeed()
                        / slowestMissile;
    return plane.crashWouldHit(attacker.getOwner()->getTeamID(), lead);
}

bool AmbientAirplane::update()
{
    // Constant cruise speed — the original plane never slows for an approach.
    currentMaxSpeed = currentGame->objectData.data[itemID][originalHouseID].maxspeed;

    if(AirUnit::update() == false) {
        return false;
    }
    if(!active) {
        return true;
    }

    const Uint32 gameCycle = currentGame->getGameCycleCount();
    if(!CA::isSpriteTick(gameCycle)) {
        return true;
    }

    if(remainingTicks > 0) --remainingTicks;

    if(takeoffFrame == 0) {
        if(remainingTicks > 0) {
            // `if (absDist < 50) { pick a new destination }`
            const Coord position = micropolisPosition();
            const Coord target = micropolisDestination();
            if(destination.isInvalid()
               || CA::getDir(position.x, position.y, target.x, target.y).distance
                      < CA::kAirplaneArriveDistance) {
                pickDestination();
            }
        } else {
            // Budget spent: stop drawing waypoints over the city and leave.
            beginDeparture();
        }
    }

    // spriteNotInBounds() (sprite.cpp:459): the original retires the plane as
    // its hot spot crosses the world edge. Here the same rule is applied to the
    // unit's world centre. This is the only way a flight ends on its own: the
    // budget starts the departure above, it never deletes the plane in mid-air
    // over the map. Combat destruction inside the map goes the ordinary
    // UnitBase::destroy() route and is unaffected.
    const Coord position = micropolisPosition();
    if(CA::notInBounds(position.x, position.y,
                       currentGameMap->getSizeX(), currentGameMap->getSizeY())) {
        leaveMap();
        return false;
    }

    return true;
}

FixPoint AmbientAirplane::getMaxSpeed() const
{
    return CA::airplaneHeadingSpeed(currentMaxSpeed, CA::airplaneHeadingFrame(angle));
}
