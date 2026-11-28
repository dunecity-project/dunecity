// Included inside ZoneGarrisonProbe after the redevelopment/UI helpers.

void checkOccupantDamageShare() {
    Fixture f("occupant_damage_share");
    f.game->getCitySimulation()->setCityEffectsEnabled(false);
    auto* zone = f.zone(f.own, Structure_ZoneCommercial, 24, 24, 2);
    const auto units = playerGarrison(f, zone, Coord(22, 24));
    const FixPoint soldier = units[0]->getHealth(), trooper = units[1]->getHealth();
    zone->handleDamage(30, NONE_ID, f.enemy);
    require(zone->getGarrisonSize() == 2 && units[0]->getHealth() == soldier - 3
                && units[1]->getHealth() == trooper - 3,
            "occupant_damage_share: a 30-HP hit did not take exactly 3 HP from each occupant");
    for(int i = 0; i < 9; ++i) zone->handleDamage(1, NONE_ID, f.enemy);
    require(units[0]->getHealth() == soldier - 3 && units[1]->getHealth() == trooper - 3,
            "occupant_damage_share: small hits rounded up instead of accumulating");
    zone->handleDamage(1, NONE_ID, f.enemy);
    require(units[0]->getHealth() == soldier - 4 && units[1]->getHealth() == trooper - 4,
            "occupant_damage_share: ten one-HP hits did not add exactly one occupant HP");
    const auto before = zone->getGarrisonHealthRemainders();
    f.game->gameInitSettings.gameType = GameType::CustomGame;
    f.game->gameType = GameType::CustomGame;
    f.game->gameInitSettings.gameOptions.immortalHumanPlayer = true;
    require(f.game->localHumanImmortalityApplies(),
            "occupant_damage_share: the zero-loss arm did not enable single-player immunity");
    const FixPoint building = zone->getHealth();
    zone->handleDamage(100000, NONE_ID, f.enemy);
    require(zone->getHealth() == building && zone->getGarrisonSize() == 2
                && units[0]->getHealth() == soldier - 4 && units[1]->getHealth() == trooper - 4,
            "occupant_damage_share: a hit causing no actual building loss hurt the garrison");
    for(size_t i = 0; i < units.size(); ++i)
        require(zone->getGarrisonHealthRemainders()[i].damage == before[i].damage,
                "occupant_damage_share: zero building damage changed the fractional debt");
    record("occupant_damage_share", "building_loss=40 each_occupant_loss=4 one_hp_hits=10 zero_loss_hit=0 infantry=2->2");
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
    for(int i = 0; i < 2 * halfHealCycles; ++i) f.game->updateGameState();
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
    { std::ofstream file(config); file << "[Map Settings]\nGarrisonHealTime = 30\n"; }
    ObjectData tuned;
    tuned.loadFromINIFile(config, false, false);
    require(tuned.garrisonHealTime == 30, "garrison_regeneration: the named INI setting was not loaded");
    record("garrison_regeneration", "full_heal_seconds=60 saturated=2 after_exit_healing=0 tuned_ini_seconds=30 hash_changed=1");
}

void checkChipDamageSurvival() {
    Fixture f("chip_damage_survival");
    f.game->getCitySimulation()->setCityEffectsEnabled(false);
    auto* zone = f.zone(f.own, Structure_ZoneCommercial, 24, 24, 2);
    std::vector<UnitBase*> units{f.unit(f.own, Unit_Trooper, 22, 24), f.unit(f.own, Unit_Trooper, 22, 25)};
    require(captureByPlayer(f, units, zone) == 2, "chip_damage_survival: Capture did not order both Troopers");
    require(f.runUntil(40, [&] { return zone->getGarrisonSize() == 2; }), "chip_damage_survival: Troopers did not enter");
    const std::vector<Uint32> ids{units[0]->getObjectID(), units[1]->getObjectID()};
    const int max = units[0]->getMaxHealth();
    require(units[0]->getHealth() == max && units[1]->getHealth() == max, "chip_damage_survival: Troopers did not start at full health");
    const int oldHits = 2 * max, expectedHits = 10 * max;
    int firstLoss = 0, lastLoss = 0, atOldLimit = 0;
    for(int hit = 1; hit <= expectedHits; ++hit) {
        // No game cycles pass in this damage-only measurement: regeneration is tested separately.
        // Restore the building, as repeated repairs do, to isolate damage taken by its occupants.
        zone->setHealth(zone->getMaxHealth());
        zone->handleDamage(1, NONE_ID, f.enemy);
        const int count = zone->getGarrisonSize();
        if(count < 2 && !firstLoss) firstLoss = hit;
        if(count == 0 && !lastLoss) lastLoss = hit;
        if(hit == oldHits) atOldLimit = count;
    }
    record("chip_damage_survival", "old_rule_hits=" + std::to_string(oldHits) + " old_limit_survivors="
           + std::to_string(atOldLimit) + " new_rule_first_loss=" + std::to_string(firstLoss)
           + " new_rule_last_loss=" + std::to_string(lastLoss) + " expected_new_hits=" + std::to_string(expectedHits));
    require(atOldLimit == 2 && firstLoss == expectedHits && lastLoss == expectedHits,
            "chip_damage_survival: a repaired building killed its garrison too quickly");
    require(f.game->pInterface->newsticker.hasMessage()
                && f.game->pInterface->newsticker.messages.front().find("Garrison casualties") != std::string::npos,
            "chip_damage_survival: deaths inside did not produce the visible casualty message");
    f.run(1);
    require(!f.alive(ids[0]) && !f.alive(ids[1]), "chip_damage_survival: dead occupants skipped the normal unit death path");
    record("garrison_casualties", "infantry=2->0 normal_death=2 visible_message=2");

    auto* nearRuin = f.zone(f.own, Structure_ZoneIndustrial, 36, 24, 2);
    const auto survivors = playerGarrison(f, nearRuin, Coord(34, 24));
    nearRuin->handleDamage(nearRuin->getHealth().floor() - 1, NONE_ID, f.enemy);
    require(nearRuin->getHealth() == 1 && nearRuin->getGarrisonSize() == 2
                && survivors[0]->getHealth() > 0 && survivors[1]->getHealth() > 0,
            "chip_damage_survival: the garrison did not survive until the building was nearly destroyed");
    record("nearly_destroyed", "building_hp=1/200 infantry=2->2");
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
        auto* zone = f.zone(f.own, Structure_ZoneResidential, 24, 24, 2);
        zone->setCivicOverlay(overlay);
        const auto units = playerGarrison(f, zone, Coord(22, 24));
        for(auto* unit : units) unit->setHealth(unit->getMaxHealth() - 4);
        for(int i = 0; i < 17; ++i) f.game->updateGameState();
        zone->handleDamage(9, NONE_ID, f.enemy);
        const auto occupants = zone->getGarrison();
        const auto remainders = zone->getGarrisonHealthRemainders();
        std::array<FixPoint, DuneCity::ZoneGarrison::kCapacity> health{};
        for(size_t i = 0; i < occupants.size(); ++i)
            health[i] = f.game->getObjectManager().getObject(occupants[i])->getHealth();
        const Uint32 id = zone->getObjectID();
        sim->hospitalCare().setOverlays({{id, static_cast<uint8_t>(overlay)}});
        sim->hospitalCare().setOverlayDays({{id, 1}});
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
        require(zone && zone->getGarrison() == occupants && zone->getCivicOverlay() == overlay && lotDensity(zone) == 2,
                "civic_save: reload changed the occupied civic lot or its two occupants");
        require(f.game->objectData.garrisonHealTime == 30, "civic_save: the configured healing rate did not survive save/load");
        std::vector<UnitBase*> loaded;
        for(size_t i = 0; i < occupants.size(); ++i) {
            auto* unit = dynamic_cast<UnitBase*>(f.game->getObjectManager().getObject(occupants[i]));
            require(unit && hidden(unit), "civic_save: a saved occupant lost its hiding state, id="
                    + std::to_string(occupants[i]));
            require(unit->getHealth() == health[i],
                    "civic_save: occupant HP changed, id=" + std::to_string(occupants[i])
                    + " expected_raw=" + std::to_string(health[i].getRawValue())
                    + " actual_raw=" + std::to_string(unit->getHealth().getRawValue()));
            require(zone->getGarrisonHealthRemainders()[i].damage == remainders[i].damage
                        && zone->getGarrisonHealthRemainders()[i].healing == remainders[i].healing,
                    "civic_save: a fractional damage/healing remainder changed on load");
            loaded.push_back(unit);
        }
        auto* panel = selectZone(f, zone);
        require(panel->garrisonLabel.isVisible() && panel->garrisonLabel.getText().find("2/5") != std::string::npos,
                "civic_save: loaded occupants exist but the player's garrison indicator disappeared");
        zone->handleDamage(1, NONE_ID, f.enemy);
        require(loaded[0]->getHealth() == health[0] - 1 && loaded[1]->getHealth() == health[1] - 1,
                "civic_save: the saved nine-HP fractional debt did not carry into the tenth HP");
        checkFootprint(zone, 2);
        playerEvacuate(f, zone, loaded);
        record("civic_save", "overlay=" + std::to_string(static_cast<int>(overlay))
               + " infantry=2->2 fractional_damage=preserved fractional_healing=preserved ui_count=2/5 evacuated=2");
    }
}
