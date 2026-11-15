/* Lag-o-meter, after Quake 3's cg_lagometer (cgame/cg_draw.c CG_DrawLagometer): two graphs of the
 * last 128 samples, newest on the right, in a box at the right edge above the health ring, and a
 * line of numbers. In network games only; /lagometer (or /lag) turns it on and off, Arklite.ini
 * [hud] lagometer=1 keeps it on.
 *
 * Upper graph, one sample a frame (Q3's frame interpolation graph):
 *   - up from the middle line: how far the game is past the last tick every player's input is in
 *     (the ticks run on guesses, Q3's extrapolating; it grows with the ping): blue up to 6 ticks,
 *     yellow past that;
 *   - down from the middle line, red: a rollback this frame, as deep as the ticks it ran again;
 *   - a gray bar the whole height: a frame that waited for the others' inputs (ran fewer ticks than
 *     the clock asked, or none).
 * Lower graph, one sample per input stream that came in (Q3's snapshot graph):
 *   - its height: how many ticks late it came (this PC had already run that far on a guess; a
 *     rollback follows when the guess was wrong), or early (a thin bar): green up to 2 ticks late
 *     (any ping over a tick), yellow up to 6, red past that.
 * The numbers: each other player's ping, and over the last second the rollbacks, the ticks run
 * again and the frames that waited. */
#include "arklite.h"

void *memset(void *, int, unsigned);
__declspec(dllimport) u32 WINAPI GetTickCount(void);

#define SAMPLES   128
#define FRAME_MAX 10      /* ticks: the upper graph's half height */
#define PKT_MAX   12      /* ticks: the lower graph's height */

static struct { u8 ran, ahead, depth, waited; } g_frame[SAMPLES];
static struct { signed char late; } g_pkt[SAMPLES];
static u32 g_fi, g_pi;
static int g_on, g_loaded;
static u8 *g_tex[7];   /* black, blue, yellow, red, gray, green, white */
enum { T_BLACK, T_BLUE, T_YELLOW, T_RED, T_GRAY, T_GREEN, T_WHITE };

/* the last second's counts */
static u32 g_sec_at, g_sec_rb, g_sec_rerun, g_sec_wait, g_show_rb, g_show_rerun, g_show_wait;

static void load(void)
{
	if (g_loaded) return;
	g_loaded = 1;
	g_on = GetPrivateProfileIntA("hud", "lagometer", 0, ini_path()) != 0;
}

int lagometer_on(void) { load(); return g_on; }

void lagometer_toggle(void)
{
	load();
	g_on = !g_on;
	WritePrivateProfileStringA("hud", "lagometer", g_on ? "1" : "0", ini_path());
	log_msg("lagometer: %s", g_on ? "on" : "off");
}

static void second(void)
{
	u32 now = GetTickCount();
	if (now - g_sec_at >= 1000) {
		g_show_rb = g_sec_rb;
		g_show_rerun = g_sec_rerun;
		g_show_wait = g_sec_wait;
		g_sec_rb = g_sec_rerun = g_sec_wait = 0;
		g_sec_at = now;
	}
}

/* net.c, every frame of a rolling game: ticks run, how far past the last known input, the rollback's
 * depth (0: none), whether it waited for inputs */
void lagometer_frame(u32 ran, u32 ahead, u32 depth, int waited)
{
	u32 i = g_fi++ % SAMPLES;
	g_frame[i].ran = (u8)(ran > 255 ? 255 : ran);
	g_frame[i].ahead = (u8)(ahead > 255 ? 255 : ahead);
	g_frame[i].depth = (u8)(depth > 255 ? 255 : depth);
	g_frame[i].waited = (u8)(waited != 0);
	second();
	if (depth) g_sec_rb++, g_sec_rerun += depth;
	if (waited) g_sec_wait++;
}

/* net.c, an input stream from another player: by how many ticks it came late (negative: early) */
void lagometer_packet(int player, int late)
{
	u32 i = g_pi++ % SAMPLES;
	(void)player;
	g_pkt[i].late = (signed char)(late > 127 ? 127 : late < -127 ? -127 : late);
}

static void bar(int tex, int x, int y, int w, int h, u32 alpha)
{
	if (h > 0 && w > 0) hud_rect(g_tex[tex], x, y, w, h, alpha);
}

void lagometer_draw(int width, int height)
{
	int px, w, gh, x0, y0, mid, scale_f, scale_p;
	char line[160];

	load();
	if (!g_on || !net_active()) return;
	if (!g_tex[0]) {
		g_tex[T_BLACK] = hud_solid(0x8000, "arklite_lag_black");
		g_tex[T_BLUE] = hud_solid(0x8000 | 6 << 10 | 12 << 5 | 31, "arklite_lag_blue");
		g_tex[T_YELLOW] = hud_solid(0x8000 | 31 << 10 | 31 << 5 | 0, "arklite_lag_yellow");
		g_tex[T_RED] = hud_solid(0x8000 | 31 << 10 | 4 << 5 | 4, "arklite_lag_red");
		g_tex[T_GRAY] = hud_solid(0x8000 | 14 << 10 | 14 << 5 | 14, "arklite_lag_gray");
		g_tex[T_GREEN] = hud_solid(0x8000 | 4 << 10 | 28 << 5 | 4, "arklite_lag_green");
		g_tex[T_WHITE] = hud_solid(0xffff, "arklite_lag_white");
		if (!g_tex[0]) return;
	}
	second();
	px = mp_hud_px(2) > 0 ? mp_hud_px(2) : 1;
	w = SAMPLES * px;
	gh = mp_hud_px(64);                     /* each graph's height */
	x0 = width - w - mp_hud_px(24);
	y0 = height / 2 - gh;                   /* the upper graph's top: the box sits at the right edge's middle */
	mid = y0 + gh / 2;
	scale_f = gh / 2 / FRAME_MAX > 0 ? gh / 2 / FRAME_MAX : 1;
	scale_p = gh / PKT_MAX > 0 ? gh / PKT_MAX : 1;
	bar(T_BLACK, x0 - px * 2, y0 - px * 2, w + px * 4, gh * 2 + px * 6, 140);
	bar(T_WHITE, x0, mid, w, px, 90);                    /* the upper graph's middle line */
	bar(T_WHITE, x0, y0 + gh + px * 2, w, px, 60);       /* between the graphs */

	/* upper: frames */
	for (u32 k = 0; k < SAMPLES; k++) {
		u32 i = (g_fi + k) % SAMPLES;   /* oldest first */
		int x = x0 + (int)k * px;
		if (g_fi < SAMPLES && i >= g_fi) continue;
		if (g_frame[i].waited) bar(T_GRAY, x, y0, px, gh, 120);
		{
			int a = g_frame[i].ahead > FRAME_MAX ? FRAME_MAX : g_frame[i].ahead;
			bar(g_frame[i].ahead > 6 ? T_YELLOW : T_BLUE, x, mid - (a ? a : 0) * scale_f, px, a * scale_f, 230);
		}
		{
			int d = g_frame[i].depth > FRAME_MAX ? FRAME_MAX : g_frame[i].depth;
			bar(T_RED, x, mid + px, px, d * scale_f, 230);
		}
	}
	/* lower: input streams */
	{
		int base = y0 + gh * 2 + px * 3;
		for (u32 k = 0; k < SAMPLES; k++) {
			u32 i = (g_pi + k) % SAMPLES;
			int x = x0 + (int)k * px, late, h;
			if (g_pi < SAMPLES && i >= g_pi) continue;
			late = g_pkt[i].late;
			if (late <= 0) {
				bar(T_GREEN, x, base - px, px, px, 230);   /* early: a thin bar */
			} else {
				h = late > PKT_MAX ? PKT_MAX : late;
				bar(late > 6 ? T_RED : late > 2 ? T_YELLOW : T_GREEN, x, base - h * scale_p, px, h * scale_p, 230);
			}
		}
	}
	/* numbers */
	net_ping_text(line, sizeof line);
	mp_hud_text_plain(line, x0 + w, y0 - mp_hud_line_height() * 2 - px * 2, MP_RIGHT, 0xffe0e0e0u);
	fmt(line, "rollbacks %u/s, %u ticks again; waited %u frames", g_show_rb, g_show_rerun, g_show_wait);
	mp_hud_text_plain(line, x0 + w, y0 - mp_hud_line_height() - px * 2, MP_RIGHT, 0xffe0e0e0u);
}

/* bug.c: the last samples as text (oldest first) */
void lagometer_note(char *out, u32 size)
{
	u32 n = 0;
	char one[160];   /* (fmt writes up to the whole line: the first one is ~70 characters) */
	out[0] = 0;
#define ADD(s_) do { u32 l_ = str_len(s_); if (n + l_ + 1 < size) { str_cpy(out + n, size - n, s_); n += l_; } } while (0)
	fmt(one, "last second: %u rollbacks, %u ticks again, %u frames waited", g_show_rb, g_show_rerun, g_show_wait);
	ADD(one);
	ADD("\r\nframes (ticks run/ahead of every input/rollback depth/waited), oldest first:\r\n");
	for (u32 k = 0; k < SAMPLES; k++) {
		u32 i = (g_fi + k) % SAMPLES;
		if (g_fi < SAMPLES && i >= g_fi) continue;
		fmt(one, "%u/%u/%u/%u ", g_frame[i].ran, g_frame[i].ahead, g_frame[i].depth, g_frame[i].waited);
		ADD(one);
	}
	ADD("\r\ninput packets (ticks late, negative: early), oldest first:\r\n");
	for (u32 k = 0; k < SAMPLES; k++) {
		u32 i = (g_pi + k) % SAMPLES;
		if (g_pi < SAMPLES && i >= g_pi) continue;
		fmt(one, "%d ", (int)g_pkt[i].late);
		ADD(one);
	}
#undef ADD
}
