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
    // Existing profiles keep Stefan's current appearance; explicit IDs select
    // Lapis Enamel (1) or Classic Desert (2). Bad INI values cannot select a skin.
    constexpr auto modern = MenuTheme::themeForUISkin(0);
    static_assert(modern.background == COLOR_RGB(20,24,32));
    static_assert(menuPalette(0, 0).foreground == COLOR_RGB(250,248,240));
    CHECK(validatedUISkin(0) == 0);
    CHECK(validatedUISkin(1) == 1);
    CHECK(validatedUISkin(2) == 2);
    for(int invalid : {-99, -1, 3, 100, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
        INFO(invalid);
        CHECK(validatedUISkin(invalid) == 0);
        CHECK(MenuTheme::themeForUISkin(invalid).background == modern.background);
        CHECK(menuPalette(0, invalid).foreground == menuPalette(0, 0).foreground);
        CHECK_FALSE(MenuTheme::isDesertUISkin(invalid));
        CHECK_FALSE(MenuTheme::isLapisEnamel(invalid));
        CHECK_FALSE(MenuTheme::isClassicDesert(invalid));
    }

    INIFile config(true, "Independent UI skin preference");
    CHECK(validatedUISkin(config.getIntValue("Video", "UI Skin", 0)) == 0);
    config.setIntValue("Video", "Menu Palette", 1);
    config.setIntValue("Video", "Interface Height", INTERFACE_HEIGHT_NATIVE);
    config.setIntValue("Video", "Start Menu Mode", 1);
    config.setStringValue("General", "Player Name", "Retained");
    for(int selected : {1, 2, 0}) {
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

TEST_CASE("Lapis Enamel uses bright ochre panels and readable dark labels", "[menu][ui-skin][accessibility]") {
    const auto classic = MenuTheme::themeForUISkin(1);
    const auto modern = MenuTheme::themeForUISkin(0);
    CHECK(classic.background == COLOR_RGB(207,157,47));
    CHECK(classic.control == COLOR_RGB(223,177,66));
    CHECK(classic.pressed == COLOR_RGB(188,135,34));
    CHECK(classic.border == COLOR_RGB(112,70,13));
    CHECK(classic.accent == COLOR_RGB(103,45,6));
    CHECK(classic.text == COLOR_RGB(60,32,7));
    CHECK(classic.muted == COLOR_RGB(105,69,21));
    CHECK(classic.edgeLight == COLOR_RGB(255,220,128));
    CHECK(classic.edgeDark == COLOR_RGB(133,85,18));
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
    for(int skin : {0, 1, 2, 1, 0}) {
        MenuTheme::setUISkin(skin);
        CHECK(MenuTheme::uiSkin == skin);
        CHECK(MenuTheme::isDesertUISkin() == (skin != 0));
        CHECK(MenuTheme::isLapisEnamel() == (skin == 1));
        CHECK(MenuTheme::isClassicDesert() == (skin == 2));
        checkActiveTheme(MenuTheme::themeForUISkin(skin));
        CHECK(menuPalette(0).foreground == menuPalette(0, skin).foreground);
        CHECK(menuPalette(1).foreground == menuPalette(1, skin).foreground);
        CHECK(MenuTheme::readableText(COLOR_WHITE) == MenuTheme::readableText(COLOR_WHITE, skin));
        CHECK(MenuTheme::readableText(COLOR_BLUE) == MenuTheme::readableText(COLOR_BLUE, skin));

        // Resolve every preview without changing the selected menu/HUD theme.
        for(int previewSkin : {0, 1, 2}) {
            const auto preview = MenuTheme::themeForUISkin(previewSkin);
            CHECK(menuPalette(0, previewSkin).foreground == preview.text);
            CHECK(MenuTheme::isDesertUISkin(previewSkin) == (previewSkin != 0));
            CHECK(MenuTheme::isLapisEnamel(previewSkin) == (previewSkin == 1));
            CHECK(MenuTheme::isClassicDesert(previewSkin) == (previewSkin == 2));
            CHECK(MenuTheme::uiSkin == skin);
            checkActiveTheme(MenuTheme::themeForUISkin(skin));
        }
    }
    MenuTheme::setUISkin(100);
    CHECK(MenuTheme::uiSkin == 0);
    CHECK_FALSE(MenuTheme::isDesertUISkin());
    CHECK_FALSE(MenuTheme::isLapisEnamel());
    CHECK_FALSE(MenuTheme::isClassicDesert());
    checkActiveTheme(MenuTheme::themeForUISkin(0));
}

TEST_CASE("Classic colored labels remain readable on gold surfaces", "[menu][ui-skin][accessibility]") {
    for(int skin : {1, 2}) {
        INFO(skin);
        const auto classic = MenuTheme::themeForUISkin(skin);
        // Cover bright and dark house colors, plus common success/warning labels.
        for(const Uint32 color : {COLOR_RED, COLOR_BLUE, COLOR_GREEN, COLOR_LIGHTGREEN,
                                 COLOR_YELLOW, COLOR_ORANGE, COLOR_WHITE, COLOR_LIGHTGREY,
                                 COLOR_DARKGREY, COLOR_RGB(120,32,170), COLOR_RGB(32,32,32)}) {
            INFO(color);
            const auto readable = MenuTheme::readableText(color, skin);
            CHECK(contrastRatio(readable, classic.background) >= 4.5);
            CHECK(contrastRatio(readable, classic.control) >= 4.5);
            CHECK(contrastRatio(readable, classic.pressed) >= 4.5);
            const auto c = RGBA2SDL(readable);
            CHECK(c.a == 255);
        }
        CHECK(MenuTheme::readableText(COLOR_RGB(0,0,80), skin) == COLOR_RGB(0,0,80));
        const auto translucent = COLOR_RGBA(255,255,0,128);
        const auto c = RGBA2SDL(MenuTheme::readableText(translucent, skin));
        CHECK(c.a == 128);
        CHECK(c.r == c.g);
        CHECK(c.b == 0);
    }
}

TEST_CASE("Classic button typography pairs readable Lapis Enamel with pale-gold relief",
          "[menu][ui-skin][buttons][accessibility]") {
    constexpr auto standard = buttonPalette(0, 1);
    constexpr auto pressed = buttonPalette(0, 1, true);
    static_assert(standard.foreground == COLOR_RGB(18,29,111));
    static_assert(standard.shadow == COLOR_RGB(255,235,176));
    static_assert(pressed.foreground == COLOR_RGB(11,20,82));
    static_assert(pressed.shadow == COLOR_RGB(255,220,128));
    const auto classic = MenuTheme::themeForUISkin(1);
    // The foreground itself must remain readable without relying on relief.
    // The pale offset is an enamel highlight, not dark accessibility ink.
    CHECK(contrastRatio(standard.foreground, classic.control) >= 4.5);
    CHECK(contrastRatio(pressed.foreground, classic.pressed) >= 4.5);
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
            CHECK(contrastRatio(colors.foreground, nominal) >= 4.5);
            CHECK(contrastRatio(colors.foreground, minimum) >= 4.5);
            CHECK(contrastRatio(colors.foreground, colors.shadow) >= 4.5);
            CHECK(relativeLuminance(colors.foreground) < relativeLuminance(colors.shadow));
            CHECK(colors.shadow == (down ? pressed.shadow : standard.shadow));
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
        }
        for(int invalid : {-1, 3, 100}) {
            CHECK(buttonPalette(0, invalid, down).foreground == menuPalette(0, 0).foreground);
            CHECK(buttonPalette(0, invalid, down).shadow == COLOR_TRANSPARENT);
        }
    }
    RestoreUISkin restore;
    for(int skin : {0, 1, 2, 0}) {
        MenuTheme::setUISkin(skin);
        CHECK(buttonPalette(0).foreground == buttonPalette(0, skin).foreground);
        CHECK(buttonPalette(1).shadow == buttonPalette(1, skin).shadow);
        for(int previewSkin : {0, 1, 2}) {
            const auto preview = buttonPalette(0, previewSkin, true);
            CHECK(preview.foreground == buttonPalette(0, previewSkin, true).foreground);
            CHECK(MenuTheme::uiSkin == skin);
            checkActiveTheme(MenuTheme::themeForUISkin(skin));
        }
    }
}

TEST_CASE("Classic Desert pairs bright textured controls with readable dark-orange lettering",
          "[menu][ui-skin][buttons][accessibility]") {
    constexpr auto classic = MenuTheme::themeForUISkin(2);
    constexpr auto standard = buttonPalette(0, 2);
    constexpr auto pressed = buttonPalette(0, 2, true);
    static_assert(classic.background == COLOR_RGB(207,157,47));
    static_assert(classic.control == COLOR_RGB(230,183,68));
    static_assert(classic.pressed == COLOR_RGB(220,169,44));
    static_assert(classic.border == COLOR_RGB(112,70,13));
    static_assert(classic.accent == COLOR_RGB(100,37,0));
    static_assert(classic.text == COLOR_RGB(100,37,0));
    static_assert(classic.muted == COLOR_RGB(105,69,21));
    static_assert(classic.edgeLight == COLOR_RGB(255,220,128));
    static_assert(classic.edgeDark == COLOR_RGB(133,85,18));
    static_assert(standard.foreground == COLOR_RGB(116,48,6));
    static_assert(pressed.foreground == COLOR_RGB(100,37,0));
    static_assert(standard.shadow == COLOR_RGB(255,225,144));
    static_assert(pressed.shadow == standard.shadow);

    // The classic treatment is a distinct palette, not a rename of Lapis.
    CHECK(classic.control != MenuTheme::themeForUISkin(1).control);
    CHECK(classic.pressed != MenuTheme::themeForUISkin(1).pressed);
    CHECK(standard.foreground != buttonPalette(0, 1).foreground);
    CHECK(pressed.foreground != buttonPalette(0, 1, true).foreground);
    for(const Uint32 ink : {standard.foreground, pressed.foreground}) {
        const auto c = RGBA2SDL(ink);
        CHECK(c.r > c.g);
        CHECK(c.g > c.b);
        CHECK(c.a == 255);
        CHECK(contrastRatio(ink, standard.shadow) >= 4.5);
    }
    // Button ink sits on its corresponding brighter text-safe control face,
    // not on the busier FAME backdrop. Body labels keep their darker ink.
    for(const Uint32 background : {classic.background, classic.control, classic.pressed})
        CHECK(contrastRatio(classic.text, background) >= 4.5);
    for(int palette : {0, 1}) {
        CHECK(menuPalette(palette, 2).foreground
              == (palette == 1 ? COLOR_RGB(20,12,0) : classic.text));
        CHECK(menuPalette(palette, 2).shadow == standard.shadow);
        for(bool down : {false, true}) {
            const auto colors = buttonPalette(palette, 2, down);
            CHECK(colors.foreground == (palette == 1 ? COLOR_RGB(20,12,0)
                                       : down ? pressed.foreground : standard.foreground));
            CHECK(colors.shadow == standard.shadow);
            CHECK(contrastRatio(colors.foreground, down ? classic.pressed : classic.control) >= 4.5);
            CHECK(contrastRatio(colors.foreground,
                  down ? COLOR_RGB(216,165,40) : COLOR_RGB(225,178,63)) >= 4.5);
        }
    }
    for(int invalidPalette : {-1, 2, 100}) {
        CHECK(menuPalette(invalidPalette, 2).foreground == classic.text);
        for(bool down : {false, true}) {
            CHECK(buttonPalette(invalidPalette, 2, down).foreground
                  == buttonPalette(0, 2, down).foreground);
            CHECK(buttonPalette(invalidPalette, 2, down).shadow == standard.shadow);
        }
    }
}

TEST_CASE("UI skin never changes interface resolution or Dune2R world presentation", "[menu][ui-skin][display]") {
    RestoreUISkin restore;
    for(int skin : {0, 1, 2, 0}) {
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
            MenuTheme::setUISkin((skin + 1) % 3);
            CHECK(dune2rWorldDrawingScale == 3);
            CHECK(dune2rWorldExtent(16) == 48);
        }
        CHECK(dune2rWorldDrawingScale == 1);
    }
}
