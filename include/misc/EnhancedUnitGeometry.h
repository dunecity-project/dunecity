#ifndef ENHANCED_UNIT_GEOMETRY_H
#define ENHANCED_UNIT_GEOMETRY_H

#include <Definitions.h>
#include <SDL.h>
#include <algorithm>
#include <cmath>

// HQ unit dimensions are authored presentation pixels, not classic tile pixels.
// Do not apply the world fallback multiplier a second time.
inline SDL_Rect calcEnhancedUnitDrawingRect(SDL_Point baseSize, unsigned int zoom,
                                            double scale, SDL_Point frameSize,
                                            SDL_Point imageAnchor, SDL_Point screenAnchor) {
    if(baseSize.x <= 0 || baseSize.y <= 0 || zoom >= NUM_ZOOMLEVEL
       || frameSize.x <= 0 || frameSize.y <= 0 || !std::isfinite(scale) || scale <= 0) {
        return {};
    }
    const int width = std::max(1, static_cast<int>(std::lround(baseSize.x * (zoom + 1) * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(baseSize.y * (zoom + 1) * scale)));
    return {
        screenAnchor.x - static_cast<int>(std::lround(imageAnchor.x * double(width) / frameSize.x)),
        screenAnchor.y - static_cast<int>(std::lround(imageAnchor.y * double(height) / frameSize.y)),
        width, height
    };
}

#endif
