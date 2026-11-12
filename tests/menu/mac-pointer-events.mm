#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <stdexcept>
#include <cmath>
#include <cstdio>

namespace {
id pointerEventMonitor = nil;
NSInteger nextPointerEventNumber = 1;
// Focus and renderer transitions may reconfigure AppKit's dispatcher. Validate
// SDL's current entry point instead of pinning an earlier Objective-C identity.
Class requireLivePointerDispatcher() {
    if(!NSApp || ![NSThread isMainThread])
        throw std::runtime_error("Native pointer dispatch requires SDL's main-thread application");
    const SEL selector = @selector(sendEvent:);
    const IMP applicationDispatch = class_getMethodImplementation(object_getClass(NSApp), selector);
    const IMP cocoaDispatch = class_getMethodImplementation([NSApplication class], selector);
    const Class candidates[] = {objc_lookUpClass("SDL3Application"), objc_lookUpClass("SDLApplication")};
    for(Class candidate : candidates) {
        if(candidate && [NSApp isKindOfClass:candidate] && applicationDispatch != cocoaDispatch
           && applicationDispatch == class_getMethodImplementation(candidate, selector))
            return candidate;
    }
    throw std::runtime_error("Native pointer dispatch requires the actual SDL application sendEvent override");
}

// Keep SDL's NSWindow class intact, including during synchronous frame-change
// notifications. Only this instance may bypass AppKit's screen-edge constraint,
// and only while setFrameOrigin runs. Restore the method before pumping input.
class PointerWindowPlacement {
public:
    explicit PointerWindowPlacement(NSWindow* native) {
        if(![NSThread isMainThread])
            throw std::runtime_error("Pointer window placement requires the AppKit main thread");
        const SEL selector = @selector(constrainFrameRect:toScreen:);
        method = class_getInstanceMethod(object_getClass(native), selector);
        if(!method) throw std::runtime_error("Missing AppKit frame constraint method");
        original = method_getImplementation(method);
        const auto constrain = reinterpret_cast<NSRect (*)(id, SEL, NSRect, NSScreen*)>(original);
        // The runtime supplies the Objective-C IMP ABI, including NSRect returns;
        // a block's arguments omit the hidden selector, unlike a method IMP.
        replacement = imp_implementationWithBlock(^NSRect(id candidate, NSRect frame, NSScreen* screen) {
            if(candidate == native) return frame;
            return constrain(candidate, selector, frame, screen);
        });
        if(!replacement) throw std::runtime_error("Cannot isolate pointer window geometry");
        method_setImplementation(method, replacement);
    }

    ~PointerWindowPlacement() {
        method_setImplementation(method, original);
        imp_removeBlock(replacement);
    }
    PointerWindowPlacement(const PointerWindowPlacement&) = delete;
    PointerWindowPlacement& operator=(const PointerWindowPlacement&) = delete;

private:
    Method method;
    IMP original;
    IMP replacement;
};

void logNativePointerEvent(SDL_Window* window, NSEvent* event, const char* stage) {
    NSWindow* native = event.window;
    NSView* view = native.contentView;
    const NSRect bounds = view.bounds;
    const NSPoint location = event.locationInWindow;
    const NSPoint content = [view convertPoint:location fromView:nil];
    const NSPoint pointer = NSEvent.mouseLocation;
    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);
    const double x = (content.x - bounds.origin.x) * width / bounds.size.width;
    const double y = (view.flipped ? content.y - bounds.origin.y : NSMaxY(bounds) - content.y)
        * height / bounds.size.height;
    std::printf("POINTER_NATIVE_EVENT: stage=%s sequence=%ld type=%lu location=%.3f,%.3f content=%.3f,%.3f global=%.3f,%.3f focused=%d nativeKey=%d\n",
                stage, static_cast<long>(event.eventNumber), static_cast<unsigned long>(event.type),
                location.x, location.y, x, y, pointer.x, pointer.y,
                int((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0), int(native.keyWindow));
    std::fflush(stdout);
}
}


// Leave the physical pointer untouched. Move only the diagnostic's own window
// beneath it, then synchronously send native events through SDL's application
// override: Cocoa_DispatchEvent records mouse focus/buttons before AppKit routes
// to SDL's responder. Calling that responder directly skips the SDL boundary.
// NSApp postEvent serializes coordinates against stale window-server geometry
// after own-window moves; sendEvent preserves the event's measured local point.
extern "C" void nativePointerProbeBegin(SDL_Window* window) {
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if(!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA)
        throw std::runtime_error("Pointer regression requires the actual Cocoa backend");
    const Class dispatchClass = requireLivePointerDispatcher();
    const SEL selector = @selector(sendEvent:);
    const Class applicationClass = object_getClass(NSApp);
    const IMP applicationDispatch = class_getMethodImplementation(applicationClass, selector);
    const IMP cocoaDispatch = class_getMethodImplementation([NSApplication class], selector);
    std::printf("POINTER_DISPATCH: route=NSApp.sendEvent synchronous=1 applicationClass=%s dispatchClass=%s sendEventIMP=%p cocoaSendEventIMP=%p\n",
                class_getName(applicationClass), class_getName(dispatchClass),
                reinterpret_cast<void*>(applicationDispatch), reinterpret_cast<void*>(cocoaDispatch));
    std::fflush(stdout);
    if(pointerEventMonitor) [NSEvent removeMonitor:pointerEventMonitor];
    __weak NSWindow* monitored = info.info.cocoa.window;
    const Uint32 monitoredID = SDL_GetWindowID(window);
    pointerEventMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:
        NSEventMaskMouseMoved | NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp
        handler:^NSEvent*(NSEvent* event) {
            if(monitored && event.window == monitored) {
                if(SDL_Window* live = SDL_GetWindowFromID(monitoredID))
                    logNativePointerEvent(live, event, "monitored");
            }
            return event;
        }];
    [NSApp activateIgnoringOtherApps:YES];
    [info.info.cocoa.window makeKeyAndOrderFront:nil];
}

extern "C" void nativePointerProbeCurrentPoint(SDL_Window* window, int* x, int* y) {
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if(!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA)
        throw std::runtime_error("Lost the native pointer window");
    NSWindow* native = info.info.cocoa.window;
    NSView* view = native.contentView;
    const NSRect bounds = view.bounds;
    const NSPoint point = [view convertPoint:[native convertPointFromScreen:NSEvent.mouseLocation] fromView:nil];
    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);
    *x = static_cast<int>(std::lround((point.x - bounds.origin.x) * width / bounds.size.width));
    *y = static_cast<int>(std::lround((view.flipped ? point.y - bounds.origin.y
        : NSMaxY(bounds) - point.y) * height / bounds.size.height));
}

// Return the measured content point, never the requested fixture coordinates.
// Any remaining placement failure still makes the named coverage gate fail.
extern "C" SDL_Point nativePointerProbePlace(SDL_Window* window, int x, int y) {
    @autoreleasepool {
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        if(!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA)
            throw std::runtime_error("Lost the Cocoa pointer regression window");
        NSWindow* native = info.info.cocoa.window;
        NSView* view = native.contentView;
        int width = 0, height = 0;
        SDL_GetWindowSize(window, &width, &height);
        if(width <= 0 || height <= 0) throw std::runtime_error("Empty native pointer window");
        if(!(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) {
            const NSRect bounds = view.bounds;
            const NSRect originalFrame = native.frame;
            if(originalFrame.size.width <= 0 || originalFrame.size.height <= 0
               || bounds.size.width <= 0 || bounds.size.height <= 0)
                throw std::runtime_error("Empty native pointer frame or content geometry");
            const NSPoint local = NSMakePoint(bounds.origin.x + x * bounds.size.width / width,
                view.flipped ? bounds.origin.y + y * bounds.size.height / height
                             : NSMaxY(bounds) - y * bounds.size.height / height);
            // A later pump can re-apply AppKit's screen constraint after the
            // override is restored. Retry only this own-window move, and accept it
            // only after the requested point survives a further pump unmoved.
            // Exhaustion returns the measured mismatch to the coverage gate.
            constexpr int placementAttempts = 4;
            for(int attempt = 1; attempt <= placementAttempts; ++attempt) {
                const NSRect beforeFrame = native.frame;
                const NSPoint screen = [native convertPointToScreen:[view convertPoint:local toView:nil]];
                const NSPoint pointer = NSEvent.mouseLocation;
                const NSPoint origin = NSMakePoint(beforeFrame.origin.x + pointer.x - screen.x,
                                                   beforeFrame.origin.y + pointer.y - screen.y);
                {
                    PointerWindowPlacement geometry(native);
                    [native setFrameOrigin:origin];
                }
                bool settled = false;
                for(int observation = 1; observation <= 2; ++observation) {
                    if(observation == 2) SDL_Delay(5);
                    SDL_PumpEvents();
                    const NSRect actualFrame = native.frame;
                    const NSRect actualBounds = view.bounds;
                    int actualWidth = 0, actualHeight = 0;
                    SDL_GetWindowSize(window, &actualWidth, &actualHeight);
                    SDL_Point measured{};
                    nativePointerProbeCurrentPoint(window, &measured.x, &measured.y);
                    std::printf("POINTER_PLACEMENT: attempt=%d observation=%d requestedOrigin=%.3f,%.3f frameBefore=%.3f,%.3f,%.3f,%.3f frameAfter=%.3f,%.3f,%.3f,%.3f contentBefore=%.3f,%.3f contentAfter=%.3f,%.3f windowBefore=%dx%d windowAfter=%dx%d requested=%d,%d actual=%d,%d\n",
                                attempt, observation, origin.x, origin.y,
                                beforeFrame.origin.x, beforeFrame.origin.y, beforeFrame.size.width, beforeFrame.size.height,
                                actualFrame.origin.x, actualFrame.origin.y, actualFrame.size.width, actualFrame.size.height,
                                bounds.size.width, bounds.size.height, actualBounds.size.width, actualBounds.size.height,
                                width, height, actualWidth, actualHeight, x, y, measured.x, measured.y);
                    std::fflush(stdout);
                    if(!NSEqualSizes(actualFrame.size, originalFrame.size) || !NSEqualSizes(actualBounds.size, bounds.size)
                       || actualWidth != width || actualHeight != height || native.contentView != view)
                        throw std::runtime_error("Own-window pointer placement changed native content or SDL dimensions");
                    if(std::abs(measured.x - x) > 1 || std::abs(measured.y - y) > 1) break;
                    settled = observation == 2;
                }
                if(settled) break;
            }
        }
        SDL_Point actual{};
        nativePointerProbeCurrentPoint(window, &actual.x, &actual.y);
        int originX = 0, originY = 0;
        SDL_GetWindowPosition(window, &originX, &originY);
        const NSPoint pointer = NSEvent.mouseLocation;
        std::printf("POINTER_NATIVE: global=%.3f,%.3f windowOrigin=%d,%d requested=%d,%d actual=%d,%d focused=%d\n",
                    pointer.x, pointer.y, originX, originY, x, y, actual.x, actual.y,
                    int((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0));
        std::fflush(stdout);
        return actual;
    }
}

extern "C" void nativePointerProbeEvent(SDL_Window* window, int x, int y, int buttonState) {
    @autoreleasepool {
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        if(!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA)
            throw std::runtime_error("Lost the Cocoa pointer regression window");
        NSWindow* native = info.info.cocoa.window;
        NSView* view = native.contentView;
        int width = 0, height = 0;
        SDL_GetWindowSize(window, &width, &height);
        if(width <= 0 || height <= 0) throw std::runtime_error("Empty native pointer window");
        const NSRect bounds = view.bounds;
        const NSPoint local = NSMakePoint(bounds.origin.x + x * bounds.size.width / width,
            view.flipped ? bounds.origin.y + y * bounds.size.height / height
                         : NSMaxY(bounds) - y * bounds.size.height / height);
        const NSPoint location = [view convertPoint:local toView:nil];
        const NSEventType type = buttonState < 0 ? NSEventTypeMouseMoved
            : buttonState ? NSEventTypeLeftMouseDown : NSEventTypeLeftMouseUp;
        NSEvent* event = [NSEvent mouseEventWithType:type location:location modifierFlags:0
            timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:native.windowNumber
            context:nil eventNumber:nextPointerEventNumber++ clickCount:1 pressure:buttonState > 0 ? 1.0 : 0.0];
        requireLivePointerDispatcher();
        logNativePointerEvent(window, event, "sending");
        [NSApp sendEvent:event];
    }
}

