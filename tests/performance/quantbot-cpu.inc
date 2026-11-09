// Included only by campaign-balance-probe.inc (run-campaign-balance.py), never by the shipped game.
//
// QuantBot CPU for the frame-cost measurement (game-perf.inc, BALANCE_PERF): while a perf run's
// match is created, the qBotEasy/Medium/Hard/Brutal factory entries build this subclass, which
// adds the thread CPU of every QuantBot::update() to GamePerf::quantBotCpuMs. The factory keeps the
// original class string and the base constructor receives exactly the ordinary difficulty and
// role; only clocks are read, so the match is the same game (same PROBE_DIGEST lines). The same
// technique as civ-wars-match.inc's MeasuredQuantBot. Separate from game-perf.inc, which must
// stay compilable in older releases (game-perf.py --source).
#include <players/PlayerFactory.h>
#include <players/QuantBot.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace GamePerf {

class MeasuredQuantBot final : public QuantBot {
public:
    using QuantBot::QuantBot;
    void update() override {
        const double start = threadCpuMs();
        QuantBot::update();
        quantBotCpuMs[getHouse()->getHouseID()] += threadCpuMs() - start;
    }
};

class MeasuredQuantBots {
    using Create = std::function<std::unique_ptr<Player>(House*, const std::string&)>;
    std::vector<std::pair<PlayerFactory::PlayerData*, Create>> original;
public:
    explicit MeasuredQuantBots(bool enabled) {
        if (!enabled) return;
        PlayerFactory::getList(); // Register once before taking stable entry addresses.
        for (auto& entry : PlayerFactory::playerDataList) {
            const auto& name = entry.getPlayerClass();
            QuantBot::Difficulty difficulty;
            if (name == "qBotEasy") difficulty = QuantBot::Difficulty::Easy;
            else if (name == "qBotMedium") difficulty = QuantBot::Difficulty::Medium;
            else if (name == "qBotHard") difficulty = QuantBot::Difficulty::Hard;
            else if (name == "qBotBrutal") difficulty = QuantBot::Difficulty::Brutal;
            else continue;
            original.emplace_back(&entry, entry.pCreate);
            entry.pCreate = [difficulty](House* house, const std::string& name) -> std::unique_ptr<Player> {
                return std::make_unique<MeasuredQuantBot>(house, name, difficulty, AIRole::Both);
            };
        }
        quantBotCpuMeasured = !original.empty();
    }
    MeasuredQuantBots(const MeasuredQuantBots&) = delete;
    MeasuredQuantBots& operator=(const MeasuredQuantBots&) = delete;
    ~MeasuredQuantBots() {
        for (auto& [entry, create] : original) entry->pCreate = std::move(create);
    }
};

} // namespace GamePerf
