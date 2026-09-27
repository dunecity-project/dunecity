cmake_minimum_required(VERSION 3.15)

# Project name and version
project(DuneCity VERSION 1.0.790 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include cmake modules
include(CMakeDependentOption)
include(CTest)
include(GNUInstallDirs)

# Build type configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# Release build optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(APPLE)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum OS X deployment version" FORCE)
        # Network/config validation and deterministic simulation share these flags.
        # Fast-math assumes finite inputs and can remove security checks; it also
        # changes arithmetic relative to the browser and other native builds.
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
    endif()
endif()

# Check for modern C++ support
if(MSVC)
    # Use C++17 consistently
    set(CMAKE_CXX_STANDARD 17)
else()
    # Check if compiler supports C++17
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-std=c++17" COMPILER_SUPPORTS_CXX17)
    
    if(COMPILER_SUPPORTS_CXX17)
        set(CMAKE_CXX_STANDARD 17)
    else()
        set(CMAKE_CXX_STANDARD 14)
        message(STATUS "C++17 not supported, falling back to C++14")
    endif()
endif()

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Options
option(DUNECITY_BUILD_TESTS "Build test suite" OFF)
# Direct peer-to-peer play. ON natively; the browser build uses P2PKit's RTCTransport through
# platform/web/src/dune-direct-bridge.ts instead of a native WebRTC library, so it is forced off
# there and the Emscripten backend is compiled in its place.
option(DUNECITY_DIRECT_P2P "Build the native direct peer-to-peer transport (libdatachannel)" ON)
option(DUNECITY_ENABLE_PCH "Enable precompiled headers" ON)
option(DUNECITY_STATIC_RUNTIME "Use static runtime libraries" OFF)

# Native dependencies come from vcpkg. Emscripten supplies browser ports for
# SDL and HTTP is handled through the browser fetch layer.
if(NOT EMSCRIPTEN)
    find_package(SDL2 CONFIG REQUIRED)
    find_package(SDL2_mixer CONFIG REQUIRED)
    find_package(SDL2_ttf CONFIG REQUIRED)
    find_package(CURL REQUIRED)
    find_package(ZLIB REQUIRED)
endif()

if(EMSCRIPTEN)
    set(DUNECITY_DIRECT_P2P OFF)
endif()

# libdatachannel carries the gameplay of a direct match natively, and has to interoperate with
# the browser's own WebRTC stack. It is pinned to a commit rather than a tag: a tag can be moved,
# and this is the library that terminates DTLS for every direct match.
#
# FetchContent at the pinned commit is the only default. Resolving whatever libdatachannel
# happens to be installed would quietly swap out a security dependency, and the installed package
# does not export the same target name anyway (it exports LibDataChannel::LibDataChannel, not the
# ::LibDataChannelStatic alias), so accepting it needs an explicit opt-in rather than a lucky
# find_package. To build against a local checkout of the same commit - which is what the macOS
# builder does, without a network fetch:
#
#   -DFETCHCONTENT_SOURCE_DIR_LIBDATACHANNEL=/path/to/libdatachannel
#
# and to use a verified prebuilt install instead:
#
#   -DDUNECITY_SYSTEM_LIBDATACHANNEL=ON -DCMAKE_PREFIX_PATH=/path/to/install
option(DUNECITY_SYSTEM_LIBDATACHANNEL
       "Use an installed libdatachannel instead of the pinned source" OFF)
set(DUNECITY_LIBDATACHANNEL_COMMIT "443f6934d9007eb7076ab7825ba330f355fcbead"
    CACHE STRING "Pinned libdatachannel commit (v0.24.5)")

if(DUNECITY_DIRECT_P2P)
    set(DUNECITY_LIBDATACHANNEL_TARGET "")
    if(DUNECITY_SYSTEM_LIBDATACHANNEL)
        find_package(LibDataChannel CONFIG REQUIRED)
        find_package(Threads REQUIRED)
        find_package(OpenSSL)
        foreach(CANDIDATE LibDataChannel::LibDataChannelStatic LibDataChannel::LibDataChannel)
            if(TARGET ${CANDIDATE} AND NOT DUNECITY_LIBDATACHANNEL_TARGET)
                set(DUNECITY_LIBDATACHANNEL_TARGET ${CANDIDATE})
            endif()
        endforeach()
        if(NOT DUNECITY_LIBDATACHANNEL_TARGET)
            message(FATAL_ERROR
                "DUNECITY_SYSTEM_LIBDATACHANNEL is ON but the package exported no usable target.")
        endif()
        message(STATUS "Direct play uses the installed ${DUNECITY_LIBDATACHANNEL_TARGET}; "
                       "verify it is commit ${DUNECITY_LIBDATACHANNEL_COMMIT}")
    else()
        include(FetchContent)
        # libdatachannel terminates DTLS with OpenSSL. On Windows libcurl uses Schannel, so
        # nothing else in this build pulls OpenSSL in and a clean CI machine would fail inside
        # libdatachannel's own find_package. The vcpkg manifest carries it explicitly for that
        # reason; asking for it here as well turns a confusing nested failure into a clear one.
        find_package(OpenSSL QUIET)
        if(NOT OpenSSL_FOUND)
            message(STATUS "OpenSSL was not found up front; libdatachannel will look for it "
                           "itself. If that fails, install the openssl dependency.")
        endif()
        # Scoped so that turning libdatachannel into a static library does not decide how every
        # other dependency in this build is linked.
        set(DUNECITY_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS OFF)
        set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
        set(NO_MEDIA ON CACHE BOOL "" FORCE)
        set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
        set(NO_TESTS ON CACHE BOOL "" FORCE)
        FetchContent_Declare(libdatachannel
            GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
            GIT_TAG ${DUNECITY_LIBDATACHANNEL_COMMIT}
            GIT_SUBMODULES_RECURSE ON)
        FetchContent_MakeAvailable(libdatachannel)
        set(BUILD_SHARED_LIBS ${DUNECITY_SAVED_BUILD_SHARED_LIBS})
        foreach(CANDIDATE LibDataChannel::LibDataChannelStatic LibDataChannel::LibDataChannel
                          datachannel-static datachannel)
            if(TARGET ${CANDIDATE} AND NOT DUNECITY_LIBDATACHANNEL_TARGET)
                set(DUNECITY_LIBDATACHANNEL_TARGET ${CANDIDATE})
            endif()
        endforeach()
        if(NOT DUNECITY_LIBDATACHANNEL_TARGET)
            message(FATAL_ERROR
                "libdatachannel ${DUNECITY_LIBDATACHANNEL_COMMIT} built no target this project "
                "recognises. Configure with -DDUNECITY_DIRECT_P2P=OFF to build without direct "
                "online play.")
        endif()
    endif()
endif()

# Platform-specific settings
if(WIN32)
    # Windows-specific settings
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    
    # Check for Visual Studio
    if(MSVC)
        # Set Visual Studio specific options
        set(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION "10.0")
        set(CMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE "x64")
        
        # Runtime library settings
        if(DUNECITY_STATIC_RUNTIME)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        else()
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        endif()
        
        # Precompiled header settings
        if(DUNECITY_ENABLE_PCH)
            set(PCH_SOURCE "${CMAKE_SOURCE_DIR}/IDE/VC/stdafx.cpp")
            set(PCH_HEADER "${CMAKE_SOURCE_DIR}/IDE/VC/stdafx.h")
        endif()
    endif()
elseif(NOT EMSCRIPTEN)
    # Unix-like systems
    find_package(Threads REQUIRED)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()

# Global compile options
if(MSVC)
    add_compile_options(/W4 /WX-)
    # v1.0.515: silence C4100 (unreferenced parameter) globally for the
    # Scaler::tripleTiledSurfaceNN wrapper and other tiny plumbing that
    # uses intentional (void)param discards but still trips MSVC /W4.
    # Also silence C4702 (unreachable code) inside the bundled third-party
    # fmtlib header (include/misc/format.h, line 1493 is one such site);
    # we do not want to fork the upstream library just to keep its
    # defensive unreachable branches in line with our warning policy.
    add_compile_options(/wd4100 /wd4702)
    # Prevent Windows.h from defining min/max macros that conflict with std::min/std::max
    add_compile_definitions(NOMINMAX)
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -pedantic)
    add_compile_options(-Wno-unused-parameter -Wno-unused-but-set-variable -Wno-unknown-pragmas) # FIXME These should be cleaned up.
    add_compile_options(-Wno-expansion-to-defined)  # Silences "define FMT_HAS_FEATURE(x) __has_feature(x) may not be portable" warnings.
endif()

# The p2pkit Emscripten SDK (C++ headers under emscripten/include/p2pkit-wasm/
# and the --js-library glue under emscripten/js/) is a real dependency,
# installed from the pinned git revision in platform/web/package.json. The
# packet streams, the WebRTC transport (src) and the packet tests (tests)
# consume it; nothing SDK-owned is copied into our tree.
set(DUNECITY_P2PKIT_DIR "${CMAKE_SOURCE_DIR}/platform/web/node_modules/p2pkit")
if(NOT EXISTS "${DUNECITY_P2PKIT_DIR}/emscripten/include/p2pkit-wasm/packet.h")
    message(FATAL_ERROR
        "p2pkit SDK not installed: ${DUNECITY_P2PKIT_DIR}/emscripten/include/p2pkit-wasm/ is missing. "
        "Run: npm install  (in platform/web)  to install the p2pkit revision pinned in "
        "platform/web/package.json, then reconfigure.")
endif()

# Add subdirectories
add_subdirectory(src)
if(DUNECITY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Create a custom target for copying data files (non-macOS platforms)
# macOS bundles handle data copying in src/CMakeLists.txt
if(NOT APPLE AND NOT EMSCRIPTEN)
    add_custom_target(copy_data ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/data/"
        "$<TARGET_FILE_DIR:dunecity>/data/"
        COMMENT "Copying data files to bin/data directory"
    )
    
    # Add dependency to ensure data files are copied after main target is built
    add_dependencies(copy_data dunecity)
endif()


# Browser builds are copied directly into the website by package-web.ps1 and
# do not use the native install or CPack rules below.
if(NOT EMSCRIPTEN)

# Platform-specific install configuration
if(WIN32)
    # Windows: install all files to root directory (not in a share subdirectory)
    set(DUNECITY_INSTALL_ROOT ".")
    set(DUNECITY_DATA_DIR ".")
    set(DUNECITY_CONFIG_DIR "config")
    set(DUNECITY_DOC_DIR ".")
elseif(APPLE)
    # macOS already handled via bundle
    set(DUNECITY_INSTALL_ROOT ".")
    set(DUNECITY_DATA_DIR "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}")
    set(DUNECITY_CONFIG_DIR "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/config")
    set(DUNECITY_DOC_DIR "${CMAKE_INSTALL_DOCDIR}")
else()
    # Unix/Linux FHS structure
    set(DUNECITY_INSTALL_ROOT "${CMAKE_INSTALL_BINDIR}")
    set(DUNECITY_DATA_DIR "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}")
    set(DUNECITY_CONFIG_DIR "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/config")
    set(DUNECITY_DOC_DIR "${CMAKE_INSTALL_DOCDIR}")
endif()

# Install data files
# Install configuration template files to config subdirectory
# For macOS bundles, these are already inside the .app via POST_BUILD commands
# For Windows with VS build, config files are installed from VS output directory to root
if(NOT APPLE)
    set(VS_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/Release-x64")
    if(WIN32 AND EXISTS "${VS_OUTPUT_DIR}/config")
        # Config files will be installed from VS output directory to root (see src/CMakeLists.txt)
    else()
        install(FILES 
            config/ObjectData.ini.default
            "config/QuantBot Config.ini.default"
            "config/Dune City.ini"
            config/README.md
            DESTINATION ${DUNECITY_CONFIG_DIR}
        )
    endif()
endif()

# Install desktop file and icons on Linux
if(UNIX AND NOT APPLE)
    install(FILES dunecity.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES dunecity.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/48x48/apps
    )
    install(FILES dunecity-128x128.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps
        RENAME dunecity.png
    )
    install(FILES dunecity.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    )
endif()

# Install the file itself: README is a source-tree symlink to README.md.
# Copying that link alone leaves Windows/Linux packages with a missing target.
if(NOT APPLE)
    install(FILES README.md DESTINATION ${DUNECITY_DOC_DIR} RENAME README)
endif()

# Install documentation
if(WIN32)
    # Windows: install docs to root
    install(FILES AUTHORS COPYING NEWS
        DESTINATION ${DUNECITY_DOC_DIR}
    )
    # Install release notes
    install(FILES "release_notes/RELEASE_NOTES.md"
        DESTINATION ${DUNECITY_DOC_DIR}
        RENAME "Release_Notes.md"
    )
elseif(NOT APPLE)
    # Linux: use standard doc directory
    install(FILES AUTHORS COPYING NEWS
        DESTINATION ${DUNECITY_DOC_DIR}
    )
    install(FILES "release_notes/RELEASE_NOTES.md"
        DESTINATION ${DUNECITY_DOC_DIR}
        RENAME "Release_Notes.md"
        OPTIONAL
    )
endif()

# CPack configuration for packaging
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VENDOR "Dune City Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_HOMEPAGE_URL ${PROJECT_HOMEPAGE_URL})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

if(WIN32)
    include(cmake/WindowsInstaller.cmake)

    # Add custom target for Windows installer
    add_custom_target(installer
        COMMAND ${CMAKE_COMMAND} -E echo "Cleaning CPack cache and install directory..."
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/_CPack_Packages
        COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/*.exe
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/install
        COMMAND ${CMAKE_COMMAND} -E echo "Building binary..."
        COMMAND ${CMAKE_COMMAND} --build . --config Release
        COMMAND ${CMAKE_COMMAND} -E echo "Removing install directory to force fresh install..."
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/install
        COMMAND ${CMAKE_COMMAND} -E echo "Installing to staging directory..."
        COMMAND ${CMAKE_COMMAND} --install . --prefix install --config Release
        COMMAND ${CMAKE_COMMAND} -E echo "Creating installer..."
        COMMAND ${CMAKE_CPACK_COMMAND} -G NSIS -C Release
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building Windows NSIS installer (with guaranteed fresh install)..."
    )
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Dune City ${PROJECT_VERSION}")
    set(CPACK_DMG_FORMAT "UDZO")  # Compressed
    set(CPACK_PACKAGE_FILE_NAME "DuneCity-${PROJECT_VERSION}-macOS")
    set(MACOSX_BUNDLE_ICON_FILE "dunecity.icns")
    
    # Install README and release notes to DMG root (not inside .app)
    install(FILES README.md DESTINATION . RENAME README COMPONENT Runtime)
    install(FILES "release_notes/RELEASE_NOTES.md" 
            DESTINATION . 
            RENAME "Release Notes.md"
            COMPONENT Runtime
            OPTIONAL)
    
    # Optional: Add custom DMG background and layout if files exist
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/dmg_background.png")
        set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/dmg_background.png")
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/DMGSetup.scpt")
        set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/packaging/DMGSetup.scpt")
    endif()
    
    # Add custom target for DMG creation
    # NOTE: We bypass CPack entirely due to persistent caching issues where old binaries
    # would get packaged even after fresh builds. This manual approach uses hdiutil directly.
    add_custom_target(dmg
        COMMAND ${CMAKE_COMMAND} -E echo "Building binary..."
        COMMAND ${CMAKE_COMMAND} --build . --config Release
        COMMAND ${CMAKE_COMMAND} -E echo "Cleaning old DMG and staging..."
        COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/DuneCity-${PROJECT_VERSION}-macOS.dmg
        COMMAND ${CMAKE_COMMAND} -E remove_directory /tmp/dunecity_dmg_staging
        COMMAND ${CMAKE_COMMAND} -E echo "Staging files for DMG..."
        COMMAND ${CMAKE_COMMAND} -E make_directory /tmp/dunecity_dmg_staging
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/bin/dunecity.app /tmp/dunecity_dmg_staging/dunecity.app
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/README /tmp/dunecity_dmg_staging/README
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/release_notes/RELEASE_NOTES.md /tmp/dunecity_dmg_staging/Release\\ Notes.md
        COMMAND ${CMAKE_COMMAND} -E echo "Creating Applications symlink for drag-and-drop install..."
        COMMAND ln -sf /Applications /tmp/dunecity_dmg_staging/Applications
        COMMAND ${CMAKE_COMMAND} -E echo "Creating DMG with hdiutil..."
        COMMAND hdiutil create -volname "Dune City ${PROJECT_VERSION}" -srcfolder /tmp/dunecity_dmg_staging -ov -format UDZO ${CMAKE_BINARY_DIR}/DuneCity-${PROJECT_VERSION}-macOS.dmg
        COMMAND ${CMAKE_COMMAND} -E echo "Cleaning up staging..."
        COMMAND ${CMAKE_COMMAND} -E remove_directory /tmp/dunecity_dmg_staging
        COMMAND ${CMAKE_COMMAND} -E echo "DMG created: DuneCity-${PROJECT_VERSION}-macOS.dmg"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building macOS DMG installer (manual hdiutil - bypasses CPack caching issues)..."
    )
else()
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    # Native packages belong under /usr. Leaving CPack at its default
    # /usr/local prefix hides launchers from some desktop environments and
    # makes package removal appear incomplete.
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    set(CPACK_DEBIAN_PACKAGE_NAME "dunecity")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    # SDL2, SDL2_mixer, SDL2_ttf, curl, miniupnpc, and discord-rpc are all
    # bundled from vcpkg into lib/dunecity/ — do not declare them as system
    # package dependencies. Only the baseline C/C++ runtime is required.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17), libstdc++6 (>= 6)")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Dune City Team")
    set(CPACK_RPM_PACKAGE_LICENSE "GPL-2.0+")
    set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
    set(CPACK_PACKAGE_FILE_NAME "DuneCity-${PROJECT_VERSION}-Linux-x64")
    
    # Add custom target for Linux packages. CMake reserves the target
    # name "package" when CPack is enabled (newer CMake versions error
    # out at configure time), so use "linux-packages" instead. Update
    # BUILD.md and .vscode/tasks.json to match.
    add_custom_target(linux-packages
        COMMAND ${CMAKE_COMMAND} -E echo "Cleaning CPack cache and install directory..."
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/_CPack_Packages
        COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/*.deb ${CMAKE_BINARY_DIR}/*.rpm ${CMAKE_BINARY_DIR}/*.tar.gz
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/install
        COMMAND ${CMAKE_COMMAND} -E echo "Building binary..."
        COMMAND ${CMAKE_COMMAND} --build . --config Release
        COMMAND ${CMAKE_COMMAND} -E echo "Removing install directory to force fresh install..."
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/install
        COMMAND ${CMAKE_COMMAND} -E echo "Installing to staging directory..."
        COMMAND ${CMAKE_COMMAND} --install . --prefix install --config Release
        COMMAND ${CMAKE_COMMAND} -E echo "Creating packages..."
        COMMAND ${CMAKE_CPACK_COMMAND} -G "DEB;RPM;TGZ" -C Release
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building Linux packages (DEB, RPM, TGZ) (with guaranteed fresh install)..."
    )
endif()

include(CPack) 

endif()
