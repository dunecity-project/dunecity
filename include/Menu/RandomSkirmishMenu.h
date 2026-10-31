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

#ifndef RANDOMSKIRMISHMENU_H
#define RANDOMSKIRMISHMENU_H

#include "MenuBase.h"

#include <GUI/StaticContainer.h>
#include <GUI/Label.h>
#include <GUI/TextButton.h>
#include <GUI/DropDownBox.h>
#include <GUI/HBox.h>
#include <GUI/Slider.h>
#include <GUI/Checkbox.h>
#include <GUI/TextBox.h>
#include <GUI/dune/AIPlayerSelector.h>

#include <DataTypes.h>
#include <GameInitSettings.h>
#include <MapEditor/SkirmishGenerator.h>
#include <mod/ModInfo.h>

#include <array>
#include <vector>

/// The steps behind the Random Skirmish screen, shared with its real-engine probe.
namespace RandomSkirmish {
/// Activate the chosen mod and refresh effectiveGameOptions; false if it cannot be activated.
bool activateMod(const std::string& modName);
/// The generator mode a mod plays: DuneCity when the mod runs the city simulation.
SkirmishGenerator::Mode modeForMod(const std::string& modName);
/// Prices, power and availability from the active mod's ObjectData.
SkirmishGenerator::Catalogue loadCatalogue(SkirmishGenerator::Mode mode);
/// The CustomGame start: the generated map text plus the generated roster. With
/// @a params.spectate the player's seat goes to @a params.spectateClass and the start is a local
/// spectator one (Custom Game's offline Spectate). Every house draws DuneCity buildings in @a skin.
GameInitSettings makeInit(const SkirmishGenerator::Result& result, const SettingsClass::GameOptionsClass& rules,
                          const SkirmishGenerator::Params& params,
                          GameInitSettings::GraphicsSkin skin = GameInitSettings::GraphicsSkin::SimCity);
/// Write the map into the user multiplayer maps directory; returns the path or "" on failure.
std::string saveMap(const SkirmishGenerator::Result& result);
}

/**
    The Random Skirmish screen. Single player (the default) starts the generated match itself,
    played or, with Spectate, watched as Custom Game's offline Spectate watches an all-AI match.
    Multiplayer mode (Custom Game > Random map...) is the host's map step: map settings, credits,
    tech level, the number of start positions and one Base / Army / City for every start, played
    with the host's mod. Next saves the map to the user multiplayer maps and closes; the host's
    map choice selects it (getCreatedMap()) and the lobby assigns players, houses and teams.
*/
class RandomSkirmishMenu : public MenuBase
{
public:
    RandomSkirmishMenu();
    /// Multiplayer mode for a game hosted with @a hostMod.
    explicit RandomSkirmishMenu(const std::string& hostMod);
    ~RandomSkirmishMenu() override;

    /// Multiplayer mode: path of the map Next saved, "" while none was created.
    const std::string& getCreatedMap() const { return createdMap; }

private:
    RandomSkirmishMenu(bool multiplayer, const std::string& hostMod);
    void onStart();
    /// What Start does before it plays: generate, remember the choices (rolling the next seed)
    /// and build the match start; false if the match cannot start.
    bool prepareStart(GameInitSettings& init);
    void onSaveMap();
    /// Multiplayer mode: generate, save to the user multiplayer maps and hand the path back.
    void onCreateMap();
    void onCancel();
    /// Opens the Game Rules window (fog of war, concrete, game speed, unit caps...), as Custom Game does.
    void onGameRules();
    void onChildWindowClose(Window* pChildWindow) override;
    /// The rules the match starts with: the player's choice once made, else the mod's defaults.
    const SettingsClass::GameOptionsClass& matchRules() const;
    void onNewSeed();
    /// Allies or enemies moved: the other one gives way so their sum stays within the cap.
    void onPlayersChanged(bool alliesMoved = true);
    void onModChanged();
    /// Spectate toggled: the AI that plays the watched house is chosen only while watching.
    void onSpectateChanged();
    /// The mod selected in the Mod box, or "" when none is listed.
    std::string selectedMod() const;

    SkirmishGenerator::Params readParams() const;
    void showParams(const SkirmishGenerator::Params& params);
    void remember(const SkirmishGenerator::Params& params) const;
    bool generate(SkirmishGenerator::Params& params, SkirmishGenerator::Result& result);

    void addRow(int column, int row, const std::string& caption, Widget* control);
    void addHeader(int column, int row, const std::string& caption);

    int houseCount = 8;
    bool multiplayer = false;
    std::string hostMod;                     ///< multiplayer: the mod the hosted game plays
    std::string createdMap;
    SkirmishGenerator::Params loaded;        ///< last-used choices; readParams() keeps the ones not shown

    StaticContainer windowWidget;
    Label title;
    Label statusLabel;
    std::array<Label, 32> captions;
    int usedCaptions = 0;
    int rowPitch = 25;                       ///< row height + gap; shrinks on short screens
    Label* allyCityCaption = nullptr;
    Label* enemyCityCaption = nullptr;
    Label* playerCityCaption = nullptr;
    Label* startCityCaption = nullptr;
    Label* playerHeader = nullptr;           ///< "You start with", or "Your house starts with" while watching
    Label* skinCaption = nullptr;            ///< "City skin", shown only for a city-sim mod

    // Settings that run from low to high are sliders; choices without an order are drop-downs.
    Slider sizeBox, rockBox, spiceBox;
    Checkbox symmetricBox;
    TextBox seedBox;
    TextButton newSeedButton;
    Checkbox fixedSeedBox;

    DropDownBox houseBox, modBox;
    Label hostModLabel;                      ///< multiplayer: the host's mod, chosen in the map list
    Label lobbyNote;                         ///< multiplayer: where players, AI and teams are chosen
    Slider creditsBox, techBox;
    std::vector<ModInfo> availableMods;      ///< Mod box entries; entry data is the index
    Slider alliesBox, enemiesBox;
    Checkbox enemiesAlliedBox;
    Checkbox spectateBox;                    ///< single player: watch an all-AI match (Custom Game's Spectate)
    HBox spectateAIBox;                      ///< the AI that plays the player's house while watching (AI, Difficulty)
    DropDownBox skinBox;                     ///< single player: SimCity or Dune2 city-building art for every house

    // Each AI row holds its selector's AI and Difficulty boxes; the partner's row also holds its Role.
    HBox partnerBox, allyAIBox, enemyAIBox;
    AIPlayerSelector partnerAI;              ///< None or a QuantBot sharing the player's house, any role
    AIPlayerSelector allyAI, enemyAI;        ///< their own houses: every AI, always Builder + Fighter
    AIPlayerSelector spectateAI;             ///< the watched house's own AI, always Builder + Fighter
    Slider playerBaseBox, playerArmyBox, playerCityBox;
    Slider allyBaseBox, allyArmyBox, allyCityBox;
    Slider enemyBaseBox, enemyArmyBox, enemyCityBox;
    Slider startsBox, startBaseBox, startArmyBox, startCityBox;

    TextButton backButton, rulesButton, saveButton, startButton, nextButton;
    SettingsClass::GameOptionsClass rules;   ///< what the Game Rules window last returned
    bool rulesChosen = false;               ///< the player confirmed the Game Rules window
};

#endif // RANDOMSKIRMISHMENU_H
