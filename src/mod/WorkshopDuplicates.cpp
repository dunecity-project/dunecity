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
#include <map>
#include <stdexcept>

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

}

std::string stampedModHash(const ShippedMod& mod, const std::string& gameVersion) {
    const fs::path ini = mod.path / "mod.ini";
    if(gameVersion.empty() || gameVersion == stampOf(mod.path))
        return contentHash("mod", "", revisionName(mod), mod.base, "", mod.path);
    if(!fs::is_regular_file(ini)) return {};
    // Change the value the way ModManager::writeModInfo() does: in place, every other byte kept.
    INIFile metadata(ini.string());
    if(!metadata.hasKey("Mod", "Game Version")) return {};
    metadata.setStringValue("Mod", "Game Version", gameVersion);
    std::string bytes(fs::file_size(ini) + gameVersion.size() + 2, '\0'); // +2: quotes
    auto rw = SDL_RWFromMem(bytes.data(), static_cast<int>(bytes.size()));
    if(rw == nullptr) return {};
    const bool written = metadata.saveChangesTo(rw);
    const auto size = SDL_RWtell(rw);
    SDL_RWclose(rw);
    if(!written || size < 0) return {};
    bytes.resize(static_cast<size_t>(size));
    return contentHash("mod", "", revisionName(mod), mod.base, "", mod.path, {{"mod.ini", bytes}});
}

std::vector<ShippedModCopy> findShippedModCopies(const fs::path& modsDir, const std::vector<ShippedMod>& shipped) {
    std::vector<ShippedModCopy> copies;
    std::error_code e;
    if(!fs::is_directory(modsDir, e)) return copies;
    std::map<std::pair<std::string, std::string>, std::string> stamped;   // (mod, stamp) -> hash
    for(const auto& entry : fs::directory_iterator(modsDir, e)) {
        const std::string folder = entry.path().filename().string();
        if(folder.rfind("ws-", 0) != 0 || !isHash(folder.substr(3)) || entry.is_symlink() || !entry.is_directory()) continue;
        const std::string hash = folder.substr(3);
        try {
            // Only an installed revision of a bundled snapshot: its pointer names the revision and
            // its id is derived from the content (contentHash with an empty id reproduces it).
            const fs::path pointer = entry.path() / "workshop-revision.ini";
            if(!fs::is_regular_file(pointer)) continue;
            INIFile meta(pointer.string());
            if(meta.getStringValue("Workshop", "Hash") != hash || !meta.getBoolValue("Workshop", "Immutable", false)) continue;
            // Older pointers have no Name; the manifest they carry names the revision exactly.
            std::string name = meta.getStringValue("Workshop", "Name"), base = meta.getStringValue("Workshop", "Base");
            if(const std::string manifest = meta.getStringValue("Workshop", "Manifest"); !manifest.empty()) {
                const Revision described = parseManifest(unhex(manifest));
                if(described.kind != "mod") continue;
                name = described.name;
                base = described.base;
            }
            const std::string stamp = stampOf(entry.path());
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
            if(contentHash("mod", "", name, base, "", entry.path()) != hash) {
                SDL_Log("Workshop: %s names a shipped %s revision but its files differ; kept", folder.c_str(), match->name.c_str());
                continue;
            }
            copies.push_back({folder, hash, match->name, stamp, folderBytes(entry.path())});
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
        // resolveMod re-stamps only a shipped mod stamped with this release, for an earlier one.
        if(!pin.gameVersion.empty() && pin.gameVersion != currentVersion && stampOf(mod.path) == currentVersion
           && stampedModHash(mod, pin.gameVersion) == copy.hash) return false;
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
                                  const std::string& activeMod, const std::string& currentVersion) {
    PruneReport report;
    for(const std::string& file : pins.unreadableNewer)
        SDL_Log("Workshop: %s was saved by a newer release; no duplicate mod copy is removed", file.c_str());
    for(ShippedModCopy& copy : findShippedModCopies(modsDir, shipped)) {
        std::string reason;
        if(!pins.unreadableNewer.empty()) reason = "a newer release's save may need it";
        else if(copy.folder == activeMod) reason = "it is the active mod";
        else for(const ModPin& pin : pins.pins)
            if(pinNeedsCopy(pin, copy, shipped, currentVersion)) { reason = "needed by " + pin.file; break; }
        if(reason.empty()) {
            const fs::path from = modsDir / copy.folder, doomed = modsDir / ("." + copy.folder + ".removing");
            std::error_code e;
            fs::remove_all(doomed, e);
            fs::rename(from, doomed, e);
            if(e) reason = "could not move it aside: " + e.message();
            else {
                fs::remove_all(doomed, e);
                if(e) SDL_Log("Workshop: %s could not be deleted completely (%s); a later start retries", doomed.string().c_str(), e.message().c_str());
                SDL_Log("Workshop: removed duplicate mod copy %s (%s, Game Version %s, %llu bytes): the shipped mod runs its saves",
                        copy.folder.c_str(), copy.shippedMod.c_str(), copy.gameVersion.c_str(), static_cast<unsigned long long>(copy.bytes));
                report.removed.push_back(copy);
                continue;
            }
        }
        if(copy.folder == activeMod || !pins.unreadableNewer.empty() || reason.rfind("could not", 0) == 0) report.complete = false;
        SDL_Log("Workshop: kept duplicate mod copy %s (%s, Game Version %s): %s",
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
