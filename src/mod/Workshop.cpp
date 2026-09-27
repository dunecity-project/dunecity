#include <mod/Workshop.h>
#include <mod/ModManager.h>
#include <mod/Dune2RAssetManager.h>
#include <mod/ModTransferValidation.h>
#include <FileClasses/INIFile.h>
#include <misc/fnkdat.h>
#include <misc/WebRuntime.h>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace Workshop {
namespace fs = std::filesystem;
namespace {
std::string userPath(const char* name) {
    char path[FILENAME_MAX];
    if(fnkdat(name,path,sizeof(path),FNKDAT_USER|FNKDAT_CREAT)<0) throw std::runtime_error("Cannot open Workshop storage.");
    return path;
}
std::string readFile(const fs::path& path) {
    std::ifstream in(path,std::ios::binary);
    if(!in) throw std::runtime_error("Cannot read saved content.");
    std::string out((std::istreambuf_iterator<char>(in)),{});
    if(in.bad()) throw std::runtime_error("Cannot read saved content."); return out;
}
void atomicWrite(const fs::path& path,const std::string& data) {
    auto temp=path; temp += ".tmp-"+newID();
    try {
        std::ofstream out(temp,std::ios::binary);out.write(data.data(),data.size());out.close();
        if(!out) throw std::runtime_error("Could not save Workshop content.");
        replaceFile(temp,path);
    } catch(...) { std::error_code e;fs::remove(temp,e);throw; }
}
void pointer(const fs::path& path,const Revision& r,bool immutable=false) {
    INIFile ini(false, std::string("Workshop revision"));ini.setStringValue("Workshop","ID",r.id);
    ini.setIntValue("Workshop","Version",r.version);ini.setStringValue("Workshop","Hash",r.hash);
    ini.setStringValue("Workshop","Name",r.name);
    // The dependency's engine base is a catalogue hint, not part of the map's identity: the
    // exact mod revision stays pinned by the manifest this file's Hash names. A received map
    // is installed before that mod can be resolved - joining downloads it afterwards, and the
    // packet handler must not block on a download - so a dependency that is not cached yet
    // leaves the hint out instead of failing the download. Installing the map again once the
    // revision is present writes it.
    if(r.kind=="map"&&!r.modHash.empty()) {
        try { ini.setStringValue("Workshop","Mod",store().get(r.modHash).base); }
        catch(const std::exception& e) { SDL_Log("Workshop: map dependency not cached yet: %s",e.what()); }
    }
    ini.setStringValue("Workshop","Base",r.base);ini.setBoolValue("Workshop","Immutable",immutable);
    if(immutable && r.kind=="mod") ini.setStringValue("Workshop","Manifest",hex(r.manifest));
    const auto temp=path.string()+".tmp";
    if(!ini.saveChangesTo(temp)) throw std::runtime_error("Could not save revision metadata.");
    replaceFile(temp,path); WebRuntime::syncPersistentFiles();
}
std::string mapID(const std::string& name,const std::string& data) {
    auto rw=SDL_RWFromConstMem(data.data(),static_cast<int>(data.size()));
    INIFile ini(rw);SDL_RWclose(rw);
    auto id=ini.getStringValue("Workshop","ID","");
    if(id.size()==32&&id.find_first_not_of("0123456789abcdef")==std::string::npos) return id;
    // Old maps have no identity. Keep their named author lineage until first editor save.
    return hashBytes("legacy-map/"+store().owner()+"/"+name+"/"+ini.getStringValue("BASIC","Author","")).substr(0,32);
}
}
Store& store() { static Store value(userPath("workshop")); return value; }
Revision saveMod(const std::string& modName) {
    auto& manager=ModManager::instance();
    if(!manager.isValidModName(modName)||!manager.modExists(modName)) throw std::runtime_error("Choose an installed mod first.");
    const fs::path path=manager.getModPath(modName), metadata=path/"workshop-revision.ini";
    INIFile meta=fs::exists(metadata)?INIFile(metadata.string()):INIFile(false, std::string("Workshop revision"));
    const auto knownHash=meta.getStringValue("Workshop","Hash","");
    if(meta.getBoolValue("Workshop","Immutable",false)) {
        auto r=store().get(knownHash);
        store().verifyDirectory(r,path);
        return r;
    }
    auto info=manager.getModInfo(modName);
    auto id=meta.getStringValue("Workshop","ID","");
    const bool bundled=modName=="vanilla"||modName=="dunecity"||modName=="Tornie"||modName=="Dune2R";
    if(bundled) id.clear(); // Derive from complete content, including optional asset packs.
    else if(id.empty()) id=newID();
    // Materialize the effective rules rather than depending on a mutable vanilla fallback.
    for(const char* file:{"ObjectData.ini","QuantBot Config.ini","GameOptions.ini"}) {
        if(!fs::exists(path/file)) {
            const fs::path fallback=fs::path(manager.getModPath("vanilla"))/file;
            if(!fs::is_regular_file(fallback)) throw std::runtime_error("The mod is missing required base rules.");
            fs::copy_file(fallback,path/file);
        }
    }
    auto r=store().capture("mod",id,info.displayName.empty()?modName:info.displayName,
                           manager.getContentBase(modName),"",path);
    // Bundled identities derive from content; writing a pointer would alter
    // integrity-checked installer payloads such as Tornie.
    if(!bundled) pointer(metadata,r);
    return r;
}
Revision saveMapData(const std::string& name,const std::string& data,const std::string& modHash) {
    if(data.empty()||data.size()>1024*1024) throw std::runtime_error("Map content is empty or too large.");
    const auto stage=store().root()/(".map-"+newID());fs::create_directories(stage);
    try {
        atomicWrite(stage/"map.ini",data);
        auto r=store().capture("map",mapID(name,data),fs::path(name).stem().string(),"",modHash,stage);
        fs::remove_all(stage);WebRuntime::syncPersistentFiles();return r;
    } catch(...) {std::error_code e;fs::remove_all(stage,e);throw;}
}
Revision saveMap(const std::string& filename,const std::string& modName) {
    const auto data=readFile(filename);
    if(fs::exists(filename+".workshop.ini")) {
        INIFile metadata(filename+".workshop.ini");
        auto hash=metadata.getStringValue("Workshop","Hash","");
        if(!hash.empty()) {
            auto previous=store().get(hash);
            if(previous.kind=="map" && previous.files[0].hash==hashBytes(data)
               && (metadata.getBoolValue("Workshop","Immutable",false)
                   || saveMod(modName).hash==previous.modHash)) return previous;
        }
    }
    auto mod=saveMod(modName); auto r=saveMapData(fs::path(filename).filename().string(),data,mod.hash);
    pointer(filename+".workshop.ini",r); return r;
}
Revision receiveMap(const std::string& name,const std::string& data,const std::string& expectedHash,
                    const std::string& manifest,unsigned version) {
    auto r=parseManifest(manifest);
    if(r.kind!="map"||r.hash!=expectedHash||data.size()!=r.files[0].size||hashBytes(data)!=r.files[0].hash)
        throw std::runtime_error("Received map does not match the selected version.");
    const auto stage=store().root()/(".received-"+newID());fs::create_directories(stage);
    try {
        atomicWrite(stage/"map.ini",data);r=store().importRevision(manifest,expectedHash,version,stage);
        fs::remove_all(stage);installMap(r);return r;
    } catch(...) { std::error_code e;fs::remove_all(stage,e);throw; }
}
std::string installMod(const Revision& revision) {
    auto r=store().get(revision.hash);if(r.kind!="mod") throw std::runtime_error("That revision is not a mod.");
    const std::string name="ws-"+r.hash;
    const fs::path destination=ModManager::instance().getModPath(name);
    if(!fs::exists(destination)) {
        const auto stage=fs::path(destination.string()+".stage-"+newID());
        try {
            fs::create_directories(stage);
            // Recursive filesystem::copy is unavailable in Android libc++.
            // The verified manifest is also the exact set of files to install.
            for(const auto& file : r.files) {
                const auto target = stage / file.path;
                fs::create_directories(target.parent_path());
                fs::copy_file(fs::path(r.directory) / file.path, target);
            }
            pointer(stage/"workshop-revision.ini",r,true);
            fs::rename(stage,destination);
        } catch(...) {std::error_code e;fs::remove_all(stage,e);throw;}
    }
    // Existing installed copies are checked too; never quietly activate altered bytes.
    store().verifyDirectory(r,destination);
    pointer(destination/"workshop-revision.ini",r,true);
    WebRuntime::syncPersistentFiles();return name;
}
std::string installMap(const Revision& revision) {
    auto r=store().get(revision.hash);if(r.kind!="map") throw std::runtime_error("That revision is not a map.");
    std::string name;
    for(unsigned char c:r.name) name += (c>=32&&c<127&&std::string("<>:\"/\\|?*").find(c)==std::string::npos)?char(c):'_';
    if(name.empty()||!ModTransferValidation::isPortablePathComponent(name)) name="Map";
    const auto directory=fs::path(userPath("maps/multiplayer"));
    fs::create_directories(directory); // fnkdat creates parents; a fresh profile has no final map folder.
    auto path=directory/(name.substr(0,100)+".ini");
    if(fs::exists(path)) {
        bool sameMap = false;
        const auto metadataPath = path.string()+".workshop.ini";
        if(fs::exists(metadataPath)) {
            INIFile meta(metadataPath);
            sameMap = meta.getStringValue("Workshop","ID","") == r.id;
        }
        if(!sameMap) path=directory/(name.substr(0,80)+" - "+r.id.substr(0,8)+".ini");
    }
    if(fs::exists(path) && Dune2RAssetManager::sha256File(path.string())!=r.files[0].hash) {
        if(!fs::exists(path.string()+".workshop.ini")) throw std::runtime_error("A local map occupies the download destination.");
        INIFile meta(path.string()+".workshop.ini");
        auto old=store().get(meta.getStringValue("Workshop","Hash",""));
        if(old.id!=r.id || Dune2RAssetManager::sha256File(path.string())!=old.files[0].hash)
            throw std::runtime_error("The local map has edits. Save them under another name before downloading this revision.");
    }
    atomicWrite(path,readFile(fs::path(r.directory)/"map.ini"));
    pointer(path.string()+".workshop.ini",r,true);return path.string();
}
bool activateModRevision(const std::string& hash) {
    try { return ModManager::instance().setActiveMod(installMod(store().get(hash))); }
    catch(const std::exception& e) { SDL_Log("Workshop: %s",e.what());return false; }
}
}
