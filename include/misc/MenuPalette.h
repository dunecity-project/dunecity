#ifndef DUNECITY_MENU_PALETTE_H
#define DUNECITY_MENU_PALETTE_H

#include <Colors.h>
#include <cmath>

inline constexpr int validatedMenuPalette(int value) {
    return value == 1 ? 1 : 0;
}

// Appearance is a local UI preference, not a mod, sprite skin or world scale.
inline constexpr int validatedUISkin(int value) {
    return value == 1 ? 1 : 0;
}

namespace MenuTheme {
struct Theme {
    Uint32 background;
    Uint32 control;
    Uint32 pressed;
    Uint32 border;
    Uint32 accent;
    Uint32 text;
    Uint32 muted;
    Uint32 edgeLight;
    Uint32 edgeDark;
};

inline constexpr Theme themeForUISkin(int value) {
    return validatedUISkin(value) == 1
        ? Theme{COLOR_RGB(207,157,47), COLOR_RGB(223,177,66), COLOR_RGB(188,135,34),
                COLOR_RGB(112,70,13), COLOR_RGB(103,45,6), COLOR_RGB(60,32,7),
                COLOR_RGB(105,69,21), COLOR_RGB(255,220,128), COLOR_RGB(133,85,18)}
        : Theme{COLOR_RGB(20,24,32), COLOR_RGB(39,45,57), COLOR_RGB(63,70,83),
                COLOR_RGB(94,104,120), COLOR_RGB(245,199,103), COLOR_RGB(250,248,240),
                COLOR_RGB(197,204,217), COLOR_RGB(94,104,120), COLOR_RGB(94,104,120)};
}

// Keep these names available to menu/HUD consumers that draw outside GUIStyle.
// DuneStyle captures its own Theme so preview instances never change live colors.
inline int uiSkin = 0;
inline Uint32 background = themeForUISkin(0).background;
inline Uint32 control = themeForUISkin(0).control;
inline Uint32 pressed = themeForUISkin(0).pressed;
inline Uint32 border = themeForUISkin(0).border;
inline Uint32 accent = themeForUISkin(0).accent;
inline Uint32 text = themeForUISkin(0).text;
inline Uint32 muted = themeForUISkin(0).muted;
inline Uint32 edgeLight = themeForUISkin(0).edgeLight;
inline Uint32 edgeDark = themeForUISkin(0).edgeDark;

inline void setUISkin(int value) {
    uiSkin = validatedUISkin(value);
    const auto theme = themeForUISkin(uiSkin);
    background = theme.background;
    control = theme.control;
    pressed = theme.pressed;
    border = theme.border;
    accent = theme.accent;
    text = theme.text;
    muted = theme.muted;
    edgeLight = theme.edgeLight;
    edgeDark = theme.edgeDark;
}

inline bool isClassicDesert() {
    return uiSkin == 1;
}

inline double textLuminance(Uint32 color) {
    const auto channels = RGBA2SDL(color);
    const auto linear = [](Uint8 channel) {
        const double value = channel / 255.0;
        return value <= 0.04045 ? value / 12.92 : std::pow((value+0.055)/1.055, 2.4);
    };
    return 0.2126*linear(channels.r) + 0.7152*linear(channels.g) + 0.0722*linear(channels.b);
}

inline double textContrast(Uint32 foreground, Uint32 backgroundColor) {
    const double first = textLuminance(foreground);
    const double second = textLuminance(backgroundColor);
    return first > second ? (first+0.05)/(second+0.05) : (second+0.05)/(first+0.05);
}

// Preserve faction hues while keeping labels readable on the selected surface.
inline Uint32 readableText(Uint32 color, int skin = uiSkin) {
    auto c=RGBA2SDL(color);
    const int brightness = 299*c.r+587*c.g+114*c.b;
    if(validatedUISkin(skin) == 1) {
        const auto classic = themeForUISkin(1);
        for(int attempt = 0; attempt < 16; ++attempt) {
            const auto candidate = COLOR_RGBA(c.r, c.g, c.b, c.a);
            if(textContrast(candidate, classic.background) >= 4.5
                && textContrast(candidate, classic.control) >= 4.5
                && textContrast(candidate, classic.pressed) >= 4.5) return candidate;
            c.r = (c.r*4)/5;
            c.g = (c.g*4)/5;
            c.b = (c.b*4)/5;
        }
        return COLOR_RGBA(0, 0, 0, c.a);
    } else if(brightness < 150000) {
        return COLOR_RGB((c.r+255)/2,(c.g+255)/2,(c.b+255)/2);
    }
    return color;
}
}

struct MenuPalette {
    Uint32 foreground;
    Uint32 shadow;
};

inline constexpr MenuPalette menuPalette(int value, int skin = MenuTheme::uiSkin) {
    return validatedUISkin(skin) == 1
        ? MenuPalette{validatedMenuPalette(value) == 1 ? COLOR_RGB(20,12,0)
                                                     : MenuTheme::themeForUISkin(1).text,
                      COLOR_TRANSPARENT}
        : MenuPalette{validatedMenuPalette(value) == 1 ? COLOR_WHITE
                                                     : MenuTheme::themeForUISkin(0).text,
                      COLOR_TRANSPARENT};
}

// Classic button lettering reprises the original red-and-gold relief without
// changing labels over menus, Mentat artwork or the game world. The recessed
// pressed face uses deeper ink to retain contrast on its darker gold surface.
inline constexpr MenuPalette buttonPalette(int value, int skin = MenuTheme::uiSkin,
                                           bool pressed = false) {
    return validatedUISkin(skin) == 1
        ? MenuPalette{validatedMenuPalette(value) == 1 ? COLOR_RGB(20,12,0)
                      : pressed ? COLOR_RGB(88,10,4) : COLOR_RGB(125,0,0),
                      pressed ? COLOR_RGB(255,220,128) : COLOR_RGB(255,235,176)}
        : menuPalette(value, skin);
}

#endif
