#include <Network/NetworkManager.h>
#include <Network/WorkshopGameContent.h>
#include <misc/OMemoryStream.h>
#include <fstream>
// Test-only entry point, linked against the real menu objects by run-menu-probe.py.
#include <Menu/HouseChoiceMenu.h>
#include <MapEditor/MapEditor.h>
#include <MapEditor/MapSettingsWindow.h>
#include <Menu/CustomGamePlayers.h>
#include <Menu/CustomGameMenu.h>
#include <Menu/CrossplayMenu.h>
#include <Menu/OptionsMenu.h>
#include <Menu/AboutMenu.h>
#include <Menu/HowToPlayMenu.h>
#include <Menu/ModMenu.h>
#include <Menu/ModEditorMenu.h>
#include <mod/Workshop.h>
#include <Menu/DisplayMenu.h>
#include <GUI/dune/GameOptionsWindow.h>
#include <GUI/dune/JoinProgressWindow.h>
#include <GUI/MsgBox.h>
#include <GUI/QstBox.h>
#include <Menu/PlaySetup.h>
#include <mod/ModManager.h>
#include <misc/FileSystem.h>
#include <FileClasses/LoadSavePNG.h>
#include <players/PlayerFactory.h>

namespace Workshop { std::unique_ptr<MenuBase> createMetaserverMenu(const std::string& kind, bool online); }
std::unique_ptr<MenuBase> createExtrasMenu(bool workshop);

void menuRequire(bool value, const char* message) {
    if(!value) { SDL_Log("MENU_PROBE_FAIL: %s", message); throw std::runtime_error(message); }
}
void menuCapture(Window& menu, const std::string& name, bool overlay = false) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    menu.draw();
    if(overlay) menu.drawOverlay();
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    auto surface = sdl2::surface_ptr(SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888));
    menuRequire(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,surface->pixels,surface->pitch)==0,"Capture failed");
    const auto path = std::string(std::getenv("MENU_PROBE_OUT"))+"/"+std::getenv("MENU_PROBE_WIDTH")+"-"+name+".bmp";
    menuRequire(SDL_SaveBMP(surface.get(),path.c_str())==0,"Screenshot write failed");
}
int runLiveMapCatalogueProbe() {
    settings.network.relayUseDevelopmentEndpoint=false;
    settings.general.playerName="Map catalogue tester";
    ModManager::instance().setActiveMod("dunecity");
    effectiveGameOptions=ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    CustomPlaySetup setup;setup.mods=ModManager::instance().listModChoices();setup.rules=effectiveGameOptions;
    CustomGameMenu browser(false,false,&setup);
    browser.onMapTypeChange(5);
    const auto deadline=SDL_GetTicks()+60000;
    while(browser.loadingMaps && static_cast<Sint32>(deadline-SDL_GetTicks())>0){browser.update();SDL_Delay(10);}
    menuRequire(!browser.loadingMaps && browser.mapEntries.size()>=127,"Live catalogue did not load seeded maps");
    browser.mapModFilter.setSelectedItem(3);browser.mapSizeFilter.setSelectedItem(4);browser.mapPlayersFilter.setSelectedItem(6);browser.rebuildMapList();
    int selected=-1;
    for(int i=0;i<browser.mapList.getNumEntries();++i)if(browser.mapEntries[browser.visibleMaps[i]].metadata.name=="Alkozeltser 4 Cities")selected=i;
    menuRequire(selected>=0,"DuneCity/256/6-player filters lost Alkozeltser 4 Cities");
    browser.mapList.setSelectedItem(selected);menuCapture(browser,"live-metaserver-maps");
    auto& entry=browser.mapEntries[browser.visibleMaps[selected]];
    Workshop::Client download;download.download(entry.revision.hash);
    const auto downloadDeadline=SDL_GetTicks()+600000;
    while(download.status()==Workshop::Client::Status::Busy && static_cast<Sint32>(downloadDeadline-SDL_GetTicks())>0){download.update();SDL_Delay(5);}
    SDL_Log("LIVE_MAP_DOWNLOAD: %s",download.message().c_str());
    menuRequire(download.status()==Workshop::Client::Status::Succeeded,"Live map/dependency download failed");
    menuRequire(browser.prepareSelectedMap(),"Downloaded map could not be selected");
    browser.onMapListSelectionChange(true);menuCapture(browser,"live-metaserver-preview");
    menuRequire(std::filesystem::path(browser.getSelectedMapPath()).filename()=="Alkozeltser 4 Cities.ini","Map download changed the name");
    browser.onNext();menuRequire(browser.retVal==MENU_SETUP_PLAYERS,"Metaserver selection did not reach player setup");
    auto path=setup.maps[setup.map];
    GameInitSettings init(getBasename(path,true),readCompleteFile(path),false,setup.rules);
    // A new custom game plays the mod that was selected here; the authored revision of the
    // downloaded bytes is reused only when it belongs to that very mod.
    const bool reused=WorkshopGameContent::applyMapRevisionForSelectedMod(path,init);
    SDL_Log("LIVE_MAP_REVISION: authored revision %s",reused?"reused":"recaptured under the selected mod");
    menuRequire(ModManager::instance().getContentBase(ModManager::instance().getActiveModName())=="dunecity",
                "Downloading a city map changed the selected mod");
    SDL_Log("MENU_PROBE_PASS: live catalogue, combined filters, verified map/mod download, stable name, preview and player setup");
    return MENU_QUIT_DEFAULT;
}

int runMenuProbe() {
    if(std::getenv("MENU_LIVE_MAPS"))return runLiveMapCatalogueProbe();
    menuRequire(getRendererWidth()==std::atoi(std::getenv("MENU_PROBE_WIDTH"))
                && getRendererHeight()==std::atoi(std::getenv("MENU_PROBE_HEIGHT")),
                "Probe did not render at the requested dimensions");
    settings.general.playerName = "Menu tester";
    settings.network.relayUseDevelopmentEndpoint = true;
    // Disable online discovery in this fixture; admission and transport have separate tests.
    settings.network.directDevelopmentEndpoint = "";
    const auto installedSkin = std::filesystem::path(ModManager::instance().getModPath("dunecity"))
        / "graphics_skins" / "Dune2" / "zones" / "dunecity_atreides_residential_zone" / "zone.ini";
    menuRequire(std::filesystem::is_regular_file(installedSkin),
                "Bundled DuneCity skin was not installed into the fresh profile");
    ModManager::instance().setActiveMod("vanilla");
    effectiveGameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    {
        auto& mods = ModManager::instance();
        // Upgrading a Dune2R profile previously captured by Workshop must not
        // retain its materialized copy of older Vanilla flight rules.
        {
            INIFile oldRules(mods.getActiveObjectDataPath());
            oldRules.setStringValue("Carryall", "MaxSpeed", "19.2");
            oldRules.setStringValue("Carryall", "TurnSpeed", "0.099");
            menuRequire(oldRules.saveChangesTo(mods.getModPath("Dune2R")+"/ObjectData.ini"),
                        "Cannot seed stale inherited Dune2R fixture");
            menuRequire(mods.dune2rNeedsReseed(), "Stale Dune2R flight rules were not detected");
            mods.initialize();
            menuRequire(!mods.dune2rNeedsReseed(), "Dune2R inherited rules did not refresh");
        }
        for(const auto& name : OnlineModPolicy::approvedMods()) {
            menuRequire(mods.setActiveMod(name), "Approved mod is not installed");
            if(name == "vanilla" || name == "dunecity" || name == "Dune2R") {
                // Exercise the installed mod path and real fixed-point loader:
                // Dune2R inherits Vanilla, while Dune City has a seeded copy.
                ObjectData units;
                units.loadFromINIFile(mods.getActiveObjectDataPath(), false);
                for(int house = 0; house < NUM_HOUSES; ++house) {
                    const double tilesPerSecond = units.data[Unit_Carryall][house].maxspeed.toDouble()
                        * (1000.0 / GAMESPEED_DEFAULT) / TILESIZE;
                    menuRequire(std::abs(tilesPerSecond - 15.0) < 0.0001,
                                "Bundled carryall cruise speed differs from Dynasty normal speed");
                    const double degreesPerSecond = units.data[Unit_Carryall][house].turnspeed.toDouble()
                        * 45 * (1000.0 / GAMESPEED_DEFAULT);
                    menuRequire(std::abs(degreesPerSecond - 253.125) < 0.0001,
                                "Bundled carryall turn rate differs from Dynasty normal speed");
                }
                SDL_Log("CARRYALL_SPEED_PROBE_PASS: %s = 15 tiles/s", name.c_str());
            }
            const auto revisions = Workshop::store().list("mod").size();
            const auto token = OnlineModPolicy::fingerprint();
            menuRequire(OnlineModPolicy::approvedName(token) == name, "Approved mod token cannot identify its installed mod");
            if(name == "dunecity") {
                // Cache stamps are runtime bookkeeping, never presentation or
                // gameplay content. Android and desktop must approve the same art.
                const auto marker = std::filesystem::path(mods.getModPath(name))
                    / "graphics_skins" / ".dune2r_graphics_skins_probe";
                { std::ofstream out(marker); out << "local extraction marker"; }
                menuRequire(OnlineModPolicy::fingerprint() == token,
                            "Android skin cache marker changed approved content identity");
                std::filesystem::remove(marker);
            }

            GameInitSettings campaign(HOUSE_ATREIDES, effectiveGameOptions, 1);
            campaign.enableCoop(true, "Approved campaign");
            // The community endpoint is disabled. Even publish/queue callers must
            // start approved campaigns without capturing or uploading a mod.
            WorkshopGameContent::pin(campaign, true, true);
            menuRequire(campaign.getModRevisionHash().empty() && campaign.getModName() == name,
                        "Approved campaign created a Workshop dependency");
            menuRequire(Workshop::store().list("mod").size() == revisions,
                        "Approved campaign copied its assets into Workshop");
            // A lobby may pin the bytes of a bundled mod while retaining its
            // installer name, rather than activating a ws-<hash> alias.
            const auto bundled = Workshop::saveMod(name);
            GameInitSettings pinned = campaign;
            pinned.setModRevision(bundled.hash, bundled.version);
            menuRequire(WorkshopGameContent::announcesRevision(name, pinned),
                        "Bundled lobby mod rejected under its installer name");
            menuRequire(WorkshopGameContent::announcesRevision("ws-"+bundled.hash, pinned),
                        "Exact workshop alias rejected");
            pinned.setModRevision(std::string(64,'0'), bundled.version);
            menuRequire(!WorkshopGameContent::announcesRevision(name, pinned),
                        "Installer name bypassed pinned content verification");
            menuRequire(!WorkshopGameContent::announcesRevision("unapproved-mod", pinned),
                        "Unapproved name bypassed pinned content verification");
            CrossplayMenu direct(campaign, true, {}, true, true);
            menuRequire(direct.startImmediately && direct.allowLateJoin, "Approved campaign lost direct start or hot joining");
            menuRequire(direct.activateGameContent(token, true), "Approved hot join requires the community service");
            auto mismatch = token;
            mismatch.back() = token.back() == '0' ? '1' : '0';
            menuRequire(!direct.activateGameContent(mismatch, true), "Approved join accepted changed rules");
        }
        menuRequire(mods.setActiveMod("dunecity"), "Cannot select Dune City");
        menuRequire(mods.getModInfo("dunecity").version == DUNECITY_MOD_VERSION
                    && mods.getModInfo("dunecity").author == "Stefan", "Mod identity is not independent");
        for(const auto* filename : {"QuantBot Config.ini", "GameOptions.ini", "mod.ini"}) {
            const auto path = std::filesystem::path(mods.getModPath("dunecity")) / filename;
            const auto bytes = readCompleteFile(path.string());
            {
                std::ofstream changed(path, std::ios::app);
                changed << (std::string(filename) == "mod.ini" ? "\n[Unapproved]\nEnabled = true\n" : "\nModified = true\n");
            }
            menuRequire(!OnlineModPolicy::approved(), "Modified official-name mod retained hot join approval");
            { std::ofstream restore(path, std::ios::binary); restore << bytes; }
            menuRequire(OnlineModPolicy::approved(), "Restored bundled mod was not approved");
        }
        {
            const auto bytes = readCompleteFile(installedSkin.string());
            menuRequire(!bytes.empty(), "Missing skin fixture");
            auto changed = bytes; changed[0] = changed[0] == '#' ? ';' : '#';
            { std::ofstream file(installedSkin, std::ios::binary); file << changed; }
            menuRequire(!OnlineModPolicy::approved(), "Same-size changed asset retained hot join approval");
            { std::ofstream file(installedSkin, std::ios::binary); file << bytes; }
            menuRequire(OnlineModPolicy::approved(), "Restored skin was not approved");
        }
        const auto extra = std::filesystem::path(mods.getModPath("dunecity")) / "extra-payload.ini";
        { std::ofstream file(extra); file << "changed"; }
        menuRequire(!OnlineModPolicy::approved(), "Extra mod content retained hot join approval");
        std::filesystem::remove(extra);
        menuRequire(OnlineModPolicy::approved(), "Removing extra content did not restore approval");
        const auto original = Workshop::saveMod("dunecity");
        menuRequire(Workshop::activateModRevision(original.hash), "Cannot simulate the previous build's mod selection");
        const auto cachedName = mods.getActiveModName();
        const auto choices = mods.listModChoices();
        menuRequire(std::none_of(choices.begin(), choices.end(), [&](const auto& mod) { return mod.name == cachedName; }),
                    "Official mod appears twice in the picker");
        const auto official = std::find_if(choices.begin(), choices.end(), [](const auto& mod) { return mod.name == "dunecity"; });
        menuRequire(official != choices.end() && official->matchesSelectionName(cachedName)
                    && official->selectionLabel() == "Dune City " + std::string(DUNECITY_MOD_VERSION),
                    "Official mod lost its independent version or cached selection alias");
        const auto registry = mods.listMods();
        menuRequire(std::any_of(registry.begin(), registry.end(), [&](const auto& mod) { return mod.name == cachedName; }),
                    "Picker filtering removed a pinned save dependency");
        {
            CustomGameMenu menu(false, false);
            const auto index = menu.modDropDown.getSelectedIndex();
            menuRequire(index >= 0 && menu.availableMods[index].name == "dunecity",
                        "Active cached Dune City copy fell back to a different mod");
        }
        OnlineModPolicy::restoreApprovedSelection();
        menuRequire(mods.getActiveModName() == "dunecity", "Upgrade left an approved mod on the sharing path");
        const std::string draft = "New campaign probe";
        // Probe profiles are reused; reset only this test-owned full asset copy.
        std::filesystem::remove_all(mods.getModPath(draft));
        menuRequire(mods.createMod(draft, "dunecity") && mods.setActiveMod(draft), "Cannot create new mod fixture");
        const auto authored = Workshop::saveMod(draft);
        menuRequire(Workshop::activateModRevision(authored.hash), "Cannot activate authored snapshot");
        OnlineModPolicy::restoreApprovedSelection();
        menuRequire(!OnlineModPolicy::approved(), "Authored mod inherited approval from its base");
        {
            HouseChoiceMenu menu(true);
            menuRequire(menu.hostCoopButton.getText() == _("Create Lobby"), "New campaign mod bypasses the pregame lobby");
            GameInitSettings campaign(HOUSE_ATREIDES, effectiveGameOptions, 1);
            campaign.enableCoop(true, "New campaign");
            CrossplayMenu direct(campaign, true, {}, true, true);
            menuRequire(!direct.startImmediately && !direct.allowLateJoin, "New mod permits direct start or hot joining");
            menuRequire(!direct.activateGameContent(authored.hash, true), "New mod accepts a running-game join");
            menuRequire(direct.activateGameContent(authored.hash, false), "New mod cannot join its pregame lobby");
            menuCapture(menu, "campaign-new-mod-lobby");
        }
        menuRequire(mods.setActiveMod("vanilla"), "Cannot restore vanilla fixture");
        effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
    }
    {
        HouseChoiceMenu menu(false);
        for(auto* dropdown : {&menu.modDropDown, &menu.enemyAIDropDown,
                              &menu.supportBotDropDown, &menu.startLevelDropDown}) {
            dropdown->onOpenListBoxButton();
            menuCapture(menu, dropdown == &menu.modDropDown ? "campaign-mod-open" : "campaign-options-open");
            const auto* geometry = menu.windowWidget.getWidgetDataFromWidget(dropdown);
            const int top = geometry->position.y + (dropdown->bListBoxAbove
                ? -dropdown->listBox.getSize().y : dropdown->getSize().y);
            menuRequire(top >= 0 && top + dropdown->listBox.getSize().y <= menu.getSize().y,
                        "Campaign dropdown extends outside the menu clip rectangle");
            dropdown->onOpenListBoxButton();
        }
        // A long list must shrink to whole rows, expose its scrollbar, and
        // still accept a click on the last item after wheel scrolling.
        auto& dropdown = menu.supportBotDropDown;
        dropdown.setNumVisibleEntries(40);
        for(int i = dropdown.getNumEntries(); i < 40; ++i)
            dropdown.addEntry("Extra partner " + std::to_string(i), i);
        dropdown.onOpenListBoxButton();
        menuCapture(menu, "campaign-scrolling-options");
        auto& list = dropdown.listBox;
        menuRequire(list.isScrollbarVisible(), "Constrained dropdown has no scrollbar");
        const auto* geometry = menu.windowWidget.getWidgetDataFromWidget(&dropdown);
        const int top = geometry->position.y + (dropdown.bListBoxAbove
            ? -list.getSize().y : dropdown.getSize().y);
        menuRequire(top >= 0 && top + list.getSize().y <= menu.getSize().y,
                    "Long campaign dropdown extends outside the menu");
        const int entryHeight = GUIStyle::getInstance().getListBoxEntryHeight();
        menuRequire((list.getSize().y - 2) % entryHeight == 0, "Dropdown clips a partial row");
        const int x = menu.getPosition().x + geometry->position.x + 10;
        const int y = menu.getPosition().y + top + 3;
        for(int i = 0; i < 40; ++i) menu.handleMouseWheel(x, y, false);
        const int lastRow = 39 - list.firstVisibleElement;
        menuRequire(lastRow >= 0 && (lastRow + 1) * entryHeight <= list.getSize().y - 2,
                    "Wheel scrolling cannot reach the last dropdown entry");
        menu.handleMouseLeft(x, y + lastRow * entryHeight, true);
        menuRequire(dropdown.getSelectedIndex() == 39 && !dropdown.bShowListBox,
                    "Last visible dropdown entry cannot be selected through the menu");
        dropdown.setSelectedItem(0);
    }
    {
        HouseChoiceMenu menu(false);
        menuRequire(HouseChoiceMenu::isPublicGame(),"Campaign should default to public");
        menu.supportBotDropDown.setSelectedItem(4);
        menuRequire(HouseChoiceMenu::getSupportBotIndex()==4,"Support setup fixture failed");
        auto& list=menu.supportBotDropDown.listBox;
        // A rapid second selection must notify the campaign setup even when
        // it falls within the list widget's double-click interval.
        list.scrollbar.setCurrentValue(3);
        menu.supportBotDropDown.onOpenListBoxButton();
        menuCapture(menu,"ai-partner-selection");
        list.lastClickTime=SDL_GetTicks();
        const int row=8-list.firstVisibleElement;
        const int rowY=1+row*GUIStyle::getInstance().getListBoxEntryHeight()+2;
        const int overlayY=rowY+(menu.supportBotDropDown.bListBoxAbove
            ? -list.getSize().y : menu.supportBotDropDown.getSize().y);
        menu.supportBotDropDown.handleMouseLeftOverlay(10,overlayY,true);
        menuRequire(menu.supportBotDropDown.getSelectedEntryIntData()==8,"Full AI selection fixture missed its row");
        menuRequire(HouseChoiceMenu::getSupportBotIndex()==8,"Full AI selection left campaign launch set to economy-only Support");
        menuRequire(!menu.supportBotDropDown.bShowListBox,"AI dropdown did not close after selection");
        for(int option=0;option<10;++option) {
            menu.supportBotDropDown.setSelectedItem(option);
            menuRequire(HouseChoiceMenu::getSupportBotIndex()==option,"AI partner choice mapping changed");
        }
        menu.supportBotDropDown.setSelectedItem(8);
        menu.enemyAIDropDown.setSelectedItem(2);
        menu.startLevelDropDown.setSelectedItem(6);
        // A stale cached callback result must never override the live widgets
        // when the player presses Start.
        HouseChoiceMenu::s_supportBotIndex=4;
        HouseChoiceMenu::s_enemyAIIndex=0;
        HouseChoiceMenu::s_startLevel=1;
        menu.hostCoopButton.handleMouseLeft(10,10,true);
        menu.hostCoopButton.handleMouseLeft(10,10,false);
        menuRequire(menu.isQuiting() && HouseChoiceMenu::getSupportBotIndex()==8
            && HouseChoiceMenu::getEnemyAIIndex()==2 && HouseChoiceMenu::getStartLevel()==7,
            "Start launched stale AI or mission choices");
    }
    {
        ListBox list;
        list.addEntry("First");list.addEntry("Second");
        list.resize(200,100);
        int selections=0,doubleClicks=0;
        list.setOnSelectionChange([&](bool){++selections;});
        list.setOnDoubleClick([&](){++doubleClicks;});
        list.lastClickTime=SDL_GetTicks();
        list.handleMouseLeft(10,3,true);
        menuRequire(selections==1 && doubleClicks==0,"First row click incorrectly counted as a double click");
        list.lastClickTime=SDL_GetTicks();
        list.handleMouseLeft(10,GUIStyle::getInstance().getListBoxEntryHeight()+3,true);
        menuRequire(selections==2 && doubleClicks==0,"Rapid different-row selection triggered double click");
        list.lastClickTime=SDL_GetTicks();
        list.handleMouseLeft(10,GUIStyle::getInstance().getListBoxEntryHeight()+3,true);
        menuRequire(selections==2 && doubleClicks==1,"Same-row double click stopped activating the row");
    }
    {
        MainMenu menu;
        menuRequire(menu.campaignButton.isVisible() && menu.customButton.isVisible() && menu.onlineButton.isVisible(), "Home play actions missing");
        menuRequire(!menu.displayButton.isVisible(),"Duplicate Display entry remains");
        menuRequire(menu.editorsButton.isVisible() && menu.editorsButton.getText()=="Editors"
            && menu.modesButton.getText()=="Extras", "Workshop and Extras destinations are not distinct");
        menuRequire(!menu.campaignButton.keyboardFocusVisible,"Home shows unsolicited keyboard focus");
        menuCapture(menu,"home");
        menu.canContinue=true; menu.refreshContextButtons();
        menuCapture(menu,"home-continue");
        menu.canContinue=false; menu.refreshContextButtons();
        SDL_Event tab{}; tab.type=SDL_KEYDOWN; tab.key.keysym.sym=SDLK_TAB;
        std::vector<TextButton*> tabOrder{&menu.campaignButton,&menu.customButton,&menu.loadButton,&menu.optionsButton,&menu.editorsButton,&menu.modesButton,&menu.quitButton};
#if defined(DUNECITY_DESKTOP_UPDATER)
        tabOrder.push_back(&menu.updateButton);
#endif
        tabOrder.push_back(&menu.onlineButton);
        for(auto* expected : tabOrder) {
            menu.handleInput(tab);
            menuRequire(menu.windowWidget.pActiveChildWidget==expected,"Home Tab order reaches a hidden destination or disagrees with screen order");
            menuRequire(expected->keyboardFocusVisible,"Keyboard navigation lost its visible focus");
        }
        menuCapture(menu,"home-keyboard");
        SDL_Event motion{}; motion.type=SDL_MOUSEMOTION; motion.motion.x=2; motion.motion.y=2;
        menu.handleInput(motion);
        menuRequire(!menu.campaignButton.keyboardFocusVisible,"Pointer movement left Campaign highlighted");
        menuRequire(!menu.campaignButton.bHover,"Pointer outside menu left Campaign hovered");
        menu.campaignButton.handleMouseMovement(5,5,false);
        menuRequire(menu.campaignButton.bHover,"Campaign no longer responds to hover");
        menu.campaignButton.handleMouseMovement(-1,-1,false);
        menuRequire(!menu.campaignButton.bHover,"Campaign hover stuck after pointer left");
        menuCapture(menu,"home-pointer");
#if defined(DUNECITY_DESKTOP_UPDATER)
        auto& updater = DesktopUpdater::instance();
        menu.bFirstLaunchPromptChecked = true;
        MainMenu::updatePromptOfferedThisLaunch = false;
        updater.manifest_.version = "9.0.0";
        updater.state_ = DesktopUpdater::State::Current;
        menu.update();
        menuRequire(!menu.pChildWindow && !menu.updatePromptOpen, "Up-to-date launch opened an update dialog");
        // An available update introduces itself; no visit to the update button.
        updater.state_ = DesktopUpdater::State::Available;
        menu.update();
        menuRequire(menu.updateButton.getText()=="Update available", "Update notice missing");
        auto* question = dynamic_cast<QstBox*>(menu.pChildWindow);
        menuRequire(question && menu.updatePromptOpen, "Update confirmation did not appear on its own");
        menuCapture(menu,"update-confirmation");
        question->onButton(QSTBOX_BUTTON2);
        menu.processChildWindowOpenCloses();
        menuRequire(!menu.updatePromptOpen && !updater.busy()
            && updater.state()==DesktopUpdater::State::Available, "Later started an installation");
        menu.update(); menu.update();
        menuRequire(!menu.pChildWindow, "Automatic update prompt returned after Later");
        {
            // A mod reinitialize builds a new main menu; "Later" still holds,
            // and the update button must still reach the confirmation.
            MainMenu recreated;
            recreated.bFirstLaunchPromptChecked = true;
            recreated.update(); recreated.update();
            menuRequire(!recreated.pChildWindow && !recreated.updatePromptOpen,
                "Later did not survive a recreated main menu");
            recreated.onUpdate();
            auto* manual = dynamic_cast<QstBox*>(recreated.pChildWindow);
            menuRequire(manual && recreated.updatePromptOpen, "Update button no longer reopens the confirmation");
            manual->onButton(QSTBOX_BUTTON2);
            recreated.processChildWindowOpenCloses();
            menuRequire(!recreated.updatePromptOpen && !updater.busy()
                && updater.state()==DesktopUpdater::State::Available, "Manual Later started an installation");
        }
        {
            // Availability arriving while another dialog is open waits for it
            // instead of closing it.
            MainMenu deferred;
            deferred.bFirstLaunchPromptChecked = true;
            MainMenu::updatePromptOfferedThisLaunch = false;
            updater.state_ = DesktopUpdater::State::Checking;
            deferred.openWindow(MsgBox::create("Busy"));
            deferred.update();
            updater.state_ = DesktopUpdater::State::Available;
            deferred.update();
            menuRequire(dynamic_cast<MsgBox*>(deferred.pChildWindow) && !deferred.updatePromptOpen
                && deferred.queuedChildWindows.empty(), "Update prompt displaced an open dialog");
            deferred.closeChildWindow();
            deferred.processChildWindowOpenCloses();
            deferred.update();
            auto* deferredPrompt = dynamic_cast<QstBox*>(deferred.pChildWindow);
            menuRequire(deferredPrompt && deferred.updatePromptOpen, "Deferred update prompt never appeared");
            deferredPrompt->onButton(QSTBOX_BUTTON2);
            deferred.processChildWindowOpenCloses();
            menuRequire(!updater.busy() && updater.state()==DesktopUpdater::State::Available,
                "Deferred prompt installed without confirmation");
        }
        updater.state_ = DesktopUpdater::State::Installing;
        auto* active = menu.windowWidget.pActiveChildWidget;
        menu.handleInput(tab);
        menuRequire(menu.windowWidget.pActiveChildWidget == active, "Installation allowed game-entry navigation");
        updater.state_ = DesktopUpdater::State::Current;
#endif

    }
    {
        HouseChoiceMenu menu;
        menuRequire(HouseChoiceMenu::isOnline(), "Campaign entry must default to online");
        menuRequire(menu.hostCoopButton.getText() == _("Start Campaign"), "Campaign offers a lobby instead of direct launch");
        menuRequire(HouseChoiceMenu::isPublicGame(), "Campaign should default to public");
        menuRequire(menu.visibilityDropDown.isVisible() && menu.supportBotDropDown.isEnabled(),
                    "Direct online campaign lost visibility or AI partner selection");
        menuCapture(menu,"campaign-default");
        menu.connectionDropDown.setSelectedItem(0);
        menuRequire(!HouseChoiceMenu::isOnline(), "Campaign cannot be switched offline");
        menuRequire(!menu.visibilityDropDown.isEnabled() && !menu.visibilityDropDown.isVisible() && menu.supportBotDropDown.isEnabled(),
                    "Offline campaign controls did not update");
        menu.onHouseButton(1);
        menuRequire(!menu.isQuiting(),"Selecting a house starts the game");
        HouseChoiceMenu::s_startLevel = 7;
        HouseChoiceMenu::s_singleMission = false;
        menu.populateLevels();
        menuRequire(menu.startLevelDropDown.getNumEntries()==9,"Campaign level range wrong");
        menuCapture(menu,"campaign-offline");
        HouseChoiceMenu::s_currentGameOptions.fogOfWar = !effectiveGameOptions.fogOfWar;
    }
    {
        HouseChoiceMenu menu(true,true,true);
        menuRequire(HouseChoiceMenu::getStartLevel()==7,"Retry lost campaign level");
        menuRequire(HouseChoiceMenu::getGameOptions().fogOfWar != effectiveGameOptions.fogOfWar,"Retry lost game rules");
        menuRequire(HouseChoiceMenu::isOnline() && menu.hostCoopButton.getText() == _("Create Lobby"),
                    "Explicit online campaign entry must retain its lobby");
        menuRequire(!menu.supportBotDropDown.isEnabled(),"Online campaign AI selector should defer to lobby");
        menuCapture(menu,"campaign-online");
        HouseChoiceMenu::s_singleMission = true;
        menu.populateLevels();
        menuRequire(menu.startLevelDropDown.getNumEntries()==22,"Single mission range wrong");
    }
    CustomPlaySetup setup;
    auto files = getFileNamesList(getDuneLegacyDataDir()+"/maps/multiplayer/","ini",true);
    auto dir = getDuneLegacyDataDir()+"/maps/multiplayer/";
    if(files.empty()) { dir = getDuneLegacyDataDir()+"/data/maps/multiplayer/"; files=getFileNamesList(dir,"ini",true); }
    menuRequire(!files.empty(),"No bundled custom maps");
    for(const auto& file : files) setup.maps.push_back(dir+file);
    setup.mods = ModManager::instance().listModChoices();
    for(size_t i=0;i<setup.mods.size();++i) if(setup.mods[i].name=="vanilla") setup.mod=i;
    setup.rules = effectiveGameOptions;
    for(bool online : {false, true}) {
        auto mapSetup = setup;
        mapSetup.online = online;
        CustomGameMenu browser(false, false, &mapSetup);
        menuRequire(browser.nextButton.getText() == _("Next"), "Map chooser still labels Next as Players");
        menuRequire(browser.mapList.getNumEntries() > 10, "Original scrolling map browser is missing");
        menuRequire(browser.getSelectedMapPath() == setup.maps[setup.map], "Map browser lost selected map");
        menuRequire(browser.mainVBox.getMinimumSize().x <= browser.mainVBox.getSize().x, "Map browser controls exceed menu width");
        menuRequire(browser.mainVBox.getMinimumSize().y <= browser.mainVBox.getSize().y, "Map browser controls exceed menu height");
        menuRequire(browser.allowJoinAfterStartCheckbox.isChecked(),"Late join must be enabled by default");
        menuRequire(browser.allowJoinAfterStartCheckbox.isVisible()==online,"Late join setting is shown for an offline game");
        menuCapture(browser, online ? "map-browser-online" : "map-browser-offline");
        browser.allowJoinAfterStartCheckbox.setChecked(false);
        browser.onNext();
        menuRequire(browser.retVal == MENU_SETUP_PLAYERS, "Map selection did not proceed to players");
        menuRequire(!mapSetup.allowJoinAfterStart,"Late join opt-out was lost");
        menuRequire(mapSetup.online == online && mapSetup.map == setup.map, "Map browser changed mode or map");
    }
    {
        CustomGameMenu browser(false,false,&setup);
        menuRequire(browser.mapModFilter.getNumEntries()==4 && browser.mapModFilter.getEntry(1)=="Vanilla","Map type choices retain legacy labels");
        browser.mapModFilter.setSelectedItem(1);browser.mapSizeFilter.setSelectedItem(4);browser.rebuildMapList();
        menuRequire(browser.mapList.getNumEntries()>0,"Old untagged large maps disappeared from Vanilla filter");
        menuCapture(browser,"vanilla-large-maps");
        browser.mapModFilter.setSelectedItem(0);browser.mapSizeFilter.setSelectedItem(0);
        browser.onMapTypeChange(5);browser.mapClient.cancel();browser.loadingMaps=false;
        browser.mapEntries.clear();
        CustomGameMenu::MapEntry city;city.metadata.name="Alkozeltser 4 Cities";
        city.metadata.mod="dunecity";city.metadata.width=city.metadata.height=256;city.metadata.players=6;city.metadata.version=3;
        CustomGameMenu::MapEntry legacy;legacy.metadata.name="Small dunes";
        legacy.metadata.mod="vanilla";legacy.metadata.width=legacy.metadata.height=64;legacy.metadata.players=2;legacy.metadata.version=1;
        browser.mapEntries={city,legacy};browser.rebuildMapList();
        menuRequire(browser.mapList.getNumEntries()==2,"Metaserver maps were not listed");
        menuRequire(browser.mapList.getEntry(0)=="Alkozeltser 4 Cities v3","Map name is missing its revision suffix");
        menuRequire(browser.mapList.getEntry(1)=="Small dunes v1","Map name is missing revision one");
        menuRequire(browser.mapPropertyVersion.getText()=="3","Map revision metadata missing");
        browser.mapModFilter.setSelectedItem(3);browser.mapSizeFilter.setSelectedItem(4);browser.mapPlayersFilter.setSelectedItem(6);browser.rebuildMapList();
        menuRequire(browser.mapList.getNumEntries()==1,"Combined map filters dropped a matching map");
        menuCapture(browser,"metaserver-maps");
        browser.mapPlayersFilter.setSelectedItem(2);browser.rebuildMapList();
        menuRequire(browser.mapList.getNumEntries()==0&&!browser.nextButton.isEnabled(),"Empty filter result can start a stale map");
    }
    {
        // "All Maps" reads four directories, so one map can arrive from several
        // of them: a metaserver download installed into the user profile, an
        // editor save of a bundled map, or a metadata-annotated copy left
        // beside its original. Those copies shared one display name and were
        // listed once each. Real files, real chooser, real INI parsing.
        namespace fs = std::filesystem;
        menuRequire(std::getenv("DUNECITY_USERDIR") != nullptr,"Duplicate map test needs an isolated profile");
        const auto userMaps = fs::path(std::getenv("DUNECITY_USERDIR"))/"maps/multiplayer";
        fs::create_directories(userMaps);
        const std::string original = readCompleteFile(dir + "6P - 256x256 - Alkozeltser 4.ini");
        const auto basicEnd = original.find('\n', original.find("[BASIC]"));
        menuRequire(basicEnd != std::string::npos,"Duplicate map fixture lost its [BASIC] section");
        auto named=[&](const std::string& keys) {
            return original.substr(0,basicEnd+1) + "Name=Menu probe duplicate\n" + keys + original.substr(basicEnd+1);
        };
        std::string distinct = named("");
        const auto row = distinct.find("\n000=");
        menuRequire(row != std::string::npos,"Duplicate map fixture lost its terrain rows");
        distinct[row+5] = distinct[row+5]=='~' ? '%' : '~';   // one changed tile: a different map
        const std::vector<std::pair<std::string,std::string>> fixtures = {
            {"menu-probe-a-copy.ini", named("")},                     // plain copy, scanned first
            {"menu-probe-b-annotated.ini", named("MapVersion=4\n")},  // same map, annotated beside it
            {"menu-probe-c-distinct.ini", distinct},                  // same name, other content
            {"6P - 256x256 - Alkozeltser 4.ini", original},           // user copy of an installed map
        };
        struct Cleanup {
            fs::path directory; std::vector<std::string> files;
            ~Cleanup(){ std::error_code e; for(const auto& f:files) fs::remove(directory/f,e); }
        } cleanup{userMaps,{}};
        for(const auto& fixture : fixtures) {
            std::ofstream out((userMaps/fixture.first).string(), std::ios::binary);
            out.write(fixture.second.data(), static_cast<std::streamsize>(fixture.second.size()));
            out.close();
            cleanup.files.push_back(fixture.first);
        }
        CustomGameMenu browser(false,false,&setup);
        browser.onMapTypeChange(4);
        int copies=0, mergedVersion=-1; std::string keptPath; bool distinctListed=false;
        for(size_t index : browser.visibleMaps) {
            const auto& entry=browser.mapEntries[index];
            if(entry.metadata.name!="Menu probe duplicate") continue;
            ++copies;
            if(fs::path(entry.path).filename()=="menu-probe-c-distinct.ini") distinctListed=true;
            else { keptPath=entry.path; mergedVersion=entry.metadata.version; }
        }
        menuRequire(copies==2,"Equivalent map copies are still listed once per file");
        menuRequire(fs::path(keptPath).filename()=="menu-probe-a-copy.ini",
                    "Deduplication did not keep the copy the catalogue scanned first");
        menuRequire(mergedVersion==4,"The surviving copy lost the newest revision of its group");
        menuRequire(distinctListed,"A different map sharing a name was hidden as a duplicate");
        int installedCopies=0; std::string installedPath;
        for(size_t index : browser.visibleMaps)
            if(browser.mapEntries[index].metadata.name=="6P - 256x256 - Alkozeltser 4")
                { ++installedCopies; installedPath=browser.mapEntries[index].path; }
        menuRequire(installedCopies==1,"A user-profile copy of an installed map is listed twice");
        menuRequire(installedPath==dir+"6P - 256x256 - Alkozeltser 4.ini",
                    "Deduplication moved an installed map to its user-profile copy");
        menuCapture(browser,"duplicate-map-copies");
    }
    {
        const auto file=setup.maps.front();
        ModManager::instance().setActiveMod("vanilla");
        GameInitSettings hosted(getBasename(file,true),readCompleteFile(file),"Map collection test",false,setup.rules);
        WorkshopGameContent::pin(hosted,false,true);
        menuRequire(!hosted.getMapRevisionHash().empty(),"Approved hosted map was not captured");
        menuRequire(std::filesystem::exists(Workshop::store().root()/"outbox"/hosted.getMapRevisionHash()),"Approved online map was not queued for collection");
        auto r=Workshop::store().get(hosted.getMapRevisionHash());
        auto installed=Workshop::installMap(r);
        menuRequire(std::filesystem::path(installed).stem().string()==r.name,"Downloaded map name gained a version suffix");
        INIFile pointer(installed+".workshop.ini");
        menuRequire(pointer.getIntValue("Workshop","Version",0)==static_cast<int>(r.version),"Downloaded map revision is not metadata");
        menuRequire(pointer.getStringValue("Workshop","Mod","")=="vanilla","Downloaded map has no mod flag");
    }
    {
        // The mod the player picks is what the game starts with. Choosing a map - a local
        // one or a downloaded one authored with another mod - never changes that choice,
        // and the choice survives Next, reopening the setup and the next launch.
        auto& mods = ModManager::instance();
        const auto rememberedBefore = settings.general.customGameMod;
        menuRequire(mods.setActiveMod("vanilla"),"Cannot start the mod choice fixture on vanilla");
        auto modSetup = setup;
        modSetup.players = ChangeEventList{};
        {
            CustomGameMenu browser(false,false,&modSetup);
            int city=-1;
            for(size_t i=0;i<browser.availableMods.size();++i)
                if(browser.availableMods[i].name=="dunecity") city=static_cast<int>(i);
            menuRequire(city>=0,"Dune City is not offered in the custom game mod picker");
            browser.modDropDown.setSelectedItem(city,true);
            menuRequire(mods.getActiveModName()=="dunecity" && mods.isCityModeActive(),
                        "Picking Dune City did not activate the city content");
            menuRequire(settings.general.customGameMod=="dunecity","Mod choice was not remembered");
            menuRequire(browser.prepareSelectedMap(),"Vanilla map could not be prepared under Dune City");
            menuRequire(mods.getActiveModName()=="dunecity","Selecting a Vanilla map reset the chosen mod");
            // Using the already selected mod also records it, even without a dropdown change.
            settings.general.customGameMod.clear();
            browser.onNext();
            menuRequire(settings.general.customGameMod=="dunecity","Next did not remember the mod being used");
            menuRequire(browser.retVal==MENU_SETUP_PLAYERS,"Mod choice fixture did not reach player setup");
            menuRequire(modSetup.mods[modSetup.mod].name=="dunecity","Player setup received a different mod than the one chosen");
            menuRequire(mods.getActiveModName()=="dunecity" && mods.isCityModeActive(),"Next left the chosen mod");
        }
        // The downloaded fixture: the same map bytes, authored and pinned under Vanilla.
        Workshop::Revision authored;
        std::string downloaded;
        {
            menuRequire(mods.setActiveMod("vanilla"),"Cannot author the Vanilla map fixture");
            GameInitSettings hosted(getBasename(setup.maps.front(),true),readCompleteFile(setup.maps.front()),
                                    "Mod choice fixture",false,setup.rules);
            WorkshopGameContent::pin(hosted,false,false);
            authored = Workshop::store().get(hosted.getMapRevisionHash());
            downloaded = Workshop::installMap(authored);
            menuRequire(existsFile(downloaded+".workshop.ini") && !authored.modHash.empty(),
                        "Downloaded map fixture has no pinned mod");
            menuRequire(mods.setActiveMod("dunecity"),"Cannot reselect Dune City");
        }
        for(const auto& file : {setup.maps.front(), downloaded}) {
            GameInitSettings started(getBasename(file,true),readCompleteFile(file),false,setup.rules);
            menuRequire(!WorkshopGameContent::applyMapRevisionForSelectedMod(file,started),
                        "A map authored with Vanilla overruled the chosen mod");
            menuRequire(mods.getActiveModName()=="dunecity","Preparing the map switched the active mod");
            WorkshopGameContent::pin(started,false,false);
            menuRequire(started.getModName()=="dunecity","The started game did not keep the chosen mod");
            menuRequire(!started.getMapRevisionHash().empty() && started.getMapRevisionHash()!=authored.hash,
                        "The same map bytes under another mod did not become a new revision");
            menuRequire(Workshop::store().get(started.getMapRevisionHash()).modHash==Workshop::saveMod("dunecity").hash,
                        "The captured map revision is not tied to the chosen mod");
            menuRequire(Workshop::store().get(authored.hash).modHash==authored.modHash,
                        "Capturing a variant changed the authored revision");
            WorkshopGameContent::resolveMod(started);
            menuRequire(mods.getActiveModName()=="dunecity","Resolving the started game's content left Dune City");
        }
        {
            // Back and a reopened setup keep the choice; so does the saved configuration.
            auto reopened = setup;
            reopened.mods = mods.listModChoices();
            for(size_t i=0;i<reopened.mods.size();++i)
                if(reopened.mods[i].matchesSelectionName(settings.general.customGameMod)) reopened.mod=static_cast<int>(i);
            CustomGameMenu browser(false,false,&reopened);
            const auto index=browser.modDropDown.getSelectedIndex();
            menuRequire(index>=0 && browser.availableMods[index].name=="dunecity","Reopening custom setup lost the chosen mod");
            INIFile config(getConfigFilepath());
            menuRequire(config.getStringValue("General","Custom Game Mod","")=="dunecity",
                        "The custom game mod choice was not saved for the next launch");
        }
        // A mod activated automatically - by a save, a join or a campaign - is not a choice.
        menuRequire(mods.setActiveMod("vanilla"),"Cannot restore the vanilla fixture");
        menuRequire(settings.general.customGameMod=="dunecity","Automatic mod activation overwrote the remembered choice");
        // Reproduce the live Vanilla game carrying cached DuneCity turret rules.
        settings.general.customGameMod="vanilla";
        effectiveGameOptions.rocketTurretsNeedPower=true;
        const auto vanillaRules=initialCustomGameRules();
        menuRequire(!vanillaRules.rocketTurretsNeedPower && !effectiveGameOptions.rocketTurretsNeedPower,
                    "A new Vanilla game inherited stale powered-turret rules");
        auto rulesSetup=setup;
        rulesSetup.rules=vanillaRules;
        for(size_t i=0;i<rulesSetup.mods.size();++i)
            if(rulesSetup.mods[i].name=="vanilla")rulesSetup.mod=static_cast<int>(i);
        {
            CustomGameMenu browser(false,false,&rulesSetup);
            browser.onNext();
            menuRequire(!rulesSetup.rules.rocketTurretsNeedPower,"Next restored stale turret rules");
        }
        // An explicit override belongs to this setup and survives Back/Next.
        rulesSetup.rules.rocketTurretsNeedPower=true;
        {
            CustomGameMenu browser(false,false,&rulesSetup);
            browser.onNext();
            menuRequire(rulesSetup.rules.rocketTurretsNeedPower,"Back/Next erased an explicit turret-rule override");
        }
        settings.general.customGameMod = rememberedBefore;
    }
    {
        // Carryall drops are a Dune City default, and only a Dune City default. Everything here
        // reads the real resolution chain: the mod's own GameOptions.ini, the player's per-mod
        // overrides in the configuration file, and the global section vanilla uses.
        ModManager& mods = ModManager::instance();
        const auto savedBase = settings.gameOptions;
        const auto savedEffective = effectiveGameOptions;
        const auto savedRules = setup.rules;
        const std::string activeBefore = mods.getActiveModName();

        auto rulesFor = [&](const char* mod) {
            menuRequire(mods.setActiveMod(mod), "Cannot activate the carryall-default fixture mod");
            effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
            return effectiveGameOptions;
        };

        // A fresh profile has no per-mod overrides, so each mod's own file decides.
        menuRequire(rulesFor("dunecity").manualCarryallDrops,
                    "A fresh Dune City game does not enable carryall drops");
        menuRequire(!rulesFor("vanilla").manualCarryallDrops,
                    "Vanilla inherited the Dune City carryall default");
        if(mods.getModInfo("Dune2R").name == "Dune2R") {
            menuRequire(!rulesFor("Dune2R").manualCarryallDrops,
                        "Dune2R inherited the Dune City carryall default");
        }
        menuRequire(!settings.gameOptions.manualCarryallDrops,
                    "Reading the effective rules changed the global defaults");

        // An explicit choice for this mod sits on top of the mod's file and is never reseeded away.
        {
            menuRequire(mods.setActiveMod("dunecity"), "Cannot activate Dune City");
            INIFile config(getConfigFilepath());
            config.setBoolValue(userGameOptionsSection(), "Manual Carryall Drops", false);
            menuRequire(config.saveChangesTo(getConfigFilepath()), "Could not store an explicit choice");
            effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
            menuRequire(!effectiveGameOptions.manualCarryallDrops,
                        "An explicit Dune City 'off' was overridden by the mod default");
        }

        // Saving defaults while a mod is active must not export that mod's rules to vanilla.
        {
            menuRequire(mods.setActiveMod("dunecity"), "Cannot activate Dune City");
            effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
            auto chosen = effectiveGameOptions;
            chosen.manualCarryallDrops = true;
            chosen.sandwormsRespawn = !chosen.sandwormsRespawn;  // A second, unrelated change.
            saveGameOptionsAsDefaults(chosen);
            menuRequire(effectiveGameOptions.manualCarryallDrops
                        && effectiveGameOptions.sandwormsRespawn == chosen.sandwormsRespawn,
                        "Saving Dune City defaults did not take effect for Dune City");
            menuRequire(!settings.gameOptions.manualCarryallDrops,
                        "Saving Dune City defaults rewrote the global game options in memory");
            INIFile config(getConfigFilepath());
            menuRequire(!config.getBoolValue("Game Options", "Manual Carryall Drops", false),
                        "Saving Dune City defaults wrote the carryall default into the global section");
            menuRequire(config.getBoolValue(userGameOptionsSection(), "Manual Carryall Drops", false),
                        "The Dune City choice was not stored in its own section");
            menuRequire(!rulesFor("vanilla").manualCarryallDrops,
                        "A Dune City options save leaked into Vanilla play");
        }

        // Vanilla still owns the global section.
        {
            menuRequire(mods.setActiveMod("vanilla"), "Cannot activate vanilla");
            menuRequire(userGameOptionsSection().empty(), "Vanilla gained a per-mod section");
            auto chosen = mods.loadEffectiveGameOptions(settings.gameOptions);
            chosen.manualCarryallDrops = true;
            saveGameOptionsAsDefaults(chosen);
            menuRequire(settings.gameOptions.manualCarryallDrops,
                        "A Vanilla options save no longer updates the global defaults");
            INIFile config(getConfigFilepath());
            menuRequire(config.getBoolValue("Game Options", "Manual Carryall Drops", false),
                        "A Vanilla options save no longer writes the global section");
        }

        // Nothing above touched an existing setup's match rules.
        menuRequire(setup.rules == savedRules, "Option default checks rewrote the saved match rules");
        SDL_Log("MENU_PROBE_CARRYALL_DEFAULTS: dunecity=1 vanilla=0 explicit_off=1 no_leak=1");

        // Put the profile back the way the rest of the probe expects it: the global section and
        // the in-memory defaults restored, and the Dune City section left agreeing with the mod.
        menuRequire(mods.setActiveMod("vanilla"), "Cannot activate vanilla");
        saveGameOptionsAsDefaults(savedBase);
        {
            INIFile config(getConfigFilepath());
            config.setBoolValue("Game Options dunecity", "Manual Carryall Drops", true);
            menuRequire(config.saveChangesTo(getConfigFilepath()), "Could not restore the profile");
        }
        settings.gameOptions = savedBase;
        menuRequire(mods.setActiveMod(activeBefore), "Cannot restore the probe's active mod");
        effectiveGameOptions = savedEffective;
        setup.rules = savedRules;
    }
    const auto map = setup.maps.front();
    GameInitSettings init(getBasename(map,true),readCompleteFile(map),false,setup.rules);
    {
        // This shipped map has Player1, Player2, Player3 and Player5. The
        // fourth visible row still needs a real team before a match starts.
        const auto sparse = dir + "4P - 128x128 - Ergsun-Odenkirk.ini";
        auto sparseSetup = setup;
        sparseSetup.online = true;
        sparseSetup.sharedHouse = true;
        sparseSetup.players = ChangeEventList{};
        GameInitSettings sparseInit(getBasename(sparse,true),readCompleteFile(sparse),true,setup.rules);
        CustomGamePlayers menu(sparseInit,true,false,&sparseSetup);
        menuRequire(menu.numHouses == 4,"Sparse four-player map fixture changed");
        for(int slot=0;slot<menu.numHouses;++slot)
            menuRequire(menu.houseInfo[slot].teamDropDown.getSelectedEntryIntData()==slot+1,
                "Sparse PlayerN sections left an unassigned team that becomes 255 in a checkpoint");
        menuCapture(menu,"custom-sparse-teams");
    }
    {
        GameInitSettings coop(HOUSE_ATREIDES, setup.rules, 1);
        coop.enableCoop(true, "Keyboard co-op");
        auto scenario = pFileManager->openCampaignFile(coop.getFilename());
        std::string scenarioData(static_cast<size_t>(SDL_RWsize(scenario.get())), '\0');
        SDL_RWread(scenario.get(), scenarioData.data(), 1, scenarioData.size());
        coop.setScenarioData(scenarioData);
        {
            CrossplayMenu direct(coop, true, {}, true, true);
            menuRequire(direct.startImmediately && direct.allowLateJoin, "Direct campaign lost autostart or late joining");
            CrossplayMenu online(coop, true);
            menuRequire(!online.startImmediately, "Online Create Game bypasses the pregame lobby");
        }
        {
            GameInitSettings withPartner = coop;
            GameInitSettings::HouseInfo house(HOUSE_ATREIDES, 1);
            house.addPlayerInfo({settings.general.playerName, HUMANPLAYERCLASS});
            house.addPlayerInfo({"AI partner", "qBotSupportHard"});
            withPartner.addHouseInfo(house);
            CustomGamePlayers direct(withPartner, true, false, nullptr, nullptr, true);
            menuRequire(direct.automaticStartPending && direct.startImmediately,
                        "Direct campaign waits for a second Start click");
            menuRequire(direct.launchTitle.getParent() == &direct.launchWidget,
                        "Direct campaign displays the editable lobby");
            direct.addAllPlayersToGameInitSettings();
            bool partnerKept = false;
            for(const auto& h : direct.gameInitSettings.getHouseInfoList())
                for(const auto& p : h.playerInfoList) partnerKept |= p.playerClass == "qBotSupportHard";
            menuRequire(partnerKept, "Direct online campaign discarded the chosen AI partner");
            menuCapture(direct, "campaign-auto-start");
        }
        CustomGamePlayers menu(coop,true);
        menuRequire(!menu.automaticStartPending, "Pregame lobby starts without the host clicking Start");
        SDL_Event tab{}; tab.type=SDL_KEYDOWN; tab.key.keysym.sym=SDLK_TAB;
        // The host can start alone, with the second controller still open for a later join.
        menu.update();
        menuRequire(menu.nextButton.isEnabled(), "Campaign requires a partner before starting");
        menu.addAllPlayersToGameInitSettings();
        for(const auto& house : menu.gameInitSettings.getHouseInfoList()) {
            if(house.houseID == HOUSE_ATREIDES)
                menuRequire(house.playerInfoList.size() == 1, "Solo campaign added a phantom partner");
        }
        menuCapture(menu,"campaign-solo-lobby");
        menu.setPlayer2Slot("Keyboard partner",1);
        menu.update();
        bool reachedStart=false;
        for(int i=0;i<40;++i) { menu.handleInput(tab); if(menu.nextButton.isActive()) { SDL_Log("MENU_PROBE: campaign Start reached after %d Tabs",i+1); reachedStart=true; break; } }
        menuRequire(reachedStart,"Campaign lobby keyboard focus cannot reach Start after a partner joins");
        menuCapture(menu,"campaign-lobby");
    }
    ChangeEventList original;
    {
        CustomGamePlayers menu(init,true,false,&setup);
        original=menu.getChangeEventList();
        menuRequire(menu.numHouses>=2,"Fixture needs two houses");
        menu.update();
        menuRequire(menu.mainVBox.getMinimumSize().x<=menu.mainVBox.getSize().x,"Custom controls exceed menu width");
        menuCapture(menu,"custom-offline");
        menu.onCancel();
        menuRequire(menu.retVal == MENU_SETUP_MAP,"Back did not return to map browser");
        menuRequire(!setup.players.changeEventList.empty(),"Custom setup lost roster");
    }
    {
        auto roundTrip = setup;
        CustomGameMenu browser(false, false, &roundTrip);
        browser.onNext();
        menuRequire(roundTrip.players.changeEventList.size() == original.changeEventList.size(), "Returning through map browser lost the roster");
        menuRequire(roundTrip.map == setup.map, "Returning through map browser changed map");
    }
    setup.online=true;
    {
        CustomGamePlayers menu(init,true,false,&setup);
        auto restored=menu.getChangeEventList();
        menuRequire(restored.changeEventList.size()==original.changeEventList.size(),"Roster shape changed across online toggle");
        auto restoredEvent=restored.changeEventList.begin();
        for(const auto& a : original.changeEventList) {
            const auto& b=*restoredEvent++;
            menuRequire(a.eventType==b.eventType && a.slot==b.slot && a.newValue==b.newValue && a.newStringValue==b.newStringValue,"Online toggle changed player choices");
        }
        menu.update();
        menuCapture(menu,"custom-online");
        menu.onNext();
        menuRequire(menu.retVal==MENU_SETUP_HOST,"Create Lobby did not return prepared setup");
    }
    {
        setup.sharedHouse=true;
        setup.players=ChangeEventList{};
        GameInitSettings sharedInit(getBasename(map,true),readCompleteFile(map),true,setup.rules);
        CustomGamePlayers menu(sharedInit,true,false,&setup);
        menuRequire(menu.mainVBox.getMinimumSize().x<=menu.mainVBox.getSize().x,"Shared house controls exceed menu width");
        menu.update(); menuCapture(menu,"custom-shared");
    }
    for(const auto& candidate : setup.maps) {
        if(getBasename(candidate).find("6P - 64x64") == std::string::npos) continue;
        for(bool shared : {false, true}) {
            auto sixSetup=setup;
            sixSetup.online=shared;
            sixSetup.sharedHouse=shared;
            sixSetup.map=static_cast<int>(std::distance(setup.maps.begin(), std::find(setup.maps.begin(), setup.maps.end(), candidate)));
            sixSetup.players=ChangeEventList{};
            GameInitSettings sixInit(getBasename(candidate,true),readCompleteFile(candidate),shared,setup.rules);
            CustomGamePlayers menu(sixInit,true,false,&sixSetup);
            menuRequire(menu.numHouses==6,"Six-house fixture not loaded");
            menuRequire(menu.mainVBox.getMinimumSize().y<=menu.mainVBox.getSize().y,"Six-house controls exceed menu height");
            menu.update();
            menuCapture(menu,shared ? "custom-six-houses" : "custom-six-offline");
        }
        break;
    }
    {
        menuRequire(ModManager::instance().setActiveMod("Tornie"),"Bonus-color fixture mod unavailable");
        GameInitSettings modInit(getBasename(map,true),readCompleteFile(map),false,setup.rules);
        CustomGamePlayers menu(modInit,true);
        menuRequire(menu.mainVBox.getMinimumSize().x<=menu.mainVBox.getSize().x,"Bonus color controls exceed menu width");
        auto& row=menu.houseInfo[0];
        menuRequire(row.bonusColorCheckbox.isVisible() && row.bonusColorCheckbox.isEnabled(),"Classic bonus color selector is unavailable");
        row.bonusColorCheckbox.setChecked(true);
        menu.onBonusColorCheckbox(0);
        menuRequire(row.colorDropDown.getNumEntries()>1,"Bonus palette has no colors");
        menuCapture(menu,"custom-bonus-colors");
        ModManager::instance().setActiveMod("vanilla");
    }
    for(const auto& mod : {std::string("vanilla"),std::string("dunecity")}) {
        menuRequire(ModManager::instance().setActiveMod(mod), "City status fixture unavailable");
        GameInitSettings skinInit(getBasename(map,true),readCompleteFile(map),false,setup.rules);
        CustomGamePlayers menu(skinInit,true);
        menuRequire(menu.mainVBox.getMinimumSize().x<=menu.mainVBox.getSize().x,"Skin controls exceed menu width");
        if(mod=="dunecity") {
            menuRequire(menu.duneCitySkinControls,"Dune City lobby skin selector missing");
            menu.houseInfo[0].graphicsSkinDropDown.setSelectedItem(1);
            menu.addAllPlayersToGameInitSettings();
            menuRequire(menu.gameInitSettings.getHouseInfoList().front().graphicsSkin==GameInitSettings::GraphicsSkin::Dune2,
                "Lobby skin selection is not retained in game settings");
        }
        menuRequire(menu.mapPropertyCity.getText()==(mod=="dunecity" ? _("On") : _("Off")), "Lobby city status disagrees with mod");
        menuCapture(menu,"city-status-"+mod);
    }
    ModManager::instance().setActiveMod("vanilla");
    setup.players=original;
    {
        CrossplayMenu menu;
        const auto settingsName = settings.general.playerName;
        settings.general.playerName = "";
        menu.onHostCustomGame();
        menuRequire(menu.hasChildWindow(),"Custom hosting bypasses the settings player name");
        menu.closeChildWindow(); menu.processChildWindowOpenCloses();
        menu.onHostCampaignCoop();
        menuRequire(menu.hasChildWindow(),"Campaign hosting bypasses the settings player name");
        menu.closeChildWindow(); menu.processChildWindowOpenCloses();
        settings.general.playerName = settingsName;
        menuRequire(menu.playerNameValue.getText()==settingsName,"Play Online does not display the settings name");
        menuRequire(CustomPlaySetup().publicGame && menu.visibilityChoice.getSelectedIndex()==1,"New games should default to public");
        menuRequire(menu.chatHistory.getParent()==&menu.windowWidget && menu.publicGameList.getParent()==&menu.windowWidget,
            "Chat and game list must share the first screen");
        menuRequire(menu.chatHistory.getSize().x>=220 && menu.publicGameList.getSize().x>=350,
            "Side-by-side lobby panes are too narrow");
        menu.stage=CrossplayMenu::Stage::Choosing;
        PublicRelayGame coop, custom;
        coop.mode="coop"; coop.hostName="Campaign host"; coop.roomCode="AAAA-BBBB-CCCC"; coop.players=1; coop.maxPeers=2;
        custom.mode="custom"; custom.hostName="Custom host"; custom.roomCode="DDDD-EEEE-FFFF"; custom.players=2; custom.maxPeers=4;
        coop.modName="vanilla"; custom.modName="dunecity";
        coop.mapName="Atreides mission 4"; custom.mapName="Habbanya-Autumn";
        custom.running=true; custom.allowLateJoin=true; custom.elapsedSeconds=620;
        menuRequire(menu.modFilter.getSelectedEntryIntData()==-1,"All mods must be the default");
        menu.allPublicGames={coop,custom}; menu.refreshDirectory();
        menuRequire(menu.publicGames.size()==2,"All mods hides another mod");
        for(int i=0;i<static_cast<int>(menu.availableMods.size());++i) {
            if(menu.availableMods[i].name!="dunecity") continue;
            menu.modFilter.setSelectedItem(i+1); menu.refreshDirectory();
            menuRequire(menu.publicGames.size()==1 && menu.publicGames[0].modName=="dunecity",
                "Specific mod filter includes other mods");
            menuRequire(ModManager::instance().getActiveModName()=="vanilla","Filtering changes active mod");
        }
        menu.modFilter.setSelectedItem(0); menu.refreshDirectory();
        menu.modeFilter.setSelectedItem(1); menu.refreshDirectory();
        menuRequire(menu.publicGames.size()==1 && menu.publicGames[0].mode=="coop","Campaign filter includes custom games");
        menu.modeFilter.setSelectedItem(2); menu.refreshDirectory();
        menuRequire(menu.publicGames.size()==1 && menu.publicGames[0].mode=="custom","Custom filter includes campaign games");
        menu.modeFilter.setSelectedItem(0); menu.refreshDirectory();
        menuCapture(menu,"join");
        menu.directory.cancel(); menu.directoryPending=false;
        // Use the installed current mod; these are local, deterministic UI checks.
        menu.publicGames[1].modName="vanilla";
        menu.publicGameList.setSelectedItem(1);
        menu.publicGames[1].contentHash=std::string(32, 'a');
        menu.joinPublicGame();
        menuRequire(menu.stage==CrossplayMenu::Stage::Choosing
            && menu.statusText.find("New mods cannot be joined")!=std::string::npos,
            "Public join accepted an obsolete partial-content fingerprint");
        menu.publicGames[1].contentHash=OnlineModPolicy::fingerprint();
        settings.network.directDevelopmentEndpoint="http://127.0.0.1:1";
        menu.joinPublicGame();
        menu.processChildWindowOpenCloses();
        menuRequire(menu.pChildWindow==nullptr && menu.stage==CrossplayMenu::Stage::Requesting,
            "Running join should request admission without a mode prompt");
        menuRequire(menu.joiningRunning && menu.joiningAsSpectator,
            "Running join must enter as a passive spectator");
        menu.admission.cancel();
        menu.stage=CrossplayMenu::Stage::Choosing;
        settings.network.directDevelopmentEndpoint="";

        menu.joiningRunning=true;
        menu.onReceiveGameInfo(init,ChangeEventList());
        menuRequire(!menu.pendingGameInfo,"Original lobby setup hijacks checkpoint admission");
        menu.joiningRunning=false;
        menu.onReceiveGameInfo(init,ChangeEventList());
        menuRequire(menu.pendingGameInfo!=nullptr,"Waiting-game setup no longer opens its lobby");
        menu.pendingGameInfo.reset();

        for(bool hotJoin : {false,true}) {
            menu.stage=CrossplayMenu::Stage::Requesting;
            menu.joiningRunning=hotJoin;
            menu.admission.status_=RoomAdmissionClient::Status::Failed;
            menu.admission.response_.errorCode="version_mismatch";
            menu.admission.errorMessage_="Version mismatch. Host: 1.0.726. Yours: 1.0.727. Both players must use the same game version. Update or switch versions, then try again.";
            menu.update();
            menuRequire(menu.hasChildWindow(),"Version mismatch did not open an acknowledged prompt");
            menuRequire(menu.pChildWindow->getSize().x<=getRendererWidth() && menu.pChildWindow->getSize().y<=getRendererHeight(),
                "Version prompt exceeds screen bounds");
            auto* prompt=dynamic_cast<MsgBox*>(menu.pChildWindow);
            menuRequire(prompt!=nullptr && prompt->getText().find("1.0.726")!=std::string::npos
                && prompt->getText().find("1.0.727")!=std::string::npos,"Version prompt omits one of the versions");
            menuRequire(menu.stage==CrossplayMenu::Stage::Choosing && menu.joinTicket.empty(),
                "Version mismatch left an active join request");
            menuCapture(menu,hotJoin ? "hot-join-version-mismatch" : "join-version-mismatch");
            menu.closeChildWindow(); menu.processChildWindowOpenCloses();
            menu.update();
            menuRequire(!menu.hasChildWindow(),"Version prompt reopened without another join attempt");
        }
        menu.joiningRunning=true;
        menu.stage=CrossplayMenu::Stage::ClientWaiting;
        pNetworkManager=std::make_unique<NetworkManager>(NetworkManager::Transport::DirectP2P);
        auto& progressNetwork=*pNetworkManager;
        progressNetwork.playerName=settingsName;
        progressNetwork.spectators.insert(settingsName);
        progressNetwork.joinStage=NetworkManager::JoinStage::Receiving;
        progressNetwork.observerTotal=128*1024;
        progressNetwork.observerBytes.assign(64*1024,'x');
        menu.update();
        menuRequire(menu.joinProgress.isVisible() && !menu.statusLabel.isVisible(),"Join progress is not visible");
        menuRequire(menu.joinProgress.getProgress()==50 && menu.joinProgress.getText().find("64 / 128 KiB")!=std::string::npos,
            "Join progress does not describe received bytes");
        menuCapture(menu,"hot-join-download-progress");
        progressNetwork.spectators.clear();
        progressNetwork.joinName="Codex Web Guest";
        progressNetwork.joinTotal=128*1024;
        progressNetwork.joinBytes.assign(64*1024,'x');
        {
            JoinProgressWindow progress;
            menuCapture(progress,"named-player-joining");
            progressNetwork.joinName=std::string(64,'W');
            progress.refresh();
            menuCapture(progress,"named-player-joining-long-name");
        }
        // The name must survive checkpoint handoff, but a completion notice
        // must wait until the replacement simulation has actually started.
        progressNetwork.joinName="Codex Web Guest";
        progressNetwork.joinStage=NetworkManager::JoinStage::Ready;
        progressNetwork.joinSnapshot=std::make_unique<GameInitSettings>();
        progressNetwork.joinSpectators.clear();
        menuRequire(progressNetwork.takeLateJoin()!=nullptr,"Checkpoint handoff failed");
        menuRequire(progressNetwork.lateJoinPlayerName()=="Codex Web Guest"
            && progressNetwork.recentJoinNotice().empty(),"Join announced before the new simulation started");
        progressNetwork.beginSimulation(1);
        menuRequire(progressNetwork.recentJoinNotice()=="Codex Web Guest joined to play"
            && progressNetwork.lateJoinPlayerName().empty(),"Completed join lost its player name");
        progressNetwork.joinNoticeTime=SDL_GetTicks()-12000;
        menuRequire(progressNetwork.recentJoinNotice().empty(),"Completed join banner did not expire");
        menu.teardownSession("Test transfer ended. Please try joining again.");
        menuRequire(!menu.joinProgress.isVisible() && menu.statusLabel.isVisible() && !menu.joiningRunning,
            "Join failure did not restore usable lobby controls");
        menuRequire(menu.statusLabel.getText().find("Please try")!=std::string::npos,"Join failure reason disappeared");
        menu.joiningRunning=false;
        menu.chat.cancel(); menu.chatSession.clear(); menu.chatPending=false;
        menu.nextChatPoll=0;
        menu.update();
        menuRequire(menu.chatAction==AdmissionOperation::ChatEnter,
            "Chat does not enter automatically from the first screen");
        menuRequire(!menu.hasChildWindow(),"Automatic chat entry opened a name confirmation");
        menu.chat.cancel();
        menu.chatPending=true;
        menu.chatAction=AdmissionOperation::ChatPoll;
        menu.chatSession="expired-session";
        menu.chat.status_=RoomAdmissionClient::Status::Failed;
        menu.chat.response_.errorCode="session_expired";
        menu.updateLobbyChat();
        menuRequire(menu.chatSession.empty() && !menu.chatPending,
            "Expired chat session was not released");
        menu.nextChatPoll=0;
        menu.update();
        menuRequire(menu.chatAction==AdmissionOperation::ChatEnter,
            "Expired chat requires manual name confirmation");
        menu.chat.cancel(); menu.chatPending=true; menu.chatAction=AdmissionOperation::ChatPoll;
        menu.chat.status_=RoomAdmissionClient::Status::Succeeded;
        menu.chat.response_=AdmissionResponse(); menu.chat.response_.ok=true;
        menu.chat.response_.hasPresence=true; menu.chat.response_.onlineCount=2;
        menu.chat.response_.waitingNames={"Alice","Bob"};
        menu.updateLobbyChat();
        menuRequire(menu.waitingLabel.getText()=="Players waiting: 2", "Waiting count not shown");
        menuRequire(menu.waitingNames.getText()=="Alice, Bob", "Waiting names not shown");
        menu.chatHistory.setText("Alice: Looking for a campaign partner.\nBob: I can join after this match.");
        menu.chatLabel.setText("Chatting as " + settingsName);
        menuCapture(menu,"public-chat");
        menu.chatPending=true; menu.chatAction=AdmissionOperation::ChatPoll;
        menu.chat.status_=RoomAdmissionClient::Status::Failed;
        menu.updateLobbyChat();
        menuRequire(menu.waitingLabel.getText()=="Players waiting"
            && menu.waitingNames.getText()=="Unable to refresh online players", "Failed poll leaves a stale count");
    }
    {
        GameInitSettings networkInit(getBasename(map,true),readCompleteFile(map),"Prepared custom",false,setup.rules);
        CrossplayMenu menu(networkInit,false,setup.players);
        menuRequire(menu.preparedGame->getFiledata()==init.getFiledata(),"Prepared hosting changed the selected map");
        menuRequire(menu.preparedPlayers.changeEventList.size()==original.changeEventList.size(),"Prepared hosting lost the roster");
        menuRequire(menu.visibilityChoice.getSelectedIndex()==0,"Explicit private room choice was not preserved");
    }
    {
        OptionsMenu menu;
        for(int page=0;page<5;++page) { menu.showPage(page); menuCapture(menu,"settings-"+std::to_string(page)); }
        menuRequire(menu.interfaceSizeDropDownBox.getParent()==&menu.interfaceHBox,"Interface size missing from Settings");
        menuRequire(menu.pages[1].getMinimumSize().y <= menu.pages[1].getSize().y,"Graphics controls overflow");
        menuRequire(menu.pages[4].getMinimumSize().y <= menu.pages[4].getSize().y,"Advanced controls overflow");
        menuRequire(menu.diagnosticLogsCheckbox.isChecked()==settings.general.diagnosticLogs,"Diagnostic preference not reflected in Settings");
        menu.diagnosticLogsCheckbox.setChecked(!settings.general.diagnosticLogs);
        menu.onChangeOption(true);
        menuRequire(menu.acceptButton.isEnabled(),"Diagnostic preference change cannot be applied");
        const bool diagnostics=menu.diagnosticLogsCheckbox.isChecked();
        menu.showPage(2);
        settings.network.metaServer=""; menu.metaServerTextBox.setText("");
        menu.playSFXCheckbox.setChecked(!settings.audio.playSFX);
        menu.wasdCameraCheckbox.setChecked(true);
        menu.leftClickOrdersCheckbox.setChecked(true);
        menu.movementPathsCheckbox.setChecked(false);
        menu.onOptionsOK();
        menuRequire(settings.general.wasdCamera && settings.general.leftClickOrders && !settings.general.showMovementPaths,"Input options not applied");
        INIFile savedOptions(getConfigFilepath());
        menuRequire(settings.general.diagnosticLogs==diagnostics
            && savedOptions.getBoolValue("General","Diagnostic Logs",!diagnostics)==diagnostics,"Diagnostic preference not applied/persisted");
        menuRequire(savedOptions.getBoolValue("General","WASD Camera",false)
            && savedOptions.getBoolValue("General","Left Click Orders",false)
            && !savedOptions.getBoolValue("General","Movement Paths",true),"Input options not persisted");
        menuRequire(menu.isQuiting(),"Unchanged legacy network field blocked audio apply");
    }
    {
        menuRequire(ModManager::instance().setActiveMod("dunecity"),"Skin options fixture mod unavailable");
        OptionsMenu menu;
        menu.showPage(0);
        menuRequire(menu.pages[0].getMinimumSize().y<=menu.pages[0].getSize().y,"Campaign skin controls overflow Settings");
        menu.duneCityCampaignSkinDropDownBox.setSelectedItem(1);
        menu.onOptionsOK();
        menuRequire(settings.general.duneCityCampaignSkin==1,"Campaign skin preference not applied");
        INIFile savedOptions(getConfigFilepath());
        menuRequire(savedOptions.getIntValue("General","DuneCity Campaign Skin",0)==1,"Campaign skin preference not persisted");
        menuCapture(menu,"settings-dunecity-skin");
        ModManager::instance().setActiveMod("vanilla");
    }
    for(const bool workshop : {true,false}) {
        auto menu = createExtrasMenu(workshop);
        auto* layout = dynamic_cast<StaticContainer*>(menu->getWindowWidget());
        menuRequire(layout != nullptr, "Workshop/Extras has no menu layout");
        std::vector<std::string> buttons, descriptions;
        for(const auto& data : layout->containedWidgets) {
            menuRequire(data.position.x>=0 && data.position.y>=0 && data.position.x+data.size.x<=menu->getSize().x
                && data.position.y+data.size.y<=menu->getSize().y, "Editors/Extras control extends outside the screen");
            if(auto* button = dynamic_cast<TextButton*>(data.pWidget)) buttons.push_back(button->getText());
            if(auto* label = dynamic_cast<Label*>(data.pWidget)) {
                menuRequire(label->isVisible(), "Editors supporting text is hidden");
                if(label->getText() != "Editors" && label->getText() != "Extras") descriptions.push_back(label->getText());
            }
        }
        if(workshop) {
            menuRequire(buttons==std::vector<std::string>{"Map Editor","Mod Editor","Asset Editors","Back"},
                        "Editor destinations changed or a mod activation destination remains");
            menuRequire(descriptions.size()==3,
                        "Editors supporting descriptions are missing");
        } else menuRequire(buttons==std::vector<std::string>{"Replays","How to Play","About & Credits","Back"},
                           "Extras contains editor or mod activation destinations");
        menuCapture(*menu,workshop ? "editors" : "extras");
    }
    { AboutMenu menu;menuCapture(menu,"about"); }
    { HowToPlayMenu menu;menuCapture(menu,"help"); }
    {
        ModMenu menu;
        menuRequire(menu.titleLabel.getText()=="Mod Editor", "Legacy mod activation screen remains");
        for(size_t i = 0; i < menu.mods.size(); ++i)
            if(menu.mods[i].name == "vanilla") menu.modListBox.setSelectedItem(static_cast<int>(i));
        menuRequire(!menu.editButton.isEnabled(), "Bundled mod can be edited without making a copy");
        menuCapture(menu,"mod-projects");
    }
    {
        auto& manager=ModManager::instance();manager.setActiveMod("dunecity");
        MapEditor editor;
        while(editor.pInterface->hasChildWindow()){editor.pInterface->closeChildWindow();editor.pInterface->processChildWindowOpenCloses();}
        editor.setMap(MapData(64,64,Terrain_Rock),MapInfo());
        editor.getMap()(0,0)=Terrain_Spice;
        auto& structures=editor.getStructureList(); auto& units=editor.getUnitList();
        structures.emplace_back(1,HOUSE_HARKONNEN,Structure_ZoneResidential,256,Coord(4,4));
        structures.emplace_back(2,HOUSE_HARKONNEN,Structure_Road,256,Coord(6,4));
        structures.emplace_back(3,HOUSE_HARKONNEN,Structure_TechCenter,256,Coord(8,4));
        structures.emplace_back(4,HOUSE_HARKONNEN,Structure_Refinery,256,Coord(12,4));
        structures.emplace_back(5,HOUSE_HARKONNEN,Structure_Wall,256,Coord(15,4));
        units.emplace_back(1,HOUSE_HARKONNEN,Unit_RocketTrike,256,Coord(4,12),0,GUARD);
        units.emplace_back(2,HOUSE_HARKONNEN,Unit_AmbientHelicopter,256,Coord(5,12),0,GUARD);
        units.emplace_back(3,HOUSE_HARKONNEN,Unit_Harvester,256,Coord(6,12),0,GUARD);
        const auto players=editor.getPlayers().size();
        auto plan=editor.planMapTypeConversion("vanilla");
        menuRequire(plan.removedStructureIDs.size()==3 && plan.removedUnitIDs.size()==2,"Vanilla conversion plan retains incompatible objects");
        menuRequire(structures.size()==5 && units.size()==3,"Planning conversion changed the map");
        auto cityPlan=editor.planMapTypeConversion("dunecity");
        menuRequire(cityPlan.removedStructureIDs.size()==1 && cityPlan.removedUnitIDs.size()==1,"City conversion retains Tornie-only objects");
        MapSettingsWindow options(&editor,HOUSE_HARKONNEN);
        options.mapTypeDropDownBox.setSelectedItem(0);options.updateMapTypeNote();
        options.centralScroll.scrollbar.setCurrentValue(180);
        menuRequire(options.getSize().x<=getRendererWidth() && options.getSize().y<=getRendererHeight(),"Map settings exceeds screen");
        menuCapture(options,"map-conversion-settings");
        options.onConvert();options.processChildWindowOpenCloses();
        auto* question=dynamic_cast<QstBox*>(options.pChildWindow);
        menuRequire(question!=nullptr,"Destructive conversion did not ask first");
        menuCapture(options,"map-conversion-confirmation");
        question->onButton(QSTBOX_BUTTON2);options.processChildWindowOpenCloses();
        menuRequire(structures.size()==5 && units.size()==3 && manager.getActiveModName()=="dunecity","Cancelled conversion changed the map or mod");
        options.onConvert();options.processChildWindowOpenCloses();
        question=dynamic_cast<QstBox*>(options.pChildWindow);menuRequire(question!=nullptr,"Conversion retry lost confirmation");
        question->onButton(QSTBOX_BUTTON1);options.processChildWindowOpenCloses();
        editor.refreshInterfaceIfNeeded();
        menuRequire(structures.size()==2 && units.size()==1 && editor.getMap()(0,0)==Terrain_Spice && editor.getPlayers().size()==players,"Conversion lost compatible content");
        menuRequire(manager.getActiveModName()=="vanilla" && !editor.pInterface->cityStructsVisible_ && editor.hasChangeSinceLastSave(),"Conversion did not switch palette or mark unsaved");
        const std::string path=std::string(std::getenv("MENU_PROBE_OUT"))+"/converted-plain-"+std::getenv("MENU_PROBE_WIDTH")+".ini";
        editor.saveMap(path);INIFile saved(path);
        menuRequire(saved.getStringValue("BASIC","Mod")=="vanilla" && saved.getIntValue("BASIC","Version",0)==2,"Saving conversion lost category or format version");
        editor.undoLastOperation();editor.refreshInterfaceIfNeeded();
        menuRequire(structures.size()==5 && units.size()==3 && manager.getActiveModName()=="dunecity" && editor.pInterface->cityStructsVisible_,"Undo did not restore converted objects and mod");
        editor.redoLastOperation();editor.refreshInterfaceIfNeeded();
        menuRequire(structures.size()==2 && units.size()==1 && manager.getActiveModName()=="vanilla","Redo failed to reapply conversion");
        editor.undoLastOperation();editor.refreshInterfaceIfNeeded();
        editor.applyMapTypeConversion(editor.planMapTypeConversion("tornie"));editor.refreshInterfaceIfNeeded();
        menuRequire(manager.isTornieContentActive() && structures.size()==3 && units.size()==2,"Tornie conversion lost its own content or kept city objects");
        editor.applyMapTypeConversion(editor.planMapTypeConversion("dunecity"));editor.refreshInterfaceIfNeeded();
        menuRequire(manager.isCityModeActive() && structures.size()==2 && units.size()==1 && editor.pInterface->cityStructsVisible_,"Conversion to city did not switch palette");
        const std::string suffix=std::getenv("MENU_PROBE_WIDTH");
        const std::string cityName="City starter "+suffix;
        const std::string desertName="Desert starter "+suffix;
        const std::string cityPath=std::string(std::getenv("MENU_PROBE_OUT"))+"/"+cityName+".ini";
        editor.saveMap(cityPath);INIFile citySaved(cityPath);
        menuRequire(citySaved.getStringValue("BASIC","Name")==cityName && citySaved.getStringValue("BASIC","Mod")=="dunecity","Sparse city Save As lost its name or category");
        const std::string desertPath=std::string(std::getenv("MENU_PROBE_OUT"))+"/"+desertName+".ini";
        editor.saveMap(desertPath);INIFile desertSaved(desertPath);
        menuRequire(desertSaved.getStringValue("BASIC","Name")==desertName && desertSaved.getStringValue("BASIC","Mod")=="vanilla","Save As retained the old city name or inferred category from the active mod");
        const auto cityID=citySaved.getStringValue("Workshop","ID");
        const auto desertID=desertSaved.getStringValue("Workshop","ID");
        menuRequire(!cityID.empty() && cityID!=desertID && desertSaved.getIntValue("BASIC","MapVersion",0)==1,
                    "Save As did not fork map identity and revision history");
        editor.saveMap(desertPath);INIFile resaved(desertPath);
        menuRequire(resaved.getStringValue("Workshop","ID")==desertID && resaved.getIntValue("BASIC","MapVersion",0)==2,
                    "Ordinary save forked identity or lost revision history");
        INIFile immutableMeta(desertPath+".workshop.ini");
        immutableMeta.setBoolValue("Workshop","Immutable",true);immutableMeta.saveChangesTo(desertPath+".workshop.ini");
        editor.saveMap(desertPath);INIFile forked(desertPath);
        menuRequire(forked.getStringValue("Workshop","ID")!=desertID && forked.getIntValue("BASIC","MapVersion",0)==1,
                    "Editing a shared map did not fork its identity");
    }
    // Exercise the real editor palette, scrolling, placement and map round trip.
    for(const std::string mod : {"vanilla", "dunecity"}) {
        menuRequire(ModManager::instance().setActiveMod(mod), "Editor mod unavailable");
        MapEditor editor;
        auto& ui = *editor.pInterface;
        while(ui.hasChildWindow()) { ui.closeChildWindow(); ui.processChildWindowOpenCloses(); }
        editor.setMap(MapData(64,64,Terrain_Rock), MapInfo());
        for(auto* button:{&ui.metaserverSaveButton,&ui.metaserverLoadButton}) {
            const auto pos=ui.windowWidget.getWidgetPosition(button);
            menuRequire(pos.x>=0 && pos.x+button->getSize().x<=getRendererWidth(),"Editor metaserver action exceeds screen width");
            menuRequire(button->getSize().x>=button->getMinimumSize().x,"Editor metaserver action label is clipped");
        }
        ui.onModeButton(3);
        ui.editorModeUnits_ScrollView.scrollbar.setCurrentValue(10000);
        menuRequire(ui.editorModeUnits_ScrollView.getSize().y>0,"Unit palette lost its scroll viewport");
        menuCapture(ui,"editor-units-"+mod);
        ui.onModeButton(2);
        menuRequire(ui.cityStructsVisible_ == (mod == "dunecity"), "City palette has wrong mod gating");
        menuRequire((ui.editorModeStructs_HBoxCityCivic.getParent() != nullptr) == (mod == "dunecity"),
                    "Civic buildings must appear only for city-capable mods");
        if(mod != "dunecity") continue;
        struct PaletteEntry { SymbolButton* button; HBox* row; int item; };
        const PaletteEntry entries[] = {
            {&ui.editorModeStructs_ZoneResidential, &ui.editorModeStructs_HBoxCityZones, Structure_ZoneResidential},
            {&ui.editorModeStructs_ZoneCommercial, &ui.editorModeStructs_HBoxCityZones, Structure_ZoneCommercial},
            {&ui.editorModeStructs_ZoneIndustrial, &ui.editorModeStructs_HBoxCityZones, Structure_ZoneIndustrial},
            {&ui.editorModeStructs_Road, &ui.editorModeStructs_HBoxCityInfra, Structure_Road},
            {&ui.editorModeStructs_NuclearPlant, &ui.editorModeStructs_HBoxCityInfra, Structure_NuclearPlant},
            {&ui.editorModeStructs_PoliceStation, &ui.editorModeStructs_HBoxCityCivic, Structure_PoliceStation},
            {&ui.editorModeStructs_Stadium, &ui.editorModeStructs_HBoxCityCivic, Structure_Stadium},
            {&ui.editorModeStructs_Airport, &ui.editorModeStructs_HBoxCityAirport, Structure_Airport},
        };
        auto& scroll = ui.editorModeStructs_ScrollView;
        int index = 0;
        for(const auto& entry : entries) {
            menuRequire(entry.button->pSymbolSurface.get() != nullptr, "Editor structure icon missing");
            const auto position = ui.editorModeStructs_VBox.getWidgetPosition(entry.row) + entry.row->getWidgetPosition(entry.button);
            menuRequire(position.x >= 0 && position.x + entry.button->getSize().x <= scroll.getContentWidth(),
                        "Editor building clipped horizontally");
            scroll.scrollbar.setCurrentValue(position.y);
            const int x = position.x + entry.button->getSize().x/2;
            const int y = position.y - scroll.scrollbar.getCurrentValue() + entry.button->getSize().y/2;
            menuRequire(y >= 0 && y < scroll.getSize().y, "Cannot scroll to city building");
            scroll.handleMouseLeft(x,y,true);
            scroll.handleMouseLeft(x,y,false);
            menuRequire(editor.currentEditorMode.itemID == entry.item && entry.button->getToggleState(),
                        "Editor building click selects wrong structure");
            editor.performMapEdit(2 + index*6, 4, false);
            menuRequire(editor.getStructureList().size() == static_cast<size_t>(++index), "City building cannot be placed");
        }
        menuCapture(ui, "editor-city-buildings");
        editor.undoLastOperation();
        menuRequire(editor.getStructureList().size() == 7, "City building undo failed");
        editor.redoLastOperation();
        menuRequire(editor.getStructureList().size() == 8, "City building redo failed");
        const std::string path = std::string(std::getenv("MENU_PROBE_OUT")) + "/city-buildings-" + std::getenv("MENU_PROBE_WIDTH") + ".ini";
        editor.saveMap(path);
        editor.loadMap(path);
        menuRequire(editor.getStructureList().size() == 8, "City buildings lost in map round trip");
        for(size_t i=0; i<8; ++i)
            menuRequire(editor.getStructureList()[i].itemID == entries[i].item, "Saved city building changed identity");
        SDL_ClearError();
        editor.drawMap(screenborder, false);
        menuRequire(SDL_GetError()[0] == '\0', "Editor city map rendering failed");
        ui.loadMetaserverMap(path);
        menuRequire(editor.refreshInterface,"Loading a map did not request its mod palette");
        editor.refreshInterfaceIfNeeded();
        menuRequire(editor.pInterface->cityStructsVisible_ && editor.getStructureList().size()==8,
                    "Loading a map lost city palette or structures");
    }
    ModManager::instance().setActiveMod("vanilla");
    { ModMenu menu(ModMenu::Purpose::MapEditor); menuCapture(menu,"map-working-mod"); }
    { ModMenu menu(ModMenu::Purpose::AssetEditors); menuCapture(menu,"asset-working-mod"); }
    {
        const std::string draft = "Workshop UI Probe";
        auto& manager = ModManager::instance();
        std::filesystem::remove_all(manager.getModPath(draft));
        menuRequire(manager.createMod(draft, "vanilla"), "Cannot create editor working copy");
        const auto path = manager.getModPath(draft);
        INIFile metadata(path + "/mod.ini");
        metadata.setStringValue("Test Metadata", "Preserved", "yes");
        menuRequire(metadata.saveChangesTo(path + "/mod.ini"), "Cannot write preservation fixture");
        ModEditorMenu menu(draft);
        menuRequire(menu.saveButton.isEnabled(), "Real mod editor did not load configuration");
        const auto* titleGeometry = menu.windowWidget.getWidgetDataFromWidget(&menu.titleLabel);
        const auto* nameGeometry = menu.windowWidget.getWidgetDataFromWidget(&menu.nameText);
        menuRequire(titleGeometry && nameGeometry && titleGeometry->position.y >= 16
            && titleGeometry->position.y+titleGeometry->size.y <= nameGeometry->position.y
            && menu.titleLabel.getSize().y >= menu.titleLabel.getMinimumSize().y,
            "Mod editor title is clipped or overlaps the first field");
        menuCapture(menu,"mod-editor");
        bool foundNumeric = false;
        for(size_t section = 0; section < menu.sections.size() && !foundNumeric; ++section) {
            menu.sectionChoice.setSelectedItem(static_cast<int>(section));
            for(size_t key = 0; key < menu.keys.size(); ++key) {
                menu.keyChoice.setSelectedItem(static_cast<int>(key));
                if(menu.valueType != ModEditorMenu::ValueType::Integer) continue;
                const std::string original = menu.valueText.getText();
                menu.valueText.setText("nan");
                menuRequire(!menu.applyValue(), "Mod editor accepted a non-finite numeric setting");
                menu.closeChildWindow(); menu.processChildWindowOpenCloses();
                menu.valueText.setText("1");
                menuRequire(menu.applyValue(), "Mod editor rejected a valid integer");
                INIFile disk(path + "/ObjectData.ini");
                menuRequire(disk.getStringValue(menu.sections[section], menu.keys[key]) == original,
                    "Editing a property wrote to disk before Save");
                foundNumeric = true; break;
            }
        }
        menuRequire(foundNumeric, "Mod editor exposes no editable numeric unit stats");
        menu.nameText.setText("Workshop UI Edited");
        menu.save(false);
        menuRequire(!menu.modified, "Save did not persist the mod draft");
        INIFile savedMetadata(path + "/mod.ini");
        menuRequire(savedMetadata.getStringValue("Test Metadata", "Preserved") == "yes",
                    "Mod metadata save discarded unrelated sections");
        const auto revision = Workshop::saveMod(draft);
        menu.save(false);
        menuRequire(Workshop::saveMod(draft).version == revision.version,
                    "Unchanged editor Save allocated another version");
        menuCapture(menu,"mod-editor-saved");
        // Copies and cached installs must carry full nested assets, not just
        // configuration files. This also exercises the Android-compatible
        // file-by-file staging path used by Workshop installation.
        const std::string nested = "graphics_skins/Probe/nested/sprite.bin";
        std::filesystem::create_directories(std::filesystem::path(path)/"graphics_skins/Probe/nested");
        { std::ofstream asset(std::filesystem::path(path)/nested,std::ios::binary); asset << "nested authored sprite bytes"; }
        const auto withAssets = Workshop::saveMod(draft);
        const auto installed = Workshop::installMod(withAssets);
        Workshop::store().verifyDirectory(withAssets,manager.getModPath(installed));
        auto hasChoice = [&](const std::string& name) {
            const auto choices = manager.listModChoices();
            return std::any_of(choices.begin(), choices.end(), [&](const auto& mod) { return mod.name == name; });
        };
        menuRequire(!hasChoice(installed) && hasChoice(draft), "Identical draft and snapshot are duplicated");
        { std::ofstream asset(std::filesystem::path(path)/nested,std::ios::binary); asset << "changed authored sprite bytes"; }
        menuRequire(hasChoice(installed) && hasChoice(draft), "Changed draft hid a distinct saved revision");
        { std::ofstream asset(std::filesystem::path(path)/nested,std::ios::binary); asset << "nested authored sprite bytes"; }
        const std::string copiedDraft = "Workshop Nested Copy Probe";
        std::filesystem::remove_all(manager.getModPath(copiedDraft));
        menuRequire(manager.createMod(copiedDraft,installed), "Cannot make editable copy of a cached revision");
        const auto copiedPath = std::filesystem::path(manager.getModPath(copiedDraft));
        std::ifstream copiedAsset(copiedPath/nested,std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(copiedAsset)),std::istreambuf_iterator<char>());
        menuRequire(bytes=="nested authored sprite bytes", "Workshop copy lost nested authored assets");
        const auto copied = Workshop::saveMod(copiedDraft);
        menuRequire(copied.id!=withAssets.id && copied.base==withAssets.base,
                    "Editable copy reused the immutable original's identity or lost its content base");
        menuRequire(!INIFile((copiedPath/"workshop-revision.ini").string()).getBoolValue("Workshop","Immutable",false),
                    "Editable copy inherited immutable download metadata");
    }
    {
        // Exercise the actual LAN unpacker with a receiver that has neither the
        // installed alias nor its cached revision. No public server is involved.
        namespace fs = std::filesystem;
        auto& manager = ModManager::instance();
        const std::string draft = "Workshop LAN Probe";
        const auto draftPath = fs::path(manager.getModPath(draft));
        const auto previousMod = manager.getActiveModName();
        menuRequire(std::getenv("DUNECITY_USERDIR") != nullptr,
                    "LAN import probe requires the isolated user profile");
        const auto profile = fs::weakly_canonical(std::getenv("DUNECITY_USERDIR"));
        menuRequire(fs::weakly_canonical(draftPath.parent_path()).parent_path() == profile,
                    "LAN import fixture escaped the isolated profile");
        fs::remove_all(draftPath);
        menuRequire(manager.createMod(draft, "vanilla"), "Cannot create LAN revision fixture");
        const std::string assetPath = "graphics/probe-asset.bin";
        const std::string assetBytes = "unpublished custom sprite bytes";
        fs::create_directories(draftPath / "graphics");
        { std::ofstream file(draftPath / assetPath, std::ios::binary); file << assetBytes; }
        const auto revision = Workshop::saveMod(draft);
        const auto alias = Workshop::installMod(revision);
        const auto installed = fs::path(manager.getModPath(alias));
        const auto cached = Workshop::store().root() / "revisions" / revision.hash;
        const auto backup = Workshop::store().root() / (".probe-backup-" + revision.hash);
        struct Cleanup {
            std::function<void()> run;
            ~Cleanup() { try { run(); } catch(const std::exception& error) { SDL_Log("LAN fixture cleanup: %s", error.what()); } }
        } cleanup{[&] {
            manager.setActiveMod(previousMod);
            fs::remove_all(installed);
            fs::remove_all(backup);
            fs::remove_all(draftPath);
            for(const auto& item : Workshop::store().list("mod"))
                if(item.id == revision.id) fs::remove_all(Workshop::store().root() / "revisions" / item.hash);
        }};
        menuRequire(Workshop::activateModRevision(revision.hash), "Cannot activate LAN source snapshot");
        const auto legacyChecksum = manager.getEffectiveChecksums().combined;
        INIFile pointer((installed / "workshop-revision.ini").string());
        menuRequire(Workshop::unhex(pointer.getStringValue("Workshop", "Manifest", "")) == revision.manifest,
                    "LAN source sidecar omitted the complete manifest");
        std::vector<std::pair<std::string, std::string>> files;
        for(const auto& entry : fs::recursive_directory_iterator(installed)) if(entry.is_regular_file()) {
            const auto name = fs::relative(entry.path(), installed).generic_string();
            if(name != ".dunecity-managed") files.emplace_back(name, readCompleteFile(entry.path().string()));
        }
        std::sort(files.begin(), files.end());
        const auto package = [&](bool tamperAsset) {
            std::string bytes;
            const auto integer = [&](uint32_t value) { bytes.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
            integer(static_cast<uint32_t>(files.size()));
            for(const auto& [name, original] : files) {
                auto contents = original;
                if(tamperAsset && name == assetPath) contents[0] ^= 1;
                integer(static_cast<uint32_t>(name.size())); bytes += name;
                integer(static_cast<uint32_t>(contents.size())); bytes += contents;
            }
            return bytes;
        };
        const auto valid = package(false), altered = package(true);
        menuRequire(valid.size() < MAX_MOD_TRANSFER_SIZE, "LAN test fixture exceeds the bounded peer transfer");
        menuRequire(manager.setActiveMod("vanilla"), "Cannot release the LAN source alias");
        fs::remove_all(installed);
        fs::rename(cached, backup);
        menuRequire(!fs::exists(installed) && !fs::exists(cached), "LAN receiver was not empty");
        menuRequire(!manager.saveReceivedMod(alias, altered, legacyChecksum),
                    "LAN accepted altered sprite bytes with matching legacy INI checksums");
        menuRequire(!fs::exists(installed) && !fs::exists(cached),
                    "Rejected LAN data became an installed or cached revision");
        menuRequire(manager.saveReceivedMod(alias, valid, legacyChecksum),
                    "LAN could not import an unpublished complete revision offline");
        const auto received = Workshop::store().get(revision.hash);
        menuRequire(received.hash == revision.hash && received.version == revision.version
                    && received.base == revision.base && received.manifest == revision.manifest,
                    "LAN import changed the pinned identity, version or engine base");
        menuRequire(Workshop::activateModRevision(revision.hash), "LAN imported revision could not activate");
        menuRequire(manager.getContentBase(alias) == "vanilla"
                    && manager.getEffectiveChecksums().combined == legacyChecksum
                    && readCompleteFile((installed / assetPath).string()) == assetBytes,
                    "LAN imported revision lost its engine identity, rules or sprite asset");
        // A second poisoned transfer cannot replace a previously verified revision.
        menuRequire(!manager.saveReceivedMod(alias, altered, legacyChecksum),
                    "LAN overwrote an installed revision with altered full-package content");
        menuRequire(Workshop::saveMod(alias).hash == revision.hash
                    && readCompleteFile((installed / assetPath).string()) == assetBytes,
                    "Rejected LAN replacement damaged the verified local revision");
        SDL_Log("MENU_PROBE_LAN_REVISION_PASS: offline full-package import and asset tamper rejection");
    }
    for(const std::string kind : {"map", "mod"}) {
        auto menu=Workshop::createMetaserverMenu(kind,false);
        auto* layout=dynamic_cast<StaticContainer*>(menu->getWindowWidget());
        menuRequire(layout!=nullptr,"Metaserver loader has no layout");
        bool load=false;
        for(const auto& data:layout->containedWidgets) {
            menuRequire(data.position.x>=0 && data.position.y>=0 && data.position.x+data.size.x<=menu->getSize().x
                && data.position.y+data.size.y<=menu->getSize().y,"Metaserver control outside screen");
            if(auto* button=dynamic_cast<TextButton*>(data.pWidget)) if(button->getText()=="Load") {
                load=true; menuRequire(!button->isEnabled(),"Empty catalogue allows loading");
            }
        }
        menuRequire(load,"Metaserver load button missing");
        menuCapture(*menu,"metaserver-"+kind+"-loader");
    }
    {
        // Receiving the first map must work before any editor or map browser has
        // created the destination directory (including a fresh browser profile).
        namespace fs = std::filesystem;
        menuRequire(std::getenv("DUNECITY_USERDIR") != nullptr, "Fresh map test needs an isolated profile");
        const auto directory = fs::path(std::getenv("DUNECITY_USERDIR"))/"maps/multiplayer";
        const auto backup = fs::path(directory.string()+".probe-backup");
        const bool existed = fs::exists(directory);
        if(existed) fs::rename(directory,backup);
        struct Restore {
            fs::path directory, backup; bool existed;
            ~Restore() { std::error_code e; fs::remove_all(directory,e); if(existed) fs::rename(backup,directory,e); }
        } restore{directory,backup,existed};
        const std::string data="[BASIC]\nName=First shared map\n";
        const auto mod=Workshop::saveMod("vanilla");
        const auto source=Workshop::saveMapData("First shared map.ini",data,mod.hash);
        const auto received=Workshop::receiveMap("First shared map.ini",data,source.hash,source.manifest,source.version);
        const auto installed=Workshop::installMap(received);
        menuRequire(fs::is_directory(directory) && readCompleteFile(installed)==data,
                    "First received map did not create its destination or preserve bytes");
        menuRequire(fs::exists(installed+".workshop.ini"), "First received map lost revision metadata");
        menuRequire(Workshop::installMap(received)==installed,
                    "Downloading the same map again changed its destination");
        fs::remove(installed+".workshop.ini");
        const auto separate=Workshop::installMap(received);
        menuRequire(separate!=installed && readCompleteFile(installed)==data
                    && readCompleteFile(separate)==data,
                    "A same-name local map without metadata was overwritten");
    }
    {
        // A guest that has never played the host's mod receives the map first: the join
        // resolves - and if necessary downloads - that mod only after the lobby opens, and
        // the packet handler must not wait for it. This drives the real SENDGAMEINFO path
        // with the dependency genuinely absent from the local store.
        namespace fs = std::filesystem;
        auto& manager = ModManager::instance();
        menuRequire(std::getenv("DUNECITY_USERDIR") != nullptr,
                    "Fresh guest probe requires the isolated user profile");
        const std::string draft = "Workshop Guest Probe";
        const auto draftPath = fs::path(manager.getModPath(draft));
        const auto previousMod = manager.getActiveModName();
        fs::remove_all(draftPath);
        menuRequire(manager.createMod(draft,"vanilla"),"Cannot create the host's mod fixture");
        const auto mod = Workshop::saveMod(draft);
        const std::string data = "[BASIC]\nName=Fresh guest map\n";
        const auto source = Workshop::saveMapData("Fresh guest map.ini",data,mod.hash);
        const auto root = Workshop::store().root();
        const auto cachedMod = root/"revisions"/mod.hash;
        const auto held = root/(".probe-guest-"+mod.hash);
        const auto installed = (fs::path(std::getenv("DUNECITY_USERDIR"))/"maps/multiplayer"
                                /"Fresh guest map.ini").string();
        struct Cleanup {
            std::function<void()> run;
            ~Cleanup() { try { run(); } catch(const std::exception& error) { SDL_Log("Fresh guest cleanup: %s", error.what()); } }
        } cleanup{[&] {
            manager.setActiveMod(previousMod);
            std::error_code code;
            fs::remove_all(manager.getModPath("ws-"+mod.hash),code);
            fs::remove_all(held,code); fs::remove_all(draftPath,code);
            fs::remove(installed,code); fs::remove(installed+".workshop.ini",code);
            for(const auto& item : Workshop::store().list("mod"))
                if(item.id==mod.id) fs::remove_all(root/"revisions"/item.hash,code);
            for(const auto& item : Workshop::store().list("map"))
                if(item.id==source.id) fs::remove_all(root/"revisions"/item.hash,code);
        }};
        // Become that guest: the map arrives inside the packet, the mod does not exist here.
        fs::rename(cachedMod,held);
        fs::remove_all(root/"revisions"/source.hash);
        { std::error_code code; fs::remove(installed,code); fs::remove(installed+".workshop.ini",code); }
        menuRequire(!fs::exists(cachedMod),"The guest fixture still holds the host's mod revision");

        struct Peer final : GamePayloadPeer {
            int refusals=0; std::string reason, value="host";
            Uint32 clientId() const override {return 1;}
            const std::string& name() const override {return value;}
            bool nameAssigned() const override {return true;}
            bool bindName(const std::string&) override {return false;}
            bool isHostPeer() const override {return true;}
            void refuse(const char* text) override {++refusals; reason = text?text:"";}
            void replyConfigHash() override {}
            std::string& gameVersion() override {return value;}
            std::string& quantBotConfigHash() override {return value;}
            std::string& objectDataHash() override {return value;}
            std::string& modRevisionHash() override {return value;}
            void disconnectWithCause(int) override {++refusals;}
        } peer;
        int accepted=0;
        std::unique_ptr<GameInitSettings> joined;
        const std::function<void(const GameInitSettings&,const ChangeEventList&)> receive=
            [&](const GameInitSettings& init,const ChangeEventList&) {
                joined=std::make_unique<GameInitSettings>(init); ++accepted;
            };
        GamePayloadContext context; context.isHost=false; context.allowMapWrite=true;
        NetworkSessionCallbacks callbacks; callbacks.onReceiveGameInfo=&receive;
        const auto host=[&](const std::string& bytes,const std::string& modHash,const std::string& manifest) {
            GameInitSettings hosted("Fresh guest map.ini",bytes,"Fresh guest probe",false,settings.gameOptions);
            // The host announces its own mod, which this guest has neither installed nor cached.
            hosted.setModIdentity(draft,"");
            hosted.setModRevision(modHash,mod.version);
            hosted.setMapRevision(source.hash,source.version,manifest);
            OMemoryStream out; out.open(); hosted.save(out); ChangeEventList().save(out);
            IMemoryStream in(out.getData(),static_cast<int>(out.getDataLength()));
            GamePayloadRouter::handle(NETWORKPACKET_SENDGAMEINFO,in,peer,context,callbacks);
        };

        host(data,mod.hash,source.manifest);
        const std::string refusal = "A guest without the host's mod refused the shared map: "+peer.reason;
        menuRequire(peer.refusals==0 && accepted==1,refusal.c_str());
        menuRequire(joined && joined->getMapRevisionHash()==source.hash,
                    "The accepted setup lost the host's map revision");
        menuRequire(readCompleteFile(installed)==data,"The received map was not installed for the guest");
        menuRequire(fs::exists(installed+".workshop.ini"),"The received map lost its revision metadata");
        {
            INIFile metadata(installed+".workshop.ini");
            menuRequire(metadata.getStringValue("Workshop","Hash","")==source.hash,
                        "Guest metadata does not pin the received map revision");
            menuRequire(metadata.getStringValue("Workshop","Mod","").empty(),
                        "Guest recorded a dependency name it never received");
        }
        menuRequire(!fs::exists(cachedMod),"Receiving a map invented the missing mod revision");
        // Exact content and dependency checks are unchanged: altered bytes and a map pinned
        // to another mod revision are still refused, and neither touches the verified copy.
        host(data+"Altered=1\n",mod.hash,source.manifest);
        menuRequire(peer.refusals==1 && accepted==1,"Altered map bytes reached the lobby");
        host(data,std::string(64,'a'),source.manifest);
        menuRequire(peer.refusals==2 && accepted==1,"A map pinned to another mod revision was accepted");
        menuRequire(readCompleteFile(installed)==data && fs::exists(installed+".workshop.ini"),
                    "A refused payload damaged the verified received map");

        // The mod arrives afterwards, as the lobby's own resolution does it.
        fs::rename(held,cachedMod);
        menuRequire(Workshop::saveMod(draft).hash==mod.hash,
                    "The mod that arrived later is not the revision the map was pinned to");
        auto resolved=*joined;
        WorkshopGameContent::resolveMod(resolved,false);
        menuRequire(manager.getActiveModName()=="ws-"+mod.hash,
                    "The guest did not activate the host's mod revision once it was available");
        menuRequire(Workshop::installMap(Workshop::store().get(source.hash))==installed,
                    "Reinstalling the received map moved it");
        INIFile refreshed(installed+".workshop.ini");
        menuRequire(refreshed.getStringValue("Workshop","Mod","")==mod.base,
                    "The resolved dependency was not recorded for the catalogue");
        menuRequire(readCompleteFile(installed)==data,"Recording the dependency changed the map");
        SDL_Log("MENU_PROBE_FRESH_GUEST_PASS: received map installs without its mod, keeps exact "
                "content checks and records the dependency once the mod resolves");
    }
    { DisplayMenu menu;menuCapture(menu,"display"); }
    {
        auto rules=settings.gameOptions;
        GameOptionsWindow menu(rules);
        menuRequire(!menu.originalUnitDamageCheckbox.isChecked(), "Original damage must default off");
        menu.concreteRequiredCheckbox.setChecked(false);
        menu.maxConstructionYardsOverrideCheckbox.setChecked(true);
        menu.maxConstructionYardsOverrideTextBox.setVisible(true);
        menu.maxConstructionYardsOverrideTextBox.setValue(2);
        menuCapture(menu,"game-rules");
        menu.originalUnitDamageCheckbox.setChecked(true);
        menu.originalUnitDamageCheckbox.bHover = true;
        menu.originalUnitDamageCheckbox.tooltipLastMouseMotion = SDL_GetTicks() - 1000;
        drawnMouseX = getRendererWidth() - 12;
        drawnMouseY = getRendererHeight() - 12;
        menuCapture(menu, "game-rules-damage-tooltip", true);
        const auto tooltipBounds = menu.originalUnitDamageCheckbox.explanationBounds;
        menuRequire(tooltipBounds.w > 0 && tooltipBounds.h > 0
                    && tooltipBounds.x >= 0 && tooltipBounds.y >= 0
                    && tooltipBounds.x + tooltipBounds.w <= getRendererWidth()
                    && tooltipBounds.y + tooltipBounds.h <= getRendererHeight(),
                    "Damage explanation overlay extends outside the viewport");
        menu.onOK();
        menuRequire(menu.getGameOptions().originalUnitDamage, "Original damage checkbox did not save selection");
        auto savedRules = menu.getGameOptions();
        GameOptionsWindow reopened(savedRules);
        menuRequire(reopened.originalUnitDamageCheckbox.isChecked(), "Original damage selection lost on reopening");
        INIFile config(false, "Damage rule fixture");
        for(const auto* section : {"Game Options", "Game Options dunecity", "Game Options dune2r"}) {
            writeGameOptionsToConfig(config, section, savedRules);
            SettingsClass::GameOptionsClass loaded;
            applyGameOptionsFromConfig(config, section, loaded);
            menuRequire(loaded.originalUnitDamage, "Original damage preference did not round-trip through INI");
            loaded.originalUnitDamage = false;
            writeGameOptionsToConfig(config, section, loaded);
            applyGameOptionsFromConfig(config, section, savedRules);
            menuRequire(!savedRules.originalUnitDamage, "Original damage preference could not be cleared");
            savedRules.originalUnitDamage = true;
        }
        menuRequire(!menu.getGameOptions().concreteRequired,"Concrete checkbox did not disable requirements");
        menuRequire(menu.getGameOptions().maximumNumberOfConstructionYardsOverride == 2, "Yard limit UI did not save selection");
    }
    SDL_Log("MENU_PROBE_PASS: home, campaign retry, mission ranges, custom roster, online transition, settings validation and rendering");
    return MENU_QUIT_DEFAULT;
}
