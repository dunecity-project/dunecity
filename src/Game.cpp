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

#include <Game.h>
#include <misc/OMemoryStream.h>
#include <GUI/dune/JoinProgressWindow.h>
#include <GUI/dune/JoinRequestsWindow.h>
#include <GUI/dune/FeedbackWindow.h>
#include <misc/CampaignControls.h>
#include <main.h>
#include <cstdarg>
#include <ctime>
#include <chrono>
#include <cmath>

// Initialize static performance logging members
std::ofstream Game::performanceLogFile;
std::mutex Game::performanceLogMutex;

#include <globals.h>
#include <config.h>
#include <players/AIDecisionLog.h>
#include <main.h>

#include <FileClasses/FileManager.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/FontManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/music/MusicPlayer.h>
#include <FileClasses/LoadSavePNG.h>
#include <SoundPlayer.h>
#include <misc/IFileStream.h>
#include <misc/OFileStream.h>
#include <misc/IMemoryStream.h>
#include <misc/FileSystem.h>
#include <misc/FrameYield.h>
#include <misc/fnkdat.h>
#include <misc/WebRuntime.h>
#include <misc/draw_util.h>
#include <misc/md5.h>
#include <misc/exceptions.h>
#include <misc/format.h>
#include <misc/SDL2pp.h>
#include <misc/DiscordManager.h>
#include <misc/SaveCompat.h>
#include <misc/TouchInput.h>
#include <CursorManager.h>
#include <misc/CursorAppearance.h>

#include <players/HumanPlayer.h>
#include <players/QuantBot.h>

#include <CommandBufferPolicy.h>

#include <Network/NetworkManager.h>
#include <Network/MetaServerClient.h>
#include <Network/PathBudgetSync.h>
#include <PathWorkerPool.h>
#include <mod/ModManager.h>
#include <Network/WorkshopGameContent.h>
#include <Network/MapCollection.h>

#include <GUI/dune/InGameMenu.h>
#include <GUI/QstBox.h>
#include <GUI/dune/WaitingForOtherPlayers.h>
#include <GUI/dune/CityBudgetWindow.h>
#include <Menu/MentatHelp.h>
#include <Menu/BriefingMenu.h>
#include <Menu/MapChoice.h>

#include <House.h>
#include <Map.h>
#include <SpatialGrid.h>
#include <Bullet.h>
#include <AStarSearch.h>
#include <Explosion.h>
#include <GameInitSettings.h>
#include <ScreenBorder.h>
#include <sand.h>

#include <structures/StructureBase.h>
#include <structures/ZoneStructure.h>
#include <structures/WindTrap.h>
#include <structures/AdvancedWindTrap.h>
#include <structures/NuclearPlant.h>
#include <structures/Scoutpost.h>
#include <structures/ConstructionYard.h>
#include <units/UnitBase.h>
#include <structures/BuilderBase.h>
#include <structures/Palace.h>
#include <units/Harvester.h>
#include <units/HarvesterHelpers.h>
#include <units/InfantryBase.h>
#include <units/GroundUnit.h>
#include <units/Carryall.h>
#include <units/AmbientAirplane.h>
#include <units/AmbientHelicopter.h>
#include <structures/Airport.h>

#include <algorithm>
#include <exception>
#include <sstream>
#include <iomanip>

namespace {

constexpr Uint32 SAVE_SETUP_COLOR_MARKER = 0x53434F4C; // "SCOL"

constexpr std::array<Uint32, 8> analyticsMixItems = {
    Unit_Tank, Unit_SiegeTank, Unit_Launcher, 9999, // QBot's special-unit group
    Unit_Ornithopter, Unit_Trike, Unit_RaiderTrike, Unit_Quad
};

std::string analyticsGameType(GameType type) {
    switch (type) {
        case GameType::Campaign: return "campaign";
        case GameType::Skirmish: return "skirmish";
        case GameType::CustomGame: return "single_custom";
        case GameType::CustomMultiplayer: return "multiplayer";
        case GameType::LoadSavegame:
        case GameType::LoadMultiplayer: return "load";
        default: return "unknown";
    }
}

std::string analyticsArray(const std::vector<std::string>& rows) {
    std::string output = "[";
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i != 0) output += ',';
        output += rows[i];
    }
    return output + ']';
}

std::string analyticsAIType(const std::string& playerClass) {
    if (playerClass.rfind("qBot", 0) == 0) return "qbot";
    if (playerClass.rfind("mentat", 0) == 0) return "mentat";
    if (playerClass.rfind("AIPlayer", 0) == 0) return "classic";
    if (playerClass == "CampaignAIPlayer") return "campaign";
    if (playerClass == "SmartBot") return "smartbot";
    return "unknown";
}

std::string analyticsAIDifficulty(const std::string& playerClass) {
    static constexpr std::array<std::pair<const char*, const char*>, 5> suffixes = {{
        {"Defend", "defend"}, {"Easy", "easy"}, {"Medium", "medium"},
        {"Hard", "hard"}, {"Brutal", "brutal"},
    }};
    for (const auto& [suffix, name] : suffixes) {
        const size_t suffixLength = std::char_traits<char>::length(suffix);
        if (playerClass.size() >= suffixLength
            && playerClass.compare(playerClass.size() - suffixLength, suffixLength, suffix) == 0) {
            return name;
        }
    }
    if (playerClass == "SmartBot") return "normal";
    return {};
}

std::string analyticsMatchID(const GameInitSettings& settings) {
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::ostringstream id;
    id << "m1-" << std::hex << static_cast<uint64_t>(micros) << '-'
       << settings.getRandomSeed() << '-' << SDL_GetPerformanceCounter();
    return id.str();
}

DuneCity::CityTilePlacementState makeCityTilePlacementState(const Tile& tile) {
    return DuneCity::makeCityTilePlacementState(
        tile.isRock(),
        tile.isMountain(),
        tile.hasAGroundObject(),
        tile.hasCityZone(),
        tile.isRoad());
}

bool hasVisibleRoadNeighbor(int x, int y) {
    constexpr int dx[] = {0, 1, 0, -1};
    constexpr int dy[] = {-1, 0, 1, 0};

    for (int i = 0; i < 4; ++i) {
        const int nx = x + dx[i];
        const int ny = y + dy[i];

        if (!currentGameMap->tileExists(nx, ny)) {
            continue;
        }

        const Tile* neighbor = currentGameMap->getTile(nx, ny);
        if (neighbor->isRoad() && !neighbor->hasCityZone()) {
            return true;
        }
    }

    return false;
}

struct StructurePlacementPreview {
    unsigned int objectPic = NUM_OBJPICS;
    int framesX = 1;
    int framesY = 1;
    int frame = 0;
    int overlayFrame = -1;
};

constexpr int TornieStructureFrame_BuildSite = 0;
constexpr int TornieStructureFrame_Destroyed = 1;
constexpr int TornieStructureFrame_Active = 2;
constexpr int TornieWindtrapPreviewFramesX = 10;
constexpr int TornieWindtrapPreviewFramesY = 7;

bool getTornieStructurePlacementPreview(int itemID, StructurePlacementPreview& preview) {
    switch(itemID) {
        case Structure_AdvancedWindTrap:
            preview = {
                ObjPic_AdvancedWindTrap,
                TornieWindtrapPreviewFramesX,
                TornieWindtrapPreviewFramesY,
                TornieStructureFrame_BuildSite,
                -1
            };
            return true;

        case Structure_AdvancedWindTrapMK2:
            preview = {
                ObjPic_AdvancedWindTrap2x3,
                TornieWindtrapPreviewFramesX,
                TornieWindtrapPreviewFramesY,
                TornieStructureFrame_BuildSite,
                -1
            };
            return true;

        case Structure_AdvancedWindTrapMK3:
            preview = {
                ObjPic_AdvancedWindTrap3x2,
                TornieWindtrapPreviewFramesX,
                TornieWindtrapPreviewFramesY,
                TornieStructureFrame_BuildSite,
                -1
            };
            return true;

        case Structure_Worfinery:
            preview = { ObjPic_Worfinery, 10, 1, TornieStructureFrame_BuildSite, -1 };
            return true;

        case Structure_TechCenter:
            preview = { ObjPic_TechCenter, 4, 1, TornieStructureFrame_BuildSite, -1 };
            return true;

        case Structure_Scoutpost:
            preview = { ObjPic_Scoutpost, 4, 1, TornieStructureFrame_BuildSite, -1 };
            return true;

        case Structure_Flamepost:
            preview = { ObjPic_Flamepost, 4, 1, TornieStructureFrame_BuildSite, -1 };
            return true;

        case Structure_Chemipost:
            preview = { ObjPic_Chemipost, 4, 1, TornieStructureFrame_BuildSite, -1 };
            return true;

        case Structure_LoveFactory:
            preview = { ObjPic_LoveFactory, 10, 1, TornieStructureFrame_BuildSite, -1 };
            return true;

        case Structure_ChaosFactory:
            preview = { ObjPic_ChaosFactory, 4, 1, TornieStructureFrame_BuildSite, -1 };
            return true;

        default:
            return false;
    }
}

} // namespace

Game::Game() {
    currentZoomlevel = settings.video.preferredZoomLevel;

    localPlayerName = settings.general.playerName;

    // City-sim features are gated by the active mod. A savegame load may
    // re-enable this later if the save contains city-sim state.
    citySimEnabled_ = ModManager::instance().isCityModeActive();

    unitList.clear();       //holds all the units
    structureList.clear();  //all the structures
    bulletList.clear();

    sideBarPos = calcAlignedDrawingRect(pGFXManager->getUIGraphic(UI_SideBar), HAlign::Right, VAlign::Top);
    topBarPos = calcAlignedDrawingRect(pGFXManager->getUIGraphic(UI_TopBar), HAlign::Left, VAlign::Top);

    // set to true for now
    debug = false;

    powerIndicatorPos.h = spiceIndicatorPos.h = settings.video.height - 146 - 2;

    musicPlayer->changeMusic(MUSIC_PEACE);
    //////////////////////////////////////////////////////////////////////////
    SDL_Rect gameBoardRect = { 0, topBarPos.h, sideBarPos.x, getRendererHeight() - topBarPos.h };
    screenborder = new ScreenBorder(gameBoardRect);
}

void Game::initializeSpatialGrid(int mapWidth, int mapHeight) {
    constexpr int kSpatialGridCellSize = 4;

    if(mapWidth <= 0 || mapHeight <= 0) {
        spatialGrid.reset();
        return;
    }

    try {
        spatialGrid = std::make_unique<SpatialGrid>(mapWidth, mapHeight, kSpatialGridCellSize);
    } catch(const std::exception& ex) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize spatial grid (%s)", ex.what());
        spatialGrid.reset();
    }
}

void Game::queueTargetRequest(Uint32 objectId) {
    if(objectId == NONE_ID) {
        return;
    }

    if(pendingTargetRequestIds.insert(objectId).second) {
        targetRequestQueue.push_back({objectId});
    }
}

void Game::queuePathRequest(Uint32 objectId) {
    if(objectId == NONE_ID) {
        return;
    }

    if(pendingPathRequestIds.insert(objectId).second) {
        pathRequestQueue.push_back({objectId});
    }
}

void Game::invalidateCarryallCandidateIds() {
    carryallCandidateCycle = std::numeric_limits<Uint32>::max();
    carryallCandidateUnitCount = std::numeric_limits<size_t>::max();
    carryallCandidateIds.clear();
}

const std::vector<Uint32>& Game::getCarryallCandidateIds() {
    // Unit creation/removal invalidates this index, including equal-count replacements within
    // one cycle. The count check also catches bulk lifecycle changes. Existence, owner, activity,
    // health and booking are revalidated by the caller on every lookup.
    if(carryallCandidateCycle != gameCycleCount || carryallCandidateUnitCount != unitList.size()) {
        carryallCandidateIds.clear();
        for(const UnitBase* unit : unitList) {
            if(unit != nullptr && isCarryallUnit(unit->getItemID())) {
                carryallCandidateIds.push_back(unit->getObjectID());
            }
        }
        carryallCandidateCycle = gameCycleCount;
        carryallCandidateUnitCount = unitList.size();
    }
    return carryallCandidateIds;
}

bool Game::isLegalRecoveryTile(const UnitBase* pUnit, const Coord& candidate) const {
    if(!currentGameMap->tileExists(candidate)) {
        return false;
    }

    // The unit's own rule for the tile: terrain, occupancy and, for infantry, how many of its own
    // team already stand there.
    if(!pUnit->canPass(candidate.x, candidate.y)) {
        return false;
    }

    const Tile* pTile = currentGameMap->getTile(candidate);

    // canPass() lets a vehicle drive onto enemy infantry because it would squash them on the way
    // through. Arriving on top of them is not the same thing, so a recovery takes a free tile, the
    // way a carryall drop does.
    if(!pUnit->isInfantry() && pTile->hasAGroundObject()) {
        return false;
    }

    // Recovery must not be a quiet way of destroying the unit, and deploying onto a bloom
    // triggers it under the unit's feet.
    return !pTile->isSpiceBloom() && !pTile->isSpecialBloom();
}

Coord Game::findPassengerRecoverySpot(const UnitBase* pUnit, const Coord& anchor) const {
    // Near where it was lost first: a recovery that searched the whole map before looking next
    // door would scatter an army. The map-wide fallback below handles a corner that stays blocked.
    static constexpr int maxRecoveryRadius = 12;

    for(int radius = 0; radius <= maxRecoveryRadius; ++radius) {
        for(int dy = -radius; dy <= radius; ++dy) {
            for(int dx = -radius; dx <= radius; ++dx) {
                if(std::max(std::abs(dx), std::abs(dy)) != radius) {
                    continue;  // Only the new ring; the inner ones were searched already.
                }

                const Coord candidate(anchor.x + dx, anchor.y + dy);
                if(isLegalRecoveryTile(pUnit, candidate)) {
                    return candidate;
                }
            }
        }
    }

    return Coord::Invalid();
}

Coord Game::findGlobalRecoverySpot(const UnitBase* pUnit, Uint32 windowIndex) const {
    // When nothing near the anchor is legal, a bounded window of the map is searched instead.
    // The window start advances by its own width every turn in the recovery queue, so turns walk the whole
    // map and a unit whose own corner is permanently walled in still gets out. Derived from the
    // queue round and the unit id only: no cursor is kept, so nothing has to be saved.
    static constexpr Uint32 globalWindowTiles = 1024;

    const Uint32 width = static_cast<Uint32>(currentGameMap->getSizeX());
    const Uint32 total = width * static_cast<Uint32>(currentGameMap->getSizeY());
    if(total == 0) {
        return Coord::Invalid();
    }

    const Uint32 start = static_cast<Uint32>((static_cast<Uint64>(windowIndex) * globalWindowTiles
                                            + pUnit->getObjectID()) % total);
    const Uint32 examined = std::min(globalWindowTiles, total);
    for(Uint32 step = 0; step < examined; ++step) {
        const Uint32 index = (start + step) % total;
        const Coord candidate(static_cast<int>(index % width), static_cast<int>(index / width));
        if(isLegalRecoveryTile(pUnit, candidate)) {
            return candidate;
        }
    }

    return Coord::Invalid();
}

bool Game::recoverOrphanedPassenger(UnitBase* pUnit, const Coord& anchor, Uint32 windowIndex) {
    // Stale bookings go first, and reciprocally: leaving the other aircraft's target set would
    // send it after a unit that is back on the map and no longer expecting a lift. The anchor was
    // captured before any of this, because clearing the target is what makes the tile search see
    // the unit's real options.
    if(pUnit->isAGroundUnit()) {
        auto* passenger = static_cast<GroundUnit*>(pUnit);
        passenger->releaseReplacementPickup(nullptr);
        passenger->bookCarrier(nullptr);
    }
    pUnit->setTarget(nullptr);

    Coord spot = currentGameMap->tileExists(anchor) ? findPassengerRecoverySpot(pUnit, anchor)
                                                    : Coord::Invalid();
    if(spot.isInvalid()) {
        spot = findGlobalRecoverySpot(pUnit, windowIndex);
    }
    if(spot.isInvalid()) {
        return false;
    }

    // The same path a carryall drop uses: map occupancy, the spatial grid, visibility,
    // respondability and a sane attack mode all come from deploy().
    pUnit->setForced(false);
    pUnit->deploy(spot);
    return true;
}

void Game::reconcileTransportContainment() {
    // Pass one: what the containers actually hold. Every house is scanned, not just one: a
    // deviated carryall keeps flying its original owner's cargo, and a passenger's own house can
    // change while it is hidden, so ownership says nothing about who holds whom.
    std::unordered_set<Uint32> contained;
    std::unordered_map<Uint32, Carryall*> claimant;   ///< Actual cargo ownership only.
    std::vector<std::pair<Carryall*, Uint32>> staleCargo;        ///< Entries that are not cargo.
    std::vector<std::pair<Carryall*, Uint32>> duplicateCargo;    ///< One extra entry in one carrier.
    std::vector<Uint32> candidates;
    Coord houseAnchor[NUM_HOUSES];
    for(Coord& anchor : houseAnchor) {
        anchor.invalidate();
    }

    for(UnitBase* pUnit : unitList) {
        if(pUnit == nullptr) {
            continue;
        }

        if(auto* carryall = dynamic_cast<Carryall*>(pUnit)) {
            for(const Uint32 cargoID : carryall->getCargoIds()) {
                // Live passenger validity, not merely "the id resolves": a structure or a unit
                // standing on the map is not cargo, and an entry naming one is unusable.
                auto* passenger = dynamic_cast<UnitBase*>(objectManager.getObject(cargoID));
                if(passenger == nullptr || !passenger->isPickedUp() || passenger->isActive()) {
                    staleCargo.emplace_back(carryall, cargoID);   // Including NONE_ID.
                    continue;
                }

                const auto claim = claimant.emplace(cargoID, carryall);
                if(claim.second) {
                    contained.insert(cargoID);   // The one claim that counts as containment.
                } else if(claim.first->second == carryall) {
                    duplicateCargo.emplace_back(carryall, cargoID);  // Listed twice by one carrier.
                } else {
                    staleCargo.emplace_back(carryall, cargoID);      // A second carrier's claim.
                }
            }

            // A pickup request is not cargo. An inactive ground target cannot be collected;
            // release the stale request just as engageTarget() does, without letting it displace
            // a later carrier's real passenger claim.
            if(ObjectBase* pTarget = carryall->getTarget();
               pTarget != nullptr && pTarget->isAGroundUnit() && !pTarget->isActive()) {
                carryall->setTarget(nullptr);
            }
        }

        const int houseID = pUnit->getOwner()->getHouseID();
        if(pUnit->isActive() && !pUnit->isPickedUp() && currentGameMap->tileExists(pUnit->getLocation())
           && houseAnchor[houseID].isInvalid()) {
            houseAnchor[houseID] = pUnit->getLocation();
        }

        if(pUnit->isAGroundUnit() && pUnit->isPickedUp() && !pUnit->isActive() && pUnit->getHealth() > 0) {
            candidates.push_back(pUnit->getObjectID());
        }
    }

    for(StructureBase* pStructure : structureList) {
        if(pStructure == nullptr) {
            continue;
        }

        // Only a running job counts. A refinery keeps pointing at the last harvester it released,
        // so a free drop-off's pointer is history; believing it would hide a unit for ever.
        if(pStructure->acceptsHarvesterDropoff() && !pStructure->isHarvesterDropoffFree()) {
            if(const UnitBase* occupant = pStructure->getContainedHarvesterUnit()) {
                contained.insert(occupant->getObjectID());
            }
        }
        if(const UnitBase* occupant = pStructure->getContainedRepairUnit()) {
            contained.insert(occupant->getObjectID());   // Already gated on an actual repair job.
        }

        const int houseID = pStructure->getOwner()->getHouseID();
        if(houseAnchor[houseID].isInvalid() && currentGameMap->tileExists(pStructure->getLocation())) {
            houseAnchor[houseID] = pStructure->getLocation();
        }
    }

    for(const auto& entry : staleCargo) {
        entry.first->releaseCargoId(entry.second);
    }
    for(const auto& entry : duplicateCargo) {
        entry.first->releaseExtraCargoId(entry.second);   // Keeps the one real claim.
    }

    // Pass two: hidden units no container holds. Object-id order, so the batch below is the same
    // batch on every peer whatever order the unit list happens to be in.
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                    [&contained](const Uint32 id) { return contained.count(id) > 0; }),
                     candidates.end());

    // Bounded work per pass, rotated by the pass index so a long queue of hidden units is worked
    // through instead of the same prefix being retried. Both the batch position and the map-wide
    // search window below come from the cycle counter and the candidate order alone: no chooser
    // cursor exists, so nothing new has to be saved and every peer makes the same choices.
    static constexpr size_t recoveryBatch = 32;
    const Uint32 passIndex = gameCycleCount / transportContainmentIntervalCycles;
    int recovered = 0;
    int unplaceable = 0;
    const size_t attempts = std::min(recoveryBatch, candidates.size());
    const Uint64 firstSlot = static_cast<Uint64>(passIndex) * attempts;
    const size_t offset = candidates.empty() ? 0 : static_cast<size_t>(firstSlot % candidates.size());

    for(size_t step = 0; step < attempts; ++step) {
        const Uint32 candidateID = candidates[(offset + step) % candidates.size()];
        auto* pUnit = dynamic_cast<UnitBase*>(objectManager.getObject(candidateID));
        if(pUnit == nullptr) {
            continue;
        }

        // Where it was lost, then where it was told to hold, then anything its house still holds,
        // and finally the middle of the map: a unit whose house owns nothing else must still have
        // somewhere to come back to.
        Coord anchor = pUnit->getLocation();
        if(!currentGameMap->tileExists(anchor)) {
            anchor = pUnit->getGuardPoint();
        }
        if(!currentGameMap->tileExists(anchor)) {
            anchor = houseAnchor[pUnit->getOwner()->getHouseID()];
        }
        if(!currentGameMap->tileExists(anchor)) {
            anchor = Coord(currentGameMap->getSizeX() / 2, currentGameMap->getSizeY() / 2);
        }

        // Advance the map window once per queue round, rather than once per global pass.
        // Otherwise a unit in a 64-unit batch is only tried on alternate passes and its windows
        // can permanently skip half of a power-of-two map. No saved cursor is needed.
        const Uint32 windowIndex = static_cast<Uint32>((firstSlot + step) / candidates.size());
        if(recoverOrphanedPassenger(pUnit, anchor, windowIndex)) {
            ++recovered;
        } else {
            ++unplaceable;
        }
    }

    recoveredPassengerCount += static_cast<Uint32>(recovered);
    unplaceablePassengerCount = static_cast<Uint32>(unplaceable);
    pendingPassengerCount = static_cast<Uint32>(candidates.size() - attempts + unplaceable);

    if(recovered > 0 || unplaceable > 0 || !staleCargo.empty() || !duplicateCargo.empty()) {
        SDL_Log("Transport containment at cycle %u: recovered %d hidden unit(s), %d with no legal"
                " tile yet, %u still queued, dropped %zu unusable and %zu duplicate cargo entries",
                gameCycleCount, recovered, unplaceable, pendingPassengerCount, staleCargo.size(),
                duplicateCargo.size());
    }
}

/**
    The destructor frees up all the used memory.
*/
Game::~Game() {
    // No path task can be outstanding here -- runBatch joins before returning --
    // but the threads themselves must not outlive the match, so they are stopped
    // and joined rather than left idle across a new game.
    activePathJobs.clear();
    PathWorkerPool::instance().shutdown();
    finishMatchAnalytics();
    if (AITelemetry::log().enabled()) {
        AITelemetry::Record roster;
        for (int h = 0; h < NUM_HOUSES; ++h) if (house[h]) {
            for (const auto& player : house[h]->getPlayerList()) player->finishTelemetry();
            AITelemetry::Record actual, built, lost;
            for (int item = ItemID_FirstID; item <= ItemID_LastID; ++item) {
                if (house[h]->getNumItems(item)) actual.set(std::to_string(item), house[h]->getNumItems(item));
                if (house[h]->getNumBuiltItems(item)) built.set(std::to_string(item), house[h]->getNumBuiltItems(item));
                if (house[h]->getNumLostItems(item)) lost.set(std::to_string(item), house[h]->getNumLostItems(item));
            }
            roster.set(std::to_string(h), AITelemetry::Record().set("name", getHouseNameByNumber(static_cast<HOUSETYPE>(h)))
                .set("team", house[h]->getTeamID()).set("alive", house[h]->isAlive()).set("credits", house[h]->getCredits())
                .set("actual", actual).set("built", built).set("lost", lost)
                .set("economy_totals", AITelemetry::log().economyTotals(h)).set("combat_rewards", house[h]->combatRewardStats(objectData))
                .set("deviation_rewards", house[h]->deviationRewardStats()));
        }
        AITelemetry::log().write(gameCycleCount, -1, -1, "game_summary", AITelemetry::Record()
            .set("local_result", finished ? (won ? "victory" : "defeat") : "ended_without_result")
            .set("local_house", pLocalHouse ? pLocalHouse->getHouseID() : -1).set("houses", roster));
    }
    AITelemetry::log().stop();
    // Close performance log
    closePerformanceLog();
    
    // Clean up cursor manager
    cursorManager.cleanup();

    if(pNetworkManager != nullptr) {
        pNetworkManager->setOnReceiveChatMessage(std::function<void (const std::string&, const std::string&)>());
        pNetworkManager->setOnReceiveCommandList(std::function<void (const std::string&, const CommandList&)>());
        pNetworkManager->setOnReceiveSelectionList(std::function<void (const std::string&, const std::set<Uint32>&, int)>());
        pNetworkManager->setOnPeerDisconnected(std::function<void (const std::string&, bool, int)>());
        pNetworkManager->setOnReceiveClientStats({});
        pNetworkManager->setOnReceiveSetPathBudget({});
        pNetworkManager->setOnReceiveMatchControl({});
        pNetworkManager->setOnReceiveMatchResumeRequest({});
        pNetworkManager->setOnReceiveRelayDiagnostic({});
    }

    for(StructureBase* pStructure : structureList) {
        delete pStructure;
    }
    structureList.clear();

    for(UnitBase* pUnit : unitList) {
        delete pUnit;
    }
    unitList.clear();

    for(Bullet* pBullet : bulletList) {
        delete pBullet;
    }
    bulletList.clear();

    for(Explosion* pExplosion : explosionList) {
        delete pExplosion;
    }
    explosionList.clear();

    if(spatialGrid) {
        spatialGrid->clear();
        spatialGrid.reset();
    }

    if(spectatorViewPlayer) { pLocalPlayer=nullptr; spectatorViewPlayer.reset(); }
    delete currentGameMap;
    currentGameMap = nullptr;
    delete screenborder;
    screenborder = nullptr;
}

void Game::initPerformanceLog() {
    if (!settings.general.diagnosticLogs) return;
    std::lock_guard<std::mutex> lock(performanceLogMutex);
    
    if(performanceLogFile.is_open()) {
        return;  // Already initialized
    }
    
    std::string logPath = getPerformanceLogFilepath();
    performanceLogFile.open(logPath, std::ios::out | std::ios::trunc);
    
    if(performanceLogFile.is_open()) {
        auto now = std::chrono::system_clock::now();
        std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        char timeStr[100];
        std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", std::localtime(&nowTime));
        
        performanceLogFile << "=== Dune City Performance Log Started " << timeStr << " ===" << std::endl;
        performanceLogFile << "Version: " << VERSION << std::endl;
        performanceLogFile << "Platform: " << SDL_GetPlatform() << std::endl;
        performanceLogFile << std::endl;
        performanceLogFile.flush();
        SDL_Log("Performance log initialized: %s", logPath.c_str());
    } else {
        SDL_Log("Warning: Could not open performance log file: %s", logPath.c_str());
    }
}

void Game::closePerformanceLog() {
    std::lock_guard<std::mutex> lock(performanceLogMutex);
    
    if(performanceLogFile.is_open()) {
        auto now = std::chrono::system_clock::now();
        std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        char timeStr[100];
        std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", std::localtime(&nowTime));
        
        performanceLogFile << std::endl;
        performanceLogFile << "=== Performance Log Closed " << timeStr << " ===" << std::endl;
        performanceLogFile.close();
    }
}

void Game::logPerformance(const char* format, ...) {
    if (!settings.general.diagnosticLogs) return;
    AITelemetry::PerformanceScope perfScope("telemetry.text_flush",gameCycleCount);
    std::lock_guard<std::mutex> lock(performanceLogMutex);
    
    if(!performanceLogFile.is_open()) {
        return;
    }
    
    char buffer[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    performanceLogFile << buffer << std::endl;
    performanceLogFile.flush();
}


void Game::applyDuneCityGraphicsSkins() {
    if(!pGFXManager) {
        return;
    }

    pGFXManager->resetDuneCityGraphicsSkins();
    if(!citySimEnabled_) {
        return;
    }

    if(gameInitSettings.getGameType() == GameType::Campaign) {
        const bool useDune2 = gameInitSettings.getCampaignGraphicsSkin()
            == GameInitSettings::GraphicsSkin::Dune2;
        for(int house = 0; house < static_cast<int>(NUM_HOUSES); ++house) {
            pGFXManager->setDuneCityHouseGraphicsSkin(house, useDune2);
        }
        return;
    }

    for(const GameInitSettings::HouseInfo& houseInfo : gameInitSettings.getHouseInfoList()) {
        pGFXManager->setDuneCityHouseGraphicsSkin(
            static_cast<int>(houseInfo.houseID),
            houseInfo.graphicsSkin == GameInitSettings::GraphicsSkin::Dune2);
    }
}

void Game::initGame(const GameInitSettings& newGameInitSettings) {
    gameInitSettings = newGameInitSettings;
    const bool loadedSave = WorkshopGameContent::isSave(gameInitSettings);
    if(!loadedSave) {
        if(!gameInitSettings.getModRevisionHash().empty()) WorkshopGameContent::resolveMod(gameInitSettings);
        else { WorkshopGameContent::resolveMod(gameInitSettings); WorkshopGameContent::pin(gameInitSettings); }
    }
    applyCustomPaletteRuntimeHouseRamps();

    // DuneCity 1.0.487: invalidate sprite texture cache to fix
    // the first-launch invisibility bug. Preserves uiGraphic.
    if(pGFXManager) {
        pGFXManager->invalidateAllSpriteTextures();
    }

    // The host's mod choice is the source of truth for whether the
    // city-sim feature flag is on — not whatever mod the local player
    // happens to have active in their main menu. This matters most for
    // multiplayer clients (their local ModManager may be on vanilla),
    // but also keeps single-player consistent if mod state drifted
    // between menu navigation and game start.
    {
        const std::string& sessionMod = gameInitSettings.getModName();
        if (!sessionMod.empty()) {
            ModInfo info = ModManager::instance().getModInfo(sessionMod);
            // If the named mod isn't installed locally we keep the
            // earlier value (set in the Game constructor) so single
            // player still works — but for any installed mod the
            // session's choice wins.
            if (!info.name.empty()) {
                citySimEnabled_ = info.enablesCityMode;
            }
        }
    }

    applyDuneCityGraphicsSkins();

    targetRequestQueue.clear();
    pendingTargetRequestIds.clear();
    pathRequestQueue.clear();
    pendingPathRequestIds.clear();
    // Suspended searches hold scratch buffers and a unit id; neither may outlive
    // the match they belong to. Dropping them is a cancellation, and counting it
    // keeps the request-lifecycle conservation check honest across a load.
    pathRequestsCancelled += activePathJobs.size();
    activePathJobs.clear();
    invalidateCarryallCandidateIds();

    // Initialize performance logging
    initPerformanceLog();

    // Initialize cursor manager
    cursorManager.initialize();

    switch(gameInitSettings.getGameType()) {
        case GameType::LoadSavegame: {
            if(loadSaveGame(gameInitSettings.getFilename()) == false) {
                THROW(std::runtime_error, "Loading save game failed!");
            }
        } break;

        case GameType::LoadCoop:
        case GameType::LoadMultiplayer: {
            IMemoryStream memStream(gameInitSettings.getFiledata().c_str(), gameInitSettings.getFiledata().size());

            if(loadSaveGame(memStream) == false) {
                THROW(std::runtime_error, "Loading save game failed!");
            }
        } break;

        case GameType::CampaignCoop:
        case GameType::SkirmishCoop:
        case GameType::Campaign:
        case GameType::Skirmish:
        case GameType::CustomGame:
        case GameType::CustomMultiplayer: {
            gameType = gameInitSettings.getGameType();
            randomGen.setSeed(gameInitSettings.getRandomSeed());

            objectData.loadFromINIFile(ModManager::instance().getActiveObjectDataPath(), false);

            // City zones must not be buildable when the active mod doesn't
            // opt into DuneCity city-sim features. Force-disable them in the
            // loaded ObjectData so every consumer (build lists, prerequisites,
            // AI, save/load) sees them as unavailable, regardless of what the
            // mod's ObjectData.ini says.
            if (!citySimEnabled_) {
                for (int h = 0; h < NUM_HOUSES; h++) {
                    for (int item = Structure_FirstID; item <= Structure_LastID; ++item)
                        if (DuneCity::isCityOnlyStructure(item)) objectData.data[item][h].enabled = false;
                }
            }

            // Apply the city reactor balance only to a city match. ObjectData
            // also loads non-city mods and must preserve their configured stats.
            if (citySimEnabled_) for (int h = 0; h < NUM_HOUSES; ++h)
                objectData.data[Structure_NuclearPlant][h].hitpoints = 750;

            objectData.logSettings();

            if(gameInitSettings.getMission() != 0) {
                techLevel = ((gameInitSettings.getMission() + 1)/3) + 1 ;
            }

            try {
                INIMapLoader(this, gameInitSettings.getFilename(), gameInitSettings.getFiledata());
            } catch(const std::exception& e) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "Game::init: scenario load failed (gameType=%d, mission=%d): %s",
                    static_cast<int>(gameInitSettings.getGameType()),
                    gameInitSettings.getMission(), e.what());
            }

            // Do not carry per-map city timing state into the next mission.
            if (citySimEnabled_ && currentGameMap != nullptr) {
                citySimulation_.reset();
            }

            if(bReplay == false && gameInitSettings.getGameType() != GameType::CustomGame && !isNetworkGameType(gameInitSettings.getGameType())) {
                /* do briefing */
                SDL_Log("Briefing...");
                BriefingMenu(gameInitSettings.getHouseID(), gameInitSettings.getMission(),BRIEFING).showMenu();
            }
        } break;

        default: {
        } break;
    }
    if(isSpectating() && !spectatorViewPlayer) setupSpectatorView();
    AITelemetry::startGame(AITelemetry::Record().set("version", VERSION)
        .set("mod", ModManager::instance().getActiveModName())
        .set("source", newGameInitSettings.getFilename()).set("seed", gameInitSettings.getRandomSeed())
        .set("entry_type", static_cast<int>(newGameInitSettings.getGameType()))
        .set("game_type", static_cast<int>(gameType)).set("tech", techLevel)
        .set("city_sim", isCitySimEnabled()).set("cycles_per_30_seconds", MILLI2CYCLES(30000))
        .set("deviation_reward_version", DeviationReward::kLedgerVersion)
        .set("start_cycle", gameCycleCount)
        .set("options", AITelemetry::Record()
            .set("concrete_required", gameInitSettings.getGameOptions().concreteRequired)
            .set("fog_of_war", gameInitSettings.getGameOptions().fogOfWar)
            .set("immortal_human_player", gameInitSettings.getGameOptions().immortalHumanPlayer)
            .set("harvester_limit_override", gameInitSettings.getGameOptions().maximumNumberOfHarvestersOverride))
        .set("map_width", currentGameMap ? currentGameMap->getSizeX() : 0)
        .set("map_height", currentGameMap ? currentGameMap->getSizeY() : 0), settings.general.diagnosticLogs);
    startMatchAnalytics();
    collectCustomMap(loadedSave);
}

/**
    A genuinely started offline custom match offers its scenario to the community
    catalogue. This runs only once the scenario itself has loaded, so a map that
    never produced a playable game is never collected. pin() already recorded the
    original map bytes and the selected mod locally; collecting only writes that
    revision to the Workshop outbox, which the menu and game loops drain later, so
    an offline player never waits for, and never fails because of, the metaserver.
*/
void Game::collectCustomMap(bool loadedSave) {
    if(currentGameMap == nullptr) return;
    try {
        if(MapCollection::collect(Workshop::store(), gameInitSettings.getGameType(), bReplay, loadedSave,
                                  gameInitSettings.getMapRevisionHash(), Workshop::queueCollection))
            SDL_Log("Metaserver: this custom map is queued for the community catalogue.");
    } catch(const std::exception& error) {
        SDL_Log("Metaserver map collection deferred: %s", error.what());
    }
}

void Game::startMatchAnalytics() {
    if (bReplay || gameInitSettings.getGameType() == GameType::LoadSavegame
        || gameInitSettings.getGameType() == GameType::LoadMultiplayer) {
        return;
    }
    // A multiplayer game is reported by its host only. The reporter is kept
    // separate from lobby announcement state, which is deliberately stopped
    // once the countdown ends.
    if (isNetworkGameType(gameInitSettings.getGameType())
        && (!pNetworkManager || !pNetworkManager->isServer())) {
        return;
    }
    matchAnalyticsID = analyticsMatchID(gameInitSettings);
#ifdef __EMSCRIPTEN__
    // Browser builds have no SDL worker threads. Submit asynchronously through
    // the same-origin web shell instead of creating a native MetaServerClient.
    WebRuntime::reportMatchStats("start", matchAnalyticsID, buildMatchAnalyticsPayload(false));
    matchAnalyticsStarted = true;
#else
    try {
        matchAnalyticsClient = std::make_unique<MetaServerClient>(settings.network.metaServer);
        matchAnalyticsClient->reportGameStats("start", matchAnalyticsID, buildMatchAnalyticsPayload(false));
        matchAnalyticsStarted = true;
    } catch (const std::exception& exception) {
        SDL_Log("Game: Match analytics reporter unavailable: %s", exception.what());
        matchAnalyticsClient.reset();
    }
#endif
}

void Game::finishMatchAnalytics() {
#ifdef __EMSCRIPTEN__
    if (!matchAnalyticsStarted) return;
    WebRuntime::reportMatchStats("end", matchAnalyticsID, buildMatchAnalyticsPayload(true));
    matchAnalyticsStarted = false;
#else
    if (!matchAnalyticsStarted || !matchAnalyticsClient) return;
    matchAnalyticsClient->reportGameStats("end", matchAnalyticsID, buildMatchAnalyticsPayload(true));
    // The client's worker drains the queued end event before joining. A failed
    // request is caught inside MetaServerClient and can never affect teardown.
    matchAnalyticsClient.reset();
    matchAnalyticsStarted = false;
#endif
}

std::string Game::buildMatchAnalyticsPayload(bool finishedMatch) const {
    int totalSpice = 0;
    int totalUnitsDestroyed = 0;
    int totalStructuresDestroyed = 0;
    int winnerHouse = -1;
    int winningTeam = -1;
    // `won` only describes the local player. Derive the winner from the game
    // state so a host that lost still records the actual winning team.
    if (finishedMatch) {
        std::set<int> survivingTeams;
        for (const auto& currentHouse : house) {
            if (currentHouse && currentHouse->getTeamID() != 0 && currentHouse->isAlive()) {
                survivingTeams.insert(currentHouse->getTeamID());
            }
        }
        if (survivingTeams.size() == 1) winningTeam = *survivingTeams.begin();
        else if (pLocalHouse && won) winningTeam = pLocalHouse->getTeamID();
    }
    std::vector<std::string> playerRows;
    int playerSlot = 0;

    for (int h = 0; h < NUM_HOUSES; ++h) {
        const House* currentHouse = house[h].get();
        if (!currentHouse) continue;
        const auto& players = currentHouse->getPlayerList();
        if (players.empty()) continue;
        totalSpice += currentHouse->getHarvestedSpice().lround();
        totalUnitsDestroyed += currentHouse->getNumDestroyedUnits();
        totalStructuresDestroyed += currentHouse->getNumDestroyedStructures();
        if (finishedMatch && winningTeam >= 0 && currentHouse->getTeamID() == winningTeam && winnerHouse < 0)
            winnerHouse = currentHouse->getHouseID();

        int unitsLost = 0;
        int structuresLost = 0;
        for (int item = ItemID_FirstID; item <= ItemID_LastID; ++item) {
            if (isUnit(item)) unitsLost += currentHouse->getNumLostItems(item);
            else if (isStructure(item)) structuresLost += currentHouse->getNumLostItems(item);
        }
        std::vector<std::string> itemRows;
        if (finishedMatch) {
            for (int item = ItemID_FirstID; item <= ItemID_LastID; ++item) {
                if (!isUnit(item) && !isStructure(item)) continue;
                const int produced = currentHouse->getNumBuiltItems(item);
                const int killed = currentHouse->getNumKilledItems(item);
                const int lost = currentHouse->getNumLostItems(item);
                if (produced == 0 && killed == 0 && lost == 0) continue;
                std::ostringstream itemRow;
                // Compact positional rows keep a full item breakdown bounded:
                // [item id, item name, kind, produced, killed, lost].
                itemRow << '[' << item << ',' << AITelemetry::Record::quote(getItemNameByID(item))
                        << ",\"" << (isUnit(item) ? "unit" : "structure") << "\"," << produced << ','
                        << killed << ',' << lost << ']';
                itemRows.push_back(itemRow.str());
            }
        }

        for (const auto& player : players) {
            const auto* qbot = dynamic_cast<const QuantBot*>(player.get());
            const std::string playerClass = player->getPlayerclass();
            const char* controller = qbot ? "qbot"
                : playerClass == HUMANPLAYERCLASS ? "human"
                : playerClass == "SpectatorPlayer" ? "spectator" : "ai";
            const std::string aiType = analyticsAIType(playerClass);
            const std::string aiDifficulty = qbot ? qbot->getDifficultyName() : analyticsAIDifficulty(playerClass);
            std::ostringstream row;
            row << "{\"slot\":" << playerSlot++
                << ",\"player_id\":" << static_cast<int>(player->getPlayerID())
                << ",\"player_name\":" << AITelemetry::Record::quote(player->getPlayername())
                << ",\"house_slot\":" << h
                << ",\"house_id\":" << currentHouse->getHouseID()
                << ",\"house_name\":" << AITelemetry::Record::quote(getHouseNameByNumber(static_cast<HOUSETYPE>(h)))
                << ",\"team\":" << currentHouse->getTeamID()
                << ",\"controller\":\"" << controller << '"'
                << ",\"player_class\":" << AITelemetry::Record::quote(playerClass)
                << ",\"shared_house_players\":" << players.size();
            if (std::string(controller) == "ai" || qbot) {
                row << ",\"ai_type\":" << AITelemetry::Record::quote(aiType)
                    << ",\"ai_support\":" << (playerClass.find("Support") != std::string::npos ? "true" : "false");
                if (!aiDifficulty.empty())
                    row << ",\"ai_difficulty\":" << AITelemetry::Record::quote(aiDifficulty);
            }
            if (qbot) row << ",\"qbot_difficulty\":" << AITelemetry::Record::quote(aiDifficulty);

            if (finishedMatch) {
                row << ",\"result\":\"" << (winningTeam >= 0 && currentHouse->getTeamID() == winningTeam ? "winner" : currentHouse->isAlive() ? "alive" : "defeated") << '"'
                    << ",\"final_credits\":" << currentHouse->getCredits()
                    << ",\"spice_harvested\":" << currentHouse->getHarvestedSpice().lround()
                    << ",\"units_built\":" << currentHouse->getNumBuiltUnits()
                    << ",\"structures_built\":" << currentHouse->getNumBuiltStructures()
                    << ",\"units_destroyed\":" << currentHouse->getNumDestroyedUnits()
                    << ",\"structures_destroyed\":" << currentHouse->getNumDestroyedStructures()
                    << ",\"units_lost\":" << unitsLost
                    << ",\"structures_lost\":" << structuresLost
                    << ",\"military_value\":" << currentHouse->getMilitaryValue()
                    << ",\"item_stats\":" << analyticsArray(itemRows);
                if (citySimulation_ && citySimulation_->isInitialized()) {
                    const auto& city = citySimulation_->getHouseState(currentHouse->getHouseID());
                    row << ",\"city_population\":" << city.getTotalPop() * DuneCity::CitySimulation::kPopDisplayMultiplier
                        << ",\"city_value\":" << city.avgLandValue
                        << ",\"city_residential_population\":" << city.resPop * DuneCity::CitySimulation::kPopDisplayMultiplier
                        << ",\"city_commercial_population\":" << city.comPop * DuneCity::CitySimulation::kPopDisplayMultiplier
                        << ",\"city_industrial_population\":" << city.indPop * DuneCity::CitySimulation::kPopDisplayMultiplier
                        << ",\"city_unemployment_percent\":" << city.unemploymentRate
                        << ",\"city_residential_demand\":" << city.resValve
                        << ",\"city_commercial_demand\":" << city.comValve
                        << ",\"city_industrial_demand\":" << city.indValve;
                }
            }
            if (qbot && finishedMatch) {
                std::vector<std::string> unitRows;
                const auto& mix = qbot->getLastUnitMixBps();
                for (size_t i = 0; i < analyticsMixItems.size(); ++i) {
                    const int item = analyticsMixItems[i];
                    int built = 0;
                    int lost = 0;
                    int destroyed = 0;
                    int64_t rewardMilli = 0;
                    int64_t damageValueMilli = 0;
                    int64_t killBonusMilli = 0;
                    int64_t lostValue = 0;
                    const std::array<Uint32, 3> specialItems = {Unit_Devastator, Unit_SonicTank, Unit_Deviator};
                    const bool specialGroup = item == 9999;
                    const auto collect = [&](Uint32 unit) {
                        built += currentHouse->getNumBuiltItems(unit);
                        lost += currentHouse->getNumLostItems(unit);
                        destroyed += currentHouse->getNumKilledItems(unit);
                        const auto& reward = currentHouse->getCombatReward(unit);
                        rewardMilli += reward.total();
                        damageValueMilli += reward.damageMilli;
                        killBonusMilli += reward.killBonusMilli;
                        lostValue += int64_t(currentHouse->getNumLostItems(unit)) * objectData.data[unit][h].price;
                    };
                    if (specialGroup) {
                        for (const auto unit : specialItems) collect(unit);
                    } else {
                        collect(item);
                    }
                    std::ostringstream unit;
                    unit << "{\"item_id\":" << item
                         << ",\"item_name\":" << AITelemetry::Record::quote(
                                specialGroup ? "special" : getItemNameByID(item))
                         << ",\"target_weight_bps\":" << mix[i]
                         << ",\"built\":" << built
                         << ",\"lost\":" << lost
                         << ",\"destroyed\":" << destroyed
                         << ",\"reward_milli\":" << rewardMilli
                         << ",\"damage_value_milli\":" << damageValueMilli
                         << ",\"kill_bonus_milli\":" << killBonusMilli
                         << ",\"lost_value\":" << lostValue << '}';
                    unitRows.push_back(unit.str());
                }
                row << ",\"qbot_units\":" << analyticsArray(unitRows);
            }
            row << '}';
            playerRows.push_back(row.str());
        }
    }

    const std::string mapName = getBasename(gameInitSettings.getFilename(), true);
    std::ostringstream payload;
    payload << "{\"schema_version\":3"
#ifdef __EMSCRIPTEN__
            << ",\"client_runtime\":\"browser\""
#else
            << ",\"client_runtime\":\"native\""
#endif
            << ",\"game_type\":" << AITelemetry::Record::quote(analyticsGameType(gameInitSettings.getGameType()))
            << ",\"game_version\":" << AITelemetry::Record::quote(VERSION)
            << ",\"map\":{\"name\":" << AITelemetry::Record::quote(mapName)
            << ",\"width\":" << (currentGameMap ? currentGameMap->getSizeX() : 0)
            << ",\"height\":" << (currentGameMap ? currentGameMap->getSizeY() : 0)
            << ",\"seed\":" << gameInitSettings.getRandomSeed() << '}'
            << ",\"mod\":{\"name\":" << AITelemetry::Record::quote(gameInitSettings.getModName()) << '}'
            << ",\"players\":" << analyticsArray(playerRows);
    if (finishedMatch) {
        payload << ",\"outcome\":" << AITelemetry::Record::quote(finished ? "finished" : "abandoned")
                << ",\"duration_cycles\":" << gameCycleCount
                << ",\"duration_seconds\":" << getGameTime() / 1000
                << ",\"winning_house\":" << winnerHouse
                << ",\"total_spice_harvested\":" << totalSpice
                << ",\"total_units_destroyed\":" << totalUnitsDestroyed
                << ",\"total_structures_destroyed\":" << totalStructuresDestroyed;
    }
    payload << '}';
    return payload.str();
}

void Game::initReplay(const std::string& filename) {
    bReplay = true;

    IFileStream fs;

    if(fs.open(filename) == false) {
        THROW(io_error, "Error while opening '%s'!", filename);
    }

    // override local player name as it was when the replay was created
    localPlayerName = fs.readString();

    // read GameInitInfo
    GameInitSettings loadedGameInitSettings(fs);

    // load all commands
    cmdManager.load(fs);

    initGame(loadedGameInitSettings);
}


void Game::processObjects()
{
    processTargetRequests();
    
    // Time pathfinding
    Uint64 pathStart = SDL_GetPerformanceCounter();
    processPathRequests();
    Uint64 pathEnd = SDL_GetPerformanceCounter();
    const double pathMs = getElapsedMs(pathStart, pathEnd);
    frameTiming.pathfindingMs += pathMs;
    frameTiming.pathfindingMsThisFrame += pathMs;

    {
        AITelemetry::PerformanceScope perfScope("objects.tiles",gameCycleCount);
        // Only tiles holding a dead-unit timer have anything to do; the rest of
        // Tile::update() was an early-out. Same per-tile work, same results.
        currentGameMap->updateActiveDeadUnitTiles();
    }

    // Time structure updates
    Uint64 structStart = SDL_GetPerformanceCounter();
    for(StructureBase* pStructure : structureList) {
        pStructure->update();
    }
    Uint64 structEnd = SDL_GetPerformanceCounter();
    const double structMs = getElapsedMs(structStart, structEnd);
    frameTiming.structuresMs += structMs;
    frameTiming.structuresMsThisFrame += structMs;

    if ((currentCursorMode == CursorMode_Placing) && selectedList.empty()) {
        setCursorMode(CursorMode_Normal);
    }

    // Time unit updates
    Uint64 unitStart = SDL_GetPerformanceCounter();
    frameTiming.unitCount = unitList.size();
    for(UnitBase* pUnit : unitList) {
        pUnit->update();
    }
    Uint64 unitEnd = SDL_GetPerformanceCounter();
    const double unitMs = getElapsedMs(unitStart, unitEnd);
    frameTiming.unitsMs += unitMs;
    frameTiming.unitsMsThisFrame += unitMs;

    for(Bullet* pBullet : bulletList) {
        pBullet->update();
    }

    for(Explosion* pExplosion : explosionList) {
        pExplosion->update();
    }
}

void Game::processTargetRequests() {
    AITelemetry::PerformanceScope perfScope("objects.target_queue",gameCycleCount);
    if(targetRequestQueue.empty()) {
        return;
    }

    // MULTIPLAYER FIX (Issue #2): Removed time-based budget check
    // Now uses ONLY deterministic per-cycle request limit
    // Timing is still measured for profiling, but does NOT affect execution

    const Uint64 start = SDL_GetPerformanceCounter();  // PROFILING ONLY

    // Deterministic per-cycle limit (same on all clients regardless of performance)
    // Target acquisition is fast (~0.05ms each), so we can process many per cycle
    static constexpr int TargetRequestsPerCycle = 50;  // ~2.5ms budget at 0.05ms each
    
    int processedCount = 0;
    while(!targetRequestQueue.empty() && processedCount < TargetRequestsPerCycle) {
        TargetRequest request = targetRequestQueue.front();
        targetRequestQueue.pop_front();
        pendingTargetRequestIds.erase(request.objectId);

        auto* unit = dynamic_cast<UnitBase*>(objectManager.getObject(request.objectId));
        if(unit != nullptr) {
            unit->resolvePendingTargetRequest();
        }

        processedCount++;
    }
    
    const Uint64 end = SDL_GetPerformanceCounter();  // PROFILING ONLY
    const double elapsedMs = getElapsedMs(start, end);  // PROFILING ONLY
    
    // MULTIPLAYER TELEMETRY: Log synchronization info and queue starvation warnings
    if(pNetworkManager != nullptr && processedCount > 0) {
        // Log every 10 seconds to verify synchronization
        if((gameCycleCount % MILLI2CYCLES(10000)) == 0) {
            SDL_Log("[MP-Sync Cycle %d] Targets: %d, Queue: %zu, Time: %.2fms",
                    gameCycleCount,
                    processedCount,
                    targetRequestQueue.size(),
                    elapsedMs);
        }
    }
    
    // Queue starvation warning (deterministic across clients)
    if(!targetRequestQueue.empty() && processedCount >= TargetRequestsPerCycle) {
        // Log every 5 seconds if queue is growing
        if((gameCycleCount % MILLI2CYCLES(5000)) == 0 && targetRequestQueue.size() > 50) {
            SDL_Log("[MP-Sync WARNING Cycle %d] Target queue may be starved: %zu requests pending",
                    gameCycleCount, targetRequestQueue.size());
        }
    }
}

void Game::recordPathTokens(size_t totalTokens) {
    const size_t lastIndex = frameTiming.pathTokenHistogram.size() - 1;
    for(size_t i = 0; i < PathTokenBucketBounds.size(); ++i) {
        if(totalTokens < PathTokenBucketBounds[i]) {
            frameTiming.pathTokenHistogram[i]++;
            return;
        }
    }
    frameTiming.pathTokenHistogram[lastIndex]++;
}

void Game::recordCompletedPathTokens(size_t totalTokens) {
    frameTiming.tokensPerCompletedPathStats.add(static_cast<double>(totalTokens));
    if(totalTokens < frameTiming.minTokensPerCompletedPath) {
        frameTiming.minTokensPerCompletedPath = totalTokens;
    }
    if(totalTokens > frameTiming.maxTokensPerCompletedPath) {
        frameTiming.maxTokensPerCompletedPath = totalTokens;
    }
}

void Game::recordFailedPathTokens(size_t totalTokens) {
    frameTiming.tokensPerFailedPathStats.add(static_cast<double>(totalTokens));
    if(totalTokens < frameTiming.minTokensPerFailedPath) {
        frameTiming.minTokensPerFailedPath = totalTokens;
    }
    if(totalTokens > frameTiming.maxTokensPerFailedPath) {
        frameTiming.maxTokensPerFailedPath = totalTokens;
    }
}

void Game::logPathInstrumentationIfNeeded() {
    constexpr Uint32 kInstrumentationInterval = MILLI2CYCLES(30 * 1000);
    if(gameCycleCount - lastPathInstrumentationLogCycle < kInstrumentationInterval) {
        return;
    }

    lastPathInstrumentationLogCycle = gameCycleCount;

    size_t hits = frameTiming.pathReuseHitsWindow;
    size_t misses = frameTiming.pathReuseMissesWindow;
    size_t totalChecks = hits + misses;

    frameTiming.totalPathReuseHits += hits;
    frameTiming.totalPathReuseMisses += misses;

    frameTiming.pathReuseHitsWindow = 0;
    frameTiming.pathReuseMissesWindow = 0;

    const auto poolStats = AStarSearch::getPoolUsageStats();
    const double reuseRate = (totalChecks > 0)
        ? (100.0 * static_cast<double>(hits) / static_cast<double>(totalChecks))
        : 0.0;

    const int mapSize = (currentGameMap != nullptr)
        ? currentGameMap->getSizeX()
        : 0;

    logPerformance("[PathInstrumentation] Cycle %u | Map: %dx%d | Reuse: %zu/%zu (%.1f%%) | Pool hits=%zu grow=%zu fallback=%zu | Pool usage=%zu/%zu",
                   gameCycleCount, mapSize, mapSize, hits, totalChecks, reuseRate,
                   poolStats.reuseHits, poolStats.bufferExpansions, poolStats.fallbackAllocs,
                   poolStats.buffersInUse, poolStats.totalBuffers);
    
    // Log path invalidation reasons
    if(misses > 0) {
        logPerformance("[PathInvalidation] DestChanged=%zu HuntTooFar=%zu Blocked=%zu",
                       frameTiming.pathInvalidDestChanged,
                       frameTiming.pathInvalidHuntTooFar,
                       frameTiming.pathInvalidBlocked);
        
        // Reset counters
        frameTiming.pathInvalidDestChanged = 0;
        frameTiming.pathInvalidHuntTooFar = 0;
        frameTiming.pathInvalidBlocked = 0;
    }
    
    // Log movement pause reasons (for debugging stuttering)
    uint64_t totalPauses = frameTiming.pauseWaitingForPath + frameTiming.pauseWaitingForBlocker +
                           frameTiming.pauseRecalcCooldown + frameTiming.pauseTurningToFace;
    if(totalPauses > 0) {
        logPerformance("[MovementPauses] Total=%llu | WaitPath=%llu WaitBlocker=%llu RecalcCD=%llu Turning=%llu",
                       totalPauses,
                       frameTiming.pauseWaitingForPath,
                       frameTiming.pauseWaitingForBlocker,
                       frameTiming.pauseRecalcCooldown,
                       frameTiming.pauseTurningToFace);
        
        // Reset counters
        frameTiming.pauseWaitingForPath = 0;
        frameTiming.pauseWaitingForBlocker = 0;
        frameTiming.pauseRecalcCooldown = 0;
        frameTiming.pauseTurningToFace = 0;
    }
}

void Game::finishPathJob(PathJob& job, UnitBase* unit, PathJobOutcome outcome) {
    // One place closes a request, so the invariant "a pending id is either queued
    // or active, never both and never neither" cannot drift.
    job.search.reset();
    if(outcome == PathJobOutcome::Applied) {
        ++pathResultsApplied;
    }
    if(outcome == PathJobOutcome::Requeued) {
        ++pathRequestsRequeued;
        // The id stays pending and the unit's flag stays set: this is the same
        // request continuing, now at the back of the queue. Going through the
        // queue rather than restarting in place is what bounds the work a unit
        // with changing orders can take from everyone else.
        pathRequestQueue.push_back({job.objectId});
        return;
    }
    if(outcome == PathJobOutcome::Cancelled) ++pathRequestsCancelled;
    pendingPathRequestIds.erase(job.objectId);
    if(unit != nullptr) {
        unit->markPathRequestFinished();
    }
}

void Game::collectPathBatch(size_t& startsThisCycle) {
    // Suspended jobs keep their slots and their order, so a long search cannot be
    // starved by a stream of new requests, and new requests enter in queue order.
    while(activePathJobs.size() < kPathBatchSize
          && startsThisCycle < kMaxPathStartsPerCycle
          && !pathRequestQueue.empty()) {
        const PathRequest request = pathRequestQueue.front();
        pathRequestQueue.pop_front();
        ++startsThisCycle;

        // The id stays in pendingPathRequestIds for as long as the request is
        // active. queuePathRequest() keys off that set and UnitBase::update() keys
        // off pathRequestQueued, so neither can add a second request for a unit
        // that already has one in flight.
        auto* unit = dynamic_cast<UnitBase*>(objectManager.getObject(request.objectId));
        if(unit == nullptr) {
            pendingPathRequestIds.erase(request.objectId);
            continue;
        }

        // Main-thread precheck: destination resolution, the HUNT reachability rule
        // and the lazy terrain-connectivity cache it consults all stay here.
        UnitBase::PathRequestStats stats;
        Coord destinationCoord;
        if(!unit->beginPathRequest(stats, destinationCoord)) {
            auto& perf = AITelemetry::log();
            const int owner = unit->getOwner() ? unit->getOwner()->getHouseID() : -1;
            perf.performance(gameCycleCount,owner,
                stats.invalidDestination ? "path.invalid_nodes" : "path.failed_nodes",0,unit->getItemID(),false);
            frameTiming.pathsProcessedThisCycle++;
            frameTiming.totalPathsProcessedThisFrame++;
            frameTiming.totalPathsProcessed++;
            pendingPathRequestIds.erase(request.objectId);
            unit->markPathRequestFinished();
            continue;  // No nodes charged, and the request is finished.
        }

        PathJob job;
        job.objectId = request.objectId;
        job.destinationCoord = destinationCoord;
        job.inputs = unit->capturePathInputs();
        job.search.reset(new AStarSearch(currentGameMap, unit, unit->getLocation(),
                                         destinationCoord, AStarSearch::Resumable{}));
        // Asserted rather than assumed: for as long as this job is active the unit
        // holds its pending flag, so neither update() nor queuePathRequest() can
        // add a second request for it. Anything that cleared the flag earlier in
        // the unit's own update cannot reopen that window.
        unit->markPathRequestActive();
        ++pathRequestsStarted;
        activePathJobs.push_back(std::move(job));
    }
}

void Game::processPathRequests() {
    frameTiming.pathsProcessedThisCycle = 0;
    frameTiming.pathfindingMsThisCycle = 0.0;
    frameTiming.pathTokensThisCycle = 0;
    if(pathRequestQueue.empty() && activePathJobs.empty()) {
        frameTiming.pathsPerCycleStats.add(0.0);
        frameTiming.pathTokensPerCycleStats.add(0.0);
        logPathInstrumentationIfNeeded();
        return;
    }

    // MULTIPLAYER FIX (Issue #1): Removed time-based budget checks
    // Now uses ONLY deterministic token budget for multiplayer synchronization
    // Timing is still measured for profiling, but does NOT affect execution

    const Uint64 start = SDL_GetPerformanceCounter();  // PROFILING ONLY

    // Track queue depth
    const int queueDepth = static_cast<int>(pathRequestQueue.size());
    if(queueDepth > frameTiming.maxPathQueueLength) {
        frameTiming.maxPathQueueLength = queueDepth;
    }

    // PHASE 1: NEGOTIATED TOKEN BUDGET WITH CARRY-OVER (single-player only)
    // Start at 15k tokens/cycle, adapt between 5k-25k based on FPS
    // NEVER scale UP aggressively (that caused the freeze!)
    // Carry-over is DISABLED in multiplayer to prevent desync
    
    // Calculate budget with carry-over (capped at kHardCap)
    // Note: carryOverTokens is always 0 in multiplayer
    size_t budget = std::min<size_t>(
        negotiatedBudget + carryOverTokens,
        kHardCap
    );
    carryOverTokens = 0;  // Reset for this cycle
    
    size_t tokensRemaining = budget;
    size_t tokensUsedThisCycle = 0;
    
    // STRICT NODE BUDGET (recommendation 4)
    // A whole search can close far more nodes than one cycle's budget: the
    // MAX_NODES_CHECKED threshold only stops neighbour expansion, after which the
    // frontier still has to be drained. So searches are sliced instead of run to
    // completion, and no slice is ever allowed to exceed what is left.
    //
    // The schedule is simulation state, not a scheduling convenience: a fixed
    // batch is collected in queue order, each member is given its quota before
    // any work starts, and results are applied in that same order. A worker count
    // can therefore only change wall time, never which nodes get spent or in what
    // order paths are handed back.
    size_t startsThisCycle = 0;
    for(size_t round = 0; round < kMaxPathRoundsPerCycle && tokensRemaining > 0; ++round) {
        collectPathBatch(startsThisCycle);
        if(activePathJobs.empty()) {
            break;
        }

        // Deterministic quotas: an even split of what is left, with the remainder
        // going to the earliest jobs. The sum is exactly tokensRemaining, so the
        // batch cannot overshoot even if every job uses all of its quota.
        const size_t batch = activePathJobs.size();
        std::vector<size_t> quotas(batch, tokensRemaining / batch);
        for(size_t i = 0; i < tokensRemaining % batch; ++i) {
            quotas[i] += 1;
        }

        std::vector<AStarSearch::Status> status(batch, AStarSearch::Status::Completed);
        std::vector<size_t> closed(batch, 0);
        std::vector<Uint64> elapsed(batch, 0);
        std::vector<UnitBase*> units(batch, nullptr);

        // PASS 1 (main thread, every slot): resolve and validate inputs.
        //
        // This has to happen for all slots before any step runs, because it is the
        // part that is not read-only: resolvePathDestination() reaches through
        // ObjectPointer::getObjPointer(), which clears its own cached id when the
        // target has gone, and the lazy terrain-connectivity cache behind the HUNT
        // rule is built here too. After this pass a slot either holds a search that
        // can be advanced against a read-only world, or is marked for collection.
        for(size_t i = 0; i < batch; ++i) {
            PathJob& job = activePathJobs[i];
            auto* unit = dynamic_cast<UnitBase*>(objectManager.getObject(job.objectId));
            units[i] = unit;
            if(unit == nullptr || job.search == nullptr) {
                continue;  // Collected below as a cancellation.
            }

            // Cross-tick input validation.
            //
            // A global geometry bump or an unrelated unit moving does NOT discard
            // the frontier. That counter moves whenever anyone anywhere builds or
            // loses blocking geometry -- measured at roughly every 2.4 cycles on a
            // busy city map -- and restarting on it meant no cross-tick search
            // ever finished: 170 restarts and 450k wasted nodes per slot, zero
            // late completions, ~950 requests queued. Costs in a retained frontier
            // may reflect slightly older observations of the map, which is the
            // normal approximation for a plan that spans world updates; the route
            // is probed before the unit commits to it and revalidated per step
            // while it walks, and cached passability is refreshed per slice.
            //
            // What does invalidate the plan is the request itself changing. Then
            // the answer being computed is to a question nobody asked any more, so
            // it is discarded and the unit's current request goes to the BACK of
            // the queue. Requeueing rather than restarting in place means a unit
            // whose orders keep changing cannot hold a slot or spend the budget on
            // abandoned frontiers, and a stale order can never be applied.
            const UnitBase::PathInputFingerprint current = unit->capturePathInputs();
            const Uint32 difference = current.differenceMask(job.inputs);
            job.restartReasons |= difference;
            if((difference & UnitBase::PathInputFingerprint::RestartForcingMask) != 0) {
                job.search.reset();
                job.requeue = true;
                continue;  // Collected below; nothing more is charged to it.
            }

        }

        // PASS 2 (dispatchable): advance the owned frontiers. Each task touches
        // only its own search's scratch and reads the world; quotas and order were
        // fixed above, so a worker count or a completion order cannot change any
        // outcome. The whole batch is joined before anything mutates the world.
        std::vector<std::function<void()>> slices;
        slices.reserve(batch);
        std::vector<size_t> sliceSlots;
        sliceSlots.reserve(batch);
        for(size_t i = 0; i < batch; ++i) {
            if(units[i] == nullptr || activePathJobs[i].search == nullptr) continue;
            sliceSlots.push_back(i);
        }
        for(const size_t slot : sliceSlots) {
            slices.push_back([this, slot, &status, &closed, &elapsed, &units, &quotas] {
                PathJob& job = activePathJobs[slot];
                const Uint64 sliceStart = SDL_GetPerformanceCounter();
                status[slot] = job.search->step(currentGameMap, units[slot],
                                                quotas[slot], quotas[slot] + kPathIterationSlack);
                // Measured only for telemetry; never read by a decision.
                elapsed[slot] = SDL_GetPerformanceCounter() - sliceStart;
                closed[slot] = job.search->getNodesClosedLastStep();
            });
        }
        PathWorkerPool::instance().runBatch(slices);
        for(const size_t slot : sliceSlots) {
            activePathJobs[slot].chargedNodes += closed[slot];
        }

        // Collect in the same fixed order: charge every node actually spent, then
        // apply finished searches. Unused quota is only redistributed by the next
        // round, after the whole batch has been collected.
        auto& perf = AITelemetry::log();
        size_t usedThisRound = 0;
        for(size_t i = 0; i < batch; ++i) {
            PathJob& job = activePathJobs[i];
            UnitBase* unit = units[i];
            usedThisRound += closed[i];

            frameTiming.pathTokensThisCycle += closed[i];
            frameTiming.pathTokensThisFrame += closed[i];
            frameTiming.totalPathTokens += closed[i];
            tokensUsedThisCycle += closed[i];

            if(unit == nullptr || job.search == nullptr) {
                finishPathJob(job, unit, job.requeue ? PathJobOutcome::Requeued : PathJobOutcome::Cancelled);
                continue;
            }
            if(status[i] == AStarSearch::Status::Running) {
                continue;  // Keeps its slot and its place in the order.
            }

            UnitBase::PathRequestStats stats;
            stats.nodesExpanded = job.chargedNodes;
            unit->applyPathResult(job.search->getFoundPath(), job.destinationCoord,
                                  job.inputs.pathingRevision, stats);
            job.search.reset();
            finishPathJob(job, unit, PathJobOutcome::Applied);

            const int owner = unit->getOwner() ? unit->getOwner()->getHouseID() : -1;
            perf.performance(gameCycleCount,owner,"path.search",static_cast<int64_t>(getElapsedMs(0,elapsed[i])*1000),unit->getItemID());
            perf.performance(gameCycleCount,owner,stats.invalidDestination ? "path.invalid_nodes" : stats.pathFound ? "path.found_nodes" : "path.failed_nodes",stats.nodesExpanded,unit->getItemID(),false);
            if(job.restarts > 0) {
                perf.performance(gameCycleCount,owner,"path.restarts",job.restarts,unit->getItemID(),false);
            }

            frameTiming.pathsProcessedThisCycle++;
            frameTiming.totalPathsProcessedThisFrame++;
            frameTiming.totalPathsProcessed++;
            recordPathTokens(stats.nodesExpanded);
            if(!stats.invalidDestination) {
                if(stats.pathFound) {
                    recordCompletedPathTokens(stats.nodesExpanded);
                } else {
                    frameTiming.pathsFailedThisFrame++;
                    frameTiming.totalPathsFailed++;
                    recordFailedPathTokens(stats.nodesExpanded);
                }
            }
        }

        if(frameTiming.pathTokensThisCycle > frameTiming.maxPathTokensPerCycle) {
            frameTiming.maxPathTokensPerCycle = frameTiming.pathTokensThisCycle;
        }

        // Drop finished and cancelled jobs, preserving the order of the rest.
        activePathJobs.erase(std::remove_if(activePathJobs.begin(), activePathJobs.end(),
            [](const PathJob& job) { return job.search == nullptr; }), activePathJobs.end());

        tokensRemaining -= std::min(usedThisRound, tokensRemaining);
    }

    auto& perf = AITelemetry::log();
    perf.performance(gameCycleCount,-1,"path.nodes",tokensUsedThisCycle,-1,false);
    perf.performance(gameCycleCount,-1,"path.effective_budget",budget,-1,false);
    perf.performance(gameCycleCount,-1,"path.budget_overshoot",tokensUsedThisCycle > budget ? tokensUsedThisCycle-budget : 0,-1,false);
    perf.performance(gameCycleCount,-1,"path.queue",pathRequestQueue.size(),-1,false);
    perf.performance(gameCycleCount,-1,"path.suspended",activePathJobs.size(),-1,false);

    // PHASE 1.2: BANK UNUSED TOKENS FOR NEXT CYCLE
    // MULTIPLAYER FIX: Disable carry-over in multiplayer to prevent desync
    // Carry-over can accumulate drift if pathfinding is even slightly non-deterministic
    if (tokensUsedThisCycle < budget && pNetworkManager == nullptr) {
        // Bank unused tokens (capped at kDebtCap) - SINGLE-PLAYER ONLY
        carryOverTokens = std::min<size_t>(
            kDebtCap,
            budget - tokensUsedThisCycle
        );
    } else if(pNetworkManager != nullptr) {
        // Multiplayer: Always discard unused tokens to maintain sync
        carryOverTokens = 0;
    }
    
    // DESYNC DETECTION: Log token usage on specific cycles for debugging
    if(pNetworkManager != nullptr && (gameCycleCount % 375 == 0)) {
        SDL_Log("[DESYNC DEBUG] Cycle %d: budget=%zu, tokensUsed=%zu, carryOver=%zu (always 0 in MP), queue=%zu",
                gameCycleCount, negotiatedBudget, tokensUsedThisCycle, carryOverTokens, pathRequestQueue.size());
    }
    
    // Track token budget exhaustion
    if (tokensRemaining == 0 && !pathRequestQueue.empty()) {
        frameTiming.tokenBudgetExhaustedCount++;
    }
    
    const Uint64 end = SDL_GetPerformanceCounter();  // PROFILING ONLY
    frameTiming.pathfindingMsThisCycle = getElapsedMs(start, end);  // PROFILING ONLY
    
    // Record cycle statistics
    frameTiming.pathsPerCycleStats.add(static_cast<double>(frameTiming.pathsProcessedThisCycle));
    frameTiming.pathTokensPerCycleStats.add(static_cast<double>(frameTiming.pathTokensThisCycle));
    
    // Track max tokens per frame
    if(frameTiming.pathTokensThisFrame > frameTiming.maxPathTokensPerFrame) {
        frameTiming.maxPathTokensPerFrame = frameTiming.pathTokensThisFrame;
    }
    
    // Track max per-cycle values (PROFILING ONLY - does not affect gameplay)
    if(frameTiming.pathfindingMsThisCycle > frameTiming.maxPathfindingMsPerCycle) {
        frameTiming.maxPathfindingMsPerCycle = frameTiming.pathfindingMsThisCycle;
    }
    if(frameTiming.pathsProcessedThisCycle > frameTiming.maxPathsPerCycle) {
        frameTiming.maxPathsPerCycle = frameTiming.pathsProcessedThisCycle;
    }
    
    // MULTIPLAYER TELEMETRY: Log synchronization info
    if(pNetworkManager != nullptr && frameTiming.pathsProcessedThisCycle > 0) {
        // Log every 10 seconds to verify synchronization
        if((gameCycleCount % MILLI2CYCLES(10000)) == 0) {
            SDL_Log("[MP-Sync Cycle %d] Budget: %zu (carry: %zu), Paths: %d, Tokens Used: %zu, Queue: %zu",
                    gameCycleCount,
                    negotiatedBudget,
                    carryOverTokens,
                    frameTiming.pathsProcessedThisCycle,
                    frameTiming.pathTokensThisCycle,
                    pathRequestQueue.size());
        }
    }

    logPathInstrumentationIfNeeded();
}


void Game::requestLowerBudget(int steps) {
    // PHASE 1.4: REQUEST LOWER BUDGET VIA NETWORK COMMAND
    SDL_Log("[PathBudget] requestLowerBudget triggered (current=%zu, steps=%d)",
            negotiatedBudget, steps);
    
    // Calculate target: reduce by steps × 500
    // Examples:
    //   1 step: 15k → 14.5k (gradual reduction)
    //   4 steps: 15k → 13k (aggressive FPS recovery)
    size_t reduction = steps * 500;
    size_t targetBudget;
    if (negotiatedBudget > kMinBudget + reduction) {
        targetBudget = negotiatedBudget - reduction;
    } else {
        targetBudget = kMinBudget;  // Floor at minimum (8k)
    }
    
    targetBudget = std::clamp(targetBudget, kMinBudget, kMaxBudget);
    
    // Don't request if already at minimum
    if (targetBudget >= negotiatedBudget) {
        SDL_Log("[PathBudget] Already at minimum (%zu tokens/cycle)", negotiatedBudget);
        return;
    }
    
    SDL_Log("[PathBudget] Requesting reduction: %zu -> %zu tokens/cycle (%d steps × 500)",
            negotiatedBudget, targetBudget, steps);
    logPerformance("[BUDGET CHANGE] Cycle %d: Requesting reduction %zu -> %zu tokens/cycle (%d steps × 500, queue=%zu)",
            gameCycleCount, negotiatedBudget, targetBudget, steps, pathRequestQueue.size());
    
    // In single-player, apply immediately
    if (pNetworkManager == nullptr) {
        negotiatedBudget = targetBudget;
        carryOverTokens = 0;  // Reset carry-over on budget change
        lastBudgetAction = BudgetAction::DECREASED;  // Track emergency drop for anti-oscillation
        SDL_Log("[PathBudget] Applied immediately (single-player)");
        logPerformance("[BUDGET CHANGE] Cycle %d: Applied immediately - new budget=%zu", 
                gameCycleCount, negotiatedBudget);
        return;
    }
    
    // In multiplayer, use the proper budget negotiation system
    // Only the host can broadcast budget changes
    if(pNetworkManager->isServer()) {
        // HOST: Broadcast the change to all clients
        SDL_Log("[PathBudget] Host broadcasting budget reduction: %zu -> %zu", 
                negotiatedBudget, targetBudget);
        lastBudgetAction = BudgetAction::DECREASED;  // Track emergency drop for anti-oscillation
        broadcastBudgetChange(targetBudget);
    } else {
        // CLIENT: This shouldn't happen - clients don't request budget changes
        // Budget changes are driven by the host's decision logic
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "[PathBudget] Client called requestLowerBudget() - ignoring (host controls budget in multiplayer)");
        logPerformance("[PathBudget] Cycle %d: Client incorrectly called requestLowerBudget() - ignoring",
                gameCycleCount);
    }
}

// MULTIPLAYER BUDGET NEGOTIATION IMPLEMENTATION

void Game::checkBudgetAdjustment() {
    if(isSpectating()) return;
    if(pNetworkManager != nullptr && !pNetworkManager->isServer()) {
        // CLIENT: Send stats ONE CYCLE BEFORE the check interval
        // This ensures the host has fresh data when it makes its decision
        if((gameCycleCount + 1) % kBudgetCheckInterval == 0 && frameTiming.frameCount > 0) {
            const double avgFps = (frameTiming.frameCount * 1000.0 / frameTiming.totalMs);
            
            // Send the budget we'll have at decision time (next cycle), not current budget
            // This avoids false DESYNC detection when a budget change is pending
            size_t budgetAtDecisionTime = negotiatedBudget;
            for (const auto& pending : pendingBudgetChanges) {
                if (pending.applyCycle == gameCycleCount + 1) {
                    budgetAtDecisionTime = pending.newBudget;
                    break;  // Use the first pending change for next cycle
                }
            }
            
            sendStatsToHost(avgFps, frameTiming.simMsAvg, pathRequestQueue.size(), budgetAtDecisionTime);
        }
    }
    else if(gameCycleCount % kBudgetCheckInterval == 0 && frameTiming.frameCount > 0) {
        const double avgFps = (frameTiming.frameCount * 1000.0 / frameTiming.totalMs);
        
        if(pNetworkManager != nullptr && pNetworkManager->isServer()) {
            // HOST: Make decision based on collected stats + handle missing stats
            // At this point, client stats from (cycle - 1) have arrived
            makeHostBudgetDecision();
        } else {
            // SINGLE-PLAYER: Apply adjustment immediately
            applySinglePlayerBudgetAdjustment(avgFps);
        }
    }
}

void Game::applySinglePlayerBudgetAdjustment(float avgFps) {
    // VSync-compatible thresholds: 50/55/58 FPS for decrease, 59.5 FPS for increase
    const double fpsIncreaseThreshold = 59.5;
    const double fpsDecreaseThresholdModerate = 58.0;
    const double fpsDecreaseThresholdAggressive = 55.0;
    const double fpsDecreaseThresholdSevere = 50.0;

    // Pathfinding's own cost over the interval — the same measure the raise
    // branch below already uses to block increases. Reductions now consult it
    // too, so an AI or city-simulation stall cannot shrink the path budget and
    // starve unit movement. Single-player only; see kPathReductionFloorMs.
    const double avgPathfindingMs = (frameTiming.frameCount > 0)
        ? (frameTiming.pathfindingMs / frameTiming.frameCount)
        : 0.0;
    if(PathBudgetSync::shouldHoldBudgetDespiteLowFps(
           avgFps, avgPathfindingMs, fpsDecreaseThresholdModerate, kPathReductionFloorMs)) {
        SDL_Log("[PathBudget] Cycle %d: FPS=%.1f low but pathfinding only %.1fms/frame "
                "(floor %.1fms) - holding budget at %zu, cost is elsewhere",
                gameCycleCount, avgFps, avgPathfindingMs, kPathReductionFloorMs, negotiatedBudget);
        logPerformance("[PathBudget] Cycle %d: FPS=%.1f low but pathfinding only %.1fms/frame "
                "(floor %.1fms) - holding budget at %zu, cost is elsewhere",
                gameCycleCount, avgFps, avgPathfindingMs, kPathReductionFloorMs, negotiatedBudget);
        lastBudgetAction = BudgetAction::NONE;
        return;
    }

    // DROP: Three-tier reduction based on severity
    if(avgFps < fpsDecreaseThresholdSevere && negotiatedBudget > kMinBudget) {
        // Severe lag: massive reduction
        size_t oldBudget = negotiatedBudget;
        requestLowerBudget(8);  // Reduce by 4k (8 × 500)
        
        SDL_Log("[PathBudget] Cycle %d: Average FPS below 50: %.1f FPS - reducing budget severely (8 steps × 500) %zu -> %zu", 
                gameCycleCount, avgFps, oldBudget, negotiatedBudget);
        logPerformance("[PathBudget] Cycle %d: Average FPS below 50: %.1f FPS - reducing budget severely %zu -> %zu", 
                gameCycleCount, avgFps, oldBudget, negotiatedBudget);
        
        lastBudgetAction = BudgetAction::DECREASED;
        logFrameTiming();
    }
    else if(avgFps < fpsDecreaseThresholdAggressive && negotiatedBudget > kMinBudget) {
        // Aggressive lag: strong reduction
        size_t oldBudget = negotiatedBudget;
        requestLowerBudget(4);  // Reduce by 2k (4 × 500)
        
        SDL_Log("[PathBudget] Cycle %d: Average FPS below 55: %.1f FPS - reducing budget aggressively (4 steps × 500) %zu -> %zu", 
                gameCycleCount, avgFps, oldBudget, negotiatedBudget);
        logPerformance("[PathBudget] Cycle %d: Average FPS below 55: %.1f FPS - reducing budget aggressively %zu -> %zu", 
                gameCycleCount, avgFps, oldBudget, negotiatedBudget);
        
        lastBudgetAction = BudgetAction::DECREASED;
        logFrameTiming();
    }
    else if(avgFps < fpsDecreaseThresholdModerate && negotiatedBudget > kMinBudget) {
        // Moderate lag: gentle reduction
        size_t oldBudget = negotiatedBudget;
        requestLowerBudget(2);  // Reduce by 1k (2 × 500)
        
        SDL_Log("[PathBudget] Cycle %d: Average FPS below 58: %.1f FPS - reducing budget moderately (2 steps × 500) %zu -> %zu", 
                gameCycleCount, avgFps, oldBudget, negotiatedBudget);
        logPerformance("[PathBudget] Cycle %d: Average FPS below 58: %.1f FPS - reducing budget moderately %zu -> %zu", 
                gameCycleCount, avgFps, oldBudget, negotiatedBudget);
        
        lastBudgetAction = BudgetAction::DECREASED;
        logFrameTiming();
    }
    // RAISE: If average FPS stable at 59.5+ AND pathfinding isn't consuming too much time
    else if(avgFps >= fpsIncreaseThreshold && negotiatedBudget < kMaxBudget) {
        // ANTI-OSCILLATION: Don't increase if we just increased last cycle
        // This ensures at least 2 intervals (12 seconds) between increases
        if(lastBudgetAction == BudgetAction::INCREASED) {
            SDL_Log("[PathBudget] Cycle %d: Skipping increase - just increased last check (anti-oscillation)", 
                    gameCycleCount);
            logPerformance("[PathBudget] Cycle %d: Skipping increase - stabilizing after last increase", 
                    gameCycleCount);
            lastBudgetAction = BudgetAction::NONE;  // Reset for next cycle
            return;
        }
        
        const size_t queueDepth = pathRequestQueue.size();

        // avgPathfindingMs is computed once above and shared with the reduction
        // gate; it is still frameTiming.pathfindingMs (cumulative) averaged over
        // the interval's frames, not a single frame's cost.

        // Safety check: don't increase if pathfinding is already eating too much time
        if(avgPathfindingMs > 10.0) {
            SDL_Log("[PathBudget] Cycle %d: FPS=%.1f but pathfinding time too high (%.1fms) - blocking budget increase", 
                    gameCycleCount, avgFps, avgPathfindingMs);
            logPerformance("[PathBudget] Cycle %d: FPS=%.1f but pathfinding time too high (%.1fms) - blocking budget increase", 
                    gameCycleCount, avgFps, avgPathfindingMs);
            lastBudgetAction = BudgetAction::NONE;
        } else {
            // KEY INSIGHT: High queue + good FPS = we have CPU headroom to drain the queue!
            // Use consistent 500 token increments with 12-second spacing to prevent oscillation
            size_t oldBudget = negotiatedBudget;
            size_t increaseAmount = 500;  // Always 500, but with anti-oscillation delay
            const char* increaseReason = (queueDepth > 300) ? "queue>300" : "queue<=300";
            
            size_t newBudget = std::min<size_t>(negotiatedBudget + increaseAmount, kMaxBudget);
            
            SDL_Log("[PathBudget] Cycle %d: %s FPS>=59.5 (%.1f) pathfinding=%.1fms queue=%zu - increasing budget %zu -> %zu", 
                    gameCycleCount, increaseReason, avgFps, avgPathfindingMs, queueDepth, oldBudget, newBudget);
            logPerformance("[BUDGET CHANGE] Cycle %d: %s - %zu -> %zu tokens/cycle (FPS=%.1f, pathfinding=%.1fms, queue=%zu)",
                    gameCycleCount, increaseReason, oldBudget, newBudget, avgFps, avgPathfindingMs, queueDepth);
            
            negotiatedBudget = newBudget;
            carryOverTokens = 0;  // Reset carry-over when budget changes
            lastBudgetAction = BudgetAction::INCREASED;  // Track action
            
            SDL_Log("[PathBudget] Applied immediately - new budget=%zu", negotiatedBudget);
            logPerformance("[BUDGET CHANGE] Cycle %d: Applied immediately - new budget=%zu", 
                    gameCycleCount, negotiatedBudget);
        }
    }
    // NOTE: Don't reset lastBudgetAction when FPS is stable (60-80)!
    // We need to preserve the action state for anti-oscillation to work.
    // lastBudgetAction is only reset after we've skipped an increase cycle.
}

void Game::sendStatsToHost(float avgFps, float simMsAvg, size_t queueDepth, size_t currentBudget) {
    if(pNetworkManager == nullptr) {
        return;  // Single-player, nothing to send
    }
    
    // Host doesn't need to send stats to itself (it computes them locally in makeHostBudgetDecision)
    if(pNetworkManager->isServer()) {
        return;  // Host, nothing to send
    }
    
    // LOGGING: Outbound stats to host (before sending)
    SDL_Log("[PathBudget CLIENT] → OUTBOUND to host: FPS=%.1f, SimAvg=%.2fms, queue=%zu, budget=%zu (cycle %d)",
            avgFps, simMsAvg, queueDepth, currentBudget, gameCycleCount);
    logPerformance("[CLIENT OUTBOUND] Cycle %d: Sending stats to host: FPS=%.1f, SimAvg=%.2fms, queue=%zu, budget=%zu, carryOver=%zu",
            gameCycleCount, avgFps, simMsAvg, queueDepth, currentBudget, carryOverTokens);
    
    // Send via NetworkManager (including simulation timing)
    pNetworkManager->sendClientStats(avgFps, simMsAvg, queueDepth, currentBudget, gameCycleCount);
    
    SDL_Log("[PathBudget CLIENT] ✓ Stats sent to host");
    logPerformance("[CLIENT OUTBOUND] Cycle %d: Stats packet sent successfully", gameCycleCount);
}

void Game::handleClientStats(Uint32 clientId, Uint32 gameCycle, float avgFps, float simMsAvg, Uint32 queueDepth, Uint32 currentBudget) {
    // HOST ONLY: Collect stats from clients
    if(pNetworkManager == nullptr || !pNetworkManager->isServer()) {
        return;  // Only host processes client stats
    }

    // Advisory numbers from a peer: non-finite values poison every later comparison and a
    // report from the future would defeat the staleness logic.
    if(!std::isfinite(avgFps) || !std::isfinite(simMsAvg) || avgFps < 0.0f || simMsAvg < 0.0f) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[PathBudget HOST] Ignoring non-finite stats from client %u", clientId);
        return;
    }
    if(gameCycle > gameCycleCount + static_cast<Uint32>(kBudgetCheckInterval)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[PathBudget HOST] Ignoring stats from client %u for future cycle %u (current %u)",
                    clientId, gameCycle, gameCycleCount);
        return;
    }

    // One entry per connected client; a peer cannot grow this map.
    if(clientStats.size() >= kMaxTrackedClients && clientStats.find(clientId) == clientStats.end()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[PathBudget HOST] Ignoring stats from client %u: tracking limit reached", clientId);
        return;
    }


    // Store client stats
    ClientPerformanceStats stats;
    stats.clientId = clientId;
    stats.lastUpdateCycle = gameCycle;
    stats.avgFps = avgFps;
    stats.simMsAvg = simMsAvg;  // POST-VSYNC: Primary metric for CPU load
    stats.queueDepth = queueDepth;  // Instantaneous at cycle boundary
    stats.currentBudget = currentBudget;
    stats.missedUpdates = 0;  // Reset counter on successful receive
    
    clientStats[clientId] = stats;
    
    // LOGGING: Inbound client stats
    SDL_Log("[PathBudget HOST] ← INBOUND from Client %d: FPS=%.1f, SimAvg=%.2fms, queue=%d, budget=%d (cycle %d)",
            clientId, avgFps, simMsAvg, queueDepth, currentBudget, gameCycle);
    logPerformance("[HOST INBOUND] Cycle %d: Client %d stats: FPS=%.1f, SimAvg=%.2fms, queue=%d, budget=%d",
            gameCycleCount, clientId, avgFps, simMsAvg, queueDepth, currentBudget);
    
    // Mark that we received stats from this client
    // Don't immediately make a decision - wait for checkBudgetAdjustment to trigger it
}

bool Game::haveStatsFromAllClients() const {
    // NOTE: This function is currently unused since we switched to time-based decision making
    // The host makes decisions every kBudgetCheckInterval, not when all stats arrive
    // Missing stats are handled by handleMissingClientStats() which uses stale data
    
    if(pNetworkManager == nullptr) {
        return false;
    }
    
    // TODO: If we ever need this again, implement getConnectedClientCount()
    // const size_t expectedClientCount = pNetworkManager->getConnectedClientCount();
    // return clientStats.size() >= expectedClientCount;
    
    return false;  // Unused
}

void Game::handleMissingClientStats() {
    // HOST ONLY: Handle clients that haven't sent stats
    if(pNetworkManager == nullptr || !pNetworkManager->isServer()) {
        return;
    }
    
    for(auto& [clientId, stats] : clientStats) {
        const Uint32 cyclesSinceLastUpdate = gameCycleCount - stats.lastUpdateCycle;
        
        // Clients send stats one cycle BEFORE the check interval
        // So we expect stats every kBudgetCheckInterval cycles, but they arrive at (interval - 1)
        // If stats are older than (kBudgetCheckInterval + a few grace cycles), they're stale
        if(cyclesSinceLastUpdate > kBudgetCheckInterval + 5) {
            stats.missedUpdates++;
            
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[PathBudget] Client %d stats are stale (last update: %d cycles ago, missed: %d)",
                clientId, cyclesSinceLastUpdate, stats.missedUpdates);
            
            // After 3 consecutive missed updates (3 × 7.5s = 22.5 seconds)
            if(stats.missedUpdates >= 3) {
                // Assume worst-case: client is struggling
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "[PathBudget] Client %d has missed 3+ updates - assuming poor performance",
                    clientId);
                logPerformance("[PathBudget] Cycle %d: Client %d stale (missed %d) - forcing reduction",
                        gameCycleCount, clientId, stats.missedUpdates);
                
                // Force FPS to 0 to trigger reduction
                stats.avgFps = 0.0f;
                stats.queueDepth = 1000;  // Assume high queue
            }
            // After 2 missed updates: reuse last known values but log warning
            else {
                SDL_Log("[PathBudget] Client %d: Reusing last known values (FPS=%.1f, queue=%d)",
                        clientId, stats.avgFps, stats.queueDepth);
                logPerformance("[PathBudget] Cycle %d: Client %d stats reused (missed %d)",
                        gameCycleCount, clientId, stats.missedUpdates);
            }
        }
    }
}

void Game::makeHostBudgetDecision() {
    // HOST ONLY: Decide budget based on ALL client stats + own stats
    if(pNetworkManager == nullptr || !pNetworkManager->isServer()) {
        return;
    }
    
    // Calculate own stats
    const double hostFps = (frameTiming.frameCount * 1000.0 / frameTiming.totalMs);
    const size_t hostQueueDepth = pathRequestQueue.size();  // Instantaneous
    
    // If we have no client stats yet (first interval), only use host's own stats
    // This is safe because clients send stats one cycle early, so after the first interval
    // we'll always have client data. For the very first decision, host-only is acceptable.
    if(clientStats.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "[PathBudget HOST] First decision with no client stats yet - using host-only (FPS=%.1f, queue=%zu)",
            hostFps, hostQueueDepth);
        logPerformance("[HOST DECISION] Cycle %d: FIRST INTERVAL (no client stats yet - host-only decision)",
                gameCycleCount);
        // Fall through to make decision based on host stats only
    }
    
    // Handle missing client stats (timeout logic)
    handleMissingClientStats();
    
    // Find minimum FPS and maximum CPU load across all peers (slowest peer wins)
    float minFps = hostFps;
    size_t maxQueueDepth = hostQueueDepth;
    float maxSimMsAvg = frameTiming.simMsAvg;  // Track worst CPU load
    
    // Track desync status
    bool allClientsSynced = true;
    
    for(const auto& [clientId, stats] : clientStats) {
        // Validate budget synchronization using cycle-based comparison
        // See: .analysis/features/pathbudget-sync/design.md for protocol contract
        
        bool budgetMatches = (stats.currentBudget == negotiatedBudget);
        
        // Defense-in-depth: Allow previous budget ONLY if client's report is from BEFORE the change
        // This is a tight cycle-based check, not an arbitrary time window
        bool clientReportFromBeforeChange = (stats.lastUpdateCycle < lastBudgetChangeCycle);
        bool matchesPrevious = (stats.currentBudget == previousNegotiatedBudget);
        
        // Budget is valid if:
        // 1. It matches current budget (normal case), OR
        // 2. Client's report is from before the last change AND matches previous budget
        bool budgetValid = budgetMatches || (clientReportFromBeforeChange && matchesPrevious);
        
        if(!budgetValid) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "[PathBudget] DESYNC DETECTED! Client %d budget=%d but host=%zu (prev=%zu, clientCycle=%d, changeCycle=%d)",
                clientId, stats.currentBudget, negotiatedBudget, previousNegotiatedBudget,
                stats.lastUpdateCycle, lastBudgetChangeCycle);
            logPerformance("[DESYNC CRITICAL] Client %d has budget=%d but host has %zu - re-syncing immediately",
                    clientId, stats.currentBudget, negotiatedBudget);
            
            // Immediately re-sync this client
            resyncClientBudget(clientId);
            allClientsSynced = false;
        }
        
        // Decision logic uses FPS, queue depth, and CPU load (simMsAvg)
        if(stats.avgFps < minFps) {
            minFps = stats.avgFps;  // Track slowest peer (lowest FPS)
        }
        if(stats.queueDepth > maxQueueDepth) {
            maxQueueDepth = stats.queueDepth;
        }
        if(stats.simMsAvg > maxSimMsAvg) {
            maxSimMsAvg = stats.simMsAvg;  // Track highest CPU load
        }
    }
    
    // If any clients are out of sync, don't make budget changes until they're synced
    if(!allClientsSynced) {
        SDL_Log("[PathBudget] Deferring budget decision until all clients re-synced");
        logPerformance("[PathBudget] Cycle %d: Deferring decision - waiting for client re-sync",
                gameCycleCount);
        return;  // Skip this decision cycle
    }
    
    // Decision logic: "Slowest peer wins" - based on FPS AND CPU load
    size_t newBudget = negotiatedBudget;
    const char* decisionReason = "STABLE";
    
    // VSync-compatible thresholds: 50/55/58 FPS for decrease, 59.5 FPS for increase
    const double fpsIncreaseThreshold = 59.5;
    const double fpsDecreaseThresholdModerate = 58.0;
    const double fpsDecreaseThresholdAggressive = 55.0;
    const double fpsDecreaseThresholdSevere = 50.0;
    
    if(minFps < fpsDecreaseThresholdSevere) {
        // AT LEAST ONE peer is struggling severely → REDUCE budget massively
        newBudget = negotiatedBudget >= 4000 + kMinBudget ? 
                    negotiatedBudget - 4000 : kMinBudget;
        decisionReason = "REDUCE (FPS<50, severe)";
        lastBudgetAction = BudgetAction::DECREASED;  // Track action for multiplayer sync
    }
    else if(minFps < fpsDecreaseThresholdAggressive) {
        // AT LEAST ONE peer is struggling aggressively → REDUCE budget aggressively
        newBudget = negotiatedBudget >= 2000 + kMinBudget ? 
                    negotiatedBudget - 2000 : kMinBudget;
        decisionReason = "REDUCE (FPS<55, aggressive)";
        lastBudgetAction = BudgetAction::DECREASED;  // Track action for multiplayer sync
    }
    else if(minFps < fpsDecreaseThresholdModerate) {
        // AT LEAST ONE peer is struggling moderately → REDUCE budget moderately
        newBudget = negotiatedBudget >= 1000 + kMinBudget ? 
                    negotiatedBudget - 1000 : kMinBudget;
        decisionReason = "REDUCE (FPS<58, moderate)";
        lastBudgetAction = BudgetAction::DECREASED;  // Track action for multiplayer sync
    }
    else if(maxSimMsAvg > 12.0f) {
        // AT LEAST ONE peer is CPU-bound (>12ms per 16ms tick) → REDUCE budget
        // This catches vsync-locked clients that still report 60 FPS but are struggling
        newBudget = negotiatedBudget >= 2000 + kMinBudget ? 
                    negotiatedBudget - 2000 : kMinBudget;
        decisionReason = "REDUCE (high CPU)";
        lastBudgetAction = BudgetAction::DECREASED;  // Track action for multiplayer sync
    }
    else if(minFps > fpsIncreaseThreshold && negotiatedBudget < kMaxBudget) {
        // ALL peers have good FPS → consider INCREASE
        
        // ANTI-OSCILLATION: Don't increase if we just increased last cycle
        if(lastBudgetAction == BudgetAction::INCREASED) {
            decisionReason = "SKIP (stabilizing after increase)";
            newBudget = negotiatedBudget;  // Keep current
            lastBudgetAction = BudgetAction::NONE;  // Reset for next cycle
        } else {
            // Check host's pathfinding time as a safety metric
            // Use frameTiming.pathfindingMs (cumulative) not pathfindingMsThisFrame (single frame)
            const double avgPathfindingMs = (frameTiming.frameCount > 0) 
                ? (frameTiming.pathfindingMs / frameTiming.frameCount) 
                : 0.0;
            
            if(avgPathfindingMs > 10.0) {
                // Pathfinding taking too much time - don't increase
                decisionReason = "BLOCKED (pathfinding>10ms)";
                newBudget = negotiatedBudget;  // Keep current
                lastBudgetAction = BudgetAction::NONE;
            } else {
                // KEY INSIGHT: High queue + good FPS = we have CPU headroom!
                // Use consistent 500 token increments with 12-second spacing to prevent oscillation
                size_t increaseAmount = 500;  // Always 500, anti-oscillation via lastBudgetAction
                decisionReason = (maxQueueDepth > 300) ? "INCREASE (queue>300)" : "INCREASE (queue<=300)";
                
                newBudget = std::min<size_t>(negotiatedBudget + increaseAmount, kMaxBudget);
                lastBudgetAction = BudgetAction::INCREASED;  // Track action for all clients
            }
        }
    }
    else {
        // Stable - no change needed
        decisionReason = "STABLE (FPS in range)";
        // NOTE: Don't reset lastBudgetAction! Preserve it for anti-oscillation.
    }
    
    // LOGGING: Only log on actual budget changes (reduce host overhead)
    if(newBudget != negotiatedBudget) {
        SDL_Log("[PathBudget HOST] ═══ BUDGET CHANGE CYCLE %d ═══", gameCycleCount);
        SDL_Log("[PathBudget HOST] Inputs: minFps=%.1f (host=%.1f), maxQueue=%zu (host=%zu)",
                minFps, hostFps, maxQueueDepth, hostQueueDepth);
        SDL_Log("[PathBudget HOST] Decision: %s → %zu → %zu",
                decisionReason, negotiatedBudget, newBudget);
        logPerformance("[HOST DECISION] Cycle %d: %s %zu → %zu (minFps=%.1f, maxQueue=%zu)",
                gameCycleCount, decisionReason, negotiatedBudget, newBudget, minFps, maxQueueDepth);
        
        broadcastBudgetChange(newBudget);
    }
    // Silent when stable (no logging overhead)
    
    // Don't clear clientStats! We need to keep them to detect missing updates
    // The stats will be updated when new packets arrive (missedUpdates reset to 0)
}

void Game::broadcastBudgetChange(size_t newBudget) {
    // HOST ONLY: Broadcast budget change to all clients
    if(pNetworkManager == nullptr || !pNetworkManager->isServer()) {
        return;
    }
    
    // Calculate apply cycle: NEXT interval boundary (e.g., cycle 750 if we're at cycle 375)
    // This gives a full interval (375 cycles) for the broadcast to reach all clients
    // and ensures budget changes always align with measurement windows
    const Uint32 nextInterval = ((gameCycleCount / kBudgetCheckInterval) + 1) * kBudgetCheckInterval;
    const Uint32 applyCycle = nextInterval;
    
    // LOGGING: Outbound budget broadcast (before sending)
    SDL_Log("[PathBudget HOST] → OUTBOUND to ALL clients: budget %zu → %zu (apply cycle %d)",
            negotiatedBudget, newBudget, applyCycle);
    logPerformance("[HOST OUTBOUND] Cycle %d: Broadcasting budget %zu → %zu (apply cycle %d)",
            gameCycleCount, negotiatedBudget, newBudget, applyCycle);
    
    // Send via NetworkManager broadcast to all clients
    pNetworkManager->broadcastPathBudget(newBudget, applyCycle);
    
    // Also queue locally (host applies the same change)
    handleSetPathBudget(newBudget, applyCycle);
    
    SDL_Log("[PathBudget HOST] ✓ Broadcast sent to all clients and queued locally");
    logPerformance("[HOST OUTBOUND] Cycle %d: Broadcast sent successfully",
            gameCycleCount);
}

void Game::resyncClientBudget(Uint32 clientId) {
    // HOST ONLY: Emergency re-sync for out-of-sync client
    if(pNetworkManager == nullptr || !pNetworkManager->isServer()) {
        return;
    }
    
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "[PathBudget] Re-syncing client %d with current budget %zu",
        clientId, negotiatedBudget);
    
    // Send immediate sync packet (apply at next interval boundary)
    // Even for desyncs, we apply at the next interval to maintain determinism
    const Uint32 nextInterval = ((gameCycleCount / kBudgetCheckInterval) + 1) * kBudgetCheckInterval;
    const Uint32 applyCycle = nextInterval;
    
    // NOTE: For now, we broadcast to all clients (since ENet doesn't have per-peer send in our current API)
    // This is safe - extra sync messages won't hurt synchronized clients
    pNetworkManager->broadcastPathBudget(negotiatedBudget, applyCycle);
    
    SDL_Log("[PathBudget HOST] ✓ Re-sync broadcast sent to all clients (including client %d)", clientId);
    logPerformance("[DESYNC RECOVERY] Cycle %d: Sent re-sync broadcast to all clients (budget=%zu, apply cycle %d)",
            gameCycleCount, negotiatedBudget, applyCycle);
}

void Game::handleSetPathBudget(size_t newBudget, Uint32 applyCycle) {
    // ALL CLIENTS: Queue the budget change for deterministic application

    // The host schedules budget changes on interval boundaries in the near future. Anything
    // else desynchronises the deterministic pathfinding schedule, so it is refused here even
    // though the sender already had to be the host connection.
    if(!PathBudgetSync::isAcceptableBudgetOrder(newBudget, applyCycle, gameCycleCount,
                                                static_cast<uint32_t>(kBudgetCheckInterval),
                                                kMinBudget, kMaxBudget)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[PathBudget] Refusing budget order %zu at cycle %u (current cycle %u)",
                    newBudget, applyCycle, gameCycleCount);
        return;
    }

    if(pendingBudgetChanges.size() >= PathBudgetSync::kMaxPendingBudgetChanges) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[PathBudget] Refusing budget order: %zu changes already pending",
                    pendingBudgetChanges.size());
        return;
    }

    // LOGGING: Inbound budget order from host
    SDL_Log("[PathBudget CLIENT] ← INBOUND from host: budget %zu → %zu (apply cycle %d, current cycle %d, delta=%d cycles)",
            negotiatedBudget, newBudget, applyCycle, gameCycleCount, 
            (int)(applyCycle - gameCycleCount));
    logPerformance("[CLIENT INBOUND] Cycle %d: Received budget order %zu → %zu (apply cycle %d, carryOver=%zu)",
            gameCycleCount, negotiatedBudget, newBudget, applyCycle, carryOverTokens);
    
    // Store in pending commands queue
    PendingBudgetChange change;
    change.newBudget = newBudget;
    change.applyCycle = applyCycle;
    change.resetCarryOver = true;  // CRITICAL: Must clear carry-over tokens!
    pendingBudgetChanges.push_back(change);
    
    SDL_Log("[PathBudget CLIENT] ✓ Budget order queued (pending: %zu)",
            pendingBudgetChanges.size());
}

void Game::applyPendingBudgetChanges() {
    // Called every cycle to check for pending budget changes
    
    auto it = pendingBudgetChanges.begin();
    while(it != pendingBudgetChanges.end()) {
        if(gameCycleCount >= it->applyCycle) {
            // Apply budget change NOW
            size_t oldBudget = negotiatedBudget;
            
            // Track previous budget for DESYNC defense-in-depth:
            // allow the previous budget only when the client report cycle is before this change cycle.
            previousNegotiatedBudget = oldBudget;
            lastBudgetChangeCycle = gameCycleCount;
            
            negotiatedBudget = std::clamp(it->newBudget, kMinBudget, kMaxBudget);
            
            // CRITICAL FOR SYNC: Reset carry-over tokens on budget change
            // Without this, clients can drift due to accumulated token differences
            size_t oldCarryOver = carryOverTokens;
            if(it->resetCarryOver) {
                carryOverTokens = 0;
            }
            
            // LOGGING: Budget application
            SDL_Log("[PathBudget] ★ APPLIED budget change at cycle %d: %zu → %zu (carryOver %zu → 0)",
                    gameCycleCount, oldBudget, negotiatedBudget, oldCarryOver);
            logPerformance("[BUDGET APPLIED] Cycle %d: %zu → %zu tokens/cycle (carryOver %zu → 0, queue=%zu)",
                    gameCycleCount, oldBudget, negotiatedBudget, oldCarryOver, pathRequestQueue.size());
            
            // Log full performance report on budget change
            logFrameTiming();
            
            // Remove from pending queue
            it = pendingBudgetChanges.erase(it);
        } else {
            ++it;
        }
    }
}


void Game::drawScreen()
{
    Coord TopLeftTile = screenborder->getTopLeftTile();
    Coord BottomRightTile = screenborder->getBottomRightTile();

    // Add margin to avoid pop-in during scrolling
    TopLeftTile.x = std::max(0, TopLeftTile.x - 2);
    TopLeftTile.y = std::max(0, TopLeftTile.y - 2);
    BottomRightTile.x = std::min(currentGameMap->getSizeX()-1, BottomRightTile.x + 2);
    BottomRightTile.y = std::min(currentGameMap->getSizeY()-1, BottomRightTile.y + 2);

    const auto x1 = TopLeftTile.x;
    const auto y1 = TopLeftTile.y;
    const auto x2 = BottomRightTile.x + 1;
    const auto y2 = BottomRightTile.y + 1;

    /* draw ground */
    currentGameMap->for_each(x1, y1, x2, y2,
        [](Tile& t) {
            t.blitGround(screenborder->world2screenX(t.getLocation().x*TILESIZE),
                screenborder->world2screenY(t.getLocation().y*TILESIZE));
        });

    /* draw city overlay */
    if (currentCityOverlay_ != DuneCity::CityOverlayMode::None && citySimulation_) {
        drawCityOverlay(x1, y1, x2, y2);
    }

    /* draw placeholder road visuals */
    drawCityRoads(x1, y1, x2, y2);

    /* draw structures */
    currentGameMap->for_each(x1, y1, x2, y2,
        [](Tile& t) {
            t.blitStructures(screenborder->world2screenX(t.getLocation().x*TILESIZE),
                screenborder->world2screenY(t.getLocation().y*TILESIZE));
        });

    /* draw underground units */
    currentGameMap->for_each(x1, y1, x2, y2,
        [](Tile& t) {
            t.blitUndergroundUnits(screenborder->world2screenX(t.getLocation().x*TILESIZE),
                screenborder->world2screenY(t.getLocation().y*TILESIZE));
        });

    /* draw dead objects */
    currentGameMap->for_each(x1, y1, x2, y2,
        [](Tile& t) {
            t.blitDeadUnits(screenborder->world2screenX(t.getLocation().x*TILESIZE),
                screenborder->world2screenY(t.getLocation().y*TILESIZE));
        });

    /* draw infantry */
    currentGameMap->for_each(x1, y1, x2, y2,
        [](Tile& t) {
            t.blitInfantry(screenborder->world2screenX(t.getLocation().x*TILESIZE),
                screenborder->world2screenY(t.getLocation().y*TILESIZE));
        });

    /* draw non-infantry ground units */
    currentGameMap->for_each(x1, y1, x2, y2,
        [](Tile& t) {
            t.blitNonInfantryGroundUnits(screenborder->world2screenX(t.getLocation().x*TILESIZE),
                screenborder->world2screenY(t.getLocation().y*TILESIZE));
        });

    /* draw bullets */
    for(const Bullet* pBullet : bulletList) {
        pBullet->blitToScreen();
    }


    /* draw explosions */
    for(const Explosion* pExplosion : explosionList) {
        pExplosion->blitToScreen();
    }

    /* draw air units */
    currentGameMap->for_each(x1, y1, x2, y2,
        [&](Tile& t) {
            t.blitAirUnits(screenborder->world2screenX(t.getLocation().x*TILESIZE),
                screenborder->world2screenY(t.getLocation().y*TILESIZE));
        });

    if(settings.general.showMovementPaths)drawMovementPaths();

    // draw the gathering point line if a structure is selected
    if(selectedList.size() == 1) {
        StructureBase *pStructure = dynamic_cast<StructureBase*>(getObjectManager().getObject(*selectedList.begin()));
        if(pStructure != nullptr) {
            pStructure->drawGatheringPointLine();
        }
    }

    /* draw selection rectangles */
    currentGameMap->for_each(x1, y1, x2, y2,
        [](Tile& t) {
            if (debug || currentGame->isSpectating() || t.isExploredByTeam(pLocalHouse->getTeamID())) {
                t.blitSelectionRects(screenborder->world2screenX(t.getLocation().x*TILESIZE),
                    screenborder->world2screenY(t.getLocation().y*TILESIZE));
            }
        });


//////////////////////////////draw unexplored/shade

    if(!debug && !isSpectating()) {
        SDL_Texture* hiddenTexZoomed = pGFXManager->getZoomedObjPic(ObjPic_Terrain_Hidden, currentZoomlevel);
        SDL_Texture* hiddenFogTexZoomed = pGFXManager->getZoomedObjPic(ObjPic_Terrain_HiddenFog, currentZoomlevel);
        int zoomedTileSize = world2zoomedWorld(TILESIZE);
        for(int x = screenborder->getTopLeftTile().x - 1; x <= screenborder->getBottomRightTile().x + 1; x++) {
            for (int y = screenborder->getTopLeftTile().y - 1; y <= screenborder->getBottomRightTile().y + 1; y++) {

                if((x >= 0) && (x < currentGameMap->getSizeX()) && (y >= 0) && (y < currentGameMap->getSizeY())) {
                    Tile* pTile = currentGameMap->getTile(x, y);

                    if(pTile->isExploredByTeam(pLocalHouse->getTeamID())) {
                        int hideTile = pTile->getHideTile(pLocalHouse->getTeamID());

                        if(hideTile != 0) {
                            SDL_Rect source = { hideTile*zoomedTileSize, 0, zoomedTileSize, zoomedTileSize };
                            SDL_Rect drawLocation = {   screenborder->world2screenX(x*TILESIZE), screenborder->world2screenY(y*TILESIZE),
                                                        zoomedTileSize, zoomedTileSize };
                            SDL_RenderCopy(renderer, hiddenTexZoomed, &source, &drawLocation);
                        }

                        if(gameInitSettings.getGameOptions().fogOfWar == true) {
                            int fogTile = pTile->getFogTile(pLocalHouse->getTeamID());

                            if(pTile->isFoggedByTeam(pLocalHouse->getTeamID()) == true) {
                                fogTile = Terrain_HiddenFull;
                            }

                            if(fogTile != 0) {
                                SDL_Rect source = { fogTile*zoomedTileSize, 0,
                                                    zoomedTileSize, zoomedTileSize };
                                SDL_Rect drawLocation = {   screenborder->world2screenX(x*TILESIZE), screenborder->world2screenY(y*TILESIZE),
                                                            zoomedTileSize, zoomedTileSize };

                                SDL_RenderCopy(renderer, hiddenFogTexZoomed, &source, &drawLocation);
                            }
                        }
                    } else {
                        if(!debug) {
                            SDL_Rect source = { zoomedTileSize*15, 0, zoomedTileSize, zoomedTileSize };
                            SDL_Rect drawLocation = {   screenborder->world2screenX(x*TILESIZE), screenborder->world2screenY(y*TILESIZE),
                                                        zoomedTileSize, zoomedTileSize };
                            SDL_RenderCopy(renderer, hiddenTexZoomed, &source, &drawLocation);
                        }
                    }
                } else {
                    // we are outside the map => draw complete hidden
                    SDL_Rect source = { zoomedTileSize*15, 0, zoomedTileSize, zoomedTileSize };
                    SDL_Rect drawLocation = {   screenborder->world2screenX(x*TILESIZE), screenborder->world2screenY(y*TILESIZE),
                                                zoomedTileSize, zoomedTileSize };
                    SDL_RenderCopy(renderer, hiddenTexZoomed, &source, &drawLocation);
                }
            }
        }
    }

/////////////draw placement position

    if(currentCursorMode == CursorMode_Placing) {
        //if user has selected to place a structure

        if(screenborder->isScreenCoordInsideMap(drawnMouseX, drawnMouseY)) {
            //if mouse is not over game bar

            int xPos = screenborder->screen2MapX(drawnMouseX);
            int yPos = screenborder->screen2MapY(drawnMouseY);

            if(selectedList.size() == 1) {
                auto pBuilder = dynamic_cast<BuilderBase*>(objectManager.getObject(*selectedList.begin()));
                if(pBuilder) {
                    int placeItem = pBuilder->getCurrentProducedItem();
                    Coord structuresize = getStructureSize(placeItem);
                    const bool footprintInsideMap =
                        structuresize.x > 0 && structuresize.y > 0
                        && xPos >= 0 && yPos >= 0
                        && xPos + structuresize.x <= currentGameMap->getSizeX()
                        && yPos + structuresize.y <= currentGameMap->getSizeY();
                    static int loggedPlacementStateCount = 0;
                    StructurePlacementPreview placementPreviewForLog;
                    if(loggedPlacementStateCount < 12 && getTornieStructurePlacementPreview(placeItem, placementPreviewForLog)) {
                        loggedPlacementStateCount++;
                        SDL_Log("TornieGFX: placing-state item=%d objectPic=%u house=%d zoom=%d selected=%u tile=(%d,%d) size=%dx%d",
                                placeItem,
                                placementPreviewForLog.objectPic,
                                pBuilder->getOwner()->getHouseID(),
                                currentZoomlevel,
                                static_cast<unsigned int>(selectedList.size()),
                                xPos,
                                yPos,
                                structuresize.x,
                                structuresize.y);
                    }

                    bool withinRange = false;
                    if(footprintInsideMap) {
                        for (int i = xPos; i < (xPos + structuresize.x); i++) {
                            for (int j = yPos; j < (yPos + structuresize.y); j++) {
                                if (currentGameMap->isWithinBuildRange(i, j, pBuilder->getOwner())) {
                                    withinRange = true;         //find out if the structure is close enough to other buildings
                                }
                            }
                        }
                    }

                    SDL_Texture* validPlace = nullptr;
                    SDL_Texture* invalidPlace = nullptr;

                    switch(currentZoomlevel) {
                        case 0: {
                            validPlace = pGFXManager->getUIGraphic(UI_ValidPlace_Zoomlevel0);
                            invalidPlace = pGFXManager->getUIGraphic(UI_InvalidPlace_Zoomlevel0);
                        } break;

                        case 1: {
                            validPlace = pGFXManager->getUIGraphic(UI_ValidPlace_Zoomlevel1);
                            invalidPlace = pGFXManager->getUIGraphic(UI_InvalidPlace_Zoomlevel1);
                        } break;

                        case 2:
                        default: {
                            validPlace = pGFXManager->getUIGraphic(UI_ValidPlace_Zoomlevel2);
                            invalidPlace = pGFXManager->getUIGraphic(UI_InvalidPlace_Zoomlevel2);
                        } break;

                    }

                    // A zone may reach onto sand, but at least one tile must sit on rock or slab.
                    bool zoneFootprintAnchored = !isZoneStructure(placeItem);
                    if(!zoneFootprintAnchored && footprintInsideMap) {
                        for(int i = xPos; i < (xPos + structuresize.x) && !zoneFootprintAnchored; i++) {
                            for(int j = yPos; j < (yPos + structuresize.y); j++) {
                                if(currentGameMap->tileExists(i,j)
                                   && DuneCity::isCityBuildableTerrain(currentGameMap->getTile(i,j)->getType())) {
                                    zoneFootprintAnchored = true;
                                    break;
                                }
                            }
                        }
                    }

                    for(int i = xPos; i < (xPos + structuresize.x); i++) {
                        for(int j = yPos; j < (yPos + structuresize.y); j++) {
                            SDL_Texture* image;

                            bool tileValid = false;
                            if(footprintInsideMap && withinRange && currentGameMap->tileExists(i,j)) {
                                Tile* pTile = currentGameMap->getTile(i,j);
                                if(isZoneStructure(placeItem)) {
                                    tileValid = zoneFootprintAnchored
                                        && DuneCity::isCityZoneTerrain(pTile->getType())
                                        && !pTile->hasCityZone()
                                        && !pTile->hasAGroundObject();
                                } else {
                                    tileValid = pTile->isRock() && !pTile->isMountain() && !pTile->hasAGroundObject()
                                        && !(((placeItem == Structure_Slab1) || (placeItem == Structure_Slab4)) && pTile->isConcrete());
                                }
                            }
                            image = tileValid ? validPlace : invalidPlace;

                            SDL_Rect drawLocation = calcDrawingRect(image, screenborder->world2screenX(i*TILESIZE), screenborder->world2screenY(j*TILESIZE));
                            SDL_RenderCopy(renderer, image, nullptr, &drawLocation);
                        }
                    }

                    StructurePlacementPreview preview;
                    if(footprintInsideMap && getTornieStructurePlacementPreview(placeItem, preview)) {
                        const int ownerHouse = pBuilder->getOwner()->getHouseID();
                        static int loggedPlacementPreviewCandidateCount = 0;
                        if(loggedPlacementPreviewCandidateCount < 12) {
                            loggedPlacementPreviewCandidateCount++;
                            SDL_Log("TornieGFX: placement-preview candidate item=%d objectPic=%u house=%d zoom=%d size=%dx%d tile=(%d,%d)",
                                    placeItem,
                                    preview.objectPic,
                                    ownerHouse,
                                    currentZoomlevel,
                                    structuresize.x,
                                    structuresize.y,
                                    xPos,
                                    yPos);
                        }
                        if(SDL_Texture* previewTexture = pGFXManager->getZoomedObjPic(preview.objectPic,
                                                                                       ownerHouse,
                                                                                       currentZoomlevel)) {
                            Uint8 previousAlpha = SDL_ALPHA_OPAQUE;
                            SDL_BlendMode previousBlendMode = SDL_BLENDMODE_NONE;
                            SDL_GetTextureAlphaMod(previewTexture, &previousAlpha);
                            SDL_GetTextureBlendMode(previewTexture, &previousBlendMode);
                            SDL_SetTextureBlendMode(previewTexture, SDL_BLENDMODE_BLEND);

                            const auto drawPreviewFrame = [&](int frame, Uint8 alpha) {
                                const int numFrames = preview.framesX * preview.framesY;
                                if((frame < 0) || (frame >= numFrames)) {
                                    return;
                                }

                                const int frameX = frame % preview.framesX;
                                const int frameY = frame / preview.framesX;
                                SDL_Rect source = calcSpriteSourceRect(previewTexture, frameX, preview.framesX, frameY, preview.framesY);
                                SDL_Rect dest = calcSpriteDrawingRect(previewTexture,
                                                                      screenborder->world2screenX(xPos * TILESIZE),
                                                                      screenborder->world2screenY(yPos * TILESIZE),
                                                                      preview.framesX,
                                                                      preview.framesY);

                                SDL_SetTextureAlphaMod(previewTexture, alpha);
                                SDL_RenderCopy(renderer, previewTexture, &source, &dest);

                                static int loggedPlacementPreviewCount = 0;
                                if(loggedPlacementPreviewCount < 12) {
                                    loggedPlacementPreviewCount++;
                                    SDL_Log("TornieGFX: placement-preview item=%d objectPic=%u house=%d zoom=%d frame=%d alpha=%u source=(%d,%d %dx%d) dest=(%d,%d %dx%d)",
                                            placeItem,
                                            preview.objectPic,
                                            ownerHouse,
                                            currentZoomlevel,
                                            frame,
                                            static_cast<unsigned int>(alpha),
                                            source.x,
                                            source.y,
                                            source.w,
                                            source.h,
                                            dest.x,
                                            dest.y,
                                            dest.w,
                                            dest.h);
                                }
                            };

                            drawPreviewFrame(preview.frame, 96);
                            drawPreviewFrame(preview.overlayFrame, 255);

                            SDL_SetTextureAlphaMod(previewTexture, previousAlpha);
                            SDL_SetTextureBlendMode(previewTexture, previousBlendMode);
                        } else {
                            static int loggedMissingPlacementPreviewCount = 0;
                            if(loggedMissingPlacementPreviewCount < 12) {
                                loggedMissingPlacementPreviewCount++;
                                SDL_Log("TornieGFX: placement-preview texture missing item=%d objectPic=%u house=%d zoom=%d",
                                        placeItem,
                                        preview.objectPic,
                                        ownerHouse,
                                        currentZoomlevel);
                            }
                        }
                    }
                }
            }
        }
    }

    if(currentCursorMode == CursorMode_CityRoad) {
        if(screenborder->isScreenCoordInsideMap(drawnMouseX, drawnMouseY)) {
            int xPos = screenborder->screen2MapX(drawnMouseX);
            int yPos = screenborder->screen2MapY(drawnMouseY);

            SDL_Texture* validPlace = nullptr;
            SDL_Texture* invalidPlace = nullptr;

            switch(currentZoomlevel) {
                case 0:
                    validPlace = pGFXManager->getUIGraphic(UI_ValidPlace_Zoomlevel0);
                    invalidPlace = pGFXManager->getUIGraphic(UI_InvalidPlace_Zoomlevel0);
                    break;
                case 1:
                    validPlace = pGFXManager->getUIGraphic(UI_ValidPlace_Zoomlevel1);
                    invalidPlace = pGFXManager->getUIGraphic(UI_InvalidPlace_Zoomlevel1);
                    break;
                case 2:
                default:
                    validPlace = pGFXManager->getUIGraphic(UI_ValidPlace_Zoomlevel2);
                    invalidPlace = pGFXManager->getUIGraphic(UI_InvalidPlace_Zoomlevel2);
                    break;
            }

            if(currentGameMap->tileExists(xPos, yPos)) {
                Tile* pTile = currentGameMap->getTile(xPos, yPos);
                const bool tileValid = DuneCity::canPlaceRoad(makeCityTilePlacementState(*pTile));
                SDL_Texture* image = tileValid ? validPlace : invalidPlace;
                SDL_Rect drawLocation = calcDrawingRect(image,
                    screenborder->world2screenX(xPos * TILESIZE),
                    screenborder->world2screenY(yPos * TILESIZE));
                SDL_RenderCopy(renderer, image, nullptr, &drawLocation);
            }
        }
    }

///////////draw game selection rectangle
    if(selectionMode) {

        int finalMouseX = drawnMouseX;
        int finalMouseY = drawnMouseY;
        if(finalMouseX >= sideBarPos.x) {
            //this keeps the box on the map, and not over game bar
            finalMouseX = sideBarPos.x-1;
        }

        if(finalMouseY < topBarPos.y+topBarPos.h) {
            finalMouseY = topBarPos.x+topBarPos.h;
        }

        // draw the mouse selection rectangle
        renderDrawRect( renderer,
                        screenborder->world2screenX(selectionRect.x),
                        screenborder->world2screenY(selectionRect.y),
                        finalMouseX,
                        finalMouseY,
                        COLOR_WHITE);
    }



///////////draw action indicator

    auto* actionTarget=objectManager.getObject(actionIndicatorObject);
    const auto* actionTile=actionTarget && currentGameMap->tileExists(actionTarget->getLocation())
        ? currentGameMap->getTile(actionTarget->getLocation()) : nullptr;
    const bool showTarget=actionTarget && actionTile && !SDL_TICKS_PASSED(SDL_GetTicks(),actionIndicatorUntil)
        && actionTarget->isVisible(pLocalHouse->getTeamID()) && actionTile->isExploredByTeam(pLocalHouse->getTeamID())
        && !actionTile->isFoggedByTeam(pLocalHouse->getTeamID());
    if(showTarget && (SDL_GetTicks()/100)%2==0) actionTarget->drawSelectionBox();
    if(!showTarget && (indicatorFrame != NONE_ID) && (screenborder->isInsideScreen(indicatorPosition, Coord(TILESIZE,TILESIZE)) == true)) {
        SDL_Texture* pUIIndicator = pGFXManager->getUIGraphic(UI_Indicator);
        SDL_Rect source = calcSpriteSourceRect(pUIIndicator, indicatorFrame, 3);
        SDL_Rect drawLocation = calcSpriteDrawingRect(  pUIIndicator,
                                                        screenborder->world2screenX(indicatorPosition.x),
                                                        screenborder->world2screenY(indicatorPosition.y),
                                                        3, 1,
                                                        HAlign::Center, VAlign::Center);
        SDL_RenderCopy(renderer, pUIIndicator, &source, &drawLocation);
    }


///////////draw game bar
    pInterface->draw(Point(0,0));
    pInterface->drawOverlay(Point(0,0));
    drawCityPlacementHint();
    if(pNetworkManager && isSpectating() && pNetworkManager->observerCatchingUp()) {
        observerProgress.resize(std::min(580,sideBarPos.x-40),32);
        observerProgress.setProgress(pNetworkManager->lateJoinPercent());
        observerProgress.setText(pNetworkManager->lateJoinProgressText());
        observerProgress.draw(Point(20,80));
    }

    // draw chat message currently typed
    if(chatMode) {
        sdl2::texture_ptr pChatTexture = pFontManager->createTextureWithText("Chat: " + typingChatMessage + (((SDL_GetTicks() / 150) % 2 == 0) ? "_" : ""), COLOR_WHITE, 14);
        SDL_Rect drawLocation = calcDrawingRect(pChatTexture.get(), 20, getRendererHeight() - 40);
        SDL_RenderCopy(renderer, pChatTexture.get(), nullptr, &drawLocation);
    }

    if(bShowFPS) {
        std::string strFPS = fmt::sprintf("fps: %.1f ", 1000.0f/averageFrameTime);

        sdl2::texture_ptr pFPSTexture = pFontManager->createTextureWithText(strFPS, COLOR_WHITE, 14);
        SDL_Rect drawLocation = calcDrawingRect(pFPSTexture.get(),sideBarPos.x - strFPS.length()*8, 60);
        SDL_RenderCopy(renderer, pFPSTexture.get(), nullptr, &drawLocation);
    }

    if(bShowTime) {
        int seconds = getGameTime() / 1000;
        std::string strTime = fmt::sprintf(" %.2d:%.2d:%.2d", seconds / 3600, (seconds % 3600)/60, (seconds % 60) );

        sdl2::texture_ptr pTimeTexture = pFontManager->createTextureWithText(strTime, COLOR_WHITE, 14);
        SDL_Rect drawLocation = calcAlignedDrawingRect(pTimeTexture.get(), HAlign::Left, VAlign::Bottom);
        drawLocation.y++;
        SDL_RenderCopy(renderer, pTimeTexture.get(), nullptr, &drawLocation);
    }

    if(finished) {
        std::string message;

        if(won) {
            message = _("You Have Completed Your Mission.");
        } else {
            message = _("You Have Failed Your Mission.");
        }

        sdl2::texture_ptr pFinishMessageTexture = pFontManager->createTextureWithText(message.c_str(), COLOR_WHITE, 28);
        SDL_Rect drawLocation = calcDrawingRect(pFinishMessageTexture.get(), sideBarPos.x/2, topBarPos.h + (getRendererHeight()-topBarPos.h)/2, HAlign::Center, VAlign::Center);
        SDL_RenderCopy(renderer, pFinishMessageTexture.get(), nullptr, &drawLocation);
    }

    if(pWaitingForOtherPlayers != nullptr) {
        pWaitingForOtherPlayers->draw();
    }

    if(pInGameMenu != nullptr) {
        pInGameMenu->draw();
    } else if(pInGameMentat != nullptr) {
        pInGameMentat->draw();
    }

    if(isSpectating()) {
        if(!spectatorLabel) spectatorLabel=pFontManager->createTextureWithText("Spectating",COLOR_WHITE,18);
        SDL_Rect rect=calcDrawingRect(spectatorLabel.get(),sideBarPos.x/2,topBarPos.h+8,HAlign::Center,VAlign::Top);
        SDL_RenderCopy(renderer,spectatorLabel.get(),nullptr,&rect);
    }
    // Update cursor
    updateCursor();
}


void Game::doInput()
{
    SDL_Event event;
    while(SDL_PollEvent(&event)) {
        updateCursorVisibilityForInput(event);
        if(TouchInput::translateTouchEvent(event)) {
            continue;
        }
        // check for a key press

        // first of all update mouse
        if(event.type == SDL_MOUSEMOTION) {
            SDL_MouseMotionEvent* mouse = &event.motion;
            drawnMouseX = std::max(0, std::min(mouse->x, settings.video.width-1));
            drawnMouseY = std::max(0, std::min(mouse->y, settings.video.height-1));

            static Uint32 lastCursorLog = 0;
            // Cursor debug logging disabled
            /*const Uint32 now = SDL_GetTicks();
            if(now - lastCursorLog >= 500) {
                bool insideMap = (screenborder != nullptr) && screenborder->isScreenCoordInsideMap(drawnMouseX, drawnMouseY);
                int mapX = insideMap ? screenborder->screen2MapX(drawnMouseX) : -1;
                int mapY = insideMap ? screenborder->screen2MapY(drawnMouseY) : -1;
                SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,
                               "CursorDebug: raw=(%d,%d) drawn=(%d,%d) mapInside=%s map=(%d,%d) cursorMode=%d hardware=%s",
                               mouse->x,
                               mouse->y,
                               drawnMouseX,
                               drawnMouseY,
                               insideMap ? "yes" : "no",
                               mapX,
                               mapY,
                               static_cast<int>(currentCursorMode),
                               cursorManager.isInitialized() ? "yes" : "no");
                lastCursorLog = now;
            }*/
        }

        if(pNetworkManager && pNetworkManager->lateJoinPaused() && event.type!=SDL_QUIT && dynamic_cast<JoinProgressWindow*>(pInGameMenu.get())==nullptr) continue;
        if(pInGameMenu != nullptr) {
            pInGameMenu->handleInput(event);

            if(bMenu == false) {
                pInGameMenu.reset();
            }

        } else if(pInGameMentat != nullptr) {
            pInGameMentat->doInput(event);

            if(bMenu == false) {
                pInGameMentat.reset();
            }

        } else if(pWaitingForOtherPlayers != nullptr) {
            pWaitingForOtherPlayers->handleInput(event);

            if(bMenu == false) {
                pWaitingForOtherPlayers.reset();
            }

        } else {
            /* Look for a keypress */
            switch (event.type) {

                case SDL_KEYDOWN: {
                    if(chatMode) {
                        handleChatInput(event.key);
                    } else {
                        handleKeyInput(event.key);
                    }
                } break;

                case SDL_TEXTINPUT: {
                    if(chatMode) {
                        std::string newText = event.text.text;
                        if(utf8Length(typingChatMessage) + utf8Length(newText) <= 60) {
                            typingChatMessage += newText;
                        }
                    }
                } break;

                case SDL_MOUSEWHEEL: {
                    if (event.wheel.y != 0) {
                        const bool wheelHandled = pInterface->handleMouseWheel(
                            drawnMouseX, drawnMouseY, event.wheel.y > 0);
                        if(!wheelHandled) {
                            applyDune2RZoom(currentZoomlevel + (event.wheel.y > 0 ? 1 : -1));
                        }
                    }
                } break;

                case SDL_MOUSEBUTTONDOWN: {
                    SDL_MouseButtonEvent* mouse = &event.button;

                    bool interfaceHandled = false;
                    switch(mouse->button) {
                        case SDL_BUTTON_LEFT: {
                            interfaceHandled = pInterface->handleMouseLeft(mouse->x, mouse->y, true);
                        } break;

                        case SDL_BUTTON_RIGHT: {
                            interfaceHandled = pInterface->handleMouseRight(mouse->x, mouse->y, true);
                        } break;
                    }

                    if(interfaceHandled) break; // A UI click must not also place or command units.

                    switch(mouse->button) {

                        case SDL_BUTTON_LEFT: {

                            switch(currentCursorMode) {

                                case CursorMode_Placing: {
                                    if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                        handlePlacementClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y));
                                    }
                                } break;

                                case CursorMode_Attack: {

                                    if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                        handleSelectedObjectsAttackClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y));
                                    }

                                } break;

case CursorMode_Heal: {

                                    if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                        handleSelectedObjectsHealClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y));
                                    }

                                } break;

                                case CursorMode_Move: {

                                    if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                        handleSelectedObjectsMoveClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y));
                                    }

                                } break;

                                case CursorMode_CarryallDrop: {

                                    if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                        handleSelectedObjectsRequestCarryallDropClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y));
                                    }

                                } break;

                                case CursorMode_Capture: {

                                    if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                        handleSelectedObjectsCaptureClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y));
                                    }

                                } break;

                                case CursorMode_CityZone: {

                                    if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                        handleCityZonePlacementClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y));
                                    }

                                } break;

                                case CursorMode_CityRoad: {

                                    if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                        handleCityRoadPlacementClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y));
                                    }

                                } break;

                                case CursorMode_Normal:
                                default: {

                                    if (mouse->x < sideBarPos.x && mouse->y >= topBarPos.h) {
                                        // it isn't on the gamebar

                                        if(!selectionMode) {
                                            // if we have started the selection rectangle
                                            // the starting point of the selection rectangele
                                            selectionRect.x = screenborder->screen2worldX(mouse->x);
                                            selectionRect.y = screenborder->screen2worldY(mouse->y);
                                        }
                                        selectionMode = true;

                                    }
                                } break;
                            }
                        } break;    //end of SDL_BUTTON_LEFT

                        case SDL_BUTTON_RIGHT: {
                            //if the right mouse button is pressed

                            if(currentCursorMode != CursorMode_Normal) {
                                //cancel special cursor mode
                                setCursorMode(CursorMode_Normal);
                            } else if(settings.general.leftClickOrders) {
                                unselectAll(selectedList); selectedList.clear(); selectionMode=false;
                            } else if((!selectedList.empty()
                                            && (((objectManager.getObject(*selectedList.begin()))->getOwner() == pLocalHouse))
                                            && (((objectManager.getObject(*selectedList.begin()))->isRespondable())) ) )
                            {
                                //if user has a controlable unit selected

                                if(screenborder->isScreenCoordInsideMap(mouse->x, mouse->y) == true) {
                                    if(handleSelectedObjectsActionClick(screenborder->screen2MapX(mouse->x), screenborder->screen2MapY(mouse->y))) {
                                        indicatorFrame = 0;
                                        indicatorPosition.x = screenborder->screen2worldX(mouse->x);
                                        indicatorPosition.y = screenborder->screen2worldY(mouse->y);
                                    }
                                }
                            }
                        } break;    //end of SDL_BUTTON_RIGHT
                    }
                } break;

                case SDL_MOUSEMOTION: {
                    SDL_MouseMotionEvent* mouse = &event.motion;

                    pInterface->handleMouseMovement(mouse->x,mouse->y);
                } break;

                case SDL_MOUSEBUTTONUP: {
                    SDL_MouseButtonEvent* mouse = &event.button;

                    bool interfaceHandled = false;
                    switch(mouse->button) {
                        case SDL_BUTTON_LEFT: {
                            interfaceHandled = pInterface->handleMouseLeft(mouse->x, mouse->y, false);
                        } break;

                        case SDL_BUTTON_RIGHT: {
                            interfaceHandled = pInterface->handleMouseRight(mouse->x, mouse->y, false);
                        } break;
                    }

                    if(interfaceHandled) {
                        selectionMode = false;
                        break;
                    }

                    if(selectionMode && mouse->button==SDL_BUTTON_LEFT && settings.general.leftClickOrders
                        && currentCursorMode==CursorMode_Normal && !(SDL_GetModState() & KMOD_SHIFT)
                        && std::abs(mouse->x-screenborder->world2screenX(selectionRect.x))<=4
                        && std::abs(mouse->y-screenborder->world2screenY(selectionRect.y))<=4
                        && screenborder->isScreenCoordInsideMap(mouse->x,mouse->y)) {
                        const Coord target(screenborder->screen2MapX(mouse->x),screenborder->screen2MapY(mouse->y));
                        const auto* tile=currentGameMap->getTile(target);
                        const auto* object=tile ? tile->getObjectAt(screenborder->screen2worldX(mouse->x),screenborder->screen2worldY(mouse->y)) : nullptr;
                        if((!object || object->getOwner()!=pLocalHouse) && handleSelectedObjectsActionClick(target.x,target.y)) {
                            indicatorFrame=0;
                            indicatorPosition=Coord(screenborder->screen2worldX(mouse->x),screenborder->screen2worldY(mouse->y));
                            selectionMode=false;
                        }
                    }
                    if(selectionMode && (mouse->button == SDL_BUTTON_LEFT)) {
                        //this keeps the box on the map, and not over game bar
                        int finalMouseX = mouse->x;
                        int finalMouseY = mouse->y;

                        if(finalMouseX >= sideBarPos.x) {
                            finalMouseX = sideBarPos.x-1;
                        }

                        if(finalMouseY < topBarPos.y+topBarPos.h) {
                            finalMouseY = topBarPos.x+topBarPos.h;
                        }

                        int rectFinishX = screenborder->screen2MapX(finalMouseX);
                        if(rectFinishX > (currentGameMap->getSizeX()-1)) {
                            rectFinishX = currentGameMap->getSizeX()-1;
                        }

                        int rectFinishY = screenborder->screen2MapY(finalMouseY);

                        // convert start also to map coordinates
                        int rectStartX = selectionRect.x/TILESIZE;
                        int rectStartY = selectionRect.y/TILESIZE;

                        currentGameMap->selectObjects(  pLocalHouse,
                                                        rectStartX, rectStartY, rectFinishX, rectFinishY,
                                                        screenborder->screen2worldX(finalMouseX),
                                                        screenborder->screen2worldY(finalMouseY),
                                                        SDL_GetModState() & KMOD_SHIFT);

                        if(selectedList.size() == 1) {
                            ObjectBase* pObject = objectManager.getObject( *selectedList.begin());
                            if(pObject != nullptr && pObject->getOwner() == pLocalHouse && isHarvesterLikeObject(pObject)) {
                                GroundUnit* pHarvester = static_cast<GroundUnit*>(pObject);

                                std::string harvesterMessage = resolveItemName(pObject->getItemID());

                                int percent = lround(100 * harvesterGetAmountOfSpice(pObject) / HARVESTERMAXSPICE);
                                if(percent > 0) {
                                    if(pHarvester->isAwaitingPickup()) {
                                        harvesterMessage += fmt::sprintf(_("@DUNE.ENG|124#full and awaiting pickup"), percent);
                                    } else if(harvesterIsReturning(pObject)) {
                                        harvesterMessage += fmt::sprintf(_("@DUNE.ENG|123#full and returning"), percent);
                                    } else if(harvesterIsHarvesting(pObject)) {
                                        harvesterMessage += fmt::sprintf(_("@DUNE.ENG|122#full and harvesting"), percent);
                                    } else {
                                        harvesterMessage += fmt::sprintf(_("@DUNE.ENG|121#full"), percent);
                                    }

                                } else {
                                    if(pHarvester->isAwaitingPickup()) {
                                        harvesterMessage += _("@DUNE.ENG|128#empty and awaiting pickup");
                                    } else if(harvesterIsReturning(pObject)) {
                                        harvesterMessage += _("@DUNE.ENG|127#empty and returning");
                                    } else if(harvesterIsHarvesting(pObject)) {
                                        harvesterMessage += _("@DUNE.ENG|126#empty and harvesting");
                                    } else {
                                        harvesterMessage += _("@DUNE.ENG|125#empty");
                                    }
                                }

                                if(!pInterface->newsTickerHasMessage()) {
                                    pInterface->addToNewsTicker(harvesterMessage);
                                }
                            }
                        }
                    }

                    selectionMode = false;

                } break;

                case (SDL_QUIT): {
                    bQuitGame = true;
                } break;

                default:
                    break;
            }
        }
    }

    if((pInGameMenu == nullptr) && (pInGameMentat == nullptr) && (pWaitingForOtherPlayers == nullptr) && (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS)) {

        const Uint8 *keystate = SDL_GetKeyboardState(nullptr);
        const bool cameraKeys=!chatMode && !pInterface->hasChildWindow();
        const bool wasd=cameraKeys && settings.general.wasdCamera && !(SDL_GetModState() & (KMOD_SHIFT|KMOD_CTRL|KMOD_ALT|KMOD_GUI));
        scrollDownMode =  (drawnMouseY >= getRendererHeight()-1-SCROLLBORDER) || (cameraKeys && keystate[SDL_SCANCODE_DOWN]) || (wasd && keystate[SDL_SCANCODE_S]);
        scrollLeftMode = (drawnMouseX <= SCROLLBORDER) || (cameraKeys && keystate[SDL_SCANCODE_LEFT]) || (wasd && keystate[SDL_SCANCODE_A]);
        scrollRightMode = (drawnMouseX >= getRendererWidth()-1-SCROLLBORDER) || (cameraKeys && keystate[SDL_SCANCODE_RIGHT]) || (wasd && keystate[SDL_SCANCODE_D]);
        scrollUpMode = (drawnMouseY <= SCROLLBORDER) || (cameraKeys && keystate[SDL_SCANCODE_UP]) || (wasd && keystate[SDL_SCANCODE_W]);

        if(scrollLeftMode && scrollRightMode) {
            // do nothing
        } else if(scrollLeftMode) {
            scrollLeftMode = screenborder->scrollLeft();
        } else if(scrollRightMode) {
            scrollRightMode = screenborder->scrollRight();
        }

        if(scrollDownMode && scrollUpMode) {
            // do nothing
        } else if(scrollDownMode) {
            scrollDownMode = screenborder->scrollDown();
        } else if(scrollUpMode) {
            scrollUpMode = screenborder->scrollUp();
        }
    } else {
        scrollDownMode = false;
        scrollLeftMode = false;
        scrollRightMode = false;
        scrollUpMode = false;
    }
}


void Game::updateCursor()
{
    if (cursorManager.isInitialized()) {
        cursorManager.setCursorMode(currentCursorMode);
    }
}

void Game::setCursorMode(int mode)
{
    if (cursorManager.canSetCursorMode(mode, std::vector<Uint32>(selectedList.begin(), selectedList.end()))) {
        currentCursorMode = mode;
        cursorManager.setCursorMode(mode);
    }
}

void Game::setupView()
{
    int i = 0;
    int j = 0;
    int count = 0;

    //setup start location/view
    i = j = count = 0;

    for(const UnitBase* pUnit : unitList) {
        if((pUnit->getOwner() == pLocalHouse) && (pUnit->getItemID() != Unit_Sandworm)) {
            i += pUnit->getX();
            j += pUnit->getY();
            count++;
        }
    }

    for(const StructureBase* pStructure : structureList) {
        if(pStructure->getOwner() == pLocalHouse) {
            i += pStructure->getX();
            j += pStructure->getY();
            count++;
        }
    }

    if(count == 0) {
        i = currentGameMap->getSizeX()*TILESIZE/2-1;
        j = currentGameMap->getSizeY()*TILESIZE/2-1;
    } else {
        i = i*TILESIZE/count;
        j = j*TILESIZE/count;
    }

    screenborder->setNewScreenCenter(Coord(i,j));
}


void Game::runMainLoop() {
    SDL_Log("Starting game...");
    initializeGameLoop();
    
    // Update Discord Rich Presence for in-game status
    std::string houseName = pLocalHouse ? getHouseNameByNumber(static_cast<HOUSETYPE>(pLocalHouse->getHouseID())) : "Unknown";
    std::string mapName = gameInitSettings.getFilename();
    // Extract just the map name from the path
    size_t lastSlash = mapName.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        mapName = mapName.substr(lastSlash + 1);
    }
    size_t lastDot = mapName.find_last_of('.');
    if (lastDot != std::string::npos) {
        mapName = mapName.substr(0, lastDot);
    }
    
    if (isNetworkGameType(gameInitSettings.getGameType()) ||
        gameInitSettings.getGameType() == GameType::LoadMultiplayer) {
        // Count human players from game init settings (not alive houses which can change during game)
        int humanPlayerCount = 0;
        int totalPlayerSlots = 0;
        for (const auto& houseInfo : gameInitSettings.getHouseInfoList()) {
            for (const auto& playerInfo : houseInfo.playerInfoList) {
                totalPlayerSlots++;
                if (playerInfo.playerClass == HUMANPLAYERCLASS) {
                    humanPlayerCount++;
                }
            }
        }
        // Use human count for current players, total slots for max party size
        DiscordManager::instance().setMultiplayerGame(houseName, mapName, humanPlayerCount, totalPlayerSlots);
    } else {
        bool isCampaign = (gameInitSettings.getGameType() == GameType::Campaign);
        DiscordManager::instance().setInGame(houseName, mapName, isCampaign);
    }

    int frameStart = SDL_GetTicks();
    int frameTime = 0;
    Uint64 previousFrameEndPerf=0;

    do {
        // Start timing this rendered frame
        const Uint64 frameStartPerf = SDL_GetPerformanceCounter();
        const Uint32 frameStartCycle=gameCycleCount;
        double inputMsThisFrame=0.0,commandsMsThisFrame=0.0;
        if (previousFrameEndPerf) {
            const auto gapUs=static_cast<int64_t>(getElapsedMs(previousFrameEndPerf,frameStartPerf)*1000);
            auto& perf=AITelemetry::log();
            perf.performance(gameCycleCount,-1,"frame.gap",gapUs);
            if (gapUs>=100000) perf.frameStall(gameCycleCount,gapUs,AITelemetry::Record()
                .set("kind","between_frames").set("paused",bPause)
                .set("input_focus",(SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS)!=0));
        }
        frameTiming.gameCyclesThisFrame = 0;
        frameTiming.totalPathsProcessedThisFrame = 0;
        frameTiming.pathTokensThisFrame = 0;
        frameTiming.pathsFailedThisFrame = 0;
        
        // Reset per-frame accumulators
        frameTiming.aiMsThisFrame = 0.0;
        frameTiming.aiWorstHouseMsThisFrame = 0.0;
        frameTiming.aiWorstHouseIdxThisFrame = -1;
        frameTiming.citySimMsThisFrame = 0.0;
        frameTiming.unitsMsThisFrame = 0.0;
        frameTiming.structuresMsThisFrame = 0.0;
        frameTiming.pathfindingMsThisFrame = 0.0;
        frameTiming.renderingMsThisFrame = 0.0;
        frameTiming.networkWaitMsThisFrame = 0.0;
        frameTiming.turretScanMsThisFrame = 0.0;
        frameTiming.turretScansThisFrame = 0;
        frameTiming.unitTargetingMsThisFrame = 0.0;
        frameTiming.unitNavigateMsThisFrame = 0.0;
        frameTiming.unitMoveMsThisFrame = 0.0;
        frameTiming.unitTurnMsThisFrame = 0.0;
        frameTiming.unitVisibilityMsThisFrame = 0.0;
        
        // MULTIPLAYER FIX (Issue #1): Removed time-based pathfinding budget
        // Token budget is now the only gate (deterministic)
        
        // Update Discord Rich Presence callbacks (once per second to avoid overhead)
        static Uint32 lastDiscordUpdate = 0;
        Uint32 discordNow = SDL_GetTicks();
        if (discordNow - lastDiscordUpdate >= 1000) {
            DiscordManager::instance().update();
            lastDiscordUpdate = discordNow;
        }

        // Keep draining the Workshop outbox while an offline match runs, so a player who
        // starts a custom game and keeps playing still contributes that map. Networked
        // games are pumped by NetworkManager and menus by MenuBase; this adds no second
        // pump to either. The transfer is bounded and non-blocking.
        if (!pNetworkManager) {
            Workshop::updatePublications();
        }

        renderFrame();

        const int frameEnd = SDL_GetTicks();
        const int actualFrameTime = frameEnd - frameStart;  // Actual time for this frame
        frameTime += actualFrameTime;
        
        // Bound catch-up after stalls, but keep at least the previous fastest
        // setting's 24ms budget. At 4ms/tick a 60Hz frame needs 4-5 ticks;
        // a three-tick budget would discard time and defeat the faster setting.
        // The independent ten-cycle guard below still prevents renderer starvation.
        const int maxFrameTime = std::max(getGameSpeed() * 3, 24);
        if (frameTime > maxFrameTime) {
            frameTime = maxFrameTime;
        }
        
        frameStart = frameEnd;  // Reset for next frame's game logic timing

        if(bShowFPS) {
            // FPS display uses actual frame time
            averageFrameTime = 0.99f * averageFrameTime + 0.01f * actualFrameTime;
        }

        // MULTIPLAYER FIX (Issue #9): Removed duplicate finished level check
        // This is already handled in updateGameState() with cycle-based timing

        if(takePeriodicalScreenshots && ((gameCycleCount % (MILLI2CYCLES(10*1000))) == 0)) {
            takeScreenshot();
        }

        if(bReplay && !bPause) {
            skipToGameCycle = gameCycleCount + (10*1000)/GAMESPEED_DEFAULT;
        }

        // DIAGNOSTIC: Track loop iterations
        int loopIterations = 0;
        int cyclesExecuted = 0;
        
        // PHASE 1.3: CYCLE GUARDRAIL - Prevent renderer starvation
        static constexpr int kMaxCyclesPerFrame = 10;
        
        while(((frameTime > getGameSpeed()) || (!finished && (gameCycleCount < skipToGameCycle))) 
              && cyclesExecuted < kMaxCyclesPerFrame) {
            loopIterations++;
            
            Uint64 networkWaitStart = SDL_GetPerformanceCounter();
            bool bWaitForNetwork = false;
            if(pNetworkManager != nullptr) {
                bWaitForNetwork = handleNetworkUpdates();
            }

            const Uint64 inputStart=SDL_GetPerformanceCounter();
            processInput();
            inputMsThisFrame+=getElapsedMs(inputStart,SDL_GetPerformanceCounter());

            if(pInGameMentat != nullptr) {
                pInGameMentat->update();
            }

            if(pWaitingForOtherPlayers != nullptr) {
                pWaitingForOtherPlayers->update();
            }

            const Uint64 commandsStart=SDL_GetPerformanceCounter();
            if(!pNetworkManager || !pNetworkManager->lateJoinPaused()) cmdManager.update();
            commandsMsThisFrame+=getElapsedMs(commandsStart,SDL_GetPerformanceCounter());

            if(!bWaitForNetwork && !isGamePaused() && (!pNetworkManager || !pNetworkManager->lateJoinPaused())) {
                // Time the core simulation step for CPU load detection
                const Uint64 simStart = SDL_GetPerformanceCounter();
                try {
                    updateGameState();
                } catch (const std::exception& e) {
                    // Record before stack unwinding closes this match's telemetry.
                    AITelemetry::log().write(gameCycleCount,-1,-1,"simulation_exception",
                        AITelemetry::Record().set("message",e.what()).set("stage","updateGameState"));
                    throw;
                }
                const Uint64 simEnd = SDL_GetPerformanceCounter();
                const double simMs = getElapsedMs(simStart, simEnd);
                
                // Update exponential moving average (0.9/0.1 split ≈ 10-tick average)
                frameTiming.simMsAvg = 0.9 * frameTiming.simMsAvg + 0.1 * simMs;
                
                // Update lagging flag with hysteresis
                static constexpr double kSimThresholdHigh = 12.0;  // ms
                static constexpr double kSimThresholdLow = 10.0;   // ms
                
                if (frameTiming.simMsAvg > kSimThresholdHigh) {
                    frameTiming.simulationLagging = true;
                } else if (frameTiming.simMsAvg < kSimThresholdLow) {
                    frameTiming.simulationLagging = false;
                }
                
                frameTiming.gameCyclesThisFrame++;
                cyclesExecuted++;
                
                // Only decrement frameTime when we actually processed a cycle
                if(gameCycleCount <= skipToGameCycle) {
                    frameTime = 0;
                } else {
                    frameTime -= getGameSpeed();
                }
            } else if(bWaitForNetwork || isGamePaused() || (pNetworkManager && pNetworkManager->lateJoinPaused())) {
                // When waiting for network or paused, measure the wait time
                if(bWaitForNetwork) {
                    Uint64 networkWaitEnd = SDL_GetPerformanceCounter();
                    const double networkWaitMs = getElapsedMs(networkWaitStart, networkWaitEnd);
                    frameTiming.networkWaitMs += networkWaitMs;
                    frameTiming.networkWaitMsThisFrame += networkWaitMs;
                    if(networkWaitMs > frameTiming.maxNetworkWaitMs) frameTiming.maxNetworkWaitMs = networkWaitMs;
                    
                    // Don't reset frameTime - let the game catch up naturally.
                    // The guardrail (10 cycles/frame max) prevents excessive catch-up.
                }
                else if (isGamePaused()){
                    // Pause in single player shouldn't jump after resuming
                    frameTime = 0;
                }
                // Break out of loop to avoid spinning - we'll try again next frame
                // Also add a small delay to avoid burning CPU. Not in the browser: see
                // handleNetworkUpdates(). This loop has just given up on the cycle, the frame
                // ends in yieldFrameToBrowser(), and an ASYNCIFY sleep here would only
                // add a second stack unwind to the same wait.
#ifndef __EMSCRIPTEN__
                SDL_Delay(1);
#endif
                break;
            }
        }
        
        // PHASE 1.3: DETECT GUARDRAIL TRIPS & REQUEST LOWER BUDGET
        if(cyclesExecuted >= kMaxCyclesPerFrame) {
            cycleGuardrailTrips++;
            
            // Log guardrail trip to performance file only at key thresholds
            if(cycleGuardrailTrips % 100 == 0) {
                logPerformance("[GUARDRAIL] Cycle %d: Hit limit %d times (10 cycles/frame) - queue=%zu, budget=%zu",
                        gameCycleCount, cycleGuardrailTrips, pathRequestQueue.size(), negotiatedBudget);
            }
            
            // Log to console periodically
            if((gameCycleCount % MILLI2CYCLES(5000)) == 0) {
                SDL_Log("[Guardrail] Hit cycle limit (%d cycles/frame), queue=%zu, trips=%d",
                        kMaxCyclesPerFrame, pathRequestQueue.size(), cycleGuardrailTrips);
            }
            
            // CHANGED: Only reduce budget after SUSTAINED poor performance
            // 300 guardrail trips = ~300 frames = ~5 seconds at 60 FPS
            // This prevents reacting to temporary spikes
            if(cycleGuardrailTrips >= 300) {
                SDL_Log("[PathBudget] Sustained poor performance detected (%d guardrail trips over ~300 frames)", cycleGuardrailTrips);
                requestLowerBudget();
                cycleGuardrailTrips = 0;  // Reset counter
            }
        } else if(cyclesExecuted < 5) {
            // Running smoothly - aggressively reset counter
            // Decay faster to forgive temporary spikes
            if(cycleGuardrailTrips > 0) {
                cycleGuardrailTrips -= 3;  // Decrease by 3 per smooth frame
                if(cycleGuardrailTrips < 0) {
                    cycleGuardrailTrips = 0;
                }
            }
        }
        
        // DIAGNOSTIC: Only log severe issues (removed frequent logging)
        // Severe frame issues are now captured in the 30-second [PathInstrumentation] report
        if(loopIterations > 20 || cyclesExecuted > 15) {
            logPerformance("[DIAGNOSTIC] Severe frame issue: %d loop iterations, %d cycles executed, gameCycle=%d", 
                loopIterations, cyclesExecuted, gameCycleCount);
        }
        
        // PERIODIC STATUS SNAPSHOT: Every 60 seconds of game time (reduced from 15s)
        if((gameCycleCount % MILLI2CYCLES(60000)) == 0 && gameCycleCount > 0) {
            const double avgFps = frameTiming.frameCount > 0 ? 
                (frameTiming.frameCount * 1000.0 / frameTiming.totalMs) : 0.0;
            const double tokensPerCycle = frameTiming.totalGameCycles > 0 ?
                static_cast<double>(frameTiming.totalPathTokens) / frameTiming.totalGameCycles : 0.0;
            
            const int mapSize = (currentGameMap != nullptr) ? currentGameMap->getSizeX() : 0;
            logPerformance("[STATUS] Cycle %d (%.1f min) - Map: %dx%d | FPS: %.1f | Budget: %zu | Queue: %zu | Tokens/cycle: %.0f",
                    gameCycleCount,
                    gameCycleCount / (60.0 * MILLI2CYCLES(1000)),
                    mapSize, mapSize,
                    avgFps,
                    negotiatedBudget,
                    pathRequestQueue.size(),
                    tokensPerCycle);
        }

        musicPlayer->musicCheck();

        // End timing this rendered frame
        const Uint64 frameEndPerf = SDL_GetPerformanceCounter();
        previousFrameEndPerf=frameEndPerf;
        const double thisFrameMs = getElapsedMs(frameStartPerf, frameEndPerf);
        frameTiming.totalMs += thisFrameMs;
        frameTiming.totalGameCycles += frameTiming.gameCyclesThisFrame;
        frameTiming.totalTurretScans += frameTiming.turretScansThisFrame;
        frameTiming.turretScanMs += frameTiming.turretScanMsThisFrame;
        frameTiming.frameCount++;

        auto& perf = AITelemetry::log();
        if (perf.enabled()) {
            const auto us = [](double ms) { return static_cast<int64_t>(ms*1000); };
            const auto record = [&](const char* name, double ms) { perf.performance(gameCycleCount,-1,name,us(ms)); };
            record("frame",thisFrameMs);
            record("frame.ai",frameTiming.aiMsThisFrame);
            record("frame.city",frameTiming.citySimMsThisFrame);
            record("frame.path",frameTiming.pathfindingMsThisFrame);
            record("frame.units",frameTiming.unitsMsThisFrame);
            record("frame.structures",frameTiming.structuresMsThisFrame);
            record("frame.render",frameTiming.renderingMsThisFrame);
            record("frame.network",frameTiming.networkWaitMsThisFrame);
            record("frame.input",inputMsThisFrame);
            record("frame.commands",commandsMsThisFrame);
            perf.performance(gameCycleCount,-1,"frame.cycles",frameTiming.gameCyclesThisFrame,-1,false);
            perf.performance(gameCycleCount,-1,"frame.units_count",unitList.size(),-1,false);
            perf.performance(gameCycleCount,-1,"frame.structures_count",structureList.size(),-1,false);
            perf.performance(gameCycleCount,-1,"frame.tick_ms",getGameSpeed(),-1,false);
            perf.performance(gameCycleCount,-1,"frame.paused",bPause,-1,false);
            if (perf.isWorstFrame(us(thisFrameMs)) || thisFrameMs>=100.0) {
                const auto context=AITelemetry::Record()
                .set("kind","game_frame").set("start_cycle",frameStartCycle)
                .set("input_us",us(inputMsThisFrame)).set("commands_us",us(commandsMsThisFrame))
                .set("menu_open",pInGameMenu!=nullptr || pInGameMentat!=nullptr || pWaitingForOtherPlayers!=nullptr)
                .set("input_focus",(SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS)!=0)
                .set("ai_us",us(frameTiming.aiMsThisFrame)).set("city_us",us(frameTiming.citySimMsThisFrame))
                .set("path_us",us(frameTiming.pathfindingMsThisFrame)).set("render_us",us(frameTiming.renderingMsThisFrame))
                .set("units_us",us(frameTiming.unitsMsThisFrame)).set("structures_us",us(frameTiming.structuresMsThisFrame))
                .set("network_us",us(frameTiming.networkWaitMsThisFrame))
                .set("worst_house",frameTiming.aiWorstHouseIdxThisFrame).set("worst_house_us",us(frameTiming.aiWorstHouseMsThisFrame))
                .set("cycles",frameTiming.gameCyclesThisFrame).set("queue",pathRequestQueue.size())
                .set("path_nodes",frameTiming.pathTokensThisFrame).set("paths",frameTiming.totalPathsProcessedThisFrame)
                .set("units",unitList.size()).set("structures",structureList.size())
                .set("tick_ms",getGameSpeed()).set("paused",bPause);
                perf.slowFrame(gameCycleCount,us(thisFrameMs),context);
                perf.frameStall(gameCycleCount,us(thisFrameMs),context);
            }
            perf.flushPerformance(gameCycleCount);
        }

        // Frame-spike detection: any single frame slower than ~50 FPS is a
        // visible stutter even if the rolling average looks fine. Log a one-
        // line breakdown so we can correlate the spike with whatever phase
        // ate the time (AI build, structures, units, pathfinding, render).
        // Skip the first 60 frames to suppress warm-up noise.
        constexpr double kFrameSpikeThresholdMs = 20.0;
        constexpr Uint32 kSpikeWarmupFrames = 60;
        static Uint32 lastSpikeLogCycle = 0;
        if (thisFrameMs > kFrameSpikeThresholdMs
            && frameTiming.frameCount > kSpikeWarmupFrames
            // Rate-limit smaller spikes to one per 10 cycles; keep every
            // substantial stall even when adjacent frames are worse.
            && (thisFrameMs>=100.0 || gameCycleCount - lastSpikeLogCycle >= 10 || lastSpikeLogCycle == 0)) {
            lastSpikeLogCycle = gameCycleCount;
            logPerformance("[FRAME SPIKE] Cycle %u frame=%.1fms cycles=%d ai=%.1f(worst h%d=%.1f)"
                           " citySim=%.1f units=%.1f"
                           " (tgt=%.1f nav=%.1f move=%.1f turn=%.1f vis=%.1f)"
                           " struct=%.1f path=%.1f render=%.1f net=%.1f"
                           " turretScan=%.1f(%dx) sim_avg=%.1f units=%d queue=%zu",
                gameCycleCount, thisFrameMs, frameTiming.gameCyclesThisFrame,
                frameTiming.aiMsThisFrame,
                frameTiming.aiWorstHouseIdxThisFrame, frameTiming.aiWorstHouseMsThisFrame,
                frameTiming.citySimMsThisFrame,
                frameTiming.unitsMsThisFrame,
                frameTiming.unitTargetingMsThisFrame, frameTiming.unitNavigateMsThisFrame,
                frameTiming.unitMoveMsThisFrame, frameTiming.unitTurnMsThisFrame,
                frameTiming.unitVisibilityMsThisFrame,
                frameTiming.structuresMsThisFrame, frameTiming.pathfindingMsThisFrame,
                frameTiming.renderingMsThisFrame, frameTiming.networkWaitMsThisFrame,
                frameTiming.turretScanMsThisFrame, frameTiming.turretScansThisFrame,
                frameTiming.simMsAvg, frameTiming.unitCount,
                pathRequestQueue.size());
        }
        
        // Track max values
        if(thisFrameMs > frameTiming.maxTotalMs) frameTiming.maxTotalMs = thisFrameMs;
        if(frameTiming.gameCyclesThisFrame > frameTiming.maxGameCyclesPerFrame) {
            frameTiming.maxGameCyclesPerFrame = frameTiming.gameCyclesThisFrame;
        }
        if(frameTiming.totalPathsProcessedThisFrame > frameTiming.maxPathsPerFrame) {
            frameTiming.maxPathsPerFrame = frameTiming.totalPathsProcessedThisFrame;
        }
        
        // Track min values (per frame)
        if(frameTiming.gameCyclesThisFrame < frameTiming.minGameCyclesPerFrame) {
            frameTiming.minGameCyclesPerFrame = frameTiming.gameCyclesThisFrame;
        }
        if(frameTiming.aiMsThisFrame < frameTiming.minAiMs) frameTiming.minAiMs = frameTiming.aiMsThisFrame;
        if(frameTiming.unitsMsThisFrame < frameTiming.minUnitsMs) frameTiming.minUnitsMs = frameTiming.unitsMsThisFrame;
        if(frameTiming.structuresMsThisFrame < frameTiming.minStructuresMs) frameTiming.minStructuresMs = frameTiming.structuresMsThisFrame;
        if(frameTiming.pathfindingMsThisFrame < frameTiming.minPathfindingMs) frameTiming.minPathfindingMs = frameTiming.pathfindingMsThisFrame;
        if(frameTiming.renderingMsThisFrame < frameTiming.minRenderingMs) frameTiming.minRenderingMs = frameTiming.renderingMsThisFrame;
        if(frameTiming.networkWaitMsThisFrame < frameTiming.minNetworkWaitMs) frameTiming.minNetworkWaitMs = frameTiming.networkWaitMsThisFrame;
        
        // Track max values (per frame)
        if(frameTiming.aiMsThisFrame > frameTiming.maxAiMs) frameTiming.maxAiMs = frameTiming.aiMsThisFrame;
        if(frameTiming.unitsMsThisFrame > frameTiming.maxUnitsMs) frameTiming.maxUnitsMs = frameTiming.unitsMsThisFrame;
        if(frameTiming.structuresMsThisFrame > frameTiming.maxStructuresMs) frameTiming.maxStructuresMs = frameTiming.structuresMsThisFrame;
        if(frameTiming.pathfindingMsThisFrame > frameTiming.maxPathfindingMs) frameTiming.maxPathfindingMs = frameTiming.pathfindingMsThisFrame;
        if(frameTiming.renderingMsThisFrame > frameTiming.maxRenderingMs) frameTiming.maxRenderingMs = frameTiming.renderingMsThisFrame;
        if(frameTiming.networkWaitMsThisFrame > frameTiming.maxNetworkWaitMs) frameTiming.maxNetworkWaitMs = frameTiming.networkWaitMsThisFrame;
        if(frameTiming.turretScanMsThisFrame > frameTiming.maxTurretScanMs) frameTiming.maxTurretScanMs = frameTiming.turretScanMsThisFrame;
        if(frameTiming.turretScanMsThisFrame < frameTiming.minTurretScanMs && frameTiming.turretScansThisFrame > 0) frameTiming.minTurretScanMs = frameTiming.turretScanMsThisFrame;
        if(frameTiming.turretScansThisFrame > frameTiming.maxTurretScansPerFrame) frameTiming.maxTurretScansPerFrame = frameTiming.turretScansThisFrame;
        
        // Log every 2 minutes as backup (reduced from 30s; main logs happen on budget changes)
        const Uint32 now = SDL_GetTicks();
        if(now - lastTimingLogMs >= 120000) {
            logFrameTiming();
            lastTimingLogMs = now;
        }

        // Browser build: hand control back to the event loop once per frame so
        // lockstep commands and transport events keep arriving mid-game.
        yieldFrameToBrowser();

    } while (!bQuitGame && !finishedLevel);
}

void Game::initializeGameLoop() {
    if(pInterface == nullptr) {
        pInterface = std::make_unique<GameInterface>();
        if(gameState == GameState::Loading) {
            pInterface->getRadarView().setRadarMode(pLocalHouse->hasRadarOn());
        } else if(pLocalHouse->hasRadarOn()) {
            pInterface->getRadarView().switchRadarMode(true);
        }
    }

    // Configure hardware-accelerated rendering with pixel-perfect scaling
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // Use nearest-neighbor scaling for pixel-perfect look
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");       // Enable render batching for performance
    // Note: Renderer driver is set in setVideoMode(), no need to override here
    
    // Enable hardware acceleration
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    
    if(screenTexture != nullptr) {
        SDL_SetTextureScaleMode(screenTexture, SDL_ScaleModeNearest);
    }

    gameState = GameState::Running;
    for(auto& owner : house) if(owner) owner->enforceCreditCapacity();
    finishedLevel = false;
    bShowTime = winFlags & WINLOSEFLAGS_TIMEOUT;

    // Initialize DuneCity simulation
    if (citySimEnabled_ && currentGameMap != nullptr && !citySimulation_) {
        citySimulation_ = std::make_unique<DuneCity::CitySimulation>();
        citySimulation_->init(currentGameMap->getSizeX(), currentGameMap->getSizeY());
        // City Effects is implicitly on whenever the city sim itself is
        // on — the GameOptions.ini toggle is kept for explicit overrides
        // but should never silently leave a city-mode game with the
        // pollution/land-value/crime/growth pipeline disabled.
        citySimulation_->setCityEffectsEnabled(true);
    }

    // Check if a player has lost
    for(int j = 0; j < NUM_HOUSES; j++) {
        if(house[j] != nullptr && !house[j]->isAlive()) {
            house[j]->lose(true);
        }
    }

    initializeReplay();
    initializeNetwork();
    musicPlayer->changeMusic(MUSIC_PEACE);
}

void Game::renderFrame() {
    const Uint64 renderStart = SDL_GetPerformanceCounter();
    
    SDL_SetRenderTarget(renderer, screenTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    
    drawScreen();
    
    // Copy to main screen and present in one step
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_RenderCopy(renderer, screenTexture, nullptr, nullptr);
    // Menus use the arrow without discarding the pending gameplay command.
    const bool modalOpen = pInGameMenu || pInGameMentat || pWaitingForOtherPlayers;
    presentWithCursor(modalOpen ? CursorMode_Normal : currentCursorMode, !modalOpen);
    
    const Uint64 renderEnd = SDL_GetPerformanceCounter();
    const double renderMs = getElapsedMs(renderStart, renderEnd);
    frameTiming.renderingMs += renderMs;
    frameTiming.renderingMsThisFrame += renderMs;
}

void Game::processInput() {
    // Process all input through doInput() which handles both menu and game input
    doInput();
    
    // Handle menu state changes after input processing
    if(pInGameMenu != nullptr) {
        if(bMenu == false) {
            pInGameMenu.reset();
            resumeGame();
        }
        return;
    } else if(pInGameMentat != nullptr) {
        if(bMenu == false) {
            pInGameMentat.reset();
            resumeGame();
        }
        return;
    } else if(pWaitingForOtherPlayers != nullptr) {
        if(bMenu == false) {
            pWaitingForOtherPlayers.reset();
        }
        return;
    }

    // Only update interface and network if no menu is active
    pInterface->updateObjectInterface();

    if(pNetworkManager != nullptr && bSelectionChanged) {
        pNetworkManager->sendSelectedList(selectedList);
        bSelectionChanged = false;
    }
}

void Game::updateGameState() {
    if(isGamePaused()) {
        return;
    }

    if(isSpectating() && !observerCyclePrepared) return;
    if(pNetworkManager && pNetworkManager->isServer() && pNetworkManager->hasObserverStreams()) {
        OMemoryStream frame; frame.open(); frame.writeUint32(negotiatedBudget);
        std::string fingerprint;
        if(gameCycleCount%GameStateDigest::kDigestIntervalCycles==0) {
            Uint8 encoded[GameStateDigest::kEncodedSize]; GameStateDigest::encode(computeStateDigest(),encoded);
            fingerprint.assign(reinterpret_cast<const char*>(encoded),sizeof(encoded));
        }
        frame.writeString(fingerprint);
        const auto& commands=cmdManager.commandsAt(gameCycleCount);
        frame.writeUint32(commands.size()); for(const auto& command : commands) command.save(frame);
        pNetworkManager->publishObserverCycle(gameCycleCount,std::string(frame.getData(),frame.getDataLength()));
    }
    observerCyclePrepared=false;
    pInterface->getRadarView().update();
    cmdManager.executeCommands(gameCycleCount);

    // Time AI/house updates (this is where QuantBot and other AI runs).
    // Track per-house worst case so a frame-spike log can name the
    // offending player if one house ate most of the AI budget.
    Uint64 aiStart = SDL_GetPerformanceCounter();
    double worstHouseMs = 0.0;
    int worstHouseIdx = -1;
    for(int i = 0; i < NUM_HOUSES; i++) {
        if(house[i] != nullptr) {
            const Uint64 houseStart = SDL_GetPerformanceCounter();
            house[i]->update();
            const Uint64 houseEnd = SDL_GetPerformanceCounter();
            const double houseMs = getElapsedMs(houseStart, houseEnd);
            AITelemetry::log().performance(gameCycleCount,i,"house.update",static_cast<int64_t>(houseMs*1000));
            if(houseMs > worstHouseMs) {
                worstHouseMs = houseMs;
                worstHouseIdx = i;
            }
            // Log any single-house update that took > 10ms — that's already
            // half a 60fps frame budget gone to one AI.
            if(houseMs > 10.0) {
                logPerformance("[AI SPIKE] Cycle %u house=%d update=%.1fms",
                    gameCycleCount, i, houseMs);
            }
        }
    }
    Uint64 aiEnd = SDL_GetPerformanceCounter();
    const double aiMs = getElapsedMs(aiStart, aiEnd);
    frameTiming.aiMs += aiMs;
    frameTiming.aiMsThisFrame += aiMs;
    if (worstHouseMs > frameTiming.aiWorstHouseMsThisFrame) {
        frameTiming.aiWorstHouseMsThisFrame = worstHouseMs;
        frameTiming.aiWorstHouseIdxThisFrame = worstHouseIdx;
    }

    screenborder->update();
    triggerManager.trigger(gameCycleCount);

    // Units hidden inside nothing are put back on the map here, on a fixed cadence read only from
    // the cycle counter: a continuous match, a loaded save, a restored observer and a replay all
    // reconcile at the same cycles, whatever the worker count is.
    if(gameCycleCount > 0 && (gameCycleCount % transportContainmentIntervalCycles) == 0) {
        reconcileTransportContainment();
    }

    processObjects();

    // DuneCity: advance one phase of the city simulation
    if (citySimEnabled_ && citySimulation_) {
        try {
            const Uint64 citySimStart = SDL_GetPerformanceCounter();
            citySimulation_->advancePhase(gameCycleCount);
            const Uint64 citySimEnd = SDL_GetPerformanceCounter();
            const double citySimMs = getElapsedMs(citySimStart, citySimEnd);
            frameTiming.citySimMsThisFrame += citySimMs;
            if(citySimMs > 10.0) {
                logPerformance("[CITYSIM SPIKE] Cycle %u advancePhase=%.1fms",
                    gameCycleCount, citySimMs);
            }
        } catch(const std::exception& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "Game::updateGameState: citySim advancePhase threw (%s) at cycle=%u",
                e.what(), gameCycleCount);
        } catch(...) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "Game::updateGameState: citySim advancePhase threw unknown exception at cycle=%u",
                gameCycleCount);
        }

        // Power shortage warning: notify player when zones lose power
        if (citySimulation_->isPowerShortageJustStarted()) {
            int32_t unpowered = citySimulation_->getUnpoweredZoneCount();
            currentGame->addToNewsTicker(fmt::sprintf(_("WARNING: Power shortage! %d zones unpowered"), unpowered));
        }

        if ((winFlags & WINLOSEFLAGS_ECONOMIC) && !finished) {
            int32_t threshold = citySimulation_->getEconomicVictoryThreshold();
            if (threshold > 0 && citySimulation_->getTotalPop() >= threshold) {
                setGameWon();
            }
        }

        // Ambient aircraft spawning — every ~10s (625 cycles at 62.5Hz),
        // deterministically check each Airport for spawning an airplane or
        // helicopter.  Cap at 3 ambient units per house.
        static constexpr Uint32 kAmbientSpawnInterval = 625;
        if (gameCycleCount > 0 && (gameCycleCount % kAmbientSpawnInterval) == 0) {
            for (StructureBase* pStruct : structureList) {
                if (pStruct->getItemID() != Structure_Airport) continue;

                House* pOwner = pStruct->getOwner();
                if (!pOwner) continue;

                // Count existing ambient units for this house
                int ambientCount = pOwner->getNumItems(Unit_AmbientAirplane)
                                 + pOwner->getNumItems(Unit_AmbientHelicopter);
                if (ambientCount >= 3) continue;

                // Deterministic "random" choice based on cycle + airport position
                const Coord airportPos = pStruct->getLocation();
                Uint32 seed = gameCycleCount * 7u
                            + static_cast<Uint32>(airportPos.x) * 131u
                            + static_cast<Uint32>(airportPos.y) * 257u;

                // Only spawn ~50% of the time
                if ((seed % 4u) < 2u) continue;

                bool spawnAirplane = (seed % 3u) != 0;  // 2/3 airplanes, 1/3 helicopters
                int unitType = spawnAirplane ? Unit_AmbientAirplane : Unit_AmbientHelicopter;

                UnitBase* pUnit = pOwner->createUnit(unitType);
                if (!pUnit) continue;

                Coord center = airportPos + Coord(1, 1);  // center of 3x3 airport

                if (spawnAirplane) {
                    // Airplane: spawn at a map edge, fly across the airport, exit other side
                    Coord edgeStart = currentGameMap->findClosestEdgePoint(center, Coord(1, 1));
                    pUnit->deploy(edgeStart);
                    pUnit->setDestination(center);
                    // guardPoint set to opposite edge for exit path
                    int exitX = currentGameMap->getSizeX() - 1 - edgeStart.x;
                    int exitY = currentGameMap->getSizeY() - 1 - edgeStart.y;
                    pUnit->setGuardPoint(Coord(exitX, exitY));
                    // Face toward destination
                    if (edgeStart.x == 0) pUnit->setAngle(RIGHT);
                    else if (edgeStart.x == currentGameMap->getSizeX() - 1) pUnit->setAngle(LEFT);
                    else if (edgeStart.y == 0) pUnit->setAngle(DOWN);
                    else pUnit->setAngle(UP);
                } else {
                    // Helicopter: spawn at airport, orbit nearby
                    pUnit->deploy(center);
                }
            }
        }
    }

    if((indicatorFrame != NONE_ID) && (--indicatorTimer <= 0)) {
        indicatorTimer = indicatorTime;
        if(++indicatorFrame > 2) {
            indicatorFrame = NONE_ID;
        }
    }

    gameCycleCount++;
    
    // Apply any pending budget changes (deterministic, cycle-based)
    applyPendingBudgetChanges();
    
    // PHASE 1: Cycle-based budget adjustment (deterministic, every 375 cycles ~7.5s at 50Hz)
    checkBudgetAdjustment();
    
    // MULTIPLAYER FIX (Issue #8): Cycle-based combat stats dump (deterministic)
    // Combat stats logging disabled (broken anti-air analysis)
    /*if(combatStats.lastDumpCycle == 0) {
        combatStats.lastDumpCycle = gameCycleCount;
    } else if((gameCycleCount - combatStats.lastDumpCycle) >= MILLI2CYCLES(30000)) {
        dumpCombatStats();
        combatStats.lastDumpCycle = gameCycleCount;
    }*/
    
    // MULTIPLAYER FIX (Issue #9): Cycle-based finished level timer (deterministic)
    if(finished && (gameCycleCount - finishedLevelCycle > MILLI2CYCLES(END_WAIT_TIME))) {
        finishedLevel = true;
    }
    
    if(takePeriodicalScreenshots && ((gameCycleCount % (MILLI2CYCLES(10*1000))) == 0)) {
        takeScreenshot();
    }

    // Taken here, at a precise point in the simulation: cycle gameCycleCount-1 is fully applied
    // and the counter has just become gameCycleCount. Every peer reaches this same point for
    // the same cycle, which is what makes two digests comparable at all.
    updateStateDigests();
    if(pNetworkManager && isSpectating()) pNetworkManager->observerAdvanced(gameCycleCount);

    musicPlayer->musicCheck();
}

void Game::initializeReplay() {
    if(bReplay) {
        cmdManager.setReadOnly(true);
    } else {
        char tmp[FILENAME_MAX];
        fnkdat("replay/auto.rpl", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);
        const std::string replayname(tmp);
        
        auto pStream = std::make_unique<OFileStream>();
        if(pStream->open(replayname)) {
            pStream->writeString(getLocalPlayerName());
            gameInitSettings.save(*pStream);
            cmdManager.save(*pStream);
            pStream->flush();
            cmdManager.setStream(std::move(pStream));
        } else {
            quitGame();
        }
    }
}

void Game::initializeNetwork() {
    if(pNetworkManager != nullptr) {
        pNetworkManager->beginSimulation(gameInitSettings.getRandomSeed());
        pNetworkManager->setOnReceiveChatMessage(
            std::bind(&ChatManager::addChatMessage, &(pInterface->getChatManager()), 
            std::placeholders::_1, std::placeholders::_2));
        pNetworkManager->setOnReceiveCommandList(
            std::bind(&CommandManager::addCommandList, &cmdManager, 
            std::placeholders::_1, std::placeholders::_2));
        pNetworkManager->setOnReceiveSelectionList(
            std::bind(&Game::onReceiveSelectionList, this, 
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        pNetworkManager->setOnPeerDisconnected(
            std::bind(&Game::onPeerDisconnected, this, 
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        
        // Multiplayer budget negotiation callbacks
        pNetworkManager->setOnReceiveClientStats(
            std::bind(&Game::handleClientStats, this,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, 
            std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
        pNetworkManager->setOnReceiveSetPathBudget(
            std::bind(&Game::handleSetPathBudget, this,
            std::placeholders::_1, std::placeholders::_2));

        pNetworkManager->setOnReceiveMatchControl([this](Uint32 revision, Uint32 speed, Uint32 pause, Uint32 resumed) {
            handleMatchControl(revision, speed, pause, resumed);
        });
        pNetworkManager->setOnReceiveMatchResumeRequest([this](const std::string& name, Uint32 pause) {
            handleMatchResumeRequest(name, pause);
        });
        if (pNetworkManager->isServer()) matchControl.revision = 1;

        // Deterministic state digests travel in the relay diagnostic envelope, not as a game
        // packet, so the ENet wire format and NETWORK_PROTOCOL_VERSION are untouched.
        pNetworkManager->setOnReceiveRelayDiagnostic(
            [this](const std::string& peerName, std::uint8_t kind, const std::uint8_t* payload,
                   std::size_t length) {
                if(kind != static_cast<std::uint8_t>(RoomRelay::DiagnosticKind::StateDigest)) {
                    return;
                }
                GameStateDigest::Digest digest;
                if(!GameStateDigest::decode(payload, length, digest)) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "Game: unusable state digest from '%s'", peerName.c_str());
                    return;
                }
                onRemoteStateDigest(peerName, digest);
            });

        localStateDigests.clear();
        pendingRemoteDigests.clear();
        stateDivergenceReported = false;
        lockstepStallReported = false;
        lastStateDigestCycle = 0;

        // Fix the relay allowance at match start. Moving it backwards later could put new
        // input into a cycle the other peer has already processed.
        Uint32 networkBuffer = 0;
        if(pNetworkManager->isRelayHttpPollingSession()) {
            const Uint32 relayRoundTripMs = pNetworkManager->getRelayServerRoundTripTimeMs();
            const int gameSpeedMs = getGameSpeed();
            networkBuffer = CommandBufferPolicy::relayCommandBufferCycles(relayRoundTripMs, gameSpeedMs);
            SDL_Log("Network buffer set to %u cycles (relay hop: %ums%s, allowance: %ums, %dms per cycle)",
                    static_cast<unsigned>(networkBuffer), static_cast<unsigned>(relayRoundTripMs),
                    relayRoundTripMs == 0 ? " - not sampled yet" : "",
                    static_cast<unsigned>(networkBuffer * gameSpeedMs), gameSpeedMs);
        } else {
            // Preserve the existing ENet and WebSocket RTT formula and Internet minimum.
            const int rttBuffer = MILLI2CYCLES(pNetworkManager->getMaxPeerRoundTripTime()) + 5;
            const bool isLAN = pNetworkManager->isLANServer();
            networkBuffer = isLAN ? rttBuffer : std::max(rttBuffer, 10);
            SDL_Log("Network buffer set to %u cycles (RTT: %dms, %s)",
                    static_cast<unsigned>(networkBuffer), pNetworkManager->getMaxPeerRoundTripTime(),
                    isLAN ? "LAN" : "Internet");
        }

        cmdManager.setNetworkCycleBuffer(networkBuffer);
        if(isSpectating()) {
            loadObserverRuntime(pNetworkManager->takeObserverRuntime());
            pNetworkManager->observerLoaded(gameCycleCount);
        }
    }
}


void Game::resumeGame()
{
    bMenu = false;
    // Only undo the pause this host menu requested. A pre-existing pause, or a
    // later manual pause from another player, keeps its explicit Resume action.
    if(pNetworkManager != nullptr) {
        if(pNetworkManager->isServer() && menuPause) {
            menuPause->closed = true;
            if(menuPause->pauseCycle != 0) {
                const auto cycle = menuPause->pauseCycle;
                menuPause.reset();
                if(matchControl.pauseCycle == cycle) handleMatchResumeRequest(localPlayerName, cycle);
            }
            // If the command is still in flight, executeMatchPause releases it.
        }
        return;
    }
    if(bPause && settings.general.diagnosticLogs) {
        AITelemetry::log().write(gameCycleCount,-1,-1,"pause_changed",
            AITelemetry::Record().set("paused",false).set("source","resume"));
    }
    bPause = false;
    

}

void Game::pauseGame(const char* source) {
    // Menus stay local in multiplayer. The explicit Pause button uses lockstep
    // to stop the match and a host control message to resume it.
    if(pNetworkManager != nullptr) {
        return;
    }
    if(!bPause && settings.general.diagnosticLogs) {
        AITelemetry::log().write(gameCycleCount,-1,-1,"pause_changed",
            AITelemetry::Record().set("paused",true).set("source",source).set("menu_open",bMenu));
    }
    bPause = true;
    

}

void Game::logFrameTiming() {
    if(frameTiming.frameCount <= 0) {
        return;
    }
    if(bPause) {
        return;
    }

    const double avgAi = frameTiming.aiMs / frameTiming.frameCount;
    const double avgUnits = frameTiming.unitsMs / frameTiming.frameCount;
    const double avgStructures = frameTiming.structuresMs / frameTiming.frameCount;
    const double avgPathfinding = frameTiming.pathfindingMs / frameTiming.frameCount;
    const double avgNetworkWait = frameTiming.networkWaitMs / frameTiming.frameCount;
    const double avgRendering = frameTiming.renderingMs / frameTiming.frameCount;
    const double avgTotal = frameTiming.totalMs / frameTiming.frameCount;
    const double avgFps = avgTotal > 0.0 ? 1000.0 / avgTotal : 0.0;
    const double maxFps = frameTiming.maxTotalMs > 0.0 ? 1000.0 / frameTiming.maxTotalMs : 0.0;
    const double avgGameCycles = static_cast<double>(frameTiming.totalGameCycles) / frameTiming.frameCount;
    
    // Pathfinding detailed stats
    const double avgPathsPerFrame = static_cast<double>(frameTiming.totalPathsProcessed) / frameTiming.frameCount;
    const double avgPathsPerCycle = frameTiming.totalGameCycles > 0 ? 
        static_cast<double>(frameTiming.totalPathsProcessed) / frameTiming.totalGameCycles : 0.0;
    const double avgPathfindingPerCycle = frameTiming.totalGameCycles > 0 ? 
        avgPathfinding / avgGameCycles : 0.0;
    const double avgMsPerPath = frameTiming.totalPathsProcessed > 0 ? 
        avgPathfinding / avgPathsPerFrame : 0.0;

    const int mapSize = (currentGameMap != nullptr) ? currentGameMap->getSizeX() : 0;
    
    logPerformance("[Performance] === AVERAGES over %d frames ===", frameTiming.frameCount);
    logPerformance("[Performance] Map: %dx%d | Units: %d", mapSize, mapSize, frameTiming.unitCount);
    logPerformance("[Performance] FPS: %.1f | Frame: %.2fms",
        avgFps, avgTotal);
    logPerformance("[Performance] GameCycles/Frame: min=%d avg=%.1f max=%d",
        frameTiming.minGameCyclesPerFrame, avgGameCycles, frameTiming.maxGameCyclesPerFrame);
    logPerformance("[Performance] AI:         min=%.2fms avg=%.2fms max=%.2fms",
        frameTiming.minAiMs, avgAi, frameTiming.maxAiMs);
    logPerformance("[Performance] Units:      min=%.2fms avg=%.2fms max=%.2fms",
        frameTiming.minUnitsMs, avgUnits, frameTiming.maxUnitsMs);
    
    // Detailed unit breakdown
    const double avgTargeting = frameTiming.unitTargetingMs / frameTiming.frameCount;
    const double avgNavigate = frameTiming.unitNavigateMs / frameTiming.frameCount;
    const double avgMove = frameTiming.unitMoveMs / frameTiming.frameCount;
    const double avgTurn = frameTiming.unitTurnMs / frameTiming.frameCount;
    const double avgVisibility = frameTiming.unitVisibilityMs / frameTiming.frameCount;
    const double unitsTotal = avgTargeting + avgNavigate + avgMove + avgTurn + avgVisibility;
    logPerformance("[Performance]   ↳ Targeting:   %.2fms (%.1f%%)", avgTargeting, unitsTotal > 0 ? (avgTargeting/unitsTotal*100) : 0);
    logPerformance("[Performance]   ↳ Navigate:    %.2fms (%.1f%%)", avgNavigate, unitsTotal > 0 ? (avgNavigate/unitsTotal*100) : 0);
    logPerformance("[Performance]   ↳ Move:        %.2fms (%.1f%%)", avgMove, unitsTotal > 0 ? (avgMove/unitsTotal*100) : 0);
    logPerformance("[Performance]   ↳ Turn:        %.2fms (%.1f%%)", avgTurn, unitsTotal > 0 ? (avgTurn/unitsTotal*100) : 0);
    logPerformance("[Performance]   ↳ Visibility:  %.2fms (%.1f%%)", avgVisibility, unitsTotal > 0 ? (avgVisibility/unitsTotal*100) : 0);
    
    logPerformance("[Performance] Structures: min=%.2fms avg=%.2fms max=%.2fms",
        frameTiming.minStructuresMs, avgStructures, frameTiming.maxStructuresMs);
    logPerformance("[Performance] Pathfinding: min=%.2fms avg=%.2fms max=%.2fms",
        frameTiming.minPathfindingMs, avgPathfinding, frameTiming.maxPathfindingMs);
    logPerformance("[Performance] NetworkWait: min=%.2fms avg=%.2fms max=%.2fms",
        frameTiming.minNetworkWaitMs, avgNetworkWait, frameTiming.maxNetworkWaitMs);
    logPerformance("[Performance] Rendering:  min=%.2fms avg=%.2fms max=%.2fms",
        frameTiming.minRenderingMs, avgRendering, frameTiming.maxRenderingMs);
    logPerformance("[Performance] Pathfinding Detail: %.1f paths/frame | %.2f paths/cycle | %.2fms/cycle | %.2fms/path",
        avgPathsPerFrame, avgPathsPerCycle, avgPathfindingPerCycle, avgMsPerPath);
    
    // Turret scan detailed stats
    const double avgTurretScans = static_cast<double>(frameTiming.totalTurretScans) / frameTiming.frameCount;
    const double avgTurretScanMs = frameTiming.turretScanMs / frameTiming.frameCount;
    const double avgMsPerTurretScan = frameTiming.totalTurretScans > 0 ? 
        frameTiming.turretScanMs / frameTiming.totalTurretScans : 0.0;
    logPerformance("[Performance] Turret Scans: %.1f scans/frame | %.2fms total/frame | %.4fms/scan",
        avgTurretScans, avgTurretScanMs, avgMsPerTurretScan);
    logPerformance("[Performance] Turret Scan Range: min=%.2fms max=%.2fms | Peak: %d scans/frame",
        frameTiming.minTurretScanMs, frameTiming.maxTurretScanMs, frameTiming.maxTurretScansPerFrame);
    
    // Phase 1: Token/node statistics
    const double avgTokensPerFrame = frameTiming.frameCount > 0 ? 
        static_cast<double>(frameTiming.totalPathTokens) / frameTiming.frameCount : 0.0;
    logPerformance("[Performance] === TOKEN STATISTICS (Phase 1) ===");
    logPerformance("[Performance] Tokens/Frame: avg=%.1f max=%zu total=%zu",
        avgTokensPerFrame, frameTiming.maxPathTokensPerFrame, frameTiming.totalPathTokens);
    logPerformance("[Performance] Tokens/Cycle: avg=%.1f±%.1f max=%zu",
        frameTiming.pathTokensPerCycleStats.mean, 
        frameTiming.pathTokensPerCycleStats.ci95(),
        frameTiming.maxPathTokensPerCycle);
    logPerformance("[Performance] Paths/Cycle: avg=%.2f±%.2f",
        frameTiming.pathsPerCycleStats.mean,
        frameTiming.pathsPerCycleStats.ci95());
    
    // Token distribution per path
    if(frameTiming.tokensPerCompletedPathStats.sampleCount > 0) {
        logPerformance("[Performance] Tokens/CompletedPath: avg=%.1f±%.1f min=%zu max=%zu (n=%zu)",
            frameTiming.tokensPerCompletedPathStats.mean,
            frameTiming.tokensPerCompletedPathStats.ci95(),
            frameTiming.minTokensPerCompletedPath,
            frameTiming.maxTokensPerCompletedPath,
            frameTiming.tokensPerCompletedPathStats.sampleCount);
    }
    if(frameTiming.tokensPerFailedPathStats.sampleCount > 0) {
        logPerformance("[Performance] Tokens/FailedPath: avg=%.1f±%.1f min=%zu max=%zu (n=%zu)",
            frameTiming.tokensPerFailedPathStats.mean,
            frameTiming.tokensPerFailedPathStats.ci95(),
            frameTiming.minTokensPerFailedPath,
            frameTiming.maxTokensPerFailedPath,
            frameTiming.tokensPerFailedPathStats.sampleCount);
    }
    
    // Path completion rates
    const double pathCompletionRate = frameTiming.totalPathsProcessed > 0 ?
        (100.0 * (frameTiming.totalPathsProcessed - frameTiming.totalPathsFailed) / frameTiming.totalPathsProcessed) : 0.0;
    logPerformance("[Performance] Path Completion: %.1f%% (%d completed, %d failed)",
        pathCompletionRate,
        frameTiming.totalPathsProcessed - frameTiming.totalPathsFailed,
        frameTiming.totalPathsFailed);
    
    // Token histogram
    logPerformance("[Performance] Token Histogram: <512:%zu 512-1k:%zu 1k-2k:%zu 2k-4k:%zu 4k-8k:%zu 8k-16k:%zu 16k+:%zu",
        frameTiming.pathTokenHistogram[0],
        frameTiming.pathTokenHistogram[1],
        frameTiming.pathTokenHistogram[2],
        frameTiming.pathTokenHistogram[3],
        frameTiming.pathTokenHistogram[4],
        frameTiming.pathTokenHistogram[5],
        frameTiming.pathTokenHistogram[6]);
    
    // Queue and budget stats
    logPerformance("[Performance] Queue: maxDepth=%d | BudgetStarvedFrames=%d",
        frameTiming.maxPathQueueLength,
        frameTiming.pathBudgetStarvedFrames);
    
    // Phase 2: Token budget statistics
    const double tokensUsedPerCycle = frameTiming.totalGameCycles > 0 ?
        static_cast<double>(frameTiming.totalPathTokens) / frameTiming.totalGameCycles : 0.0;
    const double tokenBudgetUtilization = tokensUsedPerCycle / negotiatedBudget * 100.0;
    logPerformance("[Performance] === TOKEN BUDGET (Phase 1 - Negotiated) ===");
    logPerformance("[Performance] Budget: %zu tokens/cycle | Avg Used: %.0f (%.1f%% utilization) | Carry-Over: %zu",
        negotiatedBudget, tokensUsedPerCycle, tokenBudgetUtilization, carryOverTokens);
    logPerformance("[Performance] Token Budget Exhausted: %d times | Time Budget Exceeded: %d times (safety valve)",
        frameTiming.tokenBudgetExhaustedCount,
        frameTiming.timeBudgetExceededCount);
    
    // Token-to-time correlation
    const double tokensPerMs = avgPathfindingPerCycle > 0.0 ? 
        tokensUsedPerCycle / avgPathfindingPerCycle : 0.0;
    logPerformance("[Performance] Tokens/Ms: %.0f (measured correlation)", tokensPerMs);
    
    logPerformance("[Performance] === PEAKS (worst case) ===");
    logPerformance("[Performance] FPS: %.1f | Frame: %.2fms | AI: %.2fms | Units: %.2fms | Structures: %.2fms | Pathfinding: %.2fms | NetworkWait: %.2fms | Rendering: %.2fms",
        maxFps, frameTiming.maxTotalMs,
        frameTiming.maxAiMs, frameTiming.maxUnitsMs, frameTiming.maxStructuresMs, 
        frameTiming.maxPathfindingMs, frameTiming.maxNetworkWaitMs, frameTiming.maxRenderingMs);
    logPerformance("[Performance] Pathfinding Peaks: %d paths/frame | %d paths/cycle | %.2fms/cycle",
        frameTiming.maxPathsPerFrame, frameTiming.maxPathsPerCycle, frameTiming.maxPathfindingMsPerCycle);

    // Reset counters
    frameTiming.aiMs = 0.0;
    frameTiming.unitsMs = 0.0;
    frameTiming.structuresMs = 0.0;
    frameTiming.pathfindingMs = 0.0;
    frameTiming.networkWaitMs = 0.0;
    frameTiming.renderingMs = 0.0;
    frameTiming.totalMs = 0.0;
    frameTiming.totalGameCycles = 0;
    frameTiming.totalPathsProcessed = 0;
    frameTiming.totalTurretScans = 0;
    frameTiming.turretScanMs = 0.0;
    frameTiming.frameCount = 0;
    frameTiming.maxAiMs = 0.0;
    frameTiming.maxUnitsMs = 0.0;
    frameTiming.maxStructuresMs = 0.0;
    frameTiming.maxPathfindingMs = 0.0;
    frameTiming.maxPathfindingMsPerCycle = 0.0;
    frameTiming.maxNetworkWaitMs = 0.0;
    frameTiming.maxRenderingMs = 0.0;
    frameTiming.maxTotalMs = 0.0;
    frameTiming.maxTurretScanMs = 0.0;
    frameTiming.maxTurretScansPerFrame = 0;
    frameTiming.minTurretScanMs = 999999.0;
    frameTiming.maxGameCyclesPerFrame = 0;
    frameTiming.maxPathsPerCycle = 0;
    frameTiming.maxPathsPerFrame = 0;
    frameTiming.minGameCyclesPerFrame = 999999;
    frameTiming.minAiMs = 999999.0;
    frameTiming.minUnitsMs = 999999.0;
    frameTiming.minStructuresMs = 999999.0;
    frameTiming.minPathfindingMs = 999999.0;
    frameTiming.minNetworkWaitMs = 999999.0;
    frameTiming.minRenderingMs = 999999.0;
    
    // Reset unit breakdown
    frameTiming.unitTargetingMs = 0.0;
    frameTiming.unitNavigateMs = 0.0;
    frameTiming.unitMoveMs = 0.0;
    frameTiming.unitTurnMs = 0.0;
    frameTiming.unitVisibilityMs = 0.0;
    
    // Reset Phase 1 token stats
    frameTiming.totalPathTokens = 0;
    frameTiming.maxPathTokensPerCycle = 0;
    frameTiming.maxPathTokensPerFrame = 0;
    frameTiming.pathsPerCycleStats.reset();
    frameTiming.pathTokensPerCycleStats.reset();
    frameTiming.tokensPerCompletedPathStats.reset();
    frameTiming.tokensPerFailedPathStats.reset();
    frameTiming.minTokensPerCompletedPath = SIZE_MAX;
    frameTiming.maxTokensPerCompletedPath = 0;
    frameTiming.minTokensPerFailedPath = SIZE_MAX;
    frameTiming.maxTokensPerFailedPath = 0;
    
    // Reset Phase 2 token budget stats
    frameTiming.tokenBudgetExhaustedCount = 0;
    frameTiming.timeBudgetExceededCount = 0;
    frameTiming.pathTokenHistogram = {};
    frameTiming.maxPathQueueLength = 0;
    frameTiming.pathBudgetStarvedFrames = 0;
    frameTiming.totalPathsFailed = 0;
}

void Game::onOptions()
{
    if(bReplay == true) {
        // don't show menu
        quitGame();
    } else {
        // The host stepping into the menu stops the match for everyone; every other
        // player and spectator keeps the match running while their menu is open.
        // Only ever a pause request: an explicit shared pause is never lifted here.
        if(pNetworkManager && pNetworkManager->isServer() && !isGamePaused()
           && !pauseRequestPending && canToggleMatchPause()) {
            menuPause = MenuPause{gameCycleCount};
            toggleMatchPause();
        }
        if(menuPause) menuPause->closed = false;
        Uint32 color = getHouseColorRGB(getHouseVisualHouse(pLocalHouse->getHouseID()), 3);
        pInGameMenu = std::make_unique<InGameMenu>((isNetworkGameType(gameType)), color);
        bMenu = true;
        pauseGame("options");
    }
}

void Game::onJoinRequests() {
    if(!pNetworkManager || !pNetworkManager->getDirectTransport()
       || pNetworkManager->lateJoinPaused() || pInGameMenu || pInGameMentat) return;
    if(pNetworkManager->isServer()) {
        pInGameMenu = std::make_unique<JoinRequestsWindow>();
        bMenu = true; // Choosing a controller does not pause an online game.
    } else if(isSpectating()) {
        onOptions();
    }
}

void Game::cycleDune2RZoom() {
    if(!ModManager::instance().isInitialized()
       || ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) != "Dune2R") {
        return;
    }

    // Action uses the closest existing renderer level; each press pulls back
    // through Tactical and Strategic before returning to Action.
    const int nextZoomLevel = currentZoomlevel == 2 ? 1
                            : currentZoomlevel == 1 ? 0
                            : 2;
    applyDune2RZoom(nextZoomLevel);
}

void Game::toggleDune2RVisuals() {
    if(!ModManager::instance().isInitialized()
       || ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) != "Dune2R") {
        return;
    }
    pGFXManager->toggleDune2RVisuals();
    addToNewsTicker(pGFXManager->isDune2RVisualsEnabled()
        ? "Dune2R visuals enabled"
        : "Classic visuals enabled");
}

void Game::applyDune2RZoom(int zoomLevel) {
    if(!ModManager::instance().isInitialized()
       || ModManager::instance().getContentBase(ModManager::instance().getActiveModName()) != "Dune2R") {
        return;
    }

    const int clampedZoomLevel = std::max(0, std::min(2, zoomLevel));
    if(clampedZoomLevel == currentZoomlevel) {
        return;
    }

    const Coord oldCenterCoord = screenborder->getCurrentCenter();
    currentZoomlevel = clampedZoomLevel;
    screenborder->adjustScreenBorderToMapsize(currentGameMap->getSizeX(), currentGameMap->getSizeY());
    screenborder->setNewScreenCenter(oldCenterCoord);

    const char* viewName = currentZoomlevel == 2 ? "Action"
                         : currentZoomlevel == 1 ? "Tactical"
                         : "Strategic";
    addToNewsTicker(fmt::sprintf("Dune2R view: %s (%dx)", viewName, currentZoomlevel + 1));
}


void Game::onMentat()
{
    pInGameMentat = std::make_unique<MentatHelp>(pLocalHouse->getHouseID(), techLevel, gameInitSettings.getMission());
    bMenu = true;
    pauseGame("mentat");
}

bool Game::canSkipMission() const {
    return !isSpectating() && !bReplay && !finished && !bQuitGame && pLocalPlayer && pLocalHouse
        && CampaignControls::maySkip(gameInitSettings.getGameType(), gameInitSettings.getHouseID(),
                                     pLocalHouse->getHouseID(), true);
}

void Game::onSkipMission() {
    if(!canSkipMission())return;
    class Confirmation : public Window {
    public:
        Confirmation() : Window(0, 0, getRendererWidth(), getRendererHeight()) {
            setTransparentBackground(true);
            openWindow(QstBox::create(_("Skip this mission and continue to the next level?"),
                                     _("Skip mission"), _("Cancel"), QSTBOX_BUTTON2));
        }
        void onChildWindowClose(Window* child) override {
            const auto* question = dynamic_cast<QstBox*>(child);
            if(question && question->getPressedButtonID() == QSTBOX_BUTTON1)
                currentGame->confirmSkipMission();
            currentGame->resumeGame();
        }
        void handleInput(SDL_Event& event) override {
            if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                currentGame->resumeGame();
                return;
            }
            Window::handleInput(event);
        }
    };
    pInGameMenu=std::make_unique<Confirmation>(); bMenu=true; pauseGame("skip_mission");
}

void Game::confirmSkipMission() {
    if(canSkipMission()) cmdManager.addCommand(Command(pLocalPlayer->getPlayerID(), CMD_CAMPAIGN_SKIP));
}

void Game::onFeedback() {
    pInGameMenu = std::make_unique<FeedbackWindow>();
    bMenu = true;
    pauseGame("feedback");
}

void Game::onCityBudget()
{
    if (!citySimulation_ || !citySimulation_->isInitialized()) {
        return;
    }
    pInterface->openWindow(CityBudgetWindow::create());
}


GameInitSettings Game::getNextGameInitSettings()
{
    if(nextGameInitSettings.getGameType() != GameType::Invalid) {
        // return the prepared game init settings (load game or restart mission)
        return nextGameInitSettings;
    }

    switch(gameInitSettings.getGameType()) {
        case GameType::CampaignCoop:
        case GameType::Campaign: {
            int currentMission = gameInitSettings.getMission();
            if(!won) {
                currentMission -= (currentMission >= 22) ? 1 : 3;
            }
            int nextMission = gameInitSettings.getMission();
            Uint32 alreadyPlayedRegions = gameInitSettings.getAlreadyPlayedRegions();
            if(currentMission >= -1) {
                // do map choice
                SDL_Log("Map Choice...");
                MapChoice mapChoice(gameInitSettings.getHouseID(), currentMission, alreadyPlayedRegions);
                mapChoice.showMenu();
                nextMission = mapChoice.getSelectedMission();
                alreadyPlayedRegions = mapChoice.getAlreadyPlayedRegions();
            }

            Uint32 alreadyShownTutorialHints = won ? pLocalPlayer->getAlreadyShownTutorialHints() : gameInitSettings.getAlreadyShownTutorialHints();
            GameInitSettings next(gameInitSettings, nextMission, alreadyPlayedRegions, alreadyShownTutorialHints);
            if(next.getGameType() == GameType::CampaignCoop) {
                auto file = pFileManager->openCampaignFile(next.getFilename());
                const auto size = SDL_RWsize(file.get());
                if(size <= 0) THROW(std::runtime_error, "Cannot read next co-op mission.");
                std::string data(static_cast<size_t>(size), '\0');
                if(SDL_RWread(file.get(), data.data(), 1, data.size()) != data.size())
                    THROW(std::runtime_error, "Incomplete next co-op mission.");
                next.setScenarioData(data);
            }
            return next;
        } break;

        default: {
            SDL_Log("Game::getNextGameInitClass(): Wrong gameType for next Game.");
            return GameInitSettings();
        } break;
    }
}


int Game::whatNext()
{
    if(whatNextParam != GAME_NOTHING) {
        int tmp = whatNextParam;
        whatNextParam = GAME_NOTHING;
        return tmp;
    }

    if(nextGameInitSettings.getGameType() != GameType::Invalid) {
        return GAME_LOAD;
    }

    switch(gameType) {
        case GameType::CampaignCoop:
        case GameType::Campaign: {
            if(bQuitGame == true) {
                return GAME_RETURN_TO_MENU;
            } else if(won == true) {
                if(gameInitSettings.getMission() == 22) {
                    // there is no mission after this mission
                    whatNextParam = GAME_RETURN_TO_MENU;
                } else {
                    // there is a mission after this mission
                    whatNextParam = GAME_NEXTMISSION;
                }
                return GAME_DEBRIEFING_WIN;
            } else {
                // we need to play this mission again
                whatNextParam = GAME_NEXTMISSION;

                return GAME_DEBRIEFING_LOST;
            }
        } break;

        case GameType::SkirmishCoop:
        case GameType::Skirmish: {
            if(bQuitGame == true) {
                return GAME_RETURN_TO_MENU;
            } else if(won == true) {
                whatNextParam = GAME_RETURN_TO_MENU;
                return GAME_DEBRIEFING_WIN;
            } else {
                whatNextParam = GAME_RETURN_TO_MENU;
                return GAME_DEBRIEFING_LOST;
            }
        } break;

        case GameType::CustomGame:
        case GameType::CustomMultiplayer: {
            if(bQuitGame == true) {
                return GAME_RETURN_TO_MENU;
            } else {
                whatNextParam = GAME_RETURN_TO_MENU;
                return GAME_CUSTOM_GAME_STATS;
            }
        } break;

        default: {
            return GAME_RETURN_TO_MENU;
        } break;
    }
}


bool Game::loadSaveGame(const std::string& filename) {
    IFileStream fs;

    if(fs.open(filename) == false) {
        return false;
    }

    bool ret = loadSaveGame(fs);

    fs.close();

    return ret;
}

bool Game::loadSaveGame(InputStream& stream) {
    gameState = GameState::Loading;

    targetRequestQueue.clear();
    pendingTargetRequestIds.clear();
    pathRequestQueue.clear();
    pendingPathRequestIds.clear();
    // Suspended searches hold scratch buffers and a unit id; neither may outlive
    // the match they belong to. Dropping them is a cancellation, and counting it
    // keeps the request-lifecycle conservation check honest across a load.
    pathRequestsCancelled += activePathJobs.size();
    activePathJobs.clear();
    invalidateCarryallCandidateIds();

    const char* loadStage = "header";
    auto logLoadStage = [&stream, &loadStage](const char* stage) {
        loadStage = stage;
        if(const auto* fileStream = dynamic_cast<const IFileStream*>(&stream)) {
            SDL_Log("[SaveLoad] stage=%s offset=%ld/%ld",
                    loadStage, fileStream->getPosition(), fileStream->getLength());
        } else {
            SDL_Log("[SaveLoad] stage=%s offset=unavailable", loadStage);
        }
    };

    try {
    logLoadStage("header");

    Uint32 magicNum = stream.readUint32();
    if (magicNum != SAVEMAGIC) {
        SDL_Log("Game::loadSaveGame(): No valid savegame! Expected magic number %.8X, but got %.8X!", SAVEMAGIC, magicNum);
        return false;
    }

    Uint32 savegameVersion = stream.readUint32();
    
    // Support backward compatibility with version 9705 (pre-Original AI)
    constexpr Uint32 MINIMUM_SUPPORTED_VERSION = 9705;
    if (savegameVersion < MINIMUM_SUPPORTED_VERSION || savegameVersion > SAVEGAMEVERSION) {
        SDL_Log("Game::loadSaveGame(): No valid savegame! Expected savegame version %d-%d, but got %d!", 
                MINIMUM_SUPPORTED_VERSION, SAVEGAMEVERSION, savegameVersion);
        return false;
    }
    
    // Store version for backward-compatible loading
    this->loadedSavegameVersion = savegameVersion;

    std::string duneVersion = stream.readString();

    // Read mod info (version 9806+)
    logLoadStage("mod metadata");
    std::string savedModName = "vanilla";
    std::string savedModChecksum = "";
    if (savegameVersion >= 9806) {
        savedModName = stream.readString();
        savedModChecksum = stream.readString();

    }

    // if this is a multiplayer load we need to save some information before we overwrite gameInitSettings with the settings saved in the savegame
    const bool lateJoinLoad=pNetworkManager && pNetworkManager->lateJoinLoading();
    const std::string expectedModRevision = gameInitSettings.getModRevisionHash();
    const bool bCoopLoad = gameInitSettings.getGameType() == GameType::LoadCoop;
    const std::string coopServer = gameInitSettings.getServername();
    bool bMultiplayerLoad = (gameInitSettings.getGameType() == GameType::LoadMultiplayer || bCoopLoad);
    GameInitSettings::HouseInfoList oldHouseInfoList = gameInitSettings.getHouseInfoList();

    // read gameInitSettings
    logLoadStage("game settings");
    gameInitSettings = GameInitSettings(stream);
    if(lateJoinLoad && !expectedModRevision.empty() && gameInitSettings.getModRevisionHash() != expectedModRevision)
        THROW(std::runtime_error, "The checkpoint requires a different mod revision from the advertised game.");
    // MOD4 identifies the complete immutable package. Older saves retain their legacy
    // checksum contract, but a mismatch must never silently load different rules.
    if(gameInitSettings.getModRevisionHash().empty() && savegameVersion >= 9806)
        gameInitSettings.setModIdentity(savedModName, savedModChecksum);
    WorkshopGameContent::resolveMod(gameInitSettings, !lateJoinLoad);
    citySimEnabled_ = ModManager::instance().isCityModeActive();
    if(savegameVersion <= 9820) {
        gameInitSettings.migrateLegacyHouseColorSlots();
    }

    const bool savedNetworkLayout = isNetworkGameType(gameInitSettings.getGameType());

    // read the actual house setup choosen at the beginning of the game
    logLoadStage("house setup");
    Uint32 numHouseInfo = stream.readUint32();
    for(Uint32 i=0;i<numHouseInfo;i++) {
        houseInfoListSetup.push_back(GameInitSettings::HouseInfo(stream));
    }

    if(savegameVersion >= 9814) {
        const Uint32 setupColorMarker = stream.readUint32();
        if(setupColorMarker != SAVE_SETUP_COLOR_MARKER) {
            SDL_Log("Game::loadSaveGame(): custom color marker missing");
            return false;
        }

        const Uint32 numHouseColors = stream.readUint32();
        for(Uint32 i=0; i<numHouseColors; i++) {
            int colorOfHouse = stream.readSint32();
            if(savegameVersion <= 9820) {
                colorOfHouse = migrateLegacyHouseColorSlot(colorOfHouse);
            }
            if(i < houseInfoListSetup.size()) {
                houseInfoListSetup[i].colorOfHouse = colorOfHouse;
            }
        }
    }

    // HouseInfo's legacy standalone save block intentionally keeps its old
    // byte layout. MOD3 stores skins in GameInitSettings, so restore them by
    // house identity into the setup copy used by load-game lobbies.
    for(GameInitSettings::HouseInfo& setupHouseInfo : houseInfoListSetup) {
        for(const GameInitSettings::HouseInfo& initHouseInfo : gameInitSettings.getHouseInfoList()) {
            if(initHouseInfo.houseID == setupHouseInfo.houseID) {
                setupHouseInfo.graphicsSkin = initHouseInfo.graphicsSkin;
                break;
            }
        }
    }

    // The resolved delivery income factors. Read positionally from SMUL and never from the
    // identity match above, which cannot speak for a row that was played as Random. The House
    // objects get their own copy from their own save block; this is the setup metadata the
    // load-game lobby displays.
    if(savegameVersion >= SpiceIncome::kFirstSavegameVersion) {
        if(stream.readUint32() != SpiceIncome::kSetupMarker) {
            SDL_Log("Game::loadSaveGame(): spice income marker missing");
            return false;
        }
        const Uint32 numSpiceFactors = stream.readUint32();
        if(numSpiceFactors != houseInfoListSetup.size()) {
            SDL_Log("Game::loadSaveGame(): spice income count disagrees with the setup rows");
            return false;
        }
        for(GameInitSettings::HouseInfo& setupHouseInfo : houseInfoListSetup) {
            const Uint32 factor = stream.readUint32();
            if(!SpiceIncome::isValid(factor)) {
                SDL_Log("Game::loadSaveGame(): invalid saved spice income factor %u", factor);
                return false;
            }
            setupHouseInfo.spiceIncomeMultiplier = factor;
        }
    }

    resetHouseVisualHouseMapping();
    for(const GameInitSettings::HouseInfo& setupHouseInfo : houseInfoListSetup) {
        int colorOfHouse = setupHouseInfo.colorOfHouse;
        if(!isValidHouseColorSlot(colorOfHouse)) {
            for(const GameInitSettings::HouseInfo& initHouseInfo : gameInitSettings.getHouseInfoList()) {
                if(initHouseInfo.houseID == setupHouseInfo.houseID
                   && isValidHouseColorSlot(initHouseInfo.colorOfHouse)) {
                    colorOfHouse = initHouseInfo.colorOfHouse;
                    break;
                }
            }
        }
        if(!isValidHouseColorSlot(colorOfHouse)) {
            colorOfHouse = getDefaultHouseColorSlot(setupHouseInfo.houseID);
        }
        setHouseVisualHouse(setupHouseInfo.houseID, colorOfHouse);
    }

    //read map size
    logLoadStage("map dimensions and game state");
    short mapSizeX = stream.readUint32();
    short mapSizeY = stream.readUint32();

    //create the new map
    currentGameMap = new Map(mapSizeX, mapSizeY);
    initializeSpatialGrid(mapSizeX, mapSizeY);

    //read GameCycleCount
    gameCycleCount = stream.readUint32();

    // read some settings
    gameType = static_cast<GameType>(stream.readSint8());
    techLevel = stream.readUint8();
    randomGen.setSeed(stream.readUint32());
    // Deviation control-interval numbering. Older saves restart at 1; their units carry no
    // episode, so no id can collide with one already in use.
    nextDeviationEpisodeID = savegameVersion >= 9849 ? stream.readUint32() : 1;

    // read in the unit/structure data
    logLoadStage("object data");
    // SAVEGAMEVERSION 9811+ stores an item count in the stream (pass 0 to auto-read).
    // Pre-9811 saves lack the count field; we infer it from duneVersion:
    //   "dunelegacy*"               → 41 items (original Dune Legacy 0.99.x)
    //   "dunecity1.0.0"–"1.0.7"    → 48 items
    //   "dunecity1.0.8"–"1.0.10"   → 52 items
    const int savedHouseCount = savegameVersion >= 9823
        ? NUM_HOUSES
        : (savegameVersion >= 9821 ? NUM_CAMPAIGN_HOUSES : NUM_LEGACY_HOUSES);
    int savedItemCount = determineLegacySavedItemCount(savegameVersion, duneVersion);
    if(savedItemCount != 0) {
        SDL_Log("Game::loadSaveGame(): legacy save v%d (%s) — loading %d items (current: %d)",
                savegameVersion, duneVersion.c_str(), savedItemCount, Num_ItemID);
    }
    objectData.load(stream, savedItemCount, savedHouseCount);

    //load the house(s) info
    logLoadStage("houses and players");
    for(int i=0; i<savedHouseCount; i++) {
        if (stream.readBool() == true) {
            //house in game
            house[i] = std::make_unique<House>(stream);
        }
    }

    // we have to set the local player
    logLoadStage("local player and flags");
    // Single-player saves contain a local-player byte even when hosted online.
    Uint8 savedLocalPlayerID = 0;
    if(!savedNetworkLayout) savedLocalPlayerID = stream.readUint8();
    // Passive observers retain the saved controllers exactly. Reconfiguring an
    // unchanged mixed human/AI house can alter its AI flag and unit rally logic.
    if(lateJoinLoad && !isSpectating()) {
        for(const auto& info : oldHouseInfoList) {
            auto* target=getHouse(info.houseID);
            if(!target) THROW(std::runtime_error,"The requested house no longer exists.");
            std::vector<std::pair<std::string,std::string>> desired;
            for(const auto& p : info.playerInfoList) desired.emplace_back(p.playerName,p.playerClass);
            target->configureNetworkPlayers(desired);
        }
    } else if(bCoopLoad) {
        for(const auto& info : oldHouseInfoList) {
            if(info.houseID != gameInitSettings.getHouseID()) continue;
            House* shared = getHouse(info.houseID);
            if(!shared) THROW(std::runtime_error, "The saved player house no longer exists.");
            std::vector<std::pair<std::string, std::string>> desired;
            for(const auto& player : info.playerInfoList) desired.emplace_back(player.playerName, player.playerClass);
            shared->configureCoopPlayers(desired);
        }
    }
    if(bMultiplayerLoad) {
        // get it from the gameInitSettings that started the game (not the one saved in the savegame)
        for(const GameInitSettings::HouseInfo& houseInfo : oldHouseInfoList) {

            // find the right house
            for(int i=0;i<NUM_HOUSES;i++) {
                if((house[i] != nullptr) && (house[i]->getHouseID() == houseInfo.houseID)) {
                    // iterate over all players
                    auto& players = house[i]->getPlayerList();
                    auto playerIter = players.cbegin();
                    for(const auto& playerInfo : houseInfo.playerInfoList) {
                        if(playerInfo.playerClass == HUMANPLAYERCLASS) {
                            while(playerIter != players.cend()) {

                                const auto pHumanPlayer = dynamic_cast<HumanPlayer*>(playerIter->get());
                                if(pHumanPlayer) {
                                    // we have actually found a human player and now assign the first unused name to it
                                    unregisterPlayer(pHumanPlayer);
                                    pHumanPlayer->setPlayername(playerInfo.playerName);
                                    registerPlayer(pHumanPlayer);

                                    if(playerInfo.playerName == getLocalPlayerName()) {
                                        pLocalHouse = house[i].get();
                                        pLocalPlayer = pHumanPlayer;
                                    }

                                    ++playerIter;
                                    break;
                                } else {
                                    ++playerIter;
                                }
                            }
                        }
                    }
                }
            }
        }
    } else {
        // it is stored in the savegame, so set it up
        pLocalPlayer = dynamic_cast<HumanPlayer*>(getPlayerByID(savedLocalPlayerID));
        pLocalHouse = house[pLocalPlayer->getHouse()->getHouseID()].get();
    }

    if(isSpectating()) setupSpectatorView();
    if(!pLocalPlayer || !pLocalHouse) THROW(std::runtime_error, "Cannot assign the local co-op player.");

    debug = stream.readBool();
    bCheatsEnabled = stream.readBool();

    winFlags = stream.readUint32();
    loseFlags = stream.readUint32();

    logLoadStage("map tiles");
    currentGameMap->load(stream);

    //load the structures and units
    logLoadStage("objects");
    objectManager.load(stream);

    // Reconstruct saved generation totals (including older constant-output windtraps)
    // from the loaded generators using current rules, without changing demand.
    std::array<int, NUM_HOUSES> loadedGeneration{};
    for (const auto* structure : structureList) {
        int output = 0;
        if (const auto* plant = dynamic_cast<const WindTrap*>(structure)) output = plant->getProducedPower();
        else if (const auto* plant = dynamic_cast<const AdvancedWindTrap*>(structure)) output = plant->getProducedPower();
        else if (const auto* plant = dynamic_cast<const NuclearPlant*>(structure)) output = plant->getProducedPower();
        else if (const auto* plant = dynamic_cast<const Scoutpost*>(structure)) output = plant->getProducedPower();
        loadedGeneration[structure->getOwner()->getHouseID()] += output;
    }
    for (int house = 0; house < NUM_HOUSES; ++house)
        if (getHouse(house)) getHouse(house)->setProducedPower(loadedGeneration[house]);


    // Zones from older saves can come back without owning their tiles, which
    // lets new lots be placed right on top of them. Re-attach every zone to
    // its footprint and report anything that was off.
    {
        int detachedTiles = 0;
        int overlappingTiles = 0;
        for(StructureBase* pStructure : structureList) {
            if(!isZoneStructure(pStructure->getItemID())) {
                continue;
            }
            for(int dy = 0; dy < pStructure->getStructureSizeY(); dy++) {
                for(int dx = 0; dx < pStructure->getStructureSizeX(); dx++) {
                    Tile* pTile = currentGameMap->getTile(pStructure->getX() + dx, pStructure->getY() + dy);
                    if(pTile == nullptr) {
                        continue;
                    }
                    const ObjectBase* pOccupant = pTile->getNonInfantryGroundObject();
                    if(pOccupant == pStructure) {
                        continue;
                    }
                    if(pOccupant == nullptr) {
                        pTile->assignNonInfantryGroundObject(pStructure->getObjectID());
                        detachedTiles++;
                    } else {
                        overlappingTiles++;
                        SDL_Log("Loaded game: zone %u at (%d,%d) overlaps object %u on tile (%d,%d)",
                                pStructure->getObjectID(), pStructure->getX(), pStructure->getY(),
                                pOccupant->getObjectID(), pStructure->getX() + dx, pStructure->getY() + dy);
                    }
                }
            }
        }
        if(detachedTiles > 0) {
            // Units loaded earlier may hold routes through a detached footprint.
            // Tile assignment no longer invalidates routes for ordinary traffic.
            currentGameMap->incrementPathingRevision();
        }
        if(detachedTiles > 0 || overlappingTiles > 0) {
            SDL_Log("Loaded game: re-attached %d zone tiles, %d zone tiles overlap another object",
                    detachedTiles, overlappingTiles);
        }
    }

    logLoadStage("bullets");
    int numBullets = stream.readUint32();
    for(int i = 0; i < numBullets; i++) {
        bulletList.push_back(new Bullet(stream));
    }

    logLoadStage("explosions");
    int numExplosions = stream.readUint32();
    for(int i = 0; i < numExplosions; i++) {
        explosionList.push_back(new Explosion(stream));
    }

    logLoadStage("selection and screen position");
    screenborder->adjustScreenBorderToMapsize(currentGameMap->getSizeX(), currentGameMap->getSizeY());
    if(!savedNetworkLayout) {
        selectedList = stream.readUint32Set();
        screenborder->load(stream);
    }
    if(bMultiplayerLoad) {
        unselectAll(selectedList);
        selectedList.clear();
        screenborder->setNewScreenCenter(pLocalHouse->getCenterOfMainBase()*TILESIZE);
    }

    // load city simulation state (version 9807+)
    logLoadStage("city simulation");
    if (savegameVersion >= 9807) {
        bool hasCitySim = stream.readBool();
        if (hasCitySim) {
            citySimEnabled_ = true;
            citySimulation_ = std::make_unique<DuneCity::CitySimulation>();
            citySimulation_->init(currentGameMap->getSizeX(), currentGameMap->getSizeY());
            // cityEffects was never serialized by GameInitSettings, so its
            // load-time value is always the default false. The explicit
            // hasCitySim save marker is authoritative: a DuneCity save must
            // resume its scans, valves, budgets, and zone growth.
            citySimulation_->setCityEffectsEnabled(
                DuneCity::shouldEnableLoadedCityEffects(hasCitySim));
            citySimulation_->load(stream);
            // A passive viewer must not perform the extra effects/growth pass
            // used to repair ordinary disk saves. Its exact live caches follow
            // in the observer runtime supplement.
            if(!isSpectating()) citySimulation_->reconcileLoadedMapState(gameCycleCount);
        }
    }

    // Mirror the new-game gate: if the loaded save's ObjectData has zones
    // enabled but city sim is off, force them off so vanilla saves can't
    // surface zone build entries.
    if (!citySimEnabled_) {
        for (int h = 0; h < NUM_HOUSES; h++) {
            objectData.data[Structure_ZoneResidential][h].enabled = false;
            objectData.data[Structure_ZoneCommercial][h].enabled  = false;
            objectData.data[Structure_ZoneIndustrial][h].enabled  = false;
        }
    }

    // loadSaveGame replaces gameInitSettings and may switch the active mod.
    // Apply the serialized skin choices only after both facts are known.
    applyDuneCityGraphicsSkins();

    // load triggers
    logLoadStage("triggers");
    triggerManager.load(stream);

    // CommandManager is at the very end of the file. DO NOT CHANGE THIS!
    logLoadStage("command history");
    cmdManager.load(stream);

    if(lateJoinLoad) {
        gameInitSettings.clearHouseInfo();
        for(const auto& info : oldHouseInfoList) gameInitSettings.addHouseInfo(info);
        for(auto& actual : houseInfoListSetup)
            for(const auto& requested : oldHouseInfoList)
                if(actual.houseID==requested.houseID) actual.playerInfoList=requested.playerInfoList;
        for(const auto& h : house) if(h) for(const auto& p : h->getPlayerList())
            if(auto* human=dynamic_cast<HumanPlayer*>(p.get())) human->nextExpectedCommandsCycle=gameCycleCount;
        cmdManager.discardCommandsFrom(gameCycleCount);
    }
    if(bCoopLoad) {
        const bool campaign = isCampaignGameType(gameInitSettings.getGameType());
        gameInitSettings.enableCoop(campaign, coopServer);
        gameInitSettings.clearHouseInfo();
        for(const auto& info : oldHouseInfoList) gameInitSettings.addHouseInfo(info);
        for(auto& actual : houseInfoListSetup)
            for(const auto& requested : oldHouseInfoList)
                if(actual.houseID == requested.houseID) actual.playerInfoList = requested.playerInfoList;
        gameType = gameInitSettings.getGameType();
    }
    logLoadStage("complete");
    finished = false;

    return true;
    } catch(const InputStream::exception& e) {
        if(const auto* fileStream = dynamic_cast<const IFileStream*>(&stream)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[SaveLoad] FAILED stage=%s offset=%ld/%ld error=%s",
                         loadStage, fileStream->getPosition(), fileStream->getLength(), e.what());
            THROW(InputStream::error,
                  "Save load failed during '%s' at byte %ld of %ld: %s",
                  loadStage, fileStream->getPosition(), fileStream->getLength(), e.what());
        }

        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[SaveLoad] FAILED stage=%s offset=unavailable error=%s",
                     loadStage, e.what());
        throw;
    }
}


bool Game::saveGame(const std::string& filename)
{
    OFileStream fs;

    if(fs.open(filename) == false) {
        SDL_Log("Game::saveGame(): %s", strerror(errno));
        currentGame->addToNewsTicker(std::string("Game NOT saved: Cannot open \"") + filename + "\".");
        return false;
    }

    saveGame(fs);
    fs.close();
    return true;
}

void Game::saveGame(OutputStream& fs) {
    fs.writeUint32(SAVEMAGIC);

    fs.writeUint32(SAVEGAMEVERSION);

    fs.writeString(VERSIONSTRING);

    // Write mod info for save compatibility (version 9806+)
    fs.writeString(ModManager::instance().getActiveModName());
    fs.writeString(ModManager::instance().getEffectiveChecksums().combined);

    // write gameInitSettings
    gameInitSettings.save(fs);

    fs.writeUint32(houseInfoListSetup.size());
    for(const GameInitSettings::HouseInfo& houseInfo : houseInfoListSetup) {
        houseInfo.save(fs);
    }

    fs.writeUint32(SAVE_SETUP_COLOR_MARKER);
    fs.writeUint32(houseInfoListSetup.size());
    for(const GameInitSettings::HouseInfo& houseInfo : houseInfoListSetup) {
        int visualHouse = getHouseVisualHouse(houseInfo.houseID);
        if(!isValidHouseColorSlot(visualHouse)) {
            visualHouse = houseInfo.colorOfHouse;
        }
        if(!isValidHouseColorSlot(visualHouse)) {
            visualHouse = houseInfo.houseID;
        }
        fs.writeSint32(visualHouse);
    }

    // The resolved per-row delivery income factors (SAVEGAMEVERSION 9851+).
    //
    // This cannot be recovered from gameInitSettings when the game is reloaded: a row the
    // player left on Random still reads HOUSE_INVALID there, so looking the factor up by house
    // identity would hand every Random row the default instead of what it played with. The
    // live House knows, because INIMapLoader seeded it from the resolved row, so that is what
    // is written here - one factor per saved setup row, in row order.
    fs.writeUint32(SpiceIncome::kSetupMarker);
    fs.writeUint32(houseInfoListSetup.size());
    for(const GameInitSettings::HouseInfo& houseInfo : houseInfoListSetup) {
        const House* pHouse = (houseInfo.houseID >= 0 && houseInfo.houseID < NUM_HOUSES)
            ? house[houseInfo.houseID].get() : nullptr;
        // A row whose house no longer exists (eliminated and cleaned up) keeps the factor the
        // setup row itself carries. Both are already validated, so neither needs clamping.
        fs.writeUint32(pHouse != nullptr ? pHouse->getSpiceIncomeMultiplier()
                                         : houseInfo.spiceIncomeMultiplier);
    }

    //write the map size
    fs.writeUint32(currentGameMap->getSizeX());
    fs.writeUint32(currentGameMap->getSizeY());

    // write GameCycleCount
    fs.writeUint32(gameCycleCount);

    // write some settings
    fs.writeSint8(static_cast<Sint8>(gameType));
    fs.writeUint8(techLevel);
    fs.writeUint32(randomGen.getSeed());
    fs.writeUint32(nextDeviationEpisodeID);

    // write out the unit/structure data
    objectData.save(fs);

    //write the house(s) info
    for(int i=0; i<NUM_HOUSES; i++) {
        fs.writeBool(house[i] != nullptr);

        if(house[i] != nullptr) {
            house[i]->save(fs);
        }
    }

    if(!isNetworkGameType(gameInitSettings.getGameType())) {
        fs.writeUint8(pLocalPlayer->getPlayerID());
    }

    fs.writeBool(debug);
    fs.writeBool(bCheatsEnabled);

    fs.writeUint32(winFlags);
    fs.writeUint32(loseFlags);

    currentGameMap->save(fs);

    // save the structures and units
    objectManager.save(fs);

    fs.writeUint32(bulletList.size());
    for(const Bullet* pBullet : bulletList) {
        pBullet->save(fs);
    }

    fs.writeUint32(explosionList.size());
    for(const Explosion* pExplosion : explosionList) {
        pExplosion->save(fs);
    }

    if(!isNetworkGameType(gameInitSettings.getGameType())) {
        // save selection lists

        // write out selected units list
        fs.writeUint32Set(selectedList);

        // write the screenborder info
        screenborder->save(fs);
    }

    // save city simulation state
    fs.writeBool(citySimEnabled_ && citySimulation_ != nullptr);
    if (citySimEnabled_ && citySimulation_) {
        citySimulation_->save(fs);
    }

    // save triggers
    triggerManager.save(fs);

    // CommandManager is at the very end of the file. DO NOT CHANGE THIS!
    cmdManager.save(fs);
}


void Game::saveObject(OutputStream& stream, ObjectBase* obj) const
{
    if(obj == nullptr)
        return;

    stream.writeUint32(obj->getItemID());
    obj->save(stream);
}


ObjectBase* Game::loadObject(InputStream& stream, Uint32 objectID)
{
    Uint32 itemID;

    itemID = stream.readUint32();

    ObjectBase* newObject = ObjectBase::loadObject(stream, itemID, objectID);
    if(newObject == nullptr) {
        THROW(std::runtime_error, "Error while loading an object!");
    }

    return newObject;
}


void Game::selectAll(const std::set<Uint32>& aList)
{
    for(Uint32 objectID : aList) {
        objectManager.getObject(objectID)->setSelected(true);
    }
}

void Game::selectAllOrnithopters()
{
    std::set<Uint32> ornithopterIDs;
    Coord summedPosition;

    for(UnitBase* pUnit : unitList) {
        if((pUnit->getOwner() == pLocalHouse) &&
           (pUnit->getItemID() == Unit_Ornithopter) &&
           pUnit->isRespondable()) {
            ornithopterIDs.insert(pUnit->getObjectID());
            summedPosition += pUnit->getLocation();
        }
    }

    if(ornithopterIDs.empty()) {
        return;
    }

    unselectAll(selectedList);
    selectedList.clear();

    for(Uint32 objectID : ornithopterIDs) {
        ObjectBase* pObject = objectManager.getObject(objectID);
        if(pObject != nullptr) {
            pObject->setSelected(true);
            selectedList.insert(objectID);
        }
    }

    selectionChanged();
    currentCursorMode = CursorMode_Normal;

    Coord averagePosition = summedPosition / static_cast<int>(ornithopterIDs.size());
    screenborder->setNewScreenCenter(averagePosition * TILESIZE);
}

void Game::selectAllChemicalCarryalls()
{
    std::set<Uint32> chemicalCarryallIDs;
    Coord summedPosition;

    for(UnitBase* pUnit : unitList) {
        if((pUnit->getOwner() == pLocalHouse) &&
           (pUnit->getItemID() == Unit_ChemicalCarryall) &&
           pUnit->isRespondable()) {
            chemicalCarryallIDs.insert(pUnit->getObjectID());
            summedPosition += pUnit->getLocation();
        }
    }

    if(chemicalCarryallIDs.empty()) {
        return;
    }

    unselectAll(selectedList);
    selectedList.clear();

    for(Uint32 objectID : chemicalCarryallIDs) {
        ObjectBase* pObject = objectManager.getObject(objectID);
        if(pObject != nullptr) {
            pObject->setSelected(true);
            selectedList.insert(objectID);
        }
    }

    selectionChanged();
    currentCursorMode = CursorMode_Normal;

    Coord averagePosition = summedPosition / static_cast<int>(chemicalCarryallIDs.size());
    screenborder->setNewScreenCenter(averagePosition * TILESIZE);
}


void Game::unselectAll(const std::set<Uint32>& aList)
{
    for(Uint32 objectID : aList) {
        objectManager.getObject(objectID)->setSelected(false);
    }
}

void Game::onReceiveSelectionList(const std::string& name, const std::set<Uint32>& newSelectionList, int groupListIndex)
{
    HumanPlayer* pHumanPlayer = dynamic_cast<HumanPlayer*>(getPlayerByName(name));

    if(pHumanPlayer == nullptr) {
        return;
    }

    if(groupListIndex == -1) {
        // the other player controlling the same house has selected some units

        if(pHumanPlayer->getHouse() != pLocalHouse) {
            return;
        }

        for(Uint32 objectID : selectedByOtherPlayerList) {
            ObjectBase* pObject = objectManager.getObject(objectID);
            if(pObject != nullptr) {
                pObject->setSelectedByOtherPlayer(false);
            }
        }

        selectedByOtherPlayerList = newSelectionList;

        for(Uint32 objectID : selectedByOtherPlayerList) {
            ObjectBase* pObject = objectManager.getObject(objectID);
            if(pObject != nullptr) {
                pObject->setSelectedByOtherPlayer(true);
            }
        }
    } else if(groupListIndex >= 0 && groupListIndex < NUMSELECTEDLISTS) {
        // some other player has assigned a number to a list of units
        pHumanPlayer->setGroupList(groupListIndex, newSelectionList);
    } else {
        // setGroupList() indexes a fixed-size array; the network boundary rejects this too.
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Game: ignoring selection list from '%s' with group index %d",
                    name.c_str(), groupListIndex);
    }
}

void Game::onPeerDisconnected(const std::string& name, bool bHost, int cause) {
    pInterface->getChatManager().addInfoMessage(name + " disconnected!");
    
    // If host disconnected, the game cannot continue - end it
    if(bHost) {
        if(isSpectating() && pNetworkManager && pNetworkManager->joinFailure().empty())
            pNetworkManager->failObserver("The connection to the host was interrupted.\nPlease try joining again.");
        SDL_Log("Host '%s' disconnected - ending game", name.c_str());
        pInterface->getChatManager().addInfoMessage("Host disconnected! Game ending...");
        
        // Set game as lost/quit so we return to menu
        bQuitGame = true;
        return;
    }
    
    // CRITICAL: Clear all client stats when ANY peer disconnects
    // Active clients will re-register at the next interval (< 375 cycles)
    // This is simpler and safer than trying to map player name → clientId
    if(pNetworkManager != nullptr && pNetworkManager->isServer()) {
        const size_t removedCount = clientStats.size();
        clientStats.clear();
        
        SDL_Log("[PathBudget HOST] Player '%s' disconnected - cleared all client stats (%zu entries)",
                name.c_str(), removedCount);
        SDL_Log("[PathBudget HOST] Active clients will re-register at next interval (< 375 cycles)");
        logPerformance("[PathBudget] Cycle %d: Cleared %zu client stats due to disconnect (%s) - active clients will re-register",
                gameCycleCount, removedCount, name.c_str());
    }
}

void Game::setGameWon() {
    if(isSpectating()) return;
    if(!bQuitGame && !finished) {
        won = true;
        finished = true;
        finishedLevelCycle = gameCycleCount;  // MULTIPLAYER FIX (Issue #9): Use cycle count
        soundPlayer->playVoice(YourMissionIsComplete,pLocalHouse->getHouseID());
    }
}


void Game::setGameLost() {
    if(isSpectating()) return;
    if(!bQuitGame && !finished) {
        won = false;
        finished = true;
        finishedLevelCycle = gameCycleCount;  // MULTIPLAYER FIX (Issue #9): Use cycle count
        soundPlayer->playVoice(YouHaveFailedYourMission,pLocalHouse->getHouseID());
    }
}


bool Game::onRadarClick(Coord worldPosition, bool bRightMouseButton, bool bDrag) {
    if(bRightMouseButton) {

        if(handleSelectedObjectsActionClick(worldPosition.x / TILESIZE, worldPosition.y / TILESIZE)) {
            indicatorFrame = 0;
            indicatorPosition = worldPosition;
        }

        return false;
    } else {

        if(bDrag) {
            screenborder->setNewScreenCenter(worldPosition);
            return true;
        } else {

            switch(currentCursorMode) {
                case CursorMode_Attack: {
                    handleSelectedObjectsAttackClick(worldPosition.x / TILESIZE, worldPosition.y / TILESIZE);
                    return false;
                } break;

case CursorMode_Heal: {
                    handleSelectedObjectsHealClick(worldPosition.x / TILESIZE, worldPosition.y / TILESIZE);
                    return false;
                } break;

                case CursorMode_Move: {
                    handleSelectedObjectsMoveClick(worldPosition.x / TILESIZE, worldPosition.y / TILESIZE);
                    return false;
                } break;

                case CursorMode_Capture: {
                    handleSelectedObjectsCaptureClick(worldPosition.x / TILESIZE, worldPosition.y / TILESIZE);
                    return false;
                } break;

                case CursorMode_CarryallDrop: {
                    handleSelectedObjectsRequestCarryallDropClick(worldPosition.x / TILESIZE, worldPosition.y / TILESIZE);
                    return false;
                } break;

                case CursorMode_Normal:
                default: {
                    screenborder->setNewScreenCenter(worldPosition);
                    return true;
                } break;
            }
        }
    }
}


bool Game::isOnRadarView(int mouseX, int mouseY) const {
    return pInterface->getRadarView().isOnRadar(mouseX - (sideBarPos.x + SIDEBAR_COLUMN_WIDTH), mouseY - sideBarPos.y);
}


void Game::handleChatInput(SDL_KeyboardEvent& keyboardEvent) {
    if(keyboardEvent.keysym.sym == SDLK_ESCAPE) {
        chatMode = false;
    } else if(keyboardEvent.keysym.sym == SDLK_RETURN) {
        if(typingChatMessage.length() > 0) {
            unsigned char md5sum[16];

            md5((const unsigned char*) typingChatMessage.c_str(), typingChatMessage.size(), md5sum);

            std::stringstream md5stream;
            md5stream << std::setfill('0') << std::hex << std::uppercase << "0x";
            for(int i=0;i<16;i++) {
                md5stream << std::setw(2) << (int) md5sum[i];
            }

            const std::string md5string = md5stream.str();

            if((bCheatsEnabled == false) && (md5string == "0xB8766C8EC7A61036B69893FC17AAF21E")) {
                bCheatsEnabled = true;
                pInterface->getChatManager().addInfoMessage("Cheat mode enabled");
            } else if((bCheatsEnabled == true) && (md5string == "0xB8766C8EC7A61036B69893FC17AAF21E")) {
                pInterface->getChatManager().addInfoMessage("Cheat mode already enabled");
            } else if((bCheatsEnabled == true) && (md5string == "0x57583291CB37F8167EDB0611D8D19E58")) {
                if (!isNetworkGameType(gameType)) {
                    pInterface->getChatManager().addInfoMessage("You win this game");
                    setGameWon();
                }
            } else if((bCheatsEnabled == true) && (md5string == "0x1A12BE3DBE54C5A504CAA6EE9782C1C8")) {
                if(debug == true) {
                    pInterface->getChatManager().addInfoMessage("You are already in debug mode");
                } else if (!isNetworkGameType(gameType)) {
                    pInterface->getChatManager().addInfoMessage("Debug mode enabled");
                    debug = true;
                }
            } else if((bCheatsEnabled == true) && (md5string == "0x54F68155FC64A5BC66DCD50C1E925C0B")) {
                if(debug == false) {
                    pInterface->getChatManager().addInfoMessage("You are not in debug mode");
                } else if (!isNetworkGameType(gameType)) {
                    pInterface->getChatManager().addInfoMessage("Debug mode disabled");
                    debug = false;
                }
            } else if((bCheatsEnabled == true) && (md5string == "0xCEF1D26CE4B145DE985503CA35232ED8")) {
                if (!isNetworkGameType(gameType)) {
                    pInterface->getChatManager().addInfoMessage("You got some credits");
                    pLocalHouse->returnCredits(10000);
                }
            } else if(md5string == "0x05362BF626E467A93FFE6FF0D8A899E3") {
                // Toggle immortality cheat (muaddib) - works in single-player only, no cheat mode required
                if (!isNetworkGameType(gameType) && gameType != GameType::LoadMultiplayer) {
                    bool currentState = gameInitSettings.getGameOptions().immortalHumanPlayer;
                    gameInitSettings.setImmortalHumanPlayer(!currentState);
                    if(!currentState) {
                        pInterface->getChatManager().addInfoMessage("God mode: Immortality enabled");
                    } else {
                        pInterface->getChatManager().addInfoMessage("God mode: Immortality disabled");
                    }
                }
            } else {
                if(pNetworkManager != nullptr) {
                    pNetworkManager->sendChatMessage(typingChatMessage);
                }
                pInterface->getChatManager().addChatMessage(getLocalPlayerName(), typingChatMessage);
            }
        }

        chatMode = false;
    } else if(keyboardEvent.keysym.sym == SDLK_BACKSPACE) {
        if(typingChatMessage.length() > 0) {
            typingChatMessage = utf8Substr(typingChatMessage, 0, utf8Length(typingChatMessage) - 1);
        }
    }
}


void Game::handleKeyInput(SDL_KeyboardEvent& keyboardEvent)
{
    if(isSpectating()) {
        switch(keyboardEvent.keysym.sym) {
            case SDLK_ESCAPE: case SDLK_RETURN: case SDLK_LEFT: case SDLK_RIGHT:
            case SDLK_UP: case SDLK_DOWN: case SDLK_TAB: break;
            default: return;
        }
    }
    switch(keyboardEvent.keysym.sym) {

        case SDLK_0: {
            // Removing selection also changes selectedList: traverse a snapshot.
            const auto previousSelection = selectedList;
            if(SDL_GetModState() & KMOD_CTRL) {
                for(Uint32 objectID : previousSelection) {
                    ObjectBase* pObject = objectManager.getObject(objectID);
                    if(pObject) {
                        pObject->setSelected(false);
                        selectedByOtherPlayerList.erase(objectID);
                    }
                    for(int i=0; i < NUMSELECTEDLISTS; i++) {
                        pLocalPlayer->getGroupList(i).erase(objectID);
                    }
                }
                selectedList.clear();
                currentGame->selectionChanged();
                currentCursorMode = CursorMode_Normal;
            } else {
                for(Uint32 objectID : previousSelection) {
                    if(auto* object = objectManager.getObject(objectID)) object->setSelected(false);
                }
                selectedList.clear();
                currentGame->selectionChanged();
                currentCursorMode = CursorMode_Normal;
            }
        } break;

        case SDLK_1:
        case SDLK_2:
        case SDLK_3:
        case SDLK_4:
        case SDLK_5:
        case SDLK_6:
        case SDLK_7:
        case SDLK_8:
        case SDLK_9: {
            // Command-number shortcuts belong to macOS (including screenshots).
            if (SDL_GetModState() & KMOD_GUI) break;
            int selectListIndex = keyboardEvent.keysym.sym - SDLK_1;

            if(citySimEnabled_ && (SDL_GetModState() & KMOD_SHIFT)) {
                switch(keyboardEvent.keysym.sym) {
                    case SDLK_1: currentCityOverlay_ = DuneCity::CityOverlayMode::None; break;
                    case SDLK_2: currentCityOverlay_ = DuneCity::CityOverlayMode::PowerGrid; break;
                    case SDLK_3: currentCityOverlay_ = DuneCity::CityOverlayMode::TrafficDensity; break;
                    case SDLK_4: currentCityOverlay_ = DuneCity::CityOverlayMode::Pollution; break;
                    case SDLK_5: currentCityOverlay_ = DuneCity::CityOverlayMode::LandValue; break;
                    case SDLK_6: currentCityOverlay_ = DuneCity::CityOverlayMode::CrimeRate; break;
                    case SDLK_7: currentCityOverlay_ = DuneCity::CityOverlayMode::Population; break;
                    case SDLK_8: currentCityOverlay_ = DuneCity::CityOverlayMode::WindTrapRadius; break;
                    default: break;
                }
            } else if (citySimEnabled_ && currentCursorMode == CursorMode_CityZone && selectListIndex < 3) {
                selectedZoneType_ = static_cast<DuneCity::ZoneType>(selectListIndex + 1);
            } else if(SDL_GetModState() & KMOD_CTRL) {
                pLocalPlayer->setGroupList(selectListIndex, selectedList);
                pInterface->updateObjectInterface();
            } else {
                std::set<Uint32>& groupList = pLocalPlayer->getGroupList(selectListIndex);

                // find out if we are choosing a group with all items already selected
                bool bEverythingWasSelected = (selectedList.size() == groupList.size());
                Coord averagePosition;
                for(Uint32 objectID : groupList) {
                    ObjectBase* pObject = objectManager.getObject(objectID);
                    bEverythingWasSelected = bEverythingWasSelected && pObject->isSelected();
                    averagePosition += pObject->getLocation();
                }

                if(groupList.empty() == false) {
                    averagePosition /= groupList.size();
                }


                if(SDL_GetModState() & KMOD_SHIFT) {
                    // we add the items from this list to the list of selected items
                } else {
                    // we replace the list of the selected items with the items from this list
                    unselectAll(selectedList);
                    selectedList.clear();
                    currentGame->selectionChanged();
                }

                // now we add the selected items
                for(Uint32 objectID : groupList) {
                    ObjectBase* pObject = objectManager.getObject(objectID);
                    if(pObject->getOwner() == pLocalHouse) {
                        pObject->setSelected(true);
                        selectedList.insert(pObject->getObjectID());
                        currentGame->selectionChanged();
                    }
                }

                if(bEverythingWasSelected && (groupList.empty() == false)) {
                    // we center around the newly selected units/structures
                    screenborder->setNewScreenCenter(averagePosition*TILESIZE);
                }
            }
            currentCursorMode = CursorMode_Normal;
        } break;

        case SDLK_KP_MINUS:
        case SDLK_MINUS: {
            requestGameSpeed(std::min(getGameSpeed()+1, GAMESPEED_MAX));
        } break;

        case SDLK_KP_PLUS:
        case SDLK_PLUS:
        case SDLK_EQUALS: {
            requestGameSpeed(std::max(getGameSpeed()-1, GAMESPEED_MIN));
        } break;

        case SDLK_b: {
            if (citySimEnabled_ && (SDL_GetModState() & KMOD_SHIFT)) {
                onCityBudget();
            }
        } break;

        case SDLK_c: {
            if (citySimEnabled_ && (SDL_GetModState() & KMOD_SHIFT)) {
                pInterface->toggleCityStatsOverlay();
            } else {
                setCursorMode(CursorMode_Capture);
            }
        } break;

        case SDLK_z: {
            if (!citySimEnabled_) {
                break;
            }
            if (currentCursorMode == CursorMode_CityZone) {
                setCursorMode(CursorMode_Normal);
            } else {
                setCursorMode(CursorMode_CityZone);
                selectedZoneType_ = DuneCity::ZoneType::Residential;
            }
        } break;

        case SDLK_a: {
            if(settings.general.wasdCamera && !(keyboardEvent.keysym.mod & KMOD_SHIFT))break;
            //set object to attack
            setCursorMode(CursorMode_Attack);
        } break;

        case SDLK_s: {
            if(keyboardEvent.keysym.mod & (KMOD_CTRL|KMOD_ALT|KMOD_GUI)) break;
            if(settings.general.wasdCamera && !(keyboardEvent.keysym.mod & KMOD_SHIFT)) break;
            UnitBase* responder=nullptr;
            for(Uint32 id:selectedList) {
                auto* unit=dynamic_cast<UnitBase*>(objectManager.getObject(id));
                if(unit && unit->getOwner()==pLocalHouse && unit->isRespondable()) {
                    unit->handleSetAttackModeClick(STOP); responder=unit;
                }
            }
            if(responder) responder->playConfirmSound();
        } break;

        case SDLK_t: {
            if(keyboardEvent.keysym.mod & (KMOD_ALT|KMOD_GUI)) break;
            if(keyboardEvent.keysym.mod & KMOD_SHIFT) {
                bShowTime = !bShowTime;
                break;
            }
            std::set<Uint32> types;
            for(Uint32 id:selectedList) {
                const auto* unit=dynamic_cast<const UnitBase*>(objectManager.getObject(id));
                if(unit && unit->getOwner()==pLocalHouse) types.insert(unit->getItemID());
            }
            if(types.empty()) break;
            const bool wholeMap=(keyboardEvent.keysym.mod & KMOD_CTRL)!=0;
            std::set<Uint32> matching;
            for(const auto* unit:unitList) {
                if(unit->getOwner()==pLocalHouse && unit->isRespondable() && unit->isActive()
                    && types.count(unit->getItemID())
                    && (wholeMap || screenborder->isTileInsideScreen(unit->getLocation())))
                    matching.insert(unit->getObjectID());
            }
            unselectAll(selectedList);
            selectedList=std::move(matching);
            selectAll(selectedList);
            selectionChanged();
            currentCursorMode=CursorMode_Normal;
        } break;

        case SDLK_ESCAPE: {
            if (currentCursorMode == CursorMode_CityZone || currentCursorMode == CursorMode_CityRoad) {
                setCursorMode(CursorMode_Normal);
            } else {
                onOptions();
            }
        } break;

        case SDLK_F1: {
            Coord oldCenterCoord = screenborder->getCurrentCenter();
            currentZoomlevel = 0;
            screenborder->adjustScreenBorderToMapsize(currentGameMap->getSizeX(), currentGameMap->getSizeY());
            screenborder->setNewScreenCenter(oldCenterCoord);
        } break;

        case SDLK_F2: {
            Coord oldCenterCoord = screenborder->getCurrentCenter();
            currentZoomlevel = 1;
            screenborder->adjustScreenBorderToMapsize(currentGameMap->getSizeX(), currentGameMap->getSizeY());
            screenborder->setNewScreenCenter(oldCenterCoord);
        } break;

        case SDLK_F3: {
            Coord oldCenterCoord = screenborder->getCurrentCenter();
            currentZoomlevel = 2;
            screenborder->adjustScreenBorderToMapsize(currentGameMap->getSizeX(), currentGameMap->getSizeY());
            screenborder->setNewScreenCenter(oldCenterCoord);
        } break;

        case SDLK_F4: {
            // skip a 30 seconds
            if(!isNetworkGameType(gameType) || bReplay) {
                skipToGameCycle = gameCycleCount + (10*1000)/GAMESPEED_DEFAULT;
            }
        } break;

        case SDLK_F5: {
            // skip a 30 seconds
            if(!isNetworkGameType(gameType) || bReplay) {
                skipToGameCycle = gameCycleCount + (30*1000)/GAMESPEED_DEFAULT;
            }
        } break;

        case SDLK_F6: {
            // skip 2 minutes
            if(!isNetworkGameType(gameType) || bReplay) {
                skipToGameCycle = gameCycleCount + (120*1000)/GAMESPEED_DEFAULT;
            }
        } break;

        case SDLK_F7: {
            // Test: Fire disaster notification
            if (citySimEnabled_) {
                triggerFireDisaster();
            }
        } break;

        case SDLK_F8: {
            // Test: Sandstorm disaster notification
            if (citySimEnabled_) {
                triggerSandstormDisaster();
            }
        } break;

        case SDLK_F9: {
            // Test: Sandworm disaster notification
            if (citySimEnabled_) {
                triggerSandwormDisaster();
            }
        } break;

        case SDLK_F10: {
            soundPlayer->toggleSound();
        } break;

        case SDLK_F11: {
            musicPlayer->toggleSound();
        } break;

        case SDLK_F12: {
            bShowFPS = !bShowFPS;
        } break;

        case SDLK_o: {
            if((SDL_GetModState() & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) == 0) {
                selectAllOrnithopters();
            }
        } break;

        case SDLK_m: {
            //set object to move
            setCursorMode(CursorMode_Move);
        } break;

        case SDLK_g: {
            // select next construction yard
            std::set<Uint32> itemIDs;
            itemIDs.insert(Structure_ConstructionYard);
            selectNextStructureOfType(itemIDs);
        } break;

        case SDLK_f: {
            // select next factory
            std::set<Uint32> itemIDs;
            itemIDs.insert(Structure_Barracks);
            itemIDs.insert(Structure_WOR);
            itemIDs.insert(Structure_LightFactory);
            itemIDs.insert(Structure_HeavyFactory);
            itemIDs.insert(Structure_HighTechFactory);
            itemIDs.insert(Structure_StarPort);
            selectNextStructureOfType(itemIDs);
        } break;

        case SDLK_p: {
            if(SDL_GetModState() & KMOD_CTRL) {
                // fall through to SDLK_PRINT
            } else {
                // Place structure
                if(selectedList.size() == 1) {
                    ConstructionYard* pConstructionYard = dynamic_cast<ConstructionYard*>(objectManager.getObject(*selectedList.begin()));
                    if(pConstructionYard != nullptr) {
                        if(currentCursorMode == CursorMode_Placing) {
                            setCursorMode(CursorMode_Normal);
                        } else if(pConstructionYard->isWaitingToPlace()) {
                            setCursorMode(CursorMode_Placing);
                        }
                    }
                }

                break;  // do not fall through
            }

        } // fall through

        case SDLK_PRINTSCREEN:
        case SDLK_SYSREQ: {
            if(SDL_GetModState() & KMOD_SHIFT) {
                takePeriodicalScreenshots = !takePeriodicalScreenshots;
            } else {
                takeScreenshot();
            }
        } break;

        case SDLK_h: {
            for(Uint32 objectID : selectedList) {
                ObjectBase* pObject = objectManager.getObject(objectID);
                if(isHarvesterLikeObject(pObject)) {
                    harvesterHandleReturnClick(pObject);
                }
            }
        } break;


        case SDLK_r: {
            if (citySimEnabled_ && (SDL_GetModState() & KMOD_SHIFT)) {
                if (currentCursorMode == CursorMode_CityRoad) {
                    setCursorMode(CursorMode_Normal);
                } else {
                    setCursorMode(CursorMode_CityRoad);
                    addToNewsTicker(_("Road tool selected: click rock or slab tiles to place roads."));
                }
            } else {
                for(Uint32 objectID : selectedList) {
                    ObjectBase* pObject = objectManager.getObject(objectID);
                    if(pObject->isAStructure()) {
                        static_cast<StructureBase*>(pObject)->handleRepairClick();
                    } else if(pObject->isAGroundUnit() && pObject->getHealth() < pObject->getMaxHealth()) {
                        static_cast<GroundUnit*>(pObject)->handleSendToRepairClick();
                    }
                }
            }
        } break;


        case SDLK_d: {
            if(settings.general.wasdCamera && !(keyboardEvent.keysym.mod & KMOD_SHIFT))break;
            setCursorMode(CursorMode_CarryallDrop);
        } break;

        case SDLK_u: {
            for(Uint32 objectID : selectedList) {
                ObjectBase* pObject = objectManager.getObject(objectID);
                if(pObject->isABuilder()) {
                    BuilderBase* pBuilder = static_cast<BuilderBase*>(pObject);
                    if(pBuilder->getHealth() >= pBuilder->getMaxHealth() && pBuilder->isAllowedToUpgrade()) {
                        pBuilder->handleUpgradeClick();
                    }
                }
            }
        } break;

        case SDLK_RETURN: {
            if(SDL_GetModState() & KMOD_ALT) {
                toogleFullscreen();
            } else {
                typingChatMessage = "";
                chatMode = true;
            }
        } break;

        case SDLK_TAB: {
            if(SDL_GetModState() & KMOD_ALT) {
                SDL_MinimizeWindow(window);
            }
        } break;

        case SDLK_SPACE: {
            if (!keyboardEvent.repeat) toggleMatchPause();
        } break;

        default: {
        } break;
    }
}


bool Game::handlePlacementClick(int xPos, int yPos) {
    BuilderBase* pBuilder = nullptr;

    if(selectedList.size() == 1) {
        pBuilder = dynamic_cast<BuilderBase*>(objectManager.getObject(*selectedList.begin()));
    }

    if(!pBuilder) {
        return false;
    }

    int placeItem = pBuilder->getCurrentProducedItem();
    if(!pBuilder->isWaitingToPlace() || placeItem == ItemID_Invalid || !isStructure(placeItem)) {
        setCursorMode(CursorMode_Normal);
        soundPlayer->playSound(Sound_InvalidAction);
        return false;
    }

    Coord structuresize = getStructureSize(placeItem);

    const bool footprintInsideMap =
        structuresize.x > 0 && structuresize.y > 0
        && xPos >= 0 && yPos >= 0
        && xPos + structuresize.x <= currentGameMap->getSizeX()
        && yPos + structuresize.y <= currentGameMap->getSizeY();
    if(!footprintInsideMap) {
        currentGame->addToNewsTicker(fmt::sprintf(_("@DUNE.ENG|134#Cannot place %%s here."), resolveItemName(placeItem)));
        soundPlayer->playSound(Sound_InvalidAction);
        return false;
    }

            if(placeItem == Structure_Slab1) {
            if((currentGameMap->isWithinBuildRange(xPos, yPos, pBuilder->getOwner()))
                && (currentGameMap->okayToPlaceStructure(xPos, yPos, 1, 1, false, pBuilder->getOwner()))
                && (currentGameMap->getTile(xPos, yPos)->isConcrete() == false)) {
                getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_PLACE_STRUCTURE,pBuilder->getObjectID(), xPos, yPos));
                //the user has tried to place and has been successful
                soundPlayer->playSound(Sound_PlaceStructure);
                setCursorMode(CursorMode_Normal);
                return true;
        } else {
            //the user has tried to place but clicked on impossible point
            currentGame->addToNewsTicker(_("@DUNE.ENG|135#Cannot place slab here."));
            soundPlayer->playSound(Sound_InvalidAction);    //can't place noise
            return false;
        }
    } else if(placeItem == Structure_Slab4) {
        if( (currentGameMap->isWithinBuildRange(xPos, yPos, pBuilder->getOwner()) || currentGameMap->isWithinBuildRange(xPos+1, yPos, pBuilder->getOwner())
                || currentGameMap->isWithinBuildRange(xPos+1, yPos+1, pBuilder->getOwner()) || currentGameMap->isWithinBuildRange(xPos, yPos+1, pBuilder->getOwner()))
            && ((currentGameMap->okayToPlaceStructure(xPos, yPos, 1, 1, false, pBuilder->getOwner())
                || currentGameMap->okayToPlaceStructure(xPos+1, yPos, 1, 1, false, pBuilder->getOwner())
                || currentGameMap->okayToPlaceStructure(xPos+1, yPos+1, 1, 1, false, pBuilder->getOwner())
                || currentGameMap->okayToPlaceStructure(xPos, yPos, 1, 1+1, false, pBuilder->getOwner())))
            && ((currentGameMap->getTile(xPos, yPos)->isConcrete() == false) || (currentGameMap->getTile(xPos+1, yPos)->isConcrete() == false)
                || (currentGameMap->getTile(xPos, yPos+1)->isConcrete() == false) || (currentGameMap->getTile(xPos+1, yPos+1)->isConcrete() == false)) ) {

            getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_PLACE_STRUCTURE,pBuilder->getObjectID(), xPos, yPos));
            //the user has tried to place and has been successful
            soundPlayer->playSound(Sound_PlaceStructure);
            setCursorMode(CursorMode_Normal);
            return true;
        } else {
            //the user has tried to place but clicked on impossible point
            currentGame->addToNewsTicker(_("@DUNE.ENG|135#Cannot place slab here."));
            soundPlayer->playSound(Sound_InvalidAction);    //can't place noise
            return false;
        }
    } else {
        if(currentGameMap->okayToPlaceStructure(xPos, yPos, structuresize.x, structuresize.y, false, pBuilder->getOwner(), false, placeItem)) {
            getCommandManager().addCommand(Command(pLocalPlayer->getPlayerID(), CMD_PLACE_STRUCTURE,pBuilder->getObjectID(), xPos, yPos));
            //the user has tried to place and has been successful
            soundPlayer->playSound(Sound_PlaceStructure);
            setCursorMode(CursorMode_Normal);
            return true;
        } else {
            //the user has tried to place but clicked on impossible point
            currentGame->addToNewsTicker(fmt::sprintf(_("@DUNE.ENG|134#Cannot place %%s here."), resolveItemName(placeItem)));
            soundPlayer->playSound(Sound_InvalidAction);    //can't place noise

            // is this building area only blocked by units?
            if(currentGameMap->okayToPlaceStructure(xPos, yPos, structuresize.x, structuresize.y, false, pBuilder->getOwner(), true, placeItem)) {
                // then we try to move all units outside the building area

                // generate a independent temporal random number generator as we are in input handling code (and outside game logic code)
                Random tempRandomGen(getGameCycleCount());

                for(int y = yPos; y < yPos + structuresize.y; y++) {
                    for(int x = xPos; x < xPos + structuresize.x; x++) {
                        Tile* pTile = currentGameMap->getTile(x,y);
                        if(pTile->hasANonInfantryGroundObject()) {
                            ObjectBase* pObject = pTile->getNonInfantryGroundObject();
                            if(pObject->isAUnit() && pObject->getOwner() == pBuilder->getOwner()) {
                                UnitBase* pUnit = static_cast<UnitBase*>(pObject);
                                Coord newDestination = currentGameMap->findDeploySpot(pUnit, Coord(xPos, yPos), tempRandomGen, pUnit->getLocation(), structuresize);
                                pUnit->handleMoveClick(newDestination.x, newDestination.y);
                            }
                        } else if(pTile->hasInfantry()) {
                            for(Uint32 objectID : pTile->getInfantryList()) {
                                InfantryBase* pInfantry = dynamic_cast<InfantryBase*>(getObjectManager().getObject(objectID));
                                if((pInfantry != nullptr) && (pInfantry->getOwner() == pBuilder->getOwner())) {
                                    Coord newDestination = currentGameMap->findDeploySpot(pInfantry, Coord(xPos, yPos), tempRandomGen, pInfantry->getLocation(), structuresize);
                                    pInfantry->handleMoveClick(newDestination.x, newDestination.y);
                                }
                            }
                        }
                    }
                }
            }

            return false;
        }
    }
}


bool Game::handleSelectedObjectsHealClick(int xPos, int yPos) {
    UnitBase* pResponder = nullptr;
    for(Uint32 objectID : selectedList) {
        ObjectBase* pObject = objectManager.getObject(objectID);
        if(pObject != nullptr && pObject->isAUnit() && pObject->getOwner() == pLocalHouse
                && pObject->isRespondable() && pObject->canHeal()) {
            pResponder = static_cast<UnitBase*>(pObject);
            pResponder->handleHealClick(xPos, yPos);
        }
    }

    setCursorMode(CursorMode_Normal);
    if(pResponder != nullptr) {
        pResponder->playConfirmSound();
        return true;
    }
    return false;
}

bool Game::handleSelectedObjectsAttackClick(int xPos, int yPos) {
    UnitBase* pResponder = nullptr;
    for(Uint32 objectID : selectedList) {
        ObjectBase* pObject = objectManager.getObject(objectID);
        House* pOwner = pObject->getOwner();
        if(pObject->isAUnit() && (pOwner == pLocalHouse) && pObject->isRespondable()) {
            pResponder = static_cast<UnitBase*>(pObject);
            pResponder->handleAttackClick(xPos,yPos);
        } else if(pObject->getItemID() == Structure_Palace && pOwner == pLocalHouse) {
            Palace* pPalace = static_cast<Palace*>(pObject);
            if(pPalace->isSpecialWeaponReady() && pPalace->usesTargetedSpecialWeapon()) {
                pPalace->handleDeathhandClick(xPos, yPos);
            }
        }
    }

    setCursorMode(CursorMode_Normal);
    if(pResponder) {
        pResponder->playConfirmSound();
        return true;
    } else {
        return false;
    }
}

bool Game::handleSelectedObjectsMoveClick(int xPos, int yPos) {
    UnitBase* pResponder = nullptr;

    for(Uint32 objectID : selectedList) {
        ObjectBase* pObject = objectManager.getObject(objectID);
        if (pObject->isAUnit() && (pObject->getOwner() == pLocalHouse) && pObject->isRespondable()) {
            pResponder = static_cast<UnitBase*>(pObject);
            pResponder->handleMoveClick(xPos,yPos);
        }
    }

    setCursorMode(CursorMode_Normal);
    if(pResponder) {
        pResponder->playConfirmSound();
        return true;
    } else {
        return false;
    }
}

/**
    New method for transporting units quickly using carryalls
**/
bool Game::handleSelectedObjectsRequestCarryallDropClick(int xPos, int yPos) {

    UnitBase* pResponder = nullptr;

    /*
        If manual carryall mode isn't enabled then turn this off...
    */
    if(!getGameInitSettings().getGameOptions().manualCarryallDrops) {
        setCursorMode(CursorMode_Normal);
        return false;
    }

    for(Uint32 objectID : selectedList) {
        ObjectBase* pObject = objectManager.getObject(objectID);
        if (pObject->isAGroundUnit() && (pObject->getOwner() == pLocalHouse) && pObject->isRespondable()) {
            pResponder = static_cast<UnitBase*>(pObject);
            pResponder->handleRequestCarryallDropClick(xPos,yPos);
        }
    }

    setCursorMode(CursorMode_Normal);
    if(pResponder) {
        pResponder->playConfirmSound();
        return true;
    } else {
        return false;
    }
}



bool Game::handleSelectedObjectsCaptureClick(int xPos, int yPos) {
    Tile* pTile = currentGameMap->getTile(xPos, yPos);

    if(pTile == nullptr) {
        return false;
    }

    StructureBase* pStructure = dynamic_cast<StructureBase*>(pTile->getGroundObject());
    if((pStructure != nullptr) && (pStructure->canBeCaptured()) && (pStructure->getOwner()->getTeamID() != pLocalHouse->getTeamID())) {
        InfantryBase* pResponder = nullptr;

        for(Uint32 objectID : selectedList) {
            ObjectBase* pObject = objectManager.getObject(objectID);
            if (pObject->isInfantry() && (pObject->getOwner() == pLocalHouse) && pObject->isRespondable()) {
                pResponder = static_cast<InfantryBase*>(pObject);
                pResponder->handleCaptureClick(xPos,yPos);
            }
        }

        setCursorMode(CursorMode_Normal);
        if(pResponder) {
            pResponder->playConfirmSound();
            return true;
        } else {
            return false;
        }
    }

    return false;
}

void Game::handleCityZonePlacementClick(int xPos, int yPos) {
    Tile* pTile = currentGameMap->getTile(xPos, yPos);

    if(pTile == nullptr) {
        return;
    }

    if (!pTile->isRock() && pTile->getType() != Terrain_Slab) {
        soundPlayer->playSound(Sound_InvalidAction);
        return;
    }

    if (pTile->hasANonInfantryGroundObject()) {
        soundPlayer->playSound(Sound_InvalidAction);
        return;
    }

    cmdManager.addCommand(Command(pLocalPlayer->getPlayerID(), CMD_CITY_PLACE_ZONE,
                                   static_cast<uint32_t>(xPos),
                                   static_cast<uint32_t>(yPos),
                                   static_cast<uint32_t>(selectedZoneType_)));

    soundPlayer->playSound(Sound_PlaceStructure);
}

void Game::handleCityRoadPlacementClick(int xPos, int yPos) {
    Tile* pTile = currentGameMap->getTile(xPos, yPos);

    if(pTile == nullptr) {
        return;
    }

    if(pTile->isMountain()) {
        addToNewsTicker(_("Cannot place road on mountain."));
        soundPlayer->playSound(Sound_InvalidAction);
        return;
    }

    if(pTile->hasAStructure()) {
        addToNewsTicker(_("Cannot place road on structure."));
        soundPlayer->playSound(Sound_InvalidAction);
        return;
    }

    if(pTile->isRoad()) {
        soundPlayer->playSound(Sound_InvalidAction);
        return;
    }

    cmdManager.addCommand(Command(pLocalPlayer->getPlayerID(), CMD_CITY_TOOL,
                                   static_cast<uint32_t>(xPos),
                                   static_cast<uint32_t>(yPos),
                                   static_cast<uint32_t>(DuneCity::CityTool_Road)));

    soundPlayer->playSound(Sound_PlaceStructure);
}

CursorAppearance::Action Game::getHoverCursorAction() const {
    using Action=CursorAppearance::Action;
    if(!pLocalHouse || !screenborder || !pInterface || pInterface->hasChildWindow()
        || chatMode || selectionMode || !screenborder->isScreenCoordInsideMap(drawnMouseX,drawnMouseY))
        return Action::Pointer;
    const int x=screenborder->screen2MapX(drawnMouseX),y=screenborder->screen2MapY(drawnMouseY);
    if(!currentGameMap->tileExists(x,y)) return Action::Pointer;
    for(Uint32 id:selectedList) {
        const auto* unit=dynamic_cast<const UnitBase*>(objectManager.getObject(id));
        if(!unit || unit->getOwner()!=pLocalHouse || !unit->isRespondable()) continue;
        const auto* target=unit->getActionClickTarget(x,y);
        if(settings.general.leftClickOrders && target && target->getOwner()==pLocalHouse) continue;
        if(target && target->getOwner()->getTeamID()!=pLocalHouse->getTeamID()) {
            if(unit->canAttack(target)) return Action::Attack;
        } else {
            const auto* structure=dynamic_cast<const StructureBase*>(target);
            if(isHarvesterLikeUnit(unit->getItemID()) && structure && structure->getOwner()==pLocalHouse
                && structure->acceptsHarvesterDropoff()) return Action::Return;
        }
    }
    // The move symbol is reserved for the explicit Move button / M mode.
    return Action::Pointer;
}

bool Game::handleSelectedObjectsActionClick(int xPos, int yPos) {
    //let unit handle right click on map or target
    ObjectBase  *pResponder = nullptr;
    for(Uint32 objectID : selectedList) {
        ObjectBase* pObject = objectManager.getObject(objectID);
        if(pObject && pObject->getOwner() == pLocalHouse && pObject->isRespondable()) {
            pObject->handleActionClick(xPos, yPos);

            //if this object obey the command
            if((pResponder == nullptr) && pObject->isRespondable())
                pResponder = pObject;
        }
    }

    actionIndicatorObject=NONE_ID;
    if(pResponder) {
        if(const auto* unit=dynamic_cast<const UnitBase*>(pResponder)) {
            if(const auto* target=unit->getActionClickTarget(xPos,yPos)) {
                actionIndicatorObject=target->getObjectID();
                actionIndicatorUntil=SDL_GetTicks()+600;
            }
        }
        pResponder->playConfirmSound();
        return true;
    } else {
        return false;
    }
}


void Game::drawCityOverlay(int x1, int y1, int x2, int y2) {
    if (!citySimulation_ || currentCityOverlay_ == DuneCity::CityOverlayMode::None) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    auto drawOverlayBlock = [&](int tileX, int tileY, int blockSize, uint8_t value, 
                                 uint8_t r1, uint8_t g1, uint8_t b1, 
                                 uint8_t r2, uint8_t g2, uint8_t b2) {
        float normalized = value / 255.0f;
        uint8_t r = static_cast<uint8_t>(r1 + (r2 - r1) * normalized);
        uint8_t g = static_cast<uint8_t>(g1 + (g2 - g1) * normalized);
        uint8_t b = static_cast<uint8_t>(b1 + (b2 - b1) * normalized);
        uint8_t a = 120;

        int zoomed_tilesize = world2zoomedWorld(TILESIZE);
        int blockPixels = blockSize * zoomed_tilesize;

        SDL_Rect overlayRect = {
            screenborder->world2screenX(tileX * TILESIZE),
            screenborder->world2screenY(tileY * TILESIZE),
            blockPixels,
            blockPixels
        };

        SDL_SetRenderDrawColor(renderer, r, g, b, a);
        SDL_RenderFillRect(renderer, &overlayRect);
    };


    switch (currentCityOverlay_) {
        case DuneCity::CityOverlayMode::PowerGrid: {
            // The per-tile city power grid (CitySimulation::getPowerGridMap)
            // is stubbed — every cell reads 0.  Instead, show a simple
            // house-level power status: green overlay on zone/conductive tiles
            // when the house has power, red when it doesn't.
            bool housePower = pLocalHouse->hasPower();
            uint8_t level = housePower ? 200 : 200;
            int rOn = 0, gOn = 255, bOn = 0;     // green = powered
            int rOff = 255, gOff = 0, bOff = 0;  // red   = no power
            const auto& powerMap = citySimulation_->getPowerGridMap();
            int blockSize = powerMap.getBlockSize();
            for (int x = x1; x < x2; x += blockSize) {
                for (int y = y1; y < y2; y += blockSize) {
                    if (!currentGameMap->tileExists(x, y)) continue;
                    Tile* t = currentGameMap->getTile(x, y);
                    if (t->hasCityZone() || t->isRoad()) {
                        if (housePower)
                            drawOverlayBlock(x, y, blockSize, level, rOn, gOn, bOn, rOn, gOn, bOn);
                        else
                            drawOverlayBlock(x, y, blockSize, level, rOff, gOff, bOff, rOff, gOff, bOff);
                    }
                }
            }
        } break;

        case DuneCity::CityOverlayMode::TrafficDensity: {
            const auto& trafficMap = citySimulation_->getTrafficDensityMap();
            int blockSize = trafficMap.getBlockSize();
            for (int x = x1; x < x2; x += blockSize) {
                for (int y = y1; y < y2; y += blockSize) {
                    uint8_t traffic = trafficMap.worldGet(x, y);
                    if (traffic > 0) {
                        drawOverlayBlock(x, y, blockSize, traffic, 0, 255, 0, 255, 0, 0);
                    }
                }
            }
        } break;

        case DuneCity::CityOverlayMode::Pollution: {
            const auto& pollutionMap = citySimulation_->getPollutionDensityMap();
            int blockSize = pollutionMap.getBlockSize();
            for (int x = x1; x < x2; x += blockSize) {
                for (int y = y1; y < y2; y += blockSize) {
                    uint8_t pollution = pollutionMap.worldGet(x, y);
                    if (pollution > 0) {
                        drawOverlayBlock(x, y, blockSize, pollution, 0, 255, 0, 150, 0, 200);
                    }
                }
            }
        } break;

        case DuneCity::CityOverlayMode::LandValue: {
            const auto& landValueMap = citySimulation_->getLandValueMap();
            int blockSize = landValueMap.getBlockSize();
            for (int x = x1; x < x2; x += blockSize) {
                for (int y = y1; y < y2; y += blockSize) {
                    uint8_t landValue = landValueMap.worldGet(x, y);
                    if (landValue > 0) {
                        drawOverlayBlock(x, y, blockSize, landValue, 255, 0, 0, 0, 255, 0);
                    }
                }
            }
        } break;

        case DuneCity::CityOverlayMode::CrimeRate: {
            const auto& crimeMap = citySimulation_->getCrimeRateMap();
            int blockSize = crimeMap.getBlockSize();
            for (int x = x1; x < x2; x += blockSize) {
                for (int y = y1; y < y2; y += blockSize) {
                    const int crime = crimeMap.worldGet(x, y);
                    if (crime > 0) {
                        // Crime is warm-coloured throughout its range: orange
                        // at low levels and a deep red at high levels. Green
                        // reads as safe and made this overlay misleading.
                        drawOverlayBlock(x, y, blockSize, static_cast<uint8_t>(std::min(255, crime)),
                                         255, 160, 0, 128, 0, 0);
                    }
                }
            }
        } break;

        case DuneCity::CityOverlayMode::Population: {
            const auto& popMap = citySimulation_->getPopulationDensityMap();
            int blockSize = popMap.getBlockSize();
            for (int x = x1; x < x2; x += blockSize) {
                for (int y = y1; y < y2; y += blockSize) {
                    uint8_t pop = popMap.worldGet(x, y);
                    if (pop > 0) {
                        drawOverlayBlock(x, y, blockSize, pop, 50, 50, 50, 255, 255, 255);
                    }
                }
            }
        } break;

        case DuneCity::CityOverlayMode::WindTrapRadius: {
            // Draw circles around Wind Traps showing their power radius
            // Wind Trap power radius: typically 12-15 tiles (from Micropolis)
            constexpr int WINDTRAP_POWER_RADIUS = 12;

            for (const auto* pStructure : structureList) {
                if (pStructure->getItemID() == Structure_WindTrap) {
                    int wx = pStructure->getX();
                    int wy = pStructure->getY();

                    // Draw radius as a filled circle with transparency
                    for (int dx = -WINDTRAP_POWER_RADIUS; dx <= WINDTRAP_POWER_RADIUS; dx++) {
                        for (int dy = -WINDTRAP_POWER_RADIUS; dy <= WINDTRAP_POWER_RADIUS; dy++) {
                            int tx = wx + dx;
                            int ty = wy + dy;

                            if (!currentGameMap->tileExists(tx, ty)) continue;

                            // Check if within circular radius (center is 2x2, so adjust)
                            int distSq = dx*dx + dy*dy;
                            if (distSq <= WINDTRAP_POWER_RADIUS * WINDTRAP_POWER_RADIUS) {
                                // Draw at full opacity at edge, fading toward center
                                int alpha = 50 + (distSq * 150) / (WINDTRAP_POWER_RADIUS * WINDTRAP_POWER_RADIUS);
                                alpha = std::min(200, alpha);

                                // Draw as single tiles in the overlay
                                drawOverlayBlock(tx, ty, 1, 255, 0, 150, 255, 0, 255, alpha);
                            }
                        }
                    }
                }
            }
        } break;
        default:
            break;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void Game::drawCityRoads(int x1, int y1, int x2, int y2) {
    if (!citySimulation_) {
        return;
    }

    // The proper road atlas (ObjPic_CityRoad) is rendered in Tile::blitGround
    // with auto-tiled sprites and white center markings. This placeholder
    // brown-rectangle overlay is only needed when the atlas failed to load.
    SDL_Texture* roadAtlas = pGFXManager->getZoomedObjPic(ObjPic_CityRoad, currentZoomlevel);
    if (roadAtlas) {
        return;  // atlas handles rendering; skip brown overlay
    }

    const int zoomedTileSize = world2zoomedWorld(TILESIZE);
    const int centerInset = std::max(2, zoomedTileSize / 3);
    const int laneHalfWidth = std::max(1, zoomedTileSize / 10);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    auto drawRoadStub = [&](int tileX, int tileY, int dx, int dy) {
        SDL_Rect stubRect{};
        const int centerX = screenborder->world2screenX(tileX * TILESIZE) + zoomedTileSize / 2;
        const int centerY = screenborder->world2screenY(tileY * TILESIZE) + zoomedTileSize / 2;

        if (dx != 0) {
            stubRect.y = centerY - laneHalfWidth;
            stubRect.h = laneHalfWidth * 2;
            if (dx < 0) {
                stubRect.x = centerX - centerInset;
                stubRect.w = centerInset;
            } else {
                stubRect.x = centerX;
                stubRect.w = centerInset;
            }
        } else {
            stubRect.x = centerX - laneHalfWidth;
            stubRect.w = laneHalfWidth * 2;
            if (dy < 0) {
                stubRect.y = centerY - centerInset;
                stubRect.h = centerInset;
            } else {
                stubRect.y = centerY;
                stubRect.h = centerInset;
            }
        }

        SDL_RenderFillRect(renderer, &stubRect);
    };

    currentGameMap->for_each(x1, y1, x2, y2, [&](Tile& tile) {
        if (!tile.isRoad() || tile.hasCityZone()) {
            return;
        }

        if (!debug && !tile.isExploredByTeam(pLocalHouse->getTeamID())) {
            return;
        }

        const int tileX = tile.getLocation().x;
        const int tileY = tile.getLocation().y;
        const int screenX = screenborder->world2screenX(tileX * TILESIZE);
        const int screenY = screenborder->world2screenY(tileY * TILESIZE);

        SDL_SetRenderDrawColor(renderer, 120, 92, 56, 210);
        SDL_Rect centerRect = {
            screenX + centerInset,
            screenY + centerInset,
            zoomedTileSize - 2 * centerInset,
            zoomedTileSize - 2 * centerInset
        };
        SDL_RenderFillRect(renderer, &centerRect);

        SDL_SetRenderDrawColor(renderer, 196, 168, 120, 210);
        if (currentGameMap->tileExists(tileX, tileY - 1) && currentGameMap->getTile(tileX, tileY - 1)->isRoad()) {
            drawRoadStub(tileX, tileY, 0, -1);
        }
        if (currentGameMap->tileExists(tileX + 1, tileY) && currentGameMap->getTile(tileX + 1, tileY)->isRoad()) {
            drawRoadStub(tileX, tileY, 1, 0);
        }
        if (currentGameMap->tileExists(tileX, tileY + 1) && currentGameMap->getTile(tileX, tileY + 1)->isRoad()) {
            drawRoadStub(tileX, tileY, 0, 1);
        }
        if (currentGameMap->tileExists(tileX - 1, tileY) && currentGameMap->getTile(tileX - 1, tileY)->isRoad()) {
            drawRoadStub(tileX, tileY, -1, 0);
        }

        if (!hasVisibleRoadNeighbor(tileX, tileY)) {
            SDL_SetRenderDrawColor(renderer, 196, 168, 120, 210);
            SDL_Rect fallbackRect = {
                screenX + zoomedTileSize / 2 - laneHalfWidth,
                screenY + centerInset,
                laneHalfWidth * 2,
                zoomedTileSize - 2 * centerInset
            };
            SDL_RenderFillRect(renderer, &fallbackRect);
        }
    });

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void Game::drawCityPlacementHint() {
    if (currentCursorMode != CursorMode_CityRoad) {
        return;
    }

    const std::string hint = std::string(DuneCity::getRoadPlacementModeLabel()) + "\nShift+R toggle · Click to place · Esc cancels";
    sdl2::texture_ptr hintTexture = pFontManager->createTextureWithMultilineText(hint, COLOR_WHITE, 12, true);
    if (!hintTexture) {
        return;
    }

    int texW = 0;
    int texH = 0;
    SDL_QueryTexture(hintTexture.get(), nullptr, nullptr, &texW, &texH);

    const int hintX = 20;
    const int hintY = std::max(90, topBarPos.h + 18);
    SDL_Rect bgRect = {hintX - 6, hintY - 6, texW + 12, texH + 12};

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_RenderFillRect(renderer, &bgRect);
    SDL_SetRenderDrawColor(renderer, 196, 168, 120, 220);
    SDL_RenderDrawRect(renderer, &bgRect);

    SDL_Rect textRect = {hintX, hintY, texW, texH};
    SDL_RenderCopy(renderer, hintTexture.get(), nullptr, &textRect);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}


void Game::takeScreenshot() const {
    std::string filename;
    if(saveScreenshot(renderer,filename)) {
        currentGame->addToNewsTicker(_("Screenshot saved") + ": '" + filename + "'");
    } else {
        SDL_Log("Screenshot failed: %s",SDL_GetError());
        currentGame->addToNewsTicker(_("Could not save screenshot"));
    }
}

void Game::triggerFireDisaster() {
    if(pInterface != nullptr) {
        pInterface->addDisasterNotification(DisasterType::Fire, "Fire! Extinguish immediately!", 8, 3);
    }
}

void Game::triggerSandstormDisaster() {
    if(pInterface != nullptr) {
        pInterface->addDisasterNotification(DisasterType::Sandstorm, "Sandstorm approaching!", 12, 5);
    }
}

void Game::triggerSandwormDisaster() {
    if(pInterface != nullptr) {
        pInterface->addDisasterNotification(DisasterType::Sandworm, "Sandworm spotted!", 6, 2);
    }
}

void Game::selectNextStructureOfType(const std::set<Uint32>& itemIDs) {
    bool bSelectNext = true;

    if(selectedList.size() == 1) {
        ObjectBase* pObject = getObjectManager().getObject(*selectedList.begin());
        if((pObject != nullptr) && (itemIDs.count(pObject->getItemID()) == 1)) {
            bSelectNext = false;
        }
    }

    StructureBase* pStructure2Select = nullptr;

    for(StructureBase* pStructure : structureList) {
        if(bSelectNext) {
            if( (itemIDs.count(pStructure->getItemID()) == 1) && (pStructure->getOwner() == pLocalHouse) ) {
                pStructure2Select = pStructure;
                break;
            }
        } else {
            if(selectedList.size() == 1 && pStructure->isSelected()) {
                bSelectNext = true;
            }
        }
    }

    if(pStructure2Select == nullptr) {
        // start over at the beginning
        for(StructureBase* pStructure : structureList) {
            if( (itemIDs.count(pStructure->getItemID()) == 1) && (pStructure->getOwner() == pLocalHouse) && !pStructure->isSelected() ) {
                pStructure2Select = pStructure;
                break;
            }
        }
    }

    if(pStructure2Select != nullptr) {
        unselectAll(selectedList);
        selectedList.clear();

        pStructure2Select->setSelected(true);
        selectedList.insert(pStructure2Select->getObjectID());
        currentGame->selectionChanged();

        // we center around the newly selected construction yard
        screenborder->setNewScreenCenter(pStructure2Select->getLocation()*TILESIZE);
    }
}

int Game::getGameSpeed() const {
    if(isNetworkGameType(gameType)) {
        return gameInitSettings.getGameOptions().gameSpeed;
    } else {
        return settings.gameOptions.gameSpeed;
    }
}

bool Game::canChangeGameSettings() const {
    return !bReplay && !isSpectating() && (!pNetworkManager || pNetworkManager->isServer());
}

bool Game::requestGameSpeed(int speed) {
    if (!canChangeGameSettings() || speed < GAMESPEED_MIN || speed > GAMESPEED_MAX) return false;
    if (isNetworkGameType(gameType)) {
        gameInitSettings.setGameSpeed(speed);
        ++matchControl.revision;
        publishMatchControl();
    }
    settings.gameOptions.gameSpeed = speed;
    INIFile config(getConfigFilepath());
    config.setIntValue("Game Options", "Game Speed", speed);
    config.saveChangesTo(getConfigFilepath());
    WebRuntime::syncPersistentFiles();
    addToNewsTicker(fmt::sprintf(_("Game speed") + ": %d", speed));
    return true;
}

bool Game::canToggleMatchPause() const {
    return !bReplay && !finished && !isSpectating() && pLocalPlayer
        && dynamic_cast<const HumanPlayer*>(pLocalPlayer)
        && (!pNetworkManager || !pNetworkManager->lateJoinPaused());
}

void Game::toggleMatchPause() {
    if (!canToggleMatchPause() || (pauseRequestPending && !isGamePaused())) return;
    if (!pNetworkManager) {
        if (bPause) resumeGame(); else pauseGame("pause_button");
        return;
    }
    if (matchControl.pausedAt(gameCycleCount)) {
        if (pNetworkManager->isServer()) handleMatchResumeRequest(localPlayerName, matchControl.pauseCycle);
        else pNetworkManager->requestMatchResume(matchControl.pauseCycle);
    } else {
        pauseRequestPending = true;
        cmdManager.addCommand(Command(pLocalPlayer->getPlayerID(), CMD_MATCH_PAUSE, gameCycleCount));
    }
}

void Game::executeMatchPause(Uint8 issuer, Uint32 requestCycle) {
    const auto* player = dynamic_cast<const HumanPlayer*>(getPlayerByID(issuer));
    // A replay preserves world commands, not the original players' waiting time.
    if (bReplay || !pNetworkManager || !player || !player->getHouse() || finished) return;
    if (player->getPlayername() == localPlayerName) pauseRequestPending = false;
    // Coalesce overlapping button presses, including ones still queued when the
    // first pause is resumed. A new press at the resumed boundary remains valid.
    const bool fromMenu = menuPause && player->getPlayername() == localPlayerName
        && requestCycle == menuPause->requestCycle;
    if (requestCycle > gameCycleCount || requestCycle < matchControl.pauseCycle) {
        if(fromMenu) menuPause.reset();
        return;
    }
    matchControl.pauseAfter(gameCycleCount + 1);
    if(fromMenu) menuPause->pauseCycle = matchControl.pauseCycle;
    if (pNetworkManager->isServer()) { ++matchControl.revision; publishMatchControl(); }
    addToNewsTicker(player->getPlayername() + _(" paused the game"));
    if(fromMenu && menuPause->closed) {
        const auto cycle = menuPause->pauseCycle;
        menuPause.reset();
        handleMatchResumeRequest(localPlayerName, cycle);
    }
}

void Game::handleMatchControl(Uint32 revision, Uint32 speed, Uint32 pause, Uint32 resumed) {
    if (!pNetworkManager || pNetworkManager->isServer() || speed < GAMESPEED_MIN || speed > GAMESPEED_MAX) return;
    const bool wasPaused = matchControl.pausedAt(gameCycleCount);
    const int previousSpeed = getGameSpeed();
    if (!matchControl.receive(revision, pause, resumed)) return;
    gameInitSettings.setGameSpeed(int(speed));
    if (previousSpeed != int(speed)) addToNewsTicker(fmt::sprintf(_("Game speed") + ": %d", speed));
    if (wasPaused && !matchControl.pausedAt(gameCycleCount)) addToNewsTicker(_("Game resumed"));
}

void Game::handleMatchResumeRequest(const std::string& name, Uint32 pause) {
    if (!pNetworkManager || !pNetworkManager->isServer() || finished) return;
    const auto* player = dynamic_cast<const HumanPlayer*>(getPlayerByName(name));
    if (!player || !player->getHouse() || pNetworkManager->isSpectator(name)) return;
    if (matchControl.resume(pause)) {
        ++matchControl.revision;
        addToNewsTicker(player->getPlayername() + _(" resumed the game"));
    }
    publishMatchControl();
}

void Game::publishMatchControl() {
    if (!pNetworkManager || !pNetworkManager->isServer()) return;
    pNetworkManager->sendMatchControl(matchControl.revision, getGameSpeed(),
        matchControl.pauseCycle, matchControl.resumedPauseCycle);
    lastMatchControlBroadcast = SDL_GetTicks();
}

bool Game::handleNetworkUpdates() {
    if(pNetworkManager == nullptr) {
        return false;
    }

    pNetworkManager->update();
    // Runs while paused as well: keep late joiners and slow peers up to date.
    if (pNetworkManager->isServer() && SDL_GetTicks()-lastMatchControlBroadcast >= 1000) publishMatchControl();
    if(auto* direct=pNetworkManager->getDirectTransport(); direct && pNetworkManager->isServer()) {
        for(const auto& request : direct->joinRequests()) {
            if(request.spectator) {
                if(!pNetworkManager->lateJoinPaused() && !direct->joinDecisionPending()
                   && !direct->manageJoin("approve_spectator",request.id)) direct->manageJoin("abort",request.id);
                continue;
            }
        }
    }
    if(auto* direct=pNetworkManager->getDirectTransport(); direct && isSpectating()) {
        const auto& state=direct->playRequestState();
        if(state!=lastPlayRequestState) {
            lastPlayRequestState=state;
            if(state=="pending") addToNewsTicker("Request sent. You can keep spectating while the host chooses.");
            else if(state=="declined") addToNewsTicker("The host declined your request. You can keep spectating.");
            else if(state=="cancelled") addToNewsTicker("Request to play cancelled. You are still spectating.");
            else if(state=="error") addToNewsTicker("Could not send the request to play. Please try again.");
        }
    }
    if(!pNetworkManager->lateJoinStatus().empty() && lastJoinStatus!=pNetworkManager->lateJoinStatus()) {
        lastJoinStatus=pNetworkManager->lateJoinStatus(); addToNewsTicker(lastJoinStatus);
    }
    if(pNetworkManager->lateJoinReady()) { bQuitGame=true; return true; }
    if(pNetworkManager->lateJoinPaused()) {
        startWaitingForOtherPlayersTime=0; pWaitingForOtherPlayers.reset();
        if(!pInGameMenu) { pInGameMenu=std::make_unique<JoinProgressWindow>(); bMenu=true; }
        if(auto* progress=dynamic_cast<JoinProgressWindow*>(pInGameMenu.get())) progress->refresh();
        return true;
    }
    if(dynamic_cast<JoinProgressWindow*>(pInGameMenu.get())) { pInGameMenu.reset(); bMenu=false; }

    if(pNetworkManager->isServer()) prepareObserverStreams();
    if(isSpectating()) {
        if(pNetworkManager->observerResyncWaiting()) return true;
        if(observerCyclePrepared) return false;
        std::string bytes;
        if(!pNetworkManager->takeObserverCycle(gameCycleCount,bytes)) return true;
        try {
            IMemoryStream frame(bytes.data(),bytes.size());
            const auto budget=frame.readUint32(); const auto fingerprint=frame.readString();
            if(!fingerprint.empty()) {
                GameStateDigest::Digest expected;
                if(!GameStateDigest::decode(reinterpret_cast<const Uint8*>(fingerprint.data()),fingerprint.size(),expected))
                    throw std::runtime_error("Invalid spectator fingerprint");
                const auto actual=computeStateDigest();
                if(actual.divergesFrom(expected)) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"Spectator fingerprint expected: %s; actual: %s",
                        GameStateDigest::describe(expected).c_str(),GameStateDigest::describe(actual).c_str());
                    throw std::runtime_error("Spectator state diverged");
                }
            }
            const auto count=frame.readUint32();
            if(budget<kMinBudget || budget>kMaxBudget || count>4096) throw std::runtime_error("Invalid spectator tick");
            std::vector<Command> commands; commands.reserve(count);
            for(Uint32 i=0;i<count;++i) {
                commands.emplace_back(frame);
                if(!CommandValidation::isWellFormedCommand(static_cast<Uint32>(commands.back().getCommandID()),commands.back().getParameter().size()))
                    throw std::runtime_error("Malformed spectator command");
            }
            if(frame.getRemainingLength()!=0) throw std::runtime_error("Extra spectator tick data");
            negotiatedBudget=budget;
            cmdManager.discardCommandsFrom(gameCycleCount);
            for(const auto& command : commands) cmdManager.addCommand(command,gameCycleCount);
            observerCyclePrepared=true;
            skipToGameCycle=pNetworkManager->observerFrontier();
            return false;
        } catch(const std::exception& error) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"Spectator stream stopped at cycle %u: %s",gameCycleCount,error.what());
            for(const auto& h : house) if(h) SDL_Log("Spectator house state: house=%d credits=%d structures=%d units=%d",
                h->getHouseID(),h->getCredits(),h->getNumStructures(),h->getNumUnits());
            if(pNetworkManager->requestObserverResync(gameCycleCount)) return true;
            pNetworkManager->failObserver("Joining stopped because the game state did not match the host.\n"
                "The host's game can continue. Please try joining again.");
            quitGame(); return true;
        }
    }
    bool bWaitForNetwork = false;

    // Check for network delays
    for(const std::string& playername : pNetworkManager->getConnectedPeers()) {
        HumanPlayer* pPlayer = dynamic_cast<HumanPlayer*>(getPlayerByName(playername));
        if(pPlayer != nullptr && pPlayer->nextExpectedCommandsCycle <= gameCycleCount) {
            bWaitForNetwork = true;
            break;
        }
    }

    if(bWaitForNetwork) {
        if(startWaitingForOtherPlayersTime == 0) {
            startWaitingForOtherPlayersTime = SDL_GetTicks();
        } else {
            const Uint32 waitedMs = SDL_GetTicks() - startWaitingForOtherPlayersTime;
            if(waitedMs > 1000) {
                if(pWaitingForOtherPlayers == nullptr) {
                    pWaitingForOtherPlayers = std::make_unique<WaitingForOtherPlayers>();
                    bMenu = true;
                }
            }

            // Lockstep has to end somewhere. Without this the match waits forever for a player
            // whose tab was suspended or whose connection died quietly, with nothing on screen
            // but "waiting for other players". Ending it visibly is the honest outcome; a
            // player's commands are never skipped to keep the match moving, because that is a
            // silent desynchronisation.
            if(pNetworkManager->isRoomSession()
               && waitedMs > LOCKSTEP_STALL_TIMEOUT_MS && !lockstepStallReported) {
                lockstepStallReported = true;
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Game: no commands from another player for %u ms at cycle %u - "
                             "ending the match", static_cast<unsigned>(waitedMs),
                             static_cast<unsigned>(gameCycleCount));
                addUrgentMessageToNewsTicker(
                    _("Another player stopped responding. The game has ended."));
                pNetworkManager->disconnect();
                quitGame();
            }
        }
#ifndef __EMSCRIPTEN__
        SDL_Delay(1);  // Reduced from 10ms to 1ms to improve host performance
#endif
        // Browser SDL_Delay suspends the Asyncify stack and resumes through a timer.
        // The frame-end yield already gives the browser an event-loop turn; additional
        // timers here and in the wait branch add overhead during every lockstep stall.
    } else {
        startWaitingForOtherPlayersTime = 0;
        if(pWaitingForOtherPlayers != nullptr) {
            pWaitingForOtherPlayers.reset();
            if(pInGameMenu == nullptr && pInGameMentat == nullptr) {
                bMenu = false;
            }
        }
    }

    return bWaitForNetwork;
}

GameStateDigest::Digest Game::computeStateDigest() const {
    GameStateDigest::Digest digest;
    digest.gameCycle  = gameCycleCount;
    digest.randomSeed = randomGen.getSeed();

    GameStateDigest::Hasher houses;
    for(int houseID = 0; houseID < NUM_HOUSES; houseID++) {
        const House* pHouse = house[houseID].get();
        houses.mixUint8(static_cast<Uint8>(houseID));
        houses.mixUint8(pHouse != nullptr ? 1 : 0);
        if(pHouse == nullptr) {
            continue;
        }
        houses.mixInt32(pHouse->getCredits());
        houses.mixInt32(pHouse->getNumStructures());
        houses.mixInt32(pHouse->getNumUnits());
        // Mixed in its own right, not just through the credits it produces: two peers that
        // disagree about a house's delivery rate must be told so at the next digest, rather
        // than at whatever later cycle the difference first reaches a refinery.
        houses.mixUint32(pHouse->getSpiceIncomeMultiplier());
    }
    digest.houseHash = houses.value();

    GameStateDigest::Hasher objects;
    Uint32 objectCount = 0;
    objectManager.forEachObject([&](Uint32 objectID, const ObjectBase* pObject) {
        if(pObject == nullptr) {
            return;
        }
        objectCount++;
        objects.mixUint32(objectID);
        objects.mixInt32(pObject->getItemID());
        objects.mixInt32(pObject->getOriginalHouseID());
        const House* pOwner = pObject->getOwner();
        objects.mixInt32(pOwner != nullptr ? pOwner->getHouseID() : -1);
        objects.mixInt64(static_cast<std::int64_t>(pObject->getHealth().getRawValue()));
        objects.mixInt32(pObject->getLocation().x);
        objects.mixInt32(pObject->getLocation().y);
    });
    digest.objectHash  = objects.value();
    digest.objectCount = objectCount;

    return digest;
}

void Game::updateStateDigests() {
    if(isSpectating()) return;
    if(pNetworkManager == nullptr || !pNetworkManager->isRoomSession()) {
        return;
    }
    if(gameCycleCount == 0 || (gameCycleCount % GameStateDigest::kDigestIntervalCycles) != 0) {
        return;
    }
    if(gameCycleCount == lastStateDigestCycle) {
        return;
    }
    lastStateDigestCycle = gameCycleCount;

    const GameStateDigest::Digest digest = computeStateDigest();

    localStateDigests.push_back(digest);
    while(localStateDigests.size() > GameStateDigest::kDigestHistory) {
        localStateDigests.pop_front();
    }

    Uint8 encoded[GameStateDigest::kEncodedSize];
    GameStateDigest::encode(digest, encoded);
    pNetworkManager->sendRelayDiagnostic(RoomRelay::DiagnosticKind::StateDigest, encoded,
                                         sizeof(encoded));

    // Anything a peer sent for this cycle before we reached it can be resolved now.
    for(auto iterator = pendingRemoteDigests.begin(); iterator != pendingRemoteDigests.end();) {
        if(iterator->second.gameCycle == digest.gameCycle) {
            if(digest.divergesFrom(iterator->second)) {
                reportStateDivergence(iterator->first, digest, iterator->second);
            }
            iterator = pendingRemoteDigests.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void Game::onRemoteStateDigest(const std::string& peerName,
                               const GameStateDigest::Digest& digest) {
    for(const GameStateDigest::Digest& ours : localStateDigests) {
        if(ours.gameCycle == digest.gameCycle) {
            if(ours.divergesFrom(digest)) {
                reportStateDivergence(peerName, ours, digest);
            }
            return;
        }
    }

    // Their cycle is one we have not produced yet: keep it, bounded, until we do.
    pendingRemoteDigests.emplace_back(peerName, digest);
    while(pendingRemoteDigests.size() > GameStateDigest::kDigestHistory) {
        pendingRemoteDigests.pop_front();
    }
}

void Game::reportStateDivergence(const std::string& peerName,
                                 const GameStateDigest::Digest& ours,
                                 const GameStateDigest::Digest& theirs) {
    if(stateDivergenceReported) {
        return;
    }
    stateDivergenceReported = true;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Game: state digest mismatch with '%s' - local %s, remote %s",
                 peerName.c_str(), GameStateDigest::describe(ours).c_str(),
                 GameStateDigest::describe(theirs).c_str());

    addUrgentMessageToNewsTicker(
        _("This game is no longer identical for every player. Results may differ."));
}

void Game::dumpCombatStats() {
    SDL_Log("[Combat Stats] ==================== 30-SECOND ANTI-AIR ANALYSIS ====================");
    
    // ===== ROCKET TURRETS =====
    SDL_Log("[Combat Stats] ");
    SDL_Log("[Combat Stats] === ROCKET TURRETS vs ORNITHOPTERS ===");
    SDL_Log("[Combat Stats] TARGETING:");
    SDL_Log("[Combat Stats]   Ornithopters Acquired as Target:  %d", combatStats.rocketTurretTargetsOrni);
    SDL_Log("[Combat Stats]   Target Lost (out of range/dead):  %d", combatStats.rocketTurretLosesOrniTarget);
    
    SDL_Log("[Combat Stats] FIRING OPPORTUNITIES:");
    const int totalOpportunities = combatStats.orniInRangeCorrectAngle;
    SDL_Log("[Combat Stats]   Ornithopter in Range + Correct Angle: %d", combatStats.orniInRangeCorrectAngle);
    SDL_Log("[Combat Stats]   Ornithopter in Range BUT Wrong Angle: %d", combatStats.orniInRangeButWrongAngle);
    SDL_Log("[Combat Stats]   Actually Fired:                       %d", combatStats.rocketTurretFiresOnOrni);
    SDL_Log("[Combat Stats]   Blocked by Weapon Timer:              %d", combatStats.rocketTurretFireBlocked);
    
    if(totalOpportunities > 0) {
        const double fireRate = static_cast<double>(combatStats.rocketTurretFiresOnOrni) / totalOpportunities * 100.0;
        SDL_Log("[Combat Stats]   Fire Success Rate: %.1f%% (%d fired / %d opportunities)", 
                fireRate, combatStats.rocketTurretFiresOnOrni, totalOpportunities);
    }
    
    SDL_Log("[Combat Stats] ROCKET PERFORMANCE:");
    SDL_Log("[Combat Stats]   Rockets Spawned:          %d", combatStats.turretRocketsSpawned);
    SDL_Log("[Combat Stats]   Proximity Detonations:    %d", combatStats.turretRocketsProximityDetonated);
    SDL_Log("[Combat Stats]   Timer Expirations:        %d", combatStats.turretRocketsExpired);
    SDL_Log("[Combat Stats]   Rockets Hit Ornithopter:  %d", combatStats.turretRocketsHitOrni);
    SDL_Log("[Combat Stats]   Rockets Killed Ornithopter: %d", combatStats.turretRocketsKillOrni);
    
    if(combatStats.turretRocketsSpawned > 0) {
        const double hitRate = static_cast<double>(combatStats.turretRocketsHitOrni) / combatStats.turretRocketsSpawned * 100.0;
        const double killRate = static_cast<double>(combatStats.turretRocketsKillOrni) / combatStats.turretRocketsSpawned * 100.0;
        SDL_Log("[Combat Stats]   Hit Rate:  %.1f%% (%d hits / %d rockets)", 
                hitRate, combatStats.turretRocketsHitOrni, combatStats.turretRocketsSpawned);
        SDL_Log("[Combat Stats]   Kill Rate: %.1f%% (%d kills / %d rockets)", 
                killRate, combatStats.turretRocketsKillOrni, combatStats.turretRocketsSpawned);
    }
    
    // ===== LAUNCHER UNITS =====
    SDL_Log("[Combat Stats] ");
    SDL_Log("[Combat Stats] === LAUNCHER/DEVIATOR UNITS vs ORNITHOPTERS ===");
    SDL_Log("[Combat Stats] TARGETING:");
    SDL_Log("[Combat Stats]   Ornithopters Acquired as Target:  %d", combatStats.launcherTargetsOrni);
    SDL_Log("[Combat Stats]   Shots Fired at Ornithopters:      %d", combatStats.launcherFiresOnOrni);
    
    SDL_Log("[Combat Stats] ROCKET PERFORMANCE:");
    SDL_Log("[Combat Stats]   Rockets Spawned:          %d", combatStats.launcherRocketsSpawned);
    SDL_Log("[Combat Stats]   Timer Expirations:        %d", combatStats.launcherRocketsExpired);
    SDL_Log("[Combat Stats]   Rockets Hit Ornithopter:  %d", combatStats.launcherRocketsHitOrni);
    SDL_Log("[Combat Stats]   Rockets Killed Ornithopter: %d", combatStats.launcherRocketsKillOrni);
    
    if(combatStats.launcherRocketsSpawned > 0) {
        const double hitRate = static_cast<double>(combatStats.launcherRocketsHitOrni) / combatStats.launcherRocketsSpawned * 100.0;
        const double killRate = static_cast<double>(combatStats.launcherRocketsKillOrni) / combatStats.launcherRocketsSpawned * 100.0;
        SDL_Log("[Combat Stats]   Hit Rate:  %.1f%% (%d hits / %d rockets)", 
                hitRate, combatStats.launcherRocketsHitOrni, combatStats.launcherRocketsSpawned);
        SDL_Log("[Combat Stats]   Kill Rate: %.1f%% (%d kills / %d rockets)", 
                killRate, combatStats.launcherRocketsKillOrni, combatStats.launcherRocketsSpawned);
    }
    
    // Reset stats
    combatStats.rocketTurretTargetsOrni = 0;
    combatStats.rocketTurretLosesOrniTarget = 0;
    combatStats.orniInRangeButWrongAngle = 0;
    combatStats.orniInRangeCorrectAngle = 0;
    combatStats.rocketTurretFiresOnOrni = 0;
    combatStats.rocketTurretFireBlocked = 0;
    combatStats.turretRocketsSpawned = 0;
    combatStats.turretRocketsHitOrni = 0;
    combatStats.turretRocketsKillOrni = 0;
    combatStats.turretRocketsExpired = 0;
    combatStats.turretRocketsProximityDetonated = 0;
    combatStats.launcherTargetsOrni = 0;
    combatStats.launcherFiresOnOrni = 0;
    combatStats.launcherRocketsSpawned = 0;
    combatStats.launcherRocketsHitOrni = 0;
    combatStats.launcherRocketsKillOrni = 0;
    combatStats.launcherRocketsExpired = 0;
    SDL_Log("[Combat Stats] ================================================================================");
}

void Game::drawMovementPaths() {
    SDL_Rect previousClip{};
    const bool clipped=SDL_RenderIsClipEnabled(renderer);
    SDL_RenderGetClipRect(renderer,&previousClip);
    SDL_Rect field{0,topBarPos.h,sideBarPos.x,getRendererHeight()-topBarPos.h};
    if(clipped)SDL_IntersectRect(&field,&previousClip,&field);
    SDL_RenderSetClipRect(renderer,&field);
    Uint8 r,g,b,a; SDL_GetRenderDrawColor(renderer,&r,&g,&b,&a);
    int count=0;
    for(Uint32 id:selectedList) {
        const auto* unit=dynamic_cast<const UnitBase*>(objectManager.getObject(id));
        if(!unit || !unit->isActive() || unit->getOwner()!=pLocalHouse || unit->getDestination().isInvalid()
            || unit->getDestination()==unit->getLocation())continue;
        if(++count>64)break;
        Coord previous(screenborder->world2screenX(unit->getRealX().lround()),screenborder->world2screenY(unit->getRealY().lround()));
        auto segment=[&](Coord tile,bool pending) {
            const Coord next(screenborder->world2screenX(tile.x*TILESIZE+TILESIZE/2),screenborder->world2screenY(tile.y*TILESIZE+TILESIZE/2));
            SDL_SetRenderDrawColor(renderer,0,0,0,255);
            SDL_RenderDrawLine(renderer,previous.x,previous.y+1,next.x,next.y+1);
            SDL_SetRenderDrawColor(renderer,pending?180:80,230,255,255);
            SDL_RenderDrawLine(renderer,previous.x,previous.y,next.x,next.y);
            previous=next;
        };
        const auto& route=unit->getPlannedPath();
        if(route.empty())segment(unit->getDestination(),true);
        else { int steps=0; for(Coord tile:route) { if(++steps>512)break; segment(tile,false); } }
        SDL_Rect marker{previous.x-2,previous.y-2,5,5};SDL_RenderDrawRect(renderer,&marker);
    }
    SDL_SetRenderDrawColor(renderer,r,g,b,a);
    SDL_RenderSetClipRect(renderer,clipped ? &previousClip : nullptr);
}

void Game::toggleMovementPaths() {
    settings.general.showMovementPaths = !settings.general.showMovementPaths;
    INIFile config(getConfigFilepath());
    config.setBoolValue("General","Movement Paths",settings.general.showMovementPaths);
    config.saveChangesTo(getConfigFilepath());
    WebRuntime::syncPersistentFiles();
}

bool Game::isSpectating() const { return pNetworkManager && pNetworkManager->isSpectating(); }

void Game::setupSpectatorView() {
    // Observation has no simulation player, controller slot or ownership. The UI's
    // legacy non-null HumanPlayer pointer uses a detached, unregistered view object.
    pLocalHouse=nullptr;
    for(const auto& h : house) if(h && !h->getPlayerList().empty()) { pLocalHouse=h.get(); break; }
    if(!pLocalHouse) THROW(std::runtime_error,"There is no house to observe.");
    spectatorViewPlayer=std::make_unique<HumanPlayer>(pLocalHouse,getLocalPlayerName());
    pLocalPlayer=spectatorViewPlayer.get();
}


std::vector<Game::JoinSlot> Game::availableJoinSlots() const {
    std::vector<JoinSlot> result;
    const bool coop=isCoopGameType(gameType);
    const bool shared=coop || gameInitSettings.isMultiplePlayersPerHouse();
    for(int h=0;h<NUM_HOUSES;++h) {
        const auto* target=house[h].get();
        if(!target || !target->isAlive() || (coop && h!=gameInitSettings.getHouseID())) continue;
        const auto& controllers=target->getPlayerList();
        if(controllers.empty()) continue;
        if(shared && controllers.size()<2)
            result.push_back({h,static_cast<int>(controllers.size()),"Share with "+controllers.front()->getPlayername()+" (keep existing player)"});
        int index=0;
        for(const auto& p : controllers) {
            if(dynamic_cast<HumanPlayer*>(p.get())==nullptr)
                result.push_back({h,index,"Replace "+p->getPlayername()+" (remove AI)"});
            ++index;
        }
    }
    return result;
}

bool Game::acceptJoinRequest(const std::string& request, const std::string& name, const JoinSlot& slot, bool spectator) {
    if(!pNetworkManager || !pNetworkManager->isServer() || pNetworkManager->lateJoinPaused()) return false;
    if(spectator) return pNetworkManager->getDirectTransport() && pNetworkManager->getDirectTransport()->manageJoin("approve_spectator",request);
    const auto choices=availableJoinSlots();
    if(!spectator && std::none_of(choices.begin(),choices.end(),[&](const auto& s){return s.house==slot.house && s.controller==slot.controller;})) return false;
    try {
        OMemoryStream stream; stream.open(); saveGame(stream);
        auto snapshot=gameInitSettings.networkSnapshot(std::string(stream.getData(),stream.getDataLength()));
        snapshot.setMultiplePlayersPerHouse(gameInitSettings.isMultiplePlayersPerHouse() || isCoopGameType(gameType));
        for(int h=0;h<NUM_HOUSES;++h) {
            const auto* target=house[h].get(); if(!target || target->getPlayerList().empty()) continue;
            GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(h),target->getTeamID());
            info.colorOfHouse=getHouseVisualHouse(h);
            // Rebuilt from the live house, so the factor has to come from there too: these
            // rows are resolved houses, and the joining peer must see the rate this match is
            // actually being paid at.
            info.spiceIncomeMultiplier=target->getSpiceIncomeMultiplier();
            int index=0;
            for(const auto& p : target->getPlayerList()) {
                info.addPlayerInfo(GameInitSettings::PlayerInfo(!spectator && h==slot.house && index==slot.controller ? name : p->getPlayername(),
                    !spectator && h==slot.house && index==slot.controller ? HUMANPLAYERCLASS : p->getPlayerclass()));
                ++index;
            }
            if(!spectator && h==slot.house && slot.controller==index) info.addPlayerInfo(GameInitSettings::PlayerInfo(name,HUMANPLAYERCLASS));
            snapshot.addHouseInfo(info);
        }
        return pNetworkManager->beginLateJoin(request,name,snapshot,spectator);
    } catch(const std::exception& e) { addToNewsTicker(std::string("Could not prepare the join: ")+e.what()); return false; }
}

GameInitSettings Game::spectatorSnapshot() {
    OMemoryStream save; save.open(); saveGame(save);
    auto snapshot=gameInitSettings.networkSnapshot(std::string(save.getData(),save.getDataLength()));
    snapshot.setMultiplePlayersPerHouse(gameInitSettings.isMultiplePlayersPerHouse() || isCoopGameType(gameType));
    for(int h=0;h<NUM_HOUSES;++h) {
        const auto* target=house[h].get(); if(!target || target->getPlayerList().empty()) continue;
        GameInitSettings::HouseInfo info(static_cast<HOUSETYPE>(h),target->getTeamID());
        info.colorOfHouse=getHouseVisualHouse(h);
        // As in acceptJoinRequest: a spectator reconstructs houses from this snapshot and has
        // to pay out at the same rate as the players it is watching.
        info.spiceIncomeMultiplier=target->getSpiceIncomeMultiplier();
        for(const auto& player : target->getPlayerList()) info.addPlayerInfo({player->getPlayername(),player->getPlayerclass()});
        snapshot.addHouseInfo(info);
    }
    return snapshot;
}

void Game::prepareObserverStreams() {
    const auto pending=pNetworkManager->observersNeedingSnapshot();
    if(pending.empty()) return;
    try {
        // One capture shared by viewers admitted together; no player is reloaded or paused.
        const auto snapshot=spectatorSnapshot(); const auto runtime=saveObserverRuntime();
        for(const auto peer : pending) if(!pNetworkManager->beginObserverSnapshot(peer,snapshot,runtime,gameCycleCount))
            pNetworkManager->getDirectTransport()->disconnectSpectator(peer,"This game is too large to spectate.");
    } catch(const std::exception& error) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"Spectator checkpoint preparation failed: %s",error.what());
        for(const auto peer : pending) pNetworkManager->getDirectTransport()->disconnectSpectator(peer,"Could not prepare the spectator view.");
    }
}

std::string Game::saveObserverRuntime() const {
    OMemoryStream out; out.open();
    // Version 6 adds suspended pathfinding continuations. A spectator must resume
    // the searches the host has already partly paid for: flushing them on the host
    // instead would be a mutation no peer performs, so the stream carries the whole
    // continuation state rather than a restart hint.
    //
    // Version 7 adds each unit's long-stall rescue clock (UnitBase::saveObserverRuntime).
    // That clock is thirty seconds of simulation time long, so restarting it on the spectator
    // would hand every already-stalled unit a grace period the host is not giving it and the two
    // would then book transport on different cycles.
    out.writeUint32(7); out.writeUint32(gameCycleCount);
    out.writeUint32(negotiatedBudget); out.writeUint32(cmdManager.getNetworkCycleBuffer());
    out.writeUint32(currentGameMap->getPathingRevision());
    out.writeUint32(carryOverTokens);
    out.writeUint32(targetRequestQueue.size());
    for(const auto& request : targetRequestQueue) out.writeUint32(request.objectId);
    out.writeUint32(pathRequestQueue.size());
    for(const auto& request : pathRequestQueue) out.writeUint32(request.objectId);
    // Scheduler order is part of the state: it is the batch, quota and
    // application order.
    out.writeUint32(activePathJobs.size());
    for(const auto& job : activePathJobs) {
        out.writeUint32(job.objectId);
        out.writeSint32(job.destinationCoord.x); out.writeSint32(job.destinationCoord.y);
        out.writeUint32(job.chargedNodes);
        out.writeUint32(job.restarts);
        // Every field the validation compares, or a restored job would see a
        // difference the live one does not and requeue a search that should have
        // continued.
        out.writeSint32(job.inputs.location.x); out.writeSint32(job.inputs.location.y);
        out.writeSint32(job.inputs.destination.x); out.writeSint32(job.inputs.destination.y);
        out.writeUint32(job.inputs.pathingRevision);
        out.writeUint32(job.inputs.targetObjectID);
        out.writeSint32(job.inputs.houseID); out.writeSint32(job.inputs.teamID);
        out.writeBools(job.inputs.targetFriendly, job.inputs.goingToRepairYard, job.inputs.repairYardFree);
        out.writeBools(job.inputs.targetPresent, job.inputs.targetIsStructure, job.inputs.targetVisibleToUs);
        out.writeSint32(job.inputs.targetOwnerTeamID);
        out.writeUint32(job.inputs.targetItemID);
        out.writeUint32(job.inputs.itemID);
        out.writeSint32(job.inputs.originalHouseID);
        out.writeBool(job.search != nullptr);
        if(job.search != nullptr) job.search->save(out);
    }
    out.writeUint32(unitList.size());
    for(const auto* unit : unitList) { out.writeUint32(unit->getObjectID()); unit->saveObserverRuntime(out); }
    std::vector<const QuantBot*> bots;
    for(const auto& h : house) if(h) for(const auto& p : h->getPlayerList())
        if(const auto* bot=dynamic_cast<const QuantBot*>(p.get())) bots.push_back(bot);
    out.writeUint32(bots.size());
    for(const auto* bot : bots) { out.writeUint8(bot->getPlayerID()); bot->saveObserverRuntime(out); }
    for(const auto& h : house) {
        out.writeBool(h != nullptr);
        if(h) out.writeBool(h->isAI());
    }
    out.writeBool(citySimulation_ != nullptr);
    if(citySimulation_) citySimulation_->saveObserverRuntime(out);
    out.writeUint32(matchControl.revision);
    out.writeUint32(matchControl.pauseCycle);
    out.writeUint32(matchControl.resumedPauseCycle);
    out.writeUint32(getGameSpeed());
    return std::string(out.getData(),out.getDataLength());
}

void Game::loadObserverRuntime(const std::string& bytes) {
    IMemoryStream in(bytes.data(),bytes.size());
    const auto runtimeVersion=in.readUint32();
    if((runtimeVersion!=7) || in.readUint32()!=gameCycleCount) throw std::runtime_error("Invalid spectator checkpoint cycle");
    // The checkpoint replaces this instance's unit set, so the derived carrier index describes
    // units that may no longer exist. Rebuilt on first use after this.
    invalidateCarryallCandidateIds();
    negotiatedBudget=in.readUint32(); const auto buffer=in.readUint32();
    if(negotiatedBudget<kMinBudget || negotiatedBudget>kMaxBudget || buffer>1000) throw std::runtime_error("Invalid spectator checkpoint budget");
    cmdManager.setNetworkCycleBuffer(buffer);
    currentGameMap->restoreObserverPathingRevision(in.readUint32());
    carryOverTokens=in.readUint32();
    if(carryOverTokens>kDebtCap) throw std::runtime_error("Invalid spectator checkpoint carry-over");
    targetRequestQueue.clear(); pendingTargetRequestIds.clear(); pathRequestQueue.clear(); pendingPathRequestIds.clear();
    // Whatever this instance was running is replaced by the checkpoint's schedule;
    // those requests end here, which the lifecycle conservation count must see.
    pathRequestsCancelled+=activePathJobs.size();
    activePathJobs.clear();
    const auto targets=in.readUint32(); if(targets>unitList.size()) throw std::runtime_error("Invalid spectator targets");
    for(Uint32 i=0;i<targets;++i) queueTargetRequest(in.readUint32());
    const auto paths=in.readUint32(); if(paths>unitList.size()) throw std::runtime_error("Invalid spectator paths");
    for(Uint32 i=0;i<paths;++i) queuePathRequest(in.readUint32());
    const auto suspended=in.readUint32();
    if(suspended>kPathBatchSize) throw std::runtime_error("Invalid spectator suspended search count");
    for(Uint32 i=0;i<suspended;++i) {
        PathJob job;
        job.objectId=in.readUint32();
        job.destinationCoord.x=in.readSint32(); job.destinationCoord.y=in.readSint32();
        job.chargedNodes=in.readUint32();
        job.restarts=in.readUint32();
        job.inputs.location.x=in.readSint32(); job.inputs.location.y=in.readSint32();
        job.inputs.destination.x=in.readSint32(); job.inputs.destination.y=in.readSint32();
        job.inputs.pathingRevision=in.readUint32();
        job.inputs.targetObjectID=in.readUint32();
        job.inputs.houseID=in.readSint32(); job.inputs.teamID=in.readSint32();
        in.readBools(&job.inputs.targetFriendly,&job.inputs.goingToRepairYard,&job.inputs.repairYardFree);
        in.readBools(&job.inputs.targetPresent,&job.inputs.targetIsStructure,&job.inputs.targetVisibleToUs);
        job.inputs.targetOwnerTeamID=in.readSint32();
        job.inputs.targetItemID=in.readUint32();
        job.inputs.itemID=in.readUint32();
        job.inputs.originalHouseID=in.readSint32();
        if(in.readBool()) job.search=AStarSearch::load(in);
        if(!pendingPathRequestIds.insert(job.objectId).second)
            throw std::runtime_error("Duplicate spectator pending path request");
        // A slot whose unit is absent is a legitimate checkpoint boundary, not bad
        // input: a unit can be destroyed later in the same cycle, after the path
        // phase, and its job survives until the next one. The slot, its frontier
        // and its pending id are preserved so that next phase cancels it in the
        // same fixed order, with the same stats and budget effect, as the
        // uninterrupted run would have. Skipping it only on restore would make the
        // spectator diverge. The unit flag is only set when there is a unit.
        auto* unit=dynamic_cast<UnitBase*>(objectManager.getObject(job.objectId));
        if(unit!=nullptr) unit->markPathRequestActive();
        // These requests were started by the simulation this checkpoint came from,
        // so they count as started here too; otherwise the lifecycle conservation
        // "started == applied + cancelled + requeued + active" would not hold in
        // the restoring instance.
        ++pathRequestsStarted;
        activePathJobs.push_back(std::move(job));
    }
    const auto units=in.readUint32(); if(units!=unitList.size()) throw std::runtime_error("Invalid spectator units");
    std::set<Uint32> seen;
    for(Uint32 i=0;i<units;++i) {
        const auto id=in.readUint32(); auto* unit=dynamic_cast<UnitBase*>(objectManager.getObject(id));
        if(!unit || !seen.insert(id).second) throw std::runtime_error("Invalid spectator unit");
        unit->loadObserverRuntime(in);
    }
    const auto bots=in.readUint32(); if(bots>NUM_HOUSES*2) throw std::runtime_error("Too many spectator controllers");
    std::set<Uint8> seenBots;
    for(Uint32 i=0;i<bots;++i) {
        const auto id=in.readUint8(); auto* bot=dynamic_cast<QuantBot*>(getPlayerByID(id));
        if(!bot || !seenBots.insert(id).second) throw std::runtime_error("Invalid spectator AI");
        bot->loadObserverRuntime(in);
    }
    for(const auto& h : house) {
        if(in.readBool() != (h != nullptr)) throw std::runtime_error("Invalid spectator house state");
        if(h) h->restoreObserverAI(in.readBool());
    }
    if(in.readBool() != (citySimulation_ != nullptr)) throw std::runtime_error("Invalid spectator city state");
    if(citySimulation_) citySimulation_->loadObserverRuntime(in);
    if(runtimeVersion>=4) {
        matchControl.revision=in.readUint32(); matchControl.pauseCycle=in.readUint32();
        matchControl.resumedPauseCycle=in.readUint32(); const auto speed=in.readUint32();
        if(matchControl.resumedPauseCycle>matchControl.pauseCycle || speed<GAMESPEED_MIN || speed>GAMESPEED_MAX)
            throw std::runtime_error("Invalid spectator match controls");
        gameInitSettings.setGameSpeed(int(speed));
    }
    // Zone constructors restore occupancy but do not register its dynamic power
    // draw. Rebuild that derived house total without running city growth.
    for(auto* structure : structureList)
        if(auto* zone=dynamic_cast<ZoneStructure*>(structure)) zone->refreshZonePowerDraw();
    if(in.getRemainingLength()!=0) throw std::runtime_error("Extra spectator checkpoint data");
}
