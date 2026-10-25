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

#ifndef OBJECTMANAGER_H
#define OBJECTMANAGER_H

#include <misc/InputStream.h>
#include <misc/OutputStream.h>
#include <misc/SDL2pp.h>

#include <algorithm>
#include <cstddef>
#include <map>
#include <vector>

// forward declarations
class ObjectBase;

typedef std::map<Uint32,ObjectBase*> ObjectMap;

/// This class holds all objects (structures and units) in the game.
class ObjectManager{
public:
    /**
        Default constructor
    */
    ObjectManager() : nextFreeObjectID(1)
    {
    }

    /**
        Default destructor
    */
    ~ObjectManager() { ; }


    /**
        Saves all objects to a stream
        \param  stream  Stream to save to
    */
    void save(OutputStream& stream) const;

    /**
        Loads all objects from a stream
        \param  stream  Stream to load from
    */
    void load(InputStream& stream);

    /**
        This method adds one object. The ObjectID is choosen automatically.
        \param  pObject A pointer to the object.
        \return ObjectID of the added object.
    */
    Uint32 addObject(ObjectBase* pObject);

    /**
        This method searches for the object with ObjectID.
        \param  ObjectID        ID of the object to search for
        \return Pointer to this object (nullptr if not found)
    */
    inline ObjectBase* getObject(Uint32 objectID) const {
        // The dense table mirrors objectMap for every id below kDenseIDs (see setDense).
        if(objectID < kDenseIDs) {
            return objectID < byID.size() ? byID[objectID] : nullptr;
        }
        if(largeIDs == 0) {
            return nullptr;
        }
        ObjectMap::const_iterator iter = objectMap.find(objectID);

        if(iter == objectMap.end()) {
            return nullptr;
        } else {
            return iter->second;
        }
    }

    /**
        This method removes one object.
        \param  ObjectID        ID of the object to remove
        \return false if there was no object with this ObjectID, true if it could be removed
    */
    bool removeObject(Uint32 objectID) {
        if(objectMap.erase(objectID) == 0) {
            return false;
        }
        setDense(objectID, nullptr);
        return true;
    }

    /**
        Visits every object in ascending object-id order.

        The order is a property of the container, not an accident: the deterministic state digest
        depends on visiting the same objects in the same order on every peer.
        \param  visitor     called with (objectID, pObject) for every live object
    */
    template<typename Visitor>
    void forEachObject(Visitor&& visitor) const {
        for(const auto& entry : objectMap) {
            visitor(entry.first, entry.second);
        }
    }

    /// Number of live objects.
    std::size_t getObjectCount() const { return objectMap.size(); }

private:
    /// Ids below this are also held in byID, indexed by id, so getObject() is one array read
    /// instead of a tree search. Ids are handed out in sequence; a (corrupt) save with larger
    /// ids still works through objectMap alone.
    static constexpr Uint32 kDenseIDs = 1u << 22;
    void setDense(Uint32 objectID, ObjectBase* pObject) {
        if(objectID >= kDenseIDs) {
            if(pObject) ++largeIDs; else --largeIDs;
            return;
        }
        if(objectID >= byID.size()) {
            if(!pObject) return;
            byID.resize(std::min<size_t>(kDenseIDs, std::max<size_t>(objectID + 1, byID.size() * 2)), nullptr);
        }
        byID[objectID] = pObject;
    }

    Uint32 nextFreeObjectID;
    ObjectMap objectMap;
    std::vector<ObjectBase*> byID;
    size_t largeIDs = 0;
};

#endif //OBJECTMANAGER_H
