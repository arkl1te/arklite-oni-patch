/* Multiplayer text on screen: the lobby's ready prompt and countdown (net.c), and the score
 * (players.c).
 *
 * Drawn right after Oni's own countdown timer, at the end of the HUD (ONiInGameUI_DisplayHUD
 * 0x4fff50 calls the timer 0x4fd500 at 0x50061f with ECX = 0x5ed010), with the same text calls
 * the timer and the console use, on the game's text context (*0x5ece70):
 *   0x42ee50 (ECX context, EDX ARGB): set the color (context +0xc)
 *   0x42e920 (ECX context, EDX string, [rect]): measure the string, rect = {left, top, right,
 *            bottom} as shorts; ret 4
 *   0x42e8c0 (ECX context, EDX style): the family's font of exactly the context's size and that
 *            style, or 0 (no fallback); EDX 3: the context's own font (+4), the one the size
 *            call (0x42ed50 -> 0x42ebd0) picked with its fallbacks and that draws.
 *   0x42eec0 (ECX font): its line height; reads the font, so never with 0
 *   0x42df00 (ECX context, EDX string, [alpha], [0], [point]): draw at point = {x, y} (shorts,
 *            y the bottom of the line); ret 0xc
 * Each line gets a black shadow down and right. The context draws each glyph as a sprite from
 * its font's cache texture (0x41a5e0, 2 corner points {x, y, z, 1/w}, called at 0x42deae); the
 * call is ours so the HUD's own text can be scaled with the HUD (hud_scale.c), and our text
 * passes through it as it is.
 *
 * The text grows with the screen's height in real font sizes, never stretched: Tahoma 14 at
 * REFERENCE_HEIGHT (1050, as on 1400x1050), round(14 * height / 1050) points elsewhere (19 at
 * 1440, 14 at 1080, 10 at 720). Oni's own Tahoma has 7, 9, 10, 12 and 14; big_font.c makes the
 * other sizes from the system's Tahoma (Windows', or Wine's) and puts them in a copy of the
 * context's family while we draw (corner_begin/corner_end). Without Tahoma the context falls back
 * to Oni's nearest smaller size. "FIGHT!" is its own size, 1.5 times that. Margins and gaps follow
 * the screen too (mp_hud_px), and so does everything that lays out by the line height (chat.c,
 * char_menu.c, obituary.c, mp_names.c, photo.c).
 *
 * Lobby: "Press F3 to be ready" in red, or "Press F3 to be unready" in green, and under it who
 * is ready or the countdown, until the match starts; in red under that, what keeps a joiner out
 * (the two PCs' files differ, net.c). Match (or a two-player game on one PC): the score in the
 * top right corner with the players' names (mp_names.c), this PC's player in yellow, the match's
 * time left under it when it has a time limit (red in the last 30 s), the result once a limit
 * was reached ("Press fire to go back to the lobby"), "FIGHT!" for 2 s as a match begins, the name of
 * the player this PC's player faces over their head, and "Out of sync" when a state check failed. The chat
 * (chat.c) in the lower left, in the lobby too. In the warm-up, "Press F2 to change character
 * and name." at the very top, centered, in the chat hint's plain gray. The F2 character menu
 * (char_menu.c) is drawn last, over all of it.
 *
 * The FPS counter (Options > More...) is here for the same text, but not in the HUD, which is
 * skipped in cutscenes and with the UI hidden: it goes after the console display (0x430430,
 * called at 0x4fbe7c by every frame of the game display 0x4fb480), in the score's corner.
 *
 * HUD Centering (Options > More..., hud_scale.c) moves the corners in too: the top right block
 * (FPS, score, time left) and the kill feed (top left) as far as their widest line lets them, so
 * at 100% each ends MARGIN / 2 short of the middle. A block's width only grows while it shows, so
 * it doesn't twitch as lines come and go. */
#include "arklite.h"

#define ONgTextContext      (*(void **)0x5ece70)
#define TSrContext_SetColor ((u16 (ONI_FC *)(void *, u32))0x42ee50)
#define TSrContext_Measure  ((u16 (ONI_FC *)(void *, const char *, short *))0x42e920)
#define TSrContext_GetFont  ((void *(ONI_FC *)(void *, u32))0x42e8c0)
#define TSrFont_LineHeight  ((u16 (ONI_FC *)(void *))0x42eec0)
#define TSrContext_SetSize  ((u16 (ONI_FC *)(void *, u32))0x42ed50)
#define CONTEXT_SIZE(ctx)   (*(u16 *)((u8 *)(ctx) + 0x10))
#define TSrContext_Draw     ((u16 (ONI_FC *)(void *, const char *, u32, u32, short *))0x42df00)
#define M3rScreen_Width     ((u16 (ONI_FC *)(void))0x41e0f0)
#define M3rScreen_Height    ((u16 (ONI_FC *)(void))0x41e130)
#define ONrLevel_GetCurrent ((u16 (ONI_FC *)(void))0x5054c0)
#define ONgGameState        (*(u8 **)0x5ece7c)

#define M3rDraw_Sprite      ((u32 (ONI_FC *)(const float *, const void *))0x41a5e0)
#define ONcText_SpriteCall  0x42deae
#define CONTEXT_FAMILY(ctx) (*(void **)(ctx))
#define REFERENCE_POINTS    14      /* at REFERENCE_HEIGHT */
#define MAX_POINTS          64
#define ONrTimer_Display    ((void (ONI_FC *)(void *))0x4fd500)
#define ONcTimer_DisplayCall 0x50061f
#define COrConsole_Display  ((void (ONI_FC *)(void))0x430430)
#define ONcConsole_DisplayCall 0x4fbe7c

__declspec(dllimport) int WINAPI QueryPerformanceCounter(unsigned long long *);
__declspec(dllimport) int WINAPI QueryPerformanceFrequency(unsigned long long *);

#define RED     0xffff4040u
#define GREEN   0xff40ff40u
#define WHITE   0xffffffffu
#define YELLOW  0xffffe040u
#define SHADOW  0xff000000u
#define HINT_GRAY 0xffa0a0a0u   /* chat.c's "Press ENTER to chat" */
#define WARMUP_HINT_Y mp_hud_px(4)   /* the F2 hint's top: above the score and "Out of sync" (24) */

#define FPS_REFRESH_FRAMES 6
#define REFERENCE_HEIGHT 1050.0f   /* the screen height Tahoma 14 is drawn as is at */
/* in px at the reference height; mp_hud_px gives them for this screen */
#define MARGIN   mp_hud_px(24)
#define LINE_GAP mp_hud_px(4)   /* between the lines in the middle of the screen */
#define GAP2     mp_hud_px(2)
#define GAP4     mp_hud_px(4)

static int g_line_height;         /* on screen, set by corner_begin */
static float g_screen = 1.0f;     /* the screen's height / REFERENCE_HEIGHT, set by corner_begin */
static int g_points, g_fight_points;   /* the text's size, and "FIGHT!"'s */
static void *g_family_was;        /* the context's family, put back by corner_end */

/* HUD Centering: the top right block's widest line while it shows, the FPS line's, and whether
 * the score was drawn this frame (the FPS line, drawn after the HUD, then goes with it) */
static int g_right_width, g_fps_width, g_score_drawn, g_right_shift;

/* Every glyph: Oni's own text goes to HUD scale (hud_scale.c), which leaves ours as it is */
static u32 ONI_FC text_sprite(const float *pts, const void *uv)
{
	return hud_text_sprite(pts, uv);
}

#define LEFT   MP_LEFT
#define CENTER MP_CENTER
#define RIGHT  MP_RIGHT

/* The line height (Oni's 0x42eec0: its ascender) of the font the context draws with. Not the
 * plain font of the context's size (EDX 0): "FIGHT!"'s size is made in bold only (big_font.c),
 * and that lookup's 0 crashed the match's first frame. */
#define CONTEXT_FONT 3
static int font_height(void *ctx)
{
	void *font = TSrContext_GetFont(ctx, CONTEXT_FONT);
	if (!font) font = TSrContext_GetFont(ctx, 0);
	return font ? TSrFont_LineHeight(font) : mp_hud_px(19);   /* Tahoma 14's ascender */
}

/* One line of text in the context's font, at its own size; x is its left, center or right edge,
 * y the top of the line */
static void text(const char *s, int x, int y, int align, u32 color)
{
	void *ctx = ONgTextContext;
	short rect[4] = { 0 }, at[2];
	int w, height;

	if (!ctx) return;
	height = font_height(ctx);
	TSrContext_Measure(ctx, s, rect);
	w = rect[2] - rect[0];
	if (align == CENTER) x -= w / 2;
	else if (align == RIGHT) x -= w;
	for (int pass = 0; pass < 2; pass++) {   /* shadow first */
		int off = pass ? 0 : mp_hud_px(2);
		at[0] = (short)(x + off);
		at[1] = (short)(y + off + height);   /* the point is the line's bottom */
		TSrContext_SetColor(ctx, pass ? color : SHADOW);
		TSrContext_Draw(ctx, s, color >> 24, 0, at);   /* the color's alpha: chat.c fades lines */
	}
}

/* "FIGHT!" as a match begins (net.c fight_ms): the HUD's bold font at FIGHT_SCALE times the
 * text's size (a size of its own, big_font.c), centered; it fades out over its last
 * FIGHT_FADE_MS. Returns the y under it. */
#define FIGHT_SCALE   1.5f
#define FIGHT_FADE_MS 500

static int draw_fight(int ms, int width, int y)
{
	void *ctx = ONgTextContext;
	u32 alpha = ms >= FIGHT_FADE_MS ? 255u : (u32)(ms * 255 / FIGHT_FADE_MS);
	int h;

	TSrContext_SetSize(ctx, (u32)g_fight_points);
	h = font_height(ctx);
	text("FIGHT!", width / 2, y, CENTER, alpha << 24 | (WHITE & 0xffffff));
	TSrContext_SetSize(ctx, (u32)g_points);
	return y + h + LINE_GAP;
}

/* This screen's text size, and "FIGHT!"'s */
static void sizes(void)
{
	int height = M3rScreen_Height();

	g_screen = height > 0 ? height / REFERENCE_HEIGHT : 1.0f;
	g_points = (int)(REFERENCE_POINTS * g_screen + 0.5f);
	if (g_points < 7) g_points = 7;
	if (g_points > MAX_POINTS) g_points = MAX_POINTS;
	g_fight_points = (int)(g_points * FIGHT_SCALE + 0.5f);
}

/* Between corner_begin and corner_end the context has the family with this screen's sizes and
 * the text's size set; all text drawn or measured goes between them */
static u32 corner_begin(void *ctx)
{
	u32 size = CONTEXT_SIZE(ctx);

	sizes();
	text_multibyte_begin();   /* names and chat beyond ASCII (text.c) */
	g_family_was = CONTEXT_FAMILY(ctx);
	CONTEXT_FAMILY(ctx) = big_font_family(g_family_was, g_points, g_fight_points);
	TSrContext_SetSize(ctx, (u32)g_points);   /* that size, else Oni's nearest smaller one */
	g_line_height = font_height(ctx);
	return size;
}

static void corner_end(void *ctx, u32 size)
{
	CONTEXT_FAMILY(ctx) = g_family_was;
	TSrContext_SetSize(ctx, size);
	text_multibyte_end();
}

/* char_menu.c hud_textures_prepare, before a networked match: the fonts' textures and this
 * screen's fonts made now, not while rollback may rewind */
void mp_hud_prepare(void)
{
	void *ctx = ONgTextContext;

	big_font_prepare();
	if (!ctx) return;
	sizes();
	big_font_family(CONTEXT_FAMILY(ctx), g_points, g_fight_points);
}

int mp_hud_px(int px)
{
	int v = (int)(px * g_screen + 0.5f);
	return v || !px ? v : 1;
}

/* For chat.c, char_menu.c and mp_names.c, while they draw from the HUD below */
void mp_hud_text(const char *s, int x, int y, int align, u32 color) { text(s, x, y, align, color); }
int  mp_hud_line_height(void) { return g_line_height; }

/* The same in the plain style (the HUD's context is bold): the context's style (+0x14, 0 plain)
 * and its size again, which picks the font for it (0x42ed50); both put back after */
void mp_hud_text_plain(const char *s, int x, int y, int align, u32 color)
{
	void *ctx = ONgTextContext;
	u32 *style = ctx ? (u32 *)((u8 *)ctx + 0x14) : 0, was;

	if (!ctx) return;
	was = *style;
	*style = 0;
	if (TSrContext_SetSize(ctx, CONTEXT_SIZE(ctx)) == 0) text(s, x, y, align, color);
	*style = was;
	TSrContext_SetSize(ctx, CONTEXT_SIZE(ctx));
}

/* chat.c: the context's style from here on (0 plain; the HUD's is bold), its font picked again
 * for its size (0x42ed50); returns the style before. A style the size has no font for is put
 * back. */
u32 mp_hud_style(u32 style)
{
	void *ctx = ONgTextContext;
	u32 *at = ctx ? (u32 *)((u8 *)ctx + 0x14) : 0, was;

	if (!ctx) return 0;
	was = *at;
	if (was == style) return was;
	*at = style;
	if (TSrContext_SetSize(ctx, CONTEXT_SIZE(ctx)) != 0) {
		*at = was;
		TSrContext_SetSize(ctx, CONTEXT_SIZE(ctx));
	}
	return was;
}

/* The width text() gives the first n characters of s (n < 0: all). Measured with a '|' after
 * them, less the '|' alone, so trailing spaces count (the caret goes after them). */
int mp_hud_text_width(const char *s, int n)
{
	void *ctx = ONgTextContext;
	char buf[256];
	short rect[4] = { 0 }, bar[4] = { 0 };
	int k = 0;

	if (!ctx) return 0;
	while (s[k] && k != n && k < (int)sizeof buf - 3) {   /* whole characters (text.c) */
		int e = text_next(s, k);
		while (k < e) buf[k] = s[k], k++;
	}
	if (!k) return 0;
	buf[k] = '|';
	buf[k + 1] = 0;
	TSrContext_Measure(ctx, buf, rect);
	TSrContext_Measure(ctx, "|", bar);
	return (rect[2] - rect[0]) - (bar[2] - bar[0]);
}

/* mp_hud_text_width in the plain style (mp_hud_text_plain's switch) */
int mp_hud_text_width_plain(const char *s, int n)
{
	void *ctx = ONgTextContext;
	u32 *style = ctx ? (u32 *)((u8 *)ctx + 0x14) : 0, was;
	int w = 0;

	if (!ctx) return 0;
	was = *style;
	*style = 0;
	if (TSrContext_SetSize(ctx, CONTEXT_SIZE(ctx)) == 0) w = mp_hud_text_width(s, n);
	*style = was;
	TSrContext_SetSize(ctx, CONTEXT_SIZE(ctx));
	return w;
}

/* The lines in the middle of the screen go one under another from height / 5: each of these
 * draws at y and returns the y under it */

static int draw_lobby(const NetHud *h, int width, int y)
{
	char line[128];
	int x = width / 2;

	if (players_spectate_wanted() && h->state != NETHUD_LOBBY)   /* spectators don't ready up (net.c ready_frame) */
		text("Watching: /join to play", x, y, CENTER, WHITE);
	else
		text(h->ready ? "Press F3 to be unready" : "Press F3 to be ready", x, y, CENTER, h->ready ? GREEN : RED);
	y += g_line_height + GAP4;
	if (h->match_on)
		fmt(line, "A match is on: you play when it ends");
	else if (h->countdown_ms >= 0)
		fmt(line, "The match starts in %d", (h->countdown_ms + 999) / 1000);
	else if (!h->connected)
		fmt(line, h->joiner ? "Connecting to the host..." : "Waiting for players to join");
	else if (h->players < 2)
		fmt(line, "Waiting for players to join");
	else
		fmt(line, "Warm-up: %d of %d players ready (%d needed)", h->ready_count, h->players, h->needed);
	text(line, x, y, CENTER, WHITE);
	if (h->punch_me[0] && !h->connected) {   /* net.c "Hole punching" */
		y += g_line_height + GAP2;
		str_cpy(line, sizeof line, "Host outside your network? It can /punch you: your /ip");
		text(line, x, y, CENTER, WHITE);
	}
	return y + g_line_height + LINE_GAP;
}

/* What keeps a joiner out, under the lobby text */
static int draw_problem(const NetHud *h, int width, int y)
{
	int x = width / 2, joiner = h->joiner;

	if (!h->problem) return y;
	if (h->problem == NETBAD_FULL) {
		text(joiner ? "Can't join: the server is full" : "A player can't join: the server is full", x, y, CENTER, RED);
		return y + g_line_height + LINE_GAP;
	}
	if (h->problem == NETBAD_KICKED) {   /* the server's admin removed this PC (net.c "Dedicated server") */
		char line[96];
		fmt(line, "Can't join: %s", joiner && net_kick_note()[0] ? net_kick_note() : "the server removed this player");
		text(line, x, y, CENTER, RED);
		return y + g_line_height + LINE_GAP;
	}
	text(joiner ? "Can't join: not the same on both PCs:" : "A player can't join: not the same on both PCs:",
	     x, y, CENTER, RED);
	for (u32 bit = 1; bit <= NETBAD_LAST; bit <<= 1) {
		if (bit == NETBAD_FULL || bit == NETBAD_KICKED) continue;
		if (!(h->problem & bit)) continue;
		y += g_line_height + GAP2;
		text(net_problem_text(bit), x, y, CENTER, RED);
	}
	y += g_line_height + GAP2;
	{
		char line[MAX_PATH + 48];
		fmt(line, "Copy it from one PC to the other (see %s)", log_path());
		text(line, x, y, CENTER, WHITE);
	}
	return y + g_line_height + LINE_GAP;
}

/* The players listed: a network game's still in it (one who left isn't: the F1 Scoreboard keeps their
 * score, marked "(left)"), or the ones on this PC */
static u32 players_shown(const NetHud *h)
{
	u32 mask = 0;
	if (h->state != NETHUD_OFF) return h->present;
	for (int who = 0; who < PLAYERS_MAX; who++)
		if (players_char(who)) mask |= 1u << who;
	return mask;
}

/* The score's lines in the top right; the kill feed (obituary.c) in the top left */
static void draw_score(const NetHud *h, int local, int width)
{
	char line[64];
	int x = width - MARGIN, y = MARGIN, left = players_time_left();

	for (int pass = 0; pass < 2; pass++) {   /* measured, then drawn */
		if (pass) {
			g_right_shift = hud_center_shift(width, 0, MARGIN + g_right_width, MARGIN / 2);
			x = width - MARGIN + g_right_shift;
		}
		if (show_fps()) y += g_line_height + GAP2;
		for (int who = 0; who < PLAYERS_MAX; who++) {
			if (!(players_shown(h) >> who & 1)) continue;
			if (players_spectating(who)) fmt(line, local == who ? "%s (you, watching)" : "%s (watching)", mp_name(who));
			else fmt(line, local == who ? "%s (you)   %d" : "%s   %d", mp_name(who), players_score(who));
			if (pass) text(line, x, y, RIGHT, local == who ? YELLOW : WHITE);
			else { int w = mp_hud_text_width(line, -1); if (w > g_right_width) g_right_width = w; }
			y += g_line_height + GAP2;
		}
		if (left >= 0) {   /* the match's time limit */
			int t = (left + 59) / 60;
			fmt(line, "%d:%02d", t / 60, t % 60);
			if (pass) text(line, x, y, RIGHT, t <= 30 ? RED : WHITE);
			else { int w = mp_hud_text_width(line, -1); if (w > g_right_width) g_right_width = w; }
			y += g_line_height + GAP2;
		}
		if (!pass) {
			if (show_fps() && g_fps_width > g_right_width) g_right_width = g_fps_width;
			y = MARGIN;
		}
	}
	g_score_drawn = 1;
	obit_draw(MARGIN + hud_center_shift(width, 1, MARGIN + obit_width(), MARGIN / 2), MARGIN, local);
}

/* The match is over: the result until everyone pressed fire (net.c) */
static void draw_over(const NetHud *h, int width, int height)
{
	char line[96];
	int x = width / 2, y = height / 5, w = players_winner(h->present), order[PLAYERS_MAX], n;

	if (w < 0) str_cpy(line, sizeof line, "Match over: a draw!");
	else if (w == h->local_player) str_cpy(line, sizeof line, "Match over: you win!");
	else fmt(line, "Match over: %s wins!", mp_name(w));
	text(line, x, y, CENTER, w < 0 ? WHITE : w == h->local_player ? GREEN : RED);
	y += g_line_height + GAP4;
	n = players_ranking(h->present, order);
	if (n == 2) {
		fmt(line, "%s %d : %d %s", mp_name(order[0]), players_score(order[0]), players_score(order[1]), mp_name(order[1]));
		text(line, x, y, CENTER, WHITE);
		y += g_line_height + GAP4;
	} else {
		for (int i = 0; i < n; i++) {
			fmt(line, "%d. %s   %d", i + 1, mp_name(order[i]), players_score(order[i]));
			text(line, x, y, CENTER, order[i] == h->local_player ? YELLOW : WHITE);
			y += g_line_height + GAP2;
		}
		y += GAP2;
	}
	if (h->over_ready) fmt(line, "Waiting for the others (%d of %d ready, %d s)", h->over_ready_count, h->over_players,
	                       (h->over_left_ms + 999) / 1000);
	else fmt(line, "Press fire to go back to the lobby (%d s)", (h->over_left_ms + 999) / 1000);
	text(line, x, y, CENTER, h->over_ready ? GREEN : YELLOW);
}

/* Another PC went quiet or is gone */
static void draw_connection(const NetHud *h, int width, int y)
{
	char line[96];
	int x = width / 2;

	if (h->lost) {   /* a joiner without the host: nobody else moves */
		if (net_dedicated()) str_cpy(line, sizeof line, h->lost == 2 ? "The server closed" : "Connection lost: the server stopped answering");
		else fmt(line, h->lost == 2 ? "%s left the game" : "Connection lost: %s stopped answering", mp_name(0));
		text(line, x, y, CENTER, RED);
		text("The other players stand still. Esc opens the menu to quit.", x, y + g_line_height + LINE_GAP, CENTER, WHITE);
		return;
	}
	if (h->silent_ms) {
		fmt(line, "Waiting for %s... (%d s)", h->silent_who == 0 && net_dedicated() ? "the server" : mp_name(h->silent_who),
		    (h->silent_ms + 999) / 1000);
		text(line, x, y, CENTER, YELLOW);
		y += g_line_height + LINE_GAP;
	}
	/* (players who left or stopped answering: the chat's server line says so once; no lasting
	 * line here) */
}

static void ONI_FC timer_display(void *timer)
{
	NetHud h;
	void *ctx = ONgTextContext;
	int width, height, y;
	u32 color, size;

	hud_timer_display(timer);   /* Oni's timer, scaled with the HUD */
	if (!ctx || !ONgGameState || !ONrLevel_GetCurrent()) return;
	net_hud(&h);
	if (h.state == NETHUD_OFF && !(multiplayer_active() && multiplayer_map())) return;

	width = M3rScreen_Width();
	height = M3rScreen_Height();
	color = *(u32 *)((u8 *)ctx + 0xc);   /* the timer's color, put back after */
	size = corner_begin(ctx);
	y = height / 5;

	/* first, so the screen's messages (warm-up, joining, connection) go over the names */
	if (h.state != NETHUD_LOBBY && h.state != NETHUD_STARTING) mp_names_draw(width, height);
	/* kicked: "Can't join: ..." alone, no F3 or "Connecting" for a lobby it can't enter */
	if (h.state == NETHUD_LOBBY && !char_menu_open() && !(h.joiner && h.problem == NETBAD_KICKED)) y = draw_lobby(&h, width, y);
	else if (h.state == NETHUD_STARTING && h.joining) {
		char line[80];
		fmt(line, "Joining the %s: catching up (%d%%)", h.warmup ? "warm-up" : "match", h.joining_pct);
		text(line, width / 2, y, CENTER, WHITE);
		y += g_line_height + LINE_GAP;
	} else if (h.state == NETHUD_STARTING) {
		text(h.warmup ? "Starting the warm-up..." : "Starting the match...", width / 2, y, CENTER, WHITE);
		y += g_line_height + LINE_GAP;
	} else draw_score(&h, h.state == NETHUD_MATCH ? h.local_player : -1, width);
	if (h.state == NETHUD_MATCH && h.fight_ms > 0 && !h.over) y = draw_fight(h.fight_ms, width, y);
	if (h.state == NETHUD_MATCH && h.warmup && !char_menu_open()) y = draw_lobby(&h, width, y);
	if (h.state == NETHUD_MATCH && h.joining == 2) {   /* in sync, its player comes in a moment later */
		text("Joining the game...", width / 2, y, CENTER, WHITE);
		y += g_line_height + LINE_GAP;
	}
	if (h.state == NETHUD_MATCH && !h.over && net_watching() != -1) {   /* /spectate */
		char line[96];
		int w = net_watching();
		if (w >= 0) fmt(line, "Watching %s (click: next player, /join: play)", mp_name(w));
		else fmt(line, "Free flight (click: watch a player, /join: play)");
		text(line, width / 2, y, CENTER, WHITE);
		y += g_line_height + LINE_GAP;
	}
	if (h.state == NETHUD_LOBBY || h.state == NETHUD_STARTING || h.warmup) y = draw_problem(&h, width, y);
	/* the warm-up's F2 hint at the very top, in the chat hint's style (F2 does nothing while the
	 * chat or the pause menu has the keyboard) */
	if (h.state == NETHUD_MATCH && h.warmup && !h.over && !chat_blocking() && !live_menu_open() && !char_menu_open())
		mp_hud_text_plain("Press F2 to change character and name.", width / 2, WARMUP_HINT_Y, MP_CENTER, HINT_GRAY);
	chat_draw(width, height);
	if (h.desynced) {
		char line[MAX_PATH + 48];
		fmt(line, "Out of sync with another PC (see %s)", log_path());
		text(line, width / 2, mp_hud_px(24), CENTER, RED);
	}
	if (h.state == NETHUD_MATCH && h.over) draw_over(&h, width, height);
	else if (h.state == NETHUD_MATCH) draw_connection(&h, width, y);   /* under the warm-up's lines */
	char_menu_draw(width, height);

	corner_end(ctx, size);
	TSrContext_SetColor(ctx, color);
}

/* Counts every frame, so the first figure after turning it on is a real one. The low half of the
 * performance counter is enough over 6 frames, and float math needs no 64-bit division (-nostdlib). */
static void ONI_FC console_display(void)
{
	static unsigned long long frequency;
	static u32 frames, start;
	static int fps;
	void *ctx = ONgTextContext;

	COrConsole_Display();
	if (photo_active() && ctx) {   /* its bars, grid and keys, with the HUD hidden or not */
		u32 color = *(u32 *)((u8 *)ctx + 0xc), size = corner_begin(ctx);
		photo_draw(M3rScreen_Width(), M3rScreen_Height());
		corner_end(ctx, size);
		TSrContext_SetColor(ctx, color);
	}
	if (++frames >= FPS_REFRESH_FRAMES) {
		unsigned long long now = 0;
		u32 elapsed;

		if (!frequency) QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&now);
		elapsed = (u32)now - start;
		if (start && elapsed) fps = (int)(frames * (float)(long long)frequency / (float)(long long)elapsed + 0.5f);
		start = (u32)now;
		frames = 0;
	}
	if (show_fps() && fps && ctx && ONgGameState && ONrLevel_GetCurrent()) {
		u32 color = *(u32 *)((u8 *)ctx + 0xc);
		char line[16];
		u32 size;
		int width = M3rScreen_Width(), w, shift;

		size = corner_begin(ctx);
		fmt(line, "%d FPS", fps);
		w = mp_hud_text_width(line, -1);
		if (w > g_fps_width) g_fps_width = w;
		if (g_score_drawn) shift = g_right_shift;   /* in line with the score under it */
		else shift = hud_center_shift(width, 0, MARGIN + g_fps_width, MARGIN / 2);
		text(line, width - MARGIN + shift, MARGIN, RIGHT, WHITE);
		corner_end(ctx, size);
		TSrContext_SetColor(ctx, color);
	} else {
		g_fps_width = 0;
	}
	if (!g_score_drawn) g_right_width = 0;   /* the block is gone: measured anew when it comes back */
	g_score_drawn = 0;
}

void mp_hud_install(void)
{
	if (!patch_call(ONcText_SpriteCall, (u32)M3rDraw_Sprite, (u32)text_sprite))
		log_msg("mp hud: text sprite call changed, the HUD's own text isn't scaled");
	big_font_install();
	if (patch_call(ONcTimer_DisplayCall, (u32)ONrTimer_Display, (u32)timer_display))
		log_msg("mp hud: lobby text and score on screen");
	patch_call(ONcConsole_DisplayCall, (u32)COrConsole_Display, (u32)console_display);
}
