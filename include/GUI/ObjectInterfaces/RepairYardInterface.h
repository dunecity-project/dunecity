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

#ifndef REPAIRYARDINTERFACE_H
#define REPAIRYARDINTERFACE_H

#include "DefaultStructureInterface.h"
#include "CityStatsBox.h"

#include <GUI/HBox.h>
#include <GUI/ProgressBar.h>
#include <GUI/VBox.h>

#include <units/UnitBase.h>

#include <structures/RepairYard.h>

class RepairYardInterface : public DefaultStructureInterface {
public:
    static RepairYardInterface* create(int objectID) {
        RepairYardInterface* tmp = new RepairYardInterface(objectID);
        tmp->pAllocated = true;
        return tmp;
    }

protected:
    explicit RepairYardInterface(int objectID) : DefaultStructureInterface(objectID) {
        // One full-width column: the city-sim stats rows on top (only relevant
        // in city mode but harmless otherwise — labels just show em-dashes),
        // the repair-unit progress icon centred underneath.
        //
        // The icon used to share a row with the stats column. A city stat line
        // is wider than the whole sidebar, so the text claimed the row and the
        // icon was laid out past the sidebar's right edge while a unit was
        // being repaired. Stacking keeps both inside the panel at every
        // supported resolution, in city and in vanilla games.
        Uint32 color = ownerAccentColor();
        mainHBox.addWidget(&textVBox);
        cityStats_.attachTo(textVBox, color, false, false, SIDEBARWIDTH - 25);

        textVBox.addWidget(Spacer::create(), 0.5);
        repairUnitRow.addWidget(Spacer::create());
        repairUnitRow.addWidget(&repairUnitProgressBar);
        repairUnitRow.addWidget(Spacer::create());
        textVBox.addWidget(&repairUnitRow, repairUnitRowHeight);
        textVBox.addWidget(Spacer::create(), 0.5);
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

        RepairYard* pRepairYard = dynamic_cast<RepairYard*>(pObject);
        if(pRepairYard != nullptr) {
            UnitBase* pUnit = pRepairYard->getRepairUnit();

            if(pUnit != nullptr) {
                repairUnitProgressBar.setVisible(true);
                repairUnitProgressBar.setTexture(resolveItemPicture(pUnit->getItemID()));
                repairUnitProgressBar.setProgress( ((pUnit->getHealth()*100)/pUnit->getMaxHealth()).toDouble());
            } else {
                repairUnitProgressBar.setVisible(false);
            }
        }

        cityStats_.update(dynamic_cast<StructureBase*>(pObject));

        return DefaultStructureInterface::update();
    }

private:
    /// Tall enough for every unit picture the sidebar can show.
    static constexpr Sint32 repairUnitRowHeight = 60;

    PictureProgressBar  repairUnitProgressBar;
    HBox                repairUnitRow;
    VBox                textVBox;
    CityStatsBox        cityStats_;
};

#endif // REPAIRYARDINTERFACE_H
