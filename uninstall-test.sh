#!/bin/bash
# Removes the Arklite test build (Linux/macOS): AE/arklite-test/, with the test build's options, logs
# and saves. The release, its options and its saves stay as they were; Arklite's loader stays too
# (it runs the release whenever there's no test build).
#
#   ./uninstall-test.sh [path/to/Oni/AE]      (or set ONI_AE_DIR)
#
# Without a path it looks where install-test.sh does (this file's folder and the ones around it, then the
# Wine prefixes in your home folder) for an AE folder with a test build in it. More than one: it asks which.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd -P)"

die() { echo "error: $*" >&2; echo "The test build was not removed." >&2; exit 1; }
is_ae() { [ -f "$1/Oni.exe" ] && [ -d "$1/arklite-test" ]; }
candidates() {
	local c
	for c in "$HERE" "$HERE/AE" "$HERE/.." "$HERE/../AE" "$HERE/../.." "$HERE/../../AE"; do
		is_ae "$c" && (cd "$c" && pwd -P)
	done
	return 0
}
installed() {
	local d c
	while IFS= read -r d; do
		for c in "$d"/*/Oni/AE "$d"/*/*/Oni/AE "$d"/Oni/AE; do is_ae "$c" && (cd "$c" && pwd -P); done
	done < <(find "$HOME" -maxdepth 6 -type d -name drive_c -prune 2>/dev/null)
	for c in "$HOME"/Games/Oni/AE "$HOME"/Oni/AE; do is_ae "$c" && (cd "$c" && pwd -P); done
	return 0
}

AE="${1:-${ONI_AE_DIR:-}}"
if [ -z "$AE" ]; then
	mapfile -t found < <(candidates | awk '!seen[$0]++')
	[ ${#found[@]} -gt 0 ] || mapfile -t found < <(installed | awk '!seen[$0]++')
	case ${#found[@]} in
	0) echo "No test build found: nothing to remove."; exit 0 ;;
	1) AE="${found[0]}" ;;
	*) echo "Test builds found in:"; for i in "${!found[@]}"; do echo "  $((i + 1)). ${found[$i]}"; done
	   [ -t 0 ] || die "more than one; run: $0 \"/path/to/Oni/AE\""
	   read -r -p "Which one (1-${#found[@]})? " i
	   [[ "$i" =~ ^[0-9]+$ ]] && [ "$i" -ge 1 ] && [ "$i" -le ${#found[@]} ] || die "\"$i\" isn't one of them"
	   AE="${found[$((i - 1))]}" ;;
	esac
fi
AE="${AE%/}"
[ -f "$AE/Oni.exe" ] || die "$AE has no Oni.exe; give the AE folder (Oni/AE)"
[ -d "$AE/arklite-test" ] || { echo "No test build in $AE: nothing to remove."; exit 0; }

rm -f "$AE/arklite-test/active"   # first the switch, so a folder half removed never runs
rm -rf "$AE/arklite-test"
echo "The test build is removed from $AE; Oni runs the release, with its own options and saves."
