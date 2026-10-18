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
 * Arklite.ini [players]: p2=0 turns it off, p2_pad (0..3), p2_aim_speed (8: aim per tick at
 * full right stick), p2_invert_y. The camera follows Konoko. */
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

#define ONrCharacter_AutoAim    ((u8 (ONI_FC *)(u8 *, u8 *, u8 *))0x4efad0)

#define CHR_SIZE        0x16a0
#define CHR_FLAGS(c)    (*(u32 *)((c) + 4))
#define CHR_INUSE       0x8000
#define CHR_NOINPUT     0x400000   /* the tick drops the player's record */
#define CHR_TYPE(c)     (*(u32 *)((c) + 0x1e8))   /* 0 player, 2 AI */
#define CHR_HELD(c)     (*(u32 *)((c) + 0x162c))  /* no aiming while set */

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

static void ONI_FC set_input(u8 *chr, u32 *input)
{
	ONrCharacter_SetInput(chr, input);   /* Konoko */
	if (g_p2 >= 0) player2_tick();
}

/* ---- Spawning ------------------------------------------------------------------------------ */

static u8 ONI_FC spawn_player(u8 *obj, u32 *first_seen)
{
	u32 f = OBJ_FLAGS(obj), index = 0;
	u16 err;

	if (!(f & OBJ_ISPLAYER)) return 1;
	if (!*first_seen) {   /* Konoko's, which the game just spawned */
		*first_seen = 1;
		return 1;
	}
	if (f & OBJ_SPAWNED) return 1;
	OBJ_FLAGS(obj) = f | OBJ_SPAWNED;
	/* NewCharacter makes any charType 0 character the player (0x4fc8f0 at 0x4db785):
	 * keep Konoko as player 1 */
	u8 *gs = ONgGameState;
	u32 player = *(u32 *)(gs + 0xac), player_b0 = *(u32 *)(gs + 0xb0);
	err = ONrGameState_NewCharacter(obj, 0, 0, &index);
	*(u32 *)(gs + 0xac) = player;
	*(u32 *)(gs + 0xb0) = player_b0;
	if (err) log_msg("players: can't spawn %s (error %d)", OBJ_NAME(obj), err);
	else log_msg("players: spawned %s", OBJ_NAME(obj));
	return 1;   /* keep walking */
}

/* Replaces the walks that spawn the player */
static u32 ONI_FC spawn_players(u32 type, void *callback, u32 param)
{
	u32 r = OBJrEnumerate(type, callback, param), first_seen = 0;
	OBJrEnumerate(OBJcType_CHAR, spawn_player, (u32)&first_seen);
	return r;
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
