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
 *     local RNG inside ticks. All 10 calls now draw from Arklite's own sound RNG, and a
 *     sound started in a tick picks from a seed made from that tick (sound seeds below).
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
 *   - The particle update (0x45c830: ECX time in ticks, DL 1 = the tick's classes) runs twice:
 *     in the tick for the classes without flag 1 (class +4), and once per frame after the
 *     frame's ticks (0x4fb1c7, DL 0) for those with it (Bungie's decorative ones), stepping
 *     them by however many ticks that frame ran. Some of them hurt characters (their damage
 *     goes through TakeDamage outside the tick), so at different frame rates the same hit did
 *     different damage (a network game: 1 point on the host at 166 fps, 3 on the joiner at
 *     60 fps). Every tick now runs that update for its own time, inside the tick (so it draws
 *     from the tick's RNG streams); the per-frame call then finds its time reached and does
 *     nothing.
 *   - Decorative particles whose effects only look (no damage, explosion, glass, class change or
 *     impact, and none of that in what they emit or leave behind) draw from this PC's own
 *     effects stream even inside a tick: whether one exists, and how many children it emits,
 *     isn't the same on every PC (particle quality disables classes and halves decal limits,
 *     particle memory fills differently, a glow follows a body part another PC may not have
 *     animated), and each draw it made from the tick's stream moved every later draw on that
 *     PC. Matches with Quasi (v0.4.0, 2026-11-10) desynced three times right as player 1's
 *     Daodan glow started (super particles: decorative, created and started in the character
 *     update), with every character identical and only the tick's effects seed apart. See
 *     "Decorative particles" below.
 *
 * While recording or replaying, both RNGs keep their callers: each tick stores the return
 * addresses of the calls since the last tick (bit 31: outside a tick, bit 30: tick local
 * stream), so a desync in the RNG names the code that drew differently. */
#include "arklite.h"

#define ONrGameState_Update ((u32 (ONI_FC *)(u32, void *, u32 *))0x4fafb0)
#define ONcGameState_UpdateCall 0x4d472b
#define ONrGameState_Tick   ((void (ONI_FC *)(void *))0x4fa6a0)
#define ONcGameState_TickCall 0x4fb133
#define P3rUpdate           ((void (ONI_FC *)(u32, u32))0x45c830)   /* time, DL all classes */
#define P3cFrameUpdateCall  0x4fb1c7                   /* per-frame update, DL 0 */
#define ONgGameState       (*(u8 **)0x5ece7c)
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
static int g_decor;   /* drawing for a decorative particle that only looks: this PC's stream (see below) */
static int g_trace;                       /* recording or replaying: keep RNG callers */
static u32 g_tick_local_seed TICK_STATE = 0xfded;   /* UUrLocalRandom inside ticks */
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
	if (!g_in_tick || g_decor) {   /* drawing, or a decorative particle: the original stream */
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

/* The sound RNG. Out of the ticks (ambient timing on the wall clock) it's one stream per PC.
 * A sound the game starts in a tick picks its sound from a seed both PCs agree on: a hash of
 * the tick, the sound's place among the tick's sound starts and the game RNG seed then (a
 * character's taunt or speech line sounded different on each PC before this). A stream can't
 * do it: the pick only runs when the PC's sound card has a free channel, a rollback's re-run
 * ticks don't play one-shot sounds, and speech lines (SS2 ambients) pick in the sound update
 * (0x480c20), when the sound card is ready.
 *   - one-shot sounds (impulses, 0x480440): the start sets the tick stream's seed
 *   - ambients (0x4821f0): the start gives its play id (*0x5eb70c + 1) a seed; the ambient's
 *     state machine (0x480c20, ECX the ambient, 5 calls) draws from it */
#define SSiImpulse_DriverRead   0x480443   /* mov al,[0x5eb757]: the impulse player's first test */
#define SSiAmbient_DriverRead   0x4821f3   /* the same in the ambient start */
#define SSiImpulse_PlayDriverRead 0x480560 /* and in the group player every one-shot sound starts in */
#define SSgDriverOn             (*(u8 *)0x5eb757)
#define SSgAmbientPlayID        (*(u32 *)0x5eb70c)   /* the last play id given */

__declspec(dllimport) u32 WINAPI GetCurrentThreadId(void);

static u32 g_sound_seed = 0xfded;
static u32 g_tick_tid;            /* the thread running the tick */
static u32 g_tick_sounds;         /* sounds started so far in this tick */
static u32 g_tick_sound_seed;
static u32 g_ambient_seed[256];   /* by play id */
static u8 *g_ambient;             /* the ambient whose state machine runs, and its thread */
static u32 g_ambient_tid;
static u8 g_impulses_muted;       /* rollback's re-run ticks (rollback_impulses) */

static int sound_in_tick(void)
{
	return g_in_tick && GetCurrentThreadId() == g_tick_tid;
}

/* live_menu.c: whether this is a tick's code (on the thread running it) */
int lockstep_in_tick(void)
{
	return sound_in_tick();
}

static u32 tick_sound_seed(void)
{
	u32 h = GS_TICK(ONgGameState) * 0x9e3779b1u ^ g_tick_sounds * 0x85ebca77u ^ UUgRandomSeed;
	h ^= h >> 16; h *= 0x7feb352du;
	h ^= h >> 15; h *= 0x846ca68bu;
	return h ^ h >> 16;
}

__attribute__((no_caller_saved_registers)) static u32 sound_random(void)
{
	u32 tid = GetCurrentThreadId(), *s = &g_sound_seed;
	if (g_ambient && g_ambient_tid == tid) s = &g_ambient_seed[*(u32 *)g_ambient & 0xff];
	else if (g_in_tick && tid == g_tick_tid) s = &g_tick_sound_seed;
	*s = *s * 0x19660d + 0x3c6ef35f;
	return *s >> 16;
}

static int g_play_hooked;          /* impulse_play decides which one-shot sounds re-run ticks play */

static u32 impulse_start(void)
{
	if (sound_in_tick()) {
		g_tick_sounds++;
		g_tick_sound_seed = tick_sound_seed();
	}
	return g_impulses_muted && !g_play_hooked ? 0 : SSgDriverOn;
}

/* One-shot sounds in rollback's re-run ticks. Every one-shot sound starts in 0x480560 (ECX the
 * sound group, 8th argument the impulse when 0x480440 calls it). Muting them all while
 * ticks re-ran lost every sound only the corrected run has: the other player's input arrives
 * a few ticks late, so their taunt, shot or hit first runs in a re-run tick (the Mercury Bow's
 * shot, the taunt heard only when the guess "still held" was right). Now each tick remembers
 * the sounds its runs started (group and impulse, in order), and a re-run plays a sound only
 * when it starts more of it than the tick's runs before did: a little late, not twice. Which
 * sounds play is never game state (the sound card decides as much), so the PCs stay in sync. */
#define PLAYED_TICKS 64
#define PLAYED_MAX   32
typedef struct { u32 tick, n, sig[PLAYED_MAX]; } Played;
static Played g_played[PLAYED_TICKS];
static u32 g_play_tick;              /* the tick running, and the sounds this run of it started */
static u32 g_run_sig[PLAYED_MAX], g_run_n;

static void played_tick_begin(u32 tick)
{
	Played *p = &g_played[tick % PLAYED_TICKS];
	g_play_tick = tick;
	g_run_n = 0;
	if (!g_impulses_muted || p->tick != tick) {   /* the tick's first run */
		p->tick = tick;
		p->n = 0;
	}
}

static u32 impulse_play(u32 group, u32 impulse)
{
	Played *p = &g_played[g_play_tick % PLAYED_TICKS];
	u32 sig = group * 0x9e3779b1u ^ impulse, before = 0, played = 0;
	if (!SSgDriverOn) return 0;
	if (!sound_in_tick()) return !g_impulses_muted;
	for (u32 k = 0; k < g_run_n; k++) before += g_run_sig[k] == sig;
	if (g_run_n < PLAYED_MAX) g_run_sig[g_run_n++] = sig;
	for (u32 k = 0; k < p->n; k++) played += p->sig[k] == sig;
	if (played > before) return 0;   /* an earlier run of the tick played it */
	if (p->n < PLAYED_MAX) p->sig[p->n++] = sig;
	return 1;
}

/* In place of 0x480560's mov al,[0x5eb757]: ECX, EDX kept */
__attribute__((naked)) static void impulse_play_stub(void)
{
	__asm__ volatile(
		"pushl %%ecx\n\t"
		"pushl %%edx\n\t"
		"pushl 0x2c(%%esp)\n\t"   /* the impulse (8th argument, past our and the caller's returns) */
		"pushl %%ecx\n\t"         /* the sound group */
		"call %c0\n\t"
		"addl $8, %%esp\n\t"
		"popl %%edx\n\t"
		"popl %%ecx\n\t"
		"ret\n" :: "i"(impulse_play));
}

static u32 ambient_start(void)
{
	u32 id = SSgAmbientPlayID + 1, seed;
	if (id == 0xffffffffu) id = 0;   /* as 0x4821f0 counts */
	if (sound_in_tick()) {
		g_tick_sounds++;
		seed = tick_sound_seed();
	} else {
		seed = g_sound_seed = g_sound_seed * 0x19660d + 0x3c6ef35f;
	}
	g_ambient_seed[id & 0xff] = seed;
	return SSgDriverOn;
}

/* In place of the 5-byte mov al,[0x5eb757]: AL as it would read, every other register kept */
#define DRIVER_READ_STUB(name, fn) \
	__attribute__((naked)) static void name(void) \
	{ \
		__asm__ volatile( \
			"pushl %%ecx\n\t" \
			"pushl %%edx\n\t" \
			"call %c0\n\t" \
			"popl %%edx\n\t" \
			"popl %%ecx\n\t" \
			"ret\n" :: "i"(fn)); \
	}

DRIVER_READ_STUB(impulse_start_stub, impulse_start)
DRIVER_READ_STUB(ambient_start_stub, ambient_start)

/* What the 5 calls went to before (0x480c20, or volume.c's wrapper of it, installed first) */
static u32 (ONI_FC *g_ambient_next)(u8 *, u32);

static u32 ONI_FC ambient_update(u8 *ambient, u32 edx)
{
	u8 *was = g_ambient;
	u32 was_tid = g_ambient_tid, r;
	g_ambient_tid = GetCurrentThreadId();
	g_ambient = ambient;
	r = g_ambient_next(ambient, edx);
	g_ambient = was;
	g_ambient_tid = was_tid;
	return r;
}

/* 1: impulse_play decides per sound, nothing more to patch */
int lockstep_impulses_mute(int muted)
{
	g_impulses_muted = (u8)muted;
	return g_play_hooked;
}

static void sound_seeds_install(void)
{
	static const u8 read[5] = { 0xa0, 0x57, 0xb7, 0x5e, 0x00 };
	static const u32 updates[] = { 0x480c04, 0x481dfb, 0x48237d, 0x48379d, 0x483dc7 };
	const u32 n = sizeof updates / sizeof updates[0];
	u8 b[5] = { 0xe8 };
	u32 rel;

	u32 next = 0;

	/* volume.c wraps the same 5 calls (one wrapper for all); chain to whatever they call */
	for (u32 i = 0; i < n; i++) {
		const u8 *p = (const u8 *)updates[i];
		u32 to = updates[i] + 5 + *(u32 *)(p + 1);
		if (p[0] != 0xe8 || (next && to != next)) {
			log_msg("sound seeds: 0x%x changed, skipped (sounds pick differently on each PC)", updates[i]);
			return;
		}
		next = to;
	}
	g_ambient_next = (u32 (ONI_FC *)(u8 *, u32))next;
	if (!code_matches(SSiImpulse_DriverRead, read, 5) || !code_matches(SSiAmbient_DriverRead, read, 5)) {
		log_msg("sound seeds: 0x%x/0x%x changed, skipped (sounds pick differently on each PC)",
		        SSiImpulse_DriverRead, SSiAmbient_DriverRead);
		return;
	}
	rel = (u32)impulse_start_stub - (SSiImpulse_DriverRead + 5);
	memcpy(b + 1, &rel, 4);
	patch_write(SSiImpulse_DriverRead, b, 5);
	rel = (u32)ambient_start_stub - (SSiAmbient_DriverRead + 5);
	memcpy(b + 1, &rel, 4);
	patch_write(SSiAmbient_DriverRead, b, 5);
	for (u32 i = 0; i < n; i++) patch_call(updates[i], next, (u32)ambient_update);
	log_msg("sound seeds: sounds started in a tick pick the same on every PC");
	if (code_matches(SSiImpulse_PlayDriverRead, read, 5)) {
		rel = (u32)impulse_play_stub - (SSiImpulse_PlayDriverRead + 5);
		memcpy(b + 1, &rel, 4);
		patch_write(SSiImpulse_PlayDriverRead, b, 5);
		g_play_hooked = 1;
		log_msg("sound seeds: rollback's re-run ticks play the one-shot sounds their first run didn't");
	}
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

/* Every tick starts with the three vectors empty. Oni leaves a query's marks set and clears them
 * at the start of the next query, from the undo list; a set bit the list doesn't hold is never
 * cleared and hides its quad from every later query. Rollback made such bits: the vectors are
 * written inside ticks and between them (the camera's and drawing's queries, each PC its own), and
 * a restore puts back only the words ticks alone change, so the bits and their undo list could
 * come back from different moments. The joiner of the bot test of 2026-11-08 gathered 4 to 39
 * of them over a session (the host none), its queries lost walls, and its player 2 went through
 * a wall the host's stopped at: 4 wall contacts (active +0xb0) on the host, 0 on the joiner, with
 * the same inputs and seeds. Wiped at the tick body's start (also the re-runs and the rollback
 * check's), every PC starts each tick's queries from the same empty vectors: what the next query's
 * own clear would leave, so the game plays as before. */
static void visited_wipe(void)
{
	u32 *vs[3] = { *(u32 **)0x5ccebc, *(u32 **)0x5ccec0, *(u32 **)0x5ccec4 };
	for (int i = 0; i < 3; i++) {
		u32 *v = vs[i];
		if (!v) continue;
		for (int k = 0; k < 8; k++)
			if (g_visited[k].v == v) {
				memset((u8 *)v[0], 0, (g_visited[k].bits + 7) / 8);
				g_visited[k].overflowed = 0;
				v[2] = 0;
			}
	}
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
	log_msg("collision: visited marks are always undone, and wiped at every tick's start (walls no longer go missing)");
}

/* ---- Two-sided quads ------------------------------------------------------------------- *
 * Collision makes a second, flipped contact for a two-sided quad, one with any of the flags
 * 0x2a0 (AKcGQ_Flag_2Sided: Transparent 0x80, DrawBothSides 0x200 and Jello 0x20). But Jello is
 * the camera's own mark: each frame it sets 0x20 on the quads between it and the character it
 * follows (to draw them see-through) and the next camera update clears them again (0x41aab0,
 * from 0x4d5ca0, which also runs after every tick). So the walls near the character each PC's
 * camera follows were two-sided on that PC only: in a network game one player's wall contacts
 * (active +0xb0) came out different on the two PCs from identical state, 5 on one and 10 on
 * the other, and the player ended up a little elsewhere (the out-of-syncs where only one
 * player's position was off, by a few float steps up to 0.4 units). The four tests (wall
 * contacts for characters 0x4ebf81, for objects 0x43b9ef, and the other two collision paths
 * 0x443c3b, 0x49ed15) leave Jello out. Level data never keeps Jello: the camera's clear takes it
 * off any quad it drew. Found with two bot-played instances (net.c's tick-by-tick dump and a
 * temporary log of each move step's contacts before Oni's filter 0x43a9a0). */
static void two_sided_install(void)
{
	static const struct { u32 at; u8 len; u8 code[3]; } sites[] = {
		{ 0x4ebf81, 3, { 0xf7, 0x42, 0x30 } },   /* test [edx+0x30], 0x2a0 */
		{ 0x43b9ef, 2, { 0xf7, 0xc7 } },         /* test edi, 0x2a0 */
		{ 0x443c3b, 3, { 0xf7, 0x47, 0x30 } },   /* test [edi+0x30], 0x2a0 */
		{ 0x49ed15, 3, { 0xf7, 0x40, 0x30 } },   /* test [eax+0x30], 0x2a0 */
	};
	const u32 n = sizeof sites / sizeof sites[0];
	for (u32 i = 0; i < n; i++)
		if (!code_matches(sites[i].at, sites[i].code, sites[i].len) || *(const u32 *)(sites[i].at + sites[i].len) != 0x2a0) {
			log_msg("collision: two-sided test at 0x%x changed, skipped (the camera's see-through walls stay two-sided)", sites[i].at);
			return;
		}
	for (u32 i = 0; i < n; i++) patch_byte(sites[i].at + sites[i].len, 0xa0, 0x80);
	log_msg("collision: walls the camera shows see-through aren't two-sided (same walls on every PC)");
}

/* ---- Collision query log ---------------------------------------------------------------- *
 * While recording, every level collision query made in a tick (0x441cb0: ECX the level's
 * collision, EDX the sphere (x, y, z, radius), [the move], [quad flags to skip], [most
 * results]; ret 0xc; results: count *0x6a906c (u16), 0x48 bytes each from 0x6a9080, the quad
 * at +0x28 and the contact point at +0x1c) goes to <recording>.col with its input, its result
 * and the visited marks left over in the three vectors (set bits the undo list doesn't hold,
 * which would hide quads from it). tools/col_diff.py compares two PCs' logs and prints the
 * first query that differs. A character's wall contacts (active +0xb0) come from these
 * queries in its environment callback (call at 0x4ebddf).
 *
 * Its contacts then go through the physics move step 0x43a710 (ECX the physics context, EDX the
 * move): the environment callback (context's callbacks +0x148, +4) and the object callback
 * (+8) fill 0x28-byte contacts (+0 kind: 0 level, 1 object; +8 the plane; +0x18 the point;
 * +0x24 the squared distance), 0x43a9a0 drops some (unless context +0x128 has 0x100), the
 * sort at 0x43a80b (qsort 0x51e8b1) orders them by distance, and unless context +0x128 has
 * 0x800 the first contact whose push (0x433da0) is at least 1/1024 moves the character and makes
 * the physics query again. For a character's context (callbacks 0x5ec918) each sort also goes
 * to the log: the context's 0x150 bytes, the move and the sorted contacts. Records start with
 * a tag: COL_TAG_QUERY or COL_TAG_STEP. */
#define AKrCollision_Sphere     ((u8 (ONI_FC *)(void *, const float *, const float *, u32, u32))0x441cb0)
#define AKgResultCount          (*(u16 *)0x6a906c)
#define AKgResults              ((const u8 *)0x6a9080)
#define COL_MAX_RESULTS         32
#define COL_TAG_QUERY           0x59525551   /* 'QURY' */
#define COL_TAG_STEP            0x50455453   /* 'STEP' */
#define PHcStep_SortCall        0x43a80b
#define UUrQsort                0x51e8b1
#define PHgContexts             (*(u8 **)0x5cb520)   /* u32 count, then contexts from +8 */
#define PH_CONTEXT_SIZE         0x150
#define PH_CONTACT_SIZE         0x28
#define ONcCharacterCallbacks   0x5ec918

typedef struct { u32 tick; u16 site; u16 count; float sphere[4], move[3]; u32 mask, max, stray[3]; } ColQuery;
typedef struct { u32 quad; float point[3]; } ColResult;
typedef struct { u32 tick, context, count; float move[3]; u8 data[PH_CONTEXT_SIZE]; } ColStep;

static HANDLE g_col_out = INVALID_HANDLE_VALUE;
static u8 g_col_buf[0x10000];
static u32 g_col_len, g_col_queries, g_col_stray_queries, g_col_stray_max;
static const u32 g_col_sites[] = { 0x43b8d0, 0x44f2e7, 0x4661ab, 0x466351, 0x49ec55, 0x4d6d75, 0x4e2524, 0x4ebddf, 0x4ec240 };

static void col_flush(void)
{
	u32 written;
	if (g_col_out != INVALID_HANDLE_VALUE && g_col_len) WriteFile(g_col_out, g_col_buf, g_col_len, &written, 0);
	g_col_len = 0;
}

static void col_write(const void *p, u32 n)
{
	if (g_col_len + n > sizeof g_col_buf) col_flush();
	memcpy(g_col_buf + g_col_len, p, n);
	g_col_len += n;
}

/* Set bits in a visited vector that its undo list doesn't hold (after an overflow there are
 * some, but the next clear wipes the vector whole, so they don't count) */
static u32 visited_stray(u32 *v)
{
	u32 set = 0, bits = 0;
	if (!v) return 0;
	for (int k = 0; k < 8; k++)
		if (g_visited[k].v == v) {
			if (g_visited[k].overflowed) return 0;
			bits = g_visited[k].bits;
		}
	for (u32 i = 0; i < (bits + 7) / 8; i++)
		for (u8 b = ((u8 *)v[0])[i]; b; b &= b - 1) set++;
	return set > v[2] ? set - v[2] : 0;
}

static u8 ONI_FC col_query(void *collision, const float *sphere, const float *move, u32 mask, u32 max)
{
	u32 from = (u32)__builtin_return_address(0) - 5;
	int logging = g_in_tick && g_session == SESSION_RUN && g_col_out != INVALID_HANDLE_VALUE;
	ColQuery q;

	if (logging) {
		q.stray[0] = visited_stray(*(u32 **)0x5ccebc);
		q.stray[1] = visited_stray(*(u32 **)0x5ccec0);
		q.stray[2] = visited_stray(*(u32 **)0x5ccec4);
	}
	u8 hit = AKrCollision_Sphere(collision, sphere, move, mask, max);
	if (!logging) return hit;

	q.tick = GS_TICK(ONgGameState);
	q.site = 0xffff;
	for (u16 k = 0; k < sizeof g_col_sites / sizeof g_col_sites[0]; k++)
		if (g_col_sites[k] == from) q.site = k;
	q.count = hit ? AKgResultCount : 0;
	memcpy(q.sphere, sphere, sizeof q.sphere);
	memcpy(q.move, move, sizeof q.move);
	q.mask = mask;
	q.max = max & 0xffff;
	u32 tag = COL_TAG_QUERY;
	col_write(&tag, 4);
	col_write(&q, sizeof q);
	for (u32 i = 0; i < q.count && i < COL_MAX_RESULTS; i++) {
		ColResult r;
		r.quad = *(const u32 *)(AKgResults + i * 0x48 + 0x28);
		memcpy(r.point, AKgResults + i * 0x48 + 0x1c, sizeof r.point);
		col_write(&r, sizeof r);
	}
	g_col_queries++;
	u32 stray = q.stray[0] + q.stray[1] + q.stray[2];
	if (stray) {
		g_col_stray_queries++;
		if (stray > g_col_stray_max) g_col_stray_max = stray;
	}
	return hit;
}

/* After the move step's sort (0x43a80b), for a character's context */
static void __cdecl col_step(const u8 *context, const float *move, const u8 *contacts, u32 count)
{
	ColStep s;
	u8 *all = PHgContexts;
	u32 tag = COL_TAG_STEP;

	if (!g_in_tick || g_session != SESSION_RUN || g_col_out == INVALID_HANDLE_VALUE) return;
	if (*(const u32 *)(context + 0x148) != ONcCharacterCallbacks) return;
	s.tick = GS_TICK(ONgGameState);
	s.context = all && context >= all + 8 ? (u32)(context - (all + 8)) / PH_CONTEXT_SIZE : 0xffffffffu;
	s.count = count & 0xffff;
	memcpy(s.move, move, sizeof s.move);
	memcpy(s.data, context, PH_CONTEXT_SIZE);
	col_write(&tag, 4);
	col_write(&s, sizeof s);
	col_write(contacts, (s.count < COL_MAX_RESULTS ? s.count : COL_MAX_RESULTS) * PH_CONTACT_SIZE);
}

/* In place of the cdecl call qsort(contacts, count, 0x28, compare): EDI the context, EBP the move */
__attribute__((naked)) static void step_sort(void)
{
	__asm__ volatile(
		"pushl 16(%%esp)\n\t"
		"pushl 16(%%esp)\n\t"
		"pushl 16(%%esp)\n\t"
		"pushl 16(%%esp)\n\t"
		"movl $%c0, %%eax\n\t"
		"call *%%eax\n\t"
		"addl $16, %%esp\n\t"
		"pushl 8(%%esp)\n\t"   /* count */
		"pushl 8(%%esp)\n\t"   /* contacts */
		"pushl %%ebp\n\t"
		"pushl %%edi\n\t"
		"call %c1\n\t"
		"addl $16, %%esp\n\t"
		"ret\n" :: "i"(UUrQsort), "i"(col_step));   /* EAX is free: qsort returns nothing */
}

static void col_log_begin(void)
{
	char path[MAX_PATH];
	u32 n = 0;

	while (g_path[n]) n++;
	str_cpy(path, sizeof path, g_path);
	if (n > 4 && str_ieq_n(g_path + n - 4, ".akr", 4)) path[n - 4] = 0;
	if (n + 4 < sizeof path) {
		u32 m = 0;
		while (path[m]) m++;
		str_cpy(path + m, sizeof path - m, ".col");
	}
	g_col_len = g_col_queries = g_col_stray_queries = g_col_stray_max = 0;
	g_col_out = CreateFileA(path, 0x40000000, 1, 0, 2 /* CREATE_ALWAYS */, 0x80, 0);
	if (g_col_out == INVALID_HANDLE_VALUE) log_msg("lockstep: can't create %s", path);
	else log_msg("lockstep: collision queries to %s", path);
}

static void col_log_end(void)
{
	if (g_col_out == INVALID_HANDLE_VALUE) return;
	col_flush();
	CloseHandle(g_col_out);
	g_col_out = INVALID_HANDLE_VALUE;
	log_msg("lockstep: %d collision queries in ticks logged; %d of them started with leftover visited marks (most %d)",
	        g_col_queries, g_col_stray_queries, g_col_stray_max);
}

static void col_install(void)
{
	const u32 n = sizeof g_col_sites / sizeof g_col_sites[0];
	for (u32 i = 0; i < n; i++) {
		const u8 *p = (const u8 *)g_col_sites[i];
		if (p[0] != 0xe8 || g_col_sites[i] + 5 + *(u32 *)(p + 1) != (u32)AKrCollision_Sphere) {
			log_msg("collision: query call 0x%x changed, queries not logged", g_col_sites[i]);
			return;
		}
	}
	for (u32 i = 0; i < n; i++) patch_call(g_col_sites[i], (u32)AKrCollision_Sphere, (u32)col_query);
	if (!patch_call(PHcStep_SortCall, UUrQsort, (u32)step_sort))
		log_msg("collision: move step sort 0x%x changed, move steps not logged", PHcStep_SortCall);
}

/* ---- Decorative particles -------------------------------------------------------------- *
 * Bungie's particle classes with flag 1 (definition +4) are decorative: "not necessary for
 * gameplay". Whether one exists isn't the same on every PC: a class whose quality level is above
 * this PC's particle quality can't be made at all (P3rLevelBegin sets class +0x40 bit 0x40),
 * lower quality halves or quarters the decal limits, particle memory runs out differently, and
 * the character code checks its glows still exist before sending them events (Oni_Character.c:
 * self_ref against the reference it kept). What such a particle draws from the effects RNG
 * inside a tick goes to this PC's own stream, so the tick's stream (in the state hash) only
 * moves for what every PC runs. Not for every decorative class: some hurt characters
 * (env_fire4_p01, pistol_shot, w9_scr_f01...), and their random values (damage bell curves,
 * where they fly) must be the same everywhere. A class draws from this PC's stream when it is
 * decorative and none of its actions is ChangeClass (16), DamageChar (26), DamageBlast (27),
 * Explode (28), DamageEnvironment (29), GlassCharge (30), SuperballTrigger (77) or ImpactEffect
 * (60), and every class it can make (its emitters' classes, +0x300, 0x1bc each, class at +0x40;
 * CollisionEffect (55) and CreateDecal (58) classes, action +4) passes too. Stock level0: 627
 * of 733 decorative classes, the Daodan glows (super*, murosuper*) among them.
 *
 * Where the particle code draws for a class, it runs inside one of these, wrapped at every call
 * (Oni.exe; the class decides, and a nested call for another class decides again):
 *   0x455e40 P3rCreateParticle (ECX class, EDX time), 13 calls
 *   0x456030 P3rCreateEffect (ECX spec: +0 the class, EDX data), 10 calls
 *   0x45b2c0 P3rSendEvent (ECX class, EDX particle, event, time), 21 calls
 *   0x45b170 P3iPerformActionList (ECX class, EDX particle, event, list, t, dt, collision): every
 *            event's actions, a particle's death list (0x456ce0) included, 17 calls
 *   0x45d610 P3iEmitParticle (ECX parent class, EDX parent, emitter, index, time): decided by
 *            the emitted class, whose draws (chance, burst, place, speed) come first, 2 calls
 *   0x45cc70 P3iUpdateParticleClass (ECX class, EDX time), the update loop's 2 calls
 * and the texture frame draws Oni's own code makes right after making a particle (class flag
 * 0x40000), with the class or its definition in a register: 0x453a98 (ECX definition), 0x4cd76c
 * (EDX definition), 0x5184a4 (ECX definition), 0x50a30e (ESI class). */
#define P3cClasses            0x65c520
#define P3cClassSize          0x98
#define P3cMaxClasses         0x800
#define P3_DEF(c)             (*(u8 **)((c) + 0x48))
#define P3_DEF_DECORATIVE(d)  (*(u32 *)((d) + 4) & 1)
#define P3_NUM_ACTIONS(d)     (*(u16 *)((d) + 0x12))
#define P3_NUM_EMITTERS(d)    (*(u16 *)((d) + 0x14))
#define P3_ACTIONS(d)         (*(u8 **)((d) + 0x2fc))
#define P3_EMITTERS(d)        (*(u8 **)((d) + 0x300))
#define P3cActionSize         0x1a8
#define P3cEmitterSize        0x1bc
#define P3cActionDisabled     0x100
#define P3rCreateParticle_    0x455e40
#define P3rCreateEffect_      0x456030
#define P3rSendEvent_         0x45b2c0
#define P3iPerformActionList_ 0x45b170
#define P3iEmitParticle_      0x45d610
#define P3iUpdateParticleClass_ 0x45cc70

static const u32 g_create_calls[] = { 0x4538cd, 0x453f6b, 0x4542bf, 0x45479a, 0x454e05, 0x455208, 0x4557a5,
	0x456088, 0x45e24f, 0x465044, 0x4cd635, 0x50a0b2, 0x518377 };
static const u32 g_effect_calls[] = { 0x453d17, 0x453f33, 0x454285, 0x454762, 0x454dae, 0x455769, 0x465a9d,
	0x4e9772, 0x4fe7ad, 0x509771 };
static const u32 g_event_calls[] = { 0x453918, 0x453fc1, 0x454312, 0x4547e1, 0x454e4c, 0x455258, 0x4557ee,
	0x4cd546, 0x4cd80a, 0x4cd817, 0x4df853, 0x4dfa6e, 0x4dfae4, 0x4e85ec, 0x4eaf24, 0x50a380, 0x50a6b2, 0x50aa4b,
	0x50aba0, 0x518546, 0x51a0eb };
static const u32 g_actions_calls[] = { 0x456ba0, 0x456cfa, 0x45ac86, 0x45b2f7, 0x45d29f, 0x45d312, 0x45d447,
	0x45e909, 0x462840, 0x465398, 0x465502, 0x4663b2, 0x4663d2, 0x467532, 0x46778c, 0x467f4e, 0x46800b };
static const u32 g_emit_calls[] = { 0x45d3a5, 0x464f69 };
static const u32 g_update_calls[] = { 0x45ca5f, 0x45cc48 };

/* per class index: the definition it was worked out for, and 1 looks only, 2 not, 3 working on it */
static u8 *g_decor_def[P3cMaxClasses];
static u8 g_decor_kind[P3cMaxClasses];
static u32 g_decor_classes, g_decor_looks;

static int decor_looks_only(const u8 *cls, int depth);

static int decor_makes_ok(const u8 *cls, int depth)
{
	return !cls || decor_looks_only(cls, depth + 1);
}

/* 1: a decorative class whose particles only look, and so does everything they make */
static int decor_looks_only(const u8 *cls, int depth)
{
	u32 i;
	u8 *d;
	int ok;

	if (!cls || (u32)cls < P3cClasses || (u32)cls >= P3cClasses + P3cMaxClasses * P3cClassSize ||
	    ((u32)cls - P3cClasses) % P3cClassSize || !(d = P3_DEF(cls)))
		return 0;
	i = ((u32)cls - P3cClasses) / P3cClassSize;
	if (g_decor_def[i] == d && g_decor_kind[i] != 3) return g_decor_kind[i] == 1;
	if (g_decor_def[i] == d) return 0;   /* makes itself again higher up: counted as not only looking */
	if (depth > 16 || !P3_DEF_DECORATIVE(d)) {
		g_decor_def[i] = d;
		g_decor_kind[i] = 2;
		return 0;
	}
	g_decor_def[i] = d;
	g_decor_kind[i] = 3;
	ok = 1;
	for (u32 k = 0; ok && P3_ACTIONS(d) && k < P3_NUM_ACTIONS(d); k++) {
		const u8 *a = P3_ACTIONS(d) + k * P3cActionSize;
		u32 type = *(const u32 *)a;
		if (type & P3cActionDisabled) continue;
		switch (type) {
		case 16: case 26: case 27: case 28: case 29: case 30: case 60: case 77:
			ok = 0;
			break;
		case 55: case 58:   /* CollisionEffect, CreateDecal: the class they make */
			ok = decor_makes_ok(*(u8 *const *)(a + 4), depth);
			break;
		}
	}
	for (u32 k = 0; ok && P3_EMITTERS(d) && k < P3_NUM_EMITTERS(d); k++)
		ok = decor_makes_ok(*(u8 *const *)(P3_EMITTERS(d) + k * P3cEmitterSize + 0x40), depth);
	g_decor_kind[i] = ok ? 1 : 2;
	g_decor_classes++;
	g_decor_looks += ok;
	return ok;
}

/* The wrappers: the class decides while the call runs, then the caller's choice again */
#define DECOR_CALL(cls, call) do { int was_ = g_decor; g_decor = decor_looks_only((cls), 0); call; g_decor = was_; } while (0)

static u8 *ONI_FC decor_create_particle(u8 *cls, u32 time)
{
	u8 *r;
	DECOR_CALL(cls, r = ((u8 *(ONI_FC *)(u8 *, u32))P3rCreateParticle_)(cls, time));
	return r;
}

static u8 *ONI_FC decor_create_effect(u8 **spec, void *data)
{
	u8 *r;
	DECOR_CALL(spec ? spec[0] : 0, r = ((u8 *(ONI_FC *)(u8 **, void *))P3rCreateEffect_)(spec, data));
	return r;
}

static u32 ONI_FC decor_send_event(u8 *cls, u8 *particle, u32 event, u32 time)
{
	u32 r;
	DECOR_CALL(cls, r = ((u32 (ONI_FC *)(u8 *, u8 *, u32, u32))P3rSendEvent_)(cls, particle, event, time));
	return r;
}

static u32 ONI_FC decor_actions(u8 *cls, u8 *particle, u32 event, u32 list, u32 t, u32 dt, u32 collision)
{
	u32 r;
	DECOR_CALL(cls, r = ((u32 (ONI_FC *)(u8 *, u8 *, u32, u32, u32, u32, u32))P3iPerformActionList_)(
		cls, particle, event, list, t, dt, collision));
	return r;
}

static u32 ONI_FC decor_emit(u8 *cls, u8 *particle, u8 *emitter, u32 index, u32 time)
{
	u32 r;
	u8 *child = emitter ? *(u8 **)(emitter + 0x40) : 0;
	DECOR_CALL(child ? child : cls, r = ((u32 (ONI_FC *)(u8 *, u8 *, u8 *, u32, u32))P3iEmitParticle_)(
		cls, particle, emitter, index, time));
	return r;
}

static void ONI_FC decor_update_class(u8 *cls, u32 time)
{
	DECOR_CALL(cls, ((void (ONI_FC *)(u8 *, u32))P3iUpdateParticleClass_)(cls, time));
}

/* For Arklite's own calls (items.c, mutators.c): the same choice */
u8 *ONI_FC lockstep_p3_create_particle(u8 *cls, u32 time) { return decor_create_particle(cls, time); }
u8 *ONI_FC lockstep_p3_create_effect(void *spec, void *data) { return decor_create_effect((u8 **)spec, data); }
u8 ONI_FC lockstep_p3_send_event(u8 *cls, u8 *particle, u32 event, float time)
{
	union { float f; u32 u; } t = { time };
	return (u8)decor_send_event(cls, particle, event, t.u);
}

/* The texture frame draws after a particle is made: the class or its definition in a register */
__attribute__((no_caller_saved_registers, used)) u32 lockstep_local_random_def(const u8 *def)
{
	int was = g_decor, looks = 0;
	for (u32 i = 0; i < P3cMaxClasses; i++)   /* the class whose definition it is */
		if (g_decor_def[i] == def) { looks = g_decor_kind[i] == 1; break; }
	g_decor = looks;
	u32 r = local_random();
	g_decor = was;
	return r;
}

__attribute__((no_caller_saved_registers, used)) u32 lockstep_local_random_class(const u8 *cls)
{
	int was = g_decor;
	g_decor = decor_looks_only(cls, 0);
	u32 r = local_random();
	g_decor = was;
	return r;
}

__attribute__((naked)) static void decor_random_ecx_def(void)
{
	__asm__ volatile("pushl %ecx\n\tcall _lockstep_local_random_def\n\taddl $4, %esp\n\tret");
}

__attribute__((naked)) static void decor_random_edx_def(void)
{
	__asm__ volatile("pushl %edx\n\tcall _lockstep_local_random_def\n\taddl $4, %esp\n\tret");
}

__attribute__((naked)) static void decor_random_esi_class(void)
{
	__asm__ volatile("pushl %esi\n\tcall _lockstep_local_random_class\n\taddl $4, %esp\n\tret");
}

static u32 decor_patch(const u32 *sites, u32 n, u32 target, void *with)
{
	u32 done = 0;
	for (u32 i = 0; i < n; i++) {
		const u8 *p = (const u8 *)sites[i];
		if (p[0] != 0xe8 || sites[i] + 5 + *(const u32 *)(p + 1) != target) return 0;   /* checked before any patch */
	}
	for (u32 i = 0; i < n; i++) done += patch_call(sites[i], target, (u32)with);
	return done;
}

static void decor_install(void)
{
#define N(a) (sizeof a / sizeof a[0])
	static const u32 def_ecx[] = { 0x453a98, 0x5184a4 }, def_edx[] = { 0x4cd76c }, cls_esi[] = { 0x50a30e };
	u32 want = N(g_create_calls) + N(g_effect_calls) + N(g_event_calls) + N(g_actions_calls) + N(g_emit_calls) +
	           N(g_update_calls) + 4, got = 0;
	const u32 *all[] = { g_create_calls, g_effect_calls, g_event_calls, g_actions_calls, g_emit_calls, g_update_calls,
	                     def_ecx, def_edx, cls_esi };
	const u32 counts[] = { N(g_create_calls), N(g_effect_calls), N(g_event_calls), N(g_actions_calls), N(g_emit_calls),
	                       N(g_update_calls), N(def_ecx), N(def_edx), N(cls_esi) };
	const u32 targets[] = { P3rCreateParticle_, P3rCreateEffect_, P3rSendEvent_, P3iPerformActionList_, P3iEmitParticle_,
	                        P3iUpdateParticleClass_, UUrLocalRandom, UUrLocalRandom, UUrLocalRandom };
	char v[8];
	if (GetEnvironmentVariableA("ARKLITE_DECOR_FIX", v, sizeof v) && v[0] == '0') {
		log_msg("decorative particles: left as Oni draws them (ARKLITE_DECOR_FIX=0)");
		return;
	}
	void *const with[] = { (void *)decor_create_particle, (void *)decor_create_effect, (void *)decor_send_event,
	                       (void *)decor_actions, (void *)decor_emit, (void *)decor_update_class,
	                       (void *)decor_random_ecx_def, (void *)decor_random_edx_def, (void *)decor_random_esi_class };
	for (u32 k = 0; k < N(all); k++) {   /* all or nothing: half of them would split a class's draws */
		for (u32 i = 0; i < counts[k]; i++) {
			const u8 *p = (const u8 *)all[k][i];
			if (p[0] != 0xe8 || all[k][i] + 5 + *(const u32 *)(p + 1) != targets[k]) {
				log_msg("decorative particles: call 0x%x changed, left as they were (they draw from the tick's stream)", all[k][i]);
				return;
			}
		}
	}
	for (u32 k = 0; k < N(all); k++) got += decor_patch(all[k], counts[k], targets[k], with[k]);
	log_msg("decorative particles: those that only look draw from this PC's effects stream (%u of %u calls)", got, want);
#undef N
}

/* Logged once a level: how many classes were worked out so far, and how many only look */
void lockstep_decor_level_log(void)
{
	if (g_decor_classes) log_msg("decorative particles: %u classes used so far, %u of them only look", g_decor_classes, g_decor_looks);
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

/* ---- Per-frame particles ---------------------------------------------------------------- */

static int g_frame_particles;   /* the tick hook runs the per-frame particle update */

static void frame_particles_install(void)
{
	/* mov eax,[0x5ece7c]; xor dl,dl; mov ecx,[eax+0x144]; call 0x45c830 */
	static const u8 site[] = { 0xa1, 0x7c, 0xce, 0x5e, 0x00, 0x32, 0xd2, 0x8b, 0x88, 0x44, 0x01, 0x00, 0x00, 0xe8 };
	const u32 at = P3cFrameUpdateCall - 13;

	if (!code_matches(at, site, sizeof site) || P3cFrameUpdateCall + 5 + *(u32 *)(P3cFrameUpdateCall + 1) != (u32)P3rUpdate) {
		log_msg("particles: 0x%x changed, decorative particles still step per frame", P3cFrameUpdateCall);
		return;
	}
	g_frame_particles = 1;
	log_msg("particles: decorative particles step once per tick, not per frame");
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

/* ---- SS2's extra unlock ------------------------------------------------------------------ *
 * A channel starting a compressed sound (0x484b70) opens an ACM stream for it; when
 * acmFormatSuggest, acmStreamOpen or acmStreamSize fails it calls LeaveCriticalSection on SS2's
 * lock (*0x63f394, at 0x484cbb) and returns 0, though it never entered it: its callers
 * (0x47ff50, 0x480560, through 0x47f8d0) hold the lock and leave it themselves. The first leave
 * frees the lock early; if the sound thread (0x4864e0, which holds the lock while it mixes)
 * takes it then, the caller's own leave frees it under the mixer, and the main thread can close
 * a channel's stream while the sound thread converts with it: the Linux host's crashes in
 * acmStreamConvert (0x48500c, a call to 0, 0x273 or 0xd006c), every time on the sound thread.
 * The extra leave now goes to channel_open_failed, which logs the sound's format instead. */
#define SSiChannel_FailLeave   0x484cba   /* push eax; call [LeaveCriticalSection] */

static void channel_open_failed(const u8 *channel)
{
	static volatile u32 n;
	const u8 *sound = *(const u8 * const *)(channel + 0x24);
	if (n++ >= 8) return;
	log_msg("sound: a channel couldn't open an ACM stream for format %d (%d channels, %d Hz, block %d); the sound doesn't play, SS2's lock is kept",
	        *(const u16 *)(sound + 4), *(const u16 *)(sound + 6), *(const u32 *)(sound + 8), *(const u16 *)(sound + 0x10));
}

/* called as LeaveCriticalSection(lock) (stdcall), EBP the channel */
__attribute__((naked)) static void channel_open_failed_stub(void)
{
	__asm__ volatile(
		"pushl %%ebp\n\t"
		"call %c0\n\t"
		"addl $4, %%esp\n\t"
		"ret $4\n" :: "i"(channel_open_failed));
}

static void *const g_channel_open_failed = (void *)channel_open_failed_stub;

static void channel_unlock_install(void)
{
	static const u8 site[] = { 0x50, 0xff, 0x15, 0xec, 0x90, 0x52, 0x00 };
	void *const *to = &g_channel_open_failed;
	if (!code_matches(SSiChannel_FailLeave, site, sizeof site)) {
		log_msg("sound: 0x%x changed, a failed ACM open still frees SS2's lock", SSiChannel_FailLeave);
		return;
	}
	patch_write(SSiChannel_FailLeave + 3, &to, 4);
	log_msg("sound: a failed ACM open no longer frees SS2's lock under the sound thread");
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
		col_log_end();
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
	RecHeader h = { REC_MAGIC, REC_VERSION, level, GS_TICK(gs), UUgRandomSeed, fpu_control_word(), g_tick_local_seed,
	                players_spawn_seed() };

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
		col_log_begin();
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

/* One tick of the game, with its input record (rollback.c may run it twice) */
static void tick_body(void *action)
{
	visited_wipe();                /* the same empty visited marks on every PC (see visited_wipe) */
	players_tick_record(action);   /* Konoko's F2 choice */
	g_tick_tid = GetCurrentThreadId();
	g_tick_sounds = 0;
	g_tick_sound_seed = tick_sound_seed();
	played_tick_begin(GS_TICK(ONgGameState));
	ONrGameState_Tick(action);
	mutators_tick();   /* bombs going off: the scorch, the body (mutators.c) */
	if (g_frame_particles) P3rUpdate(GS_TICK(ONgGameState) + 1, 0);   /* as the tick's own (0x4fa9bd) */
}

static void ONI_FC tick_hook(void *action)
{
	rollback_tick_begin();
	g_fpu_cw = fpu_control_word();
	g_in_tick = 1;
	if (!rollback_test_tick(action, tick_body)) tick_body(action);
	rollback_tick_end();
	g_in_tick = 0;
	net_tick_done();
	if (g_session != SESSION_RUN) return;
	if (g_mode == MODE_RECORD) record_tick();
	else replay_check_tick();
	g_ticks++;
	g_tick_in_frame++;
}

int lockstep_recording(void) { return g_mode != MODE_OFF; }

static u32 ONI_FC update_hook(u32 count, void *actions, u32 *ticks_run)
{
	u8 *gs = ONgGameState;
	u32 level = ONrLevel_GetCurrent(), err, ran = 0;

	const u32 *p2;

	rollback_frame();
	char_menu_tag(count, actions);   /* this PC's F2 choice, in every record (a replay uses its own) */
	if (photo_update(count, actions, ticks_run, &err)) return err;   /* single player only */
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
	sound_seeds_install();
	speech_install();
	channel_unlock_install();
	visited_install();
	two_sided_install();
	col_install();
	/* the tick hook tells the local RNG when a tick is running, so it's always in */
	if (tick_ok) {
		patch_call(ONcGameState_TickCall, (u32)ONrGameState_Tick, (u32)tick_hook);
		local_rng_install();
		decor_install();
		frame_particles_install();
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

/* The level loads before the replay starts (session_begin, its first update), and its spawns
 * are picked then: from the recording's header (reserved: the spawn seed; 0 in recordings made
 * before it, the pick they were made with) */
int lockstep_replay_spawn_seed(u32 *seed)
{
	RecHeader h;
	u32 got = 0;
	HANDLE f;

	if (g_mode != MODE_REPLAY || g_session != SESSION_WAIT) return 0;
	f = CreateFileA(g_path, 0x80000000 /* GENERIC_READ */, 1, 0, 3 /* OPEN_EXISTING */, 0x80, 0);
	if (f == INVALID_HANDLE_VALUE) return 0;
	ReadFile(f, &h, sizeof h, &got, 0);
	CloseHandle(f);
	if (got != sizeof h || h.magic != REC_MAGIC || h.version != REC_VERSION) return 0;
	*seed = h.reserved;
	return 1;
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
