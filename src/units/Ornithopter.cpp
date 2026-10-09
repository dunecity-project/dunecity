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

#include <units/Ornithopter.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <Map.h>
#include <House.h>
#include <Game.h>
#include <SoundPlayer.h>
#include <structures/StructureBase.h>
#include <players/QuantBotConfig.h>
#include <players/QuantBot.h>
#include <units/HarvesterHelpers.h>
#include <sand.h>
#include <Definitions.h>

#include <iterator>

#define ORNITHOPTER_FRAMETIME 3

namespace {

bool isHumanControlledHouse(const House* house) {
    if(house == nullptr) {
        return false;
    }

    for(const auto& playerPtr : house->getPlayerList()) {
        if(playerPtr && playerPtr->getPlayerclass() == HUMANPLAYERCLASS) {
            return true;
        }
    }

    return false;
}

} // namespace

Ornithopter::Ornithopter(House* newOwner) : AirUnit(newOwner) {

    Ornithopter::init();

    setHealth(getMaxHealth());

    timeLastShot = 0;
}

Ornithopter::Ornithopter(InputStream& stream) : AirUnit(stream) {
    Ornithopter::init();

    timeLastShot = stream.readUint32();
}

void Ornithopter::init() {
    itemID = Unit_Ornithopter;
    registerUnit();

    graphicID = ObjPic_Ornithopter;
    graphic = pGFXManager->getObjPic(graphicID,getOwner()->getHouseID());
    shadowGraphic = pGFXManager->getObjPic(ObjPic_OrnithopterShadow,getOwner()->getHouseID());

    numImagesX = NUM_ANGLES;
    numImagesY = 3;

    numWeapons = 1;
    bulletType = Bullet_SmallRocket;

    currentMaxSpeed = currentGame->objectData.data[itemID][originalHouseID].maxspeed;
}

Ornithopter::~Ornithopter() = default;

void Ornithopter::save(OutputStream& stream) const
{
    AirUnit::save(stream);

    stream.writeUint32(timeLastShot);
}

bool Ornithopter::isVetoedAutonomousPrey(const ObjectBase* candidate) const {
    if(!isHarvesterLikeObject(candidate)) return false;
    if(!isHumanControlledHouse(owner)) return true;
    // AI do* helpers bypass human command leases. A human in the same house
    // therefore does not authorize a stale AI order; an actual player order
    // does, including one restored with the existing saved lease.
    for(const auto& player : owner->getPlayerList())
        if(const auto* bot=dynamic_cast<const QuantBot*>(player.get());
            bot && bot->managesAutonomousOrnithopter(this)) return true;
    return false;
}

void Ornithopter::dropVetoedAutonomousPrey() {
    if(target && isVetoedAutonomousPrey(target.getObjPointer())) {
        releaseTarget();
    }
}

void Ornithopter::checkPos() {
    dropVetoedAutonomousPrey();

    AirUnit::checkPos();

    if(!target) {
        if(destination.isValid()) {
            if(blockDistance(location, destination) <= 2) {
                destination.invalidate();
            }
        } else {
            if(blockDistance(location, guardPoint) > 17) {
                setDestination(guardPoint);
            }
        }
    }

    drawnFrame = ((currentGame->getGameCycleCount() + getObjectID())/ORNITHOPTER_FRAMETIME) % numImagesY;
}

bool Ornithopter::canAttack(const ObjectBase* object) const {
    if ((object != nullptr) && !object->isAFlyingUnit()
        && ((object->getOwner()->getTeamID() != owner->getTeamID()) || object->getItemID() == Unit_Sandworm)
        && object->isVisible(getOwner()->getTeamID()))
        return true;
    else
        return false;
}

void Ornithopter::destroy() {
    // place wreck
    if(currentGameMap->tileExists(location)) {
        Tile* pTile = currentGameMap->getTile(location);
        pTile->assignDeadUnit(DeadUnit_Ornithopter, owner->getHouseID(), Coord(lround(realX), lround(realY)));
    }

    AirUnit::destroy();
}

void Ornithopter::playAttackSound() {
    soundPlayer->playSoundAt(Sound_Rocket,location);
}

bool Ornithopter::canPass(int xPos, int yPos) const {
    return (currentGameMap->tileExists(xPos, yPos) && (!currentGameMap->getTile(xPos, yPos)->hasAnAirUnit()));
}



FixPoint Ornithopter::getDestinationAngle() const {
    if(timeLastShot > 0 && (currentGame->getGameCycleCount() - timeLastShot) < MILLI2CYCLES(1000)) {
        // we already shot at target and now want to fly in the opposite direction
        return destinationAngleRad(destination.x*TILESIZE + TILESIZE/2, destination.y*TILESIZE + TILESIZE/2, realX, realY)*8 / (FixPt_PI << 1);
    } else {
        return destinationAngleRad(realX, realY, destination.x*TILESIZE + TILESIZE/2, destination.y*TILESIZE + TILESIZE/2)*8 / (FixPt_PI << 1);
    }
}

bool Ornithopter::attack() {
    // Last line of the worker veto: no shot is fired at a harvester an
    // autonomously flown aircraft should never have been holding, even for the
    // single cycle between acquiring it and the next position update.
    if(target && isVetoedAutonomousPrey(target.getObjPointer())) {
        releaseTarget();
        return false;
    }

    bool bAttacked = AirUnit::attack();

    if(bAttacked) {
        timeLastShot = currentGame->getGameCycleCount();
    }
    return bAttacked;
}

const ObjectBase* Ornithopter::findTarget() const {
    const ATTACKMODE mode = getAttackMode();
    if(mode != HUNT || !isHumanControlledHouse(owner)) {
        return ObjectBase::findTarget();
    }

    const QuantBotConfig& config = getQuantBotConfig();
    const int myTeam = owner->getTeamID();

    const ObjectBase* bestTarget = nullptr;
    double bestScore = -1.0;

    auto evaluateCandidate = [&](const ObjectBase* candidate, const QuantBotConfig::TargetPriority& priority) {
        if(candidate == nullptr || !candidate->isActive()) {
            return;
        }

        // Autonomous acquisition never picks a worker, matching the veto the
        // shared searches in ObjectBase::findTarget() apply to this hull.
        if(isHarvesterLikeObject(candidate)) {
            return;
        }

        const House* candidateOwner = candidate->getOwner();
        if(candidateOwner == nullptr || candidateOwner->getTeamID() == myTeam) {
            return;
        }

        if(!candidate->isVisible(myTeam) || !canAttack(candidate)) {
            return;
        }

        const int weight = priority.build + priority.target;
        if(weight <= 0) {
            return;
        }

        FixPoint dist = blockDistance(getLocation(), candidate->getLocation());
        const double score = static_cast<double>(weight) / (dist.toDouble() + 1.0);

        if(score > bestScore) {
            bestScore = score;
            bestTarget = candidate;
        }
    };

    for(const StructureBase* pStructure : structureList) {
        evaluateCandidate(pStructure, config.getStructurePriority(pStructure->getItemID()));
    }

    for(const UnitBase* pUnit : unitList) {
        if(pUnit->getOwner() == owner) {
            continue;
        }
        evaluateCandidate(pUnit, config.getUnitPriority(pUnit->getItemID()));
    }

    if(bestTarget != nullptr) {
        return bestTarget;
    }

    return ObjectBase::findTarget();
}
