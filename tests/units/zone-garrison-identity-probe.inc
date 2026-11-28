// Included inside ZoneGarrisonProbe. Identity is checked by object ID, item ID,
// production house and exact HP, not by the portraits of nearby generic survivors.
struct GarrisonIdentity {
    Uint32 id;
    int item;
    int technology;
    FixPoint health;
};

std::vector<GarrisonIdentity> identitySnapshot(const std::vector<UnitBase*>& units) {
    std::vector<GarrisonIdentity> result;
    for(const auto* unit : units)
        result.push_back({unit->getObjectID(), unit->getItemID(), unit->getProductionHouseID(), unit->getHealth()});
    return result;
}

std::vector<Uint32> identityWorldIDs() {
    std::vector<Uint32> result;
    for(const auto* unit : unitList) result.push_back(unit->getObjectID());
    return result;
}

void assertIdentity(const std::string& path, const std::vector<GarrisonIdentity>& before,
                    const std::vector<Uint32>& worldBefore, bool inside = false, bool collapse = false,
                    int expectedExtraSoldiers = 0) {
    int soldiers = 0, troopers = 0, extraSoldiers = 0, extraTroopers = 0, extraOther = 0;
    for(const auto& original : before) {
        const auto* unit = dynamic_cast<const UnitBase*>(currentGame->getObjectManager().getObject(original.id));
        require(unit && unit->getItemID() == original.item
                    && unit->getProductionHouseID() == original.technology,
                path + ": occupant identity/type/technology changed for " + std::to_string(original.id));
        const int injury = collapse ? static_cast<int>((int64_t{unit->getMaxHealth()}
            * currentGame->objectData.garrisonCollapseDamagePercent + 99) / 100) : 0;
        require(unit->getHealth() == original.health - injury,
                path + ": occupant HP was replaced/reset for " + std::to_string(original.id));
        require(inside ? hidden(unit) : (unit->isActive() && !unit->isPickedUp()
                    && unit->isVisible() && unit->isRespondable() && currentGameMap->tileExists(unit->getLocation())),
                path + ": original occupant is not in its expected visible/contained state");
        soldiers += unit->getItemID() == Unit_Soldier;
        troopers += unit->getItemID() == Unit_Trooper;
    }
    for(const auto* unit : unitList) {
        if(std::find(worldBefore.begin(), worldBefore.end(), unit->getObjectID()) != worldBefore.end()) continue;
        extraSoldiers += unit->getItemID() == Unit_Soldier;
        extraTroopers += unit->getItemID() == Unit_Trooper;
        extraOther += unit->getItemID() != Unit_Soldier && unit->getItemID() != Unit_Trooper;
    }
    record("identity_" + path, "occupants_in=" + std::to_string(before.size())
           + " original_soldiers_out=" + std::to_string(soldiers)
           + " original_troopers_out=" + std::to_string(troopers)
           + " retained_inside=" + std::to_string(inside ? before.size() : 0)
           + " extra_soldiers=" + std::to_string(extraSoldiers)
           + " extra_troopers=" + std::to_string(extraTroopers)
           + " extra_other=" + std::to_string(extraOther) + " exact_HP=1 original_IDs=1");
    require(extraSoldiers == expectedExtraSoldiers && extraTroopers == 0 && extraOther == 0,
            path + ": generic destruction survivors were added on top of the real occupants");
}

std::vector<UnitBase*> identityEnter(Fixture& f, ZoneStructure* zone, bool mixed) {
    const Coord at = zone->getLocation();
    std::vector<UnitBase*> units;
    for(int i = 0; i < 3; ++i)
        units.push_back(f.unit(f.own, mixed && i == 0 ? Unit_Soldier : Unit_Trooper, at.x - 3, at.y + i));
    f.run(1);
    require(captureByPlayer(f, units, zone) == 3, f.label + ": real Capture widget/map click missed an occupant");
    require(f.runUntil(30, [&] { return zone->getGarrisonSize() == 3; }), f.label + ": occupants never entered");
    for(auto* unit : units) require(hidden(unit), f.label + ": entry did not hide an original occupant");
    return units;
}

void identityEvacuate(Fixture& f, ZoneStructure* zone) {
    auto* panel = selectZone(f, zone);
    require(panel->evacuateButton.isVisible() && panel->evacuateButton.isEnabled(),
            f.label + ": real Evacuate widget unavailable");
    const int commands = queued(*f.game, CMD_ZONE_EVACUATE);
    press(panel->evacuateButton);
    require(queued(*f.game, CMD_ZONE_EVACUATE) == commands + 1, f.label + ": Evacuate queued no command");
    require(f.runUntil(5, [&] { return !zone->isGarrisoned(); }), f.label + ": Evacuate did not complete");
}

void checkIdentityExit(const std::string& path, bool mixed) {
    Fixture f("identity_" + path + (mixed ? "_mixed" : "_troopers"));
    f.game->getCitySimulation()->setCityEffectsEnabled(false);
    const bool captured = path == "capture_return" || path == "save_load";
    auto* zone = f.zone(captured ? f.enemy : f.own, Structure_ZoneCommercial, 24, 24);
    auto units = identityEnter(f, zone, mixed);
    // Force the independent generic-survivor mechanism to activate if it is not
    // explicitly suppressed; four building cells would otherwise add four Soldiers.
    f.game->objectData.data[zone->getItemID()][zone->getProductionHouseID()].infspawnprop = 100;
    if(path == "relocation") {
        auto before = identitySnapshot(units);
        const auto world = identityWorldIDs();
        zone->setHealth(relocationThreshold(zone) - 1);
        f.game->updateGameState();
        require(zone->getGarrisonSize() == 3 && zone->hasAttemptedGarrisonRelocation(),
                f.label + ": low-health no-destination path did not retain its real occupants");
        assertIdentity(f.label + "_no_destination", before, world, true);
        auto* destination = f.zone(f.own, Structure_ZoneResidential, 29, 24);
        zone->setHealth(relocationThreshold(zone));
        f.game->updateGameState();
        zone->setHealth(relocationThreshold(zone) - FixPoint::FromRawValue(1));
        before = identitySnapshot(units);
        f.game->updateGameState();
        require(!zone->isGarrisoned() && destination->getGarrisonSize() == 0,
                f.label + ": below-30% departure was not observed or occupants teleported");
        assertIdentity(f.label + "_departure", before, world);
        requireRelocationWalk(f, units, destination);
        assertIdentity(f.label + "_arrival", before, world, true);
        identityEvacuate(f, destination);
        assertIdentity(f.label + "_exit", before, world);
        return;
    }
    if(path == "save_load") {
        // Fractions catch a replacement which copies rounded health only.
        for(auto* unit : units) unit->setHealth(unit->getMaxHealth() - 3 + 0.25_fix);
        const auto before = identitySnapshot(units);
        const auto world = identityWorldIDs();
        const Uint32 zoneID = zone->getObjectID();
        const std::string file = gOut + "/identity-" + (mixed ? "mixed" : "troopers") + ".dls";
        require(f.game->saveGame(file), f.label + ": save failed");
        f.game.reset();
        f.game = loadRelocationGame(file);
        f.own = f.game->getHouse(HOUSE_ATREIDES);
        f.enemy = f.game->getHouse(HOUSE_HARKONNEN);
        f.ally = f.game->getHouse(HOUSE_ORDOS);
        f.game->getCitySimulation()->setCityEffectsEnabled(false);
        zone = dynamic_cast<ZoneStructure*>(f.game->getObjectManager().getObject(zoneID));
        require(zone && zone->getGarrisonSize() == 3 && zone->getOwner() == f.own,
                f.label + ": loaded zone/owner/occupant count changed");
        assertIdentity(f.label + "_loaded", before, world, true);
        identityEvacuate(f, zone);
        assertIdentity(f.label, before, world);
        require(zone->getOwner() == f.enemy, f.label + ": loaded captured zone did not return");
        std::remove(file.c_str());
        return;
    }
    if(path == "legacy_capture") {
        // Restore an old completed Follow step on an occupied now-enemy zone.
        // The stale path must not destroy/rebuild it or orphan/replace its units.
        auto* follower = static_cast<InfantryBase*>(f.unit(f.enemy, Unit_Trooper, 22, 24));
        follower->setTarget(zone);
        follower->bFollow = true;
        currentGameMap->removeObjectFromMap(follower->getObjectID());
        follower->ObjectBase::setLocation(24, 24);
        follower->moving = false;
        follower->justStoppedMoving = true;
        zone->setHealth(1);
        const auto before = identitySnapshot(units);
        const auto world = identityWorldIDs();
        const Uint32 zoneID = zone->getObjectID();
        follower->checkPos();
        require(f.alive(zoneID) && zone->getGarrisonSize() == 3 && zone->getOwner() == f.own,
                f.label + ": stale legacy capture deleted/replaced the held zone");
        assertIdentity(f.label + "_retained", before, world, true);
        // Do not let the synthetic intruder attack the units on their real exit.
        follower->setTarget(nullptr);
        follower->setHealth(0);
        identityEvacuate(f, zone);
        assertIdentity(f.label + "_exit", before, world);
        return;
    }
    if(path == "destruction" || path == "demolition") {
        const auto before = identitySnapshot(units);
        const auto world = identityWorldIDs();
        const Uint32 zoneID = zone->getObjectID();
        if(path == "destruction") zone->handleDamage(100000, NONE_ID, f.enemy);
        else Command(f.playerOf(f.own), CMD_ZONE_DEMOLISH, zoneID).executeCommand();
        require(f.runUntil(5, [&] { return !f.alive(zoneID); }), f.label + ": building did not die");
        assertIdentity(f.label, before, world, false, true);
        return;
    }
    for(auto* unit : units) unit->setHealth(unit->getMaxHealth() - 2 + 0.25_fix);
    auto before = identitySnapshot(units);
    const auto world = identityWorldIDs();
    identityEvacuate(f, zone);
    assertIdentity(f.label, before, world);
    if(path == "capture_return") {
        require(zone->getOwner() == f.enemy, f.label + ": zone did not return to original owner");
        require(captureByPlayer(f, units, zone) == 3, f.label + ": recapture did not queue three real orders");
        require(f.runUntil(30, [&] { return zone->getOwner() == f.own && zone->getGarrisonSize() == 3; }),
                f.label + ": same occupants failed to recapture");
        before = identitySnapshot(units);
        identityEvacuate(f, zone);
        assertIdentity(f.label + "_recapture", before, world);
        require(zone->getOwner() == f.enemy, f.label + ": recaptured zone did not return again");
    }
}

void checkIdentityEmptySurvivors() {
    Fixture f("identity_empty_survivors");
    f.game->getCitySimulation()->setCityEffectsEnabled(false);
    auto* zone = f.zone(f.own, Structure_ZoneCommercial, 24, 24);
    f.game->objectData.data[zone->getItemID()][zone->getProductionHouseID()].infspawnprop = 100;
    const auto world = identityWorldIDs();
    const Uint32 zoneID = zone->getObjectID();
    zone->handleDamage(100000, NONE_ID, f.enemy);
    require(f.runUntil(5, [&] { return !f.alive(zoneID); }), "empty survivor control: building did not die");
    assertIdentity("empty_survivors", {}, world, false, false, 4);
}

void checkGarrisonIdentity() {
    int failed = 0;
    for(bool mixed : {false, true}) {
        for(const std::string path : {"evacuate", "relocation", "destruction", "capture_return",
                                      "save_load", "demolition", "legacy_capture"}) {
            try { checkIdentityExit(path, mixed); }
            catch(const std::exception&) { ++failed; currentGame = nullptr; }
        }
    }
    try { checkIdentityEmptySurvivors(); }
    catch(const std::exception&) { ++failed; currentGame = nullptr; }
    require(failed == 0, "identity: " + std::to_string(failed) + " of 15 exit/control scenarios failed");
}
