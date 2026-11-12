// Included inside the skirmish fixture namespace; runs last, before discarding the game.
void runMountainSceneryProbe(Game& game) {
    auto* sim = game.getCitySimulation();
    skirmishRequire(sim != nullptr, "Mountain scenery requires a city simulation");
    const int width = currentGameMap->getSizeX(), height = currentGameMap->getSizeY();
    Coord plot = Coord::Invalid();
    // Keep the generated ridge itself intact. Find a vacant 2x2 lot touching it,
    // away from existing structures, in the central third of the real Civ Wars map.
    for(int y=9; y<height-10 && plot.isInvalid(); ++y) {
        for(int x=width/3; x<2*width/3 && plot.isInvalid(); ++x) {
            bool clear = true, ridge = false;
            for(int dy=-8; dy<=9; ++dy) for(int dx=-8; dx<=9; ++dx)
                if(currentGameMap->getTile(x+dx,y+dy)->hasAStructure()) clear = false;
            for(int dy=0; dy<2; ++dy) for(int dx=0; dx<2; ++dx) {
                const auto* tile = currentGameMap->getTile(x+dx,y+dy);
                if(tile->isMountain() || tile->hasAGroundObject()) clear = false;
            }
            for(int d=0; d<2; ++d)
                ridge |= currentGameMap->getTile(x-1,y+d)->isMountain()
                    || currentGameMap->getTile(x+2,y+d)->isMountain()
                    || currentGameMap->getTile(x+d,y-1)->isMountain()
                    || currentGameMap->getTile(x+d,y+2)->isMountain();
            if(clear && ridge) plot = Coord(x,y);
        }
    }
    skirmishRequire(plot.isValid(), "No vacant lot alongside a generated Civ Wars ridge");
    // Isolate forest scenery from DuneCity's independently tuned sand-as-water
    // bonus, without changing a single generated mountain/ridge tile.
    for(int y=plot.y-8; y<=plot.y+9; ++y) for(int x=plot.x-8; x<=plot.x+9; ++x) {
        auto* tile = currentGameMap->getTile(x,y);
        if(!tile->isMountain()) tile->setType(Terrain_Rock);
    }
    auto* owner = game.getHouse(HOUSE_ATREIDES);
    auto* zone = owner->placeStructure(NONE_ID,Structure_ZoneResidential,plot.x,plot.y,true,true);
    skirmishRequire(zone != nullptr, "Cannot place ridge-adjacent scenery zone");
    auto& rule = game.gameInitSettings.gameOptions.mountainScenery;
    const bool keepRule = rule;
    const auto crime = sim->crimeRateMap_;
    auto scan = [&](bool enabled) {
        rule = enabled;
        sim->crimeRateMap_ = crime; // compare the same previous-scan feedback
        sim->runEffectsScans();
        return sim->getLandValueMap().worldGet(plot.x,plot.y);
    };
    const int off = scan(false);
    const int on = scan(true);
    const int terrain = sim->getParkTerrain().landValueContribution(plot.x,plot.y);
    skirmishRequire(off > 0 && on > off && terrain > 0,
                    "Generated ridge did not raise its adjacent zone's land value");
    skirmishRequire(scan(false) == off, "Switching scenery off did not restore the old value");
    SDL_Log("MOUNTAIN_SCENERY_PASS: map=%dx%d zone=(%d,%d) ridge-preserved=1 sand-isolated=1 off=%d on=%d terrain=%d",
            width,height,plot.x,plot.y,off,on,terrain);
    rule = keepRule;
    sim->crimeRateMap_ = crime;
    sim->runEffectsScans();
}
