#!/usr/bin/python3 -I
"""Makes src/respawn_fx_data.h: how to make a multiplayer respawn's particle from the player's game.

    gen_respawn_fx.py <Catwalk project>/classes <root class> [more classes...]

Edit the classes in Catwalk, run this with the root class first and every class it emits after it,
then build Arklite. No class goes into the DLL: each one is level 0's class of that name plus a list
of edits, which respawn_fx.c applies to the player's own copy. So a class has to exist in level 0,
and whatever the edits write is ours: the changed values, not the class. Each class gets the prefix
ark_ so a map's own or level 0's never clash. The root becomes a one-shot: Arklite only sends a new particle its Create event, so what its Start and
Create turn on becomes one EmitParticles on Create, and it lives 0.1 s (a Lifetime of 0 is forever).
Every rec_<name>[__p<percent>] sound in the classes gets its sound group and impulse. The WAVs in
Catwalk's cache say how many channels each has.
"""
import difflib
import os
import re
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ONISPLIT = os.path.expanduser("~/Games/oni-x/drive_c/Program Files (x86)/Oni/AE/Tools/OniSplit.exe")
LEVEL0 = os.path.join(os.path.dirname(os.path.dirname(ONISPLIT)), "GameDataFolder", "level0_Final.dat")
SOUNDS = os.path.expanduser("~/.cache/catwalk/sounds")
MIN_COPY = 16     # a shorter stretch of the class costs more as a copy than as bytes
HEADER = '<?xml version="1.0" encoding="UTF-8"?>\n'


def one_shot(xml):
    root = ET.fromstring(xml)
    particle = root.find("Particle")
    particle.find("Options/Lifetime").text = "0.1"
    events = particle.find("Events")
    create = events.find("Create")
    if create is None:
        create = ET.SubElement(events, "Create")
    emitters = []
    for name in ("Start", "Create"):
        event = events.find(name)
        if event is None:
            continue
        for action in event.findall("EmitActivate"):
            emitters.append(action.find("Emitter").text.strip())
            if name == "Create":
                event.remove(action)
    for emitter in emitters:
        action = ET.SubElement(create, "EmitParticles")
        ET.SubElement(action, "Emitter").text = emitter
        ET.SubElement(action, "Particles").text = "1"
    return ET.tostring(root, encoding="unicode")


def sound_group(sound):
    name = sound[len("rec_"):]
    pitch = 1.0
    match = re.fullmatch(r"(.+)__p(\d+)", name)
    if match:
        name, pitch = match.group(1), int(match.group(2)) / 100
    with open(os.path.join(SOUNDS, name + ".aif.wav"), "rb") as f:
        channels = struct.unpack("<H", f.read(24)[22:24])[0]
    return f"""<Oni><SoundGroup><Volume>1</Volume><Pitch>1</Pitch><Flags></Flags>
<NumberOfChannels>{channels}</NumberOfChannels><Permutations><Permutation><Weight>10</Weight>
<Volume><Min>1</Min><Max>1</Max></Volume><Pitch><Min>{pitch}</Min><Max>{pitch}</Max></Pitch>
<Sound>{name}.aif</Sound></Permutation></Permutations></SoundGroup></Oni>
"""


def sound_impulse(sound):
    return f"""<Oni><ImpulseSound><Group>{sound}</Group><Priority>Normal</Priority>
<Volume><Distance><Min>25</Min><Max>260</Max></Distance>
<Angle><Min>360</Min><Max>360</Max><MinAttenuation>0</MinAttenuation></Angle></Volume>
<AlternateImpulse><Treshold>0</Treshold><Impulse></Impulse></AlternateImpulse>
<ImpactVelocity>0</ImpactVelocity><MinOcclusion>0</MinOcclusion></ImpulseSound></Oni>
"""


def raw_data(path):
    """An instance's binary data in a .oni: class tag, size, then the data the loaders read."""
    d = open(path, "rb").read()
    size, offset = struct.unpack("<II", d[0x88:0x90])
    raw = d[0xa0 + offset:0xa0 + offset + size]
    if len(raw) != size or struct.unpack("<I", raw[4:8])[0] + 8 != size:
        raise SystemExit(f"unexpected layout in {path}")
    return raw


def fnv1a(data):
    h = 0x811c9dc5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xffffffff
    return h


def make_edits(stock, wanted):
    """[(offset in stock, length, bytes to write after it)...] turning stock into wanted."""
    ops = []
    literal = bytearray()
    at = 0
    blocks = difflib.SequenceMatcher(None, stock, wanted, autojunk=False).get_matching_blocks()
    for i, j, n in blocks:
        if n >= MIN_COPY or not n:
            literal += wanted[at:j]
            if ops:
                ops[-1][2] += literal
            elif literal:
                ops.append([0, 0, bytearray(literal)])
            literal = bytearray()
            if n:
                ops.append([i, n, bytearray()])
            at = j + n
    out = bytearray()
    for offset, length, data in ops:
        out += stock[offset:offset + length] + data
    if bytes(out) != wanted:
        raise SystemExit("the edits don't make the class")
    return ops


def edits_bytes(ops):
    return b"".join(struct.pack("<III", o, n, len(d)) + bytes(d) + bytes(-len(d) & 3) for o, n, d in ops)


def rows(raw):
    return ",\n\t".join(", ".join(str(b) for b in raw[k:k + 24]) for k in range(0, len(raw), 24))


def main():
    classes_dir, names = sys.argv[1], [n.lower() for n in sys.argv[2:]]
    rename = {n: "ark_" + n for n in names}
    out = tempfile.mkdtemp(dir=os.path.join(HERE, "..", "build"))
    sounds = set()
    sources = []
    entries = []     # (kind, name, file)
    for name in names:
        xml = open(os.path.join(classes_dir, f"BINA3RAP{name}.xml"), encoding="utf-8-sig").read()
        for old, new in rename.items():
            xml = re.sub(rf'(Name="|>\s*){old}(?=\s*[<"])', lambda m: m.group(1) + new, xml, flags=re.I)
        if name == names[0]:
            xml = one_shot(xml)
        sounds.update(re.findall(r"rec_[A-Za-z0-9_.-]+", xml))
        path = os.path.join(out, f"BINA3RAP{rename[name]}.xml")
        open(path, "w", encoding="utf-8").write(HEADER + xml.split("?>", 1)[-1].lstrip())
        sources.append(path)
        entries.append(("FX_PARTICLE", rename[name], f"BINA3RAP{rename[name]}.oni"))
    for sound in sorted(sounds):
        for kind, suffix, text in (("FX_GROUP", "grp", sound_group(sound)), ("FX_IMPULSE", "imp", sound_impulse(sound))):
            path = os.path.join(out, f"OSBD{sound}.{suffix}.xml")
            open(path, "w", encoding="utf-8").write(HEADER + text)
            sources.append(path)
            entries.append((kind, sound, f"OSBD{sound}.{suffix}.oni"))
    made = os.path.join(out, "oni")
    os.mkdir(made)
    subprocess.run(["mono", ONISPLIT, "-create", made] + sources, check=True, stdout=subprocess.DEVNULL)

    stock_dir = os.path.join(out, "stock")
    os.mkdir(stock_dir)
    subprocess.run(["mono", ONISPLIT, "-export:BINA3RAP", stock_dir, LEVEL0], check=True, stdout=subprocess.DEVNULL)

    lines = ["/* Made by tools/gen_respawn_fx.py: don't edit. Edits to level 0's classes, not the classes. */",
             f'#define RESPAWN_FX_ROOT "{rename[names[0]]}"', ""]
    table = []
    stock_total = written = 0
    for i, (kind, name, file) in enumerate(entries):
        raw = raw_data(os.path.join(made, file))
        if kind == "FX_PARTICLE":
            base = name[len("ark_"):]
            stock = raw_data(os.path.join(stock_dir, f"BINA3RAP{base}.oni"))
            ops = make_edits(stock, raw)
            data = edits_bytes(ops)
            written += sum(len(d) for _, _, d in ops)
            stock_total += (len(stock) + 3) & ~3
            row = f'"{base}", {len(stock)}, 0x{fnv1a(stock):08x}'
        else:     # our own: a name and a pitch, or distances
            data = raw
            row = "0, 0, 0"
        lines.append(f"static const u8 g_fx{i}[] __attribute__((aligned(4))) = {{\n\t" + rows(data) + "\n};")
        table.append(f'\t{{ {kind}, "{name}", {row}, g_fx{i}, sizeof g_fx{i}, {len(raw)} }},')
    lines += ["", f"#define RESPAWN_FX_STOCK {stock_total}", "static const FxData g_fx_data[] = {", *table, "};", ""]
    open(os.path.join(HERE, "..", "src", "respawn_fx_data.h"), "w").write("\n".join(lines))
    print(f"{len(entries)} entries, {written} bytes written over {stock_total} bytes of level 0's:",
          ", ".join(e[1] for e in entries))


main()
