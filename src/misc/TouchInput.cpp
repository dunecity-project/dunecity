#include <misc/TouchInput.h>
#include <misc/MobilePlatform.h>

#include <GameInitSettings.h>
#include <globals.h>

#include <algorithm>
#include <cmath>

namespace {

struct TouchState {
    bool primaryActive = false;
    SDL_FingerID primaryFinger = 0;
    bool secondaryActive = false;
    SDL_FingerID secondaryFinger = 0;
    bool hasMousePosition = false;
    int lastX = 0;
    int lastY = 0;
};

TouchState state;

Uint32 getGameWindowId() {
    return window != nullptr ? SDL_GetWindowID(window) : 0;
}

void touchToScreen(float x, float y, int& screenX, int& screenY) {
    screenX = std::clamp(static_cast<int>(std::lround(x * settings.video.width)), 0, settings.video.width - 1);
    screenY = std::clamp(static_cast<int>(std::lround(y * settings.video.height)), 0, settings.video.height - 1);
}

void updateMousePosition(int x, int y) {
    state.hasMousePosition = true;
    state.lastX = std::clamp(x, 0, settings.video.width - 1);
    state.lastY = std::clamp(y, 0, settings.video.height - 1);
}

void logicalToWindowCoordinates(int logicalX, int logicalY, int& windowX, int& windowY) {
    logicalX = std::clamp(logicalX, 0, settings.video.width - 1);
    logicalY = std::clamp(logicalY, 0, settings.video.height - 1);

    if(renderer != nullptr) {
        SDL_RenderLogicalToWindow(renderer, static_cast<float>(logicalX), static_cast<float>(logicalY), &windowX, &windowY);
    } else {
        windowX = logicalX;
        windowY = logicalY;
    }
}

void pushMouseMotion(int logicalX, int logicalY) {
    int x = 0;
    int y = 0;
    logicalToWindowCoordinates(logicalX, logicalY, x, y);

    SDL_Event motion{};
    motion.type = SDL_MOUSEMOTION;
    motion.motion.type = SDL_MOUSEMOTION;
    motion.motion.timestamp = SDL_GetTicks();
    motion.motion.windowID = getGameWindowId();
    motion.motion.which = SDL_TOUCH_MOUSEID;
    motion.motion.state = 0;
    motion.motion.x = x;
    motion.motion.y = y;
    motion.motion.xrel = 0;
    motion.motion.yrel = 0;
    SDL_PushEvent(&motion);
}

void pushMouseButton(Uint8 button, bool pressed, int logicalX, int logicalY) {
    int x = 0;
    int y = 0;
    logicalToWindowCoordinates(logicalX, logicalY, x, y);

    SDL_Event mouse{};
    mouse.type = pressed ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    mouse.button.type = mouse.type;
    mouse.button.timestamp = SDL_GetTicks();
    mouse.button.windowID = getGameWindowId();
    mouse.button.which = SDL_TOUCH_MOUSEID;
    mouse.button.button = button;
    mouse.button.state = pressed ? SDL_PRESSED : SDL_RELEASED;
    mouse.button.clicks = 1;
    mouse.button.x = x;
    mouse.button.y = y;
    SDL_PushEvent(&mouse);
}

void pushMouseClick(Uint8 button, int x, int y) {
    pushMouseMotion(x, y);
    pushMouseButton(button, true, x, y);
    pushMouseButton(button, false, x, y);
}

void resetTouchFingers() {
    state = TouchState{};
}

void updateFromFingerIfNeeded(const SDL_TouchFingerEvent& finger) {
    if(state.hasMousePosition) {
        return;
    }

    int x = 0;
    int y = 0;
    touchToScreen(finger.x, finger.y, x, y);
    updateMousePosition(x, y);
}

}

namespace TouchInput {

bool translateTouchEvent(SDL_Event& event) {
#ifdef __ANDROID__
    if((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) &&
       (event.key.keysym.sym == SDLK_AC_BACK || event.key.keysym.scancode == SDL_SCANCODE_AC_BACK)) {
        event.key.keysym.sym = SDLK_ESCAPE;
        event.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
        return false;
    }
#endif

    if(event.type == SDL_MOUSEMOTION && event.motion.which == SDL_TOUCH_MOUSEID) {
        updateMousePosition(event.motion.x, event.motion.y);
        return false;
    }

    if((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) &&
       event.button.which == SDL_TOUCH_MOUSEID) {
        updateMousePosition(event.button.x, event.button.y);
        return false;
    }

    if(event.type != SDL_FINGERDOWN && event.type != SDL_FINGERMOTION && event.type != SDL_FINGERUP) {
        return false;
    }

#ifdef DUNECITY_MOBILE
    if(event.type == SDL_FINGERDOWN) {
        if(!state.primaryActive) {
            state.primaryActive = true;
            state.primaryFinger = event.tfinger.fingerId;
            updateFromFingerIfNeeded(event.tfinger);
        } else if(event.tfinger.fingerId != state.primaryFinger) {
            state.secondaryActive = true;
            state.secondaryFinger = event.tfinger.fingerId;
            updateFromFingerIfNeeded(event.tfinger);
            pushMouseClick(SDL_BUTTON_RIGHT, state.lastX, state.lastY);
        }
        return true;
    }

    if(event.type == SDL_FINGERMOTION && state.primaryActive && event.tfinger.fingerId == state.primaryFinger) {
        updateFromFingerIfNeeded(event.tfinger);
        return true;
    }

    if(event.type == SDL_FINGERUP && state.primaryActive && event.tfinger.fingerId == state.primaryFinger) {
        resetTouchFingers();
        return true;
    }

    if(event.type == SDL_FINGERUP && state.secondaryActive && event.tfinger.fingerId == state.secondaryFinger) {
        state.secondaryActive = false;
        state.secondaryFinger = 0;
        return true;
    }

    return true;
#else
    return false;
#endif
}

}
