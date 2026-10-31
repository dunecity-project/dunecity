// Real widgets and editor operations, included only by the private menu probe.
#include <MapEditor/ChoamWindow.h>
#include <misc/SpiceGeneration.h>

void verifyTornieFamilyMenus() {
    auto& mods = ModManager::instance();
    const auto previousMod = mods.getActiveModName();
    const auto previousOptions = effectiveGameOptions;
    const auto previousCampaignOptions = HouseChoiceMenu::s_currentGameOptions;
    const auto previousCustomMod = settings.general.customGameMod;
    INIFile previousConfig(getConfigFilepath());
    const int previousHouse = HouseChoiceMenu::s_house;
    const int previousLevel = HouseChoiceMenu::s_startLevel;
    const bool previousOnline = HouseChoiceMenu::s_online;
    const bool previousSeparate = HouseChoiceMenu::s_separateFactionCoop;
    const bool previousSingle = HouseChoiceMenu::s_singleMission;
    auto mapDir = getDuneLegacyDataDir() + "/maps/multiplayer/";
    auto maps = getFileNamesList(mapDir, "ini", true);
    if(maps.empty()) {
        mapDir = getDuneLegacyDataDir() + "/data/maps/multiplayer/";
        maps = getFileNamesList(mapDir, "ini", true);
    }
    const auto map = mapDir + "2P - 64x64 - Great Divide.ini";
    menuRequire(std::filesystem::is_regular_file(map), "Family lobby needs its bundled two-player map");

    for(const std::string name : {"Tornie", "TornieLite", "Jericho", "JerichoLite", "ArcaKev", "ArcaKevLite"}) {
        SDL_Log("TORNIE_MENU_BEGIN: mod=%s", name.c_str());
        menuRequire(mods.setActiveMod(name), "Family menu mod unavailable");
        effectiveGameOptions = mods.loadEffectiveGameOptions(settings.gameOptions);
        const auto roster = mods.getPlayableHouses();
        const bool lite = name.find("Lite") != std::string::npos;
        menuRequire(roster.size() == (lite ? 6u : 12u), "Wrong edition house count");
        HouseChoiceMenu::s_singleMission = false;
        HouseChoiceMenu::s_startLevel = 1;
        HouseChoiceMenu::s_separateFactionCoop = false;
        {
            HouseChoiceMenu menu(false);
            menuRequire(menu.houseChoices.count == static_cast<int>(roster.size())
                        && std::is_permutation(menu.houseChoices.houses.begin(), menu.houseChoices.houses.begin() + menu.houseChoices.count,
                                               roster.begin(), roster.end()), "Campaign roster differs from edition data");
            for(size_t i = 0; i < roster.size(); ++i) {
                const int expectedHouse = menu.houseChoices[static_cast<int>(i)];
                while(static_cast<int>(i) >= menu.currentHouseChoiceScrollPos + 3) {
                    menuRequire(menu.houseRightButton.isVisible(), "Campaign cannot scroll to its last houses");
                    menu.houseRightButton.handleMouseLeft(4, 4, true);
                    menu.houseRightButton.handleMouseLeft(4, 4, false);
                }
                PictureButton* buttons[] = {&menu.house1Button, &menu.house2Button, &menu.house3Button};
                auto& button = *buttons[i - menu.currentHouseChoiceScrollPos];
                menuRequire(button.isVisible(), "Campaign house button is hidden");
                button.handleMouseLeft(20, 20, true);
                button.handleMouseLeft(20, 20, false);
                menuRequire(HouseChoiceMenu::s_house == expectedHouse && !menu.isQuiting(), "Campaign click selects an invalid house or launches early");
                GameInitSettings campaign(static_cast<HOUSETYPE>(expectedHouse), effectiveGameOptions, 1);
                auto scenario = pFileManager->openCampaignFile(campaign.getFilename());
                menuRequire(scenario && SDL_RWsize(scenario.get()) > 0, "Selected campaign house has no opening mission");
            }
            menuCapture(menu, "family-campaign-" + name);
            menu.hostCoopButton.handleMouseLeft(10, 10, true);
            menu.hostCoopButton.handleMouseLeft(10, 10, false);
            menuRequire(menu.isQuiting() && menu.retVal == menu.houseChoices[menu.houseChoices.count - 1], "Campaign Start loses the selected house");
        }
        {
            HouseChoiceMenu menu(true);
            menuRequire(menu.connectionDropDown.getNumEntries() == 3, "Family campaign lacks faction co-op");
            menuRequire(menu.connectionDropDown.getSelectedEntryIntData() == 1
                        && !HouseChoiceMenu::isSeparateFactionCoop(), "Shared-house co-op stopped being the online default");
            menu.connectionDropDown.setSelectedItem(2);
            menuRequire(HouseChoiceMenu::isSeparateFactionCoop() && !menu.partnerSelector.aiBox.isEnabled()
                        && !menu.gameOptionsButton.isEnabled(), "Faction co-op exposes settings owned by its next screen");
            menu.connectionDropDown.setSelectedItem(1);
            menuRequire(!HouseChoiceMenu::isSeparateFactionCoop() && menu.gameOptionsButton.isEnabled(), "Returning to shared-house co-op loses controls");
        }
        {
            CustomPlaySetup setup;
            setup.online = true;
            setup.maps.push_back(map);
            setup.mods = mods.listModChoices();
            const auto choice = std::find_if(setup.mods.begin(), setup.mods.end(), [&](const auto& mod) { return mod.name == name; });
            menuRequire(choice != setup.mods.end(), "Family mod missing from custom chooser");
            setup.mod = static_cast<int>(std::distance(setup.mods.begin(), choice));
            setup.rules = effectiveGameOptions;
            {
                CustomGameMenu browser(false, false, &setup);
                menuRequire(browser.getSelectedMapPath() == setup.maps.front(), "Family map chooser lost its bundled map");
                browser.onNext();
                menuRequire(browser.retVal == MENU_SETUP_PLAYERS && mods.getActiveModName() == name, "Family custom chooser cannot reach player setup");
            }
            GameInitSettings init(getBasename(setup.maps.front(), true), readCompleteFile(setup.maps.front()), true, setup.rules);
            CustomGamePlayers lobby(init, true, false, &setup);
            lobby.update();
            menuRequire(lobby.numHouses == 2 && lobby.mainVBox.getMinimumSize().x <= lobby.mainVBox.getSize().x
                        && lobby.mainVBox.getMinimumSize().y <= lobby.mainVBox.getSize().y, "Family custom lobby does not fit");
            auto& houses = lobby.houseInfo[0].houseDropDown;
            std::vector<int> offered;
            for(int i = 0; i < houses.getNumEntries(); ++i)
                if(houses.getEntryIntData(i) != HOUSE_INVALID) offered.push_back(houses.getEntryIntData(i));
            menuRequire(std::is_permutation(offered.begin(), offered.end(), roster.begin(), roster.end()), "Custom lobby does not offer exactly the data-defined house roster");
            houses.onOpenListBoxButton();
            menuCapture(lobby, "family-lobby-houses-" + name);
            houses.onOpenListBoxButton();
            lobby.onNext();
            menuRequire(lobby.retVal == MENU_SETUP_HOST && lobby.gameInitSettings.getModName() == name,
                        "Family custom setup cannot prepare its lobby or loses the mod");
        }
        {
            MapEditor editor;
            auto& ui = *editor.pInterface;
            while(ui.hasChildWindow()) { ui.closeChildWindow(); ui.processChildWindowOpenCloses(); }
            editor.setMap(MapData(64, 64, Terrain_Rock), MapInfo());
            menuRequire(ui.tornieContentVisible_ && !ui.cityStructsVisible_, "Family editor has the wrong palette");
            menuRequire(ui.houseDropDownBox.getNumEntries() == static_cast<int>(roster.size()), "Editor house chooser differs from edition data");
            struct Entry { SymbolButton* button; HBox* row; int item; };
            const Entry units[] = {
                {&ui.editorModeUnits_Infantry5, &ui.editorModeUnits_HBoxSquads, Unit_Infantry5},
                {&ui.editorModeUnits_Troopers5, &ui.editorModeUnits_HBoxSquads, Unit_Troopers5},
                {&ui.editorModeUnits_EliteOrnithopter, &ui.editorModeUnits_HBoxTornieChemical, Unit_EliteOrnithopter},
                {&ui.editorModeUnits_RaiderQuad, &ui.editorModeUnits_HBoxVariants, Unit_RaiderQuad},
                {&ui.editorModeUnits_RangedTank, &ui.editorModeUnits_HBoxVariants, Unit_RangedTank},
                {&ui.editorModeUnits_SlowTank, &ui.editorModeUnits_HBoxVariants, Unit_SlowTank},
                {&ui.editorModeUnits_CapacityHarvester, &ui.editorModeUnits_HBoxHarvesters, Unit_CapacityHarvester},
                {&ui.editorModeUnits_FastHarvester, &ui.editorModeUnits_HBoxHarvesters, Unit_FastHarvester},
                {&ui.editorModeUnits_TameSandworm, &ui.editorModeUnits_HBoxHarvesters, Unit_TameSandworm},
                {&ui.editorModeUnits_FlameQuad, &ui.editorModeUnits_HBoxFlame, Unit_FlameQuad},
                {&ui.editorModeUnits_GasInfantry, &ui.editorModeUnits_HBoxFlame, Unit_GasInfantry},
                {&ui.editorModeUnits_FlameTrooper, &ui.editorModeUnits_HBoxFlame, Unit_FlameTrooper},
            };
            const Entry structures[] = {
                {&ui.editorModeStructs_Doublefinery, &ui.editorModeStructs_HBoxDoublefinery, Structure_Doublefinery},
                {&ui.editorModeStructs_Slowpost, &ui.editorModeStructs_HBoxScoutpost, Structure_Slowpost},
            };
            const auto clickPalette = [&](const Entry& entry, VBox& box, ScrollView& scroll) {
                menuRequire(entry.button->getParent() == entry.row && entry.button->pSymbolSurface,
                            "New editor object has no palette button/icon");
                const auto pos = box.getWidgetPosition(entry.row) + entry.row->getWidgetPosition(entry.button);
                menuRequire(pos.x >= 0 && pos.x + entry.button->getSize().x <= scroll.getContentWidth(), "New palette object is clipped horizontally");
                scroll.scrollbar.setCurrentValue(pos.y);
                const int x = pos.x + entry.button->getSize().x / 2;
                const int y = pos.y - scroll.scrollbar.getCurrentValue() + entry.button->getSize().y / 2;
                menuRequire(y >= 0 && y < scroll.getSize().y, "New palette object cannot be scrolled into view");
                scroll.handleMouseLeft(x, y, true);
                scroll.handleMouseLeft(x, y, false);
                menuRequire(editor.currentEditorMode.itemID == entry.item && entry.button->getToggleState(), "New palette object click selects the wrong item");
            };
            ui.onModeButton(3);
            for(size_t i = 0; i < std::size(units); ++i) {
                clickPalette(units[i], ui.editorModeUnits_VBox, ui.editorModeUnits_ScrollView);
                editor.performMapEdit(2 + static_cast<int>(i) * 4, 16, false);
                menuRequire(editor.getUnitList().size() == i + 1 && editor.getUnitList().back().itemID == units[i].item, "New editor unit cannot be placed");
            }
            menuCapture(ui, "family-editor-units-" + name);
            ui.onModeButton(2);
            for(size_t i = 0; i < std::size(structures); ++i) {
                clickPalette(structures[i], ui.editorModeStructs_VBox, ui.editorModeStructs_ScrollView);
                editor.performMapEdit(2 + static_cast<int>(i) * 6, 4, false);
                menuRequire(editor.getStructureList().size() == i + 1 && editor.getStructureList().back().itemID == structures[i].item, "New editor structure cannot be placed");
            }
            menuCapture(ui, "family-editor-structures-" + name);
            ui.onModeButton(1);
            const Entry spices[] = {
                {&ui.editorModeTerrain_PaleLilacSpice, &ui.editorModeTerrain_HBox6, Terrain_PaleLilacSpice},
                {&ui.editorModeTerrain_ThickPaleLilacSpice, &ui.editorModeTerrain_HBox6, Terrain_ThickPaleLilacSpice},
                {&ui.editorModeTerrain_PaleLilacSpiceBloom, &ui.editorModeTerrain_HBox6, Terrain_PaleLilacSpiceBloom},
                {&ui.editorModeTerrain_WhiteSpice, &ui.editorModeTerrain_HBox7, Terrain_WhiteSpice},
                {&ui.editorModeTerrain_ThickWhiteSpice, &ui.editorModeTerrain_HBox7, Terrain_ThickWhiteSpice},
                {&ui.editorModeTerrain_WhiteSpiceBloom, &ui.editorModeTerrain_HBox7, Terrain_WhiteSpiceBloom},
            };
            const unsigned expectedMask = name.find("Jericho") == 0 ? 10u : name.find("ArcaKev") == 0 ? 9u : 5u;
            menuRequire(activeSpiceVariantMask() == expectedMask, "Edition spice palette has the wrong colours");
            for(size_t i = 0; i < std::size(spices); ++i) {
                const auto& entry = spices[i];
                const bool enabled = (expectedMask & (i < 3 ? 4u : 8u)) != 0;
                menuRequire(entry.button->isEnabled() == enabled && entry.button->pSymbolSurface
                            && entry.button->getParent() == entry.row, "Edition spice control is missing or has the wrong enabled state");
                if(enabled) {
                    // The terrain modes use a different selection field from objects.
                    const auto pos = ui.editorModeTerrain_VBox.getWidgetPosition(entry.row) + entry.row->getWidgetPosition(entry.button);
                    auto& scroll = ui.editorModeTerrain_ScrollView;
                    scroll.scrollbar.setCurrentValue(pos.y);
                    const int x = pos.x + entry.button->getSize().x / 2;
                    const int y = pos.y - scroll.scrollbar.getCurrentValue() + entry.button->getSize().y / 2;
                    menuRequire(x >= 0 && x < scroll.getContentWidth() && y >= 0 && y < scroll.getSize().y, "Enabled spice control cannot be reached");
                    scroll.handleMouseLeft(x, y, true);
                    scroll.handleMouseLeft(x, y, false);
                    menuRequire(editor.currentEditorMode.terrainType == entry.item && entry.button->getToggleState(), "Spice control selects the wrong terrain");
                    editor.performMapEdit(2 + static_cast<int>(i) * 6, 40, false);
                    menuRequire(editor.getMap()(2 + static_cast<int>(i) * 6, 40) == entry.item, "Enabled spice cannot be painted");
                }
            }
            menuCapture(ui, "family-editor-spice-" + name);
            ui.choamButton.handleMouseLeft(10, 10, true);
            ui.choamButton.handleMouseLeft(10, 10, false);
            ui.processChildWindowOpenCloses();
            auto* choam = dynamic_cast<ChoamWindow*>(ui.pChildWindow);
            menuRequire(choam && choam->choamUnits_.size() == 31, "Family CHOAM does not expose all 31 units");
            menuRequire(choam->getSize().x <= getRendererWidth() && choam->getSize().y <= getRendererHeight()
                        && choam->rowsScroll.isScrollbarVisible(), "31-unit CHOAM has no usable scrolling window");
            for(size_t i = 0; i < choam->choamUnits_.size(); ++i) {
                auto& row = choam->choamRows[i / 2];
                auto& checkbox = i % 2 == 0 ? row.Checkbox_Unit1 : row.Checkbox_Unit2;
                auto& amount = i % 2 == 0 ? row.TextBox_Unit1 : row.TextBox_Unit2;
                const auto pos = choam->rowsVBox.getWidgetPosition(&row.HBox_Unit) + row.HBox_Unit.getWidgetPosition(&checkbox);
                choam->rowsScroll.scrollbar.setCurrentValue(pos.y);
                const int y = pos.y - choam->rowsScroll.scrollbar.getCurrentValue() + 10;
                menuRequire(y >= 0 && y < choam->rowsScroll.getSize().y, "CHOAM final rows cannot be reached");
                if(!checkbox.isChecked()) {
                    choam->rowsScroll.handleMouseLeft(pos.x + 8, y, true);
                    choam->rowsScroll.handleMouseLeft(pos.x + 8, y, false);
                }
                menuRequire(checkbox.isChecked() && amount.isVisible(), "CHOAM unit checkbox does not expose its amount");
                amount.setValue(static_cast<int>(i) + 1);
            }
            menuRequire(choam->choamRows.back().Checkbox_Unit2.getParent() == nullptr, "Odd final CHOAM row exposes a phantom unit");
            menuCapture(ui, "family-editor-choam-" + name);
            choam->okButton.handleMouseLeft(10, 10, true);
            choam->okButton.handleMouseLeft(10, 10, false);
            ui.processChildWindowOpenCloses();
            menuRequire(editor.getChoam().size() == 31, "CHOAM OK lost units");
            const auto choamValues = editor.getChoam();
            for(size_t i = 0; i < ChoamWindow::allChoamUnits.size(); ++i)
                menuRequire(choamValues.at(ChoamWindow::allChoamUnits[i]) == static_cast<int>(i) + 1, "CHOAM OK changed a unit amount");
            const auto path = std::string(std::getenv("MENU_PROBE_OUT")) + "/family-editor-" + name + "-" + std::getenv("MENU_PROBE_WIDTH") + ".ini";
            editor.saveMap(path);
            editor.loadMap(path);
            menuRequire(editor.getUnitList().size() == std::size(units) && editor.getStructureList().size() == std::size(structures)
                        && editor.getChoam() == choamValues, "Family editor map round trip lost new objects or CHOAM");
            for(size_t i = 0; i < std::size(units); ++i)
                menuRequire(editor.getUnitList()[i].itemID == units[i].item, "Family map round trip changed a new unit identity");
            for(size_t i = 0; i < std::size(structures); ++i)
                menuRequire(editor.getStructureList()[i].itemID == structures[i].item, "Family map round trip changed a new structure identity");
            for(size_t i = 0; i < std::size(spices); ++i)
                if(spices[i].button->isEnabled())
                    menuRequire(editor.getMap()(2 + static_cast<int>(i) * 6, 40) == spices[i].item, "Family map round trip lost coloured spice");
            SDL_ClearError();
            editor.drawMap(screenborder, false);
            menuRequire(SDL_GetError()[0] == '\0', "Family editor map rendering failed");
        }
        SDL_Log("TORNIE_MENU_PASS: mod=%s houses=%zu campaign_selections=%zu custom_lobby=1 editor_units=12 editor_structures=2 choam_units=31 choam_scroll=1 spice_enabled=3 spice_disabled=3 spice_mask=%u",
                name.c_str(), roster.size(), roster.size(), name.find("Jericho") == 0 ? 10u : name.find("ArcaKev") == 0 ? 9u : 5u);
    }
    menuRequire(mods.setActiveMod(previousMod), "Cannot restore mod after family menu matrix");
    effectiveGameOptions = previousOptions;
    settings.general.customGameMod = previousCustomMod;
    menuRequire(previousConfig.saveChangesTo(getConfigFilepath()), "Cannot restore menu fixture preferences");
    HouseChoiceMenu::s_currentGameOptions = previousCampaignOptions;
    HouseChoiceMenu::s_house = previousHouse;
    HouseChoiceMenu::s_startLevel = previousLevel;
    HouseChoiceMenu::s_online = previousOnline;
    HouseChoiceMenu::s_separateFactionCoop = previousSeparate;
    HouseChoiceMenu::s_singleMission = previousSingle;
}
