// Real renderer/sidebar regression probe; compiled only by the diagnostic runner.
#include <Game.h>
#include <GameInterface.h>
#include <House.h>
#include <Map.h>
#include <GameInitSettings.h>
#include <GUI/ObjectInterfaces/ZoneStructureInterface.h>
#include <FileClasses/LoadSavePNG.h>
#include <mod/ModManager.h>
#include <Menu/HouseChoiceMenu.h>
#include <Menu/CustomGamePlayers.h>
#include <fstream>
#include <iterator>
#include <memory>
#include <filesystem>
#include <chrono>
#include <Menu/OptionsMenu.h>
#include <Menu/SinglePlayerSkirmishMenu.h>
#include <cstring>

void skinRequire(bool condition, const char* message) {
    if(!condition) throw std::runtime_error(message);
}
std::string skinRead(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), {});
}
void skinWrite(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << bytes;
    skinRequire(bool(stream), "Cannot write cache fixture");
}
void runSkinCacheProbe() {
    namespace fs = std::filesystem;
    auto& manager = ModManager::instance();
    const fs::path profile = std::getenv("DUNECITY_USERDIR");
    const fs::path installed = fs::path(manager.getModPath("dunecity")) / "graphics_skins";
    const fs::path source = fs::path(getDuneLegacyDataDir()) / "mods/dunecity/graphics_skins";
    skinRequire(!fs::equivalent(source, installed), "Cache fixture source must be isolated from destination");
    const fs::path relative = "Dune2/zones/dunecity_atreides_residential_zone/zone.ini";
    const auto accepted = skinRead(source / relative);
    skinRequire(!accepted.empty() && skinRead(installed / relative) == accepted, "Fresh install payload mismatch");
    const std::vector<fs::path> sentinels = {profile/"save/skin-probe.dls", profile/"mods/authored-probe/GameOptions.ini", profile/"settings.keep"};
    for(const auto& path : sentinels) skinWrite(path, "preserve authored user data");
    const auto options = skinRead(installed.parent_path()/"GameOptions.ini");
    const auto configuration = skinRead(getConfigFilepath());
    // A stale older installation with newer file timestamps and equal byte count.
    skinWrite(installed / relative, std::string(accepted.size(), 'x'));
    fs::last_write_time(installed / relative, fs::file_time_type::clock::now()+std::chrono::hours(24));
    skinWrite(installed/".bundled-skin-fingerprint", "old-version-cache\n");
    manager.refreshBundledDunecityGraphicsSkins();
    skinRequire(skinRead(installed/relative)==accepted, "Equal-size stale cache survived upgrade");
    // Same-version source edit of equal length; source mtime remains unchanged.
    auto updated = accepted;
    const auto value = updated.find("SourceUnit=");
    skinRequire(value != std::string::npos, "Manifest fixture lacks SourceUnit");
    updated[value+11] = updated[value+11] == 'd' ? 'D' : 'd';
    const auto sourceTime = fs::last_write_time(source/relative);
    skinWrite(source/relative,updated); fs::last_write_time(source/relative,sourceTime);
    manager.refreshBundledDunecityGraphicsSkins();
    skinRequire(skinRead(installed/relative)==updated, "Same-version same-size source update was skipped");
    const auto installedTime = fs::last_write_time(installed/relative);
    manager.refreshBundledDunecityGraphicsSkins();
    skinRequire(fs::last_write_time(installed/relative)==installedTime, "Unchanged second launch recopied skin");
    skinWrite(source/relative,accepted); fs::last_write_time(source/relative,sourceTime);
    // Exercise the actual Advanced action, then simulate next-start restoration.
    { OptionsMenu menu; menu.onClearDuneCityAssets();
      skinRequire(menu.hasChildWindow() && !fs::exists(installed), "Advanced cache clear did not remove only the cache"); }
    manager.refreshBundledDunecityGraphicsSkins();
    skinRequire(skinRead(installed/relative)==accepted, "Manual clear did not recover bundled skin");
    for(const auto& path : sentinels) skinRequire(skinRead(path)=="preserve authored user data", "Cache touched user data");
    skinRequire(skinRead(installed.parent_path()/"GameOptions.ini")==options, "Cache touched gameplay configuration");
    skinRequire(skinRead(getConfigFilepath())==configuration, "Cache changed application settings");
    // Equivalent symlinked source/destination must never truncate the bundle.
    const fs::path backup = installed.parent_path()/"skin-probe-backup";
    fs::rename(installed,backup);
    fs::create_directory_symlink(source,installed);
    manager.refreshBundledDunecityGraphicsSkins();
    skinRequire(skinRead(source/relative)==accepted, "Same-tree refresh damaged bundled source");
    skinRequire(!manager.clearDunecityGraphicsCache(), "Desktop clear accepted the only bundled source");
    fs::remove(installed); fs::rename(backup,installed);
    SDL_Log("SKIN_CACHE_PROBE_PASS: fresh/stale/same-version cache, unchanged second refresh, Advanced recovery and user-data preservation");
}
int runSkinProbe() {
    runSkinCacheProbe();
    settings.general.playerName = "Skin tester";
    settings.network.metaServer = "http://127.0.0.1:1/";
    skinRequire(ModManager::instance().setActiveMod("dunecity"), "DuneCity mod missing");
    auto rules = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
    std::ifstream map(PROBE_MAP_PATH);
    std::string bytes((std::istreambuf_iterator<char>(map)), {});
    skinRequire(!bytes.empty(), "Map fixture missing");
    settings.network.relayUseDevelopmentEndpoint = true;
    settings.network.directDevelopmentEndpoint = "";
    effectiveGameOptions = rules;
    {
        HouseChoiceMenu host(true, false, true);
        skinRequire(host.campaignSkinDropDown.isEnabled(), "Campaign skin selector disabled for DuneCity");
        host.campaignSkinDropDown.setSelectedItem(1, true);
        skinRequire(HouseChoiceMenu::getCampaignSkin() == 1, "Campaign selector lost interactive choice");
    }
    {
        HouseChoiceMenu retry(true, true, true);
        skinRequire(retry.campaignSkinDropDown.getSelectedEntryIntData() == 1, "Campaign retry lost skin choice");
    }
    {
        GameInitSettings campaign(HOUSE_ATREIDES, rules, 1);
        campaign.setCampaignGraphicsSkin(GameInitSettings::GraphicsSkin::Dune2);
        campaign.enableCoop(true, "Skin probe");
        campaign.setScenarioData(bytes);
        CustomGamePlayers lobby(campaign, true);
        skinRequire(lobby.duneCitySkinControls, "Co-op lobby skin controls absent");
        skinRequire(lobby.houseInfo[0].graphicsSkinDropDown.getSelectedEntryIntData() == 1,
                    "Campaign skin did not propagate into co-op lobby");
        lobby.houseInfo[0].graphicsSkinDropDown.setSelectedItem(0);
        lobby.addAllPlayersToGameInitSettings();
        skinRequire(lobby.gameInitSettings.getHouseInfoList().front().graphicsSkin == GameInitSettings::GraphicsSkin::SimCity,
                    "Co-op house skin override did not enter game settings");
    }
    skinRequire(ModManager::instance().setActiveMod("dunecity"), "Restore DuneCity mod failed");
    auto game = std::make_unique<Game>(); currentGame = game.get();
    GameInitSettings init("skins.ini", bytes, "Skin probe", true, rules);
    for(const auto house : {HOUSE_ATREIDES, HOUSE_HARKONNEN}) {
        GameInitSettings::HouseInfo info(house, house == HOUSE_ATREIDES ? 1 : 2);
        info.graphicsSkin = GameInitSettings::GraphicsSkin::Dune2;
        info.addPlayerInfo({house == HOUSE_ATREIDES ? "Skin tester" : "Other house", "HumanPlayer"});
        init.addHouseInfo(info);
    }
    game->initGame(init);
    // Check actual decoded portrait pixels, not only a cache pointer: a missing
    // mapping can otherwise silently derive a top-down fallback from the atlas.
    const std::array<const char*, 8> factions{
        "harkonnen", "atreides", "ordos", "fremen", "sardaukar", "mercenary", "neutral", "rebels"};
    for(int house = 0; house < static_cast<int>(factions.size()); ++house) {
        pGFXManager->setDuneCityHouseGraphicsSkin(house, true);
        for(const bool hospital : {true, false}) {
            const auto picture = hospital ? Picture_Hospital : Picture_Church;
            const auto icon = std::filesystem::path(ModManager::instance().getModPath("dunecity"))
                / "graphics_skins/Dune2/buildings"
                / (std::string("dunecity_") + factions[house] + (hospital ? "_hospital" : "_church")) / "icon.png";
            auto input = sdl2::RWops_ptr{SDL_RWFromFile(icon.string().c_str(), "rb")};
            auto raw = input ? LoadPNG_RW(input.get()) : sdl2::surface_ptr{};
            skinRequire(bool(raw), "Civic authored icon is missing");
            auto expected = sdl2::surface_ptr{SDL_CreateRGBSurfaceWithFormat(0,91,55,32,SDL_PIXELFORMAT_RGBA32)};
            skinRequire(bool(expected), "Cannot create expected civic portrait");
            SDL_SetSurfaceBlendMode(raw.get(), SDL_BLENDMODE_NONE);
            skinRequire(SDL_BlitScaled(raw.get(), nullptr, expected.get(), nullptr) == 0,
                        "Cannot fit expected civic portrait");
            auto expectedTexture = convertSurfaceToTexture(expected.get());
            auto* actualTexture = pGFXManager->getSmallDetailPic(picture, house);
            skinRequire(actualTexture != nullptr, "Civic house portrait is missing");
            SDL_Rect bounds{0,0,91,55};
            SDL_SetRenderDrawColor(renderer,0,0,0,255); SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, actualTexture, nullptr, &bounds);
            auto actualPixels = renderReadSurface(renderer);
            SDL_RenderClear(renderer); SDL_RenderCopy(renderer, expectedTexture.get(), nullptr, &bounds);
            auto expectedPixels = renderReadSurface(renderer);
            skinRequire(actualPixels && expectedPixels && actualPixels->w == expectedPixels->w
                        && actualPixels->h == expectedPixels->h, "Cannot capture civic portrait");
            for(int row=0; row<actualPixels->h; ++row) {
                  const bool equal = std::memcmp(static_cast<char*>(actualPixels->pixels)+row*actualPixels->pitch,
                            static_cast<char*>(expectedPixels->pixels)+row*expectedPixels->pitch,
                              actualPixels->w*actualPixels->format->BytesPerPixel) == 0;
                  if(!equal) {
                      SDL_Log("Civic portrait mismatch: house=%d kind=%s row=%d", house,
                              hospital ? "hospital" : "church", row);
                      SavePNG(actualPixels.get(), "civic-actual.png");
                      SavePNG(expectedPixels.get(), "civic-expected.png");
                  }
                  skinRequire(equal, "Selected civic portrait differs from its authored faction icon");
            }
        }
        pGFXManager->setDuneCityHouseGraphicsSkin(house, false);
    }
    for(const auto house : {HOUSE_ATREIDES, HOUSE_HARKONNEN}) {
        auto* owner = game->getHouse(house);
        skinRequire(owner != nullptr, "House missing");
        for(const int item : {Structure_ZoneResidential, Structure_ZoneCommercial, Structure_ZoneIndustrial}) {
            Coord site = Coord::Invalid();
            for(int y=1; y<currentGameMap->getSizeY()-2 && !site.isValid(); ++y)
                for(int x=1; x<currentGameMap->getSizeX()-2; ++x)
                    if(currentGameMap->okayToPlaceStructure(x,y,2,2,false,nullptr)) { site=Coord(x,y); break; }
            skinRequire(site.isValid(), "No zone placement site");
            auto* zone = dynamic_cast<ZoneStructure*>(owner->placeStructure(NONE_ID,item,site.x,site.y,false,true));
            skinRequire(zone != nullptr, "Zone placement failed");
            skinRequire(zone->getStructureSize() == Coord(2,2), "Skin changed logical zone footprint");
            pGFXManager->setDuneCityHouseGraphicsSkin(house, true);
            {
                std::unique_ptr<ZoneStructureInterface> panel(ZoneStructureInterface::create(zone->getObjectID()));
                skinRequire(panel->update() && panel->objPicture.isVisible() && !panel->zonePreview.isVisible(),
                            "Selected Dune2 zone displays top-down lot instead of house icon");
                skinRequire(panel->objPicture.pTexture.get() == resolveItemPicture(item,house),
                            "Selected zone and construction portrait differ");
                SDL_SetRenderDrawColor(renderer, 20,20,20,255); SDL_RenderClear(renderer);
                panel->draw(Point(20,20));
                auto image = renderReadSurface(renderer);
                const auto filename = "zone-"+std::to_string(house)+"-"+std::to_string(item)+".png";
                SavePNG(image.get(), filename.c_str());
                for(auto overlay : {ZoneStructure::CivicOverlay::Hospital, ZoneStructure::CivicOverlay::Church}) {
                    zone->setCivicOverlay(overlay); panel->update();
                    const auto portraitID = overlay == ZoneStructure::CivicOverlay::Hospital
                        ? Picture_Hospital : Picture_Church;
                    skinRequire(panel->objPicture.isVisible() && !panel->zonePreview.isVisible()
                                && panel->objPicture.pTexture.get() == pGFXManager->getSmallDetailPic(portraitID, house),
                                "Civic overlay did not select its own house portrait");
                    const int picture = overlay == ZoneStructure::CivicOverlay::Hospital
                        ? ObjPic_Hospital : ObjPic_Church;
                    const SDL_Rect destination{200, 200, 64, 64};
                    // The real atlas cache decodes asynchronously. Retry on the
                    // render thread just as consecutive game frames do.
                    const Uint32 started = SDL_GetTicks();
                    bool drawn = false;
                    do {
                        drawn = pGFXManager->drawDuneCityBuilding(
                            item, house, 0, destination, picture);
                        if(!drawn) SDL_Delay(10);
                    } while(!drawn && SDL_GetTicks() - started < 3000);
                    skinRequire(drawn,
                                "Civic overlay failed to select its high-detail house picture");
                    pGFXManager->setDuneCityHouseGraphicsSkin(house, false);
                    panel->update();
                    skinRequire(!panel->objPicture.isVisible() && panel->zonePreview.isVisible(),
                                "SimCity civic overlay lost live preview");
                    skinRequire(!pGFXManager->drawDuneCityBuilding(
                                    item, house, 0, destination, picture),
                                "High-detail civic overlay leaked into SimCity skin");
                    pGFXManager->setDuneCityHouseGraphicsSkin(house, true);
                    panel->update();
                    skinRequire(panel->objPicture.isVisible()
                                && panel->objPicture.pTexture.get() == pGFXManager->getSmallDetailPic(portraitID, house),
                                "Open civic properties panel kept a stale icon after skin switching");
                }
                zone->setCivicOverlay(ZoneStructure::CivicOverlay::None);
                panel->update();
                skinRequire(panel->objPicture.pTexture.get() == resolveItemPicture(item,house),
                            "Cleared civic overlay kept its previous portrait");
                pGFXManager->setDuneCityHouseGraphicsSkin(house, false); panel->update();
                skinRequire(!panel->objPicture.isVisible() && panel->zonePreview.isVisible(),
                            "SimCity zone lost live preview");
            }
            pGFXManager->setDuneCityHouseGraphicsSkin(house, true);
            // Missing exact growth cell must decline the skin renderer so native art can draw.
            skinRequire(!pGFXManager->drawDuneCityZone(item,house,0,99,99,
                        GFXManager::DuneCityZoneActivity::Idle,0,200,200), "Missing cell lost native fallback");
        }
    }
    // Make the missing-house fixture independent of future artwork additions.
    const auto definitions = pGFXManager->duneCityBuildingDefinitions;
    auto& missingFixture = pGFXManager->duneCityBuildingDefinitions;
    missingFixture.erase(std::remove_if(missingFixture.begin(), missingFixture.end(),
        [](const auto& definition) {
            return definition.houseID == HOUSE_ORDOS && definition.pictureID == ObjPic_Hospital;
        }), missingFixture.end());
    pGFXManager->setDuneCityHouseGraphicsSkin(HOUSE_ORDOS, true);
    skinRequire(!pGFXManager->drawDuneCityBuilding(Structure_ZoneResidential,
                    HOUSE_ORDOS, 0, SDL_Rect{200,200,64,64}, ObjPic_Hospital),
                "Missing Hospital must retain its fallback, not another faction's art");
    pGFXManager->setDuneCityHouseGraphicsSkin(HOUSE_ORDOS, false);
    pGFXManager->duneCityBuildingDefinitions = definitions;
    game.reset(); currentGame=nullptr; pLocalPlayer=nullptr;
    // The Custom Game lobby's default row is Random: its house exists only after the map loader
    // resolves it. Dune2 chosen on such a row must reach that house; SimCity rows stay SimCity.
    {
        auto randomGame = std::make_unique<Game>(); currentGame = randomGame.get();
        GameInitSettings randomInit("skins.ini", bytes, "Skin probe", true, rules);
        for(int row = 0; row < 2; ++row) {
            GameInitSettings::HouseInfo info(HOUSE_INVALID, row + 1);
            info.graphicsSkin = row == 0 ? GameInitSettings::GraphicsSkin::Dune2 : GameInitSettings::GraphicsSkin::SimCity;
            info.addPlayerInfo({row == 0 ? "Skin tester" : "Other house", "HumanPlayer"});
            randomInit.addHouseInfo(info);
        }
        randomGame->initGame(randomInit);
        const auto& resolved = randomGame->houseInfoListSetup;
        skinRequire(resolved.size() == 2 && resolved[0].houseID != HOUSE_INVALID && resolved[1].houseID != HOUSE_INVALID,
                    "Random rows were not resolved to houses");
        skinRequire(pGFXManager->isDuneCityHouseUsingDune2(resolved[0].houseID),
                    "Dune2 chosen on a Random Custom Game row did not reach its house");
        skinRequire(!pGFXManager->isDuneCityHouseUsingDune2(resolved[1].houseID),
                    "SimCity Random row was drawn with Dune2");
        randomGame.reset(); currentGame=nullptr; pLocalPlayer=nullptr;
    }
    // Single-mission Skirmish has its own DuneCity skin choice, applied to every house.
    {
        SinglePlayerSkirmishMenu menu;
        skinRequire(menu.skinDropDown.isEnabled(), "Skirmish skin selector disabled for DuneCity");
        menu.skinDropDown.setSelectedItem(1);
        skinRequire(GameInitSettings::sanitizeGraphicsSkin(menu.skinDropDown.getSelectedEntryIntData())
                    == GameInitSettings::GraphicsSkin::Dune2, "Skirmish skin selector lost Dune2");
        auto skirmish = std::make_unique<Game>(); currentGame = skirmish.get();
        GameInitSettings skirmishInit(HOUSE_ATREIDES, 1, rules);
        skirmishInit.setCampaignGraphicsSkin(GameInitSettings::GraphicsSkin::Dune2);
        GameInitSettings::HouseInfo human(HOUSE_ATREIDES, 1);
        human.addPlayerInfo({"Skin tester", "HumanPlayer"});
        skirmishInit.addHouseInfo(human);
        skirmish->initGame(skirmishInit);
        for(const int house : {HOUSE_ATREIDES, HOUSE_HARKONNEN, HOUSE_ORDOS})
            skinRequire(pGFXManager->isDuneCityHouseUsingDune2(house), "Skirmish Dune2 skin did not reach every house");
        skirmish.reset(); currentGame=nullptr; pLocalPlayer=nullptr;
    }
    SDL_Log("SKIN_PROBE_PASS: all 16 authored civic portraits; six selected zone portraits; live skin/overlay switches; fixed footprints; high-detail civic pictures; SimCity isolation; missing-house/cell fallbacks; Random Custom Game rows; Skirmish skin");
    return MENU_QUIT_DEFAULT;
}
