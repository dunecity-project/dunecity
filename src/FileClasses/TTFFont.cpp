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

#include <FileClasses/TTFFont.h>
#include <misc/exceptions.h>

#include <Colors.h>

#include <algorithm>
#include <cstdint>

/// Constructor
/**
    The constructor reads the font from the ttf file given via rwop.
    \param  pRWOP        The file which contains the font
    \param  fontsize    The size of the font in pixels
    \param  widthPercentage  Horizontal compression (1-100); metrics and rendering agree.
*/
TTFFont::TTFFont(sdl2::RWops_ptr pRWOP, int fontsize, int widthPercentage)
    : widthPercentage(widthPercentage)
{
    if(widthPercentage < 1 || widthPercentage > 100) {
        THROW(std::invalid_argument, "TTFFont::TTFFont(): widthPercentage must be between 1 and 100!");
    }
    if(!pRWOP) {
        THROW(std::invalid_argument, "TTFFont::TTFFont(): pRWOP == nullptr!");
    }

    pTTFFont = font_ptr { TTF_OpenFontRW(pRWOP.release(), 1, fontsize) };
    if(!pTTFFont) {
        THROW(std::invalid_argument, "TTFFont::TTFFont(): TTF_OpenFontRW() failed: %s!", TTF_GetError());
    }

    TTF_SetFontHinting(pTTFFont.get(), widthPercentage == 100 ? TTF_HINTING_MONO : TTF_HINTING_LIGHT);

    characterHeight = TTF_FontHeight(pTTFFont.get());
}

/// Destructor
/**
    Frees all memory.
*/
TTFFont::~TTFFont() = default;


void TTFFont::drawTextOnSurface(SDL_Surface* pSurface, const std::string& text, Uint32 baseColor) {

    if(pSurface != nullptr && !text.empty()) {
        sdl2::surface_ptr pTextSurface { widthPercentage == 100
            ? TTF_RenderUTF8_Solid(pTTFFont.get(), text.c_str(), RGBA2SDL(baseColor))
            : TTF_RenderUTF8_Blended(pTTFFont.get(), text.c_str(), RGBA2SDL(baseColor)) };
        if(!pTextSurface) {
            THROW(std::invalid_argument, "TTFFont::drawTextOnSurface(): TTF_RenderUTF8() failed: %s!", TTF_GetError());
        }

        SDL_Rect dest { 0, -2, pTextSurface->w, pTextSurface->h };
        if(widthPercentage == 100) {
            SDL_BlitSurface(pTextSurface.get(), nullptr, pSurface, &dest);
        } else {
            // Keep straight RGB/alpha through the compression step. Blending
            // into a keyed/magenta intermediate would bake a colored fringe
            // into antialiased edges before the final label composition.
            sdl2::surface_ptr scalableText { SDL_ConvertSurface(pTextSurface.get(), pSurface->format, 0) };
            if(!scalableText) {
                THROW(std::invalid_argument, "TTFFont::drawTextOnSurface(): SDL_ConvertSurface() failed: %s!", SDL_GetError());
            }
            dest.w = getTextWidth(text);
            SDL_SetSurfaceBlendMode(scalableText.get(), SDL_BLENDMODE_NONE);
            if(SDL_BlitScaled(scalableText.get(), nullptr, pSurface, &dest) < 0) {
                THROW(std::invalid_argument, "TTFFont::drawTextOnSurface(): SDL_BlitScaled() failed: %s!", SDL_GetError());
            }
        }
    }
}

/// Returns the number of pixels a text needs
/**
        This methods returns the number of pixels this text would need if printed.
        \param  text    The text to be checked for it's length in pixel
        \return Number of pixels needed
*/
int TTFFont::getTextWidth(const std::string& text) const {

    int width = 0;
    if(TTF_SizeUTF8(pTTFFont.get(), text.c_str(), &width, nullptr) < 0) {
        THROW(std::invalid_argument, "TTFFont::getTextWidth(): TTF_SizeText() failed: %s!", TTF_GetError());
    }

    return widthPercentage == 100 || width == 0
        ? width
        : std::max(1, static_cast<int>((static_cast<std::int64_t>(width) * widthPercentage + 50) / 100));
}

bool TTFFont::canRenderText(const std::string& text) const {
    // Do not let an invalid sequence, embedded NUL, surrogate, or overlong
    // encoding make the display font appear to support only part of a label.
    size_t offset = 0;
    while(offset < text.size()) {
        const auto lead = static_cast<unsigned char>(text[offset++]);
        Uint32 codepoint = lead;
        int continuationCount = 0;
        Uint32 minimum = 0;
        if(lead == 0) {
            return false;
        } else if(lead < 0x80) {
            // ASCII is already decoded.
        } else if(lead >= 0xC2 && lead <= 0xDF) {
            continuationCount = 1;
            codepoint = lead & 0x1F;
            minimum = 0x80;
        } else if(lead >= 0xE0 && lead <= 0xEF) {
            continuationCount = 2;
            codepoint = lead & 0x0F;
            minimum = 0x800;
        } else if(lead >= 0xF0 && lead <= 0xF4) {
            continuationCount = 3;
            codepoint = lead & 0x07;
            minimum = 0x10000;
        } else {
            return false;
        }

        if(text.size() - offset < static_cast<size_t>(continuationCount)) {
            return false;
        }
        for(int i = 0; i < continuationCount; ++i) {
            const auto next = static_cast<unsigned char>(text[offset++]);
            if((next & 0xC0) != 0x80) {
                return false;
            }
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if(codepoint < minimum || codepoint > 0x10FFFF
            || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            return false;
        }
#if SDL_TTF_MAJOR_VERSION > 2 || (SDL_TTF_MAJOR_VERSION == 2 \
    && (SDL_TTF_MINOR_VERSION > 0 || SDL_TTF_PATCHLEVEL >= 18))
        if(TTF_GlyphIsProvided32(pTTFFont.get(), codepoint) == 0) return false;
#else
        // Older accepted SDL_ttf SDKs can only query BMP characters. Use the
        // original face for supplementary characters instead of raising the
        // project's minimum SDL_ttf version for an optional display font.
        if(codepoint > 0xFFFF
            || TTF_GlyphIsProvided(pTTFFont.get(), static_cast<Uint16>(codepoint)) == 0)
            return false;
#endif
    }
    return true;
}

