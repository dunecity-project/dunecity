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

#include <Menu/CustomGameMenu.h>
#include <Menu/CustomGamePlayers.h>
#include <Menu/Dune2RReadinessMenu.h>
#include <Menu/PlaySetup.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/INIFile.h>

#include <GUI/Spacer.h>
#include <GUI/GUIStyle.h>
#include <GUI/dune/GameOptionsWindow.h>
#include <GUI/dune/LoadSaveWindow.h>
#include <GUI/dune/DuneStyle.h>

#include <misc/fnkdat.h>
#include <misc/FileSystem.h>
#include <misc/draw_util.h>
#include <misc/FrameYield.h>
#include <misc/string_util.h>

#include <INIMap/INIMapPreviewCreator.h>
#include <INIMap/MapCatalogue.h>
#include <GameInitSettings.h>
#include <Network/WorkshopGameContent.h>
#include <GUI/MsgBox.h>

#include <globals.h>
#include <main.h>
#include <mod/ModManager.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>


CustomGameMenu::CustomGameMenu(bool multiplayer, bool LANServer, CustomPlaySetup* newSetup)
 : MenuBase(), setup(newSetup), bMultiplayer(multiplayer), bLANServer(LANServer),
   currentGameOptions(newSetup ? newSetup->rules : effectiveGameOptions) {
    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));

    setWindowWidget(&windowWidget);

    windowWidget.addWidget(&mainVBox, Point(24,23), Point(getRendererWidth() - 48, getRendererHeight() - 32));

    captionLabel.setText(setup ? _("Custom Game — Choose Map") : bMultiplayer ? (bLANServer ? _("LAN Game") : _("Internet Game")) : _("Custom Game"));
    captionLabel.setAlignment(Alignment_HCenter);
    mainVBox.addWidget(&captionLabel, 24);
    mainVBox.addWidget(VSpacer::create(6));
    if(setup) {
        connectionChoice.addEntry(_("Offline"));
        connectionChoice.addEntry(_("Online"));
        connectionChoice.setSelectedItem(setup->online ? 1 : 0);
        connectionChoice.setOnSelectionChange([this](bool interactive) {
            const bool online = connectionChoice.getSelectedIndex() == 1;
            // Remember the player's own choice; a programmatic online flow keeps its preference.
            if(interactive) rememberPlayMode(PlayModeScope::CustomGame, online);
            visibilityChoice.setVisible(online);
            visibilityChoice.setEnabled(online);
            allowJoinAfterStartCheckbox.setVisible(online);
            allowJoinAfterStartCheckbox.setEnabled(online && OnlineModPolicy::approved());
        });
        connectionRow.addWidget(&connectionChoice, 130);
        connectionRow.addWidget(HSpacer::create(8));
        visibilityChoice.addEntry(_("Private - invite code"));
        visibilityChoice.addEntry(_("Public - anyone"));
        visibilityChoice.setSelectedItem(setup->publicGame ? 1 : 0);
        visibilityChoice.setVisible(setup->online);
        visibilityChoice.setEnabled(setup->online);
        connectionRow.addWidget(&visibilityChoice, 180);
        connectionRow.addWidget(Spacer::create());
        mainVBox.addWidget(&connectionRow, 28);
        allowJoinAfterStartCheckbox.setText(_("Allow hot join"));
        allowJoinAfterStartCheckbox.setChecked(setup->allowJoinAfterStart && OnlineModPolicy::approved());
        allowJoinAfterStartCheckbox.setVisible(setup->online);
        allowJoinAfterStartCheckbox.setEnabled(setup->online && OnlineModPolicy::approved());
        mainVBox.addWidget(&allowJoinAfterStartCheckbox, 24);
    }

    mainVBox.addWidget(Spacer::create(), 0.05);

    mainVBox.addWidget(&mainHBox, 0.80);

    mainHBox.addWidget(Spacer::create(), 0.05);
    mainHBox.addWidget(&leftVBox, 0.8);

    leftVBox.addWidget(&mapTypeButtonsHBox, 24);

    // "All Maps" combines every .ini file from the four standard map
    // directories (SP install, SP user, MP install, MP user). Default
    // tab in multiplayer so the host doesn't have to hunt for maps;
    // also a useful escape hatch in single-player when the user just
    // wants every map at a glance.
    allMapsButton.setText(_("All Maps"));
    allMapsButton.setToggleButton(true);
    allMapsButton.setOnClick(std::bind(&CustomGameMenu::onMapTypeChange, this, 4));
    mapTypeButtonsHBox.addWidget(&allMapsButton);

    singleplayerMapsButton.setText(_("SP Maps"));
    singleplayerMapsButton.setToggleButton(true);
    singleplayerMapsButton.setOnClick(std::bind(&CustomGameMenu::onMapTypeChange, this, 0));
    mapTypeButtonsHBox.addWidget(&singleplayerMapsButton);

    singleplayerUserMapsButton.setText(_("SP User Maps"));
    singleplayerUserMapsButton.setToggleButton(true);
    singleplayerUserMapsButton.setOnClick(std::bind(&CustomGameMenu::onMapTypeChange, this, 1));
    mapTypeButtonsHBox.addWidget(&singleplayerUserMapsButton);

    multiplayerMapsButton.setText(_("MP Maps"));
    multiplayerMapsButton.setToggleButton(true);
    multiplayerMapsButton.setOnClick(std::bind(&CustomGameMenu::onMapTypeChange, this, 2));
    mapTypeButtonsHBox.addWidget(&multiplayerMapsButton);

    multiplayerUserMapsButton.setText(_("MP User Maps"));
    multiplayerUserMapsButton.setToggleButton(true);
    multiplayerUserMapsButton.setOnClick(std::bind(&CustomGameMenu::onMapTypeChange, this, 3));
    mapTypeButtonsHBox.addWidget(&multiplayerUserMapsButton);

    metaserverMapsButton.setText(_("Metaserver Maps"));
    metaserverMapsButton.setToggleButton(true);
    metaserverMapsButton.setOnClick([this](){onMapTypeChange(5);});
    remoteMapsRow.addWidget(&metaserverMapsButton);
    refreshMapsButton.setText(_("Refresh"));
    refreshMapsButton.setOnClick([this](){onMapTypeChange(mapCategory);});
    remoteMapsRow.addWidget(&refreshMapsButton);
    previewMapButton.setText(_("Preview"));
    previewMapButton.setOnClick([this](){onPreviewMap();});

    leftVBox.addWidget(&remoteMapsRow,24);
    // The category is derived from the map's own buildings, so exactly three exist.
    filterMods={"",MapMetadata::ModVanilla,MapMetadata::ModTornie,MapMetadata::ModDuneCity};
    mapModFilter.addEntry(_("Any type"));
    for(const auto& id:{MapMetadata::ModVanilla,MapMetadata::ModTornie,MapMetadata::ModDuneCity})
        mapModFilter.addEntry(MapMetadata::modLabel(id));
    for(const char* label:{"Any size","Up to 64","65 - 128","129 - 192","193 - 256","Over 256"})mapSizeFilter.addEntry(label);
    mapPlayersFilter.addEntry(_("Any players"));
    for(int i=1;i<=12;++i)mapPlayersFilter.addEntry(std::to_string(i)+" players");
    mapModFilter.setSelectedItem(0);mapSizeFilter.setSelectedItem(0);mapPlayersFilter.setSelectedItem(0);
    for(auto* filter:{&mapModFilter,&mapSizeFilter,&mapPlayersFilter}) {
        filter->setOnSelectionChange([this](bool interactive){if(interactive)rebuildMapList();});
        filterRow.addWidget(filter);
    }
    leftVBox.addWidget(&filterRow,24);
    leftVBox.addWidget(&mapLibraryStatus,18);
    dummyButton.setEnabled(false);
    mapTypeButtonsHBox.addWidget(&dummyButton, 17);
    mapList.setAutohideScrollbar(false);
    mapList.setOnSelectionChange(std::bind(&CustomGameMenu::onMapListSelectionChange, this, std::placeholders::_1));
    mapList.setOnDoubleClick(std::bind(&CustomGameMenu::onNext, this));
    leftVBox.addWidget(&mapList, 0.95);

    leftVBox.addWidget(VSpacer::create(10));

    multiplePlayersPerHouseCheckbox.setText(setup ? _("Shared house") : _("Multiple players per house"));
    multiplePlayersPerHouseCheckbox.setChecked(setup ? setup->sharedHouse : settings.general.multiplePlayersPerHouse);
    multiplePlayersPerHouseCheckbox.setOnClick(std::bind(&CustomGameMenu::onMultiplePlayersPerHouseChange, this));
    optionsHBox.addWidget(&multiplePlayersPerHouseCheckbox);
    optionsHBox.addWidget(Spacer::create());
    gameOptionsButton.setText(setup ? _("Game Rules") : _("Game Options..."));
    gameOptionsButton.setOnClick(std::bind(&CustomGameMenu::onGameOptions, this));
    optionsHBox.addWidget(&gameOptionsButton, 140);

    leftVBox.addWidget(Spacer::create(), 0.05);

    leftVBox.addWidget(&optionsHBox, 0.05);

    mainHBox.addWidget(HSpacer::create(8));
    mainHBox.addWidget(Spacer::create(), 0.05);

    mainHBox.addWidget(&rightVBox, 180);
    mainHBox.addWidget(Spacer::create(), 0.05);
    minimap.setSurface( GUIStyle::getInstance().createButtonSurface(130,130,_("Choose map"), true, false) );
    rightVBox.addWidget(&minimap);
    rightVBox.addWidget(&previewMapButton,24);

    rightVBox.addWidget(VSpacer::create(10));
    rightVBox.addWidget(&mapPropertiesHBox, 0.01);
    mapPropertiesHBox.addWidget(&mapPropertyNamesVBox, 75);
    mapPropertiesHBox.addWidget(&mapPropertyValuesVBox, 105);
    mapPropertyNamesVBox.addWidget(Label::create(_("Size") + ":"));
    mapPropertyValuesVBox.addWidget(&mapPropertySize);
    mapPropertyNamesVBox.addWidget(Label::create(_("Players") + ":"));
    mapPropertyValuesVBox.addWidget(&mapPropertyPlayers);
    mapPropertyNamesVBox.addWidget(Label::create(_("Author") + ":"));
    mapPropertyValuesVBox.addWidget(&mapPropertyAuthors);
    mapPropertyNamesVBox.addWidget(Label::create(_("License") + ":"));
    mapPropertyValuesVBox.addWidget(&mapPropertyLicense);
    mapPropertyNamesVBox.addWidget(Label::create(_("Map type")+":"));
    mapPropertyValuesVBox.addWidget(&mapPropertyMod);
    mapPropertyNamesVBox.addWidget(Label::create(_("Version")+":"));
    mapPropertyValuesVBox.addWidget(&mapPropertyVersion);
    
    rightVBox.addWidget(VSpacer::create(4));
    
    // Mod selection
    rightVBox.addWidget(&modHBox, 25);
    modLabel.setText(_("Mod:"));
    modHBox.addWidget(&modLabel, 40);
    modHBox.addWidget(HSpacer::create(5));
    modHBox.addWidget(&modDropDown, 130);
    
    // Populate mod dropdown
    availableMods = ModManager::instance().listModChoices();
    std::string activeModName = setup && !setup->mods.empty() ? setup->mods[setup->mod].name : ModManager::instance().getActiveModName();
    int activeIndex = 0;
    for (size_t i = 0; i < availableMods.size(); i++) {
        modDropDown.addEntry(availableMods[i].selectionLabel());
        if (availableMods[i].matchesSelectionName(activeModName)) {
            activeIndex = static_cast<int>(i);
        }
    }
    if (!availableMods.empty()) {
        modDropDown.setSelectedItem(activeIndex);
    }
    if(setup) modDropDown.setOnSelectionChange([this](bool interactive) {
        const int choice = modDropDown.getSelectedIndex();
        if(!interactive || choice < 0 || choice >= static_cast<int>(availableMods.size())) return;
        auto& manager = ModManager::instance();
        const auto previous = manager.getActiveModName();
        if(previous == availableMods[choice].name) {
            rememberCustomGameMod(availableMods[choice].name);
            return;
        }
        if(manager.setActiveMod(availableMods[choice].name)) {
            // The player's own mod choice is what the next custom game starts from.
            rememberCustomGameMod(availableMods[choice].name);
            // Choosing the canonical remaster starts its artwork readiness here, so
            // nobody has to visit the Asset Editor first. Cancelling leaves the
            // chooser open; Next checks again before a game is set up.
            const auto gate = ensureDune2RArtworkReady(availableMods[choice].name);
            if(!gate.ready && !gate.message.empty()) openWindow(MsgBox::create(gate.message));
            currentGameOptions = effectiveGameOptions = manager.loadEffectiveGameOptions(settings.gameOptions);
            allowJoinAfterStartCheckbox.setEnabled(connectionChoice.getSelectedIndex() == 1 && OnlineModPolicy::approved());
            if(!OnlineModPolicy::approved()) allowJoinAfterStartCheckbox.setChecked(false);
        } else {
            for(size_t i = 0; i < availableMods.size(); ++i)
                if(availableMods[i].matchesSelectionName(previous)) modDropDown.setSelectedItem(static_cast<int>(i));
        }
    });
    
    rightVBox.addWidget(Spacer::create());

    mainVBox.addWidget(Spacer::create(), 0.05);

    mainVBox.addWidget(VSpacer::create(6));
    mainVBox.addWidget(&buttonHBox, 24);
    mainVBox.addWidget(VSpacer::create(4), 0.0);

    buttonHBox.addWidget(HSpacer::create(70));
    cancelButton.setText(_("Back"));
    cancelButton.setOnClick(std::bind(&CustomGameMenu::onCancel, this));
    buttonHBox.addWidget(&cancelButton, 0.1);

    buttonHBox.addWidget(Spacer::create(), 0.0625);

    buttonHBox.addWidget(Spacer::create(), 0.25);
    loadButton.setText(_("Load"));
    loadButton.setVisible(bMultiplayer && !setup);
    loadButton.setEnabled(bMultiplayer && !setup);
    loadButton.setOnClick(std::bind(&CustomGameMenu::onLoad, this));
    buttonHBox.addWidget(&loadButton, 0.175);
    buttonHBox.addWidget(Spacer::create(), 0.25);

    buttonHBox.addWidget(Spacer::create(), 0.0625);

    nextButton.setText(_("Next"));
    nextButton.setOnClick(std::bind(&CustomGameMenu::onNext, this));
    buttonHBox.addWidget(&nextButton, 0.1);
    buttonHBox.addWidget(HSpacer::create(90));

    // Default tab: "All Maps" in multiplayer (host gets every option),
    // "SP Maps" in single-player (matches the original behaviour).
    onMapTypeChange(setup ? setup->mapCategory : bMultiplayer ? 4 : 0);
    if(setup && !setup->maps.empty()) {
        const auto& selected = setup->maps[setup->map];
        auto restoreSelection = [&]() {
            for(int i = 0; i < mapList.getNumEntries(); ++i) {
                if(i<static_cast<int>(visibleMaps.size()) && strToLower(mapEntries[visibleMaps[i]].path)==strToLower(selected)) {
                    mapList.setSelectedItem(i);
                    return true;
                }
            }
            return false;
        };
        if(!restoreSelection()) { onMapTypeChange(4); restoreSelection(); }
    }
}

CustomGameMenu::~CustomGameMenu()
{
    ;
}


void CustomGameMenu::onChildWindowClose(Window* pChildWindow) {
    LoadSaveWindow* pLoadSaveWindow = dynamic_cast<LoadSaveWindow*>(pChildWindow);
    if(pLoadSaveWindow != nullptr) {
        std::string filename = pLoadSaveWindow->getFilename();

        if(filename != "") {
            std::string savegamedata = readCompleteFile(filename);

            std::string servername = settings.general.playerName + "'s Game";
            GameInitSettings gameInitSettings(getBasename(filename, true), savegamedata, servername);

            int ret;
            try { ret = CustomGamePlayers(gameInitSettings, true, bLANServer).showMenu(); }
            catch(const std::exception& error) { openWindow(MsgBox::create(error.what())); return; }
            if(ret != MENU_QUIT_DEFAULT) {
                quit(ret);
            }
        }
    }

    GameOptionsWindow* pGameOptionsWindow = dynamic_cast<GameOptionsWindow*>(pChildWindow);
    if(pGameOptionsWindow != nullptr) {
        currentGameOptions = pGameOptionsWindow->getGameOptions();
        // Choices made here become the new defaults, the same as in Options.
        // Game Rules only persists defaults when the player requests it.
    }
}

void CustomGameMenu::onMultiplePlayersPerHouseChange() {
    if(setup) return; // Local setup is committed with Players, not as a global preference.
    // Remember the choice across games and restarts.
    settings.general.multiplePlayersPerHouse = multiplePlayersPerHouseCheckbox.isChecked();
    INIFile config(getConfigFilepath());
    config.setBoolValue("General", "Multiple Players Per House", settings.general.multiplePlayersPerHouse);
    if(!config.saveChangesTo(getConfigFilepath())) {
        SDL_Log("Warning: could not save 'Multiple Players Per House' to the configuration file");
    }
}

void CustomGameMenu::onNext()
{
    if(mapList.getSelectedIndex() < 0) {
        return;
    }

    if(!prepareSelectedMap()) return;

    // The canonical Dune2R artwork has to be complete and verified before this game
    // captures a mod version, including when the mod was restored from the profile
    // and the dropdown was never touched. A cancelled or failed download keeps the
    // chooser open instead of starting with partial art.
    {
        const int modChoice = modDropDown.getSelectedIndex();
        if(modChoice >= 0 && modChoice < static_cast<int>(availableMods.size())) {
            const auto gate = ensureDune2RArtworkReady(availableMods[modChoice].name);
            if(!gate.ready) {
                if(!gate.message.empty()) openWindow(MsgBox::create(gate.message));
                return;
            }
        }
    }

    if(setup) {
        auto path = getSelectedMapPath();
        getCaseInsensitiveFilename(path);
        const auto selected = std::find(setup->maps.begin(), setup->maps.end(), path);
        if(selected == setup->maps.end()) return;
        const int mapIndex = static_cast<int>(selected - setup->maps.begin());
        int selectedMod = setup->mod;
        const int choice = modDropDown.getSelectedIndex();
        if(choice >= 0 && choice < static_cast<int>(availableMods.size())) {
            for(size_t i = 0; i < setup->mods.size(); ++i)
                if(setup->mods[i].name == availableMods[choice].name) selectedMod = static_cast<int>(i);
        }
        if(mapIndex != setup->map || selectedMod != setup->mod) setup->players = ChangeEventList{};
        setup->map = mapIndex;
        setup->mod = selectedMod;
        if(selectedMod >= 0 && selectedMod < static_cast<int>(setup->mods.size()))
            rememberCustomGameMod(setup->mods[selectedMod].name);
        setup->online = connectionChoice.getSelectedIndex() == 1;
        setup->publicGame = visibilityChoice.getSelectedIndex() == 1;
        setup->allowJoinAfterStart = allowJoinAfterStartCheckbox.isChecked() && OnlineModPolicy::approved();
        setup->sharedHouse = multiplePlayersPerHouseCheckbox.isChecked();
        setup->rules = currentGameOptions;
        quit(MENU_SETUP_PLAYERS);
        return;
    }

    // Activate selected mod
    int modIndex = modDropDown.getSelectedIndex();
    if (modIndex >= 0 && modIndex < static_cast<int>(availableMods.size())) {
        ModManager::instance().setActiveMod(availableMods[modIndex].name);
        rememberCustomGameMod(ModManager::instance().getActiveModName());
        // Reload effective game options with new mod
        effectiveGameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    }

    std::string mapFilename = getSelectedMapPath();
    getCaseInsensitiveFilename(mapFilename);

    GameInitSettings gameInitSettings;
    if(bMultiplayer) {
        std::string servername = settings.general.playerName + "'s Game";
        gameInitSettings = GameInitSettings(getBasename(mapFilename, true), readCompleteFile(mapFilename), servername, multiplePlayersPerHouseCheckbox.isChecked(), currentGameOptions);
    } else {
        gameInitSettings = GameInitSettings(getBasename(mapFilename, true), readCompleteFile(mapFilename), multiplePlayersPerHouseCheckbox.isChecked(), currentGameOptions);
    }

    try {
        // A downloaded map keeps its authored revision when it belongs to the selected
        // mod; with another mod chosen, these bytes become a revision of that mod.
        WorkshopGameContent::applyMapRevisionForSelectedMod(mapFilename, gameInitSettings);
    } catch(const std::exception& error) { openWindow(MsgBox::create(error.what())); return; }
#ifdef __EMSCRIPTEN__
    // Browser build: the lobby-creation constructor below is a long
    // synchronous block (map parse + widget build + signaling room setup).
    // Let queued input and signaling callbacks run before it starts.
    yieldFrameToBrowser();
#endif

    int ret = CustomGamePlayers(gameInitSettings, true, bLANServer).showMenu();
    if(ret != MENU_QUIT_DEFAULT) {
        quit(ret);
    }
}

void CustomGameMenu::onCancel()
{
    quit();
}

void CustomGameMenu::onLoad()
{
    char tmp[FILENAME_MAX];
    fnkdat("mpsave/", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);
    std::string savepath(tmp);
    openWindow(LoadSaveWindow::create(false, _("Load Game"), savepath, "dls"));
}

void CustomGameMenu::onGameOptions()
{
    openWindow(GameOptionsWindow::create(currentGameOptions));
}

std::string CustomGameMenu::getSelectedMapPath() const {
    const int i=mapList.getSelectedIndex();
    return i>=0 && i<static_cast<int>(visibleMaps.size()) ? mapEntries[visibleMaps[i]].path : "";
}

void CustomGameMenu::onMapTypeChange(int buttonID) {
    mapCategory=buttonID;
    if(setup) setup->mapCategory=buttonID;
    mapClient.cancel();loadingMaps=false;mapEntries.clear();visibleMaps.clear();
    allMapsButton.setToggleState(buttonID==4);
    singleplayerMapsButton.setToggleState(buttonID==0);
    singleplayerUserMapsButton.setToggleState(buttonID==1);
    multiplayerMapsButton.setToggleState(buttonID==2);
    multiplayerUserMapsButton.setToggleState(buttonID==3);
    metaserverMapsButton.setToggleState(buttonID==5);
    previewMapButton.setVisible(buttonID==5);
    if(buttonID==5) {
        loadingMaps=true;mapClient.browseMaps();rebuildMapList();return;
    }
    std::array<std::string,4> dirs;
    dirs[0]=getDuneLegacyDataDir()+"/maps/singleplayer/";
    dirs[2]=getDuneLegacyDataDir()+"/maps/multiplayer/";
    for(int i:{0,2})if(getFileNamesList(dirs[i],"ini",true,FileListOrder_Name_Asc).empty())
        dirs[i]=getDuneLegacyDataDir()+(i==0?"/data/maps/singleplayer/":"/data/maps/multiplayer/");
    char path[FILENAME_MAX];
    if(fnkdat("maps/singleplayer/",path,sizeof(path),FNKDAT_USER|FNKDAT_CREAT)>=0)dirs[1]=path;
    if(fnkdat("maps/multiplayer/",path,sizeof(path),FNKDAT_USER|FNKDAT_CREAT)>=0)dirs[3]=path;
    for(int i=0;i<4;++i) {
        if(buttonID!=4 && buttonID!=i)continue;
        if(dirs[i].empty())continue;
        for(const auto& file:getFileNamesList(dirs[i],"ini",true,FileListOrder_Name_CaseInsensitive_Asc)) {
            if(file.size()>=13 && file.substr(file.size()-13)==".workshop.ini")continue;
            try {
                MapEntry entry;entry.path=dirs[i]+file;
                if(std::filesystem::file_size(entry.path)>1024*1024)continue;
                const auto mapBytes=readCompleteFile(entry.path);
                if(mapBytes.find('\0')!=std::string::npos)continue;
                INIFile ini(entry.path);entry.metadata=MapMetadata::read(ini,file.substr(0,file.size()-4));
                if(entry.metadata.width<=0||entry.metadata.height<=0||entry.metadata.players<=0)continue;
                // Identifies this file's playable content, so the same map
                // found in several directories is listed once.
                entry.contentKey=MapCatalogue::contentKey(mapBytes);
                entry.hasSidecar=std::filesystem::exists(entry.path+".workshop.ini");
                if(entry.hasSidecar) {
                    INIFile meta(entry.path+".workshop.ini");
                    entry.metadata.name=meta.getStringValue("Workshop","Name",entry.metadata.name);
                    entry.metadata.version=meta.getIntValue("Workshop","Version",entry.metadata.version);
                    // The authored mod is recorded as the map's dependency, not as its category.
                    entry.metadata.dependency=MapMetadata::canonicalMod(meta.getStringValue("Workshop","Mod",entry.metadata.dependency));
                }
                mapEntries.push_back(std::move(entry));
            } catch(const std::exception& e){SDL_Log("Map catalogue: %s",e.what());}
#ifdef __EMSCRIPTEN__
            if(mapEntries.size()%32==0)yieldFrameToBrowser();
#endif
        }
    }
    dropDuplicateMapCopies();
    rebuildMapList();
}

void CustomGameMenu::dropDuplicateMapCopies() {
    std::vector<MapCatalogue::Copy> copies;
    copies.reserve(mapEntries.size());
    for(const auto& entry:mapEntries)
        copies.push_back({entry.metadata.name,entry.contentKey,entry.metadata.version,
            entry.metadata.mod+":"+entry.metadata.dependency});
    const auto kept=MapCatalogue::keptCopies(copies);
    if(kept.size()==mapEntries.size())return;
    std::vector<MapEntry> remaining;
    remaining.reserve(kept.size());
    for(const auto& row:kept) {
        remaining.push_back(std::move(mapEntries[row.index]));
        // The surviving path keeps the newest revision the copies reported.
        remaining.back().metadata.version=row.version;
    }
    mapEntries=std::move(remaining);
}

void CustomGameMenu::rebuildMapList() {
    const std::string previous=getSelectedMapPath();
    mapList.clearAllEntries();visibleMaps.clear();
    const int modIndex=mapModFilter.getSelectedIndex();
    const std::string mod=modIndex>=0&&modIndex<static_cast<int>(filterMods.size())?filterMods[modIndex]:"";
    for(size_t i=0;i<mapEntries.size();++i)if(mapEntries[i].metadata.matches(mod,mapSizeFilter.getSelectedIndex(),mapPlayersFilter.getSelectedIndex()))visibleMaps.push_back(i);
    std::sort(visibleMaps.begin(),visibleMaps.end(),[this](size_t a,size_t b){return strToLower(mapEntries[a].metadata.name)<strToLower(mapEntries[b].metadata.name);});
    int selected=0;
    for(size_t i=0;i<visibleMaps.size();++i) {
        const auto& entry=mapEntries[visibleMaps[i]];
        std::string label=entry.metadata.name;
        if(entry.metadata.version>0)label+=" v"+std::to_string(entry.metadata.version);
        mapList.addEntry(label);
        if(!previous.empty()&&entry.path==previous)selected=static_cast<int>(i);
    }
    nextButton.setEnabled(!visibleMaps.empty());previewMapButton.setEnabled(!visibleMaps.empty());
    mapLibraryStatus.setText(loadingMaps?_("Loading metaserver maps..."):std::to_string(visibleMaps.size())+_(" maps"));
    if(!visibleMaps.empty())mapList.setSelectedItem(selected);
    else {
        minimap.setSurface(GUIStyle::getInstance().createButtonSurface(130,130,_("No matching maps"),true,false));
        for(auto* label:{&mapPropertySize,&mapPropertyPlayers,&mapPropertyAuthors,&mapPropertyLicense,&mapPropertyMod,&mapPropertyVersion})label->setText("");
    }
}

void CustomGameMenu::update() {
    if(!loadingMaps)return;
    mapClient.update();
    if(mapClient.status()==Workshop::Client::Status::Failed) {
        loadingMaps=false;mapLibraryStatus.setText(mapClient.message());return;
    }
    if(mapClient.status()!=Workshop::Client::Status::Succeeded)return;
    for(const auto& r:mapClient.items()) {
        // The metaserver derives the category the same way the engine does; the
        // exact mod dependency is obtained from its downloaded manifest.
        MapEntry e;e.revision=r;e.metadata.name=r.name;e.metadata.dependency="";
        e.metadata.mod=MapMetadata::canonicalCategory(r.mapMod);
        e.metadata.width=r.mapWidth;e.metadata.height=r.mapHeight;e.metadata.players=r.mapPlayers;e.metadata.version=r.version;
        mapEntries.push_back(std::move(e));
    }
    const unsigned next=mapClient.nextPage();
    loadingMaps=next!=0&&mapEntries.size()<10000;
    if(loadingMaps)mapClient.browseMaps(next);
    rebuildMapList();
}

bool CustomGameMenu::prepareSelectedMap() {
    const int i=mapList.getSelectedIndex();
    if(i<0||i>=static_cast<int>(visibleMaps.size()))return false;
    auto& entry=mapEntries[visibleMaps[i]];
    try {
        if(entry.path.empty()) {
            if(!Workshop::downloadWithProgress(entry.revision.hash))return false;
            entry.path=Workshop::installMap(Workshop::store().get(entry.revision.hash));
            if(setup && std::find(setup->maps.begin(),setup->maps.end(),entry.path)==setup->maps.end())setup->maps.push_back(entry.path);
        }
        // The mod a map was authored with is recorded for information only. Picking a map
        // never changes the mod the player chose for this game; a new game keeps playing
        // the selected mod and captures its own map revision when the two differ.
        if(!entry.revision.modHash.empty())
            entry.metadata.dependency=MapMetadata::canonicalMod(Workshop::store().get(entry.revision.modHash).base);
        return true;
    } catch(const std::exception& e){openWindow(MsgBox::create(e.what()));return false;}
}

void CustomGameMenu::onPreviewMap() {
    if(prepareSelectedMap())onMapListSelectionChange(true);
}

void CustomGameMenu::onMapListSelectionChange(bool bInteractive)
{
    nextButton.setEnabled(true);

    if(mapList.getSelectedIndex() < 0) {
        return;
    }

    const int selected=mapList.getSelectedIndex();
    if(selected>=static_cast<int>(visibleMaps.size()))return;
    const auto& entry=mapEntries[visibleMaps[selected]];
    mapPropertyMod.setText(MapMetadata::modLabel(entry.metadata.mod));
    mapPropertyVersion.setText(entry.metadata.version?std::to_string(entry.metadata.version):"-");
    if(entry.path.empty()) {
        mapPropertySize.setText(std::to_string(entry.metadata.width)+" x "+std::to_string(entry.metadata.height));
        mapPropertyPlayers.setText(std::to_string(entry.metadata.players));
        mapPropertyAuthors.setText("");mapPropertyLicense.setText("");
        minimap.setSurface(GUIStyle::getInstance().createButtonSurface(130,130,_("Download to preview"),true,false));
        return;
    }
    std::string mapFilename = getSelectedMapPath();
    getCaseInsensitiveFilename(mapFilename);
    INIFile inimap(mapFilename);

#ifdef __EMSCRIPTEN__
    // Browser build: the INI parse above is a long synchronous block inside
    // the selection-change handler; hand the browser a slice before the
    // (also yielding) minimap render so clicks and signaling aren't queued
    // behind the whole parse+render.
    yieldFrameToBrowser();
#endif

    int sizeX = 0;
    int sizeY = 0;

    if(inimap.hasKey("MAP","Seed")) {
        // old map format with seed value
        int mapscale = inimap.getIntValue("BASIC", "MapScale", -1);

        switch(mapscale) {
            case 0: {
                sizeX = 62;
                sizeY = 62;
            } break;

            case 1: {
                sizeX = 32;
                sizeY = 32;
            } break;

            case 2: {
                sizeX = 21;
                sizeY = 21;
            } break;

            default: {
                sizeX = 64;
                sizeY = 64;
            }
        }
    } else {
        // new map format with saved map
        sizeX = inimap.getIntValue("MAP","SizeX", 0);
        sizeY = inimap.getIntValue("MAP","SizeY", 0);
    }

    mapPropertySize.setText(std::to_string(sizeX) + " x " + std::to_string(sizeY));

    sdl2::surface_ptr pMapSurface = nullptr;
    try {
        INIMapPreviewCreator mapPreviewCreator(&inimap);
        pMapSurface = mapPreviewCreator.createMinimapImageOfMap(1, DuneStyle::buttonBorderColor);
    } catch(...) {
        pMapSurface = sdl2::surface_ptr{ GUIStyle::getInstance().createButtonSurface(130, 130, "Error", true, false) };
        loadButton.setEnabled(false);
    }
    minimap.setSurface(std::move(pMapSurface) );

    mapPropertyPlayers.setText(std::to_string(entry.metadata.players));



    std::string authors = inimap.getStringValue("BASIC","Author", "-");
    if(authors.size() > 11) {
        authors = authors.substr(0,9) + "...";
    }
    mapPropertyAuthors.setText(authors);


    mapPropertyLicense.setText(inimap.getStringValue("BASIC","License", "-"));

}
