// Dune Legacy Tornie achievements. SPDX-License-Identifier: GPL-2.0-or-later
#ifndef TORNIE_ACHIEVEMENT_H
#define TORNIE_ACHIEVEMENT_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace achievements {
enum class Mode { Campaign, Skirmish, Custom, Multiplayer };

struct Achievement {
    std::string id, name, nameFr, description, descriptionFr;
    std::string rule, statistic;
    std::uint64_t target = 1;
    bool secret = false;
};

struct MatchInfo {
    Mode mode = Mode::Custom;
    std::string mod = "vanilla", house;
    int mission = 0;
    int color = 0, defaultColor = 0;
    bool enabled = true, highDifficulty = false, hadEnemies = false;
    bool chaosCampaignEligible = false, coopCompletionEligible = false, cooperative = false;
    std::string coopSession;
    std::uint64_t initialRefinedSpice = 0, roadkillWindow = 125;
};

struct MatchProgress {
    std::string runID;
    MatchInfo info;
    std::map<std::string, std::uint64_t> counts;
    std::set<std::uint32_t> destroyed, lost, flameSources;
    std::set<std::string> arsenal;
    std::map<std::uint32_t, std::vector<std::uint64_t>> crushes;
    std::map<std::uint32_t, unsigned> wormSources;
    unsigned spiceTypes = 0;
    bool historyKnown = true, damagedEnemy = false, destroyedEnemy = false;
    bool completed = false;
};
}
#endif
