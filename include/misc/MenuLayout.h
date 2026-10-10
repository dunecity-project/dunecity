#ifndef DUNECITY_MENU_LAYOUT_H
#define DUNECITY_MENU_LAYOUT_H

#include <Definitions.h>

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

// SDL desktop bounds are screen coordinates. Retina windows have a denser
// backing surface, which can support render resolutions beyond those bounds.
inline SDL_Point displayVideoResolutionLimit(SDL_Point desktop, SDL_Point windowSize,
                                             SDL_Point outputSize) {
    if(desktop.x <= 0 || desktop.y <= 0) return desktop;
    if(windowSize.x <= 0 || windowSize.y <= 0 || outputSize.x <= 0 || outputSize.y <= 0)
        return desktop;
    return {std::max(desktop.x, static_cast<int>(int64_t(desktop.x) * outputSize.x / windowSize.x)),
            std::max(desktop.y, static_cast<int>(int64_t(desktop.y) * outputSize.y / windowSize.y))};
}

// Fit the presentation window without changing the selected render resolution
// or stretching its aspect ratio. The renderer maps input to its logical size.
inline SDL_Point fitVideoWindow(SDL_Point requested, SDL_Point available) {
    if(requested.x <= 0 || requested.y <= 0 || available.x <= 0 || available.y <= 0)
        return requested;
    const double scale = std::min({1.0, static_cast<double>(available.x) / requested.x,
                                  static_cast<double>(available.y) / requested.y});
    return {std::max(1, static_cast<int>(std::floor(requested.x * scale))),
            std::max(1, static_cast<int>(std::floor(requested.y * scale)))};
}

/// Interface Size choice meaning "logical size = what is on screen", i.e. one
/// interface pixel per screen pixel. 0 is automatic, 480/600/768 are the fixed
/// presets; see interfaceLogicalSize().
inline constexpr int INTERFACE_HEIGHT_NATIVE = -1;

inline int interfaceWidthForHeight(int height, bool widescreen) {
    if(!widescreen) return height * 4 / 3;
    switch(height) {
        case 480: return 854;
        case 600: return 1067;
        case 768: return 1366;
        default: return height * 16 / 9;
    }
}

inline bool isSupportedInterfaceResolution(int width, int height) {
    return (height == 480 || height == 600 || height == 768)
        && (width == interfaceWidthForHeight(height, false)
            || width == interfaceWidthForHeight(height, true));
}

inline int validatedInterfaceWidth(int width, int height) {
    if(isSupportedInterfaceResolution(width, height)) return width;
    return interfaceWidthForHeight(height, false);
}

inline constexpr int validatedStartMenuMode(int value) {
    return value == 1 ? 1 : 0;
}

// Enlarged start screens retain the original framed, single-column composition.
// Dimensions scale with the logical interface height so controls have a stable
// physical size across the 480, 600 and 768 presets.
struct StartMenuLayout {
    int width;
    int height;
    int buttonCount;

    int buttonHeight() const { return std::clamp(height * 28 / 480, 28, 46); }
    int gap() const { return std::clamp(height / 150, 4, 6); }
    int buttonWidth() const { return std::clamp(height * 220 / 480, 220, 360); }
    int bottomMargin() const { return std::clamp(height / 24, 20, 32); }
    int listHeight() const { return buttonCount * buttonHeight() + (buttonCount - 1) * gap(); }
    int top() const { return height - bottomMargin() - listHeight(); }
    SDL_Rect button(int index) const {
        return {(width - buttonWidth()) / 2,
                top() + index * (buttonHeight() + gap()), buttonWidth(), buttonHeight()};
    }
    SDL_Rect borderBounds() const {
        const auto first = button(0);
        return {first.x - 16, first.y - 12, first.w + 32, listHeight() + 24};
    }
    SDL_Rect artBounds() const {
        const int artWidth = std::min({height * 320 / 480, width - 80, 460});
        return {(width - artWidth) / 2, 16, artWidth, std::max(80, top() - 42)};
    }
    SDL_Rect planetBounds() const {
        auto bounds = artBounds();
        bounds.h -= std::clamp(height / 16, 28, 42);
        return bounds;
    }
    SDL_Rect logoBounds() const {
        const auto bounds = artBounds();
        const int logoHeight = std::clamp(height / 18, 26, 40);
        return {bounds.x + bounds.w / 8, bounds.y + bounds.h - logoHeight,
                bounds.w * 3 / 4, logoHeight};
    }
};

inline int validatedInterfaceHeight(int height, bool android) {
    if(height == 480 || height == 600 || height == 768) return height;
    // Android replaces the physical surface on fold, rotation, DeX and
    // multi-window transitions, so it always keeps a fixed logical interface.
    if(height == INTERFACE_HEIGHT_NATIVE && !android) return INTERFACE_HEIGHT_NATIVE;
    return android ? 480 : 0;
}

// How many physical pixels one interface pixel covers when the interface size is
// Automatic. Dune Legacy 0.96.4 introduced this ("Use a smaller logical
// resolution for high resolution displays, e.g. 960x540 for 1920x1080", see
// ChangeLog), so a 1080p screen shows an enlarged 960x540 interface by default.
inline int getLogicalToPhysicalResolutionFactor(int physicalWidth, int physicalHeight) {
    if(physicalWidth >= 1280*3 && physicalHeight >= 720*3) {
        return 3;
    } else if(physicalWidth >= 640*2 && physicalHeight >= 480*2) {
        return 2;
    } else {
        return 1;
    }
}

// Logical width that gives a `height`-tall interface the same shape as a
// presentedWidth x presentedHeight surface, so it fills the window without
// black bars. Even, and never below the minimum width.
inline int interfaceWidthForShape(int height, int presentedWidth, int presentedHeight) {
    if(presentedWidth <= 0 || presentedHeight <= 0) {
        return interfaceWidthForHeight(height, false);
    }
    const int width = static_cast<int>(std::lround(static_cast<double>(height) * presentedWidth / presentedHeight));
    return std::max(SCREEN_MIN_WIDTH, width & ~1);
}

// The logical (interface) size for a surface of presentedWidth x presentedHeight
// screen coordinates - the window, or the desktop for a fullscreen-desktop
// window. This is what goes to SDL_RenderSetLogicalSize, so it decides how many
// physical pixels one interface pixel covers:
//   native (1:1)     exactly the presented size, one interface pixel per screen
//                    pixel (on a HiDPI display SDL still draws into the denser
//                    pixel surface, so it stays crisp instead of tiny)
//   480/600/768      that fixed height, in the shape of the presented surface
//   automatic (0)    the presented size divided by the enlargement factor above
inline SDL_Point interfaceLogicalSize(int interfaceHeight, int presentedWidth, int presentedHeight) {
    if(interfaceHeight > 0) {
        return SDL_Point{interfaceWidthForShape(interfaceHeight, presentedWidth, presentedHeight), interfaceHeight};
    }
    int factor = 1;
    if(interfaceHeight != INTERFACE_HEIGHT_NATIVE) {
        factor = std::max(1, getLogicalToPhysicalResolutionFactor(presentedWidth, presentedHeight));
    }
    return SDL_Point{std::max(presentedWidth / factor, SCREEN_MIN_WIDTH),
                     std::max(presentedHeight / factor, SCREEN_MIN_HEIGHT)};
}

#endif
