#ifndef HEADLESSASSETS_H
#define HEADLESSASSETS_H

#include <SDL_log.h>
#include <chrono>
#include <cstdlib>

namespace HeadlessAssets {

// Profiling only: nested phases are inclusive (sounds includes voices).
class StartupTimer {
public:
    explicit StartupTimer(const char* phase)
        : phase_(phase), enabled_(std::getenv("DUNECITY_SIM_MEASURE") != nullptr) {
        if(enabled_) start_ = Clock::now();
    }
    ~StartupTimer() {
        if(enabled_) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start_).count();
            SDL_Log("ASSET_STARTUP: phase=%s ms=%.3f", phase_, elapsed / 1000.0);
        }
    }
private:
    using Clock = std::chrono::steady_clock;
    const char* phase_;
    bool enabled_;
    Clock::time_point start_{};
};

} // namespace HeadlessAssets

#endif
