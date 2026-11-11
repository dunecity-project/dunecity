#if defined(__APPLE__) && !defined(DUNECITY_IOS)
#include <filesystem>
extern "C" void nativePointerProbeBegin(SDL_Window*);
extern "C" void nativePointerProbeEvent(SDL_Window*, int, int, int);
extern "C" void nativePointerProbeCurrentPoint(SDL_Window*, int*, int*);
#include "pointer-runtime-probe.inc"

namespace PointerMappingProbe {
void settleWindow(int width, int height) {
    for(int attempt = 0; attempt < 40; ++attempt) {
        SDL_PumpEvents();
        int actualWidth = 0, actualHeight = 0;
        SDL_GetWindowSize(window, &actualWidth, &actualHeight);
        if(actualWidth == width && actualHeight == height) return;
        SDL_Delay(5);
    }
    menuRequire(false, "Native window resize did not settle at the requested size");
}


void verifyMenu(const char* phase, bool offscreen = false) {
    int ww = 0, wh = 0, ow = 0, oh = 0, lw = 0, lh = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    menuRequire(SDL_GetRendererOutputSize(renderer, &ow, &oh) == 0, "No pointer output dimensions");
    SDL_RenderGetLogicalSize(renderer, &lw, &lh);
    menuRequire(ww > 0 && wh > 0 && ow > 0 && oh > 0 && lw > 0 && lh > 0,
                "Invalid pointer presentation dimensions");
    const double scale = std::min(double(ow) / lw, double(oh) / lh);
    const double vw = std::floor(lw * scale + 0.000001), vh = std::floor(lh * scale + 0.000001);
    const double vx = (ow - vw) / 2.0, vy = (oh - vh) / 2.0;
    const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    int stationaryX = 0, stationaryY = 0;
    SDL_Point stationaryLogical{};
    if(fullscreen) {
        nativePointerProbeCurrentPoint(window, &stationaryX, &stationaryY);
        stationaryLogical = {int(std::lround((stationaryX * double(ow) / ww - vx) * lw / vw)),
                             int(std::lround((stationaryY * double(oh) / wh - vy) * lh / vh))};
        menuRequire(stationaryLogical.x >= 0 && stationaryLogical.x < lw
                    && stationaryLogical.y >= 0 && stationaryLogical.y < lh,
                    "Stationary fullscreen pointer lies outside the logical viewport");
    }
    SDL_Log("POINTER_GEOMETRY: phase=%s window=%dx%d output=%dx%d logical=%dx%d viewport=%.1f,%.1f,%.0f,%.0f fullscreen=%d",
            phase, ww, wh, ow, oh, lw, lh, vx, vy, vw, vh,
            int((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0));
    MainMenu menu;
    int clicked = -1;
    TextButton* buttons[] = {&menu.continueButton, &menu.onlineButton, &menu.campaignButton,
        &menu.customButton, &menu.randomSkirmishButton, &menu.civWarsButton, &menu.loadButton,
        &menu.optionsButton, &menu.editorsButton, &menu.modesButton, &menu.quitButton, &menu.updateButton};
    for(int i = 0; i < int(std::size(buttons)); ++i)
        buttons[i]->setOnClick([&, i]() { clicked = i; });
    for(int index : {1, 7, 10}) {
        if(fullscreen && index != 1) continue;
        if(fullscreen) {
            // Fullscreen cannot move under a stationary pointer. Exercise one
            // real MainMenu button there; normal positions/edges are covered
            // separately by the windowed own-window-origin cases below.
            for(auto* button : buttons) button->setVisible(button == buttons[index]);
            const Point size = buttons[index]->getSize();
            menu.windowWidget.setWidgetGeometry(buttons[index],
                Point(std::clamp(stationaryLogical.x - size.x / 2, 0, lw - size.x),
                      std::clamp(stationaryLogical.y - size.y / 2, 0, lh - size.y)), size);
        }
        const SDL_Rect bounds = researchMenuBounds(menu, *buttons[index]);
        menuRequire(buttons[index]->isVisible(), "Pointer target button is not visible");
        if(offscreen) menuRequire(SDL_SetRenderTarget(renderer, screenTexture) == 0, "Cannot bind gameplay-sized target");
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        menu.draw();
        const SDL_Rect marker{bounds.x + bounds.w / 2 - 3, bounds.y + bounds.h / 2 - 3, 7, 7};
        SDL_SetRenderDrawColor(renderer, 245, 1, 2, 255);
        SDL_RenderFillRect(renderer, &marker);
        if(offscreen) {
            menuRequire(SDL_SetRenderTarget(renderer, nullptr) == 0, "Cannot restore window target");
            menuRequire(SDL_RenderCopy(renderer, screenTexture, nullptr, nullptr) == 0, "Cannot present gameplay-sized target");
        }
        const int px = int(std::lround(vx + (bounds.x + bounds.w / 2) * vw / lw));
        const int py = int(std::lround(vy + (bounds.y + bounds.h / 2) * vh / lh));
        Uint32 pixel = 0;
        const SDL_Rect sample{px, py, 1, 1};
        menuRequire(SDL_RenderReadPixels(renderer, &sample, SDL_PIXELFORMAT_RGBA32, &pixel, sizeof(pixel)) == 0,
                    "Pointer render marker readback failed");
        const Uint8* rgba = reinterpret_cast<const Uint8*>(&pixel);
        menuRequire(rgba[0] == 245 && rgba[1] == 1 && rgba[2] == 2,
                    "Button render location disagrees with independent window/drawable geometry");
        presentWithCursor();
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        SDL_Point points[] = {{bounds.x + bounds.w / 2, bounds.y + bounds.h / 2},
            {bounds.x + 3, bounds.y + bounds.h / 2}, {bounds.x + bounds.w - 4, bounds.y + bounds.h / 2},
            {bounds.x - 4, bounds.y + bounds.h / 2}, {bounds.x + bounds.w + 4, bounds.y + bounds.h / 2}};
        if(fullscreen) points[0] = stationaryLogical;
        for(int pointIndex = 0; pointIndex < (fullscreen ? 1 : 5); ++pointIndex) {
            const auto point = points[pointIndex];
            // Independent presentation geometry, never the inverse SDL function
            // under test or an already-logical SDL_PushEvent fixture.
            const int wx = fullscreen ? stationaryX : int(std::lround((vx + point.x * vw / lw) * ww / ow));
            const int wy = fullscreen ? stationaryY : int(std::lround((vy + point.y * vh / lh) * wh / oh));
            const double ex = (wx * double(ow) / ww - vx) * lw / vw;
            const double ey = (wy * double(oh) / wh - vy) * lh / vh;
            const bool inside = ex >= bounds.x && ex < bounds.x + bounds.w && ey >= bounds.y && ey < bounds.y + bounds.h;
            clicked = -1;
            bool motion = false, down = false, up = false;
            if(!fullscreen) {
                nativePointerProbeEvent(window, wx > 20 ? wx - 20 : wx + 20, wy, -1);
                SDL_PumpEvents();
                SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
            }
            nativePointerProbeEvent(window, wx, wy, -1);
            nativePointerProbeEvent(window, wx, wy, 1);
            nativePointerProbeEvent(window, wx, wy, 0);
            SDL_Event event{};
            while(SDL_PollEvent(&event)) {
                if(event.type != SDL_MOUSEMOTION && event.type != SDL_MOUSEBUTTONDOWN && event.type != SDL_MOUSEBUTTONUP) continue;
                const bool moving = event.type == SDL_MOUSEMOTION;
                const Uint32 eventWindow = moving ? event.motion.windowID : event.button.windowID;
                if(eventWindow != SDL_GetWindowID(window)) continue;
                const int x = moving ? event.motion.x : event.button.x;
                const int y = moving ? event.motion.y : event.button.y;
                SDL_Log("POINTER_EVENT: phase=%s type=%u window=%d,%d expected=%.2f,%.2f actual=%d,%d target=%d inside=%d",
                        phase, event.type, wx, wy, ex, ey, x, y, index, int(inside));
                menuRequire(std::abs(x - ex) <= 2 && std::abs(y - ey) <= 2,
                            "Native SDL pointer coordinates disagree with the rendered button");
                if(moving) motion = true;
                else if(event.type == SDL_MOUSEBUTTONDOWN) down = true;
                else up = true;
                menu.doInput(event);
            }
            menuRequire(down && up && (motion || fullscreen), "Cocoa did not deliver native motion and both button events");
            if(motion) menuRequire(buttons[index]->bHover == inside, "Native pointer hover misses the visible button");
            SDL_Log("POINTER_COVERAGE: phase=%s motion=%d down=%d up=%d stationaryFullscreen=%d",
                    phase, int(motion), int(down), int(up), int(fullscreen));
            menuRequire(inside ? clicked == index : clicked != index, "Native buttons do not click the visible target");
        }
    }
}
}

// Test fixture, not production initialization: only real Vanilla defaults are
// needed to exercise original PAK artwork, fonts and the real menu input path.
// Never seed unrelated multi-gigabyte Tornie/Dune2R presentation payloads.
void initializePointerProbeVanilla() {
    char path[FILENAME_MAX];
    menuRequire(fnkdat("mods", path, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) >= 0,
                "Cannot create the isolated pointer fixture");
    auto& mods = ModManager::instance();
    mods.modsBasePath = path;
    mods.sharedBundledModsPath.clear();
    std::filesystem::create_directories(path);
    if(!mods.modExists("vanilla") || mods.vanillaNeedsReseed()) mods.seedVanillaFromDefaults();
    mods.activeMod = "vanilla";
    mods.activeContentBase = "vanilla";
    mods.activeDune2RRemasterPresentation = false;
    mods.loadActiveContentMetadata(mods.readModIni(mods.getModPath("vanilla")));
    mods.initialized = true;
}

int runPointerMappingProbe() {
    static bool applied = false;
    verifyPointerRuntime(renderer);
    nativePointerProbeBegin(window);
    if(applied) {
        menuRequire(!settings.video.fullscreen && settings.video.physicalWidth == 1440
                    && settings.video.physicalHeight == 900 && settings.video.width == 960
                    && settings.video.height == 600, "Video reinitialization did not load the windowed preset");
        menuRequire(!(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN),
                    "Video reinitialization did not actually leave fullscreen");
        PointerMappingProbe::verifyMenu("settings-apply-reinitialized-window");
        SDL_Log("MENU_PROBE_PASS: real windowed menu native motion/buttons align at centers and edges; stationary fullscreen real-button clicks, target restoration, letterbox resize and settings reinitialization align; actual backing ratios are logged");
        return MENU_QUIT_DEFAULT;
    }
    menuRequire(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN, "Pointer startup must be fullscreen");
    PointerMappingProbe::verifyMenu("fullscreen-startup");
    PointerMappingProbe::verifyMenu("render-target-roundtrip", true);
    toogleFullscreen();
    menuRequire(!(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN), "Fullscreen exit failed");
    PointerMappingProbe::verifyMenu("fullscreen-exit-windowed");
    toogleFullscreen();
    menuRequire(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN, "Fullscreen return failed");
    PointerMappingProbe::verifyMenu("fullscreen-return");
    toogleFullscreen();
    menuRequire(!(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN), "Resize fixture is not windowed");
    SDL_SetWindowSize(window, 1000, 740);
    PointerMappingProbe::settleWindow(1000, 740);
    PointerMappingProbe::verifyMenu("windowed-1000x740-letterbox");
    INIFile config(getConfigFilepath());
    config.setIntValue("Video", "Physical Width", 1440);
    config.setIntValue("Video", "Physical Height", 900);
    config.setIntValue("Video", "Width", 960);
    config.setIntValue("Video", "Height", 600);
    config.setIntValue("Video", "Interface Height", 600);
    config.setBoolValue("Video", "Fullscreen", false);
    config.saveChangesTo(getConfigFilepath());
    applied = true;
    return MENU_QUIT_REINITIALIZE;
}
#else
int runPointerMappingProbe() { throw std::runtime_error("Native pointer regression requires macOS desktop"); }
#endif
