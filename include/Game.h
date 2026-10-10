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

#ifndef GAME_H
#define GAME_H
#include <GUI/ProgressBar.h>

#include <misc/Random.h>
#include <misc/RobustList.h>
#include <misc/InputStream.h>
#include <misc/OutputStream.h>
#include <ObjectData.h>
#include <ObjectManager.h>
#include <CommandManager.h>
#include <Network/MatchControlState.h>
#include <GameInterface.h>
#include <INIMap/INIMapLoader.h>
#include <GameInitSettings.h>
#include <Trigger/TriggerManager.h>
#include <players/Player.h>
#include <players/HumanPlayer.h>
#include <misc/SDL2pp.h>
#include <CursorManager.h>
#include <Network/GameStateDigest.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/CityOverlay.h>
#include <AStarSearch.h>
#include <units/UnitBase.h>

#include <DataTypes.h>
#include <memory>

#include <stdarg.h>
#include <string>
#include <optional>
#include <set>
#include <map>
#include <utility>
#include <deque>
#include <unordered_set>
#include <fstream>
#include <mutex>
#include <cmath>
#include <array>
#include <limits>
#include <vector>

// forward declarations
class ObjectBase;
class InGameMenu;
class MentatHelp;
class WaitingForOtherPlayers;
class ObjectManager;
class House;
class Explosion;
class SpatialGrid;
class UnitBase;
class Carryall;
class MetaServerClient;


#define END_WAIT_TIME               (6*1000)

#define GAME_NOTHING            -1
#define GAME_RETURN_TO_MENU     0
#define GAME_NEXTMISSION        1
#define GAME_LOAD               2
#define GAME_DEBRIEFING_WIN     3
#define GAME_DEBRIEFING_LOST    4
#define GAME_CUSTOM_GAME_STATS  5


namespace CursorAppearance { enum class Action; }

class Game
{
public:
    CursorAppearance::Action getHoverCursorAction() const;

    /**
        Default constructor. Call initGame() or initReplay() afterwards.
    */
    Game();


    Game(const Game& o) = delete;

    /**
        Destructor
    */
    ~Game();

    /**
        Initializes a game with the specified settings
        \param  newGameInitSettings the game init settings to initialize the game
    */
    void initGame(const GameInitSettings& newGameInitSettings);

    /**
        Initializes a replay from the specified filename
        \param  filename    the file containing the replay
    */
    void initReplay(const std::string& filename);



    friend class INIMapLoader; // loading INI Maps is done with a INIMapLoader helper object


    /**
        This method processes all objects in the current game. It should be executed exactly once per game tick.
    */
    void processObjects();

    /**
        This method draws a complete frame.
    */
    void drawScreen();

    /**
        This method proccesses all the user input.
    */
    void doInput();

    /**
        Returns the current game cycle number.
        \return the current game cycle
    */
    Uint32 getGameCycleCount() const { return gameCycleCount; };
    
    /**
        Returns the loaded savegame version (for backward compatibility)
        \return the version of the savegame being loaded, or 0 if not loading
    */
    Uint32 getLoadedSavegameVersion() const { return loadedSavegameVersion; };

    /**
        Hands out the next deviation control-interval id. Deterministic and serialized, so
        every peer and every reload numbers the same captures the same way.
    */
    Uint32 allocateDeviationEpisode() { return nextDeviationEpisodeID++; }
    Uint32 peekDeviationEpisodeCounter() const { return nextDeviationEpisodeID; }

    /**
        Return the game time in milliseconds.
        \return the current game time in milliseconds
    */
    Uint32 getGameTime() const { return gameCycleCount * GAMESPEED_DEFAULT; };

    /**
        Get the command manager of this game
        \return the command manager
    */
    CommandManager& getCommandManager() { return cmdManager; };

    /**
        Get the trigger manager of this game
        \return the trigger manager
    */
    TriggerManager& getTriggerManager() { return triggerManager; };

    /**
        Get the explosion list.
        \return the explosion list
    */
    RobustList<Explosion*>& getExplosionList() { return explosionList; };

    /**
        Returns the house with the id houseID
        \param  houseID the id of the house to return
        \return the house with id houseID
    */
    House* getHouse(int houseID) {
        return house[houseID].get();
    }

    /**
        The current game is finished and the local house has won
    */
    void setGameWon();

    /**
        The current game is finished and the local house has lost
    */
    void setGameLost();

    /**
        Draws the cursor.
    */
    void drawCursor() const;
    void updateCursor();
    void setCursorMode(int mode);

    /**
        This method sets up the view. The start position is the center point of all owned units/structures
    */
    void setupView();

    /**
        This method loads a previously saved game.
        \param filename the name of the file to load from
        \return true on success, false on failure
    */
    bool loadSaveGame(const std::string& filename);

    /**
        This method loads a previously saved game.
        \param stream the stream to load from
        \return true on success, false on failure
    */
    bool loadSaveGame(InputStream& stream);

    /**
        This method saves the current running game.
        \param filename the name of the file to save to
        \return true on success, false on failure
    */
    bool saveGame(const std::string& filename);
    void saveGame(OutputStream& stream);
    struct JoinSlot { int house, controller; std::string label; };
    std::string lastPlayRequestState;
    std::string lastJoinStatus;
    std::vector<JoinSlot> availableJoinSlots() const;
    bool acceptJoinRequest(const std::string& request, const std::string& name, const JoinSlot& slot, bool spectator = false);
    /**
        True only for a *network* observer: this machine's simulation is driven by the host's
        observer stream. Keep every stream, checkpoint and lockstep gate on this predicate -
        a local observer runs the simulation itself and must not be mistaken for one.
    */
    bool isSpectating() const;

    /// True for the Offline Custom Game "Spectate" choice: all-AI match watched locally.
    bool isLocalObserver() const { return localObserver; }

    /**
        Presentation, inspection and input predicate shared by both kinds of observer: full map
        and radar, selection and detail panels for every owner, and no orders. Never use this for
        simulation, stream or save-format decisions.
    */
    bool isObserving() const { return localObserver || isSpectating(); }

    /**
        Whether the single-player "immortal human player" option may protect pLocalHouse.

        The option exists to protect the house the local *human* plays. While observing,
        pLocalHouse is only a view anchor onto a real AI faction, so answering yes there would
        quietly make one AI invulnerable and change the outcome of the match being watched.
        Watching a game must not alter it. Ordinary single-player and shared-house games are
        unaffected, and the network cases never reach these guards at all.
    */
    bool localHumanImmortalityApplies() const;

    void setupSpectatorView();

    /**
        This method starts the game. Will return when the game is finished or aborted.
    */
    void runMainLoop();

    inline void quitGame() { bQuitGame = true;};

    /**
        This method pauses the current game.
    */
    void pauseGame(const char* source = "unspecified");

    /**
        This method resumes the current paused game.
    */
    void resumeGame();

    /**
        This method writes out an object to a stream.
        \param stream   the stream to write to
        \param obj      the object to be saved
    */
    void saveObject(OutputStream& stream, ObjectBase* obj) const;

    /**
        This method loads an object from the stream.
        \param stream   the stream to read from
        \param ObjectID the object id that this unit/structure should get
        \return the read unit/structure
    */
    ObjectBase* loadObject(InputStream& stream, Uint32 objectID);

    inline ObjectManager& getObjectManager() { return objectManager; };
    inline GameInterface& getGameInterface() { return *pInterface; };
    DuneCity::CitySimulation* getCitySimulation() { return citySimulation_.get(); }
    bool isCitySimEnabled() const { return citySimEnabled_; }
    DuneCity::CityOverlayMode getCityOverlayMode() const { return currentCityOverlay_; }
    void setCityOverlayMode(DuneCity::CityOverlayMode mode) { currentCityOverlay_ = mode; }
    void queueTargetRequest(Uint32 objectId);
    void queuePathRequest(Uint32 objectId);
    inline size_t getPathRequestQueueSize() const { return pathRequestQueue.size(); }

    /**
        The object ids of every carryall currently in unitList, in unitList order.

        A derived index, not a ledger: creation/removal invalidates it; it is also rebuilt when
        the simulation cycle or unit count changes. Every reader validates each id against the object
        manager anyway. Nothing about a carryall's booking, ownership, health or activity is
        cached here, so no event can leave it describing a carryall that no longer matches
        reality - the worst a stale entry can do is cost one failed lookup.

        It exists so that asking "is any of my carryalls free" costs a scan of the carriers
        instead of a scan of every unit in the game. Preserving unitList order preserves the
        existing deterministic first-free selection exactly.

        Never serialised, and holds ids rather than pointers, so it cannot dangle.
    */
    const std::vector<Uint32>& getCarryallCandidateIds();

    /// Forces the next getCarryallCandidateIds() to rebuild. Used where a cycle can both create
    /// and remove carriers, and after an observer checkpoint replaces the unit set.
    void invalidateCarryallCandidateIds();

    /**
        Reconciles units hidden off the map against the containers that actually hold them.

        Containment is read from the carriers' cargo lists and the structures' occupant pointers,
        across every house, never from a passenger's own target or carrier booking: those are the
        fields a pickup can rewrite, which is how a unit ended up marked as carried by nothing,
        invisible and untargetable, while still counting for its house. A hidden unit no container
        claims is put back on the map at a legal tile near where it was lost, with its stale
        bookings released on both sides; cargo entries that cannot describe a live passenger any
        more, and a second carrier's claim on a passenger another carrier already holds, are
        dropped. A drop-off or repair bay counts only while it is actually busy, because a
        released refinery keeps pointing at the harvester it let go.

        One pass scans units and structures, sorts hidden candidate ids, and performs a bounded
        amount of recovery work. It runs on a
        fixed cycle cadence and reads nothing but simulation state, so a continuous match, an
        ordinary load, an observer restore and a replay all reconcile at exactly the same cycles.
        Nothing here is serialised.
    */
    void reconcileTransportContainment();

    /// Cycles between containment passes. A hidden unit no container holds is recovered within
    /// this many cycles of becoming lost, or at a later pass if it has to wait for a tile or for
    /// its turn in the batch.
    static constexpr Uint32 transportContainmentIntervalCycles = 625;

    /// Hidden units this match has put back on the map; hidden units the last pass tried and could
    /// not place anywhere yet; and hidden units still waiting, including those the last pass's
    /// bounded batch did not reach. Diagnostics only: derived, not saved, and nothing in the
    /// simulation reads them.
    Uint32 getRecoveredPassengerCount() const { return recoveredPassengerCount; }
    Uint32 getUnplaceablePassengerCount() const { return unplaceablePassengerCount; }
    Uint32 getPendingPassengerCount() const { return pendingPassengerCount; }
    inline bool isPathQueueStressed() const { return pathRequestQueue.size() > 300; }
    SpatialGrid* getSpatialGrid() const { return spatialGrid.get(); }
    void initializeSpatialGrid(int mapWidth, int mapHeight);

    const GameInitSettings& getGameInitSettings() const { return gameInitSettings; };
    void setNextGameInitSettings(const GameInitSettings& nextGameInitSettings) { this->nextGameInitSettings = nextGameInitSettings; };

    /**
        This method should be called if whatNext() returns GAME_NEXTMISSION or GAME_LOAD. You should
        destroy this Game and create a new one. Call Game::initGame() with the GameInitClass
        that was returned previously by getNextGameInitSettings().
        \return a GameInitSettings-Object that describes the next game.
    */
    GameInitSettings getNextGameInitSettings();

    /**
        This method should be called after startGame() has returned. whatNext() will tell the caller
        what should be done after the current game has finished.<br>
        Possible return values are:<br>
        GAME_RETURN_TO_MENU  - the game is finished and you should return to the main menu<br>
        GAME_NEXTMISSION     - the game is finished and you should load the next mission<br>
        GAME_LOAD            - from inside the game the user requests to load a savegame and you should do this now<br>
        GAME_DEBRIEFING_WIN  - show debriefing (player has won) and call whatNext() again afterwards<br>
        GAME_DEBRIEFING_LOST - show debriefing (player has lost) and call whatNext() again afterwards<br>
        <br>
        \return one of GAME_RETURN_TO_MENU, GAME_NEXTMISSION, GAME_LOAD, GAME_DEBRIEFING_WIN, GAME_DEBRIEFING_LOST
    */
    int whatNext();

    /**
        This method is the callback method for the OPTIONS button at the top of the screen.
        It pauses the game and loads the in game menu.
    */
    void onOptions();
    void onJoinRequests();

    /**
        This method is the callback method for the MENTAT button at the top of the screen.
        It pauses the game and loads the mentat help screen.
    */
    void onMentat();

    /**
        This method opens the city budget window. An observer gets the same window
        in read-only form; a participant gets their own house as before.
    */
    void onCityBudget();
    /**
        Opens the read-only match statistics window: the house summary, the
        per-unit-type combat ledger and any QuantBot allocation snapshot. Issues
        no command and is available in every game mode, including while observing.
    */
    void onHouseStats();
    void onFeedback();
    void onSkipMission();
    void confirmSkipMission();
    void drawMovementPaths();
    void toggleMovementPaths();
    bool canSkipMission() const;

    /**
        Cycles the local Dune2R presentation zoom without changing game state.
    */
    void cycleDune2RZoom();

    /**
        Crossfades local presentation between classic and Dune2R visuals.
    */
    void toggleDune2RVisuals();

    /**
        This method selects all units/structures in the list aList.
        \param aList the list containing all the units/structures to be selected
    */
    void selectAll(const std::set<Uint32>& aList);

    /**
        Selects all currently active ornithopters owned by the local house.
    */
    void selectAllOrnithopters();
    void selectAllChemicalCarryalls();

    /**
        This method unselects all units/structures in the list aList.
        \param aList the list containing all the units/structures to be unselected
    */
    void unselectAll(const std::set<Uint32>& aList);

    /**
        Returns a list of all currently selected objects.
        \return list of currently selected units/structures
    */
    std::set<Uint32>& getSelectedList() { return selectedList; };

    /**
        Marks that the selection changed (and must be retransmitted to other players in multiplayer games)
    */
    inline void selectionChanged() {
        bSelectionChanged = true;
        if(pInterface) {
            pInterface->updateObjectInterface();
        }
        pLocalPlayer->onSelectionChanged(selectedList);
    };


    void onReceiveSelectionList(const std::string& name, const std::set<Uint32>& newSelectionList, int groupListIndex);

    /**
        Returns a list of all currently by  the other player selected objects (Only in multiplayer with multiple players per house).
        \return list of currently selected units/structures by the other player
    */
    std::set<Uint32>& getSelectedByOtherPlayerList() { return selectedByOtherPlayerList; };

    /**
        Called when a peer disconnects the game.
    */
    void onPeerDisconnected(const std::string& name, bool bHost, int cause);

    /**
        Adds a new message to the news ticker.
        \param  text    the text to add
    */
    void addToNewsTicker(const std::string& text) {
        if(pInterface != nullptr) {
            pInterface->addToNewsTicker(text);
        }
    }

    /**
        Adds an urgent message to the news ticker.
        \param  text    the text to add
    */
    void addUrgentMessageToNewsTicker(const std::string& text) {
        if(pInterface != nullptr) {
            pInterface->addUrgentMessageToNewsTicker(text);
        }
    }

    // Disaster notification triggers
    void triggerFireDisaster();
    void triggerSandstormDisaster();
    void triggerSandwormDisaster();

    // Fire spread from combat
    void updateFireSpread();
    struct CoordLess {
        bool operator()(const Coord& a, const Coord& b) const {
            return (a.x < b.x) || (a.x == b.x && a.y < b.y);
        }
    };
    std::map<Coord, Uint32, CoordLess> fireLocations_;  ///< Active fire locations: tile coord -> last spread cycle

    /**
        This method returns wether the game is currently paused
        \return true, if paused, false otherwise
    */
    bool isGamePaused() const { return bPause || matchControl.pausedAt(gameCycleCount); };

    /**
        This method returns wether the game is finished
        \return true, if paused, false otherwise
    */
    bool isGameFinished() const { return finished; };

    /**
        Are cheats enabled?
        \return true = cheats enabled, false = cheats disabled
    */
    bool areCheatsEnabled() const { return bCheatsEnabled; };

    /**
        Returns the name of the local player; this method should be used instead of using settings.general.playerName directly
        \return the local player name
    */
    const std::string& getLocalPlayerName() const { return localPlayerName; }

    /**
        Register a new player in this game.
        \param  player      the player to register
    */
    void registerPlayer(Player* player) {
        playerName2Player.insert( std::make_pair(player->getPlayername(), player) );
        playerID2Player.insert( std::make_pair(player->getPlayerID(), player) );
    }

    /**
        Unregisters the specified player
        \param  player
    */
    void unregisterPlayer(Player* player) {
        auto id=playerID2Player.find(player->getPlayerID());
        if(id!=playerID2Player.end() && id->second==player) playerID2Player.erase(id);

        for(auto iter = playerName2Player.begin(); iter != playerName2Player.end(); ++iter) {
                if(iter->second == player) {
                    playerName2Player.erase(iter);
                    break;
                }
        }
    }

    /**
        Returns the first player with the given name.
        \param  playername  the name of the player
        \return the player or nullptr if none was found
    */
    Player* getPlayerByName(const std::string& playername) const {
        auto iter = playerName2Player.find(playername);
        if(iter != playerName2Player.end()) {
            return iter->second;
        } else {
            return nullptr;
        }
    }

    /**
        Returns the player with the given id.
        \param  playerID  the name of the player
        \return the player or nullptr if none was found
    */
    Player* getPlayerByID(Uint8 playerID) const {
        auto iter = playerID2Player.find(playerID);
        if(iter != playerID2Player.end()) {
            return iter->second;
        } else {
            return nullptr;
        }
    }

    /**
        This function is called when the user left clicks on the radar
        \param  worldPosition       position in world coordinates
        \param  bRightMouseButton   true = right mouse button, false = left mouse button
        \param  bDrag               true = the mouse was moved while being pressed, e.g. dragging
        \return true if dragging should start or continue
    */
    bool onRadarClick(Coord worldPosition, bool bRightMouseButton, bool bDrag);

    /**
        Take a screenshot and save it with a unique name
    */
    void takeScreenshot() const;

    /**
        Draw the city data overlay for the visible map area
    */
    void drawCityOverlay(int x1, int y1, int x2, int y2);

    /**
        Draw placeholder visuals for conductive city roads.
    */
    void drawCityRoads(int x1, int y1, int x2, int y2);

    /**
        Draw the active city placement hint.
    */
    void drawCityPlacementHint();
private:

    void startMatchAnalytics();
    /// Offer a started offline custom match's scenario to the community catalogue.
    void collectCustomMap(bool loadedSave);
    void finishMatchAnalytics();
    std::string buildMatchAnalyticsPayload(bool finishedMatch) const;

    void applyDune2RZoom(int zoomLevel);

    /**
        Checks whether the cursor is on the radar view
        \param  mouseX  x-coordinate of cursor
        \param  mouseY  y-coordinate of cursor
        \return true if on radar view
    */
    bool isOnRadarView(int mouseX, int mouseY) const;

    /**
        Handles the press of one key while chatting
        \param  key the key pressed
    */
    void handleChatInput(SDL_KeyboardEvent& keyboardEvent);

    /**
        Handles the press of one key
        \param  key the key pressed
    */
    void handleKeyInput(SDL_KeyboardEvent& keyboardEvent);

    /**
        Performs a building placement
        \param  xPos    x-coordinate in map coordinates
        \param  yPos    x-coordinate in map coordinates
        \return true if placement was successful
    */
    bool handlePlacementClick(int xPos, int yPos);

    /**
        Performs a attack click for the currently selected units/structures.
        \param  xPos    x-coordinate in map coordinates
        \param  yPos    x-coordinate in map coordinates
        \return true if attack is possible
    */
    bool handleSelectedObjectsAttackClick(int xPos, int yPos);
    bool handleSelectedObjectsHealClick(int xPos, int yPos);

    /**
        Performs a move click for the currently selected units/structures.
        \param  xPos    x-coordinate in map coordinates
        \param  yPos    x-coordinate in map coordinates
        \return true if move is possible
    */
    bool handleSelectedObjectsMoveClick(int xPos, int yPos);

    /**
        Performs a capture click for the currently selected units/structures.
        \param  xPos    x-coordinate in map coordinates
        \param  yPos    x-coordinate in map coordinates
        \return true if capture is possible
    */
    bool handleSelectedObjectsCaptureClick(int xPos, int yPos);

    /**
        Handles a city zone placement click.
        \param  xPos    x-coordinate in map coordinates
        \param  yPos    y-coordinate in map coordinates
    */
    void handleCityZonePlacementClick(int xPos, int yPos);
    void handleCityRoadPlacementClick(int xPos, int yPos);
    /**
        Performs a request carryall click for the currently selected units.
        \param  xPos    x-coordinate in map coordinates
        \param  yPos    x-coordinate in map coordinates
        \return true if carryall drop is possible
    */
    bool handleSelectedObjectsRequestCarryallDropClick(int xPos, int yPos);


    /**
        Performs an action click for the currently selected units/structures.
        \param  xPos    x-coordinate in map coordinates
        \param  yPos    x-coordinate in map coordinates
        \return true if action click is possible
    */
    bool handleSelectedObjectsActionClick(int xPos, int yPos);


    /**
        Selects the next structure of any of the types specified in itemIDs. If none of this type is currently selected the first one is selected.
        \param  itemIDs  the ids of the structures to select
    */
    void selectNextStructureOfType(const std::set<Uint32>& itemIDs);

    /**
        Returns the game speed of this game: The number of ms per game cycle.
        For singleplayer games this is a global setting (but can be adjusted in the in-game settings menu). For multiplayer games the game speed
        can be set by the person creating the game.
        \return the current game speed
    */
public:
    int getGameSpeed() const;
    bool canChangeGameSettings() const;
    bool requestGameSpeed(int speed);
    bool canToggleMatchPause() const;
    bool isPauseRequestPending() const { return pauseRequestPending; }
    void toggleMatchPause();
    void executeMatchPause(Uint8 issuer, Uint32 requestCycle);
    void handleMatchControl(Uint32 revision, Uint32 speed, Uint32 pauseCycle, Uint32 resumedCycle);
    void handleMatchResumeRequest(const std::string& player, Uint32 pauseCycle);
    void publishMatchControl();


public:
    enum {
        CursorMode_Normal,
        CursorMode_Attack,
        CursorMode_Heal,
        CursorMode_Move,
        CursorMode_Capture,
        CursorMode_CarryallDrop,
        CursorMode_Placing,
        CursorMode_CityZone,
        CursorMode_CityRoad
    };

    int         currentCursorMode = CursorMode_Normal;
    int         lastRoadTileX = -1;
    int         lastRoadTileY = -1;
    DuneCity::ZoneType selectedZoneType_ = DuneCity::ZoneType::Residential;

    GameType    gameType = GameType::Campaign;
    int         techLevel = 0;
    int         winFlags = 0;
    int         loseFlags = 0;

    Random      randomGen;          ///< This is the random number generator for this game
    ObjectData  objectData;         ///< This contains all the unit/structure data

    GameState   gameState = GameState::Start;
    
    std::set<Uint8> pausedPlayers;  ///< Set of player IDs that are currently paused

    // Running statistics for confidence intervals (Welford's algorithm)
    struct RunningStats {
        uint64_t sampleCount = 0;
        double mean = 0.0;
        double m2 = 0.0;  // Sum of squared differences from mean

        void reset() {
            sampleCount = 0;
            mean = 0.0;
            m2 = 0.0;
        }

        void add(double value) {
            ++sampleCount;
            double delta = value - mean;
            mean += delta / static_cast<double>(sampleCount);
            double delta2 = value - mean;
            m2 += delta * delta2;
        }

        double variance() const {
            return (sampleCount > 1) ? (m2 / static_cast<double>(sampleCount - 1)) : 0.0;
        }

        double standardError() const {
            if(sampleCount <= 1) {
                return 0.0;
            }
            return std::sqrt(variance() / static_cast<double>(sampleCount));
        }

        double ci95() const {
            if(sampleCount <= 1) {
                return 0.0;
            }
            return 1.96 * standardError();  // 95% confidence interval
        }
    };

    // Performance timing system (public for access from objects like turrets)
    struct FrameTiming {
        double aiMs = 0.0;
        double unitsMs = 0.0;
        double structuresMs = 0.0;
        double pathfindingMs = 0.0;
        double renderingMs = 0.0;
        double networkWaitMs = 0.0;
        double totalMs = 0.0;
        int gameCyclesThisFrame = 0;
        int totalGameCycles = 0;
        int frameCount = 0;
        
        // Pathfinding detailed stats
        int pathsProcessedThisCycle = 0;
        int totalPathsProcessedThisFrame = 0;
        int totalPathsProcessed = 0;
        int pathsFailedThisFrame = 0;
        int totalPathsFailed = 0;
        double pathfindingMsThisCycle = 0.0;
        double pathfindingMsThisFrame = 0.0;
        
        // Phase 1: Token/node tracking
        size_t pathTokensThisCycle = 0;
        size_t pathTokensThisFrame = 0;
        size_t totalPathTokens = 0;
        size_t maxPathTokensPerCycle = 0;
        size_t maxPathTokensPerFrame = 0;
        RunningStats pathsPerCycleStats;
        RunningStats pathTokensPerCycleStats;
        RunningStats tokensPerCompletedPathStats;
        RunningStats tokensPerFailedPathStats;
        size_t minTokensPerCompletedPath = SIZE_MAX;
        size_t maxTokensPerCompletedPath = 0;
        size_t minTokensPerFailedPath = SIZE_MAX;
        size_t maxTokensPerFailedPath = 0;
        std::array<uint64_t, 7> pathTokenHistogram{};  // Buckets for token distribution
        int maxPathQueueLength = 0;
        int pathBudgetStarvedFrames = 0;
        size_t pathReuseHitsWindow = 0;
        size_t pathReuseMissesWindow = 0;
        size_t totalPathReuseHits = 0;
        size_t totalPathReuseMisses = 0;
        
        // Path invalidation reasons (for debugging)
        size_t pathInvalidDestChanged = 0;
        size_t pathInvalidHuntTooFar = 0;
        size_t pathInvalidBlocked = 0;
        
        // Movement pause reasons (for debugging stuttering)
        uint64_t pauseWaitingForPath = 0;      // Path queued, waiting for result
        uint64_t pauseWaitingForBlocker = 0;   // Waiting for moving unit to clear
        uint64_t pauseRecalcCooldown = 0;      // recalculatePathTimer > 0
        uint64_t pauseTurningToFace = 0;       // Turning to face next waypoint
        
        // Phase 2: Token budget tracking
        int tokenBudgetExhaustedCount = 0;  // How often we hit 20k token limit
        int timeBudgetExceededCount = 0;    // How often time limit hit (should be rare)
        double avgTokensUsedPerCycle = 0.0;  // Running average
        
        // Turret target scan detailed stats
        int turretScansThisFrame = 0;
        int totalTurretScans = 0;
        double turretScanMs = 0.0;
        double turretScanMsThisFrame = 0.0;
        double maxTurretScanMs = 0.0;
        double minTurretScanMs = 999999.0;
        int maxTurretScansPerFrame = 0;
        
        // Per-frame accumulators for min/max tracking
        double aiMsThisFrame = 0.0;
        double aiWorstHouseMsThisFrame = 0.0;
        int    aiWorstHouseIdxThisFrame = -1;
        double citySimMsThisFrame = 0.0;
        double unitsMsThisFrame = 0.0;
        double structuresMsThisFrame = 0.0;
        double renderingMsThisFrame = 0.0;
        double networkWaitMsThisFrame = 0.0;
        
        // Max values
        double maxAiMs = 0.0;
        double maxUnitsMs = 0.0;
        double maxStructuresMs = 0.0;
        double maxPathfindingMs = 0.0;
        double maxPathfindingMsPerCycle = 0.0;
        double maxRenderingMs = 0.0;
        double maxNetworkWaitMs = 0.0;
        double maxTotalMs = 0.0;
        int maxGameCyclesPerFrame = 0;
        int maxPathsPerCycle = 0;
        int maxPathsPerFrame = 0;
        
        // Min values
        int minGameCyclesPerFrame = 999999;
        double minAiMs = 999999.0;
        double minUnitsMs = 999999.0;
        double minStructuresMs = 999999.0;
        double minPathfindingMs = 999999.0;
        double minRenderingMs = 999999.0;
        double minNetworkWaitMs = 999999.0;
        
        // Detailed unit timing breakdown
        double unitTargetingMs = 0.0;
        double unitNavigateMs = 0.0;
        double unitMoveMs = 0.0;
        double unitTurnMs = 0.0;
        double unitVisibilityMs = 0.0;
        double unitTargetingMsThisFrame = 0.0;
        double unitNavigateMsThisFrame = 0.0;
        double unitMoveMsThisFrame = 0.0;
        double unitTurnMsThisFrame = 0.0;
        double unitVisibilityMsThisFrame = 0.0;
        int unitCount = 0;
        
        // Simulation timing for CPU load detection (post-vsync)
        double simMsAvg = 0.0;              // Exponential moving average of simulation time per tick
        bool simulationLagging = false;     // Flag: true when simMsAvg exceeds high threshold
    };

    FrameTiming frameTiming;
    
    // Rocket Turret vs Ornithopter Statistics
    struct CombatStats {
        // Rocket Turret stats
        int rocketTurretTargetsOrni = 0;      // Turret acquires ornithopter as target
        int rocketTurretLosesOrniTarget = 0;  // Turret loses ornithopter target
        int orniInRangeButWrongAngle = 0;     // Ornithopter in range but turret not aimed
        int orniInRangeCorrectAngle = 0;      // Ornithopter in range and turret aimed correctly
        int rocketTurretFiresOnOrni = 0;      // Turret actually fires rocket at ornithopter
        int rocketTurretFireBlocked = 0;      // Weapon timer not ready
        int turretRocketsSpawned = 0;         // Total turret rockets created
        int turretRocketsHitOrni = 0;         // Rockets that damaged ornithopter
        int turretRocketsKillOrni = 0;        // Rockets that killed ornithopter
        int turretRocketsExpired = 0;         // Rockets that expired (timer)
        int turretRocketsProximityDetonated = 0; // Rockets detonated via proximity
        
        // Launcher Unit stats (Rocket Launcher + Deviator)
        int launcherTargetsOrni = 0;          // Launcher acquires ornithopter as target
        int launcherFiresOnOrni = 0;          // Launcher fires at ornithopter
        int launcherRocketsSpawned = 0;       // Total launcher rockets created
        int launcherRocketsHitOrni = 0;       // Launcher rockets that damaged ornithopter
        int launcherRocketsKillOrni = 0;      // Launcher rockets that killed ornithopter
        int launcherRocketsExpired = 0;       // Launcher rockets that expired (timer)
        
        Uint32 lastDumpCycle = 0;             // MULTIPLAYER FIX (Issue #8): Cycle-based (was lastDumpTime)
    };
    
    CombatStats combatStats;
    
    inline double getElapsedMs(Uint64 start, Uint64 end) const {
        const Uint64 frequency = SDL_GetPerformanceFrequency();
        return (static_cast<double>(end - start) / static_cast<double>(frequency)) * 1000.0;
    }
    
    void dumpCombatStats();  // Dump combat statistics
    void logPathInstrumentationIfNeeded();

private:
    void applyDuneCityGraphicsSkins();

    bool        chatMode = false;   ///< chat mode on?
    std::string typingChatMessage;  ///< currently typed chat message

    bool        scrollDownMode = false;     ///< currently scrolling the map down?
    bool        scrollLeftMode = false;     ///< currently scrolling the map left?
    bool        scrollRightMode = false;    ///< currently scrolling the map right?
    bool        scrollUpMode = false;       ///< currently scrolling the map up?

    bool        selectionMode = false;          ///< currently selection multiple units with a selection rectangle?
    SDL_Rect    selectionRect = {0, 0, 0, 0};   ///< the drawn rectangle while selection multiple units

    int         whatNextParam = GAME_NOTHING;

    Uint32      actionIndicatorObject = NONE_ID;
    Uint32      actionIndicatorUntil = 0;
    Uint32      indicatorFrame = NONE_ID;
    int         indicatorTime = 5;
    int         indicatorTimer = 0;
    Coord       indicatorPosition = Coord::Invalid();

    float       averageFrameTime = 31.25f;      ///< The weighted average of the frame time of all previous frames (smoothed fps = 1000.0f/averageFrameTime)

    Uint32      gameCycleCount = 0;

    Uint32      skipToGameCycle = 0;            ///< skip to this game cycle
    Uint32      lastPathInstrumentationLogCycle = 0;
    
    Uint32      loadedSavegameVersion = 0;      ///< Version of loaded savegame (for backward compatibility)
    /// Next deviation control-interval id; 0 is reserved for "not deviated" (SAVEGAMEVERSION 9849).
    Uint32      nextDeviationEpisodeID = 1;

    bool        takePeriodicalScreenshots = false;      ///< take a screenshot every 10 seconds

    SDL_Rect    powerIndicatorPos = {14, 146, 4, 0};    ///< position of the power indicator in the right game bar
    SDL_Rect    spiceIndicatorPos = {20, 146, 4, 0};    ///< position of the spice indicator in the right game bar
    SDL_Rect    topBarPos = {0, 0, 0, 0};               ///< position of the top game bar
    SDL_Rect    sideBarPos = {0, 0, 0, 0};              ///< position of the right side bar

    ////////////////////

    GameInitSettings                    gameInitSettings;       ///< the init settings this game was started with
    GameInitSettings                    nextGameInitSettings;   ///< the init settings the next game shall be started with (restarting the mission, loading a savegame)
    GameInitSettings::HouseInfoList     houseInfoListSetup;     ///< this saves with which houses and players the game was actually set up. It is a copy of gameInitSettings::houseInfoList but without random houses

    // A best-effort, out-of-band reporter. It never participates in lockstep
    // state and is only created by the multiplayer host or a single-player game.
    std::unique_ptr<MetaServerClient> matchAnalyticsClient;
    std::string matchAnalyticsID;
    bool matchAnalyticsStarted = false;


    std::unique_ptr<SpatialGrid>    spatialGrid;            ///< Spatial partition for fast proximity queries
    std::unique_ptr<DuneCity::CitySimulation> citySimulation_; ///< City-building simulation layer
    bool                citySimEnabled_ = false;             ///< Feature flag for city simulation. Defaults off; enabled when the active mod opts into city mode (see ModManager::isCityModeActive) or when a savegame contains city-sim state.
    DuneCity::CityOverlayMode currentCityOverlay_ = DuneCity::CityOverlayMode::None; ///< Current city data overlay mode

    ObjectManager       objectManager;          ///< This manages all the object and maps object ids to the actual objects

    CommandManager      cmdManager;             ///< This is the manager for all the game commands (e.g. moving a unit)

    CursorManager       cursorManager;          ///< This manages all hardware cursors

    TriggerManager      triggerManager;         ///< This is the manager for all the triggers the scenario has (e.g. reinforcements)

    bool    bQuitGame = false;                  ///< Should the game be quited after this game quit
    MatchControlState matchControl;
    bool pauseRequestPending = false;
    struct MenuPause {
        Uint32 requestCycle;
        Uint32 pauseCycle = 0;
        bool closed = false;
    };
    std::optional<MenuPause> menuPause; // Host UI ownership; never serialized as world state.
    Uint32 lastMatchControlBroadcast = 0;
    bool    bPause = false;                     ///< Is the game currently halted
    bool    bMenu = false;                      ///< Is there currently a menu shown (options or mentat menu)
    std::unique_ptr<HumanPlayer> spectatorViewPlayer; // UI identity only; never registered or saved.
    /// Offline Custom Game observation. Set from GameInitSettings::isLocalSpectator() on a new
    /// game and from the reserved local-player byte on load; never a network property.
    bool localObserver = false;
    sdl2::texture_ptr spectatorLabel;
    bool    bReplay = false;                    ///< Is this game actually a replay

    bool    bShowFPS = false;                   ///< Show the FPS

    bool    bShowTime = false;                  ///< Show how long this game is running

    bool    bCheatsEnabled = false;             ///< Cheat codes are enabled?

    bool    finished = false;                   ///< Is the game finished (won or lost) and we are just waiting for the end message to be shown
    bool    won = false;                        ///< If the game is finished, is it won or lost
    Uint32  finishedLevelCycle = 0;             ///< MULTIPLAYER FIX (Issue #9): Cycle-based (was finishedLevelTime)
    bool    finishedLevel = false;              ///< Set, when the game is really finished and the end message was shown

    std::unique_ptr<GameInterface>          pInterface;                             ///< This is the whole interface (top bar and side bar)
    std::unique_ptr<Window>                 pInGameMenu;                            ///< This is the menu that is opened by the option button
    std::unique_ptr<MentatHelp>             pInGameMentat;                          ///< This is the mentat dialog opened by the mentat button
    std::unique_ptr<WaitingForOtherPlayers> pWaitingForOtherPlayers;                ///< This is the dialog that pops up when we are waiting for other players during network hangs
    Uint32                                  startWaitingForOtherPlayersTime = 0;    ///< The time in milliseconds when we started waiting for other players

    bool    bSelectionChanged = false;                  ///< Has the selected list changed (and must be retransmitted to other plays in multiplayer games)
    std::set<Uint32> selectedList;                      ///< A set of all selected units/structures
    std::set<Uint32> selectedByOtherPlayerList;         ///< This is only used in multiplayer games where two players control one house
    RobustList<Explosion*> explosionList;               ///< A list containing all the explosions that must be drawn

    std::string localPlayerName;                            ///< the name of the local player
    std::multimap<std::string, Player*> playerName2Player;  ///< mapping player names to players (one entry per player)
    std::map<Uint8, Player*> playerID2Player;               ///< mapping player ids to players (one entry per player)

    std::array<std::unique_ptr<House>, NUM_HOUSES> house;   ///< All the houses of this game, index by their houseID; has the size NUM_HOUSES; unused houses are nullptr

    struct TargetRequest {
        Uint32 objectId;
    };

    struct PathRequest {
        Uint32 objectId;
    };

    /// Containment reconciliation results. Derived diagnostics; never serialised.
    Uint32 recoveredPassengerCount = 0;
    Uint32 unplaceablePassengerCount = 0;
    Uint32 pendingPassengerCount = 0;

    /// Puts one hidden unit no container holds back on the map, releasing both sides of any stale
    /// pickup first. \a windowIndex selects this unit's turn in the map-wide fallback search.
    /// Returns false when no
    /// legal tile was found anywhere this pass, in which case the unit stays hidden and a later
    /// pass tries again with the next slice.
    bool recoverOrphanedPassenger(UnitBase* pUnit, const Coord& anchor, Uint32 windowIndex);

    /// First legal tile for \a pUnit at increasing block distance from \a anchor, searched in a
    /// fixed order and bounded, so the result depends only on simulation state.
    Coord findPassengerRecoverySpot(const UnitBase* pUnit, const Coord& anchor) const;

    /// First legal tile in this pass's bounded slice of the whole map, used when nothing near the
    /// anchor is legal. Consecutive slices cover the map, so a unit lost inside a permanently
    /// blocked area still comes back. Derived from \a windowIndex and the unit id only.
    Coord findGlobalRecoverySpot(const UnitBase* pUnit, Uint32 windowIndex) const;

    /// Can \a pUnit occupy \a candidate and survive arriving there? Terrain and occupancy come
    /// from the unit's own canPass(), plus a free tile for vehicles and no bloom underfoot.
    bool isLegalRecoveryTile(const UnitBase* pUnit, const Coord& candidate) const;

    /// Derived carrier index; lifecycle invalidation handles same-cycle replacements.
    std::vector<Uint32> carryallCandidateIds;
    Uint32 carryallCandidateCycle = std::numeric_limits<Uint32>::max();
    size_t carryallCandidateUnitCount = std::numeric_limits<size_t>::max();

    std::deque<TargetRequest> targetRequestQueue;
    std::unordered_set<Uint32> pendingTargetRequestIds;
    std::deque<PathRequest> pathRequestQueue;
    std::unordered_set<Uint32> pendingPathRequestIds;

    /// A search that has started but not finished. The vector order is the batch,
    /// quota, stepping and application order, and is simulation state: it is
    /// serialised into the observer runtime so a spectator continues from the
    /// same point rather than re-running work the host already charged.
    struct PathJob {
        Uint32 objectId = NONE_ID;
        std::unique_ptr<AStarSearch> search;
        UnitBase::PathInputFingerprint inputs;
        Coord destinationCoord = Coord::Invalid();
        size_t chargedNodes = 0;   ///< Includes nodes spent on abandoned attempts.
        Uint32 restarts = 0;
        Uint32 restartReasons = 0; ///< Union of PathInputFingerprint::Difference bits. Diagnostic.
        /// Set when the request this plan answers has changed: the plan is thrown
        /// away and the request goes back to the queue instead of being applied.
        bool requeue = false;
    };
    std::vector<PathJob> activePathJobs;

    /// Request-lifecycle conservation counters. Diagnostic only, never
    /// serialised, but they make the invariant checkable at runtime:
    /// started == applied + cancelled + requeued + still active.
    size_t pathRequestsStarted = 0;
    size_t pathResultsApplied = 0;
    size_t pathRequestsCancelled = 0;
    size_t pathRequestsRequeued = 0;

    // Batch size bounds the live A* scratch buffers (the pool holds
    // TilePoolSize), and is fixed so that a future worker count cannot change
    // which requests are selected or what quota each one gets.
    static constexpr size_t kPathBatchSize = 4;
    // Deterministic ceilings so a cycle whose requests consume no nodes still
    // terminates: invalid or immediately-finished entries cannot spin.
    static constexpr size_t kMaxPathRoundsPerCycle = 8;
    static constexpr size_t kMaxPathStartsPerCycle = 32;
    // Bounds a slice that is draining the frontier without closing nodes.
    static constexpr size_t kPathIterationSlack = 4096;

    void collectPathBatch(size_t& startsThisCycle);
    enum class PathJobOutcome { Applied, Requeued, Cancelled };
    void finishPathJob(PathJob& job, UnitBase* unit, PathJobOutcome outcome);

    // MULTIPLAYER FIX (Issue #1, #2): Removed time-based budgets
    // Now using only deterministic budgets:
    static constexpr std::size_t kPathNodeBudget = 2048;
    
    // Phase 1: Budget Negotiation System
    // Start at 15k (middle of range), adapt up or down based on FPS
    size_t negotiatedBudget = 15000;  // Current token budget (adaptive 5k-25k)
    size_t carryOverTokens = 0;       // Unused tokens from previous cycle (SINGLE-PLAYER ONLY - disabled in multiplayer to prevent desync)
    
    static constexpr size_t kMinBudget = 5000;    // Minimum 5k tokens/cycle
    static constexpr size_t kMaxBudget = 25000;   // Maximum 25k tokens/cycle
    static constexpr size_t kHardCap = 30000;     // With carry-over (not used in multiplayer)
    static constexpr size_t kDebtCap = 2000;      // Max carry-over tokens — reduced from 10k to limit burst spikes on large maps
    
    static constexpr int kBudgetCheckInterval = 375;  // Check every 375 cycles (~7.5s at 50Hz)

    /// Single-player reductions require pathfinding itself to cost this much
    /// per frame, so AI/city stalls cannot steal tokens from unit movement.
    /// Multiplayer retains its existing host-negotiated policy.
    static constexpr double kPathReductionFloorMs = 4.0;
    
    // Track last budget action to prevent oscillation (deterministic, synced state)
    enum class BudgetAction { NONE, INCREASED, DECREASED };
    BudgetAction lastBudgetAction = BudgetAction::NONE;
    
    int cycleGuardrailTrips = 0;      // Counter for cycle limit hits
    
    void requestLowerBudget(int steps = 1);  // Request budget reduction via network (steps × 500)

    // Multiplayer budget negotiation structs
    struct ClientPerformanceStats {
        Uint32 clientId;
        Uint32 lastUpdateCycle;
        float avgFps;               // Legacy metric (still useful for display)
        float simMsAvg;             // POST-VSYNC: Primary metric for CPU load detection
        Uint32 queueDepth;
        Uint32 currentBudget;
        int missedUpdates = 0;
    };
    
    struct PendingBudgetChange {
        size_t newBudget;
        Uint32 applyCycle;
        bool resetCarryOver = true;
    };
    
    // Multiplayer budget negotiation state
    std::vector<PendingBudgetChange> pendingBudgetChanges;
    /// Upper bound on tracked clients; the ENet host has 32 peer slots.
    static constexpr size_t kMaxTrackedClients = 32;
    std::map<Uint32, ClientPerformanceStats> clientStats;  // Host only
    
    // Track previous budget to avoid false DESYNC detection.
    // A client report can legitimately reflect the previous budget if the report cycle is before the change cycle.
    size_t previousNegotiatedBudget = 0;   // Budget before last change
    Uint32 lastBudgetChangeCycle = 0;      // Cycle when budget last changed
    
    // Multiplayer budget negotiation functions
    void checkBudgetAdjustment();
    void sendStatsToHost(float avgFps, float simMsAvg, size_t queueDepth, size_t currentBudget);
    void handleClientStats(Uint32 clientId, Uint32 gameCycle, float avgFps, float simMsAvg, Uint32 queueDepth, Uint32 currentBudget);
    void makeHostBudgetDecision();
    void broadcastBudgetChange(size_t newBudget);
    void handleSetPathBudget(size_t newBudget, Uint32 applyCycle);
    void applyPendingBudgetChanges();
    bool haveStatsFromAllClients() const;
    void applySinglePlayerBudgetAdjustment(float avgFps);
    void resyncClientBudget(Uint32 clientId);
    void handleMissingClientStats();

    // Game loop methods
    void initializeGameLoop();
    void renderFrame();
    void processInput();
    void updateGameState();
    bool handleNetworkUpdates();
    void initializeReplay();
    void initializeNetwork();
    GameInitSettings spectatorSnapshot();
    std::string saveObserverRuntime() const;
    void loadObserverRuntime(const std::string& bytes);
    void prepareObserverStreams();
    bool observerCyclePrepared = false;
    TextProgressBar observerProgress;

    /**
        Builds the deterministic fingerprint described in Network/GameStateDigest.h.

        Const and side-effect free: it reads simulation state and nothing else, so calling it
        can never be the reason two peers disagree.
    */
    GameStateDigest::Digest computeStateDigest() const;

    /// Produces our digest on schedule, sends it, and compares anything already received.
    void updateStateDigests();

    /**
        Applies a digest another peer produced.
        \param  peerName    the sending player
        \param  digest      their fingerprint
    */
    void onRemoteStateDigest(const std::string& peerName, const GameStateDigest::Digest& digest);

    /// Reports a divergence to the player, once per match.
    void reportStateDivergence(const std::string& peerName,
                               const GameStateDigest::Digest& ours,
                               const GameStateDigest::Digest& theirs);

    /// Our recent digests, oldest first; bounded by GameStateDigest::kDigestHistory.
    std::deque<GameStateDigest::Digest> localStateDigests;
    /// Digests from peers that arrived before we reached that cycle; bounded the same way.
    std::deque<std::pair<std::string, GameStateDigest::Digest>> pendingRemoteDigests;
    /// Set once a divergence has been reported, so the news ticker is not flooded.
    bool   stateDivergenceReported = false;
    /// The last cycle a digest was produced for.
    Uint32 lastStateDigestCycle = 0;
    /// Set when the lockstep wait has been abandoned, so it is only reported once.
    bool   lockstepStallReported = false;
    /**
        How long the match waits for a player's commands before it gives up.

        Generous enough that ordinary jitter, a long load or a brief stall never trips it, and
        short enough that nobody is left staring at "waiting for other players" forever.
    */
    static constexpr Uint32 LOCKSTEP_STALL_TIMEOUT_MS = 45000;
    void processTargetRequests();
    void processPathRequests();

    Uint32 lastTimingLogMs = 0;

    // Performance logging infrastructure
    static std::ofstream performanceLogFile;
    static std::mutex performanceLogMutex;
    static constexpr std::array<size_t, 6> PathTokenBucketBounds{{512, 1024, 2048, 4096, 8192, 16384}};

    void initPerformanceLog();
    void closePerformanceLog();
    void logPerformance(const char* format, ...);
    void recordPathTokens(size_t totalTokens);
    void recordCompletedPathTokens(size_t totalTokens);
    void recordFailedPathTokens(size_t totalTokens);
    void logFrameTiming();
};

#endif // GAME_H
