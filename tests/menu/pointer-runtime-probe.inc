#include <SDL.h>
#include <mach-o/dyld.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

void verifyPointerRuntime(SDL_Renderer* renderer) {
    SDL_version version{};
    SDL_GetVersion(&version);
    SDL_RendererInfo info{};
    if(SDL_GetRendererInfo(renderer, &info) != 0 || !SDL_GetCurrentVideoDriver()
       || std::strcmp(SDL_GetCurrentVideoDriver(), "cocoa") != 0)
        throw std::runtime_error("Pointer test requires the native Cocoa renderer");
    std::printf("POINTER_RUNTIME: SDL=%d.%d.%d renderer=%s\n", version.major, version.minor, version.patch, info.name);
    const char* required = std::getenv("MENU_POINTER_RUNTIME");
    bool dynamicCore = false;
    for(uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const char* path = _dyld_get_image_name(i);
        const char* name = path ? std::strrchr(path, '/') : nullptr;
        name = name ? name + 1 : path;
        if(!name || (std::strncmp(name, "libSDL", 6) != 0 && std::strncmp(name, "SDL", 3) != 0)) continue;
        dynamicCore |= std::strcmp(name, "SDL2") == 0 || std::strncmp(name, "libSDL2-", 8) == 0
            || std::strncmp(name, "libSDL2.", 8) == 0;
        std::printf("POINTER_LIBRARY: %s\n", path);
        if(required && std::string(path).compare(0, std::strlen(required) + 1, std::string(required) + "/") != 0)
            throw std::runtime_error("Pointer test loaded SDL outside the packaged app");
    }
    const char* linkage = dynamicCore ? "dynamic" : "static";
    std::printf("POINTER_LINKAGE: %s\n", linkage);
    if(const char* expected = std::getenv("MENU_POINTER_LINKAGE")) {
        if(std::strcmp(expected, linkage) != 0)
            throw std::runtime_error("Pointer helper and packaged candidate use different SDL linkage");
    }
    std::fflush(stdout);
}
