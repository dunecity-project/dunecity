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

#ifndef ZONESTRUCTUREINTERFACE_H
#define ZONESTRUCTUREINTERFACE_H

#include "DefaultStructureInterface.h"
#include "CityStatsBox.h"

#include <FileClasses/FontManager.h>
#include <FileClasses/TextManager.h>

#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <structures/ZoneStructure.h>
#include <dunecity/CityConstants.h>

#include <GUI/Label.h>
#include <GUI/TextButton.h>
#include <Command.h>
#include <GUI/VBox.h>

#include <misc/string_util.h>

#include <string>

class ZoneStructureInterface : public DefaultStructureInterface {
public:
    static ZoneStructureInterface* create(int objectID) {
        ZoneStructureInterface* tmp = new ZoneStructureInterface(objectID);
        tmp->pAllocated = true;
        return tmp;
    }

protected:
    explicit ZoneStructureInterface(int objectID) : DefaultStructureInterface(objectID) {
        constexpr Uint32 color = COLOR_WHITE;
        objPicture.setVisible(false);
        zonePreview.objectID = objectID;
        topBox.addWidget(&zonePreview, Point(4,24), Point(SIDEBARWIDTH-33,54));

        mainHBox.addWidget(&textVBox);

        zoneNameLabel.setTextFontSize(14);
        zoneNameLabel.setTextColor(color, COLOR_TRANSPARENT);
        textVBox.addWidget(&zoneNameLabel, (Sint32)34);

        densityLabel.setTextFontSize(14);
        densityLabel.setTextColor(color, COLOR_TRANSPARENT);
        textVBox.addWidget(&densityLabel, (Sint32)24);

        poweredLabel.setTextFontSize(14);
        poweredLabel.setTextColor(color, COLOR_TRANSPARENT);
        textVBox.addWidget(&poweredLabel, (Sint32)24);

        cityStats_.attachTo(textVBox, color, /*isZone=*/true);

        textVBox.addWidget(Spacer::create(), 0.99);
    }

    bool update() override {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        if (pObject == nullptr) {
            return false;
        }

        ZoneStructure* pZone = dynamic_cast<ZoneStructure*>(pObject);
        if (pZone == nullptr) {
            return DefaultStructureInterface::update();
        }

        // Refresh on every update: civic growth and a live graphics-skin switch
        // can change the portrait while this same properties panel stays open.
        SDL_Texture* skinIcon = nullptr;
        if(pZone->getOwner()
           && pGFXManager->isDuneCityHouseUsingDune2(pZone->getOwner()->getHouseID())) {
            const auto house = pZone->getOwner()->getHouseID();
            switch(pZone->getCivicOverlay()) {
                case ZoneStructure::CivicOverlay::Hospital:
                    skinIcon = pGFXManager->getSmallDetailPic(Picture_Hospital, house); break;
                case ZoneStructure::CivicOverlay::Church:
                    skinIcon = pGFXManager->getSmallDetailPic(Picture_Church, house); break;
                default:
                    skinIcon = resolveItemPicture(itemID, static_cast<HOUSETYPE>(house)); break;
            }
        }
        const bool showSkinIcon = skinIcon != nullptr;
        if(showSkinIcon) objPicture.setTexture(skinIcon);
        objPicture.setVisible(showSkinIcon);
        zonePreview.setVisible(!showSkinIcon);

        std::string name;
        switch (pZone->getZoneType()) {
            case DuneCity::ZoneType::Residential: name = _("Residential") + std::string("\n") + _("Zone"); break;
            case DuneCity::ZoneType::Commercial:  name = _("Commercial") + std::string("\n") + _("Zone");  break;
            case DuneCity::ZoneType::Industrial:  name = _("Industrial") + std::string("\n") + _("Zone");  break;
            default:                              name = _("Zone");             break;
        }
        if(pZone->getCivicOverlay() == ZoneStructure::CivicOverlay::Hospital) name = _("Hospital");
        else if(pZone->getCivicOverlay() == ZoneStructure::CivicOverlay::Church) name = _("Church");
        zoneNameLabel.setText(" " + name);

        // Density lives on the underlying tile (set by the city sim / zone
        // command). Sample the structure's top-left tile.
        int density = 0;
        const Coord loc = pZone->getLocation();
        if (currentGameMap != nullptr && currentGameMap->tileExists(loc.x, loc.y)) {
            density = currentGameMap->getTile(loc.x, loc.y)->getCityZoneDensity();
        }
        if (pZone->getZoneType() == DuneCity::ZoneType::Residential && pZone->getResidentialPopulation() <= 8)
            densityLabel.setText(" Houses: " + std::to_string(pZone->getResidentialPopulation()) + "/8");
        else
            densityLabel.setText(" " + _("Density") + ": " + std::to_string(density) + "/" + std::to_string(DuneCity::getStructureMaxLevel(pZone->getItemID())));

        // Per-tile city power grid is still stubbed, so fall back to the
        // owning house's overall power state — same fallback the old hover
        // tooltip used.
        const bool powered = pZone->getOwner()->hasPower();
        poweredLabel.setText(std::string(" ") + (powered ? _("Powered") : _("UNPOWERED")));

        cityStats_.update(pZone);

        return DefaultStructureInterface::update();
    }

private:
    class ZonePreview : public Widget {
    public:
        int objectID = NONE_ID;
        void draw(Point position) override {
            if(!isVisible() || !currentGame) return;
            const auto* zone = dynamic_cast<const ZoneStructure*>(currentGame->getObjectManager().getObject(objectID));
            if(zone) zone->drawPreview({position.x, position.y, getSize().x, getSize().y});
        }
    } zonePreview;

    VBox    textVBox;

    Label   zoneNameLabel;
    Label   densityLabel;
    Label   poweredLabel;

    CityStatsBox cityStats_;
};

#endif // ZONESTRUCTUREINTERFACE_H
