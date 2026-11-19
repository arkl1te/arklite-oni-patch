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
 * with nothing typed closes it; Esc closes it and throws away what's typed. The box sits a row
 * higher than that, leaving room under the field for a gray "Press ENTER to chat" while the chat
 * is closed and the match isn't running (lobby, starting or catching up, warm-up, match over, the
 * host lost, a player not answering: show_hint). While the chat is open the same row always says
 * "ESC: close | ENTER: send".
 *
 * The field edits like Arklite's menu fields (mp_menu.c): Left/Right, Home/End, Ctrl for words,
 * Shift to select, Ctrl+A, Backspace/Delete (Ctrl: a word); typing replaces the selection. The
 * text scrolls sideways when it's wider than the field. At most CHAT_MAX - 1 bytes: ASCII, and
 * every other character in two (text.c: Spanish, Russian, Japanese, Chinese...); the cursor, the
 * selection and the wrapping move a character at a time, and lines without spaces (Chinese,
 * Japanese) wrap between any two characters. While an IME composes, what it has so far shows at
 * the cursor, underlined (text_composition), and its candidate list opens under the field.
 *
 * The chat draws in the plain style (the HUD's text is bold): only players' names are bold, and
 * the headings of /help and /movelist (Message.bold_a/bold_b, up to BOLD_MAX spans a message).
 * Lines are measured span by span in their own style (span_w), so wrapping and the move list's
 * column fit the mixed widths. The server's lines come as plain text, so chat_server finds the
 * names in them by their wording ("... entered the game", "... is now known as ...").
 *
 * Keys come from Oni's window messages: chat.c puts its window procedure in front of Oni's (as
 * mp_names.c does; a Unicode one, so WM_CHAR is the real character: text.c), takes Enter's
 * WM_KEYDOWN to open, and while open eats every WM_KEYDOWN and WM_CHAR (key ups still reach Oni). While the chat is open the game gets no keyboard but keeps
 * the mouse: the player still turns, aims and attacks. Oni (no DirectInput on NT, so under Wine
 * too) polls the keys in its input read 0x403220: GetAsyncKeyState for the mouse buttons 1, 2
 * and 0x10 (Shift, read as mouse button 3 / fire3) through ESI, loaded at 0x4032ee, and for every
 * bound key in the loop at 0x403599. Both go through key_poll, which answers "up" for all but
 * mouse buttons 1 and 2 while the chat blocks. Utility input (F1, Esc's pause) is skipped
 * (live_menu.c), and F2/F3 don't work. That lasts until the Enter or Esc that closed the chat is
 * released, so the key doesn't reach the game.
 *
 * Sending plays the locked-door sound (impulse door_fail, what Oni plays when a door won't
 * open); a message from another player plays the game-saved sound (impulse autosave). Both are
 * level 0 impulses found by name (0x4801f0, ECX the name) and played without a position
 * (0x480440: ECX impulse, EDX 0, three 0s; what a menu button's click does), outside any tick,
 * so they draw from this PC's own sound RNG stream (lockstep.c) and can't desync anything.
 * Messages go through net.c (PKT_CHAT, resent until acknowledged); they're only shown, never
 * game state. Another player's message also shows over their head (mp_names.c). The server's
 * lines ("Name entered the game", "Name left the game", chat_server) come from the host the same
 * way, without a name, in green and without a sound.
 *
 * Commands: a line starting with / isn't sent but done (command()). /help lists them; /name New
 * Name renames this PC's player (mp_name_change: saved, and the host tells everyone "Old is now
 * known as New"); /whisper or /w Name text goes to that player only (net.c, PktChat.to), shown
 * on both screens in pink, never over a head; /movelist or /moves prints this PC's character's
 * moves (move_list); /taunt, /taunt2, /sneeze and /idle N play an animation on this PC's player,
 * synced (players.c "Emotes"); /clear empties the chat; \/ at the start sends a line beginning
 * with / as a message.
 * Their answers are this screen's lines only, in light blue (INFO). After /help and /movelist
 * the chat stays open, scrolled to the list's first line.
 *
 * The move list is read from the character's animation collection in memory (ONCC +0xc80 and the
 * collections it inherits; "The move list" below), so it's right for every class, a mod's too: each
 * input of moves.md (moves_table.h, generated from the data behind it) is looked up the way the game
 * looks it up. Each row is a line with a tab between the move's name (empty when the game doesn't
 * name it) and its input; the game's own unnamed moves on the inputs every fighter has are left out
 * when the character has them all, and one line says what everyone has. The title names the set
 * as moves.md does ("Konoko's set"), or a mod's collection by name. Tabbed lines draw the part after
 * the tab in a column (Message.tab_ref, the list's widest name, sets where), wrapping inside it.
 *
 * Inputs show in a fighting game's notation (move_icons, 2026-11-16): P punch, K kick, the arrows
 * (text.c's two-byte characters; Tahoma has them) for forward, back, left and right, "+" for keys
 * pressed together, "," for one after another, "x4" as "×4". Crouch, Jump and Action stay words:
 * Oni's default keys put kick on C, so a letter for them would read as a key. What comes before a
 * ": " is the situation ("Running:", "Behind an opponent:") and stays as written, and so does a
 * note after " - ". A line at the top says what the signs mean. moves.md itself stays in words. */
#include "arklite.h"

#define ONgGameWindow (*(void **)0x63100c)   /* ONgPlatformData.gameWindow */

void *memcpy(void *, const void *, unsigned);

__declspec(dllimport) short WINAPI GetAsyncKeyState(int);
__declspec(dllimport) void * WINAPI GetForegroundWindow(void);
__declspec(dllimport) u32  WINAPI GetWindowThreadProcessId(void *, u32 *);
__declspec(dllimport) u32  WINAPI GetCurrentProcessId(void);
__declspec(dllimport) u32  WINAPI GetTickCount(void);

typedef long (WINAPI *WNDPROC)(void *, u32, u32, long);
__declspec(dllimport) long WINAPI SetWindowLongW(void *, int, long);
__declspec(dllimport) long WINAPI CallWindowProcW(WNDPROC, void *, u32, u32, long);

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
#define VK_T            0x54

#define CHAT_KEEP       160       /* messages kept (a move list is up to ~60) */
#define CHAT_LINES      10        /* history lines while open */
#define CHAT_LINES_IDLE 6         /* ...while closed */
#define CHAT_SHADE      51        /* 20% black */
#define WRAP_MAX        16
#define BOLD_MAX        4
#define PAD             mp_hud_px(6)   /* px at 1050 tall, as all the chat's spacing (mp_hud.c) */
#define FADE_OUT_MS     1000
#define WHITE           0xffffffffu
#define YELLOW          0xffffe040u   /* this PC's player, as in the score */
#define ORANGE          0xffffa040u   /* the others */
#define GREEN           0xff90e090u   /* the server's lines: who entered or left */
#define GRAY            0xffa0a0a0u   /* the "Press ENTER to chat" hint */
#define INFO            0xff90c8ffu   /* answers to commands: only this screen's */
#define PINK            0xffff90e0u   /* whispers */

typedef struct {
	char line[MP_NAME_MAX + 16 + CHAT_MAX];  /* "Name: text", "To Name: text" */
	int name_len;                            /* "Name:" (0: a server line) */
	int self;
	u32 color;                               /* the text's */
	u32 at;
	int tab;                                 /* line[tab] is a tab: the rest goes in a column (-1: none) */
	char tab_ref[48];                        /* ...as far right as this text is wide */
	int ref_bold;                            /* ...in bold (the same for every row of a list) */
	int col;                                 /* that column's x, for wrap_w */
	int nbold, bold_a[BOLD_MAX], bold_b[BOLD_MAX];   /* line[bold_a, bold_b) is bold */
	int wrap_w, nlines, starts[WRAP_MAX];    /* wrapped for this width (0: not yet) */
} Message;

static Message g_msg[CHAT_KEEP];
static int g_nmsg;                  /* g_msg[0] the oldest */
static int g_open, g_hold, g_scroll;
static int g_list_top;              /* a list's message count: draw scrolls to its first line (wrapped lines counted) */
static char g_in[CHAT_MAX];
static int g_cur, g_anchor, g_view;
static u32 g_fade_ms = 10000;
static WNDPROC g_old_proc;
static u8 *g_black, *g_white, *g_blue;
static u32 g_blink_from;
static u32 g_bold;                  /* the HUD's (bold) style, while chat_draw draws plain */
static int g_skip_char;             /* the WM_CHAR of the T that opened the chat (developer mode) */

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- Messages ----------------------------------------------------------------------------- */

void mp_sound(const char *impulse)
{
	u8 *s = SSrImpulse_GetByName(impulse);
	if (s) SSrImpulse_Play(s, 0, 0, 0, 0);
}

static Message *new_message(void)
{
	Message *m;
	if (g_nmsg == CHAT_KEEP) {
		memcpy(g_msg, g_msg + 1, sizeof g_msg[0] * (CHAT_KEEP - 1));
		g_nmsg--;
	}
	m = &g_msg[g_nmsg++];
	m->at = GetTickCount();
	m->wrap_w = 0;
	m->self = 0;
	m->name_len = 0;
	m->color = WHITE;
	m->tab = -1;
	m->nbold = 0;
	m->ref_bold = 0;
	return m;
}

/* m->line[a, b) bold */
static void bold(Message *m, int a, int b)
{
	if (b > a && m->nbold < BOLD_MAX) {
		m->bold_a[m->nbold] = a;
		m->bold_b[m->nbold++] = b;
	}
}

/* Where what first is in s, or -1 */
static int find(const char *s, const char *what)
{
	int n = str_len(what);
	for (int i = 0; s[i]; i++) {
		int k = 0;
		while (k < n && s[i + k] == what[k]) k++;
		if (k == n) return i;
	}
	return -1;
}

/* The first what in m's line, bold */
static void bold_text(Message *m, const char *what)
{
	int i = find(m->line, what);
	if (i >= 0) bold(m, i, i + str_len(what));
}

static void add(int who, int self, const char *text)
{
	Message *m = new_message();
	fmt(m->line, "%s: %s", mp_name(who), text);
	m->name_len = str_len(mp_name(who)) + 1;
	bold(m, 0, m->name_len - 1);
	m->self = self;
	log_msg("chat: %s", m->line);
	server_chat_line(m->line);
}

/* An answer to a command: only on this screen. A tab in it starts the column (tab_ref: how wide
 * the column before it is). */
static Message *info(const char *text, const char *tab_ref, u32 color)
{
	Message *m = new_message();
	str_cpy(m->line, sizeof m->line, text);
	m->color = color;
	for (int i = 0; m->line[i]; i++)
		if (m->line[i] == '\t') {
			m->tab = i;
			str_cpy(m->tab_ref, sizeof m->tab_ref, tab_ref ? tab_ref : "");
			break;
		}
	return m;
}

/* Printable ASCII and whole characters beyond it (text.c), no spaces at either end */
static void clean(char *d, const char *s)
{
	int n;
	while (*s == ' ') s++;
	n = text_clean(d, CHAT_MAX, s);
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

void chat_receive(int who, const char *text)
{
	char t[CHAT_MAX];

	clean(t, text);
	if (!*t) return;
	if (chat_muted(who)) return;
	add(who, 0, t);
	mp_names_say(who, t);
	mp_sound("autosave");
}

/* net.c: the server's line (someone entered or left), no name, in its own color */
void chat_server(const char *text)
{
	Message *m = new_message();
	int len, i;

	str_cpy(m->line, sizeof m->line, text);
	m->color = GREEN;
	len = str_len(m->line);
	if ((i = find(m->line, " is now known as ")) > 0) {
		bold(m, 0, i);
		bold(m, i + 17, len);
	} else if ((i = find(m->line, " entered the game")) > 0 && i + 17 == len) {
		bold(m, 0, i);
	} else if ((i = find(m->line, " left the game")) > 0 && i + 14 == len) {
		bold(m, 0, i);
	} else if ((i = find(m->line, " called a vote: ")) > 0) {   /* net.c "Votes" */
		bold(m, 0, i);
	} else if (find(m->line, "Vote passed") == 0 || find(m->line, "Vote failed") == 0) {
		bold(m, 0, 11);
	} else if (find(m->line, "Server: ") == 0) {   /* the dedicated server's admin (net_server_say) */
		bold(m, 0, 7);
		if (!server_running()) mp_sound("autosave");
	} else if (find(m->line, "Server MOTD: ") == 0) {   /* the host's message of the day (net.c) */
		bold(m, 0, 12);
		if (!server_running()) mp_sound("autosave");
	}
	log_msg("chat: (server) %s", m->line);
	server_chat_line(m->line);
}

void chat_whisper_receive(int who, const char *text)
{
	char t[CHAT_MAX];
	Message *m;

	clean(t, text);
	if (!*t || chat_muted(who)) return;
	m = new_message();
	fmt(m->line, "%s whispers: %s", mp_name(who), t);
	m->name_len = str_len(mp_name(who)) + 9;
	bold(m, 0, m->name_len - 9);
	m->color = PINK;
	log_msg("chat: %s", m->line);
	server_chat_line(m->line);
	mp_sound("autosave");
}

/* ---- Commands ------------------------------------------------------------------------------- */

#define CHR_CLASS(c)            (*(u8 **)((c) + 0xc))      /* the ONCC */
#define ONCC_ANIMATIONS(c)      (*(u8 **)((c) + 0xc80))    /* TRAC */

#define ONCC_LEFT_HANDED(o)     (*(u8 *)((o) + 0xc8c))     /* WeaponHand */
#define ONCC_SOUND(o, k)        ((const char *)(o) + 0x2b4 + 32 * (k))   /* AI vocalizations: 6 super punch, 7 super kick */
#define TRAC_PARENT(t)          (*(u8 **)((t) + 0x10))     /* the next collection the lookup tries */
#define TRAC_SHORTCUTS_ONLY(t)  (*(u16 *)((t) + 0x14))     /* the game's own: the shortcuts of the one before */
#define TRAC_COUNT(t)           (*(u16 *)((t) + 0x16))
#define TRAC_STATE(t, i)        (*(u16 *)((t) + 0x1a + 12 * (i)))   /* the situation it's looked up from */
#define TRAC_ANIMATION(t, i)    (*(u8 **)((t) + 0x20 + 12 * (i)))
#define TRAM_VOCALIZATION(a)    (*(u16 *)((a) + 0x4e))
#define TRAM_TYPE(a)            (*(u16 *)((a) + 0x15a))
#define TRAM_VARIENT(a)         (*(u16 *)((a) + 0x168))
#define TRAM_FIRST_LEVEL(a)     (*(u16 *)((a) + 0x176))
#define TRgLevelNumber          (*(u16 *)0x6b8744)
#define STATE_ANYTHING          1
#define VAR_FIGHT               0x0200
#define VAR_RIFLE_RIGHT         0x2000
#define VAR_RIFLE_LEFT          0x4000
#define TYPE_PUNCH_RIFLE        226

#include "moves_table.h"

static int ieq(const char *a, const char *b)
{
	return str_len(a) == str_len(b) && str_ieq_n(a, b, str_len(a));
}

static int starts_i(const char *s, const char *prefix)
{
	int n = str_len(prefix);
	return str_len(s) >= n && str_ieq_n(s, prefix, n);
}

/* This PC's player's character, or 0 */
static u8 *my_char(void)
{
	NetHud h;
	net_hud(&h);
	return players_char(h.local_char);
}

static const char *class_name(u8 *c)
{
	const char *n = c && CHR_CLASS(c) ? TMrInstance_GetInstanceName(CHR_CLASS(c)) : 0;
	return n ? n : "this character";
}

/* ---- The move list ----
 * Read from the character's animation collection as it is in memory, so the moves a mod adds
 * (an AE package's set, a new character) are there too. Each row of moves_table.h (moves.md's
 * inputs) is one of the game's lookups: the move type from the situation, in its collection and
 * the ones it inherits, like Oni.exe's 0x427fd0 (TRrCollection_Lookup) without its random pick
 * among equals (it would draw the game's random numbers). A combo shows only when the move it
 * follows is there (Oni_Combos.c). Names are by animation, so a mod's new animation has none. */

/* The animation col (and the ones after it) plays for type from state with the variant bits var,
 * as 0x427fd0 picks it: the highest variant that fits, the next collection's when it fits better */
static u8 *col_lookup(u8 *col, u32 type, u32 state, u32 var, int depth)
{
	u8 *res = 0, *r;
	if (!col || depth > 32) return 0;
	for (u32 i = 0; i < TRAC_COUNT(col); i++) {
		u8 *a = TRAC_ANIMATION(col, i);
		if (!a || TRAC_STATE(col, i) != state || TRAM_TYPE(a) != type) continue;
		if ((TRAM_VARIENT(a) & ~var) || TRAM_FIRST_LEVEL(a) > TRgLevelNumber) continue;
		if (!res || TRAM_VARIENT(a) > TRAM_VARIENT(res)) res = a;
	}
	if ((!res || TRAM_VARIENT(res) != var) && TRAC_PARENT(col)) {
		r = col_lookup(TRAC_PARENT(col), type, state, var, depth + 1);
		if (!r) r = col_lookup(TRAC_PARENT(col), type, STATE_ANYTHING, var, depth + 1);
		if (r && (!res || TRAM_VARIENT(r) > TRAM_VARIENT(res))) res = r;
	}
	return res;
}

static u8 *anim_lookup(u8 *trac, u32 type, u32 state, u32 var)
{
	u8 *a = col_lookup(trac, type, state, var, 0);
	return a ? a : col_lookup(trac, type, STATE_ANYTHING, var, 0);
}

#define MOVE_ROWS (int)(sizeof g_move_rows / sizeof g_move_rows[0])

/* The animation each row plays for the class (0: it hasn't the move); returns whether it has every
 * common row (then the unnamed game's own moves on those rows are left out) */
static int class_moves(u8 *oncc, u8 *anim[MOVE_ROWS])
{
	u8 *trac = ONCC_ANIMATIONS(oncc);
	u32 rifle = (ONCC_LEFT_HANDED(oncc) ? VAR_RIFLE_LEFT : VAR_RIFLE_RIGHT) | VAR_FIGHT;
	int all = 1, dropped;

	for (int i = 0; i < MOVE_ROWS; i++) {
		const MoveRow *r = &g_move_rows[i];
		anim[i] = 0;
		for (int k = 0; k < 2 && !anim[i] && r->alt[k].type; k++)
			for (int j = 0; j < (int)sizeof r->alt[k].states && r->alt[k].states[j] && !anim[i]; j++)
				anim[i] = anim_lookup(trac, r->alt[k].type, r->alt[k].states[j],
				                      r->alt[k].type == TYPE_PUNCH_RIFLE ? rifle : VAR_FIGHT);
	}
	/* a combo needs the move before it: one of that type, in any situation */
	do {
		dropped = 0;
		for (int i = 0; i < MOVE_ROWS; i++) {
			int has = 0;
			if (!anim[i] || !g_move_rows[i].prereq) continue;
			for (int j = 0; j < MOVE_ROWS && !has; j++) has = anim[j] && TRAM_TYPE(anim[j]) == g_move_rows[i].prereq;
			if (!has) anim[i] = 0, dropped = 1;
		}
	} while (dropped);
	for (int i = 0; i < MOVE_ROWS; i++) if (g_move_rows[i].common && !anim[i]) all = 0;
	return all;
}

static const char *table_find(const char *const t[][2], int n, const char *key)
{
	for (int i = 0; key && i < n; i++) if (ieq(t[i][0], key)) return t[i][1];
	return 0;
}

/* The move's name (gen_md.py's rule): the animation's, and what the class shouts with it */
static void move_name(char *d, int size, u8 *oncc, u8 *a)
{
	const char *n = table_find(g_move_names, sizeof g_move_names / sizeof g_move_names[0], TMrInstance_GetInstanceName(a));
	const char *s = 0;
	u32 v = TRAM_VOCALIZATION(a);
	if (v == 6 || v == 7) {
		char snd[33];
		memcpy(snd, ONCC_SOUND(oncc, v), 32);
		snd[32] = 0;
		s = table_find(g_move_shouts, sizeof g_move_shouts / sizeof g_move_shouts[0], snd);
	}
	if (s && n && !ieq(s, n) && !starts_i(n, s)) fmt(d, "%s (shouts %s)", n, s);
	else str_cpy(d, size, n ? n : s ? s : "");
}

static int is_stock(u8 *a)
{
	const char *n = TMrInstance_GetInstanceName(a);
	for (u32 i = 0; n && i < sizeof g_move_stock / sizeof g_move_stock[0]; i++)
		if (ieq(g_move_stock[i], n)) return 1;
	return 0;
}

/* The collection's set name ("konoko" of TRACkonoko_animations) */
static void set_name(char *d, int size, u8 *trac)
{
	const char *tn = trac && !TRAC_SHORTCUTS_ONLY(trac) ? TMrInstance_GetInstanceName(trac) : 0;
	int n;
	str_cpy(d, size, tn ? tn : "?");
	n = str_len(d);
	if (n > 11 && ieq(d + n - 11, "_animations")) d[n - 11] = 0;
}

/* ---- The notation (see the top) ---- */

#define SYM_FORWARD 0x2191   /* up arrow */
#define SYM_BACK    0x2193   /* down arrow */
#define SYM_LEFT    0x2190
#define SYM_RIGHT   0x2192
#define SYM_TIMES   0x00d7

/* c (Unicode) into d[*n], or the ASCII fallback when Arklite's text can't hold it */
static void put_sym(char *d, int *n, int size, u32 c, const char *fallback)
{
	char two[2];
	int k = text_put(two, c);
	if (k && *n + k < size) {
		for (int i = 0; i < k; i++) d[(*n)++] = two[i];
		return;
	}
	while (*fallback && *n < size - 1) d[(*n)++] = *fallback++;
}

static void put_str(char *d, int *n, int size, const char *t)
{
	while (*t && *n < size - 1) d[(*n)++] = *t++;
}

static int is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/* s starts with word w (any case), not followed by a letter */
static int word_at(const char *s, const char *w)
{
	int n = str_len(w);
	return str_ieq_n(s, w, n) && !is_alpha(s[n]);
}

/* The keys part of an input ("Back, then forward + kick") in the notation ("↓, ↑+K") */
static void keys_icons(char *d, int *n, int size, const char *s)
{
	static const struct { const char *w; u32 sym; const char *text; } words[] = {
		{ "punch", 0, "P" }, { "kick", 0, "K" },
		{ "forward", SYM_FORWARD, "Fwd" }, { "back", SYM_BACK, "Back" },
		{ "left", SYM_LEFT, "Left" }, { "right", SYM_RIGHT, "Right" },
		{ "crouch", 0, "Crouch" }, { "jump", 0, "Jump" }, { "action", 0, "Action" },
	};
	int at_word = 1;   /* s is at the start of a word */

	while (*s && *n < size - 1) {
		int done = 0;
		if (str_ieq_n(s, "Dash (double-tap forward)", 25)) {
			put_sym(d, n, size, SYM_FORWARD, "Fwd");
			put_sym(d, n, size, SYM_FORWARD, "Fwd");
			put_str(d, n, size, " (dash)");
			s += 25;
			continue;
		}
		if (str_ieq_n(s, " + ", 3)) { put_str(d, n, size, "+"); s += 3; at_word = 1; continue; }
		if (str_ieq_n(s, ", then ", 7)) { put_str(d, n, size, ", "); s += 7; at_word = 1; continue; }
		if (at_word && str_ieq_n(s, "let go of crouch", 16) && !is_alpha(s[16])) { put_str(d, n, size, "let go"); s += 16; continue; }
		/* "×4" (moves_table.h has it as "x4") */
		if (at_word && (*s == 'x' || *s == 'X') && s[1] >= '0' && s[1] <= '9') {
			put_sym(d, n, size, SYM_TIMES, "x");
			s++;
			continue;
		}
		if (at_word && is_alpha(*s)) {
			for (u32 i = 0; i < sizeof words / sizeof words[0] && !done; i++) {
				if (!word_at(s, words[i].w)) continue;
				if (words[i].sym) put_sym(d, n, size, words[i].sym, words[i].text);
				else put_str(d, n, size, words[i].text);
				s += str_len(words[i].w);
				done = 1;
			}
			if (done) { at_word = 0; continue; }
		}
		at_word = !is_alpha(*s);
		d[(*n)++] = *s++;
	}
}

/* An input of moves.md (plain ASCII, md_row's) in the notation: the situation before the last
 * ": " and a note after " - " as they are, the keys between them converted */
static void move_icons(char *d, int size, const char *s)
{
	char keys[CHAT_MAX];
	const char *note = 0, *start = s;
	int n = 0, k = 0;

	for (const char *p = s; *p; p++) if (p[0] == ' ' && p[1] == '-' && p[2] == ' ') { note = p; break; }
	for (const char *p = s; *p && (!note || p < note); p++) if (p[0] == ':' && p[1] == ' ') start = p + 2;
	while (s < start && n < size - 1) d[n++] = *s++;
	while (*s && (!note || s < note) && k < (int)sizeof keys - 1) keys[k++] = *s++;
	keys[k] = 0;
	keys_icons(d, &n, size, keys);
	if (note) put_str(d, &n, size, note);
	d[n] = 0;
}

/* The line on top that says what the signs mean */
static void icons_legend(void)
{
	char line[160];
	int n = 0;
	put_str(line, &n, sizeof line, "P punch, K kick, ");
	put_sym(line, &n, sizeof line, SYM_FORWARD, "Fwd");
	put_str(line, &n, sizeof line, " forward, ");
	put_sym(line, &n, sizeof line, SYM_BACK, "Back");
	put_str(line, &n, sizeof line, " back, ");
	put_sym(line, &n, sizeof line, SYM_LEFT, "Left");
	put_str(line, &n, sizeof line, " left, ");
	put_sym(line, &n, sizeof line, SYM_RIGHT, "Right");
	put_str(line, &n, sizeof line, " right; a+b: together, a, b: one after the other");
	line[n] = 0;
	info(line, 0, GRAY);
}

/* full: /movelist, with the gray lines on what everyone has and on blocking; /moves: the moves only */
static int move_list(int full)
{
	u8 *c = my_char(), *oncc = c ? CHR_CLASS(c) : 0, *trac = oncc ? ONCC_ANIMATIONS(oncc) : 0;
	u8 *anim[MOVE_ROWS];
	char set[64], ref[48] = "", row[CHAT_MAX + 64], name[MOVE_ROWS][48];
	const char *title;
	int rows = 0, all;

	if (!c) {
		info("You have no character right now.", 0, INFO);
		return 0;
	}
	if (!trac) {
		info("Can't tell which moves your character has.", 0, INFO);
		return 0;
	}
	all = class_moves(oncc, anim);
	set_name(set, sizeof set, trac);
	title = table_find(g_move_titles, sizeof g_move_titles / sizeof g_move_titles[0], set);
	/* the names; the game's own unnamed moves on the inputs everyone has are left out (when the
	 * character has them all: the gray line says so); the widest name, for the column */
	for (int i = 0; i < MOVE_ROWS; i++) {
		name[i][0] = 0;
		if (!anim[i]) continue;
		move_name(name[i], sizeof name[i], oncc, anim[i]);
		if (all && g_move_rows[i].common && !name[i][0] && is_stock(anim[i])) anim[i] = 0;
		else if (mp_hud_text_width(name[i], str_len(name[i])) > mp_hud_text_width(ref, str_len(ref))) str_cpy(ref, sizeof ref, name[i]);
	}
	if (mp_hud_text_width(ref, str_len(ref)) < mp_hud_text_width("Move", 4)) str_cpy(ref, sizeof ref, "Move");
	/* "Konoko's set", an AE package's set as moves.md names it ("Konoko: New Combat Moves"), or a
	 * mod's collection by name */
	if (!title) fmt(row, "Moves of %s (animation set %s):", class_name(c), set);
	else fmt(row, find(title, ":") >= 0 ? "Moves of %s (%s):" : "Moves of %s (%s's set):", class_name(c), title);
	bold_text(info(row, 0, INFO), class_name(c));
	icons_legend();
	{
		Message *head = info("Move\tInput", ref, GRAY);
		bold(head, 0, str_len(head->line));
		head->ref_bold = ieq(ref, "Move");
	}
	for (int i = 0; i < MOVE_ROWS; i++) {
		char icons[CHAT_MAX];
		if (!anim[i]) continue;
		move_icons(icons, sizeof icons, g_move_rows[i].input);
		fmt(row, "%s\t%s", name[i], icons);
		info(row, ref, WHITE)->ref_bold = ieq(ref, "Move");   /* names no wider than the bold header: its width */
		rows++;
	}
	if (!rows) info(all ? "Nothing but the moves everyone has." : "No hand-to-hand moves.", 0, GRAY), rows = 1;   /* (the lines added, for the scroll) */
	if (!full) return rows + 3;
	if (all) {   /* (two lines: a message holds MP_NAME_MAX + 16 + CHAT_MAX bytes, and one line cut it short) */
		char line[CHAT_MAX];
		int n = 0;
		bold_text(info("Everyone also has: P, P, P and K, K in a row; P or K with a direction, crouched, running, sidestepping "
		               "or in the air; K on the ground;", 0, GRAY), "Everyone also has:");
		put_sym(line, &n, sizeof line, SYM_FORWARD, "Fwd");
		put_str(line, &n, sizeof line, "+P up close or from behind (throws); Crouch, let go, P; ");
		put_sym(line, &n, sizeof line, SYM_FORWARD, "Fwd");
		put_sym(line, &n, sizeof line, SYM_FORWARD, "Fwd");
		put_str(line, &n, sizeof line, " (dash), Crouch; taunt.");
		line[n] = 0;
		info(line, 0, GRAY);
		rows += 2;
	}
	{
		Message *m = info("Blocking: stand still facing the attacker (crouch for low attacks). Escape roll: crouch + any direction.", 0, GRAY);
		bold_text(m, "Blocking:");
		bold_text(m, "Escape roll:");
	}
	return rows + 4;
}

/* /taunt, /taunt2, /sneeze, /idle N: asks players.c to play it on this PC's player in every PC's
 * same tick (word 11 of the next record); here only whether the character has it */
static void emote(int e, const char *what)
{
	u8 *c = my_char(), *a = c ? players_emote_anim(c, e) : 0;
	char t[CHAT_MAX + 64];

	if (!c) {
		info("You have no character right now.", 0, INFO);
		return;
	}
	if (!a) {
		fmt(t, e == PLAYERS_EMOTE_SNEEZE ? "%s can't sneeze (only Konoko's animations have a sneeze)." : "%s has no %s.",
		    class_name(c), what);
		info(t, 0, INFO);
		return;
	}
	players_emote_request(e);
}

/* /spectate, /join: this PC's player watches or plays again. A joiner moves to a spectator's place
 * and back (net.c "Spectators"). The host watches in place: the wish goes out in every record from
 * the next one; every PC makes the change in the same tick (players.c "Spectating"). */
static void spectate(int on)
{
	const char *moved = net_spectate_move(on);   /* a joiner moves to the other kind of place (net.c "Spectators") */
	if (moved) {
		info(moved, 0, INFO);
		return;
	}
	if (!net_synced() || !multiplayer_map()) {
		info("Only in a network game on a multiplayer map.", 0, INFO);
		return;
	}
	if (players_spectate_wanted() == on) {
		info(on ? "You're watching already. /join plays again." : "You're playing already. /spectate watches.", 0, INFO);
		return;
	}
	players_spectate_want(on);
	log_msg("chat: %s", on ? "/spectate" : "/join");
	info(on ? "You're watching in free flight (move and look as usual, jump: up, crouch: down, walk: slow). Click: watch a player. /join plays again."
	        : "You're back in the game.", 0, INFO);
}

/* Who /w means: the longest player name the text starts with (names can have spaces), else the
 * one player whose name starts with (or holds) the first word. *rest: the message after it.
 * -1: nobody, -2: more than one fits the word. */
static int whisper_target(const char *args, const char **rest)
{
	NetHud h;
	char word[MP_NAME_MAX];
	int best = -1, best_len = 0, found = -1, count = 0, n = 0;

	net_hud(&h);
	for (int w = 0; w < NET_SLOTS; w++) {
		const char *nm = mp_name(w);
		int k = str_len(nm);
		if (w == h.local_player || !(h.present >> w & 1) || !k) continue;
		if (k > best_len && str_ieq_n(args, nm, k) && (args[k] == ' ' || !args[k])) best = w, best_len = k;
	}
	if (best >= 0) {
		*rest = args + best_len;
		return best;
	}
	while (args[n] && args[n] != ' ' && n < MP_NAME_MAX - 1) word[n] = args[n], n++;
	word[n] = 0;
	while (args[n] && args[n] != ' ') n++;
	*rest = args + n;
	for (int pass = 0; pass < 2 && !count; pass++)
		for (int w = 0; w < NET_SLOTS; w++) {
			const char *nm = mp_name(w);
			if (w == h.local_player || !(h.present >> w & 1) || !*nm) continue;
			if (pass ? str_contains_i(nm, word) : starts_i(nm, word)) found = w, count++;
		}
	return count == 1 ? found : count ? -2 : -1;
}

static void whisper(const char *args)
{
	const char *rest;
	char t[CHAT_MAX], line[CHAT_MAX + 64];
	int to;
	Message *m;

	if (!*args) {
		info("Usage: /w Name message", 0, INFO);
		return;
	}
	to = whisper_target(args, &rest);
	while (*rest == ' ') rest++;
	clean(t, rest);
	if (to == -2) {
		info("More than one player's name fits that: type more of it.", 0, INFO);
		return;
	}
	if (to < 0) {
		info("No player here has that name.", 0, INFO);
		return;
	}
	if (!*t) {
		fmt(line, "Usage: /w %s message", mp_name(to));
		info(line, 0, INFO);
		return;
	}
	if (!net_chat_whisper(to, t)) {
		fmt(line, "%s can't be reached right now.", mp_name(to));
		bold(info(line, 0, INFO), 0, str_len(mp_name(to)));
		return;
	}
	m = new_message();
	fmt(m->line, "To %s: %s", mp_name(to), t);
	m->name_len = str_len(mp_name(to)) + 4;
	bold(m, 3, m->name_len - 1);
	m->self = 1;
	m->color = PINK;
	log_msg("chat: %s", m->line);
	mp_sound("door_fail");
}

static int help(void)
{
	static const char *const rows[] = {
		"/help\tthis list",
		"/clear\tempty the chat",
		"/name New Name\tchange your name (everyone is told)",
		"/whisper, /w Name text\tonly that player reads it",
		"/movelist, /moves\tyour character's moves (/moves: without the notes)",
		"/taunt, /t; /taunt2, /t2\ta taunt: the first, or the rare second (Konoko, Ninja's moonwalk, Tanker)",
		"/sneeze\tsneeze (Konoko)",
		"/idle, /i\ta special idle (most characters have one)",
		"/spectate\twatch without playing: a spectator's place, no one waits for you",
		"/join\tplay again after /spectate or Spectate (your score starts at 0)",
		"/ready, /unready\tready up for the match, or not (as F3 does)",
		"/players\tthe players and their numbers (one per internet address, from 1)",
		"/callvote map Name\tvote to change the map (its folder or its name)",
		"/callvote kick Who [why]\tvote to kick a player (name or number from /players)",
		"/callvote restart\tvote to restart from a warm-up: everyone reloads, back in sync",
		"/yes, /no\tanswer the vote (or Y / N, or the F1 Scoreboard)",
		"/ip\tyour internet address, for a host outside your network",
		"/punch address\thost: let in a player outside whose host can't be reached (they tell you their /ip)",
		"/bug what happened\tsend the developer this moment: your screen, words and the game so far",
		"/lagometer, /lag\tthe Lag-o-meter: ping, rollbacks and late inputs as they happen",
	};
	bold_text(info("Commands (taunts, sneeze and idles while standing; any key stops them):", 0, INFO), "Commands");
	for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
		Message *m = info(rows[i], "/callvote kick Who [why]", WHITE);   /* (the widest command: the column starts past it) */
		bold(m, 0, m->tab);   /* the command */
		m->ref_bold = 1;
	}
	return 1 + (int)(sizeof rows / sizeof rows[0]);
}

/* /players: everyone in this game with the number the host gave their internet address (net.c
 * "Votes"); /callvote kick takes it */
static int players_list(void)
{
	NetHud h;
	char line[CHAT_MAX + 64], id[16];
	int n = 0;

	net_hud(&h);
	if (h.state == NETHUD_OFF) {
		info("Only in a network game.", 0, INFO);
		return 0;
	}
	bold_text(info("Players (number: the same for everyone at one internet address):", 0, INFO), "Players");
	for (int w = 0; w < NET_SLOTS; w++) {
		u32 pid;
		Message *m;
		if (!(h.present >> w & 1) || (w == 0 && net_dedicated())) continue;
		pid = net_player_id(w);
		if (pid) fmt(id, "%d", pid);
		else str_cpy(id, sizeof id, "-");
		fmt(line, "%s\t%s%s%s%s", id, mp_name(w)[0] ? mp_name(w) : "(no name yet)", w == 0 ? " (host)" : "",
		    w == h.local_player ? " (you)" : "", h.state == NETHUD_MATCH && players_spectating(w) ? " (watching)" : "");
		line[CHAT_MAX - 1] = 0;
		m = info(line, "99", WHITE);
		bold(m, 0, m->tab);
		n++;
	}
	return n + 1;
}

/* /callvote map Name, /callvote kick Who [why], /callvote restart: the host runs it (net.c "Votes"), and says it to everyone */
static void vote(const char *args)
{
	const char *err;
	if (!*args) {
		info("Usage: /callvote map Name, /callvote kick Who [why] (Who: a name, or a number from /players), or /callvote restart", 0, INFO);
		return;
	}
	if ((err = net_vote_call(args))) info(err, 0, INFO);
	else log_msg("chat: /callvote %s", args);
}

static void answer(int yes)
{
	const char *err = net_vote_cast(yes);
	if (err) info(err, 0, INFO);
}

/* A line starting with /: done here, never sent. Returns the lines a list printed (the chat
 * stays open on it), else 0. */
static int command(const char *t)
{
	char cmd[16], line[CHAT_MAX + 64];
	int n = 0;
	const char *args;

	t++;
	while (t[n] && t[n] != ' ' && n < (int)sizeof cmd - 1) cmd[n] = t[n], n++;
	cmd[n] = 0;
	args = t;
	while (*args && *args != ' ') args++;
	while (*args == ' ') args++;

	if (ieq(cmd, "help") || ieq(cmd, "?")) return help();
	if (ieq(cmd, "players")) return players_list();
	if (ieq(cmd, "bug")) {   /* bug.c: the moment, sent to the developer */
		info(bug_report(args), 0, INFO);
		return 0;
	}
	if (ieq(cmd, "lagometer") || ieq(cmd, "lag")) {
		lagometer_toggle();
		info(lagometer_on() ? "Lag-o-meter on (/lagometer again turns it off)" : "Lag-o-meter off", 0, INFO);
		return 0;
	}
	if (ieq(cmd, "callvote")) {
		vote(args);
		return 0;
	}
	if (ieq(cmd, "yes") || ieq(cmd, "no")) {
		answer(ieq(cmd, "yes"));
		return 0;
	}
	if (ieq(cmd, "clear")) {
		g_nmsg = 0;
		g_scroll = 0;
		return 0;
	}
	{   /* /taunt, /taunt2, /t, /t2 (also "/taunt 2"); /idle, /idle2, /i, /i2, /i3 (also "/idle 2") */
		int kind = 0, n = 0;
		const char *num = 0;
		if (starts_i(cmd, "taunt")) kind = 't', num = cmd + 5;
		else if (starts_i(cmd, "idle")) kind = 'i', num = cmd + 4;
		else if (to_upper(cmd[0]) == 'T' || to_upper(cmd[0]) == 'I') kind = to_upper(cmd[0]) == 'T' ? 't' : 'i', num = cmd + 1;
		if (kind && *num && !(num[0] >= '0' && num[0] <= '9' && !num[1])) kind = 0;   /* /test, /tauntx: not these */
		if (kind && !*num && *args) num = args;   /* the number as an argument */
		if (kind && !*num) n = 1;
		else if (kind && num[0] >= '1' && num[0] <= '9' && !num[1]) n = num[0] - '0';
		if (kind && (*num || !*args || num == args)) {
			int max = kind == 't' ? 2 : PLAYERS_EMOTE_IDLES;
			char what[32];
			if (n < 1 || n > max) {
				fmt(line, kind == 't' ? "Usage: /t or /taunt, /t2 or /taunt2" : "Usage: /i or /idle, up to /i%d", max);
				info(line, 0, INFO);
				return 0;
			}
			if (kind == 't') {
				emote(n == 2 ? PLAYERS_EMOTE_TAUNT2 : PLAYERS_EMOTE_TAUNT, n == 2 ? "second taunt" : "taunt");
			} else {
				fmt(what, n == 1 ? "special idle" : "special idle %d", n);
				emote(PLAYERS_EMOTE_IDLE + n - 1, what);
			}
			return 0;
		}
	}
	if (ieq(cmd, "name")) {
		if (!*args) info("Usage: /name New Name", 0, INFO);
		else if (!mp_name_change(args)) info("That's your name already.", 0, INFO);
		return 0;
	}
	if (ieq(cmd, "whisper") || ieq(cmd, "w")) {
		whisper(args);
		return 0;
	}
	if (ieq(cmd, "movelist") || ieq(cmd, "moves")) return move_list(ieq(cmd, "movelist"));
	if (ieq(cmd, "ready") || ieq(cmd, "unready")) {   /* (what F3 does; F3 is a developer key in developer mode) */
		const char *why = net_ready_command(ieq(cmd, "ready"));
		if (why) info(why, 0, INFO);
		return 0;
	}
	if (ieq(cmd, "sneeze")) {
		emote(PLAYERS_EMOTE_SNEEZE, "sneeze");
		return 0;
	}
	if (ieq(cmd, "ip")) {   /* net.c "Hole punching" */
		char a[32];
		if (net_my_address(a)) fmt(line, "Your internet address: %s", a);
		else str_cpy(line, sizeof line, "Your internet address isn't known (no answer from the STUN server, or not in a network game).");
		info(line, 0, INFO);
		return 0;
	}
	if (ieq(cmd, "punch")) {
		info(*args ? net_punch(args) : "Usage: /punch 1.2.3.4:5678 (the address and port the player sees with /ip)", 0, INFO);
		return 0;
	}
	if (ieq(cmd, "spectate") || ieq(cmd, "join")) {
		spectate(ieq(cmd, "spectate"));
		return 0;
	}
	fmt(line, "No command /%s. /help lists them.", cmd);
	info(line, 0, INFO);
	return 0;
}

/* Returns the lines a command's list printed (the chat stays open, scrolled to its top) */
static int send(void)
{
	char t[CHAT_MAX];
	NetHud h;

	clean(t, g_in);
	if (!*t) return 0;
	if (t[0] == '/') return command(t);
	if (t[0] == '\\' && t[1] == '/') memcpy(t, t + 1, str_len(t));   /* \/: a message that starts with / */
	net_hud(&h);
	add(h.local_player, 1, t);
	if (!net_chat_send(t)) log_msg("chat: nobody else is here to read it");
	mp_sound("door_fail");
	return 0;
}

/* ---- Opening and closing ------------------------------------------------------------------ */

static int allowed(void)
{
	NetHud h;
	if (!ONgGameState || !ONrLevel_GetCurrent() || char_menu_open() || fx_menu_open() || live_menu_open() || mp_name_editing()) return 0;
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
	g_scroll = g_list_top = 0;
	g_hold = hold;
}

/* The hint under the closed chat: while the match isn't running (lobby, starting or catching up,
 * warm-up, over, the host lost, a player not answering) */
static int show_hint(void)
{
	NetHud h;
	if (!allowed()) return 0;
	net_hud(&h);
	return h.state != NETHUD_MATCH || h.warmup || h.over || h.lost || h.joining || h.silent_ms > 0;
}

int chat_blocking(void) { return g_open || g_hold; }

int chat_typing(void) { return g_open; }

/* Oni's key reads (0x403220): only the mouse buttons while the chat has the keyboard */
static short WINAPI key_poll(int vk)
{
	if (chat_blocking() && vk != VK_LBUTTON && vk != VK_RBUTTON) return 0;
	return GetAsyncKeyState(vk);
}

/* ---- Editing (as mp_menu.c's fields) -------------------------------------------------------- */

/* Spaces, letters and digits (a word, with every character beyond ASCII: both its bytes count, so
 * a run of them never ends inside one), and the rest (each a word of its own kind) */
static int char_class(char c)
{
	if (c == ' ') return 0;
	if ((u8)c >= 0x80) return 1;
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
	case VK_RETURN: {   /* Shift+Enter: send and keep typing; a list stays open at its top */
		int listed = *g_in ? send() : 0;
		if (*g_in && (shift || listed)) {
			g_in[0] = 0;
			g_cur = g_anchor = g_view = 0;
			g_scroll = 0;
			g_list_top = listed;
		} else {
			close_chat(1);
		}
		break;
	}
	case VK_ESCAPE: close_chat(1); break;
	case VK_LEFT: move(ctrl ? word_left(cursor) : sel && !shift ? lo : text_prev(g_in, cursor), shift); break;
	case VK_RIGHT: move(ctrl ? word_right(len, cursor) : sel && !shift ? hi : cursor < len ? text_next(g_in, cursor) : len, shift); break;
	case VK_HOME: move(0, shift); break;
	case VK_END: move(len, shift); break;
	case VK_BACK:
		if (sel) replace(lo, hi, "");
		else if (cursor > 0) replace(ctrl ? word_left(cursor) : text_prev(g_in, cursor), cursor, "");
		break;
	case VK_DELETE:
		if (sel) replace(lo, hi, "");
		else if (cursor < len) replace(cursor, ctrl ? word_right(len, cursor) : text_next(g_in, cursor), "");
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

/* A character (Unicode: the window procedure is a W one) */
static void typed(u32 c)
{
	char ins[3] = { 0 };
	int lo, hi;

	if (c < 0x20 || c == 0x7f) return;   /* Enter, Esc, Backspace, Ctrl+letters: their key downs did it */
	if (!text_put(ins, c)) return;       /* none of Arklite's characters (text.c): emoji, Korean... */
	if (down(VK_CONTROL) && !down(VK_MENU)) return;   /* (AltGr is Ctrl+Alt: its characters count) */
	if (selection(&lo, &hi)) replace(lo, hi, ins);
	else replace(g_cur, g_cur, ins);
}

static long WINAPI window_proc(void *hwnd, u32 msg, u32 wp, long lp)
{
	if (!g_open) {
		if (msg == WM_KEYDOWN && wp == VK_RETURN && !(lp & 0x40000000) && allowed() && !dev_mode()) {   /* (developer mode: Enter is its camera key) */
			open_chat();
			return 0;
		}
		/* developer mode (a test build's network game): T opens the chat, and its letter isn't typed */
		if (msg == WM_KEYDOWN && wp == VK_T && !(lp & 0x40000000) && allowed() && dev_mode()) {
			open_chat();
			g_skip_char = 1;
			return 0;
		}
	} else if (msg == WM_KEYDOWN) {
		key(wp, CHAT_LINES - 1);
		return 0;
	} else if (msg == WM_CHAR) {
		if (g_skip_char) {   /* the T that opened it */
			g_skip_char = 0;
			if (wp == 't' || wp == 'T') return 0;
		}
		typed(wp);
		return 0;
	} else if (msg == WM_MOUSEWHEEL) {
		int notches = (short)(wp >> 16) / 120;
		g_scroll = notches > 0 ? g_scroll + 3 * notches : g_scroll + 3 * notches < 0 ? 0 : g_scroll + 3 * notches;
		return 0;
	}
	return CallWindowProcW(g_old_proc, hwnd, msg, wp, lp);
}

/* Every frame, after the update (live_menu.c's main loop hook) */
void chat_frame(void)
{
	if (!g_old_proc) {
		static int said;
		u32 pid = 0;
		void *w = GetForegroundWindow();
		if (w) GetWindowThreadProcessId(w, &pid);
		/* Oni's game window only: a dedicated server's console window (conhost's) also says it's this
		 * process's, and can't be hooked: that was a log line every frame */
		if (w && pid == GetCurrentProcessId() && (!ONgGameWindow || w == ONgGameWindow)) {
			g_old_proc = (WNDPROC)SetWindowLongW(w, GWL_WNDPROC, (long)window_proc);
			if (!g_old_proc && !said++) log_msg("chat: can't read the keyboard (no window procedure)");
		}
	}
	if (g_open && !allowed()) close_chat(0);
	if (g_hold && !down(VK_RETURN) && !down(VK_ESCAPE)) close_chat(0);
	if (!net_active() && !g_open) g_nmsg = 0;   /* out of the network game: a new chat next time */
	{   /* Oni's developer mode: Arklite's keys that clash with its keys step aside (common.c dev_mode) */
		static int dev = -1;
#ifndef ARKLITE_TEST
		if (dev_mode() && net_active()) {   /* a release build: no developer mode in network games (user, 2026-11-16) */
			dev_mode_off();
			log_msg("developer mode: turned off again, a release build allows it in no network game");
			chat_server("Developer mode is off in network games (test builds only).");
		}
#endif
		if (dev_mode() != dev) {
			if (dev >= 0 || dev_mode())
				log_msg("developer mode %s: Arklite's F2, F3, F4 and Enter %s", dev_mode() ? "on" : "off",
				        dev_mode() ? "do nothing (developer keys; T chats, /ready readies)" : "work again");
			dev = dev_mode();
		}
	}
}

/* ---- Drawing ------------------------------------------------------------------------------ */

int chat_wrap(const char *s, int width, int *starts, int max)
{
	int n = 0, i = 0, len = str_len(s);

	while (i < len && n < max) {
		int e = i, space = -1;
		starts[n++] = i;
		while (e < len && mp_hud_text_width(s + i, text_next(s, e) - i) <= width) {
			if (s[e] == ' ') space = e;
			e = text_next(s, e);
		}
		if (e >= len) break;
		i = space > i ? space + 1 : e > i ? e : text_next(s, i);   /* at the last space, or mid-word if it has none */
	}
	return n;
}

/* s[a, b) at x */
static void piece(const char *s, int a, int b, int x, int y, u32 color)
{
	char t[MP_NAME_MAX + 16 + 2 * CHAT_MAX];   /* a line, or the field with an IME composition in it */
	if (b <= a) return;
	if (b - a >= (int)sizeof t) b = a + (int)sizeof t - 1;
	memcpy(t, s + a, b - a);
	t[b - a] = 0;
	mp_hud_text(t, x, y, MP_LEFT, color);
}

static int is_bold(const Message *m, int i)
{
	for (int k = 0; k < m->nbold; k++)
		if (i >= m->bold_a[k] && i < m->bold_b[k]) return 1;
	return 0;
}

/* The end of the run from a (before b) in one style and one color */
static int run_end(const Message *m, int a, int b)
{
	int e = b;
	if (m->name_len > a && m->name_len < e) e = m->name_len;
	for (int k = 0; k < m->nbold; k++) {
		if (m->bold_a[k] > a && m->bold_a[k] < e) e = m->bold_a[k];
		if (m->bold_b[k] > a && m->bold_b[k] < e) e = m->bold_b[k];
	}
	return e;
}

/* The width of line[a, b), each run in its style (chat_draw only: plain is set) */
static int span_w(const Message *m, int a, int b)
{
	int w = 0;
	while (a < b) {
		int e = run_end(m, a, b), on = is_bold(m, a);
		if (on) mp_hud_style(g_bold);
		w += mp_hud_text_width(m->line + a, e - a);
		if (on) mp_hud_style(0);
		a = e;
	}
	return w;
}

/* chat_wrap for m's line from `from` on, in its styles: the line starts into m->starts */
static int wrap_message(Message *m, int from, int width)
{
	int n = 0, i = from, len = str_len(m->line);

	while (i < len && n < WRAP_MAX) {
		int e = i, space = -1;
		m->starts[n++] = i;
		while (e < len && span_w(m, i, text_next(m->line, e)) <= width) {
			if (m->line[e] == ' ') space = e;
			e = text_next(m->line, e);
		}
		if (e >= len) break;
		i = space > i ? space + 1 : e > i ? e : text_next(m->line, i);
	}
	return n;
}

/* line[a, b) at x, each run in its style and color (the name's up to name_len) */
static void draw_span(const Message *m, int a, int b, int x, int y, u32 text, u32 name)
{
	while (a < b) {
		int e = run_end(m, a, b), on = is_bold(m, a);
		if (on) mp_hud_style(g_bold);
		piece(m->line, a, e, x, y, a < m->name_len ? name : text);
		x += mp_hud_text_width(m->line + a, e - a);
		if (on) mp_hud_style(0);
		a = e;
	}
}

static void draw_line(const Message *m, int l, int x, int y, u32 alpha)
{
	int a = m->starts[l], b = l + 1 < m->nlines ? m->starts[l + 1] : str_len(m->line);
	u32 text = (m->color & 0xffffff) | alpha << 24, name = ((m->self ? YELLOW : ORANGE) & 0xffffff) | alpha << 24;

	if (m->tab >= 0) {   /* the name column on the first line, the rest in the column */
		if (!l) draw_span(m, 0, m->tab, x, y, text, name);
		draw_span(m, a, b, x + m->col, y, text, name);
	} else {
		draw_span(m, a, b, x, y, text, name);
	}
}

/* The field: g_in, with the IME's composition (text_composition) at the cursor, underlined */
static void draw_input(int x, int y, int w, int lh, int screen_w, int screen_h)
{
	const char *comp = text_composition();
	char shown[CHAT_MAX * 2];
	int len, avail = w - 2 * PAD, lo, hi, e, cur, clen = str_len(comp);
	int sel = !*comp && selection(&lo, &hi);

	g_cur = clamp(g_cur, 0, str_len(g_in));
	cur = g_cur;
	memcpy(shown, g_in, cur);
	memcpy(shown + cur, comp, clen);
	str_cpy(shown + cur + clen, sizeof shown - cur - clen, g_in + cur);
	len = str_len(shown);
	if (clen) cur += clen;   /* the caret after the composition (the IME's own caret is inside it) */
	/* keep the cursor in view */
	if (g_view > g_cur) g_view = g_cur;
	while (g_view < cur && mp_hud_text_width(shown + g_view, cur - g_view) > avail) g_view = text_next(shown, g_view);
	for (e = g_view; e < len && mp_hud_text_width(shown + g_view, text_next(shown, e) - g_view) <= avail; e = text_next(shown, e));
	x += PAD;
	if (sel) {
		int a = clamp(lo, g_view, e), b = clamp(hi, g_view, e);
		int xa = x + mp_hud_text_width(shown + g_view, a - g_view), xb = x + mp_hud_text_width(shown + g_view, b - g_view);
		hud_rect(g_blue, xa, y, xb - xa, lh, 220);
	}
	piece(shown, g_view, e, x, y, WHITE);
	if (clen) {   /* the composition: underlined */
		int a = clamp(g_cur, g_view, e), b = clamp(g_cur + clen, g_view, e);
		int xa = x + mp_hud_text_width(shown + g_view, a - g_view), xb = x + mp_hud_text_width(shown + g_view, b - g_view);
		if (xb > xa) hud_rect(g_white, xa, y + lh - mp_hud_px(2), xb - xa, mp_hud_px(1) > 0 ? mp_hud_px(1) : 1, 255);
	}
	int xc = x + mp_hud_text_width(shown + g_view, (cur > g_view ? cur : g_view) - g_view);
	if ((GetTickCount() - g_blink_from) % 1000 < 500) hud_rect(g_white, xc, y + 1, mp_hud_px(2), lh - 2, 255);
	text_ime_spot(xc, y, lh, screen_w, screen_h);   /* the IME's candidate list under the field */
}

void chat_draw(int width, int height)
{
	int lh = mp_hud_line_height(), step, x = mp_hud_px(16), w, bottom, top, n, lines, total = 0;
	u32 now = GetTickCount();
	float scale = hud_scale();

	int hint = !g_open && show_hint();

	if (!g_open && !g_nmsg && !hint) return;
	if (!g_black) {
		g_black = hud_solid(0x8000, "arklite_chat_black");
		g_white = hud_solid(0xffff, "arklite_chat_white");
		g_blue = hud_solid(0x8000 | 6 << 10 | 12 << 5 | 24, "arklite_chat_select");
	}
	if (lh <= 0) return;
	step = lh + mp_hud_px(2);

	/* above the left ring and the subtitles */
	int ring = ONgLeftRingBackground ? *(u16 *)(ONgLeftRingBackground + 0x86) : 128;
	if (ring <= 0 || ring > 512) ring = 128;
	bottom = height - (int)((10 + ring) * (scale > 0.1f ? scale : 1.0f)) - 10;
	for (int k = 0; k < 2; k++) {
		u8 *area = k ? ONgMessageArea : ONgSubtitleArea;
		short t = area ? *(short *)(area + 0x14) : 0;
		if (t > height / 2 && t - 10 < bottom) bottom = t - 10;
	}
	int hint_y = bottom - lh;   /* the hint's row under the field: the box is a row higher for it */
	bottom -= lh + mp_hud_px(4);
	top = height * 3 / 10;
	w = width * 2 / 5;
	if (w < mp_hud_px(280)) w = width - 2 * x < mp_hud_px(280) ? width - 2 * x : mp_hud_px(280);
	{   /* HUD Centering: with the left ring it sits above, but on screen */
		int dx = hud_ring_shift(1);
		if (x + dx + w > width - x) dx = width - x - w - x;
		if (dx > 0) x += dx;
	}
	if (hint) mp_hud_text_plain(dev_mode() ? "Press T to chat (developer mode: ENTER is its camera key)" : "Press ENTER to chat",
	                            x + PAD, hint_y, MP_LEFT, GRAY);
	else if (g_open) mp_hud_text_plain("ESC: close | ENTER: send", x + PAD, hint_y, MP_LEFT, GRAY);

	/* the field under the history */
	int field_h = lh + mp_hud_px(6), field_y = bottom - field_h;
	int hist_bottom = field_y - mp_hud_px(6);
	lines = (hist_bottom - top) / step;
	if (lines > (g_open ? CHAT_LINES : CHAT_LINES_IDLE)) lines = g_open ? CHAT_LINES : CHAT_LINES_IDLE;
	if (lines < 1) return;

	g_bold = mp_hud_style(0);   /* plain, but for the bold runs */
	for (int i = 0; i < g_nmsg; i++) {
		Message *m = &g_msg[i];
		if (m->wrap_w != w - 2 * PAD) {
			m->wrap_w = w - 2 * PAD;
			if (m->tab >= 0) {   /* the part after the tab, wrapped in its column */
				int gap = mp_hud_text_width("    ", 4), tw = span_w(m, 0, m->tab), on = m->ref_bold;
				if (on) mp_hud_style(g_bold);   /* tab_ref is as wide as the column's text, in its style */
				m->col = mp_hud_text_width(m->tab_ref, -1) + gap;
				if (on) mp_hud_style(0);
				if (m->col < tw + gap) m->col = tw + gap;
				if (m->col > m->wrap_w / 2) m->col = m->wrap_w / 2;
				m->nlines = wrap_message(m, m->tab + 1, m->wrap_w - m->col);
				if (!m->nlines) m->starts[0] = m->tab + 1, m->nlines = 1;
			} else {
				m->nlines = wrap_message(m, 0, m->wrap_w);
				if (!m->nlines) m->starts[0] = 0, m->nlines = 1;
			}
		}
		total += m->nlines;
	}
	if (g_open && g_list_top) {   /* the list's lines as drawn (a long one wraps), its first on top */
		int shown = 0;
		for (int i = g_nmsg - g_list_top < 0 ? 0 : g_nmsg - g_list_top; i < g_nmsg; i++) shown += g_msg[i].nlines;
		g_scroll = shown > lines ? shown - lines : 0;
		g_list_top = 0;
	}
	if (g_open) {
		g_scroll = clamp(g_scroll, 0, total > lines ? total - lines : 0);
		hud_rect(g_black, x, hist_bottom - lines * step - mp_hud_px(4), w, lines * step + mp_hud_px(4), CHAT_SHADE);
		hud_rect(g_black, x, field_y, w, field_h, CHAT_SHADE);
		draw_input(x, field_y + mp_hud_px(3), w, lh, width, height);
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
	mp_hud_style(g_bold);
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
