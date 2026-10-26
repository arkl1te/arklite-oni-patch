# Arklite: recording, replaying and network tests

Paths below are this setup's (Oni under Wine in `~/Games/oni-x`). Adjust `AE` and `WINEPREFIX`
for another install. `env VAR=value command` works in fish and bash alike. Start Oni from a
terminal: Catwalk's Play button doesn't pass these variables.

```sh
# fish
set -x AE "/home/arkl1te/Games/oni-x/drive_c/Program Files (x86)/Oni/AE"
set -x WINEPREFIX /home/arkl1te/Games/oni-x
# bash
export AE="/home/arkl1te/Games/oni-x/drive_c/Program Files (x86)/Oni/AE"
export WINEPREFIX=/home/arkl1te/Games/oni-x

cd "$AE"
```

Maps are folders in `$AE/GameDataFolder/CustomLevels` (e.g. `NewLevel-Versus`, `StalwartXL`);
`ARKLITE_AUTOSTART=<folder>` opens Custom Game and plays that folder directly.

## 0. Build first

```sh
cd ~/Projects/oni-decompiled/arklite && ./build.sh
```

The Linux install links `AE/Arklite.dll` and `AE/realbink.dll` to `build/`, so a build is picked
up at the next start. Another PC needs `build/Arklite.dll` copied into its AE folder. Both PCs of
a network game must run the same build (the log names the version: `Arklite 0.1.0+<commit>`).

## 1. Record and replay on one PC (determinism)

Same inputs must give the same game. Record a level, then replay it hands off:

```sh
# play a while, then quit; only the first level played is recorded
env ARKLITE_RECORD=test.akr ARKLITE_AUTOSTART=NewLevel-Versus wine Oni.exe -noswitch
cp Arklite.log Arklite-record.log         # the log is rewritten at every start

# don't touch the controls until the recording runs out
env ARKLITE_REPLAY=test.akr ARKLITE_AUTOSTART=NewLevel-Versus wine Oni.exe -noswitch
cp Arklite.log Arklite-replay.log
grep -E "MATCHED|DESYNC" Arklite-replay.log
```

- The `.akr` goes in `AE/`.
- `MATCHED` = deterministic. `DESYNCED` gives the first tick that differs and the fields that
  differ.
- Keep each map version in its own folder: a recording only replays on the exact same level.
- With a gamepad on the same PC, player 2 is in the recording too (two players without a
  network).

To compare two recordings of the same level tick by tick:

```sh
python3 ~/Projects/oni-decompiled/arklite/tools/akr_diff.py first.akr second.akr [ticks]
```

## 2. Network game (two Oni windows on one PC, or two PCs)

Host (plays Konoko) and joiner (plays player 2, the second IsPlayer character):

```sh
env ARKLITE_HOST=1 ARKLITE_AUTOSTART=NewLevel-Versus wine Oni.exe -noswitch
# same PC: 127.0.0.1; another PC: the host's LAN IP, e.g. 192.168.1.10
env ARKLITE_JOIN=127.0.0.1 ARKLITE_AUTOSTART=NewLevel-Versus wine Oni.exe -noswitch
```

Windows joiner, in `cmd.exe` (not PowerShell), from the AE folder:

```bat
set ARKLITE_JOIN=192.168.1.10
set ARKLITE_AUTOSTART=NewLevel-Versus
Oni.exe -noswitch
```

Or put `join=192.168.1.10` under `[net]` in `AE\Arklite.ini` and start Oni as usual; empty it
again for single player.

The level starts as a lobby: **F3** = ready. With everyone ready a 7 s countdown runs, then both
PCs reload the level and the synced match starts. The host needs UDP port 27777 open.

By default the match uses rollback (`[net] delay=0`, the host's setting counts): no input delay,
and the log says `net: rollback on` and, every 30 s, how many corrections ran. Rollback matches
aren't recorded. For a recorded lockstep match (to track a desync with the tools below), set
`delay=4` under `[net]` in the host's `Arklite.ini`.

Files after a network game (in `AE/` of each PC):

| File | Contents |
| --- | --- |
| `Arklite.log` / `Arklite-join.log` | `net:` lines; `DESYNC` if the games came apart |
| `Arklite-net-host.akr` / `Arklite-net-join.akr` | each PC's recording of the match (lockstep only) |
| `Arklite-net-host.col` / `Arklite-net-join.col` | each PC's collision log: every in-tick collision query and character move step |
| `Arklite-desync-host.bin` / `-join.bin` | all characters at the first desync |

Compare them (copy the joiner's files next to the host's first):

```sh
grep -E "DESYNC|state hashes|respawned|changed" Arklite.log Arklite-join.log
python3 ~/Projects/oni-decompiled/arklite/tools/akr_diff.py Arklite-net-host.akr Arklite-net-join.akr
python3 ~/Projects/oni-decompiled/arklite/tools/desync_diff.py Arklite-desync-host.bin Arklite-desync-join.bin
python3 ~/Projects/oni-decompiled/arklite/tools/col_diff.py Arklite-net-host.col Arklite-net-join.col
```

At the end of a level each log says how many state hashes were compared and how many differed
(`state hashes: N compared, 0 differed` = in step all along).

## 2b. Rollback tests (one PC, no network)

Don't press Esc (the pause menu runs inside a tick) and don't use a gamepad (player 2's input
isn't kept per tick there). Play actively until the log says the survey or the test is done.

```sh
env ARKLITE_AUTOSTART=StalwartXL ARKLITE_MEMSURVEY=20 wine Oni.exe -noswitch       # Arklite-memsurvey.txt
env ARKLITE_AUTOSTART=StalwartXL ARKLITE_ROLLBACK_TEST=20 wine Oni.exe -noswitch   # Arklite-rollbacktest.txt
```

The rewind test logs `game state MATCHED` or `DIFFERS` per sample and `ALL MATCHED` at the end;
the report lists differing characters and pages with their owners.

## 3. Checklist for the F1 / F2 changes

**F1** (help screen) in a multiplayer game: the sound must stay at full volume.

**F2** (character menu), in the lobby and in the match:

- [ ] The pictures: upright, facing the camera, textured; the background darkened.
- [ ] Choose a character anywhere, also mid-fight, mid-air and holding a weapon: you change at
      once, keeping your position, health, weapon and items. Log:
      `players: Konoko changed to <class>, health X/Y (tick T)`.
- [ ] Choose "the map's character" back (or another one): you change again.
- [ ] Choose while dead: after the respawn you are the new character with the map's health.
      Log: `... respawned at spawn point N as <class>`.
- [ ] Both PCs' logs show the same lines with the same ticks, and no `DESYNC`.
- [ ] Each character: pick up, fire, reload and drop a weapon; punch/kick combos; jump, crouch,
      roll; throw and be thrown; hypo; its special moves (Ninja, Mutant Muro, Striker...).
      Watch for frozen or wrong poses (moves the class doesn't have) and for desyncs.
- [ ] Open F2 in the middle of a fight: your player stands, the game goes on, Esc closes the
      menu without opening the pause menu.

The same test on one PC without a network: record with a gamepad as player 2 (section 1), use
F2 and respawns, then replay; the replay must say `MATCHED`.
