#!/bin/bash
# Links Arklite.dll into the AE install. It is loaded by onicustom (../custom-game), which
# must be installed too. Reversible with uninstall.sh.
#   AE/Arklite.dll -> build/Arklite.dll
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
AE="${ONI_AE_DIR:-/home/arkl1te/Games/oni-x/drive_c/Program Files (x86)/Oni/AE}"

[ -f "$HERE/build/Arklite.dll" ] || "$HERE/build.sh"
ln -sfn "$HERE/build/Arklite.dll" "$AE/Arklite.dll"
echo "installed into $AE"
