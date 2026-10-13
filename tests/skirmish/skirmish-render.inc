// Included only by skirmish-probe.inc, inside its anonymous namespace. Whole-map pictures
// (4 pixels per tile) and terrain measurements of map-editor random maps, generated skirmish
// maps, saved matches and live matches, so the generator's look can be compared with the
// editor's and with a played save.

struct PictureObject { int item = ItemID_Invalid; int house = HOUSE_INVALID; int x = 0, y = 0, w = 1, h = 1; bool unit = false; };

/// Neutral whole-map description: terrain plus tile flags plus objects.
struct MapPicture {
    int size = 0;
    std::vector<int> terrain;
    std::vector<char> road, slab, wall;
    std::vector<PictureObject> objects;
    void resize(int s) { size = s; terrain.assign(s * s, Terrain_Sand); road.assign(s * s, 0); slab.assign(s * s, 0); wall.assign(s * s, 0); }
};

MapPicture pictureOf(const MapData& map) {
    MapPicture picture;
    picture.resize(map.getSizeX());
    for(int y = 0; y < picture.size; ++y) for(int x = 0; x < picture.size; ++x) picture.terrain[y * picture.size + x] = map(x, y);
    return picture;
}

MapPicture pictureOf(const Result& result, const Catalogue& catalogue) {
    MapPicture picture = pictureOf(result.map);
    const int s = picture.size;
    for(const Placement& t : result.tiles) {
        if(t.item == Structure_Road) picture.road[t.y * s + t.x] = 1;
        else if(t.item == Structure_Wall) picture.wall[t.y * s + t.x] = 1;
        else picture.slab[t.y * s + t.x] = 1;
    }
    for(const Placement& p : result.structures) {
        const Coord d = catalogue.sizes[p.item];
        picture.objects.push_back({p.item, p.house, p.x, p.y, d.x, d.y, false});
    }
    for(const Placement& u : result.units) picture.objects.push_back({u.item, u.house, u.x, u.y, 1, 1, true});
    return picture;
}

MapPicture pictureOfGame() {
    MapPicture picture;
    picture.resize(currentGameMap->getSizeX());
    const int s = picture.size;
    for(int y = 0; y < s; ++y) for(int x = 0; x < s; ++x) {
        const Tile* tile = currentGameMap->getTile(x, y);
        picture.terrain[y * s + x] = tile->getType() == Terrain_Slab ? Terrain_Rock : tile->getType();
        picture.slab[y * s + x] = tile->getType() == Terrain_Slab;
        picture.road[y * s + x] = tile->isRoad();
    }
    for(const StructureBase* st : structureList) {
        const Coord at = st->getLocation(), d = st->getStructureSize();
        if(st->getItemID() == Structure_Wall) { picture.wall[at.y * s + at.x] = 1; continue; }
        picture.objects.push_back({static_cast<int>(st->getItemID()), st->getOwner()->getHouseID(), at.x, at.y, d.x, d.y, false});
    }
    for(const UnitBase* u : unitList) {
        const Coord at = u->getLocation();
        if(currentGameMap->tileExists(at)) picture.objects.push_back({static_cast<int>(u->getItemID()), u->getOwner()->getHouseID(), at.x, at.y, 1, 1, true});
    }
    return picture;
}

bool pictureRockLike(int t) { return t == Terrain_Rock || t == Terrain_Mountain || t == Terrain_Slab; }
bool pictureSpice(int t) {
    return t == Terrain_Spice || t == Terrain_ThickSpice || t == Terrain_SpiceBloom || t == Terrain_SpecialBloom
        || t == Terrain_GreenSpice || t == Terrain_ThickGreenSpice || t == Terrain_GreenSpiceBloom
        || t == Terrain_RedSpice || t == Terrain_ThickRedSpice || t == Terrain_RedSpiceBloom;
}

/// Shares and outline shape of a map, plus per-house power and zoning facts.
std::string measure(const MapPicture& p) {
    const int s = p.size, area = s * s;
    int rock = 0, mountain = 0, spice = 0, thick = 0, dunes = 0;
    for(int t : p.terrain) {
        rock += pictureRockLike(t); mountain += t == Terrain_Mountain; spice += pictureSpice(t);
        thick += t == Terrain_ThickSpice; dunes += t == Terrain_Dunes;
    }
    auto rockAt = [&](int x, int y) { return x >= 0 && y >= 0 && x < s && y < s && pictureRockLike(p.terrain[y * s + x]); };
    // Outline: edges between rock and non-rock, corners (2x2 windows with an odd rock count),
    // and the longest straight run of outline along one row or column boundary.
    int edges = 0, corners = 0, longest = 0;
    for(int y = 0; y + 1 < s; ++y) {
        int run = 0, side = 0;
        for(int x = 0; x < s; ++x) {
            const int here = rockAt(x, y) != rockAt(x, y + 1) ? (rockAt(x, y) ? 1 : 2) : 0;
            edges += here != 0;
            run = here != 0 && here == side ? run + 1 : here != 0;
            side = here;
            longest = std::max(longest, run);
        }
    }
    for(int x = 0; x + 1 < s; ++x) {
        int run = 0, side = 0;
        for(int y = 0; y < s; ++y) {
            const int here = rockAt(x, y) != rockAt(x + 1, y) ? (rockAt(x, y) ? 1 : 2) : 0;
            edges += here != 0;
            run = here != 0 && here == side ? run + 1 : here != 0;
            side = here;
            longest = std::max(longest, run);
        }
    }
    for(int y = 0; y + 1 < s; ++y) for(int x = 0; x + 1 < s; ++x) {
        const int n = rockAt(x, y) + rockAt(x + 1, y) + rockAt(x, y + 1) + rockAt(x + 1, y + 1);
        corners += n == 1 || n == 3;
    }
    // Separate rock bodies (8-connected), counting islands of at most 40 tiles.
    std::vector<char> seen(area, 0);
    int bodies = 0, islands = 0;
    for(int i = 0; i < area; ++i) {
        if(seen[i] || !pictureRockLike(p.terrain[i])) continue;
        ++bodies;
        int tiles = 0;
        std::deque<int> queue{i};
        seen[i] = 1;
        while(!queue.empty()) {
            const int at = queue.front(); queue.pop_front(); ++tiles;
            for(int dy = -1; dy <= 1; ++dy) for(int dx = -1; dx <= 1; ++dx) {
                const int nx = at % s + dx, ny = at / s + dy;
                if(nx < 0 || ny < 0 || nx >= s || ny >= s || seen[ny * s + nx] || !pictureRockLike(p.terrain[ny * s + nx])) continue;
                seen[ny * s + nx] = 1;
                queue.push_back(ny * s + nx);
            }
        }
        islands += tiles <= 40;
    }
    std::ostringstream out;
    out << "rock=" << rock * 100 / area << "% mountain=" << mountain * 100 / area << "% spice=" << spice * 100 / area
        << "% thick=" << thick * 100 / area << "% dunes=" << dunes * 100 / area << "% rockBodies=" << bodies
        << " smallIslands=" << islands << " outlineCornersPer100Edges=" << (edges ? corners * 100 / edges : 0)
        << " longestStraightOutline=" << longest;
    std::map<int, std::array<int, 7>> houses;    // windtraps, nukes, R, C, I, police, closest I-R gap
    for(const PictureObject& o : p.objects) {
        if(o.unit) continue;
        auto& h = houses.try_emplace(o.house, std::array<int, 7>{0, 0, 0, 0, 0, 0, 999}).first->second;
        h[0] += o.item == Structure_WindTrap; h[1] += o.item == Structure_NuclearPlant;
        h[2] += o.item == Structure_ZoneResidential; h[3] += o.item == Structure_ZoneCommercial;
        h[4] += o.item == Structure_ZoneIndustrial; h[5] += o.item == Structure_PoliceStation;
        if(o.item != Structure_ZoneIndustrial) continue;
        for(const PictureObject& r : p.objects)
            if(!r.unit && r.house == o.house && r.item == Structure_ZoneResidential)
                h[6] = std::min(h[6], std::max({0, r.x - (o.x + o.w), o.x - (r.x + r.w), r.y - (o.y + o.h), o.y - (r.y + r.h)}));
    }
    for(const auto& [house, h] : houses)
        out << " | house" << house << " windtraps=" << h[0] << " nuclear=" << h[1] << " R/C/I=" << h[2] << "/" << h[3] << "/" << h[4]
            << " police=" << h[5] << (h[4] && h[2] ? " closestIndustrialToResidentialGap=" + std::to_string(h[6]) : std::string());
    return out.str();
}

void renderPicture(const MapPicture& p, const std::string& name) {
    constexpr int k = 4;
    // SavePNG writes the bytes in memory order: R, G, B, A.
    auto surface = sdl2::surface_ptr(SDL_CreateRGBSurfaceWithFormat(0, p.size * k, p.size * k, 32, SDL_PIXELFORMAT_RGBA32));
    skirmishRequire(surface != nullptr, "Could not create the render surface");
    auto fill = [&](int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b) {
        SDL_Rect rect{x, y, w, h};
        SDL_FillRect(surface.get(), &rect, SDL_MapRGB(surface->format, r, g, b));
    };
    for(int y = 0; y < p.size; ++y) for(int x = 0; x < p.size; ++x) {
        const int i = y * p.size + x;
        Uint8 r = 236, g = 196, b = 130;
        switch(p.terrain[i]) {
            case Terrain_Dunes:         r = 205; g = 158; b = 92; break;
            case Terrain_Rock:          r = 128; g = 116; b = 102; break;
            case Terrain_Slab:          r = 170; g = 170; b = 170; break;
            case Terrain_Mountain:      r = 66;  g = 52;  b = 42; break;
            case Terrain_Spice:         r = 230; g = 132; b = 50; break;
            case Terrain_ThickSpice:    r = 172; g = 72;  b = 22; break;
            case Terrain_SpiceBloom: case Terrain_SpecialBloom: r = 210; g = 30; b = 30; break;
            case Terrain_GreenSpice: case Terrain_ThickGreenSpice: case Terrain_GreenSpiceBloom: r = 90; g = 170; b = 70; break;
            case Terrain_RedSpice: case Terrain_ThickRedSpice: case Terrain_RedSpiceBloom: r = 190; g = 40; b = 70; break;
            default: break;
        }
        if(p.slab[i]) { r = 172; g = 172; b = 172; }
        if(p.road[i]) { r = 36; g = 36; b = 36; }
        if(p.wall[i]) { r = 92; g = 92; b = 100; }
        fill(x * k, y * k, k, k, r, g, b);
    }
    static const Uint8 houseColor[NUM_HOUSES][3] = {
        {200, 30, 30}, {40, 80, 230}, {30, 150, 50}, {150, 80, 200}, {150, 30, 110}, {225, 190, 20}, {245, 245, 245}, {60, 60, 60}};
    for(const PictureObject& o : p.objects) {
        const Uint8* c = o.house >= 0 && o.house < NUM_HOUSES ? houseColor[o.house] : houseColor[6];
        if(o.unit) { fill(o.x * k + 1, o.y * k + 1, k - 2, k - 2, c[0], c[1], c[2]); continue; }
        Uint8 r = c[0], g = c[1], b = c[2];
        switch(o.item) {
            case Structure_ZoneResidential: r = 70;  g = 200; b = 70;  break;
            case Structure_ZoneCommercial:  r = 70;  g = 150; b = 255; break;
            case Structure_ZoneIndustrial:  r = 250; g = 225; b = 40;  break;
            case Structure_WindTrap:        r = 0;   g = 225; b = 225; break;
            case Structure_NuclearPlant:    r = 255; g = 0;   b = 255; break;
            case Structure_PoliceStation:   r = 255; g = 255; b = 255; break;
            default: break;
        }
        fill(o.x * k, o.y * k, o.w * k, o.h * k, c[0], c[1], c[2]);         // owner frame
        fill(o.x * k + 1, o.y * k + 1, o.w * k - 2, o.h * k - 2, r, g, b);
        if(o.item == Structure_ConstructionYard) fill(o.x * k + 3, o.y * k + 3, o.w * k - 6, o.h * k - 6, 255, 255, 255);
    }
    const std::string path = probeOut(name + ".png");
    skirmishRequire(SavePNG(surface.get(), path.c_str()) == 0, "Could not write " + path);
    SDL_Log("SKIRMISH_RENDER: %s %s", path.c_str(), measure(p).c_str());
}

Params vanilla2v2Params() {
    Params p;
    p.mapSize = 128; p.seed = 488631365; p.house = HOUSE_ATREIDES; p.allies = 1; p.enemies = 2; p.techLevel = 8;
    p.mod = "vanilla"; p.mode = Mode::Vanilla;
    p.allySide = Side{"qBotMedium", BaseSize::Medium, 3, 0};
    p.enemySide = Side{"qBotMedium", BaseSize::Medium, 3, 0};
    p.playerSide = Side{"", BaseSize::Medium, 3, 0};
    return p;
}

/// The map a saved match was started from (its GameInitSettings carry the full map INI).
/// Loading the save itself needs the exact mod revision it was played with, which an isolated
/// profile cannot fetch offline; the generated map is what this picture is about.
MapPicture pictureOfSavedMap(const std::string& path) {
    const std::string bytes = readCompleteFile(path);
    IMemoryStream stream(bytes.data(), bytes.size());
    GameInitSettings::HouseInfoList houses;
    const GameInitSettings saved = GameInitSettings::readSaveSetup(stream, houses);
    const std::string& text = saved.getFiledata();
    SDL_RWops* rw = SDL_RWFromConstMem(text.data(), static_cast<int>(text.size()));
    INIFile ini(rw);
    SDL_RWclose(rw);
    MapPicture picture;
    picture.resize(ini.getIntValue("MAP", "SizeX"));
    const int s = picture.size;
    for(int y = 0; y < s; ++y) {
        char key[8];
        std::snprintf(key, sizeof(key), "%.3d", y);
        const std::string row = ini.getStringValue("MAP", key);
        for(int x = 0; x < s && x < static_cast<int>(row.size()); ++x) {
            int t = Terrain_Sand;
            switch(row[x]) {
                case '^': t = Terrain_Dunes; break;
                case '~': t = Terrain_Spice; break;
                case '+': t = Terrain_ThickSpice; break;
                case '%': t = Terrain_Rock; break;
                case '@': t = Terrain_Mountain; break;
                case 'O': t = Terrain_SpiceBloom; break;
                case 'Q': t = Terrain_SpecialBloom; break;
                default: break;
            }
            picture.terrain[y * s + x] = t;
        }
    }
    for(const char* section : {"STRUCTURES", "UNITS"}) {
        if(!ini.hasSection(section)) continue;
        for(const INIFile::Key& key : ini.getSection(section)) {
            std::vector<std::string> fields;
            std::stringstream list(key.getStringValue());
            for(std::string field; std::getline(list, field, ',');) fields.push_back(field);
            if(fields.size() < 2) continue;
            const int house = getHouseByName(fields[0]);
            const int item = static_cast<int>(getItemIDByName(fields[1]));
            const std::string name = key.getKeyName();
            const int position = name.rfind("GEN", 0) == 0 ? std::atoi(name.c_str() + 3) : fields.size() >= 4 ? std::atoi(fields[3].c_str()) : -1;
            if(position < 0 || position >= s * s || item == ItemID_Invalid) continue;
            const int x = position % s, y = position / s;
            if(item == Structure_Road) picture.road[position] = 1;
            else if(item == Structure_Wall) picture.wall[position] = 1;
            else if(item == Structure_Slab1) picture.slab[position] = 1;
            else {
                const Coord d = isStructure(item) ? getStructureSize(item) : Coord(1, 1);
                picture.objects.push_back({item, house, x, y, d.x, d.y, !isStructure(item)});
            }
        }
    }
    return picture;
}

void runRender() {
    const std::string tag = std::getenv("SKIRMISH_RENDER_TAG") ? std::getenv("SKIRMISH_RENDER_TAG") : "current";
    for(int seed : {1, 2}) renderPicture(pictureOf(generateRandomMap(128, 128, seed)), "editor-random-128-seed" + std::to_string(seed));
    for(const auto& [name, params] : {std::pair<std::string, Params>{"stefan-dunecity", caseParams("stefan-128")}, {"vanilla-2v2", vanilla2v2Params()}}) {
        skirmishRequire(RandomSkirmish::activateMod(params.mod), "Render mod did not activate");
        const Catalogue catalogue = RandomSkirmish::loadCatalogue(params.mode);
        const Result result = SkirmishGenerator::generate(params, catalogue);
        renderPicture(pictureOf(result, catalogue), tag + "-generated-" + name);
    }
    if(const char* save = std::getenv("SKIRMISH_RENDER_SAVE"))
        renderPicture(pictureOfSavedMap(save), "saved-" + getBasename(save, true));
}
