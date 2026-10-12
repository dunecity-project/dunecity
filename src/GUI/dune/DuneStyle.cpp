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

#include <GUI/dune/DuneStyle.h>
#include <misc/draw_util.h>
#include <GUI/Widget.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/FontManager.h>

#include <globals.h>

#include <algorithm>

void DuneStyle::drawControlFrame(SDL_Surface* surface, bool pressed, bool activated,
                                 const SDL_Rect& labelRect) const {
    // Reuse the original FAME sand pattern, dyed into a carved brass control.
    // Only this owned RGB surface changes, never the shared artwork seed.
    auto* artwork = classicTexture;
    if(!artwork && pGFXManager) artwork = pGFXManager->getBackgroundSurface();
    const bool hasArtwork = artwork && artwork->w > 0 && artwork->h > 0;
    fillClassicBackground(surface);
    {
        sdl2::surface_lock lock(surface);
        for(int y = 0; y < surface->h; ++y) {
            const int relief = surface->h > 1 ? 8 - (16*y)/(surface->h-1) : 0;
            for(int x = 0; x < surface->w; ++x) {
                const auto sand = RGBA2SDL(getPixel(surface, x, y));
                const int grain = hasArtwork
                    ? (299*sand.r + 587*sand.g + 114*sand.b)/1000 - 155
                    : ((x*17 + y*23 + (x*y)%19)%31) - 15;
                // Softer grain under the label protects its contrast without
                // introducing a flat rectangle in the textured button face.
                const bool underLabel = x >= labelRect.x-6
                    && x < labelRect.x+labelRect.w+6;
                const int variation = underLabel ? std::clamp(grain/6, -8, 8)
                                                 : std::clamp(grain/2, -22, 22);
                const int red = (pressed ? 211 : 230) + relief + variation;
                const int green = (pressed ? 161 : 182) + relief + variation;
                const int blue = (pressed ? 51 : 68) + relief/2 + variation/2;
                putPixel(surface, x, y, COLOR_RGB(std::clamp(red, 0, 255),
                    std::clamp(green, 0, 255), std::clamp(blue, 0, 255)));
            }
        }
    }
    drawRect(surface, 0, 0, surface->w-1, surface->h-1, activated ? theme.accent : theme.border);
    if(surface->w < 4 || surface->h < 4) return;
    const auto upper = pressed ? theme.edgeDark : theme.edgeLight;
    const auto lower = pressed ? theme.edgeLight : theme.edgeDark;
    drawHLine(surface, 1, 1, surface->w-2, upper);
    drawVLine(surface, 1, 1, surface->h-2, upper);
    drawHLine(surface, 1, surface->h-2, surface->w-2, lower);
    drawVLine(surface, surface->w-2, 1, surface->h-2, lower);
    if(surface->w < 8 || surface->h < 8) return;
    // Three carved bands stay within the existing control and hit-test bounds.
    drawHLine(surface, 2, 2, surface->w-3, upper);
    drawVLine(surface, 2, 2, surface->h-3, upper);
    drawHLine(surface, 2, surface->h-3, surface->w-3, lower);
    drawVLine(surface, surface->w-3, 2, surface->h-3, lower);
    drawRect(surface, 3, 3, surface->w-4, surface->h-4,
             pressed ? theme.edgeDark : COLOR_RGB(189,126,28));

    // End fittings belong only on roomy buttons, never on long translated
    // labels or small controls. The font size and text position do not change.
    if(surface->w >= 120 && surface->h >= 28
       && labelRect.x >= 30 && labelRect.x+labelRect.w <= surface->w-30) {
        const int centerY = surface->h/2 + (pressed ? 1 : 0);
        const Uint32 cut = pressed ? theme.border : COLOR_RGB(138,81,15);
        const Uint32 glint = pressed ? theme.control : theme.edgeLight;
        sdl2::surface_lock lock(surface);
        for(const int centerX : {13, surface->w-14}) {
            for(int dy = -3; dy <= 3; ++dy) {
                const int reach = 3-std::abs(dy);
                drawHLineNoLock(surface, centerX-reach, centerY+dy,
                                centerX+reach, dy < 0 ? glint : cut);
            }
            putPixel(surface, centerX, centerY, activated ? theme.accent : theme.edgeDark);
            drawVLineNoLock(surface, centerX-6, centerY-5, centerY+5, cut);
            drawVLineNoLock(surface, centerX+6, centerY-5, centerY+5, cut);
            drawVLineNoLock(surface, centerX-5, centerY-4, centerY+4, glint);
            drawVLineNoLock(surface, centerX+7, centerY-4, centerY+4, glint);
        }
    }
}

void DuneStyle::fillClassicBackground(SDL_Surface* surface) const {
    SDL_FillRect(surface, nullptr, theme.background);
    // During GFXManager construction its global pointer is not yet assigned.
    // The local style receives an authentic FAME.CPS seed explicitly instead.
    auto* texture = classicTexture;
    if(!texture && pGFXManager) texture = pGFXManager->getBackgroundSurface();
    if(!texture || texture->w <= 0 || texture->h <= 0) return;
    for(int y = 0; y < surface->h; y += texture->h) {
        for(int x = 0; x < surface->w; x += texture->w) {
            SDL_Rect destination{x, y, texture->w, texture->h};
            SDL_BlitSurface(texture, nullptr, surface, &destination);
        }
    }
}

sdl2::surface_ptr DuneStyle::createSurfaceWithText(const std::string& text, Uint32 color, unsigned int fontsize) {
    if(pFontManager) {
        return pFontManager->createSurfaceWithText(text, color, fontsize);
    } else {
        // create dummy surface
        auto surface = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, text.length() * 10, 12, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
        if(surface == nullptr) {
            return nullptr;
        }

        SDL_FillRect(surface.get(), nullptr, COLOR_TRANSPARENT);

        return surface;
    }
}

unsigned int DuneStyle::getTextHeight(unsigned int FontNum) {
    if(pFontManager) {
        return pFontManager->getTextHeight(FontNum);
    } else {
        return 12;
    }
}

unsigned int DuneStyle::getTextWidth(const std::string& text, unsigned int FontNum)  {
    if(pFontManager) {
        return pFontManager->getTextWidth(text,FontNum);
    } else {
        return text.length()*10;
    }
}


Point DuneStyle::getMinimumLabelSize(const std::string& text, int fontSize) {
    return Point(getTextWidth(text,fontSize) + 12,getTextHeight(fontSize) + 4);
}

sdl2::surface_ptr DuneStyle::createLabelSurface(Uint32 width, Uint32 height, const std::vector<std::string>& textLines, int fontSize, Alignment_Enum alignment, Uint32 textcolor, Uint32 textshadowcolor, Uint32 backgroundcolor) {

    // create surfaces
    sdl2::surface_ptr surface = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, width, height, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if(surface == nullptr) {
        return nullptr;
    }

    SDL_FillRect(surface.get(), nullptr, backgroundcolor);

    if(textcolor == COLOR_DEFAULT) textcolor = textPalette.foreground;
    // Explicit black outlines identify labels intentionally drawn over terrain
    // or original dark artwork, not a gold menu surface. Preserve those colors
    // so population, demand, watermark and Mentat text retain their contrast.
    else if(backgroundcolor == COLOR_TRANSPARENT
            && !(uiSkin == 1 && textshadowcolor == COLOR_BLACK))
        textcolor = MenuTheme::readableText(textcolor, uiSkin);
    if(textshadowcolor == COLOR_DEFAULT) textshadowcolor = textPalette.shadow;

    int fontheight = getTextHeight(fontSize);
    int spacing = 2;

    int textpos_y;

    if(alignment & Alignment_VCenter) {
        int textheight = fontheight * textLines.size() + spacing * (textLines.size() - 1);
        textpos_y = (((int) height) - textheight) / 2;
    } else if(alignment & Alignment_Bottom) {
        int textheight = fontheight * textLines.size() + spacing * (textLines.size() - 1);
        textpos_y = ((int) height) - textheight - spacing;
    } else {
        // Alignment_Top
        textpos_y = spacing;
    }

    for (const std::string& textLine : textLines) {
        sdl2::surface_ptr textSurface1 = createSurfaceWithText(textLine, textshadowcolor, fontSize);
        sdl2::surface_ptr textSurface2 = createSurfaceWithText(textLine, textcolor, fontSize);

        SDL_Rect textRect1 = calcDrawingRect(textSurface1.get(), 0, textpos_y + 4);
        SDL_Rect textRect2 = calcDrawingRect(textSurface2.get(), 0, textpos_y + 3);
        if(alignment & Alignment_Left) {
            textRect1.x = 4;
            textRect2.x = 3;
        } else if(alignment & Alignment_Right) {
            textRect1.x = width - textSurface1->w - 4;
            textRect2.x = width - textSurface2->w - 3;
        } else {
            // Alignment_HCenter
            textRect1.x = ((surface->w - textSurface1->w) / 2)+3;
            textRect2.x = ((surface->w - textSurface2->w) / 2)+2;
        }

        SDL_BlitSurface(textSurface1.get(),nullptr,surface.get(),&textRect1);

        SDL_BlitSurface(textSurface2.get(),nullptr,surface.get(),&textRect2);

        textpos_y += fontheight + spacing;
    }

    return surface;
}





Point DuneStyle::getMinimumCheckboxSize(const std::string& text) {
    return Point(getTextWidth(text,14) + 20 + 17,getTextHeight(14) + 8);
}

sdl2::surface_ptr DuneStyle::createCheckboxSurface(Uint32 width, Uint32 height, const std::string& text, bool checked, bool activated, Uint32 textcolor, Uint32 textshadowcolor, Uint32 backgroundcolor) {
    sdl2::surface_ptr surface = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, width, height, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if(!surface) {
        return nullptr;
    }

    SDL_FillRect(surface.get(), nullptr, backgroundcolor);

    if(textcolor == COLOR_DEFAULT) textcolor = textPalette.foreground;
    else if(backgroundcolor == COLOR_TRANSPARENT) textcolor = MenuTheme::readableText(textcolor, uiSkin);
    if(textshadowcolor == COLOR_DEFAULT) textshadowcolor = textPalette.shadow;

    if(activated) {
        textcolor = brightenUp(textcolor);
    }

    drawRect(surface.get(), 4, 5, 4 + 17, 5 + 17, textcolor);
    drawRect(surface.get(), 4 + 1, 5 + 1, 4 + 17 - 1, 5 + 17 - 1, textcolor);

    if(checked) {
        int x1 = 4 + 2;
        int y1 = 5 + 2;
        int x2 = 4 + 17 - 2;

        for(int i = 0; i < 15; i++) {
            // North-West to South-East
            putPixel(surface.get(), x1, y1, textcolor);
            putPixel(surface.get(), x1+1, y1, textcolor);
            putPixel(surface.get(), x1-1, y1, textcolor);

            // North-East to South-West
            putPixel(surface.get(), x2, y1, textcolor);
            putPixel(surface.get(), x2+1, y1, textcolor);
            putPixel(surface.get(), x2-1, y1, textcolor);

            x1++;
            y1++;
            x2--;
        }
    }

    sdl2::surface_ptr textSurface1 = createSurfaceWithText(text, textshadowcolor, 14);
    SDL_Rect textRect1 = calcDrawingRect(textSurface1.get(), 10 + 2 + 17, surface->h / 2 + 5, HAlign::Left, VAlign::Center);
    SDL_BlitSurface(textSurface1.get(), nullptr, surface.get(), &textRect1);

    sdl2::surface_ptr textSurface2 = createSurfaceWithText(text, textcolor, 14);
    SDL_Rect textRect2 = calcDrawingRect(textSurface2.get(), 10 + 1 + 17, surface->h / 2 + 4, HAlign::Left, VAlign::Center);
    SDL_BlitSurface(textSurface2.get(), nullptr, surface.get(), &textRect2);

    return surface;
}

Point DuneStyle::getMinimumRadioButtonSize(const std::string& text) {
    return Point(getTextWidth(text,14) + 16 + 15,getTextHeight(14) + 8);
}

sdl2::surface_ptr DuneStyle::createRadioButtonSurface(Uint32 width, Uint32 height, const std::string& text, bool checked, bool activated, Uint32 textcolor, Uint32 textshadowcolor, Uint32 backgroundcolor) {
    sdl2::surface_ptr surface = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, width, height, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if(!surface) {
        return nullptr;
    }

    SDL_FillRect(surface.get(), nullptr, backgroundcolor);

    if(textcolor == COLOR_DEFAULT) textcolor = textPalette.foreground;
    else if(backgroundcolor == COLOR_TRANSPARENT) textcolor = MenuTheme::readableText(textcolor, uiSkin);
    if(textshadowcolor == COLOR_DEFAULT) textshadowcolor = textPalette.shadow;

    if(activated) {
        textcolor = brightenUp(textcolor);
    }

    drawHLineNoLock(surface.get(), 8, 7, 13, textcolor);
    drawHLineNoLock(surface.get(), 7, 8, 14, textcolor);
    drawHLineNoLock(surface.get(), 6, 9, 8, textcolor);
    drawHLineNoLock(surface.get(), 13, 9, 15, textcolor);
    drawHLineNoLock(surface.get(), 5, 10, 6, textcolor);
    drawHLineNoLock(surface.get(), 15, 10, 16, textcolor);
    drawHLineNoLock(surface.get(), 4, 11, 6, textcolor);
    drawHLineNoLock(surface.get(), 15, 11, 17, textcolor);

    drawVLineNoLock(surface.get(), 4, 12, 15, textcolor);
    drawVLineNoLock(surface.get(), 5, 12, 15, textcolor);
    drawVLineNoLock(surface.get(), 16, 12, 15, textcolor);
    drawVLineNoLock(surface.get(), 17, 12, 15, textcolor);

    drawHLineNoLock(surface.get(), 4, 16, 6, textcolor);
    drawHLineNoLock(surface.get(), 15, 16, 17, textcolor);
    drawHLineNoLock(surface.get(), 5, 17, 6, textcolor);
    drawHLineNoLock(surface.get(), 15, 17, 16, textcolor);
    drawHLineNoLock(surface.get(), 6, 18, 8, textcolor);
    drawHLineNoLock(surface.get(), 13, 18, 15, textcolor);
    drawHLineNoLock(surface.get(), 7, 19, 14, textcolor);
    drawHLineNoLock(surface.get(), 8, 20, 13, textcolor);

    if(checked) {
        drawHLineNoLock(surface.get(), 9, 11, 12, textcolor);
        drawHLineNoLock(surface.get(), 8, 12, 13, textcolor);
        drawHLineNoLock(surface.get(), 8, 13, 13, textcolor);
        drawHLineNoLock(surface.get(), 8, 14, 13, textcolor);
        drawHLineNoLock(surface.get(), 8, 15, 13, textcolor);
        drawHLineNoLock(surface.get(), 9, 16, 12, textcolor);
    }

    sdl2::surface_ptr textSurface1 = createSurfaceWithText(text, textshadowcolor, 14);
    SDL_Rect textRect1 = calcDrawingRect(textSurface1.get(), 8 + 2 + 15, surface->h / 2 + 5, HAlign::Left, VAlign::Center);
    SDL_BlitSurface(textSurface1.get(), nullptr, surface.get(), &textRect1);

    sdl2::surface_ptr textSurface2 = createSurfaceWithText(text, textcolor, 14);
    SDL_Rect textRect2 = calcDrawingRect(textSurface2.get(), 8 + 1 + 15, surface->h / 2 + 4, HAlign::Left, VAlign::Center);
    SDL_BlitSurface(textSurface2.get(), nullptr, surface.get(), &textRect2);

    return surface;
}



sdl2::surface_ptr DuneStyle::createDropDownBoxButton(Uint32 size, bool pressed, bool activated, Uint32 color) {
    color = textPalette.foreground;

    // create surfaces
    sdl2::surface_ptr surface = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, size, size, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if(!surface) {
        return nullptr;
    }

    // create button background
    if(pressed == false) {
        // normal mode
        SDL_FillRect(surface.get(), nullptr, theme.control);
        drawRect(surface.get(), 0, 0, surface->w-1, surface->h-1, activated && uiSkin == 1 ? theme.accent : theme.border);
        drawHLine(surface.get(), 1, 1, surface->w-2, theme.edgeLight);
        drawVLine(surface.get(), 1, 1, surface->h-2, theme.edgeLight);
        drawHLine(surface.get(), 1, surface->h-2, surface->w-2, theme.edgeDark);
        drawVLine(surface.get(), surface->w-2, 1, surface->h-2, theme.edgeDark);
    } else {
        // pressed button mode
        SDL_FillRect(surface.get(), nullptr, theme.pressed);
        drawRect(surface.get(), 0, 0, surface->w-1, surface->h-1, theme.border);
        drawRect(surface.get(), 1, 1, surface->w-2, surface->h-2, theme.edgeDark);
    }

    int col = (pressed | activated) ? brightenUp(color) : color;

    int x1 = 3;
    int x2 = size-3-1;
    int y = size/3 - 1;

    // draw separated hline
    drawHLine(surface.get(), x1, y, x2, col);
    y+=2;

    // down arrow
    for(;x1 <= x2; ++x1, --x2, ++y) {
        drawHLine(surface.get(), x1, y, x2, col);
    }

    return surface;
}




Point DuneStyle::getMinimumButtonSize(const std::string& text) {
    return Point(getTextWidth(text.c_str(),12)+12,getTextHeight(12));
}

sdl2::surface_ptr DuneStyle::createButtonSurface(Uint32 width, Uint32 height, const std::string& text, bool pressed, bool activated, Uint32, Uint32) {
    sdl2::surface_ptr surface{SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SCREEN_FORMAT)};
    if(!surface) return nullptr;
    int fontsize = height >= 48 ? 22 : height >= 38 ? 20 : height >= 30 ? 18 : height >= 24 ? 16 : 14;
    while(fontsize>8 && (width < getTextWidth(text,fontsize)+12 || height < getTextHeight(fontsize)+4)) --fontsize;
    const auto lettering = uiSkin == 1
        ? (pressed ? pressedButtonTextPalette : buttonTextPalette) : textPalette;
    auto label=createSurfaceWithText(text,lettering.foreground,fontsize);
    SDL_Rect rect=calcDrawingRect(label.get(),width/2+(pressed ? 1 : 0),height/2+(pressed ? 1 : 0),HAlign::Center,VAlign::Center);
    if(uiSkin == 1) {
        drawControlFrame(surface.get(), pressed, activated, rect);
        auto emboss = createSurfaceWithText(text, lettering.shadow, fontsize);
        SDL_Rect embossRect = rect;
        ++embossRect.x;
        ++embossRect.y;
        SDL_BlitSurface(emboss.get(), nullptr, surface.get(), &embossRect);
    } else {
        // Modern remains byte-for-byte visually equivalent to its prior style.
        SDL_FillRect(surface.get(),nullptr,pressed ? theme.pressed : theme.control);
        drawRect(surface.get(),0,0,width-1,height-1,activated ? theme.accent : theme.border);
    }
    SDL_BlitSurface(label.get(),nullptr,surface.get(),&rect);
    return surface;
}





Point DuneStyle::getMinimumTextBoxSize(int fontSize) {
    return Point(10,getTextHeight(fontSize) + 6);
}

sdl2::surface_ptr DuneStyle::createTextBoxSurface(Uint32 width, Uint32 height, const std::string& text, bool carret, int fontSize, Alignment_Enum alignment, Uint32 textcolor, Uint32 textshadowcolor) {
    sdl2::surface_ptr surface = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, width, height, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if(!surface) {
        return nullptr;
    }

    SDL_FillRect(surface.get(), nullptr, theme.control);
    drawRect(surface.get(),0,0,surface->w-1,surface->h-1,theme.border);

    drawHLine(surface.get(),1,1,surface->w-2,theme.edgeDark);
    drawHLine(surface.get(),1,2,surface->w-2,theme.edgeDark);
    drawVLine(surface.get(),1,1,surface->h-2,theme.edgeDark);
    drawVLine(surface.get(),2,1,surface->h-2,theme.edgeDark);
    drawHLine(surface.get(),1,surface->h-2,surface->w-2,theme.edgeLight);
    drawVLine(surface.get(),surface->w-2,1,surface->h-2,theme.edgeLight);

    textcolor = textPalette.foreground;
    if(textshadowcolor == COLOR_DEFAULT) textshadowcolor = textPalette.shadow;

    SDL_Rect cursorPos;

    // create text in this text box
    if(text.size() != 0) {
        auto textSurface1 = createSurfaceWithText(text, textshadowcolor, fontSize);
        auto textSurface2 = createSurfaceWithText(text, textcolor, fontSize);
        SDL_Rect textRect1 = calcDrawingRect(textSurface1.get(), 0, surface->h/2 + 4, HAlign::Left, VAlign::Center);
        SDL_Rect textRect2 = calcDrawingRect(textSurface2.get(), 0, surface->h/2 + 3, HAlign::Left, VAlign::Center);

        if(alignment & Alignment_Left) {
            textRect1.x = 6;
            textRect2.x = 5;
        } else if(alignment & Alignment_Right) {
            textRect1.x = width - textSurface1->w - 5;
            textRect2.x = width - textSurface2->w - 4;
        } else {
            // Alignment_HCenter
            textRect1.x = ((surface->w - textSurface1->w) / 2)+3;
            textRect2.x = ((surface->w - textSurface2->w) / 2)+2;
        }

        if(textRect1.w > surface->w - 10) {
            textRect1.x -= (textSurface1->w - (surface->w - 10));
            textRect2.x -= (textSurface2->w - (surface->w - 10));
        }

        cursorPos.x = textRect2.x + textSurface2->w + 2;

        SDL_BlitSurface(textSurface1.get(),nullptr,surface.get(),&textRect1);

        SDL_BlitSurface(textSurface2.get(),nullptr,surface.get(),&textRect2);

        cursorPos.w = 1;
    } else {
        if(alignment & Alignment_Left) {
            cursorPos.x = 6;
        } else if(alignment & Alignment_Right) {
            cursorPos.x = width - 5;
        } else {
            // Alignment_HCenter
            cursorPos.x = surface->w / 2;
        }
        cursorPos.w = 1;
    }

    cursorPos.y = surface->h / 2 - 8;
    cursorPos.h = 16;

    if(carret == true) {
        drawVLine(surface.get(),cursorPos.x-2,cursorPos.y,cursorPos.y+cursorPos.h,textcolor);
        drawVLine(surface.get(),cursorPos.x-1,cursorPos.y,cursorPos.y+cursorPos.h,textcolor);
    }

    return surface;
}




Point DuneStyle::getMinimumScrollBarArrowButtonSize() {
    return Point(17,17);
}

sdl2::surface_ptr DuneStyle::createScrollBarArrowButton(bool down, bool pressed, bool activated, Uint32 color) {
    color = textPalette.foreground;

    // create surfaces
    sdl2::surface_ptr surface = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, 17, 17, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if(!surface) {
        return nullptr;
    }

    // create button background
    if(pressed == false) {
        // normal mode
        SDL_FillRect(surface.get(), nullptr, theme.control);
        drawRect(surface.get(), 0, 0, surface->w-1, surface->h-1, activated && uiSkin == 1 ? theme.accent : theme.border);
        drawHLine(surface.get(), 1, 1, surface->w-2, theme.edgeLight);
        drawVLine(surface.get(), 1, 1, surface->h-2, theme.edgeLight);
        drawHLine(surface.get(), 1, surface->h-2, surface->w-2, theme.edgeDark);
        drawVLine(surface.get(), surface->w-2, 1, surface->h-2, theme.edgeDark);
    } else {
        // pressed button mode
        SDL_FillRect(surface.get(), nullptr, theme.pressed);
        drawRect(surface.get(), 0, 0, surface->w-1, surface->h-1, theme.border);
        drawRect(surface.get(), 1, 1, surface->w-2, surface->h-2, theme.edgeDark);
    }

    int col = (pressed | activated) ? brightenUp(color) : color;

    // draw arrow
    if(down == true) {
        // down arrow
        drawHLine(surface.get(),3,4,13,col);
        drawHLine(surface.get(),4,5,12,col);
        drawHLine(surface.get(),5,6,11,col);
        drawHLine(surface.get(),6,7,10,col);
        drawHLine(surface.get(),7,8,9,col);
        drawHLine(surface.get(),8,9,8,col);
    } else {
        // up arrow
        drawHLine(surface.get(),8,5,8,col);
        drawHLine(surface.get(),7,6,9,col);
        drawHLine(surface.get(),6,7,10,col);
        drawHLine(surface.get(),5,8,11,col);
        drawHLine(surface.get(),4,9,12,col);
        drawHLine(surface.get(),3,10,13,col);
    }

    return surface;
}




Uint32 DuneStyle::getListBoxEntryHeight() {
    return 20;
}

sdl2::surface_ptr DuneStyle::createListBoxEntry(Uint32 width, const std::string& text, bool selected, Uint32 color) {
    color = textPalette.foreground;

    // create surfaces
    sdl2::surface_ptr surface = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, width, getListBoxEntryHeight(), SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if(!surface) {
        return nullptr;
    }

    if(selected == true) {
        SDL_FillRect(surface.get(), nullptr, theme.control);
        if(uiSkin == 1) drawRect(surface.get(), 0, 0, surface->w-1, surface->h-1, theme.accent);
    } else {
        SDL_FillRect(surface.get(), nullptr, COLOR_TRANSPARENT);
    }

    sdl2::surface_ptr textSurface = createSurfaceWithText(text, color, 14);
    SDL_Rect textRect = calcDrawingRect(textSurface.get(), 3, surface->h/2 + 2, HAlign::Left, VAlign::Center);
    SDL_BlitSurface(textSurface.get(),nullptr,surface.get(),&textRect);

    return surface;
}




sdl2::surface_ptr DuneStyle::createProgressBarOverlay(Uint32 width, Uint32 height, double percent, Uint32 color) {
    sdl2::surface_ptr pSurface = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, width, height, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if(!pSurface) {
        return nullptr;
    }

    SDL_FillRect(pSurface.get(), nullptr, COLOR_TRANSPARENT);

    if(color == COLOR_DEFAULT) {
        // default color

        int max_i = std::max( (int) lround(percent*(( ((int) width) - 4)/100.0)), 0);

        sdl2::surface_lock lock(pSurface.get());

        SDL_Rect dest = { 2, 2, max_i, ((int)height)-4 };
        SDL_FillRect(pSurface.get(), &dest, COLOR_HALF_TRANSPARENT);
    } else {
        int max_i = lround(percent*(width/100.0));

        SDL_Rect dest = { 0, 0, max_i, (int)height };
        SDL_FillRect(pSurface.get(), &dest, color);
    }

    return pSurface;
}



sdl2::surface_ptr DuneStyle::createToolTip(const std::string& text) {
    sdl2::surface_ptr helpTextSurface = createSurfaceWithText(text, textPalette.foreground, 14);
    if(helpTextSurface == nullptr) {
        return nullptr;
    }

    // create surfaces
    sdl2::surface_ptr surface = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, helpTextSurface->w + 5, helpTextSurface->h + 2, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
    if(surface == nullptr) {
        return nullptr;
    }

    SDL_FillRect(surface.get(), nullptr, theme.background);

    drawRect(surface.get(), 0, 0, helpTextSurface->w + 5 - 1, helpTextSurface->h + 2 - 1, theme.accent);

    SDL_Rect textRect = calcDrawingRect(helpTextSurface.get(), 3, 3);
    SDL_BlitSurface(helpTextSurface.get(), nullptr, surface.get(), &textRect);

    return surface;
}



sdl2::surface_ptr DuneStyle::createBackground(Uint32 width, Uint32 height) {
    sdl2::surface_ptr surface{SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SCREEN_FORMAT)};
    if(!surface) return nullptr;
    if(uiSkin == 1) fillClassicBackground(surface.get());
    else SDL_FillRect(surface.get(),nullptr,theme.background);
    drawRect(surface.get(),0,0,width-1,height-1,theme.border);
    if(uiSkin == 1 && width >= 6 && height >= 6) {
        drawHLine(surface.get(), 1, 1, width-2, theme.edgeLight);
        drawVLine(surface.get(), 1, 1, height-2, theme.edgeLight);
        drawHLine(surface.get(), 1, height-2, width-2, theme.edgeDark);
        drawVLine(surface.get(), width-2, 1, height-2, theme.edgeDark);
    }
    return surface;
}


sdl2::surface_ptr DuneStyle::createWidgetBackground(Uint32 width, Uint32 height) {
    sdl2::surface_ptr surface = sdl2::surface_ptr{ SDL_CreateRGBSurface(0, width, height, SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };

    if(!surface) {
        return nullptr;
    }

    SDL_FillRect(surface.get(), nullptr, theme.pressed);
    drawRect(surface.get(), 0, 0, surface->w-1, surface->h-1, theme.border);
    drawRect(surface.get(), 1, 1, surface->w-2, surface->h-2, theme.edgeDark);

    return surface;
}
