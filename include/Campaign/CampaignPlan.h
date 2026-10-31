// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef CAMPAIGNPLAN_H
#define CAMPAIGNPLAN_H

#include <FileClasses/INIFile.h>

#include <algorithm>
#include <array>
#include <optional>
#include <regex>
#include <string>
#include <string_view>

namespace campaign {

struct CampaignPlan {
    char templateLetter = 'H';
    std::array<std::string, 3> opponents;
    int openingQuota = 0;
    int revision = 0;
    std::string persistentSupport;
    std::array<std::array<std::string, 4>, 9> stageOpponents;
};

/// Campaign roles are data-driven; the 546 stage table is absent in the older editions.
inline std::optional<CampaignPlan> readCampaignPlan(const INIFile& ini, const std::string& section) {
    const auto letter = ini.getStringValue(section, "Template", "");
    if(letter != "H" && letter != "A" && letter != "O") return std::nullopt;
    CampaignPlan plan;
    plan.templateLetter = letter[0];
    for(int role = 0; role < 3; ++role)
        plan.opponents[role] = ini.getStringValue(section, "Opponent" + std::to_string(role + 1), "");
    if(std::any_of(plan.opponents.begin(), plan.opponents.end(), [](const auto& name) { return name.empty(); }))
        return std::nullopt;
    plan.openingQuota = ini.getIntValue(section, "OpeningQuota", 0);
    plan.revision = ini.getIntValue(section, "CampaignRevision", 0);
    plan.persistentSupport = ini.getStringValue(section, "PersistentSupport", "");
    if(plan.revision == 2) {
        for(int stage = 0; stage < 9; ++stage) {
            for(int role = 0; role < 4; ++role) {
                plan.stageOpponents[stage][role] = ini.getStringValue(section,
                    "Stage" + std::to_string(stage + 1) + "Opponent" + std::to_string(role + 1),
                    role == 1 ? plan.persistentSupport : "");
            }
        }
    }
    return plan;
}

/// Substitute whole faction/Emperor names once, so replacement names cannot be replaced again.
inline std::string replaceCampaignHouseNames(const std::string& original,
                                             const std::array<std::string_view, 4>& names) {
    static const std::regex tokens(R"(\b(Harkonnen|Atreides|Ordos|Sardaukars?|Emperor|Empereur)\b)", std::regex::icase);
    std::string result;
    result.reserve(original.size());
    std::size_t cursor = 0;
    for(std::sregex_iterator i(original.begin(), original.end(), tokens), end; i != end; ++i) {
        const auto position = static_cast<std::size_t>(i->position());
        result.append(original, cursor, position - cursor);
        const char first = static_cast<char>(original[position] | 0x20);
        result += names[first == 'h' ? 0 : first == 'a' ? 1 : first == 'o' ? 2 : 3];
        cursor = position + static_cast<std::size_t>(i->length());
    }
    result.append(original, cursor, std::string::npos);
    return result;
}

} // namespace campaign

#endif // CAMPAIGNPLAN_H
