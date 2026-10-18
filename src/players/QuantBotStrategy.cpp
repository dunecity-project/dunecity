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
                                              "Specials", "Air", "Infantry", "Counter", "Tuned", "Bounded"};
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
constexpr uint32_t kBoundedSalt = 0x424F554E; // "BOUN": the Bounded mix's cut points

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
    // Measured (docs/ai-play-styles.md, "Strategies"): only the Swarm tempo beats 1.0.846 on
    // Construction Yard starts, so the default plays Swarm, and Mode=Random plays these weights
    // (variety: the learned mix half the time, the mixes level with it, Swarm with some Flood;
    // TankMass, Infantry and Specials-with-Swarm measured below BASE and are left out). The default
    // mix is Tuned, the measured per-house starting ratios (section 8): Tuned/Swarm v Learned/Swarm
    // +0.27 (+0.09..+0.45), n=468. Every starting mix is bounded (2%-50% per producible slot).
    random = false;
    fixedMix = Mix::Tuned;
    fixedTempo = Tempo::Swarm;
    mixWeights[static_cast<size_t>(Mix::Learned)] = 50;
    mixWeights[static_cast<size_t>(Mix::QuadMass)] = 15;
    mixWeights[static_cast<size_t>(Mix::Counter)] = 10;
    mixWeights[static_cast<size_t>(Mix::Artillery)] = 10;
    mixWeights[static_cast<size_t>(Mix::Air)] = 10;
    mixWeights[static_cast<size_t>(Mix::TrikeSwarm)] = 5;
    tempoWeights[static_cast<size_t>(Tempo::Swarm)] = 80;
    tempoWeights[static_cast<size_t>(Tempo::Flood)] = 20;
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
    // Slots: tank, siege tank, launcher, special, ornithopter, trike, raider trike, quad. Every
    // preset is the 1.0.847 preset clamped and renormalised into the starting-mix bounds over all
    // eight slots (bound(): 2%-50% per slot); bound() renormalises it again over the slots a house
    // can produce. 1.0.847: QuadMass 20/0/0/0/0/0/10/70, TrikeSwarm 10/0/0/0/0/45/45/0, TankMass
    // 70/15/15, Artillery 20/45/35, Specials 25/15/10/50, Air 15/10/15/0/60.
    static const std::array<MixProfile, kMixCount> profiles = {
        mixProfile({}),                                                // Learned: the configured opening
        mixProfile({2667, 200, 200, 200, 200, 200, 1333, 5000}),       // QuadMass
        mixProfile({900, 200, 200, 200, 200, 4050, 4050, 200}),        // TrikeSwarm
        mixProfile({5000, 2000, 2000, 200, 200, 200, 200, 200}),       // TankMass
        mixProfile({1800, 4050, 3150, 200, 200, 200, 200, 200}),       // Artillery: siege and launchers behind tanks
        mixProfile({2300, 1380, 920, 4600, 200, 200, 200, 200}),       // Specials: Devastator / Sonic Tank / Deviator
        mixProfile({1575, 1050, 1575, 200, 5000, 200, 200, 200}),      // Air
        mixProfile({}, 35),                                            // Infantry: 35% of the army on foot
        mixProfile({}, 0, true),                                       // Counter
        mixProfile({}),                                                // Tuned: per house, resolved by QuantBot
        mixProfile({}),                                                // Bounded: drawn per game, resolved by QuantBot
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

UnitMixPolicy::Mix bound(const UnitMixPolicy::Weights& weights, const std::array<bool, 8>& available) {
    UnitMixPolicy::Mix result{};
    std::array<size_t, 8> order{};
    size_t n = 0;
    for (size_t i = 0; i < 8; ++i) if (available[i]) order[n++] = i;
    if (n == 0) return result;
    if (n == 1) { result[order[0]] = 10000; return result; }
    // The weights as millionths of the available total (floored, so a heavier slot never gets a
    // smaller value; a bounded mix in basis points maps exactly): small integers for the products below.
    UnitMixPolicy::Weights v{};
    int64_t sum = 0;
    for (size_t k = 0; k < n; ++k) sum += std::max<int64_t>(0, weights[order[k]]);
    const int64_t divisor = sum > 1000000000000LL ? sum / 1000000000000LL + 1 : 1;   // keeps w * 1e6 within 64 bits
    sum = 0;
    for (size_t k = 0; k < n; ++k) sum += std::max<int64_t>(0, weights[order[k]]) / divisor;
    for (size_t k = 0; k < n && sum > 0; ++k)
        v[order[k]] = std::max<int64_t>(0, weights[order[k]]) / divisor * 1000000 / sum;
    constexpr int64_t lo = kStartMinBps, hi = kStartMaxBps;   // n >= 2, so n * hi >= 10000
    // Ascending weight, lower slot first on a tie: with one scale t the slots at the floor are the
    // lightest, those at the cap the heaviest.
    std::stable_sort(order.begin(), order.begin() + n, [&](size_t a, size_t b) { return v[a] < v[b]; });
    for (size_t low = 0; low <= n; ++low)
        for (size_t high = 0; low + high <= n; ++high) {
            const int64_t budget = 10000 - int64_t(low) * lo - int64_t(high) * hi;
            if (budget < 0) continue;
            int64_t total = 0;
            for (size_t k = low; k < n - high; ++k) total += v[order[k]];
            bool ok = true;
            if (low == n - high) {
                // Every slot at a bound: some t must put the low ones at or under the floor and the
                // high ones (all weighted) at or over the cap.
                ok = budget == 0;
                for (size_t k = low; k < n && ok; ++k) ok = v[order[k]] > 0;
                if (ok && low > 0 && high > 0) ok = v[order[low - 1]] * hi <= v[order[low]] * lo;
            } else {
                // t = budget / total; the middle slots are within the bounds, the others beyond them.
                ok = total > 0;
                for (size_t k = 0; k < n && ok; ++k) {
                    const int64_t scaled = v[order[k]] * budget;
                    if (k < low) ok = scaled <= lo * total;
                    else if (k >= n - high) ok = scaled >= hi * total;
                    else ok = scaled >= lo * total && scaled <= hi * total;
                }
            }
            if (!ok) continue;
            int64_t assigned = 0;
            std::array<int64_t, 8> remainder{};
            for (size_t k = 0; k < n; ++k) {
                const size_t slot = order[k];
                if (k < low) result[slot] = int(lo);
                else if (k >= n - high) result[slot] = int(hi);
                else {
                    result[slot] = int(v[slot] * budget / total);
                    remainder[slot] = v[slot] * budget % total;
                }
                assigned += result[slot];
            }
            // Rounding: one basis point each to the largest remainders, lowest slot first. A slot
            // with a remainder is below its exact share, which is within the bounds.
            for (int64_t left = 10000 - assigned; left > 0; --left) {
                size_t best = 8;
                for (size_t slot = 0; slot < 8; ++slot)
                    if (remainder[slot] > 0 && (best == 8 || remainder[slot] > remainder[best])) best = slot;
                if (best == 8) break;
                ++result[best];
                remainder[best] = 0;
            }
            return result;
        }
    // Even every weighted slot at the cap is short: those at the cap, the zero-weight slots share the rest.
    int64_t rest = 10000;
    size_t zeros = 0;
    for (size_t k = 0; k < n; ++k) {
        if (v[order[k]] > 0) { result[order[k]] = int(hi); rest -= hi; }
        else ++zeros;
    }
    std::sort(order.begin(), order.begin() + n);
    int64_t extra = zeros ? rest % int64_t(zeros) : 0;
    for (size_t k = 0; k < n; ++k)
        if (v[order[k]] == 0) result[order[k]] = int(rest / int64_t(zeros) + (extra-- > 0 ? 1 : 0));
    return result;
}

bool withinBounds(const UnitMixPolicy::Mix& mix, const std::array<bool, 8>& available) {
    int count = 0, total = 0;
    for (size_t i = 0; i < 8; ++i) {
        if (!available[i] && mix[i] != 0) return false;
        if (available[i]) ++count;
        total += mix[i];
    }
    if (count == 0) return total == 0;
    if (total != 10000) return false;
    for (size_t i = 0; i < 8; ++i)
        if (available[i] && count > 1 && (mix[i] < kStartMinBps || mix[i] > kStartMaxBps)) return false;
    return true;
}

UnitMixPolicy::Mix randomMix(uint32_t seed, int house, const std::array<bool, 8>& available) {
    std::array<size_t, 8> slots{};
    size_t n = 0;
    for (size_t i = 0; i < 8; ++i) if (available[i]) slots[n++] = i;
    if (n <= 1) return bound({}, available);
    UnitMixPolicy::Weights last{};
    for (uint32_t attempt = 0; attempt < 64; ++attempt) {
        // n - 1 uniform cut points of [0, 10000]; their spacings are a flat Dirichlet draw.
        std::array<int64_t, 8> cuts{};
        for (size_t j = 0; j + 1 < n; ++j)
            cuts[j] = int64_t(draw(seed, house, kBoundedSalt + attempt * 8 + uint32_t(j)) % 10001);
        std::sort(cuts.begin(), cuts.begin() + (n - 1));
        UnitMixPolicy::Mix mix{};
        int64_t previous = 0;
        for (size_t k = 0; k < n; ++k) {
            const int64_t next = k + 1 < n ? cuts[k] : 10000;
            mix[slots[k]] = int(next - previous);
            last[slots[k]] = next - previous;
            previous = next;
        }
        if (withinBounds(mix, available)) return mix;
    }
    return bound(last, available);
}

bool parseRatios(const std::string& text, UnitMixPolicy::Weights& weights) {
    if (trim(text).empty()) { weights = {}; return true; }
    UnitMixPolicy::Weights parsed{};
    std::istringstream input(text);
    std::string entry;
    size_t count = 0;
    int64_t total = 0;
    while (std::getline(input, entry, ',')) {
        const std::string value = trim(entry);
        if (count >= parsed.size() || value.empty() || value.size() > 6
            || value.find_first_not_of("0123456789") != std::string::npos) return false;
        parsed[count] = std::stoll(value);
        total += parsed[count++];
    }
    if (count != parsed.size() || total <= 0) return false;
    weights = parsed;
    return true;
}

std::string ratiosText(const UnitMixPolicy::Weights& weights) {
    std::string text;
    for (size_t i = 0; i < weights.size(); ++i) text += (i ? "," : "") + std::to_string(weights[i]);
    return text;
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
