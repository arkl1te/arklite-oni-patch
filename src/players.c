/* Multiplayer step 2: a second player on the same PC, on a gamepad.
 *
 * Player 2 is the second character with the Player flag: AI2 setup (0x48a6b0) gives a Player
 * placement charType 0 (+0x1e8, no AI; AI characters are 2), and Konoko is the one in
 * gs+0xac (PlayerCharacter). Place a second Player-flagged character in the map.
 *
 * Oni spawns only one of them: the level start walks the CHAR objects (0x4d0080, type 'CHAR')
 * with 0x48a3a0, which spawns the first with IsPlayer (object +0x2c bit 0, +0x2c |= 0x20 for
 * spawned, ONrGameState_NewCharacter 0x4dac50) and stops the walk. The three walks with it
 * (0x48a2ee, 0x48a419, 0x48a518) are wrapped: after each, Arklite walks again and spawns every
 * other IsPlayer character that isn't spawned yet, keeping Konoko in gs+0xac (NewCharacter
 * makes every charType 0 character the player).
 *
 * Every tick (0x4fa6a0) turns that tick's action record into the input state at gs+0xb8
 * (0x30 bytes: +0 aim up/down and +4 aim left/right from the record's +0x28/+0x24, +0x10
 * buttons held, +0x18 not held, +0x20 pressed, +0x28 released; the buttons go through
 * 0x4fa520 first, which reads the character in gs+0xac) and hands it to the player with
 * ONrCharacter_SetInput (0x4ed420, call at 0x4fa934), which copies it into the character's
 * active data (+0x2158). The AI drives its characters through that same slot. Right after
 * Konoko's call, Arklite does the same for player 2 with player 2's record and its own input
 * state, pointing gs+0xac at player 2 while 0x4fa520 runs.
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
 * Player-flagged CHAR placement is a spawn point. The first is Konoko's own placement and the
 * second player 2's; the third and later ones (player3, player4, ...) are only spawn points and
 * aren't spawned. Each player starts at a spawn point picked at random (Konoko by moving her
 * placement there for the game's own walk, player 2 the same way for NewCharacter) and, after
 * dying, comes back at one: once respawn_delay seconds have passed since the death tick (char
 * +0x1638, set by the death function 0x4eb550 with the dead flag, +4 bit 0), the first tick
 * whose input state has an attack button pressed (punch, kick, fire1-3) respawns that player:
 * the body becomes a corpse
 * (0x4ef340, as the character update does for dead AIs), the character is deleted
 * (ONrGameState_DeleteCharacter 0x4dc480) and spawned again from its own placement, moved to the
 * spawn point. A character spawned from a placement takes the placement's position (+0xc) and
 * facing (+0x228). The pick is random among the points no other living player is near (the
 * farthest one if none), from a hash of the level, the tick and the player, so every PC and
 * every replay picks the same. Respawning runs in the tick, right after both players' input.
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
#define OBJ_NAME(o)             ((const char *)(o) + 0x70)
#define OBJ_POS(o)              ((u32 *)((o) + 0xc))      /* x, y, z */
#define OBJ_FACING(o)           (*(u32 *)((o) + 0x228))
#define ONrGameState_DeleteCharacter ((void (ONI_FC *)(u8 *))0x4dc480)
#define ONrCharacter_MakeCorpse ((void (ONI_FC *)(u8 *))0x4ef340)

#define ONrCharacter_AutoAim    ((u8 (ONI_FC *)(u8 *, u8 *, u8 *))0x4efad0)

#define CHR_SIZE        0x16a0
#define CHR_FLAGS(c)    (*(u32 *)((c) + 4))
#define CHR_INUSE       0x8000
#define CHR_NOINPUT     0x400000   /* the tick drops the player's record */
#define CHR_TYPE(c)     (*(u32 *)((c) + 0x1e8))   /* 0 player, 2 AI */
#define CHR_HELD(c)     (*(u32 *)((c) + 0x162c))  /* no aiming while set */
#define CHR_DEAD        0x1
#define CHR_NOCORPSE    0x80000   /* the character update makes no corpse for it either */
#define CHR_ACTIVE(c)   (*(u16 *)((c) + 2))       /* active data index, 0: none */
#define CHR_POS(c)      ((float *)((c) + 0x148))
#define CHR_DEATH_TICK(c) (*(u32 *)((c) + 0x1638))

__declspec(dllimport) HANDLE WINAPI LoadLibraryA(const char *);
__declspec(dllimport) void * WINAPI GetProcAddress(HANDLE, const char *);
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

static int g_p2 = -1;                  /* character index, -1: none */
static u32 g_level;
static u32 g_input[12];                /* player 2's input state, like gs+0xb8 */
static u32 g_record[12];               /* this frame's record */
static int g_has_record, g_tick_index;

#define MAX_SPAWN       64
#define SPAWN_CLEAR     60.0f   /* no other living player this near: a free spawn point */
static u8 *g_spawn[MAX_SPAWN];         /* Player-flagged placements: [0] Konoko's, [1] player 2's */
static int g_nspawn;
static u32 g_spawn_level;             /* for the spawn pick only: the level isn't current yet */
static int g_p1_point = -1;            /* where Konoko just spawned, for player 2's pick */
static u32 g_respawn_ticks = 90;       /* dead this long, an attack button brings a player back */
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

/* The second Player-flagged character (charType 0, not Konoko), or -1 */
static int find_player2(void)
{
	u8 *p1 = GS_PLAYER(ONgGameState);
	u32 slots = ONgCharacterSlots;
	for (u32 i = 0; i < slots; i++) {
		u8 *c = character(i);
		if ((CHR_FLAGS(c) & CHR_INUSE) && CHR_TYPE(c) == 0 && c != p1) return i;
	}
	return -1;
}

static int player2_valid(void)
{
	u8 *c;
	if (g_p2 < 0) return 0;
	c = character(g_p2);
	return (CHR_FLAGS(c) & CHR_INUSE) && CHR_TYPE(c) == 0 && c != GS_PLAYER(ONgGameState);
}

int multiplayer_active(void)
{
	return g_enabled && ONgGameState && player2_valid();
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
	if (level != g_level || (g_p2 >= 0 && !player2_valid())) {
		g_level = level;
		g_p2 = -1;
		memset(g_input, 0, sizeof g_input);
	}
	if (g_p2 < 0 && level) {
		g_p2 = find_player2();
		if (g_p2 >= 0) {
			memset(g_input, 0, sizeof g_input);
			log_msg("players: player 2 is #%d %s", g_p2, (const char *)character(g_p2) + 0x14);
		}
	}
}

const u32 *players_begin_frame(void)
{
	g_tick_index = 0;
	g_has_record = 0;
	if (!g_enabled || !ONgGameState) return 0;
	new_level_check();
	if (g_p2 < 0) return 0;
	u32 ticks = GS_SERVER_TIME(ONgGameState) - GS_TICK(ONgGameState);
	poll_pad(g_record, ticks > 6 ? 6 : ticks ? ticks : 1);
	const u32 *mask = (const u32 *)gs_field(GS_ACTION_MASK);
	g_record[0] &= mask[0];
	g_record[1] &= mask[1];
	g_has_record = 1;
	return g_record;
}

void players_replay_frame(const u32 *rec)
{
	g_tick_index = 0;
	g_has_record = 0;
	if (!g_enabled || !ONgGameState) return;
	new_level_check();
	if (rec) {
		memcpy(g_record, rec, 0x30);
		g_has_record = 1;
	}
}

/* Player 2's tick input, as the tick builds Konoko's */
static void player2_tick(void)
{
	u8 *gs = ONgGameState, *p2, *p1;
	u32 rec[12], *src = 0;
	u32 lo, hi;
	u64 b;

	if (!player2_valid()) return;
	p2 = character(g_p2);
	if (g_has_record && g_tick_index == 0) {
		memcpy(rec, g_record, sizeof rec);
		src = rec;
	}
	g_tick_index++;
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
	g_input[0] = ud;
	g_input[1] = lr;

	/* buttons: 0x4fa520 works on the character in gs+0xac */
	lo = src ? rec[0] : g_input[4];
	hi = src ? rec[1] : g_input[5];
	p1 = GS_PLAYER(gs);
	GS_PLAYER(gs) = p2;
	b = ONiPlayerButtons(lo, hi);
	GS_PLAYER(gs) = p1;
	lo = (u32)b;
	hi = (u32)(b >> 32);

	u32 prev_lo = g_input[4], prev_hi = g_input[5], not_lo = g_input[6], not_hi = g_input[7];
	g_input[4] = lo;  g_input[5] = hi;               /* held */
	g_input[6] = ~lo; g_input[7] = ~hi;              /* not held */
	g_input[8] = not_lo & lo;  g_input[9] = not_hi & hi;      /* pressed */
	g_input[10] = prev_lo & ~lo; g_input[11] = prev_hi & ~hi; /* released */
	if (GS_TICK(gs) < 0xe) {                         /* the level's first ticks, as for Konoko */
		g_input[5] &= 0xfffffe3f;
		g_input[8] = g_input[9] = g_input[10] = g_input[11] = 0;
	}
	ONrCharacter_SetInput(p2, g_input);
}

static void respawn_tick(const u32 *p1_input);

static void ONI_FC set_input(u8 *chr, u32 *input)
{
	ONrCharacter_SetInput(chr, input);   /* Konoko */
	if (g_p2 >= 0) player2_tick();
	if (multiplayer_active()) {
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
	u32 r = hash(g_spawn_level * 0x9e3779b9u ^ hash(GS_TICK(ONgGameState) * 0x85ebca6bu + who));
	return free[r % nfree];
}

static u8 ONI_FC collect_spawn(u8 *obj, u32 unused)
{
	if ((OBJ_FLAGS(obj) & OBJ_ISPLAYER) && g_nspawn < MAX_SPAWN) g_spawn[g_nspawn++] = obj;
	return 1;
}

int multiplayer_map(void)
{
	return g_nspawn >= 2;   /* collected by every level's player spawn walk */
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
	return !err;
}

static u8 ONI_FC spawn_player(u8 *obj, u32 *seen)
{
	u32 f = OBJ_FLAGS(obj), index;

	if (!(f & OBJ_ISPLAYER)) return 1;
	/* the first is Konoko's, which the game just spawned; the third and later are spawn points */
	if ((*seen)++ != 1 || (f & OBJ_SPAWNED)) return 1;
	OBJ_FLAGS(obj) = f | OBJ_SPAWNED;
	if (multiplayer_map()) {
		const float *avoid = g_p1_point >= 0 ? (const float *)OBJ_POS(g_spawn[g_p1_point]) : 0;
		int point = choose_spawn(&avoid, avoid ? 1 : 0, 2);
		if (spawn_at(obj, point, 1, &index))
			log_msg("players: spawned %s at spawn point %d", OBJ_NAME(obj), point);
	} else {
		u8 *gs = ONgGameState;
		u32 player = *(u32 *)(gs + 0xac), player_b0 = *(u32 *)(gs + 0xb0);
		u16 err = ONrGameState_NewCharacter(obj, 0, 0, &index);
		*(u32 *)(gs + 0xac) = player;
		*(u32 *)(gs + 0xb0) = player_b0;
		if (err) log_msg("players: can't spawn %s (error %d)", OBJ_NAME(obj), err);
		else log_msg("players: spawned %s", OBJ_NAME(obj));
	}
	return 1;   /* keep walking */
}

/* Replaces the walks that spawn the player (all three walk CHAR with 0x48a3a0, which spawns the
 * first Player-flagged placement) */
static u32 ONI_FC spawn_players(u32 type, void *callback, u32 param)
{
	u32 r, seen = 0;

	g_nspawn = 0;
	g_spawn_level = ONrLevel_GetCurrent();
	OBJrEnumerate(OBJcType_CHAR, collect_spawn, 0);
	g_p1_point = -1;
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
		g_p1_point = point;
		log_msg("players: multiplayer map, %d spawn points; Konoko at spawn point %d", g_nspawn, point);
	} else {
		r = OBJrEnumerate(type, callback, param);
	}
	OBJrEnumerate(OBJcType_CHAR, spawn_player, (u32)&seen);
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

static int respawn(u8 *c, u8 *other, int is_p2)
{
	const float *avoid = 0;
	u32 index;

	if (other && (CHR_FLAGS(other) & CHR_INUSE) && !(CHR_FLAGS(other) & CHR_DEAD))
		avoid = CHR_POS(other);
	int point = choose_spawn(&avoid, avoid ? 1 : 0, is_p2 ? 2 : 1);
	if (CHR_ACTIVE(c) && !(CHR_FLAGS(c) & CHR_NOCORPSE)) ONrCharacter_MakeCorpse(c);
	ONrGameState_DeleteCharacter(c);
	/* Konoko comes back as the player (NewCharacter sets gs+0xac); player 2 doesn't */
	if (!spawn_at(g_spawn[is_p2], point, is_p2, &index)) return -1;
	log_msg("players: %s respawned at spawn point %d (tick %u)", is_p2 ? "player 2" : "Konoko",
	        point, GS_TICK(ONgGameState));
	return (int)index;
}

static void respawn_tick(const u32 *p1_input)
{
	u8 *gs = ONgGameState, *p1 = GS_PLAYER(gs), *p2 = player2_valid() ? character(g_p2) : 0;

	if (!multiplayer_map()) return;
	if (p1 && wants_respawn(p1, p1_input)) respawn(p1, p2, 0);
	p1 = GS_PLAYER(gs);
	if (p2 && wants_respawn(p2, g_input)) {
		int index = respawn(p2, p1, 1);
		g_p2 = index;
		memset(g_input, 0, sizeof g_input);
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
	x = LoadLibraryA("xinput1_3.dll");
	if (!x) x = LoadLibraryA("xinput9_1_0.dll");
	g_xinput = x ? (XInputGetState_t)GetProcAddress(x, "XInputGetState") : 0;
	log_msg("players: player 2 on gamepad %d%s (place a second character with the Player flag)",
	        g_pad, g_xinput ? "" : ", but XInput didn't load");
}
