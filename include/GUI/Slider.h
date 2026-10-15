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

#ifndef SLIDER_H
#define SLIDER_H

#include "Widget.h"
#include <GUI/GUIStyle.h>
#include <misc/draw_util.h>
#include <misc/SDL2pp.h>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

/**
    A horizontal slider over a fixed list of steps, lowest on the left and highest on the right.
    The track takes the left part of the widget and the selected step's text is shown on the
    right. Click or drag on the track, use the mouse wheel, or Left/Right when active.
    Its entry API matches DropDownBox (addEntry, getSelectedEntryIntData, setOnSelectionChange...).
*/
class Slider : public Widget {
public:
    Slider() {
        enableResizing(true, false);
        resize(getMinimumSize());
    }
    Slider(const Slider&) = delete;
    Slider& operator=(const Slider&) = delete;
    ~Slider() override = default;

    /// Appends a step to the right end (the next higher value).
    void addEntry(const std::string& text, int data = 0) {
        entries.emplace_back(text, data);
        if(selected < 0) selected = 0;
        invalidateTextures();
    }

    void clearAllEntries() {
        entries.clear();
        selected = -1;
        invalidateTextures();
    }

    int getNumEntries() const { return static_cast<int>(entries.size()); }
    int getEntryIntData(unsigned int index) const { return index < entries.size() ? entries[index].second : 0; }
    int getSelectedIndex() const { return selected; }
    int getSelectedEntryIntData() const { return selected >= 0 ? entries[selected].second : -1; }

    /// Selects step \a index without calling the selection callback.
    void setSelectedItem(int index) { select(index, false); }

    /// Called with true when the player changed the step, false when the program did.
    void setOnSelectionChange(std::function<void (bool)> onChange) { pOnSelectionChange = std::move(onChange); }

    bool isActivatable() const override { return isEnabled() && isVisible(); }

    Point getMinimumSize() const override { return Point(120, 18); }

    void resize(Point newSize) override { resize(newSize.x, newSize.y); }
    void resize(Uint32 width, Uint32 height) override {
        Widget::resize(width, height);
        invalidateTextures();
    }

    void handleMouseMovement(Sint32 x, Sint32 y, bool insideOverlay) override {
        hover = !insideOverlay && x >= 0 && y >= 0 && x < getSize().x && y < getSize().y;
        if(dragging) selectAt(x, true);
    }

    bool handleMouseLeft(Sint32 x, Sint32 y, bool pressed) override {
        if(!pressed) {
            const bool wasDragging = dragging;
            dragging = false;
            return wasDragging;
        }
        if(!isVisible() || !isEnabled() || x < 0 || y < 0 || x >= trackEnd() + kKnobWidth || y >= getSize().y) return false;
        setActive();
        dragging = true;
        selectAt(x, true);
        return true;
    }

    bool handleMouseWheel(Sint32 x, Sint32 y, bool up) override {
        if(!isVisible() || !isEnabled() || x < 0 || y < 0 || x >= getSize().x || y >= getSize().y) return false;
        select(selected + (up ? 1 : -1), true);
        return true;
    }

    bool handleKeyPress(SDL_KeyboardEvent& key) override {
        if(!isVisible() || !isEnabled() || !isActive()) return false;
        if(key.keysym.sym == SDLK_LEFT) { select(selected - 1, true); return true; }
        if(key.keysym.sym == SDLK_RIGHT) { select(selected + 1, true); return true; }
        return Widget::handleKeyPress(key);
    }

    void draw(Point position) override {
        if(!isVisible()) return;
        updateTextures();
        if(pBackground) {
            SDL_Rect dest = calcDrawingRect(pBackground.get(), position.x, position.y);
            SDL_RenderCopy(renderer, pBackground.get(), nullptr, &dest);
        }
        const int midY = position.y + getSize().y / 2;
        const int left = position.x + trackStart();
        const int right = position.x + trackEnd();
        const int knobX = position.x + knobPosition();
        const bool live = isEnabled();
        // Track: the part up to the knob is filled, so "more" reads as a longer bar.
        renderFillRect(renderer, left, midY - 1, right, midY + 1, COLOR_RGB(110, 110, 110));
        if(selected >= 0) renderFillRect(renderer, left, midY - 1, knobX, midY + 1, live ? COLOR_LIGHTYELLOW : COLOR_LIGHTGREY);
        const int steps = getNumEntries();
        if(steps > 1 && steps <= 16) {
            for(int i = 0; i < steps; ++i) {
                const int tickX = position.x + stepPosition(i);
                renderDrawLine(renderer, tickX, midY - 4, tickX, midY + 4, COLOR_RGB(150, 150, 150));
            }
        }
        if(selected >= 0) {
            SDL_Rect knob = { knobX - kKnobWidth / 2, position.y + 2, kKnobWidth, getSize().y - 4 };
            const Uint32 fill = !live ? COLOR_DARKGREY : ((hover || isActive() || dragging) ? COLOR_WHITE : COLOR_LIGHTGREY);
            renderFillRect(renderer, &knob, fill);
            renderDrawRect(renderer, &knob, COLOR_BLACK);
        }
        if(pText) {
            SDL_Rect dest = calcDrawingRect(pText.get(), position.x + textStart(), position.y);
            SDL_RenderCopy(renderer, pText.get(), nullptr, &dest);
        }
        if(!live) {
            SDL_Rect veil = { position.x, position.y, getSize().x, getSize().y };
            renderFillRect(renderer, &veil, COLOR_HALF_TRANSPARENT);
        }
    }

protected:
    void updateTextures() override {
        Widget::updateTextures();
        if(!pBackground && getSize().x > 0 && getSize().y > 0) {
            pBackground = convertSurfaceToTexture(GUIStyle::getInstance().createWidgetBackground(getSize().x, getSize().y));
        }
        if(!pText && selected >= 0 && textWidth() > 0) {
            pText = convertSurfaceToTexture(GUIStyle::getInstance().createLabelSurface(
                textWidth(), getSize().y, {entries[selected].first}, 12, Alignment_Left));
        }
    }

    void invalidateTextures() override {
        Widget::invalidateTextures();
        pBackground.reset();
        pText.reset();
    }

private:
    static constexpr int kKnobWidth = 8;

    /// The right ~45% shows the step's text; the track uses the rest.
    int textWidth() const { return std::max(0, getSize().x * 45 / 100 - 4); }
    int textStart() const { return getSize().x - textWidth() - 2; }
    int trackStart() const { return kKnobWidth / 2 + 4; }
    int trackEnd() const { return std::max(trackStart(), textStart() - kKnobWidth / 2 - 6); }

    int stepPosition(int index) const {
        const int steps = getNumEntries();
        if(steps <= 1) return trackStart();
        return trackStart() + (trackEnd() - trackStart()) * index / (steps - 1);
    }
    int knobPosition() const { return stepPosition(std::max(0, selected)); }

    void selectAt(Sint32 x, bool interactive) {
        const int steps = getNumEntries();
        if(steps == 0) return;
        const int span = std::max(1, trackEnd() - trackStart());
        const int clamped = std::clamp<int>(x, trackStart(), trackEnd());
        select(((clamped - trackStart()) * (steps - 1) + span / 2) / span, interactive);
    }

    void select(int index, bool interactive) {
        if(entries.empty()) return;
        index = std::clamp(index, 0, getNumEntries() - 1);
        if(index == selected) return;
        selected = index;
        pText.reset();
        if(pOnSelectionChange) pOnSelectionChange(interactive);
    }

    std::vector<std::pair<std::string, int>> entries;
    int selected = -1;
    bool hover = false;
    bool dragging = false;
    std::function<void (bool)> pOnSelectionChange;
    sdl2::texture_ptr pBackground;
    sdl2::texture_ptr pText;
};

#endif // SLIDER_H
