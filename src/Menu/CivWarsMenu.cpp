#include <Menu/CivWarsMenu.h>
#include <Menu/RandomSkirmishMenu.h>
#include <Menu/PlaySetup.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/TextManager.h>
#include <GUI/MsgBox.h>
#include <GUI/dune/GameOptionsWindow.h>
#include <mod/ModManager.h>
#include <misc/MenuPalette.h>
#include <misc/WebRuntime.h>
#include <globals.h>
#include <main.h>
#include <sand.h>
#include <algorithm>
#include <random>

namespace CivWars {
SettingsClass::GameOptionsClass matchRules(SettingsClass::GameOptionsClass rules) {
    rules.researchLab = true;
    rules.sandwormsRespawn = true;
    rules.trooperAntiAir = true;
    rules.cityEffects = true;
    return rules;
}

GameInitSettings makeInit(const SkirmishGenerator::Result& result,
                          const SettingsClass::GameOptionsClass& rules,
                          const CivWarsGenerator::Params& params) {
    SkirmishGenerator::Params roster;
    roster.spectate = params.spectate;
    roster.spectateClass = params.spectateClass;
    auto init = RandomSkirmish::makeInit(result, matchRules(rules), roster,
        GameInitSettings::sanitizeGraphicsSkin(settings.general.duneCityCampaignSkin));
    init.setRandomSeed(params.seed);
    return init;
}
}

namespace {
uint32_t freshCivSeed() { return std::random_device{}() % 1000000000u; }

template<class Box> void selectCivData(Box& box, int data) {
    for(int i = 0; i < box.getNumEntries(); ++i)
        if(box.getEntryIntData(i) == data) { box.setSelectedItem(i); return; }
    if(box.getNumEntries() > 0) box.setSelectedItem(0);
}

void fillCivAI(AIPlayerSelector& selector, HBox& row) {
    selector.setShowRole(false);
    selector.setFamilies();
    row.addWidget(&selector.aiBox);
    row.addWidget(&selector.difficultyBox);
}

void selectCivAI(AIPlayerSelector& selector, const std::string& name) {
    if(!selector.selectPlayerClass(name)) selector.selectPlayerClass("qBotMedium");
}
}

CivWarsMenu::CivWarsMenu(bool multiplayer) : multiplayer(multiplayer) {
    RandomSkirmish::activateMod("CivWars");
    auto* background = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(background);
    resize(getTextureSize(background));
    setWindowWidget(&windowWidget);
    const int width = getSize().x, height = getSize().y;
    title.setText(_("Civilization Wars"));
    title.setTextFontSize(22);
    title.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&title, Point(70, 12), Point(width - 140, 32));
    description.setText(multiplayer ? _("Three lanes. Two teams. Houses and seats are chosen in the lobby.")
                                   : _("Three lanes. Two teams. Your city is the economy."));
    description.setTextFontSize(13);
    description.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&description, Point(70, 48), Point(width - 140, 36));

    for(int n = 1; n <= 3; ++n) teamsBox.addEntry(std::to_string(n) + "v" + std::to_string(n), n);
    for(int i = 0; i < static_cast<int>(CivWarsGenerator::kMapSizes.size()); ++i) {
        const Coord size = CivWarsGenerator::kMapSizes[i];
        sizeBox.addEntry(std::to_string(size.x) + " x " + std::to_string(size.y), i);
    }
    for(int n = 0; n <= 4; ++n) wormsBox.addEntry(std::to_string(n), n);
    const char* spiceNames[] = {"None", "Low", "Medium", "High", "Very high"};
    for(int n = 0; n < 5; ++n) spiceBox.addEntry(_(spiceNames[n]), n);
    seedBox.setAllowedChars("0123456789");
    seedBox.setMaximumTextLength(10);
    newSeedButton.setText(_("New"));
    newSeedButton.setOnClick([this]() { onNewSeed(); });
    fixedSeedBox.setText(_("Keep this seed"));
    addRow(0, 0, _("Teams"), &teamsBox);
    addRow(0, 1, _("Size"), &sizeBox);
    addRow(0, 2, _("Worms / lane"), &wormsBox);
    addRow(0, 3, _("Spice"), &spiceBox);
    addRow(0, 4, _("Seed"), &seedBox);
    const Point seedPosition = windowWidget.getWidgetPosition(&seedBox);
    const Point seedSize = seedBox.getSize();
    windowWidget.setWidgetGeometry(&seedBox, seedPosition, Point(seedSize.x - 48, seedSize.y));
    windowWidget.addWidget(&newSeedButton, Point(seedPosition.x + seedSize.x - 44, seedPosition.y), Point(44, seedSize.y));
    addRow(0, 5, "", &fixedSeedBox);

    if(!multiplayer) {
        houseBox.addEntry(_("Random"), HOUSE_INVALID);
        houseCount = 0;
        for(int h = 0; h < NUM_PLAYER_HOUSES; ++h) {
            if(h == HOUSE_NATIVES || !isCustomGameHouseAvailable(static_cast<HOUSETYPE>(h))) continue;
            houseBox.addEntry(getHouseNameByNumber(static_cast<HOUSETYPE>(h)), h);
            ++houseCount;
        }
        fillCivAI(allyAI, allyAIBox);
        fillCivAI(enemyAI, enemyAIBox);
        fillCivAI(spectateAI, spectateAIBox);
        spectateBox.setText(_("Spectate"));
        spectateBox.setOnClick([this]() { onSpectateChanged(); });
        teamsBox.setOnSelectionChange([this](bool) { allyAI.setEnabled(teamsBox.getSelectedEntryIntData() > 1); });
        addRow(1, 0, _("House"), &houseBox);
        addRow(1, 1, _("Allies"), &allyAIBox);
        addRow(1, 2, _("Enemies"), &enemyAIBox);
        addRow(1, 3, "", &spectateBox);
        addRow(1, 4, _("Watched AI"), &spectateAIBox);
    }
    setupLabel.setText(_("Starts: Yard, Research Lab, Refinery, Carryall, rocket turrets, power and a small city.\n"
                         "Credits 0. Tech level 1. Research unlocks higher tech. No free armies or automatic income nodes."));
    setupLabel.setTextFontSize(13);
    setupLabel.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&setupLabel, Point(70, 286), Point(width - 140, 72));
    statusLabel.setTextFontSize(12);
    statusLabel.setAlignment(Alignment_HCenter);
    windowWidget.addWidget(&statusLabel, Point(70, height - 108), Point(width - 140, 32));
    const int buttonWidth = (width - 148 - 4 * 6) / (multiplayer ? 3 : 5);
    const int buttonY = height - 58;
    backButton.setText(_("Back"));
    backButton.setOnClick([this]() { onCancel(); });
    rulesButton.setText(_("Game Rules"));
    rulesButton.setOnClick([this]() { onGameRules(); });
    saveButton.setText(_("Save Map"));
    saveButton.setOnClick([this]() { onSaveMap(); });
    hostButton.setText(_("Host Online"));
    hostButton.setOnClick([this]() { onHost(); });
    startButton.setText(multiplayer ? _("Next") : _("Start"));
    startButton.setOnClick([this]() { if(this->multiplayer) onCreateMap(); else onStart(); });
    int index = 0;
    for(auto* button : {&backButton, &rulesButton, &saveButton, &hostButton, &startButton}) {
        if(multiplayer && (button == &saveButton || button == &hostButton)) continue;
        windowWidget.addWidget(button, Point(74 + index++ * (buttonWidth + 6), buttonY), Point(buttonWidth, 28));
    }
    INIFile config(getConfigFilepath());
    loaded.load(config);
    loaded.multiplayer = multiplayer;
    loaded.clamp(houseCount);
    showParams(loaded);
    startButton.setActive();
}

void CivWarsMenu::addRow(int column, int row, const std::string& caption, Widget* control) {
    const int span = std::min(392, (getSize().x - 48) / 2);
    const int x = (getSize().x - 2 * span - 16) / 2 + column * (span + 16);
    const int y = 96 + row * 30;
    const int labelWidth = span * 2 / 5;
    if(!caption.empty()) {
        auto& label = captions[usedCaptions++];
        label.setText(caption);
        label.setTextFontSize(13);
        windowWidget.addWidget(&label, Point(x, y), Point(labelWidth - 4, 26));
    }
    windowWidget.addWidget(control, Point(x + labelWidth, y), Point(span - labelWidth, 26));
}

void CivWarsMenu::showParams(const CivWarsGenerator::Params& params) {
    selectCivData(teamsBox, params.teamSize);
    selectCivData(sizeBox, params.size);
    selectCivData(wormsBox, params.wormsPerLane);
    selectCivData(spiceBox, static_cast<int>(params.spice));
    seedBox.setText(std::to_string(params.seed));
    fixedSeedBox.setChecked(params.fixedSeed);
    if(!multiplayer) {
        selectCivData(houseBox, params.house);
        selectCivAI(allyAI, params.allyClass);
        selectCivAI(enemyAI, params.enemyClass);
        selectCivAI(spectateAI, params.spectateClass);
        spectateBox.setChecked(params.spectate);
        allyAI.setEnabled(params.teamSize > 1);
        onSpectateChanged();
    }
}

CivWarsGenerator::Params CivWarsMenu::readParams() const {
    auto params = loaded;
    params.teamSize = teamsBox.getSelectedEntryIntData();
    params.size = sizeBox.getSelectedEntryIntData();
    params.wormsPerLane = wormsBox.getSelectedEntryIntData();
    params.spice = static_cast<SkirmishGenerator::Spice>(spiceBox.getSelectedEntryIntData());
    if(!seedBox.getText().empty())
        params.seed = static_cast<uint32_t>(std::min<unsigned long long>(std::stoull(seedBox.getText()), 0xFFFFFFFFull));
    params.fixedSeed = fixedSeedBox.isChecked();
    if(!multiplayer) {
        params.house = houseBox.getSelectedEntryIntData();
        params.allyClass = allyAI.getPlayerClass();
        params.enemyClass = enemyAI.getPlayerClass();
        params.spectateClass = spectateAI.getPlayerClass();
        params.spectate = spectateBox.isChecked();
    }
    params.multiplayer = multiplayer;
    params.clamp(houseCount);
    return params;
}

void CivWarsMenu::remember(const CivWarsGenerator::Params& params) const {
    INIFile config(getConfigFilepath());
    params.save(config);
    if(config.saveChangesTo(getConfigFilepath())) WebRuntime::syncPersistentFiles();
    else SDL_Log("Warning: could not save Civilization Wars choices");
}

bool CivWarsMenu::generate(CivWarsGenerator::Params& params, SkirmishGenerator::Result& result) {
    params = readParams();
    remember(params);
    if(!RandomSkirmish::activateMod("CivWars")) {
        openWindow(MsgBox::create(_("The Civilization Wars mod could not be activated.")));
        return false;
    }
    try {
        result = CivWarsGenerator::generate(params, RandomSkirmish::loadCatalogue(SkirmishGenerator::Mode::DuneCity));
    } catch(const std::exception& error) {
        openWindow(MsgBox::create(error.what()));
        return false;
    }
    statusLabel.setText(result.name);
    return true;
}

bool CivWarsMenu::prepareStart(GameInitSettings& init) {
    CivWarsGenerator::Params params;
    SkirmishGenerator::Result result;
    if(!generate(params, result)) return false;
    init = CivWars::makeInit(result, rulesChosen ? rules : effectiveGameOptions, params);
    if(!params.fixedSeed) { params.seed = freshCivSeed(); remember(params); }
    return true;
}

void CivWarsMenu::onStart() {
    GameInitSettings init;
    if(!prepareStart(init)) return;
    startSinglePlayerGame(init);
    quit();
}

void CivWarsMenu::onHost() {
    remember(readParams());
    // The normal online setup owns the lobby, identity checks and direct peer connections.
    playCustomGame(true, true, false, true);
}

void CivWarsMenu::onCreateMap() {
    CivWarsGenerator::Params params;
    SkirmishGenerator::Result result;
    if(!generate(params, result)) return;
    createdMap = RandomSkirmish::saveMap(result);
    if(createdMap.empty()) { openWindow(MsgBox::create(_("The map could not be saved."))); return; }
    if(!params.fixedSeed) { params.seed = freshCivSeed(); remember(params); }
    quit();
}

void CivWarsMenu::onSaveMap() {
    auto params = readParams();
    // User scenarios use generic starts, so every faction can play either team.
    params.multiplayer = true;
    if(!RandomSkirmish::activateMod("CivWars")) {
        openWindow(MsgBox::create(_("The Civilization Wars mod could not be activated.")));
        return;
    }
    try {
        const auto result = CivWarsGenerator::generate(params, RandomSkirmish::loadCatalogue(SkirmishGenerator::Mode::DuneCity));
        const auto path = RandomSkirmish::saveMap(result);
        openWindow(MsgBox::create(path.empty() ? _("The map could not be saved.")
            : _("Saved to MP User Maps:") + "\n" + result.name + ".ini"));
    } catch(const std::exception& error) { openWindow(MsgBox::create(error.what())); }
}

void CivWarsMenu::onNewSeed() { seedBox.setText(std::to_string(freshCivSeed())); }
void CivWarsMenu::onSpectateChanged() { spectateAI.setEnabled(spectateBox.isChecked()); }
void CivWarsMenu::onCancel() { remember(readParams()); quit(); }

void CivWarsMenu::onGameRules() {
    if(!rulesChosen) {
        if(!RandomSkirmish::activateMod("CivWars")) {
            openWindow(MsgBox::create(_("The Civilization Wars mod could not be activated.")));
            return;
        }
        rules = effectiveGameOptions;
    }
    rules = CivWars::matchRules(rules);
    openWindow(GameOptionsWindow::create(rules));
}

void CivWarsMenu::onChildWindowClose(Window* child) {
    if(auto* options = dynamic_cast<GameOptionsWindow*>(child)) {
        rules = CivWars::matchRules(options->getGameOptions());
        rulesChosen = true;
    }
}

SettingsClass::GameOptionsClass CivWarsMenu::getMatchRules() const {
    return CivWars::matchRules(rulesChosen ? rules : effectiveGameOptions);
}
