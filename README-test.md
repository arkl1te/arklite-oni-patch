# Arklite test build

A build of Arklite that isn't released yet, to try before everyone else. It installs beside your
Arklite release without changing it: your release, its options and its saves stay exactly as they are.

## Install

1. Extract this zip anywhere (inside Oni's AE folder is the surest).
2. Windows: double-click `install-test.bat`. Linux: run `./install-test.sh`.
   It finds Oni's AE folder by itself; with more than one Oni it asks which, and with none it asks for the path.
3. Start Oni as usual. The bottom of the screen shows **Arklite test** and the build's commit.

The first time, your options and saves are copied for the test build. It plays with those copies only.
Without Arklite's release in that folder the test build still works; switched off, Oni then runs without Arklite.

## While testing

- You play with other testers on the same test build. The Join list shows only their games.
- Crashes, desyncs and dropped games are reported to the developer on their own, as in the release.

## Back to the release

- **Options > More...**: uncheck **Test build**, then restart Oni. Check it again to come back.
- Or remove the test build: `uninstall-test.bat` (Linux: `./uninstall-test.sh`). It deletes the test
  build with its options, logs and saves; your release is untouched.

A newer test build installs the same way, over this one, and keeps your test saves.
