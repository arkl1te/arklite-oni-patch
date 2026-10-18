/* Live pause menu, for multiplayer games only: Esc opens the menu, but the game keeps running
 * behind it. In single player (no player 2, see multiplayer_active in players.c) Esc pauses
 * exactly as in stock Oni: Arklite runs the stock sequence at once, inside the tick.
 *
 * Stock Oni: utility input (0x4f6200, called by the tick at 0x4fa91c; Daodan replaces that call)
 * sees Esc inside the tick and calls 0x4bec00: UI mode on (0x476110), game input off
 * (0x403f70(0)), the pause menu (0x4d2c70: a modal dialog, 0x96), game input on, UI mode off
 * (0x476410). The modal loop (0x4689d0) draws the game every frame through the draw hook at
 * 0x5e96cc (the game display 0x4fb480 while a level is loaded, set at 0x4f571d, cleared at
 * 0x4f5783), but nothing runs the game, so it stands still until Resume.
 *
 * Arklite takes over 0x4bec00. During a level the request waits until the frame's update is
 * done (the main loop's sound update call at 0x4d4746), so the menu doesn't open inside a
 * tick. While the menu is open the draw hook runs a whole game frame: take the (empty) input
 * buffer, server time, the game update through the same call as the main loop (so lockstep.c
 * still records and replays it), sound, then the display. Sub-dialogs (Options, Load Game) run
 * the same modal loop, so the game goes on behind them too. Konoko gets no input while the menu
 * is open; enemies don't wait. Utility input is skipped while the menu is open, so the keys
 * used in the menu don't pause or open help on top of it. The menu's init pauses sound
 * (0x514e60(1) at 0x4d2d4a: the flags at 0x6200a0, which every play function checks, and
 * 0x53d878), so new sounds wouldn't start; with the game running that call is skipped.
 *
 * The game stops running behind the menu when the level ends (gs+0x1679bc, or the hook no
 * longer being ours: leaving the level clears it) and while the game's own pause flag is set
 * (gs+0x1679ac, as the main loop does).
 *
 * In a multiplayer game nothing pauses:
 *   - the pause menu is always live;
 *   - any other dialog opened outside a tick in a level runs live the same way (the help and
 *     objectives screen, dialog 0x46, from the main loop through 0x5018c0);
 *   - a dialog opened inside a tick isn't shown, since a game frame can't run in the middle of
 *     a tick (the text console screen, dialog 0x47 from 0x504cc5);
 *   - the game's own pause flag (gs+0x1679ac, read by the main loop through 0x4fc960) is
 *     cleared every frame. */
#include "arklite.h"

#define ONrPauseMenu_Run        0x4bec00                   /* the whole Esc sequence, replaced */
#define ONrPauseMenu            ((u32 (*)(void))0x4d2c70)
#define WMrUIMode_Enter         ((void (*)(void))0x476110)
#define WMrUIMode_Leave         ((void (*)(void))0x476410)
#define LIrMode_SetGameInput    ((void (ONI_FC *)(u32))0x403f70)
#define LIrActionBuffer_Get     ((void (ONI_FC *)(u16 *, void **))0x403be0)
#define ONrGameState_UpdateServerTime ((void (ONI_FC *)(void *))0x4fbeb0)
#define ONrGameState_Display    ((void (*)(void))0x4fb480)
#define SSrUpdate               ((void (*)(void))0x483bd0)
#define ONrLevel_GetCurrent     ((u16 (ONI_FC *)(void))0x5054c0)
#define ONgGameState            (*(u8 **)0x5ece7c)
#define WMgDrawHook             (*(void (**)(void))0x5e96cc)
#define GS_PAUSED               0x1679ac                   /* u8, stock offset */
#define GS_LEVEL_END            0x1679bc                   /* u32, stock offset: 1 won, 2 lost */

#define ONcMainLoop_UpdateCall      0x4d472b               /* call ONrGameState_Update */
#define ONcMainLoop_ServerTimeCall  0x4d4708               /* mov ecx,[gs]; call 0x4fbeb0 (Daodan NOPs it) */
#define ONcMainLoop_SoundCall       0x4d4746               /* call SSrUpdate, after the update */
#define ONcTick_UtilityInputCall    0x4fa91c               /* call 0x4f6200, or Daodan's */
#define ONcPauseMenu_SoundPauseCall 0x4d2d4a               /* init: call SSrSetPaused(1) */
#define SSrSetPaused            ((void (ONI_FC *)(u32))0x514e60)

static int g_open, g_pending;
static u32 g_menu_level;
static void (ONI_FC *g_utility_input)(void *);

static u32 call_target(u32 site)
{
	const u8 *p = (const u8 *)site;
	return p[0] == 0xe8 ? site + 5 + *(u32 *)(p + 1) : 0;
}

int live_menu_open(void) { return g_open; }

static u8 *gs_field(u32 stock_offset)
{
	return ONgGameState + char_limit_gs_offset(stock_offset);
}

static int level_running(void)
{
	return ONgGameState && ONrLevel_GetCurrent() == g_menu_level &&
	       !*(u32 *)gs_field(GS_LEVEL_END);
}

/* The draw hook while the menu is open: a game frame */
static void live_frame(void)
{
	if (level_running() && !*gs_field(GS_PAUSED)) {
		u16 count = 0;
		void *actions = 0;
		u32 ran;
		u8 dummy[0x30] = { 0 };
		/* what the main loop calls: lockstep.c may have hooked it */
		u32 (ONI_FC *update)(u32, void *, u32 *) = (void *)call_target(ONcMainLoop_UpdateCall);

		LIrActionBuffer_Get(&count, &actions);   /* empty: game input is off */
		if (*(u8 *)ONcMainLoop_ServerTimeCall != 0x90)   /* stock: the main loop does it */
			ONrGameState_UpdateServerTime(ONgGameState);
		update(0, dummy, &ran);
		SSrUpdate();
	}
	ONrGameState_Display();
}

static int live_wanted(void)
{
	return multiplayer_active() && ONrLevel_GetCurrent();
}

static void run_menu(void)
{
	int live = live_wanted();

	WMrUIMode_Enter();
	LIrMode_SetGameInput(0);
	if (live) {
		g_menu_level = ONrLevel_GetCurrent();
		g_open = 1;
		if (WMgDrawHook == ONrGameState_Display) WMgDrawHook = live_frame;
	}
	ONrPauseMenu();
	if (live) {
		if (WMgDrawHook == live_frame) WMgDrawHook = ONrGameState_Display;
		g_open = 0;
		g_pending = 0;
	}
	LIrMode_SetGameInput(1);
	WMrUIMode_Leave();
}

/* Replaces 0x4bec00, called from inside a tick */
static void pause_request(void)
{
	if (!live_wanted()) {
		run_menu();
		return;
	}
	if (!g_open) g_pending = 1;
}

/* After the main loop's update: open a requested menu outside the tick */
static void main_loop_sound(void)
{
	SSrUpdate();
	if (multiplayer_active()) *gs_field(GS_PAUSED) = 0;
	if (g_pending) {
		g_pending = 0;
		g_menu_level = ONrLevel_GetCurrent();
		if (level_running()) run_menu();
	}
}

static void ONI_FC pause_menu_sound_pause(u32 paused)
{
	if (!g_open) SSrSetPaused(paused);   /* the game goes on: so do its new sounds */
}

u16 live_menu_modal(u32 id, WMtWindow *parent, WMtDialogProc proc, u32 param, u32 *result, int in_tick)
{
	static int told;
	u16 r;

	if (g_open || !multiplayer_active() || !ONrLevel_GetCurrent())
		return WMrDialog_ModalBegin(id, parent, proc, param, result);
	if (in_tick) {
		if (!told++) log_msg("live menu: dialog 0x%x opened inside a tick, not shown in a multiplayer game", id);
		if (result) *result = 0;
		return 0;
	}
	if (WMgDrawHook != ONrGameState_Display) return WMrDialog_ModalBegin(id, parent, proc, param, result);
	g_menu_level = ONrLevel_GetCurrent();
	g_open = 1;
	WMgDrawHook = live_frame;
	r = WMrDialog_ModalBegin(id, parent, proc, param, result);
	if (WMgDrawHook == live_frame) WMgDrawHook = ONrGameState_Display;
	g_open = 0;
	return r;
}

static void ONI_FC utility_input(void *action)
{
	if (!g_open) g_utility_input(action);
}

void live_menu_install(void)
{
	static const u8 entry[] = { 0xe8 };   /* 0x4bec00: call 0x476110 */
	u8 jmp[5] = { 0xe9 };
	u32 rel = (u32)pause_request - (ONrPauseMenu_Run + 5);

	g_utility_input = (void *)call_target(ONcTick_UtilityInputCall);
	if (!code_matches(ONrPauseMenu_Run, entry, 1) || call_target(ONrPauseMenu_Run) != (u32)WMrUIMode_Enter ||
	    call_target(ONcMainLoop_SoundCall) != (u32)SSrUpdate || !call_target(ONcMainLoop_UpdateCall) ||
	    !g_utility_input || call_target(ONcPauseMenu_SoundPauseCall) != (u32)SSrSetPaused) {
		log_msg("live menu: game code changed, skipped (Esc pauses the game, also in multiplayer)");
		return;
	}
	*(u32 *)(jmp + 1) = rel;
	patch_write(ONrPauseMenu_Run, jmp, sizeof jmp);
	patch_call(ONcMainLoop_SoundCall, (u32)SSrUpdate, (u32)main_loop_sound);
	patch_call(ONcTick_UtilityInputCall, (u32)g_utility_input, (u32)utility_input);
	patch_call(ONcPauseMenu_SoundPauseCall, (u32)SSrSetPaused, (u32)pause_menu_sound_pause);
	log_msg("live menu: in multiplayer games the game keeps running behind the pause menu");
}
