#ifndef DUNECITY_WORKSHOP_GAME_CONTENT_H
#define DUNECITY_WORKSHOP_GAME_CONTENT_H
#include <GameInitSettings.h>
#include <Network/OnlineModPolicy.h>
#include <mod/ModManager.h>
#include <mod/Workshop.h>
#include <mod/WorkshopClient.h>
#include <misc/FileSystem.h>
#include <misc/IMemoryStream.h>
#include <FileClasses/INIFile.h>
#include <stdexcept>

namespace WorkshopGameContent {
inline bool isSave(const GameInitSettings& init) {
    return init.getGameType() == GameType::LoadSavegame || init.getGameType() == GameType::LoadMultiplayer
        || init.getGameType() == GameType::LoadCoop;
}
inline void resolveMod(GameInitSettings& init, bool allowDownload = true) {
    auto& mods = ModManager::instance();
    if(!init.getModRevisionHash().empty()) {
        // Approved mods are already installed. Keep their shipped identity instead
        // of installing a second, hash-named copy just to join a game.
        if(OnlineModPolicy::approved(init.getModName()) && mods.modExists(init.getModName())
           && Workshop::saveMod(init.getModName()).hash == init.getModRevisionHash()) {
            if(mods.getActiveModName() != init.getModName() && !mods.setActiveMod(init.getModName()))
                throw std::runtime_error("Could not load the approved mod.");
            init.setModIdentity(mods.getActiveModName(), mods.getEffectiveChecksums().combined);
            return;
        }
        bool available = Workshop::activateModRevision(init.getModRevisionHash());
        if(!available) {
            // Fresh installations may already have the exact bundled package but no
            // local revision record yet. Verify those bytes before requiring the server.
            const auto current = mods.getActiveModName();
            try {
                if(Workshop::saveMod(current).hash == init.getModRevisionHash())
                    available = Workshop::activateModRevision(init.getModRevisionHash());
            } catch(const std::exception&) { }
            if(!available) for(const auto& candidate : mods.listMods()) {
                if(candidate.name == current || candidate.name.rfind("ws-", 0) == 0) continue;
                try {
                    if(Workshop::saveMod(candidate.name).hash == init.getModRevisionHash()) {
                        available = Workshop::activateModRevision(init.getModRevisionHash());
                        if(available) break;
                    }
                } catch(const std::exception&) { }
            }
        }
        if(!available && (!allowDownload || !Workshop::downloadWithProgress(init.getModRevisionHash())
           || !Workshop::activateModRevision(init.getModRevisionHash())))
            throw std::runtime_error("The exact mod version for this game could not be loaded. Connect to the community server to download it.");

    } else {
        if(mods.getActiveModName() != init.getModName() && !mods.setActiveMod(init.getModName()))
            throw std::runtime_error("This game's mod is not installed.");
        if(!init.getModChecksum().empty() && mods.getEffectiveChecksums().combined != init.getModChecksum())
            throw std::runtime_error("This game needs an older mod version. Its installed files have changed.");
    }
    init.setModIdentity(mods.getActiveModName(), mods.getEffectiveChecksums().combined);
}
// Selecting a map for a NEW custom game: the mod the player picked is authoritative, so
// this never activates another mod. The authored revision of these exact bytes is reused
// only when it already belongs to the active mod; otherwise no map revision is set and
// pin() captures the same bytes as a revision of the selected mod, leaving the authored
// revision untouched. Saves and joins keep their exact pinned mod through resolveMod().
inline bool applyMapRevisionForSelectedMod(const std::string& path, GameInitSettings& init) {
    if(!existsFile(path + ".workshop.ini")) return false;
    INIFile metadata(path + ".workshop.ini");
    const auto hash = metadata.getStringValue("Workshop", "Hash", "");
    // Bundled scenarios may carry catalogue hints without a locally cached revision.
    // Capture their bytes under the selected mod when the game starts. A nonempty
    // revision still has to resolve and pass the exact-content checks below.
    if(hash.empty()) return false;
    const auto map = Workshop::store().get(hash);
    if(map.kind != "map" || map.files.empty() || map.files.front().hash != Workshop::hashBytes(init.getFiledata())) return false;
    Workshop::Revision selected;
    try { selected = Workshop::saveMod(ModManager::instance().getActiveModName()); }
    catch(const std::exception&) { return false; }
    if(map.modHash != selected.hash) return false;
    init.setMapRevision(map.hash, map.version, map.manifest);
    return true;
}
// Called before advertising/starting. Save/checkpoint bytes are never treated as map INI files.
inline void pin(GameInitSettings& init, bool publish = false, bool queue = false) {
    auto& mods = ModManager::instance();
    Workshop::Revision mod;
    if(isSave(init) && init.getModRevisionHash().empty()) {
        const auto data = init.getFiledata().empty() ? readCompleteFile(init.getFilename()) : init.getFiledata();
        IMemoryStream stream(data.data(), data.size());
        GameInitSettings::HouseInfoList houses;
        auto saved = GameInitSettings::readSaveSetup(stream, houses);
        init.setModIdentity(saved.getModName(), saved.getModChecksum());
        init.setModRevision(saved.getModRevisionHash(), saved.getModRevisionVersion());
        init.setMapRevision(saved.getMapRevisionHash(), saved.getMapRevisionVersion(), saved.getMapRevisionManifest());
        resolveMod(init);
    }
    if(!init.getModRevisionHash().empty()) resolveMod(init);
    const bool approved = OnlineModPolicy::approved();
    if(approved) {
        // Approved packages avoid a blocking upload before play, but their
        // hosted maps still belong in the metaserver catalogue.
        queue = queue || publish;
        publish = false;
        if(init.getModRevisionHash().empty()
           && init.getGameType() != GameType::CustomGame && init.getGameType() != GameType::CustomMultiplayer) {
            init.setModIdentity(mods.getActiveModName(), mods.getEffectiveChecksums().combined);
            return;
        }
    }
    if(!init.getModRevisionHash().empty()) {
        mod = Workshop::store().get(init.getModRevisionHash());
    } else {
        mod = Workshop::saveMod(mods.getActiveModName());
        if(!approved && !Workshop::activateModRevision(mod.hash)) throw std::runtime_error("Could not load the saved mod revision.");
        init.setModRevision(mod.hash, mod.version);
        init.setModIdentity(mods.getActiveModName(), mods.getEffectiveChecksums().combined);
    }
    if(queue) Workshop::queuePublish(mod);
    if(queue && isSave(init) && !init.getMapRevisionHash().empty())
        Workshop::queuePublish(Workshop::store().get(init.getMapRevisionHash()));
    if(publish && !Workshop::publishWithProgress(mod, false))
        throw std::runtime_error("The mod could not be shared. The game has not been advertised.");
    if(publish) { mod = Workshop::store().get(mod.hash); init.setModRevision(mod.hash, mod.version); }
    if(init.getGameType() == GameType::CustomGame || init.getGameType() == GameType::CustomMultiplayer) {
        Workshop::Revision map;
        if(init.getMapRevisionHash().empty()) {
            map = Workshop::saveMapData(init.getFilename(), init.getFiledata(), mod.hash);
        } else {
            map = Workshop::store().get(init.getMapRevisionHash());
        }
        if(map.modHash != mod.hash) throw std::runtime_error("The selected map revision requires a different mod revision.");
        init.setMapRevision(map.hash, map.version, map.manifest);
        if(queue) Workshop::queuePublish(map);
        if(publish && !Workshop::publishWithProgress(map, false))
            throw std::runtime_error("The map could not be shared. The game has not been advertised.");
        if(publish) { map = Workshop::store().get(map.hash); init.setMapRevision(map.hash, map.version, map.manifest); }
    }
}
// Does the mod name the host announced denote this game's pinned revision?
// A captured revision is activated under its hash name, but an approved mod keeps
// its shipped installer name (see resolveMod), so the name alone cannot identify
// it. Accept that name only when the locally installed bytes of that very mod
// hash to the pinned revision - the same verification joining already performs.
inline bool announcesRevision(const std::string& modName, const GameInitSettings& init) {
    if(init.getModRevisionHash().empty()) return true;
    if(modName == "ws-" + init.getModRevisionHash()) return true;
    if(!OnlineModPolicy::approved(modName)) return false;
    try { return Workshop::saveMod(modName).hash == init.getModRevisionHash(); }
    catch(const std::exception&) { return false; }
}
inline bool matches(const GameInitSettings& init) {
    if(init.getModRevisionHash().empty()) return true;
    try {
        return Workshop::saveMod(ModManager::instance().getActiveModName()).hash == init.getModRevisionHash();
    } catch(const std::exception&) { return false; }
}
}
#endif
