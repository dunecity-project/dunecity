#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <stdexcept>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
id pointerEventMonitor = nil;
NSInteger nextPointerEventNumber = 1;

NSRect unconstrainedPointerFrame(id, SEL, NSRect frame, NSScreen*) {
    return frame;
}

// AppKit constrains titled windows' top edges to their screen. A stationary
// pointer near that edge otherwise cannot reach the fixture's lower buttons.
// Subclass only this SDL window, only during our explicit placement; retain its
// actual SDL class for input dispatch and every fullscreen/resize transition.
class PointerWindowPlacement {
public:
    explicit PointerWindowPlacement(NSWindow* native)
        : window(native), original(object_getClass(native)) {
        static Class base = Nil, placement = Nil;
        if(base != original) {
            const std::string name = std::string("DuneCityPointerPlacement_") + class_getName(original);
            placement = objc_lookUpClass(name.c_str());
            if(!placement) {
                const SEL selector = @selector(constrainFrameRect:toScreen:);
                const Method method = class_getInstanceMethod(original, selector);
                if(!method) throw std::runtime_error("Missing AppKit frame constraint method");
                placement = objc_allocateClassPair(original, name.c_str(), 0);
                if(!placement) throw std::runtime_error("Cannot isolate pointer window geometry");
                if(!class_addMethod(placement, selector, reinterpret_cast<IMP>(unconstrainedPointerFrame),
                                    method_getTypeEncoding(method))) {
                    objc_disposeClassPair(placement);
                    throw std::runtime_error("Cannot override pointer window frame constraints");
                }
                objc_registerClassPair(placement);
            }
            if(class_getSuperclass(placement) != original)
                throw std::runtime_error("Unexpected pointer placement superclass");
            base = original;
        }
        object_setClass(window, placement);
    }

    ~PointerWindowPlacement() { object_setClass(window, original); }
    PointerWindowPlacement(const PointerWindowPlacement&) = delete;
    PointerWindowPlacement& operator=(const PointerWindowPlacement&) = delete;

private:
    NSWindow* window;
    Class original;
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
// beneath it, then queue each event in NSApp's event queue exactly where the
// window server delivers real input: SDL's pump dequeues it, Cocoa_HandleMouseEvent
// records Cocoa mouse focus and button state, AppKit routes it to SDL's responder,
// and SDL converts it for the renderer. Calling the responder directly skips SDL's
// dispatch; SDL 3.4 then drops the motion (no Cocoa mouse focus) and the press
// synthesizes a focus motion at SDL's stale cached position.
extern "C" void nativePointerProbeBegin(SDL_Window* window) {
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if(!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA)
        throw std::runtime_error("Pointer regression requires the actual Cocoa backend");
    if(pointerEventMonitor) [NSEvent removeMonitor:pointerEventMonitor];
    __weak NSWindow* monitored = info.info.cocoa.window;
    const Uint32 monitoredID = SDL_GetWindowID(window);
    pointerEventMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:
        NSEventMaskMouseMoved | NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp
        handler:^NSEvent*(NSEvent* event) {
            if(monitored && event.window == monitored) {
                if(SDL_Window* live = SDL_GetWindowFromID(monitoredID))
                    logNativePointerEvent(live, event, "dequeued");
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
            const NSPoint local = NSMakePoint(bounds.origin.x + x * bounds.size.width / width,
                view.flipped ? bounds.origin.y + y * bounds.size.height / height
                             : NSMaxY(bounds) - y * bounds.size.height / height);
            const NSPoint screen = [native convertPointToScreen:[view convertPoint:local toView:nil]];
            const NSPoint pointer = NSEvent.mouseLocation;
            NSRect frame = native.frame;
            frame.origin.x += pointer.x - screen.x;
            frame.origin.y += pointer.y - screen.y;
            {
                PointerWindowPlacement geometry(native);
                [native setFrameOrigin:frame.origin];
                SDL_PumpEvents();
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
        logNativePointerEvent(window, event, "queued");
        [NSApp postEvent:event atStart:NO];
    }
}

