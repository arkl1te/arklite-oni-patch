/* Off since 2026-11-19 (CLOAK_FX_MENU 0 in arklite.h: O does nothing; the values the user set here
 * are invis.c's defaults). 1 brings it back as described below.
 *
 * The cloak effect's debug menu (user request, 2026-11-14): O opens a panel on the right of the
 * screen, so the player's character stays in view, with a slider and a number field for each of
 * invis.c's knobs (the cloaked player's own look: opacity, water, stencil tones, outline, color
 * fringe). Changes show at once. Preview puts the look on this PC's player while the menu is up,
 * cloaked or not, in any game (drawing only: nothing reaches the game state or the network).
 *
 * Like the F2 menu (char_menu.c) it's drawn with the HUD's text and plain rectangles (mp_hud.c's
 * console display hook, after the console, so with the HUD hidden too), turns the game's input
 * off while it's up (LIrMode_SetGameInput 0x403f70) and reads keys with GetAsyncKeyState only
 * while this Oni window has the focus. The mouse: the arrow cursor as the menus draw it; drag a
 * slider (or click on its track); click a number to type one (digits, '.', '-'; Enter keeps it,
 * Esc puts the old one back, a click elsewhere keeps it); the wheel over a row moves it by a
 * hundredth of its range (a whole step for whole numbers). Each filter's header has Up and Down:
 * the filters run in the menu's order (invis.c "The filter chain"), opacity included. Preview, Cloak
 * me (a 30 s phase cloak, off the network only: it's game state), Reset (every knob and the
 * order to the defaults) and Close at the bottom. While it's up the score and FPS corner isn't
 * drawn (the panel is there). O or Esc closes; closing saves the values in Arklite.ini
 * [cloak_fx] and writes them to the log on one line (invis_fx_save).
 *
 * O is also Oni's own second "swap weapon" key (key_config.txt: bind o to swap): the press that
 * opens the menu reaches the game first, as F2's does (the menu turns the input off after the
 * frame's update), so it may swap the weapon once. */
#include "arklite.h"

typedef struct { int x, y; } POINT;
typedef struct { int left, top, right, bottom; } RECT;
__declspec(dllimport) short WINAPI GetAsyncKeyState(int);
__declspec(dllimport) void * WINAPI GetForegroundWindow(void);
__declspec(dllimport) u32  WINAPI GetWindowThreadProcessId(void *, u32 *);
__declspec(dllimport) u32  WINAPI GetCurrentProcessId(void);
__declspec(dllimport) int  WINAPI GetCursorPos(POINT *);
__declspec(dllimport) int  WINAPI ScreenToClient(void *, POINT *);
__declspec(dllimport) int  WINAPI GetClientRect(void *, RECT *);

#define M3rScreen_Width         ((u16 (ONI_FC *)(void))0x41e0f0)
#define M3rScreen_Height        ((u16 (ONI_FC *)(void))0x41e130)
#define LIrMode_GetGameInput    ((u32 (ONI_FC *)(void))0x403f60)
#define LIrMode_SetGameInput    ((void (ONI_FC *)(u32))0x403f70)
#define ONrLevel_GetCurrent     ((u16 (ONI_FC *)(void))0x5054c0)
#define ONgGameState            (*(u8 **)0x5ece7c)
#define WMrCursor_Get           ((u8 *(ONI_FC *)(u32))0x4687d0)
#define PSrPartSpec_Draw        ((void (ONI_FC *)(void *, u32, const float *, u32, u32, u32))0x46a610)
#define WM_CURSOR_ARROW         1
#define PS_MIDDLE_MIDDLE        0x10

#define VK_LBUTTON 0x01
#define VK_BACK    0x08
#define VK_RETURN  0x0d
#define VK_ESCAPE  0x1b
#define VK_O       0x4f

#define WHITE   0xffffffffu
#define GREY    0xffb0b0b0u
#define ORANGE  0xffff9020u
#define BLUE    0xff8ab4ff
#define YELLOW  0xffffe040u

enum { BTN_PREVIEW, BTN_CLOAK, BTN_RESET, BTN_CLOSE, BUTTONS };

static int g_open, g_saved_input, g_preview = 1;
static u32 g_level;
static u8 g_down[256];
static int g_mx = -1, g_my = -1, g_in_window;
static int g_drag = -1;                  /* the knob whose slider is held */
static int g_edit = -1;                  /* the knob whose number is being typed */
static char g_text[24];
static int g_fresh;                      /* the number shown as it was: the first key typed replaces it */
static u8 *g_panel, *g_track, *g_fill, *g_knob, *g_box, *g_box_edit, *g_button;

/* From the last draw: where each knob's row, track and number box are, and the buttons */
static struct { int y, h, tx, tw, bx, bw; } g_row[FX_COUNT];
static struct { int x, y, w, h; } g_btn[BUTTONS];
static struct { int y, h, ux, dx, bw; } g_hdr[FXF_COUNT];   /* each place's header and its Up / Down buttons */
static int g_drawn;

int fx_menu_open(void) { return g_open; }

static int key(int vk, int focus)
{
	int down = focus && (GetAsyncKeyState(vk) & 0x8000) != 0, was = g_down[vk];
	g_down[vk] = (u8)down;
	return down && !was;
}

static int held(int vk, int focus) { return focus && (GetAsyncKeyState(vk) & 0x8000) != 0; }

static int allowed(void)
{
	return ONgGameState && ONrLevel_GetCurrent() && !live_menu_open() && !chat_blocking() && !char_menu_open() &&
	       !mp_name_editing() && !photo_active();
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Not its default, as far as its decimals show (a value read back from Arklite.ini differs in the
 * last float bits) */
static int changed(const InvisFx *f)
{
	float d = f->value - f->def, step = 0.5f;
	for (int k = 0; k < f->decimals; k++) step *= 0.1f;
	return d > step || d < -step;
}

/* v rounded to the knob's decimals, in its range */
static void set_value(int i, float v)
{
	InvisFx *f = invis_fx(i);
	float scale = 1;
	if (!f) return;
	for (int d = 0; d < f->decimals; d++) scale *= 10.0f;
	v = clampf(v, f->min, f->max);
	v = (float)(int)(v * scale + (v < 0 ? -0.5f : 0.5f)) / scale;
	f->value = clampf(v, f->min, f->max);
}

static void edit_end(int keep)
{
	float v;
	if (g_edit < 0) return;
	if (keep && float_parse(g_text, &v)) set_value(g_edit, v);
	g_edit = -1;
}

static void close_menu(void)
{
	if (!g_open) return;
	edit_end(1);
	g_open = 0;
	g_drag = -1;
	invis_fx_preview(0);
	LIrMode_SetGameInput(g_saved_input);
	invis_fx_save();
}

static void open_menu(void)
{
	g_open = 1;
	g_level = ONrLevel_GetCurrent();
	g_drag = g_edit = -1;
	g_drawn = 0;
	g_saved_input = LIrMode_GetGameInput();
	LIrMode_SetGameInput(0);
	invis_fx_preview(g_preview);
	menu_wheel_hud_take();   /* notches from before */
}

static int inside(int x, int y, int w, int h) { return g_mx >= x && g_mx < x + w && g_my >= y && g_my < y + h; }

/* The characters typed into the number being edited */
static void typing(int focus)
{
	static const struct { int vk; char c; } keys[] = {
		{ 0xbe, '.' }, { 0x6e, '.' }, { 0xbc, '.' }, { 0xbd, '-' }, { 0x6d, '-' },
	};
	u32 n;
	int typed = 0;
	for (int d = 0; d < 10 && !typed; d++) typed = (GetAsyncKeyState(0x30 + d) | GetAsyncKeyState(0x60 + d)) & 0x8000;
	for (u32 k = 0; k < sizeof keys / sizeof keys[0] && !typed; k++) typed = GetAsyncKeyState(keys[k].vk) & 0x8000;
	if (g_fresh && focus && (typed || (GetAsyncKeyState(VK_BACK) & 0x8000))) g_text[0] = 0;   /* (the key itself comes below) */
	if (typed || (GetAsyncKeyState(VK_BACK) & 0x8000)) g_fresh = 0;
	n = str_len(g_text);
	for (int d = 0; d < 10; d++)
		if ((key(0x30 + d, focus) | key(0x60 + d, focus)) && n + 1 < sizeof g_text) g_text[n++] = (char)('0' + d), g_text[n] = 0;
	for (u32 k = 0; k < sizeof keys / sizeof keys[0]; k++)
		if (key(keys[k].vk, focus) && n + 1 < sizeof g_text) g_text[n++] = keys[k].c, g_text[n] = 0;
	if (key(VK_BACK, focus) && n) g_text[--n] = 0;
}

/* Every frame, after the update (live_menu.c's main loop hook) */
void fx_menu_frame(void)
{
	u32 pid = 0;
	void *w = GetForegroundWindow();
	int focus, o, esc, enter, click, wheel;

	if (w) GetWindowThreadProcessId(w, &pid);
	focus = w && pid == GetCurrentProcessId();
	o = key(VK_O, focus);
	esc = key(VK_ESCAPE, focus);
	enter = key(VK_RETURN, focus);
	click = key(VK_LBUTTON, focus);
	if (!g_open) {
		if (CLOAK_FX_MENU && o && allowed() && !dev_mode_ctrl()) open_menu();   /* (developer mode: Ctrl+Shift+O is its key) */
		return;
	}
	if (!ONgGameState || !ONrLevel_GetCurrent() || ONrLevel_GetCurrent() != g_level || live_menu_open() || photo_active()) {
		close_menu();
		return;
	}
	wheel = menu_wheel_hud_take();

	g_in_window = 0;
	if (focus) {   /* the mouse, in screen pixels */
		POINT pt;
		RECT rc;
		if (GetCursorPos(&pt) && ScreenToClient(w, &pt) && GetClientRect(w, &rc) && rc.right > 0 && rc.bottom > 0) {
			g_mx = pt.x * M3rScreen_Width() / rc.right;
			g_my = pt.y * M3rScreen_Height() / rc.bottom;
			g_in_window = pt.x >= 0 && pt.y >= 0 && pt.x < rc.right && pt.y < rc.bottom;
		}
	}

	if (g_edit >= 0) {
		typing(focus);
		if (enter) { edit_end(1); return; }
		if (esc) { edit_end(0); return; }
		if (!click) return;
		edit_end(1);   /* a click elsewhere keeps it, and does what it does */
	} else {
		for (int d = 0; d < 10; d++) key(0x30 + d, focus), key(0x60 + d, focus);   /* (no stale presses for the next edit) */
		if (o || esc) {
			close_menu();
			return;
		}
	}
	if (!g_drawn) return;

	if (g_drag >= 0) {   /* the slider follows the mouse while the button is held */
		InvisFx *f = invis_fx(g_drag);
		if (!held(VK_LBUTTON, focus)) g_drag = -1;
		else if (f && g_row[g_drag].tw > 0)
			set_value(g_drag, f->min + (f->max - f->min) * clampf((float)(g_mx - g_row[g_drag].tx) / g_row[g_drag].tw, 0, 1));
		return;
	}
	for (int i = 0; i < FX_COUNT; i++) {
		InvisFx *f = invis_fx(i);
		if (!f || !g_row[i].h || !g_in_window || g_my < g_row[i].y || g_my >= g_row[i].y + g_row[i].h) continue;
		if (click && inside(g_row[i].bx, g_row[i].y, g_row[i].bw, g_row[i].h)) {
			g_edit = i;
			g_fresh = 1;
			float_text(g_text, sizeof g_text, f->value, f->decimals);
			return;
		}
		if (click && g_mx >= g_row[i].tx - mp_hud_px(8) && g_mx < g_row[i].tx + g_row[i].tw + mp_hud_px(8)) {
			g_drag = i;
			set_value(i, f->min + (f->max - f->min) * clampf((float)(g_mx - g_row[i].tx) / g_row[i].tw, 0, 1));
			return;
		}
		if (wheel) {
			float step = f->decimals ? (f->max - f->min) / 100.0f : 1.0f;
			set_value(i, f->value + step * (float)wheel);
		}
	}
	for (int k = 0; k < FXF_COUNT; k++) {   /* a filter up or down the chain */
		if (!click || !g_hdr[k].h || g_my < g_hdr[k].y || g_my >= g_hdr[k].y + g_hdr[k].h) continue;
		if (g_mx >= g_hdr[k].ux && g_mx < g_hdr[k].ux + g_hdr[k].bw) { invis_fx_move(k, -1); return; }
		if (g_mx >= g_hdr[k].dx && g_mx < g_hdr[k].dx + g_hdr[k].bw) { invis_fx_move(k, 1); return; }
	}
	if (click && inside(g_btn[BTN_PREVIEW].x, g_btn[BTN_PREVIEW].y, g_btn[BTN_PREVIEW].w, g_btn[BTN_PREVIEW].h)) {
		g_preview = !g_preview;
		invis_fx_preview(g_preview);
	} else if (click && inside(g_btn[BTN_CLOAK].x, g_btn[BTN_CLOAK].y, g_btn[BTN_CLOAK].w, g_btn[BTN_CLOAK].h)) {
		invis_cloak_me();
	} else if (click && inside(g_btn[BTN_RESET].x, g_btn[BTN_RESET].y, g_btn[BTN_RESET].w, g_btn[BTN_RESET].h)) {
		invis_fx_reset();
	} else if (click && inside(g_btn[BTN_CLOSE].x, g_btn[BTN_CLOSE].y, g_btn[BTN_CLOSE].w, g_btn[BTN_CLOSE].h)) {
		close_menu();
	}
}

/* s cut to width pixels (plain style) */
static void fit_plain(const char *s, int width, char *out, unsigned size)
{
	int n = (int)str_len(s);
	str_cpy(out, size, s);
	if (mp_hud_text_width_plain(s, -1) <= width) return;
	while (n > 1 && mp_hud_text_width_plain(s, n) + mp_hud_text_width_plain("..", -1) > width) n--;
	if ((unsigned)n + 3 > size) n = (int)size - 3;
	out[n] = '.';
	out[n + 1] = '.';
	out[n + 2] = 0;
}

static void button(int b, const char *title, int x, int y, int w, int h, int on)
{
	int hover = g_in_window && inside(x, y, w, h);
	g_btn[b].x = x, g_btn[b].y = y, g_btn[b].w = w, g_btn[b].h = h;
	hud_rect(g_button, x, y, w, h, hover ? 255 : 200);
	if (on) hud_rect(g_fill, x, y + h - mp_hud_px(3), w, mp_hud_px(3), 255);
	mp_hud_text(title, x + w / 2, y + (h - mp_hud_line_height()) / 2, MP_CENTER, hover ? YELLOW : WHITE);
}

void fx_menu_draw(int width, int height)
{
	int lh = mp_hud_line_height(), pad = lh / 3 + 1, margin = mp_hud_px(20);
	int panel_w, x0, x, y, inner, label_w, track_w, box_w, gap, rows, row_h, top;
	char line[96];

	if (!g_open) return;
	if (!g_panel) {
		g_panel = hud_solid(0x8000, "arklite_fx_panel");
		g_track = hud_solid(0x8000 | 9 << 10 | 9 << 5 | 10, "arklite_fx_track");
		g_fill = hud_solid(0x8000 | 31 << 10 | 18 << 5 | 4, "arklite_fx_fill");
		g_knob = hud_solid(0xffff, "arklite_fx_knob");
		g_box = hud_solid(0x8000 | 4 << 10 | 4 << 5 | 5, "arklite_fx_box");
		g_box_edit = hud_solid(0x8000 | 6 << 10 | 10 << 5 | 18, "arklite_fx_box_edit");
		g_button = hud_solid(0x8000 | 7 << 10 | 7 << 5 | 9, "arklite_fx_button");
	}

	/* the panel on the right, a third of the width (the character stays in the middle) */
	panel_w = width * 31 / 100;
	if (panel_w < lh * 20) panel_w = lh * 20;
	if (panel_w > width / 2) panel_w = width / 2;
	x0 = width - panel_w - margin;
	inner = panel_w - 2 * pad - 2 * mp_hud_px(6);
	gap = mp_hud_px(8);
	label_w = inner * 40 / 100;
	box_w = inner * 20 / 100;
	track_w = inner - label_w - box_w - 2 * gap;
	rows = 2 + FX_COUNT + FXF_COUNT + 3;   /* title and hint, knobs, filter headers, two rows of buttons */
	row_h = lh + 2 * pad;
	while (row_h > lh + 2 && rows * row_h + 2 * pad > height - 2 * margin) row_h--;
	top = margin;
	hud_rect(g_panel, x0, top, panel_w, rows * row_h + 2 * pad, 215);
	x = x0 + pad + mp_hud_px(6);
	y = top + pad;

	mp_hud_text("Cloak effect", x, y + (row_h - lh) / 2, MP_LEFT, WHITE);
	y += row_h;
	fit_plain("Drag, or click a number and type. O: close", inner, line, sizeof line);
	mp_hud_text_plain(line, x, y + (row_h - lh) / 2, MP_LEFT, GREY);
	y += row_h;

	for (int k = 0; k < FXF_COUNT; k++) {
		int filter = invis_fx_order(k), sbw = mp_hud_text_width("Down", -1) + 2 * pad, hy;
		/* the header: its place in the chain, its name, Up and Down */
		fmt(line, "%d. %s", k + 1, invis_fx_filter_name(filter));
		hy = y + (row_h - lh) / 2;
		mp_hud_text(line, x, hy, MP_LEFT, BLUE);
		g_hdr[k].y = y, g_hdr[k].h = row_h, g_hdr[k].bw = sbw;
		g_hdr[k].dx = x + inner - sbw;
		g_hdr[k].ux = g_hdr[k].dx - gap - sbw;
		if (k > 0) {
			int hover = g_in_window && inside(g_hdr[k].ux, y, sbw, row_h);
			hud_rect(g_button, g_hdr[k].ux, y + pad / 2, sbw, row_h - pad, hover ? 255 : 200);
			mp_hud_text_plain("Up", g_hdr[k].ux + sbw / 2, hy, MP_CENTER, hover ? YELLOW : WHITE);
		}
		if (k < FXF_COUNT - 1) {
			int hover = g_in_window && inside(g_hdr[k].dx, y, sbw, row_h);
			hud_rect(g_button, g_hdr[k].dx, y + pad / 2, sbw, row_h - pad, hover ? 255 : 200);
			mp_hud_text_plain("Down", g_hdr[k].dx + sbw / 2, hy, MP_CENTER, hover ? YELLOW : WHITE);
		}
		y += row_h;
		for (int i = 0; i < FX_COUNT; i++) {
			InvisFx *f = invis_fx(i);
			int ty, tx, kx, bx, editing = g_edit == i, hover;
			float t;
			if (!f || f->filter != filter) continue;
			hover = g_in_window && g_my >= y && g_my < y + row_h && g_mx >= x0 && g_mx < x0 + panel_w;
			fit_plain(f->label, label_w, line, sizeof line);
			mp_hud_text_plain(line, x, y + (row_h - lh) / 2, MP_LEFT, hover || g_drag == i ? YELLOW : WHITE);

			tx = x + label_w + gap;
			ty = y + row_h / 2;
			t = f->max > f->min ? clampf((f->value - f->min) / (f->max - f->min), 0, 1) : 0;
			kx = tx + (int)(t * (float)track_w + 0.5f);
			hud_rect(g_track, tx, ty - mp_hud_px(2), track_w, mp_hud_px(4) > 1 ? mp_hud_px(4) : 2, 255);
			hud_rect(g_fill, tx, ty - mp_hud_px(2), kx - tx, mp_hud_px(4) > 1 ? mp_hud_px(4) : 2, 255);
			hud_rect(g_knob, kx - mp_hud_px(3), ty - lh / 2 + 1, mp_hud_px(6) > 2 ? mp_hud_px(6) : 3, lh - 2, 255);

			bx = tx + track_w + gap;
			hud_rect(editing ? g_box_edit : g_box, bx, y + pad / 2, box_w, row_h - pad, 230);
			if (editing) {
				char shown[32];
				fmt(shown, g_fresh ? "[%s]" : "%s|", g_text);
				mp_hud_text_plain(shown, bx + box_w - mp_hud_px(5), y + (row_h - lh) / 2, MP_RIGHT, YELLOW);
			} else {
				float_text(line, sizeof line, f->value, f->decimals);
				mp_hud_text_plain(line, bx + box_w - mp_hud_px(5), y + (row_h - lh) / 2, MP_RIGHT,
				                  changed(f) ? ORANGE : WHITE);
			}
			g_row[i].y = y, g_row[i].h = row_h, g_row[i].tx = tx, g_row[i].tw = track_w, g_row[i].bx = bx, g_row[i].bw = box_w;
			y += row_h;
		}
	}

	y += row_h / 2;
	{
		int bw = (inner - gap) / 2;
		button(BTN_PREVIEW, g_preview ? "Preview: on" : "Preview: off", x, y, bw, row_h, g_preview);
		button(BTN_CLOAK, net_active() ? "Cloak me (offline)" : "Cloak me", x + bw + gap, y, bw, row_h, 0);
		y += row_h + pad;
		button(BTN_RESET, "Reset", x, y, bw, row_h, 0);
		button(BTN_CLOSE, "Close", x + bw + gap, y, bw, row_h, 0);
	}
	g_drawn = 1;

	if (g_in_window) {   /* the menus' arrow, as WMrCursor_Draw (0x468770) draws it */
		const u8 *cursor = WMrCursor_Get(WM_CURSOR_ARROW);
		float at[4] = { (float)(g_mx - 16), (float)(g_my - 16), 0.5f, 2.0f };
		if (cursor && *(void **)(cursor + 4)) PSrPartSpec_Draw(*(void **)(cursor + 4), PS_MIDDLE_MIDDLE, at, 32, 32, 255);
	}
}
