#ifndef AIRPORTINTERFACE_H
#define AIRPORTINTERFACE_H
#include "DefaultStructureInterface.h"
#include "CityStatsBox.h"
#include <GUI/Label.h>
#include <units/AmbientAirplane.h>
#include <GUI/TextButton.h>
#include <dunecity/CityAircraftPolicy.h>
#include <GUI/VBox.h>
#include <structures/Airport.h>
#include <House.h>
#include <Game.h>

class AirportInterface : public DefaultStructureInterface {
public:
    static AirportInterface* create(int id) {
        auto* result=new AirportInterface(id);
        result->pAllocated=true;
        return result;
    }
protected:
    explicit AirportInterface(int id) : DefaultStructureInterface(id) {
        mainHBox.addWidget(&details);
        patrolLabel.setTextFontSize(11);
        patrolLabel.setTextColor(COLOR_WHITE);
        details.addWidget(&patrolLabel, (Sint32)55);
        // City Plane Purchase. Under City Plane Build Time (Stefan, 2026-11-26: "airports don't build the
        // plane it is still just a buy button") the Airport builds the plane like a factory: paid at the
        // order, a progress bar while it builds, launched when done; one at a time per Airport.
        const std::string price = std::to_string(DuneCity::CityAircraft::planePrice());
        if (Airport::planeBuildTime()) {
            buyButton.setText(_("Build airplane") + std::string(" ") + price);
            buyButton.setTooltipText(_("Build a city airplane at this Airport, paid now; it takes off when built. "
                                       "One at a time per Airport, up to ten in the air."));
        } else {
            buyButton.setText(_("Buy airplane") + std::string(" ") + price);
            buyButton.setTooltipText(_("Launch a city airplane now. Up to ten in the air."));
        }
        buyButton.setOnClick([id] {
            auto* airport = dynamic_cast<Airport*>(currentGame->getObjectManager().getObject(id));
            if (airport) airport->handleBuyAirplaneClick();
        });
        buyButton.setVisible(false);
        details.addWidget(&buyButton, (Sint32)24);
        buildBar.setTextColor(COLOR_WHITE);
        buildBar.setVisible(false);
        details.addWidget(&buildBar, (Sint32)18);
        cityStats.attachTo(details,COLOR_WHITE,false);
        details.addWidget(Spacer::create(),0.99);
    }
    bool update() override {
        auto* airport=dynamic_cast<Airport*>(currentGame->getObjectManager().getObject(objectID));
        if (!airport) return false;
        if (!currentGame->getGameInitSettings().getGameOptions().airportOrnithopterPatrol) {
            // No Ornithopter patrol (the default): the panel says what the Airport does launch.
            const House* owner=airport->getOwner();
            const int flying=owner->getNumItems(Unit_AmbientAirplane);
            const bool purchase=currentGame->getGameInitSettings().getGameOptions().cityPlanePurchase;
            const int freeWait=(airport->getFreePlaneTimer()*GAMESPEED_DEFAULT+999)/1000;
            patrolLabel.setText(_("City airplane")+std::string("\n")
                +(!owner->hasPower() ? _("Needs power")
                  : flying>0 ? std::to_string(flying)
                      +(purchase ? "/"+std::to_string(DuneCity::CityAircraft::kMaxPlanesPerHouse) : std::string())
                      +_(" in flight") : _("No plane in flight"))
                +(owner->hasPower() && AmbientAirplane::freePlanes(owner) == 0
                    ? std::string("\n") + (airport->getFreePlaneTimer() > 0
                        ? _("Free plane in ") + std::to_string(freeWait / 60) + ":" + (freeWait % 60 < 10 ? "0" : "")
                              + std::to_string(freeWait % 60)
                        : _("Free plane ready"))
                    : std::string()));
            const bool building=purchase && airport->isBuildingPlane();
            buyButton.setVisible(purchase && !building);
            buyButton.setEnabled(purchase && currentGame->canControlHouse(owner) && airport->canBuyAirplane());
            buildBar.setVisible(building);
            if (building) {
                const int left=(airport->getPlaneBuildRemaining()*GAMESPEED_DEFAULT+999)/1000;
                buildBar.setProgress(airport->getPlaneBuildPercent());
                buildBar.setText(std::string(_("Building airplane"))+" "+std::to_string(left/60)+":"+(left%60<10?"0":"")+std::to_string(left%60));
            }
            cityStats.update(airport);
            return DefaultStructureInterface::update();
        }
        buyButton.setVisible(false);
        buildBar.setVisible(false);
        const int seconds=(airport->getSpawnTimer()*GAMESPEED_DEFAULT+999)/1000;
        std::string status;
        if (seconds>0) status=std::to_string(seconds/60)+":"+(seconds%60<10?"0":"")+std::to_string(seconds%60);
        else if (!airport->getOwner()->hasPower()) status=_("Needs power");
        else if (airport->getOwner()->isUnitLimitReached(Unit_Ornithopter)) status=_("Unit limit reached");
        else status=_("Ready");
        patrolLabel.setText(_("Ornithopter patrol")+std::string("\n")
            +std::to_string(airport->getPendingAircraft())+_(" aircraft")+"\n"+status);
        cityStats.update(airport);
        return DefaultStructureInterface::update();
    }
private:
    VBox details;
    Label patrolLabel;
    TextButton buyButton;
    TextProgressBar buildBar;
    CityStatsBox cityStats;
};
#endif
