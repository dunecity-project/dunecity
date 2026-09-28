/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <main.h>
#include <Network/RelayWebSocket.h>

#include <algorithm>
#include <cmath>
#include <globals.h>

#include <config.h>

#include <FileClasses/FileManager.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/SFXManager.h>
#include <FileClasses/FontManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/Palfile.h>
#include <FileClasses/music/DirectoryPlayer.h>
#include <FileClasses/music/ADLPlayer.h>
#include <FileClasses/music/XMIPlayer.h>

#include <GUI/GUIStyle.h>
#include <GUI/dune/DuneStyle.h>

#include <Menu/MainMenu.h>
#include <misc/DesktopUpdater.h>
#include <Menu/OptionsMenu.h>

#include <misc/DiscordManager.h>
#include <CursorManager.h>

#include <misc/fnkdat.h>
#include <misc/FileSystem.h>
#include <misc/Scaler.h>
#include <misc/MenuLayout.h>
#include <misc/string_util.h>
#include <misc/exceptions.h>
#include <misc/format.h>
#include <misc/SDL2pp.h>
#include <misc/md5.h>
#include <misc/WebRuntime.h>

#include <players/QuantBotConfig.h>
#include <mod/ModManager.h>

#include <CrashHandler.h>
#include <SoundPlayer.h>

#include <mmath.h>

#include <CutScenes/Intro.h>

#include <SDL_ttf.h>

#include <iostream>
#include <typeinfo>
#include <future>
#include <array>
#include <ctime>
//#include <sys/types.h>
//#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>


#ifdef _WIN32
    #include <windows.h>
    #include <stdio.h>
    #include <io.h>
#else
    #include <sys/types.h>
    #include <pwd.h>
    #include <unistd.h>
#endif

#ifdef __APPLE__
#include <misc/MacFunctions.h>
#include <mach-o/dyld.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>

// The browser build has no command line, so the page URL is how a tester names a relay. Both
// helpers only read the query string; the value is validated by the admission client before it
// is used, and a plain loopback address still needs the explicit development opt-in.
EM_JS(void, dunecityReadRelayUrlParameter, (char* out, int maxLength), {
    var value = '';
    try {
        value = new URLSearchParams(location.search).get('relay') || '';
    } catch (error) {
        value = '';
    }
    if (value.length > maxLength - 1) {
        value = '';
    }
    stringToUTF8(value, out, maxLength);
});

EM_JS(int, dunecityReadRelayDevelopmentParameter, (), {
    try {
        return new URLSearchParams(location.search).get('relaydev') === '1' ? 1 : 0;
    } catch (error) {
        return 0;
    }
});
#endif

#if !defined(__GNUG__) || (defined(_GLIBCXX_HAS_GTHREADS) && defined(_GLIBCXX_USE_C99_STDINT_TR1) && (ATOMIC_INT_LOCK_FREE > 1) && !defined(_GLIBCXX_HAS_GTHREADS))
// g++ does not provide std::async on all platforms
#define HAS_ASYNC
#endif

#if defined( __clang__ ) || defined(__GNUG__) || defined( __GLIBCXX__ ) || defined( __GLIBCPP__ )
#include <cxxabi.h>
inline std::string demangleSymbol(const char* symbolname) {
    int status = 0;
    std::size_t size = 0;
    char* result = abi::__cxa_demangle(symbolname, nullptr, &size, &status);
    if(status != 0) {
        return std::string(symbolname);
    } else {
        std::string name = std::string(result);
        std::free(result);
        return name;
    }
}
#else
inline std::string demangleSymbol(const char* symbolname) {
    return std::string(symbolname);
}
#endif

void setVideoMode(int displayIndex);
void realign_buttons();

static void printUsage() {
    fprintf(stderr, "Usage:\n\tdunecity [--showlog] [--fullscreen|--window] [--PlayerName=X] [--ServerPort=X]\n"
                    "\t         [--RelayEndpoint=HTTPS_URL] [--RelayDevEndpoint=LOOPBACK_URL] [--RelayDev]\n");
}

int getLogicalToPhysicalResolutionFactor(int physicalWidth, int physicalHeight) {
    if(physicalWidth >= 1280*3 && physicalHeight >= 720*3) {
        return 3;
    } else if(physicalWidth >= 640*2 && physicalHeight >= 480*2) {
        return 2;
    } else {
        return 1;
    }
}

// Keeps a windowed size inside the usable desktop area (the display minus menu
// bar, dock or task bar) so the whole window is visible. Units are the screen
// coordinates SDL_CreateWindow takes.
static void clampWindowedSizeToDisplay(int displayIndex, int& width, int& height) {
    SDL_Rect usableBounds = {0, 0, 0, 0};
    if(SDL_GetDisplayUsableBounds(displayIndex, &usableBounds) != 0 || usableBounds.w <= 0 || usableBounds.h <= 0) {
        return;
    }
    width = std::min(width, usableBounds.w);
    height = std::min(height, usableBounds.h);
}

// Logical width that gives a `height`-tall interface the same shape as a
// presentedWidth x presentedHeight surface, so it fills the window without
// black bars. Even, and never below the minimum width.
static int interfaceWidthForShape(int height, int presentedWidth, int presentedHeight) {
    if(presentedWidth <= 0 || presentedHeight <= 0) {
        return interfaceWidthForHeight(height, false);
    }
    const int width = static_cast<int>(std::lround(static_cast<double>(height) * presentedWidth / presentedHeight));
    return std::max(SCREEN_MIN_WIDTH, width & ~1);
}

void setVideoMode(int displayIndex)
{
    int videoFlags = 0;
    const int requestedInterfaceHeight = validatedInterfaceHeight(
        settings.video.interfaceHeight,
#ifdef __ANDROID__
        true
#else
        false
#endif
    );
    [[maybe_unused]] const int requestedInterfaceWidth = requestedInterfaceHeight > 0
        ? validatedInterfaceWidth(settings.video.width, requestedInterfaceHeight)
        : settings.video.width;

    // Size of what ends up on screen, in screen coordinates: the window, or
    // the desktop for a fullscreen-desktop window.
    int presentedWidth = 0;
    int presentedHeight = 0;

#ifdef __EMSCRIPTEN__
    // The selected resolution is the backing surface; the browser shell fits
    // that surface into the available stage without changing its aspect ratio.
    // SDL_WINDOW_RESIZABLE would replace the requested backing resolution
    // with the CSS size at creation and on viewport resize.
    videoFlags = 0;
    settings.video.fullscreen = false;
    settings.video.physicalWidth = std::max(settings.video.physicalWidth, SCREEN_MIN_WIDTH);
    settings.video.physicalHeight = std::max(settings.video.physicalHeight, SCREEN_MIN_HEIGHT);
    presentedWidth = settings.video.physicalWidth;
    presentedHeight = settings.video.physicalHeight;
    const int factor = getLogicalToPhysicalResolutionFactor(presentedWidth, presentedHeight);
    settings.video.width = std::max(presentedWidth / factor, SCREEN_MIN_WIDTH);
    settings.video.height = std::max(presentedHeight / factor, SCREEN_MIN_HEIGHT);
#else
    if(settings.video.fullscreen) {
        videoFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
#ifdef __ANDROID__
    videoFlags |= SDL_WINDOW_RESIZABLE;
#else
    // Render at the display's native pixel density. On a Retina Mac a
    // 1440x900 window then has a 2880x1800 pixel surface, so a 960x600
    // interface is drawn at an exact 3x instead of an uneven 1.5x. Window
    // sizes stay in screen coordinates and SDL converts mouse events to the
    // logical size, so nothing else changes.
    videoFlags |= SDL_WINDOW_ALLOW_HIGHDPI;
#endif

    // The game never switches the display mode: fullscreen means
    // SDL_WINDOW_FULLSCREEN_DESKTOP, which covers the desktop at whatever
    // resolution it currently has, and a window may have any size. So the
    // requested physical size is used as-is; the only adjustment is keeping a
    // window inside the usable desktop area. Older versions snapped the request
    // to SDL's "closest display mode" here, which on Retina Macs turned
    // 1280x800 into 1920x1200 (SDL only considers low-density modes as
    // candidates), so changing the windowed resolution never took effect.
    settings.video.physicalWidth = std::max(settings.video.physicalWidth, SCREEN_MIN_WIDTH);
    settings.video.physicalHeight = std::max(settings.video.physicalHeight, SCREEN_MIN_HEIGHT);
    if(!settings.video.fullscreen) {
        clampWindowedSizeToDisplay(displayIndex, settings.video.physicalWidth, settings.video.physicalHeight);
    }

    {
        // Derive the logical (interface) size from what actually ends up on
        // screen: for a fullscreen-desktop window that is the desktop, not the
        // saved windowed size.
        presentedWidth = settings.video.physicalWidth;
        presentedHeight = settings.video.physicalHeight;
        SDL_DisplayMode desktopDisplayMode;
        if(settings.video.fullscreen
           && SDL_GetDesktopDisplayMode(displayIndex, &desktopDisplayMode) == 0
           && desktopDisplayMode.w > 0 && desktopDisplayMode.h > 0) {
            presentedWidth = desktopDisplayMode.w;
            presentedHeight = desktopDisplayMode.h;
        }
        int factor = getLogicalToPhysicalResolutionFactor(presentedWidth, presentedHeight);
        if(factor <= 0) {
            factor = 1;
        }
        settings.video.width = std::max(presentedWidth / factor, SCREEN_MIN_WIDTH);
        settings.video.height = std::max(presentedHeight / factor, SCREEN_MIN_HEIGHT);
    }

#ifdef __ANDROID__
    // Keep the game UI stable while Android replaces the physical surface
    // during fold, unfold, rotation, DeX, and multi-window transitions.
    settings.video.interfaceHeight = requestedInterfaceHeight;
    settings.video.width = requestedInterfaceWidth;
    settings.video.height = requestedInterfaceHeight;
#endif
#endif

    // Prefer Direct3D on Windows, let SDL choose best renderer on other platforms
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
#else
    // On non-Windows platforms, let SDL choose the best renderer
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // Use nearest-neighbor scaling for pixel-perfect look
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");       // Enable render batching for performance

    window = SDL_CreateWindow("DuneCity",
                              SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex), SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex),
                              settings.video.physicalWidth, settings.video.physicalHeight,
                              videoFlags);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    // Create renderer (VSync set separately for macOS compatibility)
    Uint32 rendererFlags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;
    
    renderer = SDL_CreateRenderer(window, -1, rendererFlags);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    {
        int windowWidth = 0;
        int windowHeight = 0;
        int pixelWidth = 0;
        int pixelHeight = 0;
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        SDL_GetRendererOutputSize(renderer, &pixelWidth, &pixelHeight);
        SDL_Log("Window: %dx%d requested, %dx%d created (%s), %dx%d pixels",
                settings.video.physicalWidth, settings.video.physicalHeight,
                windowWidth, windowHeight,
                settings.video.fullscreen ? "fullscreen desktop" : "windowed",
                pixelWidth, pixelHeight);
    }

#ifdef __ANDROID__
    // The Android display mode can describe the complete panel while the SDL
    // surface is a foldable, split-screen, or desktop-mode window. The renderer
    // output is the authoritative size after SDLActivity creates that surface.
    int outputWidth = 0;
    int outputHeight = 0;
    if(SDL_GetRendererOutputSize(renderer, &outputWidth, &outputHeight) == 0 &&
       outputWidth > 0 && outputHeight > 0) {
        settings.video.physicalWidth = outputWidth;
        settings.video.physicalHeight = outputHeight;
        SDL_Log("Android SDL window: %dx%d physical, %dx%d fixed logical",
                settings.video.physicalWidth, settings.video.physicalHeight,
                settings.video.width, settings.video.height);
    }
#endif
    
    // Browser frames are paced by the explicit Asyncify yield below. SDL's
    // web VSync hook expects emscripten_set_main_loop(), which this legacy
    // synchronous loop intentionally does not use.
#ifndef __EMSCRIPTEN__
    // Set VSync after renderer creation (works better on macOS Metal)
    if(settings.video.frameLimit) {
        SDL_RenderSetVSync(renderer, 1);
        SDL_Log("VSync enabled");
    } else {
        SDL_RenderSetVSync(renderer, 0);
        SDL_Log("VSync disabled");
    }
#endif
    if(settings.video.interfaceHeight > 0) {
        settings.video.height = settings.video.interfaceHeight;
#ifdef __ANDROID__
        settings.video.width = validatedInterfaceWidth(requestedInterfaceWidth, settings.video.height);
#else
        // Only the height is a preset; the width follows the shape of the
        // window (or desktop) so the interface fills it without black bars.
        settings.video.width = interfaceWidthForShape(settings.video.height, presentedWidth, presentedHeight);
#endif
    }
    SDL_Log("Display: %dx%d physical, %dx%d logical, interface preset=%d",
            settings.video.physicalWidth, settings.video.physicalHeight,
            settings.video.width, settings.video.height, settings.video.interfaceHeight);
    SDL_RenderSetLogicalSize(renderer, settings.video.width, settings.video.height);
    screenTexture = SDL_CreateTexture(renderer, SCREEN_FORMAT, SDL_TEXTUREACCESS_TARGET, settings.video.width, settings.video.height);

    // Check if texture creation failed
    if (!screenTexture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    // Enable hardware acceleration
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(screenTexture, SDL_ScaleModeNearest);

    applyCursorVisibilitySetting();
}

void toogleFullscreen()
{
    // Safety checks
    if(window == nullptr || renderer == nullptr) {
        SDL_Log("Warning: Cannot toggle fullscreen - window or renderer not available");
        return;
    }

    if(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) {
        // switch to windowed mode
        SDL_Log("Switching to windowed mode.");
        SDL_SetWindowFullscreen(window, (SDL_GetWindowFlags(window) ^ SDL_WINDOW_FULLSCREEN_DESKTOP));

        SDL_SetWindowSize(window, settings.video.physicalWidth, settings.video.physicalHeight);
        SDL_RenderSetLogicalSize(renderer, settings.video.width, settings.video.height);
    } else {
        // switch to fullscreen mode
        SDL_Log("Switching to fullscreen mode.");
        SDL_DisplayMode displayMode;
        int displayIndex = SDL_GetWindowDisplayIndex(window);
        if(displayIndex >= 0 && SDL_GetDesktopDisplayMode(displayIndex, &displayMode) == 0) {
            SDL_SetWindowFullscreen(window, (SDL_GetWindowFlags(window) ^ SDL_WINDOW_FULLSCREEN_DESKTOP));
            SDL_SetWindowSize(window, displayMode.w, displayMode.h);
            SDL_RenderSetLogicalSize(renderer, settings.video.width, settings.video.height);
        } else {
            SDL_Log("Warning: Could not get desktop display mode, using current window size");
            SDL_SetWindowFullscreen(window, (SDL_GetWindowFlags(window) ^ SDL_WINDOW_FULLSCREEN_DESKTOP));
        }
    }

    // we just need to flush all events; otherwise we might get them twice
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);

    // wait a bit to avoid immediately switching back
    SDL_Delay(100);
}

std::string getConfigFilepath()
{
    // User config file is stored in user directory (AppData on Windows, ~/.config on Linux, etc.)
    char tmp[FILENAME_MAX];
    if(fnkdat(CONFIGFILENAME, tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) < 0) {
        THROW(std::runtime_error, "fnkdat() failed for config file!");
    }
    return std::string(tmp);
}

std::string getConfigTemplateFilepath()
{
    // Template config file is in config subdirectory of game directory
    return getDuneLegacyDataDir() + "/config/" + CONFIGFILENAME;
}

std::string getLogFilepath()
{
    // determine path to config file
    char tmp[FILENAME_MAX];
    if(fnkdat(LOGFILENAME, tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) < 0) {
        THROW(std::runtime_error, "fnkdat() failed!");
    }

    return std::string(tmp);
}

std::string getPerformanceLogFilepath()
{
    // determine path to performance logfile
    char tmp[FILENAME_MAX];
    if(fnkdat("DuneCity-Performance.log", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) < 0) {
        THROW(std::runtime_error, "fnkdat() failed for performance log!");
    }

    return std::string(tmp);
}

std::string getObjectDataConfigFilepath()
{
    // If ModManager is initialized and a non-vanilla mod is active, use mod path
    if (ModManager::instance().isInitialized() && 
        ModManager::instance().getActiveModName() != "vanilla") {
        return ModManager::instance().getActiveObjectDataPath();
    }
    
    // Default: user config directory (preserves existing user customizations)
    char tmp[FILENAME_MAX];
    if(fnkdat("config/ObjectData.ini", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) < 0) {
        THROW(std::runtime_error, "fnkdat() failed for ObjectData.ini!");
    }
    return std::string(tmp);
}

std::string getObjectDataTemplateFilepath()
{
    // Template ObjectData.ini.default is in config subdirectory of game directory
    return getDuneLegacyDataDir() + "/config/ObjectData.ini.default";
}

static std::string getQuantBotTemplateFilepath()
{
    return getDuneLegacyDataDir() + "/config/QuantBot Config.ini.default";
}

static bool computeFileDigest(const std::string& filepath, std::array<unsigned char, 16>& digest)
{
    if(md5_file(filepath.c_str(), digest.data()) != 0) {
        return false;
    }
    return true;
}

static bool areConfigFilesOutOfSync(bool& objectDataOutOfSync, bool& quantBotOutOfSync)
{
    objectDataOutOfSync = false;
    quantBotOutOfSync = false;

    const std::string objectTemplate = getObjectDataTemplateFilepath();
    const std::string objectUser = getObjectDataConfigFilepath();

    if(!existsFile(objectTemplate)) {
        SDL_Log("Warning: Template ObjectData.ini.default missing at %s", objectTemplate.c_str());
        objectDataOutOfSync = true;
    } else if(!existsFile(objectUser)) {
        SDL_Log("ObjectData.ini missing at %s", objectUser.c_str());
        objectDataOutOfSync = true;
    } else {
        std::array<unsigned char, 16> templateDigest{};
        std::array<unsigned char, 16> userDigest{};
        if(!computeFileDigest(objectTemplate, templateDigest) || !computeFileDigest(objectUser, userDigest)) {
            SDL_Log("Warning: Unable to compare ObjectData configuration files.");
            objectDataOutOfSync = true;
        } else if(templateDigest != userDigest) {
            objectDataOutOfSync = true;
        }
    }

    const std::string quantTemplate = getQuantBotTemplateFilepath();
    const std::string quantUser = getQuantBotConfigFilepath();

    if(!existsFile(quantTemplate)) {
        SDL_Log("Warning: Template QuantBot Config.ini.default missing at %s", quantTemplate.c_str());
        quantBotOutOfSync = true;
    } else if(!existsFile(quantUser)) {
        SDL_Log("QuantBot Config.ini missing at %s", quantUser.c_str());
        quantBotOutOfSync = true;
    } else {
        std::array<unsigned char, 16> templateDigest{};
        std::array<unsigned char, 16> userDigest{};
        if(!computeFileDigest(quantTemplate, templateDigest) || !computeFileDigest(quantUser, userDigest)) {
            SDL_Log("Warning: Unable to compare QuantBot configuration files.");
            quantBotOutOfSync = true;
        } else if(templateDigest != userDigest) {
            quantBotOutOfSync = true;
        }
    }

    return objectDataOutOfSync || quantBotOutOfSync;
}

static bool copyTemplateFile(const std::string& templateRelativePath, const std::string& destinationPath)
{
    try {
        auto rwSource = pFileManager->openFile(templateRelativePath);
        if(!rwSource) {
            SDL_Log("copyTemplateFile: failed to open template '%s'", templateRelativePath.c_str());
            return false;
        }

        auto rwDest = sdl2::RWops_ptr{ SDL_RWFromFile(destinationPath.c_str(), "wb") };
        if(!rwDest) {
            SDL_Log("copyTemplateFile: failed to open destination '%s': %s", destinationPath.c_str(), SDL_GetError());
            return false;
        }

        SDL_ClearError();
        std::array<char, 4096> buffer{};

        while(true) {
            size_t bytesRead = SDL_RWread(rwSource.get(), buffer.data(), 1, buffer.size());
            if(bytesRead == 0) {
                const char* err = SDL_GetError();
                if(err != nullptr && err[0] != '\0') {
                    SDL_Log("copyTemplateFile: read error on '%s': %s", templateRelativePath.c_str(), err);
                    return false;
                }
                break; // EOF
            }

            size_t bytesWritten = SDL_RWwrite(rwDest.get(), buffer.data(), 1, bytesRead);
            if(bytesWritten != bytesRead) {
                SDL_Log("copyTemplateFile: write error on '%s': %s", destinationPath.c_str(), SDL_GetError());
                return false;
            }
        }

        return true;
    } catch(const std::exception& ex) {
        SDL_Log("copyTemplateFile: exception while copying '%s' -> '%s': %s", templateRelativePath.c_str(), destinationPath.c_str(), ex.what());
        return false;
    }
}

std::string getDefaultPlayerName() {
    char playername[MAX_PLAYERNAMELENGHT+1] = "Player";

#ifdef _WIN32
    DWORD playernameLength = MAX_PLAYERNAMELENGHT+1;
    GetUserName(playername, &playernameLength);
#else
    struct passwd* pwent = getpwuid(getuid());

    if(pwent != nullptr) {
        strncpy(playername, pwent->pw_name, MAX_PLAYERNAMELENGHT + 1);
        playername[MAX_PLAYERNAMELENGHT] = '\0';
    }
#endif

    playername[0] = toupper(playername[0]);
    return std::string(playername);
}

bool restoreDefaultConfigs() {
    SDL_Log("========== RESTORING DEFAULT CONFIG FILES ==========");
    
    bool success = true;
    
    // Restore ObjectData.ini
    {
        try {
            std::string userPath = getObjectDataConfigFilepath();
            SDL_Log("Restoring ObjectData.ini to: %s", userPath.c_str());
            
            if (copyTemplateFile("config/ObjectData.ini.default", userPath)) {
                SDL_Log("  ✓ ObjectData.ini restored successfully");
            } else {
                SDL_Log("  ✗ Failed to restore ObjectData.ini");
                success = false;
            }
        } catch (std::exception& e) {
            SDL_Log("  ✗ Error restoring ObjectData.ini: %s", e.what());
            success = false;
        }
    }
    
    // Restore QuantBot Config.ini
    {
        try {
            std::string userPath = getQuantBotConfigFilepath();
            SDL_Log("Restoring QuantBot Config.ini to: %s", userPath.c_str());
            
            if (copyTemplateFile("config/QuantBot Config.ini.default", userPath)) {
                SDL_Log("  ✓ QuantBot Config.ini restored successfully");
            } else {
                SDL_Log("  ✗ Failed to restore QuantBot Config.ini");
                success = false;
            }
        } catch (std::exception& e) {
            SDL_Log("  ✗ Error restoring QuantBot Config.ini: %s", e.what());
            success = false;
        }
    }
    
    SDL_Log("====================================================");
    return success;
}

static bool promptToRestoreOutOfSyncConfigurations()
{
    // With the mod system, vanilla mod is automatically seeded from templates
    // by ModManager::initialize() -> vanillaNeedsReseed() -> seedVanillaFromDefaults()
    // This legacy check is no longer needed.
    return false;
}

void createDefaultConfigFile(const std::string& configfilepath, const std::string& language) {
    SDL_Log("Creating user config file '%s'", configfilepath.c_str());

    // Try to copy template file from config directory first (only if FileManager is initialized)
    if (pFileManager) {
    try {
        auto templateFile = pFileManager->openFile("config/" + std::string(CONFIGFILENAME));
        if (templateFile) {
            SDL_Log("Copying template from game installation directory...");
            INIFile templateINI(templateFile.get());
            
            // Set user-specific defaults
            templateINI.setStringValue("General", "Player Name", getDefaultPlayerName());
            templateINI.setStringValue("General", "Language", language);
            templateINI.setBoolValue("General", "Diagnostic Logs", SettingsClass::GeneralClass::defaultDiagnosticLogs);
            
            if (templateINI.saveChangesTo(configfilepath)) {
                SDL_Log("User config file created from template successfully");
                SDL_Log("  Template location: %s", getConfigTemplateFilepath().c_str());
                SDL_Log("  User config location: %s", configfilepath.c_str());
                return;
            }
        }
    } catch (std::exception& e) {
        SDL_Log("Warning: Could not copy template from config directory: %s", e.what());
        SDL_Log("Falling back to programmatic creation...");
    }
    } // end if (pFileManager)

    // Fallback: create programmatically in user directory
    SDL_Log("Creating default config file in user directory: %s", configfilepath.c_str());
    auto file = sdl2::RWops_ptr{ SDL_RWFromFile(configfilepath.c_str(), "w") };
    if(!file) {
        THROW(sdl_error, "Opening config file failed: %s!", SDL_GetError());
    }

    const char configfile[] =   "[General]\n"
#ifdef __EMSCRIPTEN__
                                "Diagnostic Logs = false     # Enable development diagnostics in Settings > Advanced\n"
#else
                                "Diagnostic Logs = true      # Enable development diagnostics in Settings > Advanced\n"
#endif
                                "Play Intro = false          # Play the intro when starting the game?\n"
                                "Player Name = %s            # The name of the player\n"
                                "Language = %s               # en = English, fr = French, de = German\n"
                                "Scroll Speed = 50           # Amount to scroll the map when the cursor is near the screen border\n"
                                "Show Tutorial Hints = true  # Show tutorial hints during the game\n"
                                "DuneCity Campaign Skin = 0 # 0 = SimCity, 1 = Dune2 (presentation only)\n"
                                "Multiple Players Per House = false  # Custom game: allow two players per house\n"
                                "\n"
                                "[Video]\n"
                                "# Minimum resolution is 640x480\n"
                                "Width = 640\n"
                                "Height = 480\n"
                                "Physical Width = 640\n"
                                "Physical Height = 480\n"
                                "Interface Height = 0       # 0 = automatic; 480/600/768 = fixed UI size; Width keeps 4:3 or 16:9\n"
                                "Start Menu Mode = 0        # 0 = classic; 1 = enlarged TV/tablet/accessibility layout\n"
                                "Menu Palette = 0           # 0 = desert gold; 1 = high contrast\n"
                                "Fullscreen = true\n"
                                "FrameLimit = true           # Enable VSync for smooth, tear-free rendering.\n"
                                "Preferred Zoom Level = 1    # 0 = no zooming, 1 = 2x, 2 = 3x\n"
                                "Scaler = ScaleHD            # Scaler to use: ScaleHD = apply manual drawn mask to upscale, Scale2x = smooth edges, ScaleNN = nearest neighbour, \n"
                                "RotateUnitGraphics = false  # Freely rotate unit graphics, e.g. carryall graphics\n"
                                "\n"
                                "[Audio]\n"
                                "# There are three different possibilities to play music\n"
                                "#  adl       - This option will use the Dune 2 music as used on e.g. SoundBlaster16 cards\n"
                                "#  xmi       - This option plays the xmi files of Dune 2. Sounds more midi-like\n"
                                "#  directory - Plays music from the \"music\"-directory inside your game directory\n"
                                "#              The \"music\"-directory should contain 5 subdirectories named attack, intro, peace, win and lose\n"
                                "#              Put any mp3, ogg or mid file there and it will be played in the particular situation\n"
                                "Music Type = adl\n"
                                "ADL Harmonic Stereo = false # Legacy detuned stereo effect; false gives clean, pitch-stable playback\n"
                                "Play Music = true\n"
                                "Music Volume = 64           # Volume between 0 and 128\n"
                                "Play SFX = true\n"
                                "SFX Volume = 64             # Volume between 0 and 128\n"
                                "Play Credits SFX = false    # Play sound when credits change (harvester deliveries, spending credits)\n"
                                "\n"
                                "[Network]\n"
                                "ServerPort = %d\n"
                                "MetaServer = %s\n"
                                "\n"
                                "[AI]\n"
                                "Campaign AI = qBotEasy\n"
                                "\n"
                                "[Game Options]\n"
                                "Game Speed = 16                         # The default speed of the game: 32 = very slow, 8 = very fast, 16 = default\n"
                                "Concrete Required = true                # If true building on bare rock will result in 50%% structure health penalty\n"
                                "Fog of War = false                      # If true explored terrain will become foggy when no unit or structure is next to it\n"
                                "Start with Explored Map = false         # If true the complete map is unhidden at the beginning of the game\n"
                                "Instant Build = false                   # If true the building of structures and units does not take any time\n"
                                "Only One Palace = false                 # If true, only one palace can be build per house\n"
                                "Rocket-Turrets Need Power = false       # If true, rocket turrets are dysfunctional on power shortage\n"
                                "Sandworms Respawn = false               # If true, killed sandworms respawn after some time\n"
                                "Killed Sandworms Drop Spice = false     # If true, killed sandworms drop some spice\n"
                                "Manual Carryall Drops = false           # If true, player can request carryall to transport units\n"
                                "Maximum Number of Units Override = 0    # Override the maximum number of units each house is allowed to build (-1 = use map default, 0 = unlimited, >0 = specific limit)\n"
                                "Maximum Number of Harvesters Override = -1  # Override the maximum number of harvesters each house is allowed to build (-1 = use map size defaults from ObjectData.ini, >=0 = specific limit)\n";

    // replace player name, language, server port and metaserver
    std::string playername = getDefaultPlayerName();
    std::string strConfigfile = fmt::sprintf(configfile, playername, language, DEFAULT_PORT, DEFAULT_METASERVER);

    if(SDL_RWwrite(file.get(), strConfigfile.c_str(), 1, strConfigfile.length()) == 0) {
        THROW(sdl_error, "Writing config file failed: %s!", SDL_GetError());
    }
}

void logOutputFunction(void *userdata, int category, SDL_LogPriority priority, const char *message) {
    /*
    static const char* priorityStrings[] = {
        nullptr,
        "VERBOSE ",
        "DEBUG   ",
        "INFO    ",
        "WARN    ",
        "ERROR   ",
        "CRITICAL"
    };
    fprintf(stderr, "%s:   %s\n", priorityStrings[priority], message);
    */
    fprintf(stderr, "%s\n", message);
    fflush(stderr);

    // Keep warnings/errors on stderr with diagnostics disabled, without
    // maintaining the additional development trace file.
    if (!settings.general.diagnosticLogs) return;

    // DuneCity 1.0.501: mirror all SDL logs to dunecity-crash.log next to the
    // executable. On Windows release builds stderr isn't visible, so a silent
    // crash in the async GFXManager/SFXManager loaders left users (Tornie)
    // staring at a black screen with no diagnostic. The log file gives Stefan
    // something to attach to a bug report.
    static FILE* logFile = nullptr;
    if(logFile == nullptr) {
        logFile = fopen("dunecity-crash.log", "w");
    }
    if(logFile != nullptr) {
        fprintf(logFile, "%s\n", message);
        fflush(logFile);
    }
}

void showMissingFilesMessageBox() {
#ifdef __ANDROID__
    if((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) {
        std::string instruction = "DuneCity is missing required data files. Search paths:\n";
        for(const std::string& searchPath : FileManager::getSearchPath()) {
            instruction += " " + searchPath + "\n";
        }
        SDL_Log("%s", instruction.c_str());
        fprintf(stderr, "%s\n", instruction.c_str());
        return;
    }
#endif

    applyCursorVisibilitySetting();

    std::string instruction = "DuneCity uses the data files from original Dune II. The following files are missing:\n";

    for(const std::string& missingFile : FileManager::getMissingFiles()) {
        instruction += " " + missingFile + "\n";
    }

    instruction += "\nPut them in one of the following directories and restart DuneCity:\n";
    for(const std::string& searchPath : FileManager::getSearchPath()) {
        instruction += " " + searchPath + "\n";
    }

    instruction += "\nYou may want to add GERMAN.PAK or FRENCH.PAK for playing in these languages.";

    if(!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DuneCity", instruction.c_str(), nullptr)) {
        fprintf(stderr, "%s\n", instruction.c_str());
    }
}

std::string getUserLanguage() {
    const char* pLang = nullptr;

#ifdef _WIN32
    char ISO639_LanguageName[10];
    if(GetLocaleInfo(GetUserDefaultLCID(), LOCALE_SISO639LANGNAME, ISO639_LanguageName, sizeof(ISO639_LanguageName)) == 0) {
        return "";
    } else {

        pLang = ISO639_LanguageName;
    }

#elif defined (__APPLE__)
    pLang = getMacLanguage();
    if(pLang == nullptr) {
        return "";
    }

#else
    // should work on most unices
    pLang = getenv("LC_ALL");
    if(pLang == nullptr) {
        // try LANG
        pLang = getenv("LANG");
        if(pLang == nullptr) {
            return "";
        }
    }
#endif

    SDL_Log("User locale is '%s'", pLang);

    if(strlen(pLang) < 2) {
        return "";
    } else {
        return strToLower(std::string(pLang, 2));
    }
}


int main(int argc, char *argv[]) {
#ifndef __EMSCRIPTEN__
    // Packaging checks run before opening a profile or game session.
    for(int index = 1; index < argc; ++index) {
        if(std::strcmp(argv[index], "--check-desktop-runtime") == 0) {
            if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
                std::fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
                return EXIT_FAILURE;
            }
            SDL_Window* window = SDL_CreateWindow("Dune City runtime check", 0, 0, 64, 64, SDL_WINDOW_HIDDEN);
            SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : nullptr;
            const bool ready = renderer && SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) == 0
                && SDL_RenderClear(renderer) == 0;
            if(ready) SDL_RenderPresent(renderer);
            else std::fprintf(stderr, "SDL rendering failed: %s\n", SDL_GetError());
#ifdef __APPLE__
            for(uint32_t i = 0; i < _dyld_image_count(); ++i) {
                const char* path = _dyld_get_image_name(i);
                const char* name = path ? std::strrchr(path, '/') : nullptr;
                name = name ? name + 1 : path;
                if(name && (std::strncmp(name, "libSDL", 6) == 0 || std::strncmp(name, "SDL", 3) == 0))
                    std::printf("SDL runtime library: %s\n", path);
            }
#endif
            if(renderer) SDL_DestroyRenderer(renderer);
            if(window) SDL_DestroyWindow(window);
            SDL_Quit();
            if(ready) std::puts("Desktop runtime initialization and rendering passed");
            return ready ? EXIT_SUCCESS : EXIT_FAILURE;
        }
        if(std::strcmp(argv[index], "--check-relay-support") == 0) {
            const auto support = relayWebSocketSupport();
            const bool secure = support.available && support.reason.empty();
            std::fprintf(secure ? stdout : stderr, "%s\n", secure
                ? "Secure relay WebSocket support available" : support.reason.c_str());
            return secure ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }
#endif
    SDL_LogSetOutputFunction(logOutputFunction, nullptr);
    // Do not create a development trace before the persisted preference is read.
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_WARN);

    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif

    // v1.0.512: build-stamp log so Stefan can verify the binary on disk
    // actually contains the v1.0.5xx fixes. If this line is missing from
    // the run log, the .exe is stale.
    SDL_Log("DuneCity v%s — build stamp active", std::string(VERSION).c_str());

    // v1.0.514: install SIGSEGV/SIGABRT/SIGFPE handler so a fatal native
    // crash (nullptr deref, divide by zero, etc.) writes a diagnostic to
    // dunecity-crash.log before the process dies, instead of dying silently
    // with no visible feedback. The handler then re-raises the signal with
    // the default handler so a debugger can attach if running under one.
    // This is the Tornie fix for the silent SIGSEGV that Stefan reported:
    // a previous binary (v1.0.499 etc.) crashed at the main menu after
    // Discord connected and there was no log entry to diagnose why. With
    // this handler, the crash dump is written before exit so the cause
    // can be inspected post-mortem.
    #if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    {
        struct sigaction sa {};
        sa.sa_sigaction = [](int sig, siginfo_t* info, void* /*ucontext*/) {
            // Async-signal-safe: only write(), no malloc, no SDL_Log, no fprintf
            // to a FILE* (those can deadlock). Use raw write() to fd 2 (stderr)
            // and a fixed file descriptor for the crash log.
            const char* sigName = "UNKNOWN";
            switch(sig) {
                case SIGSEGV: sigName = "SIGSEGV"; break;
                case SIGABRT: sigName = "SIGABRT"; break;
                case SIGFPE:  sigName = "SIGFPE";  break;
                default: break;
            }
            char buf[512];
            int n = snprintf(buf, sizeof(buf),
                "\n=== DuneCity fatal crash ===\n"
                "  Signal:  %d (%s)\n"
                "  Reason:  %d (si_code)\n"
                "  Address: %p (si_addr)\n"
                "  Version: %s\n"
                "  Stack trace not available (would require libunwind).\n"
                "  Check Dune City.log for the last SDL_Log lines before the\n"
                "  crash — that is where the actionable diagnostic lives.\n"
                "=============================\n",
                sig, sigName, info->si_code, info->si_addr, VERSION);
            if(n > 0) {
                // stderr
                (void)!write(2, buf, n);
                // crash log file (best-effort, opened each invocation)
                int fd = open("dunecity-crash.log",
#ifdef O_APPEND
                    O_WRONLY | O_CREAT | O_APPEND
#else
                    O_WRONLY | O_CREAT
#endif
                    , 0644);
                if(fd >= 0) {
                    (void)!write(fd, buf, n);
                    close(fd);
                }
            }
            // Re-raise with the default handler so a debugger can catch
            // the signal and the process exits with the conventional code.
            struct sigaction dfl {};
            dfl.sa_handler = SIG_DFL;
            sigemptyset(&dfl.sa_mask);
            dfl.sa_flags = 0;
            sigaction(sig, &dfl, nullptr);
            raise(sig);
        };
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, nullptr);
        sigaction(SIGABRT, &sa, nullptr);
        sigaction(SIGFPE,  &sa, nullptr);
        SDL_Log("DuneCity: SIGSEGV/SIGABRT/SIGFPE handler installed");
    }
    #endif

    // global try/catch around everything
    try {

        // init fnkdat
        if(fnkdat(nullptr, nullptr, 0, FNKDAT_INIT) < 0) {
            THROW(std::runtime_error, "Cannot initialize fnkdat!");
        }

        bool bShowDebugLog = false;
        for(int i=1; i < argc; i++) {
            //check for overiding params
            std::string parameter(argv[i]);

            if(parameter == "--showlog") {
                // special parameter which does not overwrite settings
                bShowDebugLog = true;
            } else if((parameter == "-f") || (parameter == "--fullscreen") || (parameter == "-w") || (parameter == "--window") || (parameter.compare(0, 13, "--PlayerName=") == 0) || (parameter.compare(0, 13, "--ServerPort=") == 0)
                      || parameter.compare(0, 16, "--RelayEndpoint=") == 0
                      || parameter.compare(0, 19, "--RelayDevEndpoint=") == 0
                      || parameter == "--RelayDev") {
                // normal parameter for overwriting settings
                // handle later
            } else {
                printUsage();
                exit(EXIT_FAILURE);
            }
        }

        // Read an existing preference before redirecting/logging startup output.
        // Missing keys use the platform default, including older browser profiles.
        if (existsFile(getConfigFilepath())) {
            INIFile earlyConfig(getConfigFilepath());
            settings.general.diagnosticLogs = earlyConfig.getBoolValue("General", "Diagnostic Logs",
                SettingsClass::GeneralClass::defaultDiagnosticLogs);
        }
        SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, settings.general.diagnosticLogs
            ? SDL_LOG_PRIORITY_VERBOSE : SDL_LOG_PRIORITY_WARN);

#ifndef __EMSCRIPTEN__
        // Browser errors stay in the console; avoid a growing persistent stdout
        // file. Opted-in development capture also has AI/performance trace files.
        if(bShowDebugLog == false) {
            // get utf8-encoded log file path
            std::string logfilePath = getLogFilepath();
            char* pLogfilePath = (char*) logfilePath.c_str();

            #ifdef _WIN32

            // on win32 we need an ansi-encoded filepath
            WCHAR szwLogPath[MAX_PATH];
            char szLogPath[MAX_PATH];

            if(MultiByteToWideChar(CP_UTF8, 0, pLogfilePath, -1, szwLogPath, MAX_PATH) == 0) {
                THROW(std::runtime_error, "Conversion of logfile path from utf-8 to utf-16 failed!");
            }

            if(WideCharToMultiByte(CP_ACP, 0, szwLogPath, -1, szLogPath, MAX_PATH, nullptr, nullptr) == 0) {
                THROW(std::runtime_error, "Conversion of logfile path from utf-16 to ansi failed!");
            }

            pLogfilePath = szLogPath;

            if(freopen(pLogfilePath, "w", stdout) == NULL) {
                THROW(io_error, "Reopening logfile '%s' as stdout failed!", pLogfilePath);
            }
            setbuf(stdout, nullptr);   // No buffering

            if(freopen(pLogfilePath, "w", stderr) == NULL) {
                // use stdout in this error case as stderr is not yet ready
                THROW(io_error, "Reopening logfile '%s' as stderr failed!", pLogfilePath);
            }
            setbuf(stderr, nullptr);   // No buffering

            if(dup2(fileno(stdout), fileno(stderr)) < 0) {
                THROW(io_error, "Redirecting stderr to stdout failed!");
            }

            #else

            int d = open(pLogfilePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(d < 0) {
                THROW(io_error, "Opening logfile '%s' failed!", pLogfilePath);
            }
            // Hint: fileno(stdout) != STDOUT_FILENO on Win32
            if(dup2(d, fileno(stdout)) < 0) {
                THROW(io_error, "Redirecting stdout failed!");
            }

            // Hint: fileno(stderr) != STDERR_FILENO on Win32
            if(dup2(d, fileno(stderr)) < 0) {
                THROW(io_error, "Redirecting stderr failed!");
            }

            #endif
        }

        #endif

        // Install crash handlers early, after logging is set up
#ifndef __EMSCRIPTEN__
        std::string crashLogPath = getLogFilepath();
        installCrashHandlers(crashLogPath.c_str());
#endif

        SDL_Log("Starting DuneCity %s on %s", VERSION, SDL_GetPlatform());

        // First check for missing files
        std::vector<std::string> missingFiles = FileManager::getMissingFiles();

        if(!missingFiles.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Required data check failed in '%s' (%zu missing file(s)):",
                         getDuneLegacyDataDir().c_str(), missingFiles.size());
            for(const auto& missingFile : missingFiles) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "  %s", missingFile.c_str());
            }

            // create data directory inside config directory
            char tmp[FILENAME_MAX];
            fnkdat("data/", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);

            showMissingFilesMessageBox();

            return EXIT_FAILURE;
        }

        bool bExitGame = false;
        bool bFirstInit = true;
        bool bFirstGamestart = false;

        debug = false;

        int currentDisplayIndex = SCREEN_DEFAULT_DISPLAYINDEX;

        do {
            // we do not use rand() but maybe some library does; thus we shall initialize it
            unsigned int seed = (unsigned int) time(nullptr);
            srand(seed);

            // check if configfile exists
            std::string configfilepath = getConfigFilepath();
            if(existsFile(configfilepath) == false) {
                std::string userLanguage = getUserLanguage();
                if(userLanguage.empty()) {
                    userLanguage = "en";
                }

                bFirstGamestart = true;
                createDefaultConfigFile(configfilepath, userLanguage);
            }

            INIFile myINIFile(configfilepath);

            settings.general.playIntro = myINIFile.getBoolValue("General","Play Intro",false);
            settings.general.playerName = myINIFile.getStringValue("General","Player Name","Player");
            settings.general.language = myINIFile.getStringValue("General","Language","en");
            settings.general.scrollSpeed = myINIFile.getIntValue("General","Scroll Speed",50);
            settings.general.wasdCamera = myINIFile.getBoolValue("General","WASD Camera",false);
            settings.general.leftClickOrders = myINIFile.getBoolValue("General","Left Click Orders",false);
            settings.general.showMovementPaths = myINIFile.getBoolValue("General","Movement Paths",true);
            settings.general.diagnosticLogs = myINIFile.getBoolValue("General","Diagnostic Logs",
                SettingsClass::GeneralClass::defaultDiagnosticLogs);
            SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, settings.general.diagnosticLogs
                ? SDL_LOG_PRIORITY_VERBOSE : SDL_LOG_PRIORITY_WARN);
            settings.general.showTutorialHints = myINIFile.getBoolValue("General","Show Tutorial Hints",true);
            settings.general.duneCityCampaignSkin = std::clamp(
                myINIFile.getIntValue("General", "DuneCity Campaign Skin", 0), 0, 1);
            settings.general.multiplePlayersPerHouse = myINIFile.getBoolValue("General","Multiple Players Per House",false);
            // The Offline/Online choice is remembered per menu; the defaults are the ones a
            // first-time player gets: co-op offered for the campaign, offline for custom games.
            settings.general.campaignOnline = myINIFile.getBoolValue("General","Campaign Online",true);
            settings.general.customGameOnline = myINIFile.getBoolValue("General","Custom Game Online",false);
            // Empty until the player picks a mod for a custom game; then that choice is
            // restored on every entry, independent of mods activated by saves or joins.
            settings.general.customGameMod = myINIFile.getStringValue("General","Custom Game Mod","");
            settings.video.width = myINIFile.getIntValue("Video","Width",640);
            settings.video.height = myINIFile.getIntValue("Video","Height",480);
            settings.video.interfaceHeight = validatedInterfaceHeight(
                myINIFile.getIntValue("Video", "Interface Height", 0),
#ifdef __ANDROID__
                true
#else
                false
#endif
            );
            settings.video.physicalWidth= myINIFile.getIntValue("Video","Physical Width",640);
            settings.video.physicalHeight = myINIFile.getIntValue("Video","Physical Height",480);
            settings.video.fullscreen = myINIFile.getBoolValue("Video","Fullscreen",false);
            settings.video.frameLimit = myINIFile.getBoolValue("Video","FrameLimit",true);
            settings.video.preferredZoomLevel = myINIFile.getIntValue("Video","Preferred Zoom Level", 0);
            settings.video.scaler = myINIFile.getStringValue("Video","Scaler","ScaleHD");
            settings.video.rotateUnitGraphics = myINIFile.getBoolValue("Video","RotateUnitGraphics",false);
            settings.video.showWatermark = myINIFile.getBoolValue("Video","Show Watermark",true);
            settings.video.cursorVisibility = myINIFile.getIntValue("Video","Cursor Visibility",0);
            settings.video.menuPalette = validatedMenuPalette(myINIFile.getIntValue("Video", "Menu Palette", 0));
            settings.video.startMenuMode = validatedStartMenuMode(myINIFile.getIntValue("Video", "Start Menu Mode", 0));
            settings.video.cursorScale = myINIFile.getIntValue("Video","Cursor Scale",0);
            settings.audio.musicType = myINIFile.getStringValue("Audio","Music Type","adl");
            settings.audio.adlHarmonicStereo = myINIFile.getBoolValue("Audio","ADL Harmonic Stereo", false);
            settings.audio.playMusic = myINIFile.getBoolValue("Audio","Play Music", true);
            settings.audio.musicVolume = myINIFile.getIntValue("Audio","Music Volume", 64);
            settings.audio.playSFX = myINIFile.getBoolValue("Audio","Play SFX", true);
            settings.audio.sfxVolume = myINIFile.getIntValue("Audio","SFX Volume", 64);
            settings.audio.playCreditsSFX = myINIFile.getBoolValue("Audio","Play Credits SFX", false);

            settings.network.serverPort = myINIFile.getIntValue("Network","ServerPort",DEFAULT_PORT);
            settings.network.metaServer = myINIFile.getStringValue("Network","MetaServer",DEFAULT_METASERVER);
            
            // Migrate old SourceForge metaserver URL to new dunelegacy.com URL
            if(settings.network.metaServer.find("dunelegacy.sourceforge.net") != std::string::npos) {
                SDL_Log("Migrating old SourceForge metaserver URL to dunelegacy.com...");
                size_t pos = settings.network.metaServer.find("dunelegacy.sourceforge.net");
                settings.network.metaServer.replace(pos, strlen("dunelegacy.sourceforge.net"), "dunelegacy.com");
                myINIFile.setStringValue("Network","MetaServer",settings.network.metaServer);
                myINIFile.saveChangesTo(configfilepath);
                SDL_Log("Metaserver URL updated to: %s", settings.network.metaServer.c_str());
            }
            
            // Discord settings
            settings.discord.webhookUrl = myINIFile.getStringValue("Discord","WebhookUrl","");
            
            settings.network.debugNetwork = myINIFile.getBoolValue("Network","Debug Network",false);

            // Crossplay room relay. The production endpoint is configuration only; nothing here
            // deploys one, and an empty value simply means crossplay is not offered.
            settings.network.relayEndpoint =
                myINIFile.getStringValue("Network","Relay Endpoint",DEFAULT_RELAY_ENDPOINT);
            settings.network.relayDevelopmentEndpoint =
                myINIFile.getStringValue("Network","Relay Development Endpoint",
                                         DEVELOPMENT_RELAY_ENDPOINT);
            settings.network.relayUseDevelopmentEndpoint =
                myINIFile.getBoolValue("Network","Use Relay Development Endpoint",false);

            // Direct play. Separate keys from the relay on purpose: this address may only ever
            // name a signaling service, and the client refuses a relay address here.
            settings.network.directEndpoint =
                myINIFile.getStringValue("Network","Direct Endpoint",DEFAULT_DIRECT_ENDPOINT);
            settings.network.directDevelopmentEndpoint =
                myINIFile.getStringValue("Network","Direct Development Endpoint",
                                         DEVELOPMENT_DIRECT_ENDPOINT);

#ifdef __EMSCRIPTEN__
            // The browser build has no command line, so the page URL may name the relay. The
            // value goes through exactly the same validation as any other endpoint, and a plain
            // loopback address is still only accepted with the explicit development opt-in.
            {
                char relayFromPage[256] = {0};
                dunecityReadRelayUrlParameter(relayFromPage, static_cast<int>(sizeof(relayFromPage)));
                if(relayFromPage[0] != '\0') {
                    settings.network.relayEndpoint = relayFromPage;
                    settings.network.relayDevelopmentEndpoint = relayFromPage;
                    settings.network.directEndpoint = relayFromPage;
                    settings.network.directDevelopmentEndpoint = relayFromPage;
                }
                if(dunecityReadRelayDevelopmentParameter() != 0) {
                    settings.network.relayUseDevelopmentEndpoint = true;
                }
            }
#endif

            settings.ai.campaignAI = myINIFile.getStringValue("AI","Campaign AI",DEFAULTAIPLAYERCLASS);

            settings.gameOptions.gameSpeed = myINIFile.getIntValue("Game Options","Game Speed",GAMESPEED_DEFAULT);
            settings.gameOptions.concreteRequired = myINIFile.getBoolValue("Game Options","Concrete Required",true);
            settings.gameOptions.fogOfWar = myINIFile.getBoolValue("Game Options","Fog of War",false);
            settings.gameOptions.startWithExploredMap = myINIFile.getBoolValue("Game Options","Start with Explored Map",false);
            settings.gameOptions.instantBuild = myINIFile.getBoolValue("Game Options","Instant Build",false);
            settings.gameOptions.onlyOnePalace = myINIFile.getBoolValue("Game Options","Only One Palace",false);
            settings.gameOptions.rocketTurretsNeedPower = myINIFile.getBoolValue("Game Options","Rocket-Turrets Need Power",false);
            settings.gameOptions.sandwormsRespawn = myINIFile.getBoolValue("Game Options","Sandworms Respawn",false);
            settings.gameOptions.killedSandwormsDropSpice = myINIFile.getBoolValue("Game Options","Killed Sandworms Drop Spice",false);
            settings.gameOptions.manualCarryallDrops = myINIFile.getBoolValue("Game Options","Manual Carryall Drops",false);
            settings.gameOptions.maximumNumberOfUnitsOverride = myINIFile.getIntValue("Game Options","Maximum Number of Units Override",0);
            settings.gameOptions.maximumNumberOfHarvestersOverride = myINIFile.getIntValue("Game Options","Maximum Number of Harvesters Override",-1);
            settings.gameOptions.maximumNumberOfConstructionYardsOverride = myINIFile.getIntValue("Game Options","Maximum Number of Construction Yards Override",-1);
            settings.gameOptions.originalUnitDamage = myINIFile.getBoolValue("Game Options", "Original Unit Damage", false);
            settings.gameOptions.immortalHumanPlayer = myINIFile.getBoolValue("Game Options","Immortal Human Player",false);

            pTextManager = std::make_unique<TextManager>();

            missingFiles = FileManager::getMissingFiles();
            if(!missingFiles.empty()) {
                // set back to English
                std::string setBackToEnglishWarning = fmt::sprintf("The following files are missing for language \"%s\":\n",_("LanguageFileExtension"));
                for(const std::string& filename : missingFiles) {
                    setBackToEnglishWarning += filename + "\n";
                }
                setBackToEnglishWarning += "\nLanguage is changed to English!";
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "DuneCity", setBackToEnglishWarning.c_str(), NULL);

                SDL_Log("Warning: Language is changed to English!");

                settings.general.language = "en";
                myINIFile.setStringValue("General","Language",settings.general.language);
                myINIFile.saveChangesTo(configfilepath);

                // reinit text manager
                pTextManager = std::make_unique<TextManager>();
            }

            for(int i=1; i < argc; i++) {
                //check for overiding params
                std::string parameter(argv[i]);

                if((parameter == "-f") || (parameter == "--fullscreen")) {
                    settings.video.fullscreen = true;
                } else if((parameter == "-w") || (parameter == "--window")) {
                    settings.video.fullscreen = false;
                } else if(parameter.compare(0, 13, "--PlayerName=") == 0) {
                    settings.general.playerName = parameter.substr(strlen("--PlayerName="));
                } else if(parameter.compare(0, 13, "--ServerPort=") == 0) {
                    settings.network.serverPort = atol(argv[i] + strlen("--ServerPort="));
                } else if(parameter.compare(0, 16, "--RelayEndpoint=") == 0) {
                    settings.network.relayEndpoint = parameter.substr(strlen("--RelayEndpoint="));
                } else if(parameter.compare(0, 19, "--RelayDevEndpoint=") == 0) {
                    settings.network.relayDevelopmentEndpoint =
                        parameter.substr(strlen("--RelayDevEndpoint="));
                    settings.network.relayUseDevelopmentEndpoint = true;
                } else if(parameter == "--RelayDev") {
                    // Explicit opt-in: this is the only way a plain ws:// loopback endpoint
                    // becomes acceptable.
                    settings.network.relayUseDevelopmentEndpoint = true;
                }
            }

            if(bFirstInit == true) {
                SDL_Log("Initializing game...");

                // Force OpenGL rendering on macOS
                SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "1");
                SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
                SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
                SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
                // VSync disabled by default - controlled via renderer flags in setVideoMode()
                SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
                SDL_SetHint(SDL_HINT_VIDEO_X11_FORCE_EGL, "0");  // Disable EGL
                SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");      // Enable render batching
                SDL_SetHint(SDL_HINT_RENDER_LINE_METHOD, "3");   // Best line rendering quality

                if(SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO) < 0) {
                    THROW(sdl_error, "Couldn't initialize SDL: %s!", SDL_GetError());
                }

                SDL_version compiledVersion;
                SDL_version linkedVersion;
                SDL_VERSION(&compiledVersion);
                SDL_GetVersion(&linkedVersion);
                SDL_Log("SDL runtime v%d.%d.%d", linkedVersion.major, linkedVersion.minor, linkedVersion.patch);
                SDL_Log("SDL compile-time v%d.%d.%d", compiledVersion.major, compiledVersion.minor, compiledVersion.patch);

                if(TTF_Init() < 0) {
                    THROW(sdl_error, "Couldn't initialize SDL2_ttf: %s!", TTF_GetError());
                }

                SDL_version TTFCompiledVersion;
                SDL_TTF_VERSION(&TTFCompiledVersion);
                const SDL_version* pTTFLinkedVersion = TTF_Linked_Version();
                SDL_Log("SDL2_ttf runtime v%d.%d.%d", pTTFLinkedVersion->major, pTTFLinkedVersion->minor, pTTFLinkedVersion->patch);
                SDL_Log("SDL2_ttf compile-time v%d.%d.%d", TTFCompiledVersion.major, TTFCompiledVersion.minor, TTFCompiledVersion.patch);
            }

#ifdef __EMSCRIPTEN__
            // Migrate the old forced VGA browser default once. Preserve any
            // other saved resolution and every subsequent explicit VGA choice.
            if(bFirstInit && myINIFile.getIntValue("Video", "Browser Display Version", 0) < 1) {
                if(bFirstGamestart || (settings.video.physicalWidth == 640 && settings.video.physicalHeight == 480)) {
                    settings.video.physicalWidth = WebRuntime::defaultVideoWidth();
                    settings.video.physicalHeight = WebRuntime::defaultVideoHeight();
                    myINIFile.setIntValue("Video", "Physical Width", settings.video.physicalWidth);
                    myINIFile.setIntValue("Video", "Physical Height", settings.video.physicalHeight);
                    settings.video.preferredZoomLevel = 1;
                    myINIFile.setIntValue("Video", "Preferred Zoom Level", 1);
                }
                myINIFile.setIntValue("Video", "Browser Display Version", 1);
                myINIFile.saveChangesTo(getConfigFilepath());
                WebRuntime::syncPersistentFiles();
            }
#else
            if(bFirstGamestart == true && bFirstInit == true) {
                SDL_DisplayMode displayMode;
                SDL_GetDesktopDisplayMode(currentDisplayIndex, &displayMode);

                int factor = getLogicalToPhysicalResolutionFactor(displayMode.w, displayMode.h);
                settings.video.physicalWidth = displayMode.w;
                settings.video.physicalHeight = displayMode.h;
                settings.video.width = displayMode.w / factor;
                settings.video.height = displayMode.h / factor;
                settings.video.preferredZoomLevel = 1;

                myINIFile.setIntValue("Video","Width",settings.video.width);
                myINIFile.setIntValue("Video","Height",settings.video.height);
                myINIFile.setIntValue("Video","Physical Width",settings.video.physicalWidth);
                myINIFile.setIntValue("Video","Physical Height",settings.video.physicalHeight);
                myINIFile.setIntValue("Video","Preferred Zoom Level",1);

                myINIFile.saveChangesTo(getConfigFilepath());
            }

#endif

#ifdef __ANDROID__
            if(bFirstInit == true) {
                SDL_DisplayMode displayMode;
                SDL_GetDesktopDisplayMode(currentDisplayIndex, &displayMode);

                if(displayMode.w > 0 && displayMode.h > 0) {
                    settings.video.physicalWidth = displayMode.w;
                    settings.video.physicalHeight = displayMode.h;
                }
                settings.video.height = settings.video.interfaceHeight;
                settings.video.width = validatedInterfaceWidth(settings.video.width, settings.video.height);
                settings.video.fullscreen = true;

                SDL_Log("Android display config updated to %dx%d physical, %dx%d fixed logical",
                        settings.video.physicalWidth, settings.video.physicalHeight,
                        settings.video.width, settings.video.height);

                myINIFile.setIntValue("Video","Width",settings.video.width);
                myINIFile.setIntValue("Video","Height",settings.video.height);
                myINIFile.setIntValue("Video","Physical Width",settings.video.physicalWidth);
                myINIFile.setIntValue("Video","Physical Height",settings.video.physicalHeight);
                myINIFile.setBoolValue("Video","Fullscreen",settings.video.fullscreen);
                myINIFile.setIntValue("Video","Preferred Zoom Level",settings.video.preferredZoomLevel);
                myINIFile.saveChangesTo(getConfigFilepath());
            }
#endif

            Scaler::setDefaultScaler(Scaler::getScalerByName(settings.video.scaler));

            if(bFirstInit == true) {
                SDL_Log("Initializing audio...");
                constexpr int AUDIO_BUFFER_FRAMES = 1024;
                if( Mix_OpenAudio(AUDIO_FREQUENCY, AUDIO_S16SYS, 2, AUDIO_BUFFER_FRAMES) < 0 ) {
                    const std::string audioError=Mix_GetError();
                    SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Audio device unavailable: %s. Continuing with silent audio; restart to retry the device.",audioError.c_str());
                    SDL_AudioQuit();
                    if(SDL_AudioInit("dummy")<0 || Mix_OpenAudio(AUDIO_FREQUENCY,AUDIO_S16SYS,2,AUDIO_BUFFER_FRAMES)<0)
                        THROW(sdl_error,"Audio device failed (%s); silent mixer also failed: %s",audioError.c_str(),Mix_GetError());
                }
                {
                    int actualFrequency = 0;
                    int actualChannels = 0;
                    Uint16 actualFormat = 0;
                    Mix_QuerySpec(&actualFrequency, &actualFormat, &actualChannels);
                    const char* audioDriver = SDL_GetCurrentAudioDriver();
                    SDL_Log("Audio driver: %s; mixer: %d Hz, format 0x%04X, %d channel(s), buffer: %d frames; ADL mode: %s",
                            audioDriver != nullptr ? audioDriver : "unknown",
                            actualFrequency, static_cast<unsigned int>(actualFormat), actualChannels,
                            AUDIO_BUFFER_FRAMES,
                            settings.audio.adlHarmonicStereo ? "harmonic stereo" : "single-emulator dual mono");
                    SDL_Log("%d audio channels were allocated.", Mix_AllocateChannels(28));
                }
            }

            pFileManager = std::make_unique<FileManager>();

            // Initialize the mod system (seeds vanilla mod from install defaults if needed)
            SDL_Log("Initializing mod system...");
            ModManager::instance().initialize();

            // Initialize effective game options (base settings + mod overrides)
            effectiveGameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
            SDL_Log("Effective game options initialized (active mod: %s)", 
                    ModManager::instance().getActiveModName().c_str());

            // Create user config files if they don't exist
            // Check and copy ObjectData.ini
            {
                std::string userObjectDataPath = getObjectDataConfigFilepath();
                if (!existsFile(userObjectDataPath)) {
                    SDL_Log("ObjectData.ini not found in user directory, copying template...");
                    try {
                        if (copyTemplateFile("config/ObjectData.ini.default", userObjectDataPath)) {
                            SDL_Log("ObjectData.ini created successfully at: %s", userObjectDataPath.c_str());
                        } else {
                            SDL_Log("Warning: Failed to create ObjectData.ini");
                        }
                    } catch (std::exception& e) {
                        SDL_Log("Warning: Could not copy ObjectData.ini.default template: %s", e.what());
                    }
                }
            }

            // Check and copy QuantBot Config.ini  
            {
                std::string userQuantBotPath = getQuantBotConfigFilepath();
                if (!existsFile(userQuantBotPath)) {
                    SDL_Log("QuantBot Config.ini not found in user directory, copying template...");
                    try {
                        if (copyTemplateFile("config/QuantBot Config.ini.default", userQuantBotPath)) {
                            SDL_Log("QuantBot Config.ini created successfully at: %s", userQuantBotPath.c_str());
                        } else {
                            SDL_Log("Warning: Failed to create QuantBot Config.ini");
                        }
                    } catch (std::exception& e) {
                        SDL_Log("Warning: Could not copy QuantBot Config.ini.default template: %s", e.what());
                    }
                }
            }

            bool abortIteration = false;

            if(promptToRestoreOutOfSyncConfigurations()) {
                SDL_Log("Default configuration restored. Exiting to allow restart.");
                bExitGame = true;
                abortIteration = true;
            }

            if(!abortIteration) {
                // now we can finish loading texts
                pTextManager->loadData();

                palette = LoadPalette_RW(pFileManager->openFile("IBM.PAL").get());
                loadCustomPalette();
                applyCustomPaletteRuntimeHouseRamps();

                SDL_Log("Setting video mode...");
                setVideoMode(currentDisplayIndex);
                
                // Native renderers benefit from a short initialization pause.
#ifndef __EMSCRIPTEN__
                SDL_Delay(100);
#endif
                
                SDL_RendererInfo rendererInfo;
                SDL_GetRendererInfo(renderer, &rendererInfo);
                SDL_Log("Renderer: %s (max texture size: %dx%d)", rendererInfo.name, rendererInfo.max_texture_width, rendererInfo.max_texture_height);

                // Verify renderer is valid before proceeding
                if(renderer == nullptr) {
                    SDL_Log("Error: Renderer is null after setVideoMode!");
                    THROW(std::runtime_error, "Failed to create renderer during video mode initialization");
                }

                SDL_Log("Loading fonts...");
                pFontManager = std::make_unique<FontManager>();

                SDL_Log("Loading graphics and sounds...");

#ifdef HAS_ASYNC
                auto gfxManagerFut = std::async(std::launch::async, []() { return std::make_unique<GFXManager>(); } );
                auto sfxManagerFut = std::async(std::launch::async, []() { return std::make_unique<SFXManager>(); } );

                // DuneCity 1.0.501: catch any exception from the async loaders
                // so a failed load produces a visible error dialog + log file
                // entry instead of the silent death that 1.0.499 had (Tornie
                // OOB: "game doesn't launch, no alert, just nothing").
                try {
                    pGFXManager = gfxManagerFut.get();
                } catch(const std::exception& e) {
                    std::string msg = std::string("GFXManager failed to initialize:\n\n") + e.what()
                                    + "\n\nA required data file is probably missing or the bundled PAK is corrupt."
                                    + "\nSee dunecity-crash.log next to the executable for the full SDL log.";
                    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", msg.c_str());
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DuneCity — graphics load failed", msg.c_str(), nullptr);
                    THROW(std::runtime_error, "%s", msg.c_str());
                } catch(...) {
                    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "GFXManager failed to initialize: unknown exception");
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DuneCity — graphics load failed",
                                              "GFXManager threw an unknown exception. See dunecity-crash.log.", nullptr);
                    THROW(std::runtime_error, "GFXManager unknown exception");
                }

                try {
                    pSFXManager = sfxManagerFut.get();
                } catch(const std::exception& e) {
                    // SFX is non-fatal: log and continue with a null manager so the
                    // game at least shows the menu. Audio will be silent but visible.
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SFXManager failed to initialize: %s — continuing without audio", e.what());
                    pSFXManager = nullptr;
                } catch(...) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SFXManager threw an unknown exception — continuing without audio");
                    pSFXManager = nullptr;
                }
#else
                // g++ does not provide std::launch::async on all platforms
                pGFXManager = std::make_unique<GFXManager>();
                pSFXManager = std::make_unique<SFXManager>();
#endif

                GUIStyle::setGUIStyle(std::make_unique<DuneStyle>(settings.video.menuPalette));

                if(bFirstInit == true) {
                    SDL_Log("Starting sound player...");
                    soundPlayer = std::make_unique<SoundPlayer>();

                    if(settings.audio.musicType == "directory") {
                        SDL_Log("Starting directory music player...");
                        musicPlayer = std::make_unique<DirectoryPlayer>();
                    } else if(settings.audio.musicType == "adl") {
                        SDL_Log("Starting ADL music player...");
                        musicPlayer = std::make_unique<ADLPlayer>();
                    } else if(settings.audio.musicType == "xmi") {
                        SDL_Log("Starting XMI music player...");
                        musicPlayer = std::make_unique<XMIPlayer>();
                    } else {
                        THROW(std::runtime_error, "Invalid music type: '%'", settings.audio.musicType);
                    }

                    //musicPlayer->changeMusic(MUSIC_INTRO);
                }

                WebRuntime::markGameReady();

                // Browser visitors enter the playable menu immediately on first
                // launch. They can still enable the original intro in Options.
                bool shouldPlayIntro = (bFirstGamestart == true) || (settings.general.playIntro == true);
#ifdef __EMSCRIPTEN__
                shouldPlayIntro = settings.general.playIntro;
#endif

                // Playing intro
                if(shouldPlayIntro && (bFirstInit==true)) {
                    SDL_Log("Playing intro...");
                    Intro().run();
                }

                bFirstInit = false;

                // Re-apply the selected cursor policy for the main menu.
                applyCursorVisibilitySetting();

                // Initialize Discord Rich Presence
                DiscordManager::instance().initialize();
                if (!settings.discord.webhookUrl.empty()) {
                    DiscordManager::instance().setWebhookUrl(settings.discord.webhookUrl);
                    SDL_Log("Discord webhook configured");
                }

                SDL_Log("Starting main menu...");
                { // Scope
                    int menuResult = MainMenu().showMenu();
                    if (menuResult == MENU_QUIT_DEFAULT) {
                        bExitGame = true;
                    } else if (menuResult == MENU_QUIT_REINITIALIZE) {
                        // Reinitialize video mode and continue the loop
                        SDL_Log("Reinitializing video mode...");
                        // The loop will continue and reinitialize everything
                    }
                }
            }

            SDL_Log("Deinitialize...");

            GUIStyle::destroyGUIStyle();

            // clear everything
            if(bExitGame == true) {
                musicPlayer.reset();
                soundPlayer.reset();
                Mix_HaltMusic();
                Mix_CloseAudio();
            } else {
                // save the current display index for later reuse
                if(window != nullptr) {
                    currentDisplayIndex = SDL_GetWindowDisplayIndex(window);
                    // Safety check: ensure we got a valid display index
                    if(currentDisplayIndex < 0) {
                        currentDisplayIndex = 0; // Fallback to primary display
                    }
                }
            }

            pTextManager.reset();
            pSFXManager.reset();
            releaseCursorResources();
            pGFXManager.reset();
            pFontManager.reset();
            pFileManager.reset();

            // Safely destroy SDL objects
            if(screenTexture != nullptr) {
                SDL_DestroyTexture(screenTexture);
                screenTexture = nullptr;
            }
            if(renderer != nullptr) {
                SDL_DestroyRenderer(renderer);
                renderer = nullptr;
            }
            if(window != nullptr) {
                SDL_DestroyWindow(window);
                window = nullptr;
            }

            if(bExitGame == true) {
                DiscordManager::instance().shutdown();
                TTF_Quit();
                SDL_Quit();
            }
            SDL_Log("Deinitialization finished!");
        } while(bExitGame == false);

        // deinit fnkdat
        if(fnkdat(nullptr, nullptr, 0, FNKDAT_UNINIT) < 0) {
            THROW(std::runtime_error, "Cannot uninitialize fnkdat!");
        }
    } catch(const std::exception& e) {
        std::string message = std::string("An unhandled exception of type \'") + demangleSymbol(typeid(e).name()) + std::string("\' was thrown:\n\n") + e.what() + std::string("\n\nDuneCity will now be terminated!");
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", message.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DuneCity: Unrecoverable error", message.c_str(), nullptr);

        return EXIT_FAILURE;
    }

    DesktopUpdater::relaunchAfterShutdown();
    return EXIT_SUCCESS;
}
