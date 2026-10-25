/* Options > More...: a second page of the Options dialog (0x98, proc 0x4d25c0, wrapped in volume.c).
 *
 * A "More..." button left of Close switches the dialog to Arklite's page and becomes "Back".
 * There is no dialog resource for the page: its controls are cloned from the stock ones and
 * created hidden; switching pages hides one set and shows the other. The Graphics and Sound
 * frames and the background stay; the title reads "More options". Close works on both pages,
 * and Esc on our page goes back to the stock page instead of closing the dialog.
 *
 *   Display           HUD Scale (50..300%, hud_scale.c) and the pause menu's black background
 *                     opacity (0..100%, drawn by arklite.c).
 *   When Game Pauses  three checkboxes acting as one choice (the stock radio buttons use the
 *                     grey g206 art, so Oni's own checkboxes look right here):
 *                     keep the level's music, play the main theme (the original game), silence.
 *
 * Values are saved in Arklite.ini, [options]. */
#include "arklite.h"

/* ---- Settings ------------------------------------------------------------------------------ */

#define cHudScaleMin  50
#define cHudScaleMax  300
#define cHudScaleStep 5
/* The HUD Scale slider counts steps (0..50), not percent: Oni's slider moves by whole pixels
 * over its travel (width - 4 - 4 px track caps - 9 px thumb, art psui_oniUI), so 250 percent
 * on a 64 px slider (47 px of travel) skipped values, 100 among them. 72 px gives 55 px of
 * travel for the 51 steps, so every step has a pixel. */
#define cHudScaleSteps ((cHudScaleMax - cHudScaleMin) / cHudScaleStep)

static int g_hudScale = 100;            /* percent */
static int g_pauseBackground = 33;      /* percent; 33 was Arklite's fixed value */
static int g_pauseMusic = PAUSE_MUSIC_KEEP;

static const char *const g_musicNames[] = { "keep", "theme", "silence" };

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void settings_load(void)
{
	char music[16];

	g_hudScale = clamp((int)GetPrivateProfileIntA("options", "hud_scale", 100, ini_path()), cHudScaleMin, cHudScaleMax);
	g_hudScale = cHudScaleMin + (g_hudScale - cHudScaleMin + cHudScaleStep / 2) / cHudScaleStep * cHudScaleStep;
	g_pauseBackground = clamp((int)GetPrivateProfileIntA("options", "pause_background", 33, ini_path()), 0, 100);
	GetPrivateProfileStringA("options", "pause_music", "keep", music, sizeof music, ini_path());
	g_pauseMusic = PAUSE_MUSIC_KEEP;
	for (int i = 0; i < 3; i++)
		if (str_ieq_n(music, g_musicNames[i], sizeof music)) g_pauseMusic = i;
	log_msg("options: hud_scale %d, pause_background %d, pause_music %s",
	        g_hudScale, g_pauseBackground, g_musicNames[g_pauseMusic]);
}

static void settings_save_int(const char *key, int v)
{
	char value[16];
	fmt(value, "%d", v);
	WritePrivateProfileStringA("options", key, value, ini_path());
}

int   pause_music_mode(void)       { return g_pauseMusic; }
u32   pause_background_alpha(void) { return (u32)(g_pauseBackground * 255 + 50) / 100; }
float hud_scale(void)              { return g_hudScale * 0.01f; }

/* ---- Page ---------------------------------------------------------------------------------- */

enum {
	ID_MORE = 113,
	ID_HUD_SCALE = 120, ID_PAUSE_BACKGROUND,
	ID_MUSIC_KEEP, ID_MUSIC_THEME, ID_MUSIC_SILENCE,
};
#define ID_CLOSE    2
#define ID_SOUND    102   /* stock Overall Volume slider, Sound Volume after volume.c */
#define ID_SUBTITLE 107   /* stock checkbox */

#define MAX_WINDOWS 48
static WMtWindow *g_stock[MAX_WINDOWS];   /* stock controls hidden on our page */
static WMtWindow *g_ours[MAX_WINDOWS];    /* our controls, hidden on the stock page */
static u32 g_nstock, g_nours;
static WMtWindow *g_dialog, *g_more, *g_hudValue, *g_backgroundValue;
/* New controls go right after this one in the child list. Earlier children are drawn over
 * later ones, and the frames (Title windows) have a translucent dark fill, so a control placed
 * after a frame in the list is drawn under it and looks dimmed. "Quality:" precedes every frame. */
static WMtWindow *g_anchor;
static int g_onOurPage, g_initializing;

/* Kept on both pages: the background picture, the Graphics and Sound frames (x 5, width 300,
 * at y 50 and 175) and the Close button. */
static int keep_on_both_pages(WMtWindow *w)
{
	if (WIN_ID(w) == ID_CLOSE) return 1;
	if (WIN_X(w) == 0 && WIN_Y(w) == 0) return 1;              /* pict_options_background */
	if (WIN_X(w) == 5 && WIN_W(w) == 300 && (WIN_Y(w) == 50 || WIN_Y(w) == 175)) return 1;
	return 0;
}

static WMtWindow *find_child(WMtWindow *dialog, int x, int y)
{
	for (WMtWindow *w = WIN_FIRSTCHILD(dialog); w; w = WIN_NEXT(w))
		if (WIN_X(w) == x && WIN_Y(w) == y) return w;
	return 0;
}

/* A hidden copy of src (its class, flags, style and font) with a new text, id and frame,
 * placed after g_anchor in the drawing order. */
static WMtWindow *add(WMtWindow *src, const char *text, u32 id, int x, int y, int w, int h)
{
	WMtWindow *win;

	if (!src || g_nours >= MAX_WINDOWS) return 0;
	win = WMrWindow_New(WIN_CLASS(src), text, WIN_FLAGS(src) & ~1u, WIN_STYLE(src), id, x, y, w, h, g_dialog, 0);
	if (!win) {
		log_msg("options: WMrWindow_New failed for \"%s\"", text);
		return 0;
	}
	WMrWindow_SetFontInfo(win, WIN_FONTINFO(src));
	win_insert_after(win, g_anchor);
	g_ours[g_nours++] = win;
	return win;
}

static void show_page(int ours)
{
	for (u32 i = 0; i < g_nstock; i++) WMrWindow_SetVisible(g_stock[i], !ours);
	for (u32 i = 0; i < g_nours; i++)  WMrWindow_SetVisible(g_ours[i], ours);
	if (g_more) WMrWindow_SetTitle(g_more, ours ? "Back" : "More...", 0x100);
	g_onOurPage = ours;
}

static void show_percent(WMtWindow *label, int v)
{
	char text[16];
	if (!label) return;
	fmt(text, "%d%%", v);
	WMrWindow_SetTitle(label, text, 0x100);
}

static void check_music(void)
{
	for (u32 id = ID_MUSIC_KEEP; id <= ID_MUSIC_SILENCE; id++) {
		WMtWindow *box = WMrDialog_GetItemByID(g_dialog, id);
		if (box) WMrCheckBox_SetCheck(box, (int)(id - ID_MUSIC_KEEP) == g_pauseMusic);
	}
}

void more_options_init(WMtWindow *dialog)
{
	WMtWindow *close = WMrDialog_GetItemByID(dialog, ID_CLOSE);
	WMtWindow *slider = WMrDialog_GetItemByID(dialog, ID_SOUND);
	WMtWindow *checkbox = WMrDialog_GetItemByID(dialog, ID_SUBTITLE);
	WMtWindow *rowLabel = find_child(dialog, 30, 65);      /* "Quality:" */
	WMtWindow *header = find_child(dialog, 50, 35);        /* "Graphics" (blue) */
	WMtWindow *title = find_child(dialog, 5, 10);          /* "Options", 70 px wide */
	WMtWindow *hud, *background;

	g_dialog = dialog;
	g_anchor = rowLabel;
	g_nstock = g_nours = 0;
	g_more = g_hudValue = g_backgroundValue = 0;
	g_onOurPage = 0;
	if (!close || !slider || !checkbox || !rowLabel || !header || !title) {
		log_msg("options: stock controls not found, no More... page");
		return;
	}

	for (WMtWindow *w = WIN_FIRSTCHILD(dialog); w && g_nstock < MAX_WINDOWS; w = WIN_NEXT(w))
		if (WIN_VISIBLE(w) && !keep_on_both_pages(w)) g_stock[g_nstock++] = w;

	g_initializing = 1;

	/* The stock title is too narrow to rename, so ours replaces it on this page. */
	add(title, "More options", 0, 5, 10, 200, 20);

	/* Display: the Graphics frame (x 5..305, y 50..150; its border art is several px thick and
	 * the stock controls end by x 280). "Pause Background:" is about 125 px in the dialog font
	 * and wraps in a 130 px label; "100%" is about 36 px. Everything ends by x 285. */
	add(header, "Display", 0, 50, 35, 150, 20);
	add(rowLabel, "HUD Scale:", 0, 30, 65, 136, 20);
	hud = add(slider, "hud scale", ID_HUD_SCALE, 168, 65, 72, 20);
	g_hudValue = add(rowLabel, "", 0, 241, 65, 44, 20);
	add(rowLabel, "Pause Background:", 0, 30, 90, 136, 20);
	background = add(slider, "pause background", ID_PAUSE_BACKGROUND, 168, 90, 72, 20);
	g_backgroundValue = add(rowLabel, "", 0, 241, 90, 44, 20);

	/* When Game Pauses: the Sound frame (y 175..275) */
	add(header, "When Game Pauses", 0, 50, 160, 200, 20);
	add(checkbox, "Keep playing level music", ID_MUSIC_KEEP, 30, 190, 270, 20);
	add(checkbox, "Play Main Theme (original)", ID_MUSIC_THEME, 30, 215, 270, 20);
	add(checkbox, "Silence", ID_MUSIC_SILENCE, 30, 240, 270, 20);

	if (hud) {
		WMrSlider_SetRange(hud, 0, cHudScaleSteps);
		WMrSlider_SetValue(hud, (g_hudScale - cHudScaleMin) / cHudScaleStep);
	}
	if (background) { WMrSlider_SetRange(background, 0, 100); WMrSlider_SetValue(background, g_pauseBackground); }
	show_percent(g_hudValue, g_hudScale);
	show_percent(g_backgroundValue, g_pauseBackground);
	check_music();

	/* "More..." left of Close, same size */
	g_more = WMrWindow_New(WIN_CLASS(close), "More...", WIN_FLAGS(close), WIN_STYLE(close), ID_MORE,
	                       WIN_X(close) - WIN_W(close) - 10, WIN_Y(close), WIN_W(close), WIN_H(close), dialog, 0);
	if (g_more) {
		WMrWindow_SetFontInfo(g_more, WIN_FONTINFO(close));
		win_insert_after(g_more, close);
	} else {
		log_msg("options: WMrWindow_New failed for More...");
	}

	g_initializing = 0;
	show_page(0);
}

int more_options_command(WMtWindow *dialog, u32 id, u32 notify)
{
	if (dialog != g_dialog || id < ID_MORE || id > ID_MUSIC_SILENCE) return 0;
	if (g_initializing || notify != WMcNotify_Click) return 1;

	if (id == ID_MORE) {
		show_page(!g_onOurPage);
	} else if (id == ID_HUD_SCALE || id == ID_PAUSE_BACKGROUND) {
		WMtWindow *slider = WMrDialog_GetItemByID(dialog, id);
		int v = slider ? WMrSlider_GetValue(slider) : 0;

		if (!slider) return 1;
		if (id == ID_HUD_SCALE) {
			v = cHudScaleMin + clamp(v, 0, cHudScaleSteps) * cHudScaleStep;
			if (v == g_hudScale) return 1;
			g_hudScale = v;
			settings_save_int("hud_scale", v);
			show_percent(g_hudValue, v);
		} else {
			v = clamp(v, 0, 100);
			if (v == g_pauseBackground) return 1;
			g_pauseBackground = v;
			settings_save_int("pause_background", v);
			show_percent(g_backgroundValue, v);
		}
	} else {
		/* The click toggled the box; whichever box was clicked becomes the one choice. */
		g_pauseMusic = (int)(id - ID_MUSIC_KEEP);
		WritePrivateProfileStringA("options", "pause_music", g_musicNames[g_pauseMusic], ini_path());
		check_music();
	}
	return 1;
}

int more_options_cancel(WMtWindow *dialog)
{
	if (dialog != g_dialog || !g_onOurPage) return 0;
	show_page(0);
	return 1;
}

void more_options_install(void)
{
	settings_load();
}
