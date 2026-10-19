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
 * their teams. */
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

/* Who hit whom, for the score (players.c) */
static void ONI_FC note_hit(u8 *victim, u32 owner)
{
	if (owner) players_note_hit(victim, ONrOwner_Character(owner));
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
}
