#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include "pointer-runtime-probe.inc"

extern "C" void nativePointerProbeBegin(SDL_Window*);
extern "C" void nativePointerProbeEvent(SDL_Window*, int, int, int);
extern "C" void nativePointerProbeCurrentPoint(SDL_Window*, int*, int*);

static void require(bool value, const char* message) {
    if(!value) throw std::runtime_error(message);
}

static void verify(SDL_Window* window, SDL_Renderer* renderer, const char* phase) {
    int ww = 0, wh = 0, ow = 0, oh = 0, lw = 0, lh = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    require(SDL_GetRendererOutputSize(renderer, &ow, &oh) == 0, "Missing drawable size");
    SDL_RenderGetLogicalSize(renderer, &lw, &lh);
    const double scale = std::min(double(ow) / lw, double(oh) / lh);
    const double vw = std::floor(lw * scale + 0.000001), vh = std::floor(lh * scale + 0.000001);
    const double vx = (ow - vw) / 2.0, vy = (oh - vh) / 2.0;
    std::printf("POINTER_GEOMETRY: phase=%s window=%dx%d output=%dx%d logical=%dx%d viewport=%.1f,%.1f,%.0f,%.0f\n",
                phase, ww, wh, ow, oh, lw, lh, vx, vy, vw, vh);
    const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_Point points[] = {{31, 29}, {lw / 2, lh / 2}, {lw - 31, lh - 29}};
    int stationaryX = 0, stationaryY = 0;
    if(fullscreen) {
        nativePointerProbeCurrentPoint(window, &stationaryX, &stationaryY);
        points[0] = {int(std::lround((stationaryX * double(ow) / ww - vx) * lw / vw)),
                     int(std::lround((stationaryY * double(oh) / wh - vy) * lh / vh))};
    }
    for(int i = 0; i < (fullscreen ? 1 : 3); ++i) {
        const auto point = points[i];
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_Rect target{point.x - 8, point.y - 8, 17, 17};
        require(target.x >= 0 && target.y >= 0 && target.x + target.w <= lw && target.y + target.h <= lh,
                "Stationary fullscreen pointer lies outside the usable logical viewport");
        SDL_SetRenderDrawColor(renderer, 245, 1, 2, 255);
        require(SDL_RenderFillRect(renderer, &target) == 0, "Cannot render pointer target");
        const int px = int(std::lround(vx + point.x * vw / lw));
        const int py = int(std::lround(vy + point.y * vh / lh));
        Uint32 pixel = 0;
        SDL_Rect sample{px, py, 1, 1};
        require(SDL_RenderReadPixels(renderer, &sample, SDL_PIXELFORMAT_RGBA32, &pixel, sizeof(pixel)) == 0,
                "Cannot read rendered pointer target");
        const auto* rgba = reinterpret_cast<const Uint8*>(&pixel);
        require(rgba[0] == 245 && rgba[1] == 1 && rgba[2] == 2, "Rendered target disagrees with window/drawable geometry");
        SDL_RenderPresent(renderer);
        const int wx = fullscreen ? stationaryX : int(std::lround(px * double(ww) / ow));
        const int wy = fullscreen ? stationaryY : int(std::lround(py * double(wh) / oh));
        const double ex = (wx * double(ow) / ww - vx) * lw / vw;
        const double ey = (wy * double(oh) / wh - vy) * lh / vh;
        if(!fullscreen) {
            nativePointerProbeEvent(window, wx > 20 ? wx - 20 : wx + 20, wy, -1);
            SDL_PumpEvents();
            SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        }
        nativePointerProbeEvent(window, wx, wy, -1);
        nativePointerProbeEvent(window, wx, wy, 1);
        nativePointerProbeEvent(window, wx, wy, 0);
        bool motion = false, down = false, up = false;
        SDL_Event event{};
        while(SDL_PollEvent(&event)) {
            if(event.type != SDL_MOUSEMOTION && event.type != SDL_MOUSEBUTTONDOWN && event.type != SDL_MOUSEBUTTONUP) continue;
            const bool moving = event.type == SDL_MOUSEMOTION;
            const Uint32 id = moving ? event.motion.windowID : event.button.windowID;
            if(id != SDL_GetWindowID(window)) continue;
            const int x = moving ? event.motion.x : event.button.x;
            const int y = moving ? event.motion.y : event.button.y;
            std::printf("POINTER_EVENT: phase=%s type=%u window=%d,%d expected=%.2f,%.2f actual=%d,%d\n",
                        phase, event.type, wx, wy, ex, ey, x, y);
            require(std::abs(x - ex) <= 2 && std::abs(y - ey) <= 2, "Native SDL event misses the rendered target");
            const SDL_Point hit{x, y};
            require(SDL_PointInRect(&hit, &target), "Native button/motion hit test misses the rendered target");
            if(moving) motion = true;
            else if(event.type == SDL_MOUSEBUTTONDOWN) down = true;
            else up = true;
        }
        require(down && up && (motion || fullscreen), "Native Cocoa motion or button events were not delivered");
        std::printf("POINTER_COVERAGE: phase=%s motion=%d down=%d up=%d stationaryFullscreen=%d\n",
                    phase, int(motion), int(down), int(up), int(fullscreen));
    }
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // The game's macOS presentation and mouse hints (src/main.cpp).
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "POINTER_FAIL: SDL initialization: %s\n", SDL_GetError());
        return 1;
    }
    using WindowPtr = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
    using RendererPtr = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;
    constexpr Uint32 rendererFlags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;
    try {
        {
            // The reported profile (5120x2880 request, 1066x600 interface, Fullscreen=true)
            // starts straight in desktop fullscreen on a smaller, possibly Retina, display.
            WindowPtr window{SDL_CreateWindow("DuneCity native pointer fullscreen startup",
                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 5120, 2880,
                SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI), SDL_DestroyWindow};
            RendererPtr renderer{window ? SDL_CreateRenderer(window.get(), -1, rendererFlags) : nullptr,
                SDL_DestroyRenderer};
            require(window && renderer, SDL_GetError());
            require(SDL_RenderSetLogicalSize(renderer.get(), 1066, 600) == 0, "Cannot set logical presentation");
            verifyPointerRuntime(renderer.get());
            nativePointerProbeBegin(window.get());
            SDL_PumpEvents();
            require(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN, "Fullscreen startup did not settle");
            verify(window.get(), renderer.get(), "fullscreen-desktop-startup-5120x2880-request");
        }
        WindowPtr window{SDL_CreateWindow("DuneCity native pointer phase 0",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1000, 740,
            SDL_WINDOW_ALLOW_HIGHDPI), SDL_DestroyWindow};
        RendererPtr renderer{window ? SDL_CreateRenderer(window.get(), -1, rendererFlags) : nullptr,
            SDL_DestroyRenderer};
        require(window && renderer, SDL_GetError());
        require(SDL_RenderSetLogicalSize(renderer.get(), 1066, 600) == 0, "Cannot set logical presentation");
        nativePointerProbeBegin(window.get());
        verify(window.get(), renderer.get(), "windowed-letterbox-native-origin");
        require(SDL_SetWindowFullscreen(window.get(), SDL_WINDOW_FULLSCREEN_DESKTOP) == 0, "Cannot enter fullscreen");
        SDL_PumpEvents();
        require(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN, "Fullscreen entry did not settle");
        verify(window.get(), renderer.get(), "fullscreen-stationary-native-buttons");
        require(SDL_SetWindowFullscreen(window.get(), 0) == 0, "Cannot leave fullscreen");
        SDL_PumpEvents();
        require(!(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN), "Fullscreen exit did not settle");
        require(SDL_RenderSetLogicalSize(renderer.get(), 960, 600) == 0, "Cannot change logical presentation");
        SDL_SetWindowSize(window.get(), 1440, 900);
        SDL_PumpEvents();
        verify(window.get(), renderer.get(), "windowed-1440x900-960x600");
        std::puts("MENU_PROBE_PASS: native Cocoa motion/buttons queued through SDL's own pump match rendered targets at fullscreen-desktop startup with an oversize request, under own-window origin changes, after fullscreen entry and after a 1440x900 resize; no physical pointer movement");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr, "POINTER_FAIL: %s; SDL=%s\n", error.what(), SDL_GetError());
        return 1;
    }
}
