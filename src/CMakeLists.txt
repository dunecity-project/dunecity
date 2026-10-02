# Set DUNELEGACY_DATADIR only for Linux FHS installs (with trailing slash so
# callers that concatenate subdirs without a separator still work, e.g.
# GFXManager's imported_sprites paths). Windows and macOS leave it unset so
# getDuneLegacyDataDir() falls through to SDL_GetBasePath() at runtime,
# making the install relocatable.
if(EMSCRIPTEN)
    set(DUNELEGACY_DATADIR "/")
elseif(NOT WIN32 AND NOT APPLE)
    set(DUNELEGACY_DATADIR "${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}/")
endif()

# Configure config.h (must come after DUNELEGACY_DATADIR is conditionally set)
configure_file(
    ${CMAKE_SOURCE_DIR}/include/config.h.in
    ${CMAKE_BINARY_DIR}/include/config.h
)

# Main executable
if(APPLE)
    # Create macOS application bundle
    add_executable(dunecity MACOSX_BUNDLE)
    
    # Set bundle properties
    set_target_properties(dunecity PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Dune City"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "dunecity.icns"
        MACOSX_BUNDLE_GUI_IDENTIFIER "net.dunecity.DuneCity"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/IDE/xCode/Info.plist"
        MACOSX_BUNDLE_COPYRIGHT "© Dune City Team"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "net.dunecity.DuneCity"
    )
    
    # Add icon to bundle
    set_source_files_properties(${CMAKE_SOURCE_DIR}/dunecity.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(dunecity PRIVATE ${CMAKE_SOURCE_DIR}/dunecity.icns)
elseif(ANDROID)
    add_library(dunecity SHARED)
    set_target_properties(dunecity PROPERTIES OUTPUT_NAME main)
else()
    add_executable(dunecity)
endif()

# Enable Win32 subsystem on Windows
if(WIN32)
    if(MSVC)
        # Add Windows resource file with icon for MSVC
        target_sources(dunecity PRIVATE "${CMAKE_SOURCE_DIR}/resource.rc")
        # Set application icon properties for MSVC
        set_target_properties(dunecity PROPERTIES
            VS_DPI_AWARE "PerMonitor"
            WIN32_EXECUTABLE TRUE)
        
        target_link_options(dunecity PRIVATE
            $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>
            $<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>
        )
    else()
        # For MinGW, add the resource file with the icon
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/dunecity.rc"
            "${CMAKE_CURRENT_BINARY_DIR}/dunecity.rc"
            COPYONLY
        )
        target_sources(dunecity PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/dunecity.rc")
        
        # MinGW equivalent for Windows subsystem
        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            target_link_options(dunecity PRIVATE -mwindows)
        else()
            target_link_options(dunecity PRIVATE -mconsole)
        endif()
    endif()
    
    # When using vcpkg, CMake will automatically handle DLL copying
    # Add a post-build message for confirmation
    add_custom_command(TARGET dunecity POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "vcpkg will automatically copy required DLLs to output directory"
        COMMENT "Checking for SDL2 DLLs..."
    )
endif()

# Precompiled headers
if(DUNECITY_ENABLE_PCH AND WIN32 AND MSVC)
    target_precompile_headers(dunecity
        PRIVATE
            ${PCH_HEADER}
    )
    target_sources(dunecity
        PRIVATE
            ${PCH_SOURCE}
    )
endif()

# All source files
target_sources(dunecity
    PRIVATE
        # Root sources
        AStarSearch.cpp
        PathWorkerPool.cpp
        Bullet.cpp
        Choam.cpp
        Command.cpp
        CommandManager.cpp
        CityScore.cpp
        CrashHandler.cpp
        CursorManager.cpp
        Explosion.cpp
        Game.cpp
        GameInitSettings.cpp
        GameInterface.cpp
        House.cpp
        Map.cpp
        SpatialGrid.cpp
        MapSeed.cpp
        globals.cpp
        main.cpp
        mmath.cpp
        ObjectBase.cpp
        ObjectData.cpp
        ObjectManager.cpp
        ObjectPointer.cpp
        RadarView.cpp
        ScreenBorder.cpp
        sand.cpp
        SoundPlayer.cpp
        Tile.cpp

        # Audio
        audio/sounds.cpp

        # INIMap
        INIMap/INIMapLoader.cpp
        INIMap/INIMapEditorLoader.cpp
        INIMap/INIMapPreviewCreator.cpp

        # FileClasses
        FileClasses/Animation.cpp
        FileClasses/Cpsfile.cpp
        FileClasses/Decode.cpp
        FileClasses/FileManager.cpp
        FileClasses/FontManager.cpp
        FileClasses/GFXManager.cpp
        FileClasses/EnhancedAtlasCache.cpp
        FileClasses/Icnfile.cpp
        FileClasses/IndexedTextFile.cpp
        FileClasses/INIFile.cpp
        FileClasses/LoadSavePNG.cpp
        FileClasses/lodepng.cpp
        FileClasses/MentatTextFile.cpp
        FileClasses/Pakfile.cpp
        FileClasses/Palfile.cpp
        FileClasses/PictureFactory.cpp
        FileClasses/POFile.cpp
        FileClasses/SFXManager.cpp
        FileClasses/Shpfile.cpp
        FileClasses/TextManager.cpp
        FileClasses/TTFFont.cpp
        FileClasses/Vocfile.cpp
        FileClasses/Wsafile.cpp
        FileClasses/adl/sound_adlib.cpp
        FileClasses/adl/surroundopl.cpp
        FileClasses/adl/woodyopl.cpp
        FileClasses/music/ADLPlayer.cpp
        FileClasses/music/DirectoryPlayer.cpp
        FileClasses/music/XMIPlayer.cpp
        FileClasses/xmidi/xmidi.cpp

        # GUI
        GUI/Button.cpp
        GUI/GUIStyle.cpp
        GUI/Widget.cpp
        GUI/Window.cpp
        GUI/ScrollBar.cpp
        GUI/ListBox.cpp
        GUI/DropDownBox.cpp
        GUI/TextView.cpp
        GUI/RadioButtonManager.cpp
        GUI/dune/ChatManager.cpp
        GUI/dune/FeedbackWindow.cpp
        GUI/dune/CityBudgetWindow.cpp
        GUI/dune/DuneStyle.cpp
        GUI/dune/GameOptionsWindow.cpp
        GUI/dune/LoadSaveWindow.cpp
        GUI/dune/InGameMenu.cpp
        GUI/dune/InGameSettingsMenu.cpp
        GUI/dune/NewsTicker.cpp
        GUI/dune/MessageTicker.cpp
        GUI/dune/BuilderList.cpp
        GUI/dune/DisasterNotification.cpp
        GUI/dune/WaitingForOtherPlayers.cpp

        # Menu
        Menu/MenuBase.cpp
        Menu/PlaySetup.cpp
        Menu/MainMenu.cpp
        Menu/DisplayMenu.cpp
        Menu/SinglePlayerMenu.cpp
        Menu/SinglePlayerSkirmishMenu.cpp
        Menu/CustomGameMenu.cpp
        Menu/CustomGamePlayers.cpp
        Menu/CustomGameStatsMenu.cpp
        Menu/OptionsMenu.cpp
        Menu/AboutMenu.cpp
        Menu/HouseChoiceMenu.cpp
        Menu/MentatMenu.cpp
        Menu/MultiPlayerMenu.cpp
        Menu/CrossplayMenu.cpp
        Menu/BriefingMenu.cpp
        Menu/HouseChoiceInfoMenu.cpp
        Menu/MentatHelp.cpp
        Menu/MapChoice.cpp
        Menu/CampaignStatsMenu.cpp
        Menu/ModMenu.cpp
        Menu/ModEditorMenu.cpp
        Menu/Dune2RAssetMenu.cpp
        Menu/Dune2REditorMenu.cpp
        Menu/HowToPlayMenu.cpp

        # Network
        Network/LANGameFinderAndAnnouncer.cpp
        Network/NetworkManager.cpp
    Network/WebRtcPeerLifecycle.cpp
        Network/LateJoin.cpp
        Network/ObserverStream.cpp
        Network/SnapshotTransfer.cpp
        Network/GamePayloadRouter.cpp
        Network/RoomRelayClient.cpp
        Network/RoomAdmissionClient.cpp
        Network/FeedbackClient.cpp

        Network/DirectRoomTransport.cpp
        Network/BoundedHttpClient.cpp
        Network/ENetHttp.cpp
        Network/MetaServerClient.cpp
        Network/VersionChecker.cpp
        Network/UPnPManager.cpp
        Network/StunClient.cpp

        # Players
        players/Player.cpp
        players/HumanPlayer.cpp
        players/PlayerFactory.cpp
        players/AIPlayer.cpp
        players/CampaignAIPlayer.cpp
        players/AIDecisionLog.cpp
        players/DeviationReward.cpp
        players/QuantBot.cpp
        players/QuantBotCampaign.cpp
        players/Mentat.cpp
        players/MentatBuildOrderMentat.cpp
        players/QuantBotConfig.cpp
        players/SmartBot.cpp

        # Mod System
        mod/Dune2RAssetManager.cpp
        mod/WorkshopStore.cpp
        mod/Workshop.cpp
        mod/WorkshopClient.cpp
        Menu/WorkshopCommunityMenu.cpp
        mod/ModManager.cpp

        # Structures
        structures/StructureBase.cpp
        structures/BuilderBase.cpp
        structures/TurretBase.cpp
        structures/Barracks.cpp
        structures/ConstructionYard.cpp
        structures/GunTurret.cpp
        structures/HeavyFactory.cpp
        structures/HighTechFactory.cpp
        structures/IX.cpp
        structures/LightFactory.cpp
        structures/Palace.cpp
        structures/Radar.cpp
        structures/Refinery.cpp
        structures/RepairYard.cpp
        structures/RocketTurret.cpp
        structures/Silo.cpp
        structures/StarPort.cpp
        structures/Wall.cpp
        structures/WindTrap.cpp
        structures/AdvancedWindTrap.cpp
        structures/Worfinery.cpp
        structures/TechCenter.cpp
        structures/Scoutpost.cpp
        structures/LoveFactory.cpp
        structures/ChaosFactory.cpp
        structures/WOR.cpp
        structures/NuclearPlant.cpp
        structures/PoliceStation.cpp
        structures/Stadium.cpp
        structures/Airport.cpp
        structures/ZoneStructure.cpp

        # Units
        units/UnitBase.cpp
        units/AirUnit.cpp
        units/GroundUnit.cpp
        units/InfantryBase.cpp
        units/TrackedUnit.cpp
        units/TankBase.cpp
        units/Carryall.cpp
    units/ChemicalCarryall.cpp
        units/Devastator.cpp
        units/Deviator.cpp
        units/Frigate.cpp
        units/Harvester.cpp
        units/RebelHarvester.cpp
        units/Launcher.cpp
        units/MCV.cpp
        units/Ornithopter.cpp
        units/Quad.cpp
        units/RaiderTrike.cpp
        units/Saboteur.cpp
        units/SandWorm.cpp
        units/SiegeTank.cpp
        units/Soldier.cpp
        units/SonicTank.cpp
        units/Tank.cpp
        units/Trike.cpp
        units/Trooper.cpp
        units/AmbientAirplane.cpp
        units/AmbientHelicopter.cpp
        units/RocketTrike.cpp
        units/SonicTrike.cpp
        units/FlameTank.cpp
        units/EliteLauncher.cpp
        units/EliteSiegeTank.cpp
        units/ChemicalSiegeTank.cpp

        # CutScenes
        CutScenes/CutScene.cpp
        CutScenes/Scene.cpp
        CutScenes/Intro.cpp
        CutScenes/Meanwhile.cpp
        CutScenes/Finale.cpp
        CutScenes/VideoEvent.cpp
        CutScenes/WSAVideoEvent.cpp
        CutScenes/FadeOutVideoEvent.cpp
        CutScenes/FadeInVideoEvent.cpp
        CutScenes/HoldPictureVideoEvent.cpp
        CutScenes/CrossBlendVideoEvent.cpp
        CutScenes/TextEvent.cpp
        CutScenes/CutSceneTrigger.cpp

        # MapEditor
        MapEditor/ChoamWindow.cpp
        MapEditor/MapEditor.cpp
        MapEditor/MapEditorInterface.cpp
        MapEditor/MapEditorOperation.cpp
        MapEditor/MapGenerator.cpp
        MapEditor/MapEditorRadarView.cpp
        MapEditor/MapSettingsWindow.cpp
        MapEditor/MapMirror.cpp
        MapEditor/NewMapWindow.cpp
        MapEditor/LoadMapWindow.cpp
        MapEditor/PlayerSettingsWindow.cpp
        MapEditor/ReinforcementsWindow.cpp
        MapEditor/TeamsWindow.cpp

        # Trigger
        Trigger/TriggerManager.cpp
        Trigger/ReinforcementTrigger.cpp
        Trigger/TimeoutTrigger.cpp

        # DuneCity - city-building simulation layer
        dunecity/CitySimulation.cpp
        dunecity/CityEffectsRuntime.cpp
        dunecity/PowerGrid.cpp
        dunecity/ZoneSimulation.cpp
        dunecity/TrafficSimulation.cpp
        dunecity/CityScanner.cpp
        dunecity/CityBudget.cpp
        dunecity/CityEvaluation.cpp

        # misc
        misc/draw_util.cpp
        misc/FileSystem.cpp
        misc/fnkdat.cpp
        misc/format.cpp
        misc/FrameYield.cpp
        misc/IFileStream.cpp
        misc/md5.cpp
        misc/OFileStream.cpp

        misc/sound_util.cpp
        misc/string_util.cpp
        misc/Scaler.cpp
        misc/TouchInput.cpp
        misc/WebRuntime.cpp
)

# C source files
target_sources(dunecity
    PRIVATE
        # enet
        enet/callbacks.c
        enet/compress.c
        enet/host.c
        enet/list.c
        enet/packet.c
        enet/peer.c
        enet/protocol.c
        enet/unix.c
        enet/win32.c

        # fixmath
        fixmath/fix16.c
        fixmath/fix16_exp.c
        fixmath/fix16_sqrt.c
        fixmath/fix16_str.c
        fixmath/fix16_trig.c
        fixmath/fix32.c
        fixmath/fix32_exp.c
        fixmath/fix32_sqrt.c
        fixmath/fix32_str.c
        fixmath/fix32_trig.c
)

# Platform-specific source files
if(APPLE)
    target_sources(dunecity
        PRIVATE
            ${CMAKE_SOURCE_DIR}/IDE/xCode/MacFunctions.m
    )
    # Enable Objective-C ARC (Automatic Reference Counting)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/IDE/xCode/MacFunctions.m
        PROPERTIES
            COMPILE_FLAGS "-fobjc-arc"
    )
endif()

# Source groups for better organization in IDEs
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source Files" FILES ${sources})

# Include directories
target_include_directories(dunecity PRIVATE
    ${CMAKE_BINARY_DIR}/include   # generated config.h (has correct PROJECT_VERSION)
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# p2pkit SDK headers (path + install guard defined in the top-level
# CMakeLists.txt): the packet streams and the WebRTC transport alias in
# include/Network/ are thin adapters over them.
target_include_directories(dunecity PRIVATE
    "${DUNECITY_P2PKIT_DIR}/emscripten/include"
)

# Platform-specific include directories
if(APPLE)
    target_include_directories(dunecity PRIVATE
        ${CMAKE_SOURCE_DIR}/IDE/xCode
    )
endif()

# Find native-only integrations. Browser builds use Fetch and cannot configure
# a viewer's router through UPnP.
if(NOT EMSCRIPTEN)
    find_package(miniupnpc CONFIG QUIET)
    if(NOT miniupnpc_FOUND)
        find_library(MINIUPNPC_LIBRARY miniupnpc PATHS ${CMAKE_PREFIX_PATH}/lib)
        find_path(MINIUPNPC_INCLUDE_DIR miniupnpc/miniupnpc.h PATHS ${CMAKE_PREFIX_PATH}/include)
        if(MINIUPNPC_LIBRARY AND MINIUPNPC_INCLUDE_DIR)
            add_library(miniupnpc::miniupnpc UNKNOWN IMPORTED)
            set_target_properties(miniupnpc::miniupnpc PROPERTIES
                IMPORTED_LOCATION "${MINIUPNPC_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${MINIUPNPC_INCLUDE_DIR}")
        endif()
    endif()

    find_library(DISCORD_RPC_LIBRARY discord-rpc PATHS ${CMAKE_PREFIX_PATH}/lib)
    if(DISCORD_RPC_LIBRARY)
        target_sources(dunecity PRIVATE misc/DiscordManager.cpp)
        target_compile_definitions(dunecity PRIVATE HAS_DISCORD_RPC)
    endif()
endif()

# The crossplay relay transport has one implementation per platform. Only the matching one is
# compiled: each file is also guarded internally, but listing them conditionally keeps the other
# platform's headers (libcurl, emscripten/websocket.h) out of the build entirely.
#
# The HTTPS polling transport is split the same way, plus a platform-independent state machine
# that both backends share. That file is where the sequencing, retry and pacing rules live, and
# it is the one tests/RelayHttpTransportTestCase drives against a scripted backend.
target_sources(dunecity PRIVATE Network/RelayHttpTransport.cpp)

# The direct peer-to-peer transport has one backend per runtime and they have to interoperate:
# natively a real libdatachannel data channel, in the browser P2PKit's RTCTransport reached
# through platform/web/src/dune-direct-bridge.ts. The wire format they share is
# include/Network/P2PWireFraming.h, which is header-only precisely so the tests can drive the
# real encoder and decoder without either backend.
if(EMSCRIPTEN)
    target_sources(dunecity PRIVATE
        Network/RelayWebSocketEmscripten.cpp
        Network/RelayHttpTransportEmscripten.cpp
        Network/DirectPeerConnectionEmscripten.cpp)

    # WebRTC multiplayer transport (browser only): NetworkManager reaches the
    # browser bridge through include/Network/WebRtcTransport.h, an alias of the
    # header-only p2pkit_wasm::WebRtcTransport from the installed p2pkit SDK
    # (no WebRtcTransport.cpp of our own). The bridge itself is the SDK's
    # --js-library glue plus DuneCity's adapter, linked in the target_link_options
    # block below.
else()
    target_sources(dunecity PRIVATE
        Network/RelayWebSocketCurl.cpp
        Network/RelayHttpTransportCurl.cpp
        Network/DirectPeerConnectionLibdatachannel.cpp)

    if(DUNECITY_DIRECT_P2P)
        target_link_libraries(dunecity PRIVATE ${DUNECITY_LIBDATACHANNEL_TARGET})
        target_compile_definitions(dunecity PRIVATE DUNECITY_HAVE_LIBDATACHANNEL)
    else()
        message(STATUS "libdatachannel is disabled; this build cannot play directly online")
    endif()

    # libcurl gained its WebSocket API in 7.86. Older libcurl still builds - the transport
    # compiles out and the game says online play is unavailable - but it is worth being loud
    # about, because the macOS system libcurl is also built without the ws/wss handlers even
    # when its headers are new enough. Configure with -DCURL_ROOT=/opt/homebrew/opt/curl on
    # macOS to pick up a libcurl that has them.
    if(CURL_VERSION_STRING AND CURL_VERSION_STRING VERSION_LESS "7.86.0")
        message(STATUS "libcurl ${CURL_VERSION_STRING} has no WebSocket API; "
                       "crossplay will be unavailable in this build")
    else()
        message(STATUS "libcurl ${CURL_VERSION_STRING} provides the WebSocket API used by crossplay")
    endif()
endif()

# Link libraries
if(EMSCRIPTEN)
    set_target_properties(dunecity PROPERTIES SUFFIX ".html")
    # The production shell loads these external files before the generated
    # runtime. Include them in raw CMake builds as well as packaged releases.
    foreach(WEB_SHELL_ASSET shell.js shell.css)
        configure_file("${CMAKE_SOURCE_DIR}/web/${WEB_SHELL_ASSET}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${WEB_SHELL_ASSET}" COPYONLY)
    endforeach()

    # The direct-play bridge bundle. It is generated from platform/web/src/dune-direct-bridge.ts
    # and the pinned P2PKit dependency by tools/p2pkit-build, and committed.
    # Install the SDK before configuring; this bundle is copied, never built here.
    # If missing, the game reports that this page cannot open direct connections.
    if(EXISTS "${CMAKE_SOURCE_DIR}/platform/web/p2p-direct.js")
        configure_file("${CMAKE_SOURCE_DIR}/platform/web/p2p-direct.js"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/p2p-direct.js" COPYONLY)
    else()
        message(STATUS "platform/web/p2p-direct.js is absent; "
                       "the browser build will have no direct-play bridge")
    endif()
    set_property(TARGET dunecity APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_SOURCE_DIR}/web/shell.html"
        "${CMAKE_SOURCE_DIR}/platform/web/dist/p2pkit.iife.js"
        "${CMAKE_SOURCE_DIR}/platform/web/dunecity_webrtc_config.js"
        "${DUNECITY_P2PKIT_DIR}/emscripten/js/p2pkit_webrtc_glue.cjs")
    # Matchmaking also needs the SDK runtime before the Emscripten module.
    # Package it after every link, including raw CMake/incremental builds;
    # relying on the optional wrapper left the website workflow without it.
    find_program(DUNECITY_NODE_EXECUTABLE NAMES node REQUIRED)
    set_property(TARGET dunecity APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/web/prepend-p2pkit.mjs")
    add_custom_command(TARGET dunecity POST_BUILD
        COMMAND "${DUNECITY_NODE_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/web/prepend-p2pkit.mjs"
            "${CMAKE_SOURCE_DIR}/platform/web/dist/p2pkit.iife.js"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/dunecity.js"
        COMMENT "Package the pinned matchmaking runtime"
        VERBATIM)
    target_compile_options(dunecity PRIVATE
        "SHELL:-sUSE_ZLIB=1"
        "SHELL:-sUSE_SDL=2"
        "SHELL:-sUSE_SDL_MIXER=2"
        "SHELL:-sUSE_SDL_TTF=2"
        "SHELL:-fexceptions"
        "SHELL:-sDISABLE_EXCEPTION_CATCHING=0"
    )
    target_link_options(dunecity PRIVATE
        # Keep Release C++ compilation at -O3, but use the -O2 wasm
        # optimizer pipeline. Binaryen's -O3 local2Stack pass grows prohibitively
        # on this Asyncify build (over 20 minutes / 12 GB in the release probe).
        "$<$<CONFIG:Release>:-O2>"
        "SHELL:-sUSE_ZLIB=1"
        "SHELL:-sUSE_SDL=2"
        "SHELL:-sUSE_SDL_MIXER=2"
        "SHELL:-sUSE_SDL_TTF=2"
        "SHELL:-fexceptions"
        "SHELL:-sDISABLE_EXCEPTION_CATCHING=0"
        "SHELL:-sASYNCIFY=1"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
        "SHELL:-sINITIAL_MEMORY=268435456"
        "SHELL:-sMAXIMUM_MEMORY=2147483648"
        "SHELL:-sSTACK_SIZE=5242880"
        "SHELL:-sFORCE_FILESYSTEM=1"
        "SHELL:-sEXIT_RUNTIME=0"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=['FS']"
        "SHELL:-lidbfs.js"
        # Crossplay in the browser: the WebSocket API for the gameplay connection and the Fetch
        # API for room admission. Both are asynchronous; no synchronous request is used.
        "SHELL:-lwebsocket.js"
        "SHELL:-sFETCH=1"
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/data@/"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/config@/config"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/imported_sprites@/imported_sprites"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/mods/Tornie@/mods/Tornie"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/mods/Dune2R@/mods/Dune2R"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/mods/dunecity@/mods/dunecity"
        # Keep the Dune2R mod/catalog available; its optional art downloads
        # separately, just as in the native packages.
        "SHELL:--exclude-file ${CMAKE_SOURCE_DIR}/mods/Dune2R/graphics_hd/units"
        "SHELL:--exclude-file ${CMAKE_SOURCE_DIR}/mods/Dune2R/graphics_compact/objpics"
        # WebRTC multiplayer bridge (browser only), reached from C++ through
        # include/Network/WebRtcTransport.h. The generic transport glue is the
        # p2pkit SDK's own --js-library (installed dependency, not a copy);
        # dunecity_webrtc_config.js is DuneCity's adapter on top of it (lobby
        # wiring, Emscripten event pump, stats).
        "SHELL:--js-library ${DUNECITY_P2PKIT_DIR}/emscripten/js/p2pkit_webrtc_glue.cjs"
        "SHELL:--js-library ${CMAKE_SOURCE_DIR}/platform/web/dunecity_webrtc_config.js"
    )
    file(GLOB_RECURSE WEB_MOD_FILES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/mods/Tornie/*"
        "${CMAKE_SOURCE_DIR}/mods/Dune2R/*"
        "${CMAKE_SOURCE_DIR}/mods/dunecity/*")
    list(FILTER WEB_MOD_FILES EXCLUDE REGEX "/graphics_hd/units/|/graphics_compact/objpics/")
    set_property(TARGET dunecity APPEND PROPERTY LINK_DEPENDS ${WEB_MOD_FILES})
else()
    # vcpkg provides modern CMake targets for SDL2.
    target_link_libraries(dunecity
        PRIVATE
            $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
            $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
            $<IF:$<TARGET_EXISTS:SDL2_ttf::SDL2_ttf>,SDL2_ttf::SDL2_ttf,SDL2_ttf::SDL2_ttf-static>
            $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
            CURL::libcurl
            ZLIB::ZLIB
            $<IF:$<TARGET_EXISTS:miniupnpc::miniupnpc>,miniupnpc::miniupnpc,${MINIUPNPC_LIBRARY}>
            $<$<BOOL:${DISCORD_RPC_LIBRARY}>:${DISCORD_RPC_LIBRARY}>
            $<$<PLATFORM_ID:Android>:c++_shared>
    )
endif()

if(WIN32)
    target_link_libraries(dunecity
        PRIVATE
            ws2_32
            winmm
            imm32
            version
            dbghelp
    )
endif()

# macOS-specific frameworks
if(APPLE)
    target_link_libraries(dunecity
        PRIVATE
            "-framework Cocoa"
            "-framework Foundation"
            "-framework CoreFoundation"
    )
endif()

include(${CMAKE_SOURCE_DIR}/cmake/DesktopUpdater.cmake)

# Compiler options
target_compile_options(dunecity
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -pedantic>
)

# Data directory handling
set(DATA_DIR "${CMAKE_SOURCE_DIR}/data")
set(MODS_DIR "${CMAKE_SOURCE_DIR}/mods")

if(NOT EMSCRIPTEN)
    # These balance files are copied by POST_BUILD below. A data-only edit must
    # trigger that packaging step too, or the local app keeps stale weapon data.
    file(GLOB BUNDLED_OBJECT_DATA CONFIGURE_DEPENDS
        "${MODS_DIR}/*/ObjectData.ini" "${MODS_DIR}/*/checksums.sha256")
    set_property(TARGET dunecity APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_SOURCE_DIR}/config/ObjectData.ini.default" ${BUNDLED_OBJECT_DATA})
endif()

function(add_copy_mods DEST_DIR)
    if(EXISTS "${MODS_DIR}")
        add_custom_command(
            TARGET dunecity
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${MODS_DIR}"
                "${DEST_DIR}/mods"
            COMMAND ${CMAKE_COMMAND} -E remove_directory
                "${DEST_DIR}/mods/Dune2R/graphics_hd/units"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${DEST_DIR}/mods/Dune2R/graphics_hd/units"
            COMMAND ${CMAKE_COMMAND} -E remove_directory
                "${DEST_DIR}/mods/Dune2R/graphics_compact/objpics"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${DEST_DIR}/mods/Dune2R/graphics_compact/objpics"
            COMMENT "Copying selectable mods"
        )
    endif()
endfunction()

if(APPLE)
    # For macOS bundle, copy data files directly to Resources/ (not Resources/data/)
    # This matches the Xcode project structure where .PAK files are in Resources root
    set(BUNDLE_RESOURCES_DIR "$<TARGET_BUNDLE_CONTENT_DIR:dunecity>/Resources")
    add_custom_command(
        TARGET dunecity
        POST_BUILD
        # Copy all .PAK files and other data to Resources root
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${DATA_DIR}"
            "${BUNDLE_RESOURCES_DIR}"
        # Create config subdirectory
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${BUNDLE_RESOURCES_DIR}/config"
        # Copy config template files (.ini.default and .ini) to Resources/config/
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/config/ObjectData.ini.default"
            "${CMAKE_SOURCE_DIR}/config/QuantBot Config.ini.default"
            "${CMAKE_SOURCE_DIR}/config/Dune City.ini"
            "${CMAKE_SOURCE_DIR}/config/README.md"
            "${BUNDLE_RESOURCES_DIR}/config/"
        COMMENT "Copying data and config files into app bundle Resources"
    )

    add_copy_mods("${BUNDLE_RESOURCES_DIR}")

    # Copy imported Micropolis-derived zone sprites into the bundle so they
    # are found by getDuneLegacyDataDir() + "imported_sprites/..." at runtime.
    set(IMPORTED_SPRITES_SRC "${CMAKE_SOURCE_DIR}/imported_sprites")
    if(EXISTS "${IMPORTED_SPRITES_SRC}")
        add_custom_command(
            TARGET dunecity
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${IMPORTED_SPRITES_SRC}"
                "${BUNDLE_RESOURCES_DIR}/imported_sprites"
            COMMENT "Copying imported sprites into app bundle Resources"
        )
    endif()

    # Code sign the app bundle with ad-hoc signature
    # This is required for macOS Gatekeeper to allow the app to run when downloaded
    # Without proper signing, users get "app is damaged and can't be opened" error
    add_custom_command(
        TARGET dunecity
        POST_BUILD
        COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:dunecity>"
        COMMENT "Code signing app bundle with ad-hoc signature"
    )
elseif(NOT EMSCRIPTEN)
    if(WIN32)
        set(BUILD_RUNTIME_DIR "$<TARGET_FILE_DIR:dunecity>")
    else()
        set(BUILD_RUNTIME_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endif()
    set(BUILD_DATA_DIR "${BUILD_RUNTIME_DIR}/data")
    add_custom_command(
        TARGET dunecity
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${DATA_DIR}"
            "${BUILD_DATA_DIR}"
        COMMENT "Copying data directory to build directory"
    )

    # Copy config template files next to the binary so the mod system can seed
    # ObjectData.ini etc. For local builds, DUNELEGACY_DATADIR points at a
    # non-existent install path; the fallback in FileSystem.cpp uses
    # SDL_GetBasePath() which resolves to this directory.
    add_custom_command(
        TARGET dunecity
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUILD_RUNTIME_DIR}/config"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/config/ObjectData.ini.default"
            "${CMAKE_SOURCE_DIR}/config/QuantBot Config.ini.default"
            "${CMAKE_SOURCE_DIR}/config/Dune City.ini"
            "${BUILD_RUNTIME_DIR}/config/"
        COMMENT "Copying config template files to build output"
    )

    add_copy_mods("${BUILD_RUNTIME_DIR}")

    # Copy imported zone sprites next to the binary for non-bundle builds
    set(IMPORTED_SPRITES_SRC "${CMAKE_SOURCE_DIR}/imported_sprites")
    if(EXISTS "${IMPORTED_SPRITES_SRC}")
        add_custom_command(
            TARGET dunecity
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${IMPORTED_SPRITES_SRC}"
                "${BUILD_RUNTIME_DIR}/imported_sprites"
            COMMENT "Copying imported sprites to build output"
        )
    endif()
endif()

# Install data directory
# For macOS bundles, data files are already inside the .app via POST_BUILD commands
if(NOT EMSCRIPTEN)
if(WIN32)
    # Windows: install PAK files and data to share/DuneLegacy where game expects them
    # Use data directory which has all PAK files
    
    # Install all PAK files from data directory to root installation directory
    install(
        DIRECTORY ${DATA_DIR}/
        DESTINATION .
        FILES_MATCHING PATTERN "*.PAK"
    )
    
    # Install locale files
    install(
        DIRECTORY ${DATA_DIR}/locale
        DESTINATION .
    )
    
    # Install maps
    install(
        DIRECTORY ${DATA_DIR}/maps
        DESTINATION .
    )
    
    # Install Dune2-Versions.txt
    install(
        FILES "${DATA_DIR}/Dune2-Versions.txt"
        DESTINATION .
    )

    # Neutral's herald is a loose runtime graphic rather than a PAK entry.
    install(
        FILES "${DATA_DIR}/HeraldNeu.png"
        DESTINATION data
    )
    
    # Vanilla-specific loose overrides are loaded from the runtime data
    # directory. Development builds already copy them there, but CPack used
    # to omit them and silently fall back to the original PAK resources.
    install(
        FILES
            "${DATA_DIR}/ANEU.VOC"
            "${DATA_DIR}/RREBELS.voc"
            "${DATA_DIR}/HeraldNeuMask.png"
            "${DATA_DIR}/HeraldRebels.png"
            "${DATA_DIR}/HeraldRebelsMask.png"
            "${DATA_DIR}/ChaniMentat.png"
            "${DATA_DIR}/ChaniMentatEyes.png"
            "${DATA_DIR}/ChaniMentatMouth.png"
            "${DATA_DIR}/PalaceRebelsIcon.png"
            "${DATA_DIR}/Custom_IBM.pal"
        DESTINATION data
    )
        # Install config files from config directory to root installation directory
    install(
        DIRECTORY ${CMAKE_SOURCE_DIR}/config
        DESTINATION .
    )

    # libcurl builds that use OpenSSL do not automatically consult the
    # Windows certificate store. Ship the Mozilla CA bundle at the runtime
    # path used by ENetHttp so MetaServer HTTPS works on Windows 10 as well.
    install(
        FILES "${DATA_DIR}/cacert.pem"
        DESTINATION data
    )
elseif(NOT APPLE)
    # Linux: use FHS structure
    install(
        DIRECTORY ${DATA_DIR}/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}
        FILES_MATCHING PATTERN "*.*"
    )
endif()

# Install imported sprite assets (Micropolis-derived civic/zone PNGs).
# macOS bundles already include these via the POST_BUILD copy above.
# Windows and Linux packaged builds need an explicit install rule so CPack
# bundles the PNGs into the installer — without this, getDuneLegacyDataDir()
# + "imported_sprites/..." finds nothing and civic sprites fall back to
# placeholders (or crash on older builds).
set(IMPORTED_SPRITES_SRC "${CMAKE_SOURCE_DIR}/imported_sprites")
if(EXISTS "${IMPORTED_SPRITES_SRC}")
    if(WIN32)
        install(
            DIRECTORY "${IMPORTED_SPRITES_SRC}/"
            DESTINATION imported_sprites
        )
    elseif(NOT APPLE)
        install(
            DIRECTORY "${IMPORTED_SPRITES_SRC}/"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/imported_sprites
        )
    endif()
endif()

if(EXISTS "${MODS_DIR}" AND NOT APPLE)
    if(WIN32)
        set(MODS_INSTALL_BASE ".")
    else()
        set(MODS_INSTALL_BASE "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}")
    endif()

    install(
        DIRECTORY "${MODS_DIR}/"
        DESTINATION "${MODS_INSTALL_BASE}/mods"
        PATTERN "Dune2R/graphics_hd/units/*" EXCLUDE
        PATTERN "Dune2R/graphics_compact/objpics/*" EXCLUDE
    )
endif()

# Install executable
if(WIN32)
    # Windows: install to root directory
    # Use CMake build output (not VS IDE output)
    install(TARGETS dunecity
        RUNTIME DESTINATION .
    )
    # vcpkg app-local deployment copies its DLLs but not the MSVC runtime.
    # Bundle the redistributable runtime beside the game so a clean Windows
    # machine does not need Visual Studio or a separate VC++ installer.
    if(MSVC)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
        include(InstallRequiredSystemLibraries)
    endif()
    # Install vcpkg DLLs for Windows
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR NOT CMAKE_BUILD_TYPE)
        install(DIRECTORY "$<TARGET_FILE_DIR:dunecity>/"
            DESTINATION .
            FILES_MATCHING PATTERN "*.dll"
        )
    endif()
elseif(APPLE)
    # macOS: install as bundle
    install(TARGETS dunecity
        BUNDLE DESTINATION .
    )

    # Make release bundles independent of Homebrew and vcpkg paths. CPack
    # invokes the install rules in a staging tree, so BundleUtilities copies
    # every non-system dylib into Contents/Frameworks and rewrites its load
    # commands before the final signature is applied.
    set(DUNECITY_MACOS_DEPENDENCY_DIRS "${dunecity_sparkle_SOURCE_DIR}")
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        list(APPEND DUNECITY_MACOS_DEPENDENCY_DIRS
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
    endif()

    # Homebrew SDL2 may be sdl2-compat, which dlopens SDL3 instead of linking
    # it. BundleUtilities cannot discover that runtime-only dependency.
    find_library(DUNECITY_MACOS_SDL3_LIBRARY NAMES SDL3)
    if(DUNECITY_MACOS_SDL3_LIBRARY)
        get_filename_component(_sdl3_real "${DUNECITY_MACOS_SDL3_LIBRARY}" REALPATH)
        get_filename_component(_sdl3_libdir "${_sdl3_real}" DIRECTORY)
        find_file(DUNECITY_MACOS_SDL3_LICENSE NAMES LICENSE.txt
            HINTS "${_sdl3_libdir}/.." "${_sdl3_libdir}/../share/licenses/SDL3"
            NO_DEFAULT_PATH)
    endif()

    # Re-sign last because dependency fixup changes the executable and copied
    # libraries. The signature is ad-hoc until Developer ID credentials are
    # added to the runner, but it is internally valid and supports Open Anyway.
    install(CODE "
        include(BundleUtilities)
        set(BU_CHMOD_BUNDLE_ITEMS ON)
        set(BU_COPY_FULL_FRAMEWORK_CONTENTS ON)
        set(_runtime_libraries \"\")
        execute_process(COMMAND /usr/bin/strings \"$<TARGET_FILE:SDL2::SDL2>\"
            OUTPUT_VARIABLE _sdl2_strings RESULT_VARIABLE _sdl2_result)
        if(NOT _sdl2_result EQUAL 0)
            message(FATAL_ERROR \"Could not inspect SDL2 runtime requirements\")
        endif()
        if(_sdl2_strings MATCHES \"SDL2COMPAT_DEBUG_LOGGING\")
            if(NOT EXISTS \"${DUNECITY_MACOS_SDL3_LIBRARY}\" OR NOT EXISTS \"${DUNECITY_MACOS_SDL3_LICENSE}\")
                message(FATAL_ERROR \"sdl2-compat requires SDL3 and its license for packaging\")
            endif()
            set(_sdl3_bundle \"\${CMAKE_INSTALL_PREFIX}/dunecity.app/Contents/Frameworks/libSDL3.dylib\")
            file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/dunecity.app/Contents/Frameworks\")
            configure_file(\"${DUNECITY_MACOS_SDL3_LIBRARY}\" \"\${_sdl3_bundle}\" COPYONLY)
            list(APPEND _runtime_libraries \"\${_sdl3_bundle}\")
            file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/dunecity.app/Contents/Resources/licenses\")
            configure_file(\"${DUNECITY_MACOS_SDL3_LICENSE}\"
                \"\${CMAKE_INSTALL_PREFIX}/dunecity.app/Contents/Resources/licenses/SDL3-LICENSE.txt\" COPYONLY)
        endif()
        message(STATUS \"Bundling macOS runtime dependencies...\")
        fixup_bundle(
            \"\${CMAKE_INSTALL_PREFIX}/dunecity.app\"
            \"\${_runtime_libraries}\"
            \"${DUNECITY_MACOS_DEPENDENCY_DIRS}\"
        )
        # BundleUtilities flattens framework-root executable symlinks. Restore
        # Sparkle's complete, already portable framework before signing, keeping
        # the game's rewritten dependency paths and the bundled game dylibs.
        if(EXISTS \"${dunecity_sparkle_SOURCE_DIR}/Sparkle.framework\")
            file(REMOVE_RECURSE \"\${CMAKE_INSTALL_PREFIX}/dunecity.app/Contents/Frameworks/Sparkle.framework\")
            execute_process(COMMAND /usr/bin/ditto
                \"${dunecity_sparkle_SOURCE_DIR}/Sparkle.framework\"
                \"\${CMAKE_INSTALL_PREFIX}/dunecity.app/Contents/Frameworks/Sparkle.framework\"
                RESULT_VARIABLE _sparkle_copy_result)
            if(NOT _sparkle_copy_result EQUAL 0)
                message(FATAL_ERROR \"Could not preserve the Sparkle framework\")
            endif()
        endif()
        message(STATUS \"Re-codesigning installed dunecity.app...\")
        execute_process(
            COMMAND codesign --force --deep --sign - \"\${CMAKE_INSTALL_PREFIX}/dunecity.app\"
            RESULT_VARIABLE _codesign_result
        )
        if(NOT _codesign_result EQUAL 0)
            message(FATAL_ERROR \"Install-time codesign failed with exit code \${_codesign_result}\")
        endif()
        execute_process(
            COMMAND codesign --verify --deep --strict \"\${CMAKE_INSTALL_PREFIX}/dunecity.app\"
            RESULT_VARIABLE _verify_result
        )
        if(NOT _verify_result EQUAL 0)
            message(FATAL_ERROR \"Install-time codesign verification failed with exit code \${_verify_result}\")
        endif()
        message(STATUS \"Installed dunecity.app signature verified.\")
    ")
else()
    # Linux: set rpath so the installed binary finds bundled .so files relative
    # to itself rather than requiring system-installed or build-tree libraries.
    # $ORIGIN expands to the directory containing the binary at runtime, so
    # $ORIGIN/../lib/dunecity resolves to <prefix>/lib/dunecity.
    set_target_properties(dunecity PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib/dunecity"
        BUILD_WITH_INSTALL_RPATH FALSE
    )

    install(TARGETS dunecity
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    # Bundle all vcpkg-built shared libraries so the installed binary is
    # self-contained and does not depend on system SDL2/mixer/ttf/curl/etc.
    # VCPKG_INSTALLED_DIR and VCPKG_TARGET_TRIPLET are set by the vcpkg
    # toolchain file; skip silently if building without vcpkg.
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        install(DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/dunecity"
            FILES_MATCHING
                PATTERN "*.so"
                PATTERN "*.so.*"
            PATTERN "pkgconfig" EXCLUDE
        )
    endif()
endif()

# DUNELEGACY_DATADIR is set at the top of this file (before configure_file)
# only for Linux. Windows and macOS use SDL_GetBasePath() at runtime.
endif()
