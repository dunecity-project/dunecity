// Read-only audit of a loaded save, per house (tests/ai/save-house-audit.py; Stefan, 2026-11-27:
// "i saved a game seewhatidid ... where i added more palaces to improve land, added rocket turrets as a
// maginot and built a lot of launchers. and my ai is doing better than its opponents ... analyse the save").
//
// One SAVE_HOUSE_AUDIT JSON line per house: its players, credits, city state, alive units and structures
// by type, each Palace/Stadium with its own R/C lots in reach and their mean land value, and its Rocket
// Turrets measured against the enemy (nearest enemy structure and ground unit, own base centre). Then the
// house's UNIT_ECONOMY combat record as saved (kills and damage value per type).
#include <dunecity/CityEffects.h>
#include <dunecity/CitySimulation.h>
#include <structures/StructureBase.h>
#include <structures/ZoneStructure.h>
#include <units/UnitBase.h>
#include <players/HumanPlayer.h>
#include <map>
#include <sstream>

namespace SaveHouseAudit {
inline int cheb(Coord a, Coord b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }

inline std::string jsonCounts(const std::map<std::string, int>& counts) {
    std::ostringstream out;
    bool first = true;
    for (const auto& [name, n] : counts) { out << (first ? "" : ",") << "\"" << name << "\":" << n; first = false; }
    return out.str();
}

inline void report(Game& game, const char* phase) {
    auto* sim = game.getCitySimulation();
    const int bs = sim ? std::max(1, sim->getLandValueMap().getBlockSize()) : 1;
    const double minute = game.getGameCycleCount() / double(MILLI2CYCLES(60000));
    for (int hid = 0; hid < NUM_HOUSES; ++hid) {
        House* house = game.getHouse(hid);
        if (!house) continue;
        std::ostringstream players;
        bool first = true;
        for (const auto& p : house->getPlayerList()) {
            players << (first ? "" : ",") << "{\"name\":\"" << p->getPlayername() << "\",\"class\":\"" << p->getPlayerclass()
                    << "\",\"ai\":" << (dynamic_cast<const HumanPlayer*>(p.get()) ? 0 : 1) << "}";
            first = false;
        }
        std::map<std::string, int> units, structures;
        std::vector<Coord> enemy, enemyUnits, palaces, turrets, lots;
        std::vector<int> lotValues;
        long long sx = 0, sy = 0, sn = 0;
        int garrisonedZones = 0, garrisonedUnits = 0;
        for (const auto* s : structureList) {
            const Coord c = s->getLocation() + Coord(s->getStructureSize().x / 2, s->getStructureSize().y / 2);
            const Uint32 item = s->getItemID();
            if (s->getOwner() == house) {
                structures[getItemNameByID(item)]++;
                if (item == Structure_ZoneResidential || item == Structure_ZoneCommercial || item == Structure_ZoneIndustrial) {
                    const int inside = static_cast<const ZoneStructure*>(s)->getGarrisonSize();
                    garrisonedZones += inside > 0;
                    garrisonedUnits += inside;
                }
                if (item != Structure_RocketTurret && item != Structure_GunTurret && item != Structure_Wall) { sx += c.x; sy += c.y; ++sn; }
                if (item == Structure_Palace || item == Structure_Stadium) palaces.push_back(s->getLocation() + Coord(1, 1));
                if (item == Structure_RocketTurret) turrets.push_back(c);
                if (sim && (item == Structure_ZoneResidential || item == Structure_ZoneCommercial)) {
                    lots.push_back(s->getLocation());
                    lotValues.push_back(sim->getLandValueMap().get(s->getLocation().x / bs, s->getLocation().y / bs));
                }
            } else if (s->getOwner() && s->getOwner()->getTeamID() != house->getTeamID()) {
                enemy.push_back(c);
            }
        }
        int combat = 0;
        for (const auto* u : unitList) {
            const Uint32 i = u->getItemID();
            const bool support = i == Unit_Harvester || i == Unit_MCV || i == Unit_Carryall || i == Unit_Frigate;
            if (u->getOwner() == house) {
                units[getItemNameByID(i)]++;
                if (!support && !u->isAFlyingUnit()) ++combat;
            } else if (u->getOwner() && u->getOwner()->getTeamID() != house->getTeamID() && !support && !u->isAFlyingUnit()) {
                enemyUnits.push_back(u->getLocation());
            }
        }
        const Coord centre = sn ? Coord(int(sx / sn), int(sy / sn)) : Coord(0, 0);
        std::ostringstream pal;
        first = true;
        for (const Coord p : palaces) {
            int n = 0, nearestEnemy = 9999; long long sum = 0;
            for (size_t i = 0; i < lots.size(); ++i) if (cheb(lots[i], p) <= 8) { ++n; sum += lotValues[i]; }
            for (const Coord e : enemy) nearestEnemy = std::min(nearestEnemy, cheb(e, p));
            pal << (first ? "" : ",") << "[" << p.x << "," << p.y << "," << n << "," << (n ? sum / n : 0) << "," << cheb(p, centre)
                << "," << nearestEnemy << "]";
            first = false;
        }
        int bands[5] = {0, 0, 0, 0, 0}; // nearest enemy structure <=8, 9-16, 17-32, 33-64, >64 tiles
        int unitClose = 0;              // an enemy ground unit within the turret's 8-tile range
        long long dCentre = 0, dEnemy = 0;
        std::ostringstream tur;
        first = true;
        for (const Coord t : turrets) {
            int de = 9999, du = 9999;
            for (const Coord e : enemy) de = std::min(de, cheb(e, t));
            for (const Coord e : enemyUnits) du = std::min(du, cheb(e, t));
            bands[de <= 8 ? 0 : de <= 16 ? 1 : de <= 32 ? 2 : de <= 64 ? 3 : 4]++;
            unitClose += du <= 8;
            dCentre += cheb(t, centre); dEnemy += std::min(de, 999);
            tur << (first ? "" : ",") << "[" << t.x << "," << t.y << "," << de << "," << du << "]";
            first = false;
        }
        int low = 0, lowInReach = 0; long long lvSum = 0;
        for (size_t i = 0; i < lots.size(); ++i) {
            lvSum += lotValues[i];
            if (lotValues[i] > 80) continue;
            ++low;
            for (const Coord p : palaces) if (cheb(lots[i], p) <= 8) { ++lowInReach; break; }
        }
        const int nt = std::max(1, int(turrets.size()));
        const auto* state = sim ? &sim->getHouseState(hid) : nullptr;
        SDL_Log("SAVE_HOUSE_AUDIT: {\"phase\":\"%s\",\"minute\":%.2f,\"house\":%d,\"team\":%d,\"players\":[%s],\"credits\":%d,"
                "\"pop_r\":%d,\"pop_c\":%d,\"pop_i\":%d,\"avg_land_value\":%d,\"tax_rate\":%d,\"lots\":%d,\"lot_lv_mean\":%lld,"
                "\"low_lots\":%d,\"low_lots_in_landmark_reach\":%d,\"centre\":[%d,%d],\"combat_units\":%d,"
                "\"garrisoning_and_capturing\":%d,\"garrisoned_zones\":%d,\"garrisoned_units\":%d,"
                "\"rocket_turrets\":%d,\"turret_enemy_bands\":[%d,%d,%d,%d,%d],\"turrets_enemy_unit_in_range\":%d,"
                "\"turret_mean_dist_centre\":%lld,\"turret_mean_dist_enemy\":%lld,\"landmarks\":[%s],\"turrets\":[%s],"
                "\"units\":{%s},\"structures\":{%s}}",
                phase, minute, hid, house->getTeamID(), players.str().c_str(), house->getCredits(),
                state ? state->resPop : 0, state ? state->comPop : 0, state ? state->indPop : 0,
                state ? state->avgLandValue : 0, state ? int(state->taxRate) : 0, int(lots.size()),
                lots.empty() ? 0LL : lvSum / (long long)lots.size(), low, lowInReach, centre.x, centre.y, combat,
                int(game.getGameInitSettings().getGameOptions().garrisoningAndCapturing), garrisonedZones, garrisonedUnits,
                int(turrets.size()), bands[0], bands[1], bands[2], bands[3], bands[4], unitClose, dCentre / nt, dEnemy / nt,
                pal.str().c_str(), tur.str().c_str(), jsonCounts(units).c_str(), jsonCounts(structures).c_str());
        house->logUnitEconomy(phase);
    }
}

inline void run(Game& game) {
    report(game, "loaded");
    // Play on with the AI (BALANCE_SAVE_HOUSE_AUDIT_EXTRA minutes) and audit every 10 minutes, at each of
    // BALANCE_SAVE_HOUSE_AUDIT_AT (minutes after the load, "23,48") and at the end.
    if (const char* extra = std::getenv("BALANCE_SAVE_HOUSE_AUDIT_EXTRA")) {
        const Uint32 start = game.getGameCycleCount(), minute = MILLI2CYCLES(60000);
        const Uint32 end = start + minute * Uint32(std::max(0, std::atoi(extra)));
        std::vector<Uint32> at;
        if (const char* list = std::getenv("BALANCE_SAVE_HOUSE_AUDIT_AT")) {
            std::istringstream input(list);
            std::string item;
            while (std::getline(input, item, ',')) if (!item.empty()) at.push_back(start + minute * Uint32(std::stoi(item)));
        }
        Uint32 next = start + minute * 10;
        size_t nextAt = 0;
        while (game.getGameCycleCount() < end && !game.finished) {
            game.updateGameState();
            if (game.getGameCycleCount() >= next) { report(game, "periodic"); next += minute * 10; }
            if (nextAt < at.size() && game.getGameCycleCount() >= at[nextAt]) { report(game, "at"); ++nextAt; }
        }
        report(game, "end");
    }
    SDL_Log("CAMPAIGN_BALANCE_RESULT: level=1 seed=%u attack_percent=100 partner=hard enemy=hard cycle=%u outcome=save_house_audit",
            game.getGameInitSettings().getRandomSeed(), game.getGameCycleCount());
}
} // namespace SaveHouseAudit
