#include <units/AmbientHelicopter.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <House.h>
#include <Map.h>
#include <Game.h>
#include <dunecity/CityAircraftPolicy.h>
#include <dunecity/CitySimulation.h>
#include <misc/format.h>

namespace CA = DuneCity::CityAircraft;

AmbientHelicopter::AmbientHelicopter(House* newOwner) : AirUnit(newOwner)
{
    AmbientHelicopter::init();

    setHealth(getMaxHealth());

    // Civilian: STOP keeps the per-cycle target scan from running at all.
    attackMode = STOP;
    respondable = false;

    home = Coord::Invalid();
    patrolCount = CA::kHelicopterPatrolCount;
    reportCooldown = 0;
    seekCooldown = 0;
    reportCount = 0;
    returningHome = false;
}

AmbientHelicopter::AmbientHelicopter(InputStream& stream) : AirUnit(stream)
{
    AmbientHelicopter::init();

    if(currentGame->getLoadedSavegameVersion() >= 9853) {
        home.x = stream.readSint32();
        home.y = stream.readSint32();
        patrolCount = stream.readSint32();
        reportCooldown = stream.readSint32();
        seekCooldown = stream.readSint32();
        reportCount = stream.readUint32();
        returningHome = stream.readBool();
    } else {
        // 9852 and older stored spawnCycle/lifetime/orbitCenter for the
        // placeholder orbiting helicopter. Those fields cannot be mapped onto
        // the original patrol state, so read them away and restart the patrol:
        // the unit stays alive, keeps its position, and simply gets a fresh
        // Micropolis patrol count instead of an orbit it no longer flies.
        stream.readUint32();                      // spawnCycle
        stream.readUint32();                      // lifetime
        home.x = stream.readSint32();             // orbitCenter
        home.y = stream.readSint32();
        patrolCount = CA::kHelicopterPatrolCount;
        reportCooldown = 0;
        seekCooldown = 0;
        reportCount = 0;
        returningHome = false;
    }
}

void AmbientHelicopter::init()
{
    itemID = Unit_AmbientHelicopter;
    registerUnit();

    canAttackStuff = false;

    // Original Micropolis sprite art: one 32x32 frame per heading.
    graphicID = ObjPic_CityHelicopter;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    // No shadow: Micropolis never drew one, and AirUnit::blitToScreen skips a
    // null shadowGraphic.

    numImagesX = NUM_ANGLES;
    numImagesY = 1;
    drawnFrame = 0;
}

AmbientHelicopter::~AmbientHelicopter() = default;

void AmbientHelicopter::save(OutputStream& stream) const
{
    AirUnit::save(stream);

    stream.writeSint32(home.x);
    stream.writeSint32(home.y);
    stream.writeSint32(patrolCount);
    stream.writeSint32(reportCooldown);
    stream.writeSint32(seekCooldown);
    stream.writeUint32(reportCount);
    stream.writeBool(returningHome);
}

bool AmbientHelicopter::canPass(int xPos, int yPos) const
{
    return currentGameMap->tileExists(xPos, yPos);
}

void AmbientHelicopter::deploy(const Coord& newLocation)
{
    AirUnit::deploy(newLocation);

    respondable = false;
    if(home.isInvalid()) {
        setHome(newLocation);
    }
}

void AmbientHelicopter::setHome(const Coord& tile)
{
    home = tile;
    if(destination.isInvalid()) {
        setDestination(tile);
    }
}

Coord AmbientHelicopter::micropolisPosition() const
{
    return Coord(CA::toMicropolis(lround(realX), TILESIZE),
                 CA::toMicropolis(lround(realY), TILESIZE));
}

Coord AmbientHelicopter::micropolisDestination() const
{
    return Coord(CA::toMicropolis(destination.x * TILESIZE + TILESIZE / 2, TILESIZE),
                 CA::toMicropolis(destination.y * TILESIZE + TILESIZE / 2, TILESIZE));
}

void AmbientHelicopter::turn()
{
    // doCopterSprite: `if ((spriteCycle & 3) == 0) { d = getDir(...); z = turnTo(z, d); }`
    const Uint32 gameCycle = currentGame->getGameCycleCount();
    if(!CA::isSpriteTick(gameCycle)) return;
    if((CA::spriteCycle(gameCycle) % CA::kHelicopterTurnInterval) != 0) return;
    if(destination.isInvalid()) return;

    const Coord position = micropolisPosition();
    const Coord target = micropolisDestination();
    const auto bearing = CA::getDir(position.x, position.y, target.x, target.y);
    const int heading = CA::turnTo(CA::angleToDirection(drawnAngle), bearing.direction);

    angle = FixPoint(static_cast<int>(CA::directionToAngle(heading)));
    drawnAngle = static_cast<Sint8>(CA::directionToAngle(heading));
}

void AmbientHelicopter::seekHeavyTraffic()
{
    // traffic.cpp:185: whenever the traffic simulation saturates a road cell it
    // pushes that cell at the helicopter as its new destination, so the
    // helicopter keeps being sent to whichever jam the simulation happened to
    // reach last and tours the congested parts of the city. DuneCity's traffic
    // density lives in a map layer rather than a per-journey stack, so the
    // helicopter pulls instead: same saturation threshold, and one of the
    // congested cells drawn from the shared deterministic RNG rather than
    // always the nearest, which would park it on one corner forever.
    const auto* citySim = currentGame->getCitySimulation();
    if(citySim == nullptr || home.isInvalid()) return;

    const auto& density = citySim->getTrafficDensityMap();
    const int radius = CA::kSeekRadiusTiles;
    const int houseID = getOwner()->getHouseID();

    int candidates = 0;
    Coord chosen = Coord::Invalid();
    // Density cells cover several road tiles. Inspect every tile: sampling
    // only the cell's origin would miss roads on the other coordinate parity.
    for(int y = home.y - radius; y <= home.y + radius; ++y) {
        for(int x = home.x - radius; x <= home.x + radius; ++x) {
            if(!currentGameMap->tileExists(x, y)) continue;
            const Tile* tile = currentGameMap->getTile(x, y);
            if(!tile->isRoad() || tile->getOwner() != houseID) continue;
            if(density.worldGet(x, y) < CA::kHeavyTrafficSteerThreshold) continue;
            ++candidates;
            if(currentGame->randomGen.rand(1, candidates) == 1) chosen = Coord(x, y);
        }
    }

    if(chosen.isValid()) setDestination(chosen);
}

FixPoint AmbientHelicopter::getMaxSpeed() const
{
    return CA::headingSpeed(currentMaxSpeed, drawnAngle,
                            CA::kHelicopterStep, CA::kHelicopterDiagonalStep);
}

void AmbientHelicopter::pickPatrolDestination()
{
    // newSprite(): `destX = getRandom((WORLD_W << 4) - 1)`. The original city
    // fills the whole map, so a random map point is a point over the city.
    // Here the helicopter stays over the city it belongs to, so the draw is
    // taken from its owner's own roads, falling back to a point near the
    // airport when the city has no roads yet.
    if(home.isInvalid()) return;

    const int radius = CA::kSeekRadiusTiles;
    const int houseID = getOwner()->getHouseID();

    int candidates = 0;
    Coord chosen = Coord::Invalid();
    for(int y = home.y - radius; y <= home.y + radius; ++y) {
        for(int x = home.x - radius; x <= home.x + radius; ++x) {
            if(!currentGameMap->tileExists(x, y)) continue;
            const Tile* tile = currentGameMap->getTile(x, y);
            if(!tile->isRoad() || tile->getOwner() != houseID) continue;
            // Reservoir sampling keeps one pass over the city and still draws
            // uniformly, using the shared deterministic simulation RNG.
            ++candidates;
            if(currentGame->randomGen.rand(1, candidates) == 1) chosen = Coord(x, y);
        }
    }

    if(chosen.isInvalid()) {
        const int spread = std::max(1, radius / 3);
        chosen = Coord(home.x + currentGame->randomGen.rand(-spread, spread),
                       home.y + currentGame->randomGen.rand(-spread, spread));
        chosen.x = std::clamp(chosen.x, 0, currentGameMap->getSizeX() - 1);
        chosen.y = std::clamp(chosen.y, 0, currentGameMap->getSizeY() - 1);
    }

    setDestination(chosen);
}

void AmbientHelicopter::reportHeavyTraffic()
{
    // doCopterSprite: reports the traffic density under the helicopter, at most
    // once every 200 sprite ticks and only one time in eight that it is over a
    // congested cell.
    if(reportCooldown > 0) return;

    const auto* citySim = currentGame->getCitySimulation();
    if(citySim == nullptr) return;
    if(!currentGameMap->tileExists(location)) return;

    const Tile* tile = currentGameMap->getTile(location);
    // "Friendly" traffic: the helicopter only reports congestion on a road its
    // own house built. Another player's jam is not its city's problem, and a
    // shared traffic layer must not leak one city's state into another's news.
    if(!tile->isRoad() || tile->getOwner() != getOwner()->getHouseID()) return;

    const int density = citySim->getTrafficDensityMap().worldGet(location.x, location.y);
    if(density <= CA::kHeavyTrafficThreshold) return;
    if(!CA::shouldReportTraffic(density, reportCooldown, currentGame->randomGen.rand())) return;

    reportCooldown = CA::kReportCooldown;
    ++reportCount;

    // res/stri.301 line 41 is "Heavy Traffic reported."; the original also
    // sends the position so the notice can be viewed. Only the owning player
    // hears its own traffic helicopter.
    if(getOwner() == pLocalHouse) {
        currentGame->addToNewsTicker(
            fmt::sprintf(_("Heavy Traffic reported.") + std::string(" (%d, %d)"),
                         location.x, location.y));
    }
}

void AmbientHelicopter::land()
{
    // Original: `sprite->frame = 0` — the helicopter is simply gone, with no
    // explosion and no wreck.
    setVisible(VIS_ALL, false);
    destroy();
}

bool AmbientHelicopter::update()
{
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

    // ---- doCopterSprite, control < 0 (the only branch the copter reaches) ----
    if(reportCooldown > 0) --reportCooldown;
    if(seekCooldown > 0) --seekCooldown;
    if(patrolCount > 0) --patrolCount;

    const Coord position = micropolisPosition();

    if(patrolCount <= 0) {
        // `sprite->destX = sprite->origX; ... if (absDist < 30) { frame = 0; }`
        returningHome = true;
        if(home.isValid()) {
            setDestination(home);
            const Coord target = micropolisDestination();
            if(CA::getDir(position.x, position.y, target.x, target.y).distance
               < CA::kHelicopterLandDistance) {
                land();
                return false;
            }
        } else {
            land();
            return false;
        }
    } else {
        const Coord target = micropolisDestination();
        const bool arrived = destination.isInvalid()
            || CA::getDir(position.x, position.y, target.x, target.y).distance
                   < CA::kHelicopterArriveDistance;
        if(seekCooldown <= 0 || arrived) {
            seekCooldown = CA::kSeekInterval;
            const Coord previousDestination = destination;
            seekHeavyTraffic();
            // Nothing congested in this city: keep patrolling it rather than
            // hovering on the spot the way a one-city original can afford to.
            if(arrived && destination == previousDestination) pickPatrolDestination();
        }
    }

    reportHeavyTraffic();
    return true;
}
