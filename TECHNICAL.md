Everything the [README](README.md) leaves out: what's needed, what the installer does, every
setting, and how custom levels, player 2 and network games work.

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
   the newest `AE/logs/Arklite_<date>_<time>.log` lists what was patched.

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
`CustomLevels/` if it's empty. Your settings (`AE/Arklite.ini`) and logs (`AE/logs/`) are kept; pass
`/purge` (Windows) or `--purge` (Linux/macOS), before any path, to remove them too. Custom
levels, recordings (`*.akr`), `persist.dat` and
`persist.dat.before-arklite` are never removed.

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
| | `show_fps` | 0 | 1: FPS counter in the top right corner, refreshed every 6 frames |
| `[volume]` | `sound`, `music`, `voice` | Overall Volume | 0–100 each; a missing key starts at your old Overall Volume |
| `[players]` | `p2` | 1 | 0: no player 2 |
| | `p2_name` | Player 2 | Player 2's name, shown in the score and over their head |
| | `p2_pad` | 0 | Which gamepad plays player 2, 0–3 |
| | `p2_aim_speed` | 8 | Aim per tick at full right stick |
| | `p2_invert_y` | 0 | 1: invert the right stick's up/down |
| `[net]` | `host` | 0 | 1: host a network game, as `ARKLITE_HOST=1` does. Set it back to 0 for single player |
| | `join` | (empty) | `<ip>[:port]`: join that host, as `ARKLITE_JOIN` does. Empty it for single player |
| | `port` | 27777 | The host's UDP port, if `ARKLITE_HOST` doesn't give one. The Host dialog saves its Port here |
| | `stun`, `stun2` | `stun.l.google.com:19302`, `stun.cloudflare.com:3478` | The public STUN servers your game asks for its internet address (`/ip`, `/punch`), every 25 s while it's in a network game; the second one if the first doesn't answer. Empty both to never ask |
| | `helper` | `51.20.130.46:27780` | The Arklite helper: the server list in Join, and introductions that open a host's router for its players (see below). Empty: none |
| | `upnp` | 1 | 1: while this PC hosts, ask the router (UPnP) to forward the UDP port to it, and take the forwarding back when hosting stops. 0: don't; forward the port by hand if players on the internet should join |
| | `record` | 1 | 0: don't record network games to `Arklite-net-host.akr` / `-join.akr`. Only matches of two players with input delay are recorded |
| `[host]` | `listed` | 1 | 0: your game stays off the server list (players who know your address still get introduced) |
| | `delay` | 0 | 0: no input delay; the other players' input is predicted and the game corrects itself when the real one arrives (rollback). 1 or more: the input delay in ticks (1/60 s each), and the game waits for the other players' input instead. The host's value is used on every PC |
| `[host]` | `name` | New Oni multiplayer game | The server's name in other players' Join list (the Host dialog saves it) |
| | `max_players` | 4 | The Host dialog's last value: players 2 to 4 (spectators have no limit). Matches of 3 or 4 players are untested |
| | `time_limit` | 10 | Minutes a match lasts; 0: no time limit (the Host dialog saves it) |
| | `score_limit` | 20 | Points that win a match; 0: no score limit. Below 0, e.g. -5: the first player whose score goes down to it wins, by suicides (the Host dialog saves it) |
| `[servers]` | `count`, `1`, `2`, ... | | The Join list's saved addresses (`<ip or name>[:port]`), kept by + and - |
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
| `ARKLITE_REPLAY=<file>` | Replays `AE/<file>` and writes `MATCHED` or `DESYNCED` to the newest `AE/logs/Arklite_<date>_<time>.log` |
| `ARKLITE_HOST=1` | Hosts a network game on UDP port 27777 (or `ARKLITE_HOST=<port>`) |
| `ARKLITE_JOIN=<ip>[:port]` | Joins the host at that address |
| `ARKLITE_LOG=<file>` | Names the log: `AE/logs/<name>_<date>_<time>.log` (with `ARKLITE_JOIN` the default name is `Arklite-join.log`). Give each extra Oni on one PC its own, e.g. `Arklite-join3.log` |
| `ARKLITE_BENCH=<seconds>` | Once a level has run for 5 seconds, times every frame for that long, logs the frame times and closes Oni. `tools/oni-bench.sh` runs it hands off in a private KWin |

On Windows: `cd "C:\Program Files (x86)\Oni\AE"`, `set ARKLITE_RECORD=test.akr`, `Oni.exe`.

## Custom Game

The button asks whether to start a **New** custom game or **Join** one through the network.

**New** lists every level in `GameDataFolder/CustomLevels/` plus any installed level whose
number isn't from the original game, with how many players it has room for (its Player-flagged
characters, the spawn points); the ones that can be played in multiplayer are blue. **Play** plays it alone. **Host** opens the host settings (map,
server name, port, max players; the last ones used are kept; anyone can spectate) and starts the level
as a network game others can join. A map needs two or more spawn points to be hosted; the list
says so under it when the selected one hasn't. **Dedicate** (a server without a game window) isn't
built yet. In the text fields, **Enter** or **Esc** confirms what you typed and leaves the field (Esc doesn't close the screen while you're in a field), and **Tab** goes to the next field. Text is selected as in Windows: **Shift** with the arrows, **Home** and **End** (and **Ctrl** for whole words), a double click selects a word, a triple click everything, **Ctrl+A** too.

**Join** lists your saved servers with their ping, name and players, and adds the games hosted on
this PC or your local network by itself (those aren't saved unless you edit their address). Select one to see what it
says about itself (name, map, players connected and how many are spectating) and **Play** to join it; if you don't
have its map, Play downloads it. **+** (under the list) makes a new entry: type its address (`192.168.1.10`, a
name, and `:port` if it isn't 27777) in **Address**. **-** deletes the selected one.

**Chat**: in a network game (the lobby, the warm-up and the match), press **Enter** to chat; a
gray "Press ENTER to chat" says so whenever the match isn't running. A text field
opens in the lower left, under the messages, both on a light dark background; type and press
**Enter** to send (you hear a locked door) and close it, or **Shift+Enter** to send and keep
typing. **Esc** closes it without sending what you typed, and so does **Enter** with nothing
typed; a gray "ESC: close | ENTER: send" under the field says so. While you type, the keyboard only goes to the chat, but the mouse still turns, aims and
attacks. **Up**/**Down** (or the mouse wheel, **Page Up**/**Page
Down** a page at a time) scroll the messages; **Left**/**Right** move the cursor, and selecting
and deleting text works as in the menus' text fields. When another player writes, you hear the
game-saved sound, and their message shows over their head (above their name, in a bubble, at
most two lines) for a few seconds. The chat also says, in green and without a sound, when someone
enters or leaves the game. With the chat closed, messages stay on screen for 10 seconds, then fade
(`fade` in `[chat]`).

**Chat commands**: a line starting with `/` isn't sent, it's a command. Only you see their answers
(in light blue).

| Command | What it does |
|---|---|
| `/help` | Lists the commands |
| `/name New Name` | Changes your name (saved, like typing it in F2); everyone sees "Old is now known as New" |
| `/whisper Name text`, `/w Name text` | Only that player reads it (pink on both screens, no bubble over your head). A start of the name is enough when only one player's name fits |
| `/movelist`, `/moves` | Your current character's moves, the move's name and its input in two columns (moves.md's list without the moves every character has); `/movelist` adds gray lines on what everyone has and on blocking, `/moves` leaves them out. The chat stays open so you can scroll it |
| `/taunt` or `/t`, `/taunt2` or `/t2` | Plays your character's first taunt, or its rare second one: Konoko's, the Ninja's moonwalk, Tanker's (the taunt key picks one at random, the second seldom) |
| `/sneeze` | Konoko sneezes (her rare idle; other characters have none) |
| `/idle` or `/i` | Plays your character's special idle, one made for its body: Konoko's (also Red, Shinatama), Comguy's (also Griffin, Muro, Thug, Ninjabot), Striker's (also Barabas, Elite, TCTF SWAT), Tanker's. Ninja has none |
| `/clear` | Empties the chat |
| `/ip` | Your internet address and port, as the internet sees your game (for a host behind a shared address, see `/punch`) |
| `/punch 1.2.3.4:5678` | Host: lets in a player from outside your network when they can't reach you. They tell you their address (shown under "Connecting to the host..." or by their `/ip`); your game knocks toward it for 2 minutes |
| `\/text` | Sends "/text" as a message, not a command |

A taunt first puts your character in its fighting stance (taunts belong to it, as with the taunt key). Taunts, the sneeze and idles play only while you stand, every player sees them, and any key stops
them.

 Custom levels don't need a unique level number: each one
gets a free number (31 or lower) when you pick it (a joiner takes the host's). Put each level in its own folder:

```
CustomLevels/<AnyFolderName>/
    level.ini              [level] name=Shown in the list (optional; otherwise the folder name)
                                   source=N, the original level whose save points it may use
                           [textures] levels=4,3,10: the game levels whose textures the map names
    levelN_Final.dat/.raw  any N; other level*.dat files (plugins) in the folder are loaded too,
                           up to 8, except *_tools* files
    IGMD/<level name>/     BSL scripts (optional; otherwise GameDataFolder/IGMD is used)
```

A map doesn't need to contain the game's textures: it can name them, and Arklite loads the levels
in `levels=` from your own `GameDataFolder`, keeping only their textures. Catwalk does this by itself.
A level can't start if one of those files is missing.

A level can bring its own particle classes and ambient and impulse sounds, which stock Oni never set
up for any level but level 0. Textures can be up to 1024x1024.

Catwalk writes these next to the level, and Arklite reads them:

- `vertexlight.bin`: corners whose light pulses, flickers or breaks down. A light can also play an
  impulse sound on each pulse, or when a flicker starts failing, with a pitch range and a seed so
  that lights with the same timing flicker differently. Every PC plays the sound on the same tick.
- `spinparticles.bin`: particles in a spinning group turn with it, in step with the group's
  geometry. Decals stay where they are.

A sky (ONSK) with a sixth texture draws it as a floor under the box. The game's own skies have
five, so they look as before.

**Kill camera**: on a multiplayer map, a level script's `cm_detach` doesn't move anyone's camera
at once. It marks the player that `chr_is_player` was last asked about, so the script must ask
first, as Level 3's vat does. That player's camera then stays where it is while they're dead, and
cuts to them again when they respawn. The other players' cameras are unaffected.

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

**Barabas regenerates** when a player plays him, in any game: crouch, let go of crouch, then press
Action. While he holds the pose he heals 8% of his maximum health a second, up to the maximum, and
the mouse still looks around. Any movement, attack or other action key ends it, and so do Action
again or taking hits worth 5% of his maximum health. Only characters with a regeneration
animation (Barabas) can do it.

A game with player 2 is a **multiplayer game**, and in one:

- **Auto-aim is off**, as it will have to be over the network, where each player sees
  different characters.
- **Nothing pauses**: the game keeps running behind the pause menu and behind the help and
  objectives screen (F1), and F1 doesn't turn the sound down. A console's text page opens only
  for the player who used the console, and the game goes on behind it too.
- **Esc always works** in a network game, even while the game stands "Waiting for" another
  player: it opens the pause menu, whose Custom Game button hosts or joins another game (Join
  can take the same host again). A player who stops answering is dropped after 10 seconds, and
  a joiner whose host stops answering goes back to its own warm-up.
- **Players respawn**: 1.5 seconds after dying (`respawn_delay`), a dead player presses an
  attack button (punch, kick or any fire) to come back at a spawn point, with the starting
  health and weapon of their placement. The body stays as a corpse. On custom levels a particle
  effect and sound mark the spot; they're built from your own level 0 and left out if it isn't
  the one they were made from.
- **Items respawn**: a weapon or power-up placed in the level comes back at its spot 30 seconds
  after someone takes it (`item_respawn`), with a spark and a zap sound. A level made in Catwalk
  can set the time per item (its **RespawnTime** field); 0 means it never comes back.

On a **multiplayer map** (two or more IsPlayer characters, see below), also:

- **F2 picks your character**: a menu over the running game shows a picture of every character
  you can play. Pick one with the arrow keys or the mouse and choose it with Enter or a click
  (F2 or Esc closes the menu). In the warm-up (and alone) you change at once, wherever you are,
  keeping your health, weapons and items ("Playing as:" under the name). In a match you come
  back as that character at your next respawn ("Next respawn:"); only the character you chose
  in the warm-up, or before joining a match in progress, takes over at once as the match starts.
  If you're dead, you come back as that character when you respawn. Every player has 125 health,
  whatever the character, so a Thug is as tough as a Muro. Your player stands still while the menu is open. The list can be changed
  with `characters` in `[multiplayer]`.
- **Variations**: most characters have other versions of themselves (Konoko's outfits, the
  Strikers' colors, the thug gangs). Press Q/E (or Page Up/Down), or click the `<  2 / 7  >` line
  under a character, to see them, then choose as usual.
- **Your name**: the F2 menu shows it in a text field under its title. Press Tab or click it, type a new one
  (up to 23 letters), and press Enter to keep it (Esc puts the old one back). It's saved, so
  you keep it next time. The score shows everyone's name, and when you look straight at
  another player, their name appears over their head.
- **Free for all**: players can hit, shoot and throw each other, though they're all on Konoko's
  team. AI characters keep their usual teams.
- **Damage is x1.25**: every hit on a character does a quarter more (rounded).
- **Unlimited taunts**: taunt whenever you like, not only right after a kill. Every player
  hears the same taunt line, and a new taunt cuts off your last one instead of waiting for it.
- **Aiming down while crouched**: every character aims a rifle as low as Konoko does (the gun
  used to stop halfway while the body kept bending). Ninjabot raises and aims the weapons it
  picks up.
- **Overhealing drains a quarter faster**: health above the maximum (from a hypo) goes down
  1 point every 8.8 ticks instead of every 11.

### Spawn points

Every character with the IsPlayer flag is a spawn point. The first one is Konoko, the next ones
are players 2, 3 and 4 when a network match has that many players (on one PC: Konoko and
player 2); the rest aren't spawned, they only mark more spawn points. A map takes as many
players as it has spawn points (at most 4), so a 4-player map needs at least four. Matches of 3 or 4 players are untested. Each player starts at a random spawn point and respawns at one that no other living
player is near (the farthest one if every point has someone close). The pick is the same on
every PC and in every replay. A spawn point gives the position and the facing; the class, team
and weapon come from the player's own placement, and every player starts with 125 health.

The `[players]` settings are in [Settings](#settings).

## Record and replay

Multiplayer sends only inputs between PCs, so the same inputs must give the same game on
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
format version is refused (record it again). the newest `AE/logs/Arklite_<date>_<time>.log` then says `MATCHED` or `DESYNCED`;
for a desync it gives the first tick that differs and, for the first 3 ticks that differ, the
fields that differ (up to 16 per character) with their exact bits.

Recordings are large (roughly 10–80 MB a minute with a few characters fighting).

## Network test

Two to four PCs (or Oni windows on one PC) play the same multiplayer map. **Only two-player
matches have been tested; 3 and 4 players are untested.** The host plays Konoko, the joiners play
players 2, 3 and 4 (the next IsPlayer characters) in the order they joined, and only inputs travel
between them. Each joiner talks only to the host, which passes every player's input on to the
others, so only the host needs an open port. A game takes up to the host's **Max players**, and no
more than the map has spawn points; anyone past that hears the server is full.
Every PC needs the same Arklite, the same AE and the same map, and must start it the same way.
The host checks this when someone joins: if a joiner's Arklite, Daodan, `daodan.ini`
gameplay settings, `level0_Final` files or map folder (`GameDataFolder/CustomLevels/<map>`) aren't
identical to the host's, the joiner stays out of the lobby and both screens say in red what
differs. Copy that file or folder from one PC to the other and join again.
When only `level0_Final` differs (two AE installs whose patches didn't come out the same), the
joiner gets the host's version and switches to it without restarting Oni, the way Unreal
Tournament fetches a server's packages: the level reloads, its loading box shows the download
(about 172 MB, Esc cancels), and the files are kept in `AE/cache/<dat hash>-<raw hash>/`, so the
next game with that version starts at once. Your own `GameDataFolder` files are never written.
The host's version is only used while you play on that host: when you host or play alone again,
Oni switches back to your own (through a link to your files in the cache, at the next level
load). Oni can switch up to three times per run (each keeps about 172 MB of memory); after that
it stays on the last version until you restart Oni (the log says so). Menus and fonts keep your
own version's look meanwhile.
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

Until a second player comes, you play the map alone (a joiner's first moments too, as player 2).
Then the level reloads on every PC into the **warm-up**: everyone sees each other and fights, in
step, with no limits. Kills are counted, but the match starts from 0. Press **F3** to be ready ("Press F3 to be ready"
turns from red to green; F3 again to be unready). When more than half of the players are ready, a
7-second countdown starts (it stops if someone unreadies). At 0 the level reloads on every PC and
the match begins. The score (kills; dying on your own costs a point) is in the top right corner,
with the time left under it during the match.

With rollback (`delay=0`, the default), the host doesn't wait for a second player: it starts the
warm-up alone at once. Anyone who joins a warm-up or a match then comes in without a reload. Their
PC loads the level and replays the game so far, muted and as fast as it can ("Joining the match:
catching up (n%)"), checking along the way that it matches the host's. Then they spawn at a spawn
point no one is near. The host keeps the game's inputs for about 97 minutes; after that, and when a
replay comes out different, a new player comes in the old way:

With input delay, someone joining during the warm-up reloads the level on every PC a moment
later, which starts the warm-up over (F3 stays as it was). If the countdown is running they come in
with the match instead. Someone joining during a match plays alone until it ends.

A match ends at the host's **Time limit** or **Score limit** (the Host dialog; 0 means none of
that limit). A negative score limit is reached going down, so the players race to kill
themselves. When it ends, the game stands still and shows who won, with the ranking if there were
more than two players; press **fire** (the left mouse button) to go back. When everyone did, or after 30 seconds, the level reloads on every PC
into the warm-up, where F3 starts the next match.

**F1** opens the help screen as usual, with the game still running. In a multiplayer game its
first tab is the **Scoreboard**: the time left (before the match, the time limit) and the score limit
on top, then every player's name, score and ping to the host. The host is marked, and players who
left are in blue. Tick **Mute** next to a player to hide their chat messages (until you untick it
or close Oni). The **Objective** tab says in one sentence how to win the match.
The host needs UDP port 27777 open. Over the internet the router must forward it to the host:
when you host, Arklite asks the router to do that itself (UPnP, most home routers allow it), and
a line in your chat says how it went and which address players on the internet join with. If it
says your provider shares the router's address (carrier-grade NAT: the router's internet address
is 100.64.x.x to 100.127.x.x, or another private one), forwarding can't help, but hole punching
usually does, and the **Arklite helper** does it for you:

- While you host, your game tells the helper (a small public server, `[net] helper`) what it is,
  every 15 seconds, and your chat says "Your game is on the server list". Players find it in
  **Join**, next to the games on their own network.
- When a player's game can't reach yours, it asks the helper, which tells your game the player's
  address, and your game knocks toward it: the same as `/punch`, without anyone typing it. A player
  who typed only your IP is also told your real port (behind a shared address it changes every
  time Oni starts).

The helper never carries the game itself: once both games reach each other they talk directly.
If the helper is down, or you set `listed=0` and the player doesn't know your address, punch by
hand:

1. Your chat shows your internet address when you host (`/ip` shows it again). It changes every
   time Oni starts. Give it to the player outside your network.
2. They join it. While your game can't be reached, their Join list says "No answer. Host types:
   /punch ..." with their address, and in the game "Connecting to the host..." has their address under it.
3. You type `/punch` with their address in your chat. Both games knock toward each other, each
   router takes the other's packets for answers, and they're in.

Players on your own network join you as always, by your LAN address. If punching doesn't work (some
routers and mobile networks don't allow it), ask your provider for a public IPv4 address, use a UDP
tunnel such as playit.gg, or let a player without that problem host. If the router doesn't answer
UPnP, forward UDP 27777 to your PC by hand (`upnp=0` under `[net]` stops Arklite asking).

Your PC only takes packets from players in your game: the host gives every address a key that
only a PC really at that address can read, and ignores packets without it (the log counts them as
"ignored ... from outside the game"). Received inputs are cleaned of impossible values first.
A joiner's camera, sound, HUD, laser sight and crosshair follow that joiner's own player.

The logs (`AE/logs/Arklite_<date>_<time>.log`; `Arklite-join_...` for a joiner started with
`ARKLITE_JOIN`) say whether the games started the same, and once a second the host and each joiner
compare the game state. `DESYNC` means the games came apart; the log lists this PC's characters at
that tick, and each PC writes `AE/logs/Arklite-desync-host_<date>_<time>.bin` or `...-join_...`
(`-join3` for player 3, and so on). `tools/desync_diff.py` compares two of them and prints every
value that differs. Each PC also writes `Arklite-desyncticks-host_...` / `...-join_...` (about
16 MB): the last 256 ticks one by one (every character, both random-number seeds and the inputs
each tick ran with), and `tools/desync_ticks.py` compares the two PCs' files and names the first
tick and the values where the games split. In a two-player match with input delay, each PC also records the whole game to
`AE/Arklite-net-host.akr` or `AE/Arklite-net-join.akr` (a few MB a minute; `record=0` under `[net]`
turns it off), and `tools/akr_diff.py` compares the two recordings tick by tick and names the first
tick, value and random-number calls that differ.

With rollback, some rollbacks are checked during the match (the first 40, then one in 16): their
ticks run a second time and the two runs are compared. A difference goes to
`AE/logs/Arklite-rollbackcheck_<date>_<time>.txt`.

If a joiner leaves, or sends nothing for 10 seconds, its character leaves the game (a dead one
leaves its corpse), the rest play on, and the chat says who left. Their place goes to the next
player who joins. If the host goes, a joiner plays on alone with everyone else standing
still.

Joining shows your IP address to the host, and hosting shows yours to whoever joins. While you
host, the server list shows your address, your game's name and map to anyone looking at Join
(`listed=0` under `[host]` hides it). The helper keeps addresses only in memory, drops a game 45
seconds after it stops hosting, and logs only counts.

## Troubleshooting

- **No Custom Game button**: open the newest `AE/logs/Arklite_<date>_<time>.log`. If it is missing or says
  `Arklite.dll not found` (that message is still `AE/Arklite.log`), run the installer again. Check that you started the `Oni.exe` in
  `Oni/AE/`, not the one in `Oni/`.
- **A feature does nothing**: the newest `AE/logs/Arklite_<date>_<time>.log` says which patches were applied. A patch is skipped
  when the code at its address isn't what Arklite expects (a different `Oni.exe` or Daodan).
- **Player 2 doesn't move**: the newest `AE/logs/Arklite_<date>_<time>.log` says `XInput didn't load` when no XInput DLL was
  found, and `player 2 is #...` once a level has a second player. Check `p2_pad` in `[players]`.
- **Oni crashes**: the end of the newest `AE/logs/Arklite_<date>_<time>.log` has the crash registers and stack. Include
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

The version is in `VERSION`, and Arklite writes it at the top of the newest `AE/logs/Arklite_<date>_<time>.log`.

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
| `tools/gen_respawn_fx.py` | Regenerates `src/respawn_fx_data.h`, the respawn effect's edit lists, from a Catwalk project's classes |
| `tools/oni-bench.sh` | Times a level's frames hands off (`ARKLITE_BENCH`). Set your paths at its top first; it needs a nested KWin and two small helper tools that aren't in this repository |

## License

Arklite is under the ISC license; see `LICENSE`. Oni, the Anniversary Edition, Daodan and Bink
belong to their owners and aren't covered by it.
