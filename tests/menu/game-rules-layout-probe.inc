// Included inside ZonePowerProbe. Tests the actual rules form and its real
// window-level wheel/click routing, not a reduced model of the checkbox list.
void checkRulesLayout(const SettingsClass::GameOptionsClass& initial) {
    struct Restore {
        SDL_Texture* target = SDL_GetRenderTarget(renderer);
        int width = getRendererWidth(), height = getRendererHeight(), skin = MenuTheme::uiSkin;
        std::string mod = ModManager::instance().getActiveModName();
        std::unique_ptr<GUIStyle> style = std::move(GUIStyle::currentGUIStyle);
        ~Restore() {
            SDL_SetRenderTarget(renderer, target);
            SDL_RenderSetLogicalSize(renderer, width, height);
            MenuTheme::setUISkin(skin);
            GUIStyle::setGUIStyle(std::move(style));
            ModManager::instance().setActiveMod(mod);
        }
    } restore;
    auto bounds = [](Widget& control, GameOptionsWindow& window) {
        Point origin = window.getPosition();
        for(Widget* child = &control; child != &window;) {
            Widget* parent = child->getParent();
            require(parent != nullptr, "Rules layout widget has no root");
            if(auto* box = dynamic_cast<HBox*>(parent)) origin = origin + box->getWidgetPosition(child);
            else if(auto* box = dynamic_cast<VBox*>(parent)) origin = origin + box->getWidgetPosition(child);
            else if(auto* scroll = dynamic_cast<ScrollView*>(parent))
                origin = origin + Point(0, -scroll->scrollbar.getCurrentValue());
            else require(parent == &window, "Rules layout has an unknown container");
            child = parent;
        }
        return SDL_Rect{origin.x, origin.y, control.getSize().x, control.getSize().y};
    };
    struct Size { int width, height, scale; };
    for(const char* mod : {"dunecity", "Tornie"}) {
        require(ModManager::instance().setActiveMod(mod), "Cannot activate rules layout test content");
        for(int skin : {0, 1, 2}) for(const auto size : {Size{640,480,1}, Size{960,600,2}, Size{960,600,3}}) {
            MenuTheme::setUISkin(skin);
            GUIStyle::setGUIStyle(std::make_unique<DuneStyle>(0, skin));
            sdl2::texture_ptr target(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                SDL_TEXTUREACCESS_TARGET, size.width * size.scale, size.height * size.scale));
            require(target != nullptr, "Cannot allocate rules layout offscreen target");
            SDL_SetRenderTarget(renderer, target.get());
            SDL_RenderSetLogicalSize(renderer, size.width, size.height);
            auto options = initial;
            GameOptionsWindow window(options);
            require(window.getSize().x <= size.width && window.getSize().y <= size.height,
                    "Rules layout window exceeds the viewport");
            require(window.vbox.getMinimumSize().x <= window.getSize().x
                        && window.vbox.getMinimumSize().y <= window.getSize().y,
                    "Rules layout content exceeds its window");
            const auto viewport = bounds(window.rulesScroll, window);
            const auto caption = bounds(window.captionlabel, window);
            const auto footer = bounds(window.okHBox, window);
            auto render = [&](const char* state) {
                setRenderDrawColor(renderer, MenuTheme::background);
                SDL_RenderClear(renderer);
                window.draw();
                auto image = renderReadSurface(renderer);
                require(image != nullptr, "Cannot read rules layout offscreen pixels");
                const auto path = std::string("game-rules-") + mod + "-skin" + std::to_string(skin)
                    + "-" + std::to_string(size.width) + "x" + std::to_string(size.height)
                    + "-scale" + std::to_string(size.scale) + "-" + state + ".png";
                require(SavePNG(image.get(), path.c_str()) == 0, "Cannot save rules layout screenshot");
                return image;
            };
            auto top = render("top");
            std::vector<Widget*> controls;
            auto collect = [&](auto&& self, Widget* widget) -> void {
                if(!widget->isVisible()) return;
                if(dynamic_cast<Checkbox*>(widget) || dynamic_cast<DropDownBox*>(widget)) controls.push_back(widget);
                else if(auto* box = dynamic_cast<HBox*>(widget))
                    for(const auto& row : box->containedWidgets) self(self, row.pWidget);
                else if(auto* box = dynamic_cast<VBox*>(widget))
                    for(const auto& row : box->containedWidgets) self(self, row.pWidget);
            };
            collect(collect, &window.hbox);
            require(controls.size() >= 24, "Rules layout probe did not discover the actual rule controls");
            int wheelEvents = 0, clicks = 0;
            for(auto* control : controls) {
                for(int attempts = 0; attempts < 100; ++attempts) {
                    const auto rect = bounds(*control, window);
                    if(rect.y >= viewport.y && rect.y + rect.h <= viewport.y + viewport.h) break;
                    require(window.handleMouseWheel(viewport.x + 8, viewport.y + viewport.h / 2, rect.y < viewport.y),
                            "Rules window did not route a required wheel event to the viewport");
                    ++wheelEvents;
                }
                const auto rect = bounds(*control, window);
                require(rect.x >= viewport.x && rect.x + rect.w <= viewport.x + window.rulesScroll.getContentWidth()
                            && rect.y >= viewport.y && rect.y + rect.h <= viewport.y + viewport.h,
                        "A rules control cannot be fully revealed inside its scroll viewport");
                require(control->getSize().x >= control->getMinimumSize().x
                            && control->getSize().y >= control->getMinimumSize().y,
                        "Rules scrolling clipped a control's text or height");
                if(auto* checkbox = dynamic_cast<Checkbox*>(control)) {
                    const bool before = checkbox->isChecked();
                    auto click = [&] {
                        const auto at = bounds(*checkbox, window);
                        window.handleMouseLeft(at.x + 5, at.y + 5, true);
                        window.handleMouseLeft(at.x + 5, at.y + 5, false);
                    };
                    click();
                    require(checkbox->isChecked() != before, "A visible rules checkbox did not receive its real window click");
                    require(window.vbox.getMinimumSize().x <= window.getSize().x
                                && window.vbox.getMinimumSize().y <= window.getSize().y,
                            "Enabling a numeric override overflowed the rules window");
                    click();
                    require(checkbox->isChecked() == before, "A rules checkbox could not be restored through its real click");
                    clicks += 2;
                }
            }
            // Finish at the very bottom using actual wheel events, never direct scrollbar mutation.
            for(int i = 0; i < 100 && window.rulesScroll.scrollbar.getCurrentValue() < window.rulesScroll.scrollbar.getRangeMax(); ++i) {
                require(window.handleMouseWheel(viewport.x + 8, viewport.y + viewport.h / 2, false),
                        "Rules viewport stopped accepting wheel events before its final row");
                ++wheelEvents;
            }
            require(window.rulesScroll.scrollbar.getCurrentValue() == window.rulesScroll.scrollbar.getRangeMax(),
                    "Rules viewport never reached its final row");
            if(window.hbox.getMinimumSize().y > window.rulesScroll.getSize().y)
                require(wheelEvents > 0, "Overflowing rule columns did not activate actual scrolling");
            auto bottom = render("bottom");
            SDL_LockSurface(top.get()); SDL_LockSurface(bottom.get());
            bool fixed = true;
            for(const auto rect : {caption, footer})
                for(int y = rect.y * size.scale; y < (rect.y + rect.h) * size.scale; ++y)
                    for(int x = rect.x * size.scale; x < (rect.x + rect.w) * size.scale; ++x)
                        fixed = fixed && getPixel(top.get(), x, y) == getPixel(bottom.get(), x, y);
            SDL_UnlockSurface(bottom.get()); SDL_UnlockSurface(top.get());
            require(fixed, "Scrolling rules changed or overpainted the fixed caption/confirmation controls");
            window.onOK();
            require(window.getGameOptions() == initial, "Rules layout interactions changed unrelated game options");
            SDL_Log("ZONE_POWER_RULES_SCROLL_PASS: mod=%s skin=%d logical=%dx%d scale=%d controls=%zu clicks=%d wheel=%d range=%d",
                mod, skin, size.width, size.height, size.scale, controls.size(), clicks, wheelEvents,
                window.rulesScroll.scrollbar.getRangeMax());
            SDL_SetRenderTarget(renderer, restore.target);
            SDL_RenderSetLogicalSize(renderer, restore.width, restore.height);
        }
    }
}
