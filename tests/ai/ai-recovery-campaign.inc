// Test-private historical observer include; compile with -fno-access-control.
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <globals.h>
#include <sand.h>
#include <players/HumanPlayer.h>
#include <players/Player.h>
#include <players/QuantBot.h>

#include <limits>
#include <memory>
#include <stdexcept>

namespace AIRecovery {

// This is the native Game::initGame mission-to-tech-level mapping in both releases.
inline int campaignLevel(int mission) {
    if(mission < 1 || mission > 22)
        throw std::invalid_argument("Recovery campaign mission must be between 1 and 22");
    return ((mission + 1) / 3) + 1;
}

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
    game->initGame(init);
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
    } else {
        // Never register this UI identity: the AI-only arm intentionally retains
        // the native campaign-player/enemy role defect, without a policy fix.
        game->spectatorViewPlayer = std::make_unique<HumanPlayer>(pLocalHouse, settings.general.playerName);
        game->spectatorViewPlayer->playerID = std::numeric_limits<Uint8>::max();
        pLocalPlayer = game->spectatorViewPlayer.get();
    }
    for(const auto& player : pLocalHouse->getPlayerList())
        if(const auto* bot = dynamic_cast<const QuantBot*>(player.get()))
            SDL_Log("RECOVERY_CAMPAIGN_ROLE: house=%d registered_humans=%d allied_with_human=%d campaign_enemy=%d",
                house, humans, int(bot->isAlliedWithHuman()), int(bot->isCampaignEnemy()));
    game->bReplay = false;
    return game;
}

} // namespace AIRecovery
