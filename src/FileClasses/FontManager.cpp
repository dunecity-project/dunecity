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

#include <FileClasses/FontManager.h>

#include <globals.h>

#include <misc/draw_util.h>
#include <misc/FileSystem.h>

#include <FileClasses/FileManager.h>
#include <FileClasses/TTFFont.h>
#include <FileClasses/RetroPixelFont.h>
#include <FileClasses/LoadSavePNG.h>

#include <list>
#include <exception>
#include <algorithm>

FontManager::FontManager() = default;

FontManager::~FontManager() = default;

void FontManager::drawTextOnSurface(SDL_Surface* pSurface, const std::string& text, Uint32 color, unsigned int fontSize) {
    return getFont(fontSize)->drawTextOnSurface(pSurface,text,color);
}

int FontManager::getTextWidth(const std::string& text, unsigned int fontSize) {
    return getFont(fontSize)->getTextWidth(text);
}

int FontManager::getTextHeight(unsigned int fontSize) {
    return getFont(fontSize)->getTextHeight();
}

sdl2::surface_ptr FontManager::createSurfaceWithText(const std::string& text, Uint32 color, unsigned int fontSize) {
    Font* pFont = getFont(fontSize);

    int width = pFont->getTextWidth(text);
    int height = pFont->getTextHeight();
    sdl2::surface_ptr pic = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, width, height, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if(!pic) {
        return nullptr;
    }

    SDL_SetSurfaceBlendMode(pic.get(), SDL_BLENDMODE_NONE);
    SDL_FillRect(pic.get(), nullptr, COLOR_INVALID);
    SDL_SetColorKey(pic.get(), SDL_TRUE, COLOR_INVALID);

    pFont->drawTextOnSurface(pic.get(),text,color);

    return pic;
}

sdl2::texture_ptr FontManager::createTextureWithText(const std::string& text, Uint32 color, unsigned int fontSize) {
    return convertSurfaceToTexture(createSurfaceWithText(text, color, fontSize));
}

sdl2::surface_ptr FontManager::createSurfaceWithMultilineText(const std::string& text, Uint32 color, unsigned int fontSize, bool bCentered) {
    size_t startpos = 0;
    size_t nextpos;
    std::list<std::string> textLines;
    do {
        nextpos = text.find("\n",startpos);
        if(nextpos == std::string::npos) {
            textLines.push_back(text.substr(startpos,text.length()-startpos));
        } else {
            textLines.push_back(text.substr(startpos,nextpos-startpos));
            startpos = nextpos+1;
        }
    } while(nextpos != std::string::npos);

    Font* pFont = getFont(fontSize);

    int lineHeight = pFont->getTextHeight();
    int width = pFont->getTextWidth(text);
    int height = lineHeight * textLines.size() + (lineHeight * (textLines.size()-1))/2;

    // create new picture surface
    auto pic = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, width, height, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if(pic == nullptr) {
        return nullptr;
    }

    SDL_SetSurfaceBlendMode(pic.get(), SDL_BLENDMODE_NONE);
    SDL_FillRect(pic.get(), nullptr, COLOR_INVALID);
    SDL_SetColorKey(pic.get(), SDL_TRUE, COLOR_INVALID);

    int currentLineNum = 0;
    for(const std::string& textLine : textLines) {
        auto tmpSurface = createSurfaceWithText(textLine, color, fontSize);

        SDL_Rect dest = calcDrawingRect(tmpSurface.get(), bCentered ? width/2 : 0, currentLineNum*lineHeight, bCentered ? HAlign::Center : HAlign::Left, VAlign::Top);
        SDL_BlitSurface(tmpSurface.get(),nullptr,pic.get(),&dest);

        currentLineNum++;
    }

    return pic;
}

sdl2::texture_ptr FontManager::createTextureWithMultilineText(const std::string& text, Uint32 color, unsigned int fontSize, bool bCentered) {
    return convertSurfaceToTexture(createSurfaceWithMultilineText(text, color, fontSize, bCentered));
}

std::unique_ptr<Font> FontManager::loadFont(unsigned int fontSize) {
    return std::make_unique<TTFFont>( pFileManager->openFile("Philosopher-Bold.ttf"), fontSize );
}

Font* FontManager::getDisplayFont(const std::string& text, unsigned int fontSize) {
    if(!displayFontUnavailable) {
        auto iter = displayFonts.find(fontSize);
        if(iter == displayFonts.end()) {
            try {
                auto font = std::make_unique<TTFFont>(pFileManager->openFile("Righteous-Regular.ttf"), fontSize, 80);
                iter = displayFonts.emplace(fontSize, std::move(font)).first;
            } catch(const std::exception& error) {
                // This face is optional: old installations and damaged optional
                // files keep usable labels. Avoid repeating the warning per size.
                displayFontUnavailable = true;
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Lapis Enamel display font unavailable; using the normal font: %s", error.what());
            }
        }
        if(!displayFontUnavailable && iter->second->canRenderText(text)) {
            return iter->second.get();
        }
    }
    return getFont(fontSize);
}

int FontManager::getDisplayTextWidth(const std::string& text, unsigned int fontSize) {
    return getDisplayFont(text, fontSize)->getTextWidth(text);
}

int FontManager::getDisplayTextHeight(const std::string& text, unsigned int fontSize) {
    return getDisplayFont(text, fontSize)->getTextHeight();
}

sdl2::surface_ptr FontManager::createDisplaySurfaceWithText(const std::string& text, Uint32 color, unsigned int fontSize) {
    Font* pFont = getDisplayFont(text, fontSize);
    const auto regularFont = fonts.find(fontSize);
    if(regularFont != fonts.end() && pFont == regularFont->second.get()) {
        // Fallback is the exact existing font surface, including its key,
        // blend mode and pixels; do not introduce display-only compositing.
        return createSurfaceWithText(text, color, fontSize);
    }
    const int width = pFont->getTextWidth(text);
    const int height = pFont->getTextHeight();
    sdl2::surface_ptr pic { SDL_CreateRGBSurface(0, width, height, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if(!pic) {
        return nullptr;
    }

    SDL_FillRect(pic.get(), nullptr, 0);
    SDL_SetSurfaceBlendMode(pic.get(), SDL_BLENDMODE_BLEND);
    pFont->drawTextOnSurface(pic.get(), text, color);
    return pic;
}

Font* FontManager::getRetroFont(const std::string& text, unsigned int fontSize) {
    auto iter = retroFonts.find(fontSize);
    if(iter == retroFonts.end()) {
        iter = retroFonts.emplace(fontSize, std::make_unique<RetroPixelFont>(fontSize)).first;
    }
    return iter->second->canRenderText(text) ? iter->second.get() : getFont(fontSize);
}

int FontManager::getRetroTextWidth(const std::string& text, unsigned int fontSize) {
    return getRetroFont(text, fontSize)->getTextWidth(text);
}

int FontManager::getRetroTextHeight(const std::string& text, unsigned int fontSize) {
    return getRetroFont(text, fontSize)->getTextHeight();
}

sdl2::surface_ptr FontManager::createRetroSurfaceWithText(const std::string& text, Uint32 color, unsigned int fontSize) {
    Font* font = getRetroFont(text, fontSize);
    const auto regularFont = fonts.find(fontSize);
    if(regularFont != fonts.end() && font == regularFont->second.get()) {
        // Preserve the existing font's pixels, metrics and key/blend behavior,
        // including its handling of invalid UTF-8. Never substitute '?' masks.
        return createSurfaceWithText(text, color, fontSize);
    }
    const int width = std::max(1, font->getTextWidth(text));
    const int height = font->getTextHeight();
    sdl2::surface_ptr surface { SDL_CreateRGBSurface(0, width, height, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if(!surface) return nullptr;
    SDL_FillRect(surface.get(), nullptr, 0);
    SDL_SetSurfaceBlendMode(surface.get(), SDL_BLENDMODE_BLEND);
    font->drawTextOnSurface(surface.get(), text, color);
    return surface;
}
