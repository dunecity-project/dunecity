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

#include <CursorManager.h>
#include <FileClasses/GFXManager.h>
#include <globals.h>
#include <Game.h>
#include <ObjectManager.h>
#include <units/UnitBase.h>
#include <structures/StructureBase.h>
#include <structures/Palace.h>
#include <misc/CursorAppearance.h>
#include <misc/MobilePlatform.h>
#include <map>
#include <memory>

#include <algorithm>

namespace {

#ifdef DUNECITY_MOBILE
bool autoCursorHasPhysicalMouse = false;
#else
bool autoCursorHasPhysicalMouse = true;
#endif

bool shouldShowCursor() {
    if (settings.video.cursorVisibility == 1) {
        return false;
    }
    if (settings.video.cursorVisibility == 2) {
        return true;
    }
    return autoCursorHasPhysicalMouse;
}

// SDL owns the only pointer on every platform: an OS cursor on desktop and
// a CSS cursor in the browser. No game-frame pointer is drawn underneath it.
float getEffectiveCursorScale() {
    const int configured = settings.video.cursorScale;
    return configured >= 1 && configured <= 4 ? static_cast<float>(configured) : 1.5f;
}

struct CursorDeleter { void operator()(SDL_Cursor* cursor) const { SDL_FreeCursor(cursor); } };
using CursorPtr = std::unique_ptr<SDL_Cursor, CursorDeleter>;
// The main initialization loop destroys this cache before shutting down SDL.
std::map<std::pair<int,int>, CursorPtr> cursors;


}

void applyCursorVisibilitySetting() {
    SDL_ShowCursor(shouldShowCursor() ? SDL_ENABLE : SDL_DISABLE);
}

bool pointerIsTouch() {
    return !autoCursorHasPhysicalMouse;
}

void presentWithCursor(int mode, bool contextual) {
    if(pGFXManager && shouldShowCursor()) {
        using Action = CursorAppearance::Action;
        Action action = contextual && mode==Game::CursorMode_Normal && currentGame
            ? currentGame->getHoverCursorAction() : Action::Pointer;
        switch(mode) {
            case Game::CursorMode_Move: action = Action::Move; break;
            case Game::CursorMode_Attack: action = Action::Attack; break;
            case Game::CursorMode_Patrol: action = Action::Patrol; break;
            case Game::CursorMode_Heal: action = Action::Heal; break;
            case Game::CursorMode_Capture: action = Action::Capture; break;
            case Game::CursorMode_CarryallDrop: action = Action::Drop; break;
            default: break;
        }
        const float scale = getEffectiveCursorScale();
        auto& cursor = cursors[{static_cast<int>(action), static_cast<int>(scale * 2)}];
        if(!cursor) {
            sdl2::surface_ptr surface{CursorAppearance::create(action, scale)};
            const auto hotspot = CursorAppearance::hotspot(action, scale);
            if(surface) cursor.reset(SDL_CreateColorCursor(surface.get(), hotspot.x, hotspot.y));
        }
        SDL_Cursor* desired = cursor ? cursor.get() : SDL_GetDefaultCursor();
        if(desired && SDL_GetCursor() != desired) SDL_SetCursor(desired);
    }
    applyCursorVisibilitySetting();
    SDL_RenderPresent(renderer);
}

void updateCursorVisibilityForInput(const SDL_Event& event) {
#ifdef DUNECITY_MOBILE
    // Input identity also controls touch gestures, independently of whether
    // the user forces the cursor visible or hidden.
    bool visibilityChanged = false;
    switch (event.type) {
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
            if (autoCursorHasPhysicalMouse) {
                autoCursorHasPhysicalMouse = false;
                visibilityChanged = true;
            }
            break;

        case SDL_MOUSEMOTION:
            if (event.motion.which != SDL_TOUCH_MOUSEID && !autoCursorHasPhysicalMouse) {
                autoCursorHasPhysicalMouse = true;
                visibilityChanged = true;
            }
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (event.button.which != SDL_TOUCH_MOUSEID && !autoCursorHasPhysicalMouse) {
                autoCursorHasPhysicalMouse = true;
                visibilityChanged = true;
            }
            break;

        case SDL_MOUSEWHEEL:
            if (event.wheel.which != SDL_TOUCH_MOUSEID && !autoCursorHasPhysicalMouse) {
                autoCursorHasPhysicalMouse = true;
                visibilityChanged = true;
            }
            break;

        default:
            break;
    }

    if (visibilityChanged || settings.video.cursorVisibility != 0) {
        applyCursorVisibilitySetting();
    }
#else
    if(event.type == SDL_WINDOWEVENT || event.type == SDL_MOUSEMOTION) applyCursorVisibilitySetting();
#endif
}

CursorManager::CursorManager() : initialized(false) {}
CursorManager::~CursorManager() = default;
void CursorManager::initialize() { initialized = true; applyCursorVisibilitySetting(); }
void CursorManager::cleanup() { initialized = false; }

void releaseCursorResources() {
    if(SDL_WasInit(SDL_INIT_VIDEO)) SDL_SetCursor(SDL_GetDefaultCursor());
    cursors.clear();
}
void CursorManager::setCursorMode(int /*mode*/) { applyCursorVisibilitySetting(); }

bool CursorManager::canSetCursorMode(int mode, const std::vector<Uint32>& selectedObjects) {
    if (selectedObjects.empty()) {
        return mode == Game::CursorMode_Normal || mode == Game::CursorMode_Placing;
    }

    for (Uint32 objectID : selectedObjects) {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        if (!pObject) {
            continue;
        }

        switch (mode) {
            case Game::CursorMode_Move:
            case Game::CursorMode_Patrol:
                if (pObject->isAUnit() && (pObject->getOwner() == pLocalHouse) && pObject->isRespondable()) {
                    return true;
                }
                break;
                        case Game::CursorMode_Heal:
                if (pObject->isAUnit() && (pObject->getOwner() == pLocalHouse)
                        && pObject->isRespondable() && pObject->canHeal()) {
                    return true;
                }
                break;
case Game::CursorMode_Attack:
                if (pObject->isAUnit() && (pObject->getOwner() == pLocalHouse) && pObject->isRespondable() && pObject->canAttack()) {
                    return true;
                } else if ((pObject->getItemID() == Structure_Palace) && 
                          ((pObject->getOwner()->getHouseID() == HOUSE_HARKONNEN) || (pObject->getOwner()->getHouseID() == HOUSE_SARDAUKAR))) {
                    Palace* pPalace = static_cast<Palace*>(pObject);
                    if (pPalace->isSpecialWeaponReady()) {
                        return true;
                    }
                }
                break;
            case Game::CursorMode_Capture:
                if (pObject->isAUnit() && (pObject->getOwner() == pLocalHouse) && pObject->isRespondable() && pObject->canAttack() && pObject->isInfantry()) {
                    return true;
                }
                break;
            case Game::CursorMode_CarryallDrop:
                if (pObject->isAUnit() && (pObject->getOwner() == pLocalHouse) && pObject->isRespondable()) {
                    return true;
                }
                break;
            case Game::CursorMode_Normal:
            case Game::CursorMode_Placing:
                return true;
        }
    }

    return false;
}
