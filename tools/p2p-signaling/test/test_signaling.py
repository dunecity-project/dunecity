#!/usr/bin/env python3
"""
Security and integration tests for the DuneCity P2P signaling service.

These drive a real PHP server over real HTTP. Nothing is stubbed: every assertion is about what
an actual client would receive, including the exact `key=value` envelope bounds that
include/Network/RoomAdmissionClient.h and include/Network/P2PSignalingProtocol.h parse against.

Run:  PHP_BIN=/path/to/php8.3 python3 tools/p2p-signaling/test/test_signaling.py
"""

import http.client
import json
import os
import shutil
import signal
import socket
import subprocess
import tempfile
import time
import unittest
import urllib.parse
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

PHP_BIN = os.environ.get("PHP_BIN") or shutil.which("php8.3") or shutil.which("php")

PROTOCOL = 1
APP_VERSION = "1.0.662"
GAME_PROTOCOL = 7
CONTENT_HASH = "a" * 64
ORIGIN = "https://dunelegacy.com"


def make_sdp(fp, kind="offer", extra=""):
    """A data-only description of the shape libdatachannel and a browser both produce."""
    setup = "actpass" if kind == "offer" else "active"
    return (
        "v=0\r\n"
        "o=- 4611731400430051336 2 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "a=group:BUNDLE 0\r\n"
        "a=msid-semantic: WMS\r\n"
        "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=ice-ufrag:F7gI\r\n"
        "a=ice-pwd:x9cml/YzichV2+XlhiMu8g\r\n"
        "a=ice-options:trickle\r\n"
        "a=fingerprint:sha-256 " + fp + "\r\n"
        "a=setup:" + setup + "\r\n"
        "a=mid:0\r\n"
        "a=sctp-port:5000\r\n"
        "a=max-message-size:262144\r\n"
        + extra
    )


def fingerprint(seed):
    return ":".join("%02X" % ((seed * 7 + i * 11) % 256) for i in range(32))


def pad_sdp_to(sdp, total):
    """Grows a description to exactly `total` bytes with ordinary-length attribute lines, the way
    a real one grows with a long candidate list - not one absurd line, which a per-line bound
    would catch for the wrong reason."""
    overhead = len("a=x-pad:\r\n")
    while total - len(sdp) > 100 + overhead:
        sdp += "a=x-pad:" + "p" * 90 + "\r\n"
    remaining = total - len(sdp) - overhead
    assert remaining >= 0, remaining
    return sdp + "a=x-pad:" + "p" * remaining + "\r\n"


# Candidates travel as "<sdpMid>|<candidate line>", which is what libdatachannel's
# Candidate::mid()/candidate() pair produces and what the browser bridge encodes from an
# RTCIceCandidate. See P2PSignal::isAcceptableCandidatePayload. The bound is 64 + 1 + 2048.
HOST_CANDIDATE = "0|candidate:1 1 UDP 2122252543 192.168.1.5 54321 typ host"
RELAY_CANDIDATE = ("0|candidate:4 1 UDP 41885439 203.0.113.9 50000 typ relay "
                   "raddr 0.0.0.0 rport 0")

# Shapes real stacks actually emit, rather than tidied-up ones. Chromium appends generation,
# ufrag, network-id and network-cost, hides a host address behind an mDNS name, and writes the
# transport in lower case; libdatachannel writes bare IPv4/IPv6 host candidates.
REAL_CANDIDATES = [
    ("chromium mdns host",
     "0|candidate:1510613869 1 udp 2122260223 "
     "4f9a2c31-6b5e-4d2a-9c77-1e0b8a5d3f42.local 51083 typ host "
     "generation 0 ufrag 1RpS network-id 1 network-cost 10"),
    ("chromium ipv6 host",
     "0|candidate:2853808081 1 udp 2122262783 "
     "2601:646:a080:1730:8c8d:2a1b:9f3e:44c2 51084 typ host "
     "generation 0 ufrag a+b/Cd network-id 2 network-cost 10"),
    ("chromium srflx",
     "0|candidate:842163049 1 udp 1677729535 203.0.113.7 51085 typ srflx "
     "raddr 0.0.0.0 rport 0 generation 0 ufrag 1RpS network-id 1 network-cost 10"),
    ("chromium tcp host",
     "0|candidate:3458259723 1 tcp 1518280447 192.168.1.5 9 typ host "
     "tcptype active generation 0 ufrag 1RpS network-id 1"),
    ("libdatachannel ipv4 host",
     "0|candidate:1 1 UDP 2122317823 192.168.1.5 54321 typ host"),
    ("libdatachannel ipv6 host",
     "0|candidate:2 1 UDP 2122317567 fe80::1c2d:3e4f:5a6b:7c8d 54322 typ host"),
    ("libdatachannel srflx",
     "0|candidate:3 1 UDP 1686110207 203.0.113.9 54323 typ srflx "
     "raddr 192.168.1.5 rport 54321"),
    ("empty media id",
     "|candidate:1 1 UDP 2122252543 192.168.1.5 54321 typ host"),
]


def candidate_line_of(total):
    """A grammatically valid ICE candidate line of exactly `total` bytes.

    Padding has to be legal attribute pairs rather than one enormous value, so that a size
    boundary test is measuring the size bound and not the grammar. RFC 8839 allows an ice-ufrag
    of up to 256 characters, which is what the server's per-value bound is set to.
    """
    line = "candidate:1 1 UDP 2122252543 192.168.1.5 54321 typ host"
    index = 0
    while total - len(line) > 7 + 256:
        line += " x%02d " % index + "v" * 256
        index += 1
    remaining = total - len(line) - len(" x%02d " % index)
    assert 1 <= remaining <= 256, remaining
    return line + " x%02d " % index + "v" * remaining


class Response:
    def __init__(self, status, headers, body):
        self.status = status
        self.headers = headers
        self.body = body
        self.lines = [line for line in body.split("\n") if line]
        self.fields = {}
        self.multi = {}
        for line in self.lines:
            key, _, value = line.partition("=")
            self.fields.setdefault(key, value)
            self.multi.setdefault(key, []).append(value)

    def __repr__(self):
        return "<%s %r>" % (self.status, self.body)


def php_literal(value):
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        return "'" + value.replace("\\", "\\\\").replace("'", "\\'") + "'"
    if isinstance(value, list):
        return "[" + ", ".join(php_literal(item) for item in value) + "]"
    if isinstance(value, dict):
        return "[" + ", ".join("%s => %s" % (php_literal(k), php_literal(v))
                               for k, v in value.items()) + "]"
    raise TypeError(value)


# ----------------------------------------------------------------------------------------------
# A transcription of the client's parser.
#
# This is NOT the real parser: there is no C++ compiler in this environment, so
# include/Network/P2PSignalingProtocol.h cannot be compiled and run against these responses. What
# follows is a line-by-line transcription of parseSessionResponse and parsePollResponse as they
# stand in the client workspace, kept here so that every response this suite produces is checked
# against one description of the contract instead of against ad-hoc per-test assertions.
#
# A transcription can drift from the header it was copied from. Treat a pass here as "the server
# agrees with what the contract was read to say", not as "the client parsed it".
# ----------------------------------------------------------------------------------------------

MAX_PEERS_PER_ROOM = 8
MAX_SIGNALS_PER_POLL = 32
MAX_ICE_SERVERS = 4
FINGERPRINT_CHARS = 95
MAX_SDP_BYTES = 65536
MAX_CANDIDATE_BYTES = 2048
MAX_CANDIDATE_MID_CHARS = 64


class ContractError(AssertionError):
    pass


def _unsigned(text, limit):
    if not text or len(text) > 20 or (len(text) > 1 and text[0] == "0"):
        raise ContractError("not an unsigned number: %r" % text)
    if not all("0" <= c <= "9" for c in text):
        raise ContractError("not an unsigned number: %r" % text)
    value = int(text)
    if value > limit:
        raise ContractError("%d exceeds %d" % (value, limit))
    return value


def _hex_text(text, max_bytes, allow_newlines=False):
    if len(text) > max_bytes * 2 or len(text) % 2:
        raise ContractError("hex field is malformed")
    if text and not all(c in "0123456789abcdef" for c in text):
        raise ContractError("hex field is not lowercase hex")
    raw = bytes.fromhex(text)
    for byte in raw:
        acceptable = byte >= 32 and byte != 127
        if not acceptable and not (allow_newlines and byte in (13, 10)):
            raise ContractError("hex field decodes to a control character")
    return raw.decode("utf-8", "replace")


def _fields(value, count):
    parts = value.split("|", count - 1)
    if len(parts) != count or "|" in parts[-1]:
        raise ContractError("expected %d fields in %r" % (count, value))
    return parts


def _lines(body, max_bytes, max_lines, max_line, max_key, max_value):
    """forEachResponseLine: bounded body, bounded lines, bounded keys and values."""
    if not body or len(body.encode()) > max_bytes:
        raise ContractError("body is empty or over %d bytes" % max_bytes)
    out = []
    for raw in body.split("\n"):
        line = raw[:-1] if raw.endswith("\r") else raw
        if not line:
            continue
        if len(out) + 1 > max_lines or len(line.encode()) > max_line:
            raise ContractError("too many lines, or a line over %d bytes" % max_line)
        split = line.find("=")
        if split <= 0 or split > max_key or len(line) - split - 1 > max_value:
            raise ContractError("malformed key=value line: %r" % line[:80])
        out.append((line[:split], line[split + 1:]))
    return out


def check_session_response(body):
    """P2PSignal::parseSessionResponse."""
    saw = set()
    ok = None
    ice = []
    for key, value in _lines(body, 524288, 256, 131200, 24, 131168):
        if key == "status":
            if value not in ("ok", "error"):
                raise ContractError("status=%r" % value)
            ok = value == "ok"
            saw.add("status")
        elif key == "protocol":
            if _unsigned(value, 65535) != PROTOCOL:
                raise ContractError("protocol mismatch")
        elif key == "peer":
            if _unsigned(value, 65535) == 0:
                raise ContractError("peer id 0")
            saw.add("peer")
        elif key == "session":
            if len(value) != 64 or not all(c in "0123456789abcdef" for c in value):
                raise ContractError("session token is not 64 lowercase hex")
            saw.add("session")
        elif key == "role":
            if value not in ("host", "client"):
                raise ContractError("role=%r" % value)
            saw.add("role")
        elif key == "maxPeers":
            if not 2 <= _unsigned(value, MAX_PEERS_PER_ROOM) <= MAX_PEERS_PER_ROOM:
                raise ContractError("maxPeers out of range")
            saw.add("maxPeers")
        elif key == "phase":
            if value not in ("lobby", "match"):
                raise ContractError("phase=%r" % value)
        elif key == "room":
            symbols = [c for c in value if c != "-"]
            if len(value) > 16 or len(symbols) != 12 or any(
                    c in "ILOU" or not (c.isdigit() or ("A" <= c <= "Z")) for c in symbols):
                raise ContractError("room code=%r" % value)
        elif key == "ice":
            if len(ice) >= MAX_ICE_SERVERS:
                raise ContractError("too many ice servers")
            if not (value.startswith("stun:") or value.startswith("stuns:")) or "@" in value:
                raise ContractError("ice url=%r" % value)
            ice.append(value)
    if "status" not in saw:
        raise ContractError("no status")
    if not ok:
        return
    if not {"peer", "session", "role", "maxPeers"} <= saw:
        raise ContractError("incomplete session answer: %s" % sorted(saw))


def check_poll_response(body, local_peer_id):
    """P2PSignal::parsePollResponse, including its whole-snapshot checks."""
    members, departed, fingerprints, signals = [], [], [], []
    saw_status = saw_cursor = False
    ok = None
    cursor = 0
    for key, value in _lines(body, 524288, 256, 131200, 24, 131168):
        if key == "status":
            if value not in ("ok", "error"):
                raise ContractError("status=%r" % value)
            ok = value == "ok"
            saw_status = True
        elif key == "phase":
            if value not in ("lobby", "match"):
                raise ContractError("phase=%r" % value)
        elif key == "cursor":
            cursor = _unsigned(value, 1000000000000)
            saw_cursor = True
        elif key == "peer":
            if len(members) >= MAX_PEERS_PER_ROOM:
                raise ContractError("more members than a room can hold")
            fields = _fields(value, 4)
            identifier = _unsigned(fields[0], 65535)
            if identifier == 0 or fields[1] not in ("host", "client"):
                raise ContractError("peer record=%r" % value)
            name = _hex_text(fields[2], 64)
            if not name or any(ord(c) < 32 or ord(c) == 127 for c in name):
                raise ContractError("unusable display name")
            members.append((identifier, fields[1], name, _hex_text(fields[3], 16)))
        elif key == "gone":
            if len(departed) >= MAX_PEERS_PER_ROOM:
                raise ContractError("more departures than a room can hold")
            identifier = _unsigned(value, 65535)
            if identifier == 0:
                raise ContractError("gone id 0")
            departed.append(identifier)
        elif key == "fp":
            if len(fingerprints) >= MAX_PEERS_PER_ROOM:
                raise ContractError("too many attestations")
            fields = _fields(value, 4)
            source = _unsigned(fields[0], 65535)
            target = _unsigned(fields[1], 65535)
            if source == 0 or target == 0 or source == target:
                raise ContractError("attestation pair=%r" % value)
            if fields[2] != "sha-256" or len(fields[3]) != FINGERPRINT_CHARS:
                raise ContractError("attestation algorithm or length: %r" % value)
            for index, c in enumerate(fields[3]):
                if (c != ":") if index % 3 == 2 else (c not in "0123456789ABCDEF"):
                    raise ContractError("attestation value=%r" % fields[3])
            fingerprints.append((source, target))
        elif key == "sig":
            if len(signals) >= MAX_SIGNALS_PER_POLL:
                raise ContractError("too many signals in one poll")
            fields = _fields(value, 4)
            source = _unsigned(fields[0], 65535)
            sequence = _unsigned(fields[1], 1000000000)
            if source == 0 or fields[2] not in ("offer", "answer", "candidate"):
                raise ContractError("signal record=%r" % value[:80])
            payload = _hex_text(fields[3], MAX_SDP_BYTES, allow_newlines=True)
            if not payload:
                raise ContractError("empty signal payload")
            signals.append((source, sequence, fields[2], payload))
    if not saw_status:
        raise ContractError("no status")
    if not ok:
        return
    if not saw_cursor:
        raise ContractError("no cursor")

    for index in range(1, len(signals)):
        if signals[index][1] <= signals[index - 1][1]:
            raise ContractError("signals out of order")
    if signals and signals[-1][1] > cursor:
        raise ContractError("signals past the cursor")

    hosts = [m for m in members if m[1] == "host"]
    if len(hosts) > 1:
        raise ContractError("more than one host")
    ids = [m[0] for m in members]
    names = [m[2] for m in members]
    if len(set(ids)) != len(ids) or len(set(names)) != len(names):
        raise ContractError("the same player described twice")
    if set(ids) & set(departed):
        raise ContractError("a player both joined and left")
    if len(set(departed)) != len(departed):
        raise ContractError("the same player left twice")
    for source, target in fingerprints:
        if local_peer_id and target != local_peer_id:
            raise ContractError("attestation bound to a different player")
        if source == local_peer_id or source not in ids:
            raise ContractError("attestation for somebody not in this game")
    for source, _, kind, payload in signals:
        if source == local_peer_id or source not in ids:
            raise ContractError("signal from outside this game")
        if kind == "candidate":
            split = payload.find("|")
            if split < 0 or split > MAX_CANDIDATE_MID_CHARS \
                    or len(payload) > MAX_CANDIDATE_BYTES + MAX_CANDIDATE_MID_CHARS + 1:
                raise ContractError("unusable candidate envelope")
            line = payload[split + 1:]
            if not line or len(line) > MAX_CANDIDATE_BYTES \
                    or any(ord(c) < 32 for c in line):
                raise ContractError("unusable candidate line")
        elif len(payload) > MAX_SDP_BYTES:
            raise ContractError("oversized description")


class ServiceFixture:
    """One PHP server with its own private state directory."""

    def __init__(self):
        # realpath, because the state directory's security checks require a canonical path and
        # macOS hands out /var/folders/... which is really /private/var/folders/...
        self.tmp = os.path.realpath(tempfile.mkdtemp(prefix="dunecity-p2p-test-"))
        self.state = os.path.join(self.tmp, "state")
        os.mkdir(self.state, 0o700)
        self.config = os.path.join(self.tmp, "config.php")
        self.write_config()
        self.port = self._free_port()
        # Real worker processes, so the concurrency tests exercise flock across processes the
        # way Apache would rather than being serialised by a single-threaded dev server.
        env = dict(os.environ, DUNECITY_P2P_CONFIG=self.config, PHP_CLI_SERVER_WORKERS="4")
        self.server_log = open(os.path.join(self.tmp, "php-server.log"), "w+b")
        self.notification_log = os.path.join(self.tmp, "notifications.jsonl")
        self.activity_log = os.path.join(self.tmp, "public-activity.jsonl")
        router = os.path.join(self.tmp, "router.php")
        Path(router).write_text("<?php\nfunction dunecityP2PNotifyLobby($kind, $event) {\n"
            "file_put_contents(" + php_literal(self.notification_log) + ", json_encode([$kind,$event]).\"\\n\", FILE_APPEND|LOCK_EX);\n"
            "if (file_exists(" + php_literal(os.path.join(self.tmp, "fail-notifications")) + ")) throw new RuntimeException('fixture');\n"
            "}\nfunction dunecityP2PRecordPublicActivity($event) {\n"
            "file_put_contents(" + php_literal(self.activity_log) + ", json_encode($event).\"\\n\", FILE_APPEND|LOCK_EX);\n"
            "if (file_exists(" + php_literal(os.path.join(self.tmp, "fail-public-activity")) + ")) throw new RuntimeException('fixture');\n"
            "}\nrequire " + php_literal(os.path.join(ROOT, "bin", "router.php")) + ";\n")
        self.proc = subprocess.Popen(
            [PHP_BIN, "-d", "error_log=" + os.path.join(self.tmp, "php-error.log"),
             # Tests rewrite config.php between requests; host OPcache settings
             # must not delay those fixture changes in persistent PHP workers.
             "-d", "opcache.enable=0", "-d", "opcache.enable_cli=0",
             "-S", "127.0.0.1:%d" % self.port, "-t", os.path.join(ROOT, "public"),
             router],
            env=env, stdout=subprocess.DEVNULL, stderr=self.server_log, start_new_session=(os.name == "posix"))
        self._wait()

    def write_config(self, **overrides):
        values = {
            "state_dir": self.state,
            "public_base_url": "https://dunelegacy.com/p2p",
            "allowed_origins": [ORIGIN, "https://www.dunelegacy.com"],
            "ice_servers": ["stun:stun.l.google.com:19302"],
            "allow_plaintext_loopback": True,
            "base_path": "",
            "app": "dunecity",
            "required_game_protocol": 0,
        }
        values.update(overrides)
        with open(self.config, "w") as handle:
            handle.write("<?php return " + php_literal(values) + ";\n")

    @staticmethod
    def _free_port():
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            return sock.getsockname()[1]

    def _wait(self):
        deadline = time.time() + 15
        while time.time() < deadline:
            try:
                with socket.create_connection(("127.0.0.1", self.port), 0.2):
                    return
            except OSError:
                if self.proc.poll() is not None:
                    raise RuntimeError("php server exited: "
                                       + Path(self.server_log.name).read_text(errors="replace"))
                time.sleep(0.05)
        raise RuntimeError("php server did not start")

    def request(self, method, path, form=None, headers=None, raw=None, origin=ORIGIN):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=30)
        body = raw
        if body is None and form is not None:
            body = urllib.parse.urlencode(form)
        sent = {}
        if body is not None:
            sent["Content-Type"] = "application/x-www-form-urlencoded"
        if origin is not None:
            sent["Origin"] = origin
        sent.update(headers or {})
        conn.request(method, path, body=body, headers=sent)
        raw_response = conn.getresponse()
        data = raw_response.read().decode("utf-8", "replace")
        response = Response(raw_response.status, dict(raw_response.getheaders()), data)
        conn.close()
        return response

    def stop_server(self):
        # php -S forks workers. Stopping only the parent leaves live listeners and does not
        # simulate a signaling outage. Isolate and terminate the whole fixture process group.
        if os.name == "posix":
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        elif self.proc.poll() is None:
            self.proc.terminate()
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            if os.name == "posix":
                os.killpg(self.proc.pid, signal.SIGKILL)
            else:
                self.proc.kill()
            self.proc.wait(timeout=5)

    def stop(self):
        self.stop_server()
        self.server_log.close()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def worker_count(self):
        """How many worker processes the dev server actually forked.

        The concurrency tests below are only evidence if there is more than one: a single-process
        `php -S` serialises every request and would make a broken lock look correct.
        """
        try:
            found = subprocess.run(["pgrep", "-P", str(self.proc.pid)],
                                   capture_output=True, timeout=10)
        except (OSError, subprocess.SubprocessError):
            return None
        children = [line for line in found.stdout.decode().split("\n") if line.strip()]
        return len(children) if children else None

    def state_file(self, name):
        path = os.path.join(self.state, name)
        if not os.path.exists(path):
            return None
        with open(path) as handle:
            return handle.read()


def claims(**overrides):
    form = {
        "app": "dunecity",
        "appVersion": APP_VERSION,
        "gameProtocol": GAME_PROTOCOL,
        "contentHash": CONTENT_HASH,
        "runtime": "native",
    }
    form.update(overrides)
    return form


CLAIM_KEYS = ("app", "appVersion", "gameProtocol", "contentHash", "runtime")


class SignalingTestCase(unittest.TestCase):
    service = None

    @classmethod
    def setUpClass(cls):
        if PHP_BIN is None:
            raise unittest.SkipTest("no PHP binary; set PHP_BIN")
        cls.service = ServiceFixture()

    @classmethod
    def tearDownClass(cls):
        if cls.service is not None:
            cls.service.stop()

    def setUp(self):
        # Every test starts from an empty state directory, so a leftover room, an already-spent
        # rate window or another test's grant cannot be what makes one pass.
        for name in os.listdir(self.service.state):
            path = os.path.join(self.service.state, name)
            if os.path.isdir(path) and not os.path.islink(path):
                shutil.rmtree(path, ignore_errors=True)
            else:
                os.unlink(path)

    # -- helpers ---------------------------------------------------------------------------

    def host(self, **overrides):
        form = claims(**{k: v for k, v in overrides.items() if k in CLAIM_KEYS})
        form.update({"maxPeers": overrides.get("maxPeers", 4),
                     "mode": overrides.get("mode", "custom"),
                     "visibility": overrides.get("visibility", "private")})
        response = self.service.request("POST", "/v1/admission/host", form)
        self.assertEqual(200, response.status, response.body)
        self.assertEqual("ok", response.fields["status"])
        return response

    def join(self, room, **overrides):
        form = claims(**{k: v for k, v in overrides.items() if k in CLAIM_KEYS})
        form.update({"room": room, "publicOnly": overrides.get("publicOnly", "0")})
        return self.service.request("POST", "/v1/admission/join", form)

    def session(self, grant, name, runtime="native", **overrides):
        form = claims(runtime=runtime, **{k: v for k, v in overrides.items() if k in CLAIM_KEYS})
        form.update({"grant": grant, "name": name.encode().hex()})
        return self.service.request("POST", "/v1/p2p/session", form)

    def poll(self, token, cursor=0):
        return self.service.request("POST", "/v1/p2p/poll", {"cursor": cursor},
                                    headers={"X-Dune-Session": token})

    def signal(self, token, to, kind, payload):
        return self.service.request("POST", "/v1/p2p/signal",
                                    {"to": to, "kind": kind, "data": payload.encode().hex()},
                                    headers={"X-Dune-Session": token})

    def phase(self, token, phase):
        state = json.loads((Path(self.service.state)/"rooms"/(token[:8]+".json")).read_text())
        roster = ",".join(str(i) for i in sorted(map(int,state['peers'])))
        return self.service.request("POST", "/v1/p2p/phase", {"phase": phase, "roster": roster},
                                    headers={"X-Dune-Session": token})

    def seat(self, name="Host", **kw):
        """A hosted room with the host already seated. Returns (admission, session)."""
        admission = self.host(**kw)
        session = self.session(admission.fields["grant"], name)
        self.assertEqual(200, session.status, session.body)
        return admission, session

    def seat_guest(self, admission, name="Guest", **kw):
        grant = self.join(admission.fields["room"], **kw)
        self.assertEqual(200, grant.status, grant.body)
        session = self.session(grant.fields["grant"], name, runtime=kw.get("runtime", "native"))
        self.assertEqual(200, session.status, session.body)
        return session

    def attestations(self, response):
        """`fp=<from>|<to>|sha-256|<fingerprint>`, checked against the exact client contract:
        four fields, a recipient that is this poller, sha-256 only, and 95 characters of
        colon-separated uppercase hex. P2PSignal::parsePollResponse fails the whole snapshot on
        any of these, so a server that gets one wrong takes every poll down with it."""
        out = {}
        for value in response.multi.get("fp", []):
            fields = value.split("|")
            self.assertEqual(4, len(fields), value)
            self.assertRegex(fields[0], r"^[1-9][0-9]{0,4}$")
            self.assertRegex(fields[1], r"^[1-9][0-9]{0,4}$")
            self.assertNotEqual(fields[0], fields[1])
            self.assertEqual("sha-256", fields[2])
            self.assertEqual(95, len(fields[3]))
            self.assertRegex(fields[3], r"^(?:[0-9A-F]{2}:){31}[0-9A-F]{2}$")
            out[fields[0]] = fields[3]
        return out

    def room_state(self, token):
        return json.loads(self.service.state_file("rooms/%s.json" % token[:8]))

    def write_room_state(self, token, state):
        path = os.path.join(self.service.state, "rooms", "%s.json" % token[:8])
        with open(path, "w") as handle:
            json.dump(state, handle)


# ------------------------------------------------------------------------------------------
class NotificationTests(SignalingTestCase):
    def test_lobby_notifications_follow_committed_host_and_start_only(self):
        for visibility in ["public", "private"]:
            for mode in ["custom", "coop"]:
                Path(self.service.notification_log).write_text("")
                admission = self.host(visibility=visibility, mode=mode)
                self.assertEqual("", Path(self.service.notification_log).read_text())
                form = claims(grant=admission.fields["grant"], name="Host".encode().hex(), nonce="b"*32)
                host = self.service.request("POST", "/v1/p2p/session", form)
                self.assertEqual(200, host.status)
                self.assertEqual(200, self.service.request("POST", "/v1/p2p/session", form).status)
                guest = self.seat_guest(admission)
                self.assertEqual(403, self.phase(guest.fields["session"], "match").status)
                self.assertEqual(200, self.phase(host.fields["session"], "match").status)
                self.assertEqual(200, self.phase(host.fields["session"], "match").status)
                events = [json.loads(x) for x in Path(self.service.notification_log).read_text().splitlines()]
                self.assertEqual(["hosted", "started"], [x[0] for x in events])
                self.assertEqual(visibility, events[0][1]["visibility"])
                self.assertEqual(mode, events[0][1]["mode"])
                self.assertEqual(1, events[0][1]["players"])
                self.assertEqual(2, events[1][1]["players"])
                self.assertEqual({"room_log_id", "mode", "visibility", "host", "version", "players", "max_players", "player_names", "spectator_names"}, set(events[0][1]))
                self.assertEqual(['Host'], events[0][1]['player_names'])
                self.assertEqual(['Host', 'Guest'], events[1][1]['player_names'])
                self.assertEqual([], events[1][1]['spectator_names'])
                self.assertNotIn(admission.fields["room"], json.dumps(events))
                self.assertNotIn(host.fields["session"], json.dumps(events))
                for response in [host, self.poll(host.fields["session"])]:
                    self.assertNotIn("notification", response.body)

    def test_notification_failure_cannot_break_hosting_or_start(self):
        marker = Path(self.service.tmp)/"fail-notifications"
        marker.touch()
        try:
            admission, host = self.seat()
            self.seat_guest(admission)
            self.assertEqual(200, self.phase(host.fields["session"], "match").status)
        finally:
            marker.unlink()


class AdmissionTests(SignalingTestCase):
    def test_host_answer_matches_the_shipped_parser(self):
        response = self.host()
        self.assertEqual(str(PROTOCOL), response.fields["protocol"])
        self.assertRegex(response.fields["room"],
                         r"^[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}$")
        self.assertRegex(response.fields["grant"], r"^[0-9a-f]{64}$")
        self.assertRegex(response.fields["control"], r"^[0-9a-f]{64}$")
        self.assertEqual("4", response.fields["maxPeers"])
        self.assertEqual("private", response.fields["visibility"])
        # An https url ending in /v1/p2p, never a ws:// or wss:// gameplay endpoint.
        self.assertEqual("https://dunelegacy.com/p2p/v1/p2p", response.fields["url"])
        self.assertEqual("https://dunelegacy.com/p2p", response.fields["signaling"])
        self.assertNotIn("ws://", response.body)
        self.assertNotIn("wss://", response.body)

    def test_admission_envelope_stays_inside_the_legacy_bounds(self):
        response = self.host()
        self.assertLessEqual(len(response.body.encode()), 8192)
        self.assertLessEqual(len(response.lines), 16)
        for line in response.lines:
            self.assertLessEqual(len(line.encode()), 512)
            self.assertLessEqual(len(line.partition("=")[2].encode()), 480)

    def test_responses_are_never_cached(self):
        response = self.host()
        self.assertEqual("no-store", response.headers["Cache-Control"])
        self.assertEqual("nosniff", response.headers["X-Content-Type-Options"])

    def test_private_room_is_not_listed_and_public_one_is(self):
        private = self.host(visibility="private")
        self.session(private.fields["grant"], "Quiet")
        listing = self.service.request("POST", "/v1/admission/list", claims())
        self.assertEqual("0", listing.fields["next"])
        self.assertNotIn(private.fields["room"], listing.body)

        public = self.host(visibility="public")
        self.session(public.fields["grant"], "Loud")
        listing = self.service.request("POST", "/v1/admission/list", claims())
        self.assertIn("game", listing.fields)
        entry = listing.multi["game"][0].split("|")
        self.assertEqual(public.fields["room"], entry[0])
        self.assertEqual("1", entry[1])
        self.assertEqual("4", entry[2])
        self.assertEqual("custom", entry[3])
        self.assertEqual("Loud", bytes.fromhex(entry[4]).decode())
        self.assertNotIn(private.fields["room"], listing.body)

    def test_a_room_with_no_seated_host_is_not_listed(self):
        public = self.host(visibility="public")
        listing = self.service.request("POST", "/v1/admission/list", claims())
        self.assertNotIn(public.fields["room"], listing.body)

    def test_listing_only_shows_matching_protocol_and_content(self):
        public = self.host(visibility="public")
        self.session(public.fields["grant"], "Loud")
        other = self.service.request("POST", "/v1/admission/list", claims(contentHash="b" * 64))
        self.assertNotIn(public.fields["room"], other.body)

    def test_directory_never_offers_an_incompatible_executable_version(self):
        public, host = self.seat("Loud", visibility="public")
        for options in ({}, {"allMods": 1}, {"allMods": 1, "details": 1}):
            listing = self.service.request("POST", "/v1/admission/list",
                                           claims(appVersion="9.9.9", **options))
            self.assertEqual(200, listing.status, listing.body)
            self.assertNotIn(public.fields["room"], listing.body)
            matching = self.service.request("POST", "/v1/admission/list", claims(**options))
            self.assertIn(public.fields["room"], matching.body)
        refused = self.join(public.fields["room"], appVersion="9.9.9", publicOnly="1")
        self.assertEqual("version_mismatch", refused.fields["code"])
        self.assertEqual(["Loud"], [peer["name"] for peer in self.room_state(host.fields["session"])["peers"].values()])

    def test_directory_version_comes_from_room_not_an_old_index_cache(self):
        public, host = self.seat("Loud", visibility="public")
        index_path = Path(self.service.state) / "index.json"
        for cached_version in (None, "9.9.9"):
            index = json.loads(index_path.read_text())
            cached = index["rooms"][host.fields["session"][:8]]
            if cached_version is None:
                cached.pop("appVersion", None)
            else:
                cached["appVersion"] = cached_version
            index_path.write_text(json.dumps(index))
            for refresh_host in (False, True):
                if refresh_host:
                    self.assertEqual(200, self.poll(host.fields["session"]).status)
                matching = self.service.request("POST", "/v1/admission/list", claims(details=1))
                self.assertEqual(200, matching.status, matching.body)
                self.assertIn(public.fields["room"], matching.body)
                other = self.service.request("POST", "/v1/admission/list",
                                             claims(appVersion="9.9.9", allMods=1, details=1))
                self.assertEqual(200, other.status, other.body)
                self.assertNotIn(public.fields["room"], other.body)

    def test_directory_answer_fits_the_legacy_parser(self):
        for index in range(6):
            public = self.host(visibility="public")
            self.session(public.fields["grant"], "Player" + str(index))
        listing = self.service.request("POST", "/v1/admission/list", claims())
        self.assertLessEqual(len(listing.body.encode()), 8192)
        self.assertLessEqual(len(listing.lines), 16)
        self.assertLessEqual(len(listing.multi["game"]), 12)
        for line in listing.lines:
            self.assertLessEqual(len(line.encode()), 512)
            self.assertLessEqual(len(line.partition("=")[2].encode()), 480)

    def test_public_only_join_refuses_a_private_room(self):
        admission = self.host(visibility="private")
        refused = self.join(admission.fields["room"], publicOnly="1")
        self.assertEqual(404, refused.status)
        self.assertEqual("room_not_found", refused.fields["code"])

    def test_version_number_mismatch_is_refused_before_reserving_a_seat(self):
        admission = self.host(visibility="public", maxPeers=2)
        for public_only in ("0", "1"):
            refused = self.join(admission.fields["room"], appVersion="9.9.9", publicOnly=public_only)
            self.assertEqual(409, refused.status)
            self.assertEqual("version_mismatch", refused.fields["code"])
            self.assertIn(APP_VERSION, refused.fields["message"])
            self.assertIn("9.9.9", refused.fields["message"])
        self.assertEqual(200, self.join(admission.fields["room"]).status)

    def test_content_mismatch_is_refused_at_join(self):
        admission = self.host()
        refused = self.join(admission.fields["room"], contentHash="b" * 64)
        self.assertEqual(409, refused.status)
        self.assertEqual("content_mismatch", refused.fields["code"])

    def test_room_fills_up(self):
        admission = self.host(maxPeers=2)
        self.session(admission.fields["grant"], "Host")
        self.seat_guest(admission, "Guest")
        refused = self.join(admission.fields["room"])
        self.assertEqual(409, refused.status)
        self.assertEqual("room_full", refused.fields["code"])

    def test_an_outstanding_grant_holds_its_seat(self):
        admission = self.host(maxPeers=2)
        self.session(admission.fields["grant"], "Host")
        self.assertEqual(200, self.join(admission.fields["room"]).status)
        refused = self.join(admission.fields["room"])
        self.assertEqual(409, refused.status)
        self.assertEqual("room_full", refused.fields["code"])

    def test_coop_is_always_two_players(self):
        admission = self.host(mode="coop", maxPeers=8)
        self.assertEqual("2", admission.fields["maxPeers"])

    def test_unknown_room_code(self):
        self.assertEqual(404, self.join("ZZZZ-ZZZZ-ZZZZ").status)
        self.assertEqual(400, self.join("not-a-code").status)


class GrantTests(SignalingTestCase):
    def test_a_grant_is_single_use(self):
        admission = self.host()
        self.assertEqual(200, self.session(admission.fields["grant"], "Host").status)
        replay = self.session(admission.fields["grant"], "Host")
        self.assertEqual(401, replay.status)
        self.assertEqual("unauthorized", replay.fields["code"])

    def test_a_grant_is_bound_to_its_compatibility_claims(self):
        admission = self.host()
        wrong = self.session(admission.fields["grant"], "Host", contentHash="b" * 64)
        self.assertEqual(409, wrong.status)
        self.assertEqual("content_mismatch", wrong.fields["code"])
        # ... and burned, so the correct claims cannot retry it.
        self.assertEqual(401, self.session(admission.fields["grant"], "Host").status)

    def test_runtime_claim_must_match_too(self):
        admission = self.host()
        self.assertEqual(409, self.session(admission.fields["grant"], "Host",
                                           runtime="browser").status)

    def test_appversion_claim_must_match(self):
        admission = self.host()
        self.assertEqual(409, self.session(admission.fields["grant"], "Host",
                                           appVersion="9.9.9").status)

    def test_an_invented_grant_is_refused(self):
        self.assertEqual(401, self.session("f" * 64, "Nobody").status)

    def test_session_answer_matches_the_signaling_parser(self):
        _, session = self.seat()
        self.assertEqual("ok", session.fields["status"])
        self.assertEqual(str(PROTOCOL), session.fields["protocol"])
        self.assertRegex(session.fields["session"], r"^[0-9a-f]{64}$")
        self.assertEqual("host", session.fields["role"])
        self.assertEqual("lobby", session.fields["phase"])
        self.assertNotEqual("0", session.fields["peer"])
        for url in session.multi["ice"]:
            self.assertTrue(url.startswith("stun:") or url.startswith("stuns:"), url)
        self.assertNotIn("turn:", session.body)

    def test_a_grant_issued_before_a_start_cannot_be_spent_after_it(self):
        admission = self.host()
        host = self.session(admission.fields["grant"], "Host")
        late = self.join(admission.fields["room"])
        self.assertEqual(200, late.status)
        self.assertEqual(200, self.phase(host.fields["session"], "match").status)
        spent = self.session(late.fields["grant"], "Latecomer")
        self.assertIn(spent.status, (401, 409))
        self.assertNotEqual("ok", spent.fields["status"])

    def test_no_new_admission_after_a_match_started(self):
        admission, host = self.seat()
        self.phase(host.fields["session"], "match")
        refused = self.join(admission.fields["room"])
        self.assertEqual(409, refused.status)
        self.assertEqual("match_in_progress", refused.fields["code"])

    def test_an_expired_grant_is_refused(self):
        admission = self.host()
        index_path = os.path.join(self.service.state, "rooms", admission.fields["grant"][:8] + ".json")
        with open(index_path) as handle:
            index = json.load(handle)
        for grant in index["grants"].values():
            grant["expiresAt"] -= 60000
        with open(index_path, "w") as handle:
            json.dump(index, handle)
        self.assertEqual(401, self.session(admission.fields["grant"], "Host").status)


class AtomicAdmissionTests(SignalingTestCase):
    def test_derived_cache_failure_does_not_hide_a_committed_session(self):
        admission, host=self.seat('Host');grant=self.join(admission.fields['room']).fields['grant']
        index=Path(self.service.state)/'index.json';original=index.read_bytes()
        index.write_text('{corrupt-cache')
        try:
            answer=self.session(grant,'Guest');self.assertEqual(200,answer.status)
            self.assertEqual(200,self.poll(answer.fields['session']).status)
        finally:index.write_bytes(original)


    def test_start_compares_authoritative_roster_and_revokes_outstanding_grants(self):
        admission, host = self.seat("Host")
        stale_roster = host.fields['peer']
        guest = self.seat_guest(admission)
        outstanding = self.join(admission.fields['room'])
        refused = self.service.request('POST','/v1/p2p/phase',
            {'phase':'match','roster':stale_roster}, headers={'X-Dune-Session':host.fields['session']})
        self.assertEqual(409,refused.status)
        started=self.phase(host.fields['session'],'match')
        self.assertEqual(200,started.status)
        self.assertEqual(32,len(started.fields['startId']))
        self.assertEqual(started.fields['startId'],self.phase(host.fields['session'],'match').fields['startId'])
        self.assertNotEqual(200,self.session(outstanding.fields['grant'],'Late').status)

    def test_session_nonce_recovers_committed_response_without_an_extra_seat(self):
        admission, host=self.seat('Host')
        grant=self.join(admission.fields['room']).fields['grant']
        form=dict(claims(),grant=grant,name='Guest'.encode().hex(),nonce='a'*32)
        first=self.service.request('POST','/v1/p2p/session',form)
        retry=self.service.request('POST','/v1/p2p/session',form)
        self.assertEqual(200,first.status);self.assertEqual(first.fields['session'],retry.fields['session'])
        self.assertEqual(2,len(json.loads(Path(self.room_path(admission)).read_text())['peers']))
        wrong=self.service.request('POST','/v1/p2p/session',dict(form,nonce='b'*32))
        self.assertEqual(401,wrong.status)

    def test_expired_host_closes_lobby_even_when_guest_is_still_polling(self):
        admission, host=self.seat('Host',visibility='public')
        guest=self.seat_guest(admission)
        path=Path(self.room_path(admission));state=json.loads(path.read_text())
        state['peers'][host.fields['peer']]['lastSeen']-=3600000;path.write_text(json.dumps(state))
        polled=self.poll(guest.fields['session']);self.assertEqual('1000',polled.fields['closed'])
        self.assertEqual(404,self.join(admission.fields['room']).status)

    def room_path(self, admission):
        return os.path.join(self.service.state, "rooms", admission.fields["grant"][:8] + ".json")

    def test_duplicate_name_redemption_releases_its_reservation(self):
        admission, host = self.seat("Host", maxPeers=2)
        grant = self.join(admission.fields["room"])
        self.assertEqual(409, self.session(grant.fields["grant"], "Host").status)
        room = json.loads(Path(self.room_path(admission)).read_text())
        self.assertEqual(1, len(room["peers"]))
        self.assertEqual(0, len(room["grants"]))
        guest = self.seat_guest(admission, "Guest")
        self.assertEqual(200, guest.status)

    def test_failed_room_write_does_not_consume_a_grant_or_add_a_seat(self):
        admission, host = self.seat("Host", maxPeers=2)
        grant = self.join(admission.fields["room"])
        room_path = self.room_path(admission)
        before = Path(room_path).read_bytes()
        directory = os.path.dirname(room_path)
        os.chmod(directory, 0o500)
        try:
            self.assertEqual(503, self.session(grant.fields["grant"], "Guest").status)
        finally:
            os.chmod(directory, 0o700)
        self.assertEqual(before, Path(room_path).read_bytes())
        self.assertEqual(200, self.session(grant.fields["grant"], "Guest").status)
        self.assertEqual(401, self.session(grant.fields["grant"], "Guest again").status)

    def test_stale_directory_cannot_authorize_a_started_room(self):
        admission, host = self.seat("Host", visibility="public")
        index_path = Path(self.service.state) / "index.json"
        stale = index_path.read_bytes()
        self.phase(host.fields["session"], "match")
        index_path.write_bytes(stale)  # Simulate a crash before the cache update committed.
        self.assertEqual(409, self.join(admission.fields["room"]).status)
        listed = self.service.request("POST", "/v1/admission/list", claims())
        self.assertEqual([], listed.multi.get("game", []))

    def test_stale_directory_cannot_publish_a_room_made_private(self):
        admission, host = self.seat("Host", visibility="public")
        index_path = Path(self.service.state) / "index.json"
        stale = index_path.read_bytes()
        changed = self.service.request("POST", "/v1/admission/visibility", dict(claims(),
            room=admission.fields["room"], control=admission.fields["control"], visibility="private"))
        self.assertEqual(200, changed.status)
        index_path.write_bytes(stale)
        listed = self.service.request("POST", "/v1/admission/list", claims())
        self.assertEqual([], listed.multi.get("game", []))
        self.assertEqual(404, self.join(admission.fields["room"]).status)

    def test_expired_host_cannot_start_or_leave_and_resurrect_its_seat(self):
        for operation in ("phase", "leave"):
            with self.subTest(operation=operation):
                self.setUp()
                admission, host = self.seat("Host")
                path = Path(self.room_path(admission))
                room = json.loads(path.read_text())
                for peer in room["peers"].values(): peer["lastSeen"] -= 3600000
                path.write_text(json.dumps(room))
                answer = self.service.request("POST", "/v1/p2p/" + operation,
                    {"phase": "match", "roster": "1"} if operation == "phase" else {"bye": "1"},
                    headers={"X-Dune-Session": host.fields["session"]})
                self.assertEqual(403, answer.status)
                polled = self.poll(host.fields["session"])
                self.assertEqual(403, polled.status)


class StoreIntegrityTests(SignalingTestCase):
    """The state files hold the grants, the seat accounting and the rate counters. Every failure
    mode here has to end with that state either correct or untouched - never quietly reset."""

    def corrupt(self, name, contents):
        """Replaces a state file's contents, keeping the 0600 the service requires - so that a
        test which expects a 503 is getting it for the reason it says, not for the file mode."""
        path = os.path.join(self.service.state, name)
        with open(path, "wb") as handle:
            handle.write(contents)
        os.chmod(path, 0o600)
        return path

    def test_a_group_readable_state_file_is_refused(self):
        self.host()
        path = os.path.join(self.service.state, "index.json")
        os.chmod(path, 0o640)
        try:
            response = self.service.request("POST", "/v1/admission/host",
                                            dict(claims(), maxPeers=2))
            self.assertEqual(503, response.status)
        finally:
            os.chmod(path, 0o600)
        self.assertEqual(200, self.service.request(
            "POST", "/v1/admission/host", dict(claims(), maxPeers=2)).status)

    def test_corrupt_directory_state_fails_closed_and_is_not_reset(self):
        """Truncating index.json to garbage must not look like an empty service: that would drop
        every room, every outstanding grant and the seat accounting they hold."""
        admission = self.host()
        before = self.service.state_file("index.json")
        self.assertIn(admission.fields["room"], before)

        path = self.corrupt("index.json", b"{ this is not json")
        response = self.service.request("POST", "/v1/admission/host", dict(claims(), maxPeers=2))
        self.assertEqual(503, response.status, response.body)
        self.assertEqual("unavailable", response.fields["code"])
        # Untouched: the service refused rather than writing a fresh empty directory over it.
        with open(path, "rb") as handle:
            self.assertEqual(b"{ this is not json", handle.read())

        # Put the real state back; the room and its grant are still exactly as they were.
        with open(path, "w") as handle:
            handle.write(before)
        self.assertEqual(200, self.join(admission.fields["room"]).status)

    def test_corrupt_rate_state_fails_closed_rather_than_clearing_the_counters(self):
        self.host()
        self.corrupt("rate.json", b"\x00\x01 not json at all")
        response = self.service.request("POST", "/v1/admission/host", dict(claims(), maxPeers=2))
        self.assertEqual(503, response.status)
        self.assertEqual("unavailable", response.fields["code"])

    def test_a_corrupt_room_file_fails_closed(self):
        _, host = self.seat()
        self.corrupt("rooms/%s.json" % host.fields["session"][:8], b"[[[")
        response = self.poll(host.fields["session"])
        self.assertEqual(503, response.status)

    def test_an_oversized_state_file_fails_closed(self):
        self.host()
        path = self.corrupt("index.json", b"{}" + b" " * (5 * 1024 * 1024))
        response = self.service.request("POST", "/v1/admission/host", dict(claims(), maxPeers=2))
        self.assertEqual(503, response.status)
        self.assertEqual(2 + 5 * 1024 * 1024, os.path.getsize(path))

    def test_a_zero_length_state_file_initialises(self):
        """The one thing that may be treated as fresh: a file that has never been written."""
        self.corrupt("index.json", b"")
        self.assertEqual(200, self.service.request(
            "POST", "/v1/admission/host", dict(claims(), maxPeers=2)).status)

    def test_json_that_is_not_an_object_fails_closed(self):
        self.host()
        for garbage in (b'"a string"', b"42", b"null", b"true"):
            self.corrupt("index.json", garbage)
            self.assertEqual(503, self.service.request(
                "POST", "/v1/admission/host", dict(claims(), maxPeers=2)).status, garbage)

    def test_a_symlinked_state_subdirectory_is_refused_not_followed(self):
        """is_dir() follows symlinks, so the link check has to come first. If it does not, a
        symlinked rooms/ directory is accepted as a real one and peer state is written through
        it - which is how a session token ends up in the web root."""
        outside = os.path.join(self.service.tmp, "elsewhere")
        os.mkdir(outside, 0o700)
        rooms = os.path.join(self.service.state, "rooms")
        shutil.rmtree(rooms, ignore_errors=True)
        os.symlink(outside, rooms)

        response = self.service.request("POST", "/v1/admission/host", dict(claims(), maxPeers=2))
        self.assertEqual(503, response.status, response.body)
        self.assertEqual([], os.listdir(outside))

    def test_a_symlinked_journal_is_refused_not_followed(self):
        target = os.path.join(self.service.tmp, "journal-target")
        with open(target, "w") as handle:
            handle.write("")
        os.symlink(target, os.path.join(self.service.state, "analytics.jsonl"))
        self.host()
        with open(target) as handle:
            self.assertEqual("", handle.read())

    def test_a_write_that_cannot_complete_keeps_the_previous_state(self):
        """A read-only state directory stands in for a full disk: the rename cannot happen, so
        the service refuses - and the grants that were already there survive."""
        admission = self.host()
        before = self.service.state_file("index.json")
        os.chmod(self.service.state, 0o500)
        try:
            response = self.service.request("POST", "/v1/admission/host",
                                            dict(claims(), maxPeers=2))
            self.assertEqual(503, response.status, response.body)
        finally:
            os.chmod(self.service.state, 0o700)
        self.assertEqual(before, self.service.state_file("index.json"))
        self.assertEqual(200, self.join(admission.fields["room"]).status)
        # No temp file was left lying around.
        leftovers = [name for name in os.listdir(self.service.state) if name.endswith(".tmp")]
        self.assertEqual([], leftovers)

    def test_state_is_replaced_by_rename_not_truncated_in_place(self):
        """The inode changes on every write. An in-place ftruncate would keep it - and would mean
        a reader without the lock could see a half-written, or empty, authorisation file."""
        self.host()
        path = os.path.join(self.service.state, "index.json")
        first = os.stat(path).st_ino
        self.host()
        second = os.stat(path).st_ino
        self.assertNotEqual(first, second)

    def test_the_lock_file_is_stable_private_and_a_regular_file(self):
        self.host()
        for name in ("index.lock", "rate.lock"):
            path = os.path.join(self.service.state, name)
            stat = os.stat(path)
            self.assertTrue(os.path.isfile(path), name)
            self.assertFalse(os.path.islink(path), name)
            self.assertEqual(0, stat.st_mode & 0o077, "%s is %s" % (name, oct(stat.st_mode)))
        # Stable across writes: the lock two workers hold must stay one inode.
        before = os.stat(os.path.join(self.service.state, "index.lock")).st_ino
        self.host()
        self.assertEqual(before, os.stat(os.path.join(self.service.state, "index.lock")).st_ino)

    def test_a_room_lock_survives_the_room_it_locked(self):
        """Unlinking a lock somebody may already be blocked on would give two workers a lock on
        two different inodes, so room locks come from a fixed pool and are never removed."""
        _, host = self.seat()
        room_id = host.fields["session"][:8]
        lock = os.path.join(self.service.state, "rooms", "lock-%s.lock" % room_id[:2])
        self.assertTrue(os.path.exists(lock))
        inode = os.stat(lock).st_ino
        self.service.request("POST", "/v1/p2p/leave", {"bye": "1"},
                             headers={"X-Dune-Session": host.fields["session"]})
        self.assertFalse(os.path.exists(
            os.path.join(self.service.state, "rooms", "%s.json" % room_id)))
        self.assertTrue(os.path.exists(lock))
        self.assertEqual(inode, os.stat(lock).st_ino)
        # Bounded: one lock per leading byte of the room id, not one per room.
        locks = [n for n in os.listdir(os.path.join(self.service.state, "rooms"))
                 if n.endswith(".lock")]
        self.assertLessEqual(len(locks), 256)

    def test_journals_are_private_from_their_first_line(self):
        self.host()
        for name in ("analytics.jsonl", "log.jsonl"):
            path = os.path.join(self.service.state, name)
            if os.path.exists(path):
                mode = os.stat(path).st_mode & 0o777
                self.assertEqual(0, mode & 0o077, "%s is %s" % (name, oct(mode)))

    def test_the_state_directory_is_created_private_inside_a_shared_parent(self):
        """The production bootstrap needs no administrator: the deploy account pre-creates a
        parent it can share with the web server group, and PHP creates the state directory itself,
        0700, owned by the worker. Nothing has to be chown'ed to www-data."""
        parent = os.path.realpath(tempfile.mkdtemp(prefix="dunecity-parent-"))
        os.chmod(parent, 0o2770)
        child = os.path.join(parent, "p2p-state")
        try:
            self.service.write_config(state_dir=child)
            response = self.service.request("POST", "/v1/admission/host",
                                            dict(claims(), maxPeers=2))
            self.assertEqual(200, response.status, response.body)
            self.assertTrue(os.path.isdir(child))
            self.assertEqual(0o700, os.stat(child).st_mode & 0o777)
        finally:
            self.service.write_config()
            shutil.rmtree(parent, ignore_errors=True)

    def test_a_world_writable_parent_is_refused(self):
        parent = os.path.realpath(tempfile.mkdtemp(prefix="dunecity-open-parent-"))
        os.chmod(parent, 0o777)
        child = os.path.join(parent, "p2p-state")
        try:
            self.service.write_config(state_dir=child)
            self.assertEqual(503, self.service.request(
                "POST", "/v1/admission/host", dict(claims(), maxPeers=2)).status)
            self.assertFalse(os.path.exists(child))
        finally:
            self.service.write_config()
            shutil.rmtree(parent, ignore_errors=True)

    def test_a_state_directory_reached_through_a_symlinked_parent_is_refused(self):
        real = os.path.realpath(tempfile.mkdtemp(prefix="dunecity-real-parent-"))
        link = os.path.join(self.service.tmp, "parent-link")
        os.symlink(real, link)
        try:
            self.service.write_config(state_dir=os.path.join(link, "p2p-state"))
            self.assertEqual(503, self.service.request(
                "POST", "/v1/admission/host", dict(claims(), maxPeers=2)).status)
        finally:
            self.service.write_config()
            shutil.rmtree(real, ignore_errors=True)

    def test_concurrent_writers_never_leave_the_file_unparseable(self):
        """A reader without the lock must always see one whole version. With an in-place
        truncate-then-write it would eventually see a prefix, or nothing."""
        import concurrent.futures
        import threading

        self.assertGreater(self.service.worker_count() or 1, 1,
                           "the dev server did not fork workers; concurrency is not proven")
        path = os.path.join(self.service.state, "index.json")
        self.host()
        stop = threading.Event()
        problems = []

        def watch():
            while not stop.is_set():
                try:
                    with open(path, "rb") as handle:
                        raw = handle.read()
                except FileNotFoundError:
                    problems.append("index.json vanished")
                    continue
                if not raw:
                    problems.append("index.json was empty")
                    continue
                try:
                    decoded = json.loads(raw)
                except ValueError:
                    problems.append("index.json was torn: %r" % raw[:64])
                    continue
                if not isinstance(decoded, dict) or "rooms" not in decoded:
                    problems.append("index.json lost its shape")

        watcher = threading.Thread(target=watch, daemon=True)
        watcher.start()
        try:
            with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
                futures = [pool.submit(self.service.request, "POST", "/v1/admission/host",
                                       dict(claims(), maxPeers=2)) for _ in range(6)]
                results = [future.result() for future in futures]
        finally:
            stop.set()
            watcher.join(timeout=5)
        self.assertEqual([], problems[:5])
        created = [r for r in results if r.status == 200]
        self.assertGreater(len(created), 1)
        # Every winner got its own room, and the directory still holds all of them.
        index = json.loads(self.service.state_file("index.json"))
        for response in created:
            self.assertIn(response.fields["room"], index["codes"])


class ConcurrencyTests(SignalingTestCase):
    def test_one_grant_survives_a_concurrent_redemption_race(self):
        """Two workers redeem the same grant at the same moment. Exactly one may win: the entry
        is removed under the directory lock before anything else happens, so the loser finds
        nothing even when it arrives in the same millisecond."""
        import concurrent.futures

        self.assertGreater(self.service.worker_count() or 1, 1,
                           "the dev server did not fork workers; a single-process php -S "
                           "serialises requests and would make a broken lock look correct")
        for attempt in range(5):
            admission = self.host()
            grant = admission.fields["grant"]
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                futures = [pool.submit(self.session, grant, "Racer%d" % index)
                           for index in range(4)]
                results = [future.result() for future in futures]
            accepted = [r for r in results if r.status == 200]
            self.assertEqual(1, len(accepted), [r.status for r in results])
            for refused in (r for r in results if r.status != 200):
                self.assertEqual(401, refused.status)

    def test_concurrent_signal_posts_all_get_distinct_sequences(self):
        """The signal sequence is allocated under the room lock, so two workers posting at once
        cannot be given the same number - which would make one record invisible to the client's
        strictly-increasing check."""
        import concurrent.futures

        admission, host = self.seat()
        guest = self.seat_guest(admission)
        guest_id = int(guest.fields["peer"])
        self.signal(host.fields["session"], guest_id, "offer", make_sdp(fingerprint(1)))
        payloads = [HOST_CANDIDATE.replace("54321", str(20000 + index)) for index in range(8)]
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            futures = [pool.submit(self.signal, host.fields["session"], guest_id,
                                   "candidate", payload) for payload in payloads]
            results = [future.result() for future in futures]
        self.assertTrue(all(r.status == 200 for r in results), [r.status for r in results])
        seen = self.poll(guest.fields["session"])
        sequences = [int(value.split("|")[1]) for value in seen.multi["sig"]]
        self.assertEqual(len(sequences), len(set(sequences)))
        self.assertEqual(sequences, sorted(sequences))

    def test_a_match_outlives_a_long_signaling_silence(self):
        """Well past the 60 seconds the first contract proposed: the room, the membership and
        the sessions are all still there, and nobody has been declared gone."""
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        self.phase(host.fields["session"], "match")
        state = self.room_state(host.fields["session"])
        for peer in state["peers"].values():
            peer["lastSeen"] -= 5 * 60000
        state["lastSeen"] -= 5 * 60000
        self.write_room_state(host.fields["session"], state)
        for token in (host.fields["session"], guest.fields["session"]):
            seen = self.poll(token)
            self.assertEqual(200, seen.status, seen.body)
            self.assertEqual("match", seen.fields["phase"])
            self.assertEqual(2, len(seen.multi["peer"]))
            self.assertEqual([], seen.multi.get("gone", []))


class SignalExchangeTests(SignalingTestCase):
    def test_offer_answer_and_candidate_reach_only_the_addressee(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        host_token, guest_token = host.fields["session"], guest.fields["session"]
        host_id, guest_id = int(host.fields["peer"]), int(guest.fields["peer"])

        posted = self.signal(host_token, guest_id, "offer", make_sdp(fingerprint(1)))
        self.assertEqual(200, posted.status, posted.body)

        seen = self.poll(guest_token)
        self.assertEqual(200, seen.status, seen.body)
        records = [value.split("|", 3) for value in seen.multi.get("sig", [])]
        self.assertEqual(1, len(records))
        self.assertEqual(str(host_id), records[0][0])
        self.assertEqual("offer", records[0][2])
        self.assertIn("a=fingerprint:sha-256", bytes.fromhex(records[0][3]).decode())
        # The sender does not receive its own record.
        self.assertEqual([], self.poll(host_token).multi.get("sig", []))

        answer = self.signal(guest_token, host_id, "answer", make_sdp(fingerprint(2), "answer"))
        self.assertEqual(200, answer.status, answer.body)
        candidate = self.signal(guest_token, host_id, "candidate", HOST_CANDIDATE)
        self.assertEqual(200, candidate.status, candidate.body)
        received = self.poll(host_token)
        kinds = [value.split("|")[2] for value in received.multi["sig"]]
        self.assertEqual(["answer", "candidate"], kinds)

    def test_the_poll_envelope_stays_inside_the_signaling_bounds(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        self.signal(host.fields["session"], int(guest.fields["peer"]), "offer",
                    make_sdp(fingerprint(1)))
        seen = self.poll(guest.fields["session"])
        self.assertLessEqual(len(seen.body.encode()), 524288)
        self.assertLessEqual(len(seen.lines), 256)
        for line in seen.lines:
            self.assertLessEqual(len(line.encode()), 131200)
            self.assertLessEqual(len(line.partition("=")[2].encode()), 131168)
            # The client's parser refuses anything that is not printable ASCII.
            self.assertTrue(all(32 <= ord(c) <= 126 for c in line), line[:80])

    def test_the_cursor_only_advances_through_delivered_records(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        guest_id = int(guest.fields["peer"])
        self.signal(host.fields["session"], guest_id, "offer", make_sdp(fingerprint(1)))
        first = self.poll(guest.fields["session"])
        cursor = int(first.fields["cursor"])
        self.assertEqual(1, len(first.multi["sig"]))
        self.assertEqual(int(first.multi["sig"][0].split("|")[1]), cursor)
        again = self.poll(guest.fields["session"], cursor)
        self.assertEqual([], again.multi.get("sig", []))
        self.assertEqual(cursor, int(again.fields["cursor"]))

    def test_signals_arrive_in_a_strictly_increasing_sequence(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        host_id, guest_id = int(host.fields["peer"]), int(guest.fields["peer"])
        self.signal(host.fields["session"], guest_id, "offer", make_sdp(fingerprint(1)))
        for index in range(5):
            payload = HOST_CANDIDATE.replace("54321", str(54321 + index))
            self.assertEqual(200, self.signal(host.fields["session"], guest_id,
                                              "candidate", payload).status)
        seen = self.poll(guest.fields["session"])
        sequences = [int(value.split("|")[1]) for value in seen.multi["sig"]]
        self.assertEqual(sequences, sorted(set(sequences)))
        self.assertLessEqual(sequences[-1], int(seen.fields["cursor"]))

    def test_a_retried_post_is_idempotent(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        guest_id = int(guest.fields["peer"])
        sdp = make_sdp(fingerprint(1))
        self.assertEqual(200, self.signal(host.fields["session"], guest_id, "offer", sdp).status)
        self.assertEqual(200, self.signal(host.fields["session"], guest_id, "offer", sdp).status)
        self.assertEqual(1, len(self.poll(guest.fields["session"]).multi["sig"]))

    def test_a_second_different_offer_on_one_pair_is_refused(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        guest_id = int(guest.fields["peer"])
        self.signal(host.fields["session"], guest_id, "offer", make_sdp(fingerprint(1)))
        again = self.signal(host.fields["session"], guest_id, "offer",
                            make_sdp(fingerprint(1), extra="a=ice-options:renomination\r\n"))
        self.assertEqual(409, again.status)
        self.assertEqual("bad_transition", again.fields["code"])

    def test_an_answer_without_an_offer_is_refused(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        refused = self.signal(guest.fields["session"], int(host.fields["peer"]), "answer",
                              make_sdp(fingerprint(2), "answer"))
        self.assertEqual(409, refused.status)
        self.assertEqual("bad_transition", refused.fields["code"])

    def test_a_candidate_before_any_description_is_refused(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        refused = self.signal(host.fields["session"], int(guest.fields["peer"]),
                              "candidate", HOST_CANDIDATE)
        self.assertEqual(409, refused.status)

    def test_the_per_pair_candidate_budget_is_enforced(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        guest_id = int(guest.fields["peer"])
        self.signal(host.fields["session"], guest_id, "offer", make_sdp(fingerprint(1)))
        refused = None
        for index in range(200):
            payload = HOST_CANDIDATE.replace("54321", str(10000 + index))
            response = self.signal(host.fields["session"], guest_id, "candidate", payload)
            if response.status != 200:
                refused = response
                break
        self.assertIsNotNone(refused, "the per-pair budget never fired")
        self.assertIn(refused.fields["code"], ("pair_budget", "room_budget"))

    def test_poll_reports_membership_and_departures(self):
        admission, host = self.seat("Hostess")
        guest = self.seat_guest(admission, "Guestly")
        seen = self.poll(host.fields["session"])
        names = sorted(bytes.fromhex(value.split("|")[2]).decode() for value in seen.multi["peer"])
        self.assertEqual(["Guestly", "Hostess"], names)
        roles = dict(value.split("|")[:2] for value in seen.multi["peer"])
        self.assertEqual("host", roles[host.fields["peer"]])
        self.assertEqual("client", roles[guest.fields["peer"]])

        self.service.request("POST", "/v1/p2p/leave", {"bye": "1"},
                             headers={"X-Dune-Session": guest.fields["session"]})
        after = self.poll(host.fields["session"])
        self.assertIn(guest.fields["peer"], after.multi.get("gone", []))
        self.assertEqual(1, len(after.multi["peer"]))

    def test_the_runtime_claim_is_carried_as_a_claim(self):
        admission, host = self.seat("Host")
        guest = self.seat_guest(admission, "Guest", runtime="browser")
        seen = self.poll(host.fields["session"])
        runtimes = {value.split("|")[0]: bytes.fromhex(value.split("|")[3]).decode()
                    for value in seen.multi["peer"]}
        self.assertEqual("browser", runtimes[guest.fields["peer"]])
        self.assertEqual("native", runtimes[host.fields["peer"]])


class FingerprintTests(SignalingTestCase):
    def test_three_players_bind_a_separate_fingerprint_per_link(self):
        """Each RTCPeerConnection has its own certificate, so one participant legitimately
        presents a different fingerprint on each of its links. The binding must be per pair."""
        admission, host = self.seat("Host", maxPeers=4)
        a = self.seat_guest(admission, "GuestA")
        b = self.seat_guest(admission, "GuestB")
        ids = {"host": int(host.fields["peer"]), "a": int(a.fields["peer"]),
               "b": int(b.fields["peer"])}
        tokens = {"host": host.fields["session"], "a": a.fields["session"],
                  "b": b.fields["session"]}

        prints = {}
        seed = 0
        for source in ("host", "a", "b"):
            for target in ("host", "a", "b"):
                if source != target:
                    seed += 1
                    prints[(source, target)] = fingerprint(seed)

        for source, target in (("host", "a"), ("host", "b"), ("a", "b")):
            posted = self.signal(tokens[source], ids[target], "offer",
                                 make_sdp(prints[(source, target)]))
            self.assertEqual(200, posted.status, posted.body)
            answered = self.signal(tokens[target], ids[source], "answer",
                                   make_sdp(prints[(target, source)], "answer"))
            self.assertEqual(200, answered.status, answered.body)

        # Every peer is told, for each counterparty, the fingerprint that counterparty used on
        # the link towards it - and nothing about anybody else's links.
        for who in ("host", "a", "b"):
            seen = self.poll(tokens[who])
            attested = self.attestations(seen)
            # Every attestation this peer is given is bound to a link that ends at this peer.
            for value in seen.multi["fp"]:
                self.assertEqual(str(ids[who]), value.split("|")[1], value)
            others = [other for other in ("host", "a", "b") if other != who]
            self.assertEqual(sorted(str(ids[other]) for other in others), sorted(attested))
            for other in others:
                self.assertEqual(prints[(other, who)], attested[str(ids[other])])
                # The same participant's fingerprint on its *other* link is a different value,
                # which is exactly what a per-participant binding would get wrong.
                for far in (o for o in others if o != other):
                    self.assertNotEqual(prints[(other, far)], attested[str(ids[other])])

    def test_a_changed_fingerprint_on_an_established_link_is_refused(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        guest_id = int(guest.fields["peer"])
        self.signal(host.fields["session"], guest_id, "offer", make_sdp(fingerprint(1)))
        impostor = self.signal(host.fields["session"], guest_id, "offer", make_sdp(fingerprint(9)))
        self.assertEqual(409, impostor.status)
        self.assertEqual("fingerprint_changed", impostor.fields["code"])

    def test_a_phase_change_cannot_launder_a_new_certificate(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        guest_id = int(guest.fields["peer"])
        self.signal(host.fields["session"], guest_id, "offer", make_sdp(fingerprint(1)))
        self.signal(guest.fields["session"], int(host.fields["peer"]), "answer",
                    make_sdp(fingerprint(2), "answer"))
        self.phase(host.fields["session"], "match")
        self.phase(host.fields["session"], "lobby")
        swapped = self.signal(host.fields["session"], guest_id, "offer", make_sdp(fingerprint(9)))
        self.assertEqual(409, swapped.status)
        self.assertEqual("fingerprint_changed", swapped.fields["code"])


class SdpAbuseTests(SignalingTestCase):
    def setUp(self):
        super().setUp()
        self.admission, self.host_session = self.seat()
        self.guest = self.seat_guest(self.admission)
        self.token = self.host_session.fields["session"]
        self.to = int(self.guest.fields["peer"])

    def refuse(self, payload, kind="offer"):
        response = self.signal(self.token, self.to, kind, payload)
        self.assertNotEqual(200, response.status, response.body)
        return response

    def test_a_relay_candidate_is_refused(self):
        self.assertEqual("relay_refused", self.refuse(RELAY_CANDIDATE, "candidate").fields["code"])

    def test_a_relay_candidate_embedded_in_a_description_is_refused(self):
        embedded = "a=" + RELAY_CANDIDATE.split("|", 1)[1] + "\r\n"
        response = self.refuse(make_sdp(fingerprint(1), extra=embedded))
        self.assertEqual("relay_refused", response.fields["code"])

    def test_a_turn_url_anywhere_is_refused(self):
        self.refuse(make_sdp(fingerprint(1), extra="a=x-turn:turn:evil.example.net\r\n"))

    def test_an_audio_section_is_refused(self):
        response = self.refuse(make_sdp(fingerprint(1)) + "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n")
        self.assertEqual("media_refused", response.fields["code"])

    def test_a_video_section_is_refused(self):
        response = self.refuse(make_sdp(fingerprint(1)) + "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n")
        self.assertEqual("media_refused", response.fields["code"])

    def test_two_fingerprints_are_refused(self):
        sdp = make_sdp(fingerprint(1), extra="a=fingerprint:sha-256 " + fingerprint(5) + "\r\n")
        self.assertEqual("fingerprint_refused", self.refuse(sdp).fields["code"])

    def test_a_repeated_identical_fingerprint_is_refused(self):
        sdp = make_sdp(fingerprint(1), extra="a=fingerprint:sha-256 " + fingerprint(1) + "\r\n")
        self.assertEqual(403, self.signal(self.token, self.to, "offer", sdp).status)

    def test_a_non_sha256_fingerprint_is_refused(self):
        sdp = make_sdp(fingerprint(1)).replace("sha-256", "sha-1")
        self.assertEqual("fingerprint_refused", self.refuse(sdp).fields["code"])

    def test_a_truncated_fingerprint_is_refused(self):
        sdp = make_sdp(fingerprint(1)[:20])
        self.assertEqual("fingerprint_refused", self.refuse(sdp).fields["code"])

    def test_a_missing_fingerprint_is_refused(self):
        sdp = "\r\n".join(line for line in make_sdp(fingerprint(1)).split("\r\n")
                          if not line.startswith("a=fingerprint")) + "\r\n"
        self.refuse(sdp)

    def test_a_control_character_is_refused(self):
        self.refuse(make_sdp(fingerprint(1)) + "a=evil:" + chr(1) + "\r\n")

    def test_a_description_without_a_media_section_is_refused(self):
        sdp = "\r\n".join(line for line in make_sdp(fingerprint(1)).split("\r\n")
                          if not line.startswith("m=")) + "\r\n"
        self.refuse(sdp)

    def test_a_real_sized_offer_is_accepted(self):
        normal = make_sdp(fingerprint(1), extra="a=x-pad:" + "p" * 2000 + "\r\n")
        self.assertGreater(len(normal), 2048)
        self.assertEqual(200, self.signal(self.token, self.to, "offer", normal).status)

    def test_a_full_size_offer_is_accepted_and_one_byte_over_is_not(self):
        # Padded with many ordinary-length attribute lines, the way a real description with a
        # long candidate list grows - not one absurd line, which the per-line bound catches.
        exact = pad_sdp_to(make_sdp(fingerprint(3)), 65536)
        self.assertEqual(65536, len(exact))
        accepted = self.signal(self.guest.fields["session"],
                               int(self.host_session.fields["peer"]), "offer", exact)
        self.assertEqual(200, accepted.status, accepted.body[:200])

        over = exact[:-2] + "p\r\n"
        self.assertEqual(65537, len(over))
        response = self.signal(self.token, self.to, "offer", over)
        self.assertNotEqual(200, response.status)

    def test_an_oversized_body_is_refused_without_being_truncated(self):
        response = self.service.request(
            "POST", "/v1/p2p/signal", raw="to=1&kind=offer&data=" + "61" * 200000,
            headers={"X-Dune-Session": self.token})
        self.assertEqual(413, response.status)

    def test_an_oversized_admission_body_is_refused(self):
        response = self.service.request("POST", "/v1/admission/host", raw="app=" + "x" * 9000)
        self.assertEqual(413, response.status)

    def test_a_candidate_over_its_own_bound_is_refused(self):
        self.signal(self.token, self.to, "offer", make_sdp(fingerprint(1)))
        self.refuse(HOST_CANDIDATE + " generation " + "9" * 3000, "candidate")

    def test_a_malformed_candidate_is_refused(self):
        self.signal(self.token, self.to, "offer", make_sdp(fingerprint(1)))
        self.refuse("0|candidate:1 1 UDP 2122252543 192.168.1.5 54321 typ wat", "candidate")
        self.refuse("0|not a candidate at all", "candidate")
        self.refuse("candidate:1 1 UDP 2122252543 192.168.1.5 54321 typ host", "candidate")
        self.refuse("bad mid|" + HOST_CANDIDATE.split("|", 1)[1], "candidate")

    def test_a_candidate_with_an_empty_media_id_is_accepted(self):
        """A browser bundling a data-only description reports sdpMid as an empty string."""
        self.signal(self.token, self.to, "offer", make_sdp(fingerprint(1)))
        payload = "|" + HOST_CANDIDATE.split("|", 1)[1]
        self.assertEqual(200, self.signal(self.token, self.to, "candidate", payload).status)

    def test_srflx_and_prflx_are_accepted(self):
        self.signal(self.token, self.to, "offer", make_sdp(fingerprint(1)))
        srflx = ("0|candidate:2 1 UDP 1686052607 203.0.113.7 51000 typ srflx "
                 "raddr 192.168.1.5 rport 54321")
        self.assertEqual(200, self.signal(self.token, self.to, "candidate", srflx).status)
        prflx = "0|candidate:3 1 UDP 1686052606 203.0.113.8 51001 typ prflx"
        self.assertEqual(200, self.signal(self.token, self.to, "candidate", prflx).status)

    def test_an_mdns_candidate_is_accepted(self):
        self.signal(self.token, self.to, "offer", make_sdp(fingerprint(1)))
        mdns = ("0|candidate:1 1 UDP 2122252543 "
                "8d2e1c4b-1111-2222-3333-444455556666.local 54321 typ host")
        self.assertEqual(200, self.signal(self.token, self.to, "candidate", mdns).status)

    def test_non_hex_signal_data_is_refused(self):
        response = self.service.request(
            "POST", "/v1/p2p/signal", {"to": self.to, "kind": "offer", "data": "not-hex"},
            headers={"X-Dune-Session": self.token})
        self.assertEqual(400, response.status)

    def test_an_unknown_kind_is_refused(self):
        response = self.service.request(
            "POST", "/v1/p2p/signal", {"to": self.to, "kind": "gamepacket", "data": "6162"},
            headers={"X-Dune-Session": self.token})
        self.assertEqual(400, response.status)


class ParserTranscriptionTests(SignalingTestCase):
    """Drives a realistic three-player session and runs every single response through the
    transcription of the client's parser above. This is the check that catches a field-count or
    ordering change in one place instead of one test at a time - but see the caveat on the
    transcription: it is not the real parser, and only the native+PHP smoke harness will be."""

    def test_a_three_player_session_parses_end_to_end(self):
        admission = self.host(maxPeers=4, visibility="public")
        seats = {}
        for name, grant in (("Host", admission.fields["grant"]),):
            response = self.session(grant, name)
            self.assertEqual(200, response.status, response.body)
            check_session_response(response.body)
            seats[name] = response
        for name in ("GuestA", "GuestB"):
            # The grant binds the runtime claim, so the join has to make the same one.
            runtime = "browser" if name == "GuestB" else "native"
            grant = self.join(admission.fields["room"], runtime=runtime)
            self.assertEqual(200, grant.status, grant.body)
            response = self.session(grant.fields["grant"], name, runtime=runtime)
            self.assertEqual(200, response.status, response.body)
            check_session_response(response.body)
            seats[name] = response

        ids = {name: int(r.fields["peer"]) for name, r in seats.items()}
        tokens = {name: r.fields["session"] for name, r in seats.items()}

        prints = {}
        seed = 0
        for source in seats:
            for target in seats:
                if source != target:
                    seed += 1
                    prints[(source, target)] = fingerprint(seed)

        for source, target in (("Host", "GuestA"), ("Host", "GuestB"), ("GuestA", "GuestB")):
            self.assertEqual(200, self.signal(tokens[source], ids[target], "offer",
                                              make_sdp(prints[(source, target)])).status)
            self.assertEqual(200, self.signal(tokens[target], ids[source], "answer",
                                              make_sdp(prints[(target, source)], "answer")).status)
            for _, payload in REAL_CANDIDATES[:4]:
                self.assertEqual(200, self.signal(tokens[source], ids[target],
                                                  "candidate", payload).status)

        cursors = {name: 0 for name in seats}
        for name in seats:
            for _ in range(3):
                response = self.poll(tokens[name], cursors[name])
                check_poll_response(response.body, ids[name])
                cursors[name] = int(response.fields["cursor"])

        self.assertEqual(200, self.phase(tokens["Host"], "match").status)
        for name in seats:
            check_poll_response(self.poll(tokens[name], cursors[name]).body, ids[name])

        # Departures, and the snapshot that describes them.
        self.service.request("POST", "/v1/p2p/leave", {"bye": "1"},
                             headers={"X-Dune-Session": tokens["GuestB"]})
        check_poll_response(self.poll(tokens["Host"], 0).body, ids["Host"])

    def test_the_transcription_rejects_the_contract_violations_it_is_there_to_catch(self):
        """A checker that accepts everything would make the test above meaningless."""
        good = ("status=ok\nprotocol=1\nphase=lobby\n"
                "peer=1|host|486f7374|6e6174697665\n"
                "peer=2|client|4775657374|6e6174697665\n"
                "fp=1|2|sha-256|" + fingerprint(1) + "\ncursor=0\n")
        check_poll_response(good, 2)
        for name, broken in (
            ("three-field fp", good.replace("fp=1|2|sha-256|", "fp=1|sha-256|")),
            ("fp bound elsewhere", good.replace("fp=1|2|", "fp=1|3|")),
            ("repeated name", good.replace("peer=2|client|4775657374", "peer=2|client|486f7374")),
            ("repeated id", good.replace("peer=2|client", "peer=1|client")),
            ("two hosts", good.replace("peer=2|client", "peer=2|host")),
            ("present and departed", good.replace("cursor=0", "gone=1\ncursor=0")),
            ("sha-1", good.replace("sha-256", "sha-1")),
            ("short fingerprint", good.replace(fingerprint(1), fingerprint(1)[:80])),
            ("no cursor", good.replace("cursor=0\n", "")),
        ):
            with self.subTest(name):
                with self.assertRaises(ContractError):
                    check_poll_response(broken, 2)


class ClientContractTests(SignalingTestCase):
    """P2PSignal::parsePollResponse refuses a whole snapshot - not just the offending line - if
    any of these is wrong. Each of these is therefore a "every poll fails for everybody" bug
    rather than a degraded-feature bug."""

    def test_the_fingerprint_line_has_four_fields_with_the_recipient_named(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        host_id, guest_id = int(host.fields["peer"]), int(guest.fields["peer"])
        self.signal(host.fields["session"], guest_id, "offer", make_sdp(fingerprint(1)))
        seen = self.poll(guest.fields["session"])
        self.assertEqual(1, len(seen.multi["fp"]))
        fields = seen.multi["fp"][0].split("|")
        self.assertEqual([str(host_id), str(guest_id), "sha-256", fingerprint(1)], fields)

    def test_a_trailing_newline_never_satisfies_a_field_validator(self):
        """PCRE's `$` matches before a trailing newline, so `/^(offer|answer)$/` accepts
        "offer\\n". Every validator here is a whole-string check and carries the D modifier; a
        value that slipped through would reach the response builder and try to put a line break
        inside a `key=value` line."""
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        token, to = host.fields["session"], int(guest.fields["peer"])

        smuggled = self.service.request(
            "POST", "/v1/p2p/signal",
            raw="to=%d&kind=offer%%0A&data=%s" % (to, make_sdp(fingerprint(1)).encode().hex()),
            headers={"X-Dune-Session": token})
        self.assertEqual(400, smuggled.status, smuggled.body)

        phase = self.service.request("POST", "/v1/p2p/phase", raw="phase=match%0A",
                                     headers={"X-Dune-Session": token})
        self.assertEqual(400, phase.status, phase.body)

        for field in ("runtime=native%0A", "visibility=public%0A", "mode=custom%0A"):
            body = urllib.parse.urlencode(dict(claims(), maxPeers=2))
            key = field.split("=")[0]
            body = "&".join(part for part in body.split("&")
                            if not part.startswith(key + "="))
            response = self.service.request("POST", "/v1/admission/host",
                                            raw=body + "&" + field)
            self.assertEqual(400, response.status, "%s: %s" % (field, response.body))

    def test_a_smuggled_newline_never_reaches_a_response_line(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        self.signal(host.fields["session"], int(guest.fields["peer"]), "offer",
                    make_sdp(fingerprint(1)))
        seen = self.poll(guest.fields["session"])
        self.assertEqual(200, seen.status)
        for line in seen.lines:
            self.assertTrue(all(32 <= ord(c) <= 126 for c in line), line[:80])

    def test_two_players_cannot_share_one_display_name(self):
        """The client refuses a roster with a repeated name outright, so a second arrival under
        an existing name has to be refused here or nobody in the room can poll again."""
        admission, _ = self.seat("Duncan")
        grant = self.join(admission.fields["room"])
        clash = self.session(grant.fields["grant"], "Duncan")
        self.assertEqual(409, clash.status, clash.body)
        self.assertEqual("name_taken", clash.fields["code"])
        # A different name is fine, and the roster stays parseable.
        grant = self.join(admission.fields["room"])
        self.assertEqual(200, self.session(grant.fields["grant"], "Gurney").status)

    def test_a_name_freed_by_leaving_can_be_taken_again(self):
        admission, host = self.seat("Duncan")
        guest = self.seat_guest(admission, "Gurney")
        self.service.request("POST", "/v1/p2p/leave", {"bye": "1"},
                             headers={"X-Dune-Session": guest.fields["session"]})
        grant = self.join(admission.fields["room"])
        self.assertEqual(200, self.session(grant.fields["grant"], "Gurney").status)

    def test_a_departure_is_never_also_a_member_and_never_repeated(self):
        admission, host = self.seat("Host", maxPeers=4)
        first = self.seat_guest(admission, "A")
        second = self.seat_guest(admission, "B")
        for guest in (first, second):
            self.service.request("POST", "/v1/p2p/leave", {"bye": "1"},
                                 headers={"X-Dune-Session": guest.fields["session"]})
        seen = self.poll(host.fields["session"])
        departed = seen.multi.get("gone", [])
        members = [value.split("|")[0] for value in seen.multi["peer"]]
        self.assertEqual(sorted(departed), sorted(set(departed)))
        self.assertLessEqual(len(departed), 8)
        self.assertEqual([], [i for i in departed if i in members])

    def test_the_departure_list_never_exceeds_what_the_parser_accepts(self):
        """A room can churn through more players than it can hold at once. The client's parser
        stops at eight `gone=` lines and fails the whole snapshot on the ninth, so the emitted
        list is capped whatever the room has been through. Injected directly, because reaching
        this by churning real players would hit the admission rate limit first - which is itself
        the reason it is hard to reach."""
        admission, host = self.seat("Host", maxPeers=4)
        state = self.room_state(host.fields["session"])
        state["gone"] = [{"id": 100 + index, "at": state["lastSeen"]} for index in range(20)]
        self.write_room_state(host.fields["session"], state)
        seen = self.poll(host.fields["session"])
        departed = seen.multi.get("gone", [])
        self.assertLessEqual(len(departed), 8)
        self.assertEqual(sorted(departed), sorted(set(departed)))
        # The newest departures are the ones kept.
        self.assertIn("119", departed)

    def test_exactly_one_host_appears_in_the_roster(self):
        admission, host = self.seat("Host", maxPeers=4)
        self.seat_guest(admission, "A")
        self.seat_guest(admission, "B")
        roles = [value.split("|")[1] for value in self.poll(host.fields["session"]).multi["peer"]]
        self.assertEqual(1, roles.count("host"))
        self.assertEqual(2, roles.count("client"))

    def test_the_roster_never_repeats_a_peer_id(self):
        admission, host = self.seat("Host", maxPeers=4)
        self.seat_guest(admission, "A")
        self.seat_guest(admission, "B")
        ids = [value.split("|")[0] for value in self.poll(host.fields["session"]).multi["peer"]]
        self.assertEqual(sorted(ids), sorted(set(ids)))


class RealCandidateWireTests(SignalingTestCase):
    """The exact `<mid>|<candidate>` payloads recorded from Chromium and libdatachannel. An
    earlier revision accepted only SDP-attribute lines and refused every one of these with a 403,
    which is a signaling service that cannot connect anybody."""

    def setUp(self):
        super().setUp()
        self.admission, self.host_session = self.seat()
        self.guest = self.seat_guest(self.admission)
        self.token = self.host_session.fields["session"]
        self.to = int(self.guest.fields["peer"])
        self.signal(self.token, self.to, "offer", make_sdp(fingerprint(1)))

    def test_every_real_candidate_shape_is_accepted(self):
        for name, payload in REAL_CANDIDATES:
            with self.subTest(name):
                response = self.signal(self.token, self.to, "candidate", payload)
                self.assertEqual(200, response.status, "%s: %s" % (name, response.body))

    def test_a_real_candidate_is_forwarded_byte_for_byte(self):
        payload = REAL_CANDIDATES[0][1]
        self.signal(self.token, self.to, "candidate", payload)
        delivered = [value for value in self.poll(self.guest.fields["session"]).multi["sig"]
                     if value.split("|")[2] == "candidate"]
        self.assertEqual(1, len(delivered))
        self.assertEqual(payload, bytes.fromhex(delivered[0].split("|", 3)[3]).decode())

    def test_the_payload_bound_is_the_media_id_plus_the_candidate_plus_one(self):
        """2113 = 64 + 1 + 2048, the same arithmetic as
        P2PSignal::isAcceptableCandidatePayload."""
        exact = ("m" * 64) + "|" + candidate_line_of(2048)
        self.assertEqual(2113, len(exact))
        self.assertEqual(200, self.signal(self.token, self.to, "candidate", exact).status,
                         "the largest payload the client can carry must be deliverable")
        over = ("m" * 64) + "|" + candidate_line_of(2049)
        self.assertEqual(2114, len(over))
        self.assertNotEqual(200, self.signal(self.token, self.to, "candidate", over).status)

    def test_a_candidate_line_over_2048_is_refused_even_with_a_short_media_id(self):
        line = candidate_line_of(2049)
        self.assertEqual(2049, len(line))
        self.assertNotEqual(200, self.signal(self.token, self.to, "candidate", "0|" + line).status)

    def test_a_media_id_over_64_is_refused(self):
        payload = ("m" * 65) + "|" + HOST_CANDIDATE.split("|", 1)[1]
        self.assertNotEqual(200, self.signal(self.token, self.to, "candidate", payload).status)

    def test_a_second_delimiter_belongs_to_the_candidate_not_the_envelope(self):
        """The envelope splits on the first '|' only. A candidate that contains one is not valid
        ICE, so it is refused by the grammar rather than silently re-split."""
        payload = "0|candidate:1 1 UDP 2122252543 192.168.1.5 54321 typ host|extra"
        self.assertNotEqual(200, self.signal(self.token, self.to, "candidate", payload).status)

    def test_a_newline_in_the_envelope_is_refused(self):
        for payload in ("0|" + HOST_CANDIDATE.split("|", 1)[1] + "\r\n",
                        "0|" + HOST_CANDIDATE.split("|", 1)[1] + "\na=mid:0",
                        "0\n|" + HOST_CANDIDATE.split("|", 1)[1]):
            self.assertNotEqual(200, self.signal(self.token, self.to, "candidate", payload).status,
                                repr(payload))

    def test_an_envelope_without_a_delimiter_is_refused(self):
        bare = HOST_CANDIDATE.split("|", 1)[1]
        self.assertNotEqual(200, self.signal(self.token, self.to, "candidate", bare).status)

    def test_a_relay_candidate_in_the_real_shape_is_refused(self):
        real_relay = ("0|candidate:1853887674 1 udp 41885439 203.0.113.9 50000 typ relay "
                      "raddr 203.0.113.7 rport 51085 generation 0 ufrag 1RpS network-id 1")
        response = self.signal(self.token, self.to, "candidate", real_relay)
        self.assertEqual(403, response.status)
        self.assertEqual("relay_refused", response.fields["code"])

    def test_the_embedded_sdp_candidate_grammar_was_not_loosened(self):
        """Wrapping the envelope must not have made an SDP's own `a=candidate:` lines laxer: a
        media-id envelope inside a description is still not a candidate line."""
        sdp = make_sdp(fingerprint(7), extra="a=0|candidate:1 1 UDP 1 1.2.3.4 1 typ host\r\n")
        response = self.signal(self.guest.fields["session"],
                               int(self.host_session.fields["peer"]), "offer", sdp)
        self.assertNotEqual(200, response.status)


class IsolationTests(SignalingTestCase):
    def test_a_peer_cannot_address_somebody_in_another_room(self):
        _, host_a = self.seat("A")
        _, host_b = self.seat("B")
        refused = self.signal(host_a.fields["session"], int(host_b.fields["peer"]),
                              "offer", make_sdp(fingerprint(1)))
        self.assertEqual(403, refused.status)
        self.assertEqual("forbidden", refused.fields["code"])

    def test_a_peer_cannot_address_an_id_that_does_not_exist(self):
        _, host = self.seat()
        self.assertEqual(403, self.signal(host.fields["session"], 4242, "offer",
                                          make_sdp(fingerprint(1))).status)

    def test_a_peer_cannot_address_itself(self):
        _, host = self.seat()
        self.assertEqual(403, self.signal(host.fields["session"], int(host.fields["peer"]),
                                          "offer", make_sdp(fingerprint(1))).status)

    def test_a_session_token_from_one_room_does_not_work_in_another(self):
        _, host_a = self.seat("A")
        _, host_b = self.seat("B")
        forged = host_b.fields["session"][:8] + host_a.fields["session"][8:]
        self.assertIn(self.poll(forged).status, (403, 404))

    def test_polling_without_a_session_header_is_refused(self):
        self.assertEqual(401, self.service.request("POST", "/v1/p2p/poll", {"cursor": 0}).status)

    def test_a_malformed_session_header_is_refused(self):
        refused = self.service.request("POST", "/v1/p2p/poll", {"cursor": 0},
                                       headers={"X-Dune-Session": "../../etc/passwd"})
        self.assertEqual(401, refused.status)

    def test_a_session_header_naming_a_room_that_does_not_exist(self):
        self.assertIn(self.poll("dead" + "0" * 60).status, (403, 404))

    def test_one_peer_is_not_told_another_pairs_fingerprint(self):
        admission, host = self.seat("Host", maxPeers=4)
        a = self.seat_guest(admission, "A")
        b = self.seat_guest(admission, "B")
        secret = fingerprint(42)
        self.signal(a.fields["session"], int(b.fields["peer"]), "offer", make_sdp(secret))
        seen = self.poll(host.fields["session"])
        self.assertNotIn(secret, seen.body)
        self.assertEqual([], seen.multi.get("sig", []))
        self.assertEqual({}, self.attestations(seen))

    def test_a_departed_peer_cannot_keep_using_its_session(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        self.service.request("POST", "/v1/p2p/leave", {"bye": "1"},
                             headers={"X-Dune-Session": guest.fields["session"]})
        after = self.poll(guest.fields["session"])
        self.assertIn(after.status, (403, 404))


class PhaseTests(SignalingTestCase):
    def test_only_the_host_may_change_the_phase(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        refused = self.phase(guest.fields["session"], "match")
        self.assertEqual(403, refused.status)
        self.assertEqual("forbidden", refused.fields["code"])
        allowed = self.phase(host.fields["session"], "match")
        self.assertEqual(200, allowed.status)
        self.assertEqual("match", allowed.fields["phase"])
        self.assertEqual("match", self.poll(guest.fields["session"]).fields["phase"])

    def test_there_is_exactly_one_host_seat(self):
        admission, host = self.seat()
        self.seat_guest(admission)
        room = self.room_state(host.fields["session"])
        self.assertEqual(["client", "host"], sorted(p["role"] for p in room["peers"].values()))
        self.assertEqual(int(host.fields["peer"]), room["hostPeerId"])

    def test_returning_to_the_lobby_does_not_reopen_the_room(self):
        admission, host = self.seat()
        self.phase(host.fields["session"], "match")
        back = self.phase(host.fields["session"], "lobby")
        self.assertEqual(200, back.status)
        self.assertEqual("lobby", back.fields["phase"])
        refused = self.join(admission.fields["room"])
        self.assertEqual(409, refused.status)
        self.assertEqual("match_in_progress", refused.fields["code"])

    def test_an_unknown_phase_is_refused(self):
        _, host = self.seat()
        self.assertEqual(400, self.phase(host.fields["session"], "endgame").status)

    def test_a_running_match_does_not_lose_a_silent_peer(self):
        """Once a match is running the players do not need this service, so silence here is not
        evidence that anybody left: inventing `gone=` would tear down a working match."""
        admission, host = self.seat()
        self.seat_guest(admission)
        self.phase(host.fields["session"], "match")
        state = self.room_state(host.fields["session"])
        # Well past the lobby idle bound, and well past the 60 seconds in the first contract.
        for peer in state["peers"].values():
            peer["lastSeen"] -= 600000
        self.write_room_state(host.fields["session"], state)
        seen = self.poll(host.fields["session"])
        self.assertEqual(200, seen.status)
        self.assertEqual([], seen.multi.get("gone", []))
        self.assertEqual(2, len(seen.multi["peer"]))

    def test_a_silent_lobby_peer_is_dropped(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        state = self.room_state(host.fields["session"])
        state["peers"][guest.fields["peer"]]["lastSeen"] -= 600000
        self.write_room_state(host.fields["session"], state)
        seen = self.poll(host.fields["session"])
        self.assertIn(guest.fields["peer"], seen.multi.get("gone", []))
        self.assertEqual(1, len(seen.multi["peer"]))


class VisibilityTests(SignalingTestCase):
    def test_going_private_rotates_the_invitation_and_revokes_grants(self):
        admission = self.host(visibility="public")
        self.session(admission.fields["grant"], "Host")
        pending = self.join(admission.fields["room"])
        self.assertEqual(200, pending.status)

        changed = self.service.request("POST", "/v1/admission/visibility", dict(
            claims(), room=admission.fields["room"], control=admission.fields["control"],
            visibility="private"))
        self.assertEqual(200, changed.status, changed.body)
        self.assertEqual("private", changed.fields["visibility"])
        self.assertNotEqual(admission.fields["room"], changed.fields["room"])

        # The advertised code is gone, and the grant it issued is no longer redeemable.
        self.assertEqual(404, self.join(admission.fields["room"]).status)
        self.assertEqual(401, self.session(pending.fields["grant"], "Sneak").status)
        self.assertEqual(200, self.join(changed.fields["room"]).status)

    def test_going_public_keeps_the_code(self):
        admission = self.host(visibility="private")
        self.session(admission.fields["grant"], "Host")
        changed = self.service.request("POST", "/v1/admission/visibility", dict(
            claims(), room=admission.fields["room"], control=admission.fields["control"],
            visibility="public"))
        self.assertEqual(200, changed.status)
        self.assertEqual(admission.fields["room"], changed.fields["room"])

    def test_a_wrong_control_token_cannot_change_visibility(self):
        admission = self.host(visibility="public")
        refused = self.service.request("POST", "/v1/admission/visibility", dict(
            claims(), room=admission.fields["room"], control="0" * 64, visibility="private"))
        self.assertEqual(403, refused.status)

    def test_visibility_cannot_change_after_the_match_starts(self):
        admission, host = self.seat(visibility="public")
        self.phase(host.fields["session"], "match")
        refused = self.service.request("POST", "/v1/admission/visibility", dict(
            claims(), room=admission.fields["room"], control=admission.fields["control"],
            visibility="private"))
        self.assertEqual(409, refused.status)


class LobbyChatTests(SignalingTestCase):
    def enter(self, name, **kw):
        form = claims(**kw)
        form.update({"name": name.encode().hex(), "text": "", "cursor": 0})
        return self.service.request("POST", "/v1/lobby/enter", form)

    def say(self, session, text, **kw):
        form = claims(**kw)
        form.update({"session": session, "name": "", "text": text.encode().hex(), "cursor": 0})
        return self.service.request("POST", "/v1/lobby/say", form)

    def poll_chat(self, session, cursor=0, **kw):
        form = claims(**kw)
        form.update({"session": session, "name": "", "text": "", "cursor": cursor})
        return self.service.request("POST", "/v1/lobby/poll", form)

    def test_a_confirmed_name_carries_every_message(self):
        first = self.enter("Duncan")
        self.assertEqual(200, first.status, first.body)
        self.assertRegex(first.fields["session"], r"^[0-9a-f]{64}$")
        self.assertEqual(200, self.say(first.fields["session"], "spice must flow").status)
        seen = self.poll_chat(first.fields["session"])
        self.assertEqual(1, len(seen.multi["chat"]))
        _, name, text = seen.multi["chat"][0].split("|")
        self.assertEqual("Duncan", bytes.fromhex(name).decode())
        self.assertEqual("spice must flow", bytes.fromhex(text).decode())
        self.assertEqual("0", seen.fields["gap"])

    def test_a_name_in_use_is_refused(self):
        self.enter("Duncan")
        again = self.enter("duncan")
        self.assertEqual(409, again.status)
        self.assertEqual("name_taken", again.fields["code"])

    def test_a_message_needs_a_session(self):
        refused = self.say("0" * 64, "hello")
        self.assertEqual(403, refused.status)
        self.assertEqual("session_expired", refused.fields["code"])

    def test_a_session_does_not_cross_a_content_channel(self):
        entered = self.enter("Duncan")
        self.assertEqual(403, self.say(entered.fields["session"], "hello",
                                       contentHash="b" * 64).status)

    def test_a_name_cannot_be_impersonated_through_the_say_field(self):
        first = self.enter("Duncan")
        second = self.enter("Gurney")
        self.say(second.fields["session"], "hello")
        seen = self.poll_chat(first.fields["session"])
        _, name, _ = seen.multi["chat"][0].split("|")
        self.assertEqual("Gurney", bytes.fromhex(name).decode())

    def test_chat_is_rate_limited(self):
        entered = self.enter("Duncan")
        statuses = [self.say(entered.fields["session"], "message %d" % i).status
                    for i in range(6)]
        self.assertIn(429, statuses)

    def test_bad_utf8_and_formatting_characters_are_refused(self):
        self.assertEqual(400, self.service.request("POST", "/v1/lobby/enter", dict(
            claims(), name="fffe", text="", cursor=0)).status)
        # U+202E RIGHT-TO-LEFT OVERRIDE, the classic name-spoofing character.
        self.assertEqual(400, self.service.request("POST", "/v1/lobby/enter", dict(
            claims(), name="e280ae44756e63616e", text="", cursor=0)).status)

    def test_chat_answer_fits_the_legacy_parser(self):
        entered = self.enter("Duncan")
        for _ in range(3):
            self.say(entered.fields["session"], "x" * 30)
        seen = self.poll_chat(entered.fields["session"])
        self.assertLessEqual(len(seen.body.encode()), 8192)
        self.assertLessEqual(len(seen.lines), 16)
        for line in seen.lines:
            self.assertLessEqual(len(line.encode()), 512)
            self.assertLessEqual(len(line.partition("=")[2].encode()), 480)


class TransportTests(SignalingTestCase):
    def test_only_post_reaches_an_endpoint(self):
        for method in ("GET", "PUT", "DELETE", "PATCH", "HEAD"):
            self.assertEqual(404, self.service.request(method, "/v1/admission/host").status,
                             method)

    def test_health_is_a_get(self):
        response = self.service.request("GET", "/v1/health")
        self.assertEqual(200, response.status)
        self.assertEqual("ok", response.fields["status"])

    def test_an_unknown_path_is_a_404(self):
        self.assertEqual(404, self.service.request("POST", "/v1/admission/nope", claims()).status)
        self.assertEqual(404, self.service.request("POST", "/v1/p2p/../admission/host",
                                                   claims()).status)

    def test_an_allowlisted_origin_is_echoed_exactly(self):
        response = self.host()
        self.assertEqual(ORIGIN, response.headers["Access-Control-Allow-Origin"])
        self.assertEqual("Origin", response.headers["Vary"])
        self.assertNotIn("Access-Control-Allow-Credentials", response.headers)

    def test_a_foreign_origin_is_refused_and_never_reflected(self):
        response = self.service.request("POST", "/v1/admission/host", dict(claims(), maxPeers=2),
                                        origin="https://evil.example")
        self.assertEqual(403, response.status)
        self.assertEqual("forbidden_origin", response.fields["code"])
        self.assertNotIn("Access-Control-Allow-Origin", response.headers)

    def test_the_literal_null_origin_is_refused(self):
        response = self.service.request("POST", "/v1/admission/host", dict(claims(), maxPeers=2),
                                        origin="null")
        self.assertEqual(403, response.status)

    def test_no_origin_is_accepted_for_native_clients(self):
        response = self.service.request("POST", "/v1/admission/host", dict(claims(), maxPeers=2),
                                        origin=None)
        self.assertEqual(200, response.status)
        self.assertNotIn("Access-Control-Allow-Origin", response.headers)

    def test_preflight_allows_post_only(self):
        response = self.service.request("OPTIONS", "/v1/p2p/signal",
                                        headers={"Access-Control-Request-Method": "POST"})
        self.assertEqual(204, response.status)
        self.assertEqual("POST", response.headers["Access-Control-Allow-Methods"])
        refused = self.service.request("OPTIONS", "/v1/p2p/signal",
                                       headers={"Access-Control-Request-Method": "PUT"})
        self.assertEqual(403, refused.status)

    def test_a_repeated_form_field_is_refused(self):
        body = urllib.parse.urlencode(dict(claims(), maxPeers=2)) \
            + "&visibility=private&visibility=public"
        response = self.service.request("POST", "/v1/admission/host", raw=body)
        self.assertEqual(400, response.status)
        self.assertEqual("bad_request", response.fields["code"])

    def test_a_json_body_is_refused(self):
        response = self.service.request("POST", "/v1/admission/host",
                                        raw=json.dumps({"app": "dunecity"}),
                                        headers={"Content-Type": "application/json"})
        self.assertEqual(415, response.status)

    def test_a_forwarded_for_header_does_not_change_the_address(self):
        """Rate limiting keys on REMOTE_ADDR, which a caller cannot set."""
        self.assertEqual(200, self.service.request("POST", "/v1/admission/host",
                                                   dict(claims(), maxPeers=2),
                                                   headers={"X-Forwarded-For": "10.0.0.1"}).status)
        state = json.loads(self.service.state_file("rate.json"))
        self.assertTrue(all("10.0.0.1" not in key for key in state["a"]), list(state["a"]))

    def test_admission_is_rate_limited_per_address(self):
        statuses = [self.service.request("POST", "/v1/admission/host",
                                         dict(claims(), maxPeers=2)).status for _ in range(14)]
        self.assertIn(429, statuses)

    def test_a_grant_never_appears_in_a_log(self):
        admission = self.host()
        self.session(admission.fields["grant"], "Host")
        log = self.service.state_file("log.jsonl") or ""
        self.assertNotIn(admission.fields["grant"], log)
        self.assertNotIn(admission.fields["room"], log)
        self.assertNotIn(admission.fields["control"], log)

    def test_no_sdp_or_candidate_is_ever_logged(self):
        admission, host = self.seat()
        guest = self.seat_guest(admission)
        marker = fingerprint(1)
        self.signal(host.fields["session"], int(guest.fields["peer"]), "offer", make_sdp(marker))
        self.signal(host.fields["session"], int(guest.fields["peer"]), "candidate", HOST_CANDIDATE)
        for name in ("log.jsonl", "analytics.jsonl"):
            contents = self.service.state_file(name) or ""
            self.assertNotIn(marker, contents)
            self.assertNotIn("192.168.1.5", contents)
            self.assertNotIn("a=fingerprint", contents)
            self.assertNotIn(host.fields["session"], contents)


class StateDirectoryTests(SignalingTestCase):
    def test_state_files_are_private_and_outside_the_web_root(self):
        self.host()
        self.assertFalse(os.path.exists(os.path.join(ROOT, "public", "index.json")))
        for root, _, files in os.walk(self.service.state):
            for name in files:
                mode = os.stat(os.path.join(root, name)).st_mode & 0o777
                self.assertEqual(0, mode & 0o077, "%s is %s" % (name, oct(mode)))

    def test_a_symlinked_state_file_is_refused_rather_than_followed(self):
        target = os.path.join(self.service.tmp, "outside.json")
        with open(target, "w") as handle:
            handle.write("{}")
        link = os.path.join(self.service.state, "index.json")
        if os.path.exists(link):
            os.unlink(link)
        os.symlink(target, link)
        response = self.service.request("POST", "/v1/admission/host", dict(claims(), maxPeers=2))
        self.assertEqual(503, response.status, response.body)
        with open(target) as handle:
            self.assertEqual("{}", handle.read())

    def test_the_service_refuses_a_world_readable_state_directory(self):
        loose = os.path.realpath(tempfile.mkdtemp(prefix="dunecity-loose-"))
        os.chmod(loose, 0o755)
        try:
            self.service.write_config(state_dir=loose)
            response = self.service.request("POST", "/v1/admission/host",
                                            dict(claims(), maxPeers=2))
            self.assertEqual(503, response.status)
            self.assertIn("unavailable", response.body)
            self.assertEqual([], os.listdir(loose))
        finally:
            self.service.write_config()
            shutil.rmtree(loose, ignore_errors=True)

    def test_the_service_refuses_a_missing_state_directory(self):
        try:
            self.service.write_config(state_dir="/nonexistent/dunecity/state")
            self.assertEqual(503, self.service.request(
                "POST", "/v1/admission/host", dict(claims(), maxPeers=2)).status)
        finally:
            self.service.write_config()

    def test_a_turn_url_in_the_configuration_is_refused(self):
        try:
            self.service.write_config(ice_servers=["turn:turn.example.net"])
            self.assertEqual(503, self.service.request(
                "POST", "/v1/admission/host", dict(claims(), maxPeers=2)).status)
        finally:
            self.service.write_config()

    def test_a_wildcard_origin_in_the_configuration_is_refused(self):
        for bad in (["*"], ["null"], ["https://dunelegacy.com/"], ["https://dunelegacy.com:443"]):
            try:
                self.service.write_config(allowed_origins=bad)
                self.assertEqual(503, self.service.request(
                    "POST", "/v1/admission/host", dict(claims(), maxPeers=2)).status, bad)
            finally:
                self.service.write_config()

    def test_state_stays_bounded_when_a_room_ends(self):
        _, host = self.seat()
        room_id = host.fields["session"][:8]
        room_path = os.path.join(self.service.state, "rooms", "%s.json" % room_id)
        self.assertTrue(os.path.exists(room_path))
        self.service.request("POST", "/v1/p2p/leave", {"bye": "1"},
                             headers={"X-Dune-Session": host.fields["session"]})
        self.assertFalse(os.path.exists(room_path))
        index = json.loads(self.service.state_file("index.json"))
        self.assertFalse(index["rooms"])
        self.assertFalse(index["codes"])

    def test_an_expired_room_is_swept_out_of_the_directory(self):
        admission = self.host()
        index_path = os.path.join(self.service.state, "index.json")
        with open(index_path) as handle:
            index = json.load(handle)
        for room in index["rooms"].values():
            for field in ("createdAt", "lastSeen", "emptySince"):
                room[field] -= 7 * 60 * 60 * 1000
        room_path = os.path.join(self.service.state, "rooms", admission.fields["grant"][:8] + ".json")
        with open(room_path) as handle:
            room = json.load(handle)
        room["createdAt"] -= 7 * 60 * 60 * 1000
        room["lastSeen"] -= 7 * 60 * 60 * 1000
        with open(room_path, "w") as handle:
            json.dump(room, handle)
        with open(index_path, "w") as handle:
            json.dump(index, handle)
        self.assertEqual(404, self.join(admission.fields["room"]).status)
        with open(index_path) as handle:
            self.assertFalse(json.load(handle)["rooms"])


class AnalyticsTests(SignalingTestCase):
    def test_events_are_truthful_and_carry_no_identity(self):
        admission, host = self.seat("Hostname", maxPeers=4)
        self.seat_guest(admission, "Guestname")
        for _ in range(3):
            self.assertEqual(200, self.phase(host.fields["session"], "match").status)
        raw = self.service.state_file("analytics.jsonl") or ""
        events = [json.loads(line) for line in raw.split("\n") if line]
        kinds = [event["kind"] for event in events]
        self.assertIn("created", kinds)
        self.assertEqual(2, kinds.count("joined"))
        self.assertEqual(1, kinds.count("started"))
        for event in events:
            self.assertEqual(3, event["schema_version"])
            # Never 'wss' or 'https-poll': this service carries no gameplay, and claiming one of
            # those would be a claim about something it cannot see.
            self.assertEqual("direct-p2p", event["transport"])
            self.assertNotIn("moves", event)
            self.assertNotIn("outcome", event)
            self.assertNotIn("duration", event)
        self.assertNotIn("Hostname", raw)
        self.assertNotIn("Guestname", raw)
        self.assertNotIn(admission.fields["room"], raw)
        self.assertNotIn(admission.fields["grant"], raw)

    def test_browser_and_native_are_both_classified(self):
        admission = self.host()
        self.session(admission.fields["grant"], "Host", runtime="native")
        grant = self.join(admission.fields["room"], runtime="browser")
        session = self.service.request("POST", "/v1/p2p/session", dict(
            claims(runtime="browser"), grant=grant.fields["grant"],
            name="Browser".encode().hex()))
        self.assertEqual(200, session.status, session.body)
        raw = self.service.state_file("analytics.jsonl") or ""
        events = [json.loads(line) for line in raw.split("\n") if line]
        runtimes = {event["runtime_claimed"] for event in events if event["kind"] == "joined"}
        self.assertEqual({"native", "browser"}, runtimes)


class NoGameplayTests(SignalingTestCase):
    def test_there_is_no_gameplay_endpoint(self):
        for path in ("/v1/p2p/relay", "/v1/p2p/send", "/v1/relay", "/v1/poll/exchange",
                     "/v1/poll/open", "/v1/poll/close", "/v1/p2p/game"):
            self.assertEqual(404, self.service.request("POST", path, claims()).status, path)

    def test_the_service_makes_no_outbound_request(self):
        joined = self.sources()
        for forbidden in ("curl_init", "fsockopen", "stream_socket_client", "socket_create",
                          "shell_exec", "proc_open", "popen(", "file_get_contents('http",
                          "fopen('http", "fopen(\"http"):
            self.assertNotIn(forbidden, joined, forbidden)

    def test_the_service_never_opens_a_user_supplied_path(self):
        joined = self.sources()
        for forbidden in ("$_GET", "$_POST", "$_REQUEST", "$_FILES", "eval(", "assert(",
                          "unserialize("):
            self.assertNotIn(forbidden, joined, forbidden)

    @staticmethod
    def sources():
        joined = []
        for directory in ("src", "public", "bin"):
            for root, _, files in os.walk(os.path.join(ROOT, directory)):
                for name in sorted(files):
                    if name.endswith(".php"):
                        with open(os.path.join(root, name)) as handle:
                            joined.append(handle.read())
        return "\n".join(joined)


if __name__ == "__main__":
    unittest.main(verbosity=2)
