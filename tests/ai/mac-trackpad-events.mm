#include <misc/MobilePlatform.h>

#if defined(__APPLE__) && !defined(DUNECITY_IOS)

#import <Cocoa/Cocoa.h>

#include <globals.h>
#include <misc/MacTrackpad.h>
#include <cstdio>
#include <string>
#include <SDL_syswm.h>
#include <stdexcept>

// Test-only native metadata. A valid ApplicationDefined backing event allows
// supported AppKit dispatch without private Quartz fields or system-wide input.
// NSApp postEvent: serializes that backing type and loses subclass metadata;
// sendEvent: preserves it and runs the real local monitor installed by the game.
@interface DuneCityProbeGestureEvent : NSEvent
@property(nonatomic) NSEventType probeType;
@property(nonatomic) NSEventPhase probePhase;
@property(nonatomic) BOOL probePrecise;
@property(nonatomic) CGFloat probeDeltaX;
@property(nonatomic) CGFloat probeDeltaY;
@property(nonatomic) CGFloat probeMagnification;
@end

@implementation DuneCityProbeGestureEvent
- (NSEventType)type { return self.probeType; }
- (NSEventPhase)phase { return self.probePhase; }
- (NSEventPhase)momentumPhase { return NSEventPhaseNone; }
- (BOOL)hasPreciseScrollingDeltas { return self.probePrecise; }
- (CGFloat)scrollingDeltaX { return self.probeDeltaX; }
- (CGFloat)scrollingDeltaY { return self.probeDeltaY; }
- (CGFloat)deltaX { return self.probeDeltaX; }
- (CGFloat)deltaY { return self.probeDeltaY; }
- (CGFloat)magnification { return self.probeMagnification; }
- (BOOL)isDirectionInvertedFromDevice { return NO; }
@end

namespace {

DuneCityProbeGestureEvent* makeMacProbeGesture(int logicalX, int logicalY, NSEventType type) {
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if(window == nullptr || renderer == nullptr || !SDL_GetWindowWMInfo(window, &info)
       || info.subsystem != SDL_SYSWM_COCOA) {
        throw std::runtime_error("Native trackpad injection requires the Cocoa video driver and renderer");
    }

    int windowX = 0;
    int windowY = 0;
    int width = 0;
    int height = 0;
    SDL_RenderLogicalToWindow(renderer, static_cast<float>(logicalX), static_cast<float>(logicalY), &windowX, &windowY);
    SDL_GetWindowSize(window, &width, &height);
    if(width <= 0 || height <= 0) {
        throw std::runtime_error("Native trackpad injection requires a nonempty window");
    }

    NSWindow* native = info.info.cocoa.window;
    NSView* view = native.contentView;
    const NSRect bounds = view.bounds;
    const double x = bounds.origin.x + windowX * bounds.size.width / width;
    const double y = view.flipped ? bounds.origin.y + windowY * bounds.size.height / height
                                 : NSMaxY(bounds) - windowY * bounds.size.height / height;
    const NSPoint location = [view convertPoint:NSMakePoint(x, y) toView:nil];
    auto* event = static_cast<DuneCityProbeGestureEvent*>([DuneCityProbeGestureEvent
        otherEventWithType:NSEventTypeApplicationDefined location:location modifierFlags:0
        timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:native.windowNumber
        context:nil subtype:0 data1:0 data2:0]);
    event.probeType = type;
    return event;
}

void dispatchMacProbeGesture(DuneCityProbeGestureEvent* event, bool throughMonitor = false) {
    std::fprintf(stderr,"MAC_NATIVE_DISPATCH_BEGIN route=%s type=%lu phase=%lu window=%ld\n",
                 throughMonitor ? "monitor" : "router",static_cast<unsigned long>(event.type),
                 static_cast<unsigned long>(event.phase),static_cast<long>(event.windowNumber));
    std::fflush(stderr);
    // There must not be a second local monitor: AppKit does not promise monitor
    // order, and a synthetic-event sink could swallow the gesture first. Route
    // native metadata through the exact shipped handler. Explicit passthrough
    // continues through SDL's actual view responder, not an invented SDL event.
    @try {
        if(throughMonitor) {
            [NSApp sendEvent:event];
        } else if(NSEvent* remaining = MacTrackpad::handleNativeEvent(event)) {
            if(remaining.type != NSEventTypeScrollWheel) {
                throw std::runtime_error("Native pinch router did not consume the injected map gesture");
            }
            [remaining.window.contentView scrollWheel:remaining];
        }
    } @catch(NSException* exception) {
        const char* reason = exception.reason.UTF8String;
        throw std::runtime_error(std::string("Native AppKit injection failed: ") + (reason ? reason : "unknown exception"));
    }
    std::fprintf(stderr,"MAC_NATIVE_DISPATCH_END route=%s\n",throughMonitor ? "monitor" : "router");
    std::fflush(stderr);
}

}

// Main thread, after MacTrackpad::initialize() and setMapArea(). Coordinates are
// logical pixels; scroll deltas are Cocoa points (+Y up), not SDL wheel units.
// Dispatch is synchronous; the resulting map gesture / SDL wheel is queued for
// the normal game event pump. No cursor warp or global event post is performed.
extern "C" void queueMacTrackpadScroll(int logicalX, int logicalY, double dx, double dy,
                                        bool precise, bool phased) {
    @autoreleasepool {
        auto* event = makeMacProbeGesture(logicalX, logicalY, NSEventTypeScrollWheel);
        event.probeDeltaX = dx;
        event.probeDeltaY = dy;
        event.probePrecise = precise;
        event.probePhase = phased ? NSEventPhaseChanged : NSEventPhaseNone;
        dispatchMacProbeGesture(event);
    }
}

// A separate map-pan injection exercises real NSApp monitor installation, with
// no competing test monitor. The shipped handler must consume this map event.
extern "C" void queueMacTrackpadMonitorPan(int logicalX, int logicalY, double dx, double dy) {
    @autoreleasepool {
        auto* event = makeMacProbeGesture(logicalX, logicalY, NSEventTypeScrollWheel);
        event.probeDeltaX = dx;
        event.probeDeltaY = dy;
        event.probePrecise = YES;
        event.probePhase = NSEventPhaseChanged;
        dispatchMacProbeGesture(event,true);
    }
}

// Native phase bits: Began=1, Changed=4, Ended=8, Cancelled=16.
extern "C" void queueMacTrackpadPinch(int logicalX, int logicalY, double magnification, int phase) {
    @autoreleasepool {
        auto* event = makeMacProbeGesture(logicalX, logicalY, NSEventTypeMagnify);
        event.probeMagnification = magnification;
        event.probePhase = static_cast<NSEventPhase>(phase);
        dispatchMacProbeGesture(event,true);
    }
}

#endif
