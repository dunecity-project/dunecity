// Included only by campaign-balance-probe.inc (run-campaign-balance.py --formation-measure).
//
// Measures how a QuantBot attack wave travels and fights in a real AI-versus-AI skirmish
// (docs/formations.md, QuantBot waves). Per wave:
//   - contact: the first cycle any member engages - it has an enemy target within
//     max(weapon, view) range, fires, or enters the enemy base area;
//   - engagement spread: first to last member engaging (members that engaged at all);
//   - arrival spread: first to last member reaching the enemy base area (the older metric);
//   - shots fired by members in the first 30 s of contact;
//   - value lost and value killed (enemies destroyed within 10 tiles of a living member) in
//     the first 60 s of contact;
//   - members lost before contact, members alive at the end of the five-minute wave window;
//   - for formation waves: members in formation at dispatch and at contact, and how long
//     the form-up and the formation lasted.
// Every five game minutes it also reports each house's live army value, and at the end the
// match state. The same seed, map and roster give the identical match up to the first
// dispatch, so running this with two wave settings compares how the two move a wave.
// Read-only: it never issues an order or changes simulation state.
#include <players/QuantBot.h>
#include <units/UnitBase.h>
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace FormationWaveMeasure {

/// The enemy base area: this close to any enemy building. Members already this close when
/// the wave is dispatched are not travelling and are left out of the wave.
constexpr int kTargetAreaTiles = 10;
/// A wave is evaluated over this long after dispatch.
constexpr Uint32 kWaveWindowCycles = MILLI2CYCLES(5 * 60 * 1000);
/// Shots are counted over this long after the wave's contact.
constexpr Uint32 kContactWindowCycles = MILLI2CYCLES(30 * 1000);
/// Losses and kills are counted over this long after the wave's contact.
constexpr Uint32 kTradeWindowCycles = MILLI2CYCLES(60 * 1000);
/// An enemy destroyed this close to a living member during the trade window is the wave's kill.
constexpr int kKillCreditTiles = 10;
/// Army value samples, every this many game minutes.
constexpr int kSampleMinutes = 5;
/// Members "together" at contact: within this many tiles of the member nearest their centre.
constexpr int kTogetherTiles = 8;
/// Members "arriving together": engaged within this long of the wave's contact.
constexpr Uint32 kTogetherWindowCycles = MILLI2CYCLES(10 * 1000);
/// Defence trade (docs/formations.md, "Concentrating force"): a unit destroyed this close to a
/// building of its own house is a home loss for that house; this close to a building of another
/// team it is a home kill for that building's house. Sampled every kHomeSampleCycles.
constexpr int kHomeTiles = 12;
constexpr Uint32 kHomeSampleCycles = 16;
struct HomeTrade { int kill = 0, loss = 0; };
/// Per house id, for FormationDuel::report.
inline std::map<int, HomeTrade>& homeTrade() { static std::map<int, HomeTrade> trade; return trade; }

struct Wave {
    int house = -1;
    Uint32 launched = 0;
    std::vector<Uint32> members;
    std::map<Uint32, int> value;        ///< member -> price at dispatch
    std::map<Uint32, Uint32> engaged;   ///< member -> first cycle it engaged
    std::map<Uint32, Uint32> arrival;   ///< member -> first cycle inside the enemy base area
    std::map<Uint32, Uint32> died;      ///< member -> cycle it was found destroyed
    Uint32 contact = 0;                 ///< first engagement of any member; 0 = none yet
    int shots = 0;                      ///< primary shots of members in the contact window
    int formed = 0;                     ///< members in a formation at dispatch
    bool sawForming = false;            ///< some member was forming up at some point
    Uint32 formUpEnd = 0;               ///< first cycle no member was forming up any more
    Uint32 formationEnd = 0;            ///< first cycle no member was in a formation
    int inFormationAtContact = -1;      ///< members in formation on the contact cycle
    int inFormation10s = -1;            ///< members in formation 10 s after the contact
    int togetherAtContact = -1;         ///< living members within kTogetherTiles of the anchor at contact
    int lossValue = 0;                  ///< member value destroyed in the trade window
    int killValue = 0;                  ///< enemy value destroyed near the wave in the trade window
    int survivors = -1;                 ///< members alive at the end of the wave window
    bool reported = false;
};

struct Known {
    Coord location;
    int value = 0;
    int team = -1;
};

inline double seconds(Uint32 cycles) { return cycles * GAMESPEED_DEFAULT / 1000.0; }

inline int priceOf(const ObjectBase* object) {
    return std::max(0, currentGame->objectData.data[object->getItemID()][object->getOriginalHouseID()].price);
}

inline double since(Uint32 cycle, Uint32 launched) { return cycle ? seconds(cycle - launched) : -1.0; }

inline double spreadOf(const std::map<Uint32, Uint32>& times) {
    if (times.empty()) return 0.0;
    Uint32 first = UINT32_MAX, last = 0;
    for (const auto& [id, cycle] : times) { first = std::min(first, cycle); last = std::max(last, cycle); }
    return seconds(last - first);
}

inline void report(const Wave& wave) {
    int lostBeforeContact = 0;
    for (const auto& [id, cycle] : wave.died) if (wave.contact == 0 || cycle < wave.contact) ++lostBeforeContact;
    int engaged10 = 0;
    for (const auto& [id, cycle] : wave.engaged) if (wave.contact != 0 && cycle <= wave.contact + kTogetherWindowCycles) ++engaged10;
    SDL_Log("FORMATION_WAVE: house=%d launched=%u members=%zu engaged=%zu arrived=%zu lost=%zu "
            "contact_s=%.1f engage_spread_s=%.1f arrival_spread_s=%.1f shots30=%d loss60=%d kill60=%d "
            "survivors=%d lost_before_contact=%d formed=%d formup_s=%.1f formation_end_s=%.1f "
            "in_formation_at_contact=%d in_formation_10s=%d together_at_contact=%d engaged10=%d",
            wave.house, wave.launched, wave.members.size(), wave.engaged.size(), wave.arrival.size(),
            wave.died.size(), since(wave.contact, wave.launched), spreadOf(wave.engaged), spreadOf(wave.arrival),
            wave.shots, wave.lossValue, wave.killValue, wave.survivors, lostBeforeContact, wave.formed,
            wave.formed && wave.sawForming ? since(wave.formUpEnd, wave.launched) : -1.0,
            wave.formed ? since(wave.formationEnd, wave.launched) : -1.0, wave.inFormationAtContact, wave.inFormation10s,
            wave.togetherAtContact, wave.contact != 0 ? engaged10 : -1);
}

/// A live building's footprint, collected once per cycle for inTargetArea.
struct Footprint {
    int team;
    Coord first, last;   ///< inclusive corner tiles
};

inline std::vector<Footprint> liveFootprints() {
    std::vector<Footprint> footprints;
    footprints.reserve(structureList.size());
    for (const auto* structure : structureList) {
        if (structure->getHealth() <= 0) continue;
        const Coord location = structure->getLocation();
        footprints.push_back({structure->getOwner()->getTeamID(), location,
                              location + structure->getStructureSize() - Coord(1, 1)});
    }
    return footprints;
}

/// Is this unit inside the enemy base area: within kTargetAreaTiles of a live enemy building?
/// The building's closest tile is its footprint clamped to the unit (StructureBase::getClosestPoint).
/// blockDistance is at least the larger axis offset, so a building more than kTargetAreaTiles
/// away along either axis can never round to within reach and is skipped without the exact test.
inline bool inTargetArea(const UnitBase* unit, int myTeam, const std::vector<Footprint>& footprints) {
    const Coord at = unit->getLocation();
    for (const Footprint& footprint : footprints) {
        if (footprint.team == myTeam) continue;
        const Coord closest(std::clamp(at.x, footprint.first.x, footprint.last.x),
                            std::clamp(at.y, footprint.first.y, footprint.last.y));
        if (std::abs(at.x - closest.x) > kTargetAreaTiles || std::abs(at.y - closest.y) > kTargetAreaTiles) continue;
        if (blockDistance(at, closest).lround() <= kTargetAreaTiles) return true;
    }
    return false;
}

/// Is this unit engaging: an enemy target within max(weapon, view) range?
inline bool engaging(const UnitBase* unit, int myTeam) {
    const auto* target = unit->getTarget();
    return target && target->getOwner() && target->getOwner()->getTeamID() != myTeam
        && blockDistance(unit->getLocation(), target->getClosestPoint(unit->getLocation())).lround()
               <= unit->huntTowardRange();
}

inline void sampleHouses(Game& game, int minute) {
    for (int h = 0; h < NUM_HOUSES; ++h) {
        const House* house = game.getHouse(h);
        if (!house) continue;
        int army = 0, units = 0, structures = 0;
        for (const auto* unit : unitList)
            if (unit->getOwner() == house && unit->getHealth() > 0 && unit->canAttack()) { army += priceOf(unit); ++units; }
        for (const auto* structure : structureList)
            if (structure->getOwner() == house && structure->getHealth() > 0) ++structures;
        SDL_Log("FORMATION_HOUSE: house=%d minute=%d army_value=%d army_units=%d structures=%d "
                "kill_value=%d loss_value=%d alive=%d",
                h, minute, army, units, structures, house->getKillValue(), house->getLossValue(),
                int(house->isAlive()));
    }
}

/// Runs the match to endCycle (or until 'stop' says so) and reports every wave.
inline void run(Game& game, Uint32 endCycle, const std::function<bool()>& stop = {}) {
    std::vector<Wave> waves;
    std::map<const QuantBot*, Uint32> lastLaunch;
    std::map<const QuantBot*, std::set<Uint32>> knownMembers;
    std::unordered_map<Uint32, Sint32> lastPrimaryTimer;
    // Every object last cycle, while a trade window is open. Each vanished object is credited on
    // its own, so the iteration order of these maps cannot change any total.
    std::unordered_map<Uint32, Known> previous;
    int nextSample = kSampleMinutes;
    // Home trade: every ground unit at the last sample (location, value, owner house).
    struct Home { Coord location; int value = 0; int house = -1; int team = -1; };
    std::map<Uint32, Home> homeUnits;
    auto homeOf = [](Coord at, int team, bool own) {
        // The house whose building is within kHomeTiles: own team's (own) or another team's.
        for (const auto* structure : structureList) {
            if (structure->getHealth() <= 0 || !structure->getOwner()) continue;
            if ((structure->getOwner()->getTeamID() == team) != own) continue;
            if (blockDistance(at, structure->getClosestPoint(at)).lround() <= kHomeTiles)
                return int(structure->getOwner()->getHouseID());
        }
        return -1;
    };
    while (!game.finished && game.getGameCycleCount() < endCycle && !(stop && stop())) {
        // Where every live member of a wave in contact stood before this cycle, for kill credit.
        std::vector<std::pair<size_t, Coord>> memberSpots;
        for (size_t w = 0; w < waves.size(); ++w) {
            const auto& wave = waves[w];
            if (wave.reported || wave.contact == 0) continue;
            for (const Uint32 id : wave.members) {
                const auto* unit = dynamic_cast<const UnitBase*>(game.getObjectManager().getObject(id));
                if (unit && unit->getHealth() > 0 && unit->isActive()) memberSpots.emplace_back(w, unit->getLocation());
            }
        }

        game.updateGameState();
        const Uint32 now = game.getGameCycleCount();
        const std::vector<Footprint> footprints = liveFootprints();

        // New dispatches: a changed launch cycle with at least three new ground members.
        for (int h = 0; h < NUM_HOUSES; ++h) {
            House* house = game.getHouse(h);
            if (!house) continue;
            for (const auto& player : house->getPlayerList()) {
                const auto* bot = dynamic_cast<const QuantBot*>(player.get());
                if (!bot || bot->customWave.launched == 0) continue;
                auto& known = knownMembers[bot];
                if (lastLaunch[bot] == bot->customWave.launched) {
                    known.insert(bot->customWave.members.begin(), bot->customWave.members.end());
                    continue;
                }
                lastLaunch[bot] = bot->customWave.launched;
                Wave wave;
                wave.house = h;
                wave.launched = bot->customWave.launched;
                const int myTeam = house->getTeamID();
                for (const Uint32 id : bot->customWave.members) {
                    if (known.count(id)) continue;
                    const auto* unit = dynamic_cast<const UnitBase*>(game.getObjectManager().getObject(id));
                    if (unit && unit->isAGroundUnit() && unit->isActive() && !inTargetArea(unit, myTeam, footprints)) {
                        wave.members.push_back(id);
                        wave.value[id] = priceOf(unit);
                        if (unit->isInFormation()) ++wave.formed;
                    }
                }
                known.insert(bot->customWave.members.begin(), bot->customWave.members.end());
                if (wave.members.size() >= 3) waves.push_back(wave);
            }
        }

        bool tradeOpen = false;
        for (auto& wave : waves) {
            if (wave.reported) continue;
            const int myTeam = game.getHouse(wave.house)->getTeamID();
            int forming = 0, inFormation = 0;
            for (const Uint32 id : wave.members) {
                const auto* unit = dynamic_cast<const UnitBase*>(game.getObjectManager().getObject(id));
                if (!unit || unit->getHealth() <= 0) {
                    if (!wave.died.count(id)) {
                        wave.died[id] = now;
                        if (wave.contact != 0 && now < wave.contact + kTradeWindowCycles)
                            wave.lossValue += wave.value[id];
                    }
                    continue;
                }
                if (unit->isFormationForming()) ++forming;
                if (unit->isInFormation()) ++inFormation;
                if (!unit->isActive()) continue;
                const bool inBase = inTargetArea(unit, myTeam, footprints);
                if (inBase && !wave.arrival.count(id)) wave.arrival[id] = now;
                const auto last = lastPrimaryTimer.find(id);
                const bool fired = last != lastPrimaryTimer.end() && unit->primaryWeaponTimer > last->second;
                if (!wave.engaged.count(id) && (inBase || fired || engaging(unit, myTeam))) {
                    wave.engaged[id] = now;
                    if (wave.contact == 0) wave.contact = now;
                }
                if (fired && wave.contact != 0 && now < wave.contact + kContactWindowCycles) ++wave.shots;
            }
            if (wave.contact == now) {
                wave.inFormationAtContact = inFormation;
                // How many members are with the main body when the wave first meets the enemy.
                std::vector<Coord> at;
                for (const Uint32 id : wave.members) {
                    const auto* unit = dynamic_cast<const UnitBase*>(game.getObjectManager().getObject(id));
                    if (unit && unit->getHealth() > 0 && unit->isActive()) at.push_back(unit->getLocation());
                }
                int together = 0;
                if (!at.empty()) {
                    Sint64 sx = 0, sy = 0;
                    for (const Coord& p : at) { sx += p.x; sy += p.y; }
                    const Sint64 n = Sint64(at.size());
                    Coord anchor = at.front();
                    Sint64 best = -1;
                    for (const Coord& p : at) {
                        const Sint64 dx = p.x * n - sx, dy = p.y * n - sy, d = dx * dx + dy * dy;
                        if (best < 0 || d < best) { best = d; anchor = p; }
                    }
                    for (const Coord& p : at) if (blockDistance(anchor, p).lround() <= kTogetherTiles) ++together;
                }
                wave.togetherAtContact = together;
            }
            if (wave.contact != 0 && now == wave.contact + Uint32(MILLI2CYCLES(10000))) wave.inFormation10s = inFormation;
            if (forming > 0) wave.sawForming = true;
            if (wave.sawForming && wave.formUpEnd == 0 && forming == 0) wave.formUpEnd = now;
            if (wave.formed && wave.formationEnd == 0 && inFormation == 0) wave.formationEnd = now;
            if (wave.contact != 0 && now < wave.contact + kTradeWindowCycles) tradeOpen = true;
            if (now >= wave.launched + kWaveWindowCycles) {
                wave.survivors = int(wave.members.size() - wave.died.size());
                report(wave);
                wave.reported = true;
            }
        }

        // Kill credit: an enemy of a wave that vanished this cycle within kKillCreditTiles of a
        // member that was alive at the start of the cycle, while that wave's trade window is open.
        std::unordered_map<Uint32, Known> current;
        if (tradeOpen || !previous.empty()) {
            current.reserve(unitList.size() + structureList.size());
            for (const auto* unit : unitList)
                if (unit->getHealth() > 0) current[unit->getObjectID()] = {unit->getLocation(), priceOf(unit), unit->getOwner()->getTeamID()};
            for (const auto* structure : structureList)
                if (structure->getHealth() > 0) current[structure->getObjectID()] = {structure->getLocation(), priceOf(structure), structure->getOwner()->getTeamID()};
            for (const auto& [id, known] : previous) {
                if (current.count(id)) continue;
                for (const auto& [w, spot] : memberSpots) {
                    auto& wave = waves[w];
                    if (wave.reported || now >= wave.contact + kTradeWindowCycles) continue;
                    if (known.team == game.getHouse(wave.house)->getTeamID()) continue;
                    if (blockDistance(spot, known.location).lround() > kKillCreditTiles) continue;
                    wave.killValue += known.value;
                    break;
                }
            }
        }
        previous = tradeOpen ? std::move(current) : std::unordered_map<Uint32, Known>{};

        for (const auto* unit : unitList) lastPrimaryTimer[unit->getObjectID()] = unit->primaryWeaponTimer;
        if (now >= Uint32(MILLI2CYCLES(nextSample * 60000))) { sampleHouses(game, nextSample); nextSample += kSampleMinutes; }
        if (now % kHomeSampleCycles == 0) {
            std::map<Uint32, Home> current;
            for (const auto* unit : unitList)
                if (unit->getHealth() > 0 && unit->isActive() && unit->isAGroundUnit() && unit->getOwner())
                    current[unit->getObjectID()] = {unit->getLocation(), priceOf(unit), int(unit->getOwner()->getHouseID()),
                                                    unit->getOwner()->getTeamID()};
            for (const auto& [id, known] : homeUnits) {
                if (current.count(id)) continue;
                const auto* still = game.getObjectManager().getObject(id);
                if (still && still->getHealth() > 0) continue;     // inside a carryall or a bay
                if (homeOf(known.location, known.team, true) == known.house) homeTrade()[known.house].loss += known.value;
                const int defender = homeOf(known.location, known.team, false);
                if (defender >= 0) homeTrade()[defender].kill += known.value;
            }
            homeUnits = std::move(current);
        }
    }

    int reported = 0;
    for (auto& wave : waves) {
        if (wave.reported) continue;
        wave.survivors = int(wave.members.size() - wave.died.size());
        report(wave);
        wave.reported = true;
        ++reported;
    }
    SDL_Log("FORMATION_WAVE_SUMMARY: waves=%zu cut_by_match_end=%d end_cycle=%u",
            waves.size(), reported, game.getGameCycleCount());
    sampleHouses(game, -1);
}

} // namespace FormationWaveMeasure
