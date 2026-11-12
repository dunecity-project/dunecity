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

#ifndef BUILDERINTERFACE_H
#define BUILDERINTERFACE_H

#include "DefaultStructureInterface.h"

#include <FileClasses/FontManager.h>
#include <FileClasses/TextManager.h>

#include <GUI/TextButton.h>
#include <GUI/ProgressBar.h>
#include <GUI/Label.h>
#include <GUI/dune/BuilderList.h>

#include <misc/draw_util.h>

#include <structures/BuilderBase.h>
#include <structures/StarPort.h>
#include <structures/ChaosFactory.h>

#include <algorithm>
#include <cstdint>

class BuilderInterface : public DefaultStructureInterface {
public:
    static BuilderInterface* create(int objectID) {
        BuilderInterface* tmp = new BuilderInterface(objectID);
        tmp->pAllocated = true;
        return tmp;
    }

protected:
    bool handleMouseWheel(Sint32 x, Sint32 y, bool up) override {
        // Scrolling changes only the inspected list's viewport. Keep production
        // buttons disabled while allowing observers to inspect every entry.
        if(currentGame->isObserving() && !isEnabled() && isVisible()
           && pBuilderList && x >= 0 && y >= 0 && x < getSize().x && y < getSize().y)
            return pBuilderList->handleMouseWheel(0, 0, up);
        return DefaultStructureInterface::handleMouseWheel(x, y, up);
    }

    explicit BuilderInterface(int objectID) : DefaultStructureInterface(objectID) {
        Uint32 color = ownerAccentColor();

        upgradeButton.setText(_("Upgrade"));
        upgradeButton.setTextColor(color);
        upgradeButton.setVisible(false);
        upgradeButton.setTooltipText(_("Upgrade this structure (Hotkey: U)"));
        upgradeButton.setOnClick(std::bind(&BuilderInterface::onUpgrade, this));

        upgradeProgressBar.setText("0 %");
        upgradeProgressBar.setTextColor(color);
        upgradeProgressBar.setVisible(false);
        const int upgradeWidth = ModManager::instance().isTornieFamilyContentActive() ? 75 : 83;
        topBox.addWidget(&upgradeButton,Point(18,2),Point(upgradeWidth,18));
        topBox.addWidget(&upgradeProgressBar,Point(24,2),Point(upgradeWidth - 6,18));
        int upgradeRow = 20;
        for(Label* label : {&upgradeStatusLabel, &upgradeTotalLabel, &upgradePaidLabel}) {
            label->setTextFontSize(11);
            label->setTextColor(color);
            label->setAlignment(Alignment_HCenter);
            label->setVisible(false);
            topBox.addWidget(label, Point(0, upgradeRow), Point(SIDEBARWIDTH - 25, 20));
            upgradeRow += 20;
        }

        mainHBox.addWidget(Spacer::create());

        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        BuilderBase* pBuilder = dynamic_cast<BuilderBase*>(pObject);
        if(pBuilder) {
            pBuilderList = BuilderList::create(pBuilder->getObjectID());
            mainHBox.addWidget(pBuilderList);
        } else {
            pBuilderList = nullptr;
        }

        mainHBox.addWidget(Spacer::create());

        StarPort* pStarport = dynamic_cast<StarPort*>(pObject);
        ChaosFactory* pChaosFactory = dynamic_cast<ChaosFactory*>(pObject);
        if(pStarport != nullptr || pChaosFactory != nullptr) {
            starportTimerLabel.setTextFontSize(28);
            starportTimerLabel.setTextColor(COLOR_WHITE, COLOR_TRANSPARENT);
            starportTimerLabel.setAlignment((Alignment_Enum) (Alignment_HCenter | Alignment_VCenter));
            topBox.addWidget(&starportTimerLabel, topBox.getWidgetPosition(&topBoxHBox) + topBoxHBox.getWidgetPosition(&objPicture) + Point(0, 4), objPicture.getSize());
        }
    }


    void onUpgrade() {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        BuilderBase* pBuilder = dynamic_cast<BuilderBase*>(pObject);
        if(pBuilder != nullptr && currentGame->canControlHouse(pBuilder->getOwner()) && pBuilder->canStartUpgrade()) {
            pBuilder->handleUpgradeClick();
        }
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

        BuilderBase* pBuilder = dynamic_cast<BuilderBase*>(pObject);
        if(pBuilder != nullptr) {
            updateSellButton(pBuilder);
            StarPort* pStarport = dynamic_cast<StarPort*>(pBuilder);
            ChaosFactory* pChaosFactory = dynamic_cast<ChaosFactory*>(pBuilder);
            if(pChaosFactory != nullptr) {
                const int seconds = pChaosFactory->getOfferSecondsRemaining();
                starportTimerLabel.setText(std::to_string(seconds) + "s");
            } else if(pStarport != nullptr) {
                int arrivalTimer = pStarport->getArrivalTimer();
                if(arrivalTimer > 0) {
                    int seconds = ((arrivalTimer*10)/(MILLI2CYCLES(30*1000))) + 1;
                    starportTimerLabel.setText(std::to_string(seconds));
                } else {
                    starportTimerLabel.setText("");
                }
            }

            const bool canControl = currentGame->canControlHouse(pBuilder->getOwner());
            const bool upgrading = pBuilder->isUpgrading();
            const bool healthy = pBuilder->getHealth() >= pBuilder->getMaxHealth();
            const int cost = pBuilder->getUpgradeCost();
            upgradeProgressBar.setVisible(upgrading);
            upgradeButton.setVisible(canControl && healthy && !upgrading && pBuilder->isAllowedToUpgrade());
            upgradeButton.setEnabled(canControl && healthy && pBuilder->canStartUpgrade());
            upgradeStatusLabel.setVisible(upgrading);
            upgradeTotalLabel.setVisible(upgrading);
            upgradePaidLabel.setVisible(upgrading);
            // Use the portrait's existing space: no build-list rows are lost at 640x480.
            objPicture.setVisible(!upgrading);
            starportTimerLabel.setVisible(!upgrading);
            if(upgrading) {
                const FixPoint percent = cost > 0 ? pBuilder->getUpgradeProgress() * 100 / cost : FixPoint(0);
                upgradeProgressBar.setProgress(percent.toDouble());
                upgradeProgressBar.setText(std::to_string(std::clamp(percent.floor(), 0, 100)) + " %");
                upgradeStatusLabel.setText(pBuilder->isUpgradePaused() ? _("Paused: no credits") : _("Upgrading"));
                upgradeTotalLabel.setText(_("Total: ") + std::to_string(cost) + _(" cr"));
                upgradePaidLabel.setText(_("Paid: ") + upgradeCreditText(pBuilder->getUpgradeProgress()));
            } else {
                upgradeProgressBar.setProgress(0);
                upgradeStatusLabel.setText("");
                upgradeTotalLabel.setText("");
                upgradePaidLabel.setText("");
            }
            const bool installments = currentGame->getGameInitSettings().getGameOptions().technologyInstallments;
            if(displayedUpgradeCost != cost || displayedTechnologyInstallments != installments) {
                displayedUpgradeCost = cost;
                displayedTechnologyInstallments = installments;
                upgradeButton.setTooltipText(_("Upgrade: ") + std::to_string(cost)
                    + (installments ? _(" cr, paid per tick. No full balance needed. (U)")
                                    : _(" cr balance needed to start; paid per tick. (U)")));
            }
            repairButton.setVisible(canControl && !healthy);
            repairButton.setToggleState(pBuilder->isRepairing());
        }

        return true;
    }


    static std::string upgradeCreditText(FixPoint value) {
        const auto unit = FixPoint(1).getRawValue();
        const auto raw = std::max<std::int64_t>(0, value.getRawValue());
        const auto cents = (raw / unit) * 100 + (raw % unit) * 100 / unit;
        return std::to_string(cents / 100) + "." + (cents % 100 < 10 ? "0" : "") + std::to_string(cents % 100);
    }


    TextButton      upgradeButton;
    TextProgressBar upgradeProgressBar;
    Label           upgradeStatusLabel;
    Label           upgradeTotalLabel;
    Label           upgradePaidLabel;
    Label           starportTimerLabel;
    BuilderList*    pBuilderList;
    int displayedUpgradeCost = -1;
    bool displayedTechnologyInstallments = false;
};

#endif // BUILDERINTERFACE_H
