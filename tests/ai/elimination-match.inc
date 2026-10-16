// Included only by campaign-balance-probe.inc (run-campaign-balance.py --elimination-match,
// driven by tests/ai/run-all-against-atreides.py).
//
// A full custom match played to elimination: the real game loop advances until every house
// outside one team is dead (House::isAlive, the engine's own defeat rule) or the time limit.
// Works for a local spectator, where Game::finished is never set, and for a loaded save.
// Once a game minute it logs every playing house's credits, army, harvesters and structures,
// read from the live objects so any controller (QuantBot, AI Player, human) is measured alike.
#include <units/UnitBase.h>
#include <structures/StructureBase.h>

#include <set>
#include <string>

namespace EliminationMatch {

/// Houses that take part: a team and at least one player (sandworm/neutral owners excluded).
inline std::vector<const House*> playingHouses(Game& game) {
    std::vector<const House*> houses;
    for (int h = 0; h < NUM_HOUSES; ++h) {
        const House* house = game.getHouse(h);
        if (house && house->getTeamID() != 0 && !house->getPlayerList().empty()) houses.push_back(house);
    }
    return houses;
}

/// The single team still alive, 0 while two or more teams remain, -1 if none is left.
inline int survivingTeam(Game& game) {
    std::set<int> teams;
    for (const House* house : playingHouses(game))
        if (house->isAlive()) teams.insert(house->getTeamID());
    if (teams.empty()) return -1;
    return teams.size() == 1 ? *teams.begin() : 0;
}

inline void sample(Game& game) {
    const int minute = static_cast<int>(game.getGameCycleCount() / MILLI2CYCLES(60000));
    for (const House* house : playingHouses(game)) {
        int armyValue = 0, armyUnits = 0, harvesters = 0, structures = 0, factories = 0;
        for (const auto* unit : unitList) {
            if (unit->getOwner() != house || unit->getHealth() <= 0) continue;
            const Uint32 item = unit->getItemID();
            if (item == Unit_Harvester || item == Unit_RebelHarvester) { ++harvesters; continue; }
            if (!unit->canAttack()) continue;
            armyValue += currentGame->objectData.data[item][house->getHouseID()].price;
            ++armyUnits;
        }
        for (const auto* structure : structureList) {
            if (structure->getOwner() != house || structure->getHealth() <= 0) continue;
            ++structures;
            const Uint32 item = structure->getItemID();
            if (item == Structure_HeavyFactory || item == Structure_LightFactory || item == Structure_Barracks
                || item == Structure_WOR || item == Structure_HighTechFactory) ++factories;
        }
        SDL_Log("ELIMINATION_SAMPLE: minute=%d cycle=%u house=%d team=%d alive=%d credits=%d army_value=%d army_units=%d "
                "harvesters=%d structures=%d factories=%d kill_value=%d loss_value=%d",
                minute, game.getGameCycleCount(), house->getHouseID(), house->getTeamID(), int(house->isAlive()),
                house->getCredits(), armyValue, armyUnits, harvesters, structures, factories,
                house->getKillValue(), house->getLossValue());
    }
}

/// Plays to elimination or `end`. Returns "won"/"lost" for `house`'s team, or "time_limit".
inline std::string run(Game& game, Uint32 end, int house) {
    const House* tracked = game.getHouse(house);
    if (!tracked) throw std::runtime_error("Elimination match: the tracked house is not in the game");
    const Uint32 minute = MILLI2CYCLES(60000);
    for (const House* other : playingHouses(game)) {
        std::string players;
        for (const auto& player : other->getPlayerList())
            players += (players.empty() ? "" : ",") + player->getPlayerclass();
        SDL_Log("ELIMINATION_ROSTER: house=%d team=%d players=%s observing=%d", other->getHouseID(), other->getTeamID(),
                players.c_str(), int(game.isObserving()));
    }
    int winner = 0;
    sample(game);
    while (game.getGameCycleCount() < end) {
        ProbeDigest::periodic(game);
        game.updateGameState();
        if (game.getGameCycleCount() % minute == 0) sample(game);
        if ((winner = survivingTeam(game)) != 0) break;
    }
    sample(game);
    std::string defeated;
    for (const House* other : playingHouses(game))
        if (!other->isAlive()) defeated += (defeated.empty() ? "" : ",") + std::to_string(other->getHouseID());
    SDL_Log("ELIMINATION_MATCH_RESULT: house=%d team=%d winner_team=%d cycle=%u minutes=%.2f defeated=%s",
            house, tracked->getTeamID(), winner, game.getGameCycleCount(),
            game.getGameCycleCount() / static_cast<double>(minute), defeated.empty() ? "none" : defeated.c_str());
    if (winner == 0) return "time_limit";
    return winner == tracked->getTeamID() ? "won" : "lost";
}

} // namespace EliminationMatch
