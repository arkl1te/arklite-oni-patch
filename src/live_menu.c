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
 *     a tick, not even while a live dialog is open (2026-11-08: a rollback ran the tick of a
 *     console use again while that console's page was open, and its second page opened as a
 *     sub-dialog inside the tick: the game stood still in it for good, the other PC dropped this
 *     one). The exception is the text console page (dialog 0x47, the BSL text_console command,
 *     0x504c30), which consoles use to show text: it waits for the end of the tick and then opens
 *     live, set up as 0x504c30 does it (the psui_oniUI part set, game input off). Its param
 *     points at {TxtC, page 0} on that function's stack, so the request keeps a copy. In a
 *     network match it opens only on the PC whose player used the console: Oni fires a console's
 *     events (and two other object types') through 0x4f3cc0 with the character that did it in edx.
 *     An "Execute Script" event only queues the script with that character's name; the queue runs
 *     later in the tick (0x4f2120), so each queued script is run with the character its name
 *     belongs to. A page asked for outside such a call (a script that sleeps first, or one no
 *     player started) still opens on both PCs. A rollback runs the console's tick again, which
 *     asks for the page again: a request with the tick, character and page of one already taken
 *     is that one (asked_before), so the page opens once;
 *   - the text console command (0x504c30) sets the UI up around its dialog: UI part set psui_oniUI
 *     (0x46aa90), game input off (0x403f70), UI mode on (0x476110) and off (0x476410), the input
 *     and part set put back. Inside a tick in a multiplayer game, where its dialog isn't shown
 *     there and then, those calls do nothing (console_quiet): UI mode off at its end turned off
 *     the UI of a page already open (a rollback's re-run of the console's tick), and its writes
 *     made the page of these globals tick memory, which rollback then put back (below);
 *   - rollback never puts back the window manager's and the UI's globals (WM_GLOBALS_FIRST..END,
 *     rollback_keep), nor game input on/off. Something in ticks writes their page, so rollback
 *     took it for game memory: a rewind after a console page opened put back the plain game
 *     display as the modal loop's draw hook, UI mode off and the window table without the
 *     page's windows. The page's loop then drew a game nothing ran any more (no page, no
 *     network, Esc unread) and the other PC dropped this one (2026-11-08, StalwartXL), or its
 *     window had no data and ending it crashed;
 *   - Esc is read from the keyboard itself (this Oni's window in front), every main-loop frame
 *     and every frame of a live dialog: it opens the pause menu even while the game stands
 *     waiting for another PC (the action records Esc is otherwise read from come with the game's
 *     frames). The Esc that closed a dialog (the pause menu, a text console page) doesn't count
 *     again once it's gone (esc_taken);
 *   - the game's own pause flag (gs+0x1679ac, read by the main loop through 0x4fc960) is
 *     cleared every frame;
 *   - the help screen doesn't turn the sound down: 0x5018c0 halves the master volume
 *     (SSrSetMasterVolume 0x483e00, [volume], returns the current one in ST0, ret 4; call at
 *     0x501922, the value from 0x483df0 times 0.5) while it's open and puts it back at 0x501943,
 *     which then sets what it already is.
 *
 * The F2 character menu (char_menu.c) is handled here too: its keys after every frame's update,
 * and utility input is skipped while it's open, so Esc closes it rather than pausing. */
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
#define LIrMode_GetGameInput    ((u32 (*)(void))0x403f60)
#define UIrPartSpec_Find        ((void *(ONI_FC *)(const char *))0x46aa80)
#define UIgPartSpec             (*(void **)0x5e9758)
#define WMcDialog_TextConsole   0x47
#define ONrEventList_Fire       0x4f3cc0                   /* (events, character): a console's or trigger's */
#define GS_TICK_OFF             0x144                      /* u32, stock offset */
#define GS_PAUSED               0x1679ac                   /* u8, stock offset */
#define GS_LEVEL_END            0x1679bc                   /* u32, stock offset: 1 won, 2 lost */

#define ONcMainLoop_UpdateCall      0x4d472b               /* call ONrGameState_Update */
#define ONcMainLoop_ServerTimeCall  0x4d4708               /* mov ecx,[gs]; call 0x4fbeb0 (Daodan NOPs it) */
#define ONcMainLoop_SoundCall       0x4d4746               /* call SSrUpdate, after the update */
#define ONcTick_UtilityInputCall    0x4fa91c               /* call 0x4f6200, or Daodan's */
#define ONcPauseMenu_SoundPauseCall 0x4d2d4a               /* init: call SSrSetPaused(1) */
#define SSrSetPaused            ((void (ONI_FC *)(u32))0x514e60)
#define ONcHelp_VolumeDownCall  0x501922
#define UIrPartSpec_Set         ((void (ONI_FC *)(void *))0x46aa90)
/* The window manager's and the UI's globals: every address from here to the end is used by
 * their code alone (0x468000-0x47bfff): the cursor flag 0x5e96bc, the modal loop's draw hook
 * 0x5e96cc, the UI part set 0x5e9758, the window list 0x5e9970, the focus 0x5e999c, UI mode
 * 0x5e99a0, the window table 0x5e99b0 ({in use, window} per window number, which a window's
 * data lookup 0x474dc0 checks), mouse state up to 0x5ea4e4. The dwords just before (0x5e96b0-
 * 0x5e96b8) are particle code's, game state */
#define WM_GLOBALS_FIRST        0x5e96bc
#define WM_GLOBALS_END          0x5ea4e8
/* text console command 0x504c30: its UI setup calls around the dialog (see the top) */
#define ONcConsole_PartSpecSet  0x504c8e
#define ONcConsole_InputOff     0x504c9c
#define ONcConsole_UIEnter      0x504ca1
#define ONcConsole_UILeave      0x504cca
#define ONcConsole_InputBack    0x504cd1
#define ONcConsole_PartSpecBack 0x504cd8
static const u32 ONcEventList_FireCalls[] = { 0x4c0934, 0x4c28d6, 0x4cbb0a };   /* console used, and two other objects' events */
#define ONrScript_RunDeferredOne 0x5100b0                  /* (ecx script, edx character name): runs it now */
#define ONcDeferred_RunCall     0x4f21a4                   /* in 0x4f2120, the deferred-script runner */
#define CHR_NAME                0x14                       /* char[32]: the name a deferred script gets */
#define SSrSetMasterVolume      ((float (__stdcall *)(float))0x483e00)
#define SSgMasterVolumeNow      (*(float *)0x5eb744)

#define VK_ESCAPE 0x1b
__declspec(dllimport) short WINAPI GetAsyncKeyState(int);
__declspec(dllimport) void * WINAPI GetForegroundWindow(void);
__declspec(dllimport) u32  WINAPI GetWindowThreadProcessId(void *, u32 *);
__declspec(dllimport) u32  WINAPI GetCurrentProcessId(void);

static int g_open, g_pending;
static struct { int pending; WMtDialogProc proc; u32 param[2]; u8 *who; } g_console;   /* text console page after the tick */
static u8 *g_event_char;   /* the character whose console or trigger events are firing, or 0 */
static u32 g_menu_level;
static void (ONI_FC *g_utility_input)(void *);

/* The live dialogs open now, outermost first, so live_menu_close can end them: each one's proc
 * is wrapped to learn its window from its first message (a dialog being created is the newest
 * one without a window yet) */
#define LIVE_MAX_DIALOGS 8
static struct { u32 id; WMtDialogProc proc; WMtWindow *win; } g_dialogs[LIVE_MAX_DIALOGS];
static int g_ndialogs;

/* Esc, from the keyboard itself: 1 once per press, while this Oni's window is in front (two
 * Onis may share a PC). A dialog that Esc closes (the pause menu, a text console page: their
 * own keys) takes the press with it (esc_taken), so it doesn't open the menu again */
static int g_esc_down;

static int esc_down(void)
{
	u32 pid = 0;
	void *w = GetForegroundWindow();

	if (w) GetWindowThreadProcessId(w, &pid);
	return pid == GetCurrentProcessId() && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
}

static void esc_taken(void)
{
	g_esc_down = esc_down();
}

static int esc_pressed(void)
{
	int down = esc_down(), pressed;

	pressed = down && !g_esc_down;
	g_esc_down = down;
	return pressed;
}

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
	esc_pressed();   /* only followed: the dialogs take their own Esc */
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
	esc_taken();
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

/* net.c: Esc on this PC in a network game (the action bit is kept out of the shared input) */
void live_menu_request(void)
{
	if (!g_open) g_pending = 1;
}

/* The text console page a script asked for inside the tick, as 0x504c30 opens it */
static void run_console(void)
{
	void *parts = UIgPartSpec, *ui = UIrPartSpec_Find("psui_oniUI");
	u32 input = LIrMode_GetGameInput();
	u32 param[2] = { g_console.param[0], g_console.param[1] };

	if (ui) UIgPartSpec = ui;
	LIrMode_SetGameInput(0);
	WMrUIMode_Enter();
	live_menu_modal(WMcDialog_TextConsole, 0, g_console.proc, (u32)param, 0, 0);
	WMrUIMode_Leave();
	LIrMode_SetGameInput(input);
	UIgPartSpec = parts;
}

/* Whether this PC shows a text console page that character's events asked for */
static int console_for_this_pc(u8 *who)
{
	NetHud h;

	net_hud(&h);
	if (!who || h.state != NETHUD_MATCH) return 1;
	for (int s = 0; s < PLAYERS_MAX; s++)
		if (who == players_char(s)) return s == h.local_char;
	return 1;   /* an AI's: everyone's, as before */
}

static u16 ONI_FC fire_events(void *events, u8 *who)
{
	u8 *outer = g_event_char;
	u16 r;

	g_event_char = who;
	r = ((u16 (ONI_FC *)(void *, u8 *))ONrEventList_Fire)(events, who);
	g_event_char = outer;
	return r;
}

/* A console's "Execute Script" event (0x4f3d40) doesn't run the script: it queues it with the
 * character's name (0x4f2070: script, name, "eventlist"; up to 128 entries at 0x632540), and
 * 0x4f2120 runs the queue later in the tick, one 0x5100b0 call per entry. Player characters'
 * names are unique (players.c name_apart), so the name finds whose events these are */
static u16 ONI_FC run_deferred(const char *script, const char *name)
{
	u8 *outer = g_event_char, *who = 0;
	u16 r;

	for (int s = 0; s < PLAYERS_MAX && name && !who; s++) {
		u8 *c = players_char(s);
		const char *n = c ? (const char *)c + CHR_NAME : 0;
		int k = 0;
		if (!n) continue;
		while (k < 32 && n[k] == name[k] && n[k]) k++;
		if (k == 32 || n[k] == name[k]) who = c;
	}
	g_event_char = who;
	r = ((u16 (ONI_FC *)(const char *, const char *))ONrScript_RunDeferredOne)(script, name);
	g_event_char = outer;
	return r;
}

/* After the main loop's update: open a requested menu outside the tick */
static void main_loop_sound(void)
{
	/* before chat_frame and char_menu_frame: the Esc that closes them isn't a pause (the chat
	 * blocks until it's up again, the F2 menu closes when it's let go) */
	if (esc_pressed() && net_active() && live_wanted() && !g_open && !chat_blocking() && !char_menu_open())
		g_pending = 1;   /* the action records may not be coming: the game stands waiting */
	SSrUpdate();
	if (multiplayer_active()) *gs_field(GS_PAUSED) = 0;
	char_menu_frame();
	chat_frame();
	vertex_light_frame();
	spin_particle_frame();
	bench_frame();
	if (g_pending) {
		g_pending = 0;
		g_menu_level = ONrLevel_GetCurrent();
		if (level_running()) run_menu();
	}
	if (g_console.pending) {
		g_console.pending = 0;
		g_menu_level = ONrLevel_GetCurrent();
		if (level_running() && !g_open && console_for_this_pc(g_console.who)) run_console();
	}
}

static void ONI_FC pause_menu_sound_pause(u32 paused)
{
	if (!g_open) SSrSetPaused(paused);   /* the game goes on: so do its new sounds */
}


static u8 ONI_FC tracked_proc(WMtWindow *dialog, u32 msg, u32 p1, u32 p2)
{
	int i;

	for (i = g_ndialogs - 1; i >= 0; i--)
		if (g_dialogs[i].win == dialog) return g_dialogs[i].proc(dialog, msg, p1, p2);
	for (i = g_ndialogs - 1; i >= 0; i--)
		if (!g_dialogs[i].win) {
			if (dialog) g_dialogs[i].win = dialog;
			return g_dialogs[i].proc(dialog, msg, p1, p2);
		}
	return 0;
}

static u16 tracked_modal(u32 id, WMtWindow *parent, WMtDialogProc proc, u32 param, u32 *result)
{
	u16 r;

	if (g_ndialogs >= LIVE_MAX_DIALOGS) return WMrDialog_ModalBegin(id, parent, proc, param, result);
	g_dialogs[g_ndialogs].id = id;
	g_dialogs[g_ndialogs].proc = proc;
	g_dialogs[g_ndialogs].win = 0;
	g_ndialogs++;
	r = WMrDialog_ModalBegin(id, parent, tracked_proc, param, result);
	g_ndialogs--;
	return r;
}

/* net.c, before it reloads the level: a reload inside a live dialog's frame (live_frame) left
 * the dialog's loop drawing a game nothing ran any more, a frozen picture (the Windows joiner,
 * pause menu open, going back to the lobby). Ends every live dialog as Esc does (result 0: the
 * pause menu's sub-dialogs then do nothing); the menu loops return before the next main-loop
 * frame, where net.c reloads */
void live_menu_close(void)
{
	for (int i = g_ndialogs - 1; i >= 0; i--)
		if (g_dialogs[i].win) WMrDialog_ModalEnd(g_dialogs[i].win, 0);
}

/* Whether this text console request was taken before: a rollback running its tick again (up to
 * a few ticks back, and rollback.c's check runs some twice more). A request from more than
 * CONSOLE_RERUN ticks before a remembered one means a new level: those are forgotten */
#define CONSOLE_RERUN 64
static struct { u32 tick, page; u8 *who; } g_asked[8];
static u32 g_nasked;

static int asked_before(u32 tick, u8 *who, u32 page)
{
	u32 n = g_nasked < 8 ? g_nasked : 8;
	for (u32 i = 0; i < n; i++)
		if (g_asked[i].tick > tick + CONSOLE_RERUN) {
			g_nasked = 0;
			break;
		}
	n = g_nasked < 8 ? g_nasked : 8;
	for (u32 i = 0; i < n; i++)
		if (g_asked[i].tick == tick && g_asked[i].who == who && g_asked[i].page == page) return 1;
	g_asked[g_nasked % 8].tick = tick;
	g_asked[g_nasked % 8].who = who;
	g_asked[g_nasked % 8].page = page;
	g_nasked++;
	return 0;
}

u16 live_menu_modal(u32 id, WMtWindow *parent, WMtDialogProc proc, u32 param, u32 *result, int in_tick)
{
	static int told;
	u16 r;

	if (in_tick && multiplayer_active() && ONrLevel_GetCurrent()) {   /* never inside a tick (see the top) */
		if (result) *result = 0;
		if (id != WMcDialog_TextConsole || !param) {
			if (!told++) log_msg("live menu: dialog 0x%x opened inside a tick, not shown in a multiplayer game", id);
			return 0;
		}
		if (asked_before(*(u32 *)gs_field(GS_TICK_OFF), g_event_char, ((u32 *)param)[0])) return 0;
		g_console.proc = proc;
		g_console.param[0] = ((u32 *)param)[0];
		g_console.param[1] = ((u32 *)param)[1];
		g_console.who = g_event_char;
		g_console.pending = 1;
		return 0;
	}
	if (g_open) return tracked_modal(id, parent, proc, param, result);   /* a sub-dialog of a live one */
	if (!multiplayer_active() || !ONrLevel_GetCurrent())
		return WMrDialog_ModalBegin(id, parent, proc, param, result);
	if (WMgDrawHook != ONrGameState_Display) return WMrDialog_ModalBegin(id, parent, proc, param, result);
	g_menu_level = ONrLevel_GetCurrent();
	g_open = 1;
	WMgDrawHook = live_frame;
	r = tracked_modal(id, parent, proc, param, result);
	esc_taken();
	if (WMgDrawHook == live_frame) WMgDrawHook = ONrGameState_Display;
	g_open = 0;
	return r;
}


/* The text console command's UI calls: nothing inside a tick of a multiplayer game */
static int console_quiet(void)
{
	return lockstep_in_tick() && multiplayer_active() && ONrLevel_GetCurrent();
}

static void ONI_FC console_part_spec(void *spec)
{
	if (!console_quiet()) UIrPartSpec_Set(spec);
}

static void ONI_FC console_game_input(u32 on)
{
	if (!console_quiet()) LIrMode_SetGameInput(on);
}

static void console_ui_enter(void)
{
	if (!console_quiet()) WMrUIMode_Enter();
}

static void console_ui_leave(void)
{
	if (!console_quiet()) WMrUIMode_Leave();
}

static void ONI_FC utility_input(void *action)
{
	if (!g_open && !char_menu_open() && !chat_blocking()) g_utility_input(action);
}

/* The help screen's volume down: kept in multiplayer games */
static float __stdcall help_volume_down(float volume)
{
	return multiplayer_active() ? SSgMasterVolumeNow : SSrSetMasterVolume(volume);
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
	for (u32 i = 0; i < sizeof ONcEventList_FireCalls / sizeof *ONcEventList_FireCalls; i++)
		if (!patch_call(ONcEventList_FireCalls[i], ONrEventList_Fire, (u32)fire_events))
			log_msg("live menu: event call at 0x%x changed, its text console pages show on both PCs", ONcEventList_FireCalls[i]);
	if (!patch_call(ONcDeferred_RunCall, ONrScript_RunDeferredOne, (u32)run_deferred))
		log_msg("live menu: deferred script call at 0x%x changed, console pages show on both PCs", ONcDeferred_RunCall);
	{
		static const u32 sites[][3] = {
			{ ONcConsole_PartSpecSet, (u32)UIrPartSpec_Set, (u32)console_part_spec },
			{ ONcConsole_InputOff, (u32)LIrMode_SetGameInput, (u32)console_game_input },
			{ ONcConsole_UIEnter, (u32)WMrUIMode_Enter, (u32)console_ui_enter },
			{ ONcConsole_UILeave, (u32)WMrUIMode_Leave, (u32)console_ui_leave },
			{ ONcConsole_InputBack, (u32)LIrMode_SetGameInput, (u32)console_game_input },
			{ ONcConsole_PartSpecBack, (u32)UIrPartSpec_Set, (u32)console_part_spec },
		};
		u32 n = sizeof sites / sizeof *sites, i;
		for (i = 0; i < n && call_target(sites[i][0]) == sites[i][1]; i++) {}
		if (i < n) log_msg("live menu: text console call 0x%x changed: its UI calls run inside ticks too", sites[i][0]);
		else for (i = 0; i < n; i++) patch_call(sites[i][0], sites[i][1], sites[i][2]);
	}
	/* the UI's own globals, never put back by a rollback (see the top) */
	rollback_keep((void *)WM_GLOBALS_FIRST, WM_GLOBALS_END - WM_GLOBALS_FIRST);
	rollback_keep((void *)0x55fd44, 4);   /* game input on/off (0x403f70) */
	rollback_keep((void *)0x52ecf4, 4);   /* what game input on/off sets beside it */
	if (!patch_call(ONcHelp_VolumeDownCall, (u32)SSrSetMasterVolume, (u32)help_volume_down))
		log_msg("live menu: help screen code changed, it turns the sound down in multiplayer too");
	log_msg("live menu: in multiplayer games the game keeps running behind the pause menu");
}
