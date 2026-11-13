/* Multiplayer step 2: more players than Konoko: a second player on the same PC on a gamepad,
 * and in network games one player per PC (up to PLAYERS_MAX).
 *
 * Players are numbered by slot: slot 0 is Konoko (gs+0xac, PlayerCharacter), slot s the
 * character spawned from the (s+1)th Player-flagged CHAR placement. AI2 setup (0x48a6b0) gives
 * a Player placement charType 0 (+0x1e8, no AI; AI characters are 2).
 *
 * Oni spawns only one of them: the level start walks the CHAR objects (0x4d0080, type 'CHAR')
 * with 0x48a3a0, which spawns the first with IsPlayer (object +0x2c bit 0, +0x2c |= 0x20 for
 * spawned, ONrGameState_NewCharacter 0x4dac50) and stops the walk. The three walks with it
 * (0x48a2ee, 0x48a419, 0x48a518) are wrapped: after each, Arklite walks again and spawns the
 * next placements, up to the player count (players_set_count: 2 on one PC and in a network
 * lobby, the match's players in a network match; never more than the map has), keeping Konoko
 * in gs+0xac (NewCharacter makes every charType 0 character the player). Placements past the
 * count are only spawn points.
 *
 * Every tick (0x4fa6a0) turns that tick's action record into the input state at gs+0xb8
 * (0x30 bytes: +0 aim up/down and +4 aim left/right from the record's +0x28/+0x24, +0x10
 * buttons held, +0x18 not held, +0x20 pressed, +0x28 released; the buttons go through
 * 0x4fa520 first, which reads the character in gs+0xac) and hands it to the player with
 * ONrCharacter_SetInput (0x4ed420, call at 0x4fa934), which copies it into the character's
 * active data (+0x2158). The AI drives its characters through that same slot. Right after
 * Konoko's call, Arklite does the same for every other player with that player's record and its
 * own input state, pointing gs+0xac at that player while 0x4fa520 runs.
 *
 * Player 2's record is made once per frame from an XInput gamepad, like Daodan's input makes
 * Konoko's: the first tick of the frame gets it, later ticks keep the buttons held and don't
 * aim. lockstep.c records and replays it with Konoko's. Action bits (the binding table at
 * 0x52daf8: {u16 type, u16 index, char name[32]}):
 *
 *   left stick: forward 21, backward 22, stepleft 25, stepright 26 (aim: record +0x24/+0x28)
 *   A jump 27   B crouch 28   X punch 29   Y kick 30   LB walk 32   RB action 33
 *   RT fire1 38   LT fire2 39   R3 fire3 40   L3 block 31   Back hypo 34
 *   d-pad up reload 35, left/right swap 36, down drop 37   (Start: nothing, no pausing)
 *
 * A game with player 2 is a multiplayer game (multiplayer_active), and in one:
 *   - auto-aim is off: 0x4efad0 (in the tick, from 0x4de1ed and 0x4de211) pulls the shooter's
 *     aim onto a hostile on the local screen; with lag, players aim ahead instead.
 *   - nothing pauses: see live_menu.c.
 *
 * Spawn points (a map with two or more Player-flagged characters is a multiplayer map): every
 * Player-flagged CHAR placement is a spawn point. The first is Konoko's own placement, the next
 * ones those of players 2, 3, ... (as many as the player count); the rest are only spawn points. Each player starts at a spawn point picked at random (Konoko by moving her
 * placement there for the game's own walk, the others the same way for NewCharacter) and, after
 * dying, comes back at one: once respawn_delay seconds have passed since the death tick (char
 * +0x1638, set by the death function 0x4eb550 with the dead flag, +4 bit 0), the first tick
 * whose input state has an attack button pressed (punch, kick, fire1-3) respawns that player:
 * the body becomes a corpse
 * (0x4ef340, as the character update does for dead AIs), the character is deleted
 * (ONrGameState_DeleteCharacter 0x4dc480) and spawned again from its own placement, moved to the
 * spawn point. A character spawned from a placement takes the placement's position (+0xc) and
 * facing (+0x228). The pick is random among the points no other living player is near (the
 * farthest one if none), from a hash of the level, the tick, the player and the level load's
 * spawn seed (choose_spawn_seed: in a network game the host's match seed, so every PC picks the
 * same; offline the clock's; a replay its recording's), so every game starts somewhere new and
 * every PC and replay agrees. Respawning runs in the tick, right after every player's input.
 *
 * Character choice (F2, char_menu.c): word 11 of each player's action record carries the class
 * that player comes back as (PLAYERS_CHOICE_TAG | ONCC instance number + 1, or the tag alone
 * for the placement's own class). The tick notes it for Konoko from the tick's action
 * (players_tick_record, from lockstep.c's tick hook) and for the others from their records, so
 * every PC and every replay knows it at the same tick. A respawn with a class noted puts that
 * class's name in the placement (+0x30, char[64], what NewCharacter looks up) and clears its
 * RandomCostume and UpgradeDifficulty flags (0x802, which would pick another class of the same
 * variant) while the character is made, then puts both back. The first spawn always uses the
 * placement's class. The choices start over with the level and with a network match.
 *
 * In a network match (not the warm-up: players_set_limits on) a living player's new choice waits
 * for their respawn; only the first choice the tick sees for them in the match (the one they made
 * in the warm-up, or before joining in progress) still changes them at once, so nobody starts the
 * match as the map's class. Elsewhere (the warm-up, a game alone) a living player doesn't have to
 * die: a new choice changes their class at once, wherever they are, as the shapeshifter cheat does (0x4d7c30: ECX the character, EDX the ONCC; called from
 * utility input at 0x4f62fb). It swaps the class (char +0xc) and its animations in place, so
 * position, health, weapons and inventory stay. Choosing the map's class goes back to the class
 * the player was spawned as. Every PC does it in the tick from the shared input, like
 * respawning. A player
 * who comes back as another class gets the health the map gives its own player (the maximum,
 * +0x3c, the player had when spawned with the placement's class; current health +0x38
 * set to it too), so every character is as tough as the map's Konoko. On a multiplayer map that
 * health is MP_HEALTH (125) for every player and class, set at each spawn; the in-place class
 * change (0x4d7c30) doesn't touch health, so it stays.
 *
 * Kill camera (a level's cm_detach, as in Level 3's vat: the camera stays and the dead player drops
 * out of view). Stock cm_detach is written for one player and a level reload: it leaves follow mode
 * on the PC that runs it and nothing brings it back, and a tick's script runs on every PC, with
 * chr_is_player true only for gs+0xac, Konoko, in every tick. So in a multiplayer game
 * chr_is_player is true for every player, and cm_detach (0x4bbfd0) moves no camera: it marks the
 * player chr_is_player was just asked about, which is why a level must ask first (stock has no
 * better source: ONgCurrentlyActiveTrigger_Cause, 0x5ecbbc, is the last character the trigger loop
 * looked at by the time the deferred scripts run). The
 * mark is tick state, so every PC has it and a rollback takes it back. net.c's camera update asks
 * players_killcam each time and leaves this PC's camera where it is while the character it shows
 * is marked and dead. The respawn clears the mark, and the camera follows again from there.
 *
 * Arklite.ini [players]: p2=0 turns it off, p2_pad (0..3), p2_aim_speed (8: aim per tick at
 * full right stick), p2_invert_y. [multiplayer] respawn_delay (1.5 seconds). The camera follows
 * Konoko. */
#include "arklite.h"

typedef unsigned long long u64;

#define ONgGameState            (*(u8 **)0x5ece7c)
#define GS_PLAYER(gs)           (*(u8 **)((gs) + 0xac))
#define GS_TICK(gs)             (*(u32 *)((gs) + 0x144))
#define GS_SERVER_TIME(gs)      (*(u32 *)((gs) + 0x148))
#define GS_CHARACTERS(gs)       ((gs) + 0x1260)
#define GS_INPUT_ON             0x1679ad   /* u8, stock offset: with +0x1679ae == 0, input is on */
#define GS_INPUT_OFF            0x1679ae
#define GS_ACTION_MASK          0x1679b0   /* u64, stock offset: the main loop masks records */
#define ONgInvertLook           (*(u8 *)0x5ec70f)
#define ONgCharacterSlots       (*(u16 *)(0x4dac95 + 3))
#define ONrLevel_GetCurrent     ((u16 (ONI_FC *)(void))0x5054c0)

#define ONrCharacter_SetInput   ((void (ONI_FC *)(u8 *, u32 *))0x4ed420)
#define ONcTick_SetInputCall    0x4fa934
#define ONiPlayerButtons        ((u64 (__stdcall *)(u32, u32))0x4fa520)

#define OBJrEnumerate           ((u32 (ONI_FC *)(u32, void *, u32))0x4d0080)
#define OBJcType_CHAR           0x43484152   /* 'CHAR' */
#define ONrGameState_NewCharacter ((u16 (ONI_FC *)(u8 *, u32, u32, u32 *))0x4dac50)
#define OBJ_FLAGS(o)            (*(u32 *)((o) + 0x2c))
#define OBJ_ISPLAYER            0x1
#define OBJ_SPAWNED             0x20
#define OBJ_CLASS(o)            ((char *)(o) + 0x30)      /* char[64], the ONCC's name */
#define OBJ_NAME(o)             ((const char *)(o) + 0x70)
#define OBJ_RANDOM_CLASS        0x802   /* RandomCostume, UpgradeDifficulty */
#define TAG_ONCC                0x4f4e4343

#define OBJ_POS(o)              ((u32 *)((o) + 0xc))      /* x, y, z */
#define OBJ_FACING(o)           (*(u32 *)((o) + 0x228))
#define ONrGameState_DeleteCharacter ((void (ONI_FC *)(u8 *))0x4dc480)
#define ONrCharacter_MakeCorpse ((void (ONI_FC *)(u8 *))0x4ef340)
#define ONrCharacter_SetClass   ((void (ONI_FC *)(u8 *, u8 *))0x4d7c30)

#define ONrCharacter_AutoAim    ((u8 (ONI_FC *)(u8 *, u8 *, u8 *))0x4efad0)

#define CHR_SIZE        0x16a0
#define CHR_FLAGS(c)    (*(u32 *)((c) + 4))
#define CHR_CLASS(c)    (*(u8 **)((c) + 0xc))     /* the ONCC */
#define CHR_INUSE       0x8000
#define CHR_NOINPUT     0x400000   /* the tick drops the player's record */
#define CHR_TYPE(c)     (*(u32 *)((c) + 0x1e8))   /* 0 player, 2 AI */
#define CHR_NAME_OFFSET 0x14                      /* char[32], what scripts find a character by */
#define CHR_NAME_MAX    32
#define CHR_HELD(c)     (*(u32 *)((c) + 0x162c))  /* no aiming while set */
#define CHR_DEAD        0x1
#define CHR_NOCORPSE    0x80000   /* the character update makes no corpse for it either */
#define CHR_ACTIVE(c)   (*(u16 *)((c) + 2))       /* active data index, 0: none */
#define CHR_POS(c)      ((float *)((c) + 0x148))
#define CHR_HEALTH(c)   (*(int *)((c) + 0x38))
#define CHR_MAX_HEALTH(c) (*(int *)((c) + 0x3c))
#define MP_HEALTH       125   /* every player's health on a multiplayer map */
#define CHR_DEATH_TICK(c) (*(u32 *)((c) + 0x1638))

__declspec(dllimport) HANDLE WINAPI LoadLibraryA(const char *);
__declspec(dllimport) void * WINAPI GetProcAddress(HANDLE, const char *);
__declspec(dllimport) int  WINAPI QueryPerformanceCounter(u64 *);
__declspec(dllimport) u32  WINAPI GetTickCount(void);
void *memset(void *, int, unsigned);
void *memcpy(void *, const void *, unsigned);

typedef struct {
	u32 packet;
	u16 buttons;
	u8 lt, rt;
	short lx, ly, rx, ry;
} XINPUT_STATE;
typedef u32 (WINAPI *XInputGetState_t)(u32, XINPUT_STATE *);

static int g_enabled, g_pad, g_invert_y;
static float g_aim_speed = 8.0f;
static XInputGetState_t g_xinput;

#define PL              PLAYERS_MAX
static int g_pl[PL] TICK_STATE = { -1, -1, -1, -1 };   /* slot s's character index ([0]: Konoko, in gs+0xac), -1: none */
static u32 g_level;
static u32 g_input[PL][12] TICK_STATE; /* each player's input state, like gs+0xb8 (slot 0 unused) */
static u32 g_record[12];               /* this frame's record for player 2 (gamepad) */
static int g_has_record, g_tick_index;
static const u32 (*g_net_records)[6][12];  /* networked game: records[slot][tick] (net.c) */
static u32 g_net_count;
static u32 g_count = 2;                /* players to spawn (players_set_count) */

#define MAX_SPAWN       64
#define SPAWN_CLEAR     60.0f   /* no other living player this near: a free spawn point */
static u8 *g_spawn[MAX_SPAWN];         /* Player-flagged placements: [s] slot s's, then spawn points only */
static int g_nspawn;
static u32 g_spawn_level;             /* for the spawn pick only: the level isn't current yet */
static u32 g_spawn_seed;              /* this level load's spawn picks (players_spawn_seed) */
static u32 g_next_spawn_seed;         /* players_set_spawn_seed's, for the next load */
static int g_next_spawn_seed_set;
static int g_spawn_seed_due = 1;      /* a level is loading (players_level_begin): pick a new seed */
static u32 g_respawn_ticks = 90;       /* dead this long, an attack button brings a player back */
/* Score (see "Score" below) */
#define KILL_CREDIT     600     /* 10 s */
static int g_score[PL] TICK_STATE, g_was_dead[PL] TICK_STATE;
static int g_class[PL] TICK_STATE = { -1, -1, -1, -1 };   /* ONCC instance number chosen with F2, -1: the placement's */
static int g_base_health[PL] TICK_STATE;          /* each player's maximum health with the map's class */
static int g_as[PL] TICK_STATE = { -1, -1, -1, -1 };      /* the class each player's character is, -1: the map's */
static u8 *g_map_class[PL] TICK_STATE;            /* the ONCC each player was last spawned as with the map's class */
/* Match (not warm-up): a choice waits for the respawn, except the first one the tick sees for the
 * player (made in the warm-up or before joining), which still changes them at once */
static u8 g_noted[PL] TICK_STATE, g_settled[PL] TICK_STATE;
static int g_hit_by[PL] TICK_STATE = { -1, -1, -1, -1 };
static u32 g_hit_tick[PL] TICK_STATE;
static u32 g_last_owner[PL] TICK_STATE;           /* last hit's owner handle that credited no one */
/* The kill feed's icon (obituary.c, OBIT_*): of the hit g_hit_by credits, and of the last hit that
 * credited no one (0, a skull: none); the weapon's ONWC for OBIT_WEAPON */
static u8 g_hit_cause[PL] TICK_STATE, g_last_cause[PL] TICK_STATE;
static u8 *g_hit_weapon[PL] TICK_STATE, *g_last_weapon[PL] TICK_STATE;
static u8 *g_held[PL] TICK_STATE;                 /* the last weapon each player held */
#define BLAST_CREDIT    300     /* 5 s: a death effect's blast (mad_bomber's goes off 3.5 s after) */
#define BLAST_REACH     150.0f  /* from where the player died (mad_x01's radius is 75) */
static u32 g_death_tick[PL] TICK_STATE;           /* tick + 1 of each player's last death, 0: none */
static float g_death_pos[PL][3] TICK_STATE;
/* The network match's limits (net.c, the host's Host dialog): see "Score" below */
static int g_limits_on, g_limit_score;
static u32 g_limit_ticks, g_limit_start;
static u8 g_killcam[PL] TICK_STATE;              /* the level's cm_detach marked this player's death */
/* The players who have a character, as the network host's record says (see "Joining and
 * leaving"); g_present_known: a record said it since the level loaded */
static u32 g_present TICK_STATE;
static int g_present_known TICK_STATE;
static u32 g_over_tick TICK_STATE;               /* tick + 1 in which a limit was reached, 0: not yet */
static u32 g_spect TICK_STATE;                   /* players watching (/spectate), a bit per slot */
/* Attack buttons in the input state (record bits 29 punch, 30 kick; 38-40 fire1-3) */
#define ATTACK_LO       0x60000000u
#define ATTACK_HI       0x1c0u

static u8 *gs_field(u32 stock_offset)
{
	return ONgGameState + char_limit_gs_offset(stock_offset);
}

static u8 *character(int index)
{
	return GS_CHARACTERS(ONgGameState) + index * CHR_SIZE;
}

/* Slot s (1..) has its character: a player character (charType 0) that isn't Konoko */
static int slot_valid(int s)
{
	u8 *c;
	if (s < 1 || s >= PL || g_pl[s] < 0) return 0;
	c = character(g_pl[s]);
	return (CHR_FLAGS(c) & CHR_INUSE) && CHR_TYPE(c) == 0 && c != GS_PLAYER(ONgGameState);
}

/* A player character no slot has (charType 0, not Konoko), in index order, or -1: for a slot
 * whose character wasn't spawned by spawn_players (a level loaded some other way) */
static int find_unclaimed(void)
{
	u8 *p1 = GS_PLAYER(ONgGameState);
	u32 slots = ONgCharacterSlots;
	for (u32 i = 0; i < slots; i++) {
		u8 *c = character(i);
		int claimed = 0;
		if (!(CHR_FLAGS(c) & CHR_INUSE) || CHR_TYPE(c) != 0 || c == p1) continue;
		for (int s = 1; s < PL; s++) claimed |= slot_valid(s) && g_pl[s] == (int)i;
		if (!claimed) return i;
	}
	return -1;
}

int multiplayer_active(void)
{
	if (!g_enabled || !ONgGameState) return 0;
	if (net_synced() && ONrLevel_GetCurrent()) return 1;   /* a network session, even with no one else in it yet */
	for (int s = 1; s < PL; s++)
		if (slot_valid(s)) return 1;
	return 0;
}

static u8 ONI_FC auto_aim(u8 *shooter, u8 *active, u8 *target)
{
	return multiplayer_active() ? 0 : ONrCharacter_AutoAim(shooter, active, target);
}

/* ---- Gamepad ------------------------------------------------------------------------------ */

static void set_bit(u32 *rec, int bit) { rec[bit >> 5] |= 1u << (bit & 31); }

static float stick(short v)
{
	float f = v / 32767.0f;
	float a = f < 0 ? -f : f;
	if (a < 0.2f) return 0;                     /* dead zone */
	a = (a - 0.2f) / 0.8f;
	if (a > 1) a = 1;
	return (f < 0 ? -1 : 1) * a * a;            /* fine aim near the center */
}

static void poll_pad(u32 *rec, u32 ticks)
{
	XINPUT_STATE s;
	const short move = 12000;

	memset(rec, 0, 0x30);
	if (!g_xinput || g_xinput(g_pad, &s)) return;   /* no gamepad: player 2 stands */
	if (s.ly > move) set_bit(rec, 21);
	if (s.ly < -move) set_bit(rec, 22);
	if (s.lx < -move) set_bit(rec, 25);
	if (s.lx > move) set_bit(rec, 26);
	if (s.buttons & 0x1000) set_bit(rec, 27);      /* A */
	if (s.buttons & 0x2000) set_bit(rec, 28);      /* B */
	if (s.buttons & 0x4000) set_bit(rec, 29);      /* X */
	if (s.buttons & 0x8000) set_bit(rec, 30);      /* Y */
	if (s.buttons & 0x0040) set_bit(rec, 31);      /* L3 */
	if (s.buttons & 0x0100) set_bit(rec, 32);      /* LB */
	if (s.buttons & 0x0200) set_bit(rec, 33);      /* RB */
	if (s.buttons & 0x0020) set_bit(rec, 34);      /* Back */
	if (s.buttons & 0x0001) set_bit(rec, 35);      /* d-pad up */
	if (s.buttons & 0x000c) set_bit(rec, 36);      /* d-pad left/right */
	if (s.buttons & 0x0002) set_bit(rec, 37);      /* d-pad down */
	if (s.rt > 64) set_bit(rec, 38);
	if (s.lt > 64) set_bit(rec, 39);
	if (s.buttons & 0x0080) set_bit(rec, 40);      /* R3 */
	/* aim for every tick this frame, since only the first tick gets the record */
	float lr = stick(s.rx) * g_aim_speed * ticks;
	float ud = stick(s.ry) * g_aim_speed * 0.5f * ticks;
	if (g_invert_y) ud = -ud;
	memcpy(&rec[9], &lr, 4);
	memcpy(&rec[10], &ud, 4);
}

/* ---- Frames and ticks --------------------------------------------------------------------- */

static void new_level_check(void)
{
	u32 level = ONrLevel_GetCurrent();
	if (level != g_level) {
		for (int s = 0; s < PL; s++) {
			g_score[s] = 0;
			g_hit_by[s] = -1;
			g_class[s] = -1;
			g_held[s] = 0;   /* weapon classes: a level 0 swap frees the old ones */
		}
		obit_reset();
		g_level = level;
	}
	if (!level) return;
	for (int s = 1; s < PL && s < (int)g_count; s++) {
		if (slot_valid(s)) continue;
		if (g_present_known && !(g_present >> s & 1)) continue;   /* left: no character (the tick decides) */
		if (g_spect >> s & 1) continue;                            /* watching: no character */
		if (g_pl[s] >= 0) memset(g_input[s], 0, sizeof g_input[s]);
		g_pl[s] = find_unclaimed();
		if (g_pl[s] >= 0) {
			memset(g_input[s], 0, sizeof g_input[s]);
			log_msg("players: player %d is #%d %s", s + 1, g_pl[s], (const char *)character(g_pl[s]) + 0x14);
		}
	}
}

const u32 *players_begin_frame(void)
{
	g_tick_index = 0;
	g_has_record = 0;
	g_net_records = 0;
	if (!g_enabled || !ONgGameState) return 0;
	new_level_check();
	if (!slot_valid(1)) return 0;
	u32 ticks = GS_SERVER_TIME(ONgGameState) - GS_TICK(ONgGameState);
	poll_pad(g_record, ticks > 6 ? 6 : ticks ? ticks : 1);
	const u32 *mask = (const u32 *)gs_field(GS_ACTION_MASK);
	g_record[0] &= mask[0];
	g_record[1] &= mask[1];
	g_has_record = 1;
	return g_record;
}

void players_net_frame(const u32 (*records)[6][12], u32 n)
{
	g_tick_index = 0;
	g_has_record = 0;
	g_net_records = records;
	g_net_count = records ? n : 0;
	if (g_enabled && ONgGameState) new_level_check();
}

void players_set_count(u32 n)
{
	g_count = n < 1 ? 1 : n > PL ? PL : n;
}

u32 players_count(void) { return g_count; }

int players_spawn_points(void) { return g_nspawn; }

void players_replay_frame(const u32 *rec)
{
	g_tick_index = 0;
	g_has_record = 0;
	g_net_records = 0;
	if (!g_enabled || !ONgGameState) return;
	new_level_check();
	if (rec) {
		memcpy(g_record, rec, 0x30);
		g_has_record = 1;
	}
}

/* ---- Emotes (chat.c: /taunt, /taunt2, /sneeze, /idle) ------------------------------------- *
 * A player's chat command asks for one animation (PLAYERS_EMOTE_*). The request rides in bits
 * 12-15 of word 11 of that player's record for one tick (players_emote_take: net.c make_input and
 * the lobby's frames), so every PC plays it in the same tick, and rollback's guesses clear those
 * bits (net.c rb_fill_frames) so a guessed tick never repeats it. In the tick (note_choice), if
 * the character is alive, takes input and stands in the animation's from-state (standing), it
 * plays as a script's chr_animate does (0x4b6e10, and the sound code's player animation):
 * ONrForceActiveCharacter 0x4f11b0 (ECX the character), ONrCharacter_SetAnimation_External
 * 0x4eb340 (ECX the character, DX the active character's curFromState +0x1afe, [animation],
 * [interpolation -1]; ret 8), the character's ChrAnimate flag 0x200000, animationLockFrames
 * (+0x1c90) 0 so any key ends it. A taunt first enters fight mode (0x4eb0b0): it's the Combat
 * variant, and out of fight mode it didn't show (user report). The animation is looked up in the character's animation
 * collection chain (child first), by type (TRAM +0x15a: 93 Idle, 94 Taunt, checked in level0's
 * TRAMKONOKOidle_spec3) and name: taunt N ends in "tauntN" (Konoko, Ninja and Tanker have a
 * taunt2: the ninja's moonwalk, weight 5 against 100, so rare from the taunt key), the sneeze is
 * Konoko's special idle idle_spec3 (head jerks 45 degrees in 6 frames at frame 56; idle_spec2
 * is slow), idle N the Nth special idle but the sneeze (Comguy idle15, Striker idle6, Tanker idle16,
 * Konoko idle_spec2). */

#define ONrForceActiveCharacter ((u8 *(ONI_FC *)(u8 *))0x4f11b0)
#define ONrCharacter_EnterFightMode ((void (ONI_FC *)(u8 *))0x4eb0b0)   /* ONrCharacter_FightMode (as regen.c) */
#define ONrCharacter_SetAnimation_External ((void (ONI_FC *)(u8 *, u32, void *, int))0x4eb340)
#define ACT_FROM_STATE(a)       (*(u16 *)((a) + 0x1afe))
#define ACT_LOCK_FRAMES(a)      (*(u32 *)((a) + 0x1c90))
#define CHR_CHRANIMATE          0x200000
#define ONCC_ANIMATIONS(c)      (*(u8 **)((c) + 0xc80))    /* TRAC */
#define TRAC_PARENT(t)          (*(u8 **)((t) + 0x10))
#define TRAC_COUNT(t)           (*(u16 *)((t) + 0x16))
#define TRAC_ANIMATION(t, i)    (*(u8 **)((t) + 0x20 + 12 * (i)))
#define TRAM_TYPE(a)            (*(u16 *)((a) + 0x15a))
#define TRAM_FROM(a)            (*(u16 *)((a) + 0x15e))
#define ANIM_IDLE               93
#define ANIM_TAUNT              94
#define ANIM_STAND              6
#define TRAM_HEIGHTS(a)         (*(float **)((a) + 4))      /* per frame: the pelvis over the feet */
#define TRAM_VARIENT(a)         (*(u16 *)((a) + 0x168))    /* 0: the plain stance (no Combat, no weapon) */

/* The character's own plain stance (the first plain Stand from Standing in its chain) and whether
 * an idle stands at its height. Idles are made for one body: the base collection strikercore
 * holds Striker's idle6 (pelvis 11.17), which Ninja (10.44), Comguy's family (10.37) and Tanker
 * (10.26) inherit, and played on them the character hangs in the air until it ends (user report).
 * The game only plays an idle its own class has; here: within 0.05 of the stance's height. */
static int idle_fits(u8 *c, u8 *idle)
{
	float hi = TRAM_HEIGHTS(idle) ? TRAM_HEIGHTS(idle)[0] : 0, d;
	for (u8 *trac = ONCC_ANIMATIONS(CHR_CLASS(c)); trac; trac = TRAC_PARENT(trac))
		for (u32 i = 0; i < TRAC_COUNT(trac); i++) {
			u8 *a = TRAC_ANIMATION(trac, i);
			if (!a || TRAM_TYPE(a) != ANIM_STAND || TRAM_FROM(a) != TRAM_FROM(idle) || TRAM_VARIENT(a) || !TRAM_HEIGHTS(a)) continue;
			d = TRAM_HEIGHTS(a)[0] - hi;
			return d < 0.05f && d > -0.05f;
		}
	return 0;
}

static u32 g_emote_req;   /* this PC's request, until a record carries it (not game state) */

static int ends_i(const char *s, const char *end)
{
	int n = str_len(s), k = str_len(end);
	return n >= k && str_ieq_n(s + n - k, end, k);
}

void *players_emote_anim(u8 *c, int emote)
{
	u8 *seen[16];
	int nseen = 0, want = emote >= PLAYERS_EMOTE_IDLE ? emote - PLAYERS_EMOTE_IDLE + 1 : 0;
	char end[16];

	if (!c || !CHR_CLASS(c)) return 0;
	if (emote == PLAYERS_EMOTE_TAUNT || emote == PLAYERS_EMOTE_TAUNT2) fmt(end, "taunt%d", emote == PLAYERS_EMOTE_TAUNT ? 1 : 2);
	for (u8 *trac = ONCC_ANIMATIONS(CHR_CLASS(c)); trac; trac = TRAC_PARENT(trac))
		for (u32 i = 0; i < TRAC_COUNT(trac); i++) {
			u8 *a = TRAC_ANIMATION(trac, i);
			const char *n = a ? TMrInstance_GetInstanceName(a) : 0;
			int dup = 0;
			if (!n) continue;
			switch (emote) {
			case PLAYERS_EMOTE_TAUNT:
			case PLAYERS_EMOTE_TAUNT2:
				if (TRAM_TYPE(a) == ANIM_TAUNT && ends_i(n, end)) return a;
				break;
			case PLAYERS_EMOTE_SNEEZE:
				if (TRAM_TYPE(a) == ANIM_IDLE && ends_i(n, "idle_spec3") && idle_fits(c, a)) return a;
				break;
			default:
				if (TRAM_TYPE(a) != ANIM_IDLE || !want || ends_i(n, "idle_spec3") || !idle_fits(c, a)) break;   /* the sneeze has its own */
				for (int k = 0; k < nseen; k++) dup |= seen[k] == a;
				if (dup || nseen == 16) break;
				seen[nseen++] = a;
				if (nseen == want) return a;
			}
		}
	if (emote == PLAYERS_EMOTE_TAUNT)   /* no taunt1: the first taunt */
		for (u8 *trac = ONCC_ANIMATIONS(CHR_CLASS(c)); trac; trac = TRAC_PARENT(trac))
			for (u32 i = 0; i < TRAC_COUNT(trac); i++) {
				u8 *a = TRAC_ANIMATION(trac, i);
				if (a && TRAM_TYPE(a) == ANIM_TAUNT) return a;
			}
	return 0;
}

void players_emote_request(int emote) { g_emote_req = (u32)emote & 0xf; }

u32 players_emote_take(void)
{
	u32 w = g_emote_req << PLAYERS_EMOTE_SHIFT;
	g_emote_req = 0;
	return w;
}

static u8 *player_char(int who);

/* In the tick: the emote this record asks for */
static void emote_tick(int who, u32 word)
{
	int e = (int)((word & PLAYERS_EMOTE_MASK) >> PLAYERS_EMOTE_SHIFT);
	u8 *c, *act, *anim;

	if (!e || !ONgGameState) return;
	c = player_char(who);
	if (!c || (CHR_FLAGS(c) & (CHR_DEAD | CHR_NOINPUT)) || !(anim = players_emote_anim(c, e))) return;
	if (!(act = ONrForceActiveCharacter(c))) return;
	if (ACT_FROM_STATE(act) != TRAM_FROM(anim)) {
		log_msg("players: player %d's %s waits for standing still: not played", who + 1, TMrInstance_GetInstanceName(anim));
		return;
	}
	/* taunts are the Combat variant: out of fight mode the next update swaps the animation for the
	 * plain stance's (no taunt there), so it never shows; the taunt key's taunt is in fight mode too */
	if (TRAM_TYPE(anim) == ANIM_TAUNT) ONrCharacter_EnterFightMode(c);
	ONrCharacter_SetAnimation_External(c, ACT_FROM_STATE(act), anim, -1);
	CHR_FLAGS(c) |= CHR_CHRANIMATE;
	ACT_LOCK_FRAMES(act) = 0;
	log_msg("players: player %d plays %s", who + 1, TMrInstance_GetInstanceName(anim));
}

static void spectate_tick(int who, u32 word);

/* The Daodan mutator (mp_rules.c): when a player's super amount (+0x1668) passes Muro's shield
 * threshold (0.5) either way. Logging only, at most once a tick per player. */
static void daodan_log(int who)
{
	static u8 on[PL];
	static u32 at[PL];
	u8 *c = player_char(who);
	u32 tick = ONgGameState ? GS_TICK(ONgGameState) : 0;
	if (!c || !net_mutator_daodan() || at[who] == tick) return;
	at[who] = tick;
	float super = *(float *)(c + 0x1668);
	if ((super > 0.5f) != on[who]) {
		on[who] = super > 0.5f;
		log_msg("players: player %d's Daodan shield %s (health %d of %d, tick %u)", who + 1, on[who] ? "on" : "off",
		        *(int *)(c + 0x38), *(int *)(c + 0x3c), tick);
	}
}

static void note_choice(int who, const u32 *rec)
{
	if ((rec[11] & PLAYERS_CHOICE_MASK) != PLAYERS_CHOICE_TAG) return;
	if (who == 0) spectate_tick(0, rec[11]);   /* the others' in set_input: they may have no character */
	g_class[who] = (int)(rec[11] & PLAYERS_CLASS_MASK) - 1;
	/* a character the host's rules leave out (net_char_rules, the same on every PC): the map's */
	if (!char_menu_class_allowed(g_class[who])) {
		static int said[PL] = { -1, -1, -1, -1 };   /* (logging only: once per pick, not every tick) */
		if (said[who] != g_class[who]) {
			u8 *p = 0;
			said[who] = g_class[who];
			TMrInstance_GetDataPtr_ByNumber(TAG_ONCC, (u32)g_class[who], (void **)&p);
			log_msg("players: player %d picked %s, which the host's mutators leave out: the map's character instead",
			        who + 1, p ? TMrInstance_GetInstanceName(p) : "?");
		}
		g_class[who] = -1;
	}
	g_noted[who] = 1;
	daodan_log(who);
	emote_tick(who, rec[11]);
}

static void present_tick(u32 present);

void players_tick_record(const u32 *rec)
{
	if (!g_enabled || !rec) return;
	note_choice(0, rec);
	if ((rec[11] & PLAYERS_CHOICE_MASK) == PLAYERS_CHOICE_TAG && (rec[11] & PLAYERS_PRESENT_FLAG) && ONgGameState)
		present_tick(((rec[11] >> PLAYERS_PRESENT_SHIFT) & 0xf) << 1);   /* players 1..4 (0 is always there) */
}

/* Slot s's tick input, as the tick builds Konoko's (tick: this tick's index in the frame) */
static void player_tick(int s, int tick)
{
	u8 *gs = ONgGameState, *p2, *p1;
	u32 rec[12], *src = 0, *in = g_input[s];
	u32 lo, hi;
	u64 b;

	if (!slot_valid(s)) return;
	p2 = character(g_pl[s]);
	if (g_net_records) {
		if ((u32)tick < g_net_count) {
			memcpy(rec, g_net_records[s][tick], sizeof rec);
			src = rec;
		}
	} else if (s == 1 && g_has_record && tick == 0) {
		memcpy(rec, g_record, sizeof rec);
		src = rec;
	}
	if (src) note_choice(s, rec);
	if (src && (!*gs_field(GS_INPUT_ON) || *gs_field(GS_INPUT_OFF) || (CHR_FLAGS(p2) & CHR_NOINPUT))) {
		rec[7] = rec[8] = rec[9] = rec[10] = 0;
		rec[0] &= 0x10003;
		rec[1] = 0;
	}

	/* aim */
	u32 lr = 0, ud = 0;
	if (src && !CHR_HELD(p2)) {
		lr = rec[9];
		ud = rec[10];
	}
	if (ONgInvertLook) ud ^= 0x80000000u;
	in[0] = ud;
	in[1] = lr;

	/* buttons: 0x4fa520 works on the character in gs+0xac */
	lo = src ? rec[0] : in[4];
	hi = src ? rec[1] : in[5];
	p1 = GS_PLAYER(gs);
	GS_PLAYER(gs) = p2;
	b = ONiPlayerButtons(lo, hi);
	GS_PLAYER(gs) = p1;
	lo = (u32)b;
	hi = (u32)(b >> 32);

	u32 prev_lo = in[4], prev_hi = in[5], not_lo = in[6], not_hi = in[7];
	in[4] = lo;  in[5] = hi;               /* held */
	in[6] = ~lo; in[7] = ~hi;              /* not held */
	in[8] = not_lo & lo;  in[9] = not_hi & hi;      /* pressed */
	in[10] = prev_lo & ~lo; in[11] = prev_hi & ~hi; /* released */
	if (GS_TICK(gs) < 0xe) {               /* the level's first ticks, as for Konoko */
		in[5] &= 0xfffffe3f;
		in[8] = in[9] = in[10] = in[11] = 0;
	}
	u32 regen_copy[12];
	ONrCharacter_SetInput(p2, regen_input(p2, in, regen_copy));   /* g_input[s] itself stays as built */
}

static void respawn_tick(const u32 *p1_input);
static void score_tick(void);

static void ONI_FC set_input(u8 *chr, u32 *input)
{
	u32 regen_copy[12];
	ONrCharacter_SetInput(chr, regen_input(chr, input, regen_copy));   /* Konoko; gs+0xb8 stays as built */
	if (g_net_records && (u32)g_tick_index < g_net_count)
		for (int s = 1; s < PL; s++) {
			u32 w = g_net_records[s][g_tick_index][11];
			if ((w & PLAYERS_CHOICE_MASK) == PLAYERS_CHOICE_TAG) spectate_tick(s, w);
		}
	for (int s = 1; s < PL; s++) player_tick(s, g_tick_index);
	g_tick_index++;
	if (multiplayer_active()) {
		if (multiplayer_map()) score_tick();
		respawn_tick(input);
		items_tick();
	}
}

/* ---- Spawning ------------------------------------------------------------------------------ */

static u32 hash(u32 x)
{
	x ^= x >> 16; x *= 0x7feb352d;
	x ^= x >> 15; x *= 0x846ca68b;
	x ^= x >> 16;
	return x;
}

static float dist2(const float *a, const float *b)
{
	float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
	return dx * dx + dy * dy + dz * dz;
}

/* A spawn point for player `who`: random among those with nothing in avoid[] near, else the
 * farthest from all of them */
static int choose_spawn(const float *const *avoid, int navoid, u32 who)
{
	int free[MAX_SPAWN], nfree = 0, far = 0;
	float far_d = -1;

	for (int i = 0; i < g_nspawn; i++) {
		const float *p = (const float *)OBJ_POS(g_spawn[i]);
		float d = 1e30f;
		for (int k = 0; k < navoid; k++) {
			float e = dist2(p, avoid[k]);
			if (e < d) d = e;
		}
		if (d >= SPAWN_CLEAR * SPAWN_CLEAR) free[nfree++] = i;
		if (d > far_d) { far_d = d; far = i; }
	}
	if (!nfree) return far;
	u32 r = hash(g_spawn_level * 0x9e3779b9u ^ g_spawn_seed ^ hash(GS_TICK(ONgGameState) * 0x85ebca6bu + who));
	return free[r % nfree];
}

/* The seed of a level load's spawn picks: the one net.c set for a synced load (the host's, the
 * same on every PC), a replay's own (its header), else the clock's, so every game starts
 * somewhere new. The tick and the player still make each pick of the load differ. */
static u32 choose_spawn_seed(void)
{
	u64 now = 0;
	u32 seed;

	g_spawn_seed_due = 0;
	if (g_next_spawn_seed_set) {
		g_next_spawn_seed_set = 0;
		return g_next_spawn_seed;
	}
	if (lockstep_replay_spawn_seed(&seed)) return seed;
	QueryPerformanceCounter(&now);
	return hash((u32)now ^ hash((u32)(now >> 32) + GetTickCount())) | 1;
}

void players_set_spawn_seed(u32 seed)
{
	g_next_spawn_seed = seed;
	g_next_spawn_seed_set = 1;
}

u32 players_spawn_seed(void) { return g_spawn_seed; }

/* Only a level load picks a new seed: the game's walk at 0x4f6200 spawns a player in a tick
 * when there's none, and a seed from the clock there would differ on every PC */
void players_level_begin(void) { g_spawn_seed_due = 1; }

static u8 ONI_FC collect_spawn(u8 *obj, u32 unused)
{
	if ((OBJ_FLAGS(obj) & OBJ_ISPLAYER) && g_nspawn < MAX_SPAWN) g_spawn[g_nspawn++] = obj;
	return 1;
}

int multiplayer_map(void)
{
	return g_nspawn >= 2;   /* collected by every level's player spawn walk */
}

/* Maps often give every spawn placement the same Name, and scripts find characters by name (a
 * trigger volume's script gets the name): chr_set_health on "player" would hit whichever comes
 * first in the list, not the one who walked in. A clashing extra player gets a name of its own. */
static void name_apart(u32 index)
{
	char *name = (char *)character(index) + CHR_NAME_OFFSET;
	char tail[16];
	int clash = 0;

	for (u32 i = 0; i < ONgCharacterSlots && !clash; i++) {
		u8 *o = character(i);
		const char *other = (const char *)o + CHR_NAME_OFFSET;
		int k = 0;
		if (i == index || !(CHR_FLAGS(o) & CHR_INUSE)) continue;
		while (k < CHR_NAME_MAX && name[k] == other[k] && name[k]) k++;
		clash = k == CHR_NAME_MAX || name[k] == other[k];
	}
	if (!clash) return;
	fmt(tail, "_%u", index);
	name[CHR_NAME_MAX - 1 - str_len(tail)] = 0;
	str_cpy(name + str_len(name), (unsigned)(CHR_NAME_MAX - str_len(name)), tail);
}

/* Spawns the character of placement obj at spawn point `point`; keeps gs+0xac (player 1) when
 * asked, since NewCharacter makes any charType 0 character the player (0x4fc8f0 at 0x4db785) */
static int spawn_at(u8 *obj, int point, int keep_player, u32 *index)
{
	u8 *gs = ONgGameState, *at = g_spawn[point];
	u32 pos[3], facing = OBJ_FACING(obj);
	u32 player = *(u32 *)(gs + 0xac), player_b0 = *(u32 *)(gs + 0xb0);
	u16 err;

	memcpy(pos, OBJ_POS(obj), sizeof pos);
	memcpy(OBJ_POS(obj), OBJ_POS(at), sizeof pos);
	OBJ_FACING(obj) = OBJ_FACING(at);
	*index = 0;
	err = ONrGameState_NewCharacter(obj, 0, 0, index);
	memcpy(OBJ_POS(obj), pos, sizeof pos);
	OBJ_FACING(obj) = facing;
	if (keep_player) {
		*(u32 *)(gs + 0xac) = player;
		*(u32 *)(gs + 0xb0) = player_b0;
	}
	*index &= 0xffff;
	if (err) log_msg("players: can't spawn %s (error %d)", OBJ_NAME(obj), err);
	else if (keep_player) name_apart(*index);
	return !err;
}

/* The spawn walk: the points taken so far (Konoko's first) */
static const float *g_taken[PL];
static int g_ntaken;

static u8 ONI_FC spawn_player(u8 *obj, u32 *seen)
{
	u32 f = OBJ_FLAGS(obj), index;
	int s;

	if (!(f & OBJ_ISPLAYER)) return 1;
	/* the first is Konoko's, which the game just spawned; those past the count are spawn points */
	s = (int)(*seen)++;
	if (s == 0 || s >= (int)g_count || s >= PL || (f & OBJ_SPAWNED)) return 1;
	OBJ_FLAGS(obj) = f | OBJ_SPAWNED;
	if (multiplayer_map()) {
		int point = choose_spawn(g_taken, g_ntaken, s + 1);
		if (spawn_at(obj, point, 1, &index)) {
			g_pl[s] = (int)index;
			memset(g_input[s], 0, sizeof g_input[s]);
			if (g_ntaken < PL) g_taken[g_ntaken++] = (const float *)OBJ_POS(g_spawn[point]);
			CHR_HEALTH(character(index)) = CHR_MAX_HEALTH(character(index)) = MP_HEALTH;
			g_base_health[s] = MP_HEALTH;
			g_map_class[s] = CHR_CLASS(character(index));
			g_as[s] = -1;
			log_msg("players: spawned %s (player %d) at spawn point %d", OBJ_NAME(obj), s + 1, point);
		}
	} else {
		u8 *gs = ONgGameState;
		u32 player = *(u32 *)(gs + 0xac), player_b0 = *(u32 *)(gs + 0xb0);
		u16 err = ONrGameState_NewCharacter(obj, 0, 0, &index);
		*(u32 *)(gs + 0xac) = player;
		*(u32 *)(gs + 0xb0) = player_b0;
		if (err) log_msg("players: can't spawn %s (error %d)", OBJ_NAME(obj), err);
		else {
			g_pl[s] = (int)(index & 0xffff);
			memset(g_input[s], 0, sizeof g_input[s]);
			log_msg("players: spawned %s (player %d)", OBJ_NAME(obj), s + 1);
		}
	}
	return 1;   /* keep walking */
}

/* Replaces the walks that spawn the player (all three walk CHAR with 0x48a3a0, which spawns the
 * first Player-flagged placement) */
static u32 ONI_FC spawn_players(u32 type, void *callback, u32 param)
{
	u32 r, seen = 0;

	g_nspawn = 0;
	memset(g_killcam, 0, sizeof g_killcam);
	g_spect = 0;   /* everyone is spawned; watchers' records park them again in the first tick */
	g_spawn_level = ONrLevel_GetCurrent();
	if (g_spawn_seed_due || g_next_spawn_seed_set) g_spawn_seed = choose_spawn_seed();
	OBJrEnumerate(OBJcType_CHAR, collect_spawn, 0);
	mp_rules_level();   /* multiplayer_map() is known now, and no character is made yet */
	g_ntaken = 0;
	for (int s = 1; s < PL; s++) g_pl[s] = -1;
	if (multiplayer_map()) {
		/* Konoko: her placement moved to the spawn point while the game spawns her */
		u8 *obj = g_spawn[0];
		u32 pos[3], facing = OBJ_FACING(obj);
		int point = choose_spawn(0, 0, 1);
		memcpy(pos, OBJ_POS(obj), sizeof pos);
		memcpy(OBJ_POS(obj), OBJ_POS(g_spawn[point]), sizeof pos);
		OBJ_FACING(obj) = OBJ_FACING(g_spawn[point]);
		r = OBJrEnumerate(type, callback, param);
		memcpy(OBJ_POS(obj), pos, sizeof pos);
		OBJ_FACING(obj) = facing;
		g_taken[g_ntaken++] = (const float *)OBJ_POS(g_spawn[point]);
		log_msg("players: multiplayer map, %d spawn points, %d players; Konoko at spawn point %d", g_nspawn,
		        g_count < (u32)g_nspawn ? g_count : (u32)g_nspawn, point);
	} else {
		r = OBJrEnumerate(type, callback, param);
	}
	for (int s = 0; s < PL; s++) {
		g_base_health[s] = 0;
		g_map_class[s] = 0;
		g_as[s] = -1;
		g_noted[s] = g_settled[s] = 0;
	}
	if (GS_PLAYER(ONgGameState)) {
		if (multiplayer_map())
			CHR_HEALTH(GS_PLAYER(ONgGameState)) = CHR_MAX_HEALTH(GS_PLAYER(ONgGameState)) = MP_HEALTH;
		g_base_health[0] = CHR_MAX_HEALTH(GS_PLAYER(ONgGameState));
		g_map_class[0] = CHR_CLASS(GS_PLAYER(ONgGameState));
	}
	OBJrEnumerate(OBJcType_CHAR, spawn_player, (u32)&seen);
	g_present = 1;
	for (int s = 1; s < PL; s++)
		if (g_pl[s] >= 0) g_present |= 1u << s;
	g_present_known = 0;
	return r;
}

/* ---- Respawning ---------------------------------------------------------------------------- */

/* Dead for respawn_delay and an attack button was pressed this tick (input: +0x20 pressed) */
static int wants_respawn(u8 *c, const u32 *input)
{
	return (CHR_FLAGS(c) & CHR_INUSE) && (CHR_FLAGS(c) & CHR_DEAD) &&
	       GS_TICK(ONgGameState) - CHR_DEATH_TICK(c) >= g_respawn_ticks &&
	       ((input[8] & ATTACK_LO) || (input[9] & ATTACK_HI));
}

/* Makes player who's character again from its placement at spawn point `point`, as the class
 * chosen with F2. Returns the character's index, or -1. The old character must be gone already. */
static int placement_of(int who);
static int make_player(int who, int point)
{
	u8 *obj = g_spawn[placement_of(who)], *oncc = 0, *c;
	const char *cls = 0;
	char saved[64];
	u32 flags = OBJ_FLAGS(obj), index;
	int ok;

	if (g_class[who] >= 0 && !TMrInstance_GetDataPtr_ByNumber(TAG_ONCC, (u32)g_class[who], (void **)&oncc) && oncc &&
	    (cls = TMrInstance_GetInstanceName(oncc)) && str_len(cls) < sizeof saved) {
		memcpy(saved, OBJ_CLASS(obj), sizeof saved);
		str_cpy(OBJ_CLASS(obj), sizeof saved, cls);
		OBJ_FLAGS(obj) = flags & ~OBJ_RANDOM_CLASS;
	} else {
		cls = 0;
	}
	/* Konoko comes back as the player (NewCharacter sets gs+0xac); the others don't */
	ok = spawn_at(obj, point, who != 0, &index);
	if (cls) {
		memcpy(OBJ_CLASS(obj), saved, sizeof saved);
		OBJ_FLAGS(obj) = (OBJ_FLAGS(obj) & ~OBJ_RANDOM_CLASS) | (flags & OBJ_RANDOM_CLASS);
	}
	if (!ok) return -1;
	c = character(index);
	if (multiplayer_map()) g_base_health[who] = MP_HEALTH;
	if (!cls) {
		if (!multiplayer_map()) g_base_health[who] = CHR_MAX_HEALTH(c);
		else CHR_HEALTH(c) = CHR_MAX_HEALTH(c) = MP_HEALTH;
		g_map_class[who] = CHR_CLASS(c);
	} else if (g_base_health[who] > 0) CHR_HEALTH(c) = CHR_MAX_HEALTH(c) = g_base_health[who];
	g_as[who] = cls ? g_class[who] : -1;
	log_msg("players: player %d respawned at spawn point %d as %s, health %d/%d (tick %u)", who + 1, point, cls ? cls : "the map's class", CHR_HEALTH(c), CHR_MAX_HEALTH(c), GS_TICK(ONgGameState));
	return (int)index;
}

static u8 *player_char(int who);

/* Player who's character c comes back at a spawn point no other living player is near */
static int respawn(u8 *c, int who)
{
	const float *avoid[PL];
	int navoid = 0;

	for (int s = 0; s < PL; s++) {
		u8 *o = player_char(s);
		if (s != who && o && (CHR_FLAGS(o) & CHR_INUSE) && !(CHR_FLAGS(o) & CHR_DEAD)) avoid[navoid++] = CHR_POS(o);
	}
	int point = choose_spawn(avoid, navoid, (u32)who + 1);
	if (CHR_ACTIVE(c) && !(CHR_FLAGS(c) & CHR_NOCORPSE)) ONrCharacter_MakeCorpse(c);
	ONrGameState_DeleteCharacter(c);
	g_killcam[who] = 0;
	int index = make_player(who, point);
	if (index >= 0) items_player_respawned(CHR_POS(character(index)));
	return index;
}

static int alive(u8 *c)
{
	return c && (CHR_FLAGS(c) & CHR_INUSE) && !(CHR_FLAGS(c) & CHR_DEAD);
}

/* In a match a living player's new choice waits for the respawn (make_player), apart from their
 * first (see g_settled) */
static int waits_for_respawn(int who) { return g_limits_on && g_settled[who]; }

/* A new choice while alive: the class changes in place, as the shapeshifter cheat does it */
static void change_class(int who, u8 *c)
{
	u8 *oncc = 0;

	if (!alive(c) || waits_for_respawn(who)) return;
	if (g_limits_on) {   /* a match: this is the player's first choice, if the tick has seen one */
		if (!g_noted[who]) return;
		g_settled[who] = 1;
	}
	if (g_class[who] == g_as[who]) return;
	if (g_class[who] < 0) oncc = g_map_class[who];
	else if (TMrInstance_GetDataPtr_ByNumber(TAG_ONCC, (u32)g_class[who], (void **)&oncc)) oncc = 0;
	g_as[who] = g_class[who];
	if (!oncc || oncc == CHR_CLASS(c)) return;
	ONrCharacter_SetClass(c, oncc);
	log_msg("players: player %d changed to %s, health %d/%d (tick %u)", who + 1, TMrInstance_GetInstanceName(oncc), CHR_HEALTH(c), CHR_MAX_HEALTH(c), GS_TICK(ONgGameState));
}

/* ---- Joining and leaving ------------------------------------------------------------------- *
 * In a network game the host's record (Konoko's, the tick's action) carries in word 11 the
 * players who have a character (PLAYERS_PRESENT_FLAG, a bit per player). Every PC reads it at
 * the tick's start (players_tick_record, from lockstep.c's tick body), so a player comes in or
 * goes at the same tick everywhere, and a rollback that takes the tick back takes that back too.
 * A record without the flag (a guess made before any of the host's came, a lobby, a single-player
 * game) changes nothing. A player whose bit comes on gets a new character, made as a respawn
 * makes one (its placement's class, at a spawn point no living player is near) with the score,
 * class choice and hits of whoever had the slot before wiped; one whose bit goes off loses its
 * character (a dead one leaves its corpse). Konoko (player 1, the host) never changes. */

/* On a dedicated server (net.c tells every PC, before any level of its sessions loads) Konoko
 * only watches, so one more player than the map has placements fits: that one is made from
 * Konoko's placement (placement_of), its character named apart like any extra player's */
static int g_dedicated;
void players_set_dedicated(int on)
{
	if (g_dedicated != (on != 0)) log_msg("players: %s", on ? "dedicated server: the last player is made from Konoko's placement" : "no dedicated server");
	g_dedicated = on != 0;
}
static int placement_of(int who) { return who < g_nspawn ? who : 0; }

int players_can_join(int who) { return who > 0 && who < PL && multiplayer_map() && who < g_nspawn + g_dedicated; }

static void join_player(int who)
{
	const float *avoid[PL];
	int navoid = 0, point, index;

	g_score[who] = 0;
	g_class[who] = g_as[who] = -1;
	g_noted[who] = g_settled[who] = 0;
	g_hit_by[who] = -1;
	g_hit_tick[who] = g_last_owner[who] = g_death_tick[who] = 0;
	g_last_cause[who] = OBIT_SKULL;
	g_held[who] = 0;
	g_killcam[who] = 0;
	g_was_dead[who] = 0;
	memset(g_input[who], 0, sizeof g_input[who]);
	if (!players_can_join(who)) {
		log_msg("players: player %d can't come in: the map has no spawn placement for them (tick %u)", who + 1,
		        GS_TICK(ONgGameState));
		return;
	}
	for (int s = 0; s < PL; s++) {
		u8 *o = player_char(s);
		if (s != who && alive(o)) avoid[navoid++] = CHR_POS(o);
	}
	point = choose_spawn(avoid, navoid, (u32)who + 1);
	index = make_player(who, point);
	g_pl[who] = index;
	if (index < 0) return;
	items_player_respawned(CHR_POS(character(index)));
	log_msg("players: player %d came into the game at spawn point %d (tick %u)", who + 1, point, GS_TICK(ONgGameState));
}

static void leave_player(int who)
{
	u8 *c = slot_valid(who) ? character(g_pl[who]) : 0;

	if (c) {
		if (CHR_ACTIVE(c) && (CHR_FLAGS(c) & CHR_DEAD) && !(CHR_FLAGS(c) & CHR_NOCORPSE)) ONrCharacter_MakeCorpse(c);
		ONrGameState_DeleteCharacter(c);
		log_msg("players: player %d left the game (tick %u)", who + 1, GS_TICK(ONgGameState));
	}
	g_pl[who] = -1;
	g_killcam[who] = 0;
	memset(g_input[who], 0, sizeof g_input[who]);
}

static void present_tick(u32 present)
{
	present |= 1;
	if (g_present_known && present == g_present) return;
	for (int s = 1; s < PL; s++) {
		int on = (present >> s & 1) != 0, has = slot_valid(s);
		if (!on) g_spect &= ~(1u << s);   /* gone: the place's next player plays */
		if (on && !has && !(g_spect >> s & 1)) join_player(s);
		else if (!on && (has || g_pl[s] >= 0)) leave_player(s);
	}
	g_present = present;
	g_present_known = 1;
}

/* ---- Spectating (/spectate, /join) ------------------------------------------------------------ *
 * A player who watches has no part in the game: no character to hit, no score, not in the match's
 * result. Each PC's wish (chat.c sets it, players_spectate_want) is bit 11 of word 11 of every
 * record its player sends (PLAYERS_SPECTATE_FLAG; net.c make_input), a state rather than a
 * request, so a guessed record keeps it, a replay join rebuilds it and the first records after a
 * reload into the match park the player again. Every PC applies a change in the tick whose record
 * shows it (Konoko's at the tick's start in note_choice, the others' in set_input before their
 * input), so rollback takes it back like anything else. Either way the player's score goes to 0.
 *
 *   players 2..4: their character goes as a leaver's does (a dead one leaves its corpse); /join
 *     brings them in as a newcomer (join_player: a spawn point no living player is near).
 *   Konoko (the host): gs+0xac can't be emptied (the tick, HUD, camera and sounds all read it), so
 *     she is parked: dies in place (ONrCharacter_Die 0x4eb550, ECX the character, EDX 0, as
 *     chr_set_health 0 does at 0x4da601) with her class's death particle (ONCC +0xc2c) and her
 *     death/defeat scripts (+0x88, +0xe8) held off for the call, isn't drawn on any PC
 *     (players_hidden, net.c's draw-pass hook), and doesn't respawn while watching. /join respawns
 *     her as a death does, without leaving a corpse (NoCorpse).
 *
 * What a watcher sees is each PC's own business (net.c: the camera, HUD and listener follow the
 * player watched, left click picks the next). */

#define ONrCharacter_Die        ((void (ONI_FC *)(u8 *, u8 *))0x4eb550)
#define ONCC_DEATH_PARTICLE(o)  (*(void **)((o) + 0xc2c))
#define CHR_DIE_SCRIPT(c)       ((char *)(c) + 0x88)
#define CHR_DEFEAT_SCRIPT(c)    ((char *)(c) + 0xe8)

static int g_spect_want;   /* this PC's player asks to watch (not game state: it goes out in the records) */

void players_spectate_want(int on) { g_spect_want = on != 0; }
int  players_spectate_wanted(void) { return g_spect_want; }
u32  players_spectate_word(void) { return g_spect_want ? PLAYERS_SPECTATE_FLAG : 0; }
int  players_spectating(int who) { return who >= 0 && who < PL && (g_spect >> who & 1); }
u32  players_spectators(void) { return g_spect; }

int players_hidden(u8 *c) { return ((g_spect & 1) && c && c == player_char(0)) || mutators_body_gone(c); }

/* No Weapons (mutators.c): the spawn placements' weapons (CHAR OSD weapon_class_name, object
 * +0x90) blanked for the level, so no player spawns with one, and put back at its end */
#define OBJ_WEAPON(o)           ((char *)(o) + 0x90)
static struct { u8 *obj; char name[64]; } g_weapon_saved[MAX_SPAWN];
static int g_nweapon_saved;

void players_spawn_weapons(int blank)
{
	if (!blank) {
		for (int i = 0; i < g_nweapon_saved; i++) memcpy(OBJ_WEAPON(g_weapon_saved[i].obj), g_weapon_saved[i].name, 64);
		g_nweapon_saved = 0;
		return;
	}
	for (int i = 0; i < g_nspawn && g_nweapon_saved < MAX_SPAWN; i++) {
		g_weapon_saved[g_nweapon_saved].obj = g_spawn[i];
		memcpy(g_weapon_saved[g_nweapon_saved].name, OBJ_WEAPON(g_spawn[i]), 64);
		g_nweapon_saved++;
		OBJ_WEAPON(g_spawn[i])[0] = 0;
	}
}

static void forget_player(int who)
{
	g_score[who] = 0;
	g_hit_by[who] = -1;
	g_hit_tick[who] = g_last_owner[who] = 0;
	g_last_cause[who] = OBIT_SKULL;
	g_killcam[who] = 0;
}

static void park_konoko(void)
{
	u8 *c = player_char(0), *oncc;
	void *particle;
	char die, defeat;

	if (!c || !(CHR_FLAGS(c) & CHR_INUSE)) return;
	g_was_dead[0] = 1;   /* no death to count */
	if (CHR_FLAGS(c) & CHR_DEAD) return;
	oncc = CHR_CLASS(c);
	particle = oncc ? ONCC_DEATH_PARTICLE(oncc) : 0;
	die = *CHR_DIE_SCRIPT(c);
	defeat = *CHR_DEFEAT_SCRIPT(c);
	if (oncc) ONCC_DEATH_PARTICLE(oncc) = 0;
	*CHR_DIE_SCRIPT(c) = *CHR_DEFEAT_SCRIPT(c) = 0;
	CHR_HEALTH(c) = 0;
	ONrCharacter_Die(c, 0);
	if (oncc) ONCC_DEATH_PARTICLE(oncc) = particle;
	*CHR_DIE_SCRIPT(c) = die;
	*CHR_DEFEAT_SCRIPT(c) = defeat;
}

static void spectate_tick(int who, u32 word)
{
	int want = (word & PLAYERS_SPECTATE_FLAG) != 0, is = (g_spect >> who & 1) != 0;
	u8 *c;

	if (want == is || !ONgGameState || !multiplayer_map()) return;
	if (who > 0 && g_present_known && !(g_present >> who & 1)) return;   /* not in the game (left) */
	forget_player(who);
	if (want) {
		g_spect |= 1u << who;
		if (who == 0) {
			park_konoko();
		} else if ((c = slot_valid(who) ? character(g_pl[who]) : 0)) {
			if (CHR_ACTIVE(c) && (CHR_FLAGS(c) & CHR_DEAD) && !(CHR_FLAGS(c) & CHR_NOCORPSE)) ONrCharacter_MakeCorpse(c);
			ONrGameState_DeleteCharacter(c);
			g_pl[who] = -1;
			memset(g_input[who], 0, sizeof g_input[who]);
		}
		g_was_dead[who] = who == 0;
		log_msg("players: player %d watches (tick %u)", who + 1, GS_TICK(ONgGameState));
		return;
	}
	g_spect &= ~(1u << who);
	log_msg("players: player %d plays again (tick %u)", who + 1, GS_TICK(ONgGameState));
	if (who == 0) {
		if (!(c = player_char(0))) return;
		CHR_FLAGS(c) |= CHR_NOCORPSE;   /* the parked body goes, it leaves nothing */
		respawn(c, 0);
		g_was_dead[0] = 0;
	} else if (!slot_valid(who)) {
		join_player(who);
	}
}

/* ---- Score ---------------------------------------------------------------------------------- *
 * Every hit goes through TakeDamage (0x4e65f0), whose 4th stack argument is the owner of the hit:
 * a handle that 0x51cda0 turns into the attacking character (types 1 and 2), or 0. mp_rules.c
 * reports each hit on a character here. A player who dies (the dead flag appears in a tick)
 * within KILL_CREDIT ticks of a hit by another player gives the last one who hit them a kill; any other
 * death (a fall, their own grenade) is a suicide and costs the dead player a point. All of it
 * happens in ticks, so every PC counts the same.
 *
 * A network match has limits (net.c, from the host's Host dialog): a score (reached going up, or
 * going down when it's negative) and a time in ticks from the match's start. The tick that reaches
 * one marks the match over (g_over_tick, tick state, so a rollback that changes the score changes
 * it too) and the score stops counting; net.c ends the match once that tick ran with real inputs. */

static u8 *player_char(int who)
{
	u8 *gs = ONgGameState;
	if (!gs) return 0;
	if (who == 0) return net_swapped_konoko() ? net_swapped_konoko() : GS_PLAYER(gs);   /* (a joiner's display has its own player there) */
	return slot_valid(who) ? character(g_pl[who]) : 0;
}

u8 *players_char(int who) { return player_char(who); }

static int player_of(u8 *c)
{
	if (!c) return -1;
	for (int who = 0; who < PL; who++)
		if (c == player_char(who)) return who;
	return -1;
}

/* ---- Kill camera (see the top of this file) --------------------------------------------------- */

#define ONrCamera_EnterDetached ((void (ONI_FC *)(void))0x4d7930)
#define BSL_CM_DETACH_CALL      0x4bbfd0
#define BSL_IS_PLAYER_TEST      0x4b8e34

static int killcam_game(void) { return multiplayer_active() && multiplayer_map(); }

static u8 *g_asked;   /* the player the running script just asked chr_is_player about */

static void ONI_FC script_detach(void)
{
	int who;

	if (!killcam_game()) { ONrCamera_EnterDetached(); return; }
	who = player_of(g_asked);
	if (who >= 0) g_killcam[who] = 1;
	g_asked = 0;
}

/* chr_is_player's answer: EAX, the character, on entry to the patch below */
static int script_is_player(u8 *c)
{
	int who = killcam_game() ? player_of(c) : -1;
	if (who >= 0) g_asked = c;
	return who >= 0 || c == GS_PLAYER(ONgGameState);
}

int players_killcam(u8 *c)
{
	int who = killcam_game() ? player_of(c) : -1;
	return who >= 0 && g_killcam[who] && (CHR_FLAGS(c) & CHR_DEAD);
}

static void killcam_install(void)
{
	/* mov edx,[ONgGameState]; mov esi,[edx+0xac]; cmp esi,eax; pop esi; sete al */
	static const u8 stock[] = { 0x8b, 0x15, 0x7c, 0xce, 0x5e, 0x00, 0x8b, 0xb2, 0xac, 0x00, 0x00, 0x00,
	                            0x3b, 0xf0, 0x5e, 0x0f, 0x94, 0xc0 };
	/* push ecx; push eax; call script_is_player; add esp,4; pop ecx; pop esi; nops */
	u8 code[sizeof stock] = { 0x51, 0x50, 0xe8, 0, 0, 0, 0, 0x83, 0xc4, 0x04, 0x59, 0x5e,
	                          0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	u32 rel = (u32)script_is_player - (BSL_IS_PLAYER_TEST + 7);

	if (!code_matches(BSL_IS_PLAYER_TEST, stock, sizeof stock)) {
		log_msg("players: 0x%x changed, no kill camera", BSL_IS_PLAYER_TEST);
		return;
	}
	if (!patch_call(BSL_CM_DETACH_CALL, (u32)ONrCamera_EnterDetached, (u32)script_detach)) {
		log_msg("players: 0x%x changed, no kill camera", BSL_CM_DETACH_CALL);
		return;
	}
	memcpy(code + 3, &rel, 4);
	patch_write(BSL_IS_PLAYER_TEST, code, sizeof code);
}

/* The hit's owner as a player: through 0x51cda0 (attacker), else by the handle's slot (owner
 * types 1 weapon and 2 melee: 0x8000 | salt << 8 | slot in the low 16 bits), which still finds a
 * player whose character the stock lookup turned down. -1: not another player's hit. */
static int owner_player(u8 *attacker, u32 owner)
{
	int a = player_of(attacker);
	u32 type = owner >> 24, slot = owner & 0xff;
	if (a >= 0 || (type != 1 && type != 2) || slot >= ONgCharacterSlots) return a;
	return player_of(character((int)slot));
}

/* An environmental hit (owner type 4: a particle with no owner) near where another player just
 * died: that player's death effect. mad_bomber's (ONCC DeathParticle mad_e01 -> mad_p04 ->
 * the blast mad_x01) drops the owner Oni gives it (HasDamageOwner off in mad_e01 and mad_x01),
 * so his blast's kills went to no one and cost the victim a point. -1: none. */
static int blast_player(u8 *victim, u32 owner)
{
	int best = -1;
	u32 now = GS_TICK(ONgGameState), best_tick = 0;
	const float *p = CHR_POS(victim);
	if (owner >> 24 != 4) return -1;
	for (int d = 0; d < PL; d++) {
		u32 t = g_death_tick[d];
		if (!t || now + 1 - t > BLAST_CREDIT || t <= best_tick) continue;
		float dx = p[0] - g_death_pos[d][0], dy = p[1] - g_death_pos[d][1], dz = p[2] - g_death_pos[d][2];
		if (dx * dx + dy * dy + dz * dz > BLAST_REACH * BLAST_REACH) continue;
		best = d;
		best_tick = t;
	}
	return best;
}

void players_note_hit(u8 *victim, u8 *attacker, u32 owner)
{
	int v = player_of(victim), a, kind;
	u8 *weapon;
	if (v < 0 || !ONgGameState) return;
	a = owner_player(attacker, owner);
	kind = obit_hit_cause(attacker ? attacker : a >= 0 ? player_char(a) : 0, owner, &weapon);
	if (kind == OBIT_WEAPON && !weapon && a >= 0) weapon = g_held[a];   /* dropped it since it fired */
	if (a < 0 && (a = blast_player(victim, owner)) >= 0) {
		kind = OBIT_BLAST;
		if (a != v && g_hit_by[v] != a)
			log_msg("players: player %d caught in player %d's death blast (tick %u)", v + 1, a + 1, GS_TICK(ONgGameState));
	}
	if (a < 0 || a == v) {
		g_last_owner[v] = owner;   /* for the log if this player then dies with no credit */
		g_last_cause[v] = kind;    /* and the kill feed's icon: a fall, their own weapon or blast */
		g_last_weapon[v] = weapon;
		return;
	}
	if (!attacker && owner >> 24 != 4) log_msg("players: player %d hit by owner %08x, credited to player %d by its slot (tick %u)", v + 1, owner,
	                       a + 1, GS_TICK(ONgGameState));
	g_hit_by[v] = a;
	g_hit_tick[v] = GS_TICK(ONgGameState);
	g_hit_cause[v] = kind;
	g_hit_weapon[v] = weapon;
}

int players_can_change(int who)
{
	return who >= 0 && who < PL && multiplayer_map() && alive(player_char(who)) && !waits_for_respawn(who);
}

int players_score(int who) { return who >= 0 && who < PL ? g_score[who] : 0; }

int players_enabled(void) { return g_enabled; }
u32 players_respawn_ticks(void) { return g_respawn_ticks; }
void players_set_respawn_ticks(u32 ticks) { g_respawn_ticks = ticks; }

void players_set_limits(int on, u32 ticks, int score)
{
	g_limits_on = on;
	g_limit_ticks = ticks;
	g_limit_score = score;
	g_limit_start = ONgGameState ? GS_TICK(ONgGameState) : 0;
	g_over_tick = 0;
}

u32 players_over_tick(void) { return g_limits_on ? g_over_tick : 0; }

int players_time_left(void)
{
	u32 played;
	if (!g_limits_on || !g_limit_ticks || !ONgGameState) return -1;
	played = GS_TICK(ONgGameState) - g_limit_start;
	return played >= g_limit_ticks ? 0 : (int)(g_limit_ticks - played);
}

/* Better placed: nearer the score limit (the lower score with a negative one) */
static int better(int a, int b) { return g_limit_score < 0 ? a < b : a > b; }

/* Who won among the players in mask (bit per slot): nearest the score limit, -1 for a draw */
int players_winner(u32 mask)
{
	int best = -1, tie = 0;
	mask &= ~g_spect;   /* watchers aren't in the running */
	for (int s = 0; s < PL; s++) {
		if (!(mask >> s & 1)) continue;
		if (best < 0 || better(g_score[s], g_score[best])) best = s, tie = 0;
		else if (g_score[s] == g_score[best]) tie = 1;
	}
	return tie ? -1 : best;
}

/* Slots of mask in finishing order (best first); returns how many */
int players_ranking(u32 mask, int *out)
{
	int n = 0;
	mask &= ~g_spect;
	for (int s = 0; s < PL; s++) {
		if (!(mask >> s & 1)) continue;
		int k = n++;
		while (k > 0 && better(g_score[s], g_score[out[k - 1]])) { out[k] = out[k - 1]; k--; }
		out[k] = s;
	}
	return n;
}

void players_reset_scores(void)
{
	for (int who = 0; who < PL; who++) {
		g_score[who] = 0;
		g_hit_by[who] = -1;
		g_last_owner[who] = 0;
		g_last_cause[who] = OBIT_SKULL;
		g_held[who] = 0;
		g_death_tick[who] = 0;
		g_class[who] = -1;
		g_as[who] = -1;
		g_noted[who] = g_settled[who] = 0;
		g_killcam[who] = 0;
		u8 *c = player_char(who);
		g_was_dead[who] = c && (CHR_FLAGS(c) & CHR_DEAD);
	}
	obit_reset();
}

static int limit_reached(void)
{
	u32 played = GS_TICK(ONgGameState) - g_limit_start;
	for (int who = 0; who < PL && g_limit_score; who++)
		if (g_limit_score > 0 ? g_score[who] >= g_limit_score : g_score[who] <= g_limit_score) return 1;
	return g_limit_ticks && played >= g_limit_ticks;
}

static void score_tick(void)
{
	if (g_over_tick) return;   /* the match is over: the score stays as it ended */
	for (int who = 0; who < PL; who++) {
		u8 *c = player_char(who), *w;
		int dead = c && (CHR_FLAGS(c) & CHR_INUSE) && (CHR_FLAGS(c) & CHR_DEAD);
		if (c && !dead && (w = obit_weapon_class(c))) g_held[who] = w;
		if (dead && !g_was_dead[who]) {
			int killer = g_hit_by[who];
			g_death_tick[who] = GS_TICK(ONgGameState) + 1;
			memcpy(g_death_pos[who], CHR_POS(c), sizeof g_death_pos[who]);
			if (killer >= 0 && GS_TICK(ONgGameState) - g_hit_tick[who] <= KILL_CREDIT) {
				if (!(g_spect >> killer & 1)) g_score[killer]++;   /* a watcher's last grenade scores nothing */
				obit_add(killer, who, g_hit_cause[who], g_hit_weapon[who]);
				log_msg("players: player %d killed player %d (%d : %d : %d : %d)", killer + 1, who + 1, g_score[0], g_score[1],
				        g_score[2], g_score[3]);
			} else {
				g_score[who]--;
				obit_add(-1, who, g_last_cause[who], g_last_weapon[who]);
				log_msg("players: player %d died on their own (%d : %d : %d : %d; last uncredited hit owner %08x)", who + 1,
				        g_score[0], g_score[1], g_score[2], g_score[3], g_last_owner[who]);
			}
			g_hit_by[who] = -1;
			g_last_owner[who] = 0;
			g_last_cause[who] = OBIT_SKULL;
		}
		g_was_dead[who] = dead;
	}
	if (g_limits_on && limit_reached()) {
		g_over_tick = GS_TICK(ONgGameState) + 1;
		log_msg("players: the match is over at tick %d (%d : %d : %d : %d)", GS_TICK(ONgGameState), g_score[0], g_score[1],
		        g_score[2], g_score[3]);
	}
}

static void respawn_tick(const u32 *p1_input)
{
	u8 *gs = ONgGameState, *p1 = GS_PLAYER(gs);

	if (!multiplayer_map()) return;
	if (g_spect & 1) ;   /* watching: Konoko stays parked (see "Spectating") */
	else if (p1 && wants_respawn(p1, p1_input)) respawn(p1, 0);
	else change_class(0, p1);
	for (int s = 1; s < PL; s++) {
		u8 *c = slot_valid(s) ? character(g_pl[s]) : 0;
		if (!c) continue;
		if (wants_respawn(c, g_input[s])) {
			g_pl[s] = respawn(c, s);
			memset(g_input[s], 0, sizeof g_input[s]);
		} else {
			change_class(s, c);
		}
	}
}

void players_install(void)
{
	static const u32 spawn_sites[] = { 0x48a2ee, 0x48a419, 0x48a518 };
	static const u32 aim_sites[] = { 0x4de1ed, 0x4de211 };
	const u8 *p = (const u8 *)ONcTick_SetInputCall;
	HANDLE x;

	g_enabled = GetPrivateProfileIntA("players", "p2", 1, ini_path()) != 0;
	if (!g_enabled) return;
	g_pad = GetPrivateProfileIntA("players", "p2_pad", 0, ini_path()) & 3;
	g_aim_speed = (float)(int)GetPrivateProfileIntA("players", "p2_aim_speed", 8, ini_path());
	g_invert_y = GetPrivateProfileIntA("players", "p2_invert_y", 0, ini_path()) != 0;
	{
		char buf[16];
		const char *s = buf;
		float seconds;
		GetPrivateProfileStringA("multiplayer", "respawn_delay", "1.5", buf, sizeof buf, ini_path());
		if (parse_float(&s, &seconds) && seconds >= 0) g_respawn_ticks = (u32)(seconds * 60 + 0.5f);
	}
	if (p[0] != 0xe8 || ONcTick_SetInputCall + 5 + *(u32 *)(p + 1) != (u32)ONrCharacter_SetInput) {
		log_msg("players: 0x%x changed, no player 2", ONcTick_SetInputCall);
		g_enabled = 0;
		return;
	}
	for (u32 i = 0; i < 3; i++) {
		const u8 *q = (const u8 *)spawn_sites[i];
		if (q[0] != 0xe8 || spawn_sites[i] + 5 + *(u32 *)(q + 1) != (u32)OBJrEnumerate) {
			log_msg("players: 0x%x changed, no player 2", spawn_sites[i]);
			g_enabled = 0;
			return;
		}
	}
	for (u32 i = 0; i < 2; i++) {
		const u8 *q = (const u8 *)aim_sites[i];
		if (q[0] != 0xe8 || aim_sites[i] + 5 + *(u32 *)(q + 1) != (u32)ONrCharacter_AutoAim) {
			log_msg("players: auto-aim call 0x%x changed, no player 2", aim_sites[i]);
			g_enabled = 0;
			return;
		}
	}
	for (u32 i = 0; i < 3; i++) patch_call(spawn_sites[i], (u32)OBJrEnumerate, (u32)spawn_players);
	for (u32 i = 0; i < 2; i++) patch_call(aim_sites[i], (u32)ONrCharacter_AutoAim, (u32)auto_aim);
	patch_call(ONcTick_SetInputCall, (u32)ONrCharacter_SetInput, (u32)set_input);
	killcam_install();
	x = LoadLibraryA("xinput1_3.dll");
	if (!x) x = LoadLibraryA("xinput9_1_0.dll");
	g_xinput = x ? (XInputGetState_t)GetProcAddress(x, "XInputGetState") : 0;
	log_msg("players: player 2 on gamepad %d%s (place a second character with the Player flag)",
	        g_pad, g_xinput ? "" : ", but XInput didn't load");
}
