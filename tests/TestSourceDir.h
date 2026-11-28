#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <system_error>

// The source checkout whose shipped files (mods/, config/, maps) a test reads.
// CTest sets DUNE_CITY_SOURCE_DIR. Run by hand without it, the checkout at or
// above the working directory is used, and outside a checkout the test fails with
// this message instead of crashing on a null path. __FILE__ is no substitute: the
// shared ccache's base_dir records it relative to the compiler's directory.
inline std::filesystem::path testSourceDir() {
    if(const char* configured = std::getenv("DUNE_CITY_SOURCE_DIR"); configured != nullptr && *configured != '\0') {
        return configured;
    }
    std::error_code ignored;
    for(auto dir = std::filesystem::current_path(ignored); !dir.empty(); dir = dir.parent_path()) {
        if(std::filesystem::exists(dir / "tests" / "TestSourceDir.h", ignored)) {
            return dir;
        }
        if(dir == dir.parent_path()) {
            break;
        }
    }
    throw std::runtime_error("DUNE_CITY_SOURCE_DIR is not set and the working directory is not inside a "
                             "DuneCity checkout: run the tests through ctest or set DUNE_CITY_SOURCE_DIR");
}
