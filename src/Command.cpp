#include <structures/ZoneStructure.h>
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

#include <Command.h>
#include <CommandAuthorization.h>
#include <CommandValidation.h>
#include <FormationOrders.h>
#include <misc/CampaignControls.h>

#include <globals.h>

#include <Game.h>
#include <House.h>
#include <Map.h>
#include <players/HumanPlayer.h>
#include <players/QuantBot.h>
#include <structures/PoliceStation.h>

#include <misc/exceptions.h>

#include <units/UnitBase.h>
#include <units/GroundUnit.h>
#include <units/Carryall.h>
#include <units/Devastator.h>
#include <units/MCV.h>
#include <units/Harvester.h>
#include <units/HarvesterHelpers.h>
#include <units/InfantryBase.h>
#include <structures/BuilderBase.h>
#include <structures/TurretBase.h>
#include <structures/Palace.h>
#include <structures/TechCenter.h>
#include <structures/Scoutpost.h>
#include <structures/StarPort.h>
#include <structures/ConstructionYard.h>

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

namespace {

/**
    Builds the authorization facts for a command that acts on an object.

    The decision must be identical on every peer, so it only looks at simulation state: the
    issuing player, the object, and the object's owner. pObject is the result of the command's
    own dynamic_cast, so a null pointer here means either "no such object" or "wrong type";
    the raw lookup separates the two for the log.

    \param  playerID    the player the command claims to come from
    \param  objectID    parameter 0 of the command
    \param  pObject     the object after the command's own cast, or nullptr
    \return the gathered facts
*/
CommandAuthorization::ActorContext makeActorContext(Uint8 playerID, Uint32 objectID,
                                                    const ObjectBase* pObject) {
    CommandAuthorization::ActorContext context;

    const Player* pIssuer = currentGame->getPlayerByID(playerID);
    context.issuerExists = (pIssuer != nullptr);
    if(pIssuer != nullptr && pIssuer->getHouse() != nullptr) {
        context.issuerHasHouse = true;
        context.issuerHouseID = pIssuer->getHouse()->getHouseID();
    }

    const ObjectBase* pRawObject = currentGame->getObjectManager().getObject(objectID);
    context.objectExists = (pRawObject != nullptr);
    context.objectTypeMatches = (pObject != nullptr);

    if(pObject != nullptr && pObject->getOwner() != nullptr) {
        context.objectHasOwner = true;
        context.objectOwnerHouseID = pObject->getOwner()->getHouseID();
    }

    return context;
}

/**
    Authorizes one object action and logs refusals at a bounded rate.
    \param  playerID    the player the command claims to come from
    \param  commandID   the command being executed
    \param  objectID    parameter 0 of the command
    \param  pObject     the object after the command's own cast, or nullptr
    \return true if the command may run
*/
bool mayActOnObject(Uint8 playerID, CMDTYPE commandID, Uint32 objectID, const ObjectBase* pObject) {
    const CommandAuthorization::ActorContext context = makeActorContext(playerID, objectID, pObject);
    const CommandAuthorization::Decision decision = CommandAuthorization::authorizeActor(context);

    if(decision == CommandAuthorization::Decision::Allow) {
        return true;
    }

    // Missing or already destroyed objects are ordinary in a lockstep game (the order was
    // given a few cycles ago); only an ownership or type violation is worth reporting, and
    // even then at a bounded rate so a hostile peer cannot spin the log.
    if(decision == CommandAuthorization::Decision::NotOwner
       || decision == CommandAuthorization::Decision::WrongObjectType) {
        static Uint32 lastRefusalCycle = 0;
        const Uint32 cycle = currentGame->getGameCycleCount();
        if(cycle != lastRefusalCycle) {
            lastRefusalCycle = cycle;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Command: player %u may not act on object %u with command %d",
                        static_cast<unsigned int>(playerID), objectID, static_cast<int>(commandID));
        }
    }

    return false;
}

} // namespace

void Command::executeCommand() const {
    // The detached observer identity (Game::setupSpectatorView) holds a reserved controller id
    // that House::addPlayer can never mint, so it is never registered. Most commands already
    // refuse it through mayActOnObject/executeCityCommand, which both require the issuer to
    // resolve - but a few act on the raw id before any such check: CMD_PLAYER_PAUSE inserts
    // whatever id it is given into pausedPlayers, and a future command could do the same. Drop
    // the reserved id here, ahead of every mutation and every parameter check, so observation
    // is read-only by construction rather than by each case remembering to look.
    //
    // Deliberately only this one reserved value: unknown or system ids are left exactly as they
    // were, the wire format is untouched, and no AI or human controller is affected.
    if(playerID == Player::OBSERVER_PLAYER_ID) {
        return;
    }

    // This path is replayed on every peer. AI do* helpers bypass it, so these
    // leases distinguish actual player control from old forced AI movement.
    const bool unitOrder=commandID==CMD_UNIT_MOVE2POS || commandID==CMD_UNIT_MOVE2OBJECT
        || commandID==CMD_UNIT_ATTACKPOS || commandID==CMD_UNIT_ATTACKOBJECT
        || commandID==CMD_UNIT_SETMODE || commandID==CMD_UNIT_SENDTOREPAIR
        || commandID==CMD_UNIT_REQUESTCARRYALLDROP || commandID==CMD_UNIT_HEAL
        || commandID==CMD_UNIT_ATTACKMOVE || commandID==CMD_UNIT_PATROL || commandID==CMD_UNIT_QUEUEORDER
        || commandID==CMD_UNIT_FORMATION_MOVE;
    const auto* human=dynamic_cast<const HumanPlayer*>(currentGame->getPlayerByID(playerID));
    if (unitOrder && human && !parameter.empty()) {
        const auto* unit=dynamic_cast<const UnitBase*>(currentGame->getObjectManager().getObject(parameter[0]));
        if (unit && unit->getOwner()==human->getHouse())
            for (const auto& player:unit->getOwner()->getPlayerList())
                if (auto* bot=dynamic_cast<QuantBot*>(player.get())) bot->onHumanUnitOrder(unit->getObjectID());
    }

    // Only accepted human orders reset the stall clock, and this executes on every peer.
    // AI helpers deliberately leave repeated identical routing intents on the same clock.
    // Any explicit movement order ends HOLD (docs/rts-controls.md) and the unit's formation
    // membership; a formation move then joins anew (docs/formations.md).
    const auto applyMovementOrder = [human](UnitBase* unit, bool validIntent, const auto& action) {
        if(human && validIntent) unit->cancelCarryallPickup();
        if(validIntent) unit->leaveHoldForOrder();
        unit->leaveFormation();
        action();
        if(human && validIntent) unit->noteExplicitPlayerOrder();
    };

    switch(commandID) {

        case CMD_PLACE_STRUCTURE: {
            if(parameter.size() != 3) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_PLACE_STRUCTURE needs 3 Parameters!");
            }
            ConstructionYard* pConstYard = dynamic_cast<ConstructionYard*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pConstYard)) {
                return;
            }
            pConstYard->doPlaceStructure((int) parameter[1], (int) parameter[2]);
        } break;


        case CMD_UNIT_MOVE2POS: {
            if(parameter.size() != 4) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_UNIT_MOVE2POS needs 4 Parameters!");
            }
            UnitBase* unit = dynamic_cast<UnitBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], unit)) {
                return;
            }
            if(!CommandAuthorization::isValidBooleanParameter(parameter[3])) {
                return;
            }
            const bool validIntent = currentGameMap->tileExists((int) parameter[1], (int) parameter[2]);
            applyMovementOrder(unit, validIntent, [&] {
                unit->doMove2Pos((int) parameter[1], (int) parameter[2], (bool) parameter[3]);
            });
            // An unshifted order replaces the queue and becomes its front, so a later Shift
            // order runs after it.
            if(validIntent) unit->recordPlayerOrder(UnitOrder::at(UnitOrderType::Move, Coord((int) parameter[1], (int) parameter[2])));
        } break;

        case CMD_UNIT_MOVE2OBJECT: {
            if(parameter.size() != 2) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_UNIT_MOVE2OBJECT needs 2 Parameters!");
            }
            UnitBase* unit = dynamic_cast<UnitBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], unit)) {
                return;
            }
            const auto* target = currentGame->getObjectManager().getObject(parameter[1]);
            const bool validIntent = target != nullptr && target != unit;
            applyMovementOrder(unit, validIntent, [&] {
                unit->doMove2Object((int) parameter[1]);
            });
            if(validIntent) unit->recordPlayerOrder(UnitOrder::on(UnitOrderType::MoveToObject, parameter[1]));
        } break;

        case CMD_UNIT_ATTACKPOS: {
            if(parameter.size() != 4) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_UNIT_ATTACKPOS needs 4 Parameters!");
            }
            UnitBase* unit = dynamic_cast<UnitBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], unit)) {
                return;
            }
            if(!CommandAuthorization::isValidBooleanParameter(parameter[3])) {
                return;
            }
            const bool validIntent = currentGameMap->tileExists((int) parameter[1], (int) parameter[2]);
            applyMovementOrder(unit, validIntent, [&] {
                unit->doAttackPos((int) parameter[1], (int) parameter[2], (bool) parameter[3]);
            });
            // Firing at the ground has no end, so nothing can be queued behind it.
            if(validIntent) unit->clearOrderQueue();
        } break;

        case CMD_UNIT_ATTACKOBJECT: {
            if(parameter.size() != 2) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_UNIT_ATTACKOBJECT needs 2 Parameters!");
            }
            UnitBase* pUnit = dynamic_cast<UnitBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pUnit)) {
                return;
            }
            const auto* target = currentGame->getObjectManager().getObject(parameter[1]);
            const bool validIntent = target != nullptr && target != pUnit
                && (pUnit->canAttack() || isHarvesterLikeUnit(pUnit->getItemID()));
            applyMovementOrder(pUnit, validIntent, [&] {
                pUnit->doAttackObject((int) parameter[1], true);
            });
            if(validIntent) pUnit->recordPlayerOrder(UnitOrder::on(UnitOrderType::AttackObject, parameter[1]));
        } break;

                case CMD_UNIT_HEAL: {
            if(parameter.size() != 2) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_UNIT_HEAL needs 2 Parameters!");
            }
            UnitBase* pUnit = dynamic_cast<UnitBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pUnit)) {
                return;
            }
            ObjectBase* pTarget = currentGame->getObjectManager().getObject(parameter[1]);
            if(pTarget == nullptr || !pUnit->canHeal() || !pTarget->isAUnit()
                    || pTarget->getOwner()->getTeamID() != pUnit->getOwner()->getTeamID()
                    || pTarget->getHealth() >= pTarget->getMaxHealth()) {
                return;
            }
            applyMovementOrder(pUnit, pTarget != pUnit, [&] {
                pUnit->doAttackObject((int) parameter[1], true);
            });
            if(pTarget != pUnit) pUnit->clearOrderQueue();
        } break;
case CMD_INFANTRY_CAPTURE: {
            if(parameter.size() != 2) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_INFANTRY_CAPTURE needs 2 Parameters!");
            }
            InfantryBase* pInfantry = dynamic_cast<InfantryBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pInfantry)) {
                return;
            }
            const auto* pStructure = dynamic_cast<const StructureBase*>(currentGame->getObjectManager().getObject(parameter[1]));
            const bool validIntent = pStructure != nullptr && pStructure->canBeCaptured()
                && pStructure->getOwner()->getTeamID() != pInfantry->getOwner()->getTeamID();
            if(validIntent) pInfantry->leaveHoldForOrder();
            pInfantry->leaveFormation();
            pInfantry->doCaptureStructure((int) parameter[1]);
            if(validIntent) pInfantry->recordPlayerOrder(UnitOrder::on(UnitOrderType::Capture, parameter[1]));
        } break;

        case CMD_UNIT_REQUESTCARRYALLDROP: {
            if(parameter.size() != 3) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_UNIT_REQUESTCARRYALLDROP needs 3 Parameters!");
            }
            GroundUnit* pGroundUnit = dynamic_cast<GroundUnit*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pGroundUnit)) {
                return;
            }
            const bool validIntent = currentGameMap->tileExists((int) parameter[1], (int) parameter[2]);
            applyMovementOrder(pGroundUnit, validIntent, [&] {
                pGroundUnit->doRequestCarryallDrop((int) parameter[1], (int) parameter[2]);
            });
            if(validIntent) pGroundUnit->clearOrderQueue();
        } break;

        case CMD_UNIT_SENDTOREPAIR: {
            if(parameter.size() != 1) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_UNIT_SENDTOREPAIR needs 1 Parameter!");
            }
            GroundUnit* pGroundUnit = dynamic_cast<GroundUnit*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pGroundUnit)) {
                return;
            }
            pGroundUnit->clearOrderQueue();
            pGroundUnit->leaveHoldForOrder();
            pGroundUnit->leaveFormation();
            pGroundUnit->doRepair();
        } break;

        case CMD_UNIT_SETMODE: {
            if(parameter.size() != 2) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_UNIT_SETMODE needs 2 Parameter!");
            }
            UnitBase* pUnit = dynamic_cast<UnitBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pUnit)) {
                return;
            }
            if(!CommandAuthorization::isValidAttackMode(parameter[1])) {
                return;
            }
            // Stop, Hold and every other mode change end the order queue.
            pUnit->clearOrderQueue();
            pUnit->leaveFormation();
            pUnit->doSetAttackMode((ATTACKMODE) parameter[1]);
            if(human) pUnit->noteExplicitPlayerOrder();
        } break;

        case CMD_DEVASTATOR_STARTDEVASTATE: {
            if(parameter.size() != 1) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_DEVASTATOR_STARTDEVASTATE needs 1 Parameter!");
            }
            Devastator* pDevastator = dynamic_cast<Devastator*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pDevastator)) {
                return;
            }
            pDevastator->clearOrderQueue();
            pDevastator->doStartDevastate();
        } break;

        case CMD_MCV_DEPLOY: {
            if(parameter.size() != 1) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_MCV_DEPLOY needs 1 Parameter!");
            }
            MCV* pMCV = dynamic_cast<MCV*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pMCV)) {
                return;
            }
            pMCV->doDeploy();
        } break;

        case CMD_HARVESTER_RETURN: {
            if(parameter.size() != 1) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_HARVESTER_RETURN needs 1 Parameter!");
            }
            ObjectBase* pHarvester = currentGame->getObjectManager().getObject(parameter[0]);
            if(!isHarvesterLikeObject(pHarvester)
               || !mayActOnObject(playerID, commandID, parameter[0], pHarvester)) {
                return;
            }
            if(auto* pUnit = dynamic_cast<UnitBase*>(pHarvester)) pUnit->leaveFormation();
            harvesterDoReturn(pHarvester);
            if(auto* pUnit = dynamic_cast<UnitBase*>(pHarvester)) {
                pUnit->recordPlayerOrder(UnitOrder::on(UnitOrderType::HarvesterReturn, NONE_ID));
            }
        } break;

        case CMD_STRUCTURE_SETDEPLOYPOSITION: {
            if(parameter.size() != 3) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_STRUCTURE_SETDEPLOYPOSITION needs 3 Parameters!");
            }
            StructureBase* pStructure = dynamic_cast<StructureBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pStructure)) {
                return;
            }
            pStructure->doSetDeployPosition((int) parameter[1],(int) parameter[2]);
        } break;

        case CMD_STRUCTURE_REPAIR: {
            if(parameter.size() != 1) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_STRUCTURE_REPAIR needs 1 Parameter!");
            }
            StructureBase* pStructure = dynamic_cast<StructureBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pStructure)) {
                return;
            }
            pStructure->doRepair();
        } break;

        case CMD_BUILDER_UPGRADE: {
            if(parameter.size() != 1) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_BUILDER_UPGRADE needs 1 Parameter!");
            }
            BuilderBase* pBuilder = dynamic_cast<BuilderBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pBuilder)) {
                return;
            }
            pBuilder->doUpgrade();
        } break;

        case CMD_BUILDER_PRODUCEITEM: {
            if(parameter.size() != 3) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_BUILDER_PRODUCEITEM needs 3 Parameter!");
            }
            BuilderBase* pBuilder = dynamic_cast<BuilderBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pBuilder)) {
                return;
            }
            if(!CommandAuthorization::isValidBooleanParameter(parameter[2])) {
                return;
            }
            pBuilder->doProduceItem(parameter[1],(bool) parameter[2]);
        } break;

        case CMD_BUILDER_CANCELITEM: {
            if(parameter.size() != 3) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_BUILDER_CANCELITEM needs 3 Parameter!");
            }
            BuilderBase* pBuilder = dynamic_cast<BuilderBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pBuilder)) {
                return;
            }
            if(!CommandAuthorization::isValidBooleanParameter(parameter[2])) {
                return;
            }
            pBuilder->doCancelItem(parameter[1],(bool) parameter[2]);
        } break;

        case CMD_BUILDER_SETONHOLD: {
            if(parameter.size() != 2) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_BUILDER_SETONHOLD needs 2 Parameters!");
            }
            BuilderBase* pBuilder = dynamic_cast<BuilderBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pBuilder)) {
                return;
            }
            if(!CommandAuthorization::isValidBooleanParameter(parameter[1])) {
                return;
            }
            pBuilder->doSetOnHold((bool) parameter[1]);
        } break;

        case CMD_PALACE_SPECIALWEAPON: {
            if(parameter.size() != 1) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_PALACE_SPECIALWEAPON needs 1 Parameter!");
            }
            Palace* palace = dynamic_cast<Palace*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], palace)) {
                return;
            }
            palace->doSpecialWeapon();
        } break;

        case CMD_PALACE_DEATHHAND: {
            if(parameter.size() != 3) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_PALACE_DEATHHAND needs 3 Parameter!");
            }
            Palace* palace = dynamic_cast<Palace*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], palace)) {
                return;
            }
            palace->doLaunchDeathhand((int) parameter[1], (int) parameter[2]);
        } break;

        case CMD_STARPORT_PLACEORDER: {
            if(parameter.size() != 1) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_STARPORT_PLACEORDER needs 1 Parameter!");
            }
            StarPort* pStarport = dynamic_cast<StarPort*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pStarport)) {
                return;
            }
            pStarport->doPlaceOrder();
        } break;

        case CMD_STARPORT_CANCELORDER: {
            if(parameter.size() != 1) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_STARPORT_CANCELORDER needs 1 Parameter!");
            }
            StarPort* pStarport = dynamic_cast<StarPort*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pStarport)) {
                return;
            }
            pStarport->doCancelOrder();
        } break;

        case CMD_TURRET_ATTACKOBJECT: {
            if(parameter.size() != 2) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_TURRET_ATTACKOBJECT needs 2 Parameters!");
            }
            TurretBase* pTurret = dynamic_cast<TurretBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pTurret)) {
                return;
            }
            pTurret->doAttackObject((int) parameter[1]);
        } break;

        case CMD_TECHCENTER_SPAWN: {
            if(parameter.size() != 1) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_TECHCENTER_SPAWN needs 1 Parameter!");
            }
            TechCenter* pTechCenter = dynamic_cast<TechCenter*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pTechCenter)) {
                return;
            }
            pTechCenter->doSpawnVehicles();
        } break;

        case CMD_SCOUTPOST_UPGRADE: {
            if(parameter.size() != 1) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_SCOUTPOST_UPGRADE needs 1 Parameter!");
            }
            Scoutpost* pScoutpost = dynamic_cast<Scoutpost*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pScoutpost)) {
                return;
            }
            pScoutpost->doUpgradeToFlamepost();
        } break;

        case CMD_SCOUTPOST_CHEMIPOST_UPGRADE: {
            if(parameter.size() != 1) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_SCOUTPOST_CHEMIPOST_UPGRADE needs 1 Parameter!");
            }
            Scoutpost* pScoutpost = dynamic_cast<Scoutpost*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pScoutpost)) {
                return;
            }
            pScoutpost->doUpgradeToChemipost();
        } break;
        
        case CMD_MATCH_PAUSE: {
            if (parameter.size() != 1) return;
            currentGame->executeMatchPause(playerID, parameter[0]);
        } break;

        case CMD_UNIT_ATTACKMOVE:
        case CMD_UNIT_PATROL: {
            if(parameter.size() != 4) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_UNIT_ATTACKMOVE/CMD_UNIT_PATROL need 4 Parameters!");
            }
            UnitBase* unit = dynamic_cast<UnitBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], unit)) {
                return;
            }
            if(!CommandAuthorization::isValidBooleanParameter(parameter[3])) {
                return;
            }
            const Coord point((int) parameter[1], (int) parameter[2]);
            if(!currentGameMap->tileExists(point)) {
                return;
            }
            const bool queued = parameter[3] != 0;
            if(queued && !unit->getOrderQueue().empty()) {
                // Waits behind the current orders; it takes over the unit only when it starts.
                if(commandID == CMD_UNIT_ATTACKMOVE) unit->doIssueOrder(UnitOrder::at(UnitOrderType::AttackMove, point), true);
                else unit->doIssuePatrol(point, true);
            } else {
                applyMovementOrder(unit, true, [&] {
                    if(commandID == CMD_UNIT_ATTACKMOVE) unit->doIssueOrder(UnitOrder::at(UnitOrderType::AttackMove, point), queued);
                    else unit->doIssuePatrol(point, queued);
                });
            }
        } break;

        case CMD_UNIT_QUEUEORDER: {
            if(parameter.size() != 4) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_UNIT_QUEUEORDER needs 4 Parameters!");
            }
            UnitBase* unit = dynamic_cast<UnitBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], unit)) {
                return;
            }
            UnitOrder order;
            switch(static_cast<UnitOrderType>(parameter[1])) {
                case UnitOrderType::Move: {
                    if(!currentGameMap->tileExists((int) parameter[2], (int) parameter[3])) return;
                    order = UnitOrder::at(UnitOrderType::Move, Coord((int) parameter[2], (int) parameter[3]));
                } break;
                case UnitOrderType::MoveToObject:
                case UnitOrderType::AttackObject:
                case UnitOrderType::Capture: {
                    order = UnitOrder::on(static_cast<UnitOrderType>(parameter[1]), parameter[2]);
                } break;
                case UnitOrderType::HarvesterReturn: {
                    if(!isHarvesterLikeUnit(unit->getItemID())) return;
                    order = UnitOrder::on(UnitOrderType::HarvesterReturn, NONE_ID);
                } break;
                // Attack-move and patrol have their own commands; anything else is malformed.
                default:
                    return;
            }
            // A queued order on an idle unit starts at once; its dispatch ends HOLD and any lift.
            unit->doIssueOrder(order, true);
        } break;

        case CMD_BUILDER_SETREPEAT: {
            if(parameter.size() != 3) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_BUILDER_SETREPEAT needs 3 Parameters!");
            }
            BuilderBase* pBuilder = dynamic_cast<BuilderBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pBuilder)) {
                return;
            }
            if(!CommandAuthorization::isValidBooleanParameter(parameter[2])) {
                return;
            }
            pBuilder->doSetRepeat(parameter[1], parameter[2] != 0);
        } break;

        case CMD_BUILDER_RESERVE_PLACEMENT: {
            if(parameter.size() != 4) return;
            ConstructionYard* pConstYard = dynamic_cast<ConstructionYard*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pConstYard)) {
                return;
            }
            // Coordinates arrive as Uint32; a plot off the map is refused by the plot check.
            pConstYard->doReservePlacement(parameter[1], static_cast<int>(parameter[2]), static_cast<int>(parameter[3]));
        } break;

        case CMD_BUILDER_CANCEL_RESERVATION: {
            if(parameter.size() != 2) return;
            ConstructionYard* pConstYard = dynamic_cast<ConstructionYard*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], pConstYard)) {
                return;
            }
            pConstYard->doCancelReservation(parameter[1]);
        } break;

        case CMD_UNIT_FORMATION_MOVE: {
            if(parameter.size() != 4) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_UNIT_FORMATION_MOVE needs 4 Parameters!");
            }
            UnitBase* unit = dynamic_cast<UnitBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if(!mayActOnObject(playerID, commandID, parameter[0], unit)) {
                return;
            }
            // A malformed token or a slot off the map is not something the game sends.
            if(!FormationPlanner::isValidToken(parameter[3])
               || !currentGameMap->tileExists((int) parameter[1], (int) parameter[2])) {
                return;
            }
            Coord destination((int) parameter[1], (int) parameter[2]);
            applyMovementOrder(unit, true, [&] {
                if(!FormationOrders::eligible(unit)) {
                    // Aircraft and units off the map keep the ordinary forced move.
                    unit->doMove2Pos(destination.x, destination.y, true);
                    return;
                }
                // The slot may have become unusable (a building went up) between the plan and
                // this cycle: every peer moves it to the same nearest usable tile.
                destination = FormationOrders::revalidatedSlot(unit, destination);
                unit->doMove2Pos(destination.x, destination.y, true);
                FormationOrders::join({unit}, FormationPlanner::makeGroupID(playerID, parameter[3]), {destination});
            });
            // Like a plain move, it replaces the order queue and becomes its front.
            unit->recordPlayerOrder(UnitOrder::at(UnitOrderType::Move, destination));
        } break;

        case CMD_PLAYER_PAUSE: {
            if(parameter.size() != 0) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_PLAYER_PAUSE needs 0 Parameters!");
            }
            
            // Mark this player as paused
            currentGame->pausedPlayers.insert(playerID);
            
            // Get player name
            Player* pPlayer = currentGame->getPlayerByID(playerID);
            std::string playerName = pPlayer ? pPlayer->getPlayername() : "Player " + std::to_string((int)playerID);
            
            SDL_Log("Player %s (ID %d) has paused", playerName.c_str(), (int)playerID);
            
            // If it's a remote player, show a message
            Player* pLocalPlayer = currentGame->getPlayerByName(currentGame->getLocalPlayerName());
            if(pLocalPlayer && playerID != pLocalPlayer->getPlayerID()) {
                currentGame->addToNewsTicker(playerName + " is paused");
            }
        } break;
        
        case CMD_PLAYER_RESUME: {
            if(parameter.size() != 0) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_PLAYER_RESUME needs 0 Parameters!");
            }
            
            // Remove this player from paused set
            currentGame->pausedPlayers.erase(playerID);
            
            // Get player name
            Player* pPlayer = currentGame->getPlayerByID(playerID);
            std::string playerName = pPlayer ? pPlayer->getPlayername() : "Player " + std::to_string((int)playerID);
            
            SDL_Log("Player %s (ID %d) has resumed", playerName.c_str(), (int)playerID);
            
            // If it's a remote player, show a message
            Player* pLocalPlayer = currentGame->getPlayerByName(currentGame->getLocalPlayerName());
            if(pLocalPlayer && playerID != pLocalPlayer->getPlayerID()) {
                currentGame->addToNewsTicker(playerName + " resumed");
            }
        } break;

        case CMD_TEST_SYNC: {
            if(parameter.size() != 1) {
                THROW(std::invalid_argument, "Command::executeCommand(): CMD_TEST_SYNC needs 1 Parameters!");
            }

            Uint32 currentSeed = currentGame->randomGen.getSeed();
            if(currentSeed != parameter[0]) {
                SDL_Log("Warning: Game is asynchronous in game cycle %d! Saved seed and current seed do not match: %ud != %ud", currentGame->getGameCycleCount(), parameter[0], currentSeed);
#ifdef TEST_SYNC
                currentGame->saveGame("test.sav");
                exit(0);
#endif
            }
        } break;

        case CMD_CITY_PLACE_ZONE:
        case CMD_CITY_SET_TAX_RATE:
        case CMD_CITY_SET_BUDGET:
        case CMD_CITY_TOOL: {
            DuneCity::CitySimulation::executeCityCommand(
                playerID, commandID,
                parameter.size() > 0 ? parameter[0] : 0,
                parameter.size() > 1 ? parameter[1] : 0,
                parameter.size() > 2 ? parameter[2] : 0);
        } break;

        case CMD_CAMPAIGN_SKIP: {
            if(!parameter.empty()) return;
            const auto* issuer = dynamic_cast<const HumanPlayer*>(currentGame->getPlayerByID(playerID));
            const auto& init = currentGame->getGameInitSettings();
            if(CampaignControls::maySkip(init.getGameType(), init.getHouseID(),
                    issuer && issuer->getHouse() ? issuer->getHouse()->getHouseID() : -1,
                    issuer != nullptr)) {
                currentGame->setGameWon();
            }
        } break;

        case CMD_STRUCTURE_DEMOLISH: {
            if (parameter.size() != 1) return;
            const auto* issuer = currentGame->getPlayerByID(playerID);
            auto* structure = dynamic_cast<StructureBase*>(currentGame->getObjectManager().getObject(parameter[0]));
            if (issuer && structure && issuer->getHouse() == structure->getOwner()) structure->demolish();
        } break;

        case CMD_ZONE_DEMOLISH: {
            if (parameter.size() != 1 || !currentGame->isCitySimEnabled()) return;
            const auto* issuer = currentGame->getPlayerByID(playerID);
            auto* zone = dynamic_cast<ZoneStructure*>(currentGame->getObjectManager().getObject(parameter[0]));
            if (issuer && zone && issuer->getHouse() == zone->getOwner()) zone->demolish();
        } break;

        case CMD_POLICE_REINFORCEMENTS: {
            if (parameter.size() != 1) return;
            const auto* issuer = currentGame->getPlayerByID(playerID);
            auto* station = dynamic_cast<PoliceStation*>(currentGame->getObjectManager().getObject(parameter[0]));
            if (issuer && station && issuer->getHouse() == station->getOwner()) station->doSpawnVehicles();
        } break;

        case CMD_HOUSE_AUTO_REPAIR: {
            if (parameter.size() != 1 || parameter[0] > 1) return;
            const auto* issuer = currentGame->getPlayerByID(playerID);
            if (!issuer || !issuer->getHouse()) return;
            auto* house = currentGame->getHouse(issuer->getHouse()->getHouseID());
            if (house) house->setAutoRepairEnabled(parameter[0] != 0);
        } break;

        default: {
            THROW(std::invalid_argument, "Command::executeCommand(): Unknown CommandID!");
        } break;
    }

}
