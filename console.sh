#!/bin/sh
# Arklite dedicated server: the console in a Linux terminal, for a server running under Wine.
#
# Arklite writes this file to Oni/AE as Arklite-console.sh when a dedicated server starts under
# Wine, and runs it (start /unix /bin/sh): it opens your default terminal with the console in it.
# Run it again to open another one while the server runs:
#
#   sh Arklite-console.sh          a new terminal window
#   sh Arklite-console.sh --here   this terminal
#
# The server listens on 127.0.0.1 only (TCP, the game's port number); Arklite-console.key next to
# this file has the port and the key it asks for. Closing the last console closes the server, as
# closing Wine's console window does.
dir=$(cd "$(dirname "$0")" && pwd)
self="$dir/$(basename "$0")"
keyfile="$dir/Arklite-console.key"
[ -r "$keyfile" ] || { echo "No $keyfile: is a dedicated server running from this folder?" >&2; exit 1; }
chmod 600 "$keyfile" 2>/dev/null

if [ "${1:-}" != "--here" ]; then
	# Under Wine: none of Wine's own settings for the programs a terminal starts
	unset WINEDEBUG WINEDLLOVERRIDES WINELOADERNOEXEC WINESERVERSOCKET 2>/dev/null
	found() { command -v "$1" >/dev/null 2>&1; }
	open_in() {   # terminal, then the command to run in it (each terminal says that differently)
		t=$1; shift
		case "${t##*/}" in
			xdg-terminal-exec|kitty|foot|footclient) exec "$t" "$@" ;;
			gnome-terminal|kgx|ptyxis|tilix) exec "$t" -- "$@" ;;
			xfce4-terminal|terminator|mate-terminal) exec "$t" -x "$@" ;;
			wezterm) exec "$t" start -- "$@" ;;
			konsole) exec "$t" -p tabtitle="Arklite server" -e "$@" ;;   # (it ignores the title the console sets)
			*) exec "$t" -e "$@" ;;   # alacritty, xterm, x-terminal-emulator, urxvt, st, ...
		esac
	}
	try() { found "$1" && open_in "$1" /bin/sh "$self" --here; }
	# 1. the freedesktop default terminal, 2. $TERMINAL, 3. the desktop's own setting or terminal
	try xdg-terminal-exec
	[ -n "${TERMINAL:-}" ] && try "${TERMINAL%% *}"
	case "${XDG_CURRENT_DESKTOP:-}" in
		*KDE*)
			t=$( (kreadconfig6 --group General --key TerminalApplication || kreadconfig5 --group General --key TerminalApplication) 2>/dev/null)
			[ -n "$t" ] && try "${t%% *}"
			try konsole ;;
		*GNOME*|*Unity*|*Pantheon*) try ptyxis; try kgx; try gnome-terminal ;;
		*XFCE*) try xfce4-terminal ;;
		*MATE*) try mate-terminal ;;
		*LXQt*) try qterminal ;;
	esac
	for t in x-terminal-emulator konsole gnome-terminal ptyxis kgx xfce4-terminal mate-terminal alacritty kitty foot wezterm xterm; do
		try "$t"
	done
	echo "No terminal found: run 'sh $self --here' in one." >&2
	exit 1
fi

read -r port key < "$keyfile"
if command -v python3 >/dev/null 2>&1; then
	exec python3 -c '
import os, socket, sys, threading
try:
    import readline
except ImportError:
    readline = None
port, key = int(sys.argv[1]), sys.argv[2]
try:
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
except OSError as e:
    print("Can'"'"'t reach the server on port %d: %s" % (port, e))
    sys.exit(1)
s.settimeout(None)
s.sendall(("KEY %s\n" % key).encode())
PROMPT = "> "
lock = threading.Lock()

def show(line):
    """A server line above the one being typed, which is drawn again under it"""
    with lock:
        typed = readline.get_line_buffer() if readline else ""
        sys.stdout.write("\r\x1b[K" + line + "\n" + PROMPT + typed)
        sys.stdout.flush()

def reader():
    for raw in s.makefile("r", encoding="utf-8", errors="replace", newline="\n"):
        line = raw.rstrip("\r\n")
        if line.startswith("\x01title "):
            with lock:
                sys.stdout.write("\x1b]0;%s\x07" % line[7:])
                sys.stdout.flush()
        else:
            show(line)
    with lock:
        sys.stdout.write("\r\x1b[KThe server closed.\n")
        sys.stdout.flush()
    threading.Event().wait(3)
    os._exit(0)

threading.Thread(target=reader, daemon=True).start()
while True:
    try:
        line = input(PROMPT)
    except (EOFError, KeyboardInterrupt):
        break
    try:
        s.sendall((line + "\n").encode("utf-8", "replace"))
    except OSError:
        break
s.close()
' "$port" "$key"
fi
if command -v bash >/dev/null 2>&1; then   # no Python: bash's own TCP, without redrawing the typed line
	exec bash -c 'exec 3<>"/dev/tcp/127.0.0.1/$1" || exit 1
		printf "KEY %s\n" "$2" >&3
		tr -d "\r\001" <&3 &
		while IFS= read -r -e line; do printf "%s\n" "$line" >&3 || break; done' _ "$port" "$key"
fi
echo "This console needs python3 or bash." >&2
sleep 5
exit 1
