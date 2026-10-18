# Arklite (Oni AE mod)

Quality-of-life patches for Oni, in one DLL (`build/Arklite.dll`), plus a small loader that gets it into the game (see [Loading](#loading)).

| Patch | What it does |
| --- | --- |
| Custom Game | Adds a **Custom Game** button to the main menu that lists and starts custom levels. See below. |
| Options > More... | A **More...** button next to Close opens a second page of options (below). Saved in `AE/Arklite.ini`, `[options]`. |
| HUD scale | **HUD Scale** (50–300%) grows or shrinks the health and weapon rings toward their screen corners. The stock HUD is a fixed number of pixels, so it gets tiny at high resolutions. |
| Live pause menu | Esc opens the pause menu, but the game keeps running behind it: enemies keep fighting and Konoko stands still until you resume. Needed for multiplayer. `live_menu=0` in `AE/Arklite.ini` `[options]` brings back the stock pause. See below. |
| Pause menu background | **Pause Background** sets the opacity of the black behind the pause menu (default 33%, so the paused game shows through). 100% is the original. |
| When game pauses | **Keep playing level music** (default): if the level is playing music (`sound_music_start`), it goes on under the pause menu; other sounds still pause. **Play Main Theme (original)**: the stock `main_menu_win`. **Silence**: everything pauses and nothing plays. The title screen always keeps its music. |
| Sound, Music and Voice volume | Options > Overall Volume becomes three sliders: **Sound Volume** (effects and ambience), **Music Volume** (music and Bink movies) and **Voice Volume** (character dialog). Saved in `AE/Arklite.ini`. |
| 255 characters, all active | Raises the character limit from 128 to 255 and the active-character limit from 64 to 255, so big fights no longer crash. See below. |
| No tiny strikers outside the dream lab | Randomized enemies no longer pick the dream lab's half-size striker (or its giant Shinatama). AE moved those classes into the shared `level0`, so about one random striker in four came out tiny. See below. |
| Sound RNG | Ambient sounds and sound variants pick from their own random numbers, so they no longer shift the game's (the AI's). Needed for multiplayer; sounds are as random as before. See below. |
| Tick-only effects RNG | Particles, bullets included, no longer take their random numbers from a stream that drawing reseeds, so a bullet flies the same way whatever was on screen. Needed for multiplayer. See below. |
| Speech timing | A character's line ends after its length in game ticks, not when the sound card says it stopped. Needed for multiplayer. See below. |
| Input record and replay | Step 1 toward multiplayer: records a level's input and replays it, checking every tick that the game came out the same. Off unless `ARKLITE_RECORD` or `ARKLITE_REPLAY` is set. See below. |

## How it works

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

## Live pause menu

Stock Oni pauses when Esc opens the menu: utility input (0x4f6200, called by the tick at
0x4fa91c, which Daodan replaces) sees Esc inside the tick and calls 0x4bec00, which turns UI
mode on (0x476110) and game input off (0x403f70(0)), runs the pause menu (0x4d2c70, modal dialog
0x96), and turns both back. The modal loop (0x4689d0) keeps drawing the game through the draw
hook at 0x5e96cc (the game display 0x4fb480 while a level is loaded), but nothing updates it.

Arklite replaces 0x4bec00. In a level, the request waits until the frame's update is done (the
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
loop. `live_menu=0` in `[options]` of `AE/Arklite.ini` brings back the stock pause.

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
level number. Level folder layout and known gaps: [../custom-game/README.md](../custom-game/README.md).
Engine addresses: [../docs/custom-game.md](../docs/custom-game.md) and `src/oni.h`.

For testing (Catwalk's Play uses this), `ARKLITE_AUTOSTART=<folder>` opens Custom Game and
plays that folder as if clicked, and `ARKLITE_SAVEPOINT=<n>` starts it at save point n.

## Loading

Oni → `binkw32.dll` (Daodan) → `realbink.dll` (Arklite's loader) → `Arklite.dll`.

Daodan loads one more DLL, by the fixed name `realbink.dll`, and forwards Oni's Bink calls to
it. Arklite's loader (`src/loader.c`, 3.5 KB) takes that name. It forwards the 19 Bink exports
to the original Bink, renamed `bink_orig.dll` (`src/realbink.def`), and loads `Arklite.dll`.
Everything else is in `Arklite.dll`.

A release is three files for `AE/`: `Arklite.dll`, `realbink.dll` (the loader) and
`bink_orig.dll` (a copy of AE's original `realbink.dll`). Running the AE Installer again may put
the original `realbink.dll` back. On this machine `install.sh` links them instead (the original
goes to `backup/`).

Arklite logs to `AE/Arklite.log`: what it patched, and for any crash the registers and every
stack value that points into Oni.exe, Daodan or Arklite (Wine prints nothing when Oni dies
while loading a level). If `Arklite.dll` is missing, the loader writes that there instead.

## Source

| File | What |
| --- | --- |
| `src/arklite.c` | `DllMain` and the pause menu patches |
| `src/more_options.c` | Options > More...: the second page and its settings |
| `src/hud_scale.c` | HUD scale: scales the HUD's draw calls |
| `src/custom_game.c` | Custom Game: menu hook, dialog, level mounting, script-folder redirect |
| `src/volume.c` | Sound/Music/Voice volume: mixer hooks and the Options dialog rows |
| `src/random_class.c` | Keeps the dream lab classes out of random costume pools |
| `src/char_limit.c`, `src/char_limit_sites.h` | 255 characters and active characters (the header is generated by `tools/gen_char_limit.py`) |
| `src/live_menu.c` | Live pause menu: the game keeps running behind it |
| `src/lockstep.c` | Input record and replay: the determinism test for multiplayer |
| `src/crash_log.c` | Crash log: a vectored exception handler and the stack at exit |
| `src/loader.c`, `src/realbink.def` | The loader (`realbink.dll`): loads `Arklite.dll`, forwards the Bink exports to `bink_orig.dll` |
| `src/common.c`, `src/arklite.h` | Log, patching, window and string helpers |
| `src/oni.h` | Engine addresses and calling conventions (Oni.exe sha256 `363cf096…`) |

## Scripts

- `build.sh`: builds `build/Arklite.dll` and `build/realbink.dll` with clang + lld
- `install.sh` / `uninstall.sh`: link `AE/Arklite.dll`, `AE/realbink.dll`, `AE/bink_orig.dll` and
  `AE/GameDataFolder/CustomLevels` (`../custom-game/CustomLevels`), or remove the links and
  put the original `realbink.dll` back
