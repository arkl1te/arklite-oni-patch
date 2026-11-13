/* Options > More...: a second page of the Options dialog (0x98, proc 0x4d25c0, wrapped in volume.c).
 *
 * A "More..." button left of Close switches the dialog to Arklite's page and becomes "Back".
 * There is no dialog resource for the page: its controls are cloned from the stock ones and
 * created hidden; switching pages hides one set and shows the other. The Graphics and Sound
 * frames and the background stay; the title reads "More options". Close works on both pages,
 * and Esc on our page goes back to the stock page instead of closing the dialog.
 *
 *   Display           HUD Scale (50..300%, hud_scale.c), HUD Centering (0%: the rings in the
 *                     corners, 100%: side by side in the middle, hud_scale.c; Arklite's corner
 *                     text moves with them; the default, 4:3, puts them where a 4:3 screen has
 *                     them, at any resolution: the slider's step nearest that reads "4:3", and
 *                     moving onto it picks 4:3 again), the pause menu's black background opacity (0..100%,
 *                     drawn by arklite.c) and the FPS counter (mp_hud.c). Four rows: on this page
 *                     the Graphics frame is 25 px taller, and the frames under it, Close and Back
 *                     are 25 px lower (the dialog is 480 px tall; Close ends at 470).
 *   When Game Pauses  three checkboxes acting as one choice (the stock radio buttons use the
 *                     grey g206 art, so Oni's own checkboxes look right here):
 *                     keep the level's music, play the main theme (the original game), silence.
 *   Mouse             Sensitivity: a slider (0.10..4.00 in steps of 0.05) and a field beside it
 *                     for any value from 0.001 to 99.99. 1.000 is Oni's own speed. It scales
 *                     every mouse movement Oni's input takes in (LIrActionBuffer_Add, 0x403b30,
 *                     inputs 5 and 6), whichever way it was read: DirectInput, the Windows cursor,
 *                     or Daodan's raw input (whose own [windows] mousesensitivity in daodan.ini
 *                     multiplies on top). Menus use the Windows cursor and aren't affected.
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
#define cHudCenterStep 5                /* 21 steps on a 72 px slider (55 px of travel) */
#define cHudCenter43   (-1)             /* g_hudCenter: 4:3, the default (hud_scale.c) */
static int g_hudCenter = cHudCenter43;  /* percent: 0 in the corners, 100 side by side */
static int g_pauseBackground = 33;      /* percent; 33 was Arklite's fixed value */
static int g_pauseMusic = PAUSE_MUSIC_KEEP;
static int g_showFps;

/* Mouse sensitivity in thousandths: the field takes 0.001..99.99 (5 characters fit its box), the
 * slider 0.10..4.00 in steps of 0.05 (78 steps on a 110 px slider: 93 px of travel). From 10 up
 * the field shows two decimals, so the value is kept in hundredths there. */
#define cSensDefault   1000
#define cSensMin       1
#define cSensMax       99990
#define cSensSlideMin  100
#define cSensSlideStep 50
#define cSensSteps     78
static int g_sens = cSensDefault;
static float g_sensFactor = 1.0f;

static const char *const g_musicNames[] = { "keep", "theme", "silence" };

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int sens_tidy(int t)
{
	t = clamp(t, cSensMin, cSensMax);
	return t < 10000 ? t : (t + 5) / 10 * 10;
}

static void sens_text(char *out, int t)   /* 1.000, 0.250, 12.50 */
{
	if (t < 10000) fmt(out, "%d.%03d", t / 1000, t % 1000);
	else fmt(out, "%d.%02d", t / 1000, t % 1000 / 10);
}

static void sens_set(int t)
{
	g_sens = sens_tidy(t);
	g_sensFactor = g_sens / 1000.0f;
}

static const char *center_text(void)   /* the ini's value and the Options label */
{
	static char text[8];
	if (g_hudCenter == cHudCenter43) return "4:3";
	fmt(text, "%d%%", g_hudCenter);
	return text;
}

static void settings_load(void)
{
	char music[16];

	g_hudScale = clamp((int)GetPrivateProfileIntA("options", "hud_scale", 100, ini_path()), cHudScaleMin, cHudScaleMax);
	g_hudScale = cHudScaleMin + (g_hudScale - cHudScaleMin + cHudScaleStep / 2) / cHudScaleStep * cHudScaleStep;
	{   /* "4:3" (also when missing) or a percentage */
		char text[16];
		int v = 0, digits = 0;
		GetPrivateProfileStringA("options", "hud_center", "4:3", text, sizeof text, ini_path());
		for (const char *c = text; *c >= '0' && *c <= '9' && digits < 4; c++, digits++) v = v * 10 + (*c - '0');
		if (!digits || text[digits] == ':') g_hudCenter = cHudCenter43;
		else g_hudCenter = (clamp(v, 0, 100) + cHudCenterStep / 2) / cHudCenterStep * cHudCenterStep;
	}
	g_pauseBackground = clamp((int)GetPrivateProfileIntA("options", "pause_background", 33, ini_path()), 0, 100);
	GetPrivateProfileStringA("options", "pause_music", "keep", music, sizeof music, ini_path());
	g_pauseMusic = PAUSE_MUSIC_KEEP;
	for (int i = 0; i < 3; i++)
		if (str_ieq_n(music, g_musicNames[i], sizeof music)) g_pauseMusic = i;
	g_showFps = GetPrivateProfileIntA("options", "show_fps", 0, ini_path()) != 0;
	{
		char text[24];
		int t;
		GetPrivateProfileStringA("options", "mouse_sensitivity", "1.000", text, sizeof text, ini_path());
		sens_set(ui_parse_thousandths(text, &t) && t > 0 ? t : cSensDefault);
	}
	log_msg("options: hud_scale %d, hud_center %s, pause_background %d, pause_music %s, show_fps %d, mouse_sensitivity %d/1000",
	        g_hudScale, center_text(), g_pauseBackground, g_musicNames[g_pauseMusic], g_showFps, g_sens);
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
float hud_center(void)             { return g_hudCenter == cHudCenter43 ? 0.0f : g_hudCenter * 0.01f; }
int   hud_center_43(void)          { return g_hudCenter == cHudCenter43; }
int   show_fps(void)               { return g_showFps; }

/* ---- Page ---------------------------------------------------------------------------------- */

enum {
	ID_MORE = 113,
	ID_HUD_SCALE = 120, ID_PAUSE_BACKGROUND,
	ID_MUSIC_KEEP, ID_MUSIC_THEME, ID_MUSIC_SILENCE,
	ID_SHOW_FPS, ID_SENSITIVITY, ID_SENSITIVITY_VALUE, ID_HUD_CENTER,
};
#define ID_LAST ID_HUD_CENTER
#define DLGcMessage_GetDefaultID 0x3e
#define ID_CLOSE    2
#define ID_SOUND    102   /* stock Overall Volume slider, Sound Volume after volume.c */
#define ID_SUBTITLE 107   /* stock checkbox */

#define MAX_WINDOWS 48
static WMtWindow *g_stock[MAX_WINDOWS];   /* stock controls hidden on our page */
static WMtWindow *g_ours[MAX_WINDOWS];    /* our controls, hidden on the stock page */
static u32 g_nstock, g_nours;
static WMtWindow *g_centerSlider;
static WMtWindow *g_dialog, *g_more, *g_hudValue, *g_centerValue, *g_backgroundValue, *g_sensSlider, *g_sensField;
/* Stock windows that move on our page: the Graphics, Sound and misc frames and Close */
#define cRowStep 25
static WMtWindow *g_frameDisplay, *g_frameSound, *g_frameMisc, *g_close;
static void *g_frameHookOld;
static int g_fieldsOn;   /* ui_fields_begin ran for this dialog */
/* New controls go right after this one in the child list. Earlier children are drawn over
 * later ones, and the frames (Title windows) have a translucent dark fill, so a control placed
 * after a frame in the list is drawn under it and looks dimmed. "Quality:" precedes every frame. */
static WMtWindow *g_anchor;
static int g_onOurPage, g_initializing;

/* Kept on both pages: the background picture, the Graphics, Sound and misc frames (x 5, width 300,
 * at y 50, 175 and 290) and the Close button. */
static int keep_on_both_pages(WMtWindow *w)
{
	if (WIN_ID(w) == ID_CLOSE) return 1;
	if (WIN_X(w) == 0 && WIN_Y(w) == 0) return 1;              /* pict_options_background */
	if (WIN_X(w) == 5 && WIN_W(w) == 300 && (WIN_Y(w) == 50 || WIN_Y(w) == 175 || WIN_Y(w) == 290)) return 1;
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

static void move(WMtWindow *w, int y, int h)
{
	if (w) ui_set_frame(w, WIN_X(w), y, WIN_W(w), h);
}

static void show_page(int ours)
{
	int d = ours ? cRowStep : 0;

	WMrWindow_SetFocus(g_dialog);   /* a field being edited is done (and tidied) before it hides */
	/* stock places: Graphics 50 (100 tall), Sound 175, misc 290, Close 405 */
	move(g_frameDisplay, 50, 100 + d);
	move(g_frameSound, 175 + d, 100);
	move(g_frameMisc, 290 + d, 100);
	if (g_close) move(g_close, 405 + d, WIN_H(g_close));
	if (g_more) move(g_more, 405 + d, WIN_H(g_more));
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

/* HUD Centering's thumb and label: 4:3 on its nearest step (which moves with the HUD scale) */
static void center_show(void)
{
	if (g_centerSlider)
		WMrSlider_SetValue(g_centerSlider, g_hudCenter == cHudCenter43 ? hud_center_43_step(cHudCenterStep)
		                                                               : g_hudCenter / cHudCenterStep);
	if (g_centerValue) WMrWindow_SetTitle(g_centerValue, center_text(), 0x100);
}

static void check_music(void)
{
	for (u32 id = ID_MUSIC_KEEP; id <= ID_MUSIC_SILENCE; id++) {
		WMtWindow *box = WMrDialog_GetItemByID(g_dialog, id);
		if (box) WMrCheckBox_SetCheck(box, (int)(id - ID_MUSIC_KEEP) == g_pauseMusic);
	}
}

static int sens_step(int t) { return clamp((t - cSensSlideMin + cSensSlideStep / 2) / cSensSlideStep, 0, cSensSteps); }

static void sens_show(int slider, int field)
{
	char text[16];
	if (slider && g_sensSlider) WMrSlider_SetValue(g_sensSlider, sens_step(g_sens));
	if (field && g_sensField) {
		sens_text(text, g_sens);
		WMrMessage_Send(g_sensField, EFcMessage_SetText, (u32)text, 0);
	}
}

static void sens_save(void)
{
	char text[16];
	sens_text(text, g_sens);
	WritePrivateProfileStringA("options", "mouse_sensitivity", text, ini_path());
}

static void frame(void)
{
	ui_frame_hook_call(g_frameHookOld);
	ui_fields_frame();
}

void more_options_init(WMtWindow *dialog)
{
	WMtWindow *close = WMrDialog_GetItemByID(dialog, ID_CLOSE);
	WMtWindow *slider = WMrDialog_GetItemByID(dialog, ID_SOUND);
	WMtWindow *checkbox = WMrDialog_GetItemByID(dialog, ID_SUBTITLE);
	WMtWindow *rowLabel = find_child(dialog, 30, 65);      /* "Quality:" */
	WMtWindow *header = find_child(dialog, 50, 35);        /* "Graphics" (blue) */
	WMtWindow *title = find_child(dialog, 5, 10);          /* "Options", 70 px wide */
	WMtWindow *hud, *center, *background, *fps;
	const int d = cRowStep;   /* everything under the Display frame, on this page */

	g_dialog = dialog;
	g_anchor = rowLabel;
	g_nstock = g_nours = 0;
	g_more = g_hudValue = g_centerValue = g_backgroundValue = g_sensSlider = g_sensField = g_centerSlider = 0;
	g_frameDisplay = find_child(dialog, 5, 50);
	g_frameSound = find_child(dialog, 5, 175);
	g_frameMisc = find_child(dialog, 5, 290);
	g_close = close;
	if (g_frameDisplay && (WIN_W(g_frameDisplay) != 300 || WIN_H(g_frameDisplay) != 100)) g_frameDisplay = 0;
	if (g_frameSound && (WIN_W(g_frameSound) != 300 || WIN_H(g_frameSound) != 100)) g_frameSound = 0;
	if (g_frameMisc && (WIN_W(g_frameMisc) != 300 || WIN_H(g_frameMisc) != 100)) g_frameMisc = 0;
	g_onOurPage = 0;
	if (!close || !slider || !checkbox || !rowLabel || !header || !title || !g_frameDisplay || !g_frameSound ||
	    !g_frameMisc || WIN_Y(close) != 405) {
		log_msg("options: stock controls not found, no More... page");
		return;
	}

	for (WMtWindow *w = WIN_FIRSTCHILD(dialog); w && g_nstock < MAX_WINDOWS; w = WIN_NEXT(w))
		if (WIN_VISIBLE(w) && !keep_on_both_pages(w)) g_stock[g_nstock++] = w;

	g_initializing = 1;

	/* The stock title is too narrow to rename, so ours replaces it on this page. */
	add(title, "More options", 0, 5, 10, 200, 20);

	/* Display: the Graphics frame (x 5..305, y 50..175 on this page; its border art is several px
	 * thick and the stock controls end by x 280). "Pause Background:" is about 125 px in the
	 * dialog font and wraps in a 130 px label; "100%" is about 36 px. Everything ends by x 285. */
	add(header, "Display", 0, 50, 35, 150, 20);
	add(rowLabel, "HUD Scale:", 0, 30, 65, 136, 20);
	hud = add(slider, "hud scale", ID_HUD_SCALE, 168, 65, 72, 20);
	g_hudValue = add(rowLabel, "", 0, 241, 65, 44, 20);
	add(rowLabel, "HUD Centering:", 0, 30, 90, 136, 20);
	center = add(slider, "hud centering", ID_HUD_CENTER, 168, 90, 72, 20);
	g_centerValue = add(rowLabel, "", 0, 241, 90, 44, 20);
	add(rowLabel, "Pause Background:", 0, 30, 115, 136, 20);
	background = add(slider, "pause background", ID_PAUSE_BACKGROUND, 168, 115, 72, 20);
	g_backgroundValue = add(rowLabel, "", 0, 241, 115, 44, 20);
	fps = add(checkbox, "Show FPS", ID_SHOW_FPS, 30, 140, 270, 20);

	/* When Game Pauses: the Sound frame (y 200..300 on this page) */
	add(header, "When Game Pauses", 0, 50, 160 + d, 200, 20);
	add(checkbox, "Keep playing level music", ID_MUSIC_KEEP, 30, 190 + d, 270, 20);
	add(checkbox, "Play Main Theme (original)", ID_MUSIC_THEME, 30, 215 + d, 270, 20);
	add(checkbox, "Silence", ID_MUSIC_SILENCE, 30, 240 + d, 270, 20);

	/* Mouse: the misc frame (y 315..415 on this page), which has no header on the stock page.
	 * "Sensitivity:" is 78 px; the field fits any 5 characters of digits and '.' (41 px + 8 px of
	 * its margins). */
	add(header, "Mouse", 0, 50, 275 + d, 150, 20);
	add(rowLabel, "Sensitivity:", 0, 30, 305 + d, 86, 20);
	g_sensSlider = add(slider, "mouse sensitivity", ID_SENSITIVITY, 118, 305 + d, 110, 20);
	ui_fields_begin(dialog, WMrMessage_Send(dialog, DLGcMessage_GetDefaultID, 0, 0));
	g_fieldsOn = 1;
	{
		char text[16];
		sens_text(text, g_sens);
		g_sensField = ui_field_decimal(dialog, g_anchor, rowLabel, ID_SENSITIVITY_VALUE, 234, 305 + d, 51, 5, text);
	}
	if (g_sensField) {
		WMrWindow_SetVisible(g_sensField, 0);
		if (g_nours < MAX_WINDOWS) g_ours[g_nours++] = g_sensField;
	}
	g_frameHookOld = ui_frame_hook(frame);

	if (hud) {
		WMrSlider_SetRange(hud, 0, cHudScaleSteps);
		WMrSlider_SetValue(hud, (g_hudScale - cHudScaleMin) / cHudScaleStep);
	}
	g_centerSlider = center;
	if (center) WMrSlider_SetRange(center, 0, 100 / cHudCenterStep);
	if (background) { WMrSlider_SetRange(background, 0, 100); WMrSlider_SetValue(background, g_pauseBackground); }
	show_percent(g_hudValue, g_hudScale);
	center_show();
	show_percent(g_backgroundValue, g_pauseBackground);
	check_music();
	if (fps) WMrCheckBox_SetCheck(fps, g_showFps);
	if (g_sensSlider) WMrSlider_SetRange(g_sensSlider, 0, cSensSteps);
	sens_show(1, 0);

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
	if (dialog != g_dialog || id < ID_MORE || id > ID_LAST) return 0;
	if (g_initializing) return 1;

	if (id == ID_SENSITIVITY_VALUE) {
		char text[16];
		int t;

		if (notify == UI_NOTIFY_CONFIRMED) {   /* what it was set to, written tidy */
			sens_show(0, 1);
		} else if (notify == EFcNotify_Changed && g_sensField) {   /* typing: at once, when it reads as a number */
			text[0] = 0;
			WMrMessage_Send(g_sensField, EFcMessage_GetText, (u32)text, sizeof text);
			text[sizeof text - 1] = 0;
			if (ui_parse_thousandths(text, &t) && t > 0 && sens_tidy(t) != g_sens) {
				sens_set(t);
				sens_save();
				sens_show(1, 0);
			}
		}
		return 1;
	}
	if (notify != WMcNotify_Click) return 1;

	if (id == ID_MORE) {
		show_page(!g_onOurPage);
	} else if (id == ID_HUD_SCALE || id == ID_HUD_CENTER || id == ID_PAUSE_BACKGROUND) {
		WMtWindow *slider = WMrDialog_GetItemByID(dialog, id);
		int v = slider ? WMrSlider_GetValue(slider) : 0;

		if (!slider) return 1;
		if (id == ID_HUD_SCALE) {
			v = cHudScaleMin + clamp(v, 0, cHudScaleSteps) * cHudScaleStep;
			if (v == g_hudScale) return 1;
			g_hudScale = v;
			settings_save_int("hud_scale", v);
			show_percent(g_hudValue, v);
			if (g_hudCenter == cHudCenter43) center_show();   /* its step moves with the rings' size */
		} else if (id == ID_HUD_CENTER) {
			int step = clamp(v, 0, 100 / cHudCenterStep);
			v = step == hud_center_43_step(cHudCenterStep) ? cHudCenter43 : step * cHudCenterStep;
			if (v == g_hudCenter) return 1;
			g_hudCenter = v;
			if (v == cHudCenter43) WritePrivateProfileStringA("options", "hud_center", "4:3", ini_path());
			else settings_save_int("hud_center", v);
			center_show();
		} else {
			v = clamp(v, 0, 100);
			if (v == g_pauseBackground) return 1;
			g_pauseBackground = v;
			settings_save_int("pause_background", v);
			show_percent(g_backgroundValue, v);
		}
	} else if (id == ID_SENSITIVITY) {
		/* a value between the slider's steps (typed) stays until the thumb really moves */
		int step = g_sensSlider ? clamp(WMrSlider_GetValue(g_sensSlider), 0, cSensSteps) : sens_step(g_sens);
		if (step == sens_step(g_sens)) return 1;
		sens_set(cSensSlideMin + step * cSensSlideStep);
		sens_save();
		sens_show(0, 1);
	} else if (id == ID_SHOW_FPS) {
		WMtWindow *box = WMrDialog_GetItemByID(dialog, id);

		g_showFps = !g_showFps;
		settings_save_int("show_fps", g_showFps);
		if (box) WMrCheckBox_SetCheck(box, g_showFps);
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

void more_options_end(WMtWindow *dialog)
{
	if (dialog != g_dialog) return;
	if (g_fieldsOn) {
		ui_frame_hook(g_frameHookOld);
		ui_fields_end();
		g_fieldsOn = 0;
	}
	g_dialog = 0;
	g_sensSlider = g_sensField = g_centerSlider = 0;
	g_frameDisplay = g_frameSound = g_frameMisc = g_close = 0;
}

/* ---- Mouse sensitivity --------------------------------------------------------------------- */

#define LIcActionBuffer_Add 0x403b30   /* ECX the action, EDX &{u32 input, float value} */
#define LIcMouseCode_XAxis  5
#define LIcMouseCode_YAxis  6

typedef struct { u32 input; float value; } LItDeviceInput;

static void ONI_FC scale_mouse(LItDeviceInput *in)
{
	if (in->input == LIcMouseCode_XAxis || in->input == LIcMouseCode_YAxis) in->value *= g_sensFactor;
}

/* Jumped to from 0x403b30: scale the input, then Oni's first instructions (push ebx, esi, edi;
 * mov edi, [edx]) and on at 0x403b35 */
__attribute__((naked)) static void action_add_entry(void)
{
	__asm__ volatile(
		"pushl %%ecx\n\t"
		"pushl %%edx\n\t"
		"movl %%edx, %%ecx\n\t"
		"call %c0\n\t"
		"popl %%edx\n\t"
		"popl %%ecx\n\t"
		"pushl %%ebx\n\t"
		"pushl %%esi\n\t"
		"pushl %%edi\n\t"
		"movl (%%edx), %%edi\n\t"
		"pushl $0x403b35\n\t"
		"ret\n" :: "i"(scale_mouse));
}

static void mouse_install(void)
{
	static const u8 entry[] = { 0x53, 0x56, 0x57, 0x8b, 0x3a };
	u8 jmp[5] = { 0xe9 };
	u32 rel = (u32)action_add_entry - (LIcActionBuffer_Add + 5);

	if (!code_matches(LIcActionBuffer_Add, entry, sizeof entry)) {
		log_msg("mouse sensitivity: LIrActionBuffer_Add changed, left alone");
		return;
	}
	for (int i = 0; i < 4; i++) jmp[i + 1] = (u8)(rel >> (i * 8));
	patch_write(LIcActionBuffer_Add, jmp, sizeof jmp);
	log_msg("mouse sensitivity: patched");
}

void more_options_install(void)
{
	settings_load();
	mouse_install();
}
