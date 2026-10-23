#ifndef DUNECITY_WORKSHOP_DUPLICATES_H
#define DUNECITY_WORKSHOP_DUPLICATES_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

/*
    Before 1.0.851 every release left Vanilla and Dune City saves pinned to a Workshop revision of
    the shipped mod whose mod.ini still said the saving release's Game Version, and loading such a
    save installed that revision as a `ws-<hash>` folder beside the shipped mod (Stefan's MBA had
    fourteen). Since 1.0.851 those saves run as the shipped mod (WorkshopGameContent::resolveMod,
    Workshop::bundledModHashAtGameVersion), so the copies are dead weight. This finds and removes
    them, once, where nothing still needs them; their Workshop store revisions stay.
*/
namespace Workshop {

/// A mod shipped with the game, named as saveMod() names its revisions.
struct ShippedMod {
    std::string name;         ///< "vanilla", "dunecity", ...
    std::string displayName;  ///< the revision name saveMod() records (display name, or the name)
    std::string base;         ///< ModManager::getContentBase(name)
    std::filesystem::path path;
};

/// What one saved game or replay needs: the mod it ran, its pinned revision and the release that
/// saved it (empty for a replay, which does not record it).
struct ModPin {
    std::string file;
    std::string modName;
    std::string revisionHash;
    std::string gameVersion;
};

/// A `ws-<hash>` folder holding exactly a shipped mod's files with only mod.ini's Game Version
/// value different (or none at all).
struct ShippedModCopy {
    std::string folder;       ///< "ws-<hash>"
    std::string hash;
    std::string shippedMod;   ///< the ShippedMod::name it duplicates
    std::string gameVersion;  ///< the copy's Game Version stamp
    uint64_t bytes = 0;
};

/**
    The revision hash saveMod() gives `mod` with its mod.ini's Game Version value changed to
    `gameVersion` in place (every other byte kept; the mod's own files are only read). An empty
    `gameVersion`, or the one mod.ini already says, hashes the files as they are. Empty when a
    different stamp is asked of a mod.ini without one.
*/
std::string stampedModHash(const ShippedMod& mod, const std::string& gameVersion);

/// Every `ws-<hash>` folder in `modsDir` that is an intact copy (its own files hash to <hash>) of
/// one of `shipped` apart from the Game Version value.
std::vector<ShippedModCopy> findShippedModCopies(const std::filesystem::path& modsDir,
                                                 const std::vector<ShippedMod>& shipped);

/**
    Would loading `pin` go through `copy` rather than the shipped mod? Mirrors
    WorkshopGameContent::resolveMod: a pin of the copy's revision runs as the shipped mod only when
    it names that mod and the revision is the mod's current bytes or its bytes stamped with the
    release that saved the game (`currentVersion` is this release, the stamp the shipped mod has).
*/
bool pinNeedsCopy(const ModPin& pin, const ShippedModCopy& copy, const std::vector<ShippedMod>& shipped,
                  const std::string& currentVersion);

/// Result of reading the saves and replays of a profile.
struct ProfilePins {
    std::vector<ModPin> pins;
    /// A save this release cannot read (written by a newer one): it may need any copy.
    std::vector<std::string> unreadableNewer;
};

/// The pins of every *.dls in `saveDirs` and *.rpl in `replayDirs`. Damaged or older unreadable
/// files are skipped with a log line: this release cannot load them anyway.
ProfilePins readProfilePins(const std::vector<std::filesystem::path>& saveDirs,
                            const std::vector<std::filesystem::path>& replayDirs);

struct PruneReport {
    std::vector<ShippedModCopy> removed;
    std::vector<std::pair<ShippedModCopy, std::string>> kept;   ///< with the reason
    bool complete = true;   ///< false: something may change on a later start (active copy, newer save)
};

/**
    Removes the copies findShippedModCopies() finds that no pin needs and that are not
    `activeMod`. Every copy is first renamed to a hidden name, so an interrupted removal never
    leaves a damaged `ws-<hash>` folder behind. Logs every removal and every copy kept.
*/
PruneReport pruneShippedModCopies(const std::filesystem::path& modsDir, const std::vector<ShippedMod>& shipped,
                                  const ProfilePins& pins, const std::string& activeMod,
                                  const std::string& currentVersion);

}

#endif
