#include <players/AIDecisionLog.h>
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

#include <units/UnitBase.h>
#include <units/DynastyMovement.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>

#include <SoundPlayer.h>
#include <Map.h>
#include <SpatialGrid.h>
#include <Bullet.h>
#include <ScreenBorder.h>
#include <House.h>
#include <audio/sounds.h>

#include <players/HumanPlayer.h>

#include <misc/draw_util.h>

#include <AStarSearch.h>

#include <GUI/ObjectInterfaces/UnitInterface.h>

#include <structures/Refinery.h>
#include <structures/HarvesterDropoff.h>
#include <structures/RepairYard.h>
#include <units/Harvester.h>
#include <units/HarvesterHelpers.h>
#include <units/GroundUnit.h>

#define SMOKEDELAY 30
#define UNITIDLETIMER (GAMESPEED_DEFAULT *  315)  // about every 5s

UnitBase::UnitBase(House* newOwner) : ObjectBase(newOwner) {

    UnitBase::init();

    drawnAngle = currentGame->randomGen.rand(0, 7);
    angle = drawnAngle;

    goingToRepairYard = false;
    pickedUp = false;
    bFollow = false;
    guardPoint = Coord::Invalid();
    attackPos = Coord::Invalid();

    moving = false;
    turning = false;
    justStoppedMoving = false;
    xSpeed = 0;
    ySpeed = 0;
    bumpyOffsetX = 0;
    bumpyOffsetY = 0;

    targetDistance = 0;
    targetAngle = INVALID;

    noCloserPointCount = 0;
    nextSpotFound = false;
    nextSpotAngle = drawnAngle;
    recalculatePathTimer = 0;
    nextSpot = Coord::Invalid();
    cachedPathDestination.invalidate();
    cachedPathRevision = 0;

    findTargetTimer = 0;
    primaryWeaponTimer = 0;
    secondaryWeaponTimer = INVALID;

    deviationTimer = INVALID;
}

UnitBase::UnitBase(InputStream& stream) : ObjectBase(stream) {
    restoredFromSave = true;

    UnitBase::init();

    stream.readBools(&goingToRepairYard, &pickedUp, &bFollow);
    guardPoint.x = stream.readSint32();
    guardPoint.y = stream.readSint32();
    attackPos.x = stream.readSint32();
    attackPos.y = stream.readSint32();

    stream.readBools(&moving, &turning, &justStoppedMoving);
    xSpeed = stream.readFixPoint();
    ySpeed = stream.readFixPoint();
    bumpyOffsetX = stream.readFixPoint();
    bumpyOffsetY = stream.readFixPoint();

    targetDistance = stream.readFixPoint();
    targetAngle = stream.readSint8();

    noCloserPointCount = stream.readUint8();
    nextSpotFound = stream.readBool();
    nextSpotAngle = stream.readSint8();
    recalculatePathTimer = stream.readSint32();
    nextSpot.x = stream.readSint32();
    nextSpot.y = stream.readSint32();
    int numPathNodes = stream.readUint32();
    for(int i=0;i<numPathNodes; i++) {
        Sint32 x = stream.readSint32();
        Sint32 y = stream.readSint32();
        pathList.emplace_back(x,y);
    }
    // Objects load in ID order. Resolving a forward target here would clear
    // its ObjectPointer before that target has been constructed.
    cachedPathDestination = destination;
    if(currentGame->getObjectManager().getObject(target.getObjectID()) != nullptr)
        cachedPathDestination = resolvePathDestination();
    cachedPathRevision = (currentGameMap != nullptr) ? currentGameMap->getPathingRevision() : 0;
    
    // Stuck detection fields are transient (not saved) - they reset on load

    findTargetTimer = stream.readSint32();
    primaryWeaponTimer = stream.readSint32();
    secondaryWeaponTimer = stream.readSint32();

    deviationTimer = stream.readSint32();
    if(currentGame->getLoadedSavegameVersion() >= 9844) {
        dynastyRouteTick=stream.readUint64();
        dynastyStepStart=stream.readUint64(); dynastyStepEnd=stream.readUint64();
        dynastyStartX=stream.readFixPoint(); dynastyStartY=stream.readFixPoint();
        dynastyEndpoint.x=stream.readSint32(); dynastyEndpoint.y=stream.readSint32();
    }


    // A spectator restores the pending work queues as well as the saved
    // units. Keep their negative queued-work sentinels until that work runs.
    if(findTargetTimer < 0 && !currentGame->isSpectating()) {
        findTargetTimer = 0;
    }
    if(recalculatePathTimer < 0 && !currentGame->isSpectating()) {
        recalculatePathTimer = 0;
    }
}

void UnitBase::registerUnit() {
    currentGame->getHouse(originalHouseID)->incrementUnits(itemID, !restoredFromSave);
}

void UnitBase::init() {
    aUnit = true;
    canAttackStuff = true;

    tracked = false;
    turreted = false;
    numWeapons = 0;
    bulletType = Bullet_DRocket;
    lastFiredBulletType = Bullet_DRocket;

    drawnFrame = 0;

    pendingTargetRequest = TargetRequestKind::None;
    pathRequestQueued = false;

    unitList.push_back(this);
}

UnitBase::~UnitBase() {
    pathList.clear();
    removeFromSelectionLists();

    for(int i=0; i < NUMSELECTEDLISTS; i++) {
        pLocalPlayer->getGroupList(i).erase(objectID);
    }

    currentGame->getObjectManager().removeObject(objectID);
    unitList.remove(this);
}


void UnitBase::save(OutputStream& stream) const {

    ObjectBase::save(stream);

    stream.writeBools(goingToRepairYard, pickedUp, bFollow);
    stream.writeSint32(guardPoint.x);
    stream.writeSint32(guardPoint.y);
    stream.writeSint32(attackPos.x);
    stream.writeSint32(attackPos.y);

    stream.writeBools(moving, turning, justStoppedMoving);
    stream.writeFixPoint(xSpeed);
    stream.writeFixPoint(ySpeed);
    stream.writeFixPoint(bumpyOffsetX);
    stream.writeFixPoint(bumpyOffsetY);

    stream.writeFixPoint(targetDistance);
    stream.writeSint8(targetAngle);

    stream.writeUint8(noCloserPointCount);
    stream.writeBool(nextSpotFound);
    stream.writeSint8(nextSpotAngle);
    stream.writeSint32(recalculatePathTimer);
    stream.writeSint32(nextSpot.x);
    stream.writeSint32(nextSpot.y);
    stream.writeUint32(pathList.size());
    for(const Coord& coord : pathList) {
        stream.writeSint32(coord.x);
        stream.writeSint32(coord.y);
    }
    
    // Stuck detection fields are transient (not saved)

    stream.writeSint32(findTargetTimer);
    stream.writeSint32(primaryWeaponTimer);
    stream.writeSint32(secondaryWeaponTimer);

    stream.writeSint32(deviationTimer);
    stream.writeUint64(dynastyRouteTick);
    stream.writeUint64(dynastyStepStart); stream.writeUint64(dynastyStepEnd);
    stream.writeFixPoint(dynastyStartX); stream.writeFixPoint(dynastyStartY);
    stream.writeSint32(dynastyEndpoint.x); stream.writeSint32(dynastyEndpoint.y);
}

// Network-only continuation state. Ordinary saved games deliberately reset this.
void UnitBase::saveObserverRuntime(OutputStream& s) const {
    s.writeUint8(static_cast<Uint8>(pendingTargetRequest)); s.writeBool(pathRequestQueued);
    s.writeSint32(cachedPathDestination.x); s.writeSint32(cachedPathDestination.y);
    s.writeUint32(cachedPathRevision); s.writeFixPoint(lastDistanceToDestination);
    s.writeUint8(noProgressCount); s.writeSint32(carryallRequestCooldown);
}
void UnitBase::loadObserverRuntime(InputStream& s) {
    const auto kind=s.readUint8();
    if(kind>static_cast<Uint8>(TargetRequestKind::Acquire)) throw std::runtime_error("Invalid target request");
    pendingTargetRequest=static_cast<TargetRequestKind>(kind); pathRequestQueued=s.readBool();
    cachedPathDestination.x=s.readSint32(); cachedPathDestination.y=s.readSint32();
    cachedPathRevision=s.readUint32(); lastDistanceToDestination=s.readFixPoint();
    noProgressCount=s.readUint8(); carryallRequestCooldown=s.readSint32();
}

bool UnitBase::usesLauncherRocketPair() const {
    return itemID == Unit_Launcher || itemID == Unit_EliteLauncher;
}

bool UnitBase::canFireSecondaryWeapon() const {
    if(usesLauncherRocketPair()) {
        // Dynasty fires the second rocket only above half health, so a launcher at
        // exactly 50% already fires singles. isBadlyDamaged() is strictly below half
        // and keeps that meaning everywhere else in the engine.
        return getHealth()*2 > getMaxHealth();
    }
    return !isBadlyDamaged();
}

bool UnitBase::attack() {

    if(numWeapons) {
        if((primaryWeaponTimer == 0) || ((numWeapons == 2) && (secondaryWeaponTimer == 0) && canFireSecondaryWeapon())) {

            Coord targetCenterPoint;
            Coord centerPoint = getCenterPoint();
            bool bAirBullet;

            ObjectBase* pObject = target.getObjPointer();
            if(pObject != nullptr) {
                targetCenterPoint = pObject->getClosestCenterPoint(location);
                bAirBullet = pObject->isAFlyingUnit();
            } else {
                targetCenterPoint = currentGameMap->getTile(attackPos)->getCenterPoint();
                bAirBullet = false;
            }

            int currentBulletType = bulletType;
            Sint32 currentWeaponDamage = currentGame->objectData.data[itemID][originalHouseID].weapondamage;

            if(getItemID() == Unit_Trooper && !bAirBullet) {
                // Troopers change weapon type depending on distance

                FixPoint distance = distanceFrom(centerPoint, targetCenterPoint);
                if(distance <= 2*TILESIZE) {
                    currentBulletType = Bullet_ShellSmall;
                } else {
                    currentWeaponDamage -= currentWeaponDamage/4;
                }
            } 
            // Dynasty: Launchers and Deviators use same rocket type for both ground and air
            // Air targets double the arming/steering counter; the impact destination stays fixed.

            if(primaryWeaponTimer == 0) {
                // MULTIPLAYER-SAFE: Track launcher unit firing at ornithopters
                if((getItemID() == Unit_Launcher || getItemID() == Unit_Deviator) && 
                   pObject && pObject->getItemID() == Unit_Ornithopter) {
                    currentGame->combatStats.launcherFiresOnOrni++;
                    currentGame->combatStats.launcherRocketsSpawned++;
                }
                
                bulletList.push_back( new Bullet( objectID, &centerPoint, &targetCenterPoint, currentBulletType, currentWeaponDamage, bAirBullet, pObject) );
                if(pObject != nullptr) {
                    currentGameMap->viewMap(pObject->getOwner()->getHouseID(), location, 2);
                }
                lastFiredBulletType = currentBulletType;
                enhancedCombatAnimationStartMs = currentGame->getGameTime();
                enhancedRenderInteractionKey = -1;
                playAttackSound();
                // A launcher restarts this timer on its second rocket; arming it here
                // too means an interrupted pair still reloads instead of stalling.
                primaryWeaponTimer = getWeaponReloadTime();

                secondaryWeaponTimer = usesLauncherRocketPair() ? launcherBurstGapCycles
                                                                : defaultSecondaryWeaponCycles;

                if(attackPos && getItemID() != Unit_SonicTank && currentGameMap->getTile(attackPos)->isSpiceBloom()) {
                    setDestination(location);
                    forced = false;
                    attackPos.invalidate();
                }

                // shorten deviation time
                if(deviationTimer > 0) {
                    deviationTimer = std::max(0,deviationTimer - MILLI2CYCLES(20*1000));
                }
            }

            if((numWeapons == 2) && (secondaryWeaponTimer == 0) && canFireSecondaryWeapon()) {
                // MULTIPLAYER-SAFE: Track launcher unit secondary weapon firing at ornithopters
                if((getItemID() == Unit_Launcher || getItemID() == Unit_Deviator) && 
                   pObject && pObject->getItemID() == Unit_Ornithopter) {
                    currentGame->combatStats.launcherFiresOnOrni++;
                    currentGame->combatStats.launcherRocketsSpawned++;
                }
                
                bulletList.push_back( new Bullet( objectID, &centerPoint, &targetCenterPoint, currentBulletType, currentWeaponDamage, bAirBullet, pObject) );
                if(pObject != nullptr) {
                    currentGameMap->viewMap(pObject->getOwner()->getHouseID(), location, 2);
                }
                lastFiredBulletType = currentBulletType;
                enhancedCombatAnimationStartMs = currentGame->getGameTime();
                enhancedRenderInteractionKey = -1;
                playAttackSound();
                secondaryWeaponTimer = INVALID;

                if(usesLauncherRocketPair()) {
                    // Dynasty's long launcher delay starts on the second rocket of the
                    // pair, not the first, so the next pair is a full reload away.
                    primaryWeaponTimer = getWeaponReloadTime();
                }

                if(attackPos && getItemID() != Unit_SonicTank && currentGameMap->getTile(attackPos)->isSpiceBloom()) {
                    setDestination(location);
                    forced = false;
                    attackPos.invalidate();
                }

                // shorten deviation time
                if(deviationTimer > 0) {
                    deviationTimer = std::max(0,deviationTimer - MILLI2CYCLES(20*1000));
                }
            }

            return true;
        }
    }
    return false;
}

bool UnitBase::drawEnhancedUnitSprite(int x, int y, int idleDirection,
                                      int combatDirection) {
    const Uint32 nowMs = currentGame->getGameTime();
    auto state = (moving || turning)
        ? GFXManager::EnhancedUnitState::Movement
        : GFXManager::EnhancedUnitState::Idle;
    int renderDirection = drawnAngle;
    if(state == GFXManager::EnhancedUnitState::Idle
       && idleDirection >= 0 && idleDirection < NUM_ANGLES) {
        renderDirection = idleDirection;
    }
    Uint32 elapsedMs = nowMs + getObjectID() * 97u;

    if(enhancedCombatAnimationStartMs != std::numeric_limits<Uint32>::max()) {
        const int firingDirection = combatDirection >= 0 && combatDirection < NUM_ANGLES
            ? combatDirection
            : renderDirection;
        const Uint32 combatDuration = pGFXManager->getEnhancedUnitAnimationDuration(
            getItemID(), getOwner()->getHouseID(), GFXManager::EnhancedUnitState::Combat,
            firingDirection);
        const Uint32 combatElapsed = nowMs - enhancedCombatAnimationStartMs;
        if(combatDuration > 0 && combatElapsed < combatDuration) {
            state = GFXManager::EnhancedUnitState::Combat;
            renderDirection = firingDirection;
            elapsedMs = combatElapsed;
        } else {
            enhancedCombatAnimationStartMs = std::numeric_limits<Uint32>::max();
        }
    }

    if(!pGFXManager->hasEnhancedUnitAnimation(getItemID(), getOwner()->getHouseID(),
                                              state, renderDirection)) {
        return false;
    }

    const auto renderMode = pGFXManager->getEnhancedUnitRenderMode(
        getItemID(), getOwner()->getHouseID(), state, renderDirection);
    if(renderMode == GFXManager::EnhancedRenderMode::Layered) {
        return false;
    }

    if(renderMode == GFXManager::EnhancedRenderMode::Random) {
        const int interactionKey = static_cast<int>(state) * NUM_ANGLES + renderDirection;
        if(interactionKey != enhancedRenderInteractionKey) {
            enhancedRenderInteractionKey = interactionKey;
            ++enhancedRenderInteractionSequence;
            Uint32 hash = getObjectID() * 2654435761u;
            hash ^= enhancedRenderInteractionSequence * 2246822519u;
            hash ^= static_cast<Uint32>(interactionKey + 1) * 3266489917u;
            hash ^= hash >> 16;
            enhancedRenderInteractionUsesFull = (hash & 1u) != 0;
        }
        if(!enhancedRenderInteractionUsesFull) {
            return false;
        }
    } else {
        enhancedRenderInteractionKey = -1;
    }

    return pGFXManager->drawEnhancedUnit(getItemID(), getOwner()->getHouseID(),
                                         currentZoomlevel, state, renderDirection,
                                         elapsedMs, x, y);
}

void UnitBase::blitToScreen() {
    int x = screenborder->world2screenX(realX);
    int y = screenborder->world2screenY(realY);

    // Sprite textures are invalidated when a game or mod is loaded. The Sonic
    // Trike may already exist at that point (notably on loaded custom maps), so
    // refresh its cached raw pointers before drawing it.
    if(getItemID() == Unit_SonicTrike) {
        graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    }

    SDL_Texture* pUnitGraphic = graphic[currentZoomlevel];
    if(pUnitGraphic == nullptr) {
        return;
    }
    SDL_Rect source = calcSpriteSourceRect(pUnitGraphic, drawnAngle, numImagesX, drawnFrame, numImagesY);
    SDL_Rect dest = calcSpriteDrawingRect( pUnitGraphic, x, y, numImagesX, numImagesY, HAlign::Center, VAlign::Center);

    const Uint8 dune2rBlend = pGFXManager->getDune2RVisualBlend();
    bool classicDrawn = false;
    if(dune2rBlend > 0 && dune2rBlend < SDL_ALPHA_OPAQUE) {
        SDL_RenderCopy(renderer, pUnitGraphic, &source, &dest);
        classicDrawn = true;
    }

    if(!drawEnhancedUnitSprite(x, y)
       && !pGFXManager->drawHDObjPic(graphicID, getOwner()->getHouseID(), currentZoomlevel,
                                  drawnAngle, numImagesX, drawnFrame, numImagesY, x, y)
       && !classicDrawn) {
        SDL_RenderCopy(renderer, pUnitGraphic, &source, &dest);
    }

    if(isBadlyDamaged()) {
        drawSmoke(x, y);
    }
}

ObjectInterface* UnitBase::getInterfaceContainer() {
    if((pLocalHouse == owner && isRespondable()) || (debug == true)) {
        return UnitInterface::create(objectID);
    } else {
        return DefaultObjectInterface::create(objectID);
    }
}

int UnitBase::getCurrentAttackAngle() const {
    return drawnAngle;
}

void UnitBase::deploy(const Coord& newLocation) {

    if(currentGameMap->tileExists(newLocation)) {
        setLocation(newLocation);

        if (guardPoint.isInvalid())
            guardPoint = location;
        setDestination(guardPoint);
        pickedUp = false;
        setRespondable(true);
        setActive(true);
        setVisible(VIS_ALL, true);
        setForced(false);

        // Deployment logic to hopefully stop units freezing
        if (getAttackMode() == CARRYALLREQUESTED || getAttackMode() == HUNT) {
            if(isHarvesterLikeUnit(getItemID())) {
                doSetAttackMode(HARVEST);
            } else {
                doSetAttackMode(GUARD);
            }
        }

        if(isAGroundUnit() && (getItemID() != Unit_Sandworm)) {
            if(currentGameMap->getTile(location)->isSpiceBloom()) {
                currentGameMap->getTile(location)->triggerSpiceBloom(getOwner());
                
                // Check if unit should be destroyed by the bloom
                GameType gameType = currentGame->getGameInitSettings().getGameType();
                bool isImmortal = (!isNetworkGameType(gameType)
                                  && gameType != GameType::LoadMultiplayer
                                  && currentGame->getGameInitSettings().getGameOptions().immortalHumanPlayer
                                  && getOwner() == pLocalHouse);
                
                if(!isImmortal) {
                    setHealth(0);
                    setVisible(VIS_ALL, false);
                }
            } else if(currentGameMap->getTile(location)->isSpecialBloom()){
                currentGameMap->getTile(location)->triggerSpecialBloom(getOwner());
            }
        }

        if(pLocalHouse == getOwner()) {
            pLocalPlayer->onUnitDeployed(this);
        }
    }
}

void UnitBase::cancelDeployment() {
    setTarget(nullptr);

    if(currentGameMap != nullptr) {
        currentGameMap->removeObjectFromMap(getObjectID());
    }
    currentGame->getObjectManager().removeObject(objectID);
    currentGame->getHouse(originalHouseID)->cancelCreatedUnit(itemID);
    unitList.remove(this);

    delete this;
}

void UnitBase::destroy() {
    if (currentGame && owner) AITelemetry::log().write(currentGame->getGameCycleCount(), owner->getHouseID(), -1,
        "object_destroyed", AITelemetry::Record().set("object", objectID).set("item", itemID)
            .set("x", location.x).set("y", location.y).set("health", getHealth().lround()));

    setTarget(nullptr);
    currentGameMap->removeObjectFromMap(getObjectID()); //no map point will reference now
    currentGame->getObjectManager().removeObject(objectID);

    currentGame->getHouse(originalHouseID)->decrementUnits(itemID);

    unitList.remove(this);

    if(isVisible()) {
        if(currentGame->randomGen.rand(1,100) <= getInfSpawnProp()) {
            UnitBase* pNewUnit = currentGame->getHouse(originalHouseID)->createUnit(Unit_Soldier);
            pNewUnit->setHealth(pNewUnit->getMaxHealth()/2);
            pNewUnit->deploy(location);

            if(owner->getHouseID() != originalHouseID) {
                // deviation is inherited
                pNewUnit->owner = owner;
                pNewUnit->graphic = pGFXManager->getObjPic(pNewUnit->graphicID,owner->getHouseID());
                pNewUnit->deviationTimer = deviationTimer;
            }
        }
    }

    delete this;
}

void UnitBase::deviate(House* newOwner) {

    if(newOwner->getHouseID() == originalHouseID) {
        quitDeviation();
    } else {
        removeFromSelectionLists();
        setTarget(nullptr);
        setGuardPoint(location);
        setDestination(location);
        clearPath();
        doSetAttackMode(GUARD);
        owner = newOwner;

        graphic = pGFXManager->getObjPic(graphicID,getOwner()->getHouseID());
        deviationTimer = DEVIATIONTIME;

        // The interrupted repair trip does not survive the conversion. setTarget(nullptr) above
        // already released the repair-yard booking; the carryall booked for the same trip has to
        // go with it, otherwise the unit waits for a lift its old owner will never send and
        // GroundUnit::navigate() keeps it parked for the whole deviation.
        if(isAGroundUnit()) {
            static_cast<GroundUnit*>(this)->cancelCarryallPickup();
        }
    }

    // Keep conversion reward in the same credit units as damage value, without
    // awarding a killing-blow bonus for a unit that was not destroyed.
    CombatReward::Totals conversionReward;
    conversionReward.conversionMilli = int64_t(currentGame->objectData.data[getItemID()][newOwner->getHouseID()].price)
        * ((getItemID() == Unit_Devastator || getItemID() == Unit_Ornithopter) ? 1000 : 100);
    newOwner->addCombatReward(Unit_Deviator, conversionReward);

    // Adding this in as a surrogate for damage inflicted upon deviation.. Still not sure what the best value
    // should be... going in with a 25% of the units value unless its a devastator which we can destruct or an ornithoper
    // which is likely to get killed
    if(getItemID() == Unit_Devastator || getItemID() == Unit_Ornithopter){
        newOwner->informHasDamaged(Unit_Deviator, currentGame->objectData.data[getItemID()][newOwner->getHouseID()].price);
    } else{
        newOwner->informHasDamaged(Unit_Deviator, currentGame->objectData.data[getItemID()][newOwner->getHouseID()].price / 10);
    }



}

void UnitBase::drawSelectionBox() {

    SDL_Texture* selectionBox = nullptr;

    switch(currentZoomlevel) {
        case 0:     selectionBox = pGFXManager->getUIGraphic(UI_SelectionBox_Zoomlevel0);   break;
        case 1:     selectionBox = pGFXManager->getUIGraphic(UI_SelectionBox_Zoomlevel1);   break;
        case 2:
        default:    selectionBox = pGFXManager->getUIGraphic(UI_SelectionBox_Zoomlevel2);   break;
    }

    SDL_Rect dest = calcDrawingRect(selectionBox, screenborder->world2screenX(realX), screenborder->world2screenY(realY), HAlign::Center, VAlign::Center);
    SDL_RenderCopy(renderer, selectionBox, nullptr, &dest);

    int x = screenborder->world2screenX(realX) - getWidth(selectionBox)/2;
    int y = screenborder->world2screenY(realY) - getHeight(selectionBox)/2;
    for(int i=1;i<=currentZoomlevel+1;i++) {
        renderDrawHLine(renderer, x+1, y-i, x+1 + (lround((getHealth()/getMaxHealth())*(getWidth(selectionBox)-3))), getHealthColor());
    }
}

void UnitBase::drawOtherPlayerSelectionBox() {
    SDL_Texture* selectionBox = nullptr;

    switch(currentZoomlevel) {
        case 0:     selectionBox = pGFXManager->getUIGraphic(UI_OtherPlayerSelectionBox_Zoomlevel0);   break;
        case 1:     selectionBox = pGFXManager->getUIGraphic(UI_OtherPlayerSelectionBox_Zoomlevel1);   break;
        case 2:
        default:    selectionBox = pGFXManager->getUIGraphic(UI_OtherPlayerSelectionBox_Zoomlevel2);   break;
    }

    SDL_Rect dest = calcDrawingRect(selectionBox, screenborder->world2screenX(realX), screenborder->world2screenY(realY), HAlign::Center, VAlign::Center);
    SDL_RenderCopy(renderer, selectionBox, nullptr, &dest);
}


void UnitBase::releaseTarget() {
    if(forced == true) {
        guardPoint = location;
    }
    setDestination(guardPoint);

    findTargetTimer = 0;
    setForced(false);
    setTarget(nullptr);
}

void UnitBase::engageTarget() {

    if(target && (target.getObjPointer() == nullptr)) {
        // the target does not exist anymore
        releaseTarget();
        return;
    }

    if(target && (target.getObjPointer()->isActive() == false)) {
        // the target changed its state to inactive
        releaseTarget();
        return;
    }

    if(target && !targetFriendly && !canAttack(target.getObjPointer())) {
        // the (non-friendly) target cannot be attacked anymore
        releaseTarget();
        return;
    }

    if (target && attackMode==HUNT && !forced && findTargetTimer==0
        && !currentGameMap->terrainAttackReachable(*this,*target.getObjPointer())) {
        // Drop an old hunt when its target moves behind an impassable ridge.
        // Explicit player orders and booked transport are not autonomous hunts.
        setGuardPoint(location);
        releaseTarget();
        clearPath();
        return;
    }

    if(target && !targetFriendly && !forced && !isInAttackRange(target.getObjPointer())) {
        // the (non-friendly) target left the attack mode range (and we were not forced to attack it)
        releaseTarget();
        return;
    }

    if(target) {
        // we have a target unit or structure

        Coord targetLocation = target.getObjPointer()->getClosestPoint(location);

        if(destination != targetLocation) {
            FixPoint movementDistance = blockDistance(destination, targetLocation);
            FixPoint distanceToTarget = blockDistance(location, targetLocation);
            
            if(movementDistance > 1) {
                // Target moved more than 1 tile
                if(distanceToTarget > 10) {
                    // Target is far away (>10 tiles) - don't repath yet
                    // Update destination AND cached path metadata to keep path valid
                    destination = targetLocation;
                    cachedPathDestination = targetLocation;
                } else {
                    // Target is close (<= 10 tiles) and moved significantly - repath now
                    clearPath();
                }
            } else {
                // Minor movement (<= 1 tile), update destination and cached metadata
                destination = targetLocation;
                cachedPathDestination = targetLocation;
            }
        }

        targetDistance = blockDistance(location, targetLocation);

        Sint8 newTargetAngle = destinationDrawnAngle(location, targetLocation);

        if(bFollow) {
            // we are following someone
            setDestination(targetLocation);
            return;
        }

        if(targetDistance > getWeaponRange()) {
            // we are not in attack range
            if(target.getObjPointer()->isAFlyingUnit()) {
                // we are not following this air unit
                releaseTarget();
                return;
            } else {
                // follow the target
                setDestination(targetLocation);
                return;
            }
        }

        // Dynasty launchers/deviators need no facing check for flying targets.
        // Finish an existing body turn, but do not chase the aircraft with the hull.
        const bool airMissile = (itemID == Unit_Launcher || itemID == Unit_Deviator)
            && target.getObjPointer()->isAFlyingUnit();
        const bool bodyTurnComplete = targetAngle == INVALID || angle == FixPoint(targetAngle);

        if(targetFriendly && !forced) {
            // the target is friendly and we only attack these if were forced to do so
            return;
        }

        if(goingToRepairYard) {
            // we are going to the repair yard
            // => we do not need to change the destination
            targetAngle = INVALID;
        } else if(attackMode == CAPTURE) {
            // we want to capture the target building
            setDestination(targetLocation);
            targetAngle = INVALID;
        } else if(isTracked() && target.getObjPointer()->isInfantry() && !targetFriendly && currentGameMap->tileExists(targetLocation) && !currentGameMap->getTile(targetLocation)->isMountain() && forced) {
            // we squash the infantry unit because we are forced to
            setDestination(targetLocation);
            targetAngle = INVALID;
        } else if(!isAFlyingUnit()) {
            // we decide to fire on the target thus we can stop moving
            setDestination(location);
            if(!airMissile) targetAngle = newTargetAngle;
            else if(bodyTurnComplete) targetAngle = INVALID;
        }

        if(airMissile ? !moving && bodyTurnComplete : getCurrentAttackAngle() == newTargetAngle) {
            attack();
        }

    } else if(attackPos) {
        // we attack a position

        targetDistance = blockDistance(location, attackPos);

        Sint8 newTargetAngle = destinationDrawnAngle(location, attackPos);

        if(targetDistance <= getWeaponRange()) {
            if(!isAFlyingUnit()) {
            // we are in weapon range thus we can stop moving
                setDestination(location);
                targetAngle = newTargetAngle;
            }

            if(getCurrentAttackAngle() == newTargetAngle) {
                attack();
            }
        } else {
            targetAngle = INVALID;
        }
    }
}

bool UnitBase::usesDynastyGroundTiming() const {
    return isAGroundUnit() && itemID != Unit_Sandworm && DynastyMovement::factor(itemID) != 0;
}

Coord UnitBase::movementEndpoint() const {
    return Coord(nextSpot.x*TILESIZE+TILESIZE/2, nextSpot.y*TILESIZE+TILESIZE/2);
}

bool UnitBase::beginDynastyStep(Uint64 routeTick) {
    const auto* tile=currentGameMap->getTile(nextSpot);
    const int cargo=itemID==Unit_Harvester
        ? std::clamp((static_cast<Harvester*>(this)->getAmountOfSpice()*100/HARVESTERMAXSPICE).floor(),0,100) : 0;
    const int step=DynastyMovement::step(itemID,
        DynastyMovement::throttle(itemID,static_cast<TERRAINTYPE>(tile->getType())),
        getHealth()<getMaxHealth()/2,cargo);
    const int ticks=DynastyMovement::movementTicks(step, location.x!=nextSpot.x && location.y!=nextSpot.y);
    if(ticks==0 || getMaxSpeed()<=0) return false;
    // One Dynasty coordinate unit per20Hz movement tick is0.08 world
    // units per16ms cycle (64/256 *20 *0.016). INI caps remain configurable.
    const FixPoint referenceCap=DynastyMovement::step(itemID,255,false,0)*0.08_fix;
    FixPoint scale=getMaxSpeed()/referenceCap;
    if(tile->isRoad()) scale*=ROADSPEEDMULTIPLIER;
    dynastyStepStart=routeTick*50;
    // Movement precedes scripting: the first movement tick is strictly after
    // the route call, then every3 ticks, independently of rendering cadence.
    const Uint64 duration=(routeTick/3*3+ticks*3-routeTick)*50;
    dynastyStepEnd=dynastyStepStart+std::max<Uint64>(1,lround(FixPoint(static_cast<int>(duration))/scale));
    dynastyStartX=realX-bumpyOffsetX; dynastyStartY=realY-bumpyOffsetY;
    return true;
}

bool UnitBase::moveDynastyStep() {
    if(!moving || !usesDynastyGroundTiming()) { dynastyStepEnd=0; return false; }
    if(dynastyStepEnd==0) return false; // Pre-9844 save: finish its existing step.
    const Uint64 now=static_cast<Uint64>(currentGame->getGameCycleCount())*(GAMESPEED_DEFAULT*3);
    const auto elapsed=now>dynastyStepStart ? now-dynastyStepStart : 0;
    const auto duration=dynastyStepEnd-dynastyStepStart;
    const FixPoint progress=FixPoint(static_cast<int>(std::min(elapsed,duration)))/static_cast<int>(duration);
    realX=dynastyStartX+(dynastyEndpoint.x-dynastyStartX)*progress+bumpyOffsetX;
    realY=dynastyStartY+(dynastyEndpoint.y-dynastyStartY)*progress+bumpyOffsetY;
    if(location!=nextSpot && progress>=0.5_fix) {
        unassignFromMap(location); oldLocation=location; location=nextSpot;
        if(auto* grid=currentGame->getSpatialGrid()) grid->move(*this,getGridHandle(),oldLocation,location);
        currentGameMap->viewMap(owner->getHouseID(),location,getViewRange());
    }
    if(now>=dynastyStepEnd) {
        realX=dynastyEndpoint.x; realY=dynastyEndpoint.y;
        bumpyOffsetX=0; bumpyOffsetY=0;
        moving=false; justStoppedMoving=true; dynastyStepEnd=0;
        oldLocation.invalidate();
        if(forced && location==destination && !target) {
            setForced(false);
            if(getAttackMode()==CARRYALLREQUESTED) doSetAttackMode(GUARD);
        }
    }
    if(moving) bumpyMovementOnRock(FixPoint::abs(realX-bumpyOffsetX-dynastyStartX),
        FixPoint::abs(realY-bumpyOffsetY-dynastyStartY),
        FixPoint::abs(dynastyEndpoint.x-realX+bumpyOffsetX),
        FixPoint::abs(dynastyEndpoint.y-realY+bumpyOffsetY));
    checkPos();
    return true;
}

void UnitBase::move() {
    if(moveDynastyStep()) return;

    if(moving && !justStoppedMoving) {
        const int legacyDamageDivisor = DynastyMovement::factor(itemID)==0 && !isAFlyingUnit() && isBadlyDamaged() ? 2 : 1;
        realX += xSpeed/legacyDamageDivisor;
        realY += ySpeed/legacyDamageDivisor;

        // check if vehicle is on the first half of the way
        FixPoint fromDistanceX;
        FixPoint fromDistanceY;
        FixPoint toDistanceX;
        FixPoint toDistanceY;
        if(location != nextSpot) {
            // check if vehicle is half way out of old tile

            fromDistanceX = FixPoint::abs(location.x*TILESIZE - (realX-bumpyOffsetX) + TILESIZE/2);
            fromDistanceY = FixPoint::abs(location.y*TILESIZE - (realY-bumpyOffsetY) + TILESIZE/2);
            toDistanceX = FixPoint::abs(nextSpot.x*TILESIZE - (realX-bumpyOffsetX) + TILESIZE/2);
            toDistanceY = FixPoint::abs(nextSpot.y*TILESIZE - (realY-bumpyOffsetY) + TILESIZE/2);

            if((fromDistanceX >= TILESIZE/2) || (fromDistanceY >= TILESIZE/2)) {
                // let something else go in
                unassignFromMap(location);
                oldLocation = location;
                location = nextSpot;

                if(auto* spatialGrid = currentGame->getSpatialGrid()) {
                    spatialGrid->move(*this, getGridHandle(), oldLocation, location);
                }

                if(isAFlyingUnit() == false && itemID != Unit_Sandworm) {
                    currentGameMap->viewMap(owner->getHouseID(), location, getViewRange());
                }
            }

        } else {
            // if vehicle is out of old tile

            fromDistanceX = FixPoint::abs(oldLocation.x*TILESIZE - (realX-bumpyOffsetX) + TILESIZE/2);
            fromDistanceY = FixPoint::abs(oldLocation.y*TILESIZE - (realY-bumpyOffsetY) + TILESIZE/2);
            toDistanceX = FixPoint::abs(location.x*TILESIZE - (realX-bumpyOffsetX) + TILESIZE/2);
            toDistanceY = FixPoint::abs(location.y*TILESIZE - (realY-bumpyOffsetY) + TILESIZE/2);

            if ((fromDistanceX >= TILESIZE) || (fromDistanceY >= TILESIZE)) {

                if(forced && (location == destination) && !target) {
                    setForced(false);
                    if(getAttackMode() == CARRYALLREQUESTED) {
                        doSetAttackMode(GUARD);
                    }
                }

                moving = false;
                justStoppedMoving = true;
                realX = location.x * TILESIZE + TILESIZE/2;
                realY = location.y * TILESIZE + TILESIZE/2;
                bumpyOffsetX = 0;
                bumpyOffsetY = 0;

                oldLocation.invalidate();
            }

        }

        bumpyMovementOnRock(fromDistanceX, fromDistanceY, toDistanceX, toDistanceY);

    } else {
        justStoppedMoving = false;
    }

    checkPos();
}

namespace {
constexpr int kPathValidationProbeCount = 6;
}

void UnitBase::bumpyMovementOnRock(FixPoint fromDistanceX, FixPoint fromDistanceY, FixPoint toDistanceX, FixPoint toDistanceY) {

    if(hasBumpyMovementOnRock() && ((currentGameMap->getTile(location)->getType() == Terrain_Rock)
                                    || (currentGameMap->getTile(location)->getType() == Terrain_Mountain)
                                    || (currentGameMap->getTile(location)->isThickSpice()))) {
        // bumping effect

        const FixPoint epsilon = 0.005_fix;
        const FixPoint bumpyOffset = 2.5_fix;
        const FixPoint absXSpeed = FixPoint::abs(xSpeed);
        const FixPoint absYSpeed = FixPoint::abs(ySpeed);


        if((FixPoint::abs(xSpeed) >= epsilon) && (FixPoint::abs(fromDistanceX - absXSpeed) < absXSpeed/2)) { realY -= bumpyOffset; bumpyOffsetY -= bumpyOffset; }
        if((FixPoint::abs(ySpeed) >= epsilon) && (FixPoint::abs(fromDistanceY - absYSpeed) < absYSpeed/2)) { realX += bumpyOffset; bumpyOffsetX += bumpyOffset; }

        if((FixPoint::abs(xSpeed) >= epsilon) && (FixPoint::abs(fromDistanceX - 4*absXSpeed) < absXSpeed/2)) { realY += bumpyOffset; bumpyOffsetY += bumpyOffset; }
        if((FixPoint::abs(ySpeed) >= epsilon) && (FixPoint::abs(fromDistanceY - 4*absYSpeed) < absYSpeed/2)) { realX -= bumpyOffset; bumpyOffsetX -= bumpyOffset; }


        if((FixPoint::abs(xSpeed) >= epsilon) && (FixPoint::abs(fromDistanceX - 10*absXSpeed) < absXSpeed/2)) { realY -= bumpyOffset; bumpyOffsetY -= bumpyOffset; }
        if((FixPoint::abs(ySpeed) >= epsilon) && (FixPoint::abs(fromDistanceY - 20*absYSpeed) < absYSpeed/2)) { realX += bumpyOffset; bumpyOffsetX += bumpyOffset; }

        if((FixPoint::abs(xSpeed) >= epsilon) && (FixPoint::abs(fromDistanceX - 14*absXSpeed) < absXSpeed/2)) { realY += bumpyOffset; bumpyOffsetY += bumpyOffset; }
        if((FixPoint::abs(ySpeed) >= epsilon) && (FixPoint::abs(fromDistanceY - 14*absYSpeed) < absYSpeed/2)) { realX -= bumpyOffset; bumpyOffsetX -= bumpyOffset; }


        if((FixPoint::abs(xSpeed) >= epsilon) && (FixPoint::abs(toDistanceX - absXSpeed) < absXSpeed/2)) { realY -= bumpyOffset; bumpyOffsetY -= bumpyOffset; }
        if((FixPoint::abs(ySpeed) >= epsilon) && (FixPoint::abs(toDistanceY - absYSpeed) < absYSpeed/2)) { realX += bumpyOffset; bumpyOffsetX += bumpyOffset; }

        if((FixPoint::abs(xSpeed) >= epsilon) && (FixPoint::abs(toDistanceX - 4*absXSpeed) < absXSpeed/2)) { realY += bumpyOffset; bumpyOffsetY += bumpyOffset; }
        if((FixPoint::abs(ySpeed) >= epsilon) && (FixPoint::abs(toDistanceY - 4*absYSpeed) < absYSpeed/2)) { realX -= bumpyOffset; bumpyOffsetX -= bumpyOffset; }

        if((FixPoint::abs(xSpeed) >= epsilon) && (FixPoint::abs(toDistanceX - 10*absXSpeed) < absXSpeed/2)) { realY -= bumpyOffset; bumpyOffsetY -= bumpyOffset; }
        if((FixPoint::abs(ySpeed) >= epsilon) && (FixPoint::abs(toDistanceY - 10*absYSpeed) < absYSpeed/2)) { realX += bumpyOffset; bumpyOffsetX += bumpyOffset; }

        if((FixPoint::abs(xSpeed) >= epsilon) && (FixPoint::abs(toDistanceX - 14*absXSpeed) < absXSpeed/2)) { realY += bumpyOffset; bumpyOffsetY += bumpyOffset; }
        if((FixPoint::abs(ySpeed) >= epsilon) && (FixPoint::abs(toDistanceY - 14*absYSpeed) < absYSpeed/2)) { realX -= bumpyOffset; bumpyOffsetX -= bumpyOffset; }

    }
}

void UnitBase::navigate() {

    const bool dynasty=usesDynastyGroundTiming();
    const Uint64 now=static_cast<Uint64>(currentGame->getGameCycleCount())*(GAMESPEED_DEFAULT*3);
    Uint64 routeTick=0;
    bool routeDue=false;
    if(dynasty) {
        if(!moving && location==destination) {
            dynastyRouteTick=0;
            routeDue=((currentGame->getGameCycleCount()+getObjectID()*1337)%5)==0;
        }
        else {
            if(dynastyRouteTick==0) {
                // UNIT.EMC enters the move loop on its first script update
                // for harvesters/MCVs and the following update for combat units.
                dynastyRouteTick=((now+249)/250)*5 + ((itemID==Unit_Harvester || itemID==Unit_MCV) ? 0 : 5);
                if(dynastyRouteTick==0) dynastyRouteTick=5;
            }
            routeTick=dynastyRouteTick;
            if(now>=routeTick*50) { routeDue=true; dynastyRouteTick+=15; }
            // Resolve the async path before the next script opportunity.
            if(!moving && !nextSpotFound && pathList.empty() && recalculatePathTimer==0 && !pathRequestQueued)
                enqueuePathRequest();
        }
    }
    if((dynasty && routeDue) || (!dynasty && (isAFlyingUnit() || (((currentGame->getGameCycleCount() + getObjectID()*1337) % 5) == 0)))) {
        // Standard ground routes use the15-tick script loop; extensions retain
        // the Legacy navigation cadence.

        if(!moving && (dynasty || !justStoppedMoving)) {
            if(!pathList.empty() && !isCachedPathStillValid()) {
                clearPath();
            }
            
            if(location != destination) {
                const bool hadNextSpot=nextSpotFound;
                if(nextSpotFound == false)  {

                    if(pathList.empty() && (recalculatePathTimer == 0) && !pathRequestQueued) {
                        enqueuePathRequest();
                    }

                    if(!pathList.empty()) {
                        nextSpot = pathList.front();
                        pathList.pop_front();
                        nextSpotFound = true;
                        recalculatePathTimer = 0;
                        noCloserPointCount = 0;
                    } else {
                        // Unit is paused - track why
                        if(pathRequestQueued) {
                            currentGame->frameTiming.pauseWaitingForPath++;
                        } else if(recalculatePathTimer > 0) {
                            currentGame->frameTiming.pauseRecalcCooldown++;
                        }
                    }
                }
                if(nextSpotFound && (dynasty || hadNextSpot)) {
                    int tempAngle = currentGameMap->getPosAngle(location, nextSpot);
                    if(tempAngle != INVALID) {
                        nextSpotAngle = tempAngle;
                    }

                    if(!canPass(nextSpot.x, nextSpot.y)) {
                        // Check if blockage is temporary (another moving unit)
                        // First verify nextSpot is on the map (canPass returns false for out-of-bounds)
                        if(currentGameMap->tileExists(nextSpot)) {
                            Tile* nextTile = currentGameMap->getTile(nextSpot);
                            if(nextTile && nextTile->hasAnObject()) {
                                ObjectBase* blocker = nextTile->getObject();
                                if(blocker && blocker->isAUnit()) {
                                    UnitBase* blockingUnit = static_cast<UnitBase*>(blocker);
                                    if(blockingUnit->isMoving()) {
                                        // Both units are moving - wait a cycle for blocker to move
                                        // Don't clear path, blocker will likely move soon
                                        currentGame->frameTiming.pauseWaitingForBlocker++;
                                        return;
                                    }
                                }
                            }
                        }
                        
                        // Static blocker, structure, or out-of-bounds - clear path and reroute
                        clearPath();
                    } else {
                        if (dynasty ? angle==FixPoint(nextSpotAngle) : drawnAngle==nextSpotAngle) {
                            if(dynasty && !beginDynastyStep(routeTick)) return;
                            moving = true;
                            nextSpotFound = false;

                            assignToMap(nextSpot);
                            angle = drawnAngle;
                            setSpeeds();
                            if(dynasty) dynastyEndpoint=movementEndpoint();
                        } else {
                            // Unit needs to turn to face next waypoint
                            currentGame->frameTiming.pauseTurningToFace++;
                        }
                    }
                }
            } else if(!target && attackPos.isInvalid()) {
                if(((currentGame->getGameCycleCount() + getObjectID()*1337) % MILLI2CYCLES(UNITIDLETIMER)) == 0) {
                    idleAction();
                }
            }
        }
    }
}

void UnitBase::idleAction() {
    //not moving and not wanting to go anywhere, do some random turning
    if(isAGroundUnit() && !isHarvesterLikeUnit(getItemID()) && (getAttackMode() == GUARD)) {
        // we might turn this cylce with 20% chance
        if(currentGame->randomGen.rand(0, 4) == 0) {
            // choose a random one of the eight possible angles
            nextSpotAngle = currentGame->randomGen.rand(0, 7);
        }
    }
}

ObjectBase* UnitBase::getActionClickTarget(int xPos, int yPos) const {
    const auto* tile=currentGameMap->tileExists(xPos,yPos) ? currentGameMap->getTile(xPos,yPos) : nullptr;
    if(!tile || !tile->isExploredByTeam(owner->getTeamID()) || tile->isFoggedByTeam(owner->getTeamID())) return nullptr;
    auto* object=tile->getObject();
    return object && object->isVisible(owner->getTeamID()) ? object : nullptr;
}

void UnitBase::handleActionClick(int xPos, int yPos) {
    if(respondable) {
        if(currentGameMap->tileExists(xPos, yPos)) {
            if(ObjectBase* tempTarget = getActionClickTarget(xPos,yPos)) {
                // Attack/follow only a visible target, as shown by the cursor.

                if(tempTarget->getOwner()->getTeamID() != getOwner()->getTeamID()) {
                    // attack
                    currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_UNIT_ATTACKOBJECT,objectID,tempTarget->getObjectID()));
                } else {
                    // move to object/structure
                    currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_UNIT_MOVE2OBJECT,objectID,tempTarget->getObjectID()));
                }
            } else {
                // move this unit
                currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_UNIT_MOVE2POS,objectID,(Uint32) xPos, (Uint32) yPos, (Uint32) true));
            }
        }
    }
}

void UnitBase::handleAttackClick(int xPos, int yPos) {
    if(respondable) {
        if(currentGameMap->tileExists(xPos, yPos)) {
            if(currentGameMap->getTile(xPos,yPos)->hasAnObject()) {
                // attack unit/structure or move to structure
                ObjectBase* tempTarget = currentGameMap->getTile(xPos,yPos)->getObject();

                currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_UNIT_ATTACKOBJECT,objectID,tempTarget->getObjectID()));
            } else {
                // attack pos
                currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_UNIT_ATTACKPOS,objectID,(Uint32) xPos, (Uint32) yPos, (Uint32) true));
            }
        }
    }

}

void UnitBase::handleHealClick(int xPos, int yPos) {
    if(respondable && canHeal() && currentGameMap->tileExists(xPos, yPos)
            && currentGameMap->getTile(xPos, yPos)->hasAnObject()) {
        ObjectBase* tempTarget = currentGameMap->getTile(xPos, yPos)->getObject();
        if(tempTarget->isAUnit()
                && tempTarget->getOwner()->getTeamID() == getOwner()->getTeamID()
                && tempTarget->getHealth() < tempTarget->getMaxHealth()) {
            currentGame->getCommandManager().addCommand(
                Command(pLocalPlayer->getPlayerID(), CMD_UNIT_HEAL, objectID, tempTarget->getObjectID()));
        }
    }
}
void UnitBase::handleMoveClick(int xPos, int yPos) {
    if(respondable) {
        if(currentGameMap->tileExists(xPos, yPos)) {
            // move to pos
            currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_UNIT_MOVE2POS,objectID,(Uint32) xPos, (Uint32) yPos, (Uint32) true));
        }
    }
}

void UnitBase::handleSetAttackModeClick(ATTACKMODE newAttackMode) {
    currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_UNIT_SETMODE,objectID,(Uint32) newAttackMode));
}

/**
    User action
    Request a Carryall to drop at target location
**/
void UnitBase::handleRequestCarryallDropClick(int xPos, int yPos) {
    if(respondable) {
        if(currentGameMap->tileExists(xPos, yPos)) {
            currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_UNIT_REQUESTCARRYALLDROP, objectID, (Uint32) xPos, (Uint32) yPos));
        }
    }
}



void UnitBase::doMove2Pos(int xPos, int yPos, bool bForced) {
    if(attackMode == CAPTURE || attackMode == HUNT) {
        doSetAttackMode(GUARD);
    }

    if(currentGameMap->tileExists(xPos, yPos)) {
        if((xPos != destination.x) || (yPos != destination.y)) {
            clearPath();
            findTargetTimer = 0;
        }

        setTarget(nullptr);
        setDestination(xPos,yPos);
        setForced(bForced);
        setGuardPoint(xPos,yPos);
    } else {
        setTarget(nullptr);
        setDestination(location);
        setForced(bForced);
        setGuardPoint(location);
    }
}

void UnitBase::doMove2Pos(const Coord& coord, bool bForced) {
    doMove2Pos(coord.x, coord.y, bForced);
}

void UnitBase::doMove2Object(const ObjectBase* pTargetObject) {
    if(pTargetObject->getObjectID() == getObjectID()) {
        return;
    }

    if(attackMode == CAPTURE || attackMode == HUNT) {
        doSetAttackMode(GUARD);
    }

    setDestination(INVALID_POS,INVALID_POS);
    setTarget(pTargetObject);
    setForced(true);

    bFollow = true;

    clearPath();
    findTargetTimer = 0;
}

void UnitBase::doMove2Object(Uint32 targetObjectID) {
    ObjectBase* pObject = currentGame->getObjectManager().getObject(targetObjectID);

    if(pObject == nullptr) {
        return;
    }

    doMove2Object(pObject);
}

void UnitBase::doAttackPos(int xPos, int yPos, bool bForced) {
    if(!currentGameMap->tileExists(xPos, yPos)) {
        return;
    }

    if(attackMode == CAPTURE) {
        doSetAttackMode(GUARD);
    }

    setDestination(xPos,yPos);
    setTarget(nullptr);
    setForced(bForced);
    attackPos.x = xPos;
    attackPos.y = yPos;

    clearPath();
    findTargetTimer = 0;
}

void UnitBase::doAttackObject(const ObjectBase* pTargetObject, bool bForced) {
    if(pTargetObject->getObjectID() == getObjectID() || (!canAttack() && !isHarvesterLikeUnit(getItemID()))) {
        return;
    }

    if(attackMode == CAPTURE) {
        doSetAttackMode(GUARD);
    }

    setDestination(INVALID_POS,INVALID_POS);

    setTarget(pTargetObject);
    // hack to make it possible to attack own repair yard
    if(goingToRepairYard && target && (target.getObjPointer()->getItemID() == Structure_RepairYard)) {
        static_cast<RepairYard*>(target.getObjPointer())->unBook();
        goingToRepairYard = false;
    }


    setForced(bForced);

    clearPath();
    findTargetTimer = 0;
}

void UnitBase::doAttackObject(Uint32 TargetObjectID, bool bForced) {
    ObjectBase* pObject = currentGame->getObjectManager().getObject(TargetObjectID);

    if(pObject == nullptr) {
        return;
    }

    doAttackObject(pObject, bForced);
}

void UnitBase::doSetAttackMode(ATTACKMODE newAttackMode) {
    if((newAttackMode >= 0) && (newAttackMode < ATTACKMODE_MAX)) {
        attackMode = newAttackMode;
    }

    if(attackMode == GUARD || attackMode == STOP) {
        if(moving && !justStoppedMoving) {
            doMove2Pos(nextSpot, false);
        } else {
            doMove2Pos(location, false);
        }
    }
    
    // When setting HUNT mode, immediately trigger target search
    if(attackMode == HUNT && !target && pendingTargetRequest == TargetRequestKind::None) {
        enqueueTargetRequest(TargetRequestKind::Acquire);
    }
}

void UnitBase::handleDamage(int damage, Uint32 damagerID, House* damagerOwner) {
    // shorten deviation time
    if(deviationTimer > 0) {
        deviationTimer = std::max(0,deviationTimer - MILLI2CYCLES(damage*20*1000));
    }

    ObjectBase::handleDamage(damage, damagerID, damagerOwner);

    ObjectBase* pDamager = currentGame->getObjectManager().getObject(damagerID);

    if(pDamager != nullptr){

        if(attackMode == HUNT && !forced) {
            ObjectBase* pDamager = currentGame->getObjectManager().getObject(damagerID);
            if(canAttack(pDamager)) {
                if(!target || target.getObjPointer() == nullptr || !isInWeaponRange(target.getObjPointer())) {
                    // no target or target not on weapon range => switch target
                    doAttackObject(pDamager, false);
                }

            }
        }
        
        // CRITICAL FIX: AMBUSH units switch to HUNT when damaged (Dynasty behavior, line 1930-1933)
        // This allows them to pursue and counter-attack instead of sitting in their 1-tile view range
        if(damage > 0 && attackMode == AMBUSH && !isHarvesterLikeUnit(getItemID())) {
            doSetAttackMode(HUNT);
            findTargetTimer = 0;  // Allow immediate target search
        }
        
        // Reset target timer when damaged so units can immediately look for their attacker
        // Without this, GUARD units wait up to 2 seconds before noticing they're being shot
        if(damage > 0 && canAttack(pDamager) && (attackMode == GUARD || attackMode == AREAGUARD || attackMode == HUNT)) {
            findTargetTimer = 0;  // Allow immediate target search on next update
        }
    }
}

bool UnitBase::isInGuardRange(const ObjectBase* pObject) const  {
    int checkRange;
    Coord checkFrom;
    
    switch(attackMode) {
        case GUARD: {
            checkRange = getWeaponRange();
            checkFrom = guardPoint*TILESIZE + Coord(TILESIZE/2, TILESIZE/2);
        } break;

        case AREAGUARD: {
            // Launchers get extended area guard range due to long weapon range
            checkRange = (getItemID() == Unit_Launcher) ? 12 : 10;
            checkFrom = getCenterPoint();  // Check from current location
        } break;

        case AMBUSH: {
            checkRange = getViewRange();
            checkFrom = guardPoint*TILESIZE + Coord(TILESIZE/2, TILESIZE/2);
        } break;

        case HUNT: {
            return true;
        } break;

        case CARRYALLREQUESTED: {
            return false;
        } break;

        case RETREAT: {
            return false;
        } break;

        case STOP:
        default: {
            return false;
        } break;
    }

    if(getItemID() == Unit_Sandworm) {
        checkRange = getViewRange();
    }

    return (blockDistance(checkFrom, pObject->getCenterPoint()) <= checkRange*TILESIZE);
}

bool UnitBase::isInAttackRange(const ObjectBase* pObject) const {
    int checkRange;
    Coord checkFrom;
    
    switch(attackMode) {
        case GUARD: {
            checkRange = getWeaponRange();
            checkFrom = guardPoint*TILESIZE + Coord(TILESIZE/2, TILESIZE/2);
        } break;

        case AREAGUARD: {
            // Launchers get extended area guard range due to long weapon range
            checkRange = (getItemID() == Unit_Launcher) ? 12 : 10;
            checkFrom = getCenterPoint();  // Check from current location
        } break;

        case AMBUSH: {
            checkRange = getViewRange() + 1;
            checkFrom = guardPoint*TILESIZE + Coord(TILESIZE/2, TILESIZE/2);
        } break;

        case HUNT: {
            return true;
        } break;

        case CARRYALLREQUESTED: {
            return false;
        } break;

        case RETREAT: {
            return false;
        } break;

        case STOP:
        default: {
            return false;
        } break;
    }

    if(getItemID() == Unit_Sandworm) {
        checkRange = getViewRange() + 1;
    }

    return (blockDistance(checkFrom, pObject->getCenterPoint()) <= checkRange*TILESIZE);
}

bool UnitBase::isInWeaponRange(const ObjectBase* object) const {
    if(object == nullptr) {
        return false;
    }

    Coord targetLocation = object->getClosestPoint(location);

    return (blockDistance(location, targetLocation) <= getWeaponRange());
}


void UnitBase::setAngle(int newAngle) {
    if(!moving && !justStoppedMoving) {
        newAngle = newAngle % NUM_ANGLES;
        angle = drawnAngle = newAngle;
        clearPath();
    }
}

void UnitBase::setGettingRepaired() {
    if(!isEligibleForRepair()) {
        // Last line of defence for a deviated unit. Callers keep it on the map: the two that
        // exist check the same condition first, so nothing is left removed from the map here.
        return;
    }

    if(target.getObjPointer() != nullptr && (target.getObjPointer()->getItemID() == Structure_RepairYard)) {
        if(selected) {
            removeFromSelectionLists();
        }

        currentGameMap->removeObjectFromMap(getObjectID());

        if(auto* spatialGrid = currentGame->getSpatialGrid()) {
            spatialGrid->unregister(getGridHandle());
        }

        static_cast<RepairYard*>(target.getObjPointer())->assignUnit(this);

        respondable = false;
        setActive(false);
        setVisible(VIS_ALL, false);
        goingToRepairYard = false;
        badlyDamaged = false;

        setTarget(nullptr);
        //setLocation(INVALID_POS, INVALID_POS);
        setDestination(location);
        nextSpotAngle = DOWN;
    }
}

void UnitBase::setGuardPoint(int newX, int newY) {
    if(currentGameMap->tileExists(newX, newY) || ((newX == INVALID_POS) && (newY == INVALID_POS))) {
        guardPoint.x = newX;
        guardPoint.y = newY;

        if(isHarvesterLikeUnit(getItemID()) && guardPoint.isValid()) {
            if(currentGameMap->getTile(newX, newY)->hasSpice()) {
                if(attackMode == STOP) {
                    attackMode = GUARD;
                }
            } else {
                if(attackMode != STOP) {
                    attackMode = STOP;
                }
            }
        }
    }
}

void UnitBase::setLocation(int xPos, int yPos) {

    if((xPos == INVALID_POS) && (yPos == INVALID_POS)) {
        ObjectBase::setLocation(xPos, yPos);
    } else if (currentGameMap->tileExists(xPos, yPos)) {
        ObjectBase::setLocation(xPos, yPos);
        realX += TILESIZE/2;
        realY += TILESIZE/2;
        bumpyOffsetX = 0;
        bumpyOffsetY = 0;
    }

    moving = false;
    dynastyRouteTick=0; dynastyStepEnd=0;
    pickedUp = false;
    setTarget(nullptr);

    clearPath();
}

void UnitBase::setPickedUp(UnitBase* newCarrier) {
    if(selected) {
        removeFromSelectionLists();
    }

    currentGameMap->removeObjectFromMap(getObjectID());

    if(goingToRepairYard && target.getObjPointer() != nullptr) {
        static_cast<RepairYard*>(target.getObjPointer())->unBook();
    }

    if(auto* spatialGrid = currentGame->getSpatialGrid()) {
        spatialGrid->unregister(getGridHandle());
    }

    if(isHarvesterLikeUnit(getItemID())) {
        if(harvesterIsReturning(this) && target) {
            if(StructureBase* dropoff = getHarvesterDropoff(target.getObjPointer())) {
                dropoff->unbookHarvesterDropoff();
            }
        }
    }

    target.pointTo(newCarrier);

    goingToRepairYard = false;
    forced = false;
    moving = false;
    dynastyRouteTick=0; dynastyStepEnd=0;
    pickedUp = true;
    respondable = false;
    setActive(false);
    setVisible(VIS_ALL, false);

    clearPath();
}

FixPoint UnitBase::getMaxSpeed() const {
    return currentGame->objectData.data[itemID][originalHouseID].maxspeed;
}

FixPoint UnitBase::getTerrainAdjustedSpeed(int cargoPercent) const {
    FixPoint speed = getMaxSpeed();
    if(isAFlyingUnit()) return speed;
    // Dynasty selects the terrain of the tile being entered.
    const bool dynastyUnit = DynastyMovement::factor(itemID) != 0;
    const Coord speedTile = dynastyUnit && nextSpot.isValid() && currentGameMap->tileExists(nextSpot) ? nextSpot : location;
    const Tile* tile = currentGameMap->getTile(speedTile);
    if(dynastyUnit) {
        const int base = DynastyMovement::step(itemID,255,false,0);
        const int step = DynastyMovement::step(itemID,
            DynastyMovement::throttle(itemID,static_cast<TERRAINTYPE>(tile->getType())),
            getHealth() < getMaxHealth()/2,cargoPercent);
        speed = speed * step / base;
    } else {
        speed *= 2-getTerrainDifficulty(static_cast<TERRAINTYPE>(currentGameMap->getTile(location)->getType()));
        if(isBadlyDamaged()) speed *= HEAVILYDAMAGEDSPEEDMULTIPLIER;
    }
    // City roads are an intentional extension; Dynasty has no equivalent.
    if(tile->isRoad()) speed *= ROADSPEEDMULTIPLIER;
    return speed;
}

void UnitBase::setSpeeds() {
    const FixPoint speed = getTerrainAdjustedSpeed();

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

void UnitBase::setTarget(const ObjectBase* newTarget) {
    attackPos.invalidate();
    bFollow = false;
    targetAngle = INVALID;

    if(goingToRepairYard) {
        ObjectBase* pOldTarget = target.getObjPointer();
        if(pOldTarget != nullptr && pOldTarget->getItemID() == Structure_RepairYard) {
            static_cast<RepairYard*>(pOldTarget)->unBook();
        }
        // Repair yard may have been destroyed while we were en route;
        // clear the flag either way — we are no longer booked/headed there.
        goingToRepairYard = false;
    }

    ObjectBase::setTarget(newTarget);

    ObjectBase* pNewTarget = target.getObjPointer();
    if(pNewTarget != nullptr
        && (pNewTarget->getOwner() == getOwner())
        && (pNewTarget->getItemID() == Structure_RepairYard)
        // An explicit move onto the captor's repair yard must not turn into a repair trip
        // for a deviated unit: without the booking it never reaches setGettingRepaired().
        && isEligibleForRepair()) {
        static_cast<RepairYard*>(pNewTarget)->book();
        goingToRepairYard = true;
    }
}

void UnitBase::enqueueTargetRequest(TargetRequestKind kind) {
    if(currentGame == nullptr || location.isInvalid()) {
        return;
    }

    if(pendingTargetRequest == TargetRequestKind::None) {
        pendingTargetRequest = kind;
    } else {
        if(pendingTargetRequest == TargetRequestKind::Refresh && kind == TargetRequestKind::Acquire) {
            pendingTargetRequest = kind;
        } else {
            return;
        }
    }

    findTargetTimer = -1;
    currentGame->queueTargetRequest(getObjectID());
}

void UnitBase::enqueuePathRequest() {
    if(currentGame == nullptr || pathRequestQueued || location.isInvalid() || destination.isInvalid()) {
        return;
    }

    pathRequestQueued = true;
    recalculatePathTimer = -1;
    currentGame->queuePathRequest(getObjectID());
}

void UnitBase::targeting() {
    if(findTargetTimer == 0) {
        if(attackMode != STOP && attackMode != CARRYALLREQUESTED) {
            
            // Refresh target if current one is out of weapon range
            if(target && !attackPos && !forced && 
               (attackMode == GUARD || attackMode == AREAGUARD || attackMode == AMBUSH || attackMode == HUNT)) {
                if(!isInWeaponRange(target.getObjPointer())) {
                    const ObjectBase* pNewTarget = findTarget();
                    
                    if(pNewTarget != nullptr) {
                        // Saboteurs need forced=true to pathfind onto occupied tiles
                        bool forceAttack = (getItemID() == Unit_Saboteur);
                        doAttackObject(pNewTarget, forceAttack);
                        findTargetTimer = 500;
                    }
                }
            }
            
            // Acquire new target if we don't have one
            if(!target && !attackPos && !moving && !justStoppedMoving && !forced) {
                const ObjectBase* pNewTarget = findTarget();
                
                if(pNewTarget != nullptr) {
                    bool inRange = isInGuardRange(pNewTarget);
                    
                    // Dynasty behavior: AMBUSH units switch to HUNT when they SEE an enemy
                    // Check if enemy is within view range (not just guard/weapon range)
                    if(attackMode == AMBUSH) {
                        FixPoint distanceToTarget = blockDistance(location*TILESIZE + Coord(TILESIZE/2, TILESIZE/2), 
                                                                   pNewTarget->getCenterPoint());
                        if(distanceToTarget <= getViewRange()*TILESIZE) {
                            doSetAttackMode(HUNT);
                            inRange = true;  // Force acquisition after switching to HUNT
                        }
                    }
                    
                    if(inRange) {
                        // Saboteurs need forced=true to pathfind onto occupied tiles
                        bool forceAttack = (getItemID() == Unit_Saboteur);
                        doAttackObject(pNewTarget, forceAttack);
                        
                        // Sandworms switch to HUNT after acquiring target
                        if(getItemID() == Unit_Sandworm) {
                            doSetAttackMode(HUNT);
                        }
                    }
                } else if(attackMode == HUNT) {
                    // HUNT units with no targets switch back to GUARD
                    setGuardPoint(location);
                    doSetAttackMode(GUARD);
                }
                
                // 1 second for normal targeting (balance of performance and responsiveness)
                // Units reset to 0 when damaged for instant response
                findTargetTimer = MILLI2CYCLES(1*1000);
            }
        }
    }

    engageTarget();
}

void UnitBase::resolvePendingTargetRequest() {
    TargetRequestKind request = pendingTargetRequest;
    pendingTargetRequest = TargetRequestKind::None;

    if(findTargetTimer < 0) {
        findTargetTimer = 0;
    }

    if(request == TargetRequestKind::None || currentGame == nullptr) {
        return;
    }

    if(location.isInvalid()) {
        findTargetTimer = MILLI2CYCLES(2*1000);
        return;
    }

    if(attackMode == STOP || attackMode == CARRYALLREQUESTED) {
        findTargetTimer = MILLI2CYCLES(2*1000);
        return;
    }

    if(request == TargetRequestKind::Refresh) {
        if(target && !attackPos && !forced &&
           (attackMode == GUARD || attackMode == AREAGUARD || attackMode == HUNT) &&
           !isInWeaponRange(target.getObjPointer())) {
            const ObjectBase* pNewTarget = findTarget();

            if(pNewTarget != nullptr) {
                // Saboteurs need forced=true to pathfind onto occupied tiles (structures/vehicles)
                bool forceAttack = (getItemID() == Unit_Saboteur);
                doAttackObject(pNewTarget, forceAttack);
                findTargetTimer = 500;
                return;
            }
        }

        request = TargetRequestKind::Acquire;
    }

    if(request == TargetRequestKind::Acquire) {
        // By the time we're resolving, targeting() already decided this request was appropriate
        // Just execute it without re-checking queue stress or movement state
        if(!target && !attackPos && !forced) {
            const ObjectBase* pNewTarget = findTarget();

            if(pNewTarget != nullptr) {
                // MULTIPLAYER-SAFE: Track launcher units acquiring ornithopter targets
                if((getItemID() == Unit_Launcher || getItemID() == Unit_Deviator) && 
                   pNewTarget->getItemID() == Unit_Ornithopter) {
                    currentGame->combatStats.launcherTargetsOrni++;
                }
                
                // In HUNT mode, attack targets regardless of guard range
                // Exception: Sandworms only hunt within view range
                // For other modes, only attack if in guard range
                bool shouldAttack = false;
                
                if(getItemID() == Unit_Sandworm) {
                    // Sandworms: Only hunt within view range, don't chase across map
                    shouldAttack = isInGuardRange(pNewTarget);
                    
                    // If sandworm is in HUNT mode but target is out of range, switch to AMBUSH
                    // This prevents sandworms from being too aggressive when targets are far away
                    if(!shouldAttack && attackMode == HUNT) {
                        doSetAttackMode(AMBUSH);
                    }
                } else {
                    // Regular units: HUNT attacks anything, other modes need target in range
                    shouldAttack = (attackMode == HUNT || isInGuardRange(pNewTarget));
                }
                
                if(shouldAttack) {
                    // Switch from AMBUSH to HUNT when attacking (except sandworms - keep them less aggressive)
                    if(attackMode == AMBUSH && getItemID() != Unit_Sandworm) {
                        doSetAttackMode(HUNT);
                    }
                    // Saboteurs need forced=true to pathfind onto occupied tiles (structures/vehicles)
                    bool forceAttack = (getItemID() == Unit_Saboteur);
                    doAttackObject(pNewTarget, forceAttack);
                }
            }

            if(pNewTarget==nullptr && attackMode==HUNT) {
                // Match the synchronous targeting path. An empty reachable
                // target set is not a mission to retry across the whole map.
                setGuardPoint(location);
                doSetAttackMode(GUARD);
            }
            findTargetTimer = MILLI2CYCLES(1*1000);
        } else {
            findTargetTimer = MILLI2CYCLES(1*1000);
        }
    }
}

UnitBase::PathRequestStats UnitBase::resolvePendingPathRequest() {
    PathRequestStats stats;
    pathRequestQueued = false;

    if(currentGame == nullptr) {
        recalculatePathTimer = 0;
        stats.invalidDestination = true;
        return stats;
    }

    if(location.isInvalid() || destination.isInvalid()) {
        recalculatePathTimer = 0;
        stats.invalidDestination = true;
        return stats;
    }

    if (target && attackMode==HUNT && !forced) {
        const ObjectBase* huntTarget=target.getObjPointer();
        if (!huntTarget || !currentGameMap->terrainAttackReachable(*this,*huntTarget)) {
            setGuardPoint(location);
            releaseTarget();
            clearPath();
            return stats; // No A* budget spent on a missing or disconnected target.
        }
    }

    // Jitter the cooldown by objectID to stagger re-requests across units,
    // preventing multiple units from hitting the pathfinder on the same cycle.
    recalculatePathTimer = 500 + static_cast<int>(getObjectID() % 100);

    std::size_t nodesExpanded = 0;
    bool invalidDestination = false;
    const bool pathFound = SearchPathWithAStar(nodesExpanded, invalidDestination);

    stats.nodesExpanded = nodesExpanded;
    stats.pathFound = pathFound;
    stats.invalidDestination = invalidDestination;

    // Don't count invalid destinations as pathfinding failures
    if(invalidDestination) {
        return stats;
    }
    
    // Simple stuck detection: track if we're making progress toward destination
    const FixPoint currentDistance = blockDistance(location, destination);
    
    // Check on EVERY path attempt (success or failure): did we get closer?
    if(lastDistanceToDestination >= 0) {
        if(currentDistance < lastDistanceToDestination - 0.5_fix) {
            // Made progress, reset counter
            noProgressCount = 0;
        } else {
            // No progress, increment counter
            noProgressCount++;
            
            // After 3 attempts without progress, request carryall (if cooldown expired)
            // Removed (location != oldLocation) requirement - freshly deployed units should also get help
            if(noProgressCount >= 3 && carryallRequestCooldown <= 0) {
                if(getOwner()->hasCarryalls()
                   && this->isAGroundUnit()
                   && !static_cast<GroundUnit*>(this)->hasBookedCarrier()
                   && (currentGame->getGameInitSettings().getGameOptions().manualCarryallDrops || getOwner()->isAI())
                   && currentDistance >= MIN_CARRYALL_LIFT_DISTANCE) {
                    
                    static_cast<GroundUnit*>(this)->requestCarryall();
                    noProgressCount = 0;
                    carryallRequestCooldown = MILLI2CYCLES(2000); // 2 second cooldown (reduced from 5s)
                }
            }
        }
    }
    
    lastDistanceToDestination = currentDistance;

    if(!pathFound) {
        // Removed (location != oldLocation) requirement - freshly deployed units should also get help
        if(++noCloserPointCount >= 3) {
            if(target.getObjPointer() != nullptr && targetFriendly
               && (target.getObjPointer()->getItemID() != Structure_RepairYard)
               && ((getHarvesterDropoff(target.getObjPointer()) == nullptr)
                   || !isHarvesterLikeUnit(getItemID()))) {
                setTarget(nullptr);
            }

            if(getOwner()->hasCarryalls()
               && this->isAGroundUnit()
               && !static_cast<GroundUnit*>(this)->hasBookedCarrier()
               && (currentGame->getGameInitSettings().getGameOptions().manualCarryallDrops || getOwner()->isAI())
               && blockDistance(location, destination) >= MIN_CARRYALL_LIFT_DISTANCE) {
                static_cast<GroundUnit*>(this)->requestCarryall();
            } else if(getOwner()->isAI()
                      && isHarvesterLikeUnit(getItemID())
                      && !harvesterIsReturning(this)
                      && blockDistance(location, destination) >= 2) {
                harvesterDoReturn(this);
            } else if(isHarvesterLikeUnit(getItemID())
                      && harvesterIsReturning(this)) {
                // Returning harvester - don't give up, blocker will move eventually
                noCloserPointCount = 0;
            } else {
                setDestination(location);
                forced = false;
            }
        }
    }

    return stats;
}

bool UnitBase::turnDynastyBody(int wantedAngle) {
    if(!usesDynastyGroundTiming() || dynastyRouteTick==0) return false;
    const Uint64 now=static_cast<Uint64>(currentGame->getGameCycleCount())*(GAMESPEED_DEFAULT*3);
    if(now>0 && now/200==(now-GAMESPEED_DEFAULT*3)/200) return true;
    FixPoint difference=FixPoint(wantedAngle)-angle;
    while(difference>4) difference-=8;
    while(difference< -4) difference+=8;
    const FixPoint step=currentGame->objectData.data[itemID][originalHouseID].turnspeed*25/6;
    if(FixPoint::abs(difference)<=step+0.00001_fix) angle=wantedAngle;
    else angle += difference>0 ? step : -step;
    while(angle<0) angle+=8;
    while(angle>=8) angle-=8;
    drawnAngle=lround(angle)%8;
    return true;
}

void UnitBase::turn() {
    if(!moving && (usesDynastyGroundTiming() || !justStoppedMoving)) {
        int wantedAngle = INVALID;

        // if we have to decide between moving and shooting we opt for moving
        if(nextSpotAngle != INVALID) {
            wantedAngle = nextSpotAngle;
        } else if(targetAngle != INVALID) {
            wantedAngle = targetAngle;
        }

        if(wantedAngle != INVALID) {
            if(turnDynastyBody(wantedAngle)) return;
            FixPoint angleLeft = 0;
            FixPoint angleRight = 0;

            if(angle > wantedAngle) {
                angleRight = angle - wantedAngle;
                angleLeft = FixPoint::abs(8-angle)+wantedAngle;
            } else if (angle < wantedAngle) {
                angleRight = FixPoint::abs(8-wantedAngle) + angle;
                angleLeft = wantedAngle - angle;
            }

            const FixPoint speed=currentGame->objectData.data[itemID][originalHouseID].turnspeed;
            if(usesDynastyGroundTiming() && std::min(angleLeft,angleRight)<=speed) {
                angle=wantedAngle; drawnAngle=wantedAngle;
            } else if(angleLeft <= angleRight) {
                turnLeft();
            } else {
                turnRight();
            }
        }
    }
}

void UnitBase::turnLeft() {
    angle += currentGame->objectData.data[itemID][originalHouseID].turnspeed;
    if(angle >= 7.5_fix) {
        drawnAngle = lround(angle) - NUM_ANGLES;
        angle -= NUM_ANGLES;
    } else {
        drawnAngle = lround(angle);
    }
}

void UnitBase::turnRight() {
    angle -= currentGame->objectData.data[itemID][originalHouseID].turnspeed;
    if(angle <= -0.5_fix) {
        drawnAngle = lround(angle) + NUM_ANGLES;
        angle += NUM_ANGLES;
    } else {
        drawnAngle = lround(angle);
    }
}

void UnitBase::quitDeviation() {
    if(wasDeviated()) {
        // revert back to real owner
        removeFromSelectionLists();
        setTarget(nullptr);
        setGuardPoint(location);
        setDestination(location);
        owner = currentGame->getHouse(originalHouseID);
        graphic = pGFXManager->getObjPic(graphicID,getOwner()->getHouseID());
        deviationTimer = INVALID;
    }
}

bool UnitBase::update() {
    if(active) {
        // Time targeting
        Uint64 targetStart = SDL_GetPerformanceCounter();
        targeting();
        Uint64 targetEnd = SDL_GetPerformanceCounter();
        double targetMs = currentGame->getElapsedMs(targetStart, targetEnd);
        currentGame->frameTiming.unitTargetingMs += targetMs;
        currentGame->frameTiming.unitTargetingMsThisFrame += targetMs;
        
        // Dynasty updates movement and body rotation before its route script.
        // In particular, a tile arrival can start the next step on this tick.
        const bool dynastyTiming=usesDynastyGroundTiming();
        if(dynastyTiming) {
            const Uint64 earlyMoveStart=SDL_GetPerformanceCounter();
            move();
            const Uint64 earlyTurnStart=SDL_GetPerformanceCounter();
            const double earlyMoveMs=currentGame->getElapsedMs(earlyMoveStart,earlyTurnStart);
            currentGame->frameTiming.unitMoveMs+=earlyMoveMs;
            currentGame->frameTiming.unitMoveMsThisFrame+=earlyMoveMs;
            if(active) turn();
            const double earlyTurnMs=currentGame->getElapsedMs(earlyTurnStart,SDL_GetPerformanceCounter());
            currentGame->frameTiming.unitTurnMs+=earlyTurnMs;
            currentGame->frameTiming.unitTurnMsThisFrame+=earlyTurnMs;
        }
        // Time navigate
        Uint64 navStart = SDL_GetPerformanceCounter();
        if(active) navigate();
        Uint64 navEnd = SDL_GetPerformanceCounter();
        double navMs = currentGame->getElapsedMs(navStart, navEnd);
        currentGame->frameTiming.unitNavigateMs += navMs;
        currentGame->frameTiming.unitNavigateMsThisFrame += navMs;
        
        // Time move
        Uint64 moveStart = SDL_GetPerformanceCounter();
        if(!dynastyTiming) move();
        Uint64 moveEnd = SDL_GetPerformanceCounter();
        double moveMs = currentGame->getElapsedMs(moveStart, moveEnd);
        currentGame->frameTiming.unitMoveMs += moveMs;
        currentGame->frameTiming.unitMoveMsThisFrame += moveMs;
        
        if(active) {
            // Time turn
            Uint64 turnStart = SDL_GetPerformanceCounter();
            if(!dynastyTiming) turn();
            Uint64 turnEnd = SDL_GetPerformanceCounter();
            double turnMs = currentGame->getElapsedMs(turnStart, turnEnd);
            currentGame->frameTiming.unitTurnMs += turnMs;
            currentGame->frameTiming.unitTurnMsThisFrame += turnMs;
            
            // Time updateVisibleUnits
            Uint64 visStart = SDL_GetPerformanceCounter();
            updateVisibleUnits();
            Uint64 visEnd = SDL_GetPerformanceCounter();
            double visMs = currentGame->getElapsedMs(visStart, visEnd);
            currentGame->frameTiming.unitVisibilityMs += visMs;
            currentGame->frameTiming.unitVisibilityMsThisFrame += visMs;
        }
    }

    if(getHealth() <= 0) {
        destroy();
        return false;
    }

    if(recalculatePathTimer > 0) recalculatePathTimer--;
    if(findTargetTimer > 0) findTargetTimer--;
    if(primaryWeaponTimer > 0) primaryWeaponTimer--;
    if(carryallRequestCooldown > 0) carryallRequestCooldown--;
    if(secondaryWeaponTimer > 0) {
        secondaryWeaponTimer--;
    } else if(secondaryWeaponTimer == 0 && usesLauncherRocketPair()) {
        // Targeting had this tick to fire the due second rocket. If it could not
        // (health, lost target, range or a new order), discard the interrupted pair
        // instead of retaining a late follow-up until a future attack.
        secondaryWeaponTimer = INVALID;
    }
    if(deviationTimer != INVALID) {
        if(--deviationTimer <= 0) {
            quitDeviation();
        }
    }

    return true;
}

void UnitBase::updateVisibleUnits() {
    if(isAFlyingUnit()) {
        return;
    }

    if(!currentGameMap->tileExists(location)) {
        return;
    }

    auto* pTile = currentGameMap->getTile(location);
    for (auto h = 0; h < NUM_HOUSES; h++) {
        auto* pHouse = currentGame->getHouse(h);
        if(!pHouse) {
            continue;
        }

        // Check if this unit is currently VISIBLE to this house (fog of war, not just explored)
        if(isVisible(pHouse->getTeamID()) && (pHouse->getTeamID() != getOwner()->getTeamID()) && (pHouse != getOwner())) {
            pHouse->informDirectContactWithEnemy();
            getOwner()->informDirectContactWithEnemy();
            
            // TODO: Dynasty behavior (line 3188-3189): AMBUSH units switch to HUNT when spotted by enemy
            // Disabled for now - visibility system triggers too early (units switch even in shroud)
            // Damage-based AMBUSH->HUNT switching in handleDamage() is sufficient
            /*
            if(attackMode == AMBUSH) {
                SDL_Log("Unit %d (%s) in AMBUSH spotted by enemy house %d - switching to HUNT",
                        getObjectID(), resolveItemName(getItemID()).c_str(), h);
                doSetAttackMode(HUNT);
                findTargetTimer = 0;  // Allow immediate target search
            }
            */
            
            // ORIGINAL AI: Ground-only contact triggers mutual activation (unit.c:3111-3116)
            // Only ground units (not wingers/carryalls/ornithopters) activate AI
            if(isAGroundUnit() && !isAFlyingUnit()) {
                pHouse->activateAI();        // Observing house activates
                getOwner()->activateAI();    // Unit's house also activates (mutual)
            }
        }

        if(pTile->isExploredByTeam(pHouse->getTeamID())) {
            if(pHouse->getTeamID() == getOwner()->getTeamID()) {
                pHouse->informVisibleFriendlyUnit();
            } else {
                pHouse->informVisibleEnemyUnit();
                pHouse->informContactWithEnemy();
                getOwner()->informContactWithEnemy();
            }
        }
    }
}

bool UnitBase::canPass(int xPos, int yPos) const {
    if(!currentGameMap->tileExists(xPos, yPos)) {
        return false;
    }

    Tile* pTile = currentGameMap->getTile(xPos, yPos);

    if(pTile->isMountain()) {
        return false;
    }

    if(pTile->hasAGroundObject()) {
        ObjectBase *pObject = pTile->getGroundObject();

        if( (pObject != nullptr)
            && (pObject->getObjectID() == target.getObjectID())
            && targetFriendly
            && pObject->isAStructure()
            && (pObject->getOwner()->getTeamID() == owner->getTeamID())
            && pObject->isVisible(getOwner()->getTeamID()))
        {
            // are we entering a repair yard?
            return (goingToRepairYard && (pObject->getItemID() == Structure_RepairYard) && static_cast<const RepairYard*>(pObject)->isFree());
        } else {
            return false;
        }
    }

    return true;
}

Coord UnitBase::resolvePathDestination() const {
    if(target && target.getObjPointer() != nullptr) {
        const ObjectBase* pTargetObject = target.getObjPointer();

        if(isCarryallUnit(itemID) && getHarvesterDropoff(pTargetObject) != nullptr) {
            const auto* dropoff = getHarvesterDropoff(pTargetObject);
            return pTargetObject->getLocation() + Coord(dropoff->getStructureSizeX() - 1, 0);
        } else if(itemID == Unit_Frigate && pTargetObject->getItemID() == Structure_StarPort) {
            return pTargetObject->getLocation() + Coord(1,1);
        }

        return pTargetObject->getClosestPoint(location);
    }

    return destination;
}

void UnitBase::updateCachedPathMetadata(const Coord& destinationCoord) {
    cachedPathDestination = destinationCoord;
    cachedPathRevision = (currentGameMap != nullptr) ? currentGameMap->getPathingRevision() : 0;
}

bool UnitBase::isCachedPathStillValid() {
    auto registerHit = [&]() {
        if(currentGame != nullptr) {
            currentGame->frameTiming.pathReuseHitsWindow++;
        }
    };
    auto registerMiss = [&]() {
        if(currentGame != nullptr) {
            currentGame->frameTiming.pathReuseMissesWindow++;
        }
    };

    if(pathList.empty() || currentGameMap == nullptr) {
        registerMiss();
        return false;
    }

    Coord desiredDestination = resolvePathDestination();
    if(!desiredDestination.isValid()) {
        registerMiss();
        return false;
    }
    
    // HUNT mode optimization: allow path reuse if target moved slightly
    // Only apply tolerance when queue is building up (>= 10) to reduce load
    if(desiredDestination != cachedPathDestination) {
        if(attackMode == HUNT) {
            // Check if queue is building up
            const bool queueBusy = (currentGame != nullptr) && 
                                   (currentGame->getPathRequestQueueSize() >= 10);
            
            if(queueBusy) {
                FixPoint distance = blockDistance(desiredDestination, cachedPathDestination);
                if(distance < 5) {
                    // Queue is building up and target moved < 5 tiles - keep heading in that direction
                    cachedPathDestination = desiredDestination;
                    registerHit();
                    return true;
                }
            }
            
            // Queue is healthy or target moved >5 tiles - repath for responsiveness
            if(currentGame != nullptr) {
                currentGame->frameTiming.pathInvalidHuntTooFar++;
            }
        } else {
            if(currentGame != nullptr) {
                currentGame->frameTiming.pathInvalidDestChanged++;
            }
        }
        
        // Not in HUNT or destination changed significantly
        registerMiss();
        return false;
    }

    const Uint32 currentRevision = currentGameMap->getPathingRevision();
    if(cachedPathRevision == currentRevision) {
        registerHit();
        return true;
    }

    int nodesChecked = 0;
    for(const Coord& coord : pathList) {
        if(!currentGameMap->tileExists(coord) || !canPass(coord.x, coord.y)) {
            if(currentGame != nullptr) {
                currentGame->frameTiming.pathInvalidBlocked++;
            }
            registerMiss();
            return false;
        }

        if(++nodesChecked >= kPathValidationProbeCount) {
            registerHit();
            return true;
        }
    }

    cachedPathRevision = currentRevision;
    registerHit();
    return true;
}

bool UnitBase::SearchPathWithAStar(std::size_t& nodesExpanded, bool& invalidDestination) {
    nodesExpanded = 0;
    invalidDestination = false;

    Coord destinationCoord = resolvePathDestination();

    AStarSearch pathfinder(currentGameMap, this, location, destinationCoord);
    nodesExpanded = static_cast<size_t>(pathfinder.getNodesChecked());
    pathList = pathfinder.getFoundPath();

    if(pathList.empty()) {
        cachedPathDestination.invalidate();
        cachedPathRevision = 0;
        nextSpotFound = false;
        return false;
    }

    updateCachedPathMetadata(destinationCoord);
    return true;
}

void UnitBase::drawSmoke(int x, int y) const {
    int frame = ((currentGame->getGameCycleCount() + (getObjectID() * 10)) / SMOKEDELAY) % (2*2);
    if(frame == 3) {
        frame = 1;
    }

    SDL_Texture* pSmokeTex = pGFXManager->getZoomedObjPic(ObjPic_Smoke, getOwner()->getHouseID(), currentZoomlevel);
    SDL_Rect dest = calcSpriteDrawingRect(pSmokeTex, x, y, 3, 1, HAlign::Center, VAlign::Bottom);
    SDL_Rect source = calcSpriteSourceRect(pSmokeTex, frame, 3);

    SDL_RenderCopy(renderer, pSmokeTex, &source, &dest);
}

void UnitBase::playAttackSound() {
}
