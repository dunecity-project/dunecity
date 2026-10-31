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

#ifndef OBJECTDATA_H
#define OBJECTDATA_H

#include <string>

#include <misc/InputStream.h>
#include <misc/OutputStream.h>
#include <fixmath/FixPoint.h>
#include <data.h>
#include <DataTypes.h>

#include <bitset>

class INIFile;

/// This class stores all the unit/structure data
class ObjectData {
public:
    /**
        Default constructur. Initializes everything with zero
    */
    ObjectData();

    /**
        Destructor.
    */
    virtual ~ObjectData();

    /**
        This method loads all data from an INI-File specified by filename. There should be two sections
        in the INI-File for default values: "default structure" and "default unit". The other sections are
        for exactly one unit.
        Example:<br>
            [structure name]<br>
            Enabled = true<br>
            HitPoints = 500<br>
            Price = 300<br>
            Power = 10<br>
            ViewRange = 3<br>
            Capacity = 1000<br>
            BuildTime = 80<br>
            InfSpawnProp = 45<br>
            <br>
            [unit name]
            Enabled = true<br>
            HitPoints = 100<br>
            Price = 100<br>
            ViewRange = 5<br>
            WeaponDamage = 10<br>
            WeaponRange = 4<br>
            WeaponReloadTime = 100<br>
            Speed = 3.0<br>
            TurnSpeed = 0.25<br>
            BuildTime = 56<br>
            InfSpawnProp = 45<br>
        \param filename the INI-File to load.
    */
    void loadFromINIFile(const std::string& filename, bool preferUserConfig = true, bool applyContentPolicy = true);

    /**
        Saves all stored data out into a binary stream.
        For SAVEGAMEVERSION >= 9811, writes an item count before the table
        so future item additions are self-describing.
        \param stream   the stream to save to
        \see load
    */
    void save(OutputStream& stream) const;

    /**
        Loads all stored data from a stream.
        \param stream       the stream to load from
        \param savedItemCount  number of ItemID rows stored in the save.
                               For SAVEGAMEVERSION >= 9811 this is read from
                               the stream (pass 0 to auto-read).
                               For older saves, caller must supply the correct
                               count (e.g. LEGACY_NUM_ITEM_ID_9810 for v1.0.7).
        \see save
    */
    void load(InputStream& stream, int savedItemCount = 0, int savedHouseCount = NUM_HOUSES);

    /**
        Logs all loaded object data to SDL log for debugging.
    */
    void logSettings() const;

    /**
        Returns a hash of all object data for multiplayer verification.
        This hashes the in-memory loaded values, not the raw file text.
        \return 16-character hex string (FNV-1a hash)
    */
    std::string getEffectiveHash() const;

    struct ObjectDataStruct {
        bool     enabled;                                             ///< is this unit/structure available?
        Sint32   hitpoints;                                           ///< what is the maximum health of this unit/structure?
        Sint32   price;                                               ///< how much does this structure cost?
        Sint32   power;                                               ///< how much power do this structure require. Wind traps have negative values because they produce power?
        Sint32   viewrange;                                           ///< much terrain is revealed when this structure is placed or this unit moves?
        Sint32   capacity;                                            ///< how much spice can this structure contain?
        Sint32   weapondamage;                                        ///< how much damage does the weapon of this unit/structure have?
        Sint32   weaponrange;                                         ///< how far can this unit/structure shoot?
        Sint32   weaponreloadtime;                                    ///< how many frames does it take to reload the weapon?
        FixPoint maxspeed;                                            ///< how fast can this unit move?
        FixPoint turnspeed;                                           ///< how fast can this unit turn around?
        Sint32   buildtime;                                           ///< how much time does the production of this structure/unit take?
        Sint32   infspawnprop;                                        ///< what is the probability (in percent) that a infantry soldier is spawn on destruction?
        int      builder;                                             ///< In which building can this item be built
        std::bitset<Structure_LastID + 1>   prerequisiteStructuresSet;    ///< What buildings are prerequisite for building this item
        Sint8    techLevel;                                           ///< What techLevel is needed to build this item (in campaign mode this equals to mission number)
        Sint8    upgradeLevel;                                        ///< How many upgrades must the builder already have made
    };

    ObjectDataStruct data[Num_ItemID][NUM_HOUSES];      ///< here is all the data stored. It is public for easy and fast access. Use only read-only.

    // Map settings
    int harvesterLimitSmallMap;      ///< Max harvesters for small maps (32x32 or smaller)
    int harvesterLimitMediumMap;     ///< Max harvesters for medium maps (64x64 or smaller)
    int harvesterLimitLargeMap;      ///< Max harvesters for large maps (between 64x64 and 128x128)
    int harvesterLimitHugeMap;       ///< Max harvesters for huge maps (128x128 or larger)
    
    int unitLimitSmallMap;           ///< Max units per house for small maps (32x32 or smaller)
    int unitLimitMediumMap;          ///< Max units per house for medium maps (64x64 or smaller)
    int unitLimitLargeMap;           ///< Max units per house for large maps (between 64x64 and 128x128)
    int unitLimitHugeMap;            ///< Max units per house for huge maps (128x128 or larger)

private:

    int loadIntValue(const INIFile& objectDataFile, const std::string& section, const std::string& key, char houseChar, int defaultValue = 0);
    bool loadBoolValue(const INIFile& objectDataFile, const std::string& section, const std::string& key, char houseChar, bool defaultValue = false);
    FixPoint loadFixPointValue(const INIFile& objectDataFile, const std::string& section, const std::string& key, char houseChar, FixPoint defaultValue = 0);
    std::string loadStringValue(const INIFile& objectDataFile, const std::string& section, const std::string& key, char houseChar, const std::string& defaultValue = "");
    int loadItemID(const INIFile& objectDataFile, const std::string& section, const std::string& key, char houseChar, int defaultValue = ItemID_Invalid);
    std::bitset<Structure_LastID + 1> loadPrerequisiteStructuresSet(const INIFile& objectDataFile, const std::string& section, const std::string& key, char houseChar, std::bitset<Structure_LastID + 1> defaultValue = std::bitset<Structure_LastID + 1>());
    void applyNeutralVanillaRules();
};

#endif // OBJECTDATA_H
