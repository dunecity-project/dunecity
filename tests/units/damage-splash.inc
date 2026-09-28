// Real-engine ground splash measurement: what an ordinary impact actually removes, through
// the production Map::damage and Bullet::destroy, and proof that the building, air and
// special paths were not moved. Included by projectile-audit.inc (CTest target
// projectile_mechanics_probe); the pure contract is tests/OrdinaryDamagePolicyTestCase.cpp.
//
// Every expectation is written out rather than computed from OrdinaryDamagePolicy, so this
// measures the engine instead of restating the policy to itself.
#include <Definitions.h>
#include <players/DeviationReward.h>

namespace {

// The four quarter-tile band representatives plus the two edges that decide the radius.
constexpr int kProbeCentre = 0;
constexpr int kProbeQuarter = TILESIZE/4;
constexpr int kProbeHalf = TILESIZE/2;
constexpr int kProbeThreeQuarter = (TILESIZE*3)/4;
constexpr int kProbeInsideEdge = TILESIZE-1;
constexpr int kProbeOneTile = TILESIZE;

void runDamageSplashChecks(Game& game, House* owner, House* enemy, Coord site, std::ostream& csv) {
    auto require = [](bool ok, const char* why) {
        if(!ok) throw std::runtime_error(why);
    };
    const Coord victimTile = site + Coord(4,0);
    const int houseID = owner->getHouseID();
    auto nominalOf = [&](int itemID) { return game.objectData.data[itemID][houseID].weapondamage; };

    // A fresh full-health ground victim for every single measurement.
    auto* victim = enemy->createUnit(Unit_Tank, true);
    require(victim != nullptr, "Could not create splash victim");
    victim->deploy(victimTile);
    victim->setVisible(VIS_ALL, true);
    const Coord victimCentre = victim->getCenterPoint();
    require(victim->getMaxHealth() > 60, "Splash victim cannot absorb a full centre hit");

    // One impact, measured. The arguments are the ones Bullet::destroy passes.
    auto removedBy = [&](ObjectBase* shooter, Uint32 bulletID, int nominalDamage, int radius,
                         int offset, bool aimedAir = false) {
        victim->setHealth(victim->getMaxHealth());
        const auto before = victim->getHealth();
        currentGameMap->damage(shooter->getObjectID(), owner, victimCentre + Coord(offset, 0),
                               bulletID, nominalDamage, radius, aimedAir, false, NONE_ID,
                               DeviationReward::liveProvenance(shooter, owner));
        return lround(before - victim->getHealth());
    };

    // --- 1. The accepted ordinary ground profile, per weapon, as hit points actually removed.
    struct Case { const char* name; int shooterItem; Uint32 bullet; int nominal; int band[4]; };
    const Case unitCases[] = {
        // Light guns keep their whole nominal payload at the centre.
        {"soldier",        Unit_Soldier,      Bullet_ShellSmall,  3,  {3, 1, 0, 0}},
        {"trooper_close",  Unit_Trooper,      Bullet_ShellSmall,  5,  {5, 2, 1, 0}},
        {"trooper_ranged", Unit_Trooper,      Bullet_SmallRocket, 4,  {4, 2, 1, 0}},
        {"trike",          Unit_Trike,        Bullet_ShellSmall,  5,  {5, 2, 1, 0}},
        {"raider",         Unit_RaiderTrike,  Bullet_ShellSmall,  5,  {5, 2, 1, 0}},
        {"quad",           Unit_Quad,         Bullet_ShellSmall,  7,  {7, 3, 1, 0}},
        // Heavy shells and the rocket families deliver half.
        {"tank",           Unit_Tank,         Bullet_ShellMedium, 25, {12, 6, 3, 1}},
        {"siege",          Unit_SiegeTank,    Bullet_ShellLarge,  30, {15, 7, 3, 1}},
        {"devastator",     Unit_Devastator,   Bullet_ShellLarge,  40, {20, 10, 5, 2}},
        {"launcher",       Unit_Launcher,     Bullet_Rocket,      75, {37, 18, 9, 4}},
        {"ornithopter",    Unit_Ornithopter,  Bullet_SmallRocket, 45, {22, 11, 5, 2}},
    };
    for(const auto& probe : unitCases) {
        // The Trooper's ranged rocket is the configured payload less a quarter, exactly as
        // UnitBase::attack computes it; everything else is the payload itself.
        const int nominal = probe.bullet == Bullet_SmallRocket && probe.shooterItem == Unit_Trooper
            ? nominalOf(Unit_Trooper) - nominalOf(Unit_Trooper)/4 : nominalOf(probe.shooterItem);
        require(nominal == probe.nominal, "Shipped weapon payload differs from the reviewed table");
        auto* shooter = owner->createUnit(probe.shooterItem, true);
        require(shooter != nullptr, "Could not create splash shooter");
        shooter->deploy(site);
        // Air-carried weapons still splash the ground layer from a ground impact point.
        const int radius = probe.bullet == Bullet_ShellSmall || probe.bullet == Bullet_ShellMedium
                        || probe.bullet == Bullet_ShellLarge || probe.bullet == Bullet_ShellTurret
            ? TILESIZE/2 : TILESIZE;
        const int measured[4] = {removedBy(shooter, probe.bullet, nominal, radius, kProbeCentre),
                                 removedBy(shooter, probe.bullet, nominal, radius, kProbeQuarter),
                                 removedBy(shooter, probe.bullet, nominal, radius, kProbeHalf),
                                 removedBy(shooter, probe.bullet, nominal, radius, kProbeThreeQuarter)};
        for(int i = 0; i < 4; ++i) {
            require(measured[i] == probe.band[i], "Ordinary ground splash differs from the reviewed profile");
            csv << "splash," << probe.name << ',' << i*16 << ',' << nominal << ',' << measured[i] << '\n';
        }
        // The full tile is reached, and exactly one tile away is already outside it. The band
        // shells only carry a half-tile radius of their own, so this also proves the ordinary
        // geometry no longer reads that radius.
        require(removedBy(shooter, probe.bullet, nominal, radius, kProbeInsideEdge) == probe.band[3],
                "Ordinary splash stopped short of a full tile");
        require(removedBy(shooter, probe.bullet, nominal, radius, kProbeOneTile) == 0,
                "Ordinary splash reached a victim a whole tile away");
        csv << "splash," << probe.name << ",64,"  << nominal << ",0\n";
        shooter->cancelDeployment();
    }

    // Turret weapons, fired from real turrets. The Rocket Turret's close cannon carries the
    // Gun-Turret payload, so both turret shells land on the same profile.
    {
        auto* gun = owner->placeStructure(NONE_ID, Structure_GunTurret, site.x, site.y, true, true);
        require(gun != nullptr, "Could not place gun turret");
        require(nominalOf(Structure_GunTurret) == 20, "Shipped gun turret payload differs from the reviewed table");
        const int gunBands[4] = {10, 5, 2, 1};
        for(int i = 0; i < 4; ++i) {
            const int measured = removedBy(gun, Bullet_ShellTurret, 20, TILESIZE/2, i*16);
            require(measured == gunBands[i], "Gun turret ground splash differs from the reviewed profile");
            csv << "splash,gun_turret," << i*16 << ",20," << measured << '\n';
        }
        require(removedBy(gun, Bullet_ShellTurret, 20, TILESIZE/2, kProbeOneTile) == 0,
                "Gun turret splash reached a victim a whole tile away");
        gun->destroy();
    }
    {
        auto* rocketTurret = owner->placeStructure(NONE_ID, Structure_RocketTurret, site.x, site.y, true, true);
        require(rocketTurret != nullptr, "Could not place rocket turret");
        require(nominalOf(Structure_RocketTurret) == 30, "Shipped rocket turret payload differs from the reviewed table");
        const int missileBands[4] = {15, 7, 3, 1};
        const int cannonBands[4] = {10, 5, 2, 1};
        for(int i = 0; i < 4; ++i) {
            const int missile = removedBy(rocketTurret, Bullet_TurretRocket, 30, TILESIZE, i*16);
            const int cannon = removedBy(rocketTurret, Bullet_ShellTurret, 20, TILESIZE/2, i*16);
            require(missile == missileBands[i], "Rocket turret missile splash differs from the reviewed profile");
            require(cannon == cannonBands[i], "Rocket turret cannon splash differs from the reviewed profile");
            csv << "splash,rocket_turret_missile," << i*16 << ",30," << missile << '\n';
            csv << "splash,rocket_turret_cannon," << i*16 << ",20," << cannon << '\n';
        }
        rocketTurret->destroy();
    }

    // --- 2. The diagonal metric, measured rather than asserted about coordinates.
    {
        auto* shooter = owner->createUnit(Unit_Tank, true);
        shooter->deploy(site);
        // 40/40 is 60 by the Dynasty metric, i.e. still inside the outer band, where a
        // Euclidean circle would put it at 56 - the same band here, but 48/32 is the case that
        // separates them: 64 by this metric and 57 Euclidean.
        victim->setHealth(victim->getMaxHealth());
        currentGameMap->damage(shooter->getObjectID(), owner, victimCentre + Coord(40, 40),
                               Bullet_ShellMedium, 25, TILESIZE/2, false, false, NONE_ID,
                               DeviationReward::liveProvenance(shooter, owner));
        require(lround(victim->getMaxHealth() - victim->getHealth()) == 1, "Diagonal splash band is wrong");
        victim->setHealth(victim->getMaxHealth());
        currentGameMap->damage(shooter->getObjectID(), owner, victimCentre + Coord(48, 32),
                               Bullet_ShellMedium, 25, TILESIZE/2, false, false, NONE_ID,
                               DeviationReward::liveProvenance(shooter, owner));
        require(lround(victim->getMaxHealth() - victim->getHealth()) == 0,
                "A victim exactly one tile away on the diagonal still took damage");
        csv << "splash,diagonal,60,25,1\n";
        shooter->cancelDeployment();
    }

    // --- 3. The weapon category is fixed when the shot is fired and survives its source.
    {
        auto* trooper = owner->createUnit(Unit_Trooper, true);
        trooper->deploy(site);
        auto* orni = owner->createUnit(Unit_Ornithopter, true);
        orni->deploy(site + Coord(0, 2));
        Coord start = trooper->getCenterPoint();
        Coord end = victimCentre;
        const int payload = nominalOf(Unit_Trooper) - nominalOf(Unit_Trooper)/4;

        auto landAtCentre = [&](Bullet* b) {
            b->realX = victimCentre.x; b->realY = victimCentre.y;
            victim->setHealth(victim->getMaxHealth());
            const auto before = victim->getHealth();
            b->destroy();  // removes and deletes the bullet, exactly as an arriving shot does
            return lround(before - victim->getHealth());
        };

        // Same projectile, same payload, two sources: infantry keeps the whole payload.
        auto* fromTrooper = new Bullet(trooper->getObjectID(), &start, &end, Bullet_SmallRocket, payload, false, victim);
        bulletList.push_back(fromTrooper);
        require(landAtCentre(fromTrooper) == payload, "A live Trooper's rocket lost its light payload");
        Coord airStart = orni->getCenterPoint();
        auto* fromOrni = new Bullet(orni->getObjectID(), &airStart, &end, Bullet_SmallRocket, payload, false, victim);
        bulletList.push_back(fromOrni);
        require(landAtCentre(fromOrni) == payload/2, "An Ornithopter's rocket was scored as a light weapon");

        // Now the shot outlives its shooter: the Trooper is gone before the rocket lands.
        auto* inFlight = new Bullet(trooper->getObjectID(), &start, &end, Bullet_SmallRocket, payload, false, victim);
        bulletList.push_back(inFlight);
        trooper->cancelDeployment();
        require(currentGame->getObjectManager().getObject(inFlight->shooterID) == nullptr,
                "The Trooper survived its own removal");
        require(landAtCentre(inFlight) == payload,
                "A rocket lost its source identity when the Trooper disappeared");
        csv << "splash,source_identity,0," << payload << ',' << payload << '\n';

        // A shot with no recorded source at all - a pre-provenance save - falls back to the
        // live damager rather than guessing, and to the heavy family when that is gone too.
        auto* trooperAgain = owner->createUnit(Unit_Trooper, true);
        trooperAgain->deploy(site);
        start = trooperAgain->getCenterPoint();
        auto* legacyLive = new Bullet(trooperAgain->getObjectID(), &start, &end, Bullet_SmallRocket, payload, false, victim);
        bulletList.push_back(legacyLive);
        legacyLive->provenance = DeviationReward::Provenance();
        require(landAtCentre(legacyLive) == payload, "The live-damager fallback lost the light payload");
        auto* legacyDead = new Bullet(trooperAgain->getObjectID(), &start, &end, Bullet_SmallRocket, payload, false, victim);
        bulletList.push_back(legacyDead);
        legacyDead->provenance = DeviationReward::Provenance();
        trooperAgain->cancelDeployment();
        require(landAtCentre(legacyDead) == payload/2,
                "A sourceless rocket was scored as a light weapon");
        csv << "splash,legacy_sourceless,0," << payload << ',' << payload/2 << '\n';
        orni->cancelDeployment();
    }

    // --- 4. Buildings keep the full nominal payload; nothing about them moved.
    {
        auto* shooter = owner->createUnit(Unit_Launcher, true);
        shooter->deploy(site);
        auto* tankShooter = owner->createUnit(Unit_Tank, true);
        tankShooter->deploy(site + Coord(0, 1));
        auto* building = enemy->placeStructure(NONE_ID, Structure_WindTrap, victimTile.x, victimTile.y + 3, true, true);
        require(building != nullptr, "Could not place splash building");
        const Coord inside = building->getCenterPoint();
        auto structureLoss = [&](ObjectBase* from, Uint32 bulletID, int nominal, int radius) {
            building->setHealth(building->getMaxHealth());
            const auto before = building->getHealth();
            currentGameMap->damage(from->getObjectID(), owner, inside, bulletID, nominal, radius, false, false,
                                   NONE_ID, DeviationReward::liveProvenance(from, owner));
            return lround(before - building->getHealth());
        };
        require(structureLoss(tankShooter, Bullet_ShellMedium, 25, TILESIZE/2) == 25,
                "A tank shell no longer removes its full nominal damage from a building");
        require(structureLoss(shooter, Bullet_Rocket, 75, TILESIZE) == 75,
                "A launcher rocket no longer removes its full nominal damage from a building");
        // And a hit outside the footprint still does nothing to it.
        building->setHealth(building->getMaxHealth());
        currentGameMap->damage(shooter->getObjectID(), owner, inside + Coord(0, 3*TILESIZE), Bullet_Rocket, 75,
                               TILESIZE, false, false, NONE_ID, DeviationReward::liveProvenance(shooter, owner));
        require(building->getHealth() == building->getMaxHealth(), "A near miss damaged a building");
        csv << "splash,structure_nominal,0,75,75\n";
        building->destroy();
        tankShooter->cancelDeployment(); shooter->cancelDeployment();
    }

    // --- 5. The air layer is untouched: same falloff, same per-bullet radius as before.
    {
        auto* shooter = owner->createUnit(Unit_Launcher, true);
        shooter->deploy(site);
        auto* air = enemy->createUnit(Unit_Ornithopter, true);
        air->deploy(victimTile);
        const Coord airCentre = air->getCenterPoint();
        auto airLoss = [&](Uint32 bulletID, int nominal, int radius, int offset) {
            air->setHealth(air->getMaxHealth());
            const auto before = air->getHealth();
            currentGameMap->damage(shooter->getObjectID(), owner, airCentre + Coord(offset, 0), bulletID,
                                   nominal, radius, true, false, NONE_ID,
                                   DeviationReward::liveProvenance(shooter, owner));
            return lround(before - air->getHealth());
        };
        // Rockets: full payload at the centre and one halving per quarter tile, out to a tile.
        require(airLoss(Bullet_Rocket, 8, TILESIZE, 0) == 8, "Anti-air rocket damage changed");
        require(airLoss(Bullet_Rocket, 8, TILESIZE, 24) == 4, "Anti-air rocket falloff changed");
        require(airLoss(Bullet_Rocket, 8, TILESIZE, 40) == 2, "Anti-air rocket falloff changed");
        require(airLoss(Bullet_Rocket, 8, TILESIZE, TILESIZE) == 0, "Anti-air rocket radius changed");
        // The turret cannon keeps its own half-tile anti-air radius and its full payload.
        require(airLoss(Bullet_ShellTurret, 20, TILESIZE/2, 0) == 20, "Anti-air turret cannon damage changed");
        require(airLoss(Bullet_ShellTurret, 20, TILESIZE/2, 16) == 10, "Anti-air turret cannon falloff changed");
        require(airLoss(Bullet_ShellTurret, 20, TILESIZE/2, TILESIZE/2) == 0, "Anti-air turret cannon radius changed");
        csv << "splash,air_unchanged,0,8,8\n";
        air->cancelDeployment(); shooter->cancelDeployment();
    }

    // --- 6. Every special path keeps its own rule and its own radius.
    {
        auto* devastator = owner->createUnit(Unit_Devastator, true);
        devastator->deploy(site);
        auto* sonicTank = owner->createUnit(Unit_SonicTank, true);
        sonicTank->deploy(site + Coord(0, 1));
        auto* deviator = owner->createUnit(Unit_Deviator, true);
        deviator->deploy(site + Coord(0, 2));
        auto* flameTrooper = owner->createUnit(Unit_Trooper, true);
        flameTrooper->deploy(site + Coord(0, 3));

        // The Devastator death blast passes its item id where a bullet id belongs and must keep
        // falling through the untouched path: half of 150 out to its own 16-unit radius.
        require(removedBy(devastator, Unit_Devastator, 150, 16, 0) == 75, "Devastator death blast centre changed");
        require(removedBy(devastator, Unit_Devastator, 150, 16, 15) == 75, "Devastator death blast band changed");
        require(removedBy(devastator, Unit_Devastator, 150, 16, 16) == 37, "Devastator death blast edge changed");
        require(removedBy(devastator, Unit_Devastator, 150, 16, 17) == 0, "Devastator death blast radius changed");
        csv << "splash,devastator_death,0,150,75\n";

        // Sonic waves: flat damage inside their own three-quarter-tile radius, no falloff.
        require(removedBy(sonicTank, Bullet_Sonic, 12, (TILESIZE*3)/4, 0) == 12, "Sonic wave damage changed");
        require(removedBy(sonicTank, Bullet_Sonic, 12, (TILESIZE*3)/4, 48) == 12, "Sonic wave falloff appeared");
        require(removedBy(sonicTank, Bullet_Sonic, 12, (TILESIZE*3)/4, 49) == 0, "Sonic wave radius changed");
        csv << "splash,sonic,48,12,12\n";

        // Flame keeps its own 16-unit halving and its infantry bonus.
        require(removedBy(flameTrooper, Bullet_Flame, 20, TILESIZE, 0) == 10, "Flame vehicle damage changed");
        require(removedBy(flameTrooper, Bullet_Flame, 20, TILESIZE, 16) == 5, "Flame falloff changed");
        csv << "splash,flame,0,20,10\n";

        // Gas removes no hit points at all, at any distance.
        for(int offset : {0, 24, 64, 120})
            require(removedBy(deviator, Bullet_DRocket, 40, 2*TILESIZE, offset) == 0,
                    "Gas removed hit points from a ground unit");
        csv << "splash,gas,0,40,0\n";

        // The Sandworm swallow only reaches the tile it is on, and always takes everything.
        victim->setHealth(victim->getMaxHealth());
        currentGameMap->damage(devastator->getObjectID(), owner, victimCentre + Coord(2*TILESIZE, 0),
                               Bullet_Sandworm, 5000, NONE_ID, false, false);
        require(victim->getHealth() == victim->getMaxHealth(), "Sandworm swallow reached another tile");
        victim->setHealth(victim->getMaxHealth());
        currentGameMap->damage(devastator->getObjectID(), owner, victimCentre, Bullet_Sandworm, 5000,
                               NONE_ID, false, false);
        require(victim->getHealth() <= 0, "Sandworm swallow no longer consumes its victim");
        victim->setHealth(victim->getMaxHealth());
        victim->setVisible(VIS_ALL, true);
        csv << "splash,sandworm,0,5000,all\n";

        flameTrooper->cancelDeployment(); deviator->cancelDeployment();
        sonicTank->cancelDeployment(); devastator->cancelDeployment();
    }

    victim->cancelDeployment();
}

} // namespace
