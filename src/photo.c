/* Photo mode, single player only: F10 stops the game and frees the camera; F10 again (or Esc)
 * puts everything back as it was.
 *
 * Never in multiplayer: not in a network game (net_active, the lobby included), not with a local
 * player 2 on a multiplayer map (multiplayer_active, multiplayer_map), and not while lockstep.c
 * records or replays. A game that turns
 * into one of those while photo mode is on (it can't, the game stands, but a frame advance runs
 * level scripts) leaves photo mode first.
 *
 * Freezing: lockstep.c's update hook (the main loop's game update, 0x4d472b) comes here first,
 * and while photo mode is on Oni's update (0x4fafb0) doesn't run at all, as when the stock pause
 * menu is open (the display still draws every frame). Running it with the server time on the game
 * time (0 ticks, net.c over_frame) isn't enough: while a cutscene is being skipped (gs+0x40 3) it
 * runs 32 ticks a frame whatever the server time says, and the dev modes (fast, draw every frame)
 * their own counts. The server time is kept on the game time every frame, so the first frame back
 * doesn't catch up (stock 0x4fbeb0 only ever adds the time since its last call). Particles,
 * Catwalk's lights and spinning groups go by the game tick, so they stand too. "Next Frame" runs
 * the update once with the server time one tick on, an empty input record (Konoko holds nothing).
 *
 * Cutscenes: photo mode can start in the middle of one. Its camera (Oni's animate mode) is saved
 * and put back like any other, the cutscene's scripts wait on ticks, so they wait for photo mode,
 * and the letterbox bars (0x4fceb0, ECX gs+0x30, called at 0x4fbb95) are hidden while photo mode
 * is on: "Cinema bars" adds bars of its own when wanted. Keys pressed in photo mode never reach a
 * tick, so they can't skip the cutscene or open the pause menu, and a key still held when photo
 * mode ends doesn't count as a new press there.
 *
 * The camera: Oni's manual mode (0x4d7560), so no camera mode moves it; net.c's camera hook
 * (both calls of 0x4d5ca0) returns at once while photo mode is on, and this file sets the view
 * (gs+0x80: +0x58 position, +0x64 view, +0x70 up) and hands it to the renderer (0x4d6070) every
 * frame. On entry the camera's 0x84 bytes are saved; leaving copies them back, so Oni's camera
 * goes on exactly where it was, in whatever mode it was (follow, a cutscene's animation...).
 *   Free fly: the mouse looks, the movement keys move along the view, jump and crouch go up and
 *     down, the walk key or Ctrl slows down; walls stop it as they stop a watcher in a network
 *     game (net_fly_move) unless "Stop at walls" is off.
 *   Orbit: the camera circles a character (its chest: 70% of the way from its feet to its head,
 *     0x4e3460) at a distance; the mouse turns around it, forward/back come closer or go away,
 *     left/right circle, jump/crouch raise or lower the point. F switches; entering orbit picks
 *     the character nearest the middle of the screen. Left and right click pick the next or
 *     previous character.
 *   Q/E tilt (roll the up vector around the view), R levels it. The wheel zooms (the field of
 *     view, 2.5 degrees a notch, 0.5 with the walk key or Ctrl).
 * Field of view: the renderer's camera (*0x63112c: +0x38 vertical field of view in radians, +0x3c
 * aspect, +0x40 near, +0x44 far), set with 0x41b3b0 as the gs_fov_set command does, but without
 * touching that command's global (0x556010), and put back on the way out.
 *
 * What the picture shows: the HUD (gs_show_ui, 0x5533e0), the crosshair (0x4f1fa0, called at
 * 0x4fbb0a), the laser sight (0x4e1900, called at 0x4e1836), the subtitles (hud_scale.c's call of
 * 0x430400; under a cutscene's letterbox they come as console lines instead, 0x4303e0 called at
 * 0x4fbde8, which also has the developer console's lines) and a cutscene's speaker portraits (Oni_Cinematics.c OCrCinematic_Draw, 0x4f2910,
 * called at 0x4fbded) are hidden unless "Show HUD" is on; "Hide player" keeps the player's character out of the draw pass (net.c's on-screen test);
 * "Thirds grid" and "Cinema bars" (2.39:1) draw from mp_hud.c's always-on draw call. Sounds stop
 * as the pause menu stops them (0x514e60 and the channels, arklite.c sound_pause_channels; the
 * music plays on) unless "Pause sounds" is off.
 *
 * Tab opens the settings panel: the stock Options dialog (152) emptied and refilled, as the Host
 * dialog is (mp_menu.c), moved to the left edge without its background picture, so the frozen
 * scene shows beside it and the field of view and tilt change on it as they're typed. It runs as
 * the pause menu does (UI mode, game input off, the psui_oniUI part set). Tab, Esc or Resume
 * close it; F10 or Exit Photo leave photo mode. The panel's toggles and flight speed are kept in
 * Arklite.ini [photo]; the field of view and tilt start from the game's every time.
 *
 * Only this PC's view and settings: in single player, nothing here is game state except a frame
 * advance, which is a real game tick and isn't taken back. */
#include "arklite.h"

void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);

__declspec(dllimport) short WINAPI GetAsyncKeyState(int);
__declspec(dllimport) void *WINAPI GetForegroundWindow(void);
__declspec(dllimport) u32   WINAPI GetWindowThreadProcessId(void *, u32 *);
__declspec(dllimport) u32   WINAPI GetCurrentProcessId(void);
__declspec(dllimport) u32   WINAPI GetTickCount(void);
__declspec(dllimport) int   WINAPI QueryPerformanceCounter(unsigned long long *);
__declspec(dllimport) int   WINAPI QueryPerformanceFrequency(unsigned long long *);

/* ---- Engine ----------------------------------------------------------------------------------- */

#define ONgGameState            (*(u8 **)0x5ece7c)
#define ONrLevel_GetCurrent     ((u16 (ONI_FC *)(void))0x5054c0)
#define ONrGameState_Update     ((u32 (ONI_FC *)(u32, void *, u32 *))0x4fafb0)
#define GS_SYNC_BEHIND(gs)      (*(u32 *)((gs) + 0x44))
#define GS_CAMERA(gs)           (*(u8 **)((gs) + 0x80))
#define GS_PLAYER(gs)           (*(u8 **)((gs) + 0xac))
#define GS_TICK(gs)             (*(u32 *)((gs) + 0x144))
#define GS_SERVER_TIME(gs)      (*(u32 *)((gs) + 0x148))
#define GS_CHARACTERS(gs)       ((gs) + 0x1260)
#define ONgCharacterSlots       (*(u16 *)(0x4dac95 + 3))   /* char_limit.c's count */
#define CHR_SIZE                0x16a0
#define CHR_FLAGS(c)            (*(u32 *)((c) + 4))
#define CHR_INUSE               0x8000
#define CHR_NAME(c)             ((const char *)((c) + 0x14))
#define CHR_POS(c)              ((const float *)((c) + 0x148))   /* the feet */
#define ONrCharacter_GetHead    ((void (ONI_FC *)(u8 *, float *))0x4e3460)
#define ONgInvertLook           (*(u8 *)0x5ec70f)
#define ONgShowUI               (*(u8 *)0x5533e0)           /* gs_show_ui */

#define CAM_SIZE                0x84
#define CAM_LOC(c)              ((float *)((c) + 0x58))
#define CAM_VIEW(c)             ((float *)((c) + 0x64))
#define CAM_UP(c)               ((float *)((c) + 0x70))
#define CAM_ALPHA(c)            (*(float *)((c) + 0x7c))    /* the follow camera fades the player close up */
#define ONrCamera_EnterManual   ((void (ONI_FC *)(void))0x4d7560)
#define ONrCamera_Activate      ((void (ONI_FC *)(u8 *))0x4d6070)
#define M3gCamera               (*(u8 **)0x63112c)
#define M3_FOVY(m)              (*(float *)((m) + 0x38))
#define M3_ASPECT(m)            (*(float *)((m) + 0x3c))
#define M3_NEAR(m)              (*(float *)((m) + 0x40))
#define M3_FAR(m)               (*(float *)((m) + 0x44))
#define M3rCamera_SetStaticData ((void (ONI_FC *)(u8 *, u32, float, float, float, float))0x41b3b0)

#define SSgPaused               (*(u8 *)0x6200a0)
#define SSrSetPaused            ((void (ONI_FC *)(u32))0x514e60)
#define SSrChannels_Resume      ((void (*)(void))0x483660)

#define WMrUIMode_Enter         ((void (*)(void))0x476110)
#define WMrUIMode_Leave         ((void (*)(void))0x476410)
#define LIrMode_SetGameInput    ((void (ONI_FC *)(u32))0x403f70)

#define M3rScreen_Width         ((u16 (ONI_FC *)(void))0x41e0f0)
#define M3rScreen_Height        ((u16 (ONI_FC *)(void))0x41e130)
#define WMcDialogID_Options     0x98
#define WMgDesktop              (*(WMtWindow **)0x5e9970)
#define WMrWindow_Delete        ((void (ONI_FC *)(WMtWindow *))0x474950)
#define WIN_PARENT(w)           (*(WMtWindow **)((u8 *)(w) + 8))
#define WIN_OWNER(w)            (*(WMtWindow **)((u8 *)(w) + 0xc))

#define ONrCrosshair_Display    ((void (*)(void))0x4f1fa0)
#define ONcCrosshair_Call       0x4fbb0a
#define ONrLaserSight_Display   ((void (ONI_FC *)(u8 *))0x4e1900)
#define ONcLaserSight_Call      0x4e1836
#define COrConsole_DisplayLines ((void (*)(void))0x4303e0)
#define ONcConsole_LinesCall    0x4fbde8
#define ONrCinematic_Draw       ((void (*)(void))0x4f2910)
#define ONcCinematic_DrawCall   0x4fbded
#define ONrLetterBox_Display    ((void (ONI_FC *)(void *))0x4fceb0)
#define ONcLetterBox_Call       0x4fbb95

/* record buttons (bits of words 0-1): forward 21, backward 22, stepleft 25, stepright 26,
 * jump 27, crouch 28, walk 32 */
#define REC_HELD(b, bit)        ((int)((b) >> (bit) & 1))

/* ---- Settings ------------------------------------------------------------------------------- */

#define FLY_SPEED       160.0f    /* units a second at 1.00 (a watcher's, net.c) */
#define SLOW            0.25f     /* with the walk key or Ctrl */
#define TURN_SPEED      1.8f      /* orbit: radians a second for left/right */
#define TILT_SPEED      0.8f      /* radians a second */
#define FOV_MIN         5         /* degrees, vertical */
#define FOV_MAX         120
#define FOV_NOTCH       2.5f
#define DIST_MIN        4.0f
#define DIST_MAX        2000.0f
#define SPEED_MIN       10        /* hundredths */
#define SPEED_MAX       2000
#define HINT_MS         6000
#define DEG             0.017453292f
#define PI              3.14159265f
#define HALF_PI         1.5707964f

static int g_speed = 100;          /* hundredths: 1.00 */
static int g_collide = 1, g_showHud, g_hidePlayer, g_grid, g_bars, g_pauseSound = 1;

static void settings_load(void)
{
	const char *ini = ini_path();
	int s = (int)GetPrivateProfileIntA("photo", "speed", 100, ini);
	g_speed = s < SPEED_MIN ? SPEED_MIN : s > SPEED_MAX ? SPEED_MAX : s;
	g_collide = GetPrivateProfileIntA("photo", "stop_at_walls", 1, ini) != 0;
	g_showHud = GetPrivateProfileIntA("photo", "show_hud", 0, ini) != 0;
	g_hidePlayer = GetPrivateProfileIntA("photo", "hide_player", 0, ini) != 0;
	g_grid = GetPrivateProfileIntA("photo", "thirds_grid", 0, ini) != 0;
	g_bars = GetPrivateProfileIntA("photo", "cinema_bars", 0, ini) != 0;
	g_pauseSound = GetPrivateProfileIntA("photo", "pause_sounds", 1, ini) != 0;
}

static void settings_save(void)
{
	const char *ini = ini_path();
	char v[16];
	fmt(v, "%d", g_speed);
	WritePrivateProfileStringA("photo", "speed", v, ini);
	WritePrivateProfileStringA("photo", "stop_at_walls", g_collide ? "1" : "0", ini);
	WritePrivateProfileStringA("photo", "show_hud", g_showHud ? "1" : "0", ini);
	WritePrivateProfileStringA("photo", "hide_player", g_hidePlayer ? "1" : "0", ini);
	WritePrivateProfileStringA("photo", "thirds_grid", g_grid ? "1" : "0", ini);
	WritePrivateProfileStringA("photo", "cinema_bars", g_bars ? "1" : "0", ini);
	WritePrivateProfileStringA("photo", "pause_sounds", g_pauseSound ? "1" : "0", ini);
}

/* ---- Maths ---------------------------------------------------------------------------------- */

static float fsin(float x) { float r; __asm__("fsin" : "=t"(r) : "0"(x)); return r; }
static float fcos(float x) { float r; __asm__("fcos" : "=t"(r) : "0"(x)); return r; }
static float fsqrt(float x) { float r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
static float fatan2(float y, float x) { float r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- State ---------------------------------------------------------------------------------- */

static int g_on;
static u32 g_level, g_startTick;
static u8 *g_cam;                   /* Oni's camera (gs+0x80) when photo mode began */
static u8 g_savedCam[CAM_SIZE];
static float g_savedFov;            /* radians */
static u8 g_savedShowUi, g_savedPaused;
static int g_soundPaused;

static int g_orbit;
static u8 *g_target;
static float g_pos[3], g_vel[3], g_yaw, g_pitch, g_roll, g_fov;   /* g_fov: radians */
static float g_dist, g_lift;       /* orbit: distance, and how far the point is raised */
static float g_dx, g_dy;           /* mouse not yet turned */
static unsigned long long g_buttons;
static int g_wheel;
static unsigned long long g_last, g_freq;
static int g_shotIn;               /* frames until the panel's screenshot is taken */
static u32 g_hintUntil;
static int g_panel;                /* the settings panel is open */

static u8 g_zeroRecord[0x30];

static int focused(void)
{
	u32 pid = 0;
	void *w = GetForegroundWindow();
	if (w) GetWindowThreadProcessId(w, &pid);
	return pid == GetCurrentProcessId();
}

/* ---- Keys ----------------------------------------------------------------------------------- */

enum { K_F10, K_TAB, K_ESC, K_Q, K_E, K_R, K_F, K_LMB, K_RMB, K_CTRL, K_COUNT };
static const int g_vk[K_COUNT] = { 0x79, 0x09, 0x1b, 'Q', 'E', 'R', 'F', 0x01, 0x02, 0x11 };
static u32 g_keyDown, g_keyHit;

/* Every frame, the panel's too, so a key that closed the panel isn't a new press after it */
static void keys_poll(void)
{
	u32 now = 0;
	if (focused())
		for (int k = 0; k < K_COUNT; k++)
			if (GetAsyncKeyState(g_vk[k]) & 0x8000) now |= 1u << k;
	g_keyHit = now & ~g_keyDown;
	g_keyDown = now;
}

static int hit(int k) { return (int)(g_keyHit >> k & 1); }
static int held(int k) { return (int)(g_keyDown >> k & 1); }

/* ---- Characters ----------------------------------------------------------------------------- */

static u8 *character(u32 i) { return GS_CHARACTERS(ONgGameState) + i * CHR_SIZE; }

static int is_character(u8 *c)
{
	u32 n = ONgCharacterSlots;
	if (!c || !ONgGameState || c < character(0) || c >= character(n)) return 0;
	return (u32)(c - character(0)) % CHR_SIZE == 0 && (CHR_FLAGS(c) & CHR_INUSE);
}

/* The orbit point: 70% of the way from the feet to the head, raised by g_lift */
static void pivot(u8 *c, float *p)
{
	float head[3];
	const float *feet = CHR_POS(c);
	ONrCharacter_GetHead(c, head);
	for (int k = 0; k < 3; k++) p[k] = feet[k] + (head[k] - feet[k]) * 0.7f;
	p[1] += g_lift;
}

/* The next character in use after c (step 1) or before it (-1), wrapping; c itself if alone */
static u8 *step_character(u8 *c, int step)
{
	u32 n = ONgCharacterSlots, at = is_character(c) ? (u32)(c - character(0)) / CHR_SIZE : 0;
	for (u32 k = 1; k <= n; k++) {
		u32 i = (at + n + (u32)step * k) % n;
		if (CHR_FLAGS(character(i)) & CHR_INUSE) return character(i);
	}
	return is_character(c) ? c : 0;
}

/* The character nearest the middle of the screen (within 30 degrees and 1500 units), else the player */
static u8 *aimed_character(const float *pos, const float *view)
{
	u8 *best = 0;
	float bestCos = 0.866f;
	u32 n = ONgCharacterSlots;
	for (u32 i = 0; i < n; i++) {
		u8 *c = character(i);
		float p[3], d[3], len, cosv;
		if (!(CHR_FLAGS(c) & CHR_INUSE)) continue;
		pivot(c, p);
		for (int k = 0; k < 3; k++) d[k] = p[k] - pos[k];
		len = fsqrt(dot3(d, d));
		if (len < 0.01f || len > 1500.0f) continue;
		cosv = dot3(d, view) / len;
		if (cosv > bestCos) bestCos = cosv, best = c;
	}
	return best ? best : GS_PLAYER(ONgGameState);
}

/* ---- The view ------------------------------------------------------------------------------- */

static void forward_of(float yaw, float pitch, float *f)
{
	float cp = fcos(pitch);
	f[0] = cp * fsin(yaw);
	f[1] = fsin(pitch);
	f[2] = cp * fcos(yaw);
}

static void look_at(const float *from, const float *to)
{
	float d[3] = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
	if (dot3(d, d) < 1e-6f) return;
	g_yaw = fatan2(d[0], d[2]);
	g_pitch = clampf(fatan2(d[1], fsqrt(d[0] * d[0] + d[2] * d[2])), -1.55f, 1.55f);
}

static void set_fov(float rad)
{
	u8 *m = M3gCamera;
	if (m && M3_FOVY(m) != rad) M3rCamera_SetStaticData(m, 0, rad, M3_ASPECT(m), M3_NEAR(m), M3_FAR(m));
}

/* The camera's position, view and up (tilted), handed to the renderer with the field of view */
static void apply_view(void)
{
	u8 *gs = ONgGameState, *cam = gs ? GS_CAMERA(gs) : 0;
	float f[3], cp, sp, sy, cy, up0[3], right[3], cr, sr;

	if (!cam || cam != g_cam) return;
	forward_of(g_yaw, g_pitch, f);
	cp = fcos(g_pitch), sp = fsin(g_pitch), sy = fsin(g_yaw), cy = fcos(g_yaw);
	up0[0] = -sp * sy; up0[1] = cp; up0[2] = -sp * cy;
	right[0] = -cy; right[1] = 0; right[2] = sy;   /* right of the view (Oni: right of +z is -x) */
	cr = fcos(g_roll), sr = fsin(g_roll);
	for (int k = 0; k < 3; k++) {
		CAM_LOC(cam)[k] = g_pos[k];
		CAM_VIEW(cam)[k] = f[k];
		CAM_UP(cam)[k] = up0[k] * cr - right[k] * sr;   /* Q (roll > 0) leans the camera left */
	}
	CAM_ALPHA(cam) = 1.0f;
	ONrCamera_Activate(cam);
	set_fov(g_fov);
}

/* ---- Sound and HUD -------------------------------------------------------------------------- */

static void sound_pause(int pause)
{
	if (pause && !g_soundPaused) {
		g_savedPaused = SSgPaused;
		if (!g_savedPaused) SSrSetPaused(1);
		sound_pause_channels(1);
		g_soundPaused = 1;
	} else if (!pause && g_soundPaused) {
		if (!g_savedPaused) SSrSetPaused(0);
		SSrChannels_Resume();
		g_soundPaused = 0;
	}
}

static void apply_settings(void)
{
	ONgShowUI = g_showHud ? g_savedShowUi : 0;
	sound_pause(g_pauseSound);
}

int photo_hides_hud(void) { return g_on && !g_showHud; }

static void ONI_FC crosshair(void)
{
	if (!photo_hides_hud()) ONrCrosshair_Display();
}

static void ONI_FC laser_sight(u8 *c)
{
	if (!photo_hides_hud()) ONrLaserSight_Display(c);
}

static void ONI_FC console_lines(void)
{
	if (!photo_hides_hud()) COrConsole_DisplayLines();
}

static void ONI_FC cinematics(void)
{
	if (!photo_hides_hud()) ONrCinematic_Draw();
}

static void ONI_FC letterbox(void *box)
{
	if (!g_on) ONrLetterBox_Display(box);
}

int photo_hides(u8 *c)
{
	return g_on && g_hidePlayer && ONgGameState && c == GS_PLAYER(ONgGameState);
}

int photo_active(void) { return g_on; }

/* ---- On and off ----------------------------------------------------------------------------- */

/* Never in multiplayer. Locally, a player 2 only counts on a multiplayer map: on a single-player
 * map players.c can take a spare player character for player 2 (Konoko herself, while a
 * cutscene has made another character the player), which isn't a second player */
static int allowed(void)
{
	return ONgGameState && ONrLevel_GetCurrent() && !net_active() && !(multiplayer_active() && multiplayer_map()) &&
	       !lockstep_recording();
}

static void reset_camera(void)
{
	float *v = (float *)(g_savedCam + 0x64);
	memcpy(g_pos, g_savedCam + 0x58, sizeof g_pos);
	g_yaw = fatan2(v[0], v[2]);
	g_pitch = clampf(fatan2(v[1], fsqrt(v[0] * v[0] + v[2] * v[2])), -1.55f, 1.55f);
	g_roll = 0;
	g_fov = g_savedFov;
	g_orbit = 0;
	g_target = 0;
	g_lift = 0;
	g_vel[0] = g_vel[1] = g_vel[2] = 0;
}

static void enter(void)
{
	u8 *gs = ONgGameState, *cam = GS_CAMERA(gs), *m = M3gCamera;

	if (!cam || !m) return;
	g_cam = cam;
	memcpy(g_savedCam, cam, CAM_SIZE);
	g_savedFov = M3_FOVY(m);
	g_savedShowUi = ONgShowUI;
	g_soundPaused = 0;
	g_level = ONrLevel_GetCurrent();
	g_startTick = GS_TICK(gs);
	reset_camera();
	g_dx = g_dy = 0;
	g_wheel = 0;
	menu_wheel_photo_take();   /* turns from before don't count */
	g_last = 0;
	g_shotIn = 0;
	g_on = 1;
	ONrCamera_EnterManual();
	apply_settings();
	apply_view();
	g_hintUntil = GetTickCount() + HINT_MS;
	log_msg("photo: on (level %d, tick %u, field of view %d)", g_level, g_startTick, (int)(g_savedFov / DEG + 0.5f));
}

/* Everything back as it was; same_level 0: the level went (its camera with it) */
static void leave(int same_level)
{
	u8 *gs = ONgGameState;

	if (!g_on) return;
	g_on = 0;
	if (same_level && gs && GS_CAMERA(gs) == g_cam) {
		memcpy(g_cam, g_savedCam, CAM_SIZE);
		ONrCamera_Activate(g_cam);
	}
	set_fov(g_savedFov);
	ONgShowUI = g_savedShowUi;
	if (same_level) sound_pause(0);
	else if (g_soundPaused && !g_savedPaused) SSrSetPaused(0), g_soundPaused = 0;
	g_soundPaused = 0;
	log_msg("photo: off%s", same_level ? "" : " (the level ended)");
}

/* ---- Moving --------------------------------------------------------------------------------- */

static void take_records(u32 count, const u8 *actions)
{
	count &= 0xffff;
	for (u32 i = 0; actions && i < count; i++) {
		const u32 *r = (const u32 *)(actions + i * 0x30);
		float a, b;
		memcpy(&a, &r[9], 4);
		memcpy(&b, &r[10], 4);
		g_dx += a;
		g_dy += ONgInvertLook ? -b : b;
		g_buttons = (unsigned long long)r[0] | (unsigned long long)r[1] << 32;
	}
}

static void orbit_begin(u8 *target)
{
	float p[3], d[3], f[3];
	g_lift = 0;
	if (!(g_target = target)) return;
	pivot(target, p);
	for (int k = 0; k < 3; k++) d[k] = p[k] - g_pos[k];
	g_dist = clampf(fsqrt(dot3(d, d)), DIST_MIN, 300.0f);
	look_at(g_pos, p);
	forward_of(g_yaw, g_pitch, f);
	for (int k = 0; k < 3; k++) g_pos[k] = p[k] - f[k] * g_dist;
	g_orbit = 1;
	g_vel[0] = g_vel[1] = g_vel[2] = 0;
}

static void move(float dt)
{
	unsigned long long b = g_buttons;
	float take = clampf(dt * 60.0f, 0, 1), speed = FLY_SPEED * g_speed / 100.0f, f[3];
	float fwd = (float)(REC_HELD(b, 21) - REC_HELD(b, 22)), side = (float)(REC_HELD(b, 26) - REC_HELD(b, 25)),
	      up = (float)(REC_HELD(b, 27) - REC_HELD(b, 28));
	int slow = REC_HELD(b, 32) || held(K_CTRL);

	if (slow) speed *= SLOW;
	/* look: the mouse comes once a tick, so each tick's movement is spread over the next 1/60 s
	 * (1/60 radian a unit, as net.c's free flight) */
	g_yaw -= g_dx * take / 60.0f;
	g_pitch = clampf(g_pitch + g_dy * take / 60.0f, -1.55f, 1.55f);
	g_dx -= g_dx * take;
	g_dy -= g_dy * take;
	while (g_yaw > PI) g_yaw -= 2 * PI;
	while (g_yaw < -PI) g_yaw += 2 * PI;

	if (held(K_Q) != held(K_E)) g_roll += (held(K_Q) ? 1 : -1) * TILT_SPEED * (slow ? SLOW : 1) * dt;
	if (hit(K_R)) g_roll = 0;
	while (g_roll > PI) g_roll -= 2 * PI;
	while (g_roll < -PI) g_roll += 2 * PI;

	if (g_wheel) {
		float deg = g_fov / DEG - g_wheel * FOV_NOTCH * (slow ? 0.2f : 1.0f);   /* up zooms in */
		g_fov = clampf(deg, FOV_MIN, FOV_MAX) * DEG;
		g_wheel = 0;
	}

	if (g_orbit) {
		float p[3], want[3], vel[3];
		if (!is_character(g_target)) { g_orbit = 0; g_target = 0; return; }
		g_yaw -= side * TURN_SPEED * (slow ? SLOW : 1) * dt;
		g_dist = clampf(g_dist - fwd * speed * dt, DIST_MIN, DIST_MAX);
		g_lift += up * speed * 0.5f * dt;
		pivot(g_target, p);
		forward_of(g_yaw, g_pitch, f);
		for (int k = 0; k < 3; k++) want[k] = p[k] - f[k] * g_dist;
		if (g_collide) {   /* from the point out toward where the camera wants to be */
			memcpy(g_pos, p, sizeof g_pos);
			for (int k = 0; k < 3; k++) vel[k] = want[k] - p[k];
			net_fly_move(g_pos, vel, 1.0f);
			look_at(g_pos, p);
		} else {
			memcpy(g_pos, want, sizeof g_pos);
		}
		return;
	}

	/* free fly: along the view, sideways level, up and down straight; eased in and out */
	{
		float rx = -fcos(g_yaw), rz = fsin(g_yaw), wish[3], ease = clampf(dt * 10.0f, 0, 1), len;
		forward_of(g_yaw, g_pitch, f);
		wish[0] = f[0] * fwd + rx * side;
		wish[1] = f[1] * fwd + up;
		wish[2] = f[2] * fwd + rz * side;
		len = fsqrt(dot3(wish, wish));
		for (int k = 0; k < 3; k++) {
			float target = len > 0.0001f ? wish[k] / len * speed : 0;
			g_vel[k] += (target - g_vel[k]) * ease;
		}
		if (dot3(g_vel, g_vel) < 0.01f) return;
		if (g_collide) net_fly_move(g_pos, g_vel, dt);
		else for (int k = 0; k < 3; k++) g_pos[k] += g_vel[k] * dt;
	}
}

/* ---- Frames --------------------------------------------------------------------------------- */

/* One tick of the stopped game (Next Frame) */
static void advance(void)
{
	u8 *gs = ONgGameState;
	u32 ran = 0;
	GS_SERVER_TIME(gs) = GS_TICK(gs) + 1;
	GS_SYNC_BEHIND(gs) = 0;
	ONrGameState_Update(1, g_zeroRecord, &ran);
	log_msg("photo: one frame on (tick %u)", GS_TICK(gs));
}

static void panel_open(void);

/* lockstep.c's update hook, before anything else: 1 if photo mode ran the frame (its result in *err) */
int photo_update(u32 count, void *actions, u32 *ticks_run, u32 *err)
{
	u8 *gs = ONgGameState;
	unsigned long long now;
	float dt;

	int notches = menu_wheel_photo_take();   /* (also puts the window procedure that keeps F10 from Windows in place) */

	keys_poll();
	if (g_on && (!gs || ONrLevel_GetCurrent() != g_level || GS_TICK(gs) < g_startTick || GS_CAMERA(gs) != g_cam)) leave(0);
	if (g_on && !allowed()) leave(1);
	if (hit(K_F10)) {
		if (g_on) leave(1);
		else if (allowed()) enter();
		else if (gs && ONrLevel_GetCurrent()) log_msg("photo: not in multiplayer");
	}
	if (!g_on) return 0;
	if (hit(K_ESC)) {
		leave(1);
		return 0;
	}

	take_records(count, (const u8 *)actions);
	g_wheel += notches;
	if (hit(K_F)) {
		if (g_orbit) g_orbit = 0, g_target = 0;
		else {
			float f[3];
			forward_of(g_yaw, g_pitch, f);
			orbit_begin(aimed_character(g_pos, f));
		}
	}
	if (g_orbit && (hit(K_LMB) || hit(K_RMB))) {
		u8 *next = step_character(g_target, hit(K_LMB) ? 1 : -1);
		if (next && next != g_target) {
			g_dist = clampf(g_dist, DIST_MIN, 300.0f);
			g_target = next;
		}
	}

	if (!g_freq) QueryPerformanceFrequency(&g_freq);
	QueryPerformanceCounter(&now);
	dt = g_last ? (float)(long long)(now - g_last) / (float)(long long)g_freq : 0;
	g_last = now;
	move(clampf(dt, 0, 0.1f));
	apply_view();

	/* the game stands: no update (see the top) */
	GS_SERVER_TIME(gs) = GS_TICK(gs);
	GS_SYNC_BEHIND(gs) = 0;
	if (ticks_run) *ticks_run = 0;
	*err = 0;

	if (g_shotIn && !--g_shotIn) screenshot_request();
	if (hit(K_TAB)) panel_open();
	return 1;
}

int photo_owns_camera(void) { return g_on; }

/* ---- Picture -------------------------------------------------------------------------------- */

static u8 *g_black, *g_white;
static int g_overlayDrawn;   /* this frame has the hints or the grid: not one to save */

int photo_overlay_take(void)
{
	int drawn = g_overlayDrawn;
	g_overlayDrawn = 0;
	return drawn;
}

/* From mp_hud.c's always-on draw call, its fine font set */
void photo_draw(int width, int height)
{
	int lh, y;

	if (!g_on) return;
	if (!g_black) g_black = hud_solid(0x8000, "arklite_photo_black");
	if (!g_white) g_white = hud_solid(0xffff, "arklite_photo_white");
	if (g_bars) {
		int picture = (int)((float)width / 2.39f), bar = (height - picture) / 2;
		if (bar > 0) {
			hud_rect(g_black, 0, 0, width, bar, 255);
			hud_rect(g_black, 0, height - bar, width, bar, 255);
		}
	}
	if (g_grid && !g_panel && !screenshot_pending()) {
		g_overlayDrawn = 1;
		for (int i = 1; i <= 2; i++) {
			hud_rect(g_white, width * i / 3, 0, 1, height, 110);
			hud_rect(g_white, 0, height * i / 3, width, 1, 110);
		}
	}
	if (g_panel || screenshot_pending() || g_shotIn || (int)(GetTickCount() - g_hintUntil) > 0) return;
	g_overlayDrawn = 1;
	lh = mp_hud_line_height();
	y = height - 3 * lh - mp_hud_px(24);
	{
		char line[96];
		if (g_orbit && g_target) fmt(line, "Photo Mode - orbiting %s", CHR_NAME(g_target));
		else str_cpy(line, sizeof line, "Photo Mode - free fly");
		mp_hud_text(line, width / 2, y, MP_CENTER, 0xffffffffu);
	}
	mp_hud_text_plain("Mouse: look   Move keys: fly   Jump/Crouch: up/down   Walk or Ctrl: slow   Wheel: zoom",
	                  width / 2, y + lh, MP_CENTER, 0xffe0e0e0u);
	mp_hud_text_plain(g_orbit ? "Q/E: tilt   R: level   F: free fly   Clicks: next character   Tab: settings   F10: exit"
	                          : "Q/E: tilt   R: level   F: orbit a character   Tab: settings   F10: exit",
	                  width / 2, y + 2 * lh, MP_CENTER, 0xffe0e0e0u);
}

/* ---- Settings panel ------------------------------------------------------------------------- */

enum { ID_EXIT = 3, ID_NEXT = 4, ID_SHOT = 5, ID_RESET = 6, ID_RESUME_BUTTON = 7,
       ID_FLY = 110, ID_ORBIT, ID_COLLIDE, ID_HUD, ID_HIDE, ID_GRID, ID_BARS, ID_SOUND,
       ID_FOV = 120, ID_SPEED, ID_TILT };
#define ID_RESUME   2   /* the Options dialog's Close */
#define PANEL_W     310
#define PANEL_H     520
#define BUTTON_W    130
#define BUTTON_H    40

static WMtWindow *g_dialog, *g_targetLabel;
static void *g_hookOld;
static int g_leaveAfter;           /* Exit Photo: leave once the panel is gone */
static int g_initializing;

static WMtWindow *find_child(WMtWindow *dialog, int x, int y)
{
	for (WMtWindow *w = WIN_FIRSTCHILD(dialog); w; w = WIN_NEXT(w))
		if (WIN_X(w) == x && WIN_Y(w) == y) return w;
	return 0;
}

static int is_frame(WMtWindow *w, int y) { return WIN_X(w) == 5 && WIN_W(w) == 300 && WIN_Y(w) == y; }

static void tenths(char *out, float v)   /* 45, 52.5 */
{
	int t = (int)(v * 10.0f + 0.5f);
	if (t % 10) fmt(out, "%d.%d", t / 10, t % 10);
	else fmt(out, "%d", t / 10);
}

static void show_target(void)
{
	char line[64];
	if (!g_targetLabel) return;
	if (g_orbit && is_character(g_target)) str_cpy(line, sizeof line, CHR_NAME(g_target));
	else str_cpy(line, sizeof line, "None (free fly)");
	ui_set_text_fit(g_targetLabel, line);
}

static void check(WMtWindow *dialog, u32 id, int on)
{
	WMtWindow *box = WMrDialog_GetItemByID(dialog, id);
	if (box) WMrCheckBox_SetCheck(box, on != 0);
}

static void set_field(WMtWindow *dialog, u32 id, const char *text)
{
	WMtWindow *e = WMrDialog_GetItemByID(dialog, id);
	if (e) WMrMessage_Send(e, EFcMessage_SetText, (u32)text, 0);
}

static void show_values(WMtWindow *dialog, int fields)
{
	char text[16];
	check(dialog, ID_FLY, !g_orbit);
	check(dialog, ID_ORBIT, g_orbit);
	check(dialog, ID_COLLIDE, g_collide);
	check(dialog, ID_HUD, g_showHud);
	check(dialog, ID_HIDE, g_hidePlayer);
	check(dialog, ID_GRID, g_grid);
	check(dialog, ID_BARS, g_bars);
	check(dialog, ID_SOUND, g_pauseSound);
	show_target();
	if (!fields) return;
	tenths(text, g_fov / DEG);
	set_field(dialog, ID_FOV, text);
	fmt(text, "%d.%02d", g_speed / 100, g_speed % 100);
	set_field(dialog, ID_SPEED, text);
	fmt(text, "%d", (int)(g_roll / DEG + (g_roll < 0 ? -0.5f : 0.5f)));
	set_field(dialog, ID_TILT, text);
}

/* The Options dialog's popup controls (resolution, quality) own popup menus, which are top-level
 * windows: the only ones beside the panel, which in game has no menu under it. Closing a dialog
 * activates the top-level window beside it (WMrWindow_Delete 0x474950), so that was a menu,
 * whose activation went back to its owner inside the closing panel: the panel came back into the
 * desktop's list after it was unlinked and freed, the active and focused window still. Every
 * later panel then had it beside it, and once its memory went to something else (its window
 * number reused, about five panels on), closing the panel activated freed memory: a crash at
 * 0x474859. The controls go (their menus with them), so the desktop is what a closing panel
 * activates. */
static void drop_menus(WMtWindow *dialog)
{
	for (int again = 1, guard = 0; again && guard < 16; guard++) {
		again = 0;
		for (WMtWindow *w = WMgDesktop ? WIN_FIRSTCHILD(WMgDesktop) : 0; w; w = WIN_NEXT(w)) {
			WMtWindow *owner = WIN_OWNER(w), *up;
			if (w == dialog || !owner) continue;
			for (up = owner; up && up != dialog; up = WIN_PARENT(up)) {}
			if (up == dialog) {
				WMrWindow_Delete(owner == dialog ? w : owner);   /* the list changed: from the top */
				again = 1;
				break;
			}
		}
	}
}

static void panel_init(WMtWindow *dialog)
{
	WMtWindow *close = WMrDialog_GetItemByID(dialog, ID_RESUME);
	WMtWindow *box = WMrDialog_GetItemByID(dialog, 107);   /* Subtitles: On */
	WMtWindow *row = find_child(dialog, 30, 65);           /* "Quality:": precedes every frame */
	WMtWindow *header = find_child(dialog, 50, 35);        /* "Graphics" (blue) */
	WMtWindow *title = find_child(dialog, 5, 10);          /* "Options" */
	WMtWindow *frame[3] = { 0, 0, 0 }, *hidden[48], *w;
	u32 n = 0;
	int sh = M3rScreen_Height(), y;
	char text[16];

	g_dialog = dialog;
	g_targetLabel = 0;
	if (!close || !box || !row || !header || !title) {
		log_msg("photo: the Options dialog's controls are missing");
		return;
	}
	for (w = WIN_FIRSTCHILD(dialog); w; w = WIN_NEXT(w)) {
		if (is_frame(w, 50)) frame[0] = w;
		else if (is_frame(w, 175)) frame[1] = w;
		else if (is_frame(w, 290)) frame[2] = w;
		else if (n < 48) hidden[n++] = w;   /* the background picture too: the scene shows */
	}
	if (!frame[0] || !frame[1] || !frame[2]) {
		log_msg("photo: the Options dialog's frames are missing");
		return;
	}
	g_initializing = 1;
	ui_fields_begin(dialog, ID_RESUME_BUTTON);

	/* at the left edge, the middle of the screen's height */
	y = (sh - PANEL_H) / 2;
	ui_set_frame(dialog, 24, y < 0 ? 0 : y, PANEL_W, PANEL_H);
	ui_label(dialog, row, title, "Photo Mode", 5, 10, 200, 20, UI_LEFT);

	/* Camera */
	ui_set_frame(frame[0], 5, 50, 300, 166);
	ui_label(dialog, row, header, "Camera", 50, 35, 150, 20, UI_LEFT);
	ui_add(dialog, row, box, 0, ~0u, "Free fly", ID_FLY, 30, 62, 120, 20);
	ui_add(dialog, row, box, 0, ~0u, "Orbit", ID_ORBIT, 160, 62, 120, 20);
	ui_label(dialog, row, row, "Target:", 30, 86, 64, 20, UI_LEFT);
	g_targetLabel = ui_label(dialog, row, row, "", 96, 86, 189, 20, UI_LEFT);
	ui_label(dialog, row, row, "Field of view:", 30, 110, 116, 20, UI_LEFT);
	tenths(text, g_fov / DEG);
	ui_field_decimal(dialog, row, row, ID_FOV, 150, 110, 60, 5, text);
	ui_label(dialog, row, row, "degrees", 218, 110, 67, 20, UI_LEFT);
	ui_label(dialog, row, row, "Flight speed:", 30, 134, 116, 20, UI_LEFT);
	fmt(text, "%d.%02d", g_speed / 100, g_speed % 100);
	ui_field_decimal(dialog, row, row, ID_SPEED, 150, 134, 60, 5, text);
	ui_label(dialog, row, row, "times", 218, 134, 67, 20, UI_LEFT);
	ui_label(dialog, row, row, "Tilt:", 30, 158, 116, 20, UI_LEFT);
	fmt(text, "%d", (int)(g_roll / DEG + (g_roll < 0 ? -0.5f : 0.5f)));
	ui_field_integer(dialog, row, row, ID_TILT, 150, 158, 60, 4, text, -180, 180);
	ui_label(dialog, row, row, "degrees", 218, 158, 67, 20, UI_LEFT);
	ui_add(dialog, row, box, 0, ~0u, "Stop at walls", ID_COLLIDE, 30, 182, 250, 20);

	/* Picture */
	ui_set_frame(frame[1], 5, 236, 300, 96);
	ui_label(dialog, row, header, "Picture", 50, 221, 150, 20, UI_LEFT);
	ui_add(dialog, row, box, 0, ~0u, "Show HUD", ID_HUD, 30, 250, 120, 20);
	ui_add(dialog, row, box, 0, ~0u, "Hide player", ID_HIDE, 160, 250, 125, 20);
	ui_add(dialog, row, box, 0, ~0u, "Thirds grid", ID_GRID, 30, 274, 120, 20);
	ui_add(dialog, row, box, 0, ~0u, "Cinema bars", ID_BARS, 160, 274, 125, 20);
	ui_add(dialog, row, box, 0, ~0u, "Pause sounds", ID_SOUND, 30, 298, 250, 20);

	/* Buttons: full size (the art's nine-slice corners need 130 x 40) */
	ui_set_frame(frame[2], 5, 347, 300, 163);
	ui_add(dialog, row, close, 0, ~0u, "Next Frame", ID_NEXT, 17, 362, BUTTON_W, BUTTON_H);
	ui_add(dialog, row, close, 0, ~0u, "Screenshot", ID_SHOT, 163, 362, BUTTON_W, BUTTON_H);
	ui_add(dialog, row, close, 0, ~0u, "Reset Camera", ID_RESET, 17, 410, BUTTON_W, BUTTON_H);
	ui_add(dialog, row, close, 0, ~0u, "Exit Photo", ID_EXIT, 163, 410, BUTTON_W, BUTTON_H);
	/* not the stock Close: it comes after the frames in the drawing order, so the frame's dark fill
	 * covers it (it stays hidden; Esc still sends its id) */
	ui_add(dialog, row, close, 0, ~0u, "Resume", ID_RESUME_BUTTON, 90, 458, BUTTON_W, BUTTON_H);

	for (u32 i = 0; i < n; i++) WMrWindow_SetVisible(hidden[i], 0);
	drop_menus(dialog);
	show_values(dialog, 0);
	g_initializing = 0;
}

/* A decimal field's value in hundredths of a unit (from thousandths); 0 if it isn't a number */
static int field_value(WMtWindow *dialog, u32 id, int *thousandths)
{
	char text[16] = "";
	WMtWindow *e = WMrDialog_GetItemByID(dialog, id);
	if (!e) return 0;
	WMrMessage_Send(e, EFcMessage_GetText, (u32)text, sizeof text);
	text[sizeof text - 1] = 0;
	return ui_parse_thousandths(text, thousandths);
}

static int field_integer(WMtWindow *dialog, u32 id, int *out)
{
	char text[16] = "";
	const char *s;
	int v = 0, minus, any = 0;
	WMtWindow *e = WMrDialog_GetItemByID(dialog, id);
	if (!e) return 0;
	WMrMessage_Send(e, EFcMessage_GetText, (u32)text, sizeof text);
	text[sizeof text - 1] = 0;
	s = skip_space(text);
	if ((minus = *s == '-')) s++;
	for (; *s >= '0' && *s <= '9' && v < 100000; s++, any = 1) v = v * 10 + (*s - '0');
	*out = minus ? -v : v;
	return any;
}

static void panel_close(WMtWindow *dialog)
{
	WMrWindow_SetFocus(dialog);   /* a field being edited is done first */
	WMrDialog_ModalEnd(dialog, 0);
}

static u8 ONI_FC panel_proc(WMtWindow *dialog, u32 msg, u32 p1, u32 p2)
{
	u32 id = p1 & 0xffff, code = p1 >> 16;
	int v;

	if (msg == WMcMessage_InitDialog) {
		panel_init(dialog);
		return 1;
	}
	if (msg != WMcMessage_Command || g_initializing || dialog != g_dialog) return 0;

	/* the fields: at once while typing, when the text reads as a number; tidy when confirmed */
	if (id == ID_FOV || id == ID_SPEED || id == ID_TILT) {
		if (id == ID_FOV && field_value(dialog, id, &v) && v >= FOV_MIN * 1000 && v <= FOV_MAX * 1000) {
			g_fov = v / 1000.0f * DEG;
			apply_view();
		} else if (id == ID_SPEED && field_value(dialog, id, &v) && v > 0) {
			v = (v + 5) / 10;
			v = v < SPEED_MIN ? SPEED_MIN : v > SPEED_MAX ? SPEED_MAX : v;
			if (v != g_speed) g_speed = v, settings_save();
		} else if (id == ID_TILT && field_integer(dialog, id, &v) && v >= -180 && v <= 180) {
			g_roll = v * DEG;
			apply_view();
		}
		if (code == UI_NOTIFY_CONFIRMED) show_values(dialog, 1);
		return 1;
	}
	if (code != WMcNotify_Click) return 1;

	switch (id) {
	case ID_RESUME:   /* Esc (p2 0): the panel closes, photo mode goes on */
	case ID_RESUME_BUTTON:
		panel_close(dialog);
		break;
	case ID_EXIT:
		g_leaveAfter = 1;
		panel_close(dialog);
		break;
	case ID_NEXT:
		advance();
		apply_view();   /* an orbit follows its character */
		break;
	case ID_SHOT:
		g_shotIn = 3;   /* once the panel has gone from the picture */
		panel_close(dialog);
		break;
	case ID_RESET:
		reset_camera();
		apply_view();
		show_values(dialog, 1);
		break;
	case ID_FLY:
	case ID_ORBIT:
		if (id == ID_ORBIT && !g_orbit) {
			float f[3];
			forward_of(g_yaw, g_pitch, f);
			orbit_begin(aimed_character(g_pos, f));
		} else if (id == ID_FLY) {
			g_orbit = 0;
			g_target = 0;
		}
		apply_view();
		show_values(dialog, 0);
		break;
	case ID_COLLIDE: g_collide = !g_collide; break;
	case ID_HUD:     g_showHud = !g_showHud; break;
	case ID_HIDE:    g_hidePlayer = !g_hidePlayer; break;
	case ID_GRID:    g_grid = !g_grid; break;
	case ID_BARS:    g_bars = !g_bars; break;
	case ID_SOUND:   g_pauseSound = !g_pauseSound; break;
	default: return 1;
	}
	if (id >= ID_COLLIDE && id <= ID_SOUND) {
		settings_save();
		apply_settings();
		show_values(dialog, 0);
	}
	return 1;
}

/* Every frame of the panel: Tab closes it, F10 leaves photo mode */
static void panel_frame(void)
{
	ui_frame_hook_call(g_hookOld);
	ui_fields_frame();
	keys_poll();
	if (!g_dialog) return;
	if (hit(K_F10)) {
		g_leaveAfter = 1;
		panel_close(g_dialog);
	} else if (hit(K_TAB)) {
		panel_close(g_dialog);
	}
}

static void panel_open(void)
{
	u32 result = 0;
	void *ui = ONiUI_GetContext(), *part = ONiUI_FindContext(ONcUIContextName);

	g_panel = 1;
	g_leaveAfter = 0;
	g_dialog = 0;
	if (part) ONiUI_SetContext(part);
	WMrUIMode_Enter();
	LIrMode_SetGameInput(0);
	g_hookOld = ui_frame_hook(panel_frame);
	WMrDialog_ModalBegin(WMcDialogID_Options, 0, panel_proc, 0, &result);
	ui_frame_hook(g_hookOld);
	{   /* (see drop_menus) */
		int n = 0, stayed = 0;
		for (WMtWindow *w = WMgDesktop ? WIN_FIRSTCHILD(WMgDesktop) : 0; w && n < 256; w = WIN_NEXT(w), n++) stayed |= w == g_dialog;
		if (stayed) log_msg("photo: the closed panel is still in the window list (%d top-level windows)", n);
	}
	ui_fields_end();
	g_dialog = 0;
	g_targetLabel = 0;
	LIrMode_SetGameInput(1);
	WMrUIMode_Leave();
	ONiUI_SetContext(ui);
	g_panel = 0;
	g_dx = g_dy = 0;
	g_wheel = 0;
	menu_wheel_photo_take();
	g_last = 0;   /* the panel's time isn't a step */
	keys_poll();
	/* the press that closed the panel (Esc, Tab, a click on Resume) can reach the window manager
	 * before GetAsyncKeyState shows it: counted as held, it isn't a new press once the panel's gone
	 * (Esc would leave photo mode, a click would change the orbit's character) */
	g_keyDown |= 1u << K_ESC | 1u << K_TAB | 1u << K_F10 | 1u << K_LMB | 1u << K_RMB;
	if (g_leaveAfter) leave(1);
	else g_hintUntil = GetTickCount() + (g_shotIn ? 0 : HINT_MS);
}

/* ---- Install -------------------------------------------------------------------------------- */

void photo_install(void)
{
	settings_load();
	if (!patch_call(ONcCrosshair_Call, (u32)ONrCrosshair_Display, (u32)crosshair))
		log_msg("photo: crosshair call 0x%x changed, it stays in photos", ONcCrosshair_Call);
	if (!patch_call(ONcConsole_LinesCall, (u32)COrConsole_DisplayLines, (u32)console_lines))
		log_msg("photo: console lines call 0x%x changed, a cutscene's subtitles stay in photos", ONcConsole_LinesCall);
	if (!patch_call(ONcCinematic_DrawCall, (u32)ONrCinematic_Draw, (u32)cinematics))
		log_msg("photo: portrait call 0x%x changed, a cutscene's portraits stay in photos", ONcCinematic_DrawCall);
	if (!patch_call(ONcLetterBox_Call, (u32)ONrLetterBox_Display, (u32)letterbox))
		log_msg("photo: letterbox call 0x%x changed, a cutscene's bars stay in photos", ONcLetterBox_Call);
	if (!patch_call(ONcLaserSight_Call, (u32)ONrLaserSight_Display, (u32)laser_sight))
		log_msg("photo: laser sight call 0x%x changed, it stays in photos", ONcLaserSight_Call);
	log_msg("photo: F10 (single player)");
}
