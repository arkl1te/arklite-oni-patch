/* Animated vertex lighting for custom levels: corners of the level's quads that flicker or pulse
 * between their baked color and a second color.
 *
 * The engine shades the environment with the per-corner colors of the AKEV's quad table (AGQG),
 * read every frame, so the only stock way to change them is BSL env_shade (0x4bd070). That sets
 * one color for all four corners of every quad in a script id range, once. Catwalk instead
 * writes <level folder>\vertexlight.bin: one record per animation of a corner, by quad index, and
 * this file rewrites those corners every frame from the game clock (frozen while the game is
 * paused). A corner can have several records in a row (its own animation and one per animated
 * light that reaches it, version 2): each moves the baked color toward its own second color, and
 * the moves add up. Version 3 adds how far toward the second color it goes (min..max) and, for a
 * flicker, how often it works normally: a malfunctioning light (stability, fault length).
 * Version 4 adds, after the records, the lights' sounds: an impulse played when a flicker's
 * failure starts, or at each pulse, worked out from the same clock and noise as the corners, so
 * the sound lands on the flash and every PC plays it on the same tick. This runs outside the
 * ticks, like chat's sounds (chat.c): it draws from this PC's own sound RNG and can't desync.
 * Pitch and volume come from a private copy of the impulse and its group; the sounds stay the
 * game's. Version 6 adds a seed to the records and sounds: flickers with the same timing but
 * different seeds follow different patterns.
 *
 * Version 7 adds the id of a Sound object to the sounds. A point or spot light's ambient is a
 * Sound object (SNDG sphere) Build adds to the level, which stock Oni plays nonstop; a sound record
 * naming it makes it play only while its flicker fails. update_sphere wraps the play list's call
 * of the sphere update (0x515120, once a tick): outside a failure the sphere counts as out of
 * range, its ambient cut at once (its out sound still plays); when a failure starts, the sphere
 * starts it as usual, at full volume at once instead of fading in over 0.25 s, so it lands on the
 * flash.
 *
 * Version 8 adds the light's id, and with it the ambient's pitch range: before each start the
 * Sound object's ambient (OSD + 0x50, which Oni only looks up by name while it's null) points at a
 * private copy of the ambient and its five groups, whose group pitch is the game's times one picked
 * from SoundPitch..SoundPitchMax. A channel takes its pitch (permutation x group) when a sound
 * starts on it, so the whole play, in sound to out sound, keeps that pitch. The pick comes from
 * the flicker's run (or step) and the light's id, not an RNG: every PC, and Catwalk's preview,
 * picks the same.
 *
 * In memory (offsets of env_shade and the door lighting, 0x507080):
 *   game state + 0x150 = ONLV, ONLV + 0x40 = AKEV, AKEV + 0x0c = AGQG
 *   AGQG + 0x14 = quad count, + 0x18 = quads, 0x38 bytes each:
 *     +0x00 point indices[4], +0x10 texcoord indices[4], +0x20 colors[4] (0xAARRGGBB), +0x30 flags
 *   flag 0x40: triangle (3 corners) */
#include "arklite.h"

__declspec(dllimport) int    WINAPI ReadFile(HANDLE, void *, u32, u32 *, void *);
__declspec(dllimport) int    WINAPI CloseHandle(HANDLE);
__declspec(dllimport) u32    WINAPI GetFileSize(HANDLE, u32 *);
__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) int    WINAPI VirtualFree(void *, u32, u32);
void *memcpy(void *, const void *, unsigned);

#define ONrLevel_GetCurrent ((u16 (ONI_FC *)(void))0x5054c0)
#define ONgGameState        (*(u8 **)0x5ece7c)
#define GS_TICK             0x144      /* u32, 60 per second */
#define GS_LEVEL            0x150      /* ONLV instance */
#define ONLV_AKEV           0x40
#define AKEV_AGQG           0x0c
#define AGQG_COUNT          0x14
#define AGQG_QUADS          0x18
#define QUAD_SIZE           0x38
#define QUAD_COLORS         0x20
#define QUAD_FLAGS          0x30
#define QUAD_TRIANGLE       0x40

#define VL_MAGIC   0x4c565743u   /* "CWVL" */
#define VL_VERSION 8   /* 1: one record per corner; 2: several; 3: min, max, stability, fault;
                          4: light sounds after the records; 5: their pitch range; 6: seeds;
                          7: Sound objects the sounds gate; 8: light ids (ambient pitch) */
#define VL_V2_SIZE 24  /* bytes per record up to version 2 */
#define VL_V5_SIZE ((u32)sizeof(Record) - 4)   /* versions 3 to 5 */

enum { MODE_PULSE = 1, MODE_FLICKER = 2 };
enum { INTERP_SMOOTH, INTERP_LINEAR, INTERP_STEP };

typedef struct {
	u32   quad;
	u8    corner, mode, interp, pad;
	u32   a, b;          /* baked color, the other color: 0xAARRGGBB */
	float period;        /* seconds: one pulse, or one flicker step */
	float offset;        /* seconds added to the clock */
	float lo, hi;        /* how far toward b it goes, 0..1 (version 3) */
	float stability;     /* flicker: share of the time at a, 0 = flickers all the time (version 3) */
	float fault;         /* ...in runs of this many seconds, at least one period (version 3) */
	u32   seed;          /* flicker: picks the random pattern (version 6) */
} Record;

#define SSrImpulse_GetByName ((u8 *(ONI_FC *)(const char *))0x4801f0)
#define SSrImpulse_Play      ((void (ONI_FC *)(u8 *, const float *, u32, u32, u32))0x480440)
#define IMPULSE_SIZE         0x8c      /* SStImpulse */
#define IMPULSE_GROUP        0x20
#define IMPULSE_ALT_COUNT    0x5c      /* alt_threshold: a copy at the limit would play unpitched */
#define IMPULSE_ALT          0x60
#define IMPULSE_PRIORITY     0x44
#define PRIORITY_HIGH        2         /* SStPriority2: a flash's sound shouldn't lose its channel */
#define GROUP_SIZE           0x34      /* SStGroup */
#define GROUP_VOLUME         0x20
#define GROUP_PITCH          0x24
#define MAX_SOUNDS           64
#define REWIND_TICKS         120       /* the clock went back further: the level restarted */

typedef struct {     /* as in the file */
	u32   mode;
	float period, offset, stability, fault;
	float pitch, volume;
	float pos[3];
	u32   positional;
	char  sound[32];
	float pitch_max;   /* version 5: each play picks from pitch to this */
	u32   seed;        /* version 6 */
	u32   object;      /* version 7: id of the Sound object (SNDG) playing the light's ambient,
	                      which plays only while the flicker fails; 0 = play the impulse sound */
	u32   light;       /* version 8: the light's object id, which picks an ambient's pitches */
} SoundRecord;

#define SOUND_V4_SIZE ((u32)sizeof(SoundRecord) - 16)
#define SOUND_V5_SIZE ((u32)sizeof(SoundRecord) - 12)
#define SOUND_V6_SIZE ((u32)sizeof(SoundRecord) - 8)
#define SOUND_V7_SIZE ((u32)sizeof(SoundRecord) - 4)

/* Oni's Sound objects (Oni_Sound2.c OSiPlayList_Update_Spheres, OT_Sound.c) */
#define OSiPlayList_Update_Spheres ((void (ONI_FC *)(u8 *, const float *, const float *))0x515120)
#define OScPlayList_SphereCall     0x51505f   /* in OSrUpdate's play list loop (0x514f20) */
#define OBJrSound_GetAmbient       ((u8 *(ONI_FC *)(u8 *))0x4ca3a0)
#define SSrAmbient_Stop            ((void (ONI_FC *)(u32))0x4823c0)
#define SSrAmbient_SetVolume       ((void (ONI_FC *)(u32, float, float))0x482000)
#define SOUND_AMBIENT 0x50   /* Sound object: its SStAmbient (OSD + 0x24), looked up by name while 0 */
#define AMBIENT_SIZE  0xfc   /* SStAmbient (allocated at 0x481e25) */
#define AMBIENT_GROUPS 0x44  /* detail, base track 1, base track 2, in sound, out sound */
#define AMBIENT_GROUP_COUNT 5
#define PO_OBJECT     0x00   /* OStPlayingObject (0x3c bytes): the Sound object */
#define PO_PLAY_ID    0x04   /* its ambient's play, -1 for none */
#define PO_VOLUME     0x08   /* the volume the sphere update worked out */
#define OBJ_ID        0x04   /* OBJtObject object_id: the id in the level's object list */
#define AMBIENT_FLAGS 0x24   /* SStAmbient flags (byte); 1 = InterruptTracksOnStop */
#define AMBIENT_INTERRUPT 0x01
#define NO_PLAY       0xffffffffu

typedef struct {
	SoundRecord r;
	int   step;      /* newest pulse or run seen */
	int   started;
	int   state;     /* 0: impulse not looked up, 1: ready, -1: the game has none */
	float group_pitch;   /* the game group's own */
	u8    group[GROUP_SIZE], impulse[IMPULSE_SIZE];
	/* an ambient with a pitch: the copy its Sound object plays, its groups, their own pitches */
	u8    ambient[AMBIENT_SIZE], amb_group[AMBIENT_GROUP_COUNT][GROUP_SIZE];
	float amb_pitch[AMBIENT_GROUP_COUNT];
} Sound;

static Sound   g_sounds[MAX_SOUNDS];
static u32     g_sound_count, g_sound_tick;
static Record *g_recs;
static u32     g_count, g_alloc;
static u32     g_level;            /* level the records are for */
static u8     *g_agqg;             /* its quad table */

static u8 *level_agqg(void)
{
	u8 *gs = ONgGameState, *onlv, *akev;
	if (!gs || !(onlv = *(u8 **)(gs + char_limit_gs_offset(GS_LEVEL)))) return 0;
	if (!(akev = *(u8 **)(onlv + ONLV_AKEV))) return 0;
	return *(u8 **)(akev + AKEV_AGQG);
}

static void drop(void)
{
	if (g_recs) VirtualFree(g_recs, 0, 0x8000 /* MEM_RELEASE */);
	g_recs = 0;
	g_count = g_alloc = 0;
	g_sound_count = 0;
}

static void load(u32 level, u8 *agqg)
{
	const char *dir = custom_game_level_dir(level);
	char path[MAX_PATH];
	u32 size, got = 0, head[3], quads = *(u32 *)(agqg + AGQG_COUNT), kept = 0, rsize, bytes;
	HANDLE f;

	drop();
	g_level = level;
	g_agqg = agqg;
	if (!dir) return;
	fmt(path, "%svertexlight.bin", dir);
	f = CreateFileA(path, 0x80000000 /* GENERIC_READ */, 1, 0, 3 /* OPEN_EXISTING */, 0x80, 0);
	if (f == INVALID_HANDLE_VALUE) return;
	size = GetFileSize(f, 0);
	if (size < sizeof head || !ReadFile(f, head, sizeof head, &got, 0) || got != sizeof head ||
	    head[0] != VL_MAGIC || head[1] < 1 || head[1] > VL_VERSION ||
	    head[2] > (size - sizeof head) / (rsize = head[1] >= 6 ? sizeof(Record) : head[1] >= 3 ? VL_V5_SIZE : VL_V2_SIZE)) {
		log_msg("vertex light: %s isn't a version 1-%d file, ignored", path, VL_VERSION);
		CloseHandle(f);
		return;
	}
	if (head[2]) g_recs = VirtualAlloc(0, head[2] * sizeof(Record), 0x3000, 4);
	/* Older records are read into the end of the buffer and spread out from the front: record i
	 * never lands past where record i + 1 starts. */
	bytes = head[2] * rsize;
	if (g_recs && (!ReadFile(f, (u8 *)g_recs + head[2] * sizeof(Record) - bytes, bytes, &got, 0) || got != bytes)) {
		log_msg("vertex light: can't read %s", path);
		drop();
	}
	if (head[1] >= 4) {
		u32 n = 0;
		if (ReadFile(f, &n, sizeof n, &got, 0) && got == sizeof n)
			for (u32 i = 0; i < n && i < MAX_SOUNDS; i++) {
				Sound *s = &g_sounds[g_sound_count];
				u32 want = head[1] >= 8 ? sizeof s->r : head[1] == 7 ? SOUND_V7_SIZE :
				           head[1] == 6 ? SOUND_V6_SIZE : head[1] == 5 ? SOUND_V5_SIZE : SOUND_V4_SIZE;
				if (!ReadFile(f, &s->r, want, &got, 0) || got != want) break;
				if (head[1] < 5) s->r.pitch_max = s->r.pitch;
				if (head[1] < 6) s->r.seed = 0;
				if (head[1] < 7) s->r.object = 0;
				if (head[1] < 8) s->r.light = 0;
				s->r.sound[sizeof s->r.sound - 1] = 0;
				s->started = s->state = 0;
				g_sound_count++;
			}
		if (n) {
			u32 gated = 0;
			for (u32 i = 0; i < g_sound_count; i++) gated += g_sounds[i].r.object != 0;
			log_msg("vertex light: %d light sound(s) of %d, %d of them flickering ambients",
			        g_sound_count, n, gated);
		}
	}
	CloseHandle(f);
	if (!g_recs) return;
	if (rsize != sizeof(Record)) {
		const u8 *src = (u8 *)g_recs + head[2] * sizeof(Record) - bytes;
		for (u32 i = 0; i < head[2]; i++) {
			Record r;
			memcpy(&r, src + i * rsize, rsize);
			if (head[1] < 3) { r.lo = 0.0f; r.hi = 1.0f; r.stability = 0.0f; r.fault = 0.0f; }
			r.seed = 0;
			g_recs[i] = r;
		}
	}
	/* Keep only corners this level has (a stale file from another build of it). */
	for (u32 i = 0; i < head[2]; i++) {
		Record r = g_recs[i];
		if (r.quad >= quads || r.corner > 3 || !(r.period > 0.0f)) continue;
		if (r.corner == 3 && (*(u32 *)(agqg + AGQG_QUADS + r.quad * QUAD_SIZE + QUAD_FLAGS) & QUAD_TRIANGLE)) continue;
		g_recs[kept++] = r;
	}
	g_count = kept;
	log_msg("vertex light: %d animated corner(s) of %d (%d quads)", kept, head[2], quads);
}

static int floor_i(float x) { int i = (int)x; return (float)i > x ? i - 1 : i; }

/* 0..1, the same for the same step of the same flicker */
static float noise(u32 seed, int step)
{
	u32 h = seed ^ ((u32)step * 0x9e3779b1u);
	h ^= h >> 16; h *= 0x7feb352du;
	h ^= h >> 15; h *= 0x846ca68bu;
	h ^= h >> 16;
	return (float)(h >> 8) * (1.0f / 16777215.0f);
}

static float shape(float f, u8 interp)
{
	if (interp == INTERP_STEP) return f < 0.5f ? 0.0f : 1.0f;
	if (interp == INTERP_SMOOTH) return f * f * (3.0f - 2.0f * f);
	return f;
}

static u32 flicker_seed(float period, float offset, u32 seed)
{
	union { float f; u32 u; } p = { period }, o = { offset };
	return p.u * 31u + o.u + seed;
}

/* The run of steps step k is in */
static int flicker_run(int k, float period, float fault)
{
	float run = fault > period ? fault : period;
	return floor_i((float)k * period / run);
}

static int run_works(u32 seed, int run, float stability)
{
	return noise(seed ^ 0x5bd1e995u, run) < stability;
}

/* A flicker's level at step k: 0 while the light works (its run of steps came up working, by
 * stability), else a random one between lo and hi */
static float flicker_level(const Record *r, u32 seed, int k)
{
	if (r->stability > 0.0f && run_works(seed, flicker_run(k, r->period, r->fault), r->stability))
		return 0.0f;
	return r->lo + (r->hi - r->lo) * noise(seed, k);
}

/* How far toward the second color, 0..1 */
static float amount(const Record *r, float seconds)
{
	float x = (seconds + r->offset) / r->period, f;
	int k = floor_i(x);

	f = x - (float)k;
	if (r->mode == MODE_PULSE) {
		/* there and back once per period between lo and hi; Step blinks */
		float t = r->interp == INTERP_STEP ? (f < 0.5f ? 0.0f : 1.0f)
		                                   : shape(f < 0.5f ? 2.0f * f : 2.0f - 2.0f * f, r->interp);
		return r->lo + (r->hi - r->lo) * t;
	}
	/* flicker: a random level per step, corners with the same timing in step with each other */
	u32 seed = flicker_seed(r->period, r->offset, r->seed);
	float v0 = flicker_level(r, seed, k);
	if (r->interp == INTERP_STEP) return v0;
	return v0 + (flicker_level(r, seed, k + 1) - v0) * shape(f, r->interp);
}

/* Adds t of the way from a to b to sum (per channel: alpha, red, green, blue) */
static void toward(float sum[4], u32 a, u32 b, float t)
{
	for (int s = 0; s < 4; s++) {
		float ca = (float)(a >> (8 * s) & 0xff), cb = (float)(b >> (8 * s) & 0xff);
		sum[s] += (cb - ca) * t;
	}
}

static u32 pack(const float c[4])
{
	u32 r = 0;
	for (int s = 0; s < 4; s++) {
		int v = (int)(c[s] + 0.5f);
		r |= (u32)(v < 0 ? 0 : v > 255 ? 255 : v) << (8 * s);
	}
	return r;
}

static int sound_ready(Sound *s)
{
	if (!s->state) {
		u8 *src = SSrImpulse_GetByName(s->r.sound), *group = src ? *(u8 **)(src + IMPULSE_GROUP) : 0;
		s->state = -1;
		if (group) {
			memcpy(s->impulse, src, IMPULSE_SIZE);
			memcpy(s->group, group, GROUP_SIZE);
			*(float *)(s->group + GROUP_VOLUME) *= s->r.volume;
			s->group_pitch = *(float *)(s->group + GROUP_PITCH);
			*(u8 **)(s->impulse + IMPULSE_GROUP) = s->group;
			*(u32 *)(s->impulse + IMPULSE_PRIORITY) = PRIORITY_HIGH;
			*(u32 *)(s->impulse + IMPULSE_ALT_COUNT) = 0;
			*(u8 **)(s->impulse + IMPULSE_ALT) = 0;
			s->state = 1;
		} else log_msg("vertex light: no impulse sound %s", s->r.sound);
	}
	return s->state > 0;
}

/* Plays the sounds whose failure or pulse has come. Each step counts once however the clock
 * moves: a rollback's rewound tick plays nothing again. */
static void sounds_frame(u32 tick, float seconds)
{
	if (tick + REWIND_TICKS < g_sound_tick)
		for (u32 i = 0; i < g_sound_count; i++) g_sounds[i].started = 0;
	g_sound_tick = tick;
	for (u32 i = 0; i < g_sound_count; i++) {
		Sound *s = &g_sounds[i];
		const SoundRecord *r = &s->r;
		int k = floor_i((seconds + r->offset) / r->period), step = k, due = 1;

		if (r->object) continue;   /* an ambient's Sound object: update_sphere */
		if (r->mode == MODE_FLICKER) {
			u32 seed = flicker_seed(r->period, r->offset, r->seed);
			step = flicker_run(k, r->period, r->fault);
			/* no stability: it never starts failing, it just flickers */
			due = r->stability > 0.0f && !run_works(seed, step, r->stability) &&
			      run_works(seed, step - 1, r->stability);
		}
		if (s->started && step <= s->step) continue;
		if (s->started && due && sound_ready(s)) {
			/* from the step and the sound's place in the file, not an RNG: every PC picks the same */
			float t = noise(flicker_seed(r->period, r->offset, r->seed) ^ (0x2f0b4a87u * (i + 1)), step);
			*(float *)(s->group + GROUP_PITCH) = s->group_pitch * (r->pitch + (r->pitch_max - r->pitch) * t);
			SSrImpulse_Play(s->impulse, r->positional ? r->pos : 0, 0, 0, 0);
		}
		s->step = step;
		s->started = 1;
	}
}

/* Whether a light's ambient plays now: while its flicker fails (Stability 0 fails all the time,
 * and a pulse never does). Catwalk's geometry.flickerFailing is the same. */
static int sound_gate_open(const SoundRecord *r, float seconds)
{
	if (r->mode != MODE_FLICKER || !(r->stability > 0.0f)) return 1;
	return !run_works(flicker_seed(r->period, r->offset, r->seed),
	                  flicker_run(floor_i((seconds + r->offset) / r->period), r->period, r->fault),
	                  r->stability);
}

static u8 *current_level(void);

/* The pitch a light's ambient starts at now: from SoundPitch to SoundPitchMax, by the flicker's
 * run (or the step) and the light. Catwalk's geometry.ambientPitch is the same. */
static float ambient_pitch(const SoundRecord *r, float seconds)
{
	int k = floor_i((seconds + r->offset) / r->period);
	int step = r->mode == MODE_FLICKER ? flicker_run(k, r->period, r->fault) : k;
	float t = noise(flicker_seed(r->period, r->offset, r->seed) ^ (0x6c8e9cf5u * (r->light + 1)), step);
	return r->pitch + (r->pitch_max - r->pitch) * t;
}

/* Points the Sound object at the light's own copy of its ambient, pitched for the start that
 * comes now. The copy is made from whatever the object plays the first time (the game's). */
static void pitch_ambient(Sound *s, u8 *obj, float seconds)
{
	u8 *amb = OBJrSound_GetAmbient(obj);
	float p;

	if (!amb) return;
	if (amb != s->ambient) {
		memcpy(s->ambient, amb, AMBIENT_SIZE);
		for (int g = 0; g < AMBIENT_GROUP_COUNT; g++) {
			u8 **slot = (u8 **)(s->ambient + AMBIENT_GROUPS + 4 * g);
			if (!*slot) continue;
			memcpy(s->amb_group[g], *slot, GROUP_SIZE);
			s->amb_pitch[g] = *(float *)(s->amb_group[g] + GROUP_PITCH);
			*slot = s->amb_group[g];
		}
		*(u8 **)(obj + SOUND_AMBIENT) = s->ambient;
	}
	p = ambient_pitch(&s->r, seconds);
	for (int g = 0; g < AMBIENT_GROUP_COUNT; g++)
		if (*(u8 **)(s->ambient + AMBIENT_GROUPS + 4 * g))
			*(float *)(s->amb_group[g] + GROUP_PITCH) = s->amb_pitch[g] * p;
}

/* The play list's sphere update (ECX the playing object, EDX the listener, then its facing),
 * for every Sphere sound object once a tick */
static void ONI_FC update_sphere(u8 *po, const float *listener, const float *facing)
{
	u8 *obj = *(u8 **)(po + PO_OBJECT), *gs = ONgGameState;
	Sound *s = 0;
	const SoundRecord *r;
	u32 id, before;
	float seconds;

	if (obj && gs && current_level())
		for (u32 i = 0; i < g_sound_count; i++)
			if (g_sounds[i].r.object && g_sounds[i].r.object == *(u32 *)(obj + OBJ_ID)) {
				s = &g_sounds[i];
				break;
			}
	if (!s) {
		OSiPlayList_Update_Spheres(po, listener, facing);
		return;
	}
	r = &s->r;
	seconds = (float)*(u32 *)(gs + GS_TICK) * (1.0f / 60.0f);
	if (!sound_gate_open(r, seconds)) {
		/* as if out of range, but cut now: the light has stopped failing */
		id = *(u32 *)(po + PO_PLAY_ID);
		if (id != NO_PLAY) {
			u8 *amb = OBJrSound_GetAmbient(obj), flags = amb ? amb[AMBIENT_FLAGS] : 0;
			if (amb) amb[AMBIENT_FLAGS] = flags | AMBIENT_INTERRUPT;   /* only Stop reads it */
			SSrAmbient_Stop(id);
			if (amb) amb[AMBIENT_FLAGS] = flags;
			*(u32 *)(po + PO_PLAY_ID) = NO_PLAY;
		}
		return;
	}
	before = *(u32 *)(po + PO_PLAY_ID);
	if (before == NO_PLAY && (r->pitch != 1.0f || r->pitch_max != 1.0f)) pitch_ambient(s, obj, seconds);
	OSiPlayList_Update_Spheres(po, listener, facing);
	id = *(u32 *)(po + PO_PLAY_ID);
	if (before == NO_PLAY && id != NO_PLAY) SSrAmbient_SetVolume(id, *(float *)(po + PO_VOLUME), 0.0f);
}

void vertex_light_install(void)
{
	if (patch_call(OScPlayList_SphereCall, (u32)OSiPlayList_Update_Spheres, (u32)update_sphere))
		log_msg("vertex light: flickering light ambients patched");
}

/* Every frame, after the update (live_menu.c's main loop hook) */
/* The level is going away, even to load again: a level's own sounds (the map's recordings) go with
 * it, and the same level often comes back at the same address, which load() wouldn't notice. A
 * group copied by sound_ready would keep the freed list of its permutations. */
void vertex_light_level_end(void)
{
	drop();
	g_level = 0;
	g_agqg = 0;
}

/* The level's quad table, with its vertexlight.bin loaded; 0 outside a level. The sound update
 * can come before this frame's vertex_light_frame, so both load through here. */
static u8 *current_level(void)
{
	u32 level = ONrLevel_GetCurrent();
	u8 *agqg = level ? level_agqg() : 0;

	if (!agqg) {
		if (g_recs || g_sound_count) drop();
		g_level = 0;
		g_agqg = 0;
		return 0;
	}
	if (level != g_level || agqg != g_agqg) load(level, agqg);
	return agqg;
}

void vertex_light_frame(void)
{
	u8 *agqg = current_level();
	float seconds;

	if (!agqg || (!g_count && !g_sound_count)) return;
	seconds = (float)*(u32 *)(ONgGameState + GS_TICK) * (1.0f / 60.0f);
	sounds_frame(*(u32 *)(ONgGameState + GS_TICK), seconds);
	for (u32 i = 0; i < g_count; ) {
		const Record *r = &g_recs[i];
		u32 *color = (u32 *)(agqg + AGQG_QUADS + r->quad * QUAD_SIZE + QUAD_COLORS) + r->corner;
		float sum[4];
		for (int s = 0; s < 4; s++) sum[s] = (float)(r->a >> (8 * s) & 0xff);
		/* this corner's records: the base color is the first one's */
		do {
			toward(sum, r->a, g_recs[i].b, amount(&g_recs[i], seconds));
			i++;
		} while (i < g_count && g_recs[i].quad == r->quad && g_recs[i].corner == r->corner);
		*color = pack(sum);
	}
}
