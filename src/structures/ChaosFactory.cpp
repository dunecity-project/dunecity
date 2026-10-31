/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <structures/ChaosFactory.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <FileClasses/TextManager.h>
#include <Game.h>
#include <House.h>

#include <algorithm>
#include <vector>

namespace {
constexpr Uint32 CHAOS_FACTORY_OFFER_TIME = MILLI2CYCLES(120 * 1000);
}

ChaosFactory::ChaosFactory(House* newOwner)
    : BuilderBase(newOwner),
      offerCycleStart(currentGame != nullptr ? currentGame->getGameCycleCount() : 0) {
    ChaosFactory::init();
    setHealth(getMaxHealth());
    refreshOffers(true);
}

ChaosFactory::ChaosFactory(InputStream& stream)
    : BuilderBase(stream),
      offerCycleStart(stream.readUint32()) {
    ChaosFactory::init();
    for(int i = 0; i < OfferCount; ++i) {
        offeredItems[i] = stream.readUint32();
        offeredPrices[i] = static_cast<int>(stream.readUint32());
        remainingStock[i] = std::clamp(
            static_cast<int>(stream.readUint32()), 0, StockPerOffer);
    }

    bool valid = currentGame != nullptr && offeredItems[0] != offeredItems[1];
    for(int i = 0; i < OfferCount && valid; ++i) {
        valid = isOfferCandidate(offeredItems[i]) && offeredPrices[i] > 0;
    }
    if(valid) {
        rebuildOfferList();
    } else {
        refreshOffers(true);
    }
}

ChaosFactory::~ChaosFactory() = default;

void ChaosFactory::init() {
    itemID = Structure_ChaosFactory;
    owner->incrementStructures(itemID);
    structureSize = Coord(3, 2);
    graphicID = ObjPic_ChaosFactory;
    graphic = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    numImagesX = 4;
    numImagesY = 1;
    firstAnimFrame = 2;
    lastAnimFrame = 3;
    curAnimFrame = 2;
    lastVisibleFrame = 2;
}

void ChaosFactory::save(OutputStream& stream) const {
    BuilderBase::save(stream);
    stream.writeUint32(offerCycleStart);
    for(int i = 0; i < OfferCount; ++i) {
        stream.writeUint32(offeredItems[i]);
        stream.writeUint32(static_cast<Uint32>(std::max(0, offeredPrices[i])));
        stream.writeUint32(
            static_cast<Uint32>(std::clamp(remainingStock[i], 0, StockPerOffer)));
    }
}

bool ChaosFactory::isOfferCandidate(Uint32 candidate) const {
    if(currentGame == nullptr || !isUnit(candidate)
       || isSandwormUnit(candidate)
       || candidate == Unit_Saboteur
       || candidate == Unit_Soldier
       || candidate == Unit_Infantry5
       || candidate == Unit_Troopers5
       || candidate == Unit_Infantry
       || candidate == Unit_Frigate
       || candidate == Unit_Special
       || isAmbientUnit(candidate)) {
        return false;
    }

    // The Chaos Factory draws from the global unit catalogue, deliberately
    // ignoring the owning house's normal production roster. The catalogue is
    // what player houses can field; the Natives column is not a roster.
    for(int house = 0; house < NUM_PLAYER_HOUSES; ++house) {
        const auto& unitData = currentGame->objectData.data[candidate][house];
        if(unitData.enabled && unitData.price > 0) {
            return true;
        }
    }
    return false;
}

int ChaosFactory::getOfferIndex(Uint32 candidate) const {
    for(int i = 0; i < OfferCount; ++i) {
        if(offeredItems[i] == candidate) {
            return i;
        }
    }
    return -1;
}

int ChaosFactory::getBasePrice(Uint32 candidate) const {
    if(currentGame == nullptr) {
        return 0;
    }
    const int ownerPrice =
        currentGame->objectData.data[candidate][getTechnologyHouseID()].price;
    if(ownerPrice > 0) {
        return ownerPrice;
    }
    for(int house = 0; house < NUM_PLAYER_HOUSES; ++house) {
        const auto& unitData = currentGame->objectData.data[candidate][house];
        if(unitData.enabled && unitData.price > 0) {
            return unitData.price;
        }
    }
    return 0;
}

int ChaosFactory::getRemainingStock(Uint32 candidate) const {
    const int offer = getOfferIndex(candidate);
    return offer >= 0 ? remainingStock[offer] : 0;
}

int ChaosFactory::getOfferSecondsRemaining() const {
    if(currentGame == nullptr) {
        return 0;
    }

    const Uint32 elapsed = currentGame->getGameCycleCount() - offerCycleStart;
    if(elapsed >= CHAOS_FACTORY_OFFER_TIME) {
        return 0;
    }

    const Uint32 remaining = CHAOS_FACTORY_OFFER_TIME - elapsed;
    const Uint64 scaled = static_cast<Uint64>(remaining) * 120u;
    return std::max(1, static_cast<int>(
        (scaled + CHAOS_FACTORY_OFFER_TIME - 1) / CHAOS_FACTORY_OFFER_TIME));
}

int ChaosFactory::getRandomizedPrice(Uint32 candidate) const {
    const int basePrice = getBasePrice(candidate);
    if(basePrice <= 0) {
        return 0;
    }
    const int firstRoll = currentGame->randomGen.rand(2, 8);
    const int secondRoll = currentGame->randomGen.rand(2, 8);
    return std::max(1, basePrice * (firstRoll + secondRoll) / 10);
}

void ChaosFactory::rebuildOfferList() {
    buildList.clear();
    auto iter = buildList.begin();
    for(int i = 0; i < OfferCount; ++i) {
        if(offeredItems[i] != ItemID_Invalid && offeredPrices[i] > 0) {
            insertItem(buildList, iter, offeredItems[i], offeredPrices[i]);
        }
    }
    for(BuildItem& item : buildList) {
        item.num = static_cast<int>(std::count_if(
            currentProductionQueue.begin(), currentProductionQueue.end(),
            [&](const ProductionQueueItem& queued) {
                return queued.itemID == item.itemID;
            }));
    }
}

void ChaosFactory::refreshOffers(bool initialRound) {
    if(currentGame == nullptr) {
        return;
    }

    std::vector<Uint32> candidates;
    for(int candidate = ItemID_FirstID; candidate <= ItemID_LastID; ++candidate) {
        if(isOfferCandidate(static_cast<Uint32>(candidate))) {
            candidates.push_back(static_cast<Uint32>(candidate));
        }
    }

    offeredItems.fill(ItemID_Invalid);
    offeredPrices.fill(0);
    remainingStock.fill(0);
    for(int i = 0; i < OfferCount && !candidates.empty(); ++i) {
        const int index = currentGame->randomGen.rand(
            0, static_cast<Sint32>(candidates.size()) - 1);
        offeredItems[i] = candidates[index];
        offeredPrices[i] = getRandomizedPrice(offeredItems[i]);
        remainingStock[i] = StockPerOffer;
        candidates.erase(candidates.begin() + index);
    }

    offerCycleStart = currentGame->getGameCycleCount();
    rebuildOfferList();
    if(!initialRound && getOwner() == pLocalHouse) {
        currentGame->addToNewsTicker(_("New Chaos Factory offers and prices"));
    }
}

void ChaosFactory::updateBuildList() {
    if(offeredItems[0] == ItemID_Invalid || offeredItems[1] == ItemID_Invalid) {
        refreshOffers(true);
    } else {
        rebuildOfferList();
    }
}

void ChaosFactory::doProduceItem(Uint32 productionItemID, bool multipleMode) {
    const int offer = getOfferIndex(productionItemID);
    if(offer < 0 || remainingStock[offer] <= 0
       || getBuildItem(productionItemID) == nullptr) {
        return;
    }

    const int requested = multipleMode ? remainingStock[offer] : 1;
    for(int i = 0; i < requested && remainingStock[offer] > 0; ++i) {
        const std::size_t oldSize = currentProductionQueue.size();
        BuilderBase::doProduceItem(productionItemID, false);
        if(currentProductionQueue.size() == oldSize) {
            break;
        }
        --remainingStock[offer];
    }
}

void ChaosFactory::doCancelItem(Uint32 productionItemID, bool multipleMode) {
    const int offer = getOfferIndex(productionItemID);
    const auto countQueued = [&]() {
        return static_cast<int>(std::count_if(
            currentProductionQueue.begin(), currentProductionQueue.end(),
            [&](const ProductionQueueItem& queued) {
                return queued.itemID == productionItemID;
            }));
    };
    const int before = countQueued();
    BuilderBase::doCancelItem(productionItemID, multipleMode);
    const int after = countQueued();
    if(offer >= 0 && before > after) {
        remainingStock[offer] =
            std::min(StockPerOffer, remainingStock[offer] + before - after);
    }
}

void ChaosFactory::cancelUnfinishedOrders() {
    if(getOwner() != nullptr && currentProducedItem != ItemID_Invalid) {
        refundProductionProgress();
    }
    currentProductionQueue.clear();
    for(BuildItem& item : buildList) {
        item.num = 0;
    }
    currentProducedItem = ItemID_Invalid;
    clearProductionProgress();
    deployTimer = 0;
    bCurrentItemOnHold = false;
}

void ChaosFactory::doBuildRandom() {
    if(currentGame == nullptr || getOwner() == nullptr
       || !currentProductionQueue.empty()) {
        return;
    }
    std::vector<Uint32> affordable;
    for(int i = 0; i < OfferCount; ++i) {
        const BuildItem* item = getBuildItem(offeredItems[i]);
        if(item != nullptr && remainingStock[i] > 0
           && getOwner()->getCredits() >= static_cast<int>(item->price)) {
            affordable.push_back(offeredItems[i]);
        }
    }
    if(!affordable.empty()) {
        const int index = currentGame->randomGen.rand(
            0, static_cast<Sint32>(affordable.size()) - 1);
        doProduceItem(affordable[index]);
    }
}

void ChaosFactory::updateStructureSpecificStuff() {
    if(currentGame == nullptr) {
        return;
    }
    const Uint32 now = currentGame->getGameCycleCount();
    if(now - offerCycleStart >= CHAOS_FACTORY_OFFER_TIME) {
        cancelUnfinishedOrders();
        refreshOffers(false);
    }
}
