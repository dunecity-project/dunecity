#ifndef ANALYTICSINSTALLID_H
#define ANALYTICSINSTALLID_H

#include <string>

class INIFile;

/// An anonymous number for this installation, so the public usage page can count players whose
/// games all carry the same default name (browser "Player", iPhone/iPad "Mobile"). It is 128 bits
/// from the platform's secure random generator as 32 lowercase hex characters, created once and
/// kept in the main settings file, so resetting the settings resets it. Nothing about the device,
/// account, name or network goes into it. Only the match analytics report sends it, on the row of
/// the reporting client's own human player; never in network packets or logs. The metaserver
/// stores only a keyed hash of it (metaserver/ANALYTICS.md, "Installation id", in the website repo).
namespace AnalyticsInstallID {

inline constexpr const char* section = "General";
inline constexpr const char* key = "Analytics Install ID";

/// True for exactly 32 lowercase hex characters.
bool isValid(const std::string& id);

/// A new id from std::random_device, or an empty string if the platform has no random source.
std::string generate();

/// The id stored in @a config. A missing or malformed one is replaced by a new id written to
/// @a config (not saved to disk); @a created is then true and the caller saves the file. Empty
/// only when no id could be generated, and then @a config is left unchanged.
std::string loadOrCreate(INIFile& config, bool& created);

/// What a match analytics player row adds: `,"install_id":"<id>"` for the reporting client's own
/// human player and a valid @a id, otherwise nothing.
std::string playerRowField(bool reportingClientsHumanPlayer, const std::string& id);

}

#endif
