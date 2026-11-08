/* F12 saves a screenshot to AE\screenshots\Oni_<date>_<time>.png, then plays the game-saved sound
 * (impulse autosave), anywhere a frame is drawn: in a level, in the menus and on loading screens.
 *
 * Oni's own "screenshot" input (Oni_GameState.c iScreenShot, a .bmp in the game folder) is bound
 * to F13, a Mac key that PC keyboards lack, and only runs in a game tick, so it misses the menus
 * and loading. Instead, both of Oni's frame presents (gl_display_back_buffer 0x408340 and the
 * Matrox variant 0x408360) call SwapBuffers through the import at 0x529038; those two calls come
 * here, which reads the finished back buffer right before the swap.
 *
 * The key is polled on a thread every 10 ms, so a tap counts even while a loading screen draws a
 * frame only now and then; the next frame drawn is the one saved. Encoding and writing run on
 * another thread so the game doesn't hitch: GDI+ writes a PNG, and a BMP is written if GDI+ can't.
 * The sound plays from the next frame after the file is closed. */
#include "arklite.h"

void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);

typedef struct { u16 year, month, weekday, day, hour, minute, second, ms; } SYSTEMTIME;
typedef struct { int left, top, right, bottom; } RECT;

__declspec(dllimport) HANDLE WINAPI CreateThread(void *, u32, u32 (WINAPI *)(void *), void *, u32, u32 *);
__declspec(dllimport) void   WINAPI Sleep(u32);
__declspec(dllimport) void * WINAPI VirtualAlloc(void *, u32, u32, u32);
__declspec(dllimport) int    WINAPI VirtualFree(void *, u32, u32);
__declspec(dllimport) int    WINAPI CloseHandle(HANDLE);
__declspec(dllimport) int    WINAPI CreateDirectoryA(const char *, void *);
__declspec(dllimport) void   WINAPI GetLocalTime(SYSTEMTIME *);
__declspec(dllimport) void * WINAPI GetProcAddress(HANDLE, const char *);
__declspec(dllimport) HANDLE WINAPI LoadLibraryA(const char *);
__declspec(dllimport) u32    WINAPI GetCurrentProcessId(void);
__declspec(dllimport) short  WINAPI GetAsyncKeyState(int);
__declspec(dllimport) void * WINAPI GetForegroundWindow(void);
__declspec(dllimport) u32    WINAPI GetWindowThreadProcessId(void *, u32 *);
__declspec(dllimport) int    WINAPI GetClientRect(void *, RECT *);
__declspec(dllimport) void * WINAPI WindowFromDC(void *);
__declspec(dllimport) int    WINAPI DeleteFileA(const char *);

#define SSrImpulse_GetByName ((u8 *(ONI_FC *)(const char *))0x4801f0)
#define SSrImpulse_Play      ((void (ONI_FC *)(u8 *, const float *, u32, u32, u32))0x480440)

#define VK_F12 0x7b
#define SHOT_DIR "screenshots"

/* Oni.exe: the two `call [SwapBuffers]` (ff 15 38 90 52 00) */
#define ONcSwapBuffersImport 0x529038
static const u32 g_sites[] = { 0x40834c, 0x40838f };

typedef int (WINAPI *SwapBuffersFn)(void *);

/* opengl32 */
#define GL_UNSIGNED_BYTE   0x1401
#define GL_RGB             0x1907
#define GL_BACK            0x0405
#define GL_READ_BUFFER     0x0c02
#define GL_PACK_ALIGNMENT  0x0d05
#define GL_PACK_ROW_LENGTH 0x0d02
#define GL_PACK_SKIP_ROWS  0x0d03
#define GL_PACK_SKIP_PIXELS 0x0d04
static void (WINAPI *gl_ReadPixels)(int, int, int, int, u32, u32, void *);
static void (WINAPI *gl_ReadBuffer)(u32);
static void (WINAPI *gl_PixelStorei)(u32, int);
static void (WINAPI *gl_GetIntegerv)(u32, int *);
static u32  (WINAPI *gl_GetError)(void);

static volatile int g_request;   /* F12 went down: save the next frame */
static volatile int g_busy;      /* a shot is being written (one at a time) */
static volatile int g_done;      /* written: play the sound */
static int g_gl_state;           /* 0 not looked up, 1 ok, 2 missing */

typedef struct { u8 *pixels; int w, h, stride; } Shot;
static Shot g_shot;

static int focused(void)
{
	u32 pid = 0;
	void *w = GetForegroundWindow();
	if (w) GetWindowThreadProcessId(w, &pid);
	return pid == GetCurrentProcessId();
}

static u32 WINAPI key_thread(void *unused)
{
	int was = 0;
	for (;;) {
		int down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0 && focused();
		if (down && !was) g_request = 1;
		was = down;
		Sleep(10);
	}
	return 0;
}

/* ---- Writing ------------------------------------------------------------------------------ */

typedef struct { u32 version; void *debug_cb; int no_bg_thread, no_codecs; } GdiplusStartupInput;
typedef struct { u32 d1; u16 d2, d3; u8 d4[8]; } GUID;
static const GUID g_png_clsid = { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
#define PixelFormat24bppRGB 0x21808   /* B, G, R bytes */

static int (WINAPI *gdip_Startup)(u32 *, const GdiplusStartupInput *, void *);
static int (WINAPI *gdip_CreateBitmapFromScan0)(int, int, int, int, u8 *, void **);
static int (WINAPI *gdip_SaveImageToFile)(void *, const u16 *, const GUID *, const void *);
static int (WINAPI *gdip_DisposeImage)(void *);

static int gdiplus_ready(void)
{
	static int state;   /* 0 not tried, 1 ok, 2 unavailable */
	if (!state) {
		HANDLE dll = LoadLibraryA("gdiplus.dll");
		GdiplusStartupInput in = { 1, 0, 0, 0 };
		u32 token;
		state = 2;
		if (dll) {
			gdip_Startup = GetProcAddress(dll, "GdiplusStartup");
			gdip_CreateBitmapFromScan0 = GetProcAddress(dll, "GdipCreateBitmapFromScan0");
			gdip_SaveImageToFile = GetProcAddress(dll, "GdipSaveImageToFile");
			gdip_DisposeImage = GetProcAddress(dll, "GdipDisposeImage");
			if (gdip_Startup && gdip_CreateBitmapFromScan0 && gdip_SaveImageToFile && gdip_DisposeImage &&
			    gdip_Startup(&token, &in, 0) == 0)
				state = 1;
		}
		if (state != 1) log_msg("screenshot: GDI+ unavailable, saving .bmp");
	}
	return state == 1;
}

static int save_png(const Shot *s, const char *path)
{
	u16 wide[MAX_PATH];
	void *bitmap = 0;
	int status;
	u32 i = 0;
	for (; path[i] && i < MAX_PATH - 1; i++) wide[i] = (u8)path[i];   /* our own ASCII name */
	wide[i] = 0;
	if (gdip_CreateBitmapFromScan0(s->w, s->h, s->stride, PixelFormat24bppRGB, s->pixels, &bitmap) || !bitmap)
		return 0;
	status = gdip_SaveImageToFile(bitmap, wide, &g_png_clsid, 0);
	gdip_DisposeImage(bitmap);
	if (status) log_msg("screenshot: GDI+ PNG save failed (status %d)", status);
	return status == 0;
}

static void put32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

static int save_bmp(const Shot *s, const char *path)
{
	u8 head[54];
	u32 size = (u32)(s->stride * s->h), written;
	HANDLE f;
	int ok;
	memset(head, 0, sizeof head);
	head[0] = 'B'; head[1] = 'M';
	put32(head + 2, 54 + size);
	put32(head + 10, 54);
	put32(head + 14, 40);
	put32(head + 18, (u32)s->w);
	put32(head + 22, (u32)-s->h);   /* top-down, like the rows in s->pixels */
	head[26] = 1; head[28] = 24;
	put32(head + 34, size);
	f = CreateFileA(path, 0x40000000 /* GENERIC_WRITE */, 0, 0, 2 /* CREATE_ALWAYS */, 0x80, 0);
	if (f == INVALID_HANDLE_VALUE) return 0;
	ok = WriteFile(f, head, sizeof head, &written, 0) && WriteFile(f, s->pixels, size, &written, 0) && written == size;
	CloseHandle(f);
	return ok;
}

/* AE\screenshots\Oni_2026-11-08_14-03-27.png, then _2, _3... for more in the same second */
static void shot_name(char *path, const char *ext)
{
	SYSTEMTIME t;
	char stem[MAX_PATH];
	GetLocalTime(&t);
	fmt(stem, SHOT_DIR "\\Oni_%04d-%02d-%02d_%02d-%02d-%02d", t.year, t.month, t.day, t.hour, t.minute, t.second);
	fmt(path, "%s%s", stem, ext);
	for (int n = 2; GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES && n < 100; n++)
		fmt(path, "%s_%d%s", stem, n, ext);
}

static u32 WINAPI write_thread(void *unused)
{
	Shot *s = &g_shot;
	char path[MAX_PATH], full[MAX_PATH];
	int ok;

	/* GL rows come bottom-up as R, G, B; both files want top-down B, G, R */
	for (int y = 0; y < s->h / 2; y++) {
		u8 *a = s->pixels + y * s->stride, *b = s->pixels + (s->h - 1 - y) * s->stride;
		for (int i = 0; i < s->stride; i++) { u8 t = a[i]; a[i] = b[i]; b[i] = t; }
	}
	for (int y = 0; y < s->h; y++) {
		u8 *p = s->pixels + y * s->stride;
		for (int x = 0; x < s->w; x++, p += 3) { u8 t = p[0]; p[0] = p[2]; p[2] = t; }
	}

	CreateDirectoryA(SHOT_DIR, 0);
	if (gdiplus_ready()) {
		shot_name(path, ".png");
		ok = save_png(s, path);
		if (!ok) DeleteFileA(path);
	} else ok = 0;
	if (!ok) {
		shot_name(path, ".bmp");
		ok = save_bmp(s, path);
	}
	if (!GetFullPathNameA(path, sizeof full, full, 0)) str_cpy(full, sizeof full, path);
	if (ok) log_msg("screenshot: %dx%d saved to %s", s->w, s->h, full);
	else    log_msg("screenshot: couldn't write %s", full);

	VirtualFree(s->pixels, 0, 0x8000 /* MEM_RELEASE */);
	s->pixels = 0;
	if (ok) g_done = 1;
	g_busy = 0;
	return 0;
}

/* ---- Capture ------------------------------------------------------------------------------ */

static int gl_ready(void)
{
	if (!g_gl_state) {
		HANDLE gl = GetModuleHandleA("opengl32.dll");
		g_gl_state = 2;
		if (gl) {
			gl_ReadPixels = GetProcAddress(gl, "glReadPixels");
			gl_ReadBuffer = GetProcAddress(gl, "glReadBuffer");
			gl_PixelStorei = GetProcAddress(gl, "glPixelStorei");
			gl_GetIntegerv = GetProcAddress(gl, "glGetIntegerv");
			gl_GetError = GetProcAddress(gl, "glGetError");
			if (gl_ReadPixels && gl_ReadBuffer && gl_PixelStorei && gl_GetIntegerv && gl_GetError) g_gl_state = 1;
		}
		if (g_gl_state != 1) log_msg("screenshot: opengl32 functions missing, F12 does nothing");
	}
	return g_gl_state == 1;
}

static void capture(void *dc)
{
	static const u32 pack[] = { GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS };
	int saved[4], read_buffer = GL_BACK, w, h, stride;
	RECT r;
	void *win = WindowFromDC(dc);
	u8 *pixels;

	if (!win || !GetClientRect(win, &r)) { log_msg("screenshot: no window for the frame"); return; }
	w = r.right - r.left;
	h = r.bottom - r.top;
	if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return;
	stride = (w * 3 + 3) & ~3;
	pixels = VirtualAlloc(0, (u32)(stride * h), 0x3000 /* MEM_COMMIT | MEM_RESERVE */, 4 /* PAGE_READWRITE */);
	if (!pixels) { log_msg("screenshot: out of memory for %dx%d", w, h); return; }

	while (gl_GetError()) {}   /* someone else's error mustn't look like ours */
	for (int i = 0; i < 4; i++) gl_GetIntegerv(pack[i], &saved[i]);
	gl_GetIntegerv(GL_READ_BUFFER, &read_buffer);
	gl_PixelStorei(GL_PACK_ALIGNMENT, 4);
	gl_PixelStorei(GL_PACK_ROW_LENGTH, 0);
	gl_PixelStorei(GL_PACK_SKIP_ROWS, 0);
	gl_PixelStorei(GL_PACK_SKIP_PIXELS, 0);
	gl_ReadBuffer(GL_BACK);
	gl_ReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels);
	u32 err = gl_GetError();
	gl_ReadBuffer((u32)read_buffer);
	for (int i = 0; i < 4; i++) gl_PixelStorei(pack[i], saved[i]);
	if (err) {
		log_msg("screenshot: glReadPixels failed (0x%x)", err);
		VirtualFree(pixels, 0, 0x8000);
		return;
	}

	g_shot.pixels = pixels;
	g_shot.w = w;
	g_shot.h = h;
	g_shot.stride = stride;
	g_busy = 1;
	if (!CreateThread(0, 0, write_thread, 0, 0, 0)) {
		log_msg("screenshot: couldn't start the writer");
		VirtualFree(pixels, 0, 0x8000);
		g_shot.pixels = 0;
		g_busy = 0;
	}
}

/* Stands in for SwapBuffers at both present calls: same stdcall signature */
static int WINAPI swap_buffers(void *dc)
{
	if (g_done) {   /* the game-saved sound, as chat's mp_sound plays it */
		static int warned;
		u8 *sound = SSrImpulse_GetByName("autosave");
		g_done = 0;
		if (sound) SSrImpulse_Play(sound, 0, 0, 0, 0);
		else if (!warned++) log_msg("screenshot: impulse autosave not found, no sound");
	}
	if (g_request) {
		g_request = 0;
		if (!g_busy && gl_ready()) capture(dc);
	}
	return (*(SwapBuffersFn *)ONcSwapBuffersImport)(dc);
}

void screenshot_install(void)
{
	static const u8 expected[6] = { 0xff, 0x15, 0x38, 0x90, 0x52, 0x00 };
	for (u32 i = 0; i < sizeof g_sites / sizeof g_sites[0]; i++)
		if (!code_matches(g_sites[i], expected, sizeof expected)) {
			log_msg("screenshot: SwapBuffers call at 0x%x isn't the expected one, F12 off", g_sites[i]);
			return;
		}
	for (u32 i = 0; i < sizeof g_sites / sizeof g_sites[0]; i++) {
		u8 call[6] = { 0xe8, 0, 0, 0, 0, 0x90 };
		u32 rel = (u32)swap_buffers - (g_sites[i] + 5);
		memcpy(call + 1, &rel, 4);
		patch_write(g_sites[i], call, sizeof call);
	}
	if (!CreateThread(0, 0, key_thread, 0, 0, 0)) { log_msg("screenshot: couldn't start the F12 watcher"); return; }
	log_msg("screenshot: patched (F12)");
}
