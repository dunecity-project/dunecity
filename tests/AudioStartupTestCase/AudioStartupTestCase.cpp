// Audio startup regressions for GitHub issue #56.
//
// A native Linux Mint 1.0.760 AppImage logged "Audio device unavailable: dsp:
// No such audio device. Continuing with silent audio; restart to retry the
// device." and then threw "Audio device failed (dsp: No such audio device);
// silent mixer also failed: dsp: No such audio device" — the same driver, the
// same error, from the fallback that was meant to be silent. These drive the
// real production startup path against a real SDL_mixer; the audio device is
// forced to be unusable rather than mocked away.

#include <catch2/catch_test_macros.hpp>

#include <audio/AudioStartup.h>

#include <SDL.h>
#include <SDL_mixer.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr int TEST_FREQUENCY = 44100;
constexpr int TEST_BUFFER_FRAMES = 1024;
constexpr char DRIVER_VARIABLE[] = "SDL_AUDIODRIVER";

std::string currentDriver() {
    const char* driver = SDL_GetCurrentAudioDriver();
    return driver != nullptr ? std::string{driver} : std::string{};
}

void clearVariable(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

/**
 * Chooses the audio driver for one scenario and puts the process back as it
 * was afterwards.
 *
 * The driver is set through both the environment variable and the matching
 * hint, at override priority, because that is where the fallback writes its own
 * choice: a leftover hint from an earlier scenario would otherwise decide this
 * one. The mixer and every audio-subsystem reference are released on the way
 * out for the same reason.
 */
class AudioEnvironment {
public:
    AudioEnvironment() {
        const char* driver = SDL_getenv(DRIVER_VARIABLE);
        originalDriver_ = driver != nullptr ? std::string{driver} : std::string{};
        hadDriver_ = driver != nullptr;
    }

    AudioEnvironment(const AudioEnvironment&) = delete;
    AudioEnvironment& operator=(const AudioEnvironment&) = delete;

    ~AudioEnvironment() {
        DuneAudio::shutdownMixer();
        for(const auto& variable : extraVariables_) {
            if(variable.present) {
                SDL_setenv(variable.name.c_str(), variable.value.c_str(), 1);
            } else {
                clearVariable(variable.name.c_str());
            }
        }
        if(hadDriver_) {
            SDL_setenv(DRIVER_VARIABLE, originalDriver_.c_str(), 1);
        } else {
            clearVariable(DRIVER_VARIABLE);
        }
        setDriverHint(originalDriver_.c_str());
    }

    void setDriver(const char* name) {
        SDL_setenv(DRIVER_VARIABLE, name, 1);
        setDriverHint(name);
    }

    void setVariable(const char* name, const char* value) {
        const char* previous = SDL_getenv(name);
        extraVariables_.push_back({std::string{name}, previous != nullptr,
                                   previous != nullptr ? std::string{previous} : std::string{}});
        SDL_setenv(name, value, 1);
    }

private:
    struct SavedVariable {
        std::string name;
        bool present;
        std::string value;
    };

    static void setDriverHint(const char* value) {
#ifdef SDL_HINT_AUDIODRIVER
        SDL_SetHintWithPriority(SDL_HINT_AUDIODRIVER, value, SDL_HINT_OVERRIDE);
#else
        (void)value;
#endif
    }

    std::string originalDriver_;
    bool hadDriver_ = false;
    std::vector<SavedVariable> extraVariables_;
};

} // anonymous namespace

TEST_CASE("An unavailable audio backend falls back to a usable silent mixer", "[audio][startup]") {
    AudioEnvironment audio;
    audio.setDriver("dunecity-no-such-audio-backend");

    const DuneAudio::MixerStartup startup = DuneAudio::openMixerWithSilentFallback(
        TEST_FREQUENCY, AUDIO_S16SYS, 2, TEST_BUFFER_FRAMES);

    REQUIRE_FALSE(startup.deviceError.empty());
    REQUIRE(startup.silentError.empty());
    REQUIRE(startup.opened);
    REQUIRE(startup.silent);
    REQUIRE(startup.driver == "dummy");
    REQUIRE(currentDriver() == "dummy");

    // Open is not enough: music, sound effects and the cutscene players all
    // need a mixer that answers and hands out channels.
    int frequency = 0;
    int channels = 0;
    Uint16 format = 0;
    REQUIRE(Mix_QuerySpec(&frequency, &format, &channels) == 1);
    REQUIRE(frequency > 0);
    REQUIRE(channels > 0);
    REQUIRE(Mix_AllocateChannels(28) == 28);
}

TEST_CASE("The silent fallback survives the mixer re-initialising the audio subsystem", "[audio][startup]") {
    // This is the failure in issue #56. Mix_OpenAudio() takes its own
    // audio-subsystem reference, and SDL only runs driver selection when that
    // count rises from zero — so whatever the environment and the hint say at
    // that moment is what the mixer gets. The shipped fallback named the silent
    // driver with SDL_AudioInit() alone, which sits outside that reference
    // counting, so the next selection picked the unusable device driver again
    // and reported its error a second time. Releasing the subsystem here and
    // letting SDL choose for itself reproduces exactly that moment.
    AudioEnvironment audio;
    audio.setDriver("dunecity-no-such-audio-backend");

    const DuneAudio::MixerStartup startup = DuneAudio::openMixerWithSilentFallback(
        TEST_FREQUENCY, AUDIO_S16SYS, 2, TEST_BUFFER_FRAMES);
    REQUIRE(startup.opened);
    REQUIRE(startup.silent);

    DuneAudio::shutdownMixer();
    REQUIRE(SDL_WasInit(SDL_INIT_AUDIO) == 0);

    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO) == 0);
    REQUIRE(currentDriver() == "dummy");
    REQUIRE(Mix_OpenAudio(TEST_FREQUENCY, AUDIO_S16SYS, 2, TEST_BUFFER_FRAMES) == 0);
    REQUIRE(currentDriver() == "dummy");
}

TEST_CASE("A driver that initialises but cannot open a device reaches silent audio", "[audio][startup]") {
    // Also cover a backend that initialises successfully but cannot open its
    // device. SDL's disk driver supplies this case without a sound card when
    // its output file cannot be created. Set both SDL2 and SDL3 variable names.
    AudioEnvironment audio;
    audio.setDriver("disk");
    audio.setVariable("SDL_DISKAUDIOFILE", "/dunecity-issue-56-no-such-directory/silent.raw");
    audio.setVariable("SDL_AUDIO_DISK_OUTFILE", "/dunecity-issue-56-no-such-directory/silent.raw");

    const DuneAudio::MixerStartup startup = DuneAudio::openMixerWithSilentFallback(
        TEST_FREQUENCY, AUDIO_S16SYS, 2, TEST_BUFFER_FRAMES);

    if(startup.deviceError.empty()) {
        SKIP("this SDL build opened the forced disk device; no device failure to recover from");
    }
    REQUIRE(startup.opened);
    REQUIRE(startup.silent);
    REQUIRE(startup.driver == "dummy");
    REQUIRE(currentDriver() == "dummy");
    int frequency = 0;
    int channels = 0;
    Uint16 format = 0;
    REQUIRE(Mix_QuerySpec(&frequency, &format, &channels) == 1);
}

TEST_CASE("A working audio device opens the mixer without the silent fallback", "[audio][startup]") {
    // Normal audio must not be routed through the fallback, and must not report
    // a device error. The silent driver stands in for a working device: the
    // tests have no sound card to rely on, and what is checked here is which
    // path startup took, not the sound it made.
    AudioEnvironment audio;
    audio.setDriver("dummy");

    const DuneAudio::MixerStartup startup = DuneAudio::openMixerWithSilentFallback(
        TEST_FREQUENCY, AUDIO_S16SYS, 2, TEST_BUFFER_FRAMES);

    REQUIRE(startup.opened);
    REQUIRE_FALSE(startup.silent);
    REQUIRE(startup.deviceError.empty());
    REQUIRE(startup.silentError.empty());
    REQUIRE(startup.driver == "dummy");

    int frequency = 0;
    int channels = 0;
    Uint16 format = 0;
    REQUIRE(Mix_QuerySpec(&frequency, &format, &channels) == 1);
    REQUIRE(Mix_AllocateChannels(28) == 28);
}
