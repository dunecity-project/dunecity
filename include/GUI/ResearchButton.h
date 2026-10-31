// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <GUI/TextButton.h>
#include <FileClasses/FontManager.h>
#include <globals.h>
#include <algorithm>

/// Separate name and progress/price rows remain legible in the narrow sidebar.
class ResearchButton : public TextButton {
public:
    void setPrice(int value) {
        if(price != value) { price = value; invalidateTextures(); }
    }
    void setStatus(const std::string& value) {
        if(status != value) { status = value; invalidateTextures(); }
    }
    Point getMinimumSize() const override { return Point(104, 38); }

protected:
    void updateTextures() override {
        Button::updateTextures();
        if(pUnpressedTexture) return;
        const auto surface = [this](bool pressed, bool active) {
            auto image = GUIStyle::getInstance().createButtonSurface(
                getSize().x, getSize().y, "", pressed, active, getTextColor(), getTextShadowColor());
            Uint32 colour = getTextColor() == COLOR_DEFAULT ? COLOR_WHITE : getTextColor();
            if(active) {
                const SDL_Color c = RGBA2SDL(colour);
                colour = COLOR_RGB(std::min(255, int(c.r) * 3 / 2), std::min(255, int(c.g) * 3 / 2), std::min(255, int(c.b) * 3 / 2));
            }
            const Uint32 shadow = getTextShadowColor() == COLOR_DEFAULT ? COLOR_LIGHTYELLOW : getTextShadowColor();
            const int offset = pressed ? 1 : 0;
            const auto text = [&](const std::string& value, int x, int y, Uint32 foreground, Uint32 shade, int font) {
                if(value.empty()) return;
                auto dark = pFontManager->createSurfaceWithText(value, shade, font);
                auto light = pFontManager->createSurfaceWithText(value, foreground, font);
                SDL_Rect back{x + 1 + offset, y + 1 + offset, dark->w, dark->h};
                SDL_Rect front{x + offset, y + offset, light->w, light->h};
                SDL_BlitSurface(dark.get(), nullptr, image.get(), &back);
                SDL_BlitSurface(light.get(), nullptr, image.get(), &front);
            };
            int font = 12;
            while(font > 8 && pFontManager->getTextWidth(getText(), font) > getSize().x - 8) --font;
            text(getText(), 4, 2, colour, shadow, font);
            const int row = getSize().y - pFontManager->getTextHeight(12) - 3;
            text(status, 4, row, colour, shadow, 12);
            const std::string quote = std::to_string(price);
            text(quote, getSize().x - pFontManager->getTextWidth(quote, 12) - 5, row, COLOR_WHITE, COLOR_BLACK, 12);
            return image;
        };
        setSurfaces(surface(false, false), surface(true, true), surface(false, true));
    }

private:
    int price = 0;
    std::string status;
};
