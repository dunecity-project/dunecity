#include <misc/MacTrackpad.h>

#if defined(__APPLE__) && !defined(DUNECITY_IOS)

#import <Cocoa/Cocoa.h>

#include <misc/TouchInput.h>
#include <globals.h>
#include <SDL_syswm.h>

#include <algorithm>
#include <cmath>

namespace {

id eventMonitor = nil;
id windowFocusObserver = nil;
id appFocusObserver = nil;
NSWindow* nativeWindow = nil;
SDL_Rect mapArea{};
SDL_Rect mapControls{};
bool mapEnabled = false;
double panRemainderX = 0.0;
double panRemainderY = 0.0;
double pinchScale = 1.0;
double pinchReference = 1.0;
constexpr double zoomFactor = 1.2;

void resetGestures() {
    panRemainderX = panRemainderY = 0.0;
    pinchScale = pinchReference = 1.0;
}

bool eventCoordinates(NSEvent* event, SDL_Point& cursor, double& scaleX, double& scaleY) {
    if(event.window != nativeWindow || renderer == nullptr || window == nullptr) {
        return false;
    }

    NSView* view = nativeWindow.contentView;
    const NSRect bounds = view.bounds;
    if(bounds.size.width <= 0 || bounds.size.height <= 0) {
        return false;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    SDL_RenderWindowToLogical(renderer, 0, 0, &left, &top);
    SDL_RenderWindowToLogical(renderer, width, height, &right, &bottom);
    scaleX = (right - left) / bounds.size.width;
    scaleY = (bottom - top) / bounds.size.height;

    const NSPoint location = [view convertPoint:event.locationInWindow fromView:nil];
    const double x = location.x - bounds.origin.x;
    const double y = view.flipped ? location.y - bounds.origin.y : NSMaxY(bounds) - location.y;
    cursor.x = static_cast<int>(std::lround(left + x * scaleX));
    cursor.y = static_cast<int>(std::lround(top + y * scaleY));
    return SDL_PointInRect(&cursor, &mapArea) && !SDL_PointInRect(&cursor, &mapControls);
}

NSEvent* handleEvent(NSEvent* event) {
    if(!mapEnabled) {
        return event;
    }

    // Precision describes units, not device identity: some wheel mice are precise.
    // A phased scroll (including momentum) is a native fluid gesture; a notched
    // or unphased high-resolution wheel must keep SDL's existing zoom path.
    if(event.type == NSEventTypeScrollWheel
       && event.phase == NSEventPhaseNone && event.momentumPhase == NSEventPhaseNone) {
        return event;
    }

    SDL_Point cursor{};
    double scaleX = 0.0;
    double scaleY = 0.0;
    if(!eventCoordinates(event, cursor, scaleX, scaleY)) {
        resetGestures();
        return event;
    }

    if(event.type == NSEventTypeScrollWheel) {
        if(event.phase & (NSEventPhaseBegan | NSEventPhaseMayBegin)) {
            panRemainderX = panRemainderY = 0.0;
        }
        if(!(event.phase & NSEventPhaseCancelled)) {
            // Cocoa deltas already honor natural-scroll preference. Only change
            // the coordinate system's Y direction, not that preference.
            panRemainderX += event.scrollingDeltaX * scaleX;
            panRemainderY -= event.scrollingDeltaY * scaleY;
            const int dx = static_cast<int>(panRemainderX);
            const int dy = static_cast<int>(panRemainderY);
            panRemainderX -= dx;
            panRemainderY -= dy;
            if(dx != 0 || dy != 0) {
                TouchInput::pushMapGesture({TouchInput::MapGestureKind::Pan, cursor.x, cursor.y, dx, dy});
            }
        }
        if((event.phase | event.momentumPhase) & (NSEventPhaseEnded | NSEventPhaseCancelled)) {
            panRemainderX = panRemainderY = 0.0;
        }
    } else if(event.type == NSEventTypeMagnify) {
        if(event.phase & NSEventPhaseBegan) {
            pinchScale = pinchReference = 1.0;
        }
        if(!(event.phase & NSEventPhaseCancelled)) {
            // Each event is relative to the previous one, not the gesture start:
            // AppKit's gesture-handling sample applies a factor of 1 + delta.
            pinchScale = std::max(0.01, pinchScale * (1.0 + event.magnification));
            while(pinchScale >= pinchReference * zoomFactor) {
                TouchInput::pushMapGesture({TouchInput::MapGestureKind::ZoomIn, cursor.x, cursor.y});
                pinchReference *= zoomFactor;
            }
            while(pinchScale <= pinchReference / zoomFactor) {
                TouchInput::pushMapGesture({TouchInput::MapGestureKind::ZoomOut, cursor.x, cursor.y});
                pinchReference /= zoomFactor;
            }
        }
        if(event.phase & (NSEventPhaseEnded | NSEventPhaseCancelled)) {
            pinchScale = pinchReference = 1.0;
        }
    }

    // Do not let SDL translate the same native gesture into a wheel/pinch again.
    return nil;
}

}

namespace MacTrackpad {

void initialize() {
    if(eventMonitor != nil || window == nullptr) {
        return;
    }
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if(!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA) {
        return;
    }
    nativeWindow = info.info.cocoa.window;
    eventMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel | NSEventMaskMagnify
        handler:^NSEvent*(NSEvent* event) { return handleEvent(event); }];
    NSNotificationCenter* notifications = [NSNotificationCenter defaultCenter];
    windowFocusObserver = [notifications addObserverForName:NSWindowDidResignKeyNotification object:nativeWindow
        queue:nil usingBlock:^(NSNotification*) { resetGestures(); }];
    appFocusObserver = [notifications addObserverForName:NSApplicationDidResignActiveNotification object:NSApp
        queue:nil usingBlock:^(NSNotification*) { resetGestures(); }];
}

void shutdown() {
    if(eventMonitor != nil) {
        [NSEvent removeMonitor:eventMonitor];
        eventMonitor = nil;
    }
    NSNotificationCenter* notifications = [NSNotificationCenter defaultCenter];
    if(windowFocusObserver != nil) {
        [notifications removeObserver:windowFocusObserver];
        windowFocusObserver = nil;
    }
    if(appFocusObserver != nil) {
        [notifications removeObserver:appFocusObserver];
        appFocusObserver = nil;
    }
    nativeWindow = nil;
    mapEnabled = false;
    resetGestures();
}

void setMapArea(const SDL_Rect* area, const SDL_Rect* controls) {
    const SDL_Rect exclusion = controls != nullptr ? *controls : SDL_Rect{};
    if(area == nullptr || area->w <= 0 || area->h <= 0) {
        mapEnabled = false;
        resetGestures();
        return;
    }
    if(!mapEnabled || !SDL_RectEquals(&mapArea, area) || !SDL_RectEquals(&mapControls, &exclusion)) {
        resetGestures();
    }
    mapArea = *area;
    mapControls = exclusion;
    mapEnabled = true;
}

}

#endif
