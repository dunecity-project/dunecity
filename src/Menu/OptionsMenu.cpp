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

#include <Menu/OptionsMenu.h>
#include <misc/WebRuntime.h>

#include <globals.h>

#include <GUI/Label.h>
#include <GUI/Spacer.h>
#include <GUI/dune/GameOptionsWindow.h>
#include <GUI/MsgBox.h>

#include <main.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/INIFile.h>
#include <mod/ModManager.h>
#include <FileClasses/music/MusicPlayer.h>

#include <SoundPlayer.h>

#include <players/PlayerFactory.h>

#include <misc/Scaler.h>
#include <misc/FileSystem.h>
#include <misc/format.h>
#include <misc/MenuPalette.h>
#include <misc/MenuLayout.h>

#include <algorithm>

int OptionsMenu::lastPage = 1;

OptionsMenu::OptionsMenu() : MenuBase()
{
    determineAvailableScreenResolutions();

    std::list<std::string> languagesList = getFileNamesList(getDuneLegacyDataDir() + "/locale", "po", true, FileListOrder_Name_Asc);
    if(languagesList.empty()) {
        languagesList = getFileNamesList(getDuneLegacyDataDir() + "/data/locale", "po", true, FileListOrder_Name_Asc);
    }
    availLanguages = std::vector<std::string>(languagesList.begin(), languagesList.end());

    currentGameOptions = effectiveGameOptions;  // Use mod-aware effective options

    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));

    setWindowWidget(&windowWidget);

    const int left = (getSize().x - 540) / 2;
    title.setText(_("SETTINGS"));
    title.setTextFontSize(22);
    title.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&title, Point(left, 18), Point(540, 30));
    const char* tabNames[] = {"General", "Graphics", "Audio", "Controls", "Advanced"};
    for(int page = 0; page < 5; ++page) {
        pageButtons[page].setText(_(tabNames[page]));
        pageButtons[page].setToggleButton(true);
        pageButtons[page].setOnClick([this, page] { showPage(page); });
        windowWidget.addWidget(&pageButtons[page], Point(left + page * 108, 58), Point(104, 36));
        windowWidget.addWidget(&pages[page], Point(left, 110), Point(540, getSize().y - 192));
    }
    auto optionLabel = [](const std::string& text) {
        auto* label = Label::create(text);
        label->setAlignment(static_cast<Alignment_Enum>(Alignment_Left | Alignment_VCenter));
        return label;
    };

    NameHBox.addWidget(Spacer::create(), 0.5);
    NameHBox.addWidget(optionLabel(_("Player Name")), 190);
    nameTextBox.setMaximumTextLength(MAX_PLAYERNAMELENGHT);
    nameTextBox.setOnTextChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    NameHBox.addWidget(&nameTextBox, 290);
    NameHBox.addWidget(Spacer::create(), 0.5);
    nameTextBox.setText(settings.general.playerName);

    pages[0].addWidget(&NameHBox, 32);
    pages[0].addWidget(VSpacer::create(6));

    gameOptionsHBox.addWidget(Spacer::create(), 0.5);

    gameOptionsHBox.addWidget(optionLabel(_("Default Game Rules")), 190);
    gameOptionsButton.setText(_("Change..."));
    gameOptionsButton.setOnClick(std::bind(&OptionsMenu::onGameOptions, this));
    gameOptionsHBox.addWidget(&gameOptionsButton, 130);

    gameOptionsHBox.addWidget(Spacer::create(), 160);

    gameOptionsHBox.addWidget(Spacer::create(), 0.5);

    pages[4].addWidget(&gameOptionsHBox, 32);
    pages[4].addWidget(VSpacer::create(6));

    languageHBox.addWidget(Spacer::create(), 0.5);
    languageHBox.addWidget(optionLabel(_("Language")), 190);

    for(size_t i = 0; i < availLanguages.size(); i++) {
        languageDropDownBox.addEntry(availLanguages[i].substr(0, availLanguages[i].size()-6), i);
        if(availLanguages[i].substr(availLanguages[i].size()-5,2) == settings.general.language) {
            languageDropDownBox.setSelectedItem(i);
        }
    }

    languageDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    languageHBox.addWidget(&languageDropDownBox, 100);
    languageHBox.addWidget(Spacer::create(), 190);
    languageHBox.addWidget(Spacer::create(), 0.5);

    pages[0].addWidget(&languageHBox, 32);
    pages[0].addWidget(VSpacer::create(6));

    generalHBox.addWidget(Spacer::create(), 0.5);
    generalHBox.addWidget(optionLabel(_("Campaign AI")), 190);
    int visibleIndex = 0;
    int selectedVisibleIndex = -1;
    for(unsigned int i=1; i<PlayerFactory::getList().size(); i++) {
        const PlayerFactory::PlayerData* playerData = PlayerFactory::getByIndex(i);
        if(playerData == nullptr) {
            continue;
        }
        const std::string& playerClass = playerData->getPlayerClass();
        if(playerClass.rfind("qBotSupport", 0) == 0) {
            continue;
        }
        aiDropDownBox.addEntry(playerData->getName(), i);
        if(playerClass == settings.ai.campaignAI) {
            selectedVisibleIndex = visibleIndex;
        }
        visibleIndex++;
    }
    if(selectedVisibleIndex >= 0) {
        aiDropDownBox.setSelectedItem(selectedVisibleIndex);
    } else if(visibleIndex > 0) {
        aiDropDownBox.setSelectedItem(0);
    }
    aiDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    generalHBox.addWidget(&aiDropDownBox, 290);
    introCheckbox.setText(_("Play Intro"));
    introCheckbox.setChecked(settings.general.playIntro);
    introCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    introHBox.addWidget(Spacer::create(), 0.5);
    introHBox.addWidget(&introCheckbox, 480);
    introHBox.addWidget(Spacer::create(), 0.5);
    pages[0].addWidget(&introHBox, 32);
    pages[0].addWidget(VSpacer::create(6));

    const bool duneCityActive = ModManager::instance().isInitialized()
        && ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) == "dunecity";
    duneCitySkinHBox.addWidget(Spacer::create(), 0.5);
    duneCitySkinHBox.addWidget(optionLabel(_("Campaign Graphics Skin")), 190);
    duneCityCampaignSkinDropDownBox.addEntry(_("SimCity"), 0);
    duneCityCampaignSkinDropDownBox.addEntry(_("Dune2"), 1);
    duneCityCampaignSkinDropDownBox.setSelectedItem(
        settings.general.duneCityCampaignSkin == 1 ? 1 : 0);
    duneCityCampaignSkinDropDownBox.setOnSelectionChange(
        std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    duneCitySkinHBox.addWidget(&duneCityCampaignSkinDropDownBox, 130);
    duneCitySkinHBox.addWidget(Spacer::create(), 160);
    duneCitySkinHBox.addWidget(Spacer::create(), 0.5);
    duneCitySkinHBox.setVisible(duneCityActive);
    duneCitySkinHBox.setEnabled(duneCityActive);
    if(duneCityActive) {
        pages[0].addWidget(&duneCitySkinHBox, 32);
        pages[0].addWidget(VSpacer::create(6));
    }
    generalHBox.addWidget(Spacer::create(), 0.5);

    pages[4].addWidget(&generalHBox, 32);
    pages[4].addWidget(VSpacer::create(6));

    resolutionHBox.addWidget(Spacer::create(), 0.5);
#ifdef __ANDROID__
    resolutionHBox.addWidget(optionLabel(_("Interface Resolution")), 190);
#else
    resolutionHBox.addWidget(optionLabel(_("Video Resolution")), 190);
#endif

    int i = 0;
    for(const Coord& coord : availScreenRes) {
        // Interface Size controls enlargement independently of resolution.
        resolutionDropDownBox.addEntry(fmt::sprintf("%d x %d", coord.x, coord.y), i);
        if(
#ifdef __ANDROID__
            coord.x == settings.video.width && coord.y == settings.video.interfaceHeight
#else
            coord.x == settings.video.physicalWidth && coord.y == settings.video.physicalHeight
#endif
        ) {
            resolutionDropDownBox.setSelectedItem(i);
        }
        i++;
    }
    resolutionDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    resolutionHBox.addWidget(&resolutionDropDownBox, 130);
    resolutionHBox.addWidget(Spacer::create(), 5);
    zoomlevelDropDownBox.addEntry("Zoom 1x", 0);
    zoomlevelDropDownBox.addEntry("Zoom 2x", 1);
    zoomlevelDropDownBox.addEntry("Zoom 3x", 2);
    zoomlevelDropDownBox.setSelectedItem(settings.video.preferredZoomLevel);
    zoomlevelDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    resolutionHBox.addWidget(&zoomlevelDropDownBox, 72);
    resolutionHBox.addWidget(Spacer::create(), 5);
    for(int i = 0; i < Scaler::NumScaler; i++) {
        scalerDropDownBox.addEntry(Scaler::getScalerName((Scaler::ScalerType) i));
    }
    Scaler::ScalerType currentScaler = Scaler::getScalerByName(settings.video.scaler);
    scalerDropDownBox.setSelectedItem(currentScaler >= 0 ? currentScaler : Scaler::ScaleHD);
    scalerDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    resolutionHBox.addWidget(&scalerDropDownBox, 78);

    resolutionHBox.addWidget(Spacer::create(), 0.5);

    pages[1].addWidget(&resolutionHBox, 32);
    pages[1].addWidget(VSpacer::create(6));

    videoHBox.addWidget(Spacer::create(), 0.5);
    fullScreenCheckbox.setText(_("Full Screen"));
    fullScreenCheckbox.setChecked(settings.video.fullscreen);
    fullScreenCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    videoHBox.addWidget(&fullScreenCheckbox, 240);
    frameLimitCheckbox.setText(_("Enable VSync"));
    frameLimitCheckbox.setChecked(settings.video.frameLimit);
    frameLimitCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    videoHBox.addWidget(&frameLimitCheckbox, 240);
    showTutorialHintsCheckbox.setText(_("Show Tutorial Hints"));
    showTutorialHintsCheckbox.setChecked(settings.general.showTutorialHints);
    showTutorialHintsCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    flagsHBox.addWidget(Spacer::create(), 0.5);
    flagsHBox.addWidget(&showTutorialHintsCheckbox, 240);
    videoHBox.addWidget(Spacer::create(), 0.5);

    pages[1].addWidget(&videoHBox, 32);
    pages[1].addWidget(VSpacer::create(6));

    videoHBox2.addWidget(Spacer::create(), 0.5);
    showWatermarkCheckbox.setText(_("Show Watermark"));
    showWatermarkCheckbox.setChecked(settings.video.showWatermark);
    showWatermarkCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    flagsHBox.addWidget(&showWatermarkCheckbox, 240);
    flagsHBox.addWidget(Spacer::create(), 0.5);
    pages[0].addWidget(&flagsHBox, 32);
    pages[0].addWidget(VSpacer::create(6));
    videoHBox2.addWidget(optionLabel(_("Cursor")), 110);
    cursorVisibilityDropDownBox.addEntry(_("Auto"), 0);
    cursorVisibilityDropDownBox.addEntry(_("Hidden"), 1);
    cursorVisibilityDropDownBox.addEntry(_("Visible"), 2);
    int cursorVisibilityIndex = settings.video.cursorVisibility >= 0 && settings.video.cursorVisibility <= 2
        ? settings.video.cursorVisibility : 0;
    cursorVisibilityDropDownBox.setSelectedItem(cursorVisibilityIndex);
    cursorVisibilityDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    videoHBox2.addWidget(&cursorVisibilityDropDownBox, 130);
    videoHBox2.addWidget(optionLabel(_("Scale")), 70);
    cursorScaleDropDownBox.addEntry(_("Default (1.5x)"), 0);
    cursorScaleDropDownBox.addEntry("1x", 1);
    cursorScaleDropDownBox.addEntry("2x", 2);
    cursorScaleDropDownBox.addEntry("3x", 3);
    cursorScaleDropDownBox.addEntry("4x", 4);
    int cursorScaleIndex = settings.video.cursorScale >= 0 && settings.video.cursorScale <= 4 ? settings.video.cursorScale : 0;
    cursorScaleDropDownBox.setSelectedItem(cursorScaleIndex);
    cursorScaleDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    videoHBox2.addWidget(&cursorScaleDropDownBox, 170);
    videoHBox2.addWidget(Spacer::create(), 0.5);

    pages[1].addWidget(&videoHBox2, 32);
    pages[1].addWidget(VSpacer::create(6));

    paletteHBox.addWidget(Spacer::create(), 0.5);
    paletteHBox.addWidget(optionLabel(_("Menu Layout")), 110);
    startMenuModeDropDownBox.addEntry(_("Classic"), 0);
    startMenuModeDropDownBox.addEntry(_("Enlarged"), 1);
    startMenuModeDropDownBox.setSelectedItem(validatedStartMenuMode(settings.video.startMenuMode));
    startMenuModeDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    paletteHBox.addWidget(&startMenuModeDropDownBox, 120);
    paletteHBox.addWidget(optionLabel(_("Colors")), 70);
    paletteDropDownBox.addEntry(_("Dark"), 0);
    paletteDropDownBox.addEntry(_("High Contrast"), 1);
    paletteDropDownBox.setSelectedItem(validatedMenuPalette(settings.video.menuPalette));
    paletteDropDownBox.setOnSelectionChange([this](bool interactive) {
        updatePalettePreview();
        onChangeOption(interactive);
    });
    paletteHBox.addWidget(&paletteDropDownBox, 180);
    paletteHBox.addWidget(Spacer::create(), 0.5);
    pages[1].addWidget(&paletteHBox, 32);
    pages[1].addWidget(VSpacer::create(6));
    palettePreview.setText("Dune City");
    palettePreview.setEnabled(false);
    updatePalettePreview();
    pages[1].addWidget(&palettePreview, 24);

    interfaceHBox.addWidget(Spacer::create(), 0.5);
    interfaceHBox.addWidget(optionLabel(_("Interface Size")), 190);
    const char* sizes[] = {"Large", "Medium", "Small", "Automatic", "Native (1:1)"};
    const int heights[] = {480, 600, 768, 0, INTERFACE_HEIGHT_NATIVE};
    for(int size = 0; size < 5; ++size) {
#ifdef __ANDROID__
        // Android needs a fixed logical interface across surface changes.
        if(size >= 3) continue;
#endif
        interfaceSizeDropDownBox.addEntry(_(sizes[size]), heights[size]);
        if(heights[size] == settings.video.interfaceHeight) interfaceSizeDropDownBox.setSelectedItem(size);
    }
    interfaceSizeDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    interfaceHBox.addWidget(&interfaceSizeDropDownBox, 130);
    aspectDropDownBox.addEntry("4:3", 0);
    aspectDropDownBox.addEntry("16:9", 1);
#ifdef __EMSCRIPTEN__
    aspectDropDownBox.setSelectedItem(settings.video.physicalWidth * 3 > settings.video.physicalHeight * 4 ? 1 : 0);
#else
    aspectDropDownBox.setSelectedItem(settings.video.width * 3 > settings.video.height * 4 ? 1 : 0);
#endif
    aspectDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    interfaceHBox.addWidget(optionLabel(_("Shape")), 80);
    interfaceHBox.addWidget(&aspectDropDownBox, 80);
#else
    interfaceHBox.addWidget(Spacer::create(), 160);
#endif
    interfaceHBox.addWidget(Spacer::create(), 0.5);
    pages[1].addWidget(&interfaceHBox, 32);
    pages[1].addWidget(VSpacer::create(6));

    scrollHBox.addWidget(Spacer::create(), 0.5);
    scrollHBox.addWidget(optionLabel(_("Scroll Speed")), 190);
    for(int speed : {1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20}) {
        scrollSpeedDropDownBox.addEntry(std::to_string(speed), speed);
        if(speed == settings.general.scrollSpeed) scrollSpeedDropDownBox.setSelectedItem(scrollSpeedDropDownBox.getNumEntries()-1);
    }
    if(scrollSpeedDropDownBox.getSelectedEntryIntData() != settings.general.scrollSpeed) {
        scrollSpeedDropDownBox.addEntry(std::to_string(settings.general.scrollSpeed), settings.general.scrollSpeed);
        scrollSpeedDropDownBox.setSelectedItem(scrollSpeedDropDownBox.getNumEntries()-1);
    }
    scrollSpeedDropDownBox.setOnSelectionChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    scrollHBox.addWidget(&scrollSpeedDropDownBox, 130);
    scrollHBox.addWidget(Spacer::create(), 160);
    scrollHBox.addWidget(Spacer::create(), 0.5);
    pages[3].addWidget(&scrollHBox, 32);
    auto addControl = [&](HBox& row, Checkbox& box, const char* label, bool checked) {
        row.addWidget(Spacer::create(),0.5);
        box.setText(_(label)); box.setChecked(checked);
        box.setOnClick(std::bind(&OptionsMenu::onChangeOption,this,true));
        row.addWidget(&box,480); row.addWidget(Spacer::create(),0.5);
        pages[3].addWidget(VSpacer::create(6)); pages[3].addWidget(&row,32);
    };
    addControl(cameraKeysHBox,wasdCameraCheckbox,"WASD camera (Shift+A attack, Shift+D drop)",settings.general.wasdCamera);
    wasdCameraCheckbox.setTooltipText(_("With WASD: Shift+S stops selected units. T selects matching units on screen; Ctrl+T selects across the map; Shift+T toggles time."));
    addControl(mouseOrdersHBox,leftClickOrdersCheckbox,"Left-click orders (drag to select, right-click cancels)",settings.general.leftClickOrders);
    addControl(movementPathsHBox,movementPathsCheckbox,"Show movement paths for selected units",settings.general.showMovementPaths);


    audioHBox.addWidget(Spacer::create(), 0.5);
    playSFXCheckbox.setText(_("Play SFX"));
    playSFXCheckbox.setChecked(settings.audio.playSFX);
    playSFXCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    audioHBox.addWidget(&playSFXCheckbox, 240);
    playMusicCheckbox.setText(_("Play Music"));
    playMusicCheckbox.setChecked(settings.audio.playMusic);
    playMusicCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    audioHBox.addWidget(&playMusicCheckbox, 240);
    audioHBox.addWidget(Spacer::create(), 0.5);

    pages[2].addWidget(&audioHBox, 32);
    pages[2].addWidget(VSpacer::create(6));

    audioHBox2.addWidget(Spacer::create(), 0.5);
    playCreditsSFXCheckbox.setText(_("Play Credits SFX"));
    playCreditsSFXCheckbox.setChecked(settings.audio.playCreditsSFX);
    playCreditsSFXCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    audioHBox2.addWidget(&playCreditsSFXCheckbox, 240);
    audioHBox2.addWidget(Spacer::create(), 240);
    audioHBox2.addWidget(Spacer::create(), 0.5);

    pages[2].addWidget(&audioHBox2, 32);
    const char* audioDriver = SDL_GetCurrentAudioDriver();
    if(audioDriver && std::string(audioDriver) == "dummy") {
        pages[2].addWidget(Label::create(_("Audio unavailable. Restart the game to retry.")), 32);
    }
    pages[2].addWidget(VSpacer::create(6));

    networkPortHBox.addWidget(Spacer::create(), 0.5);
    networkPortHBox.addWidget(optionLabel(_("Port")), 190);
    portTextBox.setMaximumTextLength(5);
    portTextBox.setAllowedChars("0123456789");
    portTextBox.setOnTextChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    networkPortHBox.addWidget(&portTextBox, 100);
    portTextBox.setText(std::to_string(settings.network.serverPort));
    networkPortHBox.addWidget(Spacer::create(), 190);
    networkPortHBox.addWidget(Spacer::create(), 0.5);
    pages[4].addWidget(&networkPortHBox, 32);
    pages[4].addWidget(VSpacer::create(6));

    networkMetaServerHBox.addWidget(Spacer::create(), 0.5);
    networkMetaServerHBox.addWidget(optionLabel(_("MetaServer")), 190);
    metaServerTextBox.setOnTextChange(std::bind(&OptionsMenu::onChangeOption, this, std::placeholders::_1));
    networkMetaServerHBox.addWidget(&metaServerTextBox, 290);
    metaServerTextBox.setText(settings.network.metaServer);
    networkMetaServerHBox.addWidget(Spacer::create(), 0.5);
    pages[4].addWidget(&networkMetaServerHBox, 32);
    pages[4].addWidget(VSpacer::create(6));

    diagnosticLogsCheckbox.setText(_("Diagnostic logs (development)"));
    diagnosticLogsCheckbox.setChecked(settings.general.diagnosticLogs);
    diagnosticLogsCheckbox.setOnClick(std::bind(&OptionsMenu::onChangeOption, this, true));
    diagnosticLogsHBox.addWidget(Spacer::create(), 0.5);
    diagnosticLogsHBox.addWidget(&diagnosticLogsCheckbox, 480);
    diagnosticLogsHBox.addWidget(Spacer::create(), 0.5);
    pages[4].addWidget(&diagnosticLogsHBox, 32);
    pages[4].addWidget(VSpacer::create(6));

    restoreDefaultsHBox.addWidget(Spacer::create(), 0.5);
    restoreDefaultsButton.setText(_("Restore Config Defaults"));
    restoreDefaultsButton.setOnClick(std::bind(&OptionsMenu::onRestoreDefaults, this));
    restoreDefaultsHBox.addWidget(&restoreDefaultsButton, 320);
    restoreDefaultsHBox.addWidget(Spacer::create(), 0.5);
    pages[4].addWidget(&restoreDefaultsHBox, 32);
    pages[4].addWidget(VSpacer::create(6));

    clearDuneCityAssetsHBox.addWidget(Spacer::create(), 0.5);
    clearDuneCityAssetsButton.setText(_("Clear DuneCity Asset Cache"));
    clearDuneCityAssetsButton.setOnClick(
        std::bind(&OptionsMenu::onClearDuneCityAssets, this));
    clearDuneCityAssetsHBox.addWidget(&clearDuneCityAssetsButton, 320);
    clearDuneCityAssetsHBox.addWidget(Spacer::create(), 0.5);
    pages[4].addWidget(&clearDuneCityAssetsHBox, 32);
    pages[4].addWidget(VSpacer::create(6));

    backButton.setText(_("BACK"));
    backButton.setOnClick(std::bind(&OptionsMenu::onOptionsCancel, this));


    acceptButton.setText(_("APPLY"));
    acceptButton.setEnabled(false);
    acceptButton.setOnClick(std::bind(&OptionsMenu::onOptionsOK, this));


    windowWidget.addWidget(&backButton, Point(left + 30, getSize().y - 64), Point(220, 40));
    windowWidget.addWidget(&acceptButton, Point(left + 290, getSize().y - 64), Point(220, 40));
#ifdef __ANDROID__
    fullScreenCheckbox.setEnabled(false);
#endif
    showPage(lastPage);
}

OptionsMenu::~OptionsMenu()
{
    ;
}

void OptionsMenu::showPage(int page) {
    lastPage = page;
    for(int i = 0; i < 5; ++i) {
        pages[i].setVisible(i == page);
        pages[i].setEnabled(i == page);
        pageButtons[i].setToggleState(i == page);
    }
}

void OptionsMenu::updatePalettePreview() {
    const auto colors = menuPalette(paletteDropDownBox.getSelectedEntryIntData());
    palettePreview.setTextColor(colors.foreground, colors.shadow);
}

void OptionsMenu::onChangeOption(bool bInteractive) {
    bool bChanged = false;

    bChanged |= (settings.general.playerName != nameTextBox.getText());
    int languageIndex = languageDropDownBox.getSelectedEntryIntData();
    if(languageIndex >= 0) {
        std::string languageFilename = availLanguages[languageIndex];
        bChanged |= (settings.general.language != languageFilename.substr(languageFilename.size()-5,2));
    }
    const PlayerFactory::PlayerData* pPlayerData = PlayerFactory::getByIndex(aiDropDownBox.getSelectedEntryIntData());
    bChanged |= ((pPlayerData == nullptr) || (settings.ai.campaignAI != pPlayerData->getPlayerClass()));
    bChanged |= settings.general.scrollSpeed != scrollSpeedDropDownBox.getSelectedEntryIntData();
    bChanged |= settings.general.wasdCamera != wasdCameraCheckbox.isChecked();
    bChanged |= settings.general.leftClickOrders != leftClickOrdersCheckbox.isChecked();
    bChanged |= settings.general.showMovementPaths != movementPathsCheckbox.isChecked();
    bChanged |= settings.general.diagnosticLogs != diagnosticLogsCheckbox.isChecked();
    bChanged |= settings.video.interfaceHeight != interfaceSizeDropDownBox.getSelectedEntryIntData();
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    bChanged |= (settings.video.width * 3 > settings.video.height * 4) != (aspectDropDownBox.getSelectedEntryIntData() == 1);
#endif
    bChanged |= (settings.general.playIntro != introCheckbox.isChecked());
    bChanged |= (settings.general.showTutorialHints != showTutorialHintsCheckbox.isChecked());
    bChanged |= (settings.general.duneCityCampaignSkin
                 != duneCityCampaignSkinDropDownBox.getSelectedEntryIntData());

    int selectedResolution = resolutionDropDownBox.getSelectedEntryIntData();
    if(selectedResolution >= 0) {
#ifdef __ANDROID__
        bChanged |= settings.video.width != availScreenRes[selectedResolution].x;
        bChanged |= settings.video.interfaceHeight != availScreenRes[selectedResolution].y;
#else
        bChanged |= (settings.video.physicalWidth != availScreenRes[selectedResolution].x);
        bChanged |= (settings.video.physicalHeight != availScreenRes[selectedResolution].y);
#endif
    }
    bChanged |= (settings.video.preferredZoomLevel != zoomlevelDropDownBox.getSelectedEntryIntData());
    bChanged |= (settings.video.fullscreen != fullScreenCheckbox.isChecked());
    bChanged |= (settings.video.frameLimit != frameLimitCheckbox.isChecked());
    bChanged |= (settings.video.scaler != scalerDropDownBox.getSelectedEntry());
    bChanged |= (settings.video.showWatermark != showWatermarkCheckbox.isChecked());
    bChanged |= (settings.video.cursorVisibility != cursorVisibilityDropDownBox.getSelectedEntryIntData());
    bChanged |= (settings.video.cursorScale != cursorScaleDropDownBox.getSelectedEntryIntData());
    bChanged |= (settings.video.menuPalette != paletteDropDownBox.getSelectedEntryIntData());
    bChanged |= (settings.video.startMenuMode != startMenuModeDropDownBox.getSelectedEntryIntData());

    bChanged |= (settings.audio.playSFX != playSFXCheckbox.isChecked());
    bChanged |= (settings.audio.playMusic != playMusicCheckbox.isChecked());
    bChanged |= (settings.audio.playCreditsSFX != playCreditsSFXCheckbox.isChecked());

    bChanged |= (settings.gameOptions != currentGameOptions);

    bChanged |= (settings.network.serverPort != atoi(portTextBox.getText().c_str()));
    bChanged |= (settings.network.metaServer != metaServerTextBox.getText());

    acceptButton.setEnabled(bChanged);
}

void OptionsMenu::onOptionsOK() {
    std::string playername = nameTextBox.getText();
    if(playername.empty() && playername != settings.general.playerName) {
        showPage(0);
        openWindow(MsgBox::create(_("Please enter a Player Name.")));
        return;
    }

    int serverport = settings.network.serverPort;
    if(portTextBox.getText() != std::to_string(settings.network.serverPort)
       && (!parseString(portTextBox.getText(), serverport) || serverport <= 0 || serverport > 65535)) {
        showPage(4);
        openWindow(MsgBox::create(fmt::sprintf(_("Server Port must be between 1 and 65535!\nDefault Server Port is %d!"), DEFAULT_PORT)));
        return;
    }

    std::string metaserver = metaServerTextBox.getText();
    if(metaserver.empty() && metaserver != settings.network.metaServer) {
        showPage(4);
        openWindow(MsgBox::create(_("Please enter a MetaServer.")));
        return;
    }

    settings.general.scrollSpeed = scrollSpeedDropDownBox.getSelectedEntryIntData();
    settings.general.wasdCamera=wasdCameraCheckbox.isChecked();
    settings.general.leftClickOrders=leftClickOrdersCheckbox.isChecked();
    settings.general.showMovementPaths=movementPathsCheckbox.isChecked();
    settings.general.diagnosticLogs=diagnosticLogsCheckbox.isChecked();
    settings.general.playerName = playername;
    std::string languageFilename = (languageDropDownBox.getSelectedEntryIntData() < 0) ? "English.en.po" : availLanguages[languageDropDownBox.getSelectedEntryIntData()];
    settings.general.language = languageFilename.substr(languageFilename.size()-5,2);
    settings.general.playIntro = introCheckbox.isChecked();
    settings.general.showTutorialHints = showTutorialHintsCheckbox.isChecked();
    settings.general.duneCityCampaignSkin =
        duneCityCampaignSkinDropDownBox.getSelectedEntryIntData() == 1 ? 1 : 0;

    const PlayerFactory::PlayerData* pPlayerData = PlayerFactory::getByIndex(aiDropDownBox.getSelectedEntryIntData());
    settings.ai.campaignAI = ((pPlayerData != nullptr) ? pPlayerData->getPlayerClass() : DEFAULTAIPLAYERCLASS);

    int selectedResolution = resolutionDropDownBox.getSelectedEntryIntData();
#ifdef __ANDROID__
    if(selectedResolution >= 0) {
        settings.video.width = availScreenRes[selectedResolution].x;
        settings.video.height = availScreenRes[selectedResolution].y;
        settings.video.interfaceHeight = availScreenRes[selectedResolution].y;
    }
#else
    settings.video.physicalWidth = (selectedResolution >= 0) ? availScreenRes[selectedResolution].x : 0;
    settings.video.physicalHeight = (selectedResolution >= 0) ? availScreenRes[selectedResolution].y : 0;
#endif
    
    // Validate resolution settings
    if(settings.video.physicalWidth < SCREEN_MIN_WIDTH || settings.video.physicalHeight < SCREEN_MIN_HEIGHT) {
        openWindow(MsgBox::create(_("Invalid resolution selected. Please choose a valid resolution.")));
        return;
    }
    
    settings.video.interfaceHeight = interfaceSizeDropDownBox.getSelectedEntryIntData();

#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    const bool widescreen = aspectDropDownBox.getSelectedEntryIntData() == 1;
#ifdef __EMSCRIPTEN__
    // The backing surface takes the chosen shape before the interface size is
    // derived from it.
    settings.video.physicalWidth = interfaceWidthForHeight(settings.video.physicalHeight, widescreen);
#endif
#endif

#ifndef __ANDROID__
    // The interface size decides how much the chosen resolution is enlarged:
    // Native keeps it 1:1, Automatic applies the enlargement factor, a preset
    // takes the shape of the window. setVideoMode() derives the same size from
    // what actually ends up on screen.
    {
        const SDL_Point logical = interfaceLogicalSize(settings.video.interfaceHeight,
                                                       settings.video.physicalWidth,
                                                       settings.video.physicalHeight);
        settings.video.width = logical.x;
        settings.video.height = logical.y;
    }
#endif

#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    // A fixed preset keeps its exact 4:3 or 16:9 width on the platforms where
    // the player chooses the screen shape.
    if(settings.video.interfaceHeight > 0) {
        settings.video.width = interfaceWidthForHeight(settings.video.interfaceHeight, widescreen);
        settings.video.height = settings.video.interfaceHeight;
    }
#endif
    settings.video.preferredZoomLevel = zoomlevelDropDownBox.getSelectedEntryIntData();
    settings.video.scaler = scalerDropDownBox.getSelectedEntry();
    settings.video.fullscreen = fullScreenCheckbox.isChecked();
    settings.video.frameLimit = frameLimitCheckbox.isChecked();
    settings.video.showWatermark = showWatermarkCheckbox.isChecked();
    settings.video.cursorVisibility = cursorVisibilityDropDownBox.getSelectedEntryIntData();
    settings.video.cursorScale = cursorScaleDropDownBox.getSelectedEntryIntData();
    settings.video.menuPalette = validatedMenuPalette(paletteDropDownBox.getSelectedEntryIntData());
    settings.video.startMenuMode = validatedStartMenuMode(startMenuModeDropDownBox.getSelectedEntryIntData());

    settings.audio.playSFX = playSFXCheckbox.isChecked();
    settings.audio.playMusic = playMusicCheckbox.isChecked();
    settings.audio.playCreditsSFX = playCreditsSFXCheckbox.isChecked();

    if(currentGameOptions != effectiveGameOptions) saveGameOptionsAsDefaults(currentGameOptions);

    settings.network.serverPort = serverport;
    settings.network.metaServer = metaserver;

    saveConfiguration2File();
    WebRuntime::syncPersistentFiles();

    // sound is not reinitialized when restarting
    // => music and sound player do not reload settings
    soundPlayer->setSound(settings.audio.playSFX);
    if(musicPlayer->isMusicOn() != settings.audio.playMusic) {
        musicPlayer->setMusic(settings.audio.playMusic);
        musicPlayer->changeMusic(MUSIC_INTRO);
    }

    quit(MENU_QUIT_REINITIALIZE);
}

void OptionsMenu::onOptionsCancel() {
    quit();
}

void OptionsMenu::onGameOptions() {
    openWindow(GameOptionsWindow::create(currentGameOptions));
}

void OptionsMenu::onRestoreDefaults() {
    // Restore config files
    if (restoreDefaultConfigs()) {
        // Forget the player's game option choices for this mod too, so the
        // mod's own defaults come back.
        const std::string section = userGameOptionsSection();
        if(!section.empty()) {
            INIFile config(getConfigFilepath());
            if(config.hasSection(section)) {
                config.removeSection(section);
                config.saveChangesTo(getConfigFilepath());
            }
        }
        effectiveGameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
        currentGameOptions = effectiveGameOptions;

        std::string successMessage = 
            "Config files restored successfully!\n\n"
            "ObjectData.ini, QuantBot Config.ini and the game\n"
            "option defaults have been reset.\n\n"
            "IMPORTANT: Changes will take effect on next game start.\n"
            "Please restart the game.";
        MsgBox* pMsgBox = MsgBox::create(successMessage);
        openWindow(pMsgBox);
    } else {
        std::string errorMessage = 
            "ERROR: Failed to restore config files!\n\n"
            "Check the log file for details.";
        MsgBox* pMsgBox = MsgBox::create(errorMessage);
        pMsgBox->setTextColor(COLOR_RED);
        openWindow(pMsgBox);
    }
}

void OptionsMenu::onClearDuneCityAssets() {
    // Presentation-only recovery: this removes the managed graphics_skins cache
    // and nothing else. ModManager restores the bundled payload on next start.
    if(ModManager::instance().clearDunecityGraphicsCache()) {
        openWindow(MsgBox::create(
            _("DuneCity graphics cache cleared.\n\n"
              "Your saves, settings and mods were not changed.\n"
              "Restart the game to restore the bundled assets.")));
    } else {
        MsgBox* pMsgBox = MsgBox::create(
            _("ERROR: The DuneCity graphics cache could not be cleared.\n\n"
              "Check the log file for details."));
        pMsgBox->setTextColor(COLOR_RED);
        openWindow(pMsgBox);
    }
}

void OptionsMenu::saveConfiguration2File() {
    INIFile myINIFile(getConfigFilepath());

    myINIFile.setIntValue("General","Scroll Speed",settings.general.scrollSpeed);
    myINIFile.setBoolValue("General","WASD Camera",settings.general.wasdCamera);
    myINIFile.setBoolValue("General","Left Click Orders",settings.general.leftClickOrders);
    myINIFile.setBoolValue("General","Movement Paths",settings.general.showMovementPaths);
    myINIFile.setBoolValue("General","Diagnostic Logs",settings.general.diagnosticLogs);
    myINIFile.setBoolValue("General","Play Intro",settings.general.playIntro);
    myINIFile.setBoolValue("General","Show Tutorial Hints",settings.general.showTutorialHints);
    myINIFile.setIntValue("General","DuneCity Campaign Skin",settings.general.duneCityCampaignSkin);
    myINIFile.setBoolValue("General","Multiple Players Per House",settings.general.multiplePlayersPerHouse);
    myINIFile.setBoolValue("General","Campaign Online",settings.general.campaignOnline);
    myINIFile.setBoolValue("General","Custom Game Online",settings.general.customGameOnline);
    myINIFile.setStringValue("General","Custom Game Mod",settings.general.customGameMod);

    myINIFile.setIntValue("Video","Physical Width",settings.video.physicalWidth);
    myINIFile.setIntValue("Video","Physical Height",settings.video.physicalHeight);
    myINIFile.setIntValue("Video","Width",settings.video.width);
    myINIFile.setIntValue("Video","Height",settings.video.height);
    myINIFile.setIntValue("Video","Interface Height",settings.video.interfaceHeight);
    myINIFile.setBoolValue("Video","Fullscreen",settings.video.fullscreen);
    myINIFile.setBoolValue("Video","FrameLimit",settings.video.frameLimit);
    myINIFile.setIntValue("Video","Preferred Zoom Level",settings.video.preferredZoomLevel);
    myINIFile.setStringValue("Video","Scaler",settings.video.scaler);
    myINIFile.setBoolValue("Video","RotateUnitGraphics",settings.video.rotateUnitGraphics);
    myINIFile.setBoolValue("Video","Show Watermark",settings.video.showWatermark);
    myINIFile.setIntValue("Video","Cursor Visibility",settings.video.cursorVisibility);
    myINIFile.setIntValue("Video","Cursor Scale",settings.video.cursorScale);
    myINIFile.setIntValue("Video","Menu Palette",settings.video.menuPalette);
    myINIFile.setIntValue("Video","Start Menu Mode",settings.video.startMenuMode);

    myINIFile.setStringValue("General","Player Name",settings.general.playerName);
    myINIFile.setStringValue("General","Language",settings.general.language);

    myINIFile.setStringValue("AI","Campaign AI",settings.ai.campaignAI);

    myINIFile.setBoolValue("Audio","Play SFX",settings.audio.playSFX);
    myINIFile.setBoolValue("Audio","Play Music",settings.audio.playMusic);
    myINIFile.setBoolValue("Audio","Play Credits SFX",settings.audio.playCreditsSFX);

    myINIFile.setIntValue("Game Options","Game Speed",settings.gameOptions.gameSpeed);
    myINIFile.removeKey("Game Options","Structures Degrade On Concrete");
    myINIFile.setBoolValue("Game Options","Concrete Required",settings.gameOptions.concreteRequired);
    myINIFile.setBoolValue("Game Options","Fog of War",settings.gameOptions.fogOfWar);
    myINIFile.setBoolValue("Game Options","Start with Explored Map",settings.gameOptions.startWithExploredMap);
    myINIFile.setBoolValue("Game Options","Instant Build",settings.gameOptions.instantBuild);
    myINIFile.setBoolValue("Game Options","Only One Palace",settings.gameOptions.onlyOnePalace);
    myINIFile.setBoolValue("Game Options","Rocket-Turrets Need Power",settings.gameOptions.rocketTurretsNeedPower);
    myINIFile.setBoolValue("Game Options","Sandworms Respawn",settings.gameOptions.sandwormsRespawn);
    myINIFile.setBoolValue("Game Options","Killed Sandworms Drop Spice",settings.gameOptions.killedSandwormsDropSpice);
    myINIFile.setBoolValue("Game Options","Manual Carryall Drops",settings.gameOptions.manualCarryallDrops);
    myINIFile.setIntValue("Game Options","Maximum Number of Units Override",settings.gameOptions.maximumNumberOfUnitsOverride);
    myINIFile.setIntValue("Game Options","Maximum Number of Harvesters Override",settings.gameOptions.maximumNumberOfHarvestersOverride);
    myINIFile.setIntValue("Game Options","Maximum Number of Construction Yards Override",settings.gameOptions.maximumNumberOfConstructionYardsOverride);
    myINIFile.setBoolValue("Game Options", "Original Unit Damage", settings.gameOptions.originalUnitDamage);
    myINIFile.setBoolValue("Game Options","Immortal Human Player",settings.gameOptions.immortalHumanPlayer);

    myINIFile.setIntValue("Network","ServerPort",settings.network.serverPort);
    myINIFile.setStringValue("Network","MetaServer",settings.network.metaServer);

    myINIFile.saveChangesTo(getConfigFilepath());
}

void OptionsMenu::determineAvailableScreenResolutions() {
    availScreenRes.clear();
#ifdef __EMSCRIPTEN__
    // Browser backing buffers are independent of OS display modes and may be
    // larger than the viewport. The shell scales them to fit.
    for(const Coord size : {Coord(640, 480), Coord(854, 480), Coord(800, 600),
            Coord(960, 540), Coord(1024, 768), Coord(1280, 720), Coord(1280, 800),
            Coord(1280, 960), Coord(1366, 768), Coord(1440, 900), Coord(1600, 900),
            Coord(1600, 1200), Coord(1680, 1050), Coord(1920, 1080), Coord(1920, 1200),
            Coord(2560, 1440), Coord(2560, 1600), Coord(3440, 1440), Coord(3840, 2160)}) {
        availScreenRes.push_back(size);
    }
    const Coord current(settings.video.physicalWidth, settings.video.physicalHeight);
    if(std::find(availScreenRes.begin(), availScreenRes.end(), current) == availScreenRes.end()) {
        availScreenRes.insert(availScreenRes.begin(), current);
    }
    return;
#endif
#ifdef __ANDROID__
    availScreenRes.emplace_back(640, 480);
    availScreenRes.emplace_back(854, 480);
    availScreenRes.emplace_back(800, 600);
    availScreenRes.emplace_back(1067, 600);
    availScreenRes.emplace_back(1024, 768);
    availScreenRes.emplace_back(1366, 768);
    return;
#endif

    // Safety check: ensure window exists before trying to get display index
    if(window == nullptr) {
        SDL_Log("Warning: Window not available, using fallback resolutions");
        // Use fallback resolutions
        availScreenRes.emplace_back(640, 480 );    // VGA (4:3)
        availScreenRes.emplace_back(800, 600 );    // SVGA (4:3)
        availScreenRes.emplace_back(1024, 768 );   // XGA (4:3)
        availScreenRes.emplace_back(1280, 720 );   // WXGA (16:9)
        availScreenRes.emplace_back(1280, 1024 );  // SXGA (5:4)
        availScreenRes.emplace_back(1920, 1080 );  // 1080p (16:9)
        return;
    }

    SDL_DisplayMode displayMode;
    int displayIndex = SDL_GetWindowDisplayIndex(window);
    
    // Safety check: ensure display index is valid
    if(displayIndex < 0) {
        SDL_Log("Warning: Invalid display index, using fallback resolutions");
        displayIndex = 0; // Use primary display
    }
    
    int numDisplayModes = SDL_GetNumDisplayModes(displayIndex);
    
    // Safety check: ensure we have display modes
    if(numDisplayModes <= 0) {
        SDL_Log("Warning: No display modes available, using fallback resolutions");
        availScreenRes.emplace_back(640, 480 );    // VGA (4:3)
        availScreenRes.emplace_back(800, 600 );    // SVGA (4:3)
        availScreenRes.emplace_back(1024, 768 );   // XGA (4:3)
        availScreenRes.emplace_back(1280, 720 );   // WXGA (16:9)
        availScreenRes.emplace_back(1280, 1024 );  // SXGA (5:4)
        availScreenRes.emplace_back(1920, 1080 );  // 1080p (16:9)
        return;
    }
    
    // Screen coordinates differ from backing pixels on Retina displays. A
    // fitted window can render 1080p even on a 1440x900-point desktop.
    SDL_Rect displayBounds = {0, 0, 0, 0};
    const bool haveDisplayBounds = (SDL_GetDisplayBounds(displayIndex, &displayBounds) == 0
                                    && displayBounds.w > 0 && displayBounds.h > 0);
    SDL_Point windowSize = {0, 0}, outputSize = {0, 0};
    SDL_GetWindowSize(window, &windowSize.x, &windowSize.y);
    if(renderer) SDL_GetRendererOutputSize(renderer, &outputSize.x, &outputSize.y);
    const SDL_Point renderLimit = displayVideoResolutionLimit(
        {displayBounds.w, displayBounds.h}, windowSize, outputSize);

    for(int i = numDisplayModes-1; i >=0; i--) {
        if(SDL_GetDisplayMode(displayIndex, i, &displayMode) == 0) {
            Coord screenRes(displayMode.w, displayMode.h);
            const bool fitsDisplay = !haveDisplayBounds
                                     || (screenRes.x <= renderLimit.x && screenRes.y <= renderLimit.y);
            if(fitsDisplay && screenRes.x >= SCREEN_MIN_WIDTH && screenRes.y >= SCREEN_MIN_HEIGHT) {
                if(std::find(availScreenRes.begin(), availScreenRes.end(), screenRes) == availScreenRes.end()) {
                    // not yet in the list (might happen if e.g. multiple refresh rates are reported)
                    availScreenRes.push_back(screenRes);
                }
            }
        }
    }

    // Display drivers need not enumerate every useful render size. Include
    // common choices supported by the backing pixels, independently of OS modes.
    if(haveDisplayBounds) {
        for(const Coord size : {Coord(1280, 720), Coord(1600, 900), Coord(1920, 1080),
                                Coord(2560, 1440)}) {
            if(size.x <= renderLimit.x && size.y <= renderLimit.y
               && std::find(availScreenRes.begin(), availScreenRes.end(), size) == availScreenRes.end())
                availScreenRes.push_back(size);
        }
        std::sort(availScreenRes.begin(), availScreenRes.end(), [](const Coord& a, const Coord& b) {
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });
    }

    if(availScreenRes.empty()) {
        // Not possible or not available
        // try some standard resolutions

        availScreenRes.emplace_back(640, 480 );    // VGA (4:3)
        availScreenRes.emplace_back(800, 480 );    // WVGA (5:3)
        availScreenRes.emplace_back(800, 600 );    // SVGA (4:3)
        availScreenRes.emplace_back(960, 540 );    // Quarter HD (16:9)
        availScreenRes.emplace_back(960, 720 );    // ? (4:3)
        availScreenRes.emplace_back(1024, 576 );   // WSVGA (16:9)
        availScreenRes.emplace_back(1024, 640 );   // ? (16:10)
        availScreenRes.emplace_back(1024, 768 );   // XGA (4:3)
        availScreenRes.emplace_back(1152, 864 );   // XGA+ (4:3)
        availScreenRes.emplace_back(1280, 720 );  // WXGA (16:9)
        availScreenRes.emplace_back(1280, 768 );   // WXGA (5:3)
        availScreenRes.emplace_back(1280, 800 );   // WXGA (16:10)
        availScreenRes.emplace_back(1280, 960 );   // SXGA- (4:3)
        availScreenRes.emplace_back(1280, 1024 );  // SXGA (5:4)
        availScreenRes.emplace_back(1366, 768 );   // HDTV 720p (~16:9)
        availScreenRes.emplace_back(1400, 1050 );  // SXGA+ (4:3)
        availScreenRes.emplace_back(1440, 900 );   // WXGA+ (16:10)
        availScreenRes.emplace_back(1440, 1080 );  // ? (4:3)
        availScreenRes.emplace_back(1600, 900 );   // WSXGA (16:9)
        availScreenRes.emplace_back(1600, 1200 );  // UXGA (4:3)
        availScreenRes.emplace_back(1680, 1050 );  // WSXGA+ (16:10)
        availScreenRes.emplace_back(1920, 1080 );  // 1080p (16:9)
        availScreenRes.emplace_back(1920, 1200 );  // WUXGA (16:10)
        availScreenRes.emplace_back(2560, 1440 );  // WQHD (16:9)
        availScreenRes.emplace_back(2560, 1600 );  // WQXGA (16:10)
        availScreenRes.emplace_back(3840, 2160 );  // 2160p (16:9)
    }

    Coord currentRes(settings.video.physicalWidth, settings.video.physicalHeight);

    if(std::find(availScreenRes.begin(), availScreenRes.end(), currentRes) == availScreenRes.end()) {
        // not yet in the list
        availScreenRes.insert(availScreenRes.begin(), currentRes);
    }
}

void OptionsMenu::onChildWindowClose(Window* pChildWindow) {
    GameOptionsWindow* pGameOptionsWindow = dynamic_cast<GameOptionsWindow*>(pChildWindow);
    if(pGameOptionsWindow != nullptr) {
        currentGameOptions = pGameOptionsWindow->getGameOptions();

        onChangeOption(true);
    }
}
