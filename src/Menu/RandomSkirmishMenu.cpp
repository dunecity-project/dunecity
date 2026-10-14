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
#include <Menu/Dune2RReadinessMenu.h>
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

bool activateMod(const std::string& modName) {
    auto& mods = ModManager::instance();
    if(modName.empty() || !mods.modExists(modName)) return false;
    if(mods.getActiveModName() != modName && !mods.setActiveMod(modName)) return false;
    // A new game takes its rules from its own mod, like a new Custom Game.
    effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
    return mods.getActiveModName() == modName;
}

Mode modeForMod(const std::string& modName) {
    const auto& mods = ModManager::instance();
    return mods.modExists(modName) && mods.getModInfo(modName).enablesCityMode ? Mode::DuneCity : Mode::Vanilla;
}

Catalogue loadCatalogue(Mode mode) {
    auto data = std::make_unique<ObjectData>();
    data->loadFromINIFile(ModManager::instance().getActiveObjectDataPath(), false);
    return makeCatalogue(*data, mode == Mode::DuneCity);
}

GameInitSettings makeInit(const Result& result, const SettingsClass::GameOptionsClass& rules) {
    // A partner AI shares the player's house, as a Custom Game's second player slot does.
    bool sharedHouse = false;
    for(const HouseSetup& house : result.houses)
        sharedHouse = sharedHouse || (house.role == Role::Human && PlayerFactory::getByPlayerClass(house.partnerClass) != nullptr);
    GameInitSettings init(result.name, result.ini, sharedHouse, rules);
    for(const HouseSetup& house : result.houses) {
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(house.house), house.team);
        if(house.role == Role::Human) {
            info.addPlayerInfo(GameInitSettings::PlayerInfo(settings.general.playerName, HUMANPLAYERCLASS));
            if(PlayerFactory::getByPlayerClass(house.partnerClass) != nullptr) {
                info.addPlayerInfo(GameInitSettings::PlayerInfo(getHouseNameByNumber(static_cast<HOUSETYPE>(house.house)),
                                                                house.partnerClass));
            }
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

constexpr int kRowGap = 3;
constexpr int kLeftColumnRows = 17;   ///< Map (7) + Game (5) + You start with (5)

/// AI that may share the player's house: the co-op partner list of Custom Game.
const char* const kPartnerClasses[] = {"qBotSupportEasy", "qBotSupportMedium", "qBotSupportHard", "qBotSupportBrutal",
                                       "qBotEasy", "qBotMedium", "qBotHard", "qBotBrutal", "qBotDefend"};
constexpr int kMaxContentWidth = 800;

/// Two columns of label + control rows, centred and capped on wide screens.
int contentWidth(int screenWidth) { return std::min(screenWidth - 32, kMaxContentWidth); }
int columnSpan(int screenWidth) { return (contentWidth(screenWidth) - 16) / 2; }
int columnX(int screenWidth, int column) {
    return (screenWidth - contentWidth(screenWidth)) / 2 + column * (columnSpan(screenWidth) + 16);
}

const char* const kSpiceNames[] = {"None", "Low", "Medium", "High", "Very high"};
const char* const kBaseNames[] = {"MCV only", "Outpost", "Small", "Medium", "Large", "Fortress"};

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

/// Selects the entry carrying \a data in a drop-down or slider (first entry if none does).
template<class Box>
void selectData(Box& box, int data) {
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

/// Low to high, left to right; "Random" is the rightmost step of Army and City.
void fillSide(Slider& base, Slider& army, Slider& city) {
    for(int i = 0; i <= static_cast<int>(BaseSize::Fortress); ++i) base.addEntry(_(kBaseNames[i]), i);
    for(int i = 0; i < static_cast<int>(kArmyBudgets.size()); ++i)
        army.addEntry(i == 0 ? _("None") : withThousands(kArmyBudgets[i]), i);
    army.addEntry(_("Random"), kArmyRandom);
    for(int i = 0; i < static_cast<int>(kCityTargets.size()); ++i)
        city.addEntry(i == 0 ? _("None") : withThousands(kCityTargets[i]), i);
    city.addEntry(_("Random"), kCityRandom);
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
    // Rows fit between the title and the status line, however short the window.
    rowPitch = std::clamp((height - 44 - 70) / kLeftColumnRows, 18, 25);
    title.setText(_("Random Skirmish"));
    title.setTextFontSize(22);
    title.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&title, Point(0, 8), Point(width, 30));

    for(int size : kMapSizes) sizeBox.addEntry(std::to_string(size) + " x " + std::to_string(size), size);
    for(int i = 0; i < static_cast<int>(kRockPercent.size()); ++i) rockBox.addEntry(std::to_string(kRockPercent[i]) + "%", i);
    for(int i = 0; i < 5; ++i) spiceBox.addEntry(_(kSpiceNames[i]), i);
    symmetricBox.setText(_("Mirrored starts"));
    seedBox.setAllowedChars("0123456789");
    seedBox.setMaximumTextLength(10);
    newSeedButton.setText(_("New"));
    newSeedButton.setOnClick([this]() { onNewSeed(); });
    fixedSeedBox.setText(_("Keep this seed"));

    houseBox.addEntry(_("Random"), HOUSE_INVALID);
    for(int h = 0; h < NUM_HOUSES; ++h)
        if(isCustomGameHouseAvailable(static_cast<HOUSETYPE>(h))) houseBox.addEntry(getHouseNameByNumber(static_cast<HOUSETYPE>(h)), h);
    for(int credits : kCreditPresets) creditsBox.addEntry(withThousands(credits), credits);
    // Every installed mod, as in Custom Game. City size only applies to a mod that runs the city sim.
    availableMods = ModManager::instance().listModChoices();
    for(size_t i = 0; i < availableMods.size(); ++i) modBox.addEntry(availableMods[i].selectionLabel(), static_cast<int>(i));
    modBox.setOnSelectionChange([this](bool) { onModChanged(); });
    for(int level = 1; level <= 9; ++level) techBox.addEntry(std::to_string(level), level);
    alliesBox.setOnSelectionChange([this](bool) { onPlayersChanged(true); });
    enemiesBox.setOnSelectionChange([this](bool) { onPlayersChanged(false); });
    enemiesAlliedBox.setText(_("Enemies are allied"));

    fillAI(allyAIBox);
    fillAI(enemyAIBox);
    fillSide(allyBaseBox, allyArmyBox, allyCityBox);
    fillSide(enemyBaseBox, enemyArmyBox, enemyCityBox);
    fillSide(playerBaseBox, playerArmyBox, playerCityBox);
    partnerBox.addEntry(_("None"), -1);
    for(const char* playerClass : kPartnerClasses) {
        const int index = PlayerFactory::getIndexByPlayerClass(playerClass);
        if(index >= 0) partnerBox.addEntry(PlayerFactory::getByIndex(index)->getName(), index);
    }

    int row = 0;
    addHeader(0, row++, _("Map"));
    addRow(0, row++, _("Size"), &sizeBox);
    addRow(0, row++, _("Rock"), &rockBox);
    addRow(0, row++, _("Spice"), &spiceBox);
    addRow(0, row++, _("Layout"), &symmetricBox);
    addRow(0, row++, _("Seed"), &seedBox);
    addRow(0, row++, "", &fixedSeedBox);
    addHeader(0, row++, _("Game"));
    addRow(0, row++, _("Mod"), &modBox);
    addRow(0, row++, _("Your house"), &houseBox);
    addRow(0, row++, _("Credits"), &creditsBox);
    addRow(0, row++, _("Tech level"), &techBox);
    addHeader(0, row++, _("You start with"));
    addRow(0, row++, _("AI partner"), &partnerBox);
    addRow(0, row++, _("Base"), &playerBaseBox);
    addRow(0, row++, _("Army"), &playerArmyBox);
    addRow(0, row++, _("City"), &playerCityBox);
    playerCityCaption = &captions[usedCaptions - 1];

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
    allyCityCaption = &captions[usedCaptions - 1];
    addHeader(1, row++, _("Enemies start with"));
    addRow(1, row++, _("AI"), &enemyAIBox);
    addRow(1, row++, _("Base"), &enemyBaseBox);
    addRow(1, row++, _("Army"), &enemyArmyBox);
    addRow(1, row++, _("City"), &enemyCityBox);
    enemyCityCaption = &captions[usedCaptions - 1];

    // The seed field shares its row with the "New" button.
    const int columnWidth = columnSpan(width);
    const int controlX = columnX(width, 0) + columnWidth * 2 / 5;
    const int seedRow = 5;
    const int top = 44;
    windowWidget.setWidgetGeometry(&seedBox, Point(controlX, top + seedRow * rowPitch),
                                   Point(columnWidth - columnWidth * 2 / 5 - 64, rowPitch - kRowGap));
    windowWidget.addWidget(&newSeedButton, Point(columnX(width, 0) + columnWidth - 58, top + seedRow * rowPitch), Point(58, rowPitch - kRowGap));

    statusLabel.setTextFontSize(12);
    statusLabel.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&statusLabel, Point(16, height - 66), Point(width - 32, 22));

    backButton.setText(_("Back"));
    backButton.setOnClick([this]() { onCancel(); });
    saveButton.setText(_("Save map"));
    saveButton.setOnClick([this]() { onSaveMap(); });
    startButton.setText(_("Start"));
    startButton.setOnClick([this]() { onStart(); });
    const int left = columnX(width, 0), right = left + contentWidth(width);
    const int buttonWidth = std::min(160, (contentWidth(width) - 32) / 3);
    windowWidget.addWidget(&backButton, Point(left, height - 38), Point(buttonWidth, 28));
    windowWidget.addWidget(&saveButton, Point((width - buttonWidth) / 2, height - 38), Point(buttonWidth, 28));
    windowWidget.addWidget(&startButton, Point(right - buttonWidth, height - 38), Point(buttonWidth, 28));
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
    const int columnWidth = columnSpan(getSize().x);
    windowWidget.addWidget(&label, Point(columnX(getSize().x, column), 44 + row * rowPitch),
                           Point(columnWidth, rowPitch - kRowGap));
}

void RandomSkirmishMenu::addRow(int column, int row, const std::string& caption, Widget* control) {
    const int columnWidth = columnSpan(getSize().x);
    const int x = columnX(getSize().x, column);
    const int y = 44 + row * rowPitch;
    const int captionWidth = columnWidth * 2 / 5;
    if(!caption.empty() && usedCaptions < static_cast<int>(captions.size())) {
        Label& label = captions[usedCaptions++];
        label.setText(caption);
        label.setTextFontSize(13);
        windowWidget.addWidget(&label, Point(x + 8, y), Point(captionWidth - 8, rowPitch - kRowGap));
    }
    windowWidget.addWidget(control, Point(x + captionWidth, y), Point(columnWidth - captionWidth, rowPitch - kRowGap));
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
    // The last-used mod, else the active one, else the first listed.
    int modIndex = -1;
    for(const std::string& wanted : {params.mod, ModManager::instance().getActiveModName()}) {
        for(size_t i = 0; i < availableMods.size() && modIndex < 0; ++i)
            if(availableMods[i].matchesSelectionName(wanted)) modIndex = static_cast<int>(i);
    }
    if(modIndex < 0 && !availableMods.empty()) modIndex = 0;
    if(modIndex >= 0) modBox.setSelectedItem(modIndex);
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
    // The player's own start; "None" (-1) is selected when no partner is chosen or listed.
    selectData(partnerBox, params.playerSide.playerClass.empty() ? -1 : PlayerFactory::getIndexByPlayerClass(params.playerSide.playerClass));
    selectData(playerBaseBox, static_cast<int>(params.playerSide.base));
    selectData(playerArmyBox, params.playerSide.army);
    selectData(playerCityBox, params.playerSide.city);
    // Fixed ranges (allies 0-6, enemies 1-7, within the engine's house cap); the sum is kept
    // within kMaxOtherHouses by onPlayersChanged.
    const int others = maxOtherHouses(houseCount);
    alliesBox.clearAllEntries();
    for(int i = 0; i <= std::min(kMaxAllies, others - kMinEnemies); ++i) alliesBox.addEntry(std::to_string(i), i);
    enemiesBox.clearAllEntries();
    for(int i = kMinEnemies; i <= std::min(kMaxEnemies, others); ++i) enemiesBox.addEntry(std::to_string(i), i);
    selectData(alliesBox, params.allies);
    selectData(enemiesBox, params.enemies);
    onPlayersChanged(false);
    onModChanged();
}

void RandomSkirmishMenu::onPlayersChanged(bool alliesMoved) {
    // Allies and enemies share kMaxOtherHouses (and the engine's house cap): the slider the
    // player did not touch gives way.
    const int others = maxOtherHouses(houseCount);
    const int alliesNow = std::max(0, alliesBox.getSelectedEntryIntData());
    const int enemiesNow = std::max(kMinEnemies, enemiesBox.getSelectedEntryIntData());
    if(alliesNow + enemiesNow > others) {
        if(alliesMoved) selectData(enemiesBox, std::max(kMinEnemies, others - alliesNow));
        else selectData(alliesBox, std::max(0, others - enemiesNow));
    }
    const int allies = std::max(0, alliesBox.getSelectedEntryIntData());
    enemiesAlliedBox.setEnabled(enemiesBox.getSelectedEntryIntData() > 1);
    const bool anyAllies = allies > 0;
    allyAIBox.setEnabled(anyAllies);
    for(Slider* box : {&allyBaseBox, &allyArmyBox}) box->setEnabled(anyAllies);
    onModChanged();
}

std::string RandomSkirmishMenu::selectedMod() const {
    const int index = modBox.getSelectedEntryIntData();
    return index >= 0 && index < static_cast<int>(availableMods.size()) ? availableMods[index].name : std::string();
}

void RandomSkirmishMenu::onModChanged() {
    // City size exists only for a mod that runs the city simulation; other mods hide the rows.
    const bool city = RandomSkirmish::modeForMod(selectedMod()) == Mode::DuneCity;
    for(Widget* widget : std::initializer_list<Widget*>{&allyCityBox, &enemyCityBox, &playerCityBox,
                                                        allyCityCaption, enemyCityCaption, playerCityCaption})
        if(widget != nullptr) widget->setVisible(city);
    allyCityBox.setEnabled(city && alliesBox.getSelectedEntryIntData() > 0);
    enemyCityBox.setEnabled(city);
    playerCityBox.setEnabled(city);
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
    params.mod = selectedMod();
    params.mode = RandomSkirmish::modeForMod(params.mod);
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
    const auto* partner = PlayerFactory::getByIndex(partnerBox.getSelectedEntryIntData());
    params.playerSide.playerClass = partnerBox.getSelectedEntryIntData() >= 0 && partner ? partner->getPlayerClass() : std::string();
    params.playerSide.base = static_cast<BaseSize>(playerBaseBox.getSelectedEntryIntData());
    params.playerSide.army = playerArmyBox.getSelectedEntryIntData();
    params.playerSide.city = playerCityBox.getSelectedEntryIntData();
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
    if(!RandomSkirmish::activateMod(params.mod)) {
        openWindow(MsgBox::create(_("The chosen mod could not be activated.")));
        return false;
    }
    // The Dune II remaster needs its artwork ready, exactly as when Custom Game picks it.
    const auto gate = ensureDune2RArtworkReady(params.mod);
    if(!gate.ready) {
        if(!gate.message.empty()) openWindow(MsgBox::create(gate.message));
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
