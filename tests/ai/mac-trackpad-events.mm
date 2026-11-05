#include <misc/MobilePlatform.h>

#if defined(__APPLE__) && !defined(DUNECITY_IOS)

#import <Cocoa/Cocoa.h>

#include <globals.h>
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

void dispatchMacProbeGesture(DuneCityProbeGestureEvent* event) {
    // Install after MacTrackpad::initialize(): AppKit invokes existing monitors
    // first (measured on the native SDL stack). Consumed map gestures never
    // reach this sink. For passthrough scrolling, continue through the actual
    // NSView/SDL scrollWheel: responder so menus and lists receive real SDL wheel
    // events. A synthetic magnify must not reach AppKit's backing-type router.
    id sink = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel | NSEventMaskMagnify
        handler:^NSEvent*(NSEvent* nativeEvent) {
            if(![nativeEvent isKindOfClass:[DuneCityProbeGestureEvent class]]) {
                return nativeEvent;
            }
            if(nativeEvent.type == NSEventTypeScrollWheel) {
                [nativeEvent.window.contentView scrollWheel:nativeEvent];
            }
            return nil;
        }];
    [NSApp sendEvent:event];
    [NSEvent removeMonitor:sink];
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

// Native phase bits: Began=1, Changed=4, Ended=8, Cancelled=16.
extern "C" void queueMacTrackpadPinch(int logicalX, int logicalY, double magnification, int phase) {
    @autoreleasepool {
        auto* event = makeMacProbeGesture(logicalX, logicalY, NSEventTypeMagnify);
        event.probeMagnification = magnification;
        event.probePhase = static_cast<NSEventPhase>(phase);
        dispatchMacProbeGesture(event);
    }
}

#endif
