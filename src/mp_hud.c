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
 * Each line gets a black shadow 1 pixel down and right.
 *
 * Lobby: "Press F3 to be ready" in red, or "Press F3 to be unready" in green, and under it who
 * is ready or the countdown, until the match starts. Match (or a two-player game on one PC):
 * the score in the top right corner, this PC's player in yellow. */
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

#define ONrTimer_Display    ((void (ONI_FC *)(void *))0x4fd500)
#define ONcTimer_DisplayCall 0x50061f

#define RED     0xffff4040u
#define GREEN   0xff40ff40u
#define WHITE   0xffffffffu
#define YELLOW  0xffffe040u
#define SHADOW  0xff000000u

static int g_line_height;

enum { LEFT, CENTER, RIGHT };

/* One line of text; x is its left, center or right edge, y the top of the line */
static void text(const char *s, int x, int y, int align, u32 color)
{
	void *ctx = ONgTextContext;
	short rect[4] = { 0 }, at[2];
	int w;

	if (!ctx) return;
	TSrContext_Measure(ctx, s, rect);
	w = rect[2] - rect[0];
	if (align == CENTER) x -= w / 2;
	else if (align == RIGHT) x -= w;
	at[0] = (short)(x + 1);
	at[1] = (short)(y + g_line_height + 1);
	TSrContext_SetColor(ctx, SHADOW);
	TSrContext_Draw(ctx, s, 0xff, 0, at);
	at[0]--;
	at[1]--;
	TSrContext_SetColor(ctx, color);
	TSrContext_Draw(ctx, s, 0xff, 0, at);
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

static void draw_score(int local, int width)
{
	char line[64];
	int x = width - 16, y = 16;

	for (int who = 0; who < 2; who++) {
		fmt(line, local == who ? "Player %d (you)   %d" : "Player %d   %d", who + 1, players_score(who));
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

	ONrTimer_Display(timer);
	if (!ctx || !ONgGameState || !ONrLevel_GetCurrent()) return;
	net_hud(&h);
	if (h.state == NETHUD_OFF && !(multiplayer_active() && multiplayer_map())) return;

	width = M3rScreen_Width();
	height = M3rScreen_Height();
	g_line_height = TSrFont_LineHeight(TSrContext_GetFont(ctx, 0));
	color = *(u32 *)((u8 *)ctx + 0xc);   /* the timer's color, put back after */

	if (h.state == NETHUD_LOBBY) draw_lobby(&h, width, height);
	else if (h.state == NETHUD_STARTING) text("Starting the match...", width / 2, height / 5, CENTER, WHITE);
	else draw_score(h.state == NETHUD_MATCH ? h.local_player : -1, width);

	TSrContext_SetColor(ctx, color);
}

void mp_hud_install(void)
{
	if (patch_call(ONcTimer_DisplayCall, (u32)ONrTimer_Display, (u32)timer_display))
		log_msg("mp hud: lobby text and score on screen");
}
