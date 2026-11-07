// Test-private historical observer include; compile with -fno-access-control.
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <globals.h>
#include <sand.h>
#include <players/HumanPlayer.h>
#include <players/Player.h>
#include <players/PlayerFactory.h>
#include <players/QuantBot.h>

#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace AIRecovery {

// This is the native Game::initGame mission-to-tech-level mapping in both releases.
inline int campaignLevel(int mission) {
    if(mission < 1 || mission > 22)
        throw std::invalid_argument("Recovery campaign mission must be between 1 and 22");
    return ((mission + 1) / 3) + 1;
}

// Native sandworms read pLocalHouse while the map is loading. Attach the same
// detached UI as the ordinary observer at native candidate construction, without
// registering a human, changing its constructor, or consuming simulation RNG.
class DetachedViewBootstrap {
    using Create = std::function<std::unique_ptr<Player>(House*, const std::string&)>;
    PlayerFactory::PlayerData* entry = nullptr;
    Create original;
public:
    DetachedViewBootstrap(Game& game, int targetHouse, bool enabled) {
        if(!enabled) return;
        entry = const_cast<PlayerFactory::PlayerData*>(PlayerFactory::getByPlayerClass("qBotBrutal"));
        if(!entry) throw std::runtime_error("Native Brutal QuantBot factory is missing");
        original = entry->pCreate;
        entry->pCreate = [this, &game, targetHouse](House* owner, const std::string& name) {
            auto player = original(owner, name);
            if(owner->getHouseID() == targetHouse) {
                pLocalHouse = owner;
                game.spectatorViewPlayer = std::make_unique<HumanPlayer>(owner, settings.general.playerName);
                game.spectatorViewPlayer->playerID = std::numeric_limits<Uint8>::max();
                pLocalPlayer = game.spectatorViewPlayer.get();
            }
            return player;
        };
    }
    DetachedViewBootstrap(const DetachedViewBootstrap&) = delete;
    DetachedViewBootstrap& operator=(const DetachedViewBootstrap&) = delete;
    ~DetachedViewBootstrap() {
        if(entry) entry->pCreate = std::move(original);
    }
};

// The caller first activates vanilla and applies settings.gameOptions. Returns
// after initGame, BEFORE initializeGameLoop: the caller must call that once,
// then advance the ordinary cycle loop until game.finished or its time limit.
// bReplay is used only during initGame to bypass the interactive briefing;
// ordinary commands, replay recording and native victory checks remain enabled.
inline std::unique_ptr<Game> initCampaign(int house, int mission, Uint32 seed, bool humanPartner) {
    const int level = campaignLevel(mission);
    if(house != HOUSE_HARKONNEN && house != HOUSE_ATREIDES && house != HOUSE_ORDOS)
        throw std::invalid_argument("Recovery campaign requires a native campaign house");

    const auto playerHouse = static_cast<HOUSETYPE>(house);
    GameInitSettings init(playerHouse, settings.gameOptions, level);
    init.mission = mission;
    init.filename = GameInitSettings::getScenarioFilename(playerHouse, mission);
    init.randomSeed = seed;
    init.setMultiplePlayersPerHouse(humanPartner);
    for(int h = 0; h < NUM_HOUSES; ++h) {
        const auto side = static_cast<HOUSETYPE>(h);
        if(!isHouseAvailable(side)) continue;
        GameInitSettings::HouseInfo info(side, h == house ? 1 : 2);
        info.graphicsSkin = init.getCampaignGraphicsSkin();
        if(h == house && humanPartner)
            info.addPlayerInfo({settings.general.playerName, "HumanPlayer"});
        info.addPlayerInfo(GameInitSettings::PlayerInfo(
            getHouseNameByNumber(side),
            h == house ? "qBotBrutal" : "CampaignAIPlayer"));
        init.addHouseInfo(info);
    }

    auto game = std::make_unique<Game>();
    currentGame = game.get();
    pLocalHouse = nullptr;
    pLocalPlayer = nullptr;
    game->bReplay = true;
    {
        DetachedViewBootstrap view(*game, house, !humanPartner);
        game->initGame(init);
    }
    pLocalHouse = game->getHouse(house);
    if(!pLocalHouse || pLocalHouse->getPlayerList().size() != (humanPartner ? 2u : 1u))
        throw std::runtime_error("Recovery campaign has an unexpected player-house controller count");
    int humans = 0, candidates = 0;
    for(int h = 0; h < NUM_HOUSES; ++h)
        if(const auto* owner = game->getHouse(h))
            for(const auto& player : owner->getPlayerList()) {
                if(dynamic_cast<HumanPlayer*>(player.get())) {
                    if(!humanPartner || h != house)
                        throw std::runtime_error("Recovery campaign registered an unexpected human controller");
                    ++humans;
                } else if(h == house) {
                    if(player->getPlayerclass() != "qBotBrutal")
                        throw std::runtime_error("Recovery campaign candidate is not native Brutal QuantBot");
                    ++candidates;
                }
            }
    if(candidates != 1 || humans != int(humanPartner))
        throw std::runtime_error("Recovery campaign arm controller identities do not match");
    if(humanPartner) {
        if(!pLocalPlayer || pLocalPlayer->getHouse() != pLocalHouse)
            throw std::runtime_error("Recovery campaign idle human identity was not assigned");
    } else if(!game->spectatorViewPlayer || pLocalPlayer != game->spectatorViewPlayer.get()
              || pLocalPlayer->getHouse() != pLocalHouse) {
        throw std::runtime_error("Recovery campaign detached UI was not bootstrapped during native load");
    }
    for(const auto& player : pLocalHouse->getPlayerList())
        if(const auto* bot = dynamic_cast<const QuantBot*>(player.get()))
            SDL_Log("RECOVERY_CAMPAIGN_ROLE: house=%d registered_humans=%d allied_with_human=%d campaign_enemy=%d",
                house, humans, int(bot->isAlliedWithHuman()), int(bot->isCampaignEnemy()));
    game->bReplay = false;
    return game;
}

} // namespace AIRecovery
