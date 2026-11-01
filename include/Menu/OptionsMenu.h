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

#ifndef OPTIONSMENU_H
#define OPTIONSMENU_H

#include "MenuBase.h"
#include <GUI/StaticContainer.h>
#include <GUI/HBox.h>
#include <GUI/VBox.h>
#include <GUI/TextButton.h>
#include <GUI/Label.h>
#include <GUI/TextBox.h>
#include <GUI/Checkbox.h>
#include <GUI/DropDownBox.h>
#include <GUI/PictureLabel.h>
#include <DataTypes.h>
#include <misc/SDL2pp.h>

#include <vector>


#define MENU_QUIT_REINITIALIZE  (1)


class OptionsMenu : public MenuBase
{
    friend struct ClassicUISkinProbe;
public:
    OptionsMenu();
    virtual ~OptionsMenu();

private:
    void    showPage(int page);
    void    updatePalettePreview();
    void    onChangeOption(bool bInteractive);
    void    onOptionsOK();
    void    onOptionsCancel();
    void    onGameOptions();
    void    onRestoreDefaults();
    void    onClearDuneCityAssets();

    void    saveConfiguration2File();

    void onChildWindowClose(Window* pChildWindow) override;

    /**
        This method determines all available suitable screen resolutions.
        If the system returns that any resolution is possible a reasonable
        set of resolutions is provided.
    */
    void determineAvailableScreenResolutions();

    std::vector<Coord> availScreenRes;

    std::vector<std::string> availLanguages;

    SettingsClass::GameOptionsClass currentGameOptions;

    StaticContainer windowWidget;

    Label       title;
    VBox        pages[5];
    TextButton  pageButtons[5];
    static int lastPage;
    HBox        interfaceHBox;
    DropDownBox interfaceSizeDropDownBox;
    DropDownBox aspectDropDownBox;
    HBox        scrollHBox;
    HBox        cameraKeysHBox, mouseOrdersHBox, movementPathsHBox, formationMovesHBox;
    Checkbox    wasdCameraCheckbox, leftClickOrdersCheckbox, movementPathsCheckbox, formationMovesCheckbox;
    DropDownBox scrollSpeedDropDownBox;
    HBox        introHBox;
    HBox        diagnosticLogsHBox;
    Checkbox    diagnosticLogsCheckbox, aiDecisionLogCheckbox;
    HBox        flagsHBox;

    HBox        NameHBox;
    TextBox     nameTextBox;
    HBox        gameOptionsHBox;
    TextButton  gameOptionsButton;

    HBox        languageHBox;
    DropDownBox languageDropDownBox;
    HBox        generalHBox;
    DropDownBox aiDropDownBox;
    Checkbox    introCheckbox;
    HBox        duneCitySkinHBox;
    DropDownBox duneCityCampaignSkinDropDownBox;

    HBox        resolutionHBox;
    DropDownBox resolutionDropDownBox;
    DropDownBox zoomlevelDropDownBox;
    DropDownBox scalerDropDownBox;
    HBox        videoHBox;
    Checkbox    fullScreenCheckbox;
    Checkbox    frameLimitCheckbox;
    Checkbox    showTutorialHintsCheckbox;
    HBox        videoHBox2;
    Checkbox    showWatermarkCheckbox;
    HBox        harvesterCountHBox;
    Checkbox    showHarvesterCountCheckbox;
    DropDownBox cursorVisibilityDropDownBox;
    DropDownBox cursorScaleDropDownBox;
    HBox        paletteHBox;
    DropDownBox startMenuModeDropDownBox;
    DropDownBox paletteDropDownBox;
    HBox        uiSkinHBox;
    DropDownBox uiSkinDropDownBox;
    PictureLabel palettePreview;

    HBox        audioHBox;
    Checkbox    playSFXCheckbox;
    Checkbox    playMusicCheckbox;
    HBox        audioHBox2;
    Checkbox    playCreditsSFXCheckbox;
    HBox        soundtrackHBox;
    Checkbox    alternativeSoundtrackCheckbox;

    HBox        networkPortHBox;
    TextBox     portTextBox;
    HBox        networkMetaServerHBox;
    TextBox     metaServerTextBox;

    HBox        restoreDefaultsHBox;
    TextButton  restoreDefaultsButton;
    HBox        clearDuneCityAssetsHBox;
    TextButton  clearDuneCityAssetsButton;

    TextButton  acceptButton;
    TextButton  backButton;
};


#endif // OPTIONSMENU_H
