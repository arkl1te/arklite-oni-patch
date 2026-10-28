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
| Custom Game | A **Custom Game** button on the main menu: **New** lists and starts custom levels from `AE/GameDataFolder/CustomLevels/` (play alone, or host them for others), **Join** lists your saved servers and joins one. Levels don't need a unique level number. See [Custom Game](#custom-game). |
| Options > More... | A **More...** button next to Close opens a second page of options (below). |
| HUD scale | **HUD Scale** (50–300%) grows or shrinks the health and weapon rings toward their screen corners. The stock HUD is a fixed number of pixels, so it gets tiny at high resolutions. |
| Sound, Music and Voice volume | Options > Overall Volume becomes three sliders: **Sound Volume** (effects and ambience), **Music Volume** (music and Bink movies) and **Voice Volume** (character dialog). |
| Pause menu background | **Pause Background** sets the opacity of the black behind the pause menu (default 33%, so the paused game shows through). 100% is the original. |
| When game pauses | **Keep playing level music** (default): level music goes on under the pause menu; other sounds still pause. **Play Main Theme (original)**: the stock menu theme. **Silence**: everything pauses and nothing plays. In a game with player 2 the game doesn't pause, so its sounds go on whatever this says. |
| Live pause menu | In a game with player 2, Esc opens the pause menu but the game keeps running behind it: enemies keep fighting and Konoko stands still until you resume. Single player pauses as in the original. |
| 255 characters, all active | Raises the character limit from 128 to 255, and the active-character and shadow limits (64 and 32) to 255 too, so big fights no longer crash the game. |
| No tiny strikers outside the dream lab | Randomized enemies no longer pick the dream lab's half-size striker (or its giant Shinatama). AE moved those classes into the shared `level0`, so about one random striker in four came out tiny. |
| No skybox seams | The sky no longer shows black lines along the edges and corners of the skybox. |
| Player 2 on a gamepad | A second character with the IsPlayer flag in a level is played on an XInput gamepad, next to Konoko on keyboard and mouse. See [Player 2](#player-2). |
| Determinism fixes | Ambient sounds, particles and speech no longer depend on the frame rate, the sound card or what is drawn, so the same input always gives the same game. Multiplayer needs this; in normal play you won't notice a difference. |
| Input record and replay | For testing: records a level's input and replays it, checking every tick that the game came out the same. Off unless `ARKLITE_RECORD` or `ARKLITE_REPLAY` is set. See [Record and replay](#record-and-replay). |
| Network test (two PCs) | For testing: one PC hosts, another joins by IP, and both play the same map in lockstep, Konoko on the host and player 2 on the joiner. Start it from **Custom Game** (Host / Join), or with `ARKLITE_HOST` / `ARKLITE_JOIN`. See [Network test](#network-test). |
| Crash log | If Oni crashes, `AE/Arklite.log` gets the registers and the stack values that point into Oni, Daodan or Arklite. |

Multiplayer over the network is at its first test: two players, started by hand with
environment variables. There's no server list or in-game join yet.

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
     Without a path (or `ONI_AE_DIR`) it looks next to itself, in `~/.wine`, `~/Games/*` and Bottles
     prefixes, and in `~/Games/Oni/AE` and `~/Oni/AE`.
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

Without a path, the uninstaller looks in the same places as the installer.

This removes `Arklite.dll`, puts the original `realbink.dll` back and removes
`CustomLevels/` if it's empty. Your settings (`AE/Arklite.ini`) and log are kept; pass
`/purge` (Windows) or `--purge` (Linux/macOS), before any path, to remove them too. Custom
levels, recordings (`*.akr`), `persist.dat` and `persist.dat.before-arklite` are never removed.

To uninstall by hand: close Oni, delete `AE/Arklite.dll` and `AE/realbink.dll`, then rename
`AE/bink_orig.dll` to `realbink.dll`.

## Settings

The Options dialog writes `[options]` and `[volume]`; `[players]` and `[multiplayer]` are only in the file. Edit
`AE/Arklite.ini` with Oni closed:

| Section | Key | Default | What |
| --- | --- | --- | --- |
| `[options]` | `hud_scale` | 100 | HUD scale in percent, 50–300 |
| | `pause_background` | 33 | Opacity of the black behind the pause menu, 0–100 |
| | `pause_music` | `keep` | `keep` (level music goes on), `theme` (original menu theme) or `silence` |
| `[volume]` | `sound`, `music`, `voice` | Overall Volume | 0–100 each; a missing key starts at your old Overall Volume |
| `[players]` | `p2` | 1 | 0: no player 2 |
| | `p2_name` | Player 2 | Player 2's name, shown in the score and over their head |
| | `p2_pad` | 0 | Which gamepad plays player 2, 0–3 |
| | `p2_aim_speed` | 8 | Aim per tick at full right stick |
| | `p2_invert_y` | 0 | 1: invert the right stick's up/down |
| `[net]` | `host` | 0 | 1: host a network game, as `ARKLITE_HOST=1` does. Set it back to 0 for single player |
| | `join` | (empty) | `<ip>[:port]`: join that host, as `ARKLITE_JOIN` does. Empty it for single player |
| | `port` | 27777 | The host's UDP port, if `ARKLITE_HOST` doesn't give one. The Host dialog saves its Port here |
| | `record` | 1 | 0: don't record network games to `Arklite-net-host.akr` / `-join.akr` |
| | `delay` | 0 | 0: no input delay; the other player's input is predicted and the game corrects itself when the real one arrives (rollback). 1 or more: the input delay in ticks (1/60 s each), and the game waits for the other player's input instead. The host's value is used on both PCs |
| `[host]` | `name` | New Oni multiplayer game | The server's name in other players' Join list (the Host dialog saves it) |
| | `max_players`, `max_spectators` | 8, 0 | The Host dialog's last values. For now a game has 2 players and no spectators |
| | `time_limit` | 10 | Minutes a match lasts; 0: no time limit (the Host dialog saves it) |
| | `score_limit` | 20 | Points that win a match; 0: no score limit. Below 0, e.g. -5: the first player whose score goes down to it wins, by suicides (the Host dialog saves it) |
| `[servers]` | `count`, `1`, `2`, ... | | The Join list's saved addresses (`<ip or name>[:port]`), kept by Add and Remove |
| `[multiplayer]` | `respawn_delay` | 1.5 | Seconds a dead player waits before an attack button brings them back at a spawn point (in a network game, the host's) |
| | `item_respawn` | 30 | Seconds until a taken weapon or power-up comes back, for items the level doesn't set; 0: never (in a network game, the host's) |
| | `characters` | 18 of the game's characters | The **F2** menu's characters: class names separated by commas, e.g. `konoko_generic,griffin_generic,ninja_med_1` (at most 24). To pick a character's variations yourself, separate them with `\|`: `konoko_generic\|k3\|k4_A` |
| | `name` | Player | Your name in multiplayer games. You can also change it in the **F2** menu, which saves it here |
| `[chat]` | `fade` | 10 | Seconds a chat message stays on screen while the chat is closed; 0: they stay |

Environment variables, for testing (set them before starting `Oni.exe`):

| Variable | What |
| --- | --- |
| `ARKLITE_AUTOSTART=<folder>` | Opens Custom Game and plays `CustomLevels/<folder>` |
| `ARKLITE_SAVEPOINT=<n>` | With `ARKLITE_AUTOSTART`, starts at save point n (1–9) |
| `ARKLITE_RECORD=<file>` | Records the first level played into `AE/<file>` |
| `ARKLITE_REPLAY=<file>` | Replays `AE/<file>` and writes `MATCHED` or `DESYNCED` to `AE/Arklite.log` |
| `ARKLITE_HOST=1` | Hosts a network game on UDP port 27777 (or `ARKLITE_HOST=<port>`) |
| `ARKLITE_JOIN=<ip>[:port]` | Joins the host at that address |
| `ARKLITE_LOG=<file>` | Writes the log to `AE/<file>` (the joiner's default is `Arklite-join.log`) |

On Windows: `cd "C:\Program Files (x86)\Oni\AE"`, `set ARKLITE_RECORD=test.akr`, `Oni.exe`.

## Custom Game

The button asks whether to start a **New** custom game or **Join** one through the network.

**New** lists every level in `GameDataFolder/CustomLevels/` plus any installed level whose
number isn't from the original game, with how many players it has room for (its Player-flagged
characters, the spawn points); the ones that can be played in multiplayer are blue. **Play** plays it alone. **Host** opens the host settings (map,
server name, port, max players and spectators; the last ones used are kept) and starts the level
as a network game others can join. A map needs two or more spawn points to be hosted; the list
says so under it when the selected one hasn't. **Dedicate** (a server without a game window) isn't
built yet. In the text fields, **Enter** or **Esc** confirms what you typed and leaves the field (Esc doesn't close the screen while you're in a field), and **Tab** goes to the next field. Text is selected as in Windows: **Shift** with the arrows, **Home** and **End** (and **Ctrl** for whole words), a double click selects a word, a triple click everything, **Ctrl+A** too.

**Join** lists your saved servers with their ping, name and players, and adds the games hosted on
this PC or your local network by itself (those aren't saved unless you edit their address). Select one to see what it
says about itself (name, players connected, spectators, map) and **Play** to join it; you need the
same map in your `CustomLevels`. **Add** makes a new entry: type its address (`192.168.1.10`, a
name, and `:port` if it isn't 27777) in **Address**. **Remove** deletes the selected one.

**Chat**: in a network game (the lobby and the match), press **Enter** to chat. A text field
opens in the lower left, under the messages, both on a light dark background; type and press
**Enter** to send (you hear a locked door) and close it, or **Shift+Enter** to send and keep
typing. **Esc** closes it without sending what you typed, and so does **Enter** with nothing
typed. While you type, the keyboard only goes to the chat, but the mouse still turns, aims and
attacks. **Up**/**Down** (or the mouse wheel, **Page Up**/**Page
Down** a page at a time) scroll the messages; **Left**/**Right** move the cursor, and selecting
and deleting text works as in the menus' text fields. When the other player writes, you hear the
game-saved sound, and their message shows over their head (above their name, in a bubble, at
most two lines) for a few seconds. With the chat closed, messages stay on screen for 10 seconds, then fade
(`fade` in `[chat]`).

 Custom levels don't need a unique level number: each one
gets a free number (31 or lower) when you pick it (a joiner takes the host's). Put each level in its own folder:

```
CustomLevels/<AnyFolderName>/
    level.ini              [level] name=Shown in the list (optional; otherwise the folder name)
                                   source=N, the original level whose save points it may use
    levelN_Final.dat/.raw  any N; other level*.dat files (plugins) in the folder are loaded too,
                           up to 8, except *_tools* files
    IGMD/<level name>/     BSL scripts (optional; otherwise GameDataFolder/IGMD is used)
```

Known gaps: save points made in a custom level go to the persist.dat slot it was mounted in (when that
slot has no save for a save point, the `source` level's is used); and what happens when a
custom level ends (its "next level") isn't handled yet.

For testing, `ARKLITE_AUTOSTART=<folder>` opens Custom Game and plays that folder as if
clicked, and `ARKLITE_SAVEPOINT=<n>` (1–9) starts it at save point n.

## Player 2

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
| Start | nothing (there is no pausing) |

Without a gamepad (or when XInput doesn't load), player 2 stands still, but the game is still a
multiplayer game.

A game with player 2 is a **multiplayer game**, and in one:

- **Auto-aim is off**, as it will have to be over the network, where each player sees
  different characters.
- **Nothing pauses**: the game keeps running behind the pause menu and behind the help and
  objectives screen (F1), and F1 doesn't turn the sound down.
- **Players respawn**: 1.5 seconds after dying (`respawn_delay`), a dead player presses an
  attack button (punch, kick or any fire) to come back at a spawn point, with the starting
  health and weapon of their placement. The body stays as a corpse.
- **Items respawn**: a weapon or power-up placed in the level comes back at its spot 30 seconds
  after someone takes it (`item_respawn`). A level made in Catwalk can set the time per item
  (its **RespawnTime** field); 0 means it never comes back.

On a **multiplayer map** (two or more IsPlayer characters, see below), also:

- **F2 picks your character**: a menu over the running game shows a picture of every character
  you can play. Pick one with the arrow keys or the mouse and choose it with Enter or a click
  (F2 or Esc closes the menu). You change at once, wherever you are, keeping your health,
  weapons and items; if you're dead, you come back as that character when you respawn. Every player has 100 health,
  whatever the character, so a Thug is as tough as a Muro. Your player stands still while the menu is open. The list can be changed
  with `characters` in `[multiplayer]`.
- **Variations**: most characters have other versions of themselves (Konoko's outfits, the
  Strikers' colors, the thug gangs). Press Q/E (or Page Up/Down), or click the `<  2 / 7  >` line
  under a character, to see them, then choose as usual.
- **Your name**: the F2 menu shows it under its title. Press Tab or click it, type a new one
  (up to 23 letters), and press Enter to keep it (Esc puts the old one back). It's saved, so
  you keep it next time. The score shows everyone's name, and when you look straight at
  another player, their name appears over their head.
- **Free for all**: players can hit, shoot and throw each other, though they're all on Konoko's
  team. AI characters keep their usual teams.
- **Damage is x1.25**: every hit on a character does a quarter more (rounded).
- **Unlimited taunts**: taunt whenever you like, not only right after a kill. Every player
  hears the same taunt line, and a new taunt cuts off your last one instead of waiting for it.
- **Overhealing drains a quarter faster**: health above the maximum (from a hypo) goes down
  1 point every 8.8 ticks instead of every 11.

### Spawn points

Every character with the IsPlayer flag is a spawn point. The first one is Konoko and the second
player 2; a third, fourth and so on (`player3`, `player4`...) aren't spawned, they only mark more
spawn points. Each player starts at a random spawn point and respawns at one that no other living
player is near (the farthest one if every point has someone close). The pick is the same on
every PC and in every replay. A spawn point gives the position and the facing; the class, team
and weapon come from the player's own placement, and every player starts with 100 health.

The `[players]` settings are in [Settings](#settings).

## Record and replay

Multiplayer will send only inputs between PCs, so the same inputs must give the same game on
every PC. This tests that on one machine:

```sh
cd "/path/to/Oni/AE"                      # under Wine; set WINEPREFIX if it isn't ~/.wine
ARKLITE_RECORD=test.akr wine Oni.exe     # play a level for a while, then quit
ARKLITE_REPLAY=test.akr wine Oni.exe     # start the same level the same way, hands off
```

On Windows, see [Settings](#settings). The file goes in `AE/`. Only the first level played is
recorded; recording ends when you leave or restart it. The replay starts when a level loads, feeds the recorded input in place of yours
(leave the controls alone while it runs), and gives control back when the recording runs out.
A recording of another level isn't replayed, and one made by an Arklite with another recording
format version is refused (record it again). `AE/Arklite.log` then says `MATCHED` or `DESYNCED`;
for a desync it gives the first tick that differs and, for the first 3 ticks that differ, the
fields that differ (up to 16 per character) with their exact bits.

Recordings are large (roughly 10–80 MB a minute with a few characters fighting).

## Network test

Two PCs (or two Oni windows on one PC) play the same multiplayer map. The host plays Konoko,
the joiner plays player 2 (the second IsPlayer character), and only inputs travel between them.
Both need the same Arklite, the same AE and the same map, and must start it the same way.
The host checks this when someone joins: if the two PCs' Arklite, Daodan, `daodan.ini`
gameplay settings, `level0_Final` files or map folder (`GameDataFolder/CustomLevels/<map>`) aren't
identical, the joiner stays out of the lobby and both screens say in red what differs. Copy
that file or folder from one PC to the other and join again.
When only the map folder differs, the joiner downloads the host's version instead: the level
reloads, and its loading box shows `Downloading (files left/files in the map)` and the current
file's progress and speed. Esc cancels. The joiner's old files go to
`GameDataFolder/CustomLevels.backup/<map>`. A joiner who doesn't have the map at all gets it the
same way: the Join list says `You don't have its map (<map>): Play downloads it.`, and Play
downloads it into `GameDataFolder/CustomLevels/<map>` before the level starts. If that download
fails or you press Esc, you're back in the Join list, which says why. This works for maps in
`CustomLevels`, not for the original levels. Arklite counts as the same when it
was built from the same source: a release download and your own build of that release's commit
can play together. The joiner uses the host's
`respawn_delay` and `item_respawn`.

The easy way is **Custom Game**: one PC picks a map under **New** and clicks **Host**, the other
adds the host's IP under **Join** and clicks **Play**. For scripted tests the environment works
too (a mode set this way stays on for the whole session, even when you pick **Play**):

```sh
cd "/path/to/Oni/AE"
ARKLITE_HOST=1 ARKLITE_AUTOSTART=NewLevel-Versus wine Oni.exe -noswitch               # PC 1
ARKLITE_JOIN=192.168.1.10 ARKLITE_AUTOSTART=NewLevel-Versus wine Oni.exe -noswitch    # PC 2
```

On Windows, set the variables in `cmd` (not PowerShell's `set`) and start `Oni.exe` from that
same window, or put `join=<host's IP>` under `[net]` in `AE\Arklite.ini` and start Oni as usual.
The log's `net:` lines say whether networking is on.

Use the host's IP. On one PC, join `127.0.0.1`; `-noswitch` keeps both in windows.

The level starts as a **lobby**: everyone plays the map on their own (the other player stands
still) while they wait. Press **F3** to be ready ("Press F3 to be ready" turns from red to green;
F3 again to be unready). When at least two players are there and more than half of them are
ready, a 7-second countdown starts (it stops if someone unreadies). At 0 the level reloads on
every PC and the match begins, with everyone in step. The score (kills; dying on your own
costs a point) is in the top right corner during the match, with the time left under it.

A match ends at the host's **Time limit** or **Score limit** (the Host dialog; 0 means none of
that limit). A negative score limit is reached going down, so the players race to kill
themselves. When it ends, the game stands still and shows who won; press **fire** (the left
mouse button) to go back. When everyone did, or after 30 seconds, the level reloads on every PC
into the lobby, where F3 starts the next match.

**F1** opens the help screen as usual, with the game still running. In a multiplayer game its
first tab is the **Scoreboard**: every player's name, score and ping, the time left and the
score limit. Tick **Mute** next to a player to hide their chat messages (until you untick it or
close Oni). The **Objective** tab says in one sentence how to win the match.
The host needs UDP port 27777 open (forwarded through the router for a game over the internet).
The joiner's camera, sound, HUD, laser sight and crosshair follow player 2.

Both logs (`Arklite.log` on the host, `Arklite-join.log` on the joiner) say whether the two
games started the same, and once a second they compare the game state. `DESYNC` means the two
games came apart; the log lists this PC's characters at that tick, and each PC writes
`AE/Arklite-desync-host.bin` or `AE/Arklite-desync-join.bin`. `tools/desync_diff.py` compares
the two and prints every value that differs. Each PC also records the whole game to
`AE/Arklite-net-host.akr` or `AE/Arklite-net-join.akr` (a few MB a minute with two players;
`record=0` under `[net]` turns it off), and `tools/akr_diff.py` compares the two recordings
tick by tick and names the first tick, value and random-number calls that differ. If the
other PC goes quiet for 10 seconds, its player stands still and the game goes on.

Joining shows your IP address to the host, and hosting shows yours to whoever joins.

## Troubleshooting

- **No Custom Game button**: open `AE/Arklite.log`. If it is missing or says
  `Arklite.dll not found`, run the installer again. Check that you started the `Oni.exe` in
  `Oni/AE/`, not the one in `Oni/`.
- **A feature does nothing**: `AE/Arklite.log` says which patches were applied. A patch is skipped
  when the code at its address isn't what Arklite expects (a different `Oni.exe` or Daodan).
- **Player 2 doesn't move**: `AE/Arklite.log` says `XInput didn't load` when no XInput DLL was
  found, and `player 2 is #...` once a level has a second player. Check `p2_pad` in `[players]`.
- **Oni crashes**: the end of `AE/Arklite.log` has the crash registers and stack. Include
  it, and the version on its first line, when you report the problem.

## Building from source

You need `clang`, `lld` and `llvm-dlltool` (any recent LLVM; no MinGW or Visual Studio), and
`zip` for the release zip. On Arch/CachyOS: `pacman -S clang lld llvm zip`; on Debian/Ubuntu:
`apt install clang lld llvm zip`.

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

| File | What |
| --- | --- |
| `build.sh` | Builds `build/Arklite.dll` and `build/realbink.dll` with clang + lld |
| `package.sh` | Builds and makes `dist/Arklite-<version>.zip` |
| `install.sh` / `uninstall.sh` | Install or remove (Linux/macOS, Wine). From a release they copy; from the repository they link `build/*.dll` |
| `install.bat` / `uninstall.bat` | Install or remove (Windows) |
| `tools/gen_char_limit.py` | Regenerates `src/char_limit_sites.h` (needs `objdump`; set the `ONI` and `DAODAN` paths at its top) |

## License

Arklite is under the ISC license; see `LICENSE`. Oni, the Anniversary Edition, Daodan and Bink
belong to their owners and aren't covered by it.
