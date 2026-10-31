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

#include "ObjectData.h"

#include <globals.h>

#include <FileClasses/FileManager.h>
#include <FileClasses/INIFile.h>
#include <sand.h>

#include <misc/string_util.h>
#include <misc/FileSystem.h>
#include <main.h>
#include <mod/ModManager.h>

#include <cstdint>
#include <cstdio>

ObjectData::ObjectData()
{
    // set default values
    for(int i=0;i<Num_ItemID;i++) {
        for(int h=0;h<NUM_HOUSES;h++) {
            data[i][h].enabled = false;
            data[i][h].hitpoints = 0;
            data[i][h].price = 0;
            data[i][h].power = 0;
            data[i][h].viewrange = 0;
            data[i][h].capacity = 0;
            data[i][h].weapondamage = 0;
            data[i][h].weaponrange = 0;
            data[i][h].weaponreloadtime = 0;
            data[i][h].maxspeed = 0;
            data[i][h].turnspeed = 0;
            data[i][h].buildtime = 0;
            data[i][h].infspawnprop = 0;
            data[i][h].builder = ItemID_Invalid;
            data[i][h].prerequisiteStructuresSet.reset();
            data[i][h].techLevel = -1;
            data[i][h].upgradeLevel = 0;
        }
    }
    
    // Initialize map settings with defaults
    harvesterLimitSmallMap = 5;
    harvesterLimitMediumMap = 10;
    harvesterLimitLargeMap = 15;
    harvesterLimitHugeMap = 40;
    
    unitLimitSmallMap = 25;
    unitLimitMediumMap = 100;
    unitLimitLargeMap = 250;
    unitLimitHugeMap = 300;
}

ObjectData::~ObjectData() = default;

void ObjectData::logSettings() const {
    SDL_Log("==================== OBJECTDATA CONFIGURATION ====================");
    SDL_Log("Config path: %s", getObjectDataConfigFilepath().c_str());
    SDL_Log("%s", "");
    
    // Log a sample of key units/structures for each house to keep output manageable
    // Format: ItemName [House]: HP=X Price=X Damage=X Range=X BuildTime=X
    
    const int keyItems[32] = {
        // Units
        Unit_Soldier, Unit_Trooper, Unit_Harvester, Unit_MCV,
        Unit_Trike, Unit_RaiderTrike, Unit_Quad,
        Unit_Tank, Unit_SiegeTank, Unit_Launcher,
        Unit_Devastator, Unit_SonicTank, Unit_Deviator,
        Unit_Ornithopter, Unit_Carryall,
        // Structures  
        Structure_Slab1, Structure_Wall, Structure_ConstructionYard,
        Structure_WindTrap, Structure_Refinery, Structure_Barracks,
        Structure_WOR, Structure_LightFactory, Structure_HeavyFactory,
        Structure_HighTechFactory, Structure_IX, Structure_Palace,
        Structure_RepairYard, Structure_StarPort, Structure_Silo,
        Structure_GunTurret, Structure_RocketTurret
    };
    
    SDL_Log("=== KEY UNITS AND STRUCTURES (Atreides) ===");
    for(int i = 0; i < 32; i++) {
        int itemID = keyItems[i];
        if(itemID >= Num_ItemID) continue;
        const ObjectDataStruct& obj = data[itemID][HOUSE_ATREIDES];
        if(!obj.enabled) continue;
        
        SDL_Log("%s: HP=%d Price=%d Dmg=%d Rng=%d Build=%d ViewRng=%d",
            resolveItemName(itemID).c_str(),
            obj.hitpoints, obj.price, obj.weapondamage, 
            obj.weaponrange, obj.buildtime, obj.viewrange);
    }
    
    SDL_Log("%s", "");
    SDL_Log("=== HOUSE-SPECIFIC DIFFERENCES ===");
    // Show where houses differ from Atreides
    for(int i = 0; i < 32; i++) {
        int itemID = keyItems[i];
        if(itemID >= Num_ItemID) continue;
        const ObjectDataStruct& atreides = data[itemID][HOUSE_ATREIDES];
        if(!atreides.enabled) continue;
        
        bool hasDifference = false;
        for(int h = 1; h < NUM_HOUSES; h++) {
            const ObjectDataStruct& house = data[itemID][h];
            if(house.enabled != atreides.enabled ||
               house.hitpoints != atreides.hitpoints ||
               house.price != atreides.price ||
               house.weapondamage != atreides.weapondamage ||
               house.buildtime != atreides.buildtime) {
                if(!hasDifference) {
                    SDL_Log("%s differences:", resolveItemName(itemID).c_str());
                    hasDifference = true;
                }
                SDL_Log("  %s: HP=%d Price=%d Dmg=%d Build=%d",
                    getHouseNameByNumber(static_cast<HOUSETYPE>(h)).c_str(),
                    house.hitpoints, house.price, house.weapondamage, house.buildtime);
            }
        }
    }
    
    SDL_Log("===============================================================");
}

void ObjectData::loadFromINIFile(const std::string& filename, bool preferUserConfig, bool applyContentPolicy)
{
    // Try to load from user directory first, fall back to install directory
    std::string userConfigPath = getObjectDataConfigFilepath();
    INIFile* objectDataFile = nullptr;
    std::string loadedFrom;
    
    try {
        if (preferUserConfig && existsFile(userConfigPath)) {
            SDL_Log("Loading ObjectData.ini from user directory: %s", userConfigPath.c_str());
            objectDataFile = new INIFile(userConfigPath);
            loadedFrom = userConfigPath;
        } else if (existsFile(filename)) {
            SDL_Log("Loading ObjectData.ini from filesystem path: %s", filename.c_str());
            objectDataFile = new INIFile(filename);
            loadedFrom = filename;
        } else {
            SDL_Log("Loading ObjectData.ini from install directory: %s", filename.c_str());
            objectDataFile = new INIFile(pFileManager->openFile(filename).get());
            loadedFrom = filename + " (install directory)";
        }
    } catch (std::exception& e) {
        SDL_Log("Error loading ObjectData.ini: %s", e.what());
        if (objectDataFile) {
            delete objectDataFile;
            objectDataFile = nullptr;
        }
        SDL_Log("Trying install directory as fallback");
        if (existsFile(filename)) {
            objectDataFile = new INIFile(filename);
            loadedFrom = filename;
        } else {
            objectDataFile = new INIFile(pFileManager->openFile(filename).get());
            loadedFrom = filename + " (install directory)";
        }
    }
    
    if (!objectDataFile) {
        THROW(std::runtime_error, "Failed to load ObjectData.ini from any location!");
    }
    
    std::string loadedPath = loadedFrom;

    // Load map settings
    if (objectDataFile->hasSection("Map Settings")) {
        harvesterLimitSmallMap = objectDataFile->getIntValue("Map Settings", "HarvesterLimitSmallMap", 5);
        harvesterLimitMediumMap = objectDataFile->getIntValue("Map Settings", "HarvesterLimitMediumMap", 10);
        harvesterLimitLargeMap = objectDataFile->getIntValue("Map Settings", "HarvesterLimitLargeMap", 15);
        harvesterLimitHugeMap = objectDataFile->getIntValue("Map Settings", "HarvesterLimitHugeMap", 40);
        SDL_Log("[ObjectData] Harvester limits: Small=%d, Medium=%d, Large=%d, Huge=%d", 
                harvesterLimitSmallMap, harvesterLimitMediumMap, harvesterLimitLargeMap, harvesterLimitHugeMap);
        
        unitLimitSmallMap = objectDataFile->getIntValue("Map Settings", "UnitLimitSmallMap", 25);
        unitLimitMediumMap = objectDataFile->getIntValue("Map Settings", "UnitLimitMediumMap", 100);
        unitLimitLargeMap = objectDataFile->getIntValue("Map Settings", "UnitLimitLargeMap", 250);
        unitLimitHugeMap = objectDataFile->getIntValue("Map Settings", "UnitLimitHugeMap", 300);
        SDL_Log("[ObjectData] Unit limits: Small=%d, Medium=%d, Large=%d, Huge=%d", 
                unitLimitSmallMap, unitLimitMediumMap, unitLimitLargeMap, unitLimitHugeMap);
    }

    // load default structure values
    ObjectDataStruct structureDefaultData[NUM_HOUSES];
    for(int h=0;h<NUM_HOUSES;h++) {
        structureDefaultData[h].enabled = loadBoolValue(*objectDataFile, "default structure", "Enabled", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        structureDefaultData[h].hitpoints = loadIntValue(*objectDataFile, "default structure", "HitPoints", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        structureDefaultData[h].price = loadIntValue(*objectDataFile, "default structure", "Price", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        structureDefaultData[h].power = loadIntValue(*objectDataFile, "default structure", "Power", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        structureDefaultData[h].viewrange = loadIntValue(*objectDataFile, "default structure", "ViewRange", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        structureDefaultData[h].capacity = loadIntValue(*objectDataFile, "default structure", "Capacity", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        structureDefaultData[h].weapondamage = loadIntValue(*objectDataFile, "default structure", "WeaponDamage", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        structureDefaultData[h].weaponrange = loadIntValue(*objectDataFile, "default structure", "WeaponRange", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        structureDefaultData[h].weaponreloadtime = loadIntValue(*objectDataFile, "default structure", "WeaponReloadTime", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        structureDefaultData[h].maxspeed = loadFixPointValue(*objectDataFile, "default structure", "MaxSpeed", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        structureDefaultData[h].turnspeed = loadFixPointValue(*objectDataFile, "default structure", "TurnSpeed", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        structureDefaultData[h].buildtime = loadIntValue(*objectDataFile, "default structure", "BuildTime", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        structureDefaultData[h].infspawnprop = loadIntValue(*objectDataFile, "default structure", "InfSpawnProp", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        structureDefaultData[h].builder = loadItemID(*objectDataFile, "default structure", "Builder", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        structureDefaultData[h].prerequisiteStructuresSet = loadPrerequisiteStructuresSet(*objectDataFile, "default structure", "Prerequisite", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        structureDefaultData[h].techLevel = loadIntValue(*objectDataFile, "default structure", "TechLevel", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), -1);
        structureDefaultData[h].upgradeLevel = loadIntValue(*objectDataFile, "default structure", "UpgradeLevel", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
    }

    // load default unit values
    ObjectDataStruct unitDefaultData[NUM_HOUSES];
    for(int h=0;h<NUM_HOUSES;h++) {
        unitDefaultData[h].enabled = loadBoolValue(*objectDataFile, "default unit", "Enabled", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        unitDefaultData[h].hitpoints = loadIntValue(*objectDataFile, "default unit", "HitPoints", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        unitDefaultData[h].price = loadIntValue(*objectDataFile, "default unit", "Price", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        unitDefaultData[h].power = loadIntValue(*objectDataFile, "default unit", "Power", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        unitDefaultData[h].viewrange = loadIntValue(*objectDataFile, "default unit", "ViewRange", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        unitDefaultData[h].capacity = loadIntValue(*objectDataFile, "default unit", "Capacity", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        unitDefaultData[h].weapondamage = loadIntValue(*objectDataFile, "default unit", "WeaponDamage", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        unitDefaultData[h].weaponrange = loadIntValue(*objectDataFile, "default unit", "WeaponRange", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        unitDefaultData[h].weaponreloadtime = loadIntValue(*objectDataFile, "default unit", "WeaponReloadTime", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        unitDefaultData[h].maxspeed = loadFixPointValue(*objectDataFile, "default unit", "MaxSpeed", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        unitDefaultData[h].turnspeed = loadFixPointValue(*objectDataFile, "default unit", "TurnSpeed", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        unitDefaultData[h].buildtime = loadIntValue(*objectDataFile, "default unit", "BuildTime", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        unitDefaultData[h].infspawnprop = loadIntValue(*objectDataFile, "default unit", "InfSpawnProp", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        unitDefaultData[h].builder = loadItemID(*objectDataFile, "default structure", "Builder", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        unitDefaultData[h].prerequisiteStructuresSet = loadPrerequisiteStructuresSet(*objectDataFile, "default unit", "Prerequisite", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
        unitDefaultData[h].techLevel = loadIntValue(*objectDataFile, "default unit", "TechLevel", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), -1);
        unitDefaultData[h].upgradeLevel = loadIntValue(*objectDataFile, "default unit", "UpgradeLevel", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)));
    }

    // set default values
    for(int i=0;i<Num_ItemID;i++) {
        for(int h=0;h<NUM_HOUSES;h++) {
            if(isStructure(i)) {
                data[i][h] = structureDefaultData[h];
            } else {
                data[i][h] = unitDefaultData[h];
            }
        }
    }

    for(INIFile::Section& section : *objectDataFile) {
        const std::string& sectionName = section.getSectionName();

        if(sectionName == "" || sectionName == "default structure" || sectionName == "default unit"
            || sectionName == "Map Settings") {
            continue;
        }

        Uint32 itemID = getItemIDByName(sectionName);

        if(itemID == ItemID_Invalid) {
            SDL_Log("ObjectData::ObjectData(): '%s' is no valid unit/structure name!", sectionName.c_str());
            continue;
        }

        for(int h=0;h<NUM_HOUSES;h++) {

            ObjectDataStruct& defaultData = isStructure(itemID) ? structureDefaultData[h] : unitDefaultData[h];

            data[itemID][h].enabled = loadBoolValue(*objectDataFile, sectionName, "Enabled", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.enabled);
            data[itemID][h].hitpoints = loadIntValue(*objectDataFile, sectionName, "HitPoints", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.hitpoints);
            data[itemID][h].price = loadIntValue(*objectDataFile, sectionName, "Price", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.price);
            data[itemID][h].power = loadIntValue(*objectDataFile, sectionName, "Power", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.power);
            data[itemID][h].viewrange = loadIntValue(*objectDataFile, sectionName, "ViewRange", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.viewrange);
            data[itemID][h].capacity = loadIntValue(*objectDataFile, sectionName, "Capacity", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.capacity);
            data[itemID][h].weapondamage = loadIntValue(*objectDataFile, sectionName, "WeaponDamage", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.weapondamage);
            data[itemID][h].weaponrange = loadIntValue(*objectDataFile, sectionName, "WeaponRange", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.weaponrange);
            data[itemID][h].weaponreloadtime = loadIntValue(*objectDataFile, sectionName, "WeaponReloadTime", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.weaponreloadtime);
            data[itemID][h].maxspeed = loadFixPointValue(*objectDataFile, sectionName, "MaxSpeed", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.maxspeed);
            data[itemID][h].turnspeed = loadFixPointValue(*objectDataFile, sectionName, "TurnSpeed", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.turnspeed);
            data[itemID][h].buildtime = loadIntValue(*objectDataFile, sectionName, "BuildTime", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.buildtime);
            data[itemID][h].infspawnprop = loadIntValue(*objectDataFile, sectionName, "InfSpawnProp", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.infspawnprop);
            data[itemID][h].builder = loadItemID(*objectDataFile, sectionName, "Builder", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.builder);
            data[itemID][h].prerequisiteStructuresSet = loadPrerequisiteStructuresSet(*objectDataFile, sectionName, "Prerequisite", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.prerequisiteStructuresSet);
            data[itemID][h].techLevel = loadIntValue(*objectDataFile, sectionName, "TechLevel", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.techLevel);
            data[itemID][h].upgradeLevel = loadIntValue(*objectDataFile, sectionName, "UpgradeLevel", getHouseScenarioLetter(static_cast<HOUSETYPE>(h)), defaultData.upgradeLevel);
        }
    }

    const bool tornieFamily = ModManager::instance().isTornieFamilyContentActive();
    if(tornieFamily) {
        // Orders inherit the single unit's house blueprint, while keeping their own
        // availability and prerequisites. Five soldiers/troopers cost three singles.
        for(const auto orderID : {Unit_Troopers5, Unit_Infantry5}) {
            const auto singleID = orderID == Unit_Troopers5 ? Unit_Trooper : Unit_Soldier;
            for(int h = 0; h < NUM_HOUSES; ++h) {
                if(!data[orderID][h].enabled) continue;
                const auto order = data[orderID][h];
                data[orderID][h] = data[singleID][h];
                auto& squad = data[orderID][h];
                squad.enabled = order.enabled;
                squad.price *= 3;
                squad.builder = order.builder;
                squad.techLevel = order.techLevel;
                squad.upgradeLevel = order.upgradeLevel;
                squad.prerequisiteStructuresSet = order.prerequisiteStructuresSet;
            }
        }
    } else {
        // Old/default object tables can enable unspecified sections by default.
        // New IDs must never become products in unrelated mods.
        for(int item = Unit_Troopers5; item < Num_ItemID; ++item)
            for(auto& object : data[item]) object.enabled = false;
    }
    if(applyContentPolicy && tornieFamily
       && !ModManager::instance().isTornieCustomUnitsAndBuildingsEnabled())
        applyNeutralVanillaRules();
    
    // Clean up
    delete objectDataFile;
    
    SDL_Log("ObjectData loaded from: %s", loadedPath.c_str());
}

void ObjectData::applyNeutralVanillaRules() {
    ObjectData vanilla;
    vanilla.loadFromINIFile(getDuneLegacyDataDir() + "/config/ObjectData.ini.default", false, false);
    for(int item = 0; item < Num_ItemID; ++item) {
        if(isTornieExclusiveItem(item)) vanilla.data[item][HOUSE_NEUTRAL].enabled = false;
    }
    *this = vanilla;
    for(int item = 0; item < Num_ItemID; ++item)
        for(auto& houseData : data[item]) houseData = vanilla.data[item][HOUSE_NEUTRAL];
}

void ObjectData::save(OutputStream& stream) const
{
    // SAVEGAMEVERSION >= 9811: write item count so the loader is self-describing.
    stream.writeUint32(static_cast<Uint32>(Num_ItemID));

    for(int i=0;i<Num_ItemID;i++) {
        for(int h=0;h<NUM_HOUSES;h++) {
            stream.writeBool(data[i][h].enabled);
            stream.writeSint32(data[i][h].hitpoints);
            stream.writeSint32(data[i][h].price);
            stream.writeSint32(data[i][h].power);
            stream.writeSint32(data[i][h].viewrange);
            stream.writeSint32(data[i][h].capacity);
            stream.writeSint32(data[i][h].weapondamage);
            stream.writeSint32(data[i][h].weaponrange);
            stream.writeSint32(data[i][h].weaponreloadtime);
            stream.writeFixPoint(data[i][h].maxspeed);
            stream.writeFixPoint(data[i][h].turnspeed);
            stream.writeSint32(data[i][h].buildtime);
            stream.writeSint32(data[i][h].infspawnprop);
            stream.writeSint32(data[i][h].builder);
            stream.writeUint32((Uint32) data[i][h].prerequisiteStructuresSet.to_ulong());
            stream.writeSint8(data[i][h].techLevel);
            stream.writeSint8(data[i][h].upgradeLevel);
        }
    }

    // Save map settings
    stream.writeSint32(harvesterLimitSmallMap);
    stream.writeSint32(harvesterLimitMediumMap);
    stream.writeSint32(harvesterLimitLargeMap);
    stream.writeSint32(harvesterLimitHugeMap);
    stream.writeSint32(unitLimitSmallMap);
    stream.writeSint32(unitLimitMediumMap);
    stream.writeSint32(unitLimitLargeMap);
    stream.writeSint32(unitLimitHugeMap);
}

void ObjectData::load(InputStream& stream, int savedItemCount, int savedHouseCount)
{
    savedHouseCount = std::max(1, std::min(savedHouseCount, static_cast<int>(NUM_HOUSES)));

    // For SAVEGAMEVERSION >= 9811, savedItemCount should be 0 (auto-read).
    if(savedItemCount == 0) {
        savedItemCount = static_cast<int>(stream.readUint32());
    }

    // Clamp to current Num_ItemID — if a future build added more items
    // and we're loading that save in an older build, skip the extras.
    int itemsToLoad = (savedItemCount <= Num_ItemID) ? savedItemCount : Num_ItemID;
    int itemsToSkip = (savedItemCount > Num_ItemID) ? (savedItemCount - Num_ItemID) : 0;

    for(int i=0;i<itemsToLoad;i++) {
        for(int h=0;h<savedHouseCount;h++) {
            data[i][h].enabled = stream.readBool();
            data[i][h].hitpoints = stream.readSint32();
            data[i][h].price = stream.readSint32();
            data[i][h].power = stream.readSint32();
            data[i][h].viewrange = stream.readSint32();
            data[i][h].capacity = stream.readSint32();
            data[i][h].weapondamage = stream.readSint32();
            data[i][h].weaponrange = stream.readSint32();
            data[i][h].weaponreloadtime = stream.readSint32();
            data[i][h].maxspeed = stream.readFixPoint();
            data[i][h].turnspeed = stream.readFixPoint();
            data[i][h].buildtime = stream.readSint32();
            data[i][h].infspawnprop = stream.readSint32();
            data[i][h].builder = stream.readSint32();
            data[i][h].prerequisiteStructuresSet = std::bitset<Structure_LastID + 1>( (unsigned long) stream.readUint32());
            data[i][h].techLevel = stream.readSint8();
            data[i][h].upgradeLevel = stream.readSint8();
        }
    }

    // Items beyond itemsToLoad but within Num_ItemID keep constructor defaults (zeroed/disabled).
    // Items beyond Num_ItemID in the stream must be consumed to keep alignment.
    for(int i=0;i<itemsToSkip;i++) {
        for(int h=0;h<savedHouseCount;h++) {
            stream.readBool();
            stream.readSint32(); stream.readSint32(); stream.readSint32();
            stream.readSint32(); stream.readSint32(); stream.readSint32();
            stream.readSint32(); stream.readSint32();
            stream.readFixPoint(); stream.readFixPoint();
            stream.readSint32(); stream.readSint32(); stream.readSint32();
            stream.readUint32();
            stream.readSint8(); stream.readSint8();
        }
    }

    // Load map settings
    harvesterLimitSmallMap = stream.readSint32();
    harvesterLimitMediumMap = stream.readSint32();
    harvesterLimitLargeMap = stream.readSint32();
    harvesterLimitHugeMap = stream.readSint32();
    unitLimitSmallMap = stream.readSint32();
    unitLimitMediumMap = stream.readSint32();
    unitLimitLargeMap = stream.readSint32();
    unitLimitHugeMap = stream.readSint32();
}

int ObjectData::loadIntValue(const INIFile& objectDataFile, const std::string& section, const std::string& key, char houseChar, int defaultValue) {
    std::string specializedKey = key + "(" + houseChar + ")";
    if(objectDataFile.hasKey(section, specializedKey)) {
        return objectDataFile.getIntValue(section, specializedKey, defaultValue);
    } else {
        return objectDataFile.getIntValue(section, key, defaultValue);
    }
}

bool ObjectData::loadBoolValue(const INIFile& objectDataFile, const std::string& section, const std::string& key, char houseChar, bool defaultValue) {
    std::string specializedKey = key + "(" + houseChar + ")";
    if(objectDataFile.hasKey(section, specializedKey)) {
        return objectDataFile.getBoolValue(section, specializedKey, defaultValue);
    } else {
        return objectDataFile.getBoolValue(section, key, defaultValue);
    }
}

FixPoint ObjectData::loadFixPointValue(const INIFile& objectDataFile, const std::string& section, const std::string& key, char houseChar, FixPoint defaultValue) {
    std::string specializedKey = key + "(" + houseChar + ")";
    if(objectDataFile.hasKey(section, specializedKey)) {
        return FixPoint(objectDataFile.getStringValue(section, specializedKey, defaultValue.toString()));
    } else {
        return FixPoint(objectDataFile.getStringValue(section, key, defaultValue.toString()));
    }
}

std::string ObjectData::loadStringValue(const INIFile& objectDataFile, const std::string& section, const std::string& key, char houseChar, const std::string& defaultValue) {
    std::string specializedKey = key + "(" + houseChar + ")";
    if(objectDataFile.hasKey(section, specializedKey)) {
        return objectDataFile.getStringValue(section, specializedKey, defaultValue);
    } else {
        return objectDataFile.getStringValue(section, key, defaultValue);
    }
}

int ObjectData::loadItemID(const INIFile& objectDataFile, const std::string& section, const std::string& key, char houseChar, int defaultValue) {
    std::string strItem = trim(loadStringValue(objectDataFile, section, key, houseChar, ""));

    if(strItem == "") {
        return defaultValue;
    } else if(strToLower(strItem) == "invalid") {
        return ItemID_Invalid;
    }

    int itemID = getItemIDByName(strItem);

    if(itemID == ItemID_Invalid) {
        SDL_Log("Warning: Cannot read object data from section '%s', key '%s': '%s' is no valid structure/unit name!", section.c_str(), key.c_str(), strItem.c_str() );
        return defaultValue;
    }

    return itemID;
}


std::bitset<Structure_LastID + 1> ObjectData::loadPrerequisiteStructuresSet(const INIFile& objectDataFile, const std::string& section, const std::string& key, char houseChar, std::bitset<Structure_LastID + 1> defaultValue) {
    std::bitset<Structure_LastID + 1> resultSet;

    std::string strList = loadStringValue(objectDataFile, section, key, houseChar, "");

    if(strList == "") {
        return defaultValue;
    } else if(trim(strToLower(strList)) == "invalid") {
        return resultSet;
    }

    std::vector<std::string> strItemList = splitStringToStringVector(strList);

    for(const std::string& strItem : strItemList) {
        std::string strItem2 = trim(strItem);

        int itemID = getItemIDByName(strItem2);
        if(itemID == ItemID_Invalid || !isStructure(itemID)) {
            SDL_Log("Warning: Cannot read object data from section '%s', key '%s': '%s' is no valid structure name!", section.c_str(), key.c_str(), strItem2.c_str() );
            return defaultValue;
        }

        resultSet.set(itemID);
    }

    return resultSet;
}

std::string ObjectData::getEffectiveHash() const {
    // Build a string from all gameplay-affecting in-memory values
    std::string dataStr;
    
    // Hash all unit/structure data for all houses
    // New Tornie-only rows do not change any unrelated mod's capability hash.
    const int itemCount = ModManager::instance().isTornieFamilyContentActive()
        ? static_cast<int>(Num_ItemID) : static_cast<int>(Unit_Troopers5);
    for (int itemID = 0; itemID < itemCount; itemID++) {
        for (int houseID = 0; houseID < NUM_HOUSES; houseID++) {
            const ObjectDataStruct& obj = data[itemID][houseID];
            
            // Add all gameplay-affecting fields
            dataStr += std::to_string(itemID);
            dataStr += std::to_string(houseID);
            dataStr += std::to_string(obj.enabled);
            dataStr += std::to_string(obj.hitpoints);
            dataStr += std::to_string(obj.price);
            dataStr += std::to_string(obj.power);
            dataStr += std::to_string(obj.viewrange);
            dataStr += std::to_string(obj.capacity);
            dataStr += std::to_string(obj.weapondamage);
            dataStr += std::to_string(obj.weaponrange);
            dataStr += std::to_string(obj.weaponreloadtime);
            dataStr += std::to_string(obj.maxspeed.getRawValue());
            dataStr += std::to_string(obj.turnspeed.getRawValue());
            dataStr += std::to_string(obj.buildtime);
            dataStr += std::to_string(obj.infspawnprop);
            dataStr += std::to_string(obj.builder);
            dataStr += obj.prerequisiteStructuresSet.to_string();
            dataStr += std::to_string(obj.techLevel);
            dataStr += std::to_string(obj.upgradeLevel);
        }
    }
    
    // Also hash map settings
    dataStr += std::to_string(harvesterLimitSmallMap);
    dataStr += std::to_string(harvesterLimitMediumMap);
    dataStr += std::to_string(harvesterLimitLargeMap);
    dataStr += std::to_string(harvesterLimitHugeMap);
    dataStr += std::to_string(unitLimitSmallMap);
    dataStr += std::to_string(unitLimitMediumMap);
    dataStr += std::to_string(unitLimitLargeMap);
    dataStr += std::to_string(unitLimitHugeMap);
    
    // FNV-1a hash (consistent across platforms)
    uint64_t hash = 14695981039346656037ULL;
    const uint64_t prime = 1099511628211ULL;
    
    for (char c : dataStr) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= prime;
    }
    
    char hashStr[17];
    snprintf(hashStr, sizeof(hashStr), "%016llx", (unsigned long long)hash);
    return std::string(hashStr);
}
