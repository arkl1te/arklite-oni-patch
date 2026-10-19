/* Item respawn: in a multiplayer game, a weapon or power-up placed in the map comes back at its
 * spot a while after a player takes it.
 *
 * The level start (0x51b0f0, called at 0x505fc3) clears the weapon pool and walks the WEAP and
 * PWRU objects, creating each one's item with a walk callback (ECX = the object):
 *   - 0x51ad90 for WEAP: the class is the ONWC named at object +0x2c, the weapon goes in the
 *     64-slot pool at 0x6221c0 (0x170 bytes each: +2 flags, bit 0 in use; +4 class; +8 the
 *     character holding it, 0 if none; +0x48 the tick it was made). weapon_reset (0x51ad40)
 *     deletes the unheld weapons and walks again with the same callback.
 *   - 0x51b080 for PWRU: type at object +0x2c, made by 0x51c160; the power-up's pointer goes to
 *     object +0x30. Power-ups are a packed array at 0x620460 (0x74 bytes each, count u16 at
 *     0x622160: +0 flags, bit 3 placed by the level; +8 position). Deleting one (0x51c4f0, as
 *     when it's picked up) moves the last one into its place, so the pointer goes stale; a
 *     placed power-up is recognized by the placed flag and its position. powerup_reset
 *     (0x51afc0) deletes them all and walks again.
 * Arklite wraps both callbacks (the level start's and the resets' `mov edx, callback`) to note
 * which item each object made. Every tick of a multiplayer game (players.c, after the input) it
 * checks each item: a weapon is taken once its slot isn't that weapon any more or someone holds
 * it; a power-up once no placed power-up is at its spot. The respawn time later, the callback
 * makes a new one from the object.
 *
 * The respawn time is per item: level.ini [respawn] (written by Catwalk from each item's
 * RespawnTime) lists count=N and itemK=<WEAP|PWRU> <x> <y> <z> <seconds>, matched by type and
 * position; 0 seconds means never. Items not listed take Arklite.ini [multiplayer] item_respawn
 * (30 seconds). */
#include "arklite.h"

void *memset(void *, int, unsigned);
void *memcpy(void *, const void *, unsigned);

#define ONgGameState        (*(u8 **)0x5ece7c)
#define GS_TICK(gs)         (*(u32 *)((gs) + 0x144))
#define ONrLevel_GetCurrent ((u16 (ONI_FC *)(void))0x5054c0)

#define WPrLevel_Begin          ((u32 (ONI_FC *)(void))0x51b0f0)
#define WPcLevel_BeginCall      0x505fc3
#define WPcWeapon_FromObject    0x51ad90
#define WPcPowerup_FromObject   0x51b080
typedef u8 (ONI_FC *ObjectCallback)(u8 *obj, u32 param);

#define OBJcType_WEAP       0x57454150
#define OBJcType_PWRU       0x50575255
#define OBJ_POS(o)          ((float *)((o) + 0xc))
#define OBJ_POWERUP(o)      (*(u8 **)((o) + 0x30))

#define WP_WEAPONS          ((u8 *)0x6221c0)
#define WP_WEAPON_SIZE      0x170
#define WP_WEAPON_SLOTS     64
#define WEAPON_INUSE(w)     (*(u8 *)((w) + 2) & 1)
#define WEAPON_CLASS(w)     (*(u32 *)((w) + 4))
#define WEAPON_HOLDER(w)    (*(u32 *)((w) + 8))
#define WEAPON_BORN(w)      (*(u32 *)((w) + 0x48))

#define WP_POWERUPS         ((u8 *)0x620460)
#define WP_POWERUP_SIZE     0x74
#define WP_POWERUP_COUNT    (*(u16 *)0x622160)
#define POWERUP_PLACED(p)   (*(u16 *)(p) & 8)
#define POWERUP_POS(p)      ((float *)((p) + 8))

#define MAX_ITEMS   256
#define NOT_READ    0xffffffffu

typedef struct {
	u8  *obj;
	u32  type;          /* OBJcType_WEAP or OBJcType_PWRU */
	u32  delay;         /* ticks, 0: never; NOT_READ until the level's settings are read */
	u32  due;           /* tick it comes back at, 0 while it's there */
	u8  *weapon;        /* WEAP: its slot, birth tick and class */
	u32  born, cls;
	float pos[3];       /* PWRU: where the power-up was made */
	int  made;
} Item;

static Item g_items[MAX_ITEMS];
static int  g_nitems, g_delays_read;
static u32  g_default_ticks = 30 * 60;

static Item *item_for(u8 *obj, u32 type)
{
	for (int i = 0; i < g_nitems; i++)
		if (g_items[i].obj == obj) return &g_items[i];
	if (g_nitems == MAX_ITEMS) return 0;
	Item *it = &g_items[g_nitems++];
	memset(it, 0, sizeof *it);
	it->obj = obj;
	it->type = type;
	it->delay = NOT_READ;
	return it;
}

static u8 ONI_FC weapon_from_object(u8 *obj, u32 param)
{
	u32 born[WP_WEAPON_SLOTS];
	u8 used[WP_WEAPON_SLOTS];
	Item *it = item_for(obj, OBJcType_WEAP);

	for (int i = 0; i < WP_WEAPON_SLOTS; i++) {
		u8 *w = WP_WEAPONS + i * WP_WEAPON_SIZE;
		used[i] = WEAPON_INUSE(w);
		born[i] = WEAPON_BORN(w);
	}
	u8 r = ((ObjectCallback)WPcWeapon_FromObject)(obj, param);
	if (!it) return r;
	it->made = 0;
	it->due = 0;
	for (int i = 0; i < WP_WEAPON_SLOTS; i++) {
		u8 *w = WP_WEAPONS + i * WP_WEAPON_SIZE;
		if (WEAPON_INUSE(w) && (!used[i] || WEAPON_BORN(w) != born[i])) {
			it->weapon = w;
			it->born = WEAPON_BORN(w);
			it->cls = WEAPON_CLASS(w);
			it->made = 1;
			break;
		}
	}
	return r;
}

static u8 ONI_FC powerup_from_object(u8 *obj, u32 param)
{
	Item *it = item_for(obj, OBJcType_PWRU);

	OBJ_POWERUP(obj) = 0;
	u8 r = ((ObjectCallback)WPcPowerup_FromObject)(obj, param);
	if (!it) return r;
	it->due = 0;
	it->made = OBJ_POWERUP(obj) != 0;
	if (it->made) memcpy(it->pos, POWERUP_POS(OBJ_POWERUP(obj)), sizeof it->pos);
	return r;
}

static u32 ONI_FC level_begin(void)
{
	g_nitems = 0;
	g_delays_read = 0;
	return WPrLevel_Begin();
}

/* ---- Respawn times --------------------------------------------------------------------- */

static int near(const float *a, const float *b)
{
	for (int k = 0; k < 3; k++) {
		float d = a[k] - b[k];
		if (d > 0.05f || d < -0.05f) return 0;
	}
	return 1;
}

static void read_delays(void)
{
	const char *ini = custom_game_level_ini(ONrLevel_GetCurrent());
	int count = ini ? (int)GetPrivateProfileIntA("respawn", "count", 0, ini) : 0, listed = 0;

	g_delays_read = 1;
	for (int i = 0; i < g_nitems; i++) g_items[i].delay = g_default_ticks;
	for (int k = 0; k < count; k++) {
		char key[16], line[128];
		const char *s;
		float pos[3], seconds;
		u32 type;

		fmt(key, "item%d", k);
		GetPrivateProfileStringA("respawn", key, "", line, sizeof line, ini);
		s = skip_space(line);
		if (str_len(s) < 4) continue;
		type = (u32)(u8)s[0] << 24 | (u32)(u8)s[1] << 16 | (u32)(u8)s[2] << 8 | (u8)s[3];
		s += 4;
		if (!parse_float(&s, &pos[0]) || !parse_float(&s, &pos[1]) || !parse_float(&s, &pos[2]) ||
		    !parse_float(&s, &seconds) || seconds < 0) {
			log_msg("items: level.ini [respawn] %s: can't read \"%s\"", key, line);
			continue;
		}
		for (int i = 0; i < g_nitems; i++)
			if (g_items[i].type == type && near(OBJ_POS(g_items[i].obj), pos)) {
				g_items[i].delay = (u32)(seconds * 60 + 0.5f);
				listed++;
			}
	}
	log_msg("items: %d placed items, %d with their own respawn time, the others %u s",
	        g_nitems, listed, g_default_ticks / 60);
}

/* ---- Every tick ------------------------------------------------------------------------- */

static int present(const Item *it)
{
	if (!it->made) return 0;
	if (it->type == OBJcType_WEAP) {
		u8 *w = it->weapon;
		return WEAPON_INUSE(w) && WEAPON_BORN(w) == it->born && WEAPON_CLASS(w) == it->cls &&
		       !WEAPON_HOLDER(w);
	}
	for (u32 i = 0, n = WP_POWERUP_COUNT; i < n; i++) {
		u8 *p = WP_POWERUPS + i * WP_POWERUP_SIZE;
		if (POWERUP_PLACED(p) && near(POWERUP_POS(p), it->pos)) return 1;
	}
	return 0;
}

void items_tick(void)
{
	u32 tick = GS_TICK(ONgGameState);

	if (!g_delays_read) read_delays();
	for (int i = 0; i < g_nitems; i++) {
		Item *it = &g_items[i];
		if (it->delay == NOT_READ) it->delay = g_default_ticks;   /* made after the level start */
		if (!it->delay) continue;
		if (!it->due) {
			if (!present(it)) it->due = tick + it->delay;
		} else if (tick >= it->due) {
			if (it->type == OBJcType_WEAP) weapon_from_object(it->obj, 0);
			else powerup_from_object(it->obj, 0);
			if (!it->made) it->due = tick + it->delay;   /* couldn't: try again later */
		}
	}
}

/* ---- Install ---------------------------------------------------------------------------- */

static int mov_edx_matches(u32 addr, u32 value)
{
	const u8 *p = (const u8 *)addr;
	return p[0] == 0xba && *(const u32 *)(p + 1) == value;
}

void items_install(void)
{
	static const struct { u32 addr, from; void *to; } sites[] = {
		{ 0x51b119, WPcWeapon_FromObject,  weapon_from_object },    /* level start */
		{ 0x51ad6a, WPcWeapon_FromObject,  weapon_from_object },    /* weapon_reset */
		{ 0x51b139, WPcPowerup_FromObject, powerup_from_object },   /* level start */
		{ 0x51b05b, WPcPowerup_FromObject, powerup_from_object },   /* powerup_reset */
	};
	const u8 *call = (const u8 *)WPcLevel_BeginCall;

	if (call[0] != 0xe8 || WPcLevel_BeginCall + 5 + *(u32 *)(call + 1) != (u32)WPrLevel_Begin) {
		log_msg("items: 0x%x changed, no item respawn", WPcLevel_BeginCall);
		return;
	}
	for (u32 i = 0; i < sizeof sites / sizeof sites[0]; i++)
		if (!mov_edx_matches(sites[i].addr, sites[i].from)) {
			log_msg("items: 0x%x changed, no item respawn", sites[i].addr);
			return;
		}
	for (u32 i = 0; i < sizeof sites / sizeof sites[0]; i++) {
		u32 to = (u32)sites[i].to;
		patch_write(sites[i].addr + 1, &to, 4);
	}
	patch_call(WPcLevel_BeginCall, (u32)WPrLevel_Begin, (u32)level_begin);
	g_default_ticks = GetPrivateProfileIntA("multiplayer", "item_respawn", 30, ini_path()) * 60;
	log_msg("items: placed items respawn in multiplayer games (default %u s)", g_default_ticks / 60);
}
