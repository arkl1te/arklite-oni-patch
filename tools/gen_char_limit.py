#!/usr/bin/env python3
"""Generates src/char_limit_sites.h for char_limit.c.

Oni's game state (one block, allocated at startup) holds the character table, then a tail:
the active-character table, the shadow pool, three character pointer lists and some other
fields. To raise the limits, the character table grows in place and the tail moves to the end
of a larger block. This script finds every instruction in Oni.exe (and Daodan) whose
displacement or immediate is a tail offset, and writes them out with the old->new layout.

Usage: tools/gen_char_limit.py  (needs objdump; paths below)
"""
import re
import struct
import subprocess
import sys
from pathlib import Path

ONI = "/home/arkl1te/Games/oni-x/drive_c/Program Files (x86)/Oni/Oni.exe"
DAODAN = "/home/arkl1te/Games/oni-x/drive_c/Program Files (x86)/Oni/AE/binkw32.dll"
OUT = Path(__file__).resolve().parent.parent / "src" / "char_limit_sites.h"

LIMIT = 255   # 256 would give a handle of 0xffff (salt 0x7f, index 0xff), the "none" handle

CHAR_BASE, CHAR_STRIDE = 0x1260, 0x16a0
OLD_SIZE = 0x1679e0

# Tail segments of the old layout: (old_lo, old_hi, stride or 0 for plain fields)
SEGMENTS = [
	(0x0b6260, 0x0b6268, 0),        # character count (u16) and a field
	(0x0b6268, 0x157068, 0x2838),   # active characters [64]
	(0x157068, 0x15706a, 0),        # active count (u16)
	(0x15706a, 0x1670ea, 0x804),    # shadows [32]
	(0x1670ea, 0x1670f0, 0),        # shadow high-water count
	(0x1670f0, 0x1672f0, 4),        # character list 1 [128]
	(0x1672f0, 0x1672f8, 0),        # its count, and a counter
	(0x1672f8, 0x1674f8, 4),        # character list 2 [128]
	(0x1674f8, 0x167500, 0),        # its count
	(0x167500, 0x167700, 4),        # character list 3 [128]
	(0x167700, OLD_SIZE, 0),        # its count, then other fields
]

# Displacements biased by one active entry: base - 0x2838 + field, used with a 1-based index
# (the active count shrink loop at 0x4dc79f reads +0xb3a32 + count * 0x2838)
BIASED_LO = 0x0b6268 - 0x2838

# Constants in the tail range that are not game-state offsets (checked by hand)
NOT_OFFSETS = {0xc3500, 0xdb481, 0xf423f, 0xf4240, 0xff000, 0xfffff, 0x100000, 0x13c6d0, 0x148800}


def layout():
	cur = CHAR_BASE + LIMIT * CHAR_STRIDE
	segs = []
	for lo, hi, stride in SEGMENTS:
		new_lo = ((cur + 15) & ~15) + (lo & 15)   # keep the old alignment
		size = LIMIT * stride if stride else hi - lo
		segs.append((lo, hi, new_lo))
		cur = new_lo + size
	assert segs[0][2] == CHAR_BASE + LIMIT * CHAR_STRIDE   # old count/end-of-table offset still lines up
	active_new = segs[1][2]
	segs.insert(0, (BIASED_LO, SEGMENTS[0][0], active_new - 0x2838))
	return segs, (cur + 31) & ~31


def pe_sections(path):
	exe = Path(path).read_bytes()
	pe = struct.unpack_from("<I", exe, 0x3c)[0]
	n = struct.unpack_from("<H", exe, pe + 6)[0]
	opt = struct.unpack_from("<H", exe, pe + 20)[0]
	base = struct.unpack_from("<I", exe, pe + 24 + 28)[0]
	secs = []
	for i in range(n):
		o = pe + 24 + opt + 40 * i
		name = exe[o:o + 8].rstrip(b"\0")
		vsize, va, rsize, roff = struct.unpack_from("<IIII", exe, o + 8)
		secs.append((name, base + va, roff, rsize))
	return exe, base, secs


def instructions(path):
	out = subprocess.run(["objdump", "-d", "-M", "intel", path], capture_output=True, text=True,
	                     check=True).stdout
	insns = []
	for line in out.splitlines():
		p = line.split("\t")
		if len(p) == 2 and insns and re.fullmatch(r"\s*[0-9a-f]+:", p[0]) and re.fullmatch(r"[0-9a-f ]+", p[1]):
			insns[-1][1] += bytes.fromhex(p[1].replace(" ", ""))   # wrapped long instruction
			continue
		if len(p) < 3 or not re.fullmatch(r"\s*[0-9a-f]+:", p[0]):
			continue
		insns.append([int(p[0].strip().rstrip(":"), 16), bytes.fromhex(p[1].replace(" ", "")), p[2].strip()])
	return insns


def tail_sites(path, lo, hi):
	sites = []
	for addr, raw, text in instructions(path):
		if text.startswith(("call", "j")):
			continue
		for m in re.finditer(r"0x([0-9a-f]+)\b", text):
			v = int(m.group(1), 16)
			if lo <= v < hi and v not in NOT_OFFSETS:
				pat = struct.pack("<I", v)
				off = raw.find(pat)
				assert off >= 0 and raw.find(pat, off + 1) < 0, (hex(addr), text)
				sites.append((addr, off, v, text))
	return sites


def check_raw(path, sites):
	"""Every occurrence of a site value in .text must be one of the decoded sites."""
	exe, base, secs = pe_sections(path)
	seen = {a + o for a, o, v, t in sites}
	for name, va, roff, rsize in secs:
		if name != b".text":
			continue
		data = exe[roff:roff + rsize]
		for v in sorted({s[2] for s in sites}):
			pat = struct.pack("<I", v)
			i = data.find(pat)
			while i >= 0:
				if va + i not in seen:
					sys.exit(f"{path}: {v:#x} at {va + i:#x} is not a decoded instruction")
				i = data.find(pat, i + 1)


def main():
	segs, new_size = layout()
	lo, hi = BIASED_LO, OLD_SIZE

	oni = tail_sites(ONI, lo, hi)
	check_raw(ONI, oni)
	# Daodan's other constants in this range (flags, sizes) aren't offsets: keep only values
	# that Oni itself uses as game-state offsets. Its code reaches the game state through
	# 0x5ece7c, and these sites were checked by hand.
	offsets = {s[2] for s in oni}
	daodan = [s for s in tail_sites(DAODAN, lo, hi) if s[2] in offsets]
	_, daodan_base, _ = pe_sections(DAODAN)

	w = []
	w.append("/* Generated by tools/gen_char_limit.py; do not edit. */")
	w.append("#pragma once")
	w.append(f"#define CL_LIMIT        {LIMIT}")
	w.append(f"#define CL_OLD_SIZE     {OLD_SIZE:#x}")
	w.append(f"#define CL_NEW_SIZE     {new_size:#x}")
	w.append(f"#define CL_CHAR_DWORDS  {LIMIT * CHAR_STRIDE // 4:#x}   /* 0x2d400 for 128 */")
	w.append(f"#define CL_ACTIVE_DWORDS {LIMIT * 0x2838 // 4:#x}  /* 0x28380 for 64 */")
	w.append("")
	w.append("/* old_lo, old_hi, new_lo */")
	w.append("static const u32 cl_segments[][3] = {")
	for a, b, c in segs:
		w.append(f"\t{{ {a:#08x}, {b:#08x}, {c:#08x} }},")
	w.append("};")
	w.append("")
	w.append(f"/* Oni.exe: instruction address, offset of the 32-bit value in it, old value ({len(oni)} sites) */")
	w.append("static const struct cl_site { u32 addr; u8 off; u32 old; } cl_oni_sites[] = {")
	for a, o, v, t in oni:
		w.append(f"\t{{ {a:#x}, {o}, {v:#x} }},   /* {t} */")
	w.append("};")
	w.append("")
	w.append(f"/* Daodan (binkw32.dll): same, with the address relative to its image base {daodan_base:#x} */")
	w.append("static const struct cl_site cl_daodan_sites[] = {")
	for a, o, v, t in daodan:
		w.append(f"\t{{ {a - daodan_base:#x}, {o}, {v:#x} }},   /* {t} */")
	w.append("};")
	OUT.write_text("\n".join(w) + "\n")
	print(f"{OUT}: {len(oni)} Oni.exe sites, {len(daodan)} Daodan sites, new size {new_size:#x}")


main()
