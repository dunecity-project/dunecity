#ifndef ENHANCED_BUILDING_GEOMETRY_H
#define ENHANCED_BUILDING_GEOMETRY_H

#include <Definitions.h>
#include <SDL.h>

#include <algorithm>
#include <cmath>

inline SDL_Rect calcEnhancedBuildingDrawingRect(int footprintWidth, unsigned int zoom,
                                                SDL_Point frameSize, SDL_Point imageAnchor,
                                                SDL_Point screenAnchor, int presentationScale = 1) {
    if(footprintWidth <= 0 || zoom >= NUM_ZOOMLEVEL || frameSize.x <= 0 || frameSize.y <= 0) {
        return {};
    }
    // Anchors are already in screen pixels. TILESIZE is world units, not pixels.
    const int width = footprintWidth * D2_TILESIZE * static_cast<int>(zoom + 1)
                      * (presentationScale == 3 ? 3 : 1);
    const double scale = static_cast<double>(width) / frameSize.x;
    return {
        screenAnchor.x - static_cast<int>(std::lround(imageAnchor.x * scale)),
        screenAnchor.y - static_cast<int>(std::lround(imageAnchor.y * scale)),
        width,
        std::max(1, static_cast<int>(std::lround(frameSize.y * scale))),
    };
}

#endif
