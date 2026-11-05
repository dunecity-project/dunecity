#ifndef DUNECITY_MAC_TRACKPAD_H
#define DUNECITY_MAC_TRACKPAD_H

#include <misc/MobilePlatform.h>
#include <SDL_rect.h>

#if defined(__APPLE__) && !defined(DUNECITY_IOS) && defined(__OBJC__)
@class NSEvent;
#endif

namespace MacTrackpad {

#if defined(__APPLE__) && !defined(DUNECITY_IOS)

/// Main thread, after creating the SDL window and renderer.
void initialize();
/// Main thread, before destroying the SDL window or quitting SDL.
void shutdown();
/// Logical map bounds and optional overlay exclusion; nullptr disables gestures.
/// Repeating the same bounds preserves an in-progress gesture.
void setMapArea(const SDL_Rect* area, const SDL_Rect* controls = nullptr);

#ifdef __OBJC__
/// Native router used by the local event monitor; nil means consumed.
NSEvent* handleNativeEvent(NSEvent* event);
#endif

#else

inline void initialize() {}
inline void shutdown() {}
inline void setMapArea(const SDL_Rect*, const SDL_Rect* = nullptr) {}

#endif

}

#endif // DUNECITY_MAC_TRACKPAD_H
