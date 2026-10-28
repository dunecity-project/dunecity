# Updaters are deliberately absent from Android, iOS and browser packages.
option(DUNECITY_ENABLE_UPDATER "Build signed desktop update support" ON)
target_sources(dunecity PRIVATE ${CMAKE_SOURCE_DIR}/src/misc/DesktopUpdater.cpp)
if(NOT DUNECITY_ENABLE_UPDATER OR EMSCRIPTEN OR ANDROID OR IOS)
    return()
endif()

find_package(OpenSSL REQUIRED)
target_link_libraries(dunecity PRIVATE OpenSSL::Crypto)
target_compile_definitions(dunecity PRIVATE DUNECITY_DESKTOP_UPDATER=1)
target_sources(dunecity PRIVATE ${CMAKE_SOURCE_DIR}/src/Network/UpdateManifest.cpp)
set(DUNECITY_UPDATE_FEED_BASE "https://github.com/VR48/dunecity/releases/latest/download"
    CACHE STRING "HTTPS release channel containing signed update metadata")
if(NOT DUNECITY_UPDATE_FEED_BASE MATCHES "^https://[A-Za-z0-9./_-]+$")
    message(FATAL_ERROR "Update feed must be a plain HTTPS URL")
endif()
file(STRINGS "${CMAKE_SOURCE_DIR}/cmake/update-public-key.txt" DUNECITY_UPDATE_PUBLIC_KEY)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
include(FetchContent)
if(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
        set(DUNECITY_UPDATE_PLATFORM "macos-x86_64")
    else()
        set(DUNECITY_UPDATE_PLATFORM "macos-arm64")
    endif()
    FetchContent_Declare(dunecity_sparkle
        URL https://github.com/sparkle-project/Sparkle/releases/download/2.10.0/Sparkle-2.10.0.tar.xz
        URL_HASH SHA256=c2bf58aa8387266ac179357b1415d6f2635f044da8be41042af32425dae6da0c)
    FetchContent_MakeAvailable(dunecity_sparkle)
    install(FILES "${dunecity_sparkle_SOURCE_DIR}/LICENSE"
        DESTINATION "dunecity.app/Contents/Resources/licenses" RENAME Sparkle-LICENSE)
    target_sources(dunecity PRIVATE ${CMAKE_SOURCE_DIR}/src/misc/DesktopUpdaterMac.mm)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/misc/DesktopUpdaterMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(dunecity PRIVATE "${dunecity_sparkle_SOURCE_DIR}/Sparkle.framework")
    set_property(TARGET dunecity APPEND PROPERTY BUILD_RPATH "@executable_path/../Frameworks")
    set_property(TARGET dunecity APPEND PROPERTY INSTALL_RPATH "@executable_path/../Frameworks")
    add_custom_command(TARGET dunecity POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:dunecity>/Contents/Frameworks"
        COMMAND /usr/bin/ditto "${dunecity_sparkle_SOURCE_DIR}/Sparkle.framework"
            "$<TARGET_BUNDLE_DIR:dunecity>/Contents/Frameworks/Sparkle.framework")
elseif(WIN32)
    set(DUNECITY_UPDATE_PLATFORM "windows-x64")
    FetchContent_Declare(dunecity_winsparkle
        URL https://github.com/vslavik/winsparkle/releases/download/v0.9.4/WinSparkle-0.9.4.zip
        URL_HASH SHA256=6037df37fc263bd1650a1c4949681a9d40ffe991d01f35892a406cb5d103c976)
    FetchContent_MakeAvailable(dunecity_winsparkle)
    # The vendor ZIP also contains __MACOSX, so extraction retains the SDK's
    # version directory instead of stripping a single archive root.
    set(DUNECITY_WINSPARKLE_ROOT "${dunecity_winsparkle_SOURCE_DIR}")
    if(EXISTS "${DUNECITY_WINSPARKLE_ROOT}/WinSparkle-0.9.4/include/winsparkle.h")
        string(APPEND DUNECITY_WINSPARKLE_ROOT "/WinSparkle-0.9.4")
    endif()
    foreach(_sdk_file include/winsparkle.h x64/Release/WinSparkle.lib x64/Release/WinSparkle.dll COPYING COPYING.expat)
        if(NOT EXISTS "${DUNECITY_WINSPARKLE_ROOT}/${_sdk_file}")
            message(FATAL_ERROR "WinSparkle SDK is missing ${_sdk_file}")
        endif()
    endforeach()
    target_sources(dunecity PRIVATE ${CMAKE_SOURCE_DIR}/src/misc/DesktopUpdaterWindows.cpp)
    target_include_directories(dunecity PRIVATE "${DUNECITY_WINSPARKLE_ROOT}/include")
    target_link_libraries(dunecity PRIVATE "${DUNECITY_WINSPARKLE_ROOT}/x64/Release/WinSparkle.lib")
    add_custom_command(TARGET dunecity POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DUNECITY_WINSPARKLE_ROOT}/x64/Release/WinSparkle.dll" "$<TARGET_FILE_DIR:dunecity>")
    install(FILES "${DUNECITY_WINSPARKLE_ROOT}/x64/Release/WinSparkle.dll" DESTINATION .)
    install(FILES "${DUNECITY_WINSPARKLE_ROOT}/COPYING" DESTINATION licenses RENAME WinSparkle-COPYING)
    install(FILES "${DUNECITY_WINSPARKLE_ROOT}/COPYING.expat" DESTINATION licenses RENAME WinSparkle-COPYING.expat)
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(DUNECITY_UPDATE_PLATFORM "linux-arm64")
    else()
        set(DUNECITY_UPDATE_PLATFORM "linux-x86_64")
    endif()
    target_sources(dunecity PRIVATE ${CMAKE_SOURCE_DIR}/src/misc/AppImageUpdate.cpp)
endif()
configure_file(${CMAKE_SOURCE_DIR}/cmake/UpdateConfig.h.in ${CMAKE_BINARY_DIR}/include/UpdateConfig.h @ONLY)
