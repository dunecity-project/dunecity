/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <structures/LoveFactory.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <Game.h>
#include <House.h>
#include <SoundPlayer.h>
#include <globals.h>
#include <units/HarvesterHelpers.h>

#include <algorithm>
#include <array>
#include <vector>

namespace {

constexpr Uint32 LOVE_FACTORY_PRICE_CHANGE_TIME = MILLI2CYCLES(60*1000);
constexpr Uint32 LOVE_FACTORY_AI_ORDER_INTERVAL = MILLI2CYCLES(3*1000);
constexpr int LOVE_FACTORY_AI_CREDIT_RESERVE = 1500;
constexpr int LOVE_FACTORY_MAX_STOCK = 5;

constexpr std::array<Uint32, 4> DeliveryChoices = {
    Delivery_Small,
    Delivery_Medium,
    Delivery_Heavy,
    Delivery_Support
};

int fallbackBasePrice(Uint32 deliveryID) {
    switch(deliveryID) {
        case Delivery_Small:   return 600;
        case Delivery_Medium:  return 1000;
        case Delivery_Heavy:   return 1600;
        case Delivery_Support: return 1300;
        default:               return 0;
    }
}

} // namespace

LoveFactory::LoveFactory(House* newOwner)
    : StarPort(newOwner, Structure_LoveFactory, ObjPic_LoveFactory, Coord(2,3)),
      lastPriceUpdateCycle(currentGame != nullptr ? currentGame->getGameCycleCount() : 0) {
    updateBuildList();
}

LoveFactory::LoveFactory(InputStream& stream)
    : StarPort(stream, Structure_LoveFactory, ObjPic_LoveFactory, Coord(2,3)),
      lastPriceUpdateCycle(stream.readUint32()) {
}

LoveFactory::~LoveFactory() = default;

void LoveFactory::save(OutputStream& stream) const {
    StarPort::save(stream);
    stream.writeUint32(lastPriceUpdateCycle);
}

void LoveFactory::ensureDeliveryStock() {
    if(getOwner() == nullptr) {
        return;
    }

    auto& choam = getOwner()->getChoam();
    for(const Uint32 deliveryID : DeliveryChoices) {
        if(choam.getNumAvailable(deliveryID) == INVALID) {
            choam.addItem(deliveryID, 1);
        }
    }
}

bool LoveFactory::isDeliveryChoice(Uint32 deliveryID) const {
    return std::find(DeliveryChoices.begin(), DeliveryChoices.end(), deliveryID)
        != DeliveryChoices.end();
}

int LoveFactory::getDeliveryMaximum(Uint32 deliveryID) const {
    switch(deliveryID) {
        case Delivery_Small:   return 6;
        case Delivery_Medium:  return 5;
        case Delivery_Heavy:   return 4;
        case Delivery_Support: return 3;
        default:               return 0;
    }
}

int LoveFactory::getRandomizedPrice(Uint32 deliveryID) const {
    int basePrice = currentGame->objectData.data[deliveryID][getTechnologyHouseID()].price;
    if(basePrice <= 0) {
        basePrice = fallbackBasePrice(deliveryID);
    }

    const int firstRoll = currentGame->randomGen.rand(2, 8);
    const int secondRoll = currentGame->randomGen.rand(2, 8);
    return std::max(1, basePrice * (firstRoll + secondRoll) / 10);
}

void LoveFactory::updateBuildList() {
    if(currentGame == nullptr) {
        return;
    }

    // BuilderBase may have populated this inherited Starport list before the
    // Love Factory is fully initialized. Its sidebar must expose only the four
    // reinforcement deliveries, never the regular CHOAM unit catalogue.
    for(auto iter = buildList.begin(); iter != buildList.end();) {
        if(!isDeliveryChoice(iter->itemID)) {
            iter = buildList.erase(iter);
        } else {
            ++iter;
        }
    }

    ensureDeliveryStock();

    const Uint32 now = currentGame->getGameCycleCount();
    const bool initialList = buildList.empty();
    const bool refreshPrices = initialList
        || (now - lastPriceUpdateCycle >= LOVE_FACTORY_PRICE_CHANGE_TIME);

    auto iter = buildList.begin();
    for(const Uint32 deliveryID : DeliveryChoices) {
        const BuildItem* existing = getBuildItem(deliveryID);
        int price = existing != nullptr ? static_cast<int>(existing->price)
                                        : getRandomizedPrice(deliveryID);
        if(refreshPrices && !initialList) {
            price = getRandomizedPrice(deliveryID);
        }
        insertItem(buildList, iter, deliveryID, price);
    }

    if(refreshPrices) {
        lastPriceUpdateCycle = now;
        if(!initialList && getOwner() == pLocalHouse) {
            currentGame->addToNewsTicker(_("New Love Factory delivery prices"));
        }
    }
}

void LoveFactory::handleProduceItemClick(Uint32 deliveryID, bool multipleMode) {
    const BuildItem* item = getBuildItem(deliveryID);
    if(item == nullptr || !isDeliveryChoice(deliveryID)) {
        soundPlayer->playSound(Sound_InvalidAction);
        return;
    }

    if(getOwner()->getCredits() < static_cast<int>(item->price)) {
        soundPlayer->playSound(Sound_InvalidAction);
        currentGame->addToNewsTicker(_("Not enough money"));
        return;
    }

    BuilderBase::handleProduceItemClick(deliveryID, multipleMode);
}

void LoveFactory::doProduceItem(Uint32 deliveryID, bool multipleMode) {
    ensureDeliveryStock();
    BuildItem* item = getBuildItem(deliveryID);
    if(item == nullptr || !isDeliveryChoice(deliveryID) || !okToOrder()) {
        return;
    }

    for(int i = 0; i < (multipleMode ? 5 : 1); i++) {
        const int deliveryStock = getOwner()->getChoam().getNumAvailable(deliveryID);
        if(deliveryStock <= 0 || getOwner()->getCredits() < static_cast<int>(item->price)) {
            break;
        }

        item->num++;
        // Paid up front like a Starport order: keep the starting-cash share so
        // a cancellation refunds it to the pool that funded it.
        const auto payment = getOwner()->payCredits(item->price);
        currentProductionQueue.emplace_back(deliveryID, item->price, payment.fromStarting);
        getOwner()->getChoam().setNumAvailable(deliveryID, deliveryStock - 1);
    }
}

void LoveFactory::doCancelItem(Uint32 deliveryID, bool multipleMode) {
    ensureDeliveryStock();
    BuildItem* item = getBuildItem(deliveryID);
    if(item == nullptr || !isDeliveryChoice(deliveryID)) {
        return;
    }

    for(int i = 0; i < (multipleMode ? 5 : 1); i++) {
        if(item->num == 0) {
            break;
        }

        auto mostExpensive = currentProductionQueue.end();
        Uint32 mostExpensivePrice = 0;
        for(auto iter = currentProductionQueue.begin(); iter != currentProductionQueue.end(); ++iter) {
            if(iter->itemID == deliveryID && iter->price >= mostExpensivePrice) {
                mostExpensive = iter;
                mostExpensivePrice = iter->price;
            }
        }

        if(mostExpensive == currentProductionQueue.end()) {
            break;
        }

        item->num--;
        getOwner()->returnCredits(mostExpensive->price, mostExpensive->startingCreditsPaid);
        const int deliveryStock = getOwner()->getChoam().getNumAvailable(deliveryID);
        getOwner()->getChoam().setNumAvailable(
            deliveryID, deliveryStock < LOVE_FACTORY_MAX_STOCK ? deliveryStock + 1 : LOVE_FACTORY_MAX_STOCK);
        currentProductionQueue.erase(mostExpensive);
    }
}

void LoveFactory::doBuildRandom() {
    std::vector<Uint32> affordable;
    for(const BuildItem& item : buildList) {
        if(getOwner()->getChoam().getNumAvailable(item.itemID) > 0
           && getOwner()->getCredits() >= static_cast<int>(item.price) + LOVE_FACTORY_AI_CREDIT_RESERVE) {
            affordable.push_back(item.itemID);
        }
    }

    if(affordable.empty()) {
        return;
    }

    Uint32 choice = ItemID_Invalid;
    const int harvesters = getOwner()->getNumItems(Unit_Harvester)
        + getOwner()->getNumItems(Unit_RebelHarvester);
    const int carryalls = getOwner()->getNumItems(Unit_Carryall) + getOwner()->getNumItems(Unit_ChemicalCarryall);
    if(harvesters == 0 || carryalls < (harvesters + 1) / 2) {
        if(std::find(affordable.begin(), affordable.end(), Delivery_Support) != affordable.end()) {
            choice = Delivery_Support;
        }
    }

    if(choice == ItemID_Invalid) {
        const int index = currentGame->randomGen.rand(
            0, static_cast<int>(affordable.size()) - 1);
        choice = affordable[index];
    }

    doProduceItem(choice);
}

bool LoveFactory::isDeliveryCandidate(Uint32 unitItemID, Uint32 deliveryID) const {
    if(currentGame == nullptr || !isUnit(unitItemID)
       || unitItemID == Unit_Infantry5
       || unitItemID == Unit_Troopers5
       || unitItemID == Unit_ChemicalCarryall
       || unitItemID == Unit_Frigate || isOrnithopterUnit(unitItemID)
       || isSandwormUnit(unitItemID) || unitItemID == Unit_Special
       || isAmbientUnit(unitItemID)) {
        return false;
    }

    const auto& data = currentGame->objectData.data[unitItemID][getTechnologyHouseID()];
    if(!data.enabled || data.builder == ItemID_Invalid
       || (data.techLevel >= 0 && data.techLevel > currentGame->techLevel)) {
        return false;
    }

    const bool support = isHarvesterLikeUnit(unitItemID)
        || isCarryallUnit(unitItemID) || unitItemID == Unit_MCV;
    const bool medium = unitItemID == Unit_Quad
        || unitItemID == Unit_RocketTrike || unitItemID == Unit_SonicTrike;
    const bool small = isInfantryUnit(unitItemID)
        || unitItemID == Unit_Trike || unitItemID == Unit_RaiderTrike;

    switch(deliveryID) {
        case Delivery_Small:
            return small;
        case Delivery_Medium:
            return medium;
        case Delivery_Heavy:
            return !support && !small && !medium
                && data.builder == Structure_HeavyFactory;
        case Delivery_Support:
            return support;
        default:
            return false;
    }
}

void LoveFactory::deployOrderedItem(Uint32 deliveryID) {
    std::vector<Uint32> candidates;
    for(int item = ItemID_FirstID; item <= ItemID_LastID; item++) {
        if(isDeliveryCandidate(item, deliveryID)) {
            candidates.push_back(item);
        }
    }

    const int maximum = getDeliveryMaximum(deliveryID);
    int delivered = 0;
    int attempts = 0;
    while(delivered < maximum && attempts < maximum * 4 && !candidates.empty()) {
        attempts++;
        const int index = currentGame->randomGen.rand(
            0, static_cast<int>(candidates.size()) - 1);
        const Uint32 selectedUnit = candidates[index];
        const Uint32 deployableUnit = selectedUnit == Unit_Infantry
            ? Unit_Soldier
            : (selectedUnit == Unit_Troopers ? Unit_Trooper : selectedUnit);
        if(deploySingleUnit(deployableUnit, delivered == 0)) {
            delivered++;
        }
    }
}

void LoveFactory::updateStructureSpecificStuff() {
    ensureDeliveryStock();
    StarPort::updateStructureSpecificStuff();

    if(currentGame == nullptr || getOwner() == nullptr || !getOwner()->isAI()
       || !okToOrder() || !currentProductionQueue.empty()
       || LOVE_FACTORY_AI_ORDER_INTERVAL == 0
       || ((currentGame->getGameCycleCount() + getObjectID())
           % LOVE_FACTORY_AI_ORDER_INTERVAL) != 0) {
        return;
    }

    doBuildRandom();
    if(!currentProductionQueue.empty()) {
        doPlaceOrder();
    }
}
