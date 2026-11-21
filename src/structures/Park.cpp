#include <structures/Park.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <dunecity/CitySpritePolicy.h>

Park::Park(House* newOwner) : StructureBase(newOwner) {
    Park::init();
    setHealth(getMaxHealth());
}

Park::Park(InputStream& stream) : StructureBase(stream) {
    Park::init();
}

void Park::init() {
    itemID = Structure_Park;
    owner->incrementStructures(itemID);

    structureSize.x = 1;
    structureSize.y = 1;
    graphicID = ObjPic_Park;
    graphic   = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    numImagesX = DuneCity::CitySprites::parkFrames;
    numImagesY = 1;
    firstAnimFrame = 0;
    lastAnimFrame  = 0;
    curAnimFrame   = 0;
}

Park::~Park() = default;

void Park::updateStructureSpecificStuff() {
    // Visual only: the variant follows the site, the fountain the simulation clock.
    firstAnimFrame = lastAnimFrame = curAnimFrame =
        DuneCity::CitySprites::parkFrame(currentGame->getGameCycleCount(), location.x, location.y);
}
