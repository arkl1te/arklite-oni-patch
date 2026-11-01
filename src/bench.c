/* Bench mode: with ARKLITE_BENCH=<seconds>, once a level has been running for BENCH_SETTLE seconds
 * this times every frame for that long, logs the frame times and quits. For comparing two builds
 * of a level from a script (tools/oni-bench.sh); nothing runs without the variable. A frame cap
 * hides differences below it: a run at the cap only says the level isn't slower than that. */
#include "arklite.h"

__declspec(dllimport) int  WINAPI QueryPerformanceCounter(unsigned long long *);
__declspec(dllimport) int  WINAPI QueryPerformanceFrequency(unsigned long long *);
__declspec(dllimport) void WINAPI ExitProcess(u32);

#define ONrLevel_GetCurrent ((u16 (ONI_FC *)(void))0x5054c0)
#define ONgGameState        (*(u8 **)0x5ece7c)

#define BENCH_SETTLE 5.0f
#define BENCH_MAX 60000

static u32 g_ticks[BENCH_MAX];     /* microseconds per frame */
static u32 g_count;
static int g_state;                /* 0 not read yet, 1 off, 2 waiting, 3 running */
static float g_seconds;
static unsigned long long g_frequency, g_start, g_last;

static void report(void)
{
	/* shell sort: no libc here */
	unsigned long long sum = 0;
	for (u32 i = 0; i < g_count; i++) sum += g_ticks[i];
	for (u32 gap = g_count / 2; gap; gap /= 2)
		for (u32 i = gap; i < g_count; i++) {
			u32 v = g_ticks[i], j = i;
			for (; j >= gap && g_ticks[j - gap] > v; j -= gap) g_ticks[j] = g_ticks[j - gap];
			g_ticks[j] = v;
		}
	if (!g_count) { log_msg("bench: no frames"); return; }
	log_msg("bench: frames %u mean_us %u p50_us %u p99_us %u max_us %u", g_count, (int)((float)(long long)sum / (float)g_count),
	        g_ticks[g_count / 2], g_ticks[g_count * 99 / 100], g_ticks[g_count - 1]);
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
		if ((float)(long long)(now - g_start) / (float)(long long)g_frequency < BENCH_SETTLE) return;
		g_state = 3;
		g_start = now;
		g_last = now;
		return;
	}
	if (g_count < BENCH_MAX) g_ticks[g_count++] = (u32)(int)((float)(long long)(now - g_last) * 1000000.0f / (float)(long long)g_frequency);
	g_last = now;
	if ((float)(long long)(now - g_start) / (float)(long long)g_frequency >= g_seconds) {
		report();
		ExitProcess(0);
	}
}
