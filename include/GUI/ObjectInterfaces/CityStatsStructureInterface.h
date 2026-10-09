#ifndef CITYSTATSSTRUCTUREINTERFACE_H
#define CITYSTATSSTRUCTUREINTERFACE_H

#include "DefaultStructureInterface.h"
#include "CityStatsBox.h"

#include <Game.h>

#include <GUI/Spacer.h>
#include <GUI/VBox.h>

/**
 * Default interface for non-builder structures when city sim is active.
 *
 * Wraps DefaultStructureInterface (keeps the repair button) and adds the
 * standard CityStatsBox so the player can see the structure's city role,
 * land value, pollution, and crime in the right-side panel.
 *
 * Used for structures with no other custom interface — Wall, Gun/Rocket
 * Turret, IX, Nuclear Plant, etc. Builder structures keep BuilderInterface
 * (with no city stats), as agreed in the spec.
 */
class CityStatsStructureInterface : public DefaultStructureInterface {
public:
    static CityStatsStructureInterface* create(int objectID) {
        CityStatsStructureInterface* tmp = new CityStatsStructureInterface(objectID);
        tmp->pAllocated = true;
        return tmp;
    }

protected:
    explicit CityStatsStructureInterface(int objectID) : DefaultStructureInterface(objectID) {
        Uint32 color = ownerAccentColor();
        mainHBox.addWidget(&textVBox);
        cityStats_.attachTo(textVBox, color);
        textVBox.addWidget(Spacer::create(), 0.99);
    }

    bool update() override {
        ObjectBase* pObject = currentGame->getObjectManager().getObject(objectID);
        if (pObject == nullptr) {
            return false;
        }
        cityStats_.update(dynamic_cast<StructureBase*>(pObject));
        return DefaultStructureInterface::update();
    }

private:
    VBox         textVBox;
    CityStatsBox cityStats_;
};

#endif // CITYSTATSSTRUCTUREINTERFACE_H
