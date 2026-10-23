/* Lockstep core for multiplayer, step 1: record a level's input and replay it, comparing the
 * game state tick by tick. Multiplayer by input exchange only works if the same inputs give
 * the same game on every PC; this is the test for that.
 *
 *   ARKLITE_RECORD=<file>   record the next level played: its inputs and per-tick state
 *   ARKLITE_REPLAY=<file>   start the same level the same way; the recorded inputs replace
 *                           live input and every tick is compared, results in Arklite.log
 *
 * The game loop (0x4d4640) takes each frame's action records from LIrActionBuffer_Get
 * (0x403be0; Daodan's DDrActionBuffer_Get with its input patch), then runs the game update
 * (0x4fafb0, ECX count, EDX actions, [esp] &ticks run; call at 0x4d472b). The update runs
 * server time (gs+0x148) - game tick (gs+0x144) ticks, at most 6, and gives tick i the record
 * actions[i], or none past the count: 0x4fa6a0(ECX action), called at 0x4fb133. Each tick is
 * 1/60 s. A recording stores per frame the action records and, after every tick, the RNGs and
 * every character in use: a summary, and each dword of the character (0x16a0) and its active
 * data (0x2838) that changed since the last tick. A replay runs each frame's recorded ticks
 * with its recorded actions, so live input and the frame rate don't matter, and compares
 * every dword.
 *
 * Both RNG streams start at 0xfded (0x426dad), but menus and loading advance them, so the
 * recording starts from the RNG at the level's first update and the replay restores it.
 *
 * Fixes, always on, because multiplayer needs them on every PC:
 *   - The ambient sound players (0x480fa0, 0x481140, 0x4812a0) wait on the wall clock
 *     (UUrMachineTime_Sixtieths) and then pick sounds with the game RNG, as does the sound
 *     permutation pick (0x47f8d0) they use, so an ambient sound one frame earlier or later
 *     shifted the RNG for the AI. The weighted sound pick (0x480040) does the same with the
 *     local RNG inside ticks. All 10 calls now draw from Arklite's own sound RNG.
 *   - The local RNG (UUrLocalRandom, 0x426e30, seed 0x5326d8) is for effects, and drawing
 *     reseeds it (0x41e500, from the draw state setup 0x41e340, in draw code that also reads
 *     the wall clock). But the particle update (0x45c830 -> 0x45d610) runs inside the tick and
 *     uses it, and bullets are particles: a swat's knockback desynced one tick after an
 *     identical hit. Calls made inside a tick get their own stream, which only ticks advance.
 *     Esc opens the pause menu from inside a tick (utility input 0x4f6200 -> 0x4bec00 ->
 *     0x4d2c70), and a modal dialog keeps drawing the game, so every WMrDialog_ModalBegin call
 *     (8 sites) counts as outside the tick.
 *   - A character's speech line (state at +0x15f0: speaking, +0x15f4 sound handle, +0x15f8 end
 *     tick, +0x15fc priority) ended when its sound stopped playing (0x511ec0 at 0x516aba, which
 *     also moves the 3D sound) or failed to start for want of a channel (0x516b5a), both up to
 *     the sound card. Now it ends at its end tick: start tick + the sound's length in ticks
 *     (0x5125c0, from the sound data). The sound handle isn't compared.
 *   - Collision queries mark what they visited in a bit vector with an undo list (0x437d60:
 *     test-and-set, recording the index; 0x437d20 clears only the recorded bits; two of them,
 *     *0x5ccebc for the level octree with room for 4096 and *0x5ccec4 for quads with room for
 *     765, made at level load by 0x440f30). The bit was set before checking the room, so a query
 *     visiting more than that left bits set for good, and every later query skipped those
 *     quads: characters stopped colliding with them. The camera and the laser sight query the
 *     level too, differently on each PC, so each PC lost different walls (the network games'
 *     desyncs: a different number of wall contacts, active +0xb0, from identical state). Now
 *     a query that overflows the undo list gets the whole vector cleared at its clear (sizes
 *     noted where the vectors are made: 0x440f48, 0x440fa0, 0x440ff8). Marking itself is as
 *     before: an earlier fix that just stopped marking past the room made long queries (the
 *     beam weapon's) test the same entries over and over.
 *
 * While recording or replaying, both RNGs keep their callers: each tick stores the return
 * addresses of the calls since the last tick (bit 31: outside a tick, bit 30: tick local
 * stream), so a desync in the RNG names the code that drew differently. */
#include "arklite.h"

#define ONrGameState_Update ((u32 (ONI_FC *)(u32, void *, u32 *))0x4fafb0)
#define ONcGameState_UpdateCall 0x4d472b
#define ONrGameState_Tick   ((void (ONI_FC *)(void *))0x4fa6a0)
#define ONcGameState_TickCall 0x4fb133
#define ONgGameState        (*(u8 **)0x5ece7c)
#define UUrRandom           0x426e10                   /* u16 from the game LCG at 0x5326d4 */
#define UUrLocalRandom      0x426e30                   /* u16 from the effects LCG at 0x5326d8 */
#define UUgRandomSeed       (*(u32 *)0x5326d4)
#define UUgLocalRandomSeed  (*(u32 *)0x5326d8)
#define GS_SYNC_BEHIND(gs)  (*(u32 *)((gs) + 0x44))    /* ticks owed when behind */
#define GS_TICK(gs)         (*(u32 *)((gs) + 0x144))
#define GS_SERVER_TIME(gs)  (*(u32 *)((gs) + 0x148))
#define GS_CHARACTERS(gs)   ((gs) + 0x1260)
#define GS_ACTIVE_STOCK     0xb6268                    /* active characters, 0x2838 each */
#define ONrLevel_GetCurrent ((u16 (ONI_FC *)(void))0x5054c0)
/* cmp dx, imm16 in NewCharacter's free-slot search: 0x80, or 255 with char_limit.c */
#define ONgCharacterSlots   (*(u16 *)(0x4dac95 + 3))

#define CHR_SIZE     0x16a0
#define CHR_INUSE    0x8000                    /* Flags (+4) */
/* Active data: 0 none, else the slot is the low byte (the high byte holds flags, e.g. 0x8201
 * is slot 1; 0x4f1180 does the same) */
#define CHR_ACTIVE_INDEX(c) (*(u16 *)((c) + 2))
/* Drawn frames off screen (0x4e27d0, from the character draw pass 0x4e1540), 0 when on
 * screen. Auto-aim (0x4efad0, in the tick) only aims at characters with 0. The count depends on
 * the frame rate and on frames drawn behind a pause menu, so only "on screen" is compared. */
#define CHR_OFFSCREEN_FRAMES 0x163c
#define ACTIVE_SIZE  0x2838
#define IMG_SIZE     (CHR_SIZE + ACTIVE_SIZE)  /* a character and its active data */
#define IMG_DWORDS   (IMG_SIZE / 4)            /* 4022 */
#define ACTION_SIZE  0x30
#define MAX_ACTIONS  128                       /* stock buffer holds 120 */
#define MAX_CHARS    255
#define MAX_CALLS    16384                     /* RNG callers kept per tick */

#define REC_MAGIC    0x50524b41                /* "AKRP" */
#define REC_VERSION  8
#define FRAME_P2     1                         /* RecFrame.flags: player 2's record follows the actions */
#define FRAME_P2N    2                         /* network recording: player 2's record for every tick */
#define TAG_TICK     0x4b434954                /* "TICK" */
#define TAG_END      0x46444e45                /* "ENDF": end of a frame */

void *memcpy(void *, const void *, unsigned);   /* common.c */
void *memset(void *, int, unsigned);

__declspec(dllimport) int    WINAPI ReadFile(HANDLE, void *, u32, u32 *, void *);
__declspec(dllimport) int    WINAPI CloseHandle(HANDLE);
__declspec(dllimport) u32    WINAPI GetFileSize(HANDLE, u32 *);
__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) int    WINAPI VirtualFree(void *, u32, u32);

/* File layout, little-endian:
 *   RecHeader, then the state at the level's first update (before any tick): a tick record
 *   per frame: RecFrame, actions[n_actions][0x30], player 2's record [0x30] if FRAME_P2,
 *              a tick record per tick run, TAG_END
 *   tick record: TAG_TICK, RecTick, RecChar[n_chars], then per character u32 n_changed and
 *                RecDword[n_changed], then u32 rng_callers[n_calls]
 * The dwords are of the character's image: the character (0x16a0), then its active data
 * (0x2838, zeros when not active); each one that changed since the character's last record. */
typedef struct { u32 magic, version, level, start_tick, seed, fpu_cw, tick_local_seed, reserved; } RecHeader;
typedef struct { u16 n_actions, flags; } RecFrame;
typedef struct { u32 tick, seed, n_chars, n_calls, fpu_cw, tick_local_seed, world; } RecTick;
typedef struct {
	u32 index, flags;
	int health;
	float pos[3], facing, vel[3];
} RecChar;
typedef struct { u32 index, value; } RecDword;

enum { MODE_OFF, MODE_RECORD, MODE_REPLAY };
enum { SESSION_WAIT, SESSION_RUN, SESSION_DONE };

static int g_mode, g_session;
static char g_path[MAX_PATH];
static u32 g_level, g_last_tick;
static u32 g_frames, g_ticks;
static u32 g_tick_in_frame;

/* images: the last recorded state of each character slot (record: written, replay: rebuilt) */
static u32 (*g_images)[IMG_DWORDS];
static u32 g_image_now[IMG_DWORDS];
static u8 g_ignore[IMG_DWORDS];           /* dwords that already differ at level start */

/* record */
static HANDLE g_out = INVALID_HANDLE_VALUE;
static u8 g_wbuf[0x10000];
static u32 g_wlen;
static RecDword g_changes[IMG_DWORDS];

/* replay */
static u8 *g_data;
static u32 g_size, g_pos;
static u8 g_replay_actions[MAX_ACTIONS * ACTION_SIZE];
static u32 g_rec_ticks;           /* recorded ticks in the current frame */
static u32 g_bad_ticks, g_first_bad_frame, g_first_bad_tick;
static u32 g_char_bad_ticks, g_first_char_bad_tick;   /* ticks where a character differs */
static int g_logged_diffs;        /* desyncs logged in detail, at most DIFFS_LOGGED */
#define DIFFS_LOGGED 3

/* RNG callers since the last tick, and per-tick state */
static u32 g_calls[MAX_CALLS], g_ncalls;
static int g_in_tick;
static int g_trace;                       /* recording or replaying: keep RNG callers */
static u32 g_tick_local_seed = 0xfded;    /* UUrLocalRandom inside ticks */
static u32 g_fpu_cw;                      /* at the start of this tick */

static u32 fpu_control_word(void)
{
	u16 cw;
	__asm__ volatile("fnstcw %0" : "=m"(cw));
	return cw;
}

/* ---- RNG ---------------------------------------------------------------------------------- */

/* Each replaces a function that only touches EAX, so they keep every other register. */
#pragma clang diagnostic ignored "-Wexcessive-regsave"   /* __builtin_return_address isn't a call */
__attribute__((no_caller_saved_registers)) static u32 game_random(void)
{
	u32 from = (u32)__builtin_return_address(0);
	if (g_ncalls < MAX_CALLS) g_calls[g_ncalls] = from | (g_in_tick ? 0 : 0x80000000u);
	g_ncalls++;
	UUgRandomSeed = UUgRandomSeed * 0x19660d + 0x3c6ef35f;
	return UUgRandomSeed >> 16;
}

__attribute__((no_caller_saved_registers)) static u32 local_random(void)
{
	if (!g_in_tick) {   /* drawing: the original stream, which it reseeds */
		UUgLocalRandomSeed = UUgLocalRandomSeed * 0x19660d + 0x3c6ef35f;
		return UUgLocalRandomSeed >> 16;
	}
	if (g_trace) {
		if (g_ncalls < MAX_CALLS) g_calls[g_ncalls] = (u32)__builtin_return_address(0) | 0x40000000u;
		g_ncalls++;
	}
	g_tick_local_seed = g_tick_local_seed * 0x19660d + 0x3c6ef35f;
	return g_tick_local_seed >> 16;
}

static u32 g_sound_seed = 0xfded;

__attribute__((no_caller_saved_registers)) static u32 sound_random(void)
{
	g_sound_seed = g_sound_seed * 0x19660d + 0x3c6ef35f;
	return g_sound_seed >> 16;
}

static void sound_rng_install(void)
{
	static const u32 sites[][2] = {
		{ 0x47f997, UUrRandom }, { 0x47f9be, UUrRandom },   /* 0x47f8d0: sound permutation pick */
		{ 0x48009d, UUrLocalRandom },                       /* 0x480040: weighted sound pick */
		{ 0x480fb1, UUrRandom },                            /* 0x480fa0: ambient sound start */
		{ 0x4811a9, UUrRandom }, { 0x4811de, UUrRandom },   /* 0x481140: ambient detail */
		{ 0x48124b, UUrRandom },
		{ 0x4815a8, UUrRandom }, { 0x4815dd, UUrRandom },   /* 0x4812a0: ambient update */
		{ 0x48164a, UUrRandom },
	};
	const u32 n = sizeof sites / sizeof sites[0];
	for (u32 i = 0; i < n; i++) {
		const u8 *p = (const u8 *)sites[i][0];
		if (p[0] != 0xe8 || sites[i][0] + 5 + *(u32 *)(p + 1) != sites[i][1]) {
			log_msg("sound RNG: 0x%x changed, skipped (sounds keep using the game RNGs)", sites[i][0]);
			return;
		}
	}
	for (u32 i = 0; i < n; i++) patch_call(sites[i][0], sites[i][1], (u32)sound_random);
	log_msg("sound RNG: %d sound calls moved off the game RNGs", n);
}

static int jump_to(u32 addr, const u8 *expected, void *target)
{
	u8 jmp[5] = { 0xe9 };
	u32 rel = (u32)target - (addr + 5);

	if (!code_matches(addr, expected, 5)) return 0;
	memcpy(jmp + 1, &rel, 4);
	patch_write(addr, jmp, sizeof jmp);
	return 1;
}

/* A modal dialog opened from a tick (the pause menu) draws the game until it closes */
static u16 ONI_FC modal_begin(u32 id, WMtWindow *parent, WMtDialogProc proc, u32 param, u32 *result)
{
	int in_tick = g_in_tick;
	u16 r;

	g_in_tick = 0;
	r = live_menu_modal(id, parent, proc, param, result, in_tick);
	g_in_tick = in_tick;
	return r;
}

/* ---- Collision visited bits ------------------------------------------------------------- */

#define ONcVisited_TestSet  0x437d60
#define ONcVisited_Clear    0x437d20
#define ONrVisited_New      ((u32 *(ONI_FC *)(u32, u32))0x437ce0)   /* room, bits */

/* The vectors made at level load (*0x5ccebc, *0x5ccec4, *0x5ccec0), their sizes in bits, and
 * whether a query overflowed the undo list since the last clear */
static struct { u32 *v; u32 bits; int overflowed; } g_visited[8];

static u32 *ONI_FC visited_new(u32 room, u32 bits)
{
	static u32 next;
	u32 *v = ONrVisited_New(room, bits);
	int k;

	if (!v) return v;
	for (k = 0; k < 8 && g_visited[k].v != v; k++) {}
	if (k == 8) k = next++ % 8;   /* each level load makes new ones: reuse the oldest slots */
	g_visited[k].v = v;
	g_visited[k].bits = bits;
	g_visited[k].overflowed = 0;
	return v;
}

/* ECX: { u8 *bits; int room; int count; u32 undo[room]; }, EDX: the index; 1 if already
 * visited. As Oni's, plus a note when the undo list is full. */
static u32 ONI_FC visited_test_set(u32 *v, u32 i)
{
	u8 *bits = (u8 *)v[0], m = (u8)(1u << (i & 7));
	if (bits[i >> 3] & m) return 1;
	bits[i >> 3] |= m;
	if ((int)v[2] < (int)v[1]) {
		v[3 + v[2]] = i;
		v[2]++;
	} else {
		for (int k = 0; k < 8; k++)
			if (g_visited[k].v == v) g_visited[k].overflowed = 1;
	}
	return 0;
}

/* The original keeps EDX (its callers may still use the index), so keep it here too */
__attribute__((naked)) static void visited_entry(void)
{
	__asm__ volatile(
		"pushl %%edx\n\t"
		"call %c0\n\t"
		"popl %%edx\n\t"
		"ret\n" :: "i"(visited_test_set));
}

/* Undo the recorded bits, as Oni does; after an overflow, clear the whole vector */
static void ONI_FC visited_clear(u32 *v)
{
	u8 *bits = (u8 *)v[0];
	for (int k = 0; k < 8; k++)
		if (g_visited[k].v == v && g_visited[k].overflowed) {
			memset(bits, 0, (g_visited[k].bits + 7) / 8);
			g_visited[k].overflowed = 0;
			v[2] = 0;
			return;
		}
	for (u32 n = 0; n < v[2]; n++) {
		u32 i = v[3 + n];
		bits[i >> 3] &= (u8)~(1u << (i & 7));
	}
	v[2] = 0;
}

static void visited_install(void)
{
	static const u8 test_entry[] = { 0x56, 0x8b, 0xf1, 0x8b, 0x0e };    /* push esi; mov esi,ecx; mov ecx,[esi] */
	static const u8 clear_entry[] = { 0x56, 0x8b, 0xf1, 0x57, 0x33 };   /* push esi; mov esi,ecx; push edi; xor.. */
	static const u32 new_sites[] = { 0x440f48, 0x440fa0, 0x440ff8 };

	for (u32 k = 0; k < 3; k++) {
		const u8 *q = (const u8 *)new_sites[k];
		if (q[0] != 0xe8 || new_sites[k] + 5 + *(u32 *)(q + 1) != (u32)ONrVisited_New ||
		    !code_matches(ONcVisited_TestSet, test_entry, 5) || !code_matches(ONcVisited_Clear, clear_entry, 5)) {
			log_msg("collision: visited marks code changed, skipped");
			return;
		}
	}
	for (u32 k = 0; k < 3; k++) patch_call(new_sites[k], (u32)ONrVisited_New, (u32)visited_new);
	jump_to(ONcVisited_TestSet, test_entry, visited_entry);
	jump_to(ONcVisited_Clear, clear_entry, visited_clear);
	log_msg("collision: visited marks are always undone (walls no longer go missing)");
}

static void local_rng_install(void)
{
	static const u8 entry[] = { 0xa1, 0xd8, 0x26, 0x53, 0x00 };   /* mov eax, [0x5326d8] */
	static const u32 modal_sites[] = { 0x4d289f, 0x4d2cae, 0x4d2ddb, 0x4d2e4b, 0x4d2f2d, 0x4d2f74, 0x501939, 0x504cc5 };
	const u32 n = sizeof modal_sites / sizeof modal_sites[0];

	for (u32 i = 0; i < n; i++) {
		const u8 *p = (const u8 *)modal_sites[i];
		if (p[0] != 0xe8 || modal_sites[i] + 5 + *(u32 *)(p + 1) != (u32)WMrDialog_ModalBegin) {
			log_msg("local RNG: dialog call 0x%x changed, skipped", modal_sites[i]);
			return;
		}
	}
	if (!jump_to(UUrLocalRandom, entry, local_random)) {
		log_msg("local RNG: 0x%x changed, skipped", UUrLocalRandom);
		return;
	}
	for (u32 i = 0; i < n; i++) patch_call(modal_sites[i], (u32)WMrDialog_ModalBegin, (u32)modal_begin);
	log_msg("local RNG: ticks have their own stream, drawing and dialogs keep the original");
}

/* ---- Speech ----------------------------------------------------------------------------- */

#define SSrPlayID_Update       ((u8 (ONI_FC *)(u32, void *, u32, u32))0x511ec0)   /* 0: stopped */
#define ONcSpeech_UpdateCall   0x516aba
#define ONcSpeech_PlayFailedJne 0x516b61   /* jne +7 after cmp [ebp+0x15f4], -1 */
#define CHR_SPEECH_SOUND       0x15f4

static u8 ONI_FC speech_sound_update(u32 id, void *pos, u32 a, u32 b)
{
	SSrPlayID_Update(id, pos, a, b);   /* keep the 3D position */
	return 1;                          /* the line ends at its end tick */
}

static void speech_install(void)
{
	static const u8 jne[] = { 0x75, 0x07 }, jmp[] = { 0xeb, 0x07 };
	const u8 *p = (const u8 *)ONcSpeech_UpdateCall;

	if (p[0] != 0xe8 || ONcSpeech_UpdateCall + 5 + *(u32 *)(p + 1) != (u32)SSrPlayID_Update ||
	    !code_matches(ONcSpeech_PlayFailedJne, jne, 2)) {
		log_msg("speech: 0x%x changed, skipped (lines end when their sound stops)", ONcSpeech_UpdateCall);
		return;
	}
	patch_call(ONcSpeech_UpdateCall, (u32)SSrPlayID_Update, (u32)speech_sound_update);
	patch_write(ONcSpeech_PlayFailedJne, jmp, 2);
	log_msg("speech: lines end after their length in ticks, not when the sound stops");
}

static int rng_trace_install(void)
{
	static const u8 entry[] = { 0xa1, 0xd4, 0x26, 0x53, 0x00 };   /* mov eax, [0x5326d4] */
	return jump_to(UUrRandom, entry, game_random);
}

/* ---- State capture ---------------------------------------------------------------------- */

static u8 *character(u32 index)
{
	return GS_CHARACTERS(ONgGameState) + index * CHR_SIZE;
}

static const char *char_name(u32 index)
{
	return (const char *)character(index) + 0x14;
}

/* The character's image now: the character, then its active data or zeros */
static void image_of(u32 index, u32 *img)
{
	u8 *chr = character(index);
	u16 a = CHR_ACTIVE_INDEX(chr);

	memcpy(img, chr, CHR_SIZE);
	if (a && (a & 0xff) < ONgCharacterSlots)
		memcpy((u8 *)img + CHR_SIZE, ONgGameState + char_limit_gs_offset(GS_ACTIVE_STOCK) + (a & 0xff) * ACTIVE_SIZE, ACTIVE_SIZE);
	else
		memset((u8 *)img + CHR_SIZE, 0, ACTIVE_SIZE);
	img[CHR_OFFSCREEN_FRAMES / 4] = img[CHR_OFFSCREEN_FRAMES / 4] != 0;
}

/* What character collision reads besides the characters (format 8): the level's quad flags
 * (environment [[gs+0x150]+0x40], quads at +0xc: an instance array, count +0x14, 0x38-byte
 * entries from +0x18 with the flags at +0x30) and the dead-body collision ring
 * ([0x5ece80]+0x2f8: +0xc/+0x10/+0x14 indexes, 0x44c-byte entries from +0x18 with a box of 6
 * floats at +0x434; 0x4ebb60 fills one when a character dies) */
static u32 world_hash(void)
{
	u8 *gs = ONgGameState, *level, *env, *quads, *ring;
	u32 h = 0x811c9dc5u;

	level = gs ? *(u8 **)(gs + 0x150) : 0;
	env = level ? *(u8 **)(level + 0x40) : 0;
	quads = env ? *(u8 **)(env + 0xc) : 0;
	if (quads) {
		u32 n = *(u32 *)(quads + 0x14);
		/* without 0x20, which the camera sets on quads in its way (cleared by 0x41aab0) */
		for (u32 i = 0; i < n; i++) h = (h ^ (*(u32 *)(quads + 0x18 + i * 0x38 + 0x30) & ~0x20u)) * 0x01000193u;
	}
	ring = *(u8 **)0x5ece80 ? *(u8 **)(*(u8 **)0x5ece80 + 0x2f8) : 0;
	if (ring) {
		u32 n = *(u32 *)(ring + 0x14);
		h = (h ^ *(u32 *)(ring + 0x10)) * 0x01000193u;
		for (u32 i = 0; i < n && i < 256; i++)
			for (u32 k = 0; k < 6; k++)
				h = (h ^ *(u32 *)(ring + 0x18 + i * 0x44c + 0x434 + k * 4)) * 0x01000193u;
	}
	return h;
}

static u32 capture(RecTick *t, RecChar *c)
{
	u8 *chr = GS_CHARACTERS(ONgGameState);
	u32 slots = ONgCharacterSlots, n = 0;

	if (slots > MAX_CHARS) slots = MAX_CHARS;
	for (u32 i = 0; i < slots; i++, chr += CHR_SIZE) {
		u32 flags = *(u32 *)(chr + 4);
		if (!(flags & CHR_INUSE)) continue;
		c[n].index = i;
		c[n].flags = flags;
		c[n].health = *(int *)(chr + 0x38);
		memcpy(c[n].pos, chr + 0x148, 12);
		c[n].facing = *(float *)(chr + 0x16c);
		memcpy(c[n].vel, chr + 0x1c4, 12);
		n++;
	}
	t->tick = GS_TICK(ONgGameState);
	t->seed = UUgRandomSeed;
	t->n_chars = n;
	t->n_calls = g_ncalls < MAX_CALLS ? g_ncalls : MAX_CALLS;
	t->fpu_cw = g_fpu_cw;
	t->tick_local_seed = g_tick_local_seed;
	t->world = world_hash();
	return n;
}

static int alloc_images(void)
{
	if (!g_images) g_images = VirtualAlloc(0, MAX_CHARS * IMG_SIZE, 0x3000, 4);
	else memset(g_images, 0, MAX_CHARS * IMG_SIZE);
	return g_images != 0;
}

/* ---- Record ----------------------------------------------------------------------------- */

static void out_flush(void)
{
	u32 written;
	if (g_out != INVALID_HANDLE_VALUE && g_wlen) WriteFile(g_out, g_wbuf, g_wlen, &written, 0);
	g_wlen = 0;
}

static void out_write(const void *p, u32 n)
{
	if (g_wlen + n > sizeof g_wbuf) out_flush();
	if (n > sizeof g_wbuf) {
		u32 written;
		WriteFile(g_out, p, n, &written, 0);
		return;
	}
	memcpy(g_wbuf + g_wlen, p, n);
	g_wlen += n;
}

static void out_u32(u32 v) { out_write(&v, 4); }

static void record_tick(void)
{
	static RecChar chars[MAX_CHARS];
	RecTick t;
	u32 n = capture(&t, chars);

	out_u32(TAG_TICK);
	out_write(&t, sizeof t);
	out_write(chars, n * sizeof(RecChar));
	for (u32 i = 0; i < n; i++) {
		u32 *prev = g_images[chars[i].index], m = 0;
		image_of(chars[i].index, g_image_now);
		for (u32 k = 0; k < IMG_DWORDS; k++)
			if (g_image_now[k] != prev[k]) {
				g_changes[m].index = k;
				g_changes[m++].value = prev[k] = g_image_now[k];
			}
		out_u32(m);
		out_write(g_changes, m * sizeof(RecDword));
	}
	out_write(g_calls, t.n_calls * 4);
	g_ncalls = 0;
}

/* ---- Replay ----------------------------------------------------------------------------- */

static int load_replay(void)
{
	HANDLE f = CreateFileA(g_path, 0x80000000 /* GENERIC_READ */, 1, 0, 3 /* OPEN_EXISTING */, 0x80, 0);
	u32 got = 0;

	if (f == INVALID_HANDLE_VALUE) {
		log_msg("lockstep: can't open replay %s", g_path);
		return 0;
	}
	g_size = GetFileSize(f, 0);
	g_data = g_size ? VirtualAlloc(0, g_size, 0x3000 /* MEM_COMMIT|MEM_RESERVE */, 4 /* PAGE_READWRITE */) : 0;
	if (g_data) ReadFile(f, g_data, g_size, &got, 0);
	CloseHandle(f);
	if (!g_data || got != g_size || g_size < sizeof(RecHeader)) {
		log_msg("lockstep: can't read replay %s (%d bytes)", g_path, g_size);
		return 0;
	}
	RecHeader *h = (RecHeader *)g_data;
	if (h->magic != REC_MAGIC || h->version != REC_VERSION) {
		log_msg("lockstep: %s is not a version %d Arklite recording; record it again", g_path, REC_VERSION);
		return 0;
	}
	g_pos = sizeof *h;
	return 1;
}

static void *take_at(u32 *pos, u32 n)
{
	void *p;
	if (*pos + n > g_size) return 0;
	p = g_data + *pos;
	*pos += n;
	return p;
}

static void *take(u32 n) { return take_at(&g_pos, n); }

static u32 peek_tag(u32 pos)
{
	return pos + 4 <= g_size ? *(u32 *)(g_data + pos) : 0;
}

/* Skip one tick record at *pos without using it; 0 if the file ends inside it */
static int skip_tick(u32 *pos)
{
	RecTick *t;
	if (!take_at(pos, 4) || !(t = take_at(pos, sizeof *t)) || !take_at(pos, t->n_chars * sizeof(RecChar)))
		return 0;
	for (u32 i = 0; i < t->n_chars; i++) {
		u32 *m = take_at(pos, 4);
		if (!m || !take_at(pos, *m * sizeof(RecDword))) return 0;
	}
	return take_at(pos, t->n_calls * 4) != 0;
}

/* Ticks in the frame at g_pos (after its actions), or -1 if the file ends inside it */
static int count_ticks(void)
{
	u32 pos = g_pos;
	int n = 0;
	while (peek_tag(pos) == TAG_TICK) {
		if (!skip_tick(&pos)) return -1;
		n++;
	}
	return peek_tag(pos) == TAG_END ? n : -1;
}

/* Read one tick record, applying its changes to the recorded images */
typedef struct { const RecTick *t; const RecChar *c; const u32 *calls; } RecRead;

static int read_tick(RecRead *r)
{
	if (peek_tag(g_pos) != TAG_TICK) return 0;
	take(4);
	if (!(r->t = take(sizeof(RecTick))) || !(r->c = take(r->t->n_chars * sizeof(RecChar)))) return 0;
	for (u32 i = 0; i < r->t->n_chars; i++) {
		u32 *m = take(4);
		RecDword *d = m ? take(*m * sizeof(RecDword)) : 0;
		u32 *img = g_images[r->c[i].index];
		if (!d && (!m || *m)) return 0;
		for (u32 k = 0; k < *m; k++)
			if (d[k].index < IMG_DWORDS) img[d[k].index] = d[k].value;
	}
	return (r->calls = take(r->t->n_calls * 4)) != 0 || r->t->n_calls == 0;
}

static void log_char(const char *what, const RecChar *c)
{
	log_msg("    %s #%d %s: flags %08x health %d", what, c->index, char_name(c->index), c->flags, c->health);
}

/* Dwords of a character that differ from its recorded image (ignored ones left out) */
static u32 image_diffs(u32 index, int log)
{
	u32 *rec = g_images[index], n = 0;

	image_of(index, g_image_now);
	for (u32 k = 0; k < IMG_DWORDS; k++) {
		if (g_image_now[k] == rec[k] || g_ignore[k]) continue;
		if (log && n < 16) {
			u32 off = k * 4;
			const char *where = off < CHR_SIZE ? "character" : "active data";
			union { u32 u; float f; } a = { rec[k] }, b = { g_image_now[k] };
			if (off >= CHR_SIZE) off -= CHR_SIZE;
			if (a.f > -1e6f && a.f < 1e6f && b.f > -1e6f && b.f < 1e6f)
				log_msg("      %s +0x%04x: recorded %08x, now %08x (as floats x1000: %d, %d)",
				        where, off, a.u, b.u, (int)(a.f * 1000), (int)(b.f * 1000));
			else
				log_msg("      %s +0x%04x: recorded %08x, now %08x", where, off, a.u, b.u);
		}
		n++;
	}
	if (log && n > 16) log_msg("      ... and %d more dwords", n - 16);
	return n;
}

/* The RNG callers of this tick, from the first one that differs */
static void log_callers(const RecRead *r, const RecTick *t)
{
	u32 k = 0, nr = r->t->n_calls, nn = t->n_calls;
	while (k < nr && k < nn && r->calls[k] == g_calls[k]) k++;
	log_msg("  RNG calls since the last tick: recorded %d, now %d; same for the first %d. From there:", nr, nn, k);
	for (u32 i = k; i < k + 8 && (i < nr || i < nn); i++) {
		u32 a = i < nr ? r->calls[i] : 0, b = i < nn ? g_calls[i] : 0;
		log_msg("    %d: recorded %s%06x, now %s%06x", i,
		        a >> 31 ? "outside tick " : a >> 30 ? "local " : "", a & 0x3fffffff,
		        b >> 31 ? "outside tick " : b >> 30 ? "local " : "", b & 0x3fffffff);
	}
}

/* Compare a recorded tick with the game now: 0 identical, 1 only the RNGs/tick, 2 characters */
static int compare_state(const RecRead *r, const RecTick *t, const RecChar *now, int log)
{
	const RecTick *rt = r->t;
	const RecChar *rc = r->c;
	u32 n = t->n_chars, i = 0, j = 0, shown = 0;
	int chars_differ = rt->n_chars != n;
	int other_differ = rt->tick != t->tick || rt->seed != t->seed || rt->fpu_cw != t->fpu_cw ||
	                   rt->tick_local_seed != t->tick_local_seed || rt->world != t->world;

	if (log) {
		if (rt->tick != t->tick) log_msg("  game tick: recorded %d, now %d", rt->tick, t->tick);
		if (rt->fpu_cw != t->fpu_cw) log_msg("  FPU control word: recorded %04x, now %04x", rt->fpu_cw, t->fpu_cw);
		if (rt->seed != t->seed) log_msg("  RNG: recorded %08x, now %08x", rt->seed, t->seed);
		if (rt->tick_local_seed != t->tick_local_seed)
			log_msg("  tick local RNG: recorded %08x, now %08x", rt->tick_local_seed, t->tick_local_seed);
		if (rt->seed != t->seed || rt->tick_local_seed != t->tick_local_seed) log_callers(r, t);
		if (rt->world != t->world) log_msg("  collision world (quad flags, dead bodies): recorded %08x, now %08x", rt->world, t->world);
		if (chars_differ) log_msg("  characters: recorded %d, now %d", rt->n_chars, n);
	}
	while (i < rt->n_chars || j < n) {
		if (j >= n || (i < rt->n_chars && rc[i].index < now[j].index)) {
			chars_differ = 1;
			if (log && shown++ < 12) log_char("recorded only", &rc[i]);
			i++;
		} else if (i >= rt->n_chars || now[j].index < rc[i].index) {
			chars_differ = 1;
			if (log && shown++ < 12) log_char("now only", &now[j]);
			j++;
		} else {
			if (image_diffs(now[j].index, 0)) {
				chars_differ = 1;
				if (log && shown++ < 12) {
					log_msg("    #%d %s:", now[j].index, char_name(now[j].index));
					image_diffs(now[j].index, 1);
				}
			}
			i++, j++;
		}
	}
	return chars_differ ? 2 : other_differ ? 1 : 0;
}

static void replay_check_tick(void)
{
	static RecChar now[MAX_CHARS];
	RecTick t;
	RecRead r;
	int d;

	capture(&t, now);
	if (!read_tick(&r)) {
		log_msg("lockstep: frame %d ran more ticks than recorded", g_frames);
		g_ncalls = 0;
		return;
	}
	if ((d = compare_state(&r, &t, now, 0))) {
		if (!g_bad_ticks++) {
			g_first_bad_frame = g_frames;
			g_first_bad_tick = g_ticks;
		}
		if (d == 2 && !g_char_bad_ticks++) g_first_char_bad_tick = g_ticks;
		if (g_logged_diffs < DIFFS_LOGGED) {
			g_logged_diffs++;
			log_msg("lockstep: DESYNC at frame %d, tick %d (game tick %d)", g_frames, g_ticks, t.tick);
			compare_state(&r, &t, now, 1);
		}
	}
	g_ncalls = 0;
}

/* Dwords that differ before any tick aren't game state, such as pointers into memory
 * allocated elsewhere: leave them out of every comparison. */
static void replay_check_start(const RecRead *r, const RecTick *t, const RecChar *now)
{
	u32 n = 0;

	memset(g_ignore, 0, sizeof g_ignore);
	g_ignore[CHR_SPEECH_SOUND / 4] = 1;   /* a sound system handle */
	for (u32 i = 0; i < t->n_chars; i++) {
		image_of(now[i].index, g_image_now);
		for (u32 k = 0; k < IMG_DWORDS; k++)
			if (g_image_now[k] != g_images[now[i].index][k] && !g_ignore[k]) {
				g_ignore[k] = 1;
				if (n++ < 20)
					log_msg("lockstep: ignoring %s +0x%04x (differs at level start)",
					        k * 4 < CHR_SIZE ? "character" : "active data", k * 4 < CHR_SIZE ? k * 4 : k * 4 - CHR_SIZE);
			}
	}
	if (!compare_state(r, t, now, 0)) {
		log_msg("lockstep: level start state matches the recording (%d dwords ignored)", n);
	} else {
		log_msg("lockstep: level start state DIFFERS from the recording (before any tick, so not caused by the replay):");
		compare_state(r, t, now, 1);
	}
}

/* ---- Sessions --------------------------------------------------------------------------- */

static void session_end(const char *why)
{
	if (g_session != SESSION_RUN) return;
	g_session = SESSION_DONE;
	if (g_mode == MODE_RECORD) {
		out_flush();
		CloseHandle(g_out);
		g_out = INVALID_HANDLE_VALUE;
		log_msg("lockstep: recorded level %d, %d frames, %d ticks (%d s) to %s (%s)",
		        g_level, g_frames, g_ticks, g_ticks / 60, g_path, why);
	} else if (!g_bad_ticks) {
		log_msg("lockstep: replay of level %d MATCHED: %d frames, %d ticks (%d s), every tick identical (%s)",
		        g_level, g_frames, g_ticks, g_ticks / 60, why);
	} else {
		log_msg("lockstep: replay of level %d DESYNCED: %d of %d ticks differ, first at frame %d tick %d (%s)",
		        g_level, g_bad_ticks, g_ticks, g_first_bad_frame, g_first_bad_tick, why);
		if (g_char_bad_ticks)
			log_msg("lockstep: characters differ in %d ticks, first at tick %d", g_char_bad_ticks, g_first_char_bad_tick);
		else
			log_msg("lockstep: characters never differed, only the RNGs");
	}
	if (g_data) VirtualFree(g_data, 0, 0x8000 /* MEM_RELEASE */);
	g_data = 0;
}

static void session_begin(u32 level)
{
	u8 *gs = ONgGameState;
	RecHeader h = { REC_MAGIC, REC_VERSION, level, GS_TICK(gs), UUgRandomSeed, fpu_control_word(), g_tick_local_seed, 0 };

	g_level = level;
	g_frames = g_ticks = 0;
	g_session = SESSION_DONE;   /* until it has started */
	if (!alloc_images()) {
		log_msg("lockstep: out of memory");
		return;
	}
	g_ncalls = 0;
	g_fpu_cw = h.fpu_cw;
	if (g_mode == MODE_RECORD) {
		g_out = CreateFileA(g_path, 0x40000000, 1, 0, 2 /* CREATE_ALWAYS */, 0x80, 0);
		if (g_out == INVALID_HANDLE_VALUE) {
			log_msg("lockstep: can't create %s", g_path);
			return;
		}
		out_write(&h, sizeof h);
		record_tick();
		log_msg("lockstep: recording level %d from tick %d, RNG %08x, FPU %04x", level, h.start_tick, h.seed, h.fpu_cw);
	} else {
		static RecChar now[MAX_CHARS];
		RecTick t;
		RecRead r;
		if (!load_replay()) return;
		RecHeader *rh = (RecHeader *)g_data;
		if (rh->level != level) {
			log_msg("lockstep: replay is of level %d, this is level %d; not replaying", rh->level, level);
			return;
		}
		if (rh->fpu_cw != h.fpu_cw)
			log_msg("lockstep: FPU control word %04x, recorded with %04x (floats may round differently)", h.fpu_cw, rh->fpu_cw);
		if (rh->start_tick != h.start_tick)
			log_msg("lockstep: level starts at tick %d, recorded at %d", h.start_tick, rh->start_tick);
		UUgRandomSeed = rh->seed;
		g_tick_local_seed = rh->tick_local_seed;
		log_msg("lockstep: replaying %s on level %d, RNG %08x", g_path, level, rh->seed);
		capture(&t, now);
		if (!read_tick(&r)) {
			log_msg("lockstep: replay file is empty");
			return;
		}
		replay_check_start(&r, &t, now);
		g_ncalls = 0;
	}
	g_session = SESSION_RUN;
}

/* ---- Hooks ------------------------------------------------------------------------------ */

static void ONI_FC tick_hook(void *action)
{
	g_fpu_cw = fpu_control_word();
	g_in_tick = 1;
	players_tick_record(action);   /* Konoko's F2 choice */
	ONrGameState_Tick(action);
	g_in_tick = 0;
	net_tick_done();
	if (g_session != SESSION_RUN) return;
	if (g_mode == MODE_RECORD) record_tick();
	else replay_check_tick();
	g_ticks++;
	g_tick_in_frame++;
}

static u32 ONI_FC update_hook(u32 count, void *actions, u32 *ticks_run)
{
	u8 *gs = ONgGameState;
	u32 level = ONrLevel_GetCurrent(), err, ran = 0;

	const u32 *p2;

	char_menu_tag(count, actions);   /* this PC's F2 choice, in every record (a replay uses its own) */
	if (net_active()) return net_update(count, actions, ticks_run);
	if (g_mode != MODE_OFF) {
		if (g_session == SESSION_RUN && (level != g_level || GS_TICK(gs) < g_last_tick))
			session_end(level != g_level ? "level left" : "level restarted");
		if (g_session == SESSION_WAIT && level) session_begin(level);
	}
	if (g_mode == MODE_OFF || g_session != SESSION_RUN) {
		players_begin_frame();   /* player 2's gamepad */
		return ONrGameState_Update(count, actions, ticks_run);
	}

	count &= 0xffff;
	if (count > MAX_ACTIONS) count = MAX_ACTIONS;
	if (g_mode == MODE_RECORD) {
		/* before the update, which edits the records */
		p2 = players_begin_frame();
		RecFrame f = { (u16)count, p2 ? FRAME_P2 : 0 };
		out_write(&f, sizeof f);
		if (count) out_write(actions, count * ACTION_SIZE);
		if (p2) out_write(p2, ACTION_SIZE);
	} else {
		RecFrame *f = take(sizeof *f);
		void *a = f ? take(f->n_actions * ACTION_SIZE) : 0;
		p2 = f && (f->flags & FRAME_P2) ? take(ACTION_SIZE) : 0;
		int n = (f && (a || !f->n_actions) && (p2 || !(f->flags & FRAME_P2))) ? count_ticks() : -1;
		if (n < 0) {
			session_end("end of recording");
			players_begin_frame();
			return ONrGameState_Update(count, actions, ticks_run);
		}
		players_replay_frame(p2);   /* the recorded gamepad, not the live one */
		count = f->n_actions;
		memcpy(g_replay_actions, a, count * ACTION_SIZE);
		actions = g_replay_actions;
		g_rec_ticks = n;
		/* run the recorded number of ticks, whatever the clock says */
		GS_SERVER_TIME(gs) = GS_TICK(gs) + g_rec_ticks;
		GS_SYNC_BEHIND(gs) = 0;
	}

	g_tick_in_frame = 0;
	err = ONrGameState_Update(count, actions, &ran);
	if (ticks_run) *ticks_run = ran;
	g_last_tick = GS_TICK(gs);

	if (g_session == SESSION_RUN && g_mode == MODE_RECORD) {
		out_u32(TAG_END);
	} else if (g_session == SESSION_RUN) {
		if (g_tick_in_frame != g_rec_ticks && g_logged_diffs < DIFFS_LOGGED) {
			g_logged_diffs++;
			log_msg("lockstep: DESYNC at frame %d: ran %d ticks, recorded %d", g_frames, g_tick_in_frame, g_rec_ticks);
			if (!g_bad_ticks++) {
				g_first_bad_frame = g_frames;
				g_first_bad_tick = g_ticks;
			}
		}
		/* ticks recorded but not run: apply them, so the images stay in step */
		RecRead r;
		while (peek_tag(g_pos) == TAG_TICK && read_tick(&r)) {}
		take(4);   /* TAG_END */
	}
	g_frames++;
	return err;
}

void lockstep_install(void)
{
	u8 *u = (u8 *)ONcGameState_UpdateCall, *t = (u8 *)ONcGameState_TickCall;
	int tick_ok = t[0] == 0xe8 && ONcGameState_TickCall + 5 + *(u32 *)(t + 1) == (u32)ONrGameState_Tick;
	int update_ok = u[0] == 0xe8 && ONcGameState_UpdateCall + 5 + *(u32 *)(u + 1) == (u32)ONrGameState_Update;

	sound_rng_install();
	speech_install();
	visited_install();
	/* the tick hook tells the local RNG when a tick is running, so it's always in */
	if (tick_ok) {
		patch_call(ONcGameState_TickCall, (u32)ONrGameState_Tick, (u32)tick_hook);
		local_rng_install();
	} else {
		log_msg("local RNG: game tick call changed, skipped");
	}

	/* the update hook makes player 2's record every frame, so it's always in too */
	if (update_ok) patch_call(ONcGameState_UpdateCall, (u32)ONrGameState_Update, (u32)update_hook);
	else log_msg("lockstep: game update call changed, no player 2 and no record/replay");

	if (net_active()) {
		if (!tick_ok || !update_ok) log_msg("net: game loop changed, the game can't run networked");
		return;   /* no record/replay in a networked game */
	}
	if (GetEnvironmentVariableA("ARKLITE_RECORD", g_path, sizeof g_path)) g_mode = MODE_RECORD;
	else if (GetEnvironmentVariableA("ARKLITE_REPLAY", g_path, sizeof g_path)) g_mode = MODE_REPLAY;
	if (g_mode == MODE_OFF) return;
	if (!tick_ok || !update_ok) {
		log_msg("lockstep: game loop changed, %s disabled", g_mode == MODE_RECORD ? "recording" : "replay");
		g_mode = MODE_OFF;
		return;
	}
	if (!rng_trace_install()) log_msg("lockstep: UUrRandom changed, RNG callers not traced");
	g_trace = 1;
	log_msg("lockstep: %s %s; starts with the next level", g_mode == MODE_RECORD ? "recording to" : "replaying", g_path);
}

/* ---- Network recording (net.c) ------------------------------------------------------- *
 * Each PC records the networked game in the same format, so tools/akr_diff.py can compare the
 * two tick by tick. Frames carry FRAME_P2N: player 2's record for each of the frame's ticks.
 * Started right before the first tick, once the seeds are in place. */

int lockstep_net_record_begin(u32 level, const char *path)
{
	if (g_session == SESSION_RUN) return 1;
	if (!g_trace) {
		if (!rng_trace_install()) log_msg("lockstep: UUrRandom changed, RNG callers not traced");
		g_trace = 1;
	}
	str_cpy(g_path, sizeof g_path, path);
	g_mode = MODE_RECORD;
	session_begin(level);
	return g_session == SESSION_RUN;
}

void lockstep_net_record_frame(u32 n, const u32 (*p1)[12], const u32 (*p2)[12])
{
	RecFrame f = { (u16)n, FRAME_P2N };
	if (g_session != SESSION_RUN || g_mode != MODE_RECORD) return;
	out_write(&f, sizeof f);
	out_write(p1, n * ACTION_SIZE);
	out_write(p2, n * ACTION_SIZE);
	g_tick_in_frame = 0;
}

void lockstep_net_record_frame_end(void)
{
	if (g_session != SESSION_RUN || g_mode != MODE_RECORD) return;
	out_u32(TAG_END);
	g_frames++;
}

void lockstep_net_record_end(const char *why)
{
	if (g_mode == MODE_RECORD) session_end(why);
}

void lockstep_get_seeds(u32 *game, u32 *tick_local)
{
	*game = UUgRandomSeed;
	*tick_local = g_tick_local_seed;
}

void lockstep_set_seeds(u32 game, u32 tick_local)
{
	UUgRandomSeed = game;
	g_tick_local_seed = tick_local;
}

void lockstep_exit(void)
{
	session_end("game closed");
}
