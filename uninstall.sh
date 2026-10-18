#!/bin/bash
# Removes Arklite from Oni's AE folder and puts AE's original realbink.dll back.
#
#   ./uninstall.sh [--purge] [path/to/Oni/AE]   (or set ONI_AE_DIR)
#
# Kept unless --purge: AE/Arklite.ini (settings) and AE/Arklite.log. Always kept: custom levels
# in AE/GameDataFolder/CustomLevels (an empty folder is removed), recordings (*.akr), persist.dat
# and persist.dat.before-arklite (the save file as it was before the first install).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MARKER="Arklite.dll not found"

die() { echo "error: $*" >&2; exit 1; }
# Arklite's loader: a link (development install) or a file with the marker (3.5 KB; Bink is ~280 KB).
is_loader() { [ -L "$1" ] || { [ -f "$1" ] && grep -aqF "$MARKER" "$1"; }; }

PURGE=0
[ "${1:-}" = --purge ] && { PURGE=1; shift; }
AE="${1:-${ONI_AE_DIR:-}}"
if [ -z "$AE" ]; then
	# Same search as install.sh, but only folders Arklite is installed in.
	mapfile -t found < <(for c in "$HERE/.." "$HERE/../AE" "$HOME"/.wine/drive_c/"Program Files"*/Oni/AE "$HOME"/Games/*/drive_c/"Program Files"*/Oni/AE \
	                             "$HOME"/.local/share/bottles/bottles/*/drive_c/"Program Files"*/Oni/AE "$HOME"/Games/Oni/AE "$HOME"/Oni/AE; do
		[ -e "$c/Arklite.dll" ] && (cd "$c" && pwd -P); done | sort -u)
	case ${#found[@]} in
	1) AE="${found[0]}" ;;
	0) die "couldn't find an AE folder with Arklite in it; run: $0 \"/path/to/Oni/AE\"" ;;
	*) printf 'Arklite is in more than one AE folder:\n'; printf '  %s\n' "${found[@]}"
	   die "pick one: $0 \"/path/to/Oni/AE\"" ;;
	esac
fi
AE="${AE%/}"
[ -f "$AE/Oni.exe" ] || die "$AE has no Oni.exe; give the AE folder (Oni/AE)"

# Original Bink back first: without a realbink.dll, Daodan (and Oni with it) won't start.
if is_loader "$AE/realbink.dll" || [ ! -e "$AE/realbink.dll" ]; then
	if [ -e "$AE/bink_orig.dll" ]; then
		rm -f "$AE/realbink.dll"
		cp -L "$AE/bink_orig.dll" "$AE/realbink.dll"   # -L: older dev installs linked it
		rm "$AE/bink_orig.dll"
	elif [ -f "$HERE/backup/realbink.dll" ]; then
		rm -f "$AE/realbink.dll"; cp "$HERE/backup/realbink.dll" "$AE/realbink.dll"
	else
		die "AE/bink_orig.dll (the original realbink.dll) is missing; run the AE Installer to restore it, then this script"
	fi
else
	rm -f "$AE/bink_orig.dll"   # realbink.dll is already the original (the AE Installer put it back)
fi

rm -f "$AE/Arklite.dll"
if [ -L "$AE/GameDataFolder/CustomLevels" ]; then
	rm "$AE/GameDataFolder/CustomLevels"
else
	rmdir "$AE/GameDataFolder/CustomLevels" 2>/dev/null || true
fi
[ $PURGE = 1 ] && rm -f "$AE/Arklite.ini" "$AE/Arklite.log"
echo "Arklite removed from $AE"
[ $PURGE = 1 ] || echo "Kept AE/Arklite.ini and AE/Arklite.log (--purge removes them)."
