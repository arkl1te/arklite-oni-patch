/* Dedicated server: Oni runs the game for the players without playing or showing it, and a
 * console takes the admin's commands (net.c "Dedicated server" does the network side).
 *
 * Started by the Host dialog's Dedicate (mp_menu.c) or ARKLITE_DEDICATED=<map folder>[:port]
 * (net.c, custom_game.c's autostart). From then until Oni closes:
 *
 *   - The console: a window of its own (AllocConsole), titled with the server's name. A thread
 *     reads its keys (ReadConsoleInputW) and edits the typed line itself, on the console's last row
 *     ("> ", Backspace, Esc clears, Up/Down for earlier lines): the console's own line input
 *     redrew it over whatever was printed meanwhile. Finished lines go into a small queue that
 *     the game's thread runs once a frame. Everything the chat shows on this PC (players'
 *     messages, whispers to the server, "X entered the game", the server list's news) is printed
 *     there with the time, and so are the matches' starts and ends. Names and chat beyond ASCII
 *     (text.c) go out as UTF-16 to a console of Windows' and as UTF-8 everywhere else; typed and
 *     piped lines come in the same way. Closing the console window,
 *     Ctrl+C or Ctrl+Break is the same as "quit". ARKLITE_SERVER_STDIO=1 reads the process's
 *     standard input and writes its standard output instead (a terminal, a pipe or a file: an
 *     admin over SSH, a service, tests); there the terminal does the line editing.
 *   - No drawing: the main loop's whole draw block (0x4d4783..0x4d4802: render state, frame start
 *     0x41ab00, the game display 0x4fb480, the UI 0x476140, frame end/swap 0x41ad00) is jumped
 *     over; the frame then sleeps 1 ms (timeBeginPeriod 1) instead of waiting for the swap, so the
 *     game still runs its ticks when they're due and reads the network every millisecond.
 *     Drawing isn't game state (each PC draws its own view; see net.c), so the joiners stay in sync.
 *   - The game window is hidden (ShowWindow on ONgPlatformData.gameWindow, 0x63100c): nothing to
 *     see, and no keys or clicks reach the game. live_menu.c opens no dialog on a server.
 *   - Playing on the server's PC: Daodan refuses to start when a window of class and title "ONI "
 *     exists (FindWindowA in DD_ONrPlatform_Initialize, hidden windows included), so the server
 *     retitles its window and no longer counts; the player's own Oni then starts as usual and joins
 *     at 127.0.0.1 or from the LAN list. A server started with ARKLITE_DEDICATED skips that check
 *     itself (net_install), so it also starts while a game is open.
 *   - The console, under Wine (Linux): a terminal of the desktop's, not Wine's console window.
 *     The server listens on 127.0.0.1, TCP, the game's port number, writes Arklite-console.key
 *     ("<port> <key>", a random key a console must send first) and Arklite-console.sh (console.sh,
 *     inside the DLL: build.sh) to Oni/AE, and runs the script at its first frame with
 *     "start.exe /unix /bin/sh <its Unix path>" (Wine runs a Unix program that way; the path from
 *     kernel32's wine_get_unix_file_name). The script opens the default terminal (xdg-terminal-exec,
 *     $TERMINAL, KDE's TerminalApplication, the desktop's own, a list) with a Python console in it
 *     (bash's /dev/tcp without Python). Lines go both ways as text; "\x01title <name>" sets the
 *     window's title. A console that connects gets the last BACKLOG lines first. Closing the last
 *     console closes the server, as Wine's console window does. No console after TERM_WAIT_MS:
 *     Wine's console window after all (it stays listening: sh Arklite-console.sh still connects).
 *     ARKLITE_SERVER_CONSOLE=wine: Wine's console window from the start. Windows: its console.
 *   - No sound: the master volume goes to 0 (SSrSetMasterVolume, which SSrUpdate fades in 20
 *     frames), then the driver flag ONgSoundOn (0x5eb757) is cleared every frame, as catching up
 *     and rollback's re-runs do (which sounds play is never game state).
 *
 * Oni still makes its window and OpenGL context at startup, so it needs a display: on Linux under
 * Wine, any X or Wayland session, a nested one, or Xvfb. Run it windowed (Oni.exe -noswitch) so it
 * doesn't change the screen's resolution first. */
#include "arklite.h"

#define STD_INPUT_HANDLE   ((u32)-10)
#define STD_OUTPUT_HANDLE  ((u32)-11)
#define FILE_TYPE_DISK     1
#define FILE_TYPE_CHAR     2
#define FILE_TYPE_PIPE     3
#define SW_HIDE            0
#define CTRL_CLOSE_EVENT   2

typedef struct { u16 year, month, weekday, day, hour, minute, second, ms; } SYSTEMTIME;
__declspec(dllimport) int    WINAPI AllocConsole(void);
__declspec(dllimport) HANDLE WINAPI GetStdHandle(u32);
__declspec(dllimport) int    WINAPI ReadFile(HANDLE, void *, u32, u32 *, void *);
__declspec(dllimport) int    WINAPI ReadConsoleInputW(HANDLE, void *, u32, u32 *);
__declspec(dllimport) int    WINAPI WriteConsoleW(HANDLE, const void *, u32, u32 *, void *);
__declspec(dllimport) int    WINAPI SetConsoleTitleA(const char *);
__declspec(dllimport) int    WINAPI SetConsoleCtrlHandler(int (WINAPI *)(u32), int);
__declspec(dllimport) int    WINAPI SetConsoleMode(HANDLE, u32);
__declspec(dllimport) int    WINAPI GetConsoleScreenBufferInfo(HANDLE, void *);
__declspec(dllimport) u32    WINAPI GetFileType(HANDLE);
__declspec(dllimport) HANDLE WINAPI CreateThread(void *, u32, u32 (WINAPI *)(void *), void *, u32, u32 *);
__declspec(dllimport) void   WINAPI Sleep(u32);
__declspec(dllimport) void   WINAPI GetLocalTime(SYSTEMTIME *);
__declspec(dllimport) void * WINAPI LoadLibraryA(const char *);
__declspec(dllimport) void * WINAPI GetProcAddress(void *, const char *);
__declspec(dllimport) int    WINAPI ShowWindow(void *, int);
__declspec(dllimport) int    WINAPI SetWindowTextA(void *, const char *);
__declspec(dllimport) u32    WINAPI GetTickCount(void);
__declspec(dllimport) void   WINAPI InitializeCriticalSection(void *);
__declspec(dllimport) void   WINAPI EnterCriticalSection(void *);
__declspec(dllimport) void   WINAPI LeaveCriticalSection(void *);
__declspec(dllimport) u32    WINAPI GetCurrentDirectoryA(u32, char *);
__declspec(dllimport) int    WINAPI CreateProcessA(const char *, char *, void *, void *, int, u32, void *, const char *, void *, void *);
__declspec(dllimport) int    WINAPI MultiByteToWideChar(u32, u32, const char *, int, u16 *, int);
__declspec(dllimport) int    WINAPI CloseHandle(HANDLE);
__declspec(dllimport) u32    WINAPI GetCurrentProcessId(void);
__declspec(dllimport) int    WINAPI QueryPerformanceCounter(void *);
typedef struct { u16 family, port; u32 addr; u8 zero[8]; } SOCKADDR_IN;
__declspec(dllimport) int    WINAPI WSAStartup(u16, void *);
__declspec(dllimport) int    WINAPI WSAGetLastError(void);
__declspec(dllimport) u32    WINAPI socket(int, int, int);
__declspec(dllimport) int    WINAPI bind(u32, const void *, int);
__declspec(dllimport) int    WINAPI listen(u32, int);
__declspec(dllimport) u32    WINAPI accept(u32, void *, int *);
__declspec(dllimport) int    WINAPI recv(u32, void *, int, int);
__declspec(dllimport) int    WINAPI send(u32, const void *, int, int);
__declspec(dllimport) int    WINAPI ioctlsocket(u32, int, u32 *);
__declspec(dllimport) int    WINAPI closesocket(u32);
__declspec(dllimport) u32    WINAPI inet_addr(const char *);
__declspec(dllimport) u16    WINAPI htons(u16);
void *memcpy(void *, const void *, unsigned);
void *memset(void *, int, unsigned);

#define ONgGameWindow           (*(void **)0x63100c)        /* ONgPlatformData.gameWindow */
#define ONgSoundOn              (*(u8 *)0x5eb757)
#define SSrSetMasterVolume      ((float (__stdcall *)(float))0x483e00)
#define SSgMasterVolumeNow      (*(float *)0x5eb744)
#define ONcMainLoop_DrawBlock   0x4d4783   /* mov al,[0x55600b]: the first of the frame's drawing */
#define ONcMainLoop_DrawNext    0x4d4788   /* after that instruction */
#define ONcMainLoop_DrawEnd     0x4d4802   /* call 0x426480 after the frame end */

#define LINE_MAX   200
#define QUEUE      16
#define HISTORY    16
#define KEY_EVENT  1
#define VK_BACK    0x08
#define VK_RETURN  0x0d
#define VK_ESCAPE  0x1b
#define VK_UP      0x26
#define VK_DOWN    0x28
#define ENABLE_PROCESSED_INPUT 1   /* Ctrl+C stays a signal; no line input, no echo: the reader edits */

/* ReadConsoleInputW's record, a key's part of it */
typedef struct {
	u16 type, pad;
	int down;
	u16 repeat, vk, scan;
	u16 ch;
	u32 control;
} KeyRecord;
typedef struct { short size_x, size_y, cur_x, cur_y; u16 attr; short left, top, right, bottom, max_x, max_y; } ScreenInfo;

static int g_running;          /* a dedicated server runs (server_start) */
static int g_installed;        /* the draw block patch is in */
static int g_stdio;            /* standard input/output, not a console of our own */
static HANDLE g_in, g_out;
static int g_frames;           /* server frames so far (the first hides the window) */
static volatile int g_quit;    /* the console was closed, or Ctrl+C */

/* The reader thread's lines, for the game's thread (one writer, one reader) */
static char g_lines[QUEUE][LINE_MAX];
static volatile u32 g_head, g_tail;

/* The line being typed: always the console's last row, under everything printed. The console's
 * own line editing redrew it over output printed meanwhile (Wine's console), so the reader reads
 * keys and edits it here; every write takes the lock, wipes the row, writes, and draws it again. */
static u32 g_lock[8];          /* CRITICAL_SECTION (24 bytes) */
static char g_edit[LINE_MAX];
static u32 g_edit_len;
static char g_hist[HISTORY][LINE_MAX];
static u32 g_nhist, g_hist_at;

/* The Linux terminal console (see the top): sockets, the key, the lines a new one gets first */
#define TERM_MAX      4
#define TERM_WAIT_MS  15000
#define BACKLOG       100
#define NO_SOCKET     0xffffffffu
#define FIONBIO       0x8004667e
#define WSAEWOULDBLOCK 10035
static int g_term;             /* a Linux terminal is (to be) the console */
static int g_term_had;         /* one has come */
static u32 g_term_launched;    /* when Arklite-console.sh was started (0: not yet) */
static u32 g_listen = NO_SOCKET;
static char g_key[33];
static struct { u32 sock; int authed; u32 len; char buf[LINE_MAX]; } g_tc[TERM_MAX];
static char g_back[BACKLOG][400];
static u32 g_nback;

int server_running(void) { return g_running; }

/* ---- Output ----------------------------------------------------------------------------------- */

/* Arklite's text (text.c): UTF-8 to a file, pipe or terminal, UTF-16 to a console of Windows' */
static void out_raw(const char *s, u32 n)
{
	u32 done, k;
	if (!g_out || g_out == INVALID_HANDLE_VALUE) return;
	if (g_stdio) {
		char u[1600];
		k = (u32)text_to_utf8_n(u, sizeof u, s, (int)n);
		WriteFile(g_out, u, k, &done, 0);
	} else {
		u16 w[1300];
		k = (u32)text_to_utf16_n(w, 1300, s, (int)n);
		WriteConsoleW(g_out, w, k, &done, 0);
	}
}

static u32 width(void)
{
	ScreenInfo i;
	if (g_stdio || !GetConsoleScreenBufferInfo(g_out, &i) || i.size_x < 20) return 80;
	return (u32)i.size_x > LINE_MAX ? LINE_MAX : (u32)i.size_x;
}

/* The typed line's row: blank (wipe) or "> " and as much of the line's end as fits */
static void edit_row(int wipe)
{
	char row[LINE_MAX + 8];
	u32 w = width() - 1, n = 0, from, i;   /* one short of the width: the console mustn't wrap */
	if (g_stdio) return;
	row[n++] = '\r';
	for (i = 0; i < w; i++) row[n++] = ' ';
	row[n++] = '\r';
	if (!wipe) {
		row[n++] = '>';
		row[n++] = ' ';
		from = g_edit_len > w - 3 ? g_edit_len - (w - 3) : 0;
		g_edit[g_edit_len] = 0;
		for (i = 0; i < from; i = (u32)text_next(g_edit, (int)i)) {}   /* not inside a two-byte character */
		for (from = i; i < g_edit_len; i++) row[n++] = g_edit[i];
	}
	out_raw(row, n);
}

static void term_drop(int i);
static void term_title(const char *name);
static void wine_console(void);

/* To every Linux console that's in (as UTF-8), and kept for the ones that come later (under g_lock) */
static void term_line(const char *text, u32 len)
{
	char line[1600];
	u32 n;
	if (!g_term) return;
	n = (u32)text_to_utf8_n(line, sizeof line, text, (int)len);
	u32 k = n < sizeof g_back[0] - 1 ? n : sizeof g_back[0] - 3;
	memcpy(g_back[g_nback % BACKLOG], line, k);
	if (k < n) memcpy(g_back[g_nback % BACKLOG] + k, "\r\n", 2), k += 2;
	g_back[g_nback++ % BACKLOG][k] = 0;
	for (int i = 0; i < TERM_MAX; i++)
		if (g_tc[i].sock != NO_SOCKET && g_tc[i].authed && send(g_tc[i].sock, line, (int)n, 0) < 0 &&
		    WSAGetLastError() != WSAEWOULDBLOCK)
			term_drop(i);
}

/* One line (several, split at \n), each with the time in front */
static void out_line(const char *text)
{
	char line[1200];
	SYSTEMTIME t;
	const char *s = text;

	if (!g_running) return;
	GetLocalTime(&t);
	EnterCriticalSection(g_lock);
	edit_row(1);
	while (*s || s == text) {
		const char *e = s;
		u32 n;
		while (*e && *e != '\n') e++;
		n = (u32)(e - s) < 1000 ? (u32)(e - s) : 1000;
		fmt(line, "[%02d:%02d:%02d] ", t.hour, t.minute, t.second);
		memcpy(line + 11, s, n);
		line[11 + n] = '\r';
		line[12 + n] = '\n';
		out_raw(line, 13 + n);
		term_line(line, 13 + n);
		if (!*e) break;
		s = e + 1;
	}
	edit_row(0);
	LeaveCriticalSection(g_lock);
}

void server_print(const char *f, ...)
{
	char text[1024];
	va_list ap;
	va_start(ap, f);
	wvsprintfA(text, f, ap);
	va_end(ap);
	log_msg("server: %s", text);
	out_line(text);
}

void server_chat_line(const char *line)
{
	out_line(line);
}

/* Just to the console (answers to commands: the log has the command) */
static void say_back(const char *text)
{
	if (text) out_line(text);
}

/* ---- Input ------------------------------------------------------------------------------------ */

static void push_line(const char *s, u32 n);

/* A line from a pipe or a Linux console: UTF-8 (or Windows-1252) to Arklite's text */
static void push_utf8(char *s, u32 n)
{
	char t[LINE_MAX];
	s[n] = 0;
	push_line(t, (u32)text_from_utf8(t, sizeof t, s));
}

static void push_line(const char *s, u32 n)
{
	if (g_head - g_tail >= QUEUE) return;   /* typed faster than frames run: dropped */
	if (n > LINE_MAX - 1) n = LINE_MAX - 1;
	memcpy(g_lines[g_head % QUEUE], s, n);
	g_lines[g_head % QUEUE][n] = 0;
	__sync_synchronize();
	g_head++;
}

/* A file or pipe: lines as they come */
static u32 WINAPI read_stdio(void *unused)
{
	char buf[256], line[LINE_MAX];
	u32 n, len = 0;

	for (;;) {
		if (!ReadFile(g_in, buf, sizeof buf, &n, 0) || !n) {   /* the end: nothing more will come */
			if (len) push_utf8(line, len);
			return 0;
		}
		for (u32 i = 0; i < n; i++) {
			char c = buf[i];
			if (c == '\r') continue;
			if (c == '\n') {
				push_utf8(line, len);
				len = 0;
			} else if (len < LINE_MAX - 1 && (u8)c >= 0x20) {
				line[len++] = c;
			}
		}
	}
}

static void recall(u32 at)
{
	g_hist_at = at;
	if (at >= g_nhist) g_edit_len = 0;
	else str_cpy(g_edit, sizeof g_edit, g_hist[(g_nhist - 1 - at) % HISTORY]), g_edit_len = str_len(g_edit);
}

/* The console's keys: typing, Backspace, Esc (clears), Up/Down (earlier lines), Enter */
static u32 WINAPI read_console(void *unused)
{
	KeyRecord r[16];
	u32 n;

	for (;;) {
		if (!ReadConsoleInputW(g_in, r, 16, &n) || !n) {
			Sleep(50);
			continue;
		}
		EnterCriticalSection(g_lock);
		for (u32 i = 0; i < n; i++) {
			if (r[i].type != KEY_EVENT || !r[i].down) continue;
			for (u32 rep = r[i].repeat ? r[i].repeat : 1; rep; rep--) {
				u32 c = r[i].ch;
				char b[2];
				int k;
				if (r[i].vk == VK_RETURN) {
					edit_row(1);
					out_raw("> ", 2);
					out_raw(g_edit, g_edit_len);
					out_raw("\r\n", 2);
					if (g_edit_len) {
						g_edit[g_edit_len] = 0;
						if (!g_nhist || str_len(g_hist[(g_nhist - 1) % HISTORY]) != g_edit_len ||
						    !str_ieq_n(g_hist[(g_nhist - 1) % HISTORY], g_edit, g_edit_len))
							str_cpy(g_hist[g_nhist++ % HISTORY], LINE_MAX, g_edit);
						push_line(g_edit, g_edit_len);
					}
					g_edit_len = 0;
					g_hist_at = (u32)-1;
				} else if (r[i].vk == VK_BACK) {
					g_edit[g_edit_len] = 0;
					if (g_edit_len) g_edit_len = (u32)text_prev(g_edit, (int)g_edit_len);
				} else if (r[i].vk == VK_ESCAPE) {
					g_edit_len = 0;
					g_hist_at = (u32)-1;
				} else if (r[i].vk == VK_UP) {
					u32 next = g_hist_at + 1;   /* (u32)-1 + 1: the newest */
					if (next < g_nhist && next < HISTORY) recall(next);
				} else if (r[i].vk == VK_DOWN) {
					if (g_hist_at != (u32)-1) recall(g_hist_at ? g_hist_at - 1 : (u32)-1);
					if (g_hist_at == (u32)-1) g_edit_len = 0;
				} else if (c >= 0x20 && c != 0x7f && (k = text_put(b, c)) && g_edit_len + (u32)k < LINE_MAX) {
					memcpy(g_edit + g_edit_len, b, (u32)k);
					g_edit_len += (u32)k;
				}
			}
		}
		edit_row(0);
		LeaveCriticalSection(g_lock);
	}
}

static int WINAPI on_console_event(u32 event)
{
	g_quit = 1;   /* the game's thread closes the server at its next frame */
	if (event == CTRL_CLOSE_EVENT) Sleep(4000);   /* Windows ends the process ~5 s after this returns: time for the goodbyes */
	return 1;
}

/* ---- Commands --------------------------------------------------------------------------------- */

static int ieq(const char *a, const char *b)
{
	u32 n = str_len(b);
	return str_len(a) == n && str_ieq_n(a, b, n);
}

/* A number at s; 0 if there's none */
static int number(const char *s, int *out)
{
	int v = 0, neg = *s == '-', digits = 0;
	if (neg) s++;
	for (; *s >= '0' && *s <= '9'; s++, digits++) v = v * 10 + (*s - '0');
	*out = neg ? -v : v;
	return digits > 0 && !*skip_space(s);
}

static void help(void)
{
	say_back("Commands (anything else you type is said to every player as the server):\n"
	         "  say <text>            a server message to every player\n"
	         "  players               who's here: number, name, state, score, ping, address\n"
	         "  status                map, warm-up or match, players, limits\n"
	         "  kick <player> [why]   out for " "5" " minutes (player: its number from 'players', or its name)\n"
	         "  ban <player> [why]    out until the server closes\n"
	         "  bans                  who is kicked or banned, numbered\n"
	         "  unban [who]           lets them back: number from 'bans', name, address or all\n"
	         "                        (nothing needed when only one is out)\n"
	         "  start                 the match now (from the warm-up)\n"
	         "  warmup                back to a warm-up now (ends the match)\n"
	         "  timelimit <minutes>   0: none; from the next match\n"
	         "  scorelimit <points>   0: none, below 0: reached by suicides; from the next match\n"
	         "  maxplayers <n>        2 to 4\n"
	         "  name <server name>    as the server list shows it\n"
	         "  quit                  says goodbye to the players and closes Oni");
}

static void players_cmd(void)
{
	NetPlayerRow rows[NET_SLOTS];
	int n = net_server_players(rows, NET_SLOTS);
	char line[160];

	if (!n) {
		say_back("No players.");
		return;
	}
	for (int i = 0; i < n; i++) {
		const NetPlayerRow *r = &rows[i];
		char ping[16];
		if (r->ping >= 0) fmt(ping, "%d ms", r->ping);
		else str_cpy(ping, sizeof ping, "-");
		fmt(line, "  %d  %-24s %-9s score %-4d ping %-7s %s", r->slot + 1, r->name,
		    r->gone ? "left" : r->joining ? "joining" : r->watching ? "watching" : "playing", r->score, ping, r->addr);
		say_back(line);
	}
}

/* kick/ban: "<player> [reason]": the longest name the text starts with, else the first word */
static void remove_cmd(const char *args, u32 minutes)
{
	NetPlayerRow rows[NET_SLOTS];
	int n = net_server_players(rows, NET_SLOTS), slot = -1, best = 0;
	const char *reason = 0;
	char word[32];
	u32 k = 0;

	if (!*args) {
		say_back(minutes ? "Usage: kick <player> [reason]" : "Usage: ban <player> [reason]");
		return;
	}
	for (int i = 0; i < n; i++) {
		u32 len = str_len(rows[i].name);
		if ((int)len > best && str_ieq_n(args, rows[i].name, len) && (args[len] == ' ' || !args[len]))
			best = (int)len, slot = rows[i].slot, reason = skip_space(args + len);
	}
	if (slot < 0) {
		while (args[k] && args[k] != ' ' && k < sizeof word - 1) word[k] = args[k], k++;
		word[k] = 0;
		slot = net_server_find(word);
		reason = skip_space(args + k);
	}
	if (slot == -2) {
		say_back("More than one player's name starts with that: give more of it, or the number from 'players'.");
		return;
	}
	if (slot < 0) {
		say_back("No such player ('players' lists them).");
		return;
	}
	say_back(net_server_kick(slot, minutes, reason));
}

static void command(const char *text)
{
	char cmd[16], buf[1024];
	const char *args;
	u32 n = 0;
	int v;

	text = skip_space(text);
	if (!*text) return;
	log_msg("server: > %s", text);
	while (text[n] && text[n] != ' ' && n < sizeof cmd - 1) cmd[n] = text[n], n++;
	cmd[n] = 0;
	args = text;
	while (*args && *args != ' ') args++;
	args = skip_space(args);
	if (cmd[0] == '/') memcpy(cmd, cmd + 1, sizeof cmd - 1);   /* "/kick" as in the chat works too */

	if (ieq(cmd, "help") || ieq(cmd, "?")) help();
	else if (ieq(cmd, "say")) {
		if (!*args) say_back("Usage: say <text>");
		else if (!net_server_say(args)) say_back("(nobody is here to read it)");
	} else if (ieq(cmd, "players") || ieq(cmd, "who")) players_cmd();
	else if (ieq(cmd, "status")) say_back(net_server_status(buf, sizeof buf));
	else if (ieq(cmd, "kick")) remove_cmd(args, 5);
	else if (ieq(cmd, "ban")) remove_cmd(args, 0);
	else if (ieq(cmd, "bans")) say_back(net_server_bans(buf, sizeof buf));
	else if (ieq(cmd, "unban")) say_back(net_server_unban(args));
	else if (ieq(cmd, "start")) say_back(net_server_start());
	else if (ieq(cmd, "warmup")) say_back(net_server_warmup());
	else if (ieq(cmd, "timelimit")) {
		if (!number(args, &v) || v < 0 || v > NET_TIME_LIMIT_MAX) say_back("Usage: timelimit <minutes, 0 to 999>");
		else {
			net_server_set_limits(v, 0, 1, 0);
			fmt(buf, "Time limit: %d minutes%s, from the next match.", v, v ? "" : " (none)");
			say_back(buf);
		}
	} else if (ieq(cmd, "scorelimit")) {
		if (!number(args, &v) || v < -NET_SCORE_LIMIT_MAX || v > NET_SCORE_LIMIT_MAX) say_back("Usage: scorelimit <points, -999 to 999>");
		else {
			net_server_set_limits(0, v, 0, 1);
			fmt(buf, "Score limit: %d%s, from the next match.", v, v ? "" : " (none)");
			say_back(buf);
		}
	} else if (ieq(cmd, "maxplayers")) {
		if (!number(args, &v) || v < 2 || v > NET_PLAYERS_MAX) say_back("Usage: maxplayers <2 to 4>");
		else {
			net_server_set_max_players((u32)v);
			fmt(buf, "Max players: %d (players already in stay).", v);
			say_back(buf);
		}
	} else if (ieq(cmd, "name")) {
		if (!*args) say_back("Usage: name <server name>");
		else {
			net_server_set_name(args);
			SetConsoleTitleA(args);
			term_title(args);
			say_back("Server name changed (the server list shows it from the next heartbeat).");
		}
	} else if (ieq(cmd, "quit") || ieq(cmd, "exit")) {
		g_quit = 1;
	} else if (!net_server_say(text)) {   /* plain text: a server message */
		say_back("(nobody is here to read it)");
	}
}

/* ---- The Linux terminal console --------------------------------------------------------------- */

static const char g_console_sh[] = {
#include "console_sh.inc"
	0
};

static void term_send(int i, const char *s)
{
	if (g_tc[i].sock != NO_SOCKET) send(g_tc[i].sock, s, (int)str_len(s), 0);
}

static void term_title(const char *name)
{
	char line[NET_SERVER_NAME_MAX + 16];
	fmt(line, "\x01title %s\n", name);
	EnterCriticalSection(g_lock);
	for (int i = 0; i < TERM_MAX; i++)
		if (g_tc[i].authed) term_send(i, line);
	LeaveCriticalSection(g_lock);
}

/* A console left (closed, or a wrong key). The last one that was in takes the server with it. */
static void term_drop(int i)
{
	int was_in = g_tc[i].authed, others = 0;
	closesocket(g_tc[i].sock);
	g_tc[i].sock = NO_SOCKET;
	g_tc[i].authed = 0;
	g_tc[i].len = 0;
	for (int k = 0; k < TERM_MAX; k++) others += g_tc[k].sock != NO_SOCKET && g_tc[k].authed;
	if (was_in && !others && !g_quit) {
		log_msg("server: the console closed: closing the server");
		g_quit = 1;
	}
}

static void term_got_line(int i, const char *line)
{
	if (!g_tc[i].authed) {
		char want[40];
		fmt(want, "KEY %s", g_key);
		if (str_len(line) != str_len(want) || !str_ieq_n(line, want, str_len(want))) {
			log_msg("server: a console connection with the wrong key, closed");
			term_drop(i);
			return;
		}
		char name[NET_SERVER_NAME_MAX], title[NET_SERVER_NAME_MAX + 16];
		ini_text("host", "name", "Arklite dedicated server", name, sizeof name);
		fmt(title, "\x01title %s\n", name);
		g_tc[i].authed = 1;
		g_term_had = 1;
		term_send(i, title);
		for (u32 k = g_nback > BACKLOG ? g_nback - BACKLOG : 0; k < g_nback; k++) term_send(i, g_back[k % BACKLOG]);
		log_msg("server: a Linux console is in");
		return;
	}
	if (*line) {
		char t[LINE_MAX];
		str_cpy(t, sizeof t, line);
		push_utf8(t, str_len(t));
	}
}

/* Each frame: new consoles, their typing (lines into the command queue) */
static void term_poll(void)
{
	char buf[512];
	if (g_listen == NO_SOCKET) return;
	EnterCriticalSection(g_lock);
	for (;;) {
		u32 sock = accept(g_listen, 0, 0), on = 1;
		int i;
		if (sock == NO_SOCKET) break;
		for (i = 0; i < TERM_MAX && g_tc[i].sock != NO_SOCKET; i++) {}
		if (i == TERM_MAX) { closesocket(sock); continue; }
		ioctlsocket(sock, FIONBIO, &on);
		g_tc[i].sock = sock;
		g_tc[i].authed = 0;
		g_tc[i].len = 0;
	}
	for (int i = 0; i < TERM_MAX; i++) {
		while (g_tc[i].sock != NO_SOCKET) {
			int n = recv(g_tc[i].sock, buf, sizeof buf, 0);
			if (n == 0 || (n < 0 && WSAGetLastError() != WSAEWOULDBLOCK)) { term_drop(i); break; }
			if (n < 0) break;
			for (int k = 0; k < n && g_tc[i].sock != NO_SOCKET; k++) {
				char c = buf[k];
				if (c == '\r') continue;
				if (c == '\n') {
					g_tc[i].buf[g_tc[i].len] = 0;
					g_tc[i].len = 0;
					term_got_line(i, g_tc[i].buf);
				} else if (g_tc[i].len < LINE_MAX - 1 && (u8)c >= 0x20) {
					g_tc[i].buf[g_tc[i].len++] = c;
				}
			}
		}
	}
	LeaveCriticalSection(g_lock);
}

static void random_key(void)
{
	u8 b[16];
	void *adv = LoadLibraryA("advapi32.dll");
	int (WINAPI *gen)(void *, u32) = adv ? (void *)GetProcAddress(adv, "SystemFunction036") : 0;   /* RtlGenRandom */
	if (!gen || !gen(b, sizeof b)) {
		u32 q[2] = { 0, 0 }, x;
		QueryPerformanceCounter(q);
		x = q[0] ^ GetTickCount() * 2654435761u ^ GetCurrentProcessId() * 40503u;
		for (int i = 0; i < 16; i++) x = x * 1103515245u + 12345u ^ q[1], b[i] = (u8)(x >> 23);
	}
	for (int i = 0; i < 16; i++) fmt(g_key + 2 * i, "%02x", b[i]);
}

static int write_file(const char *name, const char *data, u32 n)
{
	u32 done = 0;
	HANDLE f = CreateFileA(name, 0x40000000 /* GENERIC_WRITE */, 0, 0, 2 /* CREATE_ALWAYS */, 0x80, 0);
	if (f == INVALID_HANDLE_VALUE) return 0;
	WriteFile(f, data, n, &done, 0);
	CloseHandle(f);
	return done == n;
}

/* Under Wine and not told otherwise: listen, write the key and the script (they run at the first
 * frame, out of the loader's way: server_start can run inside DllMain). 0: Wine's console instead. */
static int term_prepare(void)
{
	char env[8], line[64], wsa[400];
	void *ntdll = GetModuleHandleA("ntdll.dll");
	SOCKADDR_IN a;
	u32 on = 1;

	if (!ntdll || !GetProcAddress(ntdll, "wine_get_version")) return 0;   /* Windows: its own console */
	if (GetEnvironmentVariableA("ARKLITE_SERVER_CONSOLE", env, sizeof env) && str_ieq_n(env, "wine", 5)) return 0;
	for (int i = 0; i < TERM_MAX; i++) g_tc[i].sock = NO_SOCKET;
	if (WSAStartup(0x0202, wsa)) return 0;
	g_listen = socket(2 /* AF_INET */, 1 /* SOCK_STREAM */, 6 /* TCP */);
	if (g_listen == NO_SOCKET) return 0;
	memset(&a, 0, sizeof a);
	a.family = 2;
	a.port = htons((u16)net_server_port());
	a.addr = inet_addr("127.0.0.1");   /* this PC only */
	if (bind(g_listen, &a, sizeof a) || listen(g_listen, 4) || ioctlsocket(g_listen, FIONBIO, &on)) {
		log_msg("server: can't listen for a Linux console on TCP port %d (error %d): Wine's console instead",
		        net_server_port(), WSAGetLastError());
		closesocket(g_listen);
		g_listen = NO_SOCKET;
		return 0;
	}
	random_key();
	fmt(line, "%d %s\n", net_server_port(), g_key);
	if (!write_file("Arklite-console.key", line, str_len(line)) ||
	    !write_file("Arklite-console.sh", g_console_sh, sizeof g_console_sh - 1)) {
		log_msg("server: can't write Arklite-console.sh/.key here: Wine's console instead");
		closesocket(g_listen);
		g_listen = NO_SOCKET;
		return 0;
	}
	return 1;
}

/* Arklite-console.sh, through Wine's start.exe /unix: it opens the desktop's terminal */
static void term_launch(void)
{
	char dir[MAX_PATH], path[MAX_PATH + 32], cmd[MAX_PATH * 2 + 64];
	u16 wide[MAX_PATH + 32];
	char *(__cdecl *unix_name)(const u16 *) = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "wine_get_unix_file_name");
	char *u;
	u32 si[17], pi[4];

	g_term_launched = GetTickCount() | 1;
	if (!unix_name || !GetCurrentDirectoryA(sizeof dir, dir)) goto fail;
	fmt(path, "%s\\Arklite-console.sh", dir);
	if (!MultiByteToWideChar(0 /* CP_ACP */, 0, path, -1, wide, MAX_PATH + 32)) goto fail;
	u = unix_name(wide);
	if (!u || str_len(u) > MAX_PATH) goto fail;
	fmt(cmd, "start.exe /unix /bin/sh \"%s\"", u);
	memset(si, 0, sizeof si);
	si[0] = sizeof si;
	if (!CreateProcessA(0, cmd, 0, 0, 0, 0x08000000 /* CREATE_NO_WINDOW */, 0, 0, si, pi)) goto fail;
	CloseHandle((HANDLE)pi[0]);
	CloseHandle((HANDLE)pi[1]);
	log_msg("server: opening a terminal: %s", cmd);
	return;
fail:
	log_msg("server: couldn't run Arklite-console.sh: Wine's console in a moment");
	g_term_launched = (GetTickCount() - TERM_WAIT_MS) | 1;
}

/* ---- The frame -------------------------------------------------------------------------------- */

static void quiet(void)
{
	if (!ONgSoundOn) return;
	if (SSgMasterVolumeNow > 0.0f) SSrSetMasterVolume(0.0f);   /* SSrUpdate fades it out */
	else ONgSoundOn = 0;
}

static void server_frame(void)
{
	if (!g_frames++) {
		void *winmm = LoadLibraryA("winmm.dll");
		u32 (WINAPI *period)(u32) = winmm ? (void *)GetProcAddress(winmm, "timeBeginPeriod") : 0;
		if (period) period(1);   /* Sleep(1) sleeps 1 ms, not a 15.6 ms timer step */
		if (ONgGameWindow) {
			ShowWindow(ONgGameWindow, SW_HIDE);
			SetWindowTextA(ONgGameWindow, "Oni dedicated server");   /* not "ONI ": a game may start beside it */
		}
		server_print("The game runs: Oni's window is hidden, nothing is drawn, the sound is off. Type help for the commands.");
		if (g_term) term_launch();
	}
	quiet();
	term_poll();
	if (g_term && !g_term_had && g_term_launched && GetTickCount() - g_term_launched > TERM_WAIT_MS && !g_out) {
		wine_console();
		EnterCriticalSection(g_lock);   /* what was printed so far, first */
		for (u32 k = g_nback > BACKLOG ? g_nback - BACKLOG : 0; k < g_nback; k++) out_raw(g_back[k % BACKLOG], str_len(g_back[k % BACKLOG]));
		LeaveCriticalSection(g_lock);
		server_print("No Linux terminal came (see %s): this is Wine's console. 'sh Arklite-console.sh' in Oni/AE still opens one.", log_path());
	}
	while (g_tail != g_head) {
		char line[LINE_MAX];
		__sync_synchronize();
		memcpy(line, g_lines[g_tail % QUEUE], LINE_MAX);
		g_tail++;
		command(line);
	}
	if (g_quit == 1) {
		g_quit = 2;
		server_print("Closing: goodbye to the players.");
		net_server_quit();
	}
	Sleep(1);
}

/* In place of the draw block's first instruction: 1 (eax) skips the block */
static int frame_skip(void)
{
	if (!g_running) return 0;
	server_frame();
	return 1;
}

__attribute__((naked)) static void draw_block_stub(void)
{
	__asm__ volatile(
		"call %c0\n\t"            /* eax, ecx, edx: the block sets all three before using them */
		"testl %%eax, %%eax\n\t"
		"jnz 1f\n\t"
		"movb 0x55600b, %%al\n\t" /* the instruction this replaced */
		"pushl %1\n\t"
		"ret\n"
		"1:\n\t"
		"pushl %2\n\t"
		"ret\n" :: "i"(frame_skip), "i"(ONcMainLoop_DrawNext), "i"(ONcMainLoop_DrawEnd));
}

/* ---- Start ------------------------------------------------------------------------------------ */

void server_install(void)
{
	static const u8 block[] = { 0xa0, 0x0b, 0x60, 0x55, 0x00 };   /* mov al,[0x55600b] */
	u8 jmp[5] = { 0xe9 };

	if (!code_matches(ONcMainLoop_DrawBlock, block, sizeof block)) {
		log_msg("server: the main loop's draw block changed: a dedicated server would still draw");
		return;
	}
	*(u32 *)(jmp + 1) = (u32)draw_block_stub - (ONcMainLoop_DrawBlock + 5);
	patch_write(ONcMainLoop_DrawBlock, jmp, sizeof jmp);
	g_installed = 1;
}

/* A console window of the process's own (Windows', or Wine's), with the reader thread */
static void wine_console(void)
{
	char name[NET_SERVER_NAME_MAX];
	AllocConsole();   /* fails if there's one already: then that one */
	g_in = GetStdHandle(STD_INPUT_HANDLE);
	g_out = GetStdHandle(STD_OUTPUT_HANDLE);
	ini_text("host", "name", "Arklite dedicated server", name, sizeof name);
	SetConsoleTitleA(name);
	SetConsoleCtrlHandler(on_console_event, 1);
	if (g_in && g_in != INVALID_HANDLE_VALUE && SetConsoleMode(g_in, ENABLE_PROCESSED_INPUT))
		CreateThread(0, 0, read_console, 0, 0, 0);
	else log_msg("server: no input to read commands from");
}

void server_start(void)
{
	char env[8];
	u32 (WINAPI *read)(void *) = 0;

	if (g_running) return;
	InitializeCriticalSection(g_lock);
	g_hist_at = (u32)-1;
	g_stdio = GetEnvironmentVariableA("ARKLITE_SERVER_STDIO", env, sizeof env) && env[0] == '1';
	if (g_stdio) {
		g_in = GetStdHandle(STD_INPUT_HANDLE);
		g_out = GetStdHandle(STD_OUTPUT_HANDLE);
		u32 type = g_in && g_in != INVALID_HANDLE_VALUE ? GetFileType(g_in) : 0;
		if (type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK || type == FILE_TYPE_CHAR) read = read_stdio;   /* (CHAR: a terminal) */
	} else if (!(g_term = term_prepare())) {
		wine_console();
	}
	g_running = 1;
	if (read) CreateThread(0, 0, read, 0, 0, 0);
	else if (!g_term && !g_out) log_msg("server: no input to read commands from");
	out_line("Arklite dedicated server " ARKLITE_VERSION);
	server_print("Starting%s. Type help for the commands; anything else is said to every player.",
	             g_installed ? "" : " (this Oni.exe's main loop isn't the known one: it will still draw)");
	if (g_term) server_print("The console opens in a Linux terminal: closing it closes the server.");
}
