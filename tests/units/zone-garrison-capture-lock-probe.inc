// Included inside ZoneGarrisonProbe after its real player-input helpers.
// Capture is refused while another house has any occupant, at every entry point.
// Explicit Attack, real evacuation/repair, and a fresh player Capture remain distinct.

static_assert(DuneCity::ZoneGarrison::entryFor(HOUSE_ATREIDES, 1, HOUSE_HARKONNEN, 2, 1, HOUSE_HARKONNEN)
              == DuneCity::ZoneGarrison::Entry::Refused, "An occupied enemy zone cannot be captured");
static_assert(DuneCity::ZoneGarrison::entryFor(HOUSE_ATREIDES, 1, HOUSE_ORDOS, 1, 1, HOUSE_ORDOS)
              == DuneCity::ZoneGarrison::Entry::Refused, "An allied garrison is still another house");
static_assert(DuneCity::ZoneGarrison::entryFor(HOUSE_ATREIDES, 1, HOUSE_ATREIDES, 1, 1, HOUSE_ORDOS)
              == DuneCity::ZoneGarrison::Entry::Refused, "A foreign garrison locks even an own zone");
static_assert(DuneCity::ZoneGarrison::entryFor(HOUSE_ATREIDES, 1, HOUSE_ATREIDES, 1, 1, HOUSE_ATREIDES)
              == DuneCity::ZoneGarrison::Entry::Enter, "Same-house reinforcement remains available");
static_assert(DuneCity::ZoneGarrison::entryFor(HOUSE_ATREIDES, 1, HOUSE_ATREIDES, 1,
                  DuneCity::ZoneGarrison::kCapacity, HOUSE_ATREIDES)
              == DuneCity::ZoneGarrison::Entry::Refused, "Reinforcement still obeys capacity");
static_assert(DuneCity::ZoneGarrison::entryFor(HOUSE_ATREIDES, 1, HOUSE_HARKONNEN, 2, 0, HOUSE_HARKONNEN)
              == DuneCity::ZoneGarrison::Entry::EnterAndTake, "An empty enemy zone remains capturable");

void checkOccupiedCaptureHealth(bool red) {
    Fixture f(red ? "occupied red capture" : "occupied healthy capture");
    f.own->producedPower = f.enemy->producedPower = 10000;
    auto* z = f.zone(f.enemy, Structure_ZoneResidential, 40, 40);
    auto* defender = f.unit(f.enemy, Unit_Soldier, 39, 40);
    f.capture(defender, z);
    require(f.runUntil(20, [&] { return z->getGarrisonSize() == 1; }), f.label + ": defender did not enter");
    if(red) z->setHealth(z->getMaxHealth() / 5);
    require(z->isGarrisoned() && hidden(defender) && z->getGarrisonHouse() == f.enemy
                && (!red || z->getHealthColor() == COLOR_RED), f.label + ": occupied health fixture is wrong");
    auto* attacker = f.unit(f.own, Unit_Trooper, 37, 40);
    auto* infantry = dynamic_cast<InfantryBase*>(attacker);
    require(infantry != nullptr, f.label + ": Trooper is not capture-capable infantry");
    const Uint32 zoneID = z->getObjectID();
    const FixPoint health = z->getHealth();
    const FixPoint attackerHealth = attacker->getHealth();
    const int ownZones = f.own->getNumItems(Structure_ZoneResidential);
    const int enemyZones = f.enemy->getNumItems(Structure_ZoneResidential);

    // No game cycle occurs between making the red target and these attempts: its live
    // occupant must block Capture before the ordinary low-health evacuation can run.
    require(captureByPlayer(f, {attacker}, z, red, false) == 0,
            f.label + ": real Capture cursor accepted a foreign garrison");
    f.capture(attacker, z);                                      // actual network/player command
    f.own->getPlayerList().front()->doCaptureStructure(infantry, z); // AI/direct pointer entry
    infantry->doCaptureStructure(zoneID);                       // direct ID execution entry
    require(!z->mayEnterGarrison(attacker) && !z->enterGarrison(attacker),
            f.label + ": direct entry bypassed the live garrison lock");
    z->transferOwnership(f.own);                                // final ownership authority
    Command(f.playerOf(f.own), CMD_UNIT_QUEUEORDER, attacker->getObjectID(),
            static_cast<Uint32>(UnitOrderType::Capture), zoneID, 0).executeCommand();
    attacker->doIssueOrder(UnitOrder::on(UnitOrderType::Capture, zoneID), false);
    require(attacker->getAttackMode() != CAPTURE && attacker->getTarget() != z
                && attacker->getOrderQueue().empty() && attacker->isActive()
                && attacker->getHealth() == attackerHealth,
            f.label + ": refused Capture installed an order, hid or consumed its infantry");
    require(f.game->getObjectManager().getObject(zoneID) == z && z->getOwner() == f.enemy
                && z->getGarrisonSize() == 1 && z->getGarrison()[0] == defender->getObjectID()
                && z->getHealth() == health && f.own->getNumItems(Structure_ZoneResidential) == ownZones
                && f.enemy->getNumItems(Structure_ZoneResidential) == enemyZones,
            f.label + ": a rejected entry point replaced, damaged or transferred the occupied zone");
    for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx)
        require(currentGameMap->getTile(40 + dx, 40 + dy)->getOwner() == HOUSE_HARKONNEN,
                f.label + ": rejected Capture changed a tile owner");

    if(!red) {
        auto* panel = dynamic_cast<UnitInterface*>(selectUnits(f, {attacker}));
        require(panel && panel->attackButton.isVisible() && panel->attackButton.isEnabled(),
                f.label + ": the actual Attack button is unavailable");
        press(panel->attackButton);
        require(f.game->currentCursorMode == Game::CursorMode_Attack, f.label + ": Attack cursor did not open");
        const int before = queued(*f.game, CMD_UNIT_ATTACKOBJECT);
        clickMap(f, z->getLocation());
        require(f.game->currentCursorMode == Game::CursorMode_Normal
                    && queued(*f.game, CMD_UNIT_ATTACKOBJECT) == before + 1,
                f.label + ": Attack on the occupied zone did not queue its real command");
        const auto removedBefore = f.own->getCombatReward(Unit_Trooper).hpRemovedMilli;
        require(f.runUntil(15, [&] {
            require(z->getOwner() == f.enemy && z->isGarrisoned(),
                    f.label + ": Attack transferred ownership or cleared the garrison before its first hit");
            return z->getHealth() < health && f.own->getCombatReward(Unit_Trooper).hpRemovedMilli > removedBefore;
        }), f.label + ": actual Attack did not damage the zone and credit the Trooper");
        require(attacker->getAttackMode() != CAPTURE && attacker->getTarget() == z,
                f.label + ": explicit Attack was mislabeled or redirected as Capture");
    }
    Command(f.playerOf(f.own), CMD_UNIT_SETMODE, attacker->getObjectID(), STOP).executeCommand();
    Command(f.playerOf(f.enemy), CMD_ZONE_EVACUATE, zoneID).executeCommand();
    require(!z->isGarrisoned() && z->getOwner() == f.enemy && onMapBeside(defender, z, 2),
            f.label + ": the owner's real Evacuate command did not empty the zone");
    Command(f.playerOf(f.enemy), CMD_UNIT_SETMODE, defender->getObjectID(), STOP).executeCommand();
    if(red) {
        Command(f.playerOf(f.enemy), CMD_STRUCTURE_REPAIR, zoneID).executeCommand();
        require(z->repairing && f.runUntil(30, [&] { return !z->tooDamagedToHold(); }) && z->getHealth() > health,
                f.label + ": real repair did not make the empty red zone valid for Capture");
    }
    require(captureByPlayer(f, {attacker}, z, !red, true) == 1,
            f.label + ": the real Capture cursor still refused the now-empty valid zone");
    require(f.runUntil(30, [&] { return z->getOwner() == f.own && z->getGarrisonHouse() == f.own; })
                && hidden(attacker) && z->getGarrisonSize() == 1,
            f.label + ": a fresh Capture did not enter and take the cleared zone");
    require(f.own->getNumItems(Structure_ZoneResidential) == ownZones + 1
                && f.enemy->getNumItems(Structure_ZoneResidential) == enemyZones - 1,
            f.label + ": the successful Capture did not transfer structure counts exactly once");
    record(red ? "occupied_capture_red" : "occupied_capture_healthy",
           "UI/direct/queue/ownership capture refused while occupied; real evacuation, repair when needed, fresh player Capture entered and took it");
}

void checkOccupiedCaptureExisting() {
    Fixture f("occupied existing capture");
    f.own->producedPower = f.enemy->producedPower = 10000;
    auto* z = f.zone(f.enemy, Structure_ZoneCommercial, 40, 40);
    auto* active = f.unit(f.own, Unit_Soldier, 30, 40);
    auto* waiting = f.unit(f.own, Unit_Trooper, 30, 44);
    f.capture(active, z);
    Command(f.playerOf(f.own), CMD_UNIT_MOVE2POS, waiting->getObjectID(), 32, 44, 1).executeCommand();
    Command(f.playerOf(f.own), CMD_UNIT_QUEUEORDER, waiting->getObjectID(),
            static_cast<Uint32>(UnitOrderType::Capture), z->getObjectID(), 0).executeCommand();
    require(active->getAttackMode() == CAPTURE && active->getTarget() == z && active->getOrderQueue().size() == 1
                && waiting->getOrderQueue().size() == 2
                && waiting->getOrderQueue()[1].type == UnitOrderType::Capture,
            f.label + ": active and waiting real Capture commands were not installed while empty");
    auto* defender = f.unit(f.enemy, Unit_Soldier, 39, 40);
    f.capture(defender, z);
    require(z->isGarrisoned() && z->getGarrisonHouse() == f.enemy,
            f.label + ": the defender did not enter before the capturers arrived");
    Command(f.playerOf(f.own), CMD_UNIT_QUEUEORDER, waiting->getObjectID(),
            static_cast<Uint32>(UnitOrderType::Capture), z->getObjectID(), 0).executeCommand();
    waiting->doIssueOrder(UnitOrder::on(UnitOrderType::Capture, z->getObjectID()), true);
    require(waiting->getOrderQueue().size() == 2 && waiting->getOrderQueue().front().type == UnitOrderType::Move,
            f.label + ": a new occupied-zone Capture was appended or replaced the existing move");
    f.game->updateGameState();
    require(active->getAttackMode() != CAPTURE && active->getTarget() != z && active->getOrderQueue().empty(),
            f.label + ": dispatched Capture survived another house entering the target");
    require(f.runUntil(20, [&] {
        require(z->getOwner() == f.enemy && z->getGarrisonHouse() == f.enemy,
                f.label + ": a stale order took the live occupied target");
        return waiting->getOrderQueue().empty();
    }), f.label + ": waiting Capture was not discarded when its move finished");
    require(waiting->getAttackMode() != CAPTURE && active->isActive() && waiting->isActive()
                && !hidden(active) && !hidden(waiting),
            f.label + ": a stale Capture hid or entered an attacking soldier");
    record("occupied_capture_existing", "active Capture cancelled, waiting Capture discarded, new queued Capture refused after live occupation");
}

void checkOccupiedCaptureLegacy(bool red) {
    Fixture f(red ? "occupied red legacy arrival" : "occupied healthy legacy arrival");
    f.own->producedPower = f.enemy->producedPower = 10000;
    auto* z = f.zone(f.enemy, Structure_ZoneIndustrial, 40, 20, 1);
    auto* defender = f.unit(f.enemy, Unit_Soldier, 39, 20);
    f.capture(defender, z);
    require(f.runUntil(20, [&] { return z->getGarrisonSize() == 1; }), f.label + ": defender did not enter");
    if(red) z->setHealth(z->getMaxHealth() / 5);
    auto* walker = f.unit(f.own, Unit_Soldier, 39, 20);
    const Uint32 zoneID = z->getObjectID();
    const Uint32 walkerID = walker->getObjectID();
    require(z->isGarrisoned() && (!red || z->getHealthColor() == COLOR_RED),
            f.label + ": move-to-object was not issued against a live occupied target");
    Command(f.playerOf(f.own), CMD_UNIT_MOVE2OBJECT, walkerID, zoneID).executeCommand();
    require(walker->getTarget() == z && walker->getAttackMode() != CAPTURE,
            f.label + ": the real legacy-path move-to-object never started");
    f.game->updateGameState();
    // With the rule ON, hostile Follow is rejected before it can reach classic capture.
    // Waiting for that forbidden interior position would only expose this idle Soldier to fire.
    require(f.alive(walkerID) && walker->isActive() && walker->getTarget() != z
                && !walker->isMoving()
                && blockDistance(walker->getLocation(), z->getClosestPoint(walker->getLocation())) > 0.5_fix,
            f.label + ": the ON-rule hostile Follow was not cancelled outside the lot");
    require(f.game->getObjectManager().getObject(zoneID) == z && z->getOwner() == f.enemy
                && z->getGarrisonHouse() == f.enemy,
            f.label + ": rejected Follow changed the live occupied target");

    // Revoke permission on an existing occupied lot, as the rule-off continuation test does.
    // This enables the real classic walking route, while the city-zone fallback must still refuse it.
    auto& options = const_cast<SettingsClass::GameOptionsClass&>(f.game->getGameInitSettings().getGameOptions());
    options.garrisoningAndCapturing = false;
    require(!ZoneStructure::garrisonsEnabled() && z->isGarrisoned(),
            f.label + ": the OFF-rule legacy treatment lost its existing garrison");
    Command(f.playerOf(f.own), CMD_UNIT_MOVE2OBJECT, walkerID, zoneID).executeCommand();
    require(walker->getTarget() == z && walker->getAttackMode() != CAPTURE,
            f.label + ": the OFF-rule classic movement path never started");
    require(f.runUntil(15, [&] {
        require(f.game->getObjectManager().getObject(zoneID) == z && z->getOwner() == f.enemy,
                f.label + ": legacy arrival replaced or captured the zone");
        require(f.alive(walkerID), f.label + ": legacy arrival consumed its soldier");
        return !walker->isMoving()
            && blockDistance(walker->getLocation(), z->getClosestPoint(walker->getLocation())) <= 0.5_fix;
    }), f.label + ": the soldier never reached the legacy capture position");
    require(walker->isActive() && walker->getHealth() > 0 && z->getOwner() == f.enemy
                && z->getGarrisonHouse() == f.enemy,
            f.label + ": non-Capture arrival bypassed the zone entry path");
    record(red ? "occupied_capture_legacy_red" : "occupied_capture_legacy_healthy",
           "on_rule_follow_refused=1 off_rule_legacy_arrival=1; exact legacy tile reached without capture, "
           "replacement, lost occupation or consumed infantry");
}

void checkOccupiedCaptureHunt() {
    Fixture f("occupied AI Hunt", true, false, true);
    f.own->producedPower = f.enemy->producedPower = 10000;
    auto* z = f.zone(f.enemy, Structure_ZoneResidential, 40, 40);
    auto* defender = f.unit(f.enemy, Unit_Soldier, 39, 40);
    f.capture(defender, z);
    require(f.runUntil(20, [&] { return z->getGarrisonSize() == 1; }), f.label + ": defender did not enter");
    auto* hunter = f.unit(f.own, Unit_Trooper, 37, 40);
    const FixPoint health = z->getHealth();
    const auto removedBefore = f.own->getCombatReward(Unit_Trooper).hpRemovedMilli;
    Command(f.playerOf(f.own), CMD_UNIT_SETMODE, hunter->getObjectID(), HUNT).executeCommand();
    require(f.runUntil(20, [&] {
        require(z->getOwner() == f.enemy && z->getGarrisonHouse() == f.enemy
                    && hunter->getAttackMode() != CAPTURE && !hidden(hunter),
                f.label + ": Hunt/auto-capture bypassed the live foreign garrison");
        return hunter->getTarget() == z && z->getHealth() < health
            && f.own->getCombatReward(Unit_Trooper).hpRemovedMilli > removedBefore;
    }), f.label + ": the real AI Hunt path did not attack and damage the occupied target");
    Command(f.playerOf(f.own), CMD_UNIT_SETMODE, hunter->getObjectID(), STOP).executeCommand();
    record("occupied_capture_hunt", "AI Hunt targeted and damaged the occupied zone without becoming Capture or transferring ownership");
}

void checkOccupiedCaptureAlly() {
    Fixture f("occupied allied house");
    auto* z = f.zone(f.ally, Structure_ZoneResidential, 40, 20);
    auto* defender = f.unit(f.ally, Unit_Soldier, 39, 20);
    f.capture(defender, z);
    require(f.runUntil(20, [&] { return z->getGarrisonSize() == 1; }), f.label + ": allied defender did not enter");
    auto* visitor = f.unit(f.own, Unit_Soldier, 38, 20);
    require(z->heldAgainst(visitor) && !z->mayEnterGarrison(visitor),
            f.label + ": team equality incorrectly made another house's garrison shareable");
    require(captureByPlayer(f, {visitor}, z, false, false) == 0,
            f.label + ": the real cursor accepted another allied house's garrison");
    f.capture(visitor, z);
    z->transferOwnership(f.own);
    require(z->getOwner() == f.ally && z->getGarrisonHouse() == f.ally && hidden(defender)
                && visitor->getAttackMode() != CAPTURE && visitor->getOrderQueue().empty(),
            f.label + ": direct Capture or ownership transfer bypassed house identity");
    record("occupied_capture_ally", "same-team foreign-house occupancy refused by cursor, command, entry and ownership authority");
}

void checkOccupiedCaptureMixedFriendly() {
    Fixture f("occupied mixed friendly crew");
    f.own->producedPower = f.ally->producedPower = 10000;
    auto* target = f.zone(f.own, Structure_ZoneCommercial, 40, 20);
    auto* source = f.zone(f.ally, Structure_ZoneIndustrial, 30, 20);
    auto* alliedCrew = f.unit(f.ally, Unit_Soldier, 29, 20);
    f.capture(alliedCrew, source);
    require(source->getGarrisonSize() == 1, f.label + ": allied source entry did not activate");
    source->setHealth(relocationThreshold(source) - 1);
    f.game->updateGameState();
    require(f.runUntil(30, [&] { return target->getGarrisonSize() == 1 && hidden(alliedCrew); })
                && target->getOwner() == f.own && target->getGarrisonHouse() == f.ally,
            f.label + ": real allied relocation did not leave a friendly crew in the own building");
    auto* ownCrew = f.unit(f.own, Unit_Soldier, 39, 20);
    require(!target->heldAgainst(ownCrew) && captureByPlayer(f, {ownCrew}, target, false, true) == 1,
            f.label + ": own reinforcement was refused on the allied-crewed building");
    require(f.runUntil(20, [&] { return target->getGarrisonSize() == 2 && hidden(ownCrew); })
                && target->getGarrisonHouse() == f.ally && target->heldAgainst(alliedCrew),
            f.label + ": the mixed crew did not retain both houses and reject a foreign takeover");
    const int ownStructures = f.own->getNumStructures();
    const int allyStructures = f.ally->getNumStructures();
    target->transferOwnership(f.ally);
    require(target->getOwner() == f.own && target->getGarrisonSize() == 2
                && f.own->getNumStructures() == ownStructures && f.ally->getNumStructures() == allyStructures,
            f.label + ": ownership Capture ignored a later foreign occupant behind a matching first crew");
    record("occupied_capture_mixed_friendly",
           "ally_relocation=1 own_reinforcement=1 mixed_foreign_takeover_refused=1; both crew houses retained");
}

void checkOccupiedCaptureLock() {
    checkOccupiedCaptureHealth(false);
    checkOccupiedCaptureHealth(true);
    checkOccupiedCaptureExisting();
    checkOccupiedCaptureLegacy(false);
    checkOccupiedCaptureLegacy(true);
    checkOccupiedCaptureHunt();
    checkOccupiedCaptureAlly();
    checkOccupiedCaptureMixedFriendly();
}
