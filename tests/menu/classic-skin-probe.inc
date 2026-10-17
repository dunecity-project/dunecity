// Diagnostic-only entry point. A private main links the production menu/HUD
// objects; this fixture is never included by the shipping executable.
#include <Menu/MainMenu.h>
#include <Menu/OptionsMenu.h>
#include <Menu/MentatMenu.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/FileManager.h>
#include <FileClasses/FontManager.h>
#include <FileClasses/TTFFont.h>
#include <FileClasses/LoadSavePNG.h>
#include <GUI/dune/DuneStyle.h>
#include <GUI/dune/InGameMenu.h>
#include <Game.h>
#include <GameInterface.h>
#include <GameInitSettings.h>
#include <House.h>
#include <Map.h>
#include <ScreenBorder.h>
#include <Tile.h>
#include <structures/StructureBase.h>
#include <mod/ModManager.h>
#include <misc/MenuLayout.h>
#include <misc/MenuPalette.h>
#include <misc/draw_util.h>
#include <config.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct ClassicUISkinProbe {
    static void require(bool condition, const char* message) {
        if(!condition) {
            SDL_Log("CLASSIC_UI_SKIN_PROBE_FAIL: %s", message);
            throw std::runtime_error(message);
        }
    }

    static std::string readConfiguration() {
        std::ifstream input(getConfigFilepath(), std::ios::binary);
        require(static_cast<bool>(input), "The isolated configuration is missing");
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    static std::filesystem::path capturePath(const std::string& name) {
        const char* output = std::getenv("MENU_PROBE_OUT");
        require(output && *output, "Screenshot output directory is required");
        const auto path = std::filesystem::path(output) / (std::string(std::getenv("MENU_PROBE_WIDTH"))
            + "-" + std::to_string(MenuTheme::uiSkin) + "-" + name + ".png");
        return path;
    }

    static void saveCapture(const std::string& name) {
        // SDL's dummy renderer still exercises the actual textures, widget
        // geometry and text. This is not a hand-drawn UI concept.
        const auto image = renderReadSurface(renderer);
        require(image != nullptr, "Reading the actual SDL frame failed");
        const auto path = capturePath(name).string();
        require(SavePNG(image.get(), path.c_str()) == 0, "Saving the UI screenshot failed");
        SDL_Log("CLASSIC_UI_SKIN_CAPTURE: %s", path.c_str());
    }

    static void capture(Window& menu, const std::string& name) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        menu.draw();
        menu.drawOverlay();
        saveCapture(name);
    }

    static int solidTextPixels(SDL_Surface* surface, Uint32 expected) {
        const auto color = RGBA2SDL(expected);
        sdl2::surface_lock lock{surface};
        int count = 0;
        for(int y = 0; y < surface->h; ++y) {
            for(int x = 0; x < surface->w; ++x) {
                SDL_Color pixel{};
                SDL_GetRGBA(getPixel(surface, x, y), surface->format,
                            &pixel.r, &pixel.g, &pixel.b, &pixel.a);
                if(pixel.a >= 250 && pixel.r == color.r && pixel.g == color.g && pixel.b == color.b)
                    ++count;
            }
        }
        return count;
    }

    static Uint32 rgbaPixel(SDL_Surface* surface, int x, int y) {
        SDL_Color pixel{};
        SDL_GetRGBA(getPixel(surface, x, y), surface->format,
                    &pixel.r, &pixel.g, &pixel.b, &pixel.a);
        return COLOR_RGBA(pixel.r, pixel.g, pixel.b, pixel.a);
    }

    static bool sameSurfacePixels(SDL_Surface* first, SDL_Surface* second) {
        if(!first || !second || first->w != second->w || first->h != second->h) return false;
        sdl2::surface_lock firstLock{first};
        sdl2::surface_lock secondLock{second};
        for(int y = 0; y < first->h; ++y)
            for(int x = 0; x < first->w; ++x)
                if(rgbaPixel(first, x, y) != rgbaPixel(second, x, y)) return false;
        return true;
    }

    static void checkDisplayTypography() {
        require(pFontManager && pFileManager,
                "Display font checks require the actual runtime font and file managers");
        const int activeSkin = MenuTheme::uiSkin;
        const auto activeBackground = MenuTheme::background;
        auto fontFile = pFileManager->openFile("Righteous-Regular.ttf");
        require(fontFile && SDL_RWsize(fontFile.get()) > 1024,
                "The selected Classic display typeface is absent from runtime assets");
        TTFFont coverage(std::move(fontFile), 18, 80);
        require(coverage.canRenderText(u8"CAMPAIGN ÉDITEURS"),
                "The display face lost basic or accented Latin glyph coverage");
        require(!coverage.canRenderText(u8"\u041a\u0410\u041c\u041f\u0410\u041d\u0418\u042f"),
                "The expected unsupported-script fallback no longer exercises a missing glyph");
        for(const auto& invalid : std::vector<std::string>{
            "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\x80", "\xC2", std::string("A\0B", 3)})
            require(!coverage.canRenderText(invalid),
                    "Display glyph coverage accepted malformed, overlong, surrogate or NUL text");
        DuneStyle modern(0, 0);
        auto modernBefore = modern.createButtonSurface(280, 34, "Play Online", false, false);
        const std::string sample = "CAMPAIGN WWW";
        for(unsigned int size : {14u, 18u, 24u}) {
            const int defaultWidth = pFontManager->getTextWidth(sample, size);
            const int defaultHeight = pFontManager->getTextHeight(size);
            auto defaultBefore = pFontManager->createSurfaceWithText(sample, COLOR_WHITE, size);
            const int displayWidth = pFontManager->getDisplayTextWidth(sample, size);
            const int displayHeight = pFontManager->getDisplayTextHeight(sample, size);
            auto displayBefore = pFontManager->createDisplaySurfaceWithText(sample, COLOR_WHITE, size);
            require(defaultBefore && displayBefore, "Rendering the actual display font failed");
            require(displayWidth > 0 && displayHeight > 0 && displayWidth != defaultWidth,
                    "Classic display metrics silently fell back to the default typeface");
            require(displayBefore->w == displayWidth && displayBefore->h == displayHeight,
                    "Classic display measurement and rendering disagree");
            require(defaultBefore->w == defaultWidth && defaultBefore->h == defaultHeight,
                    "Default font measurement and rendering disagree");
            require(!sameSurfacePixels(defaultBefore.get(), displayBefore.get()),
                    "The actual Classic display glyphs still use the original font");
            auto defaultAfter = pFontManager->createSurfaceWithText(sample, COLOR_WHITE, size);
            auto displayAfter = pFontManager->createDisplaySurfaceWithText(sample, COLOR_WHITE, size);
            require(pFontManager->getTextWidth(sample, size) == defaultWidth
                    && pFontManager->getTextHeight(size) == defaultHeight
                    && sameSurfacePixels(defaultBefore.get(), defaultAfter.get())
                    && sameSurfacePixels(displayBefore.get(), displayAfter.get()),
                    "Display and default typefaces share a contaminated size/render cache");

            // Righteous has no Cyrillic. A whole-label fallback must preserve
            // both metrics and pixels, not mix faces or emit missing boxes.
            const std::string unsupported = u8"\u041a\u0410\u041c\u041f\u0410\u041d\u0418\u042f";
            auto fallback = pFontManager->createDisplaySurfaceWithText(unsupported, COLOR_WHITE, size);
            auto original = pFontManager->createSurfaceWithText(unsupported, COLOR_WHITE, size);
            require(pFontManager->getDisplayTextWidth(unsupported, size)
                        == pFontManager->getTextWidth(unsupported, size)
                    && pFontManager->getDisplayTextHeight(unsupported, size)
                        == pFontManager->getTextHeight(size)
                    && sameSurfacePixels(fallback.get(), original.get()),
                    "Unsupported translations no longer retain the complete original-font label");
        }
        auto modernAfter = modern.createButtonSurface(280, 34, "Play Online", false, false);
        require(sameSurfacePixels(modernBefore.get(), modernAfter.get()),
                "Loading the Classic display typeface changes an existing Modern control");
        require(MenuTheme::uiSkin == activeSkin && MenuTheme::background == activeBackground,
                "Resolving display fonts mutated the active UI theme");
        SDL_Log("CLASSIC_UI_DISPLAY_FONT_PASS: runtime Righteous asset, strict UTF-8/Latin coverage, distinct actual metrics/glyphs;"
                " independent default/display caches; complete Cyrillic fallback; unchanged Modern");
    }

    static void checkButtonInterior(SDL_Surface* surface, bool classic, bool pressed) {
        require(surface != nullptr, "A diagnostic button surface is missing");
        sdl2::surface_lock lock{surface};
        std::set<Uint32> interior;
        const auto modern = MenuTheme::themeForUISkin(0);
        for(int y = 0; y < surface->h; ++y) {
            for(int x = 0; x < surface->w; ++x) {
                const auto color = rgbaPixel(surface, x, y);
                require(RGBA2SDL(color).a == 255, "Button decoration leaves a transparent pixel");
                if(x >= 6 && y >= 6 && x < surface->w - 6 && y < surface->h - 6) {
                    interior.insert(color);
                    if(!classic)
                        require(color == (pressed ? modern.pressed : modern.control),
                                "The Modern button interior is no longer its original flat color");
                }
            }
        }
        require(classic ? interior.size() >= 8 : interior.size() == 1,
                "Classic button interior is flat or Modern acquired desert decoration");
    }

    static void checkButtonLabel(SDL_Surface* blank, SDL_Surface* labeled,
                                 Uint32 foreground, Uint32 shadow, bool classic) {
        require(blank && labeled && blank->w == labeled->w && blank->h == labeled->h,
                "Labeled button geometry differs from its empty-label surface");
        sdl2::surface_lock blankLock{blank};
        sdl2::surface_lock labeledLock{labeled};
        int foregroundPixels = 0;
        int shadowPixels = 0;
        int changedPixels = 0;
        for(int y = 0; y < labeled->h; ++y) {
            for(int x = 0; x < labeled->w; ++x) {
                const auto color = rgbaPixel(labeled, x, y);
                if(color == rgbaPixel(blank, x, y)) continue;
                ++changedPixels;
                // The text-safe grain region itself follows label width. Only
                // exact ink/shadow pixels identify glyph bounds; other changes
                // against an empty button are background, not escaped text.
                if(color == foreground || (classic && color == shadow)) {
                    require(x >= 3 && y >= 3 && x < labeled->w - 3 && y < labeled->h - 3,
                            "A button label or its shadow overlaps the carved border");
                    if(color == foreground) ++foregroundPixels;
                    if(classic && color == shadow) ++shadowPixels;
                }
            }
        }
        require(changedPixels > 0 && foregroundPixels > 0,
                "The real font produced no visible button foreground glyphs");
        if(classic)
            require(shadowPixels > 0, "Classic Lapis typography lost its pale-gold offset relief");
    }

    static void checkButtonTypeface(SDL_Surface* button, const std::string& text,
                                    bool classic, bool pressed, Uint32 foreground) {
        std::string label = text;
        if(classic)
            for(char& character : label)
                if(character >= 'a' && character <= 'z') character -= 'a' - 'A';
        const bool display = classic && button->h >= 28;
        int size = button->h >= 48 ? 22 : button->h >= 38 ? 20
            : button->h >= 30 ? 18 : button->h >= 24 ? 16 : 14;
        if(display) size += 4;
        const auto width = [&](int value) {
            return display ? pFontManager->getDisplayTextWidth(label, value)
                           : pFontManager->getTextWidth(label, value);
        };
        const auto height = [&](int value) {
            return display ? pFontManager->getDisplayTextHeight(label, value)
                           : pFontManager->getTextHeight(value);
        };
        while(size > 8 && (button->w < width(size) + 12 || button->h < height(size) + 4)) --size;
        auto expected = display
            ? pFontManager->createDisplaySurfaceWithText(label, foreground, size)
            : pFontManager->createSurfaceWithText(label, foreground, size);
        require(expected != nullptr, "Rendering the independently measured button glyphs failed");
        const auto position = calcDrawingRect(expected.get(), button->w / 2 + (pressed ? 1 : 0),
            button->h / 2 + (pressed ? 1 : 0), HAlign::Center, VAlign::Center);
        int matchingPixels = 0;
        sdl2::surface_lock expectedLock{expected.get()};
        sdl2::surface_lock buttonLock{button};
        for(int y = 0; y < expected->h; ++y) {
            for(int x = 0; x < expected->w; ++x) {
                if(rgbaPixel(expected.get(), x, y) != foreground) continue;
                require(position.x + x >= 0 && position.y + y >= 0
                        && position.x + x < button->w && position.y + y < button->h,
                        "Measured button glyphs extend outside the actual control");
                require(rgbaPixel(button, position.x + x, position.y + y) == foreground,
                        "Actual button uses the wrong typeface, casing, metrics or foreground");
                ++matchingPixels;
            }
        }
        require(matchingPixels > 0, "The measured button typeface contains no solid foreground pixels");
    }

    static void checkButtonDecoration() {
        require(pFontManager && pGFXManager,
                "Button checks require actual game fonts and original desert artwork");
        const int activeSkin = MenuTheme::uiSkin;
        const auto activeBackground = MenuTheme::background;
        auto sheet = sdl2::surface_ptr{SDL_CreateRGBSurfaceWithFormat(0, 640, 240, 32, SCREEN_FORMAT)};
        require(sheet != nullptr, "Creating the diagnostic button state sheet failed");
        // Every sample below comes from production DuneStyle. The sheet simply
        // places the returned surfaces side by side; it is not a UI mockup.
        for(int skin : {0, 1}) {
            DuneStyle standard(0, skin);
            auto backdrop = standard.createBackground(320, 240);
            require(backdrop != nullptr, "Creating the actual button backdrop failed");
            SDL_Rect destination{skin * 320, 0, 320, 240};
            require(SDL_BlitSurface(backdrop.get(), nullptr, sheet.get(), &destination) == 0,
                    "Placing the diagnostic backdrop failed");
            auto heading = standard.createLabelSurface(300, 24,
                {skin ? "Lapis Enamel" : "Modern"}, 14,
                static_cast<Alignment_Enum>(Alignment_HCenter | Alignment_VCenter));
            require(heading != nullptr, "Rendering the button state sheet heading failed");
            destination = {skin * 320 + 10, 0, 300, 24};
            require(SDL_BlitSurface(heading.get(), nullptr, sheet.get(), &destination) == 0,
                    "Placing the diagnostic heading failed");
            for(int palette : {0, 1}) {
                DuneStyle style(palette, skin);
                for(int state = 0; state < 3; ++state) {
                    const bool pressed = state == 1;
                    const bool focused = state == 2;
                    const auto typography = buttonPalette(palette, skin, pressed);
                    for(int compact : {0, 1}) {
                        const int width = compact ? 70 : 280;
                        const int height = compact ? 22 : 34;
                        const std::string text = compact ? "Apply" : "Play Online";
                        auto blank = style.createButtonSurface(width, height, "", pressed, focused);
                        auto labeled = style.createButtonSurface(width, height, text, pressed, focused);
                        require(blank && labeled, "Rendering an actual button state failed");
                        require(blank->w == width && blank->h == height,
                                "Button decoration changed its requested hitbox dimensions");
                        checkButtonInterior(blank.get(), skin == 1, pressed);
                        checkButtonLabel(blank.get(), labeled.get(), typography.foreground,
                                         typography.shadow, skin == 1);
                        checkButtonTypeface(labeled.get(), text, skin == 1, pressed,
                                            typography.foreground);
                        if(palette == 0) {
                            destination = {skin * 320 + (compact ? 125 : 20),
                                           28 + state * 70 + (compact ? 39 : 0), width, height};
                            require(SDL_BlitSurface(labeled.get(), nullptr, sheet.get(), &destination) == 0,
                                    "Placing an actual button state on the evidence sheet failed");
                        }
                    }
                }
                auto normal = style.createButtonSurface(280, 34, "", false, false);
                auto pressed = style.createButtonSurface(280, 34, "", true, false);
                auto focused = style.createButtonSurface(280, 34, "", false, true);
                require(normal && pressed && focused, "Rendering the button state comparison failed");
                require(rgbaPixel(normal.get(), 0, 0) != rgbaPixel(focused.get(), 0, 0),
                        "Focused button no longer has a distinct outer border");
                require(rgbaPixel(normal.get(), 140, 10) != rgbaPixel(pressed.get(), 140, 10),
                        "Pressed button no longer has a distinct interior relief");
            }
        }
        require(MenuTheme::uiSkin == activeSkin && MenuTheme::background == activeBackground,
                "Button previews mutated the active menu/HUD theme");
        const auto path = capturePath("buttons-modern-classic-states").string();
        require(SavePNG(sheet.get(), path.c_str()) == 0, "Saving button-state pixel evidence failed");
        SDL_Log("CLASSIC_UI_BUTTON_PIXELS_PASS: opaque textured Classic interiors, Lapis glyphs and pale-gold relief;"
                " normal/pressed/focused at 280x34 and 70x22; unchanged flat Modern interiors;"
                " bounds, both text palettes and preview isolation");
    }

    static void checkArtworkLabelContrast() {
        require(pFontManager != nullptr, "Label pixel checks require the actual loaded game font");
        const int activeSkin = MenuTheme::uiSkin;
        const Uint32 activeBackground = MenuTheme::background;
        DuneStyle classic(0, 1);
        DuneStyle modern(0, 0);
        const auto alignment = static_cast<Alignment_Enum>(Alignment_Left | Alignment_VCenter);
        const std::vector<std::string> text{"MMMM WWW HUD"};
        auto overArtwork = classic.createLabelSurface(260, 40, text, 20, alignment,
            COLOR_WHITE, COLOR_BLACK, COLOR_TRANSPARENT);
        auto overGold = classic.createLabelSurface(260, 40, text, 20, alignment,
            COLOR_WHITE, COLOR_TRANSPARENT, COLOR_TRANSPARENT);
        auto modernWhite = modern.createLabelSurface(260, 40, text, 20, alignment,
            COLOR_WHITE, COLOR_TRANSPARENT, COLOR_TRANSPARENT);
        require(overArtwork && overGold && modernWhite, "Rendering label surfaces failed");
        require(solidTextPixels(overArtwork.get(), COLOR_WHITE) > 0,
                "Classic white labels over original artwork were incorrectly darkened");
        require(solidTextPixels(overArtwork.get(), COLOR_BLACK) > 0,
                "Classic artwork labels lost their explicitly requested black outline");
        const auto goldText = MenuTheme::readableText(COLOR_WHITE, 1);
        require(goldText != COLOR_WHITE && solidTextPixels(overGold.get(), goldText) > 0,
                "Classic labels on gold do not use the contrast-corrected dark glyphs");
        require(solidTextPixels(overGold.get(), COLOR_WHITE) == 0,
                "The gold-surface label incorrectly keeps unreadable white glyphs");
        const auto gold = MenuTheme::themeForUISkin(1);
        require(MenuTheme::textContrast(goldText, gold.background) >= 4.5
                && MenuTheme::textContrast(goldText, gold.control) >= 4.5
                && MenuTheme::textContrast(goldText, gold.pressed) >= 4.5,
                "Classic label glyphs fail the golden surfaces' contrast requirement");
        require(solidTextPixels(modernWhite.get(), COLOR_WHITE) > 0,
                "Modern label rendering no longer preserves its existing white text");
        require(MenuTheme::uiSkin == activeSkin && MenuTheme::background == activeBackground,
                "Local DuneStyle label instances mutated the active UI theme");
        for(const auto& sample : {std::pair<SDL_Surface*, std::string>{overArtwork.get(), "label-artwork-white"},
                                   {overGold.get(), "label-gold-dark"},
                                   {modernWhite.get(), "label-modern-white"}}) {
            const auto path = capturePath(sample.second).string();
            require(SavePNG(sample.first, path.c_str()) == 0, "Saving label pixel evidence failed");
        }
        SDL_Log("CLASSIC_UI_LABEL_PIXELS_PASS: Classic artwork white/black, contrast-corrected gold"
                " labels, unchanged Modern white, and preview-theme isolation");
    }

    static Point widgetPosition(Widget& widget, OptionsMenu& menu) {
        Point result;
        Widget* current = &widget;
        while(current != &menu) {
            auto* parent = current->getParent();
            require(parent != nullptr, "An Options control has no parent");
            Point local;
            if(auto* container = dynamic_cast<StaticContainer*>(parent)) local = container->getWidgetPosition(current);
            else if(auto* container = dynamic_cast<HBox*>(parent)) local = container->getWidgetPosition(current);
            else if(auto* container = dynamic_cast<VBox*>(parent)) local = container->getWidgetPosition(current);
            else if(auto* window = dynamic_cast<Window*>(parent)) local = window->getPosition();
            else require(false, "Unknown Options container type");
            require(local.x >= 0 && local.y >= 0, "An Options control has negative geometry");
            result = result + local;
            current = parent;
        }
        return result;
    }

    static void checkGraphicsBounds(OptionsMenu& menu) {
        menu.showPage(1);
        require(menu.getSize().x == getRendererWidth() && menu.getSize().y == getRendererHeight(),
                "Options does not match the requested logical resolution");
        for(Widget* control : {static_cast<Widget*>(&menu.uiSkinDropDownBox),
                               static_cast<Widget*>(&menu.palettePreview),
                               static_cast<Widget*>(&menu.startMenuModeDropDownBox),
                               static_cast<Widget*>(&menu.paletteDropDownBox),
                               static_cast<Widget*>(&menu.interfaceSizeDropDownBox),
                               static_cast<Widget*>(&menu.zoomlevelDropDownBox),
                               static_cast<Widget*>(&menu.acceptButton),
                               static_cast<Widget*>(&menu.backButton)}) {
            require(control->isVisible(), "An appearance/display control is hidden");
            const auto position = widgetPosition(*control, menu);
            const auto size = control->getSize();
            require(size.x > 0 && size.y > 0, "An appearance/display control has no click area");
            require(position.x + size.x <= menu.getSize().x && position.y + size.y <= menu.getSize().y,
                    "An appearance/display control extends outside the screen");
        }
        require(menu.pages[1].getSize().y >= menu.pages[1].getMinimumSize().y,
                "Graphics rows overlap the Apply/Back controls at this size");
        require(menu.uiSkinDropDownBox.getNumEntries() == 2
                && menu.uiSkinDropDownBox.getEntryIntData(0) == 0
                && menu.uiSkinDropDownBox.getEntryIntData(1) == 1,
                "Modern and Lapis Enamel choices are missing or misidentified");
        require(menu.uiSkinDropDownBox.getEntry(0) == _("Modern")
                && menu.uiSkinDropDownBox.getEntry(1) == _("Lapis Enamel"),
                "The Lapis Enamel label no longer maps to the existing saved UI skin ID 1");
        for(unsigned int index : {0u, 1u})
            require(menu.uiSkinDropDownBox.getEntry(index) != _("Classic Desert"),
                    "An unfinished Classic Desert option is still visible");
        require(menu.uiSkinDropDownBox.getSelectedEntryIntData() == settings.video.uiSkin,
                "Reopening Options lost the UI skin selection");
        require(menu.interfaceSizeDropDownBox.getSelectedEntryIntData() == settings.video.interfaceHeight,
                "UI skin switching changed the interface-size choice");
    }

    static void previewAndCancel(int target) {
        const auto configuration = readConfiguration();
        const int originalSkin = settings.video.uiSkin;
        const auto originalTheme = MenuTheme::themeForUISkin(originalSkin);
        const int originalHeight = settings.video.interfaceHeight;
        const int originalZoom = settings.video.preferredZoomLevel;
        const int originalBuildingSkin = settings.general.duneCityCampaignSkin;
        const auto originalMod = ModManager::instance().getActiveModName();
        OptionsMenu options;
        checkGraphicsBounds(options);
        options.uiSkinDropDownBox.setSelectedItem(target);
        options.onChangeOption(true);
        require(MenuTheme::uiSkin == originalSkin && MenuTheme::background == originalTheme.background,
                "The preview changes the live menu/HUD theme before Apply");
        require(settings.video.uiSkin == originalSkin, "Preview mutates the saved preference");
        require(readConfiguration() == configuration, "Preview writes the user's configuration");
        capture(options, "preview-other-skin");
        options.onOptionsCancel();
        require(options.isQuiting(), "Back did not close the appearance preview");
        require(readConfiguration() == configuration && settings.video.uiSkin == originalSkin,
                "Back applied the UI skin or changed the profile");
        require(settings.video.interfaceHeight == originalHeight
                && settings.video.preferredZoomLevel == originalZoom
                && settings.general.duneCityCampaignSkin == originalBuildingSkin
                && ModManager::instance().getActiveModName() == originalMod,
                "UI appearance preview changed graphics/world/mod preferences");
    }

    static void captureHUD() {
        // Optional bounded, offline real-game fixture. No AI ticks, paid artwork,
        // orders, saves or public network traffic. A verified immutable Vanilla
        // snapshot is also valid: game startup pins content automatically.
        require(currentGame == nullptr, "The diagnostic must not replace a live game");
        auto& mods = ModManager::instance();
        const auto originalMod = mods.getActiveModName();
        require(mods.getContentBase(originalMod) == "vanilla" && !mods.isCityModeActive(),
                "HUD probe requires verified isolated non-city Vanilla content");
        struct RestoreModSelection {
            std::string original;
            ~RestoreModSelection() {
                auto& manager = ModManager::instance();
                if(manager.getActiveModName() != original) {
                    try { manager.setActiveMod(original); }
                    catch(const std::exception& error) {
                        SDL_Log("CLASSIC_UI_SKIN_PROBE_RESTORE_FAIL: %s", error.what());
                    }
                }
            }
        } restoreMod{originalMod};
        auto rules = effectiveGameOptions;
        rules.fogOfWar = false;
        struct ResetGamePointers {
            ~ResetGamePointers() { currentGame = nullptr; pLocalPlayer = nullptr; pLocalHouse = nullptr; }
        } reset;
        auto game = std::make_unique<Game>();
        currentGame = game.get();
        // A Campaign init opens its interactive briefing. Reuse the original
        // bundled map bytes through the public CustomGame constructor instead;
        // CustomGame consumes mapdata rather than resolving an empty filename.
        require(pFileManager != nullptr, "The HUD fixture has no bundled file manager");
        auto scenarioFile = pFileManager->openCampaignFile("SCENH001.INI");
        require(scenarioFile != nullptr, "The bundled Harkonnen scenario is unavailable");
        const Sint64 scenarioSize = SDL_RWsize(scenarioFile.get());
        require(scenarioSize > 0 && scenarioSize <= 8 * 1024 * 1024,
                "The bounded HUD scenario has an invalid file size");
        std::string scenario(static_cast<size_t>(scenarioSize), '\0');
        require(SDL_RWread(scenarioFile.get(), scenario.data(), 1, scenario.size()) == scenario.size(),
                "Reading the bundled HUD scenario failed");
        GameInitSettings init("SCENH001.INI", scenario, false, rules);
        GameInitSettings::HouseInfo human(HOUSE_HARKONNEN, 1);
        human.addPlayerInfo({settings.general.playerName, "HumanPlayer"});
        init.addHouseInfo(human);
        game->initGame(init);
        require(!game->isCitySimEnabled(), "The Vanilla HUD fixture unexpectedly enabled city simulation");
        StructureBase* constructionYard = nullptr;
        for(auto* structure : structureList) {
            if(structure->getOwner() == pLocalHouse && structure->getItemID() == Structure_ConstructionYard) {
                constructionYard = structure;
                break;
            }
        }
        require(constructionYard != nullptr, "The bounded HUD scenario has no owned construction yard");
        game->getSelectedList().insert(constructionYard->getObjectID());
        game->selectAll(game->getSelectedList());
        game->selectionChanged();
        require(screenborder != nullptr, "The bounded HUD scenario has no world camera");
        screenborder->setNewScreenCenter(constructionYard->getLocation() * TILESIZE);
        require(game->getSelectedList().size() == 1, "The real construction-yard sidebar was not selected");
        {
            // initGame deliberately does not install Game's private loop HUD.
            // Construct the public component directly rather than entering
            // runMainLoop (which would advance simulation and process orders).
            // It must be destroyed before its referenced game/objects.
            GameInterface hud;
            hud.updateObjectInterface();
            auto drawStaticWorldAndHUD = [&] {
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderClear(renderer);
                const auto first = screenborder->getTopLeftTile();
                const auto last = screenborder->getBottomRightTile();
                const int x1 = std::max(0, first.x - 2);
                const int y1 = std::max(0, first.y - 2);
                const int x2 = std::min(currentGameMap->getSizeX(), last.x + 3);
                const int y2 = std::min(currentGameMap->getSizeY(), last.y + 3);
                auto drawTiles = [&](auto drawLayer) {
                    currentGameMap->for_each(x1, y1, x2, y2, [&](Tile& tile) {
                        (tile.*drawLayer)(screenborder->world2screenX(tile.getLocation().x * TILESIZE),
                                          screenborder->world2screenY(tile.getLocation().y * TILESIZE));
                    });
                };
                // Original map/entity drawing, without Game::drawScreen's
                // private pInterface dependency or the simulation loop.
                drawTiles(&Tile::blitGround);
                drawTiles(&Tile::blitStructures);
                drawTiles(&Tile::blitInfantry);
                drawTiles(&Tile::blitNonInfantryGroundUnits);
                drawTiles(&Tile::blitSelectionRects);
                hud.draw(Point(0,0));
                hud.drawDialogs();
            };
            drawStaticWorldAndHUD();
            saveCapture("selected-building-hud");
            {
                InGameMenu pause(false, PALCOLOR_HARKONNEN);
                drawStaticWorldAndHUD();
                pause.draw(Point(0,0));
                pause.drawOverlay();
                saveCapture("pause-over-hud");
            }
        }
        game.reset();
        currentGame = nullptr;
        pLocalPlayer = nullptr;
        pLocalHouse = nullptr;
        if(mods.getActiveModName() != originalMod)
            require(mods.setActiveMod(originalMod), "The HUD fixture could not restore its original mod selection");
        require(mods.getActiveModName() == originalMod, "HUD content pinning changed the appearance-loop mod choice");
    }

    static int run() {
        const char* profile = std::getenv("DUNECITY_USERDIR");
        require(profile && *profile, "UI probe requires an isolated user profile");
        require(std::filesystem::weakly_canonical(getConfigFilepath())
                == std::filesystem::weakly_canonical(std::filesystem::path(profile) / CONFIGFILENAME),
                "The profile override does not own the actual configuration path");
        require(std::getenv("MENU_PROBE_WIDTH") && std::getenv("MENU_PROBE_HEIGHT"),
                "UI probe requires explicit logical dimensions");
        require(getRendererWidth() == std::atoi(std::getenv("MENU_PROBE_WIDTH"))
                && getRendererHeight() == std::atoi(std::getenv("MENU_PROBE_HEIGHT")),
                "UI probe was silently clamped or scaled to different dimensions");
        const char* selected = std::getenv("MENU_PROBE_SKIN");
        const int initialSkin = validatedUISkin(selected ? std::atoi(selected) : 0);
        static int stage = 0;
        const int expectedSkin = stage == 1 ? 1 - initialSkin : initialSkin;
        require(settings.video.uiSkin == expectedSkin && MenuTheme::uiSkin == expectedSkin,
                "Video reinitialization did not activate the saved UI skin");
        require(MenuTheme::background == MenuTheme::themeForUISkin(expectedSkin).background,
                "Runtime menu colors do not match the selected style");
        INIFile saved(getConfigFilepath());
        require(validatedUISkin(saved.getIntValue("Video", "UI Skin", 0)) == expectedSkin,
                "Apply did not persist the UI skin in the isolated profile");

        if(stage < 2) {
            checkDisplayTypography();
            checkArtworkLabelContrast();
            checkButtonDecoration();
            MainMenu mainMenu;
            capture(mainMenu, "main");
            OptionsMenu options;
            checkGraphicsBounds(options);
            capture(options, "graphics-options");
            {
                MentatMenu mentat(HOUSE_HARKONNEN);
                mentat.setText("Original Mentat artwork retains readable white narration.");
                capture(mentat, "mentat-artwork-label");
            }
            if(std::getenv("MENU_PROBE_HUD")) captureHUD();
            previewAndCancel(1 - expectedSkin);
            const int originalHeight = settings.video.interfaceHeight;
            const int originalZoom = settings.video.preferredZoomLevel;
            const int originalBuildingSkin = settings.general.duneCityCampaignSkin;
            const int originalLayout = settings.video.startMenuMode;
            options.uiSkinDropDownBox.setSelectedItem(1 - expectedSkin);
            options.onChangeOption(true);
            options.onOptionsOK();
            require(options.isQuiting() && !options.hasChildWindow(), "UI Apply failed or opened an error");
            require(settings.video.uiSkin == 1 - expectedSkin, "Apply did not accept the chosen UI skin");
            require(settings.video.interfaceHeight == originalHeight
                    && settings.video.preferredZoomLevel == originalZoom
                    && settings.general.duneCityCampaignSkin == originalBuildingSkin
                    && settings.video.startMenuMode == originalLayout,
                    "Applying UI skin changed interface size, world zoom, building skin or menu layout");
            ++stage;
            return MENU_QUIT_REINITIALIZE;
        }
        OptionsMenu reopened;
        checkGraphicsBounds(reopened);
        capture(reopened, "graphics-restored");
        SDL_Log("CLASSIC_UI_SKIN_PROBE_PASS: actual Modern/Classic menu and Graphics rendering; bounds;"
                " independent preview and Back; Apply/reinitialization/persistence/reopening;"
                " unchanged interface size, zoom, building skin and navigation layout;"
                " actual label pixels and original Mentat artwork%s",
                std::getenv("MENU_PROBE_HUD")
                    ? "; static selected construction-yard build sidebar/HUD and pause rendering (no game loop)" : "");
        return MENU_QUIT_DEFAULT;
    }
};

int runClassicUISkinProbe() { return ClassicUISkinProbe::run(); }
