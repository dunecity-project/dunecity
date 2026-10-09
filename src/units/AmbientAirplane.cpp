#include <units/AmbientAirplane.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Map.h>
#include <Game.h>
#include <dunecity/CityAircraftPolicy.h>

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
    drawnFrame = takeoffFrame >= CA::kAirplaneLastTakeoffFrame
              && takeoffFrame <= CA::kAirplaneFirstTakeoffFrame ? takeoffFrame - 8 : 0;
}

void AmbientAirplane::init()
{
    itemID = Unit_AmbientAirplane;
    registerUnit();

    canAttackStuff = false;

    // Original Micropolis sprite art: row 0 is the eight cruise headings,
    // rows 1..3 are take-off frames 9, 10 and 11.
    graphicID = ObjPic_CityAirplane;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    // No shadow: Micropolis never drew one, and AirUnit::blitToScreen skips a
    // null shadowGraphic.

    numImagesX = NUM_ANGLES;
    numImagesY = 4;
    drawnFrame = 0;
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
    drawnFrame = static_cast<int>(takeoffFrame - 8);
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
    drawnFrame = 0;
    angle = FixPoint(static_cast<int>(LEFT));
    drawnAngle = static_cast<Sint8>(LEFT);

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
            drawnFrame = static_cast<int>(next - 8);
            angle = FixPoint(static_cast<int>(RIGHT));
            drawnAngle = static_cast<Sint8>(RIGHT);
        } else {
            drawnFrame = 0;
            const ANGLETYPE cruise = CA::directionToAngle(CA::kAirplaneAfterTakeoffDirection);
            angle = FixPoint(static_cast<int>(cruise));
            drawnAngle = static_cast<Sint8>(cruise);
        }
        return;
    }

    if(destination.isInvalid()) return;

    const Coord position = micropolisPosition();
    const Coord target = micropolisDestination();
    const auto bearing = CA::getDir(position.x, position.y, target.x, target.y);
    const int heading = CA::turnTo(CA::angleToDirection(drawnAngle), bearing.direction);

    angle = FixPoint(static_cast<int>(CA::directionToAngle(heading)));
    drawnAngle = static_cast<Sint8>(CA::directionToAngle(heading));
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
    return CA::headingSpeed(currentMaxSpeed, drawnAngle,
                            CA::kAirplaneStep, CA::kAirplaneDiagonalStep);
}
