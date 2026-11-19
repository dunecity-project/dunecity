// SPDX-License-Identifier: GPL-2.0-or-later
#include <misc/ReworkedCampaign.h>
#include <misc/DuneCityCampaign.h>

#include <FileClasses/FileManager.h>
#include <FileClasses/FontManager.h>
#include <globals.h>
#include <misc/FileSystem.h>
#include <misc/draw_util.h>

namespace ReworkedCampaign {

namespace {
bool watermarkVisible = false;

bool bundledMapExists(const std::string& name) {
    for(const auto& searchPath : FileManager::getSearchPath()) {
        std::string path = searchPath + "/" + kMapDirectory + "/" + name;
        if(getCaseInsensitiveFilename(path)) return true;
    }
    return false;
}
} // namespace

bool isAvailableForHouse(HOUSETYPE house) {
    return isCompleteSet(getHouseScenarioLetter(house), bundledMapExists);
}

void setWatermarkVisible(bool visible) { watermarkVisible = visible; }
bool isWatermarkVisible() { return watermarkVisible; }

void drawWatermark(int right, int y, bool fromBottom) {
    if(!pFontManager || !renderer) return;
    // A gold label on a dark plate: readable on the map, the planet and the menu art.
    const auto text = pFontManager->createTextureWithText(kWatermark, COLOR_RGB(236, 196, 96), 14);
    if(!text) return;
    SDL_SetTextureAlphaMod(text.get(), 230);
    int w = 0, h = 0;
    SDL_QueryTexture(text.get(), nullptr, nullptr, &w, &h);
    SDL_Rect plate { right - w - 16, fromBottom ? y - h - 6 : y, w + 16, h + 6 };
    renderFillRect(renderer, &plate, COLOR_RGBA(0, 0, 0, 150));
    renderDrawRect(renderer, &plate, COLOR_RGBA(236, 196, 96, 170));
    SDL_Rect dest { plate.x + 8, plate.y + 3, w, h };
    SDL_RenderCopy(renderer, text.get(), nullptr, &dest);
}

} // namespace ReworkedCampaign

namespace DuneCityCampaign {

bool isAvailable() {
    for(int mission = 1; mission <= kMissions; ++mission) {
        bool found = false;
        for(const auto& searchPath : FileManager::getSearchPath()) {
            std::string path = searchPath + "/" + kMapDirectory + "/" + scenarioFilename(mission);
            if(getCaseInsensitiveFilename(path)) {
                found = true;
                break;
            }
        }
        if(!found) return false;
    }
    return true;
}

} // namespace DuneCityCampaign
