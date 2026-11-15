#!/bin/bash
# Builds Arklite and makes the release zip, dist/Arklite-<version>.zip:
#   Arklite-<version>/ Arklite.dll arklite_loader.dll install.bat uninstall.bat install.sh uninstall.sh
#                      server.bat server.sh (the dedicated server)
#                      README.md TECHNICAL.md VERSION (and LICENSE when there is one)
# The loader is build/realbink.dll, renamed so that extracting the zip into AE/ can't overwrite AE's own
# realbink.dll (Bink); the installers copy it into AE/ as realbink.dll.
# Only Arklite's own files go in: no Oni, AE or Bink files.
#
# ./package.sh test makes a test build's zip instead, dist/Arklite-test-<commit>.zip, from a clean
# commit only (its commit names it, on screen and in its problem reports):
#   Arklite-test-<commit>/ Arklite.dll (built with ARKLITE_TEST) arklite_loader.dll build.txt
#                          install-test.bat install-test.sh uninstall-test.bat uninstall-test.sh README-test.md
set -euo pipefail
cd "$(dirname "$0")"
if [ "${1:-}" = test ]; then
	[ -z "$(git status --porcelain)" ] || { echo "error: a test build comes from a clean commit only: commit (or stash) first" >&2; exit 1; }
	ARKLITE_TEST=1 ./build.sh
	commit="$(git rev-parse --short=7 HEAD)"
	NAME="Arklite-test-$commit"
	rm -rf "dist/$NAME" "dist/$NAME.zip"
	mkdir -p "dist/$NAME"
	cp build/test/Arklite.dll install-test.bat install-test.sh uninstall-test.bat uninstall-test.sh README-test.md "dist/$NAME/"
	cp build/test/realbink.dll "dist/$NAME/arklite_loader.dll"
	echo "$commit $(git show -s --format=%cs HEAD)" > "dist/$NAME/build.txt"
	(cd dist && zip -qr "$NAME.zip" "$NAME")
	rm -rf "dist/$NAME"
	echo "made dist/$NAME.zip"
	exit 0
fi
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
