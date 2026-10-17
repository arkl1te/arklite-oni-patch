#!/bin/bash
# Removes the link made by install.sh. onicustom simply skips Arklite.dll when it's missing
# (the Custom Game button goes away with it).
set -euo pipefail
AE="${ONI_AE_DIR:-/home/arkl1te/Games/oni-x/drive_c/Program Files (x86)/Oni/AE}"

[ -L "$AE/Arklite.dll" ] && rm "$AE/Arklite.dll"
echo "uninstalled from $AE"
