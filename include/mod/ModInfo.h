/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MODINFO_H
#define MODINFO_H

#include <mod/ModMentatConfig.h>

// Increment when Stefan releases changed Dune City mod content, independently of the app.
inline constexpr const char* DUNECITY_MOD_VERSION = "1.002";

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

/**
 * Optional registration for the fixed generic ninth-house slot.
 * Content remains entirely mod-owned.
 */
struct CustomHouseInfo {
    bool enabled = false;
    std::string displayName;
    char scenarioLetter = '?';
    std::string regionPrefix;
    int paletteIndex = 0;
    int fallbackHouse = 0;
    std::string heraldAsset;
    std::string houseNameVoiceAsset;
    double voicePlaybackRate = 1.0;
    double voiceGain = 1.0;
};

/**
 * Checksums for mod verification in multiplayer.
 * Each hash is a 16-character hex string (FNV-1a).
 */
struct ModChecksums {
    std::string objectData;      ///< Hash of ObjectData (unit/structure stats)
    std::string quantBotConfig;  ///< Hash of QuantBot AI config
    std::string gameOptions;     ///< Hash of game options/rules
    std::string customHouse;     ///< Hash of optional CustomHouse.ini registration
    std::string combined;        ///< Combined hash of all synchronized configuration
    
    bool operator==(const ModChecksums& other) const {
        return combined == other.combined;
    }
    
    bool operator!=(const ModChecksums& other) const {
        return !(*this == other);
    }
};

/**
 * Metadata about a mod.
 */
struct ModInfo {
    std::string name;            ///< Mod folder name (e.g., "vanilla", "balanced-warfare")
    std::string displayName;     ///< Human-readable name
    std::string author;          ///< Mod author
    std::string description;     ///< Short description
    std::string version;         ///< Mod version (user-defined, e.g., "1.0.0")
    std::string baseMod;          ///< Original engine/content family for editable copies.
    std::string gameVersion;     ///< Game version this mod was created for
    ModChecksums checksums;      ///< Cached checksums
    CustomHouseInfo customHouse; ///< Optional generic ninth-house registration
    CustomHouseInfo guestCustomHouse; ///< Optional additional named house registration
    std::vector<int> playableHouses; ///< Optional explicit playable roster, in declaration order
    unsigned spiceVariantMask = 0;
    unsigned spiceVariantOverrideMask = 0;
    bool customUnitsAndBuildings = true;
    bool customUnitsAndBuildingsOverride = false;
    std::vector<ModMentatInfo> mentats; ///< Optional active-mod Mentat presentations by house ID
    
    unsigned revisionVersion = 0; ///< Saved Workshop revision, separate from legacy author version.
    std::string revisionHash;     ///< Workshop revision this install records, empty when never published.
    std::vector<std::string> selectionAliases; ///< Hidden cached copies represented by this picker entry.
    bool matchesSelectionName(const std::string& value) const {
        return name == value || std::find(selectionAliases.begin(), selectionAliases.end(), value) != selectionAliases.end();
    }
    std::string officialVersion;  ///< Independent bundled mod version ("1.001"); wins over revisionVersion.
    std::string selectionLabel() const {
        if(!officialVersion.empty()) return displayName + " " + officialVersion;
        return displayName + (revisionVersion ? " v" + std::to_string(revisionVersion) : "");
    }

    bool hasObjectData = false;          ///< Does this mod have ObjectData.ini?
    bool hasQuantBotConfig = false;      ///< Does this mod have QuantBot Config.ini?
    bool hasGameOptions = false;         ///< Does this mod have GameOptions.ini?

    bool enablesCityMode = false; ///< When true, DuneCity city-sim features are active for this mod.
};

/** True for the immutable "ws-<hash>" folders that installed Workshop revisions live in. */
inline bool isWorkshopSnapshotMod(const std::string& name) {
    return name.rfind("ws-", 0) == 0;
}

/**
 * Mod-choice policy shared by every picker.
 *
 * The same published revision can be installed twice: once as the editable mod that produced it
 * (the bundled "dunecity" install after it was shared) and once as the immutable "ws-<hash>"
 * snapshot a lobby or the community menu installs. Both carry the same display name and revision,
 * which is how the official mod ended up listed twice. The snapshot is dropped only when
 * stillMatchesRevision() confirms the install that owns the revision is still that revision byte
 * for byte; a locally edited copy keeps both entries so genuinely different content is never
 * collapsed just because the display names match. Nothing is deleted from disk.
 */
inline std::vector<ModInfo> withoutRedundantWorkshopSnapshots(
        const std::vector<ModInfo>& mods,
        const std::function<bool(const ModInfo& owner, const std::string& revisionHash)>& stillMatchesRevision) {
    std::map<std::string, size_t> ownerByRevision;
    for(size_t i = 0; i < mods.size(); ++i) {
        if(!isWorkshopSnapshotMod(mods[i].name) && !mods[i].revisionHash.empty()) {
            ownerByRevision.emplace(mods[i].revisionHash, i);
        }
    }

    auto choices = mods;
    std::vector<bool> hidden(mods.size(), false);
    for(size_t index = 0; index < mods.size(); ++index) {
        const auto& mod = mods[index];
        if(isWorkshopSnapshotMod(mod.name) && !mod.revisionHash.empty()) {
            const auto owner = ownerByRevision.find(mod.revisionHash);
            if(owner != ownerByRevision.end() && stillMatchesRevision(mods[owner->second], mod.revisionHash)) {
                choices[owner->second].selectionAliases.push_back(mod.name);
                hidden[index] = true;
            }
        }
    }
    std::vector<ModInfo> kept;
    for(size_t i = 0; i < choices.size(); ++i) if(!hidden[i]) kept.push_back(std::move(choices[i]));
    return kept;
}

#endif // MODINFO_H
