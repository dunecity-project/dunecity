// Included only by campaign-balance-probe.inc (run-campaign-balance.py --scenario, driven by
// tests/ai/scenario-matrix.py; docs/ai-scenario-matrix.md).
//
// One game of the AI scenario matrix, played to its end by the real game loop:
//   - a custom map (bundled multiplayer/singleplayer file) or a generated Random Skirmish
//     multiplayer map (BALANCE_SKIRMISH), every house given its own team and controller class
//     (BALANCE_SCENARIO_SIDES "house:team:class,...", e.g. 1:2:qBotBrutal,0:1:AIPlayerHard), run
//     until one team is left (House::isAlive, the engine's own defeat rule) or the time limit;
//   - a campaign mission (no sides: the human seat's QuantBot partner against the campaign
//     enemies, as run-campaign-balance.py sets them up), run until the mission's own win/lose
//     conditions end it (Game::finished) or the time limit.
// Once a game minute every playing house is sampled (EliminationMatch::sample) and its army's
// "sortie" measured: combat ground units more than kAwayTiles from every structure of their
// house. Scenario triggers (reinforcements, time-outs) are counted as they fire. The end logs
// one SCENARIO_HOUSE line per house and a SCENARIO_RESULT line. Read-only apart from the
// Construction Yard start (deployMcvs), which deploys each start's MCV before the first cycle.
#include <MapEditor/SkirmishGenerator.h>
#include <dunecity/CitySimulation.h>
#include <Menu/RandomSkirmishMenu.h>
#include <players/PlayerFactory.h>
#include <units/MCV.h>

#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace ScenarioMatch {

/// A combat unit further than this (tiles, Chebyshev) from every structure of its house is away from home.
constexpr int kAwayTiles = 20;
/// A minute counts as a sortie minute when at least this many of a house's fighters are away.
constexpr int kSortieUnits = 3;

struct Side {
    int house = HOUSE_INVALID;
    int team = 0;
    std::string playerClass;
};

/// BALANCE_SCENARIO_SIDES: "house:team:class,..." (houses by number, classes as PlayerFactory names them).
inline std::vector<Side> sidesFromEnv() {
    std::vector<Side> sides;
    const char* text = std::getenv("BALANCE_SCENARIO_SIDES");
    if (!text || !*text) return sides;
    std::istringstream input(text);
    std::string entry;
    while (std::getline(input, entry, ',')) {
        const auto first = entry.find(':');
        const auto second = first == std::string::npos ? first : entry.find(':', first + 1);
        if (second == std::string::npos) throw std::runtime_error("Invalid scenario side: " + entry);
        Side side;
        side.house = std::stoi(entry.substr(0, first));
        side.team = std::stoi(entry.substr(first + 1, second - first - 1));
        side.playerClass = entry.substr(second + 1);
        if (side.house < 0 || side.house >= NUM_HOUSES || side.team < 1)
            throw std::runtime_error("Invalid scenario house or team: " + entry);
        if (!PlayerFactory::getByPlayerClass(side.playerClass))
            throw std::runtime_error("Unknown scenario player class: " + entry);
        for (const Side& other : sides)
            if (other.house == side.house) throw std::runtime_error("Two scenario sides share a house");
        sides.push_back(side);
    }
    std::set<int> teams;
    for (const Side& side : sides) teams.insert(side.team);
    if (teams.size() < 2) throw std::runtime_error("A scenario match needs at least two teams");
    return sides;
}

/// BALANCE_SKIRMISH "key=value,...": a Random Skirmish multiplayer map with one start per side,
/// every start alike (the menu's start choices). Keys: size (64..256), base (none = MCV only, cy =
/// Construction Yard only, outpost, small, medium, large, fortress), army (kArmyBudgets index),
/// city (kCityTargets index), seed, tech, rock, spice (indices), credits, symmetric (0/1). The
/// mode (vanilla or DuneCity city sim) follows the active mod, as the menu sets it.
struct Skirmish {
    SkirmishGenerator::Params params;
    bool constructionYardOnly = false;
};

inline Skirmish parseSkirmish(const std::string& text, int starts) {
    using namespace SkirmishGenerator;
    Skirmish out;
    Params& p = out.params;
    p.multiplayer = true;
    p.starts = starts;
    p.mod = ModManager::instance().getActiveModName();
    p.mode = RandomSkirmish::modeForMod(p.mod);
    p.startSide = SkirmishGenerator::Side{"", BaseSize::Medium, 0, 0};
    std::istringstream input(text);
    std::string entry;
    while (std::getline(input, entry, ',')) {
        const auto eq = entry.find('=');
        if (eq == std::string::npos) throw std::runtime_error("Invalid skirmish option: " + entry);
        const std::string key = entry.substr(0, eq), value = entry.substr(eq + 1);
        if (key == "base") {
            static const std::map<std::string, BaseSize> bases{
                {"none", BaseSize::None}, {"cy", BaseSize::None}, {"outpost", BaseSize::Outpost},
                {"small", BaseSize::Small}, {"medium", BaseSize::Medium}, {"large", BaseSize::Large},
                {"fortress", BaseSize::Fortress}};
            const auto found = bases.find(value);
            if (found == bases.end()) throw std::runtime_error("Unknown skirmish base: " + value);
            p.startSide.base = found->second;
            out.constructionYardOnly = value == "cy";
        } else if (key == "size") p.mapSize = std::stoi(value);
        else if (key == "army") p.startSide.army = std::stoi(value);
        else if (key == "city") p.startSide.city = std::stoi(value);
        else if (key == "seed") p.seed = static_cast<uint32_t>(std::stoul(value));
        else if (key == "tech") p.techLevel = std::stoi(value);
        else if (key == "rock") p.rock = static_cast<Rock>(std::stoi(value));
        else if (key == "spice") p.spice = static_cast<Spice>(std::stoi(value));
        else if (key == "credits") p.credits = std::stoi(value);
        else if (key == "symmetric") p.symmetric = value == "1";
        else throw std::runtime_error("Unknown skirmish option: " + key);
    }
    const Params requested = p;
    p.clamp(getNumCustomGameHouses(), ModManager::instance().isTornieFamilyContentActive());
    if (p.starts != requested.starts || p.mapSize != requested.mapSize || p.credits != requested.credits
        || p.techLevel != requested.techLevel || p.startSide.army != requested.startSide.army
        || p.startSide.city != requested.startSide.city)
        throw std::runtime_error("Skirmish options out of range: " + text);
    return out;
}

/// The generated map as a custom game (houses are added by the caller, as for a map file).
inline GameInitSettings makeSkirmishInit(const Skirmish& skirmish, const SettingsClass::GameOptionsClass& options) {
    using namespace SkirmishGenerator;
    const Result result = generate(skirmish.params, RandomSkirmish::loadCatalogue(skirmish.params.mode));
    int skipped = 0;
    for (const HouseSetup& house : result.houses) skipped += house.skipped;
    SDL_Log("SCENARIO_MAP: name=%s mod=%s city_mode=%d size=%d starts=%d seed=%u map_seed=%u attempts=%d skipped=%d "
            "army=%d/%d cy_only=%d",
            result.name.c_str(), skirmish.params.mod.c_str(), int(skirmish.params.mode == Mode::DuneCity),
            skirmish.params.mapSize, skirmish.params.starts, skirmish.params.seed, result.mapSeed, result.attempts, skipped,
            result.houses.empty() ? 0 : result.houses[0].armySpent, result.houses.empty() ? 0 : result.houses[0].armyBudget,
            int(skirmish.constructionYardOnly));
    return GameInitSettings(result.name, result.ini, false, options);
}

/// Construction Yard start: every MCV deploys where the map put it, before the first cycle, as a
/// player's first click would. An MCV that cannot deploy there stays an MCV (logged).
inline void deployMcvs(Game& game) {
    std::vector<MCV*> mcvs;
    for (auto* unit : unitList)
        if (auto* mcv = dynamic_cast<MCV*>(unit)) mcvs.push_back(mcv);
    int deployed = 0;
    for (MCV* mcv : mcvs) {
        const int house = mcv->getOwner()->getHouseID();
        if (mcv->doDeploy()) ++deployed;
        else SDL_Log("SCENARIO_MCV_NOT_DEPLOYED: house=%d", house);
    }
    SDL_Log("SCENARIO_CY_START: mcvs=%zu deployed=%d", mcvs.size(), deployed);
    (void)game;
}

struct HouseTrack {
    int awayPeak = 0;
    int sortieMinutes = 0;
    Uint32 eliminated = 0;
    /// Where the house starts: its first structure, else its first unit (the map's start the
    /// house was given; the ladder's house/start analysis, docs/ai-scenario-matrix.md).
    Coord start{-1, -1};
};

inline Coord startOf(const House* house) {
    for (const auto* structure : structureList)
        if (structure->getOwner() == house) return structure->getLocation();
    for (const auto* unit : unitList)
        if (unit->getOwner() == house) return unit->getLocation();
    return Coord(-1, -1);
}

/// Fighters of every house away from all of their own structures (once a minute).
inline void measureSorties(const std::vector<const House*>& houses, std::map<int, HouseTrack>& tracks) {
    for (const House* house : houses) {
        std::vector<Coord> homes;
        for (const auto* structure : structureList)
            if (structure->getOwner() == house && structure->getHealth() > 0) homes.push_back(structure->getLocation());
        if (homes.empty()) continue;
        int away = 0;
        for (const auto* unit : unitList) {
            if (unit->getOwner() != house || unit->getHealth() <= 0 || !unit->canAttack() || !unit->isAGroundUnit())
                continue;
            const Coord at = unit->getLocation();
            int nearest = INT_MAX;
            for (const Coord& home : homes)
                nearest = std::min(nearest, std::max(std::abs(home.x - at.x), std::abs(home.y - at.y)));
            if (nearest > kAwayTiles) ++away;
        }
        HouseTrack& track = tracks[house->getHouseID()];
        track.awayPeak = std::max(track.awayPeak, away);
        if (away >= kSortieUnits) ++track.sortieMinutes;
    }
}

inline std::string classesOf(const House* house) {
    std::string classes;
    for (const auto& player : house->getPlayerList())
        classes += (classes.empty() ? "" : "+") + player->getPlayerclass();
    return classes.empty() ? "none" : classes;
}

/// Plays the game to its end (see the file comment). Returns "won"/"lost" for @a trackedHouse's
/// team (the campaign's own result in a campaign), or "time_limit".
inline std::string run(Game& game, Uint32 end, bool campaign, int trackedHouse, bool constructionYardOnly) {
    if (constructionYardOnly) deployMcvs(game);
    const House* tracked = game.getHouse(trackedHouse);
    // The houses that start with something: a campaign roster also names houses its map lacks.
    std::vector<const House*> houses;
    for (const House* house : EliminationMatch::playingHouses(game))
        if (house->isAlive()) houses.push_back(house);
    for (const House* house : houses)
        SDL_Log("SCENARIO_ROSTER: house=%d team=%d players=%s", house->getHouseID(), house->getTeamID(),
                classesOf(house).c_str());
    const Uint32 minute = MILLI2CYCLES(60000);
    const size_t initialTriggers = game.getTriggerManager().getTriggers().size();
    int fired = 0, winner = 0;
    std::map<int, HouseTrack> tracks;
    for (const House* house : houses) tracks[house->getHouseID()].start = startOf(house);
    EliminationMatch::sample(game);
    while (game.getGameCycleCount() < end) {
        ProbeDigest::periodic(game);
        const Uint32 now = game.getGameCycleCount();
        const auto& triggers = game.getTriggerManager().getTriggers();
        for (auto it = triggers.begin(); it != triggers.end() && (*it)->getCycleNumber() == now; ++it) ++fired;
        game.updateGameState();
        EliminationMatch::crushCount.observe();
        const Uint32 cycle = game.getGameCycleCount();
        if (cycle % minute == 0) {
            EliminationMatch::sample(game);
            measureSorties(houses, tracks);
        }
        for (const House* house : houses) {
            HouseTrack& track = tracks[house->getHouseID()];
            if (track.eliminated || house->isAlive()) continue;
            track.eliminated = cycle;
            SDL_Log("SCENARIO_ELIMINATED: house=%d team=%d cycle=%u end_s=%.1f", house->getHouseID(), house->getTeamID(),
                    cycle, FormationWaveMeasure::seconds(cycle));
        }
        if (campaign ? game.finished : (winner = EliminationMatch::survivingTeam(game)) != 0) break;
    }
    EliminationMatch::sample(game);
    const Uint32 cycle = game.getGameCycleCount();
    for (const House* house : houses) {
        int armyValue = 0, armyUnits = 0, structures = 0;
        for (const auto* unit : unitList)
            if (unit->getOwner() == house && unit->getHealth() > 0 && unit->canAttack()) {
                armyValue += currentGame->objectData.data[unit->getItemID()][house->getHouseID()].price;
                ++armyUnits;
            }
        for (const auto* structure : structureList)
            if (structure->getOwner() == house && structure->getHealth() > 0) ++structures;
        const HouseTrack& track = tracks[house->getHouseID()];
        SDL_Log("SCENARIO_HOUSE: house=%d team=%d players=%s alive=%d eliminated_s=%.1f credits=%d kill_value=%d "
                "loss_value=%d structures_destroyed=%d units_destroyed=%d structures=%d army_value=%d army_units=%d "
                "away_peak=%d sortie_minutes=%d start=%d:%d crush_kills=%d",
                house->getHouseID(), house->getTeamID(), classesOf(house).c_str(), int(house->isAlive()),
                track.eliminated ? FormationWaveMeasure::seconds(track.eliminated) : -1.0, house->getCredits(),
                house->getKillValue(), house->getLossValue(), house->getNumDestroyedStructures(),
                house->getNumDestroyedUnits(), structures, armyValue, armyUnits, track.awayPeak, track.sortieMinutes,
                track.start.x, track.start.y, EliminationMatch::crushCount.of(house->getHouseID()));
    }
    std::string outcome;
    int winnerTeam = winner;
    if (campaign) {
        outcome = game.finished ? (game.won ? "won" : "lost") : "time_limit";
        winnerTeam = !game.finished ? 0 : (game.won && tracked ? tracked->getTeamID() : -1);
    } else {
        outcome = winner == 0 ? "time_limit" : (tracked && winner == tracked->getTeamID() ? "won" : "lost");
    }
    const auto* city = game.getCitySimulation();
    SDL_Log("SCENARIO_RESULT: mode=%s outcome=%s winner_team=%d cycle=%u end_s=%.1f minutes=%.2f triggers=%zu fired=%d "
            "win_flags=%d lose_flags=%d quota=%d city_pop=%d",
            campaign ? "campaign" : "match", outcome.c_str(), winnerTeam, cycle, FormationWaveMeasure::seconds(cycle),
            cycle / static_cast<double>(minute), initialTriggers, fired, game.winFlags, game.loseFlags,
            tracked ? tracked->getQuota() : 0, city && game.isCitySimEnabled() ? int(city->getTotalPop()) : 0);
    return outcome;
}

} // namespace ScenarioMatch
