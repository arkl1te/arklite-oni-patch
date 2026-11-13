/* The phase cloak in network games: the cloaked player sees themselves half there (the body at
 * 50% opacity, no shimmer); every other player sees nothing of them at all, except the shimmer
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
 *   - body and shadow alpha are 0 while the cloak is on (invis_active and more than 120 ticks
 *     left); the last 120 ticks fade the body back in as Oni does.
 *   - the shimmer is drawn only after this character attacked: the two attacker sites are calls
 *     that note the game tick of the blow (and answer invis_active as before, so the game itself
 *     does what it did). Its opacity is the one Oni would have given the body for the blow: 0.6,
 *     fading 0.0085 a tick, gone ~71 ticks (1.2 s) later (Oni's own shimmer never passes 0.22).
 * For the character this screen belongs to (in a network game, not a spectator's screen): the
 * body and shadow alpha are at least 0.5 while invis_active (Oni's fade from 1 at pickup stops at
 * 0.5, and its fade back in at the end starts once it passes 0.5; a blow's 0.6 still shows), and
 * the shimmer isn't drawn (user request, 2026-11-13).
 * Off the network everything is as in Oni (single player's cloaked ninjas keep their shimmer and
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
#define SELF_ALPHA            0.5f     /* the cloaked player's own body, on their own screen */
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

/* The character this screen belongs to, in a network game (not on a spectator's screen) */
static int self_view(u8 *c)
{
	u8 *gs = ONgGameState;
	return net_active() && gs && c == GS_PLAYER(gs) && net_watching() == -1;
}

int invis_hidden(u8 *c)
{
	u8 *a;
	if (!c || net_watching() != -1) return 0;   /* a spectator sees every name */
	if (CHR_INVIS_LEFT(c)) return 1;
	a = ONrCharacter_GetActive(c);
	return a && ACT_INVIS_ACTIVE(a) && ACT_INVIS_ALPHA(a) < 1.0f;
}

/* Body and shadow alpha (as float bits) */
u32 invis_body_alpha(u8 *act, u8 *c)
{
	u32 bits;
	float a = ACT_INVIS_ALPHA(act);
	if (ACT_INVIS_ACTIVE(act)) {
		if (CHR_INVIS_LEFT(c) > INVIS_FADE_IN_TICKS && !as_stock(c)) return 0;
		if (a < SELF_ALPHA && self_view(c)) a = SELF_ALPHA;
	}
	memcpy(&bits, &a, 4);
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

/* ---- stubs ----------------------------------------------------------------------------------- */

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
	call_at(ONcDraw_ShadowAlpha, body_alpha, 6);
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
	log_msg("invis: in network games a cloaked player sees themselves half see-through, others see them only as a shimmer when they strike");
}
