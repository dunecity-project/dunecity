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

#ifndef MAINMENU_H
#define MAINMENU_H

#include "MenuBase.h"
#include <GUI/StaticContainer.h>
#include <GUI/VBox.h>
#include <GUI/TextButton.h>
#include <GUI/Spacer.h>
#include <GUI/PictureLabel.h>
#include <GUI/Label.h>
#include <misc/DesktopUpdater.h>

#include <memory>

class MainMenu final : public MenuBase {
public:
    MainMenu();
    virtual ~MainMenu();

    MainMenu(const MainMenu &) = delete;
    MainMenu(MainMenu &&) = delete;
    MainMenu& operator=(const MainMenu &) = delete;
    MainMenu& operator=(MainMenu &&) = delete;

    virtual int showMenu() override;
    virtual void update() override;
    void handleInput(SDL_Event& event) override;
    virtual void onChildWindowClose(Window* pChildWindow) override;

private:
    void onModes() const;
    void onDune2REditor() const;
    void onOptions();
    void onDisplay();
    void onAbout() const;
    void onHowToPlay() const;
    void onQuit();

    /// Show the one-time "Enable Dune City mod?" prompt the first time
    /// the user lands on the main menu and the city-sim mod isn't
    /// already active. Tracked via a marker file in the user config
    /// directory so it only appears once per install.
    void showFirstLaunchCityPromptIfNeeded();

    /// Refresh the footer mod/version label from the current
    /// active mod. Cheap; no-op when the displayed mod hasn't changed.
    void refreshModVersionLabel();
    void refreshContextButtons();

    StaticContainer windowWidget;
    PictureLabel planetPicture;
    PictureLabel logoPicture;
    PictureLabel buttonBorder;

    TextButton      continueButton, customButton, randomSkirmishButton, onlineButton, loadButton;
    bool canContinue = false;
    TextButton      campaignButton;
    TextButton      modesButton;
    TextButton      editorsButton;
    TextButton      dune2rEditorButton;
    bool            enlargedStartMenus = false;
    TextButton      optionsButton;
    TextButton      displayButton;
    TextButton      howToPlayButton;
    TextButton      aboutButton;
    TextButton      quitButton;


    Label           activeModLabel;  ///< Prominent active mod banner above the menu buttons.
    Label           modVersionLabel; ///< Version footer.
    std::string     lastShownModName; ///< Tracks last mod name written to modVersionLabel; avoids redundant setText.

    TextButton updateButton;
    bool updatePromptOpen = false;
    bool manualUpdateCheck = false;
    bool installationRequested = false;
    void onUpdate();

    /// Offer the update confirmation on its own once an update is known to be
    /// available, without waiting for the update button. Deferred while any
    /// other dialog owns the screen; never installs anything by itself.
    void showUpdatePromptIfAvailable();

    /// The confirmation is offered at most once per app launch. A "Later"
    /// must survive main menus recreated by a mod reinitialize, so this
    /// outlives the individual MainMenu instance.
    static bool updatePromptOfferedThisLaunch;

    // First-launch "Enable city-sim mod?" prompt state.
    bool bFirstLaunchPromptChecked = false; ///< Have we evaluated whether to show the prompt this session?
    bool bFirstLaunchPromptOpen    = false; ///< Is the prompt QstBox currently open (so we can route its result)?
};

#endif // MAINMENU_H
