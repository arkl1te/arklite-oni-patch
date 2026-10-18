/* 255 characters, all of them active at once.
 *
 * Oni keeps 128 character slots, but only 64 "active" ones (full animation, physics and AI).
 * When the active table is full, activation (0x4db830) fails and the character stays inactive,
 * but AI2rUpdateAI (0x489d40) still runs its state machine: an AI that goes into combat
 * (state 5, 0x48f3e0) reads its active data through a null pointer and the game crashes
 * (0x48f761). The 32-entry shadow pool has the same problem: its allocator (0x4fdc50) returns
 * 0xffff when full and the caller (0x4e2373) writes far out of bounds.
 *
 * All of these live in the game state, one block allocated at startup (size at 0x4fc41d):
 *   +0x1260    characters[128], 0x16a0 each
 *   +0xb6260   tail: character count, active[64] (0x2838 each), active count, shadows[32]
 *              (0x804 each), three character pointer lists[128] with their counts, other fields
 * We allocate a bigger block, let the character table grow in place to 255, and move every
 * tail field to its spot in a new tail after it, with 255 active slots, 255 shadows and
 * 255-entry lists. tools/gen_char_limit.py finds the 344 instructions that address the tail
 * (and the 2 in Daodan) and writes char_limit_sites.h.
 *
 * 255, not 256: a handle is 0x8000 | salt << 8 | index with a 7-bit salt, so index 0xff with
 * salt 0x7f would be 0xffff, the "no character" handle. With 255 characters the active table
 * and the shadow pool can never be full while a character still needs a slot.
 *
 * Every site is checked before anything is written. If one doesn't match (another mod got
 * there first), nothing is patched and the stock limits stay. */
#include "arklite.h"
#include "char_limit_sites.h"

#define STR_(x) #x
#define STR(x)  STR_(x)

/* ---- Fixed-size sites ------------------------------------------------------------------- */

/* mov ecx, imm32: sizes and rep stos counts */
#define ONcGameState_AllocSize     0x4fc41d   /* game state allocation, bytes */
#define ONcGameState_Clear         0x4f5386   /* whole game state clear, dwords */
#define ONcLevelReset_CharClear    0x4d883d   /* character table clear, dwords */
#define ONcLevelReset_ActiveClear  0x4d886b   /* active table clear, dwords */
/* cmp dx, 0x80 (imm16) in ONrGameState_NewCharacter's free-slot search */
#define ONcNewCharacter_Bound1     0x4dac95
#define ONcNewCharacter_Bound2     0x4dacb1
/* cmp dx, 0x40 / cmp eax, 0x20 + jcc in the active and shadow free-slot searches: the
 * immediates are sign-extended bytes, so these become jumps to the stubs below. */
#define ONcActivate_Next           0x4db86b   /* cmp dx,0x40; jb 0x4db84e */
#define ONcActivate_Found          0x4db87e   /* cmp dx,0x40; jae 0x4db871 */
#define ONcShadow_Next             0x4fdc6c   /* cmp eax,0x20; jb 0x4fdc5f */
#define ONcShadow_Found            0x4fdc77   /* cmp eax,0x20; jb 0x4fdc82 */

__attribute__((naked)) static void activate_next(void)
{
	__asm__ volatile(
		"cmpw $" STR(CL_LIMIT) ", %dx\n\t"
		"jb 1f\n\t"
		"pushl $0x4db871\n\t"   /* table full: return 1 */
		"ret\n"
		"1:\n\t"
		"pushl $0x4db84e\n\t"   /* next slot */
		"ret\n");
}

__attribute__((naked)) static void activate_found(void)
{
	__asm__ volatile(
		"cmpw $" STR(CL_LIMIT) ", %dx\n\t"
		"jb 1f\n\t"
		"pushl $0x4db871\n\t"
		"ret\n"
		"1:\n\t"
		"pushl $0x4db884\n\t"   /* use slot dx */
		"ret\n");
}

__attribute__((naked)) static void shadow_next(void)
{
	__asm__ volatile(
		"cmpl $" STR(CL_LIMIT) ", %eax\n\t"
		"jb 1f\n\t"
		"pushl $0x4fdc71\n\t"   /* pool full: return 0xffff */
		"ret\n"
		"1:\n\t"
		"pushl $0x4fdc5f\n\t"   /* next slot */
		"ret\n");
}

__attribute__((naked)) static void shadow_found(void)
{
	__asm__ volatile(
		"cmpl $" STR(CL_LIMIT) ", %eax\n\t"
		"jb 1f\n\t"
		"pushl $0x4fdc7c\n\t"
		"ret\n"
		"1:\n\t"
		"pushl $0x4fdc82\n\t"   /* use slot eax */
		"ret\n");
}

typedef struct {
	u32 addr;
	u8  n;
	u8  expected[6];
	u8  value[6];
} Edit;

static void imm32(u8 *b, u32 v) { b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24; }

static Edit mov_ecx(u32 addr, u32 old, u32 value)
{
	Edit e = { addr, 5, { 0xb9 }, { 0xb9 } };
	imm32(e.expected + 1, old);
	imm32(e.value + 1, value);
	return e;
}

static Edit cmp_dx_imm16(u32 addr, u16 old, u16 value)
{
	Edit e = { addr, 5, { 0x66, 0x81, 0xfa, old, old >> 8 }, { 0x66, 0x81, 0xfa, value, value >> 8 } };
	return e;
}

/* Replace n bytes with jmp target (+ nops) */
static Edit jump(u32 addr, const u8 *old, u8 n, void (*target)(void))
{
	Edit e = { addr, n };
	for (u8 i = 0; i < n; i++) { e.expected[i] = old[i]; e.value[i] = 0x90; }
	e.value[0] = 0xe9;
	imm32(e.value + 1, (u32)target - (addr + 5));
	return e;
}

/* ---- Tail relocation ------------------------------------------------------------------- */

static u32 relocate(u32 offset)
{
	for (unsigned i = 0; i < sizeof cl_segments / sizeof cl_segments[0]; i++)
		if (offset >= cl_segments[i][0] && offset < cl_segments[i][1])
			return cl_segments[i][2] + (offset - cl_segments[i][0]);
	return offset;
}

static int g_installed;

u32 char_limit_gs_offset(u32 offset)
{
	return g_installed ? relocate(offset) : offset;
}

static int sites_match(u32 base, const struct cl_site *s, unsigned n, const char *what)
{
	for (unsigned i = 0; i < n; i++)
		if (*(u32 *)(base + s[i].addr + s[i].off) != s[i].old) {
			log_msg("character limits: %s site 0x%x changed (%08x), skipped", what,
			        base + s[i].addr, *(u32 *)(base + s[i].addr + s[i].off));
			return 0;
		}
	return 1;
}

static void sites_patch(u32 base, const struct cl_site *s, unsigned n)
{
	for (unsigned i = 0; i < n; i++) {
		u32 v = relocate(s[i].old);
		patch_write(base + s[i].addr + s[i].off, &v, 4);
	}
}

void char_limit_install(void)
{
	static const u8 activate_next_old[]  = { 0x66, 0x83, 0xfa, 0x40, 0x72, 0xdd };
	static const u8 activate_found_old[] = { 0x66, 0x83, 0xfa, 0x40, 0x73, 0xed };
	static const u8 shadow_next_old[]    = { 0x83, 0xf8, 0x20, 0x72, 0xee };
	static const u8 shadow_found_old[]   = { 0x83, 0xf8, 0x20, 0x72, 0x06 };
	const unsigned n_oni = sizeof cl_oni_sites / sizeof cl_oni_sites[0];
	const unsigned n_daodan = sizeof cl_daodan_sites / sizeof cl_daodan_sites[0];
	u32 daodan = (u32)GetModuleHandleA("binkw32.dll");

	Edit edits[] = {
		mov_ecx(ONcGameState_AllocSize, CL_OLD_SIZE, CL_NEW_SIZE),
		mov_ecx(ONcGameState_Clear, CL_OLD_SIZE / 4, CL_NEW_SIZE / 4),
		mov_ecx(ONcLevelReset_CharClear, 128 * 0x16a0 / 4, CL_CHAR_DWORDS),
		mov_ecx(ONcLevelReset_ActiveClear, 64 * 0x2838 / 4, CL_ACTIVE_DWORDS),
		cmp_dx_imm16(ONcNewCharacter_Bound1, 0x80, CL_LIMIT),
		cmp_dx_imm16(ONcNewCharacter_Bound2, 0x80, CL_LIMIT),
		jump(ONcActivate_Next, activate_next_old, sizeof activate_next_old, activate_next),
		jump(ONcActivate_Found, activate_found_old, sizeof activate_found_old, activate_found),
		jump(ONcShadow_Next, shadow_next_old, sizeof shadow_next_old, shadow_next),
		jump(ONcShadow_Found, shadow_found_old, sizeof shadow_found_old, shadow_found),
	};
	const unsigned n_edits = sizeof edits / sizeof edits[0];

	for (unsigned i = 0; i < n_edits; i++)
		if (!code_matches(edits[i].addr, edits[i].expected, edits[i].n)) {
			log_msg("character limits: 0x%x changed, skipped (stock limits kept)", edits[i].addr);
			return;
		}
	if (!sites_match(0, cl_oni_sites, n_oni, "Oni.exe")) return;
	/* Daodan's character-name lookup (DDrGetCharacterIndexFromName) reads list 3 directly */
	if (daodan && !sites_match(daodan, cl_daodan_sites, n_daodan, "Daodan")) return;

	for (unsigned i = 0; i < n_edits; i++)
		patch_write(edits[i].addr, edits[i].value, edits[i].n);
	sites_patch(0, cl_oni_sites, n_oni);
	if (daodan) sites_patch(daodan, cl_daodan_sites, n_daodan);
	g_installed = 1;
	log_msg("character limits: %d characters, all active (game state 0x%x bytes, %d + %d sites)",
	        CL_LIMIT, CL_NEW_SIZE, n_oni, daodan ? n_daodan : 0);
}
