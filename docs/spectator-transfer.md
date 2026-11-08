# Spectator checkpoint compression

Spectators receive the existing full saved game, observer runtime, and capture cycle in a
lossless zlib stream. Save/loading semantics, command replay, promotion, and player admission
are unchanged. Fast compression (`Z_BEST_SPEED`) runs once per checkpoint per viewer; if the
stream plus its header is not smaller, the original bytes are sent. Progress reports count
actual wire bytes. Both compressed input and decoded output are capped at 8 MiB.

## Houseless observer view

Local observers and network spectators have `pLocalHouse == nullptr`. Their detached
`HumanPlayer` stores selection groups and chat identity only: it has no house, controller
slot, registry entry, or saved simulation state. The reserved observer controller ID still
rejects commands, including city orders without an acting object.

The interface uses the neutral palette and empty local economy gauges; inspecting an object
or browsing budget/statistics windows reads that object's or window's selected owner instead.
Fog, radar, selection, and positional audio use the observer view without changing simulation
visibility. Save/replay restoration recreates the houseless identity; promotion restores a real
local human and house. An AI-only campaign probe uses the same observer identity while native
victory rules retain the mission's authored objective house, not a local owner.


## Protocol compatibility

The existing authorized, reliable JOIN_SYNC/JOIN_ACK channel and spectator operations are used:

1. Host sends operation 10 with the wire length in `offset`. An empty data string means legacy
   raw bytes. Compressed data uses an eight-byte header: ASCII `DCZ1` followed by the decoded
   length as an unsigned little-endian 32-bit integer.
2. A receiver that understands `DCZ1` acknowledges operation 10 with offset **1**, empty data.
   The host releases its fallback copy and sends compressed operation-11 chunks. Chunk ACKs
   remain cumulative wire offsets, with the existing window, fairness and timeouts.
3. An older receiver ignores the extra header data and acknowledges offset **0**. Before any
   chunks are sent, the host sends a replacement operation-10 header with the original raw
   length and empty data, using the same epoch. After its zero ACK, the host sends raw chunks.
4. A new receiver also accepts an old host's empty header and acknowledges with zero.

Only the current epoch's pending header can be acknowledged. Unknown headers, invalid sizes,
corrupt or truncated zlib streams, decoded length mismatches, and trailing compressed bytes
are rejected. Inflation uses a fixed output allocation, so an advertised small output cannot
expand without bound. After decoding, the existing complete checkpoint parser and game-settings
validation still run before the viewer becomes ready. No protocol/version hash is bypassed.

## Verification

Run the normal native CTest suite plus `tests/wasm/run-network-wire-harness.sh wasm` with the
pinned Emscripten SDK active. The wire harness also runs natively under ASan/UBSan with `native`.
Tests cover exact binary restoration, incompressible fallback, both legacy negotiation paths,
8 MiB bounds, corruption, truncation, trailing data, and expansion past the advertised size.

`tests/network/run-late-join-probe.py --solo --mode spectate` drives actual local WebRTC peers
and compares their continuing simulation state. Use `--mode promote` for the separate spectator
to player transition. `--city --twin-cities` covers a 256x256 checkpoint. Optional `--host-binary`
or `--newcomer-binary` selects an existing old probe executable for mixed-version checks; build
it with the same map and compatible content. Use `--browser` for a browser viewer.

Level-9 campaign samples from 1.0.737 after ten simulated minutes were 701,911–714,592 raw bytes
across Atreides, Harkonnen and Ordos. This codec reduced them to 54,436–64,261 bytes (91–92%).
On the development Apple-silicon Mac, compression took approximately 2–4 ms and decompression
under 1 ms, with byte-identical results. These are checkpoint measurements, not full connection
times: admission, latency, game loading and catching up still add time. Older peers use raw
transfers. Browser asset downloads and player-promotion snapshots are separate paths.
