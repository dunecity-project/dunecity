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

#include <Menu/MenuBase.h>
#include <Network/NetworkManager.h>

#include <FileClasses/LoadSavePNG.h>
#include <misc/string_util.h>
#include <misc/FileSystem.h>
#include <misc/FrameYield.h>
#include <misc/draw_util.h>
#include <misc/DiscordManager.h>
#include <misc/TouchInput.h>
#include <misc/WebRuntime.h>
#include <CursorManager.h>

#include <globals.h>

#include <sand.h>
#include <main.h>


MenuBase::MenuBase() : Window(0,0,0,0) {
    bAllowQuiting = true;
    retVal = MENU_QUIT_DEFAULT;
    bClearScreen = true;
    quiting = false;
}

MenuBase::~MenuBase() = default;

void MenuBase::quit(int returnVal) {
    retVal = returnVal;
    quiting = true;
}

int MenuBase::showMenu() {
    SDL_Event   event;
    // valgrind reports errors in SDL_PollEvent if event is not initialized
    memset(&event, 0, sizeof(event));

    quiting = false;

    while(!quiting) {
        int frameStart = SDL_GetTicks();

#ifdef __ANDROID__
        // Android may reset pointer visibility when SDL views or windows
        // transition. Keep Auto/Hidden/Visible effective in every menu,
        // including Mentat and briefing screens.
        applyCursorVisibilitySetting();
#endif

        update();

        // Update Discord Rich Presence callbacks
        DiscordManager::instance().update();

        if(pNetworkManager != nullptr) {
            pNetworkManager->update();
        }

        if(quiting) {
            return retVal;
        }

        if(bClearScreen == true) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
        }
        draw();
        presentWithCursor();

        while(SDL_PollEvent(&event)) {
            updateCursorVisibilityForInput(event);
            if(TouchInput::translateTouchEvent(event)) {
                continue;
            }
            //check the events
            if(doInput(event) == false) {
                break;
            }
        }

        // VSync is controlled via SDL_HINT_RENDER_VSYNC in main.cpp
        // No software frame limiting needed in menus

        // Browser build: hand control back to the event loop once per frame so
        // WebRTC/signaling callbacks can run while this menu blocks. There is
        // no blocking vsync on the web, so also pace to ~60 FPS — an unpaced
        // menu redraws flat-out and starves the compositor (screenshots stall
        // for tens of seconds) while burning a full core.
#ifdef __EMSCRIPTEN__
        constexpr int kWebMenuFrameBudgetMs = 16;
        const int menuFrameMs = SDL_GetTicks() - frameStart;
        // A callback may run a nested menu or an entire match. Never turn
        // that elapsed time into a matching pause when it returns (minutes
        // of a blank screen after Quit). Cap slow-frame pacing at 50 ms.
        yieldFrameToBrowser(menuFrameMs < kWebMenuFrameBudgetMs ? kWebMenuFrameBudgetMs - menuFrameMs
                                                                : std::min(menuFrameMs, 50));
#endif
    }

    return retVal;
}

void MenuBase::draw() {
    SDL_Rect clipRect { getPosition().x, getPosition().y, getSize().x, getSize().y };
    SDL_RenderSetClipRect(renderer, &clipRect);

    Window::draw();

    drawSpecificStuff();

    Window::drawOverlay();

    SDL_RenderSetClipRect(renderer, nullptr);
}

void MenuBase::drawSpecificStuff() {
}

bool MenuBase::doInput(SDL_Event &event) {
    switch (event.type) {
        case (SDL_KEYDOWN): {
            // Look for a keypress
            switch(event.key.keysym.sym) {

                case SDLK_ESCAPE: {
                    if((pChildWindow == nullptr) && (bAllowQuiting == true)) {
                        quit();
                    }
                } break;

                case SDLK_RETURN: {
                    if(SDL_GetModState() & KMOD_ALT) {
                        toogleFullscreen();
                    }
                } break;

                case SDLK_p: {
                    if(SDL_GetModState() & KMOD_CTRL) {
                        // fall through to SDLK_PRINT
                    } else {
                        break;  // do not fall through
                    }
                } // fall through

                case SDLK_PRINTSCREEN:
                case SDLK_SYSREQ: {

                    std::string filename;
                    if(saveScreenshot(renderer,filename)) SDL_Log("Screenshot saved: %s",filename.c_str());
                    else SDL_Log("Could not save screenshot: %s",SDL_GetError());
                } break;

                case SDLK_TAB: {
                    if(SDL_GetModState() & KMOD_ALT) {
                        SDL_MinimizeWindow(window);
                    }
                } break;

                default: {
                } break;
            }
        } break;

        case SDL_MOUSEMOTION: {
            SDL_MouseMotionEvent* mouse = &event.motion;
            drawnMouseX = std::max(0, std::min(mouse->x, settings.video.width-1));
            drawnMouseY = std::max(0, std::min(mouse->y, settings.video.height-1));
        } break;

        case SDL_QUIT: {
            if((pChildWindow == nullptr) && (bAllowQuiting == true)) {
                quit();
            }
        } break;

        default: {
        } break;
    }

    handleInput(event);

    return !quiting;
}
