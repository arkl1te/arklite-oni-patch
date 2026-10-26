#!/usr/bin/env python3
"""Compares two PCs' collision logs of one network game (Arklite-net-host.col and
Arklite-net-join.col, written next to the recordings) record by record.

    tools/col_diff.py Arklite-net-host.col Arklite-net-join.col [records to show, default 4]

Two kinds of record, both only from ticks:
  - query: one level collision query (0x441cb0): its input (the sphere and its move, the quad
    flags it skips), its result (the quads touched and where) and the visited marks left over
    in the three vectors when it started (set bits the undo list doesn't hold).
  - step: a character's physics move step (0x43a710) right after it sorted its contacts: the
    physics context (0x150 bytes), the move and the sorted contacts (0x28 bytes each: +0 kind
    (0 level, 1 object), +4 the quad or object, +8 the plane, +0x18 the point, +0x24 the
    squared distance). The first contact whose push is at least 1/1024 moves the character.
For the first records that differ it prints what differs. Values that both look like memory
addresses are left out (they differ between PCs)."""
import struct
import sys

TAG_QUERY, TAG_STEP = 0x59525551, 0x50455453
QUERY = struct.Struct("<IHH4f3fII3I")
RESULT = struct.Struct("<I3f")
STEP = struct.Struct("<III3f336s")
CONTACT = 0x28
MAX_RESULTS = 32
SITES = ["0x43b8d0", "0x44f2e7", "0x4661ab particles", "0x466351 particles", "0x49ec55",
         "0x4d6d75 camera", "0x4e2524", "0x4ebddf character walls", "0x4ec240 character"]
# physics context fields known so far
CONTEXT = {0x128: "flags (0x100 keep back-facing contacts, 0x800 no push, 0x8000 second step)",
           0x12c: "kind", 0x148: "callbacks", 0x14c: "owner"}


def records(path):
    data = open(path, "rb").read()
    pos = 0
    while pos + 4 <= len(data):
        (tag,) = struct.unpack_from("<I", data, pos)
        pos += 4
        if tag == TAG_QUERY:
            q = QUERY.unpack_from(data, pos)
            pos += QUERY.size
            n = min(q[2], MAX_RESULTS)
            res = [RESULT.unpack_from(data, pos + i * RESULT.size) for i in range(n)]
            pos += n * RESULT.size
            yield dict(kind="query", tick=q[0], site=q[1], count=q[2], sphere=q[3:7], move=q[7:10],
                       mask=q[10], max=q[11], stray=q[12:15], results=res)
        elif tag == TAG_STEP:
            s = STEP.unpack_from(data, pos)
            pos += STEP.size
            n = min(s[2], MAX_RESULTS)
            contacts = [data[pos + i * CONTACT: pos + (i + 1) * CONTACT] for i in range(n)]
            pos += n * CONTACT
            yield dict(kind="step", tick=s[0], context=s[1], count=s[2], move=s[3:6],
                       data=s[6], contacts=contacts)
        else:
            sys.exit(f"{path}: unknown record {tag:#x} at {pos - 4} (a log from an older Arklite?)")


def pointer(v):
    return v == 0 or (0x00100000 <= v < 0x20000000 and not v & 3) or 0x80 <= v >> 24 <= 0x87


def f32(v):
    return struct.unpack("<f", struct.pack("<I", v))[0]


def site(s):
    return SITES[s] if s < len(SITES) else f"site {s}"


def show_query(tag, q):
    sx = " ".join(f"{v:.6f}" for v in q["sphere"])
    mv = " ".join(f"{v:.6f}" for v in q["move"])
    print(f"  {tag}: tick {q['tick']} query {site(q['site'])}, sphere {sx}, move {mv}, skip {q['mask']:#x}")
    print(f"  {' ' * len(tag)}  leftover marks (octree, nodes, quads): {q['stray']}; {q['count']} results:"
          f" {' '.join(str(r[0]) for r in q['results'])}")


def contact_text(c):
    kind, what = struct.unpack_from("<II", c, 0)
    plane = struct.unpack_from("<4f", c, 8)
    point = struct.unpack_from("<3f", c, 0x18)
    (dist,) = struct.unpack_from("<f", c, 0x24)
    return (f"kind {kind} plane {' '.join(f'{v:.6f}' for v in plane)} point "
            f"{' '.join(f'{v:.4f}' for v in point)} dist² {dist:.6f}")


def show_step(tag, s):
    mv = " ".join(f"{v:.6f}" for v in s["move"])
    flags = struct.unpack_from("<I", s["data"], 0x128)[0]
    print(f"  {tag}: tick {s['tick']} move step, context #{s['context']}, flags {flags:#x}, move {mv}, {s['count']} contacts")
    for i, c in enumerate(s["contacts"]):
        print(f"  {' ' * len(tag)}    {i}: {contact_text(c)}")


def compare_steps(x, y):
    a = struct.unpack("<84I", x["data"])
    b = struct.unpack("<84I", y["data"])
    for k in range(84):
        if a[k] != b[k] and not (pointer(a[k]) and pointer(b[k])):
            name = CONTEXT.get(k * 4, "")
            print(f"    context +{k * 4:#05x}  {a[k]:08x} {b[k]:08x}   {f32(a[k]):<14.6g} {f32(b[k]):<14.6g} {name}")
    for i, (c, d) in enumerate(zip(x["contacts"], y["contacts"])):
        ca, cb = struct.unpack("<10I", c), struct.unpack("<10I", d)
        for k in range(10):
            if ca[k] != cb[k] and not (k == 1 and pointer(ca[k]) and pointer(cb[k])):
                print(f"    contact {i} +{k * 4:#04x}  {ca[k]:08x} {cb[k]:08x}   {f32(ca[k]):<14.6g} {f32(cb[k]):<14.6g}")


def same(x, y):
    if x["kind"] != y["kind"]:
        return False
    if x["kind"] == "query":
        return x == y
    if (x["tick"], x["context"], x["count"], x["move"]) != (y["tick"], y["context"], y["count"], y["move"]):
        return False
    a, b = struct.unpack("<84I", x["data"]), struct.unpack("<84I", y["data"])
    if any(p != q and not (pointer(p) and pointer(q)) for p, q in zip(a, b)):
        return False
    for c, d in zip(x["contacts"], y["contacts"]):
        ca, cb = struct.unpack("<10I", c), struct.unpack("<10I", d)
        if any(ca[k] != cb[k] and not (k == 1 and pointer(ca[k]) and pointer(cb[k])) for k in range(10)):
            return False
    return True


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    show_n = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    shown = n = 0
    strays = [0, 0]
    for n, (x, y) in enumerate(zip(records(sys.argv[1]), records(sys.argv[2])), 1):
        for i, r in enumerate((x, y)):
            if r["kind"] == "query" and any(r["stray"]):
                strays[i] += 1
        if same(x, y):
            continue
        print(f"record {n}:")
        for tag, r in (("first ", x), ("second", y)):
            (show_query if r["kind"] == "query" else show_step)(tag, r)
        if x["kind"] == y["kind"] == "step":
            compare_steps(x, y)
        shown += 1
        if shown >= show_n:
            break
    else:
        if not shown:
            print(f"no difference in {n} records")
    print(f"queries with leftover marks (up to here): first {strays[0]}, second {strays[1]}")


if __name__ == "__main__":
    main()
