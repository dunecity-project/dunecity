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

#include <GUI/dune/BuilderList.h>

#include <globals.h>
#include <CursorManager.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/FontManager.h>
#include <FileClasses/TextManager.h>
#include <misc/draw_util.h>

#include <Game.h>
#include <House.h>
#include <SoundPlayer.h>
#include <sand.h>
#include <dunecity/CityConstants.h>
#include <mod/ModManager.h>
#include <FileClasses/FileManager.h>
#include <FileClasses/LoadSavePNG.h>
#include <GUI/dune/StructureSizePreview.h>
#include <units/HarvesterHelpers.h>

#include <structures/BuilderBase.h>
#include <structures/StarPort.h>
#include <structures/ChaosFactory.h>
#include <structures/ConstructionYard.h>
#include <algorithm>
#include <sstream>
#include <vector>

namespace Layout = BuilderListLayout;

namespace {

constexpr int ArrowX = Layout::BodyLeft + (Layout::BodyWidth - Layout::ArrowButtonWidth)/2;

int productionPortraitSlot(int itemID) {
    switch(itemID) {
        case Unit_Soldier: return 0;
        case Unit_Infantry: return 1;
        case Unit_Infantry5: return 2;
        case Unit_Trooper: return 3;
        case Unit_Troopers: return 4;
        case Unit_Troopers5: return 5;
        default: return -1;
    }
}

/// Overlay text, lines centred, at the largest font size whose lines fit inside a grid button.
/// Composed per line so the texture is exactly as large as the text (FontManager's multiline
/// surface is as wide as the whole string on one line).
sdl2::texture_ptr createFittedOverlayText(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream input(text);
    for(std::string line; std::getline(input, line); ) {
        lines.push_back(line);
    }
    auto extent = [&](unsigned int size) {
        int width = 0;
        for(const std::string& line : lines) {
            width = std::max(width, pFontManager->getTextWidth(line, size));
        }
        return Point(width, pFontManager->getTextHeight(size) * static_cast<int>(lines.size()));
    };
    constexpr unsigned int largest = 12, smallest = 8;
    unsigned int size = largest;
    while(size > smallest && (extent(size).x > Layout::ButtonWidth - 2 || extent(size).y > Layout::ButtonHeight - 2)) {
        --size;
    }

    const Point textSize = extent(size);
    sdl2::surface_ptr pic{ SDL_CreateRGBSurface(0, textSize.x, textSize.y, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    SDL_SetSurfaceBlendMode(pic.get(), SDL_BLENDMODE_NONE);
    SDL_FillRect(pic.get(), nullptr, COLOR_INVALID);
    SDL_SetColorKey(pic.get(), SDL_TRUE, COLOR_INVALID);
    const int lineHeight = pFontManager->getTextHeight(size);
    for(std::size_t i = 0; i < lines.size(); ++i) {
        sdl2::surface_ptr line = pFontManager->createSurfaceWithText(lines[i], COLOR_WHITE, size);
        SDL_Rect dest = calcDrawingRect(line.get(), textSize.x/2, static_cast<int>(i)*lineHeight, HAlign::Center, VAlign::Top);
        SDL_BlitSurface(line.get(), nullptr, pic.get(), &dest);
    }
    return convertSurfaceToTexture(pic.get());
}

/// Draw a build picture scaled into a grid button: aspect ratio kept, centred, never enlarged.
/// Linear filtering keeps the downscaled pixel art readable; the texture's own mode is restored.
void drawItemPicture(SDL_Texture* pTexture, const SDL_Rect& button) {
    const int w = getWidth(pTexture);
    const int h = getHeight(pTexture);
    if(w <= 0 || h <= 0) {
        return;
    }
    int destW = std::min(w, button.w);
    int destH = (h*destW + w/2) / w;
    if(destH > button.h) {
        destH = button.h;
        destW = (w*destH + h/2) / h;
    }
    SDL_Rect dest = { button.x + (button.w - destW)/2, button.y + (button.h - destH)/2, destW, destH };
    SDL_ScaleMode previous = SDL_ScaleModeNearest;
    SDL_GetTextureScaleMode(pTexture, &previous);
    SDL_SetTextureScaleMode(pTexture, SDL_ScaleModeLinear);
    SDL_RenderCopy(renderer, pTexture, nullptr, &dest);
    SDL_SetTextureScaleMode(pTexture, previous);
}

/// Upper and lower cap art was cut for a 101 px list. Keep both ends and repeat a plain
/// middle strip so the cap spans `width` (the frame edge stays CapRight from the right end).
void drawCap(SDL_Texture* pCap, int x, int y, int width) {
    constexpr int leftPart = 64;
    constexpr int stripBegin = 44;
    constexpr int stripWidth = leftPart - stripBegin;
    const int capW = getWidth(pCap);
    const int capH = getHeight(pCap);
    const int rightPart = capW - leftPart;

    SDL_Rect src = { 0, 0, leftPart, capH };
    SDL_Rect dest = { x, y, leftPart, capH };
    SDL_RenderCopy(renderer, pCap, &src, &dest);
    for(int fill = x + leftPart; fill < x + width - rightPart; fill += stripWidth) {
        const int w = std::min(stripWidth, x + width - rightPart - fill);
        src = { stripBegin, 0, w, capH };
        dest = { fill, y, w, capH };
        SDL_RenderCopy(renderer, pCap, &src, &dest);
    }
    src = { leftPart, 0, rightPart, capH };
    dest = { x + width - rightPart, y, rightPart, capH };
    SDL_RenderCopy(renderer, pCap, &src, &dest);
}

} // namespace

BuilderList::BuilderList(Uint32 builderObjectID) {
    enableResizing(false,true);
    this->builderObjectID = builderObjectID;

    upButton.setTextures(   pGFXManager->getUIGraphic(UI_ButtonUp,pLocalHouse->getHouseID()),
                            pGFXManager->getUIGraphic(UI_ButtonUp_Pressed,pLocalHouse->getHouseID()));

    downButton.setTextures( pGFXManager->getUIGraphic(UI_ButtonDown,pLocalHouse->getHouseID()),
                            pGFXManager->getUIGraphic(UI_ButtonDown_Pressed,pLocalHouse->getHouseID()));

    addWidget(&upButton,Point(ArrowX,0),upButton.getSize());
    upButton.setOnClick(std::bind(&BuilderList::onUp, this));

    addWidget(&downButton,
                Point(ArrowX, Layout::ArrowButtonHeight + Layout::Spacing),
                downButton.getSize());
    downButton.setOnClick(std::bind(&BuilderList::onDown, this));

    addWidget(&orderButton,
                Point(Layout::BodyLeft, Layout::ArrowButtonHeight + 2*Layout::Spacing),
                Point(Layout::BodyWidth, Layout::OrderButtonHeight));
    orderButton.setOnClick(std::bind(&BuilderList::onOrder, this));
    orderButton.setText(_("Order"));
    const auto* builder = dynamic_cast<const BuilderBase*>(currentGame->getObjectManager().getObject(builderObjectID));
    if(builder && ModManager::instance().isTornieFamilyContentActive()
       && (builder->getItemID() == Structure_Barracks || builder->getItemID() == Structure_WOR
           || builder->getItemID() == Structure_Worfinery)) {
        const char* files[] = {"ProductionSoldier.png", "ProductionInfantry3.png", "ProductionInfantry5.png",
                               "ProductionTrooper.png", "ProductionTroopers3.png", "ProductionTroopers5.png"};
        if(isHouseFaction(static_cast<HOUSETYPE>(builder->getTechnologyHouseID()), HOUSE_KLESHMERSH)) {
            files[3] = "ProductionTrooperFlames.png";
            files[4] = "ProductionTroopersFlames3.png";
            files[5] = "ProductionTroopersFlames5.png";
        }
        for(int slot = 0; slot < 6; ++slot) if(pFileManager->exists(files[slot])) {
            auto surface = LoadPNG_RW(pFileManager->openFile(files[slot]).get());
            if(surface) productionPortraits[slot] = convertSurfaceToTexture(surface.get());
        }
    }

    currentListPos = 0;

    mouseLeftButton = -1;
    mouseRightButton = -1;

    // Grid buttons are small: short texts, each at the largest size that fits a button.
    pSoldOutTextTexture = createFittedOverlayText(_("SOLD OUT"));
    pAlreadyBuiltTextTexture = createFittedOverlayText(_("ALREADY\nBUILT"));
    pPlaceItTextTexture = createFittedOverlayText(_("PLACE IT"));
    pBlockedTextTexture = createFittedOverlayText(_("BLOCKED"));
    pPlacingTextTexture = createFittedOverlayText(_("PLACING"));
    pPlanningTextTexture = createFittedOverlayText(_("PLAN\nSITE"));
    pOnHoldTextTexture = createFittedOverlayText(_("ON HOLD"));
    pUnitLimitReachedTextTexture = createFittedOverlayText(_("UNIT\nLIMIT"));
    pHarvesterLimitReachedTextTexture = createFittedOverlayText(_("HARVESTER\nLIMIT"));

    pLastTooltip = nullptr;
    tooltipText = "";
    lastMouseMovement = (1u<<31);

    resize(BuilderList::getMinimumSize().x, BuilderList::getMinimumSize().y);
}

BuilderList::~BuilderList() = default;

SDL_Texture* BuilderList::getProductionPortrait(int itemID, int technologyHouse) const {
    if(auto* portrait = pGFXManager->getFactionTrooperProductionPortrait(itemID, technologyHouse)) return portrait;
    const int slot = productionPortraitSlot(itemID);
    return slot < 0 ? nullptr : productionPortraits[slot].get();
}

void BuilderList::handleMouseMovement(Sint32 x, Sint32 y, bool insideOverlay) {
    StaticContainer::handleMouseMovement(x,y,insideOverlay);

    if((x >= 0) && (x < getSize().x) && (y >= 0) && (y < getSize().y) && !insideOverlay) {
        lastMouseMovement = SDL_GetTicks();
        lastMousePos.x = x;
        lastMousePos.y = y;
    } else {
        lastMousePos.x = INVALID_POS;
        lastMousePos.y = INVALID_POS;
    }
}

bool BuilderList::handleMouseLeft(Sint32 x, Sint32 y, bool pressed) {
    // Containers dispatch to every child, including clicks outside this list.
    // Only consume our own rectangle, and cancel any press dragged out of it.
    if(!isVisible() || !isEnabled() || x < 0 || y < 0 || x >= getSize().x || y >= getSize().y) {
        mouseLeftButton = -1;
        return false;
    }
    StaticContainer::handleMouseLeft(x,y,pressed);

    BuilderBase* pBuilder = dynamic_cast<BuilderBase*>(currentGame->getObjectManager().getObject(builderObjectID));
    if(!pBuilder) {
        return false;
    }

    StarPort* pStarport = dynamic_cast<StarPort*>(pBuilder);
    if(pStarport && (pStarport->okToOrder() == false)) {
        return false;
    }

    if(pressed == true) {
        mouseLeftButton = getButton(x,y);
    } else {
        if(mouseLeftButton == getButton(x,y)) {
            // button released
            assert(pBuilder);
            const int clickedItemID = getItemIDFromIndex(mouseLeftButton);
            if((SDL_GetModState() & KMOD_ALT) && clickedItemID != ItemID_Invalid && pBuilder->supportsRepeat(clickedItemID)) {
                // Alt+click toggles repeat production of a unit in a factory (docs/rts-controls.md).
                soundPlayer->playSound(Sound_ButtonClick);
                pBuilder->handleSetRepeatClick(clickedItemID, !pBuilder->isRepeating(clickedItemID));
            } else if((clickedItemID == static_cast<int>(pBuilder->getCurrentProducedItem())) && pBuilder->mayPlaceByHand()) {
                soundPlayer->playSound(Sound_ButtonClick);
                if(currentGame->currentCursorMode == Game::CursorMode_Placing && !currentGame->isPlanningPlacement()) {
                    currentGame->setCursorMode(Game::CursorMode_Normal);
                } else {
                    currentGame->setCursorMode(Game::CursorMode_Placing);
                }
            } else if((getItemIDFromIndex(mouseLeftButton) == static_cast<int>(pBuilder->getCurrentProducedItem())) && (pBuilder->isOnHold() == true)) {
                soundPlayer->playSound(Sound_ButtonClick);
                pBuilder->handleSetOnHoldClick(false);
            } else {
                const int clickedItem = getItemIDFromIndex(mouseLeftButton);
                ConstructionYard* pYard = dynamic_cast<ConstructionYard*>(pBuilder);
                if(clickedItem != ItemID_Invalid && pYard != nullptr
                   && currentGame->getGameInitSettings().getGameOptions().planBuildingSites
                   && ConstructionYard::isReservableItem(clickedItem)) {
                    // Pre-placement: pick the plot first; the map click orders it. Planning stays
                    // on the building after each plot, so Shift has no x5 meaning here.
                    if(currentGame->getPlanningItemID() == static_cast<Uint32>(clickedItem)) {
                        soundPlayer->playSound(Sound_ButtonClick);
                        currentGame->setCursorMode(Game::CursorMode_Normal);
                    } else if(pYard->mayReserveItem(clickedItem)) {
                        soundPlayer->playSound(Sound_ButtonClick);
                        currentGame->beginPlacementPlanning(builderObjectID, clickedItem);
                    } else {
                        soundPlayer->playSound(Sound_InvalidAction);
                    }
                } else if(clickedItem != ItemID_Invalid) {
                    soundPlayer->playSound(Sound_ButtonClick);
                    pBuilder->handleProduceItemClick(clickedItem, SDL_GetModState() & KMOD_SHIFT);
                }
            }
        }

        mouseLeftButton = -1;
    }

    return true;
}

bool BuilderList::handleMouseRight(Sint32 x, Sint32 y, bool pressed) {
    // Containers dispatch to every child, including clicks outside this list.
    // Only consume our own rectangle, and cancel any press dragged out of it.
    if(!isVisible() || !isEnabled() || x < 0 || y < 0 || x >= getSize().x || y >= getSize().y) {
        mouseRightButton = -1;
        return false;
    }
    StaticContainer::handleMouseRight(x,y,pressed);

    BuilderBase* pBuilder = dynamic_cast<BuilderBase*>(currentGame->getObjectManager().getObject(builderObjectID));
    if(!pBuilder) {
        return false;
    }

    StarPort* pStarport = dynamic_cast<StarPort*>(pBuilder);
    if(pStarport && (pStarport->okToOrder() == false)) {
        return false;
    }

    if(pressed == true) {
        mouseRightButton = getButton(x,y);
    } else {
        if(mouseRightButton == getButton(x,y)) {
            // button released
            assert(pBuilder);
            if(!pointerIsTouch() && (getItemIDFromIndex(mouseRightButton) == (int) pBuilder->getCurrentProducedItem()) && (pBuilder->isOnHold() == false)) {
                soundPlayer->playSound(Sound_ButtonClick);
                pBuilder->handleSetOnHoldClick(true);
            } else {
                if(getItemIDFromIndex(mouseRightButton) != ItemID_Invalid) {
                    soundPlayer->playSound(Sound_ButtonClick);
                    pBuilder->handleCancelItemClick(getItemIDFromIndex(mouseRightButton), SDL_GetModState() & KMOD_SHIFT);
                }
            }
        }

        mouseRightButton = -1;
    }

    return true;
}

bool BuilderList::handleMouseWheel(Sint32 x, Sint32 y, bool up) {
    if((x >= 0) && (x < getSize().x) && (y >= 0) && (y < getSize().y)) {
        if(up) {
            onUp();
        } else {
            onDown();
        }
        return true;
    } else {
        return false;
    }
}

bool BuilderList::handleKeyPress(SDL_KeyboardEvent& key) {
    return StaticContainer::handleKeyPress(key);
}

void BuilderList::draw(Point position) {
    const int height = getSize().y;
    SDL_Rect blackRectDest = {  position.x + Layout::BodyLeft, position.y + Layout::BodyTop,
                                Layout::BodyWidth, Layout::bodyHeight(height) };
    renderFillRect(renderer, &blackRectDest, COLOR_BLACK);

    BuilderBase* pBuilder = dynamic_cast<BuilderBase*>(currentGame->getObjectManager().getObject(builderObjectID));
    if(pBuilder != nullptr) {
        StarPort* pStarport = dynamic_cast<StarPort*>(pBuilder);
        ChaosFactory* pChaosFactory = dynamic_cast<ChaosFactory*>(pBuilder);

        if(pStarport != nullptr) {
            orderButton.setVisible(true);
            orderButton.setEnabled(pStarport->okToOrder());
        } else {
            orderButton.setVisible(false);
        }

        const int numItems = static_cast<int>(pBuilder->getBuildList().size());
        const int numSlots = getNumButtons(height);
        if(numSlots < numItems) {
            upButton.setVisible(true);
            downButton.setVisible(true);
            upButton.setEnabled(currentListPos > 0);
            downButton.setEnabled(currentListPos < Layout::maxFirstItem(numItems, height));
        } else {
            upButton.setVisible(false);
            downButton.setVisible(false);
        }

        int i = 0;
        for(const BuildItem& buildItem : pBuilder->getBuildList()) {
            const int slot = i - currentListPos;
            i++;
            if(slot < 0 || slot >= numSlots) {
                continue;
            }

            SDL_Rect button = { position.x + Layout::slotX(slot), position.y + Layout::slotY(slot),
                                Layout::ButtonWidth, Layout::ButtonHeight };
            const int centerX = button.x + button.w/2;
            const int centerY = button.y + button.h/2;
            auto drawCentered = [&](SDL_Texture* pText) {
                SDL_Rect textDest = calcDrawingRect(pText, centerX, centerY, HAlign::Center, VAlign::Center);
                SDL_RenderCopy(renderer, pText, nullptr, &textDest);
            };

            const HOUSETYPE pictureHouse = static_cast<HOUSETYPE>(
                DuneCity::isCityOnlyStructure(buildItem.itemID) && pBuilder->getOwner()
                    ? pBuilder->getOwner()->getHouseID()
                    : ModManager::instance().isTornieFamilyContentActive()
                        ? pBuilder->getTechnologyHouseID() : pBuilder->getOriginalHouseID());
            SDL_Texture* pTexture = resolveItemPicture(buildItem.itemID, pictureHouse);
            if(auto* portrait = getProductionPortrait(buildItem.itemID, pictureHouse)) pTexture = portrait;
            if(pTexture != nullptr) {
                drawItemPicture(pTexture, button);
            }

            const auto structureSize = isStructure(buildItem.itemID) ? getStructureSize(buildItem.itemID) : Coord(0, 0);
            const bool extendedFootprint = ModManager::instance().isTornieFamilyContentActive()
                && isStructure(buildItem.itemID) && (structureSize.x > 3 || structureSize.y > 3);
            if(extendedFootprint) {
                drawStructureSizePreview(renderer, pGFXManager->getUIGraphic(UI_StructureSizeLattice),
                    pGFXManager->getUIGraphic(UI_StructureSizeConcrete), button.x + 1, button.y + 1,
                    structureSize.x, structureSize.y, 4);
            } else if(isStructure(buildItem.itemID)) {
                // Structure size lattice at two thirds: 6 px cells become 4 px cells.
                SDL_Texture* pLattice = pGFXManager->getUIGraphic(UI_StructureSizeLattice);
                SDL_Rect destLattice = { button.x + 1, button.y + 1, (getWidth(pLattice)*2 + 1)/3, (getHeight(pLattice)*2 + 1)/3 };
                SDL_RenderCopy(renderer, pLattice, nullptr, &destLattice);

                SDL_Texture* pConcrete = pGFXManager->getUIGraphic(UI_StructureSizeConcrete);
                SDL_Rect srcConcrete = { 0, 0, 1 + structureSize.x*6, 1 + structureSize.y*6 };
                SDL_Rect destConcrete = { button.x + 1, button.y + 1, 1 + structureSize.x*4, 1 + structureSize.y*4 };
                SDL_RenderCopy(renderer, pConcrete, &srcConcrete, &destConcrete);
            }

            // The building on the placing cursor (finished, placed by hand) or the plan cursor.
            const bool placingSelected = buildItem.itemID == pBuilder->getCurrentProducedItem()
                && pBuilder->mayPlaceByHand()
                && currentGame->currentCursorMode == Game::CursorMode_Placing && !currentGame->isPlanningPlacement();
            const bool planningSelected = currentGame->getPlanningItemID() == buildItem.itemID;

            // draw price
            const int shownPrice = buildItem.itemID == pBuilder->getCurrentProducedItem()
                ? pBuilder->getCurrentProductionPrice() : buildItem.price;
            const Uint32 priceColor = ModManager::instance().isTornieFamilyContentActive()
                && (buildItem.itemID == Unit_RaiderTrike || buildItem.itemID == Unit_RocketTrike)
                ? COLOR_BLACK : COLOR_WHITE;
            sdl2::texture_ptr pPriceTexture = pFontManager->createTextureWithText(fmt::sprintf("%d", shownPrice), priceColor, 12);
            SDL_Rect drawLocation = calcDrawingRect(pPriceTexture.get(), button.x + 2, button.y + button.h - getHeight(pPriceTexture.get()) + 3);
            SDL_RenderCopy(renderer, pPriceTexture.get(), nullptr, &drawLocation);

            if(pStarport != nullptr) {
                bool bSoldOut = (pStarport->getOwner()->getChoam().getNumAvailable(buildItem.itemID) == 0);

                if(!pStarport->okToOrder() || bSoldOut) {
                    renderFillRect(renderer, &button, COLOR_HALF_TRANSPARENT);
                }

                if(bSoldOut) {
                    drawCentered(pSoldOutTextTexture.get());
                }

            } else if(pChaosFactory != nullptr
                      && pChaosFactory->getRemainingStock(buildItem.itemID) <= 0) {
                renderFillRect(renderer, &button, COLOR_HALF_TRANSPARENT);
                drawCentered(pSoldOutTextTexture.get());
            } else if(currentGame->getGameInitSettings().getGameOptions().onlyOnePalace && buildItem.itemID == Structure_Palace && pBuilder->getOwner()->getNumItems(Structure_Palace) > 0) {
                renderFillRect(renderer, &button, COLOR_HALF_TRANSPARENT);
                drawCentered(pAlreadyBuiltTextTexture.get());
            } else if(buildItem.itemID == pBuilder->getCurrentProducedItem()) {
                FixPoint progress = pBuilder->getProductionProgress();
                FixPoint price = pBuilder->getCurrentProductionPrice();
                int max_x = lround((progress/price)*button.w);

                SDL_Rect progressBar = { button.x, button.y, max_x, button.h };
                renderFillRect(renderer, &progressBar, COLOR_HALF_TRANSPARENT);

                if(placingSelected) {
                    drawCentered(pPlacingTextTexture.get());
                } else if(pBuilder->isWaitingToPlace()) {
                    drawCentered(pPlaceItTextTexture.get());
                } else if(pBuilder->mayPlaceByHand()) {
                    // A planned plot blocked by units: it waits, or the player places it elsewhere.
                    drawCentered(pBlockedTextTexture.get());
                } else if(planningSelected) {
                    drawCentered(pPlanningTextTexture.get());
                } else if(pBuilder->isOnHold()) {
                    drawCentered(pOnHoldTextTexture.get());
                } else if(pBuilder->isUnitLimitReached(buildItem.itemID)) {
                    const bool harvester = isHarvesterLikeUnit(buildItem.itemID);
                    drawCentered(harvester ? pHarvesterLimitReachedTextTexture.get() : pUnitLimitReachedTextTexture.get());
                }
            } else if(planningSelected) {
                drawCentered(pPlanningTextTexture.get());
            }

            if(placingSelected || planningSelected) {
                // The building on the placing or plan-placement cursor: a gold frame, two pixels wide.
                const Uint32 selectedEdge = COLOR_RGB(245,199,103);
                SDL_Rect outer{button.x - 1, button.y - 1, button.w + 2, button.h + 2};
                SDL_Rect inner{button.x, button.y, button.w, button.h};
                renderDrawRect(renderer, &outer, selectedEdge);
                renderDrawRect(renderer, &inner, selectedEdge);
            }

            if(buildItem.num > 0) {
                // draw number of this in build list
                sdl2::texture_ptr pNumberTexture = pFontManager->createTextureWithText(fmt::sprintf("%d", buildItem.num), COLOR_RED, 12);
                SDL_Rect drawLocationNumber = calcDrawingRect(pNumberTexture.get(), button.x + button.w - 3, button.y + button.h + 2, HAlign::Right, VAlign::Bottom);
                SDL_RenderCopy(renderer, pNumberTexture.get(), nullptr, &drawLocationNumber);
            }

            if(pBuilder->isRepeating(buildItem.itemID)) {
                // Loop marker: a small ring with an arrowhead in the top right corner.
                const int cx = button.x + button.w - 8;
                const int cy = button.y + 8;
                const Coord ring[] = { Coord(cx-2,cy-4), Coord(cx+2,cy-4), Coord(cx+4,cy-2), Coord(cx+4,cy+2),
                                       Coord(cx+2,cy+4), Coord(cx-2,cy+4), Coord(cx-4,cy+2), Coord(cx-4,cy-2) };
                SDL_Rect backing{cx-6, cy-6, 13, 13};
                renderFillRect(renderer, &backing, COLOR_HALF_TRANSPARENT);
                for(int segment = 0; segment < 7; ++segment) {
                    renderDrawLine(renderer, ring[segment].x, ring[segment].y, ring[segment+1].x, ring[segment+1].y, COLOR_YELLOW);
                }
                renderDrawLine(renderer, cx-4, cy-2, cx-6, cy-4, COLOR_YELLOW);
                renderDrawLine(renderer, cx-4, cy-2, cx-2, cy-5, COLOR_YELLOW);
            }
        }
    }

    // The caps frame the list body: CapLeft beyond its left edge, CapRight beyond its right edge.
    const int capX = blackRectDest.x - Layout::CapLeft;
    const int capWidth = Layout::CapLeft + Layout::BodyWidth + Layout::CapRight;
    SDL_Texture* pBuilderListUpperCap = pGFXManager->getUIGraphic(UI_BuilderListUpperCap);
    const int upperCapY = blackRectDest.y - 13 + 4;
    drawCap(pBuilderListUpperCap, capX, upperCapY, capWidth);

    SDL_Texture* pBuilderListLowerCap = pGFXManager->getUIGraphic(UI_BuilderListLowerCap);
    const int lowerCapY = blackRectDest.y + blackRectDest.h - 3 - 4;
    drawCap(pBuilderListLowerCap, capX, lowerCapY, capWidth);

    renderDrawVLine(renderer, blackRectDest.x + Layout::BodyWidth, upperCapY + getHeight(pBuilderListUpperCap), lowerCapY, COLOR_RGB(125,80,0));

    StaticContainer::draw(position);
}

void BuilderList::drawOverlay(Point position) {
    if(SDL_GetTicks() - lastMouseMovement <= 800) return;

    // Draw tooltip
    int btn = getButton(lastMousePos.x,lastMousePos.y);
    if(btn != -1) {
        BuilderBase* pBuilder = dynamic_cast<BuilderBase*>(currentGame->getObjectManager().getObject(builderObjectID));
        if(!pBuilder) {
            return;
        }

        const auto buildItemIter = std::next(pBuilder->getBuildList().begin(), btn);

        std::string text = resolveItemName(buildItemIter->itemID);

        if(buildItemIter->itemID == pBuilder->getCurrentProducedItem() && pBuilder->mayPlaceByHand()) {
            text += " (Hotkey: P)";
        }
        if(pBuilder->isRepeating(buildItemIter->itemID)) {
            text += " - repeating (Alt+click: stop, right-click: cancel)";
        } else if(pBuilder->supportsRepeat(buildItemIter->itemID)) {
            text += " (Alt+click: repeat)";
        }

        if(text != tooltipText) {
            pLastTooltip.reset();
        }

        if(pLastTooltip == nullptr) {
            pLastTooltip = convertSurfaceToTexture(GUIStyle::getInstance().createToolTip(text));
            tooltipText = text;
        }

        // Left of the list, whichever column is hovered, so it never covers the other column.
        SDL_Rect dest = calcDrawingRect(pLastTooltip.get(), position.x + Layout::BodyLeft - 1, position.y + lastMousePos.y, HAlign::Right, VAlign::Center);
        SDL_RenderCopy(renderer, pLastTooltip.get(), nullptr, &dest);
    }
}


void BuilderList::resize(Uint32 width, Uint32 height) {
    const int realHeight = Layout::realHeight(height);
    setWidgetGeometry(  &upButton,Point(ArrowX,-2),upButton.getSize());
    setWidgetGeometry(  &downButton,
                        Point(ArrowX, realHeight - Layout::ArrowButtonHeight - Layout::OrderButtonHeight - Layout::Spacing + 2),
                        downButton.getSize());

    setWidgetGeometry(  &orderButton,
                        Point(Layout::BodyLeft, realHeight - Layout::OrderButtonHeight + 2),
                        Point(Layout::BodyWidth, Layout::OrderButtonHeight));

    StaticContainer::resize(width,height);

    // Clamp scroll position to valid bounds (don't reset to produced item —
    // that steals the human player's scroll when QuantBot AI triggers relayout)
    BuilderBase* pBuilder = dynamic_cast<BuilderBase*>(currentGame->getObjectManager().getObject(builderObjectID));
    if(pBuilder != nullptr) {
        currentListPos = std::min(currentListPos, Layout::maxFirstItem(static_cast<int>(pBuilder->getBuildList().size()), getSize().y));
    }
}

void BuilderList::onUp() {
    currentListPos = Layout::scrollUp(currentListPos);
}

void BuilderList::onDown() {
    BuilderBase* pBuilder = dynamic_cast<BuilderBase*>(currentGame->getObjectManager().getObject(builderObjectID));
    if(pBuilder != nullptr) {
        currentListPos = Layout::scrollDown(currentListPos, static_cast<int>(pBuilder->getBuildList().size()), getSize().y);
    }
}

void BuilderList::onOrder() {
    StarPort* pStarport = dynamic_cast<StarPort*>(currentGame->getObjectManager().getObject(builderObjectID));
    if(pStarport) {
        pStarport->handlePlaceOrderClick();
    }
}

int BuilderList::getNumButtons(int height) {
    return Layout::slots(height);
}

Point BuilderList::getButtonPosition(int BtnNumber) {
    return Point(Layout::slotX(BtnNumber), Layout::slotY(BtnNumber));
}

int BuilderList::getButton(int x, int y) {
    BuilderBase* pBuilder = dynamic_cast<BuilderBase*>(currentGame->getObjectManager().getObject(builderObjectID));
    const int slot = Layout::slotAt(x, y, getSize().y);
    if(pBuilder == nullptr || slot < 0) {
        return -1;
    }
    const int index = currentListPos + slot;
    return index < static_cast<int>(pBuilder->getBuildList().size()) ? index : -1;
}

int BuilderList::getItemIDFromIndex(int i) const {

    if (i >= 0) {
        const auto pBuilder = dynamic_cast<BuilderBase*>(currentGame->getObjectManager().getObject(builderObjectID));

        if (pBuilder != nullptr) {
            const auto buildItemIter = std::next(pBuilder->getBuildList().begin(), i);
            if (buildItemIter != pBuilder->getBuildList().end()) {
                return buildItemIter->itemID;
            }
        }
    }

    return ItemID_Invalid;
}
