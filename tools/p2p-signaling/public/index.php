<?php
declare(strict_types=1);

/**
 * The DuneCity direct-P2P signaling service.
 *
 * One PHP front controller, no daemon, no database, no outbound request. It answers the legacy
 * admission and lobby endpoints with exactly the wire contract the shipped client already parses
 * (include/Network/RoomAdmissionClient.h), and it adds the five `/v1/p2p/*` endpoints the direct
 * transport uses (include/Network/P2PSignalingProtocol.h).
 *
 * What it deliberately does not have: an endpoint that would accept a game packet, a TURN
 * credential, a WebSocket, and any code path that makes a request of its own. Once two players
 * have their data channels, this service can disappear and their match continues.
 */

require __DIR__ . '/../src/Limits.php';
require __DIR__ . '/../src/Config.php';
require __DIR__ . '/../src/Http.php';
require __DIR__ . '/../src/Store.php';
require __DIR__ . '/../src/Rate.php';
require __DIR__ . '/../src/Analytics.php';
require __DIR__ . '/../src/PublicActivity.php';
require __DIR__ . '/../src/Sdp.php';
require __DIR__ . '/../src/Rooms.php';
require __DIR__ . '/../src/Signaling.php';
require __DIR__ . '/../src/Lobby.php';
require __DIR__ . '/../src/Content.php';

const CONTENT_PATHS = ['/v1/content/list', '/v1/content/begin', '/v1/content/chunk',
                       '/v1/content/commit', '/v1/content/manifest', '/v1/content/blob',
                       '/v1/content/lookup'];

const ADMISSION_PATHS = ['/v1/admission/inspect', '/v1/admission/request', '/v1/admission/request-status', '/v1/admission/host', '/v1/admission/join', '/v1/admission/list',
                         '/v1/admission/visibility', '/v1/lobby/enter', '/v1/lobby/poll',
                         '/v1/lobby/say'];
const SIGNALING_PATHS = ['/v1/p2p/join-requests', '/v1/p2p/session', '/v1/p2p/poll', '/v1/p2p/signal', '/v1/p2p/phase',
                         '/v1/p2p/leave'];

/** Field shapes, as strict as the client's own. Anything not in this table is not a field. */
const FIELD_RULES = [
    'app'          => '/^[A-Za-z0-9_-]{1,32}$/D',
    'appVersion'   => '/^[A-Za-z0-9._-]{1,32}$/D',
    'contentHash'  => '/^[0-9a-f]{0,64}$/D',
    'runtime'      => '/^(native|browser)$/D',
    'mode'         => '/^(coop|custom)$/D',
    'visibility'   => '/^(public|private)$/D',
    'control'      => '/^[0-9a-f]{64}$/D',
    'publicOnly'   => '/^[01]$/D',
    'room'         => '/^[0-9A-Za-z-]{1,16}$/D',
    'grant'        => '/^[0-9a-f]{1,64}$/D',
    'nonce'        => '/^[0-9a-f]{32}$/D',
    'kind'         => '/^(offer|answer|candidate)$/D',
    'phase'        => '/^(lobby|match)$/D',
    'roster'       => '/^[1-9][0-9]{0,4}(,[1-9][0-9]{0,4}){0,15}$/D',
    'spectate'     => '/^[01]$/D',
    'cancel'       => '/^[01]$/D',
    'request'      => '/^[0-9a-f]{64}$/D',
    'action'       => '/^(list|approve|approve_spectator|decline|abort|request_play|cancel_play|play_status)$/D',
    'bye'          => '/^[01]$/D',
];

/** Optional local deployment hook; notification failures cannot veto a committed lobby. */
function notifyLobby(string $kind, array $result): void
{
    if (!function_exists('dunecityP2PNotifyLobby')) return;
    try { dunecityP2PNotifyLobby($kind, $result['notification'] ?? []); }
    catch (Throwable) { error_log('P2P lobby notification unavailable'); }
}

function requireField(array $form, string $name): string
{
    $value = $form[$name] ?? null;
    if (!is_string($value) || !preg_match(FIELD_RULES[$name], $value)) {
        throw new ServiceError(400, 'bad_request', "The '$name' field is missing or not valid.");
    }
    return $value;
}

function optionalField(array $form, string $name, string $fallback): string
{
    return array_key_exists($name, $form) ? requireField($form, $name) : $fallback;
}

function requireInteger(array $form, string $name, int $min, int $max): int
{
    $raw = $form[$name] ?? null;
    if (!is_string($raw) || !preg_match('/^[0-9]{1,9}$/D', $raw)) {
        throw new ServiceError(400, 'bad_request', "The '$name' field is missing or not valid.");
    }
    $value = (int)$raw;
    if ($value < $min || $value > $max) {
        throw new ServiceError(400, 'bad_request', "The '$name' field is out of range.");
    }
    return $value;
}

/** A display name the game would accept: no control characters, at most 64 bytes. */
function requireHexName(array $form, string $name): string
{
    $hex = $form[$name] ?? null;
    if (!is_string($hex) || strlen($hex) > Limits::MAX_NAME_CHARS * 2 || !Http::isHex($hex)) {
        throw new ServiceError(400, 'bad_request', "The '$name' field is missing or not valid.");
    }
    $text = (string)hex2bin($hex);
    if (preg_match('/[\x00-\x1f\x7f]/', $text)) {
        throw new ServiceError(400, 'bad_request', "The '$name' field is missing or not valid.");
    }
    return $text;
}

/**
 * The admission answer.
 *
 * `url=` is what the legacy parser requires and what the direct client is pointed at: an HTTPS url
 * ending in `/v1/p2p`, never a `ws://` or `wss://` gameplay endpoint - there is no gameplay
 * endpoint to name. `signaling=` carries the same service as a plain base url, which is the form
 * DirectRoomTransport::Config::signalingBaseUrl wants (it appends `/v1/p2p/session` itself). The
 * legacy parser skips a key it does not know, so both can be sent to both clients.
 */
function admissionLines(Config $config, array $room, string $grant, bool $host): array
{
    $base = (string)$config->get('public_base_url');
    $lines = [
        ['status', 'ok'],
        ['protocol', (string)Limits::PROTOCOL_VERSION],
        ['room', (string)$room['code']],
        ['grant', $grant],
        ['grantExpiresMs', (string)Limits::GRANT_TTL_MS],
        ['maxPeers', (string)$room['maxPeers']],
        ['url', $base . '/v1/p2p'],
        ['signaling', $base],
        ['visibility', (string)$room['visibility']],
    ];
    if ($host) {
        $lines[] = ['control', (string)$room['control']];
    }
    return $lines;
}

$signalingResponse = false;
$contentResponse = false;
$http = null;
$log = null;
$path = '';

try {
    $config = Config::load();
    $http = new Http($config);
    $path = $http->path();
    $signalingResponse = in_array($path, SIGNALING_PATHS, true);
    $contentResponse = in_array($path, CONTENT_PATHS, true);

    $store = new Store($config->stateDir());
    $rooms = new Rooms($store, $config);
    $log = new ServiceLog($store, $config->get('log_enabled') === true,
                          static fn(): string => $rooms->logSalt());
    $analytics = new Analytics($store, $config->get('analytics_enabled') === true);
    $activity = new PublicActivity($store, $config->get('analytics_enabled') === true);
    $rate = new Rate($store);
    $address = $http->address();
    $method = $http->method();
    $known = in_array($path, ADMISSION_PATHS, true) || $signalingResponse || $contentResponse;

    // The ingress allowance is charged before anything is parsed, so the cheapest way to reach
    // this service is still bounded.
    if ($contentResponse) {
        $rate->charge('content', $address, 16384, 4096);
    } else {
        $rate->charge('ingress', $address, Limits::RATE_GLOBAL_INGRESS, Limits::RATE_ADDRESS_INGRESS);
    }

    if ($method === 'GET' && $path === '/v1/health') {
        $http->send(200, [['status', 'ok'], ['protocol', (string)Limits::PROTOCOL_VERSION]], false);
        return;
    }
    if ($method === 'OPTIONS' && $known) {
        $http->checkOrigin();
        $http->sendPreflight();
        return;
    }
    if ($method !== 'POST' || !$known) {
        // Including GET and HEAD on a real endpoint: a credential must never be something a
        // browser can be navigated into sending. A game payload has no endpoint here at all.
        throw new ServiceError(404, 'bad_request', 'Unknown endpoint.');
    }

    $http->requireSecureTransport();
    $http->checkOrigin();

    if ($contentResponse) {
        if ($path === '/v1/content/begin') $rate->charge('contentBegin', $address, 256, 32);
        // One automatic collection preflight per started match; a client that keeps
        // asking is throttled exactly like the upload it would otherwise start.
        if ($path === '/v1/content/lookup') $rate->charge('contentLookup', $address, 256, 32);
        if ($path === '/v1/content/commit') $rate->charge('contentCommit', $address, 256, 64);
        $form = $http->form(524288, Content::MAX_MANIFEST * 2 + 32);
        $lines = (new Content($config))->handle(substr($path, strlen('/v1/content/')), $form, $address);
        Content::send($http, 200, $lines);
        return;
    }

    $isSignal = $path === '/v1/p2p/signal';
    $form = $http->form(
        $isSignal ? Limits::HTTP_MAX_SIGNAL_BODY_BYTES : Limits::HTTP_MAX_BODY_BYTES,
        $isSignal ? Limits::HTTP_MAX_SIGNAL_FIELD_BYTES : Limits::HTTP_MAX_FIELD_BYTES
    );

    // ------------------------------------------------------------------------------------
    // Signaling, authenticated by the session header alone
    // ------------------------------------------------------------------------------------
    if ($signalingResponse && $path !== '/v1/p2p/session') {
        $rate->charge('signal', $address, Limits::RATE_GLOBAL_SIGNAL, Limits::RATE_ADDRESS_SIGNAL);
        $token = $http->sessionHeader();
        if ($token === '') {
            throw new ServiceError(401, 'unauthorized', 'That session is not valid.');
        }
        $signaling = new Signaling($store, $config);
        $maxPayload = (int)$config->get('max_signal_payload_bytes');

        if ($path === '/v1/p2p/join-requests') {
            $lines=$signaling->manageLateJoin($token,optionalField($form,'action','list'),optionalField($form,'request',''));
            $http->send(200,array_merge([['status','ok'],['protocol',(string)Limits::PROTOCOL_VERSION]],$lines),true);
            return;
        }
        if ($path === '/v1/p2p/poll') {
            $cursor = requireInteger($form, 'cursor', 0, 1000000000);
            $result = $signaling->poll($token, $cursor, $maxPayload);
            if ($result['changed'] === true) {
                $rooms->touch(Signaling::roomIdFromSession($token), ['peers' => $result['peers']]);
            }
            $http->send(200, array_merge([
                ['status', 'ok'],
                ['protocol', (string)Limits::PROTOCOL_VERSION],
                ['phase', $result['phase']],
            ], $result['lines']), true);
            return;
        }
        if ($path === '/v1/p2p/signal') {
            $to = requireInteger($form, 'to', 1, 65535);
            $kind = requireField($form, 'kind');
            $data = $form['data'] ?? '';
            // The SDP arrives as hex in a form field. There is no upload, no file, no url and
            // no other way to hand this service a blob.
            if (!is_string($data) || strlen($data) > Limits::SIGNAL_MAX_PAYLOAD_BYTES * 2
                || !Http::isHex($data)) {
                throw new ServiceError(400, 'bad_signal', 'That connection offer is not valid.');
            }
            $signaling->signal($token, $to, $kind, (string)hex2bin($data), $maxPayload);
            $http->send(200, [['status', 'ok'], ['protocol', (string)Limits::PROTOCOL_VERSION]], true);
            return;
        }
        if ($path === '/v1/p2p/phase') {
            $phase = requireField($form, 'phase');
            $roomId = Signaling::roomIdFromSession($token);
            $result = $signaling->setPhase($token, $phase, $phase === 'match' ? requireField($form, 'roster') : '');
            $rooms->touch($roomId, [
                'phase' => $result['phase'], 'epoch' => $result['epoch'],
                'everStarted' => $result['everStarted'],
            ]);
            if ($phase === 'match' && $result['phaseChanged'] && !($result['resuming']??false)) {
                notifyLobby('started', $result);
                $analytics->record('started', [
                    'room_log_id' => $result['logId'] ?? null,
                    'peers_admitted' => $result['peers'],
                ]);
            }
            if ($phase === 'match' && $result['phaseChanged'] && ($result['resuming'] ?? false)
                && isset($result['notification']['joined_name'])) notifyLobby('hot_joined', $result);
            if ($phase==='match' && $result['phaseChanged'] && !empty($result['publicActivity']))
                $activity->record('public_game_started',$result['publicActivity']);
            $http->send(200, [['status', 'ok'], ['protocol', (string)Limits::PROTOCOL_VERSION],
                              ['phase', $result['phase']], ['startId', $result['startId']],
                              ['roster', $result['roster']]], true);
            return;
        }
        // /v1/p2p/leave
        $roomId = Signaling::roomIdFromSession($token);
        $result = $signaling->leave($token);
        $analytics->record('left', ['room_log_id' => $result['logId'] ?? null, 'reason' => 'normal',
            'participant_id' => $result['participant_id'] ?? null, 'runtime_claimed' => $result['runtime'] ?? null,
            'game_version' => $result['game_version'] ?? null]);
        if ($result['closed'] === true) {
            $analytics->record('closed', ['room_log_id' => $result['logId'] ?? null,
                                          'reason' => 'host_left']);
            $rooms->closeRoom($roomId);
        } else {
            $rooms->touch($roomId, ['peers' => $result['peers']]);
        }
        $http->send(200, [['status', 'ok'], ['protocol', (string)Limits::PROTOCOL_VERSION]], true);
        return;
    }

    // ------------------------------------------------------------------------------------
    // Everything else carries the compatibility claims
    // ------------------------------------------------------------------------------------
    if ($path !== '/v1/p2p/session') {
        $polling = in_array($path, ['/v1/admission/list', '/v1/admission/request-status', '/v1/lobby/poll', '/v1/lobby/say'], true);
        $rate->charge(
            $polling ? 'poll' : 'admission',
            $address,
            $polling ? Limits::RATE_GLOBAL_POLL : Limits::RATE_GLOBAL_ADMISSION,
            $polling ? Limits::RATE_ADDRESS_POLL : Limits::RATE_ADDRESS_ADMISSION
        );
    }

    if (requireField($form, 'app') !== $config->get('app')) {
        throw new ServiceError(400, 'bad_request', 'Unknown application.');
    }
    $appVersion = requireField($form, 'appVersion');
    $gameProtocol = requireInteger($form, 'gameProtocol', 0, 65535);
    $contentHash = optionalField($form, 'contentHash', '');
    $runtime = requireField($form, 'runtime');
    $required = (int)$config->get('required_game_protocol');
    if ($required !== 0 && $gameProtocol !== $required) {
        throw new ServiceError(409, 'unsupported_version',
            'This service expects a different game version.');
    }
    $claims = ['gameProtocol' => $gameProtocol, 'contentHash' => $contentHash,
               'appVersion' => $appVersion, 'runtime' => $runtime];

    if (str_starts_with($path, '/v1/lobby/')) {
        $lobby = new Lobby($store, $activity);
        $lines = $lobby->handle(substr($path, strlen('/v1/lobby/')), $form, $gameProtocol,
                                $contentHash, $address);
        $http->send(200, array_merge([['status', 'ok'],
            ['protocol', (string)Limits::PROTOCOL_VERSION]], $lines), false);
        return;
    }

    if ($path === '/v1/p2p/session') {
        // Redeeming a grant is this path's equivalent of the relay's socket connect, and gets
        // that allowance rather than the much smaller one for issuing grants.
        $rate->charge('session', $address, Limits::RATE_GLOBAL_SESSION, Limits::RATE_ADDRESS_SESSION);
        $grant = requireField($form, 'grant');
        $name = requireHexName($form, 'name');
        // Grant consumption and seating commit together in the authoritative room file.
        $signaling = new Signaling($store, $config);
        $result = $signaling->redeemAndSeat($grant, $claims, $name, $runtime,
            array_key_exists('nonce', $form) ? requireField($form, 'nonce') : '');
        $room = $result;
        if (!($result['recovered'] ?? false) && ($result['spectator'] ?? false))
            notifyLobby('hot_joined', $result);
        $rooms->touch(Signaling::roomIdFromSession($result['session']), array_merge($result,
            $result['role'] === 'host' ? ['hostSeated' => true] : []));
        if (!($result['recovered'] ?? false)) $analytics->record('joined', [
            'room_log_id'     => (string)$room['logId'],
            'participant_id'  => (int)$result['peer'],
            'game_version'    => $appVersion,
            'runtime_claimed' => $runtime,
        ]);
        if (!($result['recovered'] ?? false) && !empty($result['publicActivity'])) {
            $event=$result['publicActivity'];
            $event['player_name']=$name;
            $event['participant_id']=(int)$result['peer'];
            $event['role']=$result['role'];
            $event['spectator']=(bool)($result['spectator']??false);
            $activity->record($result['role']==='host' ? 'public_game_created' : 'public_game_joined', $event);
        }
        if ($result['role'] === 'host' && !($result['recovered'] ?? false)) {
            notifyLobby('hosted', $result);
        }
        $lines = [
            ['status', 'ok'],
            ['protocol', (string)Limits::PROTOCOL_VERSION],
            ['peer', (string)$result['peer']],
            ['session', $result['session']],
            ['role', $result['role']],
            ['spectator', ($result['spectator']??false) ? '1' : '0'],
            ['maxPeers', (string)$result['maxPeers']],
            ['phase', $result['phase']],
            // The invitation code goes only to somebody who has just proved membership.
            ['room', (string)$room['code']],
        ];
        foreach ((array)$config->get('ice_servers') as $url) {
            $lines[] = ['ice', (string)$url];
        }
        $http->send(200, $lines, true);
        return;
    }

    if ($path === '/v1/admission/visibility') {
        $control = requireField($form, 'control');
        $signaling = new Signaling($store, $config);
        $room = $signaling->setVisibility($control, requireField($form, 'room'),
                                         requireField($form, 'visibility'));
        $rooms->touch(Signaling::roomIdFromSession($control), $room);
        $http->send(200, [['status', 'ok'], ['protocol', (string)Limits::PROTOCOL_VERSION],
                          ['visibility', (string)$room['visibility']],
                          ['room', (string)$room['code']]], false);
        return;
    }

    if ($path === '/v1/admission/list') {
        $offset = array_key_exists('offset', $form)
            ? requireInteger($form, 'offset', 0, Limits::MAX_ROOMS) : 0;
        $allMods=($form['allMods'] ?? '') === '1';
        $details=($form['details'] ?? '') === '1';
        $page = $rooms->listPublic($appVersion, $gameProtocol, $contentHash, $offset, $allMods, $details);
        $lines = [['status', 'ok'], ['protocol', (string)Limits::PROTOCOL_VERSION],
                  ['next', (string)$page['next']]];
        foreach ($page['games'] as $game) {
            // Hex names cannot inject a delimiter or a line break. Private invitations and
            // grants are never part of discovery.
            $row=[(string)$game['code'], (string)$game['peers'], (string)$game['maxPeers'],
                  (string)$game['mode'], bin2hex((string)$game['hostName'])];
            if ($allMods || $details) { $row[]=(string)$game['contentHash']; $row[]=bin2hex((string)($game['modName'] ?? '')); }
            if ($details) { $row[]=bin2hex($game['mapName']); $row[]=$game['phase']; $row[]=(string)$game['elapsed']; $row[]=$game['allowLateJoin'] ? '1':'0'; }
            $lines[] = ['game', implode('|', $row)];
        }
        $http->send(200, $lines, false);
        return;
    }

    if ($path === '/v1/admission/host') {
        $mode = optionalField($form, 'mode', 'custom');
        $requested = requireInteger($form, 'maxPeers', 2, Limits::MAX_PEERS_PER_ROOM);
        $modName=isset($form['mod']) && $form['mod']!=='' ? Lobby::decodeText($form['mod'],64) : '';
        $spec = array_merge($claims, [
            'modName' => $modName,
            'mapName' => !empty($form['map']) ? Lobby::decodeText($form['map'],120) : '',
            'allowLateJoin' => ($form['allowLateJoin'] ?? '') === '1',
            'mode'       => $mode,
            'maxPeers'   => $mode === 'coop' ? 2 : $requested,
            'visibility' => optionalField($form, 'visibility', 'private'),
        ]);
        $reserved = $rooms->reserve($spec);
        $signaling = new Signaling($store, $config);
        $room = $signaling->createRoom($reserved['roomId'], $reserved['code'], $spec);
        $rooms->touch($reserved['roomId'], $room);
        $result = ['room' => $room, 'grant' => $room['grant']];
        $analytics->record('created', ['room_log_id' => (string)$result['room']['logId'],
                                       'game_version' => $appVersion,
                                       'runtime_claimed' => $runtime]);
        $http->send(200, admissionLines($config, $result['room'], $result['grant'], true), false);
        return;
    }

    if ($path === '/v1/admission/inspect') {
        $code = requireField($form, 'room');
        $result = (new Signaling($store, $config))->inspectRoom($rooms->resolve($code), $code, $claims);
        $http->send(200, [['status', 'ok'], ['protocol', (string)Limits::PROTOCOL_VERSION],
            ['room', $result['code']], ['contentHash', $result['contentHash']],
            ['running', $result['running'] ? '1' : '0']], false);
        return;
    }

    if ($path === '/v1/admission/request') {
        $code=requireField($form,'room'); $roomId=$rooms->resolve($code);
        $ticket=(new Signaling($store,$config))->requestLateJoin($roomId,$code,array_merge($claims,
            ['publicOnly'=>optionalField($form,'publicOnly','0')==='1']),requireHexName($form,'name'),optionalField($form,'spectate','0')==='1');
        $http->send(200,[['status','ok'],['protocol',(string)Limits::PROTOCOL_VERSION],['request',$ticket],['requestState','pending']],false);
        return;
    }
    if ($path === '/v1/admission/request-status') {
        $answer=(new Signaling($store,$config))->lateJoinStatus(requireField($form,'request'),$claims,optionalField($form,'cancel','0')==='1');
        $lines=$answer['requestState']==='approved' ? admissionLines($config,$answer,$answer['grant'],false)
            : [['status','ok'],['protocol',(string)Limits::PROTOCOL_VERSION]];
        $lines[]=['requestState',$answer['requestState']];
        $http->send(200,$lines,false); return;
    }
    if ($path === '/v1/admission/join') {
        $code = requireField($form, 'room');
        $roomId = $rooms->resolve($code);
        $signaling = new Signaling($store, $config);
        $room = $signaling->issueClientGrant($roomId, $code, array_merge($claims, [
            'publicOnly' => optionalField($form, 'publicOnly', '0') === '1',
        ]));
        $rooms->touch($roomId, $room);
        $result = ['room' => $room, 'grant' => $room['grant']];
        $http->send(200, admissionLines($config, $result['room'], $result['grant'], false), false);
        return;
    }

    throw new ServiceError(404, 'bad_request', 'Unknown endpoint.');
} catch (ServiceError $error) {
    if ($log !== null && $http !== null) {
        $log->denied($path, $error->errorCode, $http->address());
    }
    if ($http !== null) {
        if ($contentResponse) Content::send($http, $error->status, [
            ['status', 'error'], ['code', $error->errorCode], ['message', bin2hex($error->getMessage())]]);
        else $http->sendError($error, $signalingResponse);
    } else {
        http_response_code($error->status);
    }
} catch (ConfigError $error) {
    // The operator's problem, and only the operator's: the reason goes to the error log, and the
    // caller is told the service is unavailable without being told anything about the host.
    error_log('dunecity-p2p: configuration refused: ' . $error->getMessage());
    http_response_code(503);
    header('Content-Type: text/plain; charset=utf-8');
    header('Cache-Control: no-store');
    echo "status=error\ncode=unavailable\nmessage=The game service is not available right now.\n";
} catch (Throwable $error) {
    error_log('dunecity-p2p: ' . $error->getMessage());
    if ($log !== null && $http !== null) {
        $log->denied($path, 'internal', $http->address());
    }
    if ($http !== null) {
        if ($contentResponse) Content::send($http, 500, [['status', 'error'], ['code', 'internal'],
            ['message', bin2hex('The request could not be handled.')]]);
        else $http->sendError(new ServiceError(500, 'internal', 'The request could not be handled.'),
                             $signalingResponse);
    } else {
        http_response_code(500);
    }
}
