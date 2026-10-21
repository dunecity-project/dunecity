#ifndef FRAMEPACING_H
#define FRAMEPACING_H

/// How many due game cycles one rendered frame runs (Game::runMainLoop, docs/game-performance.md
/// "Frame pacing"). The loop draws a frame, then runs the cycles the elapsed time owes at the
/// game speed, at most ten. On a large game ten cycles can cost far more than a frame: 8 houses on
/// a 384x384 map at 2 ms per cycle made 40-90 ms frames. A single-player frame therefore stops
/// catching up once its cycles have used kCatchUpBudgetMs; the remaining due time is carried to
/// the next frame (capped as before), so the game runs slower than its set speed while it cannot
/// keep up, and drawing and input stay at least ~1000/(16 + render) frames per second.
///
/// Only the number of cycles per rendered frame changes, never what a cycle does or which
/// commands it executes. Network games keep the ten-cycle cap alone: a peer that ran fewer
/// cycles per frame would hold every other peer back in lockstep, so there this is a no-op.
namespace FramePacing {

constexpr double kCatchUpBudgetMs = 16.0;

/// May the loop run one more due cycle in this frame? @a cyclesThisFrame cycles already ran,
/// taking @a catchUpMs of wall time since the frame's catch-up began. The first cycle always
/// runs, so a game whose single cycle exceeds the budget still advances every frame.
inline bool mayRunAnotherCycle(int cyclesThisFrame, double catchUpMs, bool networkGame) {
    return cyclesThisFrame == 0 || networkGame || catchUpMs < kCatchUpBudgetMs;
}

} // namespace FramePacing

#endif
