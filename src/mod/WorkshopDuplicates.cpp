#include <mod/WorkshopDuplicates.h>
#include <mod/Workshop.h>
#include <FileClasses/INIFile.h>
#include <GameInitSettings.h>
#include <misc/FileSystem.h>
#include <misc/IFileStream.h>
#include <misc/IMemoryStream.h>
#include <Definitions.h>

#include <SDL.h>
#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string_view>

namespace Workshop {
namespace fs = std::filesystem;
namespace {

std::string revisionName(const ShippedMod& mod) { return mod.displayName.empty() ? mod.name : mod.displayName; }

bool isHash(const std::string& s) {
    return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}

/// The Game Version value of a mod folder's mod.ini; empty without one.
std::string stampOf(const fs::path& modPath) {
    const fs::path ini = modPath / "mod.ini";
    if(!fs::is_regular_file(ini)) return {};
    INIFile metadata(ini.string());
    return metadata.getStringValue("Mod", "Game Version");
}

uint64_t folderBytes(const fs::path& folder) {
    uint64_t total = 0;
    std::error_code e;
    for(fs::recursive_directory_iterator it(folder, e), end; !e && it != end; it.increment(e))
        if(it->is_regular_file(e)) total += it->file_size(e);
    return total;
}

std::string hashStampedMetadata(const ShippedMod& mod, INIFile& metadata, size_t metadataSize,
                                const std::string& gameVersion, const std::vector<PayloadChange>& earlier) {
    if(!metadata.hasKey("Mod", "Game Version")) return {};
    metadata.setStringValue("Mod", "Game Version", gameVersion);
    std::map<std::string, std::string> replaced;
    auto& bytes = replaced["mod.ini"];
    bytes.resize(metadataSize + gameVersion.size() + 2, '\0');
    auto rw = sdl2::RWops_ptr{SDL_RWFromMem(bytes.data(), static_cast<int>(bytes.size()))};
    if(!rw || !metadata.saveChangesTo(rw.get())) return {};
    const auto size = SDL_RWtell(rw.get());
    if(size < 0) return {};
    bytes.resize(static_cast<size_t>(size));
    return contentHash("mod", "", revisionName(mod), mod.base, "", mod.path, replaced, earlier);
}

// Dune City's graphics_skins as every release up to 1.0.853 shipped them, against the 1.0.855
// payload (Vuk's special-building Compacts, Hospitals/Nuclear Plants and sidebar portraits).
const PayloadChange dunecityPayload853[] = {
#include "DuneCityPayload853.inc"
};

}

std::vector<std::vector<PayloadChange>> earlierShippedPayloads(const std::string& modName) {
    if(modName == "dunecity")
        return {std::vector<PayloadChange>(std::begin(dunecityPayload853), std::end(dunecityPayload853))};
    return {};
}

std::string stampedModHash(const ShippedMod& mod, const std::string& gameVersion,
                           const std::vector<PayloadChange>& earlier) {
    const fs::path ini = mod.path / "mod.ini";
    if(gameVersion.empty() || gameVersion == stampOf(mod.path))
        return contentHash("mod", "", revisionName(mod), mod.base, "", mod.path, {}, earlier);
    if(!fs::is_regular_file(ini)) return {};
    // Keep every other byte, including comments and historical duplicate metadata keys.
    INIFile metadata(ini.string());
    return hashStampedMetadata(mod, metadata, fs::file_size(ini), gameVersion, earlier);
}

std::string legacyBundledModHash(const ShippedMod& mod, const std::string& gameVersion,
                                 const std::vector<PayloadChange>& earlier) {
    // Older builtin profiles retained a blank Version plus a later Version/Base Mod entry.
    // Reconstruct that observed layout, never accept a hash alias or discard payload differences.
    static constexpr std::string_view vanilla =
        "[Mod]\nDisplay Name = Vanilla\nAuthor = \"Dune City\"\n"
        "Description = \"Default game settings\"\nVersion = \nGame Version = 1.0.851\n"
        "Enables City Mode = false\nVersion = \"\"\nBase Mod = \"\"\n";
    static constexpr std::string_view city =
        "[Mod]\nDisplay Name = \"Dune City\"\nAuthor = Stefan\n"
        "Description = \"Hybrid RTS + city-builder mode (zones, overlays, city sim).\"\n"
        "Version = \nGame Version = 1.0.851\nEnables City Mode = true\n"
        "Version = 1.002\nBase Mod = \"\"\n";
    const auto legacy = mod.name == "vanilla" ? vanilla : mod.name == "dunecity" ? city : std::string_view{};
    const auto path = mod.path / "mod.ini";
    if(legacy.empty() || gameVersion.empty() || !fs::is_regular_file(path)) return {};
    INIFile current(path.string());
    // Only these cosmetic metadata fields may differ. Unknown sections (including Mentats)
    // and future keys fail closed; Base Mod and City Mode must retain their actual semantics.
    static constexpr const char* keys[] = {
        "Display Name", "Author", "Description", "Version", "Game Version", "Base Mod", "Enables City Mode"
    };
    for(const auto& section : current) {
        const auto sectionName = section.getSectionName();
        if(sectionName.empty() && section.begin() == section.end()) continue;
        if(SDL_strcasecmp(sectionName.c_str(), "Mod") != 0) return {};
        for(const auto& key : section) {
            const auto name = key.getKeyName();
            if(std::none_of(std::begin(keys), std::end(keys),
                            [&](const char* known) { return SDL_strcasecmp(name.c_str(), known) == 0; })) return {};
        }
    }
    if(!current.getStringValue("Mod", "Base Mod").empty()
       || current.getBoolValue("Mod", "Enables City Mode", false) != (mod.name == "dunecity")) return {};

    // Historical metadata is opaque checksum input, not valid INI: its blank Version
    // must remain byte-identical without sending it through the parser. Serialize only
    // the valid stamp fragment so quoting and its final newline still follow INIFile.
    static constexpr std::string_view stamp = "Game Version = 1.0.851\n";
    const auto stampOffset = legacy.find(stamp);
    const auto suffix = legacy.substr(stampOffset + stamp.size());
    auto input = sdl2::RWops_ptr{SDL_RWFromConstMem(stamp.data(), static_cast<int>(stamp.size()))};
    if(!input) return {};
    INIFile metadata(input.get());
    metadata.setStringValue("", "Game Version", gameVersion);
    std::map<std::string, std::string> replaced;
    auto& bytes = replaced["mod.ini"];
    bytes.resize(legacy.size() + gameVersion.size() + 2, '\0');
    std::copy_n(legacy.data(), stampOffset, bytes.data());
    auto output = sdl2::RWops_ptr{SDL_RWFromMem(bytes.data() + stampOffset,
                                             static_cast<int>(bytes.size() - stampOffset))};
    if(!output || !metadata.saveChangesTo(output.get())) return {};
    const auto size = SDL_RWtell(output.get());
    if(size < 0) return {};
    bytes.resize(stampOffset + static_cast<size_t>(size));
    bytes.append(suffix.data(), suffix.size());
    // All rules/assets remain the installed bytes. A changed payload cannot recreate the pin.
    return contentHash("mod", "", revisionName(mod), mod.base, "", mod.path, replaced, earlier);
}

namespace {
/// The name and base of the revision an installed `ws-<hash>` folder holds, or false when the folder
/// is no immutable installed revision of <hash>.
bool copyIdentity(const fs::path& folder, const std::string& hash, std::string& name, std::string& base) {
    const fs::path pointer = folder / "workshop-revision.ini";
    if(!fs::is_regular_file(pointer)) return false;
    INIFile meta(pointer.string());
    if(meta.getStringValue("Workshop", "Hash") != hash || !meta.getBoolValue("Workshop", "Immutable", false)) return false;
    // Older pointers have no Name; the manifest they carry names the revision exactly.
    name = meta.getStringValue("Workshop", "Name");
    base = meta.getStringValue("Workshop", "Base");
    if(const std::string manifest = meta.getStringValue("Workshop", "Manifest"); !manifest.empty()) {
        const Revision described = parseManifest(unhex(manifest));
        if(described.kind != "mod") return false;
        name = described.name;
        base = described.base;
    }
    return true;
}

/// Every `ws-<hash>` folder name in `modsDir`.
std::vector<std::string> installedCopies(const fs::path& modsDir) {
    std::vector<std::string> folders;
    std::error_code e;
    if(fs::is_directory(modsDir, e))
        for(const auto& entry : fs::directory_iterator(modsDir, e)) {
            const std::string folder = entry.path().filename().string();
            if(folder.rfind("ws-", 0) == 0 && isHash(folder.substr(3)) && !entry.is_symlink() && entry.is_directory())
                folders.push_back(folder);
        }
    std::sort(folders.begin(), folders.end());
    return folders;
}
}

std::vector<ShippedModCopy> findShippedModCopies(const fs::path& modsDir, const std::vector<ShippedMod>& shipped) {
    std::vector<ShippedModCopy> copies;
    std::error_code e;
    if(!fs::is_directory(modsDir, e)) return copies;
    std::map<std::pair<std::string, std::string>, std::string> stamped;   // (mod, stamp) -> hash
    for(const std::string& folder : installedCopies(modsDir)) {
        const fs::path path = modsDir / folder;
        const std::string hash = folder.substr(3);
        try {
            // Only an installed revision of a bundled snapshot: its id is derived from the content
            // (contentHash with an empty id reproduces it).
            std::string name, base;
            if(!copyIdentity(path, hash, name, base)) continue;
            const std::string stamp = stampOf(path);
            const ShippedMod* match = nullptr;
            for(const ShippedMod& mod : shipped) {
                if(revisionName(mod) != name || mod.base != base) continue;
                auto key = std::make_pair(mod.name, stamp);
                auto found = stamped.find(key);
                if(found == stamped.end()) found = stamped.emplace(key, stampedModHash(mod, stamp)).first;
                if(found->second == hash) { match = &mod; break; }
            }
            if(match == nullptr) continue;
            // The folder's own bytes must be that revision too: nothing else in it, nothing changed.
            if(contentHash("mod", "", name, base, "", path) != hash) {
                SDL_Log("Workshop: %s names a shipped %s revision but its files differ; kept", folder.c_str(), match->name.c_str());
                continue;
            }
            copies.push_back({folder, hash, match->name, stamp, folderBytes(path)});
        } catch(const std::exception& error) {
            SDL_Log("Workshop: could not check %s: %s", folder.c_str(), error.what());
        }
    }
    std::sort(copies.begin(), copies.end(), [](const auto& a, const auto& b) { return a.folder < b.folder; });
    return copies;
}

bool pinNeedsCopy(const ModPin& pin, const ShippedModCopy& copy, const std::vector<ShippedMod>& shipped,
                  const std::string& currentVersion) {
    if(pin.revisionHash != copy.hash) return false;
    for(const ShippedMod& mod : shipped) {
        if(mod.name != pin.modName) continue;
        if(stampedModHash(mod, {}) == copy.hash) return false;
        // Mirror resolveMod's recognised metadata variants, including an old layout re-saved
        // by this release, and its recognised earlier art payloads. Other payload changes still
        // need their archived revision.
        if(pin.gameVersion.empty() || stampOf(mod.path) != currentVersion) continue;
        std::vector<std::vector<PayloadChange>> payloads{{}};
        for(auto& earlier : earlierShippedPayloads(mod.name)) payloads.push_back(std::move(earlier));
        for(const auto& earlier : payloads)
            if((pin.gameVersion != currentVersion && stampedModHash(mod, pin.gameVersion, earlier) == copy.hash)
               || legacyBundledModHash(mod, pin.gameVersion, earlier) == copy.hash) return false;
    }
    return true;
}

ProfilePins readProfilePins(const std::vector<fs::path>& saveDirs, const std::vector<fs::path>& replayDirs) {
    ProfilePins result;
    const auto files = [](const fs::path& dir, const char* extension) {
        std::vector<fs::path> found;
        std::error_code e;
        if(fs::is_directory(dir, e))
            for(const auto& entry : fs::directory_iterator(dir, e))
                if(entry.is_regular_file() && entry.path().extension() == extension) found.push_back(entry.path());
        std::sort(found.begin(), found.end());
        return found;
    };
    for(const fs::path& dir : saveDirs) for(const fs::path& path : files(dir, ".dls")) {
        const std::string bytes = readCompleteFile(path.string());
        try {
            IMemoryStream stream(bytes.data(), static_cast<int>(bytes.size()));
            GameInitSettings::HouseInfoList houses;
            const auto saved = GameInitSettings::readSaveSetup(stream, houses);
            result.pins.push_back({path.string(), saved.getModName(), saved.getModRevisionHash(), saved.getSavedGameVersion()});
        } catch(const std::exception& error) {
            Uint32 version = 0;
            try {
                IMemoryStream header(bytes.data(), static_cast<int>(bytes.size()));
                if(header.readUint32() == SAVEMAGIC) version = header.readUint32();
            } catch(const std::exception&) { }
            if(version > SAVEGAMEVERSION) result.unreadableNewer.push_back(path.string());
            else SDL_Log("Workshop: save %s pins no mod this release can load (%s)", path.string().c_str(), error.what());
        }
    }
    for(const fs::path& dir : replayDirs) for(const fs::path& path : files(dir, ".rpl")) {
        try {
            IFileStream stream;
            if(!stream.open(path.string())) throw std::runtime_error("cannot open");
            stream.readString();   // the recording player's name
            const GameInitSettings recorded(stream);
            result.pins.push_back({path.string(), recorded.getModName(), recorded.getModRevisionHash(), {}});
        } catch(const std::exception& error) {
            SDL_Log("Workshop: replay %s pins no mod this release can read (%s)", path.string().c_str(), error.what());
        }
    }
    return result;
}

PruneReport pruneShippedModCopies(const fs::path& modsDir, const std::vector<ShippedMod>& shipped, const ProfilePins& pins,
                                  const std::string& activeMod, const std::string& currentVersion,
                                  const std::function<bool(const std::string&)>& inStore) {
    PruneReport report;
    for(const std::string& file : pins.unreadableNewer)
        SDL_Log("Workshop: %s was saved by a newer release; no stamp-only copy outside the store is removed", file.c_str());
    std::map<std::string, ShippedModCopy> stampOnly;
    for(ShippedModCopy& copy : findShippedModCopies(modsDir, shipped)) stampOnly.emplace(copy.hash, copy);
    const auto remove = [&](const ShippedModCopy& copy, const char* why) -> std::string {
        const fs::path from = modsDir / copy.folder, doomed = modsDir / ("." + copy.folder + ".removing");
        std::error_code e;
        fs::remove_all(doomed, e);
        fs::rename(from, doomed, e);
        if(e) return "could not move it aside: " + e.message();
        fs::remove_all(doomed, e);
        if(e) SDL_Log("Workshop: %s could not be deleted completely (%s); a later start retries", doomed.string().c_str(), e.message().c_str());
        SDL_Log("Workshop: removed mod copy %s (%s, Game Version %s, %llu bytes): %s",
                copy.folder.c_str(), copy.shippedMod.c_str(), copy.gameVersion.c_str(), static_cast<unsigned long long>(copy.bytes), why);
        report.removed.push_back(copy);
        return {};
    };
    for(const std::string& folder : installedCopies(modsDir)) {
        const std::string hash = folder.substr(3);
        ShippedModCopy copy;
        if(const auto found = stampOnly.find(hash); found != stampOnly.end()) copy = found->second;
        else {
            // Any other installed revision of a shipped mod (an older release's rules and art).
            std::string name, base;
            try { if(!copyIdentity(modsDir / folder, hash, name, base)) continue; }
            catch(const std::exception& error) { SDL_Log("Workshop: could not check %s: %s", folder.c_str(), error.what()); continue; }
            const auto mod = std::find_if(shipped.begin(), shipped.end(), [&](const ShippedMod& m) { return m.base == base || m.name == base; });
            if(mod == shipped.end()) continue;   // a revision built on no shipped mod is never touched
            copy = {folder, hash, mod->name, stampOf(modsDir / folder), folderBytes(modsDir / folder)};
        }
        std::string reason;
        if(copy.folder == activeMod) reason = "it is the active mod";
        else if(inStore && inStore(hash))
            reason = remove(copy, "its revision is in the Workshop store; a save that needs it reinstalls it");
        else if(stampOnly.count(hash)) {
            if(!pins.unreadableNewer.empty()) reason = "a newer release's save may need it";
            else for(const ModPin& pin : pins.pins)
                if(pinNeedsCopy(pin, copy, shipped, currentVersion)) { reason = "needed by " + pin.file; break; }
            if(reason.empty()) reason = remove(copy, "the shipped mod runs its saves");
        } else reason = "an older revision that is not in the Workshop store";
        if(reason.empty()) continue;
        if(copy.folder == activeMod || (stampOnly.count(hash) && !pins.unreadableNewer.empty()) || reason.rfind("could not", 0) == 0)
            report.complete = false;
        SDL_Log("Workshop: kept mod copy %s (%s, Game Version %s): %s",
                copy.folder.c_str(), copy.shippedMod.c_str(), copy.gameVersion.c_str(), reason.c_str());
        report.kept.emplace_back(copy, reason);
    }
    // Leftovers of an interrupted earlier removal.
    std::vector<fs::path> leftovers;
    std::error_code e;
    if(fs::is_directory(modsDir, e))
        for(const auto& entry : fs::directory_iterator(modsDir, e)) {
            const std::string name = entry.path().filename().string();
            if(name.rfind(".ws-", 0) == 0 && name.size() > 9 && name.compare(name.size() - 9, 9, ".removing") == 0)
                leftovers.push_back(entry.path());
        }
    for(const fs::path& leftover : leftovers) fs::remove_all(leftover, e);
    return report;
}

}
