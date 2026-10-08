#include <dunecity/CityFactionPolicy.h>
#include <players/AIDecisionLog.h>
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

#include <structures/BuilderBase.h>

#include <FileClasses/TextManager.h>

#include <globals.h>

#include <SoundPlayer.h>
#include <Map.h>
#include <House.h>
#include <Game.h>
#include <dunecity/CitySimulation.h>
#include <mod/ModManager.h>
#include <units/UnitBase.h>
#include <units/HarvesterHelpers.h>

#include <players/HumanPlayer.h>

#include <GUI/ObjectInterfaces/BuilderInterface.h>

#include <algorithm>

const int BuilderBase::itemOrder[] = {    Unit_ChemicalCarryall,
                                           Structure_Slab4, Structure_Slab1, Structure_Road, Structure_IX, Structure_StarPort,
                                           Structure_HighTechFactory, Structure_HeavyFactory, Structure_RocketTurret,
                                           Structure_Scoutpost, Structure_Flamepost, Structure_Chemipost, Structure_LoveFactory, Structure_ChaosFactory,
                                           Structure_RepairYard, Structure_GunTurret, Structure_TechCenter, Structure_WOR,
                                           Structure_Worfinery,
                                           Structure_Barracks, Structure_Wall, Structure_LightFactory,
                                           Structure_Silo, Structure_Radar, Structure_Refinery, Structure_WindTrap,
                                           Structure_AdvancedWindTrap, Structure_AdvancedWindTrapMK2, Structure_AdvancedWindTrapMK3,
                                           Structure_NuclearPlant, Structure_PoliceStation, Structure_Palace,
                                           Structure_Stadium, Structure_Airport,
                                           Structure_ZoneResidential, Structure_ZoneCommercial, Structure_ZoneIndustrial,
                                           Unit_SonicTank, Unit_Devastator, Unit_Deviator, Unit_Special,
                                           Unit_EliteLauncher, Unit_EliteSiegeTank, Unit_ChemicalSiegeTank, Unit_FlameTank,
                                           Unit_Launcher, Unit_SiegeTank, Unit_Tank, Unit_MCV,
                                           Unit_RebelHarvester, Unit_Harvester,
                                           Unit_Ornithopter, Unit_Carryall, Unit_Quad, Unit_RocketTrike, Unit_SonicTrike, Unit_RaiderTrike,
                                           Unit_Trike, Unit_Troopers, Unit_Trooper, Unit_Infantry, Unit_Soldier,
                                           Unit_Frigate, Unit_Sandworm, Unit_Saboteur, ItemID_Invalid };

namespace {
bool isWorfineryDirectProduct(Uint32 itemID) {
    return itemID == Unit_Trooper
        || itemID == Unit_Troopers
        || itemID == Unit_Harvester;
}

bool isCityHarkonnenProductBuilder(Uint32 builderID, Uint32 productID, int originalHouseID) {
    return DuneCity::cityHarkonnenProduct(currentGame && currentGame->isCitySimEnabled(),
        originalHouseID,builderID,productID);
}

bool isAlternateTornieBuilder(Uint32 builderID, Uint32 itemID) {
    return builderID == Structure_Worfinery && isWorfineryDirectProduct(itemID);
}

void logTechCenterBuildGate(const BuilderBase* builder,
                            const House* owner,
                            const ObjectData::ObjectDataStruct& objData,
                            bool producedHere,
                            bool prerequisitesMet,
                            int missingPrerequisite,
                            const char* reason,
                            bool available) {
    if(builder == nullptr || owner == nullptr || currentGame == nullptr || !objData.enabled || objData.techLevel < 0) {
        return;
    }

    SDL_Log("TornieBuild: TechCenter builderObject=%u house=%d originalHouse=%d "
            "gameTech=%d itemTech=%d enabled=%d builder=%d currentBuilder=%d "
            "producedHere=%d upgrade=%d/%d prereqMet=%d missingPrereq=%d "
            "hasWindtrap=%d hasIX=%d hasPalace=%d available=%d reason=%s",
            builder->getObjectID(),
            owner->getHouseID(),
            builder->getOriginalHouseID(),
            currentGame->techLevel,
            objData.techLevel,
            objData.enabled ? 1 : 0,
            objData.builder,
            builder->getItemID(),
            producedHere ? 1 : 0,
            builder->getCurrentUpgradeLevel(),
            objData.upgradeLevel,
            prerequisitesMet ? 1 : 0,
            missingPrerequisite,
            owner->getNumItems(Structure_WindTrap),
            owner->getNumItems(Structure_IX),
            owner->getNumItems(Structure_Palace),
            available ? 1 : 0,
            reason);
}
}

// SAVEGAMEVERSION 9846 persists which part of a payment came from exempt
// starting cash. Older saves have no provenance: their progress counts as
// earned income, which refunds conservatively instead of inventing exempt
// cash the house never had.
//
// The fields are written only once SAVEGAMEVERSION actually reaches 9846, so
// this build stays byte-compatible with the save format it declares until the
// version bump lands. Both sides of the stream use the same constant.
static constexpr Uint32 kCreditProvenanceSaveVersion = 9846;
static constexpr bool kSavesCreditProvenance = SAVEGAMEVERSION >= kCreditProvenanceSaveVersion;

static bool savegameHasCreditProvenance() {
    return kSavesCreditProvenance
        && (currentGame == nullptr || currentGame->getLoadedSavegameVersion() >= kCreditProvenanceSaveVersion);
}

void ProductionQueueItem::save(OutputStream& stream) const {
    stream.writeUint32(itemID);
    stream.writeUint32(price);
    if(kSavesCreditProvenance) {
        stream.writeFixPoint(startingCreditsPaid);
    }
}

void ProductionQueueItem::load(InputStream& stream) {
    itemID = stream.readUint32();
    price = stream.readUint32();
    startingCreditsPaid = savegameHasCreditProvenance() ? stream.readFixPoint() : FixPoint(0);
}

BuilderBase::BuilderBase(House* newOwner) : StructureBase(newOwner) {
    BuilderBase::init();

    curUpgradeLev = 0;
    upgradeProgress = 0;
    upgrading = false;

    currentProducedItem = ItemID_Invalid;
    bCurrentItemOnHold = false;
    clearProductionProgress();
    deployTimer = 0;

    buildSpeedLimit = 1.0_fix;
}

BuilderBase::BuilderBase(InputStream& stream) : StructureBase(stream) {
    BuilderBase::init();

    upgrading = stream.readBool();
    upgradeProgress = stream.readFixPoint();
    curUpgradeLev = stream.readUint8();

    bCurrentItemOnHold = stream.readBool();
    currentProducedItem = stream.readUint32();
    productionProgress = stream.readFixPoint();
    productionProgressFromStartingCredits = savegameHasCreditProvenance() ? stream.readFixPoint() : FixPoint(0);
    deployTimer = stream.readUint32();

    buildSpeedLimit = stream.readFixPoint();

    int numProductionQueueItem = stream.readUint32();
    for(int i=0;i<numProductionQueueItem;i++) {
        ProductionQueueItem tmp;
        tmp.load(stream);
        currentProductionQueue.push_back(tmp);
    }

    int numBuildItem = stream.readUint32();
    for(int i=0;i<numBuildItem;i++) {
        BuildItem tmp;
        tmp.load(stream);
        buildList.push_back(tmp);
    }
}

void BuilderBase::init() {
    aBuilder = true;
}

BuilderBase::~BuilderBase() = default;


void BuilderBase::save(OutputStream& stream) const {
    StructureBase::save(stream);

    stream.writeBool(upgrading);
    stream.writeFixPoint(upgradeProgress);
    stream.writeUint8(curUpgradeLev);

    stream.writeBool(bCurrentItemOnHold);
    stream.writeUint32(currentProducedItem);
    stream.writeFixPoint(productionProgress);
    if(kSavesCreditProvenance) {
        stream.writeFixPoint(productionProgressFromStartingCredits);
    }
    stream.writeUint32(deployTimer);

    stream.writeFixPoint(buildSpeedLimit);

    stream.writeUint32(currentProductionQueue.size());
    for(const ProductionQueueItem& queueItem : currentProductionQueue) {
        queueItem.save(stream);
    }

    stream.writeUint32(buildList.size());
    for(const BuildItem& buildItem : buildList) {
        buildItem.save(stream);
    }
}

ObjectInterface* BuilderBase::getInterfaceContainer() {
    if(mayShowOwnerInterface()) {
        return BuilderInterface::create(objectID);
    } else {
        return DefaultObjectInterface::create(objectID);
    }
}

void BuilderBase::insertItem(std::list<BuildItem>& buildItemList, std::list<BuildItem>::iterator& iter, Uint32 itemID, int price) {
    if(iter != buildItemList.end()) {
        if(iter->itemID == itemID) {
            if(price != -1) {
                iter->price = price;
            }
            ++iter;
            return;
        }
    }

    if(price == -1) {
        price = currentGame->objectData.data[itemID][originalHouseID].price;
    }

    buildItemList.insert(iter, BuildItem(itemID, price));
}

void BuilderBase::removeItem(std::list<BuildItem>& buildItemList, std::list<BuildItem>::iterator& iter, Uint32 itemID) {
    if(iter != buildItemList.end()) {
        if(iter->itemID == itemID) {
            std::list<BuildItem>::iterator iter2 = iter;
            ++iter;
            buildItemList.erase(iter2);

            // is this item currently produced?
            if(currentProducedItem == itemID) {
                // Losing a prerequisite cancels the order: pay the owner back
                // into the pools that funded it.
                refundProductionProgress();
                currentProducedItem = ItemID_Invalid;
            }

            // remove from production list
            std::list<ProductionQueueItem>::iterator iter3 = currentProductionQueue.begin();
            while(iter3 != currentProductionQueue.end()) {
                if(iter3->itemID == itemID) {
                    std::list<ProductionQueueItem>::iterator iter4 = iter3;
                    ++iter3;
                    currentProductionQueue.erase(iter4);
                } else {
                    ++iter3;
                }
            }

            produceNextAvailableItem();
        }
    }
}


void BuilderBase::refundProductionProgress() {
    if(owner != nullptr) {
        owner->returnCredits(productionProgress, productionProgressFromStartingCredits);
    }
    clearProductionProgress();
}


void BuilderBase::setOwner(House *no) {
    // The money already sunk into the current order was paid by the previous
    // owner. Keep the progress, but drop its claim on the new owner's exempt
    // starting cash, so capturing a builder cannot mint starting credits.
    if(no != this->owner) {
        productionProgressFromStartingCredits = 0;
        for(auto& order : currentProductionQueue) order.startingCreditsPaid = 0;
    }
    this->owner = no;
}

bool BuilderBase::isWaitingToPlace() const {
    if((currentProducedItem == ItemID_Invalid) || isUnit(currentProducedItem)) {
        return false;
    }

    const BuildItem* tmp = getBuildItem(currentProducedItem);
    if(tmp == nullptr) {
        return false;
    } else {
        return (productionProgress >= tmp->price);
    }
}

bool BuilderBase::isUnitLimitReached(Uint32 itemID) const {
    if((currentProducedItem == ItemID_Invalid) || isStructure(currentProducedItem)) {
        return false;
    }

    return getOwner()->isUnitLimitReached(itemID);
}


void BuilderBase::updateProductionProgress() {
    if(currentProducedItem != ItemID_Invalid) {
        BuildItem* tmp = getBuildItem(currentProducedItem);

        if((productionProgress < tmp->price) && (isOnHold() == false) && (isUnitLimitReached(currentProducedItem) == false) && (owner->getCredits() > 0)) {

            FixPoint oldProgress = productionProgress;

            if(currentGame->getGameInitSettings().getGameOptions().instantBuild == true) {
                FixPoint totalBuildCosts = tmp->price;
                FixPoint buildCosts = totalBuildCosts - productionProgress;

                const auto payment = owner->payCredits(buildCosts);
                productionProgress += payment.total;
                productionProgressFromStartingCredits += payment.fromStarting;
            } else {

                FixPoint buildSpeed = std::min( getHealth() / getMaxHealth(), buildSpeedLimit);
                FixPoint totalBuildCosts = tmp->price;
                int buildTime = currentGame->objectData.data[currentProducedItem][originalHouseID].buildtime;
                if (currentGame->isCitySimEnabled()) {
                    buildTime = DuneCity::getCityBuildTime(currentProducedItem, buildTime);
                }
                FixPoint totalBuildGameTicks = buildTime * 15;
                FixPoint buildCosts = totalBuildCosts / totalBuildGameTicks;

                const auto payment = owner->payCredits(buildCosts*buildSpeed);
                productionProgress += payment.total;
                productionProgressFromStartingCredits += payment.fromStarting;

                /* That was wrong. Build speed does not depend on power production
                if (getOwner()->hasPower() || (((isCampaignGameType(currentGame->gameType)) || ((currentGame->gameType == GameType::Skirmish || currentGame->gameType == GameType::SkirmishCoop))) && getOwner()->isAI())) {
                    //if not enough power, production is halved
                    ProductionProgress += owner->takeCredits(0.25_fix);
                } else {
                    ProductionProgress += owner->takeCredits(0.125_fix);
                }*/

            }

            if ((oldProgress == productionProgress) && (owner == pLocalHouse)) {
                currentGame->addToNewsTicker(_("Not enough money"));
            }

            if(productionProgress >= tmp->price) {
                setWaitingToPlace();
            }
        } else if(owner == pLocalHouse && productionProgress < tmp->price) {
            // The queue is stalled and nothing tells the player why: say so in
            // the log (every 10 s) and on the ticker (every 30 s).
            static Uint32 lastStallLog = 0;
            static Uint32 lastStallTicker = 0;
            const Uint32 now = SDL_GetTicks();
            const bool unitLimit = isUnitLimitReached(currentProducedItem);
            const bool noCredits = owner->getCredits() <= 0;
            if(now - lastStallLog >= 10000) {
                lastStallLog = now;
                SDL_Log("Production stalled: item %d in builder %u (item %d): onHold=%d unitLimit=%d (units %d/%d) credits=%d (city %ld, stored %ld, starting %ld)",
                        currentProducedItem, getObjectID(), getItemID(),
                        isOnHold() ? 1 : 0, unitLimit ? 1 : 0, owner->getNumUnits(), owner->getMaxUnits(),
                        owner->getCredits(), lround(owner->getCityCredits()), lround(owner->getStoredCredits()), lround(owner->getStartingCredits()));
            }
            if(!isOnHold() && now - lastStallTicker >= 30000) {
                lastStallTicker = now;
                if(unitLimit) {
                    currentGame->addToNewsTicker(_("Unit limit reached") + " (" + std::to_string(owner->getMaxUnits()) + ")");
                } else if(noCredits) {
                    currentGame->addToNewsTicker(_("Not enough money"));
                }
            }
        }
    }
}

void BuilderBase::doBuildRandom() {
    if(!buildList.empty()) {
        int item2Produce = std::next(buildList.begin(), currentGame->randomGen.rand(0, static_cast<Sint32>(buildList.size())-1))->itemID;
        doProduceItem(item2Produce);
    }
}

void BuilderBase::produceNextAvailableItem() {
    if(currentProductionQueue.empty() == true) {
        currentProducedItem = ItemID_Invalid;
    } else {
        currentProducedItem = currentProductionQueue.front().itemID;
    }

    clearProductionProgress();
    bCurrentItemOnHold = false;
}

int BuilderBase::getMaxUpgradeLevel() const {
    int upgradeLevel = 0;

    for(int i = ItemID_FirstID; i <= ItemID_LastID; i++) {
        if (!currentGame->isCitySimEnabled() && DuneCity::isCityOnlyStructure(i)) continue;
        const int dataHouseID = (i == Unit_ChemicalCarryall) ? owner->getHouseID() : originalHouseID;
        const ObjectData::ObjectDataStruct& objData = currentGame->objectData.data[i][dataHouseID];

        if(objData.enabled && (objData.builder == (int) itemID
            || isCityHarkonnenProductBuilder(itemID, i, originalHouseID))
            && (objData.techLevel <= currentGame->techLevel)) {
            upgradeLevel = std::max(upgradeLevel, (int) objData.upgradeLevel);
        }
    }

    if(itemID == Structure_HighTechFactory && owner != nullptr
       && ModManager::instance().isTornieContentActive()
       && getHouseScenarioLetter(static_cast<HOUSETYPE>(owner->getHouseID())) == 'W'
       && currentGame->techLevel >= 7) {
        upgradeLevel = std::max(upgradeLevel, 2);
    }
    return upgradeLevel;
}

void BuilderBase::updateBuildList()
{
    std::list<BuildItem>::iterator iter = buildList.begin();

    for(int i = 0; itemOrder[i] != ItemID_Invalid; i++) {

        int itemID2Add = itemOrder[i];
        if ((itemID2Add == Structure_Slab1 || itemID2Add == Structure_Slab4)
            && !currentGame->getGameInitSettings().getGameOptions().concreteRequired) {
            removeItem(buildList, iter, itemID2Add);
            continue;
        }

        // City zones and Road are only available when the active mod opts into
        // DuneCity city-sim features. Hide them from the build list otherwise.
        // (Concrete slabs stay available in vanilla mode — they pre-date the
        // city-sim fork and are core Dune Legacy.)
        const bool isCityOnly = DuneCity::isCityOnlyStructure(itemID2Add);
        if (isCityOnly && !currentGame->isCitySimEnabled()) {
            removeItem(buildList, iter, itemID2Add);
            continue;
        }

        const bool tornieActive = ModManager::instance().isTornieContentActive();
        const bool specialChemicalCarryall = itemID2Add == Unit_ChemicalCarryall
            && itemID == Structure_HighTechFactory
            && owner != nullptr
            && tornieActive
            && (isHouseFaction(static_cast<HOUSETYPE>(owner->getHouseID()), HOUSE_WILDSPADE)
                || owner->getHouseID() == HOUSE_ATREIDES);
        if(itemID2Add == Unit_ChemicalCarryall && !specialChemicalCarryall) {
            removeItem(buildList, iter, itemID2Add);
            continue;
        }
        const int dataHouseID = (itemID2Add == Unit_ChemicalCarryall) ? owner->getHouseID() : originalHouseID;
        const ObjectData::ObjectDataStruct& objData = currentGame->objectData.data[itemID2Add][dataHouseID];

        const bool itemEnabled = objData.enabled || specialChemicalCarryall;
        const int requiredUpgrade = specialChemicalCarryall ? 2 : objData.upgradeLevel;
        const int configuredTechLevel = specialChemicalCarryall ? 1 : objData.techLevel;
        const int requiredTechLevel = itemID2Add == Structure_ChaosFactory
            ? std::max(9, configuredTechLevel)
            : configuredTechLevel;
        const bool producedHere = objData.builder == static_cast<int>(itemID)
                               || isCityHarkonnenProductBuilder(itemID, itemID2Add, originalHouseID)
                               || isAlternateTornieBuilder(itemID, itemID2Add)
                               || specialChemicalCarryall;
        const bool directWorfineryProduct = itemID == Structure_Worfinery
                                          && isWorfineryDirectProduct(itemID2Add);
        const bool traceTechCenter = (itemID2Add == Structure_TechCenter)
                                  && (itemID == Structure_ConstructionYard);

        // Trooper, Troopers and the normal Harvester are always available in
        // the Worfinery: no tech-center, upgrade or structure prerequisite.
        if(!itemEnabled || !producedHere
           || (!directWorfineryProduct && (requiredUpgrade > curUpgradeLev))
           || (!directWorfineryProduct && (requiredTechLevel > currentGame->techLevel))) {
            // first simple checks have rejected this item as being available for built in this builder
            if(traceTechCenter) {
                const char* reason = "available";
                if(!objData.enabled) {
                    reason = "disabled";
                } else if(!producedHere) {
                    reason = "wrong-builder";
                } else if(objData.upgradeLevel > curUpgradeLev) {
                    reason = "upgrade-too-low";
                } else if(objData.techLevel > currentGame->techLevel) {
                    reason = "tech-too-low";
                }
                logTechCenterBuildGate(this, owner, objData, producedHere, false, ItemID_Invalid, reason, false);
            }
            removeItem(buildList, iter, itemID2Add);
        } else {

            // check if prerequisites are met. Worfinery direct products bypass
            // all prerequisite structures by design.
            bool bPrerequisitesMet = directWorfineryProduct;
            int missingPrerequisite = ItemID_Invalid;
            if(!directWorfineryProduct) {
                bPrerequisitesMet = true;
                const int prerequisiteLimit = std::min<int>(Num_ItemID, static_cast<int>(objData.prerequisiteStructuresSet.size()));
                for(int itemID2Test = ItemID_FirstID; itemID2Test < prerequisiteLimit; itemID2Test++) {
                    if(!isStructure(itemID2Test)) {
                        continue;
                    }

                    if(objData.prerequisiteStructuresSet[itemID2Test] && (owner->getNumItems(itemID2Test) <= 0)) {
                        bPrerequisitesMet = false;
                        missingPrerequisite = itemID2Test;
                        break;
                    }
                }
            }

            if(specialChemicalCarryall && owner->getNumItems(Structure_IX) <= 0) {
                bPrerequisitesMet = false;
                missingPrerequisite = Structure_IX;
            }

            if(itemID2Add == Structure_ChaosFactory) {
                const int chaosPrerequisites[] = {
                    Structure_WindTrap,
                    Structure_LightFactory,
                    Structure_Radar,
                    Structure_HeavyFactory,
                    Structure_HighTechFactory
                };
                for(const int prerequisite : chaosPrerequisites) {
                    if(owner->getNumItems(prerequisite) <= 0) {
                        bPrerequisitesMet = false;
                        missingPrerequisite = prerequisite;
                        break;
                    }
                }
            }

            if(bPrerequisitesMet) {
                if(traceTechCenter) {
                    logTechCenterBuildGate(this, owner, objData, producedHere, true, ItemID_Invalid, "available", true);
                }
                const int buildListPrice = specialChemicalCarryall
                    ? 950
                    : (directWorfineryProduct && itemID2Add == Unit_Harvester ? 425 : -1);
                insertItem(buildList, iter, itemID2Add, buildListPrice);
            } else {
                if(traceTechCenter) {
                    logTechCenterBuildGate(this, owner, objData, producedHere, false, missingPrerequisite, "missing-prerequisite", false);
                }
                removeItem(buildList, iter, itemID2Add);
            }
        }
    }

}

void BuilderBase::setWaitingToPlace() {
    if (currentProducedItem != ItemID_Invalid)  {
        if (owner == pLocalHouse) {
            if(isStructure(currentProducedItem)) {
                soundPlayer->playVoice(ConstructionComplete, getOwner()->getHouseID());
            } else if(isFlyingUnit(currentProducedItem)) {
                soundPlayer->playVoice(UnitLaunched, getOwner()->getHouseID());
            } else if(isHarvesterLikeUnit(currentProducedItem)) {
                soundPlayer->playVoice(HarvesterDeployed, getOwner()->getHouseID());
            } else {
                soundPlayer->playVoice(UnitDeployed, getOwner()->getHouseID());
            }
        }

        if (isUnit(currentProducedItem)) {
            //if its a unit
            deployTimer = MILLI2CYCLES(750);
        } else {
            //its a structure
            if (owner == pLocalHouse) {
                currentGame->addToNewsTicker(_("@DUNE.ENG|51#Construction is complete"));
            }
        }
    }
}

void BuilderBase::unSetWaitingToPlace() {
    removeBuiltItemFromProductionQueue();
}

int BuilderBase::getUpgradeCost() const {
    return currentGame->objectData.data[itemID][originalHouseID].price / 2;
}



bool BuilderBase::update() {
    if(StructureBase::update() == false) {
        return false;
    }

    if(isUnit(currentProducedItem) && (productionProgress >= getBuildItem(currentProducedItem)->price)) {
        deployTimer--;
        if(deployTimer == 0) {
            int finishedItemID = currentProducedItem;
            removeBuiltItemFromProductionQueue();

            int num2Place = 1;

            if(finishedItemID == Unit_Infantry) {
                // make three
                finishedItemID = Unit_Soldier;
                num2Place = 3;
            } else if(finishedItemID == Unit_Troopers) {
                // make three
                finishedItemID = Unit_Trooper;
                num2Place = 3;
            }

            Coord groupDeploySpot = Coord::Invalid();
            for(int i = 0; i < num2Place; i++) {
                UnitBase* newUnit = getOwner()->createUnit(finishedItemID);

                if(newUnit != nullptr) {
                    Coord unitDestination;
                    if( getOwner()->isAI()
                        && ((isCarryallUnit(newUnit->getItemID()))
                            || isHarvesterLikeUnit(newUnit->getItemID())
                            || (newUnit->getItemID() == Unit_MCV))) {
                        // Don't want harvesters going to the rally point
                        unitDestination = location;
                    } else {
                        unitDestination = destination;
                    }

                    Coord spot = newUnit->isAFlyingUnit() ? location + Coord(1,1) : Coord::Invalid();
                    if(!newUnit->isAFlyingUnit()) {
                        if((num2Place > 1) && groupDeploySpot.isValid()) {
                            spot = groupDeploySpot;
                        } else {
                            spot = currentGameMap->findDeploySpot(newUnit, location, currentGame->randomGen, unitDestination, structureSize);
                            if(num2Place > 1) {
                                groupDeploySpot = spot;
                            }
                        }
                    }
                    newUnit->deploy(spot);

                    // Set AI unit default mode
                    if(getOwner()->isAI()) {
                        int unitType = newUnit->getItemID();
                        // Harvesters should start harvesting automatically
                        if(isHarvesterLikeUnit(unitType)) {
                            newUnit->doSetAttackMode(HARVEST);
                        }
                        // All other units keep their default GUARD/STOP until AI orders them
                    }

                    if(unitDestination.isValid()) {
                        newUnit->setGuardPoint(unitDestination);
                        newUnit->setDestination(unitDestination);
                        newUnit->setAngle(destinationDrawnAngle(newUnit->getLocation(), newUnit->getDestination()));
                    }

                    // inform owner of its new unit
                    newUnit->getOwner()->informWasBuilt(newUnit);
                    AITelemetry::log().write(currentGame->getGameCycleCount(), owner->getHouseID(), -1, "unit_produced",
                        AITelemetry::Record().set("builder", getObjectID()).set("item", finishedItemID)
                            .set("object", newUnit->getObjectID()).set("x", spot.x).set("y", spot.y));
                }
            }
        }
    }

    if(upgrading == true) {
        FixPoint totalUpgradePrice = getUpgradeCost();

        if(currentGame->getGameInitSettings().getGameOptions().instantBuild == true) {
            FixPoint upgradePriceLeft = totalUpgradePrice - upgradeProgress;
            upgradeProgress += owner->takeCredits(upgradePriceLeft);
        } else {
            FixPoint totalUpgradeGameTicks = 30 * 100 / 5;
            upgradeProgress += owner->takeCredits(totalUpgradePrice / totalUpgradeGameTicks);
        }

        if(upgradeProgress >= totalUpgradePrice) {
            upgrading = false;
            curUpgradeLev++;
            updateBuildList();

            upgradeProgress = 0;
        }
    } else {
        updateProductionProgress();
    }

    return true;
}

void BuilderBase::removeBuiltItemFromProductionQueue() {
    clearProductionProgress();

    auto currentBuildItemIter = std::find_if(   buildList.begin(),
                                                buildList.end(),
                                                [&](BuildItem& buildItem) {
                                                    return ((buildItem.itemID == currentProducedItem) && (buildItem.num > 0));
                                                });

    if(currentBuildItemIter != buildList.end()) {
        currentBuildItemIter->num--;
    }

    deployTimer = 0;
    if(!currentProductionQueue.empty()) {
        currentProductionQueue.pop_front();
    }
    produceNextAvailableItem();
}

void BuilderBase::handleUpgradeClick() {
    currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_BUILDER_UPGRADE, objectID));
}

void BuilderBase::handleProduceItemClick(Uint32 itemID, bool multipleMode) {
    for(const BuildItem& buildItem : buildList) {
        if(buildItem.itemID == itemID) {
            if( currentGame->getGameInitSettings().getGameOptions().onlyOnePalace
                && (itemID == Structure_Palace)
                && ((buildItem.num > 0) || (owner->getNumItems(Structure_Palace) > 0))) {
                // only one palace allowed
                soundPlayer->playSound(Sound_InvalidAction);
                return;
            }
        }
    }

    currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_BUILDER_PRODUCEITEM, objectID, itemID, (Uint32) multipleMode));
}

void BuilderBase::handleCancelItemClick(Uint32 itemID, bool multipleMode) {
    currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_BUILDER_CANCELITEM, objectID, itemID, (Uint32) multipleMode));
}

void BuilderBase::handleSetOnHoldClick(bool OnHold) {
    currentGame->getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_BUILDER_SETONHOLD, objectID, (Uint32) OnHold));
}


bool BuilderBase::doUpgrade() {
    if(upgrading) {
        return false;
    } else if(isAllowedToUpgrade() && (owner->getCredits() >= getUpgradeCost())) {
        upgrading = true;
        upgradeProgress = 0;
        return true;
    } else {
        return false;
    }
}

void BuilderBase::doProduceItem(Uint32 itemID, bool multipleMode) {
    if ((itemID == Structure_Slab1 || itemID == Structure_Slab4)
        && !currentGame->getGameInitSettings().getGameOptions().concreteRequired) return;
    for(BuildItem& buildItem : buildList) {
        if(buildItem.itemID == itemID) {
            for(int i = 0; i < (multipleMode ? 5 : 1); i++) {
                if( currentGame->getGameInitSettings().getGameOptions().onlyOnePalace
                    && (itemID == Structure_Palace)
                    && ((buildItem.num > 0) || (owner->getNumItems(Structure_Palace) > 0))) {
                    // only one palace allowed
                    return;
                }

                buildItem.num++;
                currentProductionQueue.emplace_back(itemID, buildItem.price );
                if(currentProducedItem == ItemID_Invalid) {
                    clearProductionProgress();
                    currentProducedItem = itemID;
                }

                if(pLocalHouse == getOwner()) {
                    pLocalPlayer->onProduceItem(itemID);
                }
            }
            break;
        }
    }
}

void BuilderBase::doCancelItem(Uint32 itemID, bool multipleMode) {
    for(BuildItem& buildItem : buildList) {
        if(buildItem.itemID == itemID) {
            for(int i = 0; i < (multipleMode ? 5 : 1); i++) {
                if(buildItem.num > 0) {
                    buildItem.num--;

                    bool bCancelCurrentItem = (itemID == currentProducedItem);

                    auto queueItemIter = std::find_if(  currentProductionQueue.rbegin(),
                                                        currentProductionQueue.rend(),
                                                        [&](ProductionQueueItem& queueItem) {
                                                            return (queueItem.itemID == itemID);
                                                        });

                    if(queueItemIter != currentProductionQueue.rend()) {
                        if(buildItem.num == 0 && bCancelCurrentItem) {
                            refundProductionProgress();
                        } else {
                            bCancelCurrentItem = false;
                        }
                        currentProductionQueue.erase(std::next(queueItemIter).base());
                    }

                    if(bCancelCurrentItem) {
                        deployTimer = 0;
                        produceNextAvailableItem();
                    }
                }
            }
            break;
        }
    }
}
