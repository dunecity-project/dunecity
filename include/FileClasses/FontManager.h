/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FONTMANAGER_H
#define FONTMANAGER_H

#include <misc/SDL2pp.h>
#include "Font.h"

#include <memory>
#include <string>
#include <map>

/// A class for managing fonts.
/**
    This class manages all fonts used in Dune Legacy and provides methods for rendering texts with a specific font.
*/
class FontManager
{
public:
    FontManager();
    ~FontManager();

    void drawTextOnSurface(SDL_Surface* pSurface, const std::string& text, Uint32 color, unsigned int fontSize);
    int getTextWidth(const std::string& text, unsigned int fontSize);
    int getTextHeight(unsigned int fontSize);
    sdl2::surface_ptr createSurfaceWithText(const std::string& text, Uint32 color, unsigned int fontSize);
    sdl2::texture_ptr createTextureWithText(const std::string& text, Uint32 color, unsigned int fontSize);
    sdl2::surface_ptr createSurfaceWithMultilineText(const std::string& text, Uint32 color, unsigned int fontSize, bool bCentered = false);
    sdl2::texture_ptr createTextureWithMultilineText(const std::string& text, Uint32 color, unsigned int fontSize, bool bCentered = false);

    // Optional Lapis Enamel display lettering. Each method chooses one font
    // for the whole string, falling back to the normal face for missing glyphs.
    int getDisplayTextWidth(const std::string& text, unsigned int fontSize);
    int getDisplayTextHeight(const std::string& text, unsigned int fontSize);
    sdl2::surface_ptr createDisplaySurfaceWithText(const std::string& text, Uint32 color, unsigned int fontSize);

    // Classic Desert's original bitmap lettering. All three methods choose the
    // same whole-label fallback for localized/unsupported text.
    int getRetroTextWidth(const std::string& text, unsigned int fontSize);
    int getRetroTextHeight(const std::string& text, unsigned int fontSize);
    sdl2::surface_ptr createRetroSurfaceWithText(const std::string& text, Uint32 color, unsigned int fontSize);
private:
    inline Font* getFont(unsigned int fontSize) {
        auto iter = fonts.find(fontSize);
        if(iter != fonts.end()) {
            return iter->second.get();
        } else {
            fonts[fontSize] = loadFont(fontSize);
            return fonts[fontSize].get();
        }
    }

    std::unique_ptr<Font> loadFont(unsigned int fontSize);
    Font* getDisplayFont(const std::string& text, unsigned int fontSize);
    Font* getRetroFont(const std::string& text, unsigned int fontSize);

    std::map<unsigned int, std::unique_ptr<Font>> fonts;
    std::map<unsigned int, std::unique_ptr<Font>> displayFonts;
    std::map<unsigned int, std::unique_ptr<Font>> retroFonts;
    bool displayFontUnavailable = false;

};

#endif // FONTMANAGER_H
