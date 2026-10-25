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
 * top right corner with the players' names (mp_names.c), this PC's player in yellow, the name of
 * the player this PC's player faces over their head, and "Out of sync" when a state check failed. The F2 character menu
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
		TSrContext_Draw(ctx, s, 0xff, 0, at);
	}
	g_scaling = 0;
}

/* For char_menu.c, while it draws from the HUD below */
void mp_hud_text(const char *s, int x, int y, int align, u32 color) { text(s, x, y, align, color); }
int  mp_hud_line_height(void) { return g_line_height; }

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
	int x = width - 24, y = 24;

	for (int who = 0; who < 2; who++) {
		fmt(line, local == who ? "%s (you)   %d" : "%s   %d", mp_name(who), players_score(who));
		text(line, x, y, RIGHT, local == who ? YELLOW : WHITE);
		y += g_line_height + 2;
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
	if (h.desynced) text("Out of sync with the other PC (see Arklite.log)", width / 2, 24, CENTER, RED);
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
