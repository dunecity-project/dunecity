// Issue #120: start-up renderer fallback order, checked without a display.
#include <catch2/catch_test_macros.hpp>
#include <misc/RendererInitialization.h>

namespace {
constexpr Uint32 accelerated = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE;
constexpr Uint32 software = SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE;

// The drivers a Linux SDL 2.32 build compiles in, in SDL's own order.
const std::vector<StartupRenderDriver> linuxDrivers{
    {"opengl", accelerated}, {"opengles2", accelerated}, {"opengles", accelerated}, {"software", software}};

std::vector<std::string> names(const std::vector<StartupRenderDriver>& drivers, const std::vector<int>& order) {
    std::vector<std::string> result;
    for(const int index : order) result.push_back(drivers[index].name);
    return result;
}
} // namespace

TEST_CASE("Linux start-up tries every accelerated driver, then software") {
    REQUIRE(names(linuxDrivers, startupRendererOrder(linuxDrivers, "", true))
            == std::vector<std::string>{"opengl", "opengles2", "opengles", "software"});
}

TEST_CASE("Without software fallback only accelerated drivers are tried") {
    REQUIRE(names(linuxDrivers, startupRendererOrder(linuxDrivers, "", false))
            == std::vector<std::string>{"opengl", "opengles2", "opengles"});
}

TEST_CASE("An explicit SDL_RENDER_DRIVER is tried first and not repeated") {
    REQUIRE(names(linuxDrivers, startupRendererOrder(linuxDrivers, "opengles2", true))
            == std::vector<std::string>{"opengles2", "opengl", "opengles", "software"});
    // Driver names match case-insensitively, as SDL's own hint lookup does.
    REQUIRE(names(linuxDrivers, startupRendererOrder(linuxDrivers, "OpenGL", false))
            == std::vector<std::string>{"opengl", "opengles2", "opengles"});
}

TEST_CASE("An explicit software request is honoured even without automatic fallback") {
    REQUIRE(names(linuxDrivers, startupRendererOrder(linuxDrivers, "software", false))
            == std::vector<std::string>{"software", "opengl", "opengles2", "opengles"});
}

TEST_CASE("An unknown requested driver falls back to the built-in order") {
    REQUIRE(names(linuxDrivers, startupRendererOrder(linuxDrivers, "vulkan", true))
            == std::vector<std::string>{"opengl", "opengles2", "opengles", "software"});
}

TEST_CASE("A build with only GLES2 and software still reaches software (the 1.0.864 AppImage)") {
    const std::vector<StartupRenderDriver> drivers{{"opengles2", accelerated}, {"software", software}};
    REQUIRE(names(drivers, startupRendererOrder(drivers, "", true))
            == std::vector<std::string>{"opengles2", "software"});
}

TEST_CASE("Drivers without target textures or with unreadable info are skipped") {
    const std::vector<StartupRenderDriver> drivers{
        {"opengl", SDL_RENDERER_ACCELERATED}, {}, {"opengles2", accelerated}, {"software", SDL_RENDERER_SOFTWARE}};
    REQUIRE(names(drivers, startupRendererOrder(drivers, "", true)) == std::vector<std::string>{"opengles2"});
}
