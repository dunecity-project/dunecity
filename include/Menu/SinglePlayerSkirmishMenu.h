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

#ifndef SINGLEPLAYERSKIRMISHMENU_H
#define SINGLEPLAYERSKIRMISHMENU_H

#include "MenuBase.h"

#include <GUI/StaticContainer.h>
#include <GUI/VBox.h>
#include <GUI/Label.h>
#include <GUI/TextButton.h>
#include <GUI/DropDownBox.h>
#include <GUI/PictureButton.h>
#include <GUI/InvisibleButton.h>
#include <GUI/Spacer.h>
#include <GUI/PictureLabel.h>
#include <GUI/dune/DigitsCounter.h>

#include <misc/string_util.h>
#include <DataTypes.h>
#include <GameInitSettings.h>

class SinglePlayerSkirmishMenu : public MenuBase
{
public:
    explicit SinglePlayerSkirmishMenu(bool campaignCoop = false);
    virtual ~SinglePlayerSkirmishMenu();

    /**
        This method is called, when the child window is about to be closed.
        This child window will be closed after this method returns.
        \param  pChildWindow    The child window that will be closed
    */
    void onChildWindowClose(Window* pChildWindow) override;

private:

    void onStart();
    void onHostCoop();
    void onLoadCoop(bool sharedSave = false);
    void hostCoopGame(GameInitSettings init);
    void onCancel();
    void onGameOptions();

    void onSelectHouseButton(int button);
    void onHouseLeft();
    void onHouseRight();

    void onMissionIncrement();
    void onMissionDecrement();

    void updateHouseChoice();
    void updateSupportBotLabel();
    void onSupportBotSelectionChanged(bool interactive);
    void onEnemyAISelectionChanged(bool interactive);

    InvisibleButton house1Button;
    PictureLabel    house1Picture;
    PictureLabel    house1SelectedPicture;
    InvisibleButton house2Button;
    PictureLabel    house2Picture;
    PictureLabel    house2SelectedPicture;
    InvisibleButton house3Button;
    PictureLabel    house3Picture;
    PictureLabel    house3SelectedPicture;

    PictureButton   houseLeftButton;
    PictureButton   houseRightButton;

    PictureButton   missionPlusButton;
    PictureButton   missionMinusButton;
    DigitsCounter   missionCounter;

    StaticContainer windowWidget;
    StaticContainer houseChoiceContainer;
    VBox            menuButtonsVBox;

    bool campaignCoop = false;
    bool hostingCoop = false;
    TextButton hostCoopButton;
    TextButton loadCoopButton;
    TextButton loadSharedSaveButton;
    DropDownBox coopNetworkDropDown;
    Label skinLabel;            ///< "DuneCity skin": the city-building art every house uses
    DropDownBox skinDropDown;   ///< SimCity or Dune2; enabled only while the DuneCity mod is active
    TextButton      startButton;
    DropDownBox     supportBotDropDown;
    DropDownBox     enemyAIDropDown;
    TextButton      gameOptionsButton;
    TextButton      backButton;

    PictureLabel    heraldPicture;
    PictureLabel    duneLegacy;
    PictureLabel    buttonBorder;

    int currentHouseChoiceScrollPos;
    int selectedButton;
    int mission;
    int supportBotIndex;
    int enemyAIIndex;
    
    SettingsClass::GameOptionsClass currentGameOptions;
};

#endif //SINGLEPLAYERSKIRMISHMENU_H
