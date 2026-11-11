---
name: update-shortcuts
description: Register every key, mouse gesture or chat command added or changed in Arklite in the current conversation in the README, TECHNICAL.md and the game's own on-screen hints, after checking it doesn't clash with Oni's or Arklite's other keys. Use after implementing or changing any shortcut (key, key combo, mouse button, wheel) or chat command in Arklite, before reporting the work as done.
---

# Update shortcuts in the docs and in the game

Arklite has no central key table: each module polls its own keys (`GetAsyncKeyState(VK_F2)` in
`src/char_menu.c`, `VK_F12` in `src/screenshot.c`, `VK_F1`/`VK_F3` in `src/net.c`...). So every
shortcut you implemented or changed **in the current conversation** has to be written down by
hand in each place below that applies to it.

## 1. List this conversation's shortcuts

Go back through the conversation and list every key, mouse button, wheel action and chat command
you added, rebound or removed. For each one, note its keys, what it does, and where it works:
alone or in multiplayer, in a level, in the menus, while a level loads.

## 2. Check for clashes

Grep `src/` for the virtual-key code (`VK_F8`, `0x77`...) and for the command name, so it isn't
already taken by another Arklite module. Check it isn't one of Oni's own keys in that situation
either (F1 is Oni's help screen, Enter is chat in multiplayer...). Say so in your reply if
anything still overlaps on purpose.

## 3. README (`README.md`, Features or For modders)

Players learn the keys here. Name the key in bold in its feature's line, the way F2 and F12 are
("**Pick your character with F2**", "**Screenshots with F12**"). Keep it short and player-facing:
one line per feature, no settings or internals. A new chat command goes into the list in the
Network multiplayer line ("/name, /w (whisper), ...").

## 4. TECHNICAL.md

Add the detail where its feature is described: when the key works and what it does there, and
the matching `AE/Arklite.ini` setting in the Settings table if there is one. A chat command gets
a row in the chat commands table (`| /name New Name | ... |`).

## 5. In-game hints

If the game itself tells players about the key (a prompt like "Press F3 to be ready", the
`/help` text in the chat, a dialog's text), update it too, and the screenshots of that screen in
`screenshots/` if the text changed visibly.

## 6. Build

Run `./build.sh`. In your reply, list each shortcut and where you added it: README, TECHNICAL.md,
in-game text, and whether anything clashed.
