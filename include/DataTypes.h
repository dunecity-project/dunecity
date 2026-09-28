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

#ifndef DATATYPES_H
#define DATATYPES_H

#include <Definitions.h>
#include <misc/SDL2pp.h>

// Libraries
#include <string>
#include <array>
#include <cstdint>
#include <cstdio>


typedef std::array<SDL_Texture*, NUM_ZOOMLEVEL> zoomable_texture;


class Coord {
public:
    Coord()
     : x(0), y(0) {
    }

    Coord(int x,int y)
     : x(x), y(y)
    {
    }

    inline bool operator==(const Coord& c) const {
        return (x == c.x && y == c.y);
    }

    inline bool operator!=(const Coord& c) const {
        return !operator==(c);
    }

    inline Coord& operator+=(const Coord& c) {
        x += c.x;
        y += c.y;
        return *this;
    }

    inline Coord operator+(const Coord& c) const {
        Coord ret = *this;
        ret += c;
        return ret;
    }

    inline Coord& operator-=(const Coord& c) {
        x -= c.x;
        y -= c.y;
        return *this;
    }

    inline Coord operator-(const Coord& c) const {
        Coord ret = *this;
        ret -= c;
        return ret;
    }

    inline Coord& operator*=(int c) {
        x *= c;
        y *= c;
        return *this;
    }

    inline Coord operator*(int c) const {
        Coord ret = *this;
        ret *= c;
        return ret;
    }

    inline Coord& operator/=(int c) {
        x /= c;
        y /= c;
        return *this;
    }

    inline Coord operator/(int c) const {
        Coord ret = *this;
        ret /= c;
        return ret;
    }

    inline void invalidate() {
        x = INVALID_POS;
        y = INVALID_POS;
    }

    inline bool isValid() const {
        return ((x != INVALID_POS) && (y != INVALID_POS));
    }

    inline bool isInvalid() const {
        return ((x == INVALID_POS) || (y == INVALID_POS));
    }

    static inline const Coord Invalid() {
        return Coord(INVALID_POS, INVALID_POS);
    }

    inline operator bool() const {
        return isValid();
    };

public:
    int x;
    int y;
};

typedef enum {
    ATTACKMODE_INVALID = -1,
    GUARD = 0,      ///< The unit will attack enemy units but will not move or follow enemy units.
    AREAGUARD = 1,  ///< Area Guard is the most common command for pre-placed AI units. They will scan for targets in a relatively large radius, and return to their original position after their target was either destroyed or left the immediate area.
    AMBUSH = 2,     ///< Ambush means a unit will remain in position until sighted by the enemy, and then proceed to attack any enemy units it might find on the map.
    HUNT = 3,       ///< Hunt makes a unit start from its position towards enemy units, even if the player has not sighted the AI (normally the AI will not attack until there has been a contact between the player's and the AI's units). Also works for human units, they'll go towards any enemy units on the map just as the mission starts.
    HARVEST = 4,    ///< Only used by the map editor
    SABOTAGE = 5,   ///< Only used by the map editor
    STOP = 6,
    CAPTURE = 7,    ///< Capture is only used for infantry units when ordered to capture a building
    CARRYALLREQUESTED = 8, ///< This allows a unit to keep requesting a carryall even if one isn't available right now
    RETREAT = 9,           ///< Ignore other units
    ATTACKMODE_MAX
} ATTACKMODE;

enum class GameState {
    Start,
    Loading,
    Running,
    Deinitialize
};

enum class GameType {
    Invalid           = -1,
    LoadSavegame      = 0,
    Campaign          = 1,
    CustomGame        = 2,
    Skirmish          = 3,
    CustomMultiplayer = 4,
    LoadMultiplayer   = 5,
    CampaignCoop     = 6,
    SkirmishCoop     = 7,
    LoadCoop         = 8
};


constexpr bool isCoopGameType(GameType type) {
    return type == GameType::CampaignCoop || type == GameType::SkirmishCoop || type == GameType::LoadCoop;
}
constexpr bool isNetworkGameType(GameType type) {
    return type == GameType::CustomMultiplayer || type == GameType::LoadMultiplayer || isCoopGameType(type);
}
constexpr bool isCampaignGameType(GameType type) {
    return type == GameType::Campaign || type == GameType::CampaignCoop;
}
constexpr bool isScenarioGameType(GameType type) {
    return isCampaignGameType(type) || type == GameType::Skirmish || type == GameType::SkirmishCoop;
}

class SettingsClass
{
public:
    class GeneralClass {
    public:
        bool            playIntro;          ///< Play the intro when starting the game?
        std::string     playerName;         ///< The name of the player used for multiplayer games
        std::string     language;           ///< Language code: "en" = English, "fr" = French, "de" = German
        int             scrollSpeed;        ///< Scroll speed in pixels
        bool            showTutorialHints;  ///< If true, tutorial hints are shown during the game
        int             duneCityCampaignSkin = 0; ///< 0=SimCity, 1=Dune2; presentation only
        bool            wasdCamera = false;
        bool            leftClickOrders = false;
        bool            showMovementPaths = true;
#ifdef __EMSCRIPTEN__
        static constexpr bool defaultDiagnosticLogs = false;
#else
        static constexpr bool defaultDiagnosticLogs = true;
#endif
        bool            diagnosticLogs = defaultDiagnosticLogs;
        bool            multiplePlayersPerHouse = false; ///< Custom game lobby: allow two players per house (remembered across games)
        bool            campaignOnline = true;     ///< Campaign setup: last Offline/Online co-op choice (remembered across games)
        bool            customGameOnline = false;  ///< Custom game setup: last Offline/Online choice (remembered across games)
        std::string     customGameMod;             ///< Custom game setup: last mod the player picked (empty: follow the active mod)
    } general;

    class VideoClass {
    public:
        bool        fullscreen;
        int         physicalWidth;
        int         physicalHeight;
        int         width;
        int         height;
        int         interfaceHeight = 0; ///< 0=automatic; 480/600/768 with a persisted 4:3 or 16:9 logical width.
        int         menuPalette = 0; ///< 0=desert gold; 1=high-contrast dark text.
        int         startMenuMode = 0; ///< 0=classic; 1=enlarged TV/tablet/accessibility layout.
        bool        frameLimit;
        int         preferredZoomLevel;
        std::string scaler;
        bool        rotateUnitGraphics;
        bool        showWatermark;      ///< Show mod/version watermark during gameplay (default: true)
        int         cursorVisibility;   ///< Cursor visibility: 0=auto, 1=hidden, 2=visible
        int         cursorScale;        ///< Cursor scale factor in window units: 0=1.5x default, 1=1x, 2=2x, 3=3x, 4=4x
    } video;

    class AudioClass {
    public:
        bool        playSFX;
        int         sfxVolume;
        bool        playMusic;
        int         musicVolume;
        std::string musicType;
        bool        adlHarmonicStereo;
        bool        playCreditsSFX;
    } audio;

    class NetworkClass {
    public:
        int         serverPort;
        std::string metaServer;
        bool        debugNetwork;
        /// Base URL of the crossplay room relay; empty means crossplay is not configured.
        std::string relayEndpoint;
        /// Loopback relay used for local testing; only reachable when the option below is on.
        std::string relayDevelopmentEndpoint;
        /// When set, the loopback relay is used and plain ws:// to loopback becomes acceptable.
        bool        relayUseDevelopmentEndpoint = false;

        /// Base URL of the direct-play signaling service; empty means direct play is off.
        std::string directEndpoint;
        /// Loopback signaling service for local testing; only reachable with the option below.
        std::string directDevelopmentEndpoint;

        /// The relay address this session should use, honouring the development option.
        std::string activeRelayEndpoint() const {
            return relayUseDevelopmentEndpoint ? relayDevelopmentEndpoint : relayEndpoint;
        }

        /**
            The signaling address a direct session should use.

            Shares the development opt-in with the relay setting, because it is the same
            decision - "use the service running on this computer" - and having two switches for
            it would let a player end up half on one and half on the other.
        */
        std::string activeDirectEndpoint() const {
            return relayUseDevelopmentEndpoint ? directDevelopmentEndpoint : directEndpoint;
        }
    } network;

    class DiscordClass {
    public:
        std::string webhookUrl;  // Discord webhook URL for game notifications
    } discord;

    class AIClass {
    public:
        std::string campaignAI;
    } ai;

    class GameOptionsClass {
    public:
        GameOptionsClass()
         : gameSpeed(GAMESPEED_DEFAULT), concreteRequired(true), fogOfWar(false),
           startWithExploredMap(false), instantBuild(false), onlyOnePalace(false), rocketTurretsNeedPower(false),
           sandwormsRespawn(false), killedSandwormsDropSpice(false), manualCarryallDrops(false), maximumNumberOfUnitsOverride(-1),
           maximumNumberOfHarvestersOverride(-1), maximumNumberOfConstructionYardsOverride(-1),
           immortalHumanPlayer(false), cityEffects(false), originalUnitDamage(false)  {
        }


        bool operator==(const GameOptionsClass& goc) const {
            return (gameSpeed == goc.gameSpeed)
                    && (concreteRequired == goc.concreteRequired)
                    && (fogOfWar == goc.fogOfWar)
                    && (startWithExploredMap == goc.startWithExploredMap)
                    && (instantBuild == goc.instantBuild)
                    && (onlyOnePalace == goc.onlyOnePalace)
                    && (rocketTurretsNeedPower == goc.rocketTurretsNeedPower)
                    && (sandwormsRespawn == goc.sandwormsRespawn)
                    && (killedSandwormsDropSpice == goc.killedSandwormsDropSpice)
                    && (manualCarryallDrops == goc.manualCarryallDrops)
                    && (maximumNumberOfUnitsOverride == goc.maximumNumberOfUnitsOverride)
                    && (maximumNumberOfHarvestersOverride == goc.maximumNumberOfHarvestersOverride)
                    && (maximumNumberOfConstructionYardsOverride == goc.maximumNumberOfConstructionYardsOverride)
                    && (immortalHumanPlayer == goc.immortalHumanPlayer)
                    && (cityEffects == goc.cityEffects)
                    && (originalUnitDamage == goc.originalUnitDamage);
        }

        bool operator!=(const GameOptionsClass& goc) const {
            return !this->operator==(goc);
        }

        /**
            Returns a hash of all game options for multiplayer verification.
            \return 16-character hex string (FNV-1a hash)
        */
        std::string getHash() const {
            // Build string from all gameplay-affecting values
            std::string optStr;
            optStr += std::to_string(gameSpeed);
            optStr += std::to_string(concreteRequired);
            optStr += std::to_string(fogOfWar);
            optStr += std::to_string(startWithExploredMap);
            optStr += std::to_string(instantBuild);
            optStr += std::to_string(onlyOnePalace);
            optStr += std::to_string(rocketTurretsNeedPower);
            optStr += std::to_string(sandwormsRespawn);
            optStr += std::to_string(killedSandwormsDropSpice);
            optStr += std::to_string(manualCarryallDrops);
            optStr += std::to_string(maximumNumberOfUnitsOverride);
            optStr += std::to_string(maximumNumberOfHarvestersOverride);
            optStr += std::to_string(cityEffects);
            optStr += "/yards=" + std::to_string(maximumNumberOfConstructionYardsOverride);
            optStr += "/originalDamage=" + std::to_string(originalUnitDamage);
            // Note: immortalHumanPlayer is intentionally excluded as it's a per-player setting
            
            // FNV-1a hash
            uint64_t hash = 14695981039346656037ULL;
            const uint64_t prime = 1099511628211ULL;
            for (char c : optStr) {
                hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
                hash *= prime;
            }
            
            char hashStr[17];
            snprintf(hashStr, sizeof(hashStr), "%016llx", (unsigned long long)hash);
            return std::string(hashStr);
        }

        int         gameSpeed;
        bool        concreteRequired;
        // "Structures Degrade On Concrete" was removed with the Dynasty-aligned
        // degradation rules (include/dunecity/StructureDegradation.h): power
        // damage applies everywhere, and a complete foundation always suppresses
        // foundation decay. GameInitSettings still reserves its stream byte.
        bool        fogOfWar;
        bool        startWithExploredMap;
        bool        instantBuild;
        bool        onlyOnePalace;
        bool        rocketTurretsNeedPower;
        bool        sandwormsRespawn;
        bool        killedSandwormsDropSpice;
        bool        manualCarryallDrops;
        int         maximumNumberOfUnitsOverride;
        int         maximumNumberOfHarvestersOverride;
        int         maximumNumberOfConstructionYardsOverride; // -1 or 0: unlimited
        bool        immortalHumanPlayer;
        bool        cityEffects;        ///< DuneCity: enable pollution/land-value/crime/zone-growth pipeline
        bool        originalUnitDamage; ///< Match-wide original damage and splash rules; off uses City balance.
    } gameOptions;
};

typedef enum
{
    HOUSE_UNUSED    = -2,
    HOUSE_INVALID   = -1,
    HOUSE_HARKONNEN =  0,
    HOUSE_ATREIDES  =  1,
    HOUSE_ORDOS     =  2,
    HOUSE_FREMEN    =  3,
    HOUSE_SARDAUKAR =  4,
    HOUSE_MERCENARY =  5,
    HOUSE_NEUTRAL   =  6,
    HOUSE_REBELS    =  7,
    HOUSE_CUSTOM     = 8,
    HOUSE_WILDSPADE  = 9,
    HOUSE_KLESHMERSH = 10,
    HOUSE_THARPIQUE  = 11,
    NUM_HOUSES
} HOUSETYPE;

constexpr int NUM_LEGACY_HOUSES = 8;
constexpr int NUM_CAMPAIGN_HOUSES = HOUSE_CUSTOM + 1;
constexpr int MAX_CUSTOM_GAME_PLAYERS = 9;
constexpr int NUM_BASE_HOUSE_COLOR_SLOTS = NUM_CAMPAIGN_HOUSES;

constexpr int HOUSECOLOR_CUSTOM_DARK_VIOLET   = NUM_BASE_HOUSE_COLOR_SLOTS;
constexpr int HOUSECOLOR_CUSTOM_FUCHSIA       = NUM_BASE_HOUSE_COLOR_SLOTS + 1;
constexpr int HOUSECOLOR_CUSTOM_TEAL          = NUM_BASE_HOUSE_COLOR_SLOTS + 2;
constexpr int HOUSECOLOR_CUSTOM_BRIGHT_YELLOW = NUM_BASE_HOUSE_COLOR_SLOTS + 3;
constexpr int HOUSECOLOR_CUSTOM_APPLE_GREEN   = NUM_BASE_HOUSE_COLOR_SLOTS + 4;
constexpr int HOUSECOLOR_CUSTOM_LIGHT_PINK    = NUM_BASE_HOUSE_COLOR_SLOTS + 5;
constexpr int NUM_CUSTOM_HOUSE_COLORS         = 6;
constexpr int HOUSECOLOR_GUEST_1               = NUM_BASE_HOUSE_COLOR_SLOTS + NUM_CUSTOM_HOUSE_COLORS;
constexpr int HOUSECOLOR_GUEST_2               = HOUSECOLOR_GUEST_1 + 1;
constexpr int HOUSECOLOR_GUEST_3               = HOUSECOLOR_GUEST_1 + 2;

constexpr int migrateLegacyHouseColorSlot(int colorSlot) noexcept {
    return (colorSlot >= NUM_LEGACY_HOUSES
            && colorSlot < NUM_LEGACY_HOUSES + NUM_CUSTOM_HOUSE_COLORS)
        ? colorSlot + 1
        : colorSlot;
}
constexpr int NUM_HOUSE_COLOR_SLOTS           = HOUSECOLOR_GUEST_3 + 1;

typedef enum {
    RIGHT,
    RIGHTUP,
    UP,
    LEFTUP,
    LEFT,
    LEFTDOWN,
    DOWN,
    RIGHTDOWN,
    NUM_ANGLES
} ANGLETYPE;

typedef enum  {
    Drop_Invalid = -1,
    Drop_North,         ///< unit will appear at a random position at the top of the map
    Drop_East,          ///< unit will appear at a random position on the right side of the map
    Drop_South,         ///< unit will appear at a random position at the bottom of the map
    Drop_West,          ///< unit will appear at a random position on the left side of the map
    Drop_Air,           ///< unit will be dropped at a random position
    Drop_Visible,       ///< unit will be dropped at a random position in the middle of the map
    Drop_Enemybase,     ///< unit will be dropped near the enemy base
    Drop_Homebase       ///< unit will be dropped near the base of the owner of the new unit
} DropLocation;

typedef enum {
    AITeamBehavior_Invalid = -1,
    AITeamBehavior_Normal,            ///< Attack units and/or structures when building up the team is complete
    AITeamBehavior_Guard,             ///< Same as AITeamBehavior_Normal
    AITeamBehavior_Kamikaze,          ///< Directly attack structures when building up the team is complete
    AITeamBehavior_Staging,           ///< A team in the process of being built up
    AITeamBehavior_Flee               ///< Do nothing (Unimplemented in Dune II?)
} AITeamBehavior;

typedef enum {
    AITeamType_Invalid = -1,
    AITeamType_Foot,
    AITeamType_Wheeled,
    AITeamType_Tracked,
    AITeamType_Winged,
    AITeamType_Slither,
    AITeamType_Harvester
} AITeamType;

#endif //DATATYPES_H
