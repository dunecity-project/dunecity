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

#include <CutScenes/CutScene.h>

#include <FileClasses/Palfile.h>
#include <FileClasses/FileManager.h>
#include <FileClasses/music/MusicPlayer.h>
#include <misc/SDL2pp.h>
#include <misc/FrameYield.h>
#include <CursorManager.h>

#include <globals.h>
#include <CursorManager.h>
#include <misc/MobilePlatform.h>
#include <sand.h>

CutScene::CutScene()
{
    quiting = false;
}

CutScene::~CutScene()
{
    // Fixes some flickering
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    presentWithCursor();
}

void CutScene::run()
{
    SDL_Event event;

    while (!quiting)
    {
        const int frameStart = SDL_GetTicks();

#ifdef DUNECITY_MOBILE
        applyCursorVisibilitySetting();
#endif

        const int nextFrameTime = draw();

        while(SDL_PollEvent(&event)) {
            updateCursorVisibilityForInput(event);

            //check the events
            switch (event.type)
            {
                case (SDL_KEYDOWN): // Look for a keypress
                {
                    if((event.key.keysym.sym == SDLK_SPACE) || (event.key.keysym.sym == SDLK_ESCAPE)) {
                        // Fixes some flickering
                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                        SDL_RenderClear(renderer);
                        presentWithCursor();
                        quiting = true;
                    }
                    break;
                }
#ifdef DUNECITY_IOS
                // iOS has no Back key to stand in for Escape, so a tap skips.
                case (SDL_MOUSEBUTTONUP): {
                    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                    SDL_RenderClear(renderer);
                    presentWithCursor();
                    quiting = true;
                    break;
                }
#endif
                default:
                    break;
            }
        }

        const int frameTime = SDL_GetTicks() - frameStart;
#ifdef __EMSCRIPTEN__
        // Browser build: ALWAYS yield, even when this frame overran its
        // budget. Yielding only when frameTime < nextFrameTime meant a
        // renderer slower than the scene's frame time (SwiftShader) never
        // slept at all: the cutscene loop spun with the JS thread wedged —
        // no input, no signaling/WebRTC callbacks, black canvas. When the
        // frame finished early, sleep the remaining budget so intro pacing
        // still matches native. On overrun, pace proportionally (yield at
        // least one frame-time): a 1ms yield let two contending clients
        // redraw back-to-back and starve the whole browser pipeline —
        // measured 2026-09-09: two-browser reachMainMenu took 335-354s
        // while a single browser reached the menu in 24s.
        yieldFrameToBrowser(frameTime < nextFrameTime ? nextFrameTime - frameTime : frameTime);
#else
        if(frameTime < nextFrameTime) {
            SDL_Delay(nextFrameTime - frameTime);
        }
#endif
    }
}

void CutScene::startNewScene() {
    scenes.push(std::make_unique<Scene>());
}

void CutScene::addVideoEvent(std::unique_ptr<VideoEvent> newVideoEvent)
{
    if(scenes.empty()) {
        scenes.push(std::make_unique<Scene>());
    }

    scenes.back()->addVideoEvent(std::move(newVideoEvent));
}

void CutScene::addTextEvent(std::unique_ptr<TextEvent> newTextEvent)
{
    if(scenes.empty()) {
        scenes.push(std::make_unique<Scene>());
    }

    scenes.back()->addTextEvent(std::move(newTextEvent));
}

void CutScene::addTrigger(std::unique_ptr<CutSceneTrigger> newTrigger)
{
    if(scenes.empty()) {
        scenes.push(std::make_unique<Scene>());
    }

    scenes.back()->addTrigger(std::move(newTrigger));
}

int CutScene::draw()
{
    int nextFrameTime = 0;

    while(scenes.empty() == false) {
        if(scenes.front()->isFinished() == true) {
            scenes.pop();
            continue;
        } else {
            nextFrameTime = scenes.front()->draw();
            break;
        }
    }

    if(scenes.empty() == true && !musicPlayer->isMusicPlaying()) {
        quit();
    }

    return nextFrameTime;
}

std::unique_ptr<Wsafile> CutScene::create_wsafile(const char* name1)
{
    return std::make_unique<Wsafile>(pFileManager->openFile(name1).get());
}

std::unique_ptr<Wsafile> CutScene::create_wsafile(const char* name1, const char* name2)
{
    return std::make_unique<Wsafile>(pFileManager->openFile(name1).get(), pFileManager->openFile(name2).get());
}

std::unique_ptr<Wsafile> CutScene::create_wsafile(const char* name1, const char* name2, const char* name3)
{
    return std::make_unique<Wsafile>(pFileManager->openFile(name1).get(), pFileManager->openFile(name2).get(), pFileManager->openFile(name3).get());
}
