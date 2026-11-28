// Include inside ZoneGarrisonProbe after zone-garrison-stats-probe.inc.
// This case belongs with the shield/collapse rule, not the independent Stats fix.
void checkGarrisonCollapseStats() {
    Fixture f("garrison_collapse_stats");
    f.game->getCitySimulation()->setCityEffectsEnabled(false);
    f.own->setAutoRepairEnabled(false);
    auto* zone = f.zone(f.own, Structure_ZoneIndustrial, 20, 30);
    const auto occupants = playerGarrison(f, zone, Coord(18, 30));
    std::vector<Uint32> ids;
    for(auto* unit : occupants) {
        ids.push_back(unit->getObjectID());
        unit->setHealth(1);
    }
    zone->setHealth(1);
    zone->repairing = false;
    zone->foundationDegrades = false;
    const Uint32 zoneID = zone->getObjectID();
    auto* attacker = f.unit(f.enemy, Unit_Tank, 25, 31, STOP);
    Command(f.playerOf(f.enemy), CMD_UNIT_ATTACKOBJECT, attacker->getObjectID(), zoneID).executeCommand();
    require(f.runUntil(30, [&] { return !f.alive(zoneID) && !f.alive(ids[0]) && !f.alive(ids[1]); }),
            "garrison_collapse_stats: a projectile-destroyed zone did not eject/kill its wounded occupants");
    auto* stats = openGarrisonStats(f);
    for(const int item : {Unit_Soldier, Unit_Trooper}) {
        require(f.own->getNumLostItems(item) == 1,
                "garrison_collapse_stats: collapse did not record the occupant death under its real type");
        const auto row = std::find_if(stats->ledger.begin(), stats->ledger.end(),
                                     [=](const auto& entry) { return entry.itemID == Uint32(item); });
        require(row != stats->ledger.end() && row->active == 0 && row->deaths == 1,
                "garrison_collapse_stats: live Stats omitted the collapsed building's casualty");
    }
    record("garrison_collapse_stats", "building_destroyed=1 Soldier_deaths=1 Trooper_deaths=1 live_stats_casualties=2");
}
