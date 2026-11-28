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

#include <units/SandWorm.h>
#include <Achievements/AchievementEvents.h>
#include <mod/ModManager.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <ScreenBorder.h>
#include <SoundPlayer.h>

#include <misc/draw_util.h>

#include <units/InfantryBase.h>
#include <structures/StructureBase.h>
#include <units/PalaceWormRules.h>
#include <players/AIDecisionLog.h>
#include <FileClasses/TextManager.h>

#include <cstring>
#include <limits>

#define MAX_SANDWORMSLEEPTIME 50000
#define MIN_SANDWORMSLEEPTIME 10000

#define SANDWORM_ATTACKFRAMETIME 10

namespace {

constexpr Uint16 warningWormSignMaskForHouse(int houseID) {
    return houseID >= 0 && houseID < NUM_HOUSES
        ? static_cast<Uint16>(1u << static_cast<unsigned int>(houseID))
        : 0;
}

static_assert(warningWormSignMaskForHouse(HOUSE_CUSTOM) != 0,
              "The worm-sign warning mask must represent the custom house");

} // namespace

Sandworm::Sandworm(House* newOwner, int unitType) : GroundUnit(newOwner) {

    Sandworm::init(unitType);
    // New worms default to ambush; loaded worms retain their saved orders.
    doSetAttackMode(itemID == Unit_TameSandworm ? GUARD : AMBUSH);

    setHealth(getMaxHealth());

    kills = 0;
    attackFrameTimer = 0;
    sleepTimer = 0;
    warningWormSignPlayedFlags = 0;
    respondable = itemID == Unit_TameSandworm;

    for(int i = 0; i < SANDWORM_SEGMENTS; i++) {
        lastLocs[i].invalidate();
    }
    shimmerOffsetIndex = -1;
}

Sandworm::Sandworm(InputStream& stream, int unitType) : GroundUnit(stream) {

    Sandworm::init(unitType);

    kills = stream.readSint32();
    attackFrameTimer = stream.readSint32();
    sleepTimer = stream.readSint32();
    warningWormSignPlayedFlags = stream.readUint8();
    shimmerOffsetIndex = stream.readSint32();
    for(int i = 0; i < SANDWORM_SEGMENTS; i++) {
        lastLocs[i].x = stream.readSint32();
        lastLocs[i].y = stream.readSint32();
    }
    // An older save cannot say which worms a reinforcement brought: none spares anybody.
    if(currentGame->getLoadedSavegameVersion() >= SAVEGAMEVERSION_NATIVES_TEAM) {
        summonerHouseID = stream.readSint32();
        if(summonerHouseID < HOUSE_INVALID || summonerHouseID >= NUM_HOUSES) summonerHouseID = HOUSE_INVALID;
    }
    if(currentGame->getLoadedSavegameVersion() >= 9881) {
        respawnOrigin.x = stream.readSint32();
        respawnOrigin.y = stream.readSint32();
    }
    if(currentGame->getLoadedSavegameVersion() >= SAVEGAMEVERSION_PALACE_WORM) {
        palaceWorm = stream.readBool();
        palaceWormCyclesLeft = stream.readSint32();
        bloomTarget.x = stream.readSint32();
        bloomTarget.y = stream.readSint32();
    }
}

void Sandworm::init(int unitType) {
    itemID = unitType;
    registerUnit();

    numWeapons = 0;

    graphicID = itemID == Unit_TameSandworm ? ObjPic_TameSandworm : ObjPic_Sandworm;
    graphic = pGFXManager->getObjPic(graphicID,getOwner()->getHouseID());

    numImagesX = 1;
    numImagesY = 9;

    drawnFrame = INVALID;
    

}

Sandworm::~Sandworm() = default;

void Sandworm::save(OutputStream& stream) const {
    GroundUnit::save(stream);

    stream.writeSint32(kills);
    stream.writeSint32(attackFrameTimer);
    stream.writeSint32(sleepTimer);
    // Preserve the legacy one-byte save layout. The ninth-house warning bit is
    // transient and may be announced once again after loading a saved game.
    stream.writeUint8(static_cast<Uint8>(warningWormSignPlayedFlags & 0xFFu));
    stream.writeSint32(shimmerOffsetIndex);
    for(int i = 0; i < SANDWORM_SEGMENTS; i++) {
        stream.writeSint32(lastLocs[i].x);
        stream.writeSint32(lastLocs[i].y);
    }
    stream.writeSint32(summonerHouseID);
    stream.writeSint32(respawnOrigin.x);
    stream.writeSint32(respawnOrigin.y);
    stream.writeBool(palaceWorm);
    stream.writeSint32(palaceWormCyclesLeft);
    stream.writeSint32(bloomTarget.x);
    stream.writeSint32(bloomTarget.y);
}

Sandworm* Sandworm::createPalaceWorm(House* owner) {
    // ObjectBase::createObject hands every Unit_Sandworm to the Natives; this one is the Palace's.
    auto* worm = new Sandworm(owner);
    worm->setObjectID(currentGame->getObjectManager().addObject(worm));
    worm->palaceWorm = true;
    worm->palaceWormCyclesLeft = MILLI2CYCLES(PalaceWorm::kLifetimeMs);
    worm->respondable = true;
    worm->doSetAttackMode(GUARD);
    return worm;
}

void Sandworm::palaceWormLeave(const char* why) {
    SDL_Log("PALACE_WORM: event=%s house=%d object=%u x=%d y=%d kills=%d cycles_left=%d cycle=%u", why,
            owner->getHouseID(), objectID, location.x, location.y, kills, palaceWormCyclesLeft,
            currentGame->getGameCycleCount());
    if(AITelemetry::log().enabled())
        AITelemetry::log().write(currentGame->getGameCycleCount(), owner->getHouseID(), -1, "palace_worm_left",
            AITelemetry::Record().set("worm", objectID).set("why", why).set("kills", kills)
                .set("x", location.x).set("y", location.y));
    palaceWormSpent = std::strcmp(why, "beaten") != 0;
    destroy();
}

void Sandworm::palaceWormBurst() {
    const Coord where = location;
    const auto terrain = currentGameMap->chooseGeneratedSpiceTerrain();
    currentGameMap->createSpiceField(where, PalaceWorm::kBloomRadius, true, terrain.first, terrain.second);
    {
        // What the burst made and how near the base: read by the real-game acceptance test
        // (tests/ai/run-palace-worm-real-game.py) and tests/ai/palace-worm-report.py.
        int spiceTiles = 0;
        for(int dy = -PalaceWorm::kBloomRadius; dy <= PalaceWorm::kBloomRadius; ++dy)
            for(int dx = -PalaceWorm::kBloomRadius; dx <= PalaceWorm::kBloomRadius; ++dx)
                if(currentGameMap->tileExists(where.x + dx, where.y + dy) && currentGameMap->getTile(where.x + dx, where.y + dy)->isSpice())
                    ++spiceTiles;
        int refineryDistance = -1;
        for(const auto* structure : structureList)
            if(structure->getOwner() == owner && structure->getItemID() == Structure_Refinery) {
                const int d = blockDistance(where, structure->getCenterPoint() / TILESIZE).lround();
                if(refineryDistance < 0 || d < refineryDistance) refineryDistance = d;
            }
        SDL_Log("PALACE_WORM: event=spice house=%d object=%u x=%d y=%d spice_tiles=%d refinery_dist=%d cycle=%u",
                owner->getHouseID(), objectID, where.x, where.y, spiceTiles, refineryDistance, currentGame->getGameCycleCount());
    }
    soundPlayer->playSoundAt(Sound_Bloom, where);
    if(owner == pLocalHouse) soundPlayer->playVoice(BloomLocated, owner->getHouseID());
    AITelemetry::log().account(owner->getHouseID(), "palace_worm_bloom", int64_t{1} << 32);
    palaceWormLeave("bloom");
}

namespace {
/// The sand tile nearest AIM (within PalaceWorm::kBloomSnapTiles, nearest first, then lowest y and x)
/// that WORM can stand on and reach: passable to it and in its own sand region.
Coord palaceWormBloomSpot(const Sandworm& worm, const Coord& aim) {
    if(!currentGameMap->tileExists(worm.getLocation())) return Coord::Invalid();
    const Uint32 region = currentGameMap->getTile(worm.getLocation())->getSandRegion();
    Coord best = Coord::Invalid();
    int bestDistance = std::numeric_limits<int>::max();
    for(int dy = -PalaceWorm::kBloomSnapTiles; dy <= PalaceWorm::kBloomSnapTiles; ++dy) {
        for(int dx = -PalaceWorm::kBloomSnapTiles; dx <= PalaceWorm::kBloomSnapTiles; ++dx) {
            const Coord spot(aim.x + dx, aim.y + dy);
            if(!worm.canPass(spot.x, spot.y)
               || currentGameMap->getTile(spot)->getSandRegion() != region) continue;
            const int distance = dx*dx + dy*dy;
            if(distance > PalaceWorm::kBloomSnapTiles*PalaceWorm::kBloomSnapTiles) continue;
            if(distance < bestDistance) {
                bestDistance = distance;
                best = spot;
            }
        }
    }
    return best;
}
} // namespace

bool Sandworm::doSpiceBloom(int x, int y) {
    if(!palaceWorm || !isActive() || !currentGameMap->tileExists(x, y)) return false;
    const Coord spot = palaceWormBloomSpot(*this, Coord(x, y));
    if(spot.isInvalid()) {
        if(owner == pLocalHouse) currentGame->addToNewsTicker(_("The worm cannot reach that spot"));
        return false;
    }
    GroundUnit::doMove2Pos(spot.x, spot.y, true);
    bloomTarget = spot;
    SDL_Log("PALACE_WORM: event=bloom_order house=%d object=%u x=%d y=%d aim_x=%d aim_y=%d cycle=%u",
            owner->getHouseID(), objectID, spot.x, spot.y, x, y, currentGame->getGameCycleCount());
    return true;
}

void Sandworm::handleSpiceBloomClick(int x, int y) {
    if(!palaceWorm || !respondable) return;
    currentGame->getCommandManager().addCommand(
        Command(pLocalPlayer->getPlayerID(), CMD_SANDWORM_SPICEBLOOM, objectID, (Uint32) x, (Uint32) y));
}

bool Sandworm::handleSelectedSpiceBloomClick(int x, int y) {
    Sandworm* responder = nullptr;
    for(Uint32 objectID : currentGame->getSelectedList()) {
        auto* worm = dynamic_cast<Sandworm*>(currentGame->getObjectManager().getObject(objectID));
        if(worm != nullptr && worm->isPalaceWorm() && currentGame->canControlHouse(worm->getOwner())) {
            worm->handleSpiceBloomClick(x, y);
            responder = worm;
        }
    }
    currentGame->setCursorMode(Game::CursorMode_Normal);
    if(responder != nullptr) responder->playConfirmSound();
    return responder != nullptr;
}

/// Clears the Spice Bloom order for a new ORDER, logging it when there was one (tests/ai/palace-worm-report.py).
void Sandworm::noteBloomCancelled(const char* order) {
    if(bloomTarget.isValid())
        SDL_Log("PALACE_WORM: event=bloom_cancelled house=%d object=%u x=%d y=%d aim_x=%d aim_y=%d order=%s cycle=%u",
                owner->getHouseID(), objectID, location.x, location.y, bloomTarget.x, bloomTarget.y, order,
                currentGame->getGameCycleCount());
    bloomTarget.invalidate();
}

// An order replaces a Spice Bloom trip, except the engine's own turn on whoever shot the worm
// (UnitBase::handleDamage in Hunt): a worm on its way to burst keeps going. Before 2026-11-27 that
// turn cancelled the trip each time the worm was hit, and the AI gave the order again every pass
// (self-play: 22.7 bloom orders for 7.0 bursts per Fremen house-game).
void Sandworm::doMove2Pos(int xPos, int yPos, bool bForced) {
    if(takingDamage && bloomTarget.isValid()) return;
    noteBloomCancelled("move");
    GroundUnit::doMove2Pos(xPos, yPos, bForced);
}

void Sandworm::doAttackObject(const ObjectBase* pTargetObject, bool bForced) {
    if(takingDamage && bloomTarget.isValid()) return;
    noteBloomCancelled("attack_object");
    GroundUnit::doAttackObject(pTargetObject, bForced);
}

void Sandworm::doAttackPos(int xPos, int yPos, bool bForced) {
    if(takingDamage && bloomTarget.isValid()) return;
    noteBloomCancelled("attack_position");
    GroundUnit::doAttackPos(xPos, yPos, bForced);
}

bool Sandworm::spares(const ObjectBase& object) const {
    if(summonerHouseID == HOUSE_INVALID || object.getOwner() == nullptr) return false;
    const House* summoner = currentGame->getHouse(summonerHouseID);
    return summoner != nullptr && object.getOwner()->getTeamID() == summoner->getTeamID();
}

void Sandworm::assignToMap(const Coord& pos) {
    if(currentGameMap->tileExists(pos)) {
        currentGameMap->getTile(pos)->assignUndergroundUnit(getObjectID());
        if((itemID == Unit_TameSandworm && ModManager::instance().isTornieFamilyContentActive()) || palaceWorm)
            currentGameMap->viewMap(owner->getHouseID(), pos, getViewRange());
        // do not unhide map cause this would give Fremen players an advantage
        // currentGameMap->viewMap(owner->getHouseID(), location, getViewRange());
    }
}

bool Sandworm::attack() {
    if(primaryWeaponTimer == 0) {
        if(target) {
            // Check if target is an immortal human-controlled unit
            ObjectBase* pTarget = target.getObjPointer();
            if(pTarget) {
                GameType gameType = currentGame->getGameInitSettings().getGameType();
                bool targetIsImmortal = (!isNetworkGameType(gameType)
                                        && gameType != GameType::LoadMultiplayer
                                        && currentGame->getGameInitSettings().getGameOptions().immortalHumanPlayer
                                        && pTarget->getOwner() == pLocalHouse);
                
                if(targetIsImmortal) {
                    // Sandworm dies trying to eat an immortal unit (chokes on it)
                    // Skip attack animation and immediately trigger sleep/die
                    kills = palaceWorm ? PalaceWorm::kMeals : 3; // Set to max so it triggers death/sleep
                    sleepOrDie();
                    return false;
                }
            }
            
            soundPlayer->playSoundAt(Sound_WormAttack, location);
            drawnFrame = 0;
            attackFrameTimer = SANDWORM_ATTACKFRAMETIME;
            primaryWeaponTimer = getWeaponReloadTime();
            // A Palace worm searches again when the meal is over: a search while eating finds
            // nothing (findTarget), and UnitBase::targeting then drops a hunter with no target
            // back to Guard, where it sees two tiles and never finds the next meal.
            if(palaceWorm) findTargetTimer = SANDWORM_ATTACKFRAMETIME * 9 + 1;
            return true;
        }
    }
    return false;
}

void Sandworm::deploy(const Coord& newLocation) {
    UnitBase::deploy(newLocation);
    if(itemID == Unit_Sandworm && respawnOrigin.isInvalid()
       && currentGame->getGameInitSettings().isCivilizationWars()
       && currentGameMap->tileExists(newLocation)) {
        respawnOrigin = newLocation;
    }

    respondable = itemID == Unit_TameSandworm || palaceWorm;
    if(palaceWorm && currentGameMap->tileExists(newLocation)) {
        // Shimmer from the start, so its owner can find and select a worm that has not moved yet.
        lastLocs[0] = lastLocs[1] = newLocation*TILESIZE + Coord(TILESIZE/2, TILESIZE/2);
        shimmerOffsetIndex = 0;
    }
}

void Sandworm::blitToScreen() {
    static const int shimmerOffset[]  = { 1, 3, 2, 5, 4, 3, 2, 1 };

    if(shimmerOffsetIndex >= 0) {
        // render sandworm's shimmer

        SDL_Texture* shimmerTex = pGFXManager->getZoomedObjPic(ObjPic_SandwormShimmerTemp, currentZoomlevel);
        SDL_Texture* shimmerMaskTex = pGFXManager->getZoomedObjPic(ObjPic_SandwormShimmerMask, currentZoomlevel);

        for(int i = 0; i < SANDWORM_SEGMENTS; i++) {
            if(lastLocs[i].isInvalid()) {
                continue;
            }

            SDL_Rect dest = calcDrawingRect(shimmerMaskTex, screenborder->world2screenX(lastLocs[i].x), screenborder->world2screenY(lastLocs[i].y), HAlign::Center, VAlign::Center);

            // switch to texture 'shimmerTex' for rendering; the shimmer samples the
            // target the map is being drawn into (the offscreen zoom board between sprite levels)
            SDL_Texture* oldRenderTarget = SDL_GetRenderTarget(renderer);
            SDL_Texture* const sceneTex = oldRenderTarget != nullptr ? oldRenderTarget : screenTexture;
            SDL_SetRenderTarget(renderer, shimmerTex);

            // copy complete mask
            // contains solid black (0,0,0,255) for pixels to take from screen
            // and transparent (0,0,0,0) for pixels that should not be copied over
            SDL_SetTextureBlendMode(shimmerMaskTex, SDL_BLENDMODE_NONE);
            SDL_RenderCopy(renderer, shimmerMaskTex, nullptr, nullptr);
            SDL_SetTextureBlendMode(shimmerMaskTex, SDL_BLENDMODE_BLEND);

            // now copy r,g,b colors from screen but don't change alpha values in mask
            SDL_SetTextureBlendMode(sceneTex, SDL_BLENDMODE_ADD);
            SDL_Rect source = dest;
            source.x += shimmerOffset[(shimmerOffsetIndex+i)%8]*2;
            SDL_RenderCopy(renderer, sceneTex, &source, nullptr);
            SDL_SetTextureBlendMode(sceneTex, SDL_BLENDMODE_NONE);

            // switch back to old rendering target (from texture 'shimmerTex')
            SDL_SetRenderTarget(renderer, oldRenderTarget);

            // now blend shimmerTex to screen (= make use of alpha values in mask)
            SDL_SetTextureBlendMode(shimmerTex, SDL_BLENDMODE_BLEND);
            SDL_RenderCopy(renderer, shimmerTex, nullptr, &dest);

        }
    }

    if(drawnFrame != INVALID || itemID == Unit_TameSandworm) {
        SDL_Rect dest = calcSpriteDrawingRect(  graphic[currentZoomlevel],
                                                screenborder->world2screenX(realX),
                                                screenborder->world2screenY(realY),
                                                numImagesX, numImagesY,
                                                HAlign::Center, VAlign::Center);
        SDL_Rect source = calcSpriteSourceRect(graphic[currentZoomlevel], 0, numImagesX, drawnFrame == INVALID ? 5 : drawnFrame, numImagesY);
        SDL_RenderCopy(renderer, graphic[currentZoomlevel], &source, &dest);
    }
}

void Sandworm::checkPos() {
    if(justStoppedMoving) {
        realX = location.x*TILESIZE + TILESIZE/2;
        realY = location.y*TILESIZE + TILESIZE/2;

        if(currentGameMap->tileExists(location)) {
            Tile* pTile = currentGameMap->getTile(location);
            if(pTile->hasInfantry() && (pTile->getInfantry()->getOwner() == pLocalHouse)) {
                soundPlayer->playVoice(SomethingUnderTheSand, pTile->getInfantry()->getOwner()->getHouseID());
            }
        }
    }
}

void Sandworm::engageTarget() {
    if(isEating()) {
        return;
    }
    if(bloomTarget.isValid()) {
        // On its way to burst: nothing else matters.
        if(target) setTarget(nullptr);
        return;
    }

    UnitBase::engageTarget();

    if(target) {
        FixPoint maxDistance;

        if(forced) {
            maxDistance = FixPt_MAX;
        } else {
            switch(attackMode) {
                case GUARD:
                case AMBUSH: {
                    // In GUARD and AMBUSH modes, stay near guard point (view range)
                    maxDistance = getViewRange();
                } break;
                
                case HUNT: {
                    // In HUNT mode, pursue more aggressively but still limited to view range
                    // This allows counter-attacking units that damaged us. A Palace worm hunts
                    // its whole sand region (canAttack keeps it there).
                    maxDistance = palaceWorm ? FixPt_MAX : getViewRange() * 2;
                } break;

                case AREAGUARD: {
                    maxDistance = FixPt_MAX;
                } break;

                case STOP:
                case CAPTURE:
                default: {
                    maxDistance = 0;
                } break;
            }
        }

        if(targetDistance > maxDistance) {
            // give up and switch to AMBUSH if in HUNT mode
            if(attackMode == HUNT) {
                doSetAttackMode(AMBUSH);
            }
            setDestination(guardPoint);
            setTarget(nullptr);
        }
    }
}

void Sandworm::setLocation(int xPos, int yPos) {
    if(currentGameMap->tileExists(xPos, yPos) || ((xPos == INVALID_POS) && (yPos == INVALID_POS))) {
        UnitBase::setLocation(xPos, yPos);
    }
}

/**
    Put sandworm to sleep for a while
*/
void Sandworm::sleep() {
    sleepTimer = currentGame->randomGen.rand(MIN_SANDWORMSLEEPTIME, MAX_SANDWORMSLEEPTIME);
    setActive(false);
    setVisible(VIS_ALL, false);
    setForced(false);
    currentGameMap->removeObjectFromMap(getObjectID()); //no map point will reference now
    setLocation(INVALID_POS, INVALID_POS);
    setHealth(getMaxHealth());
    kills = 0;
    warningWormSignPlayedFlags = 0;
    drawnFrame = INVALID;
    attackFrameTimer = 0;
    shimmerOffsetIndex = -1;
    for(int i = 0; i < SANDWORM_SEGMENTS; i++) {
        lastLocs[i].invalidate();
    }
}

bool Sandworm::sleepOrDie() {
    if(itemID == Unit_TameSandworm && ModManager::instance().isTornieFamilyContentActive()) {
        destroy();
        return false;
    }

    // Make sand worms always drop spice, even if they don't die
    if(currentGame->getGameInitSettings().getGameOptions().killedSandwormsDropSpice) {
        const auto generatedSpiceTerrain = currentGameMap->chooseGeneratedSpiceTerrain();
        currentGameMap->createSpiceField(location, 4, false, generatedSpiceTerrain.first, generatedSpiceTerrain.second);
    }

    if(currentGame->getGameInitSettings().getGameOptions().sandwormsRespawn) {
        sleep();
        return true;
    } else {
        destroy();
        return false;
    }
}

void Sandworm::setTarget(const ObjectBase* newTarget) {
    GroundUnit::setTarget(newTarget);

    // Preplaced worms load before the spectator view has a local house. Keep
    // their target state, but defer the local warning until that view exists.
    if(newTarget == nullptr || pLocalHouse == nullptr) return;

    const int localHouseID = pLocalHouse->getHouseID();
    const Uint16 localHouseMask = warningWormSignMaskForHouse(localHouseID);
    if( (newTarget != nullptr) && (newTarget->getOwner() == pLocalHouse)
        && (localHouseMask != 0)
        && ((warningWormSignPlayedFlags & localHouseMask) == 0) ) {
        soundPlayer->playVoice(WarningWormSign, localHouseID);
        warningWormSignPlayedFlags |= localHouseMask;
    }
}

void Sandworm::handleDamage(int damage, Uint32 damagerID, House* damagerOwner,
                               const DeviationReward::Provenance& provenance) {
    if(palaceWorm) {
        // Its owner's orders stand (no switch to Hunt, no forced chase); a worm in Guard or Hunt
        // that is not on its way to burst turns on an enemy that shot it from the sand.
        takingDamage = true;
        GroundUnit::handleDamage(damage, damagerID, damagerOwner, provenance);
        takingDamage = false;
        if(damage > 0 && bloomTarget.isInvalid() && !target && (attackMode == GUARD || attackMode == HUNT
           || attackMode == AREAGUARD || attackMode == AMBUSH)) {
            const ObjectBase* pDamager = currentGame->getObjectManager().getObject(damagerID);
            if(pDamager != nullptr && canAttack(pDamager)) setTarget(pDamager);
        }
        return;
    }
    // Switch to HUNT mode BEFORE calling parent so counter-attack logic works
    if(damage > 0) {
        doSetAttackMode(HUNT);
    }
    
    // Then call parent to handle counter-attack logic (now that we're in HUNT mode)
    GroundUnit::handleDamage(damage, damagerID, damagerOwner, provenance);
    
    // Additionally, if we were damaged, directly attack the damager's location if they're on sand
    if(damage > 0) {
        ObjectBase* pDamager = currentGame->getObjectManager().getObject(damagerID);
        if(pDamager != nullptr && pDamager->isAUnit()) {
            const Coord damagerLoc = pDamager->getLocation();
            if(currentGameMap->tileExists(damagerLoc)) {
                const Tile* pTile = currentGameMap->getTile(damagerLoc);
                // Only pursue if damager is on sand (sandworms can't go on rock)
                if(!pTile->isMountain() && !pTile->isRock()) {
                    // Force attack the damager to ensure we pursue even if out of search range
                    doAttackObject(pDamager, true);
                }
            }
        }
    }
}

bool Sandworm::update() {
    // update() runs once per game cycle of a running game, so this counts game time.
    if(palaceWorm && isActive()) {
        if(getHealth() <= getMaxHealth()/2) {
            AchievementEvents::wormDefeated(this);
            palaceWormLeave("beaten");
            return false;
        }
        if(palaceWormCyclesLeft > 0) --palaceWormCyclesLeft;
        if(!isEating()) {
            // At the spot, or stopped beside it (another worm took the tile), or stopped for good
            // short of it (the path search gave up: UnitBase::navigate set the destination to
            // where it stands): burst here rather than wait out its life.
            if(bloomTarget.isValid() && (location == bloomTarget
               || (!moving && !justStoppedMoving && (blockDistance(location, bloomTarget) <= 2 || destination == location)))) {
                palaceWormBurst();
                return false;
            }
            if(palaceWormCyclesLeft <= 0) {
                // Its time is up on the way to burst: it bursts where it is, not for nothing.
                if(bloomTarget.isValid()) palaceWormBurst();
                else palaceWormLeave("expired");
                return false;
            }
        }
    }

    if(itemID != Unit_TameSandworm && !palaceWorm && getHealth() <= getMaxHealth()/2) {
        AchievementEvents::wormDefeated(this);
        if(sleepOrDie() == false) {
            return false;
        }
    } else {
        if(GroundUnit::update() == false) {
            return false;
        }

        if(isActive() && (moving || justStoppedMoving) && !currentGame->isGamePaused() && !currentGame->isGameFinished()) {
            Coord realLocation = getLocation()*TILESIZE + Coord(TILESIZE/2, TILESIZE/2);
            if(lastLocs[1] != realLocation) {
                for(int i = (SANDWORM_SEGMENTS-1); i > 0 ; i--) {
                    lastLocs[i] = lastLocs[i-1];
                }
                lastLocs[1] = realLocation;
            }
            lastLocs[0].x = lround(realX);
            lastLocs[0].y = lround(realY);
            shimmerOffsetIndex = ((currentGame->getGameCycleCount() + getObjectID()) % 48)/6;
        }

        if(attackFrameTimer > 0) {
            attackFrameTimer--;

            //death frame has started
            if(attackFrameTimer == 0) {
                drawnFrame++;
                if(drawnFrame >= 9) {
                    drawnFrame = INVALID;
                    if(palaceWorm) {
                        if(kills >= PalaceWorm::kMeals) {
                            palaceWormLeave("sated");
                            return false;
                        }
                    } else if(itemID != Unit_TameSandworm && kills >= 3) {
                        if(sleepOrDie() == false) {
                            return false;
                        }
                    }
                } else {
                    attackFrameTimer = SANDWORM_ATTACKFRAMETIME;
                    if(drawnFrame == 1) {
                        // the close mouth bit of graphic is currently shown => eat unit
                        if(target && target.getObjPointer() != nullptr) {
                            bool wasAlive = target.getObjPointer()->isVisible(getOwner()->getTeamID());  //see if unit was alive before attack
                            Coord realPos = Coord(lround(realX), lround(realY));
                            currentGameMap->damage(objectID, getOwner(), realPos, Bullet_Sandworm, 5000, NONE_ID, false);

                            if(wasAlive && target && (target.getObjPointer()->isVisible(getOwner()->getTeamID()) == false)) {
                                kills++;
                                const ObjectBase* meal = target.getObjPointer();
                                if(palaceWorm && meal != nullptr) {
                                    SDL_Log("PALACE_WORM: event=ate house=%d object=%u x=%d y=%d kills=%d cycles_left=%d item=%d price=%d victim_house=%d cycle=%u",
                                            owner->getHouseID(), objectID, location.x, location.y, kills, palaceWormCyclesLeft,
                                            meal->getItemID(), currentGame->objectData.data[meal->getItemID()][meal->getOwner()->getHouseID() < NUM_HOUSES ? meal->getOwner()->getHouseID() : 0].price,
                                            meal->getOwner()->getHouseID(), currentGame->getGameCycleCount());
                                }
                            }
                        }
                    }
                }
            }
        }

        if(sleepTimer > 0) {
            sleepTimer--;

            if(sleepTimer == 0) {
                // awaken the worm!

                if(itemID == Unit_Sandworm && currentGame->getGameInitSettings().isCivilizationWars()) {
                    if(canPass(respawnOrigin.x, respawnOrigin.y)
                       && !currentGameMap->getTile(respawnOrigin)->hasAGroundObject()) {
                        deploy(respawnOrigin);
                    } else {
                        // Each worm owns this slot permanently, even while it is blocked.
                        sleep();
                    }
                } else {
                    for(int tries = 0 ; tries < 1000 ; tries++) {
                        int x = currentGame->randomGen.rand(0, currentGameMap->getSizeX() - 1);
                        int y = currentGame->randomGen.rand(0, currentGameMap->getSizeY() - 1);

                        if(canPass(x, y)) {
                            deploy(currentGameMap->getTile(x, y)->getLocation());
                            break;
                        }
                    }

                    if(isActive() == false) {
                        // no room for sandworm on map => take another nap
                        if(sleepOrDie() == false) {
                            return false;
                        }
                    }
                }
            }
        }
    }

    return true;
}

bool Sandworm::canAttack(const ObjectBase* object) const {
    if((object != nullptr)
        // Sleeping/withdrawn worms can still be referenced by AI threat checks but
        // have no map tile. Check our own position before comparing sand regions.
        && currentGameMap->tileExists(location)
        && object->isAGroundUnit()
        && !isSandwormUnit(object->getItemID())      // won't kill other sandworms
        && ((itemID != Unit_TameSandworm && !palaceWorm) || object->getOwner()->getTeamID() != owner->getTeamID())
        && !spares(*object)                         // nor the side whose reinforcement it is
        //&& object->isVisible(getOwner()->getTeamID())
        //&& (object->getOwner()->getTeamID() != owner->getTeamID())
        && currentGameMap->tileExists(object->getLocation())
        && canPass(object->getLocation().x, object->getLocation().y)
        && (currentGameMap->getTile(object->getLocation())->getSandRegion() == currentGameMap->getTile(location)->getSandRegion())) {
        return true;
    } else {
        return false;
    }
}

bool Sandworm::canPass(int xPos, int yPos) const {
    return (currentGameMap->tileExists(xPos, yPos)
            && !currentGameMap->getTile(xPos, yPos)->isRock()
            && (!currentGameMap->getTile(xPos, yPos)->hasAnUndergroundUnit()
                || (currentGameMap->getTile(xPos, yPos)->getUndergroundUnit() == this)));
}

const ObjectBase* Sandworm::findTarget() const {
    if(isEating() || bloomTarget.isValid()) {
        return nullptr;
    }

    if(!currentGameMap->tileExists(location)) {
        return nullptr;
    }

    const auto maxDistanceTiles = [&]() -> int {
        if(forced) {
            return std::numeric_limits<int>::max();
        }

        switch(attackMode) {
            case GUARD:
            case AMBUSH:
                // In GUARD and AMBUSH modes, stay near guard point (view range)
                return getViewRange();
            case HUNT:
                // In HUNT mode (when damaged), search further to counter-attack. A Palace
                // worm's Hunt order seeks the whole sand region it is in.
                return palaceWorm ? std::numeric_limits<int>::max() : getViewRange() * 2;
            case AREAGUARD:
                return std::numeric_limits<int>::max();
            default:
                return 0;
        }
    }();

    const auto calculatePriority = [&](const UnitBase* pUnit, int distance) {
        int basePriority = 0;

        if(pUnit->isInfantry()) {
            basePriority = 0x64;
        } else if(pUnit->getItemID() == Unit_Harvester || pUnit->isTracked()) {
            basePriority = 0x3E8;
        } else if(pUnit->isAGroundUnit()) {
            // Treat remaining ground units (trikes/quads etc.) as wheeled targets
            basePriority = 0x1388;
        } else {
            return 0;
        }

        if(pUnit->isMoving() || pUnit->hasATarget()) {
            basePriority *= 4;
        }

        if(distance <= 0) {
            distance = 1;
        }

        basePriority = (basePriority / distance);
        if(distance < 2) {
            basePriority *= 2;
        }

        return basePriority;
    };

    const UnitBase* bestTarget = nullptr;
    int bestPriority = 0;

    for(UnitBase* pUnit : unitList) {
        if(pUnit == nullptr || pUnit == this) {
            continue;
        }
        if(!canAttack(pUnit)) {
            continue;
        }
        // NOTE: Fog-of-war check removed - sandworms should detect all valid targets
        // in range regardless of player visibility. Fog-of-war only affects UI/player
        // knowledge, not unit behavior. Including it breaks multiplayer determinism
        // (each client would filter differently) and prevents worms from attacking
        // unscouted enemies in single-player.

        const FixPoint fpDistance = blockDistance(location, pUnit->getLocation());
        if(fpDistance > maxDistanceTiles) {
            continue;
        }

        const int distance = fpDistance.lround();
        const int priority = calculatePriority(pUnit, distance);
        if(priority > bestPriority) {
            bestPriority = priority;
            bestTarget = pUnit;
        }
    }

    if(bestTarget != nullptr) {
        return bestTarget;
    }

    if((attackMode == HUNT) || (attackMode == AREAGUARD)) {
        return nullptr;
    }

    return ObjectBase::findTarget();
}

int Sandworm::getCurrentAttackAngle() const {
    // we can always attack an target
    return targetAngle;
}

void Sandworm::playAttackSound() {
    soundPlayer->playSoundAt(Sound_WormAttack,location);
}
