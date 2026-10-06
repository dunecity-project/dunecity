# Game speed extension in 1.0.809

The speed controls now include 3 ms and 2 ms per simulation cycle. Existing
choices stay valid and the default remains 16 ms. No user profile reset is
performed. The slow end remains 32 ms; the existing bars and keyboard controls
use the same bounds.

| Setting | Nominal cycles per second | Relative to the previous fastest |
| --- | ---: | ---: |
| Default, 16 ms | 62.5 | 0.25x |
| Previous fastest, 4 ms | 250 | 1x |
| New, 3 ms | 333.33 | 1.33x |
| New fastest, 2 ms | 500 | 2x |

Tick-based gameplay still converts through `GAMESPEED_DEFAULT=16`. Only
wall-clock pacing changes. The existing 24 ms minimum frame-debt clamp and
ten-cycle frame guard remain. At 60 FPS the 2 ms setting needs approximately
8-9 cycles per frame, within that guard. Slower rendering or expensive
simulation work can prevent a match reaching its configured rate; this is
not an FPS improvement or a guarantee for large CPU-bound games.

## Compatibility and authority

Protocol 56 excludes older peers that reject speed values below 4 ms in a
lobby snapshot or MATCH_CONTROL packet. Save format 9852, observer runtime 7
and QuantBot policy `whole-army-base-scramble-v90` are unchanged. Hosts retain
shared-speed authority. Rejected requests do not alter the selected speed.
The host's choice persists; joining peers use the shared value without
replacing their personal offline-speed preference.

The relay's unchanged 70-cycle maximum buffer covers only 140 ms at 2 ms,
so sufficient CPU alone does not guarantee 500 cycles/s over a slow relay.
The policy already capped faster speeds before this change. No receiver
window or network scheduling changes were added.

## Independent checks

Native coverage admits all settable speeds, rejects out-of-range values,
checks default initialization and round-trips 2/3/4/16 ms through the existing
GameInitSettings serialization. Buffer conversion covers rounding and caps at
the new speeds. The production MATCH_CONTROL decoder accepts 2/3 ms and
retains wrong-role, nonce and malformed-packet rejection.

The existing three-peer late-join fixture used a real local signaling service
and production network paths: the host set 2 ms while paused, its partner
received it, a late spectator inherited the shared state, and all three
matched at cycle 1800. Pause/resume and client/spectator authority checks pass.

A private diagnostic compiled the actual Game::runMainLoop and rendered with
SDL/Metal on claw.local. It warmed interface setup before the timed run. The
remote display did not block at requested VSync, so a private presentation
hook capped frames at 60 Hz; simulation, input and catch-up logic were retained.
The lightweight Sihaya-Ferryman opening used two Brutal QuantBot houses and an
isolated passive human observer. It measured:

| Tick duration | Measured cycles/s | Maximum cycles per frame |
| --- | ---: | ---: |
| 16 ms | 62.428 | 2 |
| 4 ms | 249.897 | 5 |
| 3 ms | 333.235 | 7 |
| 2 ms | 499.787 | 10 |

The new/old fastest ratio was 1.999972. These are rendered Mac-mini measurements
at controlled 60 Hz, not MBA FPS or whole-match throughput. The presentation
hook is outside the repository and absent from the shipped binary. An initial
unwarmed sample included interface setup; it is retained as diagnostic evidence
and excluded from the steady-state rate results. A duplicate source-parsing
pacing model from the bounded Claude worker was removed in final review.

All 56 CTest targets pass (741.22 seconds). Pre/post Ninja dependency audits,
version consistency and diff checks pass. The portable bundle passes deep/strict
signature, all 36 ARM64 Mach-O architecture/load-path checks, and bundled SDL
initialization/hidden rendering. Native/portable code and string sections match.
Executable SHA-256: `40e8f36f1531d8521d4e57fcf3dc7fb98094336887a04f1e65ad0b9dbb3ad76f`.

MBA installation remains pending: its last verified app was 1.0.808, but SSH
became unreachable before upload. The existing app has not been replaced.
The complete archive, hashes and installation/rollback verifier are retained.
Only claim installation after fresh version/hash/signature/profile checks.

Evidence: `../outputs/game-speed-809-20261006/` (worker output, build/audits,
rendered-speed-results.json, network-controls.log, full-ctest.log, bundle-audit.json,
code-equivalence.json and installation receipts).
