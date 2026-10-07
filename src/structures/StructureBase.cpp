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

#include <structures/StructureBase.h>

#include <globals.h>

#include <House.h>
#include <Game.h>
#include <Map.h>
#include <ScreenBorder.h>
#include <Explosion.h>
#include <SoundPlayer.h>

#include <players/HumanPlayer.h>

#include <misc/draw_util.h>

#include <units/UnitBase.h>

#include <GUI/ObjectInterfaces/DefaultStructureInterface.h>
#include <GUI/ObjectInterfaces/CityStatsStructureInterface.h>

#include <dunecity/StructureDegradation.h>

#include <algorithm>
#include <set>
#include <tuple>

namespace {
// SAVEGAMEVERSION 9847 splits the single legacy degrade timer into the
// independent power-damage and foundation-decay state. Older saves carry one
// Sint32 timer whose -1 sentinel cannot identify the original foundation, so
// they restore without foundation decay (see StructureDegradation.h).
constexpr Uint32 kDegradationSaveVersion = 9847;
static_assert(SAVEGAMEVERSION >= kDegradationSaveVersion,
              "StructureBase writes the split degradation state; SAVEGAMEVERSION must cover it");

bool savegameHasDegradationState() {
    return currentGame == nullptr || currentGame->getLoadedSavegameVersion() >= kDegradationSaveVersion;
}

bool isTornieStructureForDiagnostics(int itemID) {
    return itemID == Structure_AdvancedWindTrap
        || itemID == Structure_AdvancedWindTrapMK2
        || itemID == Structure_AdvancedWindTrapMK3
        || itemID == Structure_Worfinery
        || itemID == Structure_TechCenter
        || itemID == Structure_Scoutpost
        || itemID == Structure_Flamepost
        || itemID == Structure_Chemipost
        || itemID == Structure_LoveFactory
        || itemID == Structure_ChaosFactory;
}

const char* getTornieStructureDiagnosticName(int itemID) {
    switch(itemID) {
        case Structure_AdvancedWindTrap:    return "AdvancedWindTrap3x3";
        case Structure_AdvancedWindTrapMK2: return "AdvancedWindTrap2x3";
        case Structure_AdvancedWindTrapMK3: return "AdvancedWindTrap3x2";
        case Structure_Worfinery:           return "Worfinery";
        case Structure_TechCenter:          return "TechCenter";
        case Structure_Scoutpost:           return "Scoutpost";
        case Structure_Flamepost:           return "Flamepost";
        case Structure_Chemipost:           return "Chemipost";
        case Structure_LoveFactory:         return "LoveFactory";
        case Structure_ChaosFactory:        return "ChaosFactory";
        default:                            return "Unknown";
    }
}
}

StructureBase::StructureBase(House* newOwner) : ObjectBase(newOwner) {
    StructureBase::init();

    repairing = false;
    fogged = false;
    powerDamageTimer = DuneCity::Degradation::powerDamageIntervalCycles();
    foundationDecayTimer = DuneCity::Degradation::foundationDecayIntervalCycles();
    foundationDegrades = false;
}

StructureBase::StructureBase(InputStream& stream): ObjectBase(stream) {
    StructureBase::init();

    repairing = stream.readBool();
    fogged = stream.readBool();
    lastVisibleFrame = stream.readUint32();

    if(savegameHasDegradationState()) {
        powerDamageTimer = stream.readSint32();
        foundationDecayTimer = stream.readSint32();
        foundationDegrades = stream.readBool();
    } else {
        const int legacyDegradeTimer = stream.readSint32();
        powerDamageTimer = DuneCity::Degradation::legacyPowerDamageTimer(legacyDegradeTimer);
        foundationDecayTimer = DuneCity::Degradation::foundationDecayIntervalCycles();
        foundationDegrades = DuneCity::Degradation::legacyFoundationDegrades(legacyDegradeTimer);
    }

    size_t numSmoke = stream.readUint32();
    for(size_t i=0;i<numSmoke; i++) {
        smoke.emplace_back(stream);
    }

    cityOccupancy_ = stream.readUint8();
}

void StructureBase::init() {
    aStructure = true;

    structureSize.x = 0;
    structureSize.y = 0;

    justPlacedTimer = 0;

    lastVisibleFrame = firstAnimFrame = lastAnimFrame = curAnimFrame = 2;
    animationCounter = 0;

    structureList.push_back(this);
}

StructureBase::~StructureBase() {
    try {
        currentGameMap->removeObjectFromMap(getObjectID()); //no map point will reference now
        if(currentGameMap != nullptr) {
            currentGameMap->incrementPathingRevision();
        }
        currentGame->getObjectManager().removeObject(getObjectID());
        structureList.remove(this);
        owner->decrementStructures(itemID, location, !demolishedByOwner_);

        removeFromSelectionLists();

        for(int i=0; i < NUMSELECTEDLISTS; i++) {
            pLocalPlayer->getGroupList(i).erase(getObjectID());
        }
    } catch(std::exception& e) {
        SDL_Log("StructureBase::~StructureBase(): %s", e.what());
    }
}

void StructureBase::save(OutputStream& stream) const {
    ObjectBase::save(stream);

    stream.writeBool(repairing);
    stream.writeBool(fogged);
    stream.writeUint32(lastVisibleFrame);

    stream.writeSint32(powerDamageTimer);
    stream.writeSint32(foundationDecayTimer);
    stream.writeBool(foundationDegrades);

    stream.writeUint32(smoke.size());
    for(const StructureSmoke& structureSmoke : smoke) {
        structureSmoke.save(stream);
    }

    stream.writeUint8(cityOccupancy_);
}

void StructureBase::assignToMap(const Coord& pos) {
    bool bFoundNonConcreteTile = false;

    Coord temp;
    for(int i = pos.x; i < pos.x + structureSize.x; i++) {
        for(int j = pos.y; j < pos.y + structureSize.y; j++) {
            if(currentGameMap->tileExists(i, j)) {
                Tile* pTile = currentGameMap->getTile(i,j);
                pTile->assignNonInfantryGroundObject(getObjectID());
                const bool preparedFoundation = pTile->hasPreparedFoundation();
                // Clear road flag when a structure is placed on a road tile,
                // so the tile is no longer rendered/treated as a road.
                if(pTile->isRoad()) {
                    pTile->setRoad(false);
                }
                if(!preparedFoundation && currentGame->getGameInitSettings().getGameOptions().concreteRequired && (currentGame->gameState != GameState::Start)) {
                    bFoundNonConcreteTile = true;

                    if((itemID != Structure_Wall) && (itemID != Structure_ConstructionYard)) {
                        setHealth(getHealth() - FixPoint(getMaxHealth())/(2*structureSize.x*structureSize.y));
                    }
                }
                pTile->setType(Terrain_Rock);
                pTile->setOwner(getOwner()->getHouseID());

                setVisible(VIS_ALL, true);
                setActive(true);
                setRespondable(true);
            }
        }
    }

    currentGameMap->viewMap(getOwner()->getHouseID(), pos, getViewRange());
    currentGameMap->incrementPathingRevision();

    // Dynasty parity: only a structure placed on an incomplete foundation decays
    // from it. A full prepared foundation — and any game where concrete is not
    // required at all — suppresses foundation decay, never power damage.
    foundationDegrades = bFoundNonConcreteTile;
}

void StructureBase::blitToScreen() {
    int index = fogged ? lastVisibleFrame : curAnimFrame;
    int indexX = index % numImagesX;
    int indexY = index / numImagesX;

    // Loading a map or switching mods invalidates GFXManager's texture cache.
    // Structures keep raw texture pointers, so refresh them before every draw.
    if(owner != nullptr) {
        graphic = pGFXManager->getObjPic(graphicID, owner->getHouseID());
    }

    SDL_Texture* structureTexture = graphic[currentZoomlevel];
    if(structureTexture == nullptr) {
        if(isTornieStructureForDiagnostics(itemID)) {
            SDL_Log("TornieGFX: draw %s object=%u house=%d frame=%d zoom=%d texture=null",
                    getTornieStructureDiagnosticName(itemID),
                    objectID,
                    owner ? owner->getHouseID() : -1,
                    index,
                    currentZoomlevel);
        }
        return;
    }

    SDL_Rect dest = calcSpriteDrawingRect(  structureTexture,
                                            screenborder->world2screenX(lround(realX)),
                                            screenborder->world2screenY(lround(realY)),
                                            numImagesX, numImagesY);
    SDL_Rect source = calcSpriteSourceRect(structureTexture,indexX,numImagesX,indexY,numImagesY);

    if(isTornieStructureForDiagnostics(itemID)) {
        static std::set<std::tuple<int, int, int, int, int>> loggedDrawStates;
        const int ownerHouse = owner ? owner->getHouseID() : -1;
        const auto key = std::make_tuple(itemID, ownerHouse, currentZoomlevel, numImagesX, index);
        if(loggedDrawStates.insert(key).second) {
            int textureWidth = 0;
            int textureHeight = 0;
            Uint32 textureFormat = 0;
            int textureAccess = 0;
            SDL_BlendMode textureBlend = SDL_BLENDMODE_NONE;
            Uint8 textureAlpha = SDL_ALPHA_OPAQUE;
            SDL_QueryTexture(structureTexture, &textureFormat, &textureAccess, &textureWidth, &textureHeight);
            SDL_GetTextureBlendMode(structureTexture, &textureBlend);
            SDL_GetTextureAlphaMod(structureTexture, &textureAlpha);
            const bool sourceOutOfBounds =
                source.x < 0 || source.y < 0 || source.w <= 0 || source.h <= 0
                || source.x + source.w > textureWidth
                || source.y + source.h > textureHeight;
            SDL_Log("TornieGFX: draw %s object=%u house=%d frame=%d index=(%d,%d) visibleFrame=%d fogged=%d zoom=%d numImages=%dx%d texture=%dx%d format=%u access=%d blend=%d alpha=%u source=(%d,%d,%d,%d) dest=(%d,%d,%d,%d) map=(%d,%d) sourceOutOfBounds=%d",
                    getTornieStructureDiagnosticName(itemID),
                    objectID,
                    ownerHouse,
                    index,
                    indexX,
                    indexY,
                    lastVisibleFrame,
                    fogged ? 1 : 0,
                    currentZoomlevel,
                    numImagesX,
                    numImagesY,
                    textureWidth,
                    textureHeight,
                    textureFormat,
                    textureAccess,
                    static_cast<int>(textureBlend),
                    static_cast<unsigned int>(textureAlpha),
                    source.x,
                    source.y,
                    source.w,
                    source.h,
                    dest.x,
                    dest.y,
                    dest.w,
                    dest.h,
                    location.x,
                    location.y,
                    sourceOutOfBounds ? 1 : 0);
        }
    }

    const Uint8 dune2rBlend = pGFXManager->getDune2RVisualBlend();
    bool classicDrawn = false;
    if(dune2rBlend < SDL_ALPHA_OPAQUE || fogged) {
        SDL_RenderCopy(renderer, structureTexture, &source, &dest);
        classicDrawn = true;
    }

    bool enhancedDrawn = false;
    if(!fogged && owner != nullptr) {
        // DuneCity Compact frames may carry more source pixels than the
        // native SimCity atlas cell. Draw that source directly into the
        // already-calculated classic destination so visual detail can rise
        // without changing placement, collision, footprint, or animation
        // state selection.
        enhancedDrawn = pGFXManager->drawDuneCityBuilding(
            itemID, owner->getHouseID(), index, dest);
    }
    if(!enhancedDrawn && !fogged && dune2rBlend > 0 && owner != nullptr) {
        const Uint32 nowMs = currentGame->getGameTime();
        auto visualState = GFXManager::EnhancedBuildingState::Idle;
        Uint32 elapsedMs = nowMs + getObjectID() * 97u;
        bool transitionTiming = false;

        if(enhancedPlacementStartMs != std::numeric_limits<Uint32>::max()) {
            const Uint32 placementDuration = pGFXManager->getEnhancedBuildingAnimationDuration(
                itemID, owner->getHouseID(), GFXManager::EnhancedBuildingState::Placement);
            const Uint32 constructionDuration = pGFXManager->getEnhancedBuildingAnimationDuration(
                itemID, owner->getHouseID(), GFXManager::EnhancedBuildingState::Construction);
            const Uint32 placementElapsed = nowMs - enhancedPlacementStartMs;
            if(placementDuration > 0 && placementElapsed < placementDuration) {
                visualState = GFXManager::EnhancedBuildingState::Placement;
                elapsedMs = placementElapsed;
                transitionTiming = true;
            } else if(constructionDuration > 0
                      && placementElapsed < placementDuration + constructionDuration) {
                visualState = GFXManager::EnhancedBuildingState::Construction;
                elapsedMs = placementElapsed - placementDuration;
                transitionTiming = true;
            } else {
                enhancedPlacementStartMs = std::numeric_limits<Uint32>::max();
            }
        }

        if(!transitionTiming) {
            if(repairing && getHealth() < getMaxHealth()) {
                visualState = GFXManager::EnhancedBuildingState::Repair;
                transitionTiming = true;
            } else if(isBadlyDamaged()) {
                visualState = GFXManager::EnhancedBuildingState::Damaged;
                transitionTiming = true;
            } else if(itemID == Structure_Refinery && curAnimFrame >= 8) {
                visualState = GFXManager::EnhancedBuildingState::Working;
            }

            const int stateIndex = static_cast<int>(visualState);
            if(stateIndex != enhancedVisualState) {
                enhancedVisualState = stateIndex;
                enhancedVisualStateStartMs = nowMs;
            }
            if(transitionTiming) {
                elapsedMs = nowMs - enhancedVisualStateStartMs;
            }
        } else {
            enhancedVisualState = static_cast<int>(visualState);
        }

        const int anchorX = screenborder->world2screenX(
            lround(realX) + structureSize.x * TILESIZE / 2);
        const int anchorY = screenborder->world2screenY(
            lround(realY) + structureSize.y * TILESIZE);
        enhancedDrawn = pGFXManager->drawEnhancedBuilding(
            itemID, owner->getHouseID(), currentZoomlevel,
            visualState, elapsedMs, anchorX, anchorY);
    }

    if(!classicDrawn && !enhancedDrawn) {
        SDL_RenderCopy(renderer, structureTexture, &source, &dest);
        classicDrawn = true;
    }

    if(!fogged && (dune2rBlend < SDL_ALPHA_OPAQUE || !enhancedDrawn)) {
        SDL_Texture* pSmokeTex = pGFXManager->getZoomedObjPic(ObjPic_Smoke, getOwner()->getHouseID(), currentZoomlevel);
        SDL_Rect smokeSource = calcSpriteSourceRect(pSmokeTex, 0, 3);
        for(const StructureSmoke& structureSmoke : smoke) {
            SDL_Rect smokeDest = calcSpriteDrawingRect( pSmokeTex,
                                                        screenborder->world2screenX(structureSmoke.realPos.x),
                                                        screenborder->world2screenY(structureSmoke.realPos.y),
                                                        3, 1, HAlign::Center, VAlign::Bottom);
            Uint32 cycleDiff = currentGame->getGameCycleCount() - structureSmoke.startGameCycle;

            Uint32 smokeFrame = (cycleDiff/25) % 4;
            if(smokeFrame == 3) {
                smokeFrame = 1;
            }

            smokeSource.x = smokeFrame * smokeSource.w;
            SDL_RenderCopy(renderer, pSmokeTex, &smokeSource, &smokeDest);
        }
    }
}

ObjectInterface* StructureBase::getInterfaceContainer() {
    if((pLocalHouse == owner) || (debug == true)) {
        // Non-builder structures with no specific interface (Wall, GunTurret,
        // RocketTurret, IX, NuclearPlant) get the city-sim stats panel when
        // city sim is active. Builder structures use BuilderInterface from
        // BuilderBase, so they bypass this entirely.
        if (currentGame && currentGame->isCitySimEnabled() && !isABuilder()) {
            return CityStatsStructureInterface::create(objectID);
        }
        return DefaultStructureInterface::create(objectID);
    } else {
        return DefaultObjectInterface::create(objectID);
    }
}

void StructureBase::drawSelectionBox() {
    SDL_Rect dest;
    dest.x = screenborder->world2screenX(realX);
    dest.y = screenborder->world2screenY(realY);
    dest.w = dune2rWorldExtent(world2zoomedWorld(TILESIZE * structureSize.x));
    dest.h = dune2rWorldExtent(world2zoomedWorld(TILESIZE * structureSize.y));

    //now draw the selection box thing, with parts at all corners of structure

    // top left bit
    for(int i=0;i<=currentZoomlevel;i++) {
        renderDrawHLine(renderer, dest.x+i, dest.y+i, dest.x+(currentZoomlevel+1)*3, COLOR_WHITE);
        renderDrawVLine(renderer, dest.x+i, dest.y+i, dest.y+(currentZoomlevel+1)*3, COLOR_WHITE);
    }

    // top right bit
    for(int i=0;i<=currentZoomlevel;i++) {
        renderDrawHLine(renderer, dest.x + dest.w-1 - i, dest.y+i, dest.x + dest.w-1 - (currentZoomlevel+1)*3, COLOR_WHITE);
        renderDrawVLine(renderer, dest.x + dest.w-1 - i, dest.y+i, dest.y+(currentZoomlevel+1)*3, COLOR_WHITE);
    }

    // bottom left bit
    for(int i=0;i<=currentZoomlevel;i++) {
        renderDrawHLine(renderer, dest.x+i, dest.y + dest.h-1 - i, dest.x+(currentZoomlevel+1)*3, COLOR_WHITE);
        renderDrawVLine(renderer, dest.x+i, dest.y + dest.h-1 - i, dest.y + dest.h-1 - (currentZoomlevel+1)*3, COLOR_WHITE);
    }

    // bottom right bit
    for(int i=0;i<=currentZoomlevel;i++) {
        renderDrawHLine(renderer, dest.x + dest.w-1 - i, dest.y + dest.h-1 - i, dest.x + dest.w-1 - (currentZoomlevel+1)*3, COLOR_WHITE);
        renderDrawVLine(renderer, dest.x + dest.w-1 - i, dest.y + dest.h-1 - i, dest.y + dest.h-1 - (currentZoomlevel+1)*3, COLOR_WHITE);
    }

    // health bar
    for(int i=1;i<=currentZoomlevel+1;i++) {
        renderDrawHLine(renderer, dest.x, dest.y-i-1, dest.x + (lround((getHealth()/getMaxHealth())*(dest.w - 1))), getHealthColor());
    }
}

void StructureBase::drawOtherPlayerSelectionBox() {
    SDL_Rect dest;
    dest.x = screenborder->world2screenX(realX) + (currentZoomlevel+1);
    dest.y = screenborder->world2screenY(realY) + (currentZoomlevel+1);
    dest.w = dune2rWorldExtent(world2zoomedWorld(TILESIZE * structureSize.x)) - 2*(currentZoomlevel+1);
    dest.h = dune2rWorldExtent(world2zoomedWorld(TILESIZE * structureSize.y)) - 2*(currentZoomlevel+1);

    //now draw the selection box thing, with parts at all corners of structure

    // top left bit
    for(int i=0;i<=currentZoomlevel;i++) {
        renderDrawHLine(renderer, dest.x+i, dest.y+i, dest.x+(currentZoomlevel+1)*2, COLOR_LIGHTBLUE);
        renderDrawVLine(renderer, dest.x+i, dest.y+i, dest.y+(currentZoomlevel+1)*2, COLOR_LIGHTBLUE);
    }

    // top right bit
    for(int i=0;i<=currentZoomlevel;i++) {
        renderDrawHLine(renderer, dest.x + dest.w-1 - i, dest.y+i, dest.x + dest.w-1 - (currentZoomlevel+1)*2, COLOR_LIGHTBLUE);
        renderDrawVLine(renderer, dest.x + dest.w-1 - i, dest.y+i, dest.y+(currentZoomlevel+1)*2, COLOR_LIGHTBLUE);
    }

    // bottom left bit
    for(int i=0;i<=currentZoomlevel;i++) {
        renderDrawHLine(renderer, dest.x+i, dest.y + dest.h-1 - i, dest.x+(currentZoomlevel+1)*2, COLOR_LIGHTBLUE);
        renderDrawVLine(renderer, dest.x+i, dest.y + dest.h-1 - i, dest.y + dest.h-1 - (currentZoomlevel+1)*2, COLOR_LIGHTBLUE);
    }

    // bottom right bit
    for(int i=0;i<=currentZoomlevel;i++) {
        renderDrawHLine(renderer, dest.x + dest.w-1 - i, dest.y + dest.h-1 - i, dest.x + dest.w-1 - (currentZoomlevel+1)*2, COLOR_LIGHTBLUE);
        renderDrawVLine(renderer, dest.x + dest.w-1 - i, dest.y + dest.h-1 - i, dest.y + dest.h-1 - (currentZoomlevel+1)*2, COLOR_LIGHTBLUE);
    }
}

void StructureBase::drawGatheringPointLine() {
    if(isABuilder() && (getItemID() != Structure_ConstructionYard) && destination.isValid() && (getOwner() == pLocalHouse)) {
        Coord indicatorPosition = destination*TILESIZE + Coord(TILESIZE/2, TILESIZE/2);
        Coord structurePosition = getCenterPoint();

        renderDrawLine( renderer,
                        screenborder->world2screenX(structurePosition.x), screenborder->world2screenY(structurePosition.y),
                        screenborder->world2screenX(indicatorPosition.x), screenborder->world2screenY(indicatorPosition.y),
                        COLOR_HALF_TRANSPARENT);


        SDL_Texture* pUIIndicator = pGFXManager->getUIGraphic(UI_Indicator);
        SDL_Rect source = calcSpriteSourceRect(pUIIndicator, 0, 3);
        SDL_Rect drawLocation = calcSpriteDrawingRect(  pUIIndicator,
                                                        screenborder->world2screenX(indicatorPosition.x),
                                                        screenborder->world2screenY(indicatorPosition.y),
                                                        3, 1,
                                                        HAlign::Center, VAlign::Center);

        // Render twice
        SDL_RenderCopy(renderer, pUIIndicator, &source, &drawLocation);
        SDL_RenderCopy(renderer, pUIIndicator, &source, &drawLocation);
    }
}

/**
    Returns the center point of this structure
    \return the center point in world coordinates
*/
Coord StructureBase::getCenterPoint() const {
    return Coord( lround(realX + structureSize.x*TILESIZE/2),
                  lround(realY + structureSize.y*TILESIZE/2));
}

Coord StructureBase::getClosestCenterPoint(const Coord& objectLocation) const {
    return getClosestPoint(objectLocation) * TILESIZE + Coord(TILESIZE/2, TILESIZE/2);
}

void StructureBase::handleActionClick(int xPos, int yPos) {
    if ((xPos < location.x) || (xPos >= (location.x + structureSize.x)) || (yPos < location.y) || (yPos >= (location.y + structureSize.y))) {
        currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_STRUCTURE_SETDEPLOYPOSITION,objectID, (Uint32) xPos, (Uint32) yPos));
    } else {
        currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_STRUCTURE_SETDEPLOYPOSITION,objectID, (Uint32) NONE_ID, (Uint32) NONE_ID));
    }
}

void StructureBase::handleRepairClick() {
    currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_STRUCTURE_REPAIR,objectID));
}

void StructureBase::doSetDeployPosition(int xPos, int yPos) {
    setTarget(nullptr);
    setDestination(xPos,yPos);
    setForced(true);
}


void StructureBase::doRepair() {
    repairing = true;
}

void StructureBase::setDestination(int newX, int newY) {
    if(currentGameMap->tileExists(newX, newY) || ((newX == INVALID_POS) && (newY == INVALID_POS))) {
        destination.x = newX;
        destination.y = newY;
    }
}

void StructureBase::setJustPlaced() {
    justPlacedTimer = 6;
    curAnimFrame = 0;
    animationCounter = -STRUCTURE_ANIMATIONTIMER; // make first build animation double as long
    enhancedPlacementStartMs = currentGame != nullptr
        ? currentGame->getGameTime()
        : 0;
    enhancedVisualState = -1;
}

void StructureBase::updateDegradation() {
    namespace Decay = DuneCity::Degradation;

    // Dynasty excludes walls (and slabs, which are tiles here) from both loops.
    if(itemID == Structure_Wall) return;

    // Foundation decay is independent of power. Dynasty checks the half-health
    // threshold before applying the full house amount, so the final hit can
    // cross that threshold. Process it before the house power tick, as Dynasty
    // does when both timers expire together.
    if(currentGame->getGameInitSettings().getGameOptions().concreteRequired
       && foundationDegrades && --foundationDecayTimer <= 0) {
        foundationDecayTimer = Decay::foundationDecayIntervalCycles();
        if(!Decay::foundationDecayExempt(currentGame->gameType,
                                         currentGame->getGameInitSettings().getMission())
           && getHealth() > FixPoint(Decay::foundationDecayFloor(getMaxHealth()))) {
            setHealth(getHealth() - FixPoint(Decay::houseDegradingAmount(owner->getHouseID())));
        }
    }

    // Use raw house supply/demand in every mode. House::hasPower() intentionally
    // exempts Vanilla from unrelated power rules, such as turret operation.
    if(--powerDamageTimer <= 0) {
        powerDamageTimer = Decay::powerDamageIntervalCycles();
        const FixPoint powerThreshold = FixPoint(Decay::powerHitpointsMax(getMaxHealth(),
                                                owner->getProducedPower(), owner->getPowerRequirement()));
        if(getHealth() > powerThreshold) {
            setHealth(getHealth() - FixPoint(Decay::kPowerDamagePerInterval));
        }
    }
}

bool StructureBase::update() {
    if(((currentGame->getGameCycleCount() + getObjectID()) % 512) == 0) {
        currentGameMap->viewMap(owner->getHouseID(), location, getViewRange());
    }

    if(!fogged) {
        lastVisibleFrame = curAnimFrame;
    }

    updateDegradation();

    updateStructureSpecificStuff();

    if(getHealth() <= 0) {
        destroy();
        return false;
    }

    if (!repairing && owner->isAutoRepairEnabled()
        && getHealth() < getMaxHealth() && owner->getCredits() >= 5) {
        doRepair();
    }
    if(repairing) {
        if(owner->getCredits() >= 5) {
            // Original dune 2 is doing the repair calculation with fix-point math (multiply everything with 256).
            // It is calculating what fraction 2 hitpoints of the maximum health would be.
            int fraction = (2*256)/getMaxHealth();
            FixPoint repairprice = FixPoint(fraction * currentGame->objectData.data[itemID][originalHouseID].price) / 256;

            // Original dune is always repairing 5 hitpoints (for the costs of 2) but we are only repairing 1/30th of that
            const auto repairHealth = 5_fix/30_fix;
            owner->takeCredits(repairprice/30);
            FixPoint newHealth = getHealth() + repairHealth;
            if(newHealth >= getMaxHealth()) {
                setHealth(getMaxHealth());
                repairing = false;
            } else {
                setHealth(newHealth);
            }
        } else {
            repairing = false;
        }
    } else if(owner->isAI() && (getHealth() < getMaxHealth()/2)) {
        doRepair();
    }

    // check smoke
    std::list<StructureSmoke>::iterator iter = smoke.begin();
    while(iter != smoke.end()) {
        if(currentGame->getGameCycleCount() - iter->startGameCycle >= MILLI2CYCLES(8*1000)) {
            smoke.erase(iter++);
        } else {
            ++iter;
        }
    }

    // update animations
    animationCounter++;
    if(animationCounter > STRUCTURE_ANIMATIONTIMER) {
        animationCounter = 0;
        curAnimFrame++;
        if((curAnimFrame < firstAnimFrame) || (curAnimFrame > lastAnimFrame)) {
            curAnimFrame = firstAnimFrame;
        }

        justPlacedTimer--;
        if((justPlacedTimer > 0) && (justPlacedTimer % 2 == 0)) {
            curAnimFrame = 0;
        }
    }

    return true;
}

void StructureBase::demolish() {
    demolishedByOwner_ = true;
    AITelemetry::log().write(currentGame->getGameCycleCount(), owner->getHouseID(), -1,
        "building_demolished", AITelemetry::Record().set("object",objectID).set("item",itemID)
            .set("x",location.x).set("y",location.y).set("refund",0));
    destroy(); // retain special destruction behaviour, including reactor blasts
}

void StructureBase::destroy() {
    if (currentGame && owner) AITelemetry::log().write(currentGame->getGameCycleCount(), owner->getHouseID(), -1,
        "object_destroyed", AITelemetry::Record().set("object", objectID).set("item", itemID)
            .set("x", location.x).set("y", location.y).set("health", getHealth().lround()));
    int*    pDestroyedStructureTiles = nullptr;
    int     DestroyedStructureTilesSizeY = 0;
    static int DestroyedStructureTilesWall[] = { DestroyedStructure_Wall };
    static int DestroyedStructureTiles1x1[] = { Destroyed1x1Structure };
    static int DestroyedStructureTiles2x2[] = { Destroyed2x2Structure_TopLeft, Destroyed2x2Structure_TopRight,
                                                Destroyed2x2Structure_BottomLeft, Destroyed2x2Structure_BottomRight };
    static int DestroyedStructureTiles3x2[] = { Destroyed3x2Structure_TopLeft, Destroyed3x2Structure_TopCenter, Destroyed3x2Structure_TopRight,
                                                Destroyed3x2Structure_BottomLeft, Destroyed3x2Structure_BottomCenter, Destroyed3x2Structure_BottomRight};
    static int DestroyedStructureTiles2x3[] = { Destroyed3x3Structure_TopLeft, Destroyed3x3Structure_TopCenter,
                                                Destroyed3x3Structure_CenterLeft, Destroyed3x3Structure_CenterCenter,
                                                Destroyed3x3Structure_BottomLeft, Destroyed3x3Structure_BottomCenter};
    static int DestroyedStructureTiles3x3[] = { Destroyed3x3Structure_TopLeft, Destroyed3x3Structure_TopCenter, Destroyed3x3Structure_TopRight,
                                                Destroyed3x3Structure_CenterLeft, Destroyed3x3Structure_CenterCenter, Destroyed3x3Structure_CenterRight,
                                                Destroyed3x3Structure_BottomLeft, Destroyed3x3Structure_BottomCenter, Destroyed3x3Structure_BottomRight};


    if(itemID == Structure_Wall) {
        pDestroyedStructureTiles = DestroyedStructureTilesWall;
        DestroyedStructureTilesSizeY = 1;
    } else {
        switch(structureSize.y) {
            case 1: {
                pDestroyedStructureTiles = DestroyedStructureTiles1x1;
                DestroyedStructureTilesSizeY = 1;
            } break;

            case 2: {
                if(structureSize.x == 2) {
                    pDestroyedStructureTiles = DestroyedStructureTiles2x2;
                    DestroyedStructureTilesSizeY = 2;
                } else if(structureSize.x == 3) {
                    pDestroyedStructureTiles = DestroyedStructureTiles3x2;
                    DestroyedStructureTilesSizeY = 3;
                } else {
                    THROW(std::runtime_error, "StructureBase::destroy(): Invalid structure size");
                }
            } break;

            case 3: {
                if(structureSize.x == 2) {
                    pDestroyedStructureTiles = DestroyedStructureTiles2x3;
                    DestroyedStructureTilesSizeY = 2;
                } else if(structureSize.x == 3) {
                    pDestroyedStructureTiles = DestroyedStructureTiles3x3;
                    DestroyedStructureTilesSizeY = 3;
                } else {
                    THROW(std::runtime_error, "StructureBase::destroy(): Invalid structure size");
                }
            } break;

            default: {
                THROW(std::runtime_error, "StructureBase::destroy(): Invalid structure size");
            } break;
        }
    }

    if(itemID != Structure_Wall) {
        for(int j = 0; j < structureSize.y; j++) {
            for(int i = 0; i < structureSize.x; i++) {
                Tile* pTile = currentGameMap->getTile(location.x + i, location.y + j);
                pTile->setDestroyedStructureTile(pDestroyedStructureTiles[DestroyedStructureTilesSizeY*j + i]);

                Coord position((location.x+i)*TILESIZE + TILESIZE/2, (location.y+j)*TILESIZE + TILESIZE/2);
                Uint32 explosionID = currentGame->randomGen.getRandOf({Explosion_Large1,Explosion_Large2});
                currentGame->getExplosionList().push_back(new Explosion(explosionID, position, owner->getHouseID()) );

                if(currentGame->randomGen.rand(1,100) <= getInfSpawnProp()) {
                    UnitBase* pNewUnit = owner->createUnit(Unit_Soldier);
                    pNewUnit->setHealth(pNewUnit->getMaxHealth()/2);
                    pNewUnit->deploy(location + Coord(i,j));
                }
            }
        }
    }

    if(isVisible(pLocalHouse->getTeamID()))
        soundPlayer->playSoundAt(Sound_ExplosionStructure, location);


    delete this;
}

Coord StructureBase::getClosestPoint(const Coord& objectLocation) const {
    Coord closestPoint;

    // find the closest tile of a structure from a location
    if(objectLocation.x <= location.x) {
        // if we are left of the structure
        // set destination, left most point
        closestPoint.x = location.x;
    } else if(objectLocation.x >= (location.x + structureSize.x-1)) {
        //vica versa
        closestPoint.x = location.x + structureSize.x-1;
    } else {
        //we are above or below at least one tile of the structure, closest path is straight
        closestPoint.x = objectLocation.x;
    }

    //same deal but with y
    if(objectLocation.y <= location.y) {
        closestPoint.y = location.y;
    } else if(objectLocation.y >= (location.y + structureSize.y-1)) {
        closestPoint.y = location.y + structureSize.y-1;
    } else {
        closestPoint.y = objectLocation.y;
    }

    return closestPoint;
}
