#ifndef COOP_CAMPAIGN_RUNTIME_H
#define COOP_CAMPAIGN_RUNTIME_H

#include <Campaign/CoopCampaignSession.h>
#include <GameInitSettings.h>

namespace coop {

std::string readMissionTemplate(const CoopCampaignSession& session);
GameInitSettings makeGameSettings(const CoopCampaignSession& session,
    const SettingsClass::GameOptionsClass& options, const std::string& serverName,
    bool multiplayer = true);
std::string progressPath(const Context& context);
std::string checkpointPath(const Context& context);
GameInitSettings makeSavedGameSettings(const std::string& path,
    const std::string& serverName, const std::string& requiredMod);

// Recognizes wrapped checkpoints too; never selects the legacy same-house loop.
bool isCoopCampaignSettings(const GameInitSettings& settings);

// The caller's lobby owns the direct connections for the entire campaign.
// CustomGame is also supported for offline sessions with AI allies.
void runMultiplayerSession(const GameInitSettings& initialSettings);

} // namespace coop
#endif
