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

#ifndef GAMEOPTIONSWINDOW_H
#define GAMEOPTIONSWINDOW_H

#include <GUI/Window.h>
#include <GUI/HBox.h>
#include <GUI/VBox.h>
#include <GUI/TextButton.h>
#include <GUI/Label.h>
#include <GUI/Spacer.h>
#include <GUI/Checkbox.h>
#include <GUI/PictureButton.h>
#include <GUI/ProgressBar.h>
#include <GUI/dune/DigitsTextBox.h>

#include <DataTypes.h>

#include <algorithm>

// A wrapped explanation panel for the historical combat rule. The ordinary
// single-line button tooltip cannot fit this explanation on smaller windows.
class DamageRulesCheckbox final : public Checkbox {
public:
    void setExplanation(const std::string& title, const std::string& text);
    void drawOverlay(Point position) override;

private:
    std::string explanationTitle;
    int explanationWidth = 0;
    SDL_Rect explanationBounds{};
};

class GameOptionsWindow : public Window
{
public:
    explicit GameOptionsWindow(SettingsClass::GameOptionsClass& initialGameOptions);
    virtual ~GameOptionsWindow();

    void onOK();

    const SettingsClass::GameOptionsClass& getGameOptions() const { return gameOptions; };

    /**
        This static method creates a dynamic game options window.
        The idea behind this method is to simply create a new dialog on the fly and
        add it as a child window of some other window. If the window gets closed it will be freed.
        \param  initialGameOptions the game options that will be shown on start of the dialog
        \return The new dialog box (will be automatically destroyed when it's closed)
    */
    static GameOptionsWindow* create(SettingsClass::GameOptionsClass& initialGameOptions) {
        GameOptionsWindow* dlg = new GameOptionsWindow(initialGameOptions);
        dlg->pAllocated = true;
        return dlg;
    }

private:
    void onGameSpeedMinus();
    void onGameSpeedPlus();
    void updateGameSpeedBar();

    int currentGameSpeed;
    SettingsClass::GameOptionsClass gameOptions;

    VBox vbox;                                      ///< vertical box
    HBox hbox;                                      ///< horizontal box
    VBox vboxLeft;                                  ///< inner vertical box on the left side
    VBox vboxRight;                                 ///< inner vertical box on the right side
    Label captionlabel;                             ///< label that contains the caption
    Checkbox concreteRequiredCheckbox;              ///< If not checked we can build without penalties on the bare rock
    Checkbox fogOfWarCheckbox;                      ///< If checked explored terrain will become foggy when no unit or structure is next to it
    Checkbox startWithExploredMapCheckbox;          ///< If checked the complete map is unhidden at the beginning of the game
    Checkbox instantBuildCheckbox;                  ///< If checked the building of structures and units does not take any time
    Checkbox onlyOnePalaceCheckbox;                 ///< If checked only one palace can be build per house
    DamageRulesCheckbox originalUnitDamageCheckbox;
    Checkbox rocketTurretsNeedPowerCheckbox;        ///< If checked rocket turrets are dysfunctional on power shortage
    Checkbox sandwormsRespawnCheckbox;              ///< If checked killed sandworms respawn after some time
    Checkbox killedSandwormsDropSpiceCheckbox;      ///< If checked killed sandworms drop some spice
    Checkbox manualCarryallDropsCheckbox;           ///< If checked player can request carryall to transport units
    Checkbox immortalHumanPlayerCheckbox;           ///< If checked human-controlled units and structures are invulnerable (god mode / cheat)
    HBox            gameSpeedHBox;                  ///< The HBox containing the game speed selection
    PictureButton   gameSpeedPlus;                  ///< The button for increasing the game speed
    PictureButton   gameSpeedMinus;                 ///< The button for decreasing the game speed
    TextProgressBar gameSpeedBar;                   ///< The bar showing the current game speed
    HBox            maxUnitsOverrideHBox;           ///< The HBox containing the override option for the maximum number of units
    Checkbox        maxUnitsOverrideCheckbox;       ///< If checked the maximum number of units is set directly, otherwise it is determined by the map
    DigitsTextBox   maxUnitsOverrideTextBox;        ///< The maximum number of units
    HBox            maxHarvestersOverrideHBox;      ///< The HBox containing the override option for the maximum number of harvesters
    Checkbox        maxHarvestersOverrideCheckbox;  ///< If checked the maximum number of harvesters is set directly, otherwise it is determined by map size from ObjectData.ini
    DigitsTextBox   maxHarvestersOverrideTextBox;   ///< The maximum number of harvesters
    HBox            maxConstructionYardsOverrideHBox;      ///< The HBox containing the override option for the maximum number of construction yards
    Checkbox        maxConstructionYardsOverrideCheckbox;  ///< Unchecked or zero means unlimited.
    DigitsTextBox   maxConstructionYardsOverrideTextBox;   ///< The maximum number of construction yards
    Checkbox rememberDefaults;
    TextButton okbutton;                            ///< the ok button
};


#endif // GAMEOPTIONSWINDOW_H
