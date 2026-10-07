#include <structures/PoliceStation.h>
#include <structures/Palace.h>

#include <globals.h>

#include <FileClasses/GFXManager.h>
#include <House.h>
#include <Game.h>
#include <Map.h>
#include <FileClasses/TextManager.h>
#include <algorithm>
#include <Command.h>
#include <players/Player.h>
#include <players/QuantBot.h>
#include <players/AIDecisionLog.h>
#include <units/UnitBase.h>
#include <GUI/ObjectInterfaces/PoliceStationInterface.h>
#include <GUI/ObjectInterfaces/DefaultObjectInterface.h>

PoliceStation::PoliceStation(House* newOwner) : StructureBase(newOwner) {
    PoliceStation::init();
    setHealth(getMaxHealth());
}

PoliceStation::PoliceStation(InputStream& stream) : StructureBase(stream) {
    PoliceStation::init();
    if (currentGame->getLoadedSavegameVersion() >= 9824)
        spawnTimer = std::clamp(stream.readSint32(), 0, getMaxSpawnTimer());
}

void PoliceStation::init() {
    itemID = Structure_PoliceStation;
    owner->incrementStructures(itemID);

    structureSize.x = 2;
    structureSize.y = 2;
    graphicID = ObjPic_PoliceStation;
    graphic   = pGFXManager->getObjPic(graphicID, getOwner()->getHouseID());
    // Atlas is a single still frame replicated across 4 slots so
    // StructureBase's animation step has somewhere to land — matches the
    // NuclearPlant approach.
    numImagesX = 4;
    numImagesY = 1;
    firstAnimFrame = 0;
    lastAnimFrame  = 0;
    curAnimFrame   = 0;
}

PoliceStation::~PoliceStation() = default;

void PoliceStation::save(OutputStream& stream) const {
    StructureBase::save(stream);
    stream.writeSint32(spawnTimer);
}

ObjectInterface* PoliceStation::getInterfaceContainer() {
    if (pLocalHouse == owner || debug) return PoliceStationInterface::create(objectID);
    return DefaultObjectInterface::create(objectID);
}

void PoliceStation::handleSpawnClick() {
    if (pLocalPlayer) currentGame->getCommandManager().addCommand(
        Command(pLocalPlayer->getPlayerID(), CMD_POLICE_REINFORCEMENTS, objectID));
}

int PoliceStation::getMaxSpawnTimer() const {
    return Palace::getSpecialWeaponCooldownForHouse(HOUSE_FREMEN);
}

// A partial patrol is allowed while either category has room. The House checks
// resolve the effective game limit and the existing infantry-in-thirds policy.
bool PoliceStation::isUnitLimitReached() const {
    return owner != nullptr
        && owner->isUnitLimitReached(Unit_Trike)
        && owner->isUnitLimitReached(Unit_Trooper);
}

bool PoliceStation::isReinforcementBudgetReached() const {
    if (owner == nullptr) return false;
    for (const auto& player : owner->getPlayerList()) {
        const auto* bot = dynamic_cast<const QuantBot*>(player.get());
        if (bot && !bot->permitsPoliceReinforcement(0)) return true;
    }
    return false;
}

void PoliceStation::doSpawnVehicles() {
    if (!canSpawnVehicles()) return;
    int trikes = 0, troopers = 0, quads = 0;
    int blocked = 0, capped = 0, disabled = 0;
    // One patrol: a trike followed by three individual troopers.
    constexpr int patrolSize = 4;
    for (int i = 0; i < patrolSize; ++i) {
        const Uint32 item = i == 0 ? Unit_Trike : Unit_Trooper;
        // The engine admission check below runs per batch member, so the house cannot
        // overshoot the selected limit while this patrol deploys. Stop early only when
        // no category has room left for anything the patrol contains.
        if (isUnitLimitReached()) { capped += patrolSize - i; break; }
        bool militaryCapped = false;
        for (const auto& player : owner->getPlayerList()) {
            const auto* bot = dynamic_cast<const QuantBot*>(player.get());
            if (bot && !bot->permitsPoliceReinforcement(currentGame->objectData.data[item][owner->getHouseID()].price)) {
                militaryCapped = true;
                break;
            }
        }
        if (militaryCapped) { ++capped; continue; }
        if (owner->isUnitLimitReached(item)) { ++capped; continue; }
        if (!currentGame->objectData.data[item][originalHouseID].enabled) { ++disabled; continue; }
        UnitBase* unit = owner->createUnit(item);
        if (!unit) continue;
        // Search complete rings next to this station, never the whole map.
        // Buildings boxed in by traffic keep the ability ready if nothing fits.
        Coord spot = Coord::Invalid();
        const Coord origin = getLocation(), size = getStructureSize();
        for (int radius = 1; radius <= 3 && spot.isInvalid(); ++radius) {
            for (int y = origin.y - radius; y < origin.y + size.y + radius && spot.isInvalid(); ++y) {
                for (int x = origin.x - radius; x < origin.x + size.x + radius; ++x) {
                    if (x != origin.x - radius && x != origin.x + size.x + radius - 1
                        && y != origin.y - radius && y != origin.y + size.y + radius - 1) continue;
                    if (unit->canPass(x, y)) { spot = Coord(x, y); break; }
                }
            }
        }
        if (spot.isInvalid()) { ++blocked; unit->cancelDeployment(); continue; }
        unit->deploy(spot);
        unit->setGuardPoint(spot);
        unit->doSetAttackMode(GUARD);
        if (item == Unit_Trike) ++trikes;
        else if (item == Unit_Quad) ++quads;
        else ++troopers;
        AITelemetry::log().write(currentGame->getGameCycleCount(), owner->getHouseID(), -1,
            "police_unit_spawned", AITelemetry::Record().set("station", objectID)
                .set("object", unit->getObjectID()).set("item", item)
                .set("x", spot.x).set("y", spot.y)
                .set("station_x", origin.x).set("station_y", origin.y));
    }
    if (trikes + troopers + quads > 0) spawnTimer = getMaxSpawnTimer();
    else if (owner == pLocalHouse) currentGame->addToNewsTicker(_("No space or unit capacity for reinforcements"));
    AITelemetry::log().write(currentGame->getGameCycleCount(), owner->getHouseID(), -1,
        "police_reinforcements", AITelemetry::Record().set("object", objectID)
            .set("trikes", trikes).set("troopers", troopers).set("quads", quads).set("credits_charged", 0)
            .set("blocked", blocked).set("capped", capped).set("disabled", disabled)
            .set("cooldown_cycles", spawnTimer));
}

void PoliceStation::updateStructureSpecificStuff() {
    if (getHealth() <= 0) return;
    if (spawnTimer > 0) {
        --spawnTimer;
        if (spawnTimer == 0 && owner == pLocalHouse)
            currentGame->addToNewsTicker(_("Police reinforcements ready"));
    }
    if (canSpawnVehicles() && owner->isAI()
        && currentGame->getGameCycleCount() % MILLI2CYCLES(5000) == 0)
        doSpawnVehicles();
}
