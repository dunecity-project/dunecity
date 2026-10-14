#ifndef DUNECITY_FORMATION_BAR_H
#define DUNECITY_FORMATION_BAR_H

#include <FormationPlanner.h>
#include <Game.h>
#include <globals.h>
#include <GUI/StaticContainer.h>
#include <GUI/SymbolButton.h>
#include <GUI/ObjectInterfaces/UnitCommandTooltips.h>

#include <algorithm>
#include <array>
#include <vector>

/**
    The unit panel's formation choice (docs/formations.md): Off, Box, Combined arms, Line and
    Column as one row of small buttons, the current choice held down. Shown while two or more
    of the local house's ground units are selected; a click applies to the next move,
    attack-move or patrol order (Game::chooseFormation) and is persisted like Ctrl+F.
*/
class FormationBar : public StaticContainer {
public:
    static constexpr int kChoices = 1 + FormationPlanner::kShapeCount;
    static constexpr int kPitch = 22;

    FormationBar() {
        enableResizing(true, false);
        for(int choice = 0; choice < kChoices; ++choice) {
            SymbolButton& button = buttons[choice];
            button.setSymbol(sdl2::surface_ptr{createIcon(choice)});
            button.setToggleButton(true);
            button.setTooltipText(UnitCommandTooltips::formationChoice(choice));
            button.setOnClick([choice]() {
                currentGame->chooseFormation(choice > 0, choice > 0 ? choice - 1 : settings.general.formationShape);
            });
            addWidget(&button, Point(choice * kPitch, 0), Point(kPitch - 2, kPitch - 2));
        }
    }

    /// The choice in effect: 0 for off, otherwise 1 + the FormationPlanner::Shape.
    static int currentChoice() {
        return settings.general.formationMoves ? 1 + settings.general.formationShape : 0;
    }

    Point getMinimumSize() const override {
        return Point(kChoices * kPitch, shown ? kPitch : 0);
    }

    /// Shows or hides the row and holds down the current choice.
    void refresh(bool show) {
        const int current = currentChoice();
        for(int choice = 0; choice < kChoices; ++choice) buttons[choice].setToggleState(choice == current);
        setVisible(show);
        if(show != shown) {
            shown = show;
            resize(std::max(getMinimumSize().x, getSize().x), getMinimumSize().y);
            resizeAll();
        }
    }

private:
    /// A 20x20 symbol: the formation seen from above, its front at the top.
    static SDL_Surface* createIcon(int choice) {
        static const std::array<std::vector<SDL_Point>, kChoices> dots = {{
            {{3,4},{12,2},{7,9},{15,10},{3,15},{11,15}},                                    // off: scattered
            {{4,4},{9,4},{14,4},{4,9},{9,9},{14,9},{4,14},{9,14},{14,14}},                  // box
            {{2,1},{16,1},{5,7},{9,7},{13,7},{1,9},{17,9},{7,12},{11,12},{9,16}},           // combined arms
            {{0,9},{4,9},{8,9},{12,9},{16,9}},                                              // line
            {{6,1},{11,1},{6,6},{11,6},{6,11},{11,11},{6,16},{11,16}}                       // column
        }};
        SDL_Surface* icon = SDL_CreateRGBSurfaceWithFormat(0, 20, 20, 32, SDL_PIXELFORMAT_RGBA32);
        if(icon == nullptr) return nullptr;
        SDL_FillRect(icon, nullptr, SDL_MapRGBA(icon->format, 0, 0, 0, 0));
        for(const SDL_Point& dot : dots[choice]) {
            const SDL_Rect shadow{dot.x, dot.y, 4, 4};
            SDL_FillRect(icon, &shadow, SDL_MapRGBA(icon->format, 20, 16, 10, 200));
            const SDL_Rect body{dot.x, dot.y, 3, 3};
            // The scouts of the combined-arms symbol are amber, everything else bone white.
            const bool scout = choice == 2 && dot.y == 1;
            SDL_FillRect(icon, &body, scout ? SDL_MapRGBA(icon->format, 255, 196, 64, 255)
                                            : SDL_MapRGBA(icon->format, 250, 248, 240, 255));
        }
        SDL_SetSurfaceBlendMode(icon, SDL_BLENDMODE_BLEND);
        return icon;
    }

    std::array<SymbolButton, kChoices> buttons;
    bool shown = true;
};

#endif
