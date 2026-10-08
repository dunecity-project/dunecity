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

#ifndef DEFAULTSTRUCTUREINTERFACE_H
#define DEFAULTSTRUCTUREINTERFACE_H

#include "DefaultObjectInterface.h"

#include <globals.h>
#include <Command.h>
#include <misc/MenuPalette.h>
#include <GUI/TextButton.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>

#include <structures/StructureBase.h>

class DefaultStructureInterface : public DefaultObjectInterface {
public:
    static DefaultStructureInterface* create(int objectID) {
        DefaultStructureInterface* tmp = new DefaultStructureInterface(objectID);
        tmp->pAllocated = true;
        return tmp;
    }

protected:
    explicit DefaultStructureInterface(int objectID) : DefaultObjectInterface(objectID) {
        SDL_Texture* pUIRepair = pGFXManager->getUIGraphic(UI_Repair);
        SDL_Texture* pUIRepairPressed = pGFXManager->getUIGraphic(UI_Repair_Pressed);

        repairButton.setTextures(pUIRepair, pUIRepairPressed);
        repairButton.setToggleButton(true);
        repairButton.setVisible(false);
        repairButton.setTooltipText(_("Repair this structure (Hotkey: R)"));
        repairButton.setOnClick(std::bind(&DefaultStructureInterface::OnRepair, this));

        topBox.addWidget(&repairButton, Point(2,2), getTextureSize(pUIRepair));
        destroyButton.setText(_("Destroy"));
        destroyButton.setTooltipText(_("Destroy this building. No building refund. Nuclear plants explode."));
        destroyButton.setOnClick([this]() {
            if (pLocalPlayer) currentGame->getCommandManager().addCommand(
                Command(pLocalPlayer->getPlayerID(), CMD_STRUCTURE_DEMOLISH, this->objectID));
        });
        destroyButton.setVisible(false);
        topBox.addWidget(&destroyButton, Point(28,0), Point(SIDEBARWIDTH-53,22));
    }

    void draw(Point position) override {
        if(!isVisible()) return;
        SDL_Rect panel{position.x, position.y, getSize().x, getSize().y};
        renderFillRect(renderer, &panel, MenuTheme::background);
        DefaultObjectInterface::draw(position);
    }

    void OnRepair() {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        StructureBase* pStructure = dynamic_cast<StructureBase*>(pObject);
        if(pStructure != nullptr) {
            pStructure->handleRepairClick();
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

        StructureBase* pStructure = dynamic_cast<StructureBase*>(pObject);
        if(pStructure != nullptr) {
            // While observing, pLocalPlayer is the detached view identity and its house is
            // whichever house the view is anchored to - so without this the Destroy button
            // would appear on that house's buildings. Repair is hidden for the same reason:
            // neither is an observer's to press. Both are also refused downstream, but an
            // observer should not be shown controls at all.
            const bool observing = currentGame->isObserving();
            destroyButton.setVisible(!observing && pLocalPlayer
                                     && pLocalPlayer->getHouse() == pStructure->getOwner());
            if(observing || pStructure->getHealth() >= pStructure->getMaxHealth()) {
                repairButton.setVisible(false);
            } else {
                repairButton.setVisible(true);
                repairButton.setToggleState(pStructure->isRepairing());
            }
        }

        return true;
    }

    PictureButton   repairButton;
    TextButton destroyButton;
};

#endif // DEFAULTSTRUCTUREINTERFACE_H
