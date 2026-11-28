#ifndef MISC_RENDERERINITIALIZATION_H
#define MISC_RENDERERINITIALIZATION_H

#include <SDL.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

// Keep the game and package probe on the same renderer selection path. Explicit
// SDL_RENDER_DRIVER environment settings retain SDL's normal hint precedence.
inline void configureStartupRenderHints() {
#ifdef _WIN32
    SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "direct3d11", SDL_HINT_DEFAULT);
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
}

#if defined(__linux__) && !defined(__ANDROID__)
constexpr bool startupSoftwareFallback = true;
#else
constexpr bool startupSoftwareFallback = false;
#endif

// SDL2's dummy video driver has no accelerated renderer on any platform. Only
// tests and headless tools select it, so they get the software renderer rather
// than each launcher having to pin SDL_RENDER_DRIVER. Players never use it.
inline bool defaultStartupSoftwareFallback() {
    const char* driver = SDL_GetCurrentVideoDriver();
    return startupSoftwareFallback || (driver && std::string(driver) == "dummy");
}

struct StartupRenderDriver {
    std::string name;
    Uint32 flags = 0;
};

inline bool sameRenderDriverName(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

// The order in which start-up tries SDL's compiled render drivers, as indexes
// into `drivers` (SDL_GetRenderDriverInfo order). Issue #120: SDL's own -1
// search stops at "Couldn't find matching render driver" and drops the reason
// each backend failed, so start-up walks this list itself and logs every miss.
//  1. The driver named by SDL_RENDER_DRIVER (or the platform default hint), of
//     any kind: a player who asks for "software" gets it.
//  2. Every accelerated driver with target textures, in SDL's own priority
//     order (Linux: opengl, opengles2, then opengles).
//  3. Where allowed (Linux desktop, dummy video), the software driver.
inline std::vector<int> startupRendererOrder(const std::vector<StartupRenderDriver>& drivers,
                                             const std::string& preferred, bool allowSoftwareFallback) {
    std::vector<int> order;
    const auto add = [&order](int index) {
        if(std::find(order.begin(), order.end(), index) == order.end()) order.push_back(index);
    };
    const int count = static_cast<int>(drivers.size());
    if(!preferred.empty()) {
        for(int index = 0; index < count; ++index)
            if(sameRenderDriverName(drivers[index].name, preferred)) add(index);
    }
    const Uint32 accelerated = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;
    for(int index = 0; index < count; ++index)
        if((drivers[index].flags & accelerated) == accelerated) add(index);
    if(allowSoftwareFallback) {
        const Uint32 software = SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE;
        for(int index = 0; index < count; ++index)
            if((drivers[index].flags & software) == software) add(index);
    }
    return order;
}

inline SDL_Renderer* createStartupRenderer(SDL_Window* window,
                                          bool allowSoftwareFallback = defaultStartupSoftwareFallback()) {
    if(!window) {
        SDL_SetError("Cannot create a renderer without a window");
        return nullptr;
    }

    std::vector<StartupRenderDriver> drivers;
    for(int index = 0; index < SDL_GetNumRenderDrivers(); ++index) {
        SDL_RendererInfo info{};
        if(SDL_GetRenderDriverInfo(index, &info) == 0 && info.name)
            drivers.push_back({info.name, info.flags});
        else
            drivers.push_back({});
    }

    const char* hint = SDL_GetHint(SDL_HINT_RENDER_DRIVER);
    const std::string preferred = hint ? hint : "";
    if(!preferred.empty() && std::none_of(drivers.begin(), drivers.end(), [&preferred](const StartupRenderDriver& driver) {
           return sameRenderDriverName(driver.name, preferred);
       })) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Requested render driver '%s' is not in this build; trying the built-in order",
                    preferred.c_str());
    }

    std::string failures;
    for(const int index : startupRendererOrder(drivers, preferred, allowSoftwareFallback)) {
        const StartupRenderDriver& driver = drivers[index];
        const bool software = (driver.flags & SDL_RENDERER_SOFTWARE) != 0;
        const Uint32 flags = (software ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED)
                             | SDL_RENDERER_TARGETTEXTURE;
        SDL_ClearError();
        // An explicit index tries exactly this driver; VSync is set afterwards.
        SDL_Renderer* result = SDL_CreateRenderer(window, index, flags);
        if(result && SDL_RenderTargetSupported(result)) {
            if(!failures.empty()) {
                const std::string next = software ? std::string("software rendering") : driver.name;
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Renderer fallback: %s; continuing with %s",
                            failures.c_str(), next.c_str());
            }
            return result;
        }
        if(result) {
            SDL_DestroyRenderer(result);
            SDL_SetError("no target texture support");
        }
        const char* error = SDL_GetError();
        const std::string reason = (error && *error) ? error : "unknown error";
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Render driver %s failed: %s",
                    driver.name.c_str(), reason.c_str());
        failures += (failures.empty() ? "" : "; ") + driver.name + ": " + reason;
    }
    if(failures.empty()) failures = "no compiled render driver supports target textures";
    SDL_SetError("No usable SDL renderer (%s)", failures.c_str());
    return nullptr;
}

#endif
