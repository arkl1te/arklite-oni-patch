#!/usr/bin/env python3
"""Tells which Arklite logs come from FPS Daodan (leave them out), AE Daodan (the base) or neither.

    fps_daodan_check.py LOG_OR_FOLDER...      (folders: every *.log below them)

Prints one line per log: FPS DAODAN, AE or UNCLEAR, its Arklite version, and the evidence lines. Exit
status 0 if every log is AE, 1 if any is FPS DAODAN or UNCLEAR.

FPS Daodan when any of these start-up lines is there: Arklite found the code it patches already
changed by FPS Daodan and skipped it. Each was in every FPS Daodan log seen (9 of 9: Arklite 0.4.0,
0.4.1 and 0.5.0, four Daodan builds, three PCs) and in none of the 44 AE logs (checked 2026-11-13):
  character limits: Daodan site 0x6428654a changed (...), skipped
      (AE: "character limits: 255 characters, all active (...)")
  patch 0x40d018: unexpected bytes (...), skipped       (and 0x40d058: the sky clamp)
  mp rules: 0x4e65f0 changed, damage not scaled
  flare depth: unexpected code at 0x405ad0, left alone  (Arklite 0.4.1 on; AE: "flare depth: lens flares read ...")
Backing evidence only, never enough alone:
  "net: Arklite source ..., Daodan <x>, its settings <y>, ...": x is c68fc0f5 on AE (every AE log
  seen); FPS Daodan builds seen: 7d353bac, a2998938, b107fdc7. y is bd36f39b in every FPS Daodan log (its
  daodan.ini) and 811c9dc4 in every AE log seen, but editing daodan.ini changes y on AE too.
AE when the AE character-limits line is there, no FPS Daodan line is, and any Daodan fingerprint is AE's.
UNCLEAR otherwise: a cut log, an older Arklite, a new AE Daodan or another mod. Ask the user."""
import os
import re
import sys

AE_DAODAN = {"c68fc0f5"}
FPS_DAODAN = {"7d353bac", "a2998938", "b107fdc7"}
FPS_DAODAN_LINES = [
    re.compile(r"^character limits: Daodan site 0x[0-9a-f]+ changed \([0-9a-f]+\), skipped"),
    re.compile(r"^patch 0x40d0[15]8: unexpected bytes "),
    re.compile(r"^mp rules: 0x4e65f0 changed, damage not scaled"),
    re.compile(r"^flare depth: unexpected code at 0x[0-9a-f]+, left alone"),
]
AE_LINE = re.compile(r"^character limits: \d+ characters, all active")
NET_LINE = re.compile(r"^net: Arklite source [0-9a-f]{8}, Daodan ([0-9a-f]{8}), its settings ([0-9a-f]{8})")


def check(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()
    version = lines[0][len("Arklite "):-len(" loaded")] if lines and re.match(r"^Arklite \S+ loaded$", lines[0]) else "?"
    fps_daodan, backing, ae, daodans = [], [], False, set()
    for line in lines:
        line = line.strip()
        if any(p.match(line) for p in FPS_DAODAN_LINES):
            fps_daodan.append(line)
        elif AE_LINE.match(line):
            ae = True
        m = NET_LINE.match(line)
        if m:
            daodans.add(m.group(1))
            if (m.group(1) in FPS_DAODAN or m.group(2) == "bd36f39b") and line not in backing:
                backing.append(line)
    if fps_daodan:
        return "FPS DAODAN", version, fps_daodan + backing
    if ae and daodans <= AE_DAODAN:
        return "AE", version, [f"Daodan {d}" for d in sorted(daodans)] or ["(no network game: no Daodan fingerprint)"]
    why = []
    if not ae:
        why.append("no 'character limits:' line (cut log or older Arklite)")
    for d in sorted(daodans - AE_DAODAN):
        why.append(f"Daodan {d}: " + ("an FPS Daodan build, but none of its start-up lines" if d in FPS_DAODAN else "not AE's c68fc0f5 (a new AE Daodan or another mod?)"))
    return "UNCLEAR", version, why


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    files = []
    for a in sys.argv[1:]:
        if os.path.isdir(a):
            for root, _, names in os.walk(a):
                files += [os.path.join(root, n) for n in names if n.lower().endswith(".log")]
        else:
            files.append(a)
    bad = 0
    for f in sorted(files):
        kind, version, evidence = check(f)
        bad += kind != "AE"
        print(f"{kind:10} {version:16} {f}")
        for e in evidence:
            print(f"             {e[:140]}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
