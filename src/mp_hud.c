/* Multiplayer text on screen: the lobby's ready prompt and countdown (net.c), and the score
 * (players.c).
 *
 * Drawn right after Oni's own countdown timer, at the end of the HUD (ONiInGameUI_DisplayHUD
 * 0x4fff50 calls the timer 0x4fd500 at 0x50061f with ECX = 0x5ed010), with the same text calls
 * the timer and the console use, on the game's text context (*0x5ece70):
 *   0x42ee50 (ECX context, EDX ARGB): set the color (context +0xc)
 *   0x42e920 (ECX context, EDX string, [rect]): measure the string, rect = {left, top, right,
 *            bottom} as shorts; ret 4
 *   0x42e8c0 (ECX context, EDX 0): the font; 0x42eec0 (ECX font): its line height
 *   0x42df00 (ECX context, EDX string, [alpha], [0], [point]): draw at point = {x, y} (shorts,
 *            y the bottom of the line); ret 0xc
 * Each line gets a black shadow down and right. The context renders a string into a texture and
 * draws it as one sprite (0x41a5e0, 2 corner points {x, y, z, 1/w}, called at 0x42deae); while
 * we draw, that call doubles the sprite from its top-left corner, so our text is twice Oni's
 * size (the user asked for double).
 *
 * Lobby: "Press F3 to be ready" in red, or "Press F3 to be unready" in green, and under it who
 * is ready or the countdown, until the match starts; in red under that, what keeps a joiner out
 * (the two PCs' files differ, net.c). Match (or a two-player game on one PC): the score in the
 * top right corner with the players' names (mp_names.c), this PC's player in yellow, the match's
 * time left under it when it has a time limit (red in the last 30 s), the result once a limit
 * was reached ("Press fire to go back to the lobby"), the name of
 * the player this PC's player faces over their head, and "Out of sync" when a state check failed. The chat
 * (chat.c) in the lower left, in the lobby too. The F2 character menu
 * (char_menu.c) is drawn last, over all of it. */
#include "arklite.h"

#define ONgTextContext      (*(void **)0x5ece70)
#define TSrContext_SetColor ((u16 (ONI_FC *)(void *, u32))0x42ee50)
#define TSrContext_Measure  ((u16 (ONI_FC *)(void *, const char *, short *))0x42e920)
#define TSrContext_GetFont  ((void *(ONI_FC *)(void *, u32))0x42e8c0)
#define TSrFont_LineHeight  ((u16 (ONI_FC *)(void *))0x42eec0)
#define TSrContext_Draw     ((u16 (ONI_FC *)(void *, const char *, u32, u32, short *))0x42df00)
#define M3rScreen_Width     ((u16 (ONI_FC *)(void))0x41e0f0)
#define M3rScreen_Height    ((u16 (ONI_FC *)(void))0x41e130)
#define ONrLevel_GetCurrent ((u16 (ONI_FC *)(void))0x5054c0)
#define ONgGameState        (*(u8 **)0x5ece7c)

#define M3rDraw_Sprite      ((u32 (ONI_FC *)(const float *, const void *))0x41a5e0)
#define ONcText_SpriteCall  0x42deae
#define TEXT_SCALE          2
#define ONrTimer_Display    ((void (ONI_FC *)(void *))0x4fd500)
#define ONcTimer_DisplayCall 0x50061f

#define RED     0xffff4040u
#define GREEN   0xff40ff40u
#define WHITE   0xffffffffu
#define YELLOW  0xffffe040u
#define SHADOW  0xff000000u

static int g_line_height;         /* scaled */
static int g_scaling;
static float g_anchor_x, g_anchor_y;   /* the line's top-left, which stays put */

/* A string is drawn in pieces (the loop back to 0x42dbd0); every piece is scaled about the same
 * anchor, so the whole line grows */
static u32 ONI_FC text_sprite(const float *pts, const void *uv)
{
	float p[8];
	if (!g_scaling) return hud_text_sprite(pts, uv);   /* Oni's text: HUD scale (hud_scale.c) */
	for (int i = 0; i < 8; i++) p[i] = pts[i];
	for (int k = 0; k < 8; k += 4) {
		p[k] = g_anchor_x + (p[k] - g_anchor_x) * TEXT_SCALE;
		p[k + 1] = g_anchor_y + (p[k + 1] - g_anchor_y) * TEXT_SCALE;
	}
	return M3rDraw_Sprite(p, uv);
}

#define LEFT   MP_LEFT
#define CENTER MP_CENTER
#define RIGHT  MP_RIGHT

/* One line of text; x is its left, center or right edge, y the top of the line */
static void text(const char *s, int x, int y, int align, u32 color)
{
	void *ctx = ONgTextContext;
	short rect[4] = { 0 }, at[2];
	int w;

	if (!ctx) return;
	TSrContext_Measure(ctx, s, rect);
	w = (rect[2] - rect[0]) * TEXT_SCALE;
	if (align == CENTER) x -= w / 2;
	else if (align == RIGHT) x -= w;
	/* the point is the line's bottom at Oni's size, the anchor its top-left */
	g_scaling = 1;
	for (int pass = 0; pass < 2; pass++) {
		int off = pass ? 0 : TEXT_SCALE;   /* shadow first */
		g_anchor_x = (float)(x + off);
		g_anchor_y = (float)(y + off);
		at[0] = (short)(x + off);
		at[1] = (short)(y + off + g_line_height / TEXT_SCALE);
		TSrContext_SetColor(ctx, pass ? color : SHADOW);
		TSrContext_Draw(ctx, s, color >> 24, 0, at);   /* the color's alpha: chat.c fades lines */
	}
	g_scaling = 0;
}

/* For char_menu.c, while it draws from the HUD below */
void mp_hud_text(const char *s, int x, int y, int align, u32 color) { text(s, x, y, align, color); }
int  mp_hud_line_height(void) { return g_line_height; }

/* The width text() gives the first n characters of s (n < 0: all). Measured with a '|' after
 * them, less the '|' alone, so trailing spaces count (the caret goes after them). */
int mp_hud_text_width(const char *s, int n)
{
	void *ctx = ONgTextContext;
	char buf[256];
	short rect[4] = { 0 }, bar[4] = { 0 };
	int k = 0;

	if (!ctx) return 0;
	while (s[k] && k != n && k < (int)sizeof buf - 2) { buf[k] = s[k]; k++; }
	if (!k) return 0;
	buf[k] = '|';
	buf[k + 1] = 0;
	TSrContext_Measure(ctx, buf, rect);
	TSrContext_Measure(ctx, "|", bar);
	return ((rect[2] - rect[0]) - (bar[2] - bar[0])) * TEXT_SCALE;
}

static void draw_lobby(const NetHud *h, int width, int height)
{
	char line[128];
	int x = width / 2, y = height / 5;

	text(h->ready ? "Press F3 to be unready" : "Press F3 to be ready", x, y, CENTER, h->ready ? GREEN : RED);
	y += g_line_height + 4;
	if (h->countdown_ms >= 0)
		fmt(line, "The match starts in %d", (h->countdown_ms + 999) / 1000);
	else if (!h->connected)
		fmt(line, h->local_player ? "Connecting to the host..." : "Waiting for players to join");
	else if (h->players < 2)
		fmt(line, "Waiting for players to join");
	else
		fmt(line, "%d of %d players ready (%d needed)", h->ready_count, h->players, h->needed);
	text(line, x, y, CENTER, WHITE);
}

/* What keeps the joiner out, under the lobby text */
static void draw_problem(const NetHud *h, int width, int height)
{
	int x = width / 2, y = height / 5 + 2 * (g_line_height + 4);

	if (!h->problem) return;
	text(h->local_player ? "Can't join: not the same on both PCs:" : "A player can't join: not the same on both PCs:",
	     x, y, CENTER, RED);
	for (u32 bit = 1; bit <= NETBAD_PLAYERS; bit <<= 1) {
		if (!(h->problem & bit)) continue;
		y += g_line_height + 2;
		text(net_problem_text(bit), x, y, CENTER, RED);
	}
	y += g_line_height + 2;
	text("Copy it from one PC to the other (see Arklite.log)", x, y, CENTER, WHITE);
}

static void draw_score(int local, int width)
{
	char line[64];
	int x = width - 24, y = 24, left = players_time_left();

	for (int who = 0; who < 2; who++) {
		fmt(line, local == who ? "%s (you)   %d" : "%s   %d", mp_name(who), players_score(who));
		text(line, x, y, RIGHT, local == who ? YELLOW : WHITE);
		y += g_line_height + 2;
	}
	if (left >= 0) {   /* the match's time limit */
		left = (left + 59) / 60;
		fmt(line, "%d:%02d", left / 60, left % 60);
		text(line, x, y, RIGHT, left <= 30 ? RED : WHITE);
	}
}

/* The match is over: the result until everyone pressed fire (net.c) */
static void draw_over(const NetHud *h, int width, int height)
{
	char line[96];
	int x = width / 2, y = height / 5, w = players_winner();

	if (w < 0) str_cpy(line, sizeof line, "Match over: a draw!");
	else if (w == h->local_player) str_cpy(line, sizeof line, "Match over: you win!");
	else fmt(line, "Match over: %s wins!", mp_name(w));
	text(line, x, y, CENTER, w < 0 ? WHITE : w == h->local_player ? GREEN : RED);
	y += g_line_height + 4;
	fmt(line, "%s %d : %d %s", mp_name(0), players_score(0), players_score(1), mp_name(1));
	text(line, x, y, CENTER, WHITE);
	y += g_line_height + 4;
	if (h->over_ready) fmt(line, "Waiting for the others (%d of %d ready, %d s)", h->over_ready_count, h->over_players,
	                       (h->over_left_ms + 999) / 1000);
	else fmt(line, "Press fire to go back to the lobby (%d s)", (h->over_left_ms + 999) / 1000);
	text(line, x, y, CENTER, h->over_ready ? GREEN : YELLOW);
}

/* The other PC went quiet or is gone */
static void draw_connection(const NetHud *h, int width, int height)
{
	char line[96];
	const char *who = mp_name(1 - h->local_player);
	int x = width / 2, y = height / 5;

	if (h->lost) {
		fmt(line, h->lost == 2 ? "%s left the game" : "Connection lost: %s stopped answering", who);
		text(line, x, y, CENTER, RED);
		text("Their player stands still. Esc opens the menu to quit.", x, y + g_line_height + 4, CENTER, WHITE);
	} else if (h->silent_ms) {
		fmt(line, "Waiting for %s... (%d s)", who, (h->silent_ms + 999) / 1000);
		text(line, x, y, CENTER, YELLOW);
	}
}

static void ONI_FC timer_display(void *timer)
{
	NetHud h;
	void *ctx = ONgTextContext;
	int width, height;
	u32 color;

	hud_timer_display(timer);   /* Oni's timer, scaled with the HUD */
	if (!ctx || !ONgGameState || !ONrLevel_GetCurrent()) return;
	net_hud(&h);
	if (h.state == NETHUD_OFF && !(multiplayer_active() && multiplayer_map())) return;

	width = M3rScreen_Width();
	height = M3rScreen_Height();
	g_line_height = TSrFont_LineHeight(TSrContext_GetFont(ctx, 0)) * TEXT_SCALE;
	color = *(u32 *)((u8 *)ctx + 0xc);   /* the timer's color, put back after */

	if (h.state == NETHUD_LOBBY) draw_lobby(&h, width, height);
	else if (h.state == NETHUD_STARTING) text("Starting the match...", width / 2, height / 5, CENTER, WHITE);
	else draw_score(h.state == NETHUD_MATCH ? h.local_player : -1, width);
	if (h.state == NETHUD_LOBBY || h.state == NETHUD_STARTING) draw_problem(&h, width, height);
	if (h.state != NETHUD_LOBBY && h.state != NETHUD_STARTING) mp_names_draw(width, height);
	chat_draw(width, height);
	if (h.desynced) text("Out of sync with the other PC (see Arklite.log)", width / 2, 24, CENTER, RED);
	if (h.state == NETHUD_MATCH && h.over) draw_over(&h, width, height);
	else if (h.state == NETHUD_MATCH) draw_connection(&h, width, height);
	char_menu_draw(width, height);

	TSrContext_SetColor(ctx, color);
}

void mp_hud_install(void)
{
	if (!patch_call(ONcText_SpriteCall, (u32)M3rDraw_Sprite, (u32)text_sprite))
		log_msg("mp hud: text sprite call changed, the text is Oni's size");
	if (patch_call(ONcTimer_DisplayCall, (u32)ONrTimer_Display, (u32)timer_display))
		log_msg("mp hud: lobby text and score on screen");
}
