/* Multiplayer map rules: players can hurt each other (free for all), damage x1.25 and
 * overhealing drains a quarter faster.
 *
 * Only on a multiplayer map (two or more Player-flagged characters, multiplayer_map in
 * players.c), which every PC and every replay agrees on.
 *
 * Damage: every hit on a character goes through ONrCharacter_TakeDamage (0x4e65f0; ECX the
 * character, EDX the damage in hit points, five stack arguments, ret 0x14), from 11 call sites:
 * melee, bullets and particles, falling, and the kills that pass the health itself as damage.
 * Its first 5 bytes (sub esp,0x2c; push ebx; push ebp) become a jump to a stub that reports the
 * hit for the score (4th stack argument: the owner handle, 0x51cda0 gives its character), scales
 * EDX and runs them. 1.25x rounds half up (1 stays 1, 2 becomes 3, 4 becomes 5).
 *
 * Overhealing: the character's hypo tick (0x51bad0, ESI the character) counts a cooldown at
 * +0x1b0 down and, when it's 0 and health (+0x38) is over the maximum (+0x3c), takes 1 hit
 * point and sets the cooldown to 10 (0x51bb98..0x51bbae): 1 point every 11 ticks. We replace
 * 0x51bb98..0x51bba5 with a call to a stub that does the same with cooldowns of 8, 8, 8, 8, 7
 * (by health mod 5): 5 points every 44 ticks, 1.25x stock's 4. The cooldown write at 0x51bba6
 * stays, for the hypo heal finishing (jmp at 0x51bb33).
 *
 * Free for all: every player is a copy of Konoko's placement, so all are on her team (character
 * +0x12, u16; chr_changeteam 0x4b8b10 sets it), and Oni never lets a character hurt one it's
 * friendly with. The relationship of two teams comes from 0x48b2c0 (CX, DX the teams; EAX 0
 * friendly, 1 neutral, 2 hostile; same team always friendly unless 0x5eb848, which would make
 * the AIs fight each other too). Four callers decide hits and targets:
 *   0x4e7779  melee hit (0x4e7740: ESI attacker, EBP target), friendly: no hit
 *   0x4639fd  particle/bullet collision (owner character [0x5e962c], ESI the character hit),
 *             friendly: passes through, for particle classes with flag 0x100000
 *   0x4ea314  the character search behind melee targeting and throws (EBX self, ESI other)
 *   0x4e9afa  its caller's hostile check on the character found (EBX self, EAX other)
 * On a multiplayer map each call goes through a stub that answers hostile (2) when both
 * characters are players (charType +0x1e8 == 0) and asks 0x48b2c0 otherwise, so the AIs keep
 * their teams.
 *
 * Taunts: the taunt check (0x4f80d0, ESI the character) lets the player (gs+0xac) taunt only
 * until gs+0x110, a time 0x4fdba0 moves later (after a kill); other characters always can. On a
 * multiplayer map the read of gs+0x110 (mov ecx,[eax+0x110] at 0x4f8249, then cmp ecx,edx with
 * the game tick in EDX) gives the last tick there is, so taunting is unlimited.
 *
 * Taunt lines: an animation's start (0x4e97a0) says its vocalization line with 0x48ba60 (ECX
 * the character, EDX the line type, 0 taunt; one stack argument, ret 4; call at 0x4e97fb),
 * which rolls the class's chance and asks the speech system (0x516940) with the type's priority
 * (0x52a584[type], 4 for a taunt). A line of no higher priority than the one being said
 * (character +0x15fc) waits in the one-line queue (+0x1614), so taunting during a taunt queued
 * the next line. On a multiplayer map a player's taunt line takes over: the playing line's
 * priority reads 0 during the call, so 0x516940 stops it, empties the queue and starts the new
 * one. If the chance roll says nothing, the priority is put back and the old line goes on.
 *
 * Crouched aim: a held weapon points where the aiming screen allows. Exact aiming (0x4e5a30)
 * clips the aim with 0x4292f0 (ECX the screen at active +0x1ee4, EDX &direction, one stack
 * argument &elevation = aimingUD + recoil + auto-aim, ret 4; one call, at 0x4e5dee); elevation
 * below 0 is down. The clip's floor is -(+0x10 step * +0x18 frames), its ceiling +0x14 * +0x1a.
 * The body's pose (0x429460) negates the elevation first, so aiming down bends the body over
 * +0x14 * +0x1a: the other range. Three crouch rifle screens are uneven, 30 degrees one way
 * (+0x10) and 60 the other (+0x14): TRASCOMRIFaim_screen_crouch (comguy's screens: Muro,
 * Comguy, TCTF troopers, Cop, Mad Bomber, Ninjabot), TRASSTRRIFaim_screen_crouch (striker's
 * and tanker's: Barabas, Striker, Elite, TCTF SWAT, Tanker) and TRASNINRIFaim_screen_crouch
 * (Ninja). The body bent 60 degrees down and the gun stopped at 30. Konoko's is 60 both ways.
 * On a multiplayer map the floor is the pose's reach when that's lower: 60 degrees there, the
 * prone pistol screen 30 instead of 0, every even screen as it was. The ceiling stays.
 *
 * Ninjabot: its class (ONCCninjabot) is left-handed (+0xc8c, WeaponHand) like the ninjas, but
 * it plays comguy's animations, which have no left-handed weapon ones. A held weapon makes the
 * animation variant lefty rifle 0x4000 or lefty pistol 0x1000 (0x4d7f90 and others), nothing
 * matches, so it moves as unarmed and no aiming screen applies: it never aims. On a multiplayer
 * map the class is right-handed (0x2000/0x0800, comguy's), set when the level's characters are
 * made (mp_rules_level, from the player spawn walk before the game spawns them) and put back
 * on other maps: stock levels 1 and 10 place ninjabots. */
#include "arklite.h"

void *memset(void *, int, unsigned);
void *memcpy(void *, const void *, unsigned);

#define ONrCharacter_TakeDamage     0x4e65f0
#define ONcHypoTick_Drain           0x51bb98
#define ONcHypoTick_Done            0x51bbaf

#define CHR_HEALTH(c)       (*(u32 *)((u8 *)(c) + 0x38))
#define CHR_MAX_HEALTH(c)   (*(u32 *)((u8 *)(c) + 0x3c))
#define CHR_HYPO_COOLDOWN(c) (*(u16 *)((u8 *)(c) + 0x1b0))

static u32 ONI_FC damage_scaled(u32 damage)
{
	if (!multiplayer_map()) return damage;
	if (damage > 0xccccccccu) return 0xffffffffu;
	return damage + (damage + 2) / 4;
}

static void ONI_FC overheal_drain(u8 *c)
{
	if (CHR_HEALTH(c) <= CHR_MAX_HEALTH(c)) return;
	CHR_HEALTH(c)--;
	CHR_HYPO_COOLDOWN(c) = !multiplayer_map() ? 10 : CHR_HEALTH(c) % 5 ? 8 : 7;
}

#define TEAM_RELATION   0x48b2c0
#define CHR_TYPE(c)     (*(u32 *)((u8 *)(c) + 0x1e8))   /* 0 player, 2 AI */

/* Two different players on a multiplayer map: hostile, whatever their teams */
static u32 ONI_FC players_hostile(u8 *a, u8 *b)
{
	return a && b && a != b && CHR_TYPE(a) == 0 && CHR_TYPE(b) == 0 && multiplayer_map();
}

/* A call to 0x48b2c0 where the characters are in registers A and B (or memory) */
#define RELATION_STUB(name, A, B) \
	__attribute__((naked)) static void name(void) \
	{ \
		__asm__ volatile( \
			"pushl %%ecx\n\t" \
			"pushl %%edx\n\t" \
			"movl " A ", %%ecx\n\t" \
			"movl " B ", %%edx\n\t" \
			"call %c0\n\t" \
			"popl %%edx\n\t" \
			"popl %%ecx\n\t" \
			"testl %%eax, %%eax\n\t" \
			"jz 1f\n\t" \
			"movl $2, %%eax\n\t" \
			"ret\n" \
			"1:\n\t" \
			"pushl $0x48b2c0\n\t" \
			"ret\n" :: "i"(players_hostile)); \
	}

RELATION_STUB(relation_melee,    "%%esi", "%%ebp")
RELATION_STUB(relation_particle, "0x5e962c", "%%esi")
RELATION_STUB(relation_search,   "%%ebx", "%%esi")
RELATION_STUB(relation_found,    "%%ebx", "%%eax")

static void free_for_all_install(void)
{
	static const struct { u32 site; void (*stub)(void); } sites[] = {
		{ 0x4e7779, relation_melee }, { 0x4639fd, relation_particle },
		{ 0x4ea314, relation_search }, { 0x4e9afa, relation_found },
	};
	const u32 n = sizeof sites / sizeof sites[0];

	for (u32 i = 0; i < n; i++) {
		const u8 *q = (const u8 *)sites[i].site;
		if (q[0] != 0xe8 || sites[i].site + 5 + *(u32 *)(q + 1) != TEAM_RELATION) {
			log_msg("mp rules: team check 0x%x changed, players can't hurt each other", sites[i].site);
			return;
		}
	}
	for (u32 i = 0; i < n; i++) patch_call(sites[i].site, TEAM_RELATION, (u32)sites[i].stub);
	log_msg("mp rules: players can hurt each other on multiplayer maps (free for all)");
}

#define ONrOwner_Character  ((u8 *(ONI_FC *)(u32))0x51cda0)   /* owner handle -> character, or 0 */

/* ---- Owners of ownerless particles, for the score -------------------------------------------- *
 * A particle class keeps its owner (who fired it) only with HasDamageOwner (definition +4 flag
 * 0x100000; the owner dword at particle +0x44 + class +0x6c). The Screaming Cannon's shot
 * (w9_scr_p01, owned) emits w9_scr_p02a (1 damage, trail) and its w9_scr_p02 hits a wall into
 * the effect w9_scr_x02, which emits w9_scr_f01 (5-7 damage + stun); p02a, x02 and f01 have no
 * owner, so their hits came as Environmental (04000000, WPrHitChar/WPrBlast for owner 0) and a
 * cannon kill went to no one ("died on their own"). The class layout is fixed at load, so the
 * owner can't be added; instead each particle that loses an owned parent's owner gets it here,
 * keyed by its reference (+0x2c, index and salt), and a hit that comes as Environmental from such
 * a particle's DamageChar or DamageBlast counts for that owner. Only the score sees it: the hit
 * itself (self-immunity, AI reactions) stays as stock. Hooks (on multiplayer maps only):
 *   0x45e37c  the emitter, after the stock owner copy (ESI parent, EDI its class, EBP the new
 *             particle, [EBX+0x40] its class); 8 bytes: mov ecx,[edi+0x48]; mov edx,0x8000
 *   0x4564b2  P3rCreateEffect's owner store (ESI the effect data, +4 its owner; EBX the class,
 *             EBP the particle); 10 bytes: mov ecx,[ebx+0x48]; test [ecx+4],0x100000
 *   0x465e93  DamageChar's owner (EAX, 0 when none; ESI the class, EBP the particle); 8 bytes:
 *             mov ecx,eax; and ecx,0xff000000
 *   0x46600b  DamageBlast's (EBP the class, EDI the particle); 10 bytes: mov eax,[ebp+0x48];
 *             test [eax+4],0x100000
 * Environmental comes only from those two actions, so the owner each one leaves in g_lent is
 * the right one for every TakeDamage it makes (a blast makes one per character). The table is
 * tick state, so rollback takes it back with the particles. */

#define PCLASS_HAS_OWNER(cl)  (*(u32 *)(*(u8 **)((cl) + 0x48) + 4) & 0x100000)
#define PARTICLE_OWNER(cl, p) (*(u32 *)((p) + 0x44 + *(u16 *)((cl) + 0x6c)))
#define PARTICLE_REF(p)       (*(u32 *)((p) + 0x2c))
#define OWNERS                2048   /* direct-mapped: a newer particle on a slot takes it */

static struct { u32 ref, owner; } g_owners[OWNERS] TICK_STATE;
static u32 g_lent TICK_STATE;   /* the owner the running damage action lends its hits, or 0 */

static u32 owner_slot(u32 ref) { return (ref * 0x9e3779b1u) >> 21; }

static u32 owner_find(u32 ref)
{
	u32 s = owner_slot(ref);
	return ref && g_owners[s].ref == ref ? g_owners[s].owner : 0;
}

static void owner_keep(u32 ref, u32 owner)
{
	u32 s = owner_slot(ref), type = owner >> 24;
	if (!ref) return;
	if (type == 1 || type == 2) {   /* a character's weapon or melee */
		g_owners[s].ref = ref;
		g_owners[s].owner = owner;
	} else if (g_owners[s].ref == ref) {   /* a new particle on an old one's reference */
		g_owners[s].ref = g_owners[s].owner = 0;
	}
}

/* Who fired the particle: its own owner, else the one it was given here */
static u32 particle_owner(u8 *cl, u8 *p)
{
	u32 o = PCLASS_HAS_OWNER(cl) ? PARTICLE_OWNER(cl, p) : 0;
	return o ? o : owner_find(PARTICLE_REF(p));
}

static void owner_emitted(u8 *parent, u8 *pclass, u8 *child, u8 *cclass)
{
	if (!multiplayer_map()) return;
	if (PCLASS_HAS_OWNER(cclass) && PARTICLE_OWNER(cclass, child)) owner_keep(PARTICLE_REF(child), 0);
	else owner_keep(PARTICLE_REF(child), particle_owner(pclass, parent));
}

static void owner_effect(u32 owner, u8 *cl, u8 *p)
{
	if (!multiplayer_map()) return;
	owner_keep(PARTICLE_REF(p), PCLASS_HAS_OWNER(cl) ? 0 : owner);
}

static void owner_damage(u8 *cl, u8 *p)
{
	g_lent = multiplayer_map() && !(PCLASS_HAS_OWNER(cl) && PARTICLE_OWNER(cl, p)) ? owner_find(PARTICLE_REF(p)) : 0;
}

static void mp_rules_owners_reset(void)
{
	memset(g_owners, 0, sizeof g_owners);
	g_lent = 0;
}

__attribute__((naked)) static void emitter_stub(void)
{
	__asm__ volatile(
		"pushal\n\t"
		"pushl 0x40(%%ebx)\n\t"   /* the new particle's class */
		"pushl %%ebp\n\t"
		"pushl %%edi\n\t"
		"pushl %%esi\n\t"
		"call %c0\n\t"
		"addl $16, %%esp\n\t"
		"popal\n\t"
		"movl 0x48(%%edi), %%ecx\n\t"   /* the 8 bytes we replaced */
		"movl $0x8000, %%edx\n\t"
		"ret\n" :: "i"(owner_emitted));
}

__attribute__((naked)) static void effect_stub(void)
{
	__asm__ volatile(
		"pushal\n\t"
		"pushl %%ebp\n\t"
		"pushl %%ebx\n\t"
		"pushl 4(%%esi)\n\t"
		"call %c0\n\t"
		"addl $12, %%esp\n\t"
		"popal\n\t"
		"movl 0x48(%%ebx), %%ecx\n\t"   /* the 10 bytes we replaced; the flags go back with ret */
		"testl $0x100000, 4(%%ecx)\n\t"
		"ret\n" :: "i"(owner_effect));
}

__attribute__((naked)) static void damage_char_stub(void)
{
	__asm__ volatile(
		"pushal\n\t"
		"pushl %%ebp\n\t"
		"pushl %%esi\n\t"
		"call %c0\n\t"
		"addl $8, %%esp\n\t"
		"popal\n\t"
		"movl %%eax, %%ecx\n\t"   /* the 8 bytes we replaced */
		"andl $0xff000000, %%ecx\n\t"
		"ret\n" :: "i"(owner_damage));
}

__attribute__((naked)) static void damage_blast_stub(void)
{
	__asm__ volatile(
		"pushal\n\t"
		"pushl %%edi\n\t"
		"pushl %%ebp\n\t"
		"call %c0\n\t"
		"addl $8, %%esp\n\t"
		"popal\n\t"
		"movl 0x48(%%ebp), %%eax\n\t"   /* the 10 bytes we replaced; the flags go back with ret */
		"testl $0x100000, 4(%%eax)\n\t"
		"ret\n" :: "i"(owner_damage));
}

static void owners_install(void)
{
	static const struct { u32 at; u8 n; u8 stock[10]; void (*stub)(void); } hooks[] = {
		{ 0x45e37c, 8, { 0x8b, 0x4f, 0x48, 0xba, 0x00, 0x80, 0x00, 0x00 }, emitter_stub },
		{ 0x4564b2, 10, { 0x8b, 0x4b, 0x48, 0xf7, 0x41, 0x04, 0x00, 0x00, 0x10, 0x00 }, effect_stub },
		{ 0x465e93, 8, { 0x8b, 0xc8, 0x81, 0xe1, 0x00, 0x00, 0x00, 0xff }, damage_char_stub },
		{ 0x46600b, 10, { 0x8b, 0x45, 0x48, 0xf7, 0x40, 0x04, 0x00, 0x00, 0x10, 0x00 }, damage_blast_stub },
	};
	u32 n = sizeof hooks / sizeof hooks[0];

	for (u32 i = 0; i < n; i++)
		if (!code_matches(hooks[i].at, hooks[i].stock, hooks[i].n)) {
			log_msg("mp rules: 0x%x changed, ownerless particles' hits (Screaming Cannon) count for no one", hooks[i].at);
			return;
		}
	for (u32 i = 0; i < n; i++) {
		u8 b[10];
		u32 rel = (u32)hooks[i].stub - (hooks[i].at + 5);
		memset(b, 0x90, sizeof b);
		b[0] = 0xe8;
		memcpy(b + 1, &rel, 4);
		patch_write(hooks[i].at, b, hooks[i].n);
	}
	log_msg("mp rules: hits from ownerless particles count for who fired them (Screaming Cannon)");
}

/* Who hit whom, for the score (players.c) */
static void ONI_FC note_hit(u8 *victim, u32 owner)
{
	static u32 logged;
	if (owner >> 24 == 4 && g_lent) {   /* an ownerless particle someone fired */
		owner = g_lent;
		if (logged < 20 && ++logged) log_msg("mp rules: an ownerless particle's hit counts for owner %08x", owner);
	}
	if (owner) players_note_hit(victim, ONrOwner_Character(owner), owner);
}

__attribute__((naked)) static void take_damage_entry(void)
{
	__asm__ volatile(
		"pushl %%ecx\n\t"
		"pushl %%edx\n\t"
		"movl 0x18(%%esp), %%edx\n\t"   /* 4th stack argument: the hit's owner */
		"call %c1\n\t"
		"popl %%edx\n\t"
		"popl %%ecx\n\t"
		"pushl %%ecx\n\t"
		"movl %%edx, %%ecx\n\t"
		"call %c0\n\t"
		"movl %%eax, %%edx\n\t"
		"popl %%ecx\n\t"
		"subl $0x2c, %%esp\n\t"   /* the 5 bytes we replaced */
		"pushl %%ebx\n\t"
		"pushl %%ebp\n\t"
		"pushl $0x4e65f5\n\t"
		"ret\n" :: "i"(damage_scaled), "i"(note_hit));
}

__attribute__((naked)) static void drain_stub(void)
{
	__asm__ volatile(
		"pushl %%ecx\n\t"
		"pushl %%edx\n\t"
		"movl %%esi, %%ecx\n\t"
		"call %c0\n\t"
		"popl %%edx\n\t"
		"popl %%ecx\n\t"
		"ret\n" :: "i"(overheal_drain));
}

#define ONcTaunt_UntilRead  0x4f8249

/* For mov ecx,[eax+0x110] (EAX the game state): every register but ECX kept */
__attribute__((naked)) static void taunt_until(void)
{
	__asm__ volatile(
		"pushl %%eax\n\t"
		"pushl %%edx\n\t"
		"call %c0\n\t"
		"testl %%eax, %%eax\n\t"
		"popl %%edx\n\t"
		"popl %%eax\n\t"
		"jz 1f\n\t"
		"movl $0xffffffff, %%ecx\n\t"
		"ret\n"
		"1:\n\t"
		"movl 0x110(%%eax), %%ecx\n\t"
		"ret\n" :: "i"(multiplayer_map));
}

static void taunt_install(void)
{
	static const u8 read[6] = { 0x8b, 0x88, 0x10, 0x01, 0x00, 0x00 };
	u8 b[6] = { 0xe8, 0, 0, 0, 0, 0x90 };
	u32 rel = (u32)taunt_until - (ONcTaunt_UntilRead + 5);

	if (!code_matches(ONcTaunt_UntilRead, read, sizeof read)) {
		log_msg("mp rules: 0x%x changed, taunts stay limited", ONcTaunt_UntilRead);
		return;
	}
	memcpy(b + 1, &rel, 4);
	patch_write(ONcTaunt_UntilRead, b, sizeof b);
	log_msg("mp rules: unlimited taunts on multiplayer maps");
}

#define ONrCharacter_SayLine    ((u8 (ONI_FC *)(u8 *, u32, u32))0x48ba60)
#define ONcAnimLine_SayCall     0x4e97fb
#define ONgLinePriority         ((const u32 *)0x52a584)   /* by line type */
#define LINE_TAUNT              0
#define CHR_SPEAKING(c)         (*(u8 *)((u8 *)(c) + 0x15f0))
#define CHR_LINE_PRIORITY(c)    (*(u32 *)((u8 *)(c) + 0x15fc))

static u8 ONI_FC say_anim_line(u8 *c, u32 type, u32 forced)
{
	u32 was;
	u8 r;

	if (type != LINE_TAUNT || CHR_TYPE(c) != 0 || !CHR_SPEAKING(c) ||
	    CHR_LINE_PRIORITY(c) > ONgLinePriority[type] || !multiplayer_map())
		return ONrCharacter_SayLine(c, type, forced);
	was = CHR_LINE_PRIORITY(c);
	CHR_LINE_PRIORITY(c) = 0;
	r = ONrCharacter_SayLine(c, type, forced);
	if (CHR_LINE_PRIORITY(c) == 0) CHR_LINE_PRIORITY(c) = was;   /* nothing said */
	return r;
}

static void taunt_line_install(void)
{
	const u8 *p = (const u8 *)ONcAnimLine_SayCall;
	if (p[0] != 0xe8 || ONcAnimLine_SayCall + 5 + *(u32 *)(p + 1) != (u32)ONrCharacter_SayLine) {
		log_msg("mp rules: 0x%x changed, taunt lines still wait for the last one", ONcAnimLine_SayCall);
		return;
	}
	patch_call(ONcAnimLine_SayCall, (u32)ONrCharacter_SayLine, (u32)say_anim_line);
	log_msg("mp rules: a player's taunt line cuts off the one before on multiplayer maps");
}

#define TRrAimingScreen_Clip    ((void (ONI_FC *)(const u8 *, float *, float *))0x4292f0)
#define ONcExactAim_ClipCall    0x4e5dee
#define TRAS_DOWN_STEP(s)       (*(const float *)((s) + 0x10))   /* the clip's floor */
#define TRAS_UP_STEP(s)         (*(const float *)((s) + 0x14))   /* the pose's down reach */
#define TRAS_DOWN_FRAMES(s)     (*(const short *)((s) + 0x18))
#define TRAS_UP_FRAMES(s)       (*(const short *)((s) + 0x1a))

static void ONI_FC aim_clip(const u8 *screen, float *direction, float *elevation)
{
	float e = *elevation, floor, reach;

	TRrAimingScreen_Clip(screen, direction, elevation);
	if (!screen || !multiplayer_map()) return;
	floor = TRAS_DOWN_STEP(screen) * (float)TRAS_DOWN_FRAMES(screen);
	reach = TRAS_UP_STEP(screen) * (float)TRAS_UP_FRAMES(screen);
	if (reach > floor && e < -floor) *elevation = e < -reach ? -reach : e;
}

static void aim_clip_install(void)
{
	if (patch_call(ONcExactAim_ClipCall, (u32)TRrAimingScreen_Clip, (u32)aim_clip))
		log_msg("mp rules: crouched weapons aim as low as the body bends on multiplayer maps");
	else
		log_msg("mp rules: 0x%x changed, crouched aim stays limited", ONcExactAim_ClipCall);
}

#define TMrInstance_GetDataPtr  ((u16 (ONI_FC *)(u32, const char *, void *))0x4232e0)
#define TAG_ONCC                0x4f4e4343
#define ONCC_LEFT_HANDED(c)     (*(u8 *)((c) + 0xc8c))
/* The Daodan mutator: the class flags behind Konoko's and Muro's overhealing powers, set on every
 * class. superHypo (+0xc8d, read at 0x4dd882, 0x4df890, 0x4e7bec): health over the maximum turns
 * into the super amount (OniFoxed Oni_Character.c ~4504: the glow, and melee hits stronger with
 * the overhealth, ~9738 overhypo damage). superShield (+0xc8e, read at 0x4e6225, 0x4e64d0
 * HasSuperShield, 0x4e7846, 0x4ef71a): over 0.5 of super amount the character has the Daodan
 * shield (its look and damage absorption). Overhealing itself (hypos past the maximum, up to the
 * boosted health) is already everyone's. Set at a level's load, before any character is made and
 * outside the ticks, from the host's mutators (the same on every PC: LOBBY brings them before any
 * session loads), and put back at the level's unload while its own classes are still loaded. */
#define ONCC_SUPER_HYPO(c)      (*(u8 *)((c) + 0xc8d))
#define ONCC_SUPER_SHIELD(c)    (*(u8 *)((c) + 0xc8e))
/* Its look: particles, not the force field. A class's particle list (ONCP, ONCC +0x430: +0x10
 * classes found, +0x14 count, +0x18 entries of 0x58 bytes: +0 name, +0x10 particle class name,
 * +0x50 body part, +0x54 the class, resolved) gives the super particles, its entries named
 * "super..." (0x4df994: SuperParticle_Create, while the super amount is over 0; the body part must
 * be under 19). Only Konoko's classes and Muro's have them: every other class gets a copy of its
 * list with Konoko's three added (superglow_e01 on the pelvis, super_e01 on each fist) for women,
 * Muro's (murosuperglow_e01, murosuper_e01) for men (char_menu.c), its own list pointer put back at
 * the unload. The Daodan shield's own look, the DAODAN_SHIELD texture over the body (the draw at
 * 0x4e1f72: active +0x2801 set -> mov edx,0x550e88 at 0x4e1f7c, the texture's name), goes: that
 * name is read from a buffer of Arklite's, which holds a name no texture has while the mutator is
 * on, and the draw skips a texture it can't find (0x4e1fac). A force field picked up and the boss
 * shield keep theirs (the other branch). */
#define ONCC_PARTICLES(c)       (*(u8 **)((c) + 0x430))
#define ONCP_FOUND(a)           (*(u8 *)((a) + 0x10))
#define ONCP_COUNT(a)           (*(u32 *)((a) + 0x14))
#define ONCP_ENTRY(a, i)        ((a) + 0x18 + (i) * 0x58)
#define ONCP_ENTRY_SIZE         0x58
#define ONCP_CLASS(e)           (*(u8 **)((e) + 0x54))
#define P3rGetParticleClass     ((u8 *(ONI_FC *)(const char *))0x45a030)
#define ONcDraw_DaodanShieldName 0x4e1f7c   /* mov edx,0x550e88 ("DAODAN_SHIELD") */
#define DAODAN_MAX              1024
#define SUPER_MAX               4           /* super particles copied from Konoko's or Muro's list */
static struct { u8 *oncc, *particles; u8 hypo, shield; } g_daodan[DAODAN_MAX];
static u32 g_ndaodan;
static u8 *g_lists;                         /* the copies, one block a level */
static char g_shield_name[16] = "DAODAN_SHIELD";
__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) int    WINAPI VirtualFree(void *, u32, u32);

void mp_rules_level_end(void)
{
	lockstep_decor_level_log();
	mutators_level_end();
	for (u32 i = 0; i < g_ndaodan; i++) {
		ONCC_SUPER_HYPO(g_daodan[i].oncc) = g_daodan[i].hypo;
		ONCC_SUPER_SHIELD(g_daodan[i].oncc) = g_daodan[i].shield;
		ONCC_PARTICLES(g_daodan[i].oncc) = g_daodan[i].particles;
	}
	if (g_ndaodan) log_msg("mp rules: Daodan mutator off: %d classes as they were", g_ndaodan);
	g_ndaodan = 0;
	str_cpy(g_shield_name, sizeof g_shield_name, "DAODAN_SHIELD");
	if (g_lists) VirtualFree(g_lists, 0, 0x8000);
	g_lists = 0;
}

static int is_super(const u8 *entry) { return str_ieq_n((const char *)entry, "super", 5); }

/* The class's super particle entries (Konoko's or Muro's), resolved; their count */
static u32 super_entries(const char *cls, u8 (*out)[ONCP_ENTRY_SIZE])
{
	u8 *oncc = 0, *list;
	u32 n = 0;
	if (TMrInstance_GetDataPtr(TAG_ONCC, cls, &oncc) || !oncc || !(list = ONCC_PARTICLES(oncc))) return 0;
	for (u32 i = 0; i < ONCP_COUNT(list) && n < SUPER_MAX; i++)
		if (is_super(ONCP_ENTRY(list, i))) {
			memcpy(out[n], ONCP_ENTRY(list, i), ONCP_ENTRY_SIZE);
			if (!ONCP_CLASS(out[n])) ONCP_CLASS(out[n]) = P3rGetParticleClass((const char *)out[n] + 0x10);
			if (ONCP_CLASS(out[n])) n++;
		}
	return n;
}

static void daodan_level(void)
{
	static u8 sets[2][SUPER_MAX][ONCP_ENTRY_SIZE];
	u32 n, nset[2], size = 0, used = 0, women = 0, men = 0;

	mp_rules_level_end();   /* (a level end that never came) */
	if (!multiplayer_map() || !net_mutator_daodan()) return;
	nset[0] = super_entries("muro_generic", sets[0]);     /* men */
	nset[1] = super_entries("konoko_generic", sets[1]);   /* women */
	n = TMrInstance_GetTagCount(TAG_ONCC);
	for (u32 k = 0; k < n; k++) {   /* room for every class's copy */
		u8 *p = 0, *l;
		if (TMrInstance_GetDataPtr_ByNumber(TAG_ONCC, k, (void **)&p) || !p) continue;
		l = ONCC_PARTICLES(p);
		size += 0x18 + ((l ? ONCP_COUNT(l) : 0) + SUPER_MAX) * ONCP_ENTRY_SIZE;
	}
	g_lists = size ? VirtualAlloc(0, size, 0x3000, 4) : 0;
	for (u32 k = 0; k < n && g_ndaodan < DAODAN_MAX; k++) {
		u8 *p = 0, *l, *copy;
		int has_super = 0, woman;
		if (TMrInstance_GetDataPtr_ByNumber(TAG_ONCC, k, (void **)&p) || !p) continue;
		g_daodan[g_ndaodan].oncc = p;
		g_daodan[g_ndaodan].hypo = ONCC_SUPER_HYPO(p);
		g_daodan[g_ndaodan].shield = ONCC_SUPER_SHIELD(p);
		g_daodan[g_ndaodan].particles = l = ONCC_PARTICLES(p);
		g_ndaodan++;
		ONCC_SUPER_HYPO(p) = 1;
		ONCC_SUPER_SHIELD(p) = 1;
		for (u32 i = 0; l && i < ONCP_COUNT(l) && !has_super; i++) has_super = is_super(ONCP_ENTRY(l, i));
		woman = char_menu_class_female(TMrInstance_GetInstanceName(p));
		if (has_super || !g_lists || !nset[woman]) continue;
		/* its own list (each class resolved, as Oni would) and the set */
		copy = g_lists + used;
		memset(copy, 0, 0x18);
		if (l) memcpy(copy, l, 0x18);
		ONCP_COUNT(copy) = 0;
		for (u32 i = 0; l && i < ONCP_COUNT(l); i++) {
			u8 *e = ONCP_ENTRY(copy, ONCP_COUNT(copy));
			memcpy(e, ONCP_ENTRY(l, i), ONCP_ENTRY_SIZE);
			if (!ONCP_CLASS(e)) ONCP_CLASS(e) = P3rGetParticleClass((const char *)e + 0x10);
			ONCP_COUNT(copy)++;
		}
		for (u32 i = 0; i < nset[woman]; i++) memcpy(ONCP_ENTRY(copy, ONCP_COUNT(copy)++), sets[woman][i], ONCP_ENTRY_SIZE);
		ONCP_FOUND(copy) = 1;
		used += 0x18 + ONCP_COUNT(copy) * ONCP_ENTRY_SIZE;
		ONCC_PARTICLES(p) = copy;
		if (woman) women++; else men++;
	}
	str_cpy(g_shield_name, sizeof g_shield_name, "arklite_none");   /* no texture: particles only */
	log_msg("mp rules: Daodan mutator: %d classes overheal into Konoko's power and Muro's shield; particles added to %d "
	        "(Konoko's to %d women, Muro's to %d men; %d and %d super particles found), no shield texture",
	        g_ndaodan, women + men, women, men, nset[1], nset[0]);
}

static void daodan_install(void)
{
	static const u8 stock[5] = { 0xba, 0x88, 0x0e, 0x55, 0x00 };   /* mov edx,0x550e88 */
	u8 code[5] = { 0xba };
	u32 name = (u32)g_shield_name;
	if (!code_matches(ONcDraw_DaodanShieldName, stock, sizeof stock)) {
		log_msg("mp rules: 0x%x changed, the Daodan mutator keeps the shield texture", ONcDraw_DaodanShieldName);
		return;
	}
	memcpy(code + 1, &name, 4);
	patch_write(ONcDraw_DaodanShieldName, code, sizeof code);
}

void mp_rules_level(void)
{
	static u8 *oncc;
	static u8 stock;
	u8 *p = 0;

	mp_rules_owners_reset();   /* a new level's particles start their references over */
	daodan_level();
	mutators_level();
	if (TMrInstance_GetDataPtr(TAG_ONCC, "ninjabot", &p) || !p) return;
	if (p != oncc) {   /* first time, or another level0 */
		oncc = p;
		stock = ONCC_LEFT_HANDED(p);
	}
	if (stock && ONCC_LEFT_HANDED(p) != (multiplayer_map() ? 0 : stock))
		log_msg("mp rules: Ninjabot %s-handed", multiplayer_map() ? "right" : "left (stock)");
	ONCC_LEFT_HANDED(p) = multiplayer_map() ? 0 : stock;
}

void mp_rules_install(void)
{
	static const u8 entry[5] = { 0x83, 0xec, 0x2c, 0x53, 0x55 };
	/* mov eax,[esi+0x38]; mov ecx,[esi+0x3c]; cmp eax,ecx; jbe 0x51bbaf; dec eax; mov [esi+0x38],eax */
	static const u8 drain[14] = { 0x8b, 0x46, 0x38, 0x8b, 0x4e, 0x3c, 0x3b, 0xc1, 0x76, 0x0d,
	                              0x48, 0x89, 0x46, 0x38 };
	u8 b[14];
	u32 rel;

	if (code_matches(ONrCharacter_TakeDamage, entry, sizeof entry)) {
		rel = (u32)take_damage_entry - (ONrCharacter_TakeDamage + 5);
		b[0] = 0xe9;
		memcpy(b + 1, &rel, 4);
		patch_write(ONrCharacter_TakeDamage, b, 5);
		log_msg("mp rules: damage x1.25 on multiplayer maps");
	} else {
		log_msg("mp rules: 0x%x changed, damage not scaled", ONrCharacter_TakeDamage);
	}

	free_for_all_install();
	taunt_install();
	taunt_line_install();
	aim_clip_install();
	owners_install();

	if (code_matches(ONcHypoTick_Drain, drain, sizeof drain)) {
		memset(b, 0x90, sizeof b);
		rel = (u32)drain_stub - (ONcHypoTick_Drain + 5);
		b[0] = 0xe8;
		memcpy(b + 1, &rel, 4);
		b[5] = 0xeb;                                      /* jmp short 0x51bbaf */
		b[6] = ONcHypoTick_Done - (ONcHypoTick_Drain + 7);
		patch_write(ONcHypoTick_Drain, b, sizeof b);
		log_msg("mp rules: overhealing drains 1.25x faster on multiplayer maps");
	} else {
		log_msg("mp rules: 0x%x changed, overhealing drain not changed", ONcHypoTick_Drain);
	}
	daodan_install();
}
