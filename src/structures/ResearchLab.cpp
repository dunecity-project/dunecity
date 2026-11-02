#include <structures/ResearchLab.h>

#include <globals.h>
#include <House.h>
#include <FileClasses/GFXManager.h>
#include <GUI/ObjectInterfaces/DefaultObjectInterface.h>

ResearchLab::ResearchLab(House* newOwner) : StructureBase(newOwner) {
    init();
    setHealth(getMaxHealth());
}

ResearchLab::ResearchLab(InputStream& stream) : StructureBase(stream) {
    init();
}

void ResearchLab::init() {
    itemID = Structure_ResearchLab;
    owner->incrementStructures(itemID);
    structureSize = Coord(3, 2);
    graphicID = ObjPic_TechCenter;
    graphic = pGFXManager->getObjPic(graphicID, owner->getHouseID());
    numImagesX = 4;
    numImagesY = 1;
    firstAnimFrame = 2;
    lastAnimFrame = 3;
    curAnimFrame = 2;
    lastVisibleFrame = 2;
}

ObjectInterface* ResearchLab::getInterfaceContainer() {
    return DefaultObjectInterface::create(objectID);
}
