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

#include <GameInitSettings.h>
#include <misc/CampaignControls.h>
#include <algorithm>

#include <misc/IFileStream.h>
#include <misc/IMemoryStream.h>
#include <misc/InputStream.h>
#include <misc/string_util.h>
#include <misc/exceptions.h>
#include <mmath.h>

#include <globals.h>
#include <mod/ModManager.h>

namespace {
constexpr Uint32 GAMEINIT_MOD_MARKER = 0x4D4F4421;   // "MOD!"
constexpr Uint32 GAMEINIT_MOD2_MARKER = 0x4D4F4432;  // "MOD2"
constexpr Uint32 GAMEINIT_MOD6_MARKER = 0x4D4F4436; // MOD6: original unit damage option
constexpr Uint32 GAMEINIT_MOD5_MARKER = 0x4D4F4435; // MOD5: construction yard limit
constexpr Uint32 GAMEINIT_MOD4_MARKER = 0x4D4F4434;  // "MOD4": immutable Workshop revisions
constexpr Uint32 GAMEINIT_MOD3_MARKER = 0x4D4F4433;  // "MOD3": graphics-only DuneCity skins
constexpr Uint32 GAMEINIT_MOD7_MARKER = 0x4D4F4437; // MOD7: per-house spice income multiplier

/// Every marker this build can read. A newer marker is not in the list, so it is not parsed -
/// which is why a format that adds one has to raise SAVEGAMEVERSION and the protocol version.
constexpr bool isKnownModMarker(Uint32 marker) {
    return marker == GAMEINIT_MOD_MARKER || marker == GAMEINIT_MOD2_MARKER
        || marker == GAMEINIT_MOD3_MARKER || marker == GAMEINIT_MOD4_MARKER
        || marker == GAMEINIT_MOD5_MARKER || marker == GAMEINIT_MOD6_MARKER
        || marker == GAMEINIT_MOD7_MARKER;
}

/// MOD2 and later carry the per-house colour array.
constexpr bool markerHasHouseColors(Uint32 marker) {
    return marker != GAMEINIT_MOD_MARKER;
}

/// MOD3 and later carry the per-house skin array and the campaign skin.
constexpr bool markerHasGraphicsSkins(Uint32 marker) {
    return marker == GAMEINIT_MOD3_MARKER || marker == GAMEINIT_MOD4_MARKER
        || marker == GAMEINIT_MOD5_MARKER || marker == GAMEINIT_MOD6_MARKER
        || marker == GAMEINIT_MOD7_MARKER;
}

/// MOD4 and later carry the immutable Workshop revision descriptors. A stream that announced
/// one of these and then ran out is a truncated stream, not an old format: the read rethrows.
constexpr bool markerHasWorkshopRevisions(Uint32 marker) {
    return marker == GAMEINIT_MOD4_MARKER || marker == GAMEINIT_MOD5_MARKER
        || marker == GAMEINIT_MOD6_MARKER || marker == GAMEINIT_MOD7_MARKER;
}

/// MOD5 and later carry the construction yard limit.
constexpr bool markerHasYardLimit(Uint32 marker) {
    return marker == GAMEINIT_MOD5_MARKER || marker == GAMEINIT_MOD6_MARKER
        || marker == GAMEINIT_MOD7_MARKER;
}

/// MOD6 and later carry the original-unit-damage rule.
constexpr bool markerHasOriginalUnitDamage(Uint32 marker) {
    return marker == GAMEINIT_MOD6_MARKER || marker == GAMEINIT_MOD7_MARKER;
}

/// MOD7 and later carry the per-house spice income multipliers.
constexpr bool markerHasSpiceIncome(Uint32 marker) {
    return marker == GAMEINIT_MOD7_MARKER;
}
}

// Helper to capture current mod info
static void setModInfo(std::string& modName, std::string& modChecksum) {
    if (ModManager::instance().isInitialized()) {
        modName = ModManager::instance().getActiveModName();
        modChecksum = ModManager::instance().getEffectiveChecksums().combined;
    } else {
        modName = "vanilla";
        modChecksum = "";
    }
}

GameInitSettings::GameInitSettings() {
    randomSeed = getRandomInt();
    setModInfo(modName, modChecksum);
    campaignGraphicsSkin = sanitizeGraphicsSkin(settings.general.duneCityCampaignSkin);
}

GameInitSettings::GameInitSettings(HOUSETYPE newHouseID, const SettingsClass::GameOptionsClass& gameOptions, int startLevel)
 : gameType(GameType::Campaign), houseID(newHouseID), mission(CampaignControls::firstMission(startLevel)), alreadyShownTutorialHints(0), gameOptions(gameOptions) {
    filename = getScenarioFilename(houseID, mission);
    randomSeed = getRandomInt();
    setModInfo(modName, modChecksum);
    campaignGraphicsSkin = sanitizeGraphicsSkin(settings.general.duneCityCampaignSkin);
}

GameInitSettings::GameInitSettings(const GameInitSettings& prevGameInitInfoClass, int nextMission, Uint32 alreadyPlayedRegions, Uint32 alreadyShownTutorialHints) {
    *this = prevGameInitInfoClass;
    mission = nextMission;
    this->alreadyPlayedRegions = alreadyPlayedRegions;
    this->alreadyShownTutorialHints = alreadyShownTutorialHints;
    filename = getScenarioFilename(houseID, mission);
    filedata.clear();
    randomSeed = getRandomInt();
}

GameInitSettings::GameInitSettings(HOUSETYPE newHouseID, int newMission, const SettingsClass::GameOptionsClass& gameOptions)
 : gameType(GameType::Skirmish), houseID(newHouseID), mission(newMission), gameOptions(gameOptions) {
    filename = getScenarioFilename(houseID, mission);
    randomSeed = getRandomInt();
    setModInfo(modName, modChecksum);
}

GameInitSettings::GameInitSettings(const std::string& mapfile, const std::string& filedata, bool multiplePlayersPerHouse, const SettingsClass::GameOptionsClass& gameOptions)
 : gameType(GameType::CustomGame), filename(mapfile), filedata(filedata), multiplePlayersPerHouse(multiplePlayersPerHouse), gameOptions(gameOptions) {
    randomSeed = getRandomInt();
    setModInfo(modName, modChecksum);
}

GameInitSettings::GameInitSettings(const std::string& mapfile, const std::string& filedata, const std::string& serverName, bool multiplePlayersPerHouse, const SettingsClass::GameOptionsClass& gameOptions)
 : gameType(GameType::CustomMultiplayer), filename(mapfile), filedata(filedata), servername(serverName), multiplePlayersPerHouse(multiplePlayersPerHouse), gameOptions(gameOptions) {
    randomSeed = getRandomInt();
    setModInfo(modName, modChecksum);
}

GameInitSettings::GameInitSettings(const std::string& savegame)
 : gameType(GameType::LoadSavegame) {
    checkSaveGame(savegame);
    filename = savegame;
}

GameInitSettings::GameInitSettings(const std::string& savegame, const std::string& filedata, const std::string& serverName)
 : gameType(GameType::LoadMultiplayer), filename(savegame), filedata(filedata), servername(serverName) {
    IMemoryStream memStream(filedata.c_str(), filedata.size());
    checkSaveGame(memStream);
}

GameInitSettings::GameInitSettings(InputStream& stream) {
    gameType = static_cast<GameType>(stream.readSint8());
    houseID = static_cast<HOUSETYPE>(stream.readSint8());

    filename = stream.readString();
    filedata = stream.readString();

    mission = stream.readUint8();
    alreadyPlayedRegions = stream.readUint32();
    alreadyShownTutorialHints = stream.readUint32();
    randomSeed = stream.readUint32();

    multiplePlayersPerHouse = stream.readBool();
    gameOptions.gameSpeed = stream.readUint32();
    gameOptions.concreteRequired = stream.readBool();
    // Reserved: the removed "Structures Degrade On Concrete" option. Read and
    // discarded so existing saves and lobby payloads keep their layout.
    stream.readBool();
    gameOptions.fogOfWar = stream.readBool();
    gameOptions.startWithExploredMap = stream.readBool();
    gameOptions.instantBuild = stream.readBool();
    gameOptions.onlyOnePalace = stream.readBool();
    gameOptions.rocketTurretsNeedPower = stream.readBool();
    gameOptions.sandwormsRespawn = stream.readBool();
    gameOptions.killedSandwormsDropSpice = stream.readBool();
    gameOptions.manualCarryallDrops = stream.readBool();
    gameOptions.maximumNumberOfUnitsOverride = stream.readSint32();
    gameOptions.maximumNumberOfHarvestersOverride = stream.readSint32();
    gameOptions.immortalHumanPlayer = stream.readBool();

    Uint32 numHouseInfo = stream.readUint32();
    // A house info is at least houseID + team + player count = 12 bytes.
    stream.requireReadableElements(numHouseInfo, 12);
    for(Uint32 i=0;i<numHouseInfo;i++) {
        houseInfoList.push_back(HouseInfo(stream));
    }
    
    // Read mod info (added in version with mod system)
    // Use marker to detect presence for backward compatibility
    Uint32 modMarker = 0;
    try {
        modMarker = stream.readUint32();
        if (isKnownModMarker(modMarker)) {
            modName = stream.readString();
            modChecksum = stream.readString();

            if(markerHasHouseColors(modMarker)) {
                Uint32 numHouseColors = stream.readUint32();
                stream.requireReadableElements(numHouseColors, 4);
                for(Uint32 i = 0; i < numHouseColors; i++) {
                    const int colorOfHouse = stream.readSint32();
                    if(i < houseInfoList.size()) {
                        houseInfoList[i].colorOfHouse = colorOfHouse;
                    }
                }
            }
            if(markerHasGraphicsSkins(modMarker)) {
                const Uint32 numHouseSkins = stream.readUint32();
                stream.requireReadableElements(numHouseSkins, 4);
                for(Uint32 i = 0; i < numHouseSkins; ++i) {
                    const GraphicsSkin skin = sanitizeGraphicsSkin(stream.readUint32());
                    if(i < houseInfoList.size()) {
                        houseInfoList[i].graphicsSkin = skin;
                    }
                }
                campaignGraphicsSkin = sanitizeGraphicsSkin(stream.readUint32());
            }
            if(markerHasWorkshopRevisions(modMarker)) {
                modRevisionHash = stream.readString();
                modRevisionVersion = stream.readUint32();
                mapRevisionHash = stream.readString();
                mapRevisionVersion = stream.readUint32();
                mapRevisionManifest = stream.readString();
                if(markerHasYardLimit(modMarker))
                    gameOptions.maximumNumberOfConstructionYardsOverride = stream.readSint32();
                if(markerHasOriginalUnitDamage(modMarker))
                    gameOptions.originalUnitDamage = stream.readBool();
                const auto validHash = [](const std::string& hash) {
                    return hash.empty() || (hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == std::string::npos);
                };
                if(!validHash(modRevisionHash) || !validHash(mapRevisionHash) || mapRevisionManifest.size() > 65536)
                    throw std::runtime_error("Invalid Workshop revision descriptor.");
            }
            if(markerHasSpiceIncome(modMarker)) {
                // The factors the lobby chose, in row order. A row may still be Random here,
                // so these are matched by position, never by house identity.
                const Uint32 numSpiceFactors = stream.readUint32();
                if(numSpiceFactors > static_cast<Uint32>(MAX_CUSTOM_GAME_PLAYERS)
                   || numSpiceFactors != houseInfoList.size()) {
                    throw std::runtime_error("Invalid spice income factor count.");
                }
                stream.requireReadableElements(numSpiceFactors, 4);
                for(Uint32 i = 0; i < numSpiceFactors; ++i) {
                    const Uint32 factor = stream.readUint32();
                    // Refused, not clamped: a factor this build does not recognise would make
                    // this peer pay a different rate than the one that wrote the stream.
                    if(!SpiceIncome::isValid(factor)) {
                        throw std::runtime_error("Invalid spice income factor.");
                    }
                    if(i < houseInfoList.size()) {
                        houseInfoList[i].spiceIncomeMultiplier = factor;
                    }
                }
            }
        }
    } catch (InputStream::eof&) {
        if(markerHasWorkshopRevisions(modMarker)) throw;
        // Old format without mod info - use defaults
        modName = "vanilla";
        modChecksum = "";
    }
}

void GameInitSettings::configureCoopSave(const GameInitSettings& saved, const HouseInfoList& houses) {
    houseID = saved.houseID;
    mission = saved.mission;
    alreadyPlayedRegions = saved.alreadyPlayedRegions;
    gameOptions = saved.gameOptions;
    modName = saved.modName;
    modChecksum = saved.modChecksum;
    modRevisionHash = saved.modRevisionHash;
    modRevisionVersion = saved.modRevisionVersion;
    mapRevisionHash = saved.mapRevisionHash;
    mapRevisionVersion = saved.mapRevisionVersion;
    mapRevisionManifest = saved.mapRevisionManifest;
    campaignGraphicsSkin = saved.campaignGraphicsSkin;
    houseInfoList = houses;
    // Future campaign missions can introduce enemies not present in this save.
    for(const auto& planned : saved.houseInfoList) {
        if(std::none_of(houseInfoList.begin(), houseInfoList.end(), [&](const HouseInfo& h) { return h.houseID == planned.houseID; }))
            houseInfoList.push_back(planned);
    }
}

void GameInitSettings::enableCoop(bool campaign, const std::string& serverName) {
    gameType = (gameType == GameType::LoadMultiplayer || gameType == GameType::LoadCoop)
        ? GameType::LoadCoop : (campaign ? GameType::CampaignCoop : GameType::SkirmishCoop);
    multiplePlayersPerHouse = true;
    servername = serverName;
    gameOptions.immortalHumanPlayer = false;
}

GameInitSettings GameInitSettings::readSaveSetup(InputStream& stream, HouseInfoList& houses) {
    if(stream.readUint32() != SAVEMAGIC) THROW(std::runtime_error, "Not a valid savegame.");
    const auto version = stream.readUint32();
    stream.readString();
    if(version < 9705 || version > SAVEGAMEVERSION) THROW(std::runtime_error, "Unsupported savegame version.");
    if(version >= 9806) { stream.readString(); stream.readString(); }
    GameInitSettings saved(stream);
    if(version <= 9820) saved.migrateLegacyHouseColorSlots();
    const auto count = stream.readUint32();
    if(count > NUM_HOUSES) THROW(std::runtime_error, "Invalid saved house count.");
    houses.clear();
    for(Uint32 i = 0; i < count; ++i) houses.emplace_back(stream);
    if(version >= 9814) {
        if(stream.readUint32() != 0x53434F4C) THROW(std::runtime_error, "Invalid saved house colors.");
        const auto colors = stream.readUint32();
        if(colors != count) THROW(std::runtime_error, "Invalid saved color count.");
        for(auto& house : houses) {
            house.colorOfHouse = stream.readSint32();
            if(version <= 9820) house.colorOfHouse = migrateLegacyHouseColorSlot(house.colorOfHouse);
        }
    }
    // HouseInfo's standalone save block predates skins; MOD3 owns that data.
    // Match by house identity because setup rows can differ from init ordering.
    for(auto& house : houses) {
        const auto init = std::find_if(saved.houseInfoList.begin(), saved.houseInfoList.end(),
            [&](const HouseInfo& candidate) { return candidate.houseID == house.houseID; });
        if(init != saved.houseInfoList.end()) house.graphicsSkin = init->graphicsSkin;
    }
    if(version >= SpiceIncome::kFirstSavegameVersion) {
        // Deliberately *not* matched by identity against saved.houseInfoList: a row that was
        // played as Random still says HOUSE_INVALID there, so identity matching would silently
        // hand that row the default factor. SMUL carries the resolved rows instead, and it is
        // the only authority for them.
        if(stream.readUint32() != SpiceIncome::kSetupMarker)
            THROW(std::runtime_error, "Invalid saved spice income factors.");
        const auto factors = stream.readUint32();
        if(factors != count) THROW(std::runtime_error, "Invalid saved spice income count.");
        for(auto& house : houses) {
            const Uint32 factor = stream.readUint32();
            if(!SpiceIncome::isValid(factor))
                THROW(std::runtime_error, "Invalid saved spice income factor.");
            house.spiceIncomeMultiplier = factor;
        }
    }
    return saved;
}

GameInitSettings::~GameInitSettings() {
}

void GameInitSettings::save(OutputStream& stream) const {
    if(houseInfoList.size() > static_cast<std::size_t>(MAX_CUSTOM_GAME_PLAYERS))
        THROW(std::runtime_error, "GameInitSettings::save(): too many house rows.");
    stream.writeSint8(static_cast<Sint8>(gameType));
    stream.writeSint8(houseID);

    stream.writeString(filename);
    stream.writeString(filedata);

    stream.writeUint8(mission);
    stream.writeUint32(alreadyPlayedRegions);
    stream.writeUint32(alreadyShownTutorialHints);
    stream.writeUint32(randomSeed);

    stream.writeBool(multiplePlayersPerHouse);
    stream.writeUint32(gameOptions.gameSpeed);
    stream.writeBool(gameOptions.concreteRequired);
    stream.writeBool(false);    // reserved: removed "Structures Degrade On Concrete" option
    stream.writeBool(gameOptions.fogOfWar);
    stream.writeBool(gameOptions.startWithExploredMap);
    stream.writeBool(gameOptions.instantBuild);
    stream.writeBool(gameOptions.onlyOnePalace);
    stream.writeBool(gameOptions.rocketTurretsNeedPower);
    stream.writeBool(gameOptions.sandwormsRespawn);
    stream.writeBool(gameOptions.killedSandwormsDropSpice);
    stream.writeBool(gameOptions.manualCarryallDrops);
    stream.writeSint32(gameOptions.maximumNumberOfUnitsOverride);
    stream.writeSint32(gameOptions.maximumNumberOfHarvestersOverride);
    stream.writeBool(gameOptions.immortalHumanPlayer);

    stream.writeUint32(houseInfoList.size());
    for(const HouseInfo& houseInfo : houseInfoList) {
        houseInfo.save(stream);
    }
    
    // Write mod info with marker for forward compatibility
    stream.writeUint32(GAMEINIT_MOD7_MARKER);
    stream.writeString(modName);
    stream.writeString(modChecksum);

    stream.writeUint32(houseInfoList.size());
    for(const HouseInfo& houseInfo : houseInfoList) {
        stream.writeSint32(houseInfo.colorOfHouse);
    }

    stream.writeUint32(houseInfoList.size());
    for(const HouseInfo& houseInfo : houseInfoList) {
        stream.writeUint32(static_cast<Uint32>(houseInfo.graphicsSkin));
    }
    stream.writeUint32(static_cast<Uint32>(campaignGraphicsSkin));
    stream.writeString(modRevisionHash);
    stream.writeUint32(modRevisionVersion);
    stream.writeString(mapRevisionHash);
    stream.writeUint32(mapRevisionVersion);
    stream.writeString(mapRevisionManifest);
    stream.writeSint32(gameOptions.maximumNumberOfConstructionYardsOverride);
    stream.writeBool(gameOptions.originalUnitDamage);

    // MOD7: the chosen per-row spice income factors. Written last so the block stays append
    // only. The writer refuses an out-of-range factor rather than normalising it, so a bug
    // upstream surfaces here instead of becoming a save that two builds read differently.
    stream.writeUint32(houseInfoList.size());
    for(const HouseInfo& houseInfo : houseInfoList) {
        if(!SpiceIncome::isValid(houseInfo.spiceIncomeMultiplier)) {
            THROW(std::runtime_error, "GameInitSettings::save(): invalid spice income factor "
                + std::to_string(houseInfo.spiceIncomeMultiplier) + ".");
        }
        stream.writeUint32(houseInfo.spiceIncomeMultiplier);
    }
}



void GameInitSettings::migrateLegacyHouseColorSlots() {
    for(HouseInfo& houseInfo : houseInfoList) {
        houseInfo.colorOfHouse = migrateLegacyHouseColorSlot(houseInfo.colorOfHouse);
    }
}

std::string GameInitSettings::getScenarioFilename(HOUSETYPE newHouse, int mission) {
    if((newHouse < 0) || (newHouse >= NUM_HOUSES) || !isHouseAvailable(newHouse)) {
        THROW(std::invalid_argument, "GameInitSettings::getScenarioFilename(): Invalid house id " + std::to_string(newHouse) + ".");
    }

    if( (mission < 0) || (mission > 22)) {
        THROW(std::invalid_argument, "GameInitSettings::getScenarioFilename(): There is no mission number " + std::to_string(mission) + ".");
    }

    std::string name = "SCEN?0??.INI";
    name[4] = getHouseScenarioLetter(newHouse);

    name[6] = '0' + (mission / 10);
    name[7] = '0' + (mission % 10);

    return name;
}

void GameInitSettings::checkSaveGame(const std::string& savegame) {
    IFileStream fs;

    if(fs.open(savegame) == false) {
        THROW(std::runtime_error, "Cannot open savegame. Make sure you have read access to this savegame!");
    }

    checkSaveGame(fs);

    fs.close();
}


void GameInitSettings::checkSaveGame(InputStream& stream) {
    Uint32 magicNum;
    Uint32 savegameVersion;
    std::string duneVersion;
    try {
        magicNum = stream.readUint32();
        savegameVersion = stream.readUint32();
        duneVersion = stream.readString();
    } catch (std::exception&) {
        THROW(std::runtime_error, "Cannot load this savegame,\n because it seems to be truncated!");
    }

    if(magicNum != SAVEMAGIC) {
        THROW(std::runtime_error, "Cannot load this savegame,\n because it has a wrong magic number!");
    }

    // Support backward compatibility: Accept version 9705 (pre-Original AI) and newer
    constexpr Uint32 MINIMUM_SUPPORTED_VERSION = 9705;
    
    if(savegameVersion < MINIMUM_SUPPORTED_VERSION) {
        THROW(std::runtime_error, "Cannot load this savegame,\n because it was created with an older version:\n" + duneVersion);
    }

    if(savegameVersion > SAVEGAMEVERSION) {
        THROW(std::runtime_error, "Cannot load this savegame,\n because it was created with a newer version:\n" + duneVersion);
    }
}
