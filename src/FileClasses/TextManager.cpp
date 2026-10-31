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

#include <FileClasses/TextManager.h>
#include <mod/ModManager.h>

#include <sand.h>
#include <globals.h>

#include <misc/string_util.h>

#include <FileClasses/FileManager.h>
#include <FileClasses/POFile.h>

#include <misc/FileSystem.h>
#include <misc/exceptions.h>

#include <vector>
#include <utility>

#ifdef _
#undef _
#endif
#define _(msgid) getLocalized(msgid)

TextManager::TextManager() {
    const std::vector<std::string> localeDirs = {
        getDuneLegacyDataDir() + "/locale",
        getDuneLegacyDataDir() + "/data/locale",
    };

    std::string languageFilename;
    std::string languageFilepath;
    for(const std::string& localeDir : localeDirs) {
        std::list<std::string> languagesList = getFileNamesList(localeDir, settings.general.language + ".po", true, FileListOrder_Name_Asc);
        if(!languagesList.empty()) {
            languageFilename = languagesList.front();
            languageFilepath = localeDir + "/" + languageFilename;
            break;
        }
    }

    if(languageFilepath.empty()) {
        languageFilename = "English.en.po";
        for(const std::string& localeDir : localeDirs) {
            std::string candidate = localeDir + "/" + languageFilename;
            if(existsFile(candidate)) {
                languageFilepath = candidate;
                break;
            }
        }
        if(languageFilepath.empty()) {
            languageFilepath = localeDirs.front() + "/" + languageFilename;
        }
    }

    SDL_Log("Loading localization from '%s'...", languageFilepath.c_str());
    auto rwops = sdl2::RWops_ptr{ SDL_RWFromFile(languageFilepath.c_str(), "r") };
    localizedString = loadPOFile(rwops.get(), languageFilename);
}

TextManager::~TextManager() = default;

void TextManager::loadData() {
    campaignTextPlans.clear();
    if(ModManager::instance().isTornieFamilyContentActive()) {
        INIFile ini(pFileManager->openCampaignFile("CampaignPlan.ini").get());
        for(const int house : ModManager::instance().getPlayableHouses()) {
            const auto name = getHouseNameByNumber(static_cast<HOUSETYPE>(house));
            if(auto plan = campaign::readCampaignPlan(ini, name))
                campaignTextPlans.emplace(house, std::move(*plan));
        }
    }
    origDuneText.clear();
    for(auto& mentatString : mentatStrings) {
        mentatString.reset();
    }

    addOrigDuneText("TEXTH." + _("LanguageFileExtension"), true);
    addOrigDuneText("TEXTA." + _("LanguageFileExtension"), true);
    addOrigDuneText("TEXTO." + _("LanguageFileExtension"), true);
    addOrigDuneText("DUNE." + _("LanguageFileExtension"));
    addOrigDuneText("MESSAGE." + _("LanguageFileExtension"));

    // load all mentat texts
    mentatStrings[HOUSE_HARKONNEN] = std::make_unique<MentatTextFile>(pFileManager->openFile("MENTATH." + _("LanguageFileExtension")).get());
    mentatStrings[HOUSE_ATREIDES] = std::make_unique<MentatTextFile>(pFileManager->openFile("MENTATA." + _("LanguageFileExtension")).get());
    mentatStrings[HOUSE_ORDOS] = std::make_unique<MentatTextFile>(pFileManager->openFile("MENTATO." + _("LanguageFileExtension")).get());
}

std::string TextManager::getBriefingText(unsigned int mission, unsigned int texttype, int house) const {
    if(ModManager::instance().isTornieFamilyContentActive()) {
        const bool french = settings.general.language == "fr";
        const auto name = getHouseNameByNumber(static_cast<HOUSETYPE>(house));
        const auto entry = campaignTextPlans.find(house);
        if(mission > 0 && mission <= 9 && entry != campaignTextPlans.end()) {
            const auto& plan = entry->second;
            if(mission == 1 && texttype == MISSION_DESCRIPTION) {
                if(plan.revision != 2) {
                    return (french ? "Maison " : "House ") + name
                        + (french ? u8". Établissez votre économie d'épice et protégez vos moissonneuses. "
                                  : ". Establish your spice economy and protect your harvesters. ")
                        + (plan.openingQuota > 0 ? (french ? "Objectif : " : "Objective: ")
                           + std::to_string(plan.openingQuota) + (french ? u8" crédits. " : " credits. ") : "")
                        + (french ? "Premier adversaire : " : "First opponent: ") + plan.opponents[0] + ".";
                }
                const char letter = getHouseScenarioLetter(static_cast<HOUSETYPE>(house));
                const std::string opening = letter == 'R'
                    ? (french ? u8". Votre port stellaire et votre petite exploitation remplacent le chantier initial. Achetez un VCM au CHOAM pour commencer les constructions. "
                              : ". Your starport and small spice operation replace the initial construction yard. Buy an MCV from CHOAM to begin construction. ")
                    : letter == 'W'
                    ? (french ? u8". Reconnaissez les approches avec vos appareils prêtés, utilisez le Carryall pour la logistique et gardez vos avions hors de portée des missiles. "
                              : ". Scout with your loaned aircraft, use the Carryall for logistics and keep your aircraft clear of missiles. ")
                    : (french ? u8". Établissez votre économie d'épice et protégez vos moissonneuses. "
                              : ". Establish your spice economy and protect your harvesters. ");
                return (french ? "Maison " : "House ") + name + opening
                    + (plan.openingQuota > 0 ? (french ? "Objectif : " : "Objective: ")
                       + std::to_string(plan.openingQuota) + (french ? u8" crédits. " : " credits. ") : "")
                    + (french ? "Adversaire principal : " : "Main opponent: ") + plan.stageOpponents[0][0]
                    + (french ? ", soutenu par " : ", supported by ") + plan.stageOpponents[0][1]
                    + (french ? u8". Deux petites bases ennemies et des renforts espacés menacent votre exploitation. Vous commencez avec 1 500 crédits ; le quota compte l'épice raffinée."
                              : ". Two small enemy bases and staggered reinforcements threaten your operation. You start with 1,500 credits; the quota counts refined spice.");
            }
            const auto filename = std::string("TEXT") + plan.templateLetter + "." + _("LanguageFileExtension");
            const auto source = origDuneText.find(filename);
            const unsigned offset = texttype == MISSION_WIN ? 1 : texttype == MISSION_LOSE ? 2
                : texttype == MISSION_ADVICE ? 3 : 0;
            const unsigned index = mission * 4 + offset;
            if(source != origDuneText.end() && index < source->second->getNumStrings()) {
                const bool harkonnen = plan.templateLetter == 'H', atreides = plan.templateLetter == 'A';
                const std::array<std::string_view, 4> names = {
                    harkonnen ? name : atreides ? plan.opponents[1] : plan.opponents[0],
                    harkonnen ? plan.opponents[0] : atreides ? name : plan.opponents[1],
                    harkonnen ? plan.opponents[1] : atreides ? plan.opponents[0] : name,
                    plan.opponents[2]
                };
                auto result = campaign::replaceCampaignHouseNames(source->second->getString(index), names);
                if(plan.revision == 2 && texttype == MISSION_DESCRIPTION) {
                    const auto& enemies = plan.stageOpponents[mission - 1];
                    std::string roster = (french ? "Adversaire principal : " : "Main opponent: ") + enemies[0]
                        + (french ? ". Soutien permanent : " : ". Persistent support: ") + enemies[1] + ". ";
                    for(int role = 2; role < 4; ++role) {
                        if(!enemies[role].empty())
                            roster += (french ? "Adversaire additionnel : " : "Additional opponent: ") + enemies[role] + ". ";
                    }
                    result.insert(0, roster);
                }
                return result;
            }
            // Some original archives omit the final advice entry; do not reuse a legacy faction's index.
            if(texttype == MISSION_WIN) return name + (french ? u8" a remporté cette bataille." : " has won this battle.");
            if(texttype == MISSION_LOSE) return name + (french ? u8" doit reconstruire ses forces et réessayer." : " must rebuild its forces and try again.");
            return name + (french ? u8" : protégez vos moissonneuses et développez votre base."
                                 : ": protect your harvesters and develop your base.");
        }
        if(house == HOUSE_CUSTOM || (house >= HOUSE_WILDSPADE && house < NUM_PLAYER_HOUSES)) {
            if(mission == 0) {
                const char* description = house == HOUSE_WILDSPADE
                    ? (french ? "Les Wildspade prennent le ciel pour frapper leurs adversaires sur Arrakis."
                              : "Wildspade takes to the skies to strike its opponents on Arrakis.")
                    : house == HOUSE_KLESHMERSH
                    ? (french ? u8"Kleshmersh trace sa propre voie sur Arrakis, entre puissance militaire et commerce de l'épice."
                              : "Kleshmersh follows its own path on Arrakis through military strength and the spice trade.")
                    : house == HOUSE_THARPIQUE
                    ? (french ? u8"Tharpique combine mobilité et technologies spécialisées pour conquérir Arrakis."
                              : "Tharpique combines mobility and specialized technologies to conquer Arrakis.")
                    : (french ? u8"Corruptique déploie un arsenal lourd pour prendre le contrôle de l'épice."
                              : "Corruptique deploys a heavy arsenal to take control of the spice.");
                return (french ? "Maison " : "House ") + name + "\n" + description;
            }
            if(texttype == MISSION_WIN) return name + (french ? u8" a remporté cette opération sur Arrakis." : " has won this operation on Arrakis.");
            if(texttype == MISSION_LOSE) return name + (french ? u8" a perdu cette bataille. Reconstruisez vos forces et réessayez." : " has lost this battle. Rebuild your forces and try again.");
            return (french ? "Commandez la maison " : "Command House ") + name
                + (french ? u8". Développez votre base, protégez vos moissonneuses et accomplissez les objectifs de la mission."
                          : ". Develop your base, protect your harvesters and complete the mission objectives.");
        }
    }
    switch(house) {
        case HOUSE_HARKONNEN: {
            switch(texttype) {
                case MISSION_DESCRIPTION: {
                    switch(mission) {
                        case 0: return _("@TEXTH.ENG|0#Description of House Harkonnen");       break;
                        case 1: return _("@TEXTH.ENG|4#Description of Harkonnen mission 1");   break;
                        case 2: return _("@TEXTH.ENG|8#Description of Harkonnen mission 2");   break;
                        case 3: return _("@TEXTH.ENG|12#Description of Harkonnen mission 3");  break;
                        case 4: return _("@TEXTH.ENG|16#Description of Harkonnen mission 4");  break;
                        case 5: return _("@TEXTH.ENG|20#Description of Harkonnen mission 5");  break;
                        case 6: return _("@TEXTH.ENG|24#Description of Harkonnen mission 6");  break;
                        case 7: return _("@TEXTH.ENG|28#Description of Harkonnen mission 7");  break;
                        case 8: return _("@TEXTH.ENG|32#Description of Harkonnen mission 8");  break;
                        case 9: return _("@TEXTH.ENG|36#Description of Harkonnen mission 9");  break;
                        default: return "";                                                    break;
                    }
                } break;

                case MISSION_WIN: {
                    switch(mission) {
                        case 1: return _("@TEXTH.ENG|5#Winning text of Harkonnen mission 1");   break;
                        case 2: return _("@TEXTH.ENG|9#Winning text of Harkonnen mission 2");   break;
                        case 3: return _("@TEXTH.ENG|13#Winning text of Harkonnen mission 3");  break;
                        case 4: return _("@TEXTH.ENG|17#Winning text of Harkonnen mission 4");  break;
                        case 5: return _("@TEXTH.ENG|21#Winning text of Harkonnen mission 5");  break;
                        case 6: return _("@TEXTH.ENG|25#Winning text of Harkonnen mission 6");  break;
                        case 7: return _("@TEXTH.ENG|29#Winning text of Harkonnen mission 7");  break;
                        case 8: return _("@TEXTH.ENG|33#Winning text of Harkonnen mission 8");  break;
                        case 9: return _("@TEXTH.ENG|37#Winning text of Harkonnen mission 9");  break;
                        default: return "";                                                     break;
                    }
                } break;

                case MISSION_LOSE: {
                    switch(mission) {
                        case 1: return _("@TEXTH.ENG|6#Losing text of Harkonnen mission 1");   break;
                        case 2: return _("@TEXTH.ENG|10#Losing text of Harkonnen mission 2");  break;
                        case 3: return _("@TEXTH.ENG|14#Losing text of Harkonnen mission 3");  break;
                        case 4: return _("@TEXTH.ENG|18#Losing text of Harkonnen mission 4");  break;
                        case 5: return _("@TEXTH.ENG|22#Losing text of Harkonnen mission 5");  break;
                        case 6: return _("@TEXTH.ENG|26#Losing text of Harkonnen mission 6");  break;
                        case 7: return _("@TEXTH.ENG|30#Losing text of Harkonnen mission 7");  break;
                        case 8: return _("@TEXTH.ENG|34#Losing text of Harkonnen mission 8");  break;
                        case 9: return _("@TEXTH.ENG|38#Losing text of Harkonnen mission 9");  break;
                        default: return "";                                                    break;
                    }
                } break;

                case MISSION_ADVICE: {
                    switch(mission) {
                        case 1: return _("@TEXTH.ENG|7#Advice text for Harkonnen mission 1");   break;
                        case 2: return _("@TEXTH.ENG|11#Advice text for Harkonnen mission 2");  break;
                        case 3: return _("@TEXTH.ENG|15#Advice text for Harkonnen mission 3");  break;
                        case 4: return _("@TEXTH.ENG|19#Advice text for Harkonnen mission 4");  break;
                        case 5: return _("@TEXTH.ENG|23#Advice text for Harkonnen mission 5");  break;
                        case 6: return _("@TEXTH.ENG|27#Advice text for Harkonnen mission 6");  break;
                        case 7: return _("@TEXTH.ENG|31#Advice text for Harkonnen mission 7");  break;
                        case 8: return _("@TEXTH.ENG|35#Advice text for Harkonnen mission 8");  break;
                        case 9: return _("@TEXTH.ENG|39#Advice text for Harkonnen mission 9");  break;
                        default: return "";                                                     break;
                    }
                } break;

                default: {
                    return "";
                } break;
            }
        } break;

        case HOUSE_ATREIDES: {
            switch(texttype) {
                case MISSION_DESCRIPTION: {
                    switch(mission) {
                        case 0: return _("@TEXTA.ENG|0#Description of House Atreides");       break;
                        case 1: return _("@TEXTA.ENG|4#Description of Atreides mission 1");   break;
                        case 2: return _("@TEXTA.ENG|8#Description of Atreides mission 2");   break;
                        case 3: return _("@TEXTA.ENG|12#Description of Atreides mission 3");  break;
                        case 4: return _("@TEXTA.ENG|16#Description of Atreides mission 4");  break;
                        case 5: return _("@TEXTA.ENG|20#Description of Atreides mission 5");  break;
                        case 6: return _("@TEXTA.ENG|24#Description of Atreides mission 6");  break;
                        case 7: return _("@TEXTA.ENG|28#Description of Atreides mission 7");  break;
                        case 8: return _("@TEXTA.ENG|32#Description of Atreides mission 8");  break;
                        case 9: return _("@TEXTA.ENG|36#Description of Atreides mission 9");  break;
                        default: return "";                                                   break;
                    }
                } break;

                case MISSION_WIN: {
                    switch(mission) {
                        case 1: return _("@TEXTA.ENG|5#Winning text of Atreides mission 1");   break;
                        case 2: return _("@TEXTA.ENG|9#Winning text of Atreides mission 2");   break;
                        case 3: return _("@TEXTA.ENG|13#Winning text of Atreides mission 3");  break;
                        case 4: return _("@TEXTA.ENG|17#Winning text of Atreides mission 4");  break;
                        case 5: return _("@TEXTA.ENG|21#Winning text of Atreides mission 5");  break;
                        case 6: return _("@TEXTA.ENG|25#Winning text of Atreides mission 6");  break;
                        case 7: return _("@TEXTA.ENG|29#Winning text of Atreides mission 7");  break;
                        case 8: return _("@TEXTA.ENG|33#Winning text of Atreides mission 8");  break;
                        case 9: return _("@TEXTA.ENG|37#Winning text of Atreides mission 9");  break;
                        default: return "";                                                    break;
                    }
                } break;

                case MISSION_LOSE: {
                    switch(mission) {
                        case 1: return _("@TEXTA.ENG|6#Losing text of Atreides mission 1");   break;
                        case 2: return _("@TEXTA.ENG|10#Losing text of Atreides mission 2");  break;
                        case 3: return _("@TEXTA.ENG|14#Losing text of Atreides mission 3");  break;
                        case 4: return _("@TEXTA.ENG|18#Losing text of Atreides mission 4");  break;
                        case 5: return _("@TEXTA.ENG|22#Losing text of Atreides mission 5");  break;
                        case 6: return _("@TEXTA.ENG|26#Losing text of Atreides mission 6");  break;
                        case 7: return _("@TEXTA.ENG|30#Losing text of Atreides mission 7");  break;
                        case 8: return _("@TEXTA.ENG|34#Losing text of Atreides mission 8");  break;
                        case 9: return _("@TEXTA.ENG|38#Losing text of Atreides mission 9");  break;
                        default: return "";                                                   break;
                    }
                } break;

                case MISSION_ADVICE: {
                    switch(mission) {
                        case 1: return _("@TEXTA.ENG|7#Advice text for Atreides mission 1");   break;
                        case 2: return _("@TEXTA.ENG|11#Advice text for Atreides mission 2");  break;
                        case 3: return _("@TEXTA.ENG|15#Advice text for Atreides mission 3");  break;
                        case 4: return _("@TEXTA.ENG|19#Advice text for Atreides mission 4");  break;
                        case 5: return _("@TEXTA.ENG|23#Advice text for Atreides mission 5");  break;
                        case 6: return _("@TEXTA.ENG|27#Advice text for Atreides mission 6");  break;
                        case 7: return _("@TEXTA.ENG|31#Advice text for Atreides mission 7");  break;
                        case 8: return _("@TEXTA.ENG|35#Advice text for Atreides mission 8");  break;
                        case 9: return _("@TEXTA.ENG|39#Advice text for Atreides mission 9");  break;
                        default: return "";                                                    break;
                    }
                } break;

                default: {
                    return "";
                } break;
            }
        } break;

        case HOUSE_ORDOS: {
            switch(texttype) {
                case MISSION_DESCRIPTION: {
                    switch(mission) {
                        case 0: return _("@TEXTO.ENG|0#Description of House Ordos");       break;
                        case 1: return _("@TEXTO.ENG|4#Description of Ordos mission 1");   break;
                        case 2: return _("@TEXTO.ENG|8#Description of Ordos mission 2");   break;
                        case 3: return _("@TEXTO.ENG|12#Description of Ordos mission 3");  break;
                        case 4: return _("@TEXTO.ENG|16#Description of Ordos mission 4");  break;
                        case 5: return _("@TEXTO.ENG|20#Description of Ordos mission 5");  break;
                        case 6: return _("@TEXTO.ENG|24#Description of Ordos mission 6");  break;
                        case 7: return _("@TEXTO.ENG|28#Description of Ordos mission 7");  break;
                        case 8: return _("@TEXTO.ENG|32#Description of Ordos mission 8");  break;
                        case 9: return _("@TEXTO.ENG|36#Description of Ordos mission 9");  break;
                        default: return "";                                                break;
                    }
                } break;

                case MISSION_WIN: {
                    switch(mission) {
                        case 1: return _("@TEXTO.ENG|5#Winning text of Ordos mission 1");   break;
                        case 2: return _("@TEXTO.ENG|9#Winning text of Ordos mission 2");   break;
                        case 3: return _("@TEXTO.ENG|13#Winning text of Ordos mission 3");  break;
                        case 4: return _("@TEXTO.ENG|17#Winning text of Ordos mission 4");  break;
                        case 5: return _("@TEXTO.ENG|21#Winning text of Ordos mission 5");  break;
                        case 6: return _("@TEXTO.ENG|25#Winning text of Ordos mission 6");  break;
                        case 7: return _("@TEXTO.ENG|29#Winning text of Ordos mission 7");  break;
                        case 8: return _("@TEXTO.ENG|33#Winning text of Ordos mission 8");  break;
                        case 9: return _("@TEXTO.ENG|37#Winning text of Ordos mission 9");  break;
                        default: return "";                                                 break;
                    }
                } break;

                case MISSION_LOSE: {
                    switch(mission) {
                        case 1: return _("@TEXTO.ENG|6#Losing text of Ordos mission 1");   break;
                        case 2: return _("@TEXTO.ENG|10#Losing text of Ordos mission 2");  break;
                        case 3: return _("@TEXTO.ENG|14#Losing text of Ordos mission 3");  break;
                        case 4: return _("@TEXTO.ENG|18#Losing text of Ordos mission 4");  break;
                        case 5: return _("@TEXTO.ENG|22#Losing text of Ordos mission 5");  break;
                        case 6: return _("@TEXTO.ENG|26#Losing text of Ordos mission 6");  break;
                        case 7: return _("@TEXTO.ENG|30#Losing text of Ordos mission 7");  break;
                        case 8: return _("@TEXTO.ENG|34#Losing text of Ordos mission 8");  break;
                        case 9: return _("@TEXTO.ENG|38#Losing text of Ordos mission 9");  break;
                        default: return "";                                                break;
                    }
                } break;

                case MISSION_ADVICE: {
                    switch(mission) {
                        case 1: return _("@TEXTO.ENG|7#Advice text for Ordos mission 1");   break;
                        case 2: return _("@TEXTO.ENG|11#Advice text for Ordos mission 2");  break;
                        case 3: return _("@TEXTO.ENG|15#Advice text for Ordos mission 3");  break;
                        case 4: return _("@TEXTO.ENG|19#Advice text for Ordos mission 4");  break;
                        case 5: return _("@TEXTO.ENG|23#Advice text for Ordos mission 5");  break;
                        case 6: return _("@TEXTO.ENG|27#Advice text for Ordos mission 6");  break;
                        case 7: return _("@TEXTO.ENG|31#Advice text for Ordos mission 7");  break;
                        case 8: return _("@TEXTO.ENG|35#Advice text for Ordos mission 8");  break;
                        case 9: return _("@TEXTO.ENG|39#Advice text for Ordos mission 9");  break;
                        default: return "";                                                 break;
                    }
                } break;

                default: {
                    return "";
                } break;
            }
        } break;

        case HOUSE_FREMEN: {
            switch(texttype) {
                case MISSION_DESCRIPTION: {
                    switch(mission) {
                        case 0: return _("House Fremen\nArrakis, the home planet of House Fremen, is one giant sand desert. Perfectly adapted to the warm, dry climate, the Fremen have huge advantage in defending their planet.");       break;
                        case 1: return _("@TEXTA.ENG|4|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Description of Fremen mission 1");   break;
                        case 2: return _("@TEXTA.ENG|8|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Description of Fremen mission 2");   break;
                        case 3: return _("@TEXTA.ENG|12|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Description of Fremen mission 3");  break;
                        case 4: return _("@TEXTA.ENG|16|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Description of Fremen mission 4");  break;
                        case 5: return _("@TEXTA.ENG|20|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Description of Fremen mission 5");  break;
                        case 6: return _("@TEXTA.ENG|24|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Description of Fremen mission 6");  break;
                        case 7: return _("@TEXTA.ENG|28|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Description of Fremen mission 7");  break;
                        case 8: return _("@TEXTA.ENG|32|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Description of Fremen mission 8");  break;
                        case 9: return _("@TEXTA.ENG|36|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary|remaining Ordos and Harkonnen troops->remaining Ordos, Mercenary and Harkonnen troops#Description of Fremen mission 9");  break;
                        default: return "";                                                   break;
                    }
                } break;

                case MISSION_WIN: {
                    switch(mission) {
                        case 1: return _("@TEXTA.ENG|5|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Winning text of Fremen mission 1");   break;
                        case 2: return _("@TEXTA.ENG|9|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Winning text of Fremen mission 2");   break;
                        case 3: return _("@TEXTA.ENG|13|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Winning text of Fremen mission 3");  break;
                        case 4: return _("@TEXTA.ENG|17|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Winning text of Fremen mission 4");  break;
                        case 5: return _("@TEXTA.ENG|21|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Winning text of Fremen mission 5");  break;
                        case 6: return _("@TEXTA.ENG|25|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Winning text of Fremen mission 6");  break;
                        case 7: return _("@TEXTA.ENG|29|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Winning text of Fremen mission 7");  break;
                        case 8: return _("@TEXTA.ENG|33|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Winning text of Fremen mission 8");  break;
                        case 9: return _("@TEXTA.ENG|37|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Winning text of Fremen mission 9");  break;
                        default: return "";                                                    break;
                    }
                } break;

                case MISSION_LOSE: {
                    switch(mission) {
                        case 1: return _("@TEXTA.ENG|6|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Losing text of Fremen mission 1");   break;
                        case 2: return _("@TEXTA.ENG|10|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Losing text of Fremen mission 2");  break;
                        case 3: return _("@TEXTA.ENG|14|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Losing text of Fremen mission 3");  break;
                        case 4: return _("@TEXTA.ENG|18|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Losing text of Fremen mission 4");  break;
                        case 5: return _("@TEXTA.ENG|22|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Losing text of Fremen mission 5");  break;
                        case 6: return _("@TEXTA.ENG|26|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Losing text of Fremen mission 6");  break;
                        case 7: return _("@TEXTA.ENG|30|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Losing text of Fremen mission 7");  break;
                        case 8: return _("@TEXTA.ENG|34|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Losing text of Fremen mission 8");  break;
                        case 9: return _("@TEXTA.ENG|38|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Losing text of Fremen mission 9");  break;
                        default: return "";                                                   break;
                    }
                } break;

                case MISSION_ADVICE: {
                    switch(mission) {
                        case 1: return _("@TEXTA.ENG|7|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Advice text for Fremen mission 1");   break;
                        case 2: return _("@TEXTA.ENG|11|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Advice text for Fremen mission 2");  break;
                        case 3: return _("@TEXTA.ENG|15|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Advice text for Fremen mission 3");  break;
                        case 4: return _("@TEXTA.ENG|19|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Advice text for Fremen mission 4");  break;
                        case 5: return _("@TEXTA.ENG|23|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Advice text for Fremen mission 5");  break;
                        case 6: return _("@TEXTA.ENG|27|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Advice text for Fremen mission 6");  break;
                        case 7: return _("@TEXTA.ENG|31|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Advice text for Fremen mission 7");  break;
                        case 8: return _("@TEXTA.ENG|35|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Advice text for Fremen mission 8");  break;
                        case 9: return _("@TEXTA.ENG|39|Atreides->Fremen|Ordos->Sardaukar|Harkonnen->Mercenary#Advice text for Fremen mission 9");  break;
                        default: return "";                                                    break;
                    }
                } break;

                default: {
                    return "";
                } break;
            }
        } break;

        case HOUSE_SARDAUKAR: {
            switch(texttype) {
                case MISSION_DESCRIPTION: {
                    switch(mission) {
                        case 0: return _("House Sardaukar\nOriginating from an forgotten planet, House Sardaukar has moved from planet to planet since then. Conquering foreign planets has specialized their combat skills which got them a reputation of being cruel.");  break;
                        case 1: return _("@TEXTH.ENG|4|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Description of Sardaukar mission 1");   break;
                        case 2: return _("@TEXTH.ENG|8|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Description of Sardaukar mission 2");   break;
                        case 3: return _("@TEXTH.ENG|12|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Description of Sardaukar mission 3");  break;
                        case 4: return _("@TEXTH.ENG|16|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Description of Sardaukar mission 4");  break;
                        case 5: return _("@TEXTH.ENG|20|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar|the Emperor was->the Atreides were#Description of Sardaukar mission 5");  break;
                        case 6: return _("@TEXTH.ENG|24|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Description of Sardaukar mission 6");  break;
                        case 7: return _("@TEXTH.ENG|28|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Description of Sardaukar mission 7");  break;
                        case 8: return _("@TEXTH.ENG|32|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Description of Sardaukar mission 8");  break;
                        case 9: return _("@TEXTH.ENG|36|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar|the Emperor's->their leader's#Description of Sardaukar mission 9");  break;
                        default: return "";                                                    break;
                    }
                } break;

                case MISSION_WIN: {
                    switch(mission) {
                        case 1: return _("@TEXTH.ENG|5|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Winning text of Sardaukar mission 1");   break;
                        case 2: return _("@TEXTH.ENG|9|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Winning text of Sardaukar mission 2");   break;
                        case 3: return _("@TEXTH.ENG|13|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Winning text of Sardaukar mission 3");  break;
                        case 4: return _("@TEXTH.ENG|17|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Winning text of Sardaukar mission 4");  break;
                        case 5: return _("@TEXTH.ENG|21|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Winning text of Sardaukar mission 5");  break;
                        case 6: return _("@TEXTH.ENG|25|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Winning text of Sardaukar mission 6");  break;
                        case 7: return _("@TEXTH.ENG|29|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Winning text of Sardaukar mission 7");  break;
                        case 8: return _("@TEXTH.ENG|33|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Winning text of Sardaukar mission 8");  break;
                        case 9: return _("@TEXTH.ENG|37|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar|the Emperor->the leader#Winning text of Sardaukar mission 9");  break;
                        default: return "";                                                     break;
                    }
                } break;

                case MISSION_LOSE: {
                    switch(mission) {
                        case 1: return _("@TEXTH.ENG|6|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Losing text of Sardaukar mission 1");   break;
                        case 2: return _("@TEXTH.ENG|10|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Losing text of Sardaukar mission 2");  break;
                        case 3: return _("@TEXTH.ENG|14|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Losing text of Sardaukar mission 3");  break;
                        case 4: return _("@TEXTH.ENG|18|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Losing text of Sardaukar mission 4");  break;
                        case 5: return _("@TEXTH.ENG|22|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Losing text of Sardaukar mission 5");  break;
                        case 6: return _("@TEXTH.ENG|26|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Losing text of Sardaukar mission 6");  break;
                        case 7: return _("@TEXTH.ENG|30|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Losing text of Sardaukar mission 7");  break;
                        case 8: return _("@TEXTH.ENG|34|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Losing text of Sardaukar mission 8");  break;
                        case 9: return _("@TEXTH.ENG|38|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Losing text of Sardaukar mission 9");  break;
                        default: return "";                                                    break;
                    }
                } break;

                case MISSION_ADVICE: {
                    switch(mission) {
                        case 1: return _("@TEXTH.ENG|7|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Advice text for Sardaukar mission 1");   break;
                        case 2: return _("@TEXTH.ENG|11|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Advice text for Sardaukar mission 2");  break;
                        case 3: return _("@TEXTH.ENG|15|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Advice text for Sardaukar mission 3");  break;
                        case 4: return _("@TEXTH.ENG|19|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Advice text for Sardaukar mission 4");  break;
                        case 5: return _("@TEXTH.ENG|23|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Advice text for Sardaukar mission 5");  break;
                        case 6: return _("@TEXTH.ENG|27|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Advice text for Sardaukar mission 6");  break;
                        case 7: return _("@TEXTH.ENG|31|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Advice text for Sardaukar mission 7");  break;
                        case 8: return _("@TEXTH.ENG|35|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Advice text for Sardaukar mission 8");  break;
                        case 9: return _("@TEXTH.ENG|39|Atreides->Mercenary|Ordos->Fremen|Harkonnen->Sardaukar#Advice text for Sardaukar mission 9");  break;
                        default: return "";                                                     break;
                    }
                } break;

                default: {
                    return "";
                } break;
            }
        } break;

        case HOUSE_NEUTRAL: {
            switch(texttype) {
                case MISSION_DESCRIPTION: {
                    switch(mission) {
                        case 0: return _("House Neutral\nThe Neutral forces operate between the great powers of Arrakis, holding ground through caution, trade, and opportunism."); break;
                        default: return getBriefingText(mission, texttype, HOUSE_MERCENARY); break;
                    }
                } break;

                default: {
                    return getBriefingText(mission, texttype, HOUSE_MERCENARY);
                } break;
            }
        } break;

        case HOUSE_REBELS: {
            switch(texttype) {
                case MISSION_DESCRIPTION: {
                    switch(mission) {
                        case 0: return _("House Rebels\nThe Rebels fight from hidden strongholds and stolen supply lines, striking the old powers wherever Arrakis gives them an opening."); break;
                        default: return getBriefingText(mission, texttype, HOUSE_FREMEN); break;
                    }
                } break;

                default: {
                    return getBriefingText(mission, texttype, HOUSE_FREMEN);
                } break;
            }
        } break;

        case HOUSE_MERCENARY:
        default: {
            switch(texttype) {
                case MISSION_DESCRIPTION: {
                    switch(mission) {
                        case 0: return _("House Mercenary\nThe home planet of the Mercenary consists of huge oceans and large deserts. House Mercenary is one of the oldest and most influencial intergalactical trading organization. To strengthen their power they don't hesitate to use military force.");       break;
                        case 1: return _("@TEXTO.ENG|4|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Description of Mercenary mission 1");   break;
                        case 2: return _("@TEXTO.ENG|8|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Description of Mercenary mission 2");   break;
                        case 3: return _("@TEXTO.ENG|12|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Description of Mercenary mission 3");  break;
                        case 4: return _("@TEXTO.ENG|16|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Description of Mercenary mission 4");  break;
                        case 5: return _("@TEXTO.ENG|20|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Description of Mercenary mission 5");  break;
                        case 6: return _("@TEXTO.ENG|24|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Description of Mercenary mission 6");  break;
                        case 7: return _("@TEXTO.ENG|28|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Description of Mercenary mission 7");  break;
                        case 8: return _("@TEXTO.ENG|32|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Description of Mercenary mission 8");  break;
                        case 9: return _("@TEXTO.ENG|36|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Description of Mercenary mission 9");  break;
                        default: return "";                                                break;
                    }
                } break;

                case MISSION_WIN: {
                    switch(mission) {
                        case 1: return _("@TEXTO.ENG|5|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Winning text of Mercenary mission 1");   break;
                        case 2: return _("@TEXTO.ENG|9|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Winning text of Mercenary mission 2");   break;
                        case 3: return _("@TEXTO.ENG|13|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Winning text of Mercenary mission 3");  break;
                        case 4: return _("@TEXTO.ENG|17|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Winning text of Mercenary mission 4");  break;
                        case 5: return _("@TEXTO.ENG|21|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Winning text of Mercenary mission 5");  break;
                        case 6: return _("@TEXTO.ENG|25|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Winning text of Mercenary mission 6");  break;
                        case 7: return _("@TEXTO.ENG|29|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Winning text of Mercenary mission 7");  break;
                        case 8: return _("@TEXTO.ENG|33|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Winning text of Mercenary mission 8");  break;
                        case 9: return _("@TEXTO.ENG|37|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Winning text of Mercenary mission 9");  break;
                        default: return "";                                                 break;
                    }
                } break;

                case MISSION_LOSE: {
                    switch(mission) {
                        case 1: return _("@TEXTO.ENG|6|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Losing text of Mercenary mission 1");   break;
                        case 2: return _("@TEXTO.ENG|10|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Losing text of Mercenary mission 2");  break;
                        case 3: return _("@TEXTO.ENG|14|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Losing text of Mercenary mission 3");  break;
                        case 4: return _("@TEXTO.ENG|18|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Losing text of Mercenary mission 4");  break;
                        case 5: return _("@TEXTO.ENG|22|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Losing text of Mercenary mission 5");  break;
                        case 6: return _("@TEXTO.ENG|26|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Losing text of Mercenary mission 6");  break;
                        case 7: return _("@TEXTO.ENG|30|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Losing text of Mercenary mission 7");  break;
                        case 8: return _("@TEXTO.ENG|34|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Losing text of Mercenary mission 8");  break;
                        case 9: return _("@TEXTO.ENG|38|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Losing text of Mercenary mission 9");  break;
                        default: return "";                                                break;
                    }
                } break;

                case MISSION_ADVICE: {
                    switch(mission) {
                        case 1: return _("@TEXTO.ENG|7|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Advice text for Mercenary mission 1");   break;
                        case 2: return _("@TEXTO.ENG|11|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Advice text for Mercenary mission 2");  break;
                        case 3: return _("@TEXTO.ENG|15|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Advice text for Mercenary mission 3");  break;
                        case 4: return _("@TEXTO.ENG|19|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Advice text for Mercenary mission 4");  break;
                        case 5: return _("@TEXTO.ENG|23|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Advice text for Mercenary mission 5");  break;
                        case 6: return _("@TEXTO.ENG|27|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Advice text for Mercenary mission 6");  break;
                        case 7: return _("@TEXTO.ENG|31|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Advice text for Mercenary mission 7");  break;
                        case 8: return _("@TEXTO.ENG|35|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Advice text for Mercenary mission 8");  break;
                        case 9: return _("@TEXTO.ENG|39|Atreides->Sardaukar|Ordos->Mercenary|Harkonnen->Fremen#Advice text for Mercenary mission 9");  break;
                        default: return "";                                                 break;
                    }
                } break;

                default: {
                    return "";
                } break;
            }

        } break;

    }
}

std::vector<MentatTextFile::MentatEntry> TextManager::getAllMentatEntries(int house, unsigned int techLevel) const {
    std::vector<MentatTextFile::MentatEntry> mentatEntries;

    const int identity = ModManager::instance().isInitialized()
        ? ModManager::instance().getEffectiveMentatIdentity(house) : house;
    switch(identity) {
        case HOUSE_HARKONNEN:
        case HOUSE_SARDAUKAR:
        default: {
            for(unsigned int i = 0; i <  mentatStrings[HOUSE_HARKONNEN]->getNumEntries(); i++) {
                if(mentatStrings[HOUSE_HARKONNEN]->getMentatEntry(i).techLevel <= techLevel) {
                    mentatEntries.push_back(mentatStrings[HOUSE_HARKONNEN]->getMentatEntry(i));
                }
            }
        } break;

        case HOUSE_ATREIDES:
        case HOUSE_FREMEN: {
            for(unsigned int i = 0; i <  mentatStrings[HOUSE_ATREIDES]->getNumEntries(); i++) {
                if(mentatStrings[HOUSE_ATREIDES]->getMentatEntry(i).techLevel <= techLevel) {
                    mentatEntries.push_back(mentatStrings[HOUSE_ATREIDES]->getMentatEntry(i));
                }
            }
        } break;

        case HOUSE_ORDOS:
        case HOUSE_MERCENARY: {
            for(unsigned int i = 0; i <  mentatStrings[HOUSE_ORDOS]->getNumEntries(); i++) {
                if(mentatStrings[HOUSE_ORDOS]->getMentatEntry(i).techLevel <= techLevel) {
                    mentatEntries.push_back(mentatStrings[HOUSE_ORDOS]->getMentatEntry(i));
                }
            }
        } break;
    }

    return mentatEntries;
}


const std::string& TextManager::postProcessString(const std::string& unprocessedString) const {
    size_t commentStart = unprocessedString.find_first_of('#');
    if(commentStart == std::string::npos) {
        commentStart = unprocessedString.size();
    }


    std::string commandString = unprocessedString.substr(1, commentStart-1);

    std::vector<std::string> commands = splitStringToStringVector(commandString, "\\|");

    int index = -1;
    if(commands.size() < 2 || !parseString(commands[1], index)) {
        return unprocessedString;
    } else {
        auto iter = origDuneText.find(commands[0]);
        if(iter != origDuneText.end()) {
            IndexedTextFile* pIndexedTextFile = iter->second.get();

            if(commands[0].compare(0,5,"DUNE.") && (index >= 281) && pIndexedTextFile->getNumStrings() == 335) {
                // Dune II 1.0 has 2 titles less
                index = std::max((int) 281, index - 2);
            }


            std::map<std::string, std::string> mapping;
            for(unsigned int i=2;i<commands.size();i++) {
                std::vector<std::string> parts = splitStringToStringVector(commands[i], "->");
                mapping[parts[0]] = parts[1];
            }

            localizedString[unprocessedString] = replaceAll(pIndexedTextFile->getString(index), mapping);

            return localizedString[unprocessedString];
        } else {
            return unprocessedString;
        }

        return unprocessedString;
    }
}

void TextManager::addOrigDuneText(const std::string& filename, bool bDecode) {
    origDuneText[filename] = std::make_unique<IndexedTextFile>(pFileManager->openFile(filename).get(), bDecode);
}
