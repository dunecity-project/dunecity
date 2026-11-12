// Included only by skirmish-probe.inc, inside its anonymous namespace. Whole-map pictures
// (4 pixels per tile) and terrain measurements of map-editor random maps, generated skirmish
// maps, saved matches and live matches, so the generator's look can be compared with the
// editor's and with a played save.

struct PictureObject { int item = ItemID_Invalid; int house = HOUSE_INVALID; int x = 0, y = 0, w = 1, h = 1; bool unit = false; };
struct PictureStart { int house = HOUSE_INVALID; Coord at; Rect rockBerth; };

/// Neutral whole-map description: terrain plus tile flags plus objects.
struct MapPicture {
    int size = 0;
    std::vector<int> terrain;
    std::vector<char> road, slab, wall;
    std::vector<PictureObject> objects;
    std::vector<PictureStart> starts;
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
    for(const HouseSetup& h : result.houses) picture.starts.push_back({h.house, h.start, h.rockBerth});
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
    int rock = 0, mountain = 0, spice = 0, thick = 0, dunes = 0, sand = 0;
    for(int t : p.terrain) {
        rock += pictureRockLike(t); mountain += t == Terrain_Mountain; spice += pictureSpice(t);
        thick += t == Terrain_ThickSpice; dunes += t == Terrain_Dunes;
        sand += t == Terrain_Sand;
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
        << " longestStraightOutline=" << longest
        << " sandTiles=" << sand << " duneTiles=" << dunes << " rockTiles=" << rock - mountain
        << " mountainTiles=" << mountain << " spiceTiles=" << spice;
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
    // The coloured rectangle is the bounded rock berth, not the whole connected continent.
    // Draw starts last so MCVs, roads and buildings cannot hide the measurement coordinates.
    for(const PictureStart& start : p.starts) {
        const Uint8* c = start.house >= 0 && start.house < NUM_HOUSES ? houseColor[start.house] : houseColor[6];
        const Rect& berth = start.rockBerth;
        const int x = berth.x * k, y = berth.y * k, w = berth.w * k, h = berth.h * k;
        fill(x - 1, y - 1, w + 2, 3, 0, 0, 0);
        fill(x - 1, y + h - 2, w + 2, 3, 0, 0, 0);
        fill(x - 1, y - 1, 3, h + 2, 0, 0, 0);
        fill(x + w - 2, y - 1, 3, h + 2, 0, 0, 0);
        fill(x, y, w, 1, c[0], c[1], c[2]);
        fill(x, y + h - 1, w, 1, c[0], c[1], c[2]);
        fill(x, y, 1, h, c[0], c[1], c[2]);
        fill(x + w - 1, y, 1, h, c[0], c[1], c[2]);
        const int sx = start.at.x * k + k / 2, sy = start.at.y * k + k / 2;
        fill(sx - 6, sy - 2, 13, 5, 0, 0, 0);
        fill(sx - 2, sy - 6, 5, 13, 0, 0, 0);
        fill(sx - 5, sy, 11, 1, 255, 255, 255);
        fill(sx, sy - 5, 1, 11, 255, 255, 255);
        const std::string text = "H" + std::to_string(start.house) + " (" + std::to_string(start.at.x) + "," + std::to_string(start.at.y) + ")";
        auto label = pFontManager->createSurfaceWithText(text, 0xFFFFFFFFu, 12);
        skirmishRequire(label != nullptr, "Could not draw a start label");
        SDL_Rect dest{std::clamp(sx + 9, 0, std::max(0, surface->w - label->w - 4)),
                      std::clamp(sy - label->h - 5, 0, std::max(0, surface->h - label->h - 4)), label->w, label->h};
        fill(dest.x - 2, dest.y - 2, dest.w + 4, dest.h + 4, 0, 0, 0);
        fill(dest.x - 2, dest.y - 2, 2, dest.h + 4, c[0], c[1], c[2]);
        skirmishRequire(SDL_BlitSurface(label.get(), nullptr, surface.get(), &dest) == 0, "Could not blit a start label");
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

void startRockCsvString(std::ostream& out, const std::string& value) {
    out << '"';
    for(char c : value) { if(c == '"') out << '"'; out << c; }
    out << '"';
}

/// Measurement only: a failing local floor is a CSV fact, never a baseline assertion.
class StartRockMeasurements {
public:
    explicit StartRockMeasurements(const std::string& tag)
        : tag(tag), maps(probeOut(tag + "-start-rock-maps.csv"), std::ios::binary),
          starts(probeOut(tag + "-start-rock-starts.csv"), std::ios::binary) {
        skirmishRequire(maps.good() && starts.good(), "Could not open the start-rock CSVs");
        const char* settings = "tag,case,seed,map_seed,mode,mod,size,players,tech_level,requested_house,"
            "allies,enemies,enemies_allied,mirrored,mirror_mode,sand_target_percent,rock_target_percent,"
            "mountain_target_percent,spice_index,worms,credits,research_lab,"
            "player_base,player_army,player_city,player_ai,ally_base,ally_army,ally_city,ally_ai,"
            "enemy_base,enemy_army,enemy_city,enemy_ai,terrain_options_hash,attempts,flattened_mountains";
        maps << settings << ",sand_tiles,dune_tiles,ordinary_rock_tiles,mountain_tiles,spice_tiles,other_tiles,"
            "sand_family_percent,ordinary_rock_percent,mountain_percent,mountain_error_pp,spice_percent,"
            "ordinary_rock_components,largest_ordinary_rock_component,berth_width_min,berth_width_max,"
            "berth_height_min,berth_height_max,minimum_rock_min,minimum_rock_max,"
            "local_min,local_median,local_max,local_spread,fairness_min_over_max_percent,imbalance_percent,starts_below_minimum\n";
        starts << settings << ",start_index,house,house_name,team,start_x,start_y,berth_x,berth_y,berth_width,"
            "berth_height,berth_area,minimum_rock,contiguous_ordinary_rock,global_ordinary_rock_component,"
            "local_coverage_percent,below_minimum\n";
    }

    void record(const std::string& name, const Params& params, const Result& result, const Catalogue& catalogue) {
        const int size = result.map.getSizeX(), area = size * size;
        int sand = 0, dunes = 0, rock = 0, mountain = 0, spice = 0;
        for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x) {
            const int t = result.map(x, y);
            sand += t == Terrain_Sand; dunes += t == Terrain_Dunes; rock += t == Terrain_Rock;
            mountain += t == Terrain_Mountain; spice += pictureSpice(t);
        }
        // Label global 4-neighbour ordinary-rock bodies once, rather than flood-filling the
        // same continent separately for every start. Mountains never count as buildable.
        std::vector<int> componentArea(area, 0), queue;
        queue.reserve(rock);
        int components = 0, largest = 0;
        const std::array<Coord, 4> directions{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
        for(int i = 0; i < area; ++i) {
            if(componentArea[i] || result.map(i % size, i / size) != Terrain_Rock) continue;
            ++components;
            queue.clear();
            queue.push_back(i);
            componentArea[i] = -1;
            for(size_t cursor = 0; cursor < queue.size(); ++cursor) {
                const int at = queue[cursor];
                for(const Coord& d : directions) {
                    const int x = at % size + d.x, y = at / size + d.y;
                    if(x < 0 || y < 0 || x >= size || y >= size) continue;
                    const int next = y * size + x;
                    if(componentArea[next] || result.map(x, y) != Terrain_Rock) continue;
                    componentArea[next] = -1;
                    queue.push_back(next);
                }
            }
            const int count = static_cast<int>(queue.size());
            largest = std::max(largest, count);
            for(int at : queue) componentArea[at] = count;
        }

        std::array<int, NUM_HOUSES> local{}, ordered{};
        const size_t count = result.houses.size();
        skirmishRequire(count > 0 && count <= local.size(), "Start-rock measurement has an invalid house count");
        int below = 0, minWidth = size, maxWidth = 0, minHeight = size, maxHeight = 0;
        int minRequired = area, maxRequired = 0;
        for(size_t i = 0; i < count; ++i) {
            const HouseSetup& h = result.houses[i];
            local[i] = ordered[i] = contiguousBuildableRock(result.map, h.start, h.rockBerth);
            below += local[i] < h.minimumRock;
            minWidth = std::min(minWidth, h.rockBerth.w); maxWidth = std::max(maxWidth, h.rockBerth.w);
            minHeight = std::min(minHeight, h.rockBerth.h); maxHeight = std::max(maxHeight, h.rockBerth.h);
            minRequired = std::min(minRequired, h.minimumRock); maxRequired = std::max(maxRequired, h.minimumRock);
        }
        std::sort(ordered.begin(), ordered.begin() + count);
        const int minimum = ordered[0], maximum = ordered[count - 1];
        const double median = (ordered[(count - 1) / 2] + ordered[count / 2]) / 2.0;
        const double fairness = maximum ? 100.0 * minimum / maximum : 0.0;
        const double imbalance = maximum ? 100.0 * (maximum - minimum) / maximum : 0.0;
        std::ostringstream settings;
        startRockCsvString(settings, tag); settings << ','; startRockCsvString(settings, name);
        settings << ',' << params.seed << ',' << result.mapSeed << ','
                 << (params.mode == Mode::DuneCity ? "DuneCity" : "Vanilla") << ',';
        startRockCsvString(settings, params.mod);
        settings << ',' << size << ',' << count << ',' << params.techLevel << ',' << params.house << ','
                 << params.allies << ',' << params.enemies << ',' << params.enemiesAllied << ',' << params.symmetric << ','
                 << (params.symmetric ? (count == 2 ? "point" : "both") : "none") << ','
                 << params.sandPercent << ',' << params.rockPercent << ',' << params.mountainPercent << ','
                 << static_cast<int>(params.spice) << ',' << params.worms << ',' << params.credits << ',' << params.researchLab;
        for(const Side* side : {&params.playerSide, &params.allySide, &params.enemySide}) {
            settings << ',' << static_cast<int>(side->base) << ',' << side->army << ',' << side->city << ',';
            startRockCsvString(settings, side->playerClass);
        }
        settings << ','; startRockCsvString(settings, params.terrainHash());
        settings << ',' << result.attempts << ',' << result.flattenedMountains;
        const std::string prefix = settings.str();
        maps << prefix << ',' << sand << ',' << dunes << ',' << rock << ',' << mountain << ',' << spice << ','
             << area - sand - dunes - rock - mountain - spice << ','
             << 100.0 * (sand + dunes + spice) / area << ',' << 100.0 * rock / area << ','
             << 100.0 * mountain / area << ',' << 100.0 * mountain / area - params.mountainPercent << ','
             << 100.0 * spice / area << ',' << components << ',' << largest << ','
             << minWidth << ',' << maxWidth << ',' << minHeight << ',' << maxHeight << ','
             << minRequired << ',' << maxRequired << ',' << minimum << ',' << median << ',' << maximum << ','
             << maximum - minimum << ',' << fairness << ',' << imbalance << ',' << below << '\n';
        for(size_t i = 0; i < count; ++i) {
            const HouseSetup& h = result.houses[i];
            const Rect& berth = h.rockBerth;
            const int berthArea = berth.w * berth.h;
            const int global = result.map.isInsideMap(h.start.x, h.start.y) ? componentArea[h.start.y * size + h.start.x] : 0;
            starts << prefix << ',' << i << ',' << h.house << ',';
            startRockCsvString(starts, catalogue.houseNames[h.house]);
            starts << ',' << h.team << ',' << h.start.x << ',' << h.start.y << ',' << berth.x << ',' << berth.y << ','
                   << berth.w << ',' << berth.h << ',' << berthArea << ',' << h.minimumRock << ',' << local[i] << ','
                   << global << ',' << (berthArea ? 100.0 * local[i] / berthArea : 0.0) << ','
                   << (local[i] < h.minimumRock) << '\n';
        }
        SDL_Log("SKIRMISH_RENDER_START_ROCK: %s size=%d players=%zu seed=%u mirror=%d min=%d median=%.1f max=%d fairness=%.3f%% below=%d",
                name.c_str(), size, count, params.seed, params.symmetric, minimum, median, maximum, fairness, below);
    }

    void finish() {
        maps.flush(); starts.flush();
        skirmishRequire(maps.good() && starts.good(), "Could not write the start-rock CSVs");
    }

private:
    std::string tag;
    std::ofstream maps, starts;
};

Params reportedStartRockParams(uint32_t seed) {
    // Extracted from both supplied saves' embedded INIs and serialized mod headers.
    // Atreides: legacy Rock=65 is combined land, migrated to 35/60/5; 3000 credits,
    // dunecity/a4212b3437b811b7, Random human resolves to Mercenary with qBotBrutal.
    // Sardaukar: explicit 40/40/20, 1000 credits, eight worms; its actual mod is
    // ws-37d5ce34.../9addf5f78f6e678a (Civilization Wars, Base Mod=dunecity), not
    // BASIC's generic Mod=dunecity. Use bundled CivWars, as for the 80rock case.
    // Keep house Random: forcing the saved human identity would remove an RNG draw.
    const bool sardaukar = seed == 836308809u;
    Params p;
    p.mapSize = 256; p.seed = seed; p.house = HOUSE_INVALID;
    p.allies = 0; p.enemies = 7; p.enemiesAllied = false;
    p.mode = Mode::DuneCity; p.mod = sardaukar ? "CivWars" : "dunecity"; p.techLevel = 9;
    p.sandPercent = sardaukar ? 40 : 35; p.rockPercent = sardaukar ? 40 : 60; p.mountainPercent = sardaukar ? 20 : 5;
    p.spice = Spice::None; p.worms = sardaukar ? 8 : 0; p.symmetric = false;
    p.credits = sardaukar ? 1000 : 3000;
    p.playerSide = p.allySide = p.enemySide = Side{"qBotBrutal", BaseSize::None, 0, 0};
    return p;
}

void renderStartRockMeasurements(const std::string& tag, StartRockMeasurements& measurements) {
    // These self-contained fixtures need neither the original saves nor a workshop download
    // at runtime. --render-tag baseline/current changes filenames, never generation settings.
    for(uint32_t seed : {418379001u, 836308809u}) {
        const Params p = reportedStartRockParams(seed);
        skirmishRequire(RandomSkirmish::activateMod(p.mod), "Reported start-rock mod did not activate");
        const Catalogue catalogue = RandomSkirmish::loadCatalogue(p.mode);
        const Result result = SkirmishGenerator::generate(p, catalogue);
        skirmishRequire(result.houses.size() == 8, "Reported start-rock fixture did not produce eight players");
        skirmishRequire(result.houses.front().house == (seed == 418379001u ? HOUSE_MERCENARY : HOUSE_HARKONNEN),
                        "Reported start-rock fixture changed its Random human house");
        const std::string name = "reported-seed" + std::to_string(seed);
        measurements.record(name, p, result, catalogue);
        const std::string stem = tag + "-generated-" + name;
        std::ofstream ini(probeOut(stem + ".ini"), std::ios::binary);
        ini << result.ini;
        ini.close();
        skirmishRequire(ini.good(), "Could not write the reported start-rock map INI");
        renderPicture(pictureOf(result, catalogue), stem);
    }

    // 576 pure-generation samples: 6 sizes x 3 player counts x 2 modes x 2 mirror
    // states x 8 samples. Each cell samples every mix twice and the matrix uses all
    // 16 fixed seeds. Only the two reported fixtures get PNGs/INIs, keeping I/O bounded
    // within the existing native render timeout; no live matches are started here.
    constexpr std::array<uint32_t, 16> seeds{{418379001u, 836308809u, 20261104u, 20261013u,
        488631365u, 1u, 2u, 3u, 7u, 11u, 42u, 101u, 65537u, 0x12345678u, 0x87654321u, 0xffffffffu}};
    constexpr std::array<std::array<int, 3>, 4> mixes{{{{90, 5, 5}}, {{75, 20, 5}}, {{20, 75, 5}}, {{50, 20, 30}}}};
    constexpr std::array<int, 3> players{{2, 4, 8}};
    for(int mode = 0; mode < 2; ++mode) {
        const std::string mod = mode ? "dunecity" : "vanilla";
        const Mode cityMode = mode ? Mode::DuneCity : Mode::Vanilla;
        skirmishRequire(RandomSkirmish::activateMod(mod), "Start-rock matrix mod did not activate");
        const Catalogue catalogue = RandomSkirmish::loadCatalogue(cityMode);
        for(size_t size = 0; size < kMapSizes.size(); ++size) for(size_t player = 0; player < players.size(); ++player)
            for(bool mirrored : {false, true}) for(int sample = 0; sample < 8; ++sample) {
                const auto& mix = mixes[(sample + size + player + mode) % mixes.size()];
                Params p;
                p.mapSize = kMapSizes[size];
                p.seed = seeds[(sample + 8 * mirrored + 3 * size + 5 * player + 7 * mode) % seeds.size()];
                p.house = HOUSE_INVALID; p.allies = 0; p.enemies = players[player] - 1; p.enemiesAllied = false;
                p.mode = cityMode; p.mod = mod; p.techLevel = 9; p.symmetric = mirrored;
                p.sandPercent = mix[0]; p.rockPercent = mix[1]; p.mountainPercent = mix[2];
                p.spice = Spice::Medium;
                p.playerSide = Side{"", BaseSize::None, 0, 0};
                p.allySide = p.enemySide = Side{"qBotMedium", BaseSize::None, 0, 0};
                const Result result = SkirmishGenerator::generate(p, catalogue);
                const std::string name = "matrix-" + std::to_string(p.mapSize) + "-" + std::to_string(players[player])
                    + "p-" + mod + (mirrored ? "-mirrored" : "-free") + "-sample" + std::to_string(sample);
                measurements.record(name, p, result, catalogue);
            }
    }
}

void runRender() {
    const std::string tag = std::getenv("SKIRMISH_RENDER_TAG") ? std::getenv("SKIRMISH_RENDER_TAG") : "current";
    StartRockMeasurements startRock(tag);
    for(int seed : {1, 2}) renderPicture(pictureOf(generateRandomMap(128, 128, seed)), "editor-random-128-seed" + std::to_string(seed));
    for(const auto& [name, params] : {std::pair<std::string, Params>{"stefan-dunecity", caseParams("stefan-128")}, {"vanilla-2v2", vanilla2v2Params()}}) {
        skirmishRequire(RandomSkirmish::activateMod(params.mod), "Render mod did not activate");
        const Catalogue catalogue = RandomSkirmish::loadCatalogue(params.mode);
        const Result result = SkirmishGenerator::generate(params, catalogue);
        renderPicture(pictureOf(result, catalogue), tag + "-generated-" + name);
        startRock.record(name, params, result, catalogue);
    }
    // The 80rock.dls reproduction: Random Skirmish with the CivWars mod, not the separate
    // Civilization Wars lane generator. Preserve its seed/start kit while varying geology.
    skirmishRequire(RandomSkirmish::activateMod("CivWars"), "Terrain-mix render mod did not activate");
    const Catalogue catalogue = RandomSkirmish::loadCatalogue(Mode::DuneCity);
    std::ofstream matrix(probeOut(tag + "-terrain-mix.csv"), std::ios::binary);
    matrix << "size,target,sand,dunes,rock,mountain,spice,map_seed,attempts\n";
    for(int size : kMapSizes) for(int land : {20, 50, 80}) {
        Params params;
        params.mapSize = size; params.seed = 418379001u; params.house = HOUSE_INVALID;
        params.allies = 1; params.enemies = 2; params.techLevel = 1; params.spice = Spice::None;
        params.mode = Mode::DuneCity; params.mod = "CivWars";
        params.sandPercent = 100 - land; params.rockPercent = land - 5; params.mountainPercent = 5;
        params.playerSide = Side{"qBotHard", BaseSize::None, 0, 0};
        params.allySide = params.enemySide = Side{"qBotHard", BaseSize::None, 0, 0};
        const Result result = SkirmishGenerator::generate(params, catalogue);
        startRock.record("terrain-mix-" + std::to_string(size) + "-land" + std::to_string(land), params, result, catalogue);
        int sand = 0, dunes = 0, rock = 0, mountain = 0, spice = 0;
        for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x) {
            const auto t = result.map(x, y);
            sand += t == Terrain_Sand; dunes += t == Terrain_Dunes; rock += t == Terrain_Rock;
            mountain += t == Terrain_Mountain; spice += pictureSpice(t);
        }
        matrix << size << ',' << land << ',' << sand << ',' << dunes << ',' << rock << ','
               << mountain << ',' << spice << ',' << result.mapSeed << ',' << result.attempts << '\n';
        SDL_Log("SKIRMISH_RENDER_MIX: size=%d target=%d sand=%d dunes=%d rock=%d mountain=%d spice=%d mapSeed=%u",
                size, land, sand, dunes, rock, mountain, spice, result.mapSeed);
        if(size == 160 && land == 80) {
            skirmishRequire(mountain <= 6544 / 2, "Default mountains exceed half of 80rock.dls");
            renderPicture(pictureOf(result.map), tag + "-generated-80rock");
            std::ofstream(probeOut(tag + "-generated-80rock.ini"), std::ios::binary) << result.ini;
        }
    }
    renderStartRockMeasurements(tag, startRock);
    startRock.finish();
    if(const char* save = std::getenv("SKIRMISH_RENDER_SAVE"))
        renderPicture(pictureOfSavedMap(save), "saved-" + getBasename(save, true));
}
