#include <Menu/CoopCampaignMenu.h>
#include <Menu/CrossplayMenu.h>
#include <Menu/CustomGamePlayers.h>
#include <Campaign/CoopCampaignRuntime.h>
#include <Campaign/CoopCampaignSession.h>
#include <Campaign/CoopCampaignTransition.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <Network/WorkshopGameContent.h>
#include <GUI/MsgBox.h>
#include <GUI/Spacer.h>
#include <GUI/dune/LoadSaveWindow.h>
#include <misc/FileSystem.h>
#include <misc/IMemoryStream.h>
#include <misc/fnkdat.h>
#include <mod/ModManager.h>
#include <globals.h>
#include <mmath.h>

#include <filesystem>
#include <stdexcept>
#include <utility>

CoopCampaignMenu::CoopCampaignMenu(bool LANServer) : LANServer(LANServer) {
    auto* background = pGFXManager->getUIGraphic(UI_MenuBackground);
    setBackground(background);
    resize(getTextureSize(background));
    setWindowWidget(&windowWidget);
    windowWidget.addWidget(&mainVBox, Point(36, 24), Point(getRendererWidth() - 72, getRendererHeight() - 48));
    caption.setText(_("Separate-house co-op campaign"));
    caption.setAlignment(Alignment_HCenter);
    mainVBox.addWidget(&caption, 28);
    mainVBox.addWidget(VSpacer::create(12));
    description.setTextFontSize(14);
    description.setText(_("Two or four allied bases; up to eight controllers in Archon mode.\n"
        "Each base chooses its faction, including the same faction. Nine missions follow\n"
        "the host's faction. Checkpoints and progress are separate from solo campaigns.\n\nActive mod: ")
        + ModManager::instance().getActiveModName());
    mainVBox.addWidget(&description, 0.6);
    alliedBases.addEntry(_("Two allied bases"));
    alliedBases.addEntry(_("Four allied bases"));
    alliedBases.setSelectedItem(0);
    alliesRow.addWidget(&alliedBases, 0.4);
    alliesRow.addWidget(HSpacer::create(12));
    archonMode.setText(_("Archon: four bases, up to 8 players"));
    alliesRow.addWidget(&archonMode, 0.6);
    mainVBox.addWidget(&alliesRow, 26);
    alliedControl.setText(_("Allied control"));
    alliedBuilding.setText(_("Allied construction"));
    rulesRow.addWidget(&alliedControl, 0.5);
    rulesRow.addWidget(&alliedBuilding, 0.5);
    mainVBox.addWidget(&rulesRow, 26);
    easyMode.setText(_("Easy Mode"));
    chaosMode.setText(_("Chaos Mode"));
    difficultyRow.addWidget(&easyMode, 0.5);
    difficultyRow.addWidget(&chaosMode, 0.5);
    mainVBox.addWidget(&difficultyRow, 26);
    extraEnemyForces.setText(_("Enemy reinforcements +5"));
    extraHardEnemyForces.setText(_("Enemy reinforcements +10"));
    forcesRow.addWidget(&extraEnemyForces, 0.5);
    forcesRow.addWidget(&extraHardEnemyForces, 0.5);
    mainVBox.addWidget(&forcesRow, 26);
    mainVBox.addWidget(VSpacer::create(12));
    newCampaignButton.setText(_("New campaign"));
    newCampaignButton.setOnClick([this] { onNewCampaign(); });
    mainVBox.addWidget(&newCampaignButton, 28);
    mainVBox.addWidget(VSpacer::create(8));
    resumeButton.setText(_("Resume co-op campaign"));
    resumeButton.setOnClick([this] { onResumeCampaign(); });
    mainVBox.addWidget(&resumeButton, 28);
    mainVBox.addWidget(Spacer::create(), 0.1);
    backButton.setText(_("Back"));
    backButton.setOnClick([this] { quit(); });
    mainVBox.addWidget(&backButton, 26);
}

void CoopCampaignMenu::openLobby(const GameInitSettings& settings) {
    int result;
    if(LANServer) {
        CustomGamePlayers lobby(settings, true, true);
        result = lobby.showMenu();
    } else {
        // The room only admits peers. Every gameplay/control byte uses direct channels.
        result = CrossplayMenu(settings, false, {}, false).showMenu();
    }
    if(result != MENU_QUIT_DEFAULT) quit(result);
}

void CoopCampaignMenu::onNewCampaign() {
    try {
        if(!ModManager::instance().isTornieFamilyContentActive()) throw std::runtime_error("Select a Tornie-family mod first");
        auto& mods = ModManager::instance();
        const auto& roster = mods.getPlayableHouses();
        if(roster.size() < 5) throw std::runtime_error("The active mod needs at least five playable factions");
        const auto seed = static_cast<Uint32>(getRandomInt());
        const auto sessionId = std::to_string(SDL_GetTicks()) + "-" + std::to_string(seed);
        auto session = coop::CoopCampaignSession::create(sessionId,
            mods.getContentBase(mods.getActiveModName()), roster, {roster[0], roster[1]},
            {settings.general.playerName, settings.general.playerName == "Guest" ? "Guest 2" : "Guest"},
            seed, chaosMode.isChecked());
        if(archonMode.isChecked() || alliedBases.getSelectedIndex() == 1)
            session.enableExpandedAllies(archonMode.isChecked());
        session.setAlliedControl(alliedControl.isChecked());
        session.setAlliedBuilding(alliedBuilding.isChecked());
        session.setEasyMode(easyMode.isChecked());
        session.setExtraEnemyForces(extraEnemyForces.isChecked());
        session.setExtraHardEnemyForces(extraHardEnemyForces.isChecked());
        session.skipIntroForEasyMode();
        auto options = effectiveGameOptions;
        options.easyMode = easyMode.isChecked();
        options.chaosMode = chaosMode.isChecked();
        options.immortalHumanPlayer = false;
        auto init = coop::makeGameSettings(session, options, settings.general.playerName + " — Co-op");
        // Only a brand-new lobby may rebind the source campaign with its host faction.
        init.setMapData(init.getFiledata() + "SourcePending=1\n");
        openLobby(init);
    } catch(const std::exception& error) {
        openWindow(MsgBox::create(error.what()));
    }
}

void CoopCampaignMenu::onResumeCampaign() {
    char directory[FILENAME_MAX];
    if(fnkdat("coop/", directory, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) < 0) {
        openWindow(MsgBox::create(_("The co-op progress directory could not be opened.")));
        return;
    }
    openWindow(LoadSaveWindow::create(false, _("Resume co-op progress"), directory, "ini"));
}

void CoopCampaignMenu::onChildWindowClose(Window* child) {
    auto* load = dynamic_cast<LoadSaveWindow*>(child);
    if(load && !load->getFilename().empty()) selectedProgress = load->getFilename();
}

void CoopCampaignMenu::update() {
    if(selectedProgress.empty()) return;
    const auto path = std::exchange(selectedProgress, {});
    try { resume(path); }
    catch(const std::exception& error) { openWindow(MsgBox::create(error.what())); }
}

void CoopCampaignMenu::resume(const std::string& path) {
    if(!ModManager::instance().isTornieFamilyContentActive()) throw std::runtime_error("Select a Tornie-family mod first");
    const auto session = coop::CoopCampaignSession::loadProgress(path);
    if(session.isComplete()) throw std::runtime_error("This co-op campaign is complete. Create a new campaign.");
    auto& mods = ModManager::instance();
    if(session.context().modName != mods.getContentBase(mods.getActiveModName()))
        throw std::runtime_error("First activate this co-op campaign's mod in the Mods menu");
    if(session.settingsBlob().empty()) throw std::runtime_error("Missing co-op mission settings");
    IMemoryStream stream(session.settingsBlob().data(), session.settingsBlob().size());
    GameInitSettings saved(stream);
    const auto savedContext = coop::readContext(saved.getFiledata());
    if(!savedContext || !coop::sameCampaignContext(session.context(), *savedContext))
        throw std::runtime_error("The saved mission settings do not match this campaign's progress");
    WorkshopGameContent::resolveMod(saved);
    auto init = coop::makeGameSettings(session, saved.getGameOptions(), settings.general.playerName + " — Co-op");
    init.setModRevision(saved.getModRevisionHash(), saved.getModRevisionVersion());
    if(!session.checkpointPath().empty()) {
        auto checkpoint = session.checkpointPath();
        if(!std::filesystem::is_regular_file(checkpoint)
           && std::filesystem::is_regular_file(checkpoint + ".bak")) checkpoint += ".bak";
        if(std::filesystem::is_regular_file(checkpoint)) {
            const auto bytes = readCompleteFile(checkpoint);
            IMemoryStream checkpointStream(bytes.data(), bytes.size());
            GameInitSettings::HouseInfoList houses;
            const auto setup = GameInitSettings::readSaveSetup(checkpointStream, houses);
            const auto context = coop::readContext(setup.getFiledata());
            if(!context || !coop::sameCampaignContext(session.context(), *context))
                throw std::runtime_error("The co-op checkpoint does not match this campaign's progress");
            init = coop::makeSavedGameSettings(checkpoint, settings.general.playerName + " — Co-op", mods.getActiveModName());
        }
    }
    openLobby(init);
}
