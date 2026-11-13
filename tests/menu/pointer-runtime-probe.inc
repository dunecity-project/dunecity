#include <SDL.h>
#include <mach-o/dyld.h>
#include <dlfcn.h>
#include <limits.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

void verifyPointerRuntime(SDL_Renderer* renderer) {
    SDL_version version{};
    SDL_GetVersion(&version);
    SDL_RendererInfo info{};
    const bool rendererReady = SDL_GetRendererInfo(renderer, &info) == 0;
    const char* driver = SDL_GetCurrentVideoDriver();
    std::printf("POINTER_RUNTIME: SDL=%d.%d.%d renderer=%s driver=%s\n", version.major, version.minor,
                version.patch, info.name ? info.name : "(unavailable)", driver ? driver : "(unavailable)");
    const char* required = std::getenv("MENU_POINTER_RUNTIME");
    bool loadedSDL = false, foreignSDL = false;
    for(uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const char* path = _dyld_get_image_name(i);
        const char* name = path ? std::strrchr(path, '/') : nullptr;
        name = name ? name + 1 : path;
        if(!name || (std::strncmp(name, "libSDL", 6) != 0 && std::strncmp(name, "SDL", 3) != 0)) continue;
        loadedSDL = true;
        std::printf("POINTER_LIBRARY: %s\n", path);
        char canonical[PATH_MAX];
        foreignSDL |= !required || !realpath(path, canonical)
            || std::string(canonical).compare(0, std::strlen(required) + 1, std::string(required) + "/") != 0;
    }
    Dl_info symbol{};
    const bool symbolReady = dladdr(reinterpret_cast<const void*>(&SDL_GetVersion), &symbol) != 0;
    const bool symbolIsSelf = symbolReady && symbol.dli_fbase == _dyld_get_image_header(0);
    const char* linkage = symbolIsSelf ? "static" : "dynamic";
    std::printf("POINTER_SYMBOL: SDL_GetVersion=%s self=%d\n",
                symbolReady && symbol.dli_fname ? symbol.dli_fname : "(unresolved)", int(symbolIsSelf));
    std::printf("POINTER_LINKAGE: %s\n", linkage);
    // Flush complete runtime/library/symbol evidence even when a prerequisite
    // fails; an early exception must not hide the foreign SDL that caused it.
    std::fflush(stdout);
    const auto requireRuntime = [](bool condition, const char* reason) {
        if(!condition) throw std::runtime_error(std::string("POINTER_PREREQUISITE_BLOCKED: ") + reason);
    };
    requireRuntime(rendererReady && driver && std::strcmp(driver, "cocoa") == 0,
                   "Pointer test requires the native Cocoa renderer");
    requireRuntime(required && *required, "Pointer test requires an explicit packaged candidate");
    requireRuntime(!foreignSDL, "Pointer test loaded SDL outside the packaged app");
    const char* expected = std::getenv("MENU_POINTER_LINKAGE");
    requireRuntime(symbolReady && expected && std::strcmp(expected, linkage) == 0,
                   "Pointer helper and packaged candidate use different SDL linkage");
    if(symbolIsSelf) {
        const char* receipt = std::getenv("MENU_POINTER_VERIFIED_RECEIPT");
        const bool verified = receipt && std::strlen(receipt) == 64 && std::strspn(receipt, "0123456789abcdef") == 64;
        requireRuntime(verified && !loadedSDL, "Static SDL requires a verified producer receipt and no dynamically loaded SDL");
        char actual[32];
        std::snprintf(actual, sizeof(actual), "%d.%d.%d", version.major, version.minor, version.patch);
        const char* expectedVersion = std::getenv("MENU_POINTER_SDL_VERSION");
        requireRuntime(expectedVersion && std::strcmp(actual, expectedVersion) == 0,
                       "Static SDL runtime differs from the producer archive/header receipt");
        std::printf("POINTER_PRODUCER_RECEIPT: sha256=%s actualPackageExecutable=0\n", receipt);
    } else {
        char canonical[PATH_MAX];
        requireRuntime(symbol.dli_fname && realpath(symbol.dli_fname, canonical)
                       && std::string(canonical).compare(0, std::strlen(required) + 1, std::string(required) + "/") == 0,
                       "SDL_GetVersion resolves outside the packaged app");
    }
    std::fflush(stdout);
}
