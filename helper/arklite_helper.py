#!/usr/bin/env python3
"""Arklite helper: the server list and introductions for Oni multiplayer games (Arklite.dll).

One UDP port, Python 3 standard library only. It never carries game traffic.

- A host sends BEAT every 15 s from its game socket: what its game is (name, map, players). The
  helper lists it under the address and port the packets came from, which is the host's NAT
  mapping, the one players must send to. BEAT_ACK tells the host it's listed, and its address.
- The Join list asks LIST: the games of the same protocol, in pages of LIST_PAGE.
- A player that wants to reach a host asks INTRO_REQ with the host's address (the port may be the
  default one: a host behind CGNAT gets a random public port, found by its IP if only one host is
  there). The helper sends the host INTRO with the player's address, and the host knocks toward it,
  which opens the host's NAT for the player (hole punching). INTRO_ACK tells the player the host's
  real address and port (after the one asked about).

Safety:
- Every request needs the key of its sender's address (a keyed hash only the helper can make, sent
  back in a 12-byte COOKIE to any request of 12 bytes or more). Only the real owner of an address
  sees its key, so nobody can list a game for, get a list sent to, or be introduced as someone
  else's address, and no answer ever goes to an address that didn't ask for it.
- Packets per address are limited (a token bucket), and so are games per IP and introductions per
  game.
- Addresses live only in memory and go when a game stops sending BEAT for EXPIRE_S. The log has
  counts, never addresses.

Problem reports (TCP, the same port number; --reports DIR, off without it):
- Arklite sends one when a network game goes out of sync, a player drops, or Oni crashed: the log
  and the dumps, made anonymous on the player's PC (no addresses, names, chat or absolute paths),
  each file an LZ4 block. One report per connection: "AKRP", version, body length, the body
  (Arklite version, source hash, reason, player slot, tick, session ID, the files), answered "OK".
- Kept in DIR/<day>/<session>/<slot>-<reason>-<time>-<n>/: info.json and each file as <name>.lz4.
  The sender's address is never written (it only rate-limits, in memory). Reports older than
  REPORT_KEEP_DAYS go, and the oldest go first when DIR holds more than --reports-max-mb.
"""
import argparse
import json
import re
import shutil
import hashlib
import hmac
import os
import select
import socket
import struct
import sys
import time

MAGIC = 0x50484B41          # "AKHP"
VERSION = 1
HEAD = struct.Struct("<IBBHI")  # magic, version, type, game protocol, key
COOKIE, BEAT, BEAT_ACK, LIST_REQ, LIST, INTRO_REQ, INTRO_ACK, INTRO = range(1, 9)

GAME = struct.Struct("<IIIIIIII48s64s48s")   # server_id, flags, level, players, max_players, spectators, max_spectators, in_match, name, folder, map
ENTRY = struct.Struct("<IHHIIIIIII48s64s48s")  # addr, port (network order bytes as stored), proto, then GAME without flags
ADDR = struct.Struct("<4sHH")                # IPv4 address and port (both network order), status/pad
FLAG_HIDDEN = 1                              # host: introductions only, not on the list

LIST_PAGE = 6               # entries per LIST (12 + 12 + 6 * 196 = 1200 bytes)
EXPIRE_S = 45               # a game that stopped sending BEAT for this long goes
MAX_GAMES = 2000
MAX_GAMES_PER_IP = 4
RATE = 20.0                 # packets per second per address...
BURST = 40.0                # ...with this many at once
INTRO_RATE = 10.0           # introductions per second per game...
INTRO_BURST = 20.0
COOKIE_RATE = 2000.0        # cookies per second in all (an answer to anyone who asks)
LOG_EVERY_S = 3600

REPORT_MAGIC = 0x50524B41   # "AKRP"
REPORT_HEAD = struct.Struct("<III")          # magic, version, body length
REPORT_INFO = struct.Struct("<32sIIII40sI")  # version, source hash, reason, slot, tick, session, files
REPORT_FILE = struct.Struct("<64sII")        # name, raw size, packed size
REPORT_MAX = 16 << 20       # a report's body, at most
REPORT_FILE_MAX = 64 << 20  # a file unpacked, at most
REPORT_FILES = 8
REPORT_RATE = 12.0 / 3600   # reports per second per IP (12 an hour: players behind one address share it)...
REPORT_BURST = 12.0         # ...with this many at once
REPORT_CONNS = 6            # connections at once (each holds up to REPORT_MAX in memory)
REPORT_TIMEOUT_S = 60       # a connection that hasn't sent its report by then is closed
REPORT_KEEP_DAYS = 30
REASONS = {1: "desync", 2: "crash", 3: "drop"}


class Bucket:
    __slots__ = ("tokens", "at")

    def __init__(self, burst):
        self.tokens = burst
        self.at = time.monotonic()

    def take(self, rate, burst, now):
        self.tokens = min(burst, self.tokens + (now - self.at) * rate)
        self.at = now
        if self.tokens < 1.0:
            return False
        self.tokens -= 1.0
        return True


class Game:
    __slots__ = ("addr", "proto", "fields", "flags", "last", "intros")

    def __init__(self, addr):
        self.addr = addr
        self.intros = Bucket(INTRO_BURST)


class Reports:
    """The TCP side: one anonymous problem report per connection (see the top)."""

    def __init__(self, sock, folder, max_bytes, count):
        self.sock = sock
        self.folder = folder
        self.max_bytes = max_bytes
        self.count = count
        self.conns = {}           # socket -> [buffer, opened at, ip]
        self.buckets = {}         # ip -> Bucket (memory only)
        self.serial = 0
        os.makedirs(folder, exist_ok=True)

    def sockets(self):
        return [self.sock] + list(self.conns)

    def close(self, c):
        self.conns.pop(c, None)
        try:
            c.close()
        except OSError:
            pass

    def accept(self, now):
        for _ in range(16):
            try:
                c, addr = self.sock.accept()
            except (BlockingIOError, OSError):
                return
            if len(self.conns) >= REPORT_CONNS:
                self.count("report refused: busy")
                c.close()
                continue
            c.setblocking(False)
            self.conns[c] = [bytearray(), now, addr[0]]

    def readable(self, c, now):
        st = self.conns.get(c)
        if st is None:
            return
        try:
            data = c.recv(1 << 16)
        except BlockingIOError:
            return
        except OSError:
            self.close(c)
            return
        if not data:
            self.close(c)
            return
        buf = st[0]
        buf += data
        if len(buf) >= REPORT_HEAD.size:
            magic, version, n = REPORT_HEAD.unpack_from(buf)
            if magic != REPORT_MAGIC or version != 1 or n > REPORT_MAX or n < REPORT_INFO.size:
                self.count("report refused: not one")
                self.close(c)
                return
            if len(buf) >= REPORT_HEAD.size + n:
                self.finish(c, bytes(buf[REPORT_HEAD.size:REPORT_HEAD.size + n]), st[2], now)

    def finish(self, c, body, ip, now):
        b = self.buckets.get(ip)
        if b is None:
            b = self.buckets[ip] = Bucket(REPORT_BURST)
        answer = b"NO\0\0"
        if not b.take(REPORT_RATE, REPORT_BURST, now):
            self.count("report refused: too many")
        else:
            try:
                self.store(body)
                answer = b"OK\0\0"
                self.count("reports stored")
            except (ValueError, OSError, struct.error):
                self.count("report refused: bad")
        try:
            c.send(answer)
        except OSError:
            pass
        self.close(c)

    @staticmethod
    def text(raw):
        return raw.split(b"\0", 1)[0].decode("ascii", "replace")

    def store(self, body):
        version, src_hash, reason, slot, tick, session, nfiles = REPORT_INFO.unpack_from(body)
        if nfiles > REPORT_FILES:
            raise ValueError
        session = re.sub(r"[^A-Za-z0-9-]", "", self.text(session))[:40] or "none"
        files, pos = [], REPORT_INFO.size
        for _ in range(nfiles):
            name, raw, packed = REPORT_FILE.unpack_from(body, pos)
            pos += REPORT_FILE.size
            name = re.sub(r"[^A-Za-z0-9._-]", "_", self.text(name))[:64].lstrip(".") or "file"
            if raw > REPORT_FILE_MAX or pos + packed > len(body):
                raise ValueError
            files.append((name, raw, body[pos:pos + packed]))
            pos += packed
        t = time.gmtime()
        self.serial += 1
        where = os.path.join(self.folder, time.strftime("%Y-%m-%d", t), session,
                             f"{slot if slot < 64 else 'x'}-{REASONS.get(reason, 'other')}-{time.strftime('%H%M%S', t)}-{self.serial}")
        os.makedirs(where, exist_ok=True)
        info = {"arklite": self.text(version), "source": f"{src_hash:08x}", "reason": REASONS.get(reason, str(reason)),
                "slot": slot, "tick": tick, "session": session, "received_utc": time.strftime("%Y-%m-%d %H:%M:%S", t),
                "files": [{"name": n, "size": r, "packed": len(d)} for n, r, d in files]}
        for n, _, d in files:
            with open(os.path.join(where, n + ".lz4"), "wb") as f:
                f.write(d)
        with open(os.path.join(where, "info.json"), "w") as f:
            json.dump(info, f, indent=1)

    def expire(self, now):
        for c in [c for c, st in self.conns.items() if now - st[1] > REPORT_TIMEOUT_S]:
            self.count("report closed: too slow")
            self.close(c)
        for ip in [ip for ip, b in self.buckets.items() if now - b.at > 3 * 3600]:
            del self.buckets[ip]

    def tidy(self):
        """Days older than REPORT_KEEP_DAYS go, then the oldest days while over the size limit."""
        days = sorted(d for d in os.listdir(self.folder) if re.fullmatch(r"\d{4}-\d{2}-\d{2}", d))
        cutoff = time.strftime("%Y-%m-%d", time.gmtime(time.time() - REPORT_KEEP_DAYS * 86400))
        for d in [d for d in days if d < cutoff]:
            shutil.rmtree(os.path.join(self.folder, d), ignore_errors=True)
        days = [d for d in days if d >= cutoff]

        def size(path):
            total = 0
            for root, _, names in os.walk(path):
                for n in names:
                    try:
                        total += os.path.getsize(os.path.join(root, n))
                    except OSError:
                        pass
            return total
        sizes = {d: size(os.path.join(self.folder, d)) for d in days}
        total = sum(sizes.values())
        while days and total > self.max_bytes:
            d = days.pop(0)
            shutil.rmtree(os.path.join(self.folder, d), ignore_errors=True)
            total -= sizes[d]


class Helper:
    def __init__(self, sock, per_ip=MAX_GAMES_PER_IP):
        self.sock = sock
        self.per_ip = per_ip
        self.secret = os.urandom(32)
        self.games = {}           # (ip, port) -> Game
        self.buckets = {}         # (ip, port) -> Bucket
        self.cookies = Bucket(COOKIE_RATE)
        self.counts = {}
        self.logged_at = time.monotonic()

    def count(self, what):
        self.counts[what] = self.counts.get(what, 0) + 1

    def key_of(self, addr):
        ip, port = addr
        d = hmac.new(self.secret, socket.inet_aton(ip) + struct.pack(">H", port), hashlib.sha256).digest()
        return struct.unpack("<I", d[:4])[0] or 1

    def send(self, addr, ptype, proto, body=b""):
        try:
            self.sock.sendto(HEAD.pack(MAGIC, VERSION, ptype, proto, self.key_of(addr)) + body, addr)
        except OSError:
            self.count("send failed")

    def packet(self, data, addr, now):
        if len(data) < HEAD.size:
            return
        magic, version, ptype, proto, key = HEAD.unpack_from(data)
        if magic != MAGIC or version != VERSION:
            return
        b = self.buckets.get(addr)
        if b is None:
            if len(self.buckets) > 100000:
                self.expire(now, force=True)
            b = self.buckets[addr] = Bucket(BURST)
        if not b.take(RATE, BURST, now):
            self.count("over the rate")
            return
        if key != self.key_of(addr):
            # its key, no bigger than what came (and in all no faster than COOKIE_RATE)
            if self.cookies.take(COOKIE_RATE, COOKIE_RATE, now):
                self.send(addr, COOKIE, proto)
                self.count("cookie")
            return
        body = data[HEAD.size:]
        if ptype == BEAT and len(body) >= GAME.size:
            self.beat(addr, proto, body, now)
        elif ptype == LIST_REQ and len(body) >= 4:
            self.list(addr, proto, struct.unpack_from("<I", body)[0], now)
        elif ptype == INTRO_REQ and len(body) >= ADDR.size:
            self.intro(addr, proto, body, now)

    def beat(self, addr, proto, body, now):
        g = self.games.get(addr)
        if g is None:
            if len(self.games) >= MAX_GAMES or sum(1 for a in self.games if a[0] == addr[0]) >= self.per_ip:
                self.send(addr, BEAT_ACK, proto, struct.pack("<I", 0) + ADDR.pack(socket.inet_aton(addr[0]), socket.htons(addr[1]), 0))
                self.count("game refused: full")
                return
            g = self.games[addr] = Game(addr)
            self.count("game listed")
        f = GAME.unpack_from(body)
        g.proto = proto
        g.flags = f[1]
        g.fields = (f[0],) + f[2:]
        g.last = now
        self.send(addr, BEAT_ACK, proto, struct.pack("<I", 1) + ADDR.pack(socket.inet_aton(addr[0]), socket.htons(addr[1]), 0))

    def list(self, addr, proto, page, now):
        games = sorted((g for g in self.games.values() if g.proto == proto and not g.flags & FLAG_HIDDEN and now - g.last < EXPIRE_S),
                       key=lambda g: (g.fields[7], g.addr))   # by name, then address: pages stay in order
        part = games[page * LIST_PAGE:(page + 1) * LIST_PAGE]
        body = struct.pack("<III", len(games), page, len(part))
        for g in part:
            body += ENTRY.pack(struct.unpack("<I", socket.inet_aton(g.addr[0]))[0], socket.htons(g.addr[1]), g.proto, *g.fields)
        self.send(addr, LIST, proto, body)
        self.count("list")

    def find(self, ip, port, now):
        g = self.games.get((ip, port))
        if g and now - g.last < EXPIRE_S:
            return g
        same = [g for g in self.games.values() if g.addr[0] == ip and now - g.last < EXPIRE_S]
        return same[0] if len(same) == 1 else None

    def intro(self, addr, proto, body, now):
        raw, port_n, _ = ADDR.unpack_from(body)
        ip, port = socket.inet_ntoa(raw), socket.ntohs(port_n)
        g = self.find(ip, port, now)
        asked = ADDR.pack(raw, port_n, 0)
        if g is None or g.proto != proto or g.addr == addr:
            self.send(addr, INTRO_ACK, proto, struct.pack("<I", 0) + asked + asked)
            self.count("intro: no such game")
            return
        if g.intros.take(INTRO_RATE, INTRO_BURST, now):
            self.send(g.addr, INTRO, proto, ADDR.pack(socket.inet_aton(addr[0]), socket.htons(addr[1]), 0))
            self.count("intro")
        else:
            self.count("intro: over the rate")
        self.send(addr, INTRO_ACK, proto, struct.pack("<I", 1) + asked + ADDR.pack(socket.inet_aton(g.addr[0]), socket.htons(g.addr[1]), 0))

    def expire(self, now, force=False):
        for a in [a for a, g in self.games.items() if now - g.last >= EXPIRE_S]:
            del self.games[a]
        for a in [a for a, b in self.buckets.items() if force or now - b.at > 60]:
            del self.buckets[a]

    def log(self, now):
        if now - self.logged_at < LOG_EVERY_S:
            return
        self.logged_at = now
        what = ", ".join(f"{k} {v}" for k, v in sorted(self.counts.items())) or "nothing"
        print(f"{len(self.games)} games listed; last hour: {what}", flush=True)
        self.counts.clear()

    def run(self, reports=None):
        last_expire = last_tidy = time.monotonic()
        if reports:
            reports.tidy()
        while True:
            r, _, _ = select.select([self.sock] + (reports.sockets() if reports else []), [], [], 5.0)
            now = time.monotonic()
            if reports:
                for c in r:
                    if c is reports.sock:
                        reports.accept(now)
                    elif c is not self.sock:
                        reports.readable(c, now)
                if now - last_tidy >= 3600:
                    reports.tidy()
                    last_tidy = now
            if self.sock in r:
                for _ in range(256):
                    try:
                        data, addr = self.sock.recvfrom(2048)
                    except BlockingIOError:
                        break
                    except OSError:
                        continue
                    self.packet(data, addr, now)
            if now - last_expire >= 5:
                self.expire(now)
                if reports:
                    reports.expire(now)
                last_expire = now
            self.log(now)


def main():
    p = argparse.ArgumentParser(description="Arklite helper: server list and introductions for Oni multiplayer")
    p.add_argument("--bind", default="0.0.0.0")
    p.add_argument("--port", type=int, default=27780)
    p.add_argument("--games-per-ip", type=int, default=MAX_GAMES_PER_IP, help="for tests on one PC")
    p.add_argument("--reports", help="folder for anonymous problem reports (TCP on the same port); none: off")
    p.add_argument("--reports-max-mb", type=int, default=4000, help="the reports folder's size limit")
    a = p.parse_args()
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((a.bind, a.port))
    s.setblocking(False)
    print(f"Arklite helper on UDP {a.bind}:{a.port}", flush=True)
    helper = Helper(s, a.games_per_ip)
    reports = None
    if a.reports:
        t = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        t.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        t.bind((a.bind, a.port))
        t.listen(32)
        t.setblocking(False)
        reports = Reports(t, a.reports, a.reports_max_mb << 20, helper.count)
        print(f"problem reports on TCP {a.bind}:{a.port} into {a.reports}", flush=True)
    try:
        helper.run(reports)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
