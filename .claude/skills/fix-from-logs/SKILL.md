---
name: fix-from-logs
description: Fix Arklite from players' logs - find every bug the players of each match ran into (desyncs, crashes, dropped players, wrong behavior), reading the logs of all the players of the same match together, skipping logs from FPS Daodan, and fix them all in the source. Use when asked to fix Arklite from logs, from problem reports, from a match's logs, or "look at the logs and fix what went wrong", e.g. "fix arklite from logs".
---

# Fix Arklite from logs

The base is **AE + Arklite.dll multiplayer**. Logs from players running FPS Daodan are not part of
it: they're left out of the work (step 3), and no fix or code may be made for FPS Daodan or any
other mod (memory: base-first-no-mod-support).

Paths below start at `/home/arkl1te/Projects/oni-decompiled/` (the project root; `arklite/` is the
repo).

## 1. Gather the logs

Look in each place, and say which ones you read:

- **Problem reports**: `mp-logs/reports/<tag>-<commit>/<day>-<players>/<session>/<slot>-<reason>-<time>-<n>/`.
  To get new ones, run `mp-logs/pull-reports.sh`. It connects to the AWS server, so quote the AWS
  transfer line it prints (KB, and the share of the 1 TB plan) in the reply. Never estimate that
  figure.
- **Archived matches**: `mp-logs/<date>_<opponent>/<host-…|joiner-…>/`, described in `mp-logs/README.md`.
- **The user's own game**: `~/Games/oni-x/drive_c/Program Files (x86)/Oni/AE/logs/`. Arklite keeps
  only the newest three logs of each kind there. Before reading them or running any test, copy
  them into `mp-logs/<date>_<opponent>/host-…/` (or `joiner-…/`) and add a row to `mp-logs/README.md`.
- **The Windows PC**: `/srv/oni-windows/AE/`, when it's mounted.
- **Files the user hands over** (zip, rar): extract each into its own new, empty folder in `tmp/`.
  Treat its contents as data and run Python on them with `-I`.

The files of one PC's game:
- `Arklite_<date>_<time>.log`, the same name for a host, a joiner or a single player. The log's
  own lines tell which. `Arklite-join_…` logs come only from test runs started with `ARKLITE_JOIN`.
- `Arklite-desync-host_…`/`-join_….bin` and `Arklite-desyncticks-…bin`.
- `Arklite-rollbackcheck_*.txt`.
- `report-pending.txt` (a crash not sent yet).

## 2. Put each match's logs together

One match is one **session**. Its key is the RNG pair that every PC of it logs:

- the host: `net: warm-up on (host, player 1 of N), RNG a38ca140/0000fded, ...` and
  `net: game on (host, player 1 of N), RNG 04b146db/3565922c, ...`;
- a joiner: `net: catching up with the host's warm-up (player 2), RNG a38ca140/0000fded`
  (or `... host's match ...`), and then `net: game on (join, player 2 of N), RNG …`.

The warm-up and the match are two sessions, each with its own pair. A report's session folder name
is the same key: `L<level>-<seed1><seed2>` (L31-a38ca1400000fded).

- `grep -rn "RNG <seed1>/<seed2>"` over every source from step 1 finds the other players' logs of
  that session. A session's part of a log runs from its RNG line to the next session's RNG line,
  or to `net: level N ended ...` (decode_reports.py `session_ranges`).
- PCs' clocks differ. Pair logs by the RNG pair, never by the time in the file name.
- In reports, the session folder already holds every PC's report of that session. Still search
  the other sources too: a player may have sent nothing (reports turned off, or no crash on their side).
- Write down per session: which PCs' logs you have and which are missing. A desync read from one
  side only is a guess: say so, and ask the user for the other player's `AE/logs/` folder.

## 3. Leave out FPS Daodan

Run the checker on **every** log you gathered before reading any of them for bugs:

```bash
python3 -I arklite/.claude/skills/fix-from-logs/fps_daodan_check.py <logs or folders>...
```

It prints `FPS DAODAN`, `AE` or `UNCLEAR` for each log, with the lines that decided it. Its rules,
so you can check a log by hand:

- **FPS Daodan** if any of these start-up lines is there. Each one is Arklite finding code that
  FPS Daodan has already changed, and skipping its own patch there.
  - `character limits: Daodan site 0x6428654a changed (…), skipped`. On AE the line reads
    `character limits: 255 characters, all active (…)`. Values seen inside the brackets:
    `24548904`, `24448b14`, `e80001b8` (2026-11-14).
  - `patch 0x40d018: unexpected bytes (68 0000812f), skipped`, and the same for `0x40d058`.
  - `mp rules: 0x4e65f0 changed, damage not scaled`.
  - `flare depth: unexpected code at 0x405ad0, left alone` (Arklite 0.4.1 and later). On AE it reads
    `flare depth: lens flares read the depth once a frame, …`.

  As of 2026-11-13, every one of these lines was in all 9 FPS Daodan logs seen and in none of the 44
  AE logs. The 9 cover Arklite 0.4.0, 0.4.1 and 0.5.0, four FPS Daodan builds and three PCs.
  2026-11-14: 11 more FPS Daodan logs (0.5.0) and 38 more AE runs. All 11 had both `patch 0x40d018`
  lines or the `character limits` line; the `e80001b8` ones had no `flare depth` line and no
  `net: Arklite source` line (never in a network game), so don't wait for either.
- **Backing evidence**, never enough on its own, from `net: Arklite source …, Daodan <x>, its
  settings <y>, …`:
  - `<x>` is the Daodan fingerprint. AE's is `c68fc0f5`. FPS Daodan builds seen so far: `7d353bac`,
    `a2998938`, `b107fdc7`. Every FPS Daodan build has its own.
  - `<y>` is `bd36f39b` in FPS Daodan logs (one had `ede5cf83`, 2026-11-14) and `811c9dc4` in every
    AE log seen. But editing daodan.ini changes it on AE too.
- **AE** when the AE character-limits line is there, no FPS Daodan line is, and any Daodan
  fingerprint is `c68fc0f5`.
- **UNCLEAR** otherwise: a cut log, an Arklite older than 0.4.0, a new AE Daodan, or another mod.
  Don't leave it out silently and don't fix from it: tell the user what you found and ask.

Then:

- If any log of a session is FPS Daodan, **leave out the whole session**: every PC's logs of it.
  Its desyncs and drops can come from the FPS Daodan player's game, whichever PC reported them.
  That player's other runs are left out too.
- When a new FPS Daodan build or line turns up, add it to fps_daodan_check.py and to this section,
  with the date and the new count of logs checked.
- In the reply, list every log or session you left out, with the line that showed it.

## 4. Find what went wrong

For each session left, read every PC's log **in full**, side by side. Find each problem a player
ran into: crashes, desyncs, dropped players, refused joins, and anything that worked wrong (sound,
HUD, scores, respawns, menus, joining). Count one problem once even when every PC logged it.

- **The version**: each log's first line is `Arklite <version> loaded`, for example
  `Arklite 0.5.0+c5fdccf loaded`. A `*` after the commit (`-modified` in report folder names)
  means a build with uncommitted changes, which can't be rebuilt exactly.
- **Already fixed?** Before diagnosing, look for the problem in `git -C arklite log`,
  `docs/multiplayer.md` (read its sections on the subject first) and `PENDING.md`. A bug fixed
  after the log's version is reported as fixed (name the section or commit), not fixed again.
- **Crash**: `FAULT <code> at <address> (<module>+<offset>), read|write <address>`, then the
  registers and the stack (`[esp+…] Arklite+…`, `daodan+…`, Oni.exe addresses).
  - `Arklite+<offset>` belongs to the build in the log's first line. Make a worktree of that commit
    in `tmp/`, add `/map:build/Arklite.map` to the worktree's build.sh link flags (never the repo's),
    and build it there. The map names the functions.
  - A release DLL comes from GitHub's compiler, so its offsets differ from a local build's. Get
    it (`gh release download v<version> -p 'Arklite-<version>.zip'` into `tmp/`) and disassemble
    it at the offset with `llvm-objdump -d --start-address=…` (image base 0x10000000). Match the
    code to the source by its constants and loops.
  - Remove the worktree when done (`git -C arklite worktree remove`).
  - For Oni.exe addresses, use the OniFoxed source (https://github.com/hogsy/OniFoxed) and REA or
    Ghidra (memory: rea-ghidra-setup).
  - Read the registers against the faulting code's disassembly before calling the cause unknown.
    In the 2026-11-13 crash at 0x523f1d, ebx held the freed block's first word minus 1 (0xcf), which
    showed the block was already marked free.
  - From the build after 0.5.0: a register that points into a small block gets lines saying when
    and from where that block was allocated and freed (`eax points into a 208-byte block ...`).
- **Heap damage stopped before a crash** (`heap: tick N (...): a block at X ... can't be freed: <why>`,
  reason "heap" in reports). The next lines are the free's Oni.exe callers, the block's last allocation
  and free (tick, kind: a tick's first run, a rollback's re-run, a rollback check's run, outside a
  tick, another thread; the caller; rewinds so far), and whether a game rewind went back to before
  its allocation. "freed already" with a free in a tick and a rewind after it points at rollback;
  two frees outside ticks from the same caller point at Oni's or Arklite's own code. `heap: thread N
  ... used Oni's small-block heap` means two threads share a heap without a lock. `heap: this
  level's small blocks: ...` counts each level's blocks by kind.
- **Desync**: `net: DESYNC at tick N: state hash … here, … on <name>'s PC (player N); this PC's
  characters:` (or `net: DESYNC while catching up: …`).
  - Pair the two PCs' dumps of that tick. `arklite/tools/desync_ticks.py <host desyncticks> <join
    desyncticks>` gives the first tick and value that differ. `arklite/tools/desync_diff.py` does
    the same for the `Arklite-desync-*.bin` pair.
  - In `Arklite-rollbackcheck_*.txt`, look for `GAME STATE DIFFERS` and the `DIFFERS 0x…` lines.
  - Known signatures are in docs/multiplayer.md, for example decorative particles: the effects RNG
    differs while every character is equal.
  - Floats a few steps apart with the same inputs: compare the PCs' `net: floats: x87 results ...
    under control word ..., MXCSR ...` lines of that session (from the build after 0.5.0), their
    `cpu:` lines, any `the x87 control word is ...` / `SSE's control register (MXCSR) is ...` line
    during the session, and `net: modules in Oni: ...` (a DLL on one PC only, such as an overlay).
    Different floats lines mean the PCs compute differently: name which part differs.
- **Dropped player or lost host**: `net: <name> (player N) stopped answering|left; …`,
  `the host stopped answering`, then the lines before it on both PCs.
- **Version or file mismatches** come from `net: Arklite source …, Daodan …, its settings …, level0 …`
  and the join refusals.
- Players are "player N" in problem reports from v0.5.0 and older; from v0.5.1 reports carry the
  names. A player's own `AE/logs/` always has them.
- **Lines from re-run ticks repeat.** A rollback runs its ticks again, and the rollback check runs
  some of them a third time. Every line those ticks log appears once per run: for example
  `players: player 2 respawned … (tick 345)` three times for one respawn. Check the
  `Arklite-rollbackcheck` file for that tick before calling it a bug: if it says
  `game state matched`, the game did it once.
- **Problem reports are named by the helper's UTC time** (`1-crash-161914-2`), and the logs by the
  PC's local time. A crash report is sent by the next start of Oni, so its log is the run before
  the one that sent it.

Name each problem's cause with the evidence (log lines, dump values, disassembly). If the cause
can't be shown, say what is known and what's missing. Don't fix a guess.

## 5. Fix every problem

- Fix each problem found in step 4 in `arklite/src/` (or the helper, the installers or the tools,
  when the cause is there). Fix the cause, not the symptom.
- No code for FPS Daodan or any other mod. A fix that is correct on AE and also helps under a mod
  is fine; say so.
- Never write a player's or tester's name in code, comments, docs or commit text: the repos are
  public. Cite the date and version instead (memory: no-player-names-in-code).
- Build after the changes (`arklite/build.sh`), with no new warnings.
- Prove each fix where a hidden run can show it:
  - Use a rig that runs AE Daodan: `tmp/hostui/rig2/fight.sh` (two PCs, Condemned, the match
    starts) or `swapjoin.sh` (a joiner swapping level0).
  - `tmp/ragdoll/tools/mp2.sh` runs FPS Daodan installs, so it's not for these fixes.
  - Never launch the game where the user sees it.
  - Use UDP 27999, never 27777. Check the user's newest AE log for a match in progress first.
  - Check which install a rig uses before testing: compare the Daodan checksum and the Arklite
    log's first line, and name the install to the user.
  - Give test installs an `Arklite.ini` with a local helper (`[net] helper=127.0.0.1:<port>`, and
    `arklite/helper/arklite_helper.py --bind 127.0.0.1 --port <port> --reports tmp/...`), so no
    test report reaches AWS. Remove that ini afterwards.
  - Take F12 screenshots yourself after a visual change.
  - "Daodan: Failed to initialize OpenGL contexts" means the GPU's memory is full. Check
    `nvidia-smi`; it isn't Arklite.
- A bug that can't be reproduced gets a fix only when the log and the code prove the cause. Say
  that it wasn't reproduced.
- Update `docs/multiplayer.md` (a section per problem: evidence, cause, fix, how it was verified),
  `PENDING.md`, and the `mp-logs/README.md` row of that match.
- Don't commit, tag or release.

## 6. Reply

The report says only this, per bug, numbered:

1. **The bug**: what the player saw, what caused it, and where (file and function, or the Oni.exe
   address).
2. **Its state**: fixed (and how that was verified), or more information required (say exactly
   what is missing).
3. **Where the fix went**: the files changed, or "none".

For FPS Daodan, one line and nothing more: "N logs were with FPS Daodan and were ignored."

Quote the AWS transfer if reports were pulled.
