#ifndef COOP_CAMPAIGN_MENU_H
#define COOP_CAMPAIGN_MENU_H

#include <Menu/MenuBase.h>
#include <GUI/Checkbox.h>
#include <GUI/DropDownBox.h>
#include <GUI/HBox.h>
#include <GUI/Label.h>
#include <GUI/StaticContainer.h>
#include <GUI/TextButton.h>
#include <GUI/TextView.h>
#include <GUI/VBox.h>

class GameInitSettings;

// Separate-faction campaigns keep their own progress, never the solo campaign's.
class CoopCampaignMenu : public MenuBase {
public:
    explicit CoopCampaignMenu(bool LANServer = false);
    void onChildWindowClose(Window* child) override;
    void update() override;

private:
    void onNewCampaign();
    void onResumeCampaign();
    void resume(const std::string& path);
    void openLobby(const GameInitSettings& settings);
    bool LANServer;
    std::string selectedProgress;
    StaticContainer windowWidget;
    VBox mainVBox;
    Label caption;
    TextView description;
    HBox alliesRow, rulesRow, difficultyRow, forcesRow;
    DropDownBox alliedBases;
    Checkbox archonMode, chaosMode, alliedControl, alliedBuilding, easyMode;
    Checkbox extraEnemyForces, extraHardEnemyForces;
    TextButton newCampaignButton, resumeButton, backButton;
};

#endif
