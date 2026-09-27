/*
 *  Dune City audio startup — see audio/AudioStartup.h.
 */

#include <audio/AudioStartup.h>

#include <SDL.h>
#include <SDL_mixer.h>

namespace DuneAudio {

namespace {

constexpr char SILENT_DRIVER[] = "dummy";

std::string lastError() {
    const char* error = SDL_GetError();
    return (error != nullptr && *error != '\0') ? std::string{error} : std::string{"unknown error"};
}

std::string currentDriver() {
    const char* driver = SDL_GetCurrentAudioDriver();
    return driver != nullptr ? std::string{driver} : std::string{};
}

/**
 * Brings SDL's audio subsystem back to "no reference, no driver".
 *
 * A failed Mix_OpenAudio() can leave either state behind: it takes a reference
 * through SDL_InitSubSystem() before it opens the device, and whether that
 * reference is returned on the failure path differs between SDL_mixer
 * releases. Both have to collapse to zero before a different driver can be
 * chosen, because SDL only runs driver selection on the zero-to-one
 * transition. Each SDL_QuitSubSystem() releases one reference.
 */
void releaseAudioSubsystem() {
    while(SDL_WasInit(SDL_INIT_AUDIO) != 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    if(SDL_GetCurrentAudioDriver() != nullptr) {
        SDL_AudioQuit();
    }
}

/**
 * Points SDL's default driver selection at the silent driver.
 *
 * This is the part that has to be in place before anything initialises the
 * audio subsystem again. SDL_AudioInit("dummy") only writes SDL's current
 * driver; it is outside the subsystem reference counting, so the next
 * zero-to-one SDL_InitSubSystem() — which is what Mix_OpenAudio() performs
 * internally — discards it and re-runs the default selection. The hint and the
 * environment variable are where that selection reads its answer, and the
 * override priority is required so an SDL_AUDIODRIVER already set by the user
 * or a test harness cannot win against the fallback.
 */
void selectSilentDriver() {
#ifdef SDL_HINT_AUDIODRIVER
    SDL_SetHintWithPriority(SDL_HINT_AUDIODRIVER, SILENT_DRIVER, SDL_HINT_OVERRIDE);
#endif
    SDL_setenv("SDL_AUDIODRIVER", SILENT_DRIVER, 1);
}

} // anonymous namespace

MixerStartup openMixerWithSilentFallback(int frequency, Uint16 format, int channels, int bufferFrames) {
    MixerStartup result;

    // SDL's error is sticky. Capture only errors from the current attempt.
    SDL_ClearError();
    if(Mix_OpenAudio(frequency, format, channels, bufferFrames) == 0) {
        result.opened = true;
        result.driver = currentDriver();
        return result;
    }
    result.deviceError = lastError();

    releaseAudioSubsystem();
    selectSilentDriver();

    SDL_ClearError();
    if(SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        result.silentError = lastError();
        return result;
    }

    // The reference taken above is ours and is never returned, so no later
    // SDL_InitSubSystem() can re-run driver selection. That makes it safe to
    // correct the driver here for runtimes whose default selection did not read
    // the hint we set, without the correction being undone again.
    if(currentDriver() != SILENT_DRIVER) {
        SDL_ClearError();
        if(SDL_AudioInit(SILENT_DRIVER) < 0) {
            result.silentError = lastError();
            return result;
        }
    }

    SDL_ClearError();
    if(Mix_OpenAudio(frequency, format, channels, bufferFrames) < 0) {
        result.silentError = lastError();
        return result;
    }

    result.opened = true;
    result.silent = true;
    result.driver = currentDriver();
    return result;
}

void shutdownMixer() {
    Mix_CloseAudio();
    releaseAudioSubsystem();
}

} // namespace DuneAudio
