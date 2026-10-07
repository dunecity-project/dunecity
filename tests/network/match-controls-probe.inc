// Opt-in extension of the real three-peer late-join fixture. File barriers only
// coordinate test actions; every control travels through the production network.
#include <GUI/dune/InGameSettingsMenu.h>
#include <GUI/dune/InGameMenu.h>
#include "match-controls-wire-probe.inc"
static bool probeMatchControls(Game& game, const std::string& role, const std::string& out) {
    if(!std::getenv("JOIN_MATCH_CONTROLS")) return false;
    static bool checkedWire=false;
    if(!checkedWire) {probeMatchControlWire(); checkedWire=true;}
    const bool host=role=="Host", viewer=role=="Newcomer";
    const auto exists=[&](const std::string& name){return std::filesystem::exists(out+"/control-"+name);};
    const auto write=[&](const std::string& name,const std::string& value="yes"){joinWrite(out+"/control-"+name,value);};
    const auto read=[&](const std::string& name){return joinRead(out+"/control-"+name);};
    const int stage=exists("done-2") ? 3 : exists("done-1") ? 2 : 1;
    const std::string tag=std::to_string(stage), mine=role+"-"+tag;
    const Uint32 cycle=game.getGameCycleCount();
    if(exists("done-3")) {
        if(host && cycle>=1000 && !exists("quick-close")) {
            game.onOptions();
            SDL_KeyboardEvent escape{}; escape.type=SDL_KEYDOWN; escape.keysym.sym=SDLK_ESCAPE;
            game.pInGameMenu->handleKeyPress(escape); game.pInGameMenu.reset();
            write("quick-close"); // Close before the lockstep pause command arrives.
        }
        if(host && cycle>=1200 && !exists("escape-requested")) {
            game.onOptions(); write("escape-requested");
        }
        if(host && exists("escape-requested") && !exists("escape-resumed") && game.isGamePaused()) {
            SDL_KeyboardEvent escape{}; escape.type=SDL_KEYDOWN; escape.keysym.sym=SDLK_ESCAPE;
            game.pInGameMenu->handleKeyPress(escape); game.pInGameMenu.reset();
            if(game.isGamePaused()) throw std::runtime_error("Escape did not resume the host menu pause");
            write("escape-resumed");
        }
        return false;
    }
    if((stage==2 && cycle<300) || (stage==3 && !viewer && cycle<700)) return false;
    if(stage==2 && !viewer && cycle==300) {
        write(role+"-ready");
        if(!exists("Host-ready") || !exists("Partner-ready")) return true;
    }
    if(stage==3 && !host && !viewer && !game.isGamePaused() && !exists(mine+"-menu")) {
        // A player who is not hosting keeps the match running for everyone else.
        game.onOptions();
        if(game.isPauseRequestPending() || game.isGamePaused())
            throw std::runtime_error("non-host Options paused the online game");
        game.resumeGame(); game.pInGameMenu.reset();
        write(mine+"-menu");
    }
    const bool shouldPress=!viewer && ((stage==1 && !host && cycle>=100)
        || (stage==2 && cycle>=300) || (stage==3 && host && cycle>=700));
    if(shouldPress && !exists(mine+"-requested")) {
        if(stage==3) {
            // The host entering Options is what pauses the match in this stage.
            game.onOptions();
            if(!game.isPauseRequestPending() && !game.isGamePaused())
                throw std::runtime_error("host Options did not request the shared pause");
            auto* menu=dynamic_cast<InGameMenu*>(game.pInGameMenu.get());
            menu->draw(Point(0,0));
            if(!game.isGamePaused() && (menu->pauseGameButton.isEnabled()
                || menu->onlineNotice.getText()!=_("Pausing...")))
                throw std::runtime_error("host menu did not show pending pause");
            // Leave this same window open until the network pause takes effect.
        } else {
            game.toggleMatchPause();
        }
        write(mine+"-requested");
    }
    if(game.isGamePaused()) {
        if(!exists(mine+"-paused")) {
            write(mine+"-paused",std::to_string(cycle));
            if(stage==3 && host) {
                auto* menu=dynamic_cast<InGameMenu*>(game.pInGameMenu.get());
                if(!menu) throw std::runtime_error("host menu was not kept open");
                menu->draw(Point(0,0));
                if(!menu->pauseGameButton.isEnabled() || menu->pauseGameButton.getText()!=_("Resume match")
                    || menu->onlineNotice.getText()!=_("Game paused"))
                    throw std::runtime_error("open host menu did not refresh after pause");
            }
            game.onOptions();
            // Opening the menu again on an already paused match must never resume it.
            if(!game.isGamePaused()) throw std::runtime_error("opening Options resumed the shared pause");
            joinCapture(game,out+"/controls-menu-"+mine+".bmp");
            dynamic_cast<InGameMenu*>(game.pInGameMenu.get())->onSettings();
            joinCapture(game,out+"/controls-settings-"+mine+".bmp");
            game.pInGameMenu->closeChildWindow();
            if(stage!=3 || !host) {
                game.resumeGame(); game.pInGameMenu.reset();
                if(!game.isGamePaused()) throw std::runtime_error("closing Options resumed a pre-existing pause");
            }
            InGameSettingsMenu options;
            if(options.gameSpeedPlus.isEnabled()!=host || options.gameSpeedMinus.isEnabled()!=host)
                throw std::runtime_error("speed settings authority differs from host role");
            if(!host && game.requestGameSpeed(32)) throw std::runtime_error("client changed shared speed");
            if(viewer) {
                if(game.canToggleMatchPause()) throw std::runtime_error("spectator has pause authority");
                game.toggleMatchPause();
                if(!game.isGamePaused()) throw std::runtime_error("spectator resumed the match");
            }
            joinCapture(game,out+"/controls-"+mine+".bmp");
        } else if(read(mine+"-paused")!=std::to_string(cycle)) {
            throw std::runtime_error("simulation advanced during explicit pause");
        }
        if(exists("Host-"+tag+"-paused") && exists("Partner-"+tag+"-paused")) {
            if(read("Host-"+tag+"-paused")!=read("Partner-"+tag+"-paused"))
                throw std::runtime_error("players paused at different cycles");
            if(stage==1) {
                if(host && !exists("speed-set")) {
                    // The fastest setting, so the real request/broadcast/receive path is
                    // checked at the boundary a peer on the previous protocol would refuse.
                    if(!game.requestGameSpeed(GAMESPEED_MIN)) throw std::runtime_error("host could not change paused speed");
                    if(game.requestGameSpeed(GAMESPEED_MIN-1)) throw std::runtime_error("host set a speed below the fastest setting");
                    if(game.getGameSpeed()!=GAMESPEED_MIN) throw std::runtime_error("refused speed changed the host's pacing");
                    write("speed-set");
                }
                if(!host && game.getGameSpeed()==GAMESPEED_MIN) write("speed-received");
                if(host && exists("speed-received")) {game.toggleMatchPause(); write("done-1");}
            } else if(stage==2 && !host) {
                game.toggleMatchPause(); write("done-2");
            } else if(stage==3 && host && exists("Newcomer-3-paused")) {
                if(read("Newcomer-3-paused")!=std::to_string(cycle))
                    throw std::runtime_error("late spectator did not inherit the paused cycle");
                dynamic_cast<InGameMenu*>(game.pInGameMenu.get())->onResume();
                game.pInGameMenu.reset();
                if(game.isGamePaused()) throw std::runtime_error("Back to Game did not resume host menu pause");
                write("done-3");
            }
        }
    }
    return false;
}
