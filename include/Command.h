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

#ifndef COMMAND_H
#define COMMAND_H

#include <misc/InputStream.h>
#include <misc/OutputStream.h>

#include <vector>
#include <misc/SDL2pp.h>

typedef enum {
    CMD_NONE,
    CMD_PLACE_STRUCTURE,                ///< PLACE_STRUCTURE(BUILDER_ID, X, Y)
    CMD_UNIT_MOVE2POS,                  ///< UNIT_MOVE2POS(OBJECT_ID,X,Y,BFORCED)
    CMD_UNIT_MOVE2OBJECT,               ///< UNIT_MOVE2OBJECT(OBJECT_ID,TARGET_OBJECT_ID)
    CMD_UNIT_ATTACKPOS,                 ///< UNIT_ATTACKPOS(OBJECT_ID,X,Y)
    CMD_UNIT_ATTACKOBJECT,              ///< UNIT_ATTACKOBJECT(OBJECT_ID,TARGET_OBJECT_ID)
    CMD_UNIT_HEAL,                      ///< UNIT_HEAL(OBJECT_ID,TARGET_OBJECT_ID)
    CMD_INFANTRY_CAPTURE,               ///< INFANTRY_CAPTURE(OBJECT_ID,TARGET_STRUCTURE_ID)
    CMD_UNIT_REQUESTCARRYALLDROP,       ///< UNIT_REQUESTCARRYALLDROP(OBJECT_ID,X,Y)
    CMD_UNIT_SENDTOREPAIR,              ///< UNIT_SENDTOREPAIR(OBJECT_ID)
    CMD_UNIT_SETMODE,                   ///< UNIT_SETMODE(OBJECT_ID,MODE)
    CMD_DEVASTATOR_STARTDEVASTATE,      ///< DEVASTATOR_STARTDEVASTATE(OBJECT_ID)
    CMD_MCV_DEPLOY,                     ///< MCV_DEPLOY(OBJECT_ID)
    CMD_HARVESTER_RETURN,               ///< HARVESTER_RETURN(OBJECT_ID)
    CMD_STRUCTURE_SETDEPLOYPOSITION,    ///< STRUCTURE_SETDEPLOYPOSITION(OBJECT_ID,X,Y)
    CMD_STRUCTURE_REPAIR,               ///< STRUCTURE_REPAIR(OBJECT_ID)
    CMD_BUILDER_UPGRADE,                ///< BUILDER_UPGRADE(OBJECT_ID,B_START)
    CMD_BUILDER_PRODUCEITEM,            ///< BUILDER_PRODUCEITEM(OBJECT_ID,ITEM_ID,B_MULTIMODE)
    CMD_BUILDER_CANCELITEM,             ///< BUILDER_PRODUCEITEM(OBJECT_ID,ITEM_ID,B_MULTIMODE)
    CMD_BUILDER_SETONHOLD,              ///< BUILDER_SETONHOLD(OBJECT_ID, B_HOLD)
    CMD_PALACE_SPECIALWEAPON,           ///< PALACE_SPECIALWEAPON(OBJECT_ID)
    CMD_PALACE_DEATHHAND,               ///< PALACE_DEATHHAND(OBJECT_ID, X, Y)
    CMD_STARPORT_PLACEORDER,            ///< CMD_STARPORT_PLACEORDER(OBJECT_ID)
    CMD_STARPORT_CANCELORDER,           ///< CMD_STARPORT_CANCELORDER(OBJECT_ID)
    CMD_TURRET_ATTACKOBJECT,            ///< TURRET_ATTACKOBJECT(OBJECT_ID,TARGET_OBJECT_ID)
    CMD_PLAYER_PAUSE,                   ///< CMD_PLAYER_PAUSE() - notify other players this player paused
    CMD_PLAYER_RESUME,                  ///< CMD_PLAYER_RESUME() - notify other players this player resumed
    CMD_TEST_SYNC,                      ///< TEST_SYNC(SEED)

    // DuneCity commands
    CMD_CITY_PLACE_ZONE,                ///< CMD_CITY_PLACE_ZONE(X, Y, ZONE_TYPE)
    CMD_CITY_SET_TAX_RATE,              ///< CMD_CITY_SET_TAX_RATE(HOUSE_ID, RATE)
    CMD_CITY_SET_BUDGET,                ///< CMD_CITY_SET_BUDGET(POLICE_PCT, RESERVED, RESERVED)
    CMD_CITY_TOOL,                      ///< CMD_CITY_TOOL(X, Y, TOOL_TYPE) - bulldoze, road, power line, etc.
    CMD_TECHCENTER_SPAWN,               ///< TECHCENTER_SPAWN(OBJECT_ID)
    CMD_SCOUTPOST_UPGRADE,              ///< SCOUTPOST_UPGRADE(OBJECT_ID)
    CMD_SCOUTPOST_CHEMIPOST_UPGRADE,    ///< SCOUTPOST_CHEMIPOST_UPGRADE(OBJECT_ID)

    CMD_HOUSE_AUTO_REPAIR,             ///< HOUSE_AUTO_REPAIR(ENABLED), issuing player's house only
    CMD_POLICE_REINFORCEMENTS,         ///< POLICE_REINFORCEMENTS(OBJECT_ID)
    CMD_ZONE_DEMOLISH,                 ///< ZONE_DEMOLISH(OBJECT_ID), issuing house only
    CMD_STRUCTURE_DEMOLISH,            ///< STRUCTURE_DEMOLISH(OBJECT_ID), issuing house only

    CMD_CAMPAIGN_SKIP,                ///< CAMPAIGN_SKIP(), human campaign-house controllers only

    CMD_MATCH_PAUSE,                  ///< MATCH_PAUSE(), any active human; freezes after this tick

    // Modern RTS controls (docs/rts-controls.md)
    CMD_UNIT_ATTACKMOVE,              ///< UNIT_ATTACKMOVE(OBJECT_ID, X, Y, B_QUEUE)
    CMD_UNIT_PATROL,                  ///< UNIT_PATROL(OBJECT_ID, X, Y, B_QUEUE)
    CMD_UNIT_QUEUEORDER,              ///< UNIT_QUEUEORDER(OBJECT_ID, ORDER_TYPE, X_OR_TARGET_ID, Y) Shift-queued order
    CMD_BUILDER_SETREPEAT,            ///< BUILDER_SETREPEAT(OBJECT_ID, ITEM_ID, B_ENABLED)
    CMD_BUILDER_RESERVE_PLACEMENT,    ///< BUILDER_RESERVE_PLACEMENT(BUILDER_ID, ITEM_ID, X, Y), queues the structure and reserves its plot
    CMD_BUILDER_CANCEL_RESERVATION,   ///< BUILDER_CANCEL_RESERVATION(BUILDER_ID, RESERVATION_ID), cancels that reserved order with the normal refund
    CMD_UNIT_FORMATION_MOVE,          ///< UNIT_FORMATION_MOVE(OBJECT_ID,X,Y,FACING<<28|GROUP_TOKEN), forced move to a formation slot (docs/formations.md)
    CMD_UNIT_FORMATION_HUNT,          ///< UNIT_FORMATION_HUNT(OBJECT_ID,X,Y,FACING<<28|KIND<<24|GROUP_TOKEN), attack-move (kind 1) or patrol (kind 2) to a slot, hunting in formation

    CMD_MAX
} CMDTYPE;

/**
    This class represents one command with all its parameters. The command is specified by CommandID (see CMDTYPE)
    and Parameter holds all its parameters (see the documentation for every CMDTYPE). There can be up to 4 parameters
*/
class Command {
public:

    /**
        Construct a command with CMDTYPE id and no parameter.
        \param  id  the id of the command
    */
    Command(Uint8 playerID, CMDTYPE id);

    /**
        Construct a command with CMDTYPE id and one parameter.
        \param  id          the id of the command
        \param  parameter1  the first parameter
    */
    Command(Uint8 playerID, CMDTYPE id, Uint32 parameter1);

    /**
        Construct a command with CMDTYPE id and two parameters.
        \param  id          the id of the command
        \param  parameter1  the first parameter
        \param  parameter2  the second parameter
    */
    Command(Uint8 playerID, CMDTYPE id, Uint32 parameter1, Uint32 parameter2);

    /**
        Construct a command with CMDTYPE id and three parameters.
        \param  id          the id of the command
        \param  parameter1  the first parameter
        \param  parameter2  the second parameter
        \param  parameter3  the third parameter
    */
    Command(Uint8 playerID, CMDTYPE id, Uint32 parameter1, Uint32 parameter2, Uint32 parameter3);

    /**
        Construct a command with CMDTYPE id and four parameters.
        \param  id          the id of the command
        \param  parameter1  the first parameter
        \param  parameter2  the second parameter
        \param  parameter3  the third parameter
        \param  parameter4  the forth parameter
    */
    Command(Uint8 playerID, CMDTYPE id, Uint32 parameter1, Uint32 parameter2, Uint32 parameter3, Uint32 parameter4);

    /**
        Construct a command from raw memory.
        \param  data        pointer to the data
        \param  length      length of the data
    */
    Command(Uint8 playerID, Uint8* data, Uint32 length);

    /**
        Read a command from stream.
        \param  stream  the stream to read from
    */
    explicit Command(InputStream& stream);

    /// destructor
    virtual ~Command();

    /**
        Writes the command to a stream.
        \param  stream  the stream to write to
    */
    void save(OutputStream& stream) const;

    /**
        Gets the ID of the player that added this command.
        \return the ID of the player
    */
    Uint8 getPlayerID() const { return playerID; };

    /**
        Gets the ID of this command.
        \return the ID of this command
    */
    CMDTYPE getCommandID() const { return commandID; };

    /**
        Gets the parameters of this command.
        \return the parameters of this command
    */
    const std::vector<Uint32> getParameter() const { return parameter; };


    /**
        Executes this command. This takes the appropriate actions to run this command.
    */
    void executeCommand() const;

private:
    Uint8   playerID;                   ///< the ID of the player that gave the command
    CMDTYPE commandID;                  ///< the type of command
    std::vector<Uint32> parameter;      ///< the parameters for this command
};

#endif // COMMAND_H
