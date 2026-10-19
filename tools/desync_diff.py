#!/usr/bin/env python3
"""Compares the two desync dumps a network game writes at its first DESYNC.

    tools/desync_diff.py Arklite-desync-host.bin Arklite-desync-join.bin

Each file holds every character in use at the tick whose state hash differed: the character
(0x16a0 bytes), then its active data (0x2838). Prints each 4-byte value that differs, by
character and offset ("chr+0x148" or "act+0x21bc"), as hex and as a float."""
import struct
import sys

CHR_SIZE, ACTIVE_SIZE = 0x16a0, 0x2838
IMG = CHR_SIZE + ACTIVE_SIZE


def load(path):
    data = open(path, "rb").read()
    magic, version, tick, n = struct.unpack_from("<4I", data)
    if magic != 0x53444B41 or version != 1:
        sys.exit(f"{path}: not an Arklite desync dump")
    chars, pos = {}, 16
    for _ in range(n):
        index = struct.unpack_from("<I", data, pos)[0]
        chars[index] = data[pos + 8 : pos + 8 + IMG]
        pos += 8 + IMG
    return tick, chars


def name(img):
    return img[0x14 : 0x14 + 32].split(b"\0")[0].decode("latin-1")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    (ta, a), (tb, b) = load(sys.argv[1]), load(sys.argv[2])
    if ta != tb:
        print(f"note: the dumps are of different ticks ({ta} and {tb})")
    print(f"tick {ta}: {len(a)} and {len(b)} characters")
    for index in sorted(set(a) | set(b)):
        if index not in a or index not in b:
            print(f"#{index}: only in {'the first' if index in a else 'the second'} dump")
            continue
        x, y = a[index], b[index]
        diffs = [o for o in range(0, IMG, 4) if x[o : o + 4] != y[o : o + 4]]
        if not diffs:
            continue
        print(f"#{index} {name(x)}: {len(diffs)} values differ")
        for o in diffs:
            where = f"chr+0x{o:x}" if o < CHR_SIZE else f"act+0x{o - CHR_SIZE:x}"
            u, v = struct.unpack_from("<I", x, o)[0], struct.unpack_from("<I", y, o)[0]
            f, g = struct.unpack_from("<f", x, o)[0], struct.unpack_from("<f", y, o)[0]
            print(f"  {where:12} {u:08x} {v:08x}   {f:<14.6g} {g:<14.6g}")


if __name__ == "__main__":
    main()
