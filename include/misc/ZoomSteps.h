#ifndef DUNECITY_ZOOMSTEPS_H
#define DUNECITY_ZOOMSTEPS_H

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

/**
    The in-game zoom steps (pinch, the + and - buttons, F1-F3, the Dune2R wheel).

    Sprites exist only at the three pre-scaled zoom levels (1x, 2x and 3x of the
    original graphics). A step between or beyond them draws the map at a sprite
    level into an offscreen board of a different size, which the game then scales
    onto the real map area (Game::drawScreen, ScreenBorder::setDrawingBoardSize).
    No SDL dependency, so the desktop tests cover the choice of level and size.
*/
namespace ZoomSteps {

/// On-screen size of the original graphics, smallest first.
inline constexpr std::array<float, 10> kScales{0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f};
inline constexpr int kCount = static_cast<int>(kScales.size());

/// The step showing sprite level 0, 1 or 2 at its own size.
inline int stepForLevel(int level) {
    const float scale = static_cast<float>(level < 0 ? 0 : (level > 2 ? 2 : level)) + 1.0f;
    for(int step = 0; step < kCount; ++step) {
        if(kScales[step] == scale) return step;
    }
    return 1;
}

inline int clampStep(int step) {
    return step < 0 ? 0 : (step >= kCount ? kCount - 1 : step);
}

struct Render {
    int spriteLevel = 0;    ///< currentZoomlevel to draw with
    int boardWidth = 0;     ///< drawing board size; equal to the screen board when no scaling is needed
    int boardHeight = 0;
};

/**
    How to draw a step on a map area of screenWidth x screenHeight pixels. Prefers
    the next larger sprite level, scaled down (sharper than scaling a smaller level
    up), and falls back to smaller levels while the drawing board would exceed
    maxTextureSize in either direction. Returns nothing if no level fits.
*/
inline std::optional<Render> renderFor(int step, int screenWidth, int screenHeight, int maxTextureSize) {
    const float scale = kScales[clampStep(step)];
    const int preferredLevel = std::min(2, std::max(0, static_cast<int>(std::ceil(scale)) - 1));
    for(int level = preferredLevel; level >= 0; --level) {
        const float factor = scale / static_cast<float>(level + 1);   // screen pixels per board pixel
        Render render{level, screenWidth, screenHeight};
        if(factor != 1.0f) {
            render.boardWidth = std::max(1, static_cast<int>(std::lround(screenWidth / factor)));
            render.boardHeight = std::max(1, static_cast<int>(std::lround(screenHeight / factor)));
        }
        if(render.boardWidth <= maxTextureSize && render.boardHeight <= maxTextureSize) {
            return render;
        }
    }
    return std::nullopt;
}

/// "150%" for the news ticker.
inline int percent(int step) {
    return static_cast<int>(std::lround(kScales[clampStep(step)] * 100.0f));
}

} // namespace ZoomSteps

#endif
