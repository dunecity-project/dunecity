#ifndef ALTERNATIVEMUSICPLAYER_H
#define ALTERNATIVEMUSICPLAYER_H

#include <FileClasses/music/SoundtrackPack.h>

#include <memory>

class AlternativeMusicPlayer final : public MusicPlayer {
public:
    AlternativeMusicPlayer(std::unique_ptr<MusicPlayer> currentPlayer, SoundtrackPack pack);
    ~AlternativeMusicPlayer() override;

    void changeMusic(MUSICTYPE musicType) override;
    void toggleSound() override;
    void setMusic(bool value, MUSICTYPE musicType = MUSIC_RANDOM) override;
    bool isMusicPlaying() override;
    void setMusicVolume(int newVolume) override;

private:
    void stop();

    std::unique_ptr<MusicPlayer> currentPlayer;
    SoundtrackPack pack;
    Mix_Music* music = nullptr;
    bool usingCurrentPlayer = false;
};

// Also used when applying the on/off option: the original backend remains the fallback.
std::unique_ptr<MusicPlayer> createMusicPlayer();

#endif // ALTERNATIVEMUSICPLAYER_H
