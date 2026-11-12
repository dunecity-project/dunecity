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

#include <ScreenBorder.h>

#include <globals.h>

#include <algorithm>


ScreenBorder::~ScreenBorder() {
    if(mousePanning) {
        mousePanning = false;
        SDL_CaptureMouse(SDL_FALSE);
        SDL_ShowCursor(cursorBeforePan);
    }
}

bool ScreenBorder::handleMiddlePan(const SDL_Event& event, bool enabled) {
    const bool release = event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_MIDDLE;
    const bool lostFocus = event.type == SDL_WINDOWEVENT
        && (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST || event.window.event == SDL_WINDOWEVENT_LEAVE);
    if(mousePanning && (!enabled || release || lostFocus)) {
        mousePanning = false;
        SDL_CaptureMouse(SDL_FALSE);
        SDL_ShowCursor(cursorBeforePan);
        return release;
    }
    if(!enabled) return false;
    if(event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_MIDDLE
       && event.button.which != SDL_TOUCH_MOUSEID
       && isScreenCoordInsideMap(event.button.x, event.button.y)) {
        mousePanning = true;
        panAnchor = Coord(screen2worldX(event.button.x), screen2worldY(event.button.y));
        SDL_CaptureMouse(SDL_TRUE);
        cursorBeforePan = SDL_ShowCursor(SDL_QUERY);
        SDL_ShowCursor(SDL_DISABLE);
        return true;
    }
    if(mousePanning && event.type == SDL_MOUSEMOTION) {
        const Coord underPointer(screen2worldX(event.motion.x), screen2worldY(event.motion.y));
        const Coord requested = getCurrentCenter() + panAnchor - underPointer;
        setNewScreenCenter(requested);
        // Re-anchor at map bounds so reversing direction immediately moves the view.
        if(getCurrentCenter() != requested)
            panAnchor = Coord(screen2worldX(event.motion.x), screen2worldY(event.motion.y));
        return true;
    }
    return false;
}

int ScreenBorder::mouseWheelZoom(const SDL_MouseWheelEvent& wheel) {
    float delta = static_cast<float>(wheel.y);
#if SDL_VERSION_ATLEAST(2, 0, 18)
    if(wheel.preciseY != 0) delta = wheel.preciseY;
#endif
    if(wheel.direction == SDL_MOUSEWHEEL_FLIPPED) delta = -delta;
    if(delta == 0) return 0;
    const Uint32 now = wheel.timestamp ? wheel.timestamp : SDL_GetTicks();
    if(now - wheelLastEvent > 250) resetMouseWheelZoom();
    wheelLastEvent = now;
    // Discrete wheels stay immediate, including rapidly reversed notches. Once a
    // fractional delta identifies a trackpad gesture, keep its inertia rate-limited.
    if(delta != std::trunc(delta)) wheelSmooth = true;
    if(!wheelSmooth) return delta > 0 ? 1 : -1;
    if((wheelRemainder > 0) != (delta > 0)) wheelRemainder = 0;
    wheelRemainder += delta;
    if(std::abs(wheelRemainder) < 1 || (wheelLastStep && now - wheelLastStep < 150)) return 0;
    wheelLastStep = now;
    const int step = wheelRemainder > 0 ? 1 : -1;
    wheelRemainder = 0; // Do not replay a swipe's excess momentum as queued zooms.
    return step;
}
void ScreenBorder::setNewScreenCenter(const Coord& newPosition) {
    Coord currentBorderSize = bottomRightCorner - topLeftCorner;

    topLeftCorner = newPosition - currentBorderSize/2;
    bottomRightCorner = newPosition + currentBorderSize/2;

    int worldSizeX = mapSizeX * TILESIZE;
    int worldSizeY = mapSizeY * TILESIZE;

    if((topLeftCorner.x < 0) && (bottomRightCorner.x >= worldSizeX)) {
        // screen is wider than map
        topLeftCorner.x = 0;
        bottomRightCorner.x = worldSizeX;
    } else if(topLeftCorner.x < 0) {
        // leaving the map at the left border
        bottomRightCorner.x -= topLeftCorner.x;
        topLeftCorner.x = 0;
    } else if(bottomRightCorner.x >= worldSizeX) {
        // leaving the map at the right border
        topLeftCorner.x -= (bottomRightCorner.x - worldSizeX);
        bottomRightCorner.x = worldSizeX;
    }

    if((topLeftCorner.y < 0) && (bottomRightCorner.y >= worldSizeY)) {
        // screen is higher than map
        topLeftCorner.y = 0;
        bottomRightCorner.y = worldSizeY;
    } else if(topLeftCorner.y < 0) {
        // leaving the map at the top border
        bottomRightCorner.y -= topLeftCorner.y;
        topLeftCorner.y = 0;
    } else if(bottomRightCorner.y >= worldSizeY) {
        // leaving the map at the bottom border
        topLeftCorner.y -= (bottomRightCorner.y - worldSizeY);
        bottomRightCorner.y = worldSizeY;
    }
}

bool ScreenBorder::scrollLeft() {
    if(topLeftCorner.x > 0) {
        int scrollAmount = std::min(settings.general.scrollSpeed, topLeftCorner.x);
        topLeftCorner.x -= scrollAmount;
        bottomRightCorner.x -= scrollAmount;
        return true;
    } else {
        return false;
    }
}

bool ScreenBorder::scrollRight() {
    if(bottomRightCorner.x < mapSizeX*TILESIZE-1) {
        int scrollAmount = std::min(settings.general.scrollSpeed,mapSizeX*TILESIZE-1-bottomRightCorner.x);
        topLeftCorner.x += scrollAmount;
        bottomRightCorner.x += scrollAmount;
        return true;
    } else {
        return false;
    }
}

bool ScreenBorder::scrollUp() {
    if(topLeftCorner.y > 0) {
        int scrollAmount = std::min(settings.general.scrollSpeed, topLeftCorner.y);
        topLeftCorner.y -= scrollAmount;
        bottomRightCorner.y -= scrollAmount;
        return true;
    } else {
        return false;
    }
}

bool ScreenBorder::scrollDown() {
    if(bottomRightCorner.y < mapSizeY*TILESIZE-1) {
        int scrollAmount = std::min(settings.general.scrollSpeed,mapSizeY*TILESIZE-1-bottomRightCorner.y);
        topLeftCorner.y += scrollAmount;
        bottomRightCorner.y += scrollAmount;
        return true;
    } else {
        return false;
    }
}

void ScreenBorder::adjustScreenBorderToMapsize(int newMapSizeX, int newMapSizeY) {
    mapSizeX = newMapSizeX;
    mapSizeY = newMapSizeY;

    int worldSizeX = mapSizeX * TILESIZE;
    int worldSizeY = mapSizeY * TILESIZE;

    if(presentationScale != 1) {
        const int viewWidth = std::max(1, zoomedWorld2world(gameBoardRect.w) / presentationScale);
        const int viewHeight = std::max(1, zoomedWorld2world(gameBoardRect.h) / presentationScale);
        topLeftCorner = Coord(0, 0);
        bottomRightCorner = Coord(std::min(worldSizeX, viewWidth), std::min(worldSizeY, viewHeight));
        const int pixelWidth = std::min(gameBoardRect.w, world2zoomedWorld(worldSizeX) * presentationScale);
        const int pixelHeight = std::min(gameBoardRect.h, world2zoomedWorld(worldSizeY) * presentationScale);
        presentationRect = {gameBoardRect.x + (gameBoardRect.w - pixelWidth) / 2,
                            gameBoardRect.y + (gameBoardRect.h - pixelHeight) / 2,
                            pixelWidth, pixelHeight};
        return;
    }

    if(worldSizeX >= zoomedWorld2world(gameBoardRect.w)) {
        topLeftCorner.x = 0;
        bottomRightCorner.x = zoomedWorld2world(gameBoardRect.w);
        topLeftCornerOnScreen.x = zoomedWorld2world(gameBoardRect.x);
        bottomRightCornerOnScreen.x = zoomedWorld2world(gameBoardRect.x + gameBoardRect.w);
    } else {
        topLeftCorner.x = 0;
        bottomRightCorner.x = worldSizeX;
        topLeftCornerOnScreen.x = zoomedWorld2world(gameBoardRect.x) + (zoomedWorld2world(gameBoardRect.w) - worldSizeX) / 2;
        bottomRightCornerOnScreen.x = zoomedWorld2world(gameBoardRect.x) + (zoomedWorld2world(gameBoardRect.w) - worldSizeX) / 2 + worldSizeX;
    }

    if(worldSizeY >= zoomedWorld2world(gameBoardRect.h)) {
        topLeftCorner.y = 0;
        bottomRightCorner.y = zoomedWorld2world(gameBoardRect.h);
        topLeftCornerOnScreen.y = zoomedWorld2world(gameBoardRect.y);
        bottomRightCornerOnScreen.y = zoomedWorld2world(gameBoardRect.y + gameBoardRect.h);
    } else {
        topLeftCorner.y = 0;
        bottomRightCorner.y = worldSizeY;
        topLeftCornerOnScreen.y = zoomedWorld2world(gameBoardRect.y) + (zoomedWorld2world(gameBoardRect.h) - worldSizeY) / 2;
        bottomRightCornerOnScreen.y = zoomedWorld2world(gameBoardRect.y) + (zoomedWorld2world(gameBoardRect.h) - worldSizeY) / 2 + worldSizeY;
    }
}

void ScreenBorder::setPresentationScale(int scale) {
    scale = scale == 3 ? 3 : 1;
    if(presentationScale == scale) return;
    const Coord centre = getCurrentCenter() - shakingOffset;
    presentationScale = scale;
    adjustScreenBorderToMapsize(mapSizeX, mapSizeY);
    setNewScreenCenter(centre);
}
