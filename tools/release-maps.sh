#!/bin/bash
# Zips the maps listed in maps.list, each as <Map>.zip, and packs them with notes.md (MAPS.md with
# the version and the map list) into one Arklite-maps-<version>.zip, the layout of v0.4.0's maps
# zip: the files at its top, no folder. That zip goes to the latest GitHub release (the one marked
# Latest, e.g. v0.5.0), next to its Arklite-<version>.zip, replacing an earlier upload of it:
#   https://github.com/<repo>/releases/download/v<version>/Arklite-maps-<version>.zip
# The release's title and notes are never changed.
#
# The installers download the first .zip of the latest release, which must stay Arklite's. GitHub
# lists a release's assets by name, ignoring case: v0.4.0's "Arklite-0.4.0-maps.zip" came before
# "Arklite-0.4.0.zip" ('-' sorts before '.'). "Arklite-maps-..." comes after ('m' after the
# version's digit). The script checks that order before uploading, and reads the release back
# afterwards as the installers do: if its first .zip isn't Arklite's, the maps zip is deleted.
#
#   tools/release-maps.sh            check, zip and upload
#   tools/release-maps.sh --dry-run  check and zip into dist/ only
#
# CUSTOMLEVELS=<dir> picks the folder holding the maps (default: the AE install's).
# The check refuses a map that packs a texture of the game (see tools/mapcheck.py).
set -euo pipefail
cd "$(dirname "$0")/.."
DRY=0; [ "${1:-}" = --dry-run ] && DRY=1
AE_DATA="${AE_DATA:-$HOME/Games/oni-x/drive_c/Program Files (x86)/Oni/AE/GameDataFolder}"
CUSTOMLEVELS="${CUSTOMLEVELS:-$AE_DATA/CustomLevels}"
VERSION="$(cat VERSION)"
ARKLITE_ZIP="Arklite-$VERSION.zip"
MAPS_ZIP="Arklite-maps-$VERSION.zip"
lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }

# (byte order, case ignored, as GitHub lists them)
[[ "$(lower "$MAPS_ZIP")" > "$(lower "$ARKLITE_ZIP")" ]] || {
	echo "$MAPS_ZIP would be listed before $ARKLITE_ZIP, and the installers would download it instead of Arklite"
	exit 1
}

mapfile -t MAPS < <(grep -v -e '^#' -e '^[[:space:]]*$' maps.list)
[ ${#MAPS[@]} -gt 0 ] || { echo "maps.list names no maps"; exit 1; }
for m in "${MAPS[@]}"; do
	[ -f "$CUSTOMLEVELS/$m/level50_Final.dat" ] || { echo "$m: no level50_Final.dat in $CUSTOMLEVELS/$m"; exit 1; }
done

python3 -I tools/mapcheck.py "$AE_DATA" "${MAPS[@]/#/$CUSTOMLEVELS/}"

rm -rf dist/maps "dist/$MAPS_ZIP"; mkdir -p dist/maps
list=""
for m in "${MAPS[@]}"; do
	# -X: no extra file attributes; the zip holds <Map>/..., ready to unzip into CustomLevels.
	(cd "$CUSTOMLEVELS" && zip -qrX "$OLDPWD/dist/maps/$m.zip" "$m")
	size="$(du -k "dist/maps/$m.zip" | cut -f1)"
	list+="- **$m** ($((size / 1024)).$(( (size % 1024) * 10 / 1024 )) MB)"$'\n'
	echo "zipped dist/maps/$m.zip"
done
awk -v v="$VERSION" -v maps="$list" '{ gsub(/@VERSION@/, v); if ($0 == "@MAPS@") printf "%s", maps; else print }' MAPS.md > dist/maps/notes.md
(cd dist/maps && zip -qX "../$MAPS_ZIP" ./*.zip notes.md)
echo "made dist/$MAPS_ZIP"

if [ $DRY = 1 ]; then echo "dry run: nothing uploaded"; exit 0; fi

# The latest release, as the installers find it (releases/latest), must hold this version's Arklite
REPO="$(gh repo view --json nameWithOwner --jq .nameWithOwner)"
TAG="$(gh api "repos/$REPO/releases/latest" --jq .tag_name)"
[ -n "$TAG" ] || { echo "no latest release"; exit 1; }
assets="$(gh api "repos/$REPO/releases/latest" --jq '.assets[].name')"
grep -qx "$ARKLITE_ZIP" <<<"$assets" || {
	echo "the latest release ($TAG) has no $ARKLITE_ZIP (its assets: $(tr '\n' ' ' <<<"$assets")): not uploading."
	echo "Wait for the build to attach it, or check VERSION."
	exit 1
}

gh release upload "$TAG" "dist/$MAPS_ZIP" --clobber

# Read back as install-oni.sh and Install-Oni.bat do: the first .../releases/download/...zip
first="$(gh api "repos/$REPO/releases/latest" | grep -o 'https://[^"]*/releases/download/[^"]*\.zip' | head -n 1 || true)"
if [ "${first##*/}" != "$ARKLITE_ZIP" ]; then
	echo "the latest release's first zip is now ${first##*/}, not $ARKLITE_ZIP: deleting $MAPS_ZIP again"
	gh release delete-asset "$TAG" "$MAPS_ZIP" --yes || true
	exit 1
fi
echo "uploaded $MAPS_ZIP (${#MAPS[@]} maps) to the $TAG release; the installers still get $ARKLITE_ZIP"
echo "(the release's title and notes are unchanged)"
