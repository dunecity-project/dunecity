// Included only by campaign-balance-probe.inc (through elimination-match.inc): where each house's
// army stands, read from the live objects (docs/formations.md, "Free-for-all: the army deploys").
//
// Once a game minute (EliminationMatch::sample, so in vanilla-match.py matches and in a continued
// save) every playing house logs FFA_ARMY_SAMPLE: its mobile ground army value (price) inside
// its base (within kBaseTiles of one of its own buildings, city zones and roads' buildings
// included) and outside it, and for a QuantBot the value per role (wave, defence, free), the free
// value standing at the forward bundle and at the perimeter point, units that are stuck (a free
// unit away from its destination that did not move since the last sample), the outnumbered
// verdict with the remembered enemy army summed and per enemy team, and where the bundle and the
// perimeter point lie relative to its own buildings. Read-only; probe-only state.
//
// BALANCE_FFA_DUMP=<prefix> also writes <prefix>-m<minute>.json at the first sample and every
// BALANCE_FFA_DUMP_EVERY minutes (default 5): terrain, building and road tiles per owner, every
// unit and each QuantBot's points, for tests/ai/ffa-map.py to draw. BALANCE_FFA_SCREENSHOT=<png>
// renders the game view (Game::drawScreen at the profile's resolution) centred between house
// BALANCE_FFA_SCREENSHOT_HOUSE's assembly point and its forward bundle at the first sample.
#include <players/QuantBot.h>
#include <structures/StructureBase.h>
#include <units/UnitBase.h>
#include <FileClasses/LoadSavePNG.h>
#include <ScreenBorder.h>
#include <misc/draw_util.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// Defined in src/players/QuantBotArmy.cpp (QuantBotInternal.h is not on the include path).
namespace QuantBotInternal { bool mobileCombatItem(Uint32 item); }

namespace FfaArmy {

/// A unit within this many tiles (Chebyshev) of one of its house's buildings is inside the base:
/// among a city's lots (one-tile roads between 2x2 zones) every tile is; the bundle (at least
/// kBundleClearMinTiles from buildings and roads) and the perimeter point (kPerimeterClearTiles)
/// are not.
constexpr int kBaseTiles = 2;
/// Standing at a point: within this many tiles of it (QuantBot::standsInBundle's radius).
constexpr int kAtPointTiles = 8;

inline bool groundFighter(const UnitBase* unit) {
    return unit->getOwner() && unit->getHealth() > 0 && unit->isActive() && unit->isAGroundUnit()
        && unit->canAttack() && QuantBotInternal::mobileCombatItem(unit->getItemID());
}

inline int price(const UnitBase* unit) {
    return currentGame->objectData.data[unit->getItemID()][unit->getOriginalHouseID()].price;
}

inline int chebyshev(Coord a, Coord b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }

/// Per house: the tiles within kBaseTiles of one of its buildings (row-major, map size).
struct BaseGrid {
    int w = 0, h = 0;
    std::vector<int> nearest;   ///< Chebyshev distance to the house's nearest building tile (capped)
    bool inside(Coord p) const { return p.x >= 0 && p.y >= 0 && p.x < w && p.y < h && nearest[p.y * w + p.x] <= kBaseTiles; }
    int distance(Coord p) const { return p.x >= 0 && p.y >= 0 && p.x < w && p.y < h ? nearest[p.y * w + p.x] : 999; }
};

inline BaseGrid baseGrid(const House* house) {
    BaseGrid grid;
    grid.w = currentGameMap->getSizeX();
    grid.h = currentGameMap->getSizeY();
    constexpr int kCap = 40;
    grid.nearest.assign(size_t(grid.w) * grid.h, kCap);
    for (const auto* structure : structureList) {
        if (structure->getOwner() != house || structure->getHealth() <= 0) continue;
        const Coord at = structure->getLocation(), size = structure->getStructureSize();
        for (int y = std::max(0, at.y - kCap); y < std::min(grid.h, at.y + size.y + kCap); ++y)
            for (int x = std::max(0, at.x - kCap); x < std::min(grid.w, at.x + size.x + kCap); ++x) {
                const int dx = x < at.x ? at.x - x : x >= at.x + size.x ? x - (at.x + size.x - 1) : 0;
                const int dy = y < at.y ? at.y - y : y >= at.y + size.y ? y - (at.y + size.y - 1) : 0;
                int& d = grid.nearest[size_t(y) * grid.w + x];
                d = std::min(d, std::max(dx, dy));
            }
    }
    return grid;
}

inline QuantBot* quantBotOf(const House* house) {
    for (const auto& player : house->getPlayerList())
        if (auto* bot = dynamic_cast<QuantBot*>(player.get())) return bot;
    return nullptr;
}

/// The remembered enemy army (QuantBot::enemySightings) per enemy team, from the units' owners.
inline std::map<int, int64_t> sightingsByTeam(const QuantBot& bot) {
    std::map<int, int64_t> teams;
    for (const auto& [id, sighting] : bot.enemySightings) {
        const auto* unit = dynamic_cast<const UnitBase*>(currentGame->getObjectManager().getObject(id));
        if (unit && unit->getOwner()) teams[unit->getOwner()->getTeamID()] += sighting.value;
    }
    return teams;
}

inline std::map<Uint32, Coord>& lastPositions() { static std::map<Uint32, Coord> positions; return positions; }

inline void dump(Game& game, int minute) {
    const char* prefix = std::getenv("BALANCE_FFA_DUMP");
    if (!prefix) return;
    static int lastDump = -1000;
    const int every = std::getenv("BALANCE_FFA_DUMP_EVERY") ? std::max(1, std::atoi(std::getenv("BALANCE_FFA_DUMP_EVERY"))) : 5;
    if (lastDump >= 0 && minute - lastDump < every) return;
    lastDump = minute;
    const int w = currentGameMap->getSizeX(), h = currentGameMap->getSizeY();
    std::ofstream out(std::string(prefix) + "-m" + std::to_string(minute) + ".json");
    out << "{\"minute\":" << minute << ",\"cycle\":" << game.getGameCycleCount() << ",\"w\":" << w << ",\"h\":" << h
        << ",\"terrain\":[";
    for (int y = 0; y < h; ++y) {
        out << (y ? ",\"" : "\"");
        for (int x = 0; x < w; ++x) {
            const auto* tile = currentGameMap->getTile(x, y);
            out << (tile->isMountain() ? 'M' : tile->isRock() ? 'r' : tile->hasSpice() ? 'c' : 's');
        }
        out << '"';
    }
    out << "],\"roads\":[";
    bool first = true;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (currentGameMap->getTile(x, y)->isRoad()) { out << (first ? "" : ",") << '[' << x << ',' << y << ']'; first = false; }
    out << "],\"structures\":[";
    first = true;
    for (const auto* structure : structureList) {
        if (!structure->getOwner() || structure->getHealth() <= 0) continue;
        out << (first ? "" : ",") << '[' << structure->getOwner()->getHouseID() << ',' << structure->getLocation().x << ','
            << structure->getLocation().y << ',' << structure->getStructureSize().x << ',' << structure->getStructureSize().y << ','
            << structure->getItemID() << ']';
        first = false;
    }
    out << "],\"units\":[";
    first = true;
    for (const auto* unit : unitList) {
        if (!groundFighter(unit)) continue;
        const QuantBot* bot = quantBotOf(unit->getOwner());
        int role = -1;
        if (bot) role = int(bot->unitRole(unit, {QuantBot::UnitRole::Human, QuantBot::UnitRole::Repair,
            QuantBot::UnitRole::Defence, QuantBot::UnitRole::Wave, QuantBot::UnitRole::Escort, QuantBot::UnitRole::ColonyGuard}));
        out << (first ? "" : ",") << '[' << unit->getOwner()->getHouseID() << ',' << unit->getLocation().x << ','
            << unit->getLocation().y << ',' << unit->getItemID() << ',' << role << ',' << int(unit->getAttackMode()) << ','
            << int(unit->isMoving()) << ']';
        first = false;
    }
    out << "],\"houses\":[";
    first = true;
    for (const House* house : EliminationMatch::playingHouses(game)) {
        const QuantBot* bot = quantBotOf(house);
        out << (first ? "" : ",") << "{\"house\":" << house->getHouseID() << ",\"team\":" << house->getTeamID()
            << ",\"alive\":" << int(house->isAlive());
        if (bot) {
            const Coord perimeter = bot->perimeter().point, anchor = bot->armyAssemblyAnchor();
            out << ",\"bundle\":[" << bot->bundlePoint.x << ',' << bot->bundlePoint.y << "],\"perimeter\":[" << perimeter.x << ','
                << perimeter.y << "],\"assembly\":[" << anchor.x << ',' << anchor.y << "],\"protected\":[" << bot->protectedRally.x
                << ',' << bot->protectedRally.y << "],\"garrisoned\":" << int(bot->garrisoned());
        }
        out << '}';
        first = false;
    }
    out << "]}\n";
    SDL_Log("FFA_ARMY_DUMP: minute=%d file=%s-m%d.json", minute, prefix, minute);
}

inline void screenshot(Game& game, int minute) {
    static bool taken = false;
    const char* path = std::getenv("BALANCE_FFA_SCREENSHOT");
    const char* at = std::getenv("BALANCE_FFA_SCREENSHOT_MINUTE");
    if (taken || !path || (at && minute < std::atoi(at))) return;
    taken = true;
    const House* house = game.getHouse(std::atoi(std::getenv("BALANCE_FFA_SCREENSHOT_HOUSE")));
    const QuantBot* bot = house ? quantBotOf(house) : nullptr;
    Coord centre = house ? bot ? bot->armyAssemblyAnchor() : Coord::Invalid() : Coord::Invalid();
    if (bot && centre.isValid() && bot->bundlePoint.isValid())
        centre = Coord((centre.x + bot->bundlePoint.x) / 2, (centre.y + bot->bundlePoint.y) / 2);
    if (centre.isInvalid()) return;
    screenborder->setNewScreenCenter(Coord(centre.x * TILESIZE + TILESIZE / 2, centre.y * TILESIZE + TILESIZE / 2));
    SDL_SetRenderTarget(renderer, nullptr);
    game.drawScreen();
    auto image = renderReadSurface(renderer);
    const bool saved = image != nullptr && SavePNG(image.get(), path) == 0;
    SDL_Log("FFA_ARMY_SCREENSHOT: house=%d centre=%d,%d file=%s saved=%d", house->getHouseID(), centre.x, centre.y, path,
            int(saved));
}

inline void sample(Game& game) {
    const int minute = static_cast<int>(game.getGameCycleCount() / MILLI2CYCLES(60000));
    screenshot(game, minute);
    auto& last = lastPositions();
    std::map<Uint32, Coord> now;
    for (const House* house : EliminationMatch::playingHouses(game)) {
        if (!house->isAlive()) continue;
        const BaseGrid grid = baseGrid(house);
        QuantBot* bot = quantBotOf(house);
        int64_t total = 0, inside = 0, wave = 0, defence = 0, freeValue = 0, atBundle = 0, atPerimeter = 0, freeInside = 0,
                stuck = 0, onRoad = 0;
        int units = 0, stuckUnits = 0;
        const Coord bundle = bot ? bot->bundlePoint : Coord::Invalid();
        const Coord perimeter = bot ? bot->perimeter().point : Coord::Invalid();
        for (const auto* unit : unitList) {
            if (unit->getOwner() != house || !groundFighter(unit)) continue;
            const int value = price(unit);
            const Coord at = unit->getLocation();
            now[unit->getObjectID()] = at;
            total += value; ++units;
            const bool in = grid.inside(at);
            if (in) inside += value;
            if (currentGameMap->getTile(at)->isRoad()) onRoad += value;
            if (!bot) continue;
            const auto role = bot->unitRole(unit, {QuantBot::UnitRole::Human, QuantBot::UnitRole::Repair, QuantBot::UnitRole::Defence,
                                                   QuantBot::UnitRole::Wave, QuantBot::UnitRole::Escort, QuantBot::UnitRole::ColonyGuard});
            if (role == QuantBot::UnitRole::Wave) { wave += value; continue; }
            if (role == QuantBot::UnitRole::Defence) { defence += value; continue; }
            if (role != QuantBot::UnitRole::Free) continue;
            freeValue += value;
            if (in) freeInside += value;
            if (bundle.isValid() && chebyshev(at, bundle) <= kAtPointTiles) atBundle += value;
            if (perimeter.isValid() && chebyshev(at, perimeter) <= kAtPointTiles) atPerimeter += value;
            const auto previous = last.find(unit->getObjectID());
            const Coord destination = unit->getDestination();
            if (previous != last.end() && previous->second == at && !unit->isMoving() && !unit->hasATarget()
                && destination.isValid() && chebyshev(destination, at) > 2) { stuck += value; ++stuckUnits; }
        }
        std::string extra;
        if (bot) {
            const auto teams = sightingsByTeam(*bot);
            int64_t strongest = 0, sum = 0;
            std::string perTeam;
            for (const auto& [team, value] : teams) {
                strongest = std::max(strongest, value);
                sum += value;
                perTeam += (perTeam.empty() ? "" : "/") + std::to_string(team) + ":" + std::to_string(value);
            }
            char buffer[640];
            std::snprintf(buffer, sizeof(buffer),
                " own_estimate=%lld enemy_sum=%lld enemy_strongest=%lld enemy_teams=%s outnumbered=%d garrisoned=%d "
                "bundle_ready=%d held_home=%d bundle=%d,%d bundle_base_tiles=%d bundle_road=%d perimeter=%d,%d "
                "perimeter_base_tiles=%d garrison_holds=%d bundle_orders=%d gate_holds=%d",
                (long long)bot->ownGroundArmyValue(), (long long)sum, (long long)strongest,
                perTeam.empty() ? "-" : perTeam.c_str(), int(bot->outnumberedByEstimate()), int(bot->garrisoned()),
                int(bot->bundleArmyReady()), int(bot->reserveHeldHome()), bundle.x, bundle.y,
                bundle.isValid() ? grid.distance(bundle) : -1,
                bundle.isValid() && currentGameMap->tileExists(bundle) ? int(currentGameMap->getTile(bundle)->isRoad()) : -1,
                perimeter.x, perimeter.y, perimeter.isValid() ? grid.distance(perimeter) : -1, bot->forceStats.garrisonHolds,
                bot->forceStats.bundleOrders, bot->forceStats.gateHolds);
            extra = buffer;
        }
        SDL_Log("FFA_ARMY_SAMPLE: minute=%d cycle=%u house=%d team=%d units=%d army_value=%lld inside_value=%lld "
                "outside_value=%lld on_road_value=%lld wave_value=%lld defence_value=%lld free_value=%lld free_inside_value=%lld "
                "at_bundle_value=%lld at_perimeter_value=%lld stuck_value=%lld stuck_units=%d%s",
                minute, game.getGameCycleCount(), house->getHouseID(), house->getTeamID(), units, (long long)total,
                (long long)inside, (long long)(total - inside), (long long)onRoad, (long long)wave, (long long)defence,
                (long long)freeValue, (long long)freeInside, (long long)atBundle, (long long)atPerimeter, (long long)stuck,
                stuckUnits, extra.c_str());
    }
    last.swap(now);
    dump(game, minute);
}

} // namespace FfaArmy
