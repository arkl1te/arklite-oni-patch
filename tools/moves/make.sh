#!/bin/bash
# Regenerates moves.md and src/moves_table.h (the /moves list) from the game's animation data.
#
#   tools/moves/make.sh <OniFoxed checkout> <XML folder> [<package XML folder> <class=Label,...>]
#
# An XML folder is OniSplit's XML of a level0's characters and animation collections:
#   mono AE/Tools/OniSplit.exe -extract:xml <folder> -type:ONCC AE/GameDataFolder/level0_Final.dat
#   mono AE/Tools/OniSplit.exe -extract:xml <folder> -type:TRAC AE/GameDataFolder/level0_Final.dat
# The first folder's level0 gives the F2 menu's characters: AE's core level0 with its XML patches
# and no packages (Arklite's hashes f40ec301 6091538b; a joiner keeps it in AE/cache/f40ec301-6091538b/).
# The package folder's level0 gives a package's sets for the classes listed, each a section after the
# character it changes. moves.md as it is (checked 2026-11-16: the same bytes): the package folder
# from an AE install with Konoko's New Combat Moves, konoko_generic="Konoko: New Combat Moves".
# OniFoxed (https://github.com/hogsy/OniFoxed) gives the move types' and states' numbers.
# Nothing from the game is kept: the folders stay where they are, the JSON goes to build/moves/.
set -euo pipefail
if [ $# -ne 2 ] && [ $# -ne 4 ]; then sed -n '2,/^set /p' "$0" | sed '$d'; exit 1; fi
HEADER="$(realpath "$1")/OniProj/OniGameSource/Oni_Character_Animation.h"
[ -f "$HEADER" ] || { echo "no $HEADER"; exit 1; }
set -- "$1" "$(realpath "$2")" "${@:3}"
[ $# -eq 4 ] && set -- "$1" "$2" "$(realpath "$3")" "$4"
cd "$(dirname "$0")/../.."
B=build/moves
mkdir -p $B
python3 -I tools/moves/read_moves.py "$2" > $B/moves.json
if [ $# -eq 4 ]; then
	python3 -I tools/moves/read_moves.py "$3" "$4" > $B/package.json
	python3 -I tools/moves/gen_md.py $B/moves.json $B/package.json > $B/moves.md
	python3 -I tools/moves/gen_table.py "$HEADER" "$2" $B/moves.json "$3" $B/package.json > $B/moves_table.h
else
	python3 -I tools/moves/gen_md.py $B/moves.json > $B/moves.md
	python3 -I tools/moves/gen_table.py "$HEADER" "$2" $B/moves.json > $B/moves_table.h
fi
mv $B/moves.md moves.md   # (only now: a failed step leaves both as they were)
mv $B/moves_table.h src/moves_table.h
echo "made moves.md and src/moves_table.h: build Arklite (./build.sh)"
