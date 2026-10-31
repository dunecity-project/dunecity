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

#include <FileClasses/FileManager.h>

#include <globals.h>

#include <FileClasses/TextManager.h>

#include <misc/FileSystem.h>

#include <misc/fnkdat.h>
#include <misc/md5.h>
#include <misc/string_util.h>
#include <misc/exceptions.h>
#include <mod/ModManager.h>

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <memory>
#include <vector>

namespace {
bool hasIniExtension(const std::string& filename) {
    const auto upperFilename = strToUpper(filename);
    return upperFilename.size() >= 4 && upperFilename.substr(upperFilename.size() - 4) == ".INI";
}

std::string getBaseFilenameUpper(const std::string& filepath) {
    const auto pos = filepath.find_last_of("/\\");
    return strToUpper(pos == std::string::npos ? filepath : filepath.substr(pos + 1));
}

bool isTornieModActive() {
    return ModManager::instance().isInitialized() && ModManager::instance().isTornieContentActive();
}

std::vector<std::string> getActiveModDataPaths() {
    if(!ModManager::instance().isInitialized()) {
        return {};
    }

    const auto& modManager = ModManager::instance();
    const std::string activeMod = modManager.getActiveModName();
    if(activeMod.empty() || strToUpper(activeMod) == "VANILLA") {
        return {};
    }

    std::vector<std::string> paths{
        modManager.getModPath(activeMod) + "/data"
    };
    const std::string installRoot = getDuneLegacyDataDir();
    for(const std::string& relativePrefix : {"", "/..", "/../..", "/../../.."}) {
        const std::string candidate = installRoot + relativePrefix + "/mods/" + activeMod + "/data";
        if(std::find(paths.begin(), paths.end(), candidate) == paths.end()) {
            paths.push_back(candidate);
        }
    }
    return paths;
}

bool isPakEnabledForFallbackLookup(const Pakfile& pakFile) {
    const auto pakName = getBaseFilenameUpper(pakFile.getPakFilename());
    return pakName != "TORNIE.PAK" || isTornieModActive();
}

std::vector<std::string> getFilenameCaseVariants(const std::string& filename);
bool isTinyVocPlaceholder(const std::string& filename, SDL_RWops* rwop);

sdl2::RWops_ptr openFromNamedPak(const std::vector<std::unique_ptr<Pakfile>>& pakFiles, const std::string& filename, const std::string& pakNameUpper) {
    for(const auto& pPakFile : pakFiles) {
        if(getBaseFilenameUpper(pPakFile->getPakFilename()) == pakNameUpper && pPakFile->exists(filename)) {
            auto rwop = pPakFile->openFile(filename);
            if(isTinyVocPlaceholder(filename, rwop.get())) {
                SDL_Log("FileManager: ignoring tiny VOC placeholder '%s' from %s", filename.c_str(), pakNameUpper.c_str());
                continue;
            }

            SDL_Log("FileManager: using %s for '%s'", pakNameUpper.c_str(), filename.c_str());
            return rwop;
        }
    }
    return nullptr;
}

sdl2::RWops_ptr openFromNamedPakCaseInsensitive(const std::vector<std::unique_ptr<Pakfile>>& pakFiles, const std::string& filename, const std::string& pakNameUpper) {
    for(const auto& variant : getFilenameCaseVariants(filename)) {
        if(auto rwop = openFromNamedPak(pakFiles, variant, pakNameUpper)) {
            return rwop;
        }
    }

    return nullptr;
}

sdl2::RWops_ptr openFromPriorityPak(const std::vector<std::unique_ptr<Pakfile>>& pakFiles, const std::string& filename) {
    if(isTornieModActive()) {
        if(auto rwop = openFromNamedPakCaseInsensitive(pakFiles, filename, "TORNIE.PAK")) {
            return rwop;
        }
    }

    if(auto rwop = openFromNamedPakCaseInsensitive(pakFiles, filename, "EXTRA.PAK")) {
        return rwop;
    }

    return nullptr;
}

bool isCampaignRegionFile(const std::string& filename) {
    const auto upperFilename = strToUpper(filename);
    return hasIniExtension(filename) && upperFilename.size() == 11 && upperFilename.substr(0, 6) == "REGION";
}

bool isCampaignScenarioFile(const std::string& filename) {
    const auto upperFilename = strToUpper(filename);
    return hasIniExtension(filename) && upperFilename.size() == 12 && upperFilename.substr(0, 4) == "SCEN";
}

bool isCampaignFile(const std::string& filename) {
    return isCampaignRegionFile(filename) || isCampaignScenarioFile(filename);
}

std::vector<std::string> getFilenameCaseVariants(const std::string& filename) {
    std::vector<std::string> variants{filename};

    const auto lowerFilename = strToLower(filename);
    if(std::find(variants.begin(), variants.end(), lowerFilename) == variants.end()) {
        variants.push_back(lowerFilename);
    }

    const auto upperFilename = strToUpper(filename);
    if(std::find(variants.begin(), variants.end(), upperFilename) == variants.end()) {
        variants.push_back(upperFilename);
    }

    return variants;
}

void addCampaignCandidates(std::vector<std::string>& candidates, const std::string& directory, const std::string& filename) {
    for(const auto& variant : getFilenameCaseVariants(filename)) {
        candidates.push_back(directory + "/" + variant);
    }
}

bool isVanillaCoreCampaignHouse(const char house) {
    return house == 'H' || house == 'A' || house == 'O' || house == 'F' || house == 'M' || house == 'S';
}

bool isVanillaAddonCampaignHouse(const char house) {
    return house == 'N' || house == 'R';
}

std::string getVanillaCampaignPakName(const char house) {
    if(house == 'A' || house == 'H' || house == 'O') {
        return "SCENARIO.PAK";
    }

    if(house == 'F' || house == 'M' || house == 'S') {
        return "OPENSD2.PAK";
    }

    return "";
}

char getCampaignHouseFromFilename(const std::string& filename) {
    const auto upperFilename = strToUpper(filename);
    if(isCampaignRegionFile(upperFilename)) {
        return upperFilename[6];
    }
    if(isCampaignScenarioFile(upperFilename)) {
        return upperFilename[4];
    }
    return '\0';
}

bool isVanillaCoreCampaignFile(const std::string& filename) {
    return isVanillaCoreCampaignHouse(getCampaignHouseFromFilename(filename));
}

bool isVanillaAddonCampaignFile(const std::string& filename) {
    return isVanillaAddonCampaignHouse(getCampaignHouseFromFilename(filename));
}

sdl2::RWops_ptr openExternalFileIfPresent(std::string filepath) {
    if(getCaseInsensitiveFilename(filepath)) {
        auto rwop = sdl2::RWops_ptr{SDL_RWFromFile(filepath.c_str(), "rb")};
        if(rwop) {
            return rwop;
        }
    }
    return nullptr;
}
bool isTinyVocPlaceholder(const std::string& filename, SDL_RWops* rwop) {
    const auto upperFilename = strToUpper(filename);
    if(upperFilename.size() < 4 || upperFilename.substr(upperFilename.size() - 4) != ".VOC") {
        return false;
    }

    const auto size = SDL_RWsize(rwop);
    return size >= 0 && size <= 32;
}
}

FileManager::FileManager() {
    SDL_Log("\nFileManager is loading PAK-Files...");
    SDL_Log("\nMD5-Checksum                      Filename");

    const auto search_path = getSearchPath();

    for(const auto& filename : getNeededFiles()) {
        for(const auto& sp : search_path) {
            auto filepath = sp + "/";
            filepath += filename;
            if(getCaseInsensitiveFilename(filepath)) {
                try {
                    SDL_Log("%s  %s", md5FromFilename(filepath).c_str(), filepath.c_str());
                    pakFiles.push_back(std::make_unique<Pakfile>(filepath));
                } catch (std::exception &e) {
                    pakFiles.clear();

                    THROW(io_error, "Error while opening '%s': %s!", filepath, e.what());
                }

                // break out of searchPath-loop because we have opened the file in one directory
                break;
            }
        }

    }

    SDL_Log("%s", "");
}

FileManager::~FileManager() = default;

std::vector<std::string> FileManager::getSearchPath() {
    std::vector<std::string> searchPath;

    const std::string dataDir = getDuneLegacyDataDir();
    searchPath.push_back(dataDir);
    searchPath.push_back(dataDir + "/data");
    char tmp[FILENAME_MAX];
    fnkdat("data", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);
    searchPath.push_back(tmp);

    return searchPath;
}

std::vector<std::string> FileManager::getNeededFiles() {
    std::vector<std::string> fileList = {
        "LEGACY.PAK",
        "OPENSD2.PAK",
        "GFXHD.PAK",
        "DUNE.PAK",
        "SCENARIO.PAK",
        "MENTAT.PAK",
        "VOC.PAK",
        "MERC.PAK",
        "FINALE.PAK",
        "INTRO.PAK",
        "INTROVOC.PAK",
        "SOUND.PAK",
        "Extra.PAK",
        // DuneCity 1.0.489: Tornie.PAK is the Tornie mod's
        // assets pack. Contains the custom units (Deviator,
        // Flame Tank, Sonic Tank, Elite Siege Tank), buildings
        // (Advanced Windtrap), palettes, campaigns, etc.
        // Tornie's OOB: 'Tornie.Pak is on the list of Pak?' =
        // no, it wasn't - now it is.
        "Tornie.PAK",
    };

    std::string LanguagePakFiles = (pTextManager != nullptr) ? _("LanguagePakFiles") : "";

    if(LanguagePakFiles.empty() || LanguagePakFiles == "LanguagePakFiles") {
        LanguagePakFiles = "ENGLISH.PAK,HARK.PAK,ATRE.PAK,ORDOS.PAK";
    }

    std::vector<std::string> additionalPakFiles = splitStringToStringVector(LanguagePakFiles);
    fileList.insert(std::end(fileList), std::begin(additionalPakFiles), std::end(additionalPakFiles));

    std::sort(fileList.begin(), fileList.end());

    return fileList;
}

std::vector<std::string> FileManager::getMissingFiles() {
    std::vector<std::string> MissingFiles;

    for(const std::string& fileName : getNeededFiles()) {
        bool bFound = false;
        for(const std::string& searchPath : getSearchPath()) {
            std::string filepath = searchPath + "/" + fileName;
            if(getCaseInsensitiveFilename(filepath) == true) {
                bFound = true;
                break;
            }
        }

        if(!bFound) {
            MissingFiles.push_back(fileName);
        }
    }

    return MissingFiles;
}

sdl2::RWops_ptr FileManager::openFile(const std::string& filename) {
    sdl2::RWops_ptr ret;

    // Active mod assets override the base payload, but assets from inactive
    // mods are never visible. User-installed mod data is checked first, then
    // the bundled mod directory beside the executable.
    for(const auto& modDataPath : getActiveModDataPaths()) {
        auto modFilename = modDataPath + "/" + filename;
        if(auto modFile = openExternalFileIfPresent(modFilename)) {
            if(isTinyVocPlaceholder(filename, modFile.get())) {
                SDL_Log("FileManager: ignoring tiny active-mod VOC placeholder '%s'", modFilename.c_str());
                continue;
            }
            SDL_Log("FileManager: using active-mod asset '%s'", modFilename.c_str());
            return modFile;
        }
    }

    // try loading external file
    for(const auto& searchPath : getSearchPath()) {
        auto externalFilename = searchPath + "/";
        externalFilename += filename;
        if(getCaseInsensitiveFilename(externalFilename)) {
            ret = sdl2::RWops_ptr{SDL_RWFromFile(externalFilename.c_str(), "rb")};
            if(ret) {
                if(isTinyVocPlaceholder(filename, ret.get())) {
                    SDL_Log("FileManager: ignoring tiny external VOC placeholder '%s'", externalFilename.c_str());
                    ret.reset();
                    continue;
                }
                return ret;
            }
        }
    }

    // now try loading from priority pak files
    if(auto rwop = openFromPriorityPak(pakFiles, filename)) {
        return rwop;
    }

    // now try loading from pak file
    for(const auto& pPakFile : pakFiles) {
        if(!isPakEnabledForFallbackLookup(*pPakFile)) {
            continue;
        }

        if(pPakFile->exists(filename)) {
            auto rwop = pPakFile->openFile(filename);
            if(isTinyVocPlaceholder(filename, rwop.get())) {
                SDL_Log("FileManager: ignoring tiny VOC placeholder '%s' from %s", filename.c_str(), getBaseFilenameUpper(pPakFile->getPakFilename()).c_str());
                continue;
            }

            return rwop;
        }
    }

    THROW(io_error, "Cannot find '%s'!", filename);
}

sdl2::RWops_ptr FileManager::openCampaignFile(const std::string& filename) {
    const auto upperFilename = strToUpper(filename);
    const bool familyData = upperFilename == "CAMPAIGNPLAN.INI"
        || (upperFilename.rfind("COOP/", 0) == 0 && hasIniExtension(filename));
    const bool isTornieCampaign = ModManager::instance().isTornieFamilyContentActive()
        && (isCampaignFile(filename) || familyData);

    if(isTornieCampaign) {
        const auto& manager = ModManager::instance();
        const std::string activeModName = manager.getActiveModName();
        const std::string contentBase = manager.getContentBase(activeModName);
        std::vector<std::string> candidates;
        const auto addModCandidates = [&](const std::string& name) {
            for(const auto& folder : {"/campaign", "/data"}) {
                addCampaignCandidates(candidates, manager.getModPath(name) + folder, filename);
                for(const auto& searchPath : getSearchPath()) {
                    addCampaignCandidates(candidates, searchPath + "/mods/" + name + folder, filename);
                    addCampaignCandidates(candidates, searchPath + "/../mods/" + name + folder, filename);
                }
            }
        };
        addModCandidates(activeModName);
        if(contentBase != activeModName) addModCandidates(contentBase);

        for(const auto& candidate : candidates) {
            auto rwop = openExternalFileIfPresent(candidate);
            if(rwop) {
                SDL_Log("FileManager: using %s campaign file '%s' from '%s'", contentBase.c_str(), filename.c_str(), candidate.c_str());
                return rwop;
            }
        }

        // Preserve Tornie's original archive fallback for its solo campaigns only.
        // Other editions must never borrow a mission from an unrelated campaign.
        if(contentBase == "Tornie" && isCampaignFile(filename)) {
            if(auto rwop = openFromNamedPakCaseInsensitive(pakFiles, filename, "TORNIE.PAK")) {
                SDL_Log("FileManager: using Tornie campaign file '%s' from Tornie.PAK", filename.c_str());
                return rwop;
            }
            if(auto rwop = openFromNamedPakCaseInsensitive(pakFiles, filename, "EXTRA.PAK")) {
                SDL_Log("FileManager: using fallback campaign file '%s' from Extra.PAK while Tornie is active", filename.c_str());
                return rwop;
            }
        }
        THROW(io_error, "Cannot find campaign file '%s' for content family '%s'!", filename, contentBase);
    }

    if(!isTornieCampaign && isVanillaAddonCampaignFile(filename)) {
        if(auto rwop = openFromNamedPakCaseInsensitive(pakFiles, filename, "EXTRA.PAK")) {
            SDL_Log("FileManager: using vanilla add-on campaign file '%s' from Extra.PAK", filename.c_str());
            return rwop;
        }

        std::vector<std::string> candidates;
        for(const auto& searchPath : getSearchPath()) {
            addCampaignCandidates(candidates, searchPath + "/campaign_vanilla", filename);
            addCampaignCandidates(candidates, searchPath + "/../campaign_vanilla", filename);
            addCampaignCandidates(candidates, searchPath + "/../data/campaign_vanilla", filename);
        }

        for(const auto& candidate : candidates) {
            auto rwop = openExternalFileIfPresent(candidate);
            if(rwop) {
                SDL_Log("FileManager: using vanilla add-on campaign file '%s' from '%s'", filename.c_str(), candidate.c_str());
                return rwop;
            }
        }

        THROW(io_error, "Cannot find vanilla add-on campaign file '%s' in Extra.PAK or campaign_vanilla!", filename);
    }

    if(!isTornieCampaign && isVanillaCoreCampaignFile(filename)) {
        const auto pakName = getVanillaCampaignPakName(getCampaignHouseFromFilename(filename));
        if(!pakName.empty()) {
            if(auto rwop = openFromNamedPakCaseInsensitive(pakFiles, filename, pakName)) {
                SDL_Log("FileManager: using vanilla campaign file '%s' from %s", filename.c_str(), pakName.c_str());
                return rwop;
            }

            if(auto rwop = openFromNamedPakCaseInsensitive(pakFiles, filename, "EXTRA.PAK")) {
                SDL_Log("FileManager: using fallback vanilla campaign file '%s' from Extra.PAK", filename.c_str());
                return rwop;
            }

            THROW(io_error, "Cannot find vanilla campaign file '%s' in %s or Extra.PAK!", filename, pakName);
        }
    }

    return openFile(filename);
}
sdl2::RWops_ptr FileManager::openFileFromPak(const std::string& filename) {
    for(const auto& pPakFile : pakFiles) {
        if(!isPakEnabledForFallbackLookup(*pPakFile)) {
            continue;
        }

        if(pPakFile->exists(filename)) {
            auto rwop = pPakFile->openFile(filename);
            if(isTinyVocPlaceholder(filename, rwop.get())) {
                SDL_Log("FileManager: ignoring tiny VOC placeholder '%s' from %s", filename.c_str(), getBaseFilenameUpper(pPakFile->getPakFilename()).c_str());
                continue;
            }

            return rwop;
        }
    }

    THROW(io_error, "Cannot find '%s' in loaded PAK files!", filename);
}

sdl2::RWops_ptr FileManager::openFileFromNamedPak(const std::string& filename, const std::string& pakName) {
    return openFromNamedPakCaseInsensitive(pakFiles, filename, strToUpper(pakName));
}

bool FileManager::exists(const std::string& filename) const {

    for(const auto& modDataPath : getActiveModDataPaths()) {
        auto modFilename = modDataPath + "/" + filename;
        if(getCaseInsensitiveFilename(modFilename)) {
            return true;
        }
    }

    // try finding external file
    for(const std::string& searchPath : getSearchPath()) {
        auto externalFilename = searchPath + "/";
        externalFilename += filename;
        if(getCaseInsensitiveFilename(externalFilename)) {
            return true;
        }
    }

    // now try finding in one pak file
    for(const auto& pPakFile : pakFiles) {
        if(!isPakEnabledForFallbackLookup(*pPakFile)) {
            continue;
        }

        if(pPakFile->exists(filename)) {
            return true;
        }
    }

    return false;
}

bool FileManager::existsInPak(const std::string& filename) const {
    for(const auto& pPakFile : pakFiles) {
        if(!isPakEnabledForFallbackLookup(*pPakFile)) {
            continue;
        }

        if(pPakFile->exists(filename)) {
            return true;
        }
    }

    return false;
}


std::string FileManager::md5FromFilename(const std::string& filename) const {
    unsigned char md5sum[16];

    if(md5_file(filename.c_str(), md5sum) != 0) {
        THROW(io_error, "Cannot open or read '%s'!", filename);
    } else {

        std::stringstream stream;
        stream << std::setfill('0') << std::hex;
        for(int i : md5sum) {
            stream << std::setw(2) << i;
        }
        return stream.str();
    }
}
