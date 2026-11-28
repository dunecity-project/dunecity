// Included inside ZoneGarrisonProbe. All counters come from real projectiles,
// Capture uses the player's widgets, and Stats opens from its sidebar button.

HouseStatsWindow* openGarrisonStats(Fixture& f) {
    press(f.game->pInterface->statsButton);
    f.game->pInterface->processChildWindowOpenCloses();
    auto* stats = dynamic_cast<HouseStatsWindow*>(f.game->pInterface->pChildWindow);
    require(stats != nullptr, "garrison_stats: the Stats button did not open its window");
    stats->draw(Point(0, 0));
    require(stats->selectedHouseID == f.own->getHouseID(), "garrison_stats: Stats shows the wrong house");
    return stats;
}

void checkGarrisonStats() {
    for(const int item : {Unit_Trooper, Unit_Soldier})
    for(const Coord at : {Coord(23, 31), Coord(18, 31), Coord(20, 28), Coord(20, 33),
                          Coord(22, 29), Coord(19, 29), Coord(22, 32), Coord(19, 32),
                          Coord(23, 28), Coord(18, 28), Coord(23, 33), Coord(18, 33), Coord(25, 31)}) {
        if(item == Unit_Soldier && at.x == 25) continue; // Only Troopers have this rocket range.
        Fixture f("garrison_stats_" + std::to_string(item) + "_" + std::to_string(at.x) + "_" + std::to_string(at.y));
        f.game->getCitySimulation()->setCityEffectsEnabled(false);
        f.own->setAutoRepairEnabled(false);
        f.own->producedPower = 10000; // Isolate gunfire from periodic unpowered-building decay.
        auto* zone = f.zone(f.own, Structure_ZoneIndustrial, 20, 30);
        auto* shooter = f.unit(f.own, item, 19, 29);
        zone->foundationDegrades = false;
        require(captureByPlayer(f, {shooter}, zone) == 1, "garrison_stats: Capture did not queue the occupant");
        require(f.runUntil(30, [&] { return zone->getGarrisonSize() == 1; }),
                "garrison_stats: the occupant never entered");
        require(hidden(shooter), "garrison_stats: the shooter is not inside the building");
        // A house must see its real Soldiers even if it cannot currently build them.
        f.game->objectData.data[item][f.own->getHouseID()].enabled = false;
        for(const int squad : {Unit_Infantry, Unit_Troopers, Unit_Infantry5, Unit_Troopers5})
            f.game->objectData.data[squad][f.own->getHouseID()].enabled = true;
        const int enemyDeaths = f.enemy->getNumLostItems(Unit_Soldier);
        const FixPoint buildingHealth = zone->getHealth();
        auto* target = f.unit(f.enemy, Unit_Soldier, at.x, at.y, STOP);
        if(blockDistance(zone->getClosestPoint(at), at) > shooter->getWeaponRange()) {
            const FixPoint targetHealth = target->getHealth();
            f.run(5);
            require(hidden(shooter) && zone->getHealth() == buildingHealth && target->getHealth() == targetHealth
                        && f.own->getCombatReward(item).hpRemovedMilli == 0,
                    "garrison_stats: an out-of-range target caused firing or building damage");
            record("garrison_stats_out_of_range", "item=" + std::to_string(item) + " target="
                   + std::to_string(at.x) + "," + std::to_string(at.y) + " damage=0 own_building_damage=0");
            continue;
        }
        const Uint32 targetID = target->getObjectID();
        require(f.runUntil(40, [&] {
                    require(zone->getHealth() == buildingHealth,
                            "garrison_stats: garrison fire damaged its own building");
                    return !f.alive(targetID);
                }),
                "garrison_stats: the occupant did not kill the enemy from inside");
        require(hidden(shooter) && zone->getGarrisonSize() == 1,
                "garrison_stats: the shooter left the building to get its kill");
        const auto reward = f.own->getCombatReward(item);
        require(reward.kills == 1 && reward.hpRemovedMilli > 0,
                "garrison_stats: the real occupant type did not receive its kill and damage");
        require(f.own->getCombatReward(zone->getItemID()).kills == 0
                    && f.own->getCombatReward(zone->getItemID()).hpRemovedMilli == 0,
                "garrison_stats: the building took its occupant's credit");
        require(f.enemy->getNumLostItems(Unit_Soldier) == enemyDeaths + 1
                    && f.enemy->getCombatReward(item).kills == 0,
                "garrison_stats: the victim death or firing house attribution is wrong");
        auto* stats = openGarrisonStats(f);
        auto rowIndex = [&](int wanted) {
            for(size_t i = 0; i < stats->ledger.size(); ++i)
                if(stats->ledger[i].itemID == Uint32(wanted)) return int(i);
            return -1;
        };
        const int index = rowIndex(item);
        require(index >= 0, "garrison_stats: an owned infantry type is missing when production is disabled");
        const auto& row = stats->ledger[index];
        require(row.name == (item == Unit_Soldier ? "Soldier" : "Trooper")
                    && row.active == 1 && row.kills == reward.kills
                    && row.damageHp == reward.hpRemovedMilli / 1000 && row.damageHp > 0,
                "garrison_stats: live Stats does not show the real infantry name, active count, kills and damage");
        for(const int squad : {Unit_Infantry, Unit_Troopers, Unit_Infantry5, Unit_Troopers5})
            require(rowIndex(squad) < 0, "garrison_stats: an empty squad purchase row is still listed");
        stats->onClose();
        f.game->pInterface->processChildWindowOpenCloses();

        record("garrison_stats", "item=" + std::to_string(item) + " target=" + std::to_string(at.x) + ","
               + std::to_string(at.y) + " kills=" + std::to_string(reward.kills)
               + " damage_hp=" + std::to_string(reward.hpRemovedMilli / 1000)
               + " active=1 purchase_rows=0 disabled_owned_row=1 own_building_damage=0");
    }
}
