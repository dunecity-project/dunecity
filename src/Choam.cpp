#include <Choam.h>

#include <Game.h>
#include <House.h>
#include <globals.h>

#include <FileClasses/TextManager.h>

#include <algorithm>

// change starport prices every minute
#define CHOAM_CHANGE_PRICETIME  (MILLI2CYCLES(60*1000))

// change amount of available units every 30s
#define CHOAM_CHANGE_AMOUNT     (MILLI2CYCLES(30*1000))

#define LOVE_FACTORY_CHANGE_AMOUNT (MILLI2CYCLES(120*1000))
#define LOVE_FACTORY_MAX_AMOUNT    5

namespace {
bool isLoveFactoryDeliveryItem(const Uint32 itemID) {
    return itemID == Delivery_Small || itemID == Delivery_Medium
        || itemID == Delivery_Heavy || itemID == Delivery_Support;
}
} // namespace

Choam::Choam(House* pHouse) : house(pHouse) {
}

Choam::~Choam() {
        ;
}

void Choam::save(OutputStream& stream) const {
    stream.writeUint32(availableItems.size());
    for(const BuildItem& buildItem : availableItems) {
        buildItem.save(stream);
    }
}

void Choam::load(InputStream& stream) {
    Uint32 num = stream.readUint32();
    for(Uint32 i=0;i<num;i++) {
        BuildItem tmp;
        tmp.load(stream);
        availableItems.push_back(tmp);
    }
}

int Choam::getPrice(Uint32 itemID, bool applyResearch) const {
    for(const BuildItem& buildItem : availableItems) {
        if(buildItem.itemID == itemID) {
            return applyResearch ? house->radarUnitPrice(itemID,
                currentGame->getGameInitSettings().campaignPurchasePrice(buildItem.price, house->getHouseID()))
                : buildItem.price;
        }
    }

    return 0;
}

bool Choam::isCheap(Uint32 itemID) const {
    return (getPrice(itemID) < currentGame->objectData.data[itemID][house->getHouseID()].price * 1.3_fix); // A bit of logic to make starports better
}

int Choam::getNumAvailable(Uint32 itemID) const {
    for(const BuildItem& buildItem : availableItems) {
        if(buildItem.itemID == itemID) {
            return buildItem.num;
        }
    }

    return INVALID;
}

bool Choam::setNumAvailable(Uint32 itemID, int newValue) {
    for(BuildItem& buildItem : availableItems) {
        if(buildItem.itemID == itemID) {
            buildItem.num = newValue;
            return (buildItem.num > 0);
        }
    }

    return false;
}

void Choam::addItem(Uint32 itemID, int num) {
    BuildItem tmp;
    tmp.itemID = itemID;
    tmp.num = num;
    availableItems.push_back(tmp);
}

void Choam::update() {
    if(availableItems.empty()) {
        return;
    }

    if((currentGame->getGameCycleCount() % CHOAM_CHANGE_AMOUNT) == 0) {
        std::vector<int> starportItems;
        for(int i = 0; i < static_cast<int>(availableItems.size()); ++i) {
            if(!isLoveFactoryDeliveryItem(availableItems[i].itemID)) {
                starportItems.push_back(i);
            }
        }

        if(!starportItems.empty()) {
            const int randomIndex = currentGame->randomGen.rand(
                (Uint32) 0, static_cast<Uint32>(starportItems.size() - 1));
            const int itemIndex = starportItems[randomIndex];
            availableItems[itemIndex].num = std::min(availableItems[itemIndex].num + 1, (Uint32) 10);
        }
    }

    if(currentGame->getGameCycleCount() > 0
       && (currentGame->getGameCycleCount() % LOVE_FACTORY_CHANGE_AMOUNT) == 0) {
        std::vector<int> restockableDeliveries;
        for(int i = 0; i < static_cast<int>(availableItems.size()); ++i) {
            if(isLoveFactoryDeliveryItem(availableItems[i].itemID)
               && availableItems[i].num < LOVE_FACTORY_MAX_AMOUNT) {
                restockableDeliveries.push_back(i);
            }
        }

        if(!restockableDeliveries.empty()) {
            const int randomIndex = currentGame->randomGen.rand(
                (Uint32) 0, static_cast<Uint32>(restockableDeliveries.size() - 1));
            ++availableItems[restockableDeliveries[randomIndex]].num;
        }
    }

    if((currentGame->getGameCycleCount() % CHOAM_CHANGE_PRICETIME) == 0) {
        for(BuildItem& buildItem : availableItems) {
            int price = currentGame->objectData.data[buildItem.itemID][house->getHouseID()].price;

            const int min_mod = 2;
            const int max_mod = 8;

            int rand1 = currentGame->randomGen.rand(min_mod, max_mod);
            int rand2 = currentGame->randomGen.rand(min_mod, max_mod);

            price = std::min((rand1+rand2)*(price/10), 999);

            buildItem.price = price;
        }

        if((pLocalHouse == house) && (house->hasStarPort())) {
            currentGame->addToNewsTicker(_("New Starport prices"));
        }
    }
}
