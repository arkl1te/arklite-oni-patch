/* The mouse wheel in Oni's menus. Oni has no wheel input: its UI loop (WMrUpdate 0x477070) only
 * knows mouse moves, buttons and keys. So Oni's window gets a window procedure in front of its
 * own that adds up WM_MOUSEWHEEL (and passes it on), and the modal loop's WMrUpdate call
 * (0x468ad4, every dialog runs there: main menu, pause, Options, Load Game, Custom Game, F1) is
 * wrapped to act on it: at that point no UI code is running, so the window messages below are as
 * safe as a click.
 *
 * What a notch does goes by the window under the cursor (WMrGetWindowUnderPoint 0x475f50, at
 * WMgCursorPosition, the position the last mouse event left), or its parents:
 *   list box  WHEEL_ROWS rows. Not SBcNotify_LineUp/Down: those step the scroll position, of
 *             which there are as many as rows, and the first row shown is that position times
 *             (rows - visible) / rows, so on a short list a step moves less than a row. Message
 *             0x58 with a thumb position (WMiListBox_HandleVerticalScroll) reaches any row, and
 *             the scroll bar follows. The list's scroll bar is a child, so it counts as the list.
 *   slider    a twentieth of its range, told to the dialog as the keyboard's arrows do it
 *             (WMiSlider_HandleKeyEvent: Command with SLcNotify_NewPosition).
 *   F1 screen its Previous/Next buttons (dialog 0x46), when they're showing and enabled, sent
 *             as their click's Command. On the F1 screen's other windows too, not only on them.
 * Popup menus are left alone, closed or open: a stray notch mustn't change the resolution.
 * Nothing happens while a slider or scroll bar is being dragged (they hold WMgMouseFocus).
 * Wheel turns in gameplay aren't counted, or a pause menu would start scrolled; photo mode
 * (photo.c) takes them for its zoom while it's on.
 *
 * A list's own scroll bar arrows and track (SBcNotify_LineUp/Down, PageUp/Down) step the scroll
 * position too, so on a short list an arrow click often moved nothing (the Host page's Mutators:
 * 9 rows, 8 showing, a row every 5 clicks). Their message (0x58, WMiListBox_HandleVerticalScroll
 * 0x46d2c0, called only at 0x46cd3e) comes here first and becomes the thumb position of the
 * first row one row (arrows) or one page (track) away, as the wheel's. Dragging the thumb is
 * left as it was.
 *
 * Holding an arrow repeats: the scroll bar's timer (every 10 frames from the bar's making, not
 * from the press: WMcScrollbar_TimerFrequency) sends the press again while the button is down
 * (WMiScrollbar_HandleTimer, its call at 0x471d55), so a plain click stepped twice about half the
 * time. Its repeats now start once the button has been down SB_REPEAT_DELAY_MS (the press itself
 * goes through 0x471cb5).
 *
 * F12 (Arklite's screenshot key, screenshot.c) is kept from list boxes: a key a list doesn't
 * know goes to its type-ahead search (WMiListBox_HandleKeyEvent 0x46d0c0, called at 0x46cb76),
 * which moved the selection, so a screenshot of a menu changed the menu.
 *
 * The same window procedure keeps F10 from Windows: it's the system menu key, and DefWindowProc
 * would go into menu mode on it (WM_SYSKEYDOWN, then SC_KEYMENU), which can swallow the next key.
 * Oni has no menu, and F10 is photo mode's key; Oni reads its own keys without these messages. */
#include "arklite.h"

#define WM_MOUSEWHEEL   0x20a
#define WM_SYSKEYDOWN   0x104
#define WM_SYSKEYUP     0x105
#define VK_F10          0x79
#define WHEEL_DELTA     120
#define GWL_WNDPROC     (-4)
#define WHEEL_ROWS      3
#define SLIDER_STEPS    20      /* notches from one end of a slider to the other */

typedef long (WINAPI *WNDPROC)(void *, u32, u32, long);
__declspec(dllimport) void *WINAPI GetForegroundWindow(void);
__declspec(dllimport) u32 WINAPI GetWindowThreadProcessId(void *, u32 *);
__declspec(dllimport) u32 WINAPI GetCurrentProcessId(void);
__declspec(dllimport) u32 WINAPI GetTickCount(void);
__declspec(dllimport) long WINAPI SetWindowLongW(void *, int, long);
__declspec(dllimport) long WINAPI CallWindowProcW(WNDPROC, void *, u32, u32, long);

#define WMcModal_UpdateCall     0x468ad4
#define WMrUpdate               ((void (ONI_FC *)(void))0x477070)
#define LIrMode_Get             ((int (ONI_FC *)(void))0x403f60)
#define LIcMode_Game            1
#define WMgDesktop              (*(WMtWindow **)0x5e9970)
#define WMgMouseFocus           (*(WMtWindow **)0x5e9978)
#define WMgCursorPosition       ((const short *)0x5ea4cc)   /* x, y */
/* ECX the window to search, EDX the point, [flags a window must have]; ret 4 */
#define WMrWindow_UnderPoint    ((WMtWindow *(ONI_FC *)(WMtWindow *, const short *, u32))0x475f50)
#define WIN_PARENT(w)           (*(WMtWindow **)((u8 *)(w) + 8))
#define WMcFlag_Visible         1
#define WMcFlag_Disabled        2

#define WMcClass_Dialog         6
#define WMcClass_Slider         0x11
#define WMcMessage_Command      0x17

/* List box data (WMtListBox_PrivateData) */
#define LB_VISIBLE(p)           (*(int *)((p) + 0x8c))
#define LB_START(p)             (*(int *)((p) + 0x94))     /* the first row shown */
#define LB_RATIO(p)             (*(float *)((p) + 0xa4))   /* first row = scroll position * this */
#define LBcNotify_Thumb         (4 << 16)

/* Slider data; its min, max and value come first */
typedef struct { int min, max, value; } Slider;
#define SLcNotify_NewPosition   1

/* The F1 screen (Oni_InGameUI.c) */
#define ONcPauseScreen_Dialog   0x46
#define ONcPS_Previous          106
#define ONcPS_Next              107
#define BNcNotify_Click         1

static WNDPROC g_old_proc;
static int g_wheel;      /* in 120ths of a notch, up is positive */

static void install_proc(void);
static int g_hud_wheel;  /* the same, while the F2 menu (char_menu.c) is up: it takes them itself */
static int g_photo_wheel;   /* the same, while photo mode is on */

static long WINAPI window_proc(void *hwnd, u32 msg, u32 wp, long lp)
{
	if ((msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) && wp == VK_F10) return 0;
	if (msg == WM_MOUSEWHEEL) {
		if (char_menu_open()) g_hud_wheel += (short)(wp >> 16);
		else if (photo_active() && LIrMode_Get() == LIcMode_Game) g_photo_wheel += (short)(wp >> 16);
		else if (LIrMode_Get() != LIcMode_Game) g_wheel += (short)(wp >> 16);
	}
	return CallWindowProcW(g_old_proc, hwnd, msg, wp, lp);
}

/* Whole notches turned since the last call while the F2 menu was up (up is positive). Also puts
 * the window procedure in place if no menu has yet (a game started from ARKLITE_AUTOSTART or
 * Arklite.ini [net] join skips them) */
int menu_wheel_hud_take(void)
{
	if (!g_old_proc) install_proc();
	int notches = g_hud_wheel / WHEEL_DELTA;
	g_hud_wheel -= notches * WHEEL_DELTA;
	if (!char_menu_open()) g_hud_wheel = 0;
	return notches;
}

int menu_wheel_photo_take(void)
{
	if (!g_old_proc) install_proc();
	int notches = g_photo_wheel / WHEEL_DELTA;
	g_photo_wheel -= notches * WHEEL_DELTA;
	if (!photo_active()) g_photo_wheel = 0;
	return notches;
}

static void install_proc(void)
{
	u32 pid = 0;
	void *w = GetForegroundWindow();

	if (w) GetWindowThreadProcessId(w, &pid);
	if (!w || pid != GetCurrentProcessId()) return;   /* Oni isn't in front: at a later frame */
	g_old_proc = (WNDPROC)SetWindowLongW(w, GWL_WNDPROC, (long)window_proc);
	if (!g_old_proc) log_msg("menu wheel: can't read the mouse wheel (no window procedure)");
	else log_msg("menu wheel: reading the wheel in window %p", w);
}

/* The thumb position that shows first as a list's first row (the list's own conversion,
 * first = position * ratio, undone and rounded) */
static u32 list_thumb_for(u8 *p, int first)
{
	return (u32)(int)((float)first / LB_RATIO(p) + 0.5f);
}

static void wheel_list(WMtWindow *list, int notches)
{
	u8 *p = WMrWindow_GetBytes(list, 0);
	int rows, last, first;

	if (!p) return;
	rows = (int)WMrMessage_Send(list, LBcMessage_GetNumLines, 0, 0);
	last = rows - LB_VISIBLE(p);
	if (last <= 0 || LB_RATIO(p) <= 0.0f) return;   /* all of it shows */
	first = LB_START(p) - notches * WHEEL_ROWS;
	if (first < 0) first = 0;
	if (first > last) first = last;
	if (first != LB_START(p))
		WMrMessage_Send(list, LBcMessage_Scroll, LBcNotify_Thumb, list_thumb_for(p, first));
}

#define WMiListBox_VScroll      ((void (ONI_FC *)(WMtWindow *, u32, u32))0x46d2c0)
#define WMcListBox_VScrollCall  0x46cd3e
#define WMiListBox_Key          ((void (ONI_FC *)(WMtWindow *, u32, u32, u32))0x46d0c0)
#define WMcListBox_KeyCall      0x46cb76
#define SBcNotify_PageDown      3
#define LIcKeyCode_F12          0x8c

static void ONI_FC list_vscroll(WMtWindow *list, u32 p1, u32 p2)
{
	u8 *p = WMrWindow_GetBytes(list, 0);
	u32 code = p1 >> 16;
	if (p && code <= SBcNotify_PageDown) {
		int rows = (int)WMrMessage_Send(list, LBcMessage_GetNumLines, 0, 0);
		int last = rows - LB_VISIBLE(p), step = code >= 2 ? LB_VISIBLE(p) : 1;
		int first = LB_START(p) + (code & 1 ? step : -step);   /* odd: down */
		if (last <= 0 || LB_RATIO(p) <= 0.0f || step <= 0) return;   /* all of it shows */
		if (first < 0) first = 0;
		if (first > last) first = last;
		if (first == LB_START(p)) return;
		p1 = LBcNotify_Thumb | (p1 & 0xffff);
		p2 = list_thumb_for(p, first);
	}
	WMiListBox_VScroll(list, p1, p2);
}

#define WMiScrollbar_Mouse      ((void (ONI_FC *)(WMtWindow *, u32, u32, u32))0x471de0)
#define WMcScrollbar_MouseCall  0x471cb5
#define WMcScrollbar_TimerCall  0x471d55
#define WMcMessage_LMouseDown   0x1f
#define SB_REPEAT_DELAY_MS      400

static WMtWindow *g_sb_pressed;
static u32 g_sb_press_ms;

static void ONI_FC scrollbar_mouse(WMtWindow *sb, u32 msg, u32 p1, u32 p2)
{
	if (msg == WMcMessage_LMouseDown) {
		g_sb_pressed = sb;
		g_sb_press_ms = GetTickCount();
	}
	WMiScrollbar_Mouse(sb, msg, p1, p2);
}

static void ONI_FC scrollbar_timer_press(WMtWindow *sb, u32 msg, u32 p1, u32 p2)
{
	if (sb == g_sb_pressed && GetTickCount() - g_sb_press_ms < SB_REPEAT_DELAY_MS) return;
	WMiScrollbar_Mouse(sb, msg, p1, p2);
}

static void ONI_FC list_key(WMtWindow *list, u32 msg, u32 key, u32 p2)
{
	if (key == LIcKeyCode_F12) return;
	WMiListBox_Key(list, msg, key, p2);
}

static void wheel_slider(WMtWindow *slider, int notches)
{
	Slider *s = (Slider *)WMrWindow_GetBytes(slider, 0);
	int range, step, value;

	if (!s) return;
	range = s->max - s->min;
	step = range / SLIDER_STEPS;
	if (step < 1) step = 1;
	value = s->value + notches * step;
	if (value < s->min) value = s->min;
	if (value > s->max) value = s->max;
	if (value == s->value) return;
	WMrSlider_SetValue(slider, value);
	WMrMessage_Send(WIN_PARENT(slider), WMcMessage_Command, SLcNotify_NewPosition << 16 | WIN_ID(slider), (u32)slider);
}

static void wheel_pages(WMtWindow *dialog, int notches)
{
	u32 id = notches > 0 ? ONcPS_Previous : ONcPS_Next;
	int n = notches > 0 ? notches : -notches;
	WMtWindow *button = WMrDialog_GetItemByID(dialog, id);

	while (button && n-- > 0) {
		if ((WIN_FLAGS(button) & (WMcFlag_Visible | WMcFlag_Disabled)) != WMcFlag_Visible) return;
		WMrMessage_Send(dialog, WMcMessage_Command, BNcNotify_Click << 16 | id, (u32)button);
	}
}

static void wheel(int notches)
{
	WMtWindow *w;

	if (!WMgDesktop || WMgMouseFocus) return;
	for (w = WMrWindow_UnderPoint(WMgDesktop, WMgCursorPosition, WMcFlag_Visible); w; w = WIN_PARENT(w)) {
		u32 cls = WIN_CLASS(w);
		if (cls == WMcClass_ListBox) {
			wheel_list(w, notches);
			return;
		}
		if (cls == WMcClass_Slider) {
			wheel_slider(w, notches);
			return;
		}
		if (cls == WMcClass_Dialog) {
			if (WIN_ID(w) == ONcPauseScreen_Dialog) wheel_pages(w, notches);
			return;
		}
	}
}

static void ONI_FC modal_update(void)
{
	int notches;

	WMrUpdate();
	if (!g_old_proc) install_proc();
	notches = g_wheel / WHEEL_DELTA;
	if (!notches) return;
	g_wheel -= notches * WHEEL_DELTA;
	wheel(notches);
}

void menu_wheel_install(void)
{
	if (patch_call(WMcModal_UpdateCall, (u32)WMrUpdate, (u32)modal_update))
		log_msg("menu wheel: patched");
	if (patch_call(WMcListBox_VScrollCall, 0x46d2c0, (u32)list_vscroll) && patch_call(WMcListBox_KeyCall, 0x46d0c0, (u32)list_key) &&
	    patch_call(WMcScrollbar_MouseCall, 0x471de0, (u32)scrollbar_mouse) &&
	    patch_call(WMcScrollbar_TimerCall, 0x471de0, (u32)scrollbar_timer_press))
		log_msg("menu wheel: list arrows a row a click (held: repeats after %d ms), F12 kept from lists", SB_REPEAT_DELAY_MS);
}
