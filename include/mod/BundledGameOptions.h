#ifndef BUNDLEDGAMEOPTIONS_H
#define BUNDLEDGAMEOPTIONS_H

#include <filesystem>
#include <fstream>
#include <string>

/**
    GameOptions.ini of the installer-managed vanilla and dunecity mods. ModManager seeds these
    files, and its installer identity (ModManager::installerContentHash) hashes exactly these
    bytes, so the text is LF-only and must reach the disk unchanged on every platform.
*/
namespace BundledGameOptions {

inline std::string vanilla() {
    return "# Vanilla Game Options (default values)\n"
           "[Game Options]\n"
           "Game Speed = 16\n"
           "Concrete Required = true\n"
           "Fog of War = false\n"
           "Start with Explored Map = false\n"
           "Instant Build = false\n"
           "Only One Palace = false\n"
           "Rocket-Turrets Need Power = false\n"
           "Sandworms Respawn = false\n"
           "Killed Sandworms Drop Spice = false\n"
           "Manual Carryall Drops = false\n"
           "Maximum Number of Units Override = 0\n"
           "Maximum Number of Harvesters Override = -1\n"
           "City Effects = false\n";
}

inline std::string city() {
    return "# Dune City Game Options (default values)\n"
           "[Game Options]\n"
           "Game Speed = 16\n"
           "Concrete Required = false\n"
           "Fog of War = false\n"
           "Start with Explored Map = true\n"
           "Instant Build = false\n"
           "Only One Palace = false\n"
           "Rocket-Turrets Need Power = true\n"
           "Sandworms Respawn = true\n"
           "Killed Sandworms Drop Spice = true\n"
           // Dune City plays with large armies on crowded maps, where the carryall is the only
           // way out of a deadlock a human player cannot micro around. Vanilla, Tornie and Dune2R
           // keep the original default. A player's own choice for this mod lives in the config's
           // "Game Options dunecity" section and is layered over this file, so an explicit false
           // is never overwritten by a reseed.
           "Manual Carryall Drops = true\n"
           "Maximum Number of Units Override = 0\n"
           "Maximum Number of Harvesters Override = -1\n"
           "Immortal Human Player = false\n"
           "City Effects = true\n";  // dunecity mod opts in
}

/// Write @a text to @a path byte for byte. Binary: a text-mode stream writes CRLF on Windows,
/// which left the bundled mods unrecognised there (games re-activated as ws-<hash>, and the
/// vanilla mod ran with DuneCity power rules).
inline bool write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    file.close();
    return !file.fail();
}

} // namespace BundledGameOptions

#endif // BUNDLEDGAMEOPTIONS_H
