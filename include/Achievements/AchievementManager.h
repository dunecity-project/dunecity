// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef TORNIE_ACHIEVEMENT_MANAGER_H
#define TORNIE_ACHIEVEMENT_MANAGER_H

#include <Achievements/Achievement.h>
#include <deque>

namespace achievements {
class AchievementManager {
public:
    static AchievementManager& instance();
    bool configure(const std::string& catalog, const std::string& profile);
    void begin(const MatchInfo& info, const std::string& checkpoint = {}, bool resumed = false);
    void end();
    void suspend();
    void enemyDamage();
    void flameSource(std::uint32_t object);
    void wormSource(std::uint32_t object, unsigned source);
    void enemyDestroyed(std::uint32_t object, bool structure, bool worm, bool flame);
    void unitLost(std::uint32_t object);
    void built(bool structure, const std::string& exclusiveUnit = {});
    void captured();
    void sold(std::uint64_t refund);
    void crushed(std::uint32_t victim, std::uint32_t vehicle, std::uint64_t cycle);
    void refinedSpice(std::uint64_t total);
    void harvestedType(unsigned type);
    void bloom();
    void palace();
    void missile();
    void missileResult(bool hitObject, std::uint64_t enemyBuildingsDestroyed);
    void palaceSabotaged();
    void finish(bool won, bool enemiesRemain, bool localEliminated = false,
                bool allyEliminated = false, bool allySurvives = false);
    void campaignScore(int score);
    bool unlock(const std::string& id);
    bool save();
    void checkpoint(const std::string& key);
    bool completed() const;
    bool unlocked(const std::string& id) const;
    std::uint64_t statistic(const std::string& name) const;
    std::uint64_t progress(const Achievement& achievement) const;
    const std::vector<Achievement>& definitions() const { return catalog; }
    const MatchProgress& match() const { return active; }
    const std::string& error() const { return lastError; }
    std::string takeNotification();
    std::string displayName(const Achievement& a, bool french) const;
    std::string displayDescription(const Achievement& a, bool french) const;
    bool isActive() const { return running; }

private:
    using Section = std::map<std::string, std::string>;
    using Ini = std::map<std::string, Section>;
    std::vector<Achievement> catalog;
    Ini state;
    MatchProgress active;
    std::deque<std::string> notifications;
    std::string profilePath, lastError;
    bool running = false, dirty = false, writable = true;
    void increment(const std::string& name, std::uint64_t amount = 1);
    void setCount(const std::string& name, std::uint64_t value);
    std::uint64_t progress(const Achievement& achievement, bool revealSecret) const;
    void evaluate(bool victory = false, bool enemiesRemain = true);
    void encodeMatch(Section& section) const;
    bool decodeMatch(const Section& section, const MatchInfo& info);
    static Ini parse(const std::string& text);
    static std::uint64_t number(const std::string& text);
};
}
#endif
