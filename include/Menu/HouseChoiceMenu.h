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

#ifndef HOUSECHOICEMENU_H
#define HOUSECHOICEMENU_H

#include "MenuBase.h"
#include <GUI/StaticContainer.h>
#include <GUI/VBox.h>
#include <GUI/Label.h>
#include <GUI/TextButton.h>
#include <GUI/DropDownBox.h>
#include <GUI/dune/AIPlayerSelector.h>
#include <GUI/PictureLabel.h>
#include <GUI/PictureButton.h>
#include <DataTypes.h>
#include <GUI/TextView.h>
#include <mod/ModInfo.h>

class HouseChoiceMenu : public MenuBase {
public:
    explicit HouseChoiceMenu(bool online = true, bool keepRules = false, bool showLobby = false);
    virtual ~HouseChoiceMenu();

    void onChildWindowClose(Window* pChildWindow) override;

    // Static accessors for AI settings (so SinglePlayerMenu can read them)
    static bool isOnline() { return s_online; }
    static bool isSingleMission() { return s_singleMission; }
    static bool isPublicGame() { return s_publicGame; }
    static int getStartLevel() { return s_startLevel; }
    /// The AI partner's player class sharing the human's house; empty for no partner.
    static const std::string& getPartnerClass() { return s_partnerClass; }
    /// The player class of every enemy house.
    static const std::string& getEnemyClass() { return s_enemyClass; }
    static int getCampaignSkin() { return s_campaignSkin; }
    static const SettingsClass::GameOptionsClass& getGameOptions() { return s_currentGameOptions; }

private:
    void updateConnection();
    void populateLevels();
    void onHouseButton(int button);
    void onModSelectionChanged(bool interactive);
    void updateModDescription();
    void updateHouseChoice();

    void onHouseLeft();
    void onHouseRight();
    
    void onGameOptions();
    void onPartnerChanged(bool interactive);
    void onEnemyChanged(bool interactive);

    StaticContainer windowWidget;
    VBox            optionsVBox;

    PictureLabel    selectYourHouseLabel;
    Label titleLabel, selectedHouseLabel, onlineDescription;
    DropDownBox connectionDropDown, journeyDropDown, visibilityDropDown;
    TextButton loadButton;

    PictureButton   house1Button;
    PictureButton   house2Button;
    PictureButton   house3Button;

    PictureButton   houseLeftButton;
    PictureButton   houseRightButton;

    DropDownBox     startLevelDropDown;
    DropDownBox     campaignSkinDropDown;
    DropDownBox     modDropDown;
    TextView        modDescription;
    std::vector<ModInfo> availableMods;
    TextButton      backButton;
    Label          supportDescription;
    Label          enemyDescription;
    AIPlayerSelector partnerSelector;
    AIPlayerSelector enemySelector;
    TextButton hostCoopButton;
    TextButton      gameOptionsButton;

    bool showLobby;
    int currentHouseChoiceScrollPos;

    // Static storage for AI settings
    static int s_house;
    static bool s_online, s_singleMission, s_publicGame;
    static int s_startLevel;
    static std::string s_partnerClass;
    static std::string s_enemyClass;
    static int s_campaignSkin;
    static SettingsClass::GameOptionsClass s_currentGameOptions;
};

#endif // HOUSECHOICEMENU_H
