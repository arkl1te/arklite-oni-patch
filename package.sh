#!/bin/bash
# Builds Arklite and makes the release zip, dist/Arklite-<version>.zip:
#   Arklite-<version>/ Arklite.dll arklite_loader.dll install.bat uninstall.bat install.sh uninstall.sh
#                      server.bat server.sh (the dedicated server)
#                      README.md TECHNICAL.md VERSION (and LICENSE when there is one)
# The loader is build/realbink.dll, renamed so that extracting the zip into AE/ can't overwrite AE's own
# realbink.dll (Bink); the installers copy it into AE/ as realbink.dll.
# Only Arklite's own files go in: no Oni, AE or Bink files.
set -euo pipefail
cd "$(dirname "$0")"
./build.sh
VERSION="$(cat VERSION)"
NAME="Arklite-$VERSION"
rm -rf "dist/$NAME" "dist/$NAME.zip"
mkdir -p "dist/$NAME"
cp build/Arklite.dll install.bat uninstall.bat install.sh uninstall.sh server.bat server.sh README.md TECHNICAL.md VERSION "dist/$NAME/"
cp build/realbink.dll "dist/$NAME/arklite_loader.dll"
[ -f LICENSE ] && cp LICENSE "dist/$NAME/"
(cd dist && zip -qr "$NAME.zip" "$NAME")
rm -rf "dist/$NAME"
echo "made dist/$NAME.zip"
