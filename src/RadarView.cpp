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

#include <RadarView.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>

#include <Game.h>
#include <Map.h>
#include <House.h>
#include <ScreenBorder.h>
#include <Tile.h>
#include <SoundPlayer.h>
#include <units/AmbientAirplane.h>
#include <units/UnitBase.h>
#include <sand.h>

#include <globals.h>

#include <misc/draw_util.h>


RadarView::RadarView()
 : RadarViewBase(), currentRadarMode(RadarMode::RadarOff), animFrame(NUM_STATIC_FRAMES - 1), animCounter(NUM_STATIC_FRAME_TIME)
{
    radarStaticAnimation = pGFXManager->getUIGraphic(UI_RadarAnimation);

    radarSurface = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, 128, 128, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if(radarSurface == nullptr) {
        THROW(std::runtime_error, "RadarView::RadarView(): Cannot create new surface!");
    }
    SDL_FillRect(radarSurface.get(), nullptr, COLOR_BLACK);

    radarTexture = sdl2::texture_ptr{ SDL_CreateTexture(renderer, SCREEN_FORMAT, SDL_TEXTUREACCESS_STREAMING, 128, 128) };
}


RadarView::~RadarView() = default;

int RadarView::getMapSizeX() const {
    return currentGameMap->getSizeX();
}

int RadarView::getMapSizeY() const {
    return currentGameMap->getSizeY();
}


void RadarView::draw(Point position)
{
    SDL_Rect radarPosition = { position.x + RADARVIEW_BORDERTHICKNESS, position.y + RADARVIEW_BORDERTHICKNESS, RADARWIDTH, RADARHEIGHT};

    switch(currentRadarMode) {
        case RadarMode::RadarOff:
        case RadarMode::RadarOn: {
            int mapSizeX = currentGameMap->getSizeX();
            int mapSizeY = currentGameMap->getSizeY();

            RadarScaleInfo scaleInfo = calculateScaleAndOffsets(mapSizeX, mapSizeY);

            updateRadarSurface(mapSizeX, mapSizeY, scaleInfo);

            SDL_UpdateTexture(radarTexture.get(), nullptr, radarSurface->pixels, radarSurface->pitch);

            SDL_Rect dest = calcDrawingRect(radarTexture.get(), radarPosition.x, radarPosition.y);
            SDL_RenderCopy(renderer, radarTexture.get(), nullptr, &dest);

            SDL_Rect radarRect;
            const float viewLeftTiles = screenborder->getLeft() / static_cast<float>(TILESIZE);
            const float viewTopTiles = screenborder->getTop() / static_cast<float>(TILESIZE);
            const float viewWidthTiles = (screenborder->getRight() - screenborder->getLeft()) / static_cast<float>(TILESIZE);
            const float viewHeightTiles = (screenborder->getBottom() - screenborder->getTop()) / static_cast<float>(TILESIZE);

            radarRect.x = static_cast<int>(std::floor(viewLeftTiles * scaleInfo.scale)) + scaleInfo.offsetX;
            radarRect.y = static_cast<int>(std::floor(viewTopTiles * scaleInfo.scale)) + scaleInfo.offsetY;
            radarRect.w = std::max(1, static_cast<int>(std::ceil(viewWidthTiles * scaleInfo.scale)));
            radarRect.h = std::max(1, static_cast<int>(std::ceil(viewHeightTiles * scaleInfo.scale)));

            const int mapLeft = scaleInfo.offsetX;
            const int mapTop = scaleInfo.offsetY;
            const int mapRight = scaleInfo.offsetX + scaleInfo.scaledWidth;
            const int mapBottom = scaleInfo.offsetY + scaleInfo.scaledHeight;

            if(radarRect.x < mapLeft) {
                radarRect.w -= (mapLeft - radarRect.x);
                radarRect.x = mapLeft;
            }

            if(radarRect.y < mapTop) {
                radarRect.h -= (mapTop - radarRect.y);
                radarRect.y = mapTop;
            }

            radarRect.w = std::min(mapRight, radarRect.x + radarRect.w) - radarRect.x;
            radarRect.h = std::min(mapBottom, radarRect.y + radarRect.h) - radarRect.y;

            radarRect.w = std::max(0, radarRect.w);
            radarRect.h = std::max(0, radarRect.h);

            renderDrawRect( renderer,
                            radarPosition.x + radarRect.x,
                            radarPosition.y + radarRect.y,
                            radarPosition.x + (radarRect.x + radarRect.w),
                            radarPosition.y + (radarRect.y + radarRect.h),
                            COLOR_WHITE);

        } break;

        case RadarMode::AnimationRadarOff:
        case RadarMode::AnimationRadarOn: {
            SDL_Rect source = calcSpriteSourceRect( radarStaticAnimation,
                                                    animFrame % NUM_STATIC_ANIMATIONS_PER_ROW,
                                                    NUM_STATIC_ANIMATIONS_PER_ROW,
                                                    animFrame / NUM_STATIC_ANIMATIONS_PER_ROW,
                                                    (NUM_STATIC_FRAMES + NUM_STATIC_ANIMATIONS_PER_ROW - 1) / NUM_STATIC_ANIMATIONS_PER_ROW);
            SDL_Rect dest = calcSpriteDrawingRect(  radarStaticAnimation,
                                                    radarPosition.x,
                                                    radarPosition.y,
                                                    NUM_STATIC_ANIMATIONS_PER_ROW,
                                                    (NUM_STATIC_FRAMES + NUM_STATIC_ANIMATIONS_PER_ROW - 1) / NUM_STATIC_ANIMATIONS_PER_ROW);
            SDL_RenderCopy(renderer, radarStaticAnimation, &source, &dest);
        } break;
    }
}

void RadarView::update() {
    // An observer's minimap is not a faction's radar installation: it has no local house and
    // stays on regardless of any faction's radar or power. Keep this true on every update.
    // Tile::getRadarColor supplies the whole-map colours once the mode is on.
    if(currentGame != nullptr && currentGame->isObserving()) {
        if(currentRadarMode != RadarMode::RadarOn && currentRadarMode != RadarMode::AnimationRadarOn) {
            switchRadarMode(true);
        }
    } else if(pLocalHouse && pLocalHouse->hasRadarOn()) {
        if(currentRadarMode != RadarMode::RadarOn && currentRadarMode != RadarMode::AnimationRadarOn) {
            switchRadarMode(true);
        }
    } else {
        if(currentRadarMode != RadarMode::RadarOff && currentRadarMode != RadarMode::AnimationRadarOff) {
            switchRadarMode(false);
        }
    }

    switch(currentRadarMode) {
        case RadarMode::RadarOff: {

        } break;

        case RadarMode::RadarOn: {

        } break;

        case RadarMode::AnimationRadarOff: {
            if(animFrame >= NUM_STATIC_FRAMES-1) {
                currentRadarMode = RadarMode::RadarOff;
            } else {
                animCounter--;
                if(animCounter <= 0) {
                    animFrame++;
                    animCounter = NUM_STATIC_FRAME_TIME;
                }
            }
        } break;

        case RadarMode::AnimationRadarOn: {
            if(animFrame <= 0) {
                currentRadarMode = RadarMode::RadarOn;
            } else {
                animCounter--;
                if(animCounter <= 0) {
                    animFrame--;
                    animCounter = NUM_STATIC_FRAME_TIME;
                }
            }
        } break;
    }
}

void RadarView::switchRadarMode(bool bOn) {
    soundPlayer->playSound(Sound_RadarNoise);

    if(bOn == true) {
        if(pLocalHouse) soundPlayer->playVoice(RadarActivated,pLocalHouse->getHouseID());
        currentRadarMode = RadarMode::AnimationRadarOn;
    } else {
        if(pLocalHouse) soundPlayer->playVoice(RadarDeactivated,pLocalHouse->getHouseID());
        currentRadarMode = RadarMode::AnimationRadarOff;
    }
}

void RadarView::updateRadarSurface(int mapSizeX, int mapSizeY, const RadarScaleInfo& scaleInfo) {
    SDL_FillRect(radarSurface.get(), nullptr, COLOR_BLACK);

    sdl2::surface_lock lock{ radarSurface.get() };

    const int maxTileX = mapSizeX - 1;
    const int maxTileY = mapSizeY - 1;

    auto getTileColor = [&](int tileX, int tileY) {
        Tile* pTile = currentGameMap->getTile(tileX, tileY);

        /* Selecting the right color is handled in Tile::getRadarColor() */
        Uint32 color = pTile->getRadarColor(pLocalHouse, ((currentRadarMode == RadarMode::RadarOn) || (currentRadarMode == RadarMode::AnimationRadarOff)));
        return MapRGBA(radarSurface->format, color);
    };

    for(int pixelY = 0; pixelY < scaleInfo.scaledHeight; ++pixelY) {
        const int tileY = std::clamp(static_cast<int>(pixelY / scaleInfo.scale), 0, maxTileY);
        Uint32* p = reinterpret_cast<Uint32*>(reinterpret_cast<Uint8*>(radarSurface->pixels)
                            + (scaleInfo.offsetY + pixelY) * radarSurface->pitch)
                    + scaleInfo.offsetX;

        for(int pixelX = 0; pixelX < scaleInfo.scaledWidth; ++pixelX, ++p) {
            const int tileX = std::clamp(static_cast<int>(pixelX / scaleInfo.scale), 0, maxTileX);
            *p = getTileColor(tileX, tileY);
        }
    }

    // City Airplanes as white crosses, five pixels wide, over the tile colours (Stefan,
    // 2026-11-24: "planes now appear to be invisible. can't see it on screen"). At Game Speed 2
    // a plane crosses a 960x600 view in about a third of a second; on the radar it is in sight
    // for its whole flight. As a tile colour it was one pixel in its house's colour, like any
    // unit, and on a map wider than 128 tiles the radar skips columns and rows, so it was not
    // drawn at all for part of its path. Drawing only: nothing here is read by the game.
    const Uint32 planeColor = MapRGBA(radarSurface->format, COLOR_WHITE);
    for(const AmbientAirplane* plane : AmbientAirplane::all()) {
        if(!radarShowsCityPlane(plane)) continue;
        const int centreX = static_cast<int>(std::floor(plane->getRealX().toFloat() / TILESIZE * scaleInfo.scale));
        const int centreY = static_cast<int>(std::floor(plane->getRealY().toFloat() / TILESIZE * scaleInfo.scale));
        for(int arm = -2; arm <= 2; ++arm) {
            for(const auto [dx, dy] : {std::pair{arm, 0}, std::pair{0, arm}}) {
                const int x = centreX + dx, y = centreY + dy;
                if(x < 0 || y < 0 || x >= scaleInfo.scaledWidth || y >= scaleInfo.scaledHeight) continue;
                *(reinterpret_cast<Uint32*>(reinterpret_cast<Uint8*>(radarSurface->pixels)
                    + (scaleInfo.offsetY + y) * radarSurface->pitch) + scaleInfo.offsetX + x) = planeColor;
            }
        }
    }

    // Saboteurs as crosses (x), five pixels wide, in their owner's colour with a white centre, for
    // a spectator only (Stefan, 2026-11-27: watching, he could not see Saboteurs at all). As a tile
    // colour a Saboteur was one pixel, often skipped on a map wider than 128 tiles. Drawing only.
    if(currentGame->isObserving()) {
        const Uint32 centreColor = MapRGBA(radarSurface->format, COLOR_WHITE);
        for(const UnitBase* unit : unitList) {
            if(unit->getItemID() != Unit_Saboteur || !unit->isActive()) continue;
            const Uint32 color = MapRGBA(radarSurface->format,
                getHouseRadarColor(static_cast<HOUSETYPE>(unit->getOwner()->getHouseID())));
            const int centreX = static_cast<int>(std::floor(unit->getRealX().toFloat() / TILESIZE * scaleInfo.scale));
            const int centreY = static_cast<int>(std::floor(unit->getRealY().toFloat() / TILESIZE * scaleInfo.scale));
            for(int arm = -2; arm <= 2; ++arm) {
                for(const auto [dx, dy] : {std::pair{arm, arm}, std::pair{arm, -arm}}) {
                    const int x = centreX + dx, y = centreY + dy;
                    if(x < 0 || y < 0 || x >= scaleInfo.scaledWidth || y >= scaleInfo.scaledHeight) continue;
                    *(reinterpret_cast<Uint32*>(reinterpret_cast<Uint8*>(radarSurface->pixels)
                        + (scaleInfo.offsetY + y) * radarSurface->pitch) + scaleInfo.offsetX + x)
                        = arm == 0 ? centreColor : color;
                }
            }
        }
    }
}

bool RadarView::radarShowsCityPlane(const AmbientAirplane* plane) const {
    // The same plane the radar's tile colours would show (Tile::getRadarColor): every plane to an
    // observer, a house's own team's planes always, others only with the radar on, where its
    // team sees them and the ground below is explored and not fogged.
    if(currentGame->isObserving()) return true;
    if(pLocalHouse == nullptr) return false;
    const int team = pLocalHouse->getTeamID();
    if(plane->getOwner()->getTeamID() == team) return true;
    if(currentRadarMode != RadarMode::RadarOn && currentRadarMode != RadarMode::AnimationRadarOff) return false;
    const Coord at = plane->getLocation();
    if(!currentGameMap->tileExists(at)) return false;
    const Tile* tile = currentGameMap->getTile(at);
    return plane->isVisible(team) && (debug || (tile->isExploredByTeam(team) && !tile->isFoggedByTeam(team)));
}
