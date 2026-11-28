#include <misc/DisplayVersion.h>
#include <config.h>

#if !DUNECITY_RELEASE_BUILD
#include <misc/FileSystem.h>
#include <filesystem>
#include <fstream>
#endif

std::string loadDisplayVersion(const std::string& resourceDirectory) {
#if !DUNECITY_RELEASE_BUILD
    if(!resourceDirectory.empty()) {
        std::ifstream input(std::filesystem::path(resourceDirectory) / "air-dev-version.txt");
        std::string version;
        if(std::getline(input, version)) {
            if(!version.empty() && version.back() == '\r') version.pop_back();
            if(!version.empty()) return version;
        }
    }
#else
    (void)resourceDirectory;
#endif
    return DUNECITY_DISPLAY_VERSION;
}

const std::string& getDisplayVersion() {
#if DUNECITY_RELEASE_BUILD
    static const std::string version = DUNECITY_DISPLAY_VERSION;
#else
    // This is the installed bundle's resource directory, not FileManager's
    // mod/user search path. SDL's existing bundle/base-path handling is reused.
    static const std::string version = loadDisplayVersion(getDuneLegacyDataDir());
#endif
    return version;
}
