#ifndef CIVWARSMENU_H
#define CIVWARSMENU_H

#include <Menu/MenuBase.h>
#include <MapEditor/CivWarsGenerator.h>
#include <GameInitSettings.h>
#include <GUI/StaticContainer.h>
#include <GUI/Label.h>
#include <GUI/TextButton.h>
#include <GUI/DropDownBox.h>
#include <GUI/Slider.h>
#include <GUI/Checkbox.h>
#include <GUI/TextBox.h>
#include <GUI/HBox.h>
#include <GUI/dune/AIPlayerSelector.h>
#include <array>

namespace CivWars {
/// Fixed mode requirements; all other rules are chosen in Game Rules.
SettingsClass::GameOptionsClass matchRules(SettingsClass::GameOptionsClass rules);
GameInitSettings makeInit(const SkirmishGenerator::Result& result,
                          const SettingsClass::GameOptionsClass& rules,
                          const CivWarsGenerator::Params& params);
}

/// Offline play, or the generic PlayerN map step used by the online host flow.
class CivWarsMenu final : public MenuBase {
public:
    explicit CivWarsMenu(bool multiplayer = false,
                         const SettingsClass::GameOptionsClass* initialRules = nullptr);
    const std::string& getCreatedMap() const { return createdMap; }
    SettingsClass::GameOptionsClass getMatchRules() const;

private:
    void addRow(int column, int row, const std::string& caption, Widget* control);
    void showParams(const CivWarsGenerator::Params& params);
    CivWarsGenerator::Params readParams() const;
    void remember(const CivWarsGenerator::Params& params) const;
    bool generate(CivWarsGenerator::Params& params, SkirmishGenerator::Result& result);
    bool prepareStart(GameInitSettings& init);
    void onStart();
    void onHost();
    void onCreateMap();
    void onSaveMap();
    void onNewSeed();
    void onGameRules();
    void onCancel();
    void onSpectateChanged();
    void onChildWindowClose(Window* child) override;

    bool multiplayer = false;
    int houseCount = 8;
    CivWarsGenerator::Params loaded;
    std::string createdMap;
    SettingsClass::GameOptionsClass rules;
    bool rulesChosen = false;

    StaticContainer windowWidget;
    Label title, description, setupLabel, statusLabel;
    std::array<Label, 12> captions;
    int usedCaptions = 0;
    Slider teamsBox, sizeBox, wormsBox, spiceBox;
    TextBox seedBox;
    TextButton newSeedButton;
    Checkbox fixedSeedBox, spectateBox;
    DropDownBox houseBox, researchPaceBox;
    HBox allyAIBox, enemyAIBox, spectateAIBox;
    AIPlayerSelector allyAI, enemyAI, spectateAI;
    TextButton backButton, rulesButton, saveButton, hostButton, startButton;
};

#endif // CIVWARSMENU_H
