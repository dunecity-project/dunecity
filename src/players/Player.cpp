#include <players/AIDecisionLog.h>
#include <dunecity/NuclearBlastPolicy.h>
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

#include <players/Player.h>
#include <players/QuantBotConfig.h>

#include <Map.h>
#include <Game.h>
#include <GameInitSettings.h>

#include <structures/StructureBase.h>
#include <structures/BuilderBase.h>
#include <structures/StarPort.h>
#include <structures/ConstructionYard.h>
#include <structures/Palace.h>
#include <structures/TurretBase.h>
#include <structures/Scoutpost.h>
#include <units/UnitBase.h>
#include <FormationOrders.h>
#include <units/GroundUnit.h>
#include <units/Devastator.h>
#include <units/Harvester.h>
#include <units/InfantryBase.h>
#include <units/MCV.h>

#include <misc/Random.h>
#include <mod/ModManager.h>

#include <sand.h>
#include <globals.h>


Player::Player(House* associatedHouse, const std::string& playername) : pHouse(associatedHouse), playerID(0), playername(playername) {
}

Player::Player(InputStream& stream, House* associatedHouse) : pHouse(associatedHouse) {
    playerID = stream.readUint8();
    playername = stream.readString();
}

Player::~Player() {
    currentGame->unregisterPlayer(this);
}

void Player::save(OutputStream& stream) const {
    stream.writeUint8(playerID);
    stream.writeString(playername);
}

void Player::logDebug(const char* fmt, ...) const {
//#ifdef DEBUG_AI
    std::string house = getHouseNameByNumber((HOUSETYPE) pHouse->getHouseID());
    fprintf(stderr, "%s (%s):   ", playername.c_str(), house.c_str());
    va_list arg;
    va_start(arg, fmt);
    vfprintf(stderr, fmt, arg);
    va_end(arg);
    fprintf(stderr, "\n");
//#endif
}

void Player::logWarn(const char* fmt, ...) const {
    std::string house = getHouseNameByNumber((HOUSETYPE) pHouse->getHouseID());
    fprintf(stderr, "%s (%s):   ", playername.c_str(), house.c_str());
    va_list arg;
    va_start(arg, fmt);
    vfprintf(stderr, fmt, arg);
    va_end(arg);
    fprintf(stderr, "\n");
}

Random& Player::getRandomGen() const {
    return currentGame->randomGen;
}

const GameInitSettings& Player::getGameInitSettings() const {
    return currentGame->getGameInitSettings();
}

Uint32 Player::getGameCycleCount() const {
    return currentGame->getGameCycleCount();
}

int Player::getTechLevel() const {
    return currentGame->techLevel;
}

const Map& Player::getMap() const {
    return *currentGameMap;
}

const ObjectBase* Player::getObject(Uint32 objectID) const {
    return currentGame->getObjectManager().getObject(objectID);
}

const RobustList<const StructureBase*>& Player::getStructureList() const {
    return reinterpret_cast<const RobustList<const StructureBase*>&>(structureList);
}

const RobustList<const UnitBase*>& Player::getUnitList() const {
    return reinterpret_cast<const RobustList<const UnitBase*>&>(unitList);
}

const House* Player::getHouse(int houseID) const {
    if(houseID < 0 || houseID >= NUM_HOUSES) {
        return nullptr;
    }
    return currentGame->getHouse(houseID);
}

void Player::doRepair(const ObjectBase* pObject) const {
    if(pObject->getOwner() == getHouse() && pObject->isActive()) {
        if(pObject->isAUnit()) const_cast<UnitBase*>(static_cast<const UnitBase*>(pObject))->leaveFormation();
        const_cast<ObjectBase*>(pObject)->doRepair();
    } else {
        logWarn("The player '%s' tries to repair a structure or unit he doesn't own or that is inactive!\n", playername.c_str());
    }
}

void Player::doSetDeployPosition(const StructureBase* pStructure, int x, int y) const {
    if(pStructure->getOwner() == getHouse() && pStructure->isActive()) {
        const_cast<StructureBase*>(pStructure)->doSetDeployPosition(x, y);
    } else {
        logWarn("The player '%s' tries to set the deploy position of a structure he doesn't own or that is inactive!\n", playername.c_str());
    }
}

bool Player::doUpgrade(const BuilderBase* pBuilder) const {
    if(pBuilder->getOwner() == getHouse() && pBuilder->isActive()) {
        return const_cast<BuilderBase*>(pBuilder)->doUpgrade();
    } else {
        logWarn("The player '%s' tries to upgrade a structure he doesn't own or that is inactive!\n", playername.c_str());
        return false;
    }
}

void Player::doProduceItem(const BuilderBase* pBuilder, Uint32 itemID) const {
    if(pBuilder->getOwner() == getHouse() && pBuilder->isActive()) {
        const_cast<BuilderBase*>(pBuilder)->doProduceItem(itemID);
    } else {
        logWarn("The player '%s' tries to build some item in a structure he doesn't own or that is inactive!\n", playername.c_str());
    }
}

void Player::doCancelItem(const BuilderBase* pBuilder, Uint32 itemID) const {
    if(pBuilder->getOwner() == getHouse() && pBuilder->isActive()) {
        const_cast<BuilderBase*>(pBuilder)->doCancelItem(itemID);
    } else {
        logWarn("The player '%s' tries to cancel production of some item in a structure he doesn't own or that is inactive!\n", playername.c_str());
    }
}

void Player::doSetOnHold(const BuilderBase* pBuilder, bool bOnHold) const {
    if(pBuilder->getOwner() == getHouse() && pBuilder->isActive()) {
        const_cast<BuilderBase*>(pBuilder)->doSetOnHold(bOnHold);
    } else {
        logWarn("The player '%s' tries to hold/resume production in a structure he doesn't own or that is inactive!\n", playername.c_str());
    }
}

void Player::doSetBuildSpeedLimit(const BuilderBase* pBuilder, FixPoint buildSpeedLimit) const {
    if(pBuilder->getOwner() == getHouse() && pBuilder->isActive()) {
        const_cast<BuilderBase*>(pBuilder)->doSetBuildSpeedLimit(buildSpeedLimit);
    } else {
        logWarn("The player '%s' tries to limit the build speed of a structure he doesn't own or that is inactive!\n", playername.c_str());
    }
}

void Player::doBuildRandom(const BuilderBase* pBuilder) const {
    if(pBuilder->getOwner() == getHouse() && pBuilder->isActive()) {
        const_cast<BuilderBase*>(pBuilder)->doBuildRandom();
    } else {
        logWarn("The player '%s' tries to randomly build some item in a structure he doesn't own or that is inactive!\n", playername.c_str());
    }
}

int Player::chooseLowPriorityCustomUnit(const BuilderBase* pBuilder, int chanceDenominator) const {
    if(!ModManager::instance().isTornieFamilyContentActive()
       || pBuilder == nullptr || pBuilder->getOwner() != getHouse() || !pBuilder->isActive()) {
        return ItemID_Invalid;
    }

    static const Uint32 customUnits[] = {
        Unit_RaiderTrike,
        Unit_RocketTrike,
        Unit_SonicTrike,
        Unit_FlameTank,
        Unit_EliteLauncher,
        Unit_EliteSiegeTank,
        Unit_ChemicalSiegeTank,
        Unit_ChemicalCarryall,
        Unit_RebelHarvester,
        Unit_Troopers5, Unit_Infantry5, Unit_FlameQuad, Unit_GasInfantry,
        Unit_FlameTrooper, Unit_EliteOrnithopter, Unit_RaiderQuad, Unit_RangedTank,
        Unit_SlowTank, Unit_CapacityHarvester, Unit_FastHarvester, Unit_TameSandworm,
        Unit_Saboteur
    };
    Uint32 availableUnits[sizeof(customUnits) / sizeof(customUnits[0])] = {};
    int numAvailable = 0;

    for(const Uint32 itemID : customUnits) {
        if(!pBuilder->isAvailableToBuild(itemID) || pBuilder->isUnitLimitReached(itemID)) {
            continue;
        }
        if(itemID == Unit_ChemicalCarryall) {
            if(getHouse()->getNumItems(itemID) >= 1) continue;
        } else if(itemID == Unit_RebelHarvester) {
            if(getHouse()->getNumItems(itemID) >= 3) continue;
        } else if(itemID == Unit_CapacityHarvester || itemID == Unit_FastHarvester) {
            const int ceiling = getHouse()->getMaxHarvesters();
            if(getAIHarvesterCount() >= (ceiling > 0 ? ceiling : 3)) continue;
        }

        availableUnits[numAvailable++] = itemID;
    }

    if(numAvailable == 0) {
        return ItemID_Invalid;
    }
    if(chanceDenominator > 1 && getRandomGen().rand(0, chanceDenominator - 1) != 0) {
        return ItemID_Invalid;
    }

    return static_cast<int>(availableUnits[getRandomGen().rand(0, numAvailable - 1)]);
}

bool Player::hasTornieProductionUpgradeGoal(const BuilderBase* pBuilder) const {
    if(!ModManager::instance().isTornieFamilyContentActive() || !pBuilder
        || pBuilder->getOwner() != getHouse() || !pBuilder->isActive()) return false;
    const int technology = pBuilder->getTechnologyHouseID();
    const int currentUpgrade = pBuilder->getCurrentUpgradeLevel(), maxUpgrade = pBuilder->getMaxUpgradeLevel();
    const bool customContent = ModManager::instance().isTornieCustomUnitsAndBuildingsEnabled();
    for(int item = Unit_Troopers5; item <= Unit_TameSandworm; ++item) {
        if(!isUnit(item)) continue;
        if(!customContent && item != Unit_Troopers5 && item != Unit_Infantry5) continue;
        const auto& spec = currentGame->objectData.data[item][technology];
        if(!spec.enabled || spec.builder != static_cast<int>(pBuilder->getItemID())
            || spec.techLevel > currentGame->techLevel
            || spec.upgradeLevel <= currentUpgrade || spec.upgradeLevel > maxUpgrade) continue;
        bool prerequisites = true;
        for(int prerequisite = Structure_FirstID; prerequisite < Num_ItemID; ++prerequisite)
            if(isStructure(prerequisite) && spec.prerequisiteStructuresSet[prerequisite]
                && getHouse()->getNumItems(prerequisite) == 0) prerequisites = false;
        if(prerequisites) return true;
    }
    return false;
}

Uint32 Player::chooseTornieHarvester(const BuilderBase* pBuilder, int money) const {
    if(!ModManager::instance().isTornieCustomUnitsAndBuildingsEnabled() || !pBuilder) return Unit_Harvester;
    Uint32 selected = Unit_Harvester;
    const int technology = pBuilder->getTechnologyHouseID();
    for(const Uint32 item : {Unit_CapacityHarvester, Unit_FastHarvester})
        if(pBuilder->isAvailableToBuild(item) && !pBuilder->isUnitLimitReached(item)
            && currentGame->objectData.data[item][technology].price <= money
            && (selected == Unit_Harvester || getHouse()->getNumItems(item) < getHouse()->getNumItems(selected)))
            selected = item;
    return selected;
}

int Player::getAIHarvesterCount() const {
    int count = getHouse()->getNumItems(Unit_Harvester);
    if(ModManager::instance().isTornieFamilyContentActive())
        count += getHouse()->getNumItems(Unit_CapacityHarvester) + getHouse()->getNumItems(Unit_FastHarvester);
    return count;
}

int Player::getAIRefineryBays() const {
    int bays = getHouse()->getNumItems(Structure_Refinery);
    if(ModManager::instance().isTornieFamilyContentActive())
        bays += 2 * getHouse()->getNumItems(Structure_Doublefinery) + getHouse()->getNumItems(Structure_Worfinery);
    return bays;
}

Uint32 Player::chooseTornieStructure(const BuilderBase* pBuilder, Uint32 itemID, int money) const {
    if(!ModManager::instance().isTornieFamilyContentActive() || !pBuilder) return itemID;
    const Uint32 alternative = itemID == Structure_Refinery ? Structure_Doublefinery
        : itemID == Structure_WOR ? Structure_Worfinery
        : itemID == Structure_GunTurret && pBuilder->getTechnologyHouseID() == HOUSE_NEUTRAL ? Structure_Scoutpost : itemID;
    if(alternative != itemID && pBuilder->isAvailableToBuild(alternative)
        && currentGame->objectData.data[alternative][pBuilder->getTechnologyHouseID()].price <= money)
        return alternative;
    if(itemID == NONE_ID && money > 1500 && getHouse()->getNumItems(Structure_HeavyFactory) > 0) {
        for(const Uint32 candidate : {Structure_Worfinery, Structure_Scoutpost}) {
            if(candidate == Structure_Scoutpost
                && (pBuilder->getTechnologyHouseID() != HOUSE_NEUTRAL
                    || getHouse()->getNumItems(Structure_Slowpost) > 0)) continue;
            if(getHouse()->getNumItems(candidate) > 0 || !pBuilder->isAvailableToBuild(candidate)
                || currentGame->objectData.data[candidate][pBuilder->getTechnologyHouseID()].price > money) continue;
            bool pending = false;
            for(const auto* structure : getStructureList()) {
                if(structure->getOwner() != getHouse() || !structure->isABuilder()) continue;
                for(const auto& offer : static_cast<const BuilderBase*>(structure)->getBuildList())
                    if(offer.itemID == candidate && offer.num > 0) pending = true;
            }
            if(!pending) return candidate;
        }
    }
    return itemID;
}

bool Player::doUpgradeScoutpost(const StructureBase* pStructure, int spendable) const {
    if(!ModManager::instance().isTornieFamilyContentActive() || !pStructure
        || pStructure->getOwner() != getHouse() || !pStructure->isActive()) return false;
    const auto* scoutpost = dynamic_cast<const Scoutpost*>(pStructure);
    if(!scoutpost || !scoutpost->canUpgradeToSlowpost() || scoutpost->getSlowpostUpgradeCost() > spendable) return false;
    const_cast<Scoutpost*>(scoutpost)->doUpgradeToSlowpost();
    return true;
}

void Player::doPlaceOrder(const StarPort* pStarport) const {
    if(pStarport->getOwner() == getHouse() && pStarport->isActive()) {
        const_cast<StarPort*>(pStarport)->doPlaceOrder();
    } else {
        logWarn("The player '%s' tries to order something from a starport he doesn't own or that is inactive!\n", playername.c_str());
    }
}

bool Player::doPlaceStructure(const ConstructionYard* pConstYard, int x, int y) const {
    if(pConstYard->getOwner() == getHouse() && pConstYard->isActive()) {
        return const_cast<ConstructionYard*>(pConstYard)->doPlaceStructure(x, y);
    } else {
        logWarn("The player '%s' tries to place a structure he hasn't produced (or the construction yard is inactive)!\n", playername.c_str());
        return false;
    }
}

void Player::doMoveOwnUnitsOffPlot(const ConstructionYard* pConstYard, Uint32 itemID, int x, int y) const {
    if(pConstYard->getOwner() == getHouse() && pConstYard->isActive()) {
        const_cast<ConstructionYard*>(pConstYard)->moveOwnUnitsOffPlot(itemID, x, y, true);
    }
}

void Player::doSpecialWeapon(const Palace* pPalace) const {
    if(pPalace->getOwner() == getHouse() && pPalace->isActive()) {
        const_cast<Palace*>(pPalace)->doSpecialWeapon();
    } else {
        logWarn("The player '%s' tries to activate a special weapon from a palace he doesn't own or that is inactive!\n", playername.c_str());
    }
}

Coord Player::findNuclearMissileTarget() const {
    const int team = getHouse()->getTeamID();
    const StructureBase* best = nullptr;
    int bestScore = -1;
    for (const auto* plant : getStructureList()) {
        if (plant->getItemID() != Structure_NuclearPlant || !plant->isActive() || plant->getHealth() <= 0
            || plant->getOwner()->getTeamID() == team || !plant->isVisible(team)) continue;
        int score = 0;
        const Coord center = plant->getCenterPoint();
        for (const auto* nearby : getStructureList()) {
            if (nearby == plant || nearby->getOwner()->getTeamID() == team || !nearby->isVisible(team)
                || !nearby->isActive() || nearby->getHealth() <= 0) continue;
            const Coord pos = nearby->getCenterPoint();
            if (DuneCity::NuclearBlastPolicy::contains(pos.x - center.x, pos.y - center.y))
                score += nearby->getItemID() == Structure_NuclearPlant ? 100 : 1;
        }
        if (score > bestScore || (score == bestScore && best && plant->getObjectID() < best->getObjectID())) {
            best = plant; bestScore = score;
        }
    }
    if (!best) return Coord::Invalid();
    const Coord center = best->getCenterPoint();
    AITelemetry::log().write(getGameCycleCount(), getHouse()->getHouseID(), getPlayerID(), "palace_target",
        AITelemetry::Record().set("reason", "nuclear_chain_reaction").set("target", best->getObjectID())
            .set("target_house", best->getOwner()->getHouseID()).set("chain_score", bestScore)
            .set("x", center.x / TILESIZE).set("y", center.y / TILESIZE));
    return Coord(center.x / TILESIZE, center.y / TILESIZE);
}

void Player::doLaunchDeathhand(const Palace* pPalace, int x, int y) const {
    if(pPalace->getOwner() == getHouse() && pPalace->isActive()) {
        const_cast<Palace*>(pPalace)->doLaunchDeathhand(x, y);
    } else {
        logWarn("The player '%s' tries to launch a deathhand from a palace he doesn't own or that is inactive!\n", playername.c_str());
    }
}

void Player::doAttackObject(const TurretBase* pTurret, const ObjectBase* pTargetObject) const {
    if(pTurret->getOwner() == getHouse() && pTurret->isActive()) {
        const_cast<TurretBase*>(pTurret)->doAttackObject(pTargetObject);
    } else {
        logWarn("The player '%s' tries to attack with a turret he doesn't own or that is inactive!\n", playername.c_str());
        return;
    }
}



void Player::doMove2Pos(const UnitBase* pUnit, int x, int y, bool bForced) const {
    if(pUnit->getOwner() == getHouse() && pUnit->isActive()) {
        const_cast<UnitBase*>(pUnit)->leaveFormation();
        const_cast<UnitBase*>(pUnit)->doMove2Pos(x, y, bForced);
    } else {
        logWarn("The player '%s' tries to move a unit he doesn't own or that is inactive!\n", playername.c_str());
        return;
    }
}

void Player::doHuntToward(const UnitBase* pUnit, int x, int y) const {
    if(pUnit->getOwner() == getHouse() && pUnit->isActive()) {
        const_cast<UnitBase*>(pUnit)->leaveFormation();
        const_cast<UnitBase*>(pUnit)->doHuntToward(x, y);
    } else {
        logWarn("The player '%s' tries to send a unit he doesn't own or that is inactive to hunt!\n", playername.c_str());
        return;
    }
}

Uint32 Player::allocateFormationGroup() const {
    const Uint32 cycle = getGameCycleCount();
    if(cycle != formationTokenCycle) {
        formationTokenCycle = cycle;
        formationTokensThisCycle = 0;
    }
    return FormationPlanner::makeGroupID(playerID, FormationPlanner::makeToken(cycle, formationTokensThisCycle++));
}

namespace {

/// The caller's own active units, in the given order.
std::vector<const UnitBase*> ownActiveUnits(const std::vector<const UnitBase*>& units, const House* house) {
    std::vector<const UnitBase*> own;
    for(const UnitBase* unit : units)
        if(unit != nullptr && unit->getOwner() == house && unit->isActive()) own.push_back(unit);
    return own;
}

} // namespace

int Player::aiFormationShape() {
    // Blob is a QuantBot hunt-party behaviour, not a shape: every other AI group moves as None.
    return QuantBotConfig::formationShapeOf(getQuantBotConfig().waveFormation);
}

void Player::doFormationMove(const std::vector<const UnitBase*>& units, int x, int y, bool bForced, int shape) const {
    const auto own = ownActiveUnits(units, getHouse());
    const auto plan = shape == QuantBotConfig::kWaveFormationNone ? FormationPlanner::Plan()
        : FormationOrders::planGroup(own, Coord(x, y), FormationPlanner::shapeFromId(shape));
    if(!plan.formation || static_cast<int>(plan.slots.size()) < FormationOrders::kMinAIGroup) {
        for(const UnitBase* unit : own) doMove2Pos(unit, x, y, bForced);
        return;
    }
    std::vector<UnitBase*> members;
    std::vector<Coord> slots;
    for(const UnitBase* unit : own) {
        const Coord slot = plan.slotFor(unit->getObjectID());
        if(slot.isInvalid()) { doMove2Pos(unit, x, y, bForced); continue; }
        doMove2Pos(unit, slot.x, slot.y, bForced);
        members.push_back(const_cast<UnitBase*>(unit));
        slots.push_back(slot);
    }
    FormationOrders::join(members, allocateFormationGroup(), slots, false, FormationPlanner::facingCode(plan.forward),
                          bForced ? FormationOrders::Resume::ForcedMove : FormationOrders::Resume::Move);
}

void Player::doFormationHuntToward(const std::vector<const UnitBase*>& units, int x, int y, int shape) const {
    const auto own = ownActiveUnits(units, getHouse());
    const auto plan = shape == QuantBotConfig::kWaveFormationNone ? FormationPlanner::Plan()
        : FormationOrders::planGroup(own, Coord(x, y), FormationPlanner::shapeFromId(shape));
    if(!plan.formation || static_cast<int>(plan.slots.size()) < FormationOrders::kMinAIGroup) {
        for(const UnitBase* unit : own) doHuntToward(unit, x, y);
        return;
    }
    std::vector<UnitBase*> members;
    std::vector<Coord> slots;
    for(const UnitBase* unit : own) {
        const Coord slot = plan.slotFor(unit->getObjectID());
        if(slot.isInvalid()) { doHuntToward(unit, x, y); continue; }
        doHuntToward(unit, slot.x, slot.y);
        members.push_back(const_cast<UnitBase*>(unit));
        slots.push_back(slot);
    }
    // A hunting formation: the first contact of any member breaks the group into a fight.
    FormationOrders::join(members, allocateFormationGroup(), slots, true, FormationPlanner::facingCode(plan.forward),
                          FormationOrders::Resume::HuntToward);
}

void Player::doFormUp(const std::vector<const UnitBase*>& units) const {
    std::vector<UnitBase*> members;
    for(const UnitBase* unit : ownActiveUnits(units, getHouse()))
        if(FormationOrders::eligible(unit)) members.push_back(const_cast<UnitBase*>(unit));
    if(static_cast<int>(members.size()) < FormationOrders::kMinAIGroup) return;
    FormationOrders::join(members, allocateFormationGroup(), std::vector<Coord>(members.size(), Coord::Invalid()));
}

void Player::doMove2Object(const UnitBase* pUnit, const ObjectBase* pTargetObject) const {
    if(pUnit->getOwner() == getHouse() && pUnit->isActive()) {
        const_cast<UnitBase*>(pUnit)->leaveFormation();
        const_cast<UnitBase*>(pUnit)->doMove2Object(pTargetObject);
    } else {
        logWarn("The player '%s' tries to move a unit (to an object) he doesn't own or that is inactive!\n", playername.c_str());
        return;
    }
}

void Player::doAttackPos(const UnitBase* pUnit, int x, int y, bool bForced) const {
    if(pUnit->getOwner() == getHouse() && pUnit->isActive()) {
        const_cast<UnitBase*>(pUnit)->leaveFormation();
        const_cast<UnitBase*>(pUnit)->doAttackPos(x, y, bForced);
    } else {
        logWarn("The player '%s' tries to order a unit (to attack a position) he doesn't own or that is inactive!\n", playername.c_str());
        return;
    }
}

void Player::doAttackObject(const UnitBase* pUnit, const ObjectBase* pTargetObject, bool bForced) const {
    if(pUnit->getOwner() == getHouse() && pUnit->isActive()) {
        const_cast<UnitBase*>(pUnit)->leaveFormation();
        const_cast<UnitBase*>(pUnit)->doAttackObject(pTargetObject, bForced);
    } else {
        logWarn("The player '%s' tries to attack with a unit he doesn't own or that is inactive!\n", playername.c_str());
        return;
    }
}

void Player::doSetAttackMode(const UnitBase* pUnit, ATTACKMODE attackMode) const {
    if(pUnit->getOwner() == getHouse() && pUnit->isActive()) {
        const_cast<UnitBase*>(pUnit)->leaveFormation();
        const_cast<UnitBase*>(pUnit)->doSetAttackMode(attackMode);
    } else {
        logWarn("The player '%s' tries to change the attack mode of a unit he doesn't own or that is inactive!\n", playername.c_str());
        return;
    }
}

void Player::doStartDevastate(const Devastator* pDevastator) const {
    if(pDevastator->getOwner() == getHouse() && pDevastator->isActive()) {
        const_cast<Devastator*>(pDevastator)->doStartDevastate();
    } else {
        logWarn("The player '%s' tries to devastate a devastator he doesn't own or that is inactive!\n", playername.c_str());
        return;
    }
}

void Player::doReturn(const Harvester* pHarvester) const {
    if(pHarvester->getOwner() == getHouse() && pHarvester->isActive()) {
        const_cast<Harvester*>(pHarvester)->leaveFormation();
        const_cast<Harvester*>(pHarvester)->doReturn();
    } else {
        logWarn("The player '%s' tries to return a harvester he doesn't own or that is inactive!\n", playername.c_str());
        return;
    }
}

void Player::doCaptureStructure(const InfantryBase* pInfantry, const StructureBase* pTargetStructure) const {
    if(pInfantry->getOwner() == getHouse() && pInfantry->isActive()) {
        const_cast<InfantryBase*>(pInfantry)->leaveFormation();
        const_cast<InfantryBase*>(pInfantry)->doCaptureStructure(pTargetStructure);
    } else {
        logWarn("The player '%s' tries to capture with a unit he doesn't own or that is inactive!\n", playername.c_str());
        return;
    }
}

bool Player::doDeploy(const MCV* pMCV) const {
    if(pMCV->getOwner() == getHouse() && pMCV->isActive()) {
        return const_cast<MCV*>(pMCV)->doDeploy();
    } else {
        logWarn("The player '%s' tries to deploy a MCV he doesn't own or that is inactive!\n", playername.c_str());
        return false;
    }
}


bool Player::doRequestCarryallDrop(const GroundUnit* pGroundUnit) const {
    if(pGroundUnit->getOwner() == getHouse() && pGroundUnit->isActive()) {
        const_cast<GroundUnit*>(pGroundUnit)->leaveFormation();
        return const_cast<GroundUnit*>(pGroundUnit)->requestCarryall();
    } else {
        logWarn("The player '%s' tries request a carryall for a ground unit he doesn't own or that is inactive!\n", playername.c_str());
        return false;
    }
}

