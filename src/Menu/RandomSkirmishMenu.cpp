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
#include <GUI/dune/GameOptionsWindow.h>
#include <Menu/Dune2RReadinessMenu.h>
#include <ObjectData.h>
#include <mod/ModManager.h>
#include <players/PlayerFactory.h>
#include <misc/WebRuntime.h>
#include <misc/MenuPalette.h>
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

GameInitSettings makeInit(const Result& result, const SettingsClass::GameOptionsClass& rules, const Params& params,
                          GameInitSettings::GraphicsSkin skin) {
    // A partner AI shares the player's house, as a Custom Game's second player slot does.
    bool sharedHouse = false;
    for(const HouseSetup& house : result.houses)
        sharedHouse = sharedHouse || (house.role == Role::Human && PlayerFactory::getByPlayerClass(house.partnerClass) != nullptr);
    GameInitSettings init(result.name, result.ini, sharedHouse, rules);
    // Custom Game's offline Spectate: the human seat goes to an AI and the local player only
    // watches. Local to this machine; never serialized or sent anywhere.
    init.setLocalSpectator(params.spectate);
    for(const HouseSetup& house : result.houses) {
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(house.house), house.team);
        info.graphicsSkin = skin;
        const std::string houseName = getHouseNameByNumber(static_cast<HOUSETYPE>(house.house));
        if(house.role == Role::Human) {
            if(params.spectate) {
                // The house's own AI, as the Allies/Enemies AI boxes offer it: Builder + Fighter, not a human.
                const std::string& wanted = params.spectateClass;
                const auto* data = PlayerFactory::getByPlayerClass(wanted);
                const bool usable = data != nullptr && wanted != HUMANPLAYERCLASS && data->getRole() == AIRole::Both;
                info.addPlayerInfo(GameInitSettings::PlayerInfo(houseName, usable ? wanted : std::string(DEFAULTAIPLAYERCLASS)));
            } else {
                info.addPlayerInfo(GameInitSettings::PlayerInfo(settings.general.playerName, HUMANPLAYERCLASS));
            }
            if(const auto* partner = PlayerFactory::getByPlayerClass(house.partnerClass))
                info.addPlayerInfo(GameInitSettings::PlayerInfo(houseName + " (" + AIPlayerSelector::partnerLabel(*partner) + ")", house.partnerClass));
        } else {
            const auto* player = PlayerFactory::getByPlayerClass(house.playerClass);
            info.addPlayerInfo(GameInitSettings::PlayerInfo(houseName, player ? house.playerClass : std::string(DEFAULTAIPLAYERCLASS)));
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
constexpr int kMultiplayerRows = 12;  ///< Map (7) + Game (4), and a row of slack

constexpr int kNoPartner = 0;         ///< the partner selector's "None" entry
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

/// An AI of a house of its own (allies, enemies, the watched house): every AI, Builder + Fighter.
/// Helpers share a human's house, so the role is not offered here.
void fillAI(AIPlayerSelector& selector, HBox& row) {
    selector.setShowRole(false);
    selector.setFamilies();
    row.addWidget(&selector.aiBox);
    row.addWidget(&selector.difficultyBox);
}

/// Selects a saved class, else qBotMedium (an old or unlisted class).
void selectAI(AIPlayerSelector& selector, const std::string& playerClass) {
    if(!selector.selectPlayerClass(playerClass)) selector.selectPlayerClass("qBotMedium");
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

RandomSkirmishMenu::RandomSkirmishMenu() : RandomSkirmishMenu(false, std::string()) {}

RandomSkirmishMenu::RandomSkirmishMenu(const std::string& hostMod) : RandomSkirmishMenu(true, hostMod) {}

RandomSkirmishMenu::RandomSkirmishMenu(bool multiplayer, const std::string& hostMod)
 : MenuBase(), multiplayer(multiplayer), hostMod(hostMod) {
    SDL_Texture* background = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(background);
    resize(getTextureSize(background));
    setWindowWidget(&windowWidget);

    houseCount = 0;
    for(int h = 0; h < NUM_HOUSES; ++h) houseCount += isCustomGameHouseAvailable(static_cast<HOUSETYPE>(h)) ? 1 : 0;

    const int width = getSize().x, height = getSize().y;
    // Rows fit between the title and the status line, however short the window.
    rowPitch = std::clamp((height - 44 - 70) / (multiplayer ? kMultiplayerRows : kLeftColumnRows), 18, 25);
    title.setText(multiplayer ? _("Random Map") : _("Random Skirmish"));
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
    for(int credits : kCreditPresets) creditsBox.addEntry(withThousands(credits), credits);
    for(int level = 1; level <= 9; ++level) techBox.addEntry(std::to_string(level), level);

    int row = 0;
    addHeader(0, row++, _("Map"));
    addRow(0, row++, _("Size"), &sizeBox);
    addRow(0, row++, _("Rock"), &rockBox);
    addRow(0, row++, _("Spice"), &spiceBox);
    addRow(0, row++, _("Layout"), &symmetricBox);
    addRow(0, row++, _("Seed"), &seedBox);
    addRow(0, row++, "", &fixedSeedBox);
    addHeader(0, row++, _("Game"));

    if(multiplayer) {
        // The host's game: its mod (chosen in the map list) and Game Rules stay there. Who plays
        // each start, their houses and teams are the lobby's; every start begins the same.
        std::string modLabel = hostMod;
        for(const ModInfo& mod : ModManager::instance().listModChoices())
            if(mod.matchesSelectionName(hostMod)) modLabel = mod.selectionLabel();
        hostModLabel.setText(modLabel);
        hostModLabel.setTextFontSize(13);
        addRow(0, row++, _("Mod"), &hostModLabel);
        addRow(0, row++, _("Credits"), &creditsBox);
        addRow(0, row++, _("Tech level"), &techBox);

        for(int starts = kMinStarts; starts <= std::max(kMinStarts, maxStarts(houseCount)); ++starts)
            startsBox.addEntry(std::to_string(starts), starts);
        fillSide(startBaseBox, startArmyBox, startCityBox);
        row = 0;
        addHeader(1, row++, _("Players"));
        addRow(1, row++, _("Start positions"), &startsBox);
        addHeader(1, row++, _("Every house starts with"));
        addRow(1, row++, _("Base"), &startBaseBox);
        addRow(1, row++, _("Army (credits)"), &startArmyBox);
        addRow(1, row++, _("City"), &startCityBox);
        startCityCaption = &captions[usedCaptions - 1];
        lobbyNote.setText(_("Humans join and take starts in the next screen. Empty starts can be AI or closed; houses and teams are chosen there too."));
        lobbyNote.setTextFontSize(12);
        windowWidget.addWidget(&lobbyNote, Point(columnX(width, 1) + 8, 44 + row * rowPitch),
                               Point(columnSpan(width) - 8, 3 * rowPitch));
    } else {
        houseBox.addEntry(_("Random"), HOUSE_INVALID);
        for(int h = 0; h < NUM_HOUSES; ++h)
            if(isCustomGameHouseAvailable(static_cast<HOUSETYPE>(h))) houseBox.addEntry(getHouseNameByNumber(static_cast<HOUSETYPE>(h)), h);
        // Every installed mod, as in Custom Game. City size only applies to a mod that runs the city sim.
        availableMods = ModManager::instance().listModChoices();
        for(size_t i = 0; i < availableMods.size(); ++i) modBox.addEntry(availableMods[i].selectionLabel(), static_cast<int>(i));
        modBox.setOnSelectionChange([this](bool) { onModChanged(); });
        alliesBox.setOnSelectionChange([this](bool) { onPlayersChanged(true); });
        enemiesBox.setOnSelectionChange([this](bool) { onPlayersChanged(false); });
        enemiesAlliedBox.setText(_("Enemies are allied"));
        // Custom Game's offline Spectate: an AI takes the player's seat and the player watches.
        spectateBox.setText(_("Spectate"));
        spectateBox.setTooltipText(_("Watch an AI-vs-AI match. Your house is played by the AI you choose in "
                                     "\"Your house AI\" (shown while this is ticked), with its partner, base, "
                                     "army and city as set; you get the whole map and can inspect any "
                                     "building, but give no orders."));
        spectateBox.setOnClick([this]() { onSpectateChanged(); });
        fillAI(spectateAI, spectateAIBox);
        const std::string spectateAITip = _("The AI that plays your house while you spectate.");
        spectateAI.aiBox.setTooltipText(spectateAITip);
        spectateAI.difficultyBox.setTooltipText(spectateAITip);
        // The DuneCity skin, one choice for every house as in the campaign; shared with it.
        skinBox.addEntry(_("SimCity"), 0);
        skinBox.addEntry(_("Dune2"), 1);
        skinBox.setSelectedItem(settings.general.duneCityCampaignSkin == 1 ? 1 : 0);

        fillAI(allyAI, allyAIBox);
        fillAI(enemyAI, enemyAIBox);
        fillSide(allyBaseBox, allyArmyBox, allyCityBox);
        fillSide(enemyBaseBox, enemyArmyBox, enemyCityBox);
        fillSide(playerBaseBox, playerArmyBox, playerCityBox);
        // A partner shares the player's house: a QuantBot that builds, fights, or does both.
        partnerAI.addSpecial(_("None"), kNoPartner);
        partnerAI.setFamilies({"QuantBot"});
        // AI, Difficulty and Role share the partner's row; the role labels are the longest.
        partnerBox.addWidget(&partnerAI.aiBox, 1.0);
        partnerBox.addWidget(&partnerAI.difficultyBox, 1.0);
        partnerBox.addWidget(&partnerAI.roleBox, 1.5);

        addRow(0, row++, _("Mod"), &modBox);
        addRow(0, row++, _("Your house"), &houseBox);
        addRow(0, row++, _("Credits"), &creditsBox);
        addRow(0, row++, _("Tech level"), &techBox);
        addHeader(0, row++, _("You start with"));
        playerHeader = &captions[usedCaptions - 1];
        addRow(0, row++, _("AI partner"), &partnerBox);
        addRow(0, row++, _("Base"), &playerBaseBox);
        addRow(0, row++, _("Army (credits)"), &playerArmyBox);
        addRow(0, row++, _("City"), &playerCityBox);
        playerCityCaption = &captions[usedCaptions - 1];

        row = 0;
        addHeader(1, row++, _("Players"));
        addRow(1, row++, _("Allies"), &alliesBox);
        addRow(1, row++, _("Enemies"), &enemiesBox);
        addRow(1, row++, "", &enemiesAlliedBox);
        addRow(1, row++, "", &spectateBox);
        addRow(1, row++, _("Your house AI"), &spectateAIBox);
        spectateAICaption = &captions[usedCaptions - 1];
        addHeader(1, row++, _("Allies start with"));
        addRow(1, row++, _("AI"), &allyAIBox);
        addRow(1, row++, _("Base"), &allyBaseBox);
        addRow(1, row++, _("Army (credits)"), &allyArmyBox);
        addRow(1, row++, _("City"), &allyCityBox);
        allyCityCaption = &captions[usedCaptions - 1];
        addHeader(1, row++, _("Enemies start with"));
        addRow(1, row++, _("AI"), &enemyAIBox);
        addRow(1, row++, _("Base"), &enemyBaseBox);
        addRow(1, row++, _("Army (credits)"), &enemyArmyBox);
        addRow(1, row++, _("City"), &enemyCityBox);
        enemyCityCaption = &captions[usedCaptions - 1];
        addRow(1, row++, _("City skin"), &skinBox);
        skinCaption = &captions[usedCaptions - 1];
    }

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
    // Desert skins draw the original background's house crest (53x54, 11 px in) in the
    // bottom-left corner: the status line and the button row keep clear of it, centred.
    const int crestInset = MenuTheme::isDesertUISkin() ? std::max(0, 11 + 53 + 6 - columnX(width, 0)) : 0;
    windowWidget.addWidget(&statusLabel, Point(16 + crestInset, height - 66), Point(width - 32 - 2 * crestInset, 22));

    backButton.setText(_("Back"));
    backButton.setOnClick([this]() { onCancel(); });
    const int left = columnX(width, 0) + crestInset, span = contentWidth(width) - 2 * crestInset;
    const int buttonWidth = std::min(160, (span - 3 * 16) / 4);
    const int gap = (span - 4 * buttonWidth) / 3;
    if(multiplayer) {
        // Back on the left, Next (save the map and choose players) on the right.
        nextButton.setText(_("Next"));
        nextButton.setOnClick([this]() { onCreateMap(); });
        windowWidget.addWidget(&backButton, Point(left, height - 38), Point(buttonWidth, 28));
        windowWidget.addWidget(&nextButton, Point(left + 3 * (buttonWidth + gap), height - 38), Point(buttonWidth, 28));
        nextButton.setActive();
    } else {
        rulesButton.setText(_("Game rules..."));
        rulesButton.setOnClick([this]() { onGameRules(); });
        saveButton.setText(_("Save map"));
        saveButton.setOnClick([this]() { onSaveMap(); });
        startButton.setText(_("Start"));
        startButton.setOnClick([this]() { onStart(); });
        // Back on the left, Start on the right, Game rules and Save map between them.
        TextButton* buttons[] = {&backButton, &rulesButton, &saveButton, &startButton};
        for(int i = 0; i < 4; ++i)
            windowWidget.addWidget(buttons[i], Point(left + i * (buttonWidth + gap), height - 38), Point(buttonWidth, 28));
        startButton.setActive();
    }

    Params params;
    params.multiplayer = multiplayer;
    // A hosted game starts on mirrored, equal starts unless the host turned that off before.
    params.symmetric = multiplayer;
    params.seed = freshSeed();
    params.load(INIFile(getConfigFilepath()));
    if(multiplayer) {
        params.mod = hostMod;
        params.mode = RandomSkirmish::modeForMod(hostMod);
    }
    if(!params.fixedSeed) params.seed = freshSeed();
    params.clamp(houseCount);
    loaded = params;
    showParams(params);
}

RandomSkirmishMenu::~RandomSkirmishMenu() = default;

void RandomSkirmishMenu::addHeader(int column, int row, const std::string& caption) {
    if(usedCaptions >= static_cast<int>(captions.size())) return;
    Label& label = captions[usedCaptions++];
    label.setText(caption);
    label.setTextFontSize(16);
    const int columnWidth = columnSpan(getSize().x);
    const int y = 44 + row * rowPitch;
    int x = columnX(getSize().x, column);
    // Desert skins: the first header sits beside the background's top-left crest (53x54, 11 px in).
    if(MenuTheme::isDesertUISkin() && column == 0 && y < 11 + 54) x = std::max(x, 11 + 53 + 6);
    windowWidget.addWidget(&label, Point(x, y), Point(columnWidth - (x - columnX(getSize().x, column)), rowPitch - kRowGap));
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
    for(auto [side, ai, base, army, city] : {std::tuple{&params.allySide, &allyAI, &allyBaseBox, &allyArmyBox, &allyCityBox},
                                             std::tuple{&params.enemySide, &enemyAI, &enemyBaseBox, &enemyArmyBox, &enemyCityBox}}) {
        selectAI(*ai, side->playerClass);
        selectData(*base, static_cast<int>(side->base));
        selectData(*army, side->army);
        selectData(*city, side->city);
    }
    // The player's own start; "None" is selected when no partner is chosen or offered.
    if(params.playerSide.playerClass.empty() || !partnerAI.selectPlayerClass(params.playerSide.playerClass))
        partnerAI.selectSpecial(kNoPartner);
    selectData(playerBaseBox, static_cast<int>(params.playerSide.base));
    selectData(playerArmyBox, params.playerSide.army);
    selectData(playerCityBox, params.playerSide.city);
    spectateBox.setChecked(params.spectate);
    // The watched house's AI; a class it does not offer (a helper, an old name) falls back to qBotMedium.
    selectAI(spectateAI, params.spectateClass);
    onSpectateChanged();
    // Multiplayer: the number of starts and what every one of them begins with.
    selectData(startsBox, params.starts);
    selectData(startBaseBox, static_cast<int>(params.startSide.base));
    selectData(startArmyBox, params.startSide.army);
    selectData(startCityBox, params.startSide.city);
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
    allyAI.setEnabled(anyAllies);
    for(Slider* box : {&allyBaseBox, &allyArmyBox}) box->setEnabled(anyAllies);
    onModChanged();
}

std::string RandomSkirmishMenu::selectedMod() const {
    if(multiplayer) return hostMod;
    const int index = modBox.getSelectedEntryIntData();
    return index >= 0 && index < static_cast<int>(availableMods.size()) ? availableMods[index].name : std::string();
}

void RandomSkirmishMenu::onModChanged() {
    // City size exists only for a mod that runs the city simulation; other mods hide the rows.
    const bool city = RandomSkirmish::modeForMod(selectedMod()) == Mode::DuneCity;
    for(Widget* widget : std::initializer_list<Widget*>{&allyCityBox, &enemyCityBox, &playerCityBox, &startCityBox,
                                                        allyCityCaption, enemyCityCaption, playerCityCaption, startCityCaption})
        if(widget != nullptr) widget->setVisible(city);
    // The skin row exists only in single player (a hosted game's rows choose skins in the lobby).
    if(skinCaption != nullptr) {
        skinBox.setVisible(city);
        skinBox.setEnabled(city);
        skinCaption->setVisible(city);
    }
    allyCityBox.setEnabled(city && alliesBox.getSelectedEntryIntData() > 0);
    enemyCityBox.setEnabled(city);
    playerCityBox.setEnabled(city);
    startCityBox.setEnabled(city);
}

void RandomSkirmishMenu::onSpectateChanged() {
    // Every "You start with" row keeps its meaning: the watched house still starts with that
    // partner, base, army and city. Only who plays it changes.
    const bool watch = spectateBox.isChecked();
    // Like the City rows of a non-city mod, a row that does not apply is hidden, not greyed:
    // a drop-down has no disabled look.
    spectateAIBox.setVisible(watch);
    spectateAI.setEnabled(watch);
    if(spectateAICaption != nullptr) spectateAICaption->setVisible(watch);
    if(playerHeader != nullptr) playerHeader->setText(watch ? _("Your house starts with") : _("You start with"));
}

void RandomSkirmishMenu::onNewSeed() {
    seedBox.setText(std::to_string(freshSeed()));
}

Params RandomSkirmishMenu::readParams() const {
    // Starts from the last-used choices, so the ones this mode does not show are kept as they were.
    Params params = loaded;
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
    params.credits = creditsBox.getSelectedEntryIntData();
    params.mod = selectedMod();
    params.mode = RandomSkirmish::modeForMod(params.mod);
    params.techLevel = techBox.getSelectedEntryIntData();
    if(multiplayer) {
        params.starts = startsBox.getSelectedEntryIntData();
        params.startSide.base = static_cast<BaseSize>(startBaseBox.getSelectedEntryIntData());
        params.startSide.army = startArmyBox.getSelectedEntryIntData();
        params.startSide.city = startCityBox.getSelectedEntryIntData();
        params.clamp(houseCount);
        return params;
    }
    params.house = houseBox.getSelectedEntryIntData();
    params.allies = alliesBox.getSelectedEntryIntData();
    params.enemies = enemiesBox.getSelectedEntryIntData();
    params.enemiesAllied = enemiesAlliedBox.isChecked();
    for(auto [side, ai, base, army, city] : {std::tuple{&params.allySide, &allyAI, &allyBaseBox, &allyArmyBox, &allyCityBox},
                                             std::tuple{&params.enemySide, &enemyAI, &enemyBaseBox, &enemyArmyBox, &enemyCityBox}}) {
        const std::string playerClass = ai->getPlayerClass();
        side->playerClass = playerClass.empty() ? std::string(DEFAULTAIPLAYERCLASS) : playerClass;
        side->base = static_cast<BaseSize>(base->getSelectedEntryIntData());
        side->army = army->getSelectedEntryIntData();
        side->city = city->getSelectedEntryIntData();
    }
    // "None" leaves the class empty: no partner.
    params.playerSide.playerClass = partnerAI.getPlayerClass();
    params.playerSide.base = static_cast<BaseSize>(playerBaseBox.getSelectedEntryIntData());
    params.playerSide.army = playerArmyBox.getSelectedEntryIntData();
    params.playerSide.city = playerCityBox.getSelectedEntryIntData();
    params.spectate = spectateBox.isChecked();
    const std::string watchedAI = spectateAI.getPlayerClass();
    if(!watchedAI.empty()) params.spectateClass = watchedAI;
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

bool RandomSkirmishMenu::prepareStart(GameInitSettings& init) {
    Params params;
    Result result;
    if(!generate(params, result)) return false;
    const int skin = skinBox.getSelectedEntryIntData() == 1 ? 1 : 0;
    if(!multiplayer && settings.general.duneCityCampaignSkin != skin) {
        // Remember the choice with the campaign's, so every DuneCity screen offers it next time.
        settings.general.duneCityCampaignSkin = skin;
        INIFile config(getConfigFilepath());
        config.setIntValue("General", "DuneCity Campaign Skin", skin);
        if(config.saveChangesTo(getConfigFilepath())) WebRuntime::syncPersistentFiles();
    }
    init = RandomSkirmish::makeInit(result, matchRules(), params, GameInitSettings::sanitizeGraphicsSkin(skin));
    if(!params.fixedSeed) {
        // The next visit offers a new map; this one stays reproducible from its name.
        params.seed = freshSeed();
        remember(params);
    }
    return true;
}

void RandomSkirmishMenu::onStart() {
    GameInitSettings init;
    if(!prepareStart(init)) return;
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

void RandomSkirmishMenu::onCreateMap() {
    Params params;
    Result result;
    if(!generate(params, result)) return;
    // Every start must hold the same base, army and city: a start that lost items to a crowded
    // map would not, so the host is asked for more room instead.
    int skipped = 0;
    for(const auto& house : result.houses) skipped += house.skipped;
    if(skipped > 0) {
        openWindow(MsgBox::create(_("The starts do not fit on this map: ") + std::to_string(skipped)
            + _(" planned buildings/units were left out, so the starts would not be equal.\nTry a larger map, a smaller start or fewer start positions.")));
        return;
    }
    const std::string path = RandomSkirmish::saveMap(result);
    if(path.empty()) {
        openWindow(MsgBox::create(_("The map could not be saved.")));
        return;
    }
    if(!params.fixedSeed) {
        // The next visit offers a new map; this one stays in MP User Maps under its name.
        params.seed = freshSeed();
        remember(params);
    }
    createdMap = path;
    quit();
}

void RandomSkirmishMenu::onCancel() {
    remember(readParams());
    quit();
}

const SettingsClass::GameOptionsClass& RandomSkirmishMenu::matchRules() const {
    return rulesChosen ? rules : effectiveGameOptions;
}

void RandomSkirmishMenu::onGameRules() {
    // Until the player has set rules here, the window opens on the chosen mod's own defaults.
    if(!rulesChosen) {
        if(!RandomSkirmish::activateMod(selectedMod())) {
            openWindow(MsgBox::create(_("The chosen mod could not be activated.")));
            return;
        }
        rules = effectiveGameOptions;
    }
    openWindow(GameOptionsWindow::create(rules));
}

void RandomSkirmishMenu::onChildWindowClose(Window* pChildWindow) {
    if(auto* pRulesWindow = dynamic_cast<GameOptionsWindow*>(pChildWindow)) {
        rules = pRulesWindow->getGameOptions();
        rulesChosen = true;
    }
}
