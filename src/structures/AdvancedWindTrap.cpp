#include <structures/AdvancedWindTrap.h>
#include <dunecity/PowerRules.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>

#include <GUI/ObjectInterfaces/WindTrapInterface.h>

AdvancedWindTrap::AdvancedWindTrap(House* newOwner, Uint32 newItemID) : StructureBase(newOwner) {
    AdvancedWindTrap::init(newItemID);

    setHealth(getMaxHealth());
}

AdvancedWindTrap::AdvancedWindTrap(InputStream& stream, Uint32 newItemID) : StructureBase(stream) {
    AdvancedWindTrap::init(newItemID);
}

void AdvancedWindTrap::init(Uint32 newItemID) {
    itemID = newItemID;
    owner->incrementStructures(itemID);

    switch(itemID) {
        case Structure_AdvancedWindTrapMK2:
            structureSize.x = 2;
            structureSize.y = 3;
            graphicID = ObjPic_AdvancedWindTrap2x3;
            break;

        case Structure_AdvancedWindTrapMK3:
            structureSize.x = 3;
            structureSize.y = 2;
            graphicID = ObjPic_AdvancedWindTrap3x2;
            break;

        case Structure_AdvancedWindTrap:
        default:
            itemID = Structure_AdvancedWindTrap;
            structureSize.x = 3;
            structureSize.y = 3;
    graphicID = ObjPic_AdvancedWindTrap;
            break;
    }

    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    // Use the same generated energy-animation layout as the vanilla Windtrap.
    numImagesX = NUM_WINDTRAP_ANIMATIONS_PER_ROW;
    numImagesY = (2 + NUM_WINDTRAP_ANIMATIONS + NUM_WINDTRAP_ANIMATIONS_PER_ROW - 1)
        / NUM_WINDTRAP_ANIMATIONS_PER_ROW;
    firstAnimFrame = 2;
    lastAnimFrame = 2 + NUM_WINDTRAP_ANIMATIONS - 1;
    curAnimFrame = firstAnimFrame;
    lastVisibleFrame = firstAnimFrame;
}

AdvancedWindTrap::~AdvancedWindTrap() {
    // Rejected placement and demolition must also release generated power.
    setHealth(0);
}

ObjectInterface* AdvancedWindTrap::getInterfaceContainer() {
    if(mayShowOwnerInterface()) {
        return WindTrapInterface::create(objectID);
    }

    return DefaultObjectInterface::create(objectID);
}

bool AdvancedWindTrap::update() {
    bool bResult = StructureBase::update();

    if(bResult) {
        if(justPlacedTimer <= 0 || curAnimFrame != 0) {
            curAnimFrame = 2 + ((currentGame->getGameCycleCount() / 8) % NUM_WINDTRAP_ANIMATIONS);
        }

        auto* citySim = currentGame->getCitySimulation();
        if (citySim) {
            citySim->registerPowerSource(location.x, location.y, getProducedPower());
        }
    }

    return bResult;
}

void AdvancedWindTrap::setHealth(FixPoint newHealth) {
    int producedPowerBefore = getProducedPower();
    StructureBase::setHealth(newHealth);
    int producedPowerAfterwards = getProducedPower();

    owner->setProducedPower(owner->getProducedPower() - producedPowerBefore + producedPowerAfterwards);
}

int AdvancedWindTrap::getProducedPower() const {
    const int nominal = abs(currentGame->objectData.data[itemID][originalHouseID].power);
    return DuneCity::windtrapOutput(nominal, getHealth(), getMaxHealth());
}
