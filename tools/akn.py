#!/usr/bin/env python3
"""akn.py: Arklite match recordings (<log>.<session>.akn, net.c "Match recordings").

  akn.py FILE              the header, the ticks, the inputs per player, the hashes, the frame journal
  akn.py A B               two PCs' recordings of one session: the first tick whose inputs differ,
                           and the hashes both know that differ (in sync: none)
"""
import struct
import sys

MAGIC = 0x4e524b41  # "AKRN"


def read(path):
    data = open(path, "rb").read()
    magic, version, hlen = struct.unpack_from("<III", data, 0)
    if magic != MAGIC or version not in (1, 2):
        raise SystemExit(f"{path}: not an Arklite recording (magic {magic:08x}, version {version})")
    pos = 12
    header = data[pos:pos + hlen].decode("latin-1")
    pos += hlen
    ticks, npl, enc_len = struct.unpack_from("<III", data, pos)
    pos += 12
    enc = data[pos:pos + enc_len]
    pos += enc_len
    (nh,) = struct.unpack_from("<I", data, pos)
    pos += 4
    hashes = {}
    for i in range(nh):
        t, h = struct.unpack_from("<II", data, pos + i * 8)
        if h:
            hashes[t] = h
    pos += nh * 8
    journal = []
    if version >= 2 and pos + 4 <= len(data):
        (nf,) = struct.unpack_from("<I", data, pos)
        pos += 4
        journal = [tuple(data[pos + i * 3:pos + i * 3 + 3]) for i in range(nf)]
    rows = []
    prev = [[0] * 12 for _ in range(npl)]
    k = 0
    for _ in range(ticks):
        mask = enc[k]
        k += 1
        row = [list(p) for p in prev]
        for p in range(npl):
            if not (mask >> p) & 1:
                continue
            (words,) = struct.unpack_from("<H", enc, k)
            k += 2
            for w in range(12):
                if (words >> w) & 1:
                    (row[p][w],) = struct.unpack_from("<I", enc, k)
                    k += 4
        rows.append(row)
        prev = row
    if k != len(enc):
        raise SystemExit(f"{path}: {len(enc) - k} bytes left after the ticks: the file is damaged")
    read.journal = journal
    return header, npl, rows, hashes


def show(path):
    header, npl, rows, hashes = read(path)
    print(header.rstrip())
    print(f"ticks: {len(rows)} ({len(rows) / 60:.1f} s), {npl} player slots")
    for p in range(npl):
        active = sum(1 for r in rows if any(r[p]))
        if active:
            print(f"  player {p + 1}: input in {active} ticks")
    print(f"hashes: {len(hashes)} known, last at tick {max(hashes) if hashes else '-'}")
    j = read.journal
    if j:
        idle = sum(1 for f in j if f[0] == 0)
        waited = sum(1 for f in j if f[2] & 1)
        rb = [f[1] for f in j if f[1]]
        runs, run = [], 0
        for f in j:
            if f[0] == 0:
                run += 1
            elif run:
                runs.append(run)
                run = 0
        print(f"frame journal: {len(j)} frames, {idle} without a tick (longest run {max(runs) if runs else 0}), "
              f"{waited} waited for inputs, {len(rb)} rollbacks (deepest {max(rb) if rb else 0})")


def compare(a, b):
    ha, npl, ra, xa = read(a)
    hb, _, rb, xb = read(b)
    n = min(len(ra), len(rb))
    print(f"{a}: {len(ra)} ticks; {b}: {len(rb)} ticks; comparing {n}")
    first = next((t for t in range(n) if ra[t] != rb[t]), None)
    if first is None:
        print("inputs: the same in every tick both have")
    else:
        for p in range(npl):
            if ra[first][p] != rb[first][p]:
                print(f"inputs: tick {first} differs first, player {p + 1}: {ra[first][p]} vs {rb[first][p]}")
    both = sorted(set(xa) & set(xb))
    bad = [t for t in both if xa[t] != xb[t]]
    print(f"hashes: {len(both)} known on both, {len(bad)} differ" + (f", first at tick {bad[0]}" if bad else ""))
    return first is None and not bad


if __name__ == "__main__":
    if len(sys.argv) == 2:
        show(sys.argv[1])
    elif len(sys.argv) == 3:
        sys.exit(0 if compare(sys.argv[1], sys.argv[2]) else 1)
    else:
        print(__doc__)
