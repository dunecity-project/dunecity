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

#include <Bullet.h>
#include <DynastyProjectile.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <SoundPlayer.h>
#include <ObjectBase.h>
#include <Game.h>
#include <Map.h>
#include <House.h>
#include <Explosion.h>

#include <misc/draw_util.h>
#include <misc/exceptions.h>

#include <algorithm>


Bullet::Bullet(Uint32 shooterID, Coord* newRealLocation, Coord* newRealDestination, Uint32 bulletID, int damage, bool air, const ObjectBase* pTarget)
{
    airAttack = air;

    this->shooterID = shooterID;

    this->owner = currentGame->getObjectManager().getObject(shooterID)->getOwner();

    this->bulletID = bulletID;

    this->damage = damage;

    target.pointTo(pTarget);

    Bullet::init();

    const ObjectBase* pShooter = currentGame->getObjectManager().getObject(shooterID);
    const bool usesPreciseFlameTrajectory = bulletID == Bullet_Flame
        && pShooter != nullptr
        && (pShooter->getItemID() == Unit_Trooper
            || pShooter->getItemID() == Structure_Flamepost);
    if(usesPreciseFlameTrajectory) {
        // Trooper rockets travel directly to their target. Keep the flame
        // projectile and impact, but remove the delayed point-blank miss.
        detonationTimer = 0;
    }

    if(DynastyProjectile::parameters(bulletID).step) {
        if(pTarget && pTarget->isAFlyingUnit()) detonationTimer *= 2;
    } else if(bulletID == Bullet_Flame && detonationTimer > 0
              && pTarget && pTarget->isAFlyingUnit()) {
        detonationTimer = 50;
    }

    destination = *newRealDestination;

    if(bulletID == Bullet_Sonic || bulletID == Bullet_SonicTrike) {
        int diffX = destination.x - newRealLocation->x;
        int diffY = destination.y - newRealLocation->y;

        const int sourceUnit = (bulletID == Bullet_SonicTrike) ? Unit_SonicTrike : Unit_SonicTank;
        int weaponrange = currentGame->objectData.data[sourceUnit][owner->getHouseID()].weaponrange;

        if((diffX == 0) && (diffY == 0)) {
            diffY = weaponrange*TILESIZE;
        }

        FixPoint square_root = FixPoint::sqrt(diffX*diffX + diffY*diffY);
        FixPoint ratio = (weaponrange*TILESIZE)/square_root;
        destination.x = newRealLocation->x + floor(diffX*ratio);
        destination.y = newRealLocation->y + floor(diffY*ratio);
    } else if(bulletID == Bullet_Rocket || bulletID == Bullet_DRocket) {
        const int tiles = DynastyProjectile::distance(*newRealLocation, *newRealDestination) / TILESIZE;
        const int limit = currentGame->randomGen.rand(0, 15) != 0
            ? tiles + 8 : currentGame->randomGen.rand(0, 255) + 8;
        int radius = currentGame->randomGen.rand(0, 255);
        while(radius > limit) radius /= 2;
        const int direction = currentGame->randomGen.rand(0, 255);
        const Coord scattered = destination + Coord(
            (DynastyProjectile::k_stepX[direction] * radius / 128) * 4,
            -(DynastyProjectile::k_stepY[direction] * radius / 128) * 4);
        // Dynasty rejects scatter outside its map instead of wrapping it.
        if(scattered.x >= 0 && scattered.y >= 0
           && scattered.x < currentGameMap->getSizeX()*TILESIZE
           && scattered.y < currentGameMap->getSizeY()*TILESIZE) destination = scattered;
    } else if(bulletID == Bullet_Flame && !usesPreciseFlameTrajectory) {
        // Mod-only flames retain their previous scatter and flight rules.
        const int tiles = std::max(0, lround(distanceFrom(*newRealLocation, *newRealDestination)/TILESIZE));
        const int limit = currentGame->randomGen.rand(0, 15) != 0 ? tiles+8 : currentGame->randomGen.rand(0, 255)+8;
        int radius = currentGame->randomGen.rand(0, 255);
        while(radius > limit) radius /= 2;
        const auto theta = 2 * FixPt_PI * currentGame->randomGen.randFixPoint();
        destination.x += lround(FixPoint::cos(theta)*radius);
        destination.y -= lround(FixPoint::sin(theta)*radius);
    }

    realX = newRealLocation->x;
    realY = newRealLocation->y;
    source.x = newRealLocation->x;
    source.y = newRealLocation->y;
    location.x = newRealLocation->x/TILESIZE;
    location.y = newRealLocation->y/TILESIZE;

    // v0.96.4: Launch toward original target, rockets steer toward scattered destination during flight
    FixPoint angleRad =  destinationAngleRad(*newRealLocation, *newRealDestination);
    angle = RadToDeg256(angleRad);
    drawnAngle = lround(numFrames*angle/256) % numFrames;

    xSpeed = speed * FixPoint::cos(angleRad);
    ySpeed = speed * -FixPoint::sin(angleRad);
    if(DynastyProjectile::parameters(bulletID).step) {
        projectileHeading = DynastyProjectile::direction(*newRealLocation * 4, *newRealDestination * 4) & 255;
        projectileAim = projectileHeading;
        projectileTargetPosition = pTarget ? pTarget->getCenterPoint()*4 : *newRealDestination*4;
        projectileClock = (currentGame->getGameCycleCount()*48ULL) % 600;
        angle = (64-projectileHeading+256)&255;
        drawnAngle = lround(numFrames*angle/256) % numFrames;
    }
}

Bullet::Bullet(InputStream& stream)
{
    bulletID = stream.readUint32();

    airAttack = stream.readBool();
    target.load(stream);
    damage = stream.readSint32();

    shooterID = stream.readUint32();
    Uint32 x = stream.readUint32();
    if(x < NUM_HOUSES) {
        owner = currentGame->getHouse(x);
    } else {
        owner = currentGame->getHouse(0);
    }

    source.x = stream.readSint32();
    source.y = stream.readSint32();
    destination.x = stream.readSint32();
    destination.y = stream.readSint32();
    location.x = stream.readSint32();
    location.y = stream.readSint32();
    realX = stream.readFixPoint();
    realY = stream.readFixPoint();

    xSpeed = stream.readFixPoint();
    ySpeed = stream.readFixPoint();

    drawnAngle = stream.readSint8();
    angle = stream.readFixPoint();

    Bullet::init();

    if(currentGame->getLoadedSavegameVersion() >= 9845) {
        detonationTimer = stream.readSint16();
        projectileClock = stream.readUint16();
        projectileHeading = stream.readUint8();
        projectileAim = stream.readUint8();
        projectileTurn = stream.readSint8();
        projectileDistance = stream.readSint32();
        projectileTargetPosition.x = stream.readSint32();
        projectileTargetPosition.y = stream.readSint32();
        if(projectileClock >= 600 || projectileDistance < 0) THROW(std::runtime_error, "Invalid projectile state");
    } else {
        detonationTimer = stream.readSint8();
        if(DynastyProjectile::parameters(bulletID).step) {
            // Old saves count 16ms cycles, new missiles count 50ms movement updates.
            detonationTimer = std::max(0, (detonationTimer*16+49)/50);
            projectileHeading = (64-lround(angle)+256)&255;
            projectileAim = projectileHeading;
            const auto* object = target.getObjPointer();
            projectileTargetPosition = object ? object->getCenterPoint()*4 : destination*4;
            projectileClock = (currentGame->getGameCycleCount()*48ULL) % 600;
        }
    }
}

void Bullet::init()
{
    explodesAtGroundObjects = false;

    int houseID = owner->getHouseID();

    switch(bulletID) {
        case Bullet_DRocket: {
            numFrames = 16;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_MediumRocket, houseID);
        } break;

                case Bullet_Heal: {
            damageRadius = 0;
            speed = 19.2_fix;
            detonationTimer = -1;
            numFrames = 16;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_MediumRocket, houseID);
        } break;
case Bullet_LargeRocket: {
            numFrames = 16;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_LargeRocket, houseID);
        } break;

        case Bullet_Rocket: {
            numFrames = 16;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_MediumRocket, houseID);
        } break;

        case Bullet_TurretRocket: {
            numFrames = 16;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_MediumRocket, houseID);
        } break;

        case Bullet_ShellSmall: {
            damageRadius = TILESIZE/2;
            explodesAtGroundObjects = true;
            speed = 20;
            detonationTimer = -1;
            numFrames = 1;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_Small, houseID);
        } break;

        case Bullet_ShellMedium: {
            damageRadius = TILESIZE/2;
            explodesAtGroundObjects = true;
            speed = 20;
            detonationTimer = -1;
            numFrames = 1;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_Medium, houseID);
        } break;

        case Bullet_ShellLarge: {
            damageRadius = TILESIZE/2;
            explodesAtGroundObjects = true;
            speed = 20;
            detonationTimer = -1;
            numFrames = 1;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_Large, houseID);
        } break;

        case Bullet_ShellTurret: {
            damageRadius = TILESIZE/2;
            explodesAtGroundObjects = true;
            speed = 20;
            detonationTimer = -1;
            numFrames = 1;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_Medium, houseID);
        } break;

        case Bullet_SmallRocket: {
            numFrames = 16;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_SmallRocket, houseID);
        } break;

        case Bullet_Sonic:
        case Bullet_SonicTrike: {
            damageRadius = (TILESIZE*3)/4;
            speed = 6;  // For Sonic bullets this is only half the actual speed; see Bullet::update()
            numFrames = 1;
            detonationTimer = (bulletID == Bullet_SonicTrike) ? 28 : 45;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_Sonic, HOUSE_HARKONNEN);    // no color remapping
        } break;

        case Bullet_Sandworm: {
            THROW(std::domain_error, "Cannot init 'Bullet_Sandworm': Not allowed!");
        } break;

        case Bullet_Flame: {
            damageRadius = TILESIZE;
            speed = 19.2_fix;
            numFrames = 16;
            detonationTimer = 22;
            graphic = pGFXManager->getObjPic(ObjPic_Bullet_MediumRocket, HOUSE_HARKONNEN);
        } break;

        default: {
            THROW(std::domain_error, "Unknown Bullet type %d!", bulletID);
        } break;
    }
    if(const auto p = DynastyProjectile::parameters(bulletID); p.step) {
        speed = FixPoint(p.step)*0.08_fix; // quarter-world units/step, 20Hz -> 62.5Hz
        detonationTimer = p.delay;
        damageRadius = (bulletID == Bullet_DRocket || bulletID == Bullet_LargeRocket) ? 2*TILESIZE : TILESIZE;
    }
}


Bullet::~Bullet() = default;

void Bullet::save(OutputStream& stream) const
{
    stream.writeUint32(bulletID);

    stream.writeBool(airAttack);
    target.save(stream);
    stream.writeSint32(damage);

    stream.writeUint32(shooterID);
    stream.writeUint32(owner->getHouseID());

    stream.writeSint32(source.x);
    stream.writeSint32(source.y);
    stream.writeSint32(destination.x);
    stream.writeSint32(destination.y);
    stream.writeSint32(location.x);
    stream.writeSint32(location.y);
    stream.writeFixPoint(realX);
    stream.writeFixPoint(realY);

    stream.writeFixPoint(xSpeed);
    stream.writeFixPoint(ySpeed);

    stream.writeSint8(drawnAngle);
    stream.writeFixPoint(angle);

    stream.writeSint16(detonationTimer);
    stream.writeUint16(projectileClock);
    stream.writeUint8(projectileHeading);
    stream.writeUint8(projectileAim);
    stream.writeSint8(projectileTurn);
    stream.writeSint32(projectileDistance);
    stream.writeSint32(projectileTargetPosition.x);
    stream.writeSint32(projectileTargetPosition.y);
}


void Bullet::blitToScreen() const
{
    int imageW = getWidth(graphic[currentZoomlevel])/numFrames;
    int imageH = getHeight(graphic[currentZoomlevel]);

    if(screenborder->isInsideScreen( Coord(lround(realX), lround(realY)), Coord(imageW, imageH)) == false) {
        return;
    }

    SDL_Rect dest = calcSpriteDrawingRect(graphic[currentZoomlevel], screenborder->world2screenX(realX), screenborder->world2screenY(realY), numFrames, 1, HAlign::Center, VAlign::Center);

    if(bulletID == Bullet_Sonic || bulletID == Bullet_SonicTrike) {
        static const int shimmerOffset[]  = { 1, 3, 2, 5, 4, 3, 2, 1 };

        SDL_Texture* shimmerTex = pGFXManager->getZoomedObjPic(ObjPic_Bullet_SonicTemp, currentZoomlevel);
        SDL_Texture* shimmerMaskTex = pGFXManager->getZoomedObjPic(ObjPic_Bullet_Sonic, currentZoomlevel);

        // switch to texture 'shimmerTex' for rendering
        SDL_Texture* oldRenderTarget = SDL_GetRenderTarget(renderer);
        SDL_SetRenderTarget(renderer, shimmerTex);

        // copy complete mask
        // contains solid black (0,0,0,255) for pixels to take from screen
        // and transparent (0,0,0,0) for pixels that should not be copied over
        SDL_SetTextureBlendMode(shimmerMaskTex, SDL_BLENDMODE_NONE);
        SDL_RenderCopy(renderer, shimmerMaskTex, nullptr, nullptr);
        SDL_SetTextureBlendMode(shimmerMaskTex, SDL_BLENDMODE_BLEND);

        // now copy r,g,b colors from screen but don't change alpha values in mask
        SDL_SetTextureBlendMode(screenTexture, SDL_BLENDMODE_ADD);
        SDL_Rect source = dest;
        int shimmerOffsetIndex = ((currentGame->getGameCycleCount() + getBulletID()) % 24)/3;
        source.x += shimmerOffset[shimmerOffsetIndex%8]*2;
        SDL_RenderCopy(renderer, screenTexture, &source, nullptr);
        SDL_SetTextureBlendMode(screenTexture, SDL_BLENDMODE_NONE);

        // switch back to old rendering target (from texture 'shimmerTex')
        SDL_SetRenderTarget(renderer, oldRenderTarget);

        // now blend shimmerTex to screen (= make use of alpha values in mask)
        SDL_SetTextureBlendMode(shimmerTex, SDL_BLENDMODE_BLEND);
        SDL_RenderCopy(renderer, shimmerTex, nullptr, &dest);
    } else {
        SDL_Texture* pBulletGraphic = graphic[currentZoomlevel];
        SDL_Rect source = calcSpriteSourceRect(pBulletGraphic, (numFrames > 1) ? drawnAngle: 0, numFrames);
        if(bulletID == Bullet_Flame) {
            SDL_SetTextureColorMod(pBulletGraphic, 255, 125, 35);
            SDL_RenderCopy(renderer, pBulletGraphic, &source, &dest);
            SDL_SetTextureColorMod(pBulletGraphic, 255, 255, 255);
        } else {
            SDL_RenderCopy(renderer, pBulletGraphic, &source, &dest);
        }
    }
}


// Preserve Dynasty's movement-before-aim-before-rotation order. The clocks are
// independent of rendering and game-speed preferences, like the unit clocks.
void Bullet::updateDynastyProjectile()
{
    const auto parameters = DynastyProjectile::parameters(bulletID);
    for(int elapsed=0; elapsed<48; ++elapsed) {
        if(projectileClock % 150 == 0) {
            const Coord previous(lround(realX*4), lround(realY*4));
            const Coord goal = destination*4;
            const auto step = std::min(parameters.step, DynastyProjectile::distance(previous, goal)+16);
            const auto position = DynastyProjectile::move(previous, projectileHeading, step);
            xSpeed = FixPoint(position.x-previous.x)*0.08_fix;
            ySpeed = FixPoint(position.y-previous.y)*0.08_fix;
            realX = FixPoint(position.x)/4;
            realY = FixPoint(position.y)/4;
            location = Coord(floor(realX/TILESIZE), floor(realY/TILESIZE));
            if(!currentGameMap->tileExists(location)) {
                bulletList.remove(this);
                delete this;
                return;
            }
            const auto* liveTarget = target.getObjPointer();
            if((bulletID == Bullet_TurretRocket || bulletID == Bullet_DRocket)
               && liveTarget && liveTarget->getHealth() > 0
               && liveTarget->isAFlyingUnit()) {
                const Coord targetNow = liveTarget->getCenterPoint()*4;
                // A narrow physical intercept compensates for 20Hz missiles crossing
                // 62.5Hz aircraft between samples. Work in relative motion, so crossing
                // the same place at different times does not count as a collision.
                // This is an intentional anti-air extension to Dynasty, not its
                // old destination snap or full-radius ornithopter damage bonus.
                const Coord a = previous-projectileTargetPosition;
                const Coord d = (position-targetNow)-a;
                const Sint64 length = Sint64(d.x)*d.x+Sint64(d.y)*d.y;
                const Sint64 projection = -(Sint64(a.x)*d.x+Sint64(a.y)*d.y);
                const Sint64 t = length ? std::clamp<Sint64>(projection*1024/length,0,1024) : 0;
                const Sint64 dx = Sint64(a.x)*1024+Sint64(d.x)*t;
                const Sint64 dy = Sint64(a.y)*1024+Sint64(d.y)*t;
                projectileTargetPosition = targetNow;
                if(dx*dx+dy*dy <= Sint64(32*1024)*(32*1024)) { // one eighth of a tile
                    realX = FixPoint(previous.x+int((position.x-previous.x)*t/1024))/4;
                    realY = FixPoint(previous.y+int((position.y-previous.y)*t/1024))/4;
                    if(bulletID == Bullet_TurretRocket)
                        currentGame->combatStats.turretRocketsProximityDetonated++;
                    destroy(liveTarget->getObjectID());
                    return;
                }
            }
            const int distance = DynastyProjectile::distance(position, goal);
            if((distance < 16 || distance > projectileDistance)
               && (detonationTimer == 0 || bulletID == Bullet_TurretRocket)) {
                if(bulletID == Bullet_TurretRocket && target.getObjPointer()
                   && target.getObjPointer()->getItemID() == Unit_Ornithopter)
                    currentGame->combatStats.turretRocketsProximityDetonated++;
                // Always explode at the actual missile position. Never snap to a target.
                destroy();
                return;
            }
            projectileDistance = distance;
            if(detonationTimer > 0) {
                Coord aim = goal;
                const auto* object = target.getObjPointer();
                if(object && object->getHealth() > 0 && object->isAFlyingUnit())
                    aim = object->getCenterPoint()*4;
                projectileAim = DynastyProjectile::direction(position, aim) & 255;
                // Signed orientation arithmetic matches Unit_SetOrientation,
                // including its exact half-turn tie choice.
                int difference = static_cast<Sint8>(projectileAim) - static_cast<Sint8>(projectileHeading);
                projectileTurn = projectileAim == projectileHeading ? 0 :
                    (((difference > -128 && difference < 0) || difference > 128) ? -parameters.turn : parameters.turn);
                --detonationTimer;
            }
        }
        if(projectileClock % 200 == 0 && projectileTurn != 0) {
            int difference = int(projectileAim)-int(projectileHeading);
            if(difference > 128) difference -= 256;
            if(difference < -128) difference += 256;
            if(std::abs(projectileTurn) >= std::abs(difference)) {
                projectileHeading = projectileAim;
                projectileTurn = 0;
            } else {
                projectileHeading = (projectileHeading+projectileTurn+256)&255;
            }
        }
        projectileClock = (projectileClock+1)%600;
    }
    angle = (64-projectileHeading+256)&255;
    drawnAngle = lround(numFrames*angle/256)%numFrames;
}

void Bullet::update()
{
    if(DynastyProjectile::parameters(bulletID).step) {
        updateDynastyProjectile();
        return;
    }
    if(bulletID == Bullet_Flame) {

        // Mod flame compatibility: track ornithopters while retaining the old fuse.
        ObjectBase* pTarget = target.getObjPointer();
        
        if(pTarget != nullptr && pTarget->getItemID() == Unit_Ornithopter) {
            // Track the moving ornithopter's current position (for both steering AND proximity check)
            destination = pTarget->getCenterPoint();
        }
        // Carryalls & ground targets: use the static scattered destination (no tracking)

        FixPoint angleToDestinationRad = destinationAngleRad(Coord(lround(realX), lround(realY)), destination);
        FixPoint angleToDestination = RadToDeg256(angleToDestinationRad);

        FixPoint angleDifference = angleToDestination - angle;
        if(angleDifference > 128) {
            angleDifference -= 256;
        } else if(angleDifference < -128) {
            angleDifference += 256;
        }

        // Legacy mod rate: 4.5 angle units (256 per revolution) per cycle.
        static const FixPoint turnSpeed = 4.5_fix;

        if(angleDifference >= turnSpeed) {
            angleDifference = turnSpeed;
        } else if(angleDifference <= -turnSpeed) {
            angleDifference = -turnSpeed;
        }

        angle += angleDifference;

        if(angle < 0) {
            angle += 256;
        } else if(angle >= 256) {
            angle -= 256;
        }

        xSpeed = speed * FixPoint::cos(Deg256ToRad(angle));
        ySpeed = speed * -FixPoint::sin(Deg256ToRad(angle));

        drawnAngle = lround(numFrames*angle/256) % numFrames;
    } else if(bulletID == Bullet_Heal) {

        // Mod healing projectiles follow their live target.
        ObjectBase* pTarget = target.getObjPointer();
        
        if(pTarget != nullptr) {
            // Update destination to track the moving target's current position
            destination = pTarget->getCenterPoint();
        }
        // If target destroyed, continue to last known position (destination unchanged)

        FixPoint angleToDestinationRad = destinationAngleRad(Coord(lround(realX), lround(realY)), destination);
        FixPoint angleToDestination = RadToDeg256(angleToDestinationRad);

        FixPoint angleDifference = angleToDestination - angle;
        if(angleDifference > 128) {
            angleDifference -= 256;
        } else if(angleDifference < -128) {
            angleDifference += 256;
        }

        // Legacy mod rate: 4.5 angle units (256 per revolution) per cycle.
        static const FixPoint turnSpeed = 4.5_fix;

        if(angleDifference >= turnSpeed) {
            angleDifference = turnSpeed;
        } else if(angleDifference <= -turnSpeed) {
            angleDifference = -turnSpeed;
        }

        angle += angleDifference;

        if(angle < 0) {
            angle += 256;
        } else if(angle >= 256) {
            angle -= 256;
        }

        xSpeed = speed * FixPoint::cos(Deg256ToRad(angle));
        ySpeed = speed * -FixPoint::sin(Deg256ToRad(angle));

        drawnAngle = lround(numFrames*angle/256) % numFrames;
    }


    FixPoint oldDistanceToDestination = distanceFrom(realX, realY, destination.x, destination.y);

    realX += xSpeed;  //keep the bullet moving by its current speeds
    realY += ySpeed;
    location.x = floor(realX/TILESIZE);
    location.y = floor(realY/TILESIZE);

    if((location.x < -5) || (location.x >= currentGameMap->getSizeX() + 5) || (location.y < -5) || (location.y >= currentGameMap->getSizeY() + 5)) {
        // it's off the map => delete it
        bulletList.remove(this);
        delete this;
        return;
    } else {
        FixPoint newDistanceToDestination = distanceFrom(realX, realY, destination.x, destination.y);

        if(detonationTimer > 0) {
            detonationTimer--;
        }

        if(bulletID == Bullet_Sonic || bulletID == Bullet_SonicTrike) {

            if(detonationTimer == 0) {
                destroy();
                return;
            }

            const int sourceUnit = (bulletID == Bullet_SonicTrike) ? Unit_SonicTrike : Unit_SonicTank;
            const int sonicDuration = (bulletID == Bullet_SonicTrike) ? 28 : 45;
            FixPoint weaponDamage = currentGame->objectData.data[sourceUnit][owner->getHouseID()].weapondamage;

            FixPoint startDamage = (weaponDamage / 4 + 1) / 4.5_fix;
            FixPoint endDamage = ((weaponDamage-9) / 4 + 1) / 4.5_fix;

            FixPoint damageDecrease = - (startDamage-endDamage)/(sonicDuration * 2 * speed);
            FixPoint dist = distanceFrom(source.x, source.y, realX, realY);

            FixPoint currentDamage = dist*damageDecrease + startDamage;

            Coord realPos = Coord(lround(realX), lround(realY));
            currentGameMap->damage(shooterID, owner, realPos, bulletID, currentDamage/2, damageRadius, false);

            realX += xSpeed;  //keep the bullet moving by its current speeds
            realY += ySpeed;

            realPos = Coord(lround(realX), lround(realY));
            currentGameMap->damage(shooterID, owner, realPos, bulletID, currentDamage/2, damageRadius, false);
        } else if( explodesAtGroundObjects
                    && currentGameMap->tileExists(location)
                    && currentGameMap->getTile(location)->hasAGroundObject()
                    && currentGameMap->getTile(location)->getGroundObject()->isAStructure()
                    && ((bulletID != Bullet_ShellTurret) || (currentGameMap->getTile(location)->getGroundObject()->getOwner() != owner))) {
            destroy();
            return;
        }

        if(oldDistanceToDestination < newDistanceToDestination || newDistanceToDestination < 4)  {

            if(bulletID == Bullet_Flame) {
                // Check if targeting a flying unit
                ObjectBase* pTarget = target.getObjPointer();
                bool isAirTarget = (pTarget != nullptr && pTarget->isAFlyingUnit());
                
                if(isAirTarget) {
                    // Preserve the mod flame air-impact behavior.
                    // Scatter was already applied at launch, tracking during flight
                    destroy();
                    return;
                } else {
                    // v0.96.4: Ground targets only detonate if timer has expired
                    // This creates the point-blank miss behavior
                    if(detonationTimer == 0) {
                destroy();
                return;
                    }
                }
            } else {
                realX = destination.x;
                realY = destination.y;
                destroy();
                return;
            }
        }

    }
}


void Bullet::destroy(Uint32 interceptedAirUnit)
{
    Coord position = Coord(lround(realX), lround(realY));

    int houseID = owner->getHouseID();

    switch(bulletID) {
        case Bullet_DRocket: {
            currentGameMap->damage(shooterID, owner, position, bulletID, damage, damageRadius, airAttack, true, interceptedAirUnit);
            soundPlayer->playSoundAt(Sound_ExplosionGas, position);
            currentGame->getExplosionList().push_back(new Explosion(Explosion_Gas,position,houseID));
        } break;

                case Bullet_Heal: {
            ObjectBase* pTarget = target.getObjPointer();
            if(pTarget != nullptr && pTarget->getHealth() > 0
                    && pTarget->getHealth() < pTarget->getMaxHealth()
                    && pTarget->getOwner()->getTeamID() == owner->getTeamID()) {
                pTarget->setHealth(std::min(FixPoint(pTarget->getMaxHealth()), pTarget->getHealth() + FixPoint(damage)));
            }
            soundPlayer->playSoundAt(Sound_ExplosionGas, position);
            currentGame->getExplosionList().push_back(new Explosion(Explosion_Gas, position, houseID));
        } break;
case Bullet_LargeRocket: {
            soundPlayer->playSoundAt(Sound_ExplosionLarge, position);

            // Dynasty's seventeen Death Hand blast centres (256 -> 64 coordinates).
            static constexpr int dx[] = {0,0,50,64,50,0,-50,-64,-50,0,100,128,100,0,-100,-128,-100};
            static constexpr int dy[] = {0,-64,-50,0,50,64,50,0,-50,-128,-100,0,100,128,100,0,-100};
            for(int i=0; i<17; ++i) {
                position = Coord(lround(realX)+dx[i], lround(realY)+dy[i]);
                if(position.x < 0 || position.y < 0 || position.x >= currentGameMap->getSizeX()*TILESIZE
                   || position.y >= currentGameMap->getSizeY()*TILESIZE) continue;
                currentGameMap->damage(shooterID, owner, position, bulletID, damage, damageRadius, airAttack);
                const auto explosionID = currentGame->randomGen.getRandOf({Explosion_Large1,Explosion_Large2});
                currentGame->getExplosionList().push_back(new Explosion(explosionID,position,houseID));
                screenborder->shakeScreen(22);
            }
        } break;

        case Bullet_Rocket:
        case Bullet_TurretRocket:
        case Bullet_SmallRocket: {
            currentGameMap->damage(shooterID, owner, position, bulletID, damage, damageRadius, airAttack, true, interceptedAirUnit);
            currentGame->getExplosionList().push_back(new Explosion(Explosion_Small,position,houseID));
        } break;

        case Bullet_Flame: {
            currentGameMap->damage(shooterID, owner, position, bulletID, damage, damageRadius, false);
            soundPlayer->playSoundAt(Sound_ExplosionSmall, position);
            const int persistentDamage = std::max(1, damage / 10);
            currentGame->getExplosionList().push_back(
                new Explosion(Explosion_FlameImpact, position, houseID, shooterID, persistentDamage, damageRadius));

            for(int i = 0; i < 2; i++) {
                Coord flamePos = position;
                flamePos.x += currentGame->randomGen.rand(-TILESIZE/3, TILESIZE/3);
                flamePos.y += currentGame->randomGen.rand(-TILESIZE/3, TILESIZE/3);
                currentGame->getExplosionList().push_back(new Explosion(Explosion_FlameImpactVisual, flamePos, houseID));
            }
        } break;

        case Bullet_ShellSmall: {
            currentGameMap->damage(shooterID, owner, position, bulletID, damage, damageRadius, airAttack);
            currentGame->getExplosionList().push_back(new Explosion(Explosion_ShellSmall,position,houseID));
        } break;

        case Bullet_ShellMedium: {
            currentGameMap->damage(shooterID, owner, position, bulletID, damage, damageRadius, airAttack);
            currentGame->getExplosionList().push_back(new Explosion(Explosion_ShellMedium,position,houseID));
        } break;

        case Bullet_ShellLarge: {
            currentGameMap->damage(shooterID, owner, position, bulletID, damage, damageRadius, airAttack);
            currentGame->getExplosionList().push_back(new Explosion(Explosion_ShellLarge,position,houseID));
        } break;

        case Bullet_ShellTurret: {
            currentGameMap->damage(shooterID, owner, position, bulletID, damage, damageRadius, airAttack);
            currentGame->getExplosionList().push_back(new Explosion(Explosion_ShellMedium,position,houseID));
        } break;

        case Bullet_Sonic:
        case Bullet_SonicTrike:
        case Bullet_Sandworm:
        default: {
            // do nothing
        } break;
    }

    bulletList.remove(this);
    delete this;
}
