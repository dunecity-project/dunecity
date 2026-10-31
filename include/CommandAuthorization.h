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

#ifndef COMMANDAUTHORIZATION_H
#define COMMANDAUTHORIZATION_H

#include <Command.h>
#include <DataTypes.h>

#include <cstddef>

/**
    Who may act on which object.

    Every command that acts on an object carries that object's id in parameter 0 and is executed
    on *every* peer, so the decision has to be identical everywhere: it may only depend on
    simulation state (the issuing player, the object and its owner), never on which peer is
    running it or on who sent the packet.

    Command::executeCommand() resolves the issuer and the object with the existing casts and
    then asks authorizeActor() for the verdict. The decision itself lives here so it can be
    driven directly by tests with the same inputs the production path produces - the tests are
    not a second copy of the rule.

    Rules:
      - the issuing player must exist and have a house (a command for a departed or unknown
        player is a no-op, not an exception);
      - the acting object must exist and be of the type the command requires (that is what the
        existing dynamic_cast already establishes);
      - the object belongs to the issuer's house, or the synchronized Tornie alliance rule
        explicitly grants control. Two humans sharing a house keep control, and a deviated
        unit follows its temporary owner because deviation reassigns ObjectBase::owner;
      - target parameters are deliberately not constrained: attacking, capturing or moving to
        an enemy object is the point of the game.
*/
namespace CommandAuthorization {

/// Outcome of the authorization decision. Everything except Allow is a deterministic no-op.
enum class Decision {
    Allow,
    NoIssuer,           ///< no such player, or the player has no house
    MissingObject,      ///< the object id does not resolve (destroyed, never existed)
    WrongObjectType,    ///< the object is not the type this command acts on
    NotOwner            ///< the object belongs to another house
};

/// Facts about one command execution, gathered from live simulation state by the caller.
struct ActorContext {
    bool issuerExists       = false;    ///< getPlayerByID() found the issuing player
    bool issuerHasHouse     = false;    ///< that player has a house
    int  issuerHouseID      = -1;
    bool objectExists       = false;    ///< the object id resolved to a live object
    bool objectTypeMatches  = false;    ///< the command's dynamic_cast succeeded
    bool objectHasOwner     = false;
    int  objectOwnerHouseID = -1;
    bool alliedControl      = false;    ///< resolved from synchronized match/alliance state
};

/**
    \param  context the facts gathered for this command
    \return Allow, or the reason the command must be ignored
*/
inline Decision authorizeActor(const ActorContext& context) {
    if(!context.issuerExists || !context.issuerHasHouse) {
        return Decision::NoIssuer;
    }
    if(!context.objectExists) {
        return Decision::MissingObject;
    }
    if(!context.objectTypeMatches) {
        return Decision::WrongObjectType;
    }
    // Natives objects (worms, crime troopers) belong to no controller: the Natives house never
    // has players (Game::getOrCreateNativesHouse), and even one registered there may not act.
    if(!context.objectHasOwner || !isPlayerHouseID(context.objectOwnerHouseID)
       || !isPlayerHouseID(context.issuerHouseID)
       || (context.objectOwnerHouseID != context.issuerHouseID && !context.alliedControl)) {
        return Decision::NotOwner;
    }
    return Decision::Allow;
}

/**
    \param  commandID   the command being executed
    \return true if this command acts on an object named by parameter 0
*/
inline bool actsOnOwnedObject(CMDTYPE commandID) {
    switch(commandID) {
        case CMD_PLACE_STRUCTURE:
        case CMD_UNIT_MOVE2POS:
        case CMD_UNIT_MOVE2OBJECT:
        case CMD_UNIT_ATTACKPOS:
        case CMD_UNIT_ATTACKOBJECT:
        case CMD_UNIT_HEAL:
        case CMD_INFANTRY_CAPTURE:
        case CMD_UNIT_REQUESTCARRYALLDROP:
        case CMD_UNIT_SENDTOREPAIR:
        case CMD_UNIT_SETMODE:
        case CMD_DEVASTATOR_STARTDEVASTATE:
        case CMD_MCV_DEPLOY:
        case CMD_HARVESTER_RETURN:
        case CMD_STRUCTURE_SETDEPLOYPOSITION:
        case CMD_STRUCTURE_REPAIR:
        case CMD_BUILDER_UPGRADE:
        case CMD_BUILDER_PRODUCEITEM:
        case CMD_BUILDER_CANCELITEM:
        case CMD_BUILDER_SETONHOLD:
        case CMD_PALACE_SPECIALWEAPON:
        case CMD_PALACE_DEATHHAND:
        case CMD_STARPORT_PLACEORDER:
        case CMD_STARPORT_CANCELORDER:
        case CMD_TURRET_ATTACKOBJECT:
        case CMD_TECHCENTER_SPAWN:
        case CMD_SCOUTPOST_UPGRADE:
        case CMD_SCOUTPOST_CHEMIPOST_UPGRADE:
        case CMD_POLICE_REINFORCEMENTS:
        case CMD_ZONE_DEMOLISH:
        case CMD_STRUCTURE_DEMOLISH:
        case CMD_UNIT_ATTACKMOVE:
        case CMD_UNIT_PATROL:
        case CMD_UNIT_QUEUEORDER:
        case CMD_BUILDER_SETREPEAT:
        case CMD_BUILDER_RESERVE_PLACEMENT:
        case CMD_BUILDER_CANCEL_RESERVATION:
        case CMD_UNIT_FORMATION_MOVE:
        case CMD_UNIT_FORMATION_HUNT:
        case CMD_STRUCTURE_SELL:
        case CMD_WINDTRAP_POWER_UPGRADE:
        case CMD_SCOUTPOST_SLOWPOST_UPGRADE:
        case CMD_RADAR_RESEARCH:
            return true;

        // No acting object: pause/resume and the sync probe carry only the issuer, the city
        // commands act on map state and are authorized separately, and auto-repair applies to
        // the issuer's own house.
        case CMD_NONE:
        case CMD_PLAYER_PAUSE:
        case CMD_PLAYER_RESUME:
        case CMD_TEST_SYNC:
        case CMD_CITY_PLACE_ZONE:
        case CMD_CITY_SET_TAX_RATE:
        case CMD_CITY_SET_BUDGET:
        case CMD_CITY_TOOL:
        case CMD_HOUSE_AUTO_REPAIR:
        case CMD_CAMPAIGN_SKIP:
        case CMD_MATCH_PAUSE:
        case CMD_MAX:
        default:
            return false;
    }
}

/// Valid values of the ATTACKMODE enum accepted from a command parameter.
inline bool isValidAttackMode(Uint32 mode) {
    return mode < static_cast<Uint32>(ATTACKMODE_MAX);
}

/**
    A boolean parameter on the wire is exactly 0 or 1. Anything else means the sender is not
    the game, so the command is ignored rather than silently reinterpreted.
    \param  value   the parameter as received
    \return true if the parameter is a well-formed boolean
*/
inline bool isValidBooleanParameter(Uint32 value) {
    return value <= 1;
}

/**
    City zone types come straight off the wire into a ZoneType cast.
    \param  zoneType    the parameter as received
    \return true if it names a real zone type
*/
inline bool isValidCityZoneType(Uint32 zoneType) {
    return zoneType <= 3;   // DuneCity::ZoneType::Industrial
}

/**
    \param  toolType    the parameter as received
    \return true if it names a real city tool
*/
inline bool isValidCityToolType(Uint32 toolType) {
    return toolType <= 2;   // CityTool_Bulldoze, CityTool_Road, CityTool_PowerLine
}

/// Highest city tax rate the budget UI can produce (DuneCity::MAX_TAX_RATE).
constexpr Uint32 kMaxCityTaxRate = 20;

/**
    \param  rate    the parameter as received
    \return true if the tax rate is inside the range the budget window offers
*/
inline bool isValidCityTaxRate(Uint32 rate) {
    return rate <= kMaxCityTaxRate;
}

/**
    \param  percent the parameter as received
    \return true if the police funding percentage is a percentage
*/
inline bool isValidFundingPercent(Uint32 percent) {
    return percent <= 100;
}

} // namespace CommandAuthorization

#endif // COMMANDAUTHORIZATION_H
