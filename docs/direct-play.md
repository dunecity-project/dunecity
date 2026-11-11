# Direct play (peer-to-peer crossplay)

Play Online now carries gameplay **directly between the players** over WebRTC data channels.
A server is still involved, but only to find a room, admit players and introduce them to each
other. No gameplay byte passes through it, and an established match keeps running if it goes away.

The relay transport (`RoomRelayClient`, `docs/room-relay-protocol.md`) is still in the tree for
older tests and releases. Nothing in the new path can select it: the direct transport refuses a
`ws://`/`wss://` address, refuses an address containing `/relay`, and ignores the gameplay URL an
admission answer offers.

## 1. Pieces

| File | What it is |
| --- | --- |
| `include/Network/RoomSessionTransport.h` | the interface `NetworkManager` talks to; `RoomRelayClient` and `DirectRoomTransport` both implement it |
| `include/Network/DirectRoomTransport.h`, `src/Network/DirectRoomTransport.cpp` | the session: membership, introductions, mesh readiness, liveness |
| `include/Network/P2PWireFraming.h` | the bytes on a data channel; header-only so tests drive the real codec |
| `include/Network/P2PSignalingProtocol.h` | what the signaling service may say, and how little of it is believed |
| `src/Network/DirectPeerConnectionLibdatachannel.cpp` | native backend (libdatachannel, pinned commit `443f6934d9007eb7076ab7825ba330f355fcbead`, v0.24.5) |
| `src/Network/DirectPeerConnectionEmscripten.cpp` | browser backend, through the bridge below |
| `platform/web/src/dune-direct-bridge.ts` | owns P2PKit's `RTCTransport`; bundled to `platform/web/p2p-direct.js` |
| `platform/web/package.json` | the pinned P2PKit SDK dependency, including hardened direct mode and framing |
| `tools/p2p-signaling/**` | the PHP signaling service |
| `tools/p2p-interop/**` | native-to-browser interoperability harness |

`GamePayloadRouter` and the whole `NetworkManager` receive path are shared with the other
transports. A direct session does not bring its own game parser, and the browser bridge never
sees anything but opaque hex.

## 1a. Relationship to upstream P2PKit

The browser uses the P2PKit revision pinned in `platform/web/package.json` through
its documented package exports. `platform/web/src/dune-direct-bridge.ts` selects
`direct: true` for hardened framing and STUN-only signaling. The old vendored
source tree has been removed; generic transport code comes from the dependency.
The separate browser **Find Match** flow uses the SDK's matchmaking service and
has a different trust model; see `platform/web/README.md` for configuration and deployment.

| Upstream concept | Here |
| --- | --- |
| `RTCTransport` (`transports/rtc.ts`) | the browser peer connection, with the documented local hardening |
| `SignallingChannel` (`signalling/types.ts`) | implemented by the bridge over `tools/p2p-signaling`, so `announce`/`description`/`iceCandidate` become HTTPS records instead of a WebSocket |
| `Chunker` (`framing/index.ts`) | the fragment format; `include/Network/P2PWireFraming.h` is the same format written for C++ |
| `Transport` events `connect`/`message`/`disconnect`/`error` | `DirectPeerConnection`'s state and queues |
| backends (`backends/index.ts`) | the browser passes `RTCPeerConnection`; natively the equivalent backend is libdatachannel |

The native side is therefore an **adapter**, not a reimplementation: it exists because
WebAssembly cannot hold an `RTCPeerConnection` and a desktop build has no browser to borrow one
from. Its job is to put exactly the same bytes on exactly the same kind of channel. The one place
it deliberately differs from a browser is documented in §3a.

The game's own command protocol sits unchanged **above** this transport. P2PKit carries opaque
values; `GamePayloadRouter` is still the only thing that understands them.

## 2. What goes over the channel

```
game packet bytes (ENetPacketOStream, unchanged)
  -> envelope string
    -> JSON value handed to RTCTransport.trySend() / P2PWire::splitIntoChunkPackets()
      -> chunk packets {"id","i","n","part"}, one per data-channel message
        -> DTLS/SCTP
```

Envelopes, all ASCII inside a JSON string:

| Envelope | Meaning |
| --- | --- |
| `g:<channel>:<recipient>:<hex>` | one game packet; `recipient` 0 means "everyone", and a message addressed to a third party is **dropped and the sender disconnected**, never forwarded |
| `d:<kind>:<hex>` | a bounded diagnostic; never a game packet |
| `r:<roster>:<connected>` | this peer's view of the room and of its own direct links (§4) |
| `p:<token>` / `q:<token>` | direct-path round-trip probe and reply (§5) |

Framing bounds, matching the hardened browser chunker exactly: fragment ≤ 16000 characters,
raw message ≤ 131072 bytes, reassembled value ≤ 1048576 bytes, `n` ∈ 1..128, `i` ∈ 0..n-1, group
ids 1..64 of `[A-Za-z0-9_-]`, ≤ 16 pending groups and ≤ 4 MiB pending per peer, 15 s group
lifetime. A game packet may be the full `RoomRelay::Limits::kMaxGamePayloadBytes` (262128).

**Nothing is ever silently dropped.** A malformed fragment, a contradictory one, an exhausted
bound or an expired group closes that peer's connection with a reason the player sees. Dropping a
message and continuing is how a lockstep match applies a prefix of a command stream and
desynchronises with no error anywhere.

## 3. Who you are talking to

HTTPS admission decides who may be in the room. The signaling service then binds the first DTLS
fingerprint it sees for each **pair** - `(room, epoch, from, to)`, because every peer connection
has its own certificate - and tells each side what it bound for the other. A description whose
fingerprint is not the attested one never reaches the RTC stack; one that arrives before its
attestation is held rather than applied. The RTC stack then verifies the certificate actually
presented against the fingerprint in the description it accepted, which is what ties the admitted
player to the encrypted channel.

STUN only. No TURN is ever configured, no TURN credential exists, and a candidate or description
mentioning a relayed candidate is refused on both sides. The transport is already encrypted; no
application-level crypto is added.

## 3a. The one native-only adjustment: `a=sctp-init`

Chromium offers the SCTP zero-RTT extension as `a=sctp-init:<base64>`. libdatachannel does not
implement that attribute, but it does reciprocate unknown application-level attributes into its
answer - so it advertises an extension it will not honour, the browser opens the data channel
accordingly, and the native side then rejects the first message ("Got unexpected message on
stream 1"). Both ends fail.

`P2PSignal::stripUnsupportedSctpInit()` removes those lines, whole-line and prefix-matched, before
the description reaches libdatachannel. Nothing else is touched: the fingerprint, the ICE
credentials and the DTLS role are exactly as sent, so certificate verification is unaffected. The
browser does not strip anything, because its stack supports the extension.

This was found by running the real native backend against a real browser, not by reading specs.
`tests/P2PSignalingProtocolTestCase` pins the behaviour, including CRLF, bare LF, a final line
with no separator, and the fact that an attribute merely *mentioning* the text is left alone.

## 4. The start barrier

A host having a channel to each guest does **not** mean the guests reached each other. Each peer
broadcasts `r:<roster>:<connected>`; `DirectRoomTransport::meshReady()` is true only when every
admitted peer is connected to us, reports the same roster, and lists every other admitted peer
among its own connections. Any membership change clears every report. The host's Start is gated
on it (`CustomGamePlayers`), and `meshBlockedReason()` is the sentence shown meanwhile.

## 5. Timing

Round trip is measured with `p`/`q` on the data channel, per peer. `roundTripTimeMs()` is the
worst connected peer's, with a 200 ms wide-area assumption before the first sample - never 0,
which would size the lockstep input buffer for a LAN. The HTTP command pacing that exists for the
polling relay is explicitly not applied to a direct session.

## 6. Signaling service

`tools/p2p-signaling` (PHP, no Node runtime, no SQLite extension required). It keeps the existing
admission and lobby-chat wire contract and adds `/v1/p2p/{session,poll,signal,phase,leave}`.
There is **no gameplay endpoint**. Secrets travel in the POST body (`grant`) or the
`X-Dune-Session` header, never in a URL or a log. Origins are an exact allowlist; grants are
single-use, expiring and bound to the app, version, protocol and content hash they were issued
with. The service makes no outbound request derived from user data, and SDP/ICE text only ever
reaches the RTC stack.

Client settings: `Network/Direct Endpoint` (default `https://dunelegacy.com/p2p`) and
`Network/Direct Development Endpoint` (loopback, only with the existing development opt-in).

## 6a. What ends a session

| Event | Before the match starts | After the match starts |
| --- | --- | --- |
| Signaling unreachable | **fatal** - the session exists on the admission authority | diagnostic; the match continues |
| 401/403/404, or an explicit `closed` | **fatal** | diagnostic; the roster is already frozen |
| `gone` for a peer whose channel is open | ignored | ignored |
| The host's channel is lost | session ends for every client | session ends for every client |
| Any frozen-roster player is lost | n/a | match ends for everyone, explicitly |
| A broadcast that one player did not accept | that link closes | that link closes and the match ends |

The roster freezes when the host starts the match or when this computer's own simulation starts,
whichever happens first. After that, a player the service introduces is refused rather than
admitted: lockstep has no way to bring a latecomer up to date.

Player addresses are visible to the other players in the room - that is what "direct" means, and
with no TURN it cannot be otherwise. It is a deliberate property of the requested topology, not
an oversight.

## 7. Failure is said out loud

A peer that will not connect produces "Could not open a direct connection to <name>. Your network
blocked the peer-to-peer link." There is no fallback to HTTP or WebSocket gameplay and no TURN
retry, because a silent fallback is how direct play quietly becomes relayed play.

Losing the signaling service after the players are connected is a diagnostic, not a disconnect:
polling stops and the match continues. A `gone` record for a peer whose channel is open is
ignored - the channel is the truth, the service only knows who stopped polling it.

### Joining and compatibility

The public directory offers only rooms matching the client's exact executable version and
game protocol, including **In progress / Spectate** entries. **All mods** permits discovering a
different mod revision; it does not waive executable compatibility. Invite codes and cached
listings are checked again by admission, with both versions shown on a mismatch. All players
must update together; a current browser cannot play with an older desktop or mobile build.

A background list refresh does not disable joining a selected cached room. The join cancels
that refresh and lets admission decide whether the room is still open.

Player names are unique within a room because they identify controllers in the game protocol.
If another player uses the same name (including the default **Player**), change **Player name**
in **Settings**, then rejoin. Rejection does not rename you or grant a second controller.

Finding a room, receiving admission and exchanging connection details do not establish a
working data channel. A failed direct connection is a separate transport failure, not proof of
a version mismatch or a blocked NAT. The STUN-only policy above remains unchanged; a directory
or name-guidance fix does not make every WAN peer pair reachable.


## 8. Building and testing

```bash
cmake -S . -B build -DDUNECITY_BUILD_TESTS=ON -DDUNECITY_DIRECT_P2P=ON
cmake --build build --target dunelegacy_tests
ctest --test-dir build --output-on-failure

# native/browser framing cross-check (no browser needed)
node --experimental-strip-types tools/p2p-interop/framing-vectors.mjs
```

`-DDUNECITY_DIRECT_P2P=OFF` builds without the native backend; the game then says it cannot open
direct connections rather than pretending to connect. `find_package(LibDataChannel CONFIG)` is
tried first, so a prebuilt pinned install can be supplied with `CMAKE_PREFIX_PATH`; otherwise
`FetchContent` clones the pinned commit (override with
`-DFETCHCONTENT_SOURCE_DIR_LIBDATACHANNEL=<checkout>`).

The live native-to-browser session harness is `tools/p2p-interop/README.md`.

## Match-start barrier

Before its countdown, the host asks the signaling service to atomically close admission with
an exact sorted peer-ID roster. A changed roster is refused. The successful transaction clears
unredeemed grants and returns a random start ID for that room epoch. The host then sends
`s:p:<id>:<roster>:` directly to every peer. Each verifies the same fully connected roster,
freezes it and replies `s:a:<id>:<roster>:`. Only after every acknowledgement does the host send
`s:c:<id>:<roster>:<STARTGAME hex>` and publish its local countdown event. Guests invoke the
shared game parser for one commit only. Duplicate prepare/commit messages cannot reset a
countdown; conflicts and a 30-second preparation timeout end the room. A failed fanout closes
all local links. This does not promise progress after a peer crashes or loses connectivity.

The browser's bounded synchronous queue reports acceptance truthfully; the first channel send
runs synchronously, and a thrown send returns false immediately. Backpressure preserves order
and later failure closes the connection. Queue acceptance is not a delivery acknowledgement.

Session requests include a stable per-grant recovery nonce. An identical retry within 45 seconds
can recover its already committed session, without creating another seat or joined event.
Normal teardown makes one best-effort bounded leave request (four concurrent requests maximum,
two-second deadline). The lobby also closes when its host expires. A signaling timeout never
changes the membership of an established match.

## Joining regression evidence (2026-11-11)

The discovery/name repairs are in `49f6ab537`. They are not a deployment.
The browser checks below used the **published 1.0.864 executable**, isolated browser
profiles and the fixed local PHP service. Gameplay remained direct. The selected
STUN configuration matched production: `stun:stun.l.google.com:19302` and
`stun:stun1.l.google.com:19302`.

| Guarantee | Enforcement / exercised evidence | Bound or limitation |
| --- | --- | --- |
| Different executable versions never gain a seat through discovery | `Rooms::listPublic`, authoritative `Signaling::directorySnapshot`, and the unchanged join/version checks. Real HTTP reproduced the old list-offers/join-refuses inconsistency; regression cases include an old/misleading index cache and running rooms. | Invite codes and stale listings remain subject to authoritative admission and payload compatibility checks. |
| A selected cached room remains joinable during a list refresh | `CrossplayMenu::joinPublicGame` cancels the directory request when beginning admission; `menu-probe.inc` exercises the enabled button with `directoryPending=true`. | The room can legitimately become full or close before admission. |
| A duplicate name cannot gain a second controller or pending request | Normal seating and late-join checks retain `name_taken`; a renamed retry can succeed. | The error now explains Settings; it never silently changes identity. |
| Owned Custom Game reaches lockstep with both humans' orders | Listing → both lobbies → Start → both vehicles moved. **31 shared-cycle byte-identical digests through cycle 6200 (99.2 simulation seconds)**; both direct links remained connected, no refused sends. | Browser↔browser, Vanilla Habbanya-Penny Revised; not a claim about every WAN pair. |
| A genuinely generated Random Skirmish reaches the same lockstep | 128×128, two mirrored Vanilla starts, Medium bases, 5000-credit armies, tech 9, seed 532178562. Listing → both lobbies → Start → both humans moved a vehicle. **41 matching digests through cycle 8200 (131.2 simulation seconds)**; both links connected, no refused sends. | Browser↔browser; actual generator output, not a fixed-map substitute. |

Sanitized JSON, listing/lobby/start/game screenshots and the encoded matching digests:
`outputs/multiplayer-join-20261111/controlled/{custom-stun,skirmish-stun}-gameplay.json`
under the managed project's outputs directory. The JSON identifies the executable and service
separately; the rebuilt cached-click client was not substituted for the published baseline.

The initial host-only-ICE fixture stalled before gameplay. A bare two-RTCPeerConnection
control reproduced that limitation without the game, SDK or PHP; production STUN then enabled
the real matches above. That fixture failure is **not** evidence for a production transport
patch. A third-party public-room connection also failed during the investigation, but its ICE
diagnostics were not retained; these successful owned-room checks do not explain or resolve it.
