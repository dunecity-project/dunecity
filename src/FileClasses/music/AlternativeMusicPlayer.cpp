#include <FileClasses/music/AlternativeMusicPlayer.h>

#include <FileClasses/music/ADLPlayer.h>
#include <FileClasses/music/DirectoryPlayer.h>
#include <FileClasses/music/XMIPlayer.h>
#include <globals.h>
#include <mmath.h>

#include <stdexcept>
#include <utility>

AlternativeMusicPlayer::AlternativeMusicPlayer(std::unique_ptr<MusicPlayer> player, SoundtrackPack soundtrack)
    : MusicPlayer(player->isMusicOn(), player->getMusicVolume()),
      currentPlayer(std::move(player)), pack(std::move(soundtrack)) {
    currentPlayer->setMusic(false);
    Mix_Init(MIX_INIT_OGG | MIX_INIT_MP3 | MIX_INIT_FLAC);
}

AlternativeMusicPlayer::~AlternativeMusicPlayer() {
    stop();
    Mix_Quit();
}

void AlternativeMusicPlayer::stop() {
    if(usingCurrentPlayer) {
        currentPlayer->setMusic(false);
        usingCurrentPlayer = false;
    }
    if(music != nullptr) {
        Mix_HaltMusic();
        Mix_FreeMusic(music);
        music = nullptr;
    }
}

void AlternativeMusicPlayer::changeMusic(MUSICTYPE musicType) {
    if(!musicOn || (currentMusicType == musicType && isMusicPlaying())) return;
    stop();
    currentMusicType = musicType;

    const auto count = pack.trackCount(musicType);
    const auto* filename = count == 0 ? nullptr
        : pack.trackFor(musicType, static_cast<std::size_t>(getRandomInt(0, static_cast<int>(count) - 1)));
    if(filename != nullptr) {
        music = Mix_LoadMUS(filename->c_str());
        if(music != nullptr && Mix_PlayMusic(music, 0) == 0) {
            Mix_VolumeMusic(musicVolume);
            SDL_Log("Alternative soundtrack: slot=%s file=%s", SoundtrackPack::slotName(musicType), filename->c_str());
            return;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot play alternative soundtrack %s: %s; using current soundtrack",
                    filename->c_str(), Mix_GetError());
        stop();
    }

    SDL_Log("Alternative soundtrack fallback: slot=%s backend=%s",
            SoundtrackPack::slotName(musicType), settings.audio.musicType.c_str());
    usingCurrentPlayer = true;
    currentPlayer->setMusic(true, musicType == MUSIC_CREDITS ? MUSIC_MENU : musicType);
}

void AlternativeMusicPlayer::toggleSound() {
    setMusic(!musicOn, MUSIC_PEACE);
}

void AlternativeMusicPlayer::setMusic(bool value, MUSICTYPE musicType) {
    musicOn = value;
    if(value) changeMusic(musicType);
    else stop();
}

bool AlternativeMusicPlayer::isMusicPlaying() {
    return musicOn && (usingCurrentPlayer ? currentPlayer->isMusicPlaying()
                                        : music != nullptr && Mix_PlayingMusic() != 0);
}

void AlternativeMusicPlayer::setMusicVolume(int newVolume) {
    MusicPlayer::setMusicVolume(newVolume);
    currentPlayer->setMusicVolume(musicVolume);
    Mix_VolumeMusic(musicVolume);
}

std::unique_ptr<MusicPlayer> createMusicPlayer() {
    std::unique_ptr<MusicPlayer> player;
    if(settings.audio.musicType == "directory") player = std::make_unique<DirectoryPlayer>();
    else if(settings.audio.musicType == "adl") player = std::make_unique<ADLPlayer>();
    else if(settings.audio.musicType == "xmi") player = std::make_unique<XMIPlayer>();
    else throw std::runtime_error("Invalid music type: " + settings.audio.musicType);
    SDL_Log("Starting %s music player...", settings.audio.musicType.c_str());

    if(settings.audio.alternativeSoundtrack) {
        if(auto pack = SoundtrackPack::loadInstalled()) {
            SDL_Log("Using alternative soundtrack: %s by %s", pack->name.c_str(), pack->author.c_str());
            return std::make_unique<AlternativeMusicPlayer>(std::move(player), std::move(*pack));
        }
        SDL_Log("Alternative soundtrack is not installed; using current soundtrack");
    }
    return player;
}
