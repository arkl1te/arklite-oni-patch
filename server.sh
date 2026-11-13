#!/bin/bash
# Starts an Arklite dedicated server under Wine: Oni without its window, and a console for the
# commands (type help there), which opens in your desktop's default terminal.
#
#   ./server.sh [--terminal] <map folder in GameDataFolder/CustomLevels> [port]
#
# Run it from Oni/AE, or set ONI_AE_DIR; set WINEPREFIX as for the game. --terminal makes this
# terminal the console instead of a new one (an admin over SSH). Oni still opens its window
# and OpenGL at startup, so it needs a display: any desktop session, or xvfb-run on a machine
# without one. The server name, max players and limits are the Host dialog's last ones
# (AE/Arklite.ini [host]).
set -euo pipefail
if [ "${1:-}" = "--terminal" ]; then export ARKLITE_SERVER_STDIO=1; shift; fi
AE="${ONI_AE_DIR:-$(cd "$(dirname "$0")" && pwd)}"
[ -f "$AE/Oni.exe" ] || { echo "error: no Oni.exe in $AE: run it from Oni/AE or set ONI_AE_DIR" >&2; exit 1; }
[ $# -ge 1 ] || { echo "usage: $0 [--terminal] MapFolder [port]" >&2; exit 1; }
export ARKLITE_DEDICATED="$1${2:+:$2}"
cd "$AE"
if [ "${ARKLITE_SERVER_STDIO:-}" = 1 ]; then
	# Wine gives a windowed program no terminal, but it does take pipes: the terminal still edits
	# the line, and Wine's own messages stay out of it. After quit, press Enter once more.
	cat | WINEDEBUG="${WINEDEBUG:--all}" wine Oni.exe -noswitch 2>/dev/null | cat
else
	exec wine Oni.exe -noswitch
fi
