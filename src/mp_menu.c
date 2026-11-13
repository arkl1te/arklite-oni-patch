/* Custom Game's network dialogs, built at run time from Oni's own dialogs (no new WMDD):
 *
 *   Custom Game   the New Game popup (dialog 154), widened to 500x160 and centered again: title,
 *                 "Start a NEW custom game, or JOIN one through the network!", New, Join, Cancel.
 *                 New is custom_game.c's level list.
 *   Host Game     the Options dialog (152) with its controls hidden and its three frames laid out
 *                 again: Server (Map, Server name, Port, and what a player with no mods downloads
 *                 to join: the map's folder and level0's differences, net.c), Match (Max players, Time limit in
 *                 minutes, Score limit, a message line; 0 is no limit, a negative score limit is
 *                 reached by suicides; spectators have no limit) and the third frame holding Host
 *                 and Dedicate; Cancel under it. The values are the last ones used (Arklite.ini
 *                 [host] name, max_players, time_limit, score_limit and [net] port). Dedicate
 *                 starts a dedicated server (net.c, server.c): no window, a console instead.
 *   Join Game     the Load Game dialog (151): its frame holds the bookmarked servers in three
 *                 columns (Ping, Name, Players) with + (add) and - (remove) under the list's lower
 *                 left (small buttons, see below); a second frame under it what the selected server
 *                 says (Name, Map, Connected with "(N spectating)" when any) and its Address (typed),
 *                 then a status line. Play, Refresh, Back. Bookmarks are Arklite.ini [servers] count, 1, 2, ...
 *
 * Oni's UI art is nine-slice (PSpc): a button's corners are 55 + 72 px wide, so a button narrower
 * than 127 px gets its corners squeezed. Buttons stay 130x40 like the stock ones; the dialogs and
 * frames (Title windows, outline corners 46 + 43 by 52 + 41) are made bigger instead.
 *
 * Text is measured with Tahoma Bold 10's advances (the menus' font, TSFT in level0) and cut with
 * "..." where it wouldn't fit, so nothing runs past its frame.
 *
 * Edit fields (class 7, proc 0x46b9c0) need help, and only ours get it (they're registered):
 *   - The key handler 0x46c320 (called at 0x46bc87) is replaced for them: they edit their own text
 *     (msg 0x40 get, 0x42 set, the cursor at data +0xc) with a selection Oni's field doesn't have.
 *     Enter confirms the field (numbers are clamped) and takes the focus off it, Escape does the
 *     same without leaving the dialog, Tab goes to the next field; Left/Right, Home/End, Ctrl for
 *     words, Shift to select, Ctrl+A, Backspace/Delete (Ctrl: a word). The key router 0x477510
 *     (called at 0x477307) is wrapped so Enter, Tab, Escape, Home and End reach the field instead
 *     of the dialog. A character that would make the text wider than the field is refused (the
 *     field wraps text to a second line, below the box), number fields take digits only. Oni's
 *     own delete (0x42ef70) at the end of the text copied from past the string's end until it met
 *     a zero, overwriting the heap (the crash); it isn't used any more.
 *   - The class proc (class record +4) is wrapped: a double click selects a word, a triple click
 *     everything, Shift+click up to the click; the paint's DCrDrawText (0x46bc67) draws the
 *     selection as a white bar with the selected characters dark on it.
 *   - While one of them has the focus the dialog's default button is 0 (msg 0x3f; the dialog class
 *     proc 0x468cd0 asks with 0x3e on Enter and does nothing for 0), so Enter only confirms.
 *   - The caret (0x5e9984 position, 0x5e9988 width, 0x5e998a height, part TextCaret) is placed at
 *     its line's top by 0x46bef0 with a height from whatever font was current when the field got
 *     the focus; the call at 0x46c063 is wrapped to give it the field font's height, 2 px wide.
 *     The stock caret part (ps_g206_text_caret) is nearly transparent; ours is ps_white_none_none.
 *   - The focused field's art (UI context +0x48, PSUI EditFieldFocused) is Oni's developer art
 *     ps_g206_ef_hasfocus, a white box; while our dialogs are up it's ps_oniUI_progressbar_track
 *     (+0x5c), the same family as the unfocused ps_oniUI_background (+0x44).
 *   - A field's limit (0x41) counts the terminating zero: n characters need n + 1.
 *
 * The Join dialog asks every listed server for its info about every 2 s (net.c PKT_INFO_REQ, from
 * a socket of its own) in the dialog's frame hook (0x5e96cc, called by the modal loop 0x4689d0
 * every frame before drawing). Its list is the dialog's own, owner-drawn (style 0x100000, msg 0x36)
 * in three columns, so one selection bar spans the row. */
#include "arklite.h"

__declspec(dllimport) u32 WINAPI GetTickCount(void);
__declspec(dllimport) short WINAPI GetAsyncKeyState(int);
void *memset(void *, int, unsigned);
void *memcpy(void *, const void *, unsigned);

#define WMgModalFrameHook   (*(void (**)(void))0x5e96cc)
#define WMgFocus            (*(WMtWindow **)0x5e999c)
/* [WMrWindow_SetFrame]-like: ECX window, EDX the window to go after (with flags bit 2 clear),
 * then x, y, width, height, flags (bit 0 keep the position, bit 1 the size, bit 2 the order) */
#define WMrWindow_SetFrame  ((u8 (ONI_FC *)(WMtWindow *, WMtWindow *, int, int, int, int, u32))0x475770)
#define WMcDialogID_Options 0x98
#define WMcDialogID_NewGame 0x9a
#define DLGcMessage_SetDefaultID 0x3f
#define EFrKey              ((void (ONI_FC *)(WMtWindow *, u32))0x46c320)
#define EFcKeyCall          0x46bc87
#define WMrCaret_SetPosition ((void (ONI_FC *)(u32, u32))0x475b50)
#define EFcCaretCall        0x46c063
#define WMrMessage_Post     ((u32 (ONI_FC *)(WMtWindow *, u32, u32, u32))0x476080)
#define WMrKey_Route        ((void (ONI_FC *)(const u8 *, u32))0x477510)   /* ECX the input event */
#define WMcKeyRouteCall     0x477307
#define WMcMessage_KeyDown  0x1c
/* Edit field: class proc and data (WMrWindow_GetBytes): +8 flags (0x10000 has the focus), +0xc the
 * cursor (characters), +0x12 the text's rectangle */
#define EFrProc             ((u32 (ONI_FC *)(WMtWindow *, u32, u32, u32))0x46b9c0)
#define EFrCaret_Update     ((void (ONI_FC *)(WMtWindow *, u8 *, u32))0x46bef0)
#define EFcFlag_Focused     0x10000
#define EFcPaintTextCall    0x46bc67     /* the paint's DCrDrawText */
#define WMcMessage_MouseDown 0x1f
#define WMcMessage_MouseUp  0x20         /* the field puts the cursor where it was clicked */
#define WMcMessage_Paint    0x2b
#define WMcMessage_GotFocus 0x29
#define WMcMessage_KillFocus 0x2a
/* [DCrDraw_PartSpec]-like: ECX draw context, EDX part, flags (0xffff: all nine), &{x, y}, width,
 * height, alpha */
#define DCrDrawPart         ((void (ONI_FC *)(void *, void *, u32, short *, int, int, u32))0x4698b0)
#define WIN_PARENT(w)       (*(WMtWindow **)((u8 *)(w) + 8))
#define WIN_TEXTCOLOR(w)    (*(u32 *)((u8 *)WIN_FONTINFO(w) + 8))
#define VK_SHIFT            0x10
#define VK_CONTROL          0x11
#define VK_MENU             0x12
#define DOUBLE_CLICK_MS     500
#define WMgCaretWidth       (*(u16 *)0x5e9988)
#define WMgCaretHeight      (*(u16 *)0x5e998a)
#define DCrDrawText         ((void (ONI_FC *)(void *, const char *, u32, short *))0x469f40)
#define DCrSetTextColor     ((void (ONI_FC *)(u32))0x46a5c0)
#define DCrSetTextAlign     ((void (ONI_FC *)(u32))0x46a5b0)
#define DCrSetFont          ((void (ONI_FC *)(const void *))0x46a570)   /* a window's font info */
#define EFcCaretFontCall    0x46bf10     /* 0x46bef0 (caret) sets the field's font, then lays out its text */
#define EFcClickFontCall    0x46c0a0     /* 0x46c080 (click -> cursor) does the same */
#define TMrInstance_GetDataPtr ((u16 (ONI_FC *)(u32, const char *, void *))0x4232e0)
#define TAG_PSpc            0x50537063
#define UI_TEXT_CARET       0x28   /* UI context: PSUI TextCaret (ps_g206_text_caret, nearly transparent) */
#define DCrFont_Ascent      ((u32 (ONI_FC *)(void))0x46a350)   /* of the font current for drawing */
#define DCrFont_Descent     ((u32 (ONI_FC *)(void))0x46a380)
#define UI_EDIT_FIELD       0x44   /* UI context: PSUI EditField */
#define UI_EDIT_FOCUSED     0x48   /* EditFieldFocused */
#define UI_PROGRESS_TRACK   0x5c   /* ProgressBarTrack */
#define BUTTON_W            130
#define BUTTON_H            40

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- Text width ----------------------------------------------------------------------------- */

/* Tahoma Bold 10's advances for ' '..'~' (level0 TSFT 10/bold, its TSGA glyphs +2) */
static const u8 g_bold10[95] = {
	4, 5, 7, 11, 9, 16, 11, 4, 6, 6, 9, 11, 5, 6, 5, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 5, 5, 11, 11, 11, 8,
	13, 9, 9, 9, 10, 9, 8, 10, 11, 7, 7, 10, 8, 12, 11, 11, 9, 11, 10, 9, 8, 10, 9, 14, 9, 9, 9, 6, 8, 6,
	11, 9, 8, 8, 9, 7, 9, 8, 5, 9, 9, 4, 5, 8, 4, 13, 9, 9, 9, 9, 6, 7, 6, 9, 8, 12, 8, 8, 7, 9, 9, 9, 11,
};

static int char_width(u8 c) { return c >= 32 && c < 127 ? g_bold10[c - 32] : 16; }

/* The width of the character at s (ASCII from the table, the rest from the font: text.c gave it
 * the glyphs), its bytes in *n */
static int glyph_width(const char *s, int *n)
{
	int w;
	if ((u8)s[0] < 0x80 || !s[1]) {
		*n = 1;
		return char_width((u8)s[0]);
	}
	*n = 2;
	w = text_advance(10, 1, s);
	return w > 0 ? w : 16;
}

int ui_text_width(const char *s)
{
	int w = 0, n;
	for (; *s; s += n) w += glyph_width(s, &n);
	return w;
}

void ui_fit(char *dst, unsigned size, const char *src, int width)
{
	int w = 0, dots = 3 * char_width('.'), k, cw;
	unsigned n = 0;

	if (ui_text_width(src) <= width) {
		str_cpy(dst, size, src);
		return;
	}
	while (src[n] && n + 4 < size && w + (cw = glyph_width(src + n, &k)) + dots <= width && n + k + 4 <= size) {
		w += cw;
		n += (unsigned)k;
	}
	memcpy(dst, src, n);
	str_cpy(dst + n, size - n, "...");
}

/* ---- Dialog building ------------------------------------------------------------------------ */

WMtWindow *ui_add(WMtWindow *dialog, WMtWindow *anchor, WMtWindow *src, u32 cls, u32 style,
                  const char *text, u32 id, int x, int y, int w, int h)
{
	WMtWindow *win;

	if (!src) return 0;
	win = WMrWindow_New(cls ? cls : WIN_CLASS(src), text, (WIN_FLAGS(src) | 1) & ~2u, style != ~0u ? style : WIN_STYLE(src),
	                    id, x, y, w, h, dialog, 0);
	if (!win) {
		log_msg("menu: WMrWindow_New failed for \"%s\"", text);
		return 0;
	}
	WMrWindow_SetFontInfo(win, WIN_FONTINFO(src));
	if (anchor) win_insert_after(win, anchor);
	return win;
}

void ui_set_frame(WMtWindow *w, int x, int y, int width, int height)
{
	if (w) WMrWindow_SetFrame(w, 0, x, y, width, height, 4);
}

/* A listbox counts its rows when created and never again (WM_ListBox.c, no resize message), and
 * scrolls by that count: one made shorter can't reach its last rows. Sets the count too. */
#define WMrWindow_GetLong ((u8 *(ONI_FC *)(WMtWindow *, u32))0x474dc0)
#define LB_VISIBLE(p)     (*(u32 *)((p) + 0x8c))
#define LB_ROW_H(p)       (*(short *)((p) + 0x92))

void ui_list_set_frame(WMtWindow *w, int x, int y, int width, int height)
{
	u8 *p;
	int row, chrome;

	if (!w) return;
	p = WMrWindow_GetLong(w, 0);
	row = p ? LB_ROW_H(p) : 0;
	chrome = row > 0 ? WIN_H(w) - (int)LB_VISIBLE(p) * row : -1;
	ui_set_frame(w, x, y, width, height);
	if (row > 0 && chrome >= 0 && chrome < row && height > chrome + row)
		LB_VISIBLE(p) = (u32)((height - chrome) / row);
	else
		log_msg("ui: list %d: rows left as they were (row height %d, chrome %d)", WIN_ID(w), row, chrome);
}

void ui_set_text(WMtWindow *w, const char *text)
{
	if (w) WMrWindow_SetTitle(w, text, 0x100);
}

/* The text cut to the window's width */
void ui_set_text_fit(WMtWindow *w, const char *text)
{
	char fit[256];
	if (!w) return;
	ui_fit(fit, sizeof fit, text, WIN_W(w));
	ui_set_text(w, fit);
}

void *ui_frame_hook(void (*fn)(void))
{
	void *old = (void *)WMgModalFrameHook;
	WMgModalFrameHook = fn;
	return old;
}

void ui_frame_hook_call(void *old)
{
	if (old) ((void (*)(void))old)();
}

static WMtWindow *find_child(WMtWindow *dialog, int x, int y)
{
	for (WMtWindow *w = WIN_FIRSTCHILD(dialog); w; w = WIN_NEXT(w))
		if (WIN_X(w) == x && WIN_Y(w) == y) return w;
	return 0;
}

WMtWindow *ui_label(WMtWindow *dialog, WMtWindow *anchor, WMtWindow *font, const char *text, int x, int y, int w, int h, int align)
{
	char fit[256];
	ui_fit(fit, sizeof fit, text, w);
	return ui_add(dialog, anchor, font, WMcClass_Label, UI_LABEL_STYLE | (align == UI_CENTER ? 0x20000 : align == UI_RIGHT ? 0x40000 : 0),
	              fit, 0, x, y, w, h);
}

/* ---- Small buttons -------------------------------------------------------------------------- */

/* An 18x18 button (+, -) can't use the button art: its nine-slice corners are 55 + 72 px wide. It
 * paints with Oni's own small squares instead, the checkbox's empty box (ps_oniUI_checkbox_off,
 * one 18x18 slice, so the button must be exactly that size) and, pressed, ps_oniUI_hilite (a
 * nine-slice). The button class proc (class record +4, shared by every button) is wrapped: for
 * our buttons it swaps the UI context's button parts around the paint. */
#define SMALL_BUTTON      18
#define MAX_SMALL_BUTTONS 4
#define UI_BUTTON_OFF     0x30   /* UI context: PSUI Button, ButtonOff (default), ButtonOn */
#define UI_BUTTON_DEFAULT 0x34
#define UI_BUTTON_ON      0x38

typedef u32 (ONI_FC *WMtProc)(WMtWindow *, u32, u32, u32);
static WMtWindow *g_small[MAX_SMALL_BUTTONS];
static int g_nsmall;
static WMtProc g_buttonProc;
static void *g_smallOff, *g_smallOn;

static u32 ONI_FC small_button_proc(WMtWindow *w, u32 msg, u32 p1, u32 p2)
{
	u8 *ctx;
	void *saved[3];
	u32 r;
	int ours = 0;

	for (int i = 0; i < g_nsmall && !ours; i++) ours = g_small[i] == w;
	if (!ours || msg != WMcMessage_Paint || !(ctx = ONiUI_GetContext())) return g_buttonProc(w, msg, p1, p2);
	saved[0] = *(void **)(ctx + UI_BUTTON_OFF);
	saved[1] = *(void **)(ctx + UI_BUTTON_DEFAULT);
	saved[2] = *(void **)(ctx + UI_BUTTON_ON);
	*(void **)(ctx + UI_BUTTON_OFF) = *(void **)(ctx + UI_BUTTON_DEFAULT) = g_smallOff;
	*(void **)(ctx + UI_BUTTON_ON) = g_smallOn;
	r = g_buttonProc(w, msg, p1, p2);
	*(void **)(ctx + UI_BUTTON_OFF) = saved[0];
	*(void **)(ctx + UI_BUTTON_DEFAULT) = saved[1];
	*(void **)(ctx + UI_BUTTON_ON) = saved[2];
	return r;
}

/* The dialog's small buttons go when it closes */
static void small_buttons_end(void)
{
	g_nsmall = 0;
}

/* src: a button of the dialog (its class, style and font) */
static WMtWindow *small_button_add(WMtWindow *dialog, WMtWindow *anchor, WMtWindow *src, const char *text, u32 id, int x, int y)
{
	WMtWindow *b;
	u32 *proc;

	g_smallOff = g_smallOn = 0;   /* looked up again for every dialog: level0 holds them */
	if (TMrInstance_GetDataPtr(TAG_PSpc, "ps_oniUI_checkbox_off", &g_smallOff)) g_smallOff = 0;
	if (TMrInstance_GetDataPtr(TAG_PSpc, "ps_oniUI_hilite", &g_smallOn)) g_smallOn = 0;
	b = ui_add(dialog, anchor, src, 0, ~0u, text, id, x, y, SMALL_BUTTON, SMALL_BUTTON);
	if (!b) return 0;
	if (!g_smallOff || !g_smallOn) {
		log_msg("menu: small button art missing, \"%s\" keeps the stock art", text);
		return b;
	}
	proc = (u32 *)(*(u8 **)((u8 *)b + 0x14) + 4);
	if (*proc != (u32)small_button_proc) {
		g_buttonProc = (WMtProc)*proc;
		*proc = (u32)small_button_proc;
	}
	if (g_nsmall < MAX_SMALL_BUTTONS) g_small[g_nsmall++] = b;
	return b;
}

/* ---- Edit fields ---------------------------------------------------------------------------- */

#define MAX_FIELDS 8
#define FIELD_TEXT_RAISE 2   /* px our fields' text (and caret) sit above Oni's baseline */
enum { FIELD_TEXT, FIELD_NUMBER, FIELD_SIGNED, FIELD_DECIMAL };   /* signed: a '-' may start it; decimal: digits and one '.' */
typedef struct {
	WMtWindow *w;
	int numbers, lo, hi;   /* FIELD_*, and a number field's range (applied on Enter) */
	int max;               /* characters */
	int anchor;            /* the selection runs from here to the cursor */
} UiField;

static UiField g_fields[MAX_FIELDS];
static int g_nfields;
static WMtWindow *g_fieldDialog;
static u32 g_defaultId;
static u8 *g_uiContext;
static void *g_focusArt, *g_caretArt, *g_white;
static WMtWindow *g_typed;     /* a key changed this field since the dialog last looked */
static UiField *g_painting;    /* the field whose paint is running (edit_draw_text) */
static UiField *g_clickField;  /* double and triple clicks: where and when the last click was */
static u32 g_clickAt, g_clicks;

static UiField *field_of(WMtWindow *w)
{
	for (int i = 0; w && i < g_nfields; i++)
		if (g_fields[i].w == w) return &g_fields[i];
	return 0;
}

/* A dialog with fields starts: Enter means default_id when no field is being edited */
static void fields_begin(WMtWindow *dialog, u32 default_id)
{
	g_nfields = 0;
	if (!g_fieldDialog) text_multibyte_begin();   /* names beyond ASCII while it's open (text.c) */
	g_fieldDialog = dialog;
	g_defaultId = default_id;
	WMrMessage_Send(dialog, DLGcMessage_SetDefaultID, default_id, 0);
	g_typed = 0;
	g_uiContext = ONiUI_GetContext();
	if (g_uiContext && !g_focusArt) {
		void *white = 0;
		g_focusArt = *(void **)(g_uiContext + UI_EDIT_FOCUSED);
		*(void **)(g_uiContext + UI_EDIT_FOCUSED) = *(void **)(g_uiContext + UI_PROGRESS_TRACK);
		/* a solid white caret */
		if (!TMrInstance_GetDataPtr(TAG_PSpc, "ps_white_none_none", &white) && white) {
			g_caretArt = *(void **)(g_uiContext + UI_TEXT_CARET);
			*(void **)(g_uiContext + UI_TEXT_CARET) = white;
			g_white = white;
		}
	}
}

static void fields_end(void)
{
	if (g_uiContext && g_focusArt) *(void **)(g_uiContext + UI_EDIT_FOCUSED) = g_focusArt;
	if (g_uiContext && g_caretArt) *(void **)(g_uiContext + UI_TEXT_CARET) = g_caretArt;
	g_focusArt = g_caretArt = 0;
	g_typed = 0;
	g_uiContext = 0;
	g_nfields = 0;
	if (g_fieldDialog) text_multibyte_end();
	g_fieldDialog = 0;
	g_painting = g_clickField = 0;
}

/* Every frame: Enter belongs to the field being edited */
static void fields_frame(void)
{
	if (g_fieldDialog)
		WMrMessage_Send(g_fieldDialog, DLGcMessage_SetDefaultID, field_of(WMgFocus) ? 0 : g_defaultId, 0);
}

static void edit_text(WMtWindow *e, char *buf, u32 size)
{
	buf[0] = 0;
	if (e) WMrMessage_Send(e, EFcMessage_GetText, (u32)buf, size);
	buf[size - 1] = 0;
}

static int edit_number(WMtWindow *e, int *out)
{
	char text[16];
	int v = 0, any = 0, minus;
	const char *s;

	edit_text(e, text, sizeof text);
	s = skip_space(text);
	if ((minus = *s == '-')) s++;
	for (; *s >= '0' && *s <= '9'; s++, any = 1)
		if ((v = v * 10 + (*s - '0')) > 1000000) return 0;
	*out = minus ? -v : v;
	return any;
}

static u32 ONI_FC edit_proc(WMtWindow *w, u32 msg, u32 p1, u32 p2);

/* max: characters (the field holds max + 1 with the zero) */
static WMtWindow *add_field(WMtWindow *dialog, WMtWindow *anchor, WMtWindow *font, u32 id, int x, int y, int w,
                            u32 max, const char *text, int numbers, int lo, int hi)
{
	WMtWindow *e = ui_add(dialog, anchor, font, WMcClass_EditField, numbers ? EFcStyle_Numbers : 0, "", id, x, y, w, 20);
	char fit[128];

	if (!e) return 0;
	/* the edit field class's proc (its class record +4, shared by every edit field) gets clicks
	 * and paints first; it passes everything for fields that aren't ours straight on */
	u32 *proc = (u32 *)(*(u8 **)((u8 *)e + 0x14) + 4);
	if (*proc == (u32)EFrProc) *proc = (u32)edit_proc;
	WMrMessage_Send(e, EFcMessage_SetMaxLength, max + 1, 0);
	/* never wider than the box (the field would wrap it to a line below) */
	str_cpy(fit, sizeof fit, text);
	while (fit[0] && ui_text_width(fit) > w - 8) fit[text_prev(fit, str_len(fit))] = 0;
	WMrMessage_Send(e, EFcMessage_SetText, (u32)fit, 0);
	if (g_nfields < MAX_FIELDS) {
		UiField *f = &g_fields[g_nfields++];
		f->w = e;
		f->numbers = numbers;
		f->lo = lo;
		f->hi = hi;
		f->max = (int)max;
		f->anchor = str_len(fit);
	}
	return e;
}

/* For dialogs outside this file (more_options.c) */
void ui_fields_begin(WMtWindow *dialog, u32 default_id) { fields_begin(dialog, default_id); }
void ui_fields_end(void) { fields_end(); }
void ui_fields_frame(void) { fields_frame(); }

WMtWindow *ui_field_decimal(WMtWindow *dialog, WMtWindow *anchor, WMtWindow *font, u32 id, int x, int y, int w,
                            u32 max, const char *text)
{
	return add_field(dialog, anchor, font, id, x, y, w, max, text, FIELD_DECIMAL, 0, 0);
}

WMtWindow *ui_field_integer(WMtWindow *dialog, WMtWindow *anchor, WMtWindow *font, u32 id, int x, int y, int w,
                            u32 max, const char *text, int lo, int hi)
{
	return add_field(dialog, anchor, font, id, x, y, w, max, text, lo < 0 ? FIELD_SIGNED : FIELD_NUMBER, lo, hi);
}

int ui_parse_thousandths(const char *s, int *out)
{
	int v = 0, any = 0, scale = 1000;

	s = skip_space(s);
	for (; *s >= '0' && *s <= '9'; s++, any = 1)
		if ((v = v * 10 + (*s - '0')) > 1000000) return 0;
	v *= 1000;
	if (*s == '.') {
		for (s++; *s >= '0' && *s <= '9'; s++, any = 1) {
			if (scale > 1) v += (*s - '0') * (scale /= 10);
			else if (scale == 1 && *s >= '5') { v++; scale = 0; }   /* rounds at the 4th decimal */
			else scale = 0;
		}
	}
	if (*skip_space(s)) return 0;
	*out = v;
	return any;
}

static void field_confirm(UiField *f)
{
	char text[16];
	int v;

	if (f->numbers == FIELD_DECIMAL) {   /* its dialog writes the value back as it shows numbers */
		if (WIN_PARENT(f->w))
			WMrMessage_Send(WIN_PARENT(f->w), WMcMessage_Command, WIN_ID(f->w) | UI_NOTIFY_CONFIRMED << 16, (u32)f->w);
		return;
	}
	if (f->numbers && edit_number(f->w, &v)) {
		fmt(text, "%d", clamp(v, f->lo, f->hi));
		WMrMessage_Send(f->w, EFcMessage_SetText, (u32)text, 0);
	}
}

#define KEY_ENTER        0x0d
#define KEY_ENTER_PAD    0xba
#define KEY_TAB          0x09
#define KEY_ESCAPE       0x1b
#define KEY_BACKSPACE    0x08
#define KEY_DELETE       0xad
#define KEY_HOME         0xab
#define KEY_END          0xae
#define KEY_LEFT         0xb1
#define KEY_RIGHT        0xb3

/* ---- Selection: our fields edit their text themselves (Oni's field has a cursor, no selection) */

static int key_down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

/* Oni's field keeps its cursor in characters (a two-byte one counts once: WM_EditField.c,
 * TSrString_GetLength); ours are bytes */
static int field_cursor(UiField *f)
{
	u8 *data = WMrWindow_GetBytes(f->w, 0);
	char text[128];
	int chars, at = 0;
	if (!data) return 0;
	chars = *(int *)(data + 0xc);
	edit_text(f->w, text, sizeof text);
	while (chars-- > 0 && text[at]) at = text_next(text, at);
	return at;
}

static int field_focused(UiField *f)
{
	u8 *data = WMrWindow_GetBytes(f->w, 0);
	return data && (*(u32 *)(data + 8) & EFcFlag_Focused);
}

/* The cursor to at; extend keeps the anchor (Shift), else the selection is gone */
static void field_move(UiField *f, int at, int extend)
{
	u8 *data = WMrWindow_GetBytes(f->w, 0);
	char text[128];
	int chars = 0;
	if (!data) return;
	edit_text(f->w, text, sizeof text);
	for (int i = 0; i < at && text[i]; i = text_next(text, i)) chars++;
	*(int *)(data + 0xc) = chars;
	if (!extend) f->anchor = at;
	EFrCaret_Update(f->w, data, 0);
}

/* The selection in text of len characters: 1 if there is one, its ends in *lo, *hi */
static int field_selection(UiField *f, int len, int *lo, int *hi)
{
	int a = clamp(f->anchor, 0, len), c = clamp(field_cursor(f), 0, len);
	*lo = a < c ? a : c;
	*hi = a < c ? c : a;
	return *hi > *lo;
}

/* Spaces, letters and digits (a word, with every character beyond ASCII: both its bytes count, so a
 * run never ends inside one), and the rest (".", ":", ... each a word of its own kind) */
static int char_class(char c)
{
	if (c == ' ') return 0;
	if ((u8)c >= 0x80) return 1;
	if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') return 1;
	return 2;
}

/* Ctrl+Left: to the start of this word or the one before; Ctrl+Right: to the start of the next */
static int word_left(const char *text, int i)
{
	while (i > 0 && !char_class(text[i - 1])) i--;
	if (i > 0) {
		int c = char_class(text[i - 1]);
		while (i > 0 && char_class(text[i - 1]) == c) i--;
	}
	return i;
}

static int word_right(const char *text, int len, int i)
{
	if (i < len) {
		int c = char_class(text[i]);
		if (c) while (i < len && char_class(text[i]) == c) i++;
	}
	while (i < len && !char_class(text[i])) i++;
	return i;
}

/* text[lo, hi) becomes ins; the cursor goes after it. 0 if it wouldn't fit. */
static int field_replace(UiField *f, const char *text, int lo, int hi, const char *ins)
{
	char buf[128];
	int n = 0, len = str_len(text), k = str_len(ins);

	if (lo + k + (len - hi) > f->max || lo + k + (len - hi) >= (int)sizeof buf) return 0;
	memcpy(buf, text, lo);
	n = lo;
	memcpy(buf + n, ins, k);
	n += k;
	memcpy(buf + n, text + hi, len - hi);
	n += len - hi;
	buf[n] = 0;
	if (ui_text_width(buf) > WIN_W(f->w) - 8) return 0;   /* the field would wrap it below the box */
	WMrMessage_Send(f->w, EFcMessage_SetText, (u32)buf, 0);
	field_move(f, lo + k, 0);
	g_typed = f->w;
	if (WIN_PARENT(f->w))
		WMrMessage_Send(WIN_PARENT(f->w), WMcMessage_Command, WIN_ID(f->w) | EFcNotify_Changed << 16, (u32)f->w);
	return 1;
}

static void ONI_FC edit_key(WMtWindow *w, u32 key)
{
	UiField *f = field_of(w);
	char text[128], ins[2];
	int len, cursor, lo, hi, sel, shift, ctrl;

	if (!f) {
		EFrKey(w, key);
		return;
	}
	key &= 0xffff;
	if (key == KEY_ENTER || key == KEY_ENTER_PAD || key == KEY_ESCAPE) {
		field_confirm(f);
		field_move(f, field_cursor(f), 0);
		WMrWindow_SetFocus(g_fieldDialog);
		return;
	}
	if (key == KEY_TAB) {
		field_confirm(f);
		field_move(f, field_cursor(f), 0);
		WMrWindow_SetFocus(g_fields[(f - g_fields + 1) % g_nfields].w);
		return;
	}
	edit_text(w, text, sizeof text);
	len = str_len(text);
	cursor = clamp(field_cursor(f), 0, len);
	sel = field_selection(f, len, &lo, &hi);
	shift = key_down(VK_SHIFT);
	ctrl = key_down(VK_CONTROL);
	switch (key) {
	case KEY_LEFT:
		field_move(f, ctrl ? word_left(text, cursor) : sel && !shift ? lo : text_prev(text, cursor), shift);
		return;
	case KEY_RIGHT:
		field_move(f, ctrl ? word_right(text, len, cursor) : sel && !shift ? hi : cursor < len ? text_next(text, cursor) : len, shift);
		return;
	case KEY_HOME:
		field_move(f, 0, shift);
		return;
	case KEY_END:
		field_move(f, len, shift);
		return;
	case KEY_BACKSPACE:
		if (sel) field_replace(f, text, lo, hi, "");
		else if (cursor > 0) field_replace(f, text, ctrl ? word_left(text, cursor) : text_prev(text, cursor), cursor, "");
		return;
	case KEY_DELETE:   /* (Oni's own delete at the end of the text runs past it) */
		if (sel) field_replace(f, text, lo, hi, "");
		else if (cursor < len) field_replace(f, text, cursor, ctrl ? word_right(text, len, cursor) : text_next(text, cursor), "");
		return;
	}
	if (key < 0x20 || key >= 0x7f) return;   /* no glyphs for it, or a key the field does something odd with */
	if (ctrl) {
		if (key == 'a' || key == 'A') {   /* Ctrl+A: all of it */
			f->anchor = 0;
			field_move(f, len, 1);
		}
		return;
	}
	if (f->numbers == FIELD_DECIMAL) {   /* digits, and a '.' when there's none outside the selection */
		int a = sel ? lo : cursor, b = sel ? hi : cursor, dot = 0;
		for (int i = 0; i < len; i++)
			if (text[i] == '.' && (i < a || i >= b)) dot = 1;
		if ((key < '0' || key > '9') && (key != '.' || dot)) return;
	} else if (f->numbers && (key < '0' || key > '9') &&
	    !(key == '-' && f->numbers == FIELD_SIGNED && (sel ? lo : cursor) == 0 && (text[0] != '-' || (sel && lo == 0 && hi > 0))))
		return;
	if (f->numbers && f->numbers != FIELD_DECIMAL && key != '-' && !sel && cursor == 0 && text[0] == '-') return;   /* a digit before the '-' */
	ins[0] = (char)key;
	ins[1] = 0;
	if (sel) field_replace(f, text, lo, hi, ins);
	else field_replace(f, text, cursor, cursor, ins);
}

/* text.c: a character beyond ASCII typed (or finished by an IME) while a text field has the
 * keyboard; Oni's own key path would have made it a code page byte */
int ui_field_focused(void)
{
	UiField *f = g_fieldDialog ? field_of(WMgFocus) : 0;
	return f && f->numbers == FIELD_TEXT;
}

int ui_field_type(u32 c)
{
	UiField *f = g_fieldDialog ? field_of(WMgFocus) : 0;
	char text[128], ins[3] = { 0 };
	int lo, hi;

	if (!f || f->numbers != FIELD_TEXT) return 0;
	if (!text_put(ins, c)) return 1;   /* none of Arklite's characters: dropped */
	if (key_down(VK_CONTROL) && !key_down(VK_MENU)) return 1;
	edit_text(f->w, text, sizeof text);
	if (field_selection(f, str_len(text), &lo, &hi)) field_replace(f, text, lo, hi, ins);
	else field_replace(f, text, field_cursor(f), field_cursor(f), ins);
	return 1;
}

/* The selection is drawn under the text: a white bar, the selected characters dark on it. The
 * field's paint (0x46bb5f) draws its text with one DCrDrawText at {left + 2, baseline}, with the
 * field's font current, left-aligned; pieces of it go at the same baseline. */
static void ONI_FC edit_draw_text(void *dc, const char *text, u32 rect, short *at_stock)
{
	UiField *f = g_painting;
	char piece[128];
	int len = str_len(text), lo, hi;
	short raised[2], *pos = at_stock;

	if (f && pos) {   /* Oni's baseline sits low in the box: ours go FIELD_TEXT_RAISE px up */
		raised[0] = pos[0];
		raised[1] = (short)(pos[1] - FIELD_TEXT_RAISE);
		pos = raised;
	}
	if (!f || !g_white || !pos || !field_focused(f) || !field_selection(f, len, &lo, &hi) || len >= (int)sizeof piece) {
		DCrDrawText(dc, text, rect, pos);
		return;
	}
	u32 ascent = DCrFont_Ascent() & 0xffff, descent = DCrFont_Descent() & 0xffff;
	short at[2];
	int x_lo, x_hi;

	memcpy(piece, text, lo);
	piece[lo] = 0;
	x_lo = pos[0] + ui_text_width(piece);
	memcpy(piece, text + lo, hi - lo);
	piece[hi - lo] = 0;
	x_hi = x_lo + ui_text_width(piece);
	at[0] = (short)x_lo;
	at[1] = (short)(pos[1] - ascent);
	DCrDrawPart(dc, g_white, 0xffff, at, x_hi - x_lo, ascent + descent, 0xff);
	at[1] = pos[1];
	DCrSetTextColor(0xff000000);
	DCrDrawText(dc, piece, rect, at);
	DCrSetTextColor(WIN_TEXTCOLOR(f->w));
	memcpy(piece, text, lo);
	piece[lo] = 0;
	if (lo) DCrDrawText(dc, piece, rect, pos);
	if (hi < len) {
		at[0] = (short)x_hi;
		DCrDrawText(dc, text + hi, rect, at);
	}
}

/* Our fields' class proc: clicks count for double (a word) and triple (everything) clicks, Shift
 * and a click selects up to it; the paint is told which field it draws */
static u32 ONI_FC edit_proc(WMtWindow *w, u32 msg, u32 p1, u32 p2)
{
	UiField *f = field_of(w);
	char text[128];
	int len, cursor, lo, hi;
	u32 r, now;

	if (!f) return EFrProc(w, msg, p1, p2);
	switch (msg) {
	case WMcMessage_Paint:
		g_painting = f;
		r = EFrProc(w, msg, p1, p2);
		g_painting = 0;
		return r;
	case WMcMessage_MouseDown:
		now = GetTickCount();
		g_clicks = g_clickField == f && now - g_clickAt <= DOUBLE_CLICK_MS && g_clicks < 3 ? g_clicks + 1 : 1;
		g_clickField = f;
		g_clickAt = now;
		return EFrProc(w, msg, p1, p2);
	case WMcMessage_MouseUp: {
		int anchor = f->anchor;
		r = EFrProc(w, msg, p1, p2);
		edit_text(w, text, sizeof text);
		len = str_len(text);
		cursor = clamp(field_cursor(f), 0, len);
		if (g_clickField == f && g_clicks == 3) {
			f->anchor = 0;
			field_move(f, len, 1);
		} else if (g_clickField == f && g_clicks == 2 && len) {
			int p = cursor < len ? cursor : len - 1, c = char_class(text[p]);
			for (lo = p; lo > 0 && char_class(text[lo - 1]) == c; lo--) {}
			for (hi = p + 1; hi < len && char_class(text[hi]) == c; hi++) {}
			f->anchor = lo;
			field_move(f, hi, 1);
		} else if (key_down(VK_SHIFT)) {
			f->anchor = anchor;
			field_move(f, cursor, 1);
		} else {
			field_move(f, cursor, 0);
		}
		return r;
	}
	case WMcMessage_KillFocus:   /* clicked away from a decimal field: as Enter */
		r = EFrProc(w, msg, p1, p2);
		if (f->numbers == FIELD_DECIMAL) field_confirm(f);
		return r;
	case WMcMessage_GotFocus:
	case EFcMessage_SetText:   /* the cursor is at the new text's end, nothing selected */
		r = EFrProc(w, msg, p1, p2);
		f->anchor = field_cursor(f);
		return r;
	}
	return EFrProc(w, msg, p1, p2);
}

/* The key router (0x477510) asks the focused window which kinds of keys it takes (msg 0x38), but
 * a key that is only a control character (Enter 0xd, Tab 9, Escape 0x1b) always goes to the
 * dialog: Enter to its default button, Escape to Cancel. So do Home, End and the other keys of
 * 0xab..0xaf, which the edit field doesn't ask for. Event types 9 and 0xa become key messages
 * (0x1c); for those keys on our fields, the field gets them instead. */
static void ONI_FC key_route(const u8 *ev, u32 edx)
{
	u32 type = *(const u32 *)ev, key = *(const u16 *)(ev + 8);

	if ((type == 9 || type == 0xa) && field_of(WMgFocus) &&
	    (key == KEY_ENTER || key == KEY_TAB || key == KEY_ESCAPE || key == KEY_HOME || key == KEY_END)) {
		WMrMessage_Post(WMgFocus, WMcMessage_KeyDown, key, *(const u32 *)(ev + 0xc));
		return;
	}
	WMrKey_Route(ev, edx);
}

/* 0x46bef0 hands the caret its line's top; its height was the font current when the field got
 * the focus (any font), so it gets the field's own */
static void ONI_FC caret_position(u32 x, u32 y)
{
	if (field_of(WMgFocus)) {
		u32 ascent = DCrFont_Ascent() & 0xffff, descent = DCrFont_Descent() & 0xffff;
		WMgCaretHeight = (u16)(ascent + descent);
		WMgCaretWidth = 2;
		y -= FIELD_TEXT_RAISE;   /* with the text (edit_draw_text) */
	}
	WMrCaret_SetPosition(x, y);
}

/* The layout (0x42e210) aligns lines by the draw context's alignment, which the caret and click
 * code never set: after a centered button was drawn the caret sat halfway between the text's end
 * and the field's right side. A field paints its text left-aligned (0x46a5b0(1) in its paint), so
 * its layout is now left-aligned too. */
static void ONI_FC edit_set_font(const void *info)
{
	DCrSetFont(info);
	DCrSetTextAlign(1);
}

void mp_menu_install(void)
{
	if (patch_call(EFcKeyCall, (u32)EFrKey, (u32)edit_key) && patch_call(EFcCaretCall, (u32)WMrCaret_SetPosition, (u32)caret_position))
		log_msg("menu: Custom Game's text fields: keys and caret patched");
	else
		log_msg("menu: edit field code changed, text fields keep Oni's behaviour");
	if (!patch_call(WMcKeyRouteCall, (u32)WMrKey_Route, (u32)key_route))
		log_msg("menu: key routing changed, Enter, Tab, Escape, Home and End in text fields go to the dialog");
	if (!patch_call(EFcPaintTextCall, (u32)DCrDrawText, (u32)edit_draw_text))
		log_msg("menu: edit field paint changed, text selections aren't drawn");
	if (!patch_call(EFcCaretFontCall, (u32)DCrSetFont, (u32)edit_set_font) ||
	    !patch_call(EFcClickFontCall, (u32)DCrSetFont, (u32)edit_set_font))
		log_msg("menu: edit field layout calls changed, the caret may follow the last alignment drawn");
}

/* ---- Owner-drawn list rows ------------------------------------------------------------------ */

/* Message 0x36 from a list box with style 0x100000 (its paint 0x46d3c0 draws the selection, then
 * asks the dialog for each row's text): p2 points at {draw context, list, id (u16), selected
 * (u16), row, x, y, right, bottom (shorts), text, item data}. The list's font is current. */
void ui_list_draw_text(u32 p2, int x, int width, const char *text, u32 color)
{
	const u8 *d = (const u8 *)p2;
	char fit[128];
	short point[2];

	if (!d || !text[0]) return;
	ui_fit(fit, sizeof fit, text, width);
	point[0] = (short)(*(const short *)(d + 0x10) + 3 + x);
	point[1] = (short)(*(const short *)(d + 0x12) + (short)(DCrFont_Ascent() & 0xffff));
	DCrSetTextColor(color);
	DCrSetTextAlign(1);
	DCrDrawText(*(void *const *)d, fit, 0, point);
}

int ui_list_draw_row(u32 p2, u32 color)
{
	if (!p2) return 0;
	ui_list_draw_text(p2, 0, 4096, *(const char *const *)(p2 + 0x18), color);
	return 1;
}

/* ---- Custom Game popup ---------------------------------------------------------------------- */

enum { ID_POPUP_NEW = 1, ID_POPUP_CANCEL = 2, ID_POPUP_JOIN = 3 };
#define POPUP_W 500
#define POPUP_H 160

static u8 ONI_FC popup_proc(WMtWindow *dialog, u32 msg, u32 p1, u32 p2)
{
	u32 id = p1 & 0xffff, code = p1 >> 16;

	if (msg == WMcMessage_InitDialog) {
		/* 350x150, centered: label (35,30 280x25), Start (35,70), Cancel (185,70), both 130x40,
		 * the outline frame (0,0 350x150). The frame grows (it's nine-slice); the buttons keep
		 * their size: three of them and the gaps need 500. */
		WMtWindow *label = find_child(dialog, 35, 30);
		WMtWindow *start = WMrDialog_GetItemByID(dialog, ID_POPUP_NEW);
		WMtWindow *cancel = WMrDialog_GetItemByID(dialog, ID_POPUP_CANCEL);
		WMtWindow *outline = find_child(dialog, 0, 0);
		int gap = (POPUP_W - 2 * 35 - 3 * BUTTON_W) / 2, y = 92;

		if (!label || !start || !cancel || !outline) {
			log_msg("custom game: the New Game popup's controls are missing");
			return 1;
		}
		ui_set_frame(dialog, WIN_X(dialog) - (POPUP_W - WIN_W(dialog)) / 2, WIN_Y(dialog) - (POPUP_H - WIN_H(dialog)) / 2,
		             POPUP_W, POPUP_H);
		ui_set_frame(outline, 0, 0, POPUP_W, POPUP_H);
		ui_label(dialog, label, label, "Custom Game", 20, 12, 200, 20, UI_LEFT);
		WIN_STYLE(label) = (WIN_STYLE(label) & ~0x60000u) | 0x20000;   /* centered */
		ui_set_frame(label, 20, 48, POPUP_W - 40, 20);
		ui_set_text_fit(label, "Start a NEW custom game, or JOIN one through the network!");
		ui_set_text(start, "New");
		ui_set_frame(start, 35, y, BUTTON_W, BUTTON_H);
		ui_add(dialog, start, start, 0, ~0u, "Join", ID_POPUP_JOIN, 35 + BUTTON_W + gap, y, BUTTON_W, BUTTON_H);
		ui_set_frame(cancel, 35 + 2 * (BUTTON_W + gap), y, BUTTON_W, BUTTON_H);
		WMrWindow_SetFocus(start);
		return 1;
	}
	if (msg != WMcMessage_Command || code != WMcNotify_Click) return msg == WMcMessage_Command;
	if (id == ID_POPUP_NEW) WMrDialog_ModalEnd(dialog, MPMENU_NEW);
	else if (id == ID_POPUP_JOIN) WMrDialog_ModalEnd(dialog, MPMENU_JOIN);
	else if (id == ID_POPUP_CANCEL) WMrDialog_ModalEnd(dialog, MPMENU_CANCEL);
	return 1;
}

int mp_menu_choose(void)
{
	u32 choice = MPMENU_CANCEL;
	if (WMrDialog_ModalBegin(WMcDialogID_NewGame, 0, popup_proc, 0, &choice)) return MPMENU_CANCEL;
	return (int)choice;
}

/* ---- Host ----------------------------------------------------------------------------------- *
 * The Options dialog (152, 640x480) rebuilt over the splash screen's picture (TXMBpict_splash_screen,
 * level0's own, put into the dialog's background picture):
 *
 *   one big frame (Options' Graphics frame) titled "Host Game", holding on the left the Server rows
 *   (Server name, Port) and the Rules rows (Max players, Time limit, Score limit, the message line),
 *   with its blue header, and
 *   the character checkboxes (Mutant Muro, Shinatama and Hasegawa, Mini Striker) under them, a
 *   Message of the day row at the bottom; on the right the Map dropdown (Options' Difficulty
 *   popup, holding every map that can be hosted) with what a new player downloads, and a "Current
 *   mods" frame (Options' Sound frame) listing the installed AE mods with each package's size on
 *   disk. Host, Dedicate and Cancel under the big frame, Cancel left of the splash's Bungie logo.
 *
 * Mods: GameDataFolder\installed_mods.xml's <mods> package numbers, those of 8000 and up (AE
 * Installer 2's isCorePackage() is "number < DepotConfig.corePackageNumberLimit", 8000), named by
 * AEInstaller\packages\<number, 5 digits><name>\Mod_Info.cfg's NameOfMod, sized as that whole folder.
 * The level0 download itself is measured as one diff (net.c), so a mod's own share isn't known. */

enum { ID_HOST_GO = 1, ID_HOST_CANCEL = 2, ID_HOST_DEDICATE = 3,
       ID_HOST_MAP = 108,   /* the Options dialog's Difficulty dropdown, kept */
       ID_HOST_NAME = 120, ID_HOST_PORT, ID_HOST_PLAYERS, ID_HOST_TIME, ID_HOST_SCORE,
       ID_HOST_MUTATORS = 130, ID_HOST_MODS = 132, ID_HOST_MOTD };
#define HOST_STR(x)  HOST_STR_(x)
#define HOST_STR_(x) #x
#define HOST_NOTE "2 to " HOST_STR(NET_PLAYERS_MAX) " players, if the map has room."
#define HOST_MAPS_MAX   60     /* in the dropdown (a menu too tall for the screen splits into columns) */
#define HOST_MAP_TEXT_W 175    /* the dropdown's 205 px less its arrow */
#define MAX_MODS        128
#define CORE_PACKAGES   8000   /* AE Installer 2: packages numbered below are the core */

/* Oni's popup menus (WM_PopupMenu.c); the Options dialog fills its Resolution popup with these */
#define WMrPopupMenu_Reset        ((u16 (ONI_FC *)(WMtWindow *))0x470c70)
#define WMrPopupMenu_AppendItem   ((u32 (ONI_FC *)(WMtWindow *, u32, const char *))0x4704d0)   /* ECX popup, EDX id */
#define WMrPopupMenu_SetSelection ((u32 (ONI_FC *)(WMtWindow *, u32))0x470d00)   /* and tells the dialog */
#define WMcMessage_MenuCommand    0x19       /* to the dialog: p1 the item's id (low word), p2 the popup */
#define PMcStyle_NoResize         0x10000    /* else the popup takes the menu's width */
#define DCrDraw_Begin             ((void *(ONI_FC *)(WMtWindow *))0x4694b0)
#define DCrDraw_End               ((void (ONI_FC *)(void *, WMtWindow *))0x469650)   /* ECX context, EDX window */
#define PIC_TEXTURE(w)            (*(void **)((u8 *)(w) + 0x160))   /* a picture's long 0: its TXMB, TXMP or PSpc */
#define TAG_TXMB                  0x54584d42
#define UI_OUTLINE                0x2c       /* UI context: PSUI Outline (ps_oniUI_outline, the frames) */
#define PS_MIDDLE_MIDDLE          0x10       /* drawn alone, the middle part fills the whole rectangle */
#define LBcStyle_HasScrollbar     0x10000
#define CBcStyle_HasTitle         0x10000    /* the check itself is the long's 0x10000 (msg 0x3b sets, 0x2d reads) */
#define WMrCheckBox_GetCheck      ((u32 (ONI_FC *)(WMtWindow *))0x46b920)
#define LBcStyle_HasStrings       0x80000
#define MODS_ROWS                 11         /* the Host list's 197 px hold about this many: a scroll bar past it */

__declspec(dllimport) int WINAPI ReadFile(HANDLE, void *, u32, u32 *, void *);
__declspec(dllimport) int WINAPI CloseHandle(HANDLE);
__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) int WINAPI VirtualFree(void *, u32, u32);

static WMtWindow *g_hostStatus, *g_hostDownload, *g_hostMods;
static const char *const *g_hostMaps;
static int g_hostNmaps, g_hostSel;
static int (*g_hostMapDir)(int, char *, u32);
static void *g_hostHookOld;
static int g_hostMapOk, g_hostShown;   /* the download line: 1 says the final value */
static u32 g_hostMapBytes;

typedef struct { char name[64], size[16]; } ModRow;
static ModRow g_mods[MAX_MODS + 1];
static int g_nmods;

/* A size in B up to 999 bytes, then KB from 1000 bytes (0.97 KB), MB from 1000 KB, GB from 1000 MB:
 * 1024 of one unit make the next, two decimals cut (not rounded, so it never says 1000.00) */
static void size_units(char *out, unsigned long long bytes)
{
	static const char *const unit[] = { "KB", "MB", "GB", "TB" };
	u32 k = 0;

	if (bytes < 1000) {
		fmt(out, "%u B", (u32)bytes);
		return;
	}
	while (k < 3 && bytes >= 1000ull << (10 * (k + 1))) k++;
	u32 hundredths = (u32)((bytes * 100) >> (10 * (k + 1)));
	fmt(out, "%u.%02u %s", hundredths / 100, hundredths % 100, unit[k]);
}

/* The download line: the map's bytes and level0's, once level0 is measured */
static void host_download_update(void)
{
	char text[32];
	u32 l0;
	int r = 0;   /* (the log reads it when the map can't be sent too) */

	if (!g_hostDownload || g_hostShown == 1) return;
	if (!g_hostMapOk) str_cpy(text, sizeof text, "Not possible");   /* joiners can't get the map */
	else if (!(r = net_join_level0_bytes(&l0))) {
		if (g_hostShown) return;   /* says so already */
		str_cpy(text, sizeof text, "Measuring...");
	} else if (r < 0) str_cpy(text, sizeof text, "Unknown");
	else size_units(text, (unsigned long long)g_hostMapBytes + l0);
	g_hostShown = !g_hostMapOk || r ? 1 : -1;
	ui_set_text_fit(g_hostDownload, text);
	if (g_hostShown == 1)
		log_msg("host: %s: new players download %s (map %u bytes%s, level0 %s)", g_hostMaps ? g_hostMaps[g_hostSel] : "?", text,
		        g_hostMapBytes, g_hostMapOk ? "" : ", can't be sent", r > 0 ? "measured" : r < 0 ? "unknown" : "-");
}

/* The dropdown's map i: what a new player downloads changes with it */
static void host_map_select(int i)
{
	char dir[MAX_PATH];

	if (i < 0 || i >= g_hostNmaps) return;
	g_hostSel = i;
	g_hostMapOk = net_join_map_bytes(g_hostMapDir && g_hostMapDir(i, dir, sizeof dir) ? dir : 0, &g_hostMapBytes);
	g_hostShown = 0;
	host_download_update();
}

/* ---- The Map dropdown's menu: Oni's menus have no backing in psui_oniUI (the names would float
 * over the mods list), so ours gets the frames' fill twice and the fields' outline under it ---- */

static WMtWindow *g_mapMenu;
static WMtProc g_menuProc;

static u32 ONI_FC map_menu_proc(WMtWindow *w, u32 msg, u32 p1, u32 p2)
{
	u8 *ui;
	void *dc, *fill, *box;
	short at[2] = { 0, 0 };

	if (w == g_mapMenu && msg == WMcMessage_Paint && (ui = ONiUI_GetContext()) && (fill = *(void **)(ui + UI_OUTLINE)) &&
	    (box = *(void **)(ui + UI_EDIT_FIELD)) && (dc = DCrDraw_Begin(w))) {
		DCrDrawPart(dc, fill, PS_MIDDLE_MIDDLE, at, WIN_W(w), WIN_H(w), 0xff);
		DCrDrawPart(dc, fill, PS_MIDDLE_MIDDLE, at, WIN_W(w), WIN_H(w), 0xff);
		DCrDrawPart(dc, box, 0xffff, at, WIN_W(w), WIN_H(w), 0xff);
		DCrDraw_End(dc, w);
	}
	return g_menuProc(w, msg, p1, p2);
}

static void map_menu_backing(WMtWindow *popup)
{
	u8 *data = WMrWindow_GetLong(popup, 0);   /* the popup's private data: +8 its menu window */
	u32 *proc;

	g_mapMenu = data ? *(WMtWindow **)(data + 8) : 0;
	if (!g_mapMenu) return;
	proc = (u32 *)(*(u8 **)((u8 *)g_mapMenu + 0x14) + 4);
	if (*proc != (u32)map_menu_proc) {
		g_menuProc = (WMtProc)*proc;
		*proc = (u32)map_menu_proc;
	}
}

/* ---- The mods list: owner-drawn rows (name, size at the right), no outline of its own ---- */

static WMtProc g_listProc;
static void *g_noArt;
static WMtWindow *g_modsList;   /* the mods list on screen (one dialog at a time) */

static u32 ONI_FC mods_list_proc(WMtWindow *w, u32 msg, u32 p1, u32 p2)
{
	u8 *ui;
	void *saved;
	u32 r;

	/* every message: the box is drawn outside the paint too (0x46c7e5, with its own draw context 0x469720) */
	if (!w || w != g_modsList || !g_noArt || !(ui = ONiUI_GetContext())) return g_listProc(w, msg, p1, p2);
	saved = *(void **)(ui + UI_EDIT_FIELD);   /* the list's box (WM_ListBox.c draws partspec_ui->editfield) */
	*(void **)(ui + UI_EDIT_FIELD) = g_noArt;
	r = g_listProc(w, msg, p1, p2);
	*(void **)(ui + UI_EDIT_FIELD) = saved;
	return r;
}

static u32 read_small(const char *path, char *buf, u32 size)
{
	HANDLE f = CreateFileA(path, 0x80000000 /* GENERIC_READ */, 3, 0, 3 /* OPEN_EXISTING */, 0x80, 0);
	u32 got = 0;

	if (f == INVALID_HANDLE_VALUE) return 0;
	if (!ReadFile(f, buf, size - 1, &got, 0)) got = 0;
	CloseHandle(f);
	buf[got] = 0;
	return got;
}

/* path (a folder, without the last '\\') and everything under it, in bytes */
static unsigned long long folder_bytes(char *path, u32 size, int depth)
{
	WIN32_FIND_DATAA fd;
	unsigned long long total = 0;
	u32 len = str_len(path);
	HANDLE h;

	if (depth > 8 || len + 3 >= size) return 0;
	str_cpy(path + len, size - len, "\\*");
	h = FindFirstFileA(path, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (fd.cFileName[0] == '.' && (!fd.cFileName[1] || (fd.cFileName[1] == '.' && !fd.cFileName[2]))) continue;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				if (len + 1 + str_len(fd.cFileName) + 3 >= size) continue;
				path[len] = '\\';
				str_cpy(path + len + 1, size - len - 1, fd.cFileName);
				total += folder_bytes(path, size, depth + 1);
			} else {
				total += ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
			}
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	path[len] = 0;
	return total;
}

/* Mod_Info.cfg's "NameOfMod -> ..." */
static int mod_info_name(const char *dir, char *name, u32 size)
{
	static char buf[8192];
	char path[MAX_PATH];
	const char *p;

	fmt(path, "%s\\Mod_Info.cfg", dir);
	if (!read_small(path, buf, sizeof buf)) return 0;
	for (p = buf; *p; ) {
		const char *line = skip_space(p);
		while (*p && *p != '\n') p++;
		if (*p) p++;
		if (!str_ieq_n(line, "NameOfMod", 9)) continue;
		line = skip_space(line + 9);
		if (line[0] != '-' || line[1] != '>') continue;
		line = skip_space(line + 2);
		u32 n = 0;
		while (line[n] && line[n] != '\r' && line[n] != '\n' && n + 1 < size) { name[n] = line[n]; n++; }
		while (n && name[n - 1] == ' ') n--;
		name[n] = 0;
		return n > 0;
	}
	return 0;
}

/* AEInstaller\packages\<number, 5 digits><name>: "12000New_Weapons_Collection", not "120001..." */
static int package_dir(u32 number, char *dir, char *folder, u32 size)
{
	WIN32_FIND_DATAA fd;
	char pattern[64];
	HANDLE h;
	int found = 0;

	fmt(pattern, "AEInstaller\\packages\\%05u*", number);
	h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) return 0;
	do {
		const char *n = fd.cFileName;
		if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(n[5] >= '0' && n[5] <= '9')) found = 1;
	} while (!found && FindNextFileA(h, &fd));
	FindClose(h);
	if (!found || str_len(fd.cFileName) + 24 > size) return 0;
	fmt(dir, "AEInstaller\\packages\\%s", fd.cFileName);
	str_cpy(folder, size, fd.cFileName);
	return 1;
}

/* Every package AE's depot knows (AEInstaller\PacManCache.xml: <name>, then <packageNumber>), for
 * a server's mods this PC doesn't have; read once */
#define DEPOT_MAX 1024
static struct { u32 number; char name[64]; } *g_depot;
static int g_ndepot = -1;

static void depot_load(void)
{
	HANDLE f;
	char *xml, *p;
	u32 got = 0, size = 4u << 20;

	g_ndepot = 0;
	if (!(g_depot = VirtualAlloc(0, DEPOT_MAX * sizeof *g_depot, 0x3000, 4)) || !(xml = VirtualAlloc(0, size + 1, 0x3000, 4))) return;
	f = CreateFileA("AEInstaller\\PacManCache.xml", 0x80000000, 3, 0, 3, 0x80, 0);
	if (f != INVALID_HANDLE_VALUE) {
		if (!ReadFile(f, xml, size, &got, 0)) got = 0;
		CloseHandle(f);
	}
	xml[got] = 0;
	for (p = xml; *p && g_ndepot < DEPOT_MAX; p++) {
		char *name, *q;
		u32 n = 0, k = 0;
		if (!str_ieq_n(p, "<Package><name>", 15)) continue;
		name = p + 15;
		for (q = name; *q && *q != '<'; q++) {}
		if (!str_ieq_n(q, "</name><packageNumber>", 22)) continue;
		for (const char *d = q + 22; *d >= '0' && *d <= '9'; d++) n = n * 10 + (u32)(*d - '0');
		/* the few XML entities names use */
		for (char *c = name; c < q && k + 1 < sizeof g_depot[0].name; ) {
			char ch = *c;
			if (ch == '&' && str_ieq_n(c, "&amp;", 5)) c += 5, ch = '&';
			else if (ch == '&' && str_ieq_n(c, "&apos;", 6)) c += 6, ch = '\'';
			else if (ch == '&' && str_ieq_n(c, "&quot;", 6)) c += 6, ch = '"';
			else if (ch == '&' && str_ieq_n(c, "&lt;", 4)) c += 4, ch = '<';
			else if (ch == '&' && str_ieq_n(c, "&gt;", 4)) c += 4, ch = '>';
			else c++;
			g_depot[g_ndepot].name[k++] = ch;
		}
		g_depot[g_ndepot].name[k] = 0;
		g_depot[g_ndepot++].number = n;
		p = q;
	}
	VirtualFree(xml, 0, 0x8000);
	log_msg("menu: %d packages in AE's depot list", g_ndepot);
}

/* A package's name: its Mod_Info.cfg here, else AE's depot list, else its folder's or number */
static void mod_name(u32 number, char *name, u32 size)
{
	char dir[MAX_PATH], folder[MAX_PATH];

	if (package_dir(number, dir, folder, sizeof folder)) {
		if (mod_info_name(dir, name, size)) return;
	} else {
		folder[0] = 0;
	}
	if (g_ndepot < 0) depot_load();
	for (int i = 0; i < g_ndepot; i++)
		if (g_depot[i].number == number && g_depot[i].name[0]) {
			str_cpy(name, size, g_depot[i].name);
			return;
		}
	if (folder[0]) {
		str_cpy(name, size, folder + 5);
		for (u32 i = 0; name[i]; i++) if (name[i] == '_') name[i] = ' ';
	} else {
		fmt(name, "Package %u", number);
	}
}

/* This PC's installed mods, in the order AE lists them (by package number); scanned once (a mod
 * is installed with Oni closed) */
typedef struct { u32 number, bytes; char name[64]; } LocalMod;
static LocalMod g_local[MAX_MODS];
static int g_nlocal = -1;

static void local_mods_scan(void)
{
	static char xml[65536];
	const char *p, *end;

	if (g_nlocal >= 0) return;
	g_nlocal = 0;
	if (!read_small("GameDataFolder\\installed_mods.xml", xml, sizeof xml)) {
		log_msg("menu: no GameDataFolder\\installed_mods.xml, no mods listed");
		return;
	}
	for (p = xml; *p && !str_ieq_n(p, "<mods>", 6); p++) {}
	for (end = p; *end && !str_ieq_n(end, "</mods>", 7); end++) {}
	while (p < end && g_nlocal < MAX_MODS) {
		if (str_ieq_n(p, "<int>", 5)) {
			u32 n = 0;
			for (p += 5; *p >= '0' && *p <= '9'; p++) n = n * 10 + (u32)(*p - '0');
			if (n >= CORE_PACKAGES) {
				LocalMod *m = &g_local[g_nlocal++];
				char dir[MAX_PATH], folder[MAX_PATH];
				unsigned long long b = package_dir(n, dir, folder, sizeof folder) ? folder_bytes(dir, sizeof dir, 0) : 0;
				m->number = n;
				m->bytes = b > 0xffffffffull ? 0xffffffffu : (u32)b;
				mod_name(n, m->name, sizeof m->name);
			}
		} else {
			p++;
		}
	}
	log_msg("menu: %d installed mods", g_nlocal);
}

u32 ui_installed_mods(u32 (*out)[2], u32 max)
{
	local_mods_scan();
	for (u32 i = 0; i < (u32)g_nlocal && i < max; i++) {
		out[i][0] = g_local[i].number;
		out[i][1] = g_local[i].bytes;
	}
	return (u32)g_nlocal;
}

/* The rows a mods list shows (the Host page: this PC's; the Join page: the selected server's) */

static void mods_rows_begin(void)
{
	g_nmods = 0;
	if (g_modsList) WMrMessage_Send(g_modsList, LBcMessage_Reset, 0, 0);
}

static void mods_row(const char *name, const char *size)
{
	ModRow *m;
	u32 index;

	if (g_nmods >= MAX_MODS + 1) return;
	m = &g_mods[g_nmods];
	str_cpy(m->name, sizeof m->name, name);
	str_cpy(m->size, sizeof m->size, size);
	if (g_modsList) {
		index = WMrMessage_Send(g_modsList, LBcMessage_AddString, (u32)m->name, 0);
		WMrMessage_Send(g_modsList, LBcMessage_SetItemData, (u32)g_nmods, index);
	}
	g_nmods++;
}

/* A mods list (strings, owner-drawn by mods_draw_row), without its box */
static WMtWindow *mods_list_add(WMtWindow *dialog, WMtWindow *anchor, WMtWindow *font, u32 id, int x, int y, int w, int h, int scroll)
{
	WMtWindow *l;

	if (TMrInstance_GetDataPtr(TAG_PSpc, "ps_none_none_none", &g_noArt)) g_noArt = 0;
	l = ui_add(dialog, anchor, font, WMcClass_ListBox, LBcStyle_HasStrings | LBcStyle_OwnerDraw | (scroll ? LBcStyle_HasScrollbar : 0),
	           "", id, x, y, w, h);
	if (l) {
		u32 *proc = (u32 *)(*(u8 **)((u8 *)l + 0x14) + 4);
		if (*proc != (u32)mods_list_proc) {
			g_listProc = (WMtProc)*proc;
			*proc = (u32)mods_list_proc;
		}
	}
	g_modsList = l;
	g_nmods = 0;
	return l;
}

/* How many rows the mods list shows at once */
static int mods_visible(void)
{
	u8 *p = g_modsList ? WMrWindow_GetLong(g_modsList, 0) : 0;
	return p && LB_VISIBLE(p) > 0 && LB_VISIBLE(p) < 64 ? (int)LB_VISIBLE(p) : MODS_ROWS;
}

/* A mods row: the name, the size at the right */
static int mods_draw_row(u32 p2)
{
	const u8 *d = (const u8 *)p2;
	u32 i = p2 ? *(const u32 *)(d + 0x1c) : 0xffff;   /* the item data */
	int width, sw;

	if (!p2) return 0;
	if (i >= (u32)g_nmods) return 1;
	width = *(const short *)(d + 0x14) - *(const short *)(d + 0x10);
	if (!g_mods[i].size[0]) {
		ui_list_draw_text(p2, 0, width - 6, g_mods[i].name, UI_ORANGE);
		return 1;
	}
	sw = ui_text_width(g_mods[i].size);
	ui_list_draw_text(p2, 0, width - sw - 20, g_mods[i].name, UI_ORANGE);
	ui_list_draw_text(p2, width - sw - 8, sw + 2, g_mods[i].size, UI_ORANGE);
	return 1;
}

/* ---- Mutators: the Host page's button opens a popup in Unreal Tournament's way: the available
 * mutators on the left, the enabled ones on the right, a double click moves one across, the
 * selected one's description under them, OK keeps the changes and Cancel (or Esc) drops them.
 * The New Game popup (dialog 154) made bigger, with two owner-drawn list boxes of our own. ---- */

typedef struct { u32 bit; const char *name, *about; } Mutator;
static const Mutator g_mutatorList[] = {
	{ NET_CHARS_MUTANT_MURO, "Mutant Muro", "Players may pick Mutant Muro in the character menu (F2)." },
	{ NET_CHARS_SHINATAMA, "Shinatama and Hasegawa", "Players may pick Shinatama and Hasegawa in the character menu (F2)." },
	{ NET_CHARS_MINI_STRIKER, "Mini Striker", "Players may pick the half-size Mini Striker in the character menu (F2)." },
	{ NET_MUT_DAODAN, "Daodan for All", "Overhealed, everyone gets Konoko's Daodan power and Muro's shield." },
	{ NET_MUT_NO_WEAPONS, "No Weapons", "No weapons in the map, and players spawn without one." },
	{ NET_MUT_FAST_SCREAMER, "Fast Screamer", "The Screaming Cannon's Screaming Cell flies twice as fast." },
	{ NET_MUT_FAST_PLASMA, "Fast Plasma", "The Plasma Rifle fires twice as fast." },
	{ NET_MUT_FAST_BALL, "Fast Ball", "The Super Ball Gun's ball flies twice as fast." },
	{ NET_MUT_BOMBS, "Bombs For Days", "Everyone dies as the Mad Bomber does: a blast 3 seconds later." },
};
#define MUTATORS ((int)(sizeof g_mutatorList / sizeof g_mutatorList[0]))
enum { ID_MUT_OK = 1, ID_MUT_CANCEL = 2, ID_MUT_AVAILABLE = 100, ID_MUT_ENABLED = 101 };
#define MUT_W 560
#define MUT_H 330

static u32 g_hostMutators, g_mutEdit;     /* the Host page's, and the popup's while it's open */
static WMtWindow *g_hostMutLabel, *g_mutList[2], *g_mutAbout;
static int g_mutSel[2] = { -1, -1 };
static void *g_mutHookOld;

static int mutators_count(u32 bits)
{
	int n = 0;
	for (int i = 0; i < MUTATORS; i++) n += (bits & g_mutatorList[i].bit) != 0;
	return n;
}

static void mutators_label(void)
{
	char text[32];
	int n = mutators_count(g_hostMutators);
	if (!n) str_cpy(text, sizeof text, "None");
	else fmt(text, "%d enabled", n);
	ui_set_text_fit(g_hostMutLabel, text);
}

/* The two lists from g_mutEdit; select: the mutator to select where it is now (-1 none) */
static void mutators_fill(int select)
{
	for (int l = 0; l < 2; l++) {
		if (!g_mutList[l]) continue;
		WMrMessage_Send(g_mutList[l], LBcMessage_Reset, 0, 0);
		for (int i = 0; i < MUTATORS; i++) {
			if (((g_mutEdit & g_mutatorList[i].bit) != 0) != (l == 1)) continue;
			u32 row = WMrMessage_Send(g_mutList[l], LBcMessage_AddString, (u32)g_mutatorList[i].name, 0);
			WMrMessage_Send(g_mutList[l], LBcMessage_SetItemData, (u32)i, row);
			if (i == select) WMrMessage_Send(g_mutList[l], LBcMessage_SetSelection, 0, row);
		}
		g_mutSel[l] = g_mutList[l] ? (int)WMrMessage_Send(g_mutList[l], LBcMessage_GetSelection, 0, 0) : -1;
	}
	if (select >= 0) ui_set_text_fit(g_mutAbout, g_mutatorList[select].about);
}

static int mutators_selected(int l)
{
	int row = g_mutList[l] ? (int)WMrMessage_Send(g_mutList[l], LBcMessage_GetSelection, 0, 0) : -1;
	u32 i = row >= 0 ? WMrMessage_Send(g_mutList[l], LBcMessage_GetItemData, 0, (u32)row) : 0xffffffffu;
	return i < (u32)MUTATORS ? (int)i : -1;
}

/* Every frame: the description follows whichever list's selection changed last */
static void mutators_frame(void)
{
	ui_frame_hook_call(g_mutHookOld);
	for (int l = 0; l < 2; l++) {
		int row = g_mutList[l] ? (int)WMrMessage_Send(g_mutList[l], LBcMessage_GetSelection, 0, 0) : -1;
		if (row == g_mutSel[l]) continue;
		g_mutSel[l] = row;
		int i = mutators_selected(l);
		if (i >= 0) ui_set_text_fit(g_mutAbout, g_mutatorList[i].about);
	}
}

static u8 ONI_FC mutators_proc(WMtWindow *dialog, u32 msg, u32 p1, u32 p2)
{
	u32 id = p1 & 0xffff, code = p1 >> 16;

	if (msg == WMcMessage_InitDialog) {
		/* the New Game popup: label (35,30), OK (id 1) and Cancel (id 2) 130x40, its outline (0,0) */
		WMtWindow *label = find_child(dialog, 35, 30);
		WMtWindow *ok = WMrDialog_GetItemByID(dialog, ID_MUT_OK);
		WMtWindow *cancel = WMrDialog_GetItemByID(dialog, ID_MUT_CANCEL);
		WMtWindow *outline = find_child(dialog, 0, 0);
		int gap = (MUT_W - 2 * BUTTON_W) / 3;

		g_mutList[0] = g_mutList[1] = g_mutAbout = 0;
		if (!label || !ok || !cancel || !outline) {
			log_msg("mutators: the New Game popup's controls are missing");
			return 1;
		}
		ui_set_frame(dialog, WIN_X(dialog) - (MUT_W - WIN_W(dialog)) / 2, WIN_Y(dialog) - (MUT_H - WIN_H(dialog)) / 2, MUT_W, MUT_H);
		ui_set_frame(outline, 0, 0, MUT_W, MUT_H);
		for (int k = 0; k < 2; k++) {   /* two more fills under it: the Host page hardly shows through */
			WMtWindow *b = ui_add(dialog, 0, outline, 0, ~0u, "", 0, 0, 0, MUT_W, MUT_H);
			if (b) win_insert_after(b, outline);
		}
		ui_label(dialog, label, label, "Mutators", 20, 12, 200, 20, UI_LEFT);
		ui_label(dialog, label, label, "Available", 25, 42, 240, 20, UI_LEFT);
		ui_label(dialog, label, label, "Enabled", 295, 42, 240, 20, UI_LEFT);
		/* with scroll bars: more mutators than rows, and the wheel (menu_wheel.c) only scrolls a list that has one */
		g_mutList[0] = ui_add(dialog, label, label, WMcClass_ListBox, LBcStyle_HasStrings | LBcStyle_OwnerDraw | LBcStyle_HasScrollbar,
		                      "", ID_MUT_AVAILABLE, 25, 66, 240, 160);
		g_mutList[1] = ui_add(dialog, label, label, WMcClass_ListBox, LBcStyle_HasStrings | LBcStyle_OwnerDraw | LBcStyle_HasScrollbar,
		                      "", ID_MUT_ENABLED, 295, 66, 240, 160);
		g_mutAbout = label;   /* the popup's own text line: the description */
		WIN_STYLE(label) &= ~0x60000u;   /* left */
		ui_set_frame(label, 25, 236, MUT_W - 50, 20);
		ui_set_text(label, "Double-click a mutator to move it to the other list.");
		ui_set_text(ok, "OK");
		ui_set_frame(ok, gap, 274, BUTTON_W, BUTTON_H);
		ui_set_text(cancel, "Cancel");
		ui_set_frame(cancel, 2 * gap + BUTTON_W, 274, BUTTON_W, BUTTON_H);
		mutators_fill(-1);
		if (g_mutList[0]) WMrWindow_SetFocus(g_mutList[0]);
		return 1;
	}
	if (msg == LBcMessage_DrawRow && (id == ID_MUT_AVAILABLE || id == ID_MUT_ENABLED)) return (u8)ui_list_draw_row(p2, UI_ORANGE);
	if (msg != WMcMessage_Command) return 0;
	if ((id == ID_MUT_AVAILABLE || id == ID_MUT_ENABLED) && code == WMcNotify_DoubleClick) {
		int i = mutators_selected(id == ID_MUT_ENABLED);
		if (i >= 0) {
			g_mutEdit ^= g_mutatorList[i].bit;   /* to the other list */
			log_msg("mutators: %s %s", g_mutatorList[i].name, g_mutEdit & g_mutatorList[i].bit ? "enabled" : "disabled");
			mutators_fill(i);
			WMrWindow_SetFocus(g_mutList[(g_mutEdit & g_mutatorList[i].bit) != 0]);
		}
		return 1;
	}
	if (code != WMcNotify_Click) return 1;
	if (id == ID_MUT_OK) WMrDialog_ModalEnd(dialog, 1);
	else if (id == ID_MUT_CANCEL) WMrDialog_ModalEnd(dialog, 0);
	return 1;
}

static void mutators_open(void)
{
	u32 ok = 0;

	g_mutEdit = g_hostMutators;
	g_mutSel[0] = g_mutSel[1] = -1;
	g_mutHookOld = ui_frame_hook(mutators_frame);
	WMrDialog_ModalBegin(WMcDialogID_NewGame, 0, mutators_proc, 0, &ok);
	ui_frame_hook(g_mutHookOld);
	g_mutList[0] = g_mutList[1] = g_mutAbout = 0;
	if (ok == 1) {
		g_hostMutators = g_mutEdit;
		mutators_label();
		log_msg("host: mutators OK: %x (%d on)", g_hostMutators, mutators_count(g_hostMutators));
	} else if (g_mutEdit != g_hostMutators) {
		log_msg("host: mutators cancelled, still %x", g_hostMutators);
	}
}

/* ---- The dialog ---- */

static int is_frame(WMtWindow *w, int y)
{
	return WIN_X(w) == 5 && WIN_W(w) == 300 && WIN_Y(w) == y;
}

/* Kept on the Host page: the background picture, the three frames (x 5, width 300) and the
 * Difficulty dropdown (the Map one) */
static int host_keeps(WMtWindow *w)
{
	return (WIN_X(w) == 0 && WIN_Y(w) == 0) || is_frame(w, 50) || is_frame(w, 175) || is_frame(w, 290) ||
	       WIN_ID(w) == ID_HOST_MAP;
}

static void host_status(const char *text)
{
	ui_set_text_fit(g_hostStatus, text);
}

/* The big frame's shade: a copy of it under it whose fill and outline are drawn at 60%, between
 * the stock frame and a doubled one (WM_Box.c: a box paints ps_oniUI_outline, all nine parts) */
#define HOST_SHADE_ALPHA 153
static WMtWindow *g_hostShade;
static WMtProc g_boxProc;

static u32 ONI_FC host_shade_proc(WMtWindow *w, u32 msg, u32 p1, u32 p2)
{
	u8 *ui;
	void *outline, *dc;
	short at[2] = { 0, 0 };

	if (!w || w != g_hostShade || msg != WMcMessage_Paint) return g_boxProc(w, msg, p1, p2);
	if ((ui = ONiUI_GetContext()) && (outline = *(void **)(ui + UI_OUTLINE)) && (dc = DCrDraw_Begin(w))) {
		DCrDrawPart(dc, outline, 0xffff, at, WIN_W(w), WIN_H(w), HOST_SHADE_ALPHA);
		DCrDraw_End(dc, w);
	}
	return 0;   /* WMcResult_Handled */
}

static void host_shade_add(WMtWindow *dialog, WMtWindow *frame)
{
	u32 *proc;

	g_hostShade = ui_add(dialog, frame, frame, 0, WIN_STYLE(frame) & ~0x40000u /* no title */, "", 0, WIN_X(frame), WIN_Y(frame),
	                     WIN_W(frame), WIN_H(frame));
	if (!g_hostShade) return;
	proc = (u32 *)(*(u8 **)((u8 *)g_hostShade + 0x14) + 4);
	if (*proc != (u32)host_shade_proc) {
		g_boxProc = (WMtProc)*proc;
		*proc = (u32)host_shade_proc;
	}
}

static void host_init(WMtWindow *dialog)
{
	WMtWindow *close = WMrDialog_GetItemByID(dialog, ID_HOST_CANCEL);
	WMtWindow *map = WMrDialog_GetItemByID(dialog, ID_HOST_MAP);
	WMtWindow *row = find_child(dialog, 30, 65);      /* "Quality:": precedes every frame */
	WMtWindow *header = find_child(dialog, 50, 35);   /* "Graphics" (blue) */
	WMtWindow *title = find_child(dialog, 5, 10);     /* "Options" */
	WMtWindow *frame[3] = { 0, 0, 0 }, *picture = 0, *hidden[48], *w;
	u32 n = 0;
	char name[NET_SERVER_NAME_MAX], number[16], fit[128], motd[NET_MOTD_MAX];
	void *splash = 0;

	g_hostStatus = g_hostDownload = g_hostMods = g_hostShade = 0;
	if (!close || !row || !header || !title || !map) {
		log_msg("host: the Options dialog's controls are missing");
		return;
	}
	for (w = WIN_FIRSTCHILD(dialog); w; w = WIN_NEXT(w)) {
		if (is_frame(w, 50)) frame[0] = w;
		else if (is_frame(w, 175)) frame[1] = w;
		else if (is_frame(w, 290)) frame[2] = w;
		else if (WIN_X(w) == 0 && WIN_Y(w) == 0) picture = w;
		if (!host_keeps(w) && w != close && n < 48) hidden[n++] = w;
	}
	if (!frame[0] || !frame[1] || !frame[2]) {
		log_msg("host: the Options dialog's frames are missing");
		return;
	}
	fields_begin(dialog, ID_HOST_GO);

	/* the splash screen's picture behind it all (level0's TXMB, 640x480 like the dialog) */
	if (picture && !TMrInstance_GetDataPtr(TAG_TXMB, "pict_splash_screen", &splash) && splash) PIC_TEXTURE(picture) = splash;
	else log_msg("host: no TXMBpict_splash_screen, the Options picture stays");

	/* The big frame. Every control goes after "Quality:", before the frames in the child list: a
	 * window after a frame is drawn under its dark fill and looks disabled. The mods frame goes
	 * there too (first, so every control comes before it): stock it comes after the big frame,
	 * whose fill dimmed its outline. */
	win_insert_after(frame[1], row);
	ui_set_frame(frame[0], 15, 25, 610, 390);
	host_shade_add(dialog, frame[0]);   /* darker: a 60% fill under it */
	ui_label(dialog, row, title, "Host Game", 60, 10, 150, 20, UI_LEFT);

	/* Server: Server name (its own row: a name needs the width), Port */
	ui_label(dialog, row, header, "Server", 55, 46, 150, 20, UI_LEFT);
	ui_label(dialog, row, row, "Server name:", 35, 74, 150, 20, UI_LEFT);
	ini_text("host", "name", "New Oni multiplayer game", name, sizeof name);
	add_field(dialog, row, row, ID_HOST_NAME, 35, 97, 262, NET_SERVER_NAME_MAX - 1, name, FIELD_TEXT, 0, 0);
	ui_label(dialog, row, row, "Port:", 35, 125, 60, 20, UI_LEFT);
	fmt(number, "%d", clamp((int)GetPrivateProfileIntA("net", "port", NET_DEFAULT_PORT, ini_path()), 1, 65535));
	add_field(dialog, row, row, ID_HOST_PORT, 100, 125, 80, 5, number, FIELD_NUMBER, 1, 65535);

	/* Rules: the player limit (spectators have none), the match's time and score limits (0: none;
	 * a negative score limit is reached by suicides), the message line */
	ui_label(dialog, row, header, "Rules", 55, 162, 150, 20, UI_LEFT);
	ui_label(dialog, row, row, "Max players:", 35, 190, 120, 20, UI_LEFT);
	fmt(number, "%d", clamp((int)GetPrivateProfileIntA("host", "max_players", NET_PLAYERS_MAX, ini_path()), 2, NET_PLAYERS_MAX));
	add_field(dialog, row, row, ID_HOST_PLAYERS, 165, 190, 60, 1, number, FIELD_NUMBER, 2, NET_PLAYERS_MAX);
	ui_label(dialog, row, row, "Time limit:", 35, 213, 120, 20, UI_LEFT);
	fmt(number, "%d", clamp((int)GetPrivateProfileIntA("host", "time_limit", 10, ini_path()), 0, NET_TIME_LIMIT_MAX));
	add_field(dialog, row, row, ID_HOST_TIME, 165, 213, 60, 3, number, FIELD_NUMBER, 0, NET_TIME_LIMIT_MAX);
	ui_label(dialog, row, row, "minutes", 233, 213, 67, 20, UI_LEFT);
	ui_label(dialog, row, row, "Score limit:", 35, 236, 120, 20, UI_LEFT);
	fmt(number, "%d", clamp((int)GetPrivateProfileIntA("host", "score_limit", 20, ini_path()), -NET_SCORE_LIMIT_MAX,
	                        NET_SCORE_LIMIT_MAX));
	add_field(dialog, row, row, ID_HOST_SCORE, 165, 236, 60, 4, number, FIELD_SIGNED, -NET_SCORE_LIMIT_MAX, NET_SCORE_LIMIT_MAX);
	ui_label(dialog, row, row, "points", 233, 236, 67, 20, UI_LEFT);
	g_hostStatus = ui_label(dialog, row, row, HOST_NOTE, 35, 259, 266, 20, UI_LEFT);

	/* Mutators (net.c, char_menu.c, mp_rules.c): the button opens their popup, the label says how
	 * many are on */
	g_hostMutators = net_host_char_rules_saved();
	ui_add(dialog, row, close, 0, ~0u, "Mutators", ID_HOST_MUTATORS, 35, 296, BUTTON_W, BUTTON_H);
	g_hostMutLabel = ui_label(dialog, row, row, "", 175, 306, 125, 20, UI_LEFT);
	mutators_label();

	/* Map (the dropdown) and what a player with no mods downloads to join: the map's folder and
	 * level0's differences (net.c) */
	ui_label(dialog, row, row, "Map:", 333, 79, 55, 20, UI_LEFT);
	win_insert_after(map, row);   /* stock, it comes after a frame: it would look disabled */
	WIN_STYLE(map) |= PMcStyle_NoResize;
	WMrPopupMenu_Reset(map);
	for (int i = 0; i < g_hostNmaps && i < HOST_MAPS_MAX; i++) {
		ui_fit(fit, sizeof fit, g_hostMaps[i], HOST_MAP_TEXT_W);
		WMrPopupMenu_AppendItem(map, (u32)i, fit);
	}
	if (g_hostSel >= HOST_MAPS_MAX) {   /* the one picked in the list is always there */
		ui_fit(fit, sizeof fit, g_hostMaps[g_hostSel], HOST_MAP_TEXT_W);
		WMrPopupMenu_AppendItem(map, (u32)g_hostSel, fit);
	}
	ui_set_frame(map, 393, 79, 205, WIN_H(map));
	map_menu_backing(map);
	ui_label(dialog, row, row, "New players download:", 333, 100, 168, 20, UI_LEFT);
	g_hostDownload = ui_label(dialog, row, row, "", 503, 100, 95, 20, UI_LEFT);
	WMrPopupMenu_SetSelection(map, (u32)g_hostSel);   /* (host_proc: the download line) */

	/* Current mods: the installed AE mods and their packages' sizes */
	ui_set_frame(frame[1], 327, 145, 277, 222);
	ui_label(dialog, row, title, "Current mods", 372, 130, 150, 20, UI_LEFT);
	local_mods_scan();
	g_hostMods = mods_list_add(dialog, row, row, ID_HOST_MODS, 337, 160, 257, 197, g_nlocal > MODS_ROWS);
	for (int i = 0; i < g_nlocal; i++) {
		char size[16];
		size_units(size, g_local[i].bytes);
		mods_row(g_local[i].name, size);
	}
	if (!g_nlocal) mods_row("(no mods installed)", "");

	/* The message of the day: a server line to each player who enters (net.c) */
	ui_label(dialog, row, row, "Message of the day:", 35, 380, 155, 20, UI_LEFT);
	ini_text("host", "motd", "", motd, sizeof motd);
	add_field(dialog, row, row, ID_HOST_MOTD, 195, 380, 410, NET_MOTD_MAX - 1, motd, FIELD_TEXT, 0, 0);

	/* Host and Dedicate, full size, under the big frame; Cancel left of the Bungie logo */
	ui_add(dialog, row, close, 0, ~0u, "Host", ID_HOST_GO, 30, 428, BUTTON_W, BUTTON_H);
	ui_add(dialog, row, close, 0, 0x30000, "Dedicate", ID_HOST_DEDICATE, 176, 428, BUTTON_W, BUTTON_H);
	ui_set_text(close, "Cancel");
	ui_set_frame(close, 440, 428, BUTTON_W, BUTTON_H);

	hidden[n < 48 ? n++ : 47] = frame[2];
	for (u32 i = 0; i < n; i++) WMrWindow_SetVisible(hidden[i], 0);
}

/* The fields, checked and saved; 0 (and why, on the dialog) if one isn't right */
static int host_read(WMtWindow *dialog, char *name, u32 *port, u32 *players, u32 *minutes, int *score, u32 *rules)
{
	char value[16];
	int v;

	edit_text(WMrDialog_GetItemByID(dialog, ID_HOST_NAME), name, NET_SERVER_NAME_MAX);
	if (!skip_space(name)[0]) str_cpy(name, NET_SERVER_NAME_MAX, "New Oni multiplayer game");
	if (!edit_number(WMrDialog_GetItemByID(dialog, ID_HOST_PORT), &v) || v < 1 || v > 65535) {
		host_status("Port: 1 to 65535.");
		return 0;
	}
	*port = (u32)v;
	if (!edit_number(WMrDialog_GetItemByID(dialog, ID_HOST_PLAYERS), &v) || v < 2 || v > NET_PLAYERS_MAX) {
		host_status("Max players: 2 to " HOST_STR(NET_PLAYERS_MAX) ".");
		return 0;
	}
	*players = (u32)v;
	if (!edit_number(WMrDialog_GetItemByID(dialog, ID_HOST_TIME), &v) || v < 0 || v > NET_TIME_LIMIT_MAX) {
		host_status("Time limit: 0 (none) to 999 minutes.");
		return 0;
	}
	*minutes = (u32)v;
	if (!edit_number(WMrDialog_GetItemByID(dialog, ID_HOST_SCORE), &v) || v < -NET_SCORE_LIMIT_MAX || v > NET_SCORE_LIMIT_MAX) {
		host_status("Score limit: -999 to 999 (0: none).");
		return 0;
	}
	*score = v;
	*rules = g_hostMutators & NET_MUTATORS_ALL;
	host_status(HOST_NOTE);

	ini_text_write("host", "name", name);
	fmt(value, "%d", *port);
	WritePrivateProfileStringA("net", "port", value, ini_path());
	fmt(value, "%d", *players);
	WritePrivateProfileStringA("host", "max_players", value, ini_path());
	WritePrivateProfileStringA("host", "max_spectators", 0, ini_path());   /* an older Arklite's limit goes */
	fmt(value, "%d", *minutes);
	WritePrivateProfileStringA("host", "time_limit", value, ini_path());
	fmt(value, "%d", *score);
	WritePrivateProfileStringA("host", "score_limit", value, ini_path());
	WritePrivateProfileStringA("host", "mutant_muro", *rules & NET_CHARS_MUTANT_MURO ? "1" : "0", ini_path());
	WritePrivateProfileStringA("host", "shinatama_hasegawa", *rules & NET_CHARS_SHINATAMA ? "1" : "0", ini_path());
	WritePrivateProfileStringA("host", "mini_striker", *rules & NET_CHARS_MINI_STRIKER ? "1" : "0", ini_path());
	WritePrivateProfileStringA("host", "daodan", *rules & NET_MUT_DAODAN ? "1" : "0", ini_path());
	WritePrivateProfileStringA("host", "no_weapons", *rules & NET_MUT_NO_WEAPONS ? "1" : "0", ini_path());
	WritePrivateProfileStringA("host", "fast_screamer", *rules & NET_MUT_FAST_SCREAMER ? "1" : "0", ini_path());
	WritePrivateProfileStringA("host", "fast_plasma", *rules & NET_MUT_FAST_PLASMA ? "1" : "0", ini_path());
	WritePrivateProfileStringA("host", "bombs", *rules & NET_MUT_BOMBS ? "1" : "0", ini_path());
	WritePrivateProfileStringA("host", "fast_ball", *rules & NET_MUT_FAST_BALL ? "1" : "0", ini_path());
	{
		char motd[NET_MOTD_MAX];
		edit_text(WMrDialog_GetItemByID(dialog, ID_HOST_MOTD), motd, sizeof motd);
		ini_text_write("host", "motd", skip_space(motd));   /* net_menu_host reads it */
	}
	return 1;
}

static u8 ONI_FC host_proc(WMtWindow *dialog, u32 msg, u32 p1, u32 p2)
{
	u32 id = p1 & 0xffff, code = p1 >> 16;
	char name[NET_SERVER_NAME_MAX];
	u32 port, players, minutes, rules;
	int score;

	if (msg == WMcMessage_InitDialog) {
		host_init(dialog);
		return 1;
	}
	if (msg == WMcMessage_MenuCommand) {   /* the Map dropdown */
		if (p2 && WIN_ID((WMtWindow *)p2) == ID_HOST_MAP) host_map_select((int)id);
		return 1;
	}
	if (msg == LBcMessage_DrawRow && id == ID_HOST_MODS) return (u8)mods_draw_row(p2);
	if (msg != WMcMessage_Command) return 0;
	if (code != WMcNotify_Click) return 1;
	if (id == ID_HOST_GO) {
		if (!host_read(dialog, name, &port, &players, &minutes, &score, &rules)) return 1;
		net_menu_host(port, name, players, minutes, score, rules);
		WMrDialog_ModalEnd(dialog, 1);
	} else if (id == ID_HOST_DEDICATE) {
		/* a dedicated server: Oni's window goes, a console takes the admin's commands (server.c) */
		if (!host_read(dialog, name, &port, &players, &minutes, &score, &rules)) return 1;
		net_menu_dedicated(port, name, players, minutes, score, rules);
		WMrDialog_ModalEnd(dialog, 1);
	} else if (id == ID_HOST_MUTATORS) {
		mutators_open();
	} else if (id == ID_HOST_CANCEL) {
		WMrDialog_ModalEnd(dialog, 0);
	}
	return 1;
}

static void host_frame(void)
{
	ui_frame_hook_call(g_hostHookOld);
	fields_frame();
	host_download_update();   /* level0 is measured on a thread */
}

int mp_menu_host(const char *const *maps, int nmaps, int start, int (*map_dir)(int, char *, u32))
{
	u32 go = 0;

	if (nmaps <= 0 || start < 0 || start >= nmaps) return -1;
	g_hostMaps = maps;
	g_hostNmaps = nmaps;
	g_hostSel = start;
	g_hostMapDir = map_dir;
	g_hostHookOld = ui_frame_hook(host_frame);
	WMrDialog_ModalBegin(WMcDialogID_Options, 0, host_proc, 0, &go);
	ui_frame_hook(g_hostHookOld);
	fields_end();
	g_hostDownload = g_hostMods = g_mapMenu = g_modsList = g_hostShade = 0;
	return go == 1 ? g_hostSel : -1;
}

/* ---- Join ----------------------------------------------------------------------------------- */

enum { ID_JOIN_PLAY = 1, ID_JOIN_BACK = 2, ID_JOIN_ADD = 3, ID_JOIN_REMOVE = 4, ID_JOIN_REFRESH = 5, ID_JOIN_LIST = 100,
       ID_JOIN_ADDRESS = 120, ID_JOIN_MODS = 130 };
#define JOIN_COLUMNS    3      /* Ping, Name, Players: drawn into each row of the one list */
#define MAX_BOOKMARKS   64
#define ADDRESS_MAX     64
#define ASK_MS          2000   /* every server in the list */
#define ASK_SELECTED_MS 1000
#define NO_ANSWER_MS    3000
#define RESOLVE_WAIT_MS 500    /* after the last key typed in Address */
#define LAN_TOKEN       0x7fffffffu   /* broadcast questions: the servers on this PC and the network */
#define LIST_MS         10000  /* the helper's server list (helper.c), asked again this often */
#define INTRO_MS        3000   /* a server that doesn't answer: the helper introduces this PC to it this often */
#define HELPED_WAIT_MS  8000   /* ...and once the helper told it, this long before the list says it can't be reached */
#define MAX_IGNORED     16
#define TEXT_MAX        256

typedef struct {
	char address[ADDRESS_MAX];
	u32 token;                 /* this entry's questions (new with every address) */
	int resolved;              /* 0 not yet, 1 addr/port, -1 not found */
	u32 addr;
	u16 port;
	u32 changed_at, first_ask, last_ask, last_reply;
	int have_info, have_map;
	int found;                 /* answered a broadcast or came from the server list; not saved unless its address is edited */
	int listed;                /* came from the server list: info is the list's until the server answers itself */
	int helper_knows;          /* the helper knows this server (1), doesn't (-1), hasn't said (0) */
	u32 last_intro, helped_at;
	NetServerInfo info;
	char cells[JOIN_COLUMNS][128];   /* what the columns show */
} Bookmark;

static Bookmark g_marks[MAX_BOOKMARKS];
static int g_nmarks;
static u32 g_token;
/* The list (the dialog's own, owner-drawn) and its selection as last seen */
static WMtWindow *g_jlist;
static int g_jsel = -1;
static const int g_colX[JOIN_COLUMNS] = { 0, 59, 246 }, g_colW[JOIN_COLUMNS] = { 50, 180, 52 };

static int list_selected(void)
{
	return g_jlist ? (int)WMrMessage_Send(g_jlist, LBcMessage_GetSelection, 0, 0) : -1;
}

static void list_select(int row)
{
	if (g_jlist) WMrMessage_Send(g_jlist, LBcMessage_SetSelection, 0, (u32)row);
	g_jsel = row;
}
static WMtWindow *g_jdialog, *g_address, *g_infoName, *g_infoMap, *g_infoConnected, *g_jstatus, *g_jtotal;
static char g_shown[5][TEXT_MAX];
static u32 g_jmodsKey = 1;         /* what the mods list shows (join_mods_key), 1: nothing yet */
static void *g_joinHookOld;
static char *g_joinFolder;
static u32 g_joinFolderSize, *g_joinLevel;
static u32 g_resolving;            /* the token whose address net.c's thread is looking up, 0: none */
static u32 g_lastBroadcast, g_lastList;
static u32 g_ignored[MAX_IGNORED]; /* found servers removed in this dialog (server ids) */
static char g_failedFolder[64], g_failedWhy[96];   /* the last map download that stopped, and why */
static int g_nignored;

static void bookmark_reset(Bookmark *b)
{
	b->token = ++g_token;
	b->resolved = 0;
	b->changed_at = GetTickCount();
	b->first_ask = b->last_ask = b->last_reply = 0;
	b->have_info = b->have_map = 0;
	b->listed = b->helper_knows = 0;
	b->last_intro = b->helped_at = 0;
}

static void bookmarks_load(void)
{
	char key[16];
	int n = clamp((int)GetPrivateProfileIntA("servers", "count", 0, ini_path()), 0, MAX_BOOKMARKS);

	g_nmarks = 0;
	for (int i = 0; i < n; i++) {
		Bookmark *b = &g_marks[g_nmarks];
		memset(b, 0, sizeof *b);
		fmt(key, "%d", i + 1);
		GetPrivateProfileStringA("servers", key, "", b->address, sizeof b->address, ini_path());
		bookmark_reset(b);
		b->changed_at = 0;
		g_nmarks++;
	}
}

static void bookmarks_save(void)
{
	char key[16];
	int n = 0;

	WritePrivateProfileStringA("servers", 0, 0, ini_path());   /* the old list goes */
	for (int i = 0; i < g_nmarks; i++) {
		if (g_marks[i].found) continue;
		fmt(key, "%d", ++n);
		WritePrivateProfileStringA("servers", key, g_marks[i].address, ini_path());
	}
	fmt(key, "%d", n);
	WritePrivateProfileStringA("servers", "count", key, ini_path());
}

/* What the columns say for b */
static void bookmark_cells(const Bookmark *b, u32 now, char (*cells)[128])
{
	int waiting = b->resolved == 1 && (!b->first_ask || now - b->first_ask < NO_ANSWER_MS);

	if (!b->address[0]) cells[0][0] = 0;
	else if (b->resolved < 0) str_cpy(cells[0], 128, "?");
	else if (b->have_info && now - b->last_reply < NO_ANSWER_MS + ASK_MS) fmt(cells[0], "%d", b->info.ping_ms);
	else str_cpy(cells[0], 128, (waiting || !b->resolved) ? "..." : "-");

	if ((b->have_info || b->listed) && b->info.same_version && b->info.name[0]) str_cpy(cells[1], 128, b->info.name);
	else if (b->address[0]) str_cpy(cells[1], 128, b->address);
	else str_cpy(cells[1], 128, "(new server)");

	if ((b->have_info || b->listed) && b->info.same_version) fmt(cells[2], "%d/%d", b->info.players, b->info.max_players);
	else cells[2][0] = 0;
}

static void join_add_row(int i)
{
	u32 index;
	bookmark_cells(&g_marks[i], GetTickCount(), g_marks[i].cells);
	if (!g_jlist) return;
	index = WMrMessage_Send(g_jlist, LBcMessage_AddString, (u32)g_marks[i].address, 0);
	WMrMessage_Send(g_jlist, LBcMessage_SetItemData, (u32)i, index);
}

static void show_once(int slot, WMtWindow *w, const char *text)
{
	if (str_len(g_shown[slot]) == str_len(text) && str_ieq_n(g_shown[slot], text, sizeof g_shown[slot])) return;
	str_cpy(g_shown[slot], sizeof g_shown[slot], text);
	ui_set_text_fit(w, text);
}

/* Why the selected server can't be joined, or its map; 1 if Play may go. The line is cut to its
 * label's width by show_once. */
static int join_status(const Bookmark *b, u32 now, char *text)
{
	if (!b) {
		str_cpy(text, TEXT_MAX, "Select a server, or add one with +.");
		return 0;
	}
	if (!b->address[0]) {
		str_cpy(text, TEXT_MAX, "Type its address: an IP or a name, :port if not 27777.");
		return 0;
	}
	if (b->resolved < 0) {
		str_cpy(text, TEXT_MAX, "Can't find that address.");
		return 0;
	}
	if (!b->have_info && b->helper_knows > 0) {   /* the helper told the host: it knocks back (helper.c) */
		str_cpy(text, TEXT_MAX, now - b->helped_at < HELPED_WAIT_MS ? "Reaching the host through the server list..."
		                                                            : "Can't get through to that host from this network.");
		return 0;
	}
	if (!b->have_info) {
		char me[32];
		if (b->first_ask && now - b->first_ask >= NO_ANSWER_MS && net_my_address(me))   /* net.c "Hole punching" */
			/* (no address on screen: players stream; /ip shows it in a game) */
			str_cpy(text, TEXT_MAX, "No answer: the host may need to /punch you (/ip).");
		else
			str_cpy(text, TEXT_MAX, b->first_ask && now - b->first_ask >= NO_ANSWER_MS ? "No answer: no game hosted there, or a firewall."
			                                                                          : "Asking the server...");
		return 0;
	}
	if (!b->info.same_version) {
		str_cpy(text, TEXT_MAX, "It runs another version of Arklite.");
		return 0;
	}
	if (!b->have_map && !b->info.folder[0]) {   /* an original level: nothing to download */
		str_cpy(text, TEXT_MAX, "You don't have its map: ");
		str_cpy(text + str_len(text), TEXT_MAX - str_len(text), b->info.map);
		return 0;
	}
	if (!b->have_map) {   /* a custom level: Play downloads it (net_map_fetch) */
		if (g_failedWhy[0] && str_ieq_n(g_failedFolder, b->info.folder, sizeof g_failedFolder))
			fmt(text, "Download failed (%s). Play tries again.", g_failedWhy);
		else
			fmt(text, "You don't have its map (%s): Play downloads it.", b->info.folder);
		return 1;
	}
	if (b->info.players >= b->info.max_players) {
		str_cpy(text, TEXT_MAX, "The server is full.");
		return 0;
	}
	str_cpy(text, TEXT_MAX, b->info.in_match ? "Match on: you'll wait for the next one." : "Ready: Play joins it.");
	return 1;
}

static void join_delete(int row)
{
	if (g_jlist) WMrMessage_Send(g_jlist, LBcMessage_DeleteString, 0, (u32)row);
	g_jsel = list_selected();
	for (int i = row; i + 1 < g_nmarks; i++) g_marks[i] = g_marks[i + 1];
	g_nmarks--;
}

static int ignored(u32 server_id)
{
	for (int i = 0; i < g_nignored; i++)
		if (g_ignored[i] == server_id) return 1;
	return 0;
}

static Bookmark *join_selected(void)
{
	int row = list_selected();
	return row >= 0 && row < g_nmarks ? &g_marks[row] : 0;
}

/* What Address shows of entry b: a game from the server list shows none (its internet address:
 * players stream); typing one makes the row a saved server, as for any other */
static const char *join_address_of(const Bookmark *b)
{
	return b && !b->listed ? b->address : "";
}

static char g_addressShown[ADDRESS_MAX];   /* what join_show_selected put there last */

/* Address shows the selected entry's, whatever was typed before */
static void join_show_selected(void)
{
	str_cpy(g_addressShown, sizeof g_addressShown, join_address_of(join_selected()));
	WMrMessage_Send(g_address, EFcMessage_SetText, (u32)g_addressShown, 0);
}

static void join_mods_show(const Bookmark *b, char *text);

static void join_frame(void)
{
	u32 now = GetTickCount();
	NetServerInfo info;
	Bookmark *sel;
	char typed[ADDRESS_MAX], text[TEXT_MAX];

	ui_frame_hook_call(g_joinHookOld);
	if (!g_jdialog) return;
	fields_frame();
	if (list_selected() != g_jsel) {
		g_jsel = list_selected();
		join_show_selected();
	}
	sel = join_selected();

	/* Address: a key typed there edits the selected entry, or adds one (only keys count: a new
	 * field's text starts as whatever its uninitialized buffer held) */
	if (g_typed == g_address) {
		g_typed = 0;
		edit_text(g_address, typed, sizeof typed);
		if (!sel && typed[0] && g_nmarks < MAX_BOOKMARKS) {
			Bookmark *b = &g_marks[g_nmarks];
			memset(b, 0, sizeof *b);
			bookmark_reset(b);
			join_add_row(g_nmarks++);
			list_select(g_nmarks - 1);
			sel = b;
		}
		str_cpy(g_addressShown, sizeof g_addressShown, typed);
		if (sel && !(str_len(typed) == str_len(sel->address) && str_ieq_n(typed, sel->address, sizeof typed))) {
			str_cpy(sel->address, sizeof sel->address, typed);
			sel->found = 0;
			bookmark_reset(sel);
			bookmarks_save();
		}
	}

	/* Addresses are looked up on net.c's thread, one at a time, so a slow name never stops the menu */
	if (g_resolving) {
		u32 addr;
		u16 port;
		int r = net_resolve_poll(&addr, &port);
		if (r >= 0) {
			for (int i = 0; i < g_nmarks; i++) {
				Bookmark *b = &g_marks[i];
				if (b->token != g_resolving || b->resolved) continue;
				b->resolved = r ? 1 : -1;
				b->addr = addr;
				b->port = port;
			}
			g_resolving = 0;
		}
	} else {
		u32 addr;
		u16 port;
		net_resolve_poll(&addr, &port);   /* one left from an earlier dialog */
		for (int i = 0; i < g_nmarks; i++) {
			Bookmark *b = &g_marks[i];
			if (!b->address[0] || b->resolved || now - b->changed_at < RESOLVE_WAIT_MS) continue;
			if (net_resolve_start(b->address)) g_resolving = b->token;
			break;
		}
	}

	for (int i = 0; i < g_nmarks; i++) {
		Bookmark *b = &g_marks[i];
		if (b->resolved == 1 && b->address[0] && (!b->last_ask || now - b->last_ask >= (b == sel ? ASK_SELECTED_MS : ASK_MS))) {
			net_query_send(b->token, b->addr, b->port);
			if (!b->first_ask) b->first_ask = now ? now : 1;
			b->last_ask = now;
		}
	}
	if (!g_lastBroadcast || now - g_lastBroadcast >= ASK_MS) {
		net_query_broadcast(LAN_TOKEN);
		g_lastBroadcast = now ? now : 1;
	}

	/* The helper's server list (helper.c): games outside this network, shown as found ones */
	if (!g_lastList || now - g_lastList >= LIST_MS) {
		net_helper_list();
		g_lastList = now ? now : 1;
	}
	while (helper_list_next(&info)) {
		Bookmark *b = 0;
		int i;
		if (ignored(info.server_id)) continue;
		for (i = 0; i < g_nmarks && !b; i++)
			if (((g_marks[i].have_info && g_marks[i].info.same_version) || g_marks[i].listed) && g_marks[i].info.server_id == info.server_id)
				b = &g_marks[i];
		for (i = 0; i < g_nmarks && !b; i++)
			if (g_marks[i].resolved == 1 && g_marks[i].addr == info.from_addr && g_marks[i].port == info.from_port) b = &g_marks[i];
		if (b) {   /* listed already, or answering itself: only what the list knows better */
			if (!b->have_info) b->info = info, b->listed = 1;
			continue;
		}
		if (g_nmarks >= MAX_BOOKMARKS) continue;
		{
			const u8 *ip = (const u8 *)&info.from_addr;
			b = &g_marks[g_nmarks];
			memset(b, 0, sizeof *b);
			bookmark_reset(b);
			fmt(b->address, "%d.%d.%d.%d:%d", ip[0], ip[1], ip[2], ip[3], ((info.from_port & 0xff) << 8) | (info.from_port >> 8));
			b->found = b->listed = 1;
			b->resolved = 1;
			b->addr = info.from_addr;
			b->port = info.from_port;
			b->info = info;
			b->have_map = info.folder[0] ? custom_game_has_folder(info.folder) : info.level < 127 && TMgLevelValid[info.level];
			join_add_row(g_nmarks++);
			if (!sel) {
				list_select(g_nmarks - 1);
				join_show_selected();
				sel = b;
			}
		}
	}
	/* A server that doesn't answer (or one the list brought): the helper tells it this PC's address
	 * and it knocks back, opening its NAT; the answer says where it really is */
	for (int i = 0; i < g_nmarks; i++) {
		Bookmark *b = &g_marks[i];
		int quiet = !b->have_info || now - b->last_reply >= NO_ANSWER_MS;
		if (b->resolved != 1 || !quiet || (!b->listed && (!b->first_ask || now - b->first_ask < 500))) continue;
		if (b->last_intro && now - b->last_intro < INTRO_MS) continue;
		net_helper_intro(b->addr, b->port);
		b->last_intro = now ? now : 1;
	}
	{
		u32 asked, addr;
		u16 asked_port, port;
		int known;
		while (net_helper_intro_ack(&asked, &asked_port, &known, &addr, &port)) {
			for (int i = 0; i < g_nmarks; i++) {
				Bookmark *b = &g_marks[i];
				if (b->resolved != 1 || b->addr != asked || b->port != asked_port) continue;
				if (known && b->helper_knows <= 0) b->helped_at = now;
				b->helper_knows = known ? 1 : -1;
				if (known && !b->have_info && (addr != b->addr || port != b->port)) {   /* a CGNAT host's real port */
					b->addr = addr;
					b->port = port;
					b->last_intro = 0;
					b->first_ask = 0;
				}
			}
		}
	}

	while (net_query_poll(&info)) {
		Bookmark *b = 0;
		int i;
		for (i = 0; i < g_nmarks && !b; i++)
			if (g_marks[i].token == info.token) b = &g_marks[i];
		/* a broadcast's answer: a server already listed (by its id or its address), or a new one */
		for (i = 0; i < g_nmarks && !b && info.same_version && info.server_id; i++)
			if ((g_marks[i].have_info || g_marks[i].listed) && g_marks[i].info.server_id == info.server_id) b = &g_marks[i];
		if (b && b->listed && b->found && info.token == LAN_TOKEN && (b->addr != info.from_addr || b->port != info.from_port)) {
			/* a listed game on this network: its own address here (the internet one may not loop back) */
			const u8 *ip = (const u8 *)&info.from_addr;
			u32 port = ((info.from_port & 0xff) << 8) | (info.from_port >> 8);
			if (port == NET_DEFAULT_PORT) fmt(b->address, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
			else fmt(b->address, "%d.%d.%d.%d:%d", ip[0], ip[1], ip[2], ip[3], port);
			b->addr = info.from_addr;
			b->port = info.from_port;
			b->listed = 0;
			log_msg("join: a game from the server list answered from this network: its local address shows%s",
			        b == sel ? " (the selected one)" : "");
		}
		for (i = 0; i < g_nmarks && !b; i++)
			if (g_marks[i].resolved == 1 && g_marks[i].addr == info.from_addr && g_marks[i].port == info.from_port) b = &g_marks[i];
		if (!b && info.token == LAN_TOKEN && info.same_version && !ignored(info.server_id) && g_nmarks < MAX_BOOKMARKS) {
			const u8 *ip = (const u8 *)&info.from_addr;
			u32 port = ((info.from_port & 0xff) << 8) | (info.from_port >> 8);
			b = &g_marks[g_nmarks];
			memset(b, 0, sizeof *b);
			bookmark_reset(b);
			if (port == NET_DEFAULT_PORT) fmt(b->address, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
			else fmt(b->address, "%d.%d.%d.%d:%d", ip[0], ip[1], ip[2], ip[3], port);
			b->found = 1;
			b->resolved = 1;
			b->addr = info.from_addr;
			b->port = info.from_port;
			join_add_row(g_nmarks++);
			if (!sel) {
				list_select(g_nmarks - 1);
				join_show_selected();
				sel = b;
			}
		}
		if (!b) continue;
		/* the map is looked for when it changes, not every reply */
		if (!b->have_info || !str_ieq_n(b->info.folder, info.folder, sizeof info.folder) || b->info.level != info.level)
			b->have_map = info.folder[0] ? custom_game_has_folder(info.folder) : info.level < 127 && TMgLevelValid[info.level];
		b->info = info;
		b->have_info = 1;
		b->last_reply = now;
		/* a saved server that a broadcast found first: the found row goes */
		for (i = 0; i < g_nmarks && !b->found && info.server_id; i++) {
			Bookmark *o = &g_marks[i];
			if (o == b || !o->found || !o->have_info || o->info.server_id != info.server_id) continue;
			if (sel == o) sel = 0;
			if (b > o) b--;
			join_delete(i);
			if (g_jsel < 0 && g_nmarks) list_select((int)(b - g_marks));
			break;
		}
	}
	sel = join_selected();

	/* The selected row's address changed under the field (a server-list game, its address hidden,
	 * answered the network broadcast: it's on this network, and its local address shows now; the
	 * list often brings it first when Join opens again): the field says so, unless it's being typed in */
	{
		const char *want = join_address_of(sel);
		u32 k = 0;
		while (want[k] && want[k] == g_addressShown[k]) k++;
		if ((want[k] || g_addressShown[k]) && WMgFocus != g_address) join_show_selected();
	}

	/* what each row shows; the list draws it (join_proc, msg 0x36) */
	for (int i = 0; i < g_nmarks; i++) bookmark_cells(&g_marks[i], now, g_marks[i].cells);

	/* The selected server */
	{
		int known = sel && sel->have_info && sel->info.same_version;
		if (known) str_cpy(text, sizeof text, sel->info.name);
		show_once(0, g_infoName, known ? text : "-");
		if (known) str_cpy(text, sizeof text, sel->info.map[0] ? sel->info.map : sel->info.folder);
		show_once(1, g_infoMap, known && text[0] ? text : "-");
		if (known && sel->info.spectators) fmt(text, "%d/%d (%d spectating)", sel->info.players, sel->info.max_players, sel->info.spectators);
		else if (known) fmt(text, "%d/%d", sel->info.players, sel->info.max_players);
		show_once(2, g_infoConnected, known ? text : "-");
		join_status(sel, now, text);
		show_once(3, g_jstatus, text);
		join_mods_show(known ? sel : 0, text);
		show_once(4, g_jtotal, text);
	}
}

/* ---- The selected server's mods and this PC's download to join it ---- */

static u32 join_mods_key(const Bookmark *b)
{
	u32 h = 0x811c9dc5u;
	if (!b || !b->info.has_download) return 0;
	h = (h ^ b->token) * 0x01000193u;
	h = (h ^ b->info.nmods) * 0x01000193u;
	for (u32 i = 0; i < b->info.nmods && i < NET_INFO_MODS; i++)
		h = ((h ^ b->info.mods[i][0]) * 0x01000193u ^ b->info.mods[i][1]) * 0x01000193u;
	return h | 2;
}

/* The Current mods rows (rebuilt only when they change) and the Total download line in text */
static void join_mods_show(const Bookmark *b, char *text)
{
	u32 key = join_mods_key(b), bytes, shown, rows;
	char size[24];

	if (key != g_jmodsKey) {
		g_jmodsKey = key;
		mods_rows_begin();
		if (key) {
			shown = b->info.nmods < NET_INFO_MODS ? b->info.nmods : NET_INFO_MODS;
			rows = (u32)mods_visible();
			if (b->info.nmods > rows && rows > 1) shown = shown < rows - 1 ? shown : rows - 1;   /* the last row says how many more */
			for (u32 i = 0; i < shown; i++) {
				char name[64];
				mod_name(b->info.mods[i][0], name, sizeof name);
				size_units(size, b->info.mods[i][1]);
				mods_row(name, size);
			}
			if (b->info.nmods > shown) {
				char more[48];
				fmt(more, "...and %u more", b->info.nmods - shown);
				mods_row(more, "");
			}
			if (!b->info.nmods) mods_row("(no mods installed)", "");
		}
	}
	if (!b || !b->info.has_download) {
		str_cpy(text, TEXT_MAX, "Total download: -");
		return;
	}
	int how = net_join_download(&b->info, &bytes);
	{   /* the log says it when it changes: which server, what it needs and why */
		static u32 said_token, said_bytes;
		static int said_how = -1;
		if (b->token != said_token || how != said_how || bytes != said_bytes) {
			said_token = b->token;
			said_how = how;
			said_bytes = bytes;
			log_msg("join: \"%s\" (%s): %d mods; download %s %u bytes (host level0 %08x %08x, new player's %u; map %s %u bytes%s)",
			        b->info.name, b->info.map, b->info.nmods,
			        how == NETDL_EXACT ? "exactly" : how == NETDL_ABOUT ? "about" : how == NETDL_MEASURING ? "measuring" :
			        how == NETDL_IMPOSSIBLE ? "impossible" : "unknown", bytes, b->info.l0[0], b->info.l0[1], b->info.l0_bytes,
			        b->info.folder[0] ? b->info.folder : "(original level)", b->info.map_bytes, b->info.map_ok ? "" : ", can't be sent");
		}
	}
	switch (how) {
	case NETDL_EXACT:
		if (!bytes) str_cpy(text, TEXT_MAX, "No download needed.");
		else size_units(size, bytes), fmt(text, "Total download: %s", size);
		break;
	case NETDL_ABOUT:
		size_units(size, bytes);
		fmt(text, "Total download: about %s", size);
		if (g_jtotal && ui_text_width(text) > WIN_W(g_jtotal)) fmt(text, "Total download: ~%s", size);   /* 3-digit MB */
		break;
	case NETDL_MEASURING: str_cpy(text, TEXT_MAX, "Total download: measuring..."); break;
	case NETDL_IMPOSSIBLE: str_cpy(text, TEXT_MAX, "Total download: not possible"); break;
	default: str_cpy(text, TEXT_MAX, "Total download: unknown"); break;
	}
}

/* Refresh: the games found (on the network, or from the server list) go, and come back as they
 * answer again (one that ended stays gone); the saved servers are asked again at once, and
 * everything is introduced again (helper.c) */
static void join_refresh(void)
{
	for (int i = g_nmarks - 1; i >= 0; i--)
		if (g_marks[i].found) join_delete(i);
	for (int i = 0; i < g_nmarks; i++) {
		Bookmark *b = &g_marks[i];
		b->first_ask = b->last_ask = b->last_reply = 0;
		b->have_info = b->listed = b->helper_knows = 0;
		b->last_intro = b->helped_at = 0;
		if (b->resolved < 0) b->resolved = 0;   /* a name not found: looked up again */
	}
	g_lastBroadcast = g_lastList = 0;
	if (g_nmarks) list_select(0);
	else g_jsel = -1;
	join_show_selected();
	log_msg("join: refreshed (%d saved server%s)", g_nmarks, g_nmarks == 1 ? "" : "s");
}

/* The Load Game dialog (151, 640x480, its kanji picture kept) laid out as the user's mockup: the
 * servers' frame on the left (title "Join Game" by its tab), the selected server's frame under it,
 * a "Current mods" frame on the right (the selected server's mods, named from this PC's packages or
 * AE's depot list, each with its package's size on the host's disk) ending with "Total download:"
 * (net_join_download), and Play, Refresh, Back centered under them. */
static void join_init(WMtWindow *dialog)
{
	static const char *const headers[JOIN_COLUMNS] = { "Ping", "Name", "Players" };
	static const char *const rows[4] = { "Name:", "Map:", "Connected:", "Address:" };
	WMtWindow *lb = WMrDialog_GetItemByID(dialog, 100);
	WMtWindow *play = WMrDialog_GetItemByID(dialog, ID_JOIN_PLAY);
	WMtWindow *back = WMrDialog_GetItemByID(dialog, ID_JOIN_BACK);
	WMtWindow *outline = find_child(dialog, 120, 10);
	WMtWindow *inspector, *mods, *values[4] = { 0, 0, 0, 0 };

	g_jdialog = 0;
	g_jlist = 0;
	g_jsel = -1;
	g_jtotal = 0;
	g_jmodsKey = 1;
	small_buttons_end();
	memset(g_shown, 0, sizeof g_shown);
	g_resolving = g_lastBroadcast = g_lastList = 0;
	g_nignored = 0;
	net_join_download_forget();   /* maps may have changed on disk since */
	if (!lb || !play || !back || !outline) {
		log_msg("join: the Load Game dialog's list, frame or buttons are missing");
		return;
	}
	fields_begin(dialog, ID_JOIN_PLAY);
	ui_label(dialog, lb, play, "Join Game", 55, 3, 150, 20, UI_LEFT);

	/* The servers: the stock frame (120,10 420x400) */
	ui_set_frame(outline, 10, 18, 362, 270);
	/* one list, its rows drawn in three columns: one selection bar across them; + and - under it */
	ui_list_set_frame(lb, 30, 50, 320, 203);
	WIN_STYLE(lb) |= LBcStyle_OwnerDraw;
	g_jlist = lb;
	for (int c = 0; c < JOIN_COLUMNS; c++)
		ui_label(dialog, lb, play, headers[c], 30 + 3 + g_colX[c], 27, g_colW[c] + 6, 20, UI_LEFT);

	/* The selected server, and Current mods: two more frames, drawn after (under) the controls */
	inspector = ui_add(dialog, 0, outline, 0, ~0u, "", 0, 10, 298, 362, 124);
	if (inspector) win_insert_after(inspector, outline);
	mods = ui_add(dialog, 0, outline, 0, ~0u, "", 0, 382, 40, 250, 255);
	if (mods) win_insert_after(mods, outline);
	for (int i = 0; i < 4; i++) {
		int y = 310 + i * 21;
		ui_label(dialog, lb, play, rows[i], 30, y, 105, 20, UI_LEFT);
		if (i == 3) g_address = add_field(dialog, lb, play, ID_JOIN_ADDRESS, 139, y, 211, ADDRESS_MAX - 1, "", FIELD_TEXT, 0, 0);
		else values[i] = ui_label(dialog, lb, play, "-", 139, y, 211, 20, UI_LEFT);
	}
	g_infoName = values[0];
	g_infoMap = values[1];
	g_infoConnected = values[2];
	g_jstatus = ui_label(dialog, lb, play, "", 30, 394, 330, 20, UI_LEFT);

	/* (a label for the list's flags too: a button's give it a box) */
	mods_list_add(dialog, lb, ui_label(dialog, lb, play, "Current mods", 427, 25, 150, 20, UI_LEFT), ID_JOIN_MODS, 395, 62, 225, 190, 0);
	g_jtotal = ui_label(dialog, lb, play, "Total download: -", 395, 264, 232, 20, UI_LEFT);

	/* + and - (add, remove) at the list's lower left; Play, Refresh and Back full size, centered */
	small_button_add(dialog, play, back, "+", ID_JOIN_ADD, 30, 264);
	small_button_add(dialog, play, back, "-", ID_JOIN_REMOVE, 52, 264);
	ui_set_text(play, "Play");
	ui_set_frame(play, 121, 433, BUTTON_W, BUTTON_H);
	ui_add(dialog, play, back, 0, ~0u, "Refresh", ID_JOIN_REFRESH, 262, 433, BUTTON_W, BUTTON_H);
	ui_set_text(back, "Back");
	ui_set_frame(back, 402, 433, BUTTON_W, BUTTON_H);

	bookmarks_load();
	for (int i = 0; i < g_nmarks; i++) join_add_row(i);
	if (g_nmarks) {
		list_select(0);
		join_show_selected();
	}
	g_jdialog = dialog;
	WMrWindow_SetFocus(lb);
}

static u8 ONI_FC join_proc(WMtWindow *dialog, u32 msg, u32 p1, u32 p2)
{
	u32 id = p1 & 0xffff, code = p1 >> 16;
	u32 now = GetTickCount();
	char text[TEXT_MAX];

	if (msg == WMcMessage_InitDialog) {
		join_init(dialog);
		return 1;
	}
	if (msg == LBcMessage_DrawRow && id == ID_JOIN_LIST && p2) {
		u32 row = *(u32 *)(p2 + 0xc);
		if (row < (u32)g_nmarks)
			for (int c = 0; c < JOIN_COLUMNS; c++) ui_list_draw_text(p2, g_colX[c], g_colW[c], g_marks[row].cells[c], UI_ORANGE);
		return 1;
	}
	if (msg == LBcMessage_DrawRow && id == ID_JOIN_MODS) return (u8)mods_draw_row(p2);
	if (msg != WMcMessage_Command) return 0;
	if (id == ID_JOIN_LIST && code == WMcNotify_DoubleClick) id = ID_JOIN_PLAY;
	else if (code != WMcNotify_Click) return 1;

	if (id == ID_JOIN_PLAY) {
		Bookmark *b;
		b = join_selected();
		if (!join_status(b, now, text)) {   /* it says why on the dialog already */
			log_msg("join: can't join: %s", text);
			return 1;
		}
		str_cpy(g_joinFolder, g_joinFolderSize, b->info.folder);
		*g_joinLevel = b->info.level;
		if (b->resolved == 1) {   /* where it answered from (the helper may have found a CGNAT host's real port) */
			const u8 *ip = (const u8 *)&b->addr;
			char where[32];
			fmt(where, "%d.%d.%d.%d:%d", ip[0], ip[1], ip[2], ip[3], ((b->port & 0xff) << 8) | (b->port >> 8));
			net_menu_join(where);
		} else
			net_menu_join(b->address);
		if (b->have_info && b->info.same_version && b->info.has_download) net_join_level0(b->info.l0);   /* one load fewer */
		bookmarks_save();
		WMrDialog_ModalEnd(dialog, 1);
	} else if (id == ID_JOIN_REFRESH) {
		join_refresh();
	} else if (id == ID_JOIN_ADD) {
		if (g_nmarks >= MAX_BOOKMARKS) return 1;
		Bookmark *b = &g_marks[g_nmarks];
		memset(b, 0, sizeof *b);
		bookmark_reset(b);
		join_add_row(g_nmarks++);
		list_select(g_nmarks - 1);
		join_show_selected();
		bookmarks_save();
		WMrWindow_SetFocus(g_address);
	} else if (id == ID_JOIN_REMOVE) {
		int row;
		row = list_selected();
		if (row < 0 || row >= g_nmarks) return 1;
		if (g_marks[row].found && g_marks[row].have_info && g_nignored < MAX_IGNORED)
			g_ignored[g_nignored++] = g_marks[row].info.server_id;   /* the next broadcast won't bring it back */
		join_delete(row);
		if (g_nmarks) list_select(row < g_nmarks ? row : g_nmarks - 1);
		else g_jsel = -1;
		join_show_selected();
		bookmarks_save();
	} else if (id == ID_JOIN_BACK) {
		bookmarks_save();
		WMrDialog_ModalEnd(dialog, 0);
	}
	return 1;
}

void mp_menu_join_failed(const char *folder, const char *why)
{
	str_cpy(g_failedFolder, sizeof g_failedFolder, folder);
	str_cpy(g_failedWhy, sizeof g_failedWhy, why);
}

int mp_menu_join(char *folder, u32 folder_size, u32 *level)
{
	u32 go = 0;

	g_joinFolder = folder;
	g_joinFolderSize = folder_size;
	g_joinLevel = level;
	folder[0] = 0;
	*level = 0;
	g_joinHookOld = ui_frame_hook(join_frame);
	WMrDialog_ModalBegin(WMcDialogID_LoadGame, 0, join_proc, 0, &go);
	ui_frame_hook(g_joinHookOld);
	g_jdialog = 0;
	g_jlist = 0;
	g_jtotal = 0;
	g_modsList = 0;
	fields_end();
	small_buttons_end();
	return go == 1;
}
