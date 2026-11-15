#!/bin/bash
# Installs this Arklite test build beside the release (Linux/macOS, Oni under Wine). Undo with
# uninstall-test.sh.
#
#   ./install-test.sh [path/to/Oni/AE]      (or set ONI_AE_DIR; otherwise found as install.sh does)
#
# Needs Arklite's release installed (install.sh from a release zip). The release isn't touched:
#   AE/arklite-test/Arklite.dll   this test build
#   AE/arklite-test/build.txt     its commit and date (Options > More... shows them)
#   AE/arklite-test/active        Oni runs the test build while this file is there
#   AE/arklite-test/Arklite.ini, persist.dat, saves/
#                                 its own options and saves: copies of the release's, made the first
#                                 time only (a newer test build keeps the test ones)
#   AE/realbink.dll               Arklite's loader, a version that can run the test build (it runs the
#                                 release as before whenever arklite-test/active isn't there)
# Options > More... switches between the test build and the release (from the next start).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MARKER="Arklite.dll not found"   # a string only Arklite's loader contains

die() { echo "error: $*" >&2; echo "The test build was not installed." >&2; exit 1; }
is_loader() { [ -L "$1" ] || { [ -f "$1" ] && grep -aqF "$MARKER" "$1"; }; }
# An AE folder with Arklite's release in it
is_ae() { [ -f "$1/Oni.exe" ] && [ -e "$1/bink_orig.dll" ] && [ -e "$1/Arklite.dll" ]; }
find_near() {
	local c
	for c in "$HERE/AE" "$HERE" "$HERE/../AE" "$HERE/.." "$HERE/../.."; do
		is_ae "$c" && { (cd "$c" && pwd -P); return 0; }
	done
	return 0
}
find_ae() {
	local c
	for c in "$HOME"/.wine/drive_c/"Program Files (x86)"/Oni/AE \
	         "$HOME"/.wine/drive_c/"Program Files"/Oni/AE \
	         "$HOME"/Games/*/drive_c/"Program Files (x86)"/Oni/AE \
	         "$HOME"/Games/*/drive_c/"Program Files"/Oni/AE \
	         "$HOME"/.local/share/bottles/bottles/*/drive_c/"Program Files (x86)"/Oni/AE \
	         "$HOME"/Games/Oni/AE "$HOME"/Oni/AE; do
		is_ae "$c" && (cd "$c" && pwd -P)
	done
	return 0
}

for f in Arklite.dll arklite_loader.dll build.txt; do [ -f "$HERE/$f" ] || die "$f is not next to install-test.sh"; done

AE="${1:-${ONI_AE_DIR:-}}"
[ -n "$AE" ] || AE="$(find_near)"
if [ -z "$AE" ]; then
	mapfile -t found < <(find_ae | sort -u)
	case ${#found[@]} in
	1) AE="${found[0]}" ;;
	0) die "couldn't find Oni's AE folder with Arklite in it; install the release first, or run: $0 \"/path/to/Oni/AE\"" ;;
	*) printf 'found more than one AE folder with Arklite:\n'; printf '  %s\n' "${found[@]}"
	   die "pick one: $0 \"/path/to/Oni/AE\"" ;;
	esac
fi
AE="${AE%/}"

[ -f "$AE/Oni.exe" ] || die "$AE has no Oni.exe; give the AE folder (Oni/AE)"
[ -e "$AE/Arklite.dll" ] && [ -e "$AE/bink_orig.dll" ] || die "Arklite isn't installed in $AE: install the release first (install.sh from a release zip)"
is_loader "$AE/realbink.dll" || die "AE's realbink.dll isn't Arklite's loader (the AE Installer put Bink back): run the release's install.sh first"
[ ! -L "$AE/realbink.dll" ] || die "$AE has a development install (linked from a source tree): use build.sh's ARKLITE_TEST=1 there instead"

T="$AE/arklite-test"
mkdir -p "$T"
rm -f "$T/Arklite.dll"   # (a new file: Wine may have the old one mapped)
cp "$HERE/Arklite.dll" "$T/Arklite.dll"
cp -f "$HERE/build.txt" "$T/build.txt"

# The release's options and saves, copied once: the test build reads and writes only these copies.
if [ ! -e "$T/copied-from-release.txt" ]; then
	[ ! -f "$AE/Arklite.ini" ] || cp -p "$AE/Arklite.ini" "$T/Arklite.ini"
	[ ! -f "$AE/persist.dat" ] || cp -p "$AE/persist.dat" "$T/persist.dat"
	[ ! -d "$AE/saves" ] || cp -pR "$AE/saves" "$T/saves"
	echo "The release's options and saves, copied for the test build" > "$T/copied-from-release.txt"
	echo "Copied the release's options and saves for the test build (the release's stay as they are)."
fi

rm -f "$AE/realbink.dll"
cp "$HERE/arklite_loader.dll" "$AE/realbink.dll"
: > "$T/active"
echo "Arklite test build $(head -n1 "$HERE/build.txt") installed into $T."
echo "Start Oni as usual: the bottom of the screen shows the test build. Options > More... switches back"
echo "to the release; uninstall-test.sh removes the test build and its saves."
