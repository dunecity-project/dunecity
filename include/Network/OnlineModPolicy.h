#ifndef DUNECITY_ONLINE_MOD_POLICY_H
#define DUNECITY_ONLINE_MOD_POLICY_H
#include <mod/ModManager.h>
#include <mod/Workshop.h>
#include <array>
#include <string>
#include <stdexcept>

namespace OnlineModPolicy {
// These are the approved mods shipped with the game. Editable copies never
// inherit approval from Base Mod; they must synchronize in a pregame lobby.
inline const std::array<std::string, 9>& approvedMods() {
    // Keep the original four token indices stable.
    static const std::array<std::string, 9> names{{
        "vanilla", "dunecity", "Tornie", "Dune2R",
        "TornieLite", "Jericho", "JerichoLite", "ArcaKev", "ArcaKevLite"
    }};
    return names;
}
inline bool approved(const std::string& name) {
    for(const auto& candidate : approvedMods()) if(name == candidate) return !ModManager::instance().installerContentHash(name).empty();
    return false;
}
inline bool approved() { return approved(ModManager::instance().getActiveModName()); }
inline std::string approvedName(const std::string& fingerprint) {
    if(fingerprint.size() != 64 || fingerprint.find_first_not_of("0123456789abcdef") != std::string::npos
       || fingerprint.compare(0, 15, "d00ec17a0000000") != 0) return {};
    const int index = fingerprint[15] - '1';
    return index >= 0 && static_cast<std::size_t>(index) < approvedMods().size()
        ? approvedMods()[index] : std::string();
}
inline std::string fingerprint() {
    auto& mods = ModManager::instance();
    const auto name = mods.getActiveModName();
    for(size_t i = 0; i < approvedMods().size(); ++i) {
        if(name != approvedMods()[i] || !approved(name)) continue;
        const auto checksum = mods.getEffectiveChecksums().combined;
        if(checksum.size() != 16 || checksum.find_first_not_of("0123456789abcdef") != std::string::npos)
            throw std::runtime_error("Could not verify the approved mod's rules.");
        // Domain-separated, self-identifying content token, not a Workshop revision.
        // Include the installer payload digest as well as the effective rules.
        return "d00ec17a0000000" + std::to_string(i+1)
            + Workshop::hashBytes("approved/" + name + "/" + checksum + "/" + mods.installerContentHash(name)).substr(0, 48);
    }
    return Workshop::saveMod(name).hash;
}
inline bool activateApproved(const std::string& token) {
    const auto name = approvedName(token);
    if(name.empty()) return false;
    auto& mods = ModManager::instance();
    const auto previous = mods.getActiveModName();
    if(previous != name && !mods.setActiveMod(name)) return false;
    try { if(fingerprint() == token) return true; }
    catch(const std::exception&) { }
    if(previous != name) mods.setActiveMod(previous);
    return false;
}
// 738-740 persisted automatically-created snapshots as the selected mod. Return
// those generated approved selections to the shipped mod, without changing any
// saved game or deleting the old revision. Authored copies retain their own IDs.
inline void restoreApprovedSelection() {
    auto& mods = ModManager::instance();
    const auto name = mods.getActiveModName();
    if(name.rfind("ws-", 0) != 0) return;
    try {
        auto revision = Workshop::store().get(name.substr(3));
        if(!approved(revision.base)) return;
        const auto id = revision.id;
        auto manifest = revision.manifest;
        const auto field = manifest.find("\nid=" + id + "\n");
        if(field == std::string::npos) return;
        manifest.replace(field + 4, id.size(), std::string(32, '0'));
        if(Workshop::hashBytes(manifest).substr(0,32) == id)
            mods.setActiveMod(revision.base);
    } catch(const std::exception&) { }
}
}
#endif
