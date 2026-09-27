/*
 *  Dune City audio startup — opening the mixer, and staying silent when the
 *  machine has no usable audio device.
 *
 *  Kept apart from the rest of startup because the sequence is subtle: SDL only
 *  runs audio driver selection when its audio subsystem reference count goes
 *  from zero to one, and SDL_mixer takes such a reference inside
 *  Mix_OpenAudio(). A driver chosen with SDL_AudioInit() alone is therefore
 *  thrown away again by the very call that is supposed to use it.
 */

#ifndef AUDIO_AUDIOSTARTUP_H
#define AUDIO_AUDIOSTARTUP_H

#include <SDL_stdinc.h>

#include <string>

namespace DuneAudio {

struct MixerStartup {
    /// The mixer is open and usable. Music, sound effects and the cutscene
    /// players can be created whether or not the fallback was needed.
    bool opened = false;
    /// The mixer is open on the silent driver because the preferred device
    /// could not be used. False when the preferred device opened normally,
    /// even if that device was itself already silent.
    bool silent = false;
    /// SDL_GetCurrentAudioDriver() once the mixer is open; empty on failure.
    std::string driver;
    /// Why the preferred audio device could not be opened; empty if it opened.
    std::string deviceError;
    /// Why the silent fallback could not be opened either; empty unless the
    /// fallback was attempted and failed.
    std::string silentError;
};

/**
 * Opens the SDL_mixer output, falling back to silent audio if the machine's
 * audio device or audio backend cannot be used.
 *
 * The fallback pins the silent driver where SDL's own default driver selection
 * reads it and holds an audio-subsystem reference of its own, so the choice
 * survives SDL_mixer re-initialising the subsystem internally.
 */
MixerStartup openMixerWithSilentFallback(int frequency, Uint16 format, int channels, int bufferFrames);

/**
 * Closes the mixer and releases every audio-subsystem reference, including the
 * one the silent fallback holds. The game leaves teardown to Mix_CloseAudio()
 * and SDL_Quit(); this exists so the regression tests can return the process
 * to a known state between scenarios.
 */
void shutdownMixer();

} // namespace DuneAudio

#endif // AUDIO_AUDIOSTARTUP_H
