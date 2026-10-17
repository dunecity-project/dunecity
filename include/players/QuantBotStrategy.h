#ifndef QUANTBOT_STRATEGY_H
#define QUANTBOT_STRATEGY_H

// QuantBot strategies (docs/ai-play-styles.md, "Strategies"): every Custom Hard/Brutal QuantBot
// plays one unit-mix strategy and one tempo for the whole game, two separate factors each picked
// once at game start from weighted options ([Strategy] in QuantBot Config.ini).
//
// Lockstep: the pick is a pure function of the game's random seed, the house and the (hashed)
// config - no simulation RNG draw, no clock, no local setting - so every peer, a replay and a
// reload agree, and the order the bots are created or updated in cannot change it. The choice is
// saved (SAVEGAMEVERSION 9865); a 9864 save picks it afresh by the same function.
//
// A mix strategy is a prior, not a frozen composition: it replaces the opening mix that
// UnitMixPolicy::allocate blends combat evidence into, so the in-game learning still moves
// production towards what performs, exactly as it does from the learned default.

#include <players/UnitMixPolicy.h>

#include <array>
#include <cstdint>
#include <string>

namespace QuantBotStrategy {

/// Unit-mix strategies. Learned = the configured opening mix (1.0.846).
enum class Mix : uint8_t { Learned, QuadMass, TrikeSwarm, TankMass, Artillery, Specials, Air, Infantry, Counter };
constexpr size_t kMixCount = 9;
/// Tempos (when waves leave). Base = the configured readiness (1.0.846).
enum class Tempo : uint8_t { Base, Swarm, Timing, Rush, Flood };
constexpr size_t kTempoCount = 5;

using MixWeights = std::array<int, kMixCount>;
using TempoWeights = std::array<int, kTempoCount>;

const char* name(Mix mix);
const char* name(Tempo tempo);
/// Case-insensitive name to value; false if unknown.
bool parse(const std::string& text, Mix& mix);
bool parse(const std::string& text, Tempo& tempo);

/// The [Strategy] settings. Optional INI keys; the code defaults apply when absent. Hashed.
struct Settings {
    /// Mode=Random: pick by weight; Mode=Fixed:<mix>,<tempo>: that pair. The constructor sets the
    /// measured default (Fixed:Learned,Swarm) and the measured weights.
    bool random = false;
    Mix fixedMix = Mix::Learned;
    Tempo fixedTempo = Tempo::Base;
    /// MixWeights=Learned:40,QuadMass:10,... (unlisted names weigh 0).
    MixWeights mixWeights{};
    /// TempoWeights=Base:50,Swarm:50,...
    TempoWeights tempoWeights{};
    Settings();
};

/// "Random" or "Fixed:<mix>,<tempo>" (Fixed:<mix> keeps tempo Base); false if malformed.
bool parseMode(const std::string& text, Settings& settings);
std::string modeText(const Settings& settings);
/// "Name:weight,..." over the names of the factor; false (weights unchanged) if malformed,
/// a name is unknown or every weight is 0.
bool parseWeights(const std::string& text, MixWeights& weights);
bool parseWeights(const std::string& text, TempoWeights& weights);
std::string weightsText(const MixWeights& weights);
std::string weightsText(const TempoWeights& weights);

struct Choice {
    Mix mix = Mix::Learned;
    Tempo tempo = Tempo::Base;
    bool operator==(const Choice& other) const { return mix == other.mix && tempo == other.tempo; }
};

/// The deterministic draw for one factor: SplitMix64 over (seed, house, salt).
uint64_t draw(uint32_t seed, int house, uint32_t salt);
/// Index of the weighted option \a value falls on (value modulo the total); -1 if no weight.
int weightedIndex(const int* weights, size_t count, uint64_t value);

/// The strategy of house \a house in the game with random seed \a seed. Mixes the house cannot
/// field (eligible[i] false) are left out of the draw; a fixed mix it cannot field is Learned.
Choice pick(const Settings& settings, uint32_t seed, int house, const std::array<bool, kMixCount>& eligible);

/// What a mix strategy changes.
struct MixProfile {
    /// The starting ratios (tank, siege tank, launcher, special, ornithopter, trike, raider
    /// trike, quad); all zero = the configured opening mix. Unavailable slots are dropped.
    UnitMixPolicy::Weights prior{};
    /// Share of the opening prior the strategy's ratios replace (basis points).
    int priorBps = 0;
    /// How long the prior holds against combat evidence: the fielded army counts this many
    /// percent in UnitMixPolicy::blendByEvidence (100 = as the learned default).
    int holdPercent = 100;
    /// Infantry share of the army (percent); 0 = the difficulty's. Raises barracks and WOR.
    int infantryPercent = 0;
    /// The prior is the counter of the remembered enemy army (counterPrior) once one is seen.
    bool counter = false;
};
const MixProfile& profile(Mix mix);

/// What a tempo changes. Values apply for the first `minutes` game minutes (0 = all game).
struct TempoProfile {
    /// Wave readiness percent; 0 = the configured [Force Concentration] WaveReadinessPercent.
    int readinessPercent = 0;
    /// The smallest main wave.
    int minWaveUnits = 6, minWaveValue = 3000;
    /// Light factory first, ground waves do not wait for a repair yard.
    bool lightFirst = false;
    /// The Construction Yard opening keeps its full-readiness first wave (rule I).
    bool openingReadiness = true;
    int minutes = 0;
};
const TempoProfile& profile(Tempo tempo);

/// The 8-slot prior that counters a remembered enemy army worth light/armour/launchers/air
/// credits (light -> tanks and siege, armour -> launchers and siege, launchers -> quads and
/// tanks, air -> launchers). All zero when nothing has been seen.
UnitMixPolicy::Weights counterPrior(int64_t light, int64_t armour, int64_t launchers, int64_t air);

/// \a opening with priorBps of it replaced by \a prior (masked by \a available and normalised).
/// Returns \a opening unchanged when the profile has no prior or nothing of it is available.
UnitMixPolicy::Weights applyPrior(const UnitMixPolicy::Weights& opening, const UnitMixPolicy::Weights& prior,
                                  int priorBps, const std::array<bool, 8>& available);

/// Mixes a house can field: a mix needs at least half of its prior's weight in slots the house
/// can build (\a buildable over the 8 mix slots), Infantry needs \a infantryBuildable.
std::array<bool, kMixCount> eligibleMixes(const std::array<bool, 8>& buildable, bool infantryBuildable);

} // namespace QuantBotStrategy

#endif // QUANTBOT_STRATEGY_H
