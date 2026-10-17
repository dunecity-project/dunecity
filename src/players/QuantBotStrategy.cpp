/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <players/QuantBotStrategy.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace QuantBotStrategy {

namespace {

constexpr const char* kMixNames[kMixCount] = {"Learned", "QuadMass", "TrikeSwarm", "TankMass", "Artillery",
                                              "Specials", "Air", "Infantry", "Counter"};
constexpr const char* kTempoNames[kTempoCount] = {"Base", "Swarm", "Timing", "Rush", "Flood"};

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

bool sameName(const std::string& a, const char* b) {
    const std::string other(b);
    if (a.size() != other.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(other[i]))) return false;
    return true;
}

template<size_t N> int indexOf(const std::string& text, const char* const (&names)[N]) {
    const std::string name = trim(text);
    for (size_t i = 0; i < N; ++i)
        if (sameName(name, names[i])) return static_cast<int>(i);
    return -1;
}

template<size_t N> bool parseWeightList(const std::string& text, std::array<int, N>& weights, const char* const (&names)[N]) {
    std::array<int, N> parsed{};
    std::istringstream input(text);
    std::string entry;
    int64_t total = 0;
    while (std::getline(input, entry, ',')) {
        if (trim(entry).empty()) continue;
        const auto equals = entry.find(':');
        if (equals == std::string::npos) return false;
        const int index = indexOf(entry.substr(0, equals), names);
        const std::string value = trim(entry.substr(equals + 1));
        if (index < 0 || value.empty() || value.find_first_not_of("0123456789") != std::string::npos || value.size() > 6)
            return false;
        parsed[index] = std::stoi(value);
        total += parsed[index];
    }
    if (total <= 0) return false;
    weights = parsed;
    return true;
}

template<size_t N> std::string weightListText(const std::array<int, N>& weights, const char* const (&names)[N]) {
    std::string text;
    for (size_t i = 0; i < N; ++i) {
        if (weights[i] <= 0) continue;
        if (!text.empty()) text += ',';
        text += names[i];
        text += ':';
        text += std::to_string(weights[i]);
    }
    return text;
}

uint64_t splitMix64(uint64_t z) {
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

constexpr uint32_t kMixSalt = 0x4D495853;   // "MIXS"
constexpr uint32_t kTempoSalt = 0x54454D50; // "TEMP"

MixProfile mixProfile(UnitMixPolicy::Weights prior, int infantryPercent = 0, bool counter = false) {
    MixProfile profile;
    profile.prior = prior;
    profile.priorBps = (counter || *std::max_element(prior.begin(), prior.end()) > 0) ? 10000 : 0;
    profile.infantryPercent = infantryPercent;
    profile.counter = counter;
    return profile;
}

TempoProfile tempoProfile(int readiness, int units, int value, bool lightFirst = false, bool opening = true, int minutes = 0) {
    TempoProfile profile;
    profile.readinessPercent = readiness;
    profile.minWaveUnits = units;
    profile.minWaveValue = value;
    profile.lightFirst = lightFirst;
    profile.openingReadiness = opening;
    profile.minutes = minutes;
    return profile;
}

} // namespace

Settings::Settings() {
    mixWeights[static_cast<size_t>(Mix::Learned)] = 100;
    tempoWeights[static_cast<size_t>(Tempo::Base)] = 100;
}

const char* name(Mix mix) { return static_cast<size_t>(mix) < kMixCount ? kMixNames[static_cast<size_t>(mix)] : "?"; }
const char* name(Tempo tempo) { return static_cast<size_t>(tempo) < kTempoCount ? kTempoNames[static_cast<size_t>(tempo)] : "?"; }

bool parse(const std::string& text, Mix& mix) {
    const int index = indexOf(text, kMixNames);
    if (index < 0) return false;
    mix = static_cast<Mix>(index);
    return true;
}

bool parse(const std::string& text, Tempo& tempo) {
    const int index = indexOf(text, kTempoNames);
    if (index < 0) return false;
    tempo = static_cast<Tempo>(index);
    return true;
}

bool parseMode(const std::string& text, Settings& settings) {
    const std::string mode = trim(text);
    if (sameName(mode, "Random")) {
        settings.random = true;
        return true;
    }
    const auto colon = mode.find(':');
    if (colon == std::string::npos || !sameName(trim(mode.substr(0, colon)), "Fixed")) return false;
    const std::string rest = mode.substr(colon + 1);
    const auto comma = rest.find(',');
    Mix mix;
    Tempo tempo = Tempo::Base;
    if (!parse(rest.substr(0, comma), mix)) return false;
    if (comma != std::string::npos && !parse(rest.substr(comma + 1), tempo)) return false;
    settings.random = false;
    settings.fixedMix = mix;
    settings.fixedTempo = tempo;
    return true;
}

std::string modeText(const Settings& settings) {
    if (settings.random) return "Random";
    return std::string("Fixed:") + name(settings.fixedMix) + "," + name(settings.fixedTempo);
}

bool parseWeights(const std::string& text, MixWeights& weights) { return parseWeightList(text, weights, kMixNames); }
bool parseWeights(const std::string& text, TempoWeights& weights) { return parseWeightList(text, weights, kTempoNames); }
std::string weightsText(const MixWeights& weights) { return weightListText(weights, kMixNames); }
std::string weightsText(const TempoWeights& weights) { return weightListText(weights, kTempoNames); }

uint64_t draw(uint32_t seed, int house, uint32_t salt) {
    return splitMix64(splitMix64((uint64_t(seed) << 32) | uint32_t(house)) ^ salt);
}

int weightedIndex(const int* weights, size_t count, uint64_t value) {
    uint64_t total = 0;
    for (size_t i = 0; i < count; ++i) total += uint64_t(std::max(0, weights[i]));
    if (total == 0) return -1;
    uint64_t point = value % total;
    for (size_t i = 0; i < count; ++i) {
        const uint64_t weight = uint64_t(std::max(0, weights[i]));
        if (point < weight) return static_cast<int>(i);
        point -= weight;
    }
    return -1;
}

Choice pick(const Settings& settings, uint32_t seed, int house, const std::array<bool, kMixCount>& eligible) {
    Choice choice;
    if (!settings.random) {
        choice.mix = eligible[static_cast<size_t>(settings.fixedMix)] ? settings.fixedMix : Mix::Learned;
        choice.tempo = settings.fixedTempo;
        return choice;
    }
    MixWeights mixWeights = settings.mixWeights;
    for (size_t i = 0; i < kMixCount; ++i) if (!eligible[i]) mixWeights[i] = 0;
    const int mix = weightedIndex(mixWeights.data(), kMixCount, draw(seed, house, kMixSalt));
    const int tempo = weightedIndex(settings.tempoWeights.data(), kTempoCount, draw(seed, house, kTempoSalt));
    choice.mix = mix < 0 ? Mix::Learned : static_cast<Mix>(mix);
    choice.tempo = tempo < 0 ? Tempo::Base : static_cast<Tempo>(tempo);
    return choice;
}

const MixProfile& profile(Mix mix) {
    // Slots: tank, siege tank, launcher, special, ornithopter, trike, raider trike, quad.
    static const std::array<MixProfile, kMixCount> profiles = {
        mixProfile({}),                                            // Learned: the configured opening
        mixProfile({2000, 0, 0, 0, 0, 0, 1000, 7000}),             // QuadMass
        mixProfile({1000, 0, 0, 0, 0, 4500, 4500, 0}),             // TrikeSwarm
        mixProfile({7000, 1500, 1500, 0, 0, 0, 0, 0}),             // TankMass
        mixProfile({2000, 4500, 3500, 0, 0, 0, 0, 0}),             // Artillery: siege and launchers behind tanks
        mixProfile({2500, 1500, 1000, 5000, 0, 0, 0, 0}),          // Specials: Devastator / Sonic Tank / Deviator
        mixProfile({1500, 1000, 1500, 0, 6000, 0, 0, 0}),          // Air
        mixProfile({}, 35),                                        // Infantry: 35% of the army on foot
        mixProfile({}, 0, true),                                   // Counter
    };
    return profiles[std::min(static_cast<size_t>(mix), kMixCount - 1)];
}

const TempoProfile& profile(Tempo tempo) {
    static const std::array<TempoProfile, kTempoCount> profiles = {
        tempoProfile(0, 6, 3000),                     // Base: the configured readiness (60%)
        tempoProfile(35, 4, 2000),                    // Swarm: many smaller waves after a full opening
        tempoProfile(100, 6, 3000),                   // Timing: every wave at full readiness
        tempoProfile(15, 4, 1200, true, false, 20),   // Rush: light factory first, early small waves, 20 minutes
        tempoProfile(20, 3, 1500),                    // Flood: more pressure than Swarm
    };
    return profiles[std::min(static_cast<size_t>(tempo), kTempoCount - 1)];
}

UnitMixPolicy::Weights counterPrior(int64_t light, int64_t armour, int64_t launchers, int64_t air) {
    UnitMixPolicy::Weights counter{};
    light = std::max<int64_t>(0, light);
    armour = std::max<int64_t>(0, armour);
    launchers = std::max<int64_t>(0, launchers);
    air = std::max<int64_t>(0, air);
    const int64_t total = light + armour + launchers + air;
    if (total <= 0) return counter;
    auto share = [&](int64_t part, int64_t weight) { return part * weight / total; };
    counter[0] = 2000 + share(light, 6000) + share(launchers, 3000);   // tanks
    counter[1] = 1000 + share(armour, 3000) + share(light, 3000);      // siege tanks
    counter[2] = 1000 + share(armour, 6000) + share(air, 6000);        // launchers
    counter[3] = 1500;                                                 // special
    counter[4] = 500 + share(armour, 2000);                            // ornithopters
    counter[7] = 500 + share(launchers, 4000);                         // quads
    return counter;
}

UnitMixPolicy::Weights applyPrior(const UnitMixPolicy::Weights& opening, const UnitMixPolicy::Weights& prior,
                                  int priorBps, const std::array<bool, 8>& available) {
    if (priorBps <= 0) return opening;
    UnitMixPolicy::Weights masked{};
    for (size_t i = 0; i < masked.size(); ++i) masked[i] = available[i] ? std::max<int64_t>(0, prior[i]) : 0;
    const UnitMixPolicy::Mix target = UnitMixPolicy::normalize(masked);
    if (*std::max_element(target.begin(), target.end()) == 0) return opening;
    const UnitMixPolicy::Mix base = UnitMixPolicy::normalize(opening);
    const int64_t share = std::clamp(priorBps, 0, 10000);
    UnitMixPolicy::Weights blended{};
    for (size_t i = 0; i < blended.size(); ++i)
        blended[i] = int64_t(base[i]) * (10000 - share) + int64_t(target[i]) * share;
    const UnitMixPolicy::Mix result = UnitMixPolicy::normalize(blended);
    UnitMixPolicy::Weights weights{};
    for (size_t i = 0; i < weights.size(); ++i) weights[i] = result[i];
    return weights;
}

std::array<bool, kMixCount> eligibleMixes(const std::array<bool, 8>& buildable, bool infantryBuildable) {
    std::array<bool, kMixCount> eligible{};
    for (size_t i = 0; i < kMixCount; ++i) {
        const MixProfile& mix = profile(static_cast<Mix>(i));
        if (mix.infantryPercent > 0) { eligible[i] = infantryBuildable; continue; }
        int64_t total = 0, fielded = 0;
        for (size_t slot = 0; slot < 8; ++slot) {
            total += std::max<int64_t>(0, mix.prior[slot]);
            if (buildable[slot]) fielded += std::max<int64_t>(0, mix.prior[slot]);
        }
        eligible[i] = total == 0 || fielded * 2 >= total;
    }
    return eligible;
}

} // namespace QuantBotStrategy
