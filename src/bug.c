/* /bug <what happened> in chat: everything that shows the moment, sent as a "bug" report (report.c):
 *   - the frame on the screen (screenshot.c, saved as <log>.bug<n>.png: what Oni drew, which a
 *     desktop capture of a fullscreen game often misses);
 *   - a note (<log>.bug<n>.txt): the player's words, the nickname, the time, the session, the tick,
 *     every other player's ping and the Lag-o-meter's last 128 frames and input packets;
 *   - the session's recording so far (<log>.bug<n>.akn, net.c: playable with ARKLITE_PLAYBACK);
 *   - the log and the machine and setup dump (report.c adds them).
 * The screenshot comes with the next frame drawn and is written on a thread; the report goes when it's
 * there, or after 8 s without it. Eight a session at most. */
#include "arklite.h"

__declspec(dllimport) u32 WINAPI GetTickCount(void);
__declspec(dllimport) int WINAPI CloseHandle(HANDLE);
typedef struct { u16 year, month, weekday, day, hour, minute, second, ms; } BUG_TIME;
__declspec(dllimport) void WINAPI GetLocalTime(void *);

#define ONgGameState  (*(u8 **)0x5ece7c)
#define GS_TICK(gs)   (*(u32 *)((gs) + 0x144))
#define BUG_MAX       8
#define BUG_WAIT_MS   8000

static int g_pending, g_count;
static u32 g_asked_at, g_tick;
static char g_base[MAX_PATH], g_note[MAX_PATH], g_rec[MAX_PATH], g_session[40];

static void put_line(HANDLE f, const char *s)
{
	u32 w;
	WriteFile(f, s, str_len(s), &w, 0);
	WriteFile(f, "\r\n", 2, &w, 0);
}

/* chat.c: returns what the chat says */
const char *bug_report(const char *text)
{
	char line[600], name[64];
	BUG_TIME t;
	HANDLE f;
	if (g_pending) return "Still sending the last /bug: try again in a few seconds.";
	if (!str_ieq_n(g_session, net_session_id(), sizeof g_session)) {
		str_cpy(g_session, sizeof g_session, net_session_id());
		g_count = 0;
	}
	if (g_count >= BUG_MAX) return "That's enough /bug reports for this game, thank you: the next game takes more.";
	g_count++;
	str_cpy(g_base, sizeof g_base, log_path());
	{
		u32 n = str_len(g_base);
		if (n > 4 && str_ieq_n(g_base + n - 4, ".log", 4)) g_base[n - 4] = 0;
		n = str_len(g_base);
		fmt(g_base + n, ".bug%d", g_count);
	}
	fmt(g_note, "%s.txt", g_base);
	fmt(g_rec, "%s.akn", g_base);
	g_tick = ONgGameState ? GS_TICK(ONgGameState) : 0;
	GetLocalTime(&t);
	GetPrivateProfileStringA("multiplayer", "name", "", name, sizeof name, ini_path());
	f = CreateFileA(g_note, 0x40000000, 1, 0, 2 /* CREATE_ALWAYS */, 0x80, 0);
	if (f != INVALID_HANDLE_VALUE) {
		fmt(line, "bug: %s", text && text[0] ? text : "(no words)");
		put_line(f, line);
		fmt(line, "player: %s", name[0] ? name : "(no name)");
		put_line(f, line);
		fmt(line, "time: %04u-%02u-%02u %02u:%02u:%02u", t.year, t.month, t.day, t.hour, t.minute, t.second);
		put_line(f, line);
		fmt(line, "session: %s, slot %d, tick %u, Arklite %s", net_session_id(), net_local_slot(), g_tick, ARKLITE_VERSION);
		put_line(f, line);
		net_ping_text(line, sizeof line);
		put_line(f, "ping:");
		put_line(f, line);
		{
			static char lag[16384];
			lagometer_note(lag, sizeof lag);
			put_line(f, lag);
		}
		CloseHandle(f);
	}
	if (!net_rec_snapshot(g_rec)) g_rec[0] = 0;
	screenshot_request_to(g_base);
	g_pending = 1;
	g_asked_at = GetTickCount();
	log_msg("bug: /bug %d: \"%s\" at tick %u: %s", g_count, text, g_tick, g_note);
	return report_on() ? "Thank you: the screen, the recording and your words go to the developer."
	                   : "Thank you: saved in the logs folder (reports are off in Arklite.ini).";
}

/* every level frame (lockstep.c): the report goes once the screenshot is written */
void bug_frame(void)
{
	char shot[MAX_PATH];
	if (!g_pending) return;
	if (!screenshot_written(shot, sizeof shot)) {
		if (GetTickCount() - g_asked_at < BUG_WAIT_MS) return;
		shot[0] = 0;
	}
	g_pending = 0;
	report_bug(g_session, net_local_slot(), g_tick, g_note, shot, g_rec);
}
