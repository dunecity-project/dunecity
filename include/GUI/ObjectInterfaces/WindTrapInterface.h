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

#ifndef WINDTRAPINTERFACE_H
#define WINDTRAPINTERFACE_H

#include <structures/NuclearPlant.h>
#include <structures/WindTrap.h>
#include "DefaultStructureInterface.h"
#include "CityStatsBox.h"

#include <FileClasses/FontManager.h>
#include <FileClasses/TextManager.h>

#include <House.h>
#include <structures/Scoutpost.h>
#include <mod/ModManager.h>

#include <GUI/Label.h>
#include <GUI/TextButton.h>
#include <GUI/VBox.h>

#include <misc/string_util.h>

class WindTrapInterface : public DefaultStructureInterface {
public:
    static WindTrapInterface* create(int objectID) {
        WindTrapInterface* tmp = new WindTrapInterface(objectID);
        tmp->pAllocated = true;
        return tmp;
    }

protected:
    explicit WindTrapInterface(int objectID) : DefaultStructureInterface(objectID) {
        Uint32 color = ownerAccentColor();

        mainHBox.addWidget(&textVBox);

        const auto* selected = currentGame->getObjectManager().getObject(objectID);
        if (selected && (selected->getItemID() == Structure_NuclearPlant
                         || selected->getItemID() == Structure_WindTrap)) {
            plantOutputLabel.setTextFontSize(12);
            plantOutputLabel.setTextColor(color);
            textVBox.addWidget(&plantOutputLabel, (Sint32)18);
        }

        requiredEnergyLabel.setTextFontSize(12);
        requiredEnergyLabel.setTextColor(color);
        textVBox.addWidget(&requiredEnergyLabel, (Sint32)18);
        producedEnergyLabel.setTextFontSize(12);
        producedEnergyLabel.setTextColor(color);
        textVBox.addWidget(&producedEnergyLabel, (Sint32)18);

        if(ModManager::instance().isTornieFamilyContentActive()) {
            flamepostUpgradeButton.setText(_("Upgrade"));
            flamepostUpgradeButton.setTextColor(color);
            flamepostUpgradeButton.setTooltipText(_("Requires House IX"));
            flamepostUpgradeButton.setVisible(false);
            flamepostUpgradeButton.setOnClick(std::bind(&WindTrapInterface::onFlamepostUpgrade, this));
            textVBox.addWidget(&flamepostUpgradeButton, (Sint32)26);

            chemipostUpgradeButton.setText(_("Upgrade"));
            chemipostUpgradeButton.setTextColor(color);
            chemipostUpgradeButton.setTooltipText(_("Requires House IX and Tech Level 7"));
            chemipostUpgradeButton.setVisible(false);
            chemipostUpgradeButton.setOnClick(std::bind(&WindTrapInterface::onChemipostUpgrade, this));
            textVBox.addWidget(&chemipostUpgradeButton, (Sint32)26);

            slowpostUpgradeButton.setText(_("Upgrade"));
            slowpostUpgradeButton.setTextColor(color);
            slowpostUpgradeButton.setTooltipText(_("Requires House IX and Tech Level 7"));
            slowpostUpgradeButton.setVisible(false);
            slowpostUpgradeButton.setOnClick([this]() {
                if(auto* post = dynamic_cast<Scoutpost*>(
                       currentGame->getObjectManager().getObject(this->objectID))) {
                    post->handleSlowpostUpgradeClick();
                }
            });
            textVBox.addWidget(&slowpostUpgradeButton, (Sint32)26);
        }

        if(ModManager::instance().isTornieFamilyContentActive()) {
            powerUpgradeButton.setText(_("Power x2") + " (150)");
            powerUpgradeButton.setTextColor(color);
            powerUpgradeButton.setTooltipText(_("Requires House IX and Tech Level 7"));
            powerUpgradeButton.setVisible(false);
            powerUpgradeButton.setOnClick([this]() {
                if(auto* trap = dynamic_cast<WindTrap*>(
                       currentGame->getObjectManager().getObject(this->objectID))) {
                    trap->handlePowerUpgradeClick();
                }
            });
            textVBox.addWidget(&powerUpgradeButton, (Sint32)26);
        }

        cityStats_.attachTo(textVBox, color, false,
                            selected && selected->getItemID() == Structure_WindTrap);

        textVBox.addWidget(Spacer::create(),0.99);
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
        if (const auto* plant = dynamic_cast<const NuclearPlant*>(pObject))
            plantOutputLabel.setText(" " + _("Output") + ": " + std::to_string(plant->getProducedPower()));

        if (const auto* windtrap = dynamic_cast<const WindTrap*>(pObject))
            plantOutputLabel.setText(" " + _("Output") + ": " + std::to_string(windtrap->getProducedPower()));

        requiredEnergyLabel.setText(" " + _("Required") + ": " + std::to_string(pOwner->getPowerRequirement()));
        producedEnergyLabel.setText(" " + _("Produced") + ": " + std::to_string(pOwner->getProducedPower()));

        const auto* trap = dynamic_cast<const WindTrap*>(pObject);
        const bool canPowerUpgrade = trap && trap->isPowerUpgradeEligible()
            && !currentGame->isObserving() && pLocalPlayer
            && currentGame->canControlHouse(pOwner);
        powerUpgradeButton.setVisible(canPowerUpgrade);
        powerUpgradeButton.setEnabled(canPowerUpgrade && pOwner->getCredits() >= 150);

        Scoutpost* pScoutpost = dynamic_cast<Scoutpost*>(pObject);
        const bool showFlamepostUpgrade = pScoutpost != nullptr
            && pScoutpost->isFlamepostUpgradeEligible()
            && pOwner->getNumItems(Structure_IX) > 0;
        flamepostUpgradeButton.setVisible(showFlamepostUpgrade);
        if(showFlamepostUpgrade) {
            flamepostUpgradeButton.setText(_("Upgrade"));
            flamepostUpgradeButton.setTooltipText(_("Upgrade this Scoutpost to a Flamepost"));
        }

        const bool showChemipostUpgrade = pScoutpost != nullptr
            && pScoutpost->isChemipostUpgradeEligible()
            && pOwner->getNumItems(Structure_IX) > 0;
        chemipostUpgradeButton.setVisible(showChemipostUpgrade);
        if(showChemipostUpgrade) {
            chemipostUpgradeButton.setText(_("Upgrade"));
            chemipostUpgradeButton.setTooltipText(_("Upgrade this Scoutpost to a healing Chemipost"));
        }

        const bool showSlowpostUpgrade = pScoutpost && pScoutpost->isSlowpostUpgradeEligible()
            && pOwner->getNumItems(Structure_IX) > 0 && !currentGame->isObserving()
            && pLocalPlayer && currentGame->canControlHouse(pOwner);
        slowpostUpgradeButton.setVisible(showSlowpostUpgrade);
        if(showSlowpostUpgrade) {
            slowpostUpgradeButton.setEnabled(pScoutpost->canUpgradeToSlowpost());
            slowpostUpgradeButton.setTooltipText(_("Upgrade this Scoutpost to a Slowpost") + " ("
                + std::to_string(pScoutpost->getSlowpostUpgradeCost()) + " " + _("credits") + ")");
        }

        cityStats_.update(dynamic_cast<StructureBase*>(pObject));

        return DefaultStructureInterface::update();
    }

private:
    void onFlamepostUpgrade() {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        Scoutpost* pScoutpost = dynamic_cast<Scoutpost*>(pObject);
        if(pScoutpost != nullptr) {
            pScoutpost->handleFlamepostUpgradeClick();
        }
    }

    void onChemipostUpgrade() {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        Scoutpost* pScoutpost = dynamic_cast<Scoutpost*>(pObject);
        if(pScoutpost != nullptr) {
            pScoutpost->handleChemipostUpgradeClick();
        }
    }

    VBox       textVBox;

    Label      plantOutputLabel;
    Label      requiredEnergyLabel;
    Label      producedEnergyLabel;
    TextButton flamepostUpgradeButton;
    TextButton chemipostUpgradeButton;
    TextButton powerUpgradeButton;
    TextButton slowpostUpgradeButton;

    CityStatsBox cityStats_;
};

#endif // WINDTRAPINTERFACE_H
