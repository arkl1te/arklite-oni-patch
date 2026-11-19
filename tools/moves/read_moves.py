#!/usr/bin/python3 -I
"""Reads every character's attack moves from an OniSplit XML export of level0, for gen_md.py and
gen_table.py (make.sh runs all three). Prints JSON: label -> its animation sets, super-move sounds
and moves.

    read_moves.py <XML folder> [class=Label,...]

The folder holds the ONCC, TRAC and TRAM instances as XML (make.sh says how to export them).
Without a list, the F2 menu's game characters (ROSTER); with one, those classes, e.g. a class an
AE package gives another set: konoko_generic="Konoko: New Combat Moves". Each class's moves come
from its TRAC chain, a move in a collection hiding the same one in the collections after it, the
way TRrCollection_Lookup finds them.
"""
import json, os, re, sys, xml.etree.ElementTree as ET

ROSTER = [("konoko_generic", "Konoko"), ("griffin_generic", "Griffin"), ("muro_generic", "Muro"),
          ("barabus", "Barabas"), ("SH_generic", "Shinatama"), ("Shinzom_generic", "Hasegawa"),
          ("ninja_med_1", "Ninja"), ("striker_med_1", "Striker"),
          ("tanker_med_1", "Tanker"), ("comguy_1", "Comguy"), ("elite_med_1", "Elite"),
          ("red_med_1", "Red"), ("TCTF_swat_1", "TCTF SWAT"), ("TCTF_lite_1", "TCTF Trooper"),
          ("Cop_generic", "Cop"), ("CopfemA1", "Female Cop"), ("thug_wh_1", "Thug"),
          ("mad_bomber", "Mad Bomber"), ("ninjabot", "Ninjabot"), ("sniper_generic", "Sniper"),
          ("security_guard_1", "Security Guard"), ("bd_1", "Scientist"), ("bdfem_1", "Female Scientist"),
          ("generic_male_1", "Civilian"), ("generic_female_1", "Female Civilian"), ("mutantmuro", "Mutant Muro")]

X = None   # the XML folder (main)

def load(path):
    with open(path, "rb") as f:
        return ET.fromstring(f.read().decode("utf-8-sig"))

def trac(name):
    r = load(os.path.join(X, name + ".xml"))
    parent = (r.findtext(".//ParentCollection") or "").strip()
    anims = [a.text.strip() for a in r.iter("Animation") if a.text and a.text.strip()]
    return parent, anims

_tram = {}
def tram(name):
    if name in _tram: return _tram[name]
    p = os.path.join(X, name + ".xml")
    if not os.path.exists(p): _tram[name] = None; return None
    r = load(p)
    lk = r.find(".//Lookup")
    d = {
        "name": name,
        "type": lk.findtext("Type"),
        "from": lk.findtext("FromState"),
        "to": lk.findtext("ToState"),
        "varient": lk.findtext("Varient") or "",
        "level": int(lk.findtext("FirstLevel") or 0),
        "shortcuts": [s.findtext("FromState") for s in lk.iter("Shortcut")],
        "flags": (r.findtext("Animation/Flags") or "").split(),
        "voc": int(r.findtext("Animation/Vocalization") or 65535),
        "damage": sum(int(a.findtext("HitPoints") or 0) for a in r.iter("Attack")),
        "hits": [ (a.findtext("HitType"), (a.findtext("Flags") or "")) for a in r.iter("Attack")],
        "throw": r.find(".//ThrowSource") is not None and len(list(r.find(".//ThrowSource"))) > 0,
    }
    _tram[name] = d
    return d

def oncc_info(cls):
    r = load(os.path.join(X, "ONCC" + cls + ".xml"))
    tr = None
    for e in r.iter():
        if e.text and e.text.strip().startswith("TRAC"):
            tr = e.text.strip(); break
    sounds = {k: (r.findtext(".//" + k) or "") for k in
              ("SuperPunchSound", "SuperKickSound", "Super3Sound", "Super4Sound")}
    return tr, sounds

ATTACK_TYPES = re.compile(r"Punch|Kick|Throw|Disarm|Slide|Flip|Getup|Tackle|Special|Taunt|Powerup")

def resolve(cls):
    tr, sounds = oncc_info(cls)
    chain, seen = [], {}
    while tr:
        parent, anims = trac(tr)
        chain.append(tr)
        for a in anims:
            t = tram(a)
            if not t: continue
            for st in [t["from"]] + t["shortcuts"]:
                key = (t["type"], st, t["varient"])
                seen.setdefault(key, []).append((tr, t))
        tr = parent
    moves = []
    for (typ, st, var), lst in seen.items():
        own = lst[0][0]
        lst = [t for c, t in lst if c == own]   # child collection shadows parents
        if not (ATTACK_TYPES.search(typ) or any("Attack" == f for f in lst[0]["flags"])): continue
        if typ.startswith("Thrown") or "Recoil" in typ or typ.startswith("Hit") or typ.startswith("Knockdown"): continue
        moves.append({"type": typ, "state": st, "varient": var, "collection": own,
                      "trams": [t["name"] for t in lst], "level": min(t["level"] for t in lst),
                      "voc": lst[0]["voc"], "damage": max(t["damage"] for t in lst),
                      "unblockable": any("Unblockable" in h[1] for t in lst for h in t["hits"]),
                      "throw": any(t["throw"] for t in lst)})
    return {"chain": chain, "sounds": sounds, "moves": sorted(moves, key=lambda m: (m["type"], m["state"]))}

if __name__ == "__main__":
    if len(sys.argv) not in (2, 3): sys.exit(__doc__)
    X = sys.argv[1]
    roster = ROSTER
    if len(sys.argv) == 3:
        roster = [tuple(x.split("=", 1)) for x in sys.argv[2].split(",")]
    out = {label: dict(resolve(c), cls=c) for c, label in roster}
    json.dump(out, sys.stdout, indent=1)
