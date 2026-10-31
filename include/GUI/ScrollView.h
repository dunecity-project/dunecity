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

#ifndef SCROLLVIEW_H
#define SCROLLVIEW_H

#include "ScrollBar.h"
#include "Widget.h"

#include <globals.h>

#include <algorithm>

/// A lightweight scrollable viewport for a single child widget.
class ScrollView : public Widget {
public:
    ScrollView() {
        enableResizing(true, true);
        scrollbar.setOnChange([this]() { resize(getSize().x, getSize().y); });
    }

    ~ScrollView() override {
        if(content != nullptr) {
            content->setParent(nullptr);
            content = nullptr;
        }
    }

    void setPreserveContentWidth(bool value) { preserveWidth = value; }

    void setContent(Widget* newContent) {
        if(content == newContent) {
            return;
        }

        if(content != nullptr) {
            content->setParent(nullptr);
        }

        content = newContent;

        if(content != nullptr) {
            content->setParent(this);
        }

        resize(getSize().x, getSize().y);
    }

    void removeChildWidget(Widget* pChildWidget) override {
        if(pChildWidget == content) {
            content = nullptr;
        }
    }

    Point getMinimumSize() const override {
        auto size = scrollbar.getMinimumSize();
        if(preserveWidth && content != nullptr) {
            size.x = std::max(size.x, content->getMinimumSize().x + scrollbar.getMinimumSize().x);
        }
        return size;
    }

    void resize(Point newSize) override {
        resize(newSize.x, newSize.y);
    }

    void resize(Uint32 width, Uint32 height) override {
        Widget::resize(width, height);

        if(content == nullptr) {
            scrollbar.resize(scrollbar.getMinimumSize().x, height);
            scrollbar.setRange(0, 0);
            return;
        }

        const int contentMinHeight = content->getMinimumSize().y;
        const bool needsScrollbar = contentMinHeight > static_cast<int>(height);
        const int scrollbarWidth = needsScrollbar ? scrollbar.getMinimumSize().x : 0;
        const int contentWidth = std::max(0, static_cast<int>(width) - scrollbarWidth);
        const int contentHeight = std::max(static_cast<int>(height), contentMinHeight);

        content->resize(contentWidth, contentHeight);

        scrollbar.resize(scrollbar.getMinimumSize().x, height);
        scrollbar.setRange(0, std::max(0, contentHeight - static_cast<int>(height)));
        scrollbar.setBigStepSize(std::max(1, static_cast<int>(height) / 2));
    }

    void resizeAll() override {
        resize(getSize().x, getSize().y);
        if(getParent() != nullptr) {
            getParent()->resizeAll();
        }
    }

    void handleMouseMovement(Sint32 x, Sint32 y, bool insideOverlay) override {
        if(isScrollbarVisible()) {
            scrollbar.handleMouseMovement(x - getScrollbarX(), y, insideOverlay);
        }

        if(content != nullptr) {
            content->handleMouseMovement(x, y + scrollbar.getCurrentValue(), insideOverlay);
        }
    }

    bool handleMouseLeft(Sint32 x, Sint32 y, bool pressed) override {
        if(isScrollbarVisible() && x >= getScrollbarX()) {
            return scrollbar.handleMouseLeft(x - getScrollbarX(), y, pressed);
        }

        if(content != nullptr) {
            return content->handleMouseLeft(x, y + scrollbar.getCurrentValue(), pressed);
        }

        return false;
    }

    bool handleMouseRight(Sint32 x, Sint32 y, bool pressed) override {
        if(content != nullptr) {
            return content->handleMouseRight(x, y + scrollbar.getCurrentValue(), pressed);
        }

        return false;
    }

    bool handleMouseWheel(Sint32 x, Sint32 y, bool up) override {
        if(!isScrollbarVisible()) {
            return false;
        }

        if(x < 0 || x >= getSize().x || y < 0 || y >= getSize().y) {
            return false;
        }

        const int wheelStep = 24;
        scrollbar.setCurrentValue(scrollbar.getCurrentValue() + (up ? -wheelStep : wheelStep));
        return true;
    }

    void draw(Point position) override {
        if(isVisible() == false || content == nullptr) {
            return;
        }

        SDL_Rect oldClip{};
        const SDL_bool oldClipEnabled = SDL_RenderIsClipEnabled(renderer);
        SDL_RenderGetClipRect(renderer, &oldClip);

        SDL_Rect clip{ position.x, position.y, getContentWidth(), getSize().y };
        SDL_RenderSetClipRect(renderer, &clip);
        content->draw(position + Point(0, -scrollbar.getCurrentValue()));

        if(oldClipEnabled == SDL_TRUE) {
            SDL_RenderSetClipRect(renderer, &oldClip);
        } else {
            SDL_RenderSetClipRect(renderer, nullptr);
        }

        if(isScrollbarVisible()) {
            scrollbar.draw(position + Point(getScrollbarX(), 0));
        }
    }

    void drawOverlay(Point position) override {
        if(isVisible() == false || content == nullptr) {
            return;
        }

        content->drawOverlay(position + Point(0, -scrollbar.getCurrentValue()));
    }

private:
    bool isScrollbarVisible() const {
        return scrollbar.getRangeMin() != scrollbar.getRangeMax();
    }

    int getScrollbarX() const {
        return std::max(0, getSize().x - scrollbar.getSize().x);
    }

    int getContentWidth() const {
        return isScrollbarVisible() ? getScrollbarX() : getSize().x;
    }

    Widget* content = nullptr;
    bool preserveWidth = false;
    ScrollBar scrollbar;
};

#endif // SCROLLVIEW_H
