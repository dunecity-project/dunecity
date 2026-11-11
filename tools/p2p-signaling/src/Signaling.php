<?php
declare(strict_types=1);
require_once __DIR__.'/LateJoin.php';

/**
 * Introducing two admitted players to each other, and nothing else.
 *
 * There is no gameplay endpoint here and no byte of game traffic ever passes through this
 * service. It holds a room's membership, carries offers, answers and trickled candidates between
 * two members of that room, attests which DTLS certificate it admitted for each direction of each
 * pair, and reports the room phase. Once the data channels are open the players do not need it,
 * and a match survives it going away entirely.
 *
 * The authorisation rule is short: **membership is the authorisation**. `to=` must be a current
 * member of the same room, in the same epoch, as the caller. A peer can never address, enumerate
 * or reach anything outside its own room, and the service never moves bytes between two peers
 * that are not both members of one room.
 *
 * Fingerprints are bound per (room, from, to) - per RTCPeerConnection - and not per participant.
 * Each peer connection a browser or libdatachannel opens normally carries its own certificate, so
 * in a three-player mesh one participant legitimately presents two different fingerprints. What
 * must not change is the fingerprint on one *link*: once a pair's first description is seen, a
 * later description with a different fingerprint is refused, which is what stops somebody else
 * taking over an admitted player's seat.
 */
final class Signaling
{
    use LateJoinSignaling;
    public function __construct(private readonly Store $store, private readonly Config $config)
    {
    }

    private static function file(string $roomId): string
    {
        if (!Store::isRoomId($roomId)) {
            throw new ServiceError(403, 'forbidden', 'That session is no longer valid.');
        }
        return 'rooms/' . $roomId . '.json';
    }

    /**
     * Creates the authoritative room file and the single-use grant that admits its host.
     *
     * The room file is written before the directory entry is finished, because the room file is
     * the room: an interrupted creation leaves a reserved code with no room behind it, which the
     * reaper takes back, rather than a directory that believes in a room nobody can enter.
     *
     * @return array{grant:string,control:string,logId:string}
     */
    public function createRoom(string $roomId, string $code, array $spec): array
    {
        $now = $this->store->now();
        $grant   = $roomId . Store::randomHex(28);
        $control = $roomId . Store::randomHex(28);
        $logId   = Store::randomHex(16);

        return $this->store->withLock(self::file($roomId), function (array $state) use (
            $roomId, $code, $spec, $now, $grant, $control, $logId
        ): array {
            if ($state !== []) {
                // The room id came from a fresh reservation; an existing file means the id was
                // reused, which it must never be.
                throw new ServiceError(503, 'capacity', 'Could not allocate a room. Try again shortly.');
            }
            $state = [
                'id'           => $roomId,
                'logId'        => $logId,
                'code'         => $code,
                'control'      => hash('sha256', $control),
                'visibility'   => $spec['visibility'] === 'public' ? 'public' : 'private',
                'mode'         => $spec['mode'],
                'modName'      => $spec['modName'] ?? '',
                'mapName'      => $spec['mapName'] ?? '',
                'allowLateJoin'=> $spec['allowLateJoin'] ?? false,
                'startedAt'    => 0,
                'joinRequests' => [],
                'maxPeers'     => (int)$spec['maxPeers'],
                'gameProtocol' => (int)$spec['gameProtocol'],
                'contentHash'  => (string)$spec['contentHash'],
                'appVersion'   => (string)$spec['appVersion'],
                'phase'        => 'lobby',
                'epoch'        => 0,
                'everStarted'  => false,
                'hostPeerId'   => 0,
                'hostName'     => '',
                'nextPeer'     => 1,
                'seq'          => 0,
                'bytes'        => 0,
                'createdAt'    => $now,
                'lastSeen'     => $now,
                'indexTouched' => $now,
                'grants'       => [],
                'peers'        => [],
                'signals'      => [],
                'gone'         => [],
                'fp'           => [],
                'pair'         => [],
            ];
            $state = self::issueGrant($state, $grant, 'host', $spec, $now);
            return [$state, ['grant' => $grant, 'control' => $control, 'logId' => $logId,
                             'code' => $code, 'maxPeers' => (int)$state['maxPeers'],
                             'visibility' => (string)$state['visibility'],
                             'peers' => 0, 'outstanding' => 1]];
        });
    }

    /**
     * Issues a client grant, deciding room policy and capacity from the room's own state.
     *
     * This is the whole join decision in one transaction: the code the caller presented, the
     * visibility they were promised, the content they claim, the phase, and whether a seat is
     * actually free. Capacity counts seated peers *and* live grants, so a room cannot be
     * oversubscribed by issuing grants faster than they are redeemed.
     */
    /** Code possession permits discovering pinned content before fetching it; it grants no seat. */
    public function inspectRoom(string $roomId, string $rawCode, array $spec): array
    {
        $code = Store::normalizeRoomCode($rawCode);
        $now = $this->store->now();
        $result = $this->store->withLock(self::file($roomId), function (array $state) use ($code, $spec, $now): array {
            if ($state === []) return [null, ['error' => 'room_not_found']];
            $state = self::expireGrants(self::expire($state, $now), $now);
            if (($state['closed'] ?? false) || $code === null || (string)$state['code'] !== $code)
                return [$state, ['error' => 'room_not_found']];
            if ((string)$state['appVersion'] !== (string)$spec['appVersion'])
                return [$state, ['error' => 'version_mismatch', 'hostVersion' => $state['appVersion'], 'clientVersion' => $spec['appVersion']]];
            if ((int)$state['gameProtocol'] !== (int)$spec['gameProtocol'])
                return [$state, ['error' => 'content_mismatch']];
            return [$state, ['code' => $state['code'], 'contentHash' => $state['contentHash'],
                'running' => $state['phase'] !== 'lobby' || $state['everStarted'] === true]];
        });
        self::refuse($result);
        return $result;
    }

    public function issueClientGrant(string $roomId, string $rawCode, array $spec): array
    {
        $code = Store::normalizeRoomCode($rawCode);
        $now  = $this->store->now();
        $grant = $roomId . Store::randomHex(28);

        $result = $this->store->withLock(self::file($roomId), function (array $state) use (
            $code, $spec, $now, $grant
        ): array {
            if ($state === []) {
                return [null, ['error' => 'room_not_found']];
            }
            $state = self::expireGrants(self::expire($state, $now), $now);
            $refusal = null;
            // The index is only a hint about which room a code names; this is the check that
            // decides. A code the room has since rotated away from admits nobody.
            if (($state['closed'] ?? false) || $code === null || (string)$state['code'] !== $code) {
                $refusal = 'room_not_found';
            } elseif ($spec['publicOnly'] && $state['visibility'] !== 'public') {
                $refusal = 'not_listed';
            } elseif ((string)$state['appVersion'] !== (string)$spec['appVersion']) {
                $refusal = 'version_mismatch';
            } elseif ((int)$state['gameProtocol'] !== (int)$spec['gameProtocol']
                      || (string)$state['contentHash'] !== (string)$spec['contentHash']) {
                $refusal = 'content_mismatch';
            } elseif ($state['phase'] !== 'lobby' || $state['everStarted'] === true) {
                $refusal = 'match_in_progress';
            } elseif (self::reservedSeats($state) >= (int)$state['maxPeers']) {
                $refusal = 'room_full';
            }
            if ($refusal !== null) {
                return [$state, ['error' => $refusal, 'hostVersion' => (string)$state['appVersion'],
                                 'clientVersion' => (string)$spec['appVersion']]];
            }
            $state = self::issueGrant($state, $grant, 'client', $spec, $now);
            $state['lastSeen'] = $now;
            return [$state, ['grant' => $grant, 'code' => (string)$state['code'],
                             'maxPeers' => (int)$state['maxPeers'],
                             'visibility' => (string)$state['visibility'],
                             'peers' => count($state['peers']),
                             'outstanding' => count($state['grants'])]];
        });
        self::refuse($result);
        return $result;
    }

    /**
     * Redeems a grant and seats the peer, in one transaction.
     *
     * This is the fix for R2-H3. Consuming the grant and taking the seat used to be two commits
     * in two files: the directory recorded a seat before the room had decided whether the peer
     * could have one, so a refusal here - or a crash in between - left a seat occupied by nobody
     * for the room's lifetime. One grant was enough to close a two-player room permanently.
     *
     * Now every reason to refuse is evaluated from state read in this transaction, and the grant
     * removal, the seat and the peer record commit together or not at all. The grant is burned on
     * a refusal, because a redemption is one attempt - but burning it *frees* capacity rather
     * than holding it, so a refused attempt cannot cost the room a seat.
     *
     * @return array{peer:int,session:string,role:string,phase:string,maxPeers:int}
     */
    public function redeemAndSeat(string $grant, array $claims, string $name, string $runtime, string $nonce = ""): array
    {
        $roomId = self::roomIdFromToken($grant, 'That invitation is not valid.');
        $now    = $this->store->now();
        $secret = Store::randomHex(28);
        $token  = $roomId . $secret;

        $result = $this->store->withLock(self::file($roomId), function (array $state) use (
            $grant, $claims, $name, $runtime, $nonce, $now, $token
        ): array {
            if ($state === []) {
                return [null, ['error' => 'unauthorized']];
            }
            $state = self::expireGrants($state, $now);
            $state = self::expire($state, $now);

            $hash = hash('sha256', $grant);
            $recovery = $state['redemptions'][$hash] ?? null;
            if ($nonce !== '' && is_array($recovery) && $recovery['nonce'] === $nonce
                && $recovery['name'] === $name && $recovery['claims'] === $claims
                && isset($state['peers'][(string)$recovery['result']['peer']])
                && !($state['closed'] ?? false) && ($state['phase'] === 'lobby' || ($recovery['result']['spectator'] ?? false))) {
                return [$state, array_merge($recovery['result'], ['recovered' => true])];
            }
            $record = null;
            foreach (($state['grants'] ?? []) as $key => $candidate) {
                if (hash_equals((string)$key, $hash)) {
                    $record = $candidate;
                    break;
                }
            }
            if (!is_array($record)) {
                return [$state, ['error' => 'unauthorized']];
            }
            // Single use: removed before anything else is decided, so a replay - even one that
            // arrives in the same millisecond from another worker - finds nothing.
            unset($state['grants'][$hash]);

            $refusal = null;
            if (($state['closed'] ?? false)) {
                $refusal = 'room_not_found';
            } elseif ((int)$record['expiresAt'] <= $now) {
                $refusal = 'unauthorized';
            } elseif ((int)$record['epoch'] !== (int)$state['epoch']
                      || (string)$record['code'] !== (string)$state['code']) {
                // Issued for a lobby that no longer exists in that form: a match start or an
                // invitation rotation overtook this handshake, and it must lose.
                $refusal = 'unauthorized';
            } else {
                foreach (['gameProtocol', 'contentHash', 'appVersion', 'runtime'] as $field) {
                    if ((string)$record['claims'][$field] !== (string)$claims[$field]) {
                        $refusal = 'content_mismatch';
                    }
                }
            }
            if ($refusal === null && (string)$state['appVersion'] !== (string)$claims['appVersion']) {
                return [$state, ['error' => 'version_mismatch', 'hostVersion' => (string)$state['appVersion'],
                                 'clientVersion' => (string)$claims['appVersion']]];
            }
            $role = (string)$record['role'];
            $spectator=(int)$state['gameProtocol']>=8 && ($record['spectator']??false);
            if($refusal===null && $spectator && ($state['phase']!=='match'
                || ($record['name']??'')!==$name || empty($record['lateRequest']))) $refusal='unauthorized';
            if ($refusal === null && $role === 'client'
                && !$spectator
                && ($state['phase'] !== 'lobby' || ($state['everStarted'] === true
                    && (empty($record['lateRequest']) || ($state['joinWindow']??'')!==$record['lateRequest'] || ($record['name']??'')!==$name)))) {
                $refusal = 'match_in_progress';
            }
            if ($refusal === null && self::reservedSeats($state) >= (int)$state['maxPeers']) {
                $refusal = 'room_full';
            }
            if ($refusal === null && $role === 'host' && (int)$state['hostPeerId'] !== 0) {
                // Exactly one host per room, for the room's whole life.
                $refusal = 'host_taken';
            }
            if ($refusal === null) {
                foreach ($state['peers'] as $seated) {
                    // Two players under one name is refused by the client's own snapshot check:
                    // the lobby and the command path both use a name as identity, so a duplicate
                    // is how one player is mistaken for another.
                    if ((string)$seated['name'] === $name) {
                        $refusal = 'name_taken';
                    }
                }
            }
            $peerId = (int)$state['nextPeer'];
            if ($refusal === null && ($peerId < 1 || $peerId > 65535)) {
                $refusal = 'capacity';
            }
            if ($refusal !== null) {
                // The grant is gone and no seat was taken. Committing this is the point: the
                // capacity the grant was holding is released in the same write that refuses.
                $state['lastSeen'] = $now;
                return [$state, ['error' => $refusal, 'peers' => count($state['peers']),
                                 'outstanding' => count($state['grants'])]];
            }

            $state['nextPeer'] = $peerId + 1;
            $state['peers'][(string)$peerId] = [
                'role'     => $role,
                'name'     => $name,
                'runtime'  => $runtime,
                'gameVersion' => (string)$claims['appVersion'],
                'token'    => hash('sha256', $token),
                'joinedAt' => $now,
                'lateRequest' => $record['lateRequest'] ?? '',
                'spectator' => $spectator,
                'lastSeen' => $now,
                'epoch'    => (int)$state['epoch'],
            ];
            if ($role === 'host') {
                $state['hostPeerId'] = $peerId;
                $state['hostName']   = $name;
            }
            if(!empty($record['lateRequest'])) $state['joinRequests'][$record['lateRequest']]['state']='joined';
            $state['lastSeen'] = $now;
            $answer = [
                'peer'        => $peerId,
                'session'     => $token,
                'role'        => $role,
                'spectator'   => $spectator,
                'phase'       => (string)$state['phase'],
                'maxPeers'    => (int)$state['maxPeers'],
                'code'        => (string)$state['code'],
                'logId'       => (string)$state['logId'],
                'hostName'    => (string)$state['hostName'],
                'notification' => self::notificationSnapshot($state, $spectator ? $peerId : 0),
                'publicActivity' => self::publicActivitySnapshot($state),
                'peers'       => count($state['peers']),
                'outstanding' => count($state['grants']),
            ];
            if ($nonce !== '') {
                // Short-lived response recovery. Reusing a nonce never creates a second seat.
                $state['redemptions'][$hash] = ['nonce'=>$nonce, 'name'=>$name, 'claims'=>$claims,
                    'expiresAt'=>$now+45000, 'result'=>$answer];
                while (count($state['redemptions']) > Limits::MAX_PEERS_PER_ROOM) array_shift($state['redemptions']);
            }
            return [$state, $answer];
        });
        self::refuse($result);
        return $result;
    }

    /**
     * Host-only visibility change, decided by the room itself.
     *
     * Going from public to private rotates the invitation: the code that was advertised stops
     * admitting anybody the moment this commits, and every grant issued under it becomes
     * unredeemable because a grant carries the code it was issued for. There is no revocation
     * list to keep in step, and no ordering between this and the directory update that can leave
     * the old code working.
     */
    public function setVisibility(string $control, string $rawCode, string $visibility): array
    {
        $roomId = self::roomIdFromToken($control, 'Only the host can change visibility.', 403);
        $code   = Store::normalizeRoomCode($rawCode);
        $now    = $this->store->now();
        $rotated = Store::generateRoomCode();

        $result = $this->store->withLock(self::file($roomId), function (array $state) use (
            $control, $code, $visibility, $now, $rotated
        ): array {
            if ($state === [] || $code === null || $code !== (string)$state['code']
                || !hash_equals((string)$state['control'], hash('sha256', $control))) {
                return [null, ['error' => 'forbidden']];
            }
            if ($state['everStarted'] === true || $state['phase'] !== 'lobby') {
                return [null, ['error' => 'visibility_locked']];
            }
            if ($state['visibility'] === 'public' && $visibility === 'private') {
                $state['code'] = $rotated;
                $state['grants'] = [];
            }
            $state['visibility'] = $visibility;
            $state['lastSeen']   = $now;
            return [$state, ['code' => (string)$state['code'], 'visibility' => $visibility,
                             'peers' => count($state['peers']),
                             'outstanding' => count($state['grants'])]];
        });
        self::refuse($result);
        return $result;
    }

    /** Seats taken plus grants that can still be redeemed. Both cost a seat. */
    private static function reservedSeats(array $state): int
    {
        return count($state['peers'] ?? []) + count($state['grants'] ?? []);
    }

    private static function issueGrant(array $state, string $token, string $role, array $claims,
                                       int $now): array
    {
        $state['grants'][hash('sha256', $token)] = [
            'role'  => $role,
            'epoch' => (int)$state['epoch'],
            // Binding the grant to the code it was issued for is what makes an invitation
            // rotation revoke outstanding grants without a revocation list.
            'code'  => (string)$state['code'],
            'claims' => [
                'gameProtocol' => (string)$claims['gameProtocol'],
                'contentHash'  => (string)$claims['contentHash'],
                'appVersion'   => (string)$claims['appVersion'],
                'runtime'      => (string)$claims['runtime'],
            ],
            'expiresAt' => $now + Limits::GRANT_TTL_MS,
        ];
        return $state;
    }

    private static function expireGrants(array $state, int $now): array
    {
        foreach (($state['redemptions'] ?? []) as $key => $record) {
            if ((int)($record['expiresAt'] ?? 0) <= $now) unset($state['redemptions'][$key]);
        }
        foreach (($state['grants'] ?? []) as $key => $record) {
            if (!is_array($record) || (int)($record['expiresAt'] ?? 0) <= $now) {
                unset($state['grants'][$key]);
            }
        }
        return $state;
    }

    private static function versionMismatchError(string $host, string $client): ServiceError
    {
        return new ServiceError(409, 'version_mismatch',
            'Version mismatch. Host: ' . $host . '. Yours: ' . $client
            . '. Both players must use the same game version. Update or switch versions, then try again.');
    }

    /** Turns a refusal marker into the ServiceError it stands for. */
    private static function refuse(array $result): void
    {
        if (!isset($result['error'])) {
            return;
        }
        throw match ($result['error']) {
            'room_not_found' => new ServiceError(404, 'room_not_found', 'That room code is not open.'),
            'not_listed' => new ServiceError(404, 'room_not_found',
                'That public game is no longer listed.'),
            'version_mismatch' => self::versionMismatchError($result['hostVersion'], $result['clientVersion']),
            'content_mismatch' => new ServiceError(409, 'content_mismatch',
                'This room needs the same game version and content as the host.'),
            'match_in_progress' => new ServiceError(409, 'match_in_progress',
                'That game has already started, so nobody else can join it.'),
            'visibility_locked' => new ServiceError(409, 'match_in_progress',
                'Visibility cannot change after play starts.'),
            'room_full' => new ServiceError(409, 'room_full', 'That room is full.'),
            'host_taken' => new ServiceError(409, 'forbidden', 'That room already has a host.'),
            'name_taken' => new ServiceError(409, 'name_taken',
                'Somebody in that game is already using your player name. Change Player name in Settings, then try again.'),
            'forbidden' => new ServiceError(403, 'forbidden', 'Only the host can change visibility.'),
            'unauthorized' => new ServiceError(401, 'unauthorized',
                'That invitation has expired. Ask for a new one.'),
            default => new ServiceError(503, 'capacity',
                'The game service is busy. Try again shortly.'),
        };
    }

    /**
     * Resolves a session header to a room and a peer.
     *
     * The room id is the first eight characters of the token, so this needs no directory lookup
     * and no global lock: the remaining 56 hex characters are the secret, and they are compared
     * against a stored digest rather than against the token itself.
     *
     * @return array{roomId:string,peerId:int,state:array}
     */
    private static function authenticate(array $state, string $token, int $now): array
    {
        // Callers run expiry first, so a peer found here is a peer that is still a member. No
        // method may write into peers[$id] for an id that expiry has just removed.
        unset($now);
        $hash = hash('sha256', $token);
        foreach (($state['peers'] ?? []) as $peerId => $peer) {
            if (is_array($peer) && hash_equals((string)($peer['token'] ?? ''), $hash)) {
                return ['peerId' => (int)$peerId, 'peer' => $peer];
            }
        }
        throw new ServiceError(403, 'session_expired', 'That session is no longer valid.');
    }

    public static function roomIdFromSession(string $token): string
    {
        return self::roomIdFromToken($token, 'That session is not valid.');
    }

    /**
     * Every credential this service issues is `<8 hex room id><56 hex secret>`.
     *
     * Carrying the room id in the token is what lets a grant, a control token or a session
     * resolve to exactly one room file without a directory lookup - which is what makes the whole
     * of admission a single-file transaction. The id is only a locator; the 224-bit secret is the
     * credential, and it is compared against a digest with `hash_equals`.
     */
    private static function roomIdFromToken(string $token, string $message, int $status = 401): string
    {
        if (!preg_match('/^[0-9a-f]{64}$/D', $token)) {
            throw new ServiceError($status, $status === 403 ? 'forbidden' : 'unauthorized', $message);
        }
        return substr($token, 0, 8);
    }

    /** Everything a poll answers with, already filtered to what this peer may know. */
    public function poll(string $token, int $cursor, int $maxPayloadBytes): array
    {
        $roomId = self::roomIdFromSession($token);
        $now = $this->store->now();
        return $this->store->withLock(self::file($roomId), function (array $state) use (
            $token, $cursor, $now, $maxPayloadBytes
        ): array {
            if ($state === []) {
                throw new ServiceError(404, 'room_not_found', 'That room is no longer open.');
            }
            // Expiry runs before authentication, not after it. Authenticating against the
            // pre-expiry roster and then sweeping lets a request that arrives just after its own
            // session expired still find its token - and, in the case of the host, act on it.
            // See R2-M6.
            $before = count($state['peers']);
            $state = self::expire($state, $now);
            $who = self::authenticate($state, $token, $now);
            $peerId = $who['peerId'];
            $state['peers'][(string)$peerId]['lastSeen'] = $now;
            $state['lastSeen'] = $now;

            $lines = [];
            $budget = Limits::SIGNAL_MAX_RESPONSE_BYTES - 4096;
            foreach ($state['peers'] as $otherId => $other) {
                if(!self::visiblePeer($state,$peerId,(int)$otherId)) continue;
                $lines[] = ['peer', $otherId . '|' . $other['role'] . '|'
                    . bin2hex((string)$other['name']) . '|' . bin2hex((string)$other['runtime'])
                    . ((int)$state['gameProtocol']>=8 ? '|'.(($other['spectator']??false)?'1':'0') : '')];
            }
            // The client refuses a snapshot that names more departures than a room can hold, one
            // that repeats a departure, or one that says a player both joined and left. The
            // newest departures are the ones worth reporting if there are somehow more.
            $departed = [];
            foreach (array_reverse($state['gone'] ?? []) as $entry) {
                $id = (int)$entry['id'];
                if (isset($state['peers'][(string)$id]) || isset($departed[$id])
                    || count($departed) >= Limits::MAX_PEERS_PER_ROOM) {
                    continue;
                }
                $departed[$id] = true;
                $lines[] = ['gone', (string)$id];
            }
            // Only the attestation for a link this peer is actually on: A's certificate towards
            // B is B's business and nobody else's. The recipient is named explicitly so the
            // client can refuse an attestation bound to a pair it is not part of, rather than
            // having to infer that from context - see P2PSignal::parsePollResponse, which fails
            // the whole snapshot if `to` is not its own peer id.
            foreach (($state['fp'] ?? []) as $pair => $binding) {
                [$from, $to] = explode(':', (string)$pair, 2);
                if ((int)$to === $peerId && (int)$from !== $peerId
                    && isset($state['peers'][$from]) && self::visiblePeer($state,$peerId,(int)$from)) {
                    $lines[] = ['fp', $from . '|' . $peerId . '|' . $binding['a'] . '|'
                                      . $binding['v']];
                }
            }

            $delivered = 0;
            $highest = (int)$state['seq'];
            $sent = [];
            foreach (($state['signals'] ?? []) as $record) {
                if ((int)$record['t'] !== $peerId || (int)$record['s'] <= $cursor
                    || !self::visiblePeer($state,$peerId,(int)$record['f'])) {
                    continue;
                }
                if ($delivered >= Limits::SIGNAL_MAX_PER_POLL) {
                    $highest = min($highest, (int)$record['s'] - 1);
                    break;
                }
                // `sig=<from>|<seq>|<kind>|<hex>`: the client attributes the record by `from`,
                // which is this service's own record of who posted it, never the caller's claim.
                $value = $record['f'] . '|' . $record['s'] . '|' . $record['k'] . '|' . $record['d'];
                $cost = strlen($value) + 5;
                if ($cost > $budget) {
                    $highest = min($highest, (int)$record['s'] - 1);
                    break;
                }
                $budget -= $cost;
                $lines[] = ['sig', $value];
                $sent[] = (int)$record['s'];
                $delivered++;
            }
            $newCursor = $sent === [] ? $highest : max($sent);
            $lines[] = ['cursor', (string)$newCursor];
            if ($state['closed'] ?? false) $lines[] = ['closed', '1000'];

            return [$state, [
                'phase'   => (string)$state['phase'],
                'lines'   => $lines,
                // The directory keeps a seat count for the public listing. It is only refreshed
                // when a poll actually observes a change, so the common case stays one lock.
                'changed' => $before !== count($state['peers']),
                'peers'   => count($state['peers']),
            ]];
        });
    }

    /**
     * Posts one offer, answer or candidate to another member of the same room.
     *
     * The offer/answer transition is enforced per ordered pair: one offer, then one answer in the
     * other direction, and candidates only once a description exists. A repeat of a record that
     * is still queued is idempotent, so a client that retries a lost HTTP request does not
     * confuse its counterparty's negotiation.
     */
    public function signal(string $token, int $to, string $kind, string $payload, int $maxPayloadBytes): void
    {
        $roomId = self::roomIdFromSession($token);
        $now = $this->store->now();

        if ($kind === 'candidate') {
            Sdp::validateCandidate($payload);
            $algorithm = null;
            $value = null;
        } else {
            [$algorithm, $value] = Sdp::validateDescription($payload, $kind, $maxPayloadBytes);
        }
        if (strlen($payload) > $maxPayloadBytes) {
            throw new ServiceError(413, 'signal_too_large',
                'That connection offer is larger than this service can deliver.');
        }

        $this->store->withLock(self::file($roomId), function (array $state) use (
            $token, $to, $kind, $payload, $algorithm, $value, $now
        ): array {
            if ($state === []) {
                throw new ServiceError(404, 'room_not_found', 'That room is no longer open.');
            }
            $state = self::expire($state, $now);
            $who = self::authenticate($state, $token, $now);
            $from = $who['peerId'];
            // Membership is the authorisation. A recipient that is not in this room right now,
            // in this epoch, is not addressable - there is no such thing as a cross-room send.
            if ($from === $to || !isset($state['peers'][(string)$to]) || !self::visiblePeer($state,$from,$to)) {
                throw new ServiceError(403, 'forbidden', 'That player is not in this room.');
            }
            if ((int)$state['peers'][(string)$from]['epoch'] !== (int)$state['epoch']
                || (int)$state['peers'][(string)$to]['epoch'] !== (int)$state['epoch']) {
                throw new ServiceError(409, 'stale_epoch', 'This room changed while you were connecting.');
            }
            $state['peers'][(string)$from]['lastSeen'] = $now;
            $state['lastSeen'] = $now;

            $pair = $from . ':' . $to;
            $reverse = $to . ':' . $from;
            $book = $state['pair'][$pair] ?? ['desc' => 0, 'cand' => 0, 'bytes' => 0, 'state' => 'none'];

            if ($kind !== 'candidate') {
                $binding = $state['fp'][$pair] ?? null;
                if (is_array($binding)) {
                    if (!hash_equals((string)$binding['v'], (string)$value)
                        || (string)$binding['a'] !== (string)$algorithm) {
                        // Same link, different certificate. Whoever this is, it is not the peer
                        // the other end has already been told to expect.
                        throw new ServiceError(409, 'fingerprint_changed',
                            'This connection already has a different certificate.');
                    }
                } else {
                    $state['fp'][$pair] = ['a' => $algorithm, 'v' => $value, 'e' => (int)$state['epoch']];
                }
                if ($kind === 'offer' && $book['state'] !== 'none') {
                    if (!self::isDuplicate($state, $from, $to, $kind, $payload)) {
                        throw new ServiceError(409, 'bad_transition',
                            'This connection has already been offered.');
                    }
                    return [$state, null];
                }
                if ($kind === 'answer') {
                    $reverseBook = $state['pair'][$reverse] ?? null;
                    if (!is_array($reverseBook) || $reverseBook['state'] !== 'offered') {
                        if (self::isDuplicate($state, $from, $to, $kind, $payload)) {
                            return [$state, null];
                        }
                        throw new ServiceError(409, 'bad_transition',
                            'There is nothing to answer on this connection.');
                    }
                    if ($book['state'] === 'answered') {
                        if (self::isDuplicate($state, $from, $to, $kind, $payload)) {
                            return [$state, null];
                        }
                        throw new ServiceError(409, 'bad_transition',
                            'This connection has already been answered.');
                    }
                }
                if (++$book['desc'] > Limits::PAIR_MAX_DESCRIPTIONS) {
                    throw new ServiceError(429, 'pair_budget', 'Too many connection attempts on this link.');
                }
                $book['state'] = $kind === 'offer' ? 'offered' : 'answered';
            } else {
                if (!isset($state['fp'][$pair]) && !isset($state['fp'][$reverse])) {
                    throw new ServiceError(409, 'bad_transition',
                        'Connection details arrived before the connection offer.');
                }
                if (self::isDuplicate($state, $from, $to, $kind, $payload)) {
                    return [$state, null];
                }
                if (++$book['cand'] > Limits::PAIR_MAX_CANDIDATES) {
                    throw new ServiceError(429, 'pair_budget', 'Too many connection details on this link.');
                }
            }
            $book['bytes'] = (int)$book['bytes'] + strlen($payload);
            if ($book['bytes'] > Limits::PAIR_MAX_BYTES) {
                throw new ServiceError(429, 'pair_budget', 'Too much connection traffic on this link.');
            }
            $state['pair'][$pair] = $book;

            if (count($state['signals']) >= Limits::ROOM_MAX_SIGNALS
                || (int)$state['bytes'] + strlen($payload) > Limits::ROOM_MAX_SIGNAL_BYTES) {
                throw new ServiceError(429, 'room_budget', 'This room is exchanging too much setup traffic.');
            }
            $state['seq'] = (int)$state['seq'] + 1;
            $state['bytes'] = (int)$state['bytes'] + strlen($payload);
            $state['signals'][] = [
                's' => (int)$state['seq'],
                'f' => $from,
                't' => $to,
                'k' => $kind,
                'd' => bin2hex($payload),
                'at' => $now,
            ];
            return [$state, null];
        });
    }

    /** A retry of a record that is still queued is not a second record. */
    private static function isDuplicate(array $state, int $from, int $to, string $kind, string $payload): bool
    {
        $hex = bin2hex($payload);
        foreach (($state['signals'] ?? []) as $record) {
            if ((int)$record['f'] === $from && (int)$record['t'] === $to
                && (string)$record['k'] === $kind && (string)$record['d'] === $hex) {
                return true;
            }
        }
        return false;
    }

    /**
     * Host-only phase change.
     *
     * Starting a match closes the room to new participants for good and bumps the epoch, so any
     * grant issued for the lobby is now unredeemable. Returning to the lobby (a co-op next
     * mission) bumps the epoch again but does not reopen the room: `everStarted` never goes back.
     *
     * Certificate bindings are deliberately *not* cleared by an epoch change. A pair's
     * fingerprint is fixed for the room's whole life, which is stricter than per-epoch and means
     * a phase change can never be used to launder a new certificate onto an existing link.
     */
    public function setPhase(string $token, string $phase, string $roster = ""): array
    {
        $roomId = self::roomIdFromSession($token);
        $now = $this->store->now();
        return $this->store->withLock(self::file($roomId), function (array $state) use ($token, $phase, $roster, $now): array {
            if ($state === []) {
                throw new ServiceError(404, 'room_not_found', 'That room is no longer open.');
            }
            $state = self::expire($state, $now);
            $who = self::authenticate($state, $token, $now);
            if ((int)$state['hostPeerId'] !== $who['peerId']) {
                throw new ServiceError(403, 'forbidden', 'Only the host can start the game.');
            }
            $state['peers'][(string)$who['peerId']]['lastSeen'] = $now;
            $state['lastSeen'] = $now;
            if ($phase === 'match') {
                // Controller slots remain a game rule; observers only need transport seats.
                if(($state['allowLateJoin']??false) && (int)$state['gameProtocol']>=7) $state['maxPeers']=Limits::MAX_PEERS_PER_ROOM;
                $controllers=array_filter($state['peers'],static fn(array $p)=>!($p['spectator']??false));
                $ids = array_map('intval', array_keys($controllers)); sort($ids, SORT_NUMERIC);
                if ($roster !== implode(',', $ids)) {
                    throw new ServiceError(409, 'roster_changed', 'The players changed. Check the lobby before starting.');
                }
                if ($state['phase'] !== 'match') $state['startId'] = Store::randomHex(16);
                $state['grants'] = [];
                $state['redemptions'] = [];
            }
            $resuming=!empty($state['joinWindow']);
            $joinedPeer = 0;
            if ($phase === 'match' && $resuming) {
                foreach ($state['peers'] as $id => $peer) {
                    if (($peer['lateRequest'] ?? '') === $state['joinWindow'] && !($peer['spectator'] ?? false))
                        $joinedPeer = (int)$id;
                }
            }
            if($phase==='match') $state['joinWindow']='';
            $phaseChanged = (string)$state['phase'] !== $phase;
            if ($phaseChanged) {
                $state['phase'] = $phase;
                $state['epoch'] = (int)$state['epoch'] + 1;
                if ($phase === 'match') {
                    $state['everStarted'] = true;
                    if (empty($state['startedAt'])) $state['startedAt'] = $now;
                }
                foreach ($state['peers'] as $peerId => $peer) {
                    $state['peers'][$peerId]['epoch'] = (int)$state['epoch'];
                }
            }
            return [$state, ['phase' => (string)$state['phase'], 'epoch' => (int)$state['epoch'],
                             'phaseChanged' => $phaseChanged, 'resuming' => $resuming,
                             'notification' => self::notificationSnapshot($state, $joinedPeer),
                             'publicActivity' => self::publicActivitySnapshot($state),
                             'everStarted' => (bool)$state['everStarted'],
                             'startId' => $state['startId'] ?? '', 'roster' => $roster,
                             'logId' => $state['logId'],
                             'peers' => count($state['peers'])]];
        });
    }

    /** A peer says goodbye. The host leaving a lobby ends the room; leaving a match does not. */
    public function leave(string $token): array
    {
        $roomId = self::roomIdFromSession($token);
        $now = $this->store->now();
        return $this->store->withLock(self::file($roomId), function (array $state) use ($token, $now): array {
            if ($state === []) {
                return [null, ['closed' => false, 'peers' => 0]];
            }
            $state = self::expire($state, $now);
            $who = self::authenticate($state, $token, $now);
            $peerId = $who['peerId'];
            $state = self::removePeer($state, $peerId, $now);
            $hostLeftLobby = (int)$state['hostPeerId'] === $peerId && $state['phase'] === 'lobby';
            $state['closed'] = $hostLeftLobby || count($state['peers']) === 0;
            $state['lastSeen'] = $now;
            return [$state, [
                'closed' => $state['closed'],
                'participant_id' => $peerId, 'runtime' => $who['peer']['runtime'],
                'game_version' => $who['peer']['gameVersion'] ?? '',
                'peers'  => count($state['peers']),
                'logId'  => (string)$state['logId'],
            ]];
        });
    }

    /** Spectators have a host-only stream; they cannot contact another player's game. */
    private static function visiblePeer(array $state, int $a, int $b): bool
    {
        if($a===$b || $a===(int)$state['hostPeerId'] || $b===(int)$state['hostPeerId']) return true;
        return !($state['peers'][(string)$a]['spectator']??false)
            && !($state['peers'][(string)$b]['spectator']??false);
    }

    private static function removePeer(array $state, int $peerId, int $now): array
    {
        if (!isset($state['peers'][(string)$peerId])) {
            return $state;
        }
        unset($state['peers'][(string)$peerId]);
        $gone = $state['gone'] ?? [];
        $gone[] = ['id' => $peerId, 'at' => $now];
        while (count($gone) > Limits::MAX_PEERS_PER_ROOM) {
            array_shift($gone);
        }
        $state['gone'] = $gone;
        // Anything still queued for somebody who is not here is not deliverable and not kept.
        $state['signals'] = array_values(array_filter($state['signals'] ?? [],
            static fn(array $r) => (int)$r['t'] !== $peerId && (int)$r['f'] !== $peerId));
        return $state;
    }

    /**
     * Time-based cleanup inside one room.
     *
     * A silent peer is only forgotten - and only announced as gone - while the room is still in
     * the lobby. Once a match is running the players have direct data channels and most of them
     * have stopped polling entirely, so silence here is the expected state and is not evidence
     * that anybody left. Announcing `gone=` then would tear down a working match.
     */
    private static function expire(array $state, int $now): array
    {
        if ($state['phase'] === 'lobby') {
            foreach (($state['peers'] ?? []) as $peerId => $peer) {
                if ($now - (int)$peer['lastSeen'] > Limits::LOBBY_PEER_IDLE_MS) {
                    $state = self::removePeer($state, (int)$peerId, $now);
                }
            }
        }
        // Lobby authority ends with its host. Match liveness belongs to the frozen P2P mesh.
        if ($state['phase'] === 'lobby' && (int)$state['hostPeerId'] !== 0
            && !isset($state['peers'][(string)$state['hostPeerId']])) {
            $state['closed'] = true;
            $state['grants'] = [];
        }
        $state['gone'] = array_values(array_filter($state['gone'] ?? [],
            static fn(array $g) => $now - (int)$g['at'] <= Limits::SIGNAL_TTL_MS));
        $state['signals'] = array_values(array_filter($state['signals'] ?? [],
            static fn(array $r) => $now - (int)$r['at'] <= Limits::SIGNAL_TTL_MS));
        return $state;
    }

    /** Public activity contains display names, never invitation or transport credentials. */
    private static function publicActivitySnapshot(array $state): array
    {
        if ($state['visibility'] !== 'public') return [];
        $players=[];
        foreach ($state['peers'] as $id => $peer) $players[]=[
            'id'=>(int)$id, 'name'=>(string)$peer['name'], 'role'=>(string)$peer['role'],
            'runtime'=>(string)$peer['runtime']];
        return ['room_id'=>(string)$state['logId'], 'player_name'=>(string)$state['hostName'],
            'mod_name'=>(string)($state['modName'] ?? ''), 'mode'=>(string)$state['mode'],
            'game_version'=>(string)$state['appVersion'], 'players'=>$players,
            'start_id'=>(string)($state['startId'] ?? '')];
    }

    /** Trusted deployment notification fields. Never include invitation or transport secrets. */
    private static function notificationSnapshot(array $state, int $joinedPeer = 0): array
    {
        $players = []; $spectators = [];
        foreach ($state['peers'] as $peer) {
            if ($peer['spectator'] ?? false) $spectators[] = (string)$peer['name'];
            else $players[] = (string)$peer['name'];
        }
        $event = [
            'room_log_id' => (string)$state['logId'],
            'mode' => (string)$state['mode'],
            'visibility' => (string)$state['visibility'],
            'host' => (string)$state['hostName'],
            'version' => (string)$state['appVersion'],
            'players' => count($players),
            'player_names' => $players,
            'spectator_names' => $spectators,
            'max_players' => (int)$state['maxPeers'],
        ];
        if ($joinedPeer && isset($state['peers'][(string)$joinedPeer])) {
            $peer = $state['peers'][(string)$joinedPeer];
            $event['participant_id'] = $joinedPeer;
            $event['joined_name'] = (string)$peer['name'];
            $event['joined_role'] = ($peer['spectator'] ?? false) ? 'spectator' : 'player';
        }
        return $event;
    }

    /** Only listing fields, derived from current authoritative room state. */
    public function directorySnapshot(string $roomId): ?array
    {
        $now = $this->store->now();
        return $this->store->withLock(self::file($roomId), static function (array $state) use ($now): array {
            if ($state === []) return [null, null];
            $state = self::expireGrants(self::expire($state, $now), $now);
            return [$state, [
                'id' => $state['id'], 'code' => $state['code'],
                'visibility' => $state['visibility'], 'phase' => $state['phase'],
                'everStarted' => $state['everStarted'], 'hostName' => $state['hostName'],
                'hostSeated' => isset($state['peers'][(string)$state['hostPeerId']]),
                'peers' => count($state['peers']), 'outstanding' => count($state['grants']),
                'maxPeers' => $state['maxPeers'], 'mode' => $state['mode'],
                'appVersion' => $state['appVersion'],
                'gameProtocol' => $state['gameProtocol'], 'contentHash' => $state['contentHash'],
                'modName' => $state['modName'] ?? '',
                'mapName' => $state['mapName'] ?? '',
                'allowLateJoin' => $state['allowLateJoin'] ?? false,
                'elapsed' => empty($state['startedAt']) ? 0 : max(0, intdiv($now-(int)$state['startedAt'],1000)),
                'hostActive' => isset($state['peers'][(string)$state['hostPeerId']]) && $now-(int)$state['peers'][(string)$state['hostPeerId']]['lastSeen'] < 45000,
                'createdAt' => $state['createdAt'],
            ]];
        });
    }
}
