#!/usr/bin/env python3
"""Compares the two tick-by-tick desync files a network game writes at its first DESYNC.

    tools/desync_ticks.py Arklite-desyncticks-host_*.bin Arklite-desyncticks-join_*.bin [ticks to show, default 2]

Each file holds the last 256 ticks up to the tick whose state hash differed: per tick, the
state after it (both RNG seeds, Konoko's input state gs+0xb8, each character in use and its
active data) and the inputs it ran with. Prints the first tick where anything differs and what:
the inputs of that tick and the one before, the seeds, and every 4-byte value of the characters
(as desync_diff.py, without the fields each PC's drawing writes and pairs of pointers)."""
import struct
import sys

CHR_SIZE, ACTIVE_SIZE = 0x16A0, 0x2838
IMG = CHR_SIZE + ACTIVE_SIZE

# fields each PC's drawing or sound card writes (as akr_diff.py)
RENDER = {0x163C, 0x1640, 0x1660, 0x166C, 0x15F4}
RENDER |= {CHR_SIZE + o for o in (0x2218, 0x221C, 0x2220, 0x2224, 0x27E8, 0x27FC)}
RENDER |= {CHR_SIZE + 0x1CA8 + 16 * i + 4 for i in range(16)}
MASKS = {4: ~0x4000 & 0xFFFFFFFF, 0x1604: 0xFF, 0x161C: 0xFF}


def pointer(v):
    if v == 0 or (0x00100000 <= v < 0x20000000 and not v & 3):
        return True
    return 0x80 <= v >> 24 <= 0x87


def f(v):
    return struct.unpack("<f", struct.pack("<I", v))[0]


def load(path):
    data = open(path, "rb").read()
    magic, version, tick, n, npl, nchars, img, rec = struct.unpack_from("<8I", data)
    if magic != 0x54444B41 or version != 1 or img != IMG:
        sys.exit(f"{path}: not an Arklite tick-by-tick desync file")
    ticks, pos = {}, 32
    for _ in range(n):
        t, s0, s1, nplayers, guessed, nc = struct.unpack_from("<6I", data, pos)
        q = pos + 24
        inputs = [struct.unpack_from("<12I", data, q + p * 48) for p in range(npl)]
        q += npl * 48
        gs_input = struct.unpack_from("<12I", data, q)
        q += 48
        chars = {}
        for c in range(nc):
            index = struct.unpack_from("<I", data, q + c * (8 + IMG))[0]
            chars[index] = data[q + c * (8 + IMG) + 8 : q + c * (8 + IMG) + 8 + IMG]
        ticks[t] = dict(seeds=(s0, s1), nplayers=nplayers, guessed=guessed, inputs=inputs[:nplayers],
                        gs_input=gs_input, chars=chars)
        pos += rec
    return tick, ticks


def char_diffs(a, b):
    out = []
    for o in range(0, IMG, 4):
        if o in RENDER:
            continue
        x, y = struct.unpack_from("<I", a, o)[0], struct.unpack_from("<I", b, o)[0]
        m = MASKS.get(o, 0xFFFFFFFF)
        if x & m == y & m or (pointer(x) and pointer(y)):
            continue
        out.append((o, x, y))
    return out


def where(o):
    return f"chr+{o:#x}" if o < CHR_SIZE else f"act+{o - CHR_SIZE:#x}"


def name(img):
    return img[0x14:0x14 + 32].split(b"\0")[0].decode("latin-1")


def words(r):
    return " ".join(f"{w:08x}" for w in r)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    show = int(sys.argv[3]) if len(sys.argv) > 3 else 2
    (ta, a), (tb, b) = load(sys.argv[1]), load(sys.argv[2])
    common = sorted(set(a) & set(b))
    print(f"desync at tick {ta} / {tb}; ticks in both files: {common[0] if common else '-'}..{common[-1] if common else '-'}")
    shown = 0
    for t in common:
        x, y = a[t], b[t]
        problems = []
        if x["inputs"] != y["inputs"]:
            problems.append("inputs")
        if x["seeds"] != y["seeds"]:
            problems.append("seeds")
        diffs = {}
        for i in sorted(set(x["chars"]) | set(y["chars"])):
            if i not in x["chars"] or i not in y["chars"]:
                problems.append(f"#{i} only in one")
                continue
            d = char_diffs(x["chars"][i], y["chars"][i])
            if d:
                diffs[i] = d
        if not problems and not diffs:
            continue
        print(f"\n=== after tick {t}: " + ", ".join(problems + [f"#{i} {len(d)} values" for i, d in diffs.items()]))
        for k in (t - 1, t):
            if k in a and k in b:
                for p in range(max(len(a[k]["inputs"]), len(b[k]["inputs"]))):
                    ia = a[k]["inputs"][p] if p < len(a[k]["inputs"]) else ()
                    ib = b[k]["inputs"][p] if p < len(b[k]["inputs"]) else ()
                    mark = "" if ia == ib else "   <-- differs"
                    print(f"  tick {k} player {p + 1} input A {words(ia)}{mark}")
                    if mark:
                        print(f"  tick {k} player {p + 1} input B {words(ib)}")
                print(f"  tick {k} guessed A {a[k]['guessed']:#x} B {b[k]['guessed']:#x}; seeds A {a[k]['seeds'][0]:08x}/{a[k]['seeds'][1]:08x} B {b[k]['seeds'][0]:08x}/{b[k]['seeds'][1]:08x}")
        if x["gs_input"] != y["gs_input"]:
            print(f"  gs+0xb8 A {words(x['gs_input'])}\n  gs+0xb8 B {words(y['gs_input'])}")
        for i, d in diffs.items():
            print(f"  #{i} {name(x['chars'][i])}: {len(d)} values differ")
            for o, va, vb in d[:80]:
                print(f"    {where(o):12} {va:08x} {vb:08x}   {f(va):<14.6g} {f(vb):<14.6g}")
        shown += 1
        if shown >= show:
            break
    if not shown:
        print("no tick differs in what both files hold")


if __name__ == "__main__":
    main()
