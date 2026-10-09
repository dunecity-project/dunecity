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

#include <Menu/CustomGamePlayers.h>

#include <config.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/INIFile.h>

#include <Network/NetworkManager.h>

#include <GUI/Spacer.h>
#include <GUI/GUIStyle.h>
#include <GUI/MsgBox.h>
#include <GUI/dune/GameOptionsWindow.h>
#include <GUI/dune/DuneStyle.h>

#include <players/PlayerFactory.h>
#include <players/QuantBotConfig.h>
#include <mod/ModManager.h>
#include <Network/WorkshopGameContent.h>

#include <misc/fnkdat.h>
#include <misc/FileSystem.h>
#include <misc/WebRuntime.h>
#include <misc/draw_util.h>
#include <misc/FrameYield.h>
#include <misc/string_util.h>
#include <misc/IMemoryStream.h>

#include <INIMap/INIMapPreviewCreator.h>
#include <INIMap/MapPlayerSectionUtils.h>

#include <misc/DiscordManager.h>

#include <sand.h>
#include <globals.h>

#include <algorithm>


#define PLAYER_HUMAN        0
#define PLAYER_OPEN         -1
#define PLAYER_CLOSED       -2

namespace {

int getCustomGameHouseCount() {
    return getNumCustomGameHouses();
}

bool isBonusColorSlot(int colorSlot) {
    switch(colorSlot) {
        case HOUSECOLOR_CUSTOM_TEAL:
        case HOUSECOLOR_CUSTOM_FUCHSIA:
        case HOUSECOLOR_CUSTOM_LIGHT_PINK:
        case HOUSECOLOR_CUSTOM_APPLE_GREEN:
        case HOUSECOLOR_CUSTOM_DARK_VIOLET:
        case HOUSECOLOR_CUSTOM_BRIGHT_YELLOW:
            return true;
        default:
            return false;
    }
}

Uint32 getMenuColorForHouse(int house) {
    if(isValidHouseColorSlot(house)) {
        return getHouseColorRGB(house, 3);
    }

    return COLOR_RGB(20, 20, 40);
}

const char* getCustomColorName(int colorSlot) {
    const bool tornieActive = ModManager::instance().isTornieContentActive();
    switch(colorSlot) {
        case HOUSECOLOR_CUSTOM_DARK_VIOLET: return "Dark Violet";
        case HOUSECOLOR_CUSTOM_FUCHSIA:     return "Fuchsia";
        case HOUSECOLOR_CUSTOM_TEAL:        return "Teal";
        case HOUSECOLOR_CUSTOM_APPLE_GREEN: return tornieActive ? "Dark Grey" : "Dark Green";
        case HOUSECOLOR_CUSTOM_LIGHT_PINK:  return tornieActive ? "Pink" : "Light Pink";
        case HOUSECOLOR_CUSTOM_BRIGHT_YELLOW: return tornieActive ? "Brown" : "Bright Yellow";
        default:                            return "Custom";
    }
}

void addColorDropDownEntries(DropDownBox& colorDropDown, int selectedColor, bool bonusColors) {
    colorDropDown.clearAllEntries();
    colorDropDown.addEntry(_("Original"), HOUSE_INVALID);

    if(bonusColors) {
        if(ModManager::instance().isTornieContentActive()) {
            constexpr int tornieBonusColorSlots[] = {
                HOUSECOLOR_CUSTOM_TEAL,
                HOUSECOLOR_CUSTOM_FUCHSIA,
                HOUSECOLOR_CUSTOM_LIGHT_PINK,
                HOUSECOLOR_CUSTOM_APPLE_GREEN,
                HOUSECOLOR_CUSTOM_DARK_VIOLET,
                HOUSECOLOR_CUSTOM_BRIGHT_YELLOW
            };
            for(const int colorSlot : tornieBonusColorSlots) {
                colorDropDown.addEntry(getCustomColorName(colorSlot), colorSlot);
            }
        } else {
            for(int colorSlot = HOUSECOLOR_CUSTOM_DARK_VIOLET;
                colorSlot < HOUSECOLOR_GUEST_1; ++colorSlot) {
                colorDropDown.addEntry(getCustomColorName(colorSlot), colorSlot);
            }
        }
    } else {
        for(int h = 0; h < getCustomGameHouseCount(); ++h) {
            colorDropDown.addEntry(
                getHouseNameByNumber(static_cast<HOUSETYPE>(h)),
                getDefaultHouseColorSlot(static_cast<HOUSETYPE>(h)));
        }
    }

    for(int i = 0; i < colorDropDown.getNumEntries(); i++) {
        if(colorDropDown.getEntryIntData(i) == selectedColor) {
            colorDropDown.setSelectedItem(i);
            return;
        }
    }

    colorDropDown.setSelectedItem(0);
}

int resolveSelectedColorSlot(int selectedColor, int selectedHouse) {
    if(!isValidHouseColorSlot(selectedColor)
       && selectedHouse >= 0
       && selectedHouse < NUM_HOUSES
       && isCustomGameHouseAvailable(static_cast<HOUSETYPE>(selectedHouse))) {
        selectedColor = getDefaultHouseColorSlot(static_cast<HOUSETYPE>(selectedHouse));
    }

    if(isValidHouseColorSlot(selectedColor)) {
        return selectedColor;
    }

    return HOUSE_INVALID;
}
}


CustomGamePlayers::CustomGamePlayers(const GameInitSettings& newGameInitSettings, bool server, bool LANServer, CustomPlaySetup* newSetup, const ChangeEventList* initialPlayers, bool startImmediately)
 : MenuBase(), gameInitSettings(newGameInitSettings), bServer(server), bLANServer(LANServer),
   duneCitySkinControls(ModManager::instance().isCityModeActive()), startGameTime(0),
   bConfigMismatchDetected(false), bModDownloadInProgress(false), bWaitingForModAcks(false), brainEqHumanSlot(-1) {
    setup = newSetup;
    // Only the main Campaign route skips the roster. Keep the normal content,
    // readiness and acknowledged-start handshake for its online session.
    this->startImmediately = startImmediately && server && !newSetup
        && isCoopGameType(newGameInitSettings.getGameType());
    automaticStartPending = this->startImmediately;
    if(isNetworkGameType(gameInitSettings.getGameType())) {
        bool publishBeforeSetup = false;
#ifdef __EMSCRIPTEN__
        // Find Match uses the mesh packet protocol over browser data channels.
        // Its map is chosen after pairing, so publish before sending game setup.
        publishBeforeSetup = pNetworkManager && !pNetworkManager->isRoomSession();
#endif
        if(bServer) WorkshopGameContent::pin(gameInitSettings, publishBeforeSetup,
            !bLANServer);
        else if(pNetworkManager && pNetworkManager->supportsModTransfer()) {
            try { WorkshopGameContent::resolveMod(gameInitSettings, false); }
            catch(const std::exception&) {
                // The following MOD_INFO packet starts a bounded host transfer. Until
                // its complete manifest verifies, no readiness acknowledgment is sent.
                bModDownloadInProgress = true;
                waitingForModInstall = true;
            }
        } else WorkshopGameContent::resolveMod(gameInitSettings);
        duneCitySkinControls = ModManager::instance().isCityModeActive();
    }
    installerApprovedForDisplay = OnlineModPolicy::approved();
    // Watching is an offline-only choice. Clearing it here (rather than only hiding the
    // checkbox) means an Offline setup carried into Online can never leave a network host
    // believing it is a local observer.
    if(setup && setup->online) setup->spectate = false;
    spectating = setup && setup->spectate && !setup->online
        && !isCoopGameType(gameInitSettings.getGameType());
    const bool compactPlayers = getRendererWidth() < 800;

    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));

    setWindowWidget(&windowWidget);

    windowWidget.addWidget(&mainVBox, Point(24,23), Point(getRendererWidth() - 48, getRendererHeight() - 32));

    captionLabel.setText(getBasename(gameInitSettings.getFilename(), true)
        + (gameInitSettings.getMapRevisionVersion() ? " v" + std::to_string(gameInitSettings.getMapRevisionVersion()) : ""));
    captionLabel.setAlignment(Alignment_HCenter);
    captionHBox.addWidget(&captionLabel, 1.0);
    copyCodeButton.setText(_("Copy code"));
    copyCodeButton.setVisible(false);
    copyCodeButton.setEnabled(false);
    captionHBox.addWidget(&copyCodeButton, 130);
    mainVBox.addWidget(&captionHBox, 24);
    mainVBox.addWidget(VSpacer::create(8));
    if(setup) {
        captionLabel.setText(_("Custom Game"));
        setupMapRow.addWidget(Label::create(_("Map")), 40);
        for(size_t i = 0; i < setup->maps.size(); ++i)
            setupMap.addEntry(getBasename(setup->maps[i], true), static_cast<int>(i));
        setupMap.setSelectedItem(setup->map);
        setupMap.setOnSelectionChange([this](bool interactive) { if(interactive) { setup->map = setupMap.getSelectedIndex(); rebuildSetup(false); } });
        setupMapRow.addWidget(&setupMap, 1.0);
        setupBrowseMaps.setText(_("Browse Maps"));
        setupBrowseMaps.setOnClick([this]() { onCancel(); });
        setupMapRow.addWidget(&setupBrowseMaps, 110);
        setupMapRow.addWidget(Label::create(_("Mod")), 40);
        for(size_t i = 0; i < setup->mods.size(); ++i) setupMod.addEntry(setup->mods[i].selectionLabel(), static_cast<int>(i));
        // A new custom game plays the mod the player picks, including for a downloaded map.
        setupMod.setSelectedItem(setup->mod);
        setupMod.setOnSelectionChange([this](bool interactive) {
            if(!interactive) return;
            setup->mod = setupMod.getSelectedIndex();
            if(setup->mod >= 0 && setup->mod < static_cast<int>(setup->mods.size()))
                rememberCustomGameMod(setup->mods[setup->mod].name);
            rebuildSetup(false);
        });
        setupMapRow.addWidget(&setupMod, 155);
        mainVBox.addWidget(&setupMapRow, 28);
        setupConnection.addEntry(_("Offline"), 0);
        setupConnection.addEntry(_("Online"), 1);
        setupConnection.setSelectedItem(setup->online ? 1 : 0);
        setupConnection.setOnSelectionChange([this](bool interactive) { if(interactive) { setup->online = setupConnection.getSelectedIndex() == 1; rememberPlayMode(PlayModeScope::CustomGame, setup->online); rebuildSetup(true); } });
        setupModeRow.addWidget(&setupConnection, 120);
        setupVisibility.addEntry(_("Private - invite code"), 0);
        setupVisibility.addEntry(_("Public - anyone"), 1);
        setupVisibility.setSelectedItem(setup->publicGame ? 1 : 0);
        setupVisibility.setEnabled(setup->online);
        setupVisibility.setOnSelectionChange([this](bool) { setup->publicGame = setupVisibility.getSelectedIndex() == 1; });
        if(setup->online) setupModeRow.addWidget(&setupVisibility, 180);
        setupShared.setText(_("Shared house"));
        setupShared.setChecked(setup->sharedHouse);
        setupShared.setEnabled(!spectating);
        setupShared.setOnClick([this]() { setup->sharedHouse = setupShared.isChecked(); rebuildSetup(true); });
        setupModeRow.addWidget(&setupShared, 1.0);
        // Offline only. Online keeps the row free of a control that must never apply there.
        if(!setup->online) {
            setupSpectate.setText(_("Spectate"));
            setupSpectate.setChecked(spectating);
            setupSpectate.setTooltipText(_("Watch an AI-vs-AI match. Every faction is played by an AI; "
                                           "you get the whole map and can inspect any building, but give no orders."));
            setupSpectate.setOnClick([this]() {
                using Event = ChangeEventList::ChangeEvent;
                const bool watch = setupSpectate.isChecked();
                setup->spectate = watch;
                // Carry the configured AI rows across the rebuild; only the human seat changes
                // hands. Turning watching on hands that seat to the AI fallback, turning it off
                // drops the row-0 seat events so the rebuilt row falls back to its human default.
                const int fallbackAI = PlayerFactory::getIndexByPlayerClass(DEFAULTAIPLAYERCLASS);
                ChangeEventList kept = getChangeEventList();
                for(auto it = kept.changeEventList.begin(); it != kept.changeEventList.end();) {
                    const bool humanSeatRow = it->slot < 2;
                    if(watch && it->eventType == Event::EventType::SetHumanPlayer) {
                        if(fallbackAI < 0) { it = kept.changeEventList.erase(it); continue; }
                        *it = Event(Event::EventType::ChangePlayer, it->slot,
                                    static_cast<Uint32>(fallbackAI));
                    } else if(!watch && humanSeatRow
                              && it->eventType == Event::EventType::ChangePlayer) {
                        it = kept.changeEventList.erase(it);
                        continue;
                    }
                    ++it;
                }
                setup->players = kept;
                quit(MENU_SETUP_CHANGED);
            });
            setupModeRow.addWidget(&setupSpectate, 1.0);
        } else {
            // Online: the control is neither laid out nor offered. Marked invisible as well as
            // left out of the row, so nothing can report it as an available choice.
            setupSpectate.setVisible(false);
        }
        setupRules.setText(_("Game Rules"));
        setupRules.setOnClick([this]() { openWindow(GameOptionsWindow::create(setup->rules)); });
        setupModeRow.addWidget(&setupRules, 110);
        mainVBox.addWidget(VSpacer::create(6));
        mainVBox.addWidget(&setupModeRow, 28);
    }
    readinessLabel.setTextFontSize(12);
    if(!isCoopGameType(gameInitSettings.getGameType()))
        readinessLabel.setText(_("Spice income: multiplies credits from harvester deliveries. 2x = double."));
    mainVBox.addWidget(&readinessLabel, 22);

    mainVBox.addWidget(Spacer::create(), 0.04);

    mainVBox.addWidget(&mainHBox, 0.6);

    // Keep the classic centered player roster beside the map preview. The
    // outer spacers absorb wide-screen space instead of stretching the rows.
    mainHBox.addWidget(Spacer::create(), 0.05);
    leftVBox.addWidget(Spacer::create(), 0.1);
    leftVBox.addWidget(&playerListHBox);
    playerListHBox.addWidget(Spacer::create(), 0.4);
    playerListHBox.addWidget(&playerListVBox, 0.2);
    playerListHBox.addWidget(Spacer::create(), 0.4);
    mainHBox.addWidget(&leftVBox, 0.8);
    mainHBox.addWidget(Spacer::create(), 0.05);
    mainHBox.addWidget(HSpacer::create(8));
    mainHBox.addWidget(&rightVBox, 180);
    mainHBox.addWidget(Spacer::create(), 0.05);
    minimap.setSurface( GUIStyle::getInstance().createButtonSurface(130,130,_("Choose map"), true, false) );
    rightVBox.addWidget(&minimap);

    if(isCoopGameType(gameInitSettings.getGameType())) {
        fixedCoopHouses = gameInitSettings.getHouseInfoList();
        if(gameInitSettings.getGameType() != GameType::LoadCoop) {
            auto rw = sdl2::RWops_ptr{SDL_RWFromConstMem(gameInitSettings.getFiledata().data(), gameInitSettings.getFiledata().size())};
            INIFile map(rw.get());
            extractMapInfo(&map);
        } else {
            minimap.setSurface(GUIStyle::getInstance().createButtonSurface(130, 130, _("Campaign save"), true, false));
        }
        numHouses = 1;
        slotToTeam[0] = 0;
        boundHousesOnMap.clear();
        boundHousesOnMap.push_back(gameInitSettings.getHouseID());
        brainEqHumanSlot = 0;
        mapPropertyPlayers.setText("2 co-op");
        captionLabel.setText(gameInitSettings.getServername().empty()
            ? _("Campaign co-op") : gameInitSettings.getServername());
    } else if(gameInitSettings.getGameType() == GameType::CustomGame || gameInitSettings.getGameType() == GameType::CustomMultiplayer) {
        auto RWops = sdl2::RWops_ptr{ SDL_RWFromConstMem(gameInitSettings.getFiledata().c_str(), gameInitSettings.getFiledata().size()) };

        INIFile inimap(RWops.get());
        extractMapInfo(&inimap);
    } else if(gameInitSettings.getGameType() == GameType::LoadMultiplayer) {
        IMemoryStream memStream(gameInitSettings.getFiledata().c_str(), gameInitSettings.getFiledata().size());

        GameInitSettings tmpGameInitSettings = GameInitSettings::readSaveSetup(memStream, houseInfoListSetup);

        auto RWops = sdl2::RWops_ptr{ SDL_RWFromConstMem(tmpGameInitSettings.getFiledata().c_str(), tmpGameInitSettings.getFiledata().size()) };

        INIFile inimap(RWops.get());
        extractMapInfo(&inimap);

        // adjust multiple players per house as this was not known before actually loading the saved game
        gameInitSettings.setMultiplePlayersPerHouse(tmpGameInitSettings.isMultiplePlayersPerHouse());

        // adjust numHouses to the actually used houses (which might be smaller than the houses on the map)
        numHouses = houseInfoListSetup.size();
    } else {
        INIFile inimap(gameInitSettings.getFilename());
        extractMapInfo(&inimap);
    }

#ifdef __EMSCRIPTEN__
    // Browser build: extractMapInfo() above (full map INI parse + minimap
    // render) ran as one synchronous block inside the Next click handler.
    // Yield before building the lobby widgets so the page can service input
    // and signaling between the two halves of the transition.
    yieldFrameToBrowser();
#endif

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
    mapPropertyNamesVBox.addWidget(Label::create(_("Mod") + ":"));
    ModInfo activeModInfo = ModManager::instance().getModInfo(ModManager::instance().getActiveModName());
    mapPropertyMod.setText(activeModInfo.displayName + (gameInitSettings.getModRevisionVersion() ? " v" + std::to_string(gameInitSettings.getModRevisionVersion()) : ""));
    mapPropertyCity.setText(activeModInfo.enablesCityMode ? _("On") : _("Off"));
    mapPropertyValuesVBox.addWidget(&mapPropertyMod);
#ifdef __EMSCRIPTEN__
    // Browser: the matchmaking lobby already paired us; show how the matched
    // opponent's connection is doing.
    mapPropertyNamesVBox.addWidget(Label::create(_("Opponent") + ":"));
    mapPropertyValuesVBox.addWidget(&opponentLabel);
#endif
    mapPropertyNamesVBox.addWidget(Label::create(_("City sim") + ":"));
    mapPropertyValuesVBox.addWidget(&mapPropertyCity);
    rightVBox.addWidget(Spacer::create());

    mainVBox.addWidget(Spacer::create(), 0.04);

    mainVBox.addWidget(&buttonHBox, 0.32);

    buttonHBox.addWidget(HSpacer::create(getRendererWidth() >= 1000 ? 70 : 8));

    backButtonVBox.addWidget(Spacer::create());
    backButton.setText(_("Back"));
    backButton.setOnClick(std::bind(&CustomGamePlayers::onCancel, this));
    backButtonVBox.addWidget(&backButton, 24);
    backButtonVBox.addWidget(VSpacer::create(14));
    buttonHBox.addWidget(&backButtonVBox, 110);

    buttonHBox.addWidget(HSpacer::create(10));

    chatTextView.setTextFontSize(12);
    chatVBox.addWidget(&chatTextView, 0.77);
    if(getRendererHeight() <= 600) {
        chatTextBox.setTextFontSize(12);
    }
    chatTextBox.setOnReturn(std::bind(&CustomGamePlayers::onSendChatMessage, this));
    chatVBox.addWidget(&chatTextBox, 0.2);
    chatVBox.addWidget(Spacer::create(), 0.03);
    buttonHBox.addWidget(&chatVBox, 1.0);

    if(!isNetworkGameType(gameInitSettings.getGameType())) {
        chatVBox.setVisible(false);
        chatVBox.setEnabled(false);
    }

    bool bLoadMultiplayer = (gameInitSettings.getGameType() == GameType::LoadMultiplayer);

    buttonHBox.addWidget(HSpacer::create(10));

    nextButtonVBox.addWidget(Spacer::create());
    nextButton.setText(setup && setup->online ? _("Create Lobby") : isCoopGameType(gameInitSettings.getGameType()) ? _("Start Campaign") : _("Start Game"));
    nextButton.setOnClick(std::bind(&CustomGamePlayers::onNext, this));
    nextButtonVBox.addWidget(&nextButton, 24);
    nextButtonVBox.addWidget(VSpacer::create(14));
    if(bServer == false) {
        nextButton.setEnabled(false);
        nextButton.setVisible(false);
    }
    buttonHBox.addWidget(&nextButtonVBox, 130);

    buttonHBox.addWidget(HSpacer::create(getRendererWidth() >= 1000 ? 90 : 8));

    std::list<HOUSETYPE>  tmpBoundHousesOnMap = boundHousesOnMap;

    // Watching means no faction controller belongs to this machine, so the human is treated as
    // already seated: both the player 1 and player 2 branches below then build the ordinary
    // open/closed/AI list for *every* row instead of handing the first row to the local player.
    // No house enum, faction or start position is consumed by the observer.
    bool thisPlayerPlaced = spectating;

    // How wide a house row may be.
    //
    // Every row is one HBox of fixed-width columns, so its minimum width is their sum
    // (HBox::getMinimumSize). mainHBox puts the row inside leftVBox and then adds HSpacer(8)
    // and the 180px map preview, and windowWidget insets mainVBox by 48px - so this is what is
    // left for the row itself: 404px at 640x480, 618px at 854x480, 1044px at 1280x720.
    //
    // DuneCity rows include a graphics skin selector. At narrow resolutions, omit redundant
    // captions before tightening the controls: each drop-down already names its selection.
    // House colour overrides remain in saved/network setup data, but have no menu column.
    const int rowBudget = getRendererWidth() - 48 - 8 - 180;

    struct RowWidths {
        int houseLabel = 60, houseBox = 95, teamBox = 85;
        int spiceLabel = 36, spiceBox = 56;
        int skinLabel = 30, skinBox = 72;
        int gap = 10, subGap = 6;
    } row;
    row.spiceLabel = GUIStyle::getInstance().getMinimumLabelSize(_("Spice"), 12).x;

    const auto rowWidth = [&] {
        int total = row.houseLabel + row.houseBox + row.gap + row.teamBox
                  + row.subGap + row.spiceLabel + row.spiceBox;
        if(duneCitySkinControls)        total += row.subGap + row.skinLabel + row.skinBox;
        return total;
    };

    if(rowWidth() > rowBudget) row.skinLabel = 0;
    if(rowWidth() > rowBudget) row.spiceLabel = 0;
    if(rowWidth() > rowBudget) {
        row.gap = 5;
        row.houseBox = 85; row.teamBox = 70; row.spiceBox = 46; row.skinBox = 62;
    }
    if(rowWidth() > rowBudget) row.houseLabel = 0;
    if(rowWidth() > rowBudget) {
        row.gap = 4; row.subGap = 4;
        row.houseBox = 72; row.teamBox = 58; row.spiceBox = 40; row.skinBox = 56;
    }

    for(int i=0;i<numHouses;i++) {
        HouseInfo& curHouseInfo = houseInfo[i];

        // set up header row with Label "House", DropDown for house selection and DropDown for team selection
        curHouseInfo.houseLabel.setText(_("House"));
        if(row.houseLabel > 0) {
            curHouseInfo.houseHBox.addWidget(&curHouseInfo.houseLabel, row.houseLabel);
        } else {
            curHouseInfo.houseLabel.setVisible(false);
        }

        if(bLoadMultiplayer) {
            if(i < (int) houseInfoListSetup.size()) {
                GameInitSettings::HouseInfo gisHouseInfo = houseInfoListSetup.at(i);

                if(gisHouseInfo.houseID == HOUSE_INVALID) {
                    addToHouseDropDown(curHouseInfo.houseDropDown, HOUSE_INVALID, true);
                } else {
                    addToHouseDropDown(curHouseInfo.houseDropDown, gisHouseInfo.houseID, true);
                }
            }

            curHouseInfo.houseDropDown.setEnabled(false);
        } else if(tmpBoundHousesOnMap.empty() == false) {
            HOUSETYPE housetype = tmpBoundHousesOnMap.front();
            tmpBoundHousesOnMap.pop_front();

            addToHouseDropDown(curHouseInfo.houseDropDown, housetype, true);
            curHouseInfo.houseDropDown.setEnabled(bServer);
        } else {
            addToHouseDropDown(curHouseInfo.houseDropDown, HOUSE_INVALID, true);
            curHouseInfo.houseDropDown.setEnabled(bServer);
        }
        curHouseInfo.houseDropDown.setOnSelectionChange(std::bind(&CustomGamePlayers::onChangeHousesDropDownBoxes, this, std::placeholders::_1, i));
        curHouseInfo.houseHBox.addWidget(&curHouseInfo.houseDropDown, row.houseBox);

        if(bLoadMultiplayer) {
            if(i < (int) houseInfoListSetup.size()) {
                GameInitSettings::HouseInfo gisHouseInfo = houseInfoListSetup.at(i);

                curHouseInfo.teamDropDown.addEntry(_("Team") + " " + std::to_string(gisHouseInfo.team), gisHouseInfo.team);
                curHouseInfo.teamDropDown.setSelectedItem(0);
            }
            curHouseInfo.teamDropDown.setEnabled(false);
            curHouseInfo.teamDropDown.setOnClickEnabled(false);
        } else {
            for(int team = 0 ; team < MAX_CUSTOM_GAME_PLAYERS ; team++) {
                curHouseInfo.teamDropDown.addEntry(_("Team") + " " + std::to_string(team+1), team+1);
            }
            curHouseInfo.teamDropDown.setSelectedItem(slotToTeam[i]);
            curHouseInfo.teamDropDown.setEnabled(bServer);
        }
        curHouseInfo.teamDropDown.setOnSelectionChange(std::bind(&CustomGamePlayers::onChangeTeamDropDownBoxes, this, std::placeholders::_1, i));
        curHouseInfo.houseHBox.addWidget(HSpacer::create(row.gap));
        curHouseInfo.houseHBox.addWidget(&curHouseInfo.teamDropDown, row.teamBox);

        // Spice income. Every row has one, in every mod, for a human or a bot alike, and the
        // two controllers of a shared house share it because they share the house economy.
        //
        // Only the host of a fresh custom game may change it: it decides how much each house
        // earns, so it is a match rule rather than a seat preference (LobbyAuthorization
        // refuses ChangeSpiceIncome from a client even for its own row). A load-game lobby
        // shows what the saved match was played at, read-only.
        {
            Uint32 selectedFactor = SpiceIncome::kDefault;
            if(bLoadMultiplayer && i < static_cast<int>(houseInfoListSetup.size())) {
                selectedFactor = houseInfoListSetup.at(i).spiceIncomeMultiplier;
            } else if(isCoopGameType(gameInitSettings.getGameType())) {
                for(const auto& fixed : fixedCoopHouses) {
                    if(fixed.houseID == gameInitSettings.getHouseID()) {
                        selectedFactor = fixed.spiceIncomeMultiplier;
                        break;
                    }
                }
            }
            curHouseInfo.spiceIncomeLabel.setText(_("Spice"));
            curHouseInfo.spiceIncomeLabel.setTextFontSize(12);
            curHouseInfo.spiceIncomeLabel.setAlignment(static_cast<Alignment_Enum>(Alignment_Left | Alignment_VCenter));
            for(Uint32 factor = SpiceIncome::kMin; factor <= SpiceIncome::kMax; ++factor) {
                curHouseInfo.spiceIncomeDropDown.addEntry(
                    std::to_string(factor) + "x", static_cast<int>(factor));
            }
            curHouseInfo.spiceIncomeDropDown.setSelectedItem(
                static_cast<int>(SpiceIncome::isValid(selectedFactor)
                    ? selectedFactor - SpiceIncome::kMin : 0));
            // The entries read "1x", so say what is multiplied. Nothing else scales: not the
            // cargo a harvester carries, not the spice on the map, not taxes or starting cash.
            curHouseInfo.spiceIncomeDropDown.setTooltipText(
                _("Spice delivery income only: credits paid when a harvester unloads"));
            // Campaign and skirmish co-op play at the original rate, like the campaign itself,
            // so their lobby shows 1x without offering a choice.
            curHouseInfo.spiceIncomeDropDown.setEnabled(
                !bLoadMultiplayer && bServer && !isCoopGameType(gameInitSettings.getGameType()));
            curHouseInfo.spiceIncomeDropDown.setOnSelectionChange(
                std::bind(&CustomGamePlayers::onChangeSpiceIncomeDropDownBoxes,
                          this, std::placeholders::_1, i));
            curHouseInfo.houseHBox.addWidget(HSpacer::create(row.subGap));
            if(row.spiceLabel > 0) {
                curHouseInfo.houseHBox.addWidget(&curHouseInfo.spiceIncomeLabel, row.spiceLabel);
            } else {
                curHouseInfo.spiceIncomeLabel.setVisible(false);
            }
            curHouseInfo.houseHBox.addWidget(&curHouseInfo.spiceIncomeDropDown, row.spiceBox);
        }

        if(duneCitySkinControls) {
            GameInitSettings::GraphicsSkin selectedSkin = GameInitSettings::GraphicsSkin::SimCity;
            if(isCoopGameType(gameInitSettings.getGameType())) {
                selectedSkin = gameInitSettings.getCampaignGraphicsSkin();
            }
            if(bLoadMultiplayer && i < static_cast<int>(houseInfoListSetup.size())) {
                selectedSkin = houseInfoListSetup.at(i).graphicsSkin;
            }
            curHouseInfo.graphicsSkinLabel.setText(_("Skin"));
            curHouseInfo.graphicsSkinLabel.setTextFontSize(12);
            curHouseInfo.graphicsSkinDropDown.addEntry(
                _("SimCity"), static_cast<int>(GameInitSettings::GraphicsSkin::SimCity));
            curHouseInfo.graphicsSkinDropDown.addEntry(
                _("Dune2"), static_cast<int>(GameInitSettings::GraphicsSkin::Dune2));
            curHouseInfo.graphicsSkinDropDown.setSelectedItem(
                selectedSkin == GameInitSettings::GraphicsSkin::Dune2 ? 1 : 0);
            curHouseInfo.graphicsSkinDropDown.setEnabled(!bLoadMultiplayer && bServer);
            curHouseInfo.graphicsSkinDropDown.setOnSelectionChange(
                std::bind(&CustomGamePlayers::onChangeGraphicsSkinDropDownBoxes,
                          this, std::placeholders::_1, i));
            curHouseInfo.houseHBox.addWidget(HSpacer::create(row.subGap));
            if(row.skinLabel > 0) {
                curHouseInfo.houseHBox.addWidget(&curHouseInfo.graphicsSkinLabel, row.skinLabel);
            } else {
                curHouseInfo.graphicsSkinLabel.setVisible(false);
            }
            curHouseInfo.houseHBox.addWidget(&curHouseInfo.graphicsSkinDropDown, row.skinBox);
        }

        int selectedColor = HOUSE_INVALID;
        if(bLoadMultiplayer) {
            if(i < (int) houseInfoListSetup.size()) {
                selectedColor = houseInfoListSetup.at(i).colorOfHouse;
            }
            curHouseInfo.bonusColorCheckbox.setText(_("Bonus"));
            curHouseInfo.bonusColorCheckbox.setChecked(isBonusColorSlot(selectedColor));
            addColorDropDownEntries(curHouseInfo.colorDropDown, selectedColor, curHouseInfo.bonusColorCheckbox.isChecked());
            curHouseInfo.bonusColorCheckbox.setEnabled(false);
            curHouseInfo.colorDropDown.setEnabled(false);
            curHouseInfo.colorDropDown.setOnClickEnabled(false);
        } else {
            curHouseInfo.bonusColorCheckbox.setText(_("Bonus"));
            curHouseInfo.bonusColorCheckbox.setChecked(false);
            addColorDropDownEntries(curHouseInfo.colorDropDown, HOUSE_INVALID, false);
        }
        curHouseInfo.bonusColorCheckbox.setOnClick(std::bind(&CustomGamePlayers::onBonusColorCheckbox, this, i));
        curHouseInfo.colorDropDown.setOnSelectionChange(std::bind(&CustomGamePlayers::onChangeColorDropDownBoxes, this, std::placeholders::_1, i));
        // Retain legacy colour selections for loaded matches and synchronized setup while
        // removing the colour and bonus-palette controls from Custom Game rows.
        curHouseInfo.bonusColorCheckbox.setVisible(false);
        curHouseInfo.bonusColorCheckbox.setEnabled(false);
        curHouseInfo.colorDropDown.setVisible(false);
        curHouseInfo.colorDropDown.setEnabled(false);

        curHouseInfo.houseInfoVBox.addWidget(&curHouseInfo.houseHBox);

        // add 1. player
        curHouseInfo.player1ArrowLabel.setTexture(pGFXManager->getUIGraphic(UI_CustomGamePlayersArrowNeutral));
        curHouseInfo.playerHBox.addWidget(&curHouseInfo.player1ArrowLabel);
        curHouseInfo.player1Label.setText(gameInitSettings.isMultiplePlayersPerHouse() ? (compactPlayers ? _("P1") : _("Player 1")) : _("Player"));
        curHouseInfo.player1Label.setTextFontSize(12);
        curHouseInfo.playerHBox.addWidget(&curHouseInfo.player1Label, gameInitSettings.isMultiplePlayersPerHouse() && compactPlayers ? 24 : 68);

        if(bLoadMultiplayer) {
            if(i < (int) houseInfoListSetup.size()) {
                GameInitSettings::HouseInfo gisHouseInfo = houseInfoListSetup.at(i);

                if(gisHouseInfo.houseID == HOUSE_UNUSED) {
                    curHouseInfo.player1DropDown.addEntry(_("closed"), PLAYER_CLOSED);
                } else if(gisHouseInfo.playerInfoList.empty() == false) {
                    GameInitSettings::PlayerInfo playerInfo = gisHouseInfo.playerInfoList.front();
                    if(playerInfo.playerClass == HUMANPLAYERCLASS) {
                        if(thisPlayerPlaced == false) {
                            curHouseInfo.player1DropDown.addEntry(settings.general.playerName, PLAYER_HUMAN);
                            curHouseInfo.player1DropDown.setSelectedItem(0);
                            thisPlayerPlaced = true;
                        } else {
                            curHouseInfo.player1DropDown.addEntry(_("open"), PLAYER_OPEN);
                            curHouseInfo.player1DropDown.setSelectedItem(0);
                        }
                    } else {
                        const PlayerFactory::PlayerData* pPlayerData = PlayerFactory::getByPlayerClass(playerInfo.playerClass);
                        int index = PlayerFactory::getIndexByPlayerClass(playerInfo.playerClass);

                        if(pPlayerData != nullptr) {
                            curHouseInfo.player1DropDown.addEntry(pPlayerData->getName(), -(index+2));
                            curHouseInfo.player1DropDown.setSelectedItem(0);
                            curHouseInfo.player1DropDown.setEnabled(false);
                            curHouseInfo.player1DropDown.setOnClickEnabled(false);
                        }
                    }
                }

            }

        } else if(bServer && (thisPlayerPlaced == false)) {
            curHouseInfo.player1DropDown.addEntry(settings.general.playerName, PLAYER_HUMAN);
            curHouseInfo.player1DropDown.setSelectedItem(0);
            curHouseInfo.player1DropDown.setEnabled(bServer);
            thisPlayerPlaced = true;
        } else {
            curHouseInfo.player1DropDown.addEntry(_("open"), PLAYER_OPEN);
            curHouseInfo.player1DropDown.setSelectedItem(0);
            curHouseInfo.player1DropDown.addEntry(_("closed"), PLAYER_CLOSED);
            for(unsigned int k = 1; k < PlayerFactory::getList().size(); k++) {
                const PlayerFactory::PlayerData* playerData = PlayerFactory::getByIndex(k);
                if(playerData == nullptr) {
                    continue;
                }
                const std::string& playerClass = playerData->getPlayerClass();
                if(playerClass.rfind("qBotSupport", 0) == 0 &&
                   playerClass != "qBotSupportEasy" &&
                   playerClass != "qBotSupportMedium" &&
                   playerClass != "qBotSupportHard" &&
                   playerClass != "qBotSupportBrutal") {
                    continue;
                }
                curHouseInfo.player1DropDown.addEntry(playerData->getName(), k);
            }
            curHouseInfo.player1DropDown.setEnabled(bServer);
        }
        curHouseInfo.player1DropDown.setOnSelectionChange(std::bind(&CustomGamePlayers::onChangePlayerDropDownBoxes, this, std::placeholders::_1, 2*i));
        curHouseInfo.player1DropDown.setOnClick(std::bind(&CustomGamePlayers::onClickPlayerDropDownBox, this, 2*i));
        curHouseInfo.playerHBox.addWidget(&curHouseInfo.player1DropDown, gameInitSettings.isMultiplePlayersPerHouse() && compactPlayers ? 145 : 180);

        if(gameInitSettings.isMultiplePlayersPerHouse()) curHouseInfo.playerHBox.addWidget(HSpacer::create(8));

        // add 2. player
        curHouseInfo.player2Label.setText(compactPlayers ? _("P2") : _("Player 2"));
        curHouseInfo.player2Label.setTextFontSize(12);
        if(gameInitSettings.isMultiplePlayersPerHouse()) curHouseInfo.playerHBox.addWidget(&curHouseInfo.player2Label, compactPlayers ? 24 : 68);

        if(bLoadMultiplayer) {
            if(i < (int) houseInfoListSetup.size()) {
                GameInitSettings::HouseInfo gisHouseInfo = houseInfoListSetup.at(i);

                if(gisHouseInfo.houseID == HOUSE_UNUSED) {
                    curHouseInfo.player2DropDown.addEntry(_("closed"), PLAYER_CLOSED);
                } else if(gisHouseInfo.playerInfoList.size() >= 2) {
                    GameInitSettings::PlayerInfo playerInfo = *(++gisHouseInfo.playerInfoList.begin());
                    if(playerInfo.playerClass == HUMANPLAYERCLASS) {
                        if(thisPlayerPlaced == false) {
                            curHouseInfo.player2DropDown.addEntry(settings.general.playerName, PLAYER_HUMAN);
                            curHouseInfo.player2DropDown.setSelectedItem(0);
                            thisPlayerPlaced = true;
                        } else {
                            curHouseInfo.player2DropDown.addEntry(_("open"), PLAYER_OPEN);
                            curHouseInfo.player2DropDown.setSelectedItem(0);
                        }
                    } else {
                        const PlayerFactory::PlayerData* pPlayerData = PlayerFactory::getByPlayerClass(playerInfo.playerClass);
                        int index = PlayerFactory::getIndexByPlayerClass(playerInfo.playerClass);

                        if(pPlayerData != nullptr) {
                            curHouseInfo.player1DropDown.addEntry(pPlayerData->getName(), -(index+2));
                            curHouseInfo.player1DropDown.setSelectedItem(0);
                            curHouseInfo.player1DropDown.setEnabled(false);
                            curHouseInfo.player1DropDown.setOnClickEnabled(false);
                        }
                    }
                }
            }
        } else if(bServer && (thisPlayerPlaced == false)) {
            curHouseInfo.player2DropDown.addEntry(settings.general.playerName, PLAYER_HUMAN);
            curHouseInfo.player2DropDown.setSelectedItem(0);
            curHouseInfo.player2DropDown.setEnabled(bServer);
            thisPlayerPlaced = true;
        } else {
            curHouseInfo.player2DropDown.addEntry(_("open"), PLAYER_OPEN);
            curHouseInfo.player2DropDown.setSelectedItem(0);
            curHouseInfo.player2DropDown.addEntry(_("closed"), PLAYER_CLOSED);
            for(unsigned int k = 1; k < PlayerFactory::getList().size(); k++) {
                const PlayerFactory::PlayerData* playerData = PlayerFactory::getByIndex(k);
                if(playerData == nullptr) {
                    continue;
                }
                const std::string& playerClass = playerData->getPlayerClass();
                if(playerClass.rfind("qBotSupport", 0) == 0 &&
                   playerClass != "qBotSupportEasy" &&
                   playerClass != "qBotSupportMedium" &&
                   playerClass != "qBotSupportHard" &&
                   playerClass != "qBotSupportBrutal") {
                    continue;
                }
                curHouseInfo.player2DropDown.addEntry(playerData->getName(), k);
            }
            curHouseInfo.player2DropDown.setEnabled(bServer);
        }
        curHouseInfo.player2DropDown.setOnSelectionChange(std::bind(&CustomGamePlayers::onChangePlayerDropDownBoxes, this, std::placeholders::_1, 2*i + 1));
        curHouseInfo.player2DropDown.setOnClick(std::bind(&CustomGamePlayers::onClickPlayerDropDownBox, this, 2*i + 1));
        if(gameInitSettings.isMultiplePlayersPerHouse()) curHouseInfo.playerHBox.addWidget(&curHouseInfo.player2DropDown, compactPlayers ? 145 : 180);

        curHouseInfo.houseInfoVBox.addWidget(&curHouseInfo.playerHBox);

        playerListVBox.addWidget(&curHouseInfo.houseInfoVBox, 0.01);

        playerListVBox.addWidget(VSpacer::create(4), 0.0);
        playerListVBox.addWidget(Spacer::create(), 0.07);

#ifdef __EMSCRIPTEN__
        // Browser build: each house row builds several dropdowns with full
        // entry lists; yield per row to keep the lobby transition paced.
        yieldFrameToBrowser();
#endif

        if(i >= numHouses) {
            curHouseInfo.houseInfoVBox.setEnabled(false);
            curHouseInfo.houseInfoVBox.setVisible(false);
        }
    }

    if(isCoopGameType(gameInitSettings.getGameType())) {
        auto& shared = houseInfo[0];
        shared.teamDropDown.setSelectedItem(0);
        shared.player2DropDown.clearAllEntries();
        shared.player2DropDown.addEntry(_("Open: human co-op"), PLAYER_OPEN);
        for(const char* cls : {"qBotEasy", "qBotMedium", "qBotHard", "qBotBrutal", "qBotDefend", "qBotSupportEasy", "qBotSupportMedium", "qBotSupportHard", "qBotSupportBrutal"}) {
            const int index = PlayerFactory::getIndexByPlayerClass(cls);
            if(index >= 0) shared.player2DropDown.addEntry(PlayerFactory::getByIndex(index)->getName(), index);
        }
        shared.player2DropDown.setSelectedItem(0);
    }
    if(this->startImmediately) {
        for(const auto& house : fixedCoopHouses) {
            if(house.houseID != gameInitSettings.getHouseID() || house.playerInfoList.size() < 2) continue;
            const auto& partner = *std::next(house.playerInfoList.begin());
            const int index = PlayerFactory::getIndexByPlayerClass(partner.playerClass);
            auto& choices = houseInfo[0].player2DropDown;
            for(unsigned i = 0; i < choices.getNumEntries(); ++i)
                if(choices.getEntryIntData(i) == index) choices.setSelectedItem(i);
        }
    }
    onChangeHousesDropDownBoxes(false);

    checkPlayerBoxes();

    leftVBox.addWidget(Spacer::create(), 0.3);

    // maybe there is a better fitting slot if we are loading a map in the old format with Brain=CPU and Brain=Human
    if(brainEqHumanSlot >= 0 && !spectating) {
        DropDownBox& dropDownBox1 = houseInfo[brainEqHumanSlot].player1DropDown;
        DropDownBox& dropDownBox2 = houseInfo[brainEqHumanSlot].player2DropDown;

        if(dropDownBox1.getSelectedEntry() != settings.general.playerName && dropDownBox2.getSelectedEntry() != settings.general.playerName) {
            if(dropDownBox1.getSelectedEntryIntData() == PLAYER_OPEN) {
                setPlayer2Slot(settings.general.playerName, brainEqHumanSlot*2);
            } else if((dropDownBox2.getSelectedEntryIntData() == PLAYER_OPEN) && gameInitSettings.isMultiplePlayersPerHouse()) {
                setPlayer2Slot(settings.general.playerName, brainEqHumanSlot*2+1);
            }
        }
    }

    if(setup && setup->players.changeEventList.empty() && !setup->online) {
        const int ai = PlayerFactory::getIndexByPlayerClass("qBotEasy");
        for(int slot = 0; slot < numHouses; ++slot) {
            auto& box = houseInfo[slot].player1DropDown;
            if(box.getSelectedEntryIntData() == PLAYER_OPEN)
                for(int i = 0; i < box.getNumEntries(); ++i) if(box.getEntryIntData(i) == ai) { box.setSelectedItem(i); break; }
        }
        checkPlayerBoxes();
    }
    restoringSetup = true;
    if(setup && !setup->players.changeEventList.empty()) onReceiveChangeEventList(setup->players);
    if(initialPlayers) onReceiveChangeEventList(*initialPlayers);
    restoringSetup = false;

    if(pNetworkManager != nullptr) {
        pNetworkManager->setOnPeerDisconnected(std::bind(&CustomGamePlayers::onPeerDisconnected, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        pNetworkManager->setOnReceiveChangeEventList(
            [this](const std::string& senderName, const ChangeEventList& changeEventList) {
                onReceiveChangeEventList(senderName, changeEventList);
            });
        pNetworkManager->setOnReceiveChatMessage(std::bind(&CustomGamePlayers::onReceiveChatMessage, this, std::placeholders::_1, std::placeholders::_2));
        pNetworkManager->setOnConfigMismatch(std::bind(&CustomGamePlayers::onConfigMismatch, this, std::placeholders::_1));
        pNetworkManager->setOnReceiveModInfo(std::bind(&CustomGamePlayers::onReceiveModInfo, this, std::placeholders::_1, std::placeholders::_2));
        pNetworkManager->setOnModDownloadComplete(std::bind(&CustomGamePlayers::onModDownloadComplete, this, std::placeholders::_1, std::placeholders::_2));
        pNetworkManager->setOnReceiveModAck(std::bind(&CustomGamePlayers::onReceiveModAck, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        if(bServer) {
            pNetworkManager->setGetChangeEventListForNewPlayerCallback(std::bind(&CustomGamePlayers::getChangeEventListForNewPlayer, this, std::placeholders::_1));
            // Relay guests can join before the host chooses a map. startServer immediately
            // sends their lobby snapshot, so seat assignment must already be available.
            pNetworkManager->startServer(bLANServer, gameInitSettings.getServername(), settings.general.playerName, &gameInitSettings, 1, gameInitSettings.isMultiplePlayersPerHouse() ? numHouses*2 : numHouses);
        }
        // Direct hosts also wait for the acknowledged roster before their countdown.
        if(!bServer || pNetworkManager->isDirectSession()) {
            pNetworkManager->setOnStartGame(std::bind(&CustomGamePlayers::onStartGame, this, std::placeholders::_1));
        }
        
        // Update Discord Rich Presence for multiplayer lobby
        updateDiscordLobbyPresence();

        // Only private hosts need an invitation code; public games are joined from the list.
        const std::string roomCode = pNetworkManager->getRoomCode();
        if(bServer && !pNetworkManager->isPublicRelayRoom() && !roomCode.empty()) {
            copyCodeButton.setVisible(true);
            copyCodeButton.setEnabled(true);
            copyCodeButton.setOnClick([this, roomCode]() {
                copyCodeButton.setText(WebRuntime::copyText(roomCode) ? _("Copied!") : _("Try again"));
            });
            captionLabel.setText(captionLabel.getText() + "   [" + roomCode + "]");
            addInfoMessage(_("Game code: ") + roomCode);
            if(bServer) {
                addInfoMessage(_("Give that code to a friend so they can join."));
            }
        }

        if(pNetworkManager->isRoomSession() && !bServer) {
            // Send our content fingerprint now, while the room is certainly still a lobby. On
            // the relay the host may declare the match started in the same breath as it sends
            // its own hashes, so answering that message later would be too late.
            pNetworkManager->sendConfigHash(getQuantBotConfig().getConfigHash(),
                                            getObjectDataHash(), VERSIONSTRING);
        }
    }

    if(this->startImmediately) {
        // Show progress, never the editable pregame roster, while starting.
        setWindowWidget(&launchWidget);
        const int width = getSize().x, height = getSize().y;
        launchTitle.setText(_("Starting campaign"));
        launchStatus.setText(_("Preparing your online game..."));
        cancelLaunch.setText(_("Back"));
        cancelLaunch.setOnClick([this]() { onCancel(); });
        launchWidget.addWidget(&launchTitle, Point(20, height/3), Point(width-40, 32));
        launchWidget.addWidget(&launchStatus, Point(20, height/3+48), Point(width-40, 100));
        launchWidget.addWidget(&cancelLaunch, Point(width/2-80, height-70), Point(160, 30));
    }
#ifdef __EMSCRIPTEN__
    // The opponent's connection state changes asynchronously; update()
    // refreshes the label.
    updateOpponentLabel();
#endif
}

#ifdef __EMSCRIPTEN__
void CustomGamePlayers::updateOpponentLabel() {
    if(pNetworkManager == nullptr) {
        opponentLabel.setText("-");
        return;
    }

    switch(pNetworkManager->getWebRtcState()) {
        case WebRtcTransport::State::Connecting: {
            opponentLabel.setText(_("Connecting..."));
        } break;

        case WebRtcTransport::State::Connected: {
            opponentLabel.setText(_("Connected"));
        } break;

        case WebRtcTransport::State::Failed: {
            opponentLabel.setText(_("Connection failed"));
        } break;

        case WebRtcTransport::State::Idle:
        default: {
            opponentLabel.setText("-");
        } break;
    }
}
#endif

void CustomGamePlayers::updateDiscordLobbyPresence() {
    if(pNetworkManager == nullptr) return;
    
    std::string mapName = getBasename(gameInitSettings.getFilename(), true);
    int maxPlayers = gameInitSettings.isMultiplePlayersPerHouse() ? numHouses*2 : numHouses;
    
    if(bServer) {
        // Count current players from actual connected peers (not dropdown selections)
        // This is accurate even if a slot shows "Human" but no peer is connected
        int currentPlayers = static_cast<int>(pNetworkManager->getConnectedPeers().size()) + 1;  // +1 for host
        DiscordManager::instance().setHostingGame(mapName, currentPlayers, maxPlayers);
    } else {
        DiscordManager::instance().setInLobby(gameInitSettings.getServername(), mapName);
    }
}

CustomGamePlayers::~CustomGamePlayers()
{
    if(pNetworkManager != nullptr) {
        pNetworkManager->disconnect();

        pNetworkManager->setOnPeerDisconnected(std::function<void (const std::string&, bool, int)>());
        pNetworkManager->setGetChangeEventListForNewPlayerCallback(std::function<ChangeEventList (const std::string&)>());
        pNetworkManager->setOnReceiveChangeEventList(std::function<void (const std::string&, const ChangeEventList&)>());
        pNetworkManager->setOnReceiveChatMessage(std::function<void (const std::string&, const std::string&)>());
        pNetworkManager->setOnStartGame(std::function<void (unsigned int)>());
        pNetworkManager->setOnReceiveModInfo(std::function<void (const std::string&, const std::string&)>());
        pNetworkManager->setOnModDownloadComplete(std::function<void (bool, const std::string&)>());
        pNetworkManager->setOnReceiveModAck(std::function<void (const std::string&, bool, const std::string&)>());

        if(bServer) {
            try {
                pNetworkManager->stopServer();
            } catch(std::exception& e) {
                SDL_Log("CustomGamePlayers::~CustomGamePlayers(): %s", e.what());
            }
        }
    }
}

int CustomGamePlayers::showMenu() {
    const int result = MenuBase::showMenu();
    if(rebuildAfterModTransfer) {
        // House availability, sprites and map preview all depend on the installed mod.
        // Rebuild after the network callback returns, replay the host's buffered setup,
        // and only then acknowledge readiness.
        CustomGamePlayers restored(gameInitSettings, false);
        restored.onReceiveChangeEventList(delayedModChanges);
        if(pNetworkManager) {
            pNetworkManager->sendConfigHash(getQuantBotConfig().getConfigHash(), getObjectDataHash(), VERSIONSTRING);
            pNetworkManager->sendModAck(true, ModManager::instance().getEffectiveChecksums().combined);
        }
        return restored.showMenu();
    }
    if(setup && result == MENU_QUIT_DEFAULT) {
        setup->players = getChangeEventList();
        return MENU_SETUP_MAP;
    }
    return result;
}

void CustomGamePlayers::rebuildSetup(bool keepPlayers) {
    setup->players = keepPlayers ? getChangeEventList() : ChangeEventList{};
    quit(MENU_SETUP_CHANGED);
}

void CustomGamePlayers::onChildWindowClose(Window* child) {
    if(auto* rules = dynamic_cast<GameOptionsWindow*>(child); rules && setup) {
        setup->rules = rules->getGameOptions();
        rebuildSetup(true);
    }
}

void CustomGamePlayers::update() {
    if(automaticStartPending) {
        automaticStartPending = false;
        onNext();
    }
#ifdef __EMSCRIPTEN__
    updateOpponentLabel();
#endif

    if(isCoopGameType(gameInitSettings.getGameType()) && startGameTime == 0 && bServer && !bWaitingForModAcks) {
        const int partner = houseInfo[0].player2DropDown.getSelectedEntryIntData();
        const bool solo = partner == PLAYER_OPEN || partner == PLAYER_CLOSED;
        nextButton.setEnabled(houseInfo[0].player1DropDown.getSelectedEntryIntData() == PLAYER_HUMAN);
        readinessLabel.setText(!installerApprovedForDisplay
            ? _("Players must join this lobby before you start. Hot joining is unavailable for new mods.")
            : solo ? _("Start now. Others can watch or ask to join while you play.") : _("Your co-op partner is ready."));
    } else if(!bServer && startGameTime == 0) readinessLabel.setText(_("Waiting for the host to start."));
    else if(setup) readinessLabel.setText(
        _("Spice income: multiplies credits from harvester deliveries. 2x = double."));

    if(startGameTime > 0) {
        // Check if config mismatch was detected - abort game start
        if(bConfigMismatchDetected) {
            SDL_Log("Aborting game start - config mismatch detected");
            startGameTime = 0;
            return;
        }
        
        if(SDL_GetTicks() >= startGameTime) {
            startGameTime = 0;
            if(!WorkshopGameContent::matches(gameInitSettings)) {
                onConfigMismatch(_("The selected mod revision changed. Reopen the lobby to share the updated version."));
                return;
            }

            pNetworkManager->setOnPeerDisconnected(std::function<void (const std::string&, bool, int)>());
            pNetworkManager->setGetChangeEventListForNewPlayerCallback(std::function<ChangeEventList (const std::string&)>());
            pNetworkManager->setOnReceiveChangeEventList(std::function<void (const std::string&, const ChangeEventList&)>());
            pNetworkManager->setOnReceiveChatMessage(std::function<void (const std::string&, const std::string&)>());
            pNetworkManager->setOnStartGame(std::function<void (unsigned int)>());

            if(bServer) {
                // Stop announcing the game in lobby, but DON'T reset bIsServer
                // The server needs to remain active during the game for multiplayer budget negotiation
                pNetworkManager->stopAnnouncing();
            }

            addAllPlayersToGameInitSettings();
            startMultiPlayerGame(gameInitSettings);

            quit(MENU_QUIT_GAME_FINISHED);
        } else {
            static Uint32 lastSecondLeft = 0;
            Uint32 secondsLeft = ((startGameTime - SDL_GetTicks())/1000) + 1;
            if(lastSecondLeft != secondsLeft) {
                lastSecondLeft = secondsLeft;
                addInfoMessage("Starting game in " + std::to_string(secondsLeft) + "...");
            }
        }
    }
}

LobbyAuthorization::SeatSnapshot CustomGamePlayers::makeSeatSnapshot() const {
    LobbyAuthorization::SeatSnapshot snapshot;
    snapshot.numHouses = numHouses;
    snapshot.multiplePlayersPerHouse = gameInitSettings.isMultiplePlayersPerHouse();

    const int slotCount = std::min(numHouses * 2,
                                   static_cast<int>(snapshot.slots.size()));
    for(int slot = 0; slot < slotCount; slot++) {
        const HouseInfo& curHouseInfo = houseInfo[slot / 2];
        const DropDownBox& dropDownBox = (slot % 2 == 0) ? curHouseInfo.player1DropDown
                                                         : curHouseInfo.player2DropDown;

        LobbyAuthorization::SlotState& state = snapshot.slots[static_cast<std::size_t>(slot)];
        const int entryData = dropDownBox.getSelectedEntryIntData();
        if(entryData == PLAYER_HUMAN) {
            state.kind = LobbyAuthorization::SlotKind::Human;
            state.name = dropDownBox.getSelectedEntry();
        } else if(entryData == PLAYER_OPEN) {
            state.kind = LobbyAuthorization::SlotKind::Open;
        } else if(entryData == PLAYER_CLOSED) {
            state.kind = LobbyAuthorization::SlotKind::Closed;
        } else {
            state.kind = LobbyAuthorization::SlotKind::AI;
        }
    }

    return snapshot;
}

void CustomGamePlayers::onReceiveChangeEventList(const std::string& senderName,
                                                 const ChangeEventList& changeEventList)
{
    if(!bServer && waitingForModInstall) {
        // Keep the authoritative events, not the incomplete pre-download widgets.
        if(delayedModChanges.changeEventList.size() + changeEventList.changeEventList.size() > 4096) {
            onConfigMismatch(_("Too many lobby changes arrived while receiving the mod."));
            return;
        }
        delayedModChanges.changeEventList.insert(delayedModChanges.changeEventList.end(),
            changeEventList.changeEventList.begin(), changeEventList.changeEventList.end());
        return;
    }

    // On the host a non-empty sender is a remote client, and everything it asks for has to be
    // something its own widgets could have produced: it may claim a seat for itself and change
    // the house it occupies, nothing else. The whole transaction is judged first, so a list
    // that mixes a legal and an illegal event changes nothing and is not rebroadcast.
    if(bServer && !senderName.empty()) {
        std::size_t refusedIndex = 0;
        const LobbyAuthorization::Decision decision = LobbyAuthorization::authorizeClientTransaction(
            makeSeatSnapshot(), senderName, changeEventList.changeEventList, refusedIndex);

        if(decision != LobbyAuthorization::Decision::Allow) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "CustomGamePlayers: refusing lobby change %zu from '%s': %s",
                        refusedIndex, senderName.c_str(),
                        LobbyAuthorization::describeDecision(decision));
            addInfoMessage("Ignored an unauthorized lobby change from " + senderName);
            return;
        }
    }

    for(const ChangeEventList::ChangeEvent& changeEvent : changeEventList.changeEventList) {

        // houseInfo has MAX_CUSTOM_GAME_PLAYERS entries and the slot arrives over the network:
        // house-level events index it directly, player events index it as slot/2.
        const bool isPlayerSlotEvent =
            (changeEvent.eventType == ChangeEventList::ChangeEvent::EventType::ChangePlayer)
            || (changeEvent.eventType == ChangeEventList::ChangeEvent::EventType::SetHumanPlayer);
        const Uint32 slotLimit = isPlayerSlotEvent ? static_cast<Uint32>(numHouses) * 2u
                                                   : static_cast<Uint32>(numHouses);
        if(numHouses <= 0 || numHouses > MAX_CUSTOM_GAME_PLAYERS || changeEvent.slot >= slotLimit) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "CustomGamePlayers: ignoring lobby change event for slot %u",
                        changeEvent.slot);
            continue;
        }

        switch(changeEvent.eventType) {
            case ChangeEventList::ChangeEvent::EventType::ChangeHouse: {
                HOUSETYPE houseType = (HOUSETYPE) changeEvent.newValue;

                HouseInfo& curHouseInfo = houseInfo[changeEvent.slot];

                for(int i=0;i<curHouseInfo.houseDropDown.getNumEntries();i++) {
                    if(curHouseInfo.houseDropDown.getEntryIntData(i) == houseType) {
                        curHouseInfo.houseDropDown.setSelectedItem(i);
                        break;
                    }
                }
            } break;

            case ChangeEventList::ChangeEvent::EventType::ChangeTeam: {
                int newTeam = (int) changeEvent.newValue;

                HouseInfo& curHouseInfo = houseInfo[changeEvent.slot];

                for(int i=0;i<curHouseInfo.teamDropDown.getNumEntries();i++) {
                    if(curHouseInfo.teamDropDown.getEntryIntData(i) == newTeam) {
                        curHouseInfo.teamDropDown.setSelectedItem(i);
                        break;
                    }
                }
            } break;

            case ChangeEventList::ChangeEvent::EventType::ChangeColor: {
                int newColor = (int) changeEvent.newValue;

                HouseInfo& curHouseInfo = houseInfo[changeEvent.slot];

                if(curHouseInfo.bonusColorCheckbox.isChecked() != isCustomHouseColorSlot(newColor)) {
                    curHouseInfo.bonusColorCheckbox.setChecked(isCustomHouseColorSlot(newColor));
                    addColorDropDownEntries(curHouseInfo.colorDropDown, newColor, curHouseInfo.bonusColorCheckbox.isChecked());
                }

                for(int i=0;i<curHouseInfo.colorDropDown.getNumEntries();i++) {
                    if(curHouseInfo.colorDropDown.getEntryIntData(i) == newColor) {
                        curHouseInfo.colorDropDown.setSelectedItem(i);
                        break;
                    }
                }
                onChangeHousesDropDownBoxes(false, changeEvent.slot);
            } break;

            case ChangeEventList::ChangeEvent::EventType::ChangeGraphicsSkin: {
                const auto skin = GameInitSettings::sanitizeGraphicsSkin(changeEvent.newValue);
                HouseInfo& curHouseInfo = houseInfo[changeEvent.slot];
                curHouseInfo.graphicsSkinDropDown.setSelectedItem(
                    skin == GameInitSettings::GraphicsSkin::Dune2 ? 1 : 0);
            } break;

            case ChangeEventList::ChangeEvent::EventType::ChangeSpiceIncome: {
                // Reaching here means the host decided this: a client's request was refused
                // by authorizeClientTransaction before anything was applied, and the host's
                // own broadcast is what a client sees. A factor outside the accepted range is
                // dropped rather than rounded, so a modified host cannot move this lobby to a
                // rate the local build would not itself play at.
                if(!SpiceIncome::isValid(changeEvent.newValue)) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "CustomGamePlayers: ignoring invalid spice income factor %u",
                                changeEvent.newValue);
                    break;
                }
                DropDownBox& box = houseInfo[changeEvent.slot].spiceIncomeDropDown;
                for(int entry = 0; entry < box.getNumEntries(); entry++) {
                    if(box.getEntryIntData(entry) == static_cast<int>(changeEvent.newValue)) {
                        box.setSelectedItem(entry);
                        break;
                    }
                }
            } break;

            case ChangeEventList::ChangeEvent::EventType::ChangePlayer: {
                int newPlayer = (int) changeEvent.newValue;

                HouseInfo& curHouseInfo = houseInfo[changeEvent.slot/2];

                if(changeEvent.slot % 2 == 0) {
                    for(int i=0;i<curHouseInfo.player1DropDown.getNumEntries();i++) {
                        if(curHouseInfo.player1DropDown.getEntryIntData(i) == newPlayer) {
                            curHouseInfo.player1DropDown.setSelectedItem(i);
                            break;
                        }
                    }
                } else {
                    for(int i=0;i<curHouseInfo.player2DropDown.getNumEntries();i++) {
                        if(curHouseInfo.player2DropDown.getEntryIntData(i) == newPlayer) {
                            curHouseInfo.player2DropDown.setSelectedItem(i);
                            break;
                        }
                    }
                }

                checkPlayerBoxes();
            } break;

            case ChangeEventList::ChangeEvent::EventType::SetHumanPlayer: {
                const std::string& name = changeEvent.newStringValue;
                int slot = changeEvent.slot;

                setPlayer2Slot(name, slot);

                checkPlayerBoxes();
            } break;

            default: {
            } break;
        }
    }

    if((pNetworkManager != nullptr) && bServer && !restoringSetup) {
        ChangeEventList changeEventList2 = getChangeEventList();

        pNetworkManager->sendChangeEventList(changeEventList2);
    }
    
    // Update Discord presence when lobby state changes (players join/leave/change slots)
    updateDiscordLobbyPresence();
}

ChangeEventList CustomGamePlayers::getChangeEventList()
{
    ChangeEventList changeEventList;

    for(int i=0;i<numHouses;i++) {
        HouseInfo& curHouseInfo = houseInfo[i];

        int houseID = curHouseInfo.houseDropDown.getSelectedEntryIntData();
        int team = curHouseInfo.teamDropDown.getSelectedEntryIntData();
        int color = curHouseInfo.colorDropDown.getSelectedEntryIntData();
        int player1 = curHouseInfo.player1DropDown.getSelectedEntryIntData();
        int player2 = curHouseInfo.player2DropDown.getSelectedEntryIntData();

        changeEventList.changeEventList.push_back(ChangeEventList::ChangeEvent(ChangeEventList::ChangeEvent::EventType::ChangeHouse, i, houseID));
        changeEventList.changeEventList.push_back(ChangeEventList::ChangeEvent(ChangeEventList::ChangeEvent::EventType::ChangeTeam, i, team));
        changeEventList.changeEventList.push_back(ChangeEventList::ChangeEvent(ChangeEventList::ChangeEvent::EventType::ChangeColor, i, color));
        if(duneCitySkinControls) {
            const int skin = curHouseInfo.graphicsSkinDropDown.getSelectedEntryIntData();
            changeEventList.changeEventList.emplace_back(
                ChangeEventList::ChangeEvent::EventType::ChangeGraphicsSkin, i, skin);
        }
        // Unconditional: the selector exists in every mod, so this is also what carries the
        // choice across a Back/Next trip through the map browser and across the connection and
        // shared-house toggles, which rebuild the lobby from this list.
        changeEventList.changeEventList.emplace_back(
            ChangeEventList::ChangeEvent::EventType::ChangeSpiceIncome, i,
            static_cast<Uint32>(curHouseInfo.spiceIncomeDropDown.getSelectedEntryIntData()));

        if(player1 == PLAYER_HUMAN) {
            std::string playername = curHouseInfo.player1DropDown.getSelectedEntry();
            changeEventList.changeEventList.push_back(ChangeEventList::ChangeEvent(2*i, playername));
        } else {
            changeEventList.changeEventList.push_back(ChangeEventList::ChangeEvent(ChangeEventList::ChangeEvent::EventType::ChangePlayer, 2*i, player1));
        }

        if(player2 == PLAYER_HUMAN) {
            std::string playername = curHouseInfo.player2DropDown.getSelectedEntry();
            changeEventList.changeEventList.emplace_back(2*i+1, playername);
        } else {
            changeEventList.changeEventList.emplace_back(ChangeEventList::ChangeEvent::EventType::ChangePlayer, 2*i+1, player2);
        }
    }

    return changeEventList;
}

ChangeEventList CustomGamePlayers::getChangeEventListForNewPlayer(const std::string& newPlayerName)
{
    ChangeEventList changeEventList = getChangeEventList();

    // find a place for the new player

    int newPlayerSlot = INVALID;

    // look for an open left slot
    for(int i=0;i<numHouses;i++) {
        HouseInfo& curHouseInfo = houseInfo[i];
        int player1 = curHouseInfo.player1DropDown.getSelectedEntryIntData();

        if(player1 == PLAYER_OPEN) {
            newPlayerSlot = 2*i;
            break;
        }
    }

    // if multiple players per house look for an open right slot
    if((newPlayerSlot == INVALID) && gameInitSettings.isMultiplePlayersPerHouse()) {
        for(int i=0;i<numHouses;i++) {
            HouseInfo& curHouseInfo = houseInfo[i];
            int player2 = curHouseInfo.player2DropDown.getSelectedEntryIntData();

            if(player2 == PLAYER_OPEN) {
                newPlayerSlot = 2*i + 1;
                break;
            }
        }
    }

    // as a fallback look for any non-human slot
    if(newPlayerSlot == INVALID) {
        for(int i=0;i<numHouses;i++) {
            HouseInfo& curHouseInfo = houseInfo[i];
            int player1 = curHouseInfo.player1DropDown.getSelectedEntryIntData();
            int player2 = curHouseInfo.player2DropDown.getSelectedEntryIntData();

            if(player1 != PLAYER_HUMAN) {
                newPlayerSlot = 2*i;
                break;
            } else if(gameInitSettings.isMultiplePlayersPerHouse() && (player2 != PLAYER_HUMAN)) {
                newPlayerSlot = 2*i + 1;
                break;
            }
        }
    }

    if(newPlayerSlot != INVALID) {
        setPlayer2Slot(newPlayerName, newPlayerSlot);

        ChangeEventList::ChangeEvent changeEvent(newPlayerSlot, newPlayerName);

        // add changeEvent to changeEventList for the new player
        changeEventList.changeEventList.push_back(changeEvent);

        // send it to all other connected peers
        ChangeEventList changeEventList2;
        changeEventList2.changeEventList.push_back(changeEvent);

        pNetworkManager->sendChangeEventList(changeEventList2);
    }

    return changeEventList;
}

void CustomGamePlayers::onReceiveChatMessage(const std::string& name, const std::string& message) {
    addChatMessage(name, message);
}

void CustomGamePlayers::onConfigMismatch(const std::string& errorMessage) {
    SDL_Log("CONFIG MISMATCH DETECTED: %s", errorMessage.c_str());
    
    // Set flag to prevent game from starting
    bConfigMismatchDetected = true;
    
    // Cancel game start countdown
    startGameTime = 0;
    
    // Show error dialog
    openWindow(MsgBox::create(errorMessage));
    
    // Also display in chat so all players can see the details
    if(pNetworkManager != nullptr) {
        // Send chat message with mismatch details
        pNetworkManager->sendChatMessage("*** CONFIG MISMATCH DETECTED ***");
        pNetworkManager->sendChatMessage(errorMessage);
        pNetworkManager->sendChatMessage("*** FIX CONFIG FILES AND TRY AGAIN ***");
    }
    
    // Stay in lobby - players can see the error in chat and fix their configs
    // Re-enable dropdowns so they can adjust settings if needed
    // (Game start was cancelled above)
}

void CustomGamePlayers::onReceiveModInfo(const std::string& modName, const std::string& modChecksum) {
    // Client receives mod info from host
    if(bServer) {
        // Server shouldn't receive this
        return;
    }
    if(communityModDownloadInProgress) return;
    
    SDL_Log("CLIENT: Received mod info from host - mod='%s', checksum=%s", modName.c_str(), modChecksum.c_str());
    
    hostModName = modName;
    hostModChecksum = modChecksum;
    if(!gameInitSettings.getModRevisionHash().empty()) {
        if(!WorkshopGameContent::announcesRevision(modName, gameInitSettings)) {
            onConfigMismatch(_("The host announced a different mod from the selected revision."));
            if(pNetworkManager) pNetworkManager->sendModAck(false, "");
            return;
        }
        if(!WorkshopGameContent::matches(gameInitSettings)) {
#ifdef __EMSCRIPTEN__
            // The host has published before sending setup. Resolve through the
            // content service first, which also supports packages over the peer
            // transfer limit. Call only after lobby callbacks are installed.
            if(!communityModDownloadAttempted) {
                communityModDownloadAttempted = true;
                communityModDownloadInProgress = true;
                try { WorkshopGameContent::resolveMod(gameInitSettings); }
                catch(const std::exception&) { /* Retain the bounded peer fallback. */ }
                communityModDownloadInProgress = false;
                if(WorkshopGameContent::matches(gameInitSettings)) {
                    if(ModManager::instance().getEffectiveChecksums().combined != modChecksum) {
                        onConfigMismatch(_("The host announced a different checksum from the selected revision."));
                        if(pNetworkManager) pNetworkManager->sendModAck(false, "");
                    } else {
                        rebuildAfterModTransfer = true;
                        quit();
                    }
                    return;
                }
            }
#endif
            if(pNetworkManager && pNetworkManager->supportsModTransfer()) {
                bModDownloadInProgress = true;
                addInfoMessage(_("Receiving the exact mod version from the host..."));
                pNetworkManager->requestModDownload(modName);
            } else {
                onConfigMismatch(_("The downloaded mod does not match the host's exact revision."));
                if(pNetworkManager) pNetworkManager->sendModAck(false, "");
            }
            return;
        }
    }
    bModDownloadInProgress = false;

    // Compare with our local mod
    std::string localModName = ModManager::instance().getActiveModName();
    std::string localChecksum = ModManager::instance().getEffectiveChecksums().combined;
    
    SDL_Log("CLIENT: Local mod='%s', checksum=%s", localModName.c_str(), localChecksum.c_str());
    
    if(modName == localModName && modChecksum == localChecksum) {
        SDL_Log("CLIENT: Mod checksums match!");
        addInfoMessage(_("Mod verified: ") + ModManager::instance().getModInfo(modName).displayName
            + (gameInitSettings.getModRevisionVersion() ? " v" + std::to_string(gameInitSettings.getModRevisionVersion()) : ""));
        
        // Send ACK to host - checksums match, ready to start
        if(pNetworkManager != nullptr) {
            pNetworkManager->sendConfigHash(getQuantBotConfig().getConfigHash(), getObjectDataHash(), VERSIONSTRING);
            pNetworkManager->sendModAck(true, localChecksum);
        }
        return;
    }
    
    // Checksum mismatch - need to sync
    SDL_Log("CLIENT: Mod mismatch! Host uses '%s', we have '%s'", modName.c_str(), localModName.c_str());
    
    // Check if we have the host's mod locally
    if(ModManager::instance().modExists(modName)) {
        SDL_Log("CLIENT: Found mod '%s' locally, attempting to switch to it first", modName.c_str());
        
        // Try switching to the host's mod locally first
        if(ModManager::instance().setActiveMod(modName)) {
            // Recalculate checksums after switching
            ModManager::instance().updateChecksums();
            std::string newChecksum = ModManager::instance().getEffectiveChecksums().combined;
            
            SDL_Log("CLIENT: Switched to local mod '%s', new checksum=%s", modName.c_str(), newChecksum.c_str());
            
            // Update the mod label on screen
            ModInfo activeModInfo = ModManager::instance().getModInfo(modName);
            mapPropertyMod.setText(activeModInfo.displayName);
            mapPropertyCity.setText(activeModInfo.enablesCityMode ? _("On") : _("Off"));
            
            // Reload effective game options for the new mod
            effectiveGameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
            
            if(newChecksum == modChecksum) {
                // Local mod matches host - no download needed!
                SDL_Log("CLIENT: Local mod '%s' matches host after switching!", modName.c_str());
                addInfoMessage("Switched to mod: " + modName);
                
                if(pNetworkManager != nullptr) {
                    pNetworkManager->sendConfigHash(getQuantBotConfig().getConfigHash(), getObjectDataHash(), VERSIONSTRING);
                    pNetworkManager->sendModAck(true, newChecksum);
                }
                return;
            } else {
                // Checksums still differ - may need to download host's version
                SDL_Log("CLIENT: Local mod '%s' checksums still differ (local=%s, host=%s)", 
                        modName.c_str(), newChecksum.c_str(), modChecksum.c_str());
                
                // Special case: vanilla mod cannot be downloaded/overwritten
                // If checksums differ, it's likely a game version mismatch
                if(modName == "vanilla") {
                    SDL_Log("CLIENT: Vanilla mod checksum mismatch - cannot sync (possible version mismatch)");
                    addInfoMessage("Vanilla mod version mismatch with host!");
                    bConfigMismatchDetected = true;
                    
                    openWindow(MsgBox::create(_("Vanilla mod checksum mismatch.\n\nThis usually means different game versions.\nHost: ") + modChecksum + "\nLocal: " + newChecksum));
                    
                    if(pNetworkManager != nullptr) {
                        pNetworkManager->sendModAck(false, "");
                    }
                    return;
                }
            }
        }
    }
    
    // Mod doesn't exist locally or switching didn't help - download from host
    // Note: vanilla mod cannot be downloaded (it's seeded locally)
    if(modName == "vanilla") {
        SDL_Log("CLIENT: Cannot download vanilla mod - should be seeded locally!");
        addInfoMessage("Error: vanilla mod not found locally!");
        bConfigMismatchDetected = true;
        
        // Try to seed vanilla now
        if(!ModManager::instance().modExists("vanilla")) {
            SDL_Log("CLIENT: Attempting emergency vanilla reseed");
            ModManager::instance().initialize();  // Will reseed vanilla if missing
        }
        
        openWindow(MsgBox::create(_("Vanilla mod not found or corrupted.\n\nPlease restart the game to reseed the vanilla configuration.")));
        
        if(pNetworkManager != nullptr) {
            pNetworkManager->sendModAck(false, "");
        }
        return;
    }
    
    // Show message and start download
    addInfoMessage("Downloading mod '" + modName + "' from host...");
    bModDownloadInProgress = true;
    
    if(pNetworkManager != nullptr) {
        pNetworkManager->requestModDownload(modName);
    }
}

void CustomGamePlayers::onModDownloadComplete(bool success, const std::string& data) {
    bModDownloadInProgress = false;
    
    if(!success) {
        SDL_Log("CLIENT: Mod download failed: %s", data.c_str());
        addInfoMessage("Mod download failed: " + data);
        
        // Show error dialog
        openWindow(MsgBox::create(_("Mod download failed: ") + data + "\n\n" + _("Cannot join game with mismatched mods.")));
        
        // Set mismatch flag to prevent game start
        bConfigMismatchDetected = true;
        if(pNetworkManager) pNetworkManager->sendModAck(false, "");
        return;
    }
    
    SDL_Log("CLIENT: Mod download complete (%zu bytes)", data.size());

    if(hostModName.empty() || !ModManager::instance().isValidModName(hostModName)) {
        SDL_Log("CLIENT: Refusing mod payload without a valid announced mod name");
        addInfoMessage("Mod sync failed: unexpected payload");
        bConfigMismatchDetected = true;
        if(pNetworkManager != nullptr) {
            pNetworkManager->sendModAck(false, "");
        }
        return;
    }

    // Verify the staged payload against the checksum the host announced *before* anything is
    // installed or activated. A mismatch leaves the existing mod directory untouched.
    if(ModManager::instance().saveReceivedMod(hostModName, data, hostModChecksum)) {
        SDL_Log("CLIENT: Mod '%s' saved successfully", hostModName.c_str());
        
        // Switch to the new mod
        const bool activated = gameInitSettings.getModRevisionHash().empty()
            ? ModManager::instance().setActiveMod(hostModName)
            : (hostModName == "ws-" + gameInitSettings.getModRevisionHash()
               && Workshop::activateModRevision(gameInitSettings.getModRevisionHash())
               && WorkshopGameContent::matches(gameInitSettings));
        if(activated) {
            // Reload effective game options
            effectiveGameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
            
            // Recalculate checksums after loading new mod
            ModManager::instance().updateChecksums();
            std::string newChecksum = ModManager::instance().getEffectiveChecksums().combined;
            
            // Update the mod label on screen to show the new mod
            ModInfo activeModInfo = ModManager::instance().getModInfo(hostModName);
            mapPropertyMod.setText(activeModInfo.displayName);
            mapPropertyCity.setText(activeModInfo.enablesCityMode ? _("On") : _("Off"));
            
            SDL_Log("CLIENT: Switched to mod '%s', new checksum: %s", hostModName.c_str(), newChecksum.c_str());

            if(newChecksum == hostModChecksum) {
                if(waitingForModInstall) {
                    rebuildAfterModTransfer = true;
                    quit();
                    return;
                }
                addInfoMessage("Mod '" + hostModName + "' synced successfully!");
                if(pNetworkManager != nullptr) {
                    pNetworkManager->sendConfigHash(getQuantBotConfig().getConfigHash(), getObjectDataHash(), VERSIONSTRING);
                    pNetworkManager->sendModAck(true, newChecksum);
                }
            } else {
                SDL_Log("CLIENT: Downloaded mod checksum mismatch (host=%s, local=%s)",
                        hostModChecksum.c_str(), newChecksum.c_str());
                addInfoMessage("Downloaded mod failed integrity verification: " + hostModName);
                bConfigMismatchDetected = true;
                if(pNetworkManager != nullptr) {
                    pNetworkManager->sendModAck(false, newChecksum);
                }
            }
        } else {
            SDL_Log("CLIENT: Failed to switch to mod '%s'", hostModName.c_str());
            addInfoMessage("Failed to activate mod: " + hostModName);
            bConfigMismatchDetected = true;
            
            // Send failure ACK
            if(pNetworkManager != nullptr) {
                pNetworkManager->sendModAck(false, "");
            }
        }
    } else {
        // Either the payload was malformed or it did not match the announced checksum; in
        // both cases nothing was installed and the previously active mod is untouched.
        SDL_Log("CLIENT: Refused mod '%s' (verification or unpacking failed)", hostModName.c_str());
        addInfoMessage("Mod rejected before installation: " + hostModName);
        bConfigMismatchDetected = true;
        
        // Send failure ACK
        if(pNetworkManager != nullptr) {
            pNetworkManager->sendModAck(false, "");
        }
    }
}

void CustomGamePlayers::onReceiveModAck(const std::string& playerName, bool success, const std::string& modChecksum) {
    // Host receives mod sync acknowledgment from client
    if(!bServer) {
        // Client shouldn't receive this
        return;
    }
    
    if(!bWaitingForModAcks) {
        SDL_Log("HOST: Received unexpected mod ACK from '%s' (not waiting for ACKs)", playerName.c_str());
        return;
    }
    
    if(!success) {
        SDL_Log("HOST: Client '%s' failed to sync mod!", playerName.c_str());
        addInfoMessage("Client '" + playerName + "' failed to sync mod!");
        
        // Cancel game start
        bConfigMismatchDetected = true;
        startGameTime = 0;
        bWaitingForModAcks = false;
        
        openWindow(MsgBox::create(_("Client '") + playerName + _("' failed to sync mod.\nGame cannot start.")));
        return;
    }
    
    // Verify checksum matches
    std::string hostChecksum = ModManager::instance().getEffectiveChecksums().combined;
    if(modChecksum != hostChecksum) {
        SDL_Log("HOST: Client '%s' has wrong checksum! Expected: %s, Got: %s",
                playerName.c_str(), hostChecksum.c_str(), modChecksum.c_str());
        addInfoMessage("Checksum mismatch from '" + playerName + "'!");
        
        // Cancel game start
        bConfigMismatchDetected = true;
        startGameTime = 0;
        bWaitingForModAcks = false;
        
        openWindow(MsgBox::create(_("Client '") + playerName + _("' has mismatched checksums.\nGame cannot start.")));
        return;
    }
    
    SDL_Log("HOST: Client '%s' mod synced OK (checksum: %s)", playerName.c_str(), modChecksum.c_str());
    addInfoMessage("Client '" + playerName + "' ready");
    
    clientsAckedMod.insert(playerName);
    checkAllClientsReady();
}

void CustomGamePlayers::checkAllClientsReady() {
    if(!bServer || !bWaitingForModAcks) {
        return;
    }

    // The countdown is what commits everyone to the match: it sends STARTGAME and, on the relay,
    // moves the room into its match phase. Once that has happened, "we noticed a problem" is too
    // late, so the check belongs here rather than only in update().
    if(bConfigMismatchDetected) {
        SDL_Log("HOST: not starting - a content mismatch was reported");
        bWaitingForModAcks = false;
        return;
    }

    // Get list of all connected remote peers (humans only). AI slots are not peers.
    std::set<std::string> connectedPlayers;
    if (pNetworkManager != nullptr) {
        for (const auto& name : pNetworkManager->getConnectedPeers()) {
            if (name != settings.general.playerName) {
                connectedPlayers.insert(name);
            }
        }
    }
    
    SDL_Log("HOST: Checking if all clients ready - ACKed: %zu, Connected: %zu",
            clientsAckedMod.size(), connectedPlayers.size());
    
    // Check if all connected players have ACKed
    bool allReady = true;
    for(const auto& player : connectedPlayers) {
        if(clientsAckedMod.find(player) == clientsAckedMod.end()) {
            SDL_Log("HOST: Still waiting for ACK from '%s'", player.c_str());
            allReady = false;
        }
    }
    
    if(allReady) {
        std::string contentError;
        const auto content = pNetworkManager->checkRelayContent(getQuantBotConfig().getConfigHash(),
            getObjectDataHash(), VERSIONSTRING, contentError);
        if(content != NetworkManager::ContentCheck::Match) {
            if(content == NetworkManager::ContentCheck::Mismatch) onConfigMismatch(contentError);
            else addInfoMessage(contentError);
            return;
        }
        SDL_Log("HOST: All %zu clients ready! Starting game...", connectedPlayers.size());
        addInfoMessage("All clients synced - starting game!");
        bWaitingForModAcks = false;
        
        // Now actually start the game
        unsigned int timeLeft = startImmediately ? 1 : 3000;  // Lobby hosts keep their countdown.
        if(!pNetworkManager->sendStartGame(timeLeft)) {
            addInfoMessage("The match could not start because a player disconnected or was not ready.");
            return;
        }
        if(!pNetworkManager->isDirectSession()) startGameTime = SDL_GetTicks() + timeLeft;
        else addInfoMessage("Confirming the final players before starting...");
        disableAllDropDownBoxes();
        
        // Send Discord presence with game details
        updateDiscordGameStarting();
    }
}

void CustomGamePlayers::updateDiscordGameStarting() {
    // Build player details string: "Atreides: Player1, Harkonnen: AIBot, ..."
    std::string playerDetails;
    int playerCount = 0;
    
    for(int i = 0; i < numHouses; i++) {
        HouseInfo& curHouseInfo = houseInfo[i];
        
        int houseID = curHouseInfo.houseDropDown.getSelectedEntryIntData();
        int player1 = curHouseInfo.player1DropDown.getSelectedEntryIntData();
        int player2 = curHouseInfo.player2DropDown.getSelectedEntryIntData();
        
        // Skip if no players in this house
        if((player1 == PLAYER_OPEN || player1 == PLAYER_CLOSED) && 
           (player2 == PLAYER_OPEN || player2 == PLAYER_CLOSED)) {
            continue;
        }
        
        // Get house name
        std::string houseName;
        if(houseID == HOUSE_INVALID) {
            houseName = "Random";
        } else {
            houseName = getHouseNameByNumber((HOUSETYPE)houseID);
        }
        
        // Get player names
        std::vector<std::string> players;
        
        if(player1 != PLAYER_OPEN && player1 != PLAYER_CLOSED) {
            std::string name = curHouseInfo.player1DropDown.getSelectedEntry();
            players.push_back(name);
            playerCount++;
        }
        
        if(player2 != PLAYER_OPEN && player2 != PLAYER_CLOSED) {
            std::string name = curHouseInfo.player2DropDown.getSelectedEntry();
            players.push_back(name);
            playerCount++;
        }
        
        // Build house entry
        if(!players.empty()) {
            if(!playerDetails.empty()) {
                playerDetails += ", ";
            }
            playerDetails += houseName + ": ";
            for(size_t j = 0; j < players.size(); j++) {
                if(j > 0) playerDetails += "+";
                playerDetails += players[j];
            }
        }
    }
    
    // Get map name and mod name
    std::string mapName = getBasename(gameInitSettings.getFilename(), true);
    std::string modName = ModManager::instance().getActiveModName();
    
    // Update Discord Rich Presence
    DiscordManager::instance().setGameStarting(mapName, modName, playerDetails, playerCount);
    
    // Send game start notification to metaserver (which sends Discord webhook)
#ifndef __EMSCRIPTEN__
    if(pNetworkManager != nullptr) {
        MetaServerClient* metaServer = pNetworkManager->getMetaServerClient();
        if(metaServer != nullptr) {
            metaServer->announceGameStart(mapName, modName, playerDetails);
        }
    }
#endif
}

void CustomGamePlayers::onNext()
{
    if(!WorkshopGameContent::matches(gameInitSettings)) {
        onConfigMismatch(_("The selected mod revision changed. Reopen the lobby to share the updated version."));
        return;
    }
    if(setup && setup->online) {
        setup->players = getChangeEventList();
        quit(MENU_SETUP_HOST);
        return;
    }
    if(isCoopGameType(gameInitSettings.getGameType())
       && houseInfo[0].player1DropDown.getSelectedEntryIntData() != PLAYER_HUMAN) {
        openWindow(MsgBox::create(_("Choose a human player to lead the campaign.")));
        return;
    }
    // check if we have at least two houses on the map and if we have more than one team
    int numUsedHouses = 0;
    int numTeams = 0;
    bool houseAlreadyUsed[NUM_HOUSES] = {};
    bool colorAlreadyUsed[NUM_HOUSE_COLOR_SLOTS] = {};
    bool bDuplicateHouse = false;
    bool bDuplicateColor = false;

    for(int i=0;i<numHouses;i++) {
        HouseInfo& curHouseInfo = houseInfo[i];

        int currentPlayer1 = curHouseInfo.player1DropDown.getSelectedEntryIntData();
        int currentPlayer2 = curHouseInfo.player2DropDown.getSelectedEntryIntData();
        const bool bPlayer1Active = (currentPlayer1 != PLAYER_OPEN && currentPlayer1 != PLAYER_CLOSED);
        const bool bPlayer2Active = gameInitSettings.isMultiplePlayersPerHouse()
            && (currentPlayer2 != PLAYER_OPEN && currentPlayer2 != PLAYER_CLOSED);

        if(bPlayer1Active || bPlayer2Active) {
            numUsedHouses++;

            const int selectedHouse = curHouseInfo.houseDropDown.getSelectedEntryIntData();
            if(selectedHouse >= 0 && isCustomGameHouseAvailable(static_cast<HOUSETYPE>(selectedHouse))) {
                if(houseAlreadyUsed[selectedHouse]) {
                    bDuplicateHouse = true;
                } else {
                    houseAlreadyUsed[selectedHouse] = true;
                }
            }

            int selectedColor = curHouseInfo.colorDropDown.getSelectedEntryIntData();
            selectedColor = resolveSelectedColorSlot(selectedColor, selectedHouse);
            if(isValidHouseColorSlot(selectedColor)) {
                if(colorAlreadyUsed[selectedColor]) {
                    bDuplicateColor = true;
                } else {
                    colorAlreadyUsed[selectedColor] = true;
                }
            }

            int currentTeam = curHouseInfo.teamDropDown.getSelectedEntryIntData();
            bool bTeamFound = false;
            for(int j=0;j<i;j++) {
                int player1 = houseInfo[j].player1DropDown.getSelectedEntryIntData();
                int player2 = houseInfo[j].player2DropDown.getSelectedEntryIntData();

                const bool bPreviousPlayer2Active = gameInitSettings.isMultiplePlayersPerHouse()
                    && (player2 != PLAYER_OPEN && player2 != PLAYER_CLOSED);
                if((player1 != PLAYER_OPEN && player1 != PLAYER_CLOSED) || bPreviousPlayer2Active) {

                    int team = houseInfo[j].teamDropDown.getSelectedEntryIntData();
                    if(currentTeam == team) {
                        bTeamFound = true;
                    }
                }
            }

            if(bTeamFound == false) {
                numTeams++;
            }
        }
    }

    if(numUsedHouses < 2 && !isCoopGameType(gameInitSettings.getGameType())) {
        // No game possible with only 1 house
        openWindow(MsgBox::create(_("At least 2 houses must be controlled\nby a human player or an AI player!")));
    } else if(bDuplicateHouse) {
        openWindow(MsgBox::create(_("The same house cannot be used twice.")));
    } else if(bDuplicateColor) {
        openWindow(MsgBox::create(_("The same color cannot be used twice.")));
    } else if(numTeams < 2 && !isCoopGameType(gameInitSettings.getGameType())) {
        // No game possible with only 1 team
        openWindow(MsgBox::create(_("There must be at least two different teams!")));
    } else {
        // start game

        addAllPlayersToGameInitSettings();

        if(pNetworkManager != nullptr) {
            // Multiplayer game - send and verify config hashes
            QuantBotConfig& config = getQuantBotConfig();
            std::string quantBotHash = config.getConfigHash();
            std::string objectDataHash = getObjectDataHash();
            
            SDL_Log("==================== MULTIPLAYER CONFIG CHECK ====================");
            SDL_Log("Role: %s", pNetworkManager->isServer() ? "SERVER" : "CLIENT");
            SDL_Log("Config file locations:");
            SDL_Log("  QuantBot Config: %s", getQuantBotConfigFilepath().c_str());
            SDL_Log("  ObjectData.ini:  %s", getObjectDataFilepath().c_str());
            SDL_Log("");
            SDL_Log("Config being sent:");
            SDL_Log("  Game version: %s", VERSIONSTRING);
            SDL_Log("  QuantBot Config.ini hash: %s", quantBotHash.c_str());
            SDL_Log("  ObjectData.ini hash:      %s", objectDataHash.c_str());
            SDL_Log("==================================================================");
            
            // Send version and config hashes to all players for verification
            pNetworkManager->sendConfigHash(quantBotHash, objectDataHash, VERSIONSTRING);

            clientsAckedMod.clear();
            bWaitingForModAcks = true;

            if(pNetworkManager->supportsModTransfer()) {
                // Send mod info for mod sync
                std::string modName = ModManager::instance().getActiveModName();
                std::string modChecksum = ModManager::instance().getEffectiveChecksums().combined;
                SDL_Log("HOST: Sending mod info: mod='%s', checksum=%s", modName.c_str(), modChecksum.c_str());
                pNetworkManager->sendModInfo(modName, modChecksum);

                // Wait for all clients to acknowledge mod sync before starting
                // The actual game start will happen in checkAllClientsReady() after all ACKs
                addInfoMessage("Waiting for clients to sync mod...");
                SDL_Log("HOST: Waiting for mod ACKs from clients before starting game");
            } else {
                // Crossplay carries bundled content only: there is nothing to transfer and so
                // nothing to acknowledge. Matching content was required to be admitted to the
                // room, but that was checked against whatever was active *then* - and the lobby
                // lets the host pick a different mod afterwards. So compare again, now, against
                // what is actually about to be played, and refuse rather than report agreement
                // nobody established.
                std::string reason;
                const NetworkManager::ContentCheck check = pNetworkManager->checkRelayContent(
                    quantBotHash, objectDataHash, VERSIONSTRING, reason);
                if(check == NetworkManager::ContentCheck::Mismatch) {
                    bWaitingForModAcks = false;
                    onConfigMismatch(reason);
                    return;
                }
                if(check == NetworkManager::ContentCheck::AwaitingPeer) {
                    // Recoverable: a peer that has joined but whose content has not arrived yet.
                    // Say so and let the host try again rather than killing the lobby.
                    bWaitingForModAcks = false;
                    addInfoMessage(reason);
                    return;
                }

                // Matching content is not the only thing a direct match needs. Having a channel
                // to each guest says nothing about whether the guests reached each other, and a
                // match started across a missing guest-to-guest link loses that pair's commands
                // with no error anywhere. Also recoverable: the link usually completes a moment
                // later, so this asks the host to try again rather than ending the lobby.
                if(!pNetworkManager->isMeshReady()) {
                    bWaitingForModAcks = false;
                    const std::string blocked = pNetworkManager->getMeshBlockedReason();
                    addInfoMessage(blocked.empty()
                        ? std::string(_("Not every player is connected to every other player yet."))
                        : blocked);
                    return;
                }

                for(const std::string& peerName : pNetworkManager->getConnectedPeers()) {
                    clientsAckedMod.insert(peerName);
                }
                addInfoMessage(_("Everyone is using the same game content."));
            }

            // Don't start game yet - will be started when all clients ACK
            // For single-player or if no other clients, check immediately
            checkAllClientsReady();
        } else {
            startSinglePlayerGame(gameInitSettings);

            quit(MENU_QUIT_GAME_FINISHED);
        }
    }
}

void CustomGamePlayers::addAllPlayersToGameInitSettings()
{
    // The single choke point both start routes pass through, so this is where the local-only
    // observation choice is handed to the game. It is never serialized or sent to a peer.
    gameInitSettings.setLocalSpectator(spectating);
    gameInitSettings.clearHouseInfo();
    if(isCoopGameType(gameInitSettings.getGameType())) {
        for(const auto& enemy : fixedCoopHouses)
            if(enemy.houseID != gameInitSettings.getHouseID()) gameInitSettings.addHouseInfo(enemy);
    }

    for(int i=0;i<numHouses;i++) {
        HouseInfo& curHouseInfo = houseInfo[i];

        int houseID = curHouseInfo.houseDropDown.getSelectedEntryIntData();
        int team = curHouseInfo.teamDropDown.getSelectedEntryIntData();
        int colorOfHouse = curHouseInfo.colorDropDown.getSelectedEntryIntData();
        int player1 = curHouseInfo.player1DropDown.getSelectedEntryIntData();
        std::string player1name = curHouseInfo.player1DropDown.getSelectedEntry();
        int player2 = curHouseInfo.player2DropDown.getSelectedEntryIntData();
        std::string player2name = curHouseInfo.player2DropDown.getSelectedEntry();

        if(isCoopGameType(gameInitSettings.getGameType())) {
            houseID = gameInitSettings.getHouseID();
            team = 1;
            for(const auto& fixed : fixedCoopHouses)
                if(fixed.houseID == houseID) colorOfHouse = fixed.colorOfHouse;
        }
        GameInitSettings::HouseInfo newHouseInfo((HOUSETYPE) houseID, team);
        if(duneCitySkinControls) {
            newHouseInfo.graphicsSkin = GameInitSettings::sanitizeGraphicsSkin(
                curHouseInfo.graphicsSkinDropDown.getSelectedEntryIntData());
        }
        // Row order is what matters: houseID may still be HOUSE_INVALID here, and INIMapLoader
        // carries this factor onto whichever house the Random row resolves to.
        const int selectedFactor = curHouseInfo.spiceIncomeDropDown.getSelectedEntryIntData();
        newHouseInfo.spiceIncomeMultiplier = SpiceIncome::isValid(static_cast<Uint32>(selectedFactor))
            ? static_cast<Uint32>(selectedFactor) : SpiceIncome::kDefault;
        colorOfHouse = resolveSelectedColorSlot(colorOfHouse, houseID);
        if(isValidHouseColorSlot(colorOfHouse)) {
            newHouseInfo.colorOfHouse = colorOfHouse;
        }

        bool bAdded = false;
        bAdded |= addPlayerToHouseInfo(newHouseInfo, player1, player1name);
        if(gameInitSettings.isMultiplePlayersPerHouse()) {
            bAdded |= addPlayerToHouseInfo(newHouseInfo, player2, player2name);
        }

        if(bAdded == true) {
            gameInitSettings.addHouseInfo(newHouseInfo);
        }
    }
}

bool CustomGamePlayers::addPlayerToHouseInfo(GameInitSettings::HouseInfo& newHouseInfo, int player, const std::string& playername)
{
    std::string playerName;
    std::string playerClass;

    HOUSETYPE houseID = newHouseInfo.houseID;

    switch(player) {
        case PLAYER_HUMAN: {
            playerName = playername;
            playerClass = HUMANPLAYERCLASS;
        } break;

        case PLAYER_OPEN:
        case PLAYER_CLOSED: {
            return false;
        } break;


        default: {
            // AI player
            const PlayerFactory::PlayerData* pPlayerData = PlayerFactory::getByIndex(player);
            if(pPlayerData == nullptr) {
                return false;
            }

            playerName = (houseID == HOUSE_INVALID) ? pPlayerData->getName() : getHouseNameByNumber(houseID);
            playerClass = pPlayerData->getPlayerClass();

        } break;
    }

    newHouseInfo.addPlayerInfo(GameInitSettings::PlayerInfo(playerName, playerClass));
    return true;
}

void CustomGamePlayers::onCancel()
{
    if(setup) {
        setup->players = getChangeEventList();
        quit(MENU_SETUP_MAP);
        return;
    }
    quit();
}

void CustomGamePlayers::onSendChatMessage()
{
    std::string message = chatTextBox.getText();
    addChatMessage(settings.general.playerName, message);
    chatTextBox.setText("");

    pNetworkManager->sendChatMessage(message);

}

void CustomGamePlayers::addInfoMessage(const std::string& message) {
    if(startImmediately) launchStatus.setText(message);
    std::string text = chatTextView.getText();
    if(text.length() > 0) {
        text += "\n";
    }
    text += "* " + message;
    chatTextView.setText(text);
    chatTextView.scrollToEnd();
}

void CustomGamePlayers::addChatMessage(const std::string& name, const std::string& message)
{
    std::string text = chatTextView.getText();
    if(text.length() > 0) {
        text += "\n";
    }
    text += name + ": " + message;
    chatTextView.setText(text);
    chatTextView.scrollToEnd();
}

void CustomGamePlayers::extractMapInfo(INIFile* pMap)
{
    int sizeX = 0;
    int sizeY = 0;

    if(pMap->hasKey("MAP","Seed")) {
        // old map format with seed value
        int mapscale = pMap->getIntValue("BASIC", "MapScale", -1);

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
        sizeX = pMap->getIntValue("MAP","SizeX", 0);
        sizeY = pMap->getIntValue("MAP","SizeY", 0);
    }

    mapPropertySize.setText(std::to_string(sizeX) + " x " + std::to_string(sizeY));

    sdl2::surface_ptr pMapSurface = nullptr;
    try {
        INIMapPreviewCreator mapPreviewCreator(pMap);
        pMapSurface = mapPreviewCreator.createMinimapImageOfMap(1, DuneStyle::buttonBorderColor);
    } catch(...) {
        pMapSurface = sdl2::surface_ptr{ GUIStyle::getInstance().createButtonSurface(130, 130, "Error", true, false) };
        nextButton.setEnabled(false);
    }
    minimap.setSurface(std::move(pMapSurface));


    boundHousesOnMap.clear();
    for(int h = 0; h < getCustomGameHouseCount(); h++) {
        const HOUSETYPE house = static_cast<HOUSETYPE>(h);
        if(pMap->hasSection(getHouseNameByNumber(house))) {
            boundHousesOnMap.push_back(house);
        }
    }

    const int boundHouseCount = static_cast<int>(boundHousesOnMap.size());
    const int numberedPlayerCount = MapPlayerSectionUtils::countNumberedPlayerSections(
        getCustomGameHouseCount(),
        [&pMap](int playerNumber) {
            return pMap->hasSection("Player" + std::to_string(playerNumber));
        });
    numHouses = std::min(boundHouseCount + numberedPlayerCount, MAX_CUSTOM_GAME_PLAYERS);

    mapPropertyPlayers.setText(std::to_string(numHouses));

    std::string authors = pMap->getStringValue("BASIC","Author", "-");
    if(authors.size() > 11) {
        authors = authors.substr(0,9) + "...";
    }
    mapPropertyAuthors.setText(authors);


    mapPropertyLicense.setText(pMap->getStringValue("BASIC","License", "-"));

    // find Brain=Human
    int currentIndex = 0;
    for(const HOUSETYPE& houseType : boundHousesOnMap) {
        if(strToUpper(pMap->getStringValue(getHouseNameByNumber(houseType),"Brain","")) == "HUMAN") {
            brainEqHumanSlot = currentIndex;
        }
        currentIndex++;
    }

    currentIndex = 0;
    int currentTeam = 0;
    std::vector<std::string> teamNames;
    for(const HOUSETYPE& houseType : boundHousesOnMap) {
        std::string teamName = strToUpper(pMap->getStringValue(getHouseNameByNumber(houseType),"Brain","Team " + std::to_string(currentIndex+1)));
        teamNames.push_back(teamName);
        slotToTeam[currentIndex] = currentTeam;
        currentTeam++;

        // let's see if we can reuse an old team number
        for(unsigned int i = 0; i < teamNames.size()-1; i++) {
            if(teamNames[i] == teamName) {
                slotToTeam[currentIndex] = slotToTeam[i];
                currentTeam--;
                break;
            }
        }

        currentIndex++;
    }

    // PlayerN sections may have gaps (Ergsun-Odenkirk uses 1, 2, 3, 5).
    // Scan the same capacity used to count them, not just the visible row count.
    for(int p = 0; (p < getCustomGameHouseCount()) && (currentIndex < numHouses); p++) {
        if(pMap->hasSection("Player" + std::to_string(p+1))) {
            std::string teamName = strToUpper(pMap->getStringValue("Player" + std::to_string(p+1),"Brain","Team " + std::to_string(currentIndex+p+1)));
            teamNames.push_back(teamName);
            slotToTeam[currentIndex] = currentTeam;
            currentTeam++;

            // let's see if we can reuse an old team number
            for(unsigned int i = 0; i < teamNames.size()-1; i++) {
                if(teamNames[i] == teamName) {
                    slotToTeam[currentIndex] = slotToTeam[i];
                    currentTeam--;
                    break;
                }
            }

            currentIndex++;
        }
    }

    for(;currentIndex < numHouses; currentIndex++) {
        slotToTeam[currentIndex] = -1;
    }
}

void CustomGamePlayers::onChangeHousesDropDownBoxes(bool bInteractive, int houseInfoNum) {
    if(bInteractive && houseInfoNum >= 0 && houseInfoNum < numHouses) {
        HouseInfo& changedHouseInfo = houseInfo[houseInfoNum];
        addColorDropDownEntries(changedHouseInfo.colorDropDown, HOUSE_INVALID,
                                changedHouseInfo.bonusColorCheckbox.isChecked());
    }

    if(bInteractive && houseInfoNum >= 0 && pNetworkManager != nullptr) {
        int selectedHouseID = houseInfo[houseInfoNum].houseDropDown.getSelectedEntryIntData();

        ChangeEventList changeEventList;
        changeEventList.changeEventList.emplace_back(ChangeEventList::ChangeEvent::EventType::ChangeHouse, houseInfoNum, selectedHouseID);
        changeEventList.changeEventList.emplace_back(ChangeEventList::ChangeEvent::EventType::ChangeColor, houseInfoNum,
                                                     HOUSE_INVALID);

        pNetworkManager->sendChangeEventList(changeEventList);
    }

    int numBoundHouses = boundHousesOnMap.size();
    int numUsedBoundHouses = 0;
    int numUsedRandomHouses = 0;
    for(int i=0;i<numHouses;i++) {
        int selectedHouseID = houseInfo[i].houseDropDown.getSelectedEntryIntData();

        if(selectedHouseID == HOUSE_INVALID) {
            numUsedRandomHouses++;
        } else {
            if(isBoundedHouseOnMap((HOUSETYPE) selectedHouseID)) {
                numUsedBoundHouses++;
            }
        }
    }


    for(int i=0;i<numHouses;i++) {
        HouseInfo& curHouseInfo = houseInfo[i];

        int house = curHouseInfo.houseDropDown.getSelectedEntryIntData();

        if(houseInfoNum == -1 || houseInfoNum == i) {

            int colorHouse = curHouseInfo.colorDropDown.getSelectedEntryIntData();
            colorHouse = resolveSelectedColorSlot(colorHouse, house);
            Uint32 color = getMenuColorForHouse(colorHouse);
            curHouseInfo.houseLabel.setTextColor(color);
            curHouseInfo.houseDropDown.setColor(color);
            curHouseInfo.teamDropDown.setColor(color);
            curHouseInfo.colorDropDown.setColor(color);
            curHouseInfo.player1Label.setTextColor(color);
            curHouseInfo.player1DropDown.setColor(color);
            curHouseInfo.player2Label.setTextColor(color);
            curHouseInfo.player2DropDown.setColor(color);

            if(house == HOUSE_INVALID) {
                curHouseInfo.player1ArrowLabel.setTexture(pGFXManager->getUIGraphic(UI_CustomGamePlayersArrowNeutral));
            } else {
                curHouseInfo.player1ArrowLabel.setTexture(pGFXManager->getUIGraphic(UI_CustomGamePlayersArrow, house));
            }
        }

        if(gameInitSettings.getGameType() == GameType::LoadMultiplayer || isCoopGameType(gameInitSettings.getGameType())) {
            // no house changes possible
            continue;
        }

        addToHouseDropDown(curHouseInfo.houseDropDown, HOUSE_INVALID);

        for(int h=0;h<getCustomGameHouseCount();h++) {
            bool bAddHouse;

            bool bCheck;

            if((house == HOUSE_INVALID) || (isBoundedHouseOnMap((HOUSETYPE) house))) {
                if(numUsedBoundHouses + numUsedRandomHouses - 1 >= numBoundHouses) {
                    bCheck = true;
                } else {
                    bCheck = false;
                }
            } else {
                if(numUsedBoundHouses + numUsedRandomHouses >= numBoundHouses) {
                    bCheck = true;
                } else {
                    bCheck = false;
                }
            }

            if(bCheck == true) {
                bAddHouse = true;
                for(int j = 0; j < numHouses; j++) {
                    if(i != j) {
                        DropDownBox& tmpDropDown = houseInfo[j].houseDropDown;
                        if(tmpDropDown.getSelectedEntryIntData() == h) {
                            bAddHouse = false;
                            break;
                        }
                    }
                }
            } else {
                bAddHouse = (h == house);

                if(((house == HOUSE_INVALID) || (isBoundedHouseOnMap((HOUSETYPE) house))) && isBoundedHouseOnMap((HOUSETYPE) h)) {
                    // check if this entry is random or a bounded house but is needed for a bounded house
                    bAddHouse = true;
                    for(int j = 0; j < numHouses; j++) {
                        if(i != j) {
                            DropDownBox& tmpDropDown = houseInfo[j].houseDropDown;
                            if(tmpDropDown.getSelectedEntryIntData() == h) {
                                bAddHouse = false;
                                break;
                            }
                        }
                    }
                }
            }

            if(bAddHouse == true) {
                addToHouseDropDown(curHouseInfo.houseDropDown, h);
            } else {
                removeFromHouseDropDown(curHouseInfo.houseDropDown, h);
            }
        }
    }
}

void CustomGamePlayers::onChangeTeamDropDownBoxes(bool bInteractive, int houseInfoNum) {

    if(bInteractive && houseInfoNum >= 0 && pNetworkManager != nullptr) {
        int selectedTeam = houseInfo[houseInfoNum].teamDropDown.getSelectedEntryIntData();

        ChangeEventList changeEventList;
        changeEventList.changeEventList.emplace_back(ChangeEventList::ChangeEvent::EventType::ChangeTeam, houseInfoNum, selectedTeam);

        pNetworkManager->sendChangeEventList(changeEventList);
    }
}

void CustomGamePlayers::onChangeColorDropDownBoxes(bool bInteractive, int houseInfoNum) {
    if(houseInfoNum >= 0) {
        onChangeHousesDropDownBoxes(false, houseInfoNum);
    }

    if(bInteractive && houseInfoNum >= 0 && pNetworkManager != nullptr) {
        int selectedColor = houseInfo[houseInfoNum].colorDropDown.getSelectedEntryIntData();

        ChangeEventList changeEventList;
        changeEventList.changeEventList.emplace_back(ChangeEventList::ChangeEvent::EventType::ChangeColor, houseInfoNum, selectedColor);

        pNetworkManager->sendChangeEventList(changeEventList);
    }
}

void CustomGamePlayers::onChangeGraphicsSkinDropDownBoxes(bool bInteractive, int houseInfoNum) {
    if(!duneCitySkinControls || houseInfoNum < 0 || houseInfoNum >= numHouses) {
        return;
    }
    if(bInteractive && pNetworkManager != nullptr) {
        const int selectedSkin = houseInfo[houseInfoNum].graphicsSkinDropDown.getSelectedEntryIntData();
        ChangeEventList changeEventList;
        changeEventList.changeEventList.emplace_back(
            ChangeEventList::ChangeEvent::EventType::ChangeGraphicsSkin,
            houseInfoNum, selectedSkin);
        pNetworkManager->sendChangeEventList(changeEventList);
    }
}

void CustomGamePlayers::onChangeSpiceIncomeDropDownBoxes(bool bInteractive, int houseInfoNum) {
    if(houseInfoNum < 0 || houseInfoNum >= numHouses) {
        return;
    }
    // Only the host ever originates this. A client's selector is disabled, so reaching here
    // interactively on a client would mean the widget state and the authority rule disagree;
    // sending anyway would just earn a refusal and a log line on the host.
    if(bInteractive && bServer && pNetworkManager != nullptr) {
        const int selectedFactor = houseInfo[houseInfoNum].spiceIncomeDropDown.getSelectedEntryIntData();
        ChangeEventList changeEventList;
        changeEventList.changeEventList.emplace_back(
            ChangeEventList::ChangeEvent::EventType::ChangeSpiceIncome,
            houseInfoNum, static_cast<Uint32>(selectedFactor));
        pNetworkManager->sendChangeEventList(changeEventList);
    }
}

void CustomGamePlayers::onBonusColorCheckbox(int houseInfoNum) {
    if(houseInfoNum < 0 || houseInfoNum >= numHouses) {
        return;
    }

    HouseInfo& curHouseInfo = houseInfo[houseInfoNum];
    const int oldColor = curHouseInfo.colorDropDown.getSelectedEntryIntData();
    addColorDropDownEntries(curHouseInfo.colorDropDown, oldColor, curHouseInfo.bonusColorCheckbox.isChecked());
    onChangeColorDropDownBoxes(true, houseInfoNum);
}

void CustomGamePlayers::onChangePlayerDropDownBoxes(bool bInteractive, int boxnum) {
    if(bInteractive && (boxnum < 0 || boxnum >= numHouses * 2)) return;
    if(bInteractive && pNetworkManager != nullptr
       && !LobbyAuthorization::mayConfigurePlayerSlot(editableSeats, settings.general.playerName,
                                                       static_cast<Uint32>(boxnum), bServer)) {
        auto& box = boxnum % 2 == 0 ? houseInfo[boxnum / 2].player1DropDown
                                   : houseInfo[boxnum / 2].player2DropDown;
        const int previous = lastPlayerSelections[boxnum];
        for(int i = 0; i < box.getNumEntries(); ++i) {
            if(box.getEntryIntData(i) == previous) { box.setSelectedItem(i); break; }
        }
        return;
    }
    if(bInteractive && boxnum >= 0 && pNetworkManager != nullptr) {
        DropDownBox& dropDownBox = (boxnum % 2 == 0) ? houseInfo[boxnum / 2].player1DropDown : houseInfo[boxnum / 2].player2DropDown;

        int selectedPlayer = dropDownBox.getSelectedEntryIntData();

        ChangeEventList changeEventList;
        changeEventList.changeEventList.emplace_back(ChangeEventList::ChangeEvent::EventType::ChangePlayer, boxnum, selectedPlayer);

        pNetworkManager->sendChangeEventList(changeEventList);
    }

    checkPlayerBoxes();
}

void CustomGamePlayers::onClickPlayerDropDownBox(int boxnum) {
    if(boxnum < 0 || boxnum >= numHouses * 2) return;
    if(pNetworkManager != nullptr && LobbyAuthorization::authorizeClientEvent(
        makeSeatSnapshot(), settings.general.playerName,
        ChangeEventList::ChangeEvent(boxnum, settings.general.playerName)) != LobbyAuthorization::Decision::Allow) return;
    DropDownBox& dropDownBox = (boxnum % 2 == 0) ? houseInfo[boxnum / 2].player1DropDown : houseInfo[boxnum / 2].player2DropDown;

    if(dropDownBox.getSelectedEntryIntData() == PLAYER_CLOSED) {
        return;
    }

    setPlayer2Slot(settings.general.playerName, boxnum);

    if(boxnum >= 0 && pNetworkManager != nullptr) {
        ChangeEventList changeEventList;
        changeEventList.changeEventList.emplace_back(boxnum, settings.general.playerName);

        pNetworkManager->sendChangeEventList(changeEventList);
    }
}

void CustomGamePlayers::onPeerDisconnected(const std::string& playername, bool bHost, int cause) {
    if(bHost) {
        quit(cause);
    } else {
        for(int i=0;i<numHouses*2;i++) {
            DropDownBox& curDropDownBox = (i % 2 == 0) ? houseInfo[i / 2].player1DropDown : houseInfo[i / 2].player2DropDown;

            if(curDropDownBox.getSelectedEntryIntData() == PLAYER_HUMAN && curDropDownBox.getSelectedEntry() == playername) {

                curDropDownBox.clearAllEntries();
                curDropDownBox.addEntry(_("open"), PLAYER_OPEN);
                curDropDownBox.setSelectedItem(0);

                if(gameInitSettings.getGameType() != GameType::LoadMultiplayer) {
                    curDropDownBox.addEntry(_("closed"), PLAYER_CLOSED);
                    for(unsigned int k = 1; k < PlayerFactory::getList().size(); k++) {
                        const PlayerFactory::PlayerData* playerData = PlayerFactory::getByIndex(k);
                        if(playerData == nullptr) {
                            continue;
                        }
                        const std::string& playerClass = playerData->getPlayerClass();
                        if(playerClass.rfind("qBotSupport", 0) == 0 &&
                           playerClass != "qBotSupportEasy" &&
                           playerClass != "qBotSupportMedium" &&
                           playerClass != "qBotSupportHard" &&
                           playerClass != "qBotSupportBrutal") {
                            continue;
                        }
                        curDropDownBox.addEntry(playerData->getName(), k);
                    }
                }

                break;
            }
        }

        if(bServer == false) {
            for(int i=0;i<numHouses;i++) {
                bool bIsThisPlayer = false;
                if(houseInfo[i].player1DropDown.getSelectedEntryIntData() == PLAYER_HUMAN) {
                    if(houseInfo[i].player1DropDown.getSelectedEntry() == settings.general.playerName) {
                        bIsThisPlayer = true;
                    }
                }

                if(houseInfo[i].player2DropDown.getSelectedEntryIntData() == PLAYER_HUMAN) {
                    if(houseInfo[i].player2DropDown.getSelectedEntry() == settings.general.playerName) {
                        bIsThisPlayer = true;
                    }
                }

                houseInfo[i].player1DropDown.setEnabled(bIsThisPlayer);
                houseInfo[i].player2DropDown.setEnabled(bIsThisPlayer);
                houseInfo[i].houseDropDown.setEnabled(bIsThisPlayer);
                houseInfo[i].teamDropDown.setEnabled(bIsThisPlayer);
                houseInfo[i].bonusColorCheckbox.setEnabled(false);
                houseInfo[i].colorDropDown.setEnabled(false);
            }
        }

        checkPlayerBoxes();

        addInfoMessage(playername + " disconnected!");
        
        // Update Discord presence when player count changes
        updateDiscordLobbyPresence();
    }
}

void CustomGamePlayers::onStartGame(unsigned int timeLeft) {
    if(bModDownloadInProgress) return;
    if(!WorkshopGameContent::matches(gameInitSettings)) {
        onConfigMismatch(_("The required mod revision changed before the game started."));
        return;
    }
    // Don't start if there was a config mismatch
    if(bConfigMismatchDetected) {
        SDL_Log("Ignoring STARTGAME packet - config mismatch already detected");
        return;
    }
    
    startGameTime = SDL_GetTicks() + timeLeft;
    disableAllDropDownBoxes();
    
    // Update Discord presence with game starting details (client side)
    updateDiscordGameStarting();
}

void CustomGamePlayers::setPlayer2Slot(const std::string& playername, int slot) {
    // While watching, no faction may be handed a human controller. This is the single place a
    // human name is seated, so refusing here also covers a stale SetHumanPlayer event replayed
    // from a roster that was captured before Spectate was switched on.
    if(spectating) return;
    DropDownBox& dropDownBox = (slot % 2 == 0) ? houseInfo[slot / 2].player1DropDown : houseInfo[slot / 2].player2DropDown;

    std::string oldPlayerName = "";
    int oldPlayerType = dropDownBox.getSelectedEntryIntData();
    if(oldPlayerType == PLAYER_HUMAN) {
        oldPlayerName = dropDownBox.getSelectedEntry();
    }

    for(int i=0;i<numHouses*2;i++) {
        DropDownBox& curDropDownBox = (i % 2 == 0) ? houseInfo[i / 2].player1DropDown : houseInfo[i / 2].player2DropDown;

        if(curDropDownBox.getSelectedEntryIntData() == PLAYER_HUMAN && curDropDownBox.getSelectedEntry() == playername) {

            if(oldPlayerType == PLAYER_HUMAN) {
                curDropDownBox.clearAllEntries();
                curDropDownBox.addEntry(oldPlayerName, PLAYER_HUMAN);
                curDropDownBox.setSelectedItem(0);
            } else {
                curDropDownBox.clearAllEntries();
                curDropDownBox.addEntry(_("open"), PLAYER_OPEN);

                if(gameInitSettings.getGameType() != GameType::LoadMultiplayer) {
                    curDropDownBox.addEntry(_("closed"), PLAYER_CLOSED);
                    for(unsigned int k = 1; k < PlayerFactory::getList().size(); k++) {
                        const PlayerFactory::PlayerData* playerData = PlayerFactory::getByIndex(k);
                        if(playerData == nullptr) {
                            continue;
                        }
                        const std::string& playerClass = playerData->getPlayerClass();
                        if(playerClass.rfind("qBotSupport", 0) == 0 &&
                           playerClass != "qBotSupportEasy" &&
                           playerClass != "qBotSupportMedium" &&
                           playerClass != "qBotSupportHard" &&
                           playerClass != "qBotSupportBrutal") {
                            continue;
                        }
                        curDropDownBox.addEntry(playerData->getName(), k);
                    }
                }

                for(int j = 0; j < curDropDownBox.getNumEntries(); j++) {
                    if(curDropDownBox.getEntryIntData(j) == oldPlayerType) {
                        curDropDownBox.setSelectedItem(j);
                        break;
                    }
                }
            }

            break;
        }
    }

    dropDownBox.clearAllEntries();
    dropDownBox.addEntry(playername, PLAYER_HUMAN);
    dropDownBox.setSelectedItem(0);

    if(bServer == false) {
        for(int i=0;i<numHouses;i++) {
            bool bIsThisPlayer = false;

            if(gameInitSettings.getGameType() != GameType::LoadMultiplayer) {
                if(houseInfo[i].player1DropDown.getSelectedEntryIntData() == PLAYER_HUMAN) {
                    if(houseInfo[i].player1DropDown.getSelectedEntry() == settings.general.playerName) {
                        bIsThisPlayer = true;
                    }
                }

                if(houseInfo[i].player2DropDown.getSelectedEntryIntData() == PLAYER_HUMAN) {
                    if(houseInfo[i].player2DropDown.getSelectedEntry() == settings.general.playerName) {
                        bIsThisPlayer = true;
                    }
                }
            }

            houseInfo[i].player1DropDown.setEnabled(bIsThisPlayer);
            houseInfo[i].player2DropDown.setEnabled(bIsThisPlayer);
            houseInfo[i].houseDropDown.setEnabled(bIsThisPlayer);
            houseInfo[i].teamDropDown.setEnabled(bIsThisPlayer);
            houseInfo[i].bonusColorCheckbox.setEnabled(false);
            houseInfo[i].colorDropDown.setEnabled(false);
            houseInfo[i].graphicsSkinDropDown.setEnabled(duneCitySkinControls && bIsThisPlayer);
            // Not bIsThisPlayer: a client never edits the spice income, not even for the row
            // it occupies. It still sees the host's choice, because the host broadcasts it.
            houseInfo[i].spiceIncomeDropDown.setEnabled(false);
        }
    }

    checkPlayerBoxes();
}

void CustomGamePlayers::checkPlayerBoxes() {
    if(isCoopGameType(gameInitSettings.getGameType())) {
        auto& shared = houseInfo[0];
        shared.houseDropDown.setEnabled(false);
        shared.teamDropDown.setEnabled(false);
        shared.bonusColorCheckbox.setEnabled(false);
        shared.colorDropDown.setEnabled(false);
        shared.spiceIncomeDropDown.setEnabled(false);
    }
    int numPlayers = 0;

    for(int i=0;i<numHouses;i++) {
        HouseInfo& curHouseInfo = houseInfo[i];

        int player1 = curHouseInfo.player1DropDown.getSelectedEntryIntData();
        int player2 = curHouseInfo.player2DropDown.getSelectedEntryIntData();

        if(player1 != PLAYER_OPEN) {
            numPlayers++;
        }

        if(gameInitSettings.isMultiplePlayersPerHouse() && player2 != PLAYER_OPEN) {
            numPlayers++;
        }

        if((gameInitSettings.isMultiplePlayersPerHouse() == false) || (player1 == PLAYER_OPEN && player2 == PLAYER_OPEN) || (curHouseInfo.player2DropDown.getNumEntries() == 0)) {
            curHouseInfo.player2DropDown.setVisible(false);
            curHouseInfo.player2DropDown.setEnabled(false);
            curHouseInfo.player2Label.setVisible(false);
        } else {
            curHouseInfo.player2DropDown.setVisible(true);

            bool bEnableDropDown2 = bServer;
            if(bServer == false) {
                if(player1 == PLAYER_HUMAN) {
                    if(curHouseInfo.player1DropDown.getSelectedEntry() == settings.general.playerName) {
                        bEnableDropDown2 = true;
                    }
                }

                if(player2 == PLAYER_HUMAN) {
                    if(curHouseInfo.player2DropDown.getSelectedEntry() == settings.general.playerName) {
                        bEnableDropDown2 = true;
                    }
                }
            }

            if((gameInitSettings.getGameType() == GameType::LoadMultiplayer) && (player2 != PLAYER_OPEN) && (player2 != PLAYER_HUMAN)) {
                curHouseInfo.player2DropDown.setEnabled(false);
            } else {
                curHouseInfo.player2DropDown.setEnabled(bEnableDropDown2);
            }
            curHouseInfo.player2Label.setVisible(true);
        }
    }

    editableSeats = makeSeatSnapshot();
    for(int slot = 0; slot < numHouses * 2; ++slot) {
        auto& box = slot % 2 == 0 ? houseInfo[slot / 2].player1DropDown
                                : houseInfo[slot / 2].player2DropDown;
        lastPlayerSelections[slot] = box.getSelectedEntryIntData();
        if(pNetworkManager != nullptr) {
            const bool editable = LobbyAuthorization::mayConfigurePlayerSlot(
                editableSeats, settings.general.playerName, slot, bServer);
            const bool loadedBot = gameInitSettings.getGameType() == GameType::LoadMultiplayer
                && editableSeats.slots[slot].kind == LobbyAuthorization::SlotKind::AI;
            box.setEnabled(editable && !loadedBot && box.isVisible());
            // Disabled dropdowns have a separate click-to-claim path. Protect that path too,
            // so clicking another player's support bot cannot silently move/swap the human.
            box.setOnClickEnabled(LobbyAuthorization::authorizeClientEvent(editableSeats,
                settings.general.playerName, ChangeEventList::ChangeEvent(slot, settings.general.playerName))
                == LobbyAuthorization::Decision::Allow);
        }
    }

    if(pNetworkManager != nullptr) {
        if(bServer) {
            pNetworkManager->updateServer(numPlayers);
        }
    }
}


void CustomGamePlayers::addToHouseDropDown(DropDownBox& houseDropDownBox, int house, bool bSelect) {

    if(houseDropDownBox.getNumEntries() == 0) {
        if(house == HOUSE_INVALID) {
            houseDropDownBox.addEntry(_("Random"), HOUSE_INVALID);
        } else {
            houseDropDownBox.addEntry(getHouseNameByNumber((HOUSETYPE) house), house);
        }

        if(bSelect) {
            houseDropDownBox.setSelectedItem(0);
        }
    } else {

        if(house == HOUSE_INVALID) {
            if(houseDropDownBox.getEntryIntData(0) != HOUSE_INVALID) {
                houseDropDownBox.insertEntry(0, _("Random"), HOUSE_INVALID);
            }

            if(bSelect) {
                houseDropDownBox.setSelectedItem(houseDropDownBox.getNumEntries()-1);
            }
        } else {

            int currentItemIndex = (houseDropDownBox.getEntryIntData(0) == HOUSE_INVALID) ? 1 : 0;

            for(int h = 0; h < getCustomGameHouseCount(); h++) {
                if(currentItemIndex < houseDropDownBox.getNumEntries() && houseDropDownBox.getEntryIntData(currentItemIndex) == h) {
                    if(h == house) {
                        if(bSelect) {
                            houseDropDownBox.setSelectedItem(currentItemIndex);
                        }
                        break;
                    }

                    currentItemIndex++;
                } else {
                    if(h == house) {
                        houseDropDownBox.insertEntry(currentItemIndex, getHouseNameByNumber((HOUSETYPE) h), h);

                        if(bSelect) {
                            houseDropDownBox.setSelectedItem(currentItemIndex);
                        }
                        break;
                    }
                }


            }
        }
    }
}

void CustomGamePlayers::removeFromHouseDropDown(DropDownBox& houseDropDownBox, int house) {

    for(int i=0;i<houseDropDownBox.getNumEntries(); i++) {
        if(houseDropDownBox.getEntryIntData(i) == house) {
            houseDropDownBox.removeEntry(i);
            break;
        }
    }
}

bool CustomGamePlayers::isBoundedHouseOnMap(HOUSETYPE houseID) {
    return (std::find(boundHousesOnMap.begin(), boundHousesOnMap.end(), houseID) != boundHousesOnMap.end());
}

void CustomGamePlayers::disableAllDropDownBoxes() {
    for(int i=0;i<numHouses;i++) {
        HouseInfo& curHouseInfo = houseInfo[i];

        curHouseInfo.houseDropDown.setEnabled(false);
        curHouseInfo.houseDropDown.setOnClickEnabled(false);
        curHouseInfo.teamDropDown.setEnabled(false);
        curHouseInfo.teamDropDown.setOnClickEnabled(false);
        curHouseInfo.bonusColorCheckbox.setEnabled(false);
        curHouseInfo.colorDropDown.setEnabled(false);
        curHouseInfo.colorDropDown.setOnClickEnabled(false);
        curHouseInfo.graphicsSkinDropDown.setEnabled(false);
        curHouseInfo.graphicsSkinDropDown.setOnClickEnabled(false);
        curHouseInfo.spiceIncomeDropDown.setEnabled(false);
        curHouseInfo.spiceIncomeDropDown.setOnClickEnabled(false);
        curHouseInfo.player1DropDown.setEnabled(false);
        curHouseInfo.player1DropDown.setOnClickEnabled(false);
        curHouseInfo.player2DropDown.setEnabled(false);
        curHouseInfo.player2DropDown.setOnClickEnabled(false);
    }

    backButton.setEnabled(false);
    nextButton.setEnabled(false);
}
