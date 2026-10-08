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

#ifndef DEFAULTOBJECTINTERFACE_H
#define DEFAULTOBJECTINTERFACE_H

#include "ObjectInterface.h"

#include <GUI/PictureLabel.h>

#include <globals.h>

#include <Game.h>
#include <dunecity/CityConstants.h>

#include <sand.h>

#include <misc/exceptions.h>

#include <ObjectBase.h>
#include <House.h>
#include <units/UnitBase.h>


class DefaultObjectInterface : public ObjectInterface {
public:
    static DefaultObjectInterface* create(int objectID) {
        DefaultObjectInterface* tmp = new DefaultObjectInterface(objectID);
        tmp->pAllocated = true;
        return tmp;
    }

protected:
    explicit DefaultObjectInterface(int objectID) : ObjectInterface() {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        if(pObject == nullptr) {
            THROW(std::invalid_argument, "Failed to resolve ObjectID %d!", objectID);
        }

        this->objectID = objectID;
        itemID = pObject->getItemID();

        addWidget(&topBox,Point(0,0),Point(SIDEBARWIDTH - 25,80));

        addWidget(&mainHBox,Point(0,80),Point(SIDEBARWIDTH - 25,getRendererHeight() - 80 - 148));

        topBox.addWidget(&topBoxHBox,Point(0,22),Point(SIDEBARWIDTH - 25,58));

        topBoxHBox.addWidget(Spacer::create());
        topBoxHBox.addWidget(&objPicture);

        const HOUSETYPE pictureHouse = static_cast<HOUSETYPE>(
            DuneCity::isCityOnlyStructure(itemID) && pObject->getOwner()
                ? pObject->getOwner()->getHouseID()
                : pObject->getOriginalHouseID());
        objPicture.setTexture(resolveItemPicture(itemID, pictureHouse));

        topBoxHBox.addWidget(Spacer::create());
    };

    virtual ~DefaultObjectInterface() { ; };

    /**
        Accent colour for this panel: the colour of the *inspected object's* owner, not of the
        local view house. Identical for one's own buildings. While observing (or in debug) the
        panel can belong to any house, and every figure in it is already read from
        pObject->getOwner() - so the colour has to agree about whose building this is instead of
        tinting another house's numbers with the view house's colour.
    */
    Uint32 ownerAccentColor() const {
        const ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        const House* pOwner = pObject ? pObject->getOwner() : nullptr;
        const int houseID = pOwner ? pOwner->getHouseID()
                                   : (pLocalHouse ? pLocalHouse->getHouseID() : HOUSE_HARKONNEN);
        return getHouseColorRGB(getHouseVisualHouse(houseID), 3);
    }

    /**
        This method updates the object interface.
        If the object doesn't exists anymore then update returns false.
        \return true = everything ok, false = the object container should be removed
    */
    bool update() override
    {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        return (pObject != nullptr);
    }

    int             objectID;
    int             itemID;

    StaticContainer topBox;
    HBox            topBoxHBox;
    HBox            mainHBox;
    PictureLabel    objPicture;
};

#endif // DEFAULTOBJECTINTERFACE_H
