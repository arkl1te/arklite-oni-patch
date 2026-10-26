#!/usr/bin/env python3
"""Compares two PCs' collision query logs of one network game (Arklite-net-host.col and
Arklite-net-join.col, written next to the recordings) query by query.

    tools/col_diff.py Arklite-net-host.col Arklite-net-join.col [queries to show, default 4]

Each record is one level collision query made in a tick (0x441cb0): its input (the sphere and
its move, the quad flags it skips), its result (the quads touched and where) and the visited
marks left over in the three vectors when it started (set bits the undo list doesn't hold,
which hide quads from the query). For the first queries that differ it says whether the input
already differed (the caller's state: look at the recordings with akr_diff.py) or only the
result (the query itself: leftover marks, or something else per PC)."""
import struct
import sys

QUERY = struct.Struct("<IHH4f3fII3I")
RESULT = struct.Struct("<I3f")
MAX_RESULTS = 32
SITES = ["0x43b8d0", "0x44f2e7", "0x4661ab particles", "0x466351 particles", "0x49ec55",
         "0x4d6d75 camera", "0x4e2524", "0x4ebddf character walls", "0x4ec240 character"]


def queries(path):
    data = open(path, "rb").read()
    pos = 0
    while pos + QUERY.size <= len(data):
        q = QUERY.unpack_from(data, pos)
        pos += QUERY.size
        n = min(q[2], MAX_RESULTS)
        res = [RESULT.unpack_from(data, pos + i * RESULT.size) for i in range(n)]
        pos += n * RESULT.size
        yield dict(tick=q[0], site=q[1], count=q[2], sphere=q[3:7], move=q[7:10], mask=q[10],
                   max=q[11], stray=q[12:15], results=res)


def site(s):
    return SITES[s] if s < len(SITES) else f"site {s}"


def show(tag, q):
    sx = " ".join(f"{v:.4f}" for v in q["sphere"])
    mv = " ".join(f"{v:.4f}" for v in q["move"])
    print(f"  {tag}: tick {q['tick']} {site(q['site'])}, sphere {sx}, move {mv}, skip {q['mask']:#x}")
    print(f"  {' ' * len(tag)}  leftover marks (octree, nodes, quads): {q['stray']}; {q['count']} results:"
          f" {' '.join(str(r[0]) for r in q['results'])}")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    show_n = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    a, b = queries(sys.argv[1]), queries(sys.argv[2])
    shown = n = 0
    strays = [0, 0]
    for n, (x, y) in enumerate(zip(a, b), 1):
        for i, q in enumerate((x, y)):
            if any(q["stray"]):
                strays[i] += 1
        if x == y:
            continue
        same_input = (x["tick"], x["site"], x["sphere"], x["move"], x["mask"], x["max"]) == \
                     (y["tick"], y["site"], y["sphere"], y["move"], y["mask"], y["max"])
        print(f"query {n}: {'same input, different result' if same_input else 'different input'}")
        show("first ", x)
        show("second", y)
        shown += 1
        if shown >= show_n:
            break
    else:
        if not shown:
            print(f"no difference in {n} queries")
    print(f"queries with leftover marks (up to here): first {strays[0]}, second {strays[1]}")


if __name__ == "__main__":
    main()
