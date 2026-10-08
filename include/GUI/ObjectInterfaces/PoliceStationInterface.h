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

#ifndef POLICESTATIONINTERFACE_H
#define POLICESTATIONINTERFACE_H

#include "DefaultStructureInterface.h"
#include "CityStatsBox.h"

#include <FileClasses/FontManager.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>

#include <GUI/Label.h>
#include <GUI/ProgressBar.h>
#include <GUI/VBox.h>

#include <House.h>
#include <structures/PoliceStation.h>
#include <dunecity/CityEffects.h>

class PoliceStationInterface : public DefaultStructureInterface {
public:
    static PoliceStationInterface* create(int objectID) {
        const auto tmp = new PoliceStationInterface(objectID);
        tmp->pAllocated = true;
        return tmp;
    }

protected:
    explicit PoliceStationInterface(int objectID) : DefaultStructureInterface(objectID) {
        mainHBox.addWidget(&policeStationVBox);
        policeStationVBox.addWidget(&spawnBox, (Sint32)65);

        SDL_Texture* pTexture = pGFXManager->getSmallDetailPic(Picture_Trike);
        spawnBox.addWidget(&spawnProgressBar, Point((SIDEBARWIDTH - 25 - getWidth(pTexture))/2, 5), getTextureSize(pTexture));
        spawnBox.addWidget(&spawnSelectButton, Point((SIDEBARWIDTH - 25 - getWidth(pTexture))/2, 5), getTextureSize(pTexture));

        sdl2::surface_ptr pText{ pFontManager->createSurfaceWithText(_("READY"), COLOR_WHITE, 12) };
        sdl2::surface_ptr pReady{ SDL_CreateRGBSurface(0, getWidth(pTexture), getHeight(pTexture), SCREEN_BPP, RMASK, GMASK, BMASK, AMASK) };
        SDL_FillRect(pReady.get(), nullptr, COLOR_TRANSPARENT);

        SDL_Rect dest = calcAlignedDrawingRect(pText.get(), pReady.get());
        SDL_BlitSurface(pText.get(), nullptr, pReady.get(), &dest);

        spawnSelectButton.setTextures(convertSurfaceToTexture(pReady.get()));
        spawnSelectButton.setVisible(false);
        spawnSelectButton.setTooltipText(_("Deploy 3 troopers and 1 trike near this station"));
        spawnSelectButton.setOnClick(std::bind(&PoliceStationInterface::onSpawn, this));
        reinforcementStatusLabel.setText("Unit limit\nreached");
        reinforcementStatusLabel.setTextFontSize(11);
        reinforcementStatusLabel.setTextColor(COLOR_WHITE, COLOR_BLACK);
        reinforcementStatusLabel.setVisible(false);
        spawnBox.addWidget(&reinforcementStatusLabel, Point((SIDEBARWIDTH - 25 - getWidth(pTexture))/2, 5), getTextureSize(pTexture));

        Uint32 color = ownerAccentColor();

        levelLabel.setTextFontSize(11);
        levelLabel.setTextColor(color);
        policeStationVBox.addWidget(&levelLabel, (Sint32)22);

        vehiclesLabel.setTextFontSize(11);
        vehiclesLabel.setTextColor(color);
        policeStationVBox.addWidget(&vehiclesLabel, (Sint32)22);

        poweredLabel.setTextFontSize(11);
        poweredLabel.setTextColor(color);
        policeStationVBox.addWidget(&poweredLabel, (Sint32)22);

        cityStats_.attachTo(policeStationVBox, color, /*isZone=*/false);
        policeStationVBox.addWidget(Spacer::create(), 0.99);
    }

    bool update() override {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        if(pObject == nullptr) {
            return false;
        }

        PoliceStation* pPoliceStation = dynamic_cast<PoliceStation*>(pObject);
        if(pPoliceStation != nullptr) {
            SDL_Texture* pTexture = pGFXManager->getSmallDetailPic(Picture_Trike);
            spawnProgressBar.setTexture(pTexture);
            spawnProgressBar.setProgress(pPoliceStation->getPercentComplete());
            spawnSelectButton.setVisible(pPoliceStation->canSpawnVehicles());
            const bool unitLimitReached = pPoliceStation->isUnitLimitReached();
            const bool armyTargetReached = pPoliceStation->isReinforcementBudgetReached();
            reinforcementStatusLabel.setText(unitLimitReached ? "Unit limit\nreached"
                                                              : "Army target\nreached");
            reinforcementStatusLabel.setVisible(unitLimitReached || armyTargetReached);

            levelLabel.setText("3 Troopers");
            vehiclesLabel.setText("1 Trike");
            poweredLabel.setText("Free / " + std::to_string(pPoliceStation->getMaxSpawnTimer() / MILLI2CYCLES(1000)) + "s");

            cityStats_.update(pPoliceStation);
        }

        return DefaultStructureInterface::update();
    }

private:
    Label reinforcementStatusLabel;
    void onSpawn() {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        PoliceStation* pPoliceStation = dynamic_cast<PoliceStation*>(pObject);
        if(pPoliceStation != nullptr) {
            pPoliceStation->handleSpawnClick();
        }
    }

    VBox                policeStationVBox;
    StaticContainer     spawnBox;
    PictureProgressBar  spawnProgressBar;
    PictureButton       spawnSelectButton;
    Label               levelLabel;
    Label               poweredLabel;
    Label               vehiclesLabel;
    CityStatsBox        cityStats_;
};

#endif // POLICESTATIONINTERFACE_H
