#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include "pointer-runtime-probe.inc"

extern "C" void nativePointerProbeBegin(SDL_Window*);
extern "C" void nativePointerProbeEvent(SDL_Window*, int, int, int);

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
    const SDL_Point points[] = {{31, 29}, {lw / 2, lh / 2}, {lw - 31, lh - 29}};
    for(const auto point : points) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_Rect target{point.x - 8, point.y - 8, 17, 17};
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
        const int wx = int(std::lround(px * double(ww) / ow));
        const int wy = int(std::lround(py * double(wh) / oh));
        const double ex = (wx * double(ow) / ww - vx) * lw / vw;
        const double ey = (wy * double(oh) / wh - vy) * lh / vh;
        nativePointerProbeEvent(window, wx > 20 ? wx - 20 : wx + 20, wy, -1);
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
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
        require(motion && down && up, "Native Cocoa motion or button events were not delivered");
    }
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 1;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{SDL_CreateWindow("DuneCity native pointer phase 0",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 5120, 2880,
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_FULLSCREEN_DESKTOP), SDL_DestroyWindow};
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer{
        window ? SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE) : nullptr,
        SDL_DestroyRenderer};
    try {
        require(window && renderer, SDL_GetError());
        require(SDL_RenderSetLogicalSize(renderer.get(), 1066, 600) == 0, "Cannot set logical presentation");
        verifyPointerRuntime(renderer.get());
        nativePointerProbeBegin(window.get());
        verify(window.get(), renderer.get(), "fullscreen-startup");
        require(SDL_SetWindowFullscreen(window.get(), 0) == 0, "Cannot enter windowed mode");
        require(!(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN), "Fullscreen exit did not settle");
        SDL_SetWindowSize(window.get(), 1000, 740);
        SDL_PumpEvents();
        int width = 0, height = 0;
        SDL_GetWindowSize(window.get(), &width, &height);
        require(width == 1000 && height == 740, "Requested window resize did not occur");
        verify(window.get(), renderer.get(), "windowed-letterbox");
        require(SDL_RenderSetLogicalSize(renderer.get(), 960, 600) == 0, "Cannot change logical presentation");
        SDL_SetWindowSize(window.get(), 1440, 900);
        SDL_PumpEvents();
        verify(window.get(), renderer.get(), "windowed-1440x900-960x600");
        std::puts("MENU_PROBE_PASS: SDL-only native Cocoa motion/buttons agree with independently rendered targets and window/drawable/logical transitions");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr, "POINTER_FAIL: %s; SDL=%s\n", error.what(), SDL_GetError());
        return 1;
    }
}
