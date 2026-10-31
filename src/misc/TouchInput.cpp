#include <misc/TouchInput.h>
#include <misc/MobilePlatform.h>
#include <misc/TouchGestures.h>

#include <globals.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

Uint32 mapGestureEventType = 0;     ///< SDL user event type, registered with the first gesture

void unpackPair(const void* packed, int& a, int& b) {
    const auto value = static_cast<uint32_t>(reinterpret_cast<intptr_t>(packed));
    a = static_cast<int16_t>(value >> 16);
    b = static_cast<int16_t>(value & 0xFFFF);
}

#ifdef DUNECITY_MOBILE

intptr_t packPair(int a, int b) {
    return static_cast<intptr_t>((static_cast<uint32_t>(static_cast<uint16_t>(a)) << 16) | static_cast<uint16_t>(b));
}

TouchGestures gestures;

Uint32 getGameWindowId() {
    return window != nullptr ? SDL_GetWindowID(window) : 0;
}

void touchToScreen(float x, float y, int& screenX, int& screenY) {
    screenX = std::clamp(static_cast<int>(std::lround(x * settings.video.width)), 0, settings.video.width - 1);
    screenY = std::clamp(static_cast<int>(std::lround(y * settings.video.height)), 0, settings.video.height - 1);
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

void pushMapGesture(TouchInput::MapGestureKind kind, const TouchGestures::Action& action) {
    if(mapGestureEventType == 0) {
        const Uint32 type = SDL_RegisterEvents(1);
        if(type == static_cast<Uint32>(-1)) {
            return;
        }
        mapGestureEventType = type;
    }

    SDL_Event gesture{};
    gesture.type = mapGestureEventType;
    gesture.user.type = mapGestureEventType;
    gesture.user.timestamp = SDL_GetTicks();
    gesture.user.windowID = getGameWindowId();
    gesture.user.code = static_cast<Sint32>(kind);
    gesture.user.data1 = reinterpret_cast<void*>(packPair(action.x, action.y));
    gesture.user.data2 = reinterpret_cast<void*>(packPair(action.dx, action.dy));
    SDL_PushEvent(&gesture);
}

void pushActions(const TouchGestures::Actions& actions) {
    using Kind = TouchGestures::Kind;
    for(const auto& action : actions) {
        switch(action.kind) {
            case Kind::Motion:      pushMouseMotion(action.x, action.y); break;
            case Kind::LeftDown:    pushMouseButton(SDL_BUTTON_LEFT, true, action.x, action.y); break;
            case Kind::LeftUp:      pushMouseButton(SDL_BUTTON_LEFT, false, action.x, action.y); break;
            case Kind::RightClick: {
                pushMouseButton(SDL_BUTTON_RIGHT, true, action.x, action.y);
                pushMouseButton(SDL_BUTTON_RIGHT, false, action.x, action.y);
            } break;
            case Kind::Pan:         pushMapGesture(TouchInput::MapGestureKind::Pan, action); break;
            case Kind::ZoomIn:      pushMapGesture(TouchInput::MapGestureKind::ZoomIn, action); break;
            case Kind::ZoomOut:     pushMapGesture(TouchInput::MapGestureKind::ZoomOut, action); break;
        }
    }
}

#endif

}

namespace TouchInput {

bool translateTouchEvent(SDL_Event& event, const SDL_Rect* mapArea, const SDL_Rect* mapControls) {
#ifdef __ANDROID__
    if((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) &&
       (event.key.keysym.sym == SDLK_AC_BACK || event.key.keysym.scancode == SDL_SCANCODE_AC_BACK)) {
        event.key.keysym.sym = SDLK_ESCAPE;
        event.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
        return false;
    }
#endif

#ifdef DUNECITY_MOBILE
    const auto rect = [](const SDL_Rect* r) {
        return r ? TouchGestures::Rect{r->x, r->y, r->w, r->h} : TouchGestures::Rect{};
    };
    gestures.setMapArea(rect(mapArea), rect(mapControls));
    if(event.type == SDL_APP_WILLENTERBACKGROUND
       || (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)) {
        // Fingers on the screen now may never report their FINGERUP.
        gestures.reset();
        return false;
    }
#endif

    if(event.type != SDL_FINGERDOWN && event.type != SDL_FINGERMOTION && event.type != SDL_FINGERUP) {
        return false;
    }

#ifdef DUNECITY_MOBILE
    // main.cpp turns off SDL's own touch-to-mouse events on mobile: every pointer event
    // a finger causes comes from here.
    int x = 0;
    int y = 0;
    touchToScreen(event.tfinger.x, event.tfinger.y, x, y);
    const SDL_FingerID finger = event.tfinger.fingerId;
    const Uint32 ms = event.tfinger.timestamp;
    if(event.type == SDL_FINGERDOWN) {
        pushActions(gestures.fingerDown(finger, x, y, ms));
    } else if(event.type == SDL_FINGERMOTION) {
        pushActions(gestures.fingerMotion(finger, x, y, ms));
    } else {
        pushActions(gestures.fingerUp(finger, x, y, ms));
    }
    return true;
#else
    return false;
#endif
}

SDL_Point edgeScroll() {
#ifdef DUNECITY_MOBILE
    const auto direction = gestures.edgeScroll();
    return SDL_Point{direction[0], direction[1]};
#else
    return SDL_Point{0, 0};
#endif
}

bool decodeMapGesture(const SDL_Event& event, MapGesture& gesture) {
    if(mapGestureEventType == 0 || event.type != mapGestureEventType) {
        return false;
    }
    gesture.kind = static_cast<MapGestureKind>(event.user.code);
    unpackPair(event.user.data1, gesture.x, gesture.y);
    unpackPair(event.user.data2, gesture.dx, gesture.dy);
    return true;
}

}
