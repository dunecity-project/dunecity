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

#include <Menu/SinglePlayerSkirmishMenu.h>
#include <mod/ModManager.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>

#include <GUI/dune/GameOptionsWindow.h>

#include <GameInitSettings.h>
#include <Network/WorkshopGameContent.h>
#include <sand.h>
#include <Menu/CustomGamePlayers.h>
#include <Network/NetworkManager.h>
#include <FileClasses/FileManager.h>
#include <misc/IMemoryStream.h>
#include <misc/FileSystem.h>
#include <misc/fnkdat.h>
#include <GUI/dune/LoadSaveWindow.h>
#include <GUI/MsgBox.h>

namespace {
const int houseOrder[] = {
    HOUSE_ATREIDES,
    HOUSE_ORDOS,
    HOUSE_HARKONNEN,
    HOUSE_MERCENARY,
    HOUSE_FREMEN,
    HOUSE_SARDAUKAR,
    HOUSE_NEUTRAL,
    HOUSE_REBELS,
    HOUSE_CUSTOM
};

constexpr int kVisibleHouseButtons = 3;
int getHouseChoiceCount() {
    const int capacity = sizeof(houseOrder) / sizeof(houseOrder[0]);
    return isHouseAvailable(HOUSE_CUSTOM) ? capacity : capacity - 1;
}

int getMaxHouseScrollPos() {
    return getHouseChoiceCount() - kVisibleHouseButtons;
}

const char* const kSupportPlayerClasses[] = {
    "",
    "qBotSupportEasy",
    "qBotSupportMedium",
    "qBotSupportHard",
    "qBotSupportBrutal"
};

constexpr int kSupportOptionCount = sizeof(kSupportPlayerClasses) / sizeof(kSupportPlayerClasses[0]);

const char* const kEnemyAIClasses[] = {
    "qBotEasy", "qBotMedium", "qBotHard", "qBotBrutal", "qBotDefend", "CampaignAIPlayer"
};

constexpr int kEnemyAIOptionCount = sizeof(kEnemyAIClasses) / sizeof(kEnemyAIClasses[0]);
}

SinglePlayerSkirmishMenu::SinglePlayerSkirmishMenu(bool campaignCoop) : MenuBase(), campaignCoop(campaignCoop)
{
    currentHouseChoiceScrollPos = 0;
    selectedButton = 1;
    mission = 1;
    supportBotIndex = 0;
    enemyAIIndex = 0;  // Default to QuantBot Easy
    currentGameOptions = effectiveGameOptions;  // Use mod-aware effective options

    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));

    setWindowWidget(&windowWidget);

    // set up pictures in the background
    SDL_Texture* pDuneLegacy = pGFXManager->getUIGraphic(UI_DuneLegacy);
    duneLegacy.setTexture(pDuneLegacy);
    SDL_Rect dest1 = calcAlignedDrawingRect(pDuneLegacy);
    dest1.y = dest1.y + getHeight(pDuneLegacy)/2 + 28;
    windowWidget.addWidget(&duneLegacy, dest1);

    SDL_Texture* pMenuButtonBorder = pGFXManager->getUIGraphic(UI_MenuButtonBorder);
    buttonBorder.setTexture(pMenuButtonBorder);
    SDL_Rect dest2 = calcAlignedDrawingRect(pMenuButtonBorder);
    dest2.y = dest2.y + getHeight(pMenuButtonBorder)/2 + 59;
    windowWidget.addWidget(&buttonBorder, dest2);

    // set up menu buttons
    windowWidget.addWidget(&menuButtonsVBox,Point((getRendererWidth() - 160)/2,getRendererHeight()/2 + 64), Point(160,166));

    startButton.setText(campaignCoop ? _("Host Campaign Co-op") : _("Start"));
    startButton.setOnClick(std::bind(&SinglePlayerSkirmishMenu::onStart, this));
    menuButtonsVBox.addWidget(&startButton);
    startButton.setActive();

    menuButtonsVBox.addWidget(VSpacer::create(8));

    // AI Support explanation
    Label* supportLabel = Label::create(_("AI support: help you fight"));
    supportLabel->setTextColor(COLOR_BLACK);
    supportLabel->setTextFontSize(12);
    supportLabel->setAlignment(Alignment_HCenter);
    menuButtonsVBox.addWidget(supportLabel);
    menuButtonsVBox.addWidget(VSpacer::create(2));

    supportBotDropDown.addEntry(_("AI Support: None"), 0);
    supportBotDropDown.addEntry(_("AI Support: Easy"), 1);
    supportBotDropDown.addEntry(_("AI Support: Medium"), 2);
    supportBotDropDown.addEntry(_("AI Support: Hard"), 3);
    supportBotDropDown.addEntry(_("AI Support: Brutal"), 4);
    supportBotDropDown.setSelectedItem(0);
    supportBotDropDown.setOnSelectionChange(std::bind(&SinglePlayerSkirmishMenu::onSupportBotSelectionChanged, this, std::placeholders::_1));
    menuButtonsVBox.addWidget(&supportBotDropDown);
    updateSupportBotLabel();

    menuButtonsVBox.addWidget(VSpacer::create(6));

    // Enemy AI explanation
    Label* enemyAILabel = Label::create(_("Enemy AI"));
    enemyAILabel->setTextColor(COLOR_BLACK);
    enemyAILabel->setTextFontSize(12);
    enemyAILabel->setAlignment(Alignment_HCenter);
    menuButtonsVBox.addWidget(enemyAILabel);
    menuButtonsVBox.addWidget(VSpacer::create(2));

    enemyAIDropDown.addEntry(_("QuantBot Easy"), 0);
    enemyAIDropDown.addEntry(_("QuantBot Medium"), 1);
    enemyAIDropDown.addEntry(_("QuantBot Hard"), 2);
    enemyAIDropDown.addEntry(_("QuantBot Brutal"), 3);
    enemyAIDropDown.addEntry(_("QuantBot Defend"), 4);
    enemyAIDropDown.addEntry(_("Campaign AI"), 5);
    enemyAIDropDown.setSelectedItem(0);
    enemyAIDropDown.setOnSelectionChange(std::bind(&SinglePlayerSkirmishMenu::onEnemyAISelectionChanged, this, std::placeholders::_1));
    menuButtonsVBox.addWidget(&enemyAIDropDown);

    menuButtonsVBox.addWidget(VSpacer::create(6));

    gameOptionsButton.setText(_("Game Options"));
    gameOptionsButton.setOnClick(std::bind(&SinglePlayerSkirmishMenu::onGameOptions, this));
    menuButtonsVBox.addWidget(&gameOptionsButton);

    menuButtonsVBox.addWidget(VSpacer::create(6));

    backButton.setText(_("Back"));
    backButton.setOnClick(std::bind(&SinglePlayerSkirmishMenu::onCancel, this));
    menuButtonsVBox.addWidget(&backButton);

    hostCoopButton.setText(_("Host Co-op"));
    hostCoopButton.setOnClick(std::bind(&SinglePlayerSkirmishMenu::onHostCoop, this));
    loadCoopButton.setText(_("Load Campaign Save"));
    loadCoopButton.setOnClick([this]() { onLoadCoop(false); });
    loadSharedSaveButton.setText(_("Load Co-op Save"));
    loadSharedSaveButton.setOnClick([this]() { onLoadCoop(true); });
    coopNetworkDropDown.addEntry(_("Internet"), 0);
    coopNetworkDropDown.addEntry(_("LAN only"), 1);
    coopNetworkDropDown.setSelectedItem(0);
    const int coopX = getRendererWidth()/2 - 280;
    const int coopY = getRendererHeight()/2 + 64;
    windowWidget.addWidget(&hostCoopButton, Point(coopX, coopY), Point(180, 24));
    windowWidget.addWidget(&coopNetworkDropDown, Point(coopX, coopY+32), Point(180, 24));
    windowWidget.addWidget(&loadCoopButton, Point(coopX, coopY+64), Point(180, 24));
    windowWidget.addWidget(&loadSharedSaveButton, Point(coopX, coopY+96), Point(180, 24));
    // DuneCity city-building art for this mission, the same choice as the campaign's.
    skinLabel.setText(_("DuneCity skin"));
    skinLabel.setTextFontSize(12);
    windowWidget.addWidget(&skinLabel, Point(coopX, coopY+126), Point(180, 18));
    skinDropDown.addEntry(_("SimCity"), 0);
    skinDropDown.addEntry(_("Dune2"), 1);
    skinDropDown.setSelectedItem(settings.general.duneCityCampaignSkin == 1 ? 1 : 0);
    skinDropDown.setEnabled(ModManager::instance().isCityModeActive());
    windowWidget.addWidget(&skinDropDown, Point(coopX, coopY+146), Point(180, 24));

    // set up house choice

    SDL_Texture *pHouseSelect = pGFXManager->getUIGraphic(UI_HouseSelect);
    SDL_Rect dest = calcAlignedDrawingRect(pHouseSelect);
    dest.y = dest.y - getHeight(pHouseSelect)/2 + 10;
    windowWidget.addWidget(&houseChoiceContainer, dest);

    heraldPicture.setTexture(pHouseSelect);
    houseChoiceContainer.addWidget( &heraldPicture, Point(0,0), getTextureSize(pHouseSelect));


    // House1 button
    houseChoiceContainer.addWidget( &house1Picture, Point(21,54), Point(83,91));
    houseChoiceContainer.addWidget( &house1SelectedPicture, Point(20,53), Point(83,91));

    house1Button.setOnClick(std::bind(&SinglePlayerSkirmishMenu::onSelectHouseButton, this, 0));
    houseChoiceContainer.addWidget( &house1Button, Point(20,53), Point(83,91));

    // House2 button
    houseChoiceContainer.addWidget( &house2Picture, Point(117,54), Point(83,91));
    houseChoiceContainer.addWidget( &house2SelectedPicture, Point(116,53), Point(83,91));

    house2Button.setOnClick(std::bind(&SinglePlayerSkirmishMenu::onSelectHouseButton, this, 1));
    houseChoiceContainer.addWidget( &house2Button, Point(116,53), Point(83,91));

    // House3 button
    houseChoiceContainer.addWidget( &house3Picture, Point(215,54), Point(83,91));
    houseChoiceContainer.addWidget( &house3SelectedPicture, Point(214,53), Point(83,91));

    house3Button.setOnClick(std::bind(&SinglePlayerSkirmishMenu::onSelectHouseButton, this, 2));
    houseChoiceContainer.addWidget( &house3Button, Point(214,53), Point(83,91));

    SDL_Texture *pArrowLeft = pGFXManager->getUIGraphic(UI_Herald_ArrowLeft);
    SDL_Texture *pArrowLeftHighlight = pGFXManager->getUIGraphic(UI_Herald_ArrowLeftHighlight);
    houseLeftButton.setTextures(pArrowLeft, pArrowLeft, pArrowLeftHighlight);
    houseLeftButton.setOnClick(std::bind(&SinglePlayerSkirmishMenu::onHouseLeft, this));
    houseLeftButton.setVisible(false);
    houseChoiceContainer.addWidget( &houseLeftButton, Point(houseChoiceContainer.getSize().x/2 - getWidth(pArrowLeft) - 85, 160), getTextureSize(pArrowLeft));

    SDL_Texture *pArrowRight = pGFXManager->getUIGraphic(UI_Herald_ArrowRight);
    SDL_Texture *pArrowRightHighlight = pGFXManager->getUIGraphic(UI_Herald_ArrowRightHighlight);
    houseRightButton.setTextures(pArrowRight, pArrowRight, pArrowRightHighlight);
    houseRightButton.setOnClick(std::bind(&SinglePlayerSkirmishMenu::onHouseRight, this));
    houseChoiceContainer.addWidget( &houseRightButton, Point(houseChoiceContainer.getSize().x/2 + 85, 160), getTextureSize(pArrowRight));

    updateHouseChoice();

    onSelectHouseButton(1);


    // setup +/- Buttons to select misson

    missionCounter.setCount(mission);
    windowWidget.addWidget( &missionCounter,
                            Point(((getRendererWidth()/4)*3 + 160/4) - 83/2,getRendererHeight()/2 + 89),
                            missionCounter.getMinimumSize());



    SDL_Texture* pPlus = pGFXManager->getUIGraphic(UI_Plus);
    SDL_Texture* pPlusPressed = pGFXManager->getUIGraphic(UI_Plus_Pressed);
    missionPlusButton.setTextures(pPlus, pPlusPressed);
    missionPlusButton.setOnClick(std::bind(&SinglePlayerSkirmishMenu::onMissionIncrement, this));
    windowWidget.addWidget( &missionPlusButton,
                            Point(((getRendererWidth()/4)*3 + 160/4) - getWidth(pPlus)/2 + 72,getRendererHeight()/2 + 96),
                            getTextureSize(pPlus));


    SDL_Texture* pMinus = pGFXManager->getUIGraphic(UI_Minus);
    SDL_Texture* pMinusPressed = pGFXManager->getUIGraphic(UI_Minus_Pressed);
    missionMinusButton.setTextures(pMinus, pMinusPressed);
    missionMinusButton.setOnClick(std::bind(&SinglePlayerSkirmishMenu::onMissionDecrement, this));
    windowWidget.addWidget( &missionMinusButton,
                            Point(((getRendererWidth()/4)*3 + 160/4) - getWidth(pMinus)/2 + 72,getRendererHeight()/2 + 109),
                            getTextureSize(pMinus));

}

SinglePlayerSkirmishMenu::~SinglePlayerSkirmishMenu()
{
    ;
}

void SinglePlayerSkirmishMenu::onChildWindowClose(Window* pChildWindow) {
    if(auto* load = dynamic_cast<LoadSaveWindow*>(pChildWindow)) {
        if(load->getFilename().empty()) return;
        try {
            const auto data = readCompleteFile(load->getFilename());
            IMemoryStream stream(data.data(), data.size());
            GameInitSettings::HouseInfoList houses;
            auto saved = GameInitSettings::readSaveSetup(stream, houses);
            if(!isScenarioGameType(saved.getGameType()))
                throw std::runtime_error("Choose a campaign or single-mission save.");
            GameInitSettings init(load->getFilename(), data, settings.general.playerName + "'s Campaign Co-op");
            init.configureCoopSave(saved, houses);
            hostCoopGame(init);
        } catch(const std::exception& error) { openWindow(MsgBox::create(error.what())); }
        return;
    }
    GameOptionsWindow* pGameOptionsWindow = dynamic_cast<GameOptionsWindow*>(pChildWindow);
    if(pGameOptionsWindow != nullptr) {
        currentGameOptions = pGameOptionsWindow->getGameOptions();
        // Choices made here become the new defaults, the same as in Options.
        // Game Rules only persists defaults when the player requests it.
    }
}

void SinglePlayerSkirmishMenu::onStart()
{
    HOUSETYPE houseChoice = static_cast<HOUSETYPE>(houseOrder[currentHouseChoiceScrollPos + selectedButton]);

    supportBotIndex = supportBotDropDown.getSelectedEntryIntData();
    if(supportBotIndex < 0 || supportBotIndex >= kSupportOptionCount) {
        supportBotIndex = 0;
    }

    enemyAIIndex = enemyAIDropDown.getSelectedEntryIntData();
    if(enemyAIIndex < 0 || enemyAIIndex >= kEnemyAIOptionCount) {
        enemyAIIndex = 0;
    }

    const bool supportSelected = (supportBotIndex > 0);
    const char* supportPlayerClass = supportSelected ? kSupportPlayerClasses[supportBotIndex] : nullptr;
    const char* enemyAIClass = kEnemyAIClasses[enemyAIIndex];

    GameInitSettings init(houseChoice, mission, currentGameOptions);
    init.setCampaignGraphicsSkin(GameInitSettings::sanitizeGraphicsSkin(skinDropDown.getSelectedEntryIntData()));
    if(supportSelected) {
        init.setMultiplePlayersPerHouse(true);
    }

    for(int houseID = 0; houseID < NUM_HOUSES; houseID++) {
        if(!isHouseAvailable(static_cast<HOUSETYPE>(houseID))) {
            continue;
        }
        if(houseID == houseChoice) {
            GameInitSettings::HouseInfo humanHouseInfo(static_cast<HOUSETYPE>(houseID), 1);
            humanHouseInfo.addPlayerInfo(GameInitSettings::PlayerInfo(settings.general.playerName, HUMANPLAYERCLASS));

            if(supportSelected && supportPlayerClass != nullptr && *supportPlayerClass != '\0') {
                std::string allyName = getHouseNameByNumber(static_cast<HOUSETYPE>(houseID)) + " " + _("(AI Support)");
                humanHouseInfo.addPlayerInfo(GameInitSettings::PlayerInfo(allyName, supportPlayerClass));
            }

            init.addHouseInfo(humanHouseInfo);
        } else {
            GameInitSettings::HouseInfo aiHouseInfo(static_cast<HOUSETYPE>(houseID), 2);
            aiHouseInfo.addPlayerInfo(GameInitSettings::PlayerInfo(getHouseNameByNumber(static_cast<HOUSETYPE>(houseID)), enemyAIClass));
            init.addHouseInfo(aiHouseInfo);
        }
    }

    if(hostingCoop || campaignCoop) {
        hostCoopGame(init);
    } else {
        startSinglePlayerGame(init);
        quit();
    }
}

void SinglePlayerSkirmishMenu::onHostCoop() {
    hostingCoop = true;
    onStart();
    hostingCoop = false;
}

void SinglePlayerSkirmishMenu::onLoadCoop(bool sharedSave) {
    char path[FILENAME_MAX];
    fnkdat(sharedSave ? "mpsave/" : "save/", path, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);
    openWindow(LoadSaveWindow::create(false, sharedSave ? _("Load Co-op Save") : _("Load Campaign Save"), path, "dls"));
}

void SinglePlayerSkirmishMenu::hostCoopGame(GameInitSettings init) {
    const bool ownNetwork = !pNetworkManager;
    try {
        WorkshopGameContent::resolveMod(init);
        init.enableCoop(campaignCoop, settings.general.playerName + (campaignCoop ? "'s Campaign Co-op" : "'s Mission Co-op"));
        if(init.getGameType() != GameType::LoadCoop) {
            auto file = pFileManager->openCampaignFile(init.getFilename());
            const auto size = SDL_RWsize(file.get());
            if(size <= 0) throw std::runtime_error("Cannot read campaign scenario.");
            std::string data(static_cast<size_t>(size), '\0');
            if(SDL_RWread(file.get(), data.data(), 1, data.size()) != data.size())
                throw std::runtime_error("Incomplete campaign scenario.");
            init.setScenarioData(data);
        }
        if(ownNetwork) pNetworkManager = std::make_unique<NetworkManager>(settings.network.serverPort, settings.network.metaServer);
        CustomGamePlayers(init, true, coopNetworkDropDown.getSelectedEntryIntData() == 1).showMenu();
    } catch(const std::exception& error) {
        openWindow(MsgBox::create(error.what()));
    }
    if(ownNetwork) pNetworkManager.reset();
}

void SinglePlayerSkirmishMenu::onCancel()
{
    quit();
}

void SinglePlayerSkirmishMenu::onSelectHouseButton(int button)
{
    selectedButton = button;

    house1Button.setEnabled( (selectedButton != 0) );
    house2Button.setEnabled( (selectedButton != 1) );
    house3Button.setEnabled( (selectedButton != 2) );

    house1Picture.setVisible( (selectedButton != 0) );
    house2Picture.setVisible( (selectedButton != 1) );
    house3Picture.setVisible( (selectedButton != 2) );

    house1SelectedPicture.setVisible( (selectedButton == 0) );
    house2SelectedPicture.setVisible( (selectedButton == 1) );
    house3SelectedPicture.setVisible( (selectedButton == 2) );
}

void SinglePlayerSkirmishMenu::onHouseLeft()
{
    if(currentHouseChoiceScrollPos > 0) {
        currentHouseChoiceScrollPos--;
        selectedButton++;
        onSelectHouseButton(selectedButton);
        updateHouseChoice();

        houseLeftButton.setVisible( (currentHouseChoiceScrollPos > 0) );
        houseRightButton.setVisible(true);
    }
}

void SinglePlayerSkirmishMenu::onHouseRight()
{
    if(currentHouseChoiceScrollPos < getMaxHouseScrollPos()) {
        currentHouseChoiceScrollPos++;
        selectedButton--;
        onSelectHouseButton(selectedButton);
        updateHouseChoice();

        houseLeftButton.setVisible(true);
        houseRightButton.setVisible( (currentHouseChoiceScrollPos < getMaxHouseScrollPos()) );
    }
}

void SinglePlayerSkirmishMenu::onMissionIncrement()
{
    mission++;
    if(mission > 22) {
        mission = 1;
    }
    missionCounter.setCount(mission);
}

void SinglePlayerSkirmishMenu::onMissionDecrement()
{
    mission--;
    if(mission < 1) {
        mission = 22;
    }
    missionCounter.setCount(mission);
}

void SinglePlayerSkirmishMenu::updateSupportBotLabel()
{
    if(supportBotIndex < 0 || supportBotIndex >= kSupportOptionCount) {
        supportBotIndex = 0;
    }
    if(supportBotDropDown.getNumEntries() > supportBotIndex) {
        supportBotDropDown.setSelectedItem(supportBotIndex);
    }
}

void SinglePlayerSkirmishMenu::onSupportBotSelectionChanged(bool /*interactive*/)
{
    int entry = supportBotDropDown.getSelectedEntryIntData();
    supportBotIndex = (entry >= 0 && entry < kSupportOptionCount) ? entry : 0;
}

void SinglePlayerSkirmishMenu::onEnemyAISelectionChanged(bool /*interactive*/)
{
    int entry = enemyAIDropDown.getSelectedEntryIntData();
    enemyAIIndex = (entry >= 0 && entry < kEnemyAIOptionCount) ? entry : 0;
}

void SinglePlayerSkirmishMenu::onGameOptions()
{
    openWindow(GameOptionsWindow::create(currentGameOptions));
}

void SinglePlayerSkirmishMenu::updateHouseChoice()
{
    // House1 button
    house1Picture.setTexture(pGFXManager->getUIGraphic(UI_Herald_Grey, houseOrder[currentHouseChoiceScrollPos+0]));
    house1SelectedPicture.setTexture(pGFXManager->getUIGraphic(UI_Herald_Colored, houseOrder[currentHouseChoiceScrollPos+0]));

    // House2 button
    house2Picture.setTexture(pGFXManager->getUIGraphic(UI_Herald_Grey, houseOrder[currentHouseChoiceScrollPos+1]));
    house2SelectedPicture.setTexture(pGFXManager->getUIGraphic(UI_Herald_Colored, houseOrder[currentHouseChoiceScrollPos+1]));

    // House3 button
    house3Picture.setTexture(pGFXManager->getUIGraphic(UI_Herald_Grey, houseOrder[currentHouseChoiceScrollPos+2]));
    house3SelectedPicture.setTexture(pGFXManager->getUIGraphic(UI_Herald_Colored, houseOrder[currentHouseChoiceScrollPos+2]));
}
