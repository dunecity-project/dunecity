#ifndef MISC_RENDERERINITIALIZATION_H
#define MISC_RENDERERINITIALIZATION_H

#include <SDL.h>
#include <string>

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

inline SDL_Renderer* createStartupRenderer(SDL_Window* window,
                                          bool allowSoftwareFallback = startupSoftwareFallback) {
    if(!window) {
        SDL_SetError("Cannot create a renderer without a window");
        return nullptr;
    }

    SDL_Renderer* result = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if(result && SDL_RenderTargetSupported(result)) return result;
    if(result) {
        SDL_DestroyRenderer(result);
        SDL_SetError("Selected SDL renderer does not support target textures");
    }
    if(!allowSoftwareFallback) return nullptr;

    const std::string acceleratedError = SDL_GetError();
    std::string softwareError = "No software renderer with target texture support is available";
    for(int index = 0; index < SDL_GetNumRenderDrivers(); ++index) {
        SDL_RendererInfo info{};
        const Uint32 required = SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE;
        if(SDL_GetRenderDriverInfo(index, &info) != 0 || (info.flags & required) != required)
            continue;
        // An explicit index avoids repeating a failing GL/EGL backend or hint.
        result = SDL_CreateRenderer(window, index, required);
        if(result && SDL_RenderTargetSupported(result)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Accelerated renderer failed: %s; continuing with software rendering",
                acceleratedError.c_str());
            return result;
        }
        if(result) {
            SDL_DestroyRenderer(result);
            SDL_SetError("Software renderer does not support target textures");
        }
        softwareError = SDL_GetError();
    }
    SDL_SetError("Accelerated renderer: %s; software renderer: %s",
                 acceleratedError.c_str(), softwareError.c_str());
    return nullptr;
}

#endif
