#include <units/AmbientAirplane.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <Bullet.h>
#include <DynastyProjectile.h>
#include <House.h>
#include <Map.h>
#include <ScreenBorder.h>
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
    bombSightTile = Coord::Invalid();
    stickGap = CA::kStickTailBombs + 1;
    stickPhase = 0;
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
        bombSightTile.x = stream.readSint32();
        bombSightTile.y = stream.readSint32();
    } else {
        // A plane from an older save was unarmed; it continues with a full load.
        bombs = CA::kBombLoad;
        bombTimer = 0;
        bombSightTile = Coord::Invalid();
    }
    if(currentGame->getLoadedSavegameVersion() >= SAVEGAMEVERSION_CITY_PLANE_PURCHASE) {
        stickGap = std::clamp(stream.readSint32(), 0, CA::kStickTailBombs + 1);
        stickPhase = std::clamp(stream.readSint32(), 0, CA::kStickPhasePeriod + CA::kStickPhaseStep);
    } else {
        stickGap = CA::kStickTailBombs + 1;
        stickPhase = 0;
    }
    // A plane from an older save counts as the free one.
    bought = currentGame->getLoadedSavegameVersion() >= SAVEGAMEVERSION_CITY_PLANE_FREE && stream.readBool();
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
    stream.writeSint32(bombSightTile.x);
    stream.writeSint32(bombSightTile.y);
    stream.writeSint32(stickGap);
    stream.writeSint32(stickPhase);
    stream.writeBool(bought);
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

/**
    City Plane Bombing Runs: the next leg crosses a building of another team.

    The building is drawn evenly from every living structure not on the plane's team, so a
    house with more buildings draws more runs. The waypoint lies CA::kRunOvershootTiles
    beyond it on the line from the plane, kept on the map, so the plane flies across the
    building and what stands behind it before the usual arrival rule draws the next leg.
    The draw is from the game's own random generator, as Micropolis's waypoint is.
*/
bool AmbientAirplane::pickBombingRun()
{
    const auto isTarget = [this](const StructureBase* structure) {
        return structure->getHealth() > 0 && structure->getOwner()->getTeamID() != owner->getTeamID();
    };
    int count = 0;
    for(const StructureBase* structure : structureList) {
        if(isTarget(structure)) ++count;
    }
    if(count == 0) return false;
    int pick = currentGame->randomGen.rand(0, count - 1);
    const StructureBase* target = nullptr;
    for(const StructureBase* structure : structureList) {
        if(isTarget(structure) && pick-- == 0) { target = structure; break; }
    }
    if(target == nullptr) return false;
    const Coord from = getCenterPoint();
    const Coord to = target->getCenterPoint();
    const int dx = to.x - from.x, dy = to.y - from.y;
    const int length = static_cast<int>(CA::integerSqrt(int64_t(dx) * dx + int64_t(dy) * dy));
    // Right above the building there is no line to extend: fly on to another one.
    if(length < TILESIZE) return false;
    const int beyond = CA::kRunOvershootTiles * TILESIZE;
    const int x = std::clamp(to.x + dx * beyond / length, 0, currentGameMap->getSizeX() * TILESIZE - 1);
    const int y = std::clamp(to.y + dy * beyond / length, 0, currentGameMap->getSizeY() * TILESIZE - 1);
    setFlightDestination(Coord(x / TILESIZE, y / TILESIZE));
    return true;
}

void AmbientAirplane::pickDestination()
{
    if(currentGame->getGameInitSettings().getGameOptions().cityPlaneBombingRuns
       && currentGame->getGameInitSettings().getGameOptions().cityPlaneBombing && pickBombingRun()) return;
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
    const ObjectBase* shooter = currentGame->getObjectManager().getObject(shooterID);
    SDL_Log("CITY_PLANE_DOWN: house=%d object=%u killer_house=%d killer_item=%d released=%d cycle=%u",
            getOwner()->getHouseID(), objectID, credited->getHouseID(), shooter ? shooter->getItemID() : -1,
            static_cast<int>(bombsReleased),
            currentGame->getGameCycleCount());
    // With City Plane Defence planes are shot down all over the map, about one a minute per
    // house in AI matches: only a crash on or near the screen shakes it. Presentation only.
    const bool shake = screenborder->isInsideScreen(at, Coord(TILESIZE * 8, TILESIZE * 8));
    // The crash is the plane's own damage (Stefan, 2026-11-23: "does the damage dealt include
    // damage when it crashes? it has a nuclear missile effect so that should be counted"). It
    // was dealt in the name of whoever shot the plane down, so on the defender's city it counted
    // as that house's own fire and nowhere as the plane's. It is now dealt by the plane and its
    // owner: the Stats window's City Airplane row and QuantBot's record of whether its planes
    // pay (CityAircraft::purchasePays) include it. The buildings in reach are measured before
    // and after for one log line per crash (tests/ai/city-plane-report.py).
    struct Standing { Uint32 id; bool enemy; int price; int health; int owner; };
    std::vector<Standing> standing;
    constexpr int reach = 2 * TILESIZE + Bullet::deathHandBlastRadius;
    for(const StructureBase* structure : structureList) {
        if(structure->getHealth() <= 0) continue;
        const Coord topLeft = structure->getLocation() * TILESIZE;
        const Coord bottomRight = topLeft + structure->getStructureSize() * TILESIZE;
        if(at.x < topLeft.x - reach || at.y < topLeft.y - reach || at.x >= bottomRight.x + reach
           || at.y >= bottomRight.y + reach) continue;
        standing.push_back({structure->getObjectID(), structure->getOwner()->getTeamID() != getOwner()->getTeamID(),
                            currentGame->objectData.data[structure->getItemID()][structure->getOriginalHouseID()].price,
                            static_cast<int>(lround(structure->getHealth())), structure->getOwner()->getHouseID()});
    }
    Bullet::detonateDeathHand(objectID, getOwner(), at, DynastyProjectile::deathHandDamage, false,
                              DeviationReward::liveProvenance(this, getOwner()), shake);
    int enemyDestroyed = 0, enemyCredits = 0, enemyHitPoints = 0, ownDestroyed = 0, ownCredits = 0;
    for(const Standing& before : standing) {
        const ObjectBase* object = currentGame->getObjectManager().getObject(before.id);
        const int after = (object == nullptr || object->getHealth() <= 0) ? 0 : static_cast<int>(lround(object->getHealth()));
        if(before.enemy) {
            enemyHitPoints += std::max(0, before.health - after);
            if(after <= 0) {
                ++enemyDestroyed; enemyCredits += before.price;
                // One line per building of another team the crash destroys, with its owner, for
                // the match tables (CITY_PLANE_KILL is the same for a bomb).
                SDL_Log("CITY_PLANE_CRASH_KILL: house=%d target_house=%d price=%d cycle=%u", getOwner()->getHouseID(),
                        before.owner, before.price, currentGame->getGameCycleCount());
            }
        } else if(after <= 0) {
            ++ownDestroyed;
            ownCredits += before.price;
        }
    }
    SDL_Log("CITY_PLANE_CRASH: house=%d object=%u killer_house=%d enemy_destroyed=%d enemy_credits=%d enemy_hp=%d "
            "own_destroyed=%d own_credits=%d cycle=%u",
            getOwner()->getHouseID(), objectID, credited->getHouseID(), enemyDestroyed, enemyCredits, enemyHitPoints,
            ownDestroyed, ownCredits, currentGame->getGameCycleCount());
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
    // City Plane Defence: nothing holds its fire for the crash. The first form of the rule held
    // only the shot that would bring the plane down onto the shooter's own side, counting the
    // rockets already in flight. In Stefan's game of 2026-11-23 (Metropolis, hundreds of Rocket
    // Turrets a house, up to ten bombers a house) that held 59% of all turret-plane pairs in
    // reach and 99% of the Launchers': over a city every shot was "the killing one", so planes
    // bombed on until they flew clear ("rocket turrets are no longer attacking planes, neither
    // are launchers"; asked whether a plane may be brought down over the defender's own city:
    // "yes they can crash it in their own city"). With the rule off (the game up to 1.0.876)
    // every shot at a plane over the shooter's own side is still held.
    if(currentGame->getGameInitSettings().getGameOptions().cityPlaneDefence) return false;
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
    return rocketIntercept(missile, Bullet_TurretRocket);
}

CA::Intercept AmbientAirplane::rocketIntercept(const Coord& missile, Uint32 bulletID) const
{
    // Bullet::updateDynastyProjectile moves the rocket `step` quarter units every 150 ticks of
    // a clock that advances 48 a cycle.
    const int rocketSpeed16 = DynastyProjectile::parameters(bulletID).step * 48 * 16 / 150;
    const Coord velocity = velocity16();
    return CA::intercept(missile, Coord(lround(realX * 4), lround(realY * 4)), velocity.x, velocity.y, rocketSpeed16);
}

int AmbientAirplane::touchedBy(const Coord& rocketFrom, const Coord& rocketTo) const
{
    // The same narrow test a rocket makes against its own target (one eighth of a tile, in
    // relative motion so that crossing the same place at different times is no hit). The plane's
    // own way during one rocket step: velocity16 is per cycle and a step is 150/48 cycles.
    const Coord now(lround(realX * 4), lround(realY * 4));
    const Coord velocity = velocity16();
    const Coord before(now.x - velocity.x * 25 / 128, now.y - velocity.y * 25 / 128);
    const Coord a = rocketFrom - before;
    const Coord d = (rocketTo - now) - a;
    const Sint64 length = Sint64(d.x) * d.x + Sint64(d.y) * d.y;
    const Sint64 projection = -(Sint64(a.x) * d.x + Sint64(a.y) * d.y);
    const Sint64 t = length ? std::clamp<Sint64>(projection * 1024 / length, 0, 1024) : 0;
    const Sint64 dx = Sint64(a.x) * 1024 + Sint64(d.x) * t;
    const Sint64 dy = Sint64(a.y) * 1024 + Sint64(d.y) * t;
    return dx * dx + dy * dy <= Sint64(32 * 1024) * (32 * 1024) ? static_cast<int>(t) : -1;
}

int AmbientAirplane::freePlanes(const House* house)
{
    int count = 0;
    for(const AmbientAirplane* plane : all()) {
        if(plane->getOwner() == house && !plane->isBought() && plane->getHealth() > 0) ++count;
    }
    return count;
}

int AmbientAirplane::rocketChaseRange4(int houseID)
{
    return currentGame->objectData.data[Structure_RocketTurret][houseID].weaponrange
           * turretReachFactor() * TILESIZE * 4;
}

void AmbientAirplane::applyBalance(ObjectData& data)
{
    const auto number = [](const char* name) {
        const char* text = std::getenv(name);
        return text != nullptr ? std::atoi(text) : 0;
    };
    const int hitPoints = number("DUNECITY_TEST_PLANE_HP");
    const int speedPercent = number("DUNECITY_TEST_PLANE_SPEED_PCT");
    for(int house = 0; house < NUM_HOUSES; ++house) {
        auto& plane = data.data[Unit_AmbientAirplane][house];
        if(plane.hitpoints == CA::kPlaneHitPointsSeeded) plane.hitpoints = CA::kPlaneHitPoints;
        if(hitPoints > 0) plane.hitpoints = hitPoints;
        if(speedPercent > 0) plane.maxspeed = plane.maxspeed * speedPercent / 100;
    }
}

int AmbientAirplane::bombEffectAt(const Coord& aim, const ObjectBase** hit) const
{
    const int radius = CA::kStickUnitBlastTiles2 * TILESIZE / 2;
    const int damage = stickDamage();
    const int splash = CA::stickBuildingSplashPercent();
    int best = 0;
    const auto consider = [&](const ObjectBase* object, int effect) {
        if(effect > best) {
            best = effect;
            if(hit != nullptr) *hit = object;
        }
    };
    std::set<Uint32> seen;
    for(int y = (aim.y - radius) / TILESIZE; y <= (aim.y + radius) / TILESIZE; ++y) {
        for(int x = (aim.x - radius) / TILESIZE; x <= (aim.x + radius) / TILESIZE; ++x) {
            if(x < 0 || y < 0 || !currentGameMap->tileExists(x, y)) continue;
            const Tile* tile = currentGameMap->getTile(x, y);
            const auto each = [&](const auto& ids) {
                for(Uint32 id : ids) {
                    if(!seen.insert(id).second) continue;
                    const ObjectBase* object = currentGame->getObjectManager().getObject(id);
                    if(object == nullptr || object->getHealth() <= 0 || object->getItemID() == Unit_Sandworm
                       || object->getOwner()->getTeamID() == owner->getTeamID()) continue;
                    if(object->isAStructure()) {
                        const auto* structure = static_cast<const StructureBase*>(object);
                        const Coord topLeft = structure->getLocation() * TILESIZE;
                        const Coord bottomRight = topLeft + structure->getStructureSize() * TILESIZE;
                        const int dx = std::max({topLeft.x - aim.x, 0, aim.x - (bottomRight.x - 1)});
                        const int dy = std::max({topLeft.y - aim.y, 0, aim.y - (bottomRight.y - 1)});
                        const int distance = static_cast<int>(lround(sqrt(static_cast<double>(dx * dx + dy * dy))));
                        if(distance <= radius) consider(object, CA::stickBuildingDamage(damage, distance, TILESIZE, splash));
                    } else if(object->isAUnit() && !object->isAFlyingUnit()) {
                        const int distance = static_cast<int>(lround(distanceFrom(object->getCenterPoint(), aim)));
                        if(distance <= radius) consider(object, CA::stickUnitDamage(damage, distance, TILESIZE));
                    }
                }
            };
            each(tile->getNonInfantryGroundObjectList());
            each(tile->getInfantryList());
        }
    }
    return best;
}

int AmbientAirplane::stickDamage()
{
    static const int damage = [] {
        const char* text = std::getenv("DUNECITY_TEST_BOMB_DAMAGE");
        const int value = text != nullptr ? std::atoi(text) : 0;
        return value > 0 ? value : CA::kStickDamage;
    }();
    return damage;
}

bool AmbientAirplane::rocketsHitPlanes()
{
    static const bool hits = [] {
        const char* text = std::getenv("DUNECITY_TEST_PLANE_ROCKET_HITS");
        return text == nullptr || std::atoi(text) != 0;
    }();
    return hits;
}

int AmbientAirplane::turretReachFactor()
{
    static const int factor = [] {
        const char* text = std::getenv("DUNECITY_TEST_PLANE_REACH_FACTOR");
        const int value = text != nullptr ? std::atoi(text) : 0;
        return value >= 1 && value <= CA::kRocketChaseRangeFactor ? value : CA::kTurretPlaneReachFactor;
    }();
    return factor;
}

bool AmbientAirplane::rocketCanMeet(const Coord& launch, int range4) const
{
    const auto meet = turretRocketIntercept(launch * 4);
    return meet.possible && DynastyProjectile::distance(launch * 4, meet.point) <= range4;
}

/**
    City Plane Bombing: the bomb sight is the point CA::kBombSightDistance
    ahead on the plane's course. Each time it enters a tile on which a building
    of another team stands, and a bomb is on board, the bomb is released towards
    that point; it lands CA::kBombFallCycles later (Bullet_Bomb) and damages
    the building it lands on, and by the ordinary splash rule units within half
    a tile. The plane's own and allied buildings are never aimed at, and a bomb
    only damages a building its impact point lies on, so they are never hit.
*/
Coord AmbientAirplane::bombSight() const
{
    const FixPoint angleRad = (angle * (FixPt_PI << 1)) / 8;
    const int sight = CA::toWorld(CA::kBombSightDistance, TILESIZE);
    const Coord from = getCenterPoint();
    return Coord(from.x + lround(FixPoint::cos(angleRad) * sight), from.y - lround(FixPoint::sin(angleRad) * sight));
}

/**
    City Plane Carpet Bombing: one game cycle of the stick. A stick starts with a bomb the cycle
    a bomb released now would do at least CA::kStickStartDamage to a building or ground unit of
    another team (a direct hit or its splash, bombEffectAt), then one bomb every 12.5 cycles (two
    tiles on an axis heading) while a bomb would do at least CA::kStickCarryDamage, and
    CA::kStickTailBombs more after that. A bomb that falls due while the sight is over a building
    of the plane's own team, or off the map, is not released. There is no rack to empty. A bomb
    lands CA::kBombFallCycles later on the point the sight was on; it never damages an aircraft
    (Map::damage).
*/
void AmbientAirplane::dropStick(Uint32 gameCycle)
{
    Coord from = getCenterPoint();
    Coord aim = bombSight();
    const ObjectBase* building = nullptr;
    bool onMap = false;
    if(aim.x >= 0 && aim.y >= 0 && currentGameMap->tileExists(aim.x / TILESIZE, aim.y / TILESIZE)) {
        onMap = true;
        const Tile* tile = currentGameMap->getTile(aim.x / TILESIZE, aim.y / TILESIZE);
        if(tile->hasAGroundObject() && tile->getGroundObject() != nullptr && tile->getGroundObject()->isAStructure()
           && tile->getGroundObject()->getHealth() > 0) {
            building = tile->getGroundObject();
        }
    }
    const bool own = building != nullptr && building->getOwner()->getTeamID() == owner->getTeamID();
    // What a bomb released now would do: a direct hit on an enemy building, or its splash on
    // enemy buildings and ground units around the point (bombEffectAt). Nothing over a building
    // of the plane's own side.
    const ObjectBase* target = nullptr;
    const int effect = (!onMap || own) ? 0 : bombEffectAt(aim, &target);
    if(stickGap > CA::kStickTailBombs) {
        if(effect < CA::kStickStartDamage) return;  // no stick, nothing worth starting one
        stickPhase = CA::kStickPhasePeriod;         // a new stick: its first bomb falls now
    } else {
        stickPhase += CA::kStickPhaseStep;
    }
    const bool enemy = effect >= CA::kStickCarryDamage;
    if(enemy) stickGap = 0;
    if(stickPhase < CA::kStickPhasePeriod) return;
    stickPhase -= CA::kStickPhasePeriod;
    if(!enemy && ++stickGap > CA::kStickTailBombs) return;   // the tail is spent
    if(!onMap || own) return;
    if(building == nullptr) building = (target != nullptr && target->isAStructure()) ? target : nullptr;

    bulletList.push_back(new Bullet(objectID, &from, &aim, Bullet_Bomb, stickDamage(), false,
                                    building != nullptr ? building : target));
    ++bombsReleased;
    SDL_Log("CITY_PLANE_BOMB: house=%d object=%u target_house=%d target_item=%d cycle=%u", owner->getHouseID(),
            objectID, building ? building->getOwner()->getHouseID() : -1, building ? int(building->getItemID()) : -1,
            gameCycle);
}

void AmbientAirplane::dropBomb(Uint32 gameCycle)
{
    Coord from = getCenterPoint();
    Coord aim = bombSight();
    const Coord sightTile = (aim.x < 0 || aim.y < 0) ? Coord::Invalid() : Coord(aim.x / TILESIZE, aim.y / TILESIZE);
    if(sightTile == bombSightTile) return;
    bombSightTile = sightTile;
    if(bombs <= 0 || sightTile.isInvalid() || !currentGameMap->tileExists(sightTile)) return;
    const Tile* tile = currentGameMap->getTile(sightTile);
    if(!tile->hasAGroundObject()) return;
    const ObjectBase* building = tile->getGroundObject();
    if(building == nullptr || !building->isAStructure() || building->getHealth() <= 0
       || building->getOwner()->getTeamID() == owner->getTeamID()) return;
    // No bomb for a building the bombs already falling on it will destroy.
    int falling = 0;
    for(const Bullet* bullet : bulletList) {
        if(bullet->getBulletID() == Bullet_Bomb && bullet->getTarget() == building) falling += bullet->getDamage();
    }
    if(building->getHealth() <= falling) return;

    bulletList.push_back(new Bullet(objectID, &from, &aim, Bullet_Bomb, CA::kBombDamage, false, building));
    --bombs;
    ++bombsReleased;
    SDL_Log("CITY_PLANE_BOMB: house=%d object=%u target_house=%d target_item=%d cycle=%u", owner->getHouseID(),
            objectID, building->getOwner()->getHouseID(), building->getItemID(), gameCycle);
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
    // The bomb sight is followed every cycle, so no tile it crosses is missed.
    const bool bombing = currentGame->getGameInitSettings().getGameOptions().cityPlaneBombing;
    const bool stick = bombing && currentGame->getGameInitSettings().getGameOptions().cityPlaneCarpetBombing;
    if(bombing && takeoffFrame == 0) {
        if(stick) dropStick(gameCycle);
        else      dropBomb(gameCycle);
    }
    if(!CA::isSpriteTick(gameCycle)) {
        return true;
    }

    if(remainingTicks > 0) --remainingTicks;

    if(bombing && !stick) bombs = CA::rearmBombs(bombs, bombTimer);

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
