// Included only by campaign-balance-probe.inc (run-campaign-balance.py --formation-measure).
//
// Measures how a QuantBot attack wave travels in a real AI-versus-AI skirmish: the spread
// of arrival times of its members at the enemy (first to last member reaching the target
// area) and the number of shots its members fire in the first 30 seconds of contact. The
// same seed, map and roster give the identical match up to the first dispatch, so running
// this against two builds compares how the two move a wave. Read-only: it never issues an
// order or changes simulation state.
#include <players/QuantBot.h>
#include <units/UnitBase.h>
#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace FormationWaveMeasure {

/// A member has reached the target area once it is this close to any enemy building.
/// Members already this close when the wave is dispatched are not travelling and are
/// left out of the wave.
constexpr int kTargetAreaTiles = 10;
/// A wave is evaluated over this long after dispatch; later arrivals do not count.
constexpr Uint32 kWaveWindowCycles = MILLI2CYCLES(5 * 60 * 1000);
/// Shots are counted over this long after the wave's first arrival.
constexpr Uint32 kContactWindowCycles = MILLI2CYCLES(30 * 1000);

struct Wave {
    int house = -1;
    Uint32 launched = 0;
    std::vector<Uint32> members;
    std::map<Uint32, Uint32> arrival;   ///< member -> first cycle inside the target area
    std::set<Uint32> lost;              ///< members destroyed before arriving
    Uint32 contact = 0;                 ///< first arrival of any member; 0 = none yet
    int shots = 0;                      ///< primary shots of members in the contact window
    bool reported = false;
};

inline double seconds(Uint32 cycles) { return cycles * GAMESPEED_DEFAULT / 1000.0; }

inline void report(const Wave& wave) {
    std::vector<Uint32> times;
    for (const auto& [id, cycle] : wave.arrival) times.push_back(cycle);
    std::sort(times.begin(), times.end());
    const Uint32 spread = times.empty() ? 0 : times.back() - times.front();
    const Uint32 p80 = times.empty() ? 0
        : times[std::min(times.size() - 1, (times.size() * 8 + 9) / 10 - 1)] - times.front();
    SDL_Log("FORMATION_WAVE: house=%d launched=%u members=%zu arrived=%zu lost=%zu "
            "first_arrival_s=%.1f spread_s=%.1f spread80_s=%.1f shots30=%d",
            wave.house, wave.launched, wave.members.size(), times.size(), wave.lost.size(),
            times.empty() ? -1.0 : seconds(times.front() - wave.launched),
            seconds(spread), seconds(p80), wave.shots);
}

/// Is this unit inside the target area: within kTargetAreaTiles of a live enemy building?
inline bool inTargetArea(const UnitBase* unit, int myTeam) {
    for (const auto* structure : structureList) {
        if (structure->getOwner()->getTeamID() == myTeam || structure->getHealth() <= 0) continue;
        if (blockDistance(unit->getLocation(), structure->getClosestPoint(unit->getLocation())).lround()
                <= kTargetAreaTiles) return true;
    }
    return false;
}

inline void run(Game& game, Uint32 endCycle) {
    std::vector<Wave> waves;
    std::map<const QuantBot*, Uint32> lastLaunch;
    std::map<const QuantBot*, std::set<Uint32>> knownMembers;
    std::map<Uint32, Sint32> lastPrimaryTimer;

    while (!game.finished && game.getGameCycleCount() < endCycle) {
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
                    if (unit && unit->isAGroundUnit() && unit->isActive() && !inTargetArea(unit, myTeam))
                        wave.members.push_back(id);
                }
                known.insert(bot->customWave.members.begin(), bot->customWave.members.end());
                if (wave.members.size() >= 3) waves.push_back(wave);
            }
        }

        for (auto& wave : waves) {
            if (wave.reported) continue;
            const int myTeam = game.getHouse(wave.house)->getTeamID();
            for (const Uint32 id : wave.members) {
                if (wave.arrival.count(id) || wave.lost.count(id)) continue;
                const auto* unit = dynamic_cast<const UnitBase*>(game.getObjectManager().getObject(id));
                if (!unit || unit->getHealth() <= 0) { wave.lost.insert(id); continue; }
                if (!unit->isActive()) continue;
                if (inTargetArea(unit, myTeam)) {
                    wave.arrival[id] = now;
                    if (wave.contact == 0) wave.contact = now;
                }
            }
            if (wave.contact != 0 && now < wave.contact + kContactWindowCycles) {
                for (const Uint32 id : wave.members) {
                    const auto* unit = dynamic_cast<const UnitBase*>(game.getObjectManager().getObject(id));
                    if (!unit) continue;
                    const auto previous = lastPrimaryTimer.find(id);
                    if (previous != lastPrimaryTimer.end() && unit->primaryWeaponTimer > previous->second)
                        ++wave.shots;
                }
            }
            if (now >= wave.launched + kWaveWindowCycles) { report(wave); wave.reported = true; }
        }
        for (const auto* unit : unitList) lastPrimaryTimer[unit->getObjectID()] = unit->primaryWeaponTimer;
    }

    std::vector<double> spreads;
    int shots = 0, counted = 0;
    for (auto& wave : waves) {
        if (!wave.reported) { report(wave); wave.reported = true; }
        if (wave.arrival.size() < 3) continue;
        Uint32 first = UINT32_MAX, last = 0;
        for (const auto& [id, cycle] : wave.arrival) { first = std::min(first, cycle); last = std::max(last, cycle); }
        spreads.push_back(seconds(last - first));
        shots += wave.shots;
        ++counted;
    }
    std::sort(spreads.begin(), spreads.end());
    double mean = 0;
    for (const double s : spreads) mean += s;
    if (!spreads.empty()) mean /= spreads.size();
    SDL_Log("FORMATION_WAVE_SUMMARY: waves=%zu measured=%d mean_spread_s=%.1f median_spread_s=%.1f "
            "total_shots30=%d mean_shots30=%.1f end_cycle=%u",
            waves.size(), counted, mean, spreads.empty() ? 0.0 : spreads[spreads.size() / 2],
            shots, counted ? double(shots) / counted : 0.0, game.getGameCycleCount());
}

} // namespace FormationWaveMeasure
