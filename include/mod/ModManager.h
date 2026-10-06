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

#ifndef MODMANAGER_H
#define MODMANAGER_H

#include <mod/ModInfo.h>
#include <DataTypes.h>
#include <filesystem>
#include <string>
#include <vector>

/**
 * Singleton class for managing game mods.
 * 
 * Mods are stored in the user config directory under mods/:
 *   mods/
 *     active_mod.txt          - Contains name of active mod
 *     vanilla/                - Pristine game defaults (seeded from install)
 *       mod.json
 *       ObjectData.ini
 *       QuantBot Config.ini
 *       GameOptions.ini
 *     <user-mod>/             - User-created mods
 *       mod.json
 *       ObjectData.ini
 *       ...
 */
class ModManager {
public:
    /**
     * Get the singleton instance.
     */
    static ModManager& instance();
    
    // Prevent copying
    ModManager(const ModManager&) = delete;
    ModManager& operator=(const ModManager&) = delete;
    
    /**
     * Initialize the mod system. Must be called once at startup.
     * Seeds vanilla mod from install defaults if needed.
     */
    void initialize();
    
    /**
     * Check if ModManager has been initialized.
     * \return true if initialize() has been called
     */
    bool isInitialized() const;

    // Empty unless this mod exactly matches the installer payload; never trusts user metadata.
    std::string installerContentHash(const std::string& name) const;
    
    /**
     * Get the name of the currently active mod.
     * \return Mod name (e.g., "vanilla")
     */
    std::string getActiveModName() const;
    std::string getContentBase(const std::string& name) const;
    // Cached presentation capability: builtin Dune2R or its canonical immutable snapshot.
    // Derived mods retain their existing scale, including when saved to Workshop.
    bool usesDune2RRemasterPresentation() const {
        return initialized && activeDune2RRemasterPresentation;
    }
    bool isTornieContentActive() const {
        return getContentBase(getActiveModName()) == "Tornie";
    }
    const CustomHouseInfo& getActiveCustomHouseInfo() const;
    const CustomHouseInfo& getCustomHouseInfo(int house) const;
    bool isCustomHouseRegistered() const;
    bool isCustomHouseRegistered(int house) const;
    const ModMentatInfo& getActiveMentatInfo(int house) const;
    int getEffectiveMentatIdentity(int house) const;

    /**
     * \return true if the active mod opts into DuneCity city-sim features
     *         (i.e. its mod.ini sets `Enables City Mode = true`).
     */
    bool isCityModeActive() const;
    
    /**
     * Set the active mod by name.
     * \param name Mod folder name
     * \return true if mod exists and was activated
     */
    bool setActiveMod(const std::string& name);
    
    /**
     * Check if a mod exists.
     * \param name Mod folder name
     * \return true if mod exists
     */
    bool modExists(const std::string& name) const;
    
    /**
     * List all available mods.
     * \return Vector of ModInfo for each mod
     */
    std::vector<ModInfo> listMods() const;

    /// User-facing choices, with redundant cached revisions represented by their source mod.
    /// listMods() remains the complete registry for saved games and pinned dependencies.
    std::vector<ModInfo> listModChoices() const;
    
    /**
     * Get info for a specific mod.
     * \param name Mod folder name
     * \return ModInfo (empty if not found)
     */
    ModInfo getModInfo(const std::string& name) const;
    
    // === Path getters for active mod ===
    
    /**
     * Get path to active mod's ObjectData.ini.
     * Falls back to vanilla if mod doesn't have this file.
     */
    std::string getActiveObjectDataPath() const;
    
    /**
     * Get path to active mod's QuantBot Config.ini.
     * Falls back to vanilla if mod doesn't have this file.
     */
    std::string getActiveQuantBotConfigPath() const;
    
    /**
     * Get path to active mod's GameOptions.ini.
     * Falls back to vanilla if mod doesn't have this file.
     */
    std::string getActiveGameOptionsPath() const;
    
    /**
     * Load game options from active mod's GameOptions.ini.
     * \param baseOptions The base options to override (usually settings.gameOptions)
     * \return Options with mod overrides applied
     */
    SettingsClass::GameOptionsClass loadEffectiveGameOptions(
        const SettingsClass::GameOptionsClass& baseOptions) const;
    
    // === Checksums ===
    
    /**
     * Get checksums for the effective configuration (active mod + fallbacks).
     * \return ModChecksums with hashes for multiplayer verification
     */
    ModChecksums getEffectiveChecksums() const;
    
    /**
     * Recalculate and cache checksums for current state.
     * Call after loading configs.
     */
    void updateChecksums();
    
    /**
     * Set checksums from externally computed values.
     * Use this after loading configs to set in-memory hashes.
     */
    void setChecksums(const std::string& objectDataHash, 
                      const std::string& quantBotHash,
                      const std::string& gameOptionsHash);
    
    // === Mod CRUD ===
    
    /**
     * Create a new mod by copying from another mod.
     * \param name New mod folder name
     * \param baseMod Mod to copy from (default: "vanilla")
     * \return true if created successfully
     */
    bool createMod(const std::string& name, const std::string& baseMod = "vanilla");

    /** Return true when a mod name is safe to use as one directory component. */
    bool isValidModName(const std::string& name) const;
    
    /**
     * Delete a mod (cannot delete "vanilla").
     * \param name Mod folder name
     * \return true if deleted successfully
     */
    bool deleteMod(const std::string& name);
    
    /**
     * Save a received mod from network transfer.
     * Unpacks the packaged data and creates the mod directory.
     * \param  modName     Name of the mod to save
     * \param  packagedData  Packaged mod data (from network transfer)
     * \return true if successful
     */
    bool saveReceivedMod(const std::string& modName, const std::string& packagedData);

    /**
     * Save a received mod, but only install it if the staged payload really produces the
     * checksum the sender announced.
     *
     * The payload is unpacked into a staging directory, its combined checksum is computed
     * there, and the mod directory is only replaced when that matches expectedChecksum. This
     * is an integrity and ordering guarantee, not an authentication one: the same peer sends
     * both the payload and the expected checksum, and the checksum is FNV-1a, not a signature.
     * What it does buy is that nothing is written into the active mod - and nothing is
     * activated - unless the content matches what the lobby verified against.
     *
     * \param  modName          Name of the mod to save
     * \param  packagedData     Packaged mod data (from network transfer)
     * \param  expectedChecksum Combined checksum the sender announced; empty disables the check
     * \return true if the mod was installed
     */
    bool saveReceivedMod(const std::string& modName, const std::string& packagedData,
                         const std::string& expectedChecksum);
    
    // === Vanilla seeding ===
    
    /**
     * Seed vanilla mod from install defaults.
     * Called automatically during initialize() if needed.
     */
    void seedVanillaFromDefaults();

    /**
     * Check if vanilla mod needs to be re-seeded (version mismatch).
     * \return true if game version doesn't match vanilla mod version
     */
    bool vanillaNeedsReseed() const;

    /**
     * Seed the built-in "dunecity" mod from install defaults.
     * Same config files as vanilla, but mod.ini sets `Enables City Mode = true`.
     */
    void seedDunecityFromDefaults();

    /**
     * Refresh the bundled DuneCity presentation payload in the installed
     * user-data mod without touching authored gameplay configuration. Safe to
     * call on every start: an unchanged bundle copies nothing.
     */
    void refreshBundledDunecityGraphicsSkins() const;

    /**
     * Remove only the installed DuneCity presentation cache
     * (`<mod>/graphics_skins`). The bundled copy is restored on the next
     * application start - on Android by the launcher, which keeps its
     * graphics-skin extraction marker inside that directory. Saves,
     * configuration, and every other mod directory are left untouched.
     * Returns false without deleting anything when no second source exists.
     */
    bool clearDunecityGraphicsCache() const;

    // DuneCity 1.0.492: seed the Tornie mod
    void seedTornieFromDefaults();
    void seedDune2RFromDefaults();

    /**
     * \return true if the dunecity mod is missing required files or has
     *         a stale game version and should be re-seeded.
     */
    bool dunecityNeedsReseed() const;

    // DuneCity 1.0.492: Tornie mod reseed check
    bool tornieNeedsReseed() const;

    /**
     * Return true when the installed bundled Dune2R payload is incomplete or
     * older than the copy shipped with the application.
     */
    bool dune2rNeedsReseed() const;
    
    // === Paths ===
    
    /**
     * Get the base mods directory path.
     */
    std::string getModsBasePath() const;
    
    /**
     * Get path to a specific mod's directory.
     */
    std::string getModPath(const std::string& name) const;
    
    /**
     * Write mod.ini metadata for a mod.
     */
    bool writeModInfo(const std::string& modPath, const ModInfo& info) const;
    
private:
    ModManager();
    ~ModManager();
    
    /**
     * Load active mod name from active_mod.txt.
     */
    void loadActiveMod();
    
    /**
     * Save active mod name to active_mod.txt.
     */
    void saveActiveMod() const;
    
    /**
     * Read mod.ini metadata for a mod.
     */
    ModInfo readModIni(const std::string& modPath) const;

    // Resolve identity without the active cache and optionally return the
    // checksummed Workshop metadata root from the same verification pass.
    std::string resolveContentBase(const std::string& name,
                                   std::string* verifiedModPath = nullptr) const;
    
    /**
     * Get path to install config defaults directory.
     */
    std::string getInstallConfigPath() const;

    /**
     * Compute a canonical FNV-1a hash of an INI-style file (skips comments and
     * blank lines so cosmetic edits don't change the hash). Returns
     * "FILE_NOT_FOUND" if the file is missing.
     */
    static std::string hashFileCanonical(const std::string& path);

    /**
     * Combine the per-file hashes into the checksum that is exchanged in multiplayer.
     * Shared by updateChecksums() and the staged verification of a received mod so both
     * produce byte-identical results.
     */
    static std::string combineChecksumParts(const std::string& objectDataHash,
                                            const std::string& quantBotHash,
                                            const std::string& gameOptionsHash,
                                            const std::string& customHouseHash,
                                            const std::string& engineCompatibility);

    /**
     * Compute the combined checksum a mod directory would produce if it were the active mod.
     * Used to verify a received mod while it is still staged, before anything is installed.
     */
    std::string computeCombinedChecksumForDirectory(const std::string& modName,
                                                    const std::filesystem::path& directory) const;

    /**
     * Returns true when the installed mod's ObjectData.ini canonical hash
     * differs from the install's ObjectData.ini.default canonical hash.
     * Used to auto-reseed mods when the shipped defaults are updated.
     */
    bool installedObjectDataDiffersFromDefaults(const std::string& modName) const;
    
    std::string modsBasePath;        ///< Base path for mods directory
    std::string activeMod;
    std::string activeContentBase; // Capability identity, refreshed on activation; no per-tile filesystem reads.
    bool activeDune2RRemasterPresentation = false;
    CustomHouseInfo activeCustomHouse;           ///< Active mod's campaign custom house
    CustomHouseInfo activeGuestCustomHouse;      ///< Tornie guest custom house for custom games
    std::vector<ModMentatInfo> activeMentats;     ///< Mentat overrides owned by the active mod
    mutable ModChecksums cachedChecksums;  ///< Cached checksums
    mutable bool checksumsDirty;     ///< Do checksums need recalculation?
    bool initialized;                ///< Has initialize() been called?
};

#endif // MODMANAGER_H
