/* Character choice in multiplayer games (F2): a menu over the running game, like the F1 help
 * screen (nothing pauses), with a picture of every character in the roster. The character picked
 * is the one this PC's player changes into at once (or comes back as, if dead).
 *
 * Only on a multiplayer map in a multiplayer game (players.c), in a level, while the pause menu
 * isn't open. While the menu is up this PC's player gets no game input (LIrMode_SetGameInput
 * 0x403f70, as the help screen does) and utility input is skipped (live_menu.c), so Esc closes
 * the menu instead of opening the pause menu. Keys: arrows or the mouse pick, Enter, Space or a
 * click chooses and closes, F2 or Esc closes. A living player changes at once, wherever they are
 * (players.c); a dead one at the respawn. Keys are read with GetAsyncKeyState only while this
 * Oni window has the focus (two Onis may share a PC, as for F3 in net.c).
 *
 * The choice reaches the game through the action records, so every PC and every replay sees it
 * at the same tick: word 11 of each record (analogs[9], which the tick, 0x4fa6a0, never reads)
 * carries PLAYERS_CHOICE_TAG | (class instance number + 1), or the tag alone for "the map's
 * class". lockstep.c's update hook writes it into this PC's records of every frame (and net.c
 * into each input it makes); players.c reads it in the tick from Konoko's record (the tick's
 * action) and player 2's, and swaps the class of the player's placement for the respawn. The
 * number is the class's TMrInstance_GetDataPtr_ByNumber index among the loaded ONCCs, the same
 * on every PC with the same game files (net.c compares level0's hash).
 *
 * Roster: Arklite.ini [multiplayer] characters = class names separated by commas (level0 ONCC
 * names, e.g. konoko_generic); the default is 18 of the game's characters. Classes that aren't
 * loaded or don't have the 19-part biped body are left out.
 *
 * Variations: each character also offers the game's other versions of it (Konoko's outfits, the
 * Strikers' easy/medium/hard colors, the thug gangs...), from g_families, or for a class that
 * isn't there, every loaded class with the same name apart from its number and difficulty
 * (thug_wh_1 -> thug_wh_2..5). A roster entry can list its own: konoko_generic|k3|k4_A. Q/E or
 * Page Up/Down, or a click on the "< 2 / 7 >" line, change the picked character's variation; the
 * choice is that class's number like any other, so the network and replays need nothing new.
 * The picture of a variation is rendered the first time it's shown, with the character's pose.
 *
 * Name: the line under the title is this PC's player's name (mp_names.c); Tab or a click on it
 * starts typing, Enter keeps the new name (saved in Arklite.ini), Esc puts the old one back.
 *
 * Pictures: rendered once in software, the first time the menu opens, from the class's own
 * data in memory, then kept as textures:
 *   - pose: each class's own fighting stance or taunt, from its animation collection (ONCC +0xc80
 *     the TRAC: +0x10 the parent collection, +0x16 the entry count, entries of 12 bytes from
 *     +0x18 with the TRAM at +8, as 0x42c1b0 walks them), the class's own entries before its
 *     parents'. Wanted, in order: <xxx>COMtaunt1, taunt2, then COMidle1-4 (the stance); each
 *     class takes the first one no class before it in the roster took, so characters that share
 *     moves (Griffin and Comguy) still stand differently. A taunt is shown at 55% of its frames
 *     (TRAM +0x164 the frame count), a stance at frame 0. Without any, Konoko's idle (KONOKOidle1;
 *     every biped shares the 19 bones). The rotations come from 0x427810 (ECX animation, EDX frame, [quaternions out], ret 4) and the bone
 *     matrices from 0x4274c0 (ECX body, EDX attachments or 0, [quaternions], [root matrix],
 *     [bone mask], [matrices out], ret 0x10), what the game's own character update uses (call
 *     at 0x4e5bdf). Matrices are 4x3, column-major: p' = m0*x + m3*y + m6*z + m9.
 *   - body: ONCC +0xc34 the body set (5 TRCM, +0x10 the most detailed), TRCM +4 the part count,
 *     +0x48 the parts' M3GM; ONCC +0xc38 the TRMA, its textures from +0x18, one per part (what
 *     0x427da0 gives the parts before drawing). M3GM +0x04 PNTA (+0x34 count, +0x38 xyz), +0x10
 *     TXCA (+0x18 uv), +0x14 IDXA (+0x14 count, +0x18 triangle strips, bit 31 starts a strip).
 *   - textures: TXMP +0x80 flags (0x2000: +0x94 is a pointer to the texels), +0x84/+0x86 size,
 *     +0x88 pixel type: 0 ARGB4444, 1 RGB555, 2 ARGB1555, 7 ARGB8888, 8 RGB888, 9 DXT1 (from the
 *     table at 0x529e78); level 0 of the mipmaps comes first. Alpha is left out (on characters it
 *     is mostly the reflection mask).
 *   - view: orthographic, from the front-left and above (Catwalk's previews), fitted to the
 *     picture. Oni is Y up; the front is where the toes point.
 * Pictures and the menu's plain rectangles are ARGB1555 textures made with M3rTextureMap_New
 * (0x41eb00: CX, DX the size, [pixel type], [3: in the first dynamic instance file, kept until
 * the game exits], [flags], [name], [texture out]; it rounds the size up to powers of two) at
 * 8x8, then pointed at texels Arklite allocates (see texture_new),
 * drawn with M3rDraw_Bitmap (0x41f4a0: ECX texture, EDX top-left {x, y, z, 1/w}, [width],
 * [height], [shade], [alpha]; texels 1:1). ARGB1555 because the GL upload converts it (the
 * converter table at 0x535a90 has no ARGB8888 to RGBA bytes). */
#include "arklite.h"

void *memset(void *, int, unsigned);
void *memcpy(void *, const void *, unsigned);

typedef struct { int x, y; } POINT;
typedef struct { int left, top, right, bottom; } RECT;
__declspec(dllimport) short WINAPI GetAsyncKeyState(int);
__declspec(dllimport) void * WINAPI GetForegroundWindow(void);
__declspec(dllimport) u32  WINAPI GetWindowThreadProcessId(void *, u32 *);
__declspec(dllimport) u32  WINAPI GetCurrentProcessId(void);
__declspec(dllimport) int  WINAPI GetCursorPos(POINT *);
__declspec(dllimport) int  WINAPI ScreenToClient(void *, POINT *);
__declspec(dllimport) int  WINAPI GetClientRect(void *, RECT *);
__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) u32  WINAPI GetTickCount(void);
#define MEM_COMMIT      0x1000
#define MEM_RESERVE     0x2000
#define PAGE_READWRITE  0x04

#define TAG_ONCC                0x4f4e4343
#define TAG_TRAM                0x5452414d
#define TMrInstance_GetDataPtr  ((u16 (ONI_FC *)(u32, const char *, void *))0x4232e0)
#define TRrAnimation_GetRotations ((void (ONI_FC *)(void *, u32, float *))0x427810)
#define TRrBody_BuildMatrices   ((void (ONI_FC *)(void *, void *, const float *, const float *, u32, float *))0x4274c0)
#define M3rTextureMap_New       ((u16 (ONI_FC *)(u32, u32, u32, u32, u32, const char *, u8 **))0x41eb00)
#define M3rDraw_Bitmap          ((void (ONI_FC *)(void *, const float *, u32, u32, u32, u32))0x41f4a0)
#define M3rScreen_Width         ((u16 (ONI_FC *)(void))0x41e0f0)
#define M3rScreen_Height        ((u16 (ONI_FC *)(void))0x41e130)
#define LIrMode_GetGameInput    ((u32 (ONI_FC *)(void))0x403f60)
#define LIrMode_SetGameInput    ((void (ONI_FC *)(u32))0x403f70)
#define ONrLevel_GetCurrent     ((u16 (ONI_FC *)(void))0x5054c0)
#define ONgGameState            (*(u8 **)0x5ece7c)

#define ONCC_BODY(c)            (*(u8 **)((c) + 0xc34))
#define ONCC_TEXTURES(c)        (*(u8 **)((c) + 0xc38))
#define TRCM_PARTS(b)           (*(u16 *)((b) + 4))
#define TRCM_GEOMETRY(b)        (*(u8 ***)((b) + 0x48))
#define TRAM_BONES(a)           (*(u16 *)((u8 *)(a) + 0x162))
#define TRAM_FRAMES(a)          (*(u16 *)((u8 *)(a) + 0x164))
#define ONCC_ANIMATIONS(c)      (*(u8 **)((c) + 0xc80))      /* TRAC */
#define TRAC_PARENT(t)          (*(u8 **)((t) + 0x10))
#define TRAC_COUNT(t)           (*(u16 *)((t) + 0x16))
#define TRAC_ANIMATION(t, i)    (*(void **)((t) + 0x20 + 12 * (i)))
#define TXMP_FLAGS(t)           (*(u32 *)((t) + 0x80))
#define TXMP_W(t)               (*(u16 *)((t) + 0x84))
#define TXMP_H(t)               (*(u16 *)((t) + 0x86))
#define TXMP_TYPE(t)            (*(u32 *)((t) + 0x88))
#define TXMP_TEXELS(t)          (*(u8 **)((t) + 0x94))
#define TXMP_TEXELS_LOADED      0x2000
#define PIXEL_ARGB1555          2

#define PARTS           19
#define FOOT_L          3
#define FOOT_R          6
#define MAX_ROSTER      24
#define MAX_VARS        32
#define MAX_POINTS      16384
#define SS              2               /* samples per pixel per axis */
#define MAX_PW          320
#define MAX_PH          480
#define IDLE_ANIMATION  "KONOKOidle1"

#define VK_LBUTTON 0x01
#define VK_TAB     0x09
#define VK_PRIOR   0x21
#define VK_NEXT    0x22
#define VK_E       0x45
#define VK_Q       0x51
#define VK_RETURN  0x0d
#define VK_ESCAPE  0x1b
#define VK_SPACE   0x20
#define VK_LEFT    0x25
#define VK_UP      0x26
#define VK_RIGHT   0x27
#define VK_DOWN    0x28
#define VK_F2      0x71

#define WHITE   0xffffffffu
#define GREEN   0xff40ff40u
#define YELLOW  0xffffe040u
#define GREY    0xffb0b0b0u

static const char g_default_roster[] =
	"konoko_generic,griffin_generic,muro_generic,barabus,ninja_med_1,striker_med_1,tanker_med_1,"
	"comguy_1,elite_med_1,red_med_1,TCTF_swat_1,TCTF_lite_1,Cop_generic,CopfemA1,thug_wh_1,mad_bomber,"
	"ninjabot,sniper_generic";
static const char *const g_labels[][2] = {
	{ "konoko_generic", "Konoko" }, { "griffin_generic", "Griffin" }, { "muro_generic", "Muro" },
	{ "barabus", "Barabas" }, { "SH_generic", "Shinatama" },
	{ "ninja_med_1", "Ninja" }, { "striker_med_1", "Striker" }, { "tanker_med_1", "Tanker" },
	{ "comguy_1", "Comguy" }, { "elite_med_1", "Elite" }, { "red_med_1", "Red" },
	{ "TCTF_swat_1", "TCTF SWAT" }, { "Cop_generic", "Cop" }, { "thug_wh_1", "Thug" },
	{ "sniper_generic", "Sniper" }, { "security_guard_1", "Security Guard" },
	{ "TCTF_lite_1", "TCTF Trooper" }, { "CopfemA1", "Female Cop" }, { "mad_bomber", "Mad Bomber" },
	{ "ninjabot", "Ninjabot" }, { "Shinzom_generic", "Hasegawa" }, { "bd_1", "Scientist" },
	{ "bdfem_1", "Female Scientist" }, { "generic_male_1", "Civilian" },
	{ "generic_female_1", "Female Civilian" },
};

/* The game's versions of a character: the first name is the one in the roster. A family shares
 * one animation collection (ONCC Animations), so its versions fight with the same moves: those
 * with another collection are families of their own, e.g. mad_bomber (comguy's, not thug's),
 * ninjabot (comguy's with its own groin hit, not ninja's), Shinzom_generic (Hasegawa, not
 * Shinatama's), the TCTF lite troopers and male cops (comguy's), the female cops (Konoko's). */
static const char *const g_families[] = {
	"konoko_generic|k3|k4_A|k4_F|k4_G|k4_K|k4_L",
	"griffin_generic|griffin_swat",
	"muro_generic",
	"SH_generic|SH_dream_lab",
	"Shinzom_generic",
	"ninja_easy_1|ninja_easy_2|ninja_easy_3|ninja_med_1|ninja_med_2|ninja_med_3|ninja_hard_1|ninja_hard_2|ninja_hard_3|super_ninja_1",
	"ninjabot",
	"striker_easy_1|striker_easy_2|striker_easy_3|striker_med_1|striker_med_2|striker_med_3|striker_hard_1|striker_hard_2|striker_hard_3",
	"tanker_easy_1|tanker_med_1|tanker_hard_1",
	"comguy_1|comguy_2|comguy_3",
	"elite_easy_1|elite_easy_2|elite_easy_3|elite_med_1|elite_med_2|elite_med_3|elite_hard_1|elite_hard_2|elite_hard_3",
	"red_easy_1|red_easy_2|red_easy_3|red_med_1|red_med_2|red_med_3|red_hard_1|red_hard_2|red_hard_3",
	"TCTF_swat_1|TCTF_swat_blackops_1",
	"TCTF_lite_1|TCTF_lite_blackops_1",
	"Cop_generic|CopmaleA1|CopmaleA2|CopmaleA3|CopmaleA4",
	"CopfemA1|CopfemA2",
	"thug_wh_1|thug_wh_2|thug_wh_3|thug_wh_4|thug_wh_5|thug_mfg_1|thug_mfg_2|thug_mfg_3|thug_mfg_4|thug_mfg_5|"
	"thug_air_1|thug_air_2|thug_air_3|thug_air_4|thug_air_5|thug_neuro_1|thug_neuro_2|thug_neuro_3|thug_neuro_4|"
	"thug_neuro_5|thug_pow_1|thug_pow_2|thug_pow_3|thug_pow_4|thug_pow_5",
	"mad_bomber",
	"security_guard_1|security_guard_2",
	"bd_1|bd_2|bd_3|bd_4|bd_5",
	"bdfem_1|bdfem_2|bdfem_3|bdfem_4",
	"generic_male_1|generic_male_2|generic_male_3|generic_male_4|generic_male_5|generic_male_6",
	"generic_female_1|generic_female_2|generic_female_3|generic_female_4|generic_female_5|generic_female_6|"
	"generic_female_7|generic_female_8|generic_female_9",
};

typedef struct {
	char cls[64];
	const char *label;
	u8 *oncc;
	u32 number;                 /* among the loaded ONCCs */
	void *pose;                 /* TRAM, 0: Konoko's idle */
	u32 frame;
	char spec[256];             /* the roster's own variation list ("a|b|c"), or "" */
	int nvar, var;              /* variations, and the one shown */
	u8 *voncc[MAX_VARS];
	u32 vnumber[MAX_VARS];
	u8 *vpicture[MAX_VARS];     /* texture, 0: not rendered (or failed) */
	u8 vtried[MAX_VARS];
	int x, y, w, h;             /* the cell, from the last draw */
	int vy;                     /* its "< 2 / 7 >" line */
} Entry;

static Entry g_roster[MAX_ROSTER];
static int g_count, g_resolved, g_any_vars;
static int g_choice = -1;       /* roster index chosen, -1: the map's class */
static int g_choice_var;        /* its variation */
static int g_name_y = -1;       /* the name line, from the last draw */
static u8 g_mute[256];          /* keys that typing used: no press or release until they're up */
static int g_was_editing;
static int g_open, g_sel, g_saved_input;
static u32 g_level;
static int g_pic_w, g_pic_h;    /* the pictures' size */
static u8 *g_panel, *g_cell, *g_hilite, *g_cursor;
static int g_mx = -1, g_my = -1, g_mouse_moved, g_in_window;
static u8 g_down[256];

/* ---- Small math (no libm) ------------------------------------------------------------------ */

static float fsqrt(float x)
{
	float r;
	__asm__("fsqrt" : "=t"(r) : "0"(x));
	return r;
}

static void norm(float *v)
{
	float l = fsqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (l > 1e-12f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static float dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void cross(const float *a, const float *b, float *o)
{
	o[0] = a[1] * b[2] - a[2] * b[1];
	o[1] = a[2] * b[0] - a[0] * b[2];
	o[2] = a[0] * b[1] - a[1] * b[0];
}

static int ifloor(float f)
{
	int i = (int)f;
	return (float)i > f ? i - 1 : i;
}

/* ---- Roster -------------------------------------------------------------------------------- */

static void roster_load(void)
{
	char list[1024];
	const char *s = list;

	GetPrivateProfileStringA("multiplayer", "characters", g_default_roster, list, sizeof list, ini_path());
	g_count = 0;
	while (*s && g_count < MAX_ROSTER) {
		char name[256];
		unsigned n = 0, k = 0;
		s = skip_space(s);
		while (*s && *s != ',' && n < sizeof name - 1) name[n++] = *s++;
		while (n && (name[n - 1] == ' ' || name[n - 1] == '\t')) n--;
		name[n] = 0;
		while (*s && *s != ',') s++;
		if (*s == ',') s++;
		if (!n) continue;
		Entry *e = &g_roster[g_count++];
		memset(e, 0, sizeof *e);
		while (name[k] && name[k] != '|') k++;
		if (name[k]) str_cpy(e->spec, sizeof e->spec, name);
		name[k] = 0;
		str_cpy(e->cls, sizeof e->cls, name);
		e->label = e->cls;
		for (unsigned k = 0; k < sizeof g_labels / sizeof g_labels[0]; k++)
			if (str_ieq_n(g_labels[k][0], name, 64)) e->label = g_labels[k][1];
	}
}

static int biped(u8 *oncc)
{
	u8 *body = ONCC_BODY(oncc) ? ((u8 **)ONCC_BODY(oncc))[4] : 0;
	return body && TRCM_PARTS(body) == PARTS && ONCC_TEXTURES(oncc);
}

/* The class's number among the loaded ONCCs, or 0xffffffff */
static u32 class_number(u8 *oncc)
{
	u32 total = TMrInstance_GetTagCount(TAG_ONCC);
	for (u32 k = 0; k < total; k++) {
		u8 *p = 0;
		if (!TMrInstance_GetDataPtr_ByNumber(TAG_ONCC, k, (void **)&p) && p == oncc) return k;
	}
	return 0xffffffffu;
}

/* A class's name without its number and difficulty: thug_wh_3 -> thug_wh, red_med_1 -> red */
static void family_key(const char *name, char *key, unsigned size)
{
	static const char *const tails[] = { "easy", "med", "hard", "generic" };
	unsigned n = 0;
	while (name[n] && n < size - 1) { key[n] = to_upper(name[n]); n++; }
	key[n] = 0;
	while (n && key[n - 1] >= '0' && key[n - 1] <= '9') n--;
	while (n && key[n - 1] == '_') n--;
	for (unsigned t = 0; t < sizeof tails / sizeof tails[0]; t++) {
		unsigned l = str_len(tails[t]);
		if (n > l && str_ieq_n(key + n - l, tails[t], l)) { n -= l; break; }
	}
	while (n && key[n - 1] == '_') n--;
	key[n] = 0;
}

static void add_variation(Entry *e, u8 *oncc)
{
	u32 number;
	if (!oncc || e->nvar >= MAX_VARS || !biped(oncc)) return;
	for (int v = 0; v < e->nvar; v++)
		if (e->voncc[v] == oncc) return;
	if ((number = class_number(oncc)) == 0xffffffffu) return;
	e->voncc[e->nvar] = oncc;
	e->vnumber[e->nvar++] = number;
}

/* Adds the classes of a "a|b|c" list; 1 if e's own class is in it */
static int add_list(Entry *e, const char *list)
{
	int own = 0;
	while (*list) {
		char name[64];
		unsigned n = 0;
		u8 *oncc = 0;
		while (*list && *list != '|') {
			if (n < sizeof name - 1 && *list != ' ') name[n++] = *list;
			list++;
		}
		if (*list) list++;
		name[n] = 0;
		if (!n) continue;
		own |= str_ieq_n(name, e->cls, 64) && str_len(name) == str_len(e->cls);
		if (!TMrInstance_GetDataPtr(TAG_ONCC, name, &oncc)) add_variation(e, oncc);
	}
	return own;
}

static void find_variations(Entry *e)
{
	e->nvar = 0;
	if (e->spec[0]) {
		add_list(e, e->spec);
	} else {
		unsigned i;
		for (i = 0; i < sizeof g_families / sizeof g_families[0]; i++) {
			/* is e's class in this family? */
			const char *f = g_families[i];
			int found = 0;
			while (*f && !found) {
				unsigned n = 0;
				while (f[n] && f[n] != '|') n++;
				found = n == str_len(e->cls) && str_ieq_n(f, e->cls, n);
				f += n + (f[n] == '|');
			}
			if (found) { add_list(e, g_families[i]); break; }
		}
		if (i == sizeof g_families / sizeof g_families[0]) {
			char key[64], other[64];
			u32 total = TMrInstance_GetTagCount(TAG_ONCC);
			family_key(e->cls, key, sizeof key);
			for (u32 k = 0; k < total; k++) {
				u8 *p = 0;
				const char *name;
				if (TMrInstance_GetDataPtr_ByNumber(TAG_ONCC, k, (void **)&p) || !p || !(name = TMrInstance_GetInstanceName(p))) continue;
				family_key(name, other, sizeof other);
				if (str_len(key) == str_len(other) && str_ieq_n(key, other, 64)) add_variation(e, p);
			}
		}
	}
	/* the roster's own class is always one of them */
	e->var = -1;
	for (int v = 0; v < e->nvar; v++)
		if (e->voncc[v] == e->oncc) e->var = v;
	if (e->var < 0) {
		if (e->nvar == MAX_VARS) e->nvar--;
		for (int v = e->nvar; v > 0; v--) { e->voncc[v] = e->voncc[v - 1]; e->vnumber[v] = e->vnumber[v - 1]; }
		e->voncc[0] = e->oncc;
		e->vnumber[0] = e->number;
		e->nvar++;
		e->var = 0;
	}
	if (e->nvar > 1) g_any_vars = 1;
}

/* The classes, once level0 is loaded: drop the ones that aren't there or aren't bipeds */
static void roster_resolve(void)
{
	int n = 0;

	for (int i = 0; i < g_count; i++) {
		Entry *e = &g_roster[i];
		u8 *oncc = 0;
		if (TMrInstance_GetDataPtr(TAG_ONCC, e->cls, &oncc) || !oncc) {
			log_msg("char menu: no character class %s", e->cls);
			continue;
		}
		if (!biped(oncc)) {
			log_msg("char menu: %s isn't a biped, left out", e->cls);
			continue;
		}
		e->oncc = oncc;
		if ((e->number = class_number(oncc)) == 0xffffffffu) continue;
		find_variations(e);
		if (e->nvar > 1) log_msg("char menu: %s has %d variations", e->cls, e->nvar);
		g_roster[n++] = *e;
	}
	g_count = n;
	g_resolved = 1;
	log_msg("char menu: %d characters in the F2 menu", g_count);
}

/* ---- Textures ------------------------------------------------------------------------------ */

/* An 8x8 texture from Oni (a real instance: a slot and a few bytes of the dynamic file), given
 * w x h texels (rounded up to powers of two) of our own memory. The dynamic file's pixel memory
 * is small and fixed (1 MB) and Oni's own runtime textures come from it: full-size pictures
 * there ran it out ("Unable to allocate the pixels" in debugger.txt), and the game froze later. */
static u8 *texture_new(int w, int h, const char *name)
{
	u8 *t = 0, *texels;
	int tw = 8, th = 8;

	while (tw < w) tw <<= 1;
	while (th < h) th <<= 1;
	if (M3rTextureMap_New(8, 8, PIXEL_ARGB1555, 3, 0, name, &t) || !t || !TXMP_TEXELS(t)) {
		log_msg("char menu: can't make texture %s", name);
		return 0;
	}
	if (tw > 8 || th > 8) {
		texels = VirtualAlloc(0, (u32)(tw * th * 2), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!texels) {
			log_msg("char menu: no memory for texture %s", name);
			return 0;
		}
		TXMP_W(t) = (u16)tw;
		TXMP_H(t) = (u16)th;
		TXMP_TEXELS(t) = texels;
	}
	return t;
}

static u8 *solid(u16 color, const char *name)
{
	u8 *t = texture_new(8, 8, name);
	if (t) {
		u16 *px = (u16 *)TXMP_TEXELS(t);
		for (int i = 0; i < TXMP_W(t) * TXMP_H(t); i++) px[i] = color;
	}
	return t;
}

/* A texel as 0xRRGGBB; u, v wrap */
static u32 texel(u8 *t, float u, float v)
{
	u32 w = TXMP_W(t), h = TXMP_H(t);
	const u8 *d = TXMP_TEXELS(t);
	int x = ifloor(u * (float)w) % (int)w, y = ifloor(v * (float)h) % (int)h;
	if (x < 0) x += w;
	if (y < 0) y += h;
	switch (TXMP_TYPE(t)) {
	case 0: {
		u32 p = ((const u16 *)d)[y * w + x];
		return ((p >> 8 & 15) * 17) << 16 | ((p >> 4 & 15) * 17) << 8 | (p & 15) * 17;
	}
	case 1: case 2: {
		u32 p = ((const u16 *)d)[y * w + x];
		return ((p >> 10 & 31) * 255 / 31) << 16 | ((p >> 5 & 31) * 255 / 31) << 8 | (p & 31) * 255 / 31;
	}
	case 7: case 8:
		return ((const u32 *)d)[y * w + x] & 0xffffff;
	case 9: {
		const u8 *b = d + ((y >> 2) * ((w + 3) >> 2) + (x >> 2)) * 8;
		u32 c[2] = { (u32)(b[0] | b[1] << 8), (u32)(b[2] | b[3] << 8) };
		u32 bits = b[4] | b[5] << 8 | b[6] << 16 | (u32)b[7] << 24;
		u32 i = bits >> (2 * ((y & 3) * 4 + (x & 3))) & 3;
		int rgb[2][3];
		for (int k = 0; k < 2; k++) {
			rgb[k][0] = (c[k] >> 11 & 31) * 255 / 31;
			rgb[k][1] = (c[k] >> 5 & 63) * 255 / 63;
			rgb[k][2] = (c[k] & 31) * 255 / 31;
		}
		int o[3];
		for (int k = 0; k < 3; k++) {
			if (i < 2) o[k] = rgb[i][k];
			else if (c[0] > c[1]) o[k] = i == 2 ? (2 * rgb[0][k] + rgb[1][k]) / 3 : (rgb[0][k] + 2 * rgb[1][k]) / 3;
			else o[k] = i == 2 ? (rgb[0][k] + rgb[1][k]) / 2 : 0;
		}
		return (u32)o[0] << 16 | (u32)o[1] << 8 | (u32)o[2];
	}
	}
	return 0x808080;
}

/* ---- Pictures ------------------------------------------------------------------------------ */

static void *g_idle;
static float g_mats[PARTS][12];
static float g_world[MAX_POINTS][3];
static float g_depth[MAX_PW * SS * MAX_PH * SS];
static u32 g_color[MAX_PW * SS * MAX_PH * SS];   /* 0: empty, else 0x01RRGGBB */

static void xform(const float *m, const float *p, float *o)
{
	o[0] = m[0] * p[0] + m[3] * p[1] + m[6] * p[2] + m[9];
	o[1] = m[1] * p[0] + m[4] * p[1] + m[7] * p[2] + m[10];
	o[2] = m[2] * p[0] + m[5] * p[1] + m[8] * p[2] + m[11];
}

typedef struct { u32 n; float (*p)[3]; float (*uv)[2]; u32 ni; const u32 *idx; u8 *tex; } Part;

static int part_get(u8 *oncc, u8 *body, int i, Part *o)
{
	u8 **geometry = TRCM_GEOMETRY(body), *g = geometry ? geometry[i] : 0, *pnta, *txca, *idxa, *t;
	memset(o, 0, sizeof *o);
	if (!g ||!(pnta = *(u8 **)(g + 4)) || !(txca = *(u8 **)(g + 0x10)) || !(idxa = *(u8 **)(g + 0x14))) return 0;
	o->n = *(u32 *)(pnta + 0x34);
	o->p = (float (*)[3])(pnta + 0x38);
	o->uv = (float (*)[2])(txca + 0x18);
	if (*(u32 *)(txca + 0x14) < o->n) o->n = *(u32 *)(txca + 0x14);
	o->ni = *(u32 *)(idxa + 0x14);
	o->idx = (const u32 *)(idxa + 0x18);
	t = ((u8 **)(ONCC_TEXTURES(oncc) + 0x18))[i];
	if (t && (TXMP_FLAGS(t) & TXMP_TEXELS_LOADED) && TXMP_TEXELS(t) && TXMP_W(t) && TXMP_H(t) &&
	    (TXMP_TYPE(t) <= 2 || TXMP_TYPE(t) == 7 || TXMP_TYPE(t) == 8 || TXMP_TYPE(t) == 9))
		o->tex = t;
	return 1;
}

typedef struct { float right[3], up[3], view[3], light[3], cx, cy, scale; int w, h; } View;

static void raster(const View *v, const float *a, const float *b, const float *c,
                   const float *ta, const float *tb, const float *tc, u8 *tex)
{
	float s[3][3];
	const float *p[3] = { a, b, c };
	for (int k = 0; k < 3; k++) {
		s[k][0] = (dot(p[k], v->right) - v->cx) * v->scale + v->w * 0.5f;
		s[k][1] = v->h * 0.5f - (dot(p[k], v->up) - v->cy) * v->scale;
		s[k][2] = dot(p[k], v->view);
	}
	float area = (s[1][0] - s[0][0]) * (s[2][1] - s[0][1]) - (s[2][0] - s[0][0]) * (s[1][1] - s[0][1]);
	if (area > -1e-6f && area < 1e-6f) return;
	float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] }, n[3];
	cross(e1, e2, n);
	norm(n);
	if (dot(n, v->view) > 0) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }   /* two-sided */
	float l = dot(n, v->light), shade = 0.62f + 0.5f * (l > 0 ? l : 0);
	int x0 = ifloor(s[0][0] < s[1][0] ? (s[0][0] < s[2][0] ? s[0][0] : s[2][0]) : (s[1][0] < s[2][0] ? s[1][0] : s[2][0]));
	int x1 = ifloor(s[0][0] > s[1][0] ? (s[0][0] > s[2][0] ? s[0][0] : s[2][0]) : (s[1][0] > s[2][0] ? s[1][0] : s[2][0])) + 1;
	int y0 = ifloor(s[0][1] < s[1][1] ? (s[0][1] < s[2][1] ? s[0][1] : s[2][1]) : (s[1][1] < s[2][1] ? s[1][1] : s[2][1]));
	int y1 = ifloor(s[0][1] > s[1][1] ? (s[0][1] > s[2][1] ? s[0][1] : s[2][1]) : (s[1][1] > s[2][1] ? s[1][1] : s[2][1])) + 1;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > v->w - 1) x1 = v->w - 1;
	if (y1 > v->h - 1) y1 = v->h - 1;
	for (int y = y0; y <= y1; y++) {
		float py = y + 0.5f;
		for (int x = x0; x <= x1; x++) {
			float px = x + 0.5f;
			float w0 = ((s[1][0] - px) * (s[2][1] - py) - (s[2][0] - px) * (s[1][1] - py)) / area;
			float w1 = ((s[2][0] - px) * (s[0][1] - py) - (s[0][0] - px) * (s[2][1] - py)) / area;
			float w2 = 1 - w0 - w1;
			if (w0 < 0 || w1 < 0 || w2 < 0) continue;
			float z = w0 * s[0][2] + w1 * s[1][2] + w2 * s[2][2];
			int i = y * v->w + x;
			if (g_color[i] && z >= g_depth[i]) continue;
			u32 rgb = tex ? texel(tex, w0 * ta[0] + w1 * tb[0] + w2 * tc[0], w0 * ta[1] + w1 * tb[1] + w2 * tc[1]) : 0xa0a0a0;
			u32 r = (u32)((rgb >> 16 & 255) * shade), g = (u32)((rgb >> 8 & 255) * shade), bl = (u32)((rgb & 255) * shade);
			if (r > 255) r = 255;
			if (g > 255) g = 255;
			if (bl > 255) bl = 255;
			g_depth[i] = z;
			g_color[i] = 0x01000000u | r << 16 | g << 8 | bl;
		}
	}
}

/* The picture of e's variation v, w x h pixels, in e's pose; 0 if the class can't be drawn */
static int render(Entry *e, int var, u16 *out, int pitch, int w, int h)
{
	u8 *oncc = e->voncc[var], *body = ((u8 **)ONCC_BODY(oncc))[4];
	float quats[PARTS][4], root[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
	Part parts[PARTS];
	u32 first[PARTS + 1], total = 0;
	View v;

	TRrAnimation_GetRotations(e->pose ? e->pose : g_idle, e->pose ? e->frame : 0, &quats[0][0]);
	TRrBody_BuildMatrices(body, 0, &quats[0][0], root, 0xffffffffu, &g_mats[0][0]);
	for (int i = 0; i < PARTS; i++) {
		first[i] = total;
		if (!part_get(oncc, body, i, &parts[i]) || total + parts[i].n > MAX_POINTS) {
			parts[i].n = parts[i].ni = 0;
			continue;
		}
		for (u32 k = 0; k < parts[i].n; k++) xform(g_mats[i], parts[i].p[k], g_world[total + k]);
		total += parts[i].n;
	}
	first[PARTS] = total;
	if (!total) return 0;

	/* the front: where the toes point from the ankles, level */
	float fwd[3] = { 0, 0, 0 }, up[3] = { 0, 1, 0 };
	for (int f = 0; f < 2; f++) {
		int i = f ? FOOT_R : FOOT_L;
		float c[3] = { 0, 0, 0 };
		for (u32 k = first[i]; k < first[i + 1]; k++) { c[0] += g_world[k][0]; c[2] += g_world[k][2]; }
		if (first[i + 1] > first[i]) {
			float n = (float)(first[i + 1] - first[i]);
			fwd[0] += c[0] / n - g_mats[i][9];
			fwd[2] += c[2] / n - g_mats[i][11];
		}
	}
	if (fwd[0] * fwd[0] + fwd[2] * fwd[2] < 1e-6f) { fwd[0] = 0; fwd[2] = 1; }
	norm(fwd);
	/* camera 35 degrees to the side and 22 degrees above the front */
	float side[3], dir[3], *cam_right = v.right;
	cross(up, fwd, side);
	for (int k = 0; k < 3; k++)
		dir[k] = (fwd[k] * 0.81915f + side[k] * 0.57358f) * 0.92718f + up[k] * 0.37461f;
	for (int k = 0; k < 3; k++) v.view[k] = -dir[k];
	cross(v.view, up, cam_right);
	norm(cam_right);
	cross(cam_right, v.view, v.up);
	for (int k = 0; k < 3; k++) v.light[k] = -0.35f * v.right[k] + 0.8f * v.up[k] + 0.5f * dir[k];
	norm(v.light);

	float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
	for (u32 k = 0; k < total; k++) {
		float q[2] = { dot(g_world[k], v.right), dot(g_world[k], v.up) };
		for (int j = 0; j < 2; j++) {
			if (q[j] < lo[j]) lo[j] = q[j];
			if (q[j] > hi[j]) hi[j] = q[j];
		}
	}
	v.w = w * SS;
	v.h = h * SS;
	float sx = (hi[0] - lo[0]) / v.w, sy = (hi[1] - lo[1]) / v.h, ext = sx > sy ? sx : sy;
	if (ext <= 0) return 0;
	v.scale = 0.92f / ext;
	v.cx = (lo[0] + hi[0]) * 0.5f;
	v.cy = (lo[1] + hi[1]) * 0.5f;

	memset(g_color, 0, v.w * v.h * sizeof g_color[0]);
	for (int i = 0; i < PARTS; i++) {
		const Part *pt = &parts[i];
		u32 a = 0, b = 0, run = 0;
		for (u32 j = 0; j < pt->ni; j++) {
			u32 c = pt->idx[j];
			if (c & 0x80000000u) run = 0;
			c &= 0x7fffffffu;
			if (c >= pt->n) { run = 0; continue; }
			if (run >= 2)
				raster(&v, g_world[first[i] + a], g_world[first[i] + b], g_world[first[i] + c],
				       pt->uv[a], pt->uv[b], pt->uv[c], pt->tex);
			a = b;
			b = c;
			run++;
		}
	}

	/* down to the texture: a pixel is solid when half its samples are */
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			u32 r = 0, g = 0, b = 0, n = 0;
			for (int dy = 0; dy < SS; dy++)
				for (int dx = 0; dx < SS; dx++) {
					u32 c = g_color[(y * SS + dy) * v.w + x * SS + dx];
					if (!c) continue;
					r += c >> 16 & 255; g += c >> 8 & 255; b += c & 255; n++;
				}
			out[y * pitch + x] = n * 2 < SS * SS ? 0 :
				(u16)(0x8000 | (r / n >> 3) << 10 | (g / n >> 3) << 5 | (b / n >> 3));
		}
	return 1;
}

/* The class's own stance or taunt (see the top), one no earlier class in the roster took */
static void pick_pose(Entry *e, int index)
{
	static const char *const wanted[] = { "COMtaunt1", "COMtaunt2", "COMidle1", "COMidle2", "COMidle3", "COMidle4" };
	void *found[16];
	int n = 0;

	e->pose = 0;
	e->frame = 0;
	for (u8 *trac = ONCC_ANIMATIONS(e->oncc); trac && n < 16; trac = TRAC_PARENT(trac))
		for (u32 w = 0; w < sizeof wanted / sizeof wanted[0]; w++)
			for (u32 i = 0; i < TRAC_COUNT(trac) && n < 16; i++) {
				void *a = TRAC_ANIMATION(trac, i);
				const char *name = a ? TMrInstance_GetInstanceName(a) : 0;
				if (name && str_len(name) > 3 && str_ieq_n(name + 3, wanted[w], 64) &&
				    TRAM_BONES(a) == PARTS && TRAM_FRAMES(a)) {
					found[n++] = a;
					break;
				}
			}
	for (int k = 0; k < n && !e->pose; k++) {
		int taken = 0;
		for (int j = 0; j < index; j++) taken |= g_roster[j].pose == found[k];
		if (!taken) e->pose = found[k];
	}
	if (!e->pose && n) e->pose = found[0];
	if (!e->pose) return;
	const char *name = TMrInstance_GetInstanceName(e->pose);
	if (str_contains_i(name, "taunt")) e->frame = TRAM_FRAMES(e->pose) * 55 / 100;
	log_msg("char menu: %s posed as %s, frame %u of %u", e->cls, name, e->frame, TRAM_FRAMES(e->pose));
}

/* New picture size: every picture is rendered again when it's next shown */
static void pictures_reset(int w, int h)
{
	if (!g_idle && (TMrInstance_GetDataPtr(TAG_TRAM, IDLE_ANIMATION, &g_idle) || !g_idle || TRAM_BONES(g_idle) != PARTS)) {
		log_msg("char menu: no %s animation, no pictures", IDLE_ANIMATION);
		g_idle = 0;
	}
	if (w > MAX_PW) w = MAX_PW;
	if (h > MAX_PH) h = MAX_PH;
	g_pic_w = w;
	g_pic_h = h;
	for (int i = 0; i < g_count; i++) {
		Entry *e = &g_roster[i];
		memset(e->vpicture, 0, sizeof e->vpicture);
		memset(e->vtried, 0, sizeof e->vtried);
		if (g_idle) pick_pose(e, i);
	}
}

/* The picture of e's shown variation, rendered the first time */
static u8 *picture(Entry *e)
{
	static int serial;
	int v = e->var;
	char name[64];

	if (e->vtried[v] || !g_idle) return e->vpicture[v];
	e->vtried[v] = 1;
	fmt(name, "arklite_char_%d", serial++);
	u8 *t = texture_new(g_pic_w, g_pic_h, name);
	if (!t) return 0;
	u16 *px = (u16 *)TXMP_TEXELS(t);
	memset(px, 0, TXMP_W(t) * TXMP_H(t) * 2);
	if (render(e, v, px, TXMP_W(t), g_pic_w, g_pic_h)) e->vpicture[v] = t;
	return e->vpicture[v];
}

/* ---- The menu ------------------------------------------------------------------------------ */

int char_menu_open(void) { return g_open; }

u32 char_menu_record_word(void)
{
	return PLAYERS_CHOICE_TAG | (g_choice >= 0 ? g_roster[g_choice].vnumber[g_choice_var] + 1 : 0);
}

void char_menu_tag(u32 count, void *actions)
{
	u32 *rec = actions, word = char_menu_record_word();
	if (!rec) return;
	for (u32 i = 0; i < (count & 0xffff); i++) rec[i * 12 + 11] = word;
}

static int allowed(void)
{
	return ONgGameState && ONrLevel_GetCurrent() && multiplayer_active() && multiplayer_map() &&
	       !live_menu_open() && !chat_blocking();
}

static void close_menu(void)
{
	if (!g_open) return;
	mp_name_edit_end(0);
	g_open = 0;
	LIrMode_SetGameInput(g_saved_input);
}

static void open_menu(void)
{
	if (!g_resolved) roster_resolve();
	if (!g_count) return;
	g_open = 1;
	g_level = ONrLevel_GetCurrent();
	g_sel = g_choice >= 0 ? g_choice : 0;
	if (g_choice >= 0) g_roster[g_choice].var = g_choice_var;
	g_saved_input = LIrMode_GetGameInput();
	LIrMode_SetGameInput(0);
	g_mouse_moved = 0;
}

static int key(int vk, int focus, int *released)
{
	int down = focus && (GetAsyncKeyState(vk) & 0x8000) != 0, was = g_down[vk];
	g_down[vk] = (u8)down;
	if (g_mute[vk]) {   /* typing used it */
		if (!down) g_mute[vk] = 0;
		if (released) *released = 0;
		return 0;
	}
	if (released) *released = was && !down;
	return down && !was;
}

static void cycle(int i, int step)
{
	Entry *e;
	if (i < 0 || i >= g_count || (e = &g_roster[i])->nvar < 2) return;
	e->var = (e->var + step + e->nvar) % e->nvar;
}

static void choose(int i)
{
	if (i < 0 || i >= g_count) return;
	g_choice = i;
	g_choice_var = g_roster[i].var;
	log_msg("char menu: chose %s", TMrInstance_GetInstanceName(g_roster[i].voncc[g_choice_var]));
}

/* Every frame, after the update (live_menu.c's main loop hook) */
void char_menu_frame(void)
{
	u32 pid = 0;
	void *w = GetForegroundWindow();
	int focus, esc_up, f2, left, right, up, down, enter, space, click, tab, prev, next;

	if (g_was_editing && !mp_name_editing()) g_mute[VK_RETURN] = g_mute[VK_ESCAPE] = 1;   /* they ended the typing */
	g_was_editing = mp_name_editing();
	if (w) GetWindowThreadProcessId(w, &pid);
	focus = w && pid == GetCurrentProcessId();
	f2 = key(VK_F2, focus, 0);
	tab = key(VK_TAB, focus, 0);
	prev = key(VK_Q, focus, 0) | key(VK_PRIOR, focus, 0);
	next = key(VK_E, focus, 0) | key(VK_NEXT, focus, 0);
	key(VK_ESCAPE, focus, &esc_up);
	left = key(VK_LEFT, focus, 0);
	right = key(VK_RIGHT, focus, 0);
	up = key(VK_UP, focus, 0);
	down = key(VK_DOWN, focus, 0);
	enter = key(VK_RETURN, focus, 0);
	space = key(VK_SPACE, focus, 0);
	click = key(VK_LBUTTON, focus, 0);

	if (!g_open) {
		if (f2 && allowed()) open_menu();
		return;
	}
	if (!allowed() || ONrLevel_GetCurrent() != g_level) {
		close_menu();
		return;
	}

	/* the mouse, in screen pixels */
	g_in_window = 0;
	if (focus) {
		POINT pt;
		RECT rc;
		if (GetCursorPos(&pt) && ScreenToClient(w, &pt) && GetClientRect(w, &rc) && rc.right > 0 && rc.bottom > 0) {
			int mx = pt.x * M3rScreen_Width() / rc.right, my = pt.y * M3rScreen_Height() / rc.bottom;
			g_in_window = pt.x >= 0 && pt.y >= 0 && pt.x < rc.right && pt.y < rc.bottom;
			if (mx != g_mx || my != g_my) g_mouse_moved = 1;
			g_mx = mx;
			g_my = my;
		}
	}
	int hover = -1, lh = mp_hud_line_height();
	int on_name = g_in_window && g_name_y >= 0 && g_my >= g_name_y && g_my < g_name_y + lh;
	if (g_in_window)
		for (int i = 0; i < g_count; i++) {
			const Entry *e = &g_roster[i];
			if (g_mx >= e->x && g_mx < e->x + e->w && g_my >= e->y && g_my < e->y + e->h) hover = i;
		}

	/* typing the name (mp_names.c reads the letters): a click elsewhere keeps it */
	if (mp_name_editing()) {
		if (click && !on_name) mp_name_edit_end(1);
		g_mouse_moved = 0;
		return;
	}
	if (tab || (click && on_name)) {
		mp_name_edit_begin();
		g_was_editing = mp_name_editing();
		return;
	}

	if (hover >= 0 && g_mouse_moved) g_sel = hover;
	g_mouse_moved = 0;
	if (prev) cycle(g_sel, -1);
	if (next) cycle(g_sel, 1);
	if (click && hover >= 0 && g_roster[hover].nvar > 1 && g_my >= g_roster[hover].vy) {   /* "< 2 / 7 >" */
		const Entry *e = &g_roster[hover];
		cycle(hover, g_mx < e->x + e->w / 2 ? -1 : 1);
		return;
	}

	int cols = g_count, rows = 1;
	while (cols > 8) { rows++; cols = (g_count + rows - 1) / rows; }
	if (left && g_sel > 0) g_sel--;
	if (right && g_sel < g_count - 1) g_sel++;
	if (up && g_sel >= cols) g_sel -= cols;
	if (down && g_sel + cols < g_count) g_sel += cols;

	if (click && hover >= 0) { choose(hover); close_menu(); }
	else if (enter || space) { choose(g_sel); close_menu(); }
	else if (f2 || esc_up) close_menu();
}

static void rect(u8 *t, int x, int y, int w, int h, u32 alpha)
{
	float at[4] = { (float)x, (float)y, 0.5f, 2.0f };
	if (t && w > 0 && h > 0) M3rDraw_Bitmap(t, at, (u32)w, (u32)h, 0xffffffffu, alpha);
}

/* For chat.c and mp_names.c (one color only: M3rDraw_Bitmap repeats a texture past its size) */
u8 *hud_solid(u16 argb1555, const char *name) { return solid(argb1555, name); }
void hud_rect(u8 *texture, int x, int y, int w, int h, u32 alpha) { rect(texture, x, y, w, h, alpha); }

/* "Striker" or "Striker 2/10": the label of entry i with variation v */
static void label(char *out, int i, int v)
{
	const Entry *e = &g_roster[i];
	if (e->nvar > 1) fmt(out, "%s %d/%d", e->label, v + 1, e->nvar);
	else fmt(out, "%s", e->label);
}

/* In the HUD (mp_hud.c), on top of everything else */
void char_menu_draw(int width, int height)
{
	char line[128];
	int lh = mp_hud_line_height();

	if (!g_open) return;
	if (!g_panel) {
		g_panel = solid(0x8000, "arklite_char_panel");          /* black */
		g_cell = solid(0x8000 | 6 << 10 | 6 << 5 | 7, "arklite_char_cell");
		g_hilite = solid(0x8000 | 31 << 10 | 26 << 5 | 6, "arklite_char_hilite");
		g_cursor = solid(0xffff, "arklite_char_cursor");
	}

	/* layout: up to 8 columns, cells as tall as fits */
	int cols = g_count, rows = 1;
	while (cols > 8) { rows++; cols = (g_count + rows - 1) / rows; }
	int gap = height / 90 + 2, lines = g_any_vars ? 2 : 1;
	int top = height / 8 + 4 * lh, bottom = height - height / 10 - 2 * lh;
	int ch = (bottom - top - (rows - 1) * gap) / rows, cw = ch * 5 / 8;
	if (cols * cw + (cols - 1) * gap > width * 9 / 10) {
		cw = (width * 9 / 10 - (cols - 1) * gap) / cols;
		ch = cw * 8 / 5;
	}
	int pw = cw - 2 * gap, ph = ch - lines * lh - (2 + lines) * gap;
	if (pw < 8 || ph < 8) return;
	if (pw > MAX_PW) pw = MAX_PW;
	if (ph > MAX_PH) ph = MAX_PH;
	if (pw != g_pic_w || ph != g_pic_h) pictures_reset(pw, ph);
	int grid_w = cols * cw + (cols - 1) * gap, x0 = (width - grid_w) / 2;
	int y0 = top + (bottom - top - (rows * ch + (rows - 1) * gap)) / 2;

	rect(g_panel, 0, 0, width, height, 170);
	mp_hud_text("Choose your character", width / 2, top - 4 * lh, MP_CENTER, WHITE);

	/* the name: Tab or a click types a new one */
	g_name_y = top - 3 * lh + gap / 2;
	if (mp_name_editing()) {
		fmt(line, "Name: %s%s", mp_name_edit_text(), GetTickCount() / 400 & 1 ? "_" : " ");
		mp_hud_text(line, width / 2, g_name_y, MP_CENTER, YELLOW);
	} else {
		fmt(line, "Name: %s   (Tab or click to change)", mp_name_local());
		mp_hud_text(line, width / 2, g_name_y, MP_CENTER, WHITE);
	}

	NetHud h;
	net_hud(&h);
	int now = players_can_change(h.state == NETHUD_OFF ? 0 : h.local_player);
	char chosen[96];
	if (g_choice >= 0) label(chosen, g_choice, g_choice_var);
	else str_cpy(chosen, sizeof chosen, "the map's character");
	fmt(line, now ? "You change at once (%s now)" : "Next respawn: %s", chosen);
	mp_hud_text(line, width / 2, top - 2 * lh + gap, MP_CENTER, GREEN);

	for (int i = 0; i < g_count; i++) {
		Entry *e = &g_roster[i];
		e->x = x0 + (i % cols) * (cw + gap);
		e->y = y0 + (i / cols) * (ch + gap);
		e->w = cw;
		e->h = ch;
		e->vy = e->y + ch - lh - gap;
		rect(i == g_sel ? g_hilite : g_cell, e->x, e->y, cw, ch, i == g_sel ? 150 : 200);
		u8 *pic = picture(e);
		if (pic) rect(pic, e->x + (cw - g_pic_w) / 2, e->y + gap, g_pic_w, g_pic_h, 255);
		int picked = i == g_choice && e->var == g_choice_var;
		u32 color = picked ? GREEN : i == g_sel ? YELLOW : WHITE;
		mp_hud_text(e->label, e->x + cw / 2, e->y + ch - lines * (lh + gap), MP_CENTER, color);
		if (e->nvar > 1) {
			fmt(line, "<  %d / %d  >", e->var + 1, e->nvar);
			mp_hud_text(line, e->x + cw / 2, e->vy, MP_CENTER, i == g_sel ? color : GREY);
		}
	}
	mp_hud_text(mp_name_editing() ? "Type your name   Enter: keep   Esc: cancel" :
	            g_any_vars ? "Arrows/mouse: pick   Q/E or < >: variation   Enter/click: choose   F2/Esc: close" :
	            "Arrows or mouse: pick   Enter or click: choose   F2 or Esc: close",
	            width / 2, bottom + lh, MP_CENTER, GREY);
	if (g_in_window) {
		rect(g_panel, g_mx - 1, g_my - 1, 12, 12, 255);
		rect(g_cursor, g_mx, g_my, 10, 10, 255);
	}
}

void char_menu_install(void)
{
	roster_load();
	log_msg("char menu: F2 changes the character in multiplayer games (%d in the roster)", g_count);
}
