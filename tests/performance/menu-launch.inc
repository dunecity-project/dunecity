// Included only by tests/performance/game-perf.py --menu-launch (perf_regression), never by the shipped game.
//
// The launch path of a real player's game instead of the balance probe's direct Game setup:
// the main menu object as main() builds it, the Random Skirmish screen its button opens (all
// choices from the profile's [Random Skirmish], i.e. the player's last ones), that screen's Start
// handler (generate the map, remember the choices, startSinglePlayerGame) and so the interactive
// Game::runMainLoop with everything the app does per frame: the Workshop outbox pump, Discord,
// match analytics, music, input. An SDL timer posts SDL_QUIT after MENU_LAUNCH_SECONDS, which
// ends the match as closing the window does. The frame costs come from the AI decision log's
// performance windows (DUNECITY_AI_TELEMETRY=1); this file only prints the calibration loop.
#include <Game.h>
#include "game-perf.inc"
#include <Menu/MainMenu.h>
#include <Menu/RandomSkirmishMenu.h>

namespace MenuLaunch {
Uint32 quitMatch(Uint32, void*) {
    SDL_Event event{};
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
    return 0;
}
}

int runMenuLaunchProbe() {
    const double calibration = GamePerf::calibrationMs();
    const char* secondsText = std::getenv("MENU_LAUNCH_SECONDS");
    const Uint32 seconds = secondsText ? static_cast<Uint32>(std::strtoul(secondsText, nullptr, 10)) : 30;
    const auto started = GamePerf::Clock::now();
    {
        MainMenu mainMenu;
        RandomSkirmishMenu skirmish;
        const SDL_TimerID timer = SDL_AddTimer(seconds * 1000, MenuLaunch::quitMatch, nullptr);
        if (timer == 0) {
            SDL_Log("MENU_LAUNCH_ERROR: no SDL timer: %s", SDL_GetError());
            return MENU_QUIT_DEFAULT;
        }
        skirmish.onStart();   // the Start button's handler (-fno-access-control)
    }
    SDL_Log("MENU_LAUNCH_RESULT: {\"calibration_ms\": %.3f, \"wall_s\": %.1f}", calibration,
            GamePerf::msSince(started) / 1000.0);
    return MENU_QUIT_DEFAULT;
}
