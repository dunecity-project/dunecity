/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <mod/Dune2RAssetManager.h>

#include <FileClasses/INIFile.h>
#include <Network/ENetHttp.h>
#include <misc/exceptions.h>

#include <SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace {

constexpr const char* kImmutableSnapshotMessage =
    "Switch to the Dune2R working mod to download assets; online snapshots are immutable.";

constexpr std::array<uint32_t, 64> kSha256Constants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

uint32_t rotateRight(uint32_t value, unsigned int count) {
    return (value >> count) | (value << (32u - count));
}

class Sha256 final {
public:
    Sha256()
        : state{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u} {
    }

    void update(const unsigned char* data, size_t length) {
        totalBytes += length;
        while(length > 0) {
            const size_t copied = std::min(length, block.size() - blockSize);
            std::memcpy(block.data() + blockSize, data, copied);
            blockSize += copied;
            data += copied;
            length -= copied;
            if(blockSize == block.size()) {
                transform(block.data());
                blockSize = 0;
            }
        }
    }

    std::string finish() {
        const uint64_t bitLength = totalBytes * 8u;
        block[blockSize++] = 0x80u;
        if(blockSize > 56) {
            std::fill(block.begin() + static_cast<std::ptrdiff_t>(blockSize), block.end(), 0u);
            transform(block.data());
            blockSize = 0;
        }
        std::fill(block.begin() + static_cast<std::ptrdiff_t>(blockSize), block.begin() + 56, 0u);
        for(size_t i = 0; i < 8; ++i) {
            block[63 - i] = static_cast<unsigned char>(bitLength >> (i * 8u));
        }
        transform(block.data());

        std::ostringstream result;
        result << std::hex << std::setfill('0');
        for(const uint32_t value : state) {
            result << std::setw(8) << value;
        }
        return result.str();
    }

private:
    void transform(const unsigned char* data) {
        std::array<uint32_t, 64> words{};
        for(size_t i = 0; i < 16; ++i) {
            words[i] = (static_cast<uint32_t>(data[i * 4]) << 24u)
                       | (static_cast<uint32_t>(data[i * 4 + 1]) << 16u)
                       | (static_cast<uint32_t>(data[i * 4 + 2]) << 8u)
                       | static_cast<uint32_t>(data[i * 4 + 3]);
        }
        for(size_t i = 16; i < words.size(); ++i) {
            const uint32_t s0 = rotateRight(words[i - 15], 7) ^ rotateRight(words[i - 15], 18)
                                ^ (words[i - 15] >> 3u);
            const uint32_t s1 = rotateRight(words[i - 2], 17) ^ rotateRight(words[i - 2], 19)
                                ^ (words[i - 2] >> 10u);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        auto working = state;
        for(size_t i = 0; i < words.size(); ++i) {
            const uint32_t s1 = rotateRight(working[4], 6) ^ rotateRight(working[4], 11)
                                ^ rotateRight(working[4], 25);
            const uint32_t choice = (working[4] & working[5]) ^ (~working[4] & working[6]);
            const uint32_t temp1 = working[7] + s1 + choice + kSha256Constants[i] + words[i];
            const uint32_t s0 = rotateRight(working[0], 2) ^ rotateRight(working[0], 13)
                                ^ rotateRight(working[0], 22);
            const uint32_t majority = (working[0] & working[1]) ^ (working[0] & working[2])
                                      ^ (working[1] & working[2]);
            const uint32_t temp2 = s0 + majority;
            for(size_t j = 7; j > 0; --j) {
                working[j] = working[j - 1];
            }
            working[4] += temp1;
            working[0] = temp1 + temp2;
        }
        for(size_t i = 0; i < state.size(); ++i) {
            state[i] += working[i];
        }
    }

    std::array<uint32_t, 8> state;
    std::array<unsigned char, 64> block{};
    size_t blockSize = 0;
    uint64_t totalBytes = 0;
};

std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> result;
    std::istringstream input(value);
    std::string part;
    while(std::getline(input, part, delimiter)) {
        result.push_back(part);
    }
    return result;
}

bool isHexDigest(const std::string& value) {
    return value.size() == 64
           && std::all_of(value.begin(), value.end(), [](unsigned char character) {
                  return std::isxdigit(character) != 0;
              });
}

bool verifiedFile(const std::filesystem::path& path, const Dune2RAssetFile& file) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error)
           && std::filesystem::file_size(path, error) == file.size
           && !error
           && Dune2RAssetManager::sha256File(path.string()) == file.sha256;
}

} // namespace

uint64_t Dune2RAssetPack::totalBytes() const {
    uint64_t result = 0;
    for(const auto& file : files) {
        result += file.size;
    }
    return result;
}

Dune2RAssetManager::Dune2RAssetManager(const std::string& dune2rModPath)
    : modPath(dune2rModPath) {
    auto folder = std::filesystem::path(modPath).lexically_normal();
    if(!folder.has_filename()) folder = folder.parent_path();
    readOnly = folder.filename().string().rfind("ws-", 0) == 0;
    const auto sidecar = std::filesystem::path(modPath) / "workshop-revision.ini";
    if(!readOnly && std::filesystem::is_regular_file(sidecar)) {
        INIFile metadata(sidecar.string());
        readOnly = metadata.getBoolValue("Workshop", "Immutable", false);
    }
    loadCatalog();
}

const std::vector<Dune2RAssetPack>& Dune2RAssetManager::getPacks() const noexcept {
    return packs;
}

const std::string& Dune2RAssetManager::getRevision() const noexcept {
    return revision;
}

uint64_t Dune2RAssetManager::getCatalogVersion() const noexcept {
    return catalogVersion;
}

const std::string& Dune2RAssetManager::getTargetIdentity() const noexcept {
    return targetIdentity;
}

const std::vector<std::string>& Dune2RAssetManager::getRequiredPackIDs() const noexcept {
    return requiredPackIDs;
}

std::string Dune2RAssetManager::missingRequiredPackID() const {
    for(const auto& required : requiredPackIDs) {
        if(findPack(required) == nullptr) {
            return required;
        }
    }
    return {};
}

bool Dune2RAssetManager::isReadOnly() const noexcept {
    return readOnly;
}

bool Dune2RAssetManager::isSafeRelativeAssetPath(const std::string& path) {
    if(path.empty() || path.front() == '/' || path.front() == '\\'
       || path.find('\\') != std::string::npos) {
        return false;
    }
    const std::filesystem::path candidate(path);
    if(candidate.is_absolute()) {
        return false;
    }
    for(const auto& component : candidate) {
        if(component == ".." || component == "." || component.empty()) {
            return false;
        }
    }
    return path.find_first_not_of(
               "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-/")
           == std::string::npos;
}

bool Dune2RAssetManager::parseCatalogVersion(const std::string& text, uint64_t& version) {
    version = 0;
    if(text.empty() || text.size() > 19
       || text.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    version = std::stoull(text);
    return true;
}

std::string Dune2RAssetManager::sha256Bytes(const std::string& bytes) {
    Sha256 sha;
    sha.update(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
    return sha.finish();
}

std::string Dune2RAssetManager::sha256File(const std::string& filename) {
    std::ifstream input(filename, std::ios::binary);
    if(!input) {
        return {};
    }
    Sha256 sha;
    std::array<unsigned char, 64 * 1024> buffer{};
    while(input) {
        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        const std::streamsize read = input.gcount();
        if(read > 0) {
            sha.update(buffer.data(), static_cast<size_t>(read));
        }
    }
    return input.bad() ? std::string{} : sha.finish();
}

void Dune2RAssetManager::loadCatalog() {
    auto parseFile = [this](const char* filename, Dune2RAssetManager& into) {
        const auto path = std::filesystem::path(modPath) / filename;
        if(!std::filesystem::is_regular_file(path)) return false;
        try {
            if(std::filesystem::file_size(path) > 1024u * 1024u) {
                THROW(std::runtime_error, "Dune2R catalog exceeds 1 MiB");
            }
            std::ifstream input(path, std::ios::binary);
            const std::string contents((std::istreambuf_iterator<char>(input)), {});
            into.packs.clear();
            into.parseCatalog(contents);
            return true;
        } catch(const std::exception& error) {
            SDL_Log("Dune2R catalog %s rejected: %s", filename, error.what());
            return false;
        }
    };

    Dune2RAssetManager bundled(*this), online(*this);
    const bool haveBundled = parseFile("asset-catalog.ini", bundled);
    const bool haveOnline = parseFile("asset-catalog-online.ini", online);

    // A modern bundled catalog states which packs this application ships. A published
    // catalog may add packs, but removing one would take artwork away from a build that
    // already pins it - whatever publication counter another branch has reached.
    // Legacy bundled catalogs (version 0) keep their previous behaviour and no floor.
    if(haveBundled && bundled.catalogVersion != 0) {
        requiredPackIDs.clear();
        requiredPackIDs.reserve(bundled.packs.size());
        for(const auto& pack : bundled.packs) {
            requiredPackIDs.push_back(pack.id);
        }
    }
    bundled.requiredPackIDs = online.requiredPackIDs = requiredPackIDs;

    const Dune2RAssetManager* chosen = nullptr;
    if(haveOnline && haveBundled && bundled.catalogVersion != 0) {
        // A refreshed online cache supersedes the bundled catalog only as a newer
        // publication - or as the very same target - and only with the floor intact.
        const auto missing = online.missingRequiredPackID();
        const char* refusal = nullptr;
        if(online.catalogVersion < bundled.catalogVersion) {
            refusal = "an older publication";
        } else if(online.catalogVersion == bundled.catalogVersion
                  && online.targetIdentity != bundled.targetIdentity) {
            refusal = "a different target under the same publication number";
        } else if(!missing.empty()) {
            refusal = "a target without a bundled pack";
        }
        if(refusal != nullptr) {
            SDL_Log("Dune2R online catalog (version %llu) ignored in favour of the bundled "
                    "catalog (version %llu): %s%s%s",
                    static_cast<unsigned long long>(online.catalogVersion),
                    static_cast<unsigned long long>(bundled.catalogVersion), refusal,
                    missing.empty() ? "" : ", missing ", missing.c_str());
            chosen = &bundled;
        } else {
            chosen = &online;
        }
    } else if(haveOnline) {
        chosen = &online;
    } else if(haveBundled) {
        chosen = &bundled;
    }
    if(chosen == nullptr) {
        THROW(std::runtime_error, "No valid Dune2R asset catalog is available");
    }
    baseURL = chosen->baseURL;
    revision = chosen->revision;
    catalogVersion = chosen->catalogVersion;
    targetIdentity = chosen->targetIdentity;
    packs = chosen->packs;
}

void Dune2RAssetManager::parseCatalog(const std::string& contents) {
    if(contents.empty() || contents.size() > 1024u * 1024u) {
        THROW(std::runtime_error, "Empty or oversized Dune2R catalog");
    }
    std::unique_ptr<SDL_RWops, decltype(&SDL_RWclose)> stream(
        SDL_RWFromConstMem(contents.data(), static_cast<int>(contents.size())), SDL_RWclose);
    if(!stream) THROW(std::runtime_error, "Cannot read Dune2R catalog");
    INIFile catalog(stream.get());
    if(catalog.getIntValue("Catalog", "Schema", 0) != 1) {
        THROW(std::runtime_error, "Unsupported or missing Dune2R asset catalog");
    }
    revision = catalog.getStringValue("Catalog", "Revision", "");
    baseURL = catalog.getStringValue("Catalog", "BaseURL", "");
    // Legacy catalogs have no Version. They stay loadable as version 0; a present
    // field has to be a plain decimal number, so a damaged value cannot pose as new.
    const auto versionField = catalog.getStringValue("Catalog", "Version", "0");
    if(!parseCatalogVersion(versionField, catalogVersion)) {
        THROW(std::runtime_error, "Invalid Dune2R asset catalog version");
    }
    const int packCount = catalog.getIntValue("Catalog", "PackCount", 0);
    const std::string expectedBase = "https://raw.githubusercontent.com/dunecity-project/dunecity/"
                                    + revision + "/mods/Dune2R/graphics_hd/units";
    // Existing installed catalogs remain valid after the organization transfer.
    // Keep a fixed repository allowlist; never accept arbitrary GitHub paths.
    const std::string legacyBase = "https://raw.githubusercontent.com/VR48/dunecity/"
                                  + revision + "/mods/Dune2R/graphics_hd/units";
    if(revision.size() != 40 || revision.find_first_not_of("0123456789abcdef") != std::string::npos
       || (baseURL != expectedBase && baseURL != legacyBase) || packCount < 0 || packCount > 256) {
        THROW(std::runtime_error, "Invalid Dune2R asset catalog header");
    }
    while(!baseURL.empty() && baseURL.back() == '/') {
        baseURL.pop_back();
    }

    std::set<std::string> packNames, unitNames;
    for(int packIndex = 0; packIndex < packCount; ++packIndex) {
        const std::string section = "Pack." + std::to_string(packIndex);
        Dune2RAssetPack pack;
        pack.id = catalog.getStringValue(section, "ID", "");
        pack.displayName = catalog.getStringValue(section, "DisplayName", pack.id);
        pack.variant = catalog.getStringValue(section, "Variant", "");
        pack.unit = catalog.getStringValue(section, "Unit", "");
        const int fileCount = catalog.getIntValue(section, "FileCount", 0);
        if(!isSafeRelativeAssetPath(pack.id) || !isSafeRelativeAssetPath(pack.unit)
           || pack.id.find('/') != std::string::npos || pack.unit.find('/') != std::string::npos
           || pack.variant != "remastered" || fileCount <= 0 || fileCount > 4096) {
            THROW(std::runtime_error, "Invalid Dune2R asset pack " + section);
        }
        if(!packNames.insert(pack.id).second || !unitNames.insert(pack.unit).second) {
            THROW(std::runtime_error, "Duplicate Dune2R asset pack identity");
        }
        std::set<std::string> fileNames;
        uint64_t packBytes = 0;
        for(int fileIndex = 0; fileIndex < fileCount; ++fileIndex) {
            const auto fields = split(catalog.getStringValue(
                section, "File." + std::to_string(fileIndex), ""), '|');
            if(fields.size() != 3 || !isSafeRelativeAssetPath(fields[0]) || !isHexDigest(fields[2])) {
                THROW(std::runtime_error, "Invalid file entry in Dune2R asset pack " + pack.id);
            }
            Dune2RAssetFile file;
            file.relativePath = fields[0];
            const auto extension = std::filesystem::path(file.relativePath).extension().string();
            if((extension != ".png" && file.relativePath != "unit.ini"
                && file.relativePath != "building.ini" && file.relativePath != "tile.ini")
               || !fileNames.insert(file.relativePath).second) {
                THROW(std::runtime_error, "Nonvisual or duplicate asset file");
            }
            file.sha256 = fields[2];
            try {
                size_t consumed = 0;
                file.size = std::stoull(fields[1], &consumed);
                if(consumed != fields[1].size() || fields[1].find_first_not_of("0123456789") != std::string::npos) {
                    THROW(std::runtime_error, "Invalid asset size");
                }
            } catch(const std::exception&) {
                THROW(std::runtime_error, "Invalid file size in Dune2R asset pack " + pack.id);
            }
            packBytes += file.size;
            if(file.size == 0 || file.size > 128u * 1024u * 1024u || packBytes > 2ull * 1024u * 1024u * 1024u) {
                THROW(std::runtime_error, "Asset file or pack exceeds the download budget");
            }
            pack.files.push_back(std::move(file));
        }
        packs.push_back(std::move(pack));
    }

    // Everything the runtime has to treat as one artwork target. The publication
    // counter is deliberately excluded: it orders targets, it does not identify them.
    std::ostringstream identity;
    identity << "revision=" << revision << "\nbase=" << baseURL << "\npacks=" << packs.size();
    for(const auto& pack : packs) {
        identity << "\npack=" << pack.id << '|' << pack.displayName << '|' << pack.variant
                 << '|' << pack.unit << '|' << pack.files.size();
        for(const auto& file : pack.files) {
            identity << "\nfile=" << file.relativePath << '|' << file.size << '|' << file.sha256;
        }
    }
    targetIdentity = sha256Bytes(identity.str());
}

Dune2RAssetInstallResult Dune2RAssetManager::applyCatalog(const std::string& contents) {
    if(readOnly) return {false, false, kImmutableSnapshotMessage};
    Dune2RAssetInstallResult result;
    const auto destination = std::filesystem::path(modPath) / "asset-catalog-online.ini";
    const auto staged = std::filesystem::path(modPath) / ".asset-catalog.pending";
    const auto backup = std::filesystem::path(modPath) / ".asset-catalog.previous";
    try {
        auto candidate = *this;
        candidate.packs.clear();
        candidate.parseCatalog(contents);
        // An older publication must not replace the target this installation pins.
        if(candidate.catalogVersion < catalogVersion) {
            THROW(std::runtime_error, "the published catalog version "
                  + std::to_string(candidate.catalogVersion) + " is older than the installed "
                  + std::to_string(catalogVersion));
        }
        // One publication counter describes exactly one target. The same number with a
        // different revision, base URL, pack set or expected file is a conflict, not an
        // update: accepting it would let a reused number replace a pinned target. The
        // identical target may refresh. Legacy catalogs have no counter (0) and keep
        // their previous behaviour, so this check does not apply to them.
        if(candidate.catalogVersion == catalogVersion && catalogVersion != 0
           && candidate.targetIdentity != targetIdentity) {
            THROW(std::runtime_error, "publication version " + std::to_string(catalogVersion)
                  + " already describes a different artwork target");
        }
        // Published catalogs may add packs, never remove one this application ships.
        if(const auto missing = candidate.missingRequiredPackID(); !missing.empty()) {
            THROW(std::runtime_error, "the published catalog is missing the bundled asset pack "
                  + missing);
        }
        {
            std::ofstream output(staged, std::ios::binary | std::ios::trunc);
            output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            output.close();
            if(!output) THROW(std::runtime_error, "Could not save refreshed asset catalog");
        }
        std::error_code ignored;
        std::filesystem::remove(backup, ignored);
        if(std::filesystem::exists(destination)) std::filesystem::rename(destination, backup);
        try {
            std::filesystem::rename(staged, destination);
        } catch(...) {
            if(std::filesystem::exists(backup)) std::filesystem::rename(backup, destination);
            throw;
        }
        // Changed means "a different target", not just a different revision: new
        // expected sizes, hashes or pack metadata change what has to be installed.
        result.changed = targetIdentity != candidate.targetIdentity;
        baseURL = std::move(candidate.baseURL);
        revision = std::move(candidate.revision);
        catalogVersion = candidate.catalogVersion;
        targetIdentity = std::move(candidate.targetIdentity);
        packs = std::move(candidate.packs);
        result.success = true;
        result.message = "Catalog refreshed: " + std::to_string(packs.size()) + " available packs.";
    } catch(const std::exception& error) {
        result.message = std::string("Keeping the previous catalog: ") + error.what();
        std::error_code ignored;
        std::filesystem::remove(staged, ignored);
    }
    return result;
}

Dune2RAssetInstallResult Dune2RAssetManager::refreshCatalog() {
    if(readOnly) return {false, false, kImmutableSnapshotMessage};
    try {
        return applyCatalog(loadFromHttp(
            "https://raw.githubusercontent.com/dunecity-project/dunecity/main/mods/Dune2R/asset-catalog.ini"));
    } catch(const std::exception& error) {
        return {false, false, std::string("Offline; keeping the existing catalog. ") + error.what()};
    }
}

const Dune2RAssetPack* Dune2RAssetManager::findPack(const std::string& id) const {
    const auto found = std::find_if(packs.begin(), packs.end(), [&](const Dune2RAssetPack& pack) {
        return pack.id == id;
    });
    return found == packs.end() ? nullptr : &*found;
}

bool Dune2RAssetManager::isPackInstalled(const Dune2RAssetPack& pack) const {
    const std::filesystem::path destination = std::filesystem::path(modPath)
                                              / "graphics_hd" / "units" / pack.unit;
    return std::all_of(pack.files.begin(), pack.files.end(), [&](const Dune2RAssetFile& file) {
        return verifiedFile(destination / file.relativePath, file);
    });
}

Dune2RAssetReadiness Dune2RAssetManager::checkReadiness() const {
    Dune2RAssetReadiness result;
    result.revision = revision;
    result.catalogVersion = catalogVersion;
    const std::filesystem::path unitsRoot = std::filesystem::path(modPath)
                                            / "graphics_hd" / "units";
    for(const auto& pack : packs) {
        result.totalBytes += pack.totalBytes();
        uint64_t packMissingBytes = 0;
        for(const auto& file : pack.files) {
            // Only the installed location counts. Staging and partial files are
            // never a playable target, however complete they look.
            if(!verifiedFile(unitsRoot / pack.unit / file.relativePath, file)) {
                packMissingBytes += file.size;
            }
        }
        if(packMissingBytes > 0) {
            result.missingPackIDs.push_back(pack.id);
            result.missingBytes += packMissingBytes;
        }
    }
    // An empty catalog describes no playable target, so it is never "ready".
    result.complete = !packs.empty() && result.missingPackIDs.empty();
    result.fingerprint = installedFingerprint();
    return result;
}

std::string Dune2RAssetManager::installedFingerprint() const {
    const std::filesystem::path unitsRoot = std::filesystem::path(modPath)
                                            / "graphics_hd" / "units";
    std::ostringstream text;
    // The target this was measured against, then what is on disk. A changed expected
    // size or hash therefore invalidates a cached ready result even when the installed
    // bytes are untouched.
    text << modPath << '|' << revision << '|' << catalogVersion << '|' << targetIdentity
         << '|' << packs.size();
    for(const auto& pack : packs) {
        text << '\n' << pack.unit;
        for(const auto& file : pack.files) {
            std::error_code sizeError, timeError;
            const auto path = unitsRoot / pack.unit / file.relativePath;
            const auto size = std::filesystem::file_size(path, sizeError);
            const auto written = std::filesystem::last_write_time(path, timeError);
            text << '|' << file.relativePath << ':' << file.sha256 << ':' << file.size << ':'
                 << (sizeError ? 0u : size) << ':'
                 << (timeError ? 0LL : static_cast<long long>(written.time_since_epoch().count()));
        }
    }
    return sha256Bytes(text.str());
}

Dune2RAssetInstallResult Dune2RAssetManager::install(
    const std::vector<std::string>& packIDs, const ProgressCallback& progress) const {
    if(readOnly) return {false, false, kImmutableSnapshotMessage};
    Dune2RAssetInstallResult result;
    if(packIDs.empty()) {
        result.message = "No Dune2R asset pack was selected.";
        return result;
    }

    std::vector<const Dune2RAssetPack*> selected;
    uint64_t totalBytes = 0;
    for(const auto& id : packIDs) {
        const Dune2RAssetPack* pack = findPack(id);
        if(pack == nullptr) {
            result.message = "Unknown Dune2R asset pack: " + id;
            return result;
        }
        selected.push_back(pack);
        totalBytes += pack->totalBytes();
    }

    uint64_t completedBytes = 0;
    bool changed = false;
    try {
        for(const Dune2RAssetPack* pack : selected) {
            const std::filesystem::path unitsRoot = std::filesystem::path(modPath)
                                                    / "graphics_hd" / "units";
            const std::filesystem::path destination = unitsRoot / pack->unit;
            const std::filesystem::path staged = unitsRoot / (pack->unit + ".download");
            const std::filesystem::path backup = unitsRoot / (pack->unit + ".previous");
            std::filesystem::create_directories(staged);

            bool packNeedsInstall = false;
            std::map<std::string, std::filesystem::path> verifiedByHash;
            for(const auto& file : pack->files) {
                const auto installedFile = destination / file.relativePath;
                const auto stagedFile = staged / file.relativePath;
                if(verifiedFile(installedFile, file)) {
                    std::filesystem::create_directories(stagedFile.parent_path());
                    std::filesystem::copy_file(installedFile, stagedFile,
                                               std::filesystem::copy_options::overwrite_existing);
                    verifiedByHash[file.sha256] = stagedFile;
                    completedBytes += file.size;
                    continue;
                }
                packNeedsInstall = true;
                if(verifiedFile(stagedFile, file)) {
                    verifiedByHash[file.sha256] = stagedFile;
                    completedBytes += file.size;
                    continue;
                }

                std::filesystem::create_directories(stagedFile.parent_path());
                const auto duplicate = verifiedByHash.find(file.sha256);
                if(duplicate != verifiedByHash.end()) {
                    std::filesystem::copy_file(duplicate->second, stagedFile,
                                               std::filesystem::copy_options::overwrite_existing);
                    completedBytes += file.size;
                } else {
                    const std::filesystem::path partial = stagedFile.string() + ".part";
                    std::error_code error;
                    if(std::filesystem::is_regular_file(partial, error)
                       && std::filesystem::file_size(partial, error) > file.size) {
                        std::filesystem::remove(partial, error);
                    }
                    if(!verifiedFile(partial, file)) {
                        const std::string url = baseURL + "/" + pack->unit + "/" + file.relativePath;
                        downloadHttpFile(url, partial.string(), [&](uint64_t current, uint64_t) {
                            if(!progress) {
                                return true;
                            }
                            return progress(Dune2RAssetProgress{
                                pack->displayName, file.relativePath,
                                completedBytes + std::min(current, file.size), totalBytes
                            });
                        });
                    }
                    if(!verifiedFile(partial, file)) {
                        std::filesystem::remove(partial, error);
                        THROW(std::runtime_error, "Integrity check failed for " + file.relativePath);
                    }
                    std::filesystem::remove(stagedFile, error);
                    std::filesystem::rename(partial, stagedFile);
                    verifiedByHash[file.sha256] = stagedFile;
                    completedBytes += file.size;
                }

                if(progress && !progress(Dune2RAssetProgress{
                       pack->displayName, file.relativePath, completedBytes, totalBytes})) {
                    THROW(std::runtime_error, "Download cancelled");
                }
            }

            if(packNeedsInstall) {
                std::error_code ignored;
                std::filesystem::remove_all(backup, ignored);
                if(std::filesystem::exists(destination)) {
                    std::filesystem::rename(destination, backup);
                }
                try {
                    std::filesystem::rename(staged, destination);
                } catch(...) {
                    if(std::filesystem::exists(backup) && !std::filesystem::exists(destination)) {
                        std::filesystem::rename(backup, destination);
                    }
                    throw;
                }
                std::filesystem::remove_all(backup, ignored);
                changed = true;
            } else {
                std::error_code ignored;
                std::filesystem::remove_all(staged, ignored);
            }
        }

        if(changed) {
            const std::filesystem::path marker = std::filesystem::path(modPath)
                                                  / "graphics_hd" / "units" / ".mount-revision";
            std::ofstream output(marker, std::ios::trunc);
            output << revision << "-"
                   << std::chrono::system_clock::now().time_since_epoch().count() << "\n";
            if(!output) {
                THROW(std::runtime_error, "Could not update the Dune2R mount revision");
            }
        }
        result.success = true;
        result.changed = changed;
        result.message = changed ? "Asset pack installed and mounted."
                                 : "Selected asset pack is already installed and verified.";
    } catch(const std::exception& error) {
        SDL_Log("Dune2R asset install failed: %s", error.what());
        result.message = error.what();
    }
    return result;
}
