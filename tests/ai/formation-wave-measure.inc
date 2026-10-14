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
    SDL_Log("FORMATION_WAVE: house=%d launched=%u members=%zu engaged=%zu arrived=%zu lost=%zu "
            "contact_s=%.1f engage_spread_s=%.1f arrival_spread_s=%.1f shots30=%d loss60=%d kill60=%d "
            "survivors=%d lost_before_contact=%d formed=%d formup_s=%.1f formation_end_s=%.1f "
            "in_formation_at_contact=%d",
            wave.house, wave.launched, wave.members.size(), wave.engaged.size(), wave.arrival.size(),
            wave.died.size(), since(wave.contact, wave.launched), spreadOf(wave.engaged), spreadOf(wave.arrival),
            wave.shots, wave.lossValue, wave.killValue, wave.survivors, lostBeforeContact, wave.formed,
            wave.formed && wave.sawForming ? since(wave.formUpEnd, wave.launched) : -1.0,
            wave.formed ? since(wave.formationEnd, wave.launched) : -1.0, wave.inFormationAtContact);
}

/// Is this unit inside the enemy base area: within kTargetAreaTiles of a live enemy building?
inline bool inTargetArea(const UnitBase* unit, int myTeam) {
    for (const auto* structure : structureList) {
        if (structure->getOwner()->getTeamID() == myTeam || structure->getHealth() <= 0) continue;
        if (blockDistance(unit->getLocation(), structure->getClosestPoint(unit->getLocation())).lround()
                <= kTargetAreaTiles) return true;
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
    std::map<Uint32, Sint32> lastPrimaryTimer;
    std::map<Uint32, Known> previous;        // every object last cycle, while a trade window is open
    int nextSample = kSampleMinutes;

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
                    if (unit && unit->isAGroundUnit() && unit->isActive() && !inTargetArea(unit, myTeam)) {
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
                const bool inBase = inTargetArea(unit, myTeam);
                if (inBase && !wave.arrival.count(id)) wave.arrival[id] = now;
                const auto last = lastPrimaryTimer.find(id);
                const bool fired = last != lastPrimaryTimer.end() && unit->primaryWeaponTimer > last->second;
                if (!wave.engaged.count(id) && (inBase || fired || engaging(unit, myTeam))) {
                    wave.engaged[id] = now;
                    if (wave.contact == 0) wave.contact = now;
                }
                if (fired && wave.contact != 0 && now < wave.contact + kContactWindowCycles) ++wave.shots;
            }
            if (wave.contact == now) wave.inFormationAtContact = inFormation;
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
        std::map<Uint32, Known> current;
        if (tradeOpen || !previous.empty()) {
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
        previous = tradeOpen ? std::move(current) : std::map<Uint32, Known>{};

        for (const auto* unit : unitList) lastPrimaryTimer[unit->getObjectID()] = unit->primaryWeaponTimer;
        if (now >= Uint32(MILLI2CYCLES(nextSample * 60000))) { sampleHouses(game, nextSample); nextSample += kSampleMinutes; }
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
