#ifndef DUNECITY_MOBILE_PLATFORM_H
#define DUNECITY_MOBILE_PLATFORM_H

// Android and iOS share one touch and display policy: touches are translated
// into mouse events, the pointer is hidden until a physical mouse is used, the
// on-screen keyboard follows text box focus, and the interface keeps a fixed
// logical size that SDL letterboxes onto the screen. Platform services such as
// app storage, the Android Back button or certificate paths keep their own
// __ANDROID__ / DUNECITY_IOS checks.
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#define DUNECITY_IOS 1
#endif
#endif

#if defined(__ANDROID__) || defined(DUNECITY_IOS)
#define DUNECITY_MOBILE 1
#endif

#endif
