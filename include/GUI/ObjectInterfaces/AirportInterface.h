#ifndef AIRPORTINTERFACE_H
#define AIRPORTINTERFACE_H
#include "DefaultStructureInterface.h"
#include "CityStatsBox.h"
#include <GUI/Label.h>
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
        // City Plane Purchase: launch a plane now for CityAircraft::kPlanePrice credits.
        buyButton.setText(_("Buy airplane") + std::string(" ") + std::to_string(DuneCity::CityAircraft::kPlanePrice));
        buyButton.setTooltipText(_("Launch a city airplane now. Up to three in the air; one a minute per Airport."));
        buyButton.setOnClick([id] {
            auto* airport = dynamic_cast<Airport*>(currentGame->getObjectManager().getObject(id));
            if (airport) airport->handleBuyAirplaneClick();
        });
        buyButton.setVisible(false);
        details.addWidget(&buyButton, (Sint32)24);
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
            const int wait=(airport->getPurchaseCooldown()*GAMESPEED_DEFAULT+999)/1000;
            patrolLabel.setText(_("City airplane")+std::string("\n")
                +(!owner->hasPower() ? _("Needs power")
                  : flying>0 ? std::to_string(flying)+_(" in flight") : _("Next flight soon"))
                +(purchase && wait>0 ? std::string("\n")+_("Next purchase ")+std::to_string(wait/60)+":"
                    +(wait%60<10?"0":"")+std::to_string(wait%60) : std::string()));
            buyButton.setVisible(purchase);
            buyButton.setEnabled(purchase && currentGame->canControlHouse(owner) && airport->canBuyAirplane());
            cityStats.update(airport);
            return DefaultStructureInterface::update();
        }
        buyButton.setVisible(false);
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
    CityStatsBox cityStats;
};
#endif
