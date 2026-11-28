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
#include <dunecity/ZoneGarrisonPolicy.h>

#include <GUI/Label.h>
#include <GUI/TextButton.h>
#include <Command.h>
#include <GUI/VBox.h>

#include <misc/string_util.h>

#include <algorithm>
#include <array>
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
        // Use the existing portrait area: a 3-by-2 grid fits five occupants even
        // at the shared desktop/mobile 640x480 logical minimum.
        garrisonRoster.setVisible(false);
        topBox.addWidget(&garrisonRoster, Point(4,24), Point(SIDEBARWIDTH-33,54));

        mainHBox.addWidget(&textVBox);

        zoneNameLabel.setTextFontSize(14);
        zoneNameLabel.setTextColor(color, COLOR_TRANSPARENT);
        textVBox.addWidget(&zoneNameLabel, (Sint32)34);

        densityLabel.setTextFontSize(14);
        densityLabel.setTextColor(color, COLOR_TRANSPARENT);
        textVBox.addWidget(&densityLabel, (Sint32)22);

        poweredLabel.setTextFontSize(14);
        poweredLabel.setTextColor(color, COLOR_TRANSPARENT);
        textVBox.addWidget(&poweredLabel, (Sint32)22);

        // Zone Garrisons: how many soldiers hold the zone, and the order to bring them out.
        garrisonLabel.setTextFontSize(14);
        garrisonLabel.setTextColor(color, COLOR_TRANSPARENT);
        garrisonLabel.setVisible(false);
        textVBox.addWidget(&garrisonLabel, (Sint32)22);
        evacuateButton.setText(_("Evacuate"));
        evacuateButton.setTooltipText(_("The soldiers garrisoning this zone come out beside it. A captured zone goes back to its owner."));
        evacuateButton.setOnClick([id = objectID] {
            if (currentGame && pLocalPlayer)
                currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(),
                    CMD_ZONE_EVACUATE, static_cast<Uint32>(id)));
        });
        evacuateButton.setVisible(false);
        textVBox.addWidget(&evacuateButton, (Sint32)22);

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
        garrisonRoster.refresh(*pZone);
        const bool showRoster = garrisonRoster.count != 0;
        const bool showSkinIcon = skinIcon != nullptr && !showRoster;
        if(showSkinIcon) objPicture.setTexture(skinIcon);
        objPicture.setVisible(showSkinIcon);
        zonePreview.setVisible(!showSkinIcon && !showRoster);
        garrisonRoster.setVisible(showRoster);

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
        if (pZone->getZoneType() == DuneCity::ZoneType::Residential
            && pZone->getResidentialPopulation() > 0 && pZone->getResidentialPopulation() <= 8)
            densityLabel.setText(" L0: " + _("Houses") + " " + std::to_string(pZone->getResidentialPopulation()) + "/8");
        else
            densityLabel.setText(" " + _("Density") + ": " + std::to_string(density) + "/" + std::to_string(DuneCity::getStructureMaxLevel(pZone->getItemID())));

        // Use exactly the growth/animation power predicate, including a vacant
        // lot with no generation (0 produced / 0 required).
        const bool powered = pZone->hasCityPower();
        poweredLabel.setText(std::string(" ") + (powered ? _("Powered") : _("UNPOWERED")));

        const bool garrisoned = pZone->isGarrisoned();
        garrisonLabel.setVisible(garrisoned);
        // A captured zone says so: it goes back to its owner once the garrison leaves.
        if (garrisoned)
            garrisonLabel.setText(std::string(" ") + (pZone->getGarrisonOriginalOwner() >= 0 ? "" : _("Garrison") + std::string(": "))
                                  + std::to_string(pZone->getGarrisonSize())
                                  + "/" + std::to_string(DuneCity::ZoneGarrison::kCapacity)
                                  + (pZone->getGarrisonOriginalOwner() >= 0 ? " " + _("(captured)") : std::string()));
        evacuateButton.setVisible(garrisoned);
        evacuateButton.setEnabled(garrisoned && currentGame->canControlHouse(pZone->getOwner()));

        cityStats_.update(pZone);

        return DefaultStructureInterface::update();
    }

private:
    class GarrisonRoster : public Widget {
    public:
        struct Entry {
            Uint32 objectID = NONE_ID;
            int itemID = ItemID_Invalid;
            FixPoint health = 0;
            int maxHealth = 0;
            Uint32 healthColor = COLOR_BLACK;
            SDL_Texture* icon = nullptr; // GFXManager owns the same portraits as unit panels.
        };
        std::array<Entry, DuneCity::ZoneGarrison::kCapacity> entries{};
        size_t count = 0;

        void refresh(const ZoneStructure& zone) {
            count = 0;
            for(const Uint32 id : zone.getGarrison()) {
                const auto* unit = dynamic_cast<const UnitBase*>(currentGame->getObjectManager().getObject(id));
                // Occupants are intentionally inactive and invisible. Skip only stale/dead
                // IDs; never use unit visibility as an occupancy test.
                if(!unit || unit->getHealth() <= 0) continue;
                if(count == entries.size()) break;
                entries[count++] = {id, unit->getItemID(), unit->getHealth(), unit->getMaxHealth(),
                    unit->getHealthColor(), resolveItemPicture(unit->getItemID(),
                        static_cast<HOUSETYPE>(unit->getProductionHouseID()))};
            }
        }

        SDL_Rect slotRect(size_t index, Point position) const {
            constexpr int columns = 3;
            const int width = getSize().x / columns;
            const int height = getSize().y / 2;
            return {position.x + int(index % columns) * width,
                    position.y + int(index / columns) * height, width, height};
        }

        void draw(Point position) override {
            if(!isVisible()) return;
            for(size_t i = 0; i < count; ++i) {
                const Entry& entry = entries[i];
                const SDL_Rect slot = slotRect(i, position);
                if(slot.w < 4 || slot.h < 8) continue;
                if(entry.icon) {
                    const Point source = getTextureSize(entry.icon);
                    if(source.x > 0 && source.y > 0) {
                        const double scale = std::min(double(slot.w - 2) / source.x,
                                                      double(slot.h - 6) / source.y);
                        SDL_Rect icon{slot.x, slot.y, int(source.x * scale), int(source.y * scale)};
                        icon.x += (slot.w - icon.w) / 2;
                        SDL_RenderCopy(renderer, entry.icon, nullptr, &icon);
                    }
                }
                SDL_Rect bar{slot.x + 1, slot.y + slot.h - 5, slot.w - 2, 4};
                renderFillRect(renderer, &bar, COLOR_BLACK);
                bar.x += 1;
                bar.y += 1;
                bar.w = entry.maxHealth > 0
                    ? std::clamp((entry.health * (bar.w - 2) / entry.maxHealth).lround(), 0, bar.w - 2) : 0;
                bar.h = 2;
                if(bar.w > 0) renderFillRect(renderer, &bar, entry.healthColor);
            }
        }
    } garrisonRoster;

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
    Label   garrisonLabel;
    TextButton evacuateButton;

    CityStatsBox cityStats_;
};

#endif // ZONESTRUCTUREINTERFACE_H
