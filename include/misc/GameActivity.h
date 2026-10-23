#ifndef GAMEACTIVITY_H
#define GAMEACTIVITY_H

namespace GameActivity {

// Held while a match runs (Game::runMainLoop). On macOS it declares user-initiated,
// latency-critical work (NSProcessInfo activity), so a game window that is covered by
// other windows or not frontmost is neither App-Napped nor slowed down while the match
// keeps running; idle system sleep stays allowed. No effect on other platforms and no
// effect on the simulation.
class Scope {
public:
    Scope();
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    void* token = nullptr;
};

#ifndef __APPLE__
inline Scope::Scope() = default;
inline Scope::~Scope() = default;
#endif

} // namespace GameActivity

#endif // GAMEACTIVITY_H
