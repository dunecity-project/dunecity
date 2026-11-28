// Included inside ZoneGarrisonProbe. Uses the real Capture widget, map selection,
// game damage/healing and Evacuate widget; SDL's dummy/software renderer needs no window.
void checkGarrisonPanel() {
    Fixture f("garrison panel");
    require(DuneCity::ZoneGarrison::kCapacity == 5, "panel: the five-occupant core change is missing");
    auto* zone = f.zone(f.own, Structure_ZoneResidential, 24, 24);
    const auto house = static_cast<HOUSETYPE>(f.own->getHouseID());
    pGFXManager->setDuneCityHouseGraphicsSkin(house, true);
    require(pGFXManager->isDuneCityHouseUsingDune2(house), "panel: Dune2 house skin was not selected");
    auto* panel = selectZone(f, zone);
    require(panel->garrisonRoster.count == 0 && !panel->garrisonRoster.isVisible(),
            "panel: an empty zone displays occupied slots");
    require(panel->update() && panel->objPicture.isVisible() && !panel->zonePreview.isVisible(),
            "panel: selected empty Dune2 zone does not show its house icon instead of the lot preview");
    require(panel->objPicture.pTexture.get() != nullptr
                && panel->objPicture.pTexture.get() == resolveItemPicture(zone->getItemID(), house),
            "panel: selected Dune2 zone does not use its actual house portrait");
    std::vector<UnitBase*> occupants;
    for(int i = 0; i < 5; ++i)
        occupants.push_back(f.unit(f.own, i % 2 ? Unit_Trooper : Unit_Soldier, 20, 22 + i));
    f.run(1);
    require(captureByPlayer(f, occupants, zone) == 5, "panel: Capture did not order all five men");
    require(f.runUntil(40, [&] { return zone->getGarrisonSize() == 5; }), "panel: five men did not enter");
    panel = selectZone(f, zone);

    auto checkModel = [&] {
        require(panel->update(), "panel: live update lost the selected zone");
        const auto& roster = panel->garrisonRoster;
        require(roster.isVisible() && roster.count == 5, "panel: model does not list five occupants");
        require(panel->garrisonLabel.getText().find("5/5") != std::string::npos,
                "panel: the full garrison label does not say 5/5");
        require(!panel->zonePreview.isVisible() && !panel->objPicture.isVisible(),
                "panel: a building portrait obscures the occupant icons");
        for(size_t i = 0; i < roster.count; ++i) {
            auto* unit = dynamic_cast<UnitBase*>(f.game->getObjectManager().getObject(zone->getGarrison()[i]));
            const auto& entry = roster.entries[i];
            require(unit && hidden(unit) && entry.objectID == unit->getObjectID()
                        && entry.itemID == unit->getItemID() && entry.health == unit->getHealth()
                        && entry.maxHealth == unit->getMaxHealth() && entry.healthColor == unit->getHealthColor(),
                    "panel: occupant identity/health is stale or out of entry order");
            require(entry.icon == resolveItemPicture(unit->getItemID(),
                        static_cast<HOUSETYPE>(unit->getProductionHouseID())) && entry.icon,
                    "panel: occupant does not use its own unit portrait");
        }
    };
    checkModel();

    // Wound by the production zone-hit path, not by assigning synthetic model HP.
    std::vector<FixPoint> before;
    for(const auto* unit : occupants) before.push_back(unit->getHealth());
    zone->handleDamage(30, NONE_ID, f.enemy);
    checkModel();
    std::vector<FixPoint> damaged;
    for(size_t i = 0; i < occupants.size(); ++i) {
        require(occupants[i]->getHealth() < before[i], "panel: damage never reached an occupant");
        damaged.push_back(occupants[i]->getHealth());
    }

    auto picture = [&](int width, int height, const std::string& state) {
        const int oldWidth = getRendererWidth(), oldHeight = getRendererHeight();
        SDL_Texture* oldTarget = SDL_GetRenderTarget(renderer);
        sdl2::texture_ptr target(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                                   SDL_TEXTUREACCESS_TARGET, width, height));
        require(target != nullptr, "panel: offscreen target allocation failed");
        require(SDL_SetRenderTarget(renderer, target.get()) == 0, "panel: cannot bind offscreen target");
        SDL_RenderSetLogicalSize(renderer, width, height);
        std::unique_ptr<ZoneStructureInterface> sized(ZoneStructureInterface::create(zone->getObjectID()));
        require(sized->update(), "panel: cannot update size-specific interface");
        setRenderDrawColor(renderer, COLOR_BLUE);
        SDL_RenderClear(renderer);
        sized->garrisonRoster.draw(Point(4, 24));
        auto image = renderReadSurface(renderer);
        SDL_SetRenderTarget(renderer, oldTarget);
        SDL_RenderSetLogicalSize(renderer, oldWidth, oldHeight);
        require(image != nullptr, "panel: cannot read offscreen pixels");
        require(sized->textVBox.getMinimumSize().y <= height - 80 - 148
                    && sized->textVBox.getMinimumSize().x <= SIDEBARWIDTH - 25,
                "panel: properties overflow the supported logical sidebar size");
        require(SDL_LockSurface(image.get()) == 0, "panel: cannot lock screenshot");
        auto pixelIs = [&](int x, int y, Uint32 color) {
            Uint8 r, g, b, a;
            SDL_GetRGBA(getPixel(image.get(), x, y), image->format, &r, &g, &b, &a);
            return r == ((color & RMASK) >> RSHIFT) && g == ((color & GMASK) >> GSHIFT)
                && b == ((color & BMASK) >> BSHIFT);
        };
        bool bars = true, icons = true, bounds = true;
        for(size_t i = 0; i < sized->garrisonRoster.count; ++i) {
            const auto rect = sized->garrisonRoster.slotRect(i, Point(4, 24));
            bounds = bounds && rect.x >= 4 && rect.x + rect.w <= SIDEBARWIDTH - 29
                && rect.y >= 24 && rect.y + rect.h <= 78;
            const auto& entry = sized->garrisonRoster.entries[i];
            const int fill = (entry.health * (rect.w - 4) / entry.maxHealth).lround();
            bars = bars && pixelIs(rect.x + 1, rect.y + rect.h - 5, COLOR_BLACK);
            for(int x = 0; x < rect.w - 4; ++x)
                bars = bars && pixelIs(rect.x + 2 + x, rect.y + rect.h - 4,
                                      x < fill ? entry.healthColor : COLOR_BLACK);
            bool painted = false;
            for(int y = rect.y; y < rect.y + rect.h - 6; ++y)
                for(int x = rect.x + 1; x < rect.x + rect.w - 1; ++x)
                    painted = painted || !pixelIs(x, y, COLOR_BLUE);
            icons = icons && painted;
        }
        SDL_UnlockSurface(image.get());
        require(bars && icons && bounds, "panel: icons/individual health pixels missing or outside portrait area");
        require(SDL_SetRenderTarget(renderer, target.get()) == 0, "panel: cannot bind panel capture");
        SDL_RenderSetLogicalSize(renderer, width, height);
        setRenderDrawColor(renderer, COLOR_BLACK);
        SDL_RenderClear(renderer);
        sized->draw(Point(0, 0));
        image = renderReadSurface(renderer);
        SDL_SetRenderTarget(renderer, oldTarget);
        SDL_RenderSetLogicalSize(renderer, oldWidth, oldHeight);
        require(image != nullptr, "panel: cannot capture full panel");
        const std::string file = gOut + "/garrison-panel-" + state + "-"
            + std::to_string(width) + "x" + std::to_string(height) + ".png";
        require(SavePNG(image.get(), file.c_str()) == 0, "panel: cannot save full panel screenshot");
    };
    picture(640, 480, "damaged");
    picture(1024, 768, "damaged");
    f.run(12);
    checkModel();
    for(size_t i = 0; i < occupants.size(); ++i)
        require(occupants[i]->getHealth() > damaged[i], "panel: normal garrison healing never raised occupant HP");
    picture(640, 480, "healed");

    // Enemy inspection keeps the existing generic/owner-name privacy gate. Hidden
    // occupants do not become selectable or disclose their health through this panel.
    auto* enemyZone = f.zone(f.enemy, Structure_ZoneResidential, 44, 44);
    auto* defender = f.unit(f.enemy, Unit_Trooper, 43, 43);
    f.capture(defender, enemyZone);
    require(f.runUntil(20, [&] { return enemyZone->getGarrisonSize() == 1; }), "panel: no enemy garrison formed");
    clickMap(f, enemyZone->getLocation());
    f.game->pInterface->updateObjectInterface();
    require(dynamic_cast<ZoneStructureInterface*>(f.game->pInterface->pObjectContainer) == nullptr
                && dynamic_cast<DefaultObjectInterface*>(f.game->pInterface->pObjectContainer) != nullptr,
            "panel: enemy inspection newly exposes its hidden roster");

    panel = selectZone(f, zone);
    press(panel->evacuateButton);
    require(f.runUntil(5, [&] { return !zone->isGarrisoned(); }), "panel: Evacuate did not remove the roster");
    require(panel->update() && panel->garrisonRoster.count == 0 && !panel->garrisonRoster.isVisible(),
            "panel: stale slots remain after Evacuate");
    require(panel->objPicture.isVisible() && !panel->zonePreview.isVisible()
                && panel->objPicture.pTexture.get() == resolveItemPicture(zone->getItemID(), house),
            "panel: Evacuate did not restore the selected Dune2 zone's actual house icon");
    record("panel", "selected empty Dune2 zone shows actual house icon; five real Capture occupants replace it; "
           "live per-unit damage/heal HP; 640x480/1024x768 offscreen icons and bars; enemy inspection private; "
           "Evacuate clears roster and restores house icon");
}
