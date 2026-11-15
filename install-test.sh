#!/bin/bash
# Installs this Arklite test build beside the release (Linux/macOS, Oni under Wine). Undo with
# uninstall-test.sh.
#
#   ./install-test.sh [path/to/Oni/AE]      (or set ONI_AE_DIR)
#
# Without a path it looks for Oni's AE folder: this file's own folder and the folders around it (the
# zip extracted into AE or next to it), then the Wine prefixes in your home folder. More than one: it
# asks which. An AE folder has Oni.exe and AE's binkw32.dll (the Daodan DLL, over 1 MB).
#
# The release isn't touched:
#   AE/arklite-test/Arklite.dll   this test build
#   AE/arklite-test/build.txt     its commit and date (Options > More... shows them)
#   AE/arklite-test/active        Oni runs the test build while this file is there
#   AE/arklite-test/Arklite.ini, persist.dat, saves/
#                                 its own options and saves: copies of the release's, made the first
#                                 time only (a newer test build keeps the test ones)
#   AE/realbink.dll               Arklite's loader, a version that can run the test build (it runs the
#                                 release as before whenever arklite-test/active isn't there)
#   AE/bink_orig.dll              AE's original realbink.dll (Bink), kept as install.sh keeps it
# Options > More... switches between the test build and the release (from the next start).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd -P)"
MARKER="Arklite.dll not found"   # a string only Arklite's loader contains

die() { echo "error: $*" >&2; echo "The test build was not installed." >&2; exit 1; }
is_loader() { [ -L "$1" ] || { [ -f "$1" ] && grep -aqF "$MARKER" "$1"; }; }
# An AE folder: Oni.exe and AE's binkw32.dll (the Daodan DLL; Oni/ has the plain Bink one, 280 KB)
is_ae() { [ -f "$1/Oni.exe" ] && [ -f "$1/binkw32.dll" ] && [ "$(wc -c < "$1/binkw32.dll")" -ge 1000000 ]; }
candidates() {
	local c
	for c in "$HERE" "$HERE/AE" "$HERE/.." "$HERE/../AE" "$HERE/../.." "$HERE/../../AE"; do
		is_ae "$c" && (cd "$c" && pwd -P)
	done
	return 0
}
installed() {   # every Wine prefix in the home folder (its drive_c), a few folders deep
	local d c
	while IFS= read -r d; do
		for c in "$d"/*/Oni/AE "$d"/*/*/Oni/AE "$d"/Oni/AE; do is_ae "$c" && (cd "$c" && pwd -P); done
	done < <(find "$HOME" -maxdepth 6 -type d -name drive_c -prune 2>/dev/null)
	for c in "$HOME"/Games/Oni/AE "$HOME"/Oni/AE; do is_ae "$c" && (cd "$c" && pwd -P); done
	return 0
}
pick() {   # the only one, or the one chosen
	local found=("$@") i
	[ ${#found[@]} -eq 1 ] && { AE="${found[0]}"; echo "Oni's AE folder: $AE"; return; }
	echo "Oni's AE folders found:"; for i in "${!found[@]}"; do echo "  $((i + 1)). ${found[$i]}"; done
	[ -t 0 ] || die "more than one; run: $0 \"/path/to/Oni/AE\""
	read -r -p "Which one (1-${#found[@]})? " i
	[[ "$i" =~ ^[0-9]+$ ]] && [ "$i" -ge 1 ] && [ "$i" -le ${#found[@]} ] || die "\"$i\" isn't one of them"
	AE="${found[$((i - 1))]}"
}

for f in Arklite.dll arklite_loader.dll build.txt; do [ -f "$HERE/$f" ] || die "$f is not next to install-test.sh"; done

AE="${1:-${ONI_AE_DIR:-}}"
if [ -z "$AE" ]; then
	mapfile -t found < <(candidates | awk '!seen[$0]++')
	[ ${#found[@]} -gt 0 ] || mapfile -t found < <(installed | awk '!seen[$0]++')
	[ ${#found[@]} -gt 0 ] || die "couldn't find Oni's AE folder; run: $0 \"/path/to/Oni/AE\""
	pick "${found[@]}"
fi
AE="${AE%/}"

[ -f "$AE/Oni.exe" ] || die "$AE has no Oni.exe; give the AE folder (Oni/AE)"
[ -f "$AE/binkw32.dll" ] || die "$AE has no binkw32.dll; install Anniversary Edition (it brings the Daodan DLL)"
[ -e "$AE/realbink.dll" ] || [ -e "$AE/bink_orig.dll" ] || die "$AE has no realbink.dll; give the AE folder (Oni/AE), and install Anniversary Edition if it isn't there"
[ ! -L "$AE/realbink.dll" ] || die "$AE has a development install (linked from a source tree): use build.sh's ARKLITE_TEST=1 there instead"

# The zip's files extracted straight into AE/: AE/Arklite.dll is this test build (it replaced the release's).
SAME=; [ "$HERE" -ef "$AE" ] && SAME=1
RELEASE=1; { [ -f "$AE/Arklite.dll" ] && [ -z "$SAME" ]; } || RELEASE=

# Arklite's loader needs AE's original realbink.dll (Bink) as bink_orig.dll, as install.sh keeps it.
if [ -e "$AE/realbink.dll" ] && ! is_loader "$AE/realbink.dll"; then
	rm -f "$AE/bink_orig.dll"
	mv "$AE/realbink.dll" "$AE/bink_orig.dll"
elif [ ! -e "$AE/bink_orig.dll" ]; then
	for b in "$AE"/AEInstaller/packages/*Daodan*/plain/win_only/realbink.dll "$AE/../binkw32.dll"; do
		if [ -f "$b" ] && ! is_loader "$b" && [ "$(wc -c <"$b")" -ge 65536 ]; then
			cp "$b" "$AE/bink_orig.dll"
			echo "AE's realbink.dll (Bink) had been replaced; put it back from $b"
			break
		fi
	done
	[ -e "$AE/bink_orig.dll" ] || die "$AE has no original realbink.dll (Bink) to keep; run the AE Installer again, then this script"
fi

T="$AE/arklite-test"
mkdir -p "$T"
rm -f "$T/Arklite.dll"   # (a new file: Wine may have the old one mapped)
cp "$HERE/Arklite.dll" "$T/Arklite.dll"
cp -f "$HERE/build.txt" "$T/build.txt"
[ -z "$SAME" ] || rm -f "$AE/Arklite.dll"

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
[ -z "$SAME" ] || echo "The zip was extracted straight into AE, so its Arklite.dll had replaced the release's there."
[ -n "$RELEASE" ] || echo "Note: no Arklite release in $AE: switched off, Oni runs without Arklite. install.sh from a release zip puts one beside the test build."
