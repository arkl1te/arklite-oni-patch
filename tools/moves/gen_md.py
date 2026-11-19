#!/usr/bin/python3 -I
"""Writes moves.md from read_moves.py's JSON (make.sh runs it).

    gen_md.py <moves.json> [<a package's moves.json>] > moves.md

Names are only the game's own: Konoko's diary move names, the super-move shouts (class
SuperPunch/SuperKick sounds, subtitles from the wiki's speech transcript) and Tanker's wiki move
list where the move is certain. A package's sets (read_moves.py with its own class list, labels
"Konoko: ...") come right after the character they change. gen_table.py uses make() too.
"""
import json, sys

# super-move shouts, by sound (OSBD) name -> what is shouted (wiki Quotes/Speech)
SHOUT = {
    "c18_79_14konoko": "Rising Fury", "c18_79_15konoko": "Devil Spin Kick",
    "c18_79_16barabas": "Earthquaker", "c18_79_01striker": "Striker Slam",
    "c18_79_04elite": "Cannonball", "c18_79_02red": "Whirl Kick", "c18_79_03red": "Ten Shadow Punch",
    "c18_79_05tanker": "Nuclear Tackle", "c18_79_09tanker": "Blockbuster",
    "c18_79_10comguy": "Static Fist", "c18_79_11blackops": "Lion Fist", "c18_79_12blackops": "Lion Axe",
}
# Konoko's diary (Data Comlink) move names, by animation
DIARY = {
    "TRAMKONCOMcomb_p_p_p": "Triple-Hit Haymaker", "TRAMKONCOMcomb_k_k_k": "Spinning Sidekick",
    "TRAMKONCOMbk_fw_kick": "Willow Kick", "TRAMKONCOMcomb_k_k_kfw": "Crescent Moon Kick",
    "TRAMKONCOMkick_heavy": "Devil Spin Kick", "TRAMKONCOMpunch_heavy": "Rising Fury Punch",
    "TRAMKONCOMlt_fw_kick": "Twister Kick", "TRAMKONCOMrt_fw_kick": "Twister Kick",
    "TRAMKONCOMcomb_p_p_k": "Sledgehammer Heel",
}
DIARY_TYPES = {"ThrowForwardPunch": "Throw", "ThrowForwardKick": "Throw", "ThrowBackwardKick": "Backbreaker",
               "RunThrowForwardPunch": "Running Lariat", "RunThrowForwardKick": "Running Throw",
               "Slide": "Slide", "Flip": "Jump Flip"}

FALL = ["Falling", "FallingForward", "FallingBack", "FallingLeft", "FallingRight"]
RUN = ["RunStart", "RunningLeftDown", "RunningRightDown"]
RUNBK = ["RunBackStart", "RunningBackLeftDown", "RunningBackRightDown"]
SSL = ["SidestepLeftLeftDown", "SidestepLeftRightDown"]
SSR = ["SidestepRightLeftDown", "SidestepRightRightDown"]
GROUND = ["FallenFront", "FallenBack", "LieBack"]
CR = ["Crouch", "CrouchWalk", "CrouchRunLeft", "CrouchRunRight"]

# (input, [(type, states)] tried in order, like the game's combo table)
ROWS = [
    ("Punch", [("Punch", ["Standing"])]),
    ("Punch, punch", [("Punch2", ["Standing"])]),
    ("Punch, punch, punch", [("Punch3", ["Standing"])]),
    ("Punch ×4", [("Punch4", ["Standing"])]),
    ("Kick", [("Kick", ["Standing"])]),
    ("Kick, kick", [("Kick2", ["Standing"])]),
    ("Kick, kick, kick", [("Kick3", ["Standing"])]),
    ("Kick, kick, then forward + kick", [("Kick3Fw", ["Standing"])]),
    ("Punch, punch, kick", [("PPK", ["Standing"])]),
    ("Punch, punch, kick, kick", [("PPKK", ["Standing"])]),
    ("Punch, punch, kick ×3", [("PPKKK", ["Standing"])]),
    ("Punch, punch, kick ×4", [("PPKKKK", ["Standing"])]),
    ("Punch, kick", [("PK", ["Standing"])]),
    ("Punch, kick, punch", [("PKP", ["Standing"])]),
    ("Punch, kick, kick", [("PKK", ["Standing"])]),
    ("Kick, punch", [("KP", ["Standing"])]),
    ("Kick, punch, punch", [("KPP", ["Standing"])]),
    ("Kick, punch, kick", [("KPK", ["Standing"])]),
    ("Kick, kick, punch", [("KKP", ["Standing"])]),
    ("Forward + punch", [("PunchForward", ["Standing"])]),
    ("Back + punch", [("PunchBack", ["Standing"])]),
    ("Left + punch", [("PunchLeft", ["Standing"])]),
    ("Right + punch", [("PunchRight", ["Standing"])]),
    ("Crouch + punch", [("PunchLow", ["Standing", "CrouchStart"])]),
    ("Forward + kick", [("KickForward", ["Standing"])]),
    ("Back + kick", [("KickBack", ["Standing"])]),
    ("Left + kick", [("KickLeft", ["Standing"])]),
    ("Right + kick", [("KickRight", ["Standing"])]),
    ("Crouch + kick", [("KickLow", ["Standing", "CrouchStart"])]),
    ("Crouch, let go of crouch, punch", [("Punch", ["CrouchStart"])]),
    ("Crouch, let go of crouch, kick", [("Kick", ["CrouchStart"])]),
    ("Back, then forward + kick", [("KickForward", ["RunBackStart"])]),
    ("Left or right, then forward + kick", [("KickForward", ["SidestepLeftStart", "SidestepRightStart"])]),
    ("Crouched: punch", [("PunchLow", ["Crouch"]), ("Punch", ["Crouch"])]),
    ("Crouched: kick", [("KickLow", ["Crouch"]), ("Kick", ["Crouch"])]),
    ("Crouched: forward + punch", [("PunchForward", CR)]),
    ("Crouched: forward + kick", [("KickForward", CR)]),
    ("Running: punch", [("RunPunch", RUN)]),
    ("Running: kick", [("RunKick", RUN)]),
    ("Running backwards: punch", [("RunBackPunch", RUNBK)]),
    ("Running backwards: kick", [("RunBackKick", RUNBK)]),
    ("Sidestepping left: punch", [("SidestepLeftPunch", SSL), ("Punch", SSL)]),
    ("Sidestepping left: kick", [("SidestepLeftKick", SSL)]),
    ("Sidestepping right: punch", [("SidestepRightPunch", SSR), ("Punch", SSR)]),
    ("Sidestepping right: kick", [("SidestepRightKick", SSR)]),
    ("Facing an opponent, up close: forward + punch", [("ThrowForwardPunch", ["Standing"])]),
    ("Facing an opponent, up close: forward + kick", [("ThrowForwardKick", ["Standing"])]),
    ("Behind an opponent: forward + punch", [("ThrowBackwardPunch", ["Standing"])]),
    ("Behind an opponent: forward + kick", [("ThrowBackwardKick", ["Standing"])]),
    ("Running into an opponent: punch", [("RunThrowForwardPunch", RUN)]),
    ("Running into an opponent: kick", [("RunThrowForwardKick", RUN)]),
    ("Running into an opponent's back: punch", [("RunThrowBackwardPunch", RUN)]),
    ("Running into an opponent's back: kick", [("RunThrowBackwardKick", RUN)]),
    ("In the air: punch", [("Punch", FALL)]),
    ("In the air: kick", [("Kick", FALL)]),
    ("In the air: back + punch", [("PunchBack", FALL)]),
    ("In the air: back + kick", [("KickBack", FALL)]),
    ("Jump, then crouch in the air", [("Flip", FALL)]),
    ("Dash (double-tap forward), then crouch", [("Slide", RUN + RUNBK + SSL + SSR)]),
    ("On the ground: punch", [("Punch", GROUND)]),
    ("On the ground: kick", [("Kick", GROUND)]),
    ("On the ground: back + kick", [("GetupKickBack", GROUND)]),
    ("Holding a rifle: punch", [("PunchRifle", ["Standing"])]),
    ("Action (taunt)", [("Taunt", ["Standing"])]),
]

# A combo only follows the attack before it (the game's combo table, Oni_Combos.c): the row's move
# type -> the type the last attack must have had (any move of that type, in any situation, sets it)
PREREQ = {"Punch2": "Punch", "Punch3": "Punch2", "Punch4": "Punch3", "Kick2": "Kick", "Kick3": "Kick2",
          "Kick3Fw": "Kick2", "PPK": "Punch2", "PPKK": "PPK", "PPKKK": "PPKK", "PPKKKK": "PPKKK",
          "PK": "Punch", "PKP": "PK", "PKK": "PK", "KP": "Kick", "KPP": "KP", "KPK": "KP", "KKP": "Kick2"}

def make(moves_json, package_json=None):
    """moves.md's text, and what gen_table.py needs: {"d", "name_of", "reachable"}"""
    d = json.load(open(moves_json))
    if package_json:
        merged = {}
        alt = json.load(open(package_json))
        for k, v in d.items():
            merged[k] = v
            for ak, av in alt.items():
                if ak.split(":")[0] == k: merged[ak] = av
        d = merged

    # animation -> name, from the class whose shout or diary it is (shared animations keep the name)
    name_of = dict(DIARY)
    for who, v in d.items():
        for m in v["moves"]:
            if who.split(":")[0] == "Konoko" and m["type"] in DIARY_TYPES and m["varient"] in ("", "Combat"):   # (a package's Konoko too)
                for t in m["trams"]: name_of.setdefault(t, DIARY_TYPES[m["type"]])
    for who, v in d.items():            # a class's own shout names its own animation first
        for m in v["moves"]:
            snd = {6: "SuperPunchSound", 7: "SuperKickSound"}.get(m["voc"])
            if snd and v["sounds"].get(snd) in SHOUT and m["trams"][0] not in DIARY:
                name_of.setdefault(m["trams"][0], SHOUT[v["sounds"][snd]])

    def shout_for(who, m):
        snd = {6: "SuperPunchSound", 7: "SuperKickSound"}.get(m["voc"])
        return SHOUT.get(d[who]["sounds"].get(snd or "", ""), "")

    def find(who, typ, states):
        for st in states:   # in the row's order, like Arklite's /moves (chat.c)
            for m in d[who]["moves"]:
                if m["type"] == typ and m["state"] == st and m["varient"] in ("", "Combat", "RightRifle", "LeftRifle") \
                        and (typ == "PunchRifle" or m["varient"] in ("", "Combat")):
                    return m
        return None

    def row(who, alts):
        for typ, states in alts:
            m = find(who, typ, states)
            if m: return m
        return None

    def reachable(who):
        """input -> move, for the rows the character has and can reach (a combo needs its first part)"""
        got = {inp: row(who, alts) for inp, alts in ROWS}
        got = {k: m for k, m in got.items() if m}
        while True:
            types = {m["type"] for m in got.values()}
            drop = [k for k, m in got.items() if m["type"] in PREREQ and PREREQ[m["type"]] not in types]
            if not drop: return got
            for k in drop: del got[k]

    def name(who, m):
        s = shout_for(who, m)
        n = name_of.get(m["trams"][0], "")
        if s and n and s != n and not n.startswith(s): return f"{n} (shouts {s})"
        return n or s

    def dmg(m):
        if m["type"].startswith(("Throw", "RunThrow", "Taunt")) or m["damage"] == 0: return "—"
        return str(m["damage"]) + (", unblockable" if m["unblockable"] else "")

    ORDER = list(d.keys())
    SAME = {}   # identical move set (same collection chain) -> first class
    for who in ORDER:
        key = tuple(d[who]["chain"])
        SAME.setdefault(key, who)

    out = []
    w = out.append
    w("# Moves\n")
    w("Every hand-to-hand move of the characters in the F2 menu (Arklite's default roster), read from the "
      "game's own animation data (level0). Each character's moves come from its animation set and the sets it "
      "inherits (listed under its name); a move the character's own set has wins over an inherited one. "
      "In the game, /moves reads the same from the character's loaded animations, so it also lists the moves a "
      "mod adds (a mod's new animations have no name).\n")
    w("Names are the game's own only: Konoko's move list from her diary, the name a character shouts with a "
      "super move, and Tanker's move list from the Oni wiki. A move that shares an animation with a named one "
      "keeps that name, even on another character. Moves without a name have none in the game.\n")
    w("Damage is the move's base damage before multiplayer's ×1.25, summed over all its hits; throws do their "
      "damage through the thrown character's fall, so they show —. *Unblockable* hits can't be blocked.\n")
    w("Inputs: *forward/back/left/right* are the movement keys. \"Crouch, let go of crouch, punch\" means press "
      "crouch, release it and press punch right away. \"Back, then forward + kick\" means tap back, then press "
      "forward and kick together.\n")
    w("For everyone: no key blocks (stand still facing the attacker; crouch for low attacks), and crouch + any "
      "direction is an escape roll (the diary's Escape Moves). Up close against an armed opponent, the throws "
      "with forward + punch or forward + kick take the weapon (the diary's Disarm and Stepping Disarm).\n")

    for who in ORDER:
        v = d[who]
        first = SAME[tuple(v["chain"])]
        w(f"## {who}\n")
        sets = " → ".join(c.replace("TRAC", "").replace("_animations", "") for c in v["chain"])
        if first != who:
            w(f"Same animation sets as {first} ({sets}), so the same moves as listed there.\n")
            shouts = [SHOUT[s] for s in v["sounds"].values() if s in SHOUT]
            if shouts: w(f"Its own shouts: {', '.join(shouts)}.\n")
            continue
        w(f"Animation sets: {sets}.\n")
        w("| Input | Move | Damage |")
        w("|---|---|---|")
        for inp, alts in ROWS:
            m = reachable(who).get(inp)
            if m: w(f"| {inp} | {name(who, m)} | {dmg(m)} |")
            if inp == "Crouch, let go of crouch, kick" and any(x["type"] == "Powerup" and x["state"] == "Standing" for x in v["moves"]):
                w("| Crouch, let go of crouch, Action |  | — (regenerates 8% of his health a second up to full; "
                  "the pose holds until any key, or until hits take 5%; the mouse turns only the camera) |")
        w("")

    w("## Named in the wiki, not matched to an input\n")
    w("- Tanker: Washing Machine, Headbutt Disarm, Corporate Elbow, Full Body Massage, Face Slam, Headbutt "
      "(throws and grabs; the wiki doesn't say which button starts which).")
    w("- Striker: Headbutt dash.\n")
    w("## Not for players\n")
    w("The Striker and TCTF SWAT standing heavy punch and kick, and each character's "
      "scripted moves (cutscenes, consoles) have no button: only the AI or a level script plays them.\n")
    return "\n".join(out), {"d": d, "name_of": name_of, "reachable": reachable}

if __name__ == "__main__":
    if len(sys.argv) not in (2, 3): sys.exit(__doc__)
    sys.stdout.write(make(*sys.argv[1:])[0])
