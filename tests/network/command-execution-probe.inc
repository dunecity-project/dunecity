// Included by the headless native integration test main. Not part of the game binary.
#include <Game.h>
#include <House.h>
#include <GameInitSettings.h>
#include <Command.h>
#include <GUI/dune/FeedbackWindow.h>
#include <GUI/QstBox.h>
#include <FileClasses/LoadSavePNG.h>
#include <units/UnitBase.h>
#include <units/MCV.h>
#include <misc/OMemoryStream.h>
#include <misc/IMemoryStream.h>
#include <structures/BuilderBase.h>
#include <structures/RepairYard.h>
#include <GUI/ObjectInterfaces/RepairYardInterface.h>
#include <Map.h>
#include <ScreenBorder.h>
#include <players/HumanPlayer.h>
#include <Menu/MenuBase.h>
#include <Menu/HouseChoiceMenu.h>
#include <Network/NetworkManager.h>
#include <memory>
#include <stdexcept>
#include <fstream>
#include <iterator>
#include "../units/sandworm-target-probe.inc"
int runOwnershipProbe() {
    settings.general.playerName = "Host";
    settings.network.metaServer = "http://127.0.0.1:1/";
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    std::ifstream map(PROBE_MAP_PATH);
    std::string mapData((std::istreambuf_iterator<char>(map)), std::istreambuf_iterator<char>());
    if(mapData.empty()) throw std::runtime_error("ownership fixture map missing");
    GameInitSettings init("ownership.ini", mapData, "Ownership probe", true, settings.gameOptions);
    GameInitSettings::HouseInfo humans(HOUSE_ATREIDES, 1);
    humans.addPlayerInfo({"Host", "HumanPlayer"});
    humans.addPlayerInfo({"Coop", "HumanPlayer"});
    init.addHouseInfo(humans);
    GameInitSettings::HouseInfo enemies(HOUSE_HARKONNEN, 2);
    enemies.addPlayerInfo({"Enemy", "HumanPlayer"});
    init.addHouseInfo(enemies);
    game->initGame(init);
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    auto* enemy = game->getHouse(HOUSE_HARKONNEN);
    if(!owner || !enemy) throw std::runtime_error("ownership fixture houses missing");
    runSandwormTargetProbe(*owner);
    auto* hostPlayer = game->getPlayerByName("Host");
    auto* coopPlayer = game->getPlayerByName("Coop");
    auto* enemyPlayer = game->getPlayerByName("Enemy");
    if(!hostPlayer || !coopPlayer || !enemyPlayer) throw std::runtime_error("ownership fixture players missing");
    {
        auto rules = game->gameInitSettings.getGameOptions();
        rules.maximumNumberOfConstructionYardsOverride = -1;
        game->gameInitSettings.setGameOptions(rules);
        auto* mcv = static_cast<MCV*>(owner->createUnit(Unit_MCV, true));
        Coord site = Coord::Invalid();
        for(int y=1; y<currentGameMap->getSizeY()-3 && !site.isValid(); ++y)
            for(int x=1; x<currentGameMap->getSizeX()-3; ++x)
                if(mcv->canDeploy(x,y)) { site=Coord(x,y); break; }
        if(!site.isValid()) throw std::runtime_error("No yard fixture site");
        if(owner->getNumItems(Structure_ConstructionYard) == 0) {
            if(!owner->placeStructure(NONE_ID,Structure_ConstructionYard,site.x,site.y,false,true))
                throw std::runtime_error("Cannot create initial fixture yard");
            site = Coord::Invalid();
            for(int y=1; y<currentGameMap->getSizeY()-3 && !site.isValid(); ++y)
                for(int x=1; x<currentGameMap->getSizeX()-3; ++x)
                    if(mcv->canDeploy(x,y)) { site=Coord(x,y); break; }
            if(!site.isValid()) throw std::runtime_error("No second yard fixture site");
        }
        mcv->deploy(site);
        const int before = owner->getNumItems(Structure_ConstructionYard);
        if(before < 1) throw std::runtime_error("Yard fixture needs initial yard");
        rules.maximumNumberOfConstructionYardsOverride = before;
        game->gameInitSettings.setGameOptions(rules);
        if(mcv->canDeploy(site.x,site.y) || mcv->doDeploy()
           || owner->getNumItems(Structure_ConstructionYard) != before)
            throw std::runtime_error("MCV bypassed yard cap");
        // The same MCV remains alive and can deploy when the rule permits it.
        rules.maximumNumberOfConstructionYardsOverride = before + 1;
        game->gameInitSettings.setGameOptions(rules);
        if(!mcv->doDeploy() || owner->getNumItems(Structure_ConstructionYard) != before+1)
            throw std::runtime_error("Blocked MCV lost or could not deploy below cap");
        OMemoryStream output; output.open();
        game->gameInitSettings.save(output);
        IMemoryStream input(output.getData(), output.getDataLength());
        GameInitSettings copy(input);
        if(copy.getGameOptions().maximumNumberOfConstructionYardsOverride != before+1)
            throw std::runtime_error("Yard cap lost in network/save setup");
        std::string legacy(output.getData(),output.getDataLength());
        const auto marker = legacy.rfind("7DOM");
        if(marker == std::string::npos) throw std::runtime_error("MOD7 fixture missing");
        // Everything after MOD4's payload: the yard limit, the original-damage byte, and
        // MOD7's factor count plus one factor per house.
        const auto afterMod4 = 4 + 1
            + 4 + 4 * game->gameInitSettings.getHouseInfoList().size();
        legacy[marker]='4'; legacy.resize(legacy.size()-afterMod4);
        IMemoryStream oldInput(legacy.data(),legacy.size());
        GameInitSettings old(oldInput);
        if(old.getGameOptions().maximumNumberOfConstructionYardsOverride != -1)
            throw std::runtime_error("Older setup did not default to unlimited yards");
        for(const auto& houseInfo : old.getHouseInfoList())
            if(houseInfo.spiceIncomeMultiplier != SpiceIncome::kDefault)
                throw std::runtime_error("Older setup did not default to 1x spice income");
        if(copy.getHouseInfoList().size() != game->gameInitSettings.getHouseInfoList().size())
            throw std::runtime_error("Yard cap roundtrip lost a house row");
        rules.maximumNumberOfConstructionYardsOverride = -1;
        game->gameInitSettings.setGameOptions(rules);
        SDL_Log("YARD_CAP_PROBE_PASS: refused deployment preserves MCV; MOD7 roundtrip and MOD4 defaults pass");
    }
    UnitBase* unit = owner->createUnit(Unit_Tank, true);
    unit->doSetAttackMode(GUARD);
    auto order=[&](Uint8 player, Uint32 mode) { Command(player, CMD_UNIT_SETMODE, unit->getObjectID(), mode).executeCommand(); };
    order(enemyPlayer->getPlayerID(), HUNT);
    if(unit->getAttackMode()!=GUARD) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OWNERSHIP_PROBE_FAIL: enemy command changed real unit state"); throw std::runtime_error("enemy command changed real unit state"); }
    order(hostPlayer->getPlayerID(), HUNT);
    if(unit->getAttackMode()!=HUNT) throw std::runtime_error("owner command failed");
    order(coopPlayer->getPlayerID(), STOP);
    if(unit->getAttackMode()!=STOP) throw std::runtime_error("shared-house command failed");
    order(hostPlayer->getPlayerID(), 0xffffffffu);
    if(unit->getAttackMode()!=STOP) throw std::runtime_error("invalid enum changed unit");
    order(255, GUARD);
    if(unit->getAttackMode()!=STOP) throw std::runtime_error("unknown issuer changed unit");
    Command(hostPlayer->getPlayerID(), CMD_UNIT_SETMODE, 0xfffffff0u, GUARD).executeCommand();
    SDL_Log("OWNERSHIP_PROBE_PASS: actual game objects reject enemy, unknown and invalid commands; owner and co-op commands work");
    CommandManager batches;
    static_cast<HumanPlayer*>(hostPlayer)->nextExpectedCommandsCycle = 0;
    unit->doSetAttackMode(GUARD);
    const Command legal(hostPlayer->getPlayerID(), CMD_UNIT_SETMODE, unit->getObjectID(), HUNT);
    const Command forged(enemyPlayer->getPlayerID(), CMD_UNIT_SETMODE, unit->getObjectID(), STOP);
    CommandList mixed;
    mixed.commandList.emplace_back(0, std::vector<Command>{legal});
    mixed.commandList.emplace_back(1, std::vector<Command>{forged});
    batches.addCommandList("Host", mixed);
    batches.executeCommands(0);
    if(static_cast<HumanPlayer*>(hostPlayer)->nextExpectedCommandsCycle != 0 || unit->getAttackMode() != GUARD)
        throw std::runtime_error("malformed batch partially applied");
    CommandList gap;
    gap.commandList.emplace_back(1, std::vector<Command>{legal});
    batches.addCommandList("Host", gap);
    batches.executeCommands(1);
    if(static_cast<HumanPlayer*>(hostPlayer)->nextExpectedCommandsCycle != 0 || unit->getAttackMode() != GUARD)
        throw std::runtime_error("gapped batch skipped missing cycle");
    CommandList complete;
    complete.commandList.emplace_back(0, std::vector<Command>{});
    complete.commandList.emplace_back(1, std::vector<Command>{legal});
    complete.commandList.emplace_back(2, std::vector<Command>{});
    batches.addCommandList("Host", complete);
    batches.executeCommands(1);
    if(static_cast<HumanPlayer*>(hostPlayer)->nextExpectedCommandsCycle != 3 || unit->getAttackMode() != HUNT)
        throw std::runtime_error("contiguous retransmission did not recover gap");
    batches.addCommandList("Host", complete);
    if(static_cast<HumanPlayer*>(hostPlayer)->nextExpectedCommandsCycle != 3) throw std::runtime_error("duplicate changed watermark");
    SDL_Log("COMMAND_BATCH_PROBE_PASS: mixed batch atomic, gap held, contiguous retransmission recovered, duplicate ignored");
    Command(hostPlayer->getPlayerID(), CMD_CAMPAIGN_SKIP).executeCommand();
    if(game->isGameFinished()) throw std::runtime_error("skip finished a non-campaign game");
    // Exercise the real Game entry points used by Options and Mentat, not a duplicate policy.
    game->pauseGame();
    if(!game->isGamePaused()) throw std::runtime_error("local game no longer pauses");
    game->resumeGame();
    if(game->isGamePaused()) throw std::runtime_error("local game no longer resumes");
    pNetworkManager = std::make_unique<NetworkManager>(NetworkManager::Transport::RoomRelay);
    game->pauseGame();
    if(game->isGamePaused() || !game->pausedPlayers.empty())
        throw std::runtime_error("relay menu stopped lockstep or queued pause state");
    game->resumeGame();
    if(game->isGamePaused() || !game->pausedPlayers.empty())
        throw std::runtime_error("closing relay menu changed pause state");
    SDL_Log("RELAY_PAUSE_PROBE_PASS: real Game preserves local pause and keeps relay simulation running");
    game.reset(); currentGame=nullptr; pLocalPlayer=nullptr;
    pNetworkManager.reset();
    // Run the real skip handler against campaign state, including the final-level exit.
    for(int level : {4, 9}) {
        game = std::make_unique<Game>(); currentGame = game.get();
        GameInitSettings campaign(HOUSE_ATREIDES, settings.gameOptions, level);
        {
            auto file = pFileManager->openCampaignFile(campaign.getFilename());
            std::string data(static_cast<size_t>(SDL_RWsize(file.get())), '\0');
            if(SDL_RWread(file.get(), data.data(), 1, data.size()) != data.size())
                throw std::runtime_error("campaign probe scenario read failed");
            campaign.setScenarioData(data);
            campaign.enableCoop(true, "Campaign probe");
        }
        campaign.setMultiplePlayersPerHouse(true);
        auto partners = humans;
        partners.addPlayerInfo({"Economy helper", "qBotSupportHard"});
        partners.addPlayerInfo({"Full controller", "qBotMedium"});
        campaign.addHouseInfo(partners);
        campaign.addHouseInfo(enemies);
        game->initGame(campaign);
        auto* controller = game->getPlayerByName(level == 4 ? "Host" : "Coop");
        if(!controller || !game->canSkipMission()) throw std::runtime_error("campaign skip unavailable");
        {
            const bool wasCity = game->citySimEnabled_;
            game->citySimEnabled_ = true;
            GameInterface cityInterface;
            cityInterface.updateObjectInterface();
            const auto* skip = cityInterface.windowWidget.getWidgetDataFromWidget(&cityInterface.skipMissionButton);
            const auto* pollution = cityInterface.windowWidget.getWidgetDataFromWidget(&cityInterface.pollutionOverlayButton);
            if(!cityInterface.skipMissionButton.isVisible() || skip->position.y < pollution->position.y + pollution->size.y
               || skip->position.y + skip->size.y > 480)
                throw std::runtime_error("city sidebar skip overlaps Pollution or falls below compact screen");
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer);
            cityInterface.draw(Point(0,0));
            auto citySidebar = renderReadSurface(renderer);
            SavePNG(citySidebar.get(), "skip-mission-city-sidebar.png");
            game->citySimEnabled_ = wasCity;
        }
        if(level == 4) {
            // Repair yard sidebar. The repair-unit icon used to share a row
            // with the city stat column; a stat line is wider than the whole
            // sidebar, so while a unit was being repaired the icon was laid
            // out past the sidebar's right edge. Checked with a real repairing
            // unit, in city and in vanilla mode, at this build's resolution.
            const bool wasCity = game->citySimEnabled_;
            const Sint32 panel = SIDEBARWIDTH - 25;
            const Coord yardSize = getStructureSize(Structure_RepairYard);
            for(bool cityMode : {true, false}) {
                game->citySimEnabled_ = cityMode;
                Coord site = Coord::Invalid();
                for(int y = 1; y < currentGameMap->getSizeY()-yardSize.y && !site.isValid(); ++y)
                    for(int x = 1; x < currentGameMap->getSizeX()-yardSize.x; ++x)
                        if(currentGameMap->okayToPlaceStructure(x,y,yardSize.x,yardSize.y,false,pLocalHouse)) { site = Coord(x,y); break; }
                if(!site.isValid()) throw std::runtime_error("no repair yard fixture site");
                auto* yard = dynamic_cast<RepairYard*>(pLocalHouse->placeStructure(NONE_ID,Structure_RepairYard,site.x,site.y,false,true));
                auto* wounded = pLocalHouse->createUnit(Unit_Tank, true);
                if(!yard || !wounded) throw std::runtime_error("repair yard fixture missing");
                yard->assignUnit(ObjectPointer(wounded));
                GameInterface sidebar;
                game->getSelectedList().clear();
                game->getSelectedList().insert(yard->getObjectID());
                sidebar.updateObjectInterface();
                auto* repairInterface = dynamic_cast<RepairYardInterface*>(sidebar.pObjectContainer);
                if(!repairInterface) throw std::runtime_error("repair yard has no sidebar interface");
                repairInterface->update();
                if(!repairInterface->repairUnitProgressBar.isVisible())
                    throw std::runtime_error("repairing unit has no progress icon");
                // Worst-case city lines, measured with the real font.
                if(cityMode) {
                    auto& stats = repairInterface->cityStats_;
                    stats.setFitted(stats.roleLabel_,        " Role: Residential");
                    stats.setFitted(stats.populationLabel_,  " Pop: 12345 (lvl 12/16)");
                    stats.setFitted(stats.landValueLabel_,   " Value: Middle Class");
                    stats.setFitted(stats.pollutionLabel_,   " Pollution: Very Heavy (emits)");
                    stats.setFitted(stats.emissionsLabel_,   " Local pollution: Very Heavy");
                    stats.setFitted(stats.crimeLabel_,       " Crime: Dangerous");
                    for(const Label* label : {&stats.roleLabel_, &stats.populationLabel_, &stats.landValueLabel_,
                                              &stats.pollutionLabel_, &stats.emissionsLabel_, &stats.crimeLabel_})
                        if(label->getMinimumSize().x > panel)
                            throw std::runtime_error("city stat line is wider than the sidebar");
                }
                // Nothing in the panel may demand more width than the panel has.
                for(const Widget* widget : {static_cast<const Widget*>(&repairInterface->mainHBox),
                                            static_cast<const Widget*>(&repairInterface->textVBox),
                                            static_cast<const Widget*>(&repairInterface->repairUnitRow),
                                            static_cast<const Widget*>(&repairInterface->repairUnitProgressBar)})
                    if(widget->getMinimumSize().x > panel || widget->getSize().x > panel)
                        throw std::runtime_error("repair yard sidebar content is wider than the sidebar");
                if(repairInterface->textVBox.getMinimumSize().y > repairInterface->textVBox.getSize().y)
                    throw std::runtime_error("repair yard sidebar content is taller than the sidebar");
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer);
                sidebar.draw(Point(0,0));
                auto image = renderReadSurface(renderer);
                SavePNG(image.get(), cityMode ? "repair-yard-city-sidebar.png" : "repair-yard-sidebar.png");
                game->getSelectedList().clear();
            }
            game->citySimEnabled_ = wasCity;
            SDL_Log("REPAIR_YARD_SIDEBAR_PROBE_PASS: repairing icon and worst-case city stats stay inside the sidebar in city and vanilla modes");
            if(std::getenv("DUNECITY_REPAIR_SIDEBAR_ONLY")) {
                game.reset();currentGame=nullptr;pLocalPlayer=nullptr;
                return MENU_QUIT_DEFAULT;
            }
        }
        {
            GameInterface interface;
            game->getSelectedList().clear();
            interface.updateObjectInterface();
            if(!interface.skipMissionButton.isVisible()) throw std::runtime_error("campaign sidebar skip missing");
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer);
            interface.draw(Point(0,0));
            auto sidebar = renderReadSurface(renderer);
            SavePNG(sidebar.get(), "skip-mission-sidebar.png");
            const auto commandsBefore = game->getCommandManager().timeslot;
            auto openSkip = [&]() {
                interface.skipMissionButton.handleMouseLeft(5, 5, true);
                interface.skipMissionButton.handleMouseLeft(5, 5, false);
                auto* menu = game->pInGameMenu.get();
                auto* question = menu ? dynamic_cast<QstBox*>(menu->pChildWindow) : nullptr;
                if(!question || !question->button2.isActive())
                    throw std::runtime_error("sidebar skip confirmation missing or Cancel not default");
                return question;
            };
            auto* question = openSkip();
            game->pInGameMenu->draw();
            auto confirmation = renderReadSurface(renderer);
            SavePNG(confirmation.get(), "skip-mission-confirmation.png");
            SDL_Event cancel{}; cancel.type = SDL_KEYDOWN; cancel.key.keysym.sym = SDLK_RETURN;
            game->pInGameMenu->handleInput(cancel);
            if(game->isGameFinished() || game->isGamePaused() || game->bMenu
               || game->getCommandManager().timeslot.size() != commandsBefore.size())
                throw std::runtime_error("Cancel skipped the mission");
            game->resumeGame(); game->pInGameMenu.reset();
            openSkip();
            SDL_Event escape{}; escape.type = SDL_KEYDOWN; escape.key.keysym.sym = SDLK_ESCAPE;
            game->pInGameMenu->handleInput(escape);
            if(game->isGamePaused() || game->bMenu) throw std::runtime_error("Escape did not return to game");
            game->pInGameMenu.reset();
            question = openSkip();
            question->onButton(QSTBOX_BUTTON1);
            game->pInGameMenu->processChildWindowOpenCloses();
            size_t skipCount = 0;
            for(const auto& slot : game->getCommandManager().timeslot)
                for(const auto& command : slot) if(command.getCommandID() == CMD_CAMPAIGN_SKIP) ++skipCount;
            if(skipCount != 1) throw std::runtime_error("confirmation did not queue exactly one skip command");
            game->getCommandManager().timeslot = commandsBefore;
            game->pInGameMenu.reset();
            game->bReplay = true; interface.updateObjectInterface();
            if(interface.skipMissionButton.isVisible()) throw std::runtime_error("replay allows mission skip");
            game->bReplay = false; interface.updateObjectInterface();
            SDL_Log("SIDEBAR_SKIP_PROBE_PASS: visible campaign action, Cancel default, cancellation safe, confirmation queues skip, replay hidden");
            // Selecting a builder used to swallow all subsequent map clicks.
            bool foundBuilder = false;
            for(auto* structure : structureList) {
                if(structure->getOwner() == pLocalHouse && dynamic_cast<BuilderBase*>(structure)) {
                    game->getSelectedList().clear();
                    game->getSelectedList().insert(structure->getObjectID());
                    interface.updateObjectInterface();
                    if(interface.skipMissionButton.isVisible()) throw std::runtime_error("skip overlaps selected object controls");
                    foundBuilder = true;
                    break;
                }
            }
            if(!foundBuilder) throw std::runtime_error("map input fixture has no builder");
            const auto checkMapClicks = [&] {
                for(int yy : {80, 150, 250, 350}) for(int xx : {100, 200, 300}) {
                    if(interface.handleMouseLeft(xx, yy, true) || interface.handleMouseLeft(xx, yy, false)
                        || interface.handleMouseRight(xx, yy, true) || interface.handleMouseRight(xx, yy, false))
                        throw std::runtime_error("selected builder swallowed a map click");
                }
            };
            checkMapClicks();
            SDL_SetRenderTarget(renderer, nullptr);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer);
            interface.draw(Point(0,0));
            auto menuImage = renderReadSurface(renderer);
            SavePNG(menuImage.get(), "game-interface.png");
            const int x = 630; // Give feedback in the 1024x768 test renderer.
            const int y = 15;
            if(!interface.handleMouseLeft(x, y, true) || !interface.handleMouseLeft(x, y, false)
               || !game->isGamePaused()) throw std::runtime_error("feedback button did not open its modal");
            game->resumeGame();
            checkMapClicks();
            SDL_Log("MAP_INPUT_PROBE_PASS: map clicks pass through selected builder before and after feedback");
            std::vector<std::map<std::string, std::string>> sent;
            auto result = std::make_shared<FeedbackClient::Submission>();
            FeedbackWindow feedback([&](const auto& fields) { sent.push_back(fields); return result; });
            SDL_SetRenderTarget(renderer, nullptr);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            feedback.draw();
            auto image = renderReadSurface(renderer);
            SavePNG(image.get(), "feedback-window.png");
            SDL_TextInputEvent text{};
            SDL_strlcpy(text.text, "Unit selection", sizeof(text.text)); feedback.handleTextInput(text);
            const Point origin = feedback.getPosition();
            const auto click = [&](int x, int y) {
                feedback.handleMouseLeft(origin.x+x, origin.y+y, true);
                feedback.handleMouseLeft(origin.x+x, origin.y+y, false);
            };
            click(40, 160);
            SDL_strlcpy(text.text, "Cannot select units", sizeof(text.text)); feedback.handleTextInput(text);
            click(40, 414);
            if(sent.size() != 1 || sent[0].at("title") != "Unit selection"
                || sent[0].at("details") != "Cannot select units"
                || sent[0].at("context").find("AI Support (Hard)") == std::string::npos
                || sent[0].at("context").find("QuantBot Medium") == std::string::npos)
                throw std::runtime_error("feedback text or AI context missing");
            result->response = "ERROR Temporary failure"; result->done.store(true); feedback.draw();
            result = std::make_shared<FeedbackClient::Submission>();
            click(40, 414);
            if(sent.size() != 2 || sent[0] != sent[1]) throw std::runtime_error("retry changed feedback or id");
            result->response = "OK https://github.com/ggtothemax/dunecity/issues/123"; result->done.store(true);
            feedback.draw();
            image = renderReadSurface(renderer); SavePNG(image.get(), "feedback-success.png");
            click(400, 414); // OK returns to game; View request is deliberately not opened in tests.
            checkMapClicks();
            SDL_Log("FEEDBACK_SUBMISSION_PROBE_PASS: context, failure retry and confirmed success work without a browser");
        }
        Command(255, CMD_CAMPAIGN_SKIP).executeCommand();
        auto* other = game->getPlayerByName("Enemy");
        if(other) Command(other->getPlayerID(), CMD_CAMPAIGN_SKIP).executeCommand();
        Command(controller->getPlayerID(), CMD_CAMPAIGN_SKIP, 1).executeCommand();
        if(game->isGameFinished()) throw std::runtime_error("invalid campaign skip accepted");
        Command(controller->getPlayerID(), CMD_CAMPAIGN_SKIP).executeCommand();
        if(!game->isGameFinished() || game->canSkipMission()) throw std::runtime_error("campaign skip did not finish");
        if(game->whatNext() != GAME_DEBRIEFING_WIN) throw std::runtime_error("skip did not use victory path");
        if(game->whatNext() != (level == 9 ? GAME_RETURN_TO_MENU : GAME_NEXTMISSION))
            throw std::runtime_error("skip chose wrong campaign continuation");
        // Exercise the real SDL event -> Game::doInput -> Map::selectObjects path.
        // Quit only the isolated fixture loop after it has created the live interface.
        SDL_Event quit{}; quit.type = SDL_QUIT; SDL_PushEvent(&quit); game->runMainLoop();
        StructureBase* builder = nullptr;
        for(auto* structure : structureList)
            if(structure->getOwner() == pLocalHouse && dynamic_cast<BuilderBase*>(structure)) { builder = structure; break; }
        UnitBase* target = nullptr;
        for(auto* candidate : unitList)
            if(candidate->getOwner() == pLocalHouse && candidate->getLocation().isValid()) { target = candidate; break; }
        if(!builder || !target) throw std::runtime_error("selection fixture missing builder or unit");
        game->getSelectedList().clear(); game->getSelectedList().insert(builder->getObjectID());
        game->getGameInterface().updateObjectInterface();
        const Coord center = target->getCenterPoint(); screenborder->setNewScreenCenter(center);
        currentGameMap->getTile(target->getLocation())->setExplored(pLocalHouse->getHouseID(), game->getGameCycleCount());
        const int targetX = screenborder->world2screenX(center.x);
        const int targetY = screenborder->world2screenY(center.y);
        for(auto type : {SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP}) {
            SDL_Event click{}; click.type = type; click.button.button = SDL_BUTTON_LEFT;
            click.button.x = targetX; click.button.y = targetY; SDL_PushEvent(&click);
        }
        game->doInput();
        if(game->getSelectedList().count(target->getObjectID()) != 1)
            throw std::runtime_error("real game input could not select a unit after a builder");
        SDL_Log("UNIT_SELECTION_PROBE_PASS: real SDL input selects a unit after a builder on level %d", level);
        // Even with Shift held, a building click replaces troop/building groups.
        StructureBase* otherBuilding = nullptr;
        for (auto* structure : structureList)
            if (structure->getOwner() == pLocalHouse && structure != builder) { otherBuilding = structure; break; }
        for (auto* selectedBefore : {static_cast<ObjectBase*>(target), static_cast<ObjectBase*>(otherBuilding)}) {
            if (!selectedBefore) continue;
            game->unselectAll(game->getSelectedList()); game->getSelectedList().clear();
            selectedBefore->setSelected(true); game->getSelectedList().insert(selectedBefore->getObjectID());
            game->getGameInterface().updateObjectInterface();
            const Coord buildingCenter = builder->getCenterPoint(); screenborder->setNewScreenCenter(buildingCenter);
            currentGameMap->getTile(builder->getLocation())->setExplored(pLocalHouse->getHouseID(), game->getGameCycleCount());
            SDL_SetModState(KMOD_SHIFT);
            for (auto type : {SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP}) {
                SDL_Event click{}; click.type = type; click.button.button = SDL_BUTTON_LEFT;
                click.button.x = screenborder->world2screenX(buildingCenter.x);
                click.button.y = screenborder->world2screenY(buildingCenter.y); SDL_PushEvent(&click);
            }
            game->doInput(); SDL_SetModState(KMOD_NONE);
            if (game->getSelectedList() != std::set<Uint32>{builder->getObjectID()} || selectedBefore->isSelected())
                throw std::runtime_error("building click retained a previous troop/building selection");
            game->getGameInterface().updateObjectInterface();
        }
        SDL_Log("BUILDING_SELECTION_PROBE_PASS: building clicks replace units and other buildings, including Shift-click");
        game.reset(); currentGame = nullptr; pLocalPlayer = nullptr;
    }
    {
        FeedbackEditor editor;
        editor.resize(400, 140);
        editor.setActive();
        SDL_TextInputEvent text{};
        SDL_strlcpy(text.text, "Café", sizeof(text.text));
        editor.handleTextInput(text);
        SDL_KeyboardEvent key{};
        key.keysym.sym = SDLK_BACKSPACE;
        editor.handleKeyPress(key);
        if(editor.value() != "Caf") throw std::runtime_error("feedback backspace broke Unicode");
        key.keysym.sym = SDLK_RETURN; editor.handleKeyPress(key);
        SDL_strlcpy(text.text, "Feedback", sizeof(text.text)); editor.handleTextInput(text);
        if(editor.value() != "Caf\nFeedback") throw std::runtime_error("feedback multiline input failed");
        key.keysym.sym = SDLK_a; key.keysym.mod = KMOD_CTRL; editor.handleKeyPress(key);
        SDL_strlcpy(text.text, "Replacement", sizeof(text.text)); editor.handleTextInput(text);
        if(editor.value() != "Replacement") throw std::runtime_error("feedback select-all replace failed");
        editor.setInactive();
    }
    {
        sdl2::texture_ptr menuTarget(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 640, 480));
        if(!menuTarget || SDL_SetRenderTarget(renderer, menuTarget.get()) != 0) throw std::runtime_error("menu render target failed");
        SDL_RenderSetLogicalSize(renderer, 640, 480);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); SDL_RenderClear(renderer);
        HouseChoiceMenu menu;
        menu.handleMouseLeft(400, 326, true); menu.handleMouseLeft(400, 326, false);
        SDL_KeyboardEvent key{}; key.keysym.sym = SDLK_DOWN;
        for(int choice=1; choice<=9; ++choice) {
            menu.handleKeyPress(key);
            if(HouseChoiceMenu::getSupportBotIndex() != choice)
                throw std::runtime_error("AI partner option missing");
        }
        menu.draw();
        auto image = renderReadSurface(renderer); SavePNG(image.get(), "campaign-ai-options.png");
        key.keysym.sym = SDLK_SPACE; menu.handleKeyPress(key); SDL_RenderClear(renderer); menu.draw();
        image = renderReadSurface(renderer); SavePNG(image.get(), "campaign-ai-description.png");
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_Log("AI_PARTNER_PROBE_PASS: all support and full QuantBot difficulties selectable at 640x480");
    }
    SDL_Log("FEEDBACK_EDITOR_PROBE_PASS: multiline input, Unicode deletion and selection replacement work");
    SDL_Log("CAMPAIGN_SKIP_PROBE_PASS: real commands win campaigns, advance levels, finish final mission and reject invalid issuers");
    return MENU_QUIT_DEFAULT;
}
