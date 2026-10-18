#!/bin/bash
# Builds Arklite and makes the release zip, dist/Arklite-<version>.zip:
#   Arklite-<version>/ Arklite.dll realbink.dll install.bat uninstall.bat install.sh uninstall.sh
#                      README.md VERSION (and LICENSE when there is one)
# Only Arklite's own files go in: no Oni, AE or Bink files.
set -euo pipefail
cd "$(dirname "$0")"
./build.sh
VERSION="$(cat VERSION)"
NAME="Arklite-$VERSION"
rm -rf "dist/$NAME" "dist/$NAME.zip"
mkdir -p "dist/$NAME"
cp build/Arklite.dll build/realbink.dll install.bat uninstall.bat install.sh uninstall.sh README.md VERSION "dist/$NAME/"
[ -f LICENSE ] && cp LICENSE "dist/$NAME/"
(cd dist && zip -qr "$NAME.zip" "$NAME")
rm -rf "dist/$NAME"
echo "made dist/$NAME.zip"
