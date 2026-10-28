/* The F1 screen in multiplayer games: a Scoreboard tab, and the match's goal on the Objective tab.
 *
 * F1 opens Oni's pause screen (dialog 0x46, "Pause Screen", 640x480; 0x5018c0 runs it from the
 * main loop when gs+0x138 is set). In a multiplayer game it runs live (live_menu.c) and net.c
 * sets that flag itself, since the F1 action bit is kept out of the shared input. Its proc
 * (0x501990, pushed at 0x501932) is wrapped here:
 *   - msg 0x3d (init): after the stock init (Objective page, radio 101..105 checked by 0x468bb0,
 *     prev/next 106/107 and the sub area 111 with its outline 120 shown), a Scoreboard button
 *     (id 100, a copy of Objective) goes where Objective was (25,15) and the five stock tab
 *     buttons move 60 px down (Help ends at 355; the sub area's buttons start at x 165). The
 *     Scoreboard tab is the one shown first.
 *   - msg 0x17 (command): 100 shows the scoreboard; 101..105 leave it (the stock proc then shows
 *     its page and puts back the prev/next buttons and the sub area); 200 + player toggles that
 *     player's Mute box.
 *   - msg 0x36 (owner draw, p2 = {draw context, window, id u16}): main area 110 (175,30 310x260,
 *     coordinates from its top left) draws the scoreboard while it's up. On the Objective page
 *     the stock text (the level's objectives, often none on a multiplayer map: *0x5ecfb0 counts
 *     them) is drawn first, then the match's goal in one sentence, under the objectives at the
 *     area's bottom, or at its top when there are none.
 * The page state is the dialog's data, 0x5ed0f8 (+0 the page, 1..5; +0x10 the objectives).
 *
 * Scoreboard: Player, Score, Ping (the last round trip, match only) and a Mute box (chat.c) for
 * every other player of a network game; under it the time left, the score limit, and the result
 * once the match is over. This PC's player in white, the others orange. */
#include "arklite.h"

#define ONcHelp_ProcPush        0x501932   /* push 0x501990 */
#define ONrHelp_Proc            0x501990
#define WMrDialog_CheckRadio    ((void (ONI_FC *)(WMtWindow *, u32, u32, u32))0x468bb0)   /* first, last, checked */
#define DCrDrawText             ((void (ONI_FC *)(void *, const char *, u32, short *))0x469f40)
#define DCrSetTextColor         ((void (ONI_FC *)(u32))0x46a5c0)
#define DCrSetTextAlign         ((void (ONI_FC *)(u32))0x46a5b0)
#define DCrSetFont              ((void (ONI_FC *)(const void *))0x46a570)
#define DCrFont_Ascent          ((u32 (ONI_FC *)(void))0x46a350)
#define ONgHelpPage             (*(u32 *)0x5ed0f8)
#define ONgHelpObjectives       (*(void **)(0x5ed0f8 + 0x10))
#define ONgObjectiveCount       (*(short *)0x5ecfb0)

#define WMcMessage_Destroy      0x13
#define WMcMessage_OwnerDraw    0x36
enum { ID_BOARD = 100, ID_OBJECTIVE, ID_ITEMS, ID_WEAPONS, ID_DIARY, ID_HELP, ID_MAIN_PREV, ID_MAIN_NEXT,
       ID_MAIN_AREA = 110, ID_SUB_AREA, ID_SUB_PREV, ID_SUB_NEXT, ID_SUB_OUTLINE = 120, ID_MUTE = 200 };
#define TAB_STEP        60
#define AREA_X          175        /* the main area, in the dialog */
#define AREA_Y          30
#define AREA_W          310
#define AREA_H          260
#define ROW_H           22
#define ROWS_Y          24         /* the first player's row, under the header */
#define COL_SCORE       195        /* right edges */
#define COL_PING        245
#define COL_MUTE        255        /* the Mute box's left */
#define MUTE_W          55
#define WHITE           0xffffffffu

static WMtDialogProc g_stock;
static int g_board;                /* the Scoreboard tab is up */
static WMtWindow *g_mute[2];

static int board_wanted(void)
{
	NetHud h;
	net_hud(&h);
	return h.state != NETHUD_OFF || (multiplayer_active() && multiplayer_map());
}

/* The players listed: this PC's, and the other one when it's there */
static int listed(const NetHud *h, int who)
{
	return h->state == NETHUD_OFF || who == h->local_player || h->peer_present;
}

/* The match's goal in one sentence */
void mp_board_goal(char *out, u32 minutes, int score)
{
	char time[32];
	fmt(time, minutes == 1 ? "the minute" : "the %d minutes", minutes);
	if (score > 0 && minutes)
		fmt(out, "Score %d points to win, or have the most points when %s run out.", score, time);
	else if (score > 0)
		fmt(out, "The first player to score %d points wins.", score);
	else if (score < 0 && minutes)
		fmt(out, "Take your score down to %d to win (every suicide costs a point), or have the fewest points when %s run out.",
		    score, time);
	else if (score < 0)
		fmt(out, "The first player to take their score down to %d wins: every suicide costs a point.", score);
	else if (minutes)
		fmt(out, "Have the most points when %s run out: a kill scores a point, a suicide costs one.", time);
	else
		fmt(out, "A kill scores a point and a suicide costs one; there's no score or time limit.");
}

/* ---- Drawing in the main area --------------------------------------------------------------- */

static void *g_dc;
static int g_ascent;

static void text_at(const char *s, int x, int y, int align, u32 color, int width)
{
	char fit[128];
	short point[2];

	ui_fit(fit, sizeof fit, s, width);
	if (align == UI_RIGHT) x -= ui_text_width(fit);
	else if (align == UI_CENTER) x -= ui_text_width(fit) / 2;
	point[0] = (short)x;
	point[1] = (short)(y + g_ascent);
	DCrSetTextColor(color);
	DCrDrawText(g_dc, fit, 0, point);
}

/* s wrapped to the area's width; returns the lines drawn (or that would be, with draw 0) */
static int text_wrapped(const char *s, int y, u32 color, int draw)
{
	char line[128];
	int lines = 0;

	while (*s) {
		int n = 0, cut = 0;
		while (s[n] && n < (int)sizeof line - 1) {
			line[n] = s[n];
			line[n + 1] = 0;
			if (ui_text_width(line) > AREA_W) break;
			if (s[n] == ' ') cut = n;
			n++;
		}
		if (s[n] && n < (int)sizeof line - 1 && cut) n = cut;   /* back to the last space */
		line[n] = 0;
		if (draw) text_at(line, 0, y + lines * ROW_H, UI_LEFT, UI_ORANGE, AREA_W);
		lines++;
		s += n;
		while (*s == ' ') s++;
	}
	return lines;
}

static void draw_board(void)
{
	NetHud h;
	char line[96];
	int row = 0, y;

	net_hud(&h);
	text_at("Player", 0, 0, UI_LEFT, UI_BLUE, 150);
	text_at("Score", COL_SCORE, 0, UI_RIGHT, UI_BLUE, 60);
	if (h.state != NETHUD_OFF) {
		text_at("Ping", COL_PING, 0, UI_RIGHT, UI_BLUE, 45);
		text_at("Chat", COL_MUTE, 0, UI_LEFT, UI_BLUE, AREA_W - COL_MUTE);
	}
	for (int who = 0; who < 2; who++) {
		WMtWindow *box = g_mute[who];
		int show = 0;
		if (listed(&h, who)) {
			u32 color = h.state != NETHUD_OFF && who == h.local_player ? WHITE : UI_ORANGE;
			y = ROWS_Y + row * ROW_H;
			text_at(mp_name(who), 0, y, UI_LEFT, color, COL_SCORE - 60);
			fmt(line, "%d", players_score(who));
			text_at(line, COL_SCORE, y, UI_RIGHT, color, 60);
			if (h.state != NETHUD_OFF) {
				if (who != h.local_player && h.ping_ms >= 0) fmt(line, "%d", h.ping_ms);
				else str_cpy(line, sizeof line, "-");
				text_at(line, COL_PING, y, UI_RIGHT, color, 45);
				if (box && who != h.local_player) {
					show = 1;
					ui_set_frame(box, AREA_X + COL_MUTE, AREA_Y + y, MUTE_W, 20);
					WMrCheckBox_SetCheck(box, chat_muted(who));
				}
			}
			row++;
		}
		if (box && (WIN_VISIBLE(box) != 0) != show) WMrWindow_SetVisible(box, show);
	}

	y = ROWS_Y + 2 * ROW_H + 14;
	if (h.state == NETHUD_LOBBY || h.state == NETHUD_STARTING) {
		text_wrapped("Warm-up: the match starts when enough players are ready (F3).", y, WHITE, 1);
		return;
	}
	if (h.state == NETHUD_OFF) {
		text_at("No score or time limit on one PC.", 0, y, UI_LEFT, UI_ORANGE, AREA_W);
		return;
	}
	if (h.over) {
		int w = players_winner();
		if (w < 0) str_cpy(line, sizeof line, "Match over: a draw!");
		else fmt(line, "Match over: %s wins!", mp_name(w));
		text_at(line, 0, y, UI_LEFT, WHITE, AREA_W);
		y += ROW_H;
	}
	{
		int left = players_time_left();
		if (left < 0) str_cpy(line, sizeof line, "Time left: no limit");
		else fmt(line, "Time left: %d:%02d", (left + 59) / 60 / 60, (left + 59) / 60 % 60);
	}
	text_at(line, 0, y, UI_LEFT, UI_ORANGE, AREA_W);
	y += ROW_H;
	if (!h.score_limit) str_cpy(line, sizeof line, "Score limit: none");
	else fmt(line, h.score_limit < 0 ? "Score limit: %d (by suicides)" : "Score limit: %d", h.score_limit);
	text_at(line, 0, y, UI_LEFT, UI_ORANGE, AREA_W);
}

static void draw_goal(void)
{
	NetHud h;
	char goal[192];
	int lines, y = 0;

	net_hud(&h);
	if (h.state == NETHUD_OFF) return;   /* two players on one PC: no limits */
	mp_board_goal(goal, h.time_limit, h.score_limit);
	lines = text_wrapped(goal, 0, UI_ORANGE, 0);
	if (ONgObjectiveCount && ONgHelpObjectives) y = AREA_H - lines * ROW_H;   /* under the level's own */
	text_wrapped(goal, y, UI_ORANGE, 1);
}

/* ---- The dialog ----------------------------------------------------------------------------- */

/* The stock pages' Previous/Next buttons and sub area; the stock proc shows them again with its page */
static void hide_stock_page_parts(WMtWindow *dialog)
{
	static const u32 ids[] = { ID_MAIN_PREV, ID_MAIN_NEXT, ID_SUB_AREA, ID_SUB_OUTLINE, ID_SUB_PREV, ID_SUB_NEXT };
	for (u32 i = 0; i < sizeof ids / sizeof *ids; i++) {
		WMtWindow *w = WMrDialog_GetItemByID(dialog, ids[i]);
		if (w) WMrWindow_SetVisible(w, 0);
	}
}

static void board_show(WMtWindow *dialog)
{
	g_board = 1;
	WMrDialog_CheckRadio(dialog, ID_BOARD, ID_HELP, ID_BOARD);
	hide_stock_page_parts(dialog);
}

static void board_init(WMtWindow *dialog)
{
	WMtWindow *objective = WMrDialog_GetItemByID(dialog, ID_OBJECTIVE), *w;

	if (!objective || WIN_Y(objective) != 15) {
		log_msg("F1 screen: the Objective button isn't where it was, no Scoreboard tab");
		return;
	}
	for (u32 id = ID_OBJECTIVE; id <= ID_HELP; id++)
		if ((w = WMrDialog_GetItemByID(dialog, id))) ui_set_frame(w, WIN_X(w), WIN_Y(w) + TAB_STEP, WIN_W(w), WIN_H(w));
	/* after Objective, which comes before the frames in the child list (a window after a frame is
	 * drawn under its dark fill) */
	ui_add(dialog, objective, objective, 0, ~0u, "Scoreboard", ID_BOARD, 25, 15, 130, 40);
	for (int who = 0; who < 2; who++) {
		g_mute[who] = ui_add(dialog, objective, objective, WMcClass_CheckBox, 0x10000, "Mute", ID_MUTE + who,
		                     AREA_X + COL_MUTE, AREA_Y + ROWS_Y, MUTE_W, 20);
		if (g_mute[who]) WMrWindow_SetVisible(g_mute[who], 0);
	}
	board_show(dialog);
}

static void mute_hide(void)
{
	for (int who = 0; who < 2; who++)
		if (g_mute[who]) WMrWindow_SetVisible(g_mute[who], 0);
}

static u8 ONI_FC help_proc(WMtWindow *dialog, u32 msg, u32 p1, u32 p2)
{
	u32 id = p1 & 0xffff, code = p1 >> 16;
	u8 r;

	if (msg == WMcMessage_InitDialog) {
		g_board = 0;
		g_mute[0] = g_mute[1] = 0;
		r = g_stock(dialog, msg, p1, p2);
		if (board_wanted()) board_init(dialog);
		return r;
	}
	if (msg == WMcMessage_Destroy) {
		g_board = 0;
		g_mute[0] = g_mute[1] = 0;
		return g_stock(dialog, msg, p1, p2);
	}
	if (msg == WMcMessage_Command && code == WMcNotify_Click) {
		if (id == ID_BOARD) {
			board_show(dialog);
			return 1;
		}
		if (id >= ID_MUTE && id < ID_MUTE + 2) {
			chat_set_muted((int)(id - ID_MUTE), !chat_muted((int)(id - ID_MUTE)));
			return 1;
		}
		if (id >= ID_OBJECTIVE && id <= ID_HELP && g_board) {
			g_board = 0;
			mute_hide();
			r = g_stock(dialog, msg, p1, p2);
			WMrDialog_CheckRadio(dialog, ID_BOARD, ID_HELP, id);
			return r;
		}
	}
	if (msg == WMcMessage_OwnerDraw && p2 && *(const u16 *)(p2 + 8) == ID_MAIN_AREA && board_wanted()) {
		WMtWindow *area = *(WMtWindow *const *)(p2 + 4);
		if (g_board) {
			r = 1;
		} else {
			r = g_stock(dialog, msg, p1, p2);
			if (ONgHelpPage != 1) return r;
		}
		g_dc = *(void *const *)p2;
		DCrSetFont(WIN_FONTINFO(area));
		DCrSetTextAlign(1);
		g_ascent = (int)(DCrFont_Ascent() & 0xffff);
		if (g_board) draw_board();
		else draw_goal();
		return r;
	}
	return g_stock(dialog, msg, p1, p2);
}

void mp_board_install(void)
{
	g_stock = (WMtDialogProc)ONrHelp_Proc;
	if (patch_push_imm32(ONcHelp_ProcPush, ONrHelp_Proc, (u32)help_proc))
		log_msg("F1 screen: Scoreboard tab and the match's goal in multiplayer games");
}
