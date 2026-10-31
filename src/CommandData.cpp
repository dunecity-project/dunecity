// SPDX-License-Identifier: GPL-2.0-or-later
// Command values and wire serialization are independent of simulation dispatch.
#include <Command.h>
#include <CommandValidation.h>
#include <misc/exceptions.h>

Command::Command(Uint8 playerID, CMDTYPE id)
 : playerID(playerID), commandID(id)
{
}

Command::Command(Uint8 playerID, CMDTYPE id, Uint32 parameter1)
 : playerID(playerID), commandID(id)
{
    parameter.push_back(parameter1);
}

Command::Command(Uint8 playerID, CMDTYPE id, Uint32 parameter1, Uint32 parameter2)
 : playerID(playerID), commandID(id)
{
    parameter.push_back(parameter1);
    parameter.push_back(parameter2);
}

Command::Command(Uint8 playerID, CMDTYPE id, Uint32 parameter1, Uint32 parameter2, Uint32 parameter3)
 : playerID(playerID), commandID(id)
{
    parameter.push_back(parameter1);
    parameter.push_back(parameter2);
    parameter.push_back(parameter3);
}

Command::Command(Uint8 playerID, CMDTYPE id, Uint32 parameter1, Uint32 parameter2, Uint32 parameter3, Uint32 parameter4)
 : playerID(playerID), commandID(id)
{
    parameter.push_back(parameter1);
    parameter.push_back(parameter2);
    parameter.push_back(parameter3);
    parameter.push_back(parameter4);
}

Command::Command(Uint8 playerID, Uint8* data, Uint32 length)
 : playerID(playerID)
{
    if(length % 4 != 0) {
        THROW(std::invalid_argument, "Command::Command(): Length must be multiple of 4!");
    }

    if(length < 4) {
        THROW(std::invalid_argument, "Command::Command(): Command must be at least 4 bytes long!");
    }

    commandID = (CMDTYPE) *((Uint32*) data);

    if(commandID >= CMD_MAX) {
        THROW(std::invalid_argument, "Command::Command(): CommandID unknown!");
    }

    Uint32* pData = (Uint32*) (data+4);
    for(Uint32 i=0;i<(length-4)/4;i++) {
        parameter.push_back(*pData);
        pData++;
    }
}

Command::Command(InputStream& stream) {
    playerID = stream.readUint8();
    const Uint32 rawCommandID = stream.readUint32();
    if(!CommandValidation::isKnownCommandID(rawCommandID)) {
        // Command::executeCommand() throws on an unknown id, and that throw escapes the
        // simulation loop. Refuse the command while we are still parsing, so the caller
        // (network receive or replay load) can drop it instead of crashing later.
        throw InputStream::error("Command::Command(): CommandID unknown!");
    }
    commandID = (CMDTYPE) rawCommandID;
    parameter = stream.readUint32Vector();
}

Command::~Command() = default;

void Command::save(OutputStream& stream) const {
    stream.writeUint8(playerID);
    stream.writeUint32((Uint32) commandID);
    stream.writeUint32Vector(parameter);
    stream.flush();
}

