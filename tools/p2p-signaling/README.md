# DuneCity P2P signaling service

PHP-only signaling for the direct peer-to-peer path. It replaces the Node room relay's role in
**discovery, admission, invitation, lobby chat and WebRTC introduction** — and nothing else.

**There is no gameplay endpoint here.** No WebSocket, no HTTP poll carriage, no TURN credential,
and no code path that makes an outbound request. Once every player is connected and the match has started, this
service can be switched off and their match continues. A lobby still requires admission authority.

```
tools/p2p-signaling/
  public/index.php     front controller; DocumentRoot (or Alias) target
  public/.htaccess     Apache 2.4 routing + request bounds, no modules beyond mod_rewrite
  src/                 Limits, Config, Http, Store, Rate, Sdp, Rooms, Signaling, Lobby, Analytics
  config/              config.sample.php — copy to config.php, outside the web root
  bin/router.php       router for `php -S`, used by the tests and local development
  test/                Python unittest suite driving a real PHP server over real HTTP
```

## Endpoints

Unchanged from relay v1, parsed by `RoomAdmission::parseAdmissionResponse`
(8192 bytes / 16 lines / 512 per line / 480 per value):

| Endpoint | Body | Answer |
| --- | --- | --- |
| `POST /v1/admission/host` | `app,appVersion,gameProtocol,contentHash,runtime,maxPeers,mode,visibility` | `room`, `grant`, `grantExpiresMs`, `maxPeers`, `url`, `signaling`, `visibility`, `control` |
| `POST /v1/admission/join` | `…,room,publicOnly` | same, without `control` |
| `POST /v1/admission/list` | `…,offset` | `next`, up to 12 × `game=<code>|<players>|<maxPeers>|<mode>|<hex host name>` |
| `POST /v1/admission/visibility` | `…,room,control,visibility` | `visibility`, `room` |
| `POST /v1/lobby/{enter,poll,say}` | `…,session,name,text,cursor` | `session`/`cursor`/`gap`/`chat=<id>|<hex name>|<hex text>` |
| `GET /v1/health` | — | `status`, `protocol` |

New, parsed by `P2PSignal::parse{Session,Poll}Response`:

| Endpoint | Auth | Body | Answer |
| --- | --- | --- | --- |
| `POST /v1/p2p/session` | single-use `grant` | `app,appVersion,gameProtocol,contentHash,runtime,grant,name,nonce` | `peer`, `session` (64 hex), `role`, `maxPeers`, `phase`, `room`, `ice=` (STUN only, repeatable) |
| `POST /v1/p2p/poll` | `X-Dune-Session` | `cursor` | `phase`, `peer=<id>|<role>|<hex name>|<hex runtime>`, `gone=<id>`, `fp=<from>|<to>|sha-256|<value>`, `sig=<from>|<seq>|<kind>|<hex>`, `cursor` |
| `POST /v1/p2p/signal` | `X-Dune-Session` | `to,kind=offer|answer|candidate,data=<hex>` | `status=ok` |
| `POST /v1/p2p/phase` | `X-Dune-Session`, host only | `phase=lobby|match,roster` (exact sorted roster required for match) | `status=ok`, `phase`, `startId`, `roster` |
| `POST /v1/p2p/leave` | `X-Dune-Session` | `bye=1` | `status=ok` |

A candidate's payload is `<sdpMid>|<candidate line>` — the media id both stacks need to place the
candidate, bounded separately from and far below a description: 64 + 1 + 2048 = 2113 total, the
same arithmetic as `P2PSignal::isAcceptableCandidatePayload`. An empty media id is accepted,
because that is what a browser reports for a bundled data-only description.

`fp=` names its recipient (`<to>`) explicitly. `P2PSignal::parsePollResponse` fails the **whole
snapshot** if an attestation is bound to a pair the poller is not part of — and likewise if the
roster repeats a peer id or a display name, names two hosts, says a player both joined and left,
or lists more than eight departures. Those are not degraded-feature bugs: any one of them means
every poll fails for everybody in the room. The server enforces each of them at the point where
the state is created, so `/v1/p2p/session` refuses a display name already in use in that room.

### `url=` and `signaling=`

The client does **not** take its endpoint from here: `CrossplayMenu` sets `signalingBaseUrl` from
`settings.network.activeDirectEndpoint()` and deliberately ignores `socketUrl`, so that a
compromised admission answer cannot move gameplay back onto a server. These fields are therefore
informational, and they are shaped so that nothing can be misread as a gameplay endpoint:

* `url=<public_base_url>/v1/p2p` — an HTTPS url ending in `/v1/p2p`, which is also what the
  legacy parser requires to be non-empty. Never `ws://` or `wss://`; there is no gameplay
  endpoint to name.
* `signaling=<public_base_url>` — the same service as a base url, the form
  `DirectRoomTransport::Config::signalingBaseUrl` expects. The legacy parser skips the key it
  does not know, so both go to both clients.

## Security properties

1. **Membership is the authorisation.** `to=` must be a current member of the same room in the
   same epoch. There is no cross-room addressing, no enumeration, and the service never moves
   bytes between two peers that are not both members of one room.
2. **Fingerprints bind per (room, from, to)** — per RTCPeerConnection, not per participant. Each
   peer connection normally carries its own certificate, so in a three-player mesh one player
   legitimately presents a different fingerprint on each of its links. What is fixed is the
   fingerprint on one *link*: once bound it can never change, not even across a phase change.
   Each poller is told only the attestations for links it is on.
3. **Data-only SDP.** Exactly one `m=application … webrtc-datachannel` section and exactly one
   `a=fingerprint:sha-256` with 32 colon-separated octets. An `m=audio`/`m=video` section is
   refused outright: this service will not help a peer ask another for a microphone.
4. **No relay.** `typ relay`, `turn:` and `turns:` are refused in a candidate, inside a
   description and in the configuration itself. Only `host`, `srflx` and `prflx` candidates pass.
5. **Grants** are CSPRNG, single-use, 30-second, phase-epoch-bound and bound to the exact
   `gameProtocol`/`contentHash`/`appVersion`/`runtime` they were issued with. A mismatched
   redemption burns the grant. A room that has ever started never admits another participant.
6. **Credentials never in a URL, a query string or a log.** The grant is a POST field, the
   session is the `X-Dune-Session` header, every response is `Cache-Control: no-store`, and the
   log records only an endpoint, a refusal code and a salted address tag — never an SDP, a
   candidate, a name, a chat message, an invitation code or a token.
7. **CORS** is an exact allowlist. Never `*`, never `null`, never `Allow-Credentials`, always
   `Vary: Origin`. A foreign origin gets a 403 and no header rather than a reflected one.
8. **HTTPS**, with plaintext tolerated only when `allow_plaintext_loopback` is on *and* the
   caller is on the loopback interface.
9. **`REMOTE_ADDR` only.** `X-Forwarded-For` is a caller-supplied string and is never read.
10. **State** lives in one private directory outside the web root, bounded in file size, record
    count, TTL, per-pair budget, per-room budget, per-address rate and global rate. There is no
    default state directory: an unconfigured or unsafe one is a 503, never a fallback into the
    web root or `/tmp`. Three properties are worth stating precisely, because an earlier draft of
    this service got each of them wrong:

    * **Stable lock files, atomic replacement.** Every state file `X.json` has a companion
      `X.lock` that is created once and never removed; all readers and writers take `flock` on
      that, and the data file is replaced by `rename()` from a fresh temp file underneath it.
      Locking the data file itself is wrong — once a writer replaces it, two workers hold locks
      on two different inodes and both believe they won — and truncating it in place can erase
      the grant and rate state authorisation depends on. Room locks come from a fixed pool of 256
      keyed by the room id's first byte, so a lock is never unlinked while somebody waits on it.
    * **Corrupt state fails closed.** A non-empty state file that is unreadable, oversized or not
      valid JSON is a 503. Treating it as empty would silently reset the room directory, the
      outstanding grants and the rate counters — exactly the state an attacker would want reset.
      Only a missing or zero-length file initialises. A short `fwrite` is a failure, not a
      success.
    * **There is no `O_NOFOLLOW` here**, because PHP's `fopen()` has no such flag, and this
      service does not claim one. The real model is two things together: the state directory is
      0700 and owned by the PHP worker, inside a non-world-writable parent whose path contains no
      symlink, so no other account can create a name in it; and every handle is verified *after*
      it is open — regular file, owned by us, not group- or world-accessible, and its (dev, ino)
      equal to what `lstat` reports for the name that was asked for. New files use `fopen(…,
      'xb')`, which fails rather than following an existing symlink. A `is_dir()` check before an
      `is_link()` check would accept a symlinked directory as a real one, so the link check comes
      first everywhere.

## Lobby announcements

After a host seats successfully, and after an authenticated host commits the match
phase, the optional trusted local `dunecityP2PNotifyLobby(kind, fields)` hook receives
mode, visibility, host display name, version, human player counts and human
player/spectator names plus an opaque log ID. `hot_joined` events include the joining
name, role and room-local participant ID: spectator admission emits once when the
session seats; controller admission/promotion emits only when the host resumes the
match with that participant still present. Requests, approvals and aborted controller
transfers do not announce a player join. No AI names are available to this service.
It receives no invitation code, grant, control/session token, SDP or address.
Grant recovery and repeated match-phase requests do not emit another event. Hook
failure cannot undo admission/start. The core service still makes no outbound
requests; the website deployment owns Discord configuration, queuing and delivery.
Both public and private lobbies are announced, with private invitations omitted.
No map or mod is claimed in this announcement hook. New hosts may provide a mod
identifier for directory filtering; maps remain between game peers.

## Analytics

Truthful, session-only lifecycle events, appended as bounded JSONL to `analytics.jsonl` in the
state directory: `created`, `joined`, `started`, `left`, `closed` at `schema_version: 3` with
`transport: "direct-p2p"` and the additive `runtime_claimed` / `peers_admitted` fields. The relay
schema (`tools/room-relay/src/analytics.js`, version 2) is extended, not replaced, and the
browser/native runtime classification is preserved as what it is — a client's claim about itself.

Nothing here may be described as an observed move, outcome, score or duration: this service never
sees a game packet, because there is no endpoint that would take one.

The production entrypoint provides a trusted local `dunecityP2PRecordEvent` hook which uses the
existing metaserver PHP/Python SQLite writer. Schema 3 adds `direct-p2p` and labels its source
`signaling_service_v1`; schema 1/2 rows retain their meanings. The private JSONL journal remains
available if analytics storage fails. A storage failure never vetoes a committed admission.
There is no outbound HTTP call and no gameplay data in this hook.

### Public activity and waiting players (1.0.725)

The separate trusted local `dunecityP2PRecordPublicActivity(event)` hook records
`chat_message`, `public_game_created`, `public_game_joined`, and
`public_game_started`. It runs only when server analytics are enabled and a hook
is installed. This is independent of the client Diagnostic logs preference.
Accepted chat binds text to the authenticated session display name; committed
public seating binds the actor to a peer. Match start records the authoritative
admitted roster (names, peer IDs, roles, claimed native/browser runtime). Private
rooms are excluded. Game event IDs deduplicate retries; only newly accepted chat
messages create chat records. The event allowlist omits codes, tokens, addresses,
SDP and ICE. Hook failures do not veto chat, joining or starting, but named activity
has no retry journal; a storage outage can leave gaps. No old chat is backfilled.
The website stores this stream in `analytics_public_activity`, separate from the
anonymous lifecycle journal, whose existing privacy contract stays unchanged.

Directory requests with `allMods=1` include all content hashes for the same game
protocol and append `contentHash|hex-mod-name` to each game row. Legacy requests
retain five fields and content matching. Admission always checks content matching.
New hosting requests optionally send `mod=<hex identifier>`; old rooms may have no
mod identifier. The client only switches to an installed, fingerprint-matching mod.

Lobby polls may opt into `presence=1`: `online=<count>` and up to twelve
`waiting=<hex name>` lines reflect sessions active within twenty seconds across
same-protocol mod channels. Poll/enter/send update activity; no presence events are
written to analytics. Chat names are display identities, not verified accounts.
Legacy polls keep their original response shape. Run `test/test_public_activity.py`
in addition to `test/test_signaling.py` for these behaviors.

## Deployment

1. `public/` is the DocumentRoot (or an `Alias /p2p`). `src/`, `config/`, `bin/` and `test/` must
   not be reachable over HTTP.
2. `cp config/config.sample.php config/config.php` and edit. Alternatively point
   `DUNECITY_P2P_CONFIG` at a configuration file anywhere outside the web root.
3. Create the *parent* of `state_dir` as the ordinary deploy account — **no administrator and no
   `chown` to `www-data` is needed**:

   ```sh
   mkdir -p /home/dunelegacy/private
   chgrp www-data /home/dunelegacy/private     # a group the deploy user is already in
   chmod 2770     /home/dunelegacy/private     # setgid, not world-writable, not world-readable
   ```

   Point `state_dir` at a child of it (`/home/dunelegacy/private/p2p-state`) and leave the child
   alone: the PHP worker creates it on first request, 0700 and owned by itself. The parent path
   must be canonical — no symlinked ancestor — and must not be world-writable, or the service
   refuses to start with a reason in the error log.
4. Nothing else. No daemon, no systemd unit, no database, no PDO, no cron. Expired rooms, grants,
   sessions and chat are swept opportunistically by the requests that touch them.

The existing legacy Node relay and its Apache endpoints are untouched: this service shares no
file, no port and no state directory with them, so games already in progress are unaffected.

## Tests

```
PHP_BIN=/path/to/php8.3 python3 tools/p2p-signaling/test/test_signaling.py
```

164 tests against a real PHP server over real HTTP, with `PHP_CLI_SERVER_WORKERS=4` so the
concurrency tests cross process boundaries the way Apache does — the tests that depend on that
assert the server really forked, rather than quietly proving nothing on a single-process build.
They cover hosting, public and private joining, grant replay and compatibility binding, names and
chat and rate bounds, cross-room spoofing, host-only phase and visibility, relay candidates,
oversize bodies, SDP abuse, per-pair fingerprints with three players, expiry and directory
cleanup, strict methods, exact CORS, and — in `StoreIntegrityTests` — corrupt and oversized state
failing closed without resetting admission or rate counters, symlinked subdirectories and
journals being refused rather than followed, replacement by rename rather than in-place
truncation, a failed write preserving the previous state, stable private lock files, and the
no-administrator directory bootstrap. `RealCandidateWireTests` uses candidate payloads in the
exact shapes Chromium and libdatachannel emit (mDNS host, IPv6, srflx with `raddr`/`rport`, TCP
with `tcptype`, the `generation`/`ufrag`/`network-id`/`network-cost` tail), and
`ParserTranscriptionTests` drives a three-player session through a transcription of
`P2PSignal::parse{Session,Poll}Response`.

**That transcription is not the real parser** — there is no C++ compiler in the environment this
was written in, so the header could not be compiled against these responses. It is a line-by-line
reading of the contract, and it can drift from the header it was copied from. Real interoperation
is what `tools/p2p-session-smoke` will establish; nothing here should be read as having
demonstrated it.

### Admission commit boundary

The room JSON is authoritative for grants, reservations, seated peers, phase, invitation and
certificate bindings. Redemption evaluates all refusal conditions and either consumes the grant
without a seat, or consumes it and seats its holder in one atomic rename under the room lock.
A failed write leaves the previous grant and seat state intact. The index reserves codes and
helps locate rooms; it cannot authorize a join. Public listing reads current room policy and
occupancy, so a stale or interrupted cache update cannot publish a room made private or consume
capacity. Reaping rechecks expiry under the same lock used for deletion.

## Workshop maps and mods

The same deployment now provides independent content distribution at `/v1/content/*`.
A player can browse, download and share without joining a game. These routes distribute
immutable authored files; they do not transport simulation packets or running-game checkpoints.
HTTPS, the origin allowlist and POST-only rules apply. Content errors use
`status=error`, `code=<fixed-token>` and **`message=<hex-encoded UTF-8>`**. Existing admission
and signaling envelopes remain unchanged.

**Game clients from 2026-10-18 on** use only the transfer routes: an online host of an
authored mod uploads it (`begin`/`chunk`/`commit`, `source=host`, `promoted=0`) and joiners
download pinned revisions (`manifest`/`blob`). They no longer call `list` or `lookup`, never
send `collect=1`, never upload maps, and have no background upload queue. The catalogue and
collection sections below describe what the service still accepts from older clients.

Every manifest has this exact ASCII representation, including its final newline:

```text
DUNEWORKSHOP1
kind=map
id=0123456789abcdef0123456789abcdef
name=5368617265642064756e6573
base=
mod=
file=<64-lowercase-hex-sha256>,<decimal-byte-length>,6d61702e696e69
```

- `kind` is `map` or `mod`; `id` is a permanent 32-lowercase-hex item identity.
- `name` is a hex-encoded UTF-8 display name (1–128 bytes). Same names do not imply same item.
- `base` is a hex-encoded portable mod folder name (up to 64 bytes) or empty. It identifies
  engine heritage, not a floating downloadable dependency. Mods contain their complete files.
- `mod` is an exact committed mod manifest SHA-256 for a map, or empty; mods require empty.
- `file` lines repeat in raw path byte order. Paths are hex-encoded portable ASCII relative
  paths, up to 240 bytes; traversal, reserved device names, case collisions and file/directory
  conflicts are rejected. Directory spellings must use consistent case.
- A map contains exactly `map.ini`, up to 1 MiB. A mod must include `mod.ini` and has up to 4,096 files, at most 128 MiB
  each and 2 GiB total. Manifest bytes are limited to **255 KiB** so a hex-encoded manifest
  plus response envelope fits the client's 512 KiB HTTP response budget.
- The revision identity is SHA-256 of the exact raw manifest. A display version is allocated
  separately at commit, starting at 1 and increasing for each new revision of that item.
  Deduplicating the same manifest never increments it; file blobs deduplicate across revisions.

All fields below are URL-encoded form fields. Success replies begin `status=ok`.

| POST route | Fields | Success fields |
| --- | --- | --- |
| `/v1/content/list` | Optional `kind=map\|mod`, `cursor=0` | Repeated `item=kind,id,version,hash,namehex,basehex,modhash`; `next=<offset>` or `0` at end |
| `/v1/content/lookup` | `file=<map-sha256>`, `mod=<exact-mod-revision>` | `found=0`, or `found=1` with existing `hash`, `version`, `name` (hex), `id` |
| `/v1/content/begin` | `manifest=<hex>`, `hash=<sha256>`, `owner=<64hex>`; optional `source=host\|manual`, `promoted=0\|1` | `upload=<64hex>`; if already published, `version=<number>`, `hash=<sha256>` instead |
| `/v1/content/chunk` | `upload`, `file=<sha256>`, `offset=<bytes>`, `data=<hex>` (at most 64 KiB decoded) | `next=<contiguous-byte-count>` |
| `/v1/content/commit` | `upload` | `version=<number>`, `hash=<sha256>` |
| `/v1/content/manifest` | `hash=<manifest-sha256>` | `version=<number>`, `manifest=<hex>` |
| `/v1/content/blob` | `hash=<manifest-sha256>`, `file=<file-sha256>`, `offset`, optional `count` (1–65536) | `data=<hex>`; shorter at EOF |

The catalog lists every committed revision, 50 per page, in publication order. Empty `next=0`
terminates pagination. Metadata (`source` and `promoted`) defaults to manual/1 and does not alter
immutable file identity. Chunk offsets are contiguous; an exact replay is accepted, differing or
overlapping data is refused. Empty files need no chunk. A commit is idempotent for the lifetime
of its upload receipt; after expiry, repeat `begin` to recover the existing version by hash.

The client generates and persists an unpredictable owner capability, independent of player name.
Only its hash is retained on the server. The first successful commit binds the item's owner;
changed revisions require the same capability. Concurrent first publishers recheck ownership
under the commit lock. To edit another person's item, save a copy with a new item ID and owner.
Anyone can identify an already committed exact manifest without changing ownership.

Storage is under `state_dir/content`, outside the webroot, with private directories/files.
A stable lock protects owner checks, quota accounting and monotonic revision allocation. The
server verifies every complete file hash before publishing the manifest; reads require membership
in a committed manifest. It rejects symlinked payload files. Blobs and metadata are published by
atomic replacement, and incomplete snapshots never appear in the catalog.

`content_quota_bytes` defaults to 20 GiB. Quota accounting includes immutable blobs and retained
upload reservations; immutable physical sizes are reconciled before new reservations so interrupted
commits cannot evade accounting. Bounds also include 10,000 revisions, 65,536 blob files, 256 upload
receipts and 8 active uploads per address. Uploads/receipts expire after 24 hours and are reclaimed
on subsequent content requests. Expiry never removes a committed revision. Keep regular backups
of the entire content directory, including `index.json` (ownership and numbering). There is no
public deletion endpoint; operators manage retention explicitly rather than breaking pinned saves.

Content has separate per-minute ingress allowances (4,096 per address, 16,384 service-wide), begin
allowances (32/256) and commit allowances (64/256). HTTP 429 means retry after the current minute.
These do not consume gameplay signaling allowances. The webserver/PHP request-body limit must
permit at least 512 KiB URL-encoded requests (`post_max_size=1M` or more).

`POST /v1/admission/inspect` accepts the normal compatibility claims plus `room`. Possession of
the current invitation code permits discovery of `room`, `contentHash` and `running=0|1`, with
normal game-version/protocol checks, before the client has downloaded that content. It returns
no grant and reserves no seat. Actual join/admission still checks the exact content hash.

Run the content tests alongside the existing service suites:

```sh
python3 tools/p2p-signaling/test/test_content.py
```

These exercise real concurrent PHP workers: hashes, chunk replay, partial uploads, unsafe paths,
case collisions, immutable versions, owner races, dependency closure, blob membership, large
manifest bounds, pagination, quota expiry, symlink refusal, and private-code inspection.

For a native client/server wire smoke test after configuring the CMake build, run:

```sh
python3 tools/p2p-signaling/test/test_client_smoke.py
```

It compiles the production Workshop store/client, SHA-256 implementation and bounded curl
transport into a small harness, starts the real PHP service, and uses two temporary profiles.
The harness verifies resumed uploads, duplicate large atlas blobs, exact mod dependencies,
map revision history, corrupt-cache repair, and owner rejection. No game window opens and no
normal user profile is touched. `--binary` runs a previously compiled harness; `--build-dir`
selects another configured native build. C++17, pkg-config, SDL2, curl and PHP are required.

### Metaserver map catalogue

`POST /v1/content/list` with `catalogue=maps` returns the newest revision of each
map lineage, including automatic host uploads, deduplicated by map bytes and
building category and exact gameplay dependency. Optional filters are `mod` (empty means all), `size=WIDTHxHEIGHT`,
and `players=1..12`; `cursor` follows the existing 50-row pagination. Catalogue
`item` rows append four fields to the legacy seven: hex-encoded mod identifier,
width, height, and maximum player slots. The legacy list protocol is unchanged.

The service retains `content/maps/<revision>.ini` as a hard link to its verified
immutable blob, with `<revision>.json` containing the display name, version,
mod/dependency, dimensions, player slots and hashes. These private files survive
service deployments and can be reviewed for a later release. Existing revisions
are indexed lazily. Content quotas count the blob once. Map display names never
need a version suffix; the server-assigned version remains metadata.

Map categories are derived from actual `[STRUCTURES]` entries: Vanilla by default,
Tornie for its exclusive buildings, and DuneCity for city buildings (including roads).
City content takes precedence in mixed maps. City/ Cities-named starter maps with at most max(4, twice the player count)
functional buildings also count as DuneCity; walls and slabs do not count toward
that limit. Existing tags and unit types do not determine the category. Schema-4
metadata refreshes older cached labels.
Editor saves write the derived `[BASIC] Mod` flag. The pinned mod dependency remains
separate and authoritative for exact gameplay; reclassification never changes it. `[BASIC] MapVersion` describes
an authored map revision; `[BASIC] Version` remains the legacy file-format number.
The downloaded `.workshop.ini` sidecar records the server revision and mod.

The catalogue was seeded by `scripts/seed-metaserver-maps.py` in the game repository;
that script was removed with the in-game catalogue on 2026-10-18.

### Offline custom map collection (1.0.771)

After a new offline custom match loads successfully, the client queues its original
scenario and exact selected mod revision. Replays, saves, campaigns and cancelled
setup do not contribute. Transfers run asynchronously in menus and offline games;
network failures leave the persistent outbox entry for retry.

Deploy the service before distributing 1.0.771: the new `lookup` route must be
present in both the HTTP router and Apache rewrite rules. Older services reject
lookup and the client retains the queued map until the service is upgraded.

Lookup matches map-file SHA-256 plus mod revision, independent of local publisher
identity. Legacy records are checked using their immutable manifests when derived
metadata is absent. Automatic map uploads additionally send `collect=1` to
`begin`; both begin and commit recheck under the storage lock and can return
`collected=<existing-revision>` instead of creating a duplicate. Manual editor
publication retains its existing ownership and version behavior. A collection
receipt never grants ownership or invents a server version for the local copy.
