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

#ifndef CUSTOMGAMEMENU_H
#define CUSTOMGAMEMENU_H

#include <GUI/StaticContainer.h>
#include <GUI/VBox.h>
#include <GUI/HBox.h>
#include <GUI/Label.h>
#include <GUI/TextButton.h>
#include <GUI/ListBox.h>
#include <GUI/DropDownBox.h>
#include <GUI/PictureLabel.h>
#include <GUI/Checkbox.h>

#include <mod/ModInfo.h>
#include <INIMap/MapMetadata.h>
#include <DataTypes.h>

#include <string>
#include <vector>

#include "MenuBase.h"

struct CustomPlaySetup;

class CustomGameMenu : public MenuBase
{
public:
    CustomGameMenu(bool multiplayer, bool LANServer = true, CustomPlaySetup* setup = nullptr);
    virtual ~CustomGameMenu();

    /**
        This method is called, when the child window is about to be closed.
        This child window will be closed after this method returns.
        \param  pChildWindow    The child window that will be closed
    */
    void onChildWindowClose(Window* pChildWindow) override;

private:
    void rebuildMapList();
    /// Collapse copies of the same map found in several map directories.
    void dropDuplicateMapCopies();
    void onNext();
    void onCancel();
    void onLoad();
    void onGameOptions();
    void onMultiplePlayersPerHouseChange();
    void onMapTypeChange(int buttonID);
    void onMapListSelectionChange(bool bInteractive);
    /// Host: generate a Random Skirmish map with open starts, then continue with it as the chosen map.
    void onRandomMap();
    /// Show MP User Maps with every filter cleared and select the map at @a path; false if not listed.
    bool selectUserMap(const std::string& path);

    /// Full path of the selected local or downloaded map; empty until downloaded.
    std::string getSelectedMapPath() const;

    CustomPlaySetup* setup = nullptr;
    HBox connectionRow;
    DropDownBox connectionChoice, visibilityChoice;

    bool bMultiplayer;
    bool bLANServer;

    SettingsClass::GameOptionsClass currentGameOptions;

    StaticContainer windowWidget;
    VBox            mainVBox;

    Label           captionLabel;

    HBox            mainHBox;

    // left VBox with map list and map options
    VBox            leftVBox;
    HBox            mapTypeButtonsHBox;
    TextButton      allMapsButton;          ///< Combined: every map from all 4 directories
    TextButton      singleplayerMapsButton;
    TextButton      singleplayerUserMapsButton;
    TextButton      multiplayerMapsButton;
    TextButton      multiplayerUserMapsButton;
    TextButton      dummyButton;
    HBox            filterRow;
    DropDownBox     mapModFilter, mapSizeFilter, mapPlayersFilter;
    Label           mapLibraryStatus, mapPropertyMod, mapPropertyVersion;
    struct MapEntry {
        std::string path; MapMetadata metadata;
        std::string contentKey;   ///< playable content of a local file; identifies equivalent copies
        bool hasSidecar = false;  ///< a .workshop.ini beside the file pins an exact revision
    };
    std::vector<MapEntry> mapEntries;
    std::vector<size_t> visibleMaps;
    std::vector<std::string> filterMods;
    ListBox         mapList;
    HBox            optionsHBox;
    Checkbox        multiplePlayersPerHouseCheckbox;
    Checkbox        allowJoinAfterStartCheckbox;
    TextButton      gameOptionsButton;

    // right VBox with mini map
    VBox            rightVBox;
    PictureLabel    minimap;
    HBox            mapPropertiesHBox;
    VBox            mapPropertyNamesVBox;
    VBox            mapPropertyValuesVBox;
    Label           mapPropertySize;
    Label           mapPropertyPlayers;
    Label           mapPropertyAuthors;
    Label           mapPropertyLicense;
    
    // Mod selection
    HBox            modHBox;
    Label           modLabel;
    DropDownBox     modDropDown;
    std::vector<ModInfo> availableMods;

    // bottom row of buttons
    HBox            buttonHBox;
    TextButton      nextButton;
    TextButton      loadButton;
    TextButton      randomMapButton;
    TextButton      cancelButton;

};

#endif //CUSTOMGAMEMENU_H
