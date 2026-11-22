#ifndef AIRPORTINTERFACE_H
#define AIRPORTINTERFACE_H
#include "DefaultStructureInterface.h"
#include "CityStatsBox.h"
#include <GUI/Label.h>
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
        cityStats.attachTo(details,COLOR_WHITE,false);
        details.addWidget(Spacer::create(),0.99);
    }
    bool update() override {
        auto* airport=dynamic_cast<Airport*>(currentGame->getObjectManager().getObject(objectID));
        if (!airport) return false;
        if (!currentGame->getGameInitSettings().getGameOptions().airportOrnithopterPatrol) {
            // No Ornithopter patrol (the default): the panel says what the Airport does launch.
            const House* owner=airport->getOwner();
            patrolLabel.setText(_("City airplane")+std::string("\n")
                +(!owner->hasPower() ? _("Needs power")
                  : owner->getNumItems(Unit_AmbientAirplane)>0 ? _("In flight") : _("Next flight soon")));
            cityStats.update(airport);
            return DefaultStructureInterface::update();
        }
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
    CityStatsBox cityStats;
};
#endif
