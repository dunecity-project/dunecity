#include <INIMap/CampaignMapLibrary.h>

#include <FileClasses/FileManager.h>
#include <globals.h>
#include <mod/ModManager.h>
#include <misc/FileSystem.h>
#include <misc/fnkdat.h>
#include <sand.h>

#include <SDL.h>

namespace CampaignMapLibrary {

std::string directory() {
    char path[FILENAME_MAX];
    if(fnkdat("maps/campaign/", path, sizeof(path), FNKDAT_USER | FNKDAT_CREAT) < 0) return "";
    std::string dir = std::string(path) + ModManager::instance().getActiveModName() + "/";
    createDir(dir);
    return dir;
}

std::string exportMaps() {
    const std::string dir = directory();
    if(dir.empty() || !pFileManager) return dir;
    for(int house = 0; house < NUM_PLAYER_HOUSES; ++house) {
        const auto houseID = static_cast<HOUSETYPE>(house);
        if(!isHouseAvailable(houseID)) continue;
        for(int mission = 1; mission <= 22; ++mission) {
            try {
                // Same naming as the campaign itself (GameInitSettings::getScenarioFilename).
                std::string scenario = "SCEN?0??.INI";
                scenario[4] = getHouseScenarioLetter(houseID);
                scenario[6] = static_cast<char>('0' + mission / 10);
                scenario[7] = static_cast<char>('0' + mission % 10);
                const std::string target = dir + getHouseNameByNumber(houseID) + " Mission " + missionLabel(mission) + ".ini";
                if(existsFile(target) || !pFileManager->exists(scenario)) continue;
                auto rw = pFileManager->openFile(scenario);
                const Sint64 size = SDL_RWsize(rw.get());
                if(size <= 0) continue;
                std::string bytes(static_cast<size_t>(size), '\0');
                if(SDL_RWread(rw.get(), bytes.data(), 1, bytes.size()) != bytes.size()) continue;
                writeCompleteFile(target, bytes);
            } catch(const std::exception& e) {
                SDL_Log("Campaign maps: %s", e.what());
            }
        }
    }
    return dir;
}

} // namespace CampaignMapLibrary
