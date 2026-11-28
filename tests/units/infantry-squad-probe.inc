// Infantry squads and taxable infantry housing through the player's own path (docs/infantry-squads.md).
//
// Stefan, 2026-11-28: "in the original dune you could build three troopers for a discount - i think maybe 3
// were 200 credits?" ... "yes" ... "both wor and infantry barracks count as a high density residential and is
// taxable". Every case drives what a player does: select the building, click its build-list button (the real
// BuilderList widget), run game cycles, and assert what the player sees and gets.
//
// build_list     the Barracks list offers Soldier 30 and Infantry 50, the WOR list Trooper 100 and Troopers 200
// troopers       one click on Troopers costs exactly 200 and puts exactly three Troopers on the map, as a
//                group; one Move click sends all three, and all three arrive
// infantry       one click on Infantry costs exactly 50 and puts exactly three Soldiers on the map
// singles        one click on Trooper costs 100 and gives one Trooper; Soldier 30 and one Soldier
// barracks_tax   a Barracks ordered from the Construction Yard list and placed with a map click adds exactly
//                one level-3 Residential zone's tax base (80 eighths) and population (40); destroyed, both go
// wor_tax        the same for a WOR
// land_value     a Barracks or WOR raises the land value of own lots 2-4 tiles away by more than 0 and by 20-50% of
//                what a Palace on the same spot does ("and they moderately increase land value")
// police         a Barracks or WOR gives 0.50 (+-0.02) of a Police Station's police coverage on every cell around the
//                same spot, also at 50% Police funding, and crime on each lot is crime before police less that
//                coverage ("and infantry and wor have 50% police effect")

#include <House.h>
#include <Map.h>
#include <Tile.h>
#include <Command.h>
#include <ScreenBorder.h>
#include <GameInterface.h>
#include <GUI/dune/BuilderList.h>
#include <GUI/ObjectInterfaces/BuilderInterface.h>
#include <players/Player.h>
#include <structures/BuilderBase.h>
#include <structures/ConstructionYard.h>
#include <units/UnitBase.h>
#include <dunecity/CitySimulation.h>
#include <dunecity/CityEffects.h>
#include <dunecity/InfantrySquadPolicy.h>
#include <structures/ZoneStructure.h>
#include <algorithm>
#include <mod/ModManager.h>
#include <cstdio>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace InfantrySquadProbe {

inline void require(bool ok, const std::string& why) {
    if(!ok) {
        SDL_Log("INFANTRY_SQUAD_PROBE_FAIL: %s", why.c_str());
        throw std::runtime_error(why);
    }
}

void record(const std::string& name, const std::string& what) {
    SDL_Log("INFANTRY_SQUAD_PROBE_CASE: %s: %s", name.c_str(), what.c_str());
}

constexpr int kMapSize = 64;
SettingsClass::GameOptionsClass gModOptions;

std::string fixtureMap() {
    std::string data = "[BASIC]\nVersion=2\nWinFlags=3\nLoseFlags=1\nTechLevel=8\n[MAP]\nSizeX="
                       + std::to_string(kMapSize) + "\nSizeY=" + std::to_string(kMapSize) + "\n";
    for(int row = 0; row < kMapSize; ++row) {
        char key[16];
        std::snprintf(key, sizeof(key), "%03d=", row);
        data += key + std::string(kMapSize, '%') + "\n";
    }
    data += "[Player1]\nCredits=2000\nBrain=Team1\n[Player2]\nCredits=2000\nBrain=Team2\n"
            "[UNITS]\nID001=Player1,MCV,256,0,64,Guard\nID002=Player2,MCV,256,"
            + std::to_string(kMapSize * kMapSize - 1) + ",64,Guard\n";
    return data;
}

/// Ordos (local; it builds both a Barracks and a WOR) against Harkonnen, in a city game.
struct Fixture {
    std::unique_ptr<Game> game;
    House* own = nullptr;
    std::string label;

    explicit Fixture(const std::string& name) : label(name) {
        auto options = gModOptions;
        options.immortalHumanPlayer = false;
        GameInitSettings init("infantry-squad.ini", fixtureMap(), "Infantry squads", true, options);
        init.randomSeed = 23;
        GameInitSettings::HouseInfo o(HOUSE_ORDOS, 1);
        o.addPlayerInfo({"Squad tester", "HumanPlayer"});
        init.addHouseInfo(o);
        GameInitSettings::HouseInfo h(HOUSE_HARKONNEN, 2);
        h.addPlayerInfo({"Enemy", "HumanPlayer"});
        init.addHouseInfo(h);
        game = std::make_unique<Game>();
        currentGame = game.get();
        game->bReplay = true;   // Private fixture, never a public match-analytics sample.
        game->initGame(init);
        game->bReplay = false;
        game->initializeGameLoop();
        own = game->getHouse(HOUSE_ORDOS);
        require(own != nullptr, label + ": fixture house missing");
        require(game->isCitySimEnabled(), label + ": the fixture is not a city game");
        own->startingCredits = 20000;
        own->enforceCreditCeiling();
    }

    ~Fixture() {
        game.reset();
        currentGame = nullptr;
    }

    StructureBase* place(int item, int x, int y) {
        for(int dy = 0; dy < 3; ++dy) for(int dx = 0; dx < 3; ++dx) currentGameMap->getTile(x + dx, y + dy)->setType(Terrain_Rock);
        auto* s = own->placeStructure(NONE_ID, item, x, y, true, true);
        require(s != nullptr, label + ": cannot place item " + std::to_string(item));
        s->setVisible(VIS_ALL, true);
        return s;
    }

    /// Power, a Radar and the producer: what a player has when the WOR or Barracks list is open.
    BuilderBase* producer(int item, int x, int y) {
        for(int i = 0; i < 4; ++i) place(Structure_WindTrap, 2 + 2 * i, 2);
        place(Structure_Radar, 2, 5);
        if(item == Structure_WOR) place(Structure_Barracks, 5, 5);
        auto* b = dynamic_cast<BuilderBase*>(place(item, x, y));
        require(b != nullptr, label + ": the producer is not a builder");
        b->updateBuildList();
        return b;
    }

    void run(int seconds) {
        const Uint32 end = game->getGameCycleCount() + MILLI2CYCLES(seconds * 1000);
        while(game->getGameCycleCount() < end) game->updateGameState();
    }

    template<class F> bool runUntil(int seconds, F done) {
        const Uint32 end = game->getGameCycleCount() + MILLI2CYCLES(seconds * 1000);
        while(game->getGameCycleCount() < end) {
            if(done()) return true;
            game->updateGameState();
        }
        return done();
    }

    std::vector<UnitBase*> units(int item) const {
        std::vector<UnitBase*> found;
        for(UnitBase* u : unitList) if(u->getOwner() == own && u->getItemID() == item && u->getHealth() > 0) found.push_back(u);
        return found;
    }
};

/// Selects BUILDER as a selection click does and returns the build list the player sees.
BuilderList* openList(Fixture& f, BuilderBase* builder) {
    Game& game = *f.game;
    game.unselectAll(game.selectedList);
    game.selectedList.clear();
    game.selectedList.insert(builder->getObjectID());
    builder->setSelected(true);
    game.selectionChanged();
    game.setCursorMode(Game::CursorMode_Normal);
    game.pInterface->updateObjectInterface();
    auto* panel = dynamic_cast<BuilderInterface*>(game.pInterface->pObjectContainer);
    require(panel != nullptr && panel->pBuilderList != nullptr, f.label + ": the builder's panel has no build list");
    panel->update();
    return panel->pBuilderList;
}

/// The price the build list shows for ITEM, or -1 when it does not offer it.
int listedPrice(const BuilderBase* builder, Uint32 item) {
    for(const BuildItem& offer : builder->getBuildList()) if(offer.itemID == item) return static_cast<int>(offer.price);
    return -1;
}

/// A real left click on ITEM's button in the build list (press and release on the button).
void clickItem(Fixture& f, BuilderBase* builder, BuilderList* list, Uint32 item) {
    int index = 0;
    for(const BuildItem& offer : builder->getBuildList()) { if(offer.itemID == item) break; ++index; }
    require(index < builder->getBuildListSize(), f.label + ": item " + std::to_string(item) + " is not in the list");
    list->currentListPos = index;
    require(list->getItemIDFromIndex(index) == static_cast<int>(item), f.label + ": the list button shows another item");
    const Point button = list->getButtonPosition(0) + Point(10, 10);
    SDL_SetModState(KMOD_NONE);
    list->handleMouseLeft(button.x, button.y, true);
    list->handleMouseLeft(button.x, button.y, false);
}

/// Buys ITEM from PRODUCER with one click; asserts the price paid and the men that walk out.
std::vector<UnitBase*> buyOnce(Fixture& f, BuilderBase* producer, Uint32 item, Uint32 man, int price, int men) {
    BuilderList* list = openList(f, producer);
    require(listedPrice(producer, item) == price, f.label + ": the list prices item " + std::to_string(item) + " at "
            + std::to_string(listedPrice(producer, item)) + ", not " + std::to_string(price));
    std::set<Uint32> before;
    for(UnitBase* u : f.units(man)) before.insert(u->getObjectID());
    const int credits = f.own->getCredits();
    clickItem(f, producer, list, item);
    const int buildSeconds = f.game->objectData.data[item][f.own->getHouseID()].buildtime * 15 / 62 + 8;
    std::vector<UnitBase*> fresh;
    // What the order cost is what the builder took for it (its production progress, paid from the house's
    // credits as it builds): the credit balance also moves with the city budget while the men are built.
    FixPoint paid = 0;
    const bool out = f.runUntil(buildSeconds + 30, [&] {
        if(producer->getCurrentProducedItem() == item) paid = std::max(paid, producer->getProductionProgress());
        fresh.clear();
        for(UnitBase* u : f.units(man)) if(!before.count(u->getObjectID())) fresh.push_back(u);
        return producer->getProductionQueueSize() == 0 && static_cast<int>(fresh.size()) >= men;
    });
    require(out, f.label + ": " + std::to_string(fresh.size()) + " of " + std::to_string(men) + " men came out");
    f.run(3);
    fresh.clear();
    for(UnitBase* u : f.units(man)) if(!before.count(u->getObjectID())) fresh.push_back(u);
    require(static_cast<int>(fresh.size()) == men, f.label + ": the order put " + std::to_string(fresh.size())
            + " men on the map, not " + std::to_string(men));
    require(paid.lround() == price, f.label + ": the builder took " + std::to_string(paid.lround()) + " for the order, not "
            + std::to_string(price));
    require(credits - f.own->getCredits() >= price, f.label + ": the house's credits fell by only "
            + std::to_string(credits - f.own->getCredits()) + " for an order of " + std::to_string(price));
    return fresh;
}

void buildList() {
    Fixture f("build_list");
    auto* barracks = f.producer(Structure_Barracks, 10, 10);
    openList(f, barracks);
    require(listedPrice(barracks, Unit_Soldier) == 30, "build_list: Barracks Soldier " + std::to_string(listedPrice(barracks, Unit_Soldier)));
    require(listedPrice(barracks, Unit_Infantry) == 50, "build_list: Barracks Infantry " + std::to_string(listedPrice(barracks, Unit_Infantry)));
    auto* wor = dynamic_cast<BuilderBase*>(f.place(Structure_WOR, 14, 10));
    require(wor != nullptr, "build_list: no WOR");
    wor->updateBuildList();
    openList(f, wor);
    require(listedPrice(wor, Unit_Trooper) == 100, "build_list: WOR Trooper " + std::to_string(listedPrice(wor, Unit_Trooper)));
    require(listedPrice(wor, Unit_Troopers) == 200, "build_list: WOR Troopers " + std::to_string(listedPrice(wor, Unit_Troopers)));
    record("build_list", "Barracks: Soldier 30, Infantry 50; WOR: Trooper 100, Troopers 200");
}

void troopers() {
    Fixture f("troopers");
    auto* wor = f.producer(Structure_WOR, 10, 10);
    std::vector<UnitBase*> men = buyOnce(f, wor, Unit_Troopers, Unit_Trooper, 200, 3);
    // One Move click on the map sends all three; all three get there.
    Game& game = *f.game;
    game.unselectAll(game.selectedList);
    game.selectedList.clear();
    for(UnitBase* u : men) { game.selectedList.insert(u->getObjectID()); u->setSelected(true); }
    game.selectionChanged();
    game.pInterface->updateObjectInterface();
    require(game.selectedList.size() == 3, "troopers: the three men cannot be selected together");
    const Coord target(30, 30);
    game.setCursorMode(Game::CursorMode_Move);
    require(game.handleSelectedObjectsMoveClick(target.x, target.y), "troopers: the Move click was refused");
    const bool arrived = f.runUntil(120, [&] {
        for(UnitBase* u : men) if(blockDistance(u->getLocation(), target) > 2) return false;
        return true;
    });
    require(arrived, "troopers: not all three Troopers reached the Move target");
    record("troopers", "one click: 200 credits, three Troopers; one Move click: all three arrived");
}

void infantry() {
    Fixture f("infantry");
    auto* barracks = f.producer(Structure_Barracks, 10, 10);
    buyOnce(f, barracks, Unit_Infantry, Unit_Soldier, 50, 3);
    record("infantry", "one click: 50 credits, three Soldiers");
}

void singles() {
    Fixture f("singles");
    auto* wor = f.producer(Structure_WOR, 10, 10);
    buyOnce(f, wor, Unit_Trooper, Unit_Trooper, 100, 1);
    auto* barracks = dynamic_cast<BuilderBase*>(f.game->getObjectManager().getObject(
        [&] { for(StructureBase* s : structureList) if(s->getOwner() == f.own && s->getItemID() == Structure_Barracks) return s->getObjectID(); return Uint32(0); }()));
    require(barracks != nullptr, "singles: no Barracks");
    buyOnce(f, barracks, Unit_Soldier, Unit_Soldier, 30, 1);
    record("singles", "Trooper: 100 credits, one man; Soldier: 30 credits, one man");
}

int taxBase(Fixture& f) { return f.game->getCitySimulation()->houseState_[f.own->getHouseID()].taxBaseEighths; }
int residents(Fixture& f) { return f.game->getCitySimulation()->houseState_[f.own->getHouseID()].resPop; }

/// Orders ITEM from the Construction Yard list, places it with a map click, and checks the tax base and
/// population it adds and takes back when it is destroyed.
void housingTax(const std::string& name, int item) {
    Fixture f(name);
    for(int i = 0; i < 4; ++i) f.place(Structure_WindTrap, 2 + 2 * i, 2);
    f.place(Structure_Radar, 2, 5);
    if(item == Structure_WOR) f.place(Structure_Barracks, 5, 5);
    auto* yard = dynamic_cast<ConstructionYard*>(f.place(Structure_ConstructionYard, 20, 20));
    require(yard != nullptr, name + ": no Construction Yard");
    yard->updateBuildList();
    Game& game = *f.game;
    f.run(30);   // a settled census before the building
    const int baseBefore = taxBase(f), popBefore = residents(f);
    BuilderList* list = openList(f, yard);
    clickItem(f, yard, list, item);
    const Coord site(22, 20);   // beside the yard: a plot must touch the base
    for(int dy = 0; dy < 3; ++dy) for(int dx = 0; dx < 3; ++dx) currentGameMap->getTile(site.x + dx, site.y + dy)->setType(Terrain_Rock);
    StructureBase* built = nullptr;
    auto findBuilt = [&] {
        for(StructureBase* s : structureList) if(s->getOwner() == f.own && s->getItemID() == item
            && s->getLocation() == site) { built = s; return true; }
        return false;
    };
    if(game.isPlanningPlacement()) {
        // The pre-placement rule: the click plans the plot and the yard builds and places it there.
        require(game.handlePlacementClick(site.x, site.y), name + ": the planning click on the plot was refused");
    } else {
        require(f.runUntil(120, [&] { return yard->isWaitingToPlace(); }), name + ": the yard did not finish the building");
        openList(f, yard);
        clickItem(f, yard, list, item);
        require(game.currentCursorMode == Game::CursorMode_Placing, name + ": a click on the finished item did not open placement");
        require(game.handlePlacementClick(site.x, site.y), name + ": the placement click was refused");
    }
    require(f.runUntil(180, findBuilt), name + ": the building never stood on the clicked plot");
    const int expectedBase = DuneCity::taxablePopulationEighths(Structure_ZoneResidential, 40, 3);
    const int expectedPop = DuneCity::getZonePopulation(Structure_ZoneResidential, 3);
    const bool counted = f.runUntil(120, [&] { return taxBase(f) - baseBefore >= expectedBase; });
    f.run(10);
    require(counted && taxBase(f) - baseBefore == expectedBase, name + ": tax base rose by "
            + std::to_string(taxBase(f) - baseBefore) + " eighths, not " + std::to_string(expectedBase));
    require(residents(f) - popBefore == expectedPop, name + ": residential population rose by "
            + std::to_string(residents(f) - popBefore) + ", not " + std::to_string(expectedPop));
    const int baseWith = taxBase(f), popWith = residents(f);
    built->destroy();
    // The tax base is recounted every city day (runDailyBudget), residents at the zone-growth census: wait for both.
    const bool gone = f.runUntil(180, [&] {
        return baseWith - taxBase(f) >= expectedBase && popWith - residents(f) >= expectedPop;
    });
    require(gone && baseWith - taxBase(f) == expectedBase && popWith - residents(f) == expectedPop,
            name + ": destroying it took back " + std::to_string(baseWith - taxBase(f)) + " eighths and "
            + std::to_string(popWith - residents(f)) + " people");
    record(name, "+" + std::to_string(expectedBase) + " tax eighths and +" + std::to_string(expectedPop)
           + " residents when built from the yard list, both back when destroyed");
}


/// Own Residential lots around SITE (distances 2-4 tiles), power, and settled city censuses.
std::vector<Coord> cityBlock(Fixture& f, Coord site) {
    for(int i = 0; i < 6; ++i) f.place(Structure_WindTrap, 2 + 2 * i, 2);
    std::vector<Coord> lots;
    for(const Coord offset : {Coord(-4, 0), Coord(4, 0), Coord(0, -4), Coord(0, 4), Coord(-3, -3), Coord(3, 3)}) {
        const Coord lot = site + offset;
        for(int dy = 0; dy < 2; ++dy) for(int dx = 0; dx < 2; ++dx) currentGameMap->getTile(lot.x + dx, lot.y + dy)->setType(Terrain_Rock);
        auto* z = f.own->placeStructure(NONE_ID, Structure_ZoneResidential, lot.x, lot.y, true, true);
        require(z != nullptr, f.label + ": cannot place a lot");
        static_cast<ZoneStructure*>(z)->setStartingLevel(2);
        lots.push_back(lot);
    }
    f.run(60);
    return lots;
}

double meanLandValue(Fixture& f, const std::vector<Coord>& lots) {
    double total = 0;
    for(const Coord lot : lots) total += f.game->getCitySimulation()->getLandValueMap().worldGet(lot.x, lot.y);
    return total / lots.size();
}

/// The mean land-value rise on the lots around SITE after ITEM is placed there.
double landValueRise(const std::string& name, int item, Coord site) {
    Fixture f(name);
    const auto lots = cityBlock(f, site);
    const double before = meanLandValue(f, lots);
    f.place(item, site.x, site.y);
    f.run(60);
    return meanLandValue(f, lots) - before;
}

void landValue() {
    // Stefan, 2026-11-28: "and they moderately increase land value". Moderate (the integrator's number): a third
    // of the Palace's stamp over 6 tiles instead of 8, so the rise is above 0 and 20-50% of a Palace's on the spot.
    const Coord site(30, 30);
    const double palace = landValueRise("land_value palace", Structure_Palace, site);
    require(palace > 0, "land_value: the Palace raised nothing, the comparison is void");
    for(const int item : {Structure_Barracks, Structure_WOR}) {
        const double rise = landValueRise("land_value " + std::to_string(item), item, site);
        require(rise > 0 && rise >= 0.2 * palace && rise <= 0.5 * palace, "land_value: item " + std::to_string(item)
                + " raised lots by " + std::to_string(rise) + ", the Palace by " + std::to_string(palace));
        record("land_value", "item " + std::to_string(item) + ": +" + std::to_string(rise) + " on lots 2-4 tiles away, "
               "Palace +" + std::to_string(palace) + " (" + std::to_string(int(100 * rise / palace)) + "%)");
    }
}

struct PoliceReading {
    std::vector<int> coverage, before, after;   // per lot, then the source cell last (coverage only)
};

/// Police coverage and crime before / after police on the lots around SITE with ITEM there, at FUNDING percent.
PoliceReading policeEffect(const std::string& name, int item, Coord site, int funding) {
    Fixture f(name);
    const auto lots = cityBlock(f, site);
    f.game->getCitySimulation()->houseState_[f.own->getHouseID()].policeFundingPercent = funding;
    f.place(item, site.x, site.y);
    f.run(60);
    const auto* city = f.game->getCitySimulation();
    PoliceReading reading;
    for(const Coord lot : lots) {
        reading.coverage.push_back(city->getPoliceCoverageMap().worldGet(lot.x, lot.y));
        reading.before.push_back(city->getCrimeBeforePoliceMap().worldGet(lot.x, lot.y));
        reading.after.push_back(city->getCrimeRateMap().worldGet(lot.x, lot.y));
    }
    reading.coverage.push_back(city->getPoliceCoverageMap().worldGet(site.x, site.y));
    return reading;
}

void police() {
    // Stefan, 2026-11-28: "and infantry and wor have 50% police effect": half a Police Station's police coverage,
    // taken off crime exactly as a station's is, and scaled by Police funding as a station's is. Crime after
    // police is clamp(before - coverage, 0, 250) (computeCrimeAfterPolice): near a station crime reaches 0, so the
    // size of the drop is capped by each lot's crime before police (higher around a Barracks, which houses 40
    // people) and is not a measure of the effect; the coverage, cell by cell, is.
    const Coord site(30, 30);
    const PoliceReading station = policeEffect("police station", Structure_PoliceStation, site, 100);
    const PoliceReading stationHalf = policeEffect("police station 50%", Structure_PoliceStation, site, 50);
    for(size_t i = 0; i < station.coverage.size(); ++i)
        require(station.coverage[i] > 0, "police: the Police Station covers nothing at cell " + std::to_string(i));
    for(const int item : {Structure_Barracks, Structure_WOR}) {
        const std::string what = "police: item " + std::to_string(item);
        const PoliceReading full = policeEffect("police " + std::to_string(item), item, site, 100);
        const PoliceReading half = policeEffect("police 50% " + std::to_string(item), item, site, 50);
        double low = 1, high = 0;
        int lowered = 0;
        for(size_t i = 0; i < station.coverage.size(); ++i) {
            // Coverage is integer: smoothPoliceCoverage runs three passes of (c + floor(n/4)) / 2, each losing less
            // than 1 unit, so each field is within 3 units of its exact value and a half-strength field within
            // 1.5 x 3 = 4.5 of half the full one. Cells: within 4 units of half the station's, and where the
            // station gives 200 or more (rounding under 2%), within 0.48-0.52 of it.
            const auto halfOf = [&](int barracks, int stationValue, const std::string& when) {
                const double ratio = double(barracks) / std::max(1, stationValue);
                low = std::min(low, ratio);
                high = std::max(high, ratio);
                require(std::abs(2 * barracks - stationValue) <= 8, what + when + ": coverage at cell " + std::to_string(i)
                        + " is " + std::to_string(barracks) + ", half the Police Station's " + std::to_string(stationValue)
                        + " is " + std::to_string(stationValue / 2.0) + " (more than 4 units of rounding)");
                require(stationValue < 200 || (ratio >= 0.48 && ratio <= 0.52), what + when + ": coverage at cell "
                        + std::to_string(i) + " is " + std::to_string(ratio) + " of a Police Station's");
            };
            halfOf(full.coverage[i], station.coverage[i], "");
            halfOf(half.coverage[i], stationHalf.coverage[i], " at 50% funding");
            require(half.coverage[i] < full.coverage[i], what + ": 50% funding did not lower its coverage");
            if(i + 1 == station.coverage.size()) break;   // the source cell has no lot
            const int expected = std::clamp(full.before[i] - full.coverage[i], 0, 250);
            require(full.after[i] == expected, what + ": crime at lot " + std::to_string(i) + " is "
                    + std::to_string(full.after[i]) + ", not crime before police " + std::to_string(full.before[i])
                    + " less its coverage " + std::to_string(full.coverage[i]));
            if(full.after[i] < full.before[i]) ++lowered;
        }
        require(lowered >= 1, what + " lowered crime on no lot");
        record("police", "item " + std::to_string(item) + ": coverage " + std::to_string(low) + "-" + std::to_string(high)
               + " of a Police Station's on every cell (also at 50% funding), taken off crime on " + std::to_string(lowered)
               + " of " + std::to_string(station.coverage.size() - 1) + " lots");
    }
}

}   // namespace InfantrySquadProbe

int runInfantrySquadProbe() {
    using namespace InfantrySquadProbe;
    require(ModManager::instance().setActiveMod("dunecity"), "Missing the dunecity mod");
    settings.general.playerName = "Squad tester";
    gModOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    gModOptions.aiInfantryTactics = false;
    // Every case runs even after one fails, so a run shows all failures; any failure fails the probe.
    int failed = 0;
    const std::vector<std::pair<const char*, void (*)()>> cases = {
        {"build_list", buildList}, {"troopers", troopers}, {"infantry", infantry}, {"singles", singles},
        {"barracks_tax", [] { housingTax("barracks_tax", Structure_Barracks); }},
        {"wor_tax", [] { housingTax("wor_tax", Structure_WOR); }},
        {"land_value", landValue}, {"police", police}};
    for(const auto& [name, check] : cases) {
        try {
            check();
        } catch(const std::exception&) {
            ++failed;   // require() logged INFANTRY_SQUAD_PROBE_FAIL with the reason.
            currentGame = nullptr;
        }
    }
    if(failed == 0) SDL_Log("INFANTRY_SQUAD_PROBE_PASS: %d cases", int(cases.size()));
    else SDL_Log("INFANTRY_SQUAD_PROBE_SUMMARY: %d of %d cases failed", failed, int(cases.size()));
    return -1;
}
