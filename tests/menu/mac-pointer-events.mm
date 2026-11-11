#import <Cocoa/Cocoa.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <stdexcept>
#include <cmath>
#include <cstdio>


// Leave the physical pointer untouched. Move only the diagnostic's own window
// beneath it, then exercise the real Cocoa responder and SDL event conversion.
extern "C" void nativePointerProbeBegin(SDL_Window* window) {
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if(!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA)
        throw std::runtime_error("Pointer regression requires the actual Cocoa backend");
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
            context:nil eventNumber:1 clickCount:1 pressure:buttonState > 0 ? 1.0 : 0.0];
        if(buttonState < 0) {
            if(!(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) {
                const NSPoint screen = [native convertPointToScreen:location];
                const NSPoint pointer = NSEvent.mouseLocation;
                NSRect frame = native.frame;
                frame.origin.x += pointer.x - screen.x;
                frame.origin.y += pointer.y - screen.y;
                [native setFrameOrigin:frame.origin];
                SDL_PumpEvents();
            }
            int actualX = 0, actualY = 0;
            nativePointerProbeCurrentPoint(window, &actualX, &actualY);
            if(std::abs(actualX - x) > 1 || std::abs(actualY - y) > 1)
                throw std::runtime_error("Own-window position was clamped or fullscreen pointer point is unavailable");
            int originX = 0, originY = 0;
            SDL_GetWindowPosition(window, &originX, &originY);
            const NSPoint pointer = NSEvent.mouseLocation;
            std::printf("POINTER_NATIVE: global=%.1f,%.1f windowOrigin=%d,%d requested=%d,%d actual=%d,%d focused=%d\n",
                        pointer.x, pointer.y, originX, originY, x, y, actualX, actualY,
                        int((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0));
            std::fflush(stdout);
            [view mouseMoved:event];
        } else if(buttonState) {
            [view mouseDown:event];
        } else {
            [view mouseUp:event];
        }
    }
}

