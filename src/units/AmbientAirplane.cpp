#include <units/AmbientAirplane.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <Bullet.h>
#include <DynastyProjectile.h>
#include <House.h>
#include <Map.h>
#include <Game.h>
#include <Tile.h>
#include <structures/RocketTurret.h>
#include <structures/StructureBase.h>
#include <dunecity/CityAircraftPolicy.h>

#include <algorithm>
#include <cstdlib>
#include <set>

namespace CA = DuneCity::CityAircraft;

namespace {
std::vector<AmbientAirplane*> cityAirplanes;
}

const std::vector<AmbientAirplane*>& AmbientAirplane::all() { return cityAirplanes; }

AmbientAirplane::AmbientAirplane(House* newOwner) : AirUnit(newOwner)
{
    AmbientAirplane::init();

    setHealth(getMaxHealth());

    // Civilian: STOP keeps the per-cycle target scan from running at all.
    attackMode = STOP;
    respondable = false;

    takeoffFrame = 0;
    remainingTicks = CA::kAirplaneMaxTicks;
    bombs = CA::kBombLoad;
    bombTimer = 0;
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
    if(currentGame->getLoadedSavegameVersion() >= SAVEGAMEVERSION_CITY_PLANE_BOMBS) {
        bombs = std::clamp(stream.readSint32(), 0, CA::kBombLoad);
        bombTimer = std::clamp(stream.readSint32(), 0, CA::kBombRearmTicks - 1);
    } else {
        // A plane from an older save was unarmed; it continues with a full load.
        bombs = CA::kBombLoad;
        bombTimer = 0;
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

    cityAirplanes.push_back(this);
}

AmbientAirplane::~AmbientAirplane() {
    cityAirplanes.erase(std::remove(cityAirplanes.begin(), cityAirplanes.end(), this), cityAirplanes.end());
}

void AmbientAirplane::save(OutputStream& stream) const
{
    AirUnit::save(stream);

    stream.writeSint32(takeoffFrame);
    stream.writeSint32(remainingTicks);
    stream.writeSint32(bombs);
    stream.writeSint32(bombTimer);
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

bool AmbientAirplane::crashWouldHit(int teamID, FixPoint near, FixPoint far) const
{
    // The crash points: every half tile along the plane's course from `near`
    // to `far` world units ahead (near 0 is the plane now).
    const Coord centre = getCenterPoint();
    const FixPoint angleRad = (angle * (FixPt_PI << 1)) / 8;
    const Coord from(centre.x + lround(FixPoint::cos(angleRad) * near), centre.y - lround(FixPoint::sin(angleRad) * near));
    const Coord to(centre.x + lround(FixPoint::cos(angleRad) * far), centre.y - lround(FixPoint::sin(angleRad) * far));
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
    const auto collect = [&](const auto& objects) {
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
    const int teamID = attacker.getOwner()->getTeamID();
    if(currentGame->getGameInitSettings().getGameOptions().cityPlaneDefence) {
        // Only the shot that could bring the plane down is held. Shots already on their way
        // count; a turret's rocket damage is exact, any other weapon's is doubled for safety.
        int pending = 0;
        for(const Bullet* bullet : bulletList) {
            if(bullet->getTarget() == &plane) pending += bullet->getDamage();
        }
        const bool turret = attacker.getItemID() == Structure_RocketTurret;
        const int shot = turret
            ? static_cast<const RocketTurret&>(attacker).rocketDamage()
            : 2 * currentGame->objectData.data[attacker.getItemID()][attacker.getOriginalHouseID()].weapondamage;
        if(plane.getHealth() - pending > shot) return false;
        if(turret) {
            // Its rocket flies to the meeting point: only a crash around there matters.
            const auto meet = plane.turretRocketIntercept(attacker.getCenterPoint() * 4);
            if(meet.possible) {
                const FixPoint along = plane.getMaxSpeed() * meet.cycles16 / 16;
                return plane.crashWouldHit(teamID, std::max(FixPoint(0), along - TILESIZE), along + 2 * TILESIZE);
            }
        }
    }
    return plane.crashWouldHit(teamID, 0, lead);
}

Coord AmbientAirplane::velocity16() const
{
    const FixPoint angleRad = (angle * (FixPt_PI << 1)) / 8;
    const FixPoint speed = getMaxSpeed() * getMovementSpeedFactor() * 64;
    return Coord(lround(FixPoint::cos(angleRad) * speed), -lround(FixPoint::sin(angleRad) * speed));
}

CA::Intercept AmbientAirplane::turretRocketIntercept(const Coord& missile) const
{
    // Bullet::updateDynastyProjectile moves the rocket `step` quarter units every 150 ticks of
    // a clock that advances 48 a cycle.
    static const int rocketSpeed16 = DynastyProjectile::parameters(Bullet_TurretRocket).step * 48 * 16 / 150;
    const Coord velocity = velocity16();
    return CA::intercept(missile, Coord(lround(realX * 4), lround(realY * 4)), velocity.x, velocity.y, rocketSpeed16);
}

/**
    City Plane Bombing: on every second sprite tick the plane looks
    CA::kBombSightDistance ahead on its course. If a building of another team
    stands there and a bomb is on board, the bomb is released towards that
    point; it lands about a quarter of a second later (Bullet_Bomb) and damages
    the building it lands on, and by the ordinary splash rule units within half
    a tile. The plane's own and allied buildings are never aimed at, and a bomb
    only damages a building its impact point lies on, so they are never hit.
*/
void AmbientAirplane::dropBomb(Uint32 gameCycle)
{
    if(bombs <= 0 || (CA::spriteCycle(gameCycle) + objectID) % CA::kBombDropInterval != 0) return;

    const FixPoint angleRad = (angle * (FixPt_PI << 1)) / 8;
    const int sight = CA::toWorld(CA::kBombSightDistance, TILESIZE);
    Coord from = getCenterPoint();
    Coord aim(from.x + lround(FixPoint::cos(angleRad) * sight), from.y - lround(FixPoint::sin(angleRad) * sight));
    if(aim.x < 0 || aim.y < 0 || !currentGameMap->tileExists(aim.x / TILESIZE, aim.y / TILESIZE)) return;
    const Tile* tile = currentGameMap->getTile(aim.x / TILESIZE, aim.y / TILESIZE);
    if(!tile->hasAGroundObject()) return;
    const ObjectBase* building = tile->getGroundObject();
    if(building == nullptr || !building->isAStructure() || building->getHealth() <= 0
       || building->getOwner()->getTeamID() == owner->getTeamID()) return;

    bulletList.push_back(new Bullet(objectID, &from, &aim, Bullet_Bomb, CA::kBombDamage, false, building));
    --bombs;
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

    if(currentGame->getGameInitSettings().getGameOptions().cityPlaneBombing) {
        bombs = CA::rearmBombs(bombs, bombTimer);
        if(takeoffFrame == 0) dropBomb(gameCycle);
    }

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
