/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef DUNE2RASSETMANAGER_H
#define DUNE2RASSETMANAGER_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct Dune2RAssetFile {
    std::string relativePath;
    uint64_t size = 0;
    std::string sha256;
};

struct Dune2RAssetPack {
    std::string id;
    std::string displayName;
    std::string variant;
    std::string unit;
    std::vector<Dune2RAssetFile> files;

    uint64_t totalBytes() const;
};

struct Dune2RAssetProgress {
    std::string packName;
    std::string filename;
    uint64_t completedBytes = 0;
    uint64_t totalBytes = 0;
};

struct Dune2RAssetInstallResult {
    bool success = false;
    bool changed = false;
    std::string message;
};

class Dune2RAssetManager final {
public:
    using ProgressCallback = std::function<bool(const Dune2RAssetProgress&)>;

    explicit Dune2RAssetManager(const std::string& dune2rModPath);

    const std::vector<Dune2RAssetPack>& getPacks() const noexcept;
    const std::string& getRevision() const noexcept;
    bool isReadOnly() const noexcept;
    bool isPackInstalled(const Dune2RAssetPack& pack) const;
    Dune2RAssetInstallResult refreshCatalog();
    Dune2RAssetInstallResult applyCatalog(const std::string& contents);

    Dune2RAssetInstallResult install(const std::vector<std::string>& packIDs,
                                     const ProgressCallback& progress = {}) const;

    static bool isSafeRelativeAssetPath(const std::string& path);
    static std::string sha256Bytes(const std::string& bytes);
    static std::string sha256File(const std::string& filename);

private:
    const Dune2RAssetPack* findPack(const std::string& id) const;
    void loadCatalog();
    void parseCatalog(const std::string& contents);

    std::string modPath;
    bool readOnly = false;
    std::string baseURL;
    std::string revision;
    std::vector<Dune2RAssetPack> packs;
};

#endif // DUNE2RASSETMANAGER_H
