#ifndef DUNECITY_UNIT_ACTION_BAR_H
#define DUNECITY_UNIT_ACTION_BAR_H

#include <GUI/StaticContainer.h>
#include <misc/MobilePlatform.h>
#include <initializer_list>

// Keep available unit actions together without reserving blank icon slots.
class UnitActionBar : public StaticContainer {
public:
#ifdef DUNECITY_MOBILE
    // Finger-sized: three per row across the 110 px panel. The unit panels give up the
    // unit picture (kPanelTop) so the extra icon row and the order buttons still fit.
    static constexpr int kColumns = 3;
    static constexpr int kIconSize = 36;
    static constexpr int kPanelTop = 22;
#else
    static constexpr int kColumns = 4;
    static constexpr int kIconSize = 26;
    static constexpr int kPanelTop = 80;
#endif
    static constexpr int kPitch = (110 + 2) / kColumns;   // icon plus gap; the last gap is cut off

    UnitActionBar() { enableResizing(true, false); }

    void setButtons(std::initializer_list<Widget*> buttons) {
        for(auto* button : buttons) addWidget(button, Point(0,0), Point(kIconSize,kIconSize));
        refresh();
    }

    Point getMinimumSize() const override {
        int count=0;
        for(const auto& item : containedWidgets) if(item.pWidget->isVisible()) ++count;
        return Point(110, std::max(1,(count+kColumns-1)/kColumns)*kPitch-(kPitch-kIconSize));
    }

    void refresh() {
        unsigned visible=0, mask=0, bit=1;
        for(auto& item : containedWidgets) {
            if(item.pWidget->isVisible()) {
                mask |= bit;
                item.position=Point((visible%kColumns)*kPitch,(visible/kColumns)*kPitch);
                ++visible;
            }
            bit <<= 1;
        }
        if(mask != visibleMask) {
            visibleMask=mask;
            resize(std::max(110,getSize().x),getMinimumSize().y);
            resizeAll();
        }
    }
private:
    unsigned visibleMask=0;
};
#endif
