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

/// The unit-mix slot of \a item (QuantBot::kMixItems order: tank, siege tank, launcher, Devastator,
/// Sonic Tank, Deviator, ornithopter, trike, raider trike, quad), 10 for infantry, -1 otherwise.
inline int mixSlot(Uint32 item) {
    for (size_t i = 0; i < QuantBot::kMixItems.size(); ++i) if (QuantBot::kMixItems[i] == item) return int(i);
    switch (item) {
        case Unit_Soldier: case Unit_Infantry: case Unit_Trooper: case Unit_Troopers: return int(UnitMixPolicy::kSlots);
        default: return -1;
    }
}

/// Per mix slot (+ infantry) of a house: the value built, lost and standing, and the combat reward
/// credited to it (credits of damage value; House::getCombatReward), as "a/b/.../i".
inline void sampleMix(Game& game, const House* house, int minute, bool final) {
    const int houseID = house->getHouseID();
    std::array<long long, UnitMixPolicy::kSlots + 1> built{}, lost{}, army{}, reward{};
    for (Uint32 item = Unit_FirstID; item <= Unit_LastID; ++item) {
        const int slot = mixSlot(item);
        if (slot < 0) continue;
        const long long price = game.objectData.data[item][houseID].price;
        built[slot] += house->getNumBuiltItems(item) * price;
        lost[slot] += house->getNumLostItems(item) * price;
        reward[slot] += house->getCombatReward(item).total() / 1000;
    }
    for (const auto* unit : unitList)
        if (unit->getOwner() == house && unit->getHealth() > 0) {
            const int slot = mixSlot(unit->getItemID());
            if (slot >= 0) army[slot] += game.objectData.data[unit->getItemID()][houseID].price;
        }
    auto join = [](const std::array<long long, UnitMixPolicy::kSlots + 1>& values) {
        std::string out;
        for (const auto value : values) out += (out.empty() ? "" : "/") + std::to_string(value);
        return out;
    };
    SDL_Log("UNIT_MIX_SAMPLE: minute=%d house=%d final=%d built=%s lost=%s army=%s reward=%s", minute, houseID, int(final),
            join(built).c_str(), join(lost).c_str(), join(army).c_str(), join(reward).c_str());
}

} // namespace EliminationMatch

#include "ffa-army.inc"

namespace EliminationMatch {

inline void sample(Game& game, bool final = false) {
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
        // The composition at 10, 20, 30... game minutes and at the end (docs/ai-play-styles.md, section 8).
        if (final || (minute > 0 && minute % 10 == 0)) sampleMix(game, house, minute, final);
    }
    FfaArmy::sample(game);
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
    sample(game, true);
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
