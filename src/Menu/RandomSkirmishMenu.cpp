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

#include <Menu/RandomSkirmishMenu.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/TextManager.h>
#include <GUI/MsgBox.h>
#include <ObjectData.h>
#include <mod/ModManager.h>
#include <players/PlayerFactory.h>
#include <misc/WebRuntime.h>
#include <misc/fnkdat.h>
#include <globals.h>
#include <main.h>
#include <sand.h>

#include <fstream>
#include <memory>
#include <random>

using namespace SkirmishGenerator;

namespace RandomSkirmish {

bool activateMode(Mode mode) {
    auto& mods = ModManager::instance();
    const std::string wanted = mode == Mode::DuneCity ? "dunecity" : "vanilla";
    if(mods.getActiveModName() != wanted && !mods.setActiveMod(wanted)) return false;
    // A new game takes its rules from its own mod, like a new Custom Game.
    effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
    return mods.getActiveModName() == wanted;
}

Catalogue loadCatalogue(Mode mode) {
    auto data = std::make_unique<ObjectData>();
    data->loadFromINIFile(ModManager::instance().getActiveObjectDataPath(), false);
    return makeCatalogue(*data, mode == Mode::DuneCity);
}

GameInitSettings makeInit(const Result& result, const SettingsClass::GameOptionsClass& rules) {
    GameInitSettings init(result.name, result.ini, false, rules);
    for(const HouseSetup& house : result.houses) {
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(house.house), house.team);
        if(house.role == Role::Human) {
            info.addPlayerInfo(GameInitSettings::PlayerInfo(settings.general.playerName, HUMANPLAYERCLASS));
        } else {
            const auto* player = PlayerFactory::getByPlayerClass(house.playerClass);
            info.addPlayerInfo(GameInitSettings::PlayerInfo(getHouseNameByNumber(static_cast<HOUSETYPE>(house.house)),
                player ? house.playerClass : std::string(DEFAULTAIPLAYERCLASS)));
        }
        init.addHouseInfo(info);
    }
    return init;
}

std::string saveMap(const Result& result) {
    char directory[FILENAME_MAX];
    if(fnkdat("maps/multiplayer/", directory, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) < 0) return {};
    const std::string path = std::string(directory) + result.name + ".ini";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << result.ini;
    file.close();
    if(!file) return {};
    WebRuntime::syncPersistentFiles();
    return path;
}

}

namespace {

constexpr int kRowHeight = 22;
constexpr int kRowGap = 3;

const char* const kRockNames[] = {"Low", "Medium", "High", "Very high"};
const char* const kSpiceNames[] = {"None", "Low", "Medium", "High", "Very high"};
const char* const kBaseNames[] = {"None (MCV only)", "Outpost", "Small", "Medium", "Large", "Fortress"};

std::string withThousands(int value) {
    std::string text = std::to_string(value);
    for(int i = static_cast<int>(text.size()) - 3; i > 0; i -= 3) text.insert(i, ",");
    return text;
}

uint32_t freshSeed() {
    // Menu-side only: the seed itself is what makes the match reproducible.
    std::random_device device;
    return device() % 1000000000u;
}

void selectData(DropDownBox& box, int data) {
    for(int i = 0; i < box.getNumEntries(); ++i)
        if(box.getEntryIntData(i) == data) { box.setSelectedItem(i); return; }
    if(box.getNumEntries() > 0 && box.getSelectedIndex() < 0) box.setSelectedItem(0);
}

void fillAI(DropDownBox& box) {
    for(const auto& player : PlayerFactory::getList()) {
        const std::string& playerClass = player.getPlayerClass();
        // Helpers share a human's house; an allied side here is a house of its own.
        if(playerClass == HUMANPLAYERCLASS || playerClass.find("Support") != std::string::npos) continue;
        box.addEntry(player.getName(), PlayerFactory::getIndexByPlayerClass(playerClass));
    }
}

void fillSide(DropDownBox& base, DropDownBox& army, DropDownBox& city) {
    for(int i = 0; i <= static_cast<int>(BaseSize::Fortress); ++i) base.addEntry(_(kBaseNames[i]), i);
    for(int i = 0; i < static_cast<int>(kArmyBudgets.size()); ++i)
        army.addEntry(i == 0 ? _("None") : withThousands(kArmyBudgets[i]) + " " + _("credits"), i);
    army.addEntry(_("Random") + " (" + withThousands(kArmyRandomMin) + "-" + withThousands(kArmyRandomMax) + ")", kArmyRandom);
    for(int i = 0; i < static_cast<int>(kCityTargets.size()); ++i)
        city.addEntry(i == 0 ? _("None") : withThousands(kCityTargets[i]) + " " + _("people"), i);
    city.addEntry(_("Random") + " (" + withThousands(kCityRandomMin) + "-" + withThousands(kCityRandomMax) + ")", kCityRandom);
}

}

RandomSkirmishMenu::RandomSkirmishMenu() : MenuBase() {
    SDL_Texture* background = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(background);
    resize(getTextureSize(background));
    setWindowWidget(&windowWidget);

    houseCount = 0;
    for(int h = 0; h < NUM_HOUSES; ++h) houseCount += isCustomGameHouseAvailable(static_cast<HOUSETYPE>(h)) ? 1 : 0;

    const int width = getSize().x, height = getSize().y;
    title.setText(_("Random Skirmish"));
    title.setTextFontSize(22);
    title.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&title, Point(0, 8), Point(width, 30));

    for(int size : kMapSizes) sizeBox.addEntry(std::to_string(size) + " x " + std::to_string(size), size);
    for(int i = 0; i < 4; ++i) rockBox.addEntry(_(kRockNames[i]) + " (" + std::to_string(kRockPercent[i]) + "%)", i);
    for(int i = 0; i < 5; ++i) spiceBox.addEntry(_(kSpiceNames[i]), i);
    symmetricBox.setText(_("Mirrored, fair start areas"));
    seedBox.setAllowedChars("0123456789");
    seedBox.setMaximumTextLength(10);
    newSeedButton.setText(_("New"));
    newSeedButton.setOnClick([this]() { onNewSeed(); });
    fixedSeedBox.setText(_("Keep this seed"));

    houseBox.addEntry(_("Random"), HOUSE_INVALID);
    for(int h = 0; h < NUM_HOUSES; ++h)
        if(isCustomGameHouseAvailable(static_cast<HOUSETYPE>(h))) houseBox.addEntry(getHouseNameByNumber(static_cast<HOUSETYPE>(h)), h);
    for(int credits : kCreditPresets) creditsBox.addEntry(withThousands(credits), credits);
    modeBox.addEntry(_("Vanilla"), static_cast<int>(Mode::Vanilla));
    if(ModManager::instance().modExists("dunecity")) modeBox.addEntry(_("DuneCity"), static_cast<int>(Mode::DuneCity));
    modeBox.setOnSelectionChange([this](bool) { onModeChanged(); });
    for(int level = 1; level <= 9; ++level) techBox.addEntry(std::to_string(level), level);
    alliesBox.setOnSelectionChange([this](bool) { onPlayersChanged(); });
    enemiesBox.setOnSelectionChange([this](bool) { onPlayersChanged(); });
    enemiesAlliedBox.setText(_("Enemies are allied"));

    fillAI(allyAIBox);
    fillAI(enemyAIBox);
    fillSide(allyBaseBox, allyArmyBox, allyCityBox);
    fillSide(enemyBaseBox, enemyArmyBox, enemyCityBox);

    int row = 0;
    addHeader(0, row++, _("Map"));
    addRow(0, row++, _("Size"), &sizeBox);
    addRow(0, row++, _("Rock"), &rockBox);
    addRow(0, row++, _("Spice"), &spiceBox);
    addRow(0, row++, _("Layout"), &symmetricBox);
    addRow(0, row++, _("Seed"), &seedBox);
    addRow(0, row++, "", &fixedSeedBox);
    addHeader(0, row++, _("Game"));
    addRow(0, row++, _("Your house"), &houseBox);
    addRow(0, row++, _("Credits"), &creditsBox);
    addRow(0, row++, _("Mode"), &modeBox);
    addRow(0, row++, _("Tech level"), &techBox);

    row = 0;
    addHeader(1, row++, _("Players"));
    addRow(1, row++, _("Allies"), &alliesBox);
    addRow(1, row++, _("Enemies"), &enemiesBox);
    addRow(1, row++, "", &enemiesAlliedBox);
    addHeader(1, row++, _("Allies start with"));
    addRow(1, row++, _("AI"), &allyAIBox);
    addRow(1, row++, _("Base"), &allyBaseBox);
    addRow(1, row++, _("Army"), &allyArmyBox);
    addRow(1, row++, _("City"), &allyCityBox);
    addHeader(1, row++, _("Enemies start with"));
    addRow(1, row++, _("AI"), &enemyAIBox);
    addRow(1, row++, _("Base"), &enemyBaseBox);
    addRow(1, row++, _("Army"), &enemyArmyBox);
    addRow(1, row++, _("City"), &enemyCityBox);

    // The seed field shares its row with the "New" button.
    const int columnWidth = (width - 48) / 2;
    const int controlX = 16 + columnWidth * 2 / 5;
    const int seedRow = 5;
    const int top = 44;
    windowWidget.setWidgetGeometry(&seedBox, Point(controlX, top + seedRow * (kRowHeight + kRowGap)),
                                   Point(columnWidth - columnWidth * 2 / 5 - 64, kRowHeight));
    windowWidget.addWidget(&newSeedButton, Point(16 + columnWidth - 58, top + seedRow * (kRowHeight + kRowGap)), Point(58, kRowHeight));

    statusLabel.setTextFontSize(12);
    statusLabel.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&statusLabel, Point(16, height - 66), Point(width - 32, 22));

    backButton.setText(_("Back"));
    backButton.setOnClick([this]() { onCancel(); });
    saveButton.setText(_("Save map"));
    saveButton.setOnClick([this]() { onSaveMap(); });
    startButton.setText(_("Start"));
    startButton.setOnClick([this]() { onStart(); });
    const int buttonWidth = std::min(160, (width - 64) / 3);
    windowWidget.addWidget(&backButton, Point(16, height - 38), Point(buttonWidth, 28));
    windowWidget.addWidget(&saveButton, Point((width - buttonWidth) / 2, height - 38), Point(buttonWidth, 28));
    windowWidget.addWidget(&startButton, Point(width - 16 - buttonWidth, height - 38), Point(buttonWidth, 28));
    startButton.setActive();

    Params params;
    params.seed = freshSeed();
    params.load(INIFile(getConfigFilepath()));
    if(!params.fixedSeed) params.seed = freshSeed();
    params.clamp(houseCount);
    showParams(params);
}

RandomSkirmishMenu::~RandomSkirmishMenu() = default;

void RandomSkirmishMenu::addHeader(int column, int row, const std::string& caption) {
    if(usedCaptions >= static_cast<int>(captions.size())) return;
    Label& label = captions[usedCaptions++];
    label.setText(caption);
    label.setTextFontSize(16);
    const int columnWidth = (getSize().x - 48) / 2;
    windowWidget.addWidget(&label, Point(16 + column * (columnWidth + 16), 44 + row * (kRowHeight + kRowGap)),
                           Point(columnWidth, kRowHeight));
}

void RandomSkirmishMenu::addRow(int column, int row, const std::string& caption, Widget* control) {
    const int columnWidth = (getSize().x - 48) / 2;
    const int x = 16 + column * (columnWidth + 16);
    const int y = 44 + row * (kRowHeight + kRowGap);
    const int captionWidth = columnWidth * 2 / 5;
    if(!caption.empty() && usedCaptions < static_cast<int>(captions.size())) {
        Label& label = captions[usedCaptions++];
        label.setText(caption);
        label.setTextFontSize(13);
        windowWidget.addWidget(&label, Point(x + 8, y), Point(captionWidth - 8, kRowHeight));
    }
    windowWidget.addWidget(control, Point(x + captionWidth, y), Point(columnWidth - captionWidth, kRowHeight));
}

void RandomSkirmishMenu::showParams(const Params& params) {
    selectData(sizeBox, params.mapSize);
    selectData(rockBox, static_cast<int>(params.rock));
    selectData(spiceBox, static_cast<int>(params.spice));
    symmetricBox.setChecked(params.symmetric);
    seedBox.setText(std::to_string(params.seed));
    fixedSeedBox.setChecked(params.fixedSeed);
    selectData(houseBox, params.house);
    selectData(creditsBox, params.credits);
    selectData(modeBox, static_cast<int>(params.mode));
    selectData(techBox, params.techLevel);
    enemiesAlliedBox.setChecked(params.enemiesAllied);
    for(auto [side, ai, base, army, city] : {std::tuple{&params.allySide, &allyAIBox, &allyBaseBox, &allyArmyBox, &allyCityBox},
                                             std::tuple{&params.enemySide, &enemyAIBox, &enemyBaseBox, &enemyArmyBox, &enemyCityBox}}) {
        selectData(*ai, PlayerFactory::getIndexByPlayerClass(side->playerClass));
        if(ai->getSelectedIndex() < 0) selectData(*ai, PlayerFactory::getIndexByPlayerClass("qBotMedium"));
        selectData(*base, static_cast<int>(side->base));
        selectData(*army, side->army);
        selectData(*city, side->city);
    }
    alliesBox.clearAllEntries();
    for(int i = 0; i <= std::min(kMaxAllies, houseCount - 2); ++i) alliesBox.addEntry(std::to_string(i), i);
    selectData(alliesBox, params.allies);
    onPlayersChanged();
    selectData(enemiesBox, params.enemies);
    onModeChanged();
}

void RandomSkirmishMenu::onPlayersChanged() {
    // Total houses never exceed what the engine can field in a custom game.
    const int allies = std::max(0, alliesBox.getSelectedEntryIntData());
    const int previous = enemiesBox.getSelectedIndex() >= 0 ? enemiesBox.getSelectedEntryIntData() : kMinEnemies;
    const int most = std::clamp(houseCount - 1 - allies, kMinEnemies, kMaxEnemies);
    if(enemiesBox.getNumEntries() != most) {
        enemiesBox.clearAllEntries();
        for(int i = kMinEnemies; i <= most; ++i) enemiesBox.addEntry(std::to_string(i), i);
        selectData(enemiesBox, std::min(previous, most));
    }
    enemiesAlliedBox.setEnabled(enemiesBox.getSelectedEntryIntData() > 1);
    const bool anyAllies = allies > 0;
    for(DropDownBox* box : {&allyAIBox, &allyBaseBox, &allyArmyBox}) box->setEnabled(anyAllies);
    onModeChanged();
}

void RandomSkirmishMenu::onModeChanged() {
    const bool city = modeBox.getSelectedEntryIntData() == static_cast<int>(Mode::DuneCity);
    allyCityBox.setEnabled(city && alliesBox.getSelectedEntryIntData() > 0);
    enemyCityBox.setEnabled(city);
}

void RandomSkirmishMenu::onNewSeed() {
    seedBox.setText(std::to_string(freshSeed()));
}

Params RandomSkirmishMenu::readParams() const {
    Params params;
    params.mapSize = sizeBox.getSelectedEntryIntData();
    params.rock = static_cast<Rock>(rockBox.getSelectedEntryIntData());
    params.spice = static_cast<Spice>(spiceBox.getSelectedEntryIntData());
    params.symmetric = symmetricBox.isChecked();
    const std::string seed = seedBox.getText();
    if(!seed.empty()) {
        const unsigned long long value = std::stoull(seed);
        params.seed = static_cast<uint32_t>(std::min<unsigned long long>(value, 0xFFFFFFFFull));
    }
    params.fixedSeed = fixedSeedBox.isChecked();
    params.house = houseBox.getSelectedEntryIntData();
    params.credits = creditsBox.getSelectedEntryIntData();
    params.mode = static_cast<Mode>(modeBox.getSelectedEntryIntData());
    params.techLevel = techBox.getSelectedEntryIntData();
    params.allies = alliesBox.getSelectedEntryIntData();
    params.enemies = enemiesBox.getSelectedEntryIntData();
    params.enemiesAllied = enemiesAlliedBox.isChecked();
    for(auto [side, ai, base, army, city] : {std::tuple{&params.allySide, &allyAIBox, &allyBaseBox, &allyArmyBox, &allyCityBox},
                                             std::tuple{&params.enemySide, &enemyAIBox, &enemyBaseBox, &enemyArmyBox, &enemyCityBox}}) {
        const auto* player = PlayerFactory::getByIndex(ai->getSelectedEntryIntData());
        side->playerClass = player ? player->getPlayerClass() : std::string(DEFAULTAIPLAYERCLASS);
        side->base = static_cast<BaseSize>(base->getSelectedEntryIntData());
        side->army = army->getSelectedEntryIntData();
        side->city = city->getSelectedEntryIntData();
    }
    params.clamp(houseCount);
    return params;
}

void RandomSkirmishMenu::remember(const Params& params) const {
    INIFile config(getConfigFilepath());
    params.save(config);
    if(!config.saveChangesTo(getConfigFilepath())) {
        SDL_Log("Warning: could not save the Random Skirmish choices to the configuration file");
        return;
    }
    WebRuntime::syncPersistentFiles();
}

bool RandomSkirmishMenu::generate(Params& params, Result& result) {
    params = readParams();
    remember(params);
    if(!RandomSkirmish::activateMode(params.mode)) {
        openWindow(MsgBox::create(_("The mod for this game mode could not be activated.")));
        return false;
    }
    result = SkirmishGenerator::generate(params, RandomSkirmish::loadCatalogue(params.mode));
    int skipped = 0;
    for(const auto& house : result.houses) skipped += house.skipped;
    SDL_Log("Random Skirmish: %s (map seed %u after %d attempt(s)%s, %d item(s) did not fit)", result.name.c_str(),
            result.mapSeed, result.attempts, result.flattenedMountains ? ", mountains levelled" : "", skipped);
    statusLabel.setText(skipped > 0
        ? _("Map is crowded: ") + std::to_string(skipped) + _(" planned buildings/units did not fit. Try a larger map.")
        : result.name);
    return true;
}

void RandomSkirmishMenu::onStart() {
    Params params;
    Result result;
    if(!generate(params, result)) return;
    const GameInitSettings init = RandomSkirmish::makeInit(result, effectiveGameOptions);
    if(!params.fixedSeed) {
        // The next visit offers a new map; this one stays reproducible from its name.
        params.seed = freshSeed();
        remember(params);
    }
    startSinglePlayerGame(init);
    quit();
}

void RandomSkirmishMenu::onSaveMap() {
    Params params;
    Result result;
    if(!generate(params, result)) return;
    const std::string path = RandomSkirmish::saveMap(result);
    openWindow(MsgBox::create(path.empty() ? _("The map could not be saved.")
                                           : _("Saved as") + "\n" + result.name + ".ini\n" + _("(Custom Game > MP User Maps)")));
}

void RandomSkirmishMenu::onCancel() {
    remember(readParams());
    quit();
}
