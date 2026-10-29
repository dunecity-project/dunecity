#ifndef TOUCHINPUT_H
#define TOUCHINPUT_H

#include <SDL.h>

namespace TouchInput {

/**
    On phones and tablets, consumes finger events and pushes the mouse events and map
    gestures they amount to (misc/TouchGestures.h). Returns true for a consumed event.
*/
bool translateTouchEvent(SDL_Event& event);

enum class MapGestureKind : Sint32 { Pan = 1, ZoomIn = 2, ZoomOut = 3 };

/// A two-finger pan or zoom over the game view, in logical screen pixels.
struct MapGesture {
    MapGestureKind kind = MapGestureKind::Pan;
    int x = 0;      ///< midpoint of the fingers
    int y = 0;
    int dx = 0;     ///< Pan: how far the midpoint moved
    int dy = 0;
};

/// Decodes a map gesture pushed by translateTouchEvent; false for any other event.
bool decodeMapGesture(const SDL_Event& event, MapGesture& gesture);

}

#endif // TOUCHINPUT_H
