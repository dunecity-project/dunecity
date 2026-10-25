#include <mod/Workshop.h>
#include <mod/Dune2RAssetManager.h>
#include <mod/ModTransferValidation.h>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <SDL.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Workshop {
namespace fs = std::filesystem;
namespace {
std::recursive_mutex storeMutex;
constexpr uint64_t maxFile = 128ull * 1024 * 1024, maxTotal = 2ull * 1024 * 1024 * 1024;
void require(bool value, const char* message) { if(!value) throw std::runtime_error(message); }
bool token(const std::string& s, size_t length) {
    return s.size() == length && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}
std::string read(const fs::path& path, uint64_t limit = 261120) {
    require(fs::is_regular_file(path) && !fs::is_symlink(path) && fs::file_size(path) <= limit,
            "Workshop file is missing or too large.");
    std::ifstream in(path, std::ios::binary);
    require(bool(in), "Cannot read Workshop file.");
    std::string data((std::istreambuf_iterator<char>(in)), {});
    require(!in.bad(), "Cannot read Workshop file."); return data;
}
void write(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    auto temp = path; temp += ".tmp-" + newID();
    try {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(data.data(), data.size()); out.close();
        require(bool(out), "Cannot save Workshop file. Check free disk space.");
        replaceFile(temp, path);
    } catch(...) { std::error_code e; fs::remove(temp,e); throw; }
}
uint64_t number(const std::string& s) {
    require(!s.empty() && s.size() <= 10 && (s == "0" || s.front() != '0')
            && s.find_first_not_of("0123456789") == std::string::npos, "Invalid Workshop size or version.");
    return std::stoull(s);
}
bool validDisplayName(const std::string& text) {
    bool nonblank=false;
    for(size_t i=0;i<text.size();) {
        const auto first=static_cast<unsigned char>(text[i++]);
        if(first<0x80) {
            if(first<0x20||first==0x7f)return false;
            nonblank=nonblank||first!=' ';
            continue;
        }
        nonblank=true;
        unsigned continuation=0;
        unsigned char minimum=0x80,maximum=0xbf;
        if(first>=0xc2&&first<=0xdf)continuation=1;
        else if(first>=0xe0&&first<=0xef) {
            continuation=2;
            if(first==0xe0)minimum=0xa0; // No overlong encoding.
            if(first==0xed)maximum=0x9f; // No UTF-16 surrogate code points.
        } else if(first>=0xf0&&first<=0xf4) {
            continuation=3;
            if(first==0xf0)minimum=0x90;
            if(first==0xf4)maximum=0x8f; // Unicode ends at U+10FFFF.
        } else return false;
        if(text.size()-i<continuation)return false;
        const auto second=static_cast<unsigned char>(text[i]);
        if(second<minimum||second>maximum)return false;
        for(unsigned n=0;n<continuation;++n) {
            const auto next=static_cast<unsigned char>(text[i++]);
            if(next<0x80||next>0xbf)return false;
        }
    }
    return nonblank;
}
bool safePath(const std::string& s) {
    fs::path out;
    for(const auto& part : fs::path(s)) if(!part.string().empty() && part.string().front()==' ') return false;
    return s.size() <= 240 && s.find('\\') == std::string::npos
        && std::all_of(s.begin(),s.end(),[](unsigned char c){return c >= 32 && c < 127;})
        && ModTransferValidation::normalizeRelativeFilePath(s,out) && out.generic_string() == s;
}
void checkFile(const fs::path& root, const File& file) {
    auto path = root;
    for(const auto& part : fs::path(file.path)) {
        path /= part; require(!fs::is_symlink(path), "Workshop packages cannot contain symbolic links.");
    }
    require(fs::is_regular_file(path) && fs::file_size(path) == file.size
            && Dune2RAssetManager::sha256File(path.string()) == file.hash,
            "Workshop content does not match its saved checksum.");
}
bool excluded(const fs::path& path) {
    for(const auto& part : path) {
        auto n = part.string();
        if(n.empty() || n.front() == '.' || n == "workshop-revision.ini" || n == "asset-catalog-online.ini") return true;
    }
    return false;
}
std::string serialize(const Revision& r) {
    std::string s = "DUNEWORKSHOP1\nkind=" + r.kind + "\nid=" + r.id + "\nname=" + hex(r.name)
                  + "\nbase=" + hex(r.base) + "\nmod=" + r.modHash + "\n";
    for(const auto& f : r.files) s += "file=" + f.hash + "," + std::to_string(f.size) + "," + hex(f.path) + "\n";
    return s;
}
// The revision capture() records for `source`: every file's path, size and SHA-256, with a
// bundled snapshot's ID derived from that content. `replaced` gives some files other bytes;
// `earlier` (checked against the current entries) records some files as an earlier payload had them.
Revision describe(const std::string& kind,const std::string& id,const std::string& name,const std::string& base,
                  const std::string& modHash,const fs::path& source,const std::map<std::string,std::string>& replaced,
                  const std::vector<PayloadChange>& earlier) {
    Revision r; r.kind=kind;r.id=id;r.name=name;r.base=base;r.modHash=modHash;
    for(const auto& e:fs::recursive_directory_iterator(source)) {
        auto path=e.path().lexically_relative(source);
        require(!e.is_symlink(),"Workshop packages cannot contain symbolic links.");
        if(e.is_directory() || excluded(path)) continue;
        require(e.is_regular_file(),"Workshop packages must contain regular files.");
        const auto relative=path.generic_string();
        const auto other=replaced.find(relative);
        if(other==replaced.end()) r.files.push_back({relative,Dune2RAssetManager::sha256File(e.path().string()),e.file_size()});
        else r.files.push_back({relative,hashBytes(other->second),other->second.size()});
    }
    for(const auto& change:earlier) {
        const auto current=std::find_if(r.files.begin(),r.files.end(),[&](const File& f){return f.path==change.path;});
        const std::string now=change.nowHash, then=change.thenHash;
        require(now.empty() ? current==r.files.end()
                            : current!=r.files.end() && current->hash==now && current->size==change.nowSize,
                "The installed payload is not the one this earlier payload was recorded against.");
        if(current!=r.files.end()) r.files.erase(current);
        if(!then.empty()) r.files.push_back({change.path,then,change.thenSize});
    }
    std::sort(r.files.begin(),r.files.end(),[](const File& a,const File& b){return a.path<b.path;});
    // Unowned bundled snapshots derive identity from their complete immutable content.
    // Optional asset packs must never compete for another publisher's item capability.
    if(r.id.empty()) { r.id=std::string(32,'0');r.id=hashBytes(serialize(r)).substr(0,32); }
    return r;
}
}
void replaceFile(const fs::path& source,const fs::path& destination) {
#ifdef _WIN32
    if(!MoveFileExW(source.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw fs::filesystem_error("Could not replace saved file",source,destination,
                                   std::error_code(GetLastError(),std::system_category()));
#else
    fs::rename(source,destination);
#endif
}
std::string hex(const std::string& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out; out.reserve(bytes.size()*2);
    for(unsigned char c : bytes) { out += digits[c>>4]; out += digits[c&15]; } return out;
}
std::string unhex(const std::string& s) {
    require(s.size()%2 == 0 && s.find_first_not_of("0123456789abcdef") == std::string::npos,"Invalid Workshop encoding.");
    auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};
    std::string out; out.reserve(s.size()/2);
    for(size_t i=0;i<s.size();i+=2) out += char((nibble(s[i])<<4)|nibble(s[i+1])); return out;
}
std::string newID() {
    std::random_device rng; std::string bytes;
    for(int i=0;i<16;++i) bytes += static_cast<char>(rng());
    return hex(bytes);
}
std::string hashBytes(const std::string& s) { return Dune2RAssetManager::sha256Bytes(s); }
Revision parseManifest(const std::string& manifest) {
    require(manifest.size() <= 261120 && !manifest.empty() && manifest.back() == '\n',"Invalid Workshop manifest.");
    std::istringstream in(manifest); std::string line; Revision r;
    std::getline(in,line); require(line == "DUNEWORKSHOP1","Unsupported Workshop format.");
    auto field=[&](const char* key){ std::getline(in,line); const std::string p=std::string(key)+"=";
        require(line.rfind(p,0)==0,"Invalid Workshop manifest fields."); return line.substr(p.size()); };
    r.kind=field("kind"); r.id=field("id"); r.name=unhex(field("name"));
    r.base=unhex(field("base")); r.modHash=field("mod");
    require((r.kind=="map"||r.kind=="mod") && token(r.id,32) && !r.name.empty() && r.name.size()<=128
            && validDisplayName(r.name)
            && (r.base.empty() || (r.base.size()<=64 && safePath(r.base) && r.base.find('/')==std::string::npos))
            && (r.modHash.empty() || token(r.modHash,64)) && (r.kind!="mod" || r.modHash.empty()),
            "Invalid Workshop identity or dependency.");
    std::set<std::string> paths; uint64_t total=0; std::string previous;
    while(std::getline(in,line)) {
        require(line.rfind("file=",0)==0,"Invalid Workshop file entry.");
        auto a=line.find(',',5),b=line.find(',',a==std::string::npos?a:a+1);
        require(a!=std::string::npos&&b!=std::string::npos,"Invalid Workshop file entry.");
        File f; f.hash=line.substr(5,a-5); f.size=number(line.substr(a+1,b-a-1)); f.path=unhex(line.substr(b+1));
        require(token(f.hash,64) && f.size<=maxFile && safePath(f.path) && f.path>previous
            && paths.insert(ModTransferValidation::portablePathKey(f.path)).second,"Unsafe or repeated Workshop file.");
        // A file must never also be another file's directory.
        for(auto parent=fs::path(f.path).parent_path(); !parent.empty();parent=parent.parent_path())
            require(paths.count(ModTransferValidation::portablePathKey(parent))==0,"Conflicting Workshop paths.");
        total += f.size; require(total<=maxTotal && r.files.size()<4096,"Workshop package is too large.");
        previous=f.path; r.files.push_back(f);
    }
    std::map<std::string,std::string> directories;
    for(const auto& f:r.files) {
        for(auto parent=fs::path(f.path).parent_path();!parent.empty();parent=parent.parent_path()) {
            auto key=ModTransferValidation::portablePathKey(parent);
            require(paths.count(key)==0,"Conflicting Workshop paths.");
            auto existing=directories.emplace(key,parent.generic_string());
            require(existing.second||existing.first->second==parent.generic_string(),"Directory case aliases are not portable.");
        }
    }
    require(!r.files.empty(),"Workshop package is empty.");
    require(r.kind!="map" || (r.files.size()==1 && r.files[0].path=="map.ini" && r.files[0].size<=1024*1024),"Invalid map package.");
    require(r.kind!="mod" || std::any_of(r.files.begin(),r.files.end(),[](const File& f){return f.path=="mod.ini";}),"Mod metadata is missing.");
    require(serialize(r)==manifest,"Workshop manifest is not canonical.");
    r.manifest=manifest; r.hash=hashBytes(manifest); return r;
}
Store::Store(fs::path root):root_(std::move(root)) {
    fs::create_directories(root_/"revisions");
    // Earlier releases queued automatic map/mod uploads here and retried them every
    // minute. Nothing reads that queue any more; remove only that folder, once. It
    // held hash-named markers, never revisions, saves or drafts.
    std::error_code error;
    const auto outbox=root_/"outbox";
    if(fs::is_directory(outbox,error)) {
        const auto removed=fs::remove_all(outbox,error);
        if(error) SDL_Log("Workshop: could not remove the retired sharing queue %s: %s",outbox.string().c_str(),error.message().c_str());
        else SDL_Log("Workshop: removed the retired automatic sharing queue (%ju items).",static_cast<uintmax_t>(removed));
    }
}
Revision Store::get(const std::string& hash) const {
    std::lock_guard<std::recursive_mutex> lock(storeMutex);
    require(token(hash,64),"Invalid Workshop checksum.");
    const auto dir=root_/"revisions"/hash;
    auto r=parseManifest(read(dir/"manifest")); require(r.hash==hash,"Workshop manifest checksum mismatch.");
    r.directory=(dir/"files").string(); r.version=static_cast<unsigned>(number(read(dir/"version",10)));
    for(const auto& f:r.files) checkFile(r.directory,f);
    return r;
}
std::vector<Revision> Store::list(const std::string& kind) const {
    std::lock_guard<std::recursive_mutex> lock(storeMutex); std::vector<Revision> out;
    for(const auto& e:fs::directory_iterator(root_/"revisions")) {
        if(!token(e.path().filename().string(),64)) continue;
        try { // Listing reads metadata only; activation/import verifies the complete package.
            auto r=parseManifest(read(e.path()/"manifest"));
            require(r.hash==e.path().filename().string(),"Invalid snapshot.");
            r.version=static_cast<unsigned>(number(read(e.path()/"version",10)));
            r.directory=(e.path()/"files").string();
            if(kind.empty()||r.kind==kind) out.push_back(std::move(r));
        } catch(...) { /* An incomplete cache entry is never advertised. */ }
    }
    std::sort(out.begin(),out.end(),[](const Revision& a,const Revision& b){return a.name==b.name?a.version>b.version:a.name<b.name;}); return out;
}
Revision Store::importRevision(const std::string& manifest,const std::string& expectedHash,unsigned version,const fs::path& source) {
    std::lock_guard<std::recursive_mutex> lock(storeMutex);
    auto r=parseManifest(manifest); require(r.hash==expectedHash,"Downloaded revision checksum does not match.");
    const auto destination=root_/"revisions"/r.hash;
    if(fs::exists(destination)) {
        try { return get(r.hash); }
        catch(const std::exception&) {
            // Keep evidence for recovery, but let a verified download repair a corrupt cache.
            fs::rename(destination,root_/"revisions"/(".corrupt-"+r.hash+"-"+newID()));
        }
    }
    unsigned next=1;
    for(const auto& old:list(r.kind)) if(old.id==r.id) {
        next=std::max(next,old.version+1); if(old.version==version) version=0;
    }
    r.version=version?version:next;
    auto stage=root_/"revisions"/(".stage-"+newID());
    try {
        fs::create_directories(stage/"files");
        for(const auto& f:r.files) {
            checkFile(source,f);
            fs::create_directories((stage/"files"/f.path).parent_path());
            fs::copy_file(source/f.path,stage/"files"/f.path);
            checkFile(stage/"files",f); // Verify the copied bytes, not a file that might have changed.
        }
        write(stage/"manifest",manifest); write(stage/"version",std::to_string(r.version));
        fs::rename(stage,root_/"revisions"/r.hash);
    } catch(...) { std::error_code e; fs::remove_all(stage,e); throw; }
    r.directory=(root_/"revisions"/r.hash/"files").string(); return r;
}
Revision Store::capture(const std::string& kind,const std::string& id,const std::string& name,
                        const std::string& base,const std::string& modHash,const fs::path& source) {
    std::lock_guard<std::recursive_mutex> lock(storeMutex);
    auto manifest=serialize(describe(kind,id,name,base,modHash,source,{},{}));
    return importRevision(manifest,hashBytes(manifest),0,source);
}
std::string contentHash(const std::string& kind,const std::string& id,const std::string& name,const std::string& base,
                        const std::string& modHash,const fs::path& source,const std::map<std::string,std::string>& replaced,
                        const std::vector<PayloadChange>& earlier) {
    if(earlier.empty()) return hashBytes(serialize(describe(kind,id,name,base,modHash,source,replaced,earlier)));
    try { return hashBytes(serialize(describe(kind,id,name,base,modHash,source,replaced,earlier))); }
    catch(const std::exception&) { return {}; }   // Not that earlier payload: no reconstruction.
}
void Store::verifyDirectory(const Revision& revision,const fs::path& directory) const {
    std::lock_guard<std::recursive_mutex> lock(storeMutex);
    require(!fs::is_symlink(directory),"Workshop folders cannot be symbolic links.");
    std::set<std::string> expected;
    for(const auto& f:revision.files){checkFile(directory,f);expected.insert(f.path);}
    for(const auto& e:fs::recursive_directory_iterator(directory)) {
        const auto relative=e.path().lexically_relative(directory);
        require(!e.is_symlink(),"Workshop packages cannot contain symbolic links.");
        if(e.is_directory()||excluded(relative))continue;
        require(e.is_regular_file()&&expected.count(relative.generic_string())!=0,"The saved revision contains unexpected files.");
    }
}
std::string Store::owner() {
    std::lock_guard<std::recursive_mutex> lock(storeMutex); auto path=root_/"owner";
    if(fs::exists(path)) { auto s=read(path,64); require(token(s,64),"Invalid Workshop publishing identity."); return s; }
    auto s=newID()+newID(); write(path,s);
    fs::permissions(path,fs::perms::owner_read|fs::perms::owner_write,fs::perm_options::replace); return s;
}
void Store::setSharedVersion(const std::string& hash,unsigned version) {
    std::lock_guard<std::recursive_mutex> lock(storeMutex); require(version>0,"Invalid shared version.");
    auto r=get(hash);
    unsigned next=version+1;
    auto revisions=list(r.kind);
    for(const auto& other:revisions) if(other.id==r.id) next=std::max(next,other.version+1);
    for(const auto& other:revisions) if(other.id==r.id && other.hash!=hash && other.version==version) {
        require(!fs::exists(root_/"revisions"/other.hash/"shared-version"),"Conflicting server version.");
        write(root_/"revisions"/other.hash/"version",std::to_string(next++));
    }
    write(root_/"revisions"/hash/"version",std::to_string(version));
    write(root_/"revisions"/hash/"shared-version",std::to_string(version));
}
}
