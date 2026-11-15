/* Test builds: a build of any commit that testers play beside their release, never touching it.
 *
 *   AE\Arklite.dll           the release, as installed
 *   AE\Arklite.ini, saves\, logs\, persist.dat   the release's: a test build never opens them
 *   AE\realbink.dll          the loader (loader.c): runs AE\arklite-test\Arklite.dll while
 *                            AE\arklite-test\active exists, else AE\Arklite.dll
 *   AE\arklite-test\         Arklite.dll (built with ARKLITE_TEST), build.txt ("<commit> <date>"),
 *                            active, and the test build's own Arklite.ini, logs\, saves\ and
 *                            persist.dat (copies of the release's at the first install-test)
 *
 * The test build's own files go there through OWN_DIR (arklite.h); Oni's persist.dat (settings
 * and the original levels' save points) through its 16 references to the name, pointed at
 * arklite-test\persist.dat before Oni reads it (test_build_install). Shared: maps, the level0
 * cache, screenshots (Oni's data and the player's pictures, the same for both builds).
 *
 * A test build shows "Arklite test <commit> · <date>" at the bottom of every frame (menus, game,
 * photo mode, so recordings and F12 screenshots carry it) and in the window title, and talks to
 * the dev helper (port 27781: its own server list, and problem reports filed by commit).
 * Options > More... switches between the builds (test_build_choose) when a test build is there. */
#include "arklite.h"

__declspec(dllimport) int    WINAPI MessageBoxA(void *, const char *, const char *, u32);
__declspec(dllimport) void   WINAPI ExitProcess(u32);
__declspec(dllimport) int    WINAPI DeleteFileA(const char *);
__declspec(dllimport) int    WINAPI CloseHandle(HANDLE);
__declspec(dllimport) HANDLE WINAPI GetModuleHandleA(const char *);
__declspec(dllimport) void * WINAPI GetProcAddress(HANDLE, const char *);
__declspec(dllimport) void * WINAPI WindowFromDC(void *);
__declspec(dllimport) int    WINAPI SetWindowTextA(void *, const char *);
__declspec(dllimport) int    WINAPI ReadFile(HANDLE, void *, u32, u32 *, void *);
typedef struct { int left, top, right, bottom; } RECT;
__declspec(dllimport) int    WINAPI GetClientRect(void *, RECT *);

#define NET_HELPER_RELEASE "51.20.130.46:27780"   /* the public helper (releases) */
#define NET_HELPER_DEV     "51.20.130.46:27781"   /* the dev helper (test builds) */

/* [net] helper. A test build's options start as a copy of the release's, so the public helper
 * written there (or the default) means the dev one; any other address stays as set. */
void net_helper_address(char *out, u32 size)
{
#ifdef ARKLITE_TEST
	GetPrivateProfileStringA("net", "helper", NET_HELPER_DEV, out, size, ini_path());
	if (str_ieq_n(out, NET_HELPER_RELEASE, sizeof NET_HELPER_RELEASE)) str_cpy(out, size, NET_HELPER_DEV);
#else
	GetPrivateProfileStringA("net", "helper", NET_HELPER_RELEASE, out, size, ini_path());
#endif
}

/* ---- The build switch (both builds: Options > More...) ------------------------------------ */

#define TEST_DLL    TEST_DIR "\\Arklite.dll"
#define TEST_ACTIVE TEST_DIR "\\active"
#define TEST_INFO   TEST_DIR "\\build.txt"

int test_build_installed(char *desc, u32 size)
{
	if (desc && size) desc[0] = 0;
	if (GetFileAttributesA(TEST_DLL) == 0xffffffffu) return 0;
	if (desc && size > 1) {   /* the first line of build.txt (install-test writes it from the zip) */
		u32 got = 0;
		HANDLE f = CreateFileA(TEST_INFO, 0x80000000 /* GENERIC_READ */, 3, 0, 3 /* OPEN_EXISTING */, 0x80, 0);
		if (f != INVALID_HANDLE_VALUE) {
			if (!ReadFile(f, desc, size - 1, &got, 0)) got = 0;
			CloseHandle(f);
		}
		desc[got] = 0;
		for (u32 i = 0; i < got; i++)
			if (desc[i] == '\r' || desc[i] == '\n') { desc[i] = 0; break; }
	}
	return 1;
}

int test_build_chosen(void) { return GetFileAttributesA(TEST_ACTIVE) != 0xffffffffu; }

int test_build_choose(int on)
{
	if (!on) return DeleteFileA(TEST_ACTIVE) || !test_build_chosen();
	if (!test_build_installed(0, 0)) return 0;
	HANDLE f = CreateFileA(TEST_ACTIVE, 0x40000000 /* GENERIC_WRITE */, 0, 0, 2 /* CREATE_ALWAYS */, 0x80, 0);
	if (f == INVALID_HANDLE_VALUE) return 0;
	CloseHandle(f);
	return 1;
}

#ifdef ARKLITE_TEST

#ifndef ARKLITE_COMMIT
#define ARKLITE_COMMIT "?"
#endif
#ifndef ARKLITE_COMMIT_DATE
#define ARKLITE_COMMIT_DATE "?"
#endif

/* ---- Oni's persist.dat in the test folder ------------------------------------------------- *
 * Every reference to the name "persist.dat" (0x557194) is a `mov ecx, 0x557194` before
 * BFrFile_FOpen (0x401820) in the persist functions (0x50ee80..0x50f640); BFrFileRef_Set takes a
 * path with a folder (it only refuses '/', and the folder must exist). All 16 or none: a test
 * build that would write the release's save file doesn't run. */
#define ONcPersistName 0x557194
static const u32 g_persist_refs[] = {
	0x50ee88, 0x50eff8, 0x50f087, 0x50f0ec, 0x50f13a, 0x50f1b5, 0x50f205, 0x50f27a,
	0x50f2c8, 0x50f3ab, 0x50f419, 0x50f4b6, 0x50f52b, 0x50f59f, 0x50f5f5, 0x50f635,
};
static const char g_persist_path[] = TEST_DIR "\\persist.dat";

void test_build_install(void)
{
	static const u8 mov[5] = { 0xb9, ONcPersistName & 0xff, ONcPersistName >> 8 & 0xff, ONcPersistName >> 16 & 0xff, ONcPersistName >> 24 };
	u32 n = sizeof g_persist_refs / sizeof g_persist_refs[0], bad = 0, to = (u32)g_persist_path;

	for (u32 i = 0; i < n; i++)
		if (!code_matches(g_persist_refs[i], mov, 5)) bad++;
	if (bad) {
		log_msg("test build: %u of Oni's %u references to persist.dat aren't where they were: closing, so the release's "
		        "save file stays untouched", bad, n);
		MessageBoxA(0, "This Arklite test build can't keep its save file apart from the release's in this Oni.exe, "
		               "so Oni closes.\n\nOptions > More... can't switch back from here: delete AE\\" TEST_DIR "\\active "
		               "(or run uninstall-test) to play the release.", "Arklite test build", 0x10 /* MB_ICONERROR */);
		ExitProcess(1);
	}
	for (u32 i = 0; i < n; i++) patch_write(g_persist_refs[i] + 1, &to, 4);
	log_msg("test build %s (%s): options, logs, saves and persist.dat in AE\\" TEST_DIR "\\; the dev helper", ARKLITE_COMMIT,
	        ARKLITE_COMMIT_DATE);
}

/* ---- The label ------------------------------------------------------------------------------ *
 * Drawn with OpenGL right before the swap (screenshot.c's hook, so F12 shots have it), in pixel
 * quads from a 5x7 font (no texture: Oni names its own GL textures), every GL state it changes
 * pushed and popped. */
#define GL_VIEWPORT          0x0ba2
#define GL_MODELVIEW         0x1700
#define GL_PROJECTION        0x1701
#define GL_TEXTURE           0x1702
#define GL_ENABLE_BIT        0x00002000
#define GL_COLOR_BUFFER_BIT  0x00004000
#define GL_CURRENT_BIT       0x00000001
#define GL_POLYGON_BIT       0x00000008
#define GL_VIEWPORT_BIT      0x00000800
#define GL_TRANSFORM_BIT     0x00001000
#define GL_TEXTURE_1D        0x0de0
#define GL_TEXTURE_2D        0x0de1
#define GL_DEPTH_TEST        0x0b71
#define GL_CULL_FACE         0x0b44
#define GL_LIGHTING          0x0b50
#define GL_FOG               0x0b60
#define GL_ALPHA_TEST        0x0bc0
#define GL_SCISSOR_TEST      0x0c11
#define GL_STENCIL_TEST      0x0b90
#define GL_BLEND             0x0be2
#define GL_SRC_ALPHA         0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_FRONT_AND_BACK    0x0408
#define GL_FILL              0x1b02
#define GL_ACTIVE_TEXTURE    0x84e0
#define GL_MAX_TEXTURE_UNITS 0x84e2
#define GL_TEXTURE0          0x84c0

static struct {
	int state;   /* 0 not looked up, 1 ready, 2 missing */
	void (WINAPI *GetIntegerv)(u32, int *);
	void (WINAPI *PushAttrib)(u32);
	void (WINAPI *PopAttrib)(void);
	void (WINAPI *MatrixMode)(u32);
	void (WINAPI *PushMatrix)(void);
	void (WINAPI *PopMatrix)(void);
	void (WINAPI *LoadIdentity)(void);
	void (WINAPI *Ortho)(double, double, double, double, double, double);
	void (WINAPI *Viewport)(int, int, int, int);
	void (WINAPI *Disable)(u32);
	void (WINAPI *Enable)(u32);
	void (WINAPI *BlendFunc)(u32, u32);
	void (WINAPI *PolygonMode)(u32, u32);
	void (WINAPI *Color4f)(float, float, float, float);
	void (WINAPI *Rectf)(float, float, float, float);
	void (WINAPI *ActiveTexture)(u32);   /* GL 1.3 / ARB_multitexture, if there */
} gl;

static int gl_load(void)
{
	HANDLE m;
	void *(WINAPI *wgl_proc)(const char *);
	if (gl.state) return gl.state == 1;
	gl.state = 2;
	if (!(m = GetModuleHandleA("opengl32.dll"))) return 0;
#define L(f, n) if (!(*(void **)&gl.f = GetProcAddress(m, n))) return 0
	L(GetIntegerv, "glGetIntegerv"); L(PushAttrib, "glPushAttrib"); L(PopAttrib, "glPopAttrib");
	L(MatrixMode, "glMatrixMode"); L(PushMatrix, "glPushMatrix"); L(PopMatrix, "glPopMatrix");
	L(LoadIdentity, "glLoadIdentity"); L(Ortho, "glOrtho"); L(Viewport, "glViewport"); L(Disable, "glDisable");
	L(Enable, "glEnable"); L(BlendFunc, "glBlendFunc"); L(PolygonMode, "glPolygonMode"); L(Color4f, "glColor4f");
	L(Rectf, "glRectf");
#undef L
	wgl_proc = (void *)GetProcAddress(m, "wglGetProcAddress");
	if (wgl_proc) {
		*(void **)&gl.ActiveTexture = wgl_proc("glActiveTextureARB");
		if (!gl.ActiveTexture) *(void **)&gl.ActiveTexture = wgl_proc("glActiveTexture");
	}
	gl.state = 1;
	return 1;
}

/* 5x7 glyphs, a row a byte (bit 4 the left column): the label's letters, hex digits, '-', ' ' and '|' as
 * the middle dot */
static const struct { char c; u8 row[7]; } g_font[] = {
	{ '0', { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e } }, { '1', { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e } },
	{ '2', { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f } }, { '3', { 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e } },
	{ '4', { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 } }, { '5', { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e } },
	{ '6', { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e } }, { '7', { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
	{ '8', { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e } }, { '9', { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c } },
	{ 'a', { 0x00, 0x00, 0x0e, 0x01, 0x0f, 0x11, 0x0f } }, { 'b', { 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1e } },
	{ 'c', { 0x00, 0x00, 0x0e, 0x10, 0x10, 0x11, 0x0e } }, { 'd', { 0x01, 0x01, 0x0d, 0x13, 0x11, 0x11, 0x0f } },
	{ 'e', { 0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0e } }, { 'f', { 0x06, 0x09, 0x08, 0x1c, 0x08, 0x08, 0x08 } },
	{ 'i', { 0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e } }, { 'k', { 0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12 } },
	{ 'l', { 0x0c, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e } }, { 'r', { 0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10 } },
	{ 's', { 0x00, 0x00, 0x0e, 0x10, 0x0e, 0x01, 0x1e } }, { 't', { 0x08, 0x08, 0x1c, 0x08, 0x08, 0x09, 0x06 } },
	{ 'A', { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 } }, { '-', { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 } },
	{ '|', { 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00 } },
};

static const char g_label[] = "Arklite test " ARKLITE_COMMIT " | " ARKLITE_COMMIT_DATE;

static void draw_text(float x, float y, float px)
{
	for (const char *c = g_label; *c; c++, x += 6 * px) {
		const u8 *row = 0;
		for (u32 g = 0; g < sizeof g_font / sizeof g_font[0]; g++)
			if (g_font[g].c == *c) { row = g_font[g].row; break; }
		if (!row) continue;
		for (int r = 0; r < 7; r++)
			for (int k = 0; k < 5; k++)
				if (row[r] & (0x10 >> k)) gl.Rectf(x + k * px, y + r * px, x + (k + 1) * px, y + (r + 1) * px);
	}
}

static void draw_label(int w, int h)
{
	int units = 1, active = 0;
	float px = (float)(h / 480 < 2 ? 2 : h / 480 > 4 ? 4 : h / 480);
	float tw = (float)(sizeof g_label - 1) * 6 * px - px, x = (float)(int)((w - tw) / 2), y = h - 9 * px;
	float sh = px > 2 ? px / 2 : 1;

	gl.PushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_POLYGON_BIT | GL_VIEWPORT_BIT | GL_TRANSFORM_BIT);
	if (gl.ActiveTexture) {   /* every unit's texturing off (GL_ENABLE_BIT saved them all) */
		gl.GetIntegerv(GL_ACTIVE_TEXTURE, &active);
		gl.GetIntegerv(GL_MAX_TEXTURE_UNITS, &units);
		if (units < 1 || units > 8) units = 1;
		for (int u = 0; u < units; u++) {
			gl.ActiveTexture(GL_TEXTURE0 + u);
			gl.Disable(GL_TEXTURE_1D);
			gl.Disable(GL_TEXTURE_2D);
		}
		gl.ActiveTexture(GL_TEXTURE0);
	} else {
		gl.Disable(GL_TEXTURE_1D);
		gl.Disable(GL_TEXTURE_2D);
	}
	gl.Disable(GL_DEPTH_TEST); gl.Disable(GL_CULL_FACE); gl.Disable(GL_LIGHTING); gl.Disable(GL_FOG);
	gl.Disable(GL_ALPHA_TEST); gl.Disable(GL_SCISSOR_TEST); gl.Disable(GL_STENCIL_TEST);
	gl.Enable(GL_BLEND);
	gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	gl.Viewport(0, 0, w, h);
	gl.MatrixMode(GL_TEXTURE); gl.PushMatrix(); gl.LoadIdentity();
	gl.MatrixMode(GL_PROJECTION); gl.PushMatrix(); gl.LoadIdentity(); gl.Ortho(0, w, h, 0, -1, 1);
	gl.MatrixMode(GL_MODELVIEW); gl.PushMatrix(); gl.LoadIdentity();

	gl.Color4f(0, 0, 0, 0.6f);
	draw_text(x + sh, y + sh, px);
	gl.Color4f(1, 1, 1, 0.8f);
	draw_text(x, y, px);

	gl.MatrixMode(GL_MODELVIEW); gl.PopMatrix();
	gl.MatrixMode(GL_PROJECTION); gl.PopMatrix();
	gl.MatrixMode(GL_TEXTURE); gl.PopMatrix();
	if (gl.ActiveTexture) gl.ActiveTexture((u32)active);
	gl.PopAttrib();   /* (the matrix mode with GL_TRANSFORM_BIT) */
}

void test_build_present(void *dc)
{
	static void *titled;
	void *win = WindowFromDC(dc);
	RECT r;
	if (win && win != titled) {   /* once per window (Oni makes a new one when the screen mode changes) */
		titled = win;
		SetWindowTextA(win, "ONI - Arklite test " ARKLITE_COMMIT " (" ARKLITE_COMMIT_DATE ")");
	}
	if (!win || !GetClientRect(win, &r) || r.right <= 0 || r.bottom <= 0 || !gl_load()) return;
	draw_label(r.right, r.bottom);
}

#else   /* a release */

void test_build_install(void) {}
void test_build_present(void *dc) {}

#endif
