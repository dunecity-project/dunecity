// SPDX-License-Identifier: GPL-2.0-or-later
#include <CutScenes/CampaignStory.h>

#include <Campaign/CampaignPlan.h>
#include <FileClasses/FileManager.h>
#include <FileClasses/Wsafile.h>
#include <globals.h>
#include <mod/ModManager.h>
#include <sand.h>

#include <stdexcept>
#include <utility>

CampaignStory::CampaignStory(int house) : model(house), enabled(ModManager::instance().isTornieFamilyContentActive()) {
    for(int i = 0; i < NUM_HOUSES; ++i) roles[i] = i;
    if(!enabled) return;
    if(!isPlayerHouseID(house)) throw std::invalid_argument("Invalid campaign story house");

    const auto identity = getHouseFactionIdentity(static_cast<HOUSETYPE>(house));
    const auto fallback = identity == HOUSE_KLESHMERSH ? HOUSE_HARKONNEN
        : getHouseFallbackHouse(static_cast<HOUSETYPE>(house));
    switch(fallback) {
        case HOUSE_ATREIDES: case HOUSE_FREMEN: case HOUSE_REBELS: model = HOUSE_ATREIDES; break;
        case HOUSE_ORDOS: case HOUSE_MERCENARY: case HOUSE_NEUTRAL: model = HOUSE_ORDOS; break;
        default: model = HOUSE_HARKONNEN; break;
    }

    INIFile ini(pFileManager->openCampaignFile("CampaignPlan.ini").get());
    const auto plan = campaign::readCampaignPlan(ini, getHouseNameByNumber(static_cast<HOUSETYPE>(house)));
    if(plan) {
        model = plan->templateLetter == 'H' ? HOUSE_HARKONNEN
            : plan->templateLetter == 'A' ? HOUSE_ATREIDES : HOUSE_ORDOS;
        const int first = model == HOUSE_HARKONNEN ? HOUSE_ATREIDES
            : model == HOUSE_ATREIDES ? HOUSE_ORDOS : HOUSE_HARKONNEN;
        const int second = model == HOUSE_HARKONNEN ? HOUSE_ORDOS
            : model == HOUSE_ATREIDES ? HOUSE_HARKONNEN : HOUSE_ATREIDES;
        const int source[] = {first, second, HOUSE_SARDAUKAR};
        for(int role = 0; role < 3; ++role) {
            const auto opponent = getHouseByName(plan->opponents[role]);
            if(!isPlayerHouseID(opponent)) throw std::runtime_error("Invalid campaign story opponent");
            roles[source[role]] = opponent;
        }
    }
    roles[model] = house;
    auto scenePalette = std::make_shared<Palette>(palette);
    const int storyHouses[] = {HOUSE_HARKONNEN, HOUSE_ATREIDES, HOUSE_ORDOS, HOUSE_SARDAUKAR};
    for(int i = 0; i < 4; ++i) {
        const int original = storyHouses[i];
        roleNames[i] = getHouseNameByNumber(static_cast<HOUSETYPE>(roles[original]));
        for(int shade = 0; shade < 8; ++shade)
            (*scenePalette)[houseToPaletteIndex[original] + shade] = getHouseColorSDL(getHouseVisualHouse(roles[original]), shade);
    }
    paletteOverride = std::move(scenePalette);
}

void CampaignStory::apply(Wsafile* animation) const {
    if(paletteOverride && animation) animation->setPaletteOverride(paletteOverride);
}

std::string CampaignStory::text(const std::string& original) const {
    if(!enabled) return original;
    return campaign::replaceCampaignHouseNames(original, {roleNames[0], roleNames[1], roleNames[2], roleNames[3]});
}
