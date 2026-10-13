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
#include <GUI/Checkbox.h>
#include <GUI/TextBox.h>

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
/// The CustomGame start: the generated map text plus the generated roster.
GameInitSettings makeInit(const SkirmishGenerator::Result& result, const SettingsClass::GameOptionsClass& rules);
/// Write the map into the user multiplayer maps directory; returns the path or "" on failure.
std::string saveMap(const SkirmishGenerator::Result& result);
}

class RandomSkirmishMenu : public MenuBase
{
public:
    RandomSkirmishMenu();
    ~RandomSkirmishMenu() override;

private:
    void onStart();
    void onSaveMap();
    void onCancel();
    void onNewSeed();
    void onPlayersChanged();
    void onModChanged();
    /// The mod selected in the Mod box, or "" when none is listed.
    std::string selectedMod() const;

    SkirmishGenerator::Params readParams() const;
    void showParams(const SkirmishGenerator::Params& params);
    void remember(const SkirmishGenerator::Params& params) const;
    bool generate(SkirmishGenerator::Params& params, SkirmishGenerator::Result& result);

    void addRow(int column, int row, const std::string& caption, Widget* control);
    void addHeader(int column, int row, const std::string& caption);

    int houseCount = 8;

    StaticContainer windowWidget;
    Label title;
    Label statusLabel;
    std::array<Label, 30> captions;
    int usedCaptions = 0;
    int rowPitch = 25;                       ///< row height + gap; shrinks on short screens
    Label* allyCityCaption = nullptr;
    Label* enemyCityCaption = nullptr;
    Label* playerCityCaption = nullptr;

    DropDownBox sizeBox, rockBox, spiceBox;
    Checkbox symmetricBox;
    TextBox seedBox;
    TextButton newSeedButton;
    Checkbox fixedSeedBox;

    DropDownBox houseBox, creditsBox, modBox, techBox;
    std::vector<ModInfo> availableMods;      ///< Mod box entries; entry data is the index
    DropDownBox alliesBox, enemiesBox;
    Checkbox enemiesAlliedBox;

    DropDownBox partnerBox, playerBaseBox, playerArmyBox, playerCityBox;
    DropDownBox allyAIBox, allyBaseBox, allyArmyBox, allyCityBox;
    DropDownBox enemyAIBox, enemyBaseBox, enemyArmyBox, enemyCityBox;

    TextButton backButton, saveButton, startButton;
};

#endif // RANDOMSKIRMISHMENU_H
