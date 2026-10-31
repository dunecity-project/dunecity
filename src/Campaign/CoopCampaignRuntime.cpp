#include <Campaign/CoopCampaignRuntime.h>
#include <Campaign/CoopCampaignTransition.h>
#include <Game.h>
#include <FileClasses/FileManager.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/music/MusicPlayer.h>
#include <GUI/Label.h>
#include <GUI/StaticContainer.h>
#include <GUI/TextButton.h>
#include <Menu/CustomGameStatsMenu.h>
#include <Menu/MenuBase.h>
#include <Network/NetworkManager.h>
#include <misc/FileSystem.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <misc/WebRuntime.h>
#include <mod/ModManager.h>
#include <globals.h>
#include <main.h>
#include <sand.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace coop {
namespace {
std::string serializeSettings(const GameInitSettings& settings) {
    OMemoryStream stream;
    settings.save(stream);
    return std::string(stream.getData(), stream.getDataLength());
}

GameInitSettings nextMissionSettings(const CoopCampaignSession& session,
                                    const GameInitSettings& previous, bool networked = true) {
    auto next = makeGameSettings(session, previous.getGameOptions(), previous.getServername(), networked);
    next.setModIdentity(previous.getModName(), previous.getModChecksum());
    next.setModRevision(previous.getModRevisionHash(), previous.getModRevisionVersion());
    return next;
}

void destroyGame() {
    delete currentGame;
    currentGame = nullptr;
    resetHouseVisualHouseMapping();
}

struct SessionLifetime {
    ~SessionLifetime() {
        destroyGame();
        if(pNetworkManager) pNetworkManager->clearCoopCampaign();
        if(musicPlayer) musicPlayer->changeMusic(MUSIC_MENU);
    }
};

void persist(const CoopCampaignSession& session, const GameInitSettings& settings,
             const std::string& checkpoint = "") {
    try {
        auto bound = session;
        if(!session.isComplete()) {
            bound = CoopCampaignSession::fromMapData(settings.getFiledata());
            if(!sameCampaignContext(bound.context(), session.context()))
                throw std::invalid_argument("Co-op progress does not match prepared settings");
        }
        bound.saveProgress(progressPath(bound.context()), serializeSettings(settings), checkpoint);
        WebRuntime::syncPersistentFiles();
    } catch(const std::exception& error) {
        // Local storage is not simulation state. A full disk never removes a peer.
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Co-op progression could not be saved: %s", error.what());
    }
}

bool checkpoint(Game& game, const Context& context) {
    const auto target = std::filesystem::u8path(checkpointPath(context));
    auto temporary = target;
    temporary += ".tmp";
    auto backup = target;
    backup += ".bak";
    std::error_code ignored;
    try {
        std::filesystem::create_directories(target.parent_path());
        if(!game.saveGame(temporary.u8string())) {
            std::filesystem::remove(temporary, ignored);
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Co-op checkpoint could not be saved");
            return false;
        }
        const bool hadTarget = std::filesystem::exists(target);
        if(hadTarget) {
            std::filesystem::remove(backup, ignored);
            std::filesystem::rename(target, backup);
        }
        try {
            std::filesystem::rename(temporary, target);
        } catch(...) {
            if(hadTarget && !std::filesystem::exists(target))
                std::filesystem::rename(backup, target, ignored);
            throw;
        }
        std::filesystem::remove(backup, ignored);
        WebRuntime::syncPersistentFiles();
        return true;
    } catch(const std::exception& error) {
        std::filesystem::remove(temporary, ignored);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Co-op checkpoint could not be saved: %s", error.what());
        return false;
    }
}

bool peersMatch(const std::vector<std::string>& partners) {
    if(!pNetworkManager) return partners.empty();
    const auto peers = pNetworkManager->getConnectedPeers();
    return std::set<std::string>(peers.begin(), peers.end())
        == std::set<std::string>(partners.begin(), partners.end());
}

GameInitSettings savedSetup(const GameInitSettings& settings) {
    const auto bytes = settings.getFiledata().empty()
        ? readCompleteFile(settings.getFilename()) : settings.getFiledata();
    IMemoryStream stream(bytes.data(), bytes.size());
    GameInitSettings::HouseInfoList houses;
    return GameInitSettings::readSaveSetup(stream, houses);
}

void reconfigureResume(CoopCampaignSession& session, const GameInitSettings::HouseInfoList& requested) {
    std::vector<Slot> allies;
    auto participants = currentGame->getGameInitSettings().getHouseInfoList();
    for(const int index : session.context().alliedSlots()) {
        auto slot = session.context().slots[index];
        const auto selected = std::find_if(requested.begin(), requested.end(),
            [&](const auto& house) { return house.houseID == slot.house; });
        if(selected != requested.end()) {
            std::vector<std::string> controllers;
            for(const auto& player : selected->playerInfoList)
                if(player.playerClass == HUMANPLAYERCLASS) controllers.push_back(player.playerName);
            if(!controllers.empty()) {
                slot.playerClass = HUMANPLAYERCLASS;
                slot.playerName = controllers.front();
                slot.controllers = session.context().archon ? controllers : std::vector<std::string>{};
            } else if(!selected->playerInfoList.empty()) {
                slot.playerClass = selected->playerInfoList.front().playerClass;
                slot.playerName = selected->playerInfoList.front().playerName;
                slot.controllers.clear();
            }
            slot.color = selected->colorOfHouse;
            for(auto& house : participants)
                if(house.houseID == slot.house) house = *selected;
        }
        allies.push_back(std::move(slot));
    }
    // Rebind all bases atomically, including every secondary Archon controller.
    session.reconfigureAllies(allies);
    currentGame->updateCoopParticipants(session.prepareMap(currentGame->getGameInitSettings().getFiledata()), participants);
}

class TransitionMenu final : public MenuBase {
public:
    TransitionMenu(CoopCampaignSession session, const GameInitSettings& settings,
                   Outcome outcome, std::vector<std::string> partners, bool networked)
        : session_(std::move(session)), settings_(settings), outcome_(outcome),
          partners_(std::move(partners)), networked_(networked),
          barrier_(session_.context().sessionId, session_.context().stage, partners_) {
        auto* background = pGFXManager->getUIGraphic(UI_MenuBackground);
        setBackground(background);
        resize(getTextureSize(background));
        setWindowWidget(&container_);
        title_.setAlignment(Alignment_HCenter);
        title_.setText(_("Co-op campaign"));
        container_.addWidget(&title_, Point(36, 40), Point(getRendererWidth() - 72, 36));
        message_.setAlignment(Alignment_HCenter);
        message_.setTextFontSize(16);
        message_.setText(_("Mission ") + std::to_string(session_.context().stage) + "/9\n\n"
            + (outcome == Outcome::Won
                ? _("Your team won.\nAll controllers must be ready to continue.")
                : _("Your team lost.\nProgress remains at this mission.")));
        container_.addWidget(&message_, Point(36, 110), Point(getRendererWidth() - 72, 160));
        continue_.setText(outcome == Outcome::Won ? _("Ready: continue") : _("Ready: retry"));
        continue_.setOnClick([this] { ready(); });
        container_.addWidget(&continue_, Point(getRendererWidth() / 2 - 150, 300), Point(300, 32));
        cancel_.setText(_("Return to menu"));
        cancel_.setOnClick([this] { quit(); });
        container_.addWidget(&cancel_, Point(getRendererWidth() / 2 - 150, 360), Point(300, 32));
    }

    void quit(int value = MENU_QUIT_DEFAULT) override {
        if(!resolved_ && networked_ && pNetworkManager) {
            if(pNetworkManager->isServer())
                pNetworkManager->sendCoopAdvance(session_.context().sessionId, session_.context().stage,
                    static_cast<Uint8>(Advance::Aborted));
            else pNetworkManager->sendCoopReady(session_.context().sessionId, session_.context().stage,
                    static_cast<Uint8>(Outcome::Aborted));
        }
        MenuBase::quit(value);
    }

    void update() override {
        if(isQuiting()) return;
        if(!networked_) {
            if(ready_) resolveHost(*barrier_.decision(outcome_, StageCount));
            return;
        }
        if(!pNetworkManager || !peersMatch(partners_)) { quit(); return; }
        if(pNetworkManager->isServer()) {
            for(const auto& message : pNetworkManager->takeCoopReady()) {
                if(message.epoch != pNetworkManager->getCoopCampaignEpoch()) continue;
                barrier_.receive(message.playerName, message.sessionId, message.stage,
                    static_cast<Outcome>(message.value));
            }
            const auto action = barrier_.decision(outcome_, StageCount);
            if(action && (*action == Advance::Aborted || ready_)) resolveHost(*action);
        } else {
            for(const auto& message : pNetworkManager->takeCoopAdvance()) {
                if(message.epoch != pNetworkManager->getCoopCampaignEpoch()
                   || message.sessionId != session_.context().sessionId
                   || message.stage != session_.context().stage) continue;
                const auto action = static_cast<Advance>(message.value);
                if(!barrier_.accepts(action, outcome_, StageCount)
                   || (action != Advance::Aborted && !ready_)) { quit(); return; }
                if(action == Advance::Aborted) { resolved_ = true; MenuBase::quit(); return; }
                try {
                    auto advanced = session_;
                    advanced.completeMission(outcome_ == Outcome::Won);
                    if(action == Advance::Complete) {
                        session_ = std::move(advanced);
                        persist(session_, settings_);
                        resolved_ = complete_ = true;
                        MenuBase::quit(1);
                        return;
                    }
                    IMemoryStream stream(message.settingsBlob.data(), message.settingsBlob.size());
                    GameInitSettings next(stream);
                    const auto context = readContext(next.getFiledata());
                    const auto expectedSettings = nextMissionSettings(advanced, settings_);
                    const auto expected = readContext(expectedSettings.getFiledata());
                    if(next.getGameType() != GameType::CustomMultiplayer || !context || !expected
                       || !sameCampaignContext(*expected, *context)
                       || next.getModName() != settings_.getModName()
                       || message.settingsBlob != serializeSettings(expectedSettings)) {
                        quit(); return;
                    }
                    // Revalidate the authored source marker, not only the context.
                    session_ = CoopCampaignSession::fromMapData(advanced.prepareMap(next.getFiledata()));
                    next_ = std::move(next);
                    persist(session_, *next_);
                    resolved_ = true;
                    MenuBase::quit(1);
                    return;
                } catch(const std::exception& error) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Invalid co-op transition: %s", error.what());
                    quit(); return;
                }
            }
        }
    }

    const std::optional<GameInitSettings>& next() const { return next_; }
    bool complete() const { return complete_; }

private:
    void ready() {
        if(ready_) return;
        ready_ = true;
        continue_.setEnabled(false);
        continue_.setText(_("Waiting for all controllers..."));
        if(networked_ && pNetworkManager && !pNetworkManager->isServer())
            pNetworkManager->sendCoopReady(session_.context().sessionId, session_.context().stage,
                static_cast<Uint8>(outcome_));
    }

    void resolveHost(Advance action) {
        if(action == Advance::Aborted) { quit(); return; }
        const auto previous = session_.context();
        try {
            auto advanced = session_;
            advanced.completeMission(outcome_ == Outcome::Won);
            if(action == Advance::Complete) {
                persist(advanced, settings_);
                if(networked_) pNetworkManager->sendCoopAdvance(previous.sessionId, previous.stage,
                    static_cast<Uint8>(action));
                complete_ = true;
            } else {
                next_ = nextMissionSettings(advanced, settings_, networked_);
                advanced = CoopCampaignSession::fromMapData(next_->getFiledata());
                persist(advanced, *next_);
                if(networked_) pNetworkManager->sendCoopAdvance(previous.sessionId, previous.stage,
                    static_cast<Uint8>(action), serializeSettings(*next_));
            }
            session_ = std::move(advanced);
            resolved_ = true;
            MenuBase::quit(1);
        } catch(const std::exception& error) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Co-op mission could not advance: %s", error.what());
            quit();
        }
    }

    CoopCampaignSession session_;
    GameInitSettings settings_;
    Outcome outcome_;
    std::vector<std::string> partners_;
    bool networked_;
    MissionBarrier barrier_;
    bool ready_ = false, resolved_ = false, complete_ = false;
    std::optional<GameInitSettings> next_;
    StaticContainer container_;
    Label title_, message_;
    TextButton continue_, cancel_;
};

class CompletionMenu final : public MenuBase {
public:
    CompletionMenu() {
        auto* background = pGFXManager->getUIGraphic(UI_MenuBackground);
        setBackground(background);
        resize(getTextureSize(background));
        setWindowWidget(&container_);
        message_.setAlignment(Alignment_HCenter);
        message_.setText(_("Co-op campaign complete!\n\nYour team's nine missions have been saved."));
        container_.addWidget(&message_, Point(36, 130), Point(getRendererWidth() - 72, 160));
        back_.setText(_("Return to menu"));
        back_.setOnClick([this] { quit(); });
        container_.addWidget(&back_, Point(getRendererWidth()/2 - 150, 330), Point(300, 32));
    }
private:
    StaticContainer container_;
    Label message_;
    TextButton back_;
};
} // namespace

std::string readMissionTemplate(const CoopCampaignSession& session) {
    if(!ModManager::instance().isTornieFamilyContentActive()
       || session.context().modName != ModManager::instance().getContentBase(ModManager::instance().getActiveModName()))
        throw std::runtime_error("Activate this co-op campaign's content family first");
    auto file = pFileManager->openCampaignFile("coop/" + session.missionFilename());
    const auto size = SDL_RWsize(file.get());
    if(size <= 0) throw std::runtime_error("Cannot read co-op mission template");
    std::string data(static_cast<std::size_t>(size), '\0');
    if(SDL_RWread(file.get(), data.data(), 1, data.size()) != data.size())
        throw std::runtime_error("Incomplete co-op mission template");
    return data;
}

GameInitSettings makeGameSettings(const CoopCampaignSession& session,
    const SettingsClass::GameOptionsClass& requestedOptions, const std::string& serverName, bool multiplayer) {
    auto options = requestedOptions;
    options.easyMode = session.context().easyMode;
    options.alliedControl = session.context().alliedControl;
    options.alliedBuilding = session.context().alliedBuilding;
    options.chaosMode = requestedOptions.chaosMode && session.context().chaosEligible;
    options.immortalHumanPlayer = false;
    const auto mapData = session.prepareMap(readMissionTemplate(session));
    const auto prepared = readContext(mapData);
    if(!prepared) throw std::runtime_error("Missing prepared co-op context");
    const std::string filename = "coop-faction" + std::to_string(prepared->sourceFaction)
        + "-" + getBasename(session.missionFilename());
    GameInitSettings settings = multiplayer
        ? GameInitSettings(filename, mapData, serverName, prepared->archon, options)
        : GameInitSettings(filename, mapData, prepared->archon, options);
    settings.setRandomSeed(session.context().seed ^ (0x9e3779b9u * static_cast<Uint32>(session.context().stage)));
    std::array<bool, NUM_HOUSE_COLOR_SLOTS> usedColors{};
    usedColors[HOUSECOLOR_NATIVES] = true;
    for(const auto& slot : prepared->slots) {
        if(!slot.active()) continue;
        GameInitSettings::HouseInfo house(static_cast<HOUSETYPE>(slot.house), slot.team());
        int color = isValidHouseColorSlot(slot.color) ? slot.color
            : GameInitSettings::HouseInfo(static_cast<HOUSETYPE>(slot.faction), slot.team()).colorOfHouse;
        if(usedColors[color]) {
            color = 0;
            while(color < NUM_HOUSE_COLOR_SLOTS && usedColors[color]) ++color;
        }
        if(color >= NUM_HOUSE_COLOR_SLOTS) throw std::runtime_error("No free co-op house color");
        house.colorOfHouse = color;
        usedColors[color] = true;
        if(slot.playerClass == HUMANPLAYERCLASS) {
            for(const auto& name : slot.humanNames())
                house.addPlayerInfo(GameInitSettings::PlayerInfo(name, HUMANPLAYERCLASS));
        } else house.addPlayerInfo(GameInitSettings::PlayerInfo(slot.playerName, slot.playerClass));
        settings.addHouseInfo(house);
    }
    return settings;
}

std::string progressPath(const Context& context) {
    return getDirname(getConfigFilepath()) + "/coop/" + context.sessionId + ".ini";
}

std::string checkpointPath(const Context& context) {
    return getDirname(getConfigFilepath()) + "/coop/" + context.sessionId + ".dls";
}

GameInitSettings makeSavedGameSettings(const std::string& path,
    const std::string& serverName, const std::string& requiredMod) {
    const auto bytes = readCompleteFile(path);
    IMemoryStream stream(bytes.data(), bytes.size());
    GameInitSettings::HouseInfoList houses;
    const auto saved = GameInitSettings::readSaveSetup(stream, houses);
    const auto context = readContext(saved.getFiledata());
    if(!context || (saved.getGameType() != GameType::CustomMultiplayer && saved.getGameType() != GameType::CustomGame))
        throw std::runtime_error("This save is not a separate-house co-op campaign");
    if(saved.getModName() != requiredMod || context->modName != ModManager::instance().getContentBase(requiredMod))
        throw std::runtime_error("First activate this co-op save's mod in the Mods menu");
    GameInitSettings init(getBasename(path, true), bytes, serverName);
    init.configureCoopSave(saved, houses);
    init.setMultiplePlayersPerHouse(context->archon);
    return init;
}

bool isCoopCampaignSettings(const GameInitSettings& settings) {
    if(!ModManager::instance().isTornieFamilyContentActive()) return false;
    const auto type = settings.getGameType();
    if(type == GameType::LoadMultiplayer || type == GameType::LoadSavegame)
        return isCoopCampaignSettings(savedSetup(settings));
    if(type != GameType::CustomMultiplayer && type != GameType::CustomGame) return false;
    const auto context = readContext(settings.getFiledata());
    return context && context->modName == ModManager::instance().getContentBase(settings.getModName());
}

void runMultiplayerSession(const GameInitSettings& initialSettings) {
    SessionLifetime lifetime;
    GameInitSettings settings = initialSettings;
    const bool networked = isNetworkGameType(initialSettings.getGameType());
    if(networked && (!pNetworkManager || (pNetworkManager->isRoomSession() && !pNetworkManager->isDirectSession())))
        throw std::runtime_error("Separate-house co-op requires direct peer connections");
    if(pNetworkManager) pNetworkManager->clearCoopCampaign();
    Uint32 missionEpoch = 1;
    while(true) {
        const auto requested = settings.getHouseInfoList();
        const bool resume = settings.getGameType() == GameType::LoadMultiplayer;
        currentGame = new Game();
        currentGame->initGame(settings);
        settings = currentGame->getGameInitSettings();
        auto session = CoopCampaignSession::fromMapData(settings.getFiledata());
        if(resume) {
            reconfigureResume(session, requested);
            settings = currentGame->getGameInitSettings();
        }
        const auto context = session.context();
        if(networked) pNetworkManager->configureCoopCampaign(context, missionEpoch);
        if(checkpoint(*currentGame, context)) persist(session, settings, checkpointPath(context));
        else persist(session, settings);
        currentGame->runMainLoop();
        const auto outcome = currentGame->hasFinished()
            ? (currentGame->hasWon() ? Outcome::Won : Outcome::Lost) : Outcome::Aborted;
        if(outcome == Outcome::Aborted) {
            if(checkpoint(*currentGame, context)) persist(session, settings, checkpointPath(context));
            if(networked) {
                if(pNetworkManager->isServer()) pNetworkManager->sendCoopAdvance(context.sessionId, context.stage,
                    static_cast<Uint8>(Advance::Aborted));
                else pNetworkManager->sendCoopReady(context.sessionId, context.stage,
                    static_cast<Uint8>(Outcome::Aborted));
            }
            return;
        }
        CustomGameStatsMenu().showMenu();
        destroyGame();
        auto partners = context.humanNames();
        partners.erase(std::remove(partners.begin(), partners.end(), ::settings.general.playerName), partners.end());
        if(!networked) partners.clear();
        if(networked && !peersMatch(partners)) return;
        TransitionMenu transition(session, settings, outcome, partners, networked);
        transition.showMenu();
        if(transition.complete()) { CompletionMenu().showMenu(); return; }
        if(!transition.next()) return;
        settings = *transition.next();
        ++missionEpoch;
    }
}
} // namespace coop
