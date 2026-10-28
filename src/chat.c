/* Chat in network games (net.c): Enter opens it, Enter sends, Esc closes it.
 *
 * In the lobby and the match, while this PC is in a level of a network game and neither the F2
 * menu nor the pause menu is up. The history sits in the lower left, above the left HUD ring and
 * the subtitle area (whichever is higher), at most 2/5 of the screen wide; without a background
 * while the chat is closed, each message there for [chat] fade seconds (Arklite.ini, 10 by
 * default, 0: they stay), the last second fading out. Enter opens it: a text field under the
 * history, and both get a 20% black background (CHAT_SHADE); the history then shows everything
 * kept (CHAT_KEEP messages), Up/Down scroll it a line, Page Up/Down a page, the mouse wheel 3
 * lines. Enter sends what's typed and closes the chat, Shift+Enter sends and keeps it open, Enter
 * with nothing typed closes it; Esc closes it and throws away what's typed.
 *
 * The field edits like Arklite's menu fields (mp_menu.c): Left/Right, Home/End, Ctrl for words,
 * Shift to select, Ctrl+A, Backspace/Delete (Ctrl: a word); typing replaces the selection. The
 * text scrolls sideways when it's wider than the field. At most CHAT_MAX - 1 characters of plain
 * ASCII (what Oni's fonts draw).
 *
 * Keys come from Oni's window messages: chat.c puts its window procedure in front of Oni's (as
 * mp_names.c does), takes Enter's WM_KEYDOWN to open, and while open eats every WM_KEYDOWN and
 * WM_CHAR (key ups still reach Oni). While the chat is open the game gets no keyboard but keeps
 * the mouse: the player still turns, aims and attacks. Oni (no DirectInput on NT, so under Wine
 * too) polls the keys in its input read 0x403220: GetAsyncKeyState for the mouse buttons 1, 2
 * and 0x10 (Shift, read as mouse button 3 / fire3) through ESI, loaded at 0x4032ee, and for every
 * bound key in the loop at 0x403599. Both go through key_poll, which answers "up" for all but
 * mouse buttons 1 and 2 while the chat blocks. Utility input (F1, Esc's pause) is skipped
 * (live_menu.c), and F2/F3 don't work. That lasts until the Enter or Esc that closed the chat is
 * released, so the key doesn't reach the game.
 *
 * Sending plays the locked-door sound (impulse door_fail, what Oni plays when a door won't
 * open); a message from the other player plays the game-saved sound (impulse autosave). Both are
 * level 0 impulses found by name (0x4801f0, ECX the name) and played without a position
 * (0x480440: ECX impulse, EDX 0, three 0s; what a menu button's click does), outside any tick,
 * so they draw from this PC's own sound RNG stream (lockstep.c) and can't desync anything.
 * Messages go through net.c (PKT_CHAT, resent until acknowledged); they're only shown, never
 * game state. Another player's message also shows over their head (mp_names.c). */
#include "arklite.h"

void *memcpy(void *, const void *, unsigned);

__declspec(dllimport) short WINAPI GetAsyncKeyState(int);
__declspec(dllimport) void * WINAPI GetForegroundWindow(void);
__declspec(dllimport) u32  WINAPI GetWindowThreadProcessId(void *, u32 *);
__declspec(dllimport) u32  WINAPI GetCurrentProcessId(void);
__declspec(dllimport) u32  WINAPI GetTickCount(void);

typedef long (WINAPI *WNDPROC)(void *, u32, u32, long);
__declspec(dllimport) long WINAPI SetWindowLongA(void *, int, long);
__declspec(dllimport) long WINAPI CallWindowProcA(WNDPROC, void *, u32, u32, long);

#define ONgGameState            (*(u8 **)0x5ece7c)
#define ONrLevel_GetCurrent     ((u16 (ONI_FC *)(void))0x5054c0)
#define LIcMouseKeysLoad        0x4032ee   /* mov esi,[GetAsyncKeyState] (6 bytes) */
#define LIcKeyPollCall          0x403599   /* call [GetAsyncKeyState] (6 bytes) */
#define VK_LBUTTON      0x01
#define VK_RBUTTON      0x02
#define SSrImpulse_GetByName    ((u8 *(ONI_FC *)(const char *))0x4801f0)
#define SSrImpulse_Play         ((void (ONI_FC *)(u8 *, const float *, u32, u32, u32))0x480440)
#define ONgLeftRingBackground   (*(u8 **)0x5ed020)   /* TXMP: +0x86 its height */
#define ONgSubtitleArea         (*(u8 **)0x5cb470)   /* +0x12 bounds: left, top, right, bottom (shorts) */
#define ONgMessageArea          (*(u8 **)0x5cb398)

#define GWL_WNDPROC     (-4)
#define WM_KEYDOWN      0x100
#define WM_CHAR         0x102
#define WM_MOUSEWHEEL   0x20a
#define VK_BACK         0x08
#define VK_RETURN       0x0d
#define VK_SHIFT        0x10
#define VK_CONTROL      0x11
#define VK_MENU         0x12
#define VK_ESCAPE       0x1b
#define VK_PRIOR        0x21
#define VK_NEXT         0x22
#define VK_END          0x23
#define VK_HOME         0x24
#define VK_LEFT         0x25
#define VK_UP           0x26
#define VK_RIGHT        0x27
#define VK_DOWN         0x28
#define VK_DELETE       0x2e

#define CHAT_KEEP       64        /* messages kept */
#define CHAT_LINES      10        /* history lines while open */
#define CHAT_LINES_IDLE 6         /* ...while closed */
#define CHAT_SHADE      51        /* 20% black */
#define WRAP_MAX        16
#define PAD             6
#define FADE_OUT_MS     1000
#define WHITE           0xffffffffu
#define YELLOW          0xffffe040u   /* this PC's player, as in the score */
#define ORANGE          0xffffa040u   /* the others */

typedef struct {
	char line[MP_NAME_MAX + 2 + CHAT_MAX];   /* "Name: text" */
	int name_len;                            /* "Name:" */
	int self;
	u32 at;
	int wrap_w, nlines, starts[WRAP_MAX];    /* wrapped for this width (0: not yet) */
} Message;

static Message g_msg[CHAT_KEEP];
static int g_nmsg;                  /* g_msg[0] the oldest */
static int g_open, g_hold, g_scroll;
static char g_in[CHAT_MAX];
static int g_cur, g_anchor, g_view;
static u32 g_fade_ms = 10000;
static WNDPROC g_old_proc;
static u8 *g_black, *g_white, *g_blue;
static u32 g_blink_from;

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- Messages ----------------------------------------------------------------------------- */

static void play(const char *impulse)
{
	u8 *s = SSrImpulse_GetByName(impulse);
	if (s) SSrImpulse_Play(s, 0, 0, 0, 0);
}

static void add(int who, int self, const char *text)
{
	Message *m;
	if (g_nmsg == CHAT_KEEP) {
		memcpy(g_msg, g_msg + 1, sizeof g_msg[0] * (CHAT_KEEP - 1));
		g_nmsg--;
	}
	m = &g_msg[g_nmsg++];
	fmt(m->line, "%s: %s", mp_name(who), text);
	m->name_len = str_len(mp_name(who)) + 1;
	m->self = self;
	m->at = GetTickCount();
	m->wrap_w = 0;
	log_msg("chat: %s", m->line);
}

/* Plain ASCII, no spaces at either end */
static void clean(char *d, const char *s)
{
	int n = 0;
	while (*s == ' ') s++;
	for (; *s && n < CHAT_MAX - 1; s++)
		if (*s >= 0x20 && *s < 0x7f) d[n++] = *s;
	while (n && d[n - 1] == ' ') n--;
	d[n] = 0;
}

/* ---- Muting --------------------------------------------------------------------------------- *
 * The F1 scoreboard's Mute boxes (mp_board.c). A muted player's messages aren't shown, said over
 * their head or sounded. Kept by name until Oni closes, so a player who leaves and joins again
 * stays muted, and someone new in the same place isn't. */

#define MAX_MUTED 16
static char g_muted[MAX_MUTED][MP_NAME_MAX];
static int g_nmuted;

static int muted_index(const char *name)
{
	for (int i = 0; i < g_nmuted; i++)
		if (str_len(g_muted[i]) == str_len(name) && str_ieq_n(g_muted[i], name, str_len(name))) return i;
	return -1;
}

int chat_muted(int who) { return muted_index(mp_name(who)) >= 0; }

void chat_set_muted(int who, int on)
{
	const char *name = mp_name(who);
	int i = muted_index(name);

	if (on && i < 0 && g_nmuted < MAX_MUTED) str_cpy(g_muted[g_nmuted++], MP_NAME_MAX, name);
	if (!on && i >= 0) memcpy(g_muted[i], g_muted[--g_nmuted], MP_NAME_MAX);
	log_msg("chat: %s %s", name, on ? "muted" : "no longer muted");
}

void chat_receive(const char *text)
{
	char t[CHAT_MAX];
	NetHud h;
	int who;

	clean(t, text);
	if (!*t) return;
	net_hud(&h);
	who = 1 - h.local_player;
	if (chat_muted(who)) return;
	add(who, 0, t);
	mp_names_say(who, t);
	play("autosave");
}

static void send(void)
{
	char t[CHAT_MAX];
	NetHud h;

	clean(t, g_in);
	if (!*t) return;
	net_hud(&h);
	add(h.local_player, 1, t);
	if (!net_chat_send(t)) log_msg("chat: nobody else is here to read it");
	play("door_fail");
}

/* ---- Opening and closing ------------------------------------------------------------------ */

static int allowed(void)
{
	NetHud h;
	if (!ONgGameState || !ONrLevel_GetCurrent() || char_menu_open() || live_menu_open() || mp_name_editing()) return 0;
	net_hud(&h);
	return h.state != NETHUD_OFF;
}

static void open_chat(void)
{
	if (g_open) return;
	g_open = 1;
	g_hold = 0;
	g_in[0] = 0;
	g_cur = g_anchor = g_view = g_scroll = 0;
	g_blink_from = GetTickCount();
}

/* The game gets the keyboard back once Enter and Esc are up (hold) or at once */
static void close_chat(int hold)
{
	if (!g_open && !g_hold) return;
	g_open = 0;
	g_in[0] = 0;
	g_scroll = 0;
	g_hold = hold;
}

int chat_blocking(void) { return g_open || g_hold; }

/* Oni's key reads (0x403220): only the mouse buttons while the chat has the keyboard */
static short WINAPI key_poll(int vk)
{
	if (chat_blocking() && vk != VK_LBUTTON && vk != VK_RBUTTON) return 0;
	return GetAsyncKeyState(vk);
}

/* ---- Editing (as mp_menu.c's fields) -------------------------------------------------------- */

/* Spaces, letters and digits (a word), and the rest (each a word of its own kind) */
static int char_class(char c)
{
	if (c == ' ') return 0;
	if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') return 1;
	return 2;
}

static int word_left(int i)
{
	while (i > 0 && !char_class(g_in[i - 1])) i--;
	if (i > 0) {
		int c = char_class(g_in[i - 1]);
		while (i > 0 && char_class(g_in[i - 1]) == c) i--;
	}
	return i;
}

static int word_right(int len, int i)
{
	if (i < len) {
		int c = char_class(g_in[i]);
		if (c) while (i < len && char_class(g_in[i]) == c) i++;
	}
	while (i < len && !char_class(g_in[i])) i++;
	return i;
}

static int selection(int *lo, int *hi)
{
	int len = str_len(g_in), a = clamp(g_anchor, 0, len), c = clamp(g_cur, 0, len);
	*lo = a < c ? a : c;
	*hi = a < c ? c : a;
	return *hi > *lo;
}

static void move(int to, int shift)
{
	g_cur = to;
	if (!shift) g_anchor = to;
	g_blink_from = GetTickCount();
}

/* g_in[lo, hi) becomes ins; the cursor goes after it */
static void replace(int lo, int hi, const char *ins)
{
	char buf[CHAT_MAX];
	int len = str_len(g_in), k = str_len(ins), n;

	if (lo + k + (len - hi) > CHAT_MAX - 1) return;
	memcpy(buf, g_in, lo);
	memcpy(buf + lo, ins, k);
	memcpy(buf + lo + k, g_in + hi, len - hi);
	n = lo + k + len - hi;
	buf[n] = 0;
	memcpy(g_in, buf, n + 1);
	move(lo + k, 0);
}

static int down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

static void key(u32 vk, int lines_page)
{
	int len = str_len(g_in), cursor = clamp(g_cur, 0, len), lo, hi, sel = selection(&lo, &hi);
	int shift = down(VK_SHIFT), ctrl = down(VK_CONTROL);

	switch (vk) {
	case VK_RETURN:   /* Shift+Enter: send and keep typing */
		if (*g_in) send();
		if (*g_in && shift) {
			g_in[0] = 0;
			g_cur = g_anchor = g_view = 0;
		} else {
			close_chat(1);
		}
		break;
	case VK_ESCAPE: close_chat(1); break;
	case VK_LEFT: move(ctrl ? word_left(cursor) : sel && !shift ? lo : cursor > 0 ? cursor - 1 : 0, shift); break;
	case VK_RIGHT: move(ctrl ? word_right(len, cursor) : sel && !shift ? hi : cursor < len ? cursor + 1 : len, shift); break;
	case VK_HOME: move(0, shift); break;
	case VK_END: move(len, shift); break;
	case VK_BACK:
		if (sel) replace(lo, hi, "");
		else if (cursor > 0) replace(ctrl ? word_left(cursor) : cursor - 1, cursor, "");
		break;
	case VK_DELETE:
		if (sel) replace(lo, hi, "");
		else if (cursor < len) replace(cursor, ctrl ? word_right(len, cursor) : cursor + 1, "");
		break;
	case VK_UP: g_scroll++; break;
	case VK_DOWN: if (g_scroll) g_scroll--; break;
	case VK_PRIOR: g_scroll += lines_page; break;
	case VK_NEXT: g_scroll = g_scroll > lines_page ? g_scroll - lines_page : 0; break;
	case 'A':
		if (ctrl) {
			g_anchor = 0;
			move(len, 1);
		}
		break;
	}
}

static void typed(u32 c)
{
	char ins[2] = { (char)c, 0 };
	int lo, hi;

	if (c < 0x20 || c >= 0x7f) return;   /* Enter, Esc, Backspace, Ctrl+letters: their key downs did it */
	if (down(VK_CONTROL) && !down(VK_MENU)) return;   /* (AltGr is Ctrl+Alt: its characters count) */
	if (selection(&lo, &hi)) replace(lo, hi, ins);
	else replace(g_cur, g_cur, ins);
}

static long WINAPI window_proc(void *hwnd, u32 msg, u32 wp, long lp)
{
	if (!g_open) {
		if (msg == WM_KEYDOWN && wp == VK_RETURN && !(lp & 0x40000000) && allowed()) {
			open_chat();
			return 0;
		}
	} else if (msg == WM_KEYDOWN) {
		key(wp, CHAT_LINES - 1);
		return 0;
	} else if (msg == WM_CHAR) {
		typed(wp);
		return 0;
	} else if (msg == WM_MOUSEWHEEL) {
		int notches = (short)(wp >> 16) / 120;
		g_scroll = notches > 0 ? g_scroll + 3 * notches : g_scroll + 3 * notches < 0 ? 0 : g_scroll + 3 * notches;
		return 0;
	}
	return CallWindowProcA(g_old_proc, hwnd, msg, wp, lp);
}

/* Every frame, after the update (live_menu.c's main loop hook) */
void chat_frame(void)
{
	if (!g_old_proc) {
		u32 pid = 0;
		void *w = GetForegroundWindow();
		if (w) GetWindowThreadProcessId(w, &pid);
		if (w && pid == GetCurrentProcessId()) {
			g_old_proc = (WNDPROC)SetWindowLongA(w, GWL_WNDPROC, (long)window_proc);
			if (!g_old_proc) log_msg("chat: can't read the keyboard (no window procedure)");
		}
	}
	if (g_open && !allowed()) close_chat(0);
	if (g_hold && !down(VK_RETURN) && !down(VK_ESCAPE)) close_chat(0);
	if (!net_active() && !g_open) g_nmsg = 0;   /* out of the network game: a new chat next time */
}

/* ---- Drawing ------------------------------------------------------------------------------ */

int chat_wrap(const char *s, int width, int *starts, int max)
{
	int n = 0, i = 0, len = str_len(s);

	while (i < len && n < max) {
		int e = i, space = -1;
		starts[n++] = i;
		while (e < len && mp_hud_text_width(s + i, e + 1 - i) <= width) {
			if (s[e] == ' ') space = e;
			e++;
		}
		if (e >= len) break;
		i = space > i ? space + 1 : e > i ? e : i + 1;   /* at the last space, or mid-word if it has none */
	}
	return n;
}

/* s[a, b) at x */
static void piece(const char *s, int a, int b, int x, int y, u32 color)
{
	char t[MP_NAME_MAX + 2 + CHAT_MAX];
	if (b <= a) return;
	memcpy(t, s + a, b - a);
	t[b - a] = 0;
	mp_hud_text(t, x, y, MP_LEFT, color);
}

static void draw_line(const Message *m, int l, int x, int y, u32 alpha)
{
	int a = m->starts[l], b = l + 1 < m->nlines ? m->starts[l + 1] : str_len(m->line);
	u32 text = (WHITE & 0xffffff) | alpha << 24, name = ((m->self ? YELLOW : ORANGE) & 0xffffff) | alpha << 24;

	if (a < m->name_len) {   /* the name, in its color */
		int e = b < m->name_len ? b : m->name_len;
		piece(m->line, a, e, x, y, name);
		piece(m->line, e, b, x + mp_hud_text_width(m->line + a, e - a), y, text);
	} else {
		piece(m->line, a, b, x, y, text);
	}
}

static void draw_input(int x, int y, int w, int lh)
{
	int len = str_len(g_in), avail = w - 2 * PAD, lo, hi, e;
	int sel = selection(&lo, &hi);

	g_cur = clamp(g_cur, 0, len);
	/* keep the cursor in view */
	if (g_view > g_cur) g_view = g_cur;
	while (g_view < g_cur && mp_hud_text_width(g_in + g_view, g_cur - g_view) > avail) g_view++;
	for (e = g_view; e < len && mp_hud_text_width(g_in + g_view, e + 1 - g_view) <= avail; e++);
	x += PAD;
	if (sel) {
		int a = clamp(lo, g_view, e), b = clamp(hi, g_view, e);
		int xa = x + mp_hud_text_width(g_in + g_view, a - g_view), xb = x + mp_hud_text_width(g_in + g_view, b - g_view);
		hud_rect(g_blue, xa, y, xb - xa, lh, 220);
	}
	piece(g_in, g_view, e, x, y, WHITE);
	if ((GetTickCount() - g_blink_from) % 1000 < 500) {
		int xc = x + mp_hud_text_width(g_in + g_view, g_cur - g_view);
		hud_rect(g_white, xc, y + 1, 2, lh - 2, 255);
	}
}

void chat_draw(int width, int height)
{
	int lh = mp_hud_line_height(), step, x = 16, w, bottom, top, n, lines, total = 0;
	u32 now = GetTickCount();
	float scale = hud_scale();

	if (!g_open && !g_nmsg) return;
	if (!g_black) {
		g_black = hud_solid(0x8000, "arklite_chat_black");
		g_white = hud_solid(0xffff, "arklite_chat_white");
		g_blue = hud_solid(0x8000 | 6 << 10 | 12 << 5 | 24, "arklite_chat_select");
	}
	if (lh <= 0) return;
	step = lh + 2;

	/* above the left ring and the subtitles */
	int ring = ONgLeftRingBackground ? *(u16 *)(ONgLeftRingBackground + 0x86) : 128;
	if (ring <= 0 || ring > 512) ring = 128;
	bottom = height - (int)((10 + ring) * (scale > 0.1f ? scale : 1.0f)) - 10;
	for (int k = 0; k < 2; k++) {
		u8 *area = k ? ONgMessageArea : ONgSubtitleArea;
		short t = area ? *(short *)(area + 0x14) : 0;
		if (t > height / 2 && t - 10 < bottom) bottom = t - 10;
	}
	top = height * 3 / 10;
	w = width * 2 / 5;
	if (w < 280) w = width - 2 * x < 280 ? width - 2 * x : 280;

	/* the field under the history */
	int field_h = lh + 6, field_y = bottom - field_h;
	int hist_bottom = field_y - 6;
	lines = (hist_bottom - top) / step;
	if (lines > (g_open ? CHAT_LINES : CHAT_LINES_IDLE)) lines = g_open ? CHAT_LINES : CHAT_LINES_IDLE;
	if (lines < 1) return;

	for (int i = 0; i < g_nmsg; i++) {
		Message *m = &g_msg[i];
		if (m->wrap_w != w - 2 * PAD) {
			m->wrap_w = w - 2 * PAD;
			m->nlines = chat_wrap(m->line, m->wrap_w, m->starts, WRAP_MAX);
			if (!m->nlines) m->starts[0] = 0, m->nlines = 1;
		}
		total += m->nlines;
	}
	if (g_open) {
		g_scroll = clamp(g_scroll, 0, total > lines ? total - lines : 0);
		hud_rect(g_black, x, hist_bottom - lines * step - 4, w, lines * step + 4, CHAT_SHADE);
		hud_rect(g_black, x, field_y, w, field_h, CHAT_SHADE);
		draw_input(x, field_y + 3, w, lh);
	}

	/* from the newest line up, skipping the scrolled ones */
	int skip = g_open ? g_scroll : 0, row = 0;
	for (n = g_nmsg - 1; n >= 0 && row < lines; n--) {
		const Message *m = &g_msg[n];
		u32 alpha = 255;
		if (!g_open && g_fade_ms) {
			u32 age = now - m->at;
			if (age >= g_fade_ms) break;
			if (age > g_fade_ms - FADE_OUT_MS) alpha = 255 * (g_fade_ms - age) / FADE_OUT_MS;
			if (alpha < 8) continue;
		}
		for (int l = m->nlines - 1; l >= 0 && row < lines; l--) {
			if (skip) {
				skip--;
				continue;
			}
			draw_line(m, l, x + PAD, hist_bottom - (row + 1) * step, alpha);
			row++;
		}
	}
	if (g_open && g_scroll) mp_hud_text("v", x + w - PAD, hist_bottom - step, MP_RIGHT, 0xffa0a0a0u);   /* more below */
}

void chat_install(void)
{
	static const u8 load[] = { 0x8b, 0x35, 0xac, 0x91, 0x52, 0x00 }, poll[] = { 0xff, 0x15, 0xac, 0x91, 0x52, 0x00 };
	static void *key_poll_ptr = (void *)key_poll;
	u8 b[6];

	if (code_matches(LIcMouseKeysLoad, load, 6) && code_matches(LIcKeyPollCall, poll, 6)) {
		b[0] = 0x8b; b[1] = 0x35;   /* mov esi,[&key_poll_ptr] */
		*(u32 *)(b + 2) = (u32)&key_poll_ptr;
		patch_write(LIcMouseKeysLoad, b, 6);
		b[0] = 0xff; b[1] = 0x15;   /* call [&key_poll_ptr] */
		patch_write(LIcKeyPollCall, b, 6);
	} else {
		log_msg("chat: input code changed, typing in the chat also moves the player");
	}
	int fade = GetPrivateProfileIntA("chat", "fade", 10, ini_path());
	g_fade_ms = fade > 0 ? (u32)fade * 1000 : 0;
	if (fade > 0) log_msg("chat: Enter chats in network games; messages fade after %d s", fade);
	else log_msg("chat: Enter chats in network games; messages stay on screen");
}
