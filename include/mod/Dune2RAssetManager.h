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

/// Result of comparing the installed art against every pack the catalog describes.
struct Dune2RAssetReadiness {
    bool complete = false;                      ///< every catalogued file verified in its final location
    std::vector<std::string> missingPackIDs;    ///< packs with at least one missing or changed file
    uint64_t missingBytes = 0;                  ///< bytes that still have to be fetched
    uint64_t totalBytes = 0;                    ///< bytes of the complete target
    std::string revision;                       ///< asset revision this target was verified against
    uint64_t catalogVersion = 0;                ///< monotonic catalog version of that target
    std::string fingerprint;                    ///< cheap key; a change invalidates a cached result
};

class Dune2RAssetManager final {
public:
    using ProgressCallback = std::function<bool(const Dune2RAssetProgress&)>;
    /// Reports progress while checkReadiness() checksums the catalogued files, with
    /// the bytes already checked and the total of the complete target. Returning false
    /// abandons the check; an abandoned check never reports a complete target and
    /// carries no fingerprint, so nothing about it can be cached or started from.
    using VerifyCallback = std::function<bool(uint64_t checkedBytes, uint64_t totalBytes)>;

    explicit Dune2RAssetManager(const std::string& dune2rModPath);

    const std::vector<Dune2RAssetPack>& getPacks() const noexcept;
    const std::string& getRevision() const noexcept;
    /// Publication counter of the loaded catalog; 0 for legacy catalogs that predate
    /// the key. The counter is carried forward by the generator, because neither a
    /// revision hash nor a commit date can order two published catalogs. It decides
    /// whether a refreshed catalog is a newer publication than the installed one.
    uint64_t getCatalogVersion() const noexcept;
    /// Digest of everything that identifies the artwork target: revision, base URL and
    /// every pack's metadata with its expected file paths, sizes and hashes. One
    /// publication counter must never describe two different identities.
    const std::string& getTargetIdentity() const noexcept;
    /// Pack IDs the bundled catalog requires. A published catalog may add packs, but it
    /// may never remove one this application ships. Empty for a legacy bundled catalog.
    const std::vector<std::string>& getRequiredPackIDs() const noexcept;
    bool isReadOnly() const noexcept;
    bool isPackInstalled(const Dune2RAssetPack& pack) const;

    /// Checksum-verify every catalogued pack in its final (non-staging) location.
    /// Checksumming the published target reads hundreds of megabytes, so a caller
    /// that has to stay responsive on one thread passes a callback and is reported to
    /// between files.
    Dune2RAssetReadiness checkReadiness(const VerifyCallback& onProgress = {}) const;
    /// Digest of the target identity plus each installed file's size and modification
    /// time. Cheap enough to run on a menu interaction, and it changes whenever the
    /// installed files change or the catalog expects a different size, hash or pack.
    std::string installedFingerprint() const;

    Dune2RAssetInstallResult refreshCatalog();
    Dune2RAssetInstallResult applyCatalog(const std::string& contents);

    Dune2RAssetInstallResult install(const std::vector<std::string>& packIDs,
                                     const ProgressCallback& progress = {}) const;

    static bool isSafeRelativeAssetPath(const std::string& path);
    static std::string sha256Bytes(const std::string& bytes);
    static std::string sha256File(const std::string& filename);
    /// Parse a catalog version field. Digits only; anything else is rejected so a
    /// malformed value can never be read as a newer target.
    static bool parseCatalogVersion(const std::string& text, uint64_t& version);

private:
    const Dune2RAssetPack* findPack(const std::string& id) const;
    void loadCatalog();
    void parseCatalog(const std::string& contents);
    /// First required pack ID this catalog does not contain, empty when none is missing.
    std::string missingRequiredPackID() const;

    std::string modPath;
    bool readOnly = false;
    std::string baseURL;
    std::string revision;
    uint64_t catalogVersion = 0;
    std::string targetIdentity;
    std::vector<std::string> requiredPackIDs;
    std::vector<Dune2RAssetPack> packs;
};

#endif // DUNE2RASSETMANAGER_H
