#!/bin/bash
# Removes the links made by install.sh and puts the original realbink.dll back.
# persist.dat is left alone; a copy from install time is in backup/persist.dat.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
AE="${ONI_AE_DIR:-/home/arkl1te/Games/oni-x/drive_c/Program Files (x86)/Oni/AE}"

[ -L "$AE/Arklite.dll" ] && rm "$AE/Arklite.dll"
[ -L "$AE/realbink.dll" ] && rm "$AE/realbink.dll" && cp -p "$HERE/backup/realbink.dll" "$AE/realbink.dll"
[ -L "$AE/bink_orig.dll" ] && rm "$AE/bink_orig.dll"
[ -L "$AE/GameDataFolder/CustomLevels" ] && rm "$AE/GameDataFolder/CustomLevels"
echo "uninstalled from $AE"
