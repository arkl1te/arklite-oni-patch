/* The phase cloak in network games: the cloaked player sees themselves faintly (the body's
 * nearest surfaces at 30%, no shimmer; Arklite.ini [multiplayer] cloak_preset=1: the filter
 * chain's preset instead, 5% with its outline and tones); every other player sees nothing of them at all, except the shimmer
 * flashing up when the cloaked player lands a blow. Being hit shows nothing.
 *
 * Oni (ONrCharacter_Invis_Update 0x4ef530, the character draw around 0x4e1d00 with ESI the active
 * character and EBP the character):
 *   - character +0x1ba (u16) is the cloak left in ticks; active +0x2815 (invis_active) is set
 *     while cloaked, +0x2818 the body's alpha: 1 at pickup, fading 0.0085 a tick (0x52b33c) to 0,
 *     and back up to 1 in the last 120 ticks (the cloak wearing off).
 *   - a blow lifts the alpha of attacker and defender to at least 0.6 (0x52a9d4), which then fades
 *     the same way: melee hits at 0x4e7882 (attacker: EBX character, EDI active; defender: EBP,
 *     ESI), throws at 0x4e9c6d (thrower: EBX, EBP; target: ESI, EDI).
 *   - the body (and the weapon in hand, drawn with it) is drawn with that alpha (0x4e1dcc loads
 *     it), and so is its shadow (0x4e2317); at 0 neither is drawn.
 *   - while invis_active (0x4e1fe2) the "INVIS" shimmer is drawn over it at (1 - alpha) * 56.1
 *     (0x4e2030 fld 1.0; fsub alpha; fmul [0x52b300]).
 *
 * In a network game, for a character that isn't the one this screen belongs to (gs+0xac during
 * the display: this PC's player, or the one a spectator follows; net.c view_character):
 *   - body and shadow fade out as in Oni when the cloak comes on (1 to 0 in ~2 s; instantly 0 up
 *     to 2026-11-19, user request), then stay at 0 while the cloak is on (invis_active and more
 *     than 120 ticks left): Oni's alpha, but never back up (pickup_fade: the lowest seen, so a
 *     blow's lift to 0.6 shows nothing); the last 120 ticks fade the body back in as Oni does.
 *     No shimmer in that fade (only blows show it, below).
 *   - the shimmer is drawn only after this character attacked: the two attacker sites are calls
 *     that note the game tick of the blow (and answer invis_active as before, so the game itself
 *     does what it did). Its opacity is the one Oni would have given the body for the blow: 0.6,
 *     fading 0.0085 a tick, gone ~71 ticks (1.2 s) later (Oni's own shimmer never passes 0.22).
 * For the character this screen belongs to (in a network game, not a spectator's screen): the
 * body alpha is at least the opacity knob while invis_active (Oni's fade from 1 at
 * pickup stops there, and its fade back in at the end starts once it passes it; a blow's 0.6 still
 * shows; 0.3 (0.5 until 2026-11-19), or the opacity knob, 0.05, with cloak_preset; the shadow
 * keeps Oni's alpha, so it fades out completely, user request 2026-11-19), and
 * the shimmer isn't drawn (user request, 2026-11-13). How that body is drawn (user request,
 * 2026-11-14: only its front surfaces, and the textures' white showing) is "The see-through body".
 * Off the network everything is as in Oni (unless Arklite.ini [singleplayer] use_new_cloak_shader=1:
 * then the player's own body is drawn as above there too; single player's cloaked ninjas keep their shimmer and
 * flashes), and so it is for a spectator (/spectate, net_watching): they take no part in the
 * fight, so they see every cloaked player as Oni draws them, the fading body and the shimmer, and
 * their names. Drawing only: the noted ticks are Arklite's, not game state, so nothing can desync
 * (a rollback re-run notes the same tick again).
 *
 * The cloak's end (a stock bug, fixed everywhere): Invis_Update clears invis_active only when the
 * alpha is 0 and no cloak is left (0x4ef69c fld [0x529514] = 0.0; fcomp alpha), but the cloak's
 * last 120 ticks fade the body back to 1, so once a character had the cloak invis_active stayed
 * set for good. The character update skips motion blur while it's set (0x4e0516; OniFoxed
 * Oni_Character.c "if (!active_character->invis_active)"), so the ninja's dashes lost their
 * afterimages after the first cloak. The comparison is now with 1.0 (0x529508): cleared when the
 * body is fully back and no cloak is left. With none left the goal alpha is 1 and the fade-in is
 * min(alpha + 0.02, 1), so it always gets there. Nothing else reads it but drawing, the blow
 * flashes and a dead character's last updates (0x4dceb0), which now end as for anyone else.
 *
 * What else showed a cloaked player (user report, 2026-11-12): the shield shell and the Daodan
 * glow, both drawn apart from the body.
 *   - The shell (a force field picked up, or the Daodan shield's texture: the draw at 0x4e1f40,
 *     mov al,[esi+0x2800] shield_active, ESI active, EBP character) isn't drawn for a character
 *     whose body this screen hides (the rule above). Drawing only.
 *   - The super particles (the Daodan glow and its sparks: started and stopped in the character
 *     update by the super amount, fld [ebx+0x1668] at 0x4df937, EBX character, EBP active): on a
 *     multiplayer map a character cloaked for more than the last 120 ticks reads a super amount
 *     of 0 there, so Oni stops them (and their sparks) and starts them again after. That's game
 *     state, so it's the same on every PC: the cloaked player's own glow goes too. Only that read:
 *     the super amount itself, the overhealth and the Daodan shield's protection are untouched.
 *
 * invis_hidden(): cloaked as far as other players can tell, for the name tag and what they say
 * over their head (mp_names.c): cloak left, or invis_active with the alpha under 1. */
#include "arklite.h"

void *memcpy(void *, const void *, unsigned);
__declspec(dllimport) void *WINAPI GetProcAddress(HANDLE, const char *);

#define ONgGameState          (*(u8 **)0x5ece7c)
#define GS_PLAYER(gs)         (*(u8 **)((gs) + 0xac))
#define GS_TICK(gs)           (*(u32 *)((gs) + 0x144))
#define ONrCharacter_GetActive ((u8 *(ONI_FC *)(u8 *))0x4f1180)
#define CHR_INVIS_LEFT(c)     (*(u16 *)((c) + 0x1ba))
#define ACT_INVIS_ACTIVE(a)   (*(u8 *)((a) + 0x2815))
#define ACT_INVIS_ALPHA(a)    (*(float *)((a) + 0x2818))
#define INVIS_FADE_IN_TICKS   120      /* Invis_Update: the last ticks fade the body back in */
#define FLASH                 0.6f     /* 0x52a9d4: a blow's alpha */
#define FLASH_FADE            0.0085f  /* 0x52b33c: fading a tick */
#define SHIMMER_SCALE         0.22f    /* 0x52b300 = 255 * 0.22: the shimmer's opacity is that times 1 - alpha */

#define ONcDraw_BodyAlpha     0x4e1dcc   /* mov edx,[esi+0x2818] */
#define ONcDraw_ShadowAlpha   0x4e2317   /* mov edx,[esi+0x2818] */
#define ONcDraw_InvisRead     0x4e1fe2   /* mov al,[esi+0x2815] */
#define ONcDraw_InvisAmount   0x4e2030   /* fld [0x529508]; fsub [esi+0x2818] */
#define ONcMelee_Attacker     0x4e7882   /* mov al,[edi+0x2815] */
#define ONcThrow_Attacker     0x4e9c6d   /* mov al,[ebp+0x2815] */
#define ONcDraw_ShieldRead    0x4e1f40   /* mov al,[esi+0x2800] (shield_active) */
#define ONcUpdate_SuperRead   0x4df937   /* fld [ebx+0x1668] (super amount), then compared with 0 */
#define CHR_SUPER(c)          (*(float *)((c) + 0x1668))
#define ONcInvis_EndCompare   0x4ef69c   /* fld [0x529514] (0.0): compared with the alpha before clearing invis_active */

/* "12.5" and the like, for the knobs (no C library here) */
int float_parse(const char *s, float *out)
{
	float v = 0, scale = 1;
	int neg = 0, digits = 0, frac = 0;
	while (*s == ' ') s++;
	if (*s == '-' || *s == '+') neg = *s++ == '-';
	for (; (*s >= '0' && *s <= '9') || *s == '.' || *s == ','; s++) {
		if (*s == '.' || *s == ',') {
			if (frac) return 0;
			frac = 1;
			continue;
		}
		digits++;
		if (frac) v += (float)(*s - '0') * (scale *= 0.1f);
		else v = v * 10.0f + (float)(*s - '0');
	}
	while (*s == ' ') s++;
	if (!digits || *s) return 0;
	*out = neg ? -v : v;
	return 1;
}

void float_text(char *out, u32 size, float v, int decimals)
{
	char t[48];
	u32 n = 0, whole;
	float scale = 1;
	if (decimals > 6) decimals = 6;
	for (int d = 0; d < decimals; d++) scale *= 10.0f;
	if (v < 0) { t[n++] = '-'; v = -v; }
	v = v + 0.5f / scale;
	if (v > 1e9f) v = 1e9f;
	whole = (u32)v;
	fmt(t + n, "%u", whole);
	n = str_len(t);
	if (decimals > 0) {
		float f = v - (float)whole;
		t[n++] = '.';
		for (int d = 0; d < decimals; d++) {
			int digit;
			f *= 10.0f;
			digit = (int)f;
			if (digit > 9) digit = 9;
			t[n++] = (char)('0' + digit);
			f -= (float)digit;
		}
		t[n] = 0;
	}
	str_cpy(out, size, t);
}

/* The knobs: a table the O key's debug menu (fx_menu.c) changes as the game runs, kept in
 * Arklite.ini [cloak_fx] under the same keys (and "order"); the defaults are the "def" column.
 * Each belongs to one filter of the chain (see "The filter chain"). Water speed: the noise rolls
 * through 2 * pi * water_loop_radius / water_period_s cells a second (1.6, 20 s: ~0.5, a new swell
 * every 2 s); a bigger radius is faster and less repetitive, a shorter period faster and loops
 * sooner. Granularity: more cells, smaller swells. Lengths "of the screen's height" are fractions
 * (0.003: 3 px at 1050): Warp, Width and Split. The texture slide happens in the body's own drawing, so
 * always before every filter. */
static InvisFx g_fx[FX_COUNT] = {
	/* key                  label                         filter        def      min      max     decimals */
	/* (def: the values the user set in the menu, Arklite.ini [cloak_fx] of 2026-11-18; up to then
	 * opacity 0.5, loop 20, speed 1.6, warp 0.008 / 8 swells, slide 0.02 / 8, tones 3 / 0.08, outline
	 * 0.0025 pale blue 0.55 0.85 1 at 0.8, fringe 0.003 at 20 degrees, zoom 1.2) */
	{ "opacity",            "Opacity",                    FXF_OPACITY,  0.05f,   0.05f,   1.0f,   2 },
	{ "water_period_s",     "Loop length (s)",            FXF_WATER,    2.0f,    2.0f,    120.0f, 1 },
	{ "water_loop_radius",  "Speed",                      FXF_WATER,    1.55f,   0.0f,    6.0f,   2 },
	{ "water_vertex",       "Warp",                       FXF_WATER,    0.0012f, 0.0f,    0.05f,  4 },
	{ "water_vertex_cells", "Warp swells",                FXF_WATER,    40.0f,   0.5f,    40.0f,  1 },
	{ "water_texture",      "Texture slide",              FXF_WATER,    0.2f,    0.0f,    0.2f,   3 },
	{ "water_texture_cells","Texture swells",             FXF_WATER,    11.1f,   0.5f,    40.0f,  1 },
	{ "stencil",            "Strength",                   FXF_TONES,    1.0f,    0.0f,    1.0f,   2 },
	{ "stencil_levels",     "Tones",                      FXF_TONES,    12.0f,   1.0f,    12.0f,  0 },
	{ "stencil_soft",       "Edge softness",              FXF_TONES,    1.0f,    0.0f,    1.0f,   2 },
	{ "outline_px",         "Width",                      FXF_OUTLINE,  0.0004f, 0.0f,    0.02f,  4 },
	{ "outline_r",          "Red",                        FXF_OUTLINE,  1.0f,    0.0f,    1.0f,   2 },
	{ "outline_g",          "Green",                      FXF_OUTLINE,  1.0f,    0.0f,    1.0f,   2 },
	{ "outline_b",          "Blue",                       FXF_OUTLINE,  1.0f,    0.0f,    1.0f,   2 },
	{ "outline_alpha",      "Solidity",                   FXF_OUTLINE,  0.11f,   0.0f,    1.0f,   2 },
	{ "fringe_px",          "Split",                      FXF_FRINGE,   0.0027f, 0.0f,    0.03f,  4 },
	{ "fringe_angle",       "Angle (degrees)",            FXF_FRINGE,   -92.0f,  -180.0f, 180.0f, 0 },
	{ "zoom",               "Zoom (field of view)",       FXF_ZOOM,     1.18f,   1.0f,    3.0f,   2 },
	{ "zoom_pulse",         "Pulse",                      FXF_ZOOM,     0.0f,    0.0f,    1.0f,   2 },
};
static const char *const g_filter_name[FXF_COUNT] = { "Opacity", "Water", "Stencil tones", "Outline", "Color fringe", "Zoom" };
static const char *const g_filter_key[FXF_COUNT] = { "opacity", "water", "tones", "outline", "fringe", "zoom" };
/* The default order (the user's, 2026-11-18): the outline first, so the fading takes it too, then
 * the body faded, the zoomed view behind it showing through, the water waving body and view
 * together, the tones over all of it, and the fringes on top. (Up to then: opacity, tones, zoom,
 * water, outline, fringe.) */
static const int g_order_def[FXF_COUNT] = { FXF_OUTLINE, FXF_OPACITY, FXF_ZOOM, FXF_WATER, FXF_TONES, FXF_FRINGE };
static int g_order[FXF_COUNT] = { FXF_OUTLINE, FXF_OPACITY, FXF_ZOOM, FXF_WATER, FXF_TONES, FXF_FRINGE };

const char *invis_fx_filter_name(int f) { return f >= 0 && f < FXF_COUNT ? g_filter_name[f] : "?"; }
int invis_fx_order(int k) { return k >= 0 && k < FXF_COUNT ? g_order[k] : -1; }

/* The filter at place k goes up (dir -1, earlier) or down (1) the chain */
void invis_fx_move(int k, int dir)
{
	int t = k + dir, f;
	if (k < 0 || k >= FXF_COUNT || t < 0 || t >= FXF_COUNT) return;
	f = g_order[k];
	g_order[k] = g_order[t];
	g_order[t] = f;
}

static void order_load(const char *v)
{
	int got[FXF_COUNT], n = 0, seen = 0;
	while (*v && n < FXF_COUNT) {
		int f = -1;
		while (*v == ' ' || *v == ',') v++;
		for (int k = 0; k < FXF_COUNT; k++) {
			u32 len = str_len(g_filter_key[k]);
			if (str_ieq_n(v, g_filter_key[k], len) && (v[len] == ',' || v[len] == ' ' || !v[len])) f = k;
		}
		if (f < 0 || (seen >> f & 1)) return;   /* (a wrong list: the default order) */
		seen |= 1 << f;
		got[n++] = f;
		while (*v && *v != ',') v++;
	}
	for (int k = 0; k < FXF_COUNT; k++)   /* filters the list doesn't name (newer than it): where the default has them */
		if (!(seen >> g_order_def[k] & 1)) {
			int at = k < n ? k : n;
			for (int m = n; m > at; m--) got[m] = got[m - 1];
			got[at] = g_order_def[k];
			seen |= 1 << g_order_def[k];
			n++;
		}
	if (n != FXF_COUNT) return;
	for (int k = 0; k < FXF_COUNT; k++) g_order[k] = got[k];
}
#define FX(i)                 (g_fx[i].value)
static int g_fx_loaded, g_fx_preview;
/* The look of the cloaked player's own body (user, 2026-11-19): plain, its nearest surfaces at
 * PLAIN_OPACITY (as on 2026-11-14 before the filters), unless Arklite.ini [multiplayer]
 * cloak_preset=1 (no menu shows it) or the O menu is built in: then the filter chain, with g_fx */
#define PLAIN_OPACITY         0.3f   /* (user, 2026-11-19; 0.5 before) */
static int g_fx_chain;
static int g_single_player;   /* Arklite.ini [singleplayer] use_new_cloak_shader=1: self_view off the network too */
static float self_opacity(void) { return g_fx_chain ? FX(FX_OPACITY) : PLAIN_OPACITY; }

int invis_fx_count(void) { return FX_COUNT; }
InvisFx *invis_fx(int i) { return i >= 0 && i < FX_COUNT ? &g_fx[i] : 0; }
void invis_fx_preview(int on) { g_fx_preview = on; }

/* The O menu's Cloak me: this PC's player cloaked for CLOAK_ME_TICKS, as a phase cloak pickup does
 * (Invis_Update 0x4ef530 starts it from character +0x1ba). Game state, so only off the network:
 * in a network game every PC would have to do it in the same tick. 0 if it can't. */
#define CLOAK_ME_TICKS 1800   /* 30 s */
int invis_cloak_me(void)
{
	u8 *gs = ONgGameState, *c;
	if (net_active() || !gs || !(c = GS_PLAYER(gs))) return 0;
	CHR_INVIS_LEFT(c) = CLOAK_ME_TICKS;
	log_msg("invis: Cloak me: this PC's player is cloaked for %d s", CLOAK_ME_TICKS / 60);
	return 1;
}

void invis_fx_reset(void)
{
	for (int i = 0; i < FX_COUNT; i++) g_fx[i].value = g_fx[i].def;
	for (int k = 0; k < FXF_COUNT; k++) g_order[k] = g_order_def[k];
}

static void fx_load(void)
{
	char v[128];   /* (the order: 40 characters and more) */
	if (g_fx_loaded) return;
	g_fx_loaded = 1;
	invis_fx_reset();
	g_fx_chain = CLOAK_FX_MENU || GetPrivateProfileIntA("multiplayer", "cloak_preset", 0, ini_path()) != 0;
	g_single_player = GetPrivateProfileIntA("singleplayer", "use_new_cloak_shader", 0, ini_path()) != 0;
	if (g_single_player) log_msg("invis: [singleplayer] use_new_cloak_shader=1: the player's own cloaked look off the network too (single player, local Custom Game)");
	log_msg("invis: the cloaked player's own body: %s", g_fx_chain ? "the filter chain's preset (cloak_preset=1)" : "plain, at 30%");
	if (!CLOAK_FX_MENU) return;   /* (no menu: the defaults, whatever an older Arklite.ini kept) */
	for (int i = 0; i < FX_COUNT; i++) {
		float f;
		GetPrivateProfileStringA("cloak_fx", g_fx[i].key, "", v, sizeof v, ini_path());
		if (v[0] && float_parse(v, &f)) g_fx[i].value = f < g_fx[i].min ? g_fx[i].min : f > g_fx[i].max ? g_fx[i].max : f;
	}
	GetPrivateProfileStringA("cloak_fx", "order", "", v, sizeof v, ini_path());
	if (v[0]) order_load(v);
}

/* Arklite.ini [cloak_fx], and the log (a line to paste back as the defaults) */
void invis_fx_save(void)
{
	char v[32], line[1200];
	u32 n = 0;
	line[0] = 0;
	for (int i = 0; i < FX_COUNT; i++) {
		float_text(v, sizeof v, g_fx[i].value, g_fx[i].decimals + 1);
		WritePrivateProfileStringA("cloak_fx", g_fx[i].key, v, ini_path());
		fmt(line + n, "%s%s=%s", i ? " " : "", g_fx[i].key, v);
		n = str_len(line);
	}
	{
		char order[64];
		order[0] = 0;
		for (int k = 0; k < FXF_COUNT; k++) {
			if (k) str_cpy(order + str_len(order), sizeof order - str_len(order), ",");
			str_cpy(order + str_len(order), sizeof order - str_len(order), g_filter_key[g_order[k]]);
		}
		WritePrivateProfileStringA("cloak_fx", "order", order, ini_path());
		fmt(line + n, " order=%s", order);
	}
	log_msg("invis: cloak effect values saved in Arklite.ini [cloak_fx]: %s", line);
}

/* ---- blows ----------------------------------------------------------------------------------- */

#define FLASH_SLOTS 16
static struct { u8 *chr; u32 tick; } g_flash[FLASH_SLOTS];
static u32 g_next;

u32 invis_attacked(u8 *c, u8 *act)
{
	u8 *gs = ONgGameState;
	u32 i;
	if (!ACT_INVIS_ACTIVE(act)) return 0;
	if (gs) {
		for (i = 0; i < FLASH_SLOTS && g_flash[i].chr != c; i++) {}
		if (i == FLASH_SLOTS) i = g_next++ % FLASH_SLOTS;
		g_flash[i].chr = c;
		g_flash[i].tick = GS_TICK(gs);
	}
	return 1;
}

/* The flash Oni would give c's body for its last blow, 0..0.6 */
static float flash(u8 *c)
{
	u8 *gs = ONgGameState;
	if (!gs) return 0;
	for (u32 i = 0; i < FLASH_SLOTS; i++)
		if (g_flash[i].chr == c) {
			u32 now = GS_TICK(gs);
			if (now < g_flash[i].tick) return 0;   /* a new level */
			float f = FLASH - FLASH_FADE * (float)(now - g_flash[i].tick);
			return f > 0 ? f : 0;
		}
	return 0;
}

/* ---- drawing --------------------------------------------------------------------------------- */

/* Drawn as Oni does: off the network, the character this screen belongs to, or anyone on a
 * spectator's screen */
static int as_stock(u8 *c)
{
	u8 *gs = ONgGameState;
	return !net_active() || !gs || c == GS_PLAYER(gs) || net_watching() != -1;
}

/* The character this screen belongs to, in a network game (not on a spectator's screen); off the
 * network too with [singleplayer] use_new_cloak_shader=1 (user request, 2026-11-19: off by default) */
static int self_view(u8 *c)
{
	u8 *gs = ONgGameState;
	if (!gs || c != GS_PLAYER(gs)) return 0;
	return net_active() ? net_watching() == -1 : g_single_player;
}

int invis_hidden(u8 *c)
{
	u8 *a;
	if (!c || net_watching() != -1) return 0;   /* a spectator sees every name */
	if (CHR_INVIS_LEFT(c)) return 1;
	a = ONrCharacter_GetActive(c);
	return a && ACT_INVIS_ACTIVE(a) && ACT_INVIS_ALPHA(a) < 1.0f;
}

/* What others see of a cloak coming on: Oni's alpha a, but never back up (a blow lifts Oni's to
 * 0.6), so the lowest seen since the cloak came on. A character unseen for more than 120 ticks
 * (the cloak's end lasts that long) or on a new level starts again from a. Drawing only. */
#define FADE_SLOTS 16
static struct { u8 *chr; u32 tick; float low; } g_fade[FADE_SLOTS];
static u32 g_fade_next;

static float pickup_fade(u8 *c, float a)
{
	u8 *gs = ONgGameState;
	u32 now = gs ? GS_TICK(gs) : 0, i;
	for (i = 0; i < FADE_SLOTS && g_fade[i].chr != c; i++) {}
	if (i == FADE_SLOTS) {
		i = g_fade_next++ % FADE_SLOTS;
		g_fade[i].chr = c;
		g_fade[i].low = a;
	} else if (now < g_fade[i].tick || now - g_fade[i].tick > INVIS_FADE_IN_TICKS) g_fade[i].low = a;
	if (a < g_fade[i].low) g_fade[i].low = a;
	g_fade[i].tick = now;
	return g_fade[i].low;
}

static void pickup_fade_forget(u8 *c)
{
	for (u32 i = 0; i < FADE_SLOTS; i++)
		if (g_fade[i].chr == c) g_fade[i].chr = 0;
}

/* Body and shadow alpha (as float bits) */
u32 invis_body_alpha(u8 *act, u8 *c)
{
	u32 bits;
	float a = ACT_INVIS_ALPHA(act);
	if (ACT_INVIS_ACTIVE(act) && CHR_INVIS_LEFT(c) > INVIS_FADE_IN_TICKS && !as_stock(c)) a = pickup_fade(c, a);
	else pickup_fade_forget(c);
	if (ACT_INVIS_ACTIVE(act)) {
		if (a < self_opacity() && self_view(c)) a = self_opacity();
	}
	memcpy(&bits, &a, 4);
	return bits;
}

/* Shadow alpha (as float bits): as the body's, but for self_view Oni's own alpha, not floored at
 * the body's opacity, so the shadow fades out completely (1 to 0 in ~2 s after pickup) and back in
 * when the cloak wears off, as in stock Oni (user request, 2026-11-19) */
u32 invis_shadow_alpha(u8 *act, u8 *c)
{
	u32 bits;
	if (!ACT_INVIS_ACTIVE(act) || !self_view(c)) return invis_body_alpha(act, c);
	memcpy(&bits, &ACT_INVIS_ALPHA(act), 4);
	return bits;
}

/* Whether the shimmer is drawn */
u32 invis_shimmer_shown(u8 *act, u8 *c)
{
	if (!ACT_INVIS_ACTIVE(act) || self_view(c)) return 0;
	return as_stock(c) || flash(c) > 0;
}

/* The shield shell is drawn: shield_active, unless this screen hides the cloaked body */
u32 invis_shield_shown(u8 *act, u8 *c)
{
	if (!*(u8 *)(act + 0x2800)) return 0;
	return !(ACT_INVIS_ACTIVE(act) && CHR_INVIS_LEFT(c) > INVIS_FADE_IN_TICKS && !as_stock(c));
}

/* The super amount the super particles go by: none while cloaked on a multiplayer map */
float invis_super_for_particles(u8 *c, u8 *act)
{
	if (multiplayer_map() && ACT_INVIS_ACTIVE(act) && CHR_INVIS_LEFT(c) > INVIS_FADE_IN_TICKS) return 0.0f;
	return CHR_SUPER(c);
}

/* What the shimmer's opacity is 0.22 times: 1 - alpha as Oni has it, or the blow's flash / 0.22 */
float invis_shimmer_amount(u8 *act, u8 *c)
{
	return as_stock(c) ? 1.0f - ACT_INVIS_ALPHA(act) : flash(c) / SHIMMER_SCALE;
}

/* ---- The see-through body on its own player's screen ------------------------------------------ *
 * Oni draws a body with alpha in (0, 1) (ONiCharacter_Display_Body 0x4e1d70, 0x4e1e7e..0x4e1ec9)
 * with draw state 0x10 (FrameBufferBlendWithConstantAlpha) on, z-write off, the geometry state's
 * submit mode 1 (SortAlphaTris) and its alpha at alpha * 255: its triangles join the frame's
 * alpha-sorted list, drawn at the frame's end, every one of them, so the far side of the body
 * (its front faces: knee pads and chest seen through the back) showed through the near side.
 * Retail Oni.exe has no GL_CONSTANT_ALPHA anywhere (OniFoxed's GL_ENABLE_ALPHA_FADE_CODE is
 * compiled out), so they're blended GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA with the texture's alpha
 * times the alpha: and a character texture's alpha is its shine mask (with an environment map
 * the base texture is drawn over the reflection by its alpha, GL_DECAL at 0x40da4e or the NV
 * register combiners), so the shiny parts, the white ones on most characters, all but vanished.
 *
 * For self_view (user request, 2026-11-14) the body draw (TRrBody_Draw 0x427630, called at
 * 0x4e1f3b; fastcall, 6 more arguments, ret 0x18) only notes its arguments, and the body is drawn
 * once the rest of the scene is (after WPrPowerup_Display 0x51c2b0, called at 0x4fba43: the
 * characters, objects, weapons, decals, and particles' submission are done), twice, at once (submit
 * mode 0; Oni's GL engine draws every primitive at once with glBegin/glEnd, 0x405c16..0x407a0f):
 *   1. depth only (glColorMask off, z-write on): the body's nearest surfaces in the depth buffer;
 *   2. colors (z-write off; Oni's depth compare is GL_LEQUAL, 0x40cee8), so only those nearest
 *      surfaces are drawn, each once; blended GL_CONSTANT_ALPHA / GL_ONE_MINUS_CONSTANT_ALPHA at
 *      the alpha (glBlendColor), whatever the texture's alpha. Below 255 Oni draws no reflection
 *      (the environment map needs the constant color's alpha at 0xff), so this is the base
 *      texture's colors, lit, at that alpha. Oni sets its blend factors through its GL table (*0x560604
 *      +0x1c, glBlendFunc; the last ones it set in 0x530954 / 0x56061c), so that slot answers with
 *      the constant-alpha factors for this pass and Oni's own are set again after it.
 * Drawn later because its depth (pass 1) would hide whatever is drawn after it behind the body,
 * an opponent ahead of the player most of all; now only the alpha-sorted list (particles, drawn at
 * the frame's end, after the HUD) can be behind it unseen. Back faces aren't drawn: Oni's geometry
 * engine leaves them out itself (GL culling on top cut real front faces out: the triangles it
 * submits don't all wind the same way on screen). The body's textures are set again first
 * (TRrBody_SetMaps 0x427da0 with its class's list, ONCC +0xc38: classes may share a body, and
 * another character drawn after it set its own). The note is dropped at the next character draw
 * pass (0x4e1540, called at 0x4fb922), so it's never drawn in another frame. Drawing only. Without
 * glBlendColor (OpenGL 1.4) the body is drawn as in Oni. */

#define TRrBody_Draw          ((void (ONI_FC *)(void *, void *, void *, u32, u32, u32, u32, u32))0x427630)
#define TRrBody_SetMaps       ((void (ONI_FC *)(void *, void *))0x427da0)
#define ONrGameState_DisplayCharacters ((void (*)(void))0x4e1540)
#define WPrPowerup_Display    ((void (*)(void))0x51c2b0)
#define ONcDraw_BodyDrawCall  0x4e1f3b
#define ONcDisplay_CharactersCall 0x4fb922
#define ONcDisplay_PowerupsCall   0x4fba43
#define CHR_CLASS(c)          (*(u8 **)((c) + 0xc))
#define ONCC_TEXTURES(cc)     (*(void **)((cc) + 0xc38))
#define M3rDraw_State_SetInt  ((void (ONI_FC *)(u32, u32))0x41e170)
#define M3rGeom_State_Set     ((void (ONI_FC *)(u32, u32))0x41e720)
#define M3rGeom_State_Commit  ((void (ONI_FC *)(void))0x41e870)
#define M3rGeom_State_Push    ((void (ONI_FC *)(void))0x41e750)   /* the draw state's too (0x41e1f0) */
#define M3rGeom_State_Pop     ((void (ONI_FC *)(void))0x41e7f0)
#define DRAW_ZWRITE           4
#define DRAW_CONSTANT_BLEND   0x10
#define GEOM_SUBMIT_MODE      4        /* 0 at once, 1 alpha-sorted */
#define GEOM_ALPHA            5
#define GLgFuncs              (*(u8 **)0x560604)
#define GL_SLOT_BLEND_FUNC    0x1c
#define ONgGLBlendSrc         (*(u32 *)0x530954)
#define ONgGLBlendDst         (*(u32 *)0x56061c)
#define GL_CONSTANT_ALPHA     0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004

typedef void (WINAPI *GlBlendFunc)(u32, u32);
static struct {
	int loaded, ok;
	void (WINAPI *ColorMask)(u8, u8, u8, u8);
	void (WINAPI *BlendColor)(float, float, float, float);
	GlBlendFunc BlendFunc;
} gl;
static struct {
	int pending;
	void *body, *extra, *matrices, *textures;
	u32 parts, flash, a, b, c;
	float alpha;
	u8 *chr;
} g_ghost;
u8 *g_draw_act, *g_draw_chr;   /* the body_draw_call stub's ESI and EBP */
void *g_body_draw;

static int load_gl(void)
{
	HANDLE m;
	void *(WINAPI *get)(const char *);

	if (gl.loaded) return gl.ok;
	gl.loaded = 1;
	if (!(m = GetModuleHandleA("opengl32.dll"))) return 0;
	gl.ColorMask = GetProcAddress(m, "glColorMask");
	gl.BlendFunc = GetProcAddress(m, "glBlendFunc");
	get = GetProcAddress(m, "wglGetProcAddress");   /* (asked while Oni's context is current: we're in its draw) */
	if (get && !(gl.BlendColor = get("glBlendColor"))) gl.BlendColor = get("glBlendColorEXT");
	gl.ok = gl.ColorMask && gl.BlendFunc && gl.BlendColor && GLgFuncs;
	log_msg("invis: %s", gl.ok ? "the cloaked player's own body is drawn as its nearest surfaces, its colors at the cloak's opacity"
	                           : "OpenGL 1.4's glBlendColor is missing: the cloaked player's own body is drawn as in Oni");
	return gl.ok;
}

/* What Oni's glBlendFunc slot does while a ghost pass runs: these factors, whatever Oni asks */
static u32 g_force_src = GL_CONSTANT_ALPHA, g_force_dst = GL_ONE_MINUS_CONSTANT_ALPHA;
static void WINAPI blend_constant(u32 src, u32 dst)
{
	gl.BlendFunc(g_force_src, g_force_dst);
}

static void ONI_FC body_draw(void *body, void *extra, void *matrices, u32 parts, u32 flash, u32 a, u32 b, u32 c)
{
	u8 *act = g_draw_act, *chr = g_draw_chr;
	float alpha;
	u32 bits;

	int preview = g_fx_preview && act && chr && ONgGameState && chr == GS_PLAYER(ONgGameState) && net_watching() == -1;

	if (!act || !chr || !(preview || (ACT_INVIS_ACTIVE(act) && self_view(chr))) || !load_gl()) {
		TRrBody_Draw(body, extra, matrices, parts, flash, a, b, c);
		return;
	}
	bits = invis_body_alpha(act, chr);
	memcpy(&alpha, &bits, 4);
	if (preview && (!ACT_INVIS_ACTIVE(act) || alpha >= 1.0f || alpha < FX(FX_OPACITY))) alpha = FX(FX_OPACITY);   /* the menu's look, cloaked or not */
	if (!(alpha > 0.0f && alpha < 1.0f)) {   /* (Oni drew it opaque: nothing set up for alpha) */
		TRrBody_Draw(body, extra, matrices, parts, flash, a, b, c);
		return;
	}
	g_ghost.body = body;
	g_ghost.extra = extra;
	g_ghost.matrices = matrices;
	g_ghost.textures = ONCC_TEXTURES(CHR_CLASS(chr));
	g_ghost.parts = parts;
	g_ghost.flash = flash;
	g_ghost.a = a;
	g_ghost.b = b;
	g_ghost.c = c;
	g_ghost.alpha = alpha;
	g_ghost.chr = chr;
	g_ghost.pending = 1;
}

/* ---- The filter chain: water, stencil tones, outline, color fringe, opacity (user requests,
 * 2026-11-14) ------------------------------------------------------------------------------------- *
 * Only with Arklite.ini [multiplayer] cloak_preset=1 (or the O menu built in); otherwise the body is
 * the plain ghost at 30% (ghost_plain), as before the filters (user, 2026-11-19).
 * The body is drawn into a layer of its own (a framebuffer object the size of Oni's viewport, with
 * a color and a depth texture), and the filters run over that layer one after another in the order
 * the O menu sets (g_order), each a full-screen pass from one color texture into the other; then
 * the layer goes on screen. So each filter works on what the ones before it made: an outline
 * after the water waves with it, fringes after the outline split it too, opacity last fades
 * everything, opacity first leaves the later outline and fringes at their own strength.
 *   - Layer: Oni's screen points are pixels already projected (glVertex3f x, y, z depth;
 *     glTexCoord4f(u / w, v / w, 0, 1 / w), 0x405c16..), drawn with Oni's own projection into the
 *     layer with the same viewport size, so the body lands where it would on screen. The body
 *     program divides the texture coordinates by w (as fixed-function texturing does), slides
 *     them by the water's texture noise over the surface (water_texture: always, before every
 *     filter, being part of the body's surface), multiplies by the lit vertex color, adds Oni's
 *     linear fog (GL_FOG_MODE GL_LINEAR, 0x40c748) by the eye distance as fixed function takes it
 *     when GL_FOG is on, and writes it opaque, z-tested within the layer, so only the body's
 *     nearest surfaces are in it (Oni's geometry engine already leaves back faces out).
 *     Premultiplied alpha from here on.
 *   - Opacity: the layer times the alpha (the knob, or the cloak's own when it's higher: a
 *     blow's 0.6, the fade at the end).
 *   - Water: the layer read through a turbulence, two octaves of smooth 3D value noise (its own
 *     sine hash) whose time axis goes round a circle, so it loops seamlessly every
 *     water_period_s; moved by up to water_vertex of the screen's height, water_vertex_cells
 *     swells to the height.
 *   - Stencil tones: the color's brightness cut to stencil_levels flat tones (soft-edged by
 *     stencil_soft so they don't crawl), the hue kept, mixed in by stencil.
 *   - Outline: where the layer has nothing but something within outline_px (8 directions), the
 *     outline color at outline_alpha, behind what's there.
 *   - Color fringe: red read fringe_px one way along fringe_angle, blue the other, green in place.
 *   - Zoom: what's behind the body, seen through it as through another field of view: before the
 *     body is drawn, the screen (the scene behind it, the alpha-sorted list not drawn yet) is
 *     copied from the back buffer into a texture (glCopyTexSubImage2D); the filter fills what the layer leaves
 *     see-through inside the body's silhouette with that copy scaled zoom times about the
 *     viewport's center (where a field of view narrows to), so the view behind the body is
 *     zoomed in (a narrower field of view) there and only there; 1 is none. The layer becomes opaque inside the
 *     silhouette (the zoomed view takes the place of the real one), so a filter after it (water,
 *     outline, fringe, opacity) acts on the zoomed view too. zoom_pulse makes it breathe, three
 *     times a water loop.
 *   - On screen: blended as premultiplied alpha, z-tested against the scene (z-write off), at the
 *     nearest depth of the body within reach of everything that moved it (center and 16 taps),
 *     so walls in front hide it and whatever is behind still shows through, particles included.
 * Oni's GL state is put back after: its blend factors (the glBlendFunc slot answers our factors
 * meanwhile), its texture on unit 0 (its cache 0x560614 made to bind again), viewport, scissor,
 * framebuffer 0. No GLSL or framebuffer objects (OpenGL 2.0, ARB/EXT framebuffer object), or a
 * shader that doesn't compile: logged once, and the ghost is drawn plain: its nearest surfaces at
 * the opacity (a depth pass, then the colors blended GL_CONSTANT_ALPHA). */

#define M3rScreen_Height      ((u16 (ONI_FC *)(void))0x41e130)
#define GLgDriver             (*(u8 **)0x560600)
#define GLgBound              (*(u8 **)0x560614)   /* the texture Oni bound last on unit 0 */
#define GL_SLOT_ACTIVE_TEXTURE 0x568              /* glActiveTextureARB in Oni's table (with the driver's multitexture) */
#define GL_FOG                0x0b60
#define GL_SCISSOR_TEST       0x0c11
#define GL_VIEWPORT           0x0ba2
#define GL_TEXTURE_2D         0x0de1
#define GL_TEXTURE0           0x84c0
#define GL_TEXTURE1           0x84c1
#define GL_RGBA               0x1908
#define GL_RGBA8              0x8058
#define GL_UNSIGNED_BYTE      0x1401
#define GL_UNSIGNED_INT       0x1405
#define GL_DEPTH_COMPONENT    0x1902
#define GL_DEPTH_COMPONENT24  0x81a6
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S     0x2802
#define GL_TEXTURE_WRAP_T     0x2803
#define GL_LINEAR             0x2601
#define GL_NEAREST            0x2600
#define GL_CLAMP_TO_EDGE      0x812f
#define GL_COLOR_BUFFER_BIT   0x4000
#define GL_DEPTH_BUFFER_BIT   0x0100
#define GL_QUADS              0x0007
#define GL_ONE                1
#define GL_ZERO               0
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_FRAMEBUFFER        0x8d40
#define GL_COLOR_ATTACHMENT0  0x8ce0
#define GL_DEPTH_ATTACHMENT   0x8d00
#define GL_FRAMEBUFFER_COMPLETE 0x8cd5
#define GL_VERTEX_SHADER      0x8b31
#define GL_FRAGMENT_SHADER    0x8b30
#define GL_COMPILE_STATUS     0x8b81
#define GL_LINK_STATUS        0x8b82
enum { K_OPACITY = 1, K_WATER, K_TONES, K_OUTLINE, K_FRINGE, K_SCREEN, K_ZOOM, K_SHELL };   /* the filter program's u_kind */

__declspec(dllimport) u32 WINAPI GetTickCount(void);

static const char g_noise_src[] =
	"#version 110\n"
	"uniform float u_angle;\n"
	"float hash(vec3 p) { return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }\n"
	"float noise(vec3 p) {\n"
	"	vec3 i = floor(p), f = fract(p);\n"
	"	f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
	"	return mix(mix(mix(hash(i), hash(i + vec3(1.0, 0.0, 0.0)), f.x),\n"
	"	               mix(hash(i + vec3(0.0, 1.0, 0.0)), hash(i + vec3(1.0, 1.0, 0.0)), f.x), f.y),\n"
	"	           mix(mix(hash(i + vec3(0.0, 0.0, 1.0)), hash(i + vec3(1.0, 0.0, 1.0)), f.x),\n"
	"	               mix(hash(i + vec3(0.0, 1.0, 1.0)), hash(i + vec3(1.0, 1.0, 1.0)), f.x), f.y), f.z);\n"
	"}\n"
	"float turbulence(vec3 p) { return noise(p) * 0.65 + noise(p * 2.03 + vec3(17.0, 5.0, 3.0)) * 0.35; }\n"
	"vec2 flow(vec2 p, float r) {\n"
	"	vec3 a = vec3(p, 0.0) + vec3(0.0, r * cos(u_angle), r * sin(u_angle));\n"
	"	return vec2(turbulence(a), turbulence(a + vec3(31.4, 47.2, 11.9))) * 2.0 - 1.0;\n"
	"}\n";

/* The body, into the layer */
static const char g_body_vs[] =
	"varying vec4 v_tc;\n"
	"void main() {\n"
	"	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
	"	gl_FrontColor = gl_Color;\n"
	"	v_tc = gl_MultiTexCoord0;\n"
	"	gl_FogFragCoord = abs((gl_ModelViewMatrix * gl_Vertex).z);\n"
	"}\n";
static const char g_body_fs[] =
	"uniform sampler2D u_tex;\n"
	"uniform float u_tamp, u_tcells, u_radius, u_fog;\n"
	"varying vec4 v_tc;\n"
	"void main() {\n"
	"	vec2 uv = v_tc.xy / v_tc.w;\n"
	"	uv += flow(uv * u_tcells + vec2(7.3, 2.9), u_radius) * u_tamp;\n"
	"	vec3 c = texture2D(u_tex, uv).rgb * gl_Color.rgb;\n"
	"	if (u_fog > 0.5) c = mix(gl_Fog.color.rgb, c, clamp((gl_Fog.end - gl_FogFragCoord) * gl_Fog.scale, 0.0, 1.0));\n"
	"	gl_FragColor = vec4(c, 1.0);\n"
	"}\n";

/* The filters and the last pass to the screen: a quad over the viewport (gl_Vertex in -1..1) */
static const char g_filter_vs[] =
	"void main() { gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0); }\n";
static const char g_filter_fs[] =
	"uniform sampler2D u_src, u_depth;\n"
	"uniform int u_kind;\n"
	"uniform vec4 u_vp;\n"            /* the viewport: x, y, width, height */
	"uniform float u_a, u_b, u_c, u_radius;\n"
	"uniform vec4 u_color;\n"
	"uniform sampler2D u_bg;\n"       /* zoom: the screen before the body */
	"vec4 at(vec2 uv) { return texture2D(u_src, uv); }\n"
	"float cover(float a) { return clamp(a * 20.0, 0.0, 1.0); }\n"
	"void main() {\n"
	"	vec2 px = 1.0 / u_vp.zw, uv = (gl_FragCoord.xy - u_vp.xy) * px;\n"
	"	vec4 c = at(uv);\n"
	"	if (u_kind == 1) {\n"                       /* opacity: u_a */
	"		c *= u_a;\n"
	"	} else if (u_kind == 2) {\n"                /* water: u_a pixels, u_b swells to the height */
	"		vec2 p = (gl_FragCoord.xy - u_vp.xy) / u_vp.w * u_b;\n"
	"		c = at(uv + flow(p, u_radius) * u_a * px);\n"
	"	} else if (u_kind == 3) {\n"                /* tones: u_a strength, u_b levels, u_c softness */
	"		if (c.a > 0.004) {\n"
	"			vec3 k = c.rgb / c.a;\n"
	"			float l = dot(k, vec3(0.299, 0.587, 0.114)), q = l * u_b;\n"
	"			float t = (floor(q) + smoothstep(1.0 - u_c, 1.0, fract(q)) + 0.5) / u_b;\n"
	"			k = mix(k, clamp(k * (min(t, 1.0) / max(l, 0.004)), 0.0, 1.0), u_a);\n"
	"			c.rgb = k * c.a;\n"
	"		}\n"
	"	} else if (u_kind == 4) {\n"                /* outline: u_a pixels, u_color (alpha: solidity) */
	"		float m = 0.0, r = u_a, d = 0.7071 * u_a;\n"
	"		m = max(m, cover(at(uv + vec2(r, 0.0) * px).a)); m = max(m, cover(at(uv - vec2(r, 0.0) * px).a));\n"
	"		m = max(m, cover(at(uv + vec2(0.0, r) * px).a)); m = max(m, cover(at(uv - vec2(0.0, r) * px).a));\n"
	"		m = max(m, cover(at(uv + vec2(d, d) * px).a)); m = max(m, cover(at(uv - vec2(d, d) * px).a));\n"
	"		m = max(m, cover(at(uv + vec2(d, -d) * px).a)); m = max(m, cover(at(uv - vec2(d, -d) * px).a));\n"
	"		float o = clamp(m - cover(c.a), 0.0, 1.0) * u_color.a;\n"
	"		c += (1.0 - c.a) * vec4(u_color.rgb * o, o);\n"
	"	} else if (u_kind == 5) {\n"                /* fringe: u_a pixels at angle u_b (radians; y down on screen) */
	"		vec2 s = vec2(cos(u_b), -sin(u_b)) * u_a * px;\n"
	"		vec4 r = at(uv + s), b = at(uv - s);\n"
	"		c = vec4(r.r, c.g, b.b, max(c.a, max(r.a, b.a)));\n"
	"	} else if (u_kind == 7) {\n"                /* zoom: the view behind the body, u_b times, in its see-through part */
	"		float m = cover(c.a);\n"
	"		if (m > 0.0) {\n"
	"			vec3 g = texture2D(u_bg, 0.5 + (uv - 0.5) / u_b).rgb;\n"
	"			c = vec4(c.rgb + (1.0 - c.a) * m * g, c.a + (1.0 - c.a) * m);\n"
	"		}\n"
	"	} else if (u_kind == 8) {\n"                /* a force field's shell to the screen, at its own depth (added) */
	"		float z = texture2D(u_depth, uv).r;\n"
	"		if (z >= 1.0) discard;\n"
	"		gl_FragDepth = z;\n"
	"	} else if (u_kind == 6) {\n"                /* to the screen, at the body's nearest depth within u_a pixels */
	"		if (c.a < 0.004) discard;\n"
	"		float z = texture2D(u_depth, uv).r;\n"
	"		for (int i = 0; i < 8; i++) {\n"
	"			float t = float(i) * 0.785398;\n"
	"			vec2 o = vec2(cos(t), sin(t)) * px;\n"
	"			z = min(z, texture2D(u_depth, uv + o * u_a).r);\n"
	"			z = min(z, texture2D(u_depth, uv + o * u_a * 0.5).r);\n"
	"		}\n"
	"		gl_FragDepth = z;\n"
	"	}\n"
	"	gl_FragColor = c;\n"
	"}\n";

/* A force field's shell, into the layer: what Oni's additive blend would add (texture times the
 * lit color, times the texture's and the part's alpha), opaque, so the nearest surface wins */
static const char g_shell_fs[] =
	"uniform sampler2D u_tex;\n"
	"varying vec4 v_tc;\n"
	"void main() {\n"
	"	vec4 t = texture2D(u_tex, v_tc.xy / v_tc.w);\n"
	"	gl_FragColor = vec4(t.rgb * gl_Color.rgb * (t.a * gl_Color.a), 1.0);\n"
	"}\n";

static struct {
	int tried;
	u32 body, filter, shell; /* the programs (0: none) */
	u32 fbo, tex[2], depth;  /* the layer: two color textures (ping-pong) and its depth */
	u32 bg;                  /* the screen before the body (Zoom) */
	int w, h;                /* their size */
	int b_angle, b_tex, b_tamp, b_tcells, b_radius, b_fog, s_tex;
	int f_angle, f_src, f_depth, f_bg, f_kind, f_vp, f_a, f_b, f_c, f_radius, f_color;
	u32 (WINAPI *CreateShader)(u32);
	void (WINAPI *ShaderSource)(u32, int, const char *const *, const int *);
	void (WINAPI *CompileShader)(u32);
	void (WINAPI *GetShaderiv)(u32, u32, int *);
	void (WINAPI *GetShaderInfoLog)(u32, int, int *, char *);
	u32 (WINAPI *CreateProgram)(void);
	void (WINAPI *AttachShader)(u32, u32);
	void (WINAPI *LinkProgram)(u32);
	void (WINAPI *GetProgramiv)(u32, u32, int *);
	void (WINAPI *GetProgramInfoLog)(u32, int, int *, char *);
	void (WINAPI *UseProgram)(u32);
	int (WINAPI *GetUniformLocation)(u32, const char *);
	void (WINAPI *Uniform1f)(int, float);
	void (WINAPI *Uniform4f)(int, float, float, float, float);
	void (WINAPI *Uniform2f)(int, float, float);
	void (WINAPI *Uniform1i)(int, int);
	void (WINAPI *GenFramebuffers)(int, u32 *);
	void (WINAPI *BindFramebuffer)(u32, u32);
	void (WINAPI *FramebufferTexture2D)(u32, u32, u32, u32, int);
	u32 (WINAPI *CheckFramebufferStatus)(u32);
	void (WINAPI *ActiveTexture)(u32);
	u8 (WINAPI *IsEnabled)(u32);
	void (WINAPI *Enable)(u32);
	void (WINAPI *Disable)(u32);
	void (WINAPI *GetIntegerv)(u32, int *);
	void (WINAPI *Viewport)(int, int, int, int);
	void (WINAPI *ClearColor)(float, float, float, float);
	void (WINAPI *Clear)(u32);
	void (WINAPI *GenTextures)(int, u32 *);
	void (WINAPI *BindTexture)(u32, u32);
	void (WINAPI *TexImage2D)(u32, int, int, int, int, int, u32, u32, const void *);
	void (WINAPI *TexParameteri)(u32, u32, int);
	void (WINAPI *CopyTexSubImage2D)(u32, int, int, int, int, int, int, int);
	void (WINAPI *ReadBuffer)(u32);
	void (WINAPI *Begin)(u32);
	void (WINAPI *End)(void);
	void (WINAPI *Vertex2f)(float, float);
} fx;

static u32 fx_shader(u32 type, const char *body, const char *what)
{
	const char *src[2] = { g_noise_src, body };
	char info[512];
	int ok = 0, n = 0;
	u32 sh = fx.CreateShader(type);

	if (!sh) return 0;
	fx.ShaderSource(sh, 2, src, 0);
	fx.CompileShader(sh);
	fx.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (ok) return sh;
	info[0] = 0;
	fx.GetShaderInfoLog(sh, sizeof info, &n, info);
	info[sizeof info - 1] = 0;
	log_msg("invis: the cloak effect's %s shader didn't compile: %s", what, info);
	return 0;
}

static u32 fx_program(const char *vs_src, const char *fs_src, const char *what)
{
	u32 vs, fs, prog;
	int ok = 0, n = 0;
	char info[512];

	if (!(vs = fx_shader(GL_VERTEX_SHADER, vs_src, what)) || !(fs = fx_shader(GL_FRAGMENT_SHADER, fs_src, what)) ||
	    !(prog = fx.CreateProgram()))
		return 0;
	fx.AttachShader(prog, vs);
	fx.AttachShader(prog, fs);
	fx.LinkProgram(prog);
	fx.GetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (ok) return prog;
	info[0] = 0;
	fx.GetProgramInfoLog(prog, sizeof info, &n, info);
	info[sizeof info - 1] = 0;
	log_msg("invis: the cloak effect's %s program didn't link: %s", what, info);
	return 0;
}

/* The functions and programs, the first time (Oni's context is current: we're in its draw); 1 ready */
static int fx_ready(void)
{
	HANDLE m;
	void *(WINAPI *get)(const char *);

	if (fx.tried) return fx.body && fx.filter;
	fx.tried = 1;
	if (!(m = GetModuleHandleA("opengl32.dll")) || !(get = GetProcAddress(m, "wglGetProcAddress"))) return 0;
	fx.CreateShader = get("glCreateShader");
	fx.ShaderSource = get("glShaderSource");
	fx.CompileShader = get("glCompileShader");
	fx.GetShaderiv = get("glGetShaderiv");
	fx.GetShaderInfoLog = get("glGetShaderInfoLog");
	fx.CreateProgram = get("glCreateProgram");
	fx.AttachShader = get("glAttachShader");
	fx.LinkProgram = get("glLinkProgram");
	fx.GetProgramiv = get("glGetProgramiv");
	fx.GetProgramInfoLog = get("glGetProgramInfoLog");
	fx.UseProgram = get("glUseProgram");
	fx.GetUniformLocation = get("glGetUniformLocation");
	fx.Uniform1f = get("glUniform1f");
	fx.Uniform4f = get("glUniform4f");
	fx.Uniform2f = get("glUniform2f");
	fx.Uniform1i = get("glUniform1i");
	fx.ActiveTexture = get("glActiveTexture");
	if (!(fx.GenFramebuffers = get("glGenFramebuffers"))) {   /* (ARB_framebuffer_object, else EXT) */
		fx.GenFramebuffers = get("glGenFramebuffersEXT");
		fx.BindFramebuffer = get("glBindFramebufferEXT");
		fx.FramebufferTexture2D = get("glFramebufferTexture2DEXT");
		fx.CheckFramebufferStatus = get("glCheckFramebufferStatusEXT");
	} else {
		fx.BindFramebuffer = get("glBindFramebuffer");
		fx.FramebufferTexture2D = get("glFramebufferTexture2D");
		fx.CheckFramebufferStatus = get("glCheckFramebufferStatus");
	}
	fx.IsEnabled = GetProcAddress(m, "glIsEnabled");
	fx.Enable = GetProcAddress(m, "glEnable");
	fx.Disable = GetProcAddress(m, "glDisable");
	fx.GetIntegerv = GetProcAddress(m, "glGetIntegerv");
	fx.Viewport = GetProcAddress(m, "glViewport");
	fx.ClearColor = GetProcAddress(m, "glClearColor");
	fx.Clear = GetProcAddress(m, "glClear");
	fx.GenTextures = GetProcAddress(m, "glGenTextures");
	fx.BindTexture = GetProcAddress(m, "glBindTexture");
	fx.TexImage2D = GetProcAddress(m, "glTexImage2D");
	fx.TexParameteri = GetProcAddress(m, "glTexParameteri");
	fx.CopyTexSubImage2D = GetProcAddress(m, "glCopyTexSubImage2D");
	fx.ReadBuffer = GetProcAddress(m, "glReadBuffer");
	fx.Begin = GetProcAddress(m, "glBegin");
	fx.End = GetProcAddress(m, "glEnd");
	fx.Vertex2f = GetProcAddress(m, "glVertex2f");
	if (!fx.CreateShader || !fx.ShaderSource || !fx.CompileShader || !fx.GetShaderiv || !fx.GetShaderInfoLog ||
	    !fx.CreateProgram || !fx.AttachShader || !fx.LinkProgram || !fx.GetProgramiv || !fx.GetProgramInfoLog ||
	    !fx.UseProgram || !fx.GetUniformLocation || !fx.Uniform1f || !fx.Uniform4f || !fx.Uniform2f || !fx.Uniform1i || !fx.ActiveTexture ||
	    !fx.GenFramebuffers || !fx.BindFramebuffer || !fx.FramebufferTexture2D || !fx.CheckFramebufferStatus ||
	    !fx.IsEnabled || !fx.Enable || !fx.Disable || !fx.GetIntegerv || !fx.Viewport || !fx.ClearColor || !fx.Clear ||
	    !fx.GenTextures || !fx.BindTexture || !fx.TexImage2D || !fx.TexParameteri || !fx.CopyTexSubImage2D || !fx.ReadBuffer || !fx.Begin || !fx.End || !fx.Vertex2f) {
		log_msg("invis: no OpenGL shaders or framebuffer objects here: the cloaked player's own body is drawn plain, at its opacity");
		return 0;
	}
	if (!(fx.body = fx_program(g_body_vs, g_body_fs, "body")) || !(fx.filter = fx_program(g_filter_vs, g_filter_fs, "filter")) ||
	    !(fx.shell = fx_program(g_body_vs, g_shell_fs, "force field"))) {
		fx.body = fx.filter = fx.shell = 0;
		return 0;
	}
	fx.s_tex = fx.GetUniformLocation(fx.shell, "u_tex");
	fx.b_angle = fx.GetUniformLocation(fx.body, "u_angle");
	fx.b_tex = fx.GetUniformLocation(fx.body, "u_tex");
	fx.b_tamp = fx.GetUniformLocation(fx.body, "u_tamp");
	fx.b_tcells = fx.GetUniformLocation(fx.body, "u_tcells");
	fx.b_radius = fx.GetUniformLocation(fx.body, "u_radius");
	fx.b_fog = fx.GetUniformLocation(fx.body, "u_fog");
	fx.f_angle = fx.GetUniformLocation(fx.filter, "u_angle");
	fx.f_src = fx.GetUniformLocation(fx.filter, "u_src");
	fx.f_depth = fx.GetUniformLocation(fx.filter, "u_depth");
	fx.f_kind = fx.GetUniformLocation(fx.filter, "u_kind");
	fx.f_vp = fx.GetUniformLocation(fx.filter, "u_vp");
	fx.f_a = fx.GetUniformLocation(fx.filter, "u_a");
	fx.f_b = fx.GetUniformLocation(fx.filter, "u_b");
	fx.f_c = fx.GetUniformLocation(fx.filter, "u_c");
	fx.f_radius = fx.GetUniformLocation(fx.filter, "u_radius");
	fx.f_color = fx.GetUniformLocation(fx.filter, "u_color");
	fx.f_bg = fx.GetUniformLocation(fx.filter, "u_bg");
	log_msg("invis: OpenGL shaders and framebuffer objects ready (force fields%s)", g_fx_chain ? ", the cloak's filter chain" : "");
	return 1;
}

static void texture_setup(u32 t, int w, int h, int depth)
{
	fx.BindTexture(GL_TEXTURE_2D, t);
	fx.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, depth ? GL_NEAREST : GL_LINEAR);
	fx.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, depth ? GL_NEAREST : GL_LINEAR);
	fx.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	fx.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (depth) fx.TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 0);
	else fx.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
}

/* The layer at the viewport's size (made again when it changes); 1 ready */
static int layer_ready(int w, int h)
{
	if (fx.fbo && fx.w == w && fx.h == h) return 1;
	if (w < 16 || h < 16 || w > 16384 || h > 16384) return 0;
	if (!fx.fbo) {
		fx.GenFramebuffers(1, &fx.fbo);
		fx.GenTextures(2, fx.tex);
		fx.GenTextures(1, &fx.depth);
		fx.GenTextures(1, &fx.bg);
	}
	texture_setup(fx.tex[0], w, h, 0);
	texture_setup(fx.tex[1], w, h, 0);
	texture_setup(fx.bg, w, h, 0);
	texture_setup(fx.depth, w, h, 1);
	fx.w = w;
	fx.h = h;
	fx.BindFramebuffer(GL_FRAMEBUFFER, fx.fbo);
	fx.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fx.tex[0], 0);
	fx.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fx.depth, 0);
	if (fx.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		fx.BindFramebuffer(GL_FRAMEBUFFER, 0);
		log_msg("invis: the cloak effect's layer (%dx%d) can't be drawn into: the body is drawn plain", w, h);
		fx.body = fx.filter = fx.shell = 0;   /* (no more tries) */
		return 0;
	}
	fx.BindFramebuffer(GL_FRAMEBUFFER, 0);
	log_msg("invis: the cloak effect's layer is %dx%d", w, h);
	return 1;
}

static void quad(void)
{
	fx.Begin(GL_QUADS);
	fx.Vertex2f(-1, -1);
	fx.Vertex2f(1, -1);
	fx.Vertex2f(1, 1);
	fx.Vertex2f(-1, 1);
	fx.End();
}

static float fx_angle(void)
{
	u32 period = (u32)(FX(FX_WATER_PERIOD) * 1000.0f);
	if (period < 100) period = 100;
	return (float)(GetTickCount() % period) * (6.2831853f / (float)period);
}

/* One filter over the layer, from tex[*src] into the other; 0 if it changes nothing */
static int filter_pass(int f, int *src, float h, float alpha)
{
	float a = 0, b = 0, c = 0, ang, sn, cs;
	int kind;
	switch (f) {
	case FXF_OPACITY: kind = K_OPACITY; a = alpha; if (a >= 1.0f) return 0; break;
	case FXF_WATER:   kind = K_WATER; a = FX(FX_WATER_VERTEX) * h; b = FX(FX_WATER_VCELLS); if (a <= 0.01f) return 0; break;
	case FXF_TONES:
		kind = K_TONES;
		a = FX(FX_STENCIL);
		b = FX(FX_STENCIL_LEVELS) < 1.0f ? 1.0f : (float)(int)(FX(FX_STENCIL_LEVELS) + 0.5f);
		c = FX(FX_STENCIL_SOFT);
		if (a <= 0.0f) return 0;
		break;
	case FXF_OUTLINE: kind = K_OUTLINE; a = FX(FX_OUTLINE_PX) * h; if (a <= 0.05f || FX(FX_OUTLINE_ALPHA) <= 0.0f) return 0; break;
	case FXF_ZOOM: {
		float z = FX(FX_ZOOM), s, cs;
		__asm__("fsincos" : "=t"(cs), "=u"(s) : "0"(fx_angle() * 3.0f));   /* (three breaths a loop) */
		z = 1.0f + (z - 1.0f) * (1.0f + FX(FX_ZOOM_PULSE) * s);
		kind = K_ZOOM;
		b = z;
		if (z < 1.001f) return 0;
		break;
	}
	case FXF_FRINGE:  kind = K_FRINGE; a = FX(FX_FRINGE_PX) * h; b = FX(FX_FRINGE_ANGLE) * (3.14159265f / 180.0f); if (a <= 0.05f) return 0; break;
	default: return 0;
	}
	(void)ang; (void)sn; (void)cs;
	fx.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fx.tex[!*src], 0);
	fx.BindTexture(GL_TEXTURE_2D, fx.tex[*src]);
	fx.Uniform1i(fx.f_kind, kind);
	fx.Uniform1f(fx.f_a, a);
	fx.Uniform1f(fx.f_b, b);
	fx.Uniform1f(fx.f_c, c);
	quad();
	*src = !*src;
	return 1;
}

/* The plain ghost, without shaders: its nearest surfaces at the opacity */
static void ghost_plain(float alpha)
{
	GlBlendFunc *slot = (GlBlendFunc *)(GLgFuncs + GL_SLOT_BLEND_FUNC), oni_blend;
	gl.ColorMask(0, 0, 0, 0);
	TRrBody_Draw(g_ghost.body, g_ghost.extra, g_ghost.matrices, g_ghost.parts, g_ghost.flash, g_ghost.a, g_ghost.b, g_ghost.c);
	gl.ColorMask(1, 1, 1, 1);
	M3rDraw_State_SetInt(DRAW_ZWRITE, 0);
	M3rGeom_State_Commit();
	oni_blend = *slot;
	g_force_src = GL_CONSTANT_ALPHA;
	g_force_dst = GL_ONE_MINUS_CONSTANT_ALPHA;
	*slot = blend_constant;
	gl.BlendColor(0.0f, 0.0f, 0.0f, alpha);
	gl.BlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
	TRrBody_Draw(g_ghost.body, g_ghost.extra, g_ghost.matrices, g_ghost.parts, g_ghost.flash, g_ghost.a, g_ghost.b, g_ghost.c);
	*slot = oni_blend;
	gl.BlendFunc(ONgGLBlendSrc, ONgGLBlendDst);
}

static void ghost_draw(void)
{
	GlBlendFunc *slot = (GlBlendFunc *)(GLgFuncs + GL_SLOT_BLEND_FUNC), oni_blend;
	float alpha = g_ghost.alpha, h, reach;
	int vp[4] = { 0, 0, 0, 0 }, scissor, src = 0;

	g_ghost.pending = 0;
	M3rGeom_State_Push();
	TRrBody_SetMaps(g_ghost.body, g_ghost.textures);
	M3rGeom_State_Set(GEOM_SUBMIT_MODE, 0);
	M3rGeom_State_Set(GEOM_ALPHA, (u32)(alpha * 255.0f + 0.5f));   /* (below 255: no reflection pass, the base texture) */
	M3rDraw_State_SetInt(DRAW_CONSTANT_BLEND, 0);
	M3rDraw_State_SetInt(DRAW_ZWRITE, 1);
	M3rGeom_State_Commit();
	if (g_fx_chain && fx_ready()) fx.GetIntegerv(GL_VIEWPORT, vp);
	if (!g_fx_chain || !fx.body || !layer_ready(vp[2], vp[3])) {   /* (fx_ready: force fields' too) */
		ghost_plain(alpha);
		M3rGeom_State_Pop();
		M3rGeom_State_Commit();
		return;
	}
	h = (float)M3rScreen_Height();
	if (h < 1.0f) h = (float)vp[3];
	scissor = fx.IsEnabled(GL_SCISSOR_TEST);
	if (scissor) fx.Disable(GL_SCISSOR_TEST);
	oni_blend = *slot;
	*slot = blend_constant;
	if (GLgDriver && *(u32 *)(GLgDriver + 0x584)) fx.ActiveTexture(GL_TEXTURE0);   /* (Oni ends its binds on unit 0) */

	/* 0. the screen as it is, behind the body (Zoom's view): from the back buffer the frame is drawn
	 * in (Oni leaves the read buffer on GL_FRONT: glReadBuffer at 0x405a92, the flare's depth
	 * read), its own read buffer put back after */
	if (FX(FX_ZOOM) >= 1.001f) {
		int read = 0;
		fx.GetIntegerv(0x0c02 /* GL_READ_BUFFER */, &read);
		fx.ReadBuffer(0x0405 /* GL_BACK */);
		fx.BindTexture(GL_TEXTURE_2D, fx.bg);
		fx.CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
		if (read) fx.ReadBuffer((u32)read);
	}

	/* 1. the body into the layer, opaque, its nearest surfaces */
	fx.BindFramebuffer(GL_FRAMEBUFFER, fx.fbo);
	fx.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fx.tex[0], 0);
	fx.Viewport(0, 0, vp[2], vp[3]);
	fx.ClearColor(0, 0, 0, 0);
	fx.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	g_force_src = GL_ONE;
	g_force_dst = GL_ZERO;
	gl.BlendFunc(GL_ONE, GL_ZERO);
	fx.UseProgram(fx.body);
	fx.Uniform1f(fx.b_angle, fx_angle());
	fx.Uniform1i(fx.b_tex, 0);
	fx.Uniform1f(fx.b_tamp, FX(FX_WATER_TEXTURE));
	fx.Uniform1f(fx.b_tcells, FX(FX_WATER_TCELLS));
	fx.Uniform1f(fx.b_radius, FX(FX_WATER_RADIUS));
	fx.Uniform1f(fx.b_fog, fx.IsEnabled(GL_FOG) ? 1.0f : 0.0f);
	TRrBody_Draw(g_ghost.body, g_ghost.extra, g_ghost.matrices, g_ghost.parts, g_ghost.flash, g_ghost.a, g_ghost.b, g_ghost.c);

	/* 2. the filters, in the menu's order (no depth: z-write off, and the passes write everything) */
	M3rDraw_State_SetInt(DRAW_ZWRITE, 0);
	M3rDraw_State_SetInt(3 /* ZCompare */, 0);
	M3rGeom_State_Commit();
	fx.UseProgram(fx.filter);
	fx.Uniform1f(fx.f_angle, fx_angle());
	fx.Uniform1f(fx.f_radius, FX(FX_WATER_RADIUS));
	fx.Uniform1i(fx.f_src, 0);
	fx.Uniform1i(fx.f_depth, 1);
	fx.Uniform4f(fx.f_vp, 0, 0, (float)vp[2], (float)vp[3]);
	fx.Uniform4f(fx.f_color, FX(FX_OUTLINE_R), FX(FX_OUTLINE_G), FX(FX_OUTLINE_B), FX(FX_OUTLINE_ALPHA));
	fx.Uniform1i(fx.f_bg, 2);
	fx.ActiveTexture(GL_TEXTURE0 + 2);
	fx.BindTexture(GL_TEXTURE_2D, fx.bg);
	fx.ActiveTexture(GL_TEXTURE0);
	for (int k = 0; k < FXF_COUNT; k++) filter_pass(g_order[k], &src, h, alpha);

	/* 3. on the screen: premultiplied, at the body's depth, z-tested, no z-write */
	fx.BindFramebuffer(GL_FRAMEBUFFER, 0);
	fx.Viewport(vp[0], vp[1], vp[2], vp[3]);
	M3rDraw_State_SetInt(3 /* ZCompare */, 1);
	M3rGeom_State_Commit();
	g_force_src = GL_ONE;
	g_force_dst = GL_ONE_MINUS_SRC_ALPHA;
	gl.BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	reach = 2.0f + FX(FX_WATER_VERTEX) * h + FX(FX_OUTLINE_PX) * h + FX(FX_FRINGE_PX) * h;
	fx.Uniform4f(fx.f_vp, (float)vp[0], (float)vp[1], (float)vp[2], (float)vp[3]);
	fx.Uniform1i(fx.f_kind, K_SCREEN);
	fx.Uniform1f(fx.f_a, reach);
	fx.ActiveTexture(GL_TEXTURE1);
	fx.BindTexture(GL_TEXTURE_2D, fx.depth);
	fx.ActiveTexture(GL_TEXTURE0);
	fx.BindTexture(GL_TEXTURE_2D, fx.tex[src]);
	quad();

	fx.UseProgram(0);
	fx.ActiveTexture(GL_TEXTURE0 + 2);
	fx.BindTexture(GL_TEXTURE_2D, 0);
	fx.ActiveTexture(GL_TEXTURE1);
	fx.BindTexture(GL_TEXTURE_2D, 0);
	fx.ActiveTexture(GL_TEXTURE0);
	GLgBound = (u8 *)0xffffffff;   /* Oni binds its own again */
	*(u8 **)0x560618 = (u8 *)0xffffffff;   /* (its unit 1 too) */
	if (scissor) fx.Enable(GL_SCISSOR_TEST);
	*slot = oni_blend;
	gl.BlendFunc(ONgGLBlendSrc, ONgGLBlendDst);   /* what Oni's cache says is set */
	M3rGeom_State_Pop();
	M3rGeom_State_Commit();
}

/* ---- Force fields: only the shell's front surface (user request, 2026-11-14) ------------------ *
 * A force field (and the Daodan shield's texture) is drawn by TRrBody_DrawMagic (0x427770,
 * called at 0x4e1fdd; fastcall body, matrices, then texture, scale 1.4, part alphas; ret 0xc):
 * every part of the body again, scaled, with the SHIELD texture, additive, so it goes into the
 * alpha-sorted list and every surface adds up: the far side of each part's shell, and the shells
 * of the parts behind, glowed through the near ones. Now the call only notes its arguments (and
 * the time the texture animates by, draw state 0xa), and after the scene (display_powerups, as
 * the ghost) each shell is drawn into the layer at once (submit mode 0), z-tested within it so
 * only its nearest surface is there, as the color Oni's additive blend would have added (shell
 * program: texture times the lit color times the texture's and the part's alpha), then added to
 * the screen (GL_ONE, GL_ONE), z-tested against the scene at its own depth (no z-write: nothing
 * behind it is hidden, as before). One layer per shell, so two characters' fields still add up.
 * Without the layer (no shaders or framebuffer objects) they're drawn as before. Drawing only. */
#define TRrBody_DrawMagic     ((void (ONI_FC *)(void *, void *, void *, float, void *))0x427770)
#define ONcDraw_ShieldMagicCall 0x4e1fdd
#define DRAW_TIME             0x0a
#define SHELLS                16
static struct { void *body, *matrices, *texture, *parts; float scale; u32 time; } g_shell[SHELLS];
static int g_nshells;

static void ONI_FC shield_draw(void *body, void *matrices, void *texture, float scale, void *parts)
{
	u8 *gs = ONgGameState;
	if (g_nshells >= SHELLS || !gs || !load_gl() || !fx_ready()) {   /* (load_gl: the blend functions the passes use) */
		TRrBody_DrawMagic(body, matrices, texture, scale, parts);
		return;
	}
	g_shell[g_nshells].body = body;
	g_shell[g_nshells].matrices = matrices;
	g_shell[g_nshells].texture = texture;
	g_shell[g_nshells].parts = parts;
	g_shell[g_nshells].scale = scale;
	g_shell[g_nshells].time = GS_TICK(gs);
	g_nshells++;
}

static void shells_draw(void)
{
	GlBlendFunc *slot = (GlBlendFunc *)(GLgFuncs + GL_SLOT_BLEND_FUNC), oni_blend;
	int vp[4] = { 0, 0, 0, 0 }, scissor, n = g_nshells;

	g_nshells = 0;
	fx.GetIntegerv(GL_VIEWPORT, vp);
	if (!fx.shell || !layer_ready(vp[2], vp[3])) {   /* as Oni drew them */
		for (int i = 0; i < n; i++) {
			M3rDraw_State_SetInt(DRAW_TIME, g_shell[i].time);
			TRrBody_DrawMagic(g_shell[i].body, g_shell[i].matrices, g_shell[i].texture, g_shell[i].scale, g_shell[i].parts);
		}
		return;
	}
	M3rGeom_State_Push();
	scissor = fx.IsEnabled(GL_SCISSOR_TEST);
	if (scissor) fx.Disable(GL_SCISSOR_TEST);
	oni_blend = *slot;
	*slot = blend_constant;
	if (GLgDriver && *(u32 *)(GLgDriver + 0x584)) fx.ActiveTexture(GL_TEXTURE0);
	for (int i = 0; i < n; i++) {
		/* into the layer: its nearest surface */
		M3rGeom_State_Set(GEOM_SUBMIT_MODE, 0);
		M3rDraw_State_SetInt(DRAW_CONSTANT_BLEND, 0);
		M3rDraw_State_SetInt(DRAW_ZWRITE, 1);
		M3rDraw_State_SetInt(3 /* ZCompare */, 1);
		M3rDraw_State_SetInt(DRAW_TIME, g_shell[i].time);
		M3rGeom_State_Commit();
		fx.BindFramebuffer(GL_FRAMEBUFFER, fx.fbo);
		fx.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fx.tex[0], 0);
		fx.Viewport(0, 0, vp[2], vp[3]);
		fx.ClearColor(0, 0, 0, 0);
		fx.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		g_force_src = GL_ONE;
		g_force_dst = GL_ZERO;
		gl.BlendFunc(GL_ONE, GL_ZERO);
		fx.UseProgram(fx.shell);
		fx.Uniform1i(fx.s_tex, 0);
		TRrBody_DrawMagic(g_shell[i].body, g_shell[i].matrices, g_shell[i].texture, g_shell[i].scale, g_shell[i].parts);

		/* added to the screen at its depth */
		fx.BindFramebuffer(GL_FRAMEBUFFER, 0);
		fx.Viewport(vp[0], vp[1], vp[2], vp[3]);
		M3rDraw_State_SetInt(DRAW_ZWRITE, 0);
		M3rGeom_State_Commit();
		g_force_src = GL_ONE;
		g_force_dst = GL_ONE;
		gl.BlendFunc(GL_ONE, GL_ONE);
		fx.UseProgram(fx.filter);
		fx.Uniform1i(fx.f_src, 0);
		fx.Uniform1i(fx.f_depth, 1);
		fx.Uniform4f(fx.f_vp, (float)vp[0], (float)vp[1], (float)vp[2], (float)vp[3]);
		fx.Uniform1i(fx.f_kind, K_SHELL);
		fx.ActiveTexture(GL_TEXTURE1);
		fx.BindTexture(GL_TEXTURE_2D, fx.depth);
		fx.ActiveTexture(GL_TEXTURE0);
		fx.BindTexture(GL_TEXTURE_2D, fx.tex[0]);
		quad();
	}
	fx.UseProgram(0);
	fx.ActiveTexture(GL_TEXTURE1);
	fx.BindTexture(GL_TEXTURE_2D, 0);
	fx.ActiveTexture(GL_TEXTURE0);
	GLgBound = (u8 *)0xffffffff;
	*(u8 **)0x560618 = (u8 *)0xffffffff;
	if (scissor) fx.Enable(GL_SCISSOR_TEST);
	*slot = oni_blend;
	gl.BlendFunc(ONgGLBlendSrc, ONgGLBlendDst);
	M3rGeom_State_Pop();
	M3rGeom_State_Commit();
}

static void display_characters(void)
{
	g_ghost.pending = 0;
	g_nshells = 0;
	ONrGameState_DisplayCharacters();
}

static void display_powerups(void)
{
	WPrPowerup_Display();
	if (g_ghost.pending) ghost_draw();
	if (g_nshells) shells_draw();
}

/* ---- stubs ----------------------------------------------------------------------------------- */

__attribute__((naked)) static void body_draw_call(void)   /* call TRrBody_Draw, with ESI active, EBP character */
{
	__asm__ volatile(
		"movl %esi, _g_draw_act\n\t"
		"movl %ebp, _g_draw_chr\n\t"
		"jmp *_g_body_draw");
}


__attribute__((naked)) static void body_alpha(void)   /* edx = alpha */
{
	__asm__ volatile(
		"pushl %eax\n\t"
		"pushl %ecx\n\t"
		"pushl %ebp\n\t"
		"pushl %esi\n\t"
		"call _invis_body_alpha\n\t"
		"movl %eax, %edx\n\t"
		"addl $8, %esp\n\t"
		"popl %ecx\n\t"
		"popl %eax\n\t"
		"ret");
}

__attribute__((naked)) static void shadow_alpha(void)   /* edx = alpha */
{
	__asm__ volatile(
		"pushl %eax\n\t"
		"pushl %ecx\n\t"
		"pushl %ebp\n\t"
		"pushl %esi\n\t"
		"call _invis_shadow_alpha\n\t"
		"movl %eax, %edx\n\t"
		"addl $8, %esp\n\t"
		"popl %ecx\n\t"
		"popl %eax\n\t"
		"ret");
}

__attribute__((naked)) static void shimmer_read(void)   /* al = draw the shimmer */
{
	__asm__ volatile(
		"pushl %ecx\n\t"
		"pushl %edx\n\t"
		"pushl %ebp\n\t"
		"pushl %esi\n\t"
		"call _invis_shimmer_shown\n\t"
		"addl $8, %esp\n\t"
		"popl %edx\n\t"
		"popl %ecx\n\t"
		"ret");
}

__attribute__((naked)) static void shimmer_amount(void)   /* st0 = 1 - alpha */
{
	__asm__ volatile(
		"pushl %eax\n\t"
		"pushl %ecx\n\t"
		"pushl %edx\n\t"
		"pushl %ebp\n\t"
		"pushl %esi\n\t"
		"call _invis_shimmer_amount\n\t"
		"addl $8, %esp\n\t"
		"popl %edx\n\t"
		"popl %ecx\n\t"
		"popl %eax\n\t"
		"ret");
}

__attribute__((naked)) static void shield_read(void)   /* al = draw the shell */
{
	__asm__ volatile(
		"pushl %ecx\n\t"
		"pushl %edx\n\t"
		"pushl %ebp\n\t"
		"pushl %esi\n\t"
		"call _invis_shield_shown\n\t"
		"addl $8, %esp\n\t"
		"popl %edx\n\t"
		"popl %ecx\n\t"
		"ret");
}

__attribute__((naked)) static void super_read(void)   /* st0 = the super amount for the particles */
{
	__asm__ volatile(
		"pushl %eax\n\t"
		"pushl %ecx\n\t"
		"pushl %edx\n\t"
		"pushl %ebp\n\t"
		"pushl %ebx\n\t"
		"call _invis_super_for_particles\n\t"
		"addl $8, %esp\n\t"
		"popl %edx\n\t"
		"popl %ecx\n\t"
		"popl %eax\n\t"
		"ret");
}

__attribute__((naked)) static void melee_attacker(void)   /* al = [edi+0x2815] */
{
	__asm__ volatile(
		"pushl %ecx\n\t"
		"pushl %edx\n\t"
		"pushl %edi\n\t"
		"pushl %ebx\n\t"
		"call _invis_attacked\n\t"
		"addl $8, %esp\n\t"
		"popl %edx\n\t"
		"popl %ecx\n\t"
		"ret");
}

__attribute__((naked)) static void throw_attacker(void)   /* al = [ebp+0x2815] */
{
	__asm__ volatile(
		"pushl %ecx\n\t"
		"pushl %edx\n\t"
		"pushl %ebp\n\t"
		"pushl %ebx\n\t"
		"call _invis_attacked\n\t"
		"addl $8, %esp\n\t"
		"popl %edx\n\t"
		"popl %ecx\n\t"
		"ret");
}

/* ---- install --------------------------------------------------------------------------------- */

static void call_at(u32 addr, void *stub, u32 len)
{
	u8 code[12];
	u32 rel = (u32)stub - (addr + 5);
	code[0] = 0xe8;
	memcpy(code + 1, &rel, 4);
	for (u32 i = 5; i < len; i++) code[i] = 0x90;
	patch_write(addr, code, len);
}

void invis_install(void)
{
	fx_load();
	static const u8 body[]    = { 0x8b, 0x96, 0x18, 0x28, 0x00, 0x00, 0x89, 0x54, 0x24, 0x14 };
	static const u8 shadow[]  = { 0x8b, 0x96, 0x18, 0x28, 0x00, 0x00, 0x89, 0x54, 0x24, 0x7c };
	static const u8 read[]    = { 0x8a, 0x86, 0x15, 0x28, 0x00, 0x00, 0x84, 0xc0, 0x0f, 0x84 };
	static const u8 amount[]  = { 0xd9, 0x05, 0x08, 0x95, 0x52, 0x00, 0xd8, 0xa6, 0x18, 0x28, 0x00, 0x00 };
	static const u8 melee[]   = { 0x8a, 0x87, 0x15, 0x28, 0x00, 0x00, 0x84, 0xc0, 0x74 };
	static const u8 thrown[]  = { 0x8a, 0x85, 0x15, 0x28, 0x00, 0x00, 0x84, 0xc0, 0x74 };

	{   /* the cloak ends when the body is back to 1, not 0 (see above), whatever the rest does */
		static const u8 end_old[] = { 0xd9, 0x05, 0x14, 0x95, 0x52, 0x00 }, end_new[] = { 0xd9, 0x05, 0x08, 0x95, 0x52, 0x00 };
		if (code_matches(ONcInvis_EndCompare, end_old, sizeof end_old)) {
			patch_write(ONcInvis_EndCompare, end_new, sizeof end_new);
			log_msg("invis: the cloak now ends when the body is back (motion blur, the ninja's dash afterimages, returns after it)");
		} else {
			log_msg("invis: Oni's cloak end changed, left alone");
		}
	}

	if (!code_matches(ONcDraw_BodyAlpha, body, sizeof body) ||
	    !code_matches(ONcDraw_ShadowAlpha, shadow, sizeof shadow) ||
	    !code_matches(ONcDraw_InvisRead, read, sizeof read) ||
	    !code_matches(ONcDraw_InvisAmount, amount, sizeof amount) ||
	    !code_matches(ONcMelee_Attacker, melee, sizeof melee) ||
	    !code_matches(ONcThrow_Attacker, thrown, sizeof thrown)) {
		log_msg("invis: Oni's cloak code changed, everyone sees cloaked players as in Oni");
		return;
	}
	call_at(ONcDraw_BodyAlpha, body_alpha, 6);
	call_at(ONcDraw_ShadowAlpha, shadow_alpha, 6);
	call_at(ONcDraw_InvisRead, shimmer_read, 6);
	call_at(ONcDraw_InvisAmount, shimmer_amount, 12);
	call_at(ONcMelee_Attacker, melee_attacker, 6);
	{
		static const u8 shield[] = { 0x8a, 0x86, 0x00, 0x28, 0x00, 0x00, 0x84, 0xc0, 0x0f, 0x84 };
		static const u8 super[]  = { 0xd9, 0x83, 0x68, 0x16, 0x00, 0x00, 0xd8, 0x1d, 0x14, 0x95, 0x52, 0x00 };
		if (code_matches(ONcDraw_ShieldRead, shield, sizeof shield) && code_matches(ONcUpdate_SuperRead, super, sizeof super)) {
			call_at(ONcDraw_ShieldRead, shield_read, 6);
			call_at(ONcUpdate_SuperRead, super_read, 6);
			log_msg("invis: a cloaked player shows no shield shell to others and no Daodan glow (multiplayer maps)");
		} else {
			log_msg("invis: Oni's shield or super particle code changed: a cloaked player's shell and glow still show");
		}
	}
	call_at(ONcThrow_Attacker, throw_attacker, 6);
	{   /* the cloaked player's own body: its nearest surfaces, colors at the cloak's opacity (see "The see-through body") */
		u8 call[5] = { 0xe8 };
		u32 rel = 0x427630 - (ONcDraw_BodyDrawCall + 5);
		int ok = 0;
		memcpy(call + 1, &rel, 4);
		g_body_draw = (void *)body_draw;
		if (code_matches(ONcDraw_BodyDrawCall, call, 5) &&
		    patch_call(ONcDisplay_CharactersCall, (u32)ONrGameState_DisplayCharacters, (u32)display_characters)) {
			if (patch_call(ONcDisplay_PowerupsCall, (u32)WPrPowerup_Display, (u32)display_powerups)) {
				call_at(ONcDraw_BodyDrawCall, body_draw_call, 5);
				ok = 1;
				if (!patch_call(ONcDraw_ShieldMagicCall, 0x427770, (u32)shield_draw))
					log_msg("invis: Oni's force field draw changed: its shell shows both sides as before");
			} else {
				patch_call(ONcDisplay_CharactersCall, (u32)display_characters, (u32)ONrGameState_DisplayCharacters);
			}
		}
		if (!ok) log_msg("invis: Oni's body draw or display calls changed: the cloaked player's own body is drawn as in Oni");
	}
	log_msg("invis: in network games a cloaked player sees themselves faintly (30 percent), others see them only as a shimmer when they strike");
}
