#ifndef DUNECITY_DISPLAY_MENU_H
#define DUNECITY_DISPLAY_MENU_H

#include <Menu/MenuBase.h>
#include <GUI/StaticContainer.h>
#include <GUI/TextButton.h>
#include <GUI/Label.h>
#include <array>

class DisplayMenu final : public MenuBase {
public:
    DisplayMenu();
private:
    void select(int height);
    void selectAspect(bool widescreen);
    void selectLayout(int mode);
    void apply();
    StaticContainer content;
    Label title;
    Label aspectTitle;
    Label sizeTitle;
    std::array<TextButton, 2> layoutChoices;
    std::array<TextButton, 2> aspectChoices;
    std::array<TextButton, 5> choices;
    TextButton cancelButton;
    TextButton applyButton;
    int selectedHeight;
    bool selectedWidescreen;
    int selectedLayout;
};

#endif
