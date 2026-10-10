#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <misc/MenuLayout.h>
#include <misc/MenuPalette.h>
#include <FileClasses/INIFile.h>

TEST_CASE("Retina backing pixels support HD choices beyond desktop coordinates", "[menu][display][retina]") {
    const auto retina = displayVideoResolutionLimit({1440,900}, {1440,900}, {2880,1800});
    REQUIRE(retina.x == 2880);
    REQUIRE(retina.y == 1800);
    REQUIRE(1920 <= retina.x);
    REQUIRE(1080 <= retina.y);
    const auto ordinary = displayVideoResolutionLimit({1440,900}, {1440,900}, {1440,900});
    REQUIRE(ordinary.x == 1440);
    REQUIRE(ordinary.y == 900);
    const auto unavailable = displayVideoResolutionLimit({1440,900}, {0,0}, {0,0});
    REQUIRE(unavailable.x == 1440);
    REQUIRE(unavailable.y == 900);
}

TEST_CASE("Fitting an HD window preserves its shape and selected render size", "[menu][display][retina]") {
    const SDL_Point requested{1920,1080};
    for(const auto available : {SDL_Point{1440,900}, SDL_Point{1440,850}, SDL_Point{1024,768}}) {
        const auto window = fitVideoWindow(requested, available);
        REQUIRE(window.x <= available.x);
        REQUIRE(window.y <= available.y);
        REQUIRE(std::abs(window.x * 1080 - window.y * 1920) <= 1920);
        const auto logical = interfaceLogicalSize(INTERFACE_HEIGHT_NATIVE, requested.x, requested.y);
        REQUIRE(logical.x == 1920);
        REQUIRE(logical.y == 1080);
    }
    const auto unchanged = fitVideoWindow({1280,720}, {1440,900});
    REQUIRE(unchanged.x == 1280);
    REQUIRE(unchanged.y == 720);
    const auto unknown = fitVideoWindow(requested, {0,0});
    REQUIRE(unknown.x == requested.x);
    REQUIRE(unknown.y == requested.y);
}

TEST_CASE("Start menus retain readable targets and clear artwork at supported resolutions", "[menu][display]") {
    for(const auto size : {SDL_Point{640,480}, SDL_Point{800,600}, SDL_Point{1024,768},
                           SDL_Point{960,540}, SDL_Point{1280,720}, SDL_Point{1920,1080}}) {
        for(int buttonCount : {6,8}) {
            const StartMenuLayout layout{size.x, size.y, buttonCount};
            REQUIRE(layout.artBounds().h >= 100);
            const auto border = layout.borderBounds();
            REQUIRE(border.x >= 0);
            REQUIRE(border.x + border.w <= size.x);
            for(int i = 0; i < buttonCount; ++i) {
                const auto button = layout.button(i);
                REQUIRE(button.w >= 220);
                REQUIRE(button.h >= 28);
                REQUIRE(button.x >= 24);
                REQUIRE(button.x + button.w <= size.x - 24);
                REQUIRE(button.y >= layout.artBounds().y + layout.artBounds().h);
                REQUIRE(button.y + button.h <= size.y - layout.bottomMargin());
                for(int j = 0; j < i; ++j) {
                    const auto other = layout.button(j);
                    REQUIRE_FALSE(SDL_HasIntersection(&button, &other));
                }
            }
        }
    }
}

TEST_CASE("Android preserves chosen interface size and repairs old native resolution values", "[menu][display]") {
    for(int size : {480,600,768}) {
        REQUIRE(validatedInterfaceHeight(size, true) == size);
        REQUIRE(validatedInterfaceHeight(size, false) == size);
    }
    for(int old : {0,2000,2800}) {
        REQUIRE(validatedInterfaceHeight(old, true) == 480);
        REQUIRE(validatedInterfaceHeight(old, false) == 0);
    }
    // Native 1:1 is a desktop/browser choice; Android keeps a fixed interface.
    REQUIRE(validatedInterfaceHeight(INTERFACE_HEIGHT_NATIVE, false) == INTERFACE_HEIGHT_NATIVE);
    REQUIRE(validatedInterfaceHeight(INTERFACE_HEIGHT_NATIVE, true) == 480);
    for(int height : {480,600,768}) {
        const int classic = interfaceWidthForHeight(height, false);
        const int wide = interfaceWidthForHeight(height, true);
        REQUIRE(isSupportedInterfaceResolution(classic, height));
        REQUIRE(isSupportedInterfaceResolution(wide, height));
        REQUIRE(validatedInterfaceWidth(classic, height) == classic);
        REQUIRE(validatedInterfaceWidth(wide, height) == wide);
        REQUIRE(validatedInterfaceWidth(9999, height) == classic);
        REQUIRE(static_cast<double>(wide) / height > 1.77);
        REQUIRE(static_cast<double>(wide) / height < 1.78);
    }
}

TEST_CASE("1920x1080 can be played 1:1 while the enlarged choices keep their size", "[menu][display][scale]") {
    // Automatic is the upstream 0.96.4 downscale ("Use a smaller logical
    // resolution for high resolution displays, e.g. 960x540 for 1920x1080"):
    // the whole interface is drawn at 2x.
    REQUIRE(getLogicalToPhysicalResolutionFactor(1920, 1080) == 2);
    const auto automatic = interfaceLogicalSize(0, 1920, 1080);
    REQUIRE(automatic.x == 960);
    REQUIRE(automatic.y == 540);

    // The smallest preset was as close to 1:1 as a player could get, and it is
    // an uneven upscale.
    const auto small = interfaceLogicalSize(768, 1920, 1080);
    REQUIRE(small.x == 1364);
    REQUIRE(small.y == 768);
    REQUIRE(1920 % small.x != 0);

    // Native is exactly one interface pixel per screen pixel.
    const auto native = interfaceLogicalSize(INTERFACE_HEIGHT_NATIVE, 1920, 1080);
    REQUIRE(native.x == 1920);
    REQUIRE(native.y == 1080);

    // Explicitly chosen larger interfaces are untouched by the new option.
    const auto large = interfaceLogicalSize(480, 1920, 1080);
    REQUIRE(large.x == 852);
    REQUIRE(large.y == 480);
    const auto medium = interfaceLogicalSize(600, 1920, 1080);
    REQUIRE(medium.x == 1066);
    REQUIRE(medium.y == 600);
    for(int preset : {480, 600, 768}) {
        const auto logical = interfaceLogicalSize(preset, 1920, 1080);
        REQUIRE(logical.y == preset);
    }

    // The defect: before native existed, no Interface Size choice could put one
    // interface pixel on one screen pixel at 1920x1080.
    int choicesThatAre1to1 = 0;
    for(int choice : {0, 480, 600, 768}) {
        const auto logical = interfaceLogicalSize(choice, 1920, 1080);
        if(logical.x == 1920 && logical.y == 1080) choicesThatAre1to1++;
    }
    REQUIRE(choicesThatAre1to1 == 0);
    REQUIRE(interfaceLogicalSize(INTERFACE_HEIGHT_NATIVE, 1920, 1080).x == 1920);
}

TEST_CASE("Native 1:1 follows the presented surface on small screens, HiDPI and resize", "[menu][display][scale]") {
    // Below the enlargement thresholds nothing changes: automatic already is 1:1.
    for(const auto size : {SDL_Point{640,480}, SDL_Point{800,600}, SDL_Point{1024,768}, SDL_Point{1280,720}}) {
        REQUIRE(getLogicalToPhysicalResolutionFactor(size.x, size.y) == 1);
        const auto automatic = interfaceLogicalSize(0, size.x, size.y);
        const auto native = interfaceLogicalSize(INTERFACE_HEIGHT_NATIVE, size.x, size.y);
        REQUIRE(automatic.x == size.x);
        REQUIRE(automatic.y == size.y);
        REQUIRE(native.x == automatic.x);
        REQUIRE(native.y == automatic.y);
    }

    // First size that is enlarged automatically, and the 3x step.
    const auto smallAutomatic = interfaceLogicalSize(0, 1280, 960);
    REQUIRE(smallAutomatic.x == 640);
    REQUIRE(smallAutomatic.y == 480);
    const auto smallNative = interfaceLogicalSize(INTERFACE_HEIGHT_NATIVE, 1280, 960);
    REQUIRE(smallNative.x == 1280);
    REQUIRE(smallNative.y == 960);
    const auto uhdAutomatic = interfaceLogicalSize(0, 3840, 2160);
    REQUIRE(uhdAutomatic.x == 1280);
    REQUIRE(uhdAutomatic.y == 720);

    // Never below the minimum interface size, however small the surface is.
    for(const auto size : {SDL_Point{640,400}, SDL_Point{320,240}, SDL_Point{0,0}}) {
        const auto native = interfaceLogicalSize(INTERFACE_HEIGHT_NATIVE, size.x, size.y);
        REQUIRE(native.x >= SCREEN_MIN_WIDTH);
        REQUIRE(native.y >= SCREEN_MIN_HEIGHT);
    }

    // The presented size is in screen coordinates, so a HiDPI window keeps the
    // interface the player chose; SDL draws it into the denser pixel surface.
    const auto retinaWindow = interfaceLogicalSize(INTERFACE_HEIGHT_NATIVE, 1280, 800);
    REQUIRE(retinaWindow.x == 1280);
    REQUIRE(retinaWindow.y == 800);
    REQUIRE(retinaWindow.x != 2560);
    REQUIRE(interfaceLogicalSize(600, 1280, 800).y == 600);

    // Window, fullscreen-desktop and a resized window all derive from whatever
    // is presented at that moment.
    for(const auto presented : {SDL_Point{1280,800}, SDL_Point{1920,1080}, SDL_Point{2560,1440}, SDL_Point{1600,900}}) {
        const auto native = interfaceLogicalSize(INTERFACE_HEIGHT_NATIVE, presented.x, presented.y);
        REQUIRE(native.x == presented.x);
        REQUIRE(native.y == presented.y);
        const auto preset = interfaceLogicalSize(600, presented.x, presented.y);
        REQUIRE(preset.y == 600);
        REQUIRE(std::abs(static_cast<double>(preset.x) / preset.y
                         - static_cast<double>(presented.x) / presented.y) < 0.01);
    }
}

TEST_CASE("Existing interface size configurations keep their meaning", "[menu][display][scale]") {
    INIFile config(true, "Interface size preference test");
    // An untouched or older configuration file still means automatic.
    REQUIRE(validatedInterfaceHeight(config.getIntValue("Video", "Interface Height", 0), false) == 0);
    for(int selected : {480, 600, 768, 0, INTERFACE_HEIGHT_NATIVE}) {
        config.setIntValue("Video", "Interface Height", selected);
        const int stored = validatedInterfaceHeight(config.getIntValue("Video", "Interface Height", 0), false);
        REQUIRE(stored == selected);
        const auto logical = interfaceLogicalSize(stored, 1920, 1080);
        REQUIRE(logical.x >= SCREEN_MIN_WIDTH);
        REQUIRE(logical.y >= SCREEN_MIN_HEIGHT);
        REQUIRE(logical.y == (selected > 0 ? selected : (selected == 0 ? 540 : 1080)));
    }
}

TEST_CASE("Dark menu colors default safely and high contrast remains opt in", "[menu][accessibility]") {
    const auto desert = menuPalette(0);
    REQUIRE(desert.foreground != COLOR_BLACK);
    REQUIRE(desert.shadow == COLOR_TRANSPARENT);
    const auto contrast = menuPalette(1);
    REQUIRE(contrast.foreground == COLOR_WHITE);
    REQUIRE(contrast.shadow == COLOR_TRANSPARENT);
    for(int invalid : {-99, -1, 2, 100}) {
        REQUIRE(validatedMenuPalette(invalid) == 0);
        REQUIRE(menuPalette(invalid).foreground == desert.foreground);
    }
    INIFile config(true, "Menu color preference test");
    REQUIRE(validatedMenuPalette(config.getIntValue("Video", "Menu Palette", 0)) == 0);
    for(int selected : {1, 0}) {
        config.setIntValue("Video", "Menu Palette", selected);
        REQUIRE(validatedMenuPalette(config.getIntValue("Video", "Menu Palette", 0)) == selected);
    }
}

TEST_CASE("Classic start menus default safely and enlarged remains opt in", "[menu][accessibility]") {
    for(int invalid : {-99, -1, 2, 100}) {
        REQUIRE(validatedStartMenuMode(invalid) == 0);
    }
    REQUIRE(validatedStartMenuMode(0) == 0);
    REQUIRE(validatedStartMenuMode(1) == 1);

    INIFile config(true, "Start menu mode preference test");
    REQUIRE(validatedStartMenuMode(config.getIntValue("Video", "Start Menu Mode", 0)) == 0);
    for(int selected : {1, 0}) {
        config.setIntValue("Video", "Start Menu Mode", selected);
        REQUIRE(validatedStartMenuMode(config.getIntValue("Video", "Start Menu Mode", 0)) == selected);
    }
}
