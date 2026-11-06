/* Particles in Catwalk's spinning groups.
 *
 * A spinning group's faces become a looping physics object, but a Particle object (PART) has no
 * animation: it sits at a matrix kept in its EPtEnvParticle. Catwalk writes
 * <level folder>\spinparticles.bin, one record per PART of a spinning group, and every frame this
 * file turns that matrix around the group's vertical axis, from the game clock (frozen while the
 * game is paused, the same in every game at the same tick), then calls the engine's
 * EPrNotifyMatrixChange to carry it over to the live particle. What the particle already emitted
 * stays where it was emitted; new emissions leave from the new place and direction.
 *
 * The turn has the same length in ticks, direction and start as the group's OBAN, so a particle
 * keeps its place on the group's geometry: measured against the object's live matrix in the game,
 * +X turns toward -Z, and a quarter of a 600-tick turn is done at tick 150. Decals stay put: moving one means projecting it onto the walls again.
 *
 * In memory (OT_Particle.c, BFW_EnvParticle.c; offsets checked against 0x4c8760 and 0x453820):
 *   EPgHead (0x6a8b48) lists every EPtEnvParticle through +0xc4.
 *   +0x70 matrix (3 axes, then the translation), +0xaa owner type, 2 = a PART object's,
 *   +0xb4 class, +0xb8 particle, +0xbc that particle's self reference.
 *   A PART's OSD starts with the EPtEnvParticle and sits at object + 0x2c; object + 4 = its id.
 *   The particle pointer outlives the particle: only a matching self reference (particle + 0x2c)
 *   says it's still ours, as the engine's own checks do. */
#include "arklite.h"

__declspec(dllimport) int    WINAPI ReadFile(HANDLE, void *, u32, u32 *, void *);
__declspec(dllimport) int    WINAPI CloseHandle(HANDLE);
__declspec(dllimport) u32    WINAPI GetFileSize(HANDLE, u32 *);
__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) int    WINAPI VirtualFree(void *, u32, u32);
void *memcpy(void *, const void *, unsigned);

#define ONrLevel_GetCurrent     ((u16 (ONI_FC *)(void))0x5054c0)
#define EPrNotifyMatrixChange   ((void (ONI_FC *)(u8 *, u32, u32))0x453820)
#define ONgGameState            (*(u8 **)0x5ece7c)
#define GS_TICK                 0x144
#define EPgHead                 (*(u8 **)0x6a8b48)

#define EP_MATRIX      0x70
#define EP_OWNER_TYPE  0xaa
#define EP_CLASS       0xb4
#define EP_PARTICLE    0xb8
#define EP_SELF_REF    0xbc
#define EP_NEXT        0xc4
#define OWNER_PART     2
#define OBJ_OSD        0x2c
#define OBJ_ID         4
#define CLASS_DEF      0x48
#define DEF_FLAGS2     8
#define FLAGS2_DECAL   0x800000
#define P_FLAGS        4
#define P_SELF_REF     0x2c
#define P_POS_CHANGED  0x200     /* P3cParticleFlag_PositionChanged: its ambient sound moves too */

#define SP_MAGIC   0x50535743u   /* "CWSP" */
#define SP_VERSION 1
#define TAU        6.2831855f

typedef struct {     /* as in the file, sorted by id */
	u32   id;
	float seconds;   /* per turn; negative = the other way */
	float x, z;      /* the group's axis */
} Record;

typedef struct {
	Record r;
	u8    *ep;        /* the env particle base is from */
	float  base[12];  /* its matrix as the level placed it */
} Spin;

static Spin *g_spins;
static u32   g_count;
static u32   g_level;

static void drop(void)
{
	if (g_spins) VirtualFree(g_spins, 0, 0x8000 /* MEM_RELEASE */);
	g_spins = 0;
	g_count = 0;
}

static void load(u32 level)
{
	const char *dir = custom_game_level_dir(level);
	char path[MAX_PATH];
	u32 size, got = 0, head[3];
	HANDLE f;

	drop();
	g_level = level;
	if (!dir) return;
	fmt(path, "%sspinparticles.bin", dir);
	f = CreateFileA(path, 0x80000000 /* GENERIC_READ */, 1, 0, 3 /* OPEN_EXISTING */, 0x80, 0);
	if (f == INVALID_HANDLE_VALUE) return;
	size = GetFileSize(f, 0);
	if (size < sizeof head || !ReadFile(f, head, sizeof head, &got, 0) || got != sizeof head ||
	    head[0] != SP_MAGIC || head[1] != SP_VERSION || head[2] > (size - sizeof head) / sizeof(Record)) {
		log_msg("spin particles: %s isn't a version %d file, ignored", path, SP_VERSION);
		CloseHandle(f);
		return;
	}
	if (head[2] && (g_spins = VirtualAlloc(0, head[2] * sizeof(Spin), 0x3000, 4))) {
		for (u32 i = 0; i < head[2]; i++) {
			if (!ReadFile(f, &g_spins[i].r, sizeof(Record), &got, 0) || got != sizeof(Record)) {
				log_msg("spin particles: can't read %s", path);
				drop();
				break;
			}
			g_spins[i].ep = 0;
			g_count = i + 1;
		}
	}
	CloseHandle(f);
	log_msg("spin particles: %d particle(s)", g_count);
}

static Spin *find(u32 id)
{
	u32 lo = 0, hi = g_count;
	while (lo < hi) {
		u32 mid = (lo + hi) / 2;
		if (g_spins[mid].r.id == id) return &g_spins[mid];
		if (g_spins[mid].r.id < id) lo = mid + 1;
		else hi = mid;
	}
	return 0;
}

static void sin_cos(float a, float *s, float *c)
{
	long double sv, cv;
	__asm__("fsincos" : "=t"(cv), "=u"(sv) : "0"((long double)a));
	*s = (float)sv;
	*c = (float)cv;
}

/* The group's turn at this tick: the same period as spinSamplers in Catwalk's geometry.nim */
static float angle(float seconds, u32 tick)
{
	float length = seconds < 0.0f ? -seconds : seconds;
	u32 ticks = (u32)(length * 60.0f + 0.5f);
	float a;
	if (ticks < 2) ticks = 2;
	a = TAU * (float)(tick % ticks) / (float)ticks;
	return seconds < 0.0f ? -a : a;
}

/* Every frame, after the update (live_menu.c's main loop hook) */
void spin_particle_frame(void)
{
	u32 level = ONgGameState ? ONrLevel_GetCurrent() : 0, tick;

	if (!level) {
		if (g_spins) drop();
		g_level = 0;
		return;
	}
	if (level != g_level) load(level);
	if (!g_count) return;
	tick = *(u32 *)(ONgGameState + GS_TICK);
	for (u8 *ep = EPgHead; ep; ep = *(u8 **)(ep + EP_NEXT)) {
		Spin *s;
		u8 *cls, *p;
		float *m, sn, cs;

		if (*(u16 *)(ep + EP_OWNER_TYPE) != OWNER_PART) continue;
		s = find(*(u32 *)(ep - OBJ_OSD + OBJ_ID));
		if (!s) continue;
		cls = *(u8 **)(ep + EP_CLASS);
		if (cls && (*(u32 *)(*(u8 **)(cls + CLASS_DEF) + DEF_FLAGS2) & FLAGS2_DECAL)) continue;
		m = (float *)(ep + EP_MATRIX);
		if (s->ep != ep) {
			s->ep = ep;
			memcpy(s->base, m, sizeof s->base);
		}
		sin_cos(angle(s->r.seconds, tick), &sn, &cs);
		for (int col = 0; col < 4; col++) {
			float x = s->base[col * 3], z = s->base[col * 3 + 2];
			if (col == 3) { x -= s->r.x; z -= s->r.z; }
			m[col * 3] = x * cs + z * sn;
			m[col * 3 + 2] = z * cs - x * sn;
			if (col == 3) { m[9] += s->r.x; m[11] += s->r.z; }
		}
		/* Not created yet (a script starts it): it'll start from this matrix. */
		p = *(u8 **)(ep + EP_PARTICLE);
		if (cls && p && *(u32 *)(p + P_SELF_REF) == *(u32 *)(ep + EP_SELF_REF)) {
			EPrNotifyMatrixChange(ep, tick, 0);
			*(u32 *)(p + P_FLAGS) |= P_POS_CHANGED;
		}
	}
}
