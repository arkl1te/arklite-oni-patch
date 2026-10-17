#!/bin/bash
# Links Arklite into the AE install. Reversible with uninstall.sh.
#   AE/Arklite.dll                 -> build/Arklite.dll            (the mod)
#   AE/realbink.dll                -> build/realbink.dll           (its loader; Daodan loads it)
#   AE/bink_orig.dll               -> backup/realbink.dll          (original Bink, our exports forward to it)
#   AE/GameDataFolder/CustomLevels -> ../custom-game/CustomLevels/
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
AE="${ONI_AE_DIR:-/home/arkl1te/Games/oni-x/drive_c/Program Files (x86)/Oni/AE}"

[ -f "$HERE/build/Arklite.dll" ] && [ -f "$HERE/build/realbink.dll" ] || "$HERE/build.sh"
mkdir -p "$HERE/backup"

# Keep the original Bink DLL (and the save file, which custom levels may write to).
if [ ! -L "$AE/realbink.dll" ]; then
	mv "$AE/realbink.dll" "$HERE/backup/realbink.dll"
fi
[ -f "$HERE/backup/persist.dat" ] || cp -p "$AE/persist.dat" "$HERE/backup/persist.dat"

ln -sfn "$HERE/build/Arklite.dll"               "$AE/Arklite.dll"
ln -sfn "$HERE/build/realbink.dll"              "$AE/realbink.dll"
ln -sfn "$HERE/backup/realbink.dll"             "$AE/bink_orig.dll"
ln -sfn "$(cd "$HERE/../custom-game/CustomLevels" && pwd)" "$AE/GameDataFolder/CustomLevels"
echo "installed into $AE"
