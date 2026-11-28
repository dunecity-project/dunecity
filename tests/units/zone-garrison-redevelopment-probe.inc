// Included inside ZoneGarrisonProbe, after its real Capture/Evacuate widget helpers.
// Only the environment/day of the production census is forced; entry and exit run through
// the player's buttons, map clicks, queued commands and real game cycles.

enum class Redevelopment { Growth, DemandDecline, PowerDecline };

int lotDensity(const ZoneStructure* zone) {
    return currentGameMap->getTile(zone->getLocation())->getCityZoneDensity();
}

int lotPopulation(const ZoneStructure* zone) {
    return DuneCity::getStructurePopulation(zone, lotDensity(zone));
}

void prepareCensus(Fixture& f, bool negativeDemand = false) {
    auto* sim = f.game->getCitySimulation();
    for(int h = 0; h < DuneCity::kMaxCityHouses; ++h) {
        auto& state = sim->getHouseStateMut(h);
        state.resValve = state.comValve = state.indValve = negativeDemand ? -2000 : 2000;
        state.taxRate = negativeDemand ? 20 : 0;
    }
    for(int y = 0; y < currentGameMap->getSizeY(); y += 2) {
        for(int x = 0; x < currentGameMap->getSizeX(); x += 2) {
            sim->landValueMap_.set(x / sim->landValueMap_.getBlockSize(), y / sim->landValueMap_.getBlockSize(),
                                  negativeDemand ? 0 : 200);
            sim->pollutionDensityMap_.set(x / sim->pollutionDensityMap_.getBlockSize(),
                                         y / sim->pollutionDensityMap_.getBlockSize(), negativeDemand ? 250 : 0);
            sim->crimeRateMap_.set(x / sim->crimeRateMap_.getBlockSize(), y / sim->crimeRateMap_.getBlockSize(),
                                  negativeDemand ? 250 : 0);
        }
    }
}

std::vector<UnitBase*> playerGarrison(Fixture& f, ZoneStructure* zone, Coord first) {
    auto* soldier = f.unit(f.own, Unit_Soldier, first.x, first.y);
    auto* trooper = f.unit(f.own, Unit_Trooper, first.x, first.y + 1);
    const std::vector<UnitBase*> units{soldier, trooper};
    require(captureByPlayer(f, units, zone) == 2, f.label + ": Capture did not queue both orders");
    require(f.runUntil(40, [&] { return zone->getGarrisonSize() == 2; }),
            f.label + ": Capture never put both soldiers inside");
    for(const auto* unit : units) require(hidden(unit), f.label + ": an occupant is still on the map");
    return units;
}

void playerEvacuate(Fixture& f, ZoneStructure* zone, const std::vector<UnitBase*>& units) {
    auto* panel = selectZone(f, zone);
    require(panel->evacuateButton.isVisible() && panel->evacuateButton.isEnabled(),
            f.label + ": the real Evacuate button is unavailable");
    press(panel->evacuateButton);
    require(f.runUntil(5, [&] { return !zone->isGarrisoned(); }), f.label + ": Evacuate did not finish");
    for(const auto* unit : units)
        require(onMapBeside(unit, zone, 2) && unit->getHealth() > 0,
                f.label + ": an evacuated soldier is missing or still hidden");
}

void checkFootprint(const ZoneStructure* zone, int density) {
    const auto at = zone->getLocation();
    for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx) {
        const auto* tile = currentGameMap->getTile(at.x + dx, at.y + dy);
        require(tile->getNonInfantryGroundObject() == zone && tile->getCityZoneType() == zone->getZoneType()
                    && tile->getCityZoneDensity() == density && tile->getOwner() == zone->getOwner()->getHouseID(),
                "redevelopment: a footprint tile drifted from its structure");
    }
}

void checkRedevelopmentHeld(Redevelopment kind) {
    const bool growth = kind == Redevelopment::Growth;
    const bool demand = kind == Redevelopment::DemandDecline;
    const std::string name = growth ? "growth_held" : demand ? "demand_decline_held" : "power_decline_held";
    bool allHeld = true;
    int activations = 0;
    for(int item : {Structure_ZoneResidential, Structure_ZoneCommercial, Structure_ZoneIndustrial}) {
        Fixture f(name);
        auto* sim = f.game->getCitySimulation();
        sim->setCityEffectsEnabled(false); // The test schedules real census passes explicitly.
        f.game->gameInitSettings.gameOptions.unpoweredZonesVacate = true;
        f.own->producedPower = 10000;
        auto* zone = f.zone(f.own, item, 24, 24, growth ? 0 : 2);
        auto* control = f.zone(f.own, item, 32, 24, growth ? 0 : 2);
        const auto units = playerGarrison(f, zone, Coord(22, 24));
        const auto occupants = zone->getGarrison();
        const int population = lotPopulation(zone), density = lotDensity(zone), power = zone->getZonePowerDraw();
        const int soldiers = f.own->getNumItems(Unit_Soldier), troopers = f.own->getNumItems(Unit_Trooper);
        const Uint32 zoneID = zone->getObjectID();
        f.own->producedPower = kind == Redevelopment::PowerDecline ? 0 : 10000;
        for(int day = 0; day < 16; ++day) {
            prepareCensus(f, demand);
            ++sim->lastProcessedDay_;
            sim->runZoneGrowth();
        }
        int live = 0;
        for(const Uint32 id : occupants) {
            const auto* unit = dynamic_cast<const UnitBase*>(f.game->getObjectManager().getObject(id));
            live += unit && unit->getHealth() > 0;
        }
        record(name, "item=" + std::to_string(item) + " occupants_before=2 occupants_after="
               + std::to_string(zone->getGarrisonSize()) + " infantry_before=2 infantry_after=" + std::to_string(live)
               + " density=" + std::to_string(density) + "->" + std::to_string(lotDensity(zone))
               + " population=" + std::to_string(population) + "->" + std::to_string(lotPopulation(zone))
               + " control_population=" + std::to_string(lotPopulation(control)));
        require(f.alive(zoneID) && zone->getGarrison() == occupants && live == 2
                    && f.own->getNumItems(Unit_Soldier) == soldiers && f.own->getNumItems(Unit_Trooper) == troopers,
                name + ": the census lost a zone or a soldier");
        for(const auto* unit : units) require(hidden(unit), name + ": the census ejected an occupant");
        allHeld &= lotDensity(zone) == density && lotPopulation(zone) == population && zone->getZonePowerDraw() == power;
        checkFootprint(zone, lotDensity(zone));
        require(growth ? lotPopulation(control) > population : lotPopulation(control) < population,
                name + ": the empty control did not exercise the requested transition");
        ++activations;
        playerEvacuate(f, zone, units);
        for(int day = 0; day < 16; ++day) {
            prepareCensus(f, demand);
            ++sim->lastProcessedDay_;
            sim->runZoneGrowth();
        }
        require(growth ? lotPopulation(zone) > population : lotPopulation(zone) < population,
                name + ": the evacuated lot did not resume redevelopment");
        checkFootprint(zone, lotDensity(zone));
        record(name + "_released", "item=" + std::to_string(item) + " activations=1 infantry_after=2 density="
               + std::to_string(lotDensity(zone)) + " population=" + std::to_string(lotPopulation(zone)));
    }
    require(activations == 3 && allHeld, name + ": an occupied R/C/I lot changed population, density or power draw");
}

void checkGrowthHeld() { checkRedevelopmentHeld(Redevelopment::Growth); }
void checkDemandDeclineHeld() { checkRedevelopmentHeld(Redevelopment::DemandDecline); }
void checkPowerDeclineHeld() { checkRedevelopmentHeld(Redevelopment::PowerDecline); }

void checkCivicHeld() {
    using Overlay = ZoneStructure::CivicOverlay;
    bool allHeld = true;
    for(const auto overlay : {Overlay::None, Overlay::Hospital, Overlay::Church}) {
        Fixture f("civic_held");
        auto* sim = f.game->getCitySimulation();
        sim->setCityEffectsEnabled(false);
        f.own->producedPower = 10000;
        auto* zone = f.zone(f.own, Structure_ZoneResidential, 20, 20, 3);
        zone->setCivicOverlay(overlay);
        // Enough population for a designation, but no existing civic buildings: the held
        // ordinary lot is the census's first candidate. A held civic has no such quota.
        if(overlay == Overlay::None)
            for(int i = 1; i < 12; ++i) f.zone(f.own, Structure_ZoneResidential, 20 + (i % 4) * 4, 20 + (i / 4) * 4, 3);
        const auto units = playerGarrison(f, zone, Coord(18, 20));
        sim->hospitalCare().setOverlayDays({{zone->getObjectID(), 1}});
        sim->lastProcessedDay_ = 400;
        prepareCensus(f);
        sim->runZoneGrowth();
        const auto after = zone->getCivicOverlay();
        record("civic_held", "overlay=" + std::to_string(static_cast<int>(overlay)) + "->"
               + std::to_string(static_cast<int>(after)) + " occupants_before=2 occupants_after="
               + std::to_string(zone->getGarrisonSize()));
        allHeld &= after == overlay;
        require(zone->getGarrisonSize() == 2, "civic_held: the census lost an occupant");
        playerEvacuate(f, zone, units);
        if(overlay == Overlay::None)
            for(auto* structure : structureList)
                if(auto* other = dynamic_cast<ZoneStructure*>(structure); other && other != zone)
                    other->setCivicOverlay(Overlay::None);
        sim->hospitalCare().houseChanges(f.own->getHouseID()) = {};
        sim->lastProcessedDay_ = 800;
        prepareCensus(f);
        sim->runZoneGrowth();
        require(overlay == Overlay::None ? zone->getCivicOverlay() != Overlay::None
                                        : zone->getCivicOverlay() == Overlay::None,
                "civic_held: the released lot did not permit a civic type change");
        record("civic_released", "old_overlay=" + std::to_string(static_cast<int>(overlay))
               + " new_overlay=" + std::to_string(static_cast<int>(zone->getCivicOverlay())) + " infantry_after=2");
    }
    require(allHeld, "civic_held: the census changed a garrisoned building's civic type");
}

void checkDemolitionEjects() {
    for(const auto command : {CMD_ZONE_DEMOLISH, CMD_STRUCTURE_DEMOLISH}) {
        Fixture f("demolition_ejects");
        f.game->getCitySimulation()->setCityEffectsEnabled(false);
        auto* zone = f.zone(f.own, Structure_ZoneIndustrial, 24, 24, 2);
        const auto units = playerGarrison(f, zone, Coord(22, 24));
        const Uint32 id = zone->getObjectID();
        std::vector<int> health;
        for(const auto* unit : units) health.push_back(unit->getHealth().lround());
        Command(f.playerOf(f.own), command, id).executeCommand();
        require(!f.alive(id), "demolition_ejects: demolition did not remove the zone");
        f.run(1);
        for(size_t i = 0; i < units.size(); ++i) {
            const auto* unit = units[i];
            const int collapseDamage = static_cast<int>((static_cast<int64_t>(unit->getMaxHealth())
                * f.game->objectData.garrisonCollapseDamagePercent + 99) / 100);
            require(f.alive(unit->getObjectID()) && unit->isActive() && !unit->isPickedUp()
                        && currentGameMap->tileExists(unit->getLocation())
                        && unit->getHealth().lround() == health[i] - collapseDamage,
                    "demolition_ejects: demolition silently lost or failed to hurt an occupant");
        }
        for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx) {
            const auto* tile = currentGameMap->getTile(24 + dx, 24 + dy);
            require(!tile->hasAStructure() && !tile->hasCityZone() && tile->getCityZoneDensity() == 0,
                    "demolition_ejects: demolished lot retained simulation state");
        }
        record("demolition_ejects", "command=" + std::to_string(command) + " before=2 after=2 active=2 hurt=2");
    }
}

void checkBundledHeld() {
    Fixture f("bundled_metropolis_held", true, true);
    auto* sim = f.game->getCitySimulation();
    sim->setCityEffectsEnabled(false);
    ZoneStructure* zone = nullptr;
    Coord entry;
    for(auto* structure : structureList) {
        auto* candidate = dynamic_cast<ZoneStructure*>(structure);
        if(!candidate || candidate->getOwner() != f.enemy || candidate->tooDamagedToHold()) continue;
        const auto at = candidate->getLocation();
        bool coherent = true;
        for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx) {
            const auto* tile = currentGameMap->getTile(at.x + dx, at.y + dy);
            coherent &= tile && tile->getNonInfantryGroundObject() == candidate
                && tile->getCityZoneType() == candidate->getZoneType()
                && tile->getOwner() == f.enemy->getHouseID();
        }
        for(const auto* other : structureList) {
            if(other == candidate) continue;
            const auto p = other->getLocation(), size = other->getStructureSize();
            if(p.x < at.x + 2 && p.x + size.x > at.x && p.y < at.y + 2 && p.y + size.y > at.y)
                coherent = false;
        }
        if(!coherent) continue;
        const Coord spot(at.x - 1, at.y);
        if(!currentGameMap->tileExists(spot) || !currentGameMap->tileExists(spot.x, spot.y + 1)) continue;
        bool threatened = false;
        const auto threatensEntry = [&](const ObjectBase* object) {
            return object->getOwner()->getTeamID() != f.own->getTeamID() && object->canAttack()
                && blockDistance(object->getClosestPoint(spot), spot) <= object->getWeaponRange() + 2;
        };
        for(const auto* object : structureList) threatened |= threatensEntry(object);
        for(const auto* object : unitList) threatened |= threatensEntry(object);
        if(threatened) continue;
        const auto* a = currentGameMap->getTile(spot);
        const auto* b = currentGameMap->getTile(spot.x, spot.y + 1);
        if(a && b && !a->hasAGroundObject() && !b->hasAGroundObject()) { zone = candidate; entry = spot; break; }
    }
    require(zone != nullptr, "bundled_metropolis_held: no enemy lot with a safe accessible entrance");
    // The bundled conversion deliberately restarts density at zero (map header).
    // Develop this existing lot before the player enters; its real footprint, roads
    // and neighbouring city structures are retained.
    zone->setStartingLevel(2);
    const auto units = playerGarrison(f, zone, entry);
    require(zone->getOwner() == f.own, "bundled_metropolis_held: Capture did not take the enemy lot");
    const int density = lotDensity(zone), population = lotPopulation(zone);
    const auto occupants = zone->getGarrison();
    f.own->producedPower = 0;
    prepareCensus(f);
    ++sim->lastProcessedDay_;
    sim->runZoneGrowth();
    record("bundled_metropolis_held", "density=" + std::to_string(density) + "->" + std::to_string(lotDensity(zone))
           + " population=" + std::to_string(population) + "->" + std::to_string(lotPopulation(zone))
           + " occupants_before=2 occupants_after=" + std::to_string(zone->getGarrisonSize()));
    const bool held = lotDensity(zone) == density && lotPopulation(zone) == population;
    require(zone->getGarrison() == occupants, "bundled_metropolis_held: a soldier disappeared during the census");
    playerEvacuate(f, zone, units);
    require(zone->getOwner() == f.enemy, "bundled_metropolis_held: evacuation did not return the captured lot");
    f.enemy->producedPower = 0;
    prepareCensus(f);
    ++sim->lastProcessedDay_;
    sim->runZoneGrowth();
    require(lotPopulation(zone) < population, "bundled_metropolis_held: the released lot did not decline");
    checkFootprint(zone, lotDensity(zone));
    record("bundled_metropolis_released", "infantry_after=2 active=2 population=" + std::to_string(lotPopulation(zone)));
    require(held, "bundled_metropolis_held: the captured, occupied lot redeveloped");
}
