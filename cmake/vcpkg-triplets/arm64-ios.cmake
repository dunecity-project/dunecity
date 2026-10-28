# iPhone and iPad hardware. Same as vcpkg's community triplet, plus the SDK
# (make-based ports such as OpenSSL otherwise fall back to the macOS SDK) and
# the deployment target so every static dependency matches the game's minimum.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME iOS)
set(VCPKG_OSX_SYSROOT iphoneos)
set(VCPKG_OSX_DEPLOYMENT_TARGET 15.0)

# curl's configure check finds pipe2() in the iOS 27 SDK, which is unavailable
# before iOS 27; with the iOS 15 minimum that is a hard availability error.
if(PORT STREQUAL "curl")
    set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DHAVE_PIPE2=OFF)
endif()
