/* Lens flares without a GPU stall each.
 *
 * Every lens flare particle (Muro's and the ninjas' glows, flashes and trails, lamps, weapon
 * flashes; 129 classes in level 0) asks each frame whether its point is hidden. The particle draw
 * (P3rPointVisible) goes through the geom engine's pointVisible (0x412b10), which, when the draw
 * engine supports depth reads (0x41a670 -> 0x405b50: Oni's multitexture mode is 2, nVidia register
 * combiners, which every NVIDIA card gets), projects the point and calls gl_point_visible
 * (0x405ad0): glReadBuffer(GL_BACK) and a 1x1 glReadPixels of the depth buffer, through Oni's GL
 * table (*0x560604 +0x3b0, +0x3b4). Each read makes the CPU wait until the GPU has drawn everything
 * sent so far, so Muro's four-punch combo, with its glows emitting two flares every 0.01 s, stalled
 * dozens of times a frame: 170 FPS fell to 15 (super_ninja_1's specials: 11). Other cards take
 * Oni's raycast path (0x412c09), which doesn't see characters.
 *
 * All of those tests come in one pass after the whole 3D scene (the lens flare pass of the overlay
 * elements, then the sun's), when nothing writes depth any more. So gl_point_visible is replaced:
 * a frame's first test reads, once, the depth of the area where last frame's flares were tested
 * (plus MARGIN pixels, and a FIRST_SPAN square around this point), and every test of the frame is
 * answered from it, as Oni's own read would have been: the same pixel (x rounded, rows from the
 * bottom), the same comparison. A point outside it reads a BLOCK square around itself (up to
 * EXTRA_MAX a frame), and past that Oni's own read runs. Frames are counted at the present
 * (screenshot.c's SwapBuffers hook calls flare_depth_present).
 *
 * Measured hidden at 1400x1050 without vsync (Muro's four-punch combo on a loop): 9.8 ms a frame,
 * 1% of frames over 154 ms, before; 3.2 ms and 6.2 ms after (2.4 ms with no flares), ~0.25 MP read
 * a frame. super_ninja_1's specials: 92 ms -> 3.5 ms. 20000 answers checked against Oni's own
 * read: all the same. ARKLITE_FLARE_DEPTH=0 leaves Oni's reads, for comparing. */
#include "arklite.h"

void *memcpy(void *, const void *, unsigned);

__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) int    WINAPI VirtualFree(void *, u32, u32);

#define GLcPointVisible     0x405ad0     /* gl_point_visible(ecx = M3tPointScreen *, [tolerance]) */
#define GLgEngine           (*(u8 **)0x560600)   /* +0x540 width, +0x542 height (u16) */
#define GLgFuncs            (*(u8 **)0x560604)   /* Oni's GL function table */
#define GL_ReadBuffer       (*(void (WINAPI **)(u32))(GLgFuncs + 0x3b0))
#define GL_ReadPixels       (*(void (WINAPI **)(int, int, int, int, u32, u32, void *))(GLgFuncs + 0x3b4))
#define MUrRound            ((u32 (WINAPI *)(float))0x4340a0)

#define GL_BACK             0x0405
#define GL_DEPTH_COMPONENT  0x1902
#define GL_FLOAT            0x1406

static u32 g_frame = 1, g_read_frame;
static u16 g_w, g_h;
static float *g_depth;
static u32 g_cap;
static u32 g_stat_frames, g_stat_tests;
static unsigned long long g_t_read;
__declspec(dllimport) int WINAPI QueryPerformanceCounter(unsigned long long *);
__declspec(dllimport) int WINAPI QueryPerformanceFrequency(unsigned long long *);

void flare_depth_present(void) { g_frame++; }

/* Oni's own read, from its second instruction (the first five bytes are our jump) */
__attribute__((naked)) static u32 ONI_FC original(const float *point, u32 edx, float tolerance)
{
	__asm__ volatile(
		"pushl %ecx\n\t"
		"pushl %ebx\n\t"
		"pushl %esi\n\t"
		"movl %ecx, %esi\n\t"
		"pushl $0x405ad5\n\t"
		"ret\n");
}

/* The area read this frame: last frame's tested points and a margin; points outside it get a
 * block of their own (up to EXTRA_MAX a frame), then Oni's own read */
#define MARGIN      48
#define FIRST_SPAN  128
#define BLOCK       64
#define EXTRA_MAX   8
typedef struct { int x, y, w, h; float *d; } Area;
static Area g_main, g_extra[EXTRA_MAX];
static int g_nextra;
static float g_extra_buf[EXTRA_MAX][BLOCK * BLOCK];
static int g_bx0, g_by0, g_bx1, g_by1, g_have_box;   /* this frame's tested points */
static int g_px0, g_py0, g_px1, g_py1, g_have_prev;  /* last frame's */
static u32 g_stat_extra, g_stat_orig, g_stat_area;

static void read_area(Area *a, int x, int y, int w, int h, float *into)
{
	unsigned long long t0, t1;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > g_w) w = g_w - x;
	if (y + h > g_h) h = g_h - y;
	a->x = x; a->y = y; a->w = w > 0 ? w : 0; a->h = h > 0 ? h : 0; a->d = into;
	if (!a->w || !a->h) return;
	QueryPerformanceCounter(&t0);
	GL_ReadBuffer(GL_BACK);
	GL_ReadPixels(x, y, a->w, a->h, GL_DEPTH_COMPONENT, GL_FLOAT, into);
	QueryPerformanceCounter(&t1);
	if (g_stat_frames < 600) g_t_read += t1 - t0;
}

static int in_area(const Area *a, u32 x, u32 y)
{
	return (int)x >= a->x && (int)x < a->x + a->w && (int)y >= a->y && (int)y < a->y + a->h;
}

static u32 ONI_FC point_visible(const float *point, u32 edx, float tolerance)
{
	u8 *gl = GLgEngine;
	u16 w = *(u16 *)(gl + 0x540), h = *(u16 *)(gl + 0x542);
	u32 x = MUrRound(point[0]), y = h - MUrRound(point[1]);   /* GL rows count from the bottom, as Oni's read */
	const Area *a = 0;

	if (x >= w || y >= h) return original(point, edx, tolerance);
	if (g_read_frame != g_frame || w != g_w || h != g_h) {   /* the frame's first test */
		u32 need = (u32)w * h * 4;
		if (w != g_w || h != g_h) g_have_prev = 0;
		else if (g_have_box) { g_px0 = g_bx0; g_py0 = g_by0; g_px1 = g_bx1; g_py1 = g_by1; g_have_prev = 1; }
		g_have_box = 0;
		if (need > g_cap) {
			if (g_depth) VirtualFree(g_depth, 0, 0x8000);
			g_depth = VirtualAlloc(0, need, 0x3000, 4);
			g_cap = g_depth ? need : 0;
			if (!g_depth) return original(point, edx, tolerance);
		}
		g_read_frame = g_frame;
		g_w = w;
		g_h = h;
		g_nextra = 0;
		{
			int x0 = (int)x - FIRST_SPAN / 2, y0 = (int)y - FIRST_SPAN / 2, x1 = (int)x + FIRST_SPAN / 2, y1 = (int)y + FIRST_SPAN / 2;
			if (g_have_prev) {
				if (g_px0 - MARGIN < x0) x0 = g_px0 - MARGIN;
				if (g_py0 - MARGIN < y0) y0 = g_py0 - MARGIN;
				if (g_px1 + MARGIN > x1) x1 = g_px1 + MARGIN;
				if (g_py1 + MARGIN > y1) y1 = g_py1 + MARGIN;
			}
			read_area(&g_main, x0, y0, x1 - x0, y1 - y0, g_depth);
			if (g_stat_frames < 600) g_stat_area += (u32)g_main.w * g_main.h;
		}
		if (g_stat_frames < 600 && ++g_stat_frames == 600) {
			unsigned long long f;
			QueryPerformanceFrequency(&f);
			log_msg("flare depth: 600 frames with flares, %d tests (%d a frame), read %d us a frame, %d px a frame, %d extra blocks, %d of Oni's reads",
			        g_stat_tests, g_stat_tests / 600, (int)((double)g_t_read / 600.0 * 1000000.0 / (double)f), g_stat_area / 600,
			        g_stat_extra, g_stat_orig);
		}
	}
	if (g_stat_frames < 600) g_stat_tests++;
	if (!g_have_box) { g_bx0 = g_bx1 = (int)x; g_by0 = g_by1 = (int)y; g_have_box = 1; }
	else {
		if ((int)x < g_bx0) g_bx0 = (int)x;
		if ((int)x > g_bx1) g_bx1 = (int)x;
		if ((int)y < g_by0) g_by0 = (int)y;
		if ((int)y > g_by1) g_by1 = (int)y;
	}
	if (in_area(&g_main, x, y)) a = &g_main;
	for (int i = 0; !a && i < g_nextra; i++)
		if (in_area(&g_extra[i], x, y)) a = &g_extra[i];
	if (!a && g_nextra < EXTRA_MAX) {
		Area *e = &g_extra[g_nextra];
		read_area(e, (int)x - BLOCK / 2, (int)y - BLOCK / 2, BLOCK, BLOCK, g_extra_buf[g_nextra]);
		g_nextra++;
		if (g_stat_frames < 600) g_stat_extra++;
		if (in_area(e, x, y)) a = e;
	}
	if (!a) {
		if (g_stat_frames < 600) g_stat_orig++;
		return original(point, edx, tolerance);
	}
	return a->d[((int)y - a->y) * a->w + ((int)x - a->x)] > point[2];
}

void flare_depth_install(void)
{
	static const u8 expected[] = { 0x51, 0x53, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x06, 0x50, 0xe8 };
	char v[4];
	u8 jump[5] = { 0xe9 };
	u32 rel = (u32)point_visible - (GLcPointVisible + 5);

	if (GetEnvironmentVariableA("ARKLITE_FLARE_DEPTH", v, sizeof v) == 1 && v[0] == '0') {
		log_msg("flare depth: Oni's own reads (ARKLITE_FLARE_DEPTH=0)");
		return;
	}
	if (!screenshot_hooked()) { log_msg("flare depth: no frame hook (screenshot.c), left alone"); return; }
	if (!code_matches(GLcPointVisible, expected, sizeof expected)) {
		log_msg("flare depth: unexpected code at 0x405ad0, left alone");
		return;
	}
	memcpy(jump + 1, &rel, 4);
	patch_write(GLcPointVisible, jump, sizeof jump);
	log_msg("flare depth: lens flares read the depth once a frame, not once each (no stalls)");
}
