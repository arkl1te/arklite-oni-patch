/* Custom Game's network dialogs, built at run time from Oni's own dialogs (no new WMDD):
 *
 *   Custom Game   the New Game popup (dialog 154), widened to 500x160 and centered again: title,
 *                 "Start a NEW custom game, or JOIN one through the network!", New, Join, Cancel.
 *                 New is custom_game.c's level list.
 *   Host Game     the Options dialog (152) with its controls hidden and its three frames laid out
 *                 again: Server (Map, Server name, Port), Match (Max players, Max spectators, Time
 *                 limit in minutes, Score limit, a message line; 0 is no limit, a negative score
 *                 limit is reached by suicides) and the third frame holding Host and Dedicate;
 *                 Cancel under it. The values are the last ones used (Arklite.ini [host] name,
 *                 max_players, max_spectators, time_limit, score_limit and [net] port). Dedicate
 *                 (a server without a window) isn't built.
 *   Join Game     the Load Game dialog (151): its frame holds the bookmarked servers in three
 *                 columns (Ping, Name, Players); a second frame under it the selected one's Address
 *                 (typed) and what the server says: Name, Connected, Spectating, its map. Play, Add,
 *                 Remove, Back. Bookmarks are Arklite.ini [servers] count, 1, 2, ...
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
/* [DCrDraw_PartSpec]-like: ECX draw context, EDX part, flags (0xffff: all nine), &{x, y}, width,
 * height, alpha */
#define DCrDrawPart         ((void (ONI_FC *)(void *, void *, u32, short *, int, int, u32))0x4698b0)
#define WIN_PARENT(w)       (*(WMtWindow **)((u8 *)(w) + 8))
#define WIN_TEXTCOLOR(w)    (*(u32 *)((u8 *)WIN_FONTINFO(w) + 8))
#define VK_SHIFT            0x10
#define VK_CONTROL          0x11
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

int ui_text_width(const char *s)
{
	int w = 0;
	while (*s) w += char_width((u8)*s++);
	return w;
}

void ui_fit(char *dst, unsigned size, const char *src, int width)
{
	int w = 0, dots = 3 * char_width('.');
	unsigned n = 0;

	if (ui_text_width(src) <= width) {
		str_cpy(dst, size, src);
		return;
	}
	while (src[n] && n + 4 < size && w + char_width((u8)src[n]) + dots <= width) w += char_width((u8)src[n++]);
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

/* ---- Edit fields ---------------------------------------------------------------------------- */

#define MAX_FIELDS 8
enum { FIELD_TEXT, FIELD_NUMBER, FIELD_SIGNED };   /* signed: a '-' may start it */
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
	while (fit[0] && ui_text_width(fit) > w - 8) fit[str_len(fit) - 1] = 0;
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

static void field_confirm(UiField *f)
{
	char text[16];
	int v;

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

static int field_cursor(UiField *f)
{
	u8 *data = WMrWindow_GetBytes(f->w, 0);
	return data ? *(int *)(data + 0xc) : 0;
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
	if (!data) return;
	*(int *)(data + 0xc) = at;
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

/* Spaces, letters and digits (a word), and the rest (".", ":", ... each a word of its own kind) */
static int char_class(char c)
{
	if (c == ' ') return 0;
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
		field_move(f, ctrl ? word_left(text, cursor) : sel && !shift ? lo : cursor > 0 ? cursor - 1 : 0, shift);
		return;
	case KEY_RIGHT:
		field_move(f, ctrl ? word_right(text, len, cursor) : sel && !shift ? hi : cursor < len ? cursor + 1 : len, shift);
		return;
	case KEY_HOME:
		field_move(f, 0, shift);
		return;
	case KEY_END:
		field_move(f, len, shift);
		return;
	case KEY_BACKSPACE:
		if (sel) field_replace(f, text, lo, hi, "");
		else if (cursor > 0) field_replace(f, text, ctrl ? word_left(text, cursor) : cursor - 1, cursor, "");
		return;
	case KEY_DELETE:   /* (Oni's own delete at the end of the text runs past it) */
		if (sel) field_replace(f, text, lo, hi, "");
		else if (cursor < len) field_replace(f, text, cursor, ctrl ? word_right(text, len, cursor) : cursor + 1, "");
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
	if (f->numbers && (key < '0' || key > '9') &&
	    !(key == '-' && f->numbers == FIELD_SIGNED && (sel ? lo : cursor) == 0 && (text[0] != '-' || (sel && lo == 0 && hi > 0))))
		return;
	if (f->numbers && key != '-' && !sel && cursor == 0 && text[0] == '-') return;   /* a digit before the '-' */
	ins[0] = (char)key;
	ins[1] = 0;
	if (sel) field_replace(f, text, lo, hi, ins);
	else field_replace(f, text, cursor, cursor, ins);
}

/* The selection is drawn under the text: a white bar, the selected characters dark on it. The
 * field's paint (0x46bb5f) draws its text with one DCrDrawText at {left + 2, baseline}, with the
 * field's font current, left-aligned; pieces of it go at the same baseline. */
static void ONI_FC edit_draw_text(void *dc, const char *text, u32 rect, short *pos)
{
	UiField *f = g_painting;
	char piece[128];
	int len = str_len(text), lo, hi;

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

/* ---- Host ----------------------------------------------------------------------------------- */

enum { ID_HOST_GO = 1, ID_HOST_CANCEL = 2, ID_HOST_DEDICATE = 3,
       ID_HOST_NAME = 120, ID_HOST_PORT, ID_HOST_PLAYERS, ID_HOST_SPECTATORS, ID_HOST_TIME, ID_HOST_SCORE };
#define MAX_PLAYERS_SETTING 64
#define HOST_NOTE "A game has 2 players for now."

static WMtWindow *g_hostStatus;
static const char *g_hostMap;
static void *g_hostHookOld;

static int is_frame(WMtWindow *w, int y)
{
	return WIN_X(w) == 5 && WIN_W(w) == 300 && WIN_Y(w) == y;
}

/* Kept on the Host page: the background picture and the three frames (x 5, width 300) */
static int host_keeps(WMtWindow *w)
{
	return (WIN_X(w) == 0 && WIN_Y(w) == 0) || is_frame(w, 50) || is_frame(w, 175) || is_frame(w, 290);
}

static void host_status(const char *text)
{
	ui_set_text_fit(g_hostStatus, text);
}

static void host_init(WMtWindow *dialog)
{
	WMtWindow *close = WMrDialog_GetItemByID(dialog, ID_HOST_CANCEL);
	WMtWindow *row = find_child(dialog, 30, 65);      /* "Quality:": precedes every frame */
	WMtWindow *header = find_child(dialog, 50, 35);   /* "Graphics" (blue) */
	WMtWindow *title = find_child(dialog, 5, 10);     /* "Options" */
	WMtWindow *frame[3] = { 0, 0, 0 }, *hidden[48], *w;
	u32 n = 0;
	char name[NET_SERVER_NAME_MAX], number[16];

	g_hostStatus = 0;
	if (!close || !row || !header || !title) {
		log_msg("host: the Options dialog's controls are missing");
		return;
	}
	for (w = WIN_FIRSTCHILD(dialog); w; w = WIN_NEXT(w)) {
		if (is_frame(w, 50)) frame[0] = w;
		else if (is_frame(w, 175)) frame[1] = w;
		else if (is_frame(w, 290)) frame[2] = w;
		if (!host_keeps(w) && w != close && n < 48) hidden[n++] = w;
	}
	if (!frame[0] || !frame[1] || !frame[2]) {
		log_msg("host: the Options dialog's frames are missing");
		return;
	}
	fields_begin(dialog, ID_HOST_GO);

	ui_label(dialog, row, title, "Host Game", 5, 10, 150, 20, UI_LEFT);

	/* Server: Map, Server name (its own row: a name needs the frame's width), Port */
	ui_set_frame(frame[0], 5, 50, 300, 115);
	ui_label(dialog, row, header, "Server", 50, 35, 150, 20, UI_LEFT);
	ui_label(dialog, row, row, "Map:", 25, 62, 60, 20, UI_LEFT);
	ui_label(dialog, row, row, g_hostMap, 90, 62, 205, 20, UI_LEFT);
	ui_label(dialog, row, row, "Server name:", 25, 86, 150, 20, UI_LEFT);
	GetPrivateProfileStringA("host", "name", "New Oni multiplayer game", name, sizeof name, ini_path());
	add_field(dialog, row, row, ID_HOST_NAME, 25, 108, 270, NET_SERVER_NAME_MAX - 1, name, FIELD_TEXT, 0, 0);
	ui_label(dialog, row, row, "Port:", 25, 134, 60, 20, UI_LEFT);
	fmt(number, "%d", clamp((int)GetPrivateProfileIntA("net", "port", NET_DEFAULT_PORT, ini_path()), 1, 65535));
	add_field(dialog, row, row, ID_HOST_PORT, 90, 134, 80, 5, number, FIELD_NUMBER, 1, 65535);

	/* Match: the player limits, the match's time and score limits (0: none; a negative score
	 * limit is reached by suicides) and the message line */
	ui_set_frame(frame[1], 5, 187, 300, 133);   /* the frame art needs 93 px (52 + 41 corners) */
	ui_label(dialog, row, header, "Match", 50, 172, 150, 20, UI_LEFT);
	ui_label(dialog, row, row, "Max players:", 25, 199, 120, 20, UI_LEFT);
	fmt(number, "%d", clamp((int)GetPrivateProfileIntA("host", "max_players", 8, ini_path()), 2, MAX_PLAYERS_SETTING));
	add_field(dialog, row, row, ID_HOST_PLAYERS, 150, 199, 60, 2, number, FIELD_NUMBER, 2, MAX_PLAYERS_SETTING);
	ui_label(dialog, row, row, "Max spectators:", 25, 222, 120, 20, UI_LEFT);
	fmt(number, "%d", clamp((int)GetPrivateProfileIntA("host", "max_spectators", 0, ini_path()), 0, MAX_PLAYERS_SETTING));
	add_field(dialog, row, row, ID_HOST_SPECTATORS, 150, 222, 60, 2, number, FIELD_NUMBER, 0, MAX_PLAYERS_SETTING);
	ui_label(dialog, row, row, "Time limit:", 25, 245, 120, 20, UI_LEFT);
	fmt(number, "%d", clamp((int)GetPrivateProfileIntA("host", "time_limit", 10, ini_path()), 0, NET_TIME_LIMIT_MAX));
	add_field(dialog, row, row, ID_HOST_TIME, 150, 245, 60, 3, number, FIELD_NUMBER, 0, NET_TIME_LIMIT_MAX);
	ui_label(dialog, row, row, "minutes", 218, 245, 77, 20, UI_LEFT);
	ui_label(dialog, row, row, "Score limit:", 25, 268, 120, 20, UI_LEFT);
	fmt(number, "%d", clamp((int)GetPrivateProfileIntA("host", "score_limit", 20, ini_path()), -NET_SCORE_LIMIT_MAX,
	                        NET_SCORE_LIMIT_MAX));
	add_field(dialog, row, row, ID_HOST_SCORE, 150, 268, 60, 4, number, FIELD_SIGNED, -NET_SCORE_LIMIT_MAX, NET_SCORE_LIMIT_MAX);
	ui_label(dialog, row, row, "points", 218, 268, 77, 20, UI_LEFT);
	g_hostStatus = ui_label(dialog, row, row, HOST_NOTE, 25, 291, 270, 20, UI_LEFT);

	/* The third frame: Host and Dedicate, full size; Cancel under it */
	ui_set_frame(frame[2], 5, 325, 300, 95);
	/* after "Quality:" like everything else: a window after a frame in the child list is drawn
	 * under the frame's dark fill and looks disabled */
	ui_add(dialog, row, close, 0, ~0u, "Host", ID_HOST_GO, 17, 352, BUTTON_W, BUTTON_H);
	ui_add(dialog, row, close, 0, 0x30000, "Dedicate", ID_HOST_DEDICATE, 163, 352, BUTTON_W, BUTTON_H);
	ui_set_text(close, "Cancel");
	ui_set_frame(close, WIN_X(close), 430, BUTTON_W, BUTTON_H);

	for (u32 i = 0; i < n; i++) WMrWindow_SetVisible(hidden[i], 0);
}

/* The fields, checked and saved; 0 (and why, on the dialog) if one isn't right */
static int host_read(WMtWindow *dialog, char *name, u32 *port, u32 *players, u32 *spectators, u32 *minutes, int *score)
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
	if (!edit_number(WMrDialog_GetItemByID(dialog, ID_HOST_PLAYERS), &v) || v < 2 || v > MAX_PLAYERS_SETTING) {
		host_status("Max players: 2 to 64.");
		return 0;
	}
	*players = (u32)v;
	if (!edit_number(WMrDialog_GetItemByID(dialog, ID_HOST_SPECTATORS), &v) || v > MAX_PLAYERS_SETTING) {
		host_status("Max spectators: 0 to 64.");
		return 0;
	}
	*spectators = (u32)v;
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
	host_status(HOST_NOTE);

	WritePrivateProfileStringA("host", "name", name, ini_path());
	fmt(value, "%d", *port);
	WritePrivateProfileStringA("net", "port", value, ini_path());
	fmt(value, "%d", *players);
	WritePrivateProfileStringA("host", "max_players", value, ini_path());
	fmt(value, "%d", *spectators);
	WritePrivateProfileStringA("host", "max_spectators", value, ini_path());
	fmt(value, "%d", *minutes);
	WritePrivateProfileStringA("host", "time_limit", value, ini_path());
	fmt(value, "%d", *score);
	WritePrivateProfileStringA("host", "score_limit", value, ini_path());
	return 1;
}

static u8 ONI_FC host_proc(WMtWindow *dialog, u32 msg, u32 p1, u32 p2)
{
	u32 id = p1 & 0xffff, code = p1 >> 16;
	char name[NET_SERVER_NAME_MAX];
	u32 port, players, spectators, minutes;
	int score;

	if (msg == WMcMessage_InitDialog) {
		host_init(dialog);
		return 1;
	}
	if (msg != WMcMessage_Command) return 0;
	if (code != WMcNotify_Click) return 1;
	if (id == ID_HOST_GO) {
		if (!host_read(dialog, name, &port, &players, &spectators, &minutes, &score)) return 1;
		net_menu_host(port, name, players, spectators, minutes, score);
		WMrDialog_ModalEnd(dialog, 1);
	} else if (id == ID_HOST_DEDICATE) {
		if (!host_read(dialog, name, &port, &players, &spectators, &minutes, &score)) return 1;
		host_status("Dedicated servers aren't built yet.");
		log_msg("host: Dedicate clicked; dedicated servers aren't built yet");
	} else if (id == ID_HOST_CANCEL) {
		WMrDialog_ModalEnd(dialog, 0);
	}
	return 1;
}

static void host_frame(void)
{
	ui_frame_hook_call(g_hostHookOld);
	fields_frame();
}

int mp_menu_host(const char *map_name)
{
	u32 go = 0;

	g_hostMap = map_name;
	g_hostHookOld = ui_frame_hook(host_frame);
	WMrDialog_ModalBegin(WMcDialogID_Options, 0, host_proc, 0, &go);
	ui_frame_hook(g_hostHookOld);
	fields_end();
	return go == 1;
}

/* ---- Join ----------------------------------------------------------------------------------- */

enum { ID_JOIN_PLAY = 1, ID_JOIN_BACK = 2, ID_JOIN_ADD = 3, ID_JOIN_REMOVE = 4, ID_JOIN_LIST = 100,
       ID_JOIN_ADDRESS = 120 };
#define JOIN_COLUMNS    3      /* Ping, Name, Players: drawn into each row of the one list */
#define MAX_BOOKMARKS   64
#define ADDRESS_MAX     64
#define ASK_MS          2000   /* every server in the list */
#define ASK_SELECTED_MS 1000
#define NO_ANSWER_MS    3000
#define RESOLVE_WAIT_MS 500    /* after the last key typed in Address */
#define LAN_TOKEN       0x7fffffffu   /* broadcast questions: the servers on this PC and the network */
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
	int found;                 /* answered a broadcast; not saved unless its address is edited */
	NetServerInfo info;
	char cells[JOIN_COLUMNS][128];   /* what the columns show */
} Bookmark;

static Bookmark g_marks[MAX_BOOKMARKS];
static int g_nmarks;
static u32 g_token;
/* The list (the dialog's own, owner-drawn) and its selection as last seen */
static WMtWindow *g_jlist;
static int g_jsel = -1;
static const int g_colX[JOIN_COLUMNS] = { 0, 59, 301 }, g_colW[JOIN_COLUMNS] = { 50, 232, 52 };

static int list_selected(void)
{
	return g_jlist ? (int)WMrMessage_Send(g_jlist, LBcMessage_GetSelection, 0, 0) : -1;
}

static void list_select(int row)
{
	if (g_jlist) WMrMessage_Send(g_jlist, LBcMessage_SetSelection, 0, (u32)row);
	g_jsel = row;
}
static WMtWindow *g_jdialog, *g_address, *g_infoName, *g_infoConnected, *g_infoSpectating, *g_jstatus;
static char g_shown[4][TEXT_MAX];
static void *g_joinHookOld;
static char *g_joinFolder;
static u32 g_joinFolderSize, *g_joinLevel;
static u32 g_resolving;            /* the token whose address net.c's thread is looking up, 0: none */
static u32 g_lastBroadcast;
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

	if (b->have_info && b->info.same_version && b->info.name[0]) str_cpy(cells[1], 128, b->info.name);
	else if (b->address[0]) str_cpy(cells[1], 128, b->address);
	else str_cpy(cells[1], 128, "(new server)");

	if (b->have_info && b->info.same_version) fmt(cells[2], "%d/%d", b->info.players, b->info.max_players);
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
		str_cpy(text, TEXT_MAX, "Select a server, or Add one.");
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
	if (!b->have_info) {
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
	str_cpy(text, TEXT_MAX, b->info.in_match ? "Match on, you'll wait for the next: " : "Map: ");
	str_cpy(text + str_len(text), TEXT_MAX - str_len(text), b->info.map);
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

/* Address shows the selected entry's, whatever was typed before */
static void join_show_selected(void)
{
	Bookmark *b = join_selected();
	WMrMessage_Send(g_address, EFcMessage_SetText, (u32)(b ? b->address : ""), 0);
}

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

	while (net_query_poll(&info)) {
		Bookmark *b = 0;
		int i;
		for (i = 0; i < g_nmarks && !b; i++)
			if (g_marks[i].token == info.token) b = &g_marks[i];
		/* a broadcast's answer: a server already listed (by its id or its address), or a new one */
		for (i = 0; i < g_nmarks && !b && info.same_version && info.server_id; i++)
			if (g_marks[i].have_info && g_marks[i].info.server_id == info.server_id) b = &g_marks[i];
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

	/* what each row shows; the list draws it (join_proc, msg 0x36) */
	for (int i = 0; i < g_nmarks; i++) bookmark_cells(&g_marks[i], now, g_marks[i].cells);

	/* The selected server */
	{
		int known = sel && sel->have_info && sel->info.same_version;
		if (known) str_cpy(text, sizeof text, sel->info.name);
		show_once(0, g_infoName, known ? text : "-");
		if (known) fmt(text, "%d/%d", sel->info.players, sel->info.max_players);
		show_once(1, g_infoConnected, known ? text : "-");
		if (known) fmt(text, "%d/%d", sel->info.spectators, sel->info.max_spectators);
		show_once(2, g_infoSpectating, known ? text : "-");
		join_status(sel, now, text);
		show_once(3, g_jstatus, text);
	}
}

static void join_init(WMtWindow *dialog)
{
	static const char *const headers[JOIN_COLUMNS] = { "Ping", "Name", "Players" };
	static const char *const rows[4] = { "Address:", "Name:", "Connected:", "Spectating:" };
	WMtWindow *lb = WMrDialog_GetItemByID(dialog, 100);
	WMtWindow *play = WMrDialog_GetItemByID(dialog, ID_JOIN_PLAY);
	WMtWindow *back = WMrDialog_GetItemByID(dialog, ID_JOIN_BACK);
	WMtWindow *outline = find_child(dialog, 120, 10);
	WMtWindow *inspector, *values[4] = { 0, 0, 0, 0 };

	g_jdialog = 0;
	g_jlist = 0;
	g_jsel = -1;
	memset(g_shown, 0, sizeof g_shown);
	g_resolving = g_lastBroadcast = 0;
	g_nignored = 0;
	if (!lb || !play || !back || !outline) {
		log_msg("join: the Load Game dialog's list, frame or buttons are missing");
		return;
	}
	fields_begin(dialog, ID_JOIN_PLAY);
	ui_label(dialog, lb, play, "Join Game", 5, 10, 110, 20, UI_LEFT);

	/* The bookmarks: the stock frame (120,10 420x400), now the top 60% */
	ui_set_frame(outline, 120, 10, 420, 276);
	/* one list, its rows drawn in three columns: one selection bar across them */
	ui_set_frame(lb, 140, 42, 375, 232);
	WIN_STYLE(lb) |= LBcStyle_OwnerDraw;
	g_jlist = lb;
	for (int c = 0; c < JOIN_COLUMNS; c++)
		ui_label(dialog, lb, play, headers[c], 140 + 3 + g_colX[c], 20, g_colW[c] + 6, 20, UI_LEFT);

	/* The selected server: a second frame, drawn after (under) the controls */
	inspector = ui_add(dialog, 0, outline, 0, ~0u, "", 0, 120, 292, 420, 120);
	if (inspector) win_insert_after(inspector, outline);
	for (int i = 0; i < 4; i++) {
		int y = 302 + i * 21;
		ui_label(dialog, lb, play, rows[i], 140, y, 105, 20, UI_LEFT);
		if (i == 0) g_address = add_field(dialog, lb, play, ID_JOIN_ADDRESS, 250, y, 265, ADDRESS_MAX - 1, "", FIELD_TEXT, 0, 0);
		else values[i] = ui_label(dialog, lb, play, "-", 250, y, 265, 20, UI_LEFT);
	}
	g_infoName = values[1];
	g_infoConnected = values[2];
	g_infoSpectating = values[3];
	g_jstatus = ui_label(dialog, lb, play, "", 140, 386, 375, 20, UI_LEFT);

	/* Play, Add, Remove, Back: full size, across the screen */
	ui_set_text(play, "Play");
	ui_set_frame(play, 45, 425, BUTTON_W, BUTTON_H);
	ui_add(dialog, play, back, 0, ~0u, "Add", ID_JOIN_ADD, 185, 425, BUTTON_W, BUTTON_H);
	ui_add(dialog, play, back, 0, ~0u, "Remove", ID_JOIN_REMOVE, 325, 425, BUTTON_W, BUTTON_H);
	ui_set_text(back, "Back");
	ui_set_frame(back, 465, 425, BUTTON_W, BUTTON_H);

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
		net_menu_join(b->address);
		bookmarks_save();
		WMrDialog_ModalEnd(dialog, 1);
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
	fields_end();
	return go == 1;
}
