/* The Host page's gameplay mutators (the character ones: char_menu.c; Daodan for All: mp_rules.c).
 * Every change to the game's data is made at a level's load (mutators_level, from mp_rules_level,
 * before any character is made and outside the ticks) from the host's mutators, which LOBBY
 * brings to every PC before any session loads, and put back at the level's unload
 * (mutators_level_end, while the level's data is still there). What happens during play is done
 * in the tick (mutators_tick, lockstep.c), so every PC, replay and rollback does the same.
 *
 * No Weapons (NET_MUT_NO_WEAPONS): items.c makes none of the map's WEAP items (nor respawns
 * them), and the player spawn placements' weapon (CHAR OSD weapon_class_name, object +0x90, after
 * flags +0x2c, class +0x30, name +0x70) is blanked for the level (players.c players_spawn_weapons).
 *
 * Fast Screamer (NET_MUT_FAST_SCREAMER): the Screaming Cell (w9_scr_p01) flies twice as fast. Its
 * Update event sets its speed every tick with SetVelocity (action type 52, Speed 20 in its own
 * space, AttractSpring steering it); that 20 becomes 40. A particle class (P3rGetParticleClass)
 * has its definition at +0x48, the action count at definition +0x12 (u16), the action array at
 * +0x2fc and the emitter array at +0x300; an action is 0x1a8 bytes (type u32, data u32, 8
 * variable references of 0x18, then 8 values of 0x1c: type u32, then a union as big as a
 * variable reference), checked by the emitters following the actions exactly. SetVelocity's value
 * 0 is the speed. Only the hand weapon fires this class (ONWC: w9_scr_e01 > e02 > p01); the
 * turrets' w9_scr_p01a has no SetVelocity and keeps its emitter's speed.
 *
 * Fast Plasma (NET_MUT_FAST_PLASMA): the SML3 Plasma Rifle (ONWC w3_phr) fires twice as fast: its
 * shooter attachment (w3_phr_e01: matrix 0x30, class name 0x10, class 4, then ammo used u16 1 and
 * chamber time u16 55 frames) gets 28.
 *
 * Fast Ball (NET_MUT_FAST_BALL): the Super Ball Gun's ball (w5_sbg_p01) flies twice as fast. Unlike
 * the Screaming Cell it has no SetVelocity: the shooter w5_sbg_e01 (the ONWC w5_sbg's only
 * particle) emits it once at Uniform speed 150, and after that only half gravity and the bounces
 * move it; that 150 becomes 300. An emitter (OniFoxed P3tEmitter, version 9+) is 0x1bc bytes:
 * class name char[64], class 0x40, flags 0x44, then link, rate, position, direction and speed
 * types (speed at 0x60), the two orientations, and 12 values of 0x1c from 0x6c (2 rate,
 * 3 position, 3 direction, then speed: the Uniform speed is value 8, at 0x14c). The emitter count
 * is the definition's +0x14 (u16).
 *
 * Bombs (NET_MUT_BOMBS): every class dies as the Mad Bomber does. A class's death particle (ONCC
 * death_effect +0xbec, char[64], resolved at +0xc2c; the death function 0x4eb550 attaches it at
 * the pelvis, at 0x4eb835) becomes mad_e01 (three seconds of countdown, then the blast) and its
 * delete delay (+0xc8a, u16, copied to the character's +0x1644 timer at death) the Mad Bomber's,
 * 190 ticks.
 *
 * A bomb that went off (always for a class whose death particle is mad_e01: the Mad Bomber, or
 * everyone with Bombs): Oni deletes an AI one when its timer runs out, but never a player, whose
 * body stayed. Now, one tick before that, a black scorch (the decal particle d_SCORCH, made with
 * P3rCreateEffect 0x456030 as the CreateDecal action makes decals: location Decal, random
 * rotation, on the floor quad a sphere swept down from the body finds) marks the spot, and from
 * then a player's body isn't drawn (players_hidden, net.c's draw-pass hook) and leaves no corpse
 * when they respawn (NoCorpse). */
#include "arklite.h"

void *memset(void *, int, unsigned);
void *memcpy(void *, const void *, unsigned);

#define ONgGameState            (*(u8 **)0x5ece7c)
#define GS_TICK(gs)             (*(u32 *)((gs) + 0x144))
#define GS_CHARACTERS(gs)       ((gs) + 0x1260)
#define GS_COLLISION(gs)        (*(u8 **)(*(u8 **)((gs) + 0x150) + 0x40))
#define CHR_SIZE                0x16a0
#define CHR_SLOTS               255
#define CHR_FLAGS(c)            (*(u32 *)((c) + 4))
#define CHR_DEAD                0x1
#define CHR_INUSE               0x8000
#define CHR_NOCORPSE            0x80000
#define CHR_CLASS(c)            (*(u8 **)((c) + 0xc))
#define CHR_TYPE(c)             (*(u32 *)((c) + 0x1e8))   /* 0 player */
#define CHR_POS(c)              ((float *)((c) + 0x148))
#define CHR_DEATH_TICK(c)       (*(u32 *)((c) + 0x1638))
#define TAG_ONCC                0x4f4e4343
#define TAG_ONWC                0x4f4e5743
#define ONCC_DEATH_NAME(o)      ((char *)(o) + 0xbec)
#define ONCC_DEATH_CLASS(o)     (*(u8 **)((o) + 0xc2c))
#define ONCC_DELETE_DELAY(o)    (*(u16 *)((o) + 0xc8a))
#define P3rGetParticleClass     ((u8 *(ONI_FC *)(const char *))0x45a030)
#define TMrInstance_GetDataPtr  ((u16 (ONI_FC *)(u32, const char *, void *))0x4232e0)
#define P3rCreateEffect         lockstep_p3_create_effect   /* 0x456030, choosing the effects stream (lockstep.c) */
#define ONrGameState_GetGameTime ((u32 (ONI_FC *)(void))0x4fc880)
#define AKrCollision_Sphere     ((u8 (ONI_FC *)(void *, const float *, const float *, u32, u32))0x441cb0)
#define AKgResultCount          (*(u16 *)0x6a906c)
#define AKgResults              ((const u8 *)0x6a9080)
#define AKcResultSize           0x48       /* +0x0c the quad's plane, +0x1c the contact point, +0x28 its quad */
#define FLOOR_SKIP              0x48800    /* quad flags a character's collision skips */

#define P3_DEF(c)               (*(u8 **)((c) + 0x48))
#define P3_NUM_ACTIONS(d)       (*(u16 *)((d) + 0x12))
#define P3_ACTIONS(d)           (*(u8 **)((d) + 0x2fc))
#define P3_EMITTERS(d)          (*(u8 **)((d) + 0x300))
#define P3_NUM_EMITTERS(d)      (*(u16 *)((d) + 0x14))
#define P3_EMITTER_SIZE         0x1bc
#define P3_EMITTER_SPEED_TYPE   0x60
#define P3_EMITTER_SPEED        0x14c      /* value 8, the Uniform speed */
#define P3_SPEED_UNIFORM        0
#define P3_ACTION_SIZE          0x1a8
#define P3_ACTION_VALUES        0xc8
#define P3_VALUE_SIZE           0x1c
#define P3_ACTION_SETVELOCITY   52
#define P3_VALUE_FLOAT          3

#define SCREAMER_CELL           "w9_scr_p01"
#define BALL_SHOOTER            "w5_sbg_e01"
#define BALL                    "w5_sbg_p01"
#define PLASMA_RIFLE            "w3_phr"
#define PLASMA_SHOOTER          "w3_phr_e01"
#define PLASMA_DELAY            55
#define BOMB_PARTICLE           "mad_e01"
#define SCORCH_DECAL            "d_SCORCH"
#define SCORCH_SCALE            12.0f      /* the decal's size against the class's 1 */
#define CLASSES_MAX             1024

static float *g_cell_speed, g_cell_stock;   /* the Screaming Cell's SetVelocity speed */
static u16 *g_plasma_delay, g_plasma_stock;
static float *g_ball_speed, g_ball_stock;   /* the Super Ball Gun's launch speed */
static struct { u8 *oncc, *death; char name[64]; u16 delay; } g_bombs[CLASSES_MAX];
static u32 g_nbombs;
static u8 *g_bomb_class;                    /* mad_e01, for this level */
static u8 *g_scorch;                        /* d_SCORCH */

/* ---- Screaming Cell ---- */

static float *cell_speed(void)
{
	u8 *cls = P3rGetParticleClass(SCREAMER_CELL), *def, *act;
	u32 n;

	if (!cls || !(def = P3_DEF(cls)) || !(act = P3_ACTIONS(def))) return 0;
	n = P3_NUM_ACTIONS(def);
	if (P3_EMITTERS(def) && P3_EMITTERS(def) != act + n * P3_ACTION_SIZE) {
		log_msg("mutators: %s's actions aren't laid out as expected (%u at %p, emitters at %p), left alone",
			SCREAMER_CELL, n, act, P3_EMITTERS(def));
		return 0;
	}
	for (u32 i = 0; i < n; i++) {
		u8 *a = act + i * P3_ACTION_SIZE;
		if (*(u32 *)a != P3_ACTION_SETVELOCITY) continue;
		u8 *v = a + P3_ACTION_VALUES;   /* value 0: speed */
		if (*(u32 *)v == P3_VALUE_FLOAT && *(float *)(v + 4) > 0) return (float *)(v + 4);
	}
	log_msg("mutators: no SetVelocity speed in %s, left alone", SCREAMER_CELL);
	return 0;
}

/* ---- Super Ball Gun ---- */

static float *ball_speed(void)
{
	u8 *cls = P3rGetParticleClass(BALL_SHOOTER), *def, *em;
	u32 n;

	if (!cls || !(def = P3_DEF(cls)) || !(em = P3_EMITTERS(def))) return 0;
	n = P3_NUM_EMITTERS(def);
	if (P3_ACTIONS(def) && em != P3_ACTIONS(def) + P3_NUM_ACTIONS(def) * P3_ACTION_SIZE) {
		log_msg("mutators: %s's emitters aren't laid out as expected (%u at %p), left alone", BALL_SHOOTER, n, em);
		return 0;
	}
	for (u32 i = 0; i < n && i < 16; i++) {
		u8 *e = em + i * P3_EMITTER_SIZE;
		if (!str_ieq_n((const char *)e, BALL, sizeof BALL)) continue;
		u8 *v = e + P3_EMITTER_SPEED;
		if (*(u32 *)(e + P3_EMITTER_SPEED_TYPE) == P3_SPEED_UNIFORM && *(u32 *)v == P3_VALUE_FLOAT &&
		    *(float *)(v + 4) > 0) return (float *)(v + 4);
	}
	log_msg("mutators: no uniform speed for %s in %s, left alone", BALL, BALL_SHOOTER);
	return 0;
}

/* ---- Plasma Rifle ---- */

static u16 *plasma_delay(void)
{
	u8 *w = 0;
	if (TMrInstance_GetDataPtr(TAG_ONWC, PLASMA_RIFLE, &w) || !w) return 0;
	for (u32 i = 0x30; i < 0x800; i += 4) {   /* the shooter attachment, by its class name */
		u8 *a = w + i;
		if (!str_ieq_n((const char *)a, PLASMA_SHOOTER, sizeof PLASMA_SHOOTER)) continue;
		if (*(u16 *)(a + 0x14) == 1 && *(u16 *)(a + 0x16) == PLASMA_DELAY) return (u16 *)(a + 0x16);
	}
	log_msg("mutators: %s's shooter (%s, %d frames) not found, left alone", PLASMA_RIFLE, PLASMA_SHOOTER, PLASMA_DELAY);
	return 0;
}

/* ---- Bombs ---- */

static void bombs_apply(void)
{
	u8 *mad = 0;
	u16 delay;
	u32 n = TMrInstance_GetTagCount(TAG_ONCC);

	if (TMrInstance_GetDataPtr(TAG_ONCC, "mad_bomber", &mad) || !mad ||
	    !str_ieq_n(ONCC_DEATH_NAME(mad), BOMB_PARTICLE, sizeof BOMB_PARTICLE) || !g_bomb_class) {
		log_msg("mutators: the Mad Bomber's death (%s) isn't there, no bombs", BOMB_PARTICLE);
		return;
	}
	delay = ONCC_DELETE_DELAY(mad);
	for (u32 k = 0; k < n && g_nbombs < CLASSES_MAX; k++) {
		u8 *p = 0;
		if (TMrInstance_GetDataPtr_ByNumber(TAG_ONCC, k, (void **)&p) || !p) continue;
		g_bombs[g_nbombs].oncc = p;
		g_bombs[g_nbombs].death = ONCC_DEATH_CLASS(p);
		memcpy(g_bombs[g_nbombs].name, ONCC_DEATH_NAME(p), 64);
		g_bombs[g_nbombs].delay = ONCC_DELETE_DELAY(p);
		g_nbombs++;
		str_cpy(ONCC_DEATH_NAME(p), 64, BOMB_PARTICLE);
		ONCC_DEATH_CLASS(p) = g_bomb_class;
		ONCC_DELETE_DELAY(p) = delay;
	}
	log_msg("mutators: Bombs: %d classes die as the Mad Bomber (%s, gone after %d ticks)", g_nbombs, BOMB_PARTICLE, delay);
}

/* ---- Level load and unload ---- */

void mutators_level_end(void)
{
	int any = g_cell_speed || g_ball_speed || g_plasma_delay || g_nbombs;

	if (g_cell_speed) *g_cell_speed = g_cell_stock;
	if (g_ball_speed) *g_ball_speed = g_ball_stock;
	if (g_plasma_delay) *g_plasma_delay = g_plasma_stock;
	for (u32 i = 0; i < g_nbombs; i++) {
		memcpy(ONCC_DEATH_NAME(g_bombs[i].oncc), g_bombs[i].name, 64);
		ONCC_DEATH_CLASS(g_bombs[i].oncc) = g_bombs[i].death;
		ONCC_DELETE_DELAY(g_bombs[i].oncc) = g_bombs[i].delay;
	}
	players_spawn_weapons(0);
	if (any) log_msg("mutators: the level's mutated data is as it was");
	g_cell_speed = 0;
	g_ball_speed = 0;
	g_plasma_delay = 0;
	g_nbombs = 0;
}

void mutators_level(void)
{
	mutators_level_end();   /* (a level end that never came) */
	g_bomb_class = P3rGetParticleClass(BOMB_PARTICLE);
	g_scorch = P3rGetParticleClass(SCORCH_DECAL);
	if (!multiplayer_map()) return;
	if (net_mutator(NET_MUT_NO_WEAPONS)) {
		players_spawn_weapons(1);
		log_msg("mutators: No Weapons: none in the map, players spawn without one");
	}
	if (net_mutator(NET_MUT_FAST_SCREAMER) && (g_cell_speed = cell_speed())) {
		g_cell_stock = *g_cell_speed;
		*g_cell_speed = g_cell_stock * 2;
		log_msg("mutators: Fast Screamer: the Screaming Cell flies at %d (was %d)", (int)*g_cell_speed, (int)g_cell_stock);
	}
	if (net_mutator(NET_MUT_FAST_BALL) && (g_ball_speed = ball_speed())) {
		g_ball_stock = *g_ball_speed;
		*g_ball_speed = g_ball_stock * 2;
		log_msg("mutators: Fast Ball: the Super Ball Gun's ball flies at %d (was %d)", (int)*g_ball_speed, (int)g_ball_stock);
	}
	if (net_mutator(NET_MUT_FAST_PLASMA) && (g_plasma_delay = plasma_delay())) {
		g_plasma_stock = *g_plasma_delay;
		*g_plasma_delay = (u16)((g_plasma_stock + 1) / 2);
		log_msg("mutators: Fast Plasma: the Plasma Rifle fires every %d frames (was %d)", *g_plasma_delay, g_plasma_stock);
	}
	if (net_mutator(NET_MUT_BOMBS)) bombs_apply();
}

/* ---- Bombs going off ---- */

static int bomb_class(const u8 *c)
{
	const u8 *o = CHR_CLASS(c);
	return g_bomb_class && o && ONCC_DEATH_CLASS(o) == g_bomb_class && ONCC_DELETE_DELAY(o) > 0;
}

/* Ticks since c died, if it's a dead bomb; -1 otherwise */
static int bomb_age(const u8 *c)
{
	u8 *gs = ONgGameState;
	if (!gs || !c || (CHR_FLAGS(c) & (CHR_INUSE | CHR_DEAD)) != (CHR_INUSE | CHR_DEAD) || !bomb_class(c)) return -1;
	return (int)(GS_TICK(gs) - CHR_DEATH_TICK(c));
}

int mutators_body_gone(const u8 *c)
{
	int age = bomb_age(c);
	return age >= 0 && CHR_TYPE(c) == 0 && age >= (int)ONCC_DELETE_DELAY(CHR_CLASS(c));
}

/* A black scorch on the floor under c (the decal the CreateDecal action would make) */
static void scorch(u8 *c)
{
	u8 spec[0x14], data[0x80];
	float sphere[4], down[3] = { 0, -60.0f, 0 };
	const u8 *hit = 0;
	u8 *gs = ONgGameState;

	if (!g_scorch || !gs || !*(u8 **)(gs + 0x150)) return;
	memcpy(sphere, CHR_POS(c), 12);
	sphere[1] += 6.0f;
	sphere[3] = 2.0f;
	if (!AKrCollision_Sphere(GS_COLLISION(gs), sphere, down, FLOOR_SKIP, 0xff)) return;
	for (u32 i = 0; i < AKgResultCount && i < 255; i++) {   /* the highest floor-facing quad */
		const u8 *r = AKgResults + i * AKcResultSize;
		if (((const float *)(r + 0x0c))[1] < 0.7f) continue;
		if (!hit || ((const float *)(r + 0x1c))[1] > ((const float *)(hit + 0x1c))[1]) hit = r;
	}
	if (!hit) return;
	memset(spec, 0, sizeof spec);
	*(u8 **)spec = g_scorch;
	*(u32 *)(spec + 8) = 4;            /* location: decal */
	spec[0xd] = 1;                     /* random rotation */
	memset(data, 0, sizeof data);
	*(u32 *)data = ONrGameState_GetGameTime();
	memcpy(data + 0x08, hit + 0x1c, 12);   /* position: the contact point */
	memcpy(data + 0x20, hit + 0x0c, 12);   /* normal: the quad's */
	((float *)(data + 0x2c))[2] = 1.0f;     /* up: +Z (a normal along Y is fine with it) */
	*(float *)(data + 0x38) = *(float *)(data + 0x3c) = SCORCH_SCALE;
	*(u32 *)(data + 0x54) = 1;         /* cause: a particle hit a wall */
	*(u32 *)(data + 0x58) = *(const u32 *)(hit + 0x28);   /* its quad */
	if (!P3rCreateEffect(spec, data))
		log_msg("mutators: no scorch for %s (the decal couldn't be made)", (const char *)c + 0x14);
}

void mutators_tick(void)
{
	u8 *gs = ONgGameState;
	if (!gs || !g_bomb_class) return;
	for (u32 i = 0; i < CHR_SLOTS; i++) {
		u8 *c = GS_CHARACTERS(gs) + i * CHR_SIZE;
		int age = bomb_age(c), delay;
		if (age < 0) continue;
		delay = ONCC_DELETE_DELAY(CHR_CLASS(c));
		if (age == delay - 1) {
			scorch(c);
			log_msg("mutators: %s blew up (tick %u)", (const char *)c + 0x14, GS_TICK(gs));
		}
		if (CHR_TYPE(c) == 0 && age >= delay) CHR_FLAGS(c) |= CHR_NOCORPSE;   /* no body left at the respawn */
	}
}
