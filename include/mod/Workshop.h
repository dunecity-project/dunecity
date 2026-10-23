#ifndef DUNECITY_WORKSHOP_H
#define DUNECITY_WORKSHOP_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Workshop {
struct File { std::string path, hash; uint64_t size = 0; };
struct Revision {
    std::string kind, id, name, base, hash, modHash;
    unsigned version = 0;
    std::string directory;
    std::vector<File> files;
    std::string manifest;
};
// Immutable local revisions. Throws on incomplete saves, invalid paths or hashes.
class Store {
public:
    explicit Store(std::filesystem::path root);
    Revision capture(const std::string& kind, const std::string& id,
                     const std::string& name, const std::string& base,
                     const std::string& modHash, const std::filesystem::path& source);
    Revision importRevision(const std::string& manifest, const std::string& expectedHash,
                            unsigned version, const std::filesystem::path& source);
    Revision get(const std::string& hash) const;
    void verifyDirectory(const Revision& revision, const std::filesystem::path& directory) const;
    std::vector<Revision> list(const std::string& kind = {}) const;
    std::string owner();
    void setSharedVersion(const std::string& hash, unsigned version);
    std::filesystem::path root() const { return root_; }
private:
    std::filesystem::path root_;
};
std::string hex(const std::string& bytes);
std::string unhex(const std::string& text);
std::string newID();
void replaceFile(const std::filesystem::path& source, const std::filesystem::path& destination);
std::string hashBytes(const std::string& bytes);
Revision parseManifest(const std::string& manifest);
Store& store();
Revision saveMod(const std::string& modName);
Revision saveMap(const std::string& filename, const std::string& modName);
Revision saveMapData(const std::string& name, const std::string& data, const std::string& modHash);
Revision receiveMap(const std::string& name, const std::string& data,
                    const std::string& expectedHash, const std::string& manifest = {},
                    unsigned version = 0);
// Installs an exact cached mod revision under an immutable hash-specific folder name.
std::string installMod(const Revision& revision);
std::string installMap(const Revision& revision);
bool activateModRevision(const std::string& hash);
}
#endif
