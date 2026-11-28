// Included inside ZoneGarrisonProbe. No visible window: production layer drawing
// and bitmap/modern sidebar layout are checked on SDL software render targets.
void checkGarrisonAppearance() {
    Fixture f("garrison appearance");
    struct RestoreRenderState {
        SDL_Texture* target = SDL_GetRenderTarget(renderer);
        int width = getRendererWidth(), height = getRendererHeight();
        int zoom = currentZoomlevel, skin = MenuTheme::uiSkin;
        std::array<int, NUM_HOUSES> colors = houseToVisualHouse;
        std::unique_ptr<GUIStyle> style = std::move(GUIStyle::currentGUIStyle);
        ~RestoreRenderState() {
            SDL_SetRenderTarget(renderer, target);
            SDL_RenderSetLogicalSize(renderer, width, height);
            currentZoomlevel = zoom;
            houseToVisualHouse = colors;
            MenuTheme::setUISkin(skin);
            GUIStyle::setGUIStyle(std::move(style));
        }
    } restore;
    GUIStyle::setGUIStyle(std::make_unique<DuneStyle>());
    auto* own = f.zone(f.own, Structure_ZoneResidential, 20, 20);
    auto* enemy = f.zone(f.enemy, Structure_ZoneCommercial, 44, 44);
    auto* hospital = f.zone(f.enemy, Structure_ZoneResidential, 40, 24, 3);
    hospital->setCivicOverlay(ZoneStructure::CivicOverlay::Hospital);
    auto* ownMan = f.unit(f.own, Unit_Soldier, 19, 20);
    auto* enemyMan = f.unit(f.enemy, Unit_Trooper, 43, 44);
    auto* taker = f.unit(f.own, Unit_Soldier, 39, 24);
    f.capture(ownMan, own);
    f.capture(enemyMan, enemy);
    require(f.runUntil(10, [&] { return own->isGarrisoned() && enemy->isGarrisoned(); }),
            "appearance: own/enemy fixtures did not garrison");
    require(captureByPlayer(f, {taker}, hospital) == 1, "appearance: real Capture widget queued no hospital order");
    require(f.runUntil(20, [&] { return hospital->getOwner() == f.own && hospital->isGarrisoned(); }),
            "appearance: the hospital was not captured");
    require(hospital->getGarrisonOriginalOwner() == f.enemy->getHouseID(), "appearance: hospital has no captured marker");

    auto rgba = [](SDL_Surface* image, int x, int y) {
        Uint8 r, g, b, a;
        SDL_GetRGBA(getPixel(image, x, y), image->format, &r, &g, &b, &a);
        return COLOR_RGB(r, g, b);
    };
    auto save = [&](SDL_Surface* image, const std::string& name) {
        const std::string file = gOut + "/" + name + ".png";
        require(SavePNG(image, file.c_str()) == 0, "appearance: cannot save " + name);
    };
    sdl2::texture_ptr target(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                               SDL_TEXTUREACCESS_TARGET, 640, 480));
    require(target != nullptr, "appearance: cannot allocate overlay target");
    // Deliberately remap identities to different lobby colours. An ID-based or
    // local-player-only tint must fail, including for an enemy garrison.
    houseToVisualHouse[f.own->getHouseID()] = HOUSE_ORDOS;
    houseToVisualHouse[f.enemy->getHouseID()] = HOUSE_ATREIDES;
    for(auto* zone : {own, enemy, hospital}) {
        for(int zoom = 0; zoom < 3; ++zoom) {
            currentZoomlevel = zoom;
            screenborder->adjustScreenBorderToMapsize(kMapSize, kMapSize);
            screenborder->setNewScreenCenter(zone->getLocation() * TILESIZE);
            currentGameMap->viewMap(f.own->getHouseID(), zone->getLocation(), 6);
            zone->setFogged(false);
            SDL_SetRenderTarget(renderer, target.get());
            SDL_RenderSetLogicalSize(renderer, 640, 480);
            auto clear = [&] { setRenderDrawColor(renderer, COLOR_RGB(255, 0, 255)); SDL_RenderClear(renderer); };
            clear();
            zone->drawGarrisonSandbags();
            auto layer = renderReadSurface(renderer);
            clear();
            zone->blitToScreen(); // actual visible own/enemy building render path
            auto composed = renderReadSurface(renderer);
            require(layer && composed, "appearance: overlay pixel readback failed");
            const int slot = getHouseVisualHouse(zone->getGarrisonHouse()->getHouseID());
            const std::array<Uint32, 4> ramp{{getHouseColorRGB(slot, 0), getHouseColorRGB(slot, 3),
                                             getHouseColorRGB(slot, 5), getHouseColorRGB(slot, 7)}};
            std::array<int, 4> pixels{};
            int wrong = 0, missing = 0;
            SDL_LockSurface(layer.get());
            SDL_LockSurface(composed.get());
            for(int y = 0; y < layer->h; ++y) for(int x = 0; x < layer->w; ++x) {
                const Uint32 color = rgba(layer.get(), x, y);
                if(color == COLOR_RGB(255, 0, 255)) continue;
                const auto shade = std::find(ramp.begin(), ramp.end(), color);
                if(shade == ramp.end()) ++wrong;
                else ++pixels[static_cast<size_t>(shade - ramp.begin())];
                if(rgba(composed.get(), x, y) != color) ++missing;
            }
            SDL_UnlockSurface(composed.get());
            SDL_UnlockSurface(layer.get());
            require(wrong == 0 && missing == 0 && *std::min_element(pixels.begin(), pixels.end()) > 0,
                    "appearance: sandbags/helmets are neutral, use the wrong house ramp, or vanish on enemy rendering");
            if(zoom == 1) save(composed.get(), std::string("garrison-house-colour-")
                               + (zone == enemy ? "enemy" : zone == hospital ? "captured-hospital" : "own"));
            if(zone == enemy && zoom == 1) {
                zone->setFogged(true);
                clear(); zone->blitToScreen(); auto fogged = renderReadSurface(renderer);
                clear(); zone->drawZoneImage(); auto plain = renderReadSurface(renderer);
                require(fogged && plain, "appearance: fog readback failed");
                SDL_LockSurface(fogged.get()); SDL_LockSurface(plain.get());
                bool equal = true;
                for(int y = 0; y < plain->h; ++y) for(int x = 0; x < plain->w; ++x)
                    equal = equal && rgba(fogged.get(), x, y) == rgba(plain.get(), x, y);
                SDL_UnlockSurface(plain.get()); SDL_UnlockSurface(fogged.get());
                require(equal, "appearance: enemy garrison overlay leaked through fog");
                zone->setFogged(false);
            }
        }
    }
    SDL_SetRenderTarget(renderer, restore.target);
    SDL_RenderSetLogicalSize(renderer, restore.width, restore.height);
    currentZoomlevel = restore.zoom;
    screenborder->adjustScreenBorderToMapsize(kMapSize, kMapSize);
    selectZone(f, hospital); // actual click selects the same captured hospital as Stefan's screenshot
    struct Size { int width, height, scale; };
    for(int skin : {0, 1, 2}) for(const auto size : {Size{640, 480, 1}, Size{960, 600, 2}, Size{960, 600, 3}}) {
        MenuTheme::setUISkin(skin);
        GUIStyle::setGUIStyle(std::make_unique<DuneStyle>(0, skin));
        sdl2::texture_ptr panelTarget(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_TARGET, size.width * size.scale, size.height * size.scale));
        require(panelTarget != nullptr, "appearance: cannot allocate retina panel target");
        SDL_SetRenderTarget(renderer, panelTarget.get());
        SDL_RenderSetLogicalSize(renderer, size.width, size.height);
        std::unique_ptr<ZoneStructureInterface> panel(ZoneStructureInterface::create(hospital->getObjectID()));
        require(panel->update(), "appearance: captured hospital panel update failed");
        const auto& label = panel->garrisonLabel;
        require(label.getText().find("1/5") != std::string::npos
                    && label.getText().find(_("(captured)")) != std::string::npos,
                "appearance: captured panel lost the count5 denominator or captured status");
        const auto lines = greedyWordWrap(label.getText(), label.getSize().x, [&](const std::string& text) {
            return GUIStyle::getInstance().getMinimumLabelSize(text, label.getTextFontSize()).x - 4;
        });
        require(lines.size() == 1 && label.getMinimumSize().x <= label.getSize().x
                    && label.getMinimumSize().y <= label.getSize().y,
                "appearance: captured status wraps or clips in this skin/scale");
        int y = 80, labelBottom = -1, buttonTop = -1;
        for(const auto& row : panel->textVBox.containedWidgets) {
            if(row.pWidget == &panel->garrisonLabel) labelBottom = y + row.pWidget->getSize().y;
            if(row.pWidget == &panel->evacuateButton) buttonTop = y;
            y += row.pWidget->getSize().y;
        }
        require(labelBottom > 0 && buttonTop >= labelBottom + 2
                    && panel->textVBox.getMinimumSize().y <= size.height - 228,
                "appearance: captured status overlaps Evacuate or spills past the compact sidebar");
        setRenderDrawColor(renderer, MenuTheme::background); SDL_RenderClear(renderer);
        panel->draw(Point(0, 0));
        auto image = renderReadSurface(renderer);
        SDL_SetRenderTarget(renderer, restore.target);
        SDL_RenderSetLogicalSize(renderer, restore.width, restore.height);
        require(image != nullptr, "appearance: retina panel readback failed");
        auto sidebar = getSubPicture(image.get(), 0, 0, (SIDEBARWIDTH - 25) * size.scale,
                                     (size.height - 148) * size.scale);
        require(sidebar != nullptr, "appearance: cannot crop retina sidebar");
        save(sidebar.get(), "garrison-captured-panel-skin" + std::to_string(skin) + "-"
             + std::to_string(size.width) + "x" + std::to_string(size.height) + "-scale" + std::to_string(size.scale));
    }
    record("appearance", "own/enemy/captured sandbags+helmets use remapped house ramps at all zooms; fog stays private; "
           "captured Hospital status1/5 fits all3 skins at640x480 and Air960x600 2x/3x with2px Evacuate clearance");
}
