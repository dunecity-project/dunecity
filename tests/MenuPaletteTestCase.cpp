#include <catch2/catch_test_macros.hpp>

#include <FileClasses/INIFile.h>
#include <misc/Dune2RPresentation.h>
#include <misc/MenuLayout.h>
#include <misc/MenuPalette.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
struct RestoreUISkin {
    int previous = MenuTheme::uiSkin;
    ~RestoreUISkin() { MenuTheme::setUISkin(previous); }
};

double relativeLuminance(Uint32 color) {
    const auto c = RGBA2SDL(color);
    const auto linear = [](Uint8 channel) {
        const double value = channel / 255.0;
        return value <= 0.04045 ? value / 12.92
                              : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b);
}

double contrastRatio(Uint32 first, Uint32 second) {
    const auto firstLuminance = relativeLuminance(first);
    const auto secondLuminance = relativeLuminance(second);
    return (std::max(firstLuminance, secondLuminance) + 0.05)
         / (std::min(firstLuminance, secondLuminance) + 0.05);
}

void checkActiveTheme(const MenuTheme::Theme& expected) {
    CHECK(MenuTheme::background == expected.background);
    CHECK(MenuTheme::control == expected.control);
    CHECK(MenuTheme::pressed == expected.pressed);
    CHECK(MenuTheme::border == expected.border);
    CHECK(MenuTheme::accent == expected.accent);
    CHECK(MenuTheme::text == expected.text);
    CHECK(MenuTheme::muted == expected.muted);
    CHECK(MenuTheme::edgeLight == expected.edgeLight);
    CHECK(MenuTheme::edgeDark == expected.edgeDark);
}
}

TEST_CASE("UI skin preference defaults to Modern and validates independently of colors", "[menu][ui-skin]") {
    // Existing profiles keep Stefan's current appearance; only an explicit 1
    // selects Classic Desert. Bad INI values must not leak into theme lookup.
    constexpr auto modern = MenuTheme::themeForUISkin(0);
    static_assert(modern.background == COLOR_RGB(20,24,32));
    static_assert(menuPalette(0, 0).foreground == COLOR_RGB(250,248,240));
    CHECK(validatedUISkin(0) == 0);
    CHECK(validatedUISkin(1) == 1);
    for(int invalid : {-99, -1, 2, 100, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
        INFO(invalid);
        CHECK(validatedUISkin(invalid) == 0);
        CHECK(MenuTheme::themeForUISkin(invalid).background == modern.background);
        CHECK(menuPalette(0, invalid).foreground == menuPalette(0, 0).foreground);
    }

    INIFile config(true, "Independent UI skin preference");
    CHECK(validatedUISkin(config.getIntValue("Video", "UI Skin", 0)) == 0);
    config.setIntValue("Video", "Menu Palette", 1);
    config.setIntValue("Video", "Interface Height", INTERFACE_HEIGHT_NATIVE);
    config.setIntValue("Video", "Start Menu Mode", 1);
    config.setStringValue("General", "Player Name", "Retained");
    for(int selected : {1, 0}) {
        config.setIntValue("Video", "UI Skin", selected);
        CHECK(validatedUISkin(config.getIntValue("Video", "UI Skin", 0)) == selected);
        CHECK(config.getIntValue("Video", "Menu Palette", 0) == 1);
        CHECK(config.getIntValue("Video", "Interface Height", 0) == INTERFACE_HEIGHT_NATIVE);
        CHECK(config.getIntValue("Video", "Start Menu Mode", 0) == 1);
        CHECK(config.getStringValue("General", "Player Name", "") == "Retained");
    }
}

TEST_CASE("Modern UI skin retains the exact existing palette", "[menu][ui-skin][accessibility]") {
    const auto modern = MenuTheme::themeForUISkin(0);
    CHECK(modern.background == COLOR_RGB(20,24,32));
    CHECK(modern.control == COLOR_RGB(39,45,57));
    CHECK(modern.pressed == COLOR_RGB(63,70,83));
    CHECK(modern.border == COLOR_RGB(94,104,120));
    CHECK(modern.accent == COLOR_RGB(245,199,103));
    CHECK(modern.text == COLOR_RGB(250,248,240));
    CHECK(modern.muted == COLOR_RGB(197,204,217));
    CHECK(menuPalette(0, 0).foreground == modern.text);
    CHECK(menuPalette(1, 0).foreground == COLOR_WHITE);
    CHECK(menuPalette(0, 0).shadow == COLOR_TRANSPARENT);
    CHECK(menuPalette(1, 0).shadow == COLOR_TRANSPARENT);
    for(const Uint32 color : {COLOR_BLUE, COLOR_RED, COLOR_GREEN, COLOR_YELLOW, COLOR_WHITE, COLOR_DARKGREY}) {
        const auto c = RGBA2SDL(color);
        const auto original = 299*c.r + 587*c.g + 114*c.b < 150000
            ? COLOR_RGB((c.r+255)/2, (c.g+255)/2, (c.b+255)/2) : color;
        CHECK(MenuTheme::readableText(color, 0) == original);
    }
}

TEST_CASE("Classic Desert uses bright ochre panels and readable dark labels", "[menu][ui-skin][accessibility]") {
    const auto classic = MenuTheme::themeForUISkin(1);
    const auto modern = MenuTheme::themeForUISkin(0);
    for(const Uint32 color : {classic.background, classic.control, classic.pressed}) {
        const auto c = RGBA2SDL(color);
        CHECK(c.r > c.g);
        CHECK(c.g > c.b);
        CHECK(c.a == 255);
        CHECK(relativeLuminance(color) > 0.20);
        CHECK(color != modern.background);
        CHECK(color != COLOR_BLACK);
        for(int palette : {0, 1}) {
            CHECK(contrastRatio(menuPalette(palette, 1).foreground, color) >= 4.5);
            CHECK(menuPalette(palette, 1).shadow == COLOR_TRANSPARENT);
        }
    }
    CHECK(menuPalette(0, 1).foreground == classic.text);
    CHECK(relativeLuminance(menuPalette(1, 1).foreground)
          < relativeLuminance(menuPalette(0, 1).foreground));
    CHECK(classic.edgeLight != classic.edgeDark);
    CHECK(contrastRatio(classic.edgeLight, classic.edgeDark) > 2.0);
    for(int invalid : {-1, 2, 100})
        CHECK(menuPalette(invalid, 1).foreground == classic.text);
}

TEST_CASE("Switching UI skins restores every runtime color without preview mutation", "[menu][ui-skin]") {
    RestoreUISkin restore;
    MenuTheme::setUISkin(0);
    CHECK_FALSE(MenuTheme::isClassicDesert());
    checkActiveTheme(MenuTheme::themeForUISkin(0));

    // Resolving a preview must not change active menu or HUD colors.
    const auto preview = MenuTheme::themeForUISkin(1);
    CHECK(preview.background != MenuTheme::background);
    CHECK(menuPalette(0, 1).foreground == preview.text);
    CHECK(MenuTheme::uiSkin == 0);
    checkActiveTheme(MenuTheme::themeForUISkin(0));

    MenuTheme::setUISkin(1);
    CHECK(MenuTheme::isClassicDesert());
    CHECK(MenuTheme::uiSkin == 1);
    checkActiveTheme(preview);
    CHECK(menuPalette(0).foreground == menuPalette(0, 1).foreground);
    CHECK(menuPalette(1).foreground == menuPalette(1, 1).foreground);
    CHECK(MenuTheme::readableText(COLOR_WHITE) == MenuTheme::readableText(COLOR_WHITE, 1));

    MenuTheme::setUISkin(0);
    CHECK_FALSE(MenuTheme::isClassicDesert());
    checkActiveTheme(MenuTheme::themeForUISkin(0));
    CHECK(menuPalette(0).foreground == menuPalette(0, 0).foreground);
    CHECK(MenuTheme::readableText(COLOR_BLUE) == MenuTheme::readableText(COLOR_BLUE, 0));
    MenuTheme::setUISkin(100);
    CHECK(MenuTheme::uiSkin == 0);
    checkActiveTheme(MenuTheme::themeForUISkin(0));
}

TEST_CASE("Classic colored labels remain readable on gold surfaces", "[menu][ui-skin][accessibility]") {
    const auto classic = MenuTheme::themeForUISkin(1);
    // Cover bright and dark house colors, plus common success/warning labels.
    for(const Uint32 color : {COLOR_RED, COLOR_BLUE, COLOR_GREEN, COLOR_LIGHTGREEN,
                             COLOR_YELLOW, COLOR_ORANGE, COLOR_WHITE, COLOR_LIGHTGREY,
                             COLOR_DARKGREY, COLOR_RGB(120,32,170), COLOR_RGB(32,32,32)}) {
        INFO(color);
        const auto readable = MenuTheme::readableText(color, 1);
        CHECK(contrastRatio(readable, classic.background) >= 4.5);
        CHECK(contrastRatio(readable, classic.control) >= 4.5);
        CHECK(contrastRatio(readable, classic.pressed) >= 4.5);
        const auto c = RGBA2SDL(readable);
        CHECK(c.a == 255);
    }
    CHECK(MenuTheme::readableText(COLOR_RGB(0,0,80), 1) == COLOR_RGB(0,0,80));
    const auto translucent = COLOR_RGBA(255,255,0,128);
    const auto c = RGBA2SDL(MenuTheme::readableText(translucent, 1));
    CHECK(c.a == 128);
    CHECK(c.r == c.g);
    CHECK(c.b == 0);
}

TEST_CASE("Classic button typography pairs decorative copper with a dark shadow and high-contrast option",
          "[menu][ui-skin][buttons][accessibility]") {
    constexpr auto standard = buttonPalette(0, 1);
    constexpr auto pressed = buttonPalette(0, 1, true);
    static_assert(standard.foreground == COLOR_RGB(151,91,50));
    static_assert(standard.shadow == COLOR_RGB(65,37,18));
    static_assert(pressed.foreground == COLOR_RGB(137,77,41));
    static_assert(pressed.shadow == COLOR_RGB(45,25,12));
    const auto classic = MenuTheme::themeForUISkin(1);
    // The approved copper face is decorative, not a 4.5:1 accessibility claim.
    // Its dark offset shadow supplies separation; High Contrast retains dark
    // foreground glyphs for users who need the full foreground contrast.
    CHECK(contrastRatio(standard.foreground, classic.control) < 4.5);
    CHECK(contrastRatio(pressed.foreground, classic.pressed) < 4.5);
    CHECK(standard.foreground != menuPalette(0, 1).foreground);
    CHECK(menuPalette(0, 1).shadow == COLOR_TRANSPARENT);
    for(int palette : {0, 1}) {
        for(bool down : {false, true}) {
            const auto modern = buttonPalette(palette, 0, down);
            CHECK(modern.foreground == menuPalette(palette, 0).foreground);
            CHECK(modern.shadow == menuPalette(palette, 0).shadow);
            const auto colors = buttonPalette(palette, 1, down);
            CHECK(RGBA2SDL(colors.foreground).a == 255);
            CHECK(RGBA2SDL(colors.shadow).a == 255);
            const auto nominal = down ? classic.pressed : classic.control;
            // Minimum central brass colors include both the vertical relief
            // and the quieter grain beneath a label in drawControlFrame.
            const auto minimum = down ? COLOR_RGB(195,145,39) : COLOR_RGB(214,166,56);
            CHECK(contrastRatio(colors.shadow, nominal) >= 4.5);
            CHECK(contrastRatio(colors.shadow, minimum) >= 4.5);
            if(palette == 1) {
                CHECK(contrastRatio(colors.foreground, nominal) >= 4.5);
                CHECK(contrastRatio(colors.foreground, minimum) >= 4.5);
            } else {
                CHECK(relativeLuminance(colors.foreground) > relativeLuminance(colors.shadow));
            }
        }
    }
    for(bool down : {false, true}) {
        const auto highContrast = buttonPalette(1, 1, down);
        CHECK(highContrast.foreground == menuPalette(1, 1).foreground);
        CHECK(contrastRatio(highContrast.foreground, down ? classic.pressed : classic.control) >= 4.5);
        for(int invalid : {-1, 2, 100}) {
            const auto fallback = buttonPalette(invalid, 1, down);
            CHECK(fallback.foreground == buttonPalette(0, 1, down).foreground);
            CHECK(fallback.shadow == buttonPalette(0, 1, down).shadow);
            CHECK(buttonPalette(0, invalid, down).foreground == menuPalette(0, 0).foreground);
            CHECK(buttonPalette(0, invalid, down).shadow == COLOR_TRANSPARENT);
        }
    }
    RestoreUISkin restore;
    for(int skin : {0, 1, 0}) {
        MenuTheme::setUISkin(skin);
        CHECK(buttonPalette(0).foreground == buttonPalette(0, skin).foreground);
        CHECK(buttonPalette(1).shadow == buttonPalette(1, skin).shadow);
        const auto preview = buttonPalette(0, 1 - skin, true);
        CHECK(preview.foreground == buttonPalette(0, 1 - skin, true).foreground);
        CHECK(MenuTheme::uiSkin == skin);
        checkActiveTheme(MenuTheme::themeForUISkin(skin));
    }
}

TEST_CASE("UI skin never changes interface resolution or Dune2R world presentation", "[menu][ui-skin][display]") {
    RestoreUISkin restore;
    for(int skin : {0, 1, 0}) {
        MenuTheme::setUISkin(skin);
        const auto native = interfaceLogicalSize(INTERFACE_HEIGHT_NATIVE, 1920, 1080);
        CHECK(native.x == 1920);
        CHECK(native.y == 1080);
        const auto automatic = interfaceLogicalSize(0, 1920, 1080);
        CHECK(automatic.x == 960);
        CHECK(automatic.y == 540);
        for(int height : {480, 600, 768})
            CHECK(interfaceLogicalSize(height, 1920, 1080).y == height);
        CHECK(dune2rPresentationScale("Dune2R", true) == 3);
        CHECK(dune2rPresentationScale("Dune2R", false) == 1);
        for(const std::string mod : {"vanilla", "DuneLegacy", "dunecity", "DuneCity", "Tornie"})
            CHECK(dune2rPresentationScale(mod, true) == 1);
        CHECK(dune2rWorldDrawingScale == 1);
        {
            Dune2RWorldDrawingScope world(3);
            MenuTheme::setUISkin(1 - skin);
            CHECK(dune2rWorldDrawingScale == 3);
            CHECK(dune2rWorldExtent(16) == 48);
        }
        CHECK(dune2rWorldDrawingScale == 1);
    }
}
