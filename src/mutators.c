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
 * when they respawn (NoCorpse).
 *
 * Jamming Guns (NET_MUT_JAMMING): a player's shot jams the gun JAM_CHANCE (5%) of the time, but a
 * gun rolls at most once in each JAM_ROLL_TICKS (300 ms) window of the game's clock: the window's
 * first shot rolls, its other shots don't (user, 2026-11-16: "every 300 ms, 5% chance the next
 * bullet jams"). A slow gun has the 5% on every shot, a fast one 5% per 300 ms of firing, and
 * holding the gun without firing counts for nothing (2026-11-19; from 2026-11-16 a roll every 300 ms
 * while a player held the gun armed its next shot, so the odds piled up while it wasn't fired and
 * most first shots jammed; before that, every shot had the 5%). Every shot passes through WPiPulseShooter (0x519c50: ECX weapon, DX shooter
 * index, the shooter attachment on the stack, ret 4), which spends the ammo: from the trigger's
 * first pull (the Start event in WPiDoEvent, 0x51a104), from an autofire weapon's held trigger
 * (WPiFireShooter, 0x51a354) and from the update's continuous fire (WPiUpdate, 0x5194e6). Those
 * three calls come here. A jamming shot isn't fired and its round stays in the gun, and the
 * trigger lets go at the weapon's next update (flag DeferUpdate_StopFiring 0x4000; the event
 * loop that called us mustn't get Stop events in the middle). The jam asks for 1 to 3 reloads and,
 * JAM_DROP_CHANCE (10%) of the time, 1 drop (user, 2026-11-16; it was 1-3 drops and 0-1 reload; the
 * drop was 50% until the user asked for far fewer, 2026-11-19), in any order; an unreloadable
 * class (flag 0x800) 1 to 3 drops and no reload. A drop counts when the
 * dropped gun touches the ground (weapon flag HitGround 0x2, which picking it up clears), a reload
 * when the reload animation reloads it (WPrNotifyReload 0x5181e0, called once at 0x4dfb7d).
 * Reloading a jammed gun that wants its reload only clears the jam: it works with a full magazine and
 * with no clip in the inventory, spends no clip, and leaves the rounds as they were (the reload
 * section below); one that only wants drops reloads as any gun does. While
 * it's jammed, WPrDepressTrigger (0x51a1b0, from the character's fire at 0x4e912f and the update's
 * forced autofire at 0x5193cb) doesn't fire, and the character's pull clicks (WPrOutOfAmmoSound
 * 0x51d080) at most every JAM_CLICK_TICKS. The jam stays with the gun, whoever picks it up; only
 * players' shots jam it. The table is tick state, keyed by the weapon's slot and birth time, and
 * the dice are a hash of the tick, the weapon and the game's RNG seed, so every PC, replay and
 * rollback gets the same jams without drawing from the game's RNG. Weapon (0x170 bytes, at
 * 0x6221c0): +2 flags u16, +4 class, +8 holder, +0x48 birth, +0x56 ammo; class: +0xc2 attachment
 * count, +0xcc flags; character: +4 flags (0x40 infinite ammo), +0x194 the weapon in hand.
 *
 * Reloads (the reload section): WPrTryReload (0x51c560: ECX the character, EDX the weapon, two
 * out-flag pointers on the stack, ret 8; called at 0x4ddea4, the reload key, and 0x4e916b, the
 * trigger on an empty gun) turns back a full gun (ammo +0x56 equal to the class's +0xc0) and one
 * whose holder has no clip for it (ONrCharacter_HasAmmoForReload 0x4edc90 at 0x51c60c and
 * 0x51c673), then spends the clip (WPrPowerup_Use 0x51c770: ECX the character, EDX the ammo type,
 * 0 ballistic or 1 energy, called at 0x51c704) and starts the reload animation on the inventory
 * overlay (ONrOverlay_Set 0x4d7b80 at 0x51c716: ECX active +0x1c64, EDX the animation, flags on
 * the stack, ret 4). The rounds come at the animation's action frame (WPrNotifyReload at 0x4dfb7d),
 * and only if the gun is still in hand.
 *   - A jammed gun that wants a reload (Jamming Guns): its full magazine reads as empty during the
 *     call, the clip check says yes and no clip is spent; at the action frame its rounds are put
 *     back as they were.
 *   - Interrupted reloads, on a multiplayer map (user, 2026-11-16): stock Oni loses the clip a
 *     reload spent when the animation never reaches its action frame (a knockdown clears the
 *     overlay, a disarm or drop takes the gun, a holster replaces the animation, death). Each
 *     reload that spent a clip is noted (g_reloads, tick state: character, gun, animation, ammo
 *     type); the action frame clears it, and every tick (mutators_tick) one whose character is
 *     dead, has another gun in hand, or another animation (or none) on the overlay drops that
 *     clip as a power-up (WPrPowerup_Drop 0x51bd80: ECX type, EDX amount, then the character and
 *     "in front" 0, ret 8: as a death drops its items, with the last hit's push), so it can be
 *     picked up again. In the tick, from the game's RNG: every PC drops the same clip. */
#include "arklite.h"

void *memset(void *, int, unsigned);
void *memcpy(void *, const void *, unsigned);

#define ONgGameState            (*(u8 **)0x5ece7c)
#define GS_TICK(gs)             (*(u32 *)((gs) + 0x144))
#define GS_CHARACTERS(gs)       ((gs) + 0x1260)
#define GS_COLLISION(gs)        (*(u8 **)(*(u8 **)((gs) + 0x150) + 0x40))
#define CHR_SIZE                0x16a0
#define CHR_SLOTS               255   /* the most, with char_limit.c's raise */
#define ONgCharacterSlots       (*(u16 *)(0x4dac95 + 3))   /* the slots there are now: 128 stock, 255 raised (char_limit.c) */
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

/* ---- Jamming Guns: Oni's weapons ---- */

#define WP_WEAPONS              ((u8 *)0x6221c0)
#define WP_WEAPON_SIZE          0x170
#define WP_WEAPON_SLOTS         64
#define WEAPON_FLAGS(w)         (*(u16 *)((w) + 2))
#define WEAPON_INUSE            0x1
#define WEAPON_HIT_GROUND       0x2
#define WEAPON_BACKGROUND_FX    0x20
#define WEAPON_DEFER_STOP       0x4000
#define WEAPON_CLASS(w)         (*(u8 **)((w) + 4))
#define WEAPON_HOLDER(w)        (*(u8 **)((w) + 8))
#define WEAPON_BORN(w)          (*(u32 *)((w) + 0x48))
#define WEAPON_AMMO(w)          (*(u16 *)((w) + 0x56))
#define WCLASS_ATTACHMENTS(k)   (*(u16 *)((k) + 0xc2))
#define WCLASS_FLAGS(k)         (*(u32 *)((k) + 0xcc))
#define WCLASS_UNRELOADABLE     0x800
#define ATTACH_AMMO_USED(a)     (*(u16 *)((a) + 0x44))
#define CHR_INFINITE_AMMO       0x40
#define CHR_WEAPON(c)           (*(u8 **)((c) + 0x194))
#define EVENT_BACKGROUND_STOP   5
#define UUgRandomSeed           (*(u32 *)0x5326d4)

#define WPiPulseShooter         0x519c50
#define WPiDoEvent              ((void (ONI_FC *)(u8 *, u32, u32))0x519f70)
#define WPrDepressTrigger       0x51a1b0
#define WPrNotifyReload         0x5181e0
#define WPrOutOfAmmoSound       ((void (ONI_FC *)(u8 *))0x51d080)
#define WCLASS_MAX_AMMO(k)      (*(u16 *)((k) + 0xc0))

#define JAM_CHANCE              5     /* percent, each roll */
#define JAM_ROLL_TICKS          18    /* a gun rolls at most once in each 300 ms window */
#define JAM_DROP_CHANCE         10    /* percent of jams that also want a drop (reloadable guns) */
#define JAM_CLICK_TICKS         15    /* a held trigger clicks this often */

typedef struct {
	u32 born;           /* the weapon's birth time: another weapon in the slot isn't jammed (or rolled) */
	u8 jammed, held;    /* held: in a hand since its last drop counted */
	u8 drops, reloads;  /* still needed */
	u32 click;          /* the tick of its last click */
	u32 window;         /* 1 + the JAM_ROLL_TICKS window of the gun's last roll (0: none yet) */
} Jam;

static Jam g_jam[WP_WEAPON_SLOTS] TICK_STATE;
static int g_jam_on;    /* this level has the mutator (set at its load, the same on every PC) */

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

/* ---- Jamming Guns ---- */

static u32 mix(u32 h)
{
	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	h *= 0x846ca68bu;
	h ^= h >> 16;
	return h;
}

/* Weapon w's entry, or 0 if w isn't one of Oni's weapon slots */
static Jam *jam_entry(const u8 *w)
{
	u32 off = (u32)(w - WP_WEAPONS);
	if (w < WP_WEAPONS || off >= WP_WEAPON_SLOTS * WP_WEAPON_SIZE || off % WP_WEAPON_SIZE) return 0;
	return &g_jam[off / WP_WEAPON_SIZE];
}

/* w's jam, if it's jammed (an entry left by another weapon in the slot isn't w's) */
static Jam *jam_of(const u8 *w)
{
	Jam *j = g_jam_on && w ? jam_entry(w) : 0;
	return j && j->jammed && j->born == WEAPON_BORN(w) ? j : 0;
}

static const char *weapon_name(const u8 *w)
{
	const char *n = TMrInstance_GetInstanceName(WEAPON_CLASS(w));
	return n ? n : "a weapon";
}

/* The drops and reloads asked for are done: the gun works again */
static void jam_check(const u8 *w, Jam *j)
{
	if (j->drops || j->reloads) return;
	j->jammed = 0;
	if (!net_rerun()) log_msg("mutators: %s works again (tick %u)", weapon_name(w), GS_TICK(ONgGameState));
}

/* The shot jams the gun: it isn't fired, and its round stays (a reload clears the jam, not the
 * magazine: see "Reloads") */
static void jam_start(u8 *w, Jam *j, const u8 *owner, u32 r)
{
	u8 *k = WEAPON_CLASS(w);

	j->born = WEAPON_BORN(w);
	j->jammed = 1;
	j->held = 1;
	if (WCLASS_FLAGS(k) & WCLASS_UNRELOADABLE) {   /* nothing to reload: drops alone, as many as before */
		j->drops = (u8)(1 + mix(r ^ 0x68e31da4u) % 3);
		j->reloads = 0;
	} else {
		j->reloads = (u8)(1 + mix(r ^ 0xb5297a4du) % 3);
		j->drops = (u8)(mix(r ^ 0x68e31da4u) % 100 < JAM_DROP_CHANCE);
	}
	j->click = GS_TICK(ONgGameState);
	WPrOutOfAmmoSound(w);
	if (!net_rerun())
		log_msg("mutators: %s's %s jammed (tick %u): %d drop%s and %d reload(s) to fix it, %d rounds left",
		        (const char *)owner + 0x14, weapon_name(w), GS_TICK(ONgGameState), j->drops, j->drops == 1 ? "" : "s",
		        j->reloads, WEAPON_AMMO(w));
}

/* Every shot (WPiPulseShooter's three calls) */
static void ONI_FC pulse_hook(u8 *w, u32 shooter, u8 *attachment)
{
	u8 *owner = WEAPON_HOLDER(w), *gs = ONgGameState;
	Jam *j = g_jam_on && owner && gs ? jam_entry(w) : 0;

	if (j) {
		if (jam_of(w)) {   /* (a shot the trigger's block didn't stop: the update's continuous fire) */
			WEAPON_FLAGS(w) |= WEAPON_DEFER_STOP;
			return;
		}
		u32 window = GS_TICK(gs) / JAM_ROLL_TICKS + 1;
		if (j->born != WEAPON_BORN(w)) {   /* another weapon in the slot: it hasn't rolled yet */
			j->born = WEAPON_BORN(w);
			j->window = 0;
		}
		if (CHR_TYPE(owner) == 0 && j->window != window) {   /* a player's shot, the gun's first in this window */
			u32 slot = (u32)(j - g_jam);
			u32 r = mix(GS_TICK(gs) * 0x9e3779b9u ^ mix(UUgRandomSeed ^ slot << 24 ^ WEAPON_BORN(w) ^ (u32)WEAPON_AMMO(w) << 8));
			j->window = window;
			if (mix(r ^ 0x3c6ef372u) % 100 < JAM_CHANCE) {
				jam_start(w, j, owner, r);
				WEAPON_FLAGS(w) |= WEAPON_DEFER_STOP;   /* the trigger lets go at the weapon's next update */
				return;
			}
		}
	}
	((void (ONI_FC *)(u8 *, u32, u8 *))WPiPulseShooter)(w, shooter, attachment);
}

/* The trigger: a jammed gun with rounds in it doesn't fire, and the character's pull clicks. An
 * empty one goes on to Oni, which can't fire it either and reloads it as for any empty gun. */
static u32 ONI_FC depress_hook(u8 *w, u32 forced, u32 alternate, u8 *no_ammo)
{
	Jam *j = jam_of(w);
	u8 *gs = ONgGameState;

	if (j && WEAPON_AMMO(w)) {
		if (no_ammo) *no_ammo = 0;
		if (!(forced & 0xff) && gs && GS_TICK(gs) - j->click >= JAM_CLICK_TICKS) {
			j->click = GS_TICK(gs);
			WPrOutOfAmmoSound(w);
		}
		return 0;
	}
	return ((u32 (ONI_FC *)(u8 *, u32, u32, u8 *))WPrDepressTrigger)(w, forced, alternate, no_ammo);
}

static void reload_done(const u8 *w);

/* The reload animation reloaded the weapon in hand (a jammed one: only the jam, its rounds stay) */
static void ONI_FC reload_hook(u8 *w)
{
	Jam *j = jam_of(w);
	u16 ammo = WEAPON_AMMO(w);

	((void (ONI_FC *)(u8 *))WPrNotifyReload)(w);
	reload_done(w);
	if (j && j->reloads) WEAPON_AMMO(w) = ammo;   /* (a jam that only wants drops: a reload is an ordinary one) */
	if (j && j->reloads) {
		j->reloads--;
		if (!net_rerun()) log_msg("mutators: %s reloaded while jammed (%d reload(s) and %d drop%s to go; %d rounds in it, %d clips left)",
		                          weapon_name(w), j->reloads, j->drops, j->drops == 1 ? "" : "s", WEAPON_AMMO(w),
		                          WEAPON_HOLDER(w) ? *(u16 *)(WEAPON_HOLDER(w) + 0x1a2) : -1);
		jam_check(w, j);
	}
}

/* Every tick: a jammed gun that was let go counts a drop when it touches the ground */
static void jams_tick(void)
{
	for (u32 i = 0; i < WP_WEAPON_SLOTS; i++) {
		Jam *j = &g_jam[i];
		u8 *w = WP_WEAPONS + i * WP_WEAPON_SIZE;
		if (!j->jammed) continue;
		if (!(WEAPON_FLAGS(w) & WEAPON_INUSE) || WEAPON_BORN(w) != j->born) {   /* the weapon is gone */
			j->jammed = 0;
			continue;
		}
		if (WEAPON_HOLDER(w)) j->held = 1;
		else if (j->held && (WEAPON_FLAGS(w) & WEAPON_HIT_GROUND)) {
			j->held = 0;
			if (j->drops) j->drops--;
			if (!net_rerun()) log_msg("mutators: %s dropped while jammed (%d drop%s and %d reload(s) to go)", weapon_name(w),
			                          j->drops, j->drops == 1 ? "" : "s", j->reloads);
			jam_check(w, j);
		}
	}
}

/* The HUD (mp_hud.c): character c's gun in hand is jammed; what it still needs */
int mutators_jammed(const u8 *c, int *drops, int *reloads)
{
	Jam *j = c ? jam_of(CHR_WEAPON(c)) : 0;
	if (!j) return 0;
	*drops = j->drops;
	*reloads = j->reloads;
	return 1;
}

/* ---- Reloads (see the top: Jamming Guns' jammed guns, and interrupted reloads) ---- */

#define WPrTryReload            0x51c560
#define ONrCharacter_HasAmmoForReload 0x4edc90
#define WPrPowerup_Use          0x51c770
#define ONrOverlay_Set          0x4d7b80
#define WPrPowerup_Drop         ((u8 *(ONI_FC *)(u32, u32, u8 *, u32))0x51bd80)
#define ONrCharacter_GetActive  ((u8 *(ONI_FC *)(u8 *))0x4f1180)
#define ACT_INVENTORY_ANIMATION(a) (*(void **)((a) + 0x1c64))   /* the inventory overlay's animation, 0: none */
#define RELOADS_MAX             32

typedef struct {
	u8 *c, *weapon;     /* c 0: a free entry */
	void *animation;    /* the reload's, on the inventory overlay */
	u32 type;           /* the clip's power-up type: 0 ballistic, 1 energy */
} Reload;
static Reload g_reloads[RELOADS_MAX] TICK_STATE;
static u8 *g_jam_reload;     /* the jammed gun WPrTryReload runs for (within one call) */
static Reload *g_reload_new; /* the entry WPrPowerup_Use just made, for ONrOverlay_Set's animation (within one call) */
static int g_reloads_ok;     /* every reload call was as expected */

typedef u8 (ONI_FC *TryReloadFn)(u8 *, u8 *, u8 *, u8 *);

static u8 ONI_FC try_reload_hook(u8 *c, u8 *w, u8 *full, u8 *no_ammo)
{
	Jam *j = jam_of(w);
	u16 ammo;
	u8 r;

	if (!j || !j->reloads || (WCLASS_FLAGS(WEAPON_CLASS(w)) & WCLASS_UNRELOADABLE)) return ((TryReloadFn)WPrTryReload)(c, w, full, no_ammo);
	ammo = WEAPON_AMMO(w);
	if (ammo == WCLASS_MAX_AMMO(WEAPON_CLASS(w))) WEAPON_AMMO(w) = 0;   /* not "full" (only the full check reads it) */
	g_jam_reload = w;
	r = ((TryReloadFn)WPrTryReload)(c, w, full, no_ammo);
	g_jam_reload = 0;
	WEAPON_AMMO(w) = ammo;
	return r;
}

static u8 ONI_FC has_clip_hook(u8 *c, u8 *w)
{
	if (g_jam_reload) return 1;   /* a jammed gun's reload needs no clip */
	return ((u8 (ONI_FC *)(u8 *, u8 *))ONrCharacter_HasAmmoForReload)(c, w);
}

static u8 ONI_FC clip_use_hook(u8 *c, u32 type)
{
	u8 r;

	g_reload_new = 0;
	if (g_jam_reload) return 1;   /* ...and spends none */
	r = ((u8 (ONI_FC *)(u8 *, u32))WPrPowerup_Use)(c, type);
	if (!r || !g_reloads_ok || !multiplayer_map() || (type != 0 && type != 1)) return r;
	for (u32 i = 0; i < RELOADS_MAX; i++) {
		Reload *e = &g_reloads[i];
		if (e->c && e->c != c) continue;
		e->c = c;
		e->weapon = CHR_WEAPON(c);
		e->animation = 0;
		e->type = type;
		g_reload_new = e;
		return r;
	}
	if (!net_rerun()) log_msg("mutators: %d reloads at once: %s's clip isn't kept if it's interrupted", RELOADS_MAX, (const char *)c + 0x14);
	return r;
}

static void ONI_FC overlay_hook(void **overlay, void *animation, u32 flags)
{
	((void (ONI_FC *)(void **, void *, u32))ONrOverlay_Set)(overlay, animation, flags);
	if (g_reload_new) g_reload_new->animation = animation;
	g_reload_new = 0;
}

/* The action frame reloaded w: its clip is in */
static void reload_done(const u8 *w)
{
	for (u32 i = 0; i < RELOADS_MAX; i++)
		if (g_reloads[i].c && g_reloads[i].weapon == w) memset(&g_reloads[i], 0, sizeof g_reloads[i]);
}

/* Every tick: a reload that can no longer reach its action frame drops its clip */
static void reloads_tick(void)
{
	for (u32 i = 0; i < RELOADS_MAX; i++) {
		Reload *e = &g_reloads[i];
		u8 *c = e->c, *a;
		const char *why;

		if (!c) continue;
		if (!(CHR_FLAGS(c) & CHR_INUSE)) {   /* the character is gone (the clip with it) */
			memset(e, 0, sizeof *e);
			continue;
		}
		a = ONrCharacter_GetActive(c);
		if (CHR_FLAGS(c) & CHR_DEAD) why = "died";
		else if (CHR_WEAPON(c) != e->weapon) why = "lost the gun";
		else if (!a || !e->animation || ACT_INVENTORY_ANIMATION(a) != e->animation) why = "was stopped";
		else continue;
		WPrPowerup_Drop(e->type, 1, c, 0);
		if (!net_rerun())
			log_msg("mutators: %s %s while reloading: the %s drops (tick %u)", (const char *)c + 0x14, why,
			        e->type ? "energy cell" : "ammo clip", GS_TICK(ONgGameState));
		memset(e, 0, sizeof *e);
	}
}

void mutators_install(void)
{
	static const u32 pulses[] = { 0x5194e6, 0x51a104, 0x51a354 };
	int ok = 1;

	for (u32 i = 0; i < 3; i++) ok &= patch_call(pulses[i], WPiPulseShooter, (u32)pulse_hook);
	ok &= patch_call(0x4e912f, WPrDepressTrigger, (u32)depress_hook);
	ok &= patch_call(0x5193cb, WPrDepressTrigger, (u32)depress_hook);
	ok &= patch_call(0x4dfb7d, WPrNotifyReload, (u32)reload_hook);
	ok &= patch_call(0x4ddea4, WPrTryReload, (u32)try_reload_hook);
	ok &= patch_call(0x4e916b, WPrTryReload, (u32)try_reload_hook);
	ok &= patch_call(0x51c60c, ONrCharacter_HasAmmoForReload, (u32)has_clip_hook);
	ok &= patch_call(0x51c673, ONrCharacter_HasAmmoForReload, (u32)has_clip_hook);
	g_reloads_ok = patch_call(0x51c704, WPrPowerup_Use, (u32)clip_use_hook);
	g_reloads_ok &= patch_call(0x51c716, ONrOverlay_Set, (u32)overlay_hook);
	if (!ok) log_msg("mutators: some of Jamming Guns' calls weren't as expected (see the patch lines above)");
	if (g_reloads_ok) log_msg("mutators: an interrupted reload drops its clip on multiplayer maps");
	else log_msg("mutators: the reload's clip calls weren't as expected: interrupted reloads lose their clip as in stock Oni");
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
	g_jam_on = 0;
	if (any) log_msg("mutators: the level's mutated data is as it was");
	g_cell_speed = 0;
	g_ball_speed = 0;
	g_plasma_delay = 0;
	g_nbombs = 0;
}

void mutators_level(void)
{
	mutators_level_end();   /* (a level end that never came) */
	memset(g_reloads, 0, sizeof g_reloads);   /* (every level: the last one's characters are gone) */
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
	memset(g_jam, 0, sizeof g_jam);
	if (net_mutator(NET_MUT_JAMMING)) {
		g_jam_on = 1;
		log_msg("mutators: Jamming Guns: a player's shot jams the gun %d%% of the time, at most one roll a gun every %d ticks; "
		        "%d%% of jams also want a drop", JAM_CHANCE, JAM_ROLL_TICKS, JAM_DROP_CHANCE);
	}
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
	u32 slots = ONgCharacterSlots;
	if (gs && g_jam_on) jams_tick();
	if (gs) reloads_tick();
	if (!gs || !g_bomb_class) return;
	/* the live count, as lockstep.c and photo.c use: char_limit.c skips its raise when something
	 * else changed its sites, and slots 128.. are then not characters (a crash after a death) */
	if (slots > CHR_SLOTS) slots = CHR_SLOTS;
	for (u32 i = 0; i < slots; i++) {
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
