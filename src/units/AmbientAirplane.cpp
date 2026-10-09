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

void AmbientAirplane::beginTakeoff()
{
    // newSprite(SPRITE_AIRPLANE): `destX = sprite->x + 200; frame = 11;`
    takeoffFrame = CA::kAirplaneFirstTakeoffFrame;
    drawnFrame = static_cast<int>(takeoffFrame - 8);
    angle = FixPoint(static_cast<int>(RIGHT));
    drawnAngle = static_cast<Sint8>(RIGHT);

    const int distance = 200 * TILESIZE / CA::kMicropolisUnitsPerTile;
    setDestination(Coord((lround(realX) + distance) / TILESIZE, location.y));
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
    setDestination(Coord((lround(realX) - distance) / TILESIZE, location.y));
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
    setDestination(Coord(toTile(microX), toTile(microY)));
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
        // `if (absDist < 50) { pick a new destination }`
        const Coord position = micropolisPosition();
        const Coord target = micropolisDestination();
        if(destination.isInvalid()
           || CA::getDir(position.x, position.y, target.x, target.y).distance
                  < CA::kAirplaneArriveDistance) {
            pickDestination();
        }
    }

    // spriteNotInBounds(): the original removes the plane as its hot spot
    // crosses the edge. One tile of slack keeps the whole sprite out of view
    // first, matching what the engine's other fly-off units do.
    const long x = lround(realX);
    const long y = lround(realY);
    if(x < -TILESIZE || y < -TILESIZE
       || x > (currentGameMap->getSizeX() + 1) * TILESIZE
       || y > (currentGameMap->getSizeY() + 1) * TILESIZE
       || remainingTicks <= 0) {
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
