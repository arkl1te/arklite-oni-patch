# Arklite (Oni AE mod)

Quality-of-life patches for Oni, in one DLL (`build/Arklite.dll`).

| Patch | What it does |
| --- | --- |
| Custom Game | Adds a **Custom Game** button to the main menu that lists and starts custom levels. See below. |
| Options > More... | A **More...** button next to Close opens a second page of options (below). Saved in `AE/Arklite.ini`, `[options]`. |
| HUD scale | **HUD Scale** (50–300%) grows or shrinks the health and weapon rings toward their screen corners. The stock HUD is a fixed number of pixels, so it gets tiny at high resolutions. |
| Pause menu background | **Pause Background** sets the opacity of the black behind the pause menu (default 33%, so the paused game shows through). 100% is the original. |
| When game pauses | **Keep playing level music** (default): if the level is playing music (`sound_music_start`), it goes on under the pause menu; other sounds still pause. **Play Main Theme (original)**: the stock `main_menu_win`. **Silence**: everything pauses and nothing plays. The title screen always keeps its music. |
| Sound, Music and Voice volume | Options > Overall Volume becomes three sliders: **Sound Volume** (effects and ambience), **Music Volume** (music and Bink movies) and **Voice Volume** (character dialog). Saved in `AE/Arklite.ini`. |
| 255 characters, all active | Raises the character limit from 128 to 255 and the active-character limit from 64 to 255, so big fights no longer crash. See below. |
| No tiny strikers outside the dream lab | Randomized enemies no longer pick the dream lab's half-size striker (or its giant Shinatama). AE moved those classes into the shared `level0`, so about one random striker in four came out tiny. See below. |

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
More page goes back instead of closing: Esc posts message 0x14 to the dialog, which the default
dialog proc (0x468cd0) would turn into a Close command, so Arklite handles 0x14 there itself. The stock radio buttons use
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

## Custom Game

The button lists every level in `GameDataFolder/CustomLevels/` plus any installed level whose
number isn't from the original game. The chosen level is added to the engine's level table
under a free number (31 or lower) when it is picked, so custom levels don't need a unique
level number. Level folder layout and known gaps: [../custom-game/README.md](../custom-game/README.md).
Engine addresses: [../docs/custom-game.md](../docs/custom-game.md) and `src/oni.h`.

For testing (Catwalk's Play uses this), `ONICUSTOM_AUTOSTART=<folder>` opens Custom Game and
plays that folder as if clicked, and `ONICUSTOM_SAVEPOINT=<n>` starts it at save point n.

## Loading

Oni → `binkw32.dll` (Daodan) → `realbink.dll` (onicustom, `../custom-game`) →
`LoadLibraryA("Arklite.dll")`. onicustom must be installed. It logs whether
Arklite.dll was found, and any crash, in `AE/onicustom.log`. Arklite itself logs to
`AE/Arklite.log`.

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
| `src/common.c`, `src/arklite.h` | Log, patching, window and string helpers |
| `src/oni.h` | Engine addresses and calling conventions (Oni.exe sha256 `363cf096…`) |

## Scripts

- `build.sh`: builds `build/Arklite.dll` with clang + lld
- `install.sh` / `uninstall.sh`: add or remove the `AE/Arklite.dll` symlink
