<?php
declare(strict_types=1);

/**
 * The room directory: a code index and a listing cache, and nothing that decides anything.
 *
 * This class used to hold the grants and the seat count, which made admission a two-file
 * operation: the directory committed "a seat is taken" and the room file then decided whether
 * the peer could actually sit down. When it said no - a duplicate display name, a host seat
 * already filled, a phase that had moved on - the seat stayed taken for the room's lifetime, and
 * a two-player room could be denied with one grant. A crash between the two commits left the same
 * ghost. See R2-H3.
 *
 * So the split is now a different one:
 *
 *   * **the room file is the room.** Its code, its policy, its phase and epoch, its outstanding
 *     grants and its seated peers all live in one file and change in one locked read-modify-write.
 *     Every decision that can deny somebody a seat is made there, from state read in that same
 *     transaction, and commits atomically with its own consequences;
 *   * **this index is a cache.** It maps an invitation code to a room id so a join can find the
 *     room file, it holds enough copied fields to render the public listing, and it gives the
 *     reaper a list of room ids to consider. Nothing here is trusted: a join re-checks the code
 *     against the room file, so a stale or half-written index fails closed rather than admitting
 *     anybody, and a wrong cached player count is a listing that is briefly off by one.
 *
 * Lock discipline is unchanged: one lock at a time, released before the next is taken, so there
 * is no order to get wrong. What changed is that the *cache* is the thing left stale by a crash,
 * never the authority.
 */
final class Rooms
{
    /** How long an allocation may sit unfinished before the reaper takes the code back. */
    private const PENDING_TTL_MS = 60000;
    /** Room files confirmed and deleted per request, so a sweep cannot become the request. */
    private const MAX_REAPED_PER_REQUEST = 4;

    public function __construct(private readonly Store $store, private readonly Config $config)
    {
    }

    // ------------------------------------------------------------------------------------
    // Index maintenance
    // ------------------------------------------------------------------------------------

    /**
     * Marks rooms whose cached state says they are finished. Nothing is deleted here: the cache
     * is not allowed to decide that a room is over, only to nominate it.
     *
     * @return array{0:array,1:list<string>} the swept index, and the ids to confirm
     */
    private static function sweep(array $state, int $now): array
    {
        $rooms = is_array($state['rooms'] ?? null) ? $state['rooms'] : [];
        $codes = is_array($state['codes'] ?? null) ? $state['codes'] : [];
        $candidates = [];

        foreach ($rooms as $roomId => $room) {
            if (!is_array($room) || !Store::isRoomId((string)$roomId)) {
                unset($rooms[$roomId]);
                continue;
            }
            if (($room['pending'] ?? false) === true) {
                // An allocation that never finished writing its room file. The code it reserved
                // is worth more than the entry, so it is taken back once it cannot still be in
                // flight.
                if ($now - (int)($room['createdAt'] ?? 0) > self::PENDING_TTL_MS) {
                    $candidates[] = (string)$roomId;
                }
                continue;
            }
            $idle = ($room['everStarted'] ?? false) ? Limits::MATCH_ROOM_IDLE_MS
                                                    : Limits::LOBBY_ROOM_IDLE_MS;
            $expired = ($now - (int)($room['createdAt'] ?? 0) > Limits::ROOM_LIFETIME_MS)
                || ((int)($room['peers'] ?? 0) === 0 && (int)($room['outstanding'] ?? 0) === 0
                    && $now - (int)($room['emptySince'] ?? 0) > Limits::EMPTY_ROOM_TTL_MS)
                || ($now - (int)($room['lastSeen'] ?? 0) > $idle);
            if ($expired) {
                $candidates[] = (string)$roomId;
            }
        }
        foreach ($codes as $code => $roomId) {
            if (!isset($rooms[(string)$roomId])) {
                unset($codes[$code]);
            }
        }
        $state['rooms'] = $rooms;
        $state['codes'] = $codes;
        if (!is_string($state['salt'] ?? null) || strlen((string)$state['salt']) !== 32) {
            $state['salt'] = Store::randomHex(16);
        }
        return [$state, array_slice($candidates, 0, self::MAX_REAPED_PER_REQUEST)];
    }

    /**
     * Confirms each nominated room against its own file before anything is deleted.
     *
     * The cached `lastSeen` only moves when something touches the index, and a lobby whose
     * players are quietly polling touches it rarely. Asking the room itself is what stops the
     * reaper from closing a room full of people who were simply not generating index writes.
     */
    private function confirmReaped(array $candidates): void
    {
        $now = $this->store->now();
        foreach ($candidates as $roomId) {
            // Re-check expiry and unlink under the same room lock. A peer may have polled
            // since the index nominated this room, and that activity must win over stale data.
            $deleted = $this->store->deleteRoomFile($roomId, static function (array $state) use ($now): bool {
                if ($state === []) return true;
                $idle = ($state['everStarted'] ?? false) ? Limits::MATCH_ROOM_IDLE_MS
                                                         : Limits::LOBBY_ROOM_IDLE_MS;
                return $now - (int)($state['lastSeen'] ?? 0) > $idle
                    || $now - (int)($state['createdAt'] ?? 0) > Limits::ROOM_LIFETIME_MS;
            });
            if ($deleted) $this->forget($roomId);
        }
    }

    /** Removes one room from the index. The room file is deleted separately, under its own lock. */
    private function forget(string $roomId): void
    {
        $this->store->withLock('index.json', static function (array $state) use ($roomId): array {
            $room = $state['rooms'][$roomId] ?? null;
            if (!is_array($room)) {
                return [null, null];
            }
            unset($state['rooms'][$roomId]);
            foreach (($state['codes'] ?? []) as $code => $mapped) {
                if ((string)$mapped === $roomId) {
                    unset($state['codes'][$code]);
                }
            }
            return [$state, null];
        });
    }

    /** The per-installation salt used to tag addresses in the log; created on first use. */
    public function logSalt(): string
    {
        return (string)$this->store->withLock('index.json', static function (array $state): array {
            if (!is_string($state['salt'] ?? null) || strlen((string)$state['salt']) !== 32) {
                $state['salt'] = Store::randomHex(16);
                return [$state, $state['salt']];
            }
            return [null, $state['salt']];
        });
    }

    /**
     * Copies authoritative fields back into the listing cache. Never an authorisation decision,
     * and never fatal: the cache being stale costs a wrong number in the directory, nothing more.
     */
    public function touch(string $roomId, array $fields): void
    {
        $now = $this->store->now();
        try { $this->store->withLock('index.json', static function (array $state) use ($roomId, $fields, $now): array {
            $room = $state['rooms'][$roomId] ?? null;
            if (!is_array($room)) {
                return [null, null];
            }
            foreach (['peers', 'outstanding', 'phase', 'everStarted', 'hostName', 'hostSeated',
                      'visibility', 'code'] as $field) {
                if (array_key_exists($field, $fields)) {
                    $room[$field] = $fields[$field];
                }
            }
            $room['pending']  = false;
            $room['lastSeen'] = (int)($fields['lastSeen'] ?? $now);
            $room['emptySince'] = (int)($room['peers'] ?? 0) === 0
                ? min((int)($room['emptySince'] ?? $now), $now) : $now;
            $state['rooms'][$roomId] = $room;
            if (array_key_exists('code', $fields)) {
                foreach (($state['codes'] ?? []) as $code => $mapped) {
                    if ((string)$mapped === $roomId && $code !== $fields['code']) {
                        unset($state['codes'][$code]);
                    }
                }
                $state['codes'][(string)$fields['code']] = $roomId;
            }
            return [$state, null];
        });
        } catch (\Throwable $ignored) { /* derived cache; authoritative room remains committed */ }
    }

    // ------------------------------------------------------------------------------------
    // Allocation and lookup
    // ------------------------------------------------------------------------------------

    /**
     * Reserves a room id and an invitation code.
     *
     * The entry is written `pending`, and stays invisible to the listing and unusable for a join
     * until the room file exists. If the caller dies before writing it, the reaper takes the code
     * back - and because no seat or grant lives here, an abandoned reservation costs a code and
     * nothing else.
     *
     * @return array{roomId:string,code:string}
     */
    public function reserve(array $spec): array
    {
        $now = $this->store->now();
        $result = $this->store->withLock('index.json', function (array $state) use ($spec, $now): array {
            [$state, $candidates] = self::sweep($state, $now);
            if (count($state['rooms']) >= Limits::MAX_ROOMS) {
                return [$state, ['error' => 'capacity', 'candidates' => $candidates]];
            }
            $roomId = null;
            for ($attempt = 0; $attempt < 8 && $roomId === null; $attempt++) {
                $candidate = Store::randomHex(4);
                if (!isset($state['rooms'][$candidate])) {
                    $roomId = $candidate;
                }
            }
            $code = null;
            for ($attempt = 0; $attempt < 8 && $code === null; $attempt++) {
                $candidate = Store::generateRoomCode();
                if (!isset($state['codes'][$candidate])) {
                    $code = $candidate;
                }
            }
            if ($roomId === null || $code === null) {
                return [$state, ['error' => 'capacity', 'candidates' => $candidates]];
            }
            $state['rooms'][$roomId] = [
                'id'           => $roomId,
                'code'         => $code,
                'pending'      => true,
                'visibility'   => $spec['visibility'] === 'public' ? 'public' : 'private',
                'mode'         => $spec['mode'],
                'maxPeers'     => $spec['maxPeers'],
                'gameProtocol' => $spec['gameProtocol'],
                'contentHash'  => $spec['contentHash'],
                'appVersion'   => $spec['appVersion'],
                'phase'        => 'lobby',
                'everStarted'  => false,
                'createdAt'    => $now,
                'emptySince'   => $now,
                'lastSeen'     => $now,
                'peers'        => 0,
                'outstanding'  => 0,
                'hostName'     => '',
                'hostSeated'   => false,
            ];
            $state['codes'][$code] = $roomId;
            return [$state, ['roomId' => $roomId, 'code' => $code, 'candidates' => $candidates]];
        });
        $this->confirmReaped($result['candidates']);
        if (isset($result['error'])) {
            throw new ServiceError(503, 'capacity',
                'The game service is at its room limit. Try again shortly.');
        }
        return $result;
    }

    /**
     * Resolves an invitation code to a room id.
     *
     * This is a hint and is treated as one: the room file re-checks the code it is presented
     * with, so a stale mapping - left by a rotation whose cache update did not land - ends in a
     * refusal rather than in somebody joining the wrong room.
     */
    public function resolve(string $rawCode): string
    {
        $code = Store::normalizeRoomCode($rawCode);
        if ($code === null) {
            throw new ServiceError(400, 'bad_request', 'That room code is not valid.');
        }
        $now = $this->store->now();
        $result = $this->store->withLock('index.json', function (array $state) use ($code, $now): array {
            [$state, $candidates] = self::sweep($state, $now);
            $roomId = (string)($state['codes'][$code] ?? '');
            $room = $state['rooms'][$roomId] ?? null;
            $usable = is_array($room) && ($room['pending'] ?? false) !== true;
            return [$state, ['roomId' => $usable ? $roomId : '', 'candidates' => $candidates]];
        });
        $this->confirmReaped($result['candidates']);
        if ($result['roomId'] === '') {
            throw new ServiceError(404, 'room_not_found', 'That room code is not open.');
        }
        return $result['roomId'];
    }

    /** Drops a room from the index and deletes its file. Used when a host leaves a lobby. */
    public function closeRoom(string $roomId): void
    {
        $this->store->deleteRoomFile($roomId);
        $this->forget($roomId);
    }

    /**
     * One page of the public directory, rendered entirely from the cache.
     *
     * Only public rooms with a seated host, available capacity and the caller's exact executable
     * version and protocol are offered. Content may differ only for an all-mods discovery request;
     * admission still verifies the selected revision. Private rooms are never enumerated.
     * A stale count can hide a joinable room or offer a full one; the join itself is authoritative.
     */
    public function listPublic(string $appVersion, int $gameProtocol, string $contentHash, int $offset, bool $allMods = false, bool $details = false): array
    {
        $now = $this->store->now();
        $result = $this->store->withLock('index.json', function (array $state) use ($now): array {
            [$state, $candidates] = self::sweep($state, $now);
            return [$state, ['rooms' => $state['rooms'], 'candidates' => $candidates]];
        });
        $this->confirmReaped($result['candidates']);

        $eligible = [];
        $signaling = new Signaling($this->store, $this->config);
        foreach ($result['rooms'] as $hint) {
            // A crashed or delayed cache update must never list a private room or strand a
            // released seat. Read policy and occupancy from the one authoritative room file.
            $room = $signaling->directorySnapshot((string)$hint['id']);
            if ($room === null) continue;
            if (($room['pending'] ?? false) !== true
                && $room['visibility'] === 'public'
                && ((!($room['everStarted'] ?? false) && $room['phase'] === 'lobby')
                    || ($details && ($room['allowLateJoin'] ?? false) && ($room['hostActive'] ?? false) && $room['phase'] === 'match'))
                && ($room['hostSeated'] ?? false) === true
                && (string)$room['hostName'] !== ''
                && (int)$room['peers'] + (int)$room['outstanding'] < (int)$room['maxPeers']
                && (string)($room['appVersion'] ?? '') === $appVersion
                && (int)$room['gameProtocol'] === $gameProtocol
                && ($allMods || (string)$room['contentHash'] === $contentHash)) {
                $eligible[] = $room;
            }
        }
        usort($eligible, static fn(array $a, array $b) => $a['createdAt'] <=> $b['createdAt']);
        $page = array_slice($eligible, $offset, Limits::PAGE_SIZE);
        return [
            'games' => $page,
            'next'  => $offset + count($page) < count($eligible) ? $offset + count($page) : 0,
        ];
    }

    /** @return array|null the cached directory entry, for telemetry and listing only */
    public function cached(string $roomId): ?array
    {
        return $this->store->withLock('index.json', static function (array $state) use ($roomId): array {
            $room = $state['rooms'][$roomId] ?? null;
            return [null, is_array($room) ? $room : null];
        });
    }
}
