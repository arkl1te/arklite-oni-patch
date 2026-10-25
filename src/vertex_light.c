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
#define VL_VERSION 3   /* 1: one record per corner; 2: several; 3: min, max, stability, fault */
#define VL_V2_SIZE 24  /* bytes per record up to version 2 */

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
} Record;

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
	    head[2] > (size - sizeof head) / (rsize = head[1] >= 3 ? sizeof(Record) : VL_V2_SIZE)) {
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
	CloseHandle(f);
	if (!g_recs) return;
	if (rsize != sizeof(Record)) {
		const u8 *src = (u8 *)g_recs + head[2] * sizeof(Record) - bytes;
		for (u32 i = 0; i < head[2]; i++) {
			Record r;
			memcpy(&r, src + i * rsize, rsize);
			r.lo = 0.0f; r.hi = 1.0f; r.stability = 0.0f; r.fault = 0.0f;
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

/* A flicker's level at step k: 0 while the light works (its run of steps came up working, by
 * stability), else a random one between lo and hi */
static float flicker_level(const Record *r, u32 seed, int k)
{
	if (r->stability > 0.0f) {
		float run = r->fault > r->period ? r->fault : r->period;
		if (noise(seed ^ 0x5bd1e995u, floor_i((float)k * r->period / run)) < r->stability) return 0.0f;
	}
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
	union { float f; u32 u; } p = { r->period }, o = { r->offset };
	u32 seed = p.u * 31u + o.u;
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

/* Every frame, after the update (live_menu.c's main loop hook) */
void vertex_light_frame(void)
{
	u32 level = ONrLevel_GetCurrent();
	u8 *agqg = level ? level_agqg() : 0;
	float seconds;

	if (!agqg) {
		if (g_recs) drop();
		g_level = 0;
		g_agqg = 0;
		return;
	}
	if (level != g_level || agqg != g_agqg) load(level, agqg);
	if (!g_count) return;
	seconds = (float)*(u32 *)(ONgGameState + GS_TICK) * (1.0f / 60.0f);
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
