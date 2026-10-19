#!/usr/bin/env python3
"""Compares two Arklite recordings tick by tick: the two PCs' recordings of one network game
(Arklite-net-host.akr and Arklite-net-join.akr), or any two recordings of the same level.

    tools/akr_diff.py Arklite-net-host.akr Arklite-net-join.akr [ticks to show, default 3]

Frames don't matter (each PC splits ticks into frames its own way); ticks are compared in
order. For each tick: the inputs it ran with (network recordings), both RNG streams, the RNG
callers (code addresses; bit 31 = outside a tick, bit 30 = the tick-local stream), and every
4-byte value of every character and its active data. Values that already differ at the level's
start (pointers, which differ between PCs) and the fields drawing writes (flag 0x4000, +0x163c,
+0x1660, active +0x221c..+0x2224) are skipped. Prints the first ticks that differ."""
import struct
import sys

MAGIC, VERSION = 0x50524B41, 7
TAG_TICK, TAG_END = 0x4B434954, 0x46444E45
CHR_SIZE, ACTIVE_SIZE = 0x16A0, 0x2838
IMG_DWORDS = (CHR_SIZE + ACTIVE_SIZE) // 4
ACTION = 0x30
FRAME_P2, FRAME_P2N = 1, 2

RENDER = {0x163C // 4, 0x1640 // 4, 0x1660 // 4}
RENDER |= {(CHR_SIZE + o) // 4 for o in (0x221C, 0x2220, 0x2224)}
MASKS = {1: ~0x4000 & 0xFFFFFFFF}   # flags: drawn this frame


class Rec:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        self.pos = 0
        h = self.take("<8I")
        if h[0] != MAGIC or h[1] != VERSION:
            sys.exit(f"{path}: not an Arklite recording of format {VERSION}")
        self.level, self.start_tick, self.seed = h[2], h[3], h[4]
        self.images = {}
        self.names = {}
        self.start = self.read_tick()

    def take(self, fmt):
        v = struct.unpack_from(fmt, self.data, self.pos)
        self.pos += struct.calcsize(fmt)
        return v

    def read_tick(self):
        (tag,) = self.take("<I")
        assert tag == TAG_TICK, hex(tag)
        tick, seed, n_chars, n_calls, fpu, local = self.take("<6I")
        chars = [self.take("<IIi7f") for _ in range(n_chars)]
        for c in chars:
            img = self.images.setdefault(c[0], [0] * IMG_DWORDS)
            (m,) = self.take("<I")
            for _ in range(m):
                k, v = self.take("<II")
                img[k] = v
        calls = list(self.take(f"<{n_calls}I")) if n_calls else []
        present = {c[0]: list(self.images[c[0]]) for c in chars}
        return dict(tick=tick, seed=seed, local=local, fpu=fpu, calls=calls, chars=present)

    def ticks(self):
        """(inputs, tick) for every tick, inputs = (p1 record, p2 record or None)"""
        while self.pos + 4 <= len(self.data):
            n, flags = self.take("<HH")
            p1 = [self.data[self.pos + i * ACTION : self.pos + (i + 1) * ACTION] for i in range(n)]
            self.pos += n * ACTION
            p2 = [None] * n
            if flags & FRAME_P2N:
                p2 = [self.data[self.pos + i * ACTION : self.pos + (i + 1) * ACTION] for i in range(n)]
                self.pos += n * ACTION
            elif flags & FRAME_P2:
                p2 = [self.data[self.pos : self.pos + ACTION]] + [None] * max(n - 1, 0)
                self.pos += ACTION
            i = 0
            while self.pos + 4 <= len(self.data):
                (tag,) = struct.unpack_from("<I", self.data, self.pos)
                if tag == TAG_END:
                    self.pos += 4
                    break
                t = self.read_tick()
                yield (p1[i] if i < len(p1) else None, p2[i] if i < len(p2) else None), t
                i += 1


def name(img):
    raw = struct.pack("<8I", *img[5:13])   # +0x14, 32 bytes
    return raw.split(b"\0")[0].decode("latin-1")


def where(k):
    o = k * 4
    return f"chr+0x{o:x}" if o < CHR_SIZE else f"act+0x{o - CHR_SIZE:x}"


def as_float(v):
    return struct.unpack("<f", struct.pack("<I", v))[0]


def callers(calls):
    out = []
    for c in calls:
        tag = "out " if c & 0x80000000 else "loc " if c & 0x40000000 else ""
        out.append(f"{tag}{c & 0x3FFFFFFF:x}")
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    show = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    a, b = Rec(sys.argv[1]), Rec(sys.argv[2])
    print(f"level {a.level} / {b.level}, start tick {a.start_tick} / {b.start_tick}, RNG {a.seed:08x} / {b.seed:08x}")
    ignore = {}
    for idx in set(a.start["chars"]) & set(b.start["chars"]):
        x, y = a.start["chars"][idx], b.start["chars"][idx]
        ignore[idx] = {k for k in range(IMG_DWORDS) if x[k] != y[k]}
    shown = 0
    count = 0
    for count, ((ia, ta), (ib, tb)) in enumerate(zip(a.ticks(), b.ticks()), 1):
        out = []
        if ia != ib:
            for who, (ra, rb) in zip(("player 1", "player 2"), zip(ia, ib)):
                if ra != rb:
                    out.append(f"  {who}'s input differs: {ra.hex() if ra else None}")
                    out.append(f"  {' ' * len(who)}                   {rb.hex() if rb else None}")
        if ta["seed"] != tb["seed"] or ta["local"] != tb["local"]:
            out.append(f"  RNG {ta['seed']:08x}/{ta['local']:08x} vs {tb['seed']:08x}/{tb['local']:08x}")
        if ta["calls"] != tb["calls"]:
            ca, cb = callers(ta["calls"]), callers(tb["calls"])
            out.append(f"  RNG callers ({len(ca)} vs {len(cb)}):")
            out.append(f"    first: {' '.join(ca)}")
            out.append(f"    second: {' '.join(cb)}")
        for idx in sorted(set(ta["chars"]) | set(tb["chars"])):
            if idx not in ta["chars"] or idx not in tb["chars"]:
                out.append(f"  #{idx} only in the {'first' if idx in ta['chars'] else 'second'} recording")
                continue
            x, y = ta["chars"][idx], tb["chars"][idx]
            skip = ignore.get(idx, set())
            diffs = []
            for k in range(IMG_DWORDS):
                if k in skip or k in RENDER:
                    continue
                m = MASKS.get(k, 0xFFFFFFFF)
                if (x[k] & m) != (y[k] & m):
                    diffs.append(k)
            if diffs:
                out.append(f"  #{idx} {name(x)}: {len(diffs)} values differ")
                for k in diffs[:24]:
                    out.append(f"    {where(k):12} {x[k]:08x} {y[k]:08x}   {as_float(x[k]):<14.6g} {as_float(y[k]):<14.6g}")
        if out:
            print(f"tick {count} (game tick {ta['tick']} / {tb['tick']}):")
            print("\n".join(out))
            shown += 1
            if shown >= show:
                break
    else:
        if not shown:
            print(f"no difference in {count} ticks")


if __name__ == "__main__":
    main()
