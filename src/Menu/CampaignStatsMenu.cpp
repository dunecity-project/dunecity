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

#include <Menu/CampaignStatsMenu.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/music/MusicPlayer.h>
#include <misc/format.h>
#include <misc/draw_util.h>
#include <mod/ModManager.h>
#include <House.h>
#include <SoundPlayer.h>
#include <Game.h>
#include <structures/StructureBase.h>
#include <units/UnitBase.h>
#include <units/Harvester.h>

#include <climits>
#include <cmath>
#include <algorithm>

#define max3(a,b,c) (std::max((a),std::max((b),(c))))

#define PROGRESSBARTIME 4000.0f
#define WAITTIME 1000

namespace {

/// The tax row belongs to Dune City alone. Other mods with a city simulation
/// (Tornie) and the plain Dune II rule sets keep the original three groups, so
/// the city-sim flag has to be paired with the mod's content identity.
bool isDuneCityTaxStatisticEnabled() {
    const auto& modManager = ModManager::instance();
    if(!modManager.isInitialized() || currentGame == nullptr || !currentGame->isCitySimEnabled()) {
        return false;
    }
    return modManager.getContentBase(currentGame->getGameInitSettings().getModName()) == "dunecity";
}

} // anonymous namespace

CampaignStatsMenu::CampaignStatsMenu(int level) : MenuBase()
{
    calculateScore(level);

    showTaxStatistics = isDuneCityTaxStatisticEnabled();

    const int localHouseID = pLocalHouse->getHouseID();
    Uint32 colorYou = getHouseColorRGB(getHouseVisualHouse(localHouseID), 1);
    Uint32 colorEnemy = localHouseID == HOUSE_SARDAUKAR
        ? COLOR_RGB(255, 32, 192)
        : SDL2RGB(palette[PALCOLOR_SARDAUKAR + 1]);

    // set up window
    SDL_Texture *pBackground = pGFXManager->getUIGraphic(UI_GameStatsBackground);
    setBackground(pBackground);
    resize(getTextureSize(pBackground));
    if(showTaxStatistics) {
        // Reuse the original framed row artwork for all four groups instead
        // of drawing a fourth set of labels over the three-row background.
        auto source = pGFXManager->getUIGraphicSurface(UI_GameStatsBackground);
        sdl2::surface_ptr background(SDL_ConvertSurfaceFormat(source, SCREEN_FORMAT, 0));
        auto row = getSubPicture(background.get(), getSize().x / 2 - 304, getSize().y / 2 - 40, 610, 74);
        for(int group = 0; group < 4; ++group) {
            SDL_Rect dest{getSize().x / 2 - 304, getSize().y / 2 - 40 + group * 56, 610, 56};
            SDL_BlitScaled(row.get(), nullptr, background.get(), &dest);
        }
        setBackground(std::move(background));
    }

    setWindowWidget(&windowWidget);

    scoreLabel.setTextColor(COLOR_WHITE, COLOR_BLACK);
    scoreLabel.setText(fmt::sprintf(_("@DUNE.ENG|21#Score: %d"), totalScore));
    windowWidget.addWidget(&scoreLabel, (getSize()/2) + Point(-175, -172), scoreLabel.getSize());

    timeLabel.setTextColor(COLOR_WHITE, COLOR_BLACK);
    timeLabel.setText(fmt::sprintf(_("@DUNE.ENG|22#Time: %d:%02d"), totalTime/3600, (totalTime%3600)/60));
    windowWidget.addWidget(&timeLabel, (getSize()/2) + Point(+180 - timeLabel.getSize().x, -172), timeLabel.getSize());

    yourRankLabel.setAlignment(Alignment_HCenter);
    yourRankLabel.setTextColor(COLOR_WHITE, COLOR_BLACK);
    yourRankLabel.setText(_("@DUNE.ENG|23#You have attained the rank"));
    windowWidget.addWidget(&yourRankLabel, (getSize()/2) + Point(-yourRankLabel.getSize().x/2, -126), yourRankLabel.getSize());

    rankLabel.setAlignment(Alignment_HCenter);
    rankLabel.setText(rank);
    windowWidget.addWidget(&rankLabel, (getSize()/2) + Point(-rankLabel.getSize().x/2, -104), rankLabel.getSize());

    // The classic panel has three 74 pixel statistic rows. Dune City adds a
    // fourth (tax) row, so only that mod compresses the block enough to keep
    // all four rows inside the same frame; every other mod keeps the original
    // row positions exactly.
    const int groupPitch = showTaxStatistics ? 56 : 74;
    const int firstGroupY = -40;

    auto addStatGroup = [&](int index, Label& headingLabel, const std::string& heading,
                            Label& youLabel, ProgressBar& youShadowBar, ProgressBar& youBar, Label& youValueLabel,
                            Label& enemyLabel, ProgressBar& enemyShadowBar, ProgressBar& enemyBar, Label& enemyValueLabel) {
        const int y = firstGroupY + index * groupPitch;
        const auto offset = [&](int value) { return showTaxStatistics ? value * 56 / 74 : value; };
        if(showTaxStatistics) {
            youLabel.setTextFontSize(10); enemyLabel.setTextFontSize(10);
            youValueLabel.setTextFontSize(10); enemyValueLabel.setTextFontSize(10);
        }

        headingLabel.setTextColor(COLOR_WHITE, COLOR_BLACK, COLOR_THICKSPICE);
        headingLabel.setAlignment(Alignment_HCenter);
        headingLabel.setText(heading);
        windowWidget.addWidget(&headingLabel, (getSize()/2) + Point(-headingLabel.getSize().x/2, y), headingLabel.getSize());

        youLabel.setTextColor(COLOR_WHITE, COLOR_BLACK);
        youLabel.setAlignment(Alignment_Right);
        youLabel.setText(_("@DUNE.ENG|329#You:"));
        windowWidget.addWidget(&youLabel, (getSize()/2) + Point(-229 - youLabel.getSize().x, y + offset(19)), youLabel.getSize());

        youShadowBar.setColor(COLOR_BLACK);
        youShadowBar.setProgress(0.0);
        windowWidget.addWidget(&youShadowBar, (getSize()/2) + Point(-228 + 2, y + offset(25) + 2), Point(440, offset(12)));

        youBar.setColor(colorYou);
        youBar.setProgress(0.0);
        windowWidget.addWidget(&youBar, (getSize()/2) + Point(-228, y + offset(25)), Point(440, offset(12)));

        youValueLabel.setTextColor(COLOR_WHITE, COLOR_BLACK);
        youValueLabel.setAlignment(Alignment_HCenter);
        youValueLabel.setVisible(false);
        windowWidget.addWidget(&youValueLabel, (getSize()/2) + Point(222, y + offset(20)), Point(66, offset(21)));

        enemyLabel.setTextColor(COLOR_WHITE, COLOR_BLACK);
        enemyLabel.setAlignment(Alignment_Right);
        enemyLabel.setText(_("@DUNE.ENG|330#Enemy:"));
        windowWidget.addWidget(&enemyLabel, (getSize()/2) + Point(-229 - enemyLabel.getSize().x, y + offset(37)), enemyLabel.getSize());

        enemyShadowBar.setColor(COLOR_BLACK);
        enemyShadowBar.setProgress(0.0);
        windowWidget.addWidget(&enemyShadowBar, (getSize()/2) + Point(-228 + 2, y + offset(43) + 2), Point(440, offset(12)));

        enemyBar.setColor(colorEnemy);
        enemyBar.setProgress(0.0);
        windowWidget.addWidget(&enemyBar, (getSize()/2) + Point(-228, y + offset(43)), Point(440, offset(12)));

        enemyValueLabel.setTextColor(COLOR_WHITE, COLOR_BLACK);
        enemyValueLabel.setAlignment(Alignment_HCenter);
        enemyValueLabel.setVisible(false);
        windowWidget.addWidget(&enemyValueLabel, (getSize()/2) + Point(222, y + offset(38)), Point(66, offset(21)));
    };

    // spice statistics
    addStatGroup(0, spiceHarvestedByLabel, _("@DUNE.ENG|26#Spice harvested by"),
                 you1Label, spiceYouShadowProgressBar, spiceYouProgressBar, spiceYouLabel,
                 enemy1Label, spiceEnemyShadowProgressBar, spiceEnemyProgressBar, spiceEnemyLabel);

    // unit kill statistics
    addStatGroup(1, unitsDestroyedByLabel, _("@DUNE.ENG|24#Units destroyed by"),
                 you2Label, unitsYouShadowProgressBar, unitsYouProgressBar, unitsYouLabel,
                 enemy2Label, unitsEnemyShadowProgressBar, unitsEnemyProgressBar, unitsEnemyLabel);

    // buildings kill statistics
    addStatGroup(2, buildingsDestroyedByLabel, _("@DUNE.ENG|25#Buildings destroyed by"),
                 you3Label, buildingsYouShadowProgressBar, buildingsYouProgressBar, buildingsYouLabel,
                 enemy3Label, buildingsEnemyShadowProgressBar, buildingsEnemyProgressBar, buildingsEnemyLabel);

    // city tax statistics (Dune City only)
    if(showTaxStatistics) {
        addStatGroup(3, taxCollectedByLabel, _("Tax collected by"),
                     you4Label, taxYouShadowProgressBar, taxYouProgressBar, taxYouLabel,
                     enemy4Label, taxEnemyShadowProgressBar, taxEnemyProgressBar, taxEnemyLabel);
    }
}

CampaignStatsMenu::~CampaignStatsMenu() = default;

int CampaignStatsMenu::showMenu()
{
    musicPlayer->changeMusic(MUSIC_GAMESTAT);

    currentStateStartTime = SDL_GetTicks();
    currentState = State_HumanSpice;

    return MenuBase::showMenu();
}

bool CampaignStatsMenu::doInput(SDL_Event &event)
{
    if(event.type == SDL_MOUSEBUTTONUP) {
        if(currentState == State_Finished) {
            quit();
        } else {
            while(currentState != State_Finished) {
                doState(INT_MAX);
            }
        }
    }

    return MenuBase::doInput(event);
}

void CampaignStatsMenu::drawSpecificStuff()
{
    doState(SDL_GetTicks() - currentStateStartTime);
}
void CampaignStatsMenu::doState(int elapsedTime)
{
    switch(currentState) {
        case State_HumanSpice:
        {
            float MaxSpiceHarvested = max3(spiceHarvestedByHuman, spiceHarvestedByAI, 3000.0f);
            float SpiceComplete = std::min(elapsedTime / PROGRESSBARTIME, 1.0f);

            float Human_PercentSpiceComplete;
            if(SpiceComplete < spiceHarvestedByHuman / MaxSpiceHarvested) {
                Human_PercentSpiceComplete = SpiceComplete * 100.0f;
                spiceYouLabel.setText( std::to_string( (int) (SpiceComplete*MaxSpiceHarvested)));
                soundPlayer->playSound(Sound_CreditsTick);
            } else {
                Human_PercentSpiceComplete = spiceHarvestedByHuman * 100.0f / MaxSpiceHarvested;
                spiceYouLabel.setText(std::to_string( (int) spiceHarvestedByHuman));
                soundPlayer->playSound(Sound_Tick);
                currentState = State_Between_HumanSpice_and_AISpice;
                currentStateStartTime = SDL_GetTicks();
            }

            spiceYouLabel.setVisible(true);
            spiceYouProgressBar.setProgress( Human_PercentSpiceComplete );
            spiceYouShadowProgressBar.setProgress( Human_PercentSpiceComplete );
        } break;

        case State_Between_HumanSpice_and_AISpice:
        {
            if(elapsedTime > WAITTIME) {
                currentState = State_AISpice;
                currentStateStartTime = SDL_GetTicks();
            }
        }
        break;

        case State_AISpice:
        {
            float MaxSpiceHarvested = max3(spiceHarvestedByHuman, spiceHarvestedByAI, 3000.0f);
            float SpiceComplete = std::min(elapsedTime / PROGRESSBARTIME, 1.0f);

            float AI_PercentSpiceComplete;
            if(SpiceComplete < spiceHarvestedByAI / MaxSpiceHarvested) {
                AI_PercentSpiceComplete = SpiceComplete * 100.0f;
                spiceEnemyLabel.setText( std::to_string( (int) (SpiceComplete*MaxSpiceHarvested)));
                soundPlayer->playSound(Sound_CreditsTick);
            } else {
                AI_PercentSpiceComplete = spiceHarvestedByAI * 100.0f / MaxSpiceHarvested;
                spiceEnemyLabel.setText(std::to_string( (int) spiceHarvestedByAI));
                soundPlayer->playSound(Sound_Tick);
                currentState = State_Between_AISpice_and_HumanUnits;
                currentStateStartTime = SDL_GetTicks();
            }

            spiceEnemyLabel.setVisible(true);
            spiceEnemyProgressBar.setProgress( AI_PercentSpiceComplete );
            spiceEnemyShadowProgressBar.setProgress( AI_PercentSpiceComplete );
        } break;

        case State_Between_AISpice_and_HumanUnits:
            if(elapsedTime > WAITTIME) {
                currentState = State_HumanUnits;
                currentStateStartTime = SDL_GetTicks();
            }
        break;

        case State_HumanUnits:
        {
            float MaxUnitsDestroyed = (float) max3(unitsDestroyedByHuman, unitsDestroyedByAI, 200);
            float UnitsComplete = std::min(elapsedTime / PROGRESSBARTIME, 1.0f);

            float Human_PercentUnitsComplete;
            if(UnitsComplete < unitsDestroyedByHuman / MaxUnitsDestroyed) {
                Human_PercentUnitsComplete = UnitsComplete * 100.0f;
                unitsYouLabel.setText( std::to_string( (int) (UnitsComplete*MaxUnitsDestroyed)));
                soundPlayer->playSound(Sound_CreditsTick);
            } else {
                Human_PercentUnitsComplete = unitsDestroyedByHuman * 100.0f / MaxUnitsDestroyed;
                unitsYouLabel.setText( std::to_string(unitsDestroyedByHuman));
                soundPlayer->playSound(Sound_Tick);
                currentState = State_Between_HumanUnits_and_AIUnits;
                currentStateStartTime = SDL_GetTicks();
            }

            unitsYouLabel.setVisible(true);
            unitsYouProgressBar.setProgress( Human_PercentUnitsComplete );
            unitsYouShadowProgressBar.setProgress( Human_PercentUnitsComplete );
        } break;

        case State_Between_HumanUnits_and_AIUnits:
            if(elapsedTime > WAITTIME) {
                currentState = State_AIUnits;
                currentStateStartTime = SDL_GetTicks();
            }
        break;

        case State_AIUnits:
        {
            float MaxUnitsDestroyed = (float) max3(unitsDestroyedByHuman, unitsDestroyedByAI, 200);
            float UnitsComplete = std::min(elapsedTime / PROGRESSBARTIME, 1.0f);

            float AI_PercentUnitsComplete;
            if(UnitsComplete < unitsDestroyedByAI / MaxUnitsDestroyed) {
                AI_PercentUnitsComplete = UnitsComplete * 100.0f;
                unitsEnemyLabel.setText( std::to_string( (int) (UnitsComplete*MaxUnitsDestroyed)));
                soundPlayer->playSound(Sound_CreditsTick);
            } else {
                AI_PercentUnitsComplete = unitsDestroyedByAI * 100.0f / MaxUnitsDestroyed;
                unitsEnemyLabel.setText( std::to_string(unitsDestroyedByAI));
                soundPlayer->playSound(Sound_Tick);
                currentState = State_Between_AIUnits_and_HumanBuildings;
                currentStateStartTime = SDL_GetTicks();
            }

            unitsEnemyLabel.setVisible(true);
            unitsEnemyProgressBar.setProgress( AI_PercentUnitsComplete );
            unitsEnemyShadowProgressBar.setProgress( AI_PercentUnitsComplete );
        } break;

        case State_Between_AIUnits_and_HumanBuildings:
            if(elapsedTime > WAITTIME) {
                currentState = State_HumanBuildings;
                currentStateStartTime = SDL_GetTicks();
            }
        break;

        case State_HumanBuildings:
        {
            float MaxBuildingsDestroyed = (float) max3(structuresDestroyedByHuman, structuresDestroyedByAI, 200);
            float BuildingsComplete = std::min(elapsedTime / PROGRESSBARTIME, 1.0f);

            float Human_PercentBuildingsComplete;
            if(BuildingsComplete < structuresDestroyedByHuman / MaxBuildingsDestroyed) {
                Human_PercentBuildingsComplete = BuildingsComplete * 100.0f;
                buildingsYouLabel.setText( std::to_string( (int) (BuildingsComplete*MaxBuildingsDestroyed)));
                soundPlayer->playSound(Sound_CreditsTick);
            } else {
                Human_PercentBuildingsComplete = structuresDestroyedByHuman * 100.0f / MaxBuildingsDestroyed;
                buildingsYouLabel.setText( std::to_string(structuresDestroyedByHuman));
                soundPlayer->playSound(Sound_Tick);
                currentState = State_Between_HumanBuildings_and_AIBuildings;
                currentStateStartTime = SDL_GetTicks();
            }

            buildingsYouLabel.setVisible(true);
            buildingsYouProgressBar.setProgress( Human_PercentBuildingsComplete );
            buildingsYouShadowProgressBar.setProgress( Human_PercentBuildingsComplete );
        } break;

        case State_Between_HumanBuildings_and_AIBuildings:
            if(elapsedTime > WAITTIME) {
                currentState = State_AIBuildings;
                currentStateStartTime = SDL_GetTicks();
            }
        break;

        case State_AIBuildings:
        {
            float MaxBuildingsDestroyed = (float) max3(structuresDestroyedByHuman, structuresDestroyedByAI, 200);
            float BuildingsComplete = std::min(elapsedTime / PROGRESSBARTIME, 1.0f);

            float AI_PercentBuildingsComplete;
            if(BuildingsComplete < structuresDestroyedByAI / MaxBuildingsDestroyed) {
                AI_PercentBuildingsComplete = BuildingsComplete * 100.0f;
                buildingsEnemyLabel.setText( std::to_string( (int) (BuildingsComplete*MaxBuildingsDestroyed)));
                soundPlayer->playSound(Sound_CreditsTick);
            } else {
                AI_PercentBuildingsComplete = structuresDestroyedByAI * 100.0f / MaxBuildingsDestroyed;
                buildingsEnemyLabel.setText( std::to_string(structuresDestroyedByAI));
                soundPlayer->playSound(Sound_Tick);
                currentState = showTaxStatistics ? State_Between_AIBuildings_and_HumanTax : State_Finished;
                currentStateStartTime = SDL_GetTicks();
            }

            buildingsEnemyLabel.setVisible(true);
            buildingsEnemyProgressBar.setProgress( AI_PercentBuildingsComplete );
            buildingsEnemyShadowProgressBar.setProgress( AI_PercentBuildingsComplete );
        } break;

        case State_Between_AIBuildings_and_HumanTax:
            if(elapsedTime > WAITTIME) {
                currentState = State_HumanTax;
                currentStateStartTime = SDL_GetTicks();
            }
        break;

        case State_HumanTax:
        {
            // Fixed-point house totals are converted only for this display.
            const double taxHuman = taxCollectedByHuman;
            const double MaxTaxCollected = max3(taxHuman, taxCollectedByAI, 1000.0);
            const float TaxComplete = std::min(elapsedTime / PROGRESSBARTIME, 1.0f);

            float Human_PercentTaxComplete;
            if(TaxComplete < taxHuman / MaxTaxCollected) {
                Human_PercentTaxComplete = TaxComplete * 100.0f;
                taxYouLabel.setText( std::to_string( (long long) (TaxComplete*MaxTaxCollected)));
                soundPlayer->playSound(Sound_CreditsTick);
            } else {
                Human_PercentTaxComplete = taxHuman * 100.0f / MaxTaxCollected;
                taxYouLabel.setText( std::to_string( std::llround(taxCollectedByHuman)));
                soundPlayer->playSound(Sound_Tick);
                currentState = State_Between_HumanTax_and_AITax;
                currentStateStartTime = SDL_GetTicks();
            }

            taxYouLabel.setVisible(true);
            taxYouProgressBar.setProgress( Human_PercentTaxComplete );
            taxYouShadowProgressBar.setProgress( Human_PercentTaxComplete );
        } break;

        case State_Between_HumanTax_and_AITax:
            if(elapsedTime > WAITTIME) {
                currentState = State_AITax;
                currentStateStartTime = SDL_GetTicks();
            }
        break;

        case State_AITax:
        {
            const double taxAI = taxCollectedByAI;
            const double MaxTaxCollected = max3(taxCollectedByHuman, taxAI, 1000.0);
            const float TaxComplete = std::min(elapsedTime / PROGRESSBARTIME, 1.0f);

            float AI_PercentTaxComplete;
            if(TaxComplete < taxAI / MaxTaxCollected) {
                AI_PercentTaxComplete = TaxComplete * 100.0f;
                taxEnemyLabel.setText( std::to_string( (long long) (TaxComplete*MaxTaxCollected)));
                soundPlayer->playSound(Sound_CreditsTick);
            } else {
                AI_PercentTaxComplete = taxAI * 100.0f / MaxTaxCollected;
                taxEnemyLabel.setText( std::to_string( std::llround(taxCollectedByAI)));
                soundPlayer->playSound(Sound_Tick);
                currentState = State_Finished;
                currentStateStartTime = SDL_GetTicks();
            }

            taxEnemyLabel.setVisible(true);
            taxEnemyProgressBar.setProgress( AI_PercentTaxComplete );
            taxEnemyShadowProgressBar.setProgress( AI_PercentTaxComplete );
        } break;

        case State_Finished:
        default:
        {
            // nothing
        } break;
    }
}

void CampaignStatsMenu::calculateScore(int level)
{
    unitsDestroyedByHuman = 0;
    unitsDestroyedByAI = 0;

    structuresDestroyedByHuman = 0;
    structuresDestroyedByAI = 0;

    spiceHarvestedByHuman = 0.0f;
    spiceHarvestedByAI = 0.0f;

    taxCollectedByHuman = 0;
    taxCollectedByAI = 0;

    totalTime = currentGame->getGameTime()/1000;

    totalScore = level*45;

    float totalHumanCredits = 0.0f;
    // A campaign house can have both a human and an AI partner. Controller
    // type does not identify the enemy; use the same teams as campaign victory.
    const int localTeam = pLocalHouse->getTeamID();
    for(int i=0; i < NUM_HOUSES; i++) {
        House* pHouse = currentGame->getHouse(i);
        // The Natives (worms, crime troopers) are neither side: their kills and losses are no
        // house's statistics.
        if(pHouse != nullptr && !pHouse->isNatives()) {
            if(pHouse->getTeamID() != localTeam) {
                unitsDestroyedByAI += pHouse->getNumDestroyedUnits();
                structuresDestroyedByAI += pHouse->getNumDestroyedStructures();
                spiceHarvestedByAI += pHouse->getHarvestedSpice().toFloat();
                // Tax is a separate statistic: it is neither spice nor score.
                taxCollectedByAI += pHouse->getCityTaxReceipts().toDouble();

                totalScore -= pHouse->getDestroyedValue();
            } else {
                unitsDestroyedByHuman += pHouse->getNumDestroyedUnits();
                structuresDestroyedByHuman += pHouse->getNumDestroyedStructures();
                spiceHarvestedByHuman += pHouse->getHarvestedSpice().toFloat();
                taxCollectedByHuman += pHouse->getCityTaxReceipts().toDouble();

                totalHumanCredits += pHouse->getCredits();

                totalScore += pHouse->getDestroyedValue();
            }
        }
    }

    totalScore += ((int) totalHumanCredits) / 100;

    for(const StructureBase* pStructure : structureList) {
        if(pStructure->getOwner()->getTeamID() == localTeam) {
            totalScore += currentGame->objectData.data[pStructure->getItemID()][pStructure->getOriginalHouseID()].price / 100;
        }
    }

    totalScore -= ((totalTime/60) + 1);

    for(const UnitBase* pUnit : unitList) {
        if(pUnit->getItemID() == Unit_Harvester) {
            const Harvester* pHarvester = static_cast<const Harvester*>(pUnit);
            if(pHarvester->getOwner()->getTeamID() != localTeam) {
                spiceHarvestedByAI += pHarvester->getAmountOfSpice().toFloat();
            } else {
                spiceHarvestedByHuman += pHarvester->getAmountOfSpice().toFloat();
            }
        }
    }

    if(currentGame->areCheatsEnabled() == true) {
        rank = "Cheater";
    } else {

        if(totalScore >= 1400)       rank = _("@DUNE.ENG|282#Emperor");
        else if(totalScore >= 1000)  rank = _("@DUNE.ENG|281#Ruler of Arrakis");
        else if(totalScore >= 700)   rank = _("@DUNE.ENG|280#Chief Warlord");
        else if(totalScore >= 500)   rank = _("@DUNE.ENG|279#Warlord");
        else if(totalScore >= 400)   rank = _("@DUNE.ENG|278#Base Commander");
        else if(totalScore >= 300)   rank = _("@DUNE.ENG|277#Outpost Commander");
        else if(totalScore >= 200)   rank = _("@DUNE.ENG|276#Squad Leader");
        else if(totalScore >= 150)   rank = _("@DUNE.ENG|275#Dune Trooper");
        else if(totalScore >= 100)   rank = _("@DUNE.ENG|274#Sand Warrior");
        else if(totalScore >= 50)    rank = _("@DUNE.ENG|273#Desert Mongoose");
        else if(totalScore >= 25)    rank = _("@DUNE.ENG|272#Sand Snake");
        else                         rank = _("@DUNE.ENG|271#Sand Flea");
    }
}
