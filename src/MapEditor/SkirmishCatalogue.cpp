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

#include <MapEditor/SkirmishGenerator.h>

#include <ObjectData.h>
#include <dunecity/CityConstants.h>
#include <globals.h>
#include <sand.h>

namespace SkirmishGenerator {

Catalogue makeCatalogue(const ObjectData& data, bool cityMode) {
    Catalogue catalogue;
    for(int house = 0; house < NUM_HOUSES; ++house) {
        catalogue.houseNames[house] = getHouseNameByNumber(static_cast<HOUSETYPE>(house));
        if(isCustomGameHouseAvailable(static_cast<HOUSETYPE>(house))) catalogue.houses.push_back(house);
    }
    for(int item = ItemID_Invalid + 1; item < Num_ItemID; ++item) {
        if(!isStructure(item) && !isUnit(item)) continue;
        catalogue.itemNames[item] = getItemNameByID(item);
        catalogue.sizes[item] = isStructure(item) ? getStructureSize(item) : Coord(1, 1);
        for(int house = 0; house < NUM_HOUSES; ++house) {
            const ObjectData::ObjectDataStruct& source = data.data[item][house];
            ItemRule& rule = catalogue.rules[item][house];
            // The match disables city-only buildings outside city mode (Game::initGame).
            rule.enabled = source.enabled && (cityMode || !DuneCity::isCityOnlyStructure(item));
            rule.builder = source.builder;
            rule.price = source.price;
            rule.power = source.power;
            rule.techLevel = source.techLevel;
            for(int prerequisite = Structure_FirstID; prerequisite <= Structure_LastID; ++prerequisite)
                if(source.prerequisiteStructuresSet[prerequisite]) rule.prerequisites.push_back(prerequisite);
        }
    }
    return catalogue;
}

}
