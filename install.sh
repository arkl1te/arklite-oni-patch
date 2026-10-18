#!/bin/bash
# Installs Arklite into Oni Anniversary Edition's AE folder (Linux/macOS, Oni under Wine).
# Undo with uninstall.sh.
#
#   ./install.sh [path/to/Oni/AE]      (or set ONI_AE_DIR; otherwise common Wine prefixes are searched)
#
# From a release zip it copies Arklite.dll and realbink.dll into AE/. From the source repository
# it builds them if needed and links them instead, so a rebuild is picked up without reinstalling.
#
#   AE/Arklite.dll                  the mod
#   AE/realbink.dll                 Arklite's loader (Daodan loads this name)
#   AE/bink_orig.dll                AE's original realbink.dll (Bink), renamed; the loader forwards to it
#   AE/GameDataFolder/CustomLevels  where Custom Game looks for levels
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MARKER="Arklite.dll not found"   # a string only Arklite's loader contains
ONI_SHA256=363cf09684ca6b729b62f22f90701d7ebf9a33778e55b191f5040bccdcdd013d

die() { echo "error: $*" >&2; exit 1; }
# Arklite's loader: a link (development install) or a file with the marker (3.5 KB; Bink is ~280 KB).
is_loader() { [ -L "$1" ] || { [ -f "$1" ] && grep -aqF "$MARKER" "$1"; }; }

find_ae() {
	local c
	for c in "$HERE/.." "$HERE/../AE" "$HOME"/.wine/drive_c/"Program Files (x86)"/Oni/AE \
	         "$HOME"/.wine/drive_c/"Program Files"/Oni/AE \
	         "$HOME"/Games/*/drive_c/"Program Files (x86)"/Oni/AE \
	         "$HOME"/Games/*/drive_c/"Program Files"/Oni/AE \
	         "$HOME"/.local/share/bottles/bottles/*/drive_c/"Program Files (x86)"/Oni/AE \
	         "$HOME"/Games/Oni/AE "$HOME"/Oni/AE; do
		[ -f "$c/Oni.exe" ] && { [ -e "$c/realbink.dll" ] || [ -e "$c/bink_orig.dll" ]; } && (cd "$c" && pwd -P)
	done
}

AE="${1:-${ONI_AE_DIR:-}}"
if [ -z "$AE" ]; then
	mapfile -t found < <(find_ae | sort -u)
	case ${#found[@]} in
	1) AE="${found[0]}" ;;
	0) die "couldn't find Oni's AE folder; run: $0 \"/path/to/Oni/AE\"" ;;
	*) printf 'found more than one AE folder:\n'; printf '  %s\n' "${found[@]}"
	   die "pick one: $0 \"/path/to/Oni/AE\"" ;;
	esac
fi
AE="${AE%/}"

[ -f "$AE/Oni.exe" ]     || die "$AE has no Oni.exe; give the AE folder (Oni/AE), not Oni/"
[ -e "$AE/realbink.dll" ] || [ -e "$AE/bink_orig.dll" ] || die "$AE has no realbink.dll; give the AE folder (Oni/AE), and install Anniversary Edition if it isn't there"
[ -f "$AE/binkw32.dll" ] || die "$AE has no binkw32.dll; install Anniversary Edition (it brings the Daodan DLL)"
if command -v sha256sum >/dev/null; then
	sum="$(sha256sum "$AE/Oni.exe" | cut -d' ' -f1)"
	[ "$sum" = "$ONI_SHA256" ] || echo "warning: AE/Oni.exe is not the one Arklite was made for; patches that don't match will be skipped (see AE/Arklite.log)"
fi

# Release zip or source repository?
if [ -f "$HERE/Arklite.dll" ]; then
	SRC="$HERE"; MODE=copy
else
	[ -f "$HERE/build/Arklite.dll" ] && [ -f "$HERE/build/realbink.dll" ] || "$HERE/build.sh"
	SRC="$HERE/build"; MODE=link
fi

# Keep AE's original realbink.dll (Bink) as bink_orig.dll. When realbink.dll is not Arklite's loader,
# it is the original: a first install, or the AE Installer put it back.
if [ -e "$AE/realbink.dll" ] && ! is_loader "$AE/realbink.dll"; then
	rm -f "$AE/bink_orig.dll"
	mv "$AE/realbink.dll" "$AE/bink_orig.dll"
elif [ ! -e "$AE/bink_orig.dll" ]; then
	die "$AE has no original realbink.dll (Bink) to keep; run the AE Installer again, then this script"
fi

# One copy of the save file from before Arklite (custom levels can write save points to it).
[ -f "$AE/persist.dat" ] && [ ! -e "$AE/persist.dat.before-arklite" ] && cp -p "$AE/persist.dat" "$AE/persist.dat.before-arklite"

if [ $MODE = copy ]; then
	rm -f "$AE/Arklite.dll" "$AE/realbink.dll"
	cp "$SRC/Arklite.dll" "$SRC/realbink.dll" "$AE/"
	[ -L "$AE/GameDataFolder/CustomLevels" ] || mkdir -p "$AE/GameDataFolder/CustomLevels"
else
	ln -sfn "$SRC/Arklite.dll"  "$AE/Arklite.dll"
	ln -sfn "$SRC/realbink.dll" "$AE/realbink.dll"
	# Development levels, if this repository sits next to them.
	if [ -d "$HERE/../custom-game/CustomLevels" ] && [ ! -e "$AE/GameDataFolder/CustomLevels" -o -L "$AE/GameDataFolder/CustomLevels" ]; then
		ln -sfn "$(cd "$HERE/../custom-game/CustomLevels" && pwd)" "$AE/GameDataFolder/CustomLevels"
	else
		mkdir -p "$AE/GameDataFolder/CustomLevels"
	fi
fi
echo "Arklite $(cat "$SRC/VERSION" 2>/dev/null || cat "$HERE/VERSION" 2>/dev/null || echo) installed into $AE ($MODE)"
echo "Start Oni as usual; AE/Arklite.log lists what was patched."
