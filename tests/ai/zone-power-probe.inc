// Real city-growth and UI paths; diagnostic executable only.
#include <dunecity/CitySimulation.h>
#include <dunecity/CityStructurePopulation.h>
#include <GUI/ObjectInterfaces/ZoneStructureInterface.h>
#include <structures/ZoneStructure.h>
#include <map>
#include <tuple>

namespace ZonePowerProbe {
void require(bool ok, const char* why) {
    if (!ok) { SDL_Log("ZONE_POWER_FAIL: %s", why); throw std::runtime_error(why); }
}
int level(const ZoneStructure* zone) {
    const auto at = zone->getLocation();
    return currentGameMap->getTile(at.x,at.y)->getCityZoneDensity();
}
int population(const ZoneStructure* zone) {
    return DuneCity::getStructurePopulation(zone,level(zone));
}
void status(ZoneStructure* zone, bool powered) {
    ZoneStructureInterface panel(zone->getObjectID());
    require(panel.update(),"Zone properties failed to update");
    require(panel.poweredLabel.getText() == std::string(" ") + (powered ? _("Powered") : _("UNPOWERED")),
        "Zone properties disagree with the growth power predicate");
    if (population(zone) == 0)
        require(panel.densityLabel.getText() == " " + _("Density") + ": 0/3",
            "Empty lot does not display density zero");
}
}

void runZonePowerProbe(Game& game) {
    using namespace ZonePowerProbe;
    auto* house = game.getHouse(HOUSE_HARKONNEN);
    auto* sim = game.getCitySimulation();
    require(house && sim && game.isCitySimEnabled(),"Zone power fixture needs a city house");
    constexpr int span = 24;
    Coord origin = Coord::Invalid();
    for (int y=4;y+span<currentGameMap->getSizeY() && origin.isInvalid();++y)
        for (int x=4;x+span<currentGameMap->getSizeX() && origin.isInvalid();++x) {
            bool clear = true;
            for (int dy=0;dy<span;++dy) for (int dx=0;dx<span;++dx)
                clear &= !currentGameMap->getTile(x+dx,y+dy)->hasAGroundObject();
            if (clear) origin = Coord(x,y);
        }
    require(origin.isValid(),"Zone power fixture needs a clear district");
    for (int y=0;y<span;++y) for (int x=0;x<span;++x) {
        auto* tile = currentGameMap->getTile(origin.x+x,origin.y+y);
        tile->setType(Terrain_Rock);
        tile->setRoad(true);
    }
    std::vector<ZoneStructure*> zones;
    for (int start=1;start<=3;++start) for (int type=0;type<3;++type) {
        const Uint32 item = type==0 ? Structure_ZoneResidential
            : type==1 ? Structure_ZoneCommercial : Structure_ZoneIndustrial;
        auto* zone = dynamic_cast<ZoneStructure*>(house->placeStructure(NONE_ID,item,
            origin.x+3+type*6,origin.y+3+(start-1)*6,true,true));
        require(zone!=nullptr,"Could not place zone power fixture lot");
        zones.push_back(zone);
    }
    auto& rule = game.gameInitSettings.gameOptions.unpoweredZonesVacate;
    auto reset = [&] {
        for (size_t i=0;i<zones.size();++i) zones[i]->setStartingLevel(1+int(i)/3);
    };
    auto scan = [&] {
        auto& state = sim->houseState_[house->getHouseID()];
        state.resValve=2000; state.comValve=2000; state.indValve=2000; state.taxRate=0;
        for (auto* zone:zones) {
            const auto at=zone->getLocation();
            sim->landValueMap_.set(at.x/sim->landValueMap_.getBlockSize(),at.y/sim->landValueMap_.getBlockSize(),200);
            sim->pollutionDensityMap_.set(at.x/sim->pollutionDensityMap_.getBlockSize(),at.y/sim->pollutionDensityMap_.getBlockSize(),0);
            sim->crimeRateMap_.set(at.x/sim->crimeRateMap_.getBlockSize(),at.y/sim->crimeRateMap_.getBlockSize(),0);
        }
        ++sim->lastProcessedDay_;
        sim->runZoneGrowth();
    };
    // Powered populations must follow an identical trajectory with the rule off/on.
    const auto keepState=sim->houseState_[house->getHouseID()];
    const auto firstDay=sim->lastProcessedDay_;
    std::vector<int> poweredControl;
    for (bool enabled:{false,true}) {
        rule=enabled; reset(); house->producedPower=10000;
        sim->houseState_[house->getHouseID()]=keepState; sim->lastProcessedDay_=firstDay;
        for (int day=0;day<16;++day) {
            scan();
            for (size_t i=0;i<zones.size();++i) {
                const int pop=population(zones[i]);
                require(pop>0,"Powered lot became vacant");
                if (!enabled) poweredControl.push_back(pop);
                else require(pop==poweredControl[day*zones.size()+i],"Vacancy rule changed powered growth");
            }
        }
        status(zones.front(),true);
    }
    // Negative control: the old rule leaves every unpowered R/C/I lot at L1.
    rule=false; reset(); house->producedPower=0;
    for (int day=0;day<16;++day) scan();
    for (auto* zone:zones) require(level(zone)==1,"Legacy power decline changed");
    rule=true; reset();
    int emptiedDay=0;
    for (int day=1;day<=12;++day) {
        scan();
        bool empty=true;
        for (auto* zone:zones) empty &= population(zone)==0 && level(zone)==0;
        if (empty && !emptiedDay) emptiedDay=day;
    }
    require(emptiedDay==12,"R/C/I did not empty at the deterministic population-step rate");
    for (auto* zone:zones) {
        require(zone->getZonePowerDraw()==0,"Empty lot retained dynamic power demand");
        status(zone,false);
    }
    // No 0/0 bootstrap when the final resident releases the last power demand.
    const int keepRequirement=house->powerRequirement;
    house->powerRequirement=0;
    for (int day=0;day<48;++day) scan();
    for (auto* zone:zones) require(population(zone)==0,"Generator-free empty lot regrew");
    status(zones.front(),false);
    house->powerRequirement=keepRequirement;
    house->producedPower=10000;
    scan();
    for (auto* zone:zones) {
        require(population(zone)>0,"Power restoration did not permit regrowth");
        status(zone,true);
    }
    SDL_Log("ZONE_POWER_PASS: legacy_floor=1 vacancy_empty_day=%d powered_trajectory_identical=1 zero_grid_stays_empty=1 regrowth=1 ui=1",emptiedDay);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=zone_power outcome=passed");
}

void runZonePowerContinuation(Game& game, int minutes) {
    using namespace ZonePowerProbe;
    auto* sim=game.getCitySimulation();
    require(sim && game.isCitySimEnabled(),"Save continuation needs city simulation");
    auto& rule=game.gameInitSettings.gameOptions.unpoweredZonesVacate;
    const bool savedRule=rule;
    const std::string mode=std::getenv("BALANCE_ZONE_POWER_CONTINUATION");
    if (mode!="saved") rule=mode=="on";
    const Uint32 start=game.getGameCycleCount();
    SDL_Log("ZONE_POWER_SETUP: version=%d cycle=%u day=%u saved_rule=%d rule=%d options=%s revision=%s",
        game.getLoadedSavegameVersion(),start,sim->lastProcessedDay_,int(savedRule),int(rule),
        game.gameInitSettings.getGameOptions().getHash().c_str(),game.gameInitSettings.getModRevisionHash().c_str());
    if (const char* path=std::getenv("BALANCE_ZONE_POWER_SAVE")) {
        require(mode=="on","Opted-in save output needs the rule explicitly on");
        require(game.saveGame(path),"Could not write the opted-in save copy");
        SDL_Log("ZONE_POWER_SAVE: path=%s rule=1 cycle=%u",path,start);
    }
    std::map<Uint32,std::tuple<int,int,bool>> previous;
    auto sample=[&](bool full) {
        for (int h=0;h<NUM_HOUSES;++h) if (const auto* house=game.getHouse(h)) {
            int lots=0,empty=0,pop=0;
            std::array<int,4> levels{};
            for (const auto* building:structureList) {
                const auto* zone=dynamic_cast<const ZoneStructure*>(building);
                if (!zone || zone->getOwner()!=house) continue;
                const int density=level(zone),residents=population(zone);
                const bool powered=zone->hasCityPower();
                ++lots; ++levels[std::clamp(density,0,3)]; pop+=residents; empty+=residents==0;
                const auto key=std::make_tuple(density,residents,powered);
                const auto old=previous.find(zone->getObjectID());
                if (full || old==previous.end() || old->second!=key) {
                    const auto at=zone->getLocation();
                    SDL_Log("ZONE_POWER_LOT: cycle=%u elapsed=%u day=%u house=%d color=%d object=%u item=%u x=%d y=%d level=%d population=%d powered=%d draw=%d produced=%d needed=%d",
                        game.getGameCycleCount(),game.getGameCycleCount()-start,sim->lastProcessedDay_,h,getHouseVisualHouse(h),
                        zone->getObjectID(),zone->getItemID(),at.x,at.y,density,residents,int(powered),zone->getZonePowerDraw(),
                        house->getProducedPower(),house->getPowerRequirement());
                    previous[zone->getObjectID()]=key;
                }
            }
            if (full) SDL_Log("ZONE_POWER_HOUSE: cycle=%u elapsed=%u day=%u house=%d color=%d produced=%d needed=%d lots=%d empty=%d level0=%d level1=%d level2=%d level3=%d population=%d",
                game.getGameCycleCount(),game.getGameCycleCount()-start,sim->lastProcessedDay_,h,getHouseVisualHouse(h),
                house->getProducedPower(),house->getPowerRequirement(),lots,empty,levels[0],levels[1],levels[2],levels[3],pop);
        }
    };
    sample(true);
    Uint32 observedDay=sim->lastProcessedDay_;
    const Uint32 end=start+MILLI2CYCLES(minutes*60000);
    while (!game.finished && game.getGameCycleCount()<end) {
        game.updateGameState();
        if (sim->lastProcessedDay_!=observedDay && !sim->pendingGrowthPhase_) {
            observedDay=sim->lastProcessedDay_;
            sample(observedDay%48u==0);
        }
    }
    sample(true);
    SDL_Log("CAMPAIGN_BALANCE_RESULT: probe=zone_power_continuation rule=%d elapsed=%u outcome=%s",
        int(rule),game.getGameCycleCount()-start,game.finished ? "ended" : "time_limit");
}
