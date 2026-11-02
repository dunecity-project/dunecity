#include <structures/ResearchLab.h>

#include <globals.h>
#include <House.h>
#include <Game.h>
#include <Command.h>
#include <GUI/ObjectInterfaces/ResearchLabInterface.h>
#include <FileClasses/GFXManager.h>
#include <GUI/ObjectInterfaces/DefaultObjectInterface.h>

ResearchLab::ResearchLab(House* newOwner) : StructureBase(newOwner) {
    init();
    setHealth(getMaxHealth());
}

ResearchLab::ResearchLab(InputStream& stream) : StructureBase(stream) {
    init();
}

ResearchLab::~ResearchLab() {
    owner->cancelTechResearch(objectID);
}

void ResearchLab::handleResearchClick() {
    if(currentGame && pLocalPlayer && currentGame->canControlHouse(owner))
        currentGame->getCommandManager().addCommand(
            Command(pLocalPlayer->getPlayerID(), CMD_RESEARCH_TECH_LEVEL, objectID));
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
    return mayShowOwnerInterface() ? ResearchLabInterface::create(objectID)
        : DefaultObjectInterface::create(objectID);
}
