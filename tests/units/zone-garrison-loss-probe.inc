// Included inside ZoneGarrisonProbe after the existing real-player helpers.

void checkBloomEjection() {
    Fixture f("bloom_ejection");
    f.game->getCitySimulation()->setCityEffectsEnabled(false);
    auto* zone = f.zone(f.own, Structure_ZoneCommercial, 24, 24, 2);
    std::vector<UnitBase*> units{f.unit(f.own, Unit_Trooper, 22, 24), f.unit(f.own, Unit_Trooper, 22, 25)};
    require(captureByPlayer(f, units, zone) == 2
                && f.runUntil(20, [&] { return zone->getGarrisonSize() == 2; }),
            "bloom_ejection: both real Capture orders did not enter");
    const std::array<Uint32, 2> ids{units[0]->getObjectID(), units[1]->getObjectID()};
    require(units[0]->getHealth() > 0 && units[1]->getHealth() > 0 && hidden(units[0]) && hidden(units[1]),
            "bloom_ejection: the before state was not two living sheltered occupants");
    const Coord exit(23, 24);
    int walls = 0;
    for(int y = 23; y <= 26; ++y) for(int x = 23; x <= 26; ++x) {
        if((x >= 24 && x <= 25 && y >= 24 && y <= 25) || Coord(x, y) == exit) continue;
        require(f.own->placeStructure(NONE_ID, Structure_Wall, x, y, true, true) != nullptr,
                "bloom_ejection: could not enclose the ruin's other exits with real walls");
        ++walls;
    }
    require(walls == 11, "bloom_ejection: the constrained perimeter is incomplete");
    currentGameMap->getTile(exit)->setType(Terrain_SpiceBloom);
    const Uint32 zoneID = zone->getObjectID();
    const size_t explosions = f.game->getExplosionList().size();
    Command(f.playerOf(f.own), CMD_ZONE_DEMOLISH, zoneID).executeCommand();
    require(!f.alive(zoneID) && !currentGameMap->getTile(exit)->isSpiceBloom()
                && f.game->getExplosionList().size() > explosions,
            "bloom_ejection: demolition did not activate the real deployment bloom and its explosion");
    int alive = 0, dead = 0, hiddenAlive = 0, hiddenDead = 0;
    for(const auto id : ids) {
        const auto* unit = dynamic_cast<UnitBase*>(f.game->getObjectManager().getObject(id));
        if(unit && unit->getHealth() > 0) {
            ++alive;
            if(!unit->isVisible() || !unit->isActive() || unit->isPickedUp()) ++hiddenAlive;
        } else {
            ++dead;
            if(unit && (!unit->isVisible() || !unit->isActive() || unit->isPickedUp())) ++hiddenDead;
        }
    }
    record("bloom_ejection", "activation=1 infantry_before=2 living_after=" + std::to_string(alive)
           + " deaths=" + std::to_string(dead) + " hidden_living=" + std::to_string(hiddenAlive)
           + " hidden_dead=" + std::to_string(hiddenDead) + " explosions="
           + std::to_string(f.game->getExplosionList().size() - explosions));
    require(alive == 1 && dead == 1 && hiddenAlive == 0 && hiddenDead == 0,
            "bloom_ejection: ruin injury resurrected or hid a bloom casualty");
    require(!f.game->pInterface->newsticker.messages.empty()
                && f.game->pInterface->newsticker.messages.front().find("Garrison casualties") != std::string::npos,
            "bloom_ejection: the ejection casualty was not visible to its owner");
    f.game->updateGameState();
    int remaining = 0;
    for(const auto id : ids) if(f.alive(id)) ++remaining;
    require(remaining == 1, "bloom_ejection: the hazard casualty bypassed normal unit removal");
    int normalDeaths = 0;
    for(const auto& corpse : currentGameMap->getTile(exit)->deadUnits)
        if(corpse.type == DeadUnit_Infantry) ++normalDeaths;
    require(normalDeaths == 1, "bloom_ejection: the hazard casualty did not create its normal on-map death animation");
    record("bloom_ejection_death", "normal_death=1 visible_animation=1 no_resurrection=1");
}

void checkOwnershipChangingFollow() {
    Fixture f("ownership_changing_follow");
    f.game->getCitySimulation()->setCityEffectsEnabled(false);
    auto* zone = f.zone(f.own, Structure_ZoneCommercial, 24, 24, 2);
    auto* follower = static_cast<InfantryBase*>(f.unit(f.own, Unit_Soldier, 18, 24));
    selectUnits(f, {follower});
    clickMap(f, zone->getLocation(), SDL_BUTTON_RIGHT);
    const Uint32 zoneID = zone->getObjectID();
    require(f.runUntil(2, [&] { return follower->bFollow && follower->target.getObjectID() == zoneID; }),
            "ownership_changing_follow: the player's friendly right-click never created Follow");
    std::vector<UnitBase*> occupants{f.unit(f.enemy, Unit_Trooper, 26, 24), f.unit(f.enemy, Unit_Trooper, 26, 25)};
    f.capture(occupants[0], zone);
    f.capture(occupants[1], zone);
    require(zone->getOwner() == f.enemy && zone->getGarrisonSize() == 2 && follower->bFollow,
            "ownership_changing_follow: the target did not change ownership while Follow was pending");
    // Restore the stale arrival boundary that the legacy branch accepted: a
    // completed step already inside a now-enemy red-health zone, before that
    // zone gets its next low-health evacuation update. This also protects old
    // saves containing such a queued step; no UI completion is fabricated.
    const Coord at = zone->getLocation();
    currentGameMap->removeObjectFromMap(follower->getObjectID());
    follower->ObjectBase::setLocation(at.x, at.y);
    follower->realX += TILESIZE / 2;
    follower->realY += TILESIZE / 2;
    require(follower->bFollow && follower->target.getObjectID() == zoneID,
            "ownership_changing_follow: the restored arrival lost its original Follow intent");
    follower->moving = false;
    follower->justStoppedMoving = true;
    zone->setHealth(1);
    follower->checkPos();
    auto* retained = dynamic_cast<ZoneStructure*>(f.game->getObjectManager().getObject(zoneID));
    int hiddenOccupants = 0;
    for(const auto* unit : occupants) if(unit->getHealth() > 0 && hidden(unit)) ++hiddenOccupants;
    record("ownership_changing_follow", "follow_activation=1 ownership_activation=1 stale_arrival=1 garrison=2->"
           + std::to_string(retained ? retained->getGarrisonSize() : 0)
           + " tracked_hidden=" + std::to_string(hiddenOccupants));
    require(retained && retained->getGarrisonSize() == 2 && hiddenOccupants == 2 && follower->getHealth() > 0,
            "ownership_changing_follow: legacy capture deleted the zone and orphaned its two occupants");
    require(!follower->canPass(at.x, at.y), "ownership_changing_follow: new infantry paths still enter a held zone's footprint");
    follower->targeting();
    require(!follower->bFollow && follower->target.getObjectID() == NONE_ID,
            "ownership_changing_follow: Follow was not cancelled when its building became hostile");
}
