/* Bench mode: with ARKLITE_BENCH=<seconds>, once a level has been running for BENCH_SETTLE seconds
 * this times every frame for that long, logs the frame times and quits. For comparing two builds
 * of a level from a script (tools/oni-bench.sh); nothing runs without the variable. A frame cap
 * hides differences below it: a run at the cap only says the level isn't slower than that, so it
 * also times each frame's drawing, from the update's end to the end of SwapBuffers (draw_us), which
 * no cap shortens. With ARKLITE_PLAYBACK, the part of the recording played at the game's speed
 * (ARKLITE_PLAYBACK_REALTIME) is timed instead, to its end or for the seconds given. */
#include "arklite.h"

__declspec(dllimport) int  WINAPI QueryPerformanceCounter(unsigned long long *);
__declspec(dllimport) int  WINAPI QueryPerformanceFrequency(unsigned long long *);
__declspec(dllimport) void WINAPI ExitProcess(u32);

#define ONrLevel_GetCurrent ((u16 (ONI_FC *)(void))0x5054c0)
#define ONgGameState        (*(u8 **)0x5ece7c)

#define BENCH_SETTLE 5.0f
#define BENCH_MAX 60000

static u32 g_ticks[BENCH_MAX];     /* microseconds per frame */
static u32 g_draw[BENCH_MAX];      /* microseconds drawing each frame (bench_present) */
static u32 g_count, g_dcount;
static unsigned long long g_second;   /* the per-second lines: when the current second began, its first frame */
static u32 g_second_at, g_dsecond_at, g_seconds_done;
static unsigned long long g_updated;   /* this frame's update ended (bench_frame), 0: not timed */
static int g_state;                /* 0 not read yet, 1 off, 2 waiting, 3 running */
static float g_seconds;
static unsigned long long g_frequency, g_start, g_last;

static u32 us_since(unsigned long long then, unsigned long long now)
{
	return (u32)(int)((float)(long long)(now - then) * 1000000.0f / (float)(long long)g_frequency);
}

/* mean, median, 99th percentile and slowest of n values (sorts them) */
static void stats(const char *what, u32 *v, u32 n)
{
	/* shell sort: no libc here */
	unsigned long long sum = 0;
	for (u32 i = 0; i < n; i++) sum += v[i];
	for (u32 gap = n / 2; gap; gap /= 2)
		for (u32 i = gap; i < n; i++) {
			u32 x = v[i], j = i;
			for (; j >= gap && v[j - gap] > x; j -= gap) v[j] = v[j - gap];
			v[j] = x;
		}
	if (!n) { log_msg("bench: no %s", what); return; }
	log_msg("bench: %s %u mean_us %u p50_us %u p99_us %u max_us %u", what, n, (int)((float)(long long)sum / (float)n),
	        v[n / 2], v[n * 99 / 100], v[n - 1]);
}

static void report(void)
{
	stats("frames", g_ticks, g_count);
	stats("draws", g_draw, g_dcount);
}

/* After every SwapBuffers (screenshot.c): the frame's drawing, from its update's end */
void bench_present(void)
{
	unsigned long long now;
	if (g_state != 3 || !g_updated) return;
	QueryPerformanceCounter(&now);
	if (g_dcount < BENCH_MAX) g_draw[g_dcount++] = us_since(g_updated, now);
	g_updated = 0;
}

/* Every frame, after the update (live_menu.c's main loop hook) */
void bench_frame(void)
{
	unsigned long long now;

	if (g_state == 1) return;
	if (!g_state) {
		char v[16];
		int whole = 0;
		g_state = 1;
		if (!GetEnvironmentVariableA("ARKLITE_BENCH", v, sizeof v)) return;
		for (char *c = v; *c >= '0' && *c <= '9'; c++) whole = whole * 10 + (*c - '0');
		if (whole <= 0) return;
		g_seconds = (float)whole;
		QueryPerformanceFrequency(&g_frequency);
		g_state = 2;
		log_msg("bench: on, %d s", whole);
	}
	if (!ONrLevel_GetCurrent() || !ONgGameState) { g_start = 0; return; }
	QueryPerformanceCounter(&now);
	if (!g_start) g_start = now;
	if (g_state == 2) {
		int pb = net_playback_phase();
		if (pb < 0 ? (float)(long long)(now - g_start) / (float)(long long)g_frequency < BENCH_SETTLE : pb != 1) return;
		if (pb == 1) log_msg("bench: timing the recording's part at the game's speed");
		g_state = 3;
		g_start = now;
		g_last = now;
		g_updated = now;
		g_second = now;
		return;
	}
	if (g_count < BENCH_MAX) g_ticks[g_count++] = us_since(g_last, now);
	if (g_count > g_second_at && us_since(g_second, now) >= 1000000) {   /* each second: its means */
		unsigned long long f = 0, d = 0;
		for (u32 i = g_second_at; i < g_count; i++) f += g_ticks[i];
		for (u32 i = g_dsecond_at; i < g_dcount; i++) d += g_draw[i];
		log_msg("bench: second %u: %u frames, frame mean_us %u, draw mean_us %u", g_seconds_done++, g_count - g_second_at,
		        (u32)((float)(long long)f / (float)(g_count - g_second_at)), g_dcount > g_dsecond_at ? (u32)((float)(long long)d / (float)(g_dcount - g_dsecond_at)) : 0);
		g_second = now;
		g_second_at = g_count;
		g_dsecond_at = g_dcount;
	}
	g_last = now;
	g_updated = now;
	if ((float)(long long)(now - g_start) / (float)(long long)g_frequency >= g_seconds || net_playback_phase() == 2) {
		report();
		ExitProcess(0);
	}
}
