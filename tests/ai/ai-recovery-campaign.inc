// Test-private historical observer include; compile with -fno-access-control.
#include <Game.h>
#include <GameInitSettings.h>
#include <House.h>
#include <globals.h>
#include <sand.h>
#include <players/HumanPlayer.h>
#include <players/Player.h>

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
inline std::unique_ptr<Game> initCampaign(int house, int mission, Uint32 seed) {
    const int level = campaignLevel(mission);
    if(house != HOUSE_HARKONNEN && house != HOUSE_ATREIDES && house != HOUSE_ORDOS)
        throw std::invalid_argument("Recovery campaign requires a native campaign house");

    const auto playerHouse = static_cast<HOUSETYPE>(house);
    GameInitSettings init(playerHouse, settings.gameOptions, level);
    init.mission = mission;
    init.filename = GameInitSettings::getScenarioFilename(playerHouse, mission);
    init.randomSeed = seed;
    init.setMultiplePlayersPerHouse(false);
    for(int h = 0; h < NUM_HOUSES; ++h) {
        const auto side = static_cast<HOUSETYPE>(h);
        if(!isHouseAvailable(side)) continue;
        GameInitSettings::HouseInfo info(side, h == house ? 1 : 2);
        info.graphicsSkin = init.getCampaignGraphicsSkin();
        info.addPlayerInfo(GameInitSettings::PlayerInfo(
            h == house ? settings.general.playerName : getHouseNameByNumber(side),
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
    if(!pLocalHouse || pLocalHouse->getPlayerList().size() != 1
       || pLocalHouse->getPlayerList().front()->getPlayerclass() != "qBotBrutal")
        throw std::runtime_error("Recovery campaign did not create its sole native QuantBot controller");
    for(int h = 0; h < NUM_HOUSES; ++h)
        if(const auto* owner = game->getHouse(h))
            for(const auto& player : owner->getPlayerList())
                if(dynamic_cast<HumanPlayer*>(player.get()))
                    throw std::runtime_error("Recovery campaign registered a human controller");

    // Native production callbacks and GameInterface require a HumanPlayer UI
    // identity. It is owned by Game but never added to a house or registered.
    // Neither historical Player header has modern OBSERVER_PLAYER_ID; reserve
    // the maximum byte explicitly, outside this one-controller-per-house roster.
    game->spectatorViewPlayer = std::make_unique<HumanPlayer>(pLocalHouse, settings.general.playerName);
    game->spectatorViewPlayer->playerID = std::numeric_limits<Uint8>::max();
    pLocalPlayer = game->spectatorViewPlayer.get();
    game->bReplay = false;
    return game;
}

} // namespace AIRecovery
