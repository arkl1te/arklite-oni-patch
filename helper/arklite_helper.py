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
"""
import argparse
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

    def run(self):
        last_expire = time.monotonic()
        while True:
            r, _, _ = select.select([self.sock], [], [], 5.0)
            now = time.monotonic()
            if r:
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
                last_expire = now
            self.log(now)


def main():
    p = argparse.ArgumentParser(description="Arklite helper: server list and introductions for Oni multiplayer")
    p.add_argument("--bind", default="0.0.0.0")
    p.add_argument("--port", type=int, default=27780)
    p.add_argument("--games-per-ip", type=int, default=MAX_GAMES_PER_IP, help="for tests on one PC")
    a = p.parse_args()
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((a.bind, a.port))
    s.setblocking(False)
    print(f"Arklite helper on UDP {a.bind}:{a.port}", flush=True)
    try:
        Helper(s, a.games_per_ip).run()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
