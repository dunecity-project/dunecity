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

#ifndef RADARINTERFACE_H
#define RADARINTERFACE_H

#include "DefaultStructureInterface.h"
#include "CityStatsBox.h"

#include <FileClasses/FontManager.h>
#include <FileClasses/TextManager.h>

#include <House.h>
#include <Command.h>
#include <mod/ModManager.h>
#include <GUI/ResearchButton.h>
#include <array>

#include <GUI/Label.h>
#include <GUI/VBox.h>

#include <misc/string_util.h>

class RadarInterface : public DefaultStructureInterface {
public:
    static RadarInterface* create(int objectID) {
        RadarInterface* tmp = new RadarInterface(objectID);
        tmp->pAllocated = true;
        return tmp;
    }

protected:
    explicit RadarInterface(int objectID) : DefaultStructureInterface(objectID) {
        Uint32 color = ownerAccentColor();

        mainHBox.addWidget(&textVBox);

        friendlyUnitsLabel.setTextFontSize(12);
        friendlyUnitsLabel.setTextColor(color);
        textVBox.addWidget(&friendlyUnitsLabel, (Sint32)18);
        enemyUnitsLabel.setTextFontSize(12);
        enemyUnitsLabel.setTextColor(color);
        textVBox.addWidget(&enemyUnitsLabel, (Sint32)18);

        cityStats_.attachTo(textVBox, color);

        for(int kind = 0; kind < radar::Count; ++kind) {
            auto& button = researchButtons[kind];
            button.setVisible(false);
            button.setTextColor(color);
            button.setPrice(radar::cost(kind));
            button.setOnClick([objectID, kind] {
                const auto* building = currentGame->getObjectManager().getObject(objectID);
                if(building && pLocalPlayer && currentGame->canControlHouse(building->getOwner()))
                    currentGame->getCommandManager().addCommand(
                        Command(pLocalPlayer->getPlayerID(), CMD_RADAR_RESEARCH, objectID, kind));
            });
            textVBox.addWidget(&button, 38);
        }

        textVBox.addWidget(Spacer::create(), 0.99);
    }

    /**
        This method updates the object interface.
        If the object doesn't exists anymore then update returns false.
        \return true = everything ok, false = the object container should be removed
    */
    bool update() override
    {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        if(pObject == nullptr) {
            return false;
        }

        House* pOwner = pObject->getOwner();

        friendlyUnitsLabel.setText(" " + _("Friend") + ": " + std::to_string(pOwner->getNumVisibleFriendlyUnits()));
        enemyUnitsLabel.setText(" " + _("Enemy") + ": " + std::to_string(pOwner->getNumVisibleEnemyUnits()));

        cityStats_.update(dynamic_cast<StructureBase*>(pObject));

        const bool family = ModManager::instance().isTornieFamilyContentActive();
        if(family && displayedFaction != pOwner->getFactionID()) {
            displayedFaction = pOwner->getFactionID();
            for(int kind = 0; kind < radar::Count; ++kind) {
                researchButtons[kind].setText(kind == radar::BaseExpansion ? _("Base +2")
                    : kind == radar::ExtendedVision ? _("Vision +2") : _(specialName(pOwner)));
                std::string description = kind == radar::BaseExpansion ? _("Build range: +2 tiles around the base.")
                    : kind == radar::ExtendedVision ? _("Sight: +2 tiles, excluding aircraft.") : _(specialDescription(pOwner));
                description += " " + std::to_string(radar::cost(kind)) + _(" credits, ")
                    + std::to_string(radar::seconds(kind)) + _(" s, tech ")
                    + std::to_string(radar::level(kind)) + (kind == radar::Specialization ? " + IX." : ".");
                researchButtons[kind].setTooltipText(description);
            }
        }
        for(int kind = 0; kind < radar::Count; ++kind) {
            auto& button = researchButtons[kind];
            button.setVisible(family && currentGame->techLevel >= radar::level(kind));
            button.setEnabled(currentGame->canControlHouse(pOwner) && pOwner->canStartRadarResearch(kind));
            const int progress = pOwner->hasRadarResearch(kind) ? 100
                : pOwner->getRadarResearchActive() == kind
                    ? 100 - 100 * pOwner->getRadarResearchRemaining() / MILLI2CYCLES(radar::seconds(kind) * 1000) : -1;
            if(progress != displayedProgress[kind]) {
                displayedProgress[kind] = progress;
                button.setStatus(progress == 100 ? _("OK") : progress >= 0 ? std::to_string(progress) + " %" : "");
            }
        }

        return DefaultStructureInterface::update();
    }

private:
    static const char* specialName(const House* house) {
        switch(getHouseFactionIdentity(static_cast<HOUSETYPE>(house->getFactionID()))) {
            case HOUSE_HARKONNEN: return "Damage";
            case HOUSE_ATREIDES: case HOUSE_FREMEN: return "Fast Troopers";
            case HOUSE_ORDOS: return "Fast vehicles";
            case HOUSE_SARDAUKAR: return "Tank crews";
            case HOUSE_MERCENARY: return "Lower prices";
            case HOUSE_NEUTRAL: return "Repairs";
            case HOUSE_REBELS: return "Range +1";
            case HOUSE_WILDSPADE: return "Aircraft";
            case HOUSE_KLESHMERSH: return "Fire +2 s";
            case HOUSE_CUSTOM: return "Armour";
            case HOUSE_THARPIQUE: return "Scouting";
            default: return "Specialization";
        }
    }

    static const char* specialDescription(const House* house) {
        switch(getHouseFactionIdentity(static_cast<HOUSETYPE>(house->getFactionID()))) {
            case HOUSE_HARKONNEN: return "Combat Tank, Devastator explosion and Palace missile: +20% damage.";
            case HOUSE_ATREIDES: case HOUSE_FREMEN: return "Troopers and squads: +15% movement speed.";
            case HOUSE_ORDOS: return "Light vehicles: +15% movement speed.";
            case HOUSE_SARDAUKAR: return "Destroyed tanks: 35% chance to rescue one Trooper, replacing the ordinary Soldier roll.";
            case HOUSE_MERCENARY: return "Infantry, light vehicles and aircraft: -10% purchase price.";
            case HOUSE_NEUTRAL: return "Building and Repair Yard repairs: +25% speed, unchanged credits per health point.";
            case HOUSE_REBELS: return "Armed ground vehicles: +1 tile weapon range.";
            case HOUSE_WILDSPADE: return "Ornithopters and Carryalls, including variants: +25% health.";
            case HOUSE_KLESHMERSH: return "Persistent flames: +2 seconds with unchanged damage rate.";
            case HOUSE_CUSTOM: return "Conventional tanks and variants: -20% incoming damage, -10% movement speed.";
            case HOUSE_THARPIQUE: return "Scouts and artillery: +2 sight, stacking with the common research.";
            default: return "";
        }
    }

    std::array<ResearchButton, radar::Count> researchButtons;
    std::array<int, radar::Count> displayedProgress{{-2, -2, -2}};
    int displayedFaction = HOUSE_INVALID;
    VBox    textVBox;

    Label   friendlyUnitsLabel;
    Label   enemyUnitsLabel;

    CityStatsBox cityStats_;
};

#endif // RADARINTERFACE_H
