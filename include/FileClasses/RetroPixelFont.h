/*
 * Original Classic Desert pixel lettering for DuneCity.
 * Distributed under the same GPL-2.0-or-later terms as DuneCity.
 */

#ifndef RETROPIXELFONT_H
#define RETROPIXELFONT_H

#include <FileClasses/Font.h>

// A hand-authored bitmap interpretation of arched, retro-futuristic lettering,
// not a conversion of a TrueType font or a copy of the original Dune II font.
// Printable ASCII is supported; lowercase uses the corresponding capital mask.
// FontManager chooses the existing font for a whole unsupported/localized label.
class RetroPixelFont final : public Font {
public:
    explicit RetroPixelFont(unsigned int fontSize);

    void drawTextOnSurface(SDL_Surface* surface, const std::string& text,
                           Uint32 baseColor = 0xFFFFFFFF) override;
    int getTextWidth(const std::string& text) const override;
    bool canRenderText(const std::string& text) const override;
    int getTextHeight() const override { return characterHeight; }

private:
    int characterHeight;
    int capHeight;
    int characterSpacing;
};

#endif // RETROPIXELFONT_H
