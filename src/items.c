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
 * (30 seconds). Force fields and phase cloaks (PWRU type 3 ShieldBelt and 4 Invisibility, the type at
 * object +0x2c as 0x51b080 reads it: 3 gets a 100-point charge, 4 1800 ticks of cloak) come back
 * after SPECIAL_RESPAWN_S whatever the level or the setting says, unless the level says never.
 *
 * A respawn shows: the particle item_e01 at the item's spot, its +Y (the way its sparks fly) up,
 * and a zap there, zap09 and zap15 in turn. Those two are only permutations of the group
 * elec_detail, whose picks are random, so each gets a one-permutation group and impulse of
 * Arklite's, filled from the game's. All of it runs in the tick, so every PC does the same; the
 * turn is tick state, and rollback's re-runs play no sound twice (lockstep.c). */
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
#define OBJ_PWRU_TYPE(o)    (*(u32 *)((o) + 0x2c))
#define PWRU_SHIELD         3      /* force field */
#define PWRU_INVISIBILITY   4      /* phase cloak */
#define SPECIAL_RESPAWN_S   60     /* their respawn time (user, 2026-11-12) */
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

static Item g_items[MAX_ITEMS] TICK_STATE;
#define PLAYER_FX_DELAY  3    /* ticks */
#define PLAYER_FX_MAX    8
static struct { u32 due; float pos[3]; } g_player_fx[PLAYER_FX_MAX] TICK_STATE;
static int  g_nitems TICK_STATE, g_delays_read TICK_STATE, g_zap TICK_STATE;
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
	if (net_mutator(NET_MUT_NO_WEAPONS)) return 1;   /* the No Weapons mutator: none made, none respawned (the item never "is there") */
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
	memset(g_player_fx, 0, sizeof g_player_fx);
	g_zap = 0;
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

/* A force field or a phase cloak */
static int special(const Item *it)
{
	return it->type == OBJcType_PWRU && it->obj &&
	       (OBJ_PWRU_TYPE(it->obj) == PWRU_SHIELD || OBJ_PWRU_TYPE(it->obj) == PWRU_INVISIBILITY);
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
	int specials = 0;
	for (int i = 0; i < g_nitems; i++)
		if (special(&g_items[i]) && g_items[i].delay) {
			g_items[i].delay = SPECIAL_RESPAWN_S * 60;
			specials++;
		}
	log_msg("items: %d placed items, %d with their own respawn time, the others %u s; %d force fields and cloaks %d s",
	        g_nitems, listed, g_default_ticks / 60, specials, SPECIAL_RESPAWN_S);
}

/* ---- What a respawn looks and sounds like ------------------------------------------------ */

/* What the game's particle command does (around 0x50a0a5, in the stock source
 * ONiParticle3_CreateParticle), with its own orientation: the command's can't point straight up */
#define P3rGetParticleClass     ((u8 *(ONI_FC *)(const char *))0x45a030)
#define P3rCreateParticle       lockstep_p3_create_particle   /* 0x455e40, choosing the effects stream (lockstep.c) */
#define P3rSendEvent            lockstep_p3_send_event        /* 0x45b2c0, the same */
#define P3rGetPositionPtr       ((float *(ONI_FC *)(u8 *, u8 *, u8 *))0x45abc0)
#define ONrGameState_GetGameTime ((u32 (ONI_FC *)(void))0x4fc880)
#define UUrLocalRandom          ((u32 (ONI_FC *)(void))0x426e30)
#define AKrCollision_InitCache  ((void (ONI_FC *)(void *))0x441190)
typedef u32 *(ONI_FC *ParticleField)(u8 *, u8 *);
#define P3rGetVelocityPtr       ((ParticleField)0x50a3a0)
#define P3rGetOffsetPosPtr      ((ParticleField)0x50a3c0)
#define P3rGetDynamicMatrixPtr  ((ParticleField)0x50a3e0)
#define P3rGetTextureIndexPtr   ((ParticleField)0x50a400)
#define P3rGetTextureTimePtr    ((ParticleField)0x50a420)
#define P3rGetOwnerPtr          ((ParticleField)0x50a440)
#define P3rGetPrevPositionPtr   ((ParticleField)0x50a460)
#define P3rGetLensFramesPtr     ((ParticleField)0x50a480)
#define P3rGetEnvironmentCache  ((ParticleField)0x50a4a0)
#define P3cEvent_Create         11
#define PCLASS_DEF(c)           (*(u8 **)((c) + 0x48))
#define PDEF_HAS_ORIENTATION    0x2000
#define PCLASS_ORIENT_OFFSET(c) (*(u16 *)((c) + 0x5e))
#define P3_HEADER_SIZE          0x44
#define RESPAWN_PARTICLE        "item_e01"

static void field_zero(u32 *p, int dwords)
{
	if (p) memset(p, 0, dwords * 4);
}

static void respawn_particle(const char *name, const float *pos)
{
	static const float up[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };   /* its +Y is world up, X = Y x Z */
	u8 *cls = P3rGetParticleClass(name), *p, dead = 0;
	u32 time, *texture;
	float *at;

	if (!cls) return;
	time = ONrGameState_GetGameTime();
	if (!(p = P3rCreateParticle(cls, time))) return;
	at = P3rGetPositionPtr(cls, p, &dead);
	if (dead) return;
	memcpy(at, pos, 12);
	if (*(u32 *)(PCLASS_DEF(cls) + 4) & PDEF_HAS_ORIENTATION)
		memcpy(p + PCLASS_ORIENT_OFFSET(cls) + P3_HEADER_SIZE, up, sizeof up);
	field_zero(P3rGetVelocityPtr(cls, p), 3);
	field_zero(P3rGetOffsetPosPtr(cls, p), 3);
	field_zero(P3rGetDynamicMatrixPtr(cls, p), 1);
	field_zero(P3rGetOwnerPtr(cls, p), 1);
	if ((texture = P3rGetTextureIndexPtr(cls, p))) *texture = UUrLocalRandom() & 0xffff;
	if ((texture = P3rGetTextureTimePtr(cls, p))) *texture = time;
	field_zero(P3rGetPrevPositionPtr(cls, p), 1);
	field_zero(P3rGetLensFramesPtr(cls, p), 1);
	if ((texture = P3rGetEnvironmentCache(cls, p))) AKrCollision_InitCache(texture);
	P3rSendEvent(cls, p, P3cEvent_Create, (float)(int)time * (1.0f / 60.0f));
}

/* The game's sound structures (BFW_SoundSystem2.h) */
#define SSrGroup_GetByName      ((u8 *(ONI_FC *)(const char *))0x47fcf0)
#define SSrImpulse_Play         ((void (ONI_FC *)(u8 *, const float *, u32, u32, u32))0x480440)
#define SND_ZAP_GROUP           "elec_detail"
#define SND_NAME_SIZE           32
#define GROUP_NUM_CHANNELS(g)   (*(u32 *)((g) + 0x2c))
#define GROUP_PERMUTATIONS(g)   (*(u8 **)((g) + 0x30))
#define ARRAY_USED(a)           (*(u32 *)((a) + 0x4))
#define ARRAY_DATA(a)           (*(u8 **)((a) + 0x10))
#define PERM_SIZE               0x38
#define PERM_NAME(p)            ((const char *)(p) + 0x18)
#define SND_PRIORITY_NORMAL     1
#define ZAP_NEAR                50.0f   /* full volume inside this distance */
#define ZAP_FAR                 400.0f  /* silent past it */

typedef struct {   /* UUtMemory_Array */
	u32 elem_size, used, chunk, allocated;
	void *data;
} SoundArray;

typedef struct {   /* SStGroup, SStImpulse */
	u8 group[0x34], impulse[0x8c], permutation[PERM_SIZE];
	SoundArray permutations;
	const char *name;
	int ready;
} Zap;

static Zap g_zaps[2] = { { .name = "zap09" }, { .name = "zap15" } };

static int name_starts(const char *s, const char *prefix)
{
	while (*prefix) if (*s++ != *prefix++) return 0;
	return *s == 0 || *s == '.';
}

/* Fills it from the game's permutation: the sound data's pointer is the game's to set */
static int zap_ready(Zap *z)
{
	u8 *group = SSrGroup_GetByName(SND_ZAP_GROUP), *array, *perm;
	u32 n;

	if (!group || !(array = GROUP_PERMUTATIONS(group))) return 0;
	perm = ARRAY_DATA(array);
	n = ARRAY_USED(array);
	for (; n; n--, perm += PERM_SIZE)
		if (name_starts(PERM_NAME(perm), z->name)) break;
	if (!n) return 0;
	memcpy(z->permutation, perm, PERM_SIZE);
	if (!z->ready) {
		z->permutations.elem_size = PERM_SIZE;
		z->permutations.used = z->permutations.allocated = 1;
		z->permutations.data = z->permutation;
		memcpy(z->group, group, sizeof z->group);
		*(u16 *)(z->group + 0x28) = 0;   /* flags: no-repeat means nothing with one sound */
		GROUP_PERMUTATIONS(z->group) = (u8 *)&z->permutations;
		*(u8 **)(z->impulse + 0x20) = z->group;
		*(u32 *)(z->impulse + 0x44) = SND_PRIORITY_NORMAL;
		*(float *)(z->impulse + 0x48) = ZAP_NEAR;
		*(float *)(z->impulse + 0x4c) = ZAP_FAR;
		z->ready = 1;
	}
	return 1;
}

static void respawn_sound(const float *pos)
{
	Zap *z = &g_zaps[g_zap];

	g_zap ^= 1;
	if (zap_ready(z)) SSrImpulse_Play(z->impulse, pos, 0, 0, 0);
	else log_msg("items: no sound %s in the group %s", z->name, SND_ZAP_GROUP);
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

u32 items_default_ticks(void) { return g_default_ticks; }

/* the network host's, on the joiner: read again at the next level start (read_delays) */
void items_set_default_ticks(u32 ticks) { g_default_ticks = ticks; }

/* A firing weapon's loop sound (SMG, Phase Stream Projector) is an ambient its particles start on
 * the weapon's Start event and end on Stop, which only a tick's trigger release sends. When the
 * game stops ticking with a trigger held (the match's end), WPrReleaseTrigger (0x51a3b0, ECX the
 * weapon, DL 1: now, even mid-burst) clears the firing flag (+2 bit 0x10) and sends Stop. */
#define WPrReleaseTrigger   ((void (ONI_FC *)(u8 *weapon, u32 now))0x51a3b0)
#define WEAPON_FIRING(w)    (*(u16 *)((w) + 2) & 0x10)

void items_stop_firing(void)
{
	for (int i = 0; i < WP_WEAPON_SLOTS; i++) {
		u8 *w = WP_WEAPONS + i * WP_WEAPON_SIZE;
		if (WEAPON_INUSE(w) && WEAPON_FIRING(w)) WPrReleaseTrigger(w, 1);
	}
}

/* The particle for a player coming back, whatever the map (respawn_fx.c): in the tick, like an
 * item's, so every PC and every replay fires it. A few ticks late: on the respawn's tick
 * the camera, which is the sound listener, is still at the body, and an impulse's volume is set
 * as it starts, so its sound came out as far away as the spawn point is from the corpse. */

void items_player_respawned(const float *pos)
{
	if (!P3rGetParticleClass(respawn_fx_root())) {
		log_msg("items: no particle class %s", respawn_fx_root());
		return;
	}
	for (int i = 0; i < PLAYER_FX_MAX; i++)
		if (!g_player_fx[i].due) {
			g_player_fx[i].due = GS_TICK(ONgGameState) + PLAYER_FX_DELAY;
			memcpy(g_player_fx[i].pos, pos, sizeof g_player_fx[i].pos);
			return;
		}
}

static void player_fx_tick(u32 tick)
{
	for (int i = 0; i < PLAYER_FX_MAX; i++)
		if (g_player_fx[i].due && tick >= g_player_fx[i].due) {
			g_player_fx[i].due = 0;
			respawn_particle(respawn_fx_root(), g_player_fx[i].pos);
		}
}

void items_tick(void)
{
	u32 tick = GS_TICK(ONgGameState);

	if (!g_delays_read) read_delays();
	player_fx_tick(tick);
	for (int i = 0; i < g_nitems; i++) {
		Item *it = &g_items[i];
		if (it->delay == NOT_READ) it->delay = special(it) ? SPECIAL_RESPAWN_S * 60 : g_default_ticks;   /* made after the level start */
		if (!it->delay) continue;
		if (!it->due) {
			if (!present(it)) it->due = tick + it->delay;
		} else if (tick >= it->due) {
			if (it->type == OBJcType_WEAP) weapon_from_object(it->obj, 0);
			else powerup_from_object(it->obj, 0);
			if (!it->made) it->due = tick + it->delay;   /* couldn't: try again later */
			else {
				static u32 said_tick[MAX_ITEMS];   /* (logging only: rollback re-runs the tick) */
				if (special(it) && said_tick[i] < tick) {
					said_tick[i] = tick;
					log_msg("items: %s back at %d %d %d (tick %u)", OBJ_PWRU_TYPE(it->obj) == PWRU_SHIELD ? "force field" : "phase cloak",
					        (int)OBJ_POS(it->obj)[0], (int)OBJ_POS(it->obj)[1], (int)OBJ_POS(it->obj)[2], tick);
				}
				respawn_particle(RESPAWN_PARTICLE, OBJ_POS(it->obj));
				respawn_sound(OBJ_POS(it->obj));
			}
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
