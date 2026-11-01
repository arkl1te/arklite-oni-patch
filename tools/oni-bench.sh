#!/bin/bash
AE="/your/oni/path/AE"                                # Oni's AE folder
export WINEPREFIX="/your/wine/prefix/path"            # the Wine prefix Oni runs in
UITEST="/your/uitest/tools/path"                      # holds eitool (keeps a libei pointer device open) and xwin.py (lists X windows)
UI="${ONI_BENCH_DIR:-/your/bench/scratch/path}"       # this script's own files: backups, logs

# oni-bench.sh <CustomLevels folder> [seconds] [save point]: plays a level hands off in a private
# nested KWin, so nothing appears on the desktop, and prints the frame times Arklite logged
# (ARKLITE_BENCH, src/bench.c). Refuses to run next to a running Oni. A timeout ends a hung game.
# Options the game saves are restored afterwards, however it ends. Run it in the background: a
# level takes ~1 min. ONI_SHOTS=<dir>: also saves a screenshot of the game every 3 seconds there.
set -u
case "$AE:$WINEPREFIX:$UITEST:$UI" in */your/*) echo "set the paths at the top of $0"; exit 2;; esac
FOLDER=$1; SECONDS_RUN=${2:-20}; SAVEPOINT=${3:-}
SOCK=oni-bench-$$
mkdir -p "$UI"
for _ in 1 2 3 4 5 6 7 8 9 10 11 12; do pgrep -x wineserver >/dev/null || break; sleep 1; done   # lingers after a game
if pgrep -x Oni.exe >/dev/null || pgrep -x wineserver >/dev/null; then echo "wine is already running: not starting"; exit 2; fi
[ -d "$AE/GameDataFolder/CustomLevels/$FOLDER" ] || { echo "no level $FOLDER"; exit 2; }
for f in persist.dat Arklite.ini daodan.ini; do cp -p "$AE/$f" "$UI/$f.bak"; done
LOG=Arklite-bench-$$.log
KWIN=; EI=; SHOTS=
cleanup() {
	for f in persist.dat Arklite.ini daodan.ini; do cp -p "$UI/$f.bak" "$AE/$f"; done
	rm -f "$AE/logs/${LOG%.log}_"*.log
	[ -n "$SHOTS" ] && kill $SHOTS 2>/dev/null
	[ -n "$EI" ] && { pkill -P $EI 2>/dev/null; kill $EI 2>/dev/null; }
	[ -n "$KWIN" ] && kill $KWIN 2>/dev/null
}
trap cleanup EXIT
trap 'exit 1' INT TERM
cat > "$UI/wrap.sh" <<W
#!/bin/bash
echo "\$DBUS_SESSION_BUS_ADDRESS" > "$UI/bus.txt"
export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json __GLX_VENDOR_LIBRARY_NAME=nvidia GBM_BACKEND=nvidia-drm
exec -a $SOCK /usr/bin/kwin_wayland --virtual --xwayland --no-lockscreen --socket $SOCK --width 1920 --height 1200
W
chmod +x "$UI/wrap.sh"; rm -f "$UI/bus.txt"
before=$(ls /tmp/.X11-unix)
env -u DISPLAY -u DBUS_SESSION_BUS_ADDRESS WAYLAND_DISPLAY= KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 \
  setsid dbus-run-session -- "$UI/wrap.sh" > "$UI/kwin.log" 2>&1 < /dev/null &
for _ in $(seq 1 40); do [ -s "$UI/bus.txt" ] && break; sleep 0.25; done; sleep 3
KWIN=$(pgrep -f "^$SOCK")
X=$(comm -13 <(echo "$before" | sort) <(ls /tmp/.X11-unix | sort) | head -1); DISP=:${X#X}
[ "$DISP" != ":" ] && [ "$DISP" != ":0" ] || { echo "no private display"; kill $KWIN; exit 2; }
# Keeps a pointer device: Xwayland crashes when Oni locks the pointer without one.
(sleep infinity | DBUS_SESSION_BUS_ADDRESS=$(cat "$UI/bus.txt") "$UITEST/eitool" >/dev/null 2>&1) &
EI=$!
if [ -n "${ONI_SHOTS:-}" ]; then
	mkdir -p "$ONI_SHOTS"
	(n=0; while sleep 3; do
		id=$(DISPLAY=$DISP python3 "$UITEST/xwin.py" 2>/dev/null | grep -i oni | head -1 | cut -d' ' -f1)
		n=$((n + 1))
		[ -n "$id" ] && DISPLAY=$DISP import -window "$id" "$ONI_SHOTS/$(printf %03d $n).png" 2>/dev/null
	done) &
	SHOTS=$!
fi
cd "$AE"
env DISPLAY=$DISP WAYLAND_DISPLAY= DBUS_SESSION_BUS_ADDRESS=$(cat "$UI/bus.txt") ARKLITE_AUTOSTART="$FOLDER" ${SAVEPOINT:+ARKLITE_SAVEPOINT=$SAVEPOINT} \
  ARKLITE_BENCH=$SECONDS_RUN ARKLITE_LOG=$LOG timeout -k 5 $((SECONDS_RUN + 45)) /usr/bin/wine Oni.exe -noswitch > "$UI/game.log" 2>&1
code=$?
grep -h "bench:" "$AE/logs/${LOG%.log}_"*.log 2>/dev/null || { echo "no result (exit $code)"; tail -5 "$UI/game.log"; }
exit 0
