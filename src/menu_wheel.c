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
 * Wheel turns in gameplay aren't counted, or a pause menu would start scrolled. */
#include "arklite.h"

#define WM_MOUSEWHEEL   0x20a
#define WHEEL_DELTA     120
#define GWL_WNDPROC     (-4)
#define WHEEL_ROWS      3
#define SLIDER_STEPS    20      /* notches from one end of a slider to the other */

typedef long (WINAPI *WNDPROC)(void *, u32, u32, long);
__declspec(dllimport) void *WINAPI GetForegroundWindow(void);
__declspec(dllimport) u32 WINAPI GetWindowThreadProcessId(void *, u32 *);
__declspec(dllimport) u32 WINAPI GetCurrentProcessId(void);
__declspec(dllimport) long WINAPI SetWindowLongA(void *, int, long);
__declspec(dllimport) long WINAPI CallWindowProcA(WNDPROC, void *, u32, u32, long);

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

static long WINAPI window_proc(void *hwnd, u32 msg, u32 wp, long lp)
{
	if (msg == WM_MOUSEWHEEL && LIrMode_Get() != LIcMode_Game) g_wheel += (short)(wp >> 16);
	return CallWindowProcA(g_old_proc, hwnd, msg, wp, lp);
}

static void install_proc(void)
{
	u32 pid = 0;
	void *w = GetForegroundWindow();

	if (w) GetWindowThreadProcessId(w, &pid);
	if (!w || pid != GetCurrentProcessId()) return;   /* Oni isn't in front: at a later frame */
	g_old_proc = (WNDPROC)SetWindowLongA(w, GWL_WNDPROC, (long)window_proc);
	if (!g_old_proc) log_msg("menu wheel: can't read the mouse wheel (no window procedure)");
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
		WMrMessage_Send(list, LBcMessage_Scroll, LBcNotify_Thumb, (u32)(int)((float)first / LB_RATIO(p) + 0.5f));
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
}
