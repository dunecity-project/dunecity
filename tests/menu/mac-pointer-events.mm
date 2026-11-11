#import <Cocoa/Cocoa.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <stdexcept>
#include <cmath>
#include <string>


// Move only the OS pointer, not SDL's cached mouse state. A motion event must
// therefore traverse Cocoa before it can satisfy the SDL event assertions.
extern "C" void nativePointerProbeBegin(SDL_Window* window) {
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if(!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA)
        throw std::runtime_error("Pointer regression requires the actual Cocoa backend");
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
            const NSPoint screen = [native convertPointToScreen:location];
            const CGFloat primaryTop = NSMaxY(NSScreen.screens.firstObject.frame);
            if(CGError error = CGWarpMouseCursorPosition(CGPointMake(screen.x, primaryTop - screen.y)))
                throw std::runtime_error("Native pointer warp failed: " + std::to_string(error));
            bool settled = false;
            for(int attempt = 0; attempt < 30; ++attempt) {
                const NSPoint actual = [NSEvent mouseLocation];
                if(std::abs(actual.x - screen.x) < 1.0 && std::abs(actual.y - screen.y) < 1.0) {
                    settled = true;
                    break;
                }
                SDL_Delay(1);
            }
            if(!settled) throw std::runtime_error("Native pointer did not reach the requested Cocoa point");
            SDL_PumpEvents();
            [view mouseMoved:event];
        } else if(buttonState) {
            [view mouseDown:event];
        } else {
            [view mouseUp:event];
        }
    }
}

