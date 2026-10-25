cmake_minimum_required(VERSION 3.10)

# Find Catch2 package
find_package(Catch2 3 REQUIRED)

# Test executable
add_executable(dunelegacy_tests)

# Source files - new Catch2-based tests
# Note: Catch2WithMain provides main(), no testmain.cpp needed
target_sources(dunelegacy_tests
    PRIVATE
        WorkshopStoreTestCase.cpp
        # Random Skirmish generator: the pure generator over the real map generator/mirror.
        SkirmishGeneratorTestCase.cpp
        ${CMAKE_SOURCE_DIR}/src/MapEditor/SkirmishGenerator.cpp
        ${CMAKE_SOURCE_DIR}/src/MapEditor/MapGenerator.cpp
        ${CMAKE_SOURCE_DIR}/src/MapEditor/MapMirror.cpp
        ${CMAKE_SOURCE_DIR}/src/mod/WorkshopStore.cpp
        ${CMAKE_SOURCE_DIR}/src/mod/WorkshopDuplicates.cpp
        SnapshotTransferTestCase.cpp
        ${CMAKE_SOURCE_DIR}/src/Network/SnapshotTransfer.cpp
        CoopGameSettingsTestCase.cpp
        ${CMAKE_SOURCE_DIR}/src/GameInitSettings.cpp
        ${CMAKE_SOURCE_DIR}/src/misc/IFileStream.cpp
        MapCatalogueTestCase.cpp
        CityTrafficPolicyTestCase.cpp
        CitySpritePolicyTestCase.cpp
        CityAircraftPolicyTestCase.cpp
        RendererSizeTestCase.cpp
        CursorAppearanceTestCase.cpp
        GasDeviationPolicyTestCase.cpp
        OrdinaryDamagePolicyTestCase.cpp
        QuantBotBuildPolicyTestCase.cpp
        ArmyPosturePolicyTestCase.cpp
        BlobWavePolicyTestCase.cpp
        ForceConcentrationPolicyTestCase.cpp
        FormationPlannerTestCase.cpp
        FramePacingTestCase.cpp
        ${CMAKE_SOURCE_DIR}/src/FormationPlanner.cpp
        # The army-recovery test asserts that every new behavioural config field
        # reaches the multiplayer hash, so it drives the real QuantBotConfig.
        ${CMAKE_SOURCE_DIR}/src/players/QuantBotConfig.cpp
        ${CMAKE_SOURCE_DIR}/src/players/QuantBotStrategy.cpp
        QuantBotStrategyTestCase.cpp
        QuantBotCityPolicyTestCase.cpp
        QuantBotSchedulePolicyTestCase.cpp
        MatchControlTestCase.cpp
        QuantBotCityCampaignPolicyTestCase.cpp
        AIDecisionLogTestCase.cpp
        CityPlacementPolicyTestCase.cpp
        PathWorkerPoolTestCase.cpp
        ${CMAKE_SOURCE_DIR}/src/PathWorkerPool.cpp
        GroundAccessPolicyTestCase.cpp
        NuclearBlastPolicyTestCase.cpp
        RepairYardJobTestCase.cpp
        ${CMAKE_SOURCE_DIR}/src/players/AIDecisionLog.cpp
        PathBudgetTestCase/PathBudgetTestCase.cpp
        NetworkManagerTestCase/NetworkManagerTestCase.cpp
        NetworkHardeningTestCase/NetworkHardeningTestCase.cpp
        RelayProtocolTestCase/RelayProtocolTestCase.cpp
        # Direct peer-to-peer play. The framing and protocol headers are header-only so these
        # drive the real encoder and decoder; the transport test drives the real state machine
        # with a scripted signaling service and fake data channels.
        P2PWireFramingTestCase/P2PWireFramingTestCase.cpp
        P2PSignalingProtocolTestCase/P2PSignalingProtocolTestCase.cpp
        DirectRoomTransportTestCase/DirectRoomTransportTestCase.cpp
        ${CMAKE_SOURCE_DIR}/src/Network/DirectRoomTransport.cpp
        RelayProtocolTestCase/ContentCompatibilityTestCase.cpp
        CommandEmissionTestCase/CommandEmissionTestCase.cpp
        CommandBufferPolicyTestCase/CommandBufferPolicyTestCase.cpp
        # WebRTC multiplayer transport framing (browser multiplayer foundation).
        PacketStreamTestCase/PacketStreamTestCase.cpp
        GenericNinthHouseRegressionTestCase/GenericNinthHouseRegressionTestCase.cpp
        ZoneStructureTestCase/ZoneStructureTestCase.cpp
        RoadPlacementTestCase/RoadPlacementTestCase.cpp
        CityBudgetIntegrationTestCase/CityBudgetIntegrationTestCase.cpp
        BudgetAllocationTestCase/BudgetAllocationTestCase.cpp
        CityBuildLogicTestCase/CityBuildLogicTestCase.cpp
        OverlayLegendTestCase/OverlayLegendTestCase.cpp
        CityRoadTestCase/CityRoadTestCase.cpp
        RoadNetworkGrowthTestCase/RoadNetworkGrowthTestCase.cpp
        MilestoneTestCase/MilestoneTestCase.cpp
        CityStructuresTestCase/CityStructuresTestCase.cpp
        ConstructionYardResolverTestCase/ConstructionYardResolverTestCase.cpp
        UnitMovementPolicyTestCase/UnitMovementPolicyTestCase.cpp
        RtsControlsTestCase.cpp
        BuilderListLayoutTestCase.cpp
        PalaceUnitLimitRegressionTestCase/PalaceUnitLimitRegressionTestCase.cpp
        PlacementReservationTestCase/PlacementReservationTestCase.cpp
        ZonePowerTestCase/ZonePowerTestCase.cpp
        CityEffectsTestCase/CityEffectsTestCase.cpp
        ../src/fixmath/fix32.c
        ../src/fixmath/fix32_trig.c
        ../src/fixmath/fix32_sqrt.c
        ValueModelTestCase/ValueModelTestCase.cpp
        PoliceStationTestCase/PoliceStationTestCase.cpp
        SaveCompatTestCase/SaveCompatTestCase.cpp
        StructureDegradationTestCase/StructureDegradationTestCase.cpp
        NeutralHouseTestCase/NeutralHouseTestCase.cpp
        Dune2RAssetManagerTestCase/Dune2RAssetManagerTestCase.cpp
        EnhancedAtlasCacheTestCase/EnhancedAtlasCacheTestCase.cpp
        EnhancedBuildingGeometryTestCase/EnhancedBuildingGeometryTestCase.cpp
        Dune2RPresentationTestCase.cpp
        ${CMAKE_SOURCE_DIR}/src/ScreenBorder.cpp
        MenuLayoutTestCase/MenuLayoutTestCase.cpp
        MenuPaletteTestCase.cpp
        ${CMAKE_SOURCE_DIR}/src/mmath.cpp
        ${CMAKE_SOURCE_DIR}/src/FileClasses/EnhancedAtlasCache.cpp
        ${CMAKE_SOURCE_DIR}/src/FileClasses/lodepng.cpp
        # DuneCity core sources (needed for CitySimulation tests)
        ${CMAKE_SOURCE_DIR}/src/dunecity/CitySimulation.cpp
        CityEffectsRuntime_test_stub.cpp
        ${CMAKE_SOURCE_DIR}/src/dunecity/PowerGrid.cpp
        ${CMAKE_SOURCE_DIR}/src/dunecity/ZoneSimulation.cpp
        # TrafficSimulation.cpp excluded: it pulls in Tile/Map/StructureBase
        # and is only used by CityEffectsRuntime (which is stubbed in tests).
        ${CMAKE_SOURCE_DIR}/src/dunecity/CityBudget.cpp
        ${CMAKE_SOURCE_DIR}/src/dunecity/CityScanner.cpp
        ${CMAKE_SOURCE_DIR}/src/dunecity/CityEvaluation.cpp
        ${CMAKE_SOURCE_DIR}/src/audio/sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/mod/Dune2RAssetManager.cpp
        ${CMAKE_SOURCE_DIR}/src/Network/ENetHttp.cpp
        ${CMAKE_SOURCE_DIR}/src/FileClasses/INIFile.cpp
        ${CMAKE_SOURCE_DIR}/src/misc/FileSystem.cpp
        ${CMAKE_SOURCE_DIR}/src/misc/fnkdat.cpp
        globals_test_stub.cpp
        TextManager_stub.cpp
        # ENet library sources (needed for NetworkManager tests)
        ${CMAKE_SOURCE_DIR}/src/enet/callbacks.c
        ${CMAKE_SOURCE_DIR}/src/enet/compress.c
        ${CMAKE_SOURCE_DIR}/src/enet/host.c
        ${CMAKE_SOURCE_DIR}/src/enet/list.c
        ${CMAKE_SOURCE_DIR}/src/enet/packet.c
        ${CMAKE_SOURCE_DIR}/src/enet/peer.c
        ${CMAKE_SOURCE_DIR}/src/enet/protocol.c
        $<$<NOT:$<PLATFORM_ID:Windows>>:${CMAKE_SOURCE_DIR}/src/enet/unix.c>
        $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/src/enet/win32.c>
        # fmt library (needed for THROW macro in ENet headers)
        ${CMAKE_SOURCE_DIR}/src/misc/format.cpp
)

# Include directories
target_include_directories(dunelegacy_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/enet  # For enet headers
        ${CMAKE_SOURCE_DIR}/src      # For audio/sounds.h and other src-level headers
        # p2pkit SDK headers (path + install guard defined in the top-level
        # CMakeLists.txt): PacketStreamTestCase drives NetworkPacketBuffer/
        # NetworkPacketView, which are thin adapters over them.
        ${DUNECITY_P2PKIT_DIR}/emscripten/include
)

# Link libraries
target_link_libraries(dunelegacy_tests
    PRIVATE
        Catch2::Catch2WithMain
        SDL2::SDL2
        $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
        CURL::libcurl
        ZLIB::ZLIB
)

if(APPLE)
    # fnkdat.cpp calls getMacApplicationSupportFolder(), which is implemented in
    # the Objective-C helper the game target also compiles (src/CMakeLists.txt).
    target_sources(dunelegacy_tests PRIVATE ${CMAKE_SOURCE_DIR}/IDE/xCode/MacFunctions.m)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/IDE/xCode/MacFunctions.m
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(dunelegacy_tests
        PRIVATE
            "-framework Cocoa"
            "-framework Foundation"
            "-framework CoreFoundation"
    )
endif()

if(WIN32)
    target_link_libraries(dunelegacy_tests
        PRIVATE
            ws2_32
            winmm
            imm32
            version
    )
endif()

# Compiler options
target_compile_options(dunelegacy_tests
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -pedantic>
)

# Add test
# The dedicated skirmish_generator_tests below runs every [skirmish] case.
# Exclude that group here to avoid repeating its expensive generator matrices.
add_test(NAME dunelegacy_tests COMMAND dunelegacy_tests "~[skirmish]")
add_test(NAME lobby_authorization_tests COMMAND dunelegacy_tests "[lobby][security][authorization]")
# Local revision store: snapshots, pinning, verified imports and retired sharing-queue cleanup.
add_test(NAME workshop_store_tests COMMAND dunelegacy_tests "[workshop]")
add_test(NAME preplace_queue_tests COMMAND dunelegacy_tests "[preplace]")
# Map chooser/editor catalogue rules: duplicate collapse, revision sidecars are not
# map rows, and a locally edited workshop map keeps its authored mod in the filter.
add_test(NAME map_chooser_tests COMMAND dunelegacy_tests "[maps][chooser]")
add_test(NAME dune2r_presentation_tests COMMAND dunelegacy_tests "[dune2r][presentation],[dune2r][infantry],[Dune2RAssets]")
add_test(NAME classic_desert_ui_tests COMMAND dunelegacy_tests "[menu][ui-skin]")
# Modern RTS controls: order queue, attack-move, HOLD and repeat production rules (docs/rts-controls.md).
add_test(NAME rts_controls_tests COMMAND dunelegacy_tests "[rts-controls]")
add_test(NAME skirmish_generator_tests COMMAND dunelegacy_tests "[skirmish]")
# Formation planner (docs/formations.md): determinism, layout, fallback, edges, catch-up.
add_test(NAME formation_planner_tests COMMAND dunelegacy_tests "[formation]")

# Sidebar build grid geometry (docs/sidebar-grid.md): slots, hit-testing, row scrolling.
add_test(NAME sidebar_grid_tests COMMAND dunelegacy_tests "[sidebar-grid]")

# Standalone network wire harness.
#
# Same sources as tests/wasm/run-network-wire-harness.sh, built for the host so the wire
# boundary can be checked without the rest of the test target (and, with the script, under
# Emscripten where size_t is 32 bits). It needs no game data and no Catch2.
add_executable(network_wire_harness
    wasm/NetworkWireHarness.cpp
    ${CMAKE_SOURCE_DIR}/src/Network/SnapshotTransfer.cpp
    ${CMAKE_SOURCE_DIR}/src/misc/format.cpp
    ${CMAKE_SOURCE_DIR}/src/enet/callbacks.c
    ${CMAKE_SOURCE_DIR}/src/enet/compress.c
    ${CMAKE_SOURCE_DIR}/src/enet/host.c
    ${CMAKE_SOURCE_DIR}/src/enet/list.c
    ${CMAKE_SOURCE_DIR}/src/enet/packet.c
    ${CMAKE_SOURCE_DIR}/src/enet/peer.c
    ${CMAKE_SOURCE_DIR}/src/enet/protocol.c
    $<$<NOT:$<PLATFORM_ID:Windows>>:${CMAKE_SOURCE_DIR}/src/enet/unix.c>
    $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/src/enet/win32.c>
)

target_include_directories(network_wire_harness
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/enet
)

target_link_libraries(network_wire_harness
    PRIVATE
        ZLIB::ZLIB
        SDL2::SDL2
        $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
)

if(WIN32)
    target_link_libraries(network_wire_harness PRIVATE ws2_32 winmm)
endif()

add_test(NAME network_wire_harness COMMAND network_wire_harness)

# Standalone relay wire harness.
#
# The crossplay equivalent of the above, and the reason it is separate: the relay protocol, the
# endpoint validation, the admission parser, the HTTPS polling batch format and the state digest
# have no SDL, ENet or game dependencies at all. tests/wasm/run-relay-wire-harness.sh builds the
# same sources under Emscripten, which is the run that matters because the browser client is
# wasm32 and an additive length check wraps there.
#
# RelayHttpTransport.cpp is compiled in for the same reason: it is the polling state machine, it
# is deliberately free of SDL, libcurl and Emscripten, and its own length checks have to hold on
# a 32-bit size_t as well.
add_executable(relay_wire_harness
    wasm/RelayWireHarness.cpp
    ${CMAKE_SOURCE_DIR}/src/Network/RelayHttpTransport.cpp
)

target_include_directories(relay_wire_harness
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
)

add_test(NAME relay_wire_harness COMMAND relay_wire_harness)

# Relay session tests.
#
# A separate executable rather than another file in dunelegacy_tests, because it compiles the
# production RoomRelayClient.cpp and needs the transport to be a scripted one. RoomRelayClient
# reaches its socket through two free functions; this target supplies its own definitions of them
# instead of linking RelayWebSocketCurl.cpp, so the code under test is exactly what ships and the
# production build carries no test hook for it.
add_executable(relay_session_tests
    RelaySessionTestCase/RelaySessionTestCase.cpp
    RelaySessionTestCase/MockRelayWebSocket.cpp
    ${CMAKE_SOURCE_DIR}/src/Network/RoomRelayClient.cpp
    # misc/SDL2pp.h reaches misc/exceptions.h, whose THROW macro is built on fmt.
    ${CMAKE_SOURCE_DIR}/src/misc/format.cpp
)

target_include_directories(relay_session_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/RelaySessionTestCase
)

target_link_libraries(relay_session_tests
    PRIVATE
        Catch2::Catch2WithMain
        SDL2::SDL2
        $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
)

target_compile_options(relay_session_tests
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -pedantic>
)

if(WIN32)
    target_link_libraries(relay_session_tests PRIVATE ws2_32 winmm version)
endif()

add_test(NAME relay_session_tests COMMAND relay_session_tests)

# Relay HTTPS polling transport tests.
#
# Separate from dunelegacy_tests for the same reason relay_session_tests is: it compiles a
# production source file - the polling state machine - and supplies its collaborator itself. The
# transport reaches HTTP and the clock through RelayHttpBackend, so the test target provides a
# scripted backend with a virtual clock and asserts on the exact bytes the transport sent. No
# hook exists in the production build for any of it.
add_executable(relay_http_transport_tests
    RelayHttpTransportTestCase/RelayHttpTransportTestCase.cpp
    ${CMAKE_SOURCE_DIR}/src/Network/RelayHttpTransport.cpp
)

target_include_directories(relay_http_transport_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/RelayHttpTransportTestCase
)

target_link_libraries(relay_http_transport_tests
    PRIVATE
        Catch2::Catch2WithMain
)

target_compile_options(relay_http_transport_tests
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -pedantic>
)

add_test(NAME relay_http_transport_tests COMMAND relay_http_transport_tests)

# Relay transport harness.
#
# Not a ctest: it needs a running relay, so it is driven by hand or by
# tests/relay/run-relay-transport-harness.sh. It opens a real connection through the real
# transport, which is the only way to show that the native side actually works.
if(NOT EMSCRIPTEN)
    add_executable(relay_transport_harness
        relay/RelayTransportHarness.cpp
        ${CMAKE_SOURCE_DIR}/src/Network/RoomRelayClient.cpp
        ${CMAKE_SOURCE_DIR}/src/Network/RoomAdmissionClient.cpp
        # Admission and direct-play signalling share one bounded HTTP client.
        ${CMAKE_SOURCE_DIR}/src/Network/BoundedHttpClient.cpp
        ${CMAKE_SOURCE_DIR}/src/Network/RelayWebSocketCurl.cpp
        # Both transports, because createRelayWebSocket() now dispatches on the endpoint scheme
        # and the harness has to be able to follow the relay to whichever it hands out.
        ${CMAKE_SOURCE_DIR}/src/Network/RelayHttpTransport.cpp
        ${CMAKE_SOURCE_DIR}/src/Network/RelayHttpTransportCurl.cpp
        ${CMAKE_SOURCE_DIR}/src/misc/FileSystem.cpp
        ${CMAKE_SOURCE_DIR}/src/misc/fnkdat.cpp
        ${CMAKE_SOURCE_DIR}/src/misc/format.cpp
    )

    target_include_directories(relay_transport_harness
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_BINARY_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(relay_transport_harness
        PRIVATE
            SDL2::SDL2
            $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
            CURL::libcurl
    )

    if(APPLE)
        target_sources(relay_transport_harness
            PRIVATE ${CMAKE_SOURCE_DIR}/IDE/xCode/MacFunctions.m)
        set_source_files_properties(${CMAKE_SOURCE_DIR}/IDE/xCode/MacFunctions.m
            PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_link_libraries(relay_transport_harness
            PRIVATE
                "-framework Cocoa"
                "-framework Foundation"
                "-framework CoreFoundation"
        )
    endif()

    if(WIN32)
        target_link_libraries(relay_transport_harness PRIVATE ws2_32 winmm version)
    endif()
endif()

# Set test properties
# map_chooser_tests reads the bundled map files too: without DUNE_CITY_SOURCE_DIR
# it falls back to the working directory and so to the test target's POST_BUILD
# copy of data/, which carries no dependency on the maps themselves.
set_tests_properties(dunelegacy_tests lobby_authorization_tests workshop_store_tests map_chooser_tests dune2r_presentation_tests rts_controls_tests skirmish_generator_tests sidebar_grid_tests classic_desert_ui_tests PROPERTIES
    ENVIRONMENT "DUNECITY_DATADIR=${CMAKE_SOURCE_DIR}/data;DUNE_CITY_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
)

# Copy test data to build directory
add_custom_command(
    TARGET dunelegacy_tests
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/data"
        "${CMAKE_BINARY_DIR}/tests/data"
    COMMENT "Copying test data to build directory"
)

# The interoperability harness is a separate executable on purpose: it needs the real WebRTC
# backend, which the unit tests deliberately do not link. See tools/p2p-interop/README.md.
if(DUNECITY_DIRECT_P2P AND NOT EMSCRIPTEN)
    add_executable(dunecity_p2p_interop
        ${CMAKE_SOURCE_DIR}/tools/p2p-interop/native-peer.cpp
        ${CMAKE_SOURCE_DIR}/src/Network/DirectPeerConnectionLibdatachannel.cpp)
    target_include_directories(dunecity_p2p_interop PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(dunecity_p2p_interop PRIVATE DUNECITY_HAVE_LIBDATACHANNEL)
    target_link_libraries(dunecity_p2p_interop PRIVATE ${DUNECITY_LIBDATACHANNEL_TARGET})
    target_compile_features(dunecity_p2p_interop PRIVATE cxx_std_17)
endif()

# Real SDL menu rendering and setup preservation, using isolated local profiles.
# The diagnostic main links production menu objects; no test access ships in the app.
if(APPLE AND CMAKE_GENERATOR STREQUAL "Ninja")
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME quantbot_match_score COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/test_match_score.py)
        set_tests_properties(quantbot_match_score PROPERTIES TIMEOUT 30)
        add_test(NAME save_scenario_export COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/scenarios/export-save-scenario.py
            --build-dir ${CMAKE_BINARY_DIR} --work-dir ${CMAKE_BINARY_DIR}/scenario-export-regression
            --selftest --force --match-seconds 10 --min-structures 4 --min-units 19)
        set_tests_properties(save_scenario_export PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
        # A save pinned to an authored mod's exact Workshop revision loads once the mod
        # exists only in workshop/revisions, and a retired upload queue in that profile is
        # deleted without any publication.
        add_test(NAME save_pinned_workshop_revision COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/scenarios/export-save-scenario.py
            --build-dir ${CMAKE_BINARY_DIR} --work-dir ${CMAKE_BINARY_DIR}/pinned-revision-save
            --selftest --fixture-draft "Pinned Draft" --force --match-seconds 5)
        set_tests_properties(save_pinned_workshop_revision PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
        add_test(NAME save_scenario_input_preservation COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/scenarios/test-export-paths.py)
        set_tests_properties(save_scenario_input_preservation PROPERTIES TIMEOUT 30)
        add_test(NAME web_mod_payload_tests COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/tests/test_check_web_mods.py)
        set_tests_properties(web_mod_payload_tests PROPERTIES TIMEOUT 30)
        add_test(NAME bundled_city_eight_slot_startup COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/bundled-eight-slot-startup
            --repeatable --custom-map "${CMAKE_SOURCE_DIR}/data/maps/singleplayer/4P - 384x384 - new beginning.ini"
            --mod dunecity --local-spectator --minutes 1 --enemy-difficulty hard --partner-difficulty hard)
        set_tests_properties(bundled_city_eight_slot_startup PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
        add_test(NAME bundled_three_way_startup COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/bundled-three-way-startup
            --repeatable --custom-map "${CMAKE_SOURCE_DIR}/data/maps/singleplayer/3P - 192x192 - 3waysplit.ini"
            --mod dunecity --local-spectator --minutes 1 --enemy-difficulty hard --partner-difficulty hard)
        set_tests_properties(bundled_three_way_startup PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
        # Every catalogue map in data/maps/community-maps.json is the recorded revision, has the
        # slots and size its name advertises, a start for every slot, and loads and plays for a
        # game minute in its mod with every lobby house still holding its start.
        add_test(NAME community_maps_startup COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/maps/check-community-maps.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/community-maps-startup)
        set_tests_properties(community_maps_startup PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
        # Exact reviewed payloads, real five-minute AI production from two bare yards,
        # and matching final digests across two runs of every community mod.
        add_test(NAME community_mods_startup COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/mods/check-community-mods.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/community-mods-startup)
        set_tests_properties(community_mods_startup PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
        # Sihaya-Phonics stacks two scenario sandworms on tile 790. The second
        # cannot be placed and is destroyed while an observed game's map loads,
        # before any local player exists; that crashed 1.0.830 at startup.
        add_test(NAME observed_startup_unplaceable_unit COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/observed-startup-unplaceable-unit
            --repeatable --custom-map "${CMAKE_SOURCE_DIR}/data/maps/multiplayer/4P - 51x31 - 2v2 - Sihaya-Phonics.ini"
            --mod dunecity --local-spectator --minutes 1 --enemy-difficulty hard --partner-difficulty hard)
        set_tests_properties(observed_startup_unplaceable_unit PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
        add_test(NAME graphics_skin_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/skins/run-skin-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/skin-probe)
        set_tests_properties(graphics_skin_probe PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
        add_test(NAME menu_navigation_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/menu/run-menu-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/menu-probe)
        set_tests_properties(menu_navigation_probe PROPERTIES TIMEOUT 1380 RUN_SERIAL TRUE)
        # UI skins on the real menus and a real DuneCity game (Modern and the default Classic
        # Desert at 640x480 and Stefan's 1440x900 Medium): every screen rendered and linted, the
        # spectated end plate fits the map in every house colour, chat clears the city readouts,
        # a skin round trip leaves the in-game sidebar pixel-identical, new and 1.0.850 profiles
        # get Classic. The full 3 skins x 7 sizes gallery: run-ui-gallery-probe.py without --skins/--cases.
        add_test(NAME ui_gallery_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/menu/run-ui-gallery-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/ui-gallery-probe
            --skins 0 2 --cases 640x480 1440x900-hd)
        set_tests_properties(ui_gallery_probe PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
        # Random Skirmish on the real engine: the screen (house cap, persistence, Save map,
        # screenshots), the hosted Random map (Custom Game's button, open Player starts saved and
        # chosen, lobby with host, guest, AI and teams, the guest's wire-format copy, both peers'
        # match on equal starts with matching state digests), a pre-zone-level city map loading
        # unchanged, and three generated matches (vanilla 1v1, 2v3 vs allied Large bases with 10k
        # armies, DuneCity 192 with 15k cities at tech 8) checked against the generator's plan,
        # measured city population, a one-minute played smoke with a clean log and a save/load round trip.
        add_test(NAME skirmish_generator_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/skirmish/run-skirmish-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/skirmish-probe)
        set_tests_properties(skirmish_generator_probe PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
        add_test(NAME retina_resolution_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/menu/run-menu-probe.py --retina-resolution
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/retina-resolution-probe)
        set_tests_properties(retina_resolution_probe PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
        add_test(NAME game_command_regressions COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/network/run-command-execution-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/command-probe)
        set_tests_properties(game_command_regressions PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
        # Offline Custom Game "Spectate": two real AI factions with no observer controller or
        # start slot, full map and radar without touching exploration, owner-specific panels for
        # every owner, every mutation route refused while the owners' own commands still work,
        # and observation preserved across save and reload. Separate from the menu probe so
        # neither has to carry the other's runtime.
        add_test(NAME local_spectator_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/spectator/run-spectator-probe.py
            --map "${CMAKE_SOURCE_DIR}/data/maps/multiplayer/4P - 192x192 - Metropolis.ini"
            --mod dunecity --build-dir ${CMAKE_BINARY_DIR}
            --output-dir ${CMAKE_BINARY_DIR}/spectator-probe)
        set_tests_properties(local_spectator_probe PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
        foreach(shared IN ITEMS off on)
            set(shared_args)
            if(shared STREQUAL "on")
                list(APPEND shared_args --shared-house)
            endif()
            add_test(NAME local_spectator_worm_startup_${shared} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/spectator/run-spectator-probe.py --eight-houses ${shared_args}
                --map "${CMAKE_SOURCE_DIR}/tests/spectator/8P-preplaced-worms.ini"
                --mod dunecity --build-dir ${CMAKE_BINARY_DIR}
                --output-dir ${CMAKE_BINARY_DIR}/spectator-worm-${shared}-probe)
            set_tests_properties(local_spectator_worm_startup_${shared} PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        endforeach()
        # Control case: the shared non-network local-player byte must keep its old meaning for
        # an ordinary human single-player save, and must never read back as an observer.
        add_test(NAME local_spectator_ordinary_round_trip COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/spectator/run-spectator-probe.py --ordinary-round-trip
            --map "${CMAKE_SOURCE_DIR}/data/maps/multiplayer/4P - 192x192 - Metropolis.ini"
            --mod dunecity --build-dir ${CMAKE_BINARY_DIR}
            --output-dir ${CMAKE_BINARY_DIR}/spectator-ordinary-probe)
        set_tests_properties(local_spectator_ordinary_round_trip PROPERTIES TIMEOUT 400 RUN_SERIAL TRUE)
        # The Offline Custom Game setup row itself, against the real CustomGamePlayers widgets.
        add_test(NAME local_spectator_setup_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/spectator/run-spectator-probe.py --setup
            --map "${CMAKE_SOURCE_DIR}/data/maps/multiplayer/4P - 192x192 - Metropolis.ini"
            --mod dunecity --build-dir ${CMAKE_BINARY_DIR}
            --output-dir ${CMAKE_BINARY_DIR}/spectator-setup-probe)
        set_tests_properties(local_spectator_setup_probe PROPERTIES TIMEOUT 400 RUN_SERIAL TRUE)
        # Stefan: "when there is a winner and player is spectating the game should end." Real
        # matches end for a view without a side: spectated Random Skirmish FFA and 2v2 in both
        # mods, a player out on a WinFlags=1 map, quota and time-limit rules, and a spectator
        # beside two networked players (same end cycle and state digest as the winning player);
        # the winner is named in its colour in the game and the statistics; played games unchanged.
        add_test(NAME spectator_game_end_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/spectator/run-game-end-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/spectator-game-end-probe)
        set_tests_properties(spectator_game_end_probe PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
        # The same end online: host, partner and a mid-game spectator as three processes over the
        # local signaling service (PHP) and WebRTC, each in the real main loop. The spectator ends
        # on the host's last cycle and digest with the winner on its plate and statistics when the
        # host leaves after its end message, quits at the decision, while the spectator hangs 20-25 s
        # or runs at half speed, and in a Dune City game with Natives worms. Skipped without PHP.
        add_test(NAME spectator_online_end_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/network/run-spectator-end-probe.py
            --cases end,decision,lag-end,lag-decision,slow,natives
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/spectator-online-end-probe)
        set_tests_properties(spectator_online_end_probe PROPERTIES TIMEOUT 1200 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
        # Stefan: "make any spawned troopers due to crime and make all worms belong to 'natives' house".
        # Worms on bundled maps, a Random Skirmish, a campaign mission and a 9868 save are the
        # Natives'; crime troopers are, and QuantBot's threat estimate ignores them; Natives is in
        # no house list or statistics; win/lose ignores it; the inspect panel and radar show it.
        # 1.0.852 interactions: Natives kills and a shot-down plane's crash deciding spectated,
        # played and campaign games (a crash taking two last teams at once names nobody), the
        # analytics roster, and host/guest/spectator lockstep digests with all of it in play.
        add_test(NAME natives_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/natives/run-natives-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/natives-probe)
        set_tests_properties(natives_probe PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
        # Saves 1.0.842, 1.0.847, 1.0.850 and the public 1.0.851 wrote (vanilla FFA, DuneCity 1v1,
        # mid-game with worms) load in a fresh profile, run 600 cycles, re-save as the current
        # savegame version and load again, every worm the Natives'.
        add_test(NAME save_roundtrip_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/saves/run-save-roundtrip-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/save-roundtrip-probe)
        set_tests_properties(save_roundtrip_probe PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
        add_test(NAME carryall_flight_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/units/run-carryall-flight-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/carryall-flight-probe)
        set_tests_properties(carryall_flight_probe PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        # A ground unit walled in on a real map really does stop moving, so the thirty-second
        # long-stall rescue can be measured against the engine instead of being simulated:
        # the clock survives a queued path request and a route cleared every cycle, and the
        # rescue books exactly one free owned carrier and delivers to the unit's own goal.
        # The Micropolis city aircraft against the real engine: the Airport's
        # doAirport launch odds and per-house singleton, the helicopter's
        # traffic patrol and bounded friendly-only reporting, the airplane's
        # original take-off and off-map ending, hostile anti-air destroying
        # both with no tile/unit/count leak, and an exact save continuation.
        add_test(NAME city_aircraft_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/units/run-city-aircraft-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/city-aircraft-probe)
        set_tests_properties(city_aircraft_probe PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
        add_test(NAME stalled_carryall_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/units/run-stalled-carryall-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/stalled-carryall-probe)
        set_tests_properties(stalled_carryall_probe PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
        # A unit that is not on the map belongs to one carrier or one busy structure bay: the
        # pickup of a badly damaged booked passenger enrols that unit even though its own repair
        # order can promise it to another aircraft, and units already hidden inside nothing are
        # recovered - including from a walled-off corner, with no anchor at all, and in bounded
        # batches - without disturbing valid cargo or bay occupants. Runs all three engines and
        # repeats the scheduled boundary under one and four path workers.
        add_test(NAME transport_containment_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/units/run-transport-containment-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/transport-containment-probe)
        set_tests_properties(transport_containment_probe PROPERTIES TIMEOUT 1800 RUN_SERIAL TRUE)
        # Brutal only plans past its configured military value cap when the lobby
        # selected an explicit unit-count override; the engine count limit stays
        # the authority, and other difficulties keep the configured cap.
        add_test(NAME brutal_unit_override_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-brutal-unit-override-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/brutal-unit-override-probe)
        set_tests_properties(brutal_unit_override_probe PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        add_test(NAME active_tile_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/performance/run-active-tile-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/active-tile-probe)
        set_tests_properties(active_tile_probe PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
        add_test(NAME tile_track_bounds_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/units/run-tile-track-bounds-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --phases current
            --output-dir ${CMAKE_BINARY_DIR}/tile-track-bounds-probe)
        set_tests_properties(tile_track_bounds_probe PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
        add_test(NAME body_angle_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/units/run-body-angle-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/body-angle-probe)
        set_tests_properties(body_angle_probe PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
        add_test(NAME unit_speed_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/units/run-unit-speed-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/unit-speed-probe)
        set_tests_properties(unit_speed_probe PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        # Formation movement (docs/formations.md): twelve mixed units through the real
        # right-click path with formations on and off, a hunting mid-march save/reload, 9854
        # and 9857 unit records, Combined arms with sonic tanks and scouts (sonic lanes never
        # entered), Line and Column, and a hunt in formation that breaks on contact; leaves
        # formation-march.png and formation-combined.png (with the panel's formation buttons).
        add_test(NAME formation_movement_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/units/run-formation-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/formation-probe)
        set_tests_properties(formation_movement_probe PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
        add_test(NAME unit_route_continuation_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/units/run-unit-route-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --continuation
            --output-dir ${CMAKE_BINARY_DIR}/unit-route-continuation-probe)
        set_tests_properties(unit_route_continuation_probe PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        add_test(NAME weapon_reload_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/units/run-unit-route-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --weapon-reloads
            --output-dir ${CMAKE_BINARY_DIR}/weapon-reload-probe)
        set_tests_properties(weapon_reload_probe PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        # Modern RTS controls on real units over real cycles: attack-move column, HOLD launcher
        # under fire, three-waypoint Shift queue, repeat production through the unit limit,
        # save round trip, the committed 9854 save, and a rendered waypoint/tooltip smoke frame.
        add_test(NAME rts_controls_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/units/run-rts-controls-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/rts-controls-probe)
        set_tests_properties(rts_controls_probe PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
        # Smooth unit motion at 2 ms game speed through the real main loop and frame pacing:
        # simulated (and drawn) units never stand still mid-step, keep justStoppedMoving on a
        # chained step or jump a tile, per cycle and per rendered frame, and a unit whose carryall
        # wait was cancelled drives on at its speed.
        add_test(NAME unit_motion_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/units/run-unit-motion-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/unit-motion-probe)
        set_tests_properties(unit_motion_probe PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        add_test(NAME deviation_reward_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/units/run-unit-route-probe.py
            --build-dir ${CMAKE_BINARY_DIR} --deviation-reward
            --output-dir ${CMAKE_BINARY_DIR}/deviation-reward-probe)
        set_tests_properties(deviation_reward_probe PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
        foreach(projectile_probe IN ITEMS mechanics combat continuation)
            if(projectile_probe STREQUAL "mechanics")
                set(projectile_flag --projectiles)
            else()
                set(projectile_flag --projectile-${projectile_probe})
            endif()
            add_test(NAME projectile_${projectile_probe}_probe COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/units/run-unit-route-probe.py
                --build-dir ${CMAKE_BINARY_DIR} ${projectile_flag}
                --output-dir ${CMAKE_BINARY_DIR}/projectile-${projectile_probe}-probe)
            set_tests_properties(projectile_${projectile_probe}_probe PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        endforeach()
        foreach(ai_mod IN ITEMS vanilla dunecity Dune2R)
            foreach(ai_probe IN ITEMS air-defense shared-spending stats)
                add_test(NAME quantbot_${ai_probe}_${ai_mod} COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --${ai_probe}-probe --repeatable
                    --level 9 --mod ${ai_mod} --build-dir ${CMAKE_BINARY_DIR}
                    --output-dir ${CMAKE_BINARY_DIR}/${ai_probe}-${ai_mod}-probe)
                set_tests_properties(quantbot_${ai_probe}_${ai_mod} PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
            endforeach()
        endforeach()
        # Exercise campaign wave readiness, first-dispatch funding, surviving attackers
        # and saved partner state through the live Harkonnen level-9 fixture.
        foreach(pressure_mod IN ITEMS vanilla dunecity)
            add_test(NAME quantbot_campaign_pressure_${pressure_mod} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --pressure-probe --repeatable
                --house harkonnen --level 9 --mod ${pressure_mod} --build-dir ${CMAKE_BINARY_DIR}
                --output-dir ${CMAKE_BINARY_DIR}/campaign-pressure-${pressure_mod}-probe)
            set_tests_properties(quantbot_campaign_pressure_${pressure_mod}
                PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
        endforeach()
        # Read-only Stats window, observer house browsing and the sidebar squeeze,
        # on a real renderer at the smallest supported interface height and at
        # native 1080. Both sizes capture screenshots into their output directory.
        foreach(ui_viewport IN ITEMS 640x480 1920x1080)
            add_test(NAME stats_ui_${ui_viewport} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --stats-ui-probe --repeatable
                --viewport ${ui_viewport} --level 9 --mod dunecity --build-dir ${CMAKE_BINARY_DIR}
                --output-dir ${CMAKE_BINARY_DIR}/stats-ui-${ui_viewport}-probe)
            set_tests_properties(stats_ui_${ui_viewport} PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        endforeach()
        # The same window without a city economy: tax and population must read
        # N/A rather than a fabricated zero, and Stats must still be reachable.
        add_test(NAME stats_ui_vanilla COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --stats-ui-probe --repeatable
            --viewport 1024x768 --level 9 --mod vanilla --build-dir ${CMAKE_BINARY_DIR}
            --output-dir ${CMAKE_BINARY_DIR}/stats-ui-vanilla-probe)
        set_tests_properties(stats_ui_vanilla PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        foreach(ai_mod IN ITEMS vanilla dunecity Dune2R)
            add_test(NAME quantbot_opening-economy_${ai_mod} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --opening-economy-probe --repeatable
                --custom-map "${CMAKE_SOURCE_DIR}/data/maps/singleplayer/5P - 128x128 - All against Atreides.ini"
                --mod ${ai_mod} --build-dir ${CMAKE_BINARY_DIR}
                --output-dir ${CMAKE_BINARY_DIR}/opening-economy-${ai_mod}-probe)
            set_tests_properties(quantbot_opening-economy_${ai_mod} PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
        endforeach()
        add_test(NAME quantbot_police_placement COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --police-placement-probe --repeatable
            --custom-map "${CMAKE_SOURCE_DIR}/data/maps/multiplayer/6P - 256x256 - Alkozeltser 4.ini"
            --mod dunecity --build-dir ${CMAKE_BINARY_DIR}
            --output-dir ${CMAKE_BINARY_DIR}/police-placement-probe)
        set_tests_properties(quantbot_police_placement PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
        add_test(NAME quantbot_police_budget COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --police-budget-probe --repeatable
            --custom-map "${CMAKE_SOURCE_DIR}/data/maps/multiplayer/6P - 256x256 - Alkozeltser 4.ini"
            --mod dunecity --build-dir ${CMAKE_BINARY_DIR}
            --output-dir ${CMAKE_BINARY_DIR}/police-budget-probe)
        set_tests_properties(quantbot_police_budget PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
        # Police reinforcement admission: the unit limit the lobby selected is the only
        # authority for the sidebar status and for deployment, and the station's own
        # garrison restrictions stay independent of it.
        add_test(NAME police_reinforcement_limit COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --police-reinforcement-probe --repeatable
            --custom-map "${CMAKE_SOURCE_DIR}/data/maps/multiplayer/6P - 256x256 - Alkozeltser 4.ini"
            --mod dunecity --build-dir ${CMAKE_BINARY_DIR}
            --output-dir ${CMAKE_BINARY_DIR}/police-reinforcement-probe)
        set_tests_properties(police_reinforcement_limit PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
        # MCVs must turn into yards on the rock the base already stands on,
        # even while the advanced core and its turret cover are unfinished.
        # The reported match ran this map and mod; the fixture asserts that
        # the colonisation gate really is shut before it measures anything.
        add_test(NAME quantbot_mcv_deployment COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --mcv-deployment-probe --repeatable
            --mod dunecity
            --custom-map "${CMAKE_SOURCE_DIR}/data/maps/singleplayer/5P - 128x128 - All against Atreides.ini"
            --build-dir ${CMAKE_BINARY_DIR}
            --output-dir ${CMAKE_BINARY_DIR}/mcv-deployment-probe)
        set_tests_properties(quantbot_mcv_deployment PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        option(DUNECITY_ENABLE_AI_MATCH_TESTS "Enable long, victory-required QuantBot matches" OFF)
        if(DUNECITY_ENABLE_AI_MATCH_TESTS)
            foreach(seed 1841712204 41562270 1430976924)
                add_test(NAME quantbot_city_hard_match_${seed} COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --repeatable --require-win
                    --mod dunecity --house atreides --partner-difficulty brutal
                    --enemy-ai ai-player --enemy-difficulty hard --concrete-required
                    --custom-map "${CMAKE_SOURCE_DIR}/data/maps/singleplayer/5P - 128x128 - All against Atreides.ini"
                    --seed ${seed} --minutes 90 --wall-timeout 2400 --capture-mib 512
                    --build-dir ${CMAKE_BINARY_DIR}
                    --output-dir ${CMAKE_BINARY_DIR}/city-hard-match-${seed})
                set_tests_properties(quantbot_city_hard_match_${seed} PROPERTIES
                    TIMEOUT 2500 RUN_SERIAL TRUE LABELS "full-match")
            endforeach()
            # The reported 1.0.780 game used powered rocket turrets and this seed.
            add_test(NAME quantbot_city_reclaimed_rock_match COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --repeatable --require-win
                --mod dunecity --house atreides --partner-difficulty brutal
                --enemy-ai ai-player --enemy-difficulty hard --concrete-required
                --rocket-turrets-need-power
                --custom-map "${CMAKE_SOURCE_DIR}/data/maps/singleplayer/5P - 128x128 - All against Atreides.ini"
                --seed 1804669015 --minutes 90 --wall-timeout 2400 --capture-mib 512
                --build-dir ${CMAKE_BINARY_DIR}
                --output-dir ${CMAKE_BINARY_DIR}/city-reclaimed-rock-match)
            set_tests_properties(quantbot_city_reclaimed_rock_match PROPERTIES
                TIMEOUT 2500 RUN_SERIAL TRUE LABELS "full-match")
        endif()
        add_test(NAME quantbot_city_growth COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --city-growth-probe --repeatable
            --custom-map "${CMAKE_SOURCE_DIR}/data/maps/multiplayer/6P - 256x256 - Alkozeltser 4.ini"
            --mod dunecity --build-dir ${CMAKE_BINARY_DIR}
            --output-dir ${CMAKE_BINARY_DIR}/city-growth-probe)
        set_tests_properties(quantbot_city_growth PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
        # A city map with no usable spice must seed demanded R/C/I out of its
        # starting grant, including the storage needed to bank its tax. Optional
        # port technology and transport must leave this budget intact.
        add_test(NAME quantbot_no_spice_city COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --no-spice-city-probe --repeatable
            --custom-map "${CMAKE_SOURCE_DIR}/data/maps/singleplayer/4P - 192x192 - DuneCity.ini"
            --mod dunecity --build-dir ${CMAKE_BINARY_DIR}
            --output-dir ${CMAKE_BINARY_DIR}/no-spice-city-probe)
        set_tests_properties(quantbot_no_spice_city PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
        # A growth-cap project (demanded Stadium/Airport, planned reactor) must enter
        # production without its price banked and be charged in installments out of
        # arriving tax: the reported 1.0.817 city never ordered a 3000-credit stadium
        # in 247109 cycles. Concrete is required so the foundation budget is exercised.
        add_test(NAME quantbot_growth_installments COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --growth-installment-probe
            --repeatable --level 9 --mod dunecity --concrete-required
            --custom-map "${CMAKE_SOURCE_DIR}/data/maps/singleplayer/4P - 192x192 - DuneCity.ini"
            --build-dir ${CMAKE_BINARY_DIR}
            --output-dir ${CMAKE_BINARY_DIR}/growth-installment-probe)
        set_tests_properties(quantbot_growth_installments PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        add_test(NAME quantbot_civic_priority COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --civic-priority-probe
            --repeatable --level 9 --mod dunecity --concrete-required
            --custom-map "${CMAKE_SOURCE_DIR}/data/maps/singleplayer/4P - 192x192 - DuneCity.ini"
            --build-dir ${CMAKE_BINARY_DIR}
            --output-dir ${CMAKE_BINARY_DIR}/civic-priority-probe)
        set_tests_properties(quantbot_civic_priority PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        foreach(no_spice_map IN ITEMS dunecity low-cash)
            add_test(NAME quantbot_no_spice_income_${no_spice_map} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/ai/run-no-spice-city-progress.py
                --map ${no_spice_map} --build-dir ${CMAKE_BINARY_DIR}
                --output-dir ${CMAKE_BINARY_DIR}/no-spice-income-probe)
            set_tests_properties(quantbot_no_spice_income_${no_spice_map}
                PROPERTIES TIMEOUT 400 RUN_SERIAL TRUE)
        endforeach()
        foreach(starport_mod IN ITEMS vanilla dunecity)
            add_test(NAME quantbot_starport_${starport_mod} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --starport-probe --level 9 --repeatable
                --mod ${starport_mod} --build-dir ${CMAKE_BINARY_DIR}
                --output-dir ${CMAKE_BINARY_DIR}/starport-${starport_mod}-probe)
            set_tests_properties(quantbot_starport_${starport_mod} PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        endforeach()
        # Dynasty-aligned degradation: power damage on the raw produced/required
        # figures, foundation decay from real placement, walls excluded, the
        # campaign exemption and the 9847 save state. Every mod is measured,
        # including Tornie, which needs no concrete but still loses power damage.
        foreach(degradation_mod IN ITEMS vanilla dunecity Dune2R Tornie)
            add_test(NAME structure_degradation_${degradation_mod} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --degradation-probe --repeatable
                --level 9 --mod ${degradation_mod} --build-dir ${CMAKE_BINARY_DIR}
                --output-dir ${CMAKE_BINARY_DIR}/degradation-${degradation_mod}-probe)
            set_tests_properties(structure_degradation_${degradation_mod} PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        endforeach()
        add_test(NAME quantbot_custom_attack COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --custom-attack-probe --repeatable
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/custom-attack-probe)
        set_tests_properties(quantbot_custom_attack PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
        # Custom Hard/Brutal army recovery: reinforcements assemble at home, a
        # badly traded main-force collapse withdraws while an ordinary raid does
        # not, the withdrawal really moves units home and still defends on
        # arrival, the outnumbered dispatch gate holds its 80%-of-configured
        # boundary, the enemy-facing battery is legal and corridor-preserving,
        # the special allocator learns, and a large wave is recalled fairly.
        add_test(NAME quantbot_army_recovery COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --recovery-probe --repeatable
            --level 9 --mod dunecity
            --custom-map "${CMAKE_SOURCE_DIR}/data/maps/singleplayer/5P - 128x128 - All against Atreides.ini"
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/army-recovery-probe)
        set_tests_properties(quantbot_army_recovery PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        # Custom Hard/Brutal whole-army dispatch: one attack sends every
        # available combat troop to engine Hunt - idle, targeting, moving,
        # tracked from an older wave, posted as a colony guard or produced at a
        # remote colony - while human orders, repair runs, transports, workers,
        # MCVs and the air planner's aircraft keep theirs. An attack on a
        # building we own then pulls an adequate local response in one event,
        # including hunting troops, and Easy/Medium keep their shared waves.
        add_test(NAME quantbot_whole_army_dispatch COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --whole-army-probe --repeatable
            --level 9 --mod dunecity
            --custom-map "${CMAKE_SOURCE_DIR}/data/maps/singleplayer/5P - 128x128 - All against Atreides.ini"
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/whole-army-dispatch-probe)
        set_tests_properties(quantbot_whole_army_dispatch PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        # QuantBot attack waves in formation (docs/formations.md, QuantBot waves): a mixed
        # Hard wave launches as one hunting formation in the configured shape (Combined arms,
        # Box, Line) towards its own objective, harvester and MCV left out, no slot in a sonic
        # lane; it marches in Hunt and every member breaks into the fight on contact and stays
        # in Hunt. WaveFormation None and a wave below three units keep today's orders.
        add_test(NAME quantbot_wave_formation COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --wave-formation-probe --repeatable
            --level 9 --mod dunecity
            --custom-map "${CMAKE_SOURCE_DIR}/data/maps/singleplayer/5P - 128x128 - All against Atreides.ini"
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/wave-formation-probe)
        set_tests_properties(quantbot_wave_formation PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        # QuantBot hunt parties (docs/formations.md, Blob waves): a mixed Hard wave with
        # WaveFormation Blob gathers every available fighter at a rally point, marches leg by
        # leg with its fast members kept near the slow ones, releases into engine Hunt on
        # contact, reforms when quiet, falls back a short way when overwhelmed and returns; the
        # gather times out on stragglers; party orders do not depend on unit order; the party
        # survives a save and load mid-gather, mid-march and mid-reform; None keeps today's orders.
        add_test(NAME quantbot_blob_waves COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --blob-wave-probe --repeatable
            --level 9 --mod dunecity
            --custom-map "${CMAKE_SOURCE_DIR}/data/maps/singleplayer/5P - 128x128 - All against Atreides.ini"
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/blob-wave-probe)
        set_tests_properties(quantbot_blob_waves PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        # QuantBot force concentration defaults (docs/formations.md, "Concentrating force"):
        # Custom Hard/Brutal hunters focus the damaged enemy fighter in reach, a wave member not
        # fighting walks to its army's fight nearby, and a wave launches at 60% readiness; the
        # idle reserve gathers at the bundle (capped orders, farthest first), a ready wave waits
        # for it (at most 60 s) and launches from it, an attacked building pulls defenders out
        # of it, and save/load mid-gather restores the bundle; the orders do not depend on unit
        # order, Medium is untouched, and the duel override 0 is the previous behaviour.
        add_test(NAME quantbot_force_concentration COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --force-probe --repeatable
            --level 9 --mod dunecity
            --custom-map "${CMAKE_SOURCE_DIR}/data/maps/singleplayer/5P - 128x128 - All against Atreides.ini"
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/force-concentration-probe)
        set_tests_properties(quantbot_force_concentration PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        # QuantBot strategies (docs/ai-play-styles.md, "Strategies"), tests/ai/strategy-probe.inc:
        # two peers of the same four-house vanilla game pick the same seeded strategies and stay
        # in lockstep; the pick survives save/load (byte-identical blocks, a 9864 block re-picks
        # it, the checkpoint load continues to the host's digest); every mix strategy's production
        # passes play its prior before combat evidence and still learn, and its lead slots get a
        # larger share of the built value than the Learned game's on the mean over three seeds
        # (486409243 and --strategy-margin-seeds 11,22: one game per mix is one trajectory);
        # Learned/Base plays exactly the configured default's game.
        add_test(NAME quantbot_strategy COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --strategy-probe --repeatable
            --mod vanilla --seed 486409243 --no-ai-telemetry --path-workers 1
            --custom-map "${CMAKE_SOURCE_DIR}/data/maps/multiplayer/4P - 64x64 - Four Chambers.ini"
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/strategy-probe)
        set_tests_properties(quantbot_strategy PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        # Custom Hard/Brutal launcher spacing while hunting, with the production force rules
        # on: an enemy that is already too close makes a hunting launcher move away over real
        # game cycles, keeping engine Hunt, its tracked army membership and its firing range,
        # and focus fire gives it no order meanwhile; standing off, it takes and hits the focus
        # target. A distant enemy - even one that just hit it - produces no escape, and
        # human/repair/transit/defence authority, other hunters, Deviators, Easy/Medium and
        # campaign games keep theirs.
        add_test(NAME quantbot_launcher_kiting COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --launcher-kiting-probe --repeatable
            --level 9 --mod dunecity
            --custom-map "${CMAKE_SOURCE_DIR}/data/maps/singleplayer/5P - 128x128 - All against Atreides.ini"
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/launcher-kiting-probe)
        set_tests_properties(quantbot_launcher_kiting PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        add_test(NAME quantbot_battery_clearance COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --battery-clearance-probe --repeatable
            --level 9 --mod dunecity
            --custom-map "${CMAKE_SOURCE_DIR}/data/maps/singleplayer/5P - 128x128 - All against Atreides.ini"
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/battery-clearance-probe)
        set_tests_properties(quantbot_battery_clearance PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
        # MANDATORY for every AI/unit-behaviour change (AGENTS.md, CLAUDE.md): Stefan's
        # "All against Atreides" setup played to the end - vanilla, Atreides QuantBot Brutal
        # (local spectator) against four AI Player (hard) houses, his Game Options (unit and
        # harvester overrides 0). Atreides must eliminate every house within 40 game minutes
        # on all six seeds; 1.0.796 does so (32-37 min), 1.0.812-1.0.842 lose or take 42-71.
        # docs/quantbot-all-against-atreides.md.
        add_test(NAME quantbot_all_against_atreides COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-all-against-atreides.py --jobs 3 --win-minutes 40
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/all-against-atreides)
        set_tests_properties(quantbot_all_against_atreides PROPERTIES TIMEOUT 1500 RUN_SERIAL TRUE)
        # MANDATORY for every AI/unit-behaviour change (AGENTS.md, CLAUDE.md): four QuantBot Brutal
        # houses in generated 4P 160x160 DuneCity free-for-alls (Fortress starts, 20,000-credit
        # armies, 40,000 cities, Stefan's game options), seeds 9101, 9103, 9105 three at a time.
        # Fails when, on 9101, no wave of 30+ units has left by minute 8 or a house keeps its idle
        # reserve 80%+ inside its own base for more than 6 minutes in a row, or when no seed has a
        # house eliminated by minute 30 - 1.0.847 summed every enemy into "outnumbered" and never
        # attacked. One seed's elimination time is chance (12-seed sweep, run-ffa-army-deploys.py).
        # docs/formations.md, "Free-for-all: the army deploys".
        add_test(NAME quantbot_ffa_army_deploys COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-ffa-army-deploys.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/ffa-army-deploys)
        set_tests_properties(quantbot_ffa_army_deploys PROPERTIES TIMEOUT 1500 RUN_SERIAL TRUE)
        # Restores the overdue Palace timer in the stalled minute-35 city fixture, checks
        # that no Palace site exists, then requires other completed construction and no
        # prolonged all-yard idle/storage-cap plateau. Disk saves reset this planning timer.
        add_test(NAME quantbot_strategic_site_stall COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-strategic-site-stall.py
            --build-dir ${CMAKE_BINARY_DIR} --output-dir ${CMAKE_BINARY_DIR}/strategic-site-stall)
        set_tests_properties(quantbot_strategic_site_stall PROPERTIES TIMEOUT 1200 RUN_SERIAL TRUE)
        # Mandatory for every AI/unit/game-loop change (Stefan, 2026-10-18): Stefan's 4P DuneCity
        # Brutal Random Skirmish map, 6 game minutes headless, one path thread; fails when the mean
        # simulation CPU per cycle or the worst house update exceeds its budget, each a multiple of
        # a calibration loop timed in the same process. Then 30 s of a game started the way a
        # player starts it (main menu -> Random Skirmish -> Start, the app's whole per-frame work);
        # fails when per-frame time outside simulation and rendering exceeds its budget. Then 2
        # game minutes of Stefan's 8-house 384x384 save (1.0.849 fpsstillfucked.dls, rebuilt from
        # tests/performance/data/eight-house-384); fails when the p99 simulation CPU per cycle or
        # the p99 modelled 10-cycle frame exceeds its budget (1.0.849: 6.3 and 16.8 x calibration,
        # budgets 3 and 9). docs/game-performance.md.
        add_test(NAME perf_regression COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/performance/game-perf.py --scenario stefan-map --minutes 6
            --path-workers 1 --menu-launch 30 --eight-house 2 --check --label perf_regression
            --build-dir ${CMAKE_BINARY_DIR})
        set_tests_properties(perf_regression PROPERTIES TIMEOUT 1200 RUN_SERIAL TRUE)
        # MANDATORY with quantbot_all_against_atreides (AGENTS.md, CLAUDE.md): the smoke tier of
        # the AI scenario matrix (docs/ai-scenario-matrix.md) - one fast game per category: a
        # 4-house Random Skirmish free-for-all, a Construction Yard only duel (replayed: its final
        # state digest must reproduce), the reverse All against Atreides (Medium against four
        # Hard must lose), Hard v Easy on both sides, a 2v2 on Sihaya-Toto and three campaign
        # missions (levels 1, 4, 9; vanilla and DuneCity) the Hard partner must win. Fails on a
        # crash, a missed expected winner, a time-out, nobody eliminated or idle armies.
        add_test(NAME quantbot_scenario_matrix COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/scenario-matrix.py --tier smoke --fresh --jobs 3
            --build-dir ${CMAKE_BINARY_DIR} --results ${CMAKE_BINARY_DIR}/scenario-matrix-smoke)
        set_tests_properties(quantbot_scenario_matrix PROPERTIES TIMEOUT 1200 RUN_SERIAL TRUE)
        # Spice income: the lobby's per-house delivery factor reaches the live house, a real
        # refinery unloading a real harvester pays exactly the delivered cargo times that
        # factor at every accepted step, and the factor survives a save and load. All three
        # mods are measured: DuneCity adds city taxes, which must not earn at the factor, and
        # Tornie adds the Worfinery/Harvestank pair, which must.
        foreach(spice_mod IN ITEMS vanilla dunecity Tornie)
            add_test(NAME house_spice_income_${spice_mod} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --spice-income-probe --repeatable
                --mod ${spice_mod} --build-dir ${CMAKE_BINARY_DIR}
                --output-dir ${CMAKE_BINARY_DIR}/spice-income-${spice_mod}-probe)
            set_tests_properties(house_spice_income_${spice_mod} PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
        endforeach()
        # No population victory: Atreides level 4 (SCENA008-010, WinFlags=19) must not
        # end when the city passes 500 population; vanilla must behave the same.
        foreach(popwin_mod IN ITEMS vanilla dunecity)
            add_test(NAME campaign_no_population_victory_${popwin_mod} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --no-population-victory-probe --repeatable
                --level 4 --house atreides
                --mod ${popwin_mod} --build-dir ${CMAKE_BINARY_DIR}
                --output-dir ${CMAKE_BINARY_DIR}/no-population-victory-${popwin_mod}-probe)
            set_tests_properties(campaign_no_population_victory_${popwin_mod} PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        endforeach()
        # Credit storage: refined spice and city taxes share the refinery/silo
        # capacity, starting cash is exempt, and the overflow is destroyed at once.
        # City tax only exists with the dunecity mod, so both mods are measured.
        foreach(credit_mod IN ITEMS vanilla dunecity)
            add_test(NAME house_credit_storage_${credit_mod} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --credit-storage-probe --repeatable
                --mod ${credit_mod} --build-dir ${CMAKE_BINARY_DIR}
                --output-dir ${CMAKE_BINARY_DIR}/credit-storage-${credit_mod}-probe)
            set_tests_properties(house_credit_storage_${credit_mod} PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        endforeach()
        # The same probe in a custom game (a bundled multiplayer map), so the spice quota met by
        # refined spice and city tax credits alike is checked outside campaigns too, for every house.
        foreach(credit_mod IN ITEMS vanilla dunecity)
            add_test(NAME house_credit_storage_custom_${credit_mod} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --credit-storage-probe --repeatable
                --mod ${credit_mod}
                --custom-map "${CMAKE_SOURCE_DIR}/data/maps/multiplayer/2P - 128x128 - 1v1 - Habbanya-Penny.ini"
                --build-dir ${CMAKE_BINARY_DIR}
                --output-dir ${CMAKE_BINARY_DIR}/credit-storage-custom-${credit_mod}-probe)
            set_tests_properties(house_credit_storage_custom_${credit_mod} PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        endforeach()
        # Construction Yard pre-placement queue: sidebar planning, reservation commands
        # (chained and refused), save/load, placement on plan after moving a unit, the blocked
        # fallback with its ticker message, mixed queue order, cancel refunds, yard
        # destruction and a 9854 save. Writes preplace-ghosts.png / preplace-fallback.png.
        add_test(NAME preplace_queue_probe COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --preplace-probe --repeatable
            --mod dunecity --viewport 1024x768 --build-dir ${CMAKE_BINARY_DIR}
            --output-dir ${CMAKE_BINARY_DIR}/preplace-probe)
        set_tests_properties(preplace_queue_probe PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        # Sidebar build grid (docs/sidebar-grid.md): a DuneCity Construction Yard, a factory
        # with a repeating unit in production and a Starport with a sold-out item, rendered
        # through Game::drawScreen at both sizes. Writes sidebar-*-<viewport>.png.
        foreach(grid_viewport IN ITEMS 1024x768 1920x1080)
            add_test(NAME sidebar_grid_probe_${grid_viewport} COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/ai/run-campaign-balance.py --sidebar-grid-probe --repeatable
                --mod dunecity --level 9 --viewport ${grid_viewport} --build-dir ${CMAKE_BINARY_DIR}
                --output-dir ${CMAKE_BINARY_DIR}/sidebar-grid-${grid_viewport}-probe)
            set_tests_properties(sidebar_grid_probe_${grid_viewport} PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        endforeach()
    endif()
endif()

# Homebrew's Catch2 is not built with AddressSanitizer, and libc++ annotates its containers, so
# a sanitized macOS build reports false container overflows inside Catch2's argument parsing.
# libstdc++ on Linux does not annotate by default and needs nothing here.
if(APPLE AND DUNECITY_SANITIZE MATCHES "address")
    get_property(DUNECITY_ALL_TESTS DIRECTORY PROPERTY TESTS)
    set_property(TEST ${DUNECITY_ALL_TESTS} APPEND PROPERTY
        ENVIRONMENT "ASAN_OPTIONS=detect_container_overflow=0")
endif()

if(NOT EMSCRIPTEN AND NOT ANDROID)
    add_subdirectory(desktop-updater)
endif()
