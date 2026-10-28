/* Player names in multiplayer games: what each player is called, typing a new one in the F2 menu
 * (char_menu.c), and the name over another player when this PC's player faces them.
 *
 * Names: Arklite.ini [multiplayer] name is this PC's player (default "Player"), [players]
 * p2_name the gamepad player 2 of a game on one PC (default "Player 2"). Typing a new name in the
 * F2 menu saves it there, so it's kept for the next game. In a network game each PC sends its
 * own player's name to the other (net.c, a NAME packet every second and at once on a change);
 * names are only shown, never part of the game state, so they can't desync anything. A name is
 * at most 23 characters of plain ASCII (what Oni's fonts draw).
 *
 * Typing: while the name is being edited, Oni's window gets our window procedure in front of its
 * own (SetWindowLongA GWL_WNDPROC, installed the first time and then left in place; it passes
 * everything on when nothing is being typed), which takes WM_CHAR: letters, Backspace, Enter
 * (keep) and Esc (put the old name back). Key down and up still reach Oni; this PC's player gets
 * no game input while the F2 menu is up anyway.
 *
 * Facing: every frame, in the HUD (mp_hud.c; the display runs with this PC's player in gs+0xac),
 * each other player within NAME_RANGE whose feet, middle or head (0x4e3460: ECX character, EDX
 * the head's position out) is on screen in the zone players look at: the middle 3/11 of the
 * width, from the top edge down to 3/5 of the height (the camera looks down over this PC's
 * player, so the others sit above the center). A point there counts if no wall is between it
 * and the camera: the level's ray query 0x441c80 (ECX the level's collision ([gs+0x150]+0x40),
 * EDX the start, [the ray: the whole segment], [quad flags to skip], [1]; ret 0xc; al 1: hit),
 * what the laser sight's test 0x4d4e50 uses with the flags 0x44800, plus 0x20: the quads the
 * camera made see-through this frame (it sets 0x20 on what stands between it and the player,
 * cleared by 0x41aab0), so a wall the camera shows through doesn't hide a name. The name is
 * drawn over the head, and NAME_HOLD_MS after the player leaves the zone, so it doesn't flicker. The camera is Motoko's, *0x631004: +0x08
 * position, +0x14 view direction, +0x20 up, +0x38 vertical field of view (radians), +0x3c
 * aspect (0x41b4a0 and 0x41b3b0 read and set them).
 *
 * Chat (chat.c): what another player said shows over their head for SAY_MS, above their name
 * (drawn then too, faced or not), wherever they are on screen within SAY_RANGE with no wall in
 * between. It's wrapped to the width of SAY_WIDTH_TEXT in the HUD's chat font (the user's pick),
 * at most SAY_LINES lines; longer messages end in "...". It sits half a line over the name, on a
 * pill (a rectangle whose ends are half circles as tall as it) of 10% black. M3rDraw_Bitmap
 * (0x41f4a0) draws a texture one texel per pixel and repeats it past its size (u = width /
 * texture width), so only a one-color texture can be drawn at any size: the ends are drawn a row
 * of pixels at a time with the solid one, the edge pixel of each row at the share of it the
 * circle covers, so they're smooth and no pixel is covered twice. */
#include "arklite.h"


#define ONgGameState        (*(u8 **)0x5ece7c)
#define GS_PLAYER(gs)       (*(u8 **)((gs) + 0xac))
#define M3gActiveCamera     (*(u8 **)0x631004)
#define CAM_POS(c)          ((const float *)((c) + 0x08))
#define CAM_VIEW(c)         ((const float *)((c) + 0x14))
#define CAM_UP(c)           ((const float *)((c) + 0x20))
#define CAM_FOVY(c)         (*(const float *)((c) + 0x38))
#define CAM_ASPECT(c)       (*(const float *)((c) + 0x3c))
#define GS_COLLISION(gs)    (*(u8 **)(*(u8 **)((gs) + 0x150) + 0x40))
#define AKrCollision_Ray    ((u8 (ONI_FC *)(u8 *, const float *, const float *, u32, u32))0x441c80)
#define QUAD_SKIP           0x44800  /* what the laser sight's test skips */
#define QUAD_SEE_THROUGH    0x20     /* set by the camera on the quads in its way */
#define CHR_POS(c)          ((float *)((c) + 0x148))
#define ONrCharacter_GetHead ((void (ONI_FC *)(u8 *, float *))0x4e3460)

#define CHR_FLAGS(c)        (*(u32 *)((c) + 4))
#define CHR_INUSE           0x8000
#define CHR_DEAD            0x1

#define NAME_RANGE          400.0f
#define NAME_HOLD_MS        400
#define ZONE_X              (3.0f / 11)   /* half the zone's width, of the half screen */
#define ZONE_BOTTOM         (-0.2f)       /* 3/5 of the height down from the top: -1 bottom, 1 top */
#define NAME_ABOVE          4.0f     /* units over the head */
#define SAY_MS              6000
#define SAY_RANGE           1000.0f
#define SAY_LINES           2
#define SAY_WIDTH_TEXT      "This width is allowed"
#define SAY_SHADE           26       /* 10% black */
#define SAY_COLOR           0xffc8e6ffu

#define GWL_WNDPROC         (-4)
#define WM_CHAR             0x102

typedef long (WINAPI *WNDPROC)(void *, u32, u32, long);
__declspec(dllimport) long WINAPI SetWindowLongA(void *, int, long);
__declspec(dllimport) long WINAPI CallWindowProcA(WNDPROC, void *, u32, u32, long);
__declspec(dllimport) void * WINAPI GetForegroundWindow(void);
__declspec(dllimport) u32  WINAPI GetWindowThreadProcessId(void *, u32 *);
__declspec(dllimport) u32  WINAPI GetCurrentProcessId(void);
__declspec(dllimport) u32  WINAPI GetTickCount(void);

static char g_name[MP_NAME_MAX], g_p2_name[MP_NAME_MAX], g_peer[MP_NAME_MAX];
static char g_edit[MP_NAME_MAX], g_before[MP_NAME_MAX];
static int g_editing;
static u32 g_serial = 1;
static WNDPROC g_old_proc;
static void *g_hwnd;
static u8 *g_target;               /* the player the name is over */
static u32 g_target_seen;
static char g_say[2][CHAT_MAX];    /* chat: the last message of each player, and when */
static u32 g_say_at[2];
static u8 *g_pill;

/* ---- Names -------------------------------------------------------------------------------- */

static int same(const char *a, const char *b)
{
	while (*a && *a == *b) a++, b++;
	return *a == *b;
}

/* Plain ASCII, no leading or trailing spaces, at most MP_NAME_MAX - 1; fallback if empty */
static void clean(char *d, const char *s, const char *fallback)
{
	unsigned n = 0;
	while (*s == ' ') s++;
	for (; *s && n < MP_NAME_MAX - 1; s++)
		if (*s >= 0x20 && *s < 0x7f) d[n++] = *s;
	while (n && d[n - 1] == ' ') n--;
	d[n] = 0;
	if (!n) str_cpy(d, MP_NAME_MAX, fallback);
}

const char *mp_name_local(void) { return g_name; }
u32 mp_name_serial(void) { return g_serial; }

void mp_name_set_peer(const char *name)
{
	char n[MP_NAME_MAX];
	clean(n, name, "");
	if (same(n, g_peer)) return;
	str_cpy(g_peer, sizeof g_peer, n);
	if (*n) log_msg("names: the other player is %s", n);
}

const char *mp_name(int who)
{
	static char fallback[16];
	NetHud h;

	net_hud(&h);
	if (h.state == NETHUD_OFF) return who == 1 ? g_p2_name : g_name;
	if (who == h.local_player) return g_name;
	if (*g_peer) return g_peer;
	fmt(fallback, "Player %d", who + 1);
	return fallback;
}

/* ---- Typing ------------------------------------------------------------------------------- */

static void edit_char(u32 c)
{
	unsigned n = str_len(g_edit);
	if (c == 8) {
		if (n) g_edit[n - 1] = 0;
	} else if (c == 13) {
		mp_name_edit_end(1);
	} else if (c == 27) {
		mp_name_edit_end(0);
	} else if (c >= 0x20 && c < 0x7f && n < MP_NAME_MAX - 1 && (n || c != ' ')) {
		g_edit[n] = (char)c;
		g_edit[n + 1] = 0;
	}
}

static long WINAPI window_proc(void *hwnd, u32 msg, u32 wp, long lp)
{
	if (g_editing && msg == WM_CHAR) {
		edit_char(wp);
		return 0;
	}
	return CallWindowProcA(g_old_proc, hwnd, msg, wp, lp);
}

int mp_name_editing(void) { return g_editing; }

void mp_name_edit_begin(void)
{
	if (g_editing) return;
	if (!g_old_proc) {
		u32 pid = 0;
		void *w = GetForegroundWindow();
		if (w) GetWindowThreadProcessId(w, &pid);
		if (!w || pid != GetCurrentProcessId()) return;
		g_hwnd = w;
		g_old_proc = (WNDPROC)SetWindowLongA(w, GWL_WNDPROC, (long)window_proc);
		if (!g_old_proc) {
			log_msg("names: can't read the keyboard (no window procedure)");
			return;
		}
	}
	str_cpy(g_before, sizeof g_before, g_name);
	str_cpy(g_edit, sizeof g_edit, g_name);
	g_editing = 1;
}

void mp_name_edit_end(int keep)
{
	if (!g_editing) return;
	g_editing = 0;
	if (!keep) return;
	clean(g_name, g_edit, g_before);
	if (same(g_name, g_before)) return;
	g_serial++;
	WritePrivateProfileStringA("multiplayer", "name", g_name, ini_path());
	log_msg("names: this PC's player is now %s (saved in Arklite.ini)", g_name);
}

const char *mp_name_edit_text(void) { return g_editing ? g_edit : g_name; }

/* ---- The name over the player this PC's player faces ---------------------------------------- */

static float fsqrt(float x)
{
	float r;
	__asm__("fsqrt" : "=t"(r) : "0"(x));
	return r;
}

static float ftan(float x)
{
	float r;
	__asm__("fptan\n\tfstp %%st(0)" : "=t"(r) : "0"(x));
	return r;
}

static float dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void cross(const float *a, const float *b, float *o)
{
	o[0] = a[1] * b[2] - a[2] * b[1];
	o[1] = a[2] * b[0] - a[0] * b[2];
	o[2] = a[0] * b[1] - a[1] * b[0];
}

static int norm(float *v)
{
	float l = fsqrt(dot(v, v));
	if (l < 1e-6f) return 0;
	v[0] /= l; v[1] /= l; v[2] /= l;
	return 1;
}

static int other_player(u8 *c, u8 *me)
{
	if (!c || c == me || !(CHR_FLAGS(c) & CHR_INUSE) || (CHR_FLAGS(c) & CHR_DEAD)) return -1;
	for (int who = 0; who < 2; who++)
		if (c == players_char(who)) return who;
	return -1;
}

typedef struct { const float *pos; float f[3], right[3], u[3], t, aspect; u8 *collision; } View;

/* In the screen rectangle (ndc x: -zx..zx, y: bottom..1, y up) within range, and no wall between
 * it and the camera */
static int in_view(const View *v, const float *p, float zx, float bottom, float range)
{
	float rel[3];
	for (int k = 0; k < 3; k++) rel[k] = p[k] - v->pos[k];
	float z = dot(rel, v->f);
	if (z < 1.0f || dot(rel, rel) > range * range) return 0;
	float x = dot(rel, v->right) / (z * v->t * v->aspect), y = dot(rel, v->u) / (z * v->t);
	if (x < -zx || x > zx || y < bottom || y > 1.0f) return 0;
	return !v->collision || !AKrCollision_Ray(v->collision, v->pos, rel, QUAD_SKIP | QUAD_SEE_THROUGH, 1);
}

/* Its feet, middle or head */
static int seen(const View *v, u8 *c, float zx, float bottom, float range)
{
	float head[3], mid[3];
	ONrCharacter_GetHead(c, head);
	for (int k = 0; k < 3; k++) mid[k] = (head[k] + CHR_POS(c)[k]) * 0.5f;
	return in_view(v, head, zx, bottom, range) || in_view(v, mid, zx, bottom, range) ||
	       in_view(v, CHR_POS(c), zx, bottom, range);
}

/* In the zone players look at */
static int facing(const View *v, u8 *c) { return seen(v, c, ZONE_X, ZONE_BOTTOM, NAME_RANGE); }

void mp_names_say(int who, const char *text)
{
	if (who < 0 || who > 1) return;
	str_cpy(g_say[who], sizeof g_say[who], text);
	g_say_at[who] = GetTickCount() | 1;
}

/* ---- What a player said, over their name ---------------------------------------------------- */

/* A pill: the middle from x + h/2 to x + w - h/2, half circles of radius h/2 at both ends */
static void draw_pill(int x, int top, int w, int h, u32 alpha)
{
	float R = h * 0.5f;
	int r = h / 2, cl = x + r, cr = x + w - r;   /* the ends' centers */

	hud_rect(g_pill, cl, top, cr - cl, h, alpha);
	for (int j = 0; j < h; j++) {
		float dy = j + 0.5f - R, hw = R * R - dy * dy > 0 ? fsqrt(R * R - dy * dy) : 0;
		int full = (int)hw;   /* whole pixels the circle covers out from the center on this row */
		u32 edge = (u32)((hw - full) * alpha + 0.5f);
		hud_rect(g_pill, cl - full, top + j, full, 1, alpha);
		hud_rect(g_pill, cr, top + j, full, 1, alpha);
		if (edge) {
			hud_rect(g_pill, cl - full - 1, top + j, 1, 1, edge);
			hud_rect(g_pill, cr + full, top + j, 1, 1, edge);
		}
	}
}

/* text cut to at most SAY_LINES lines of SAY_WIDTH_TEXT's width, the last ending in "..." if it
 * had to be cut; lines[i] gets each line */
static int say_lines(const char *text, char lines[SAY_LINES][CHAT_MAX + 4], int max_w)
{
	int starts[SAY_LINES + 1], len = str_len(text);
	int n = chat_wrap(text, max_w, starts, SAY_LINES + 1), cut = n > SAY_LINES;

	if (cut) n = SAY_LINES;
	for (int l = 0; l < n; l++) {
		int a = starts[l], b = l + 1 < n ? starts[l + 1] : len, k;
		while (b > a && text[b - 1] == ' ') b--;
		for (k = a; k < b; k++) lines[l][k - a] = text[k];
		lines[l][b - a] = 0;
		if (cut && l == n - 1) {   /* the rest goes: as much of this line as fits with "..." */
			int m = b - a;
			for (;;) {
				lines[l][m] = '.'; lines[l][m + 1] = '.'; lines[l][m + 2] = '.'; lines[l][m + 3] = 0;
				if (!m || mp_hud_text_width(lines[l], -1) <= max_w) break;
				m--;
				while (m && lines[l][m - 1] == ' ') m--;
			}
		}
	}
	return n;
}

/* Centered on x, its bottom half a line over the name line at y */
static void draw_say(const char *text, int x, int y)
{
	char lines[SAY_LINES][CHAT_MAX + 4];
	int lh = mp_hud_line_height(), max_w = mp_hud_text_width(SAY_WIDTH_TEXT, -1), w = 0;
	int n = say_lines(text, lines, max_w);

	if (!n || lh <= 0) return;
	for (int l = 0; l < n; l++) {
		int lw = mp_hud_text_width(lines[l], -1);
		if (lw > w) w = lw;
	}
	int step = lh + 2, pad_y = lh / 3, h = n * step - 2 + 2 * pad_y, r = h / 2;
	int bottom = y - lh / 2, top = bottom - h;   /* half a line between it and the name */
	int mid_w = w + lh - 2 * r > 0 ? w + lh - 2 * r : 0;   /* the text and a little room fit between the ends' centers */
	int left = x - mid_w / 2 - r;
	if (top < 0) return;
	draw_pill(left, top, mid_w + 2 * r, h, SAY_SHADE);
	for (int l = 0; l < n; l++) mp_hud_text(lines[l], x, top + pad_y + l * step, MP_CENTER, SAY_COLOR);
}

/* Where the name line over c's head goes (its top center); 0 if off screen */
static int name_point(const View *v, u8 *c, int width, int height, int *x, int *y)
{
	float head[3], rel[3];
	ONrCharacter_GetHead(c, head);
	head[1] += NAME_ABOVE;
	for (int k = 0; k < 3; k++) rel[k] = head[k] - v->pos[k];
	float z = dot(rel, v->f);
	if (z < 1.0f) return 0;
	*x = (int)(width * 0.5f * (1 + dot(rel, v->right) / (z * v->t * v->aspect)));
	*y = (int)(height * 0.5f * (1 - dot(rel, v->u) / (z * v->t))) - mp_hud_line_height();
	return *x >= 0 && *x < width && *y >= 0 && *y < height;
}

void mp_names_draw(int width, int height)
{
	u8 *gs = ONgGameState, *cam = M3gActiveCamera, *me;
	float up[3];
	u32 now = GetTickCount();
	View v;

	if (!gs || !cam || !(me = GS_PLAYER(gs)) || char_menu_open() || live_menu_open()) {
		g_target = 0;
		return;
	}
	for (int k = 0; k < 3; k++) { v.f[k] = CAM_VIEW(cam)[k]; up[k] = CAM_UP(cam)[k]; }
	if (!norm(v.f)) return;
	cross(v.f, up, v.right);
	if (!norm(v.right)) return;
	cross(v.right, v.f, v.u);
	v.pos = CAM_POS(cam);
	float fovy = CAM_FOVY(cam);
	if (fovy <= 0.05f || fovy >= 3.0f) fovy = 0.785398f;
	v.aspect = CAM_ASPECT(cam);
	if (v.aspect <= 0.1f) v.aspect = (float)width / (float)height;
	v.t = ftan(fovy * 0.5f);
	v.collision = *(u8 **)(gs + 0x150) ? GS_COLLISION(gs) : 0;

	/* the one already named keeps it while it's still faced; else the first one faced */
	if (g_target && other_player(g_target, me) >= 0 && facing(&v, g_target)) g_target_seen = now;
	else
		for (int i = 0; i < 2; i++) {
			u8 *c = players_char(i);
			if (c != g_target && other_player(c, me) >= 0 && facing(&v, c)) {
				g_target = c;
				g_target_seen = now;
				break;
			}
		}
	if (g_target && (now - g_target_seen > NAME_HOLD_MS || other_player(g_target, me) < 0)) g_target = 0;

	if (!g_pill) g_pill = hud_solid(0x8000, "arklite_say_pill");

	/* the name of the one faced, and of anyone talking, with what they said over it */
	for (int i = 0; i < 2; i++) {
		u8 *c = players_char(i);
		int who = other_player(c, me), x, y, talking;
		if (who < 0) continue;
		talking = g_say_at[who] && now - g_say_at[who] < SAY_MS && seen(&v, c, 1.0f, -1.0f, SAY_RANGE);
		if ((c != g_target && !talking) || !name_point(&v, c, width, height, &x, &y)) continue;
		mp_hud_text(mp_name(who), x, y, MP_CENTER, 0xffffffffu);
		if (!talking) continue;
		draw_say(g_say[who], x, y);
	}
}

void mp_names_install(void)
{
	char s[64];
	GetPrivateProfileStringA("multiplayer", "name", "Player", s, sizeof s, ini_path());
	clean(g_name, s, "Player");
	GetPrivateProfileStringA("players", "p2_name", "Player 2", s, sizeof s, ini_path());
	clean(g_p2_name, s, "Player 2");
	log_msg("names: this PC's player is %s", g_name);
}
