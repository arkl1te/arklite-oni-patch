#!/bin/bash
# Zips the maps listed in maps.list and uploads them to the "maps" GitHub release, replacing
# the zips already there (the download links stay the same):
#   https://github.com/<repo>/releases/download/maps/<Map>.zip
# The release notes come from MAPS.md. The release is never marked Latest, because the
# installers download the first zip of the latest release and that must stay Arklite's.
#
#   tools/release-maps.sh            check, zip and upload
#   tools/release-maps.sh --dry-run  check and zip into dist/maps/ only
#
# CUSTOMLEVELS=<dir> picks the folder holding the maps (default: the AE install's).
# The check refuses a map that packs a texture of the game (see tools/mapcheck.py).
set -euo pipefail
cd "$(dirname "$0")/.."
DRY=0; [ "${1:-}" = --dry-run ] && DRY=1
AE_DATA="${AE_DATA:-$HOME/Games/oni-x/drive_c/Program Files (x86)/Oni/AE/GameDataFolder}"
CUSTOMLEVELS="${CUSTOMLEVELS:-$AE_DATA/CustomLevels}"
TAG=maps
VERSION="$(cat VERSION)"

mapfile -t MAPS < <(grep -v -e '^#' -e '^[[:space:]]*$' maps.list)
[ ${#MAPS[@]} -gt 0 ] || { echo "maps.list names no maps"; exit 1; }
for m in "${MAPS[@]}"; do
	[ -f "$CUSTOMLEVELS/$m/level50_Final.dat" ] || { echo "$m: no level50_Final.dat in $CUSTOMLEVELS/$m"; exit 1; }
done

python3 -I tools/mapcheck.py "$AE_DATA" "${MAPS[@]/#/$CUSTOMLEVELS/}"

rm -rf dist/maps; mkdir -p dist/maps
list=""
for m in "${MAPS[@]}"; do
	# -X: no extra file attributes; the zip holds <Map>/..., ready to unzip into CustomLevels.
	(cd "$CUSTOMLEVELS" && zip -qrX "$OLDPWD/dist/maps/$m.zip" "$m")
	size="$(du -k "dist/maps/$m.zip" | cut -f1)"
	list+="- **$m** ($((size / 1024)).$(( (size % 1024) * 10 / 1024 )) MB)"$'\n'
	echo "zipped dist/maps/$m.zip"
done
notes="dist/maps/notes.md"
awk -v v="$VERSION" -v maps="$list" '{ gsub(/@VERSION@/, v); if ($0 == "@MAPS@") printf "%s", maps; else print }' MAPS.md > "$notes"

if [ $DRY = 1 ]; then echo "dry run: nothing uploaded (notes in $notes)"; exit 0; fi

if gh release view "$TAG" >/dev/null 2>&1; then
	gh release upload "$TAG" dist/maps/*.zip --clobber
	gh release edit "$TAG" --notes-file "$notes"
else
	gh release create "$TAG" dist/maps/*.zip --target main --title "Maps" --notes-file "$notes" --latest=false
fi
echo "uploaded ${#MAPS[@]} maps to the $TAG release"
