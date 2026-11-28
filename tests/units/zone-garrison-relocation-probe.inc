// Included inside ZoneGarrisonProbe, after its shared fixture and player-input helpers.
// Exercise real game cycles: shelter departure is a reserved walk, not a teleport or an
// AI-only evacuation. Capacity, failed-attempt persistence and ownership changes are observable.

FixPoint relocationThreshold(const ZoneStructure* zone) {
    return FixPoint(zone->getMaxHealth()) * 30 / 100;
}

std::vector<UnitBase*> relocationPair(Fixture& f, ZoneStructure* source, bool playerClick = false) {
    const Coord at = source->getLocation();
    std::vector<UnitBase*> units = {
        f.unit(source->getOwner(), Unit_Soldier, at.x - 1, at.y),
        f.unit(source->getOwner(), Unit_Trooper, at.x - 1, at.y + 1)
    };
    if(playerClick) {
        f.run(1);
        require(captureByPlayer(f, units, source, false, true) == 2,
                f.label + ": the real Capture button/map click did not order both occupants");
    } else {
        for(UnitBase* unit : units) f.capture(unit, source);
    }
    require(f.runUntil(20, [&] { return source->getGarrisonSize() == 2; }),
            f.label + ": both occupants did not enter the source");
    for(const UnitBase* unit : units) require(hidden(unit), f.label + ": a source occupant is not hidden");
    return units;
}

void fillRelocationDestination(Fixture& f, ZoneStructure* destination, int count) {
    const Coord at = destination->getLocation();
    for(int i = 0; i < count; ++i) {
        auto* unit = f.unit(destination->getOwner(), Unit_Soldier, at.x - 1, at.y + i % 2);
        f.capture(unit, destination);
    }
    require(f.runUntil(20, [&] { return destination->getGarrisonSize() == count; }),
            f.label + ": destination occupants did not enter");
}

// Soldiers have half the configured speed cap of Troopers. Keep the fixture legs compact,
// rather than weakening the 45-second bound or the real walking/arrival assertions.
void requireRelocationWalk(Fixture& f, const std::vector<UnitBase*>& units, ZoneStructure* destination) {
    const int expected = destination->getGarrisonSize() + static_cast<int>(units.size());
    std::vector<Coord> exits;
    std::vector<bool> walked(units.size(), false);
    for(const UnitBase* unit : units) {
        require(unit->isActive() && !unit->isPickedUp()
                    && unit->getGarrisonRelocationTarget() == destination->getObjectID(),
                f.label + ": departure did not leave a living unit with a destination reservation");
        exits.push_back(unit->getLocation());
    }
    require(f.runUntil(45, [&] {
        for(size_t i = 0; i < units.size(); ++i)
            if(units[i]->isActive() && units[i]->getLocation() != exits[i]) walked[i] = true;
        return destination->getGarrisonSize() == expected;
    }), f.label + ": the reserved occupants never arrived");
    for(size_t i = 0; i < units.size(); ++i) {
        require(walked[i], f.label + ": an occupant never made an on-map walking step");
        require(hidden(units[i]) && units[i]->getGarrisonRelocationTarget() == NONE_ID
                    && std::find(destination->getGarrison().begin(), destination->getGarrison().end(),
                                 units[i]->getObjectID()) != destination->getGarrison().end(),
                f.label + ": arrival did not hide the original unit and release its reservation");
    }
}

template<class F> bool runRelocationUntil(Game& game, int seconds, F done) {
    const Uint32 end = game.getGameCycleCount() + MILLI2CYCLES(seconds * 1000);
    while(game.getGameCycleCount() < end) {
        if(done()) return true;
        game.updateGameState();
    }
    return done();
}

std::unique_ptr<Game> loadRelocationGame(const std::string& path) {
    pLocalPlayer = nullptr;
    pLocalHouse = nullptr;
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;
    game->initGame(GameInitSettings(path));
    game->bReplay = false;
    game->initializeGameLoop();
    require(game->getLoadedSavegameVersion() >= SAVEGAMEVERSION_ZONE_GARRISON_SHELTER,
            "relocation save: the shelter save gate was not used");
    return game;
}

void checkGarrisonRelocationWalking() {
    Fixture f("relocation walking");
    auto* source = f.zone(f.own, Structure_ZoneResidential, 20, 20);
    auto* destination = f.zone(f.own, Structure_ZoneCommercial, 31, 20);
    auto units = relocationPair(f, source, true);
    const FixPoint firstHealth = units[0]->getHealth();
    const FixPoint secondHealth = units[1]->getHealth();
    source->setHealth(relocationThreshold(source));
    f.run(1);
    require(source->getGarrisonSize() == 2 && !source->hasAttemptedGarrisonRelocation()
                && units[0]->getHealth() == firstHealth && units[1]->getHealth() == secondHealth,
            "relocation walking: exactly 30% changed the occupants or attempted relocation");
    // One raw fixed-point unit below the boundary must trigger even when lround() is unchanged.
    source->setHealth(relocationThreshold(source) - FixPoint::FromRawValue(1));
    f.game->updateGameState();
    require(source->getGarrisonSize() == 0 && source->hasAttemptedGarrisonRelocation()
                && destination->getGarrisonSize() == 0,
            "relocation walking: fractional below-30% did not depart, or teleported into the destination");
    requireRelocationWalk(f, units, destination);
    record("relocation_walking", "real Capture button; 2 held at exactly30; fractional drop; 2 walk; 2 arrive");
}

void checkGarrisonRelocationBlocked() {
    Fixture f("relocation blocked");
    auto* source = f.zone(f.own, Structure_ZoneResidential, 20, 30);
    auto* blocked = f.zone(f.own, Structure_ZoneCommercial, 26, 30);
    auto* reachable = f.zone(f.own, Structure_ZoneIndustrial, 29, 30);
    const Coord at = blocked->getLocation();
    for(int y = at.y - 1; y <= at.y + blocked->getStructureSizeY(); ++y) {
        for(int x = at.x - 1; x <= at.x + blocked->getStructureSizeX(); ++x) {
            if(x >= at.x && x < at.x + blocked->getStructureSizeX()
               && y >= at.y && y < at.y + blocked->getStructureSizeY()) continue;
            require(f.own->placeStructure(NONE_ID, Structure_Wall, x, y, true, true) != nullptr,
                    "relocation blocked: could not seal the nearest destination");
        }
    }
    auto units = relocationPair(f, source);
    source->setHealth(relocationThreshold(source) - 1);
    f.game->updateGameState();
    require(source->getGarrisonSize() == 0 && blocked->getGarrisonSize() == 0,
            "relocation blocked: the source did not leave or selected the sealed destination");
    for(UnitBase* unit : units)
        require(unit->getGarrisonRelocationTarget() == reachable->getObjectID(),
                "relocation blocked: nearest blocked destination was not skipped");
    requireRelocationWalk(f, units, reachable);
    record("relocation_blocked", "2 skip a nearer wall-sealed shelter and walk into the reachable one");
}

void checkGarrisonRelocationTie() {
    Fixture f("relocation tie");
    auto* source = f.zone(f.own, Structure_ZoneResidential, 24, 20);
    auto* first = f.zone(f.own, Structure_ZoneIndustrial, 34, 20);
    auto* second = f.zone(f.own, Structure_ZoneCommercial, 14, 20);
    require(first->getObjectID() < second->getObjectID(), "relocation tie: fixture IDs are not ordered");
    auto units = relocationPair(f, source);
    source->setHealth(relocationThreshold(source) - 1);
    f.game->updateGameState();
    for(UnitBase* unit : units)
        require(unit->getGarrisonRelocationTarget() == first->getObjectID(),
                "relocation tie: equal-distance selection did not use the lower stable object ID");
    requireRelocationWalk(f, units, first);
    require(second->getGarrisonSize() == 0, "relocation tie: the higher-ID shelter received an occupant");
    record("relocation_tie", "equal map distance chooses lower object ID for both occupants");
}

void checkGarrisonRelocationNoRoom() {
    Fixture f("relocation no room");
    auto* source = f.zone(f.own, Structure_ZoneResidential, 20, 40);
    auto* full = f.zone(f.own, Structure_ZoneCommercial, 30, 40);
    fillRelocationDestination(f, full, DuneCity::ZoneGarrison::kCapacity);
    auto units = relocationPair(f, source);
    source->setHealth(relocationThreshold(source) - 1);
    f.run(1);
    require(source->getGarrisonSize() == 2 && source->hasAttemptedGarrisonRelocation(),
            "relocation no room: occupants were ejected despite no room");
    for(const UnitBase* unit : units) require(hidden(unit), "relocation no room: a soldier is outside");
    auto* newlyAvailable = f.zone(f.own, Structure_ZoneIndustrial, 40, 40);
    Command(f.playerOf(f.own), CMD_ZONE_EVACUATE, full->getObjectID()).executeCommand();
    require(full->getGarrisonSize() == 0, "relocation no room: manual evacuation did not free the destination");
    f.run(3);
    require(source->getGarrisonSize() == 2 && newlyAvailable->getGarrisonSize() == 0,
            "relocation no room: a new destination or newly free room caused a second attempt");
    for(const UnitBase* unit : units) require(hidden(unit), "relocation no room: a latched occupant left later");
    record("relocation_no_room", "2 stay hidden with full destination; new shelter/free room do not retry");
}

void checkGarrisonRelocationReservations() {
    Fixture f("relocation reservations");
    auto* first = f.zone(f.own, Structure_ZoneResidential, 20, 20);
    auto* second = f.zone(f.own, Structure_ZoneIndustrial, 20, 28);
    auto* destination = f.zone(f.own, Structure_ZoneCommercial, 29, 24);
    fillRelocationDestination(f, destination, 3);
    auto firstPair = relocationPair(f, first);
    auto secondPair = relocationPair(f, second);
    first->setHealth(relocationThreshold(first) - 1);
    second->setHealth(relocationThreshold(second) - 1);
    f.game->updateGameState();
    int reservations = 0;
    std::vector<UnitBase*> walking;
    for(UnitBase* unit : {firstPair[0], firstPair[1], secondPair[0], secondPair[1]}) {
        if(unit->getGarrisonRelocationTarget() == destination->getObjectID()) {
            ++reservations;
            walking.push_back(unit);
        }
    }
    require(reservations == 2 && first->getGarrisonSize() + second->getGarrisonSize() == 2
                && destination->getGarrisonSize() == 3,
            "relocation reservations: in-flight units did not consume the two remaining places");
    requireRelocationWalk(f, walking, destination);
    require(destination->getGarrisonSize() == 5
                && first->getGarrisonSize() + second->getGarrisonSize() == 2,
            "relocation reservations: capacity was exceeded or unreserved occupants were ejected");
    record("relocation_reservations", "two simultaneous 2-person sources reserve only two remaining places");
}

void checkGarrisonRelocationFriendly() {
    Fixture f("relocation friendly");
    auto* source = f.zone(f.own, Structure_ZoneResidential, 20, 30);
    auto* destination = f.zone(f.ally, Structure_ZoneCommercial, 32, 30);
    fillRelocationDestination(f, destination, 1);
    auto* manual = f.unit(f.own, Unit_Soldier, 29, 30);
    f.capture(manual, destination);
    f.run(1);
    require(destination->getGarrisonSize() == 1 && manual->isActive(),
            "relocation friendly: normal manual ally entry was allowed");
    auto units = relocationPair(f, source, true);
    const int ownStructures = f.own->getNumStructures();
    const int allyStructures = f.ally->getNumStructures();
    source->setHealth(relocationThreshold(source) - 1);
    f.game->updateGameState();
    requireRelocationWalk(f, units, destination);
    require(destination->getGarrisonSize() == 3 && destination->getOwner() == f.ally
                && destination->getGarrisonOriginalOwner() < 0
                && f.own->getNumStructures() == ownStructures && f.ally->getNumStructures() == allyStructures,
            "relocation friendly: sharing shelter transferred ownership or structure counts");
    f.capture(manual, destination);
    f.run(1);
    require(manual->isActive() && destination->getGarrisonSize() == 3,
            "relocation friendly: automatic admission leaked into later manual ally entry");
    // Automatic relocation can leave an own building with an allied crew. Its owner must
    // still be able to reinforce it manually; targeting the ally's building remains refused.
    auto* allySource = f.zone(f.ally, Structure_ZoneIndustrial, 34, 42);
    auto* ownDestination = f.zone(f.own, Structure_ZoneCommercial, 34, 32);
    auto alliedUnits = relocationPair(f, allySource);
    allySource->setHealth(relocationThreshold(allySource) - 1);
    f.game->updateGameState();
    requireRelocationWalk(f, alliedUnits, ownDestination);
    require(ownDestination->getOwner() == f.own && ownDestination->getGarrisonHouse() == f.ally,
            "relocation friendly: own-zone allied-crew precondition did not occur");
    require(captureByPlayer(f, {manual}, ownDestination, false, true) == 1,
            "relocation friendly: the owner could not issue Capture on its allied-crewed building");
    require(f.runUntil(45, [&] { return ownDestination->getGarrisonSize() == 3; })
                && hidden(manual) && manual->getGarrisonRelocationTarget() == NONE_ID
                && ownDestination->getOwner() == f.own,
            "relocation friendly: a normal own-house reinforcement could not join friendly allied occupants");
    record("relocation_friendly", "friendly relocation shares shelter without capture; manual ally target refused; own-zone owner joins allied crew");
}

void checkGarrisonRelocationCrossingSave() {
    const std::string path = gOut + "/zone-garrison-relocation-crossing.dls";
    Uint32 sourceID = NONE_ID;
    std::vector<Uint32> ids;
    {
        Fixture f("relocation crossing save");
        auto* source = f.zone(f.own, Structure_ZoneResidential, 20, 20);
        auto units = relocationPair(f, source);
        sourceID = source->getObjectID();
        for(const UnitBase* unit : units) ids.push_back(unit->getObjectID());
        source->setHealth(relocationThreshold(source) - 1);
        f.run(1);
        require(source->getGarrisonSize() == 2 && source->hasAttemptedGarrisonRelocation(),
                "relocation crossing save: no-destination attempt did not latch with two inside");
        require(f.game->saveGame(path), "relocation crossing save: could not save the failed attempt");
    }
    auto game = loadRelocationGame(path);
    auto* source = dynamic_cast<ZoneStructure*>(game->getObjectManager().getObject(sourceID));
    require(source && source->hasAttemptedGarrisonRelocation() && source->getGarrison() == ids,
            "relocation crossing save: load lost the attempt latch or occupants");
    auto* owner = game->getHouse(HOUSE_ATREIDES);
    auto* destination = dynamic_cast<ZoneStructure*>(owner->placeStructure(
        NONE_ID, Structure_ZoneCommercial, 32, 20, true, true));
    require(destination != nullptr, "relocation crossing save: cannot add a shelter after load");
    destination->setStartingLevel(2);
    destination->setVisible(VIS_ALL, true);
    for(int i = 0; i < MILLI2CYCLES(1000); ++i) game->updateGameState();
    require(source->getGarrison() == ids && destination->getGarrisonSize() == 0,
            "relocation crossing save: load or a new destination retried the same crossing");
    source->setHealth(relocationThreshold(source));
    game->updateGameState();
    require(!source->hasAttemptedGarrisonRelocation() && source->getGarrisonSize() == 2,
            "relocation crossing save: recovery to exactly30 did not re-arm relocation");
    source->setHealth(relocationThreshold(source) - FixPoint::FromRawValue(1));
    game->updateGameState();
    require(source->hasAttemptedGarrisonRelocation() && source->getGarrisonSize() == 0
                && destination->getGarrisonSize() == 0,
            "relocation crossing save: the recovered/new drop did not start a real walk");
    for(const Uint32 id : ids) {
        const auto* unit = dynamic_cast<const UnitBase*>(game->getObjectManager().getObject(id));
        require(unit && unit->isActive() && unit->getGarrisonRelocationTarget() == destination->getObjectID(),
                "relocation crossing save: a saved occupant did not reserve its new destination");
    }
    require(runRelocationUntil(*game, 45, [&] { return destination->getGarrisonSize() == 2; }),
            "relocation crossing save: both saved occupants never arrived after the new crossing");
    for(const Uint32 id : ids) {
        const auto* unit = dynamic_cast<const UnitBase*>(game->getObjectManager().getObject(id));
        require(unit && hidden(unit) && unit->getGarrisonRelocationTarget() == NONE_ID,
                "relocation crossing save: arrival did not clear a loaded occupant's permission");
    }
    game.reset();
    currentGame = nullptr;
    record("relocation_crossing_save", "failed attempt survives save/load; no retry; exact30 recovery; new drop moves2");
}

void checkGarrisonRelocationTransitSave() {
    const std::string path = gOut + "/zone-garrison-relocation-transit.dls";
    Uint32 destinationID = NONE_ID;
    std::vector<Uint32> ids;
    {
        Fixture f("relocation transit save");
        auto* source = f.zone(f.own, Structure_ZoneIndustrial, 20, 20);
        auto units = relocationPair(f, source);
        // The target is deliberately created after its units, exercising forward IDs on load.
        auto* destination = f.zone(f.ally, Structure_ZoneCommercial, 30, 20);
        destinationID = destination->getObjectID();
        source->setHealth(relocationThreshold(source) - 1);
        f.game->updateGameState();
        for(const UnitBase* unit : units) {
            require(unit->isActive() && unit->getGarrisonRelocationTarget() == destinationID,
                    "relocation transit save: no live transfer intent to save");
            ids.push_back(unit->getObjectID());
        }
        require(destination->getGarrisonSize() == 0 && f.game->saveGame(path),
                "relocation transit save: could not save the in-flight occupants");
    }
    auto game = loadRelocationGame(path);
    auto* destination = dynamic_cast<ZoneStructure*>(game->getObjectManager().getObject(destinationID));
    require(destination && destination->getOwner() == game->getHouse(HOUSE_ORDOS),
            "relocation transit save: ally destination changed owner in the load");
    for(const Uint32 id : ids) {
        const auto* unit = dynamic_cast<const UnitBase*>(game->getObjectManager().getObject(id));
        require(unit && unit->isActive() && unit->getGarrisonRelocationTarget() == destinationID,
                "relocation transit save: load lost the in-flight destination permission");
    }
    require(runRelocationUntil(*game, 45, [&] { return destination->getGarrisonSize() == 2; }),
            "relocation transit save: saved transfers did not finish walking into the ally shelter");
    for(const Uint32 id : ids) {
        const auto* unit = dynamic_cast<const UnitBase*>(game->getObjectManager().getObject(id));
        require(unit && hidden(unit) && unit->getGarrisonRelocationTarget() == NONE_ID,
                "relocation transit save: arrival retained stale saved permission");
    }
    require(destination->getOwner() == game->getHouse(HOUSE_ORDOS),
            "relocation transit save: friendly arrival captured the shelter");
    game.reset();
    currentGame = nullptr;
    record("relocation_transit_save", "2 in-flight friendly reservations survive save/load and finish without capture");
}

void checkGarrisonRelocationCancellation() {
    Fixture f("relocation cancellation");
    auto* source = f.zone(f.own, Structure_ZoneResidential, 20, 10);
    auto* destination = f.zone(f.ally, Structure_ZoneCommercial, 44, 10);
    auto units = relocationPair(f, source);
    source->setHealth(relocationThreshold(source) - 1);
    f.game->updateGameState();
    for(UnitBase* unit : units)
        require(unit->getGarrisonRelocationTarget() == destination->getObjectID(),
                "relocation cancellation: a transfer did not start");
    const auto digestWithIntent = f.game->computeStateDigest().objectHash;
    // Queue revocation changes neither HP nor position: its reserved intent must itself be hashed.
    units[0]->clearOrderQueue();
    require(units[0]->getGarrisonRelocationTarget() == NONE_ID
                && f.game->computeStateDigest().objectHash != digestWithIntent,
            "relocation cancellation: revocation did not clear or hash the transfer intent");
    Command(f.playerOf(f.own), CMD_UNIT_MOVE2POS, units[0]->getObjectID(), 20, 6, true).executeCommand();
    f.capture(units[0], destination);
    require(units[0]->getGarrisonRelocationTarget() == NONE_ID && !destination->mayEnterGarrison(units[0]),
            "relocation cancellation: superseding player orders retained automatic ally permission");
    const FixPoint before = destination->getHealth();
    destination->transferOwnership(f.enemy);
    f.run(1);
    require(units[1]->getGarrisonRelocationTarget() == NONE_ID && units[1]->getTarget() == nullptr
                && units[1]->getAttackMode() == GUARD && destination->getHealth() == before,
            "relocation cancellation: a newly hostile automatic destination became an attack");
    const Uint32 deadID = units[0]->getObjectID();
    units[0]->setHealth(0);
    f.game->updateGameState();
    require(!f.alive(deadID), "relocation cancellation: the dead transfer unit remained in the object manager");
    record("relocation_cancellation", "intent is hashed; new player order revokes ally entry; hostile shelter cancels safely");
}

void checkGarrisonCaptureOwnershipChange() {
    Fixture f("capture ownership change");
    auto* target = f.zone(f.own, Structure_ZoneCommercial, 40, 40);
    std::vector<UnitBase*> attackers = {
        f.unit(f.own, Unit_Trooper, 27, 39), f.unit(f.own, Unit_Trooper, 27, 41)
    };
    for(UnitBase* unit : attackers) {
        f.capture(unit, target);
        require(unit->getAttackMode() == CAPTURE && unit->getTarget() == target,
                "capture ownership change: friendly Capture did not start its Follow-backed walk");
    }
    target->transferOwnership(f.enemy);
    auto* defender = f.unit(f.enemy, Unit_Soldier, 39, 40);
    f.capture(defender, target);
    require(target->getGarrisonSize() == 1, "capture ownership change: no enemy defender entered");
    f.game->updateGameState();
    for(UnitBase* unit : attackers)
        require(unit->getAttackMode() == CAPTURE && unit->getTarget() == target
                    && unit->getGarrisonRelocationTarget() == NONE_ID,
                "capture ownership change: explicit Capture was mistaken for ordinary Follow/automatic transfer");
    const FixPoint before = target->getHealth();
    require(f.runUntil(45, [&] { return target->getHealth() < before; }),
            "capture ownership change: explicit friendly Capture never normalized to enemy assault");
    record("capture_ownership_change", "2 friendly Capture orders persist through takeover and assault the enemy-held target");
}

void checkGarrisonRelocationAIClass(const std::string& aiClass) {
    auto options = gModOptions;
    options.garrisoningAndCapturing = true;
    options.immortalHumanPlayer = false;
    GameInitSettings init("relocation-ai.ini", fixtureMap(), "Relocation AI", true, options);
    init.randomSeed = 17;
    GameInitSettings::HouseInfo human(HOUSE_ATREIDES, 1);
    human.addPlayerInfo({"Garrison tester", "HumanPlayer"});
    init.addHouseInfo(human);
    GameInitSettings::HouseInfo ai(HOUSE_HARKONNEN, 2);
    ai.addPlayerInfo({"Shelter AI", aiClass});
    init.addHouseInfo(ai);
    GameInitSettings::HouseInfo ally(HOUSE_ORDOS, 1);
    ally.addPlayerInfo({"Ally", "HumanPlayer"});
    init.addHouseInfo(ally);
    auto game = std::make_unique<Game>();
    currentGame = game.get();
    game->bReplay = true;
    game->initGame(init);
    game->bReplay = false;
    game->initializeGameLoop();
    House* owner = game->getHouse(HOUSE_HARKONNEN);
    require(owner && owner->isAI(), "relocation AI: the occupant house is not an actual AI player");
    auto* source = dynamic_cast<ZoneStructure*>(owner->placeStructure(
        NONE_ID, Structure_ZoneResidential, 20, 30, true, true));
    auto* destination = dynamic_cast<ZoneStructure*>(owner->placeStructure(
        NONE_ID, Structure_ZoneIndustrial, 32, 30, true, true));
    require(source && destination, "relocation AI: could not place the AI's shelters");
    for(ZoneStructure* zone : {source, destination}) {
        zone->setStartingLevel(2);
        zone->setVisible(VIS_ALL, true);
    }
    // Keep the autonomous AI active, but put these own shelters at a real front. A quiet
    // rear garrison may legitimately be released by tactics even at exactly 30% health.
    auto* enemyWindtrap = game->getHouse(HOUSE_ATREIDES)->placeStructure(
        NONE_ID, Structure_WindTrap, 17, 29, true, true);
    require(enemyWindtrap != nullptr, "relocation AI: could not establish a visible enemy front");
    enemyWindtrap->setVisible(VIS_ALL, true);
    std::vector<UnitBase*> units;
    for(int i = 0; i < 2; ++i) {
        auto* unit = owner->createUnit(i ? Unit_Trooper : Unit_Soldier, true);
        require(unit != nullptr, "relocation AI: could not create an occupant");
        unit->deploy(Coord(19, 30 + i));
        Command(owner->getPlayerList().front()->getPlayerID(), CMD_INFANTRY_CAPTURE,
                unit->getObjectID(), source->getObjectID()).executeCommand();
        units.push_back(unit);
    }
    require(runRelocationUntil(*game, 20, [&] { return source->getGarrisonSize() == 2; }),
            "relocation AI: both AI-owned soldiers did not enter");
    source->setHealth(relocationThreshold(source));
    for(int i = 0; i < MILLI2CYCLES(1000); ++i) game->updateGameState();
    require(source->getGarrisonSize() == 2 && !source->hasAttemptedGarrisonRelocation(),
            "relocation AI: exactly30 evacuated the AI's occupants");
    source->setHealth(relocationThreshold(source) - 1);
    game->updateGameState();
    require(source->getGarrisonSize() == 0 && destination->getGarrisonSize() == 0,
            "relocation AI: the automatic AI path stayed inside or teleported");
    const Coord firstExit = units[0]->getLocation();
    const Coord secondExit = units[1]->getLocation();
    bool firstWalked = false;
    bool secondWalked = false;
    for(const UnitBase* unit : units)
        require(unit->isActive() && unit->getGarrisonRelocationTarget() == destination->getObjectID(),
                "relocation AI: no live reserved walking order");
    require(runRelocationUntil(*game, 45, [&] {
        firstWalked = firstWalked || (units[0]->isActive() && units[0]->getLocation() != firstExit);
        secondWalked = secondWalked || (units[1]->isActive() && units[1]->getLocation() != secondExit);
        return destination->getGarrisonSize() == 2;
    }), "relocation AI: both occupants did not arrive");
    require(firstWalked && secondWalked && destination->getOwner() == owner,
            "relocation AI: no actual walking or destination owner changed");
    for(const UnitBase* unit : units)
        require(hidden(unit) && unit->getGarrisonRelocationTarget() == NONE_ID,
                "relocation AI: arrival did not clear the transfer intent");
    game.reset();
    currentGame = nullptr;
    record("relocation_ai", aiClass + " owns both occupants; exact30 holds; below30 walks2 into its shelter");
}

void checkGarrisonRelocationAI() {
    checkGarrisonRelocationAIClass("AIPlayer:hard");
    checkGarrisonRelocationAIClass("qBotSupportHard");
}

void checkGarrisonRelocationDigest() {
    Fixture f("relocation digest");
    auto* source = f.zone(f.own, Structure_ZoneResidential, 20, 20);
    relocationPair(f, source);
    const FixPoint low = relocationThreshold(source) - 1;
    source->setHealth(low);
    f.game->updateGameState();
    require(source->hasAttemptedGarrisonRelocation() && source->getGarrisonSize() == 2,
            "relocation digest: the failed attempt did not latch");
    const auto attemptedHash = f.game->computeStateDigest().objectHash;
    source->setHealth(relocationThreshold(source));
    f.game->updateGameState();
    require(!source->hasAttemptedGarrisonRelocation(), "relocation digest: recovery did not clear the latch");
    source->setHealth(low);
    require(f.game->computeStateDigest().objectHash != attemptedHash,
            "relocation digest: identical HP/occupants with a different crossing latch hashed identically");
    record("relocation_digest", "saved crossing latch is independently covered by the multiplayer object hash");
}
