// Land-value audit of a running city match (tests/ai/land-value-report.py; Stefan, 2026-11-27:
// "there's still lots of low value see mba save low"). Read-only: it plays the match on and,
// at the start and every BALANCE_LAND_VALUE_AUDIT_EVERY game minutes, logs one
// LAND_VALUE_AUDIT line per city house with its R/C lot land value and what holds it down.
//
// A lot's land value is Micropolis's (CityEffects.h computeBaseLandValue): a distance term
// (34 - d/2) * 4 with d the Manhattan distance to its developed cluster's zone centre (clamped
// to 64), plus park terrain and sand, minus pollution and 20 for crime above 190, plus the
// Palace/Stadium stamp (+40 falling off over 8 tiles). The audit recomputes the distance term
// from the same clusters and reads every other input from the live maps, so the parts it
// prints are the engine's, not a model of them.
#include <dunecity/CityEffects.h>
#include <dunecity/CitySimulation.h>
#include <structures/StructureBase.h>
#include <players/QuantBot.h>
#include <map>

namespace LandValueAudit {
struct Lot { int x, y, lv, pollution, crime, park, distanceTerm, palace, polluterTiles; Uint32 item; };

inline bool landmark(Uint32 item) { return item == Structure_Palace || item == Structure_Stadium; }

// Developed clusters and their zone centres, as runEffectsScans() builds them (micropolis rules).
inline std::vector<int> distanceTerms(int bs, int blocksW, int blocksH) {
    const int w = currentGameMap->getSizeX(), h = currentGameMap->getSizeY();
    std::vector<uint8_t> developed(size_t(blocksW) * blocksH, 0);
    std::vector<int> owner(size_t(w) * h, -1);
    for (const auto* s : structureList) {
        const Coord o = s->getLocation(), size = s->getStructureSize();
        for (int y = o.y; y < o.y + size.y; ++y) for (int x = o.x; x < o.x + size.x; ++x)
            if (x >= 0 && y >= 0 && x < w && y < h) owner[size_t(y) * w + x] = 1;
    }
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        if (currentGameMap->getTile(x, y)->isRoad() || owner[size_t(y) * w + x] >= 0)
            developed[size_t(y / bs) * blocksW + x / bs] = 1;
    std::vector<int> cluster(developed.size(), -1);
    std::vector<long long> sx, sy, n;
    for (int by = 0; by < blocksH; ++by) for (int bx = 0; bx < blocksW; ++bx) {
        if (!developed[size_t(by) * blocksW + bx] || cluster[size_t(by) * blocksW + bx] >= 0) continue;
        const int id = int(sx.size()); sx.push_back(0); sy.push_back(0); n.push_back(0);
        std::vector<std::pair<int,int>> stack{{bx, by}}; cluster[size_t(by) * blocksW + bx] = id;
        while (!stack.empty()) {
            auto [qx, qy] = stack.back(); stack.pop_back();
            sx[id] += qx * bs; sy[id] += qy * bs; ++n[id];
            const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int d = 0; d < 4; ++d) {
                const int nx = qx + dx[d], ny = qy + dy[d];
                if (nx < 0 || ny < 0 || nx >= blocksW || ny >= blocksH) continue;
                const size_t i = size_t(ny) * blocksW + nx;
                if (developed[i] && cluster[i] < 0) { cluster[i] = id; stack.push_back({nx, ny}); }
            }
        }
    }
    std::vector<long long> zx(sx.size(), 0), zy(sx.size(), 0), zn(sx.size(), 0);
    for (const auto* s : structureList) {
        if (s->getItemID() == Structure_Wall || s->getItemID() == Structure_Park) continue;
        const Coord o = s->getLocation(), size = s->getStructureSize();
        const int cx = std::min(w - 1, o.x + size.x / 2), cy = std::min(h - 1, o.y + size.y / 2);
        const int id = cluster[size_t(cy / bs) * blocksW + cx / bs];
        if (id < 0) continue;
        zx[id] += cx; zy[id] += cy; ++zn[id];
    }
    std::vector<int> term(developed.size(), 0);
    for (int by = 0; by < blocksH; ++by) for (int bx = 0; bx < blocksW; ++bx) {
        const int id = cluster[size_t(by) * blocksW + bx];
        if (id < 0) continue;
        const long long cx = zn[id] ? zx[id] / zn[id] : sx[id] / n[id], cy = zn[id] ? zy[id] / zn[id] : sy[id] / n[id];
        const int d = std::min<int>(DuneCity::kLandValueDistanceClamp, int(std::abs(bx * bs - cx) + std::abs(by * bs - cy)));
        term[size_t(by) * blocksW + bx] = (34 - d / 2) * 4;
    }
    return term;
}

inline void report(Game& game, const char* phase) {
    auto* sim = game.getCitySimulation();
    if (!sim || !game.isCitySimEnabled()) return;
    const auto& lv = sim->getLandValueMap();
    const int bs = std::max(1, lv.getBlockSize());
    const int w = currentGameMap->getSizeX(), h = currentGameMap->getSizeY();
    const int blocksW = (w + bs - 1) / bs, blocksH = (h + bs - 1) / bs;
    const auto term = distanceTerms(bs, blocksW, blocksH);
    const double minute = game.getGameCycleCount() / double(MILLI2CYCLES(60000));
    for (int hid = 0; hid < NUM_HOUSES; ++hid) {
        const House* house = game.getHouse(hid);
        if (!house) continue;
        std::vector<Lot> lots;
        std::vector<Coord> landmarks, polluters;
        std::map<std::string, int> items;
        for (const auto* s : structureList) {
            const Uint32 item = s->getItemID();
            if (s->getOwner() != house) continue;
            items[getItemNameByID(item)]++;
            if (landmark(item)) landmarks.push_back(s->getLocation() + Coord(1, 1));
        }
        for (const auto* s : structureList) {
            const Uint32 item = s->getItemID();
            if (item != Structure_ZoneResidential && item != Structure_ZoneCommercial) continue;
            if (s->getOwner() != house) continue;
            const Coord o = s->getLocation();
            const int bx = o.x / bs, by = o.y / bs;
            Lot lot{o.x, o.y, lv.get(bx, by), sim->getPollutionDensityMap().get(bx, by),
                    int(sim->getCrimeRateMap().get(bx, by)),
                    sim->getParkTerrain().landValueContribution(bx * bs, by * bs, bs),
                    term[size_t(by) * blocksW + bx], 0, 0, item};
            for (const Coord c : landmarks)
                lot.palace = std::max(lot.palace, DuneCity::falloff(DuneCity::kStadiumLandValueBonus,
                    std::max(std::abs(c.x - o.x), std::abs(c.y - o.y)), 8));
            lots.push_back(lot);
        }
        // Polluting buildings (Industrial zones and factories) within 5 tiles of each lot.
        for (const auto* s : structureList) {
            if (s->getOwner() != house) continue;
            const Uint32 item = s->getItemID();
            const int level = DuneCity::isCityZoneStructure(item) ? currentGameMap->getTile(s->getLocation())->getCityZoneDensity()
                                                                  : DuneCity::getStructureMaxLevel(item);
            if (DuneCity::getPollutionEmission(item, std::max(1, level)) <= 0) continue;
            polluters.push_back(s->getLocation());
        }
        for (auto& lot : lots)
            for (const Coord p : polluters)
                lot.polluterTiles += std::max(std::abs(p.x - lot.x), std::abs(p.y - lot.y)) <= DuneCity::kPollutionRadius;
        // Road tiles of this house and those in a 2x2 all-road square (a two-lane strip).
        int roads = 0, doubled = 0;
        // As QuantBot::findLaneParkSite: the house's road tiles, or unowned road beside its buildings.
        auto ownRoad = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= w || y >= h) return false;
            const Tile* tile = currentGameMap->getTile(x, y);
            if (!tile->isRoad()) return false;
            if (tile->getOwner() == hid) return true;
            if (tile->getOwner() >= 0) return false;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                if (x + dx < 0 || y + dy < 0 || x + dx >= w || y + dy >= h) continue;
                const auto* o = currentGameMap->getTile(x + dx, y + dy)->getNonInfantryGroundObject();
                if (o && o->isAStructure() && o->getOwner() == house) return true;
            }
            return false;
        };
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            if (!ownRoad(x, y)) continue;
            ++roads;
            bool square = false;
            for (int oy = -1; oy <= 0 && !square; ++oy) for (int ox = -1; ox <= 0 && !square; ++ox)
                square = ownRoad(x + ox, y + oy) && ownRoad(x + ox + 1, y + oy) && ownRoad(x + ox, y + oy + 1)
                    && ownRoad(x + ox + 1, y + oy + 1);
            doubled += square;
        }
        if (lots.empty() && roads == 0) continue;
        int buckets[4] = {0, 0, 0, 0};
        long long sumLv = 0;
        int low = 0, lowPolluted = 0, lowCrime = 0, lowNoPalace = 0, lowFar = 0, lowNoPark = 0;
        long long lowPollution = 0, lowTerm = 0, lowPark = 0, lowPalace = 0;
        for (const auto& lot : lots) {
            sumLv += lot.lv;
            buckets[lot.lv <= 40 ? 0 : lot.lv <= 80 ? 1 : lot.lv <= 120 ? 2 : 3]++;
            if (lot.lv > 80) continue;
            ++low; lowPollution += lot.pollution; lowTerm += lot.distanceTerm; lowPark += lot.park; lowPalace += lot.palace;
            lowPolluted += lot.pollution >= 20; lowCrime += lot.crime > DuneCity::kCrimeLandValueThreshold;
            lowNoPalace += lot.palace == 0; lowFar += lot.distanceTerm <= 40; lowNoPark += lot.park == 0;
        }
        const auto& state = sim->getHouseState(hid);
        std::ostringstream items_json;
        bool first = true;
        for (const char* name : {"Residential Zone", "Commercial Zone", "Industrial Zone", "Park", "Palace", "Stadium",
                                 "Airport", "Light Factory", "Heavy Factory", "High-Tech Factory", "Refinery", "Repair Yard",
                                 "Police Station", "Starport"}) {
            const auto it = items.find(name);
            items_json << (first ? "" : ",") << "\"" << name << "\":" << (it == items.end() ? 0 : it->second);
            first = false;
        }
        const int n = std::max(1, int(lots.size())), l = std::max(1, low);
        SDL_Log("LAND_VALUE_AUDIT: {\"phase\":\"%s\",\"minute\":%.2f,\"house\":%d,\"credits\":%d,\"avg_land_value\":%d,"
                "\"pop_r\":%d,\"pop_c\":%d,\"pop_i\":%d,\"lots\":%d,\"lot_lv_mean\":%lld,\"lv_le40\":%d,\"lv_41_80\":%d,"
                "\"lv_81_120\":%d,\"lv_gt120\":%d,\"low\":%d,\"low_pollution_mean\":%lld,\"low_polluted20\":%d,"
                "\"low_crime190\":%d,\"low_distance_term_mean\":%lld,\"low_distance_le40\":%d,\"low_park_mean\":%lld,"
                "\"low_no_park\":%d,\"low_palace_mean\":%lld,\"low_no_palace\":%d,\"low_polluter_tiles_mean_x10\":%lld,"
                "\"roads\":%d,\"roads_in_2x2\":%d,\"items\":{%s}}",
            phase, minute, hid, house->getCredits(), state.avgLandValue, state.resPop, state.comPop, state.indPop,
            int(lots.size()), sumLv / n, buckets[0], buckets[1], buckets[2], buckets[3], low, lowPollution / l, lowPolluted,
            lowCrime, lowTerm / l, lowFar, lowPark / l, lowNoPark, lowPalace / l, lowNoPalace,
            [&] { long long t = 0; for (const auto& lot : lots) if (lot.lv <= 80) t += lot.polluterTiles; return t * 10 / l; }(),
            roads, doubled, items_json.str().c_str());
    }
}

inline void run(Game& game, Uint32 end) {
    // A continuation of a save runs this many minutes past its own clock (land-value-continuation.py).
    if (const char* extra = std::getenv("BALANCE_LAND_VALUE_AUDIT_EXTRA"))
        end = game.getGameCycleCount() + MILLI2CYCLES(60000) * Uint32(std::max(1, std::atoi(extra)));
    // The rule-off arm: match rule Parks Count As Roads off for the continuation.
    if (const char* rule = std::getenv("BALANCE_PARKS_COUNT_AS_ROADS"))
        const_cast<SettingsClass::GameOptionsClass&>(game.getGameInitSettings().getGameOptions()).parksCountAsRoads
            = std::atoi(rule) != 0;
    SDL_Log("LAND_VALUE_AUDIT_SETUP: parks_count_as_roads=%d end_cycle=%u",
            int(game.getGameInitSettings().getGameOptions().parksCountAsRoads), end);
    const char* every = std::getenv("BALANCE_LAND_VALUE_AUDIT_EVERY");
    const Uint32 step = MILLI2CYCLES(60000) * Uint32(std::max(1, every ? std::atoi(every) : 5));
    report(game, "loaded");
    Uint32 next = game.getGameCycleCount() + step;
    while (game.getGameCycleCount() < end && !game.finished) {
        game.updateGameState();
        if (game.getGameCycleCount() >= next) { report(game, "periodic"); next += step; }
    }
    report(game, "end");
}
} // namespace LandValueAudit
