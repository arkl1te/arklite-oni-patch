# Arklite

![Screenshot](screenshots/15-more-options-ingame.png)
(See screenshots folder for more...)

Quality-of-life fixes and network multiplayer for [Oni](https://oni.bungie.org/)'s Anniversary
Edition, in one DLL. It patches the game in memory when it starts, so no game file is changed.

Arklite's aim is to be the Oni update that never came.

## Features

- **Network multiplayer** for 2 to 4 players (as many as the map has spawn points). Host a map from the menu, or join one from your server list or your local network. Matches have a lobby, time and score limits, a scoreboard (F1) and chat (Enter), which also says when someone enters or leaves the game. The other players are predicted, so there's no input delay.
- **Pick your character with F2**, from Konoko to Mad Bomber, with their outfits and colors. Type your name there too, and it shows over your head.
- **Custom Game** on the main menu lists every custom level. Play it alone or host it. Joiners who don't have the map download it from the host.
- **A HUD that scales** from 50% to 300%, and stays sharp. The stock HUD gets tiny at high resolutions.
- **Separate Sound, Music and Voice volume**, with all character speech and taunts on Voice.
- **An FPS counter**, off by default, in Options > More...
- **A see-through pause menu**, and the level music keeps playing behind it if you like.
- **Up to 255 characters**, all active at once, so big fights no longer crash the game.
- **No more seams** along the edges of the sky.
- **No more tiny strikers** outside the dream lab.
- **Player 2 on a gamepad**, next to Konoko on keyboard and mouse.
- **A crash log** in `AE/logs/`, dated, with the last three games kept, for when something goes wrong.

Everything is in Options → **More...** and in the **Custom Game** menu. Press F1 in a match for the scoreboard, and F2 to change character.

## For modders

- **A folder per custom level.** Drop it in `GameDataFolder/CustomLevels/`. A level doesn't need its own level number, and its files never replace the original levels. It can bring its own BSL scripts (`IGMD/`) and up to 8 plugin `.dat` files.
- **Dynamic vertex lighting.** Lights pulse, flicker or break down, from a `vertexlight.bin` next to the level.
- **255 characters, 255 active, 255 shadows**, up from 128, 64 and 32.
- **Multiplayer maps from any level.** Every character with the IsPlayer flag is a spawn point, and each weapon or power-up can set its own respawn time.
- **Maps travel by themselves.** A joiner whose map differs or is missing gets the host's copy.
- **Jump straight in for testing.** `ARKLITE_AUTOSTART=<folder>` starts a level from the command line, and `ARKLITE_SAVEPOINT=<n>` starts it at a save point.
- **Record and replay** a level's input, with every tick checked against the recording.
- **Crash reports you can read.** The registers and the stack values that point into Oni, Daodan or Arklite.

## Install

You need Oni with the [Anniversary Edition](https://oni.bungie.org/) installed. Download
`Arklite-<version>.zip` from the [Releases](../../releases) page and extract it into `Oni/AE/`.
Close Oni, then:

### Windows

Double-click `install.bat`. If Oni is in `C:\Program Files (x86)`, right-click it and choose
**Run as administrator**.

### Linux / macOS (Wine)

```sh
./install.sh
```

It finds the game in the usual Wine, Lutris and Bottles folders. Otherwise, pass the AE folder:
`./install.sh "/path/to/Oni/AE"`.

Start Oni from `Oni/AE/` as usual: the main menu now has a **Custom Game** button.

To update, run the new version's installer, and run it again after the AE Installer. `uninstall.bat` or `./uninstall.sh` removes Arklite and keeps your saves.

More details in [TECHNICAL.md](TECHNICAL.md).

---

<sub>Arklite is an unofficial fan project, not affiliated with or endorsed by Bungie, Take-Two
Interactive or Rockstar. It contains no Oni code or data: you need your own copy of Oni.</sub>
