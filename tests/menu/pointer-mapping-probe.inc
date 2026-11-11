#if defined(__APPLE__) && !defined(DUNECITY_IOS)
extern "C" void nativePointerProbeBegin(SDL_Window*);
extern "C" void nativePointerProbeEvent(SDL_Window*, int, int, int);
extern "C" void nativePointerProbeEnd();

namespace PointerMappingProbe {
void verifyRuntime() {
    SDL_version version{};
    SDL_GetVersion(&version);
    SDL_RendererInfo info{};
    menuRequire(SDL_GetRendererInfo(renderer, &info) == 0, "Pointer renderer is unavailable");
    menuRequire(std::string(SDL_GetCurrentVideoDriver()) == "cocoa", "Pointer test must use native Cocoa");
    SDL_Log("POINTER_RUNTIME: SDL=%d.%d.%d renderer=%s", version.major, version.minor, version.patch, info.name);
    const char* required = std::getenv("MENU_POINTER_RUNTIME");
    bool dynamicSDL = false;
    for(uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const char* path = _dyld_get_image_name(i);
        const char* name = path ? std::strrchr(path, '/') : nullptr;
        name = name ? name + 1 : path;
        if(!name || (std::strncmp(name, "libSDL", 6) != 0 && std::strncmp(name, "SDL", 3) != 0)) continue;
        dynamicSDL = true;
        SDL_Log("POINTER_LIBRARY: %s", path);
        menuRequire(!required || std::string(path).compare(0, std::strlen(required) + 1,
                    std::string(required) + "/") == 0, "Pointer test loaded SDL outside the packaged app");
    }
    SDL_Log("POINTER_LINKAGE: %s", dynamicSDL ? "dynamic" : "static");
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
        const SDL_Point points[] = {{bounds.x + bounds.w / 2, bounds.y + bounds.h / 2},
            {bounds.x + 3, bounds.y + bounds.h / 2}, {bounds.x + bounds.w - 4, bounds.y + bounds.h / 2},
            {bounds.x - 4, bounds.y + bounds.h / 2}, {bounds.x + bounds.w + 4, bounds.y + bounds.h / 2}};
        for(const auto point : points) {
            // Independent presentation geometry, never the inverse SDL function
            // under test or an already-logical SDL_PushEvent fixture.
            const int wx = int(std::lround((vx + point.x * vw / lw) * ww / ow));
            const int wy = int(std::lround((vy + point.y * vh / lh) * wh / oh));
            const double ex = (wx * double(ow) / ww - vx) * lw / vw;
            const double ey = (wy * double(oh) / wh - vy) * lh / vh;
            const bool inside = ex >= bounds.x && ex < bounds.x + bounds.w && ey >= bounds.y && ey < bounds.y + bounds.h;
            clicked = -1;
            bool motion = false, down = false, up = false;
            nativePointerProbeEvent(window, wx, wy, -1);
            nativePointerProbeEvent(window, wx, wy, 1);
            nativePointerProbeEvent(window, wx, wy, 0);
            SDL_Event event{};
            while(SDL_PollEvent(&event)) {
                if(event.type != SDL_MOUSEMOTION && event.type != SDL_MOUSEBUTTONDOWN && event.type != SDL_MOUSEBUTTONUP) continue;
                const bool moving = event.type == SDL_MOUSEMOTION;
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
            menuRequire(motion && down && up, "Cocoa did not deliver native motion and both button events");
            menuRequire(buttons[index]->bHover == inside, "Native pointer hover misses the visible button");
            menuRequire(inside ? clicked == index : clicked != index, "Native buttons do not click the visible target");
        }
    }
}
}

int runPointerMappingProbe() {
    PointerMappingProbe::verifyRuntime();
    nativePointerProbeBegin(window);
    struct RestoreNativePointer { ~RestoreNativePointer() { nativePointerProbeEnd(); } } restore;
    PointerMappingProbe::verifyMenu("startup");
    PointerMappingProbe::verifyMenu("render-target-roundtrip", true);
    toogleFullscreen();
    PointerMappingProbe::verifyMenu("fullscreen-roundtrip-enter");
    toogleFullscreen();
    PointerMappingProbe::verifyMenu("fullscreen-roundtrip-return");
    SDL_SetWindowSize(window, 1000, 740);
    SDL_PumpEvents();
    PointerMappingProbe::verifyMenu("live-window-resize-letterbox");
    SDL_Log("MENU_PROBE_PASS: native Cocoa motion/buttons align with real menu targets, edges, Retina backing pixels, render-target and fullscreen/window-resize transitions");
    return MENU_QUIT_DEFAULT;
}
#else
int runPointerMappingProbe() { throw std::runtime_error("Native pointer regression requires macOS desktop"); }
#endif
