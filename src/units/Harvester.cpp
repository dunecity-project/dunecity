/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <units/Harvester.h>
#include <units/UnitMovementPolicy.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <Explosion.h>
#include <SoundPlayer.h>
#include <ScreenBorder.h>

#include <players/HumanPlayer.h>
#include <players/QuantBot.h>

#include <structures/Refinery.h>
#include <structures/HarvesterDropoff.h>

#include <misc/draw_util.h>

#include <algorithm>
#include <map>
#include <SDL_log.h>

/* how often is the same sandframe redrawn */
#define HARVESTERDELAY 30

/* how often to change harvester position while harvesting */
#define RANDOMHARVESTMOVE 500

/* number spice output frames - 1 */
#define LASTSANDFRAME 2

Harvester::Harvester(House* newOwner) : TrackedUnit(newOwner)
{
    Harvester::init();

    setHealth(getMaxHealth());

    spice = 0;
    harvestingMode = false;
    returningToRefinery = false;
    spiceCheckCounter = 0;
    pathFailCounter = 0;
    returnPathFailCounter = 0;

    attackMode = GUARD;
}

Harvester::Harvester(InputStream& stream) : TrackedUnit(stream)
{
    Harvester::init();

    harvestingMode = stream.readBool();
    returningToRefinery = stream.readBool();
    spice = stream.readFixPoint();
    spiceCheckCounter = stream.readUint32();
    pathFailCounter = 0;  // Runtime counter - not persisted
    returnPathFailCounter = 0;  // Runtime counter - not persisted
}

void Harvester::init()
{
    itemID = Unit_Harvester;
    registerUnit();

    canAttackStuff = false;

    graphicID = ObjPic_Harvester;
    graphic = pGFXManager->getObjPic(graphicID,getOwner()->getHouseID());

    numImagesX = NUM_ANGLES;
    numImagesY = 1;
}

Harvester::~Harvester() = default;

void Harvester::save(OutputStream& stream) const
{
    TrackedUnit::save(stream);
    stream.writeBool(harvestingMode);
    stream.writeBool(returningToRefinery);
    stream.writeFixPoint(spice);
    stream.writeUint32(spiceCheckCounter);
    // pathFailCounter is NOT saved - runtime counter only
}

void Harvester::blitToScreen()
{
    int x = screenborder->world2screenX(realX);
    int y = screenborder->world2screenY(realY);

    SDL_Texture* pUnitGraphic = graphic[currentZoomlevel];
    SDL_Rect source = calcSpriteSourceRect(pUnitGraphic, drawnAngle, numImagesX);
    SDL_Rect dest = calcSpriteDrawingRect( pUnitGraphic, x, y, numImagesX, 1, HAlign::Center, VAlign::Center);

    if(!pGFXManager->drawHDObjPic(graphicID, getOwner()->getHouseID(), currentZoomlevel,
                                  drawnAngle, numImagesX, 0, 1, x, y)) {
        SDL_RenderCopy(renderer, pUnitGraphic, &source, &dest);
    }

    if(isHarvesting() == true) {

        const Coord harvesterSandOffset[] = {   Coord(-56, 4),
                                                Coord(-28, 20),
                                                Coord(0, 24),
                                                Coord(28, 20),
                                                Coord(56, 4),
                                                Coord(40, -24),
                                                Coord(0, -36),
                                                Coord(-36, -24)
                                            };

        SDL_Texture* pSandGraphic = pGFXManager->getZoomedObjPic(ObjPic_Harvester_Sand, getOwner()->getHouseID(), currentZoomlevel);

        int frame = ((currentGame->getGameCycleCount() + (getObjectID() * 10)) / HARVESTERDELAY) % (2*LASTSANDFRAME);
        if(frame > LASTSANDFRAME) {
            frame -= LASTSANDFRAME;
        }

        SDL_Rect sandSource = calcSpriteSourceRect(pSandGraphic, drawnAngle, NUM_ANGLES, frame, LASTSANDFRAME+1);
        SDL_Rect sandDest = calcSpriteDrawingRect(  pSandGraphic,
                                                    screenborder->world2screenX(realX + harvesterSandOffset[drawnAngle].x),
                                                    screenborder->world2screenY(realY + harvesterSandOffset[drawnAngle].y),
                                                    NUM_ANGLES, LASTSANDFRAME+1,
                                                    HAlign::Center, VAlign::Center);

        SDL_RenderCopy(renderer, pSandGraphic, &sandSource, &sandDest);
    }

    if(isBadlyDamaged()) {
        drawSmoke(x, y);
    }
}

void Harvester::checkPos()
{
    TrackedUnit::checkPos();

    // Log LONG-TERM stuck harvesters (30+ seconds) - ANY harvester not moving
    static std::map<Uint32, int> idleLogCounters;
    
    if(active && !moving && !justStoppedMoving) {
        idleLogCounters[getObjectID()]++;
        if(idleLogCounters[getObjectID()] == 1800) { // 30 seconds at 60fps - log once
            SDL_Log("HARVESTER %d STUCK 30s: loc=(%d,%d) dest=(%d,%d) harvestMode=%d returning=%d awaiting=%d hasTarget=%d spice=%.1f respondable=%d attackMode=%d pathSize=%zu",
                    getObjectID(),
                    location.x, location.y,
                    destination.x, destination.y,
                    harvestingMode ? 1 : 0,
                    returningToRefinery ? 1 : 0,
                    awaitingPickup ? 1 : 0,
                    (target.getObjPointer() != nullptr) ? 1 : 0,
                    spice.toFloat(),
                    respondable ? 1 : 0,
                    attackMode,
                    pathList.size());
        }
    } else {
        idleLogCounters[getObjectID()] = 0;
    }

    if(attackMode == STOP) {
        harvestingMode = false;

        // Original/Smart AI rely on this restart after deployment. Only
        // qBot manages safety holds and explicitly resumes safe harvesting.
        const bool managedSafety = std::any_of(getOwner()->getPlayerList().begin(),
            getOwner()->getPlayerList().end(), [](const auto& player) {
                return dynamic_cast<const QuantBot*>(player.get()) != nullptr;
            });
        if (UnitMovementPolicy::resumeStoppedHarvester(getOwner()->isAI(), managedSafety))
            doSetAttackMode(HARVEST);
    }

    if(active)  {
        if (returningToRefinery) {
            if (target && getHarvesterDropoff(target.getObjPointer()) != nullptr) {
                StructureBase* pRefinery = getHarvesterDropoff(target.getObjPointer());
                Tile* pTile = currentGameMap->getTile(location);
                ObjectBase *pObject = pTile->getGroundObject();

                if( justStoppedMoving
                    && (pObject != nullptr)
                    && (pObject->getObjectID() == target.getObjectID()) )
                {
                    if(pRefinery->isHarvesterDropoffFree()) {
                        awaitingPickup = false;
                        setReturned();
                    } else {
                        // the repair yard is already in use by some other unit => move out
                        Coord newDestination = currentGameMap->findDeploySpot(this, target.getObjPointer()->getLocation(), currentGame->randomGen, getLocation(), pRefinery->getStructureSize());
                        doMove2Pos(newDestination, true);
                        requestCarryall();
                    }
                } else if(!awaitingPickup && owner->hasCarryalls() && pRefinery->isHarvesterDropoffFree() && blockDistance(location, pRefinery->getClosestPoint(location)) >= MIN_CARRYALL_LIFT_DISTANCE) {
                    requestCarryall();
                }
                
                // Check if path to refinery is blocked - request carryall if stuck.
                // Waiting for the scheduler to compute a route is not a failure.
                if(!awaitingPickup && !moving && pathList.empty() && destination != location
                   && !hasPendingPathRequest()) {
                    // Not moving, no path, but has a destination - path is likely blocked
                    returnPathFailCounter++;
                    if(returnPathFailCounter >= 3) {
                        if(pRefinery->isHarvesterDropoffFree() && owner->hasCarryalls()) {
                            // Refinery is free but path is blocked - request carryall
                            SDL_Log("HARVESTER %d: Path to refinery blocked, requesting carryall pickup", getObjectID());
                            requestCarryall();
                            returnPathFailCounter = 0;
                        } else if(!pRefinery->isHarvesterDropoffFree()) {
                            // Refinery is occupied - try to find another free refinery
                            StructureBase* pAlternateRefinery = nullptr;
                            FixPoint closestDistance = FixPt32_MAX;
                            
                            for(StructureBase* pStructure : structureList) {
                                if(pStructure->acceptsHarvesterDropoff() && (pStructure->getOwner() == owner)) {
                                    StructureBase* pOtherRefinery = pStructure;
                                    if(pOtherRefinery != pRefinery && pOtherRefinery->isHarvesterDropoffFree()) {
                                        FixPoint dist = blockDistance(location, pOtherRefinery->getClosestPoint(location));
                                        if(dist < closestDistance) {
                                            closestDistance = dist;
                                            pAlternateRefinery = pOtherRefinery;
                                        }
                                    }
                                }
                            }
                            
                            if(pAlternateRefinery) {
                                // Found an alternate free refinery - switch to it
                                SDL_Log("HARVESTER %d: Current refinery occupied, switching to alternate", getObjectID());
                                doMove2Object(pAlternateRefinery);
                                pAlternateRefinery->startHarvesterDropoffAnimation();
                            }
                            // If no alternate found, just wait for current refinery
                            returnPathFailCounter = 0;
                        }
                    }
                } else if(moving || !pathList.empty()) {
                    // Successfully moving or have path - reset counter
                    returnPathFailCounter = 0;
                }


            } else {
                int leastNumBookings = std::numeric_limits<int>::max(); //huge amount so refinery couldn't possibly compete with any refinery num bookings
                FixPoint closestLeastBookedRefineryDistance = FixPt32_MAX;
                StructureBase* pBestRefinery = nullptr;

                for(StructureBase* pStructure : structureList) {
                    if(pStructure->acceptsHarvesterDropoff() && (pStructure->getOwner() == owner)) {
                        StructureBase* pRefinery = pStructure;
                        Coord closestPoint = pRefinery->getClosestPoint(location);
                        FixPoint refineryDistance = blockDistance(location, closestPoint);

                        if (pRefinery->getHarvesterDropoffBookings() < leastNumBookings) {
                            leastNumBookings = pRefinery->getHarvesterDropoffBookings();
                            closestLeastBookedRefineryDistance = refineryDistance;
                            pBestRefinery = pRefinery;
                        } else if (pRefinery->getHarvesterDropoffBookings() == leastNumBookings) {
                            if (refineryDistance < closestLeastBookedRefineryDistance) {
                                closestLeastBookedRefineryDistance = refineryDistance;
                                pBestRefinery = pRefinery;
                            }
                        }
                    }
                }

                if (pBestRefinery) {
                    doMove2Object(pBestRefinery);
                    pBestRefinery->startHarvesterDropoffAnimation();
                } else {
                    setDestination(location);
                }
            }
        } else if (harvestingMode && !hasBookedCarrier() && destination.isValid() && (blockDistance(location, destination) >= MIN_CARRYALL_LIFT_DISTANCE)) {
            requestCarryall();
        } else if(harvestingMode && destination != location && pathList.empty() && !moving) {
            // Stuck in harvesting mode with unreachable destination
            // Give pathfinding a few cycles to resolve, then try carryall or give up
            pathFailCounter++;
            if(pathFailCounter >= 3) {
                if(!hasBookedCarrier() && owner->hasCarryalls() && blockDistance(location, destination) >= MIN_CARRYALL_LIFT_DISTANCE) {
                    // Path blocked but carryalls available - request pickup to spice
                    SDL_Log("HARVESTER %d: Path to spice blocked, requesting carryall pickup", getObjectID());
                    requestCarryall();
                    pathFailCounter = 0;
                } else if(!owner->hasCarryalls()) {
                    // No carryalls available - give up on this spice location
                    harvestingMode = false;
                    setDestination(location);
                    pathFailCounter = 0;
                }
                // If already has a booked carrier, just wait
            }
        } else if(harvestingMode && (!pathList.empty() || moving)) {
            // Successfully moving or have path - reset counter
            pathFailCounter = 0;
        } else if(respondable && !harvestingMode && attackMode != STOP) {
            if(spiceCheckCounter == 0) {
                // If stuck with unreachable destination, reset guardPoint to try new area
                if(destination != location && pathList.empty()) {
                    setGuardPoint(location);
                }
                
                // Find harvest location nearest to our base
                Coord newDestination;
                if(currentGameMap->findSpice(newDestination, guardPoint)) {
                    setDestination(newDestination);
                    setGuardPoint(newDestination);
                    harvestingMode = true;
                    pathFailCounter = 0;  // Reset for new destination
                } else {
                    setDestination(location);
                    setGuardPoint(location);
                    harvestingMode = false;
                    pathFailCounter = 0;
                }
                spiceCheckCounter = 100;
            } else {
                spiceCheckCounter--;
            }
        }
    }
}

void Harvester::deploy(const Coord& newLocation)
{
    if(currentGameMap->tileExists(newLocation)) {
        TrackedUnit::deploy(newLocation);
        if(spice == 0) {
            Coord newDestination;
            if((attackMode != STOP) && currentGameMap->findSpice(newDestination, guardPoint)) {
                harvestingMode = true;
                setDestination(newDestination);
                setGuardPoint(newDestination);

            } else {
                harvestingMode = false;
            }
        }
    }
}

void Harvester::destroy()
{
    if(currentGameMap->tileExists(location) && isVisible()) {
        int xpos = location.x;
        int ypos = location.y;

        if(currentGameMap->tileExists(xpos,ypos)) {
            const auto spiceSpreaded = spice * 0.75_fix;
            const auto generatedSpiceTerrain = currentGameMap->chooseGeneratedSpiceTerrain();
            int availableSandPos = 0;

            int circleRadius = lround(spice / 210);

            /* how many regions have sand */
            for(int i = -circleRadius; i <= circleRadius; i++) {
                for(int j = -circleRadius; j <= circleRadius; j++) {
                    if(currentGameMap->tileExists(xpos + i, ypos + j)
                        && (distanceFrom(xpos, ypos, xpos + i, ypos + j) + 0.0005_fix <= circleRadius))
                    {
                        Tile *pTile = currentGameMap->getTile(xpos + i, ypos + j);
                        if((pTile != nullptr) & ((pTile->isSand()) || (pTile->isSpice()) )) {
                            availableSandPos++;
                        }
                    }
                }
            }

            /* now we can spread spice */
            for(int i = -circleRadius; i <= circleRadius; i++) {
                for(int j = -circleRadius; j <= circleRadius; j++) {
                    if(currentGameMap->tileExists(xpos + i, ypos + j)
                        && (distanceFrom(xpos, ypos, xpos + i, ypos + j) + 0.0005_fix  <= circleRadius))
                    {
                        Tile *pTile = currentGameMap->getTile(xpos + i, ypos + j);
                        if((pTile != nullptr) & ((pTile->isSand()) || (pTile->isSpice()) )) {
                            const auto oldTileSpice = pTile->getSpice();
                            if(pTile->isSand()
                               || pTile->getType() == Terrain_Spice
                               || pTile->getType() == Terrain_ThickSpice) {
                                pTile->setType(generatedSpiceTerrain.first);
                            }
                            pTile->setSpice(oldTileSpice + spiceSpreaded / availableSandPos);
                        }
                    }
                }
            }
        }

        setTarget(nullptr);

        Coord realPos(lround(realX), lround(realY));
        Uint32 explosionID = currentGame->randomGen.getRandOf({Explosion_Medium1, Explosion_Medium2});
        currentGame->getExplosionList().push_back(new Explosion(explosionID, realPos, owner->getHouseID()));

        if(isVisible(getOwner()->getTeamID())) {
            screenborder->shakeScreen(18);
            soundPlayer->playSoundAt(Sound_ExplosionLarge,location);
        }
    }

    TrackedUnit::destroy();
}

void Harvester::drawSelectionBox()
{
    SDL_Texture* selectionBox = nullptr;

    switch(currentZoomlevel) {
        case 0:     selectionBox = pGFXManager->getUIGraphic(UI_SelectionBox_Zoomlevel0);   break;
        case 1:     selectionBox = pGFXManager->getUIGraphic(UI_SelectionBox_Zoomlevel1);   break;
        case 2:
        default:    selectionBox = pGFXManager->getUIGraphic(UI_SelectionBox_Zoomlevel2);   break;
    }

    SDL_Rect dest = calcDrawingRect(selectionBox, screenborder->world2screenX(realX), screenborder->world2screenY(realY), HAlign::Center, VAlign::Center);
    SDL_RenderCopy(renderer, selectionBox, nullptr, &dest);

    for(int i=1;i<=currentZoomlevel+1;i++) {
        renderDrawHLine(renderer, dest.x+1, dest.y-i, dest.x+1 + (lround((getHealth()/getMaxHealth())*(getWidth(selectionBox)-3))), getHealthColor());
    }

    if((getOwner() == pLocalHouse) && (spice > 0)) {
        for(int i=1;i<=currentZoomlevel+1;i++) {
            renderDrawHLine(renderer, dest.x+1, dest.y-i-(currentZoomlevel+1), dest.x+1 + (lround(((spice)/HARVESTERMAXSPICE)*(getWidth(selectionBox)-3))), COLOR_ORANGE);
        }
    }
}

void Harvester::handleDamage(int damage, Uint32 damagerID, House* damagerOwner,
                                const DeviationReward::Provenance& provenance)
{
    TrackedUnit::handleDamage(damage, damagerID, damagerOwner, provenance);

    ObjectBase* damager = currentGame->getObjectManager().getObject(damagerID);

    if(!target && !forced && damager && canAttack(damager) && (attackMode != STOP)) {
        setTarget(damager);
    }
}

void Harvester::handleReturnClick() {
    currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_HARVESTER_RETURN,objectID));
}

void Harvester::doReturn()
{
    if(!returningToRefinery && isActive()) {
        returningToRefinery = true;
        harvestingMode = false;

        if(getAttackMode() == STOP) {
            setGuardPoint(Coord::Invalid());
        }
    }
}

void Harvester::setAmountOfSpice(FixPoint newSpice)
{
    if((newSpice >= 0) && (newSpice <= HARVESTERMAXSPICE)) {
        spice = newSpice;
    }
}

void Harvester::setDestination(int newX, int newY)
{
    TrackedUnit::setDestination(newX, newY);

    harvestingMode =  (attackMode != STOP) && (currentGameMap->tileExists(newX, newY) && currentGameMap->getTile(newX,newY)->hasSpice());
}

void Harvester::setTarget(const ObjectBase* newTarget)
{
    if(returningToRefinery && target)
    {
        if(StructureBase* dropoff = getHarvesterDropoff(target.getObjPointer())) {
            dropoff->unbookHarvesterDropoff();
        }
        returningToRefinery = false;
    }

    TrackedUnit::setTarget(newTarget);

    if(target && (target.getObjPointer() != nullptr)
        && (target.getObjPointer()->getOwner() == getOwner()))
    {
        if(StructureBase* dropoff = getHarvesterDropoff(target.getObjPointer())) {
            dropoff->bookHarvesterDropoff();
            returningToRefinery = true;
        }
    }

}

void Harvester::setReturned()
{
    StructureBase* dropoff = getHarvesterDropoff(target.getObjPointer());
    if(dropoff == nullptr) {
        returningToRefinery = false;
        return;
    }

    const bool storedInside = dropoff->receiveHarvester(this);
    returningToRefinery = false;
    moving = false;
    awaitingPickup = false;

    if(!storedInside) {
        // A non-storing drop-off must let the harvester leave the occupied
        // structure tiles before resuming its harvesting cycle.
        Coord deployPos = currentGameMap->findDeploySpot(
            this, dropoff->getLocation(), currentGame->randomGen,
            getGuardPoint(), dropoff->getStructureSize());
        currentGameMap->removeObjectFromMap(getObjectID());
        TrackedUnit::setTarget(nullptr);
        deploy(deployPos);
        respondable = true;
        setVisible(VIS_ALL, true);
        setDestination(deployPos);
        doSetAttackMode(HARVEST);
        return;
    }

    if(selected) {
        removeFromSelectionLists();
    }

    currentGameMap->removeObjectFromMap(getObjectID());
    respondable = false;
    setActive(false);
    setLocation(INVALID_POS, INVALID_POS);
    setVisible(VIS_ALL, false);
}

void Harvester::move()
{
    TrackedUnit::move();
    
    // Log harvesters sitting on spice tiles
    static std::map<Uint32, int> onSpiceCounter;
    // Track if harvester is on spice but not moving (removed spammy logging)

    if(active && !moving && !justStoppedMoving) {
        if(harvestingMode) {
            // Track stuck harvesters (removed spammy 5-second logging)
            if(location == destination) {
                if(spice < HARVESTERMAXSPICE) {

                    Tile* tile = currentGameMap->getTile(location);

                    if(tile->hasSpice()) {

                        int beforeTileType = tile->getType();
                        spice += tile->harvestSpice();
                        int afterTileType = tile->getType();

                        if(beforeTileType != afterTileType) {
                            currentGameMap->spiceRemoved(location);
                            if(!currentGameMap->findSpice(destination, location)) {
                                doReturn();
                            } else {
                                doMove2Pos(destination, false);
                            }
                        }
                    } else if (!currentGameMap->findSpice(destination, location)) {
                        if(spice > 0) {
                            doReturn();
                        } else {
                            // No spice anywhere and we're empty - stop harvesting mode so checkPos can search again
                            harvestingMode = false;
                        }
                    } else {
                        // Found spice elsewhere, try to move there
                        doMove2Pos(destination, false);
                    }
                } else {
                    doReturn();
                }
            } else if(pathList.empty()) {
                // Stuck: in harvesting mode with a destination, but can't path there.
                // If a carryall is coming to deliver us, preserve harvestingMode so
                // checkPos() doesn't reset guardPoint/destination back to the old tile.
                if(!awaitingPickup) {
                    harvestingMode = false;
                }
            }
        }
    }
}

bool Harvester::isHarvesting() const {
    return  harvestingMode
            && (spice < HARVESTERMAXSPICE)
            && (blockDistance(location, destination) <= FixPt_SQRT2)
            && currentGameMap->tileExists(location) && currentGameMap->getTile(location)->hasSpice();
}

bool Harvester::canAttack(const ObjectBase* object) const
{
    return((object != nullptr)
            && object->isInfantry()
            && (object->getOwner()->getTeamID() != owner->getTeamID())
            && object->isVisible(getOwner()->getTeamID()));
}

FixPoint Harvester::extractSpice(FixPoint extractionSpeed)
{
    FixPoint oldSpice = spice;

    if((spice - extractionSpeed) >= 0) {
        spice -= extractionSpeed;
    } else {
        spice = 0;
    }

    return (oldSpice - spice);
}

void Harvester::setSpeeds()
{
    const int cargoPercent = std::clamp((spice*100/HARVESTERMAXSPICE).floor(),0,100);
    FixPoint speed = getTerrainAdjustedSpeed(cargoPercent);
    if(itemID != Unit_Harvester) {
        // Preserve the explicit behavior of mod-only harvester derivatives.
        speed = getMaxSpeed() * (1-0.4_fix*spice/HARVESTERMAXSPICE);
        if(isBadlyDamaged()) speed *= HEAVILYDAMAGEDSPEEDMULTIPLIER;
        if(currentGameMap->getTile(location)->isRoad()) speed *= ROADSPEEDMULTIPLIER;
    }

    switch(drawnAngle){
        case LEFT:      xSpeed = -speed;                    ySpeed = 0;         break;
        case LEFTUP:    xSpeed = -speed*DIAGONALSPEEDCONST; ySpeed = xSpeed;    break;
        case UP:        xSpeed = 0;                         ySpeed = -speed;    break;
        case RIGHTUP:   xSpeed = speed*DIAGONALSPEEDCONST;  ySpeed = -xSpeed;   break;
        case RIGHT:     xSpeed = speed;                     ySpeed = 0;         break;
        case RIGHTDOWN: xSpeed = speed*DIAGONALSPEEDCONST;  ySpeed = xSpeed;    break;
        case DOWN:      xSpeed = 0;                         ySpeed = speed;     break;
        case LEFTDOWN:  xSpeed = -speed*DIAGONALSPEEDCONST; ySpeed = -xSpeed;   break;
    }
}

// These counters affect refinery selection and carryall requests. Ordinary
// saves may reset them, but a live viewer must replay the exact same decisions.
void Harvester::saveObserverRuntime(OutputStream& stream) const {
    UnitBase::saveObserverRuntime(stream);
    stream.writeUint8(pathFailCounter);
    stream.writeUint8(returnPathFailCounter);
}
void Harvester::loadObserverRuntime(InputStream& stream) {
    UnitBase::loadObserverRuntime(stream);
    pathFailCounter=stream.readUint8();
    returnPathFailCounter=stream.readUint8();
}
