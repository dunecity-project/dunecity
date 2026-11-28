// Included inside ZoneGarrisonProbe after the redevelopment/UI helpers.

void checkOccupantShelter() {
    Fixture f("occupant_shelter");
    f.game->getCitySimulation()->setCityEffectsEnabled(false);
    auto* zone = f.zone(f.own, Structure_ZoneCommercial, 24, 24, 2);
    const auto units = playerGarrison(f, zone, Coord(22, 24));
    for(auto* unit : units) unit->handleDamage(4, NONE_ID, nullptr);
    const FixPoint soldier = units[0]->getHealth(), trooper = units[1]->getHealth();
    const auto remainders = zone->getGarrisonHealthRemainders();
    const FixPoint building = zone->getHealth();
    zone->handleDamage(30, NONE_ID, f.enemy);
    require(zone->getHealth() == building - 30 && zone->getGarrisonSize() == 2
                && units[0]->getHealth() == soldier && units[1]->getHealth() == trooper,
            "occupant_shelter: an actual 30-HP building hit hurt an occupant");
    for(int i = 0; i < 10; ++i) zone->handleDamage(1, NONE_ID, f.enemy);
    require(zone->getHealth() == building - 40 && zone->getHealth() * 100 >= zone->getMaxHealth() * 30
                && zone->getGarrisonSize() == 2 && units[0]->getHealth() == soldier && units[1]->getHealth() == trooper,
            "occupant_shelter: actual chip damage at or above 30% hurt the two occupants");
    for(size_t i = 0; i < units.size(); ++i)
        require(zone->getGarrisonHealthRemainders()[i].healing == remainders[i].healing,
                "occupant_shelter: a building hit changed an occupant's exact healing remainder");
    f.game->gameInitSettings.gameType = GameType::CustomGame;
    f.game->gameType = GameType::CustomGame;
    f.game->gameInitSettings.gameOptions.immortalHumanPlayer = true;
    require(f.game->localHumanImmortalityApplies(),
            "occupant_shelter: the zero-loss arm did not enable single-player immunity");
    zone->handleDamage(100000, NONE_ID, f.enemy);
    require(zone->getHealth() == building - 40 && zone->getGarrisonSize() == 2
                && units[0]->getHealth() == soldier && units[1]->getHealth() == trooper,
            "occupant_shelter: a hit causing no actual building loss hurt the garrison");
    record("occupant_shelter", "actual_building_loss=40 each_occupant_loss=0 one_hp_hits=10 zero_loss_hit=0 infantry=2->2");
}

void checkGarrisonRegeneration() {
    Fixture f("garrison_regeneration");
    f.game->getCitySimulation()->setCityEffectsEnabled(false);
    f.own->producedPower = 10000;
    f.own->setAutoRepairEnabled(false);
    require(f.game->objectData.garrisonHealTime == 60, "garrison_regeneration: default full-heal time is not 60 seconds");
    auto* zone = f.zone(f.own, Structure_ZoneResidential, 24, 24, 2);
    const auto units = playerGarrison(f, zone, Coord(22, 24));
    zone->setHealth(zone->getMaxHealth() * 4 / 5);
    zone->repairing = false;
    zone->foundationDegrades = false;
    for(auto* unit : units) unit->setHealth(1);
    const int halfHealCycles = MILLI2CYCLES(30000);
    const FixPoint building = zone->getHealth();
    for(int i = 0; i < halfHealCycles; ++i) f.game->updateGameState();
    require(zone->getGarrisonSize() == 2 && zone->getHealth() == building,
            "garrison_regeneration: the fixture repaired or lost its occupied building");
    for(const auto* unit : units)
        require(unit->getHealth() == 1_fix + FixPoint(unit->getMaxHealth()) / 2,
                "garrison_regeneration: 30 seconds did not heal exactly half maximum HP");
    record("garrison_regeneration_half", "seconds=30 soldier_hp=" + std::to_string(units[0]->getHealth().getRawValue())
           + " trooper_hp=" + std::to_string(units[1]->getHealth().getRawValue()) + " infantry=2->2 building_repaired=0");
    zone->handleDamage(30, NONE_ID, f.enemy);
    for(const auto* unit : units)
        require(unit->getHealth() == 1_fix + FixPoint(unit->getMaxHealth()) / 2,
                "garrison_regeneration: building damage interrupted continuous healing");
    for(int i = 0; i < halfHealCycles; ++i) f.game->updateGameState();
    for(const auto* unit : units) require(unit->getHealth() == unit->getMaxHealth(), "garrison_regeneration: health did not saturate");
    for(auto* unit : units) unit->setHealth(FixPoint(unit->getMaxHealth()) / 2);
    playerEvacuate(f, zone, units);
    const FixPoint a = units[0]->getHealth(), b = units[1]->getHealth();
    f.run(5);
    require(units[0]->getHealth() == a && units[1]->getHealth() == b && !zone->isGarrisoned(),
            "garrison_regeneration: healing continued after the occupants left");
    const auto hash = f.game->objectData.getEffectiveHash();
    f.game->objectData.garrisonHealTime = 30;
    require(f.game->objectData.getEffectiveHash() != hash, "garrison_regeneration: tuning the rate does not change the multiplayer data hash");
    const std::string config = gOut + "/garrison-heal-rate.ini";
    { std::ofstream file(config); file << "[Map Settings]\nGarrisonHealTime = 30\nGarrisonCollapseDamagePercent = 33\n"; }
    ObjectData tuned;
    tuned.loadFromINIFile(config, false, false);
    require(tuned.garrisonHealTime == 30 && tuned.garrisonCollapseDamagePercent == 33,
            "garrison_regeneration: the named healing/collapse INI settings were not loaded");
    const auto tunedHash = tuned.getEffectiveHash();
    tuned.garrisonCollapseDamagePercent = 34;
    require(tuned.getEffectiveHash() != tunedHash,
            "garrison_regeneration: collapse damage tuning does not change the multiplayer data hash");
    record("garrison_regeneration", "full_heal_seconds=60 saturated=2 continuous_under_fire=1 after_exit_healing=0 tuned_ini_seconds=30 collapse_percent=33 hash_changed=1");
}

void checkChipDamageSurvival() {
    Fixture f("chip_damage_survival");
    f.game->getCitySimulation()->setCityEffectsEnabled(false);
    auto* zone = f.zone(f.own, Structure_ZoneCommercial, 24, 24, 2);
    std::vector<UnitBase*> units{f.unit(f.own, Unit_Trooper, 22, 24), f.unit(f.own, Unit_Trooper, 22, 25)};
    require(captureByPlayer(f, units, zone) == 2, "chip_damage_survival: Capture did not order both Troopers");
    require(f.runUntil(40, [&] { return zone->getGarrisonSize() == 2; }), "chip_damage_survival: Troopers did not enter");
    const int max = units[0]->getMaxHealth();
    const int hits = 10 * max;
    for(int hit = 0; hit < hits; ++hit) {
        // No cycles pass: isolate actual building hits from continuous regeneration.
        zone->setHealth(zone->getMaxHealth());
        zone->handleDamage(1, NONE_ID, f.enemy);
        require(zone->getHealth() == zone->getMaxHealth() - 1 && zone->getGarrisonSize() == 2
                    && units[0]->getHealth() == max && units[1]->getHealth() == max,
                "chip_damage_survival: a repaired building accumulated damage against a sheltered occupant");
    }
    require(hidden(units[0]) && hidden(units[1]), "chip_damage_survival: a surviving occupant left shelter");
    record("chip_damage_survival", "actual_one_hp_hits=" + std::to_string(hits)
           + " occupant_loss=0 infantry=2->2 hidden_living=2");
    auto* nearRuin = f.zone(f.own, Structure_ZoneIndustrial, 36, 24, 2);
    const auto survivors = playerGarrison(f, nearRuin, Coord(34, 24));
    const FixPoint a = survivors[0]->getHealth(), b = survivors[1]->getHealth();
    nearRuin->handleDamage(nearRuin->getHealth().floor() - 1, NONE_ID, f.enemy);
    require(nearRuin->getHealth() == 1 && nearRuin->getGarrisonSize() == 2
                && survivors[0]->getHealth() == a && survivors[1]->getHealth() == b,
            "chip_damage_survival: damage to a nearly destroyed building reached its occupants");
    record("nearly_destroyed", "building_hp=1/200 infantry=2->2 occupant_loss=0");
}

void checkGarrisonPartialEvacuation() {
    Fixture f("partial_evacuation");
    f.game->getCitySimulation()->setCityEffectsEnabled(false);
    auto* zone = f.zone(f.enemy, Structure_ZoneCommercial, 24, 24, 2);
    std::vector<UnitBase*> units;
    for(int i = 0; i < 4; ++i) {
        auto* unit = f.unit(f.own, i == 2 ? Unit_Soldier : Unit_Trooper, 22, 24 + i);
        units.push_back(unit);
        f.capture(unit, zone);
        require(f.runUntil(20, [&] { return zone->getGarrisonSize() == i + 1; }),
                "partial_evacuation: a real capture order did not enter");
    }
    for(auto* unit : units) {
        unit->handleDamage(4, NONE_ID, nullptr);
        f.game->updateGameState();
    }
    for(int i = 0; i < 17; ++i) f.game->updateGameState();
    const auto occupants = zone->getGarrison();
    const auto remainders = zone->getGarrisonHealthRemainders();
    require(occupants.size() == 4 && remainders[2].healing != 0
                && remainders[1].healing != remainders[2].healing,
            "partial_evacuation: distinct nonzero healing remainders were not exercised");
    auto* dead = dynamic_cast<UnitBase*>(f.game->getObjectManager().getObject(occupants[0]));
    dead->handleDamage(dead->getMaxHealth(), NONE_ID, nullptr);
    const int deployed = zone->evacuateGarrison(false, 1);
    require(deployed == 2 && zone->getGarrison() == std::vector<Uint32>{occupants[2]}
                && zone->getGarrisonHealthRemainders()[0].healing == remainders[2].healing
                && zone->getOwner() == f.own && zone->getGarrisonOriginalOwner() == f.enemy->getHouseID(),
            "partial_evacuation: KEEP did not prefer the later Soldier over the first live Trooper");
    require(dead->getHealth() <= 0 && dead->isActive() && dead->isVisible() && !dead->isPickedUp()
                && currentGameMap->tileExists(dead->getLocation()),
            "partial_evacuation: a dead first occupant consumed KEEP or remained a hidden casualty");
    for(size_t i = 1; i < zone->getGarrisonHealthRemainders().size(); ++i)
        require(zone->getGarrisonHealthRemainders()[i].healing == 0,
                "partial_evacuation: discarded occupants retained healing state");
    require(zone->evacuateGarrison() == 1 && !zone->isGarrisoned() && zone->getOwner() == f.enemy,
            "partial_evacuation: default KEEP=0 did not empty and return the captured zone");
    f.game->updateGameState();
    require(!f.alive(occupants[0]), "partial_evacuation: the dead occupant skipped ordinary unit removal");
    auto* ruin = f.zone(f.own, Structure_ZoneIndustrial, 36, 24, 2);
    const auto ejected = playerGarrison(f, ruin, Coord(34, 24));
    const std::array<FixPoint, 2> health{ejected[0]->getHealth(), ejected[1]->getHealth()};
    require(ruin->evacuateGarrison(true, DuneCity::ZoneGarrison::kCapacity) == 2 && !ruin->isGarrisoned(),
            "partial_evacuation: destruction obeyed KEEP instead of ejecting everyone");
    for(size_t i = 0; i < ejected.size(); ++i) {
        const int injury = static_cast<int>((int64_t{ejected[i]->getMaxHealth()}
            * f.game->objectData.garrisonCollapseDamagePercent + 99) / 100);
        require(ejected[i]->getHealth() == health[i] - injury && ejected[i]->isVisible()
                    && ejected[i]->isActive() && !ejected[i]->isPickedUp(),
                "partial_evacuation: destruction did not use ordinary damage after deployment");
    }
    ruin->destroy();
    record("partial_evacuation", "keep=1 retained_later_soldier=1 dead_consumed_keep=0 healing_aligned=1 default_keep=0 returned=1 destruction_ignores_keep=1");
}

void checkGarrisonCollapse() {
    Fixture f("garrison_collapse");
    f.game->getCitySimulation()->setCityEffectsEnabled(false);
    require(f.game->objectData.garrisonCollapseDamagePercent == 50,
            "garrison_collapse: default collapse damage is not 50 percent");
    f.game->objectData.data[Unit_Trooper][f.own->getHouseID()].hitpoints = 91;
    auto* zone = f.zone(f.own, Structure_ZoneCommercial, 24, 24, 2);
    std::vector<UnitBase*> units{f.unit(f.own, Unit_Trooper, 22, 24), f.unit(f.own, Unit_Trooper, 22, 25)};
    require(captureByPlayer(f, units, zone) == 2
                && f.runUntil(20, [&] { return zone->getGarrisonSize() == 2; }),
            "garrison_collapse: both real capture orders did not enter");
    const int injury = (units[0]->getMaxHealth() + 1) / 2;
    require(units[0]->getMaxHealth() == 91 && injury == 46,
            "garrison_collapse: odd maximum HP did not exercise upward rounding");
    units[0]->handleDamage(units[0]->getMaxHealth() - injury, NONE_ID, nullptr);
    auto* standingDead = f.unit(f.own, Unit_Trooper, 48, 48);
    auto* standingLive = f.unit(f.own, Unit_Trooper, 49, 48);
    standingDead->handleDamage(standingDead->getMaxHealth() - injury, NONE_ID, nullptr);
    const std::array<Uint32, 2> ids{units[0]->getObjectID(), units[1]->getObjectID()};
    const Uint32 standingDeadID = standingDead->getObjectID(), zoneID = zone->getObjectID();
    const Coord standingDeath = standingDead->getLocation();
    // Friendly automatic relocation can put our soldiers inside an ally's building.
    // Exercise the same ownership boundary without changing the casualty's owner.
    zone->transferOwnership(f.ally, false);
    const size_t explosions = f.game->getExplosionList().size();
    zone->handleDamage(100000, NONE_ID, f.enemy);
    require(zone->getHealth() == 0 && zone->getGarrisonSize() == 2
                && units[0]->getHealth() == injury && units[1]->getHealth() == 91,
            "garrison_collapse: a lethal building hit hurt soldiers before they were ejected");
    standingDead->handleDamage(injury, NONE_ID, nullptr);
    standingLive->handleDamage(injury, NONE_ID, nullptr);
    require(!zone->update() && !f.alive(zoneID) && f.game->getExplosionList().size() > explosions,
            "garrison_collapse: the actual destruction path produced no building explosions");
    const size_t spawnedExplosions = f.game->getExplosionList().size() - explosions;
    const Coord occupantDeath = units[0]->getLocation();
    require(units[0]->getHealth() == standingDead->getHealth() && units[0]->getHealth() == 0
                && units[0]->isVisible() && units[0]->isActive() && !units[0]->isPickedUp()
                && currentGameMap->tileExists(occupantDeath),
            "garrison_collapse: the wounded occupant was resurrected or its normal death stayed hidden");
    require(units[1]->getHealth() == standingLive->getHealth() && units[1]->getHealth() == 45
                && units[1]->isVisible() && units[1]->isActive() && !units[1]->isPickedUp(),
            "garrison_collapse: collapse damage differs from the identical ordinary standing-unit hit");
    const auto& messages = f.game->pInterface->newsticker.messages;
    require(messages.size() == 1 && messages.front().find("Garrison casualties") != std::string::npos,
            "garrison_collapse: our soldier dying in an ally's building produced no owner casualty message");
    f.game->updateGameState();
    require(!f.alive(ids[0]) && !f.alive(standingDeadID) && f.alive(ids[1]),
            "garrison_collapse: ordinary unit removal did not match the standing-unit control");
    auto normalDeathsAt = [](const Coord& at) {
        int count = 0;
        for(const auto& dead : currentGameMap->getTile(at)->deadUnits)
            if(dead.type == DeadUnit_Infantry) ++count;
        return count;
    };
    require(normalDeathsAt(occupantDeath) == 1 && normalDeathsAt(standingDeath) == 1,
            "garrison_collapse: occupant/control did not create the same normal on-map infantry death animation");
    record("garrison_collapse", "interior_loss=0 collapse_percent=50 max_hp=91 injury=46 explosions="
           + std::to_string(spawnedExplosions)
           + " occupant_normal_death=1 standing_normal_death=1 survivor_hp=45 friendly_building_ticker=1");
}

void checkGarrisonLegacyHealth() {
    Fixture f("garrison_legacy_health");
    f.game->getCitySimulation()->setCityEffectsEnabled(false);
    auto* zone = f.zone(f.own, Structure_ZoneCommercial, 24, 24, 2);
    const auto units = playerGarrison(f, zone, Coord(22, 24));
    for(auto* unit : units) unit->handleDamage(4, NONE_ID, nullptr);
    for(int i = 0; i < 17; ++i) f.game->updateGameState();
    const auto healing = zone->getGarrisonHealthRemainders();
    require(healing[0].healing != 0 && healing[1].healing != 0,
            "garrison_legacy_health: no fractional healing was exercised");
    const Uint32 version = f.game->loadedSavegameVersion;
    constexpr Uint32 sentinel = 0x47534c54;
    for(const Uint32 legacy : {Uint32{9905}, Uint32{9906}}) {
        OMemoryStream old;
        old.open();
        zone->StructureBase::save(old);
        old.writeUint8(static_cast<uint8_t>(zone->getZoneType()));
        DuneCity::ResidentialPopulation::write(old, zone->getResidentialPopulation());
        old.writeUint32Vector(zone->getGarrison());
        old.writeUint32(NONE_ID);
        old.writeSint32(7);
        old.writeSint32(-1);
        for(size_t i = 0; i < units.size(); ++i) {
            old.writeSint64(static_cast<int64_t>(i + 1) << 32);
            old.writeUint64(healing[i].healing);
        }
        old.writeUint32(sentinel);
        f.game->loadedSavegameVersion = legacy;
        IMemoryStream input(old.getData(), old.getDataLength());
        {
            CommercialZone restored(input);
            require(restored.getGarrison() == zone->getGarrison() && !restored.hasAttemptedGarrisonRelocation()
                        && input.readUint32() == sentinel,
                    "garrison_legacy_health: an old damage debt was not consumed or invented a relocation attempt");
            for(size_t i = 0; i < units.size(); ++i)
                require(restored.getGarrisonHealthRemainders()[i].healing == healing[i].healing,
                        "garrison_legacy_health: loading a discarded damage debt changed exact healing");
            OMemoryStream modern;
            modern.open();
            restored.save(modern);
            modern.writeUint32(sentinel);
            require(modern.getDataLength() + 8 * units.size() == old.getDataLength() + 1,
                    "garrison_legacy_health: the new layout still writes obsolete damage state or omits its saved bool");
            f.game->loadedSavegameVersion = SAVEGAMEVERSION_ZONE_GARRISON_SHELTER;
            IMemoryStream modernInput(modern.getData(), modern.getDataLength());
            CommercialZone reloaded(modernInput);
            require(reloaded.getGarrison() == restored.getGarrison() && !reloaded.hasAttemptedGarrisonRelocation()
                        && modernInput.readUint32() == sentinel,
                    "garrison_legacy_health: the new healing-only layout lost stream alignment");
        }
    }
    f.game->objectData.garrisonCollapseDamagePercent = 37;
    OMemoryStream data;
    data.open();
    f.game->objectData.save(data);
    data.writeUint32(sentinel);
    for(const Uint32 loadedVersion : {Uint32{9905}, Uint32{9906}, Uint32{SAVEGAMEVERSION_ZONE_GARRISON_SHELTER}}) {
        f.game->loadedSavegameVersion = loadedVersion;
        IMemoryStream input(data.getData(), data.getDataLength());
        ObjectData restored;
        restored.load(input);
        require(restored.garrisonHealTime == f.game->objectData.garrisonHealTime,
                "garrison_legacy_health: the healing rate did not retain its existing save layout");
        if(loadedVersion < SAVEGAMEVERSION_ZONE_GARRISON_SHELTER)
            require(restored.garrisonCollapseDamagePercent == 50 && input.readSint32() == 37
                        && input.readUint32() == sentinel,
                    "garrison_legacy_health: an old ObjectData layout consumed new collapse tuning");
        else
            require(restored.garrisonCollapseDamagePercent == 37 && input.readUint32() == sentinel,
                    "garrison_legacy_health: the new ObjectData layout did not preserve collapse tuning");
    }
    f.game->loadedSavegameVersion = version;
    record("garrison_legacy_health", "old_versions=9905,9906 damage_debt=consumed_not_saved healing_exact=1 new_bool_bytes=1 alignment=preserved collapse_tuning_saved=1");
}


std::string saveThroughPlayerDialog(Fixture& f, const std::string& basename) {
    SDL_KeyboardEvent key{};
    key.type = SDL_KEYDOWN;
    key.keysym.sym = SDLK_ESCAPE;
    f.game->handleKeyInput(key);
    auto* menu = dynamic_cast<InGameMenu*>(f.game->pInGameMenu.get());
    require(menu != nullptr, "civic_save: Escape did not open the game menu");
    press(menu->saveGameButton);
    menu->processChildWindowOpenCloses();
    auto* dialog = dynamic_cast<LoadSaveWindow*>(menu->pChildWindow);
    require(dialog != nullptr, "civic_save: the Save Game button did not open its dialog");
    dialog->saveName.setText(basename);
    const std::string path = dialog->getDirectory() + basename + ".dls";
    press(dialog->okButton);
    menu->processChildWindowOpenCloses();
    require(existsFile(path), "civic_save: the Save dialog callback did not write a save");
    return path;
}

void checkGarrisonCivicSave() {
    for(const auto overlay : {ZoneStructure::CivicOverlay::Hospital, ZoneStructure::CivicOverlay::Church}) {
        Fixture f("civic_save");
        auto* sim = f.game->getCitySimulation();
        sim->setCityEffectsEnabled(false);
        f.game->objectData.garrisonHealTime = 30;
        f.game->objectData.garrisonCollapseDamagePercent = 33;
        auto* zone = f.zone(f.own, Structure_ZoneResidential, 24, 24, 2);
        auto* civic = f.zone(f.own, Structure_ZoneResidential, 36, 24, 2);
        civic->setCivicOverlay(overlay);
        require(!civic->hasCityPower(), "civic_save: the round-trip did not exercise an unpowered civic lot");
        const auto units = playerGarrison(f, zone, Coord(22, 24));
        for(auto* unit : units) unit->setHealth(unit->getMaxHealth() - 4);
        for(int i = 0; i < 17; ++i) f.game->updateGameState();
        const bool attempted = overlay == ZoneStructure::CivicOverlay::Church;
        if(attempted) {
            // The separate civic lot must not become a refuge in the failed-attempt save case.
            civic->foundationDegrades = false;
            civic->repairing = false;
            civic->handleDamage(civic->getHealth().floor() - civic->getMaxHealth() / 4, NONE_ID, f.enemy);
            require(civic->getHealth() * 100 < civic->getMaxHealth() * DuneCity::ZoneGarrison::kEvacuateHealthPercent,
                    "civic_save: the separate civic lot remained an eligible relocation destination");
            zone->foundationDegrades = false;
            zone->repairing = false;
            zone->handleDamage(zone->getHealth().floor() - zone->getMaxHealth() * 29 / 100, NONE_ID, f.enemy);
            f.game->updateGameState();
        }
        require(zone->hasAttemptedGarrisonRelocation() == attempted && zone->getGarrisonSize() == 2,
                "civic_save: the real no-destination crossing did not preserve the garrison and its attempt state");
        zone->handleDamage(9, NONE_ID, f.enemy);
        const auto occupants = zone->getGarrison();
        const auto remainders = zone->getGarrisonHealthRemainders();
        std::array<FixPoint, DuneCity::ZoneGarrison::kCapacity> health{};
        for(size_t i = 0; i < occupants.size(); ++i)
            health[i] = f.game->getObjectManager().getObject(occupants[i])->getHealth();
        const Uint32 id = zone->getObjectID();
        const Uint32 civicID = civic->getObjectID();
        const int zonePopulation = zone->getResidentialPopulation();
        const int civicPopulation = civic->getResidentialPopulation();
        const int taxBase = DuneCity::getStructureTaxBaseEighths(zone, lotDensity(zone))
            + DuneCity::getStructureTaxBaseEighths(civic, lotDensity(civic));
        const auto& beforeCity = sim->getHouseState(f.own->getHouseID());
        const std::array<int, 6> demandState{beforeCity.resValve, beforeCity.comValve, beforeCity.indValve,
            beforeCity.prevResPop, beforeCity.prevComPop, beforeCity.prevIndPop};
        sim->hospitalCare().setOverlays({{civicID, static_cast<uint8_t>(overlay)}});
        sim->hospitalCare().setOverlayDays({{civicID, 1}});
        sim->hospitalCare().houseChanges(f.own->getHouseID()) = {1, 1};
        sim->setCityEffectsEnabled(true);
        const std::string path = saveThroughPlayerDialog(f, "garrison-civic-" + std::to_string(static_cast<int>(overlay)));
        f.game.reset();
        currentGame = nullptr;
        f.game = std::make_unique<Game>();
        currentGame = f.game.get();
        f.game->bReplay = true;
        f.game->initGame(GameInitSettings(path));
        f.game->bReplay = false;
        f.game->initializeGameLoop();
        f.own = f.game->getHouse(HOUSE_ATREIDES);
        f.enemy = f.game->getHouse(HOUSE_HARKONNEN);
        f.ally = f.game->getHouse(HOUSE_ORDOS);
        zone = dynamic_cast<ZoneStructure*>(f.game->getObjectManager().getObject(id));
        civic = dynamic_cast<ZoneStructure*>(f.game->getObjectManager().getObject(civicID));
        require(zone && zone->getGarrison() == occupants && zone->getCivicOverlay() == ZoneStructure::CivicOverlay::None
                    && lotDensity(zone) == 2 && civic && civic->getCivicOverlay() == overlay && lotDensity(civic) == 2,
                "civic_save: reload changed the occupied eligible lot, its occupants or the separate civic lot");
        require(zone->getResidentialPopulation() == zonePopulation && civic->getResidentialPopulation() == civicPopulation,
                "civic_save: loading advanced population growth/decline on a saved lot");
        const auto& loadedCity = f.game->getCitySimulation()->getHouseState(f.own->getHouseID());
        require(loadedCity.resPop == zonePopulation + civicPopulation && loadedCity.taxBaseEighths == taxBase,
                "civic_save: load reconciliation did not rebuild the unchanged lots' population and tax census");
        require((std::array<int, 6>{loadedCity.resValve, loadedCity.comValve, loadedCity.indValve,
                    loadedCity.prevResPop, loadedCity.prevComPop, loadedCity.prevIndPop}) == demandState,
                "civic_save: loading advanced demand valves or previous-population history");
        require(zone->hasAttemptedGarrisonRelocation() == attempted,
                "civic_save: reload changed the saved relocation crossing state");
        require(f.game->objectData.garrisonHealTime == 30 && f.game->objectData.garrisonCollapseDamagePercent == 33,
                "civic_save: configured healing/collapse tuning did not survive save/load");
        std::vector<UnitBase*> loaded;
        for(size_t i = 0; i < occupants.size(); ++i) {
            auto* unit = dynamic_cast<UnitBase*>(f.game->getObjectManager().getObject(occupants[i]));
            require(unit && hidden(unit), "civic_save: a saved occupant lost its hiding state, id="
                    + std::to_string(occupants[i]));
            require(unit->getHealth() == health[i],
                    "civic_save: occupant HP changed, id=" + std::to_string(occupants[i])
                    + " expected_raw=" + std::to_string(health[i].getRawValue())
                    + " actual_raw=" + std::to_string(unit->getHealth().getRawValue()));
            require(zone->getGarrisonHealthRemainders()[i].healing == remainders[i].healing,
                    "civic_save: a fractional healing remainder changed on load");
            loaded.push_back(unit);
        }
        auto* panel = selectZone(f, zone);
        require(panel->garrisonLabel.isVisible() && panel->garrisonLabel.getText().find("2/5") != std::string::npos,
                "civic_save: loaded occupants exist but the player's garrison indicator disappeared");
        zone->handleDamage(1, NONE_ID, f.enemy);
        require(loaded[0]->getHealth() == health[0] && loaded[1]->getHealth() == health[1],
                "civic_save: building damage after reload reached sheltered occupants");
        checkFootprint(zone, 2);
        checkFootprint(civic, 2);
        playerEvacuate(f, zone, loaded);
        record("civic_save", "overlay=" + std::to_string(static_cast<int>(overlay))
               + " infantry=2->2 interior_damage=0 fractional_healing=preserved relocation_attempted="
               + std::to_string(attempted) + " collapse_percent=33 ui_count=2/5 evacuated=2 load_redevelopment=0 census_preserved=1");
    }
}
