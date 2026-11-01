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

#ifndef GAMEINTERFACE_H
#define GAMEINTERFACE_H

#include <GUI/Window.h>
#include <GUI/HBox.h>
#include <GUI/StaticContainer.h>
#include <GUI/Spacer.h>
#include <GUI/TextButton.h>
#include <GUI/AircraftCountButton.h>
#include <GUI/PictureButton.h>
#include <GUI/PictureLabel.h>
#include <GUI/Label.h>
#include <GUI/dune/ChatManager.h>
#include <GUI/dune/NewsTicker.h>
#include <GUI/dune/DisasterNotification.h>

#include <array>
#include <data.h>
#include <vector>
#include <functional>
#include <climits>

#include <RadarView.h>

class ObjectInterface;
class House;

/// Deliberately distinct from the routine sidebar controls.
class SkipMissionButton : public TextButton {
protected:
    void updateTextures() override;
};

/// This class represents the in-game interface.
class GameInterface : public Window {
public:
    /// default constructor
    GameInterface();
    const SDL_Rect& getMapViewControlsRect() const { return mapViewControlsRect; }

    /// destructor
    virtual ~GameInterface();

    /**
        Draws this window to screen. This method should be called every frame.
        \param  position    Position to draw the window to. The position of the window is added to this.
    */
    void draw(Point position) override;
    /// Draw child dialogs after the HUD, so counters cannot cover their text.
    void drawDialogs();

    /**
        Checks whether the newticker currently shows a message
        \return true if a message is shown, false otherwise
    */
    virtual bool newsTickerHasMessage() {
        return newsticker.hasMessage();
    }

    /**
        This method adds a message to the news ticker
        \param  text    the message to add
    */
    virtual void addToNewsTicker(const std::string& text) {
        newsticker.addMessage(text);
    }

    /**
        This method adds a urgent message to the news ticker
        \param  text    the urgent message to add
    */
    virtual void addUrgentMessageToNewsTicker(const std::string& text) {
        newsticker.addUrgentMessage(text);
    }

    /**
        This method adds a disaster notification
        \param  type            the type of disaster
        \param  message         the notification message
        \param  durationSeconds duration in seconds
        \param  affectedCount   number of zones affected
    */
    virtual void addDisasterNotification(DisasterType type, const std::string& message, int durationSeconds, int affectedCount);

    /**
        Returns the radar view
        \return the radar view
    */
    RadarView& getRadarView() { return radarView; };

    /**
        Returns the chat manager
        \return the chat manager
    */
    ChatManager& getChatManager() { return chatManager; };


    /**
        This method updates the object interface
    */
    virtual void updateObjectInterface();

    /**
        Toggle the city stats HUD overlay
    */
    void toggleCityStatsOverlay() { showCityStatsOverlay = !showCityStatsOverlay; }

private:
    void refreshAircraftButtons();
    void removeOldContainer();
    /// Disables the freshly created object panel while observing, so every panel type is
    /// read-only from one place. See the definition for why disabling the container is enough.
    void makeContainerReadOnlyWhileObserving();
    void drawCityStatsOverlay();
    void updateJoinRequestButton();
    /// Shows a shortcut for each builder type the local house owns below the stacked controls.
    void updateBuilderButtons(bool nothingSelected);


    ObjectInterface*    pObjectContainer;       ///< The container holding information about the currently selected unit/structure
    Uint32              objectID;               ///< The id of the currently selected object

    StaticContainer     windowWidget;           ///< The main widget of this interface

    HBox                topBarHBox;             ///< The container for the top bar containing newsticker, options button and mentat button
    NewsTicker          newsticker;             ///< The newsticker showing news on the game (e.g. new starport prices, harvester fill level, etc.)
    TextButton          pauseButton;
    TextButton          optionsButton;          ///< Button for accessing the ingame menu
    TextButton          mentatButton;           ///< Button for accessing the mentat menu
    TextButton          budgetButton;           ///< City sim mode only: opens the budget mini-window
    TextButton          dune2rZoomButton;       ///< Dune2R only: cycles local presentation zoom
    TextButton          dune2rVisualButton;     ///< Dune2R only: crossfades classic/enhanced visuals
    SDL_Rect            mapViewControlsRect{};  ///< Excluded from touch edge scrolling.
    TextButton          zoomOutButton;          ///< One zoom step out (Game::zoomOut)
    TextButton          zoomInButton;           ///< One zoom step in (Game::zoomIn)
    PictureLabel        topBar;                 ///< The background of the top bar

    PictureLabel        sideBar;                ///< The background of the side bar

    RadarView           radarView;              ///< This is the minimap/radar in the side bar

    ChatManager         chatManager;            ///< Manages chat manages shown overlayed with the main map
    TextButton          feedbackButton;
    TextButton          joinRequestButton;
    bool                joinRequestFlash = false;
    TextButton          autoRepairButton;
    TextButton movementPathsButton;
    TextButton          landValueOverlayButton;
    TextButton          crimeOverlayButton;
    TextButton          pollutionOverlayButton;
    TextButton          statsButton;            ///< Opens the read-only match statistics window
    SkipMissionButton   skipMissionButton;
    AircraftCountButton ornithopterSelectButton;///< Select all controllable, responding ornithopters
    AircraftCountButton chemicalCarryallSelectButton;///< Select all controllable, responding chemical carryalls
    bool                tornieFamilyPresentation = false;
    Uint32              aircraftCountCycle = static_cast<Uint32>(-1);
    const House*        aircraftCountHouse = nullptr;
    /// Builder types with a shortcut on the empty-selection sidebar, in build order.
    static constexpr std::array<int, 9> kBuilderShortcutTypes{
        Structure_ConstructionYard, Structure_Barracks, Structure_WOR, Structure_Worfinery,
        Structure_LightFactory, Structure_HeavyFactory, Structure_HighTechFactory,
        Structure_StarPort, Structure_ChaosFactory};
    std::array<TextButton, kBuilderShortcutTypes.size()> builderButtons; ///< Opens a builder's build menu
    int                 builderRowsTop = 0;     ///< First row below the stacked controls (the skip-mission row)
    int                 builderRowWidth = 0;
    std::array<int, kBuilderShortcutTypes.size()> builderButtonY{}; ///< Current row of each shortcut

    bool                showCityStatsOverlay;   ///< Whether to show the city stats overlay

    Label               modVersionLabel;        ///< High-contrast active match mod/version badge.
    Label               harvesterCountLabel;    ///< Local-house count, independent of watermark visibility
    int                 displayedHarvesterCount = -1;
    Label               populationLabel;        ///< Always-visible "Pop: N" pill (city sim mode only). Refreshed in update().
    Label               rciDemandLabel;         ///< RCI demand readout (city sim mode only), sits just below populationLabel.
    int                 lastShownPopulation = -1; ///< Tracks last value pushed to populationLabel; avoids redundant setText.
    int                 lastShownResValve = INT_MIN; ///< Tracks last RCI snapshot pushed to rciDemandLabel.
    int                 lastShownComValve = INT_MIN;
    int                 lastShownIndValve = INT_MIN;

    std::vector<std::unique_ptr<DisasterNotification>> disasterNotifications_;
};
    #endif // GAMEINTERFACE_H
