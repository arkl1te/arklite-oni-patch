#!/bin/bash
# Removes the Arklite test build (Linux/macOS): AE/arklite-test/, with the test build's options, logs
# and saves. The release, its options and its saves stay as they were; Arklite's loader stays too
# (it runs the release whenever there's no test build).
#
#   ./uninstall-test.sh [path/to/Oni/AE]      (or set ONI_AE_DIR)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

die() { echo "error: $*" >&2; echo "The test build was not removed." >&2; exit 1; }
is_ae() { [ -f "$1/Oni.exe" ] && [ -d "$1/arklite-test" ]; }
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

AE="${1:-${ONI_AE_DIR:-}}"
[ -n "$AE" ] || AE="$(find_near)"
if [ -z "$AE" ]; then
	mapfile -t found < <(find_ae | sort -u)
	case ${#found[@]} in
	1) AE="${found[0]}" ;;
	0) echo "No test build found: nothing to remove."; exit 0 ;;
	*) printf 'found more than one AE folder with a test build:\n'; printf '  %s\n' "${found[@]}"
	   die "pick one: $0 \"/path/to/Oni/AE\"" ;;
	esac
fi
AE="${AE%/}"
[ -f "$AE/Oni.exe" ] || die "$AE has no Oni.exe; give the AE folder (Oni/AE)"
[ -d "$AE/arklite-test" ] || { echo "No test build in $AE: nothing to remove."; exit 0; }

rm -f "$AE/arklite-test/active"   # first the switch, so a folder half removed never runs
rm -rf "$AE/arklite-test"
echo "The test build is removed; Oni runs the release, with its own options and saves."
