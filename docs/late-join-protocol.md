# Joining a running online game

Version 1.0.726 adds direct-session live joining (game protocol 6). New custom
online games have “Allow hot join” checked by default; the map screen can opt out. Legacy hosts omit
`allowLateJoin`, which remains false on the service. ENet/LAN and the legacy
WebSocket relay do not implement this feature.

## Discovery and admission

`POST /v1/admission/list` with `details=1` returns eleven fields per game:
`code|players|max|mode|hexHost|contentHash|hexMod|hexMap|phase|elapsedSeconds|allowLateJoin`.
The old five/seven-field requests remain compatible. Elapsed time is wall time
since the first match start, including pauses. Running games are listed only
when the host is active, opted in, and has a transport seat available. Selecting
a row shows full metadata; compact rows and details explicitly say **In progress**
alongside elapsed time, or Waiting before start. The running-game action is **Spectate**.

A compatibility-checked `/v1/admission/request` creates a private request ticket,
not a room seat. `/v1/admission/request-status` polls it using the same claims;
`cancel=1` cancels the request. Polls use the polling rate allowance, not the
smaller room-creation allowance. Requests expire after inactivity or five minutes.
Only the authenticated host session can use `/v1/p2p/join-requests` to list,
approve, decline or abort. Approval creates a single-use grant bound to the
approved name and claims. General admission remains closed after match start.

From 1.0.727, ordinary joins and hot-join requests require an exact application
version match as well as protocol/content compatibility. A `version_mismatch`
refusal names both host and client versions; clients display an acknowledged,
wrapped popup and return to the lobby without granting a seat or notifying the
host. Older services' generic compatibility errors also open a popup.

## Synchronization

The host chooses an eligible living house and controller in Options → Join
requests. Existing humans cannot be replaced. An AI may be replaced; an extra
controller is allowed only in shared-house modes, up to two controllers per
house. Campaign co-op restricts assignment to the campaign's shared house.

The host saves an authoritative checkpoint and sends a host-only prepare packet
(21). Existing peers pause and acknowledge (22). Only then does the host open a
specific-name membership window and approve the service request. The original
peer identities, certificates and connections remain bound. A service poll alone
cannot reopen a running roster. Losing an original peer still ends the match.

Once the newcomer connects to every existing peer, the host transfers the same
serialized network-save settings to everyone over direct WebRTC channels. The
shared GamePayloadRouter parses these packets for all transports. Chunks are
at most 48 KiB, acknowledged cumulatively by every peer before the next chunk;
there is no unbounded send queue. The complete envelope is capped at 5 MiB and
the saved-game payload retains the existing 4 MiB network-save limit. Larger
saves fail visibly before pausing. No saved state passes through HTTP.

Every receiver validates the complete settings before acknowledging completion.
The existing roster-close/start prepare/ack/commit barrier commits the enlarged
mesh. All peers reload the same checkpoint and retain existing human state,
unit ownership, teams and house colors. Changed controllers keep their slot's
player ID. A new simulation epoch rejects packets from before synchronization;
future commands from the checkpoint are discarded consistently to avoid replaying
buffered input under changed ownership. No pathfinding/node budget is changed.

The host can cancel before the start barrier. Cancellation/timeout discards the
pending checkpoint, revokes the newcomer and resumes the original match. Peers
have a two-minute synchronization deadline. A failure after roster commitment
uses the existing fail-closed start behavior. Progress remains visible while
paused. Original game saves and their version are unchanged.

## Logging and verification

Public seating events retain their existing named activity log. Each resumed
roster commitment also records the updated public start roster with a fresh
start ID, without another public-lobby start notification or anonymous new-match
count. Server analytics settings remain independent of browser diagnostics.

Connection failures distinguish unfinished signaling, an explicit ICE failure after
both descriptions were exchanged, and a data channel that never opened. Only the
explicit ICE case suggests a blocked firewall/NAT route and states that no relay
fallback exists. A timeout or generic backend error is not proof that NAT is at fault.
`Direct connection failed` logs the phase, elapsed/deadline state, spectator roles,
STUN-server count, description exchange, and gathered/received host, server-reflexive
and peer-reflexive candidate types. Native `Direct ICE` lines and the browser console
also record ICE/gathering states and selected local/remote candidate **types** (or
`none` if no pair was selected). No candidate addresses, SDP, fingerprints or tokens
are logged. A selected pair can exist before the encrypted data channel opens.

`tools/p2p-signaling/test/test_late_join.py` exercises real HTTP admission,
name-bound grants, cancellation, authorization, compatibility and rate allowances.
`tests/network/run-late-join-probe.py` links a separate diagnostic main against
production game objects and runs three isolated native processes with real local
PHP/WebRTC. Modes cover AI replacement, human sharing, AI sharing and abort;
matching simulation digests are required after resumption. `--browser` provides
a local service/static origin for an actual browser newcomer. Nothing in the
probe changes production objects or uses the user's settings/saves.

## Passive spectators (1.0.729, protocol 8)

Selecting Join Game for a running public entry offers Request to play, Spectate,
or Cancel. Spectate is admitted automatically by the host. Reject join converts
a pending play request to observation. Neither path assigns a controller slot.
Exact application version/content and the host's Allow hot join opt-in remain
required. The room cap is eight connections, including spectators; co-op still
has two controllers. Protocol 7 retains its historical synchronized-observer
admission behavior on the service for older clients.

The service binds the spectator flag to the single-use, name-bound grant and
session. Protocol 8 session responses include `spectator=0/1`; member records
append a fifth `0/1` field. An observer's admission leaves phase, epoch and the
controller roster unchanged. Observers discover and connect only to the host;
other players never connect to them. Signaling authorization enforces that pair
restriction independently of the client. Host start-roster checks omit viewers.

Spectators are excluded from readiness, start acknowledgements, frozen player
rosters, command broadcasts, timing/backlog measurements and gameplay diagnostics.
A failed, congested or disconnected viewer channel is removed alone. A bounded
retired-ID bitmap prevents repeated viewer visits exhausting a match-ending
connection-attempt counter. Spectator input cannot exhaust the player event queue.
Their only accepted game payloads are bounded stream acknowledgements and chat.
Chat is forwarded by the host with the original display name; it is never a
simulation command. Active-player loss retains the normal fail-closed behavior.

After the host-to-viewer channel opens, the host captures an in-memory network
checkpoint without pausing or reloading any player. Only the viewer loads it.
The save remains capped at 4 MiB; the spectator envelope is capped at 8 MiB
on both endpoints, allowing the larger runtime supplement on 256x256 maps.
The supplementary network-only record (version 2) preserves path/target request
queues, unit movement caches, stuck detection, AI planning state, current path
budget, command-buffer size, live house AI flags and the exact city simulation
caches and phase state.
Observers retain the saved controllers and rebuild zone power draw without the
ordinary load-time city reconciliation, which would advance growth and effects
an extra time relative to the running host. Unit loading also retains references
to objects that appear later in the checkpoint; resolving them prematurely used
to clear carryall targets.
Ordinary disk-save format and its deliberate load-time resets are unchanged.

The host then streams the canonical command set and path budget for each tick
whose player inputs are complete. The spectator replays these ticks, sends no
commands or performance votes, and never runs ahead of received input. Camera
movement and full-map viewing are local. Periodic host state fingerprints check
catch-up integrity; divergence ends only that spectator view. AI simulation still
runs deterministically, while the host's command stream supplies its commands.

These messages use the shared GamePayloadRouter and authenticated packets 21/22:

| Operation | Direction | Meaning |
| --- | --- | --- |
| 10 | Host / viewer ACK | Begin checkpoint / acknowledge header |
| 11 | Host / viewer ACK | Checkpoint chunk / cumulative byte offset |
| 12 | Host | Ordered simulation tick, budget and optional fingerprint |
| 13 | Viewer | Checkpoint loaded; begin tick delivery |
| 14 | Viewer | Tick consumption frontier |
| 15 | Host | Attributed forwarded chat |

Every message carries the simulation epoch. Snapshot chunks and tick payloads are
at most 48 KiB. Snapshot delivery permits four unacknowledged chunks (192 KiB),
with cumulative ACKs accepted only at sent chunk boundaries or the exact end.
This avoids a full round trip per chunk without changing the receiver wire format.
Host catch-up history is bounded to 1,500 ticks and 4 MiB, with at
most 16 unconsumed ticks in flight per observer. Fair rotating delivery admits at
most eight messages and 64 KiB per update across all observers. Transfer/ACK
timeouts affect only the viewer; an idle, caught-up viewer does not time out merely
because the game is paused. A viewer beyond the bounded history must reconnect.
No player's simulation waits for an observer's acknowledgement or loading screen.

Actual player hot joining retains the synchronized checkpoint transaction above,
with spectators excluded from its barrier. When that transaction or a co-op
mission creates a new game state, the host restarts the spectator stream with a
fresh checkpoint. Viewers do not influence slot assignment, path budgets, fog,
exploration or any gameplay action.

The real-peer probe supports `spectate` and `reject_spectate`, a deliberate
five-second non-reading viewer, `--busy` for moving armies/AI production, and
`--stall` for a viewer that never finishes loading before its timeout. It checks
that original players advance during loading, compare at cycle 1,800, and remain
identical after the viewer leaves. Service tests cover unchanged match phase,
host-only viewer links, grants and old-protocol compatibility. Transport tests
cover unready/congested viewers independently of player readiness and broadcasts.

## Spectator-first entry (from 1.0.730, protocol 9)

Selecting **Spectate** for a running public game enters as a spectator directly,
without the older protocol-8 mode prompt. While connecting, the status explicitly
says it is connecting to the host to spectate, not receiving a pregame setup. In Options,
the viewer can Request to play or cancel a pending request. The authenticated
viewer session uses `/v1/p2p/join-requests` actions `request_play`, `cancel_play`
and `play_status`; no new admission ticket or second connection is created.
Both host and requester see a persistent flashing approval button on the game
screen while a play request is pending. The host clicks it to review, approve or
decline; it does not interrupt play with an automatic dialog. The requester can
click their notice for request options. Declining leaves a visible status and
permits a later request. Cancelling likewise preserves observation.
The protocol-8 admission behavior described above remains for older clients.

Approval selects an eligible controller slot and uses the existing checkpoint
transaction. A viewer requires both an authenticated service roster change and
the host's prepare packet before becoming a controller. It then discovers and
connects to all original controllers. Other viewers remain excluded from the
barrier. A declined request never grants gameplay authority.

Shared house (Multiple players per house) enables a second controller alongside
an existing human or AI, up to two controllers per house. The approval dialog
prefers a share slot where available and distinguishes keeping the existing
player from replacing/removing an AI. Choosing Replace still transfers that AI's
house entirely to the new human.

Start preparation fixes the expected roster but waits up to 30 seconds for the
local mesh's readiness reports before acknowledging. Peer channels have no
shared delivery ordering, so a host prepare may precede another controller's
readiness. Replayed preparation cannot extend this deadline; a conflicting
roster or premature commit is rejected. New or changed readiness reports elicit
an updated local report, recovering reports that arrived before a spectator's
authenticated role change. Identical reports do not produce reply loops.

Custom-map player sections may have gaps, such as Player1, Player2, Player3 and
Player5 in Ergsun-Odenkirk. Team initialization scans the same slot capacity as
house counting, assigning every occupied house a valid default team. Previously
the fourth house could retain -1, serialized as 255, causing the spectator
checkpoint validator to reject it. The map and checkpoint limits are unchanged;
checkpoint policy rejection now logs its reason on the host.

The `promote --city` probe exercises decline, retry, persistent notices on both
peers, opening the host dialog from the notice, default sharing, retaining the AI,
clearing the notice after promotion and matching resumed simulation state.
Set `JOIN_CAPTURE_UI=1` to save native rendered BMPs of both pending notices and
the approval dialog in the probe output directory. With `--browser`, set the browser player
name to Newcomer in Settings before joining. The original peers compare state
300 ticks after the promotion checkpoint, allowing time for manual browser
interaction. Create `browser-observed` only after verifying the browser view and
successful promotion; native digests alone do not prove browser success.

For the populated 256x256 regression, run
`python3 tests/network/run-late-join-probe.py --mode spectate --city --twin-cities --solo`.
Set `JOIN_AT_CYCLE=1400` to cover loading after production and carryall bookings;
`JOIN_TRACE=1` retains per-peer state summaries every 200 cycles for diagnosis.

For a battle in progress, use a reproducible seed and accelerated host warm-up:

```sh
JOIN_FAST_WARMUP=1 JOIN_CHECK_SPATIAL=1 JOIN_SEED=118705914 \
JOIN_AT_CYCLE=60147 JOIN_VERIFY_CYCLE=63000 \
python3 tests/network/run-late-join-probe.py --mode spectate --city --twin-cities --solo
```

Warm-up is restricted to a solo host and spectator-only modes. It runs without
frame delays until the requested checkpoint, then resumes normal cadence once
the spectator transfer begins. The viewer still deliberately stops reading for
five seconds. Spatial checks verify moving units remain indexed at their current
positions and that reversing cell insertion order leaves target selection
unchanged. The final comparison also checks that the host continues after the
viewer leaves. `JOIN_SEED` sets only the integration fixture's initial RNG seed.

Aircraft, carryall pickup adjustments and infantry movement must update the
spatial grid just as ordinary ground movement does. Checkpoint loads rebuild it
from active objects, excluding cargo and units inside repair yards. Cell queries
use object-ID order, so target ties do not depend on movement history. Spectator
loads also preserve negative targeting/path timer sentinels alongside their
restored work queues. These changes leave the save and spectator wire layouts
unchanged; both peers need the matching simulation build.

## 1.0.733: visible progress and bounded recovery

Joining now reports acknowledged/received snapshot bytes in the lobby and controller
synchronization dialog. After loading, a non-modal map banner shows replay progress
against the host's current cycle. Connecting and preparing stages do not invent a
percentage. Matching 733 clients are required by the existing exact-version gate.

Observer operation 16 (host-only JOIN_SYNC, empty payload, offset = current cycle)
reports that frontier at most four times per second. Operation 17 (viewer-only
JOIN_ACK, empty payload, current snapshot epoch) asks for a fresh checkpoint after a
fingerprint mismatch. Each snapshot has a distinct epoch, so queued old ACKs/ticks
cannot apply to the next one. The host permits two restarts per spectator connection,
including transfers that exceed retained history or time out. A viewer waits at most
30 seconds for a replacement header. No player is paused or reloaded for recovery.

Replay now sends batches within the existing eight-message/64 KiB update allowance,
with at most 64 unconsumed ticks per viewer. Snapshot, history and chunk byte caps are
unchanged. Permanent mismatch ends only the viewer and returns it to a usable lobby
with an acknowledged explanation; checkpoint-load exceptions use the same path.

Unit registration restores counts to each unit's original house, including temporary
Deviator ownership. Load constructors also stop adding the military value a second
time after it has already been restored from the save. Ordinary save bytes are unchanged.
The real-peer fixture supports JOIN_DEVIATED_UNIT, JOIN_DESYNC_ONCE and
JOIN_DESYNC_ALWAYS for regression, automatic recovery and bounded-failure checks.

Observer runtime version 3 also preserves both harvester path-failure counters.
Resetting these at checkpoint load changes refinery and carryall decisions even
when the initial object digest matches. Sandworm load constructors preserve the
saved attack mode instead of overwriting it with the new-unit ambush default.
The ordinary save format remains unchanged.

The populated four-house regression reproduces a late join after substantial
production and combat, including the five-second viewer stall:

```sh
JOIN_FAST_WARMUP=1 JOIN_SEED=118705914 JOIN_AT_CYCLE=42100 JOIN_VERIFY_CYCLE=45000 \
python3 tests/network/run-late-join-probe.py --solo --city --four-corners --mode spectate
```

Set JOIN_CHECKPOINT_TRACE=1 to retain checkpoint saves, runtime supplements,
per-object bytes and house/harvester counters for host/viewer comparisons.
JOIN_CAPTURE_UI=1 retains the rendered catch-up bar. The menu navigation probe
checks real download progress and failure recovery at three window sizes.
