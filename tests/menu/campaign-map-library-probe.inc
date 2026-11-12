// Campaign missions as ordinary maps: exported from the game data, listed in the
// editor's Campaign tab and Custom Game's SP Maps, loadable by both.
#include <INIMap/CampaignMapLibrary.h>
void verifyCampaignMapLibrary() {
    using CampaignMapLibrary::missionLabel;
    menuRequire(missionLabel(1) == "1" && missionLabel(2) == "2 (A)" && missionLabel(4) == "2 (C)"
                && missionLabel(5) == "3 (A)" && missionLabel(19) == "7 (C)" && missionLabel(20) == "8 (A)"
                && missionLabel(21) == "8 (B)" && missionLabel(22) == "9",
                "Campaign mission labels do not follow the level/region layout");
    const auto pakBytes = [](const std::string& scenario) {
        auto rw = pFileManager->openFile(scenario);
        std::string bytes(static_cast<size_t>(SDL_RWsize(rw.get())), '\0');
        SDL_RWread(rw.get(), bytes.data(), 1, bytes.size());
        return bytes;
    };
    const std::string dir = CampaignMapLibrary::exportMaps();
    const std::string first = dir + "Atreides Mission 1.ini";
    menuRequire(existsFile(first) && existsFile(dir + "Harkonnen Mission 9.ini") && existsFile(dir + "Ordos Mission 8 (B).ini"),
                "Campaign missions were not exported as maps");
    menuRequire(readCompleteFile(first) == pakBytes("SCENA001.INI")
                && readCompleteFile(dir + "Ordos Mission 3 (B).ini") == pakBytes("SCENO006.INI"),
                "Exported campaign map differs from its scenario");

    // An edit saved over an exported map survives the next export; deleting it restores the original.
    writeCompleteFile(first, readCompleteFile(first) + "\n; edited\n");
    CampaignMapLibrary::exportMaps();
    menuRequire(readCompleteFile(first).find("; edited") != std::string::npos, "Export overwrote an edited campaign map");
    deleteFile(first);
    CampaignMapLibrary::exportMaps();
    menuRequire(readCompleteFile(first) == pakBytes("SCENA001.INI"), "Deleting a campaign map does not restore it");

    {
        LoadMapWindow window;
        window.onMapTypeChange(2);
        int listed = 0;
        for(int i = 0; i < window.mapList.getNumEntries(); ++i)
            for(const char* house : {"Atreides", "Harkonnen", "Ordos"})
                if(window.mapList.getEntry(i).rfind(std::string(house) + " Mission ", 0) == 0) ++listed;
        menuRequire(listed == 66, "Editor Campaign tab does not list all 66 original missions");
        menuCapture(window, "campaign-maps-editor");
    }
    {
        MapEditor editor;
        editor.loadMap(dir + "Atreides Mission 3 (B).ini");
        menuRequire(editor.getMap().getSizeX() > 0 && editor.getMap().getSizeY() > 0 && !editor.getStructureList().empty(),
                    "Editor cannot load a campaign mission");
    }
    {
        CustomPlaySetup setup; setup.mods = ModManager::instance().listModChoices(); setup.rules = effectiveGameOptions;
        CustomGameMenu browser(false, false, &setup);
        browser.onMapTypeChange(0);
        const auto* found = static_cast<const decltype(browser.mapEntries)::value_type*>(nullptr);
        for(const auto visible : browser.visibleMaps)
            if(browser.mapEntries[visible].path == first) found = &browser.mapEntries[visible];
        menuRequire(found && found->metadata.name == "Atreides Mission 1" && found->metadata.players >= 2,
                    "SP Maps does not offer the campaign mission as a playable map");
        menuCapture(browser, "campaign-maps-custom-game");
        GameInitSettings init(getBasename(first, true), readCompleteFile(first), true, setup.rules);
        CustomGamePlayers lobby(init, true, false, &setup);
        lobby.update();
        menuRequire(lobby.numHouses >= 2, "Custom Game lobby does not seat the campaign mission's houses");
    }
    // Not MENU_PROBE_PASS: the runner accepts exactly one result line per run.
    SDL_Log("CAMPAIGN_MAPS_OK: campaign missions exported, listed in editor and Custom Game, loadable by both");
}
