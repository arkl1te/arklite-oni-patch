# Arklite for Oni

Quality-of-life patches and multiplayer groundwork for [Oni](https://oni.bungie.org/)'s
Anniversary Edition, in one DLL. Arklite doesn't change any game file: it patches Oni in memory
when the game starts, and checks every patch site first, so a patch that doesn't match is skipped
and written to `AE/Arklite.log`.

Arklite is an unofficial fan project. It isn't affiliated with or endorsed by Bungie, Take-Two
Interactive or Rockstar, and it contains no Oni code or data: you need your own copy of Oni.

## What it offers

| Feature | What it does |
| --- | --- |
| Custom Game | A **Custom Game** button on the main menu lists and starts custom levels from `AE/GameDataFolder/CustomLevels/`. Levels don't need a unique level number. See [Custom Game](#custom-game). |
| Options > More... | A **More...** button next to Close opens a second page of options (below). |
| HUD scale | **HUD Scale** (50–300%) grows or shrinks the health and weapon rings toward their screen corners. The stock HUD is a fixed number of pixels, so it gets tiny at high resolutions. |
| Sound, Music and Voice volume | Options > Overall Volume becomes three sliders: **Sound Volume** (effects and ambience), **Music Volume** (music and Bink movies) and **Voice Volume** (character dialog). |
| Pause menu background | **Pause Background** sets the opacity of the black behind the pause menu (default 33%, so the paused game shows through). 100% is the original. |
| When game pauses | **Keep playing level music** (default): level music goes on under the pause menu; other sounds still pause. **Play Main Theme (original)**: the stock menu theme. **Silence**: everything pauses and nothing plays. |
| Live pause menu | Esc opens the pause menu, but the game keeps running behind it: enemies keep fighting and Konoko stands still until you resume. `live_menu=0` brings back the stock pause. See [Live pause menu](#live-pause-menu). |
| 255 characters, all active | Raises the character limit from 128 to 255 and the active-character limit from 64 to 255, so big fights no longer crash the game. See [Character limits](#character-limits). |
| No tiny strikers outside the dream lab | Randomized enemies no longer pick the dream lab's half-size striker (or its giant Shinatama). AE moved those classes into the shared `level0`, so about one random striker in four came out tiny. See [Random costumes](#random-costumes). |
| Player 2 on a gamepad | A second character with the IsPlayer flag in a level is played on an XInput gamepad, next to Konoko on keyboard and mouse. See [Player 2](#player-2-multiplayer-step-2). |
| Determinism fixes | Ambient sounds, particles and speech no longer depend on the frame rate, the sound card or what is on screen, so the same input always gives the same game. Multiplayer needs this; in normal play you won't notice a difference. |
| Input record and replay | For testing: records a level's input and replays it, checking every tick that the game came out the same. Off unless `ARKLITE_RECORD` or `ARKLITE_REPLAY` is set. See [Record and replay](#record-and-replay-multiplayer-step-1). |
| Crash log | If Oni crashes, `AE/Arklite.log` gets the registers and the stack values that point into Oni, Daodan or Arklite. |

Multiplayer over the network isn't in yet; the player 2 and record/replay features are the
first steps toward it.

## Requirements

- **Oni**, the retail PC version, installed. Arklite was made against the `Oni.exe` that AE
  installs (sha256 `363cf09684ca6b729b62f22f90701d7ebf9a33778e55b191f5040bccdcdd013d`); with
  another one, any patch that doesn't match is skipped.
- **Oni Anniversary Edition** (AE 7, AE Installer 2) with its **Daodan DLL** (v4.1), installed
  in `Oni/AE/`. Daodan is what loads Arklite. Get AE from <https://oni.bungie.org/>.
- **Windows**, or **Linux/macOS with Wine**. Arklite is a 32-bit Windows DLL, like Oni.
- Optional: an **XInput gamepad** (Xbox controller or compatible) for player 2.

## Install

1. Download `Arklite-<version>.zip` from the [Releases](../../releases) page and extract it
   anywhere (into `Oni/` or `Oni/AE/` works best: the installer finds the game on its own).
2. Close Oni.
3. Run the installer:
   - **Windows**: double-click `install.bat`. If Oni is in a protected folder such as
     `C:\Program Files (x86)`, right-click it and pick **Run as administrator**. If it can't find
     the game it asks for the AE folder; you can also pass it: `install.bat "D:\Games\Oni\AE"`.
   - **Linux/macOS (Wine)**: `./install.sh`, or `./install.sh "/path/to/drive_c/Program Files (x86)/Oni/AE"`.
     Without a path it looks in `~/.wine`, `~/Games/*` and Bottles prefixes, and next to itself.
4. Start Oni from `Oni/AE/` as usual. The main menu has a **Custom Game** button, and
   `AE/Arklite.log` lists what was patched.

The installer puts three files in `AE/`:

| File | What |
| --- | --- |
| `Arklite.dll` | The mod. |
| `realbink.dll` | Arklite's 3.5 KB loader. Daodan loads a DLL by this name, so the loader takes it. |
| `bink_orig.dll` | AE's original `realbink.dll` (the Bink video DLL), renamed. The loader forwards Bink calls to it. It isn't part of the download: the installer renames your own copy. |

It also makes `AE/GameDataFolder/CustomLevels/` and, the first time, copies `AE/persist.dat`
(your saves and settings) to `AE/persist.dat.before-arklite`.

**Updating**: run the new version's installer over the old one. **After running the AE
Installer**, run Arklite's installer again: the AE Installer may put the original
`realbink.dll` back, and Oni then starts without Arklite (nothing breaks).

## Uninstall

- **Windows**: double-click `uninstall.bat` (as administrator if Oni is in `Program Files`).
- **Linux/macOS (Wine)**: `./uninstall.sh` (or with the AE folder's path).

This removes `Arklite.dll`, puts the original `realbink.dll` back and removes
`CustomLevels/` if it's empty. Your settings (`AE/Arklite.ini`) and log are kept; pass
`/purge` (Windows) or `--purge` (Linux) to remove them too. Custom levels, recordings (`*.akr`),
`persist.dat` and `persist.dat.before-arklite` are never removed.

To uninstall by hand: close Oni, delete `AE/Arklite.dll` and `AE/realbink.dll`, then rename
`AE/bink_orig.dll` to `realbink.dll`.

## Settings

The Options dialog writes them, and you can edit `AE/Arklite.ini` with Oni closed:

| Section | Key | Default | What |
| --- | --- | --- | --- |
| `[options]` | `hud_scale` | 100 | HUD scale in percent, 50–300 |
| | `pause_background` | 33 | Opacity of the black behind the pause menu, 0–100 |
| | `pause_music` | `keep` | `keep` (level music goes on), `theme` (original menu theme) or `silence` |
| | `live_menu` | 1 | 0: the game pauses behind the pause menu, as in the original (not in a two-player game) |
| `[volume]` | `sound`, `music`, `voice` | Overall Volume | 0–100 each; a missing key starts at your old Overall Volume |
| `[players]` | `p2` | 1 | 0: no player 2 |
| | `p2_pad` | 0 | Which gamepad plays player 2, 0–3 |
| | `p2_aim_speed` | 8 | Aim per tick at full right stick |
| | `p2_invert_y` | 0 | 1: invert the right stick's up/down |

Environment variables, for testing (set them before starting `Oni.exe`):

| Variable | What |
| --- | --- |
| `ARKLITE_AUTOSTART=<folder>` | Opens Custom Game and plays `CustomLevels/<folder>` |
| `ARKLITE_SAVEPOINT=<n>` | With `ARKLITE_AUTOSTART`, starts at save point n |
| `ARKLITE_RECORD=<file>` | Records the first level played into `AE/<file>` |
| `ARKLITE_REPLAY=<file>` | Replays `AE/<file>` and writes `MATCHED` or `DESYNCED` to `AE/Arklite.log` |

On Windows: `cd "C:\Program Files (x86)\Oni\AE"`, `set ARKLITE_RECORD=test.akr`, `Oni.exe`.

## Troubleshooting

- **No Custom Game button**: open `AE/Arklite.log`. If it is missing or says
  `Arklite.dll not found`, run the installer again. Check that you started the `Oni.exe` in
  `Oni/AE/`, not the one in `Oni/`.
- **A feature does nothing**: `AE/Arklite.log` says which patches were applied. A patch is skipped
  when the code at its address isn't what Arklite expects (a different `Oni.exe` or Daodan).
- **Oni crashes**: the end of `AE/Arklite.log` has the crash registers and stack. Include
  it, and the version on its first line, when you report the problem.

## Building from source

You need `clang`, `lld` and `llvm-dlltool` (any recent LLVM; no MinGW or Visual Studio). On
Arch/CachyOS: `pacman -S clang lld llvm`; on Debian/Ubuntu: `apt install clang lld llvm`.

```sh
./build.sh      # build/Arklite.dll and build/realbink.dll
./package.sh    # dist/Arklite-<version>.zip, the release zip
./install.sh    # from the repository: links build/*.dll into AE/, so a rebuild needs no reinstall
```

The version is in `VERSION`, and Arklite writes it at the top of `AE/Arklite.log`.

**Releasing**: put the new version in `VERSION`, commit, then tag and push it
(`git tag v0.1.0 && git push origin v0.1.0`). The GitHub workflow
(`.github/workflows/build.yml`) builds every push and pull request, and for a `v*` tag
that matches `VERSION` it publishes a release with the zip attached.

The rest of this file is how each patch works, with the Oni addresses it uses.

## Pause menu music and background

The pause menu is the main menu dialog (0x96). Its init handler (0x4d2cd0, msg 0x3d) starts
the `main_menu_win` music at full volume, then fades it to 1.0 over 0.5 s with the call at
0x4d2d6a (to 0x512730). Arklite points that call at its own function. When a level is loaded
(0x5054c0 returns a level number) and the pause music isn't **Play Main Theme**, the function
fades to volume 0 over 0 s instead. The
music is still started and stopped as before, so nothing else about sound state changes.
The patch checks the original call bytes first and writes the result to `AE/Arklite.log`.

The same init handler stops every playing channel with the call at 0x4d2d4f (to 0x4835d0), and
closing the dialog plays them again (0x483660). Arklite points that call at its own loop, which
in a level with **Keep playing level music** skips the channels owned by a Music play (tagged
by the volume patch below), so the level's music goes on under the menu. Resuming calls Play on a buffer that is already playing,
which leaves it where it is. On the title screen, and with the other two choices, the original function runs.

The black behind the pause menu is not part of the dialog. Before running it, 0x4d2c70 sets
the desktop background (desktop msg 0x3c) to partspec list entry 1, `InGameBackground` =
`ps_black_none_none`. The desktop then draws that full screen over the level, which the modal
loop keeps rendering through its draw hook (0x4fb480). The desktop's paint handler draws it
with the call at 0x476f0c (to 0x4698b0) and a fixed alpha of 255. Arklite points that call at
its own function, which uses the **Pause Background** alpha (84 = 33% by default) when the
partspec is `InGameBackground` and
passes every other background through unchanged.

## More options

Controls are drawn in reverse child-list order (earlier on top), and the frames have a
translucent fill, so new controls go right after the "Quality:" label, ahead of every frame;
after a frame they would look dimmed.

The Options dialog (0x98) has no room and there is no dialog resource for a second page, so
the page is built at init (`more_options.c`): its controls are cloned from the stock ones and
start hidden, and **More...**/**Back** hides one set and shows the other. The Graphics and Sound
frames, the background and Close stay on both pages; the title reads "More options". Esc on the
More page goes back instead of closing. The dialog class proc (0x468cd0) handles Esc itself and
sends the dialog proc Close (id 2, click) with no window, while a real Close click passes the
button, so Arklite takes a windowless Close on the More page as Back. The stock radio buttons use
the grey g206 art, so the three pause-music choices are Oni checkboxes kept exclusive by the
dialog proc. A pause-music change applies the next time the game pauses.

The HUD (0x4fff50, called at 0x4fbbc4) is laid out in screen pixels by 0x504270: ring centers
half a background texture in from the bottom corners, fixed ring radii. It draws everything
through two Motoko primitives, a textured triangle (0x41a5c0, every ring arc) and a sprite
(0x41a5e0, backgrounds and pictures), with screen points in ECX. Arklite routes the 7 HUD call
sites through functions that, while the HUD is drawing, scale the points toward the screen
corner nearest the primitive. The F1 HUD help overlay and the countdown timer are not scaled.

## Volume sliders

Every channel volume goes to DirectSound through 0x485480 (21 call sites). Arklite routes
those calls through a function that scales the volume by the category of the play that owns
the channel. A play is tagged as Music when it is started by the main menu music (0x512798)
or by `sound_music_start` (0x5144de, a handler it shares with `sound_ambient_start`, so the
`sound_music_start` registration at 0x512e60 is wrapped). It is tagged as Voice when started
by `sound_dialog_play*` (0x5141f6). Everything else is Sound. The play update (0x480c20) is
wrapped too, because a new play sets its first channel volume before it stores the channel.

The old Overall Volume in `persist.dat` is no longer used. The sound init (0x51497f) gets 1.0
for the master volume, Bink movies get the Music volume, and the Options slider never writes
it. The three values (0..100) are in `AE/Arklite.ini`, section `[volume]`. Keys that are
missing start at the old Overall Volume. In the Options dialog, item 102 becomes Sound, and
Music (111) and Voice (112) are cloned from it in the Sound box.

## Random costumes

A placed character with RandomCostume (flag 0x2) or UpgradeDifficulty (0x800) gets a random class:
`ONrGameState_NewCharacter` (0x4dac50) walks every loaded ONCC through
`TMrInstance_GetDataPtr_ByNumber` (call at 0x4dae84) and keeps those whose variant name matches.
In retail, `striker_dream_lab` (variant `striker_easy`, size 0.5) and `SH_dream_lab` (Shinatama,
size 7) were only in level 13's file, and no randomized character there uses those variants. AE
put every class in `level0_Final.dat`, so they joined those pools in every level. Arklite routes
that one call through a function that swaps any `*_dream_lab` class for a stand-in whose variant
name matches nothing, so the loop skips it. The dream lab places those classes directly, so it
is unaffected. The game data is not changed.

## Character limits

Oni has 128 character slots but only 64 *active* ones (animation, physics, full AI). When the
active table is full, activation (0x4db830) fails and the character stays inactive, but the AI
update (0x489d40) still runs its state machine. An AI that goes into combat (state 5, 0x48f3e0)
then reads its active data through a null pointer: the crash at 0x48f761 when more than about 64
characters react. The 32-entry shadow pool has the same flaw: when full, its allocator (0x4fdc50)
returns 0xffff and the caller (0x4e2373) writes far out of bounds.

All of these are in the game state, one block allocated at startup (size at 0x4fc41d). Arklite
allocates a bigger one (0x46b6e0 bytes) and lets the character table (+0x1260, 0x16a0 each) grow
in place to 255. The tail that followed it moves to a new spot after it: the character count,
the active table (255 × 0x2838), the shadow pool (255 × 0x804), the three character lists
(255 entries each) and the remaining fields. That means rewriting the offset in 344 instructions,
plus 2 in Daodan's `DDrGetCharacterIndexFromName`, raising the free-slot bounds, and resizing the
level-load clears. With 255 characters, every one of them can be active and have a shadow, so
neither pool can run out.

255, not 256: a handle is `0x8000 | salt << 8 | index` with a 7-bit salt, so index 0xff could
come out as 0xffff, the "no character" handle.

`tools/gen_char_limit.py` disassembles Oni.exe and Daodan with objdump, finds every instruction
that addresses the tail, and writes `src/char_limit_sites.h`. At load, Arklite checks every site
first. If any of them differs, it patches nothing and the stock limits stay.

## Player 2 (multiplayer step 2)

Place a second character with the **IsPlayer** flag in the map (in Catwalk, copy the player
start and give it another name). Konoko stays player 1 on keyboard and mouse; the other one is
player 2, on the first XInput gamepad. The camera follows Konoko.

| Gamepad | Action |
| --- | --- |
| Left stick | forward, backward, step left, step right |
| Right stick | aim |
| A / B / X / Y | jump / crouch / punch / kick |
| RT / LT / R3 | fire1 / fire2 / fire3 |
| RB / LB / L3 | action / walk / block |
| Back | hypo |
| D-pad up / left or right / down | reload / swap / drop |

A game with player 2 is a **multiplayer game**, and in one:

- **Auto-aim is off.** Auto-aim (0x4efad0, in the tick, called at 0x4de1ed and 0x4de211) pulls the
  shooter's aim onto a hostile within 500 units that is on the local screen. Each player sees
  different characters, and with lag players aim ahead of their target instead.
- **Nothing pauses.** The pause menu is live. Any other dialog opened
  outside a tick runs live the same way (the help and objectives screen, dialog 0x46, from the
  main loop through 0x5018c0). A dialog opened inside a tick isn't shown, since a game frame
  can't run in the middle of a tick (the text console screen, dialog 0x47, from 0x504cc5). The
  game's own pause flag (gs+0x1679ac, read by the main loop through 0x4fc960) is cleared every frame.

`[players]` in `AE/Arklite.ini`: `p2=0` turns player 2 off, `p2_pad` picks the gamepad (0–3),
`p2_aim_speed` sets the aim per tick at full right stick (8; Konoko's mouse turns about 1–7
per tick), `p2_invert_y=1` inverts up/down.

How it works: AI setup (0x48a6b0) gives a Player-flagged placement `charType` 0 (+0x1e8, no AI;
AI characters are 2), and the first one is `PlayerCharacter` (gs+0xac). Oni spawns only that first
one: the level start walks the CHAR objects (0x4d0080) with 0x48a3a0, which spawns the first
IsPlayer object (+0x2c bit 0; +0x2c |= 0x20 once spawned; `ONrGameState_NewCharacter` 0x4dac50)
and stops. Arklite wraps the three walks that use it (0x48a2ee, 0x48a419, 0x48a518) and then
spawns every other IsPlayer character that isn't spawned yet. Each tick turns its
action record into the input state at gs+0xb8 (+0 aim up/down from record +0x28, +4 aim
left/right from +0x24, then buttons held, not held, pressed and released as 64-bit masks; the
buttons go through 0x4fa520, which reads gs+0xac) and gives it to the player with
`ONrCharacter_SetInput` (0x4ed420, call at 0x4fa934), which copies it into the character's
active data at +0x2158. The AI drives its characters through that same slot. Arklite wraps that
call: after Konoko, it builds player 2's input state the same way from player 2's record, with
gs+0xac pointing at player 2 while 0x4fa520 runs, and calls 0x4ed420 for player 2.

Player 2's record is made once per frame from the gamepad (the first tick of the frame gets it,
like Daodan's input does for Konoko) and goes into recordings, so record and replay cover both
players. The action bits come from the binding table at 0x52daf8 (`{u16 type, u16 index, char
name[32]}`): escape 0, console 1, forward 21, backward 22, turn 23/24, step 25/26, jump 27,
crouch 28, punch 29, kick 30, block 31, walk 32, action 33, hypo 34, reload 35, swap 36, drop
37, fire1–3 38–40; aim left/right and up/down are floats at +0x24 and +0x28.

## Live pause menu

Only in a multiplayer game (one with player 2); single player pauses exactly as stock Oni.

Stock Oni pauses when Esc opens the menu: utility input (0x4f6200, called by the tick at
0x4fa91c, which Daodan replaces) sees Esc inside the tick and calls 0x4bec00, which turns UI
mode on (0x476110) and game input off (0x403f70(0)), runs the pause menu (0x4d2c70, modal dialog
0x96), and turns both back. The modal loop (0x4689d0) keeps drawing the game through the draw
hook at 0x5e96cc (the game display 0x4fb480 while a level is loaded), but nothing updates it.

Arklite replaces 0x4bec00. In single player it runs the stock sequence at once. In a multiplayer
game the request waits until the frame's update is done (the
main loop's sound update call at 0x4d4746), so the menu never opens inside a tick. While the
menu is open, the draw hook runs a whole game frame: the (empty) input buffer, server time, the
game update through the main loop's own call (so record and replay still see it), the sound
update and the display. Options and Load Game run the same modal loop, so the game goes on
behind them too. Konoko gets no input; enemies don't wait. Utility input is skipped while the
menu is open, so the keys used in the menu don't pause or open help again. Sounds keep playing,
whatever **When game pauses** says, and the menu theme doesn't start: the menu's init would pause
sound (0x514e60(1) at 0x4d2d4a sets 0x6200a0, which every play function checks, so no new sound
could start), and Arklite skips that call while the game runs behind the menu.

The game stops running behind the menu when the level ends (gs+0x1679bc) or is left (leaving
clears the draw hook), and while the game's own pause flag is set (gs+0x1679ac), as in the main
loop.

## Record and replay (multiplayer step 1)

Multiplayer will send only inputs between PCs, so the same inputs must give the same game on
every PC. This tests that on one machine:

```fish
ARKLITE_RECORD=test.akr oni-ae-game.sh   # play a level for a while, then quit
ARKLITE_REPLAY=test.akr oni-ae-game.sh   # start the same level the same way, hands off
```

The file goes in `AE/`. Only the first level played is recorded; recording ends when you leave
or restart it. The replay starts when a level loads, feeds the recorded input in place of yours
(leave the controls alone while it runs), and gives control back when the recording runs out.
`AE/Arklite.log` then says `MATCHED` or `DESYNCED`; for a desync it gives the first tick that
differs and, for the first 3 ticks that differ, every field that differs, with its exact bits.

The game loop (0x4d4640) gets each frame's action records (0x30 bytes) from
`LIrActionBuffer_Get` (0x403be0, or Daodan's `DDrActionBuffer_Get` with its input patch) and
calls the game update (0x4fafb0, call at 0x4d472b: ECX count, EDX records, `[esp]` ticks run).
The update runs server time (gs+0x148) minus game tick (gs+0x144) ticks, at most 6, and gives
tick *i* record *i* or none: 0x4fa6a0(ECX record), call at 0x4fb133. A tick is 1/60 s. Arklite
hooks both calls. Per frame the recording keeps the tick count and the records; after every tick
it keeps the game tick, the game RNG (0x5326d4) and, for every character in use (Flags & 0x8000),
flags, health, position, facing and velocity, plus every dword of the character (0x16a0) and of
its active data (0x2838: animation, physics, AI) that changed since the last tick, and the x87
control word. The replay rebuilds those values and compares each dword, so a desync names the exact
field. Dwords that already differ at level start (pointers to memory allocated elsewhere) are left out. The replay sets server time so that the update
runs the recorded tick count, replaces the records, and compares bit for bit.

Both RNG streams start at 0xfded (0x426dad), but menus and loading advance them, so the RNG at
the level's first update goes in the file header and the replay restores it. The recording also
keeps the character state at that moment, before any tick, so the replay can tell a level that
loads differently from a tick that runs differently. The second stream
(0x5326d8) is reseeded by drawing (0x41e579), so it isn't checked. The header also keeps the x87
control word, and the replay warns if it differs.

The ambient sound players (0x480fa0, 0x481140, 0x4812a0) wait on the wall clock
(`UUrMachineTime_Sixtieths`) and then pick sounds with the game RNG (`UUrRandom`, 0x426e10), as
does the sound permutation pick (0x47f8d0); the weighted sound pick (0x480040) uses the local
RNG the same way. An ambient sound one frame earlier or later then
shifted every random number the AI drew afterwards: the first replays desynced only in the RNG,
one call apart. Arklite points those 10 calls at its own sound RNG, always, because multiplayer
needs the same on every PC.

The local RNG (`UUrLocalRandom`, 0x426e30) is meant for effects, and drawing reseeds it (0x41e500,
from the draw setup 0x41e340, in draw code that also reads the wall clock). But the particle
update (0x45c830 -> 0x45d610) runs inside the tick and uses it too, and bullets are particles:
after the sound fix, the first desync left was a swat's knockback from a bullet, one tick after
an identical hit. Arklite gives calls made inside a tick their own stream, which only ticks
advance, and leaves the original to drawing. Esc opens the pause menu from inside a tick
(utility input 0x4f6200 -> 0x4bec00 -> 0x4d2c70), and a modal dialog keeps drawing the game, so
Arklite counts every `WMrDialog_ModalBegin` call (8 sites) as outside the tick. Always on, like the
sound RNG.

A character's speech line (+0x15f0 speaking, +0x15f4 sound handle, +0x15f8 end tick, +0x15fc
priority; 0x516940 starts one, 0x516a60 updates it) ended when its sound stopped playing (the
answer of 0x511ec0 at 0x516aba, which also moves the 3D sound) or when it couldn't get a
channel (0x516b5a). Both depend on the sound card, and with 99% of an NPC fight in sync these
fields were the only difference left. Arklite ignores both: a line ends at its end tick, the
start tick plus the sound's length in ticks (0x5125c0 computes it from the sound data). The
sound handle is left out of the comparison.

Character +0x163c counts drawn frames the character has been off screen (0x4e27d0, from the
draw pass 0x4e1540) and is 0 while it's on screen. Auto-aim (0x4efad0, in the tick: hostile,
within 500 units, clear line of fire) only aims at characters with 0. The count depends on the
frame rate and on frames drawn behind the pause menu, so the recording keeps only whether it is
0. Multiplayer will need more than that, because each player's screen shows different
characters: auto-aim has to use the shooter's own view, sent with their input, or be turned off.

While recording or replaying, `UUrRandom` is hooked to keep the return address of every call
(bit 31 set for calls outside a tick). A desync with a different RNG lists the calls from the
first one that differs, so the log names the code that drew differently.

A recording's size depends on how much changes each tick, roughly 10–80 MB a minute with a few
characters fighting; the replay loads the whole file.

## Custom Game

The button lists every level in `GameDataFolder/CustomLevels/` plus any installed level whose
number isn't from the original game. The chosen level is added to the engine's level table
under a free number (31 or lower) when it is picked, so custom levels don't need a unique
level number: they aren't in `GameDataFolder` at startup, so the engine never sees two files
with the same number.

```
CustomLevels/<AnyFolderName>/
    level.ini              [level] name=Shown in the list
    levelN_Final.dat/.raw  any N; other level*.dat files (plugins) in the folder are loaded too
    IGMD/<level name>/     BSL scripts (optional; otherwise GameDataFolder/IGMD is used)
```

Known gaps: the screen reuses the Load Game dialog, so its background art still says "Load
Game"; save points made in a custom level go to the persist.dat slot it was mounted in; and
what happens when a custom level ends (its "next level") isn't handled yet. Engine addresses are
in `src/oni.h`.

For testing, `ARKLITE_AUTOSTART=<folder>` opens Custom Game and plays that folder as if
clicked, and `ARKLITE_SAVEPOINT=<n>` starts it at save point n.

## Loading

Oni → `binkw32.dll` (Daodan) → `realbink.dll` (Arklite's loader) → `Arklite.dll`.

Daodan loads one more DLL, by the fixed name `realbink.dll`, and forwards Oni's Bink calls to
it. Arklite's loader (`src/loader.c`, 3.5 KB) takes that name. It forwards the 19 Bink exports
to the original Bink, renamed `bink_orig.dll` (`src/realbink.def`), and loads `Arklite.dll`.
Everything else is in `Arklite.dll`.

A release ships `Arklite.dll` and `realbink.dll` (the loader). `bink_orig.dll` is the player's
own `realbink.dll` from AE, which the installer renames, so no Bink or AE file is redistributed.
The installers tell the loader from the original by its size (the loader is 3.5 KB, Bink about
280 KB) and, in the shell scripts, by its "Arklite.dll not found" string.

Arklite logs to `AE/Arklite.log`: what it patched, and for any crash the registers and every
stack value that points into Oni.exe, Daodan or Arklite (Wine prints nothing when Oni dies
while loading a level). If `Arklite.dll` is missing, the loader writes that there instead.

## Source

| File | What |
| --- | --- |
| `src/arklite.c` | `DllMain` (logs the version) and the pause menu patches |
| `src/more_options.c` | Options > More...: the second page and its settings |
| `src/hud_scale.c` | HUD scale: scales the HUD's draw calls |
| `src/custom_game.c` | Custom Game: menu hook, dialog, level mounting, script-folder redirect |
| `src/volume.c` | Sound/Music/Voice volume: mixer hooks and the Options dialog rows |
| `src/random_class.c` | Keeps the dream lab classes out of random costume pools |
| `src/char_limit.c`, `src/char_limit_sites.h` | 255 characters and active characters (the header is generated by `tools/gen_char_limit.py`) |
| `src/players.c` | Player 2: the second Player-flagged character, on a gamepad |
| `src/live_menu.c` | Live pause menu (multiplayer only): the game keeps running behind it |
| `src/lockstep.c` | Input record and replay: the determinism test for multiplayer |
| `src/crash_log.c` | Crash log: a vectored exception handler and the stack at exit |
| `src/loader.c`, `src/realbink.def` | The loader (`realbink.dll`): loads `Arklite.dll`, forwards the Bink exports to `bink_orig.dll` |
| `src/common.c`, `src/arklite.h` | Log, patching, window and string helpers |
| `src/oni.h` | Engine addresses and calling conventions (Oni.exe sha256 `363cf096…`) |

## Scripts

| File | What |
| --- | --- |
| `build.sh` | Builds `build/Arklite.dll` and `build/realbink.dll` with clang + lld |
| `package.sh` | Builds and makes `dist/Arklite-<version>.zip` |
| `install.sh` / `uninstall.sh` | Install or remove (Linux/macOS, Wine). From a release, copies; from the repository, links `build/*.dll`, and links `AE/GameDataFolder/CustomLevels` to `../custom-game/CustomLevels` when that folder exists |
| `install.bat` / `uninstall.bat` | Install or remove (Windows) |
| `tools/gen_char_limit.py` | Regenerates `src/char_limit_sites.h` from your own `Oni.exe` and Daodan |
| `.github/workflows/build.yml` | CI build; publishes a release for a `v*` tag |
