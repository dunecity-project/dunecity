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

#ifndef CURSORMANAGER_H
#define CURSORMANAGER_H

#include <SDL.h>
#include <vector>

class CursorManager {
private:
    bool initialized;
    
public:
    CursorManager();
    ~CursorManager();
    
    void initialize();
    void cleanup();
    void setCursorMode(int mode);
    bool canSetCursorMode(int mode, const std::vector<Uint32>& selectedObjects);
    bool isInitialized() const { return initialized; }
};

/** Apply the configured cursor visibility mode immediately. */
void applyCursorVisibilitySetting();
void releaseCursorResources();

/**
 * Update automatic cursor visibility from an input event. On Android,
 * physical mouse input shows the cursor while touch input hides it.
 */
void updateCursorVisibilityForInput(const SDL_Event& event);

/** True while the last pointer input was a finger, so nothing hovers (no tooltips). */
bool pointerIsTouch();

/** Present the frame and update the single SDL cursor on desktop and web. */
void presentWithCursor(int mode = 0, bool contextual = false);

#endif // CURSORMANAGER_H
