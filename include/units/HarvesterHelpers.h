#ifndef HARVESTERHELPERS_H
#define HARVESTERHELPERS_H

#include <ObjectBase.h>
#include <data.h>
#include <fixmath/FixPoint.h>
#include <mod/ModManager.h>
#include <units/Harvester.h>
#include <units/RebelHarvester.h>

inline bool isHarvesterLikeUnit(int itemID) {
    return itemID == Unit_Harvester || itemID == Unit_RebelHarvester
        || ((itemID == Unit_FastHarvester || itemID == Unit_CapacityHarvester)
            && ModManager::instance().isTornieFamilyContentActive());
}

inline bool isHarvesterLikeObject(const ObjectBase* object) {
    return object != nullptr && isHarvesterLikeUnit(object->getItemID());
}

inline bool harvesterIsReturning(const ObjectBase* object) {
    if(!isHarvesterLikeObject(object)) return false;

    switch(object->getItemID()) {
        case Unit_FastHarvester:
        case Unit_CapacityHarvester:
        case Unit_Harvester:      return static_cast<const Harvester*>(object)->isReturning();
        case Unit_RebelHarvester: return static_cast<const RebelHarvester*>(object)->isReturning();
        default:                  return false;
    }
}

inline bool harvesterIsHarvesting(const ObjectBase* object) {
    if(!isHarvesterLikeObject(object)) return false;

    switch(object->getItemID()) {
        case Unit_FastHarvester:
        case Unit_CapacityHarvester:
        case Unit_Harvester:      return static_cast<const Harvester*>(object)->isHarvesting();
        case Unit_RebelHarvester: return static_cast<const RebelHarvester*>(object)->isHarvesting();
        default:                  return false;
    }
}

inline FixPoint harvesterGetAmountOfSpice(const ObjectBase* object) {
    if(!isHarvesterLikeObject(object)) return 0;

    switch(object->getItemID()) {
        case Unit_FastHarvester:
        case Unit_CapacityHarvester:
        case Unit_Harvester:      return static_cast<const Harvester*>(object)->getAmountOfSpice();
        case Unit_RebelHarvester: return static_cast<const RebelHarvester*>(object)->getAmountOfSpice();
        default:                  return 0;
    }
}

inline int harvesterGetSpiceCapacity(const ObjectBase* object) {
    if(!isHarvesterLikeObject(object)) return HARVESTERMAXSPICE;
    switch(object->getItemID()) {
        case Unit_FastHarvester:
        case Unit_CapacityHarvester:
        case Unit_Harvester: return static_cast<const Harvester*>(object)->getSpiceCapacity();
        default: return HARVESTERMAXSPICE;
    }
}

inline void harvesterSetAmountOfSpice(ObjectBase* object, FixPoint spice) {
    if(!isHarvesterLikeObject(object)) return;

    switch(object->getItemID()) {
        case Unit_FastHarvester:
        case Unit_CapacityHarvester:
        case Unit_Harvester:      static_cast<Harvester*>(object)->setAmountOfSpice(spice); break;
        case Unit_RebelHarvester: static_cast<RebelHarvester*>(object)->setAmountOfSpice(spice); break;
        default:                  break;
    }
}

inline FixPoint harvesterExtractSpice(ObjectBase* object, FixPoint extractionSpeed) {
    if(!isHarvesterLikeObject(object)) return 0;

    switch(object->getItemID()) {
        case Unit_FastHarvester:
        case Unit_CapacityHarvester:
        case Unit_Harvester:      return static_cast<Harvester*>(object)->extractSpice(extractionSpeed);
        case Unit_RebelHarvester: return static_cast<RebelHarvester*>(object)->extractSpice(extractionSpeed);
        default:                  return 0;
    }
}

inline void harvesterDoReturn(ObjectBase* object) {
    if(!isHarvesterLikeObject(object)) return;

    switch(object->getItemID()) {
        case Unit_FastHarvester:
        case Unit_CapacityHarvester:
        case Unit_Harvester:      static_cast<Harvester*>(object)->doReturn(); break;
        case Unit_RebelHarvester: static_cast<RebelHarvester*>(object)->doReturn(); break;
        default:                  break;
    }
}

inline void harvesterHandleReturnClick(ObjectBase* object) {
    if(!isHarvesterLikeObject(object)) return;

    switch(object->getItemID()) {
        case Unit_FastHarvester:
        case Unit_CapacityHarvester:
        case Unit_Harvester:      static_cast<Harvester*>(object)->handleReturnClick(); break;
        case Unit_RebelHarvester: static_cast<RebelHarvester*>(object)->handleReturnClick(); break;
        default:                  break;
    }
}

inline void harvesterSetReturned(ObjectBase* object) {
    if(!isHarvesterLikeObject(object)) return;

    switch(object->getItemID()) {
        case Unit_FastHarvester:
        case Unit_CapacityHarvester:
        case Unit_Harvester:      static_cast<Harvester*>(object)->setReturned(); break;
        case Unit_RebelHarvester: static_cast<RebelHarvester*>(object)->setReturned(); break;
        default:                  break;
    }
}

#endif // HARVESTERHELPERS_H
