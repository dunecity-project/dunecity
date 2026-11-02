// Production-engine fixture, included by the Civ Wars probe with -fno-access-control.
// Call on an initialized generated map. The full save/load replaces the owning Game;
// callers must reacquire any object/house pointers afterwards.
#include <Game.h>
#include <House.h>
#include <Map.h>
#include <globals.h>
#include <misc/IMemoryStream.h>
#include <misc/OMemoryStream.h>
#include <units/SandWorm.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace {
void civWormRequire(bool value, const char* message) {
    if(!value) throw std::runtime_error(message);
}
}

void runCivWarsWormRespawnProbe(std::unique_ptr<Game>& game, const std::string& savePath) {
    civWormRequire(game && currentGame == game.get() && currentGameMap,
                   "Civ Wars worm fixture needs the current initialized game");
    civWormRequire(game->getGameInitSettings().isCivilizationWars(),
                   "Civ Wars worm fixture needs the exact generated-map marker");
    civWormRequire(SAVEGAMEVERSION >= 9881, "Civ Wars origins require the shared 9881 save bump");
    const auto originalOptions = game->gameInitSettings.getGameOptions();
    auto options = originalOptions;
    options.sandwormsRespawn = true;
    options.killedSandwormsDropSpice = false;
    game->gameInitSettings.setGameOptions(options);

    const auto freeSand = [](const Coord& at) {
        if(!currentGameMap->tileExists(at)) return false;
        const auto* tile = currentGameMap->getTile(at);
        return tile->isSand() && !tile->hasAGroundObject() && !tile->hasAnUndergroundUnit();
    };
    Coord leftAt = Coord::Invalid();
    Coord rightAt = Coord::Invalid();
    for(int y = 1; y + 1 < currentGameMap->getSizeY() && leftAt.isInvalid(); ++y) {
        for(int x = 1; x < currentGameMap->getSizeX() / 2 && leftAt.isInvalid(); ++x) {
            const Coord left(x, y), right(currentGameMap->getSizeX() - 1 - x, y);
            if(freeSand(left) && freeSand(right)) { leftAt = left; rightAt = right; }
        }
    }
    civWormRequire(leftAt.isValid(), "No free mirrored sand slots for worm fixture");
    Coord wanderedAt = Coord::Invalid();
    for(int y = 1; y + 1 < currentGameMap->getSizeY() && wanderedAt.isInvalid(); ++y) {
        for(int x = 1; x + 1 < currentGameMap->getSizeX() && wanderedAt.isInvalid(); ++x) {
            const Coord at(x, y);
            if(at != leftAt && at != rightAt && freeSand(at)
               && currentGameMap->getTile(at)->getSandRegion()
                   == currentGameMap->getTile(leftAt)->getSandRegion()) wanderedAt = at;
        }
    }
    civWormRequire(wanderedAt.isValid(), "No connected sand tile for worm movement fixture");
    const auto makeWorm = [&]() {
        auto* worm = dynamic_cast<Sandworm*>(game->getOrCreateNativesHouse()->createUnit(Unit_Sandworm, true));
        civWormRequire(worm != nullptr, "Cannot create Civ Wars fixture worm");
        return worm;
    };
    auto* left = makeWorm();
    auto* right = makeWorm();
    left->deploy(Coord::Invalid());
    civWormRequire(left->getRespawnOrigin().isInvalid(), "Invalid deployment captured a worm slot");
    left->deploy(leftAt);
    right->deploy(rightAt);
    civWormRequire(left->getRespawnOrigin() == leftAt && right->getRespawnOrigin() == rightAt,
                   "Initial deployments did not capture mirrored permanent worm slots");
    const auto leftID = left->getObjectID(), rightID = right->getObjectID();

    // A later location or guard-point change is not a new spawn origin.
    left->unassignFromMap(leftAt);
    left->setLocation(wanderedAt);
    left->setGuardPoint(wanderedAt.x, wanderedAt.y);
    civWormRequire(left->getRespawnOrigin() == leftAt, "Movement replaced the permanent worm slot");
    left->setHealth(left->getMaxHealth() / 2);
    civWormRequire(left->update(), "Defeated Civ Wars worm was destroyed instead of sleeping");
    civWormRequire(!left->isActive() && left->getLocation().isInvalid() && left->getSleepTimer() > 0
                       && left->getRespawnOrigin() == leftAt,
                   "Defeat sleep discarded the permanent worm slot");

    // Enter the real final eating-animation boundary with the three-meal threshold met.
    right->doSetAttackMode(STOP);
    right->kills = 3;
    right->drawnFrame = 8;
    right->attackFrameTimer = 1;
    civWormRequire(right->update(), "Satiated Civ Wars worm was destroyed instead of sleeping");
    civWormRequire(!right->isActive() && right->getLocation().isInvalid() && right->getSleepTimer() > 0
                       && right->getRespawnOrigin() == rightAt,
                   "Eating sleep discarded the permanent worm slot");

    const auto wakeAtSlot = [&](Sandworm* worm, const Coord& at) {
        const auto seed = game->randomGen.getSeed();
        worm->sleepTimer = 1;
        civWormRequire(worm->update() && worm->isActive() && worm->getLocation() == at,
                       "Civ Wars worm did not wake at its permanent slot");
        civWormRequire(worm->getRespawnOrigin() == at && game->randomGen.getSeed() == seed,
                       "Civ Wars wake changed its slot or consumed map-wide position RNG");
    };
    const auto blockedWake = [&](Sandworm* worm, const Coord& at) {
        auto expectedRandom = game->randomGen;
        expectedRandom.rand();  // The unchanged random nap duration is one draw, never x/y draws.
        worm->sleepTimer = 1;
        civWormRequire(worm->update() && !worm->isActive() && worm->getLocation().isInvalid()
                           && worm->getSleepTimer() > 0 && worm->getRespawnOrigin() == at,
                       "Blocked Civ Wars worm abandoned its permanent slot");
        civWormRequire(game->randomGen.getSeed() == expectedRandom.getSeed(),
                       "Blocked Civ Wars wake attempted map-wide RNG fallback");
    };
    wakeAtSlot(left, leftAt);
    left->sleep();
    auto* undergroundBlocker = makeWorm();
    undergroundBlocker->deploy(leftAt);
    blockedWake(left, leftAt);
    undergroundBlocker->cancelDeployment();
    wakeAtSlot(left, leftAt);

    left->sleep();
    House* surfaceOwner = nullptr;
    for(int id = 0; id < NUM_HOUSES && surfaceOwner == nullptr; ++id) {
        auto* house = game->getHouse(id);
        if(house && !house->isNatives()) surfaceOwner = house;
    }
    civWormRequire(surfaceOwner != nullptr, "No playing house for occupied-slot fixture");
    auto* surfaceBlocker = surfaceOwner->createUnit(Unit_Tank, true);
    civWormRequire(surfaceBlocker != nullptr, "Cannot create occupied-slot ground unit");
    surfaceBlocker->deploy(leftAt);
    blockedWake(left, leftAt);
    surfaceBlocker->cancelDeployment();

    // A pre-9881 object record has no origin bytes: do not consume the next record.
    OMemoryStream record;
    record.open();
    left->save(record);
    civWormRequire(record.getDataLength() >= 8, "Worm record omitted its two origin coordinates");
    std::string legacyBytes(record.getData(), record.getDataLength() - 8);
    const std::string tail = "slot-tail";
    legacyBytes += tail;
    const auto loadedVersion = game->loadedSavegameVersion;
    game->loadedSavegameVersion = 9880;
    IMemoryStream legacyInput(legacyBytes.data(), legacyBytes.size());
    auto* legacy = new Sandworm(legacyInput);
    legacy->setObjectID(game->getObjectManager().addObject(legacy));
    game->loadedSavegameVersion = loadedVersion;
    civWormRequire(legacy->getRespawnOrigin().isInvalid()
                       && legacy->getSummoner() == left->getSummoner()
                       && legacyInput.getRemainingLength() == tail.size()
                       && legacyInput.readUint8() == static_cast<Uint8>('s'),
                   "Pre-9881 worm load consumed following bytes or fabricated an origin");
    legacy->cancelDeployment();

    const auto leftSleep = left->getSleepTimer(), rightSleep = right->getSleepTimer();
    civWormRequire(game->saveGame(savePath), "Cannot save sleeping Civ Wars worms");
    game.reset();
    currentGame = nullptr;
    pLocalPlayer = nullptr;
    pLocalHouse = nullptr;
    game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;
    game->initGame(GameInitSettings(savePath));
    game->bReplay = false;
    game->initializeGameLoop();
    civWormRequire(game->getLoadedSavegameVersion() >= 9881
                       && game->getGameInitSettings().isCivilizationWars(),
                   "Save/load lost the Civ Wars generated-map mode");
    left = dynamic_cast<Sandworm*>(game->getObjectManager().getObject(leftID));
    right = dynamic_cast<Sandworm*>(game->getObjectManager().getObject(rightID));
    civWormRequire(left && right && left->getRespawnOrigin() == leftAt && right->getRespawnOrigin() == rightAt
                       && !left->isActive() && !right->isActive()
                       && left->getLocation().isInvalid() && right->getLocation().isInvalid()
                       && left->getSleepTimer() == leftSleep && right->getSleepTimer() == rightSleep,
                   "Sleeping worm slots/timers did not survive the full engine save/load");
    wakeAtSlot(left, leftAt);
    wakeAtSlot(right, rightAt);
    left->cancelDeployment();
    right->cancelDeployment();

    // The exact same engine, mod and terrain without the map marker must retain
    // its original 1000-attempt random respawn and its exact shared RNG state.
    const auto generatedMap = game->gameInitSettings.getFiledata();
    game->gameInitSettings.setMapData("[BASIC]\nName=Own scenario without lane rules\n");
    civWormRequire(!game->gameInitSettings.isCivilizationWars(), "Map replacement retained cached lane rules");
    auto* ordinary = makeWorm();
    ordinary->deploy(leftAt);
    civWormRequire(ordinary->getRespawnOrigin().isInvalid(), "Ordinary worm captured a Civ Wars slot");
    ordinary->sleep();
    ordinary->sleepTimer = 1;
    auto expectedRandom = game->randomGen;
    Coord expectedAt = Coord::Invalid();
    for(int tries = 0; tries < 1000; ++tries) {
        const int x = expectedRandom.rand(0, currentGameMap->getSizeX() - 1);
        const int y = expectedRandom.rand(0, currentGameMap->getSizeY() - 1);
        if(ordinary->canPass(x, y)) { expectedAt = Coord(x, y); break; }
    }
    civWormRequire(expectedAt.isValid(), "Ordinary respawn fixture found no random legal tile");
    civWormRequire(ordinary->update() && ordinary->isActive() && ordinary->getLocation() == expectedAt
                       && ordinary->getRespawnOrigin().isInvalid()
                       && game->randomGen.getSeed() == expectedRandom.getSeed(),
                   "Unmarked scenario changed ordinary worm respawn or RNG progression");
    ordinary->cancelDeployment();
    game->gameInitSettings.setMapData(generatedMap);
    game->gameInitSettings.setGameOptions(originalOptions);
    civWormRequire(game->gameInitSettings.isCivilizationWars(), "Restored map did not invalidate the negative mode cache");
    SDL_Log("CIV_WARS_WORM_RESPAWN_PASS: mirrored slots, movement, defeat, eating, blocked retries, save/load, 9880 gate, ordinary RNG");
}
