/* Character choice in multiplayer games (F2): a menu over the running game, like the F1 help
 * screen (nothing pauses), with a portrait of every character, the game's and the mods' (Hanako,
 * Naomi, Janice, BGI Sarai, RAYMAN...). The character picked is the one this PC's player changes
 * into at once in the warm-up, or comes back as at the next respawn in a match (or if dead).
 *
 * Only on a multiplayer map in a multiplayer game (players.c), in a level, while the pause menu
 * isn't open. While the menu is up this PC's player gets no game input (LIrMode_SetGameInput
 * 0x403f70, as the help screen does) and utility input is skipped (live_menu.c), so Esc closes
 * the menu instead of opening the pause menu. Keys are read with GetAsyncKeyState only while this
 * Oni window has the focus (two Onis may share a PC, as for F3 in net.c).
 *
 * Layout: the title, the Name field and "Playing as:" on top; under them a grid of portraits
 * (head and shoulders) on the left and the picked character's full-body picture on the right,
 * with its name, its variation ("3 / 9") and class. The portraits are as big as lets the whole
 * grid fit; with no room even at the smallest size the grid scrolls (a scroll bar on its right).
 * When there are mod characters the grid has two sections, "Oni" and "Mods".
 *
 * Input: the arrows or the mouse pick (the picked one follows the cursor over the grid), Enter,
 * Space, a click on a portrait or on the big picture chooses and closes, F2 or Esc closes. Q/E,
 * Page Up/Down, the "<" ">" on the picked portrait or the wheel over the big picture change its
 * variation; the wheel elsewhere scrolls the grid, or changes the variation when nothing scrolls
 * (menu_wheel.c hands the menu the notches). Tab or a click on the Name field types the name.
 *
 * The choice reaches the game through the action records, so every PC and every replay sees it
 * at the same tick: word 11 of each record (analogs[9], which the tick, 0x4fa6a0, never reads)
 * carries PLAYERS_CHOICE_TAG | (class instance number + 1), or the tag alone for "the map's
 * class". lockstep.c's update hook writes it into this PC's records of every frame (and net.c
 * into each input it makes); players.c reads it in the tick from Konoko's record (the tick's
 * action) and player 2's, and swaps the class of the player's placement for the respawn. The
 * number is the class's TMrInstance_GetDataPtr_ByNumber index among the loaded ONCCs, the same
 * on every PC with the same game files (net.c compares level0's hash). After level0 changes
 * (level0_swap.c) the choice is found again by its class name.
 *
 * Roster: every loaded character with the 19-part biped body. g_families groups the game's
 * classes into characters (and names them), then the mods Arklite knows; any other loaded class
 * is a mod's character of its own, with the classes sharing its name apart from the number and
 * difficulty (family_key), or its first word and animation collection (hanako_red, hanako_suit),
 * as its variations. Arklite.ini [multiplayer] characters = class names separated by commas
 * (level0 ONCC names, e.g. konoko_generic) gives a list of its own instead, at most 96; an entry
 * can list its own variations: konoko_generic|k3|k4_A.
 *
 * Name: under the title, a text field like the Host dialog's (name_field) with this PC's player's
 * name (mp_names.c); Tab or a click on it starts typing, Enter keeps the new name (saved in
 * Arklite.ini), Esc puts the old one back.
 *
 * The line under it says what a choice does now: "Playing as: X" where it changes a living player
 * at once (the warm-up, a game alone), "Next respawn: X" in a match or while dead (players.c).
 *
 * Pictures: rendered in software from the class's own data in memory, then kept as textures. A
 * portrait is rendered the first time it's shown (a few a frame, the picked one first), the big
 * picture when its character is picked (the last PREVIEWS are kept):
 *   - pose: the big picture shows each class's own fighting stance or taunt, from its animation
 *     collection (ONCC +0xc80 the TRAC: +0x10 the parent collection, +0x16 the entry count,
 *     entries of 12 bytes from +0x18 with the TRAM at +8, as 0x42c1b0 walks them), the class's
 *     own entries before its parents'. Wanted, in order: <xxx>COMtaunt1, taunt2, then COMidle1-4
 *     (the stance); each class takes the first one no class before it in the roster took, so
 *     characters that share moves (Griffin and Comguy) still stand differently. A taunt is shown
 *     at 55% of its frames (TRAM +0x164 the frame count), a stance at frame 0. The portrait
 *     always shows the stance. Without any, Konoko's idle (KONOKOidle1; every biped shares the
 *     19 bones). The rotations come from 0x427810 (ECX animation, EDX frame, [quaternions out],
 *     ret 4) and the bone matrices from 0x4274c0 (ECX body, EDX attachments or 0, [quaternions],
 *     [root matrix], [bone mask], [matrices out], ret 0x10), what the game's own character
 *     update uses (call at 0x4e5bdf). Matrices are 4x3, column-major: p' = m0*x + m3*y + m6*z + m9.
 *   - body: ONCC +0xc34 the body set (5 TRCM, +0x10 the most detailed), TRCM +4 the part count,
 *     +0x48 the parts' M3GM; ONCC +0xc38 the TRMA, its textures from +0x18, one per part (what
 *     0x427da0 gives the parts before drawing). M3GM +0x04 PNTA (+0x34 count, +0x38 xyz), +0x10
 *     TXCA (+0x18 uv), +0x14 IDXA (+0x14 count, +0x18 triangle strips, bit 31 starts a strip).
 *     Part 10 is the head (ONcHead_Index, Oni_Character.h): a portrait is framed on it.
 *   - textures: TXMP +0x80 flags (0x2000: +0x94 is a pointer to the texels), +0x84/+0x86 size,
 *     +0x88 pixel type: 0 ARGB4444, 1 RGB555, 2 ARGB1555, 7 RGBA8888 (bytes R G B A), 8 BGR888
 *     (bytes B G R x), 9 DXT1 (OniSplit's TextureFormat names; from the
 *     table at 0x529e78); level 0 of the mipmaps comes first. Alpha is left out (on characters it
 *     is mostly the reflection mask).
 *   - view: orthographic, from the front-left and above (Catwalk's previews), fitted to the
 *     picture; a portrait from nearer the front. Oni is Y up; the front is where the toes point.
 * Pictures and the menu's plain rectangles are ARGB1555 textures made with M3rTextureMap_New
 * (0x41eb00: CX, DX the size, [pixel type], [where: bit 0 makes a new instance, bit 1 puts it in
 * the file a level load empties (0x5711a0, 1 MB), without it in the one kept until the game exits
 * (0x5711a4, 2 MB), both 256 instances], [flags], [name], [texture out]; it rounds the size up to
 * powers of two) at 8x8, then pointed at texels Arklite allocates (see texture_new),
 * drawn with M3rDraw_Bitmap (0x41f4a0: ECX texture, EDX top-left {x, y, z, 1/w}, [width],
 * [height], [shade], [alpha]; texels 1:1). ARGB1555 because the GL upload converts it (the
 * converter table at 0x535a90 has no ARGB8888 to RGBA bytes). Their number is fixed (a portrait
 * texture per character, PREVIEWS big ones), since the instances run out at 256. */
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
#define WMrCursor_Get           ((u8 *(ONI_FC *)(u32))0x4687d0)          /* ECX type: {type, PSpc} */
#define PSrPartSpec_Draw        ((void (ONI_FC *)(void *, u32, const float *, u32, u32, u32))0x46a610)   /* ECX partspec, EDX parts, [at], [w], [h], [alpha] */
#define WM_CURSOR_ARROW         1
#define PS_MIDDLE_MIDDLE        0x10    /* alone: the middle part stretched over the whole size */
#define PS_ALL                  0xffff  /* the nine parts: corners, edges, middle */
#define TAG_PSpc                0x50537063

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
#define TXMP_DIRTY(t)           (*(u8 *)((t) + 0xa0))      /* uploaded again when set (0x41a460); +0x9c the GL name */
#define TXMP_TEXELS_LOADED      0x2000
#define PIXEL_ARGB1555          2

#define PARTS           19
#define FOOT_L          3
#define FOOT_R          6
#define HEAD            10              /* ONcHead_Index */
#define MAX_ROSTER      96
#define MAX_VARS        32
#define MAX_CLASSES     2048            /* loaded ONCCs a mod's character can be found among */
#define MAX_POINTS      16384
#define SS              2               /* samples per pixel per axis */
#define MAX_PW          320
#define MAX_PH          560
#define MAX_TILE        256             /* a portrait's side */
#define PREVIEWS        6               /* big pictures kept */
#define TILE_BUDGET     4               /* portraits rendered a frame, the picked one apart */
#define MAX_LINES       (MAX_ROSTER * 2)
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
#define ORANGE  0xffff7f00u     /* the menus' titles and labels (255 127 0 in the stock WMDDs) */

/* The characters: a label, the class shown first, and every class that is a version of it (the
 * variations). The game's first, in this order, then the mods'. A family shares one animation
 * collection (ONCC Animations), so its versions fight with the same moves: those with another
 * collection are families of their own, e.g. mad_bomber (comguy's, not thug's), ninjabot
 * (comguy's with its own groin hit, not ninja's), Shinzom_generic (Hasegawa, not Shinatama's),
 * the TCTF lite troopers and male cops (comguy's), the female cops (Konoko's). Every class of the
 * game's level0 is in one, so a class in none comes from a mod. Mods by AE package: Hanako 23615
 * (+ Rose Outfit 23616), Naomi 21000, KTSN Janice A/B 29900/29901, BGI Sarai 22015, Kit Ballard
 * 29915 (red_*_4), Cosplay Lara 25100, Shinatama Evolved 23100, RAYMAN 23640, Dragon Unit 23680. */
typedef struct { const char *label, *main, *classes; int mod; } Family;
static const Family g_families[] = {
	{ "Konoko", "konoko_generic", "konoko_generic|k3|k4_A|k4_F|k4_G|k4_K|k4_L", 0 },
	{ "Griffin", "griffin_generic", "griffin_generic|griffin_swat", 0 },
	{ "Muro", "muro_generic", "muro_generic", 0 },
	{ "Barabas", "barabus", "barabus", 0 },
	{ "Shinatama", "SH_generic", "SH_generic|SH_dream_lab", 0 },
	{ "Hasegawa", "Shinzom_generic", "Shinzom_generic", 0 },
	{ "Ninja", "ninja_med_1", "ninja_easy_1|ninja_easy_2|ninja_easy_3|ninja_med_1|ninja_med_2|ninja_med_3|"
	  "ninja_hard_1|ninja_hard_2|ninja_hard_3|super_ninja_1", 0 },
	{ "Striker", "striker_med_1", "striker_easy_1|striker_easy_2|striker_easy_3|striker_med_1|striker_med_2|"
	  "striker_med_3|striker_hard_1|striker_hard_2|striker_hard_3|striker_dream_lab", 0 },
	{ "Tanker", "tanker_med_1", "tanker_easy_1|tanker_med_1|tanker_hard_1", 0 },
	{ "Comguy", "comguy_1", "comguy_1|comguy_2|comguy_3", 0 },
	{ "Elite", "elite_med_1", "elite_easy_1|elite_easy_2|elite_easy_3|elite_med_1|elite_med_2|elite_med_3|"
	  "elite_hard_1|elite_hard_2|elite_hard_3", 0 },
	{ "Red", "red_med_1", "red_easy_1|red_easy_2|red_easy_3|red_med_1|red_med_2|red_med_3|red_hard_1|"
	  "red_hard_2|red_hard_3", 0 },
	{ "TCTF SWAT", "TCTF_swat_1", "TCTF_swat_1|TCTF_swat_blackops_1", 0 },
	{ "TCTF Trooper", "TCTF_lite_1", "TCTF_lite_1|TCTF_lite_blackops_1", 0 },
	{ "Cop", "Cop_generic", "Cop_generic|CopmaleA1|CopmaleA2|CopmaleA3|CopmaleA4", 0 },
	{ "Female Cop", "CopfemA1", "CopfemA1|CopfemA2", 0 },
	{ "Thug", "thug_wh_1", "thug_wh_1|thug_wh_2|thug_wh_3|thug_wh_4|thug_wh_5|thug_mfg_1|thug_mfg_2|"
	  "thug_mfg_3|thug_mfg_4|thug_mfg_5|thug_air_1|thug_air_2|thug_air_3|thug_air_4|thug_air_5|"
	  "thug_neuro_1|thug_neuro_2|thug_neuro_3|thug_neuro_4|thug_neuro_5|thug_pow_1|thug_pow_2|"
	  "thug_pow_3|thug_pow_4|thug_pow_5", 0 },
	{ "Mad Bomber", "mad_bomber", "mad_bomber", 0 },
	{ "Ninjabot", "ninjabot", "ninjabot", 0 },
	{ "Sniper", "sniper_generic", "sniper_generic", 0 },
	{ "Security Guard", "security_guard_1", "security_guard_1|security_guard_2", 0 },
	{ "Scientist", "bd_1", "bd_1|bd_2|bd_3|bd_4|bd_5", 0 },
	{ "Female Scientist", "bdfem_1", "bdfem_1|bdfem_2|bdfem_3|bdfem_4", 0 },
	{ "Civilian", "generic_male_1", "generic_male_1|generic_male_2|generic_male_3|generic_male_4|"
	  "generic_male_5|generic_male_6", 0 },
	{ "Female Civilian", "generic_female_1", "generic_female_1|generic_female_2|generic_female_3|"
	  "generic_female_4|generic_female_5|generic_female_6|generic_female_7|generic_female_8|generic_female_9", 0 },
	{ "Mutant Muro", "mutantmuro", "mutantmuro", 0 },
	{ "Hanako", "hanako_classic", "hanako_classic|hanako_black|hanako_white|hanako_red|hanako_ylw|"
	  "hanako_jacket|hanako_suit|hanako_chun|hanako_rose", 1 },
	{ "Naomi", "naomi", "naomi|naomi_battle|naomi_camo|naomi_hunter|naomi_special|naomi_rose", 1 },
	{ "Janice", "KTSNjanA", "KTSNjanA|KTSNjanB", 1 },
	{ "BGI Sarai", "bgi_sarai", "bgi_sarai|bgi_sarai_jkt|bgi_sarai_arm|bgi_sarai_grn", 1 },
	{ "Kit Ballard", "red_med_4", "red_easy_4|red_med_4|red_hard_4", 1 },
	{ "Lara", "lara", "lara", 1 },
	{ "Shinatama Evolved", "SH_bionic", "SH_bionic|SH_bot|SH_bot2|SH_borg|SH_borg_black|SH_borg_gold", 1 },
	{ "RAYMAN", "rayman", "rayman", 1 },
	{ "Dragon Unit", "daodanized_GATC_1B", "daodanized_GATC_1B", 1 },
};
#define FAMILIES ((int)(sizeof g_families / sizeof g_families[0]))

typedef struct {
	char cls[64];               /* the class shown first */
	char label[48];
	const char *family;         /* its g_families list, or 0 */
	char spec[256];             /* the ini's own variation list ("a|b|c"), or "" */
	int mod;                    /* a mod's: the Mods section */
	u8 *oncc;
	u32 number;                 /* among the loaded ONCCs */
	void *pose;                 /* the big picture's TRAM, 0: Konoko's idle */
	u32 frame;
	void *stance;               /* the portrait's, 0: Konoko's idle */
	int nvar, var;              /* variations, and the one shown */
	u8 *voncc[MAX_VARS];
	u32 vnumber[MAX_VARS];
	int line, col;              /* its place in the grid */
	int shown, x, y, w, h;      /* its cell, from the last draw */
} Entry;

static Entry g_roster[MAX_ROSTER];
static int g_count, g_resolved, g_any_vars, g_auto;
static int g_choice = -1;       /* roster index chosen, -1: the map's class */
static int g_choice_var;        /* its variation */
static char g_choice_name[64];  /* its class, to find it again after level0 changes */
static int g_name_x, g_name_y = -1, g_name_w, g_name_h;   /* the name's label and field, from the last draw */
static u8 g_mute[256];          /* keys that typing used: no press or release until they're up */
static int g_was_editing;
static int g_open, g_sel, g_saved_input;
static u32 g_level;
static u8 *g_panel, *g_cell, *g_hilite, *g_green, *g_thumb;
static int g_mx = -1, g_my = -1, g_mouse_moved, g_in_window;
static u8 g_down[256];

/* The grid, from the last draw: lines of portraits and section titles */
typedef struct { int first, count, header; } Line;   /* a header's first: 0 the game's, 1 the mods' */
static Line g_lines[MAX_LINES];
static int g_nlines, g_top_line, g_shown_lines, g_scrollable, g_follow;
static int g_sb_x, g_sb_y, g_sb_w, g_sb_h, g_thumb_y, g_thumb_h;   /* the scroll bar and its thumb */
static int g_pan_x, g_pan_y, g_pan_w, g_pan_h;                      /* the big picture's panel */
static int g_pic_x, g_pic_y;                                        /* the big picture */
static int g_arrow_y, g_arrow_w, g_arrow_h, g_arrow_l, g_arrow_r;   /* the picked portrait's < >; g_arrow_h 0: none */

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

/* Is name one of the list's "a|b|c" (no spaces)? */
static int in_list(const char *list, const char *name)
{
	unsigned l = str_len(name);
	while (*list) {
		unsigned n = 0;
		while (list[n] && list[n] != '|') n++;
		if (n == l && str_ieq_n(list, name, n)) return 1;
		list += n + (list[n] == '|');
	}
	return 0;
}

static int family_of(const char *name)
{
	for (int f = 0; f < FAMILIES; f++)
		if (in_list(g_families[f].classes, name)) return f;
	return -1;
}

/* Women, for the Daodan mutator's particles (mp_rules.c: Konoko's for women, Muro's for men): by
 * family, the game's and the known mods'; a class in no family by its name ("fem"), else a man */
static const char *const g_women[] = {
	"Konoko", "Shinatama", "Red", "Female Cop", "Female Scientist", "Female Civilian",
	"Hanako", "Naomi", "Janice", "BGI Sarai", "Kit Ballard", "Lara", "Shinatama Evolved",
};

int char_menu_class_female(const char *name)
{
	int f;
	if (!name) return 0;
	if ((f = family_of(name)) >= 0) {
		for (u32 i = 0; i < sizeof g_women / sizeof g_women[0]; i++)
			if (str_len(g_families[f].label) == str_len(g_women[i]) && str_ieq_n(g_families[f].label, g_women[i], 64)) return 1;
		return 0;
	}
	for (const char *s = name; *s; s++)
		if (str_ieq_n(s, "fem", 3)) return 1;
	return 0;
}

/* Its family's label and section, or for a class in none, a mod's: "my_char_2" -> "My char 2" */
static void entry_names(Entry *e)
{
	int f = family_of(e->cls);
	e->family = f >= 0 ? g_families[f].classes : 0;
	e->mod = f >= 0 ? g_families[f].mod : 1;
	if (f >= 0) {
		str_cpy(e->label, sizeof e->label, g_families[f].label);
	} else {
		unsigned n = 0;
		for (; e->cls[n] && n < sizeof e->label - 1; n++) e->label[n] = e->cls[n] == '_' ? ' ' : e->cls[n];
		e->label[n] = 0;
		e->label[0] = to_upper(e->label[0]);
	}
}

static void roster_load(void)
{
	char list[4096];
	const char *s = list;

	GetPrivateProfileStringA("multiplayer", "characters", "", list, sizeof list, ini_path());
	g_count = 0;
	g_auto = !*skip_space(list);   /* every loaded character, found when level0 is there */
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
		entry_names(e);
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

/* Its first word: hanako_red -> HANAKO */
static void first_word(const char *name, char *word, unsigned size)
{
	unsigned n = 0;
	while (name[n] && name[n] != '_' && n < size - 1) { word[n] = to_upper(name[n]); n++; }
	word[n] = 0;
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

/* Adds the classes of a "a|b|c" list */
static void add_list(Entry *e, const char *list)
{
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
		if (n && !TMrInstance_GetDataPtr(TAG_ONCC, name, &oncc)) add_variation(e, oncc);
	}
}

static void find_variations(Entry *e)
{
	e->nvar = 0;
	if (e->spec[0]) {
		add_list(e, e->spec);
	} else if (e->family) {
		add_list(e, e->family);
	} else {
		/* a class no family lists: the others no family lists with its name apart from the
		 * number and difficulty, or its first word and the same animations */
		char key[64], word[64], other[64];
		u32 total = TMrInstance_GetTagCount(TAG_ONCC);
		family_key(e->cls, key, sizeof key);
		first_word(e->cls, word, sizeof word);
		for (u32 k = 0; k < total; k++) {
			u8 *p = 0;
			const char *name;
			if (TMrInstance_GetDataPtr_ByNumber(TAG_ONCC, k, (void **)&p) || !p || !(name = TMrInstance_GetInstanceName(p)) ||
			    family_of(name) >= 0) continue;
			family_key(name, other, sizeof other);
			int same = str_len(key) == str_len(other) && str_ieq_n(key, other, 64);
			if (!same && str_len(word) >= 3 && ONCC_ANIMATIONS(p) == ONCC_ANIMATIONS(e->oncc)) {
				first_word(name, other, sizeof other);
				same = str_len(word) == str_len(other) && str_ieq_n(word, other, 64);
			}
			if (same) add_variation(e, p);
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

/* Characters only the host's Host dialog lets players pick (net_char_rules, NET_CHARS_*) */
static const struct { const char *cls; u32 rule; } g_held_back[] = {
	{ "mutantmuro", NET_CHARS_MUTANT_MURO },
	{ "SH_generic", NET_CHARS_SHINATAMA },
	{ "SH_dream_lab", NET_CHARS_SHINATAMA },
	{ "Shinzom_generic", NET_CHARS_SHINATAMA },   /* Hasegawa */
	{ "striker_dream_lab", NET_CHARS_MINI_STRIKER },   /* Mini Striker (a Striker variation) */
};
static u32 g_rules_applied = NET_CHARS_ALL;   /* the roster was resolved with these */

static int name_allowed(const char *name, u32 rules)
{
	if (!name) return 1;
	for (u32 i = 0; i < sizeof g_held_back / sizeof g_held_back[0]; i++)
		if (!(rules & g_held_back[i].rule) && str_len(name) == str_len(g_held_back[i].cls) &&
		    str_ieq_n(name, g_held_back[i].cls, 64)) return 0;
	return 1;
}

int char_menu_class_allowed(int number)
{
	u32 rules = net_char_rules();
	u8 *p = 0;

	if (number < 0 || rules == NET_CHARS_ALL) return 1;
	if (TMrInstance_GetDataPtr_ByNumber(TAG_ONCC, (u32)number, (void **)&p) || !p) return 1;
	return name_allowed(TMrInstance_GetInstanceName(p), rules);
}

/* The variations the game's rules leave out go; the entry too when none is left */
static int entry_allowed(Entry *e)
{
	int n = 0, kept_var = -1;

	for (int v = 0; v < e->nvar; v++) {
		if (!name_allowed(TMrInstance_GetInstanceName(e->voncc[v]), g_rules_applied)) continue;
		if (v == e->var) kept_var = n;
		e->voncc[n] = e->voncc[v];
		e->vnumber[n++] = e->vnumber[v];
	}
	if (!n) {
		log_msg("char menu: %s left out (the host's rules)", e->cls);
		return 0;
	}
	if (n < e->nvar) {   /* its own class may be gone: the first one left shows */
		e->nvar = n;
		e->var = kept_var >= 0 ? kept_var : 0;
		e->oncc = e->voncc[e->var];
		e->number = e->vnumber[e->var];
	}
	return 1;
}

/* Its class, if it's loaded and a biped, and its variations */
static int entry_resolve(Entry *e)
{
	u8 *oncc = 0;
	if (TMrInstance_GetDataPtr(TAG_ONCC, e->cls, &oncc) || !oncc) {
		log_msg("char menu: no character class %s", e->cls);
		return 0;
	}
	if (!biped(oncc)) {
		log_msg("char menu: %s isn't a biped, left out", e->cls);
		return 0;
	}
	e->oncc = oncc;
	if ((e->number = class_number(oncc)) == 0xffffffffu) return 0;
	find_variations(e);
	return entry_allowed(e);
}

static int loaded_biped(const char *name)
{
	u8 *p = 0;
	return !TMrInstance_GetDataPtr(TAG_ONCC, name, &p) && p && biped(p);
}

/* Every family with a class loaded, then every loaded class in none, each with its versions */
static void roster_every(void)
{
	static u8 taken[MAX_CLASSES];
	u32 total = TMrInstance_GetTagCount(TAG_ONCC);

	g_count = 0;
	for (int f = 0; f < FAMILIES && g_count < MAX_ROSTER; f++) {
		const Family *fam = &g_families[f];
		char first[64] = "";
		if (loaded_biped(fam->main)) {
			str_cpy(first, sizeof first, fam->main);
		} else {   /* its first one loaded */
			const char *l = fam->classes;
			while (*l && !first[0]) {
				unsigned n = 0;
				while (l[n] && l[n] != '|') n++;
				if (n < sizeof first) {
					memcpy(first, l, n);
					first[n] = 0;
					if (!loaded_biped(first)) first[0] = 0;
				}
				l += n + (l[n] == '|');
			}
		}
		if (!first[0]) continue;
		Entry *e = &g_roster[g_count];
		memset(e, 0, sizeof *e);
		str_cpy(e->cls, sizeof e->cls, first);
		entry_names(e);
		if (entry_resolve(e)) g_count++;
	}
	memset(taken, 0, sizeof taken);
	for (u32 k = 0; k < total && k < MAX_CLASSES && g_count < MAX_ROSTER; k++) {
		u8 *p = 0;
		const char *name;
		if (taken[k] || TMrInstance_GetDataPtr_ByNumber(TAG_ONCC, k, (void **)&p) || !p || !biped(p) ||
		    !(name = TMrInstance_GetInstanceName(p)) || family_of(name) >= 0) continue;
		Entry *e = &g_roster[g_count];
		memset(e, 0, sizeof *e);
		str_cpy(e->cls, sizeof e->cls, name);
		entry_names(e);
		if (!entry_resolve(e)) continue;
		for (int v = 0; v < e->nvar; v++)
			if (e->vnumber[v] < MAX_CLASSES) taken[e->vnumber[v]] = 1;
		log_msg("char menu: %s, a mod's character", e->cls);
		g_count++;
	}
}

static void pick_poses(void);

/* The classes, once level0 is loaded: drop the ones that aren't there or aren't bipeds */
static void roster_resolve(void)
{
	int n = 0;

	g_any_vars = 0;
	g_rules_applied = net_char_rules();
	if (g_auto) {
		roster_every();
	} else {
		for (int i = 0; i < g_count; i++) {
			Entry *e = &g_roster[i];
			if (entry_resolve(e)) g_roster[n++] = *e;
		}
		g_count = n;
	}
	for (int i = 0; i < g_count; i++)
		if (g_roster[i].nvar > 1) log_msg("char menu: %s has %d variations", g_roster[i].cls, g_roster[i].nvar);
	pick_poses();
	g_resolved = 1;
	g_nlines = 0;
	/* the choice, by its class: level0 may have changed under it */
	g_choice = -1;
	for (int i = 0; i < g_count && g_choice_name[0]; i++)
		for (int v = 0; v < g_roster[i].nvar; v++) {
			const char *name = TMrInstance_GetInstanceName(g_roster[i].voncc[v]);
			if (name && str_len(name) == str_len(g_choice_name) && str_ieq_n(name, g_choice_name, 64)) {
				g_choice = i;
				g_choice_var = v;
			}
		}
	log_msg("char menu: %d characters in the F2 menu", g_count);
}

/* ---- Textures ------------------------------------------------------------------------------ */

/* An 8x8 TXMP of Oni's (a real instance: a slot and a few bytes of the dynamic file), kept out
 * of rollback's restores: the struct holds the size, the pixels pointer, the GL texture name
 * (+0x9c, given by glGenTextures at 0x40cfdb) and the dirty flag (+0xa0), after the instance's
 * 8-byte header */
static u8 *texture_make(const char *name)
{
	u8 *t = 0;
	/* 1: the file that outlives levels. With 3 a level load (0x4217d0) emptied the file, so the
	 * next picture made got the panel's slot, and the menu's background tiled a character. */
	if (M3rTextureMap_New(8, 8, PIXEL_ARGB1555, 1, 0, name, &t) || !t || !TXMP_TEXELS(t)) {
		log_msg("char menu: can't make texture %s", name);
		return 0;
	}
	rollback_keep(t - 8, 8 + 0xa4);
	return t;
}

/* Textures made before a networked match starts (hud_textures_prepare): a rewind puts back the
 * dynamic file's own bookkeeping too, so a texture made during the match could get the slot of
 * one made after the tick it rewound to, and the two drew each other (the menu's cells full of
 * character pictures, on the joiner). */
static u8 *g_pool[MAX_ROSTER + PREVIEWS + 16];
static int g_npool;

/* A texture given w x h texels (rounded up to powers of two) of our own memory. The dynamic
 * file's pixel memory is small and fixed (1 MB) and Oni's own runtime textures come from it:
 * full-size pictures there ran it out ("Unable to allocate the pixels" in debugger.txt), and the
 * game froze later. */
static int texture_size(u8 *t, int w, int h)
{
	u8 *texels;
	int tw = 8, th = 8;

	while (tw < w) tw <<= 1;
	while (th < h) th <<= 1;
	if (tw > TXMP_W(t) || th > TXMP_H(t)) {
		texels = VirtualAlloc(0, (u32)(tw * th * 2), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!texels) {
			log_msg("char menu: no memory for texture %s", (const char *)t);
			return 0;
		}
		TXMP_W(t) = (u16)tw;
		TXMP_H(t) = (u16)th;
		TXMP_TEXELS(t) = texels;
	}
	TXMP_DIRTY(t) = 1;   /* uploaded again when next drawn */
	return 1;
}

static u8 *texture_new(int w, int h, const char *name)
{
	u8 *t;

	if (g_npool) {
		t = g_pool[--g_npool];
		str_cpy((char *)t, 0x80, name);
	} else if (!(t = texture_make(name))) {
		return 0;
	}
	return texture_size(t, w, h) ? t : 0;
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
	case 7: {   /* bytes R G B A (OniSplit's RGBA: Ninjabot's texture, HD retextures) */
		u32 p = ((const u32 *)d)[y * w + x];
		return (p & 0xff) << 16 | (p & 0xff00) | (p >> 16 & 0xff);
	}
	case 8:     /* bytes B G R x (OniSplit's BGR) */
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

/* The picture of a class in a pose, w x h pixels: the whole body, or a portrait (head and
 * shoulders, from nearer the front); 0 if the class can't be drawn */
static int render(u8 *oncc, void *pose, u32 frame, int portrait, u16 *out, int pitch, int w, int h)
{
	u8 *body = ((u8 **)ONCC_BODY(oncc))[4];
	float quats[PARTS][4], root[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
	Part parts[PARTS];
	u32 first[PARTS + 1], total = 0;
	View v;

	TRrAnimation_GetRotations(pose ? pose : g_idle, pose ? frame : 0, &quats[0][0]);
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
	/* the camera 35 degrees to the side and 22 above the front; a portrait's 20 and 8 */
	float side[3], dir[3], *cam_right = v.right;
	float cs = portrait ? 0.93969f : 0.81915f, sn = portrait ? 0.34202f : 0.57358f;
	float ce = portrait ? 0.99027f : 0.92718f, se = portrait ? 0.13917f : 0.37461f;
	cross(up, fwd, side);
	for (int k = 0; k < 3; k++)
		dir[k] = (fwd[k] * cs + side[k] * sn) * ce + up[k] * se;
	for (int k = 0; k < 3; k++) v.view[k] = -dir[k];
	cross(v.view, up, cam_right);
	norm(cam_right);
	cross(cam_right, v.view, v.up);
	for (int k = 0; k < 3; k++) v.light[k] = -0.35f * v.right[k] + 0.8f * v.up[k] + 0.5f * dir[k];
	norm(v.light);

	/* fitted to the whole body, or to the head for a portrait */
	u32 from = 0, to = total;
	if (portrait && first[HEAD + 1] > first[HEAD]) { from = first[HEAD]; to = first[HEAD + 1]; }
	float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
	for (u32 k = from; k < to; k++) {
		float q[2] = { dot(g_world[k], v.right), dot(g_world[k], v.up) };
		for (int j = 0; j < 2; j++) {
			if (q[j] < lo[j]) lo[j] = q[j];
			if (q[j] > hi[j]) hi[j] = q[j];
		}
	}
	v.w = w * SS;
	v.h = h * SS;
	if (portrait && to - from < total) {
		/* the head 40% of the height, its top a little under the picture's; a head taller than
		 * wide (a ponytail down the back) counts as 1.4 times its width */
		float size = hi[1] - lo[1], wide = hi[0] - lo[0];
		if (size > wide * 1.4f) size = wide * 1.4f;
		if (size < wide * 0.9f) size = wide * 0.9f;
		if (size <= 0) return 0;
		v.scale = v.h / (size * 2.4f);
		v.cx = (lo[0] + hi[0]) * 0.5f;
		v.cy = hi[1] + size * 0.25f - v.h * 0.5f / v.scale;
	} else {
		float sx = (hi[0] - lo[0]) / v.w, sy = (hi[1] - lo[1]) / v.h, ext = sx > sy ? sx : sy;
		if (ext <= 0) return 0;
		v.scale = 0.92f / ext;
		v.cx = (lo[0] + hi[0]) * 0.5f;
		v.cy = (lo[1] + hi[1]) * 0.5f;
	}

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

/* The first of the wanted animations e's class has, its own collection before its parents'; at
 * most 16 */
static int find_poses(Entry *e, const char *const *wanted, int nwanted, void **found)
{
	int n = 0;
	for (u8 *trac = ONCC_ANIMATIONS(e->oncc); trac && n < 16; trac = TRAC_PARENT(trac))
		for (int w = 0; w < nwanted; w++)
			for (u32 i = 0; i < TRAC_COUNT(trac) && n < 16; i++) {
				void *a = TRAC_ANIMATION(trac, i);
				const char *name = a ? TMrInstance_GetInstanceName(a) : 0;
				if (name && str_len(name) > 3 && str_ieq_n(name + 3, wanted[w], 64) &&
				    TRAM_BONES(a) == PARTS && TRAM_FRAMES(a)) {
					found[n++] = a;
					break;
				}
			}
	return n;
}

/* The big picture: the class's own stance or taunt (see the top), one no earlier class in the
 * roster took; the portrait: its stance */
static void pick_pose(Entry *e, int index)
{
	static const char *const wanted[] = { "COMtaunt1", "COMtaunt2", "COMidle1", "COMidle2", "COMidle3", "COMidle4" };
	void *found[16];
	int n = find_poses(e, wanted + 2, 4, found);

	e->pose = 0;
	e->frame = 0;
	e->stance = n ? found[0] : 0;
	n = find_poses(e, wanted, 6, found);
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

static void pick_poses(void)
{
	if (!g_idle && (TMrInstance_GetDataPtr(TAG_TRAM, IDLE_ANIMATION, &g_idle) || !g_idle || TRAM_BONES(g_idle) != PARTS)) {
		log_msg("char menu: no %s animation, no pictures", IDLE_ANIMATION);
		g_idle = 0;
	}
	for (int i = 0; i < g_count; i++) pick_pose(&g_roster[i], i);
}

/* Portraits: one texture per place in the roster, kept from one roster to the next */
static u8 *g_tile_tex[MAX_ROSTER];
static u8 *g_tile_of[MAX_ROSTER];      /* the class its texture shows, 0: none yet */
static u8 g_tile_ok[MAX_ROSTER];
static int g_tile;                     /* their side */

/* Big pictures: the last PREVIEWS rendered */
typedef struct { u8 *tex, *oncc; int ok; u32 used; } Preview;
static Preview g_prev[PREVIEWS];
static u32 g_prev_clock;
static int g_pic_w, g_pic_h;

static void pictures_forget(void)
{
	memset(g_tile_of, 0, sizeof g_tile_of);
	for (int s = 0; s < PREVIEWS; s++) g_prev[s].oncc = 0;
}

static int draw_into(u8 **t, const char *name, int w, int h)
{
	if (*t ? !texture_size(*t, w, h) : !(*t = texture_new(w, h, name))) return 0;
	memset(TXMP_TEXELS(*t), 0, TXMP_W(*t) * TXMP_H(*t) * 2);
	return 1;
}

/* The portrait of entry i's shown variation; *budget: how many more may be rendered this frame */
static u8 *portrait(int i, int *budget)
{
	Entry *e = &g_roster[i];
	u8 *oncc = e->voncc[e->var];
	char name[32];

	if (g_tile_of[i] == oncc) return g_tile_ok[i] ? g_tile_tex[i] : 0;
	if (*budget <= 0 || !g_idle) return 0;
	(*budget)--;
	g_tile_of[i] = oncc;
	g_tile_ok[i] = 0;
	fmt(name, "arklite_face_%d", i);
	if (!draw_into(&g_tile_tex[i], name, g_tile, g_tile)) return 0;
	g_tile_ok[i] = (u8)render(oncc, e->stance, 0, 1, (u16 *)TXMP_TEXELS(g_tile_tex[i]), TXMP_W(g_tile_tex[i]), g_tile, g_tile);
	return g_tile_ok[i] ? g_tile_tex[i] : 0;
}

/* The big picture of entry i's shown variation */
static u8 *preview(int i)
{
	Entry *e = &g_roster[i];
	u8 *oncc = e->voncc[e->var];
	int s, old = 0;
	char name[32];

	for (s = 0; s < PREVIEWS; s++)
		if (g_prev[s].oncc == oncc) {
			g_prev[s].used = ++g_prev_clock;
			return g_prev[s].ok ? g_prev[s].tex : 0;
		}
	if (!g_idle) return 0;
	for (s = 1; s < PREVIEWS; s++)
		if (g_prev[s].used < g_prev[old].used) old = s;
	Preview *p = &g_prev[old];
	p->oncc = oncc;
	p->ok = 0;
	p->used = ++g_prev_clock;
	fmt(name, "arklite_char_%d", old);
	if (draw_into(&p->tex, name, g_pic_w, g_pic_h))
		p->ok = render(oncc, e->pose, e->frame, 0, (u16 *)TXMP_TEXELS(p->tex), TXMP_W(p->tex), g_pic_w, g_pic_h);
	return p->ok ? p->tex : 0;
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

static void roster_ready(void);

static void open_menu(void)
{
	roster_ready();
	if (!g_count) return;
	g_open = 1;
	g_level = ONrLevel_GetCurrent();
	g_sel = g_choice >= 0 ? g_choice : 0;
	if (g_choice >= 0) g_roster[g_choice].var = g_choice_var;
	g_saved_input = LIrMode_GetGameInput();
	LIrMode_SetGameInput(0);
	g_mouse_moved = 0;
	g_follow = 1;
	menu_wheel_hud_take();   /* notches from before */
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
	e->var = ((e->var + step) % e->nvar + e->nvar) % e->nvar;
}

static void choose(int i)
{
	const char *name;
	if (i < 0 || i >= g_count) return;
	g_choice = i;
	g_choice_var = g_roster[i].var;
	name = TMrInstance_GetInstanceName(g_roster[i].voncc[g_choice_var]);
	str_cpy(g_choice_name, sizeof g_choice_name, name ? name : "");
	log_msg("char menu: chose %s", g_choice_name);
}

static int inside(int x, int y, int w, int h) { return g_mx >= x && g_mx < x + w && g_my >= y && g_my < y + h; }

/* Every frame, after the update (live_menu.c's main loop hook) */
void char_menu_frame(void)
{
	u32 pid = 0;
	void *w = GetForegroundWindow();
	int focus, esc_up, f2, left, right, up, down, enter, space, click, tab, prev, next, wheel;

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
	wheel = menu_wheel_hud_take();

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
	int hover = -1;
	int on_name = g_in_window && g_name_y >= 0 && inside(g_name_x, g_name_y, g_name_w, g_name_h);
	int on_panel = g_in_window && inside(g_pan_x, g_pan_y, g_pan_w, g_pan_h);
	int on_bar = g_in_window && g_scrollable && inside(g_sb_x - g_sb_w, g_sb_y, 3 * g_sb_w, g_sb_h);
	if (g_in_window)
		for (int i = 0; i < g_count; i++) {
			const Entry *e = &g_roster[i];
			if (e->shown && inside(e->x, e->y, e->w, e->h)) hover = i;
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
	if (g_sel >= g_count) g_sel = 0;
	if (prev) cycle(g_sel, -1);
	if (next) cycle(g_sel, 1);
	if (wheel) {
		if (on_panel || !g_scrollable) cycle(g_sel, -wheel);
		else g_top_line -= wheel;   /* kept in range by the draw */
	}

	if (click && g_arrow_h && g_my >= g_arrow_y && g_my < g_arrow_y + g_arrow_h) {   /* the picked portrait's < > */
		if (g_mx >= g_arrow_l && g_mx < g_arrow_l + g_arrow_w) { cycle(g_sel, -1); return; }
		if (g_mx >= g_arrow_r && g_mx < g_arrow_r + g_arrow_w) { cycle(g_sel, 1); return; }
	}
	if (click && on_bar) {   /* a page up or down */
		int page = g_shown_lines > 2 ? g_shown_lines - 1 : 1;
		if (g_my < g_thumb_y) g_top_line -= page;
		else if (g_my >= g_thumb_y + g_thumb_h) g_top_line += page;
		return;
	}

	/* the arrows: along the roster, and to the nearest portrait on the line above or below */
	if (left && g_sel > 0) { g_sel--; g_follow = 1; }
	if (right && g_sel < g_count - 1) { g_sel++; g_follow = 1; }
	if ((up || down) && g_nlines) {
		const Entry *e = &g_roster[g_sel];
		for (int l = e->line + (up ? -1 : 1); l >= 0 && l < g_nlines; l += up ? -1 : 1)
			if (!g_lines[l].header) {
				g_sel = g_lines[l].first + (e->col < g_lines[l].count ? e->col : g_lines[l].count - 1);
				g_follow = 1;
				break;
			}
	}

	int on_picture = on_panel && inside(g_pic_x, g_pic_y, g_pic_w, g_pic_h);
	if (click && hover >= 0) { choose(hover); close_menu(); }
	else if (click && on_picture) { choose(g_sel); close_menu(); }
	else if (enter || space) { choose(g_sel); close_menu(); }
	else if (f2 || esc_up) close_menu();
}

static void rect(u8 *t, int x, int y, int w, int h, u32 alpha)
{
	float at[4] = { (float)x, (float)y, 0.5f, 2.0f };
	if (t && w > 0 && h > 0) M3rDraw_Bitmap(t, at, (u32)w, (u32)h, 0xffffffffu, alpha);
}

static void frame_rect(u8 *t, int x, int y, int w, int h, int b, u32 alpha)
{
	rect(t, x, y, w, b, alpha);
	rect(t, x, y + h - b, w, b, alpha);
	rect(t, x, y + b, b, h - 2 * b, alpha);
	rect(t, x + w - b, y + b, b, h - 2 * b, alpha);
}

/* net.c, before rollback starts: enough textures for every picture and the HUD's rectangles */
void hud_textures_prepare(void)
{
	static int done;
	int want = 16, places;

	if (done) return;
	done = 1;
	obit_textures_prepare();   /* the kill feed's icons */
	mp_hud_prepare();          /* the text's fonts (big_font.c) */
	if (!g_resolved) roster_resolve();
	places = g_count > 48 ? g_count : 48;   /* room for a level0 with more mods later */
	for (int i = 0; i < places; i++) want += !g_tile_tex[i];   /* the rest are made */
	for (int s = 0; s < PREVIEWS; s++) want += !g_prev[s].tex;
	while (g_npool < want && g_npool < (int)(sizeof g_pool / sizeof g_pool[0])) {
		char name[32];
		fmt(name, "arklite_hud_%d", g_npool);
		if (!(g_pool[g_npool] = texture_make(name))) break;
		g_npool++;
	}
	log_msg("char menu: %d textures made before the match", g_npool);
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

/* s cut to fit width pixels, ".." where it's cut; plain: in mp_hud_text_plain's style */
static void fit(const char *s, int width, char *out, unsigned size, int plain)
{
	int (*measure)(const char *, int) = plain ? mp_hud_text_width_plain : mp_hud_text_width;
	int n = (int)str_len(s), dots = measure("..", -1);
	str_cpy(out, size, s);
	if (measure(s, -1) <= width) return;
	while (n > 1 && measure(s, n) + dots > width) n--;
	if ((unsigned)n + 3 > size) n = (int)size - 3;
	memcpy(out, s, (unsigned)n);
	out[n] = out[n + 1] = '.';
	out[n + 2] = 0;
}

static void *part(const char *name)
{
	void *p = 0;
	return TMrInstance_GetDataPtr(TAG_PSpc, name, &p) ? 0 : p;
}

/* "Name: [Arkl1te   ]" centered at y (the text's top), drawn like the Host dialog's text fields
 * (mp_menu.c): the box is psui_oniUI's EditField art, ps_oniUI_background, and while typing its
 * focused art there, ps_oniUI_progressbar_track; the text is the dialogs' orange, the caret a
 * 2-pixel white bar (ps_white_none_none) after the text. */
static void name_field(int width, int y, int lh, int pad)
{
	static void *art, *focused, *white;
	static int looked;
	int editing = mp_name_editing();
	const char *text = editing ? mp_name_edit_text() : mp_name_local();

	if (!looked) {
		looked = 1;
		art = part("ps_oniUI_background");
		focused = part("ps_oniUI_progressbar_track");
		white = part("ps_white_none_none");
		if (!art || !focused || !white) log_msg("char menu: text field art missing, the name is drawn without it");
	}
	/* a field as wide as a long name (MP_NAME_MAX - 1 characters), wider if this one is */
	int label_w = mp_hud_text_width("Name:", -1), space = lh / 2, text_w = mp_hud_text_width(text, -1);
	int box_w = mp_hud_text_width("Mmmmmmmmmmmmmmmmmmmmmmm", MP_NAME_MAX - 1) + 2 * pad;
	if (box_w < text_w + 2 * pad + 4) box_w = text_w + 2 * pad + 4;
	int x = (width - (label_w + space + box_w)) / 2, box_x = x + label_w + space;
	int box_y = y - pad, box_h = lh + 2 * pad;

	mp_hud_text("Name:", x, y, MP_LEFT, ORANGE);
	void *box = editing && focused ? focused : art;
	if (box) {
		float at[4] = { (float)box_x, (float)box_y, 0.5f, 2.0f };
		PSrPartSpec_Draw(box, PS_ALL, at, (u32)box_w, (u32)box_h, 255);
	}
	mp_hud_text(text, box_x + pad, y, MP_LEFT, ORANGE);
	if (editing && white && GetTickCount() / 400 & 1) {
		float at[4] = { (float)(box_x + pad + text_w + 1), (float)y, 0.5f, 2.0f };
		PSrPartSpec_Draw(white, PS_MIDDLE_MIDDLE, at, 2, (u32)lh, 255);
	}
	/* the click that starts typing: the label or the box */
	g_name_x = x;
	g_name_y = box_y;
	g_name_w = box_x + box_w - x;
	g_name_h = box_h;
}

/* The grid's lines for cols portraits a line: a title before each section when there are both
 * the game's and mods' characters (only for the whole roster: an ini's list keeps its order) */
static void lines_build(int cols)
{
	int any_game = 0, any_mod = 0, sections, section = -1;
	for (int i = 0; i < g_count; i++) {
		any_game |= !g_roster[i].mod;
		any_mod |= g_roster[i].mod;
	}
	sections = g_auto && any_game && any_mod;
	g_nlines = 0;
	for (int i = 0; i < g_count && g_nlines < MAX_LINES - 1; i++) {
		Entry *e = &g_roster[i];
		Line *l = g_nlines ? &g_lines[g_nlines - 1] : 0;
		if (sections && e->mod != section) {
			section = e->mod;
			l = &g_lines[g_nlines++];
			l->first = section;
			l->count = 0;
			l->header = 1;
		}
		if (!l || l->header || l->count == cols) {
			l = &g_lines[g_nlines++];
			l->first = i;
			l->count = 0;
			l->header = 0;
		}
		e->line = (int)(l - g_lines);
		e->col = l->count++;
	}
}

static int lines_height(int from, int to, int row_h, int head_h)
{
	int h = 0;
	for (int l = from; l < to; l++) h += g_lines[l].header ? head_h : row_h;
	return h;
}

/* In the HUD (mp_hud.c), on top of everything else */
void char_menu_draw(int width, int height)
{
	static int last_w, last_h, last_count, last_lh, tw, cols, pw, ph, panel_w;
	char line[160];
	int lh = mp_hud_line_height();

	if (!g_open) return;
	if (!g_panel) {
		g_panel = solid(0x8000, "arklite_char_panel");          /* black */
		g_cell = solid(0x8000 | 6 << 10 | 6 << 5 | 7, "arklite_char_cell");
		g_hilite = solid(0x8000 | 31 << 10 | 26 << 5 | 6, "arklite_char_hilite");
		g_green = solid(0x8000 | 8 << 10 | 31 << 5 | 8, "arklite_char_green");
		g_thumb = solid(0x8000 | 22 << 10 | 22 << 5 | 22, "arklite_char_thumb");
	}

	/* the title, the Name field and the choice's line, then the grid and the big picture */
	int gap = height / 90 + 2, pad = gap / 3 + 2;
	int title_y = height / 14, name_y = title_y + lh + gap, choice_y = name_y + lh + pad + gap;
	int top = choice_y + lh + 2 * gap, bottom = height - height / 16 - lh;
	int area_h = bottom - top, area_w = width - width / 10;
	int cell_extra = lh + 2 * pad;   /* the name under a portrait */
	int row_h, head_h = lh + gap, sb_w = gap > 6 ? gap : 6;
	if (area_h < 4 * lh) return;

	if (width != last_w || height != last_h || g_count != last_count || lh != last_lh || !g_nlines) {
		/* the big picture: as tall as the area, 9/20 as wide (a standing character), at most a
		 * quarter of the width */
		ph = area_h - 2 * gap - 3 * (lh + gap);
		pw = ph * 9 / 20;
		panel_w = pw + 2 * gap;
		if (panel_w > area_w / 4) { panel_w = area_w / 4; pw = panel_w - 2 * gap; }
		if (pw > MAX_PW) pw = MAX_PW;
		if (ph > MAX_PH) ph = MAX_PH;
		/* the portraits: the biggest with which every line fits */
		int grid_w = area_w - panel_w - 3 * gap - sb_w, tmin = 3 * lh < 40 ? 40 : 3 * lh;
		for (tw = MAX_TILE; ; tw -= 2) {
			cols = (grid_w + gap) / (tw + gap);
			if (cols > g_count) cols = g_count;
			if (cols < 1) cols = 1;
			lines_build(cols);
			if (tw <= tmin || lines_height(0, g_nlines, tw + cell_extra + gap, head_h) <= area_h + gap) break;
		}
		last_w = width; last_h = height; last_count = g_count; last_lh = lh;
		g_follow = 1;
	}
	if (pw < 8 || ph < 16 || tw < 8) return;
	if (pw != g_pic_w || ph != g_pic_h || tw != g_tile) {
		g_pic_w = pw;
		g_pic_h = ph;
		g_tile = tw;
		pictures_forget();
	}
	row_h = tw + cell_extra + gap;

	/* the lines shown: from g_top_line, as many as fit; the picked one's in them */
	int max_top = g_nlines;
	while (max_top > 0 && lines_height(max_top - 1, g_nlines, row_h, head_h) <= area_h + gap) max_top--;
	g_scrollable = max_top > 0;
	if (g_follow && g_sel < g_count) {
		int l = g_roster[g_sel].line;
		if (l < g_top_line) g_top_line = l > 0 && g_lines[l - 1].header ? l - 1 : l;
		while (g_top_line < l && lines_height(g_top_line, l + 1, row_h, head_h) > area_h + gap) g_top_line++;
		g_follow = 0;
	}
	if (g_top_line > max_top) g_top_line = max_top;
	if (g_top_line < 0) g_top_line = 0;
	int end = g_top_line;
	while (end < g_nlines && lines_height(g_top_line, end + 1, row_h, head_h) <= area_h + gap) end++;
	g_shown_lines = end - g_top_line;

	/* the grid, its scroll bar and the panel, centered together */
	int grid_w = cols * tw + (cols - 1) * gap;
	int all_w = grid_w + (g_scrollable ? gap + sb_w : 0) + 2 * gap + panel_w;
	int x0 = (width - all_w) / 2;
	g_pan_x = x0 + all_w - panel_w;
	g_pan_y = top;
	g_pan_w = panel_w;
	g_pan_h = area_h;

	rect(g_panel, 0, 0, width, height, 170);
	mp_hud_text("Choose your character", width / 2, title_y, MP_CENTER, ORANGE);
	name_field(width, name_y, lh, pad);

	/* what F2 does now: a match (not the warm-up) changes a living player at the respawn */
	NetHud h;
	net_hud(&h);
	int now = players_can_change(h.state == NETHUD_OFF ? 0 : h.local_char);
	char chosen[96];
	if (g_choice >= 0) label(chosen, g_choice, g_choice_var);
	else str_cpy(chosen, sizeof chosen, "the map's character");
	fmt(line, now ? "Playing as: %s" : "Next respawn: %s", chosen);
	mp_hud_text(line, width / 2, choice_y, MP_CENTER, GREEN);

	/* the portraits: the picked one rendered first, then a few a frame in order */
	int budget = TILE_BUDGET, one = 1;
	for (int i = 0; i < g_count; i++) g_roster[i].shown = 0;
	g_arrow_h = 0;
	if (g_sel < g_count && g_roster[g_sel].line >= g_top_line && g_roster[g_sel].line < end) portrait(g_sel, &one);
	int y = top;
	for (int l = g_top_line; l < end; l++) {
		const Line *ln = &g_lines[l];
		if (ln->header) {
			mp_hud_text(ln->first ? "Mods" : "Oni", x0, y + gap / 2, MP_LEFT, ORANGE);
			y += head_h;
			continue;
		}
		for (int c = 0; c < ln->count; c++) {
			int i = ln->first + c;
			Entry *e = &g_roster[i];
			e->x = x0 + c * (tw + gap);
			e->y = y;
			e->w = tw;
			e->h = tw + cell_extra;
			e->shown = 1;
			rect(i == g_sel ? g_hilite : g_cell, e->x, e->y, e->w, e->h, i == g_sel ? 150 : 200);
			u8 *pic = portrait(i, &budget);
			if (pic) rect(pic, e->x, e->y, tw, tw, 255);
			if (i == g_choice) frame_rect(g_green, e->x, e->y, e->w, e->h, 2, 255);
			/* in the plain font, which is narrower; it may reach into the gaps beside */
			fit(e->label, tw + gap - 2, line, sizeof line, 1);
			mp_hud_text_plain(line, e->x + tw / 2, e->y + tw + pad, MP_CENTER, i == g_choice ? GREEN : i == g_sel ? YELLOW : WHITE);
			if (i == g_sel && e->nvar > 1) {   /* < > over the portrait's sides */
				g_arrow_w = tw / 6 > lh ? tw / 6 : lh;
				g_arrow_h = 2 * lh;
				g_arrow_y = e->y + (tw - g_arrow_h) / 2;
				g_arrow_l = e->x;
				g_arrow_r = e->x + tw - g_arrow_w;
				rect(g_panel, g_arrow_l, g_arrow_y, g_arrow_w, g_arrow_h, 150);
				rect(g_panel, g_arrow_r, g_arrow_y, g_arrow_w, g_arrow_h, 150);
				mp_hud_text("<", g_arrow_l + g_arrow_w / 2, g_arrow_y + lh / 2, MP_CENTER, YELLOW);
				mp_hud_text(">", g_arrow_r + g_arrow_w / 2, g_arrow_y + lh / 2, MP_CENTER, YELLOW);
			}
		}
		y += row_h;
	}
	if (g_scrollable) {
		int total = lines_height(0, g_nlines, row_h, head_h), above = lines_height(0, g_top_line, row_h, head_h);
		int shown = lines_height(g_top_line, end, row_h, head_h);
		g_sb_x = x0 + grid_w + gap;
		g_sb_y = top;
		g_sb_w = sb_w;
		g_sb_h = area_h;
		g_thumb_y = top + above * area_h / total;
		g_thumb_h = shown * area_h / total;
		if (g_thumb_h < sb_w) g_thumb_h = sb_w;
		rect(g_cell, g_sb_x, g_sb_y, g_sb_w, g_sb_h, 200);
		rect(g_thumb, g_sb_x, g_thumb_y, g_sb_w, g_thumb_h, 230);
	}

	/* the panel: the picked character's big picture, its name, variation and class */
	rect(g_cell, g_pan_x, g_pan_y, g_pan_w, g_pan_h, 200);
	if (g_sel < g_count) {
		Entry *e = &g_roster[g_sel];
		int picked = g_sel == g_choice && e->var == g_choice_var;
		const char *cls = TMrInstance_GetInstanceName(e->voncc[e->var]);
		u8 *pic = preview(g_sel);
		g_pic_x = g_pan_x + (g_pan_w - g_pic_w) / 2;
		g_pic_y = g_pan_y + gap;
		if (pic) rect(pic, g_pic_x, g_pic_y, g_pic_w, g_pic_h, 255);
		int ty = g_pan_y + g_pan_h - 3 * (lh + gap);
		fit(e->label, g_pan_w - 2 * gap, line, sizeof line, 0);
		mp_hud_text(line, g_pan_x + g_pan_w / 2, ty, MP_CENTER, picked ? GREEN : YELLOW);
		if (e->nvar > 1) {
			fmt(line, "<  %d / %d  >", e->var + 1, e->nvar);
			mp_hud_text(line, g_pan_x + g_pan_w / 2, ty + lh + gap, MP_CENTER, WHITE);
		}
		fit(cls ? cls : "", g_pan_w - 2 * gap, line, sizeof line, 1);
		mp_hud_text_plain(line, g_pan_x + g_pan_w / 2, ty + 2 * (lh + gap), MP_CENTER, GREY);
	}

	mp_hud_text(mp_name_editing() ? "Type your name   Enter: keep   Esc: cancel" :
	            g_any_vars ? "Arrows: pick   Enter or click: choose   Q/E or wheel: variation   Tab: name   F2/Esc: close" :
	                         "Arrows: pick   Enter or click: choose   Tab: name   F2/Esc: close",
	            width / 2, bottom + lh, MP_CENTER, GREY);
	if (g_in_window) {   /* the menus' arrow, as WMrCursor_Draw (0x468770) draws it */
		const u8 *cursor = WMrCursor_Get(WM_CURSOR_ARROW);
		float at[4] = { (float)(g_mx - 16), (float)(g_my - 16), 0.5f, 2.0f };
		if (cursor && *(void **)(cursor + 4)) PSrPartSpec_Draw(*(void **)(cursor + 4), PS_MIDDLE_MIDDLE, at, 32, 32, 255);
	}
}

/* The host's character rules came or changed (net.c): the roster is made again, the choice found
 * again by name (none if it's left out) */
void char_menu_rules_changed(void)
{
	char_menu_level0_changed();
}

/* Resolved with the rules in force (a game's end puts every character back) */
static void roster_ready(void)
{
	if (g_resolved && g_rules_applied != net_char_rules()) char_menu_level0_changed();
	if (!g_resolved) roster_resolve();
}

void char_menu_level0_changed(void)
{
	close_menu();
	roster_load();
	g_resolved = 0;
	g_any_vars = 0;
	g_choice = -1;   /* found again by name (g_choice_name) when the roster is */
	g_nlines = 0;
	pictures_forget();
}

void char_menu_install(void)
{
	roster_load();
	log_msg("char menu: F2 changes the character in multiplayer games (%s)",
	        g_auto ? "every loaded character" : "the ini's list");
}
