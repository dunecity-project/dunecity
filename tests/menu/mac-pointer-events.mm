#import <Cocoa/Cocoa.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <stdexcept>

namespace {
NSPoint savedPointer;
NSRunningApplication* previousApplication;
bool active = false;
}

// Test-only Cocoa events enter SDL's own native responder. On macOS 26 SDL
// samples the global pointer for motion, so a real window-local warp is required;
// merely constructing an NSEvent at a requested location would be a false test.
extern "C" void nativePointerProbeBegin(SDL_Window* window) {
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if(!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA)
        throw std::runtime_error("Pointer regression requires the actual Cocoa backend");
    savedPointer = [NSEvent mouseLocation];
    previousApplication = NSWorkspace.sharedWorkspace.frontmostApplication;
    active = true;
    [NSApp activateIgnoringOtherApps:YES];
    [info.info.cocoa.window makeKeyAndOrderFront:nil];
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
            SDL_WarpMouseInWindow(window, x, y);
            SDL_PumpEvents();
            [view mouseMoved:event];
        } else if(buttonState) {
            [view mouseDown:event];
        } else {
            [view mouseUp:event];
        }
    }
}

extern "C" void nativePointerProbeEnd() {
    if(!active) return;
    const CGFloat primaryTop = NSMaxY(NSScreen.screens.firstObject.frame);
    SDL_WarpMouseGlobal(static_cast<int>(savedPointer.x), static_cast<int>(primaryTop - savedPointer.y));
    [previousApplication activateWithOptions:0];
    previousApplication = nil;
    active = false;
}
