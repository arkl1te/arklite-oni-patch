/* Barabas's regeneration for players: crouch, let go of crouch, then Action (the taunt key).
 *
 * Stock Oni only lets the AI regenerate: Barabas's combat behavior (0x491540, mode 6) asks for
 * his Powerup animation (type 0xdb: TRAMBARABpowerup_start from Standing, then the looping
 * TRAMBARABpowerup from state Powerup 0x44) every update, after fight mode (0x4eb0b0, ECX the
 * character), with ONrCharacter_DoAnimation (0x4e4400: ECX character, EDX its active data,
 * stack priority 1 "appropriate" and the type; ret 8), and heals him while the loop plays
 * (IsPoweringUp 0x4e3ab0, ECX the character). The animation itself heals nothing and no input
 * asks for it.
 *
 * Start: the Action key goes to the taunt handler (0x4f80d0: ECX the input, EDX/ESI the
 * character, stack/EDI its active data) after consoles, doors and neutral characters had their
 * turn; it asks for the taunt with DoTableAnimation (0x4fa1c0: ECX input, EDX character, stack
 * active, the move table, 1, 0, 0; ret 0x14; call at 0x4f82b4). There the call goes through
 * taunt_call: a player (charType +0x1e8 == 0) whose next animation state (active +0x1afc) is
 * CrouchStart (0x2f, just after letting go of crouch, where the heavy punch and kick are looked
 * up) and whose class has a Powerup animation from Standing (the class's collection, ONCC
 * +0xc80, asked through the game's lookup 0x427f00: ECX collection, EDX type, stack state and
 * variant, ret 8) starts regenerating instead. No class but Barabas has one; nobody has a taunt
 * from CrouchStart, so the key did nothing there before.
 *
 * While it's on (regen_input, from players.c for every player's tick input, before
 * ONrCharacter_SetInput copies it into the character):
 *   - fight mode and the Powerup animation are asked for every tick, as the AI does. The same
 *     animation playing, a hit, a throw or a knockdown only queue it (0x4e4400's priority), so
 *     the loop never restarts and nothing is cut short; it comes back after a light hit.
 *   - while the loop plays, health rises 8% of the maximum a second (the AI's rate, 0x491540:
 *     max * 8 / 1200 per 5-tick update), counted in 1/6000ths so a 100-point player gets 8 a
 *     second, up to the maximum (never lowering an overheal). At the maximum the healing stops
 *     and the pose goes on.
 *   - it ends on any action input: a movement, jump, crouch, punch, kick, block, hypo, reload,
 *     swap, drop or fire key pressed or held (input +0x10 held, +0x20 pressed), or Action pressed
 *     again; or when the hits taken since it started reach 5% of the maximum, the AI's limit
 *     (AI2cBarabbas_RegenDamageStopFraction); or on death, a class change or a level change.
 *   - the body can't turn but the camera can: the input given to the character holds Look
 *     (bit 15; 0x4f7b90 then turns the aim's yaw, active aimingLR, by the mouse instead of the
 *     facing, up to 90 degrees, and the follow camera adds it), the turn keys (23, 24) are taken
 *     out, and the desired facing (+0x170) was set to the facing (+0x16c) at the start, so no
 *     turn in progress finishes. When it ends Look is up again and the game's look-spring brings
 *     the camera back behind him.
 *
 * Everything is decided in the tick from the tick's input, and the state is TICK_STATE, so every
 * PC, replay, rollback and join in progress does the same. */
#include "arklite.h"

void *memcpy(void *, const void *, unsigned);

#define ONgGameState            (*(u8 **)0x5ece7c)
#define GS_TICK(gs)             (*(u32 *)((gs) + 0x144))
#define GS_ACTIVE_STOCK         0xb6268
#define ACTIVE_SIZE             0x2838

#define CHR_ACTIVE(c)           (*(u16 *)((c) + 2))
#define CHR_FLAGS(c)            (*(u32 *)((c) + 4))
#define CHR_DEAD                0x1
#define CHR_CLASS(c)            (*(u8 **)((c) + 0xc))
#define CHR_HEALTH(c)           (*(int *)((c) + 0x38))
#define CHR_MAX_HEALTH(c)       (*(int *)((c) + 0x3c))
#define CHR_FACING(c)           (*(float *)((c) + 0x16c))
#define CHR_DESIRED_FACING(c)   (*(float *)((c) + 0x170))
#define CHR_TYPE(c)             (*(u32 *)((c) + 0x1e8))   /* 0 player */
#define ONCC_ANIMATIONS(o)      (*(u8 **)((o) + 0xc80))
#define ACT_NEXT_STATE(a)       (*(u16 *)((a) + 0x1afc))

#define ANIM_POWERUP            0xdb
#define STATE_STANDING          0x07
#define STATE_CROUCH_START      0x2f
#define PRIORITY_APPROPRIATE    1

#define ONrCharacter_DoAnimation ((void *(ONI_FC *)(u8 *, u8 *, u32, u32))0x4e4400)
#define ONrCharacter_FightMode  ((void (ONI_FC *)(u8 *))0x4eb0b0)
#define ONrCharacter_IsPoweringUp ((u8 (ONI_FC *)(u8 *))0x4e3ab0)
#define TRrCollection_Lookup    ((void *(ONI_FC *)(u8 *, u32, u32, u32))0x427f00)
#define DoTableAnimation_Addr   0x4fa1c0
#define DoTableAnimation        ((u32 (ONI_FC *)(u32 *, u8 *, u8 *, void *, u32, u32, u32))DoTableAnimation_Addr)
#define ONcTaunt_TableCall      0x4f82b4

/* Input state words (u32): [4],[5] held, [6],[7] not held, [8],[9] pressed, [10],[11] released */
#define BIT(n)                  (1u << ((n) & 31))
#define LOOK_LO                 BIT(15)
#define TURN_LO                 (BIT(23) | BIT(24))
/* forward 21, backward 22, step left/right 25/26, jump 27, crouch 28, punch 29, kick 30, block 31 */
#define ACT_LO                  (BIT(21) | BIT(22) | BIT(25) | BIT(26) | BIT(27) | BIT(28) | BIT(29) | BIT(30) | BIT(31))
/* action 33, hypo 34, reload 35, swap 36, drop 37, fire1-3 38-40 (walk 32 alone does nothing) */
#define ACTION_HI               BIT(33)
#define ACT_HI                  (BIT(33) | BIT(34) | BIT(35) | BIT(36) | BIT(37) | BIT(38) | BIT(39) | BIT(40))

#define HEAL_PER_SECOND         8      /* % of the maximum */
#define HEAL_UNIT               6000   /* 100 % * 60 ticks */
#define STOP_DAMAGE_PERCENT     5

typedef struct {
	u8 *chr;          /* 0: free */
	u8 *oncc;         /* its class when it started */
	u32 tick;         /* the last tick it ran in */
	int last_health;  /* after the last tick's heal */
	u32 taken;        /* hit points lost since the start */
	u32 heal_acc;     /* in 1/HEAL_UNIT of a hit point */
} Regen;

#define MAX_REGEN 8
static Regen g_regen[MAX_REGEN] TICK_STATE;

static int g_installed;

static u8 *active_of(u8 *chr)
{
	u16 a = CHR_ACTIVE(chr);
	if (!a) return 0;
	return ONgGameState + char_limit_gs_offset(GS_ACTIVE_STOCK) + (a & 0xff) * ACTIVE_SIZE;
}

static Regen *find(u8 *chr)
{
	for (int i = 0; i < MAX_REGEN; i++)
		if (g_regen[i].chr == chr) return &g_regen[i];
	return 0;
}

/* The class has a Powerup animation from Standing (a pure function of the level's data: a
 * cache outside the tick state is fine) */
static int can_regenerate(u8 *oncc)
{
	static u8 *last_oncc;
	static int last_result;
	u8 *trac;
	if (!oncc) return 0;
	if (oncc == last_oncc) return last_result;
	trac = ONCC_ANIMATIONS(oncc);
	last_oncc = oncc;
	last_result = trac && TRrCollection_Lookup(trac, ANIM_POWERUP, STATE_STANDING, 0xffff) != 0;
	return last_result;
}

static void ask_powerup(u8 *chr, u8 *active)
{
	ONrCharacter_FightMode(chr);
	ONrCharacter_DoAnimation(chr, active, PRIORITY_APPROPRIATE, ANIM_POWERUP);
}

static int start(u8 *chr, u8 *active)
{
	Regen *r;
	if (!chr || !active || CHR_TYPE(chr) != 0 || (CHR_FLAGS(chr) & CHR_DEAD) || CHR_HEALTH(chr) <= 0) return 0;
	if (ACT_NEXT_STATE(active) != STATE_CROUCH_START || !can_regenerate(CHR_CLASS(chr))) return 0;
	if (!(r = find(chr)) && !(r = find(0))) return 0;
	r->chr = chr;
	r->oncc = CHR_CLASS(chr);
	r->tick = GS_TICK(ONgGameState);
	r->last_health = CHR_HEALTH(chr);
	r->taken = 0;
	r->heal_acc = 0;
	CHR_DESIRED_FACING(chr) = CHR_FACING(chr);
	ask_powerup(chr, active);   /* from CrouchStart nothing matches yet; it starts once he stands */
	return 1;
}

static u32 ONI_FC taunt_call(u32 *input, u8 *chr, u8 *active, void *table, u32 priority, u32 a, u32 b)
{
	if (start(chr, active)) return 1;
	return DoTableAnimation(input, chr, active, table, priority, a, b);
}

u32 *regen_input(u8 *chr, u32 *in, u32 *copy)
{
	Regen *r;
	u8 *active;
	u32 tick;
	int h, max;

	if (!g_installed || !chr || !in || !(r = find(chr))) return in;
	tick = GS_TICK(ONgGameState);
	active = active_of(chr);
	h = CHR_HEALTH(chr);
	max = CHR_MAX_HEALTH(chr);
	if (!active || CHR_TYPE(chr) != 0 || (CHR_FLAGS(chr) & CHR_DEAD) || h <= 0 || max <= 0 ||
	    CHR_CLASS(chr) != r->oncc || (tick != r->tick && tick != r->tick + 1)) {
		r->chr = 0;
		return in;
	}
	r->tick = tick;

	/* hits since the last tick (an overheal draining down to the maximum isn't one) */
	if (h < r->last_health) {
		int from = r->last_health > max ? max : r->last_health;
		if (h < from) r->taken += (u32)(from - h);
	}
	if (r->taken * 100 >= (u32)max * STOP_DAMAGE_PERCENT ||
	    (in[8] & ACT_LO) || (in[9] & ACT_HI) || (in[4] & ACT_LO) || (in[5] & ACT_HI & ~ACTION_HI)) {
		r->chr = 0;
		return in;
	}

	if (ONrCharacter_IsPoweringUp(chr) && h < max) {
		u32 n;
		r->heal_acc += (u32)max * HEAL_PER_SECOND;
		n = r->heal_acc / HEAL_UNIT;
		r->heal_acc -= n * HEAL_UNIT;
		h += (int)n;
		if (h > max) h = max;
		CHR_HEALTH(chr) = h;
	}
	r->last_health = CHR_HEALTH(chr);
	ask_powerup(chr, active);

	/* the body stays, the mouse turns only the camera */
	memcpy(copy, in, 12 * sizeof(u32));
	copy[4] = (copy[4] | LOOK_LO) & ~TURN_LO;
	copy[6] = (copy[6] & ~LOOK_LO) | TURN_LO;
	copy[8] &= ~(LOOK_LO | TURN_LO);
	copy[10] &= ~(LOOK_LO | TURN_LO);
	return copy;
}

void regen_install(void)
{
	if (!patch_call(ONcTaunt_TableCall, DoTableAnimation_Addr, (u32)taunt_call)) {
		log_msg("regen: 0x%x changed, no player regeneration", ONcTaunt_TableCall);
		return;
	}
	g_installed = 1;
	log_msg("regen: crouch, let go of crouch, Action regenerates characters with a Powerup animation (Barabas)");
}
