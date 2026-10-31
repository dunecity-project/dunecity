// Tornie upstream achievement observations adapted to DuneCity.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <Achievements/AchievementEvents.h>
#include <Achievements/AchievementManager.h>
#include <Campaign/CoopCampaignSession.h>
#include <Game.h>
#include <House.h>
#include <ObjectBase.h>
#include <Tile.h>
#include <globals.h>
#include <main.h>
#include <misc/FileSystem.h>
#include <misc/md5.h>
#include <mod/ModManager.h>
#include <structures/StructureBase.h>
#include <units/UnitBase.h>
#include <algorithm>
#include <exception>
#include <iomanip>
#include <sstream>

namespace AchievementEvents {
namespace {
bool missileImpact = false;
bool missileHit = false;
std::uint32_t missileShooter = 0;
std::set<std::uint32_t> missileBuildings;
std::uint32_t lastNotification = 0;
std::uint32_t lastSave = 0;

auto& manager() { return achievements::AchievementManager::instance(); }
bool local(const House* house) { return house && pLocalHouse && house == pLocalHouse; }
bool enemy(const House* house) {
    return house && pLocalHouse && house->getTeamID() != pLocalHouse->getTeamID();
}

bool enemiesRemain(bool initial = false) {
    const auto& destroyed = manager().match().destroyed;
    // DuneCity's thirteenth house is wildlife, not an opposing participant.
    for(const auto* unit : unitList) {
        if(!unit->getOwner()->isNatives() && enemy(unit->getOwner()) && unit->getHealth() > 0
           && (initial || !destroyed.count(unit->getObjectID()))) return true;
    }
    for(const auto* structure : structureList) {
        if(!structure->getOwner()->isNatives() && enemy(structure->getOwner()) && structure->getHealth() > 0
           && (initial || !destroyed.count(structure->getObjectID()))) return true;
    }
    return false;
}

std::string houseName(int faction) {
    static constexpr const char* names[] = {
        "Harkonnen", "Atreides", "Ordos", "Fremen", "Sardaukar", "Mercenary",
        "Neutral", "Rebels", "Corruptique", "Wildspade", "Kleshmersh", "Tharpique"
    };
    return faction >= 0 && faction < static_cast<int>(sizeof(names) / sizeof(*names))
        ? names[faction] : "Unknown";
}

bool french() { return settings.general.language == "fr"; }
}

bool initializeProfile() {
    if(!ModManager::instance().isTornieFamilyContentActive()) return false;
    try {
        const auto catalog = getDuneLegacyDataDir() + "/config/Achievements.ini";
        if(manager().configure(catalog, getDirname(getConfigFilepath()) + "/achievements.ini")) return true;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Achievements: %s", manager().error().c_str());
    } catch(const std::exception& error) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Achievements: %s", error.what());
    }
    manager().suspend();
    return false;
}

std::string dataKey(const std::string& data) {
    unsigned char digest[16];
    md5(reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest);
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for(auto byte : digest) out << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}

std::string fileKey(const std::string& name) {
    // A missing/unreadable local profile checkpoint must never prevent a game or save.
    try {
        return existsFile(name) ? dataKey(readCompleteFile(name)) : std::string();
    } catch(const std::exception&) {
        return {};
    }
}

void begin(Game& game, const std::string& key, bool resumed, bool eligible) {
    manager().end();
    missileImpact = false;
    missileBuildings.clear();
    if(!ModManager::instance().isTornieFamilyContentActive()) return;

    if(!initializeProfile()) return;
    achievements::MatchInfo info;
    const auto& setup = game.getGameInitSettings();
    switch(setup.getGameType()) {
        case GameType::Campaign: info.mode = achievements::Mode::Campaign; break;
        case GameType::Skirmish: info.mode = achievements::Mode::Skirmish; break;
        case GameType::CustomMultiplayer: info.mode = achievements::Mode::Multiplayer; break;
        default: info.mode = achievements::Mode::Custom; break;
    }
    // Workshop/derived copies share their content family's campaign progression.
    auto& mods = ModManager::instance();
    info.mod = mods.getContentBase(mods.getActiveModName());
    info.house = pLocalHouse ? houseName(pLocalHouse->getFactionID()) : "Unknown";
    info.chaosCampaignEligible = setup.isChaosCampaignEligible();
    if(const auto& context = setup.getCoopCampaignContext(); context && pLocalHouse) {
        const int slot = coop::mapSlotForHouse(*context, pLocalHouse->getHouseID());
        info.coopSession = context->sessionId;
        const bool localAlly = slot >= 1 && slot <= static_cast<int>(context->slots.size())
            && context->isAlliedSlot(slot - 1) && context->slots[slot - 1].active();
        unsigned allies = 0;
        for(std::size_t index = 0; index < context->slots.size(); ++index)
            if(context->isAlliedSlot(index) && context->slots[index].active()) ++allies;
        info.cooperative = localAlly && allies >= 2;
        info.coopCompletionEligible = localAlly && context->stage == coop::StageCount
            && context->completedMask == (1u << (coop::StageCount - 1)) - 1u;
    }
    info.mission = setup.getMission();
    info.enabled = eligible && pLocalHouse && !pLocalHouse->isNatives() && !game.isObserving();
    if(pLocalHouse) {
        info.color = getHouseVisualHouse(pLocalHouse->getHouseID());
        info.defaultColor = getDefaultHouseColorSlot(static_cast<HOUSETYPE>(pLocalHouse->getFactionID()));
        info.initialRefinedSpice = static_cast<std::uint64_t>(std::max(0, pLocalHouse->getHarvestedSpice().floor()));
    }
    info.hadEnemies = enemiesRemain(true);
    info.roadkillWindow = MILLI2CYCLES(2000);
    for(const auto& house : setup.getHouseInfoList()) {
        if(!pLocalHouse || house.team == pLocalHouse->getTeamID()) continue;
        for(const auto& player : house.playerInfoList)
            if(player.playerClass.find("Hard") != std::string::npos
               || player.playerClass.find("Brutal") != std::string::npos) info.highDifficulty = true;
    }
    manager().begin(info, key, resumed);
    if(!manager().isActive()) return;
    lastNotification = lastSave = SDL_GetTicks();
    for(const auto* unit : unitList)
        if(unit->getItemID() == Unit_FlameTank) manager().flameSource(unit->getObjectID());
}

void finish(bool won) {
    if(!manager().isActive()) return;
    bool allyEliminated = false;
    bool allySurvives = false;
    if(currentGame && pLocalHouse && manager().match().info.cooperative) {
        if(const auto& context = currentGame->getGameInitSettings().getCoopCampaignContext(); context)
            for(std::size_t index = 0; index < context->slots.size(); ++index) {
                if(!context->isAlliedSlot(index) || !context->slots[index].active()) continue;
                const auto* house = currentGame->getHouse(context->slots[index].house);
                if(!house || house == pLocalHouse) continue;
                if(house->isAlive()) allySurvives = true;
                else allyEliminated = true;
            }
    }
    manager().finish(won, enemiesRemain(), pLocalHouse && !pLocalHouse->isAlive(), allyEliminated, allySurvives);
}

void pump(Game& game) {
    if(!manager().isActive()) return;
    const auto now = SDL_GetTicks();
    if(now - lastSave >= 5000) {
        manager().save();
        lastSave = now;
    }
    if(now - lastNotification < 4000) return;
    const auto id = manager().takeNotification();
    if(id.empty()) return;
    for(const auto& achievement : manager().definitions()) {
        if(achievement.id != id) continue;
        game.addToNewsTicker(std::string(french() ? "Haut fait débloqué : " : "Achievement unlocked: ")
            + manager().displayName(achievement, french()));
        break;
    }
    lastNotification = now;
}

void damage(ObjectBase* victim, std::uint32_t attackerID, House* attacker, bool lethal) {
    if(!manager().isActive()) return;
    if(missileImpact && victim && local(attacker) && attackerID == missileShooter) {
        missileHit = true;
        if(lethal && victim->isAStructure() && victim->getItemID() != Structure_Wall && enemy(victim->getOwner()))
            missileBuildings.insert(victim->getObjectID());
    }
    if(victim && lethal && victim->isAUnit() && victim->getItemID() != Unit_Sandworm && pLocalHouse
       && (local(victim->getOwner()) || victim->getOriginalHouseID() == pLocalHouse->getHouseID()))
        manager().unitLost(victim->getObjectID());
    const bool flame = manager().match().flameSources.count(attackerID) > 0;
    if(victim && victim->getItemID() == Unit_Sandworm)
        manager().wormSource(victim->getObjectID(), local(attacker) ? (flame ? 2 : 1) : 0);
    if(!currentGame || !victim || !local(attacker) || !enemy(victim->getOwner())) return;
    const auto* source = currentGame->getObjectManager().getObject(attackerID);
    if(source && source->getItemID() == Unit_Sandworm) return;
    if(lethal && source && source->getItemID() == Unit_Saboteur && victim->getItemID() == Structure_Palace
       && !manager().match().destroyed.count(victim->getObjectID())) manager().palaceSabotaged();
    manager().enemyDamage();
    if(lethal && victim->getItemID() != Unit_Sandworm)
        manager().enemyDestroyed(victim->getObjectID(), victim->isAStructure(), false, flame);
}

void wormDefeated(UnitBase* unit) {
    if(!manager().isActive()) return;
    if(unit && unit->getItemID() == Unit_Sandworm && unit->getHealth() <= unit->getMaxHealth() / 2) {
        const auto source = manager().match().wormSources.find(unit->getObjectID());
        if(source != manager().match().wormSources.end() && source->second > 0)
            manager().enemyDestroyed(unit->getObjectID(), false, true, source->second == 2);
    }
}

void lost(UnitBase* unit) {
    if(!manager().isActive()) return;
    wormDefeated(unit);
    if(!unit || !pLocalHouse || (!local(unit->getOwner()) && unit->getOriginalHouseID() != pLocalHouse->getHouseID())) return;
    // Healthy MCV deployment and off-map transport departure are not casualties.
    if(unit->getHealth() > 0 && !unit->isVisible()
       && (unit->getItemID() == Unit_MCV || unit->getItemID() == Unit_Frigate || isCarryallUnit(unit->getItemID()))) return;
    manager().unitLost(unit->getObjectID());
}

void sold(House* owner, int refund) {
    if(manager().isActive() && local(owner)) manager().sold(static_cast<std::uint64_t>(std::max(0, refund)));
}

void built(House* house, ObjectBase* object, bool produced) {
    if(!manager().isActive() || !object) return;
    if(object->getItemID() == Unit_FlameTank) manager().flameSource(object->getObjectID());
    if(!produced || !local(house)) return;
    const char* special = "";
    switch(object->getItemID()) {
        case Unit_RocketTrike: special = "RocketTrike"; break;
        case Unit_SonicTrike: special = "SonicTrike"; break;
        case Unit_FlameTank: special = "FlameTank"; break;
        case Unit_EliteLauncher: special = "EliteLauncher"; break;
        case Unit_EliteSiegeTank: special = "EliteSiegeTank"; break;
        case Unit_ChemicalSiegeTank: special = "ChemicalSiegeTank"; break;
        case Unit_RaiderQuad: special = "RaiderQuad"; break;
        case Unit_RangedTank: special = "RangedTank"; break;
        case Unit_SlowTank: special = "SlowTank"; break;
        case Unit_CapacityHarvester: special = "CapacityHarvester"; break;
        case Unit_FastHarvester: special = "FastHarvester"; break;
        case Unit_TameSandworm: special = "TameSandworm"; break;
        case Unit_FlameQuad: special = "FlameQuad"; break;
        case Unit_GasInfantry: special = "GasInfantry"; break;
        case Unit_FlameTrooper: special = "FlameTrooper"; break;
        case Unit_EliteOrnithopter: special = "EliteOrnithopter"; break;
        case Unit_ChemicalCarryall: special = "ChemicalCarryall"; break;
        case Unit_RebelHarvester: special = "Harvestank"; break;
        default: break;
    }
    manager().built(object->isAStructure(), special);
}

void captured(House* captor, House* previous) {
    if(manager().isActive() && local(captor) && enemy(previous)) manager().captured();
}
void crushed(ObjectBase* victim, ObjectBase* vehicle) {
    if(manager().isActive() && currentGame && victim && vehicle && local(vehicle->getOwner()) && enemy(victim->getOwner()))
        manager().crushed(victim->getObjectID(), vehicle->getObjectID(), currentGame->getGameCycleCount());
}
void refined(House* house, std::uint64_t total) {
    if(manager().isActive() && local(house)) manager().refinedSpice(total);
}
void spice(House* house, const Tile* tile) {
    if(!manager().isActive() || !local(house) || !tile) return;
    manager().harvestedType(tile->isRedSpice() ? 2 : tile->isGreenSpice() ? 4
        : tile->isPaleLilacSpice() ? 8 : tile->isWhiteSpice() ? 16 : 1);
}
void bloom(House* house) { if(manager().isActive() && local(house)) manager().bloom(); }
void missile(House* house) { if(manager().isActive() && local(house)) manager().missile(); }
void beginMissileImpact(House* house, std::uint32_t shooter) {
    missileImpact = manager().isActive() && local(house);
    missileShooter = shooter;
    missileHit = false;
    missileBuildings.clear();
}
void endMissileImpact() {
    if(missileImpact) manager().missileResult(missileHit, missileBuildings.size());
    missileImpact = false;
    missileBuildings.clear();
}
void palace(House* house) { if(manager().isActive() && local(house)) manager().palace(); }
}
