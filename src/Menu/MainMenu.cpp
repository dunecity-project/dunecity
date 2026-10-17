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

#include <Menu/MainMenu.h>
#include <Menu/PlaySetup.h>
#include <GUI/MsgBox.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/music/MusicPlayer.h>

#include <MapEditor/MapEditor.h>

#include <Menu/SinglePlayerMenu.h>
#include <Menu/CrossplayMenu.h>
#include <Menu/MultiPlayerMenu.h>
#include <Menu/OptionsMenu.h>
#include <Menu/DisplayMenu.h>
#include <misc/MenuLayout.h>
#include <misc/MenuPalette.h>
#include <Menu/ModMenu.h>
#include <Menu/Dune2REditorMenu.h>
#include <Menu/AboutMenu.h>
#include <Menu/HowToPlayMenu.h>

#include <GUI/QstBox.h>
#include <misc/DiscordManager.h>
#include <misc/fnkdat.h>
#include <mod/ModManager.h>
#include <mod/ModInfo.h>
#include <config.h>

#include <cstdio>
#include <cctype>
#include <fstream>
#include <vector>

namespace {
// Marker file under the user config dir. Once written, the first-launch
// "Enable city-sim mod?" prompt is suppressed forever.
std::string firstLaunchMarkerPath() {
    char tmp[FILENAME_MAX];
    if (fnkdat("dunecity-first-launch.done", tmp, FILENAME_MAX,
               FNKDAT_USER | FNKDAT_CREAT) < 0) {
        return std::string();
    }
    return std::string(tmp);
}

bool firstLaunchMarkerExists() {
    const std::string p = firstLaunchMarkerPath();
    if (p.empty()) return true; // fail closed: don't pester
    FILE* f = std::fopen(p.c_str(), "rb");
    if (f == nullptr) return false;
    std::fclose(f);
    return true;
}

void writeFirstLaunchMarker() {
    const std::string p = firstLaunchMarkerPath();
    if (p.empty()) return;
    std::ofstream out(p);
    out << "Dune City " << VERSION << "\n";
}

class ModesMenu final : public MenuBase {
public:
    explicit ModesMenu(bool editors = false) {
        setBackground(pGFXManager->getUIGraphic(UI_MenuBackground));
        resize(getTextureSize(pGFXManager->getUIGraphic(UI_MenuBackground)));
        setWindowWidget(&content);
        title.setText(editors ? _("Editors") : _("Extras"));
        title.setTextFontSize(22);
        title.setAlignment(Alignment_HCenter);
        const int width = std::min(560, getSize().x - 48);
        const int x = (getSize().x - width) / 2;
        const int top = std::max(20, (getSize().y - (editors ? 330 : 280)) / 2);
        content.addWidget(&title, Point(x, top), Point(width, 36));
        const char* editorLabels[] = {"Map Editor", "Mod Editor", "Asset Editors", "Back"};
        const char* extraLabels[] = {"Replays", "How to Play", "About & Credits", "Back"};
        const char* descriptions[] = {
            "Create and edit maps using your chosen mod.",
            "Create mods and change units, buildings, rules and AI.",
            "Preview Dune2R sprites and choose animation settings."
        };
        constexpr int count = 4;
        for(int i = 0; i < count; ++i) {
            buttons[i].setText(_(editors ? editorLabels[i] : extraLabels[i]));
            const int y = top + 48 + i * (editors ? 69 : 48);
            content.addWidget(&buttons[i], Point(x, y), Point(width, 32));
            if(editors && i < count - 1) {
                descriptionsLabels[i].setText(_(descriptions[i]));
                descriptionsLabels[i].setTextFontSize(12);
                descriptionsLabels[i].setAlignment(Alignment_HCenter);
                content.addWidget(&descriptionsLabels[i], Point(x, y + 34), Point(width, 28));
            }
        }
        if(editors) {
            buttons[0].setOnClick([]() { ModMenu(ModMenu::Purpose::MapEditor).showMenu(); });
            buttons[1].setOnClick([]() { ModMenu().showMenu(); });
            buttons[2].setOnClick([]() { ModMenu(ModMenu::Purpose::AssetEditors).showMenu(); });
        } else {
            buttons[0].setOnClick([]() { showGameLibrary(true); });
            buttons[1].setOnClick([]() { HowToPlayMenu().showMenu(); });
            buttons[2].setOnClick([]() { AboutMenu().showMenu(); });
        }
        buttons[count - 1].setOnClick([this]() { quit(); });
        buttons[0].setActive();
    }
private:
    StaticContainer content;
    Label title;
    Label descriptionsLabels[3];
    TextButton buttons[4];
};
} // namespace

std::unique_ptr<MenuBase> createExtrasMenu(bool editors) {
    return std::make_unique<ModesMenu>(editors);
}

bool MainMenu::updatePromptOfferedThisLaunch = false;

MainMenu::MainMenu()
{
    // Update Discord Rich Presence
    DiscordManager::instance().setMainMenu();
    
    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));

    setWindowWidget(&windowWidget);
    enlargedStartMenus = validatedStartMenuMode(settings.video.startMenuMode) == 1;

    canContinue = hasRecentGame();
    continueButton.setText(_("Continue"));
    continueButton.setOnClick([this]() { continueRecentGame(); canContinue = hasRecentGame(); });
    customButton.setText(_("Custom Game"));
    customButton.setOnClick([this]() { playCustomGame(settings.general.customGameOnline); canContinue = hasRecentGame(); });
    onlineButton.setText(_("Play Online"));
    onlineButton.setOnClick([]() { CrossplayMenu().showMenu(); });
    loadButton.setText(_("Load Game"));
    loadButton.setOnClick([this]() { showGameLibrary(); canContinue = hasRecentGame(); });
    campaignButton.setText(_("Campaign"));
    campaignButton.setOnClick([this]() { SinglePlayerMenu::playCampaign(); canContinue = hasRecentGame(); });
    editorsButton.setText(_("Editors"));
    editorsButton.setOnClick([]() { createExtrasMenu(true)->showMenu(); });
    modesButton.setText(_("Extras"));
    modesButton.setOnClick(std::bind(&MainMenu::onModes, this));
    dune2rEditorButton.setText("DUNE2R ASSETS");
    dune2rEditorButton.setOnClick(std::bind(&MainMenu::onDune2REditor, this));
    optionsButton.setText(_("Settings"));
    optionsButton.setOnClick(std::bind(&MainMenu::onOptions, this));
    displayButton.setText(_("DISPLAY"));
    displayButton.setOnClick(std::bind(&MainMenu::onDisplay, this));
    howToPlayButton.setText(_("HOW TO PLAY"));
    howToPlayButton.setOnClick(std::bind(&MainMenu::onHowToPlay, this));
    aboutButton.setText(_("ABOUT"));
    aboutButton.setOnClick(std::bind(&MainMenu::onAbout, this));
    quitButton.setText(_("QUIT"));
    quitButton.setOnClick(std::bind(&MainMenu::onQuit, this));
    SDL_Texture* pPlanet = pGFXManager->getUIGraphic(UI_PlanetBackground);
    SDL_Texture* pLogo = pGFXManager->getUIGraphic(UI_DuneLegacy);
    planetPicture.setTexture(pPlanet);
    logoPicture.setTexture(pLogo);

    if(enlargedStartMenus) {
        const StartMenuLayout layout{getSize().x, getSize().y, 6};
        planetPicture.setFitToSize(true);
        windowWidget.addWidget(&planetPicture, layout.planetBounds());
        logoPicture.setFitToSize(true);
        windowWidget.addWidget(&logoPicture, layout.logoBounds());

        SDL_Texture* pBorder = pGFXManager->getUIGraphic(UI_MenuButtonBorder);
        buttonBorder.setTexture(pBorder);
        buttonBorder.setStretchToSize(true);
        windowWidget.addWidget(&buttonBorder, layout.borderBounds());
    } else {
        SDL_Rect planetBounds = calcAlignedDrawingRect(pPlanet);
        planetBounds.y = planetBounds.y - getHeight(pPlanet) / 2 + 10;
        windowWidget.addWidget(&planetPicture, planetBounds);

        SDL_Rect logoBounds = calcAlignedDrawingRect(pLogo);
        logoBounds.y = logoBounds.y + getHeight(pLogo) / 2 + 28;
        windowWidget.addWidget(&logoPicture, logoBounds);

        SDL_Texture* pBorder = pGFXManager->getUIGraphic(UI_MenuButtonBorder);
        buttonBorder.setTexture(pBorder);
        SDL_Rect borderBounds = calcAlignedDrawingRect(pBorder);
        borderBounds.y = borderBounds.y + getHeight(pBorder) / 2 + 59;
        windowWidget.addWidget(&buttonBorder, borderBounds);

    }
    // Only visible destinations participate in keyboard navigation, in screen order.
    TextButton* allButtons[] = {&continueButton, &onlineButton, &campaignButton, &customButton,
                                &loadButton, &optionsButton, &editorsButton, &modesButton, &quitButton};
    for(TextButton* button : allButtons) {
        button->setKeyboardFocusVisible(false);
        windowWidget.addWidget(button, Point(0, 0), Point(1, 1));
    }
    // The generic product logo must not imply DuneCity rules when Vanilla is active.
    logoPicture.setVisible(false);
    activeModLabel.setTextFontSize(24);
    activeModLabel.setTextColor(MenuTheme::isDesertUISkin()
        ? buttonPalette(settings.video.menuPalette, settings.video.uiSkin).foreground : COLOR_RGB(115,220,210),
        COLOR_TRANSPARENT, COLOR_TRANSPARENT);
    activeModLabel.setAlignment(static_cast<Alignment_Enum>(Alignment_HCenter | Alignment_VCenter));
    windowWidget.addWidget(&activeModLabel, Point(0, 0), Point(1, 1));
    refreshContextButtons();
    modVersionLabel.setTextFontSize(14);
    modVersionLabel.setTextColor(COLOR_WHITE, COLOR_BLACK);
    modVersionLabel.setAlignment(enlargedStartMenus
        ? Alignment_HCenter
        : static_cast<Alignment_Enum>(Alignment_Left | Alignment_VCenter));
    refreshModVersionLabel();
    if(enlargedStartMenus) {
        windowWidget.addWidget(&modVersionLabel, Point(24, getSize().y - 30), Point(getSize().x - 48, 24));
    } else {
        windowWidget.addWidget(&modVersionLabel, Point(12, getSize().y - 58), Point(220, 50));
    }
#if defined(DUNECITY_DESKTOP_UPDATER)
    updateButton.setKeyboardFocusVisible(false);
    updateButton.setText(_("Check for updates"));
    updateButton.setOnClick([this]() { onUpdate(); });
    windowWidget.addWidget(&updateButton, Point(getSize().x-232, getSize().y-38), Point(220,28));
    windowWidget.setWidgetGeometry(&modVersionLabel, Point(12,getSize().y-38), Point(180,28));
    modVersionLabel.setAlignment(Alignment_Left);
#endif
    if(canContinue) continueButton.setActive();
    else onlineButton.setActive();
}

void MainMenu::handleInput(SDL_Event& event)
{
    if (DesktopUpdater::instance().busy()) return;
    if(!pChildWindow && (event.type == SDL_KEYDOWN || event.type == SDL_MOUSEMOTION
                       || event.type == SDL_MOUSEBUTTONDOWN)) {
        const bool keyboard = event.type == SDL_KEYDOWN;
        for(auto* button : {&continueButton, &onlineButton, &campaignButton, &customButton,
                            &loadButton, &optionsButton, &editorsButton, &modesButton, &quitButton, &updateButton}) {
            button->setKeyboardFocusVisible(keyboard);
            if(keyboard) button->handleMouseMovement(-1,-1,false);
        }
    }
    MenuBase::handleInput(event);
}

void MainMenu::refreshModVersionLabel()
{
    std::string activeModName;
    std::string modDisplayName = "Vanilla";
    ModManager& modManager = ModManager::instance();
    if (modManager.isInitialized()) {
        activeModName = modManager.getActiveModName();
        // v1.0.510: defensive null guard. Tornie's ModInfo.displayName was
        // observed empty on some mod bundles (Tornie was registered but
        // the ModInfo was never populated past init). Reading an empty
        // string then concatenating with "\nv" was crashing in some
        // label rendering paths downstream. Fall back to the raw mod
        // name in that case.
        try {
            ModInfo info = modManager.getModInfo(activeModName);
            if (!info.displayName.empty()) {
                modDisplayName = info.displayName + (info.version.empty() ? "" : " " + info.version);
            } else if (!info.name.empty()) {
                modDisplayName = info.name;
            } else if (!activeModName.empty()) {
                modDisplayName = activeModName;
            }
        } catch (const std::exception& e) {
            SDL_Log("MainMenu: refreshModVersionLabel failed: %s — using raw mod name", e.what());
            modDisplayName = activeModName.empty() ? "Unknown" : activeModName;
        }
    }

    if (activeModName == lastShownModName && !modVersionLabel.getText().empty()) {
        return;
    }
    lastShownModName = activeModName;
    try {
        std::transform(modDisplayName.begin(), modDisplayName.end(), modDisplayName.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        const std::string bannerText = "MOD: " + modDisplayName;
        int bannerFontSize = 24;
        const int bannerWidth = std::min(getSize().x - 48, 420);
        while (bannerFontSize > 12 && GUIStyle::getInstance().getMinimumLabelSize(bannerText, bannerFontSize).x > bannerWidth)
            --bannerFontSize;
        activeModLabel.setTextFontSize(bannerFontSize);
        activeModLabel.setText(bannerText);
        modVersionLabel.setText("App v" + std::string(VERSION));
    } catch (const std::exception& e) {
        SDL_Log("MainMenu: setText failed: %s", e.what());
    }
}

MainMenu::~MainMenu() = default;

int MainMenu::showMenu()
{
    int menuResult = -1;
    try {
        musicPlayer->changeMusic(MUSIC_MENU);

#if defined(DUNECITY_DESKTOP_UPDATER)
        auto& updater = DesktopUpdater::instance();
        if (updater.state() == DesktopUpdater::State::Idle) updater.check();
#endif

        menuResult = MenuBase::showMenu();
    } catch(const std::exception& e) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
            "MainMenu::showMenu failed: %s — returning to caller with code -1", e.what());
    } catch(...) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
            "MainMenu::showMenu failed: unknown exception — returning to caller with code -1");
    }
    return menuResult;
}

void MainMenu::update()
{
    // Mod can be switched from any sub-menu (ModMenu, CustomGameMenu,
    // CustomGamePlayers); refresh the watermark on every tick so it
    // tracks the live ModManager state when control returns here.
    refreshModVersionLabel();
    refreshContextButtons();

#if defined(DUNECITY_DESKTOP_UPDATER)
    auto& updater = DesktopUpdater::instance();
    updater.poll();
    using State = DesktopUpdater::State;
    const auto state = updater.state();
    const char* label = state == State::Checking ? "Checking for updates..." :
                        state == State::Installing ? "Installing update..." :
                        state == State::Available ? "Update available" : "Check for updates";
    if (updateButton.getText() != label) updateButton.setText(_(label));
    updateButton.setEnabled(state != State::Checking && state != State::Installing);
    // Native updater dialogs run their own event loop. Keep all game-entry
    // actions disabled for that entire session, including keyboard activation.
    for (auto* button : {&continueButton, &onlineButton, &campaignButton, &customButton,
                         &loadButton, &optionsButton, &editorsButton, &modesButton}) {
        button->setEnabled(!updater.busy() && (button != &continueButton || canContinue));
    }
    if (state == State::Restart) { quit(); return; }
    if (installationRequested && state != State::Installing && !pChildWindow) {
        installationRequested = false;
        if (state == State::Failed) openWindow(MsgBox::create(updater.message()));
    }
    if (manualUpdateCheck && state != State::Checking && state != State::Installing && !pChildWindow) {
        manualUpdateCheck = false;
        if (state == State::Current) openWindow(MsgBox::create(_("Dune City is up to date.")));
        else if (state == State::Failed) openWindow(MsgBox::create(updater.message()));
        else if (state == State::Available) onUpdate();
    }
    showUpdatePromptIfAvailable();
#endif

    // First-launch "Enable city-sim mod?" prompt. Runs after the update
    // dialog so we don't stack two QstBoxes on top of each other.
    showFirstLaunchCityPromptIfNeeded();
}

void MainMenu::onChildWindowClose(Window* pChildWindow)
{
    QstBox* pQstBox = dynamic_cast<QstBox*>(pChildWindow);
    if (pQstBox == nullptr) return;

    if (bFirstLaunchPromptOpen) {
        // This QstBox was the first-launch "Enable city-sim mod?" prompt.
        bFirstLaunchPromptOpen = false;
        writeFirstLaunchMarker(); // record the user's decision either way

        if (pQstBox->getPressedButtonID() == QSTBOX_BUTTON1) {
            ModManager& mm = ModManager::instance();
            if (mm.setActiveMod("dunecity")) {
                // Reinitialize so all subsystems pick up the new mod's
                // ObjectData.ini, QuantBot Config.ini, and game options.
                quit(MENU_QUIT_REINITIALIZE);
            }
        }
        return;
    }

    if (updatePromptOpen) {
        updatePromptOpen = false;
        if (pQstBox->getPressedButtonID() == QSTBOX_BUTTON1) {
            DesktopUpdater::instance().install();
            installationRequested = true;
            manualUpdateCheck = false;
        }
    }
}

void MainMenu::onUpdate() {
    auto& updater = DesktopUpdater::instance();
    if (!updater.supported()) {
#if defined(_WIN32)
        openWindow(MsgBox::create(_("Install the Windows EXE edition once to enable in-game updates. Your saves and settings will be kept.")));
#else
        openWindow(MsgBox::create(_("Use the AppImage edition for in-game updates. For DEB/RPM or other installations, install the new package using your usual method.")));
#endif
        return;
    }
    if (updater.state() == DesktopUpdater::State::Available) {
        updatePromptOpen = true;
        updatePromptOfferedThisLaunch = true;
        openWindow(QstBox::create(_("Install Dune City ") + updater.version() +
            _(" and restart?\n\nYour saves, settings and user mods will be kept."),
            _("Install update"), _("Later"), QSTBOX_BUTTON2));
    } else if (!updater.busy()) {
        updater.check(); manualUpdateCheck = true;
    }
}

void MainMenu::showUpdatePromptIfAvailable()
{
#if defined(DUNECITY_DESKTOP_UPDATER)
    // Offered once per launch: after a "Later" the user is not asked again,
    // not even by a main menu recreated through a mod reinitialize. The
    // update button still reopens the confirmation on demand.
    if (updatePromptOfferedThisLaunch || updatePromptOpen) return;

    auto& updater = DesktopUpdater::instance();
    if (updater.state() != DesktopUpdater::State::Available) return;
    // Packagings we cannot install into (DEB/RPM, portable ZIP) get the
    // explanatory message from the button, never an unsolicited dialog.
    if (!updater.supported()) return;

    // Another dialog owns the screen — openWindow() would close it. Leave the
    // flag clear so the offer simply arrives on a later tick.
    if (pChildWindow != nullptr || !queuedChildWindows.empty()) return;

    onUpdate();
#endif
}

void MainMenu::onModes() const
{
    createExtrasMenu(false)->showMenu();
}

void MainMenu::onDune2REditor() const
{
    ModMenu(ModMenu::Purpose::AssetEditors).showMenu();
}

void MainMenu::refreshContextButtons()
{
    dune2rEditorButton.setEnabled(false);
    howToPlayButton.setEnabled(false);
    aboutButton.setEnabled(false);
    displayButton.setEnabled(false);
    dune2rEditorButton.setVisible(false);
    howToPlayButton.setVisible(false);
    aboutButton.setVisible(false);
    displayButton.setVisible(false);
    continueButton.setVisible(canContinue);
    continueButton.setEnabled(canContinue);
    std::vector<TextButton*> buttons;
    if(canContinue) buttons.push_back(&continueButton);
    for(auto* button : {&onlineButton,&campaignButton,&customButton,&loadButton,&optionsButton,&editorsButton,&modesButton,&quitButton}) buttons.push_back(button);
    const int width = enlargedStartMenus ? 320 : 280;
    const int x = (getSize().x-width)/2;
    const int top = std::max(132, (getSize().y-340)/2);
    planetPicture.setFitToSize(true);
    windowWidget.setWidgetGeometry(&planetPicture, Point((getSize().x-176)/2,top-122), Point(176,80));
    activeModLabel.setTextFontSize(16);
    windowWidget.setWidgetGeometry(&activeModLabel, Point(x-30,top-40), Point(width+60,30));
    buttonBorder.setVisible(false);
    const int gap = 5;
    const int height = std::min(36, (getSize().y-top-48)/static_cast<int>(buttons.size())-gap);
    for(size_t i=0; i<buttons.size(); ++i) {
        windowWidget.setWidgetGeometry(buttons[i], Point(x,top+static_cast<int>(i)*(height+gap)), Point(width,height));
    }
}

void MainMenu::onOptions() {
    OptionsMenu  optionsMenu;
    int ret = optionsMenu.showMenu();

    if(ret == MENU_QUIT_REINITIALIZE) {
        quit(MENU_QUIT_REINITIALIZE);
    }
}

void MainMenu::onDisplay() {
    if(DisplayMenu().showMenu() == MENU_QUIT_REINITIALIZE) quit(MENU_QUIT_REINITIALIZE);
}

void MainMenu::onAbout() const
{
    AboutMenu myAbout;
    myAbout.showMenu();
}

void MainMenu::onHowToPlay() const
{
    HowToPlayMenu menu;
    menu.showMenu();
}

void MainMenu::onQuit() {
    quit();
}

void MainMenu::showFirstLaunchCityPromptIfNeeded()
{
    if (bFirstLaunchPromptChecked) return;
    bFirstLaunchPromptChecked = true;

    // Don't compete with the version-update dialog.
    if (updatePromptOpen || DesktopUpdater::instance().busy() || pChildWindow != nullptr) {
        // Reschedule on next tick by un-flagging.
        bFirstLaunchPromptChecked = false;
        return;
    }

    ModManager& mm = ModManager::instance();
    if (!mm.isInitialized()) return;

    // Already on a city-sim mod -> nothing to prompt.
    if (mm.isCityModeActive()) {
        writeFirstLaunchMarker();
        return;
    }

    // Already shown previously -> respect the user's choice.
    if (firstLaunchMarkerExists()) return;

    // Need the dunecity mod to exist before we can offer to activate it.
    if (!mm.modExists("dunecity")) return;

    bFirstLaunchPromptOpen = true;

    std::string message = _("Welcome to Dune City!");
    message += "\n\n";
    message += _("Build a city on Arrakis with districts, roads,\npower and public services.");
    message += "\n\n";
    message += _("Enable Dune City now?\nYou can change this later in MODS.");

    auto* prompt = QstBox::create(message, _("Enable now"), _("Later"), QSTBOX_BUTTON1);
    openWindow(prompt);
}


