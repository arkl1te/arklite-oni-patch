/* The kill feed: each death in a multiplayer game as a line in the top left corner (the score
 * has the top right), in pictures rather than words: "Killer [icon] Victim", or "[icon] Victim" for a death
 * nobody is credited with (players.c "Score": the same deaths that cost a point).
 *
 * Icons, by how the death was caused (players.c works it out from the hits, see obit_hit_cause):
 *   - a weapon: the weapon class's own icon (ONWC +0x4c, the 64x64 picture Oni's HUD shows for
 *     the weapon held), read from the player's game. A player's own weapon too ("[icon] Victim").
 *   - a hand-to-hand hit (punches, kicks, throws): a fist.
 *   - a super move: the fist in front of a burst (see "Super moves").
 *   - an explosion: a player's death blast (Mad Bomber's, players.c blast_player).
 *   - a fall: Oni's fall out of the level (owner type 5, Falling).
 *   - anything else: a skull.
 * The fist, burst, explosion, fall and skull are Arklite's own art (tools/gen_obit_icons.py,
 * obit_icons.h), in the style of Oni's weapon icons: no Oni data in the DLL.
 *
 * The feed is tick state: lines are added in the tick that sees the death, so a rollback that
 * takes a death back takes its line back too, and every PC shows the same lines. A line stays
 * OBIT_TICKS and fades over the last OBIT_FADE; OBIT_MAX at once, the oldest dropped first.
 *
 * Super moves: hand-to-hand hits land through HandleAttackMask (0x4e7740: ECX attacker, EDX
 * defender, 6 stack arguments: the active attacker, the active defender, the attack (a copy of the
 * animation's TRtAttack), its index, the attacker's and the defender's parts; ret 0x18), called
 * twice from 0x4e73e0 (at 0x4e7688 and 0x4e7703). Both calls go through melee_hit, which notes the
 * attack while it runs: the TakeDamage calls inside it (mp_rules.c reports them to players.c) ask
 * obit_melee_super. A super move is an attack with the SpecialMove flag (attack +8, bit 3; it goes
 * through a block at half damage, OniSplit's "HalfDamage": Konoko's Rising Fury Punch) or one of
 * an animation whose shout is a super move's (TRrAnimation_GetVocalization 0x428970, ECX the
 * animation at active +0x1af8: 6..9, the class's Super Punch/Kick/3/4 sounds; Konoko's Devil Spin
 * Kick, which is only unblockable).
 *
 * Drawing: the icons go through the same M3 calls as M3rDraw_Bitmap (0x41f4a0): push the draw
 * state (0x41e1f0), the texture (0x41e1b0, ECX 4), appearance 3, interpolation 0, the shade (7),
 * alpha (9) and fill 2 (0x41e170, ECX the state, EDX the value), commit (0x41e340), the sprite
 * (0x41a5e0: 2 corners {x, y, z, 1/w}, 4 UV pairs), pop (0x41e2b0), but scaled to the line, which
 * M3rDraw_Bitmap can't do (texels 1:1). Our icons are TXMPs made with M3rTextureMap_New at 8x8 and
 * pointed at the texels in the DLL (ARGB4444, Oni's type 0 as the stock icons), before a network
 * match starts (hud_textures_prepare: see char_menu.c for why). */
#include "arklite.h"
#include "obit_icons.h"

#define ONgGameState        (*(u8 **)0x5ece7c)
#define GS_TICK(gs)         (*(u32 *)((gs) + 0x144))

#define OBIT_MAX    5
#define OBIT_TICKS  (6 * 60)
#define OBIT_FADE   60

typedef struct {
	u32 tick;      /* + 1, 0: an empty line */
	signed char killer, victim;
	u8 kind;
	u8 *weapon;    /* OBIT_WEAPON: its ONWC */
} Obit;

static Obit g_obit[OBIT_MAX] TICK_STATE;   /* [0] the newest */

/* ---- Causes ----------------------------------------------------------------------------- */

#define WP_WEAPONS          ((u8 *)0x6221c0)
#define WP_WEAPON_SIZE      0x170
#define WP_WEAPON_SLOTS     64
#define WEAPON_INUSE(w)     (*(u8 *)((w) + 2) & 1)
#define WEAPON_CLASS(w)     (*(u8 **)((w) + 4))
#define WEAPON_OWNER(w)     (*(u8 **)((w) + 8))
#define ONWC_ICON(c)        (*(u8 **)((c) + 0x4c))

u8 *obit_weapon_class(u8 *c)
{
	if (!c) return 0;
	for (int i = 0; i < WP_WEAPON_SLOTS; i++) {
		u8 *w = WP_WEAPONS + i * WP_WEAPON_SIZE;
		if (WEAPON_INUSE(w) && WEAPON_OWNER(w) == c) return WEAPON_CLASS(w);
	}
	return 0;
}

#define HandleAttackMask        ((void (ONI_FC *)(u8 *, u8 *, u8 *, u8 *, const u8 *, u32, u32, u32))0x4e7740)
#define HandleAttackMask_Call1  0x4e7688
#define HandleAttackMask_Call2  0x4e7703
#define TRrAnimation_GetVocalization ((u16 (ONI_FC *)(const u8 *))0x428970)
#define ACTIVE_ANIMATION(a)     (*(const u8 **)((a) + 0x1af8))
#define ATTACK_FLAGS(t)         (*(const u32 *)((t) + 8))
#define ATTACK_SPECIAL_MOVE     0x8
#define VOC_SUPER_FIRST         6
#define VOC_SUPER_LAST          9

static u8 *g_melee_attacker;   /* while an attack lands: its character, and whether it's a super move */
static int g_melee_super;

static void ONI_FC melee_hit(u8 *attacker, u8 *defender, u8 *active_attacker, u8 *active_defender, const u8 *attack,
                             u32 index, u32 attacker_parts, u32 defender_parts)
{
	u8 *was = g_melee_attacker;
	int was_super = g_melee_super;
	const u8 *anim = active_attacker ? ACTIVE_ANIMATION(active_attacker) : 0;
	u16 voc = anim ? TRrAnimation_GetVocalization(anim) : 0xffff;

	g_melee_attacker = attacker;
	g_melee_super = (attack && (ATTACK_FLAGS(attack) & ATTACK_SPECIAL_MOVE)) || (voc >= VOC_SUPER_FIRST && voc <= VOC_SUPER_LAST);
	HandleAttackMask(attacker, defender, active_attacker, active_defender, attack, index, attacker_parts, defender_parts);
	g_melee_attacker = was;
	g_melee_super = was_super;
}

int obit_hit_cause(u8 *attacker, u32 owner, u8 **weapon)
{
	*weapon = 0;
	switch (owner >> 24) {
	case 1:   /* a character's weapon */
		*weapon = obit_weapon_class(attacker);
		return OBIT_WEAPON;
	case 2:   /* hand to hand */
		return attacker && attacker == g_melee_attacker && g_melee_super ? OBIT_SUPER : OBIT_MELEE;
	case 5:
		return OBIT_FALL;
	default:
		return OBIT_SKULL;
	}
}

/* ---- The feed --------------------------------------------------------------------------- */

void obit_add(int killer, int victim, int kind, u8 *weapon)
{
	if (!ONgGameState) return;
	if (kind == OBIT_WEAPON && !weapon) kind = OBIT_SKULL;
	for (int i = OBIT_MAX - 1; i > 0; i--) g_obit[i] = g_obit[i - 1];
	g_obit[0].tick = GS_TICK(ONgGameState) + 1;
	g_obit[0].killer = (signed char)killer;
	g_obit[0].victim = (signed char)victim;
	g_obit[0].kind = (u8)kind;
	g_obit[0].weapon = weapon;
}

void obit_reset(void)
{
	for (int i = 0; i < OBIT_MAX; i++) g_obit[i].tick = 0;
}

/* ---- Icons ------------------------------------------------------------------------------ */

#define M3rTextureMap_New       ((u16 (ONI_FC *)(u32, u32, u32, u32, u32, const char *, u8 **))0x41eb00)
#define PIXEL_ARGB4444          0
#define TXMP_W(t)               (*(u16 *)((t) + 0x84))
#define TXMP_H(t)               (*(u16 *)((t) + 0x86))
#define TXMP_TEXELS(t)          (*(const void **)((t) + 0x94))
#define TXMP_DIRTY(t)           (*(u8 *)((t) + 0xa0))

static u8 *g_icon[OBIT_ICONS];
static int g_icons_tried;

/* Kind -> our icon (OBIT_WEAPON has the weapon's) */
static const int g_kind_icon[] = {
	[OBIT_SKULL] = OBIT_ICON_SKULL, [OBIT_MELEE] = OBIT_ICON_MELEE, [OBIT_SUPER] = OBIT_ICON_SUPER,
	[OBIT_BLAST] = OBIT_ICON_BLAST, [OBIT_FALL] = OBIT_ICON_FALL, [OBIT_WEAPON] = OBIT_ICON_SKULL,
};

/* Made once, in the file that outlives levels (where 1), kept out of rollback's restores */
void obit_textures_prepare(void)
{
	static const char *const names[OBIT_ICONS] = { "arklite_obit_melee", "arklite_obit_super", "arklite_obit_blast",
	                                               "arklite_obit_skull", "arklite_obit_fall" };
	if (g_icons_tried) return;
	g_icons_tried = 1;
	for (int i = 0; i < OBIT_ICONS; i++) {
		u8 *t = 0;
		if (M3rTextureMap_New(8, 8, PIXEL_ARGB4444, 1, 0, names[i], &t) || !t) {
			log_msg("obituaries: can't make texture %s", names[i]);
			continue;
		}
		TXMP_W(t) = TXMP_H(t) = OBIT_ICON_SIZE;
		TXMP_TEXELS(t) = g_obit_icon_texels[i];
		TXMP_DIRTY(t) = 1;
		rollback_keep(t - 8, 8 + 0xa4);
		g_icon[i] = t;
	}
}

#define M3rDraw_State_Push      ((void (ONI_FC *)(void))0x41e1f0)
#define M3rDraw_State_Pop       ((void (ONI_FC *)(void))0x41e2b0)
#define M3rDraw_State_SetPtr    ((void (ONI_FC *)(u32, const void *))0x41e1b0)
#define M3rDraw_State_SetInt    ((void (ONI_FC *)(u32, u32))0x41e170)
#define M3rDraw_State_Commit    ((void (ONI_FC *)(void))0x41e340)
#define M3rDraw_Sprite          ((u32 (ONI_FC *)(const float *, const float *))0x41a5e0)
#define STATE_TEXTURE           4
#define STATE_APPEARANCE        0
#define STATE_INTERPOLATION     1
#define STATE_FILL              2
#define STATE_SHADE             7
#define STATE_ALPHA             9

static void icon_draw(const u8 *texture, int x, int y, int size, u32 alpha)
{
	static const float uv[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };
	float pts[8] = { (float)x, (float)y, 0.5f, 2.0f, (float)(x + size), (float)(y + size), 0.5f, 2.0f };

	M3rDraw_State_Push();
	M3rDraw_State_SetPtr(STATE_TEXTURE, texture);
	M3rDraw_State_SetInt(STATE_APPEARANCE, 3);
	M3rDraw_State_SetInt(STATE_INTERPOLATION, 0);
	M3rDraw_State_SetInt(STATE_SHADE, 0xffffffffu);
	M3rDraw_State_SetInt(STATE_ALPHA, alpha);
	M3rDraw_State_SetInt(STATE_FILL, 2);
	M3rDraw_State_Commit();
	M3rDraw_Sprite(pts, uv);
	M3rDraw_State_Pop();
}

/* ---- On screen -------------------------------------------------------------------------- */

#define WHITE   0x00ffffffu
#define YELLOW  0x00ffe040u
#define GAP     mp_hud_px(8)   /* between a name and the icon (px at 1050 tall, mp_hud.c) */

/* The age of a line on screen now, or -1 when it isn't */
static int obit_age(const Obit *o, u32 now)
{
	if (!o->tick || now + 1 < o->tick) return -1;   /* none, or from before a level restart */
	return now + 1 - o->tick < OBIT_TICKS ? (int)(now + 1 - o->tick) : -1;
}

/* The feed's widest line, for HUD Centering (mp_hud.c), with the HUD's text font. It only grows
 * while lines show, so the feed doesn't move as they come and go. */
int obit_width(void)
{
	static int widest;
	int line = mp_hud_line_height(), now_widest = 0, any = 0;
	u32 now;

	if (!ONgGameState || !line) return widest;
	now = GS_TICK(ONgGameState);
	for (int i = 0; i < OBIT_MAX; i++) {
		const Obit *o = &g_obit[i];
		int w = line * 2 + GAP + mp_hud_text_width(mp_name(o->victim), -1);

		if (obit_age(o, now) < 0) continue;
		any = 1;
		if (o->killer >= 0) w += mp_hud_text_width(mp_name(o->killer), -1) + GAP;
		if (w > now_widest) now_widest = w;
	}
	if (!any) widest = 0;
	else if (now_widest > widest) widest = now_widest;
	return widest;
}

/* From the HUD (mp_hud.c), with its text font: lines from y down, left-aligned to x */
void obit_draw(int x, int y, int local)
{
	u32 now;
	int line = mp_hud_line_height(), size = line * 2, row = size + mp_hud_px(2);

	if (!ONgGameState || !line) return;
	if (!g_icons_tried && !net_synced()) obit_textures_prepare();   /* a game on one PC: no rollback */
	now = GS_TICK(ONgGameState);
	for (int i = 0; i < OBIT_MAX; i++) {
		const Obit *o = &g_obit[i];
		const u8 *icon;
		u32 age, alpha;
		int left = x, ty = y + (size - line) / 2;

		if (obit_age(o, now) < 0) continue;
		age = (u32)obit_age(o, now);
		alpha = age + OBIT_FADE <= OBIT_TICKS ? 255 : (OBIT_TICKS - age) * 255 / OBIT_FADE;
		icon = o->kind == OBIT_WEAPON && o->weapon ? ONWC_ICON(o->weapon) : g_icon[g_kind_icon[o->kind]];

		if (o->killer >= 0) {
			mp_hud_text(mp_name(o->killer), left, ty, MP_LEFT, alpha << 24 | (o->killer == local ? YELLOW : WHITE));
			left += mp_hud_text_width(mp_name(o->killer), -1) + GAP;
		}
		if (icon) icon_draw(icon, left, y, size, alpha);
		left += size + GAP;
		mp_hud_text(mp_name(o->victim), left, ty, MP_LEFT, alpha << 24 | (o->victim == local ? YELLOW : WHITE));
		y += row;
	}
}

void obituary_install(void)
{
	if (patch_call(HandleAttackMask_Call1, (u32)HandleAttackMask, (u32)melee_hit) &&
	    patch_call(HandleAttackMask_Call2, (u32)HandleAttackMask, (u32)melee_hit))
		log_msg("obituaries: kill feed on, super moves told apart");
	else
		log_msg("obituaries: 0x4e7740's calls changed, every hand-to-hand kill shows the fist");
}
