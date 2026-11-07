#include <catch2/catch_test_macros.hpp>
#include <misc/RendererInitialization.h>
#include <memory>

namespace {
struct DummyVideo {
    DummyVideo() {
        SDL_SetHintWithPriority(SDL_HINT_VIDEODRIVER, "dummy", SDL_HINT_OVERRIDE);
        SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "", SDL_HINT_OVERRIDE);
        REQUIRE(SDL_Init(SDL_INIT_VIDEO) == 0);
    }
    ~DummyVideo() { SDL_Quit(); SDL_ClearHints(); }
};
using Window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
using Renderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;
}

TEST_CASE("Missing Linux acceleration recovers with a real target-capable software renderer") {
    DummyVideo video;
    Window window{SDL_CreateWindow("Renderer regression", 0, 0, 16, 16, SDL_WINDOW_HIDDEN), SDL_DestroyWindow};
    REQUIRE(window);
    REQUIRE(createStartupRenderer(window.get(), false) == nullptr);
    Renderer renderer{createStartupRenderer(window.get(), true), SDL_DestroyRenderer};
    REQUIRE(renderer);
    SDL_RendererInfo info{};
    REQUIRE(SDL_GetRendererInfo(renderer.get(), &info) == 0);
    REQUIRE(std::string(info.name) == "software");
    REQUIRE(SDL_RenderTargetSupported(renderer.get()) == SDL_TRUE);
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture{
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 16, 16),
        SDL_DestroyTexture};
    REQUIRE(texture);
    REQUIRE(SDL_SetRenderTarget(renderer.get(), texture.get()) == 0);
    REQUIRE(SDL_SetRenderDrawColor(renderer.get(), 17, 31, 63, 255) == 0);
    REQUIRE(SDL_RenderClear(renderer.get()) == 0);
    REQUIRE(SDL_SetRenderTarget(renderer.get(), nullptr) == 0);
    REQUIRE(SDL_RenderCopy(renderer.get(), texture.get(), nullptr, nullptr) == 0);
    Uint8 pixel[4]{};
    const SDL_Rect sample{0, 0, 1, 1};
    REQUIRE(SDL_RenderReadPixels(renderer.get(), &sample, SDL_PIXELFORMAT_RGBA32, pixel, 4) == 0);
    REQUIRE(pixel[0] == 17);
    REQUIRE(pixel[1] == 31);
    REQUIRE(pixel[2] == 63);
    REQUIRE(pixel[3] == 255);
}

TEST_CASE("Explicit software preference is preserved without automatic fallback") {
    DummyVideo video;
    SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "software", SDL_HINT_OVERRIDE);
    configureStartupRenderHints();
    REQUIRE(std::string(SDL_GetHint(SDL_HINT_RENDER_DRIVER)) == "software");
    Window window{SDL_CreateWindow("Renderer preference", 0, 0, 16, 16, SDL_WINDOW_HIDDEN), SDL_DestroyWindow};
    REQUIRE(window);
    Renderer renderer{createStartupRenderer(window.get(), false), SDL_DestroyRenderer};
    REQUIRE(renderer);
    SDL_RendererInfo info{};
    REQUIRE(SDL_GetRendererInfo(renderer.get(), &info) == 0);
    REQUIRE(std::string(info.name) == "software");
}

TEST_CASE("Total renderer failure preserves both errors without disturbing an existing renderer") {
    DummyVideo video;
    Window window{SDL_CreateWindow("Renderer failure", 0, 0, 16, 16, SDL_WINDOW_HIDDEN), SDL_DestroyWindow};
    REQUIRE(window);
    Renderer renderer{createStartupRenderer(window.get(), true), SDL_DestroyRenderer};
    REQUIRE(renderer);
    REQUIRE(createStartupRenderer(window.get(), true) == nullptr);
    const std::string error = SDL_GetError();
    REQUIRE(error.find("Accelerated renderer:") != std::string::npos);
    REQUIRE(error.find("software renderer:") != std::string::npos);
    REQUIRE(SDL_RenderClear(renderer.get()) == 0);
}

TEST_CASE("A null window fails safely") {
    REQUIRE(createStartupRenderer(nullptr, true) == nullptr);
    REQUIRE(std::string(SDL_GetError()).find("without a window") != std::string::npos);
}
